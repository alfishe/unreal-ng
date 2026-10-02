# ATM450 requirements

Machine: **ATM Turbo 2 v4.50** ("ATM-Turbo v4.50", UnrealSpeccy model name),
short name `ATM450`, `MM_ATM450`.

**Hardware source (primary):** the MicroArt manual "Многофункциональный
компьютер ATM Turbo — Инструкция по наладке. Описание компьютера" (~1992,
board versions 4.10-5.20): [index](https://zxpress.ru/book.php?id=170),
[appendix 2, port table](https://zxpress.ru/ru/books/chapter/2356),
[Ver 4.50 schematic sheet](https://zxpress.ru/chapters_images/atmturbo-6.jpg).
Where it is explicit it wins over the emulators; where it is silent (PAL
marker timing, palette intensity order) the emulators' shared behavior ships.
No PLD dump or HDL reimplementation of the board exists in public.

References: UnrealSpeccy 0.37.x
(`emulators/github/unreal-speccy`, **primary** — where the references differ,
UnrealSpeccy's behavior is the one we ship until hardware documentation says
otherwise), ZXMAK2 (`emulators/github/ZXMAK2`,
`src/ZXMAK2.Hardware/Atm/MemoryAtm450.cs`). Xpeccy is **not** a reference for
this machine (its `HW_ATM1` is a dead enum; there is no `atm1.c`). All
reference line numbers: see [cross-mapping.md](cross-mapping.md) §3; every
place where the two references disagree is listed in cross-mapping §3.3.

## R1 — Model registry and creatability

- `ATM450` is creatable on every surface (WebAPI `POST /emulator/start`,
  MCP, CLI, Qt menu): `GetPortDecoderForModel` gains a `MM_ATM450` case,
  `IsModelSupported(MM_ATM450)` is true, `IsModelCreatable` resolves because
  `data/configs/atm450/unreal.ini` exists (folder name auto-derived from the
  short name).
- Default RAM 512 KiB; `RAM_512 | RAM_1024` selectable (already in
  `config.h:60` — do not change).
- Frame timing already correct and shared with 710/ATM3
  (`config.cpp` `intstart=1756`, `intlen=32`, frame 69888, line 224). No change.
- `data/configs/atm450/unreal.ini` derives from `data/configs/atm710/unreal.ini`
  with: `HIMEM=ATM450`, `RAMSize=512`, `[ROM] ATM1=rom/atm1.rom` +
  a `ROMSET=ROM.ATM1` whose labels match the verified page order of R3
  (`sys=atm1.rom:0`, `dos=atm1.rom:1`, `128=atm1.rom:2`, `sos=atm1.rom:3`),
  ZX-Evo-only sections removed (`[ZC]`, `[NETWORK] ComPort` — the AVR serial
  and the SD slot are ATM3 hardware; `[EVO]` if present). GS/Covox/MoonSound
  stay: they are bus cards, not machine features.

## R2 — Memory map and ports (the core of the work)

State (all fields already exist in `EmulatorState`): `pFDFD`, `aFE`, `aFB`
(`platform.h:1120,1141-1142`), `p7FFD`, `atmMemSwapped` (vestigial, keep as-is).

**The two address-bus latches hold the LOW address byte (A7-A0) of the I/O
cycle**, not the high byte: UnrealSpeccy `set_atm_aFE((unsigned char)port)`
(`io.cpp:466`) and `comp.aFB = (unsigned char)port` (`io.cpp:1045`); ZXMAK2
`AFE = (byte)addr` / `AFB = (byte)addr` (its `"High address byte"` attribute
description is wrong — the code truncates to the low byte). The software
interface therefore is *which port address* is used: `OUT (#FE)` / `#BE` /
`#9E` / `#7E` select ROM/RAM at `#0000` and the video mode, `IN A,(#FB)` /
`IN A,(#7B)` switch the system ROM on/off.

Write decode groups on A15/A9/A1 (all four are disjoint — this answers the old
OQ-3, there is no collision with the AY):

| Group | A15 | A9 | A1 | `port & 0x8202` | Meaning on ATM450 |
|---|---|---|---|---|---|
| `#7DFD` | 0 | 0 | 0 | `0x0000` | palette write (R4) |
| `#7FFD` | 0 | 1 | 0 | `0x0200` | 128K paging (with the usual 48K lock) |
| `#FDFD` | 1 | 0 | 0 | `0x8000` | `pFDFD` extension latch |
| `#FFFD`/`#BFFD` | 1 | 1 | 0 | `0x8200` | AY (A14 picks select/data) |

Port arms (UnrealSpeccy `memory.cpp:134-162`, `io.cpp:533-537,577-582`):

1. **`pFDFD` latch** — write decode `(port & 0x8202) == 0x8000`. Bits:
   - 1-0 (EA16 / EA17): RAM page extension — page at `#C000` becomes
     `((p7FFD & 7) | (pFDFD & 3) << 3) & ramMask`, 32 pages = 512 KiB, the
     board's maximum (16 × 565РУ7).
   - 2 (RA16): ROM A16, the upper half of a 128 KiB "ROM disc" (27C010);
     selects nothing with the shipped 64 KiB image.
   - 3: CPNET — TR-DOS forces the system ROM (see arm 4).
   Manual schematic (latch D3: D0→EA16, D1→EA17, D2→RA16, D3→CPNET,
   D4/D5 = phone line TON/TNAB) and ZXMAK2 agree. UnrealSpeccy feeds D2 into
   the RAM page instead (its own comment says the hardware does not) — no
   difference at 512 KiB. The model table still offers 1024 KiB for
   UnrealSpeccy parity; with the hardware decode nothing reaches pages 32-63
   (OQ-5).
2. **`aFE` latch** — on every write with A0=0 (`#xxFE` group), latch the
   **low address byte** (`set_atm_aFE`, `atm.cpp:286-292`):
   - bit 7 (A7) = 0 → **RAM at `#0000`** ("CPUS"): window 0 = RAM page 0
     (writable), window 1 = RAM page **4** (not 5 — `memory.cpp:140-145`),
     window 2 unchanged, window 3 per arm 1; an A7 transition rebuilds the banks;
   - bits 6-5 (A6-A5): video mode (R4);
   - no memory swap: the manual wires A6 to RG0 only; UnrealSpeccy's
     `atm_memswap()` on an A6 change is gated behind its default-off
     `AtmMemSwap` option and is not ported (`atmMemSwapped` stays false).
   The `#FE` write itself (border, beeper, tape) and the ATM bright-border bit
   from A3 (arm 5) happen on the same cycle.
3. **`aFB` latch** — on a read with A2=0 that **no earlier read arm claimed**,
   latch the **low address byte**; bit 7 (A7) = CPSYS (system ROM select). Read
   order matters: UnrealSpeccy checks `#FE` (A0=0), GS `#B3/#BB`
   (`io.cpp:724`), the DOS ports, the AY etc. first, and only the fall-through
   reaches the latch (`io.cpp:1040-1055`), which then returns `#FF`. The wide
   `(port & 0x04) == 0` decode is deliberate ("for MODPLAYi"; the strict
   `(port & 0x7F) == 0x7B` line is commented out). Divergence: ZXMAK2 latches
   on every A2=0 read without claiming the bus. We follow UnrealSpeccy: fall-
   through position, floating `#FF` result.
4. **ROM at `#0000`** (when aFE.7 = 1), evaluated on every bank rebuild in this
   order:
   1. `p7FFD & 0x20` (48K paging lock) **clears** `aFB.7` (a sticky state
      change, not just a mapping decision);
   2. TR-DOS active **and** `pFDFD & 8` → **sets** `aFB.7` (sticky, wins over 1);
   3. `aFB & 0x80` (CPSYS) → **sys** ROM;
   4. TR-DOS active → **dos** ROM (whatever 7FFD.4 says);
   5. else standard 128K rule: `p7FFD & 0x10` selects **sos** (48) / **128** ROM.
   TR-DOS-active is the common `CF_TRDOS` session (entry on `#3Dxx` fetch from
   the 48 ROM, exit on execution from RAM), as on every Beta-128 machine.
5. **`#FE` write side effects** shared with the ATM family: bright border from
   A3 (`(port & 8) ^ 8`, `io.cpp:461-463`) into `atmBorderBright`.
6. **`#FE` read**: bit 7 = `atm450_z(t)` — 0x80 normally, three short zero
   windows in a normal-speed frame: t in [7200,7240), [7284,7324),
   [7326,7366), where **t counts from the INT edge** (UnrealSpeccy `cpu.t`
   starts at INT, `while (cpu.t < conf.intlen)`). This core counts frame
   T-states from the frame start and raises INT at `intstart + 1`, so the
   decoder converts: `t = (frameT - (intstart + 1)) mod frame`.
   **This is the system ROM's copy-protection key:** before CP/M starts, the
   ROM halts, waits a fixed delay, samples Z 16 times (~42 T apart, `2027`-
   `2031` in `atm1.rom`) and XOR-decrypts its CP/M loader (`22B3` → `#D400`)
   with the result. Frame-relative windows gave a wrong key and the CP/M menu
   entry silently fell back to the menu.
   Bits 6-0 come from the normal keyboard/tape read. Implement faithfully with
   unit tests pinning the windows; the copy-protection games that read it are
   the point of having the machine at all.
7. **No ATM 7.10 register file**: `#xx77`, `#xxF7` windows, `#EFF7`, the 710
   `#xx9F/#xxBF/#xxDF/#xxFF` palette group, `#xxBF`/`#xxBE`, gluk CMOS, SD must
   **not** decode on ATM450 (the 710 arms are not inherited — the 450 decoder
   has its own arm list).

## R3 — ROM

- File: `data/rom/atm1.rom` (already shipped, 65536 bytes = 4 × 16 KiB).
- Page order **verified** from the image (old OQ-1, resolved 2026-10-01):
  page 0 = **sys** (`DI; JP #3F00` at `#0000`, character set inside, no
  Sinclair strings), page 1 = **TR-DOS 5.03**, page 2 = **128** (menu strings,
  "1986 Sinclair Research"), page 3 = **48 BASIC** ("1982 Sinclair Research").
  This matches `rom.cpp:238-241` (`sys=0, dos=1, 128=2, sos=3`, from
  UnrealSpeccy `config.cpp:898-904`) and ZXMAK2 `GetRomIndex`. The inherited
  `ROM.ATM1` ROMSET in other configs (`data/configs/spectrum3/unreal.ini:645-648`,
  `sos=atm1.rom:0`) is wrong and must not be copied.
- A decoder test pins the page order (signature bytes of each page mapped at
  `#0000` under the R2 arm-4 conditions).

## R4 — Video and palette

- Mode select already implemented and green:
  `Screen::DetectModeATM1` (`screen.cpp:336-357`) reads `(aFE >> 5) & 3`:
  0 → `M_ATM16` (EGA 320×200 16-color), 1 → `M_ATMHR` (640×200 multicolor),
  2 → unused (no picture), 3 → ZX 256×192.
  Port addresses: `#9E`/`#1E` → EGA, `#BE`/`#3E` → hi-res, `#FE`/`#7E` → ZX
  (A7 = ROM/RAM at 0 in each pair). Matrix test exists:
  `atm_video_modes_suite_test.cpp:167` (`ModeMatrix_ATM450_AFEBits`).
- The decoder must trigger `Screen::InitRaster()` when aFE bits 6-5 change
  (same role as `Port_FF77_Out` on 710).
- Text mode 6 is ATM3-only — no ATM450 text mode.
- **Palette exists on ATM450** (old OQ-2, resolved: both references and the
  manual — appendix 2 "PORT 7DFD (WRITE WITH A15=A9=A1=0): D0-D5 bgrBGR").
  Write decode: the `#7DFD` group, `(port & 0x8202) == 0x0000`, no DOS gate,
  no `pen2` gate (UnrealSpeccy `io.cpp:533-537`, ZXMAK2
  `BusWritePort7DFD`). The cell is the 4-bit border color (`border_attr` + the
  A3 bright bit). The data byte layout differs from 7.10: **ATM1 = `--grbGRB`**
  (bits active-low: v = value ^ 0xFF; G=v.2, R=v.1, B=v.0 high bits, g=v.5,
  r=v.4, b=v.3 low bits; bits 7-6 unused), versus ATM2 `grbG--RB`. The
  manual's one-line "bgrBGR" does not say which triplet is the bright one;
  the system ROM settles it: at reset it loads the Sinclair palette, and with
  this layout cell 1 (blue) comes out `#0000AA` and cell 9 (bright blue)
  `#0000FF` (boot test `ATM450Boot_Test.SystemRomBootMenu`, OQ-8). Same 2-bit
  per channel DAC (`0xA * high + 5 * low` on the 4-bit ladder used by
  `Port_ATM_Palette_Out`). UnrealSpeccy `draw.cpp:440-447`, ZXMAK2
  `UlaAtm450.cs` `InitStaticTables` (identical tables).
- Renderers (`ScreenAtm`, `AtmVideoMapper`, `atmgeometry`) are family-shared.
- Border: bright bit from A3 of the `#FE` write (R2 arm 5).

## R5 — Peripherals

- **FDD**: Beta-128 WD1793 on `#1F/#3F/#5F/#7F/#FF` while the TR-DOS session
  has the DOS ports (`CF_DOSPORTS`), like every Beta-128 machine. ZXMAK2
  `FddAtm450.cs` decodes `(port & 0x83) == 0x03` / `(port & 0xE3) == 0xE3`
  — the same partial decode (old OQ-4, resolved). Its gate is `DOSEN ||
  SYSEN` (`General/FddController.cs:205`), and on ATM450 SYSEN = CPSYS with
  ROM at 0, so ZXMAK2 also opens the FDC while the system ROM is mapped;
  UnrealSpeccy opens it only inside the TR-DOS session (`CF_DOSPORTS`). We ship
  UnrealSpeccy (OQ-7); the system-ROM boot test shows whether the sys ROM
  needs the FDC outside a session. The 7.10-only "`#FF` bit 6 ignored"
  quirk (no DDEN wiring) is **not** carried over without evidence: UnrealSpeccy
  ignores bit 6 for every model, so the result is the same either way — keep
  the 710 mask for parity and note it.
- **IDE**: `[HDD] Scheme=ATM` already fits `MM_ATM450`
  (`idecontroller.cpp:149-150`); keep the config-driven fitment.
- **AY** (`#FFFD`/`#BFFD`), GS (`#B3/#BB/#33`), Kempston and the low-byte
  cards: same decodes as 710.
- **Not present on 450** (must not decode): gluk CMOS, Z-Controller SD,
  `EvoAvr`, PS/2 keyboard, board NMI (`xxBF`/`xxBE`).

## R6 — Reset and boot ROM modes (`ApplyBootROMDefaults` override)

Port of UnrealSpeccy `z80.cpp:123-133` (reset) plus the shared
`memory.cpp:375-400` boot-mode table (already in `Memory::SetROMMode`):
- reset clears `pFDFD` (`z80.cpp:91`);
- `RM_DOS`: `aFE = 0x80 | 0x60` (ROM at 0, ZX mode), `aFB = 0` — boots the
  TR-DOS ROM (CF_TRDOS set, 7FFD.4 **kept set**, unlike 710/ATM3; the dos ROM
  wins by the R2 arm-4 rule);
- every other mode (`RM_128`, `RM_SOS`, `RM_SYS`): `aFE = 0x80` (ROM at 0,
  video mode 0 = EGA), `aFB = 0x80` (CPSYS) — the machine **always starts in
  the system ROM**, which then selects the ROM/video mode itself. ZXMAK2's reset
  does the same (`AFE |= 0x80`, `AFB |= 0x80`).
The UnrealSpeccy `CF_SETDOSROM` step trap is the generic TR-DOS entry
mechanism our `Z80Step` already implements — nothing model-specific to port.

## R7 — State, TTD, automation

- `TTDAtmPaging` serializes `aFE`/`aFB` (`ttdatmpaging.cpp:30-31`) but **not
  `pFDFD`** (audit done). `pFDFD` takes the blob's former zero filler byte
  (`reserved0`): the 136-byte layout is unchanged, older recordings restore
  0 = the reset value, ATM710/ATM3 never set it. The ATM450 palette rides the existing `atmPalette` /
  `atmPaletteRegs` state the blob already carries for 710 — verify.
- `TtdClockUnits()` returns **1** (no turbo states; 710 returns 2). If R8's
  turbo ever lands, this becomes 2 and old recordings invalidate — deliberate.
- `DeviceState`/WebAPI/MCP/CLI already reference `MM_ATM450` in their ATM
  branches (`devicestate.cpp:1477`, `state_memory_api.cpp:370`,
  `cli-processor-state.cpp:550`) — audit each branch for fields the 450 lacks
  (FF77, xFF7 windows, CMOS, SD) and report `pFDFD`/`aFE`/`aFB` instead.
- Port-trace session model name and port-map rows for the new decoder
  (`portdecoder.cpp:571` pattern).

## R8 — Non-goals

- **7 MHz turbo**: no software port switches it on the 4.50 board — the
  manual: a "Turbo" toggle switch, latched into D50 by the next `OUT (#FE)`. References:
  UnrealSpeccy ships 3.5 MHz only (its `atm450_z` has dead turbo branches);
  ZXMAK2 ships a separate `UlaAtmTurbo` machine variant that doubles the frame
  tact count. If wanted later, do it as an ini-selectable machine variant,
  not a port — and revisit `TtdClockUnits`.
- **`AtmMemSwap`** (physical A5-A7 ↔ A8-A10 RAM permutation): default-off in
  every reference and not emulated here (same decision as 710/ATM3,
  `platform.h:1144`).
- **ATM Turbo 1** (v1.x-2.x): nobody implements it and no sources exist in the
  reference tree — out of scope permanently unless hardware docs surface.

## Open questions

| # | Question | Status |
|---|---|---|
| OQ-1 | Page order of `data/rom/atm1.rom` | **Resolved**: sys/dos/128/sos = 0/1/2/3 (R3) |
| OQ-2 | Palette on 450 | **Resolved**: yes, `#7DFD` group, ATM1 bit layout (R4) |
| OQ-3 | `#FDFD` vs AY `#FFFD` collision | **Resolved**: none, A9 separates them (R2 table) |
| OQ-4 | ZXMAK2 `FddAtm450.cs` vs the 710 FDD decode | **Resolved**: same partial decode (R5) |
| OQ-5 | `pFDFD` bit 2: RAM (UnrealSpeccy) or ROM A16 | **Resolved by the schematic**: ROM A16 (RA16), shipped. Left open: drop `RAM_1024` from the ATM450 model row (hardware maximum 512 KiB) or keep it for UnrealSpeccy parity |
| OQ-6 | `aFB` latch: fall-through + `#FF` (UnrealSpeccy) or side effect on every A2=0 read (ZXMAK2) | Open. The manual: the A2=0, A0=1 read is the **printer port** read (CPSYS from A7, BUSY on D7, ULINE on D6), so the board drives the bus. Shipped: UnrealSpeccy (fall-through, `#FF`, GS claimed first). On the real board a GS `#BB` read would also flip CPSYS - not emulated |
| OQ-7 | FDC ports while the system ROM is mapped outside a TR-DOS session (ZXMAK2 SYSEN: open; UnrealSpeccy: closed) | Open; ship UnrealSpeccy. The boot menu's TR-DOS / 128 / 48 entries work with it; CP/M needs a CP/M disk to tell |
| OQ-8 | Palette intensity order (`bgrBGR` in the manual) | **Resolved empirically**: the system ROM's Sinclair palette is right with the emulators' `--grbGRB` layout (R4) |
| OQ-9 | `#FE` read bit 7 (Z, the PAL marker): the manual confirms the bit (keyboard buffer D45, from the protected 1556ХЛ8 PLM) but not the timing | **Resolved by the ROM**: the UnrealSpeccy windows, measured from INT, produce the key that decrypts the system ROM's CP/M loader (boot test `MenuCpmLoaderReachesTheDisk`). The PLM has no public dump |
| OQ-10 | CP/M system disk for the 4.50 | **Resolved**: none needed - CCP and BDOS are in the ROM; the loader reads the CP/M directory (cylinder 1). `testdata/machines/atm450/cpm/sys.trd` (NedoPC "SYSTEM" disk for ATM1/2/2+) boots to `A>` and `DIR B:` lists it (boot test `CpmBootsFromSystemDiskAndListsIt`). B: is the floppy, A: the electronic disk |
| OQ-11 | Keys typed in CP/M sometimes arrive as scan code + 1 (next matrix row: `DIR` → `DKR`, `R` → `4`) | Open. Probed: each row read returns the current matrix, the keyboard interrupt runs once per frame - the slip happens in the ROM's shift-state machine (`#5F40`, `l141d`..`l14c3` in the BIOS 1.03 disassembly). Unknown whether the real board shows it with the same key timing; the test types like a user (erase + retype) |

## Definition of done

- `ninja -C cmake-build-agent-release` zero warnings; `test-parallel` green
  (job cap at 50 % cores per `AGENTS.md`).
- `ATM450` creatable and boots: system ROM by default, 128 BASIC, TR-DOS
  (`RM_DOS` and `#3D13` entry); a real ATM-mode program runs (candidate: an
  ATM 16-color title, mirroring `atm710_game2048_repro_test.cpp`).
- TTD record/replay across a mode switch; model switch away/back keeps state.
- `.recipe/_common/machines.md`, `AGENTS.md`, `docs/features/automation.md`
  list the model; `unreal-qt` menu shows it.
