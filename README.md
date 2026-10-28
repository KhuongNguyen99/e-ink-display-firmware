# Inventory Counter display firmware

BLE firmware for the Elecrow CrowPanel ESP32-S3 2.13-inch, 250 × 122 black-and-white e-paper display used by Inventory Counter. This repository starts with firmware **3.7.0** and can be consumed by an application repository as a pinned Git submodule.

Current firmware **3.8.0** reduces idle wakeups and advertises once per second while remaining connectable. Buttons, incoming USB data, and Bluetooth activity wake the application as needed; QR and Find timers retain their deadlines. Discovery may take a few seconds. Transmit power stays at 0 dBm to retain the existing range.

The label shows a product name, image, quantity, and QR code. Android clients send authenticated updates without Bluetooth bonding or a passcode. Menu shows or hides the QR, which also hides after two minutes. **Find tag** inverts the label for 30 seconds while preserving the QR area and its current visibility. Bluetooth remains available with modem sleep and automatic light sleep enabled.

## Layout

- `firmware/inventory_counter_label/`: firmware, signed BLE protocol, panel drivers, and power controls.
- `firmware/idf/`, `firmware/build-idf.ps1`: pinned Arduino ESP32 2.0.10 / ESP-IDF 4.4.5 SDK build.
- `firmware/tests/`: C++ protocol, rendering, security, and power-state tests.
- `scripts/`: USB setup and public device-enrollment helpers.
- `tests/`: Python checks for those helpers.
- `SOURCE.json`: import provenance and per-file hashes; imported text uses LF line endings.

See [firmware setup and protocol](firmware/README.md), [encrypted phone setup](firmware/PROVISIONING.md), [hardware provenance and third-party notices](firmware/NOTICE.md), and [historical verification notes](docs/verification.md).

## Check and build

Use Python 3.11+, Docker with Linux containers, Git, and PowerShell. Run from this repository's root:

```powershell
python -m pip install -r requirements-dev.txt
python -m pytest -q
.\firmware\tests\run.ps1
$publicKey = '<installation-authority-public-key>'
.\firmware\build-idf.ps1 -Panel ssd1680 -ProvisioningPublicKey $publicKey
```

The connected demo board uses SSD1680. The other supported profile is `jd79661`; identify the actual panel before installing firmware. The build requires the installation's public authority key and exports verified artifacts without flashing. Keep private signing keys, device keys, live settings, and compiled firmware outside Git. Read the firmware guide before flashing; preserve NVS to retain the device's assignment and identity.

The setup helpers communicate with an external Inventory Counter-compatible backend. The enrollment helper currently expects the standalone Inventory Counter backend container. Their API and container assumptions are retained as references for the later Jeevy migration; this repository does not integrate them into Jeevy.

## Use from Jeevy Portal

The submodule lives at `references/inventory-counter/display-firmware`. From the Jeevy repository, fetch its pinned revision with:

```sh
git submodule update --init --recursive references/inventory-counter/display-firmware
```

Commit firmware work in this repository first, push it, then update and commit the Jeevy submodule pointer. The frozen Inventory Counter source archive remains a historical reference; this repository is the working firmware source for subsequent changes.
