# Insult Demo IM2 Disk Loader - Complete Reverse Engineering

> **Memory Range**: `0xF4F4 - 0xF700` (524 bytes)  
> **Entry Point**: `0xF4F4` (IM2 interrupt handler)  
> **Total Instructions**: 244

## Executive Summary

This loader is a sophisticated **interrupt-driven disk loader** that operates during the IM2 interrupt cycle. It loads data from TR-DOS floppy disk sectors while simultaneously running a visual "LOADING" effect via a depack/decrunching routine.

The architecture uses **self-modifying code (SMC)** to dynamically switch between two operating modes during interrupt handling, enabling seamless integration with the TR-DOS ROM gateway.

---

## Memory Map

```mermaid
block-beta
    columns 1
    block:LOADER["IM2 Disk Loader (0xF4F4-0xF700)"]
        columns 4
        A["IM2 Handler<br/>0xF4F4-0xF4FF<br/>(12 bytes)"]
        B["Main Loader<br/>0xF500-0xF5A5<br/>(166 bytes)"]
        C["TRDOS Wrappers<br/>0xF5A6-0xF5F4<br/>(79 bytes)"]
        D["Depack Routine<br/>0xF5F5-0xF693<br/>(159 bytes)"]
    end
    block:DATA["Data Area"]
        columns 2
        E["Work Buffer<br/>0xF694-0xF69B<br/>(8 bytes)"]
        F["Loading Text<br/>0xF69C-0xF6FF<br/>(100 bytes)"]
    end
```

### Key Memory Variables

| Address | Name | Description |
|---------|------|-------------|
| `0x3D2F` | `TRDOS_ENTRY` | TR-DOS ROM gateway entry point |
| `0xF4F6` | `SMC_jump` | Self-modifying jump offset (switches handler behavior) |
| `0xF508` | `loader_status` | Loader completion/error status (0=done, 2=error) |
| `0xF569` | `transfer_state` | Active sector transfer flag |
| `0xF667` | `depack_flag` | Depack routine state toggle |
| `0xF66E` | `text_ptr` | Pointer to current loading text position |
| `0xF694` | `work_buffer` | 64-byte rotating graphics buffer |
| `0xF69C` | `loading_text` | Embedded "LOADING" message text |
| `0xF6D1` | `depack_flag2` | Secondary depack control flag |

---

## Architecture Overview

```mermaid
flowchart TB
    subgraph "System Context"
        IM2["IM2 Interrupt<br/>(50Hz from ULA)"]
        TRDOS["TR-DOS ROM<br/>(0x3D2F Gateway)"]
        FDC["WD1793 FDC<br/>(Ports 0x1F, 0x5F, 0x7F)"]
    end

    subgraph "Loader Components"
        HANDLER["IM2 Handler<br/>0xF4F4"]
        MAIN["Main Loader<br/>0xF500"]
        WRAPPERS["TRDOS Wrappers<br/>0xF5A6-0xF5F4"]
        DEPACK["Depack Routine<br/>0xF5F5"]
    end

    IM2 --> HANDLER
    HANDLER -->|"SMC: JR #08"| SKIP[Skip to TRDOS]
    HANDLER -->|"SMC: JR #28"| STORE[Store state]
    SKIP --> TRDOS
    STORE --> MAIN
    MAIN --> WRAPPERS
    WRAPPERS --> TRDOS
    MAIN --> DEPACK
    TRDOS -.-> FDC
```

---

## Component Analysis

### 1. IM2 Interrupt Handler (0xF4F4-0xF4FF)

The interrupt handler is the **entry point** for all loader activity. It uses **self-modifying code** to switch between two modes:

```mermaid
flowchart TD
    A["INT Received<br/>0xF4F4"] --> B["EX AF,AF'<br/>(Save AF)"]
    B --> C["CP B<br/>(Check state)"]
    C --> D{"SMC Jump<br/>@ 0xF4F6"}
    
    D -->|"JR #08<br/>(Normal)"| E["Skip to 0xF4FF"]
    D -->|"JR #28<br/>(Transfer)"| F["Store to<br/>transfer_state"]
    
    E --> G["EX AF,AF'"]
    F --> G
    G --> H["EI"]
    H --> I["JP 0x3D2F<br/>(TRDOS Entry)"]
```

#### SMC Mechanism

| SMC Value | Jump Target | Mode | Description |
|-----------|-------------|------|-------------|
| `0x18` | `0xF4FE` | **Normal** | Standard interrupt - exit immediately to TRDOS |
| `0x28` | `0xF4F8` | **Transfer** | Active read - store state and synchronize with loader |

The loader writes to `0xF4F6` to toggle between these modes during sector transfers.

---

### 2. Main Disk Loader (0xF500-0xF5A5)

The main loader orchestrates the complete loading process:

