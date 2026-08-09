# Emulator Operations Reference

This document provides a comprehensive reference of all supported emulator operations across different interfaces: CLI, API, Lua, Python, and WebAPI. Focus is on programmatic operations that can be used in headless mode.

## Core Operations Reference Table

| **Category** | **Operation** | **CLI Command** | **API Method** | **Lua Interface** | **Python Interface** | **WebAPI Endpoint** | **Description** |
|--------------|---------------|-----------------|----------------|-------------------|---------------------|-------------------|-----------------|
| **Emulator Management** | Create Emulator | N/A | `EmulatorManager::CreateEmulator()` | N/A | N/A | `POST /api/v1/emulator` | Create new emulator instance |
| | List Emulators | `list` | `EmulatorManager::GetEmulatorIds()` | N/A | N/A | `GET /api/v1/emulator` | List all emulator instances |
| | Select Emulator | `select <id>` | `EmulatorManager::GetEmulator()` | N/A | N/A | N/A | Select active emulator |
| | Remove Emulator | N/A | `EmulatorManager::RemoveEmulator()` | N/A | N/A | `DELETE /api/v1/emulator/{id}` | Remove emulator instance |
| | Get Status | `status` | `Emulator::GetState()` | N/A | N/A | `GET /api/v1/emulator/status` | Get emulator status |
| **Emulator Control** | Start | N/A | `Emulator::Start()` | `emulator:start()` | N/A | `POST /api/v1/emulator/{id}/start` | Start emulation |
| | Start Async | N/A | `Emulator::StartAsync()` | N/A | N/A | `POST /api/v1/emulator/{id}/start` | Start emulation asynchronously |
| | Stop | N/A | `Emulator::Stop()` | `emulator:stop()` | N/A | `POST /api/v1/emulator/{id}/stop` | Stop emulation |
| | Pause | `pause` | `Emulator::Pause()` | `emulator:pause()` | N/A | `POST /api/v1/emulator/{id}/pause` | Pause emulation |
| | Resume | `resume` | `Emulator::Resume()` | `emulator:resume()` | N/A | `POST /api/v1/emulator/{id}/resume` | Resume emulation |
| | Reset | `reset` | `Emulator::Reset()` | N/A | N/A | N/A | Reset emulator state |
| | Step | `step`, `stepin` | `Emulator::RunSingleCPUCycle()` | N/A | N/A | N/A | Execute single CPU instruction |
| | Steps | `steps <count>` | `Emulator::RunNCPUCycles()` | N/A | N/A | N/A | Execute 1 to N CPU instructions |
| | Step Over | `stepover` | `Emulator::StepOver()` | N/A | N/A | N/A | Execute instruction, skip calls and subroutines |
| | Step Out | N/A | `Emulator::RunUntilCondition()` | N/A | N/A | N/A | Execute until return |
| | Frame Step | N/A | `Emulator::RunNCPUCycles()` | N/A | N/A | N/A | Execute one frame |
| | Wait Interrupt | N/A | `BreakpointManager::AddExecutionBreakpoint()` | N/A | N/A | N/A | Wait for interrupt |
| **Debug Control** | Debug On | N/A | `Emulator::DebugOn()` | N/A | N/A | N/A | Enable debug mode |
| | Debug Off | N/A | `Emulator::DebugOff()` | N/A | N/A | N/A | Disable debug mode |
| | Debug Mode Toggle | `debugmode <on\|off>` | N/A | N/A | N/A | N/A | Toggle debug memory mode |
| **Memory Operations** | Read Memory | `memory <addr>` | `Memory::ReadByte()` | N/A | N/A | N/A | Read memory byte |
| | Write Memory | N/A | `Memory::WriteByte()` | N/A | N/A | N/A | Write memory byte |
| | Read Memory Block | N/A | `Memory::ReadBlock()` | N/A | N/A | N/A | Read memory block |
| | Write Memory Block | N/A | `Memory::WriteBlock()` | N/A | N/A | N/A | Write memory block |
| | Memory Counters | `memcounters`, `memstats` | `MemoryAccessTracker::GetStatistics()` | N/A | N/A | N/A | Show memory access statistics |
| | Get Z80 Bank Read Count | N/A | `MemoryAccessTracker::GetZ80BankReadAccessCount(bank)` | N/A | N/A | N/A | Get read access count for Z80 bank (banks: 0-3) |
| | Get Z80 Bank Write Count | N/A | `MemoryAccessTracker::GetZ80BankWriteAccessCount(bank)` | N/A | N/A | N/A | Get write access count for Z80 bank (banks: 0-3) |
| | Get Z80 Bank Execute Count | N/A | `MemoryAccessTracker::GetZ80BankExecuteAccessCount(bank)` | N/A | N/A | N/A | Get execute access count for Z80 bank (banks: 0-3) |
| | Get Z80 Bank Total Count | N/A | `MemoryAccessTracker::GetZ80BankTotalAccessCount(bank)` | N/A | N/A | N/A | Get total access count for Z80 bank (banks: 0-3) |
| | Get Page Read Count | N/A | `MemoryAccessTracker::GetPageReadAccessCount(page)` | N/A | N/A | N/A | Get read access count for memory page (pages: 0-MAX_PAGES) |
| | Get Page Write Count | N/A | `MemoryAccessTracker::GetPageWriteAccessCount(page)` | N/A | N/A | N/A | Get write access count for memory page (pages: 0-MAX_PAGES) |
| | Get Page Execute Count | N/A | `MemoryAccessTracker::GetPageExecuteAccessCount(page)` | N/A | N/A | N/A | Get execute access count for memory page (pages: 0-MAX_PAGES) |
| | Get Page Total Count | N/A | `MemoryAccessTracker::GetPageTotalAccessCount(page)` | N/A | N/A | N/A | Get total access count for memory page (pages: 0-MAX_PAGES) |
| | Check Memory Activity | N/A | `MemoryAccessTracker::HasActivity(start, end)` | N/A | N/A | N/A | Check if any activity in address range |
| | Reset Memory Counters | N/A | `MemoryAccessTracker::ResetCounters()` | N/A | N/A | N/A | Reset all memory access counters |
| | Save Memory Access Data | N/A | `MemoryAccessTracker::SaveAccessData(path, format, singleFile, filterPages)` | N/A | N/A | N/A | Save memory access data (formats: yaml) |
| | Generate Region Report | N/A | `MemoryAccessTracker::GenerateRegionReport()` | N/A | N/A | N/A | Generate monitored regions report |
| | Generate Port Report | N/A | `MemoryAccessTracker::GeneratePortReport()` | N/A | N/A | N/A | Generate monitored ports report |
| | Generate Segment Report | N/A | `MemoryAccessTracker::GenerateSegmentReport()` | N/A | N/A | N/A | Generate segmented tracking report |
| | Get Memory Bank | N/A | `Memory::GetCurrentBank()` | N/A | N/A | N/A | Get current memory bank |
| | Set Memory Bank | N/A | `Memory::SetBank()` | N/A | N/A | N/A | Set memory bank |
| **Register Operations** | Get Registers | `registers` | `Z80::GetRegisters()` | N/A | N/A | N/A | Get CPU register values |
| | Set Registers | N/A | `Z80::SetRegisters()` | N/A | N/A | N/A | Set CPU register values |
| | Get Single Register | N/A | `Z80::GetRegister()` | N/A | N/A | N/A | Get specific register value |
| | Set Single Register | N/A | `Z80::SetRegister()` | N/A | N/A | N/A | Set specific register value |
| **Breakpoint Management** | Set Execution Breakpoint | `bp <addr> [note]` | `BreakpointManager::AddExecutionBreakpoint()` | N/A | N/A | N/A | Set execution breakpoint |
| | Set Memory Read Breakpoint | N/A | `BreakpointManager::AddMemoryReadBreakpoint()` | N/A | N/A | N/A | Set memory read breakpoint |
| | Set Memory Write Breakpoint | N/A | `BreakpointManager::AddMemoryWriteBreakpoint()` | N/A | N/A | N/A | Set memory write breakpoint |
| | Set Memory Access Breakpoint | N/A | `BreakpointManager::AddMemoryAccessBreakpoint()` | N/A | N/A | N/A | Set memory access breakpoint |
| | Set Combined Memory Breakpoint | `wp <addr> <type> [note]` | `BreakpointManager::AddCombinedMemoryBreakpoint()` | N/A | N/A | N/A | Set memory watchpoint |
| | Set Port Read Breakpoint | N/A | `BreakpointManager::AddPortReadBreakpoint()` | N/A | N/A | N/A | Set port read breakpoint |
| | Set Port Write Breakpoint | N/A | `BreakpointManager::AddPortWriteBreakpoint()` | N/A | N/A | N/A | Set port write breakpoint |
| | Set Combined Port Breakpoint | `bport <port> <type> [note]` | `BreakpointManager::AddCombinedPortBreakpoint()` | N/A | N/A | N/A | Set port breakpoint |
| | List Breakpoints | `bplist [group]` | `BreakpointManager::GetBreakpointListAsString()` | N/A | N/A | N/A | List all breakpoints |
| | Get Breakpoint by ID | N/A | `BreakpointManager::GetBreakpointByID()` | N/A | N/A | N/A | Get specific breakpoint |
| | Remove Breakpoint | `bpclear <option>` | `BreakpointManager::RemoveBreakpointByID()` | N/A | N/A | N/A | Remove breakpoint |
| | Remove All Breakpoints | N/A | `BreakpointManager::RemoveAllBreakpoints()` | N/A | N/A | N/A | Remove all breakpoints |
| | Activate Breakpoint | `bpon <option>` | `BreakpointManager::ActivateBreakpoint()` | N/A | N/A | N/A | Activate breakpoint |
| | Deactivate Breakpoint | `bpoff <option>` | `BreakpointManager::DeactivateBreakpoint()` | N/A | N/A | N/A | Deactivate breakpoint |
| | Get Breakpoint Groups | `bpgroup <command>` | `BreakpointManager::GetBreakpointGroups()` | N/A | N/A | N/A | Get breakpoint groups |
| | Add Breakpoint Group | N/A | `BreakpointManager::AddBreakpointGroup()` | N/A | N/A | N/A | Add breakpoint group |
| | Remove Breakpoint Group | N/A | `BreakpointManager::RemoveBreakpointGroup()` | N/A | N/A | N/A | Remove breakpoint group |
| **File Operations** | Load ROM | N/A | `Emulator::LoadROM()` | N/A | N/A | N/A | Load ROM file |
| | Load Snapshot | N/A | `Emulator::LoadSnapshot()` | N/A | N/A | N/A | Load snapshot file (formats: SNA, Z80) |
| | Save Snapshot | N/A | `LoaderSNA::save()`, `LoaderZ80::save()` | N/A | N/A | N/A | Save snapshot (formats: SNA, Z80) |
| | Load Tape | N/A | `Emulator::LoadTape()` | N/A | N/A | N/A | Load tape file (formats: TAP) |
| | Save Tape | N/A | `Tape::handlePortOut()` | N/A | N/A | N/A | Save tape recording (when emulated system saves) |
| | Load Disk | N/A | `Emulator::LoadDiskToDrive(drive)` | N/A | N/A | N/A | Load disk image (drives: 0-3, formats: TRD, SCL) |
| | Save Disk | N/A | `LoaderTRD::writeImage()`, `LoaderSCL::writeImage()` | N/A | N/A | N/A | Save disk image (formats: TRD, SCL) |
| | Eject Disk | N/A | `FDD::ejectDisk()` | N/A | N/A | N/A | Eject disk from drive (drives: 0-3) |
| | Get Disk Status | N/A | `FDD::isDiskInserted()` | N/A | N/A | N/A | Check if disk is inserted (drives: 0-3) |
| | Get Disk Info | N/A | `DiskImage::getCylinders()`, `DiskImage::getSides()` | N/A | N/A | N/A | Get disk image information |
| | Validate Disk | N/A | `LoaderTRD::validateTRDOSImage()` | N/A | N/A | N/A | Validate disk image integrity |
| | Format Disk | N/A | `LoaderTRD::format()` | N/A | N/A | N/A | Format disk image |
| | Open File | `open <file>` | N/A | N/A | N/A | N/A | Open file (generic) |
| **Feature Management** | List Features | `feature` | `FeatureManager::getAllFeatures()` | N/A | N/A | N/A | List all features |
| | Enable Feature | `feature <name> on` | `FeatureManager::setFeature()` | N/A | N/A | N/A | Enable a feature |
| | Disable Feature | `feature <name> off` | `FeatureManager::setFeature()` | N/A | N/A | N/A | Disable a feature |
| | Set Feature Mode | `feature <name> mode <mode>` | `FeatureManager::setMode()` | N/A | N/A | N/A | Set feature mode |
| | Get Feature State | N/A | `FeatureManager::getFeature()` | N/A | N/A | N/A | Get feature state |
| | Save Features | `feature save` | `FeatureManager::saveToFile()` | N/A | N/A | N/A | Save feature settings |
| | Load Features | N/A | `FeatureManager::loadFromFile()` | N/A | N/A | N/A | Load feature settings |
| **Analysis Tools** | Get Call Trace | `calltrace <command>` | `CallTraceBuffer::GetLatestEvents()` | N/A | N/A | N/A | Get call trace data |
| | Clear Call Trace | N/A | `CallTraceBuffer::Clear()` | N/A | N/A | N/A | Clear call trace buffer |
| | Get Call Trace Statistics | N/A | `CallTraceBuffer::GetStatistics()` | N/A | N/A | N/A | Get call trace statistics |
| **Disassembly** | Disassemble Address | N/A | `Z80Disassembler::Disassemble()` | N/A | N/A | N/A | Disassemble Z80 instruction |
| | Get Instruction Length | N/A | `Z80Disassembler::GetInstructionLength()` | N/A | N/A | N/A | Get instruction byte length |
| | Format Instruction | N/A | `Z80Disassembler::FormatInstruction()` | N/A | N/A | N/A | Format disassembled instruction |
| | Disassemble Block | N/A | `Z80Disassembler::DisassembleBlock()` | N/A | N/A | N/A | Disassemble memory block |
| | Disassemble Single Command | N/A | `Z80Disassembler::disassembleSingleCommand()` | N/A | N/A | N/A | Disassemble single instruction |
| | Disassemble with Runtime | N/A | `Z80Disassembler::disassembleSingleCommandWithRuntime()` | N/A | N/A | N/A | Disassemble with runtime analysis |
| | Get Runtime Hints | N/A | `Z80Disassembler::getRuntimeHints()` | N/A | N/A | N/A | Get runtime analysis hints |
| | Get Command Annotation | N/A | `Z80Disassembler::getCommandAnnotation()` | N/A | N/A | N/A | Get instruction annotation |
| | Decode Instruction | N/A | `Z80Disassembler::decodeInstruction()` | N/A | N/A | N/A | Decode instruction details |
| | Get Opcode | N/A | `Z80Disassembler::getOpcode()` | N/A | N/A | N/A | Get opcode information |
| | Check Should Step Over | N/A | `Z80Disassembler::shouldStepOver()` | N/A | N/A | N/A | Check if instruction should be stepped over |
| | Get Next Instruction Address | N/A | `Z80Disassembler::getNextInstructionAddress()` | N/A | N/A | N/A | Get next instruction address |
| | Get Step Over Exclusion Ranges | N/A | `Z80Disassembler::getStepOverExclusionRanges()` | N/A | N/A | N/A | Get step-over exclusion ranges |
| | Analyze Called Function | N/A | `Z80Disassembler::analyzeCalledFunction()` | N/A | N/A | N/A | Analyze function for step-over |
| | Get Opcode Flags | N/A | `OpFlags::getFlagName()` | N/A | N/A | N/A | Get opcode flag names |
| | Get Flag Descriptions | N/A | `OpFlags::getFlagDescription()` | N/A | N/A | N/A | Get opcode flag descriptions |
| **Label Management** | Add Label | N/A | `LabelManager::AddLabel()` | N/A | N/A | N/A | Add debug symbol/label |
| | Remove Label | N/A | `LabelManager::RemoveLabel()` | N/A | N/A | N/A | Remove debug symbol/label |
| | Update Label | N/A | `LabelManager::UpdateLabel()` | N/A | N/A | N/A | Update label properties |
| | Get Label by Name | N/A | `LabelManager::GetLabelByName()` | N/A | N/A | N/A | Find label by name |
| | Get Label by Address | N/A | `LabelManager::GetLabelByZ80Address()` | N/A | N/A | N/A | Find label by address |
| | Get All Labels | N/A | `LabelManager::GetAllLabels()` | N/A | N/A | N/A | Get all labels |
| | Load Labels from File | N/A | `LabelManager::LoadLabels()` | N/A | N/A | N/A | Load labels from various formats |
| | Save Labels to File | N/A | `LabelManager::SaveLabels()` | N/A | N/A | N/A | Save labels to various formats |
| **System Information** | Get Statistics | N/A | `Emulator::GetStatistics()` | N/A | N/A | N/A | Get emulator statistics |
| | Get Instance Info | N/A | `Emulator::GetInstanceInfo()` | N/A | N/A | N/A | Get detailed instance info |
| | Get System Info | N/A | `Emulator::GetSystemInfo()` | N/A | N/A | N/A | Get system information |
| | Get Speed | N/A | `Emulator::GetSpeed()` | N/A | N/A | N/A | Get emulator speed |
| | Set Speed | N/A | `Emulator::SetSpeed()` | N/A | N/A | N/A | Set emulator speed |
| | Get CPU State | N/A | `Z80::GetCPUState()` | N/A | N/A | N/A | Get CPU state information |
| | Get Memory Layout | N/A | `Memory::GetMemoryLayout()` | N/A | N/A | N/A | Get memory layout information |
| **Port Operations** | Read Port | N/A | `Z80::ReadPort()` | N/A | N/A | N/A | Read from I/O port |
| | Write Port | N/A | `Z80::WritePort()` | N/A | N/A | N/A | Write to I/O port |
| **Utility** | Help | `help`, `?` | N/A | N/A | N/A | N/A | Show help information |
| | Exit/Quit | `exit`, `quit` | N/A | N/A | N/A | N/A | Exit CLI session |
| | Dummy | `dummy` | N/A | N/A | N/A | N/A | Silent initialization command |

