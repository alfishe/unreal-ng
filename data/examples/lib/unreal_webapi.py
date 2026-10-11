"""A small unreal-ng WebAPI client for the examples: Python 3.8+ standard library only.

    from unreal_webapi import Unreal
    emu = Unreal().create("48K")            # or Unreal().attach() for the instance already running
    emu.load_snapshot("demo.sna")
    emu.post("/run_frames", {"count": 50})

Every method takes paths relative to /api/v1/emulator/{id}. The whole API: GET /api/v1/openapi.json,
or the recipes in the repository's .recipe/ directory.
"""

import json
import os
import urllib.error
import urllib.request

DEFAULT_BASE = os.environ.get("UNREAL_WEBAPI_URL") or "http://localhost:{}/api/v1".format(
    os.environ.get("UNREAL_WEBAPI_PORT", "8090"))


class WebApiError(RuntimeError):
    pass


class Unreal:
    """The server: lists, creates and attaches emulator instances."""

    def __init__(self, base=DEFAULT_BASE, timeout=60):
        self.base = base.rstrip("/")
        self.timeout = timeout

    def request(self, method, path, body=None, raw=False):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body_bytes = resp.read()
        except urllib.error.HTTPError as e:
            raise WebApiError("{} {}: HTTP {} {}".format(method, path, e.code, e.read().decode(errors="replace")))
        except urllib.error.URLError as e:
            raise WebApiError("{} {}: {} (is unreal-qt running? WebAPI at {})".format(method, path, e.reason, self.base))
        if raw:
            return body_bytes
        return json.loads(body_bytes) if body_bytes else {}

    def emulators(self):
        return self.request("GET", "/emulator").get("emulators", [])

    def create(self, model):
        """A new instance of `model` (48K, PENTAGON, 128K, ...: GET /api/v1/emulator/models)."""
        reply = self.request("POST", "/emulator/start", {"model": model})
        return Emulator(self, reply["id"])

    def attach(self, emulator_id=None):
        """The instance with this id, or the only one running (the unreal-qt window's own)."""
        if emulator_id:
            return Emulator(self, emulator_id)
        running = self.emulators()
        if len(running) != 1:
            raise WebApiError("{} emulators running: give an id ({})".format(
                len(running), ", ".join(e.get("id", "?") for e in running)))
        return Emulator(self, running[0]["id"])


class Emulator:
    """One emulator instance."""

    def __init__(self, server, emulator_id):
        self.server = server
        self.id = emulator_id

    def get(self, path):
        return self.server.request("GET", "/emulator/{}{}".format(self.id, path))

    def post(self, path, body=None):
        return self.server.request("POST", "/emulator/{}{}".format(self.id, path), {} if body is None else body)

    def put(self, path, body=None):
        return self.server.request("PUT", "/emulator/{}{}".format(self.id, path), {} if body is None else body)

    def delete(self, path=""):
        return self.server.request("DELETE", "/emulator/{}{}".format(self.id, path))

    # --- the calls the examples share ---

    def remove(self):
        """Destroy the instance."""
        self.delete("")

    def mute(self, muted=True):
        self.put("/audio/mixer/master", {"muted": muted})

    def load_snapshot(self, path):
        """Load a snapshot file the emulator process can read. A model switch gives a new instance id."""
        reply = self.post("/snapshot/load", {"path": os.path.abspath(path)})
        self.id = reply.get("emulator_id", self.id)
        return reply

    def import_symbols(self, path, fmt="sjasmplus-sym"):
        """Labels for the disassembler and the debugger. `fmt`: a format id from GET /symbols/formats
        (sjasmplus-sym, sjasmplus-sld, z88dk-map, ...); None = by the file's extension."""
        body = {"path": os.path.abspath(path)}
        if fmt:
            body["format"] = fmt
        return self.post("/symbols/import", body)

    def pause(self):
        self.post("/pause")

    def resume(self):
        self.post("/resume")

    def enable_breakpoints(self):
        """Breakpoints fire only with the `breakpoints` feature on (it switches debug mode on too)."""
        self.put("/feature/breakpoints", {"enabled": True})

    def add_breakpoint(self, address, kind="exec", **extra):
        body = {"type": kind, "address": address}
        body.update(extra)
        return self.post("/breakpoints", body)["id"]

    def remove_breakpoint(self, breakpoint_id):
        self.delete("/breakpoints/{}".format(breakpoint_id))

    def steps(self, count=100000):
        """Run up to `count` instructions on a paused machine; a breakpoint ends the run early.
        Returns the reply: {executed, stop: {reason: step | breakpoint, breakpoint_id, address}, pc, sp}."""
        return self.post("/steps", {"count": count})

    def time(self):
        """(frame, t in frame, frame length): one coherent reading."""
        t = self.get("/debug/snapshot?disasm=0&stack=0")["time"]
        return t["frame"], t["t"], t["frame_t"]

    def registers(self):
        return self.get("/registers")

    def read_memory(self, address, length):
        """Bytes at a Z80 address."""
        return self.server.request("GET", "/emulator/{}/memory/{}?len={}&format=binary".format(self.id, address, length),
                                   raw=True)

    def screenshot(self, path):
        return self.get("/capture/screen?format=png&path={}".format(os.path.abspath(path)))


def load_sym(path):
    """sjasmplus --sym output ("Name: EQU 0x00008000") as {name: address}."""
    symbols = {}
    with open(path) as f:
        for line in f:
            name, sep, rest = line.partition(":")
            if sep and "EQU" in rest:
                symbols[name.strip()] = int(rest.split("EQU")[1].strip(), 0)
    return symbols