```mermaid
flowchart TD
    START["main_loader<br/>0xF500"] --> EXX["EXX<br/>(Alt registers)"]
    EXX --> DEPACK["CALL depack_routine<br/>(Visual effect)"]
    DEPACK --> SOUND["CALL #5E15<br/>(AY sound update)"]
    SOUND --> LOADSTAT["LD A, #00"]
    LOADSTAT --> EXXBACK["EXX"]
    
    EXXBACK --> CHECK{"HL == 0xFFFF?"}
    CHECK -->|"Yes"| EXIT["Exit Loader"]
    CHECK -->|"No"| SETUP["Setup Sector"]
    
    SETUP --> CALCTRACK["A = D >> 1<br/>(Physical track)"]
    CALCTRACK --> SEEK["CALL trdos_seek"]
    SEEK --> WAITSEEK["CALL trdos_wait_fdc"]
    
    WAITSEEK --> SECTORLOOP["Sector Loop"]
    
    subgraph SECTORLOOP["Sector Read Loop (0xF530)"]
        S1["Set Track"] --> S2["Set Side"]
        S2 --> S3["Set Sector"]
        S3 --> S4["Read Sector<br/>Command #84"]
        S4 --> S5{"FDC Status OK?"}
        S5 -->|"Error"| S6["Store Error #02"]
        S5 -->|"OK"| S7["Next Sector"]
        S7 --> S8{"More Sectors?"}
        S8 -->|"Yes"| S1
        S8 -->|"No"| RETURN["RET"]
    end
    
    EXIT --> INCSP["INC SP × 2<br/>(Pop return)"]
    INCSP --> CLEAR["Clear loader_status"]
    CLEAR --> TRDOSEXIT["JP TRDOS_ENTRY"]
    S6 --> EXIT
```

#### Load Completion Detection

The loader uses `HL = 0xFFFF` as the **end sentinel**:

```z80
0xF50A: ld a,h         ; Check high byte
0xF50B: inc a          ; H == 0xFF → A == 0x00
0xF50C: jr nz,.continue
0xF50E: ld a,l         ; Check low byte
0xF50F: inc a          ; L == 0xFF → A == 0x00
0xF510: jr nz,.continue
; Both matched = done loading
```

#### Track/Sector Calculation

The loader uses **interleaved** track/side addressing:

| Register D | Physical Track | Side |
|------------|----------------|------|
| 0 | 0 | 0 |
| 1 | 0 | 1 |
| 2 | 1 | 0 |
| 3 | 1 | 1 |
| ... | ... | ... |

```z80
0xF522: ld a,d         ; Get logical track
0xF523: srl a          ; Physical track = D >> 1
0xF536: bit 0,d        ; Side = D & 1
```

---

### 3. Sector Read Sequence

```mermaid
sequenceDiagram
    participant L as Loader
    participant W as Wrappers
    participant T as TR-DOS ROM
    participant F as FDC (WD1793)
    
    Note over L: Read Sector Setup
    L->>W: trdos_set_track(track)
    W->>T: JP 0x1E3A
    T->>F: OUT (0x7F), track
    
    L->>W: trdos_set_side(side)
    W->>T: JP 0x1FF3
    T->>F: OUT (0xFF), side_bits
    
    L->>W: trdos_set_sector(sector+1)
    W->>T: JP 0x2A53
    T->>F: OUT (0x5F), sector
    
    Note over L: Issue Read Command
    L->>L: Set SMC = #28 (Transfer mode)
    L->>W: trdos_call(#84)
    W->>T: JP 0x3D2F
    T->>F: OUT (0x1F), #84
    
    Note over L,F: Transfer Loop (via INT)
    loop Each Byte via IM2
        F-->>L: INT triggers handler
        L->>L: Store byte via transfer_state
    end
    
    L->>L: Set SMC = #18 (Normal mode)
    L->>W: trdos_wait_fdc()
    W->>T: JP 0x3FE5
    T->>F: IN A, (0x1F)
    T-->>L: Return status
```

---

### 4. TRDOS Gateway Wrappers (0xF5A6-0xF5F4)

The loader uses a sophisticated **return address injection** technique to call TR-DOS ROM routines:

```mermaid
flowchart LR
    subgraph "trdos_call (0xF5A6)"
        A1["PUSH HL"] --> A2["LD HL, #2FC3"]
        A2 --> A3["EX (SP), HL"]
        A3 --> A4["JP 0x3D2F"]
    end
    
    subgraph "Stack State"
        B1["Before: [return_addr]"]
        B2["After: [#2FC3, return_addr]"]
    end
    
    A1 -.-> B1
    A3 -.-> B2
```

The magic address `0x2FC3` is the **return point** within TR-DOS that properly exits back to the caller.

#### Wrapper Functions

