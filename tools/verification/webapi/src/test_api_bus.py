"""Bus analyzer on every live surface: WebAPI routes, the Python and Lua bindings and the CLI (docs/inprogress/2026-10-11-bus-analyzer).

Needs a running unreal-qt / unreal-cli WebAPI (UNREAL_API_URL, default http://localhost:8090) and a TSL (TS-Conf) model; the CLI
checks use UNREAL_CLI_PORT (default 8765) and skip themselves when nothing listens there. The values are asserted by the core
tests (core/tests/emulator/bus/); this file checks what only a live server shows: the routes, the query and body parsing, the
status codes, and that every surface reaches the same DeviceState::Bus* answer (one machine, one finding, four readers).
"""

import json
import os
import socket
import time

import pytest

# DI; DMA destination #50000, length #FF words, one burst; select the SD card; start the SPI -> RAM DMA; IN A,(#57) at once; JR $
# (the program of tools/verification/mcp/test_bus.py)
COLLIDE = bytes.fromhex('F3 01 AF 1D 3E 00 ED 79 06 1E ED 79 06 1F 3E 14 ED 79 06 26 3E FF ED 79 06 28 AF ED 79 '
                        '3E 00 D3 77 06 27 3E 02 ED 79 DB 57 18 FE')
IN_PC = 0x8000 + 39  # the IN A,(#57)


@pytest.fixture(scope="function")
def tsconf_emulator(api_client):
    try:
        response = api_client.create_and_start_emulator(model="TSL")
    except Exception as error:  # the model is not creatable in this build
        pytest.skip(f"TSL is not creatable here: {error}")
    emulator_id = response["id"]
    api_client.session.put(api_client._url(f"/api/v1/emulator/{emulator_id}/audio/mixer/master"), json={"muted": True})
    time.sleep(0.5)
    yield emulator_id
    try:
        api_client.stop_emulator(emulator_id)
        api_client.delete_emulator(emulator_id)
    except Exception as error:
        print(f"Teardown failed for emulator {emulator_id}: {error}")


def get(api_client, emulator_id, path):
    return api_client.session.get(api_client._url(f"/api/v1/emulator/{emulator_id}{path}"))


def post(api_client, emulator_id, path, body):
    return api_client.session.post(api_client._url(f"/api/v1/emulator/{emulator_id}{path}"), json=body)


def buses(api_client, emulator_id):
    response = get(api_client, emulator_id, "/bus")
    assert response.status_code == 200, response.text
    return {bus["id"]: bus for bus in response.json()["buses"]}


def run_collision(api_client, emulator_id):
    """The program reads #57 while its own SPI DMA runs: one dma_collision at IN_PC"""
    api_client.pause_emulator(emulator_id)
    wrote = post(api_client, emulator_id, "/memory/write", {"address": "0x8000", "data": list(COLLIDE)})
    assert wrote.status_code == 200, wrote.text
    pc = api_client.session.put(api_client._url(f"/api/v1/emulator/{emulator_id}/registers/pc"), json={"value": 0x8000})
    assert pc.status_code == 200, pc.text
    ran = post(api_client, emulator_id, "/run_frames", {"count": 2})
    assert ran.status_code == 200, ran.text
    assert ran.json()["pc"] == IN_PC + 2, ("the program did not reach its JR $", ran.json())


