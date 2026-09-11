# ProfROM SMUC "not found" messages - reasons and driver disassembly

Status: analysis complete (2026-09-10). Documents why the ProfROM service monitor prints
`CMOS not found`, `NVRAM not found`, `Interrupt controller not found` and
`IDE controller not found` on every boot of the PROFSCORP model, the full disassembly of
the firmware driver that performs the checks, and the agreed direction for lightweight
SMUC stubs (see final section).

All ROM addresses are ProfROM 4.01 (`data/rom/scorp_prof401.rom`, 512 KB = 128 pages of
16 KB). Quadrant-0 plane-1 page numbers use the file mapping `page N = file offset N*0x4000`;
the same driver bytes are duplicated at page 23 (quadrant 2) - every file hit below occurs
twice, 16 pages apart.

## 1. The messages and the live evidence

* The four messages are rendered on the boot status screen of the service monitor.
  They were verified by glyph-reading the rendered frame (the ROM font is 6x8 and the
  text is composed from word tokens, see section 6).
* They appear in every recorded run of this emulator, including the earliest baseline
  (`scratch/baseline_run.log`) and every full-suite log since - the messages are not a
  regression; they have been printed since the first PROFSCORP boot.
* Hardware context: SMUC (Scorpion MOA Universal Controller) is a Turbo+/ISA
  expansion-board controller (IDE bridge + DS1685 RTC/CMOS + 2 KB NVRAM + 8259 PIC +
  ISA window). Our ZS-256 base machine does not include it (scope decision 1 in
  [README.md](README.md)), so the checks probe absent hardware.

## 2. Why every check fails (emulator mechanism)

The SMUC sub-devices are probed through a single serial-link port, `#FFBA`
(see [ports.md](../../ports/ports.md) section SMUC for the full port map):

* `PortDecoder` never decodes `#FFBA`: `PortDecoder::DecodePortIn` returns `0xFF` for
  undecoded reads (`core/src/emulator/ports/portdecoder.cpp:106`), and `#FFBA` is an
  even port (A0 = 0), so `Z80::in` applies no floating-bus override
  (`core/src/emulator/cpu/z80.cpp:612`).
* The firmware presence check (helper `#0EDE`, section 4.3) releases the open-collector
  data line and samples bit 6 of `#FFBA`. ACK = bit 6 **low** (the device pulls the line
  down). Reading back `0xFF` forever means no device ever ACKs.
* Every probe therefore times out after 200 attempts (helper `#0E91`, section 4.5),
  returns carry-set, and the caller prints the "not found" message for that device.

This is hardware-accurate behavior for a SMUC-less machine - not an emulator defect.

## 3. Forensics: no lost fix ever existed

