# TR-DOS Analyzer: Test-Driven Design

## Architecture Overview

The TR-DOS analyzer follows a **two-layer architecture**:

```
┌─────────────────────────────────────────────────────────────┐
│                    Layer 2: Semantic Events                 │
│          (Post-processing / Pattern Recognition)            │
│                                                             │
│  • COMMAND_START (LIST, LOAD, SAVE, CAT, FORMAT)            │
│  • FILE_FOUND / FILE_NOT_FOUND                              │
│  • MODULE_LOAD / MODULE_SAVE                                │
│  • LOADER_DETECTED (custom loader recognition)              │
│  • PROTECTION_DETECTED                                      │
└──────────────────────────┬──────────────────────────────────┘
                           │ Aggregation
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Layer 1: Raw Events                      │
│               (Fast Capture / Full Context)                 │
│                                                             │
│  • RawFDCEvent (every WD1793 interaction)                   │
│  • RawBreakpointEvent (every ROM/RAM breakpoint hit)        │
│  • Full Z80 context (PC, SP, registers, IFF1/IM, stack)     │
│  • Full timing information)                                 │
└─────────────────────────────────────────────────────────────┘
```

## Design Principles

1. **Raw First**: Capture everything at Layer 1, aggregate at Layer 2
2. **No Data Loss**: Raw events contain complete context for offline analysis
3. **Fast Capture**: Layer 1 is hot-path optimized (minimal allocation, ring buffer)
4. **Lazy Aggregation**: Layer 2 can be computed on-demand or streamed
5. **API Parity**: All automation modules expose identical raw + semantic endpoints

## References

- [Technical TR-DOS Information](docs/inprogress/2026-01-21-trdos-analyzer/technical_trdos_info.md) - Entry points, system variables, file format
- [ECI Command Interface](docs/emulator/design/control-interfaces/command-interface.md) - Unified command interface
- [High-Level Disk Operations](docs/analysis/capture/high-level-disk-operations.md) - Original semantic analyzer design
- [Custom Loader Examples](docs/inprogress/2026-01-21-trdos-analyzer/materials/) - Real-world loader analysis

## Technical Context

### TR-DOS Entry Points

All entry points (standard and non-standard):

| Address | Hex | Purpose | Usage Pattern |
|---------|-----|---------|---------------|
| 15616 | #3D00 | Enter TR-DOS command mode | Cold entry to command line |
| 15619 | #3D03 | Execute TR-DOS command from BASIC | `RANDOMIZE USR 15619: REM : COMMAND` |
| 15635 | #3D13 | **System Function Interpreter** (C=function code) | Primary API for assembly programs |
| 15649 | #3D21 | Initialize TR-DOS system variables | Required before first API call |
| **15663** | **#3D2F** | **Universal Return (NOP + RET)** | **Critical: IM2 interrupt returns & stack hijacking** |
| 15664 | #3D30 | Return to BASIC (RET) | Stack manipulation tricks |
| 102 | #0066 | **NMI Entry Point (Magic Button)** | Hardware snapshot to disk |

### System Function Interpreter (Entry #3D13)

Called with function code in register C:

| Code | Hex | Function | Parameters |
|------|-----|----------|------------|
| 0 | #00 | Controller Reset | None - Resets WD1793 |
| 1 | #01 | Initialize Drive | A=drive (0-3) |
| 2 | #02 | Move Head to Track | Physical positioning |
| 3 | #03 | Set Sector Number | A=sector |
| 4 | #04 | Set Data Buffer Address | HL=address |
| 5 | #05 | Read Group of Sectors | D=track, E=sector, HL=buffer, B=count |
| 6 | #06 | Write Group of Sectors | D=track, E=sector, HL=buffer, B=count |
| 7 | #07 | Display Disk Directory (CAT) | A=stream number |
| 8 | #08 | Read Directory Element | A=entry (0-127) |
| 9 | #09 | Write Directory Element | A=entry |
| 10 | #0A | Find File in Directory | Descriptor at #5CE6, returns C=entry |
| 11 | #0B | Create New File Header | - |
| 12 | #0C | Write BASIC Program | Descriptor set |
| 14 | #0E | Read/Verify File | A=mode (0=load, 3=load to HL, #FF=verify) |
| 18 | #12 | Delete File | Descriptor set |
| 19 | #13 | Define File Descriptor | HL=pointer to 16-byte descriptor |
| 20 | #14 | Copy File Descriptor | HL=destination, copies from #5CE6 |
| 21 | #15 | Test Track | D=physical track, returns error count in #5CD6 |
| 23 | #17 | Select Disk Side | Selects bottom side (head  1) |
| 24 | #18 | Read System Volume Sector | Reads Track 0, Sector 9 |

