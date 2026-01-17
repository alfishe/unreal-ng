# unreal-ng features 

**Legend:**
*   ✅ **Complete:** Fully implemented in the Core/Codebase.
*   ⚠️ **Partial:** Underlying infrastructure (data collection) exists, but UI/Tooling/Visualization is missing.
*   ❌ **New:** Not implemented; represents a valid feature request.
*   𔲲 **Planned:** Future roadmap item.
*   �📉 **Low Priority:** Proposed but considered overkill or low value.

### **unreal-ng Feature Status (Comprehensive)**

| Category | Feature | Status | Notes / Codebase Location |
| :--- | :--- | :---: | :--- |
| **Hardware Support & Emulation** |
| **Machine Models** | Base ZX Spectrum Models (48K, 128K, +2, +3) | ✅ Complete | Core emulation for the official Sinclair and Amstrad models. |
| | Pentagon Models (128k, 512k) | ✅ Complete | Emulation of the popular Soviet clones with demoscene-critical memory timings. |
| | Timex Clones Emulation (e.g., TS2068) | 𔲲 Planned | Support for unique Timex screen modes (e.g., 512x192) and extended features. |
| | ATM Turbo 2+ Emulation | 𔲲 Planned | Support for extended graphics modes (e.g., 640x200), faster CPU, and CP/M. |
| | Profi Emulation | 𔲲 Planned | Support for extended memory (up to 1MB) and native CP/M support. |
| | ZX Spectrum Next Machine Emulation | 𔲲 Planned | Full emulation of the modern hardware: 7/14/28MHz Z80, hardware sprites & tilemaps, 2MB RAM, triple AY, etc. |
| **Audio Peripherals** | Base Audio (AY-8910, Beeper, TurboSound) | ✅ Complete | Core sound hardware is fully emulated. |
| | COVOX Support | 𔲲 Planned | Support for the digital-to-analog converter for sample playback. |
| | General Sound Support | 𔲲 Planned | Support for the 16-channel MIDI-compatible sound card. |
| | Moonsound Support | 𔲲 Planned | Support for the OPL4-based FM and wavetable synthesis card. |
| **Core Debugging & Inspection** |
| Disassembly | Full Z80 Instruction Set Disassembler | ✅ Complete | Decodes all instructions, prefixes, and tracks T-states. `core/src/debugger/disassembler/z80disasm.h` |
| Breakpoints | Memory & I/O Port Breakpoints | ✅ Complete | Includes bank-aware matching, groups, and annotations. `core/src/debugger/breakpoints/breakpointmanager.h` |
| | Conditional Breakpoints (on register values/expressions) | ❌ New | **High Priority.** A crucial feature for advanced debugging. |
| | Screen Position (Raster) Breakpoints | ❌ New | **High Priority.** Break when the electron beam reaches a specific X,Y coordinate. |
| | Memory Pattern Breakpoints | ❌ New | Break when a specific sequence of bytes appears in memory. |
| | Frame Count Breakpoints | ❌ New | Break execution at a specific frame number. |
| Labels & Symbols | Label Management System | ✅ Complete | Allows creating and managing labels for memory addresses. `core/src/debugger/labels/labelmanager.*` |
| | Import/Export Label Files (SLD, etc.) | ❌ New | Needed for compatibility with other reverse engineering tools. |
| | Community Database Integration for Symbols | ❌ New | Ability to pull labels and annotations from a shared, central database. |
| **Execution Analysis** |
| Call Stack | Call & Execution Flow Tracing | ✅ Complete | Logs all control flow events with loop compression. `core/src/emulator/memory/calltrace.h` |
| | Call Graph Generation & Visualization | ⚠️ Partial | Data is fully collected by `CallTraceBuffer`, but no visualization layer exists. |
| | Reverse Call Graph | ❌ New | From a given address, show all possible calling locations. |
| Code Coverage | Per-Address Execution Counters | ✅ Complete | `_z80ExecuteCounters` in `core/src/emulator/memory/memoryaccesstracker.h` |
| | Code Coverage Overlay in Disassembler | ⚠️ Partial | **UI Task.** Data is collected, but not visualized over the disassembly. |
| Cross-References | Find References to Address/Label | ❌ New | **High Priority.** Determine "what calls this?" or "what uses this data?". |
| **Memory Analysis** |
| Memory Tracking | Read/Write/Execute Access Counters & Callers | ✅ Complete | Tracks access counts, caller PCs, and data values. `core/src/emulator/memory/memoryaccesstracker.h` |
| Memory Heatmap | Heatmap Data Collection | ✅ Complete | All necessary data is collected by `MemoryAccessTracker`. |
| | Heatmap Visualization Layer | ⚠️ Partial | **UI Task.** A UI is needed to display the collected heatmap data. |
| | Temporal Heatmap (access over time) | ❌ New | Visualize how memory access changes from frame to frame, not just totals. |
| | Clickable Heatmap Regions | ❌ New | UI feature to click a "hot" region and jump to the code that accesses it. |
| Comparative Analysis | Snapshot / State Comparison & Diffing | ❌ New | Diff memory, registers, and state between two points in time. |
| | Track Variable Changes Over Time | ❌ New | Watch a memory location and log its value changes and what code changed it. |
| **Automation & Scripting** |
| Scripting APIs | WebAPI (HTTP), Lua, CLI, Python | ✅ Complete | Comprehensive automation support. Located in `core/automation/`. |
| Macros | Macro Recording System | ❌ New | User-facing tool to record and replay sequences of actions for repetitive tasks. |
| **Graphics Analysis & Extraction** |
| Screen Capture | Framebuffer Capture & Recording (Video/GIF/APNG) | ✅ Complete | `RecordingManager`. |
| Visualizers | Screen Memory Visualizer (Pixel/Attribute split-view) | ❌ New | **High Priority.** A crucial tool for inspecting screen data structures. |
| Resource Extraction | Sprite Discovery (8x8, 16x16, arbitrary size) | ❌ New | |
| | Bitmap Font & Tileset/Charset Recognition | ❌ New | |
| | Masked Sprite Detection (Sprite + Mask Pairs) | ❌ New | |
| | Animation Sequence Detection & Export | ❌ New | |
| | Screen Layout / Tilemap Reconstruction | ❌ New | |
| | Color Palette Extraction | ❌ New | |
| **Audio Analysis & Extraction** |
| Analysis | AY Register Logging & Pattern Analysis | ❌ New | **High Priority.** A tool for understanding music player routines. |
| | Beeper Routine Detection & Waveform Capture | ❌ New | |
| Resource Extraction | Music Ripping (ProTracker, Vortex, etc. formats) | ❌ New | |
| | Sample / Digitized Audio Extraction | ❌ New | |
| | Music Pattern / Instrument Identification | ❌ New | |
| **Peripheral Visualization & Monitoring** |
| Memory System | Memory Page/Bank Switching Timeline & Visualization | ❌ New | Visualize which ROM/RAM pages are active and when they change. |
| Storage Devices | FDC/Floppy Disk & TR-DOS Command Logging | ❌ New | |
| | Disk Sector Access Heatmap | ❌ New | |
| | Tape Activity Visualization (loading phases, pulses) | ❌ New | |
| I/O Activity | AY Register Change Timeline | ❌ New | |
| | Port I/O Activity Summary | ❌ New | |
| | Keyboard, Joystick, Mouse Activity Logging | ❌ New | |
| Reporting | High-Level Peripheral Usage Summary Report | ❌ New | A quick "what was activated" brief for initial analysis. |
| **Smart Code & Data Analysis** |
| Block Classification | Code vs. Data vs. Unused Block Detection | ❌ New | |
| | Dead Code Identification & Reachability Analysis | ❌ New | |
| Self-Modifying Code | SMC & Decruncher Detection | ❌ New | Identify code that writes to memory and then executes it. |
| | SMC Region Highlighting in Disassembly | ❌ New | |
| Interrupt Analysis | ISR (Interrupt Service Routine) Detection | ❌ New | Identify routines called by IM1/IM2. |
| | Interrupt Frequency Measurement | ❌ New | |
| Signature Analysis | Known Routine Signatures (ROM, Libraries) | ❌ New | |
| | Compression Algorithm & Packer Detection (RLE, LZ) | ❌ New | |
| | Protection Scheme & Anti-Debug Signature Detection | ❌ New | |
| | User-Defined Custom Signature Database | ❌ New | |
| Function Logic | Main Loop, State Machine, Input Handler Detection | ❌ New | |
| | Specific Function Classification (Render, Sound, Math) | ❌ New | |
| Pattern Matching | Find Similar or Copy-Pasted Code Blocks | ❌ New | |
| Data Flow | Variable Lifetime & Data Flow Tracking (Source to Sink) | ❌ New | |
| **Demo & Game Specific Analysis** |
| Structural Analysis | Demo Part Boundary Detection | ❌ New | Automatically segment a demo into its distinct parts. |
| | Effect Cataloging (Plasma, Rasters, Scrollers) | ❌ New | |
| | Music / Graphics Sync Point Detection | ❌ New | |
| Asset Extraction | Loading Screen Extraction | ❌ New | |
| **Advanced Semantic Analysis** |
| Procedure Fingerprinting| Routine Classification via Memory & Temporal Patterns | ❌ New | **Novel Concept.** The core idea of classifying routines (music player, effect, etc.) based on their memory access "fingerprint". |

