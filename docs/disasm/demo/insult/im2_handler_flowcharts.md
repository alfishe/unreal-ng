# Insult Demo IM2 Interrupt Handler Analysis

This document describes the self-modifying code (SMC) mechanism used by the Insult demo's IM2 disk loader.

## Overview

The interrupt handler at `0xF4F4` uses a self-modifying JR instruction to switch between two operational modes:

| State | JR Offset | Destination | Purpose |
|-------|-----------|-------------|---------|
| STATE 1 (Default) | `#08` | `0xF500` | Normal processing - depack & update |
| STATE 2 (Transfer) | `#28` | `0xF520` | Active during sector read |

## Handler Code

```z80
; IM2 Handler at 0xF4F4
0xF4F4: ex af,af'       ; Save AF
0xF4F5: cp b            ; Compare A' with B  
0xF4F6: jr XX           ; SMC: XX = #08 or #28
0xF4F8: ld (#F569),a    ; (fallthrough path - normally skipped)
0xF4FB: ex af,af'
0xF4FC: ei
0xF4FD: jp #3D2F        ; TRDOS entry
```

## STATE 1: Default Mode (JR #08)

```mermaid
flowchart TD
    subgraph "STATE 1 - Default Mode"
        INT1[/"IM2 Interrupt"/] --> SAVE1["ex af,af'<br/>Save AF"]
        SAVE1 --> CMP1["cp b<br/>Compare A' with B"]
        CMP1 --> JR1["jr #08<br/>Jump to 0xF500"]
        JR1 --> MAIN["main_loader (0xF500)"]
        
        MAIN --> EXX["exx<br/>Switch registers"]
        EXX --> DEPACK["call depack_routine<br/>Decompress data"]
        DEPACK --> SOUND["call #5E15<br/>Update AY sound"]
        SOUND --> CHECK{"HL = 0xFFFF?<br/>All data loaded?"}
        
        CHECK -->|Yes| EXIT["Exit loader<br/>Return to caller"]
        CHECK -->|No| SETUP["Continue sector setup"]
        SETUP --> RESTORE1["ld (#F4F6),#18<br/>Keep STATE 1"]
        RESTORE1 --> TRDOS1["Call TR-DOS routines"]
    end
```

**Purpose**: Between sector reads, process interrupts to:
- Decompress loaded data
- Update sound
- Check completion status

---

## STATE 2: Transfer Mode (JR #28)

```mermaid
flowchart TD
    subgraph "STATE 2 - Transfer Mode"
        INT2[/"IM2 Interrupt<br/>(during sector read)"/] --> SAVE2["ex af,af'<br/>Save AF"]
        SAVE2 --> CMP2["cp b<br/>Compare A' with B"]
        CMP2 --> JR2["jr #28<br/>Jump to 0xF520"]
        
        JR2 --> OVERLAP["0xF520: OR #F4<br/>(overlapping code artifact)"]
        OVERLAP --> TRACK["ld a,d<br/>Get track number"]
        TRACK --> CALC["srl a<br/>Calculate cylinder"]
        CALC --> SEEK["call trdos_seek"]
        SEEK --> SECTOR["Setup sector read"]
        SECTOR --> STATE2["ld (#F4F6),#28<br/>Stay in STATE 2"]
        STATE2 --> WAIT["call trdos_wait_fdc<br/>Re-enter TR-DOS INI loop"]
        
        WAIT -.->|"Interrupt fires again"| INT2
    end
```

**Purpose**: When interrupt fires during TR-DOS INI loop:
- Re-enter the loading code
- Resume sector reading
- Maintain transfer state

---

## State Transition Diagram

```mermaid
stateDiagram-v2
    [*] --> STATE1: Power on / Reset
    
    STATE1: Default Mode (JR #08)
    STATE2: Transfer Mode (JR #28)
    
    STATE1 --> STATE2: Before trdos_wait_fdc<br/>LD (#F4F6),#28
    STATE2 --> STATE1: After sector complete<br/>LD (#F4F6),#18
    STATE2 --> STATE2: Interrupt during read<br/>(stays in transfer mode)
    
    note right of STATE1
        Handles depack, sound,
        completion check
    end note
    
    note right of STATE2
        Active during TR-DOS
        INI byte transfer loop
    end note
```

---

## SMC Modification Points

| Address | Instruction | Effect |
|---------|-------------|--------|
| `0xF51F` | `ld (#F4F6),#18` | → STATE 1 (after sector setup) |
| `0xF559` | `ld (#F4F6),#28` | → STATE 2 (before sector read) |
| `0xF565` | `ld (#F4F6),#18` | → STATE 1 (after sector complete) |

---

## Overlapping Code at 0xF520

The jump destination 0xF520 lands in the middle of a `ld (#F4F6),a` instruction:

```
Address:  F51D F51E F51F F520 F521 F522
Bytes:    3E   18   32   F6   F4   7A
          └────┘   └─────────┘   │
          ld a,#18  ld (#F4F6),a  ld a,d
```

When jumped to at 0xF520:
- `F6 F4` = `or #F4` (artifact instruction)
- Immediately overwritten by `ld a,d` at 0xF522

---

## Key Insight: B Register Check

The retry logic at `0xF56F`:
```z80
0xF56E: pop bc         ; Restore BC from before trdos_wait_fdc
0xF56F: ld a,b         ; Get B (INI loop counter residue)
0xF570: pop bc         ; 
0xF571: and a          ; Test if B = 0
0xF572: jr z,.check_status  ; If zero, all 256 bytes read
0xF574: pop hl         ; Otherwise...
0xF575: jr .continue_loading  ; RETRY the sector!
```

**If B ≠ 0 after TR-DOS returns, the sector read is retried.**
