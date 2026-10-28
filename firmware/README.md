# Inventory Counter Bluetooth labels

Firmware for the Elecrow CrowPanel ESP32-S3 2.13-inch e-paper display. An Android phone running Inventory Counter in Chrome sends labels over BLE. The board does not join Wi-Fi or open a setup hotspot.

## Panel and build

Identify the fitted display controller before flashing. USB identity and the ESP32-S3 marking do not identify the panel.

| Profile | Vendor reference | BUSY active level |
| --- | --- | --- |
| `ssd1680` | Original `example/arduino` panel | HIGH |
| `jd79661` | `example/arduino-v1.2` panel | LOW |

The connected demo board was identified as **SSD1680** by an exact match between its original factory application and Elecrow's V1.0 factory image. Both profiles remain available for other boards. See [provenance](NOTICE.md). A factory backup may contain private settings; do not publish it.

Build firmware 3.8.0 from this directory with the pinned Arduino ESP32 **2.0.10** component and a rebuilt ESP-IDF **4.4.5** SDK. The build requires Docker and Git:

```powershell
$publicKey = '<authority-public-key>'
.\tests\run.ps1
.\build-idf.ps1 -Panel ssd1680 -ProvisioningPublicKey $publicKey
```

Export the installation's public authority key from `GET /api/display-setup/authority` and supply its `public_key` (130 lowercase hexadecimal characters). This is public material; the backend signing private key stays in its persistent data volume. `INVENTORY_COUNTER_PROVISIONING_PUBLIC_KEY` can supply the same build input. The build stages this public anchor under the isolated cache and refuses a missing key. Test fixture keys must never be used for deployed builds.

`build-idf.ps1` rebuilds the SDK with power management, tickless idle, BLE modem sleep and main-crystal retention in light sleep. It uses a separate `C:\cwesp32-idf` cache by default; `-CacheRoot` selects another location. The output is `C:\cwesp32-idf\source-ssd1680\output`, including `flasher_args.json`, referenced binaries, the ELF/map files, `sdkconfig`, and SHA-256 hashes. The older `build.ps1` uses the stock Arduino SDK and cannot provide automatic light sleep for this version. It remains available for comparison, not for installing the 3.8.0 power behavior.

Builds never flash or erase the board. Identify the fitted panel and verify the generated `flasher_args.json` and binary hashes before a separate installation step.

Pins: power7 HIGH, SCK12, MOSI11, RESET10, DC13, CS14, BUSY9, Menu2 and Back1. Use stable USB power. Firmware updates preserve NVS; a full erase removes the device key, box assignment, stable Bluetooth address and cached labels. Version 3 removes legacy Bluetooth bonds while preserving the application key and label records.

## Connect and update

1. Open Inventory Counter's HTTPS address in **Chrome on Android**, then open the box. Bluetooth stays available while the tag is powered. Press **Menu** to show or hide its QR; the QR hides automatically after two minutes. The product, image and count remain visible while the QR is hidden. If the saved label has no trusted QR bounds, hiding it blanks the whole screen.
2. Choose **Update display**, then select the display in Chrome's device picker. Android Bluetooth pairing and a passcode are not required.
3. Inventory Counter sends a signed label update. Later stock changes save immediately and send through the connected phone. If the phone or display is unavailable, the update stays pending; reconnect when nearby.
4. Keep Inventory Counter open while sending a label; browser background delivery is not guaranteed. Idle connections close after 30 seconds. A connection that has not supplied a valid signed update also closes after 30 seconds, even if it keeps reading metadata.

Displays already provisioned with version 2 keep their box assignment and key. Version 3.2.0 adds **Replace display** using Chrome on Android. Hold **Menu for five seconds** on the new display to open its encrypted two-minute setup window, choose Replace display in Inventory Counter, and select the new display. The old display does not need to connect. Back closes setup and restores the saved label. A display already assigned to another box is rejected.

New firmware installation includes a one-time trusted device enrollment: the firmware generates a P-256 device signing key in its existing NVS namespace and exposes only its public key through the read-only USB `usb_enrollment` command. Register that public key with this Inventory Counter installation using `python scripts/enroll_display.py --port COM3 --container yoloe-counter-counter-1` from the repository root. The installer registers through local Docker access; there is no HTTP enrollment endpoint. The device's private signing key stays on the device. Each display needs a one-time firmware installation and public identity registration. After that preparation, setup and replacement use only Chrome on Android: hold Menu for five seconds, with no USB setup, Android Bluetooth bonding, or passcode. Keep the backend authority key and enrollment registry in persistent storage across deployments. A new authority requires a matching firmware build; an erased device requires enrollment again.