Before this analysis the messages were suspected to be a regression ("already fixed
before changes lost"). Exhaustive forensics found no such fix in any recoverable
artifact:

* all branches, the reflog, `git stash list` and dangling commits on this repository;
* all transcripts and scope documents of the clone project;
* the messages are present in the earliest baseline logs.

Conclusion: the checks have always failed in this codebase lineage. The agreed
resolution is the stub direction in section 8.

## 4. Driver disassembly (page 7, `#0E00`-`#0F55` and `#0D51`-`#0DFE`)

The SMUC serial link is a 3-wire, open-collector interface driven through port `#FFBA`:

| Bit | Mask | Direction | Role |
|-----|------|-----------|------|
| 4 | `#10` | Z80 -> device | SDA out (data from Z80) |
| 6 | `#40` | Z80 -> device / device -> Z80 | SCL out (clock) AND data in (open collector) |
| 5 | `#20` | Z80 -> device | WP (write protect, set at transaction end) |

Line state is shadowed at RAM `#DFF0`. Sub-device select byte = `#A0 OR (device << 1)`
plus bit 0 = read flag; the sub-device register/address travels in the byte after the
select (transaction layer, section 4.6).

### 4.1 Init and primitives

```asm
#0F42  LD   BC,#FFBA        ; serial-link port
       LD   A,(#DFF0)       ; shadowed line state
       AND  #DF             ; WP off
#0F4A  OR   #50             ; bits 6,4 high (idle: clock+data released)
#0F4C  LD   (#DFF0),A       ; update shadow
       OUT  (C),A
       RET

#0F52  PUSH BC / POP BC     ; timing delay (bus turnaround)
```

Start (falling SDA while SCL high) and stop (rising SDA while SCL high) conditions:

```asm
#0F2C  OR   #50             ; SCL+SDA high
       OUT  (C),A ; delay
       AND  #EF             ; SDA low  -> START
       OUT  (C),A ; delay
       AND  #BF             ; SCL low (fall into #0F4C: shadow + out)

#0F1C  AND  #EF             ; SDA low
       OUT  (C),A ; delay
       OR   #40             ; SCL high
       OUT  (C),A ; delay
       JR   #0F4A           ; SDA high -> STOP

#0F3E  OR   #20             ; WP on (transaction epilogue)
       JR   #0F4A
```

### 4.2 Shift out `#0EF7` (MSB first, device samples on SCL rising edge)

```asm
#0EF7  PUSH HL
       LD   L,#08           ; 8 bits
       AND  #BF             ; SCL low
       OUT  (C),A
#0EFE  PUSH BC / POP BC     ; timing
       AND  #EF             ; SDA low
       RL   D               ; MSB into carry
       JR   NC,#0F08
       OR   #10             ; SDA high
#0F08  OUT  (C),A           ; present data bit
       PUSH BC / POP BC
       OR   #40             ; SCL high -> device latches SDA
       OUT  (C),A
       CALL #0F52
       AND  #BF             ; SCL low
       OUT  (C),A
       DEC  L / JR NZ,#0EFE
       POP  HL / RET
```

### 4.3 ACK / presence check `#0EDE` (Z = device present)

```asm
#0EDE  OR   #10             ; release SDA (open collector)
       OUT  (C),A ; CALL #0F52
       OR   #40             ; SCL high - 9th clock
       OUT  (C),A
       PUSH HL
       IN   L,(C)           ; sample the line
       CALL #0F52
       AND  #BF             ; SCL low
       OUT  (C),A
       BIT  6,L             ; ACK = bit 6 LOW (device pulls line down)
       POP  HL / RET        ; Z flag -> caller
```

### 4.4 Shift in `#0EB8` (MSB first, device drives SDA)

```asm
#0EB8  PUSH HL
       LD   L,#08
       AND  #BF / OR #10    ; SCL low, SDA released
       OUT  (C),A
#0EC1  CALL #0F52
       OR   #40             ; SCL high
       OUT  (C),A ; NOP ; NOP
       IN   H,(C)           ; sample bit 6
       SLA  H / SLA H       ; bit 6 -> carry
       RL   D               ; into result
       CALL #0F52
       AND  #BF             ; SCL low
       OUT  (C),A
       DEC  L / JR NZ,#0EC1
       POP  HL / RET
```

### 4.5 Sub-device select and presence poll

```asm
#0EA5  PUSH AF / EXX
       LD   A,H             ; caller's H (exx set) = device index
       EXX
       AND  #07
       RLCA                 ; device << 1
       OR   #A0             ; select byte prefix
       LD   D,A / POP AF / RET

#0EB1  PUSH AF / EXX
       LD   A,L             ; caller's L (exx set) = sub-address byte
       EXX
       LD   D,A / POP AF / RET

#0E91  LD   L,#C8           ; 200 attempts
#0E93  CALL #0F2C           ; START
       CALL #0EA5           ; D = select byte
       CALL #0EF7           ; shift select out
       CALL #0EDE           ; ACK?
       RET  Z               ; device answered
       DEC  L / JR NZ,#0E93
       SCF  / RET           ; carry = no device
```

### 4.6 Transaction layer

Read transaction `#0E01`: init (`#0F42`), presence poll (`#0E91`), write sub-address
(`#0EB1` + `#0EF7` + `#0EDE`), RESTART (`#0F2C`), select+read (`#0EA5`, `SET 0,D`,
`#0EF7`, `#0EDE`), read byte (`#0EB8` + `#0EDE`), STOP+WP (`#0F1C`, `#0F3E`); result in
A (=D), carry set on any failure.

Write transaction `#0E4B`: same prologue, then the caller's byte (saved in E at entry)
is shifted out twice in a row (`LD D,E` / `#0EF7` / `#0EDE` for data and for address
forms). `#0E72` writes two bytes (E then D). `#0E80` is the bare presence probe
(init + poll + STOP).

### 4.7 NVRAM driver layer `#0D51`-`#0DFE`

Sits directly on the transaction layer; the NVRAM appears as a 2 KB address space
behind the serial link:

* `#0D51`: read the two bytes at sub-addresses `#00FE`/`#00FF` into HL (stored checksum).
* `#0DE8`: read sub-addresses `#0000`-`#00FD`, folding each byte through the CRC at
  `#220F` (DE = seed `#FFFF`), producing the computed checksum.
* `#0D62`: validation entry - stored vs computed checksum; on match sets the NVRAM-OK
  flag `SET 5,(IY+#1A)` and returns code `#27`; mismatch/absence returns code `#26`
  with carry set (`#0DA1`), which the boot screen renders as `NVRAM not found`.
* `#0DAD`: bulk read of 2 KB (DE = `#0800`) from sub-address 0 into RAM `#7530`
  (error `#69` on failure); `#0DC7`: bulk write back (same error on failure).
* A first-boot path (`#0D6F`) writes a default byte (`#61`) through `#0E4B`/`#0E72`
  and re-validates - i.e. the firmware is prepared to *format* an empty NVRAM, but
  only if the device ACKs; with no ACK it reports "not found" instead.

### 4.8 IDE detection (page 4, around `#0BC0`)

ASCII strings `IDE/AT` (page 4 `#0BC0`), `Serial Number:` (`#0BED`), `Firmware...`
(`#0BFC`) mark the ATA IDENTIFY flow: it drives the SMUC IDE window
(`#F8BE`-`#FFBE` -> ATA `#1F0`-`#1F7`/`#3F6`, see ports.md) through the paged-LDIR
engine and prints drive identity strings on success. With the window undecoded (reads
float to `0xFF`) the IDENTIFY handshake never completes and the check degrades to the
`IDE controller not found` message.

## 5. Port map (summary)

| Port | Device | Notes |
|------|--------|-------|
| `#FFBA` | SYS serial link | probed by every presence check (this doc) |
| `#5FBA` / `#5FBE` | version / revision registers | |
| `#7EBE` / `#7FBE` | 8259 PIC | "Interrupt controller" |
| `#7FBA` | virtual FDD | |
| `#D8BE` | IDE high byte | 16-bit data path |
| `#DFBA` | DS1685 RTC | "CMOS" |
| `#F8BE`-`#FFBE` | IDE window | ATA `#1F0`-`#1F7`/`#3F6`, "IDE controller" |

Full decode patterns: [ports.md](../../ports/ports.md) section SMUC.

## 6. Message strings (token composition)

The messages are not stored as plain ASCII. Page 5 carries two vocabulary tables using
the convention "last letter OR #80" (token terminator):

* general table (alphabetical, around `#15A0`): `foun[d]`, `no`, `no[t]`, `nam[e]`, ...
* SMUC table (around `#12F0` and `#1720`): `dismoun[t]`, `interrup[t]`,
  `controll[e]r`, `NVRA[M]`, `SMU[C]`, `LB[A]`, `magi[c]`, `butto[n]`, `monito[r]`,
  `har[d]`, `checks[u]m`, `CMO[S]`, `boo[t]`, `recor[d]`, `bus[y]`, `read[y]`, ...

`CMOS not found` = `CMO[S]` + `no[t]` + `foun[d]` etc.; `IDE/AT` is the one plain ASCII
fragment (detection path, section 4.8). The four boot lines were confirmed against the
rendered screen (section 1).

## 7. What is NOT the cause

* Not the EAR/tape fix or the NMI plane handling (those are separate, fixed issues -
  see [profrom-nmi-gaps-and-findings.md](profrom-nmi-gaps-and-findings.md)).
* Not a timing/phase problem: the probes fail deterministically (`#FFBA` reads `#FF`).
* Not dependent on RAM state across Reset: the probes are pure port transactions.

## 8. Agreed direction: lightweight SMUC stubs (2026-09-10)

User decision: stub the checks so they pass, modeled on other emulators; full IDE is out
of scope for now but the detection should pass. Reference implementation: Xpeccy
(`src/libxpeccy/hdd.c`, functions `ide_smuc_rd`/`ide_smuc_wr`, `nvCreate`/`nvWr`/`nvRd`),
local copy at `/Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy/`:

* `#FFBA` read returns `nvRd() ? 0xFF : 0xBF` - bit 6 is the open-collector data line,
  everything else reads 1 (matches the idle `0xFF` we already return).
* `#FFBA` write feeds the 3-wire state machine:
  `nvWr(nv, val & 0x10 /*sda*/, val & 0x40 /*scl*/, val & 0x20 /*wp*/)`.
* 256-byte NVRAM image persisted with the profile (Xpeccy) - for us a 2 KB backing
  buffer satisfies the `#0D51`-`#0DE8` checksum validation; a zeroed buffer takes the
  firmware's own format path (`#0D6F`), which is acceptable.
* The stub ACKs select bytes, so presence polls (`#0E91`) succeed for all sub-device
  numbers - this is what makes `CMOS`/`NVRAM`/`Interrupt controller` lines disappear.
  Per-device select-byte mapping (`H` values) does not need to be decoded for that.
* IDE detection needs the `#F8BE`-`#FFBE` window decode to answer IDENTIFY-era status
  reads; minimum viable: alternate-status/`0x50`-style answers so the page-4 detection
  completes. Exact port sequence to be captured while implementing.

Implementation and tests are tracked as the `smuc-impl` work item; this document is the
evidence base.

### 8.1 Implementation (2026-09-10)

Landed as:

* `core/src/emulator/memory/nvram.h/.cpp` - class `SMUCNvram`: the Xpeccy LC16 behavioral
  port (`WriteSerialLink` / `ReadSerialLink` / `ResetSerialLinkState`, 2 KB backing buffer,
  contents survive reset; the old parallel-access API and the commented ancestor
  `NVRAM::write` block were removed). Renamed from `NVRAM` to avoid the collision with the
  heritage `struct NVRAM` in `platform.h` (dead `EmulatorState::nvram` layout).
* `core/src/emulator/ports/models/portdecoder_scorpion256.h/.cpp` - `IsPort_SMUC`
  (family mask `0x18FB`, match `0x18BA`: A12, A11 plus the exact low byte `#BA`/`#BE` with
  A2 free - see 8.3 for why the low byte must be exact) gated on `MM_PROFSCORP`, with arms
  placed BEFORE the `#FE` arms on both IN and OUT: every SMUC address also carries the weak
  FE pattern (A5=1, A1=1, A0=0) and would otherwise reach keyboard/border/mic. Sub-decode
  `(port & 0xA044)`: `0xA000` serial link (`ReadSerialLink() ? 0xFF : 0xBF`), `0x8000`
  DS1685 RTC (address/data phase via `pFFBA` bit 7), `0x2000` virtual FDD latch
  (`p7FBA | 0x3F`), `0x0000` version (`0x3F`), `0x0004` revision / `0x2004` 8259 PIC
  (`0x57`), `0x8004` IDE data-high, `0xA004` IDE window task file (register
  write/readback, status reads `0x50`).
* `core/tests/emulator/ports/models/scorpionsmuc_test.cpp` - replays the page-7 primitives
  (`#0F2C` START, `#0EF7` shift-out, `#0EDE` ACK check, `#0EB8` shift-in, `#0F1C` STOP) at
  the port level, checks the register stubs, and verifies the live boot.

Verified live (boot test diagnostics): the NVRAM validation takes the `#0D6F` first-boot
format path (EEPROM[0] = `0x61` by frame ~200), the `#0E72` checksum pair lands at
`#00FE/#00FF` and is later rewritten when the driver saves its config, and the boot panel
grows from a single header line (stubs disabled) to six device/config rows - the four
"not found" messages no longer render. The post-check PC trajectory is identical with and
without the stubs (settles into the same ROM idle loop), so the added decode arms do not
perturb the boot path.

### 8.2 Board-absent default (2026-09-10)

User decision: ship with the SMUC board ABSENT by default - the detected-board boot is
expensive. Measured (PROFSCORP cold boot, per-frame stepping, frozen RTC):

| mode | `#E02D=C0` boot detection | menu idle (`#3683-#3685`) reached |
|------|---------------------------|----------------------------------|
| board present (8.1 stubs) | frame 45 | ~frame 350 |
| board absent (default) | frame 45 | ~frame 150 |

The ~200 extra frames (~4 s at 50 Hz) go to the page-7 serial-link driver: the `#0DE8`
checksum validation reads 254 NVRAM bytes over bit-banged I2C, the `#0D6F` first-boot
format path writes defaults and re-validates, and the config save staging follows. With
the board absent the four presence polls just burn their 200 START/select/ACK retries
(~25 frames total) and everything else is skipped. The turbo boot detection is identical
in both modes.

Mechanics: `PortDecoder_Scorpion256::SetSmucEnabled(bool)` / `IsSmucEnabled()` gate
`ReadSMUCPort`/`WriteSMUCPort`. Absent means the whole `#xxBA/#xxBE` family floats:
reads return constant open-bus `#FF` (NOT the attribute-latch floating stream - its bit 
6 varies with the raster and would ACK presence polls at random screen positions),
writes hit no latch. The stubs stay fully wired behind the flag;
`scorpionsmuc_test.cpp` opts in via `SetSmucEnabled(true)` in its context helper, so the
present-board behavior stays verified. Re-enabling for real use later = flipping the
default or wiring the flag to a config option.

### 8.3 Keyboard regression from the first SMUC mask (2026-09-10)

Symptom: right after the 8.1 change the ProfROM service monitor stopped responding to the
keyboard (second keyboard outage after the earlier `#0066` DI-loop wedge - unrelated cause).
Diagnosed live over WebAPI on the running instance: a 1.5 s hold of any key outside the
6-7-8-9-0 half-row changed neither the screen digest nor the monitor state, while the
machine idled normally.

Root cause: the first `IsPort_SMUC` pattern was derived from the common bits of the four
`#xxBA` base addresses only - mask `0x18A3` (A12, A11, A7, A5, A1 set, A0 clear), match
`0x18A2`. Every keyboard row port ends in `#FE` (A7, A5, A1 set, A0 clear), so the low
byte always matched, and every row high byte except `#EF` carries A12 and A11: 7 of the 8
rows (`#FEFE #FDFE #FBFE #F7FE #DFFE #BFFE #7FFE`) fell into the SMUC arm, which sits
BEFORE the `#FE` arm (ordering is mandatory - SMUC addresses carry the FE pattern). The
monitor's scan read the SMUC open bus (`#FF`) instead of the key matrix and saw every row
unpressed; only the `#EFFE` half-row (6-7-8-9-0) kept working. The old `static_assert`
checked only the bare `0x00FE` base port, which no scanner uses.

Fix: all documented SMUC ports end in the exact low byte `#BA` or `#BE` (A2 splits them).
The family mask now pins the full low byte (mask `0x18FB`, match `0x18BA`), keeping only
A12/A11 plus the sub-device bits from the high byte. The `static_assert` set grew explicit
`#FEFE`/`#FDFE` exclusion checks, and `ScorpionSMUC_Test.KeyboardRowsAreNotShadowedBySmuc`
holds one key per previously shadowed half-row and asserts the pressed bit reads 0 through
`DecodePortIn` (with the board PRESENT, so both arms are exercised simultaneously).

Verified live over WebAPI: fresh PROFSCORP boot, NMI into the monitor, then 1.5 s holds of
`1` (row `#F7FE`), `E` (row `#FBFE`) and `6` (row `#EFFE`) - each hold changes the screen
digest and the monitor redraws (menu navigation / command line), where the broken build
showed zero reaction.
