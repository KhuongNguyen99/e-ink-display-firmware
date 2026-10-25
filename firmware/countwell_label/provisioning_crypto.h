#pragma once
#include "protocol.h"
#include <stdio.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

namespace countwell {
// These operations run only on loopTask. Bluetooth callbacks expose cached public
// values and enqueue commands; they never perform elliptic-curve operations.
inline void encodeHex(const uint8_t *value, size_t size, char *out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) { out[2*i] = digits[value[i] >> 4]; out[2*i+1] = digits[value[i] & 15]; }
  out[2*size] = 0;
}
inline bool decodeHex(const char *text, uint8_t *out, size_t size) {
  if (!hex(text, size * 2)) return false;
  for (size_t i = 0; i < size; ++i) {
    const char a = text[2*i], b = text[2*i+1];
    out[i] = uint8_t(((a <= '9' ? a-'0' : a-'a'+10) << 4) | (b <= '9' ? b-'0' : b-'a'+10));
  }
  return true;
}
inline int setupSha256(const uint8_t *value, size_t size, uint8_t *out) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256(value, size, out, 0);
#else
  return mbedtls_sha256_ret(value, size, out, 0);
#endif
}
struct ProvisionEnvelope {
  const char *authority, *box, *generation, *nonce, *serverPublic, *iv, *ciphertext, *tag, *signature;
};
class ProvisionCrypto {
  mbedtls_ecp_group group;
  mbedtls_mpi ephemeral, claim;
  mbedtls_ecp_point authority;
  int (*rng)(void *, unsigned char *, size_t);
  void *rngContext;
 public:
  bool ready = false, active = false;
  char authorityId[17] = {}, claimPublic[131] = {}, devicePublic[131] = {};
  char nonce[33] = {}, deviceSignature[129] = {};
  ProvisionCrypto(int (*random)(void *, unsigned char *, size_t), void *context = nullptr)
      : rng(random), rngContext(context) {
    mbedtls_ecp_group_init(&group); mbedtls_mpi_init(&ephemeral);
    mbedtls_mpi_init(&claim); mbedtls_ecp_point_init(&authority);
  }
  ~ProvisionCrypto() {
    mbedtls_ecp_point_free(&authority); mbedtls_mpi_free(&claim);
    mbedtls_mpi_free(&ephemeral); mbedtls_ecp_group_free(&group);
  }
  ProvisionCrypto(const ProvisionCrypto &) = delete;
  ProvisionCrypto &operator=(const ProvisionCrypto &) = delete;
  bool initialize(const char *authorityHex) {
    ready = false;
    uint8_t point[65], digest[32];
    if (!decodeHex(authorityHex, point, sizeof(point)) || point[0] != 4 ||
        mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) ||
        mbedtls_ecp_point_read_binary(&group, &authority, point, sizeof(point)) ||
        mbedtls_ecp_check_pubkey(&group, &authority) || setupSha256(point, sizeof(point), digest)) return false;
    encodeHex(digest, 8, authorityId); ready = true; return true;
  }
  bool loadClaim(const uint8_t scalar[32]) {
    if (!ready || mbedtls_mpi_read_binary(&claim, scalar, 32) || mbedtls_ecp_check_privkey(&group, &claim)) return false;
    mbedtls_ecp_point pub; mbedtls_ecp_point_init(&pub);
    const bool ok = !mbedtls_ecp_mul(&group, &pub, &claim, &group.G, rng, rngContext) && publicHex(pub, claimPublic);
    mbedtls_ecp_point_free(&pub); return ok;
  }
  bool generateClaim(uint8_t scalar[32]) {
    mbedtls_ecp_point pub; mbedtls_ecp_point_init(&pub);
    const bool ok = ready && !mbedtls_ecp_gen_keypair(&group, &claim, &pub, rng, rngContext) &&
        !mbedtls_mpi_write_binary(&claim, scalar, 32) && publicHex(pub, claimPublic);
    mbedtls_ecp_point_free(&pub); return ok;
  }
  void close() {
    active = false; mbedtls_mpi_free(&ephemeral); mbedtls_mpi_init(&ephemeral);
    memset(devicePublic, 0, sizeof(devicePublic)); memset(nonce, 0, sizeof(nonce));
    memset(deviceSignature, 0, sizeof(deviceSignature));
  }
  bool open(const char *device, const char *profile) {
    close(); uint8_t randomNonce[16];
    mbedtls_ecp_point pub; mbedtls_ecp_point_init(&pub);
    bool ok = ready && claimPublic[0] && !rng(rngContext, randomNonce, sizeof(randomNonce)) &&
        !mbedtls_ecp_gen_keypair(&group, &ephemeral, &pub, rng, rngContext) && publicHex(pub, devicePublic);
    mbedtls_ecp_point_free(&pub);
    if (ok) {
      encodeHex(randomNonce, sizeof(randomNonce), nonce);
      char transcript[320];
      const int n = snprintf(transcript, sizeof(transcript), "CW3|setup|%s|%s|%s|%s|%s",
          authorityId, device, profile, nonce, devicePublic);
      ok = n > 0 && size_t(n) < sizeof(transcript) && sign(transcript, deviceSignature);
    }
    if (!ok) close(); else active = true;
    return ok;
  }
  bool decrypt(const ProvisionEnvelope &envelope, const char *device, const char *profile, uint8_t key[32]) {
    memset(key, 0, 32);
    if (!ready || !active || !hex(envelope.authority, 16) || strcmp(envelope.authority, authorityId) ||
        !uuid(envelope.box) || !hex(envelope.generation, 16) || !hex(envelope.nonce, 32) || strcmp(envelope.nonce, nonce) ||
        !hex(envelope.serverPublic, 130) || strncmp(envelope.serverPublic, "04", 2) || !hex(envelope.iv, 24) ||
        !hex(envelope.ciphertext, 64) || !hex(envelope.tag, 32) || !hex(envelope.signature, 128)) return false;
    char context[512], transcript[612];
    const int n = snprintf(context, sizeof(context), "CW3|provision|%s|%s|%s|%s|%s|%s|%s|%s|%s", authorityId,
        device, profile, envelope.box, envelope.generation, nonce, devicePublic, envelope.serverPublic, envelope.iv);
    const int m = snprintf(transcript, sizeof(transcript), "%s|%s|%s", context, envelope.ciphertext, envelope.tag);
    if (n <= 0 || size_t(n) >= sizeof(context) || m <= 0 || size_t(m) >= sizeof(transcript) ||
        !verify(transcript, envelope.signature)) return false;
    uint8_t peerBytes[65], salt[16], iv[12], ciphertext[32], tag[16], shared[32] = {}, aesKey[32] = {};
    decodeHex(envelope.serverPublic, peerBytes, sizeof(peerBytes)); decodeHex(nonce, salt, sizeof(salt));
    decodeHex(envelope.iv, iv, sizeof(iv)); decodeHex(envelope.ciphertext, ciphertext, sizeof(ciphertext));
    decodeHex(envelope.tag, tag, sizeof(tag));
    mbedtls_ecp_point peer; mbedtls_ecp_point_init(&peer);
    mbedtls_mpi secret; mbedtls_mpi_init(&secret);
    mbedtls_gcm_context gcm; mbedtls_gcm_init(&gcm);
    const bool ok = !mbedtls_ecp_point_read_binary(&group, &peer, peerBytes, sizeof(peerBytes)) &&
        !mbedtls_ecp_check_pubkey(&group, &peer) &&
        !mbedtls_ecdh_compute_shared(&group, &secret, &peer, &ephemeral, rng, rngContext) &&
        !mbedtls_mpi_write_binary(&secret, shared, sizeof(shared)) &&
        !mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, sizeof(salt), shared, sizeof(shared),
            reinterpret_cast<const uint8_t *>(context), size_t(n), aesKey, sizeof(aesKey)) &&
        !mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aesKey, 256) &&
        !mbedtls_gcm_auth_decrypt(&gcm, sizeof(ciphertext), iv, sizeof(iv), reinterpret_cast<const uint8_t *>(context),
            size_t(n), tag, sizeof(tag), ciphertext, key);
    mbedtls_gcm_free(&gcm); mbedtls_mpi_free(&secret); mbedtls_ecp_point_free(&peer);
    mbedtls_platform_zeroize(shared, sizeof(shared)); mbedtls_platform_zeroize(aesKey, sizeof(aesKey));
    if (!ok) mbedtls_platform_zeroize(key, 32);
    return ok;
  }
 private:
  bool publicHex(const mbedtls_ecp_point &point, char *output) {
    uint8_t bytes[65]; size_t size = 0;
    if (mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &size, bytes, sizeof(bytes)) || size != 65) return false;
    encodeHex(bytes, size, output); return true;
  }
  bool sign(const char *message, char *signature) {
    uint8_t digest[32], raw[64]; mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    const bool ok = !setupSha256(reinterpret_cast<const uint8_t *>(message), strlen(message), digest) &&
        !mbedtls_ecdsa_sign(&group, &r, &s, &claim, digest, sizeof(digest), rng, rngContext) &&
        !mbedtls_mpi_write_binary(&r, raw, 32) && !mbedtls_mpi_write_binary(&s, raw + 32, 32);
    if (ok) encodeHex(raw, sizeof(raw), signature);
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); return ok;
  }
  bool verify(const char *message, const char *signature) {
    uint8_t digest[32], raw[64]; mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    const bool ok = decodeHex(signature, raw, sizeof(raw)) &&
        !setupSha256(reinterpret_cast<const uint8_t *>(message), strlen(message), digest) &&
        !mbedtls_mpi_read_binary(&r, raw, 32) && !mbedtls_mpi_read_binary(&s, raw + 32, 32) &&
        !mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &authority, &r, &s);
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); return ok;
  }
};
}  // namespace countwell