The existing USB provisioning operation remains available for recovery during the physical setup window. It cannot run during a Bluetooth connection or display refresh. It never echoes the application key. Phone setup keeps the permanent application key encrypted in transit and confirms the saved binding with a signed receipt before Inventory Counter switches the box to the new display.

The stable box QR opens `/?box=<code>`. Its database record identifies the corresponding display. Web Bluetooth supplies a browser-specific device ID rather than exposing a hardware MAC address, so that ID is a connection hint, not the permanent box binding.

**Find tag** temporarily inverts the label for 30 seconds, keeping the QR area and its white border unchanged. A hidden QR stays hidden. Another Find restarts the timer without redrawing the same highlight. The board restores the normal label even after the phone disconnects; Menu and the two-minute QR timer continue to work. A normal label update ends the highlight. Find does not write the cached label or change stock history.

## Privacy and authorization

Advertisements contain the custom service UUID while the tag is powered. After a successful label update, the Bluetooth name is `E-ink [product name]`, and follows later product renames. The full name is limited to 248 UTF-8 bytes; scan responses contain up to 29 bytes, shortened at a character boundary when necessary. Before the first named update, the name is `E-ink display`. The committed name survives restart with its label. Nearby scanners can see this name while the radio is on. A random static Bluetooth address is generated once and saved in NVS so Chrome can reconnect after a restart without bonding. Inventory Counter filters its device picker by the service; **other BLE scanners can still detect and connect while the tag is powered**.

GATT uses an unbonded, unencrypted connection. Nearby devices can read identity/status and observe label traffic. Authorization comes from the random per-device key shared by the backend and display: each update requires a valid HMAC over its box, generation, profile, revision, complete image hash and a fresh device challenge. The browser relays signed jobs and receipts without receiving the permanent key. Frame data is accepted only after an authorized BEGIN, and the complete hash and padding are checked before any display refresh. This protects update integrity and authorization, not radio confidentiality or availability. The local API remains restricted to the existing trusted network/Tailscale boundary.

Phone provisioning uses authenticated P-256 ECDH, HKDF-SHA256 and AES-256-GCM inside the otherwise unencrypted BLE connection. The backend first verifies the new display's signed setup statement against its trusted enrollment registry. The display verifies the backend's ECDSA signature against the public authority embedded at build time. This prevents an unauthenticated radio intermediary from substituting either side's setup key. The permanent application key is encrypted, never included in cleartext BLE JSON, and never exposed to the browser. Plain BLE `pair` commands remain rejected. USB replies never return either private key. No Android pairing is requested or accepted.

## Transfer protocol

Firmware **3.8.0** preserves wire protocol **2** and these custom UUIDs:

| Characteristic | UUID | Access |
| --- | --- | --- |
| Service | `8f7c0001-4a9b-4f3d-9c21-6b8e2d5a1000` | Advertised |
| Identity | `8f7c0002-4a9b-4f3d-9c21-6b8e2d5a1000` | Read |
| Control | `8f7c0003-4a9b-4f3d-9c21-6b8e2d5a1000` | Write with response |
| Frame | `8f7c0004-4a9b-4f3d-9c21-6b8e2d5a1000` | Write with response |
| Status | `8f7c0005-4a9b-4f3d-9c21-6b8e2d5a1000` | Read / notification hint |
| Setup | `8f7c0006-4a9b-4f3d-9c21-6b8e2d5a1000` | Read |

Control messages are newline-terminated UTF-8 JSON, split into sequential writes of at most 20 bytes. Frame writes contain a little-endian 16-bit offset followed by at most 18 pixel bytes. The browser reads the full status JSON; a GATT write response or short notification is not proof that the display refreshed.

`begin` authenticates an immutable job against a fresh display challenge, box, profile, revision and frame hash. An accepted BEGIN consumes its challenge immediately; connect, disconnect and commit also rotate the challenge. `commit` validates the entire upload before refreshing. The display returns a signed `committed` receipt only after the full refresh and persistent commit succeed. Reading status after reconnect recovers an acknowledgement lost in transit. Identity includes `phone_pairing:false` and `provisioning:"ble-encrypted-v1"`.

