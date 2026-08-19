# AI Agent & Unreal-NG Interaction Use Cases

> **Status**: Draft  
> **Date**: 2026-08-14  
> **Scope**: Concrete, highly specific forensic and engineering use cases demonstrating how an AI Agent leverages the MCP interface for deep emulator analysis.

---

## 1. Concrete Use Case Inventory

| # | Scenario | Core Problem | Agent Action (via MCP) |
|:---|:---|:---|:---|
| 1 | **Video Timing Forensics** | Identify timing issue in Pentagon rendering. | Agent traces ULA beam positions and interrupt timings, comparing T-states against known floating-bus constraints to find rendering drift. |
| 2 | **Algorithm Analysis** | Auto-RE (Reverse Engineer) a demoscene effect. | Agent engages Opcode Profilers and Memory Heatmaps to identify the rendering pattern (e.g. stack-abuse vs LDIR), then queries the CallTrace buffer for algorithmic translation. |
| 3 | **FDC Synchronization** | Find why a custom TR-DOS disk loader fails. | Agent queries WD1793 state, monitoring Port `#1F`/`#7F` reads and DRQ flags to identify where the loader's polling loop desynchronizes from disk rotation speed. |
| 4 | **Audio Glitch Triage** | Find why there are ticks in tape-load generated sound. | Agent profiles Port `#FE` edge toggles, identifying T-state stretching caused by memory contention or rogue interrupts that generate audio pops. |
| 5 | **Automated De-Crunching** | Bypass cruncher and extract unpacked payload. | Agent sets an "execute-on-written" memory trap, letting the emulator run at warp speed until the packer jumps to the newly decompressed entry point, then dumps the clean RAM. |
| 6 | **Entity Tracking** | Track moving sprites/objects across frames. | Agent uses VRAM Delta buffers to mathematically isolate moving objects, or profiles working RAM to find and track internal entity X/Y coordinate variables. |

---

## 2. Detailed Interaction Flows & Automation Constraints

### 2.1 Video Timing Forensics: Pentagon Rendering
**Goal**: Pinpoint exactly why a border effect flickers or a multiplexer fails on the Pentagon 128K model.
**Interaction Flow**:
1. Agent uses `emulator_manage` to ensure the active model is `PENTAGON`.
2. Agent uses `load_software` to mount the failing `.trd` or `.sna` demo file.
3. Agent uses `search_api` to discover advanced video and interrupt profiling endpoints.
4. Agent uses `invoke_api` to set an execution breakpoint at the INT handler (`0x0038`).
5. When paused, the agent enters a loop: it steps the CPU (`control_execution`), and for each instruction, it invokes the video beam query endpoint to retrieve the ULA beam position (X/Y) and `frame_t_states`. 
6. The agent compares the reported `frame_t_states` against known Pentagon floating-bus constraints (71680 T-states per frame) to find exactly where the rendering drift occurs.

### 2.2 Algorithm Analysis: Auto-RE Demo Effect
**Goal**: Understand the mathematical approach behind a custom plasma or scroll effect without having source code.
**Interaction Flow**:
1. Agent allows the emulator to run until the effect is visually active on screen.
2. Instead of blindly stepping, the agent uses `search_api` to discover the advanced telemetry tools (Profilers and Heatmaps).
3. Agent uses `invoke_api` to start the **Opcode Profiler** and the **Memory Access Heatmap** analyzer, allowing the emulator to run for 50 frames.
4. Agent queries the `analyzer/opcode` results. The distribution reveals the rendering strategy (e.g., exposing that 85% of cycles are spent on `PUSH rr` and `POP rr`, immediately identifying a stack-based rendering technique).
5. Agent queries the `analyzer/heatmap` results for VRAM (`0x4000-0x57FF`). The spatial data reveals if the screen is drawn sequentially, vertically, or via scattered look-up tables.
6. Armed with this macro-level structural context, the agent queries the **CallTrace Buffer** to pull the exact mathematical instruction flow, translating the raw Z80 into a high-level Python/C pseudocode summary.

### 2.3 FDC Synchronization: Custom Disk Loader Failure
**Goal**: Debug a fast loader that works on real hardware but hangs or corrupts data in the emulator.
**Interaction Flow**:
1. Agent loads the failing TRD image via `load_software`.
2. Agent uses `search_api` to locate WD1793 FDC telemetry endpoints and Port Read breakpoints.
3. Agent uses `invoke_api` to set a Port-Read Breakpoint on the FDC Status Port (`0x1F`) or Data Port (`0x7F`).
4. Upon breaking, the agent uses `invoke_api` to query the internal state of the WD1793 FDC (Status Register, Track, Sector, DRQ pin state).
5. By tracing the sequence of reads and the FDC state, the agent identifies if the loader relies on a specific DRQ polling timing or a Lost Data (E-Flag) anomaly that isn't accurately modeled in the FSM.

