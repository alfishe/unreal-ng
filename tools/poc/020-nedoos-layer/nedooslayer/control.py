"""Operations that change the machine: kernel call trace, direct kernel calls, ending a stuck call.

All of them go through WebAPI breakpoints and register writes, so they need
debug mode; each one leaves the machine as it found it (breakpoints cleared,
debug mode restored).
"""

import collections
import time

from . import kernel as k


class Control:
    def __init__(self, os_view):
        self.os = os_view
        self.api = os_view.api
        self.sym = os_view.sym

    def _in_sys(self):
        return self.api.bank_pages()[0] == self.os.pages.roles["pgsys"]

    def _current_task_id(self):
        app = self.os.current_app()
        return self.os.page_of("pgsys")[app + 1]

    # --- NK-15 (demo quality) --------------------------------------------
    def trace(self, count=60, timeout=30.0):
        """Sample kernel calls with a breakpoint on callbdos (about 4 calls/s over the WebAPI)."""
        entry = self.sym["callbdos"]
        calls = []
        self.api.debug_mode(True)
        self.api.clear_breakpoints()
        self.api.add_exec_breakpoint(entry, "nedoos-layer trace")
        deadline = time.time() + timeout
        try:
            while len(calls) < count and time.time() < deadline:
                regs = self.api.run_until(lambda r: r["special"]["pc"] == entry and self._in_sys(),
                                          timeout=max(0.1, deadline - time.time()))
                if regs is None:
                    break
                cmd = regs["main"]["bc"] & 0xFF
                calls.append((self._current_task_id(), cmd))
        finally:
            self.api.clear_breakpoints()
            self.api.debug_mode(False)
            self.api.resume()
        return collections.Counter((task, self.sym.commands.get(cmd, f"#{cmd:02X}")) for task, cmd in calls)

    # --- NK-11 -----------------------------------------------------------
    def call(self, task_id, command, bc=None, de=None, hl=None, timeout=15.0):
        """Run one kernel call on behalf of a task at a safe point; returns the result registers."""
        entry = self.sym["callbdos"]
        yield_cmd = self.sym["CMD_YIELD"]
        self.api.debug_mode(True)
        self.api.clear_breakpoints()
        try:
            # 1. the task enters the kernel with YIELD: its return address is on its stack
            self.api.add_exec_breakpoint(entry, "nedoos-layer safe point")
            regs = self.api.run_until(
                lambda r: r["special"]["pc"] == entry and self._in_sys()
                and (r["main"]["bc"] & 0xFF) == yield_cmd and self._current_task_id() == task_id,
                timeout)
            if regs is None:
                raise TimeoutError(f"task {task_id} did not yield (kernel busy?)")
            task = self.os.task(task_id)
            back = k.word(self.os.read_task(task, regs["special"]["sp"], 2), 0)
            # 2. the yield returns: the task is in user mode, interrupts on, kernel free
            self.api.clear_breakpoints()
            self.api.add_exec_breakpoint(back, "nedoos-layer return")
            saved = self.api.run_until(
                lambda r: r["special"]["pc"] == back and not self._in_sys()
                and self._current_task_id() == task_id, timeout)
            if saved is None:
                raise TimeoutError("task did not come back from YIELD")
            # 3. call #0005 from there; the call returns to the same pc with the same sp
            sp = saved["special"]["sp"] - 2
            self.api.set_register("sp", sp)
            self.api.write_cpu(sp, [back & 0xFF, back >> 8])
            m = saved["main"]
            self.api.set_register("bc", ((bc if bc is not None else m["bc"]) & 0xFF00) | command)
            if de is not None:
                self.api.set_register("de", de)
            if hl is not None:
                self.api.set_register("hl", hl)
            self.api.set_register("pc", 0x0005)
            result = self.api.run_until(
                lambda r: r["special"]["pc"] == back and r["special"]["sp"] == saved["special"]["sp"]
                and self._current_task_id() == task_id, timeout)
            if result is None:
                raise TimeoutError("kernel call did not return")
            # 4. put every register back
            for name in ("af", "bc", "de", "hl"):
                self.api.set_register(name, saved["main"][name])
            for name in ("ix", "iy"):
                self.api.set_register(name, saved["index"][name])
            return result
        finally:
            self.api.clear_breakpoints()
            self.api.debug_mode(False)
            self.api.resume()

    # --- NK-22 -----------------------------------------------------------
    def end_stuck_network_call(self):
        """Make a stuck Wiznet call return ERR_NOTCONN to its caller (machine must be paused in it)."""
        state = self.os.kernel_state()
        if not state.get("in_kernel"):
            raise RuntimeError("the CPU is not in the kernel")
        sp = self.api.registers()["special"]["sp"]
        sys = self.os.page_of("pgsys")
        top = k.word(sys, sp) if sp < 0x4000 else None
        expected = self.sym["callbdos"] + k.CALLBDOS_HANDLER_OPERAND + 2
        if top != expected:
            raise RuntimeError(f"kernel stack top is {top!r}, not the return into callbdos (#{expected:04X})")
        self.api.set_register("pc", self.sym["wiznet_fail_1"])
        return state