## Interface Coverage Summary

### CLI Interface
- **Most Comprehensive**: Supports all core operations with text-based commands
- **Features**: Full breakpoint management, memory operations, feature toggles, analysis tools
- **Strengths**: Complete command set, good for automation and scripting
- **Headless Ready**: Fully functional without GUI

### WebAPI Interface
- **Current Coverage**: Basic emulator lifecycle management
- **Missing**: Debug operations, memory access, breakpoints, features, analysis tools
- **Strengths**: RESTful design, good for web-based control
- **Headless Ready**: Designed for headless operation

### Lua Interface
- **Current Coverage**: Minimal - only basic emulator control
- **Missing**: Most debug operations, memory access, breakpoints, features
- **Strengths**: Scripting integration, but needs significant expansion
- **Headless Ready**: Can be used in headless mode

### Python Interface
- **Current Coverage**: Very limited - mainly script execution
- **Missing**: Almost all operations
- **Strengths**: Python ecosystem integration, but requires major implementation
- **Headless Ready**: Can be used in headless mode

### API Methods (C++)
- **Complete Coverage**: All operations available through direct method calls
- **Strengths**: Full access to all functionality, best performance
- **Headless Ready**: Fully functional without GUI

## Core Programmatic Capabilities

### Emulator Lifecycle Management
- Create, configure, start, stop, pause, resume emulator instances
- Manage multiple emulator instances
- Get detailed status and statistics

