"""Thin client for the unreal-ng WebAPI: only what the NedoOS layer POC needs."""

import json
import time
import urllib.error
import urllib.request


class WebApi:
    def __init__(self, base="http://localhost:8090", emulator_id=None):
        self.base = base.rstrip("/") + "/api/v1/emulator"
        self.emulator_id = emulator_id or self._single_emulator()
        self._pages = {}

    # --- transport -------------------------------------------------------
    def _request(self, method, path, body=None, absolute=False):
        url = path if absolute else f"{self.base}/{self.emulator_id}/{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=10) as reply:
            text = reply.read()
        return json.loads(text) if text else {}

    def get(self, path):
        return self._request("GET", path)

    def post(self, path, body=None):
        return self._request("POST", path, body if body is not None else {})

    def put(self, path, body):
        return self._request("PUT", path, body)

    def delete(self, path):
        return self._request("DELETE", path)

    def _single_emulator(self):
        listing = self._request("GET", self.base, absolute=True)
        emulators = listing.get("emulators", [])
        if len(emulators) != 1:
            raise SystemExit(f"{len(emulators)} emulators running; pass --id")
        return emulators[0]["id"]

    # --- memory ----------------------------------------------------------
    def ram_page(self, page):
        """Whole 16 KB physical RAM page, cached until flush()."""
        if page not in self._pages:
            reply = self.get(f"memory/ram/{page}/0?len=16384")
            self._pages[page] = bytes(reply["data"])
        return self._pages[page]

    def write_cpu(self, address, data):
        return self.post("memory/write", {"address": address, "data": list(data)})

    def flush(self):
        self._pages.clear()

    # --- CPU and machine -------------------------------------------------
    def registers(self):
        return self.get("registers")

    def set_register(self, name, value):
        self.put(f"registers/{name}", {"value": value})

    def bank_pages(self):
        return [bank["page"] for bank in self.get("state/paging")["banks"]]

    def pause(self):
        """Idempotent: the WebAPI answers 400 when the machine is already paused."""
        if not self.is_paused():
            self.post("pause")
        self.flush()

    def resume(self):
        """Idempotent: the WebAPI answers 400 when the machine already runs."""
        self.flush()
        if self.is_paused():
            self.post("resume")

    def is_paused(self):
        return self.get("breakpoints/status").get("is_paused", False)

    def debug_mode(self, enabled):
        self.put("debugmode", {"enabled": enabled})

    def text_screen(self):
        return self.get("video/text")

    # --- breakpoints -----------------------------------------------------
    def add_exec_breakpoint(self, address, note=""):
        return self.post("breakpoints", {"type": "execution", "address": address, "note": note})

    def clear_breakpoints(self):
        self.delete("breakpoints")

    def run_until(self, predicate, timeout=10.0):
        """Resume, then stop at the first breakpoint hit for which predicate(regs) is true.

        The WebAPI has no page-qualified breakpoint, so the predicate filters hits
        that are the same Z80 address in another page (and stale pause states).
        """
        deadline = time.time() + timeout
        self.resume()
        while time.time() < deadline:
            if self.is_paused():
                self.flush()
                regs = self.registers()
                if predicate(regs):
                    return regs
                self.resume()
            time.sleep(0.005)
        self.pause()
        return None
