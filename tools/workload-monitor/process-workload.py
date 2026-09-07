#!/usr/bin/env python3
"""
True Process Workload & Thread Profiler for Unreal-NG

Calculates and displays the true real-time workload of a single process (such as
unreal-qt, unreal-screen-viewer, or unreal-videowall).

Highlights both:
1. Single-Core Equivalent % (where 100% = 1 full CPU core saturated)
2. All-Cores / System Monitor % (normalized across all logical cores, matching
   system monitors like GNOME Resources, KDE System Monitor, or Task Manager)

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

DEFAULT_TARGET_NAMES = ["unreal-qt", "unreal-screen-viewer", "unreal-videowall"]


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
        """Returns (utime_ticks, stime_ticks, rss_bytes, vms_bytes, num_threads)"""
        try:
            with open(f"/proc/{self.pid}/stat", "r", encoding="utf-8") as f:
                content = f.read()
            # comm is inside parentheses and can contain spaces/parentheses
            r_idx = content.rfind(")")
            if r_idx == -1:
                return None
            fields = content[r_idx + 2:].split()
            # fields:
            # 0: state
            # 1: ppid
            # 2: pgrp
            # 3: session
            # 4: tty_nr
            # 5: tpgid
            # 6: flags
            # 7: minflt
            # 8: cminflt
            # 9: majflt
            # 10: cmajflt
            # 11: utime (field 13 in 0-indexed overall stat)
            # 12: stime (field 14 in 0-indexed overall stat)
            # 17: num_threads (field 19)
            # 20: vsize (field 22)
            # 21: rss in pages (field 23)
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
        """Returns dict of tid -> (comm, utime_ticks, stime_ticks, state)"""
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

            thread_samples.append(ThreadSample(
                tid=tid,
                name=comm,
                single_core_pct=th_single_core,
                system_pct=th_sys,
                total_cpu_seconds=total_sec,
                state=state
            ))

        thread_samples.sort(key=lambda t: t.single_core_pct, reverse=True)

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
            threads=thread_samples
        )


class PsutilCollector(BaseCollector):
    """Cross-platform collector using psutil (Windows, macOS, Linux)."""

    def __init__(self, pid: int, num_cores: int):
        super().__init__(pid, num_cores)
        self.proc = psutil.Process(pid)
        self._name = self.proc.name()

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
            # Thread times if available
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

            # Threads
            thread_samples: List[ThreadSample] = []
            try:
                for th in self.proc.threads():
                    prev_time = threads_before.get(th.id, th.user_time + th.system_time)
                    th_cpu_time = max((th.user_time + th.system_time) - prev_time, 0.0)
                    th_single_core = (th_cpu_time / elapsed) * 100.0
                    th_sys = th_single_core / self.num_cores
                    thread_samples.append(ThreadSample(
                        tid=th.id,
                        name=f"thread-{th.id}",
                        single_core_pct=th_single_core,
                        system_pct=th_sys,
                        total_cpu_seconds=th.user_time + th.system_time,
                        state="R" if th_single_core > 0.1 else "S"
                    ))
            except Exception:
                pass

            thread_samples.sort(key=lambda t: t.single_core_pct, reverse=True)

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
                threads=thread_samples
            )
        except Exception:
            return None


def create_collector(pid: int, num_cores: int) -> BaseCollector:
    # On Linux, LinuxProcCollector is native, reads accurate thread names from /proc, and has no dependencies
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
# Process Discovery
# -----------------------------------------------------------------------------

def find_processes_by_name(pattern: str) -> List[Tuple[int, str]]:
    matches: List[Tuple[int, str]] = []

    # Try psutil first if present
    if HAS_PSUTIL:
        try:
            for p in psutil.process_iter(["pid", "name", "cmdline"]):
                name = p.info.get("name") or ""
                cmdline = " ".join(p.info.get("cmdline") or [])
                if re.search(pattern, name, re.IGNORECASE) or re.search(pattern, cmdline, re.IGNORECASE):
                    matches.append((p.info["pid"], name))
            return matches
        except Exception:
            pass

    # Linux /proc scan
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

                cmdline = ""
                cmdline_path = f"/proc/{pid}/cmdline"
                if os.path.exists(cmdline_path):
                    with open(cmdline_path, "rb") as f:
                        cmdline = f.read().replace(b"\x00", b" ").decode("utf-8", "replace").strip()

                target_str = f"{comm} {cmdline}"
                if re.search(pattern, target_str, re.IGNORECASE):
                    matches.append((pid, comm if comm else f"pid-{pid}"))
            except Exception:
                continue
        return matches

    # macOS / Unix fallback using 'ps'
    if platform.system() in ("Darwin", "FreeBSD"):
        try:
            import subprocess
            out = subprocess.check_output(["ps", "-ax", "-o", "pid,command"], text=True)
            for line in out.splitlines()[1:]:
                parts = line.strip().split(None, 1)
                if len(parts) == 2:
                    p_pid, p_cmd = int(parts[0]), parts[1]
                    if re.search(pattern, p_cmd, re.IGNORECASE):
                        matches.append((p_pid, os.path.basename(p_cmd.split()[0])))
            return matches
        except Exception:
            pass

    # Windows fallback using 'tasklist'
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


def auto_detect_target(preferred_name: Optional[str] = None) -> Optional[Tuple[int, str]]:
    candidates = [preferred_name] if preferred_name else DEFAULT_TARGET_NAMES

    for cand in candidates:
        if not cand:
            continue
        found = find_processes_by_name(f"^{re.escape(cand)}(\\.exe)?$")
        if not found:
            # Partial match
            found = find_processes_by_name(re.escape(cand))
        if found:
            # Sort by PID descending (most recent first)
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
    colors: Colors,
    history: List[float],
    start_time: float
) -> str:
    lines: List[str] = []
    width = 78
    divider = "─" * width

    # History metrics
    avg_single = sum(history) / len(history) if history else sample.single_core_pct
    min_single = min(history) if history else sample.single_core_pct
    max_single = max(history) if history else sample.single_core_pct
    session_duration = time.time() - start_time

    # Header
    now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"{colors.BOLD}{colors.CYAN}┌{'─' * (width - 2)}┐{colors.RESET}")
    title = f" TRUE WORKLOAD PROFILER :: {sample.name.upper()} (PID {sample.pid}) "
    lines.append(f"{colors.BOLD}{colors.CYAN}│{colors.WHITE}{title.center(width - 2)}{colors.CYAN}│{colors.RESET}")
    lines.append(f"{colors.BOLD}{colors.CYAN}└{'─' * (width - 2)}┘{colors.RESET}")

    lines.append(
        f" Host: {colors.BOLD}{platform.node()}{colors.RESET} | "
        f"OS: {platform.system()} {platform.release()} | "
        f"Logical Cores: {colors.BOLD}{sample.num_cores}{colors.RESET} | "
        f"Time: {now_str}"
    )
    lines.append(divider)

    # Core comparison callout
    lines.append(f"{colors.BOLD}CPU WORKLOAD BREAKDOWN:{colors.RESET}")
    single_bar = make_bar(sample.single_core_pct, 100.0, 20)
    sys_bar = make_bar(sample.system_pct, 100.0, 20)

    lines.append(
        f"  ▶ {colors.BOLD}Single-Core Load:{colors.RESET}     "
        f"{colors.color_pct(sample.single_core_pct)} {single_bar} "
        f"{colors.DIM}(1 core = 100.0%){colors.RESET}"
    )
    lines.append(
        f"  ▶ {colors.BOLD}All-Cores Equivalent:{colors.RESET} "
        f"{colors.color_pct(sample.system_pct)} {sys_bar} "
        f"{colors.DIM}(normalized across all {sample.num_cores} cores){colors.RESET}"
    )
    lines.append(
        f"  ▶ {colors.BOLD}Cores Saturated:{colors.RESET}      "
        f"{colors.CYAN}{sample.cores_equivalent:5.2f} core(s){colors.RESET} "
        f"{colors.DIM}| Sample interval: {sample.elapsed_seconds:.2f}s | CPU Time: {sample.cpu_time_consumed:.3f}s{colors.RESET}"
    )

    lines.append(
        f"  ▶ {colors.BOLD}CPU Mode Splits:{colors.RESET}      "
        f"User: {colors.CYAN}{sample.user_time_consumed:.3f}s{colors.RESET} | "
        f"Kernel/System: {colors.YELLOW}{sample.system_time_consumed:.3f}s{colors.RESET}"
    )
    lines.append(
        f"  ▶ {colors.BOLD}Session Statistics:{colors.RESET}   "
        f"Avg: {colors.color_pct(avg_single)} | "
        f"Min: {colors.color_pct(min_single)} | "
        f"Max: {colors.color_pct(max_single)} | "
        f"Duration: {format_duration(session_duration)}"
    )

    lines.append(divider)

    # Memory & Process Health
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

    # Thread table
    lines.append(
        f"{colors.BOLD}THREAD BREAKDOWN (sorted by CPU load):{colors.RESET} "
        f"{colors.DIM}(Top {top_threads} active threads){colors.RESET}"
    )
    lines.append(
        f"  {colors.DIM}{'TID':>8}  {'Thread Name / Role':<24} {'1-Core %':>10} {'System %':>10} {'Total CPU':>11}  {'State'}{colors.RESET}"
    )
    lines.append(f"  {colors.DIM}{'─' * 74}{colors.RESET}")

    displayed_threads = sample.threads if show_all_threads else [t for t in sample.threads if t.single_core_pct > 0.01 or show_all_threads]
    if not displayed_threads and sample.threads:
        displayed_threads = sample.threads[:top_threads]
    else:
        displayed_threads = displayed_threads[:top_threads]

    if not displayed_threads:
        lines.append(f"  {colors.DIM}(no thread activity recorded in this sample window){colors.RESET}")
    else:
        for th in displayed_threads:
            st_color = colors.GREEN if th.state == "R" else colors.DIM
            th_name = th.name if len(th.name) <= 24 else th.name[:21] + "..."
            lines.append(
                f"  {th.tid:8d}  {colors.BOLD}{th_name:<24}{colors.RESET} "
                f"{colors.color_pct(th.single_core_pct):>10} "
                f"{th.system_pct:9.2f}% "
                f"{format_duration(th.total_cpu_seconds):>11}  "
                f"{st_color}{th.state}{colors.RESET}"
            )

    lines.append(divider)
    lines.append(
        f"{colors.DIM}Note: 'Resources' and Task Manager show 'System %'. "
        f"True single-thread workload is '1-Core %'. (Ctrl+C to stop){colors.RESET}"
    )

    return "\n".join(lines)


# -----------------------------------------------------------------------------
# Main Loop and Handlers
# -----------------------------------------------------------------------------

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Cross-platform True Process Workload & Thread Profiler for Unreal-NG",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-detect running unreal-qt and monitor interactively
  %(prog)s

  # Monitor specific PID with 0.5s interval
  %(prog)s --pid 12345 --interval 0.5

  # Monitor by process name
  %(prog)s --name unreal-screen-viewer

  # Take a single snapshot over 2 seconds and exit (for scripts/CI)
  %(prog)s --once --interval 2.0

  # Output JSON for automated performance benchmarks
  %(prog)s --json --once

  # Log timeseries metrics to a CSV file
  %(prog)s --csv workload-trace.csv
"""
    )
    parser.add_argument("-p", "--pid", type=int, default=None, help="Target process PID (default: auto-detect)")
    parser.add_argument("-n", "--name", type=str, default=None, help="Target process name regex (default: unreal-qt)")
    parser.add_argument("-i", "--interval", type=float, default=1.0, help="Sampling interval in seconds (default: 1.0)")
    parser.add_argument("-c", "--count", type=int, default=0, help="Number of samples to collect before exit (0 = infinite)")
    parser.add_argument("-1", "--once", action="store_true", help="Take a single sample and exit (snapshot mode)")
    parser.add_argument("-t", "--top", type=int, default=15, help="Number of active threads to display (default: 15)")
    parser.add_argument("--show-all-threads", action="store_true", help="Display all threads including 0%% idle threads")
    parser.add_argument("--wait", action="store_true", help="Wait for target process if not currently running")
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI color codes")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")
    parser.add_argument("--csv", type=str, default=None, help="Save timeseries metrics to specified CSV file")
    parser.add_argument("--version", action="version", version="process-workload.py 1.0.0")
    return parser.parse_args()


