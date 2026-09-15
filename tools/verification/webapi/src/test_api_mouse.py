"""
Contract tests for the Kempston Mouse injection WebAPI.
Design: docs/inprogress/2026-09-12-kempston-mouse/automation-interfaces.md §4.4, §5.5
"""

import pytest


UNKNOWN_ID = "00000000-0000-0000-0000-000000000000"


def mouse_url(api_client, emu_id, route):
    return api_client._url(f"/api/v1/emulator/{emu_id}/mouse/{route}")


def post(api_client, emu_id, route, body=None):
    return api_client.session.post(mouse_url(api_client, emu_id, route), json=body)


def get(api_client, emu_id, route):
    return api_client.session.get(mouse_url(api_client, emu_id, route))


def status(api_client, emu_id):
    resp = get(api_client, emu_id, "status")
    assert resp.status_code == 200, resp.text
    return resp.json()


def run_frames(api_client, emu_id, count):
    resp = api_client.session.post(api_client._url(f"/api/v1/emulator/{emu_id}/run_frames"), json={"count": count})
    assert resp.status_code == 200, resp.text


def assert_bad_request(resp, fragment):
    assert resp.status_code == 400, resp.text
    body = resp.json()
    assert body["error"] == "Bad Request"
    assert fragment in body["message"], body["message"]


@pytest.fixture
def paused_emulator(api_client, active_emulator):
    """Paused emulator with the mouse counters at a known position and no button held."""
    api_client.pause_emulator(active_emulator)
    assert post(api_client, active_emulator, "counters", {"x": 100, "y": 100}).status_code == 200
    assert post(api_client, active_emulator, "release_all").status_code == 200
    return active_emulator


class TestMouseStatus:

    def test_status_shape(self, api_client, active_emulator):
        st = status(api_client, active_emulator)
        assert st["emulator_id"] == active_emulator
        assert st["available"] is True
        for field in ("present", "wheel_enabled", "x", "y", "buttons", "button_mask", "wheel", "ports",
                      "pending_click", "ttd_journal"):
            assert field in st, field
        assert set(st["ports"].keys()) == {"FADF", "FBDF", "FFDF"}
        assert set(st["buttons"].keys()) == {"left", "right", "middle"}
        assert st["ttd_journal"] == "supported"

    def test_button_list(self, api_client, active_emulator):
        resp = get(api_client, active_emulator, "buttons")
        assert resp.status_code == 200
        body = resp.json()
        assert body["buttons"] == ["left", "right", "middle"]
        assert body["count"] == 3


class TestMouseInput:

    def test_move_reflected_in_status(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "move", {"dx": 10, "dy": -5})
        assert resp.status_code == 200, resp.text
        body = resp.json()
        assert body["success"] is True
        assert body["dx"] == 10 and body["dy"] == -5
        assert body["state"]["x"] == 110 and body["state"]["y"] == 95

        st = status(api_client, paused_emulator)
        assert st["x"] == 110
        assert st["y"] == 95

    def test_move_single_axis(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "move", {"dy": 7})
        assert resp.status_code == 200, resp.text
        assert resp.json()["state"]["y"] == 107

    def test_counters_wrap(self, api_client, paused_emulator):
        assert post(api_client, paused_emulator, "counters", {"x": 250, "y": 3}).status_code == 200
        resp = post(api_client, paused_emulator, "move", {"dx": 10, "dy": -5})
        assert resp.status_code == 200
        assert resp.json()["state"]["x"] == 4
        assert resp.json()["state"]["y"] == 254

    def test_press_release(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "press", {"button": "L"})
        assert resp.status_code == 200, resp.text
        assert resp.json()["button"] == "left"
        assert resp.json()["state"]["buttons"]["left"] is True
        assert resp.json()["state"]["button_mask"] & 0x01 == 0

        resp = post(api_client, paused_emulator, "release", {"button": "left"})
        assert resp.status_code == 200
        assert resp.json()["state"]["buttons"]["left"] is False

    def test_buttons_set_and_clear(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "buttons", {"pressed": ["left", "m"]})
        assert resp.status_code == 200, resp.text
        state = resp.json()["state"]
        assert state["buttons"] == {"left": True, "right": False, "middle": True}

        resp = post(api_client, paused_emulator, "buttons", {"pressed": []})
        assert resp.status_code == 200
        assert resp.json()["pressed"] == []
        assert resp.json()["state"]["buttons"] == {"left": False, "right": False, "middle": False}

    def test_wheel(self, api_client, paused_emulator):
        before = status(api_client, paused_emulator)["wheel"]
        resp = post(api_client, paused_emulator, "wheel", {"steps": 2})
        assert resp.status_code == 200, resp.text
        assert resp.json()["state"]["wheel"] == (before + 2) % 16

    def test_click_pending_then_released(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "click", {"button": "left", "frames": 3})
        assert resp.status_code == 200, resp.text
        assert resp.json()["frames"] == 3

        st = status(api_client, paused_emulator)
        assert st["pending_click"] == {"button": "left", "frames_left": 3}
        assert st["buttons"]["left"] is True

        run_frames(api_client, paused_emulator, 4)

        st = status(api_client, paused_emulator)
        assert st["pending_click"] is None
        assert st["buttons"]["left"] is False

    def test_click_default_frames(self, api_client, paused_emulator):
        resp = post(api_client, paused_emulator, "click", {"button": "right"})
        assert resp.status_code == 200, resp.text
        assert resp.json()["frames"] == 2

    def test_release_all_cancels_click(self, api_client, paused_emulator):
        assert post(api_client, paused_emulator, "click", {"button": "middle", "frames": 50}).status_code == 200
        resp = post(api_client, paused_emulator, "release_all")
        assert resp.status_code == 200
        assert resp.json()["state"]["pending_click"] is None
        assert resp.json()["state"]["button_mask"] & 0x07 == 0x07


