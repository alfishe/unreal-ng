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

Standard library only.
"""

from __future__ import annotations

import json
import os
import platform
import socket
import sys
import tempfile
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
            raise GuiError("failed", "named pipes: connect with tcp= (start the program with --gui-control-port)")
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


if __name__ == "__main__":
    # A tiny smoke: the hosts, then the first one's tree
    for entry in hosts():
        print(entry["id"], entry.get("program"), entry["socket"])
    if hosts():
        with Gui.connect() as gui:
            json.dump(gui.list(), sys.stdout, indent=2)
            print()
