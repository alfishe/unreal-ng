#!/usr/bin/env python3
"""DZRP protocol client for testing."""

import socket
import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

class DZRPCommand(IntEnum):
    CMD_INIT = 1
    CMD_CLOSE = 2
    CMD_GET_REGISTERS = 3
    CMD_SET_REGISTER = 4
    CMD_WRITE_BANK = 5
    CMD_CONTINUE = 6
    CMD_PAUSE = 7
    CMD_READ_MEM = 8
    CMD_WRITE_MEM = 9
    CMD_SET_SLOT = 10
    CMD_SET_BORDER = 12
    CMD_READ_PORT = 20
    CMD_WRITE_PORT = 21
    CMD_GET_SUPPORTED_COMMANDS = 24
    CMD_ADD_BREAKPOINT = 40
    CMD_REMOVE_BREAKPOINT = 41
    CMD_ADD_WATCHPOINT = 42
    CMD_REMOVE_WATCHPOINT = 43
    CMD_READ_STATE = 50
    CMD_WRITE_STATE = 51
    # Unreal-NG extensions (TTD-backed instruction history)
    CMD_GET_HISTORY_INFO = 0xE0
    CMD_GET_HISTORY_ENTRY = 0xE1

class DZRPNotification(IntEnum):
    NTF_PAUSE = 1

@dataclass
class DZRPResponse:
    seq_no: int
    nak: bool
    payload: bytes

@dataclass
class DZRPPauseNotification:
    reason: int
    address: int
    bank: int
    message: str

