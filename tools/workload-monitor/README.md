# Unreal-NG Process Workload & Thread Profiler

A cross-platform Python utility designed to measure and display the **true workload** of individual or multi-instance Unreal-NG processes:
- `unreal-qt` (Desktop emulator UI)
- `unreal-videowall` (Multi-tile grid video wall)
- `unreal-screen-viewer` (Standalone screen presentation)
- Any custom process PID

---

## The Problem: Why Does "Resources" Display Only 0.7%?

When running `unreal-qt` or single-tile `unreal-videowall`, modern OS resource monitors (like GNOME *Resources*, GNOME *System Monitor*, or Windows *Task Manager*) often report what looks like an impossibly low CPU usage—typically **0.7% to 1.0%**.

This occurs because system monitors report **all-cores normalized CPU utilization**:

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

- **True Single-Core Workload**: 100% corresponds to 1 CPU core fully saturated (developer & `ps` standard).
- **System-Wide Workload**: Accurately reproduces what GNOME Resources / Task Manager displays.
- **Cores Saturated**: Exact decimal capacity (e.g., `0.13 core(s)` or `2.40 core(s)`).
- **VideoWall Grid & Tile Analytics**:
  - 🔲 **Active Tiles Counter**: Automatically detects active emulator tile worker threads.
  - ⚡ **Aggregate Emulation CPU**: Total CPU consumed by all running emulator instances.
  - 📊 **Per-Tile Load Distribution**: Average, minimum, and maximum CPU consumption across tiles to catch lagging/stalled emulators.
  - 🖥️ **Grid & UI Rendering Overhead**: Isolates video wall texture presentation and Qt layout cost from core emulation.
  - 🚀 **Sustainable Capacity Forecast**: Projects how many concurrent 50 FPS tiles your host CPU can sustain under an 85% safety ceiling.
- **Multi-Process Mode (`--all`)**: Live overview of all running Unreal-NG processes in the ecosystem (`unreal-qt`, `unreal-videowall`, `unreal-screen-viewer`) with aggregate saturation and memory usage.
- **Per-Thread Role Categorization (`--group-threads`)**: Automatically categorizes threads into `[Tile]` (emulation cores), `[UI]` (main window & input), `[Worker]` (thread pool), and `[Service]` (WebAPI, audio DAC, logger).
- **Process Memory & Health**: Live Resident Set Size (RSS), Virtual Memory (VMS), total threads, and active thread counts.
- **Session Tracking**: Live Min, Max, and Average CPU load over time with summary on exit.
- **Zero Dependencies on Linux**: Reads native `/proc/[pid]/` entries with standard Python 3.
- **Cross-Platform**: Works on Linux, macOS, and Windows (uses optional `psutil` where available).
- **Flexible Modes**:
  - Interactive live terminal dashboard with color gauges and auto-refresh.
  - Single-shot snapshot mode (`--once`) for quick checks or automated scripts.
  - Structured JSON output (`--json`) with full VideoWall tile metadata.
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

### 1. Auto-Detect Running Application
Automatically finds any running Unreal-NG process (`unreal-videowall`, `unreal-qt`, or `unreal-screen-viewer`):
```bash
python3 tools/workload-monitor/process-workload.py
```

### 2. Monitor VideoWall Specifically
```bash
# Target VideoWall using dedicated shortcut
python3 tools/workload-monitor/process-workload.py --videowall
# or
python3 tools/workload-monitor/process-workload.py -W

# With thread grouping enabled
python3 tools/workload-monitor/process-workload.py -W --group-threads
```

### 3. Monitor All Running Unreal-NG Processes (Multi-Process Overview)
If you have multiple emulators or VideoWall running concurrently:
```bash
python3 tools/workload-monitor/process-workload.py --all
```

### 4. Target `unreal-qt` Specifically
```bash
python3 tools/workload-monitor/process-workload.py --qt
# or
python3 tools/workload-monitor/process-workload.py -Q
```

