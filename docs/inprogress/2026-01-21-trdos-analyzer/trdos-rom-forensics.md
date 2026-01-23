# TR-DOS ROM Forensics Analysis

> **Document Purpose**: Detailed ROM analysis findings for TR-DOS command detection, filename extraction, and diagnostic analyzer implementation.
>
> **Source ROM**: TR-DOS 5.04T
> **Analysis Date**: 2026-01-23

---

## Executive Summary

This document captures critical findings from TR-DOS ROM forensic analysis, including:
- Command detection via C register at `$3D13` service entry
- Filename extraction from `$5CDD` system variable
- Automatic "boot" injection mechanism for bare `RUN` command
- Breakpoint addresses for capturing TR-DOS operations

---

## 1. TR-DOS Entry Points

### 1.1 Primary Entry Points

| Address | Hex | Name | Purpose |
|---------|-----|------|---------|
| **15616** | `$3D00` | TRDOS_ENTRY | Standard TR-DOS entry from BASIC |
| **15635** | `$3D13` | SERVICE_ENTRY | Service dispatcher, **C register = command code** |
| **15649** | `$3D21` | COMMAND_DISPATCH | Internal command dispatch (C may be invalid here) |
| **119** | `$0077` | EXIT | Return to BASIC/SOS ROM |

### 1.2 Critical Discovery: C Register at $3D13

At `$3D13` (SERVICE_ENTRY), the Z80 C register contains the TR-DOS service/command number:

| C Value | Command | Description |
|---------|---------|-------------|
| `$01` | LOAD | Load file from disk |
| `$02` | SAVE | Save file to disk |
| `$03` | VERIFY | Verify file against disk |
| `$04` | MERGE | Merge BASIC program |
| `$05` | **RUN** | Load and execute file |
| `$06` | ERASE | Delete file from disk |
| `$07` | MOVE | Move/rename file |
| `$09` | COPY | Copy file |
| `$0A` | FORMAT | Format disk |
| `$0F` | CAT | List directory |

> **Hazard #138 - Wrong Entry Point**: At `$3D21`, the C register may NOT contain a valid command. Command detection must only occur at `$3D13`.

---

## 2. Filename System Variables

### 2.1 Correct Filename Address: `$5CDD`

TR-DOS stores the parsed filename for current operations at:

