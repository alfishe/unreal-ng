"""
Contract tests for the Kempston joystick injection WebAPI.
Design: docs/inprogress/2026-09-15-atm-baseconf-highres-ports/tdd-kempston-joystick.md §5, §7 (JOY-13)

Input is queued for the machine thread while the loop lives, so a paused machine applies it on its next
executed instruction: tests call run_frames(1) before reading the device back (the Kempston mouse queues the
same way, which is why its immediate read-back in test_api_mouse.py fails on a live emulator).

The joystick is a state byte, active high, read by the guest at IN #1F. ATM3 (ZX-Evo) decodes it
outside shadow mode, so these tests run on ATM3; the last class reads the port from a real guest loop.
"""

import time

import pytest

UNKNOWN_ID = "00000000-0000-0000-0000-000000000000"
NAMES = ["up", "down", "left", "right", "fire", "b5", "b6", "b7"]


def joystick_url(api_client, emu_id, route=""):
    suffix = f"/{route}" if route else ""
    return api_client._url(f"/api/v1/emulator/{emu_id}/joystick{suffix}")


def post(api_client, emu_id, route, body=None):
    return api_client.session.post(joystick_url(api_client, emu_id, route), json=body)


def status(api_client, emu_id):
    resp = api_client.session.get(joystick_url(api_client, emu_id))
    assert resp.status_code == 200, resp.text
    return resp.json()


def run_frames(api_client, emu_id, count):
    resp = api_client.session.post(api_client._url(f"/api/v1/emulator/{emu_id}/run_frames"), json={"count": count})
    assert resp.status_code == 200, resp.text


def settle(api_client, emu_id):
    """The machine thread owns input while the loop lives: a paused machine applies a queued change
    on its next executed instruction, so run one frame before reading the device back."""
    run_frames(api_client, emu_id, 1)


def assert_bad_request(resp, fragment):
    assert resp.status_code == 400, resp.text
    body = resp.json()
    assert body["error"] == "Bad Request"
    assert fragment in body["message"], body["message"]


@pytest.fixture
def atm3(api_client):
    response = api_client.create_and_start_emulator(model="ATM3")
    emu_id = response["id"]
    time.sleep(0.5)
    yield emu_id
    try:
        api_client.stop_emulator(emu_id)
        time.sleep(0.5)
        api_client.delete_emulator(emu_id)
    except Exception as e:  # pragma: no cover - teardown only
        print(f"Teardown failed for emulator {emu_id}: {e}")


@pytest.fixture
def paused(api_client, atm3):
    """Paused ATM3 with nothing held."""
    api_client.pause_emulator(atm3)
    assert post(api_client, atm3, "set", {"state": 0}).status_code == 200
    settle(api_client, atm3)
    return atm3


class TestJoystickStatus:

    def test_status_shape(self, api_client, atm3):
        st = status(api_client, atm3)
        assert st["emulator_id"] == atm3
        assert st["available"] is True and st["present"] is True and st["wired"] is True
        for field in ("state", "port_value", "buttons", "pressed", "button_names", "keys", "pending_tap"):
            assert field in st, field
        assert st["button_names"] == NAMES
        assert set(st["buttons"].keys()) == set(NAMES)
        assert "up:kp_8" in st["keys"]

    def test_unknown_emulator_is_404(self, api_client):
        resp = api_client.session.get(joystick_url(api_client, UNKNOWN_ID))
        assert resp.status_code == 404


class TestJoystickInput:

    def test_press_release(self, api_client, paused):
        resp = post(api_client, paused, "press", {"buttons": "up+fire"})
        assert resp.status_code == 200, resp.text
        body = resp.json()
        assert body["success"] is True
        assert body["message"] == "Joystick pressed: up,fire"
        assert "warning" not in body
        settle(api_client, paused)
        st = status(api_client, paused)
        assert st["state"] == 0x18 and st["port_value"] == 0x18
        assert st["pressed"] == ["up", "fire"]

        resp = post(api_client, paused, "release", {"buttons": ["up"]})
        assert resp.status_code == 200
        settle(api_client, paused)
        assert status(api_client, paused)["state"] == 0x10

    def test_set_state_and_list(self, api_client, paused):
        resp = post(api_client, paused, "set", {"state": 0xE5})
        assert resp.status_code == 200, resp.text
        assert resp.json()["requested_state"] == 0xE5
        settle(api_client, paused)
        assert status(api_client, paused)["state"] == 0xE5
        assert post(api_client, paused, "set", {"buttons": ["down", "left"]}).status_code == 200
        settle(api_client, paused)
        assert status(api_client, paused)["state"] == 0x06
        assert post(api_client, paused, "set", {"buttons": []}).status_code == 200
        settle(api_client, paused)
        assert status(api_client, paused)["state"] == 0x00

    def test_tap_pending_then_released(self, api_client, paused):
        resp = post(api_client, paused, "tap", {"buttons": "fire", "frames": 3})
        assert resp.status_code == 200, resp.text
        assert resp.json()["frames"] == 3

        st = status(api_client, paused)
        assert st["pending_tap"] == {"mask": 0x10, "frames_left": 3}

        run_frames(api_client, paused, 2)
        assert status(api_client, paused)["buttons"]["fire"] is True, "still held after 2 of 3 frames"
        run_frames(api_client, paused, 2)
        st = status(api_client, paused)
        assert st["pending_tap"] is None
        assert st["buttons"]["fire"] is False

    def test_tap_default_frames(self, api_client, paused):
        resp = post(api_client, paused, "tap", {"buttons": "left"})
        assert resp.status_code == 200, resp.text
        assert resp.json()["frames"] == 2


