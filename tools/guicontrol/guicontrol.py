#!/usr/bin/env python3
"""A client of the GUI control plane (docs/emulator/design/gui-control/README.md).

Drives the windows and views of running unreal-ng GUI programs over their local socket (or a TCP listener):
demo scripts and GUI tests use it instead of synthetic clicks.

    from guicontrol import Gui
    gui = Gui.connect()                         # the only program running, else the oldest one
    gui.call("memory.goto", addr="0x6A00")      # the focused memory view
    gui.call("window.move", target="debugger", x=40, y=40, w=1200, h=800)
    print(gui.state("memory"))
    for event in gui.watch(["state"], max_hz=10):
        ...

    web = WebGui(8090)                          # the same through a program's core: REST /api/v1/gui (design §4.10)
    web = WebGui(8090, via="mcp", mcp_port=8290)  # or its MCP's gui_<interface> tools

Standard library only.
"""

from __future__ import annotations

import json
import os
import platform
import socket
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Iterator, Optional


class GuiError(RuntimeError):
    """A refused or failed call: `error` is the plane's code (not_found, bad_request, ambiguous, ...)."""

    def __init__(self, error: str, message: str, body: Optional[dict] = None):
        super().__init__(f"{error}: {message}")
        self.error = error
        self.message = message
        self.body = body or {}


def discovery_directory() -> Path:
    """Where GUI programs write their discovery files: UNREAL_GUI_CONTROL_DIR, else Qt's runtime location."""
    overridden = os.environ.get("UNREAL_GUI_CONTROL_DIR")
    if overridden:
        return Path(overridden)
    system = platform.system()
    if system == "Darwin":
        base = Path.home() / "Library" / "Application Support"
    elif system == "Windows":
        base = Path.home()
    else:
        runtime = os.environ.get("XDG_RUNTIME_DIR")
        base = Path(runtime) if runtime else Path(tempfile.gettempdir()) / f"runtime-{os.environ.get('USER', '')}"
    return base / "unreal-ng" / "gui"


def _alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if platform.system() == "Windows":
        return True  # the connect attempt tells
    try:
        os.kill(pid, 0)
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def hosts(directory: Optional[Path] = None) -> list[dict]:
    """The running GUI programs, oldest first (the discovery entries of live processes)."""
    directory = directory or discovery_directory()
    entries = []
    for file in sorted(directory.glob("*.json")) if directory.is_dir() else []:
        try:
            entry = json.loads(file.read_text())
        except (OSError, ValueError):
            continue
        if entry.get("id") and entry.get("socket") and _alive(int(entry.get("pid", 0))):
            entries.append(entry)
    return sorted(entries, key=lambda e: e.get("started", 0))


class _Pipe:
    """A Windows named pipe with the few socket calls Gui makes (no timeouts: a pipe read blocks)."""

    def __init__(self, name: str):
        self._file = open(name, "r+b", buffering=0)

    def makefile(self, mode: str):
        return self._file

    def sendall(self, data: bytes) -> None:
        self._file.write(data)

    def settimeout(self, timeout: Optional[float]) -> None:
        pass

    def close(self) -> None:
        try:
            self._file.close()
        except OSError:
            pass


class Gui:
    """One connection to one host."""

    def __init__(self, sock: socket.socket):
        self._sock = sock
        self._reader = sock.makefile("rb")
        self._rid = 0
        self._events: list[dict] = []
        self.welcome = self._read()
        if self.welcome.get("op") != "welcome":
            raise GuiError("failed", f"no welcome from the host: {self.welcome}")

    @classmethod
    def connect(cls, host: Optional[str] = None, tcp: Optional[str] = None, timeout: float = 5.0) -> "Gui":
        """A host by id (the discovery file's), a TCP listener ("ADDRESS:PORT"), else the oldest running one."""
        if tcp:
            address, _, port = tcp.rpartition(":")
            sock = socket.create_connection((address, int(port)), timeout=timeout)
            return cls(sock)
        entries = hosts()
        if host:
            entries = [e for e in entries if e["id"] == host]
        if not entries:
            raise GuiError("not_found", f"no GUI program is running ({discovery_directory()})")
        name = entries[0]["socket"]
        if platform.system() == "Windows":
            return cls(_Pipe(name))
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect(name)
        return cls(sock)

    def close(self) -> None:
        self._reader.close()
        self._sock.close()

    def __enter__(self) -> "Gui":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _read(self) -> dict:
        line = self._reader.readline()
        if not line:
            raise GuiError("failed", "the host closed the connection")
        return json.loads(line)

    def _request(self, message: dict) -> dict:
        self._rid += 1
        message["rid"] = self._rid
        self._sock.sendall((json.dumps(message) + "\n").encode())
        while True:
            answer = self._read()
            if answer.get("op") == "event":
                self._events.append(answer)
            elif answer.get("rid") == self._rid:
                return answer

    def call(self, method: str, target: Optional[str] = None, **params) -> dict:
        """Run a command; its body, or GuiError."""
        message = {"op": "call", "method": method}
        if target:
            message["target"] = target
        if params:
            message["params"] = params
        answer = self._request(message)
        if answer.get("op") == "error" or not answer.get("ok", False):
            raise GuiError(answer.get("error", "failed"), answer.get("message", ""), answer.get("body"))
        return answer.get("body", {})

    def list(self) -> dict:
        return self.call("list")

    def state(self, target: str) -> dict:
        """A node's state, interfaces and machine: {target, state, interfaces, instance?}."""
        return self.call("state", target=target)

    def subscribe(self, topics: list[str], max_hz: int = 20) -> None:
        answer = self._request({"op": "subscribe", "topics": topics, "max_hz": max_hz})
        if answer.get("op") != "subscribed":
            raise GuiError(answer.get("error", "failed"), answer.get("message", ""))

    def watch(self, topics: list[str], max_hz: int = 20) -> Iterator[dict]:
        """The pushed events, until the connection closes."""
        self.subscribe(topics, max_hz)
        self._sock.settimeout(None)
        while True:
            while self._events:
                yield self._events.pop(0)
            yield self._read()