class TestBusWebApi:

    def test_list_names_the_buses_and_their_devices(self, api_client, tsconf_emulator):
        listed = buses(api_client, tsconf_emulator)
        assert {"zc.spi", "ide0"} <= set(listed), listed.keys()
        spi = listed["zc.spi"]
        assert spi["protocol"] == "spi" and spi["trace"]["enabled"] is False and spi["checks"]["enabled"] is False
        sd = [d for d in spi["devices"] if d.get("name") == "sd"]
        assert sd and sd[0]["sd"]["timing"] == "ideal", spi["devices"]
        assert listed["ide0"]["protocol"] == "ata"

    def test_trace_journal_switches_one_bus_or_all(self, api_client, tsconf_emulator):
        reply = post(api_client, tsconf_emulator, "/bus/zc.spi/trace-journal", {"enabled": True, "checks": True, "protocol_only": True})
        assert reply.status_code == 200, reply.text
        listed = buses(api_client, tsconf_emulator)
        assert listed["zc.spi"]["trace"]["enabled"] is True and listed["zc.spi"]["trace"]["protocol_only"] is True
        assert listed["zc.spi"]["checks"]["enabled"] is True
        assert listed["ide0"]["trace"]["enabled"] is False
        assert post(api_client, tsconf_emulator, "/bus/all/trace-journal", {"enabled": True}).status_code == 200
        assert buses(api_client, tsconf_emulator)["ide0"]["trace"]["enabled"] is True
        assert post(api_client, tsconf_emulator, "/bus/all/trace-journal", {"enabled": False, "checks": False}).status_code == 200
        assert all(not b["trace"]["enabled"] for b in buses(api_client, tsconf_emulator).values())

    def test_the_dma_collision_is_found_with_the_pc(self, api_client, tsconf_emulator):
        assert post(api_client, tsconf_emulator, "/bus/zc.spi/trace-journal", {"enabled": True, "checks": True, "clear": True}).status_code == 200
        run_collision(api_client, tsconf_emulator)
        findings = get(api_client, tsconf_emulator, "/bus/zc.spi/findings?rules=dma_collision").json()
        hits = findings["findings"]
        assert hits, (findings, buses(api_client, tsconf_emulator)["zc.spi"]["trace"])
        assert hits[0]["rule"] == "dma_collision" and hits[0]["severity"] == "error"
        assert hits[0]["pc"] == "0x%04X" % IN_PC, hits[0]
        assert findings["checks"]["by_rule"]["dma_collision"] >= 1
        dma = get(api_client, tsconf_emulator, "/bus/zc.spi/events?kinds=byte&limit=5").json()["events"]
        assert dma and all(e["source"] == "dma" for e in dma), dma
        # no select assertion: a select event is a change of the chip selects, and the BIOS may have left the card selected
        assert dma[0]["slot"] == 0, dma

    def test_since_reads_only_newer_events(self, api_client, tsconf_emulator):
        post(api_client, tsconf_emulator, "/bus/zc.spi/trace-journal", {"enabled": True, "clear": True})
        run_collision(api_client, tsconf_emulator)
        events = get(api_client, tsconf_emulator, "/bus/zc.spi/events?limit=0").json()["events"]
        assert len(events) > 2
        middle = events[len(events) // 2]["seq"]
        newer = get(api_client, tsconf_emulator, f"/bus/zc.spi/events?since={middle}&limit=0").json()["events"]
        assert newer and all(e["seq"] > middle for e in newer)

    def test_sd_timing_reports_and_changes_the_profile(self, api_client, tsconf_emulator):
        assert get(api_client, tsconf_emulator, "/bus/zc.spi/sd-timing").status_code == 200
        reply = post(api_client, tsconf_emulator, "/bus/zc.spi/sd-timing", {"profile": "slow", "seed": 7})
        assert reply.status_code == 200, reply.text
        api_client.session.post(api_client._url(f"/api/v1/emulator/{tsconf_emulator}/run_frames"), json={"count": 1})
        sd = [d for d in buses(api_client, tsconf_emulator)["zc.spi"]["devices"] if d.get("name") == "sd"][0]["sd"]
        assert sd["timing"] == "slow", sd
        assert post(api_client, tsconf_emulator, "/bus/zc.spi/sd-timing", {"profile": "fast"}).status_code == 400
        no_card = post(api_client, tsconf_emulator, "/bus/ide0/sd-timing", {"profile": "slow"})
        assert no_card.status_code == 404 and "no SD card" in no_card.json()["message"], no_card.text

    def test_export_vcd_and_jsonl(self, api_client, tsconf_emulator):
        post(api_client, tsconf_emulator, "/bus/zc.spi/trace-journal", {"enabled": True, "clear": True})
        run_collision(api_client, tsconf_emulator)
        vcd = get(api_client, tsconf_emulator, "/bus/zc.spi/export?format=vcd")
        assert vcd.status_code == 200 and "$timescale 1ns $end" in vcd.text and "sck" in vcd.text, vcd.text[:300]
        jsonl = get(api_client, tsconf_emulator, "/bus/zc.spi/export?format=jsonl&kinds=byte&limit=8")
        assert jsonl.status_code == 200, jsonl.text
        lines = [json.loads(line) for line in jsonl.text.splitlines() if line.strip()]
        # the first line is the bus header, then one event per line
        assert lines[0]["bus"] == "zc.spi" and lines[0]["devices"][0]["name"] == "sd", lines[0]
        assert len(lines) > 1 and all(line["kind"] == "byte" for line in lines[1:]), lines[:3]
        assert get(api_client, tsconf_emulator, "/bus/zc.spi/export?format=wav").status_code == 400

    @pytest.mark.parametrize("path", [
        "/bus/zc.spi/events?kinds=jump",
        "/bus/zc.spi/events?limit=x",
        "/bus/zc.spi/findings?rules=everything",
    ])
    def test_bad_parameters_are_400(self, api_client, tsconf_emulator, path):
        response = get(api_client, tsconf_emulator, path)
        assert response.status_code == 400, (path, response.text)
        assert response.json().get("message"), response.text

    def test_an_unknown_bus_names_the_machines_buses(self, api_client, tsconf_emulator):
        response = get(api_client, tsconf_emulator, "/bus/next.spi/events")
        assert response.status_code in (400, 404), response.text
        assert "zc.spi" in response.text

    @pytest.mark.parametrize("path", ["/bus", "/bus/zc.spi/events", "/bus/zc.spi/findings", "/bus/zc.spi/export"])
    def test_unknown_emulator_is_404(self, api_client, path):
        response = get(api_client, "00000000-0000-0000-0000-000000000000", path)
        assert response.status_code == 404, (path, response.text)


class TestBusBindings:

    def test_python_bindings(self, api_client, tsconf_emulator):
        if not api_client.get_python_status().get("available", False):
            pytest.skip("Python interpreter not available")
        run_collision(api_client, tsconf_emulator)  # nothing traced yet: the trace starts below
        code = """
import unreal_emulator as ue
emu = ue.emu_get('__EMU_ID__')
assert emu is not None, 'emulator not found'
ids = [b['id'] for b in emu.bus_list()['buses']]
assert 'zc.spi' in ids and 'ide0' in ids, ids
st = emu.bus_control('zc.spi', enabled=True, checks=True, clear=True, protocol_only=False)
assert st['trace']['enabled'] is True, st
assert emu.bus_events('zc.spi', limit=0)['events'] == []
try:
    emu.bus_events('zc.spi', kinds='jump')
    raise AssertionError('no ValueError for a bad kind')
except ValueError as e:
    assert 'kinds' in str(e), str(e)
assert emu.bus_sd_timing('zc.spi')['devices'] is not None
assert emu.bus_export('zc.spi', 'jsonl').splitlines() == [emu.bus_export('zc.spi', 'jsonl').splitlines()[0]]  # the header only
assert '"bus":"zc.spi"' in emu.bus_export('zc.spi', 'jsonl')
emu.bus_control('all', enabled=False, checks=False)
print('BUS_PY_OK')
""".replace("__EMU_ID__", tsconf_emulator)
        resp = api_client.exec_python(code)
        assert resp["success"] is True, resp
        assert "BUS_PY_OK" in resp.get("output", ""), resp

    def test_lua_bindings(self, api_client, tsconf_emulator):
        if not api_client.get_lua_status().get("available", False):
            pytest.skip("Lua interpreter not available")
        post(api_client, tsconf_emulator, "/bus/zc.spi/trace-journal", {"enabled": True, "checks": True, "clear": True})
        run_collision(api_client, tsconf_emulator)
        code = """
local emu = assert(emu_get('__EMU_ID__'))
local list = assert(bus_list())
local found = false
for _, b in ipairs(list.buses) do if b.id == "zc.spi" then found = b.trace.enabled end end
assert(found == true, "zc.spi traced")
local f = assert(bus_findings("zc.spi", {rules="dma_collision"}))
assert(#f.findings >= 1 and f.findings[1].pc == "__PC__", "finding at the IN: " .. tostring(f.findings[1] and f.findings[1].pc))
local ev = assert(bus_events("zc.spi", {kinds="byte", limit=5}))
assert(#ev.events > 0 and ev.events[1].source == "dma", "the DMA's bytes")
local ok, err = bus_events("zc.spi", {kinds="jump"})
assert(ok == nil and string.find(err, "kinds", 1, true), "bad kind: " .. tostring(err))
ok, err = bus_events("next.spi")
assert(ok == nil and string.find(err, "zc.spi", 1, true), "unknown bus: " .. tostring(err))
local vcd = assert(bus_export("zc.spi", {format="vcd"}))
assert(string.find(vcd, "$timescale", 1, true), "vcd")
assert(bus_control("all", {enabled=false, checks=false}))
print("BUS_LUA_OK")
""".replace("__EMU_ID__", tsconf_emulator).replace("__PC__", "0x%04X" % IN_PC)
        resp = api_client.exec_lua(code)
        assert resp["success"] is True, resp
        assert "BUS_LUA_OK" in resp.get("output", ""), resp


class Cli:
    """Line-oriented client for the CLI port (telnet negotiation stripped)"""

    def __init__(self, port):
        self.sock = socket.create_connection(("localhost", port), timeout=5)
        self.sock.settimeout(0.4)
        self.read_quiet()

    def read_quiet(self):
        data = b""
        while True:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
        out, i = bytearray(), 0
        while i < len(data):
            if data[i] == 0xFF and i + 2 < len(data):
                i += 3
                continue
            out.append(data[i])
            i += 1
        return out.decode("utf-8", "replace")

    def run(self, line):
        self.sock.sendall((line + "\n").encode())
        deadline = time.time() + 5
        text = ""
        while time.time() < deadline:
            text += self.read_quiet()
            if text.strip():
                return text
        return text

    def close(self):
        self.sock.close()


class TestBusCli:

    def test_cli_reads_the_same_finding(self, api_client, tsconf_emulator):
        port = int(os.environ.get("UNREAL_CLI_PORT", "8765"))
        try:
            cli = Cli(port)
        except OSError:
            pytest.skip(f"no CLI on port {port}")
        try:
            assert "Error" not in cli.run(f"select {tsconf_emulator}")
            listed = cli.run("bus")
            assert "zc.spi (spi)" in listed and "ide0 (ata)" in listed, listed
            on = cli.run("bus zc.spi on protocol")
            assert "trace on (protocol only)" in on, on
            cli.run("bus zc.spi on")
            cli.run("bus zc.spi clear")
            run_collision(api_client, tsconf_emulator)
            findings = cli.run("bus zc.spi findings")
            assert "error dma_collision" in findings and "pc=0x%04X" % IN_PC in findings, findings
            assert "kinds" in cli.run("bus zc.spi events kinds=jump")
            assert "ata_write_while_busy" in cli.run("bus help")
            assert "trace off" in cli.run("bus all off")
        finally:
            cli.close()
