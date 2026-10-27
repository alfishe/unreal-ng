"""The operation log: GET /api/v1/emulator/{id}/operations (machine state coordinator, every surface's
OperationsStatus report: CLI `ops`, Lua / Python operations(), MCP emulator_manage operations)."""

import pytest

ENTRY_FIELDS = {"seq", "frame", "t_in_frame", "wall_ms", "kind", "source", "mode", "status", "effect", "summary",
                "detail", "repeats"}


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
        assert entry["repeats"] == 1

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

    def test_each_surface_is_named_as_the_source(self, api_client, active_emulator):
        """The operation log says who asked: the WebAPI per request, Lua / Python by their interpreter thread"""
        emu_id = active_emulator
        before = api_client.get_operations(emu_id)["last_seq"]
        api_client.reset_emulator(emu_id)
        sources = ["webapi"]
        if api_client.get_lua_status().get("available", False):
            resp = api_client.exec_lua(f"emu_get('{emu_id}'):reset(false)")
            assert resp["success"] is True, resp
            sources.append("lua")
        if api_client.get_python_status().get("available", False):
            resp = api_client.exec_python(f"import unreal_emulator as ue\nue.emu_get('{emu_id}').reset(False)")
            assert resp["success"] is True, resp
            sources.append("python")

        entries = api_client.get_operations(emu_id, since=before)["entries"]
        assert [(e["kind"], e["source"]) for e in entries] == [("reset", s) for s in sources]

    def test_a_tape_while_recording_is_an_event_of_the_recording(self, api_client, active_emulator, tmp_path):
        """tape/load during a TTD recording (M3): accepted, logged done in mode recording with no session effect - it
        lands at the next frame boundary as an event of the recording, which goes on"""
        emu_id = active_emulator
        tape = tmp_path / "while-recording.tap"
        # One standard-speed header block: flag 0, type 3 (code), name, length 0, start 0x8000, checksum
        header = bytes([0x00, 0x03]) + b"recorded  " + bytes([0x00, 0x00, 0x00, 0x80, 0x00, 0x80])
        checksum = 0
        for b in header:
            checksum ^= b
        block = header + bytes([checksum])
        tape.write_bytes(len(block).to_bytes(2, "little") + block)
        api_client.ttd_start(emu_id)
        try:
            assert api_client.get_operations(emu_id)["mode"] == "recording"
            before = api_client.get_operations(emu_id)["last_seq"]
            resp = api_client.session.post(api_client._url(f"/api/v1/emulator/{emu_id}/tape/load"),
                                           json={"path": str(tape)})
            assert resp.status_code == 200, resp.text

            entries = api_client.get_operations(emu_id, since=before)["entries"]
            assert len(entries) == 1, entries
            entry = entries[0]
            assert (entry["kind"], entry["status"], entry["source"], entry["mode"], entry["effect"]) == \
                ("media-insert", "done", "webapi", "recording", "none")
            assert entry["summary"].startswith("tape ") and entry["summary"].endswith("while-recording.tap")
            assert api_client.ttd_status(emu_id)["state"] == "recording"
        finally:
            api_client.ttd_stop(emu_id)
