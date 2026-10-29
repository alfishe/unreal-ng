#!/usr/bin/env python3
"""GDB Remote Serial Protocol client, agent-expression assembler and host launcher for the verifiers.

- RSPClient: framing ($data#cs), acks / no-ack mode, binary escaping, RLE expansion, console output (O packets),
  interrupts (0x03), raw bytes for malformed input.
- AgentExpr: a small assembler for gdb agent expressions (gdbsupport/ax.def), with forward labels.
- Host: starts unreal-qt offscreen on ports of its own (UNREAL_*_PORT), creates and mutes an emulator instance
  through the WebAPI and stops it at the end; WebAPI: the few REST calls the verifiers need.

Stdlib only.
"""

import json
import os
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# gdb's register numbers (core/automation/gdb/src/gdbtarget_z80.cpp)
REG_AF, REG_BC, REG_DE, REG_HL, REG_SP, REG_PC, REG_IX, REG_IY = range(8)
REG_AF2, REG_BC2, REG_DE2, REG_HL2, REG_IR, REG_IFF, REG_IM = range(8, 15)
REG_P7FFD, REG_P1FFD, REG_PFE = 15, 16, 17
# Bytes per register in g / p replies (paging pseudo-registers are p/P only, not in g)
REG_SIZES = [2] * 13 + [1, 1] + [1, 1, 1]
G_REGISTERS = 15


class RSPError(Exception):
    pass


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def escape_binary(data: bytes) -> bytes:
    """X packet payload: '#', '$', '}' and '*' go as '}' + (byte ^ 0x20)"""
    out = bytearray()
    for b in data:
        if b in (0x23, 0x24, 0x7D, 0x2A):
            out += bytes([0x7D, b ^ 0x20])
        else:
            out.append(b)
    return bytes(out)


