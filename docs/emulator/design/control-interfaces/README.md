# Emulator Control Interfaces Documentation

This directory contains comprehensive documentation for all emulator control interfaces in the unreal-ng ZX Spectrum emulator project.

## Overview

The Emulator Control Interface (ECI) provides multiple ways to interact with and control the emulator, each optimized for different use cases. All interfaces share the same underlying command set but differ in their protocol, performance characteristics, and typical usage patterns.

## Documentation Structure

### Core Documentation

📄 **[command-interface.md](./command-interface.md)** - **START HERE**  
The main high-level documentation covering:
- Architecture and design philosophy
- Complete command reference (implemented and planned)
- Command categories and detailed specifications
- Protocol-agnostic command semantics

### Interface-Specific Documentation

Each interface has its own detailed documentation file:

#### Implemented Interfaces

✅ **[cli-interface.md](./cli-interface.md)** - TCP-based Command Line Interface  
- Interactive text-based protocol
- Connection examples (telnet, netcat)
- Session management
- Scripting and automation
- **Best for**: Manual debugging, shell scripts, simple automation

✅ **[webapi-interface.md](./webapi-interface.md)** - HTTP/REST API  
- RESTful JSON endpoints
- WebSocket support (planned)
- CORS and security considerations
- Client examples (JavaScript, Python, curl)
- **Best for**: Web frontends, dashboards, remote management

✅ **[python-interface.md](./python-interface.md)** - Python Bindings (pybind11)  
- In-process C++ bindings
- Complete API reference
- Zero-copy memory access
- NumPy integration
- **Best for**: Test automation, AI/ML, data analysis, complex scripting

✅ **[lua-interface.md](./lua-interface.md)** - Lua Bindings (sol2)  
- Lightweight scripting interface
- Embedded logic and macros
- Event callbacks
- Hot reload support
- **Best for**: Game scripts, hotkeys, quick macros, event handlers

#### Planned Interfaces (Future)

🔮 **[gdb-protocol.md](./gdb-protocol.md)** - GDB Remote Serial Protocol  
Target: Q2 2026
- Standard GDB/LLDB compatibility
- IDE integration (VS Code, CLion)
- Professional debugging workflows
- **Best for**: Source-level debugging, IDE users, GDB power users

🔮 **[udb-protocol.md](./udb-protocol.md)** - Universal Debug Bridge  
Target: Q3-Q4 2026
- High-performance binary protocol
- Advanced profiling and tracing
- Memory streaming
- Time-travel debugging
- **Best for**: Performance analysis, reverse engineering, deep inspection

## Quick Start

### For Users

1. **Interactive debugging**: Start with [CLI Interface](./cli-interface.md)
2. **Web integration**: Use [WebAPI Interface](./webapi-interface.md)
3. **Automation scripts**: Choose [Python](./python-interface.md) or [Lua](./lua-interface.md)

### For Developers

1. Read the [Command Interface Overview](./command-interface.md) to understand the architecture
2. Choose your interface based on use case (see comparison table below)
3. Refer to interface-specific documentation for implementation details
4. Check the implementation status in each document

## Interface Comparison

| Feature | CLI | WebAPI | Python | Lua | GDB | UDB |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Status** | ✅ | ✅ | ✅ | ✅ | 🔮 | 🔮 |
| **Protocol** | Text | HTTP/JSON | Native | Native | Binary | Binary |
| **Performance** | Medium | Medium | High | High | Medium | Very High |
| **Learning Curve** | Low | Low | Medium | Low | Medium | High |
| **Scripting** | Shell | JavaScript | Python | Lua | Python | Lua/Python |
| **IDE Support** | No | No | Yes | Limited | Yes | Yes |
| **Remote Access** | Yes | Yes | No | No | Yes | Yes |
| **Real-time Streaming** | No | Limited | No | No | No | Yes |
| **Profiling** | No | No | Limited | No | No | Yes |
| **Multi-Instance** | Yes | Yes | Yes | Yes | Yes | Yes |

**Legend**: ✅ Implemented | 🔧 In Progress | 🔮 Planned

