"""Register a display identity once when installing phone-setup firmware."""

import argparse
import json
import re
import subprocess

try:
    from .setup_display import SetupError, request_api, request_usb
except ImportError:
    from setup_display import SetupError, request_api, request_usb


def enroll_display(port, api, register):
    authority = api("/api/display-setup/authority")
    enrollment = request_usb(port, {"op": "usb_enrollment"}).get("enrollment")
    if (not isinstance(enrollment, dict) or set(enrollment) !=
            {"device_id", "panel_profile", "authority_key_id", "claim_public_key"} or
            enrollment["authority_key_id"] != authority.get("authority_key_id")):
        raise SetupError("Install display firmware built for this Countwell app before registering it.")
    result = register({"device_id": enrollment["device_id"], "panel_profile": enrollment["panel_profile"],
        "claim_public_key": enrollment["claim_public_key"]})
    if (result.get("enrolled") is not True or result.get("device_id") != enrollment["device_id"] or
            result.get("claim_public_key") != enrollment["claim_public_key"]):
        raise SetupError("Countwell did not confirm the display identity. Try registering it again.")
    return result["device_id"]


def register_in_container(enrollment, container=None):
    try:
        if container is None:
            candidates = subprocess.run(["docker", "ps", "--filter", "label=com.docker.compose.service=counter",
                "--format", "{{.Names}}"], capture_output=True, text=True, check=True, timeout=15).stdout.split()
            if len(candidates) != 1:
                raise SetupError("Choose the local backend container with --container.")
            container = candidates[0]
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", container):
            raise SetupError("Use a valid backend container name.")
        code = ("import json,os,sys; from app.ble_setup import SetupAuthority; "
                "payload=json.load(sys.stdin); print(json.dumps(SetupAuthority(os.environ['DATA_DIR']).enroll(**payload)))")
        result = subprocess.run(["docker", "exec", "-i", container, "python", "-c", code],
            input=json.dumps(enrollment), capture_output=True, text=True, check=True, timeout=20)
        return json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        raise SetupError("Could not register the display in the local backend. Check that Countwell is running.") from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Display USB port, such as COM3 or /dev/ttyUSB0")
    parser.add_argument("--server", default="http://127.0.0.1:8082", help="Countwell API or HTTPS app address")
    parser.add_argument("--container", help="Local Countwell backend container; detected automatically when unique")
    args = parser.parse_args()
    from urllib.parse import urlsplit
    url = urlsplit(args.server)
    if (url.scheme not in ("http", "https") or not url.hostname or url.username or url.password or
            url.path not in ("", "/") or url.query or url.fragment):
        parser.error("Use the Countwell address without a path, password or query.")
    try:
        import serial
    except ImportError:
        parser.error("Install the USB helper: python -m pip install -r firmware/requirements-setup.txt")
    try:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=5)
        port.port = args.port; port.dtr = False; port.rts = False; port.open()
        try:
            enroll_display(port, lambda path, payload=None: request_api(args.server.rstrip("/"), path, payload),
                           lambda enrollment: register_in_container(enrollment, args.container))
        finally:
            port.close()
        print("Display ready for phone setup. Hold Menu, then choose Replace display or Set up display in Countwell.")
    except (SetupError, serial.SerialException) as exc:
        parser.exit(1, f"Registration failed: {exc}\n")


if __name__ == "__main__":
    main()