| Address | Name | Purpose | TR-DOS Routine |
|---------|------|---------|----------------|
| `0xF5A6` | `trdos_call` | Generic TRDOS call | Variable (A register) |
| `0xF5AE` | `fdc_read_status` | Read FDC status register | `0x3FEC` |
| `0xF5C6` | `trdos_set_track` | Set track register | `0x1E3A` |
| `0xF5CE` | `trdos_set_sector` | Set sector register | `0x2A53` |
| `0xF5E0` | `trdos_seek` | Seek to track | (shares `0x2A53`) |
| `0xF5E5` | `trdos_set_side` | Set disk side | `0x1FF3` |
| `0xF5ED` | `trdos_wait_fdc` | Wait for FDC completion | `0x3FE5` |

---

### 5. Depack/Decrunching Routine (0xF5F5-0xF693)

The depack routine creates a **visual loading effect** by rotating bits through a 32-byte buffer to create animated graphics:

```mermaid
flowchart TD
    START["depack_routine<br/>0xF5F5"] --> INIT["HL = 0x50FF (source)<br/>DE = 0xF694 (buffer)<br/>B = 8 (bits)"]
    
    INIT --> ROTLOOP["Rotate Loop"]
    
    subgraph ROTLOOP["Bit Rotation (×8)"]
        R1["RLC (source byte)"] --> R2["RL (buffer[31])"]
        R2 --> R3["RL (buffer[30])"]
        R3 --> R4["..."]
        R4 --> R5["RL (buffer[0])"]
        R5 --> R6["INC source page"]
        R6 --> R7{"--B == 0?"}
        R7 -->|"No"| R1
    end
    
    R7 -->|"Yes"| FINISH["Toggle depack_flag"]
    FINISH --> CFCHECK{"Carry set?"}
    CFCHECK -->|"No"| RETURN["RET"]
    CFCHECK -->|"Yes"| TEXTWORK["Process Text"]
    
    TEXTWORK --> GETCHAR["Get char from text_ptr"]
    GETCHAR --> CALCADDR["HL = char × 8 + 0x3C00"]
    CALCADDR --> GFXLOOP["Copy 8 bytes to buffer"]
    GFXLOOP --> RETURN2["RET"]
```

#### Rotation Chain

The routine performs a **32-byte chained rotation** - each `RL (HL)` instruction propagates the carry from the previous rotation, creating a smooth scrolling effect:

```
Source byte → Buffer[31] → Buffer[30] → ... → Buffer[0]
     ↓            ↓            ↓                 ↓
   RLC          RL           RL               RL
     ↓            ↓            ↓                 ↓
   Carry ────────────────────────────────────────→
```

---

## State Machine

```mermaid
stateDiagram-v2
    [*] --> IDLE: Reset
    
    IDLE --> LOADING: Start Load<br/>(HL ≠ 0xFFFF)
    
    state LOADING {
        [*] --> SEEK: Calculate Track
        SEEK --> SET_PARAMS: Seek Complete
        SET_PARAMS --> READ_SECTOR: Params Set
        READ_SECTOR --> TRANSFER: Command Issued
        
        state TRANSFER {
            [*] --> WAIT_BYTE: SMC = #28
            WAIT_BYTE --> GOT_BYTE: INT
            GOT_BYTE --> WAIT_BYTE: More bytes
            GOT_BYTE --> [*]: Sector done
        }
        
        TRANSFER --> CHECK_STATUS: SMC = #18
        CHECK_STATUS --> NEXT_SECTOR: Status OK
        CHECK_STATUS --> ERROR: Status Error
        
        NEXT_SECTOR --> READ_SECTOR: More sectors
        NEXT_SECTOR --> NEXT_TRACK: Sector overflow
        NEXT_TRACK --> SEEK: Side change
    }
    
    LOADING --> DONE: HL == 0xFFFF
    ERROR --> DONE: Store error code
    DONE --> [*]: JP TRDOS_ENTRY
```

---

## Self-Modifying Code Analysis

| Location | Original | Modified | Purpose |
|----------|----------|----------|---------|
| `0xF4F6` | `JR #08` | `JR #28` | Switch INT handler to transfer mode |
| `0xF508` | `#00` | `#02` | Store error status |
| `0xF569` | `#00` | `#01` | Mark transfer active |
| `0xF667` | Variable | Toggle | Control depack alternation |

### SMC Timing Diagram

```mermaid
gantt
    title SMC State During Sector Read
    dateFormat X
    axisFormat %s
    
    section SMC @ 0xF4F6
    Normal Mode (#18)    :a1, 0, 1
    Transfer Mode (#28)  :a2, 1, 2
    Normal Mode (#18)    :a3, 2, 3
    
    section transfer_state
    Inactive (#00)       :b1, 0, 1
    Active (#01)         :b2, 1, 2
    Inactive (#00)       :b3, 2, 3
    
    section FDC Activity
    Seek                 :c1, 0, 1
    Read Sector          :c2, 1, 2
    Wait Complete        :c3, 2, 3
```