class WebGui:
    """The plane through a program's core (design §4.10): REST /api/v1/gui on its WebAPI port, or the gui_* tools of
    its MCP. The same call / state / list as Gui. `host` pins one host: a target goes there (`<host>/<target>`; no
    target: `<host>/<interface>`, the focused node of the interface there)."""

    def __init__(self, port: int, host: Optional[str] = None, via: str = "rest", mcp_port: Optional[int] = None,
                 timeout: float = 20.0):
        self.port = port
        self.host = host
        self.via = via
        self.mcp_url = f"http://127.0.0.1:{mcp_port or port + 200}/mcp"
        self.timeout = timeout
        self._id = 0

    def close(self) -> None:
        pass

    def __enter__(self) -> "WebGui":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _pin(self, method: str, target: Optional[str]) -> Optional[str]:
        if not self.host:
            return target
        if not target:
            return f"{self.host}/{method.split('.')[0]}"
        return target if target == self.host or target.startswith(self.host + "/") else f"{self.host}/{target}"

    def _http(self, method: str, path: str, body: Optional[dict] = None) -> dict:
        request = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", method=method,
                                         data=json.dumps(body).encode() if body is not None else None,
                                         headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as reply:
                return json.loads(reply.read() or b"{}")
        except urllib.error.HTTPError as e:
            try:
                return json.loads(e.read() or b"{}")
            except ValueError:
                return {"ok": False, "error": "failed", "message": f"HTTP {e.code}"}

    def _tool(self, name: str, arguments: dict) -> dict:
        self._id += 1
        payload = {"jsonrpc": "2.0", "id": self._id, "method": "tools/call",
                   "params": {"name": name, "arguments": arguments}}
        request = urllib.request.Request(self.mcp_url, data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json",
                                                  "Accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(request, timeout=self.timeout) as reply:
            raw = reply.read().decode()
        if raw.startswith("event:") or raw.startswith("data:"):
            raw = [line[5:].strip() for line in raw.splitlines() if line.startswith("data:")][-1]
        result = json.loads(raw).get("result", {})
        structured = result.get("structuredContent")
        if isinstance(structured, dict) and "ok" in structured:
            return structured
        text = "\n".join(c.get("text", "") for c in result.get("content", []))
        return {"ok": False, "error": "failed", "message": text}

    def _result(self, answer: dict) -> dict:
        if not answer.get("ok", False):
            raise GuiError(answer.get("error", "failed"), answer.get("message", ""), answer.get("body"))
        return answer.get("body", {})

    def call(self, method: str, target: Optional[str] = None, **params) -> dict:
        """Run a command; its body, or GuiError."""
        if method == "list":
            return self.list()
        if method == "state":
            return self.state(target or "")
        target = self._pin(method, target)
        if self.via == "mcp":
            interface, _, action = method.partition(".")
            arguments = dict(params, action=action)
            if target:
                arguments["target"] = target
            return self._result(self._tool(f"gui_{interface}", arguments))
        body = dict(params)
        if target:
            body["target"] = target
        return self._result(self._http("POST", "/api/v1/gui/" + urllib.parse.quote(method), body))

    def list(self) -> dict:
        """Every host ({hosts}); with a pinned host its own entry."""
        answer = self._tool("gui_list", {}) if self.via == "mcp" else self._http("GET", "/api/v1/gui")
        body = self._result(answer)
        if not self.host:
            return body
        for entry in body.get("hosts", []):
            if entry.get("id") == self.host:
                return entry
        raise GuiError("not_found", f"no host {self.host}")

    def state(self, target: str) -> dict:
        """A node's state, interfaces and machine: {target, state, interfaces, instance?}."""
        target = self._pin("", target) if target else target
        if self.via == "mcp":
            return self._result(self._tool("gui_state", {"target": target}))
        return self._result(self._http("GET", "/api/v1/gui/state?target=" + urllib.parse.quote(target, safe="")))


if __name__ == "__main__":
    # A tiny smoke: the hosts, then the first one's tree
    for entry in hosts():
        print(entry["id"], entry.get("program"), entry["socket"])
    if hosts():
        with Gui.connect() as gui:
            json.dump(gui.list(), sys.stdout, indent=2)
            print()
