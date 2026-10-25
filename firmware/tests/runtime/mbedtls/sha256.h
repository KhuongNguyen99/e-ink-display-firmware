#pragma once
#include_next <mbedtls/sha256.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
inline int mbedtls_sha256_ret(const unsigned char *p, size_t n, unsigned char *out, int mode) { return mbedtls_sha256(p, n, out, mode); }
#endif
