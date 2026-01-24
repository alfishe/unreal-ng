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

### 1.2 Critical Discovery: C Register at $3D13 Contains SERVICE CODES

> ⚠️ **HAZARD #140**: The C register at `$3D13` contains **low-level disk SERVICE codes**, NOT user command identifiers! These are API calls for machine code programs.

At `$3D13` (SERVICE_ENTRY), the Z80 C register contains the TR-DOS **service/function** number:

| C Value | Service Name | Description | Registers |
|---------|--------------|-------------|-----------|
| `$00` | **Restore** | Move head to Track 0 | - |
| `$01` | **Select Drive** | Select drive 0-3 | A = drive |
| `$02` | **Seek Track** | Move head to track | A = track |
| `$03` | **Set Sector** | Set sector register | A = sector |
| `$04` | **Set DMA** | Set transfer address | HL = address |
| `$05` | **Read Sectors** | Read sectors from disk | D=track, E=sector, B=count, HL=dest |
| `$06` | **Write Sectors** | Write sectors to disk | D=track, E=sector, B=count, HL=src |
| `$07` | **Catalog** | Print directory | A = stream |
| `$08` | **Read Descriptor** | Read catalog entry N | A = entry (0-127) |
| `$09` | **Write Descriptor** | Write catalog entry | A = entry |
| `$0A` | **Find File** | Search for file in `$5CDD` | Returns C=entry, Z=found |
| `$0B` | **Save File** | Save file to disk | HL=start, DE=length |
| `$0C` | **Save BASIC** | Save BASIC program | uses $5CDD |
| `$0D` | **Exit** | Return to BASIC | - |
| `$0E` | **Load File** | Load file | A=0:orig addr, A=3:HL addr |
| `$12` | **Delete Sector** | Internal delete op | - |
| `$13` | **Move Descriptor In** | Copy descriptor to $5CDD | HL=source |
| `$14` | **Move Descriptor Out** | Copy $5CDD to memory | HL=dest |
| `$15` | **Format Track** | Format single track | - |
| `$16` | **Select Side 0** | Select upper disk side | - |
| `$17` | **Select Side 1** | Select lower disk side | - |
| `$18` | **Read System Sector** | Read Track 0, Sec 9 | Updates free space vars |

### 1.3 Important: Service Codes ≠ User Commands!

**User commands** (RUN, LOAD, CAT) typed at the TR-DOS prompt are processed by the **Command Processor** at `$3D03`, NOT by the service dispatcher at `$3D13`.

When you type `RUN "game"`:
1. Command Processor parses the command at `$3D03`
2. It may call `$3D13` with C=`$0E` (Load File) internally
3. Then it may call `$3D13` with C=`$05` (Read Sectors) multiple times to load data

> **Reference**: See `/docs/inprogress/2026-01-21-trdos-analyzer/materials/detailed_trdos_internals.md` Section 5 for complete API documentation.

### 1.4 How to Detect USER COMMANDS (RUN, LOAD, CAT, etc.)

To detect **user-typed commands** (not low-level service calls), you must monitor the **Command Processor** entry points and read the tokenized command from memory.

#### Entry Points for User Commands

> ⚠️ **HAZARD #142**: When TR-DOS is **already active** at the `A>` prompt, user commands do NOT go through `$3D1A` or `$3D03`. They are processed by the **Internal Command Processor Loop** inside the ROM!

| Address | Hex | Name | Purpose |
|---------|-----|------|---------|
| **15619** | `$3D03` | Command Processor | Entry from BASIC (`RANDOMIZE USR 15619`) |
| **15642** | `$3D1A` | Command Entry | After init, before parsing (from BASIC ROM only!) |
| **778** | `$030A` | **Internal Dispatcher** | **Best Sniff Point** - A register = command token |
| **715** | `$02CB` | Command Loop Entry | Top of resident command processor loop |
| **751** | `$02EF` | After Tokenization | Command has been tokenized |

#### Detection Strategy (Updated)

There are **two scenarios** for detecting user commands:

**Scenario 1: Command from BASIC** (e.g., `RANDOMIZE USR 15619: REM: CAT`)
- Use breakpoint at `$3D1A`
- Read token from CH_ADD (`$5C5D`)

**Scenario 2: Command from A> prompt** (TR-DOS already active)
- Use breakpoint at **`$030A`** (Internal Command Dispatcher)
- Read token from **A register** directly
- The command has already been tokenized at this point

