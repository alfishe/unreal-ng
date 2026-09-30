"""NedoOS kernel knowledge: symbols, page roles and the structures the layer reads.

Everything here follows docs/inprogress/2026-09-30-nedoos-integration/nedoos-kernel-reference.md.
Addresses come from the build's label file (sjasmplus user.l); only the layout
constants that the label file does not carry are written down here.
"""

import os
import struct

PAGE_XOR = 0xFF          # atm=1: physical page = port value ^ 0xFF
APP_OFFSET = 0x12        # app structure after the 18-byte register frame (safestack_sz)
APP_SIZE = 0x56          # sizeof(app) with bdosstack_sz = 0
FRAME_SIZE = 16          # af', ix, hl', de', bc', iy, hl, sp
MAIN_PAGE_WINDOWS = (0x44, 0x4A, 0x50)   # curpg16k, curpg32klow, curpg32khigh operands
COMMAND_LINE = 0x0080
FIL_OWNER = 0x05         # FIL.PAD1 = owner task id
FATFS_WIN = 51           # offset of FATFS.win
SOCKET_ENTRY = 5         # w53_socflags entry size

FLAG_BITS = ((0, "active"), (1, "child-finished"), (5, "gfx"), (7, "waiting"))

# Which page each anchor lives in. user.l is flat: the same number means
# different things in different pages (reference section 2).
ANCHOR_ROLES = {
    "callbdos": "pgsys", "callbdos_sp": "pgsys", "appaddr": "pgsys", "focusappaddr": "pgsys",
    "sys_timer": "pgsys", "safestack": "pgsys", "tsys_pages": "pgsys", "tbdoscmds": "pgsys",
    "freepipes": "pgsys", "pipeowners": "pgsys", "pipetypes": "pgsys", "pipebufs": "pgsys",
    "w53_socflags": "pgtrdosfs", "wizlocalport": "pgtrdosfs", "wiznet_fail_1": "pgtrdosfs",
    "ffilearray": "pgfatfs2",
}

# Operand of the patched "call nn" in callbdos (handler of the current call):
# not a label in the source, fixed relative to callbdos for this kernel.
CALLBDOS_HANDLER_OPERAND = 0x18


class Symbols:
    def __init__(self, path):
        self.values = {}
        with open(path, encoding="ascii", errors="replace") as source:
            for line in source:
                line = line.strip()
                if not line.startswith(":"):
                    continue
                address, _, name = line[1:].partition(" ")
                self.values[name] = int(address, 16)
        self.commands = {}
        for name, value in self.values.items():
            if name.startswith("CMD_"):
                self.commands.setdefault(value, name[4:])

    def __getitem__(self, name):
        return self.values[name]

    def get(self, name, default=None):
        return self.values.get(name, default)


def default_symbols():
    here = os.path.dirname(os.path.abspath(__file__))
    return Symbols(os.path.join(here, "..", "symbols", "evo-inetdrv1-44049473", "user.l"))


class PageMap:
    """Physical pages of the fixed system roles (TOPDOWNMEM = 0)."""

    def __init__(self, symbols):
        self.roles = {
            "pgtrdosfs": 8, "pgfatfs": 9, "pgsys": 10, "pgfatfs2": 11, "pgkillable": 4,
        }
        self.window_base = {"pgsys": 0x0000, "pgtrdosfs": 0x4000, "pgfatfs2": 0xC000}

    def locate(self, role, address):
        """(physical page, offset) of a label that lives in the given role."""
        return self.roles[role], address - self.window_base[role]


def physical(port_value):
    return port_value ^ PAGE_XOR


def word(data, offset):
    return data[offset] | data[offset + 1] << 8


def flag_names(flags):
    names = [name for bit, name in FLAG_BITS if flags >> bit & 1]
    return names or ["inactive"]


def decode_frame(frame):
    names = ("af_", "ix", "hl_", "de_", "bc_", "iy", "hl", "sp")
    return {name: struct.unpack_from("<H", frame, 2 * i)[0] for i, name in enumerate(names)}
