#!/usr/bin/env python3
"""ZRCP (ZEsarUX Remote Control Protocol) client for testing.

Talks the line-based text protocol exactly like DeZog's ZesaruxSocket
(src/remotes/zesarux/zesaruxsocket.ts):

  - commands are '\\n'-terminated lines
  - EVERY response ends with the prompt "command...> " (no trailing newline);
    DeZog detects it via startsWith('command') && endsWith('> ')
  - a bare "\\n" interrupts a running "run" (DeZog's pause())
  - response lines starting with "error" (first 5 chars, case-insensitive)
    only warn client-side; "log> " lines are diagnostics forwarded to output

On connect the server sends a welcome banner followed by the first prompt.
"""

import socket
from typing import List, Optional, Tuple

PROMPT = "command...> "
RUN_BANNER = "Running until a breakpoint, key press or data sent, menu opening or other event"

# Field order is load-bearing: DeZog memoizes indexOf offsets per line shape,
# so a parser that reads them in this order fails loudly on a reshuffled line.
# (F=/F' are parsed separately: their values are flag strings, not hex words.)
_REGISTER_FIELDS = [
    "PC=", "SP=", "AF=", "BC=", "HL=", "DE=", "IX=", "IY=",
    "AF'=", "BC'=", "HL'=", "DE'=", "I=", "R=",
]


def is_error(line: str) -> bool:
    """DeZog error-line check: first 5 chars == 'error', case-insensitive."""
    return line[:5].lower() == "error"


def _hex_word_at(line: str, field: str, offset: int) -> Tuple[str, int]:
    """Finds `field` at/after `offset` and scans the hex value that follows.
    Returns (text, end offset) so fields can be parsed strictly in order."""
    pos = line.find(field, offset)
    if pos < 0:
        raise ValueError(f"register field {field!r} missing (after offset {offset})")
    start = pos + len(field)
    end = start
    while end < len(line) and line[end] in "0123456789abcdefABCDEF":
        end += 1
    if end == start:
        raise ValueError(f"no hex value after {field!r}")
    return line[start:end], end


def parse_registers(line: str) -> dict:
    """Parses the print_registers line into a dict.

    Values are ints; 'flags'/'flags2' keep the 8-char string, 'mmu' the list
    of 8 lowercase hex words (contiguous, first 4 = the 16 KB slots).
    Raises ValueError on a malformed line so protocol drift never passes
    silently.
    """
    regs: dict = {}
    offset = 0
    for field in _REGISTER_FIELDS:
        text, offset = _hex_word_at(line, field, offset)
        regs[field.rstrip("=").replace("'", "2")] = int(text, 16)
    # TWO spaces after R= precede F=; the leading space anchors the search
    # past the register words (AF= etc. must not match)
    regs["flags"] = _extract_after(line, " F=", 8)
    regs["flags2"] = _extract_after(line, " F'=", 8)
    regs["memptr"] = int(_extract_after(line, "MEMPTR=", 4), 16)
    regs["im"] = int(_extract_after(line, " IM", 1))
    regs["iff"] = _extract_after(line, " IFF", 2)  # "--" (both modeled off)
    mmu = _extract_after(line, "MMU=", 32)
    if not all(c in "0123456789abcdef" for c in mmu):
        raise ValueError(f"MMU not 32 contiguous lowercase hex chars: {mmu!r}")
    regs["mmu"] = [int(mmu[i:i + 4], 16) for i in range(0, 32, 4)]
    return regs


def _extract_after(line: str, marker: str, count: int) -> str:
    """Fixed-width field after a marker; raises when truncated (wire drift)."""
    pos = line.find(marker)
    if pos < 0:
        raise ValueError(f"marker {marker!r} missing")
    pos += len(marker)
    out = line[pos:pos + count]
    if len(out) != count:
        raise ValueError(f"field after {marker!r} truncated: {out!r}")
    return out


def parse_history_entry(line: str) -> dict:
    """Parses a cpu-history get line: register prefix + (PC)/(SP) + MMU.

    History lines have no F=/MEMPTR/VPS; (PC) is 4 opcode bytes at the entry
    PC in memory order, (SP) the word at SP (hi byte first), both lowercase
    hex, and the MMU groups are followed by a single trailing space.
    """
    entry = parse_registers_until(line, " (PC)=")
    pos = line.find("(PC)=")
    if pos < 0:
        raise ValueError("(PC)= missing")
    entry["pc_opcodes"] = line[pos + 5:pos + 13]
    pos = line.find("(SP)=")
    if pos < 0:
        raise ValueError("(SP)= missing")
    entry["sp_content"] = line[pos + 5:pos + 9]
    trimmed = line.rstrip()
    pos = trimmed.find("MMU=")
    if pos < 0:
        raise ValueError("MMU= missing")
    mmu = trimmed[pos + 4:]
    if len(mmu) != 32:
        raise ValueError(f"history MMU not 32 chars: {mmu!r}")
    entry["mmu"] = [int(mmu[i:i + 4], 16) for i in range(0, 32, 4)]
    return entry


