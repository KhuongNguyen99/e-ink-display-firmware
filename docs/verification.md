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

The reference UI, namespaces, build flags, sketch and artifact names, setup messages, and documentation now use Inventory Counter. After the rename, all 12 Python helper tests and the complete C++ host suite pass, and both pinned SDK panel profiles compile. BLE UUIDs, CW2/CW3/CWUSB messages, the cwble2 NVS namespace, saved-record layouts, and device IDs are preserved. That naming-only revision was not installed on the physical board.

## Always-available BLE with lower idle activity (3.8.0)

Verified on October 28, 2026. Firmware 3.8.0 retains connectable BLE while enabling optimized SDK light-sleep transitions, a one-second idle advertising interval, and up to one second between quiescent main-loop checks. Menu and UART RX notify the main task immediately. Active work and unavailable wake support retain 25 ms polling; QR, Find, and retry deadlines shorten idle waits. Startup/disconnect discovery uses 100 ms advertising for five seconds. Transmit power remains 0 dBm and the main crystal remains retained for BLE.

The full AddressSanitizer/UndefinedBehaviorSanitizer firmware host suite and all 12 Python setup-helper tests pass. New tests exercise the actual loop with UART/task notifications, active versus idle waits, wake setup failures, and exact refresh/retry deadlines including wraparound. Both SSD1680 and JD79661 profiles compile with the pinned Arduino ESP32 2.0.10 / ESP-IDF 4.4.5 SDK and both IRAM transition optimizations enabled.

The SSD1680 demo board was flashed application-only on COM3. Esptool verified the upload. The installed application image SHA-256 is `416f4c7c6fe9ecb2d6583dda1510d32630d7443078a81506e4bea6db47ad9f45`. NVS, enrollment, box binding, saved label, and Bluetooth name were preserved. The application region was backed up privately before installation.

Hardware verification passed:

- During a 130-second idle check, the main loop made 169 passes, including the initial USB-active interval. Diagnostics confirmed a maximum scheduled wait of 1,000 ms, successful light-sleep entry, active advertising at 1,000 ms, and automatic QR hiding.
- All ten USB requests after idle returned within 94–110 ms, including the helper's 50 ms wake preamble delay.
- A real unbonded BLE client replayed the existing signed 4,000-byte label; the backend accepted the receipt. The canonical label, stock quantity, and history stayed unchanged.
- Find tag refreshed the panel and restored it automatically after the client disconnected. Reconnection after light sleep recovered the normal signed receipt.
- The user confirmed that two short Menu presses show and hide the QR, leaving it hidden.

The first 12-second PC scan missed the tag. A broader scan found it, and the full BLE workflow passed with a longer discovery window. Discovery latency depends on scanning and radio conditions. Main-loop counters and a light-sleep wake cause do not measure battery current, total sleep residency, or battery life. No current measurement or new physical long-hold setup test was performed for this update. Private hardware records and the backup image remain outside Git.