### 5. Monitor a Specific PID with 500 ms Refresh
```bash
python3 tools/workload-monitor/process-workload.py --pid 313539 --interval 0.5
```

### 6. Single-Shot Snapshot Mode (CLI / Scripting)
Takes a measurement over the specified interval, prints a clean report, and exits:
```bash
python3 tools/workload-monitor/process-workload.py -W --once --interval 1.0
```

### 7. Automated Benchmarks / JSON Mode
Outputs machine-readable JSON (including VideoWall tile metrics) for automated benchmark scripts:
```bash
python3 tools/workload-monitor/process-workload.py -W --json --once
```

### 8. Export Timeseries to CSV
Logs every sample interval (including tile counts and rendering load) to a CSV file:
```bash
python3 tools/workload-monitor/process-workload.py -W --csv scratch/videowall-workload.csv --count 60
```

---

## Sample Outputs

### VideoWall Monitoring Output

```text
┌────────────────────────────────────────────────────────────────────────────┐
│           TRUE WORKLOAD PROFILER :: UNREAL-VIDEOWAL (PID 313539)           │
└────────────────────────────────────────────────────────────────────────────┘
 Host: dev-Precision-5560 | OS: Linux 7.0.0-31-generic | Logical Cores: 16 | Time: 2026-09-07 14:16:43
──────────────────────────────────────────────────────────────────────────────
CPU WORKLOAD BREAKDOWN:
  ▶ Single-Core Load:      13.00% [███░░░░░░░░░░░░░░░░░] (1 core = 100.0%)
  ▶ All-Cores Equivalent:   0.81% [░░░░░░░░░░░░░░░░░░░░] (normalized across all 16 cores)
  ▶ Cores Saturated:       0.13 core(s) | Sample interval: 1.00s | CPU Time: 0.130s
  ▶ CPU Mode Splits:      User: 0.110s | Kernel/System: 0.020s
  ▶ Session Statistics:   Avg:  13.00% | Min:  13.00% | Max:  13.00% | Duration:  1.00s
──────────────────────────────────────────────────────────────────────────────
VIDEOWALL GRID & TILE ANALYTICS:
  🔲 Active Tiles / Emulators:    1 tiles | Total Tile CPU:   7.00% (0.44% system)
  ⚡ Avg Load per Tile:           7.00% (min: 7.00%, max: 7.00%)
  🖥️  Grid & UI Rendering Load:     6.00% | Services / Automation: 0.00%
  🚀 Capacity Forecast:         ~193 sustainable 50 FPS tiles on 16 cores (85% CPU ceiling)
──────────────────────────────────────────────────────────────────────────────
MEMORY & PROCESS HEALTH:
  • Resident Memory (RSS):    78.33 MB   • Virtual Memory (VMS):  1677.48 MB
  • Total Threads:            22      • Active Threads (>0%):      7
──────────────────────────────────────────────────────────────────────────────
THREAD BREAKDOWN (sorted by CPU load): (Top 15 active threads)
       TID  Role     Thread Name            1-Core %   System %   Total CPU  State
  ──────────────────────────────────────────────────────────────────────────
    313645  [Tile]   unreal-videowal        7.00%      0.44%       7.32s  S
    313539  [UI]     unreal-videowal        1.00%      0.06%       1.69s  S
    313554  [Worker] unreal-videowal        1.00%      0.06%       1.28s  S
    313648  [Worker] Thread (pooled)        1.00%      0.06%       0.33s  S
    313649  [Worker] Thread (pooled)        1.00%      0.06%       0.31s  S
    314071  [Worker] Thread (pooled)        1.00%      0.06%       0.18s  S
    314090  [Worker] Thread (pooled)        1.00%      0.06%       0.17s  S
──────────────────────────────────────────────────────────────────────────────
Note: 'Resources' shows 'System %'. True single-thread workload is '1-Core %'. (Ctrl+C to stop)
```

### Multi-Process Ecosystem Overview (`--all`)