| Address | Hex | Size | Name | Description |
|---------|-----|------|------|-------------|
| **23773** | `$5CDD` | 8 bytes | FILE_NAME | Current filename (8 chars, space-padded) |
| **23781** | `$5CE5` | 1 byte | FILE_TYPE | File extension (B, C, D, #) |

### 2.2 Common Mistake: `$5CD1` is NOT the Filename

Address `$5CD1` (23761) is the **autostart line number** for BASIC programs, NOT the filename!

```cpp
// WRONG - reads autostart line area
event.filename = readFilenameFromMemory(0x5CD1);

// CORRECT - reads filename area
event.filename = readFilenameFromMemory(0x5CDD);
```

### 2.3 Filename Setup Routine (`$1C57`)

The TR-DOS ROM routine at `$1C57` sets up the filename:

```asm
x1C57:  LD      HL,#5CDD    ; Address of filename variable
        LD      B,8         ; 8 characters max
x1C5C:  LD      (HL)," "    ; Initialize with spaces
        INC     HL
        DJNZ    x1C5C
        ; ... parse filename from command line
        LD      DE,#5CDD    ; Copy filename here
        LDIR                ; Copy filename characters
```

---

## 3. Automatic "boot" Injection (RUN with No Parameters)

### 3.1 Discovery

When user types bare `RUN` (no filename), TR-DOS **does NOT** store "boot" in ROM as a string. Instead, it **dynamically builds** the command `RUN "boot"` character-by-character in the BASIC command buffer.

### 3.2 ROM Code at `$027B`

```asm
x027B:  LD      HL,($5C59)  ; Get E_LINE (command buffer address)
        LD      A,#FE       ; Flag: boot file was run
        LD      (#5D0E),A
        LD      (HL),#F7    ; RUN token
        INC     HL
        LD      (HL),#22    ; Quote "
        INC     HL
        LD      (HL),"b"    ; 'b'
        INC     HL
        LD      (HL),"o"    ; 'o'
        INC     HL
        LD      (HL),"o"    ; 'o'
        INC     HL
        LD      (HL),"t"    ; 't'
        INC     HL
        LD      (HL),#22    ; Quote "
        INC     HL
        LD      (#5C5B),HL  ; Set K_CUR (cursor position)
        LD      (HL),#0D    ; ENTER terminator
        INC     HL
        LD      (HL),#80    ; End-of-area marker
        INC     HL
        LD      (#5C61),HL  ; Set WORKSP
        LD      (#5C63),HL  ; Set STKBOT
        LD      (#5C65),HL  ; Set STKEND
        SET     3,(IY+1)    ; Set L mode flag (FLAGS bit 3) ← CRITICAL!
        ; ... then jump to execute the command
```

### 3.3 Critical Flag: L Mode (FLAGS bit 3)

The ROM sets **FLAGS bit 3** (L mode) at the end of injection:
```asm
SET     3,(IY+1)    ; IY+1 = FLAGS ($5C0B = 23611)
```

This ensures extended character entry mode is active. The L mode flag affects:
- Keyboard input interpretation during ENTER processing
- Extended key combinations (SYMBOL SHIFT + key)

**Implementation in BasicEncoder:**
```cpp
uint8_t flags = memory->DirectReadFromZ80Memory(FLAGS);
memory->DirectWriteToZ80Memory(FLAGS, flags | 0x08);  // Set bit 3
```

### 3.4 Execution Flow

```
User types: RUN [ENTER]
    ↓
TR-DOS detects bare RUN command
    ↓
ROM at $027B injects: RUN "boot" into E_LINE buffer
    ↓
Command is parsed and executed normally
    ↓
At $3D13: C = 0x05 (RUN), filename "boot" at $5CDD
```

### 3.5 Startup Auto-Boot Detection

TR-DOS checks `$5B00` at startup:
- If `$5B00 == $AA` → Auto-run "boot" file
- This is how `RANDOMIZE USR 15616` triggers boot execution

```asm
x0271:  LD      A,(#5B00)   ; Check auto-boot flag
        CP      #AA
        JR      NZ,x02CB    ; Skip if not set
        ; ... proceed to inject RUN "boot"
```

---

## 4. Breakpoint Addresses for Forensic Capture

### 4.1 Command Flow Breakpoints

| Address | Hex | When | What to Capture |
|---------|-----|------|-----------------|
| `$3D00` | 15616 | TR-DOS entry | `TRDOS_ENTRY` event |
| `$3D13` | 15635 | Service dispatch | **C = command**, filename at `$5CDD` |
| `$027B` | 635 | Boot injection | Before "boot" is injected |
| `$1C57` | 7255 | Filename setup | Before filename copied to `$5CDD` |
| `$1CB0` | 7344 | File search | After filename parsed, before catalog scan |
| `$1D4D` | 7501 | RUN handler | RUN command about to execute |
| `$0077` | 119 | Exit | TR-DOS returning control |

### 4.2 File Operation Breakpoints

| Address | Hex | Purpose |
|---------|-----|---------|
| `$1836` | 6198 | Load file parameter setup |
| `$18AB` | 6315 | After file loaded, before execution |
| `$1AD0` | 6864 | SAVE command handler |
| `$1EC2` | 7874 | FORMAT command handler |

---

## 5. State Machine for Command Detection

### 5.1 Analyzer State Transitions

```
IDLE ──[$3D00]──▶ IN_TRDOS ──[$3D13]──▶ IN_COMMAND ──[$0077]──▶ IN_TRDOS
  │                   │                      │
  │                   │                      │
  └─────────[$3D13]───┘ (implied entry)      └──(internal calls OK)──┘
```

### 5.2 Command Deduplication Logic

```cpp
void TRDOSAnalyzer::handleCommandDispatch(uint16_t address, Z80* cpu)
{
    // Only emit COMMAND_START at $3D13 (where C register is valid)
    if (address != BP_SERVICE_ENTRY)
        return;
    
    // Don't emit for internal service calls during command execution
    if (_state == TRDOSAnalyzerState::IN_COMMAND)
        return;
    
    // Now safe to identify command from C register
    _currentCommand = identifyCommand(cpu);  // reads cpu->c
    // Emit COMMAND_START event...
}
```

---

## 6. Memory Layout Reference

### 6.1 TR-DOS System Variables (`$5CB6`-`$5D32`)

| Address | Name | Size | Description |
|---------|------|------|-------------|
| `$5CB6` | IF1_FLAG | 1 | Interface 1 detection |
| `$5CC2` | RET_INSTR | 1 | Contains `$C9` (RET) when TR-DOS initialized |
| `$5CC8` | DRIVE_A_MODE | 1 | Drive A configuration flags |
| `$5CDD` | FILE_NAME | 8 | **Current filename** |
| `$5CE5` | FILE_TYPE | 1 | File type (B, C, D, #) |
| `$5CE6` | FILE_START | 2 | Start address / autorun line |
| `$5CE8` | FILE_LENGTH | 2 | Length in bytes |
| `$5D0F` | ERROR_CODE | 1 | Last error code |
| `$5D19` | DEFAULT_DRIVE | 1 | Default drive (0=A, 1=B, 2=C, 3=D) |

### 6.2 SOS (BASIC) System Variables Used by TR-DOS

| Address | Name | Purpose in TR-DOS |
|---------|------|-------------------|
| `$5C59` | E_LINE | Command buffer address |
| `$5C5B` | K_CUR | Cursor position |
| `$5C61` | WORKSP | Workspace pointer |
| `$5C5D` | CH_ADD | Character address (for LINE-SCAN) |

---

## 7. Hazards & Lessons Learned

### Hazard #137 - Command Over-reporting (Resolved)
**Problem**: Multiple `COMMAND_START` events for single user command.
**Cause**: TR-DOS makes internal `$3D13` service calls during command execution.
**Fix**: Only emit `COMMAND_START` when transitioning from `IDLE` or `IN_TRDOS` state.

### Hazard #138 - Wrong Entry Point for Command Detection (Resolved)
**Problem**: `COMMAND_START` showing `UNKNOWN` command.
**Cause**: Reading C register at `$3D21` where it doesn't contain valid command.
**Fix**: Only read C register at `$3D13` service entry.

### Hazard #139 - Wrong Filename Address (Resolved)
**Problem**: Filename showing garbage like "5616".
**Cause**: Reading from `$5CD1` (autostart line) instead of `$5CDD` (filename).
**Fix**: Read filename from `$5CDD`.

---

## 8. Related Files

- **ROM Disassembly**: `/docs/rom/trdos504t.asm.txt`
- **System Variables**: `/docs/inprogress/2026-01-21-trdos-analyzer/materials/book-trdos/trdos-system-variables.json`
- **Analyzer Implementation**: `/core/src/debugger/analyzers/trdos/trdosanalyzer.cpp`
- **WebAPI Serialization**: `/core/automation/webapi/src/api/analyzers_api.cpp`

---

## Appendix A: C Register Command Mapping (identifyCommand)

```cpp
TRDOSCommand TRDOSAnalyzer::identifyCommand(uint16_t address, Z80* cpu)
{
    if (!cpu) return TRDOSCommand::UNKNOWN;
    
    uint8_t cmdByte = cpu->c;  // Read C register at $3D13
    
    switch (cmdByte)
    {
        case 0x01: return TRDOSCommand::LOAD;
        case 0x02: return TRDOSCommand::SAVE;
        case 0x03: return TRDOSCommand::VERIFY;
        case 0x04: return TRDOSCommand::MERGE;
        case 0x05: return TRDOSCommand::RUN;
        case 0x06: return TRDOSCommand::ERASE;
        case 0x07: return TRDOSCommand::MOVE;
        case 0x09: return TRDOSCommand::COPY;
        case 0x0A: return TRDOSCommand::FORMAT;
        case 0x0F: return TRDOSCommand::CAT;
        default:   return TRDOSCommand::UNKNOWN;
    }
}
```
