# Firmware verification

The standalone extraction starts at firmware **3.7.0**. Firmware logic, setup helpers, and host tests match the Inventory Counter reference snapshot recorded in `SOURCE.json`; text line endings use LF. The firmware guide's verification link points here.

## Checks run after extraction

- `python -m pytest -q`: all 12 USB setup and enrollment helper tests pass.
- `firmware/tests/run.ps1`: the complete C++ host suite passes with AddressSanitizer and UndefinedBehaviorSanitizer, real mbedTLS cryptography, and pinned cJSON.
- Both SSD1680 and JD79661 driver tests pass, including SPI packing, opposite BUSY polarities, disconnected/stuck panel waits, and SSD1680 controller sleep.
- Power tests cover BLE controller modem sleep, automatic light sleep, wake failures, and the stock-SDK fallback.
- BLE/provisioning tests cover packet and JSON bounds, replay/stale revision rejection, signed receipts, incomplete or malformed uploads, authentication failures, encrypted enrollment, and failed persistence.
- Find tag tests cover QR pixels and quiet margins, hidden QR, timer wraparound, repeat requests, reconnects, retry recovery, expiry during another upload, normal commits, and setup cancellation.

## Hardware baseline

Before extraction, the same 3.7.0 firmware was installed application-only on the SSD1680 demo board. The upload was verified. Real unbonded BLE commands exercised Find tag with a hidden QR, repeat requests without redrawing, reconnect preservation, and automatic restoration after the client disconnected. The canonical saved label and application history stayed unchanged. USB diagnostics confirmed QR auto-hide, 500 ms idle advertising, modem sleep, and successful automatic light-sleep entry.

Both panel profiles were compiled using the pinned Arduino ESP32 2.0.10 / ESP-IDF 4.4.5 SDK before extraction. Hardware optical QR scanning during the inverted state and battery-connector current were not measured. The host tests simulate hardware and cannot establish physical readability, waveform quality, radio range, or battery life. Identify the fitted panel before flashing and use the build/installation instructions in `firmware/README.md`.

The public interoperability fixture in `firmware/tests/provisioning_vector.json` deliberately contains trivial test private scalars. It is test data and must never be used as a deployment identity. Production keys, device bindings, application databases, and private hardware records are not part of this repository.

## Inventory Counter naming refactor

The reference UI, namespaces, build flags, sketch and artifact names, setup messages, and documentation now use Inventory Counter. After the rename, all 12 Python helper tests and the complete C++ host suite pass, and both pinned SDK panel profiles compile. BLE UUIDs, CW2/CW3/CWUSB messages, the cwble2 NVS namespace, saved-record layouts, and device IDs are preserved. This renamed source has not been flashed to the physical board.