## Command Categories

All interfaces (where applicable) support these command categories:

### Implemented Commands

1. **Session & Lifecycle** - Connection management, emulator selection
   - `help`, `status`, `list`, `select`, `open`, `exit`

2. **Execution Control** - CPU control and stepping
   - `pause`, `resume`, `reset`, `step`, `stepin`, `steps`, `stepover`

3. **State Inspection** - View internal state
   - `registers`, `memory`, `debugmode`, `memcounters`, `calltrace`

4. **Breakpoints & Watchpoints** - Advanced debugging
   - `bp`, `wp`, `bport`, `bplist`, `bpclear`, `bpgroup`, `bpon`, `bpoff`

5. **Feature Management** - Runtime feature toggles
   - `feature` (list/enable/disable subsystems)

### Planned Commands (Future)

6. **Snapshot Management** - Save/load state
7. **Input Injection** - Keyboard, joystick automation
8. **Content Analyzers & Extractors** - Intelligent content analysis
   - ✅ BASIC Extractor (implemented)
   - Music detector & ripper
   - Sprite ripper
   - Self-modifying code detector
   - Compressor/packer detector
   - Tape/disk loader detector
   - Graphics effects detector
9. **LLM Integration (MCP/A2A)** - AI-assisted debugging and automation
   - Model Context Protocol for LLM queries
   - Agent-to-Agent protocol for autonomous agents
   - Natural language debugging
   - Automated testing and analysis
10. **Media Operations** - Tape/disk control
11. **Audio/Video Capture** - Recording and export
12. **Scripting Integration** - Script execution
13. **Disassembly** - Code analysis
14. **Performance Profiling** - CPU hotspots
15. **Network & Multi-Emulator** - Multi-instance coordination

See [command-interface.md](./command-interface.md) for complete details.

## Implementation Status Summary

### Current State (January 2026)

**Fully Implemented**:
- ✅ CLI server with full command set
- ✅ WebAPI basic endpoints (emulator control)
- ✅ Python bindings (core API)
- ✅ Lua bindings (core API)
- ✅ Breakpoint system (execution, memory, port)
- ✅ Debug features (stepping, memory inspection)
- ✅ Feature toggles
- ✅ **BASIC Extractor** - Tokenized BASIC program extraction

**Partially Implemented**:
- 🔧 WebAPI (missing: breakpoints, memory, advanced features)
- 🔧 Python/Lua (missing: some advanced debugging features)

**Planned - Phase 1 (Q1-Q2 2026)**:
- 🔮 Snapshot save/load commands
- 🔮 Input injection (keyboard, joystick)
- 🔮 Disassembly commands
- 🔮 Symbol file loading

**Planned - Phase 2 (Q2-Q3 2026)**:
- 🔮 GDB Remote Serial Protocol
- 🔮 **Music Detector & Ripper** - Extract and identify music formats
- 🔮 **Compressor/Packer Detector** - Identify protection schemes
- 🔮 Media operations (tape/disk control)
- 🔮 Audio/video capture

**Planned - Phase 3 (Q3 2026)**:
- 🔮 **Sprite Ripper** - Graphics asset extraction
- 🔮 **Self-Modifying Code Detector** - Dynamic code analysis
- 🔮 **Loader Detector** - Custom tape/disk routines
- 🔮 **Graphics Effects Detector** - Multicolor, gigascreen
- 🔮 Performance profiling
- 🔮 History rewind/replay

**Planned - Phase 4 (Q3-Q4 2026)**:
- 🔮 Universal Debug Bridge (UDB) Protocol
- 🔮 **Model Context Protocol (MCP)** - LLM integration for AI-assisted debugging
- 🔮 **Agent-to-Agent (A2A) Protocol** - Autonomous agent control
- 🔮 **Analyzer Framework** - Extensible pattern detection system
- 🔮 Multi-emulator networking

## Development Roadmap

### Q1 2026 (Current)
- ✅ Document all existing interfaces
- ✅ **BASIC Extractor** implementation complete
- Stabilize CLI and Python/Lua APIs
- Expand WebAPI endpoint coverage
- Begin analyzer framework design

