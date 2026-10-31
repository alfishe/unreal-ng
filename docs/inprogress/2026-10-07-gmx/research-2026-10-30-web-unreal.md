# Scorpion ZS 256 + GMX in the web build of Unreal Speccy (zxby.org) - reverse-engineering notes

Input for the unreal-ng implementation spec. Sources (all untrusted downloads, analysed statically only):

- `unreal.wasm` (from <https://www.zxby.org/unreal/unreal/unreal.zip>, build 20261030181446) (707068 bytes, WASI, asyncify). No `name` section, but 171 exports name the `web_*` entry
  points. Disassembled with wabt 1.0.42 `wasm2wat` (no `wasm-decompile` in that bottle) and a crude stack folder
  ; data segments read.
- `data/rom/gmx644.rom` (512 KB flash). Split into 16 KB pages, disassembled with z88dk-dis; the Loader's ZX0 stream
  unpacked (port of the reference dzx0, v2 format).
- <https://www.zxby.org/unreal/js/app.js> and the page itself.

Notation: `wasm f1170 @0x0564ad` = function index 1170, code-section file offset of its body. Linear-memory
addresses are given in hex (`[0x1090c8]`); decimal in the WAT. `ROM 0x0c0b5` = file offset in gmx644.rom, `pgN` =
16 KB page N (offset N*0x4000), `blkN` = 64 KB flash block N.

**Date:** 2026-10-30 - part of [README.md](README.md). The helper scripts (function slicer, stack folder, xref,
data-segment reader, port scan, ZX0 unpacker) were throwaway and are not kept; every fact cites its wasm function or
ROM offset, so it can be re-derived from the two files.

---

## 1. Overall structure: stock Unreal vs added for GMX

Stock Unreal (sources: the local clone of `unreal-speccy`) already has `MM_PROFSCORP` with `PROFROM` (64/128/256 KB,
`comp.p7EFD`, 64 KB block = `128 / SOS / SYS / DOS` 16 KB slots, PROF-ROM "switch by read" at #0100/#0104/#0108/#010C
of the SYS ROM). The web build keeps all of that and adds a `web/web_gmx.cpp` layer that is active only when

```
conf.mem_model == 2 (MM_PROFSCORP)  &&  gmx_enabled ([0x11f4ca] != 0)
```

(wasm f1168 @0x056337 is exactly this predicate; enum order confirmed: 0 = Pentagon, 1 = Scorpion, 2 = ProfScorp,
from the `br_table` in set_banks f93.) `gmx_enabled` comes from ini `[MISC] GMX=1` (web_init f1325, string "GMX" at
mem 0x1968 read from section "MISC" at 0x1fdf).

Added by the GMX layer (all new code is in f1168..f1175 plus hooks in out/in/set_banks/reset/render/turbo):

| Area | Stock behaviour kept | Added for GMX |
|------|---------------------|---------------|
| Flash | `load_rom(PROFROM)` | must be exactly 512 KB, else fatal "invalid GMX flash size (512 KB)" (f1325, string 0x25c8); `profrom_mask = 7` |
| ROM block select | 7EFD bits 5-4 (stock) | 7EFD bits 6-4 (8 blocks), bit 7 = turbo, bit 3 = GMX video |
| RAM | Scorpion 1FFD bits 4,6,7 | 2048 KB: page = DFFD[2:0]:1FFD[4]:7FFD[2:0]; port 78FD = page at #8000 |
| Ports | 1FFD, 7FFD (Scorpion mask), stock 7EFD | exact-match 78FD, 7AFD, 7CFD, 7EFD, DFFD, plus "port #xx00" control register |
| Video | ZX screen | 640x200 linear, pages #39/#79 (#3B/#7B), hardware scroll |
| Reset | stock reset | boot block chosen by "Loader V2" signature, sticky block bit, boot-code serial readback |
| Turbo | Scorpion: IN-port trick | 7EFD bit 7 = x2 |
| Writes to flash | go to the TRASH page | same - **no flash programming is emulated** |

### Linear-memory map recovered (needed to read the code)

| Address | Meaning | Evidence |
|---------|---------|----------|
| `0x176000` | `RAM_BASE_M` (page n = 0x176000 + n*0x4000) | set_banks f93 |
| `0x582000` | `ROM_BASE_M` = RAM_BASE + 0x40C000; flash loaded here | f1325 `f29(path, 0x582000, 32)` |
| `0x57e000` | TRASH page (bankw for any ROM window) | f93 tail: `if bankw >= 0x582000 -> 0x57e000` |
| `0xb02000..0xb0201c` | `bankr[0..3]`, `bankw[0..3]` | f93 |
| `0x175cd8 / dc / e0 / e4` | `base_sos_rom / base_dos_rom / base_128_rom / base_sys_rom` (vars.cpp order) | f93 bank0 selection |
| `0x1090c0..` | `comp`: p7FFD c0, pFE c1, pEFF7 c2, pXXXX c3, pDFFD c4, pFDFD c5, p1FFD c6, pFF77 c7, **p7EFD c8**, p00 c9, p80FD ca | matches stock `struct COMPUTER` layout; pFE written by port FE in f91 |
| `0x109112` | `comp.flags` (CF_TRDOS=2, CF_PROFROM=0x80, ...) | f93 |
| `0x115e9d` | `comp.profrom_bank` (= flash block 0..7) | f1170, f94 |
| `0x14f540` | `temp.profrom_mask` (7 for GMX) | f1325 |
| `0x14f099` | `temp.ram_mask` = (RAMSize-1)>>4 (127 for 2048 KB) | f1325 |
| `0x116f30` | `conf.mem_model`; `0x116f34` = `conf.ramsize` (KB) | f1168, f1173 |
| `0x108bad` | Scorpion turbo flag (x2) | f1341 |
| `0x118e1f8` | SD card object (web_sd_mount f1406); `0x118e458/45a` Z-Controller state | f157, f963 |
| **`0x1341590..0x1341596`** | **GMX register block** (see below) | f1169..f1175 |

GMX register block (`gmx` struct, base 0x1341590 = 20190608):

| Off | Name used here | Written by | Meaning |
|-----|----------------|-----------|---------|
| +0 | `r78FD` | OUT #78FD (full byte) | page mapped at #8000-#BFFF (XOR 2, 7 bits) |
| +1 | `r7AFD` | OUT #7AFD & #F0 | scroll, low byte |
| +2 | `r7CFD` | OUT #7CFD & #3F | scroll, high byte |
| +3 | `r00` | OUT to any port with A7..A0 = 0 | control register (lock / sticky block / flags) |
| +4 | `noLoader` | gmx_reset | 1 when block 0 does not contain "Loader V2" |
| +5 | `bootCode` | OUT #xx00 with bit 3 = 1 | `(val & 0x0F) | 0x80`, read back serially by IN #78FD |
| +6 | `bitIdx` | IN #78FD (block 0) / OUT #78FD resets to 0 | serial bit counter 0..7 |

---

## 2. Port map (wasm)

### 2.1 OUT - `gmx_out(port, val)` wasm f1170 @0x0564ad

Called first from `out()` (f91 @0x010e5a, `if (gmx_enabled && f1170(port,val)) return;`), before every stock
decoder. Returns 1 when it consumed the write. Only active when model == PROFSCORP and gmx_enabled.

```
if ((port & 0xFF) == 0x00) {                 // "port #xx00": any port with A7..A0 = 0 (ROM uses #0000)
    r00 = val;
    if (val & 0x08) {                         // command: software reset with a boot code
        r00 = val & 0xF7;
        bootCode = (val & 0x0F) | 0x80;
        reset(0);                             // f157(RM_NOCHANGE)
    }
    return 1;                                 // works even while GMX is hidden (r00 bit 5)
}
if (r00 & 0x20) return 0;                     // GMX hidden: all other GMX ports fall through to stock
switch (port) {                               // exact 16-bit match
case 0x78FD: r78FD = val; bitIdx = 0; set_banks(); return 1;
case 0x7AFD: r7AFD = val & 0xF0;               return 1;   // no set_banks
case 0x7CFD: r7CFD = val & 0x3F;               return 1;
case 0xDFFD: comp.pDFFD = val; set_banks();    return 1;
case 0x7EFD: comp.p7EFD = val;
             turbo = val >> 7;                             // [0x108bad]
             profrom_bank = (val >> 4) & 7;
             base_128 = ROM + bank*64K + 0x0000;  base_sos = +0x4000;
             base_sys = ROM + bank*64K + 0x8000;  base_dos = +0xC000;
             set_banks();                      return 1;
}
return 0;
```

`web_gmx_out(port, val)` (f1172 @0x05667b) = `gmx_out(port, val & 0xFF)` (front-end poke; return value dropped).

Stock decoders still handle (because gmx_out returns 0 for them) - out() f91 around the Scorpion branch:

- **#1FFD**: A15 = 0, A1 = 0, `(port & 0x4021) == 0x0021` (same as stock `& 0xC023 == 0x1FFD & 0xC023`).
- **#7FFD**: A15 = 0, A1 = 0, `(port & 0x4021) == 0x4021`; 7FFD bit 5 (48K lock) still blocks further 7FFD writes on
  this model (DFFD / 1FFD / 78FD are not blocked).
- **#7EFD** stock exact-match handler for model 2 also exists but is never reached while GMX is on.
- **#FE**: Scorpion decoding `(port & 0x23) == (0xFE & 0x23)` and not in DOS ports.

Note the GMX ports 78FD / 7AFD / 7CFD / 7EFD all satisfy the Scorpion 7FFD mask, so they must be decoded exactly and
before 7FFD - the web build does that by running gmx_out first.

### 2.2 IN - GMX block in `in()` wasm f116 @0x01fd49

Exact 16-bit match, only when GMX active and `!(r00 & 0x20)`:

| Port | Value returned |
|------|----------------|
| **#78FD** | `(r78FD & 0x7F) | (pFE.1 << 7)`; **if flash block == 0**: bit 0 is replaced by `(bootCode >> bitIdx) & 1` and `bitIdx = (bitIdx+1) & 7` |
| **#7AFD** | `(pDFFD & 7) << 4 | (p1FFD.4 << 3) | (p7FFD & 7) | (pFE.0 << 7)` = current page number at #C000 (7 bits) + one border bit |
| **#7EFD** | b7 = pFE.2, b6 = p1FFD.0, b5 = 0, b4 = r00.7, b3 = p7EFD.3, b2 = p7EFD.7, b1 = p7FFD.3, b0 = p7FFD.5 |
| #7CFD | not readable (falls to stock / floating bus) |

(pFE = last value written to port #FE, so bits 7 of 78FD/7AFD/7EFD together give the border colour R/B/G.)

The 7EFD formula was checked in raw WAT (the folded output mis-orders a `local.tee`; correct: b1/b0 come from p7FFD).

With GMX hidden (r00.5 = 1) these ports read as on a plain Scorpion; the ROMs use "IN #7EFD == #FF" as the test for
"GMX hidden" and write `#82` to port #0000 to un-hide it (ROM pg16 0x08be..0x08d1, pg18 0x00b5..0x00de).

Stock Scorpion turbo-by-IN (`(port & 0xC023) == 0x4021` -> turbo on, `== 0x0021` -> off, flag 0x108bad) is skipped
while GMX is enabled (f116, block guarded by `!gmx_enabled`).

### 2.3 Memory paging - set_banks wasm f93 @0x012e75 (MM_PROFSCORP case falls into MM_SCORP)

```
bank3 page = ((pDFFD & 7) << 4) | (((p1FFD >> 4) & 1) << 3) | (p7FFD & 7)      // 0..127, & ram_mask
bank2 page = ((r78FD & 0x7F) ^ 2) & ram_mask                                     // GMX only
bank1      = page 5 (stock)
bank0      = p1FFD.0 ? RAM page 0
           : p1FFD.1 ? base_sys_rom
           : CF_TRDOS ? (p7FFD.4 ? base_dos_rom : base_sys_rom)
           :            (p7FFD.4 ? base_sos_rom : base_128_rom)
p1FFD.2 -> CF_TRDOS forced
CF_PROFROM = (bank0 == base_sys_rom)       // PROF-ROM read-switch armed (membits at #0100/#0104/#0108/#010C)
any ROM bank: bankw = TRASH                // no flash writes
screen (ZX mode) = page 5 / 7 by p7FFD.3 (stock)
```

Without GMX the stock Scorpion formula is used: `bank = 7FFD[2:0] | 1FFD.4 << 3 | 1FFD[7:6] << 4`.

`r78FD ^ 2`: the default 0 maps page 2 (normal #8000). The ROM writes `#3B` to see page #39 (screen pixels) and
`#7B` for #79 (attributes) - Shadow monitor pg20 0x14eb..0x1536 - which confirms the XOR is the hardware's.

The Loader's own page helper (ROM pg0 unpacked, 0x7140) writes exactly this split: `DFFD = (p >> 4) & 7`,
`7FFD[2:0] = p & 7`, `1FFD.4 = (p >> 3) & 1`.

PROF-ROM switch by read (f94 @0x013d17, called from the step loop f151 when CF_PROFROM and a read hits #0100,
#0104, #0108, #010C): stock table `{0,1,2,3, 3,3,3,2, 2,2,0,1, 1,0,1,0}`, but with GMX it applies to the low 2 bits
and keeps block bit 2: `bank = (table[addr_idx*4 + (bank&3)] & 3) | (bank & 4)`.

### 2.4 Flash

- Loaded whole into `ROM_BASE_M` (f1325; `f29(PROFROM, 0x582000, 32)` must return 512, else
  `"invalid GMX flash size (512 KB)"` and exit). `profrom_mask = 7`, then `gmx_reset()`.
- Block n = flash 0x10000*n; slot order inside a block: +0 128, +0x4000 SOS(48), +0x8000 SYS, +0xC000 DOS.
- **Read-only.** No AMD/JEDEC command decoding exists: no 0x555/0x2AA/0x5555/0xAAAA constants in the code-path
  functions (only a bit-reverse 0x5555 in f1202), and set_banks redirects every ROM bankw to the TRASH page.
  (Confidence high.) The flash contents themselves contain no plain AMD-sequence code either (searched all 32
  pages for `LD (HL),#AA` / `LD A,#AA; LD (nn),A` near #55 writes - only an MBR 55AA signature in pg22). A flasher may
  live inside the compressed module in pg3 (text fragment "Flash-g..." in the ZX0 stream); not unpacked.

### 2.5 Reset / power-on

`gmx_reset()` wasm f1169 @0x056352 (called from web_init and web_gmx_cold):

```
p7EFD = 0; bootCode = 0; r00 = 0;
noLoader = (memmem(flash[0..0xFFFF], "Loader V2", 9) not found);  // string at mem 0x8fb8; scan offsets 0..65526
r78FD = r7AFD = r7CFD = 0;
p7EFD = noLoader ? 0x40 : 0x00;           // block 4 if block 0 has no GMX Loader
profrom_bank = p7EFD >> 4; set ROM bases; turbo = 0;
```

Every reset (`reset(mode)` f157 @0x0266b4), GMX branch:

```
p7EFD = (r00 & 0x10) ? (p7EFD & 0x70)          // "sticky block": stay in the current block, turbo/video off
                     : (noLoader ? 0x40 : 0);   // else: back to the Loader (block 0)
profrom_bank = p7EFD >> 4; set ROM bases;
r78FD = r7AFD = r7CFD = 0; turbo = 0;
r00 and bootCode are NOT cleared by reset (only by gmx_reset / cold)
Z-Controller: only [0x118e45a]=0xFF, [0x118e458]=0 - the SD card object is not reset (f963 skipped)
then the stock part: pDFFD = p1FFD = p7FFD = 0 ... set_mode(reset_rom)
```

Ini uses `RESET=MENU` (= RM_128): the 128 slot of block 0 = the GMX Loader starts. (RESET=SYS would start the SYS
slot of block 0 = data page pg2 and crash; so the reset ROM must be RM_128.) App macro `reset: 2` calls
web_reset(2) -> f1424 -> `reset(4 = RM_128)`.

`web_gmx_cold()` (f1173 @0x05668a): `memset(RAM, 0, ramsize KB)`, `gmx_reset()`, `reset(0)`, full Z-Controller/SD
reset (f963 -> f939(0x118e1f8)). App calls it after mounting an SD card ("its shadow monitor looks for the card at
power-on only").

### 2.6 Turbo

f1341 @0x07bdb5 (frame length): for models 1/2 the multiplier is `[0x108bad] ? 2 : 1`; with GMX that flag is
7EFD bit 7 (set by OUT #7EFD, cleared by reset). 3.5 -> 7 MHz, frame t-states x2. (The app's "state & 0xFF == 0xC0"
= turbo + block 4.)

### 2.7 NMI / Shadow Service Monitor

No GMX-specific code. `web_nmi(0)` = f1432 = stock `main_nmi`: `nmi_pending = 1; m_nmi(RM_NOCHANGE)` (push PC,
PC = #0066); `web_nmi(1)` = f1433 = stock `main_nmidos` (Scorpion: defer while PC < #4000 outside DOS, then
`m_nmi(RM_DOS)`). The step loop's stock Scorpion rule (`nmi_pending && PC >= #4000 -> m_nmi(RM_DOS)`) puts the CPU
into the DOS slot of the current block at #0066. In block 4/7 that is the GMX TR-DOS 5.03 (pg19): `#0066: JP #2A56 ->
JP #0807`, which saves IN #7EFD / IN #7AFD, writes `OUT (#1FFD),#12` (SYS slot + RAM bank bit) and continues in the
SYS slot (pg18), a dispatcher that pages block 5 (`SET 4,H` on the saved 7EFD value, `OUT #7EFD`) = the Shadow
Service Monitor 6.44. (Confidence: medium - traced to the 7EFD switch, not instruction by instruction.)

### 2.8 Video: 640x200 (render f49 @0x0086ed, GMX part near the end)

Condition: `gmx_enabled && model == 2 && !(r00 & 0x20) && (p7EFD & 0x08)` (f49; same test in f1344 to pick the
hi-res frame path). Otherwise the normal ZX screen is drawn. Only the 32 bpp path exists (`[0x14f0d8] == 32`).

```
pixpage = (p7FFD & 8) ? 0x3B : 0x39;   attrpage = pixpage | 0x40     (#79 / #7B), & ram_mask
S = r7AFD | (r7CFD << 8)              // web_gmx_scroll() returns exactly this (f1175)
for y in 0..199:
    o = (y*80 + S) % 16000            // linear, 80 bytes per line, wraps at 16000 (0x3E80)
    for x in 0..79:                   // 640 pixels
        p = RAM[pixpage][o]; a = RAM[attrpage][o]; o = (o+1 > 0x3E7F) ? 0 : o+1
        8 pixels, MSB first: colour = bit ? sctab32[pass][0x100 + a] : sctab32[pass][a]
    each line drawn twice (pass 0 table 0x8ba40, pass 1 table 0x8c240 = stock t.sctab32[0]/[1]) -> 640x400
picture centred vertically in the frame if frame height > 200; border = stock border fill (f81/f82)
```

`sctab32` is indexed by a **PC attribute** (stock draw.cpp: `pc_attr = paper << 4 | ink`, each 4-bit IGRB with
bit 3 = bright). The GMX path indexes it with the raw attribute byte, so a GMX attribute is:
**bits 3-0 ink (bit 3 = bright), bits 7-4 paper (bit 7 = bright), no FLASH** - 16 ink + 16 paper colours from the
normal ZX palette ("own attributes" in page.html). Scroll granularity is 16 bytes (r7AFD keeps bits 7-4 only),
range 0..0x3FF0, applied modulo 16000. No GMX palette registers were found.

---

## 3. web_gmx_* exports

| Export | Index / offset | Returns / does |
|--------|----------------|----------------|
| `web_gmx_state()` | f1171 @0x05662b | 0 unless GMX active; else `p7EFD | r78FD << 8 | r00 << 16 | bootCode << 24` |
| `web_gmx_out(port, val)` | f1172 @0x05667b | `gmx_out(port, val & 0xFF)` |
| `web_gmx_cold()` | f1173 @0x05668a | RAM cleared, gmx_reset, reset(0), SD/Z-Controller reset |
| `web_gmx_on()` | f1174 @0x0566c9 | `!(r00 & 0x20) && model == 2 && gmx_enabled` (GMX not hidden) |
| `web_gmx_scroll()` | f1175 @0x0566f0 | `r7AFD | r7CFD << 8` |

---

## 4. Flash map (gmx644.rom, 512 KB = 8 blocks x 4 slots)

Slot order inside a 64 KB block (as the emulator maps it): `+0 128 | +4000 SOS(48) | +8000 SYS | +C000 DOS`.

| Blk | Slot 0 (128) | Slot 1 (SOS) | Slot 2 (SYS) | Slot 3 (DOS) |
|-----|--------------|--------------|--------------|--------------|
| 0 | pg0 **GMX Loader V2.00** ("TMgmx(r) Loader V2.00", "(c) 1997,1998 by Andrew MOA (c) 2024 LW/PLM", "Compiled: 17 Mar 2024", ZX0-packed body + FAT32 / "ZS-Scorpion") | pg1 data: two 8 KB "schemes" `DK_test` (29.02.00), `Pentagon7` (22.01.99) | pg2 data: schemes `PentaGON`, `Komposit` | pg3 lower 8 KB: scheme `Work Sch`; upper: Loader DOS trampolines at #3D00 (`IN A,(C):RET / OUT (C),A:RET / JP ...`) + ZX0 data ("LoaderxV2.00.334", "Flash-g", HDD/MBR strings) |
| 1 | pg4 Sinclair 128 BASIC 1986 (original) | pg5 Sinclair 48 BASIC 1982 | pg6 TR-DOS 5.03 | pg7 = pg6 (identical) |
| 2 | pg8 small test/diagnostic code (border cycling, IN #7FFE, many GMX ports) + 8x8 font | pg9 empty (#FF) | pg10 = pg9 empty | pg11 font + little code |
| 3 | pg12 128 BASIC "(C)2024 Reset Rom Loader" | pg13 48 BASIC (IN #57 present) | pg14 **RRL**: /RRL/BOOT.$C, FASTBOOT.INI, SHELL/DEBUGGER/MONITOR/SNAP.RRL | pg15 TR-DOS 5.03 + "Rom Loader v2.00s (C)2024 LW/PLM", SMUC IDE (#FFBA/#7FBA/#xxBE) |
| 4 | pg16 **Scorpion GMX 128 menu/BASIC** ("1992-2025 Scorpion GMX 204", OUT #0000, IN #7EFD) | pg17 48 BASIC (GMX patched: OUT #0000, IN #7EFD) | pg18 SYS dispatcher/"Analyser" (78FD/7EFD/1FFD/DFFD switching) | pg19 TR-DOS 5.03 (GMX, NMI -> monitor, SMUC #7FBA) |
| 5 | pg20 **Shadow Service Monitor v6.44s** ("1993-1998 MOA, 2022-2025 PLM", "Grafic Memory eXpander", "Scorpion GMX 2048", "Compiled: Thu 21 Aug 2025") | pg21 monitor part 2 ("Professional Extention. Shadow service monitor with Analyser", "v6.44su, #25AB, 21.08.2025", CMOS, Z-Controller) | pg22 monitor part 3 (disk/RAM bank tools, **Z-Controller SD driver #77/#57**) | pg23 FAT32 / SMUC IDE driver (#xxBE, #xxBA) |
| 6 | pg24 utilities: NEMO, SMUC, ZC, NeoGS, ProteusGC detection | pg25 **Navigator** (file manager: copy disk/image, TR-DOS, LFN) | pg26 ROM disk / setup (SNA SPG $C TRD SCL TAP TZX, NVRAM/CMOS, Kempston, "Scorpion/Pentagon", "Basic 48/128") | pg27 ZXWord-like editor, "For run, you need GMX with firmware at least v5.XX.40 with 2Mb memory" |
| 7 | pg28 = pg16 (identical GMX 128) | pg29 48 BASIC (variant of pg17) | pg30 = pg18 | pg31 TR-DOS 5.03 variant (more SMUC #7FBA) |

(Duplicates: pg7=pg6, pg10=pg9, pg28=pg16, pg30=pg18.)

### 4.1 Boot path (reset -> 128 menu)

1. Reset: p7EFD = 0 (block 0, "Loader V2" present), 7FFD = 0 -> 128 slot = pg0 at #0000.
2. pg0 `#0000 DI; JP #00B5` (ROM 0x00b5): `OUT (#7EFD),#84` (turbo on, block 0), delay, SP = #5BFF,
   `CALL #00F1`: `OUT (#78FD),0` (resets bitIdx, page 2 at #8000), reads 8 bits of `IN #78FD` bit 0 LSB-first into A
   (= bootCode), then `IN D,(#7AFD)`.
3. `LD HL,(#0008)` (= #016E), `DE = #5C01`, `RST #18` -> ZX0 standard decompressor at #012A: 9904 bytes unpacked to
   #5C01; 4 bytes from #0004 copied to #5C03; `CALL #5C01` with A = bootCode. (`work/loader_patched.bin`)
4. Loader (#5C0F): `OUT (#0000),A&7`, `OUT (#7EFD),#84`, `CALL #71C5` = zero DFFD, 78FD, 7FFD, 1FFD, 7AFD, 7CFD; RAM
   test/clear (#70D8 probes all 128 pages via #7140), screen.
   - bootCode.7 = 0 (power-on): `CALL #6827` (checksum a scheme); if OK, `OUT (#0000),#0A` = reset with bootCode #8A.
   - Keyboard at #5CF1: CAPS SHIFT + 0 (`IN #FEFE` bit 0 / `IN #EFFE` bit 0, i.e. "Delete") -> Loader setup menu
     (#5D53, "GMX Loader v2.00 (C) 2024 LW/PLM"). App note: "Delete at start: Set Up".
5. Normal boot (#5D0C..#5D46): `CALL #6858` reads the selected scheme trailer -> D, E; then
   `OUT (#7EFD), E==#FF ? #40 : E & #F0`, `OUT (#0000), (D==#FF ? 0 : D & #F0) | (bootCode & 7)`, `JP #0000`
   -> the 128 slot of the chosen block (default block 4: Scorpion GMX 128 menu, then 48 BASIC / TR-DOS of block 4).
   The app waits for `web_gmx_state() & 0xFF == 0xC0` (turbo + block 4) before pressing Enter in the 128 menu.

Scheme record (8 KB slot, trailer at +0x1FE6): 8-byte date, **D** (port #00 flags, +0x1FEE), **E** (7EFD value,
+0x1FEF), 10-byte name, checksum word at +0x1FFA over 0x1FF0 bytes (#7024). Scheme index = bootCode & 7:
0/1 = pg0 halves (1 = "Start Sch", D=E=#FF), 2 = pg3 lower ("Work Sch", #FF/#FF), 3 = none, 4/5 = pg2 halves
("PentaGON" D=#E0 E=#10, "Komposit" D=#60 E=#10), 6/7 = pg1 halves ("DK_test" D=#10 E=#20, "Pentagon7" D=#A0 E=#30).
So r00 bit 5 (hide GMX) is used by the Pentagon-compatible schemes, bit 4 (sticky block) by DK_test.

### 4.2 OUT / IN port usage in the flash (LD BC,nnnn + OUT/IN (C))

- Loader (unpacked): #0000 (x5), #7EFD (x6), #1FFD, #7FFD, #DFFD, #78FD, #7AFD, #7CFD; disk drivers through
  variables #7BA5/#7BA7 (port of the current device).
- Block 4 128/48: #0000, IN #7EFD, IN #7AFD, #7FFD, #DFFD, AY #FFFD/#BFFD, #77DD.
- Block 4 SYS / block 7 SYS: #1FFD, IN/OUT #78FD, IN/OUT #7EFD, #7FFD, #DFFD.
- Monitor (block 5): #78FD (save/restore, #3B/#7B screen pages), #7AFD/#7CFD (scroll write), #DFFD, #1FFD,
  Z-Controller #0077 / #0057, SMUC #xxBA/#xxBE, Kempston mouse #FADF/#FBDF, #FF1F.

### 4.3 Device detection

- **SD (Z-Controller)**: Loader driver table at unpacked #7BFF = `77 00 57 00` (#0077 config/status, #0057 data);
  monitor pg22 0x32d1: `IN A,(#0077); AND 2` (card-present/WP test), then SPI traffic on #0057 with `OUT (#0077)` chip
  select. The monitor probes the card at power-on only (hence the app's web_gmx_cold after inserting a card).
- **SMUC / IDE**: Loader driver table at #7BD2 = `BE F8 .. BE FF` = SMUC IDE registers #F8BE..#FFBE (accessed as DOS
  ports through the pg3 #3D00/#3D03 trampolines); IDE identify/diagnostic (`#90`, `#EC`) at #8060..; TR-DOS pages use
  #FFBA / #7FBA (SMUC clock/NVRAM). Ini: `SMUC=1`, `CMOS=DALLAS`.
- Block 6 utility page probes NEMO, SMUC, ZC, NeoGS, ProteusGC.

---

## 5. Front end (app.js / page.html)

- Machine `GMX` (app.js @9456): name "Scorpion ZS 256 GMX", base 69888 t-states/frame, speed 2.5, files
  `['gmx644.rom']`, ini `MISC: HIMEM=PROFSCORP, RAMSize=2048, CMOS=DALLAS, SMUC=1, RESET=MENU, GMX=1`,
  `ROM: ROMSET='', PROFROM=gmx644.rom`, `ULA: Preset=SCORPION`. Note "SD: Z-Controller, HDD: SMUC".
- Reset-to-128-menu macro (@214688): `reset: 2` (RM_128), then `until (web_gmx_state() & 0xFF) === 0xC0` (max 1500
  frames, soft) - "the loader, the monitor's test (and the SD card) first: until its flash block 4 runs the 128 ROM
  (turbo on)", then wait, Enter ("128 TR-DOS" is the first menu item).
- SD mount (@343453): after a successful mount on GMX calls `web_gmx_cold()` (power cycle).
- **Mix Frames** (@49904, page.html 1339): GMX only, on by default for GMX. Each shown picture = mean of the last two
  emulated frames; applied only when the shown ZX screen (#7FFD bit 3, tracked over 4 frames) alternates 1-2-1-2
  (mixScr 5 or 10), not for 1-1-2-2 (Edge Grinder); off at max speed.
- **F8 / F9 / F10** (@133376, OSZ_FKEYS @168339): on GMX send CAPS SHIFT + SYMBOL SHIFT + 1 / 9 / 0 (held 8 frames):
  OSZ "Ext+1" system process, "Ext+9" None Commander, "Ext+0" shadow monitor.
- OSZ model: SD card zip with `.json {"model":"gmx","a":"OS.TRD"}` -> start GMX with the card, OS.TRD in A:.
- Debugger screen source "GMX: the 640x200 screen of Scorpion GMX from the RAM now (page #39 / #3B)" (page.html 1854).
- REST link name `gmx` / aliases `gmx`, `scorpgmx`.

---

## 6. Confidence

| Topic | Confidence | Why |
|-------|-----------|-----|
| Port decode and bit fields (OUT) | high | read directly from f1170 WAT |
| IN readback layout | high (7EFD verified in raw WAT) | f116 |
| 2 MB paging formula, 78FD^2 | high | f93 + Loader #7140 + monitor writing #3B/#7B |
| ROM slot order and 3-bit block | high | f1170 / f157 / f94 pointer order + Loader #7173 |
| Reset / sticky block / boot code serial | high (emulator side); medium (meaning of each D/E flag) | f157, f1169, Loader #00F1, #5D14 |
| 640x200 layout, scroll, attributes | high for layout/scroll; medium-high for attribute bit meaning | f49 + stock sctab32 semantics |
| Turbo x2 | high | f1341 |
| No flash write emulation | high | no command decoding, TRASH bankw |
| NMI -> monitor path | medium | stock NMI code; ROM path traced to the block switch |
| Flash map | high for identification by strings/md5; medium for pg1/pg2/pg8/pg11 purpose |

## 7. Open questions

1. Real GMX meaning of port #xx00 bits: bit 5 = hide GMX, bit 4 = keep block on reset, bit 3 = reset-with-code are
   emulated; bits 7 and 6 (set by "PentaGON"/"Komposit"/"Pentagon7" schemes) and bits 2-0 are only stored (bit 7 read
   back at 7EFD.4). Pentagon timing / 7FFD decoding / composite video are candidates - not emulated in the web build.
   Is the real decode really "A7..A0 = 0" (any #xx00)?
2. 7EFD bits 2-0 on write: ignored by the web build (only 7..3 used). Real hardware?
3. Flash programming protocol (chip type, enable bit, command addresses) - not emulated; a flasher may be inside the
   ZX0 data of pg3 ("Flash-g") or on disk. Needed only if unreal-ng wants Loader Setup / firmware update to persist.
4. Scroll: only r7AFD bits 7-4 and r7CFD bits 5-0 kept -> 16-byte steps; real hardware may scroll by lines or have a
   different wrap (the web build wraps at 16000 bytes).
5. GMX video timing: the web build draws the whole 640x200 picture per frame (no per-line raster effects, no
   mid-frame register changes, no contention changes in turbo).
6. Border in GMX mode: stock border fill; whether the GMX border/vertical position matches real hardware unknown.
7. Interaction of 7FFD bit 5 (48K lock) with DFFD / 78FD on real GMX (web build: lock blocks only 7FFD).
8. Which RAM pages are visible to the ZX screen in GMX mode (web: pages 5/7 as usual when 7EFD.3 = 0).
9. The pg1/pg2/pg3 "scheme" 8 KB bodies (what the Loader does with them besides the D/E trailer) were not analysed.