---

## Error Handling

```mermaid
flowchart TD
    READ["Read Sector"] --> STATUS["CALL fdc_read_status"]
    STATUS --> CHECK{"A & #08"}
    CHECK -->|"Zero"| OK["Continue"]
    CHECK -->|"Non-zero"| ERR["Error!"]
    ERR --> STORE["LD (loader_status), #02"]
    STORE --> EXIT["INC SP × 2<br/>(Pop return addr)"]
    EXIT --> TRDOS["JP TRDOS_ENTRY"]
```

The loader checks **bit 3** of the FDC status register (CRC error or record not found) and terminates on error.

---

## Embedded Data

### Loading Text (0xF69C)

```
"> OF <INSULT> MEGADEMO...                                LOADING AND DECRUNCHING PART <INTRO"
```

This text scrolls across the screen during loading via the depack routine.

---

## Call Graph

```mermaid
flowchart TD
    INT["IM2 Interrupt"] --> HANDLER["IM2_handler<br/>0xF4F4"]
    
    HANDLER --> MAIN["main_loader<br/>0xF500"]
    MAIN --> DEPACK["depack_routine<br/>0xF5F5"]
    MAIN --> SOUND["#5E15<br/>(AY update)"]
    
    MAIN --> TRDOS_CALL["trdos_call<br/>0xF5A6"]
    MAIN --> FDC_STATUS["fdc_read_status<br/>0xF5AE"]
    MAIN --> SET_TRACK["trdos_set_track<br/>0xF5C6"]
    MAIN --> SET_SECTOR["trdos_set_sector<br/>0xF5CE"]
    MAIN --> SEEK["trdos_seek<br/>0xF5E0"]
    MAIN --> SET_SIDE["trdos_set_side<br/>0xF5E5"]
    MAIN --> WAIT_FDC["trdos_wait_fdc<br/>0xF5ED"]
    
    TRDOS_CALL --> TRDOS["TRDOS_ENTRY<br/>0x3D2F"]
    FDC_STATUS --> TRDOS
    SET_TRACK --> TRDOS
    SET_SECTOR --> TRDOS
    SEEK --> TRDOS
    SET_SIDE --> TRDOS
    WAIT_FDC --> TRDOS
    
    style HANDLER fill:#f96
    style MAIN fill:#9f9
    style DEPACK fill:#99f
    style TRDOS fill:#ff9
```

---

## Register Usage Summary

| Register | Usage |
|----------|-------|
| **A** | Command codes, status, calculations |
| **B** | Loop counters, remaining bytes |
| **C** | Sector count, port numbers |
| **D** | Logical track (physical track = D >> 1, side = D & 1) |
| **E** | Current sector (0-15) |
| **HL** | Destination address / pointer |
| **AF'** | Preserved across interrupt |
| **BC', DE', HL'** | Used by depack_routine |

---

## Key Techniques

1. **Self-Modifying Code (SMC)**: Dynamically switches interrupt handler behavior between normal and transfer modes.

2. **Return Address Injection**: Uses `EX (SP),HL` to inject a custom return address (`0x2FC3`) onto the stack before calling TR-DOS.

3. **Interleaved Track/Side**: Encodes both track and side in register D using bit manipulation (`SRL`, `BIT 0`).

4. **Chained Bit Rotation**: Creates smooth scrolling animation by propagating carry through 32 consecutive `RL (HL)` instructions.

5. **End Sentinel**: Uses `HL = 0xFFFF` as a termination marker rather than an explicit count.

---

## Hazards & Gotchas

> [!CAUTION]
> **Register Stasis Bug**: The loader's custom `0x2FC3` gateway return bypasses normal TR-DOS sector register updates. If an FDC NOTREADY status causes a retry, the sector register remains unchanged, leading to infinite reads of the same sector.

> [!WARNING]
> **SMC Race Condition**: The interrupt handler may be called while the SMC byte at `0xF4F6` is being modified. Critical timing ensures this window is very short.

> [!IMPORTANT]
> **Stack Manipulation**: The `EX (SP),HL` technique requires careful stack balance. The loader manually adjusts SP (`INC SP × 2`) on exit to account for unpopped addresses.

---

## Disassembly Cross-Reference

| Address Range | Function | Lines in Original |
|---------------|----------|-------------------|
| `0xF4F4-0xF4FF` | IM2_handler | 24-32 |
| `0xF500-0xF5A5` | main_loader | 34-143 |
| `0xF5A6-0xF5C5` | trdos_call, fdc_read_status | 146-169 |
| `0xF5C6-0xF5F4` | Set track/sector/side/seek/wait | 171-207 |
| `0xF5F5-0xF693` | depack_routine | 210-320 |
| `0xF694-0xF700` | Data area | 322-337 |
