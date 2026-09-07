#!/usr/bin/env python3
"""
True Process Workload & Thread Profiler for Unreal-NG
Supports unreal-qt, unreal-videowall, and unreal-screen-viewer.

Calculates and displays the true real-time workload of single or multi-instance
Unreal-NG processes, with specialized analytics for VideoWall tile grids.

Highlights:
1. Single-Core Equivalent % (where 100% = 1 full CPU core saturated)
2. All-Cores / System Monitor % (normalized across all logical cores, matching
   system monitors like GNOME Resources, KDE System Monitor, or Task Manager)
3. VideoWall Tile Analytics: Count active emulator tiles, per-tile CPU consumption,
   grid rendering overhead, and sustainable tile capacity forecast.
4. Multi-Process Overview: Monitor all Unreal-NG processes concurrently.

Zero external dependencies required on Linux (uses /proc filesystem directly).
Supports psutil on Windows, macOS, and Linux when available.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import json
import os
import platform
import re
import shutil
import signal
import sys
import time
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Tuple

# Try importing psutil for cross-platform support (Windows/macOS/Linux)
try:
    import psutil  # type: ignore
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False

DEFAULT_TARGET_NAMES = ["unreal-videowall", "unreal-videowal", "unreal-qt", "unreal-screen-viewer"]

_terminal_resized = False


def _handle_sigwinch(signum: Any, frame: Any) -> None:
    global _terminal_resized
    _terminal_resized = True


def setup_signal_handlers() -> None:
    if hasattr(signal, "SIGWINCH"):
        try:
            signal.signal(signal.SIGWINCH, _handle_sigwinch)
        except Exception:
            pass


def enable_windows_vt() -> None:
    """Enables Virtual Terminal Processing (ANSI escape sequences) on Windows console handles."""
    if platform.system() == "Windows":
        try:
            import ctypes
            kernel32 = ctypes.windll.kernel32
            h_out = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
            if h_out and h_out != -1:
                mode = ctypes.c_ulong()
                if kernel32.GetConsoleMode(h_out, ctypes.byref(mode)):
                    kernel32.SetConsoleMode(h_out, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING
        except Exception:
            try:
                os.system('')  # Fallback for Windows 10 ANSI support
            except Exception:
                pass


class TerminalScreenManager:
    """
    Manages ANSI terminal alternate screen buffer, cursor visibility, and clean teardown.
    Switches to alternate screen buffer (\033[?1049h) so terminal scrollback history is preserved.
    """
    def __init__(self, enabled: bool):
        self.enabled = enabled
        self._in_alt_screen = False

    def enter(self) -> None:
        if not self.enabled:
            return
        enable_windows_vt()
        setup_signal_handlers()
        # Switch to alternate screen buffer and hide cursor
        sys.stdout.write("\033[?1049h\033[?25l")
        sys.stdout.flush()
        self._in_alt_screen = True

    def leave(self) -> None:
        if not self.enabled or not self._in_alt_screen:
            return
        # Show cursor and leave alternate screen buffer
        sys.stdout.write("\033[?25h\033[?1049l")
        sys.stdout.flush()
        self._in_alt_screen = False

    def __enter__(self) -> "TerminalScreenManager":
        self.enter()
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.leave()


def draw_terminal_screen(content: str, term_columns: int, term_lines: int, force_clear: bool = False) -> None:
    """
    Draws dashboard in-place at cursor position (1,1).
    Appends \\033[K (erase to end of line) to prevent residual characters from previous ticks.
    Appends \\033[J (erase to bottom of screen) to clear leftover rows.
    """
    lines = content.splitlines()
    lines = lines[:max(1, term_lines - 1)]

    out_parts: List[str] = []
    if force_clear:
        out_parts.append("\033[2J\033[H")
    else:
        out_parts.append("\033[H")

    for line in lines:
        out_parts.append(line + "\033[K\n")

    out_parts.append("\033[J")

    sys.stdout.write("".join(out_parts))
    sys.stdout.flush()




# -----------------------------------------------------------------------------
# Data Structures
# -----------------------------------------------------------------------------

@dataclass
class ThreadSample:
    tid: int
    name: str
    single_core_pct: float
    system_pct: float
    total_cpu_seconds: float
    state: str = "S"
    category: str = "Other"  # "Tile", "UI", "Service", "Worker"


@dataclass
class VideoWallMetrics:
    is_videowall: bool
    tile_count: int
    emulation_single_core_pct: float
    emulation_system_pct: float
    avg_per_tile_pct: float
    min_tile_pct: float
    max_tile_pct: float
    rendering_single_core_pct: float
    services_single_core_pct: float
    estimated_max_tiles: int


@dataclass
class ProcessSample:
    pid: int
    name: str
    timestamp: float
    elapsed_seconds: float
    cpu_time_consumed: float
    user_time_consumed: float
    system_time_consumed: float
    single_core_pct: float
    system_pct: float
    cores_equivalent: float
    rss_mb: float
    vms_mb: float
    thread_count: int
    num_cores: int
    threads: List[ThreadSample] = field(default_factory=list)
    videowall: Optional[VideoWallMetrics] = None


# -----------------------------------------------------------------------------
# Thread Categorization & VideoWall Analytics
# -----------------------------------------------------------------------------

def categorize_thread(name: str, pid: int, tid: int, proc_name: str = "", single_core_pct: float = 0.0) -> str:
    name_lower = name.lower()
    proc_lower = proc_name.lower()
    is_videowall = "videowall" in proc_lower

    if name_lower.startswith("emulator") or "emulator-" in name_lower:
        return "Tile" if is_videowall else "Emulator"
    if "drogon" in name_lower or "audio" in name_lower or "sound" in name_lower or "logger" in name_lower or "lua" in name_lower or "dbus" in name_lower or "gmain" in name_lower or "gdbus" in name_lower or "dconf" in name_lower:
        return "Service"
    if "pool" in name_lower or "thread" in name_lower:
        return "Worker"
    if "xcb" in name_lower:
        return "UI"
    if tid == pid:
        return "UI"

    # On Linux, thread names exceeding 15 chars (e.g. 'emulator-xxxxxxxxxxxx') inherit the parent process comm.
    # In unreal-videowall / unreal-qt, worker threads running emulation have the process comm name.
    is_unreal = is_videowall or "unreal" in proc_lower
    if is_unreal and (name_lower == proc_lower or name_lower.startswith(proc_lower[:10])):
        if single_core_pct >= 2.0 or is_videowall and single_core_pct >= 0.5:
            return "Tile" if is_videowall else "Emulator"
        return "Worker"

    return "Other"


def compute_videowall_metrics(
    proc_name: str,
    threads: List[ThreadSample],
    num_cores: int
) -> Optional[VideoWallMetrics]:
    emulator_threads = [t for t in threads if t.category in ("Tile", "Emulator") or t.name.lower().startswith("emulator")]
    is_vw = "videowall" in proc_name.lower() or len(emulator_threads) >= 2

    tile_count = len(emulator_threads)
    if tile_count == 0 and not is_vw:
        return None

    emu_cpu = sum(t.single_core_pct for t in emulator_threads)
    emu_sys = emu_cpu / num_cores
    avg_tile = (emu_cpu / tile_count) if tile_count > 0 else 0.0
    min_tile = min((t.single_core_pct for t in emulator_threads), default=0.0)
    max_tile = max((t.single_core_pct for t in emulator_threads), default=0.0)

    rendering_threads = [t for t in threads if t.category in ("UI", "Worker") and t not in emulator_threads]
    rendering_cpu = sum(t.single_core_pct for t in rendering_threads)

    service_threads = [t for t in threads if t.category == "Service"]
    service_cpu = sum(t.single_core_pct for t in service_threads)

    # Capacity forecast: Assume 85% of total multi-core headroom can be used safely
    # (leaving 15% for OS scheduler, compositor, audio DAC interrupts)
    effective_avg = avg_tile if avg_tile > 0.5 else 6.0  # default ~6% per Pentagon tile
    available_core_pct = max((num_cores * 100.0 * 0.85) - max(rendering_cpu, 5.0), 0.0)
    estimated_max = int(available_core_pct / effective_avg)

    return VideoWallMetrics(
        is_videowall=is_vw,
        tile_count=tile_count,
        emulation_single_core_pct=emu_cpu,
        emulation_system_pct=emu_sys,
        avg_per_tile_pct=avg_tile,
        min_tile_pct=min_tile,
        max_tile_pct=max_tile,
        rendering_single_core_pct=rendering_cpu,
        services_single_core_pct=service_cpu,
        estimated_max_tiles=estimated_max,
    )


# -----------------------------------------------------------------------------
# OS-Specific Process Collectors
# -----------------------------------------------------------------------------

class BaseCollector:
    def __init__(self, pid: int, num_cores: int):
        self.pid = pid
        self.num_cores = num_cores

    def sample(self, sample_duration: float) -> Optional[ProcessSample]:
        raise NotImplementedError

    def is_alive(self) -> bool:
        raise NotImplementedError

    def process_name(self) -> str:
        raise NotImplementedError


class LinuxProcCollector(BaseCollector):
    """Native Linux collector using /proc filesystem with zero external dependencies."""

    def __init__(self, pid: int, num_cores: int):
        super().__init__(pid, num_cores)
        self.clock_ticks = os.sysconf(os.sysconf_names.get("SC_CLK_TCK", 100))
        self.page_size = os.sysconf(os.sysconf_names.get("SC_PAGE_SIZE", 4096))
        self._name = self._read_name()

    def _read_name(self) -> str:
        try:
            with open(f"/proc/{self.pid}/comm", "r", encoding="utf-8") as f:
                return f.read().strip()
        except Exception:
            return f"pid-{self.pid}"

    def process_name(self) -> str:
        return self._name

    def is_alive(self) -> bool:
        return os.path.exists(f"/proc/{self.pid}")

    def _get_proc_times_and_mem(self) -> Optional[Tuple[int, int, int, int, int]]:
        try:
            with open(f"/proc/{self.pid}/stat", "r", encoding="utf-8") as f:
                content = f.read()
            r_idx = content.rfind(")")
            if r_idx == -1:
                return None
            fields = content[r_idx + 2:].split()
            utime = int(fields[11])
            stime = int(fields[12])
            num_threads = int(fields[17])
            vms_bytes = int(fields[20])
            rss_pages = int(fields[21])
            rss_bytes = rss_pages * self.page_size
            return utime, stime, rss_bytes, vms_bytes, num_threads
        except Exception:
            return None

    def _get_threads(self) -> Dict[int, Tuple[str, int, int, str]]:
        res: Dict[int, Tuple[str, int, int, str]] = {}
        task_dir = f"/proc/{self.pid}/task"
        if not os.path.isdir(task_dir):
            return res

        for tid_str in os.listdir(task_dir):
            if not tid_str.isdigit():
                continue
            tid = int(tid_str)
            stat_path = f"{task_dir}/{tid_str}/stat"
            comm_path = f"{task_dir}/{tid_str}/comm"
            try:
                comm = ""
                if os.path.exists(comm_path):
                    with open(comm_path, "r", encoding="utf-8") as f:
                        comm = f.read().strip()

                with open(stat_path, "r", encoding="utf-8") as f:
                    content = f.read()

                r_idx = content.rfind(")")
                if r_idx == -1:
                    continue
                if not comm:
                    l_idx = content.find("(")
                    comm = content[l_idx + 1:r_idx]

                fields = content[r_idx + 2:].split()
                state = fields[0]
                utime = int(fields[11])
                stime = int(fields[12])
                res[tid] = (comm, utime, stime, state)
            except Exception:
                continue
        return res

    def sample(self, sample_duration: float) -> Optional[ProcessSample]:
        stat1 = self._get_proc_times_and_mem()
        threads1 = self._get_threads()
        if stat1 is None:
            return None

        t1 = time.time()
        time.sleep(sample_duration)
        t2 = time.time()

        stat2 = self._get_proc_times_and_mem()
        threads2 = self._get_threads()
        if stat2 is None:
            return None

        elapsed = max(t2 - t1, 0.0001)

        u1, s1, _, _, _ = stat1
        u2, s2, rss_bytes, vms_bytes, num_threads = stat2

        delta_u = max(u2 - u1, 0)
        delta_s = max(s2 - s1, 0)
        delta_total = delta_u + delta_s

        cpu_time = delta_total / self.clock_ticks
        user_time = delta_u / self.clock_ticks
        sys_time = delta_s / self.clock_ticks

        single_core_pct = (cpu_time / elapsed) * 100.0
        system_pct = single_core_pct / self.num_cores
        cores_equivalent = single_core_pct / 100.0

        thread_samples: List[ThreadSample] = []
        for tid, (comm, u2_th, s2_th, state) in threads2.items():
            prev = threads1.get(tid)
            if prev is not None:
                _, u1_th, s1_th, _ = prev
                diff_ticks = max((u2_th + s2_th) - (u1_th + s1_th), 0)
            else:
                diff_ticks = 0

            th_cpu_time = diff_ticks / self.clock_ticks
            th_single_core = (th_cpu_time / elapsed) * 100.0
            th_sys = th_single_core / self.num_cores
            total_sec = (u2_th + s2_th) / self.clock_ticks
            category = categorize_thread(comm, self.pid, tid, self._name, th_single_core)

            thread_samples.append(ThreadSample(
                tid=tid,
                name=comm,
                single_core_pct=th_single_core,
                system_pct=th_sys,
                total_cpu_seconds=total_sec,
                state=state,
                category=category
            ))

        thread_samples.sort(key=lambda t: t.single_core_pct, reverse=True)
        vw_metrics = compute_videowall_metrics(self._name, thread_samples, self.num_cores)

        return ProcessSample(
            pid=self.pid,
            name=self._name,
            timestamp=t2,
            elapsed_seconds=elapsed,
            cpu_time_consumed=cpu_time,
            user_time_consumed=user_time,
            system_time_consumed=sys_time,
            single_core_pct=single_core_pct,
            system_pct=system_pct,
            cores_equivalent=cores_equivalent,
            rss_mb=rss_bytes / (1024.0 * 1024.0),
            vms_mb=vms_bytes / (1024.0 * 1024.0),
            thread_count=num_threads,
            num_cores=self.num_cores,
            threads=thread_samples,
            videowall=vw_metrics
        )


class PsutilCollector(BaseCollector):
    """Cross-platform collector using psutil (Windows, macOS, Linux)."""

    def __init__(self, pid: int, num_cores: int):
        super().__init__(pid, num_cores)
        self.proc = psutil.Process(pid)
        self._name = self.proc.name()
        self._get_thread_description = self._init_thread_description_api()

    def _init_thread_description_api(self):
        """Initialize GetThreadDescription API on Windows 10 1607+."""
        if platform.system() != "Windows":
            return None
        try:
            import ctypes
            from ctypes import wintypes
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            GetThreadDescription = kernel32.GetThreadDescription
            GetThreadDescription.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
            GetThreadDescription.restype = wintypes.LONG
            LocalFree = kernel32.LocalFree
            LocalFree.argtypes = [wintypes.HANDLE]
            LocalFree.restype = wintypes.HANDLE
            OpenThread = kernel32.OpenThread
            OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            OpenThread.restype = wintypes.HANDLE
            CloseHandle = kernel32.CloseHandle
            CloseHandle.argtypes = [wintypes.HANDLE]
            CloseHandle.restype = wintypes.BOOL
            return (GetThreadDescription, LocalFree, OpenThread, CloseHandle, ctypes)
        except Exception:
            return None

    def _get_win_thread_name(self, tid: int) -> Optional[str]:
        """Get thread name on Windows using GetThreadDescription API."""
        if self._get_thread_description is None:
            return None
        GetThreadDescription, LocalFree, OpenThread, CloseHandle, ctypes = self._get_thread_description
        THREAD_QUERY_LIMITED_INFORMATION = 0x0800
        try:
            h_thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, False, tid)
            if not h_thread:
                return None
            try:
                desc_ptr = ctypes.c_wchar_p()
                hr = GetThreadDescription(h_thread, ctypes.byref(desc_ptr))
                if hr >= 0 and desc_ptr.value:
                    name = desc_ptr.value
                    LocalFree(ctypes.cast(desc_ptr, ctypes.c_void_p))
                    return name if name else None
                return None
            finally:
                CloseHandle(h_thread)
        except Exception:
            return None

    def process_name(self) -> str:
        return self._name

    def is_alive(self) -> bool:
        try:
            return self.proc.is_running() and self.proc.status() != psutil.STATUS_ZOMBIE
        except Exception:
            return False

    def sample(self, sample_duration: float) -> Optional[ProcessSample]:
        try:
            t1 = time.time()
            cpu1 = self.proc.cpu_times()
            threads_before = {}
            try:
                for th in self.proc.threads():
                    threads_before[th.id] = th.user_time + th.system_time
            except Exception:
                pass

            time.sleep(sample_duration)
            t2 = time.time()

            cpu2 = self.proc.cpu_times()
            mem = self.proc.memory_info()
            num_threads = self.proc.num_threads()

            elapsed = max(t2 - t1, 0.0001)
            delta_user = max(cpu2.user - cpu1.user, 0.0)
            delta_system = max(cpu2.system - cpu1.system, 0.0)
            cpu_time = delta_user + delta_system

            single_core_pct = (cpu_time / elapsed) * 100.0
            system_pct = single_core_pct / self.num_cores
            cores_equivalent = single_core_pct / 100.0

            thread_samples: List[ThreadSample] = []
            try:
                for th in self.proc.threads():
                    prev_time = threads_before.get(th.id, th.user_time + th.system_time)
                    th_cpu_time = max((th.user_time + th.system_time) - prev_time, 0.0)
                    th_single_core = (th_cpu_time / elapsed) * 100.0
                    th_sys = th_single_core / self.num_cores
                    th_name = self._get_win_thread_name(th.id) or f"thread-{th.id}"
                    cat = categorize_thread(th_name, self.pid, th.id, self._name, th_single_core)
                    thread_samples.append(ThreadSample(
                        tid=th.id,
                        name=th_name,
                        single_core_pct=th_single_core,
                        system_pct=th_sys,
                        total_cpu_seconds=th.user_time + th.system_time,
                        state="R" if th_single_core > 0.1 else "S",
                        category=cat
                    ))
            except Exception:
                pass

            thread_samples.sort(key=lambda t: t.single_core_pct, reverse=True)
            vw_metrics = compute_videowall_metrics(self._name, thread_samples, self.num_cores)

            return ProcessSample(
                pid=self.pid,
                name=self._name,
                timestamp=t2,
                elapsed_seconds=elapsed,
                cpu_time_consumed=cpu_time,
                user_time_consumed=delta_user,
                system_time_consumed=delta_system,
                single_core_pct=single_core_pct,
                system_pct=system_pct,
                cores_equivalent=cores_equivalent,
                rss_mb=mem.rss / (1024.0 * 1024.0),
                vms_mb=mem.vms / (1024.0 * 1024.0),
                thread_count=num_threads,
                num_cores=self.num_cores,
                threads=thread_samples,
                videowall=vw_metrics
            )
        except Exception:
            return None


def create_collector(pid: int, num_cores: int) -> BaseCollector:
    if platform.system() == "Linux" and os.path.isdir(f"/proc/{pid}"):
        return LinuxProcCollector(pid, num_cores)

    if HAS_PSUTIL:
        return PsutilCollector(pid, num_cores)

    if platform.system() == "Linux":
        return LinuxProcCollector(pid, num_cores)

    raise RuntimeError(
        f"psutil is required on {platform.system()} to monitor processes. "
        f"Install it with: pip install psutil"
    )


# -----------------------------------------------------------------------------
# Process Discovery & Multi-Process Scanning
# -----------------------------------------------------------------------------

def find_processes_by_name(pattern: str) -> List[Tuple[int, str]]:
    matches: List[Tuple[int, str]] = []

    if HAS_PSUTIL:
        try:
            for p in psutil.process_iter(["pid", "name", "cmdline"]):
                name = p.info.get("name") or ""
                cmdline = p.info.get("cmdline") or []
                exe_base = os.path.basename(cmdline[0]) if cmdline else ""
                if re.search(pattern, name, re.IGNORECASE) or (exe_base and re.search(pattern, exe_base, re.IGNORECASE)):
                    matches.append((p.info["pid"], name or exe_base))
            return matches
        except Exception:
            pass

    if platform.system() == "Linux" and os.path.isdir("/proc"):
        for entry in os.listdir("/proc"):
            if not entry.isdigit():
                continue
            pid = int(entry)
            try:
                comm = ""
                comm_path = f"/proc/{pid}/comm"
                if os.path.exists(comm_path):
                    with open(comm_path, "r", encoding="utf-8") as f:
                        comm = f.read().strip()

                exe_base = ""
                cmdline_path = f"/proc/{pid}/cmdline"
                if os.path.exists(cmdline_path):
                    with open(cmdline_path, "rb") as f:
                        raw = f.read()
                        if raw:
                            first_arg = raw.split(b"\x00")[0].decode("utf-8", "replace")
                            exe_base = os.path.basename(first_arg)

                # Match against process comm or executable basename, NOT arbitrary command-line arguments
                if re.search(pattern, comm, re.IGNORECASE) or (exe_base and re.search(pattern, exe_base, re.IGNORECASE)):
                    matches.append((pid, comm if comm else exe_base))
            except Exception:
                continue
        return matches

    if platform.system() in ("Darwin", "FreeBSD"):
        try:
            import subprocess
            out = subprocess.check_output(["ps", "-ax", "-o", "pid,command"], text=True)
            for line in out.splitlines()[1:]:
                parts = line.strip().split(None, 1)
                if len(parts) == 2:
                    p_pid, p_cmd = int(parts[0]), parts[1]
                    exe_name = os.path.basename(p_cmd.split()[0])
                    if re.search(pattern, exe_name, re.IGNORECASE):
                        matches.append((p_pid, exe_name))
            return matches
        except Exception:
            pass

    if platform.system() == "Windows":
        try:
            import subprocess
            out = subprocess.check_output(["tasklist", "/FO", "CSV", "/NH"], text=True)
            for row in csv.reader(out.splitlines()):
                if len(row) >= 2:
                    p_name, p_pid = row[0], int(row[1])
                    if re.search(pattern, p_name, re.IGNORECASE):
                        matches.append((p_pid, p_name))
            return matches
        except Exception:
            pass

    return matches


def find_all_unreal_processes() -> List[Tuple[int, str]]:
    """Discovers all running Unreal-NG related processes (unreal-qt, unreal-videowall, unreal-screen-viewer)."""
    found: Dict[int, str] = {}
    for name in DEFAULT_TARGET_NAMES:
        for pid, p_name in find_processes_by_name(re.escape(name)):
            found[pid] = p_name
    # Return sorted by PID
    return sorted(found.items(), key=lambda x: x[0])


def auto_detect_target(preferred_name: Optional[str] = None) -> Optional[Tuple[int, str]]:
    candidates = [preferred_name] if preferred_name else DEFAULT_TARGET_NAMES

    for cand in candidates:
        if not cand:
            continue
        found = find_processes_by_name(f"^{re.escape(cand)}(\\.exe)?$")
        if not found:
            found = find_processes_by_name(re.escape(cand))
        if found:
            found.sort(key=lambda x: x[0], reverse=True)
            return found[0]

    return None


# -----------------------------------------------------------------------------
# Formatting and UI
# -----------------------------------------------------------------------------

class Colors:
    def __init__(self, enabled: bool):
        self.enabled = enabled
        self.RESET = "\033[0m" if enabled else ""
        self.BOLD = "\033[1m" if enabled else ""
        self.DIM = "\033[2m" if enabled else ""
        self.GREEN = "\033[32m" if enabled else ""
        self.YELLOW = "\033[33m" if enabled else ""
        self.RED = "\033[31m" if enabled else ""
        self.CYAN = "\033[36m" if enabled else ""
        self.BLUE = "\033[34m" if enabled else ""
        self.MAGENTA = "\033[35m" if enabled else ""
        self.WHITE = "\033[37m" if enabled else ""

    def color_pct(self, pct: float, warn_threshold: float = 70.0, crit_threshold: float = 95.0) -> str:
        if not self.enabled:
            return f"{pct:6.2f}%"
        if pct >= crit_threshold:
            c = self.RED
        elif pct >= warn_threshold:
            c = self.YELLOW
        else:
            c = self.GREEN
        return f"{c}{pct:6.2f}%{self.RESET}"


def make_bar(pct: float, max_val: float = 100.0, length: int = 24) -> str:
    filled_ratio = min(max(pct / max_val, 0.0), 1.0)
    filled_len = int(round(filled_ratio * length))
    empty_len = length - filled_len
    return f"[{'█' * filled_len}{'░' * empty_len}]"


def format_duration(seconds: float) -> str:
    m, s = divmod(seconds, 60)
    h, m = divmod(m, 60)
    if h > 0:
        return f"{int(h)}h {int(m):02d}m {s:04.1f}s"
    if m > 0:
        return f"{int(m)}m {s:04.1f}s"
    return f"{s:5.2f}s"


def render_dashboard(
    sample: ProcessSample,
    top_threads: int,
    show_all_threads: bool,
    group_threads: bool,
    colors: Colors,
    history: List[float],
    start_time: float,
    term_width: int = 78,
    term_lines: int = 24
) -> str:
    lines: List[str] = []
    width = max(40, min(term_width - 1, 100))
    divider = "─" * width

    # History metrics
    avg_single = sum(history) / len(history) if history else sample.single_core_pct
    min_single = min(history) if history else sample.single_core_pct
    max_single = max(history) if history else sample.single_core_pct
    session_duration = time.time() - start_time

    # Header
    now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    app_type = "VIDEOWALL" if (sample.videowall and sample.videowall.is_videowall) else sample.name.upper()
    lines.append(f"{colors.BOLD}{colors.CYAN}┌{'─' * (width - 2)}┐{colors.RESET}")
    title = f" TRUE WORKLOAD PROFILER :: {app_type} (PID {sample.pid}) "
    if len(title) > width - 4:
        title = title[:max(5, width - 7)] + "... "
    lines.append(f"{colors.BOLD}{colors.CYAN}│{colors.WHITE}{title.center(width - 2)}{colors.CYAN}│{colors.RESET}")
    lines.append(f"{colors.BOLD}{colors.CYAN}└{'─' * (width - 2)}┘{colors.RESET}")

    if width >= 75:
        lines.append(
            f" Host: {colors.BOLD}{platform.node()}{colors.RESET} | "
            f"OS: {platform.system()} {platform.release()} | "
            f"Logical Cores: {colors.BOLD}{sample.num_cores}{colors.RESET} | "
            f"Time: {now_str}"
        )
    else:
        lines.append(
            f" Host: {colors.BOLD}{platform.node()}{colors.RESET} | "
            f"Cores: {colors.BOLD}{sample.num_cores}{colors.RESET} | "
            f"Time: {now_str}"
        )
    lines.append(divider)

    # Core comparison callout
    lines.append(f"{colors.BOLD}CPU WORKLOAD BREAKDOWN:{colors.RESET}")
    bar_len = max(8, min(20, width - 58))
    single_bar = make_bar(sample.single_core_pct, 100.0, bar_len)
    sys_bar = make_bar(sample.system_pct, 100.0, bar_len)

    lines.append(
        f"  ▶ {colors.BOLD}Single-Core Load:{colors.RESET}     "
        f"{colors.color_pct(sample.single_core_pct)} {single_bar} "
        f"{colors.DIM}(1 core = 100.0%){colors.RESET}"
    )
    lines.append(
        f"  ▶ {colors.BOLD}All-Cores Equivalent:{colors.RESET} "
        f"{colors.color_pct(sample.system_pct)} {sys_bar} "
        f"{colors.DIM}(normalized across {sample.num_cores} cores){colors.RESET}"
    )
    lines.append(
        f"  ▶ {colors.BOLD}Cores Saturated:{colors.RESET}      "
        f"{colors.CYAN}{sample.cores_equivalent:5.2f} core(s){colors.RESET} "
        f"{colors.DIM}| Interval: {sample.elapsed_seconds:.2f}s | CPU Time: {sample.cpu_time_consumed:.3f}s{colors.RESET}"
    )

    if term_lines >= 18:
        lines.append(
            f"  ▶ {colors.BOLD}CPU Mode Splits:{colors.RESET}      "
            f"User: {colors.CYAN}{sample.user_time_consumed:.3f}s{colors.RESET} | "
            f"Kernel/System: {colors.YELLOW}{sample.system_time_consumed:.3f}s{colors.RESET}"
        )

    if term_lines >= 20:
        lines.append(
            f"  ▶ {colors.BOLD}Session Statistics:{colors.RESET}   "
            f"Avg: {colors.color_pct(avg_single)} | "
            f"Min: {colors.color_pct(min_single)} | "
            f"Max: {colors.color_pct(max_single)} | "
            f"Duration: {format_duration(session_duration)}"
        )

    # VideoWall Analytics Card (if videowall or multiple emulators)
    has_vw = bool(sample.videowall and (sample.videowall.is_videowall or sample.videowall.tile_count > 0))
    if has_vw and sample.videowall and term_lines >= 22:
        vw = sample.videowall
        lines.append(divider)
        lines.append(f"{colors.BOLD}{colors.MAGENTA}VIDEOWALL GRID & TILE ANALYTICS:{colors.RESET}")
        lines.append(
            f"  🔲 {colors.BOLD}Active Tiles / Emulators:{colors.RESET}  {colors.CYAN}{vw.tile_count:3d} tiles{colors.RESET} "
            f"| Total Tile CPU: {colors.color_pct(vw.emulation_single_core_pct)} "
            f"({vw.emulation_system_pct:.2f}% system)"
        )
        lines.append(
            f"  ⚡ {colors.BOLD}Avg Load per Tile:{colors.RESET}         {colors.GREEN}{vw.avg_per_tile_pct:6.2f}%{colors.RESET} "
            f"{colors.DIM}(min: {vw.min_tile_pct:.2f}%, max: {vw.max_tile_pct:.2f}%){colors.RESET}"
        )
        lines.append(
            f"  🖥️  {colors.BOLD}Grid & UI Rendering Load:{colors.RESET}   {colors.YELLOW}{vw.rendering_single_core_pct:6.2f}%{colors.RESET} "
            f"| Services / Automation: {vw.services_single_core_pct:.2f}%"
        )
        lines.append(
            f"  🚀 {colors.BOLD}Capacity Forecast:{colors.RESET}         "
            f"~{colors.BOLD}{vw.estimated_max_tiles}{colors.RESET} sustainable 50 FPS tiles on {sample.num_cores} cores "
            f"{colors.DIM}(85% CPU ceiling){colors.RESET}"
        )

    # Memory & Process Health
    if term_lines >= 16:
        lines.append(divider)
        lines.append(f"{colors.BOLD}MEMORY & PROCESS HEALTH:{colors.RESET}")
        lines.append(
            f"  • Resident Memory (RSS): {colors.BOLD}{sample.rss_mb:8.2f} MB{colors.RESET}   "
            f"• Virtual Memory (VMS): {sample.vms_mb:8.2f} MB"
        )
        active_threads = sum(1 for t in sample.threads if t.single_core_pct > 0.05)
        lines.append(
            f"  • Total Threads:         {colors.BOLD}{sample.thread_count:5d}{colors.RESET}      "
            f"• Active Threads (>0%):  {colors.BOLD}{active_threads:5d}{colors.RESET}"
        )

    lines.append(divider)

    # Use requested top_threads directly (terminal height limiting was too aggressive)
    effective_top_threads = top_threads

    # Thread table
    active_count = sum(1 for t in sample.threads if t.single_core_pct > 0.0)
    lines.append(
        f"{colors.BOLD}THREAD BREAKDOWN:{colors.RESET} "
        f"{colors.DIM}({active_count} active, showing {min(effective_top_threads, len(sample.threads))}){colors.RESET}"
    )

    max_name_len = max(8, min(20, width - 56))
    lines.append(
        f"  {colors.DIM}{'TID':>8}  {'Role':<8} {'Thread Name':<{max_name_len}} {'1-Core %':>10} {'System %':>10} {'Total CPU':>11}  {'State'}{colors.RESET}"
    )
    lines.append(f"  {colors.DIM}{'─' * (width - 4)}{colors.RESET}")

    # Sort threads by importance: main > automation/service > audio > message > emulator > pooled > other
    THREAD_PRIORITY_PATTERNS = [
        (0, ("main",)),                              # Main thread
        (1, ("automation", "cli-")),                 # Automation/CLI services
        (2, ("miniaudio", "audio")),                 # Audio
        (3, ("message",)),                           # Message center
        (5, ("pooled",)),                            # Pooled workers (lower than emulator)
    ]
    CATEGORY_PRIORITIES = {"Emulator": 4, "Tile": 4}
    DEFAULT_PRIORITY = 6

    def thread_sort_key(t: ThreadSample) -> tuple:
        name_lower = t.name.lower()
        for priority, patterns in THREAD_PRIORITY_PATTERNS:
            if any(p in name_lower for p in patterns):
                return (priority, t.tid)
        if t.category in CATEGORY_PRIORITIES:
            return (CATEGORY_PRIORITIES[t.category], t.tid)
        return (DEFAULT_PRIORITY, t.tid)

    all_threads = sorted(sample.threads, key=thread_sort_key)
    active = [t for t in all_threads if t.single_core_pct > 0.0]
    idle = [t for t in all_threads if t.single_core_pct == 0.0]

    if show_all_threads:
        displayed_threads = (active + idle)[:effective_top_threads]
    else:
        # Show all active threads first, then fill remaining slots with idle threads
        displayed_threads = active[:effective_top_threads]
        remaining_slots = effective_top_threads - len(displayed_threads)
        if remaining_slots > 0:
            displayed_threads.extend(idle[:remaining_slots])

    if not displayed_threads:
        lines.append(f"  {colors.DIM}(no thread activity recorded in this sample window){colors.RESET}")
    else:
        for th in displayed_threads:
            st_color = colors.GREEN if th.state == "R" else colors.DIM
            cat_tag = f"[{th.category}]"
            th_name = th.name if len(th.name) <= max_name_len else th.name[:max(3, max_name_len - 3)] + "..."
            lines.append(
                f"  {th.tid:8d}  {colors.CYAN}{cat_tag:<8}{colors.RESET} "
                f"{colors.BOLD}{th_name:<{max_name_len}}{colors.RESET} "
                f"{colors.color_pct(th.single_core_pct):>10} "
                f"{th.system_pct:9.2f}% "
                f"{format_duration(th.total_cpu_seconds):>11}  "
                f"{st_color}{th.state}{colors.RESET}"
            )

    lines.append(divider)
    lines.append(
        f"{colors.DIM}Note: 'Resources' shows 'System %'. "
        f"True single-thread workload is '1-Core %'. (Ctrl+C to stop){colors.RESET}"
    )

    return "\n".join(lines)


def render_multi_process_dashboard(
    samples: List[ProcessSample],
    colors: Colors,
    num_cores: int,
    elapsed: float,
    term_width: int = 78,
    term_lines: int = 24
) -> str:
    lines: List[str] = []
    width = max(40, min(term_width - 1, 100))
    divider = "─" * width

    now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"{colors.BOLD}{colors.CYAN}┌{'─' * (width - 2)}┐{colors.RESET}")
    title = f" UNREAL-NG ECOSYSTEM WORKLOAD OVERVIEW (MULTI-PROCESS) "
    if len(title) > width - 4:
        title = title[:max(5, width - 7)] + "... "
    lines.append(f"{colors.BOLD}{colors.CYAN}│{colors.WHITE}{title.center(width - 2)}{colors.CYAN}│{colors.RESET}")
    lines.append(f"{colors.BOLD}{colors.CYAN}└{'─' * (width - 2)}┘{colors.RESET}")

    if width >= 75:
        lines.append(
            f" Host: {colors.BOLD}{platform.node()}{colors.RESET} | "
            f"Logical Cores: {colors.BOLD}{num_cores}{colors.RESET} | "
            f"Active Unreal Processes: {colors.BOLD}{len(samples)}{colors.RESET} | "
            f"Time: {now_str}"
        )
    else:
        lines.append(
            f" Host: {colors.BOLD}{platform.node()}{colors.RESET} | "
            f"Cores: {colors.BOLD}{num_cores}{colors.RESET} | "
            f"Procs: {colors.BOLD}{len(samples)}{colors.RESET}"
        )
    lines.append(divider)

    max_proc_len = max(8, min(18, width - 62))

    lines.append(
        f"  {colors.DIM}{'PID':>8}  {'Process':<{max_proc_len}} {'Role/Type':<12} {'Tiles':>6} {'1-Core %':>10} {'System %':>10} {'RSS (MB)':>10}{colors.RESET}"
    )
    lines.append(f"  {colors.DIM}{'─' * (width - 4)}{colors.RESET}")

    fixed_bottom_lines = 7
    available_proc_rows = max(1, term_lines - len(lines) - fixed_bottom_lines)
    displayed_samples = samples[:available_proc_rows]

    tot_single = sum(s.single_core_pct for s in samples)
    tot_system = sum(s.system_pct for s in samples)
    tot_rss = sum(s.rss_mb for s in samples)
    tot_tiles = sum(s.videowall.tile_count if s.videowall else 1 for s in samples)

    for s in displayed_samples:
        tiles = s.videowall.tile_count if s.videowall else 1
        role = "VideoWall" if ("videowal" in s.name.lower() or (s.videowall and s.videowall.is_videowall)) else "Emulator"
        p_name = s.name if len(s.name) <= max_proc_len else s.name[:max(3, max_proc_len - 3)] + "..."
        lines.append(
            f"  {s.pid:8d}  {colors.BOLD}{p_name:<{max_proc_len}}{colors.RESET} "
            f"{role:<12} "
            f"{tiles:6d} "
            f"{colors.color_pct(s.single_core_pct):>10} "
            f"{s.system_pct:9.2f}% "
            f"{s.rss_mb:9.1f}M"
        )

    if len(samples) > len(displayed_samples):
        hidden_count = len(samples) - len(displayed_samples)
        lines.append(f"  {colors.DIM}... ({hidden_count} more process(es) omitted to fit window){colors.RESET}")

    lines.append(f"  {colors.DIM}{'─' * (width - 4)}{colors.RESET}")
    lines.append(
        f"  {colors.BOLD}{'TOTAL CONCURRENT WORKLOAD:':<{max_proc_len + 23}} {tot_tiles:6d} "
        f"{colors.color_pct(tot_single):>10} "
        f"{tot_system:9.2f}% "
        f"{tot_rss:9.1f}M{colors.RESET}"
    )
    lines.append(divider)

    saturated_cores = tot_single / 100.0
    headroom = max(100.0 - tot_system, 0.0)
    lines.append(
        f"  ▶ {colors.BOLD}Cores Saturated:{colors.RESET} {colors.CYAN}{saturated_cores:.2f} of {num_cores} cores{colors.RESET} "
        f"| System Headroom: {colors.GREEN}{headroom:.1f}%{colors.RESET}"
    )
    lines.append(divider)
    lines.append(f"{colors.DIM}(Press Ctrl+C to stop){colors.RESET}")

    return "\n".join(lines)



# -----------------------------------------------------------------------------
# Main Loop and Handlers
# -----------------------------------------------------------------------------

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Cross-platform True Process Workload & Thread Profiler for Unreal-NG (unreal-qt, unreal-videowall, unreal-screen-viewer)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Target Selection Examples:
  # Auto-detect running Unreal process (unreal-qt or unreal-videowall)
  %(prog)s

  # Specifically target unreal-videowall
  %(prog)s --videowall
  %(prog)s -W

  # Specifically target unreal-qt
  %(prog)s --qt
  %(prog)s -Q

  # Specifically target unreal-screen-viewer
  %(prog)s --screen-viewer

  # Monitor ALL running Unreal processes simultaneously
  %(prog)s --all

  # Target specific PID with 0.5s interval and thread grouping
  %(prog)s --pid 12345 --interval 0.5 --group-threads

Output Mode Examples:
  # Take a single snapshot over 2 seconds and exit (for scripts/CI)
  %(prog)s --once --interval 2.0

  # Output JSON (includes videowall tile metrics)
  %(prog)s --json --once

  # Log timeseries metrics to a CSV file
  %(prog)s --csv workload-trace.csv
"""
    )
    parser.add_argument("-p", "--pid", type=int, default=None, help="Target process PID (default: auto-detect)")
    parser.add_argument("-n", "--name", type=str, default=None, help="Target process name regex")
    parser.add_argument("-W", "--videowall", action="store_true", help="Shortcut: target unreal-videowall")
    parser.add_argument("-Q", "--qt", action="store_true", help="Shortcut: target unreal-qt")
    parser.add_argument("-S", "--screen-viewer", action="store_true", help="Shortcut: target unreal-screen-viewer")
    parser.add_argument("-A", "--all", "--multi", dest="multi_mode", action="store_true", help="Monitor all active Unreal-NG processes simultaneously")
    parser.add_argument("-g", "--group-threads", action="store_true", help="Group threads by role (Tile, UI, Service, Worker)")
    parser.add_argument("-i", "--interval", type=float, default=1.0, help="Sampling interval in seconds (default: 1.0)")
    parser.add_argument("-c", "--count", type=int, default=0, help="Number of samples to collect before exit (0 = infinite)")
    parser.add_argument("-1", "--once", action="store_true", help="Take a single sample and exit (snapshot mode)")
    parser.add_argument("-t", "--top", type=int, default=15, help="Number of active threads to display (default: 15)")
    parser.add_argument("--show-all-threads", action="store_true", help="Display all threads including 0%% idle threads")
    parser.add_argument("--wait", action="store_true", help="Wait for target process if not currently running")
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI color codes")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")
    parser.add_argument("--csv", type=str, default=None, help="Save timeseries metrics to specified CSV file")
    parser.add_argument("--version", action="version", version="process-workload.py 1.1.0")
    return parser.parse_args()


def write_csv_header(csv_path: str) -> None:
    if not os.path.exists(csv_path) or os.path.getsize(csv_path) == 0:
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                "timestamp", "pid", "name", "elapsed_s", "cpu_time_s",
                "single_core_pct", "system_pct", "cores_equivalent",
                "rss_mb", "vms_mb", "threads_count", "tiles_count",
                "avg_tile_cpu_pct", "rendering_cpu_pct"
            ])


