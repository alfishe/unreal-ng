# Feature Brainstorming & Codebase Reconciliation

This document reconciles an extensive feature brainstorm for reverse engineering with the actual state of the `unreal-ng` codebase.

## 1. Core Debugging & Emulation (Implemented)
These features are already present in the modular architecture:
- **Modular Core**: Separate `EmulatorContext`, `DebugManager`, and model-specific port decoders.
- **Disassembly**: Interactive Z80 disassembler with label support.
- **Breakpoints**: Robust system for standard and conditional breakpoints.
- **Memory Tracking**: Basic per-byte read/write/execute counters in `MemoryAccessTracker`.
- **Automation**: Lua scripting engine and RESTful WebAPI (Drogon-based) for remote control. (Note: Batch command execution is currently in a separate feature branch).
- **Multi-Instance**: Framework for managing and switching between multiple emulator instances.

## 2. Advanced Analysis (High Priority / Proposed)
The following areas represent the next phase of development for deep reverse engineering:

### Memory Analysis & Tracking
- **Heatmaps**: Real-time visualization of access frequency (R/W/E).
- **Pattern Search**: Find repeated data structures (sprite tables, music patterns).
- **Self-Modifying Code**: Automatic detection and highlighting of instructions that modify memory.
- **Data Type Inference**: Heuristics to distinguish between code, graphics, sound data, and tables.

### Execution Profiling & Tracing
- **SQLite Trace Log**: Replacing flat text logs with a queryable SQL database for frame-by-frame instruction and port traces.
- **Hot Path Identification**: Aggregate execution cycles to find bottlenecks or "essence" routines.
- **Cycle-Accurate Timeline**: Visualization of execution flow relative to raster position.

### Graphics & Audio Forensics
- **Sprite Extraction**: Automatic atlas generation by tracking sequential screen writes and source memory regions.
- **Audio Register Logging**: Deep AY-3-8910 register tracking with pattern visualization to identify music engines.
- **Waveform Analysis**: Real-time visualization of beeper and AY output for routine isolation.
- **Planned Hardware Support**: Implementation of COVOX, General Sound, Moonsound, and ZX-Next 3xAY is targeted for future versions.

### Demo-Specific Analysis
- **Effect Detection**: Fingerprinting specific visual effects (rasters, plasma, scrollers).
- **Sync Point Finder**: Detecting the correlation between AY register changes and screen updates.
- **Part Segmentation**: Automated splitting of demo parts based on code range shifts and memory loads.