def expand_rle(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        if i + 2 < len(data) and data[i + 1] == 0x2A:
            out += bytes([data[i]]) * (data[i + 2] - 29 + 1)
            i += 3
        else:
            out.append(data[i])
            i += 1
    return bytes(out)


def le_hex(value: int, size: int) -> str:
    return value.to_bytes(size, "little").hex()


def parse_le_hex(text: str) -> int:
    return int.from_bytes(bytes.fromhex(text), "little")


def parse_stop(reply: str) -> Tuple[Optional[int], Dict[str, str]]:
    """'T05swbreak:;thread:01;' -> (5, {'swbreak': '', 'thread': '01'}); 'S05' -> (5, {}); else (None, {})"""
    if not reply or reply[0] not in "TS" or len(reply) < 3:
        return None, {}
    try:
        signal = int(reply[1:3], 16)
    except ValueError:
        return None, {}
    pairs = {}
    for item in reply[3:].split(";"):
        if not item:
            continue
        key, _, value = item.partition(":")
        pairs[key] = value
    return signal, pairs


class RSPClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 2000, timeout: float = 3.0, verbose: bool = False):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.verbose = verbose
        self.sock: Optional[socket.socket] = None
        self.buffer = b""
        self.no_ack = False
        self.console: List[str] = []   # decoded O packets
        self.acks: List[str] = []      # '+' / '-' seen from the server

    # --- transport ---

    def connect(self) -> "RSPClient":
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return self

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def log(self, text: str):
        if self.verbose:
            print("    " + (text if len(text) < 160 else text[:160] + "..."))

    def send_raw(self, data: bytes):
        self.log(f">>> {data!r}")
        self.sock.sendall(data)

    def frame(self, payload: bytes) -> bytes:
        return b"$" + payload + b"#" + f"{checksum(payload):02x}".encode()

    def send(self, data, binary: bool = False):
        payload = data if isinstance(data, bytes) else data.encode("latin-1")
        self.send_raw(self.frame(payload))

    def _fill(self, deadline: float) -> bool:
        remaining = deadline - time.time()
        if remaining <= 0:
            return False
        self.sock.settimeout(remaining)
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            return False
        if not chunk:
            raise RSPError("connection closed by the server")
        self.buffer += chunk
        return True

    def read_packet(self, timeout: Optional[float] = None, keep_console: bool = False) -> Optional[str]:
        """The next packet's data (RLE expanded); None on timeout. Acks are recorded, O packets collected unless
        keep_console (then they are returned like any packet)"""
        deadline = time.time() + (self.timeout if timeout is None else timeout)
        while True:
            # Drop acks and noise before '$'
            while self.buffer and self.buffer[:1] != b"$":
                c = self.buffer[:1]
                if c in (b"+", b"-"):
                    self.acks.append(c.decode())
                self.buffer = self.buffer[1:]
            hash_at = self.buffer.find(b"#")
            if self.buffer[:1] == b"$" and hash_at >= 0 and len(self.buffer) >= hash_at + 3:
                payload = self.buffer[1:hash_at]
                received = self.buffer[hash_at + 1:hash_at + 3]
                self.buffer = self.buffer[hash_at + 3:]
                if f"{checksum(payload):02x}".encode() != received.lower():
                    raise RSPError(f"bad checksum from the server: {payload!r}#{received!r}")
                if not self.no_ack:
                    self.sock.sendall(b"+")
                data = expand_rle(payload).decode("latin-1")
                self.log(f"<<< {data!r}")
                if not keep_console and data.startswith("O") and data != "OK" and _is_hex(data[1:]):
                    self.console.append(bytes.fromhex(data[1:]).decode("latin-1"))
                    continue
                return data
            if not self._fill(deadline):
                return None

    def read_ack(self, timeout: float = 1.0) -> Optional[str]:
        deadline = time.time() + timeout
        while True:
            if self.buffer[:1] in (b"+", b"-"):
                c = self.buffer[:1].decode()
                self.buffer = self.buffer[1:]
                return c
            if self.buffer:
                return None
            if not self._fill(deadline):
                return None

    def request(self, data, timeout: Optional[float] = None, expect_ack: bool = True) -> Optional[str]:
        """Sends a packet, checks the server's '+' (ack mode), returns the reply data or None (no reply)"""
        self.send(data)
        if not self.no_ack and expect_ack:
            ack = self.read_ack(timeout if timeout is not None else self.timeout)
            if ack != "+":
                raise RSPError(f"no '+' for {data!r} (got {ack!r})")
        return self.read_packet(timeout)

    def drain(self, quiet: float = 0.2) -> List[str]:
        """Reads whatever arrives until the line is quiet for `quiet` seconds"""
        packets = []
        while True:
            p = self.read_packet(quiet)
            if p is None:
                return packets
            packets.append(p)

    def interrupt(self):
        self.send_raw(b"\x03")

    # --- conveniences ---

    def read_registers(self) -> List[int]:
        g = self.request("g")
        if g is None or g.startswith("E") and len(g) == 3:
            raise RSPError(f"g -> {g!r}")
        values, at = [], 0
        for size in REG_SIZES[:G_REGISTERS]:
            values.append(parse_le_hex(g[at:at + size * 2]))
            at += size * 2
        return values

    def reg(self, n: int) -> int:
        r = self.request(f"p{n:x}")
        if r is None or (r.startswith("E") and len(r) == 3):
            raise RSPError(f"p{n:x} -> {r!r}")
        return parse_le_hex(r)

    def set_reg(self, n: int, value: int) -> Optional[str]:
        return self.request(f"P{n:x}={le_hex(value, REG_SIZES[n])}")

    def read_mem(self, addr: int, length: int) -> bytes:
        r = self.request(f"m{addr:x},{length:x}")
        if r is None or (r.startswith("E") and len(r) == 3):
            raise RSPError(f"m{addr:x},{length:x} -> {r!r}")
        return bytes.fromhex(r)

    def write_mem(self, addr: int, data: bytes) -> Optional[str]:
        return self.request(f"M{addr:x},{len(data):x}:{data.hex()}")

    def write_mem_binary(self, addr: int, data: bytes) -> Optional[str]:
        return self.request(f"X{addr:x},{len(data):x}:".encode() + escape_binary(data))

    def monitor(self, command: str, timeout: Optional[float] = None) -> Tuple[Optional[str], str]:
        """qRcmd: (final reply, text = the hex-decoded reply plus any O output)"""
        self.console.clear()
        r = self.request("qRcmd," + command.encode().hex(), timeout)
        text = "".join(self.console)
        if r and r != "OK" and _is_hex(r):
            text += bytes.fromhex(r).decode("latin-1")
        return r, text


def _is_hex(text: str) -> bool:
    return len(text) % 2 == 0 and all(c in "0123456789abcdefABCDEF" for c in text)