class TestMouseErrors:

    @pytest.mark.parametrize("route,body,fragment", [
        # Body / field presence
        ("move", None, "Missing 'dx' or 'dy'"),
        ("move", {}, "Missing 'dx' or 'dy'"),
        ("press", {}, "Missing 'button'"),
        ("release", None, "Missing 'button'"),
        ("click", {}, "Missing 'button'"),
        ("buttons", {}, "Missing 'pressed'"),
        ("wheel", {}, "Missing 'steps'"),
        ("counters", {"x": 1}, "Missing 'x' or 'y'"),
        # Wrong JSON types
        ("move", {"dx": "ten"}, "'dx' must be an integer"),
        ("move", {"dx": 1.5}, "'dx' must be an integer"),
        ("move", {"dx": 1, "dy": "2"}, "'dy' must be an integer"),
        ("wheel", {"steps": 1.0}, "'steps' must be an integer"),
        ("click", {"button": "left", "frames": "3"}, "'frames' must be an integer"),
        ("buttons", {"pressed": "left"}, "'pressed' must be an array"),
        # Out of range (validated by the manager)
        ("move", {"dx": 200}, "dx=200 out of range -127..127"),
        ("move", {"dy": -128}, "dy=-128 out of range -127..127"),
        ("wheel", {"steps": -9}, "steps=-9 out of range -7..7"),
        ("click", {"button": "left", "frames": 0}, "frames=0 out of range 1..65535"),
        ("click", {"button": "left", "frames": 70000}, "frames=70000 out of range 1..65535"),
        ("counters", {"x": 256, "y": 0}, "x=256 out of range 0..255"),
        ("counters", {"x": 0, "y": -1}, "y=-1 out of range 0..255"),
        # Zero input
        ("move", {"dx": 0, "dy": 0}, "move requires a non-zero dx or dy"),
        ("wheel", {"steps": 0}, "wheel requires non-zero steps"),
        # Unknown button names
        ("press", {"button": "foo"}, "Unknown button 'foo'. Valid: left, right, middle (l, r, m)"),
        ("click", {"button": "foo"}, "Unknown button 'foo'"),
        ("buttons", {"pressed": ["left", "foo"]}, "Unknown button 'foo'"),
    ])
    def test_bad_request(self, api_client, active_emulator, route, body, fragment):
        assert_bad_request(post(api_client, active_emulator, route, body), fragment)

    def test_rejected_input_leaves_state_unchanged(self, api_client, paused_emulator):
        post(api_client, paused_emulator, "move", {"dx": 200, "dy": 5})
        st = status(api_client, paused_emulator)
        assert st["x"] == 100 and st["y"] == 100

    @pytest.mark.parametrize("method,route,body", [
        ("post", "move", {"dx": 1}),
        ("post", "click", {"button": "left"}),
        ("post", "release_all", None),
        ("get", "status", None),
        ("get", "buttons", None),
    ])
    def test_unknown_emulator_404(self, api_client, method, route, body):
        if method == "post":
            resp = post(api_client, UNKNOWN_ID, route, body)
        else:
            resp = get(api_client, UNKNOWN_ID, route)
        assert resp.status_code == 404, resp.text
        assert resp.json()["message"] == "Emulator with specified ID not found"

    def test_cors_header_on_error(self, api_client, active_emulator):
        resp = post(api_client, active_emulator, "move", {"dx": 200})
        assert resp.status_code == 400
        assert "Access-Control-Allow-Origin" in resp.headers


class TestKeyboardUnknownKey:
    """Regression: keyboard routes return 400 for key names DebugKeyboardManager cannot resolve."""

    def _post(self, api_client, emu_id, route, body):
        return api_client.session.post(api_client._url(f"/api/v1/emulator/{emu_id}/keyboard/{route}"), json=body)

    @pytest.mark.parametrize("route", ["tap", "press", "release"])
    def test_unknown_key_400(self, api_client, active_emulator, route):
        resp = self._post(api_client, active_emulator, route, {"key": "nosuchkey"})
        assert_bad_request(resp, "Unknown key 'nosuchkey'")
        assert "Access-Control-Allow-Origin" in resp.headers

    def test_combo_with_one_unknown_key_400(self, api_client, active_emulator):
        resp = self._post(api_client, active_emulator, "combo", {"keys": ["cs", "nosuchkey"]})
        assert_bad_request(resp, "Unknown key 'nosuchkey'")

    def test_known_key_still_200(self, api_client, active_emulator):
        resp = self._post(api_client, active_emulator, "tap", {"key": "a"})
        assert resp.status_code == 200, resp.text