def write_csv_header(csv_path: str) -> None:
    if not os.path.exists(csv_path) or os.path.getsize(csv_path) == 0:
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                "timestamp", "pid", "name", "elapsed_s", "cpu_time_s",
                "single_core_pct", "system_pct", "cores_equivalent",
                "rss_mb", "vms_mb", "threads_count"
            ])


def append_csv_row(csv_path: str, sample: ProcessSample) -> None:
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
            sample.thread_count
        ])


def main() -> int:
    args = parse_arguments()

    if args.interval <= 0:
        print("Error: --interval must be > 0", file=sys.stderr)
        return 1

    if args.once:
        args.count = 1

    # Detect cores
    num_cores = os.cpu_count() or 1

    # Target resolution
    target_pid = args.pid
    target_name = args.name

    if target_pid is None:
        while True:
            detected = auto_detect_target(target_name)
            if detected:
                target_pid, target_name = detected
                break
            if not args.wait:
                name_msg = f"matching '{target_name}'" if target_name else "from known candidates (unreal-qt, etc.)"
                print(
                    f"Error: No active Unreal-NG process found {name_msg}.\n"
                    f"Provide --pid <PID>, start unreal-qt, or pass --wait to wait for launch.",
                    file=sys.stderr
                )
                return 1
            print(f"Waiting for process {target_name or 'unreal-qt'} to launch...", file=sys.stderr)
            time.sleep(1.0)

    try:
        collector = create_collector(target_pid, num_cores)
    except Exception as e:
        print(f"Error creating process collector: {e}", file=sys.stderr)
        return 1

    if not collector.is_alive():
        print(f"Error: Process with PID {target_pid} is not running or accessible.", file=sys.stderr)
        return 1

    colors = Colors(enabled=sys.stdout.isatty() and not args.no_color and not args.json)

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

    try:
        while True:
            if not collector.is_alive():
                if not args.json:
                    print(f"\n{colors.YELLOW}Process {target_pid} ({collector.process_name()}) terminated.{colors.RESET}")
                break

            sample = collector.sample(args.interval)
            if sample is None:
                if not args.json:
                    print(f"\n{colors.YELLOW}Process {target_pid} terminated during sampling.{colors.RESET}")
                break

            sample_num += 1
            history.append(sample.single_core_pct)

            if args.csv:
                append_csv_row(args.csv, sample)

            if args.json:
                data = asdict(sample)
                data["threads"] = [asdict(t) for t in sample.threads]
                print(json.dumps(data, indent=2 if args.once else None))
            else:
                dashboard = render_dashboard(
                    sample=sample,
                    top_threads=args.top,
                    show_all_threads=args.show_all_threads,
                    colors=colors,
                    history=history,
                    start_time=start_time
                )

                if is_terminal:
                    # Clear screen and move cursor to top-left
                    sys.stdout.write("\033[2J\033[H")
                print(dashboard)
                sys.stdout.flush()

            if args.count > 0 and sample_num >= args.count:
                break

    except KeyboardInterrupt:
        if not args.json:
            print("\nMonitoring stopped by user.")

    # Print summary if ran multiple samples interactively
    if not args.json and len(history) > 1:
        avg_pct = sum(history) / len(history)
        min_pct = min(history)
        max_pct = max(history)
        print(f"\n{colors.BOLD}Session Summary ({len(history)} samples over {time.time() - start_time:.1f}s):{colors.RESET}")
        print(f"  • Single-Core CPU %: Avg={avg_pct:.2f}%, Min={min_pct:.2f}%, Max={max_pct:.2f}%")
        print(f"  • System All-Cores %: Avg={avg_pct/num_cores:.2f}%, Min={min_pct/num_cores:.2f}%, Max={max_pct/num_cores:.2f}%")

    return 0


if __name__ == "__main__":
    sys.exit(main())
