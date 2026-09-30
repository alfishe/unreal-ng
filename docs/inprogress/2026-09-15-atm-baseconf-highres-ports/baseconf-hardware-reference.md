# ZX Evolution (PentEvo) BaseConf — hardware ground truth

> **Role in this folder:** hardware evidence for the 2026-09-27 BaseConf gap analysis ([gap-analysis.md](gap-analysis.md)). Research notes, read-only survey of the FPGA (current `base_trdemu` and legacy `baseconf` trees), AVR firmware, ROM/ERS sources and NedoOS. `emulators/github/` is the reference-source directory next to the project.

Research date: 2026-09-27. Read-only survey of:

- `P` = `emulators/github/pentevo` (NedoPC SVN mirror, HEAD c24723db 2026-06-19)
- `emulators/github/zxevo.pentevo` — **byte-identical** to `P` for `fpga/`, `rom/`, `avr/` (checked with `diff -rq --strip-trailing-cr`); cite `P` only.
- `emulators/github/zx-evo/pentevo` — TS-Conf tree (`fpga/current` = TS-Conf, `fpga/base` = an older BaseConf snapshot). Not used except where noted.
- `D` = `emulators/github/zx-evo-docs`
- NedoOS = `emulators/github/NedoOS`

Line numbers are 1-based as in the files.

---

## 0. WHICH BaseConf? (read this first)

There are **two** BaseConf FPGA trees in `P/fpga`, and they differ in port semantics:

| Tree | Status | Evidence |
|---|---|---|
| `P/fpga/baseconf/trunk` | **Frozen legacy** BaseConf (~2014 RTL; last touched 2021 only to delete binaries). Matches the PDF manual "версия от 27.10.2014" (`D/Baseconf/zxevo_base_configuration.pdf`). | `git log -- fpga/baseconf` → d002a853 2021-02-28 "removing .rbf/.sof" |
| `P/fpga/base_trdemu/trunk` | **Current, released BaseConf** ("trdemu" = TR-DOS / VG93 software-emulation hooks). All post-2020 development (444 palette, 8-bit Kempston, ULA+ fixes, SD-contention fix, "removed completely xxBE read ports"). Ships as `P/cfgs/standalone_base_trdemu/trunk/zxevo_fw.bin`. Documented by `P/fpga/base_trdemu/trunk/doc/zxevo_trdemu.odt` ("версия от 27.02.2020"). | `P/cfgs/standalone_base_trdemu/trunk/README:1-4` ("pentevo/avr/baseconf/trunk as 'avr', pentevo/fpga/base_trdemu/trunk as 'fpga'"); `git log -- fpga/base_trdemu` (commits 2021-2026, e.g. 663b8cf2 2023-06-17 "baseconf: removed completely xxBE read ports from RTL", 6bab9e30 2024-02-10, 7f535dfb "baseconf: compiled") |
| `P/fpga/base_trdemu/newirq` | WIP branch (2026-01) adding enhanced IRQ controller at `#14BD..#16BD`; does not even compile (`rd_irq_stat = (1'b0, ...}` typo). **Ignore.** | `diff trunk/z80/zports.v newirq/z80/zports.v` |

Both configs use the same AVR firmware tree `P/avr/baseconf/trunk`.

Released firmware headers (first bytes of `zxevo_fw.bin`, also the 16-byte version block the Z80 reads — see §B):

- `P/cfgs/standalone_base_trdemu/trunk/zxevo_fw.bin` (50816 bytes): `"ZXEVO\x1A" "ZXEvo 4M"`; version block at 0xC670: `5A 58 45 76 6F 20 34 4D 00 00 00 00 | 27 B4 | 41 47` → name "ZXEvo 4M", date word 0xB427 = **release, 2026-01-07**, CRC 0x4741.
- `P/cfgs/standalone_baseconf/trunk/zxevo_fw.bin` (49792 bytes): block at 0xC270: `4E 6F 20 69 6E 66 6F 00 00 00 00 00 | 5C AA | C2 6F` → name "No info", date word 0xAA5C = release, 2021-02-28, CRC 0x6FC2.

The emulation target for "ZX Evo BaseConf" in 2026 should be **base_trdemu/trunk**. Differences vs the legacy tree are tabulated in §A.14.

Citation shorthand below: `T/` = `P/fpga/base_trdemu/trunk/`, `L/` = `P/fpga/baseconf/trunk/` (legacy).

---

## A. FPGA BaseConf RTL

### A.1 Global facts

- FPGA: Altera ACEX EP1K50QC208; fclk = 28 MHz; Z80 clocked at 3.5/7/14 MHz by `T/z80/zclock.v`.
- RAM: 4 MB DRAM, 256 × 16K pages (page register 8 bits; DRAM word address 21 bits × 16 bit = 4 MB) — `T/top.v:289` (`cpu_addr [20:0]`), doc `zxevo_trdemu.odt` §4 "4 мегабайта ОЗУ и 512 килобайт ПЗУ".
- ROM: 512 KB flash (Am29F040/M29F040), 32 × 16K pages; ROM page = `page[4:0]` (`T/z80/zmem.v:187`); pins `rompg0_n = ~rompg[0]`, `dos_n = rompg[1]`, `rompg2..4 = rompg[2..4]` (`T/top.v:364-368`, pins in `T/quartus/top.qsf:140-144`). Commented-out "adr_fix" block (`T/top.v:352-362`) notes the ATM2-style ROM address wiring ("need to split and re-build old ATM romfiles") — i.e. the flash's A14/A15 are driven by the inverted/DOS-named signals; the ROM image layout in the file must be built accordingly (see §C).
- Port decoding is **always at least on A7..A0**; mainboard ports are never visible to ZX-Bus cards (porthit blocks IORQ to the bus) — `T/z80/zbus.v:46-50`, doc §4.
- Unclaimed port reads and the IM2 vector read return **#FF** (`drive_ff = (~(iorq2_n|iorqge2) & ~rd_n) | ~(m1_n|iorq_n)`, `T/z80/zbus.v:50`).
- "shadow" = `dos || shadow_en_reg` (`T/z80/zports.v:320`), where `dos` is the TR-DOS (DOSEN) signal (`T/z80/zdos.v`) and `shadow_en_reg` is `#BF` bit 0.

### A.2 Complete I/O port decode (base_trdemu/trunk, `T/z80/zports.v`)

`porthit` list: `T/z80/zports.v:331-359`; read mux: `:424-482`. "noshad" = only when shadow=0, "shadow" = only when shadow=1, "always" = both.