### Memory Operations
- Read/write individual bytes and blocks
- Memory bank management
- Memory access tracking and statistics
- Memory layout information

### CPU Control and Debugging
- Step-by-step execution (single cycle, instruction, frame)
- Register access (read/write individual registers)
- CPU state information
- Execution control (start, stop, pause, resume)

### Breakpoint System
- Execution breakpoints
- Memory read/write/access breakpoints
- Port read/write breakpoints
- Breakpoint groups and management
- Conditional breakpoints

### Analysis and Debugging Tools
- Call trace analysis
- Memory access statistics
- Disassembly engine
- Label/symbol management
- Feature toggling system

### Advanced Disassembly Capabilities
- **Basic Disassembly**: Disassemble single instructions and memory blocks
- **Runtime Analysis**: Disassemble with runtime state analysis
- **Instruction Decoding**: Detailed instruction analysis with opcode flags
- **Step-Over Support**: Advanced debugging with step-over functionality
- **Function Analysis**: Analyze called functions for debugging
- **Runtime Hints**: Get runtime analysis hints and annotations
- **Opcode Flags**: Comprehensive opcode flag system for instruction classification
- **Instruction Classification**: Identify instruction types (jumps, calls, I/O, etc.)

### File Operations
- Load ROM, snapshots, tapes, disks
- Save snapshots
- Label file management

