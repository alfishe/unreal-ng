"""A small unreal-ng WebAPI client for the examples: Python 3.8+ standard library only.

    from unreal_webapi import connect, attach
    emu = connect().create("48K")         # finds unreal-qt (waits for it to start), makes a new instance
    emu = attach()                        # or one already running: asks which when there are several
    emu.load_snapshot("demo.sna")
    emu.post("/run_frames", {"count": 50})

Finding the emulator: --url / UNREAL_WEBAPI_URL when given, else localhost ports UNREAL_WEBAPI_PORT and 8090..8099.
Every Emulator method takes paths relative to /api/v1/emulator/{id}; the whole API: GET /api/v1/openapi.json, or the
recipes in the repository's .recipe/ directory.

Remote control: emu.take_control("my-script") opens an automation session. The emulator window then shows who controls
it and locks its own controls; the user can take control back from the toolbar. emu.release_control() ends it.

Errors: WebApiError for a refused request (bad arguments, ...); its subclasses ConnectionLost when the emulator
process does not answer any more (closed, crashed), InstanceGone when the instance was removed, ControlRevoked when
the user took control back (stop: do not retry, do not take control again by yourself) and ControlHeld when someone
else holds the control.
"""

import http.client
import json
import os
import socket
import urllib.error
import urllib.parse
import urllib.request

import unreal_tui as tui

DEFAULT_PORTS = range(8090, 8100)


class WebApiError(RuntimeError):
    def __init__(self, message, status=None):
        super().__init__(message)
        self.status = status


class ConnectionLost(WebApiError):
    """The emulator process does not answer: it was closed, it crashed, or the port is wrong."""


class InstanceGone(WebApiError):
    """The emulator process answers, but the instance is not there any more."""


class ControlRevoked(WebApiError):
    """The user took control back in the emulator window: stop, do not retry, do not take control again."""


class ControlHeld(WebApiError):
    """Run control is held by another client, or the user refused this one a re-take for a while."""


SESSION_HEADER = "X-Unreal-Session"


def candidate_bases(url=None):
    """WebAPI base URLs to try, the most likely first."""
    if url:
        return [url.rstrip("/")]
    if os.environ.get("UNREAL_WEBAPI_URL"):
        return [os.environ["UNREAL_WEBAPI_URL"].rstrip("/")]
    ports = []
    if os.environ.get("UNREAL_WEBAPI_PORT", "").isdigit():
        ports.append(int(os.environ["UNREAL_WEBAPI_PORT"]))
    ports += [p for p in scan_ports() if p not in ports]
    return ["http://localhost:{}/api/v1".format(p) for p in ports]


def scan_ports():
    """The ports to look on: UNREAL_WEBAPI_PORTS ("8090-8099" or "8090,8190"), else 8090..8099."""
    spec = os.environ.get("UNREAL_WEBAPI_PORTS", "").strip()
    if not spec:
        return list(DEFAULT_PORTS)
    ports = []
    for part in spec.split(","):
        first, _, last = part.strip().partition("-")
        if first.isdigit():
            ports += range(int(first), int(last) + 1) if last.isdigit() else [int(first)]
    return ports


