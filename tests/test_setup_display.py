"""USB setup never sends permanent display credentials over BLE."""

import json

import pytest

from scripts.setup_display import PREFIX, USB_WAKE_PREAMBLE, SetupError, link_display, request_usb


BOX = "01234567-89ab-4cde-8123-456789abcdef"
IDENTITY = {"protocol": 2, "device_id": "cw-test", "phone_pairing": False,
            "provisioning": "usb", "paired": False, "box_id": ""}
KEY = "ab" * 32
GENERATION = "12" * 8


class Port:
    def __init__(self, responses):
        self.responses = iter(responses)
        self.pending = bytearray()
        self.messages = []
        self.writes = []
        self.events = []

    def write(self, message):
        self.writes.append(message)
        self.events.append(("write", message))
        if message == USB_WAKE_PREAMBLE:
            return
        assert message.startswith(PREFIX) and message.endswith(b"\n")
        self.messages.append(json.loads(message[len(PREFIX):]))
        self.pending.extend(next(self.responses))

    def flush(self):
        self.events.append(("flush",))

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, size):
        data = bytes(self.pending[:min(size, 3)])
        del self.pending[:len(data)]
        return data


def response(payload):
    return PREFIX + json.dumps(payload).encode() + b"\n"


def test_usb_request_wakes_uart_before_sending_command(monkeypatch):
    port = Port([response({"ok": True, "identity": IDENTITY})])
    monkeypatch.setattr("scripts.setup_display.time.sleep",
                        lambda seconds: port.events.append(("sleep", seconds)))
    assert request_usb(port, {"op": "usb_identity"})["identity"] == IDENTITY
    command = PREFIX + b'{"op":"usb_identity"}\n'
    assert port.events == [("write", USB_WAKE_PREAMBLE), ("flush",),
                           ("sleep", 0.05), ("write", command), ("flush",)]


def test_oversized_command_is_rejected_before_uart_wake(monkeypatch):
    port = Port([])
    sleeps = []
    monkeypatch.setattr("scripts.setup_display.time.sleep", sleeps.append)
    with pytest.raises(SetupError, match="too large"):
        request_usb(port, {"op": "usb_pair", "key": "ab" * 600})
    assert port.writes == [] and sleeps == []


def test_usb_setup_uses_private_usb_command_and_confirms_signed_receipt():
    receipt = {"state": "paired", "device_id": "cw-test", "box_id": BOX,
               "generation": GENERATION, "signature": "cd" * 32}
    port = Port([b"boot log\r\n" + response({"ok": True, "identity": IDENTITY}),
                 response({"ok": True, "receipt": receipt})])
    calls = []

    def api(path, payload=None):
        calls.append((path, payload))
        if path == "/api/boxes/AB12CD34":
            return {"id": BOX, "display": None}
        if path.endswith("/display/pair"):
            assert payload["provisioning"] == "usb" and payload["browser_device_id"] is None
            return {"command": {"op": "pair", "box_id": BOX, "generation": GENERATION, "key": KEY}}
        assert path == "/api/devices/cw-test/confirm" and payload == {"receipt": receipt}
        return {}

    assert link_display(port, "AB12CD34", api) == "cw-test"
    assert port.messages == [{"op": "usb_identity"},
                             {"op": "usb_pair", "box_id": BOX, "generation": GENERATION, "key": KEY}]
    assert port.writes[::2] == [USB_WAKE_PREAMBLE, USB_WAKE_PREAMBLE]
    assert all(KEY not in json.dumps(payload) for _, payload in calls)


def test_already_configured_display_needs_no_provisioning():
    identity = {**IDENTITY, "paired": True, "box_id": BOX}
    port = Port([response({"ok": True, "identity": identity})])
    calls = []

    def api(path, payload=None):
        calls.append(path)
        return {"id": BOX, "display": {"device_id": "cw-test", "status": "synced"}}

    assert link_display(port, "AB12CD34", api) == "cw-test"
    assert calls == ["/api/boxes/AB12CD34"]
    assert port.messages == [{"op": "usb_identity"}]


@pytest.mark.parametrize("identity", [None, {**IDENTITY, "phone_pairing": True},
                                           {**IDENTITY, "paired": True, "box_id": "other"}])
def test_unsupported_or_wrong_box_identity_never_receives_key(identity):
    port = Port([response({"ok": True, "identity": identity})])
    with pytest.raises(SetupError):
        link_display(port, BOX, lambda *_: {"id": BOX, "display": None})
    assert port.messages == [{"op": "usb_identity"}]


def test_oversized_serial_noise_cannot_splice_a_response():
    port = Port([b"x" * 3000 + response({"ok": True, "identity": "forged"}) +
                 response({"ok": True, "identity": IDENTITY})])
    assert request_usb(port, {"op": "usb_identity"})["identity"] == IDENTITY


def test_usb_rejection_does_not_echo_response_or_credentials():
    port = Port([response({"ok": False, "error": KEY})])
    with pytest.raises(SetupError) as failure:
        request_usb(port, {"op": "usb_identity"})
    assert KEY not in str(failure.value)
    assert port.writes[0] == USB_WAKE_PREAMBLE
