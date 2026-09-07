# Triage: "Scroller by Demarche" Boot & Decrunch Failure Analysis

## 1. Executive Summary

- **Target**: "Scroller by Demarche" (1996) — an iconic Soviet/Russian ZX Spectrum Covox demo written for the **Pentagon 128**.
- **Disk Image**: [`testdata/sound/covox/scroller_by_demarche.trd`](testdata/sound/covox/scroller_by_demarche.trd).
- **User-Reported Symptom**:
  > *"Scroller still doesn't work in emulator - does some loading, then black screen or reset."*
- **Verdict**: **Port `#7FFD` vs `BANK_M` (`$5B5C`) shadow desynchronization in the 128K Sinclair editor environment**.
  When booted from the 128K service menu or 128K BASIC, the 128K editor's RAM trampoline at `$5B00` intercepts the BASIC loader's statement boundary and clobbers port `#7FFD`, reverting Page 4 back to Page 0. As a result, the main executable (`SCROLL12`) loads into the wrong page, the MegaLZ depacker decodes all zeros into Page 2, and jumping to `$9B6B` executes a 25,000-byte NOP slide into ROM reset.

---

## 2. Disk Catalog and Boot Architecture

### 2.1 TRD Catalog Structure
The disk contains 8 files:

| File | Type | Start Address | Length | Sectors | Track | Sector | Purpose |
|---|---|---|---|---|---|---|---|
| `SCROLLER.B` | BASIC | `$014D` | 333 B | 2 | 1 | 0 | Main loader script |
| `SCROLL00.C` | CODE | `$6200` | 178 B | 1 | 1 | 2 | Depack dispatcher & MegaLZ routine |
| `SCROLL15.C` | CODE | `$8000` | 1,708 B | 7 | 1 | 3 | Packed data (depacked to Page 5 @ `$62B2`) |
| `SCROLL10.C` | CODE | `$8000` | 4,601 B | 18 | 1 | 10 | Packed data (depacked to Page 0) |
| `SCROLL11.C` | CODE | `$8000` | 5,342 B | 21 | 2 | 12 | Packed data (depacked to Page 1) |
| `SCROLL13.C` | CODE | `$8000` | 2,962 B | 12 | 4 | 1 | Packed data (depacked to Page 3) |
| `SCROLL17.C` | CODE | `$8000` | 4,371 B | 18 | 4 | 13 | Packed data (depacked to Page 7 @ `$DB00`) |
| `SCROLL12.C` | CODE | `$C000` | 12,155 B | 48 | 5 | 15 | Packed main demo body (depacked to Page 2) |

---

## 3. Disassembly of Boot Components

### 3.1 BASIC Loader (`SCROLLER.B`)

```basic
10 CLEAR VAL"24575": POKE VAL"23624",VAL"0": OUT VAL"254",VAL"0": RANDOMIZE USR VAL"25088"
20 RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL00" CODE
30 RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL15" CODE
40 RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL10" CODE
50 RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL11" CODE
60 RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL13" CODE
70 RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL17" CODE
80 RANDOMIZE USR VAL"25094": OUT VAL"32765",VAL"20": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL12" CODE
90 RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"25088"
```

- **Line 10**: Sets `RAMTOP` to 24575 (`$5FFF`), clears border and screen, initializes SP.
- **Lines 20–30**: Loads `SCROLL00` to `$6200` (depacker) and `SCROLL15` to `$8000`.
- **Lines 40–70**: Successively depacks into pages 5, 0, 1, 3, and 7 while streaming raw parts from disk to `$8000`.
- **Line 80**:
  - `RANDOMIZE USR VAL"25094"`: Depacks `SCROLL17`.
  - `OUT VAL"32765", VAL"20"`: Outputs `0x14` (`0b00010100`) to port `#7FFD`:
    - Bits 0–2 = `4` &rarr; Maps **RAM Page 4** to bank 3 (`$C000`–`$FFFF`).
    - Bit 3 = `0` &rarr; Normal display buffer (Bank 5).
    - Bit 4 = `1` &rarr; ROM 1 (48K BASIC).
    - Bit 5 = `0` &rarr; Paging not disabled.
  - `RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL12" CODE`: Calls TR-DOS (`$3D03`) to load 48 sectors (12,155 bytes) of `SCROLL12` to address `$C000`.
- **Line 90**:
  - `RANDOMIZE USR VAL"25094"`: Depacks `SCROLL12` from Page 4 (`$C000`) into Page 2 (`$8000`–`$BFFF`).
  - `RANDOMIZE USR VAL"25088"`: Jumps to demo entry point at `$9B6B`.