```asm
; Address #030A - Command Dispatcher (best sniff point)
x030A:  LD      A,(HL)      ; A = first token/character
        ...
        LD      HL,x2FF3    ; Address of Command Token Table
```

#### BASIC Token → TR-DOS Command Mapping

| Token | Hex | BASIC Keyword | TR-DOS Interpretation |
|-------|-----|---------------|----------------------|
| **CAT** | `$CF` | CAT | List directory |
| **SAVE** | `$F8` | SAVE | Save file |
| **LOAD** | `$EF` | LOAD | Load/RUN/MERGE* |
| **FORMAT** | `$D0` | FORMAT | Format disk |
| **MOVE** | `$D1` | MOVE | Rename file |
| **ERASE** | `$D2` | ERASE | Delete file |
| **COPY** | `$FF` | COPY | Copy file |
| **MERGE** | `$D5` | MERGE | Merge BASIC |
| **VERIFY** | `$D6` | VERIFY | Verify file |

> **Note**: LOAD, RUN, and MERGE all use token `$EF`. TR-DOS disambiguates them by examining the characters/context in the BASIC line via the parser at `$030A`.

#### Implementation Example

```cpp
void TRDOSAnalyzer::handleCommandEntry(Z80* cpu, Memory* memory)
{
    // Read CH_ADD from $5C5D (pointer to command token)
    uint16_t chAdd = memory->DirectReadFromZ80Memory(0x5C5D) |
                     (memory->DirectReadFromZ80Memory(0x5C5E) << 8);
    
    // Read the token at that address
    uint8_t token = memory->DirectReadFromZ80Memory(chAdd);
    
    // Map token to command
    switch (token)
    {
        case 0xCF: _userCommand = "CAT"; break;
        case 0xF8: _userCommand = "SAVE"; break;
        case 0xEF: _userCommand = "LOAD/RUN"; break;  // needs disambiguation
        case 0xD0: _userCommand = "FORMAT"; break;
        case 0xD1: _userCommand = "MOVE"; break;
        case 0xD2: _userCommand = "ERASE"; break;
        case 0xFF: _userCommand = "COPY"; break;
        default:   _userCommand = "UNKNOWN"; break;
    }
}
```

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

### 2.3 When to Read Filename (Command-Dependent)

Based on the TR-DOS ROM jump table at `x3008`, not all commands use filenames. Only read filename when appropriate:

| Handler Address | Command | Needs Filename? | Notes |
|-----------------|---------|-----------------|-------|
| `$0433` | CAT | **NO** | Directory listing |
| `$1018` | `*` (Drive) | **NO** | Drive letter only |
| `$1EC2` | FORMAT | **NO** | Uses disk label, not file |
| `$053A` | NEW/RENAME | **YES** | `$5CDD` |
| `$0787` | ERASE | **YES** | `$5CDD` |
| `$1815` | LOAD | **YES** | `$5CDD` |
| `$1AD0` | SAVE | **YES** | `$5CDD` |
| `$19B1` | MERGE | **YES** | `$5CDD` |
| `$1D4D` | RUN | **YES** | `$5CDD` |
| `$1810` | VERIFY | **YES** | `$5CDD` |
| `$0690` | COPY | **YES** | `$5CDD` |

#### Avoiding Garbage Data

TR-DOS initializes filename buffers with spaces (`0x20`), not nulls. Before reading:

1. **Check first byte of `$5CDD`**:
   - `0x00` = Empty/end of directory → skip
   - `0x01` = Deleted file marker → skip
   - `< 0x20` = Control character (garbage) → skip
   
2. **Only accept printable ASCII** (`0x20-0x7E`)

3. **Read extension** from `$5CE5` (1 byte: 'B', 'C', 'D', '#')

```cpp
// Example: Proper filename extraction
bool commandRequiresFilename(TRDOSUserCommand cmd) {
    switch (cmd) {
        case SAVE: case LOAD: case ERASE: case COPY:
        case MOVE: case MERGE: case VERIFY:
            return true;
        default:
            return false;  // CAT, FORMAT, etc.
    }
}
```

### 2.4 Filename Setup Routine (`$1C57`)

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