### 2.4 Audio Glitch Triage: Tape Load Ticks
**Goal**: Eliminate audible pops or ticks generated during a synthesized tape loading routine.
**Interaction Flow**:
1. Agent loads the TAP file and initiates the loading sequence.
2. Agent uses `search_api` to find port breakpoint and trace buffer endpoints.
3. Agent uses `invoke_api` to set a Port-Write Breakpoint on the ULA control port (`0xFE`), which toggles the EAR/MIC speaker bit.
4. The agent resumes execution. Each time the breakpoint hits, the agent records the exact `t_states` counter.
5. The agent calculates the delta in T-states between toggles. If it identifies a gap that is too large (causing an audible frequency drop/pop), it uses `inspect_state` and the call stack to trace backwards, finding the memory contention or rogue interrupt handler stealing those cycles.

### 2.5 Automated De-Crunching: Packer Bypass
**Goal**: Automatically bypass a software cruncher/packer (e.g., Exomizer, MegaLZ) and capture a clean, unpacked memory snapshot exactly at the true entry point.
**Interaction Flow**:
1. **Pass 1 (Probe & Profile)**: Agent loads the compressed software. It enables **Memory Counters** and the **Heatmap Analyzer** to observe the decompression phase, identifying the exact memory regions being heavily written to (the target payload).
2. **Pass 2 (Execution Tracking)**: Agent enables SMC (Self-Modifying Code) traps and CallTrace tracking specifically for the identified payload memory region.
3. **Pass 3 (Time-Travel Recording)**: Agent enables the **Time-Travel Recorder** (rewind buffer) and runs the emulator at maximum warp speed.
4. **Precision Alignment**: When the SMC trap triggers (the PC executes an instruction in the newly unpacked memory), the emulator pauses. Because packers often have messy or obfuscated transitions, the agent analyzes the Time-Travel trace track backwards to pinpoint the *exact* `JP`, `CALL`, or `RET` instruction that logically exited the cruncher.
5. **Verification & Extraction**: The agent uses the time-travel engine to rewind to the exact cycle of the entry point, verifies the integrity of the CPU registers (parameters expected by the payload), and invokes `emulator_manage` to dump a pristine `.SNA` snapshot.

### 2.6 Entity Tracking & Delta Frame Analysis
**Goal**: Detect per-frame changes and track moving sprites/objects for collision debugging or automated game testing without using slow Optical Character Recognition or Computer Vision on screenshots.
**Interaction Flow**:
1. Agent allows the game to run and uses `control_execution` to step frame-by-frame.
2. To detect per-frame changes visually, the agent uses `search_api` to invoke the **VRAM Delta Buffer** endpoint. Instead of a full memory dump, this returns only the exact bytes and coordinates that changed between Frame `N` and Frame `N+1`. The agent mathematically calculates the bounding box of the moving sprite based on these modified bytes.
3. To track the object logically, the agent uses the **Memory Counters/Heatmap** to identify the specific RAM addresses (e.g., `0x8000`, `0x8001`) that are updating perfectly in sync with the sprite's movement.
4. The agent then sets a memory read/write watchpoint on those specific addresses, allowing it to extract the true internal X/Y coordinate state of the game entities directly from memory on every frame.

### 2.7 Evaluating Audio/Visual Output (The Inefficiency of Media)
**Goal**: Verify what is actually being rendered to the screen or speakers without sending massive, token-heavy image/audio files or screen recordings to the LLM.
**Interaction Flow**:
Sending frame-by-frame PNGs or WAV files to an LLM is astronomically expensive and slow. Instead, the AI agent evaluates AV output programmatically:
1. **Visuals (VRAM Dumps - Intent)**: To check if a sprite or pixel was theoretically rendered, the agent uses `inspect_state` to pull the raw bytes from the Video RAM (`0x4000 - 0x5AFF`) and mathematically verifies the bitmask/attributes.
2. **Visuals (Host Framebuffer - Quality)**: To verify the *actual output* including the border, CRT shaders, and aspect ratio correction, the agent queries the `video/framebuffer` endpoint. Instead of a massive PNG, this returns a perceptual hash (e.g., SSIM against a reference) or a severely downsampled/indexed array of the host's final texture.
3. **Text (Internal OCR)**: If the agent needs to read a menu, it uses a dedicated `search_api` endpoint that asks the *emulator* to run OCR internally and return a plain text string, rather than taking a screenshot.
4. **Audio (Chip Registers - Intent)**: To understand what the software *wants* to play, the agent queries the internal state of the AY-3-8910 sound chip (14 registers) or the Beeper state.
5. **Audio (Post-Mixer PCM - Quality)**: To verify the *actual output* after the mixer, filters, and DACs, the agent queries the `audio/pcm` endpoint. It can request up to 2 frames of raw PCM data. To avoid overloading the context window, the endpoint also provides built-in DSP helpers returning metadata like Fundamental Frequency (Hz), Peak Signal-to-Noise Ratio (PSNR), and clipping alerts.