---

### 3.2 MegaLZ Depack Dispatcher (`SCROLL00.C`)

Located at `$6200`:

```z80
; Entry 25088 ($6200) - Start Demo
6200: 31 00 62       LD   SP, $6200
6203: C3 6B 9B       JP   $9B6B          ; Jump to Covox menu / demo entry point

; Entry 25094 ($6206) - Depack Dispatcher
6206: F3             DI
6207: FD E5          PUSH IY
6209: 21 26 62       LD   HL, $6226      ; Pointer into depack table
620C: 7E             LD   A, (HL)        ; Read #7FFD port value
620D: 23             INC  HL
620E: 01 FD 7F       LD   BC, $7FFD
6211: ED 79          OUT  (C), A         ; Switch RAM page to bank 3 ($C000)
6213: 5E             LD   E, (HL)        ; Destination low
6214: 23             INC  HL
6215: 56             LD   D, (HL)        ; Destination high
6216: 23             INC  HL
6217: 4E             LD   C, (HL)        ; Source low
6218: 23             INC  HL
6219: 46             LD   B, (HL)        ; Source high
621A: 23             INC  HL
621B: 22 0A 62       LD   ($620A), HL    ; Advance table pointer for next call
621E: 60             LD   H, B           ; HL = Source
621F: 69             LD   L, C
6220: CD 44 62       CALL $6244          ; Call MegaLZ depacker
6223: FD E1          POP  IY
6225: C9             RET
```

#### Depack Table at `$6226` (5 bytes per entry: `#7FFD`, Destination, Source)
1. Call 1: `#7FFD = 0x15` (Page 5), Dest = `$62B2`, Src = `$8000`
2. Call 2: `#7FFD = 0x10` (Page 0), Dest = `$C000`, Src = `$8000`
3. Call 3: `#7FFD = 0x11` (Page 1), Dest = `$C000`, Src = `$8000`
4. Call 4: `#7FFD = 0x13` (Page 3), Dest = `$C000`, Src = `$8000`
5. Call 5: `#7FFD = 0x17` (Page 7), Dest = `$DB00`, Src = `$8000`
6. **Call 6**: `#7FFD = 0x14` (**Page 4**), Dest = **`$8000` (Page 2)**, Src = **`$C000` (Page 4)**

> [!IMPORTANT]
> **Call 6 is the fatal failure point.** It explicitly maps Page 4 to `$C000`, expects `SCROLL12`'s compressed stream there, and decodes it into Page 2 (`$8000`–`$BFFF`) which contains the menu code at `$9B6B`.

---

## 4. Root Cause Analysis: Port `#7FFD` vs `BANK_M` (`$5B5C`) Desynchronization

### 4.1 The 128K ROM SWAP Trampoline at `$5B00`
In standard 128K Sinclair architecture, the 128K editor executes from ROM 0, while the 48K BASIC interpreter executes from ROM 1. To alternate execution between ROM 0 and ROM 1, 128K ROM installs a 20-byte routine at `$5B00` in the printer buffer area:

```z80
5B00: F5          PUSH AF
5B01: C5          PUSH BC
5B02: 01 FD 7F    LD   BC, $7FFD
5B05: 3A 5C 5B    LD   A, ($5B5C)   ; Read BANK_M sysvar (shadow copy of port 7FFD)
5B08: EE 10       XOR  $10          ; Toggle bit 4 (switch ROM 0 <-> ROM 1)
5B0A: F3          DI
5B0B: 32 5C 5B    LD   ($5B5C), A   ; Store updated value back to BANK_M
5B0E: ED 79       OUT  (C), A       ; Output to #7FFD hardware port
5B10: FB          EI
5B11: C1          POP  BC
5B12: F1          POP  AF
5B13: C9          RET
```

At `$5B14`, the 128K editor provides an inter-ROM trampoline:
```z80
5B14: CD 00 5B    CALL $5B00        ; Call SWAP (return address = $5B17)
5B17: E5          PUSH HL
5B18: 2A 5A 5B    LD   HL, ($5B5A)  ; Read return address from SWAP_ADDR
5B1B: E3          EX   (SP), HL
5B1C: C9          RET
```

### 4.2 The Desynchronization Trap
1. Line 80 executes:
   ```basic
   OUT VAL"32765",VAL"20": RANDOMIZE USR VAL"15619": REM : LOAD "SCROLL12" CODE
   ```