class Unreal:
    """One emulator process (its WebAPI server): lists, creates and attaches instances."""

    def __init__(self, base, timeout=60):
        self.base = base.rstrip("/")
        self.timeout = timeout
        self.status = {}

    @property
    def where(self):
        parts = urllib.parse.urlsplit(self.base)
        return parts.netloc

    def request(self, method, path, body=None, raw=False, timeout=None, session=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        if session:
            req.add_header(SESSION_HEADER, session)
        try:
            with urllib.request.urlopen(req, timeout=timeout or self.timeout) as resp:
                body_bytes = resp.read()
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace")
            try:
                error = json.loads(text).get("error", "")
            except ValueError:
                error = ""
            if e.code == 409 and error == "revoked_by_user":
                raise ControlRevoked("the user took control back in the emulator window", e.code)
            if e.code == 409 and error in ("held_by_other", "revoked_cooldown"):
                raise ControlHeld("{}: {}".format(error, text), e.code)
            raise WebApiError("{} {}: HTTP {} {}".format(method, path, e.code, text), e.code)
        except (urllib.error.URLError, http.client.HTTPException, ConnectionError, socket.timeout) as e:
            reason = getattr(e, "reason", e)
            raise ConnectionLost("unreal-ng at {} does not answer ({}): was it closed?".format(self.where, reason))
        if raw:
            return body_bytes
        try:
            return json.loads(body_bytes) if body_bytes else {}
        except ValueError:
            raise WebApiError("{} {}: not a WebAPI answer (another program on {}?)".format(method, path, self.where))

    def probe(self):
        """True when an unreal-ng WebAPI answers here (quickly: for scanning ports)."""
        try:
            self.status = self.request("GET", "/emulator/status", timeout=0.5)
            return "emulator_count" in self.status
        except WebApiError:
            return False

    def identity(self):
        """What tells this process from another: its pid; older builds without one: the ids of its instances."""
        pid = self.status.get("server", {}).get("pid")
        if pid is not None:
            return "pid", pid
        try:
            ids = tuple(sorted(e.get("id", "") for e in self.emulators()))
        except WebApiError:
            ids = ()
        return ("instances", ids) if ids else ("url", self.base)

    def describe(self):
        server = self.status.get("server", {})
        pid = server.get("pid")
        return "unreal-ng {} ({}) at {}{}, {} instance(s)".format(
            server.get("version", "?"), server.get("git_commit", "?"), self.where,
            ", pid {}".format(pid) if pid is not None else "", self.status.get("emulator_count", 0))

    def emulators(self):
        return self.request("GET", "/emulator").get("emulators", [])

    def create(self, model):
        """A new instance of `model` (48K, PENTAGON, 128K, ...: GET /api/v1/emulator/models)."""
        reply = self.request("POST", "/emulator/start", {"model": model})
        return Emulator(self, reply["id"], model)


def find_servers(url=None):
    """Every emulator process that answers, once: one process answers on its WebAPI port and on its MCP port."""
    found, seen = [], set()
    for base in candidate_bases(url):
        server = Unreal(base)
        if not server.probe():
            continue
        identity = server.identity()
        if identity not in seen:
            seen.add(identity)
            found.append(server)
    return found


def connect(url=None, wait=True):
    """The emulator process: found on the usual ports, waited for when none runs yet, chosen when several answer."""
    servers = find_servers(url)
    if not servers and wait:
        ports = scan_ports()
        where = url or os.environ.get("UNREAL_WEBAPI_URL") or "localhost:{}".format(
            "{}..{}".format(min(ports), max(ports)) if len(ports) > 1 else ports[0] if ports else "?")
        servers = tui.wait_until(lambda: find_servers(url), "Waiting for unreal-ng (start unreal-qt) on " + where)
    if not servers:
        raise ConnectionLost("no unreal-ng WebAPI found ({})".format(", ".join(candidate_bases(url))))
    if len(servers) == 1:
        return servers[0]
    index = tui.choose("Several unreal-ng processes answer. Which one?", [s.describe() for s in servers])
    if index is None:
        raise SystemExit("several unreal-ng processes answer: pick one with --url" if not tui.INTERACTIVE
                         else "cancelled")
    return servers[index]


def describe_instance(server, info):
    window = "window" if info.get("symbolic_id") == "unreal-qt" else "no window"
    return "{:<10} {:<8} {}  {:<9} at {}".format(info.get("model", "?"), info.get("state", "?"),
                                                 info.get("id", "?")[:8], window, server.where)


def find_instances(url=None):
    """(server, instance info) for every instance of every emulator process found."""
    found = []
    for server in find_servers(url):
        try:
            found += [(server, info) for info in server.emulators()]
        except WebApiError:
            pass
    return found


def attach(emulator_id=None, url=None, wait=True):
    """An instance already running: the one with this id (or id prefix), else the only one, else the user picks.
    Waits while there is none."""
    def matching():
        found = find_instances(url)
        if emulator_id:
            found = [(s, i) for s, i in found if i.get("id", "").startswith(emulator_id)]
        return found

    found = matching()
    if not found and wait:
        what = "instance {}".format(emulator_id) if emulator_id else "an emulator instance"
        found = tui.wait_until(matching, "Waiting for {} (start unreal-qt)".format(what))
    if not found:
        raise InstanceGone("no emulator instance{} found".format(" " + emulator_id if emulator_id else ""))
    index = 0
    if len(found) > 1:
        shown = {"found": found}                            # what the menu shows: r reads the list anew

        def refreshed():
            shown["found"] = matching()
            return [describe_instance(s, i) for s, i in shown["found"]]

        index = tui.choose("Several emulator instances run. Which one?",
                           [describe_instance(s, i) for s, i in found], refresh=refreshed)
        if index is None:
            raise SystemExit("several emulator instances run: give one with --attach ID" if not tui.INTERACTIVE
                             else "cancelled")
        found = shown["found"]
    server, info = found[index]
    return Emulator(server, info["id"], info.get("model"))


class Emulator:
    """One emulator instance."""

    def __init__(self, server, emulator_id, model=None):
        self.server = server
        self.id = emulator_id
        self.model = model
        self.session = None      # the automation session's id while this client holds run control
        self.implicit_session = None   # the session the emulator opened by itself (the automation gate), if any
        self.implicit_advice = None    # what it advised then

    def _call(self, method, path, body=None, raw=False):
        try:
            reply = self.server.request(method, "/emulator/{}{}".format(self.id, path), body, raw=raw,
                                        session=self.session)
            # A change without a session of our own: the emulator opened one (the automation gate) and says so
            if isinstance(reply, dict) and isinstance(reply.get("automation_session"), dict):
                gate = reply["automation_session"]
                if gate.get("opened") or self.implicit_session is None:
                    self.implicit_session = gate.get("id")
                    self.implicit_advice = gate.get("advice")
            return reply
        except (ConnectionLost, ControlRevoked, ControlHeld):
            raise
        except WebApiError as e:
            if e.status == 404 and not self.exists():
                raise InstanceGone("emulator instance {} is gone (removed or the machine was switched)".format(
                    self.id[:8]), 404)
            raise

    def exists(self):
        try:
            self.server.request("GET", "/emulator/{}".format(self.id), timeout=5)
            return True
        except ConnectionLost:
            raise
        except WebApiError:
            return False

    def get(self, path):
        return self._call("GET", path)

    def post(self, path, body=None):
        return self._call("POST", path, {} if body is None else body)

    def put(self, path, body=None):
        return self._call("PUT", path, {} if body is None else body)

    def delete(self, path=""):
        return self._call("DELETE", path)

    # --- the calls the examples share ---

    def remove(self):
        """Destroy the instance."""
        self.delete("")

    def take_control(self, client, lease_seconds=60):
        """Open an automation session: the emulator window shows `client` controls it and locks its controls until
        release_control() (or `lease_seconds` without a request). ControlHeld when someone else holds it."""
        reply = self.post("/control/session", {"client": client, "lease_seconds": lease_seconds})
        self.session = reply["session"]
        return reply

    def release_control(self):
        """End the automation session (nothing when there is none, or the user already took control back)."""
        if not self.session:
            return
        try:
            self.delete("/control/session")
        except WebApiError:
            pass
        self.session = None

    def mute(self, muted=True):
        self.put("/audio/mixer/master", {"muted": muted})

    def load_snapshot(self, path):
        """Load a snapshot file the emulator process can read. A model switch gives a new instance id."""
        reply = self.post("/snapshot/load", {"path": os.path.abspath(path)})
        self.id = reply.get("emulator_id", self.id)
        return reply

    def import_symbols(self, path, fmt=None):
        """Labels for the disassembler and the debugger. `fmt`: a format id from GET /symbols/formats
        (sjasmplus-sym, sjasmplus-sld, z88dk-map, ...); None = by the file (extension and content)."""
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
        """`address`: a number, "0x8000" / "#8000", or a label's name."""
        body = {"type": kind, "address": address}
        body.update(extra)
        return self.post("/breakpoints", body)["id"]

    def remove_breakpoint(self, breakpoint_id):
        self.delete("/breakpoints/{}".format(breakpoint_id))

    def steps(self, count=100000):
        """Run up to `count` instructions on a paused machine; a breakpoint ends the run early.
        Returns the reply: {executed, stop: {reason: step | breakpoint, breakpoint_id, address}, pc, sp}."""
        return self.post("/steps", {"count": count})

    def snapshot(self):
        """One coherent reading: {time: {frame, t, frame_t, ...}, regs: {main, alternate, index, special, ...}, ...}."""
        return self.get("/debug/snapshot?disasm=0&stack=0")

    def time(self):
        """(frame, t in frame, frame length)."""
        t = self.snapshot()["time"]
        return t["frame"], t["t"], t["frame_t"]

    def registers(self):
        return self.get("/registers")

    def read_memory(self, address, length):
        """Bytes at a Z80 address."""
        return self._call("GET", "/memory/{}?len={}&format=binary".format(address, length), raw=True)

    def screenshot(self, path):
        return self.get("/capture/screen?format=png&path={}".format(os.path.abspath(path)))


def register_value(regs, name):
    """A register from a /registers or snapshot "regs" object by name: pc, sp, ix, iy, af, bc, de, hl, a, b, ...,
    af_ / a_ ... (the alternate set), i, r."""
    name = name.lower()
    groups = [regs.get(g, {}) for g in ("main", "index", "special", "alternate")]
    for group in groups:
        if name in group:
            return group[name]
    halves = {"a": ("af", 8), "f": ("af", 0), "b": ("bc", 8), "c": ("bc", 0), "d": ("de", 8), "e": ("de", 0),
              "h": ("hl", 8), "l": ("hl", 0), "ixh": ("ix", 8), "ixl": ("ix", 0), "iyh": ("iy", 8), "iyl": ("iy", 0),
              "a_": ("af_", 8), "f_": ("af_", 0), "b_": ("bc_", 8), "c_": ("bc_", 0), "d_": ("de_", 8),
              "e_": ("de_", 0), "h_": ("hl_", 8), "l_": ("hl_", 0)}
    if name in halves:
        pair, shift = halves[name]
        return (register_value(regs, pair) >> shift) & 0xFF
    raise KeyError("no register {}".format(name))


def load_sym(path):
    """sjasmplus --sym output ("Name: EQU 0x00008000") as {name: address}."""
    symbols = {}
    with open(path) as f:
        for line in f:
            name, sep, rest = line.partition(":")
            if sep and "EQU" in rest:
                symbols[name.strip()] = int(rest.split("EQU")[1].strip(), 0)
    return symbols