### Q2 2026 - Core & Analysis Tools
**Focus**: Essential debugging + content analysis
- Implement GDB Remote Serial Protocol
- Add snapshot save/load commands
- Input injection (keyboard/joystick)
- **Music Detector & Ripper** - AY/Beeper format detection
- **Compressor/Packer Detector** - Protection scheme identification
- Disassembly commands with symbol support

### Q3 2026 - Advanced Analysis
**Focus**: Automated content extraction
- Begin Universal Debug Bridge protocol
- **Sprite Ripper** - Graphics extraction
- **Self-Modifying Code Detector** - Dynamic analysis
- **Tape/Disk Loader Detector** - Custom routine detection
- **Graphics Effects Detector** - Multicolor, gigascreen
- Audio/video capture
- Performance profiling
- **Analyzer Framework** core infrastructure

### Q4 2026 - AI Integration
**Focus**: LLM integration and autonomous agents
- Complete UDB protocol
- **Model Context Protocol (MCP)** - LLM query interface
- **Agent-to-Agent (A2A) Protocol** - Autonomous agent control
- Natural language debugging capabilities
- Automated testing with AI agents
- **Extensible Analyzer System** - Python/Lua custom analyzers
- Time-travel debugging
- Multi-agent collaboration features

### 2027+ - Future Expansion
- Machine learning-based pattern detection
- Cloud-based LLM integration
- Collaborative debugging (multi-user sessions)
- Advanced game AI training environments
- Automated software archaeology tools
- Community pattern database (crowdsourced signatures)

## Architecture Highlights

### Layered Design

```
┌─────────────────────────────────────┐
│  Interface Layer                    │  (CLI, WebAPI, Python, Lua)
│  Protocol-specific                  │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│  Command Processor Layer            │  (CLIProcessor, command dispatch)
│  Protocol-agnostic                  │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│  Emulator Core Layer                │  (Emulator, Z80, Memory, Debug)
│  Platform-independent               │
└─────────────────────────────────────┘
```

### Key Design Principles

1. **Separation of Concerns**: Interface ≠ Protocol ≠ Implementation
2. **Consistency**: Same command semantics across all interfaces
3. **Extensibility**: Easy to add new interfaces
4. **Performance**: Zero-copy where possible (Python/Lua)
5. **Security**: Sandboxed execution, access control (planned)

## Contributing

Contributions to interfaces and documentation are welcome!

### Adding New Commands

1. Define command in [command-interface.md](./command-interface.md)
2. Implement in `CLIProcessor` (core logic)
3. Add to interface-specific handlers (CLI, WebAPI, etc.)
4. Update relevant documentation
5. Add tests

### Adding New Interfaces

1. Create interface class (e.g., `AutomationGDB`)
2. Implement protocol handling
3. Map protocol commands to `CLIProcessor` methods
4. Create documentation file
5. Update this README

### Documentation Guidelines

- Keep interface-specific docs separate from core command docs
- Include practical examples
- Document limitations and known issues
- Maintain consistent formatting across all docs

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### References

#### Internal Documentation
- **[Architecture Overview](../../ARCHITECTURE.md)** *(if exists)*
- **[Developer Guide](../../../../CONTRIBUTING.md)** *(if exists)*

#### External References
- **[GDB Remote Protocol Spec](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)**
- **[Android Debug Bridge (ADB)](https://developer.android.com/studio/command-line/adb)**
- **[pybind11 Documentation](https://pybind11.readthedocs.io/)**
- **[sol2 Documentation](https://sol2.readthedocs.io/)**
- **[Drogon Framework](https://drogon.org/)**

## License

Documentation is part of the unreal-ng project. See project LICENSE file.

## Contact

For questions, issues, or contributions related to control interfaces:
- GitHub Issues: [Project Issues Page]
- Documentation Issues: Tag with `documentation` label

---

**Last Updated**: January 3, 2026  
**Documentation Version**: 1.0  
**Project Status**: Active Development
