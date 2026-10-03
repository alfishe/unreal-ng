# Sprinter Sp2000 — goals and requirements

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28); decisions applied |
| **Hardware** | [hardware-reference.md](hardware-reference.md) |
| **Design** | [high-level-design.md](high-level-design.md), [technical-design.md](technical-design.md) |

## 1. Goal in one paragraph

Make the Peters Plus **Sprinter Sp2000** a creatable machine (model key `SPRINTER`) that runs its
own firmware unchanged: the real BIOS ROM starts, fills its port table, shows its setup screen,
and boots **Estex DSS** from a floppy, from a hard-disk image, or from a **folder on the PC**
presented as a FAT16 disk. From the BIOS the user can also drop into the **Spectrum mode**
(BASIC 128/48, TR-DOS 5.04Em reading `.trd` images). Sprinter-native programs (graphics modes,
accelerator, Covox-Blaster) run, and the machine has the same debugging, time-travel (TTD) and
automation support as the other models.

## 2. What "done" means

| Level | The user can… | Proven by |
|---|---|---|
| **D1 firmware** | create a `SPRINTER` machine; the BIOS 3.04 logo and setup screen appear; the date/time and settings survive a restart (CMOS file) | ACC-1, ACC-2 |
| **D2 DSS from floppy** | insert the DSS 1.62 boot floppy (1.44 MB `.img`) and reach the DSS prompt; `DIR` lists the disk | ACC-3 |
| **D3 DSS from HDD** | attach a hard-disk image and boot DSS from it; or point `ide0.master` at a folder and boot DSS from that folder | ACC-4, ACC-5 |
| **D4 Spectrum mode** | leave the BIOS with ESC, get BASIC 128, run TR-DOS and `LOAD` from a `.trd` in drive A | ACC-6 |
| **D5 native software** | run a Sprinter `.EXE` from the DSS disk (graphics demo, Flex Navigator) with correct colors | ACC-7, ACC-8 |
| **D6 accelerator + sound** | a program that uses the accelerator and the Covox-Blaster runs at the right speed with sound | ACC-9 |
| **D7 tooling** | the machine works in the Qt debugger (memory by physical page, port table view), TTD records and replays a DSS session, and every automation surface can create it, insert media and read its state | ACC-10, ACC-11 |

D1-D4 are the minimum for calling the machine "supported"; D5-D7 complete it.

## 3. Non-goals (v1)

| Non-goal | Why |
|---|---|
| Emulating the Altera PLD bitstream | the bitstream format is closed (MAN §1.4); the standard configuration is modeled directly, like MAME does |
| The "Game", "DooM", "Video" PLD configurations (Sp97 legacy) | **v1: Standard only; the other configurations come later as modules; the extension point (`SprinterPldConfiguration`) is in scope** (FR-9). Few programs need them, and MAME itself calls the Game renderer "not fully discovered" (`sprinter.cpp:44`), so each is analyzed against MAME first (a follow-up task after v1) |
| RAM above 4 MB | page registers are 8 bits (256 pages = 4 MB); the unreal-ng memory ceiling is also 256 pages (`core/src/emulator/platform.h:250`) |
| ISA cards | no card is chosen and MAME does not implement ISA memory; v1 keeps the register model only (reads `#FF`) |
| Printer, PC-link cable | no known need |
| Sp2016 / Sp2022 boards | different boards; the design keeps the Sp2000 names so a later variant can subclass |
| Writing the flash ROM (BIOS update) | risky and rare; ROM writes are refused and logged (a later option) |
| Cycle-exact turbo memory waits | the wait model is MAME's approximation until a hardware measurement exists |

## 4. Functional requirements

### 4.1 Machine and firmware

