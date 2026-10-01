# ATM450 requirements

Machine: **ATM Turbo 2 v4.50** ("ATM-Turbo v4.50", UnrealSpeccy model name),
short name `ATM450`, `MM_ATM450`. References: UnrealSpeccy 0.37.x
(`emulators/github/unreal-speccy`), ZXMAK2 (`emulators/github/ZXMAK2`,
`src/ZXMAK2.Hardware/Atm/MemoryAtm450.cs`). Xpeccy is **not** a reference for
this machine (its `HW_ATM1` is a dead enum; there is no `atm1.c`). All
reference line numbers: see [cross-mapping.md](cross-mapping.md) §3.

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
  `ROMSET=ROM.ATM1` (label offsets chosen after resolving OQ-1, below),
  ZX-Evo-only sections removed (`[ZC]`, `[NETWORK] ComPort` — the AVR serial
  and the SD slot are ATM3 hardware; `[EVO]` if present). GS/Covox/MoonSound
  stay: they are bus cards, not machine features.

## R2 — Memory map (the core of the work)

State (all fields already exist in `EmulatorState`): `pFDFD`, `aFE`, `aFB`
(`platform.h:1120,1141-1142`), `p7FFD`, `atmMemSwapped` (vestigial, keep as-is).

Port arms (UnrealSpeccy `memory.cpp:134-162`, `io.cpp:577-582`):

1. **`pFDFD` latch** — write decode `(port & 0x8202) == 0x8000`
   (A15=1, A9=0, A1=0 — i.e. the `#FDFD` group, not an exact port). Bits:
   - 2-0: RAM page extension — page at `#C000` becomes
     `(p7FFD & 7) | (pFDFD & 7) << 3` (512 KiB = 5 bits, 1024 KiB = 6 bits
     via `ram_mask`);
   - 3: TR-DOS ROM priority (see arm 4).
   Note the reference comment: "original ATM uses D2 as ROM address extension,
   not RAM" — the emulated behavior (all of D2-D0 as RAM extension) is what
   every emulator ships; follow it.
2. **`aFE` latch** — on every write in the `#xxFE` group (`!(port & 1)`),
   latch the *high address byte* (`set_atm_aFE`, `atm.cpp:286-292`):
   - bit 7 = 0 → RAM at `#0000`: window 0 = RAM page 0, window 1 = RAM
     page 4 (not 5 — reference `memory.cpp:140-145`), window 2 unchanged,
     window 3 per arm 1; aFE.7 transition triggers a bank update;
   - bits 6-5: video mode (R4);
   - bit 6 transition toggles the (non-emulated) memswap flag — mirror the
     `atmMemSwapped` bookkeeping the 710 decoder already does for FF77.0.
3. **`aFB` latch** — on every read with A2=0 (the `#xxFB`/`#xx7B` group;
   UnrealSpeccy deliberately uses the wide `(port & 0x04) == 0` decode,
   "for MODPLAYi", the strict `& 0x7F == 0x7B` variant is commented out):
   latch the high address byte; **bit 7 = CPSYS** (system ROM select).
4. **ROM at `#0000`** (when aFE.7 = 1), in priority order:
   1. `p7FFD & 0x20` (48K paging lock) **clears** `aFB.7` first;
   2. TR-DOS active **and** `pFDFD & 8` → `aFB.7 = 1` (highest priority);
   3. `aFB & 0x80` (CPSYS) → **sys** ROM;
   4. TR-DOS active → **dos** ROM;
   5. else standard 128K rule: `p7FFD & 0x10` selects **128** / **sos** ROM.
   TR-DOS-active itself comes from the boot ROM mode (R6) and the model's
   Beta-128/DOS-entry path exactly as on ATM710.
5. **`#FE` write side effects** shared with the ATM family: bright border from
   A3 (`(port & 8) ^ 8`, `io.cpp:461-463`).
6. **`#FE` read**: bit 7 = PAL flag `atm450_z(t)` — 0x80 normally, three short
   zero windows (normal-speed frame: t in [7200,7240), [7284,7324),
   [7326,7366); ZXMAK2 phases it as `Tact % FrameTactCount`). Implement
   faithfully with unit tests pinning the windows; the copy-protection games
   that read it are the point of having the machine at all.
7. **No ATM register file**: `#xx77`/`#xxFF7`/`#EFF7`/`#xxBF`/`#xxBE`/gluk/SD
   must **not** decode (the 710 arms the subclass inherits must be disabled or
   re-gated). `#FF77`-style palette writes are 710/ATM3-only in the reference
   (`io.cpp:231` gate) — but see OQ-2 for ZXMAK2's `#7DFD` palette claim.

## R3 — ROM

- File: `data/rom/atm1.rom` (already shipped, 65536 bytes = 4 × 16 KiB).
- Page mapping already ported (`rom.cpp:242-248`, from UnrealSpeccy
  `config.cpp:898-904`): `sys=0, dos=1, 128=2, sos=3` for the whole-file load.
- **OQ-1 (resolve before shipping the ini):** the inherited `ROM.ATM1` sets in
  other configs (e.g. `data/configs/spectrum3/unreal.ini:645-648`) label
  `sos=atm1.rom:0` — contradicting the `sys=0` whole-file order. The ROMSET
  loader is label-based so either can be made consistent; resolve by boot test
  (a CPM/sys boot shows immediately which physical page holds the sys ROM) and
  pin the shipped `ROM.ATM1` offsets to the verified layout.

## R4 — Video

