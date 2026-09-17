# Monitor ROM Disassembly (0xF800-0xFFFF)

The v7.10 system ROM's resident monitor: boot menu, console I/O stubs, the
CP/M service layer and the FDC driver the CP/M BIOS delegates to.

## Service Sessions

The single most important structural fact when tracing CP/M on this machine:
**BDOS and the disk subsystem do not run in the TPA mapping.**

When the monitor services a BDOS/disk request it sets **7FFD bit 4 (0x10)**,
which switches the low 16K (0x0000-0x3FFF) from the CP/M page-zero/BIOS RAM
to a service ROM bank (ROM page 39 in the v7.10 set). Code keeps executing
at the same Z80 addresses, but from ROM. Clearing the bit returns the CP/M
mapping.

Tracing implication: an M1 filter of `(p7FFD & 0x10) == 0` selects exactly
the TPA (0100-DD00); everything else is monitor/service code.

### 0x0965 - Service Session Wait Loop

Observed as long runs of consecutive M1 fetches (389+ in a row) inside
service sessions during disk operations. It is a **spin loop, not a HALT**:
the emulator's M1 trace hook does not fire while the CPU is in the halted
state, so a repeated-pc M1 stream proves the CPU is executing (JR $-style
poll or equivalent).

## Jump Table (0xF800)

| Address | Vector | Function |
|---------|--------|----------|
| 0xF800 | JP 0xF835 | Boot entry |
| 0xF803 | JP 0xF8A9 | Console input |
| 0xF806 | JP 0xF833 | NOP/RET |
| 0xF809 | JP 0xF89A | Console output |
| 0xF80F | JP 0xF84A | List output |
| 0xF818 | JP 0xF857 | Home disk |
| 0xF81B | JP 0xF833 | NOP/RET |
| 0xF81E | JP 0xF83B | System reset |
| 0xF821 | JP 0xF88F | Read sector |
| 0xF824 | JP 0xF872 | Write sector |

## FDC Driver (0xF8xx)

The ROM-resident disk driver sits in the 0xF84x-0xF8Fx range and drives the
Beta128/WD1793 ports directly (see [fdc-driver.md](fdc-driver.md) for the
port-level routines; the 0x14xx listings there are the RAM-loaded equivalents
of the same driver family).

Observed in the failed-open trace (directory search for a missing file):

```text
F845                ; enter directory search
F8FE-F8D8           ; status-poll burst on the Beta128 status port
F848, F849          ; read ID / reposition
```

## BDOS Implementation (0xEAxx)

The BDOS function bodies execute at 0xEA9F-0xEABD inside the service
mapping. Observed sequence during FCB OPEN processing:

```text
EA9F-EAB6           ; open/directory-search core
EB0D-EB12           ; FCB setup / result marshalling
EAB9, EABC, EABD    ; return-path pieces -> back to caller's TPA
```

## Warm Boot Target (0xD400)

When a transient terminates (RET from 0100 or a failed launch), control
returns to the CCP warm-boot entry at **0xD400**. Watching for the first M1
at 0xD400 after a launch is the cleanest "game exited back to CP/M"
detector.

## Port Groups

Two port groups matter when tracing the monitor and any transient that
manages memory itself:

| Group | Decode | Purpose |
|-------|--------|---------|
| `xx77` (any port with low byte 0x77) | ATM manager port | Latches `aFF77`: video mode (bits 0-2 -> `pFF77`), plus PEN (a8) and CPM (a9) control lines. Video mode 0 = M_ATM16 320x200x16. |
| `xFF7` (low byte 0xF7) | Memory manager window | Selects the RAM page visible in the 0xC000-0xFFFF window (`pFFF7[0..7]` per 16K segment). |

The monitor drives 7FFD through the `#FD` mirror; ATM710 answers the 7FFD
group on any port with A15=0, A2=1, A1=0 (see `portdecoder_atm710.cpp`).

## Boot Flow (observed)

1. Reset -> monitor menu (screen contains "SPECTRUM128" in the item list)
2. ENTER on the CP/M entry -> CP/M 2.2 (XVR BIOS V1.07.13) starts from ROM
3. The monitor reads and caches the disk catalog during boot, before the
   "A>" CCP prompt appears
