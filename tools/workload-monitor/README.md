# Unreal-NG Process Workload & Thread Profiler

A cross-platform Python utility designed to measure and display the **true workload** of individual Unreal-NG processes (`unreal-qt`, `unreal-screen-viewer`, `unreal-videowall`, or any custom PID).

---

## The Problem: Why Does "Resources" Display Only 0.7%?

When running `unreal-qt`, modern OS resource monitors (like GNOME *Resources*, GNOME *System Monitor*, or Windows *Task Manager*) often report what looks like an impossibly low CPU usage—typically **0.7% to 1.0%**.

This discrepancy occurs because system monitors report **all-cores normalized CPU utilization**:

$$\text{Reported CPU \%} = \frac{\Delta\text{Process CPU Time}}{\Delta\text{Wall Time} \times \text{Total Logical Cores}} \times 100\%$$

On a modern multi-core system (e.g., an 8-core / 16-thread CPU):
- **100% of 1 full core** = $\frac{100\%}{16} = \mathbf{6.25\%}$ total system capacity.
- An emulator utilizing **~12% of a single CPU core** will display as:
  $$\frac{12\%}{16} \approx \mathbf{0.75\%}$$

### Developer Perspective vs. System Monitor

| Metric | System Monitor (Resources) | True Single-Core Metric (`process-workload.py`) |
|---|---|---|
| **Formula** | Divided by all $N$ CPU cores | Normalized to 1 CPU core = 100% |
| **On 16 cores (12% of 1 core)** | `0.75%` | `12.00%` |
| **Meaning** | How much of the entire computer is busy | How heavily the process's active thread is loaded |
| **Target Audience** | General desktop users | Developers optimizing emulation loops & latency |

`process-workload.py` bridges this gap by displaying **both metrics side-by-side**, alongside real-time **per-thread execution profiling**.

---

## Features

- **True Single-Core Workload**: 100% corresponds to 1 CPU core fully saturated.
- **System-Wide Workload**: Accurately reproduces what GNOME Resources / Task Manager displays.
- **Cores Saturated**: Exact decimal capacity (e.g., `0.13 core(s)` or `1.25 core(s)`).
- **Per-Thread Breakdown**: Lists active threads sorted by CPU consumption, showing thread names (e.g., emulation worker, Qt UI event loop, audio stream, WebAPI I/O loops), individual CPU %, thread state, and accumulated CPU time.
- **Process Memory & Health**: Live Resident Set Size (RSS), Virtual Memory (VMS), total threads, and active threads count.
- **Session Statistics**: Live Min, Max, and Average CPU load tracking over time.
- **Auto-Detection**: Automatically detects running instances of `unreal-qt`, `unreal-screen-viewer`, or `unreal-videowall`.
- **Zero Dependencies on Linux**: Reads native `/proc/[pid]/` entries with standard Python 3.
- **Cross-Platform**: Works on Linux, macOS, and Windows (uses optional `psutil` where available).
- **Flexible Modes**:
  - Interactive live terminal dashboard with color gauges and auto-refresh.
  - Single-shot snapshot mode (`--once`) for quick checks or automated scripts.
  - Structured JSON output (`--json`) for automated profiling / CI test runs.
  - Continuous CSV timeseries logging (`--csv <file>`).

---

## Installation & Prerequisites

### Linux
**Zero external dependencies required.** Runs directly with Python 3.7+ out of the box:
```bash
python3 tools/workload-monitor/process-workload.py
```

### macOS & Windows
Requires the optional `psutil` library:
```bash
pip install -r tools/workload-monitor/requirements.txt
# or directly:
pip install psutil
```

---

## Usage Guide

### 1. Auto-Detect Running Emulator (Interactive Dashboard)
Simply run the script with `unreal-qt` already running:
```bash
python3 tools/workload-monitor/process-workload.py
```

### 2. Monitor a Specific Process Name or PID
```bash
# Target by name
python3 tools/workload-monitor/process-workload.py --name unreal-videowall

# Target by specific PID
python3 tools/workload-monitor/process-workload.py --pid 295051
```

### 3. Change Refresh Rate
```bash
# Refresh every 500 ms (default is 1.0s)
python3 tools/workload-monitor/process-workload.py --interval 0.5
```

### 4. Single-Shot Snapshot Mode (CLI / Scripting)
Takes a measurement over the specified interval and prints a clean report, then exits:
```bash
python3 tools/workload-monitor/process-workload.py --once --interval 1.0
```

### 5. Wait for Process Launch
If you want to start the profiler before launching the emulator:
```bash
python3 tools/workload-monitor/process-workload.py --name unreal-qt --wait
```

### 6. Export Timeseries to CSV
Logs every sample interval to a CSV file for performance analysis:
```bash
python3 tools/workload-monitor/process-workload.py --csv scratch/workload-trace.csv --count 60
```

### 7. Automated Benchmarks / JSON Mode
Outputs machine-readable JSON for integration with benchmark scripts:
```bash
python3 tools/workload-monitor/process-workload.py --json --once
```

---

## Sample Dashboard Output