### Hazard #140 - Confusing Service Codes with User Commands (CRITICAL)
**Problem**: Thought C=0x05 at `$3D13` was "RUN" command, saw 8 "RUN" events.
**Cause**: **Service codes ≠ User commands!** C=0x05 is "Read Sectors", not RUN.
**Reality**: We were seeing a game loader reading disk sectors, not 8 RUN commands.
**Fix**: 
- Rename enum from `TRDOSCommand` to `TRDOSService`
- Update mapping to use correct service names
- To detect user commands, monitor `$3D03` (Command Processor) instead

---

## 8. Related Files

- **ROM Disassembly**: `/docs/rom/trdos504t.asm.txt`
- **System Variables**: `/docs/inprogress/2026-01-21-trdos-analyzer/materials/book-trdos/trdos-system-variables.json`
- **Analyzer Implementation**: `/core/src/debugger/analyzers/trdos/trdosanalyzer.cpp`
- **WebAPI Serialization**: `/core/automation/webapi/src/api/analyzers_api.cpp`

---

## Appendix A: C Register SERVICE Code Mapping

> ⚠️ **These are low-level disk API service codes, NOT user commands!**

```cpp
// CORRECTED: These are SERVICE codes for machine code API
TRDOSService TRDOSAnalyzer::identifyService(Z80* cpu)
{
    if (!cpu) return TRDOSService::UNKNOWN;
    
    uint8_t svcCode = cpu->c;  // Read C register at $3D13
    
    switch (svcCode)
    {
        case 0x00: return TRDOSService::RESTORE;        // Head to Track 0
        case 0x01: return TRDOSService::SELECT_DRIVE;   // Select drive A=0-3
        case 0x02: return TRDOSService::SEEK_TRACK;     // Seek to track A
        case 0x03: return TRDOSService::SET_SECTOR;     // Set sector register
        case 0x04: return TRDOSService::SET_DMA;        // Set transfer address HL
        case 0x05: return TRDOSService::READ_SECTORS;   // Read B sectors
        case 0x06: return TRDOSService::WRITE_SECTORS;  // Write B sectors
        case 0x07: return TRDOSService::CATALOG;        // Print directory
        case 0x08: return TRDOSService::READ_DESCRIPTOR; // Read catalog entry
        case 0x09: return TRDOSService::WRITE_DESCRIPTOR;// Write catalog entry
        case 0x0A: return TRDOSService::FIND_FILE;      // Search for file
        case 0x0B: return TRDOSService::SAVE_FILE;      // Save file
        case 0x0C: return TRDOSService::SAVE_BASIC;     // Save BASIC program
        case 0x0D: return TRDOSService::EXIT;           // Return to BASIC
        case 0x0E: return TRDOSService::LOAD_FILE;      // Load file
        case 0x12: return TRDOSService::DELETE_SECTOR;  // Internal delete
        case 0x13: return TRDOSService::MOVE_DESC_IN;   // Copy desc to $5CDD
        case 0x14: return TRDOSService::MOVE_DESC_OUT;  // Copy $5CDD to memory
        case 0x15: return TRDOSService::FORMAT_TRACK;   // Format track
        case 0x16: return TRDOSService::SELECT_SIDE_0;  // Upper side
        case 0x17: return TRDOSService::SELECT_SIDE_1;  // Lower side
        case 0x18: return TRDOSService::READ_SYS_SECTOR;// Read Track 0, Sec 9
        default:   return TRDOSService::UNKNOWN;
    }
}
```

## Appendix B: Detecting User Commands (Summary)

### Quick Reference

| Goal | Breakpoint | What to Read |
|------|------------|--------------|
| **User command typed** | `$3D1A` | Token at `($5C5D)` |
| **Low-level disk API** | `$3D13` | Service code in C register |

### User Command Detection Procedure

1. Set breakpoint at **`$3D1A`** (15642)
2. Read 2 bytes from **`$5C5D`** (CH_ADD) → this is a pointer
3. Read 1 byte from that pointer address → this is the **BASIC token**
4. Map token to command:
   - `$CF` = CAT
   - `$F8` = SAVE  
   - `$EF` = LOAD/RUN/MERGE (needs further disambiguation)
   - `$D0` = FORMAT
   - `$D1` = MOVE
   - `$D2` = ERASE

### Why $3D13 Shows Many "Read Sectors" Calls

When you type `RUN "game"`:
1. **One** command entry at `$3D1A` (the user's RUN command)
2. TR-DOS loads the game loader
3. Game loader bypasses BASIC and calls `$3D13` with C=`$05` (Read Sectors) **many times**
4. This is the game loading its data directly via the disk API

So: **1 user command → Many low-level service calls**
