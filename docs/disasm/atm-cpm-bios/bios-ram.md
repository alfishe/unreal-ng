# RAM-Resident BIOS and CCP (0x0000-0x3BFF, 0xD400)

The CP/M 2.2 layer the monitor installs in RAM at cold start: page-zero
vectors, the minimal BIOS at 0x387B, and the CCP console command processor
whose warm-boot entry lives at 0xD400.

## Page Zero

| Address | Content | Meaning |
|---------|---------|---------|
| 0x0000 | JP 0x387E (WBOOT vector) | Warm boot |
| 0x0005 | BDOS entry (`ED 59 00 00`, ATM-specific) | Function dispatch into the service-session BDOS (0xEAxx) |

BDOS entry convention (verified by tracing): the caller does `CALL 0005`
with the function in C and parameters in DE/HL. The **return address of that
CALL sits at (SP)** when the first M1 fires at pc=0x0005, and registers
still hold the caller's values. On return, A carries the result code.

Emulator tracing note: the pushed return address observed at (SP) is one
less than the instruction after the CALL (a `CALL` at 2672 showed the return
address as 2674 instead of 2675) - match `pc == retPc || pc == retPc+1`
when sampling the result code at the returning M1.

## BDOS Functions Observed During Launches

| C | Function | Parameters / result |
|---|----------|---------------------|
| 0x09 | Print string | DE -> `$`-terminated string (PR2 prints an ESC control banner) |
| 0x0D | Disk reset | - |
| 0x0E | Select disk | E = drive (0=A, 1=B) |
| 0x0F | Open FCB | DE -> FCB; returns A=0 ok / 0xFF not found |
| 0x14 | Sequential read | reads one record (128 bytes) to DMA |
| 0x1A | Set DMA | DE = DMA address |
| 0x20 | User code | sets/queries user number before launching a transient |

## CP/M Drive Letters on the v7.10 Monitor

**A: is the electronic (RAM) disk; the floppy is B:.**

This is the reverse of what most CP/M boxes do with a single floppy, and it
is the root cause of the PR2 launch failure (see
[pr2-loader.md](pr2-loader.md)): the game opens its data files with FCB
drive byte 0 (= CURRENT drive), so the floppy must be current when the
transient starts.

## CCP Launch Protocol (observed with BDOS caller tracing)

For a command line `PR2` typed at the `B>` prompt:

```text
C=0E E=01        select drive B (the typed prefix drive)
C=0F DE=DBCD     open FCB "PR2 COM"           -> A=0
~176 x ( C=1A DE=0100+0x80k   set DMA, stepping 0100..5880
          C=14               sequential read ) load the transient
C=0E E=00        re-select drive A (current drive state!)
C=09             print CR/LF line
C=1A DE=0080     DMA back to the default command buffer
C=20             user code
JP 0100           enter the transient
```

Two facts that bite:

1. The re-select at the end restores the **current drive** - after loading
   `B:PR2` in one command line, the CCP leaves A: (electronic disk) current.
2. The transient's FCBs default to drive 0 = current drive, so any data
   files it opens resolve against whatever drive is current at entry.

Correct launch for FCB-drive-0 transients with files on the floppy:

```text
B><B:><CR>       prompt becomes "B>"
B><PR2><CR>      transient starts with B: current
```

## TPA Layout (observed)

| Range | Content |
|-------|---------|
| 0x0100-0xDD00 | TPA proper (M1 filter window when `(p7FFD & 0x10) == 0`) |
| 0x3600-0x387A | BDOS/CCP (RAM copy) |
| 0x387B-0x3A00 | BIOS jump stubs (BOOT at +0, WBOOT at +3) |
| 0xD400 | CCP warm-boot entry (exit target of every transient) |

## Interrupts During a Launch

Healthy launches show both vectors active: NMI (0x0066) ~1800 hits and INT
(0x0038) ~160 hits during the PR2 load, with the game's main loop running
IM2 (`I = 0xF9`). "No interrupts" is not an explanation for a launch failure
on this machine - verify with ISR landing counters before suspecting the
INT path.
