"""Link a USB-connected display to a Inventory Counter box without phone pairing."""

import argparse
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request


PREFIX = b"CWUSB "
USB_WAKE_PREAMBLE = b"        \n\n"


class SetupError(Exception):
    pass


def request_api(server, path, payload=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(server + path, data=data,
                                     headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            raw = response.read(1_048_577)
        if len(raw) > 1_048_576:
            raise SetupError("Inventory Counter returned an oversized response.")
        return json.loads(raw)
    except urllib.error.HTTPError as exc:
        raise SetupError(f"Inventory Counter rejected setup (HTTP {exc.code}). Check the box and try again.") from None
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        raise SetupError("Could not reach Inventory Counter. Check its address and try again.") from None


def request_usb(port, payload, timeout=8):
    encoded = json.dumps(payload, separators=(",", ":")).encode("ascii")
    if len(encoded) > 1024:
        raise SetupError("USB setup message is too large.")
    port.write(USB_WAKE_PREAMBLE)
    port.flush()
    time.sleep(0.05)
    port.write(PREFIX + encoded + b"\n")
    port.flush()
    deadline = time.monotonic() + timeout
    line = bytearray()
    discard = False
    while time.monotonic() < deadline:
        chunk = port.read(min(max(port.in_waiting, 1), 256))
        for value in chunk:
            if value != 10:
                if not discard:
                    if len(line) == 2048:
                        line.clear()
                        discard = True
                    else:
                        line.append(value)
                continue
            if discard:
                discard = False
                continue
            raw = bytes(line).rstrip(b"\r")
            line.clear()
            if not raw.startswith(PREFIX):
                continue
            try:
                response = json.loads(raw[len(PREFIX):])
            except (ValueError, UnicodeError):
                raise SetupError("The display returned an unreadable USB reply.") from None
            if not isinstance(response, dict) or response.get("ok") is not True:
                raise SetupError("The display rejected setup. Hold Menu for five seconds and try again.")
            return response
    raise SetupError("No USB reply. Check the port and install Inventory Counter firmware 3 or later.")


def link_display(port, box_identifier, api):
    box = api("/api/boxes/" + urllib.parse.quote(box_identifier, safe=""))
    identity = request_usb(port, {"op": "usb_identity"}).get("identity")
    if (not isinstance(identity, dict) or identity.get("protocol") != 2 or
            identity.get("phone_pairing") is not False or identity.get("provisioning") != "usb"):
        raise SetupError("Update the display to Inventory Counter firmware 3 before USB setup.")
    if identity.get("paired") and identity.get("box_id") != box.get("id"):
        raise SetupError("This display is linked to a different box.")
    display = box.get("display") or {}
    if (identity.get("paired") and display.get("device_id") == identity.get("device_id") and
            display.get("status") not in ("pairing", "re_pair_required")):
        return identity["device_id"]
    response = api(f"/api/boxes/{urllib.parse.quote(box['id'], safe='')}/display/pair", {
        "identity": identity, "browser_device_id": None, "provisioning": "usb",
    })
    command = response.get("command", {})
    if (command.get("op") != "pair" or command.get("box_id") != box["id"] or
            not isinstance(command.get("key"), str) or not re.fullmatch("[0-9a-f]{64}", command["key"]) or
            not isinstance(command.get("generation"), str) or
            not re.fullmatch("[0-9a-f]{16}", command["generation"])):
        raise SetupError("Inventory Counter could not prepare USB setup.")
    reply = request_usb(port, {**command, "op": "usb_pair"})
    receipt = reply.get("receipt")
    if (not isinstance(receipt, dict) or receipt.get("state") != "paired" or
            receipt.get("device_id") != identity.get("device_id") or receipt.get("box_id") != box["id"] or
            receipt.get("generation") != command["generation"]):
        raise SetupError("The display did not confirm setup. Hold Menu and run setup again.")
    api(f"/api/devices/{urllib.parse.quote(identity['device_id'], safe='')}/confirm", {"receipt": receipt})
    return identity["device_id"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB port, for example COM3 or /dev/ttyUSB0")
    parser.add_argument("--box", required=True, help="Box code or box ID from Inventory Counter")
    parser.add_argument("--server", default="http://127.0.0.1:8082", help="Inventory Counter API or HTTPS app address")
    args = parser.parse_args()
    server = args.server.rstrip("/")
    url = urllib.parse.urlsplit(server)
    if (url.scheme not in ("http", "https") or not url.hostname or url.username or
            url.password or url.query or url.fragment or url.path not in ("", "/")):
        parser.error("Use the Inventory Counter address without a path, password or query.")
    try:
        import serial
    except ImportError:
        parser.error("Install the USB helper first: python -m pip install -r firmware/requirements-setup.txt")
    print("For a new display, hold Menu for five seconds before running this command.")
    try:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=5)
        port.port = args.port
        port.dtr = False
        port.rts = False
        port.open()
        try:
            device = link_display(port, args.box, lambda path, payload=None: request_api(server, path, payload))
        finally:
            port.close()
        print(f"Display {device} is linked. Open the box on your phone and choose Update display.")
    except (SetupError, serial.SerialException) as exc:
        parser.exit(1, f"Setup failed: {exc}\n")


if __name__ == "__main__":
    main()