Identity advertises `find_tag:true`. A signed `find` command binds the current saved box, generation, profile, revision, frame hash, fresh challenge, 30,000 ms duration and exact saved QR bounds. Its HMAC transcript is `CW2|find|device_id|generation|box_id|panel_profile|revision|sha256|challenge|30000|qr_x|qr_y|qr_size`. The display consumes the challenge before rendering and reports unsigned `finding` with `find_remaining_ms`; it issues a normal committed receipt only after the canonical label and current QR visibility are physically restored. Find and QR redraws share the same bounded refresh and retry path, including while another frame is being uploaded.

Setup exposes `{protocol:1,device_id,panel_profile,authority_key_id,setup_open}` and, only during the physical window, `setup_nonce`, `device_public_key` and `device_signature`. This separate characteristic keeps every GATT value below 512 bytes. The ephemeral private key is generated on Menu hold and cleared when setup closes. The nonce is consumed before saving an accepted binding, including when that write fails. An exact existing binding may be retried; a different existing binding cannot be replaced over BLE. Heavy ECDH/ECDSA work runs in the main loop, outside Bluetooth callbacks. See [the complete encrypted setup contract](PROVISIONING.md).

USB uses 115200 baud and newline-delimited `CWUSB ` followed by a JSON object, limited to 1,024 JSON bytes. The setup helper sends whitespace and blank lines before each command to wake UART reception; the firmware ignores those lines. `{"op":"usb_identity"}` returns `CWUSB {"ok":true,"device_id":"...","identity":{...}}`. During the Menu setup window, `{"op":"usb_pair","box_id":"...","generation":"...","key":"..."}` validates and persists the binding and returns `CWUSB {"ok":true,"device_id":"...","receipt":{...}}` with a signed `paired` receipt for backend confirmation. `{"op":"usb_enrollment"}` returns `CWUSB {"ok":true,"device_id":"...","enrollment":{"device_id":"...","panel_profile":"...","authority_key_id":"...","claim_public_key":"..."}}` without needing Menu. The key is never echoed. Malformed, oversized and unauthorized commands receive a generic error. Reprovisioning the identical binding preserves the saved frame.

HMAC-SHA256 uses the decoded 32-byte key and these exact UTF-8 strings, with lowercase hex values and decimal revision:

```text
CW2|paired|device_id|generation|box_id
CW2|begin|device_id|generation|box_id|panel_profile|revision|sha256|challenge
CW2|committed|device_id|generation|box_id|panel_profile|revision|sha256
```

Firmware 3.1 adds `ble_name_sha256` to identity as a capability and the applied name's fingerprint. For capable displays, `begin` also carries `ble_name`; its UTF-8 SHA-256 is appended to the BEGIN transcript after `challenge`. The matching committed receipt includes `ble_name_sha256`, appended after the frame hash in its transcript. Older firmware receives the original commands. Name metadata is saved beside each frame slot without changing existing frame records, and is activated with the frame only after validation and a successful update. A name-only retry of identical pixels does not refresh the panel again.

Firmware 3.5 adds optional `qr_sleep` to identity: `1` means this firmware can hide the QR but has no trusted bounds for its saved label; `2` means validated QR bounds are saved. Older firmware omits the field and continues to receive its original protocol-2 commands. For a capable display, the backend includes integer `qr_x`, `qr_y` and `qr_size` in BEGIN. Their decimal values are appended to the HMAC transcript after the challenge and optional Bluetooth-name hash: `CW2|begin|device_id|generation|box_id|panel_profile|revision|sha256|challenge[|ble_name_sha256]|qr_x|qr_y|qr_size`. The geometry belongs to the exact signed frame; it is validated and saved in an NVS sidecar beside the active frame, then recovered on restart. A `qr_sleep:1` display needs a signed label update to install that sidecar, even if the pixels are already current. If bounds are missing or invalid when the QR is hidden, the firmware blanks the full display rather than risk leaving a QR visible.

The canonical frame is exactly **4,000 bytes** for **250 × 122** visible pixels. Black is 1 and white is 0. Pixel `(x,y)` uses byte `16 * (249 - x) + floor(y / 8)` and bit `0x80 >> (y % 8)`. The six unused bits in each column must be white. SHA-256 covers the complete canonical frame. Only the SSD1680 driver inverts the bytes on the wire.

