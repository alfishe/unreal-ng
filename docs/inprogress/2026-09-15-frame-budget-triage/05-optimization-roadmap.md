# Optimization Roadmap — Eliminating the 15–20 ms Frame Tail

Based on empirical benchmark data (`04-benchmark-evidence.md`), log deduction (`01-evidence-and-deductions.md`), and hypothesis testing (`02-hypothesis-matrix.md`).

---

## 1. What the Findings Mean

1. **The Emulation Engine is NOT Slow:**
   In headless execution (`BM_CovoxDemoFrame`), the entire Pentagon 128 emulation cycle—Z80 instructions, video screen rendering, and full Covox sound synthesis—completes in **2.97 ms per frame**. That is only **14.5% of the 20.48 ms budget** (a 6.9× real-time safety margin).
2. **The 18–21 ms Spikes are Environmental Interplay, Not Emulation Load:**
   The worst frames are on-CPU for ~99% of wall time (18.2 ms on-CPU out of 18.3 ms wall time). The thread is actively executing code, but hardware throughput collapses by **~3.6×–4×** across all stages simultaneously.
3. **The Root Cause: The "15 ms Sleep & Wake" Penalty:**
   Because the core work only takes ~3–5 ms, the emulation thread sleeps for ~15 ms between frames to pace at 50 Hz. This sleep triggers:
   - **OS Power Governor Downclocking:** Windows reduces core frequency (e.g. from 3.7 GHz to 1.0–1.2 GHz) or puts the core into deep C-states / parking during the sleep. Modern governor ramp-up takes 15–30 ms—causing the entire subsequent frame to execute at a fraction of base clock speed.
   - **SMT Sibling Interference:** The thread wakes on a logical processor whose hyper-threaded sibling is active with audio pulls (WASAPI MMCSS), Qt GUI presentation, or DWM.
   - **Cache Eviction:** During the 15 ms gap, GUI rendering touches the framebuffer and displaces the Z80 interpreter's working set from L1/L2.
   - **Diagnostic Overhead:** The 4 `chrono::steady_clock::now()` calls per Z80 step in `OnCPUStep` add ~1.7–2.5 ms of fixed overhead per frame.

---

## 2. Optimization Roadmap

### Tier 1: Immediate Wins (Zero Architectural Risk)

#### 1. Compile-Gate Per-Step Diagnostic Timer Reads
- **Location:** [`core/src/emulator/mainloop.cpp`](file:///c:/Projects/LocalGit/unreal-ng/core/src/emulator/mainloop.cpp#L575-L596) (`MainLoop::OnCPUStep`)
- **Action:** Gate `_stepScreenNs`, `_stepIoNs`, `_stepSoundNs` behind `#ifdef ENABLE_STEP_DIAGNOSTICS`.
- **Gain:** Saves **1.7–2.5 ms per frame** immediately (eliminates ~50,000–70,000 clock reads per frame in interactive runs).

#### 2. Hybrid Pacing (Spin-Wait Pre-Wake to Heat the Core)
- **Location:** [`core/src/common/timehelper.cpp`](file:///c:/Projects/LocalGit/unreal-ng/core/src/common/timehelper.cpp) (`TimeHelper::WaitUntilPrecise`) / [`MainLoop::Run`](file:///c:/Projects/LocalGit/unreal-ng/core/src/emulator/mainloop.cpp#L200)
- **Action:** Sleep via waitable timer for `target - 1.5 ms`, then execute a short pause/spin-wait for the final 1.5 ms before the frame deadline.
- **Why it works:** The 1.5 ms spin warms the core, forces the Windows frequency governor to scale the core to maximum P-state *before* `RunFrame()` starts, and prevents the thread from waking in a low-frequency state.

---

### Tier 2: Threading & OS Scheduling Architecture

#### 3. Emulation Thread Affinity & SMT Sibling Isolation
- **Location:** [`core/src/common/threadhelper.cpp`](file:///c:/Projects/LocalGit/unreal-ng/core/src/common/threadhelper.cpp) / [`MainLoop::UpdateRealtimeScheduling`](file:///c:/Projects/LocalGit/unreal-ng/core/src/emulator/mainloop.cpp#L75)
- **Action:**
  - Query CPU topology via `GetLogicalProcessorInformationEx(RelationProcessorCore)`.
  - Pin the active emulation thread to an exclusive physical core (primary LP).
  - Explicitly assign GUI worker and background threads to different physical cores.
- **Why it works:** Completely prevents SMT execution stall collisions with the GUI thread, audio buffer pulls, and DWM.

#### 4. Windows Power Request & Quality-of-Service (EcoQoS Prevention)
- **Location:** [`unreal-qt/src/main.cpp`](file:///c:/Projects/LocalGit/unreal-ng/unreal-qt/src/main.cpp) / [`ThreadHelper`](file:///c:/Projects/LocalGit/unreal-ng/core/src/common/threadhelper.cpp)
- **Action:**
  - Register `PowerCreateRequest(POWER_REQUEST_EXECUTION_REQUIRED)` during active emulation.
  - Set `THREAD_POWER_THROTTLING_CURRENT_VERSION` with `THREAD_POWER_THROTTLING_EXECUTION_SPEED` to prevent Windows 11 / EcoQoS from throttling the thread during background or partial occlusion states.

---

### Tier 3: Memory & Presentation Decoupling

#### 5. Lock-Free Double-Buffered Frame Presentation
- **Location:** [`unreal-qt/src/mainwindow.cpp`](file:///c:/Projects/LocalGit/unreal-ng/unreal-qt/src/mainwindow.cpp#L3607) (`setFrameSource`) & [`Screen::LatchFramebuffer`](file:///c:/Projects/LocalGit/unreal-ng/core/src/emulator/video/screen.cpp)
- **Action:** Ensure the presentation buffer used by OpenGL texture upload is completely isolated from the emulator's active render page using ping-pong buffering.
- **Why it works:** Prevents GUI texture uploads from evicting L2/L3 cache lines while the next emulation frame is initializing.

---

## 3. Architectural Invariants (Non-Negotiable Constraints)

### ⛔ Non-Goal: Fast-Forwarding or Skipping `HALT`
Do **not** attempt to fast-forward the CPU clock across `HALT` instructions:
- **Asynchronous Peripheral Synchronization:** Peripherals including **WD1793 FDC (BetaDisk)**, **AY-3-8910**, **TurboSound FM (YM2203)**, **MoonSound (Yamaha OPL4)**, **General Sound (12 MHz Z80 coprocessor)**, **Tape**, and the **ULA raster beam** execute in cycle-accurate lockstep with main CPU T-states.
- Skipping `HALT` breaks FDC 112 T-state byte transfers, desynchronizes YM2203 / OPL4 hardware timers, starves the GS Z80 coprocessor of clock cycles, and corrupts tape edge detection.
- **No Emulation Need:** The headless benchmark (`BM_CovoxDemoFrame`) proves that executing all 17,900 steps with full peripheral synchronization takes only **2.97 ms** (6.9× faster than real-time budget). Optimization must focus on environmental SMT contention, C-state sleep recovery, and diagnostic elimination—never compromising peripheral fidelity.

