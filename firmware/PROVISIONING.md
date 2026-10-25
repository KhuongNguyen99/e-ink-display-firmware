# Encrypted phone provisioning v1

Version 3.2.0 keeps protocol 2 label updates and adds the read-only Setup
characteristic `8f7c0006-4a9b-4f3d-9c21-6b8e2d5a1000`. No Android bonding is used.

## Installation trust

The backend retains a P-256 ECDSA authority private key in its persistent data
volume. `build.ps1 -ProvisioningPublicKey` embeds its 65-byte SEC1 uncompressed
public key as 130 lowercase hexadecimal characters. `authority_key_id` is the
first 16 lowercase hexadecimal characters of SHA-256 of those 65 bytes.

On first boot with an authority anchor, the device generates a separate P-256
claim signing key and saves its 32-byte private scalar under `cwble2/claimkey`.
The existing `pair`, frame/name slots and `bleaddr` are preserved. An existing
invalid claim key fails closed and is never silently regenerated. Read-only
`usb_enrollment` exports only `claim_public_key`, `device_id`, `panel_profile`
and `authority_key_id` inside the normal USB response's `enrollment` object.
Register this public enrollment through the trusted firmware-installation
connection, using `python scripts/enroll_display.py --port COM3 --container
yoloe-counter-counter-1`. The installer uses local Docker administration, not
an HTTP enrollment endpoint. The backend must reject unknown or mismatched
device claim keys.

## Setup statement

A five-second Menu hold opens a two-minute window, creates a fresh P-256 ECDH
key pair and a 16-byte random nonce, and disconnects a pre-existing connection.
Back cancels the window. Setup reads return:

```json
{"protocol":1,"device_id":"cw-...","panel_profile":"ssd1680","authority_key_id":"<16 hex>","setup_open":true,"setup_nonce":"<32 hex>","device_public_key":"<130 hex>","device_signature":"<128 hex>"}
```

The last three fields are absent while closed. `device_signature` is raw
32-byte `r` followed by 32-byte `s`, hex encoded. The claim key signs SHA-256
of the exact UTF-8 string, without a trailing newline:

```text
CW3|setup|authority_key_id|device_id|panel_profile|setup_nonce|device_public_key
```

The backend verifies this signature before preparing a provisioning envelope.
There is no private or permanent application key in any identity response.

## Provisioning envelope

The backend generates an ephemeral P-256 ECDH pair and a random 12-byte IV.
The request is flat JSON with exactly these ten fields:

```json
{"op":"provision","authority_key_id":"<16 hex>","box_id":"<lowercase UUID>","generation":"<16 hex>","setup_nonce":"<32 hex>","server_public_key":"<130 hex>","iv":"<24 hex>","ciphertext":"<64 hex>","tag":"<32 hex>","signature":"<128 hex>"}
```

All hexadecimal strings are lowercase. Both public keys use SEC1 uncompressed
encoding (`04 || X32 || Y32`). The canonical context is:

```text
CW3|provision|authority_key_id|device_id|panel_profile|box_id|generation|setup_nonce|device_public_key|server_public_key|iv
```

1. Compute ECDH and encode the shared X coordinate as exactly 32 big-endian bytes.
2. HKDF-SHA256 derives 32 bytes with the decoded 16-byte setup nonce as salt,
   the shared secret as input key material, and context UTF-8 bytes as info.
3. AES-256-GCM encrypts the raw 32-byte permanent application key with that
   derived key, the decoded IV, and context UTF-8 bytes as AAD. The tag is 16 bytes.
4. The backend authority signs SHA-256 of `context|ciphertext|tag`. Encode the
   ECDSA signature as raw `r32 || s32`, then lowercase hex.

The device validates all lengths, authority ID, current nonce, public point,
ECDSA signature, setup window, connection and GCM tag. It accepts an unbound
device or a byte-for-byte identical existing binding. It never overwrites
a different binding over BLE. A successful authorization consumes the window
before NVS persistence; storage failure requires another Menu hold.

After saving, Status exposes the existing `paired` HMAC receipt. The backend
must verify that receipt against its staged application key before atomically
switching the box binding and retiring its old display. Reconnecting recovers
the `paired` receipt if its first read was lost. Fresh label jobs then use the
existing signed protocol 2. Public metadata and label traffic remain visible
on the unencrypted BLE transport; only provisioning key confidentiality is
added by this exchange.

`tests/provisioning_vector.json` is a public interoperability fixture with
deliberately trivial private scalars. It must never be used for deployments.
