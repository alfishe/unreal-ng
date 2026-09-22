# PR2.COM Loader Disassembly (Prince of Persia, ATM CP/M)

PR2.COM (22528 bytes, directory blocks c8-d2 on `prince.trd`) is a packed,
self-unpacking launcher for banked 512K/1024K machines. It loads the
PRINCE*.OVL / *.FNT / *.DAT data files through the #FF7 memory window and
switches the machine into EGA mode.

## On-Disk Image vs Runtime TPA

The file image does **not** match the runtime TPA: only 599 of 22528 bytes
are identical after launch. The on-disk image starts with a pointer table
(`76 5C ...`); the byte at 0100 in the file is `76` (HALT), so the entry
seen at runtime is produced by unpacking, not by the file bytes.

Unpacked entry chain (verified from a runtime TPA snapshot):

```text
0100: C3 74 1F     JP 1F74
1F74: 31 F0 A1     LD SP, #A1F0    ; own stack below CCP
      CD 80 44     CALL 4480       ; unpack/relocate core
      ...          string copies -> F200-F600 (config/params)
      FB           EI              ; frame ISR required from here on
      C3 67 20     JP 2067         ; main
```

## 0x2577 - Window Programming Helper (hand-decoded)

The game's own #FF7 window programmer. Uses an entry at table **030F**
(per-window control byte); the instruction stream required M1 realignment
to decode (overlapping prefixes).

```z80
2577: C5            PUSH BC
2578: 01 0F 03      LD BC, #030F    ; control-byte table
257B: 08            EX AF, AF'
257C: 0A            LD A, (BC)      ; A = table entry
257D: 08            EX AF, AF'
257E: 02            LD (BC), A      ; write back (toggle/advance)
257F: E6 1F         AND #1F
2581: F6 80         OR #80
2583: 01 F7 FF      LD BC, #FFF7    ; memory-manager window port
2586: 2F            CPL
2587: ED 79         OUT (C), A      ; select window page
2589: C1            POP BC
258A: 08            EX AF, AF'
258B: C9            RET
```

Effect: reads the control byte at 030F+n, derives a page number, and
publishes it to the window group. The loader calls it twice per block: once
for the source staging setup, once (page value from counter **03ED**) to
aim the window at the destination bank before the LDIR.

## 0x2670-0x26E3 - Overlay Loader

Runs after a successful FCB open of the data file (FCB lives at **0336**,
always with drive byte 0 = CURRENT drive):

```text
LD (03EA), A          ; save open result / file id
FCB setup at 0336     ; name template
CPI name loop         ; match remaining directory names
CALL 2577             ; program window (staging)
set DMA 0080          ; C=1A
read loop             ; C=14 sequential reads into 0080
F3                    DI
CALL 2577             ; program window: page from counter (03ED)
LDIR                  ; 128 bytes from 0080 -> windowed destination
```

The 128-byte LDIR repeats per block, walking the #FF7 window through the
RAM banks - this is how the overlays reach memory outside the Z80 64K. On
the emulated machine this shows up as 128-160 FDC commands **after** the
EGA mode switch (the game streams overlays while already displaying).

## Wait Sites (do not mistake for hangs)

| Address | What it is | Trace signature |
|---------|-----------|-----------------|
| 0x54E4 | string-copy loop | short M1 run, memory writes |
| 0x275E | LDIR block copy | 275E repeated (LDIR refetches M1 each iteration; ~330 seen) |
| 0x386F | spin / delay loop | 1958+ consecutive M1s |
| 0x3872 | frame-counter poll: `LD A,(0FA3) / CP E / JR Z` | tight 3-instruction loop |

All of these execute with interrupts potentially enabled; they are only
stuck if the counter at **0FA3** stops advancing.

## Failed-Launch Exit Stream (pre-fix)

With `B:PR2` typed in one line, the CCP loads the transient but re-selects
A: before jumping to it ([bios-ram.md](bios-ram.md)). The transient then
opens "PRINCE3 OVL" (FCB at 0336, drive 0) on the electronic disk:

```text
[348] C=0F A=00 DE=0336 HL=03E8 from=2674 retA=FF   OPEN-FAILED
```

M1 ring from the EGA switch frozen at the first D400 (run-length
compressed):

```text
386F x1958  3871  01C8 01C9          ; banner wait times out
F845  F8FE-F8D8  F848 F849           ; ROM FDC driver: directory search
EA9F-EAB6  EB0D-EB12  EAB9 EABC EABD ; service-session BDOS open path
D400                                 ; CCP warm boot - game is gone
```

The open failure is clean: no BDOS error print, just a RET back to the CCP.

## Successful Launch (verified)

Launch protocol: `B:` + ENTER (prompt `B>`), then `PR2` + ENTER. Observed
on both 1024K and 512K:

| Metric | 1024K | 512K |
|--------|-------|------|
| FDC commands to reach EGA switch | 256 | 256 |
| FDC commands after switch (overlays) | 160 | 128 |
| Title, displayed bank 5 | 5-6 colors, ~44.6k non-bg px | 5 colors, ~44.6k px |
| Compose bank 7 (off-screen) | 16 colors, ~21k px | 16 colors, ~21k px |

The title paints **progressively** - mid-draw states already carry >10000
non-background pixels but only 2-3 colors; assertions must wait for
multi-color AND dense, not pixel count alone.

EGA switch: first `xx77` OUT with `(value & 7) == 0` - video mode 0 =
M_ATM16 320x200x16 plus 7MHz turbo; the CP/M text console is switched out
at that point (deliver keys as raw matrix presses afterwards).

## Game Variables Map (observed)

| Address | Purpose |
|---------|---------|
| 0x030F | #FF7 window control-byte table (see 2577) |
| 0x0336 | transient FCB used for overlay opens |
| 0x03EA | open result / file id save slot |
| 0x03ED | window destination page counter |
| 0x0FA3 | frame counter polled at 3872 |
| 0x5F6D | key FIFO count (INC on enqueue by frame scan, DEC on pop) |

## Emulation Verification

`core/tests/emulator/machines/atm710/atm710_cpm_boot_test.cpp`:

- `CpmDirListsPrinceCatalog` - disk reads/catalog verification
- `Pr2GameRunsOn1024` / `Pr2GameRunsOn512` - full launch, EGA title screen
  and post-switch overlay streaming assertions
