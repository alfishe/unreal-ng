"""Read-only views of a running NedoOS: tasks, pages, pipes, sockets, files, kernel state.

Requirement ids (NK-n) refer to
docs/inprogress/2026-09-30-nedoos-integration/requirements-nedoos-layer.md.
"""

import struct
from dataclasses import dataclass, field

from . import kernel as k


@dataclass
class Task:
    slot: int
    app: int                      # address of the app structure in pgsys
    flags: int
    id: int
    parent: int
    main_page: int                # port value
    stdin: int
    stdout: int
    stderr: int
    last_time: int
    screen: int
    gfx_mode: int
    gfx_keep: int
    screen_pages: list
    child_result: int
    volume: int
    dir_cluster: int
    windows: list                 # port values at #0000/#4000/#8000/#C000
    command_line: str
    frame: dict = field(default_factory=dict)

    @property
    def name(self):
        return self.command_line.split(" ")[0] if self.command_line else "?"

    @property
    def states(self):
        return k.flag_names(self.flags)


class NedoOs:
    def __init__(self, api, symbols=None):
        self.api = api
        self.sym = symbols or k.default_symbols()
        self.pages = k.PageMap(self.sym)

    # --- raw access ------------------------------------------------------
    def page_of(self, role):
        return self.api.ram_page(self.pages.roles[role])

    def sys_byte(self, name, offset=0):
        page, at = self.pages.locate(k.ANCHOR_ROLES[name], self.sym[name])
        return self.api.ram_page(page)[at + offset]

    def sys_word(self, name, offset=0):
        page, at = self.pages.locate(k.ANCHOR_ROLES[name], self.sym[name])
        return k.word(self.api.ram_page(page), at + offset)

    # --- NK-1 ------------------------------------------------------------
    def detect(self):
        """The kernel entry points are where this build puts them."""
        sys = self.page_of("pgsys")
        checks = {
            "#0004 jp sys_quit": sys[4] == 0xC3 and k.word(sys, 5) == self.sym["sys_quit"],
            "#0009 jp callbdos": sys[9] == 0xC3 and k.word(sys, 10) == self.sym["callbdos"],
            "#0038 jp sys_sysint": sys[0x38] == 0xC3 and k.word(sys, 0x39) == self.sym["sys_sysint"],
        }
        return all(checks.values()), checks

    # --- NK-4 / NK-6 -----------------------------------------------------
    def tasks(self):
        sys = self.page_of("pgsys")
        slot_size = self.sym["app_sz"]
        first = self.sym["safestack"]
        result = []
        for slot in range(16):
            app = first + slot * slot_size + k.APP_OFFSET
            s = sys[app:app + k.APP_SIZE]
            if s[1] == 0:
                continue
            main = self.api.ram_page(k.physical(s[3]))
            line = main[k.COMMAND_LINE:k.COMMAND_LINE + 0x80]
            line = line[:line.index(0)] if 0 in line else line
            result.append(Task(
                slot=slot, app=app, flags=s[0], id=s[1], parent=s[2], main_page=s[3],
                stdin=s[4], stdout=s[5], stderr=s[6], last_time=s[7], screen=s[9],
                gfx_mode=s[10], gfx_keep=s[11], screen_pages=list(s[12:16]),
                child_result=k.word(s, 0x10), volume=s[0x17],
                dir_cluster=struct.unpack_from("<I", s, 0x18)[0],
                windows=[s[3]] + [main[o] for o in k.MAIN_PAGE_WINDOWS],
                command_line=line.decode("cp866", "replace"),
                frame=k.decode_frame(sys[app - k.FRAME_SIZE:app]),
            ))
        return result

    def task(self, task_id):
        for t in self.tasks():
            if t.id == task_id:
                return t
        raise KeyError(f"no task {task_id}")

    def current_app(self):
        return self.sys_word("appaddr")

    def focus_app(self):
        return self.sys_word("focusappaddr")

    def read_task(self, task, address, length):
        """Task memory through the task's own windows, never the CPU's current view."""
        out = bytearray()
        for i in range(length):
            a = (address + i) & 0xFFFF
            page = self.api.ram_page(k.physical(task.windows[a >> 14]))
            out.append(page[a & 0x3FFF])
        return bytes(out)

    def stopped_at(self, task):
        """Where a waiting task will resume: its stack holds de, bc, af, pc."""
        sp = task.frame["sp"]
        de, bc, af, pc = struct.unpack("<4H", self.read_task(task, sp, 8))
        return {"pc": pc, "sp": sp, "af": af, "bc": bc, "de": de, "hl": task.frame["hl"],
                "ix": task.frame["ix"], "iy": task.frame["iy"]}

    # --- NK-7 ------------------------------------------------------------
    def page_owners(self):
        n = self.sym["sys_npages"]
        return list(self.page_of("pgsys")[self.sym["tsys_pages"]:self.sym["tsys_pages"] + n])

    # --- NK-9 (b) --------------------------------------------------------
    def pipes(self):
        sys = self.page_of("pgsys")
        out = []
        for i in range(8):
            sides = sys[self.sym["freepipes"] + i]
            reader = sys[self.sym["pipeowners"] + i]
            lines = sys[self.sym["pipetypes"] + i]
            buf = sys[self.sym["pipebufs"] + 256 * i:self.sym["pipebufs"] + 256 * i + 256]
            if sides or reader:
                out.append({"handle": 0x80 + i, "open_sides": sides, "reader": reader,
                            "lines": lines, "queued": bytes(buf[1:1 + buf[0]])})
        return out

    # --- NK-14 -----------------------------------------------------------
    def sockets(self):
        page, at = self.pages.locate("pgtrdosfs", self.sym["w53_socflags"] + 1)
        data = self.api.ram_page(page)
        out = []
        for i in range(8):
            e = data[at + k.SOCKET_ENTRY * i:at + k.SOCKET_ENTRY * (i + 1)]
            out.append({"handle": 1 + k.SOCKET_ENTRY * i, "chip_socket": e[1] - 8,
                        "rx_count": e[2] | e[3] << 8, "owner": e[4]})
        port_page, port_at = self.pages.locate("pgtrdosfs", self.sym["wizlocalport"])
        return out, k.word(self.api.ram_page(port_page), port_at)

    # --- NK-13 -----------------------------------------------------------
    def open_files(self):
        page, at = self.pages.locate("pgfatfs2", self.sym["ffilearray"])
        data = self.api.ram_page(page)
        size = self.sym["FIL_sz"]
        out = []
        for i in range(self.sym["MAXFILES"]):
            e = data[at + i * size:at + i * size + 32]
            if len(e) < 32:
                break
            fs = k.word(e, 0)
            if not fs:
                continue
            fptr, fsize = struct.unpack_from("<II", e, 6)
            dir_sector = struct.unpack_from("<I", e, 0x1A)[0]
            dir_ptr = k.word(e, 0x1E)
            out.append({"index": i, "owner": e[k.FIL_OWNER], "flag": e[4], "position": fptr,
                        "size": fsize, "dir_sector": dir_sector,
                        "dir_entry_offset": dir_ptr - fs - k.FATFS_WIN})
        return out

    # --- NK-21 -----------------------------------------------------------
    def kernel_state(self):
        """Is the CPU in the kernel, and if so for whom and in which call."""
        regs = self.api.registers()
        banks = self.api.bank_pages()
        in_kernel = banks[0] == self.pages.roles["pgsys"]
        state = {"pc": regs["special"]["pc"], "bank_pages": banks, "in_kernel": in_kernel,
                 "timer": self.sys_byte("sys_timer")}
        if not in_kernel:
            return state
        sys = self.page_of("pgsys")
        handler = k.word(sys, self.sym["callbdos"] + k.CALLBDOS_HANDLER_OPERAND)
        table = self.sym["tbdoscmds"]
        commands = [c for c in range(256) if (sys[table + c] | sys[table + 256 + c] << 8) == handler]
        app = self.current_app()
        task = next(t for t in self.tasks() if t.app == app)
        user_sp = self.sys_word("callbdos_sp")
        ret = k.word(self.read_task(task, user_sp, 2), 0)
        state.update({
            "task": task.id, "task_name": task.name, "handler": handler,
            "handler_label": self.label_at(handler, "pgsys"),
            "command": [f"CMD_{self.sym.commands.get(c, hex(c))} (#{c:02X})" for c in commands],
            "caller_pc": (ret - 3) & 0xFFFF, "user_sp": user_sp,
            "cpu_label": self.label_at(regs["special"]["pc"], None),
            "bc": regs["main"]["bc"], "a": regs["main"]["af"] >> 8,
        })
        return state

    def label_at(self, address, role):
        """Nearest label at or below address (role filtering is future work: user.l is flat)."""
        best = None
        for name, value in self.sym.values.items():
            if value <= address and (best is None or value > best[1]) and not name.startswith("CMD_"):
                if address - value < 0x400:
                    best = (name, value)
        return f"{best[0]}+#{address - best[1]:X}" if best else f"#{address:04X}"