**Error Handling**: After calling #3D13, check P/V flag (Parity/Overflow). If set, an error occurred. Error code is in system variable #5D0F.

### Key System Variables

| Address | Description |
|---------|-------------|
| #5CE6 | File Descriptor (16 bytes: name, type, start, length) |
| #5CF6 | Current Drive (0-3) |
| #5CF7 | Current Track |
| #5CF8 | Current Sector |
| #5D00 | Buffer address for sector read/write |

### TRDOS Catalog Entry Format (16 bytes)

| Offset | Length | Description |
|--------|--------|-------------|
| 0 | 8 | Filename (padded with spaces) |
| 8 | 1 | File Type (B=BASIC, C=CODE, D=DATA, #=Sequential) |
| 9 | 2 | Start Address (CODE) or Program Length (BASIC) |
| 11 | 2 | Length in bytes |
| 13 | 1 | Length in sectors |
| 14 | 1 | Starting Sector |
| 15 | 1 | Starting Track |

### FDC Port Mapping

| Port | Register |
|------|----------|
| #1F | Command/Status |
| #3F | Track |
| #5F | Sector |
| #7F | Data |
| #FF | System (drive, side, density) |

### WD1793 FDC Commands

Commands written to port #1F:

| Byte | Command | Description |
|------|---------|-------------|
| 0x00-0x0F | RESTORE | Seek to track 0 |
| 0x10-0x1F | SEEK | Seek to track in track register |
| 0x20-0x3F | STEP | Step in last direction |
| 0x40-0x5F | STEP IN | Step toward track 79 |
| 0x60-0x7F | STEP OUT | Step toward track 0 |
| 0x80-0x9F | READ SECTOR | Read sector(s) |
| 0xA0-0xBF | WRITE SECTOR | Write sector(s) |
| 0xC0-0xCF | READ ADDRESS | Read sector ID field |
| 0xE0-0xEF | READ TRACK | Read entire track |
| 0xF0-0xFF | WRITE TRACK | Format track |
| 0xD0 | FORCE INTERRUPT | Terminate command |

### FDC Status Register Bits

| Bit | Meaning |
|-----|---------|
| 7 | NOT READY |
| 6 | WRITE PROTECT |
| 5 | HEAD LOADED / RECORD TYPE |
| 4 | SEEK ERROR / RNF |
| 3 | CRC ERROR |
| 2 | LOST DATA / TRACK 0 |
| 1 | DRQ (Data Request) |
| 0 | BUSY |

### TR-DOS Error Codes

Error code stored in system variable `ERR_NR` (#5D0F / 23823):

| Code | Hex | Error | Meaning |
|------|-----|-------|---------|
| 0 | #00 | O.K. | Success |
| 1 | #01 | No File(s) | File not found in directory |
| 2 | #02 | File Already Exists | Cannot create duplicate |
| 3 | #03 | No Space | Insufficient disk space or RAM |
| 4 | #04 | Directory Full | Max 128 files reached |
| 5 | #05 | Rec O/F | Record overflow (sequential/random access) |
| 6 | #06 | No Disk | No diskette in drive |
| 8 | #08 | Write Protected | Disk is write-protected |
| 10 | #0A | Stream opened | Stream already open |
| 11 | #0B | Not disk file | Attempt to close non-disk stream |
| 12 | #0C | *ERROR* | General error |
| 13 | #0D | Verify Error | Data mismatch during verify |
| 14 | #0E | Array not found | BASIC array missing in file |
| 20 | #14 | *BREAK* | Break key pressed during operation |

### Abnormal Behavior Detection

#### Infinite FORCE INTERRUPT Loops

**Symptom**: Software issues repeated `FORCE INTERRUPT` commands (#D0 to port #1F) without progress.

**Cause**: WD1793/KP1818VG93 bug - controller waiting for interrupt that never arrives due to hardware/emulation flaw.

**Detection Pattern**:
```
RawFDCEvent { port: 0x1F, direction: WRITE, value: 0xD0, pc: 0x???? }
... no status change or data transfer ...
RawFDCEvent { port: 0x1F, direction: WRITE, value: 0xD0, pc: 0x???? }
... repeats indefinitely ...
```

**Semantic Event**:
```
ABNORMAL_FDC_HANG {
  type: FORCE_INTERRUPT_LOOP,
  iterations: N,
  pc_range: [0xAAAA, 0xBBBB],
  duration_tstates: T
}
```

#### Protection Loader Patterns

**Direct Port Access**: IN/OUT to #1F-#7F without prior call to #3D13 indicates custom disk routines.

**Detection**:
- PC in RAM (< #3D00 or > #3FFF) during FDC access
- Entry via #3D2F (service routine) from RAM instead of #3D00/#3D03

**Non-standard Operations**:
- Sector skipping (intentional out-of-order access)
- Using sector IDs beyond physical sector count
- Reading track without directory lookup

#### Memory Corruption Risks

TR-DOS system variables occupy **#5CB6-#5D25 (23734-23845)**.

**Abnormal Pattern**: User program writes to this range during disk operation → likely crash or directory corruption.

**Detection**:
```
RawMemoryWrite { address: 0x5CB6-0x5D25, pc: 0x???? }
  during active disk operation (FDC BUSY=1)
```


## Use Cases

### UC-1: Standard LIST Command

**Scenario**: User types `LIST` at TR-DOS prompt and presses ENTER.

**Expected Raw Events**:
```
RawBreakpointEvent { address: 0x3D00, pc: 0x3D00 }  // TR-DOS entry
RawBreakpointEvent { address: 0x3D13, pc: 0x3D13 }  // Service entry (C=command)
RawBreakpointEvent { address: 0x3D21, pc: 0x3D21 }  // Command dispatch
RawFDCEvent { port: 0x1F, direction: WRITE, value: 0x08 }  // RESTORE
RawFDCEvent { port: 0x1F, direction: READ, value: 0x00 }   // Status poll
RawFDCEvent { port: 0x1F, direction: WRITE, value: 0x18 }  // SEEK
... (multiple SEEK/READ_ADDR/READ cycles)
RawFDCEvent { port: 0x7F, direction: READ, value: ... }    // Data byte (x256)
...
```

**Expected Semantic Events**:
```
TRDOS_ENTRY { entry_point: 0x3D00, caller: <stack_addr> }
COMMAND_START { command: CAT, filename: "" }
FDC_CMD_RESTORE { from_track: ?, to_track: 0 }
FDC_CMD_SEEK { from_track: 0, to_track: 0 }
FDC_CMD_READ_ADDR { track: 0, sector: 9, side: 0 }
SECTOR_TRANSFER { track: 0, sector: 9, bytes: 256, direction: READ }
... (catalog sectors)
COMMAND_COMPLETE { command: CAT, success: true }
TRDOS_EXIT { exit_type: RET }
```

**Test Assertions**:
- [ ] Raw FDC events captured with correct port/value
- [ ] Raw breakpoint events captured with correct address
- [ ] Z80 context populated (PC, SP, IFF1, IM)
- [ ] Stack snapshot available (first 8 bytes)
- [ ] Semantic COMMAND_START correctly identifies CAT
- [ ] All catalog sectors (T0/S1-S8) read

---

### UC-2: LOAD "filename" Command

**Scenario**: User loads a CODE file from TR-DOS.

**Expected Raw Events**:
- TR-DOS entry breakpoints (0x3D00, 0x3D13, 0x3D21)
- Catalog read (T0/S1-S8) to find file
- File sector reads (based on catalog entry)
- TR-DOS exit

**Expected Semantic Events**:
```
TRDOS_ENTRY { ... }
COMMAND_START { command: LOAD, filename: "filename" }
FILE_FOUND { filename: "filename", type: CODE, start_track: T, start_sector: S, length: L }
MODULE_LOAD { type: CODE, address: <load_addr>, length: <bytes> }
COMMAND_COMPLETE { ... }
TRDOS_EXIT { ... }
```

**Test Assertions**:
- [ ] Filename extracted from memory (0x5CD1-0x5CD8)
- [ ] File type correctly classified (B/C/D/#/N)
- [ ] Load address captured from catalog entry
- [ ] All file sectors read in order

---

### UC-3: Custom Loader Detection

**Scenario**: A game with custom loader bypasses standard TR-DOS command interface and accesses FDC via ROM service routines or directly.

#### Known Custom Loader Entry Points (fromreal examples)

Based on analysis of real custom loaders:

| Address | Hex | Routine | Description |
|---------|-----|---------|-------------|
| **15663** | **#3D2F** | **Universal Return** | **Most critical entry point** - used for IM2 interrupt returns and stack hijacking |
| 10835 | #2A53 | Port Output | Direct port I/O routine |
| 16117 | #3EF5 | Wait Ready | Wait for FDC to become ready |
| 16357 | #3FE5 | Read Data | Read sector data to (HL) - highly optimized |
| 16330 | #3FCA | Write Data | Write sector data from (HL) |
| 16179 | #3F33 | Read Status | Read FDC status register |
| 15794 | #3DB2 | Find Index | Wait for index pulse |

#### IM2 Loader Pattern (Music During Load)

From source analysis of *Deja Vu #01* and *TRDOS-IM2-Loader*:

**Characteristics**:
- Uses IM 2 with interrupt handler at high RAM (e.g., #BFBF)
- Keeps EI during disk operations (enables music playback)
- **Interrupt handler MUST exit via `JP #3D2F`** (not RET/RETI)
- Main loader calls TR-DOS via PUSH IX; JP #3D2F pattern

**Raw Events to Capture**:
```
RawBreakpointEvent { address: 0x3D2F, pc: 0x3D2F }  // Universal Return entry
RawFDCEvent { pc: 0xBFBF, ... }  // PC in high RAM = interrupt handler
RawFDCEvent { pc: 0x????, ... }  // PC in RAM = custom loader
// Note: iff1=1 (EI), im=2 during load
```

#### Stack Hijacking Pattern

From *ZF DiskDriver* analysis:

```asm
LD IX, #3FE5    ; Target: Read Block routine
PUSH IX         ; Push target address
JP #3D2F        ; Hardware switch + implicit RET pops target
```

**Detection**:
- PUSH to stack followed by JP #3D2F
- Target address is internal ROM routine (#2A53-#3FFF range)

**Raw Events**:
```
RawStackWrite { address: SP, value_hi: 0x3F, value_lo: 0xE5 }
RawBreakpointEvent { address: 0x3D2F, pc: 0x8xxx, sp: SP }
// Next PC will be 0x3FE5 (popped from stack)
```

#### Direct FDC Access Pattern

From `zf_diskdriver.a80`:

```asm
TO1F    ld c,#1F
TOPORT  ld ix,#2A53
TODOS   push ix
        jp #3D2F
```

**FDC Ports Used**:
- `#1F` - Command/Status register
- `#3F` - Track register
- `#5F` - Sector register
- `#7F` - Data register
- `#FF` - System register (drive, side, density)

**Raw Events to Capture**:
```
RawFDCEvent { port: 0x1F, direction: WRITE, value: 0x80, pc: 0x8xxx }  // Read command from RAM
RawFDCEvent { port: 0x7F, direction: READ, value: ..., pc: 0x3Fxx }   // Data via ROM routine
```

#### Detection Criteria

| Criterion | Value | Meaning |
|-----------|-------|---------|
| PC in RAM during FDC access | `pc < 0x3D00 || pc > 0x3FFF` | Custom loader code |
| Entry via #3D2F | Breakpoint hit at 0x3D2F | **Universal Return** - IM2 handler or stack hijack |
| Entry via #3D00 | Breakpoint hit at 0x3D00 | Standard entry (NOT custom) |
| IM mode | `im == 2` | Likely IM2 loader |
| Interrupts | `iff1 == 1` during FDC | Music loader (rare) |
| Internal ROM call | PC in #2A53-#3FFF after #3D2F | Using internal routines directly |

**Semantic Detection**:
```
LOADER_DETECTED
{ 
  entry_point: 0x3D2F,        // Universal Return entry
  caller_pc: 0x8xxx,          // PC that called (from stack)
  classification: IM2_MUSIC,  // or STACK_HIJACK, DIRECT_FDC, STANDARD_WRAPPER
  iff1: 1,                    // Interrupts enabled
  im: 2,                      // IM2 mode
  internal_target: 0x3FE5     // If stack hijacking to internal routine
}
```

**Test Assertions**:
- [ ] Entry via #3D2F from RAM triggers LOADER_DETECTED
- [ ] Entry via #3D00 (standard) does NOT trigger LOADER_DETECTED
- [ ] IM2 mode correctly identified
- [ ] IFF1 state captured (EI/DI during load)
- [ ] Caller PC extracted from stack
- [ ] Stack hijacking pattern detected (PUSH IX; JP #3D2F)
- [ ] Internal ROM routine calls identified (#2A53, #3EF5, #3FE5, #3FCA, #3F33, #3DB2)
- [ ] Classification: `STANDARD_WRAPPER` | `DIRECT_FDC` | `IM2_MUSIC` | `STACK_HIJACK`


---

### UC-4: FORMAT Command

**Scenario**: User formats a disk.

**Expected Raw Events**:
- Breakpoints at FORMAT entry (0x1EC2)
- For each track:
  - FDC SEEK to track
  - FDC WRITE_TRACK (format track)
  - FDC READ_SECTOR (verify each sector: 0-15)
- Final service sector update (Track 0, Sector 8)

**Expected Semantic Events**:
```
COMMAND_START { command: FORMAT }
For each track:
  FDC_CMD_SEEK { to_track: N }
  FDC_CMD_WRITE_TRACK { track: N, side: S, sectors: 16 }
  FDC_CMD_READ_SECTOR { track: N, sector: 0 }  // Verify sector 0
  FDC_CMD_READ_SECTOR { track: N, sector: 1 }  // Verify sector 1
  ... (sectors 2-15)
  FDC_CMD_READ_SECTOR { track: N, sector: 15 } // Verify sector 15
SERVICE_SECTOR_UPDATE { track: 0, sector: 8, disk_type: 0x16 }
COMMAND_COMPLETE { command: FORMAT, tracks_formatted: 80, verified: true }
```

**Test Assertions**:
- [ ] All tracks formatted (80 or 40 depending on disk type)
- [ ] Double-sided detection (determined by $ as first name of disk label)
- [ ] FORMAT progress trackable via raw events

---

### UC-5: Error Handling

**Scenario**: Disk read fails with CRC error.

**Raw Event Indicators**:
```
RawFDCEvent { port: 0x1F, direction: READ, value: 0x08 }  // Status with CRC error bit
```

**Expected Semantic Events**:
```
ERROR_CRC { track: T, sector: S, fdc_status: 0x08 }
```

**Test Assertions**:
- [ ] FDC status register bits correctly interpreted
- [ ] Error type classified (CRC, RNF, LOST_DATA, WRITE_PROTECT)
- [ ] Error context includes track/sector

---

### UC-6: API Parity

**Scenario**: Retrieve raw events via WebAPI and CLI.

**WebAPI**:
```http
GET /api/v1/emulator/{id}/analyzer/trdos/raw/fdc
GET /api/v1/emulator/{id}/analyzer/trdos/raw/breakpoints
GET /api/v1/emulator/{id}/analyzer/trdos/semantic
```

**CLI**:
```
analyzer trdos raw fdc [--limit=N]
analyzer trdos raw breakpoints [--limit=N]
analyzer trdos semantic [--limit=N]
```

**Test Assertions**:
- [ ] WebAPI and CLI return identical JSON structure
- [ ] Raw events include: tstate, frame, port, direction, value, pc, sp, iff1, im, stack[]
- [ ] Semantic events include: type, timestamp, frame, command, filename, track, sector, etc.
- [ ] Both interfaces support limit/pagination

---

## Stack Trace Validation

Stack traces captured during disk operations must be validated to detect:
1. **Stack corruption** (bad return addresses)
2. **Custom loaders** using unusual ROM addresses

### Discovery Approach

**Phase 1**: Collect raw stack addresses in events without validation
**Phase 2**: Analyze captured dumps to identify common ROM addresses
**Phase 3**: Update whitelist tables based on observed patterns
**Phase 4**: Enable validation to flag unknown addresses

The whitelist below is a starting point - expect refinement based on real-world captures.

### Valid TR-DOS ROM Address Ranges

Based on `trdos504t.asm`, `spectrumconstants.h`, and `detailed_trdos_internals.md`:

| Range | Description |
|-------|-------------|
| `#0000-#3CFF` | General TR-DOS ROM code |
| `#3D00-#3DFF` | **Trap Range** (entry points) |
| `#3E00-#3FFF` | Internal routines |

### Valid Entry Points (Commonly on Stack)

| Address | Hex | Name | Notes |
|---------|-----|------|-------|
| 15616 | #3D00 | ENTRY_MAIN | Cold start |
| 15619 | #3D03 | ENTRY_COMMAND | BASIC command |
| 15622 | #3D06 | ENTRY_FILE_IN | Data input |
| 15630 | #3D0E | ENTRY_FILE_OUT | Data output |
| 15635 | #3D13 | ENTRY_MCODE | API dispatch |
| 15638 | #3D16 | Error Handler | |
| 15649 | #3D21 | Init Vars | |
| **15663** | **#3D2F** | **ROM_TRAMPOLINE** | **Universal return** |
| 15664 | #3D30 | ENTRY_FULL | RET hook |
| 15665 | #3D31 | Full Entry | |

### Valid Internal Routines (v5.03/5.04T)

| Address | Hex | Purpose |
|---------|-----|---------|
| 10835 | #2A53 | Port output utility |
| 15794 | #3DB2 | Find index pulse |
| 15816 | #3DC8 | Select default drive |
| 15821 | #3DCD | Select drive N |
| 16117 | #3EF5 | Wait for FDC ready |
| 16179 | #3F33 | Read FDC status |
| 16330 | #3FCA | Write sector data block |
| 16357 | #3FE5 | Read sector data block |
| 16341 | #3FD5 | Block transfer loop |

### Valid RAM Addresses

| Address | Hex | Purpose |
|---------|-----|---------|
| 23746 | #5CC2 | RAM stub (RET #C9) - **ROM/RAM switch point** |
| `#6000-#FFFF` | | User code area |

### Invalid/Suspect Addresses

| Range | Reason |
|-------|--------|
| `#4000-#5AFF` | Screen memory (unlikely return) |
| `#5B00-#5CC1` | System variables (data, not code) |
| `#5CC3-#5DFF` | TR-DOS variables (except #5CC2) |
| Unknown ROM | Not in whitelist → custom loader or corruption |

### Validation Algorithm

```cpp
enum StackValidation
{
    VALID,              // Known good address
    CUSTOM_LOADER,      // Valid but unusual (custom loader suspected)
    CORRUPTED           // Invalid address (stack corruption)
};

StackValidation validateReturnAddress(uint16_t addr)
{
    // Known entry points
    if (addr >= 0x3D00 && addr <= 0x3DFF) return VALID;  // Trap range
    if (addr >= 0x2A53 && addr <= 0x3FFF) return VALID;  // Internal routines
    
    // RAM stub
    if (addr == 0x5CC2) return VALID;
    
    // User code
    if (addr >= 0x6000) return VALID;
    
    // Screen memory - suspect
    if (addr >= 0x4000 && addr <= 0x5AFF) return CORRUPTED;
    
    // System variables - suspect
    if (addr >= 0x5B00 && addr <= 0x5DFF) return CORRUPTED;
    
    // Unknown ROM address
    if (addr < 0x4000) return CUSTOM_LOADER;
    
    return VALID;
}
```

---

## Data Structures

### RawFDCEvent

```cpp
struct RawFDCEvent
{
    // Timing
    uint64_t tstate;
    uint32_t frameNumber;
    
    // FDC Access
    uint8_t port;              // 0x1F, 0x3F, 0x5F, 0x7F, 0xFF
    uint8_t direction;         // 0=READ(IN), 1=WRITE(OUT)
    uint8_t value;             // Value read/written
    
    // FDC Register Snapshot
    uint8_t commandReg;
    uint8_t statusReg;
    uint8_t trackReg;
    uint8_t sectorReg;
    uint8_t dataReg;
    uint8_t systemReg;         // 0xFF port (drive/side/density)
    
    // Z80 Context
    uint16_t pc;
    uint16_t sp;
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t iff1, iff2, im;
    
    // Stack snapshot for call chain reconstruction
    // 
    // STACK SANITY VALIDATION:
    // Each 16-bit return address should be in valid range:
    //   - TR-DOS ROM: 0x0000-0x3FFF (when DOS paged)
    //   - Standard ROM: 0x0000-0x3FFF (when SOS paged)
    //   - RAM: 0x4000-0xFFFF
    // 
    // VALID TR-DOS ROM ADDRESSES (from trdos504t.asm + spectrumconstants.h):
    // Entry points (#3D00-#3DFF trap range):
    //   0x3D00 ENTRY_MAIN      - Main TR-DOS entry
    //   0x3D03 ENTRY_COMMAND   - Execute command from BASIC
    //   0x3D06 ENTRY_FILE_IN   - Data file input
    //   0x3D0E ENTRY_FILE_OUT  - Data file output
    //   0x3D13 ENTRY_MCODE     - Machine code calls (#3D13 API dispatch)
    //   0x3D16                 - Error handler
    //   0x3D21                 - Init system variables
    //   0x3D2F ROM_TRAMPOLINE  - Universal return (JP target on stack)
    //   0x3D31 ENTRY_FULL      - Full DOS entry
    // 
    // Internal routines (commonly on stack during disk ops):
    //   0x3D80-0x3D94          - Print routines
    //   0x3D98-0x3D9A          - RESTORE with INTRQ wait
    //   0x3DAD                 - Check disk presence
    //   0x3DC8-0x3DCB          - Select drive routines
    //   0x3DFD-0x3DFF          - Delay routines
    //   0x2A53                 - Port output utility
    //   0x3EF5                 - Wait for FDC ready
    //   0x3FE5                 - Read sector data block
    //   0x3FCA                 - Write sector data block
    //   0x3F33                 - Read FDC status
    //   0x3DB2                 - Find index pulse
    //
    // RAM stub address:
    //   0x5CC2 RAM_STUB        - RET instruction (ROM/RAM switch point)
    //
    // SUSPECT CORRUPTION OR CUSTOM LOADER if:
    //   - Address in screen memory: 0x4000-0x5AFF
    //   - Address in system vars: 0x5B00-0x5DFF (except 0x5CC2)
    //   - Unknown ROM address not in whitelist above
    //   - Multiple consecutive identical addresses
    //
    std::array<uint8_t, 16> stack;  // First 16 bytes from SP (8 return addresses)
};
```

### RawBreakpointEvent

```cpp
struct RawBreakpointEvent
{
    // Timing
    uint64_t tstate;
    uint32_t frameNumber;
    
    // Breakpoint
    uint16_t address;          // Breakpoint address hit
    
    // Z80 Context (full snapshot)
    uint16_t pc;
    uint16_t sp;
    uint16_t af, bc, de, hl;
    uint16_t af_, bc_, de_, hl_;  // Alternate set
    uint16_t ix, iy;
    uint8_t i, r;
    uint8_t iff1, iff2, im;
    
    // Stack snapshot
    // NOTE: Validate stack sanity - detect corruption via ROM routines + RAM jump stubs addresses checks
    std::array<uint8_t, 16> stack;  // First 16 bytes from SP (8 return addresses)
};
```

### SemanticEvent

```cpp
enum class TRDOSCommand
{
    UNKNOWN = 0,
    LIST,           // CAT
    LOAD,           // LOAD "filename"
    SAVE,           // SAVE "filename"
    ERASE,          // ERASE "filename"
    COPY,           // COPY "src" TO "dst"
    MOVE,           // MOVE
    FORMAT,         // FORMAT
    RUN,            // RUN "filename"
    VERIFY,         // VERIFY
    MERGE,          // MERGE
    NEW,            // NEW (rename)
    GOTO,           // GOTO (change drive)
};

enum class SemanticEventType
{
    // Entry/Exit Events
    TRDOS_ENTRY,           // Hit ROM entry point
    TRDOS_EXIT,            // Return from TR-DOS
    
    // Command Events
    COMMAND_START,         // Command parsing started
    COMMAND_COMPLETE,      // Command finished (success/failure)
    
    // File Events
    FILE_FOUND,            // File located in catalog
    FILE_NOT_FOUND,        // File not in catalog
    MODULE_LOAD,           // File loaded to memory
    MODULE_SAVE,           // File saved to disk
    
    // System Function Events
    API_CALL,              // Call to #3D13 with function code
    
    // FDC Aggregate Events  
    SECTOR_TRANSFER,       // Complete sector read/write
    TRACK_FORMATTED,       // Track format + verification complete
    SERVICE_SECTOR_UPDATE, // Service sector (T0/S8) modified
    
    // Loader Detection
    CUSTOM_OADER_DETECTED, // Custom loader pattern identified
    
    // Error/Abnormal Events
    ERROR_DETECTED,        // TR-DOS error code set
    FDC_HANG_DETECTED,     // Loop detected (FORCE_INTERRUPT, SEEK or other)
    MEMORY_CORRUPTION,     // Write to system variables during disk op
};

struct SemanticEvent
{
    // Timing
    uint64_t tstate;
    uint32_t frameNumber;
    SemanticEventType type;
    
    // Context
    uint16_t pc;
    uint16_t caller;                    // From stack
    
    // Event-specific data (use std::variant or union in implementation)
    union
    {
        struct { uint16_t entryPoint; } entry;
        
        struct
        { 
            TRDOSCommand command;       // LIST, LOAD, SAVE, etc.
            char filename[9];
        } commandStart;
        
        struct
        {
            TRDOSCommand command;
            bool success;
            uint8_t errorCode;
        } commandComplete;
        
        struct
        {
            char filename[9];
            uint8_t fileType;           // B, C, D, #
            uint16_t startAddress;
            uint16_t length;
            uint8_t startTrack;
            uint8_t startSector;
        } fileFound;
        
        struct
        {
            uint8_t functionCode;       // #00-#18
            uint16_t regA, regC, regHL, regDE, regBC;
        } apiCall;
        
        struct
        {
            uint8_t track;
            uint8_t sector;
            uint8_t side;
            uint16_t bytes;
            bool isWrite;
        } sectorTransfer;
        
        struct
        {
            uint8_t track;
            uint8_t side;
            uint8_t sectorsVerified;
            bool allPassed;
        } trackFormatted;
        
        struct
        {
            uint16_t entryPoint;        // #3D2F, #3D00, etc.
            uint16_t callerPc;
            uint8_t im;
            bool iff1;
            enum { STANDARD_WRAPPER, DIRECT_FDC, IM2_MUSIC, STACK_HIJACK } classification;
            uint16_t internalTarget;    // If stack hijacking
        } loaderDetected;
        
        struct
        {
            uint8_t errorCode;          // 0-20
            uint8_t hwStatus;           // FDC status register
        } errorDetected;
        
        struct
        {
            uint32_t iterations;
            uint16_t pcRangeLow;
            uint16_t pcRangeHigh;
        } fdcHangDetected;
    };
};
```

---

## Test Plan

| Test ID | Use Case | Layer | Automation |
|---------|----------|-------|------------|
| T1.1 | LIST command raw capture | Layer 1 | Integration test |
| T1.2 | LIST command semantic | Layer 2 | Integration test |
| T2.1 | LOAD raw capture | Layer 1 | Integration test |
| T2.2 | LOAD semantic | Layer 2 | Integration test |
| T3.1 | Custom loader detection | Layer 2 | Integration test |
| T4.1 | FORMAT raw capture | Layer 1 | Integration test |
| T5.1 | Error handling | Layer 2 | Unit test |
| T6.1 | WebAPI/CLI parity | API | Integration test |
| T6.2 | Raw JSON structure | API | Unit test |
