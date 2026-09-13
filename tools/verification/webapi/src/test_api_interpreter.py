import pytest

class TestInterpreters:
    
    def test_python_interpreter(self, api_client):
        """Verify Python interpreter execution."""
        
        # Check status
        status = api_client.get_python_status()
        if not status.get("available", False):
            pytest.skip("Python interpreter not available")
            
        # Exec code
        code = "print('Hello API')"
        resp = api_client.exec_python(code)
        assert resp["success"] is True
        assert "Hello API" in resp["output"]
        
        # Stop (hard to test without long running script, but checking endpoint works)
        api_client.stop_python()

    def test_lua_interpreter(self, api_client):
        """Verify Lua interpreter execution."""
        
        # Check status
        status = api_client.get_lua_status()
        if not status.get("available", False):
            pytest.skip("Lua interpreter not available")
            
        # Exec code
        code = "print('Hello API')"
        resp = api_client.exec_lua(code)
        assert resp["success"] is True
        assert "Hello API" in resp["output"]
        
        # Stop
        api_client.stop_lua()

    # ------------------------------------------------------------------
    # Kempston Mouse bindings (automation-interfaces §4.6, §4.7, §5.7)
    # ------------------------------------------------------------------

    def test_mouse_python_bindings(self, api_client, active_emulator):
        """emu.mouse_*: state dict keys, ValueError on bad arguments."""
        status = api_client.get_python_status()
        if not status.get("available", False):
            pytest.skip("Python interpreter not available")

        code = """
import unreal_emulator as ue
emu = ue.emu_get('__EMU_ID__')
assert emu is not None, 'emulator not found'
emu.pause()

st = emu.mouse_set_counters(31, 85)
st = emu.mouse_move(10, -5)
assert (st['x'], st['y']) == (41, 80), st
for key in ('x', 'y', 'buttons', 'button_mask', 'wheel', 'wheel_enabled',
            'present', 'ports', 'pending_click', 'ttd_journal'):
    assert key in st, key
assert set(st['buttons']) == {'left', 'right', 'middle'}, st['buttons']
assert set(st['ports']) == {'FADF', 'FBDF', 'FFDF'}, st['ports']
assert st['ttd_journal'] == 'supported', st['ttd_journal']
assert st['pending_click'] is None, st['pending_click']

st = emu.mouse_click('left', 3)
assert st['buttons']['left'] is True, st
assert st['pending_click'] == {'button': 'left', 'frames_left': 3}, st['pending_click']
assert emu.mouse_click_pending() is True

st = emu.mouse_release_all()
assert st['buttons']['left'] is False and st['pending_click'] is None, st
assert emu.mouse_status()['x'] == 41

def expect_value_error(fn, *args, fragment=''):
    try:
        fn(*args)
    except ValueError as e:
        assert fragment in str(e), str(e)
        return
    raise AssertionError('no ValueError for %r' % (args,))

expect_value_error(emu.mouse_move, 200, 0, fragment='dx=200')
expect_value_error(emu.mouse_wheel, 12, fragment='steps=12')
expect_value_error(emu.mouse_press, 'foo', fragment="'foo'")
expect_value_error(emu.mouse_buttons, ['left', 'bogus'], fragment="'bogus'")
expect_value_error(emu.mouse_click, 'left', 0, fragment='frames=0')
assert emu.mouse_button_names() == ['left', 'right', 'middle']
emu.resume()
print('MOUSE_PY_OK')
""".replace("__EMU_ID__", active_emulator)

        resp = api_client.exec_python(code)
        assert resp["success"] is True, resp
        assert "MOUSE_PY_OK" in resp.get("output", ""), resp

    def test_mouse_lua_bindings(self, api_client, active_emulator):
        """mouse_* globals: state table, nil+err on bad arguments, selected-emulator targeting."""
        status = api_client.get_lua_status()
        if not status.get("available", False):
            pytest.skip("Lua interpreter not available")

        # Nothing is bound: the fixture started (and selected) active_emulator,
        # so mouse_* must act on it via effectiveEmulator().
        code = """
local st = assert(mouse_set_counters(31, 85))
st = assert(mouse_move(10, -5))
assert(st.x == 41 and st.y == 80, "move: " .. tostring(st.x) .. "," .. tostring(st.y))
assert(type(st.buttons.left) == "boolean", "buttons.left")
assert(st.ports.FADF ~= nil and st.ports.FBDF ~= nil and st.ports.FFDF ~= nil, "ports")
assert(st.ttd_journal == "supported", "ttd_journal")
assert(type(st.button_mask) == "number" and type(st.wheel) == "number", "mask/wheel")

local ok, err = mouse_move(200, 0)
assert(ok == nil and string.find(err, "dx=200", 1, true), "range: " .. tostring(err))
ok, err = mouse_move(1.5, 0)
assert(ok == nil and string.find(err, "integer", 1, true), "non-integer: " .. tostring(err))
ok, err = mouse_press("foo")
assert(ok == nil and string.find(err, "foo", 1, true), "bad name: " .. tostring(err))
ok, err = mouse_wheel(12)
assert(ok == nil and string.find(err, "steps=12", 1, true), "wheel: " .. tostring(err))

assert(mouse_buttons({"left", "middle"}).buttons.middle == true, "buttons")
assert(mouse_release_all().buttons.left == false, "release_all")
assert(#mouse_button_names() == 3, "names")
assert(mouse_status().x == 41, "status")
print("MOUSE_LUA_OK")
"""
        resp = api_client.exec_lua(code)
        assert resp["success"] is True, resp
        assert "MOUSE_LUA_OK" in resp.get("output", ""), resp

        # Cross-check that Lua changed the selected emulator (when Python is available)
        if api_client.get_python_status().get("available", False):
            check = """
import unreal_emulator as ue
print('X=%d' % ue.emu_get('__EMU_ID__').mouse_status()['x'])
""".replace("__EMU_ID__", active_emulator)
            resp = api_client.exec_python(check)
            assert resp["success"] is True, resp
            assert "X=41" in resp.get("output", ""), resp