---

## 3. Automation Gap Analysis (Missing WebAPI Features)

For the AI agent to successfully execute the use cases above, the underlying `AutomationWebAPI` must expose deep emulator telemetry. The table below identifies functionality that must be verified or added to the core Drogon REST controllers so the `search_api` / `invoke_api` router can access them.

| Use Case | Required Functionality | Missing/Unverified WebAPI Endpoint | Required Payload & Response Schema |
|:---|:---|:---|:---|
| **2.1 Video Timing** | ULA Beam Tracking | `GET /api/v1/emulator/{id}/video/beam` | **Response**: `{"ray_x": int, "ray_y": int, "t_states": int, "frame_t_states": int}` |
| **2.2 Algorithm RE** | Opcode Profiler Stats | `GET /api/v1/emulator/{id}/analyzer/opcode` | **Response**: `{"total_cycles": 71680, "distribution": {"PUSH HL": 0.45, "LDIR": 0.10, ...}}` |
| **2.2 Algorithm RE** | Memory Access Heatmap | `GET /api/v1/emulator/{id}/analyzer/heatmap` | **Response**: `{"region": "0x4000-0x57FF", "writes": [..intensity matrix..]}` |
| **2.2 Algorithm RE** | CallTrace Buffer | `GET /api/v1/emulator/{id}/analyzer/calltrace` | **Response**: `{"history": [{"pc": 32768, "opcode": "C5", "mnem": "PUSH BC"}, ...]}` |
| **2.3 FDC Sync** | WD1793 Telemetry | `GET /api/v1/emulator/{id}/fdc/state` | **Response**: `{"status": int, "track": int, "sector": int, "data": int, "drq": bool, "intrq": bool}` |
| **2.3 FDC Sync** | Port Breakpoints | `POST /api/v1/emulator/{id}/breakpoints` | **Payload**: `{"type": "port_read", "port": 31}` (0x1F) |
| **2.4 Audio Glitch** | Hardware Event Trace Log | `GET /api/v1/emulator/{id}/analyzer/trace` | **Response**: `{"events": [{"type": "PORT_WRITE", "port": 254, "value": 16, "t_states": 124555}, ...]}` |
| **2.4 Audio Glitch** | Global T-State Exposure | `GET /api/v1/emulator/{id}/cpu/state` | **Response** must strictly include `t_states` (64-bit absolute cycle counter) and not just registers. |
| **2.5 De-Crunching** | SMC Traps / Memory Counters | `POST /api/v1/emulator/{id}/traps` | **Payload**: `{"type": "smc_execution", "range": [16384, 65535]}` |
| **2.5 De-Crunching** | Time-Travel Engine | `POST /api/v1/emulator/{id}/timetravel` | **Payload**: `{"action": "rewind_to_pc", "pc": 32768}` |
| **2.6 Entity Tracking** | VRAM Delta Buffer | `GET /api/v1/emulator/{id}/video/vram_delta` | **Response**: `{"frame_a": 100, "frame_b": 101, "diffs": [{"addr": 16384, "old": 0, "new": 255}]}` |
| **2.7 AV Eval** | Internal Screen OCR | `GET /api/v1/emulator/{id}/video/ocr` | **Response**: `{"text": "1982 Sinclair Research Ltd", "confidence": 0.98}` |
| **2.7 AV Eval** | AY-3-8910 Registers | `GET /api/v1/emulator/{id}/audio/ay/state` | **Response**: `{"r0": 255, "r1": 12, "r8_vol_a": 15, "r9_vol_b": 0, ...}` |
| **2.7 AV Eval** | Post-Mixer PCM & DSP | `GET /api/v1/emulator/{id}/audio/pcm` | **Response**: `{"frames": 2, "fundamental_hz": 440.1, "psnr": 42.1, "raw_samples": [...]}` |
| **2.7 AV Eval** | Host Framebuffer | `GET /api/v1/emulator/{id}/video/framebuffer` | **Response**: `{"width": 800, "height": 600, "perceptual_hash": "a1b2...", "raw_rgb": [...]}` |