| ID | Requirement |
|---|---|
| FR-1 | A model `SPRINTER` (full name "Sprinter 2000") exists, is creatable, and is listed by `GET /api/v1/emulator/models` and every surface that lists models |
| FR-2 | The machine runs a 256 KB BIOS image (default 3.07 BETA 1 since 2026-10-02, owner decision; 3.04, 3.06 and others selectable) with no patches; tests run on 3.04, 3.06 and 3.07 |
| FR-3 | Power-on runs the ROM's PLD loader (the user default); the configuration becomes active when the loader has streamed the whole bitstream; an optional **fast start** skips the loader (same resulting state; the default for tests) |
| FR-4 | The port decoder is **driven by page `#40`** exactly as the hardware: every external port access looks up the table; programs that edit the table see the change at the next access |
| FR-5 | Memory: 256 pages, window registers, the Spectrum-compatible window 3, vROM, system ROM, fast RAM, graphics pages, the reset page and the ISA pages behave as in hardware-reference §3 |
| FR-6 | CPU clock 3.5 MHz and 21 MHz switchable by the CNF/SYS port at run time; turbo applies memory wait states; fast RAM runs without waits |
| FR-7 | Frame of 320 or 312 lines × 224 T (at 3.5 MHz), switchable by software; INT position taken from the mode table in video RAM |
| FR-8 | Z84C15 on-chip devices: SIO channels A (keyboard) and B (mouse) with receive FIFOs and status, the CTC as far as the BIOS and DSS use it, the PIO as a register file, and the system/watchdog registers as storage |
| FR-9 | PLD configurations are modules behind one extension point (`SprinterPldConfiguration`), looked up by the bitstream hash after loading; an unknown bitstream keeps the Standard module and logs its hash; v1 ships the Standard module |

### 4.2 Video

| ID | Requirement |
|---|---|
| FR-10 | Video RAM as a write-only shadow with graphics and Spectrum addressing, transparency and video-only sub-modes |
| FR-11 | Per-square modes: Spectrum/text 320 and 640, graphics 256-color 320 and 16-color 640; 8 palettes of 24-bit colors; two mode pages (RGMOD); HOLD offsets; border and blank squares |
| FR-12 | The screenshot and recording paths, the beam widget and `GET /state/screen/mode` report the Sprinter modes |

### 4.3 Storage