### System Information
- Detailed statistics
- System information
- Performance metrics
- Memory layout details

## Advanced Disassembly Features

### Instruction Decoding
- **OpCode Structure**: Complete opcode information with flags, T-states, and mnemonics
- **DecodedInstruction**: Rich instruction representation with runtime data
- **Flag System**: Comprehensive flag system for instruction classification
- **Runtime Analysis**: Instruction analysis with current CPU state

### Step-Over Debugging Support
- **Should Step Over**: Determine if instruction should be stepped over
- **Exclusion Ranges**: Get address ranges to exclude during step-over
- **Function Analysis**: Analyze called functions for debugging
- **Next Instruction Address**: Calculate next instruction address

### Runtime Analysis
- **Runtime Hints**: Get runtime analysis hints for instructions
- **Command Annotations**: Generate instruction annotations
- **Jump Addresses**: Calculate jump and call target addresses
- **Displacement Addresses**: Handle indexed addressing modes

### Opcode Classification
- **Basic Flags**: Prefix, memory operations, control flow
- **Memory Flags**: Byte/word operands, displacement, memory addresses
- **Flow Flags**: Conditions, relative jumps, absolute jumps, returns
- **CPU Flags**: Flag modification, specific flag groups
- **Operation Flags**: Variable cycles, register exchange, block operations, I/O, interrupts