class TestJoystickErrors:

    @pytest.mark.parametrize("route,body,fragment", [
        ("press", None, "Missing 'buttons'"),
        ("press", {}, "Missing 'buttons'"),
        ("release", {}, "Missing 'buttons'"),
        ("tap", {}, "Missing 'buttons'"),
        ("set", {}, "Missing 'state' or 'buttons'"),
        ("press", {"buttons": 5}, "'buttons' must be a string or an array"),
        ("set", {"state": "5"}, "'state' must be an integer"),
        ("tap", {"buttons": "fire", "frames": "2"}, "'frames' must be an integer"),
        ("press", {"buttons": "jump"}, "unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)"),
        ("set", {"buttons": ["up", "jump"]}, "unknown joystick button"),
        ("set", {"state": 300}, "state=300 out of range 0..255"),
        ("set", {"state": -1}, "state=-1 out of range 0..255"),
        ("tap", {"buttons": "fire", "frames": 0}, "frames=0 out of range 1..65535"),
        ("tap", {"buttons": "fire", "frames": 70000}, "frames=70000 out of range 1..65535"),
    ])
    def test_bad_request(self, api_client, atm3, route, body, fragment):
        assert_bad_request(post(api_client, atm3, route, body), fragment)

    def test_rejected_input_leaves_state_unchanged(self, api_client, paused):
        post(api_client, paused, "set", {"state": 300})
        settle(api_client, paused)
        assert status(api_client, paused)["state"] == 0

    @pytest.mark.parametrize("route,body", [
        ("press", {"buttons": "up"}),
        ("release", {"buttons": "up"}),
        ("set", {"state": 1}),
        ("tap", {"buttons": "fire"}),
    ])
    def test_unknown_emulator_is_404(self, api_client, route, body):
        assert post(api_client, UNKNOWN_ID, route, body).status_code == 404


class TestJoystickWarnings:

    def test_unwired_machine_warns(self, api_client):
        response = api_client.create_and_start_emulator(model="PENTAGON")
        emu_id = response["id"]
        try:
            api_client.pause_emulator(emu_id)
            resp = post(api_client, emu_id, "press", {"buttons": "fire"})
            assert resp.status_code == 200, resp.text
            assert "does not decode a Kempston joystick port" in resp.json()["warning"]
            assert status(api_client, emu_id)["wired"] is False
        finally:
            api_client.stop_emulator(emu_id)
            time.sleep(0.5)
            api_client.delete_emulator(emu_id)


class TestJoystickGuestRead:
    """A guest loop reads IN #1F outside shadow mode and stores the byte: the whole chain from the WebAPI."""

    # F3 DI; loop: DB 1F  IN A,(#1F); 32 00 90 LD (#9000),A; 18 F9 JR loop
    LOOP = [0xF3, 0xDB, 0x1F, 0x32, 0x00, 0x90, 0x18, 0xF9]

    def read_byte(self, api_client, emu_id, address):
        resp = api_client.session.get(api_client._url(f"/api/v1/emulator/{emu_id}/memory/read/{address}?length=1&format=full"))
        assert resp.status_code == 200, resp.text
        return resp.json()["data"][0]

    def test_guest_reads_the_pressed_buttons(self, api_client, paused):
        # The ZX-Evo boots in shadow mode, where #1F is the floppy controller; its service menu
        # (PC #6117) is outside shadow, about 60 frames after reset
        run_frames(api_client, paused, 120)
        resp = api_client.session.post(api_client._url(f"/api/v1/emulator/{paused}/memory/write"),
                                       json={"address": "0xC000", "data": self.LOOP})
        assert resp.status_code == 200, resp.text
        resp = api_client.session.put(api_client._url(f"/api/v1/emulator/{paused}/registers/pc"),
                                      json={"value": 0xC000})
        assert resp.status_code == 200, resp.text

        assert post(api_client, paused, "press", {"buttons": "up+fire"}).status_code == 200
        run_frames(api_client, paused, 2)
        assert self.read_byte(api_client, paused, "0x9000") == 0x18

        assert post(api_client, paused, "set", {"state": 0x04}).status_code == 200
        run_frames(api_client, paused, 2)
        assert self.read_byte(api_client, paused, "0x9000") == 0x04
