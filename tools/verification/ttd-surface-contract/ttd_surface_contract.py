#!/usr/bin/env python3
"""TTD surface contract check: the same time-travel calls through WebAPI, CLI,
Lua and Python give the same answers (Phase 5, Step 1, requirement QR-8).

Every surface turns its input into a TTDControl request and the reply into its
own form (JSON, CLI text, Lua table / values, Python dict / RuntimeError), so a
difference here is a mapping bug in one surface. Run it against a running app:

    UNREAL_WEBAPI_PORT=8197 UNREAL_CLI_PORT=8797 unreal-qt &
    python3 tools/verification/ttd-surface-contract/ttd_surface_contract.py \\
        --base-url http://localhost:8197 --cli-port 8797

It creates one instance of its own and removes it at the end; other instances
are left alone. Exit status 0: every check passed.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ttd-analyzer" / "scripts"))
from record_fixtures import ApiError, EmulatorApi, run_frames  # noqa: E402


class Cli:
    """Line-oriented client for the CLI port (telnet negotiation stripped)"""

    def __init__(self, port: int):
        self.sock = socket.create_connection(("localhost", port), timeout=10)
        self.read_quiet()

    def read_quiet(self, settle: float = 0.4) -> str:
        data = b""
        self.sock.settimeout(settle)
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        # Drop telnet IAC sequences (3 bytes each)
        out = bytearray()
        i = 0
        while i < len(data):
            if data[i] == 0xFF and i + 2 < len(data):
                i += 3
                continue
            out.append(data[i])
            i += 1
        return out.decode("utf-8", "replace").replace("\r", "")

    def run(self, line: str, settle: float = 0.6) -> str:
        self.sock.sendall((line + "\n").encode())
        return self.read_quiet(settle)


class Contract:
    python = True  # False when the app was built without Python automation

    def __init__(self, api: EmulatorApi, cli: Cli, emu_id: str):
        self.api = api
        self.cli = cli
        self.base = f"/emulator/{emu_id}"
        self.failures: List[str] = []
        self.checks = 0

    def check(self, ok: bool, what: str) -> None:
        if not self.python and (what.startswith("py ") or what.startswith("Python")):
            return
        self.checks += 1
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            self.failures.append(what)

    # --- one call per surface -------------------------------------------------

    def web(self, method: str, route: str, body: Dict[str, Any] | None = None) -> Tuple[int, Dict[str, Any]]:
        try:
            return 200, self.api._request(method, self.base + route, body) or {}
        except ApiError as e:
            text = str(e)  # "<METHOD> <path> -> HTTP <code>: <body>"
            marker = text.find("-> HTTP ")
            status = int(text[marker + 8:].split(":", 1)[0]) if marker >= 0 else 0
            try:
                payload = json.loads(text[text.index("{"):])
            except ValueError:
                payload = {"raw": text}
            return status, payload

    @staticmethod
    def script_output(reply: str) -> str:
        """What the script printed: the lines after "Output:" up to the prompt
        (the CLI echoes the command and reports success around it)"""
        lines = reply.splitlines()
        if "Output:" in lines:
            lines = lines[lines.index("Output:") + 1:]
        elif lines and (lines[0].startswith("lua exec") or lines[0].startswith("python exec")):
            lines = lines[1:]
        return "\n".join(l for l in lines if l.strip() not in (">", "") and
                         not l.startswith("Lua code executed") and not l.startswith("Python code executed")).strip()

    def lua(self, code: str) -> str:
        return self.script_output(self.cli.run("lua exec " + code))

    def py(self, code: str) -> str:
        # One line: the CLI reads a command per line
        return self.script_output(self.cli.run("python exec " + code))

    def lua_json(self, expr: str) -> Dict[str, Any]:
        # The table as "key<TAB>value" lines (scalars only), sorted
        out = self.lua("local t = " + expr + "; local k = {}; for n, v in pairs(t) do "
                       "if type(v) ~= 'table' then k[#k + 1] = n .. '\\t' .. tostring(v) end end; "
                       "table.sort(k); print(table.concat(k, '\\n'))")
        result: Dict[str, Any] = {}
        for line in out.splitlines():
            if "\t" in line:
                key, value = line.split("\t", 1)
                result[key] = value
        return result

    def py_json(self, expr: str) -> Any:
        out = self.py("import json; print('JSON:' + json.dumps(" + expr + ", sort_keys=True, default=str))")
        for line in out.splitlines():
            if line.startswith("JSON:"):
                return json.loads(line[5:])
        return {"raw": out}


def scalar_text(value: Any) -> str:
    """A JSON scalar as Lua's tostring prints it"""
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def run_contract(c: Contract) -> None:
    print("[status: the same fields on WebAPI, Lua and Python]")
    c.web("POST", "/ttd/start", {"journal": True})
    run_frames(c.api, c.base, 3)
    c.web("POST", "/ttd/stop")
    _, web = c.web("GET", "/ttd/status")
    lua = c.lua_json("ttd_status()")
    py = c.py_json("emu.ttd_status()")
    scalars = {k: v for k, v in web.items() if not isinstance(v, (dict, list)) and v is not None}
    for key, value in sorted(scalars.items()):
        if key in ("session_heap_bytes", "page_store_bytes", "history_bytes"):
            continue  # heap figures move between calls
        c.check(lua.get(key) == scalar_text(value), f"lua  {key} = {lua.get(key)} (WebAPI {value})")
        c.check(isinstance(py, dict) and py.get(key) == value, f"py   {key} = {py.get(key) if isinstance(py, dict) else py} (WebAPI {value})")
    c.check(isinstance(py, dict) and set(web) <= set(py), "py   has every WebAPI status key")

    print("[history-limit: set on one surface, read on the others]")
    c.lua("ttd_set_history_limit(1234, 0)")
    _, web = c.web("POST", "/ttd/history-limit", {})
    c.check(web.get("history_limit_frames") == 1234, f"WebAPI sees the Lua limit ({web.get('history_limit_frames')})")
    py = c.py_json("emu.ttd_set_history_limit(None, None)")
    c.check(py == [1234, 0], f"Python sees it ({py})")
    status, web = c.web("POST", "/ttd/history-limit", {"frames": -1})
    c.check(status == 400, f"WebAPI frames=-1 is 400 ({status})")
    c.web("POST", "/ttd/history-limit", {"frames": 0, "bytes": 0})

    print("[refusals: the same message everywhere while recording]")
    c.web("POST", "/ttd/start")
    run_frames(c.api, c.base, 2)
    status, web = c.web("POST", "/ttd/invalidate", {"reason": "contract"})
    message = web.get("message", "")
    c.check(status == 409 and message != "", f"WebAPI invalidate is 409 ({status})")
    lua = c.lua("local ok, why = ttd_invalidate('contract'); print(tostring(ok) .. '|' .. why)")
    c.check(lua == "false|" + message, f"Lua invalidate answers false and the same message ({lua!r})")
    py = c.py("exec(\"try:\\n    emu.ttd_invalidate('contract')\\n    print('no error')\\nexcept RuntimeError as e:\\n    print('ERR:' + str(e))\")")
    c.check(("ERR:" + message) in py, f"Python invalidate raises the same message ({py!r})")
    cli = c.cli.run("ttd invalidate contract")
    c.check(("Error: " + message) in cli, f"CLI invalidate prints the same message ({cli.strip()!r})")

    status, web = c.web("POST", "/ttd/journal/build", {})
    build_message = web.get("message", "")
    c.check(status == 409 and build_message != "", f"WebAPI journal build is 409 while recording ({status})")
    lua = c.lua("local r = ttd_build_journal(); print(tostring(r.ok) .. '|' .. tostring(r.error))")
    c.check(lua == "false|" + build_message, f"Lua journal build refused with the same message ({lua!r})")
    py = c.py("exec(\"try:\\n    emu.ttd_build_journal()\\n    print('no error')\\nexcept RuntimeError as e:\\n    print('ERR:' + str(e))\")")
    c.check(("ERR:" + build_message) in py, f"Python journal build raises the same message ({py!r})")
    cli = c.cli.run("ttd journal build")
    c.check(build_message in cli, f"CLI journal build refused with the same message ({cli.strip()!r})")

    print("[lifecycle: start / stop agree]")
    lua = c.lua("print(tostring(ttd_start()))")
    c.check(lua == "true", f"Lua ttd_start while recording answers true ({lua!r})")
    c.cli.run("ttd stop")
    _, web = c.web("GET", "/ttd/status")
    c.check(web.get("state") == "idle", f"CLI stop is seen by WebAPI ({web.get('state')})")
    py = c.py_json("emu.ttd_start(journal=False)")
    _, web = c.web("GET", "/ttd/status")
    c.check(py is True and web.get("state") == "recording", f"Python start is seen by WebAPI ({py}, {web.get('state')})")
    c.lua("ttd_stop()")
    _, web = c.web("GET", "/ttd/status")
    c.check(web.get("state") == "idle", f"Lua stop is seen by WebAPI ({web.get('state')})")

    print("[moving in the timeline: seek, steps, resume, position]")
    c.web("POST", "/ttd/start")
    run_frames(c.api, c.base, 8)
    status, web = c.web("POST", "/ttd/seek", {"frame": 1})
    scrub = web.get("message", "")
    c.check(status == 409 and scrub != "", f"WebAPI seek while recording is 409 ({status})")
    lua = c.lua("local r = ttd_seek(1); print(tostring(r.reached) .. '|' .. tostring(r.error))")
    c.check(lua == "false|" + scrub, f"Lua seek while recording: reached false and the same message ({lua!r})")
    cli = c.cli.run("ttd seek 1")
    c.check(("Error: " + scrub) in cli, f"CLI seek while recording prints the same message ({cli.strip()!r})")
    py = c.py_json("emu.ttd_seek(1)")
    c.check(isinstance(py, dict) and py.get("reached") is False and py.get("error") == scrub,
            f"Python seek while recording: reached False and the same message ({py})")
    c.web("POST", "/ttd/stop")
    _, pos = c.web("GET", "/ttd/position")
    end = pos["session_end"]["frame"]
    target = end - 4

    def paused() -> bool:
        return bool(c.api.instance_info(c.base.split("/")[-1]).get("is_paused", False))

    c.api.set_running(c.base.split("/")[-1], True)
    lua = c.lua(f"local r = ttd_seek({target}); print(tostring(r.reached) .. '|' .. r.arrived_at.frame .. '|' .. r.state)")
    c.check(lua == f"true|{target}|detached", f"Lua seek reaches the target ({lua!r})")
    lua_paused = paused()
    _, web = c.web("GET", "/ttd/position")
    c.check(web["current"]["frame"] == target, f"WebAPI position after the Lua seek ({web['current']['frame']})")
    c.api.set_running(c.base.split("/")[-1], True)
    _, web = c.web("POST", "/ttd/seek", {"frame": target})
    c.check(paused() == lua_paused, f"the machine is left the same way after a Lua and a WebAPI seek (paused {lua_paused})")
    cli = c.cli.run("ttd step-back")
    c.check(f"frame={target - 1}," in cli, f"CLI step-back ({cli.strip()!r})")
    lua = c.lua("print(tostring(ttd_step_forward()))")
    _, web = c.web("GET", "/ttd/position")
    c.check(lua == "true" and web["current"]["frame"] == target, f"Lua step-forward seen by WebAPI ({lua!r}, {web['current']['frame']})")
    py = c.py_json("emu.ttd_position()")
    c.check(isinstance(py, dict) and py.get("current", {}).get("frame") == target, f"Python position ({py})")
    status, web = c.web("POST", "/ttd/reverse-step", {})
    c.check(status == 400, f"WebAPI reverse-step without count or tstates is 400 ({status})")
    lua = c.lua("print(tostring(ttd_reverse_step(2)))")
    c.check(lua == "true", f"Lua reverse-step ({lua!r})")
    lua = c.lua("print(tostring(ttd_resume()))")
    _, web = c.web("GET", "/ttd/status")
    c.check(lua == "true" and web.get("state") == "recording", f"Lua resume: recording again ({lua!r}, {web.get('state')})")
    c.check(not paused(), "Lua resume runs the machine again")
    c.web("POST", "/ttd/stop")

    print("[write journal: Lua start() keeps the choice, WebAPI start defaults it off]")
    c.lua("ttd_set_journal_enabled(true)")
    c.lua("ttd_start()")
    _, web = c.web("GET", "/ttd/status")
    c.check(web.get("write_journal_enabled") is True, f"Lua start() keeps the journal on ({web.get('write_journal_enabled')})")
    c.lua("ttd_stop()")
    _, web = c.web("POST", "/ttd/start", {})
    c.check(web.get("write_journal_enabled") is False, f"WebAPI start without journal turns it off ({web.get('write_journal_enabled')})")
    c.web("POST", "/ttd/stop")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base-url", default="http://localhost:8090")
    parser.add_argument("--cli-port", type=int, default=8765)
    args = parser.parse_args()

    api = EmulatorApi(args.base_url, timeout=60.0)
    emu_id = api.create_instance("PENTAGON")
    cli = Cli(args.cli_port)
    try:
        cli.run("select " + emu_id)
        c = Contract(api, cli, emu_id)
        c.python = "not available" not in cli.run("python exec print(1)")
        if not c.python:
            print("(this build has no Python automation: Python checks skipped)")
        run_contract(c)
    finally:
        try:
            api.delete(f"/emulator/{emu_id}")
        except ApiError:
            pass
    print(f"\n{c.checks - len(c.failures)} of {c.checks} checks passed")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(main())