def parse_registers_until(line: str, sentinel: str) -> dict:
    """Numeric register fields (in order) over the prefix before `sentinel`."""
    cut = line.find(sentinel)
    if cut < 0:
        raise ValueError(f"sentinel {sentinel!r} missing")
    prefix = line[:cut]
    regs: dict = {}
    offset = 0
    for field in _REGISTER_FIELDS:
        text, offset = _hex_word_at(prefix, field, offset)
        regs[field.rstrip("=").replace("'", "2")] = int(text, 16)
    return regs


def parse_disasm_line(line: str) -> Tuple[int, int, str]:
    """Splits a disassembly line "%04X %X <mnemonic>" (7-char prefix).

    DeZog slices substring(7, 7+4) to detect CALL/RST opcodes, so the prefix
    width is part of the wire contract.
    """
    if len(line) < 7 or line[4] != " " or line[6] != " ":
        raise ValueError(f"malformed disassembly prefix: {line!r}")
    return int(line[0:4], 16), int(line[5], 16), line[7:]


class ZRCPClient:
    def __init__(self, host: str = "localhost", port: int = 10000, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self.banner: List[str] = []
        self.logs: List[str] = []
        self._buffer = b""

    def connect(self, timeout: float = 10.0) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect((self.host, self.port))
            # Welcome banner + first prompt
            text = self.read_until_prompt()
            self.banner = text.split("\n") if text else []
            return any("welcome" in line.lower() for line in self.banner)
        except Exception as e:  # noqa: BLE001
            print(f"Connect failed: {e}")
            self.sock = None
            return False

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def socket_closed(self, timeout: float = 3.0) -> bool:
        """True once the server closed the connection (EOF/reset)."""
        if not self.sock:
            return True
        self.sock.settimeout(timeout)
        try:
            return self.sock.recv(1) == b""
        except socket.timeout:
            return False
        except OSError:
            return True

    # --- low level ---

    def _fill(self, timeout: float) -> bool:
        self.sock.settimeout(timeout)
        try:
            chunk = self.sock.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError("server closed the connection")
        self._buffer += chunk
        return True

    def send_line(self, line: str):
        self.sock.sendall(line.encode() + b"\n")

    def send_blank(self):
        """DeZog's pause(): a bare newline interrupts a running 'run'."""
        self.send_line("")

    def read_line(self, timeout: Optional[float] = None) -> str:
        timeout = self.timeout if timeout is None else timeout
        while b"\n" not in self._buffer:
            if not self._fill(timeout):
                raise TimeoutError("no line within timeout")
        line, _, self._buffer = self._buffer.partition(b"\n")
        return line.decode(errors="replace")

    def _strip_log_lines(self):
        """DeZog's log handling (zesaruxsocket.ts): a 'log> ' line at a line
        start is removed from the chunk and forwarded to the UI - BEFORE the
        prompt check. A log-only answer therefore completes with an empty
        command result, never with the log text."""
        prefix = b"log> "
        search_from = 0
        while True:
            start = self._buffer.find(prefix, search_from)
            if start < 0:
                return
            if start > 0 and self._buffer[start - 1:start] != b"\n":
                search_from = start + len(prefix)
                continue
            end = self._buffer.find(b"\n", start)
            if end < 0:
                return  # incomplete log line: wait for more data
            self.logs.append(self._buffer[start + len(prefix):end].decode(errors="replace"))
            self._buffer = self._buffer[:start] + self._buffer[end + 1:]
            search_from = start

    def read_until_prompt(self, timeout: Optional[float] = None) -> str:
        """Accumulates until the stream ends with '\n' + prompt - DeZog's exact
        acceptance rule (zesaruxsocket.ts splits the chunk on '\n' and requires
        the prompt to be the last split line). 'log> ' lines are stripped from
        the chunk BEFORE that check, exactly like DeZog (collected in logs).
        Returns the response text before that newline (final newline stripped,
        like DeZog's receivedMsg handling of the pre-prompt lines)."""
        timeout = self.timeout if timeout is None else timeout
        prompt_end = b"\n" + PROMPT.encode()
        while True:
            self._strip_log_lines()
            if self._buffer.endswith(prompt_end):
                break
            if not self._fill(timeout):
                raise TimeoutError("no prompt within timeout")
        response = self._buffer[:-len(prompt_end)]
        self._buffer = b""
        if response.endswith(b"\n"):
            response = response[:-1]
        return response.decode(errors="replace")

    # --- high level ---

    def command(self, line: str, timeout: Optional[float] = None) -> str:
        """Sends one command, returns everything before the prompt."""
        self.send_line(line)
        return self.read_until_prompt(timeout)

    def run(self, timeout: float = 15.0) -> Tuple[str, str]:
        """Sends 'run': the banner line arrives immediately, the stop output
        only when the emulator pauses. Returns (banner, stop_output)."""
        self.send_line("run")
        banner = self.read_line(timeout)
        return banner, self.read_until_prompt(timeout)


if __name__ == "__main__":
    # Quick smoke test against a running server (default port 10000)
    client = ZRCPClient()
    if client.connect():
        print("Connected, banner:")
        for line in client.banner:
            print(f"  {line}")
        print(f"Version: {client.command('get-version')}")
        client.command("quit")
        client.disconnect()
    else:
        print("Failed to connect")