2. The Z80 executes `OUT ($7FFD), A` with `A = 0x14`:
   - Hardware port `#7FFD` is set to `0x14` (Page 4 mapped to `$C000`).
   - **RAM address `$5B5C` (`BANK_M`) is untouched** (still contains `0x00`).
3. The 48K interpreter completes the `OUT` statement and reaches the colon (`:`).
4. At the statement separator, control returns to the 128K editor via `$5B14`:
   - `$5B14` calls `$5B00` (`SWAP`).
   - `$5B00` reads `$5B5C` (`0x00`), toggles bit 4 &rarr; `0x10`, and outputs `0x10` to `#7FFD`!
   - **`0x10` has bits 0–2 = `0` &rarr; RAM Page 0!**
   - **Page 4 is instantly disconnected, and Page 0 is paged in.**
5. Then the 128K editor starts the next statement:
   `RANDOMIZE USR 15619: REM : LOAD "SCROLL12" CODE`
   - It swaps back to ROM 1 via `$5B00`, writing `0x00` or `0x10` to `#7FFD`, **keeping Page 0 mapped**.
6. TR-DOS (`$3D03`) receives the `LOAD "SCROLL12" CODE` request and streams 48 sectors to `$C000`.
   - Because Page 0 is in bank 3, **all 48 sectors land in Page 0**.
   - **Page 4 remains unwritten (all `0x00`)**.
7. Line 90 executes:
   ```basic
   RANDOMIZE USR VAL"25094": RANDOMIZE USR VAL"25088"
   ```
   - Call 6 of the depacker selects Page 4 and attempts to depack from `$C000` to `$8000` (Page 2).
   - Reading `0x00` bytes from Page 4 causes the depacker to fill Page 2 (`$8000`–`$BFFF`) with all zeros.
   - `USR 25088` executes `LD SP, $6200; JP $9B6B`.
   - Address `$9B6B` in Page 2 now contains `0x00` (`NOP`).
   - The CPU slides through 25,000 bytes of `NOP`s:
     `$9B6B` &rarr; `$FFFF` &rarr; wraps to `$0000` (reset vector).
   - **The machine resets to the boot ROM / black screen.**

---

## 5. Hardware Bus Trace & Event Timeline

Captured directly from the emulator's Z80 bus tracer in [`Scroller_Boot_Test.DISABLED_RealtimeGuiFlow_NoPerturbation`](core/tests/loaders/disk/scroller_boot_test.cpp#L716):

```
[SEQ 285] OUT #7FFD value=10 pc=5B10 caller=1E7A -> p7FFD=10 bank0=48k trdos=0 bank3=page0
[SEQ 286] OUT #7FFD value=14 pc=1E7F caller=0000 -> p7FFD=14 bank0=48k trdos=0 bank3=page4   <-- Line 80 OUT
[SEQ 287] OUT #7FFD value=00 pc=5B10 caller=5B17 -> p7FFD=00 bank0=128 trdos=0 bank3=page0   <-- $5B00 SWAP clobber!
[SEQ 288] OUT #7FFD value=10 pc=5B10 caller=0018 -> p7FFD=10 bank0=48k trdos=0 bank3=page0   <-- Swapped for TR-DOS
...
[SEQ 301] DOS entry USR 15619 (hit #7), p7FFD=10 bank3=page0                                  <-- TR-DOS loads to Page 0!
...
[SEQ 318] DEPACK call #6: entry=623F page=14 dest=8000 src=C000 p7FFD=10 bank3=page0
[SEQ 320] POST-DEPACK: p0=CDFF9385... p4=00000000... @8000=00000000                           <-- Page 4 empty, Page 2 zeroed!
DEMO MAIN reached at $9B6B
[REALTIME] outcome: menuSeen=0 spaceSent=0 resetSeen=1
[REALTIME] PC=BC3C (NOP slide) -> $0000 RESET
```

---

## 6. Why Authentic Pentagon 128 Hardware in 1996 Did Not Crash

