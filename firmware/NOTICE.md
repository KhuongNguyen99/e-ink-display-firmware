# Hardware reference provenance

The pin assignments, controller initialization register values, SSD1680 full-refresh sequence, JD79661 full-refresh LUT values and polarity were transcribed from Elecrow's official reference repository:

https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250

Reference commit: `11291e3` (the repository clone inspected for this implementation).

- Original SSD1680: `example/arduino/Example Code/2.13_wifi/EPD_Init.cpp`, `EPD.cpp`, and `spi.h`.
- V1.2 JD79661: `example/arduino-v1.2/main/EPD_Init.cpp`, `EPD.cpp`, `spi.h`, and `main.ino`.
- Product specifications: root `readme.md` lists ESP32-S3, 8 MB flash, 8 MB PSRAM, 122 by 250 visible pixels, and both controller families.

The inspected vendor project has no top-level license or license notice in these driver files. No license grant is inferred here. Inventory Counter's networking, state handling, bounds checks, timeouts and protocol tests are new implementation; vendor example applications, artwork, fonts, upload handling and bundled third-party libraries are not included. Review upstream terms before separately redistributing the hardware-derived driver sequences.

Only the full-refresh paths are implemented. The two controller families use different commands and opposite BUSY polarity. The JD79661 full-refresh waveform consists of five 56-byte records: eight vendor prefix bytes followed by 48 zero bytes, including the vendor's alternating R22/R23 selection. No partial-refresh, vendor BLE, image-upload example or modified WebServer library is used.

## mbedTLS HKDF fallback

`inventory_counter_label/hkdf_fallback.c` is the upstream Mbed TLS v2.28.0 RFC 5869
implementation from https://github.com/Mbed-TLS/mbedtls/blob/v2.28.0/library/hkdf.c.
Copyright The Mbed TLS Contributors, Apache-2.0; the license is included in
`LICENSE-mbedTLS.txt`. The only changes replace the internal `common.h` include
with public configuration includes and enable compilation when the pinned ESP32
core omits `MBEDTLS_HKDF_C` (or the host test explicitly forces the fallback).
The cryptographic implementation is unchanged. The pinned core supplies the
remaining mbedTLS cryptographic primitives.