- Mode select already implemented and green:
  `Screen::DetectModeATM1` (`screen.cpp:336-357`) reads `(aFE >> 5) & 3`:
  0 → `M_ATM16` (EGA 320×200 16-color), 1 → `M_ATMHR` (640×200 multicolor),
  2 → unused (falls back to ZX mode on the 320×200 raster), 3 → ZX 256×192.
  Matrix test already exists: `atm_video_modes_suite_test.cpp:167`
  (`ModeMatrix_ATM450_AFEBits`).
- Text mode 6 (`FF77_TL`) is ATM3-only — no ATM450 text mode.
- Renderers (`ScreenAtm`, `AtmVideoMapper`, `atmgeometry`) are family-shared;
  no video work beyond wiring the model → family selection if one is needed.
- Border: bright bit from A3 of the `#FE` write (R2 arm 5) — same code path
  the 710 uses for `atmBorderBright`.

## R5 — Peripherals

- **FDD**: Beta-128 WD1793 as on 710 (ZXMAK2 has `FddAtm450.cs` with its own
  decode — diff it against the 710 decode during implementation; expect only
  the DOS-gate to differ). DOS entry/boot modes per R6.
- **IDE**: `[HDD] Scheme=ATM` already fits `MM_ATM450`
  (`idecontroller.cpp:149-150`) — the ATM IDE status read is 710-or-IDE_ATM
  in the reference (`io.cpp:1007`); keep the config-driven fitment.
- **AY**, Kempston, Covox, GS cards: same decodes as 710 unless the reference
  differs — verify in `io.cpp`/ZXMAK2 during implementation.
- **Not present on 450** (must not decode): gluk CMOS, Z-Controller SD,
  `EvoAvr`, PS/2 keyboard, board NMI (`xxBF`/`xxBE`).

## R6 — Boot ROM modes (`ApplyBootROMDefaults` override)

Port of UnrealSpeccy `memory.cpp:375-400` for `MM_ATM450`:
- `RM_128`: clear TR-DOS, clear `p7FFD.4`;
- `RM_SOS`: clear TR-DOS, set `p7FFD.4`;
- `RM_SYS`: set TR-DOS, clear `p7FFD.4`;
- `RM_DOS`: set TR-DOS, **set** `p7FFD.4` (unlike 710/ATM3, which clear it
  again — `memory.cpp:396-398` — the 450 keeps the bit; sos/128 selection is
  then overridden by the dos ROM rule in R2 arm 4).
The UnrealSpeccy `CF_SETDOSROM` step-trap (PC in `#3Dxx`, `z80_main.inl:113-122`)
is their DOS-entry mechanism; our equivalent is the existing Beta-128/DOS-entry
path used by the 710 decoder — do not port the trap itself.

## R7 — State, TTD, automation

- `TTDAtmPaging` already serializes `aFE`/`aFB` (`ttdatmpaging.cpp:30-31`);
  `pFDFD` rides the model-agnostic paging state — verify it is inside the ATM
  blob's contract and add it if not (bump the blob layout consciously).
- `TtdClockUnits()` returns **1** (no turbo states; 710 returns 2). If R8's
  turbo ever lands, this becomes 2 and old recordings invalidate — deliberate.
- `DeviceState`/WebAPI/MCP/CLI already reference `MM_ATM450` in their ATM
  branches (`devicestate.cpp:1477`, `state_memory_api.cpp:370`) — audit each
  branch for fields the 450 lacks (CMOS, SD) rather than adding new ones.
- Port-trace session model name and port-map rows for the new decoder
  (`portdecoder.cpp:571` pattern, `getPortMapEntries`).

## R8 — Non-goals

- **7 MHz turbo**: no software port switches it on the 4.50 board. References:
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

| # | Question | Resolution path |
|---|---|---|
| OQ-1 | Which physical page of `data/rom/atm1.rom` holds the sys ROM (`sys=0` per `rom.cpp` vs `sos=0` per inherited ROMSET labels) | Boot test with both layouts; CPM boot is the discriminator; pin the ini + a decoder test |
| OQ-2 | Palette on 450: UnrealSpeccy gates the `#xx9F`/`#FF` palette writes to 710/ATM3 only; the ZXMAK2 survey attributes a `#7DFD` palette to ATM450 | Read `MemoryAtm450.cs` paging table; if it writes a palette, decide follow-the-majority (no palette) vs ZXMAK2; note in cross-mapping §2 |
| OQ-3 | Does the `#FDFD` group write collide with the AY `#FFFD` data write (both A15=1,A9=0,A1=0)? Reference order in `out1` decides which latch wins | Port the reference's handler order exactly; pin with a test writing `#FFFD` and asserting both latches |
| OQ-4 | ZXMAK2 `FddAtm450.cs` decode vs the 710 FDD decode | Diff during implementation; only the gate is expected to differ |

## Definition of done

- `ninja -C cmake-build-agent-release` zero warnings; `test-parallel` green
  (job cap at 50 % cores per `AGENTS.md`).
- `ATM450` creatable and boots: 128 BASIC, TR-DOS (`#3D13`), sys/CPM ROM
  entry; a real ATM-mode game runs (candidate: an ATM 16-color title from the
  ATM software catalog, mirroring `atm710_game2048_repro_test.cpp`).
- TTD record/replay across a mode switch; model switch away/back keeps state.
- `.recipe/_common/machines.md`, `AGENTS.md`, `docs/features/automation.md`
  list the model; `unreal-qt` menu shows it.