class DZRPClient:
    # Frames carry at least one seq byte; the server rejects >1MB command
    # payloads, so anything beyond this generous cap is wire corruption.
    MAX_FRAME_LEN = 16 * 1024 * 1024

    def __init__(self, host: str = "localhost", port: int = 12000):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.seq_no = 0
        # Notifications (seq 0) may interleave with responses; like DeZog we
        # stash them and let wait_notification() consume them in order.
        self.pending_notifications: list = []

    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5.0)
            self.sock.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"Connect failed: {e}")
            return False

    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def _next_seq(self) -> int:
        self.seq_no = (self.seq_no % 15) + 1
        return self.seq_no

    def _send_command(self, cmd_id: int, payload: bytes = b"") -> DZRPResponse:
        seq = self._next_seq()
        # DZRP COMMAND framing exactly as DeZog's dzrpbufferremote.ts sends it:
        #   length(4) = DATA length ONLY (excludes the seqNo and command bytes)
        #   then seqNo(1) + command(1) + data
        data = struct.pack("<I", len(payload)) + bytes([seq, cmd_id]) + payload
        self.sock.sendall(data)
        resp = self._recv_response()
        # DeZog treats a mismatched seq as fatal ("Received wrong SeqNo"),
        # so every exchange must echo the sequence number exactly.
        if resp.seq_no != seq:
            raise RuntimeError(f"Seq mismatch: sent {seq}, got {resp.seq_no}")
        return resp

    def _recv_frame(self) -> bytes:
        length_data = self._recv_exact(4)
        length = struct.unpack("<I", length_data)[0]
        if length == 0 or length > self.MAX_FRAME_LEN:
            raise RuntimeError(f"Invalid frame length {length} (wire corruption?)")
        return self._recv_exact(length)

    def _recv_response(self) -> DZRPResponse:
        # Skip (and stash) any notification frames that arrive before the response
        for _ in range(8):
            data = self._recv_frame()
            if data and data[0] == 0:
                self.pending_notifications.append(data)
                continue
            seq_nak = data[0]
            nak = bool(seq_nak & 0x80)
            seq_no = seq_nak & 0x0F
            return DZRPResponse(seq_no, nak, data[1:] if len(data) > 1 else b"")
        raise RuntimeError("Too many interleaved notifications while waiting for response")

    def _recv_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        return data

    # High-level commands
    def cmd_init(self, version: tuple = (2, 2, 0), name: str = "TestClient") -> dict:
        payload = bytes(version) + name.encode() + b"\x00"
        resp = self._send_command(DZRPCommand.CMD_INIT, payload)
        if resp.nak:
            return {"error": "NAK"}
        if len(resp.payload) < 5:
            return {"error": "Invalid response"}
        return {
            "error": resp.payload[0],
            "version": tuple(resp.payload[1:4]),
            "machine": resp.payload[4],
            "name": resp.payload[5:].rstrip(b"\x00").decode() if len(resp.payload) > 5 else "",
        }

    def cmd_close(self) -> bool:
        resp = self._send_command(DZRPCommand.CMD_CLOSE)
        return not resp.nak

    def cmd_get_registers(self) -> dict:
        resp = self._send_command(DZRPCommand.CMD_GET_REGISTERS)
        if resp.nak or len(resp.payload) < 29:
            return {"error": "Invalid response"}
        p = resp.payload
        nslots = p[28]
        return {
            "pc": struct.unpack("<H", p[0:2])[0],
            "sp": struct.unpack("<H", p[2:4])[0],
            "af": struct.unpack("<H", p[4:6])[0],
            "bc": struct.unpack("<H", p[6:8])[0],
            "de": struct.unpack("<H", p[8:10])[0],
            "hl": struct.unpack("<H", p[10:12])[0],
            "ix": struct.unpack("<H", p[12:14])[0],
            "iy": struct.unpack("<H", p[14:16])[0],
            "af2": struct.unpack("<H", p[16:18])[0],
            "bc2": struct.unpack("<H", p[18:20])[0],
            "de2": struct.unpack("<H", p[20:22])[0],
            "hl2": struct.unpack("<H", p[22:24])[0],
            "r": p[24],
            "i": p[25],
            "im": p[26],
            "slots": list(p[29:29+nslots]) if nslots > 0 else [],
        }

    def cmd_set_register(self, reg_id: int, value: int) -> bool:
        payload = bytes([reg_id]) + struct.pack("<H", value)
        resp = self._send_command(DZRPCommand.CMD_SET_REGISTER, payload)
        return not resp.nak

    def cmd_read_mem(self, addr: int, size: int) -> bytes:
        payload = bytes([0]) + struct.pack("<HH", addr, size)
        resp = self._send_command(DZRPCommand.CMD_READ_MEM, payload)
        if resp.nak:
            return b""
        return resp.payload

    def cmd_write_mem(self, addr: int, data: bytes) -> bool:
        payload = bytes([0]) + struct.pack("<H", addr) + data
        resp = self._send_command(DZRPCommand.CMD_WRITE_MEM, payload)
        return not resp.nak

    def cmd_continue(self) -> bool:
        # No breakpoints, no alternate command
        payload = bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        resp = self._send_command(DZRPCommand.CMD_CONTINUE, payload)
        return not resp.nak

    def cmd_continue_with_temp_bps(self, bp1_addr: int, bp2_addr=None) -> bool:
        # Matches DeZog's sendDzrpCmdContinue byte layout:
        # [en1][addr1 lo][addr1 hi][en2][addr2 lo][addr2 hi][alternate][unused x4]
        payload = bytes([1]) + struct.pack("<H", bp1_addr)
        if bp2_addr is not None:
            payload += bytes([1]) + struct.pack("<H", bp2_addr)
        else:
            payload += bytes([0, 0, 0])  # bp2 disabled
        payload += bytes([0, 0, 0, 0, 0])  # alternate=CONTINUE(0), unused
        resp = self._send_command(DZRPCommand.CMD_CONTINUE, payload)
        return not resp.nak

    def _send_raw_command(self, cmd_id: int, payload: bytes) -> DZRPResponse:
        """Send raw command (for testing unknown commands)."""
        return self._send_command(cmd_id, payload)

    def cmd_pause(self) -> bool:
        resp = self._send_command(DZRPCommand.CMD_PAUSE)
        return not resp.nak

    def cmd_add_breakpoint(self, addr: int, bank: int = 0, condition: str = "") -> int:
        payload = struct.pack("<H", addr) + bytes([bank]) + condition.encode() + b"\x00"
        resp = self._send_command(DZRPCommand.CMD_ADD_BREAKPOINT, payload)
        if resp.nak or len(resp.payload) < 2:
            return 0
        return struct.unpack("<H", resp.payload[0:2])[0]

    def cmd_remove_breakpoint(self, bp_id: int) -> bool:
        payload = struct.pack("<H", bp_id)
        resp = self._send_command(DZRPCommand.CMD_REMOVE_BREAKPOINT, payload)
        return not resp.nak

    def cmd_add_watchpoint(self, addr: int, size: int, bank: int = 0, access: int = 3) -> bool:
        payload = struct.pack("<H", addr) + bytes([bank]) + struct.pack("<H", size) + bytes([access])
        resp = self._send_command(DZRPCommand.CMD_ADD_WATCHPOINT, payload)
        if resp.nak or len(resp.payload) < 1:
            return False
        return resp.payload[0] == 0

    def cmd_remove_watchpoint(self, addr: int, size: int, bank: int = 0, access: int = 3) -> bool:
        payload = struct.pack("<H", addr) + bytes([bank]) + struct.pack("<H", size) + bytes([access])
        resp = self._send_command(DZRPCommand.CMD_REMOVE_WATCHPOINT, payload)
        return not resp.nak

    def cmd_set_slot(self, slot: int, bank: int) -> bool:
        payload = bytes([slot, bank])
        resp = self._send_command(DZRPCommand.CMD_SET_SLOT, payload)
        return not resp.nak and len(resp.payload) >= 1 and resp.payload[0] == 0

    def cmd_write_bank(self, bank: int, data: bytes) -> bool:
        payload = bytes([bank]) + data
        resp = self._send_command(DZRPCommand.CMD_WRITE_BANK, payload)
        return not resp.nak and len(resp.payload) >= 1 and resp.payload[0] == 0

    def cmd_set_border(self, color: int) -> bool:
        resp = self._send_command(DZRPCommand.CMD_SET_BORDER, bytes([color]))
        return not resp.nak

    def cmd_read_port(self, port: int) -> Optional[int]:
        """CMD_READ_PORT per DeZog cspectremote.ts: port(2) -> exactly one data
        byte. Returns None when the server violates the contract (e.g. an empty
        ACK from an unimplemented handler) - DeZog would read `undefined` there."""
        resp = self._send_command(DZRPCommand.CMD_READ_PORT, struct.pack("<H", port))
        if resp.nak or len(resp.payload) != 1:
            return None
        return resp.payload[0]

    def cmd_write_port(self, port: int, value: int) -> bool:
        """CMD_WRITE_PORT per DeZog cspectremote.ts: port(2) + value(1) -> empty ACK."""
        resp = self._send_command(DZRPCommand.CMD_WRITE_PORT,
                                  struct.pack("<H", port) + bytes([value & 0xFF]))
        return not resp.nak and len(resp.payload) == 0

    def cmd_get_supported_commands(self) -> set:
        resp = self._send_command(DZRPCommand.CMD_GET_SUPPORTED_COMMANDS)
        if resp.nak or len(resp.payload) < 32:
            return set()
        supported = set()
        for byte_idx in range(32):
            byte_val = resp.payload[byte_idx]
            for bit in range(8):
                if byte_val & (1 << bit):
                    supported.add(byte_idx * 8 + bit)
        return supported

    def cmd_read_state(self) -> bytes:
        resp = self._send_command(DZRPCommand.CMD_READ_STATE)
        if resp.nak:
            return b""
        return resp.payload

    def cmd_write_state(self, state: bytes) -> bool:
        resp = self._send_command(DZRPCommand.CMD_WRITE_STATE, state)
        return not resp.nak

    def cmd_get_history_info(self) -> dict:
        resp = self._send_command(DZRPCommand.CMD_GET_HISTORY_INFO)
        if resp.nak or len(resp.payload) < 2:
            return {"available": False, "recording": False}
        return {"available": resp.payload[0] == 1, "recording": resp.payload[1] == 1}

    def cmd_get_history_entry(self, index: int) -> Optional[dict]:
        """Returns None when out of range / unavailable, else a register dict plus
        'opcodes' (4 bytes at PC) and 'sp_content' (word at SP)."""
        resp = self._send_command(DZRPCommand.CMD_GET_HISTORY_ENTRY, struct.pack("<I", index))
        if resp.nak or len(resp.payload) < 1 or resp.payload[0] != 0:
            return None
        p = resp.payload[1:]
        if len(p) < 29:
            return None
        nslots = p[28]
        end = 29 + nslots
        if len(p) < end + 6:
            return None
        return {
            "pc": struct.unpack("<H", p[0:2])[0],
            "sp": struct.unpack("<H", p[2:4])[0],
            "af": struct.unpack("<H", p[4:6])[0],
            "hl": struct.unpack("<H", p[10:12])[0],
            "slots": list(p[29:end]),
            "opcodes": bytes(p[end:end + 4]),
            "sp_content": struct.unpack("<H", p[end + 4:end + 6])[0],
        }

    def has_pending_notification(self) -> bool:
        return len(self.pending_notifications) > 0

    def wait_notification(self, timeout: float = 10.0) -> Optional[DZRPPauseNotification]:
        self.sock.settimeout(timeout)
        try:
            if self.pending_notifications:
                data = self.pending_notifications.pop(0)
            else:
                data = self._recv_frame()
            seq = data[0]
            if seq != 0:  # Not a notification
                return None
            notif_id = data[1]
            if notif_id != DZRPNotification.NTF_PAUSE:
                return None
            reason = data[2]
            addr = struct.unpack("<H", data[3:5])[0]
            bank = data[5]
            msg_end = data.find(0, 6)
            message = data[6:msg_end].decode() if msg_end > 6 else ""
            return DZRPPauseNotification(reason, addr, bank, message)
        except socket.timeout:
            return None


if __name__ == "__main__":
    # Quick test
    client = DZRPClient()
    if client.connect():
        print("Connected!")
        init = client.cmd_init()
        print(f"Init: {init}")
        regs = client.cmd_get_registers()
        print(f"Registers: {regs}")
        client.cmd_close()
        client.disconnect()
    else:
        print("Failed to connect")
