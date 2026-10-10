"""The operation log: GET /api/v1/emulator/{id}/operations (machine state coordinator, every surface's
OperationsStatus report: CLI `ops`, Lua / Python operations(), MCP emulator_manage operations)."""

import pytest

ENTRY_FIELDS = {"seq", "frame", "t_in_frame", "wall_ms", "kind", "source", "mode", "status", "effect", "summary",
                "detail"}


class TestOperationLog:

    def test_a_reset_is_logged(self, api_client, active_emulator):
        emu_id = active_emulator
        before = api_client.get_operations(emu_id)["last_seq"]

        api_client.reset_emulator(emu_id)
        log = api_client.get_operations(emu_id, since=before)

        assert log["mode"] in {"live", "recording", "black-box", "replaying", "detached"}
        assert log["capacity"] == 1024
        assert log["last_seq"] == before + 1
        assert len(log["entries"]) == 1
        entry = log["entries"][0]
        assert set(entry) == ENTRY_FIELDS
        assert entry["seq"] == before + 1
        assert entry["kind"] == "reset"
        assert entry["status"] == "done"

    def test_since_and_limit(self, api_client, active_emulator):
        emu_id = active_emulator
        before = api_client.get_operations(emu_id)["last_seq"]
        for _ in range(3):
            api_client.reset_emulator(emu_id)

        newest = api_client.get_operations(emu_id, since=before, limit=1)
        assert [e["seq"] for e in newest["entries"]] == [before + 3]
        assert api_client.get_operations(emu_id, since=before + 3)["entries"] == []

    def test_bad_query_is_400(self, api_client, active_emulator):
        resp = api_client.session.get(api_client._url(f"/api/v1/emulator/{active_emulator}/operations"),
                                      params={"since": "-1"})
        assert resp.status_code == 400
        assert "since" in resp.json()["message"]

    def test_unknown_emulator_is_404(self, api_client):
        resp = api_client.session.get(api_client._url("/api/v1/emulator/no-such-instance/operations"))
        assert resp.status_code == 404

    def test_python_binding_returns_the_same_report(self, api_client, active_emulator):
        status = api_client.get_python_status()
        if not status.get("available", False):
            pytest.skip("Python interpreter not available")
        code = """
import unreal_emulator as ue
emu = ue.emu_get('__EMU_ID__')
before = emu.operations()['last_seq']
emu.reset(False)
log = emu.operations(since=before)
assert log['last_seq'] == before + 1, log
assert [e['kind'] for e in log['entries']] == ['reset'], log
assert len(emu.operations(limit=1)['entries']) == 1
print('OPS_PY_OK')
""".replace("__EMU_ID__", active_emulator)
        resp = api_client.exec_python(code)
        assert resp["success"] is True, resp
        assert "OPS_PY_OK" in resp["output"], resp

    def test_lua_binding_returns_the_same_report(self, api_client, active_emulator):
        status = api_client.get_lua_status()
        if not status.get("available", False):
            pytest.skip("Lua interpreter not available")
        before = api_client.get_operations(active_emulator)["last_seq"]
        api_client.reset_emulator(active_emulator)
        code = f"""
local log = operations({before})
assert(log.last_seq == {before} + 1, "last_seq " .. tostring(log.last_seq))
assert(#log.entries == 1 and log.entries[1].kind == "reset", "entries")
assert(#operations(0, 1).entries == 1, "limit")
print("OPS_LUA_OK")
"""
        resp = api_client.exec_lua(code)
        assert resp["success"] is True, resp
        assert "OPS_LUA_OK" in resp["output"], resp