class AgentExpr:
    """gdb agent-expression bytecode (gdbsupport/ax.def); jumps take labels resolved at build()"""

    OPS = {
        "add": 0x02, "sub": 0x03, "mul": 0x04, "div_signed": 0x05, "div_unsigned": 0x06, "rem_signed": 0x07,
        "rem_unsigned": 0x08, "lsh": 0x09, "rsh_signed": 0x0A, "rsh_unsigned": 0x0B, "trace": 0x0C,
        "trace_quick": 0x0D, "log_not": 0x0E, "bit_and": 0x0F, "bit_or": 0x10, "bit_xor": 0x11, "bit_not": 0x12,
        "equal": 0x13, "less_signed": 0x14, "less_unsigned": 0x15, "ref8": 0x17, "ref16": 0x18, "ref32": 0x19,
        "end": 0x27, "dup": 0x28, "pop": 0x29, "swap": 0x2B, "rot": 0x33, "getv": 0x2C, "printf": 0x34,
    }

    def __init__(self):
        self.code = bytearray()
        self.fixups: List[Tuple[int, str]] = []
        self.labels: Dict[str, int] = {}

    def op(self, name: str) -> "AgentExpr":
        self.code.append(self.OPS[name])
        return self

    def const(self, value: int) -> "AgentExpr":
        for opcode, size in ((0x22, 1), (0x23, 2), (0x24, 4), (0x25, 8)):
            if 0 <= value < (1 << (8 * size)):
                self.code.append(opcode)
                self.code += value.to_bytes(size, "big")
                return self
        raise ValueError(value)

    def reg(self, n: int) -> "AgentExpr":
        self.code.append(0x26)
        self.code += n.to_bytes(2, "big")
        return self

    def ext(self, bits: int) -> "AgentExpr":
        self.code += bytes([0x16, bits])
        return self

    def zero_ext(self, bits: int) -> "AgentExpr":
        self.code += bytes([0x2A, bits])
        return self

    def pick(self, n: int) -> "AgentExpr":
        self.code += bytes([0x32, n])
        return self

    def raw(self, data: bytes) -> "AgentExpr":
        self.code += data
        return self

    def _jump(self, opcode: int, label) -> "AgentExpr":
        self.code.append(opcode)
        if isinstance(label, int):
            self.code += label.to_bytes(2, "big")
        else:
            self.fixups.append((len(self.code), label))
            self.code += b"\x00\x00"
        return self

    def if_goto(self, label) -> "AgentExpr":
        return self._jump(0x20, label)

    def goto(self, label) -> "AgentExpr":
        return self._jump(0x21, label)

    def label(self, name: str) -> "AgentExpr":
        self.labels[name] = len(self.code)
        return self

    def build(self) -> bytes:
        for at, name in self.fixups:
            self.code[at:at + 2] = self.labels[name].to_bytes(2, "big")
        return bytes(self.code)

    @staticmethod
    def condition_list(*expressions: bytes) -> str:
        """The text after the first ';' of a Z packet"""
        return ";".join(f"X{len(e):x},{e.hex()}" for e in expressions)


def reg_equals(regnum: int, value: int) -> bytes:
    return AgentExpr().reg(regnum).const(value).op("equal").op("end").build()


def reg8_equals(regnum: int, high: bool, value: int) -> bytes:
    """A / H / ... of a pair: (pair >> 8) & 0xFF == value, as gdb compiles $a == value"""
    e = AgentExpr().reg(regnum)
    if high:
        e.const(8).op("rsh_unsigned")
    return e.zero_ext(8).const(value).op("equal").op("end").build()


# --- the host ---

def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "core").is_dir():
            return current
        current = current.parent
    raise RuntimeError("project root not found")


def port_is_open(port: int, host: str = "127.0.0.1") -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.5)
        return probe.connect_ex((host, port)) == 0


def free_port_base(start: int = 21000, stop: int = 29000, step: int = 10) -> int:
    """A base whose six ports (WebAPI +0, CLI +1, MCP +2, GDB +3, DeZog +4, ZRCP +5) nobody listens on"""
    for base in range(start, stop, step):
        if not any(port_is_open(base + i) for i in range(6)):
            ok = True
            for i in range(6):
                with socket.socket() as s:
                    try:
                        s.bind(("127.0.0.1", base + i))
                    except OSError:
                        ok = False
                        break
            if ok:
                return base
    raise RuntimeError("no free port range")