Stale revisions and conflicting hashes are rejected. An identical committed revision/hash is acknowledged without redrawing. Before staging, the unused inactive frame/name keys are removed so NVS does not temporarily retain three full labels. The active label, connection key and address are untouched. New pixels are staged in that inactive slot, refreshed, then committed with a single active-slot pointer. A restart restores the last committed label. Hardware waits require BUSY to assert within one second and return idle within 15 seconds. A timeout fails the update instead of claiming success.

## Power use

Firmware 3.8.0 keeps BLE available while the tag is powered. A short Menu press toggles QR visibility; the QR hides automatically 120 seconds after it is shown. Holding Menu for five seconds still opens the encrypted setup window. With trusted QR bounds, a hide refresh whites out only the QR area, leaving the product name, image and quantity visible. The saved canonical frame and hash remain intact so Menu can restore the QR. Without trusted bounds, the firmware blanks the full screen. A failed refresh is retried while BLE remains available.

The SDK also enables `CONFIG_PM_SLP_IRAM_OPT` and `CONFIG_PM_RTOS_IDLE_OPT` to shorten sleep/idle transitions. The build rejects missing optimizations and checks available IRAM during linking. The rebuilt SDK enables dynamic frequency scaling and automatic light sleep during idle waits, with a 240 MHz maximum and an 80 MHz minimum. Scoped CPU locks cover hashing, provisioning and panel work; a no-light-sleep lock covers the main loop's active work and is released before its notification wait. The BLE controller uses MODE1 modem sleep with the main crystal selected. The crystal remains powered during light sleep so BLE can continue; this limits any expected power saving. Connectable advertising uses a 1,000 ms idle interval. A five-second discovery window after startup or disconnect uses 100 ms advertising; the physical setup window also uses 100 ms. Discovery can take a few seconds while idle. Default and advertising transmit power are 0 dBm. The board LED stays off, and the application can wait up to 1,000 ms between quiescent checks. Menu and UART RX callbacks notify the main task immediately. It retains 25 ms polling during a connection, setup, button debounce/hold, recent USB traffic, or unavailable wake support; QR, Find, and retry deadlines shorten idle waits as needed. There are no measured battery-current or lifetime results for this change.

After a successful SSD1680 refresh, the driver disables the analog supply/oscillator and enters controller deep sleep. The image remains visible. The next refresh resets and initializes the controller; GPIO7 remains high. The JD79661 driver retains its existing sequence.

The read-only USB command `{"op":"usb_power"}` reports CPU speed, BLE modem-sleep setup, automatic-PM availability and configuration/readback results, lock results, advertising mode/interval, transmit-power status, QR visibility and its remaining timer. `loop_iterations` counts main-loop passes and `max_loop_wait_ms` records the largest scheduled notification wait; neither measures current or actual sleep duration. `{"op":"usb_pm"}` dumps ESP-IDF PM lock/profile statistics to the USB console and returns a result code. `light_sleep_observed` latches a nonzero wake cause after successful light sleep on a normal boot; `last_observed_light_sleep_wake_cause` identifies that wake source. PM mode percentages measure sleep eligibility, not actual slept time. Neither diagnostic establishes battery current or runtime. Measure at the battery connector with USB disconnected to quantify power use. Slower idle advertising can increase discovery time.

## Verification

Host tests cover packet bounds, frame validation, revisions, the actual firmware command/callback paths with simulated radio/storage, and both panel drivers under AddressSanitizer/UndefinedBehaviorSanitizer. The runtime tests link real mbedTLS cryptography, including the same upstream HKDF fallback used by the pinned Arduino core. They verify the independent Python provisioning fixture, device and authority signatures, ciphertext/tag tampering, invalid EC points, setup timing, replay rejection, conflicting bindings, malformed commands, failed persistence, signed receipts, and unchanged legacy updates. The crypto library build and all test processes have time limits. Pinned cJSON serialization tests enforce the Bluetooth callback stack fix. Both firmware profiles must compile. These checks cannot prove physical radio behavior, panel waveforms, readability or QR scanning. Confirm unpaired transfer, disconnect/retry, stable-address reconnect and power-cycle recovery on the actual board. See [verification notes](../docs/verification.md).