def append_csv_row(csv_path: str, sample: ProcessSample) -> None:
    vw = sample.videowall
    tiles = vw.tile_count if vw else 1
    avg_tile = vw.avg_per_tile_pct if vw else sample.single_core_pct
    rend = vw.rendering_single_core_pct if vw else 0.0

    with open(csv_path, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            datetime.datetime.fromtimestamp(sample.timestamp).isoformat(),
            sample.pid,
            sample.name,
            f"{sample.elapsed_seconds:.3f}",
            f"{sample.cpu_time_consumed:.3f}",
            f"{sample.single_core_pct:.2f}",
            f"{sample.system_pct:.2f}",
            f"{sample.cores_equivalent:.3f}",
            f"{sample.rss_mb:.2f}",
            f"{sample.vms_mb:.2f}",
            sample.thread_count,
            tiles,
            f"{avg_tile:.2f}",
            f"{rend:.2f}"
        ])


def run_multi_mode(args: argparse.Namespace, num_cores: int, colors: Colors) -> int:
    """Monitors all active Unreal-NG processes in parallel."""
    is_terminal = sys.stdout.isatty() and not args.json and not args.once
    sample_num = 0
    last_term_size: Optional[Tuple[int, int]] = None

    with TerminalScreenManager(enabled=is_terminal):
        try:
            while True:
                procs = find_all_unreal_processes()
                if not procs:
                    if not args.wait:
                        if not is_terminal:
                            print("Error: No running Unreal-NG processes found (unreal-qt, unreal-videowall, etc.)", file=sys.stderr)
                        return 1
                    time.sleep(1.0)
                    continue

                # Sample each collector
                collectors = [create_collector(pid, num_cores) for pid, _ in procs]
                # Parallel sampling via interval sleep
                t1 = time.time()
                samples: List[ProcessSample] = []
                for col in collectors:
                    s = col.sample(args.interval / max(len(collectors), 1))
                    if s:
                        samples.append(s)
                t2 = time.time()

                sample_num += 1

                if args.json:
                    data = [asdict(s) for s in samples]
                    print(json.dumps(data, indent=2 if args.once else None))
                else:
                    curr_term_size = shutil.get_terminal_size((80, 24))
                    global _terminal_resized
                    size_changed = (curr_term_size != last_term_size) or _terminal_resized
                    if size_changed:
                        _terminal_resized = False
                        last_term_size = curr_term_size

                    term_columns, term_lines = curr_term_size
                    out = render_multi_process_dashboard(
                        samples, colors, num_cores, t2 - t1,
                        term_width=term_columns, term_lines=term_lines
                    )
                    if is_terminal:
                        draw_terminal_screen(out, term_columns, term_lines, force_clear=size_changed)
                    else:
                        print(out)
                        sys.stdout.flush()

                if args.count > 0 and sample_num >= args.count:
                    break

        except KeyboardInterrupt:
            pass

    if is_terminal and not args.json:
        print("Monitoring stopped by user.")

    return 0