class WebAPI:
    def __init__(self, port: int, host: str = "127.0.0.1"):
        self.base = f"http://{host}:{port}"
        self.emulator_id: Optional[str] = None

    def call(self, method: str, path: str, body=None, timeout: float = 10.0, headers: Optional[dict] = None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path.replace("{id}", self.emulator_id or ""), data=data,
                                         method=method, headers={"Content-Type": "application/json",
                                                                 **(headers or {})})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                raw = response.read().decode()
                return response.status, (json.loads(raw) if raw.strip().startswith(("{", "[")) else raw)
        except urllib.error.HTTPError as error:
            raw = error.read().decode()
            try:
                return error.code, json.loads(raw)
            except ValueError:
                return error.code, raw

    def alive(self) -> bool:
        try:
            status, _ = self.call("GET", "/api/v1/emulator", timeout=2)
            return status == 200
        except (OSError, urllib.error.URLError):
            return False

    def first_emulator(self) -> Optional[str]:
        status, body = self.call("GET", "/api/v1/emulator")
        items = body.get("emulators", body) if isinstance(body, dict) else body
        if isinstance(items, list) and items:
            first = items[0]
            return first.get("id") if isinstance(first, dict) else str(first)
        return None

    def start(self, model: str) -> str:
        status, body = self.call("POST", "/api/v1/emulator/start", {"model": model}, timeout=30)
        if status >= 300 or not isinstance(body, dict) or "id" not in body:
            raise RuntimeError(f"emulator start failed: {status} {body}")
        self.emulator_id = body["id"]
        return self.emulator_id

    def release(self):
        """Ends the implicit run-control session a changing call opens for this client: gdb's resume would be
        refused while it lasts"""
        self.call("DELETE", "/api/v1/emulator/{id}/control/session")

    def changing(self, method: str, path: str, body=None) -> int:
        status = self.call(method, path, body)[0]
        self.release()
        return status

    def mute(self) -> bool:
        return self.changing("PUT", "/api/v1/emulator/{id}/audio/mixer/master", {"muted": True}) < 300

    def pause(self) -> int:
        return self.changing("POST", "/api/v1/emulator/{id}/pause")

    def resume(self) -> int:
        return self.changing("POST", "/api/v1/emulator/{id}/resume")

    def info(self) -> dict:
        return self.call("GET", "/api/v1/emulator/{id}")[1]

    def breakpoints(self) -> List[dict]:
        status, body = self.call("GET", "/api/v1/emulator/{id}/breakpoints")
        if isinstance(body, dict):
            return body.get("breakpoints", [])
        return body if isinstance(body, list) else []

    def add_breakpoint(self, body: dict):
        return self.call("POST", "/api/v1/emulator/{id}/breakpoints", body)

    def remove_breakpoint(self, bp_id: int):
        return self.call("DELETE", f"/api/v1/emulator/{{id}}/breakpoints/{bp_id}")


class Host:
    """unreal-qt offscreen on ports of its own with one muted, paused emulator instance"""

    def __init__(self, binary: str, model: str = "PENTAGON", port_base: Optional[int] = None,
                 log_dir: Optional[Path] = None):
        self.binary = binary
        self.model = model
        self.port_base = port_base or free_port_base()
        self.webapi_port = self.port_base
        self.gdb_port = self.port_base + 3
        self.process: Optional[subprocess.Popen] = None
        log_dir = log_dir or (find_project_root() / "scratch" / "verification-gdb")
        log_dir.mkdir(parents=True, exist_ok=True)
        self.log_path = log_dir / f"host-{self.port_base}.log"
        self.api = WebAPI(self.webapi_port)

    def start(self, timeout: float = 60.0) -> "Host":
        for i in range(6):
            if port_is_open(self.port_base + i):
                raise RuntimeError(f"port {self.port_base + i} is taken by another process")
        env = dict(os.environ)
        for name, offset in (("UNREAL_WEBAPI_PORT", 0), ("UNREAL_CLI_PORT", 1), ("UNREAL_MCP_PORT", 2),
                             ("UNREAL_GDB_PORT", 3), ("UNREAL_DEZOG_PORT", 4), ("UNREAL_ZRCP_PORT", 5)):
            env[name] = str(self.port_base + offset)
        env.setdefault("UNREAL_STREAMDECK_SUPERVISOR", "off")
        env.setdefault("UNREAL_QT_VIEW_SETTINGS", "off")
        env.setdefault("UNREAL_RECENT_FILES", "off")
        env["QT_QPA_PLATFORM"] = "offscreen"
        self.log = open(self.log_path, "w")
        self.process = subprocess.Popen([self.binary, "-platform", "offscreen"], env=env, stdout=self.log,
                                        stderr=subprocess.STDOUT)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"{self.binary} exited with {self.process.returncode} (log {self.log_path})")
            if self.api.alive() and port_is_open(self.gdb_port):
                break
            time.sleep(0.3)
        else:
            self.stop()
            raise RuntimeError(f"the host did not open WebAPI {self.webapi_port} / GDB {self.gdb_port}")
        self.api.start(self.model)
        self.api.mute()
        time.sleep(0.5)  # let it boot a little
        self.api.pause()
        return self

    def stop(self):
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        self.log.close()