## Recommendations for Complete Headless Support

### 1. Extend Lua Interface
- Add bindings for all CLI operations
- Implement debug operations (breakpoints, memory access)
- Add feature management support
- Include analysis tools integration
- Add comprehensive disassembly capabilities
- Ensure all operations work in headless mode

### 2. Extend Python Interface
- Implement comprehensive Python bindings
- Add all core emulator operations
- Include debug functionality
- Provide high-level automation APIs
- Add advanced disassembly features
- Ensure headless compatibility

### 3. Extend WebAPI Interface
- Add endpoints for debug operations
- Implement memory access endpoints
- Add breakpoint management endpoints
- Include feature management endpoints
- Add analysis tools endpoints
- Add disassembly endpoints with runtime analysis
- Ensure RESTful design for headless operation

### 4. Standardize Across Interfaces
- Ensure all interfaces support the same core operations
- Maintain consistent naming conventions
- Provide similar error handling patterns
- Document all interfaces uniformly
- Verify headless operation capability
- Include advanced disassembly features across all interfaces

## Headless Mode Requirements

### Essential Operations for Headless Mode
1. **Emulator Management**: Create, configure, start, stop
2. **Memory Operations**: Read, write, analyze
3. **CPU Control**: Step execution, register access
4. **Breakpoint System**: Set, manage, trigger
5. **Analysis Tools**: Call trace, statistics, disassembly
6. **Advanced Disassembly**: Runtime analysis, step-over support, instruction decoding
7. **File Operations**: Load/save various formats
8. **Feature Management**: Toggle debugging features
9. **System Information**: Get status, statistics, layout

### Interface Priorities for Headless Mode
1. **CLI Interface**: Already complete and headless-ready
2. **API Methods**: Complete and headless-ready
3. **WebAPI Interface**: Good foundation, needs expansion
4. **Lua Interface**: Needs major expansion
5. **Python Interface**: Needs complete implementation

## Notes

- **CLI Interface**: Most mature and complete implementation, fully headless-ready
- **WebAPI Interface**: Good foundation but needs significant expansion for full headless support
- **Lua Interface**: Minimal implementation, requires major work for headless automation
- **Python Interface**: Very basic, needs complete implementation for headless automation
- **API Methods**: Complete and fully functional, ideal for headless integration
- **Advanced Disassembly**: Comprehensive Z80 disassembly with runtime analysis and debugging support

This document serves as a single source of truth for all programmatic emulator operations and can guide the implementation of missing functionality across the different interfaces for complete headless mode support. 