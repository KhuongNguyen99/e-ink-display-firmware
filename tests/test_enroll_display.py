"""Firmware installation registers only a public device identity."""

import pytest

from scripts.enroll_display import enroll_display, register_in_container
from scripts.setup_display import SetupError
from test_setup_display import Port, USB_WAKE_PREAMBLE, response


def test_enrollment_records_public_identity_without_changing_box_assignment():
    enrollment = {"device_id": "cw-new", "panel_profile": "ssd1680",
                  "authority_key_id": "ab" * 8, "claim_public_key": "04" + "11" * 64}
    port = Port([response({"ok": True, "enrollment": enrollment})])
    calls = []
    def api(path, payload=None):
        calls.append((path, payload))
        assert path == "/api/display-setup/authority" and payload is None
        return {"authority_key_id": enrollment["authority_key_id"]}
    def register(payload):
        assert set(payload) == {"device_id", "panel_profile", "claim_public_key"}
        return {**enrollment, "enrolled": True}
    assert enroll_display(port, api, register) == "cw-new"
    assert port.messages == [{"op": "usb_enrollment"}]
    assert port.writes[0] == USB_WAKE_PREAMBLE
    assert len(calls) == 1


def test_wrong_firmware_authority_cannot_enroll():
    port = Port([response({"ok": True, "enrollment": {"authority_key_id": "wrong"}})])
    with pytest.raises(SetupError, match="built for this Countwell"):
        enroll_display(port, lambda *_: {"authority_key_id": "correct"}, lambda _: pytest.fail("must not register"))


def test_registration_uses_local_container_admin_access_without_http_or_secret_data(monkeypatch):
    from types import SimpleNamespace
    calls = []
    enrollment = {"device_id": "cw-test", "panel_profile": "ssd1680", "claim_public_key": "04" + "12" * 64}
    def run(command, **options):
        calls.append((command, options))
        return SimpleNamespace(stdout='{"enrolled":true}')
    monkeypatch.setattr("scripts.enroll_display.subprocess.run", run)
    assert register_in_container(enrollment, "countwell-api")["enrolled"] is True
    command, options = calls[0]
    assert command[:5] == ["docker", "exec", "-i", "countwell-api", "python"]
    import json
    assert json.loads(options["input"]) == enrollment
    assert not {"key", "token", "private_key"}.intersection(json.loads(options["input"]))
    assert not options.get("shell")
    with pytest.raises(SetupError, match="valid backend"):
        register_in_container(enrollment, "--privileged")