On an authentic Soviet/Russian **Pentagon 128**:
1. **Boot Hardware**: The Pentagon 128 jumper configuration booted directly into **TR-DOS** (`RESET=DOS`) or **48K BASIC** (`RESET=BASIC`), or through **Mr Gluk's Reset Service**.
2. **No 128K Editor Trampoline**: In native TR-DOS or 48K BASIC, the Sinclair 128K editor was **never initialized**. The `$5B00` SWAP routine was never copied to RAM, and no statement-boundary hook existed.
3. **Uninhibited Port Control**: `OUT 32765, 20` wrote directly to the 7FFD hardware latch. No software hook altered `#7FFD` during or after the statement, so Page 4 remained mapped for the entire 48 sectors of `SCROLL12`.
4. **Recent Regression Context**: Commit [`79bd9291`](https://github.com/alfishe/unreal-ng/commit/79bd9291) switched `unreal.ini` default from `RESET=BASIC` to `RESET=128`. In Unreal-NG, launching the emulator placed users in the 128K menu, arming the `$5B00` hook and reproducing the crash.

---

## 7. Remediation & Fix Options

### Option 1: Universal Dual-Mode BASIC Loader Patch (`scroller_fixed.trd`)
The cleanest standalone software fix for the disk image itself is to patch Line 80 of `SCROLLER.B`:

```basic
REM Original Line 80 (333 bytes total, fails under 128K Sinclair editor):
80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE

REM Patched Line 80 (349 bytes total, boots under all menus and models):
80 RANDOMIZE USR VAL "25094": POKE VAL "23388",VAL "20": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM : LOAD "SCROLL12" CODE
```

#### Why This Works Universally
1. **Under Sinclair 128K Editor**: `POKE VAL "23388", VAL "20"` sets system variable `BANK_M` (`$5B5C` / 23388). When the statement ends at the colon (`:`), the editor's `$5B00` SWAP routine toggles bit 4 across ROM switches while keeping RAM Page 4 mapped in bits 0–2. TR-DOS loads all 48 sectors of `SCROLL12` directly into Page 4.
2. **Under 48K BASIC / Native TR-DOS Boot**: `OUT VAL "32765", VAL "20"` directly writes to hardware port `#7FFD` on the bus. (In 48K mode, the 128K editor is never initialized, so `POKE` alone would not trigger an `OUT`).
3. **TR-DOS Catalog Parameter 2 Synchronization**:
   In TR-DOS catalog entry 0 for `SCROLLER.B`:
   - Bytes 9–10 (`param 1`): Total program length.
   - Bytes 11–12 (`param 2`): Program length without variables.
   When expanding Line 80 from 333 to 349 bytes, **both** `param 1` and `param 2` must be updated to 349. If `param 2` remains at 333, Sinclair BASIC sets `VARS` (`$5C4B`) at `PROG + 333` (in the middle of Line 90), corrupting Line 90 into variable tokens (`0x80 0x0D...`) and throwing `C Nonsense in BASIC, 90:1`.
4. **Artifact**: Use [`patch_scroller_trd.py`](docs/disasm/demo/scroller/patch_scroller_trd.py) to generate [`scroller_fixed.trd`](docs/disasm/demo/scroller/scroller_fixed.trd), [`scroller_fixed.$B`](docs/disasm/demo/scroller/scroller_fixed.$B), and [`scroller_fixed.bin`](docs/disasm/demo/scroller/scroller_fixed.bin).

### Option 2: Standalone 128K SNA Snapshot (`scroller_by_demarche.sna`)
For zero-friction playback and regression testing, [`make_scroller_sna.py`](docs/disasm/demo/scroller/make_scroller_sna.py) pre-decrunches all 7 parts directly into their designated 128K RAM banks and outputs a standard 131,103-byte `.sna` snapshot.
- Skips 10 seconds of disk transfers and decompressions entirely.
- Starts instantly at the Covox menu (`$9B6B`).
- Completely immune to loader, disk, or firmware menu issues.

### Option 3: Authentic Pentagon Boot Mode Configuration (`RESET=BASIC` / `RESET=DOS`)
In [`data/configs/pentagon128k/unreal.ini`](data/configs/pentagon128k/unreal.ini#L48):
```ini
RESET=BASIC   ; or RESET=DOS
```
Restores authentic 1996 Soviet Pentagon 128 hardware jumper behavior: the machine boots directly into TR-DOS or 48K BASIC. The Sinclair 128K editor is never invoked, the `$5B00` trampoline is never installed, and the original untouched `scroller_by_demarche.trd` runs cleanly.

### Option 4: Emulator Hardware-Assisted Shadow Synchronization (Evaluated & Rejected)
Mirroring hardware `#7FFD` writes automatically to `$5B5C` in C++:
```cpp
// In PortDecoder_Pentagon128::Port_7FFD_Out():
state.p7FFD = value;
memory.DirectWriteToZ80Memory(0x5B5C, value);  // REJECTED: Violates hardware fidelity
```
> [!CAUTION]
> **Rejected by all major emulators (Unreal, USP, Xpeccy, ZXMAK2)**:
> Hardware port latches do not write to RAM on real Z80 circuits. Forcing writes to `$5B5C` breaks hardware accuracy and corrupts programs that repurpose the printer buffer space (`$5B00`–`$5BFF`) for custom sound drivers or stack buffers.


---

## 8. Verification Test Reference

The reproduction and validation scenarios are codified in:
- [`core/tests/loaders/disk/scroller_boot_test.cpp`](core/tests/loaders/disk/scroller_boot_test.cpp)
  - `Scroller_Boot_Test.BootScrollerDemoTRD`: 48K/Clean TR-DOS boot path (passes).
  - `Scroller_Boot_Test.BootScrollerDemoTRD_Via128KMenu`: 128K menu path reproducing the authentic `$5B00` desync failure.
  - `Scroller_Boot_Test.BootScrollerFixedTRD_Via128KMenu`: 128K menu path loading `scroller_fixed.trd`, verifying all 7 TR-DOS loads, all 6 MegaLZ unpacks, `#7FFD` port switches, Covox menu entry (`$9B6B`), Space trigger (`$9CD6`), and 50Hz IM2 audio execution.
  - `Scroller_Boot_Test.RunGeneratedScrollerSNA`: Automated validation of the clean 128K `.SNA` snapshot.
  - `Scroller_Boot_Test.DISABLED_RealtimeGuiFlow_NoPerturbation`: Full passive realtime bus trace pin-pointing the exact instruction sequence.

---

## 9. Variant Failure: Mid-LOAD TR-DOS EI Window Split (Detailed Forensic Trace)

### 9.1 Background: The Secondary User Symptom
In certain runtime conditions or emulator configurations where interrupts remain active during disk I/O, users reported a perplexing secondary symptom:
> *"The Covox menu actually appears on screen! But when I press SPACE to start the demo, it immediately crashes or reboots into the Sinclair ROM banner."*

### 9.2 The TR-DOS 5.03/5.04 Per-Sector Interrupt Window
During multi-sector reads, TR-DOS (`$3D03` entry) loops over 256-byte sector transfers. Inside `trdos.rom`:
- Each sector read begins with a `DI` (`$3F16`).
- After transferring the 256 bytes from the WD1793 data register to memory, TR-DOS executes an `EI` instruction at `$3F32` before preparing for the next sector header.
- This creates a tiny interrupt acceptance window between sectors!

### 9.3 The Sector Stream Split at Sector $k$
If a 50 Hz vertical blank interrupt fires during this per-sector `EI` window:
1. The Z80 accepts the interrupt and vectors through RST 38 (`$0038`) to the 128K interrupt service routine.
2. The ISR calls the editor's `$5B00` `SWAP` routine.
3. `$5B00` reads the stale `$5B5C` (`0x00`) and flips `#7FFD` to `0x10` (Page 0).
4. When the ISR returns, TR-DOS resumes loading the *remaining* sectors of `SCROLL12`.
5. But Bank 3 is now mapped to **Page 0** instead of Page 4!

As a consequence, the 48 sectors of `SCROLL12` become split across two different physical RAM pages at sector boundary $k$:
- Sectors $0$ to $k-1$ land in **RAM Page 4**.
- Sectors $k$ to $47$ land in **RAM Page 0**.

### 9.4 The Bifurcated Outcome
When Call #6 of the MegaLZ depacker decodes from Page 4 into Page 2 (`$8000`–`$BFFF`):
- **If $k < 22$**: Page 4 contains too little data. The depacker aborts or generates zeroes early in Page 2. The menu at `$9B6B` is never formed, resulting in an immediate NOP slide into reset.
- **If $k \ge 22$**: The first ~22 sectors contain the depacked code for `INIT1` (`$8000`), the Covox menu (`$9B6B`), and `STARTDEMO` (`$9CD6`).
  - The menu appears on screen!
  - The keyboard loop polls Space and detects user input.
  - Pressing Space enters `STARTDEMO` (`$9CD6`), performs the fade-out, and jumps to `$8000` (`DEMO_ENTRY`).
  - `INIT1` executes, then calls `IM2INI` at `$BF02`.
  - **The trap**: The upper memory of Bank 2 (`$BF00`–`$BFFF`) is located in sectors $35$–$48$, which were written to Page 0 and never made it to Page 4!
  - As a result, the IM2 vector table (`$BE00`–`$BF01`) and the interrupt handler (`$BFBF`) contain uninitialized or zero bytes.
  - When interrupts fire, the CPU fetches a corrupted vector, jumps to uninitialized memory, slides past `$FFFF`, and triggers a hard reset.

This conclusively unifies both user-reported failure modes under a single root mechanism: the desynchronization between port `#7FFD` and `BANK_M` (`$5B5C`).