```text
┌────────────────────────────────────────────────────────────────────────────┐
│           UNREAL-NG ECOSYSTEM WORKLOAD OVERVIEW (MULTI-PROCESS)            │
└────────────────────────────────────────────────────────────────────────────┘
 Host: dev-Precision-5560 | Logical Cores: 16 | Active Unreal Processes: 2 | Time: 2026-09-07 14:17:09
──────────────────────────────────────────────────────────────────────────────
       PID  Process            Role/Type     Tiles   1-Core %   System %   RSS (MB)
  ──────────────────────────────────────────────────────────────────────────
    295051  unreal-qt          Emulator          1     12.00%      0.75%    112.5M
    313539  unreal-videowal    VideoWall         1     13.00%      0.81%     78.5M
  ──────────────────────────────────────────────────────────────────────────
  TOTAL CONCURRENT WORKLOAD:                    2     25.00%      1.56%    191.0M
──────────────────────────────────────────────────────────────────────────────
  ▶ Cores Saturated: 0.25 of 16 cores | System Headroom: 98.4%
──────────────────────────────────────────────────────────────────────────────
```

---

## VideoWall Performance & Scaling Notes

In `unreal-videowall`:
1. **Emulation Tiles**:
   - Each tile runs an independent Z80 emulation thread (`MainLoop::Run()`).
   - At standard 50 Hz frame rates, each tile takes **~5% to 7% of 1 core** (~1.2 ms active execution + 18.8 ms frame sleep).
   - 4 tiles consume ~25% single-core (~1.5% all-core).
   - 16 tiles consume ~100% single-core (1 full core).
   - 64 tiles consume ~400% single-core (4 full cores).
2. **Video Wall Grid & UI Rendering**:
   - Handled by the Qt Main Thread + Qt concurrent worker pools.
   - Responsible for fetching framebuffers, scaling, and blitting tiles to the grid display.
   - Typically consumes **~4% to 8% single-core** depending on screen resolution and grid dimensions.
3. **Capacity Forecast Formula**:
   $$\text{Max Sustainable Tiles} \approx \left\lfloor \frac{(\text{Total Cores} \times 100\% \times 0.85) - \text{Rendering Overhead}}{\text{Average Load per Tile}} \right\rfloor$$
   This estimation ensures sufficient CPU headroom to prevent audio ring buffer underruns and UI stutter.

---

## Command-Line Options Reference

| Option | Short | Default | Description |
|---|---|---|---|
| `--videowall` | `-W` | `False` | Shortcut: target `unreal-videowall` |
| `--qt` | `-Q` | `False` | Shortcut: target `unreal-qt` |
| `--screen-viewer` | `-S` | `False` | Shortcut: target `unreal-screen-viewer` |
| `--all` / `--multi` | `-A` | `False` | Monitor all active Unreal-NG processes concurrently |
| `--group-threads` | `-g` | `False` | Group threads by role (`[Tile]`, `[UI]`, `[Worker]`, `[Service]`) |
| `--pid PID` | `-p` | Auto-detect | Target process PID |
| `--name REGEX` | `-n` | Auto-detect | Target process name regex pattern |
| `--interval SEC` | `-i` | `1.0` | Sampling interval duration in seconds |
| `--count N` | `-c` | `0` (infinite) | Number of samples to collect before exiting |
| `--once` | `-1` | `False` | Collect 1 sample and exit (snapshot mode) |
| `--top N` | `-t` | `15` | Maximum number of active threads to display in table |
| `--show-all-threads` | | `False` | Show all threads including idle (0.0% CPU) threads |
| `--wait` | | `False` | Wait until the target process appears if not running |
| `--no-color` | | `False` | Disable ANSI terminal color escape codes |
| `--json` | | `False` | Output JSON formatted metrics (includes VideoWall card) |
| `--csv FILE` | | `None` | Append timeseries metrics to a CSV file |
| `--help` | `-h` | | Show help message and exit |
| `--version` | | | Show program version |