| ID | Requirement |
|---|---|
| FR-20 | Four floppy drives `fdd.a-d` on the WD1793 with density selection (720 KB / 1.44 MB) and DD/HD media: TRD/SCL/FDI/UDI and raw PC images of 737 280 and 1 474 560 bytes |
| FR-21 | A density mismatch (HD medium read at DD rate or the reverse) fails the way hardware does (no ID found), so the BIOS density detection works |
| FR-22 | Two IDE channels, four units `ide0.master`, `ide0.slave`, `ide1.master`, `ide1.slave`; hard disks from raw images (and the shared IDE formats); an ATAPI CD on a unit configured `cdrom` |
| FR-23 | A host folder can be the hard disk: a FAT16 volume (default) that DSS boots from, with the DSS boot loader and `SYSTEM.*` supplied as described in `tdd-storage.md` §5 |
| FR-24 | CMOS: DS12887-compatible RTC + 128 bytes of NVRAM, persisted in a file; host time or a fixed time for tests |
| FR-25 | All slots are registered with the media manager (PLAN #58): one `media` vocabulary, TTD rules and model-switch behavior |

### 4.4 Sound and input

| ID | Requirement |
|---|---|
| FR-30 | AY at 1.75 MHz with ABC stereo; beeper; Covox; Covox-Blaster with its buffer, rates, stereo/16-bit modes and its interrupt |
| FR-31 | Host keyboard → both the ZX matrix (code `#40`) and AT set-2 scan codes into SIO channel A, from one key event (the design shared with ZX-Evo PS/2, PLAN #55 E2b) |
| FR-32 | Host mouse → MS serial packets on SIO channel B and the Kempston mouse registers |
| FR-33 | Kempston joystick |

### 4.5 Accelerator

| ID | Requirement |
|---|---|
| FR-40 | Accelerator opcodes and block operations (fill, copy, vertical variants, AND/OR/XOR) with the CPU held for the block time |

### 4.6 Tooling

| ID | Requirement |
|---|---|
| FR-50 | TTD: every piece of Sprinter state (PLD cells, port-table page is ordinary RAM, video RAM, accelerator, CBL, SIO/CTC, IDE latch, CMOS) is captured and restored; replay is deterministic |
| FR-51 | Snapshots: a Sprinter state file (no Spectrum snapshot format can hold it); loading a `.sna`/`.z80` is refused with a clear error **outside the ZX mode**; inside it, it is applied to the Spectrum pages through the cell table as an emulator convenience (S8 Z5, [tdd-zx-mode.md](tdd-zx-mode.md) §3.5; not built yet: today nothing refuses) |
| FR-52 | Debugger: physical-page memory views; a port-table view (address → code, per map, DOS, R/W); a video mode-table view; breakpoints on internal codes |
| FR-53 | Automation parity: WebAPI, CLI, MCP, Lua, Python can create the model, insert media, read the Sprinter state (`sprinter` state block: pages, cells, map, clock, video) |

## 5. Non-functional requirements

| ID | Requirement |
|---|---|
| NFR-1 | No behavior change for any other model; the shared hooks cost at most 1 % on existing benchmarks (the gate the TSConf design already defines) |
| NFR-2 | Real time at 21 MHz on the reference Mac (M-series) with sound and video, host CPU below one core |
| NFR-3 | Deterministic: the same inputs give the same frames and TTD hashes; no host time in the core except the CMOS clock in "host time" mode |
| NFR-4 | Tests: under 50 ms each (boot-to-BIOS tests justified in a comment, `core/tests/README.md`); ROM-dependent tests skip cleanly when the ROM is absent |
| NFR-5 | Zero warnings on gcc, clang, MSVC, mingw |
| NFR-6 | All Sprinter-only code lives in Sprinter folders (`ports/models/portdecoder_sprinter.*`, `memory/sprinter/`, `video/sprinter/`, `io/z84c15/`, `debugger/ttd/sprinter/`); shared code only gets generic hooks |

## 6. Acceptance criteria (real firmware and software)

| ID | Scenario | Pass condition |
|---|---|---|
| ACC-1 | Cold start with BIOS 3.04, no media | within 10 emulated seconds the logo, then the "no boot device" menu (DEL/ESC/ENTER) appears; the port table in page `#40` equals the reference table decoded from BIOS-TT (`DCP.ASM`) for the entries that BIOS 3.04 shares |
| ACC-2 | BIOS setup (DEL): change the date, save, restart | the new date shows; the CMOS file holds it; checksum `#3F` valid. **As built (S2):** SETUP 1.58 of BIOS 3.04 has no date page, so the test changes "Memory Test" (CMOS `#0E`), saves with F10 and checks the restart, `#3F` and the file (roadmap §7) |
| ACC-3 | `fdd.a` = `dss_1_62_92.img` (1.44 MB), cold start | "Starting DOS..." then the DSS prompt; `DIR` output matches the image's root directory; the density port was set to 1.44 MB. **S3a (2026-10-01):** met up to the prompt with the image in `fdd.b` (a blank CMOS boots the IDE master, then floppy B: SETUP default CMOS `#10` = `#12`); `DIR` needs DSS keyboard input (S4) |
| ACC-4 | `ide0.master` = a built FAT16 image (MBR entry 0 type `#06`, DSS loader at LBA 1-3, `SYSTEM.DOS/EXE/BAT` from DSS 1.62), boot drive = IDE | DSS prompt on drive C:; a file written by the guest is in the image after Save |
| ACC-5 | `ide0.master` = a host folder with the same files | same as ACC-4; the folder is unchanged (session writes) until commit |
| ACC-6 | ESC at the boot menu → Spectrum mode; `RANDOMIZE USR 15616`; `LOAD "…"` from a `.trd` in drive A | TR-DOS catalog lists the files; the program runs. **S3a (2026-10-01):** BIOS 3.04 has no Spectrum ROMs (ESC: "Spectrum ROM not installed. Use spectrum.exe"); met through DSS `A:\ZX\SPECTRUM.EXE PENT128.ZX` → 128 menu → TR-DOS 7.01: `LIST` shows the catalog, `LOAD "…" CODE` loads byte-exact ([roadmap-and-plan.md](roadmap-and-plan.md) §8) |
| ACC-7 | DSS: run a 256-color graphics demo (SPRINTEM `disk/FLAMES.EXE` or `256COLOR.EXE`) | screenshot matches a reference captured from MAME (palette order settled by hardware-reference §4.5) |
| ACC-8 | DSS: run Flex Navigator (`FN.EXE`) | 80×32 text UI renders; keyboard navigation works |
| ACC-9 | an accelerator + CBL program (pick from app.sprinter.ru during S5) | frame-time and audio match MAME within tolerance |
| ACC-10 | TTD: record 30 s of ACC-3, seek back and forward | identical frame hashes on replay; no divergence |
| ACC-11 | Automation smoke: create `SPRINTER`, insert `fdd.a`, run, read `sprinter` state and a screenshot on each surface | all surfaces agree |