```text
┌────────────────────────────────────────────────────────────────────────────┐
│              TRUE WORKLOAD PROFILER :: UNREAL-QT (PID 295051)              │
└────────────────────────────────────────────────────────────────────────────┘
 Host: dev-Precision-5560 | OS: Linux 7.0.0-31-generic | Logical Cores: 16 | Time: 2026-09-07 14:04:31
──────────────────────────────────────────────────────────────────────────────
CPU WORKLOAD BREAKDOWN:
  ▶ Single-Core Load:      13.00% [███░░░░░░░░░░░░░░░░░] (1 core = 100.0%)
  ▶ All-Cores Equivalent:   0.81% [░░░░░░░░░░░░░░░░░░░░] (normalized across all 16 cores)
  ▶ Cores Saturated:       0.13 core(s) | Sample interval: 1.00s | CPU Time: 0.130s
  ▶ CPU Mode Splits:      User: 0.110s | Kernel/System: 0.020s
  ▶ Session Statistics:   Avg:  12.00% | Min:  11.00% | Max:  13.00% | Duration:  2.00s
──────────────────────────────────────────────────────────────────────────────
MEMORY & PROCESS HEALTH:
  • Resident Memory (RSS):   110.12 MB   • Virtual Memory (VMS):  2235.46 MB
  • Total Threads:            26      • Active Threads (>0%):      8
──────────────────────────────────────────────────────────────────────────────
THREAD BREAKDOWN (sorted by CPU load): (Top 15 active threads)
       TID  Thread Name / Role         1-Core %   System %   Total CPU  State
  ──────────────────────────────────────────────────────────────────────────
    295121  unreal-qt                  5.00%      0.31%      35.24s  S
    295051  unreal-qt                  3.00%      0.19%      13.63s  S
    295060  unreal-qt                  2.00%      0.12%       8.49s  S
    295061  unreal-qt                  1.00%      0.06%       1.23s  S
    295066  Thread (pooled)            1.00%      0.06%       2.50s  S
    295067  Thread (pooled)            1.00%      0.06%       2.51s  S
    295068  Thread (pooled)            1.00%      0.06%       2.53s  S
    295070  Thread (pooled)            1.00%      0.06%       2.53s  S
──────────────────────────────────────────────────────────────────────────────
Note: 'Resources' and Task Manager show 'System %'. True single-thread workload is '1-Core %'. (Ctrl+C to stop)
```

---

## Understanding Unreal-NG Threads

When monitoring `unreal-qt`, you will see several distinct thread categories:

1. **Emulation Worker Thread (`MainLoop::Run`)**:
   - Typically consumes **~5% to 7% of 1 core**.
   - Executes ~71,680 Z80 T-states, scanline rendering, and audio AY/Beeper synthesis in **~1.2 ms**.
   - Then enters `TimeHelper::WaitUntilPrecise()` and sleeps for the remaining **~19 ms** until the next 50 Hz frame deadline.
2. **Main GUI Thread (Process PID)**:
   - Typically consumes **~2% to 3% of 1 core**.
   - Handles Qt event processing, keyboard/mouse input, and window repaint updates.
3. **Qt Worker / Render Thread Pool (`Thread (pooled)`)**:
   - Typically consumes **~1% to 3% of 1 core combined**.
   - Handles screen buffer scaling, OpenGL presentation, and async task dispatching.
4. **Background Service Threads**:
   - `DrogonIoLoop`: HTTP WebAPI server listening for automation calls.
   - `miniaudio`: Real-time audio DAC ring-buffer polling.
   - `AsyncFileLogger`: Disk-flush logger worker.
   - `automation_lua`: Lua scripting engine thread.
   - These background threads consume **< 0.1% CPU** when idle.

### Normal Mode vs. Turbo Mode Workload
- **Normal Mode (50 FPS)**: `~12%` single-core total (~0.75% on 16 cores) due to frame pacing sleeps.
- **Turbo Mode (`config.turbo_mode`)**: Frame pacing is disabled. The emulation thread runs unthrottled and will consume **`100%` of 1 core** (~6.25% on 16 cores).

---

## Command-Line Options

| Option | Short | Default | Description |
|---|---|---|---|
| `--pid PID` | `-p` | Auto-detect | Target process PID |
| `--name REGEX` | `-n` | `unreal-qt` | Target process name regex pattern |
| `--interval SEC` | `-i` | `1.0` | Sampling interval duration in seconds |
| `--count N` | `-c` | `0` (infinite) | Number of samples to collect before exiting |
| `--once` | `-1` | `False` | Collect 1 sample and exit (snapshot mode) |
| `--top N` | `-t` | `15` | Maximum number of active threads to display in table |
| `--show-all-threads` | | `False` | Show all threads including idle (0.0% CPU) threads |
| `--wait` | | `False` | Wait until the target process appears if not running |
| `--no-color` | | `False` | Disable ANSI terminal color escape codes |
| `--json` | | `False` | Output JSON formatted metrics (per interval) |
| `--csv FILE` | | `None` | Append timeseries metrics to a CSV file |
| `--help` | `-h` | | Show help message and exit |
| `--version` | | | Show program version |