def main() -> int:
    args = parse_arguments()

    if args.interval <= 0:
        print("Error: --interval must be > 0", file=sys.stderr)
        return 1

    if args.once:
        args.count = 1

    num_cores = os.cpu_count() or 1
    colors = Colors(enabled=sys.stdout.isatty() and not args.no_color and not args.json)

    # Multi-process mode
    if args.multi_mode:
        return run_multi_mode(args, num_cores, colors)

    # Shortcut flags resolution
    target_name = args.name
    if args.videowall:
        target_name = "unreal-videowall"
    elif args.qt:
        target_name = "unreal-qt"
    elif args.screen_viewer:
        target_name = "unreal-screen-viewer"

    target_pid = args.pid

    if target_pid is None:
        while True:
            detected = auto_detect_target(target_name)
            if detected:
                target_pid, detected_name = detected
                break
            if not args.wait:
                name_msg = f"matching '{target_name}'" if target_name else "from known candidates (unreal-qt, unreal-videowall, etc.)"
                print(
                    f"Error: No active Unreal-NG process found {name_msg}.\n"
                    f"Provide --pid <PID>, start the application, or pass --wait to wait for launch.",
                    file=sys.stderr
                )
                return 1
            print(f"Waiting for process {target_name or 'Unreal-NG'} to launch...", file=sys.stderr)
            time.sleep(1.0)

    try:
        collector = create_collector(target_pid, num_cores)
    except Exception as e:
        print(f"Error creating process collector: {e}", file=sys.stderr)
        return 1

    if not collector.is_alive():
        print(f"Error: Process with PID {target_pid} is not running or accessible.", file=sys.stderr)
        return 1

    if args.csv:
        try:
            write_csv_header(args.csv)
        except Exception as e:
            print(f"Error initializing CSV file '{args.csv}': {e}", file=sys.stderr)
            return 1

    history: List[float] = []
    start_time = time.time()
    sample_num = 0

    is_terminal = sys.stdout.isatty() and not args.json and not args.once
    last_term_size: Optional[Tuple[int, int]] = None

    with TerminalScreenManager(enabled=is_terminal):
        try:
            while True:
                if not collector.is_alive():
                    break

                sample = collector.sample(args.interval)
                if sample is None:
                    break

                sample_num += 1
                history.append(sample.single_core_pct)

                if args.csv:
                    append_csv_row(args.csv, sample)

                if args.json:
                    data = asdict(sample)
                    data["threads"] = [asdict(t) for t in sample.threads]
                    if sample.videowall:
                        data["videowall"] = asdict(sample.videowall)
                    print(json.dumps(data, indent=2 if args.once else None))
                else:
                    curr_term_size = shutil.get_terminal_size((80, 24))
                    global _terminal_resized
                    size_changed = (curr_term_size != last_term_size) or _terminal_resized
                    if size_changed:
                        _terminal_resized = False
                        last_term_size = curr_term_size

                    term_columns, term_lines = curr_term_size
                    dashboard = render_dashboard(
                        sample=sample,
                        top_threads=args.top,
                        show_all_threads=args.show_all_threads,
                        group_threads=args.group_threads,
                        colors=colors,
                        history=history,
                        start_time=start_time,
                        term_width=term_columns,
                        term_lines=term_lines
                    )

                    if is_terminal:
                        draw_terminal_screen(dashboard, term_columns, term_lines, force_clear=size_changed)
                    else:
                        print(dashboard)
                        sys.stdout.flush()

                if args.count > 0 and sample_num >= args.count:
                    break

        except KeyboardInterrupt:
            pass

    # Print summary if ran multiple samples interactively
    if not args.json:
        if not collector.is_alive():
            print(f"\n{colors.YELLOW}Process {target_pid} ({collector.process_name()}) terminated.{colors.RESET}")
        if len(history) > 1:
            avg_pct = sum(history) / len(history)
            min_pct = min(history)
            max_pct = max(history)
            print(f"\n{colors.BOLD}Session Summary ({len(history)} samples over {time.time() - start_time:.1f}s):{colors.RESET}")
            print(f"  • Single-Core CPU %: Avg={avg_pct:.2f}%, Min={min_pct:.2f}%, Max={max_pct:.2f}%")
            print(f"  • System All-Cores %: Avg={avg_pct/num_cores:.2f}%, Min={min_pct/num_cores:.2f}%, Max={max_pct/num_cores:.2f}%")

    return 0


if __name__ == "__main__":
    sys.exit(main())