| Port (low byte / full) | Dir | Visibility | Function | RTL evidence |
|---|---|---|---|---|
| `#xxFE` | R/W | always | R: `{1, tape_in, 0, keys[4:0]}` (keyboard half-rows by A15..A8, AND-combined, `T/z80/zkbdmus.v:92-115`). W: D2..0 border (color 0..7, border bit3 = ~A3 = 0), D3 tape-out, D4 beeper. | `:427-428`, `:533-537`, `beeper_wr = loa==FE` `:944` |
| `#xxF6` | R/W | always | R: same as #FE. W: border only, colors 8..15 (border[3] = ~A3 = 1). **No beeper/tape write** (beeper_wr is only for `loa==FE`). | `:429-430`, `:533-537`, `:944` |
| `#xxFC` | W | always | Quirk: counted as porthit; a write sets border (bit3=0) **and**, if A15=0, also writes #7FFD (`portfd_wr` includes `loa==FC`). Read returns #FF. | `:334`, `:486`, `:533` |
| `#7FFD` (A15=0, low byte #FD, also #FC) | W | always | Memory: see A.4. Blocked when `7FFD.5 & EFF7.2` (48K lock only in 128K mode). | `:486`, `:694-710` |
| `#FFFD` (A15:14=11, low #FD) | R/W | always | AY/YM register select (W) / data read (R). External chip (real YM2149 on board). | `:663-684` |
| `#BFFD` (A15:14=10, low #FD) | W | always | AY/YM data write. | same |
| `#xxFB` | W | always | Covox 8-bit PWM DAC on the beeper output (until next #FE write that toggles the beeper bit). | `:945`, `T/sound/sound.v:55-78` |
| `#xxF7` A8=1, A12=0 (`#EFF7`) | W | noshad | EFF7 register (A.4). Not writable in shadow ("EEF7 in shadow mode is abandoned"). | `:490-491`, `:714-720` |
| `#xxF7` A13=0 (`#DFF7` noshad / `#DEF7` shadow) | W | gluclock_on | Gluk/CMOS address register → `gluclock_addr` (sent to AVR). | `:741-750` |
| `#xxF7` A14=0 (`#BFF7` noshad / `#BEF7` shadow) | R/W | gluclock_on | Gluk/CMOS data — a **WAIT port**: Z80 is held in WAIT until the AVR services it over SPI (A.10). Read returns `wait_read`. | `:455-460`, `:764-778` |
| `#xFF7` (A11:10=11, A8=1) | W | shadow | ATM pager: page/ramnrom/dos7ffd for window A15:14. | `:853`, `T/mem/atm_pager.v:195-204` |
| `#x7F7` (A11:10=01, A8=1) | W | shadow | ATM pager: 8-bit inverted RAM page (0..255), forces RAM. | `T/mem/atm_pager.v:206-210` |
| `#xBF7` (A11:10=10, A8=1) | W | shadow | Per-window read-only bit (D0). | `T/mem/atm_pager.v:185-187` |
| `#xx77` | W | shadow | ATM system port: D2..0 video mode, D3 turbo; A8=0 pager off; A9=0 force DOS/shadow ("CP/M"); A14=0 palette-write enable. | `:854`, `:885-905` |
| `#xx77` | R/W | noshad | Z-Controller SD config. W: D1 = SD /CS. R: always `#00` (card present, R/W). | `:449-450`, `:812-822` |
| `#xx57` | R/W | always | SD SPI data. W (noshad, or shadow with A15=0): send byte. W shadow with A15=1: SD /CS (replacement for #77). R: return last received byte (latched `sd_rd_buffer`) and start a new #FF exchange. | `:451-452`, `:812-836` |
| `#xx1F` | R | noshad | Kempston joystick, **8-bit** (`kj_in[7:0]` from AVR SPI reg $23; standard bits `000FUDLR`, upper bits used by AVR for extra pad buttons). | `:444-445`, `T/z80/zkbdmus.v:85-86` |
| `#xxDF` | R | always | Kempston mouse: A8=0 → buttons+wheel (`#FADF`), A8=1&A10=0 → X (`#FBDF`), A8=1&A10=1 → Y (`#FFDF`). #FF if no mouse (AVR). | `:446-447`, `T/z80/zkbdmus.v:118-120` |
| `#xx1F/#3F/#5F/#7F` | R/W | shadow | WD1793 (КР1818ВГ93) command/status, track, sector, data — **external chip**; `/CS` additionally gated by FDD emulation mask (A.8). | `:342`, `:797-799` |
| `#xxFF` | R/W | shadow | Beta system reg. W: D1..0 drive, D2 /RESET, D3 HLT/HRDY, D4 side (stored inverted), also palette write when `#77`.A14=0. R: `{INTRQ, DRQ, 1, side(D4 as written), HRDY, RES_n, drive[1:0]}`. | `:441-442`, `:911-917`, `T/vg93/vg93.v:172-177` |
| `#xxBF` | R/W | always | Evo config: D0 shadow_en, D1 ROM write enable, D2 font-RAM write enable, D3 NMI request (1→0 edge), D4 breakpoint enable, D5 ATM3 4:4:4 palette enable. Read returns `{00, D5..D0}`. | `:466-468`, `:856-881` |
| `#xxBE` | W | always | "NMI end / trdemu end" strobe (`clr_nmi`). Value ignored. **Not readable in trdemu** (legacy: readback port). | `:937` |
| `#xxBD` | R (+W for some) | always | Readback/config port indexed by **A12..A8** (A.5). W: `#10BD/#11BD` breakpoint address, `#13BD` FDD emulation mask. | `:470-472`, `:504-525`, `:956-994` |
| `#F8EF..#FFEF` (low byte #EF) | R/W | always | 16550-style RS-232 (A10..A8 = register 0..7), WAIT port serviced by AVR. | `:462-464`, `:500-501`, `:755-759` |
| `#xx3B` | W (R) | always | ULA+: A14=0 (`#BF3B`) register select, A14=1 (`#FF3B`) data. Read returns last data written. | `:474-476`, `:1003-1034` |
| NemoIDE `#10,#11,#30,#50,#70,#90,#B0,#D0,#F0,#C8` | R/W | always | IDE (A.9). | `:169-173`, `:544-654` |

Aliases worth knowing: the NIDE_REGS macro (`:169-173`) is `(x[2:0]==000) && (x[3]!=x[4])`, so it also claims `#08,#28,#48,#68,#88,#A8,#E8` — these go to CS0 with `ide_a=A7..5` (i.e. aliases of #10/#30/…/#F0 register numbers 0..7); only `#C8` drives CS1.

Ports **not** present in BaseConf: #1FFD (no +3 paging), #xxFD with A15=0 other than 7FFD semantics, TurboSound (#FFFD D7..), SAA1099, General Sound (external NeoGS card on ZX-Bus only), DMA (none in BaseConf; DMA exists only in TS-Conf), #xxAF/#xx57-style TS ports.

### A.3 Shadow/DOS mechanism (`T/z80/zdos.v`, `T/mem/atm_pager.v`)

- `dos` resets to 1 (`T/z80/zdos.v:65-78`); forced to 1 while `atm_cpm_n=0` (`#77` A9=0; reset value 0 → DOS forced at reset).
- **DOS turn-on**: M1 opcode fetch in a window whose address A13..A8 = `#3D`, when `7FFD.4=1` (map 1 active), map 1's window is ROM and has `dos7ffd=1` (`T/mem/atm_pager.v:246-249`). Z80 clock stalled ~3 fclk while the ROM output settles (`:262-282`).
- **DOS turn-off**: any M1 fetch from a RAM window (`ram_exec_stb`, `:251-256`), unless `cpm_n=0`.
- For a ROM window with `dos7ffd=1`, the low bit of the ROM page is replaced by `dos` (`:158-161`) → ROM pair {even=BASIC48 / odd=TR-DOS} (see §C for actual page numbers).

### A.4 Memory map & page registers

Two maps (sets of 4 window registers); **`7FFD` bit 4 selects the active map** (`pent1m_ROM`), `T/mem/atm_pager.v:86-91,141-166`. Every write to `#xFF7/#x7F7/#xBF7` modifies only the **active** map.

`#xFF7` (window = A15:14: `#3FF7`,`#7FF7`,`#BFF7`,`#FFF7`), `T/mem/atm_pager.v:200-204`:
- D5..0: **inverted** page number (RAM 0..63 / ROM 0..31); stored as `~{11, D5..0}` → page = `{00, ~D[5:0]}`.
- D6: ramnrom (1 = RAM).
- D7: dos7ffd — RAM: low 3 bits (128K mode) or 6 bits (Pentagon-1024 mode) of page replaced by 7FFD bits; ROM: low bit replaced by DOS and `#3Dxx` DOS entry enabled.

`#x7F7` (`#37F7`,`#77F7`,`#B7F7`,`#F7F7`): D7..0 inverted RAM page 0..255; forces ramnrom=1; dos7ffd unchanged (`:206-210`). In RAM+dos7ffd with 1M mode: page = `{pages[7:6], 7ffd_page6}`; 128K mode: `{pages[7:3], 7FFD[2:0]}` (`:147-156`).

`#xBF7` (`#3BF7`...`#FBF7`): D0 = write-protect for that window of the active map (also protects ROM from flash writes) (`:185-187`). Not applied to page0-RAM-via-EFF7 or NMI/trdemu pages (`:124-127`, doc).

`#7FFD` (`T/z80/zports.v:694-731`): 128K mode (EFF7.2=1): D2..0 page, D3 screen, D4 map select (≡ ROM select), D5 lock. Pentagon-1024 mode (EFF7.2=0, reset default): page6 = `{D7..5, D2..0}`, no lock. In 128K mode bits 7..5 are masked off from the exported p7ffd (`:723`).

`#EFF7` (`:714-731`; doc): D0 = ZX 16-color mode (Alone's 256x192x16), D2 = 1:128K mode / 0:Pentagon-1024, D3 = RAM page 0 forced in #0000 (overrides pager), D4 = turbo off (see A.11), D5 = ZX hardware multicolor, D7 = Gluk clock ports enable (noshad). Reset 0.

**No #1FFD.**

Priority for window 0 (`T/mem/atm_pager.v:114-168`): `pager_off` (`#77` A8=0 → every window ROM page #FF i.e. ROM page 31) > `in_nmi` (RAM #FF) / `in_trdemu` (RAM #FE) > `EFF7.3` (RAM 0) > normal pager.

Reset state: `atm_pen=1` → pager off → all four windows = ROM page 31 (0x1F = last 16K of flash) (`T/z80/zports.v:886-897`, `atm_pager.v:116-121`); `atm_cpm_n=0` → DOS forced; `atm_scr_mode=011` (ZX); turbo bit 0; wrdisables 0. Pager registers themselves (`pages`, `ramnrom`, `dos7ffd`) are **not reset** — the ROM boot code (ERS) programs them.

Reset recommended mapping (doc §5): ERS programs both maps like a 128K (ROM 128 in map 0 / BASIC48 in map 1 with dos7ffd, window 3 = RAM with dos7ffd).

ROM write: `romwe_n = wr_n | mreq_n | ~romrw_en | wrdisable` (`T/z80/zmem.v:193`) — any Z80 memory write to a ROM window while `#BF.1=1` and window not write-protected goes to the flash chip (software does the AMD command sequence).

### A.5 `#xxBD` readback / config (trdemu) — index = A12..A8

`T/z80/zports.v:220-248` (names), `:956-994` (mux), doc `zxevo_trdemu.odt` §8.3.

| Port | R/W | Content |
|---|---|---|
| `#00BD..#03BD` | R | map 0 window 0..3 page, **inverted** (top.v passes `~rd_pages`, `T/top.v:878-881` region `.pages(~{...})`) — i.e. value as would be written to #x7F7 |
| `#04BD..#07BD` | R | map 1 window 0..3 page, inverted |
| `#08BD` | R | ramnrom bits: bit i = window i map0 (i=0..3), bits 4..7 = map1 |
| `#09BD` | R | dos7ffd bits, same order |
| `#0ABD` | R | last #7FFD value (raw `p7ffd_int`) |
| `#0BBD` | R | last #EFF7 value |
| `#0CBD` | R | `{~pen2 (=A14 of #77), cpm_n (=A9), ~pen (=A8), DOS, turbo(D3), scr_mode(D2..0)}` |
| `#0DBD` | R | current displayed palette color, format `{g,r,b,G,1,1,R,B}` inverted (same as #FF palette write) |
| `#0EBD` | R | current text-mode font byte (fontrom readback) |
| `#0FBD` | R | D3..0 last border color (0..7 via #FE, 8..15 via #F6) |
| `#10BD` / `#11BD` | R/W | breakpoint address low / high (write decode `a[12:9]==8`, byte by A8) |
| `#12BD` | R | write-protect bits (`#xBF7`), same order as #08BD |
| `#13BD` | R/W | D3..0 **FDD emulation mask** (bit n=1 → drive n is software-emulated) — reset 0 |
| other | R | undefined (X) |

Legacy (`L/`): same table on **`#xxBE`** (read, indices 0x00..0x12, `L/z80/zports.v:893-929`), write `#00BD`/`#01BD` (by A8) = breakpoint address (`L/z80/zports.v:473-487`), no FDD mask.

Pager restore algorithm (doc): write #xFF7 with dos7ffd/ramnrom and page (<64 or ROM), then #x7F7 for RAM pages ≥64.

### A.6 `#xxBF` bits (`T/z80/zports.v:856-881`, `:466-468`)

| Bit | Name | Meaning (reset 0) |
|---|---|---|
| 0 | shadow_en | force shadow ports on (does not page TR-DOS ROM) |
| 1 | romrw_en | flash ROM write enable |
| 2 | fntw_en | every Z80 memory write also writes font RAM at A&2047 (`fnt_wr = fntw_en & mem_wr`, `:950`) |
| 3 | set_nmi | NMI request: a **1→0** transition arms NMI; delivered at next INT start |
| 4 | brk_ena | enable M1 breakpoint at `#10BD/#11BD` address |
| 5 | pal444_ena | (trdemu only) ATM3 4:4:4 palette: `#FF` palette writes take low color bits from A15..A8 (`atm_paldatalow`, `:917`) |
| 7..6 | — | read as 0 |

Readback: `{2'b00, pal444, brk_ena, set_nmi, fntw, romrw, shadow_en}` (legacy: `{3'b000, ...}` without bit5, `L/z80/zports.v:436`).

### A.7 NMI, breakpoint, INT (`T/z80/znmi.v`, `T/z80/zbreak.v`, `T/z80/zint.v`)

- NMI sources: `set_nmi[0]` from AVR (SPI config0 bit1: PrintScreen key / NMI pins), `set_nmi[1]` from `#BF.3`. A falling edge sets `pending_nmi` (`znmi.v:123-126`); NMI is actually asserted **at the next `int_start`** (`nmi_start = pending_nmi && int_start || imm_nmi_now`, `znmi.v:148`). /NMI low for 4 Z80 clocks (`nmi_count`, `znmi.v:194-203`).
- Breakpoint: `imm_nmi` when M1 && A==brk_addr && brk_ena (`zbreak.v:50-56`) → NMI immediately (not INT-synchronized). Breakpoint remains enabled.
- Entry: after NMI, the FPGA **drives #00 (NOP)** on the data bus for the M1 fetch at `#0066` (`drive_00`, `znmi.v:177`; data mux `T/top.v:441-446`, ROM CS suppressed); at that M1's refresh `in_nmi` becomes 1 (`znmi.v:181-191`) and window 0 becomes **RAM page #FF** (`atm_pager.v:124-132`). Handler's first real instruction is at `#0067` of RAM page #FF.
- Exit: `OUT (#BE),A` → `clr_count=3`, decremented at each refresh; `in_nmi` cleared after **2 more M1 cycles** (i.e. at the end of the following `RETN`) (`znmi.v:152-163,186-187`).
- `#0CBD` bit 4 (DOS) lets the handler know whether NMI interrupted TR-DOS.
- INT: `int_start` from `T/video/video_sync_v.v:190-197` and `video_sync_h.v:234-237`; /INT held low for **256 fclk (≈9.14 µs ≈ 32 T @3.5 MHz)** or until INTACK (`zint.v:57-78`; counter frozen during WAIT). IM2 vector = #FF (`zbus.v:50`).

INT position / raster by `modes_raster` (set by AVR, config0 bits 5:4):

| modes_raster | Raster | Lines | T/line | Frame (T@3.5) | INT line | INT hpos (7 MHz px) | Contention |
|---|---|---|---|---|---|---|---|
| 00 | Pentagon | 320 | 224 | 71680 | 0 | 2 | none |
| 01 | "60 Hz" | 262 | 224 | — | 0 | 2 | none |
| 10 | 48K | 312 | 224 | 69888 | 1 | 126 | 48K pattern, only at 3.5 MHz |
| 11 | 128K | 311 | 228 | 70908 | 1 | 130 | 128K (pages odd in #C000) |

Evidence: `T/video/video_sync_v.v` localparams (VPERIOD_*, INT_BEG_*), `T/video/video_sync_h.v:109-115`, `T/video/video_top.v:65-71`, `T/z80/zclock.v:276-282`.

### A.8 Virtual TR-DOS / "trdemu" (VG93 software emulation) — hardware half

This is the mechanism the ERS / Evo-DOS uses to emulate drives from images (SD/HDD/RAM) **while running the original, unpatched TR-DOS ROM**. Sources: `T/z80/zdos.v:36-99`, `T/z80/zports.v:82-89,797-799,921-930,937`, `T/mem/atm_pager.v:54-59,124-132`, `T/top.v:544-569`, doc `zxevo_trdemu.odt` §"Программный эмулятор ВГ93".

1. **Arm**: software writes a drive mask to **`#13BD`** (D3..0; bit n = drive n emulated). Reset 0 (all real).
2. **Bus effect**: for an emulated drive, `vg_cs_n` is forced high → the real WD1793 is **not selected** (`vg_matched_n = fdd_mask[vg_a]`, `zports.v:797-799`). `vg_a` is the drive number latched from the last `#FF` write (for an `OUT (#FF)` the check uses the **newly written** value because `vg_rdwr_fclk` is registered after the write).
3. **Trigger** (`zdos.v:61`): `trdemu_on = vg_rdwr_fclk && fdd_mask[drive] && dos && romnram[window0] && !atm_pen2`, where `vg_rdwr_fclk` pulses on **any read or write of #1F/#3F/#5F/#7F/#FF** while shadow (`zports.v:921-930`). I.e. only when real TR-DOS (DOS signal, ROM in #0000) executes the I/O, and not during palette-write mode.
4. **Page switch**: `in_trdemu` ← 1 (`zdos.v:82-88`). Window #0000-#3FFF becomes **RAM page #FE** (`page <= {7'h7F, in_nmi}` → #FE, or #FF if also in NMI; `atm_pager.v:124-132`). The switch is effective for the very next opcode fetch: the Z80 continues at **PC = address after the IN/OUT instruction, but now reads from RAM #FE**. Page #FE therefore holds, at each TR-DOS I/O-instruction return address, the emulation stub for that access (for the specific TR-DOS ROM version).
5. **Write protection glitch guard**: `trdemu_wr_disable` is set on trigger and cleared at the next M1 (`zdos.v:91-98`), making page #FE read-only for the remainder of the trapping instruction (protects #FE against the memory write of INI/IND/INIR etc.) (`atm_pager.v:126`).
6. **What the Z80 sees on the trapped access**: for #1F/#3F/#5F/#7F with emulated drive, nothing drives the bus (external_port=1 → FPGA does not drive; VG93 /CS inactive) — the stub supplies the value itself. For `#FF` reads the FPGA always answers `{INTRQ,DRQ,1,side,HRDY,RES_n,drive}` (state of real chip + latched bits), which lets the emulator recover what TR-DOS wrote.
7. **Return**: stub executes `OUT (#BE),A` placed so that the next fetch is at the TR-DOS continuation address; `in_trdemu` is cleared **immediately** by `clr_nmi` (not after 2 M1 like NMI), unless in NMI (`zdos.v:82-86`; 2022 fix 48413f80 "exit from nmi while into trdemu not to also exit from trdemu mode").

No NMI is involved in trdemu; it is a transparent page swap. The legacy tree has no trdemu; instead it had four spare shadow latch ports **`#2F/#4F/#6F/#8F`** (RW shadow, plain registers "used for VG93 emulation with the RAM disk", `L/z80/zports.v:186-189,409-410,937-945`; PDF p.38) used by a patched-TR-DOS RAM-disk scheme.

### A.9 NemoIDE (`T/z80/zports.v:544-654`, doc §9.8)

- Ports (8-bit decode): `#10` data low (and high in "divide" mode), `#11` data high latch, `#30` err/feat, `#50` count, `#70` sector/LBA0, `#90` cyl-lo/LBA1, `#B0` cyl-hi/LBA2, `#D0` head/LBA3+dev, `#F0` status/cmd, `#C8` alt-status/devctl (CS1). ATA A2..A0 = Z80 A7..A5; CS0 for all except #C8.
- Nemo read: `IN #10` gives low byte and latches high byte into `#11` (`:601-603`); `IN #11` reads the latch (no IDE cycle).
- Nemo write: `OUT #11` latches high, `OUT #10` performs the 16-bit write (`:589-596`, `:653-654`).
- Extended ("nemo-divide") mode, no enable needed: `INIR`/`OTIR` on `#10` alone — read: 1st `#10` = low (real IDE read), 2nd `#10` = latched high; write: 1st `#10` low latched, 2nd `#10` performs write (`:554-580`, `:625-634`). Phase re-synchronized by any access to other IDE ports.
- IDE ports are visible in shadow too. IDE /RESET = system reset (`T/top.v:267`).

### A.10 WAIT ports → AVR (Gluk CMOS, RS-232) and SPI slave (`T/z80/zwait.v`, `T/slave/slavespi.v`, `T/slave/spi_fmt.txt`)

- `#BFF7`/`#BEF7` access (with gluclock enabled) and any `#xxEF` access assert Z80 /WAIT and raise `spiint_n` to the AVR (`zwait.v:57-79`). The AVR reads SPI status (bit7 rnw, bit0 gluk, bit1 COM), reads `$41` (gluk address = last `#DFF7` value) or `$42` (COM reg A10..8), reads/writes `$40` (data), then un-waits on spics_n 0→1 (`spi_fmt.txt`).
- `#DFF7` itself is not a wait port; it just latches the address (`zports.v:741-750`).
- Other SPI regs (AVR→FPGA): `$10/$11` 40-bit keyboard matrix, `$20/$21/$22` mouse X/Y/buttons, `$23` Kempston joystick (8 bit), `$30` Z80 reset, `$50` config0 {D0 VGA, D1 NMI (1→0), D2 tape-in → #FE.6, D3 beeper/tapeout mux, D5:4 raster}, `$60/$61` AVR access to the SD card (lock arbitration with the Z80). Legacy had `$51` config1 (drive mask), unused in top. Full SPI protocol → §B.
- Gluk cell semantics (registers 0x00..0xFF, extensions 0xF0..0xFF, EEPROM, version) are implemented in the AVR → §B.

### A.11 CPU speed, contention

`turbo = {atm_turbo (#77 D3), ~EFF7.4}` (`T/top.v:401`): `00`=3.5 MHz, `01`=7 MHz, `1x`=14 MHz (with waits). **Reset → 7 MHz** (both 0). Switch happens in /RFSH (`zclock.v:134-148`). At 14 MHz external I/O (AY, VG93) gets extra wait (`zclock.v:160-196`). Contention only in 48K/128K rasters **and** at 3.5 MHz (`zclock.v:282`).

### A.12 Sound, video, input summary

- Sound: FPGA outputs a single 1-bit sound (beeper or tape-out, chosen by AVR config0 bit3 "Num Lock"; or Covox PWM) — `T/sound/sound.v:55-88`. One real AY/YM chip (clock `ay_clk = fclk/16 = 1.75 MHz`, `T/top.v:340-346`). No TurboSound, SAA, GS, SounDrive in BaseConf.
- Video modes (`#EFF7` D0/D5 + `#77` D2..0): ZX (011), ZX multicolor (EFF7.5), ZX 16c (EFF7.0), ATM 640x200 MC (010), ATM 320x200 16c (000), ATM text 80x25 (110), single-page text 80x25 (111, page 8/10) — doc §7.1. Palette: ATM, via `#FF` when `#77` A14=0, data `~{g,r,b,G,x,x,R,B}` (`zports.v:916`); 444 extension via `#BF.5` (+A15..8). ULA+ (`#BF3B/#FF3B`). Font RAM 2K via `#BF.2`.
- Keyboard: 40-bit ZX matrix delivered by AVR (PS/2 → ZX mapping and/or real ZX keyboard); `#FE` reading uses A15..A8 half-row selects (`zkbdmus.v:92-115`).
- Mouse/joystick: from AVR (PS/2 mouse, Kempston/Sega pad on DB-9).
- Tape-in: AVR provides `tape_read` via config0 bit2 → `#FE` bit6.

### A.13 SD card (Z-Controller compatible), shared with the AVR

`#77` (noshad) W: D1 = /CS, D0 should be 1; R = #00. `#57` data (INIR/OTIR allowed). In shadow: `#57` A15=1 = /CS, A15=0 = data. Read latches data into `sd_rd_buffer` (2021 contention fix, `zports.v:833-836`). The SPI master (`T/spihub/spihub.v`) arbitrates between Z80 and AVR (`avr_lock_claim/grant`, `T/top.v:298-311`). SD presence/WP status is **not** on #77; it's in Gluk register `0x0C` bits 2/3 (§B).

### A.14 Differences: legacy `baseconf/trunk` vs current `base_trdemu/trunk`

| Feature | legacy `L/` | current `T/` |
|---|---|---|
| Readback port | `#xxBE` read (`L/z80/zports.v:439-441,893-929`) | `#xxBD` read (`T/…:470-472,956-994`); `#BE` is write-only |
| Breakpoint address write | `#00BD` (lo) / `#01BD` (hi) by A8 (`L/:479-487`) | `#10BD` / `#11BD` (`T/:510-518`), also readable |
| FDD emulation mask | — | `#13BD` R/W; gates VG93 /CS; trdemu page #FE |
| Spare shadow ports `#2F/#4F/#6F/#8F` | RW latches (`L/:186-189,409-410,937-945`) | removed |
| `#FF` read low bits | `vgFF[5:0]` last written (`L/:406-407,465-467`) | reconstructed `{1,side,hrdy,res_n,drv}` |
| Kempston `#1F` | 5 bit `{000,FUDLR}` (`L/:413-414`) | 8 bit (commit ba022af4 2022-09-25) |
| `#BF` bit 5 | — | 444 palette enable |
| `#xBF7` write-protect | present (both) | present |
| SD read latch | direct `sd_dataout` | `sd_rd_buffer` |
| zdos | plain DOS flop | + trdemu logic |

### A.15 TS-Conf differences (only for orientation)

TS-Conf (`zx-evo/pentevo/fpga/current`, `D/TSconf/tsconf_en.md`) replaces most of this: `#xxAF` register file, DMA, sprites/tiles, different memory manager (#xxAF pages), and **no** `#xxBD/#BE/#BF` Evo semantics except shared Gluk/SD/IDE/AY ports. Mentioned per request; not analyzed further.

---

## B. AVR firmware (Gluk/CMOS, SPI, FPGA load, bootloader, version reporting)

(Merged from AVR sub-report; headings demoted one level.)

### ZX Evolution (PentEvo) BaseConf: AVR firmware, bootloader, AVR<->FPGA SPI, and the Gluk/CMOS interface

Research notes (read-only). All claims cite `file:line`. Path abbreviations:

| Abbrev | Path |
|---|---|
| `AVR/` | `emulators/github/pentevo/avr/baseconf/trunk/src/` |
| `AVRB/` | `emulators/github/pentevo/avr/baseconf/trunk/build/` |
| `BOOT/` | `emulators/github/pentevo/avrboot/trunk/` (asm comments are CP866, `read_me.txt`/`history.txt` are CP1251) |
| `FPGA/` | `emulators/github/pentevo/fpga/baseconf/trunk/` |
| `ROM/` | `emulators/github/pentevo/rom/` (CP866) |
| `TOOLS/` | `emulators/github/pentevo/tools/` |
| `VEXT` | `emulators/github/zx-evo-docs/GluExt/version_ext.md` |

The MCU is an ATmega128 at 11.0592 MHz (`AVRB/Makefile:18,24`; `BOOT/avr/boot_evo.asm:195`).

---

### 0. Big picture

```
 Z80 --(IN/OUT #BFF7)--> FPGA zports: asserts Z80 /WAIT, raises spiint_n
                          |
 AVR INT6 (falling) <-----+   AVR reads the SPI status byte, then the Gluk address ($41),
                              computes/stores the byte, writes/reads $40; nSPICS 0->1 ends the wait
 AVR  <--TWI-->  PCF8583 RTC (I2C 0xA0) + its 240 bytes of battery RAM
 AVR  internal EEPROM (4 KiB): user PS/2 keymap + a Z80-accessible window
 AVR  flash (128 KiB): main firmware + MegaLZ-packed FPGA bitstream (0..0x1DFFF), 8 KiB bootloader (0x1E000..0x1FFFF)
```

In BaseConf, "the configuration" is one unit: the AVR main firmware image embeds the one FPGA bitstream. The AVR has no config selection. The **CONF_VERSION** readable from the Z80 is therefore the version tag of the AVR main firmware image (`zxevo_fw.bin`), stored at AVR flash 0x1DFF0.

---

### 1. Gluk / CMOS register map as seen from the Z80

#### 1.1 Port decoding (FPGA side)

| Port | Function | Evidence |
|---|---|---|
| `#EFF7` bit 7 | Gluk enable (`gluclock_on = peff7_int[7] \|\| shadow`). In shadow/DOS mode Gluk access is **always on**. | `FPGA/z80/zports.v:701` |
| `#EFF7` write | Latched only when `!a[12]` and **not** in shadow mode ("EEF7 in shadow mode is abandoned"). Reset value 0x00. | `FPGA/z80/zports.v:676-683` |
| `#DFF7` (`!a[13]`) | Gluk address register. **Latched in the FPGA** (`gluclock_addr <= din`). No wait, no AVR involvement. | `FPGA/z80/zports.v:704-710` |
| `#BFF7` (`!a[14]`) | Gluk data. Both IN and OUT start a **wait cycle** (`wait_start_gluclock`). OUT data is latched into `wait_write`. IN returns `wait_read`. | `FPGA/z80/zports.v:727-731,741`; read mux `:424-429` |
| Shadow aliases | In shadow mode the F7 ports answer only with `a[8]=0` (e.g. `#DEF7`/`#BEF7`) so they do not clash with the ATM `xFF7` ports. Outside shadow mode they answer only with `a[8]=1`. | `FPGA/z80/zports.v:457-463`; ROM constants `ROM/ports_evo.a80:18-22` (`CMOSD_SET_ADR=0xDEF7`, `CMOSD_RD_WR=0xBEF7`, `CMOS_SET_ADR=0xDFF7`, `CMOS_RD_WR=0xBFF7`, `PENT_CONF=0xEFF7`) |
| `#xxF7` IN (other) | Returns 0xFF. | `FPGA/z80/zports.v:427-428` |
| `shadow` | `dos \|\| shadow_en_reg` (bit 0 of `#xxBF`). | `FPGA/z80/zports.v:283` |

The Z80 sequence used by the ERS service ROM (`ROM/mainmenu/src/call_cmos.a80:451-466`):
`OUT (#EFF7),#80` -> `OUT (#DFF7),addr` -> `IN (#BFF7)` -> `OUT (#EFF7),0`. `CMOS_ON=0x80`, `CMOS_OFF=0` (`ROM/mainmenu/src/main.a80:41-42`).

#### 1.2 AVR dispatch

`zx_wait_task()` (`AVR/zx.c:578-633`):
- status bit 7 = 1 means the Z80 did an IN. The AVR reads the Gluk address from SPI reg `$41`, calls `gluk_get_reg(addr)`, and writes the result to `$40` (`AVR/zx.c:589-603`).
- status bit 7 = 0 means the Z80 did an OUT. The AVR reads the Z80's byte from `$40`, then calls `gluk_set_reg(addr,data)` (`AVR/zx.c:604-621`).
- Default read data is 0xFF (`AVR/zx.c:581`).

`gluk_regs[14]` holds registers 0x00-0x0D in AVR RAM (`AVR/rtc.c:18`).

#### 1.3 Registers 0x00-0x0D (MC146818 / DS12887 emulation on top of a PCF8583)

The real chip is a **PCF8583** at I2C address 0xA0 (`AVR/rtc.h:8,19-20`), driven by the AVR TWI at about 98.7 kHz (`AVR/rtc.c:186-191`). `rtc_init` writes 0 to the PCF8583 control/status register (`AVR/rtc.c:194-196`). The MC146818 register file is **emulated in AVR RAM**:
- On init, `gluk_init()` reads the PCF8583 once (`AVR/rtc.c:252-288`). Because of the check at `AVR/rtc.c:200`, it runs twice when seconds read as 0.
- After that, time advances in AVR RAM. `gluk_inc()` runs on every falling edge of INT7, the RTC interrupt line (`AVR/interrupts.c:331-336`, `AVR/rtc.c:290-322`; `AVR/main.c:226-229` sets INT7 falling edge). It handles month lengths and a simple leap-year rule (`year&3==0`) (`AVR/rtc.c:148-162`).
- Header summary: "full read/write time emulate; full read/write nvram emulate; registers A,B,C,D read only; alarm functions not emulated" (`AVR/rtc.h:10-14`). In practice A, B and C are partly writable, as the table below shows.

| Reg | Read | Write | Evidence |
|---|---|---|---|
| 0x00 sec | from RAM. BCD unless B.DM=1 (index<10 is converted with `hex_to_bcd`). | Stored in RAM. If <=59, also written to PCF 0x02 (BCD). | `AVR/rtc.c:342-350,447-449` |
| 0x01 sec alarm | RAM only | RAM only (no PCF write) | `AVR/rtc.c:434-442` (no case in switch 445-475) |
| 0x02 min | RAM | RAM + PCF 0x03 if <=59 | `AVR/rtc.c:451-453` |
| 0x03 min alarm | RAM only | RAM only | same |
| 0x04 hour | RAM (24 h only; "TODO 12/24") | RAM + PCF 0x04 (`0x3F & bcd`) if <=23 | `AVR/rtc.c:285,455-457` |
| 0x05 hour alarm | RAM only | RAM only | same |
| 0x06 day of week (1..7) | RAM | RAM + PCF 0x06 (`(dow-1)<<5 \| bcd(month)`), only if dow 1..7 and month 1..12. The PCF 0..6 weekday is converted to DS 1..7. | `AVR/rtc.c:264-268,459-468` |
| 0x07 day of month | RAM | RAM + PCF 0x05 (`year<<6 \| bcd(day)`) | `AVR/rtc.c:472-474` |
| 0x08 month | RAM | RAM + PCF 0x06 (as dow) | `AVR/rtc.c:459-468` |
| 0x09 year (0..99) | RAM | RAM + PCF 0xFF (full year) + PCF 0x05. The PCF holds only 2 year bits; full year kept in PCF RAM 0xFF, with wrap correction on init. | `AVR/rtc.c:270-282,470-474` |
| 0x0A "A" | returns `gluk_regs[A]`, which is **the EEPROM page pointer, not UIP/DV/RS**. Init 0x00. | Sets the EEPROM page (see 1.5). | `AVR/rtc.c:481-484`, `AVR/rtc.h:92` |
| 0x0B "B" | `gluk_regs[B]`. Init 0x02 (24 h, BCD). | Stores only the DM bit: `(data & 0x04) \| 0x02`. | `AVR/rtc.c:486-489`, `AVR/rtc.h:76-79,94` |
| 0x0C "C" | See the bit map below. Reading **clears UF** (bit 4). | See the bit map below. | `AVR/rtc.c:352-371,491-510` |
| 0x0D "D" | bit 7 = 1 (VRT, init 0x80). Bits 6..0 = live PS/2 modifier state: b0 LCtrl, b1 RCtrl, b2 LAlt, b3 RAlt, b4 LShift, b5 RShift, b6 F12. | ignored | `AVR/rtc.c:373-378`, `AVR/zx.h:83-96`, `AVR/rtc.h:98` |

**Register C (0x0C) bits (ZX-Evo specific):**

| Bit | Read | Write | Evidence |
|---|---|---|---|
| 7 | EEPROM-window mode flag (`GLUK_C_EEPROM_FLAG = 0x80`; the header comment wrongly says "2 bit") | Toggles when the written bit differs from the current state, so it effectively sets the mode. | `AVR/rtc.h:88-89`, `AVR/rtc.c:505-509` |
| 4 | UF: set every second by `gluk_inc`, cleared on read | - | `AVR/rtc.c:321-322,354-355` |
| 3 | SD card detect (1 = card present; PB5 inverted) | - | `AVR/rtc.c:357-360`, `AVR/pins.h:179-186` |
| 2 | SD write-protect (1 = protected; PB4 inverted) | - | same |
| 1 | CAPS LED state as last set | A written value that differs from the stored bit toggles `MODE_CAPSLED` and sends a PS/2 SET-LED command. | `AVR/rtc.c:497-504` |
| 0 | **Tape-out/beeper-mux mode** (`modes_register & 0x02`, which is also the NumLock LED bit). It is not a Num Lock key state. | 1 = clear the PS/2 keyboard log | `AVR/rtc.c:362-370,492-496`, `AVR/rtc.h:82-85`, `AVR/ps2.h:47` |

Release notes confirm these additions (`AVR/main.h:11-17,30-33`).

#### 1.4 Registers 0x0E..0xEF: user CMOS RAM, stored in the PCF8583 RAM

For `0x0E <= idx <= 0xEF` the AVR reads/writes the **PCF8583 at address `idx+2`**, so CMOS 0x0E maps to PCF 0x10 (`AVR/rtc.c:395-401,529-535`: "on PCF8583 nvram started from #10, on 512vi1[DS12887] nvram started from #0E").

- Every access is a live I2C transaction. There is no RAM cache.
- CMOS 0xEF maps to PCF 0xF1.
- PCF RAM bytes used privately by the AVR, not reachable through CMOS indexes: 0xFD = PS/2 mouse resolution (bits 1..0), 0xFE = saved `modes_register` (VGA/raster/tapeout), 0xFF = full year (`AVR/rtc.h:22-27`; `AVR/rtc.c:203`; `AVR/zx.c:643-644`; `AVR/ps2.c:543,836-861`).
- This is the "boot from"/ERS settings store. The ERS layout counts downward from 0xF0 (`ROM/global_vars.a80:172-201`, `_MINUSVAR` pre-decrements, `ROM/macros.a80:366-372`):

| CMOS | ERS name | Meaning (from ROM comments) | Evidence |
|---|---|---|---|
| 0xEF / 0xEE | CRCHIGH / CRCLOW | CRC of the ERS CMOS settings | `ROM/global_vars.a80:191-192` |
| 0xED | BYTE_00 | b7 TURBO14, b6 EMUL_TAPE, b5 PRINTER_AY, b4 RELOAD_FONT, b3 TYPE_FONT, b2 AUTO_TAPE. **b1..0 = reset target**: 0 EVO SERVICE, 1 GLUK SERVICE, 2 PROFROM, 3 CUSTOM ROM. | `ROM/global_vars.a80:226-240`; used at `ROM/page0/source/services.a80:101-117` |
| 0xEC | BYTE_01 | b7 TURBO 3.5/7, b6 NeoGS SD, b5 automount, b4 clock view, b3 key click, b2 resident. b1..0 memory model (0 = 1 MB, 1 = 48K, 2 = 128K). | `ROM/global_vars.a80:242-255` |
| 0xEB | VIRT_REAL_DRIVE | b7 ZC SD access, b6 HDD master, b5 HDD slave, b3..2 real drive, b1..0 virtual drive | `ROM/global_vars.a80:195,260-266` |
| 0xEA | HDD_TIMEOUT | b7..4 screensaver timeout, b3..0 HDD detect timeout | `ROM/global_vars.a80:196,268-271` |
| 0xE9 / 0xE8 | BYTE_02 / BYTE_03 | "CMOS_E9" masks: b7 KILL_REZIDENT, b6 AUTOBOOT, **b1..0 BOOTDEVICE (0 FDD, 1 HDD, 2 SD)**. The ROM reads these masks from `BYTE_03`. The label/address naming in the ROM is inconsistent. | `ROM/global_vars.a80:197-198,273-283`; `ROM/mainmenu/src/main.a80:767-769` |

The ERS saves and loads the whole 0x00..0xEF range (`ROM/mainmenu/src/call_cmos.a80:423-447`).

#### 1.5 Registers 0xF0..0xFF: the extension window

`gluk_get_reg`/`gluk_set_reg` for `idx >= 0xF0` (`AVR/rtc.c:382-394,516-528`):

**Mode A: EEPROM window (C.bit7 = 1).**
- Read/write AVR internal EEPROM at `(A << 4) + (idx & 0x0F)`, where A = register 0x0A (`AVR/rtc.c:164-184`). This gives 16-byte pages. A = 0..255 covers the whole 4 KiB EEPROM.
- EEPROM contents: user PS/2 keymap at 0x000 (signature 'K','B' in bytes 0..1, then 2 bytes per scancode) and E0-prefixed map at 0x100 (`AVR/kbmap.c:291-294,326-337,349-367`). The Z80 can therefore read and replace the keymap. "Add EEPROM access via RTC interface" (`AVR/main.h:15`).

**Mode B: version/extension window (C.bit7 = 0, the default after power-up).**
- **Any write to any 0xF0..0xFF selects the extension type**: `SetVersionType(data)` sets `ext_type_gluk = data` (`AVR/rtc.c:524-527`, `AVR/version.c:47-50`).
- A read of 0xF0+n returns `GetVersionByte(n)` (`AVR/rtc.c:390-393`, `AVR/version.c:13-45`). `ext_type_gluk` is 0 at boot (`AVR/main.c:214`).

| EXTSW value | Name (`AVR/main.h:157-166`) | Read of 0xF0+n (n=0..15) |
|---|---|---|
| 0 | EXT_TYPE_BASECONF_VERSION | `pgm_read_byte_far(0x1DFF0 + n)`: main-firmware version tag (`AVR/version.c:8,19-23`) |
| 1 | EXT_TYPE_BOOTLOADER_VERSION | `pgm_read_byte_far(0x1FFF0 + n)`: bootloader version tag (`AVR/version.c:11,25-29`) |
| 2 | EXT_TYPE_PS2KEYBOARDS_LOG | `ps2keyboard_from_log()`. **n is ignored.** Every read pops the next raw PS/2 byte (see 1.6). (`AVR/version.c:31-35`) |
| 3 | EXT_TYPE_RDCFG | n=0: `modes_register`. Otherwise 0xFF. (`AVR/version.c:37-41`) |
| other | - | 0xFF (`AVR/version.c:44`) |

`modes_register` bits (the RDCFG byte), per `AVR/main.h:145-155`:

| Bit | Meaning |
|---|---|
| 0 | VGA (1) / TV (0). Also the ScrollLock LED. |
| 1 | tape-out beeper mux (1 = tapeout). Also the NumLock LED. |
| 2 | CAPS LED |
| 5..4 | raster: 00 Pentagon 71680, 01 60 Hz, 10 48K 69888, 11 128K 70908 |

This matches the BaseConf table in `VEXT` (lines "BaseConfig", bits 5..4/3/2/0). Note: `VEXT` says bit 3 = Beeper_Mux, but in this source the tapeout mode is `modes_register` bit 1 (`MODE_TAPEOUT 0x02`, `AVR/main.h:149-150`). Bit 3 is the SPI config0 bit, not a `modes_register` bit. RDCFG returns the raw `modes_register`.

**Extensions in `VEXT` that are NOT in this BaseConf AVR source:** 0x0E CONFIG (MODES_VIDEO/MISC, HOTKEYS, PAD_*, PROTECT, COMMAND F7=REBOOT / FE=REBOOT_FLASH) and 0x10 SPIFL (SPI-flash interface, BSLOAD, BTF tags). `version.c` handles only types 0..3, so these return 0xFF here. They come from a different, newer AVR firmware that is not in `pentevo/avr/baseconf`.

A consequence of "any write to F0..FF is EXTSW" (also noted in `VEXT`) is that writing SFI/CONFIG registers F1..FF on BaseConf silently changes the extension type. Software must restore it with `OUT F0,0`.

#### 1.6 PS/2 keyboard log (extension 2)

- 16-byte ring buffer (`AVR/ps2.c:72-74`).
- Raw scancode bytes are logged, including E0/F0 prefixes. Protocol bytes FA/FE/EE/AA are not logged. The PAUSE (E1 plus 7 bytes) is not logged (`AVR/ps2.c:360-378`).
- After a reset of the log (`start=0xFF`), logging resumes only at the start of a clean make sequence, not in the middle of an E0/F0 prefix (`AVR/ps2.c:368-377`).
- Read returns 0 when the log is empty or in reset state. It returns **0xFF on overflow**, and reading the 0xFF resets the log (`AVR/ps2.c:127-178`).
- Cleared by writing register C with bit 0 = 1 (`AVR/rtc.c:492-496`).
- The ERS PC-keys tester uses exactly this: it reads `CMOS.READ_PS2` (= 0xF0) repeatedly, treats 0 as nothing, 0xFF as reinitialize, and parses E0/F0 (`ROM/mainmenu/src/pc_keys_test.a80:49-64`; `ROM/global_vars.a80:199-201`).

#### 1.7 What exactly the Z80 reads for versions (answers item 4)

Layout of the 16-byte tag (`AVR/version.h:11-30`; `BOOT/read_me.txt:82-99` [CP1251]):

| Offset | Content |
|---|---|
| 0x00..0x0B | ASCII name, zero-padded to 12 bytes. It is not terminated when exactly 12 chars. |
| 0x0C..0x0D | 16-bit date word, **little-endian**: bits 4..0 day (1..31), bits 8..5 month (1..12), bits 14..9 year-2000 (0..63), bit 15 "official release" (0 = beta). So `byte0C = day \| (month&7)<<5` and `byte0D = (month>>3) \| year<<1 \| official<<7`. |
| 0x0E..0x0F | CRC-16/CCITT of the image, **big-endian** (hi byte first) |

Example from the header: `50 65 6E 74 31 6D 00 00 00 00 00 00 7B 14 3C B1` is "Pent1m", 27.03.2010, non-official, CRC 3CB1 (`AVR/version.h:22-30`).

Where the bytes come from:

- **Main firmware (EXTSW=0)**: AVR flash 0x1DFF0..0x1DFFF. Written at build time by `make_fw`:
  - name = first 12 chars of `version.txt` (`strncpy(&fbuff[0x1dff0], vs, 12)`)
  - date word at 0x1DFFC/D, from the host's local date; the official bit is set when the 5th argument is `o`
  - CRC16-CCITT (poly 0x1021, init 0xFFFF) over 0x00000..0x1DFFD stored big-endian at 0x1DFFE/F

  Evidence: `TOOLS/make_fw/source/make_fw.c:205-209,261-265,283-286`. The BaseConf Makefile calls `make_fw core.hex core.eep zxevo.bin version.txt o`, so builds are "official" (`AVRB/Makefile:100-101`). `version.txt` = `ZXEvo 4M` (`AVRB/version.txt:1`). The Z80 therefore sees `5A 58 45 76 6F 20 34 4D 00 00 00 00 <dateLo> <dateHi|0x80> <crcHi> <crcLo>`.
- **Bootloader (EXTSW=1)**: AVR flash 0x1FFF0..0x1FFFF (`BOOT_VERS` at `.ORG FLASHEND-7` words = 0xFFF8 words = 0x1FFF0 bytes; placeholders `.DW 0...`) (`BOOT/avr/boot_evo.asm:2123-2128`). The bootloader's own image carries it: `crcbldr` patches it at build time (name from `version.txt` at +0x1FF0, date at +0x1FFC/D with an optional `o` official flag, CRC over the 8 KiB boot block at +0x1FFE/F; offsets relative to 0x1E000) (`TOOLS/crcbldr/source/crcbldr.c:98-99,196-209`; `BOOT/avr/make.bat`). `BOOT/avr/version.txt` = `ZXEvoAVRBoot` (exactly 12 chars, so no NUL). The main firmware simply `pgm_read_byte_far`s the boot-block address (`AVR/version.c:11,28`). The bootloader itself also prints both tags to RS-232 on the update path (`BOOT/avr/boot_evo.asm:205-222,1690-1737`, `MAIN_VERS=$EFF8` words = 0x1DFF0 bytes, `:62`).

Z80 protocol used by the ERS ROM to show "BaseConf/AVRBoot" versions (`ROM/mainmenu/src/call_cmos.a80:474-543`, called from `ROM/mainmenu/src/main.a80:775-780` with L=0 then L=1):
1. `WRITECMOS(0xF0, type)` (type 0 or 1).
2. `READCMOS(0xF0)`. If the byte equals the written value, it is a plain DS12887-style CMOS RAM (not Evo, or an old FPGA): give up. If 0xFF, there is no clock: give up.
3. Read 0xF1..0xFF (15 more bytes) into a 16-byte buffer.
4. Format as `<name up to 12 chars> DD.MM.20YY`, and append `" beta"` if bit 15 of the date word is 0.

The ERS does not reset C.bit7; it relies on the power-up default of 0. For RDCFG the ERS writes 3 to 0xF0 and reads 0xF0, then decodes bits 5..4 (raster) and bit 0 (TV/VGA) (`ROM/mainmenu/src/call_cmos.a80:596-635`).

---

### 2. AVR <-> FPGA SPI protocol (`slavespi.v`, `spi_fmt.txt`)

#### 2.1 Physical/framing
- AVR is SPI master. `SPCR=0b01110000` means SPE, **DORD=1 (LSB first)**, MSTR, mode 0. `SPSR=1` means SPI2X (fosc/2 = 5.53 MHz) (`AVR/spi.c:7-11`). The FPGA shifts LSB-first: `regnum <= {sdo, regnum[7:1]}` (`FPGA/slave/slavespi.v:155`). All signals are resynchronized to `fclk` (`:79-100`).
- **Register number** is shifted in while `spics_n = 1`. The FPGA clears it to 0 on every `spics_n` 0->1 edge (`FPGA/slave/slavespi.v:147-157`; `FPGA/slave/spi_fmt.txt` "установка номера - при spics_n=1 ...", "после передерга spics_n 0->1 номер заново надо ставить").
- **Data** is shifted while `spics_n = 0`. Strobes and latches fire on `spics_n` 0->1 (`scs_n_01`) (`FPGA/slave/slavespi.v:210-283`).
- **MISO**: on either CS edge `shift_out` is loaded with **status** (when CS=1) or with the selected register's read data (when CS=0) (`FPGA/slave/slavespi.v:172-184`). So the byte clocked while sending the register number returns the **status**, and the byte clocked during the data phase returns register data.
- AVR access routine `zx_spi_send(addr,data,mask)` (`AVR/zx.c:54-69`): CS low then high (latch status) -> `status = spi_send(addr)` (CS high) -> CS low -> `ret = spi_send(data)` -> CS high (strobe). If `status & mask` (a wait pending), it immediately services the wait. Normal traffic uses mask 0x7F.

#### 2.2 Status byte (`status_in`)
`{wr_n, waits[6:0]}` (`FPGA/top.v:752`):

| Bit | Meaning | Evidence |
|---|---|---|
| 7 | Z80 `/WR` at latch time: 1 = the Z80 is waiting in an IN (read), 0 = in an OUT (write) | `spi_fmt.txt` "bit.7 - Read-Not-Write" |
| 0 | Gluk clock wait pending (`ZXW_GLUK_CLOCK=0x01`) | `AVR/zx.h:157-161`; `FPGA/z80/zwait.v:57-61` |
| 1 | Kondratiev RS-232 (#F8EF..#FFEF) wait pending (`ZXW_KONDR_RS232=0x02`) | `FPGA/z80/zwait.v:63-67` |
| 6..2 | reserved (0) | `FPGA/z80/zwait.v:70-73` |

`spiint_n = ~|waits` drives AVR INT6. The same condition pulls Z80 `/WAIT` low (`FPGA/z80/zwait.v:78-79`). INT6 only sets `FLAG_SPI_INT` (`AVR/interrupts.c:324-329`). The main loop then reads status (CS pulse, `spi_send(0)`) and calls `zx_wait_task` (`AVR/main.c:257-266`).

#### 2.3 Register map

| Reg | Dir | Width | Function | Evidence |
|---|---|---|---|---|
| `$00` | W (ignored) | - | default after CS 0->1; dummy | `spi_fmt.txt` |
| `$10` (`$1x`, even) | W | 40-bit shift | ZX keyboard matrix data | `FPGA/slave/slavespi.v:189,215-216` |
| `$11` (`$1x`, odd) | strobe | - | CS 0->1 with `$11` selected copies `$10` into the Z80-visible matrix (`kbd_stb`) | `:190,257-258`; `FPGA/z80/zkbdmus.v:71-74` |
| `$20` | W | 8 | Kempston mouse X (`#FBDF`) | `:192,261`; `zkbdmus.v:76-77,118-120` |
| `$21` | W | 8 | Kempston mouse Y (`#FFDF`) | `:193,262` |
| `$22` | W | 8 | Mouse buttons (`#FADF`): b7..4 wheel (1111 if none), b3=1, b2 middle, b1 right, b0 left (0 = pressed) | `:194,263`; `AVR/zx.h:130-139` |
| `$23` | W | 5 used | Kempston joystick (`kj_data <= mus_in[4:0]`): b0 R, b1 L, b2 D, b3 U, b4 Fire, active high | `:195,264`; `zkbdmus.v:85-86`; `AVR/joystick.c:202-211`, `AVR/pins.h:142-153` |
| `$30` (`$3x`) | strobe | - | Z80 reset: CS 0->1 pulses `genrst`, which drives `resetter` for the full FPGA/Z80 reset | `:197,266`; `FPGA/top.v:243-247` |
| `$40` | R/W | 8 | Wait data. Write: byte returned to a waiting IN (`wait_read`). Read: byte the Z80 wrote (`wait_write`). CS 0->1 with `$40` gives `wait_end`, which releases WAIT and clears the status bits. Shared by all wait ports. | `:199,164,223-224,268-269`; `FPGA/z80/zwait.v:52-53` |
| `$41` | R | 8 | Gluk address last written by the Z80 to `#DFF7` | `:200,165` |
| `$42` | R | 3 | Z80 A[10:8] of the last `#F8EF..#FFEF` access (UART register index) | `:201,166`; `FPGA/z80/zports.v:718-722` |
| `$50` | W | 8 | **config0**, latched on CS 0->1 | `:203,227-232`; bits below |
| `$51` | W | 8 | **config1**: b3..0 `fdd_mask` ("disk mask for ROM substitution"). In this BaseConf top it is wired to an unused `fdd_mask` net, and the AVR never writes it. | `:204,233-234`; `FPGA/top.v:193,764` |
| `$60` | R/W | 8 | SD data: write sends a byte (exchange starts at CS 0->1), read gives the last received byte | `:206,167,236-238,281-283`; `spi_fmt.txt` |
| `$61` | R/W | b7,b0 | SD control: b7 lock (R: granted), b0 CS_n (W). Reset `sdctrl=2'b01`. | `:207,168,240-253,277-278` |

`config0` bits (`FPGA/top.v:763` `{nu[7:6], modes_raster[1:0], beeper_mux, tape_read, set_nmi[0], cfg_vga_on}`), as the AVR composes them in `zx_set_config()` (`AVR/zx.c:650-659`, `AVR/zx.h:47-55`):

| Bit | Meaning |
|---|---|
| 0 | VGA on (`modes_register` bit 0) |
| 1 | NMI request. The FPGA fires on the **1->0 transition** (`set_nmi_now = \|(set_nmi_r & ~set_nmi)`, `FPGA/z80/znmi.v:124-126`). The AVR sets it while the NMI button or PrintScreen is held and clears it on release, so the NMI happens at release. |
| 2 | tape-in level shown on `#FE` D6 (from the AVR TAPEIN pin, `AVR/tape.c:10-25`) |
| 3 | beeper mux: 1 = tape-out (D3) to the speaker, 0 = beeper (D4) (`FPGA/sound/sound.v:68`) |
| 5..4 | raster mode (00 Pentagon, 01 60 Hz, 10 48K, 11 128K) |
| 7..6 | unused |

The **SD sharing** registers `$60/$61` are not used by the BaseConf main AVR firmware (no references in `AVR/`). The bootloader does not use them either: it loads its own passthrough FPGA image (see 3.2).

#### 2.4 Keyboard data encoding
- The AVR keeps `zx_map[5]`: ZX key code k (0..39, `AVR/kbmap.h:13-58`) sets bit `0x80 >> (k&7)` of byte `k>>3` (`AVR/zx.c:213-220`).
- It sends 5 bytes to `$10` in order `zx_map[4]..zx_map[0]`, each ORed with the inverted mechanical-keyboard/joystick matrix `~zx_realkbd[i]`, then strobes `$11` (`AVR/zx.c:243-299`).
- After 40 LSB-first shifts, `kbd[n]` = key code `39-n`. The FPGA maps these into the 8x5 half-row matrix (`FPGA/z80/zkbdmus.v:92-115`).
- CS/SS ordering is enforced with a pause counter (`SHIFT_PAUSE=8` timer ticks) (`AVR/zx.c:189-210`, `AVR/zx.h:67-70`).

#### 2.5 Mouse/joystick
- PS/2 mouse init sequence: reset x3, IntelliMouse magic 200/100/80, get ID, scaling 1:1, rate 100, enable (`AVR/ps2.c:455-467`). A 4-byte packet means a wheel mouse (`:716-723`).
- X/Y accumulate with deltas added modulo 256 (`AVR/ps2.c:612-635`).
- No mouse: the AVR sends X=Y=0xFF. Mouse present: initial X=0, Y=1 (`AVR/zx.c:535-550`).
- Resolution 0..3 is stored in PCF 0xFD. Change it with keypad `*`/`+`/`-` while holding both mouse buttons (`AVR/ps2.c:814-867`).
- Joystick port PG0..4 is sent to `$23` on change (`AVR/joystick.c:193-219`).
- A SEGA pad is auto-detected (`AVR/joystick.c:55-136`) and mapped into ZX keys or Kempston via `joymaps[]`. Holding the Mode+X/Y/Z combo (`jkey_state & 0x0e00`) selects one of 4 maps: default/Kempston/SMB/Elite (`AVR/joystick.c:23-49,139-191`).
- The mechanical ZX keyboard is scanned via PORTA/PORTC in the Timer2 ISR (`AVR/interrupts.c:164-214`).

---

### 3. FPGA configuration, bootloader, reset, PS/2, RTC, SD

#### 3.1 How the main firmware loads the FPGA (BaseConf)
- **Source: AVR internal flash only.** The bitstream `top.rbf` is packed with MegaLZ into `top.mlz`, converted to an ELF object with symbol `fpga` in `.progmem.data`, and linked into the firmware (`AVRB/Makefile:14,79-83`). The output is `zxevo.bin`, copied to `zxevo_fw.bin` (`AVRB/Makefile:63-64,100-101`).
- Sequence (`AVR/main.c:149-189`):
  1. Pulse nCONFIG (PF0) low 20 ms and wait for nSTATUS (PF1) high.
  2. `curFpga = GET_FAR_ADDRESS(fpga)`.
  3. `depacker_dirty()` MegaLZ-unpacks through a 2 KiB ring buffer and streams bytes over the SPI pins, LSB-first (Altera passive serial) (`AVR/depacker_dirty.c:14-79`, `AVR/depacker_dirty.h:12-18`, `AVR/main.c:45-55`).
  4. Wait 20 ms and check CONF_DONE (PF2). Retry the whole load until CONF_DONE is high. The power LED blinks during the load.
- Pins: `AVR/pins.h:16-41`.
- There is **no configuration choice and no "baseconf vs ts-conf" identification** in this firmware: one image is baked in, and the version tag (1.7) is the only identity.
- Experimental variant `avr/sdload`: it bit-bangs SPI to an SD card on PORTF (the JTAG pins), mounts FAT with Petit FatFs, lists `*.RBF` in the root on RS-232, takes a digit choice, else defaults to **`TOP.RBF`**, and streams it into the FPGA. Its flash-based loader is `#if 0`'d (`emulators/github/pentevo/avr/sdload/trunk/src/pfs/test.c:17-89`; `.../sdload/trunk/src/main.c:160-203`; `.../pfs/diskio.c:17-39`). This is not the shipping BaseConf path.

#### 3.2 Bootloader (avrboot, 8 KiB at word 0xF000 = byte 0x1E000)

Fuses: BOOTRST=0 (reset to the boot block), BOOTSZ=00 (8 KiB) (`BOOT/read_me.txt:105-139`). The flow (`BOOT/avr/boot_evo.asm`):

1. If the reset cause is **watchdog** (`MCUCSR & 0x08`), jump to 0x0000, the main firmware (`:105-110`).
2. Check the CRC of the bootloader itself. If bad, restart (`:135-140`).
3. If the **SoftReset button (PC7) is held**, go to the update path. Otherwise check the main-program CRC (CRC-16/CCITT, poly 0x1021, init 0xFFFF, over the whole 0x00000..0x1DFFF including the stored CRC; the result must be 0) (`:141-146,1875-1915`; `FLASHSIZE=480` blocks of 256 = 0x1E000, `:61`). If good, arm the watchdog and spin, so the WDT reset lands in the main firmware with a clean MCU state (`:148-152`).
4. Update path (`:179-356`):
   1. Enable ATX power and UART1 at 115200 8N2.
   2. Print an ANSI title plus `boot: <tag>` and `main: <tag>` (or "Bad CRC!").
   3. Wait for power, then load a **temporary FPGA config** (`avrboot/fpga/main.v`, MegaLZ-packed and embedded as `PACKED_FPGA`). It holds the Z80 in reset and wires the AVR SPI straight to the SD card (`sdclk=spick, sddo=spido, spidi=sddi, sdcs_n=spics_n`) (`BOOT/fpga/main.v:121,150-153`; `BOOT/avr/boot_evo.asm:2116-2119`).
5. SD init supports SD v1/v2/SDHC/MMC (`:360-415`; `BOOT/history.txt` 2010.12.31 adds MMC). FAT12/16/32 is detected via the MBR/BPB. The root directory is searched for the 8.3 name **`ZXEVO_FW.BIN`** (`FILENAME: "ZXEVO_FWBIN"`) (`:664-707,2067`).
6. File format, produced by `make_fw` (`TOOLS/make_fw/source/make_fw.c:196-316`):
   - 128-byte header:
     - `+00` `"ZXEVO",0x1A` signature (checked by `CHECK_SIGNATURE`, `BOOT/avr/boot_evo.asm:1183-1193,2068`)
     - `+06..` version text (up to 56 chars, NUL-terminated)
     - `+3E/3F` date word
     - `+40..+7B` 480-bit bitmap of non-empty 256-byte flash blocks
     - `+7C..+7D` 16-bit bitmap of non-empty 256-byte EEPROM blocks
     - `+7E/7F` CRC-16/CCITT (init 0) of bytes 0..7D, big-endian. The bootloader checks that the CRC over all 128 bytes is 0 (`:753-766`).
   - Then the listed 256-byte blocks in order.
   - The bootloader erases and programs each flash page (empty blocks are erased to FF) (`:768-838,1925-`). It then writes the EEPROM blocks whose bits are set; blocks not listed keep their EEPROM content (`:840-902,1994-2027`; `BOOT/history.txt` 2010.03.06).
   - Afterwards it re-checks the main CRC and prints "Ok!" plus the new version, or "Update is failure." (`:156-175`).
7. **No version comparison** is done. Acceptance relies only on the header CRC, the signature and the final flash CRC.
8. On SD failure it beeps the error code (1 card, 2 read error, 3 FAT, 4 file not found, 5 wrong file), then falls back to **RS-232 XModem-CRC**. It waits about 60 s, then retries booting the main firmware (`:904-1003`).

#### 3.3 Main firmware start-up and ATX/power
- `wait_for_atx_power()` (`AVR/atx.c:18-72`):
  - After a power-on reset, blink about 2 s, then wait for the SoftReset button. If it was pressed during the blink, continue at once.
  - Then set ATXPWRON (PF3) and wait 1 s.
  - The "power present" test is the nCONFIG pin reading high (`:33`).
- Main loop tasks: `AVR/main.c:246-270`.
- Init order: FPGA load, then Timer2 (about 337.5 Hz tick), PS/2 init, INT4/5/6/7 falling edge (keyboard/mouse/SPI-wait/RTC), `kbmap_init`, `zx_init` (resets the Z80 via `$30`), `rtc_init` (restores `modes_register` from PCF 0xFE and pushes config0), then the joystick (`AVR/main.c:196-244`, `AVR/zx.c:44-52`, `AVR/rtc.c:186-207`).

#### 3.4 Reset / hotkeys handled by the AVR (PS/2 set-2 scancodes)

| Input | Action | Evidence |
|---|---|---|
| **Ctrl+Alt+Del** (either Ctrl, either Alt; Del is E0 71) | `FLAG_HARD_RESET`: the main loop exits, `goto start` re-inits the AVR, **reloads the FPGA bitstream**, and resets the Z80. The Del key is not passed to the ZX. | `AVR/zx.c:366-386`; `AVR/main.c:270-272` |
| **F12** or the **SoftReset button**, short press | On release: `zx_spi_send(SPI_RST_REG)`, a Z80/FPGA-logic reset only | `AVR/interrupts.c:151-162`; `AVR/atx.c:119-123` |
| F12 / SoftReset held > `PWROFF_KEY_TIME` = 1000 Timer2 ticks (about 3 s at 337.5 Hz; comments say 5 s) | ATX power off, then `FLAG_HARD_RESET`, then back to `wait_for_atx_power` | `AVR/atx.h:15-16`; `AVR/atx.c:80-118` |
| **PrintScreen** (E0 7C) or the NMI button (PC6) | NMI via config0 bit 1 (fires on release) | `AVR/zx.c:352-365,125-143` |
| **ScrollLock** | Cycles the 3-bit video mode counter {VGA bit0, raster bits5..4}: TV/VGA toggles, carrying into the raster (Pent -> 60 Hz -> 48K -> 128K). Saved to PCF 0xFE, LEDs updated. | `AVR/zx.c:394-420,635-648` |
| **NumLock** | Toggles tape-out/beeper mux (`MODE_TAPEOUT`), saved | `AVR/zx.c:421-425` |
| Keypad `*`/`+`/`-` with both mouse buttons held | Mouse resolution | `AVR/zx.c:451-456` |
| **ESC** (76, also E0 76) | `CLRKYS`: clears all ZX keys | `AVR/kbmap.c:140,279`; `AVR/zx.c:156-168,474-481` |
| F9/F10/F11 | not used for reset (older versions used them) | `AVR/main.h:74` |

The old reset-to-ROM-type keys (`RST_48/RST128/RSTRDS/RSTSYS`) are commented out (`AVR/kbmap.h:60-64`, `AVR/zx.c:169-186,258-270`). **The AVR does not choose the ROM page.** The reset target is decided by the FPGA and the service ROM:
- FPGA reset state: `atm_pen=1` (pager off), which maps the same ROM page 0xFF (inverted page numbering) into every window, and `atm_cpm_n=0` (permanent DOS/shadow) (`FPGA/z80/zports.v:840-846`; `FPGA/mem/atm_pager.v:113-117`). The Z80 therefore starts in the Evo service ROM ("page0").
- The service ROM holds some keys during reset: "0" resets the resident, "6" enters the debugger. Otherwise it reads **CMOS 0xED bits 1..0** and configures the pages for EVO SERVICE (0), GLUK (1), PROFROM (2) or CUSTOM ROM (3) (`ROM/page0/source/services.a80:74-117`; `ROM/global_vars.a80:235-240`).
- The "boot device" (FDD/HDD/SD) and autoboot are in CMOS 0xE9/0xE8 bits (1.4). Both are purely Z80-ROM policy.

#### 3.5 PS/2 keyboard processing and keymap
- Bit-level receive and send run in the INT4 ISR with Timer2 timeouts (`AVR/interrupts.c:217-268,123-133`).
- Parser (`AVR/ps2.c:349-446`): handles E0/F0/E1, suppresses typematic repeats (`last_scancode`), drops fake E0 12.
- `to_zx` handles hotkeys and translates through `kbmap_get` (`AVR/zx.c:327-466`). Each PS/2 key maps to up to 2 ZX keys. Examples: BACKSPACE = CS+0, arrows = CS+5..8, CAPSLOCK = CS+2, `,` = SS+N, Del = CS+9, Home = SS+Q, End = SS+E, Ins = SS+W, PgUp/PgDn = CS+3/CS+4 (`AVR/kbmap.c:13-150,152-289`).
- Scancode 0x83 (F7) is remapped to 0x7F (`AVR/zx.c:331-332`).
- Default map in PROGMEM; user map in EEPROM when the signature 'KB' is present (`AVR/kbmap.c:306-347`). There are no alternate "layouts" beyond the EEPROM user map.
- Modifier state is exposed through register D (1.3).
- LEDs: the SET-LED command sends `modes_register & 7`: Scroll = VGA, Num = tapeout, Caps = Gluk C bit1 (`AVR/ps2.c:232-247`).
- A keyboard error triggers a keyboard RESET command and clears the ZX keys (`AVR/ps2.c:249-275`).

#### 3.6 RTC handling
Covered in 1.3/1.4. In summary:
- PCF8583 over TWI, read once at boot.
- Time is kept in AVR RAM and ticked by the RTC INT line on INT7.
- Writes go through to the PCF.
- The full year is kept in PCF RAM 0xFF, because the PCF stores only 2 year bits.
- Alarms are not emulated.

#### 3.7 SD-card related AVR functionality (main firmware)
- Only the **card-detect and write-protect pins**, exposed in Gluk register C bits 3/2 (`AVR/rtc.c:357-360`).
- The `$60/$61` AVR-side SD access in `slavespi.v` is unused by this firmware.
- The Z80 itself sees the SD through FPGA ports `#77`/`#57`. In this BaseConf `#77` reads always return 0x00, meaning "inserted, R/W" (`FPGA/z80/zports.v:194-195,418-421`).
- SD-based flashing is the bootloader's job (3.2).

#### 3.8 Kondratiev RS-232 (the second wait device)
`#F8EF..#FFEF` form a 16550-like UART emulated by the AVR on UART1 (`AVR/rs232.c:194-380`, register index = A[10:8] from SPI `$42`). If DLM bit 7 is set, UBRR is loaded directly (`AVR/rs232.c:148-150`; `AVR/main.h:47`). It uses the same wait mechanism as the Gluk ports (status bit 1).

---

### 4. Emulator-relevant summary (what to model)

1. **`#EFF7.7` gate** (or shadow mode, where the ports move to `a[8]=0` aliases).
   - `#DFF7` write latches the address.
   - `#BFF7` IN/OUT: dispatch `idx < 0x0E` to the MC146818 emulation, `0x0E..0xEF` to battery RAM (PCF 0x10..0xF1), and `0xF0..0xFF` per C.bit7: EEPROM window `(A<<4)+(idx&15)`, or the extension (write = select type; read = type-dependent byte).
2. **Version reads**: type 0 returns 16 bytes from AVR flash 0x1DFF0; type 1 returns 16 bytes from 0x1FFF0. Format: 12-byte name, LE date word (bit 15 official), BE CRC. Emulator defaults could be `"ZXEvo 4M"` and `"ZXEvoAVRBoot"` with a plausible date. **Reading back the written type value from 0xF0 must not happen**, because the ERS uses that echo to detect "not Evo".
3. **Register C read**: clears UF. Bit 0 = tapeout mode, bits 3/2 = SD present/WP, bit 7 = EEPROM mode.
4. **Register D read**: 0x80 | modifier bits.
5. **Register A** is the EEPROM page. **Register B** keeps only DM (bit 2) plus bit 1 = 1.
6. **Extension 2**: 16-byte raw PS/2 scancode FIFO. 0 = empty, 0xFF = overflow.
7. **Extension 3, index 0**: `modes_register` (bit 0 VGA, bit 1 tapeout, bit 2 caps, bits 5..4 raster).
8. **Extensions 0x0E/0x10** from `version_ext.md` are **not** part of this BaseConf AVR firmware; on this firmware those types read 0xFF.


---

## C. ROM (512K flash), Evo Reset Service, virtual TR-DOS software, IDE/CD, NedoOS

(Merged from ROM sub-report; headings demoted one level. ROM equates cross-checked: `pentevo/rom/ports_evo.a80:41-42` EXIT_PORT=#BE, LBASE=#BD — the current ROM targets the trdemu FPGA readback on #xxBD.)

### Part C: ZX Evolution BaseConf ROM (512K flash), ERS, virtual TR-DOS, IDE/CD, NedoOS

Path abbreviations used below:

- `ROM/` = `emulators/github/pentevo/rom/`
- `FPGA/` = `emulators/github/pentevo/fpga/base_trdemu/trunk/`. This is the currently released BaseConf with trdemu. `fpga/baseconf/trunk` is frozen.
- `NOS/` = `emulators/github/NedoOS/src/`
- `DOC` = `FPGA/doc/zxevo_trdemu.odt`, the BaseConf user manual dated 27.02.2020. Its text was extracted with `unzip -p … content.xml`, and sections are quoted by title.

All ROM sources are CP866-encoded. Line numbers refer to the original files; the UTF-8 conversion keeps them unchanged.

---

### 1. Default ROM image layout

#### 1.1 How the 512K image is assembled

The 512K image is 8 slots of 64K each. Each slot holds 4 pages of 16K. Flash offset = page × 16K.

`ROM/build_full.bat` (Windows) is the authoritative script:

- `:87` builds `ers.rom` = `basic48_128 + evo-dos_virt + rst8service + ff_16k + basic48_128 + evo-dos_emu3d13 + basic128 + services` (192K).
- `:88` builds `ers_fe.rom` = `ff_16k + ff_16k + rst8service_fe + ff_16k + basic48_128 + neo-dos + basic128 + services_fe` (192K).
- `:93` builds `glukpent.rom` = `page3/2006.rom + trdos_v6/dosatm3.rom + page2/basic128.rom + page0/glukpen.rom` (64K).
- `:98` builds `basics_std.rom` = `atm_cpm/rbios.rom + page3/basic48_128_std.rom + page2/128_std.rom + page3/basic48_orig.rom` (64K).
- `:104` builds `zxevo.rom` = `ff_64k + basics_std + glukpent + profrom/evoprofrom.rom (128K) + ers.rom`. The comment at `:103` gives the sizes: 64/64/64/128/192.
- `:110` builds `zxevo_fe.rom` from the same parts, with `ers_fe.rom` in place of `ers.rom`.

`ROM/build_full_trd503.bat:69` builds `ers_fe` with `page1\trdos503.rom` (a patched TR-DOS 5.03) in the DOS slot. `:85` then writes the result to `zxevo_fe_trd503.rom`.

`ROM/build_full.sh` (Linux) differs from the .bat in three ways:

1. It builds only `zxevo_fe.rom` (`:94`).
2. Its ERS_FE line uses **`page1/tr5_03.rom`** (a stock TR-DOS 5.03) instead of `neo-dos.rom` (`:84`).
3. It does not build evo-dos or trdos503.

Other pieces:

- `rst8service.rom` / `rst8service_fe.rom` are 80K (5 pages). They are built with and without `-D DOS_FE` (`ROM/page5/source/build.sh`).
- `services.rom` / `services_fe.rom` are built the same way (`ROM/page0/source/build.sh`).
- `main.rom` / `main_fe.rom` (the ERS menu) are built the same way and stored LZ-packed (`ROM/mainmenu/src/build.sh`).
- `dos_fe.rom` is the page-#FE VG93 emulator, stored packed as `page1/dos_fe_pack.rom` (`ROM/page1/dos_fe/build.sh`).

#### 1.2 Resulting page map

I verified the map by hashing and string-scanning each 16K page of the prebuilt ROMs in `ROM/`.

- **Page N** is the physical 16K flash page.
- **#xFF7 value** is the value written to the ATM ROM pager, where page = `~value & 0x1F`.
- **Logical name** is the constant from `ROM/global_vars.a80:38-56` (`ROM_*`, `INIT_VAR`/`SETVAR` from 0).

| Page | Slot | zxevo.rom (non-FE) | zxevo_fe.rom / _trd503 | Logical name / #xFF7 value |
|---|---|---|---|---|
| 0–3 | 0 | FF (empty, **CUSTOM ROM** slot) | FF | `CONF4CUSTOM`=0x9C (`global_vars.a80:60`) |
| 4 | 1 | ATM CP/M `rbios` | same | `ROM_ATMCPM`=0x1B |
| 5 | 1 | BASIC48 for 128 (std) | same | `ROM_BAS48_128`=0x1A |
| 6 | 1 | BASIC128 std | same | `ROM_BAS128_STD`=0x19 |
| 7 | 1 | BASIC48 original | same | `ROM_BAS48_STD`=0x18 (`global_vars.a80:52-56`) |
| 8 | 2 | GLUK "2006" 48 BASIC | same | `CONF4GLUK`=0x94 (+3 → 0x97 = pages 8/9) |
| 9 | 2 | TR-DOS 6.10 `dosatm3` | same | |
| 10 | 2 | BASIC128 | same | 0x95 → pages 10/11 |
| 11 | 2 | Gluk Reset Service (`glukpen`, "GLUK R…") | same | |
| 12–19 | 3–4 | EVO ProfROM 128K ("Profesional Extension", "AutoConfig Ok") | same | `CONF4PROF`=0x90 |
| 20 | 5 | BASIC48 (copy) | FF | `ROM_ADD_BAS48`=11 |
| 21 | 5 | EVO-DOS "virt" (full port-intercept variant) | FF | `ROM_ADD_DOS`=10 |
| 22 | 5 | RST8 service p.3 (mounter, "RAMDISKO") | rst8service_fe | `ROM_RST83`=9 |
| 23 | 5 | RST8 service p.2 ("SD card lost") | " | `ROM_RST82`=8 |
| 24 | 6 | RST8 service p.1 (NMI "MAGIC Service") | " | `ROM_RST81`=7 |
| 25 | 6 | RST8 service p.0 | " | `ROM_RST80`=6 |
| 26 | 6 | packed ERS main menu (`main_pack`) | " (`main_fe`) | `ROM_MAINMENU`=5 |
| 27 | 6 | FF | FF | `ROM_EMPTY`=4 |
| 28 | 7 | BASIC48 (128-compatible) | same | `ROM_BAS48`=3 |
| 29 | 7 | EVO-DOS "emu3d13" (" EVO-DOS Ver 0.60") | zxevo_fe: **NEO-DOS** (" NEO-DOS Ver 0.60"); _trd503: "* TR-DOS Ver 5.03 *" | `ROM_DOS`=2 |
| 30 | 7 | BASIC128 | same | `ROM_BAS128`=1 |
| 31 | 7 | ERS start/services page (`services.a80`) | `services_fe` | `ROM_ERS`=0 |

The "Slot" column follows the in-source comment table (`ROM/page0/source/services.a80:492-514`), which labels value pairs 0x81/0x83 (slot 0, top) through 0x9D/0x9F (slot 7, bottom).

The prebuilt `ROM/zxevo_fe.rom` has NEO-DOS in page 29, which is the output of `build_full.bat`. It differs from `tr5_03.rom` in 2340 bytes. `build_full.sh` would put stock `tr5_03.rom` there instead.

**Prebuilt ROM files:**

- `ROM/zxevo.rom`, `ROM/zxevo_fe.rom`, `ROM/zxevo_fe_trd503.rom` (each 524288 bytes).
- Components: `ROM/page1/tr5_03.rom`, `ROM/page3/{2006,basic48_128,basic48_128_std,basic48_orig}.rom`, `ROM/page2/128_std.rom`, `ROM/page0/glukpen.rom`, `ROM/profrom/{evoprofrom,profrom}.rom`, `ROM/ff_16k.rom`, `ROM/ff_64k.rom`.
- The second tree, `emulators/github/zx-evo/pentevo/rom/bin/`, is the **TS-Conf** branch. It holds `ts-bios*.rom` (64K) and an older `zxevo.rom` whose pages 0–3 contain ts-bios. `txt/how_to.txt` says: "Program ts-bios.rom to ROM pages 00-03". So TS-Conf's BIOS lives in the CUSTOM slot.

**Page 31 start state:**

- The reset vector page is 31. After reset the pager is off, and in that mode every window maps ROM page `8'hFF` (`FPGA/mem/atm_pager.v:114-118`), i.e. the last page (31).
- `services.a80:11-13` starts with `DI; JP INITPAGE0`.
- `INITPAGE0` (`services.a80:37-64`) writes `PAGES_CONF` (`services.a80:521-522`) to both maps:
  - For `#7FFD` bit4=1: win3=#FF, win2=#7D, win1=#7A, win0=**#83**. #83 selects ROM page 28, or page 29 when `dos=1`.
  - For `#7FFD` bit4=0: win3..0 = #FF, #7D, #7A, **#00**. #00 selects page 31.
  - The final byte #81 is the value used once init is finished (pages 30/31).
- The GLUK, PROFROM and CUSTOM alternatives are defined at `services.a80:524-534`.

#### 1.3 ROM page selection from the Z80

**ATM ROM/RAM pager, ports `#3FF7/#7FF7/#BFF7/#FFF7`** (windows 0–3, `ROM/ports_evo.a80:13-16`):

- Writes are accepted only in shadow mode (DOS active, or `#BF` bit0 set) with A8=1: `FPGA/z80/zports.v:853` defines `atmF7_wr_fclk = (loa==ATMF7) && a[8] && shadow`, and `zports.v:320` defines `shadow = dos || shadow_en_reg`.
- Data bits (`FPGA/mem/atm_pager.v:210-214`):
  - `pages <= ~{2'b11, zd[5:0]}`, so the ROM page is the inverted D5..0.
  - `ramnrom <= zd[6]`: 1 = RAM, 0 = ROM.
  - `dos_7ffd <= zd[7]`.
- When D7=1 on a ROM window, page bit0 is replaced by the live `dos` signal (`atm_pager.v:160-163`: `page <= {pages[7:1], dos}`). This lets one value select a BASIC48/TR-DOS pair.
- For RAM with D7=1, the low 3 bits (or 6 bits in the 1M mode) come from `#7FFD`.

**Two maps.** Every window has two register sets, selected by `pent1m_ROM`, which is `#7FFD` bit4 (`atm_pager.v:85, 142-173`). A 128K ROM switch therefore flips between the two programmed maps.

**Pentevo RAM ports `#37F7/#77F7/#B7F7/#F7F7`** (`ports_evo.a80:5-8`) take a full 8-bit page number (256 pages = 4MB) and force RAM: `atm_pager.v:216-219` (`pages <= ~zd; ramnrom <= 1`). So the physical RAM page is `0xFF - value`. For example, ROM constant `RAM_EVODOS=1` means physical page #FE, and `RAM_NMI=0` means #FF (`global_vars.a80:21-35`).

**Write protect ports `#3BF7/#7BF7/#BBF7/#FBF7`** (`ports_evo.a80:9-12`): D0 sets `wrdisable` (`atm_pager.v:196-199`).

**DOS (TR-DOS) signal:**

- It turns on when the Z80 fetches an opcode (M1) from `#3Dxx` in a ROM window whose map-1 entry has D7=1, while `#7FFD` bit4=1 (`atm_pager.v:246-248`).
- It turns off on any M1 from RAM (`atm_pager.v:251-256`; `FPGA/z80/zdos.v` holds the register).
- `#xx77` A9=0 forces DOS on and shadow on, and A8=0 turns the pager off (DOC, section "#xx77").

**Readback, `#xxBD` indices** (`ports_evo.a80:41-60`; `zports.v:221-248`, mux `zports.v:955-993`):

| Port | Meaning |
|---|---|
| `#00BD`..`#07BD` | Map-0 window 0..3 and map-1 window 0..3 pages. Returns `~page`, so a ROM entry reads back as `0xC0 \| D5..0` (`FPGA/top.v` passes `.pages(~{rd_pages…})`). |
| `#08BD` | RAM/ROM bits |
| `#09BD` | DOS/7FFD bits |
| `#0ABD` | `#7FFD` |
| `#0BBD` | `#EFF7` |
| `#0CBD` | `xx77` state: `{~pen2, cpm_n, ~pen, dos, turbo, scr_mode}` |
| `#0DBD` | Palette |
| `#0EBD` | Font readback |
| `#0FBD` | Border |
| `#10BD` / `#11BD` | Breakpoint address low / high (read and write) |
| `#12BD` | Write-disable bits |
| `#13BD` | **fdd_mask** (read and write, trdemu builds only) |

**Port `#xxBF`** (read and write, always accessible; `zports.v:467`, write logic `zports.v:862-876`; ROM bit names in `global_vars.a80:124-129`):

| Bit | Meaning |
|---|---|
| 0 | Shadow ports on |
| 1 | **ROM (flash) write enable** (`FLASH_BF`) |
| 2 | Font RAM write |
| 3 | NMI generate (on a 1→0 transition) |
| 4 | Breakpoint enable |
| 5 | 444 palette |

`#xxBE` (write) ends NMI or trdemu mode (`zports.v:937`: `clr_nmi = (loa==ZXEVBE) && port_wr`).

#### 1.4 Flash programming from the Z80 (ERS "Fast update ROM" / "Update custom ROM")

1. The file browser loads a `.ROM` file into RAM starting at `RAM_FLASHER`: `LOAD_ROM` at `ROM/mainmenu/src/fat_boot.a80:925-931`. `RAM_FLASHER` = logical 61, i.e. physical #C2 downward (`global_vars.a80:35`).
2. `FLASHER` (`fat_boot.a80:1435-1512`):
   - Sets `#BF |= SHADOW|FLASH` (`PEC_ON M_SHADOW_BF+M_FLASH_BF`, `:1448`/`:1479`).
   - Unmaps ROM from window 0 by writing `#3FF7 ← #7F`.
   - A 64K file is written to slot 0 (the custom ROM, `E=0`). It uses a sector erase only on chips with ID #E220 or #A401, i.e. 64K-sector 29F040 parts (`:1453-1471`).
   - A 512K file triggers a chip erase followed by programming all 8 × 64K (`:1486-1500`).
   - At the end it clears `FLASH_BF` (`:1510`) and jumps through `OUT (#BC77),2` (`:1511-1515`).
3. The programming primitives are in `ROM/mainmenu/src/flasher.a80`. They use the AMD/JEDEC sequence:
   - `PGM_BYTE`: `AA→0x555, 55→0x2AA, A0→0x555, data` (`:3-34`).
   - `ERASE_BLK`: `…80…AA…55…30` (`:102-137`).
   - `erase_all_chip` and `rom_read_id` (F0/90).
   - `WRBYTE`/`RDBYTE` map a flash address EHL (512K) through window 3: `#FFF7 ← ~((E&7)<<2 | H>>6) & 0x3F` (`:192-228`). This confirms that flash offset = ROM page × 16K.
4. There is also an AVR-side flasher in `pentevo/test_n_service/trunk/avr/_flasher.asm`. Its comment UI at `:7-17` shows `.rom` files from SD (zxevo.rom 512K, 64K/16K pieces) being placed into slots by the AVR; the Z80 is not involved.

---

### 2. ERS (Evo Reset Service): versions, settings, reset and boot options

#### 2.1 Version display

The screen header (`ROM/mainmenu/src/menu_data.a80:482-498`) reads:

```
"EVO Reset Service v",VERSBIOS          ; VERSBIOS = "0.60.05 FE " or "0.60.05 " (ROM/version.a80:5-10)
"ZX-Evolution 4096 Kb "
"Baseconf: " VERS_CONF  (default text "NONE")
"AVR Boot: " VERS_BOOT  (default text "NONE")
```

`TXT_NONE` is at `menu_data.a80:721`.

These fields are filled by the tail of the setup-screen routine in `ROM/mainmenu/src/main.a80:774-780`:

- It runs only if `ERS_FLAGS.CLOCK_ON` is set, i.e. the clock was detected.
- `GET_VERS_EVO` is called with `L=0` for Baseconf and `L=1` for the AVR bootloader.

The ERS does not show a separate "AVR firmware" version. BaseConf *is* the AVR firmware `zxevo_fw.bin`, which contains the FPGA bitstream (DOC, introduction). So the "Baseconf" line is the AVR firmware plus FPGA version.

**`GET_VERS_EVO`** (`ROM/mainmenu/src/call_cmos.a80:475-546`):

1. Write L (0 or 1) to CMOS cell `0xF0` (`CMOS.READ_PS2`, `global_vars.a80:200-201`) through `RST8 _CMOS_RW,_WRITE_CMOS`.
2. `READCMOS` (`call_cmos.a80:451-466`):
   - `OUT (#EFF7),#80` enables Gluk clock access (bit7).
   - `OUT (#DFF7),H` sets the address.
   - `IN L,(#BFF7)` reads the data.
   - `OUT (#EFF7),0`.
   - Port names come from `ports_evo.a80:18-22`. In shadow mode the equivalents are `#DEF7`/`#BEF7`.
3. The byte read back is then checked:
   - **== the value written**: not supported (old FPGA, or not an Evo). Return, keeping "NONE" (`:478-480`).
   - **== #FF**: no clock. Return (`:481-482`).
   - Otherwise read 16 bytes from cells F0..FF (`:485-491`).
4. The 16 bytes are formatted as follows (the format matches DOC section 9.6.1):
   - Bytes 0..11 are the ASCII name, zero-padded.
   - Bytes 12..13 are packed as follows. Low byte (0xFC): bits 4..0 = day, bits 7..5 = month bits 2..0. High byte (0xFD): bit0 = month bit3, bits 6..1 = year−2000, bit7 = 1 for release.
   - Output string: `"<name> DD.MM.20YY"`. If bit15 = 0, `" beta"` is appended (`TXT_BETA`, `menu_data.a80:720`; unpacking at `call_cmos.a80:513-546`).
   - DOC example: `50 65 6E 74 31 6D 00 00 00 00 00 00 7B 14 3C B1` = "Pent1m", beta, 27.03.2010, CRC #B13C.
   - Bytes 14..15 are the CRC of `zxevo_fw.bin`.
5. Mode **3** written to F0 gives `modes_register` bits: VGA / tapeout / 60Hz. ERS uses it in `VIDEOMODE` (`call_cmos.a80:596-620`) to display TV/VGA and Hz. Mode **2** gives the PS/2 keycode FIFO (DOC section 9.6.2).

**FPGA suitability check.** `RST8 _VERSION` (`ROM/page5/source/rst8service.a80:261-281`) returns:

- `BC:DE = VERSBIN = 0x0000:6005` (`version.a80:5`).
- `A` bit0 = 1 in DOS_FE builds, meaning `zxevo_fw.bin` from `standalone_base_trdemu` is required.
- `A` bit7 = `INCORRECTFPGA`. It is set if writing `%1010` to `#13BD` does not read back the same value.

`S_FACE` (`main.a80:600-612`) prints "Incorrect FPGA zxevo_fw.bin" (`menu_data.a80:538`) when that combination is found.

#### 2.2 CMOS settings (Gluk NVRAM emulated by the AVR; ERS cells at top below 0xF0)

Defined in `ROM/global_vars.a80:172-285`. The ERS reads these in DOS mode through `#DEF7`/`#BEF7` (`services.a80:363-372`).

| Cell | Name | Bits |
|---|---|---|
| 0x00–0x0D | RTC (sec…year, regs A–D) | standard MC146818 layout |
| 0xEF/0xEE | CRCHIGH/CRCLOW | CRC over CMOS settings |
| **0xED** `BYTE_00` | | b7 TURBO14 enable; b6 EMUL_TAPE (tape-load emulation via breakpoint); b5 PRINTER_AY; b4 RELOAD_FONT; b3 TYPE_FONT; b2 AUTO_TAPE; **b1..0 reset target**: 0=EVO SERVICE, 1=GLUK, 2=PROFROM, 3=CUSTOM ROM (`:235-240`) |
| **0xEC** `BYTE_01` | | b7 TURBO 3.5/7; b6 SD NeoGS on/off; b5 AUTOMOUNT; b4 CLOCK_VIEW; b3 SOUNDKEYS; b2 REZIDENT (Honey Commander); b1..0 memory model 0=1MB, 1=48K, 2=128K (`:251-255`) |
| **0xEB** `VIRT_REAL_DRIVE` | | b7 access ZC SD; b6 HDD master; b5 HDD slave; **b3..2 real drive number; b1..0 virtual (RAM-disk) drive number** (`:260-266`) |
| 0xEA `HDD_TIMEOUT` | | b7..4 screensaver timeout; b3..0 HDD detect delay |
| 0xE9 `BYTE_02` | | b7 KILL_REZIDENT; b6 AUTOBOOT; b1..0 BOOTDEVICE 0=FDD, 1=HDD, 2=SD (`:273-283`) |
| 0xE8 `BYTE_03` | | (used for kill-resident flag, `main.a80:768-771`) |
| 0xF0–0xFF | | AVR extension window (version, PS/2 FIFO, modes, EEPROM) |

**ERS menus** (`menu_data.a80`):

- **Main menu** (`:206-218`): Z.TR-DOS boot, F.File browse, T.Tape load, B.HDD boot, D.CD boot, 5.SDcard boot, X.Perfect Cmd, S.TR-DOS, I.48k basic, U.128k basic, C.Setup, R.Service.
- **Status lines** (`:517-533`): 1-4 TR-DOS Drive, Y.Virtual Drive, M.Memory Lock, W.CPU frequency, L.Emu tape load, G.RESET=>, E.Reload FONT, J.Type FONT, N.Automount TRD.
- **Setup** (`:158-176`): Sound on keys, Access SD NeoGS, DRV AY printer, Resident Honey Cmd, Kill resident, Autostart tape, HDD delay, Screensaver.
- **Services** (`:97-111`): Reset NeoGS, Reset CMOS, **Format ramdisk 640k**, Basic 48/128 standard, Edit CMOS, Test PC keyboard, Device detector, ATM CP/M, IS-DOS boot, **Fast update ROM**, **Update custom ROM**, Dismount image.
- **Help** (`:453-471`): keys held during reset (Space=TRDOS, CS=Basic128, SS=Basic48, D=demo, C=ColorTable, S=old CMOS SetUp, 0=return to ERS).
- The legacy CMOS setup (`cmosset`) is unpacked from `services.a80:424` (`BONUADR`).

#### 2.3 Reset flow (`ROM/page0/source/services.a80`)

1. `INITPAGE0` programs both maps (see 1.2), enables shadow, and calls page `ROM_RST81` to set up RST8/NMI (`:37-72`). `INSTALL_NMIRST` (`ROM/page5/source/addon1.a80:128-160`) patches the NMI jump in RAM page #FF and **clears fdd_mask (`#13BD ← 0`)**.
2. Keys held at reset:
   - **"0"** resets the resident and forces the main config (`:77-81`).
   - **"6"** enters the STS debugger through an NMI generated by `#BF` bit3 (`:85-100`).
3. Otherwise the reset target comes from CMOS `0xED` b1..0 (`:101-117`):
   - GLUK or CUSTOM: jump via `JP #3D2D` with `#FF77 ← #A3` to the alternate 64K slot (`:174-184`).
   - PROFROM: `LDIR4PROFROM` (`:172, 477-490`).
   - EVO service (`:186-216`): in DOS_FE builds the VG93 emulator is **unpacked into RAM page #FE** (`CALL UNP_DOS_FE+CPU3`, `:191-198`). In non-FE builds, `ROM_ADD_DOS` (page 21, EVO-DOS virt) is copied into RAM #FE (`:199-209`).
4. Keys at this stage:
   - **CS** → BASIC128 (A=0 to `#7FFD`).
   - **SS** → BASIC48 (`#7FFD`=0x30).
   - **SPACE** → TR-DOS, entered with `#7FFD`=#10 and a return address of `#3D2F` (`:235-253`).
   - **D** → demo (`:255-258`).
   - More than one key → CMOS setup / keyboard test (`:230, 281-289`).
   - No key → ERS main menu: `ROM_MAINMENU` page 26 unpacked to #6000 (`:259-269`).
5. The final hand-off code is copied to #5C80 (`RAM_CODE`, `:326-360`). It sets:
   - `#EFF7` turbo/memory from CMOS `0xEC`;
   - `#7FFD`;
   - window-0 ROM `#3FF7 ← IXH`;
   - `#FF77` turbo/mode;
   - clears shadow.

**Boot devices from the ERS menu:**

- FDD: TR-DOS boot.
- HDD: see section 4.
- SD: `SD_BOOT.$C` in the FAT root, loaded as a hobeta code block (`ROM/mainmenu/src/sdcardboot.a80:5-47`).
- CD: `AUTORUN.ZX`.
- IS-DOS boot.
- ATM CP/M: `conf_up.a80:7-36` maps ROM page `ROM_ATMCPM`, sets `#0177 ← 6`, **clears `#13BD`**, then `JP #000C`.

---

### 3. Virtual TR-DOS (RAM disk and mounted TRD) — mechanisms and flow

There are two implementations, one per ROM build.

#### 3.1 DOS_FE build (zxevo_fe.rom, the current one): FPGA "trdemu" trap to RAM page #FE

**Hardware** (`FPGA/z80/zdos.v:61-98`, `FPGA/mem/atm_pager.v:124-131`, `FPGA/z80/zports.v:519-525, 797-799, 937`):

- **Trigger:** `trdemu_on = vg_rdwr && fdd_mask[vg_a] && dos && romnram && !atm_pen2`.
  - `vg_rdwr` is an I/O access to `#1F/#3F/#5F/#7F/#FF` in shadow mode.
  - `vg_a` is the drive number last written to `#FF`.
  - The access must come while executing TR-DOS **ROM** in window 0.
- **Real VG93 suppressed:** `vg_cs_n = fdd_mask[vg_a] | …` (`zports.v:797-799`), so the real FDC chip is not selected for masked drives.
- **Page switch:** right after that IN/OUT, `in_trdemu=1` and window 0 is forced to **RAM page #FE** (`page <= {7'h7F, in_nmi}`). Page #FF is used for NMI instead.
- **Write protection:** `trdemu_wr_disable` blocks writes to #FE until the next M1 (`zdos.v:91-98`).
- **Exit:** `OUT (#BE),A` clears `in_trdemu` immediately (unlike the NMI exit, which waits 2 M1 cycles).
- **fdd_mask** is written with `OUT (#13BD),A` (low nibble = drives A–D) and read back from `#13BD`.
- **Status readback:** reading `#FF` returns INTRQ/DRQ plus the last value written in bits 4..0 (DOC, section "Программный эмулятор ВГ93").
- DOC describes the whole design: "…the next instruction is fetched from RAM page #FE… the code there emulates the controller transparently and returns to ROM by executing `OUT (#BE),A`, placed so that TR-DOS ROM execution continues directly after the instruction that caused the entry."

**Software** (`ROM/page1/dos_fe/dos_fe.a80`):

This image is assembled at ORG 0 as a 16K image of RAM page #FE. It is unpacked there at reset (`services.a80:191-198`). It is tied to the exact addresses of the DOS ROM in page 29 (TR-DOS 5.03 derivatives: tr5_03 / NEO-DOS / trdos503). I verified in `ROM/page1/tr5_03.rom` that each table address holds the listed IN/OUT, e.g. `#02BE`=`D3 FF`, `#1FDD`=`DB 1F`, `#3FEC`=`ED A2` (INI), `#3FF3`=`ED 78`.

- **Mirror stubs.** For each trapped ROM address X (the IN/OUT instruction itself), the page #FE image holds at the same address X:
  `JP_EMU X` = `OUT (#BE),A` (2 bytes, same length as the trapped opcode) followed by `JP ADR_X` (`dos_fe.a80:25-30`; instances at `:655-715, 1541-1616`).
- **`TABLE_VIRT`** (`:63-138`): 75 entries of `DW X, handler`. The addresses are listed with comments such as `#1E3A OUT (#3F),A`, `#1FDD IN A,(#1F)`, `#3FD7 IN A,(#FF)` (read sector), `#3FBC/#3FCA/#3FD1` (write sector, OUTI), `#3FEC INI`, `#3FF0 OUT (C),A`, `#3FF3 IN A,(C)`, and `#2A71/#2A77` (MAGIC).
- **Stubs `EMU_JUMP X`** (`:39-45`, `:140-393`): `LD (OLD_AF+1),A; LD A,index; JP WORKER`.

**Flow (trap → handler → return):**

1. TR-DOS ROM executes, for example, `IN A,(#1F)` at `#1FDD` on a drive whose bit is set in fdd_mask. The real VG93 is not selected. The FPGA maps RAM #FE into `#0000-#3FFF`.
2. The next fetch at `X+2` comes from page #FE and is `JP ADR_X`. `ADR_X` saves A and jumps to `WORKER` with the table index.
3. `WORKER` (`:492-530`):
   - saves SP and switches to its own stack `#0DFF`;
   - saves F, and saves the IFF state via `LD A,I`/parity;
   - saves `#BF` and then sets bit0 (shadow on);
   - sets `I=#0D`, so IM2 vectors to `INT_BREAK` at `#0DFF`;
   - saves HL/DE/BC;
   - looks up `TABLE_VIRT[index]`: return address → `ADR_EXIT`, then handler;
   - pushes `EXIT_PAGE_FE` and jumps to the handler.
4. The handler emulates the WD1793 register file in RAM variables (`WR_1F`, `RD_1F`, `PORT_3F/5F/7F`, `WR_FF`, `RD_FF`; `ROM/evodos_vars.a80:12-18`):
   - **`OUT_1F`** (`:719-816`) dispatches on command bits 7..4: restore (`3F=0`), seek (`3F←7F`, and returns "not ready" #80 if no disk), step/in/out (keeps the direction by self-modifying `INC A`/`DEC A`), read/write (no-op here), and force-interrupt. It always sets `RD_FF=#BF` (INTRQ).
   - **`IN_1F`** (`:904-955`) synthesises status. It toggles the index pulse bit 1 and track-0 bit 2, and returns RNF (#10) for track ≥ 80 during read/write.
   - **`IN_3F/5F/7F/FF`** return the stored values in `OLD_AF+1` (`:958-964`).
   - **`OUT_FF`** writes the value to the real `#FF`, with fdd_mask temporarily cleared (`WR_C_D`, `:830-863`), so drive select, side and density reach the hardware.
   - `OUT (C),A` / `IN H,(C)` with non-FDC ports: `OUT (C),A` to the ATM/Pentevo pagers is shadowed into saved window state (`WRCA3`, `:866-885`).
5. **Sector data is not transferred byte by byte through DRQ.** When TR-DOS reaches its DRQ loop (`#3FD7`/`#3FE5` read, `#3FBC`/`#3FCA`/`#3FD1` write), the handler moves the whole sector in one step. Flow: `READ_SECTOR`/`WRITE_SECTOR` (`:1036-1119`) → `W_WR_RD_SECT` (`:1156-1335`).
   - If interrupts were enabled, it first `HALT`s to let one INT happen, using `FLAG_RW_BREAK` and `INT_BREAK` (`:397-470`).
   - It then LDIRs 256 bytes (or 128 bytes) between the RAM disk and the caller's HL. It handles the case where the buffer crosses a page boundary, and destinations `#3Fxx` or `#FFxx`.
   - It sets `OLD_AF = #8090` (fake success status in A) and `ADR_EXIT = #2A53`.
   - `#2A53` in ROM is `OUT (C),A; RET`, so the mirror `OUT (#BE),A` at `#2A53` returns to ROM `#2A55` = `RET`. This leaves the whole TR-DOS sector routine.
   - `C0` (read address) is handled by writing a fake 6-byte ID and `B-=6` (`:1043-1093`).
6. **Return path.** The handler `RET`s into `EXIT_PAGE_FE` (`:473-490`):
   - restores `#BF`, I, AF, HL, DE, BC, SP;
   - `JP ADR_EXIT` (= X, in page #FE);
   - at X it executes `OUT (#BE),A`, which unmaps #FE immediately;
   - the next fetch at `X+2` comes from TR-DOS ROM, i.e. the instruction after the trapped IN/OUT, with emulated register values.
   - Some entries use alternate exits such as `EXIT_0x2F59`/`EXIT_0x3EF5` (`:704-712, 1566-1569`).

**Which image serves a request.** `W_WR_RD_SECT` checks drive bit `(WR_FF & 3)` against `COPY_VIRT_BITS` (`:1156-1165`):

- **Mounted file image** → `MOUNT_RW` (`:533-566`).
  - Pushes return `#2A77`, pushes `MNT_RW+#4000`, maps #FE also in window 1 (`#77F7←RAM_EVODOS`), and exits through `#2A53`/`RET`.
  - `MNT_RW` runs in RAM window 1. It builds `D = track×2+side` and `E = sector`, and calls `RST 8 _MOUNTER,_RDWR_MOUNT` (`:568-581`).
  - It then does `JP #3D2F` to re-enter the DOS ROM. `RET` goes to `#2A77`, which is an `IN A,(#1F)` in ROM, so it traps again.
  - The #FE mirror at `#2A79` does `JP RET_MNT_RW` (`:675-677`), which copies the sector buffer to the caller (`:584-630`).
- **RAM disk** → `FIND_SECTOR` / copy from RAM (`:1121-1335`).

**RAM disk layout** (`CREATE_TRDTABL`, `dos_fe.a80:1352-1404`, same as `mounter.a80:ILD_IMG3`):

- **Descriptor page** `RAM_RAMDISK` = logical 10 = physical **#F5**.
  - Row `#40tt` = in-page 4K offset of track tt.
  - Row `#41tt` = page offset.
  - Rows `#42+2i` / `#43+2i` = sector number and size code for sector slot i (i=0..15).
  - Up to `#A0` = 160 tracks (80 cylinders × 2 sides).
  - Marker `'D'` at `#7EFF` and `'R'` at `#7FFF`, i.e. offsets `#3EFF`/`#3FFF` (checked by `DISK_NONE`, `:1488-1510`).
- **Data** starts at `RAM_DATARAMD` = logical 11 = physical **#F4** and goes downward.
  - 4 tracks × 4K per 16K page; 640K for a standard TRD.
  - 50 pages are reserved (`global_vars.a80:32-33`: `RAM_DATARAMD, 800/16`).
- A fresh disk gets the TR-DOS sector-9 info at offset `#08E1`: label "RAMDISKO", 2544 free sectors, type #16 (`DSKINFO`, `dos_fe.a80:636-651`).
- **fdd_mask source.** `GET_VIRT_BITS`/`WR_VIRT_BITS` (`ROM/page5/source/fat/mounter.a80:1227-1293`) combine two sources: bits 3..0 are the mounted images for drives A–D, and bits 7..4 are one RAM-disk bit taken from CMOS `0xEB` b1..0. The OR of both nibbles is stored in `B_PORT_VIRT` (`ROM/page5/source/nmi_service.a80:160`).
  - In DOS_FE builds this value is written to `#13BD` when leaving the NMI and RST8 services (`nmi_service.a80:281-285, 483-487`).
  - `#13BD` is saved and zeroed on entry (`:208-212, 382-386`), so the ERS itself sees the real FDC.
  - The same bits are copied into `COPY_VIRT_BITS` in page #FE.

#### 3.2 Non-FE build (zxevo.rom): patched EVO-DOS, no hardware trap

`page1/evo-dos` is TR-DOS patched so that FDC I/O becomes **`RST #30`** calls:

- `RST30_WORK` (`ROM/page1/evo-dos/virtual.a80:100-117`) maps RAM page #FE, which holds a copy of `evo-dos_virt` (`services.a80:199-209`), into window 0 with shadow on. It then dispatches on function numbers `_OUT_1F … _INI, _WR_RD_SECT, _CMP_RAMDISK` (`:22-67`).
- Two DOS ROMs are built (`ROM/page1/evo-dos/build.sh`):
  - `EMU3D2F=1` → `evo-dos_virt` (page 21, `ROM_ADD_DOS`), full intercept.
  - `EMU3D2F=0` → `evo-dos_emu3d13` (page 29), which emulates only via the `#3D13` entry.
- `NUM_ALT_PAGE` / `DOS_EMUL` / `DOS_NOEMUL` are at `virtual.a80:84-94`.

#### 3.3 Image formats, storage devices and file systems

**Mount from file** ("Mount A:–D:"):

- Only **TRD** (`FILE_EXT DZ "TRD"`, `mounter.a80:665`). `OPEN_MOUNT` (`:497-663`) precomputes a cluster list into `MOUNT_CLS`.
- `RDWR_MOUNT` (`:671-805`) computes the file sector as `track*16 + sector-1` in 256-byte units, then reads the matching 512-byte FS sector.
- **Autoload:** `IMAGE.MNT` text file (`:829`) and `FIND_MOUNTED`, controlled by CMOS `0xEC` b5.

**Load into RAM disk** (`LOADIMAGE`, `mounter.a80:37-66`; ERS menu "TRD to:/SCL to: Ramdisk A..D", `menu_data.a80:275-312`):

- **TRD**: raw copy.
- **SCL**: `LOAD_SCL` `:167` converts it to TRD layout.
- **FDI**: `LOAD_FDI` `:310` builds per-track sector tables with real sector numbers and sizes. This is why the descriptor page stores numbers and sizes.
- **TAP**: goes to `RAM_TAPE` for tape emulation. It is not a disk.

**Devices** (`mounter.a80:588-597`; drivers in `ROM/page5/source/fat/`):

| Device | Driver | Ports |
|---|---|---|
| Z-Controller SD | `COMSDZ`, `z_sd_drv.a80` | `#77` CS / `#57` data (`ROM/sdcomand.a80:4-5`) |
| NeoGS SD | `COMSDG`, `ngs_sd_drv.a80` | |
| Nemo IDE HDD | `COMHDDN`, `nemo_drv.a80` | `#10…#F0`, `#C8` |
| USB SL811 | `usb_drv.a80` | |

- In shadow mode the SD CS is at `#xx57` with A15=1 (DOC, SD section).
- File systems are FAT12/16/32 with MBR partition types `01`, `04/06/0E`, `0B/0C`, plus raw TR-DOS (`define.a80:150-157`). Code: `read_fat.a80`, `dev_drv.a80`.

---

### 4. IDE HDD and CD-ROM

**Port sets** (`ROM/ports_ide.a80`):

- **Nemo** (the Evo native one): `#10` data low, `#11` data high, `#30` err, `#50` count, `#70` sec, `#90` cyl-lo, `#B0` cyl-hi, `#D0` head, `#F0` status/cmd, `#C8` control (`:25-36`).
- Also defined for other hardware: DivIDE (`#A3…#BF`), Profi (`#xxCB`/`#xxEB`), SMUC (`#F8BE…#FFBE`), ATM Turbo2+ (`#FExF`) (`:12-93`).
- The FPGA accepts only `#10, #11, #30, #50, #70, #90, #B0, #D0, #F0, #C8`, fully decoded and also in shadow mode. INIR/OTIR on `#10` transfers whole 16-bit words (DOC section 9.8; `zports.v:179-188`).
- ATA commands (`ROM/cmd_cdhdd.a80:4-12`): IDENTIFY #EC, READ #20/#24, WRITE #30/#34, SET MAX #F9/#37, INIT PARAMS #91. The CD/ATAPI section starts at `:14`.

**HDD boot** (`HDD_BOOT`, `ROM/mainmenu/src/menu_execute.a80:239-313`):

1. `RST8 _COM_DEV,_COMHDDN,_DEV_INIT`.
2. Load **LBA 2, #30 sectors (24K) to #6000** with `_DEV_READ`, then jump to #6000 (`HDDBOOT2`, `:259-265`).
3. An inline LBA variant writes `#D0←E0` (master, LBA) and `#F0←#20`, and reads the words `#10`/`#11` (`:267-310`).
4. The legacy CHS variant `HDDBOOT`/`HDDREAD` (`ROM/mainmenu/src/hdd_cd_boot.a80:22-84`) reads C0/H0/S3, #30 sectors, to #6000 through `#E800`.

**CD boot** (`CDBOOT`, `menu_execute.a80:318-324` → `CDBOOTGO`, `hdd_cd_boot.a80:118-360`):

1. ATAPI device on **slave (#B0)** (`device EQU #B0`, `:137`).
2. Reset #08, IDENTIFY #EC, then check the ATAPI signature `#EB14` in the byte-count registers (`:240-259`).
3. Send packets and `READTOC`. Load the session start (ISO9660 volume descriptors, `#8800` bytes) and the root directory, then search for **`AUTORUN.ZX`** (`:572-574`).
4. Load the file to #6000 and enter it with:
   - `A=#B0` (device);
   - `B`=computer type (0 = Pentagon), `C`=IDE type (0 = Nemo);
   - `D`=language (1 = RU), `E`=COVOX port `#FB`;
   - `HL`=video modes (`:122-142, 331-337`).

**SD boot:**

- `SD_BOOT.$C` from FAT root on Z-controller SD (`sdcardboot.a80:30-47`).
- The FAT boot micro-loader `ROM/fat_boot/source/micro_boot_fat.a80` is embedded packed as `MICRO_BOOT` (`ROM/page5/source/addon2.a80:63`). It picks a reader by device number: `_SD_SDZ` → `READ_ZSD`, `_SD_SDG` → `READ_NEOGS`, `_HDD_NEMO_MASTER` → `RD_HDD_NEMO`, and optionally SMUC/DivIDE/Profi (`:25-47`). It chooses master or slave LBA bits as `#E0`/`#F0` (`:48-54`).
- Structures: `ROM/bootsecfat.a80` is the BPB layout (FAT12/16 at `:4-28`, FAT32 at `:30-44`). `ROM/mbr.a80:4-17` is the 16-byte partition entry used for MBR parsing by the device manager (`_DEVFIND`/`_KOL_VOL`/`_GET_FNDVOLUME`, `define.a80:79-89`).
- `hdd_detect.a80` provides "Device detector" and SD version decoding.

`mixdos*.a80` (`ROM/mixdos.a80`, `mixdos_func.a80`) are MSX-DOS-like BDOS definitions (jump table at #F000, CP/M function numbers). Nothing in the build includes them; I checked with grep.

---

### 5. NedoOS

**Target selection is compile-time. There is no runtime detection of BaseConf vs TS-Conf, and no TS-Conf kernel target.** The `syssets.asm` flags per build script:

| Script | Settings | Target |
|---|---|---|
| `NOS/mkevo.bat:2-10`, `NOS/kernel/build_kernel_evo.bat` | `atm=1`, `NEMOIDE=1`, `SYSDRV=12`, `PS2KBD=1`, `NGSSD`, output `sd_boot.$C` | ZX Evolution **BaseConf booted by ERS from SD** |
| `NOS/mkatm3.bat`, `mkatm3sd.bat` | `atm=3` | ATM3-style build on Evo BaseConf (TRD or SD) |
| `NOS/mkatm2.bat` | `atm=2` | ATM Turbo 2 |
| `NOS/mkpe26.bat` | `atm=2`, `KOE` | Pentagon |

Grepping kernel and SDK found no TS-Conf ports (`#xxAF`); "tsconf" appears only in a few games and kapps.

**What `atm==1` does with BaseConf and ERS:**

- **ERS detection and version.** `rst #08; db #4D` (`_VERSION`, `D=0`) returns DE. The kernel requires `DE ≥ #5812` (ERS 0.58.12) (`NOS/kernel/main.asm:191-199`). Otherwise `idle.asm:7-20` prints "You need update ERS to version 0.58.12 or newer! http://zxevo.ru/zxevo.rom".
- **Boot device.** Found via ERS RST8 device manager calls: `#50,#03` (`_GET_FNDVOLUME`), `#50,#02` (`_KOL_VOL`), `#50,#05` (`_SET_DEVICE`), `#50,#04,#02` (`_TO_DRV`/`_DEV_READ`). The kernel then reads the MBR at `+#1BE` to choose SYSDRV (`main.asm:200-262`).
- **Ports used:**

| Port | Use | Evidence |
|---|---|---|
| `#01BF`←1 | Shadow on | `main.asm:271-272`, `syskrnl.asm:470-471` |
| `#BF`←32 | Bit5 444 palette; also clears shadow | `main.asm:156-158` (atm==3), `:278-279`, `:757-760`, `syskrnl.asm:474-475` |
| `#BD77` / `#FF77` | `xx77`: video mode + shadow and palette on/off | `main.asm:276`, `syskrnl.asm:466-473` |
| `#EFF7` | `#80+#10` for CMOS/noturbo | `main.asm:267-269`; `readtime`, `main.asm:853-969` |
| `#DEF7` / `#BEF7` | AVR CMOS extension: address #F7 then write **2** = PS/2 keycode FIFO; overflow reset by writing 1 to cell #0C | `main.asm:287-293`, `NOS/kernel/ps2drv.asm:48-60`, `syskrnl.asm:1197`, `xt_drv.asm:48` |
| `#3FF7…#FFF7` | ROM pager; memport constants `#37F7/#77F7/#B7F7/#F7F7` (Pentevo RAM, `pagexor=#FF`) | `main.asm:25-45` |
| `IN A,(#04BD)` with A=4 | Reads map-1 window-0 ROM value to find the TR-DOS ROM page (`sys_pgdos`), `AND #BF` | `main.asm:307-313` |
| `#FADF/#FBDF/#FFDF` | Kempston mouse | `syskrnl.asm:458-463` |
| `#1F` | Kempston joystick | `syskrnl.asm:467` |
| `#57` / `#77` | Z-controller SD | `NOS/kernel/fatfsdrv.asm:546-600, 880`; `SD_INIT` used for `atm==3 \|\| atm==1`, `:106-107` |
| Nemo IDE `#10…#F0/#C8` | Hard disk | `main.asm:59-72` |
| `#FD` | `#7FFD` short decode | `fd_system` at `main.asm:120, 444-445` |

- **TR-DOS use.** Plain ROM calls: `JP #3D2F` / `JP #3D13` (`main.asm:469-475, 523, 723, 763`) and the TR-DOS FS module (`trdosio.asm`).
- **Nothing in NedoOS writes `#13BD` (fdd_mask), `#xxBE`, or `#BF` bits 1/3/4.** A grep for `13bd` finds nothing. NedoOS therefore does not use virtual TR-DOS or trdemu itself. When it calls TR-DOS, any fdd_mask left by the ERS still applies, because the trap is transparent to the caller.

---

### Emulator-relevant takeaways (short)

1. **Reset start.** Reset boots from ROM page 31, with every window mapped to ROM `#FF` while the pager is off.
2. **ATM ROM value.** ROM page = `~D5..0`; D6=1 means RAM; D7 substitutes `dos` into page bit0.
3. **Two maps.** They are selected by `#7FFD` bit4.
4. **RAM pages.** Pentevo `x7F7` port: physical page = `~value`.
5. **trdemu is required for zxevo_fe.rom.** It needs:
   - `#13BD` read/write;
   - the trap on FDC IN/OUT from DOS ROM in window 0 when `fdd_mask[#FF&3]` is set;
   - window 0 remapped to RAM #FE from the next fetch;
   - VG93 chip-select suppressed;
   - `OUT (#BE)` unmapping immediately (versus 2 M1 later for NMI);
   - `#FF` readback carrying the last written low 5 bits;
   - writes to #FE blocked until the next M1.

   Without it, ERS shows "Incorrect FPGA zxevo_fw.bin", and the RAM-disk and mount features fail.
6. **CMOS version window.** `#DFF7`/`#BFF7` (or `#DEF7`/`#BEF7` in shadow mode), cells F0–FF: write 0 (BaseConf) or 1 (bootloader) to get the 16-byte record; 2 = PS/2 FIFO; 3 = modes. If a read returns the value just written, ERS treats the feature as unsupported and shows "NONE".
7. **Flash writes.** `#BF` bit1 enables them, and ERS uses the AMD 29F040 command set (IDs #E220 / #A401).


---

## D. zx-evo-docs (`D = emulators/github/zx-evo-docs`)

| Path | Content | Relevance |
|---|---|---|
| `D/Baseconf/zxevo_base_configuration.pdf` (49 p., "версия от 27.10.2014") | Official BaseConf user manual — **legacy** port map (#xxBE readback, #00BD/#01BD breakpoint, #2F/#4F/#6F/#8F RAM-disk ports, #FF read = last 6 bits). Contains all tables in §A (memory ports p.10-13, speed p.14, video p.14-20, NMI/#BE p.22-25, I/O ports p.26-40, Gluk p.28-33, SD p.33, IDE p.34-36, FDC p.37-38, RS-232 p.38-41, port summary p.42-45, history p.46-49). | Legacy. The 2020 successor is `P/fpga/base_trdemu/trunk/doc/zxevo_trdemu.odt` (same structure, #xxBD + trdemu section). |
| `D/GluExt/version_ext.md` | Gluk extension protocol: `#EFF7`.7, `#DFF7`/`#BFF7`; write `0x0C`=0 to disable EEPROM mode; EXTSW at `0xF0` (any `0xF0..0xFF` in BaseConf): `00` CONF_VERSION, `01` BOOTLOADER_VERSION, `02` PS2 keyboard log, `03` RDCFG (modes register), `0E` CONFIG (TS-Conf config interface), `10` SPIFL (SPI flash, TS-Conf). Recommends resetting EXTSW to 0 after use. | Version reporting (see §B) |
| `D/revC/zxevo_user_manual.pdf` (2012) / `D/revb/ZXEvo_user_manual.pdf` | Board manual. AVR = bootloader (fixed) + configuration (zxevo_fw.bin); configurations TEST&SERVICE and BASECONF; ROM image `zxevo.rom` for the 29F040 whose base is **EVO RESET SERVICE**, flashable from TEST&SERVICE or from ERS itself (p.21-22). | ROM/firmware context |
| `D/ZXEvo_firmware_update.pdf` (2012) | Bootloader update: `ZXEVO_FW.BIN` in SD root (FAT12/16/32), hold SoftReset at power-on; beep codes 1..5 (no card / read error / no FS / no file / corrupt); or XMODEM-CRC over RS-232 115200 8N2. | AVR bootloader |
| `D/ZXEvo_rescue_flasher.pdf` | Rescue ROM flasher | — |
| `D/ToDo/evonmi.txt` (CP866) | Early design note for NMI + #BE readback (the origin of §A.5/A.7). | historical |
| `D/ZX/nemo-divide.txt` | Nemo-IDE "divide" extension description (matches §A.9). | IDE |
| `D/ZX/zx-ports-full-table.txt` | Black_Cat generic ZX port table. | general |
| `D/ZX/ZC.pdf` | Z-Controller (SD #57/#77) | SD |
| `D/ATM/atm2_arch.pdf` | ATM Turbo 2 architecture (origin of #77/#xFF7/#x7F7). | memory model |
| `D/TSconf/*` | TS-Conf (not BaseConf). | — |

Key doc-vs-RTL discrepancies (RTL wins):
- Doc says `#F6` "fully duplicates #FE"; RTL: `#F6` writes do **not** touch beeper/tape (`T/z80/zports.v:944`).
- Doc (2020) `#BF` bits 7..5 undefined; RTL has bit 5 = 444 palette.
- Doc (2020) `#FF` read "bits 4..0 = last written"; RTL returns bit5=1 and reconstructs bits 4..0 from latched VG signals (equivalent content).
- `#FC` quirks (7FFD+border) undocumented.
