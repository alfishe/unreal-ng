# Scorpion ZS-256 — Hardware Reference

Verified behavior of the real machine, written comparatively against the ZX Spectrum
128K (its Sinclair ancestor) and the Pentagon (its closest clone sibling). Everything in
this document is normative for the clone implementation; divergences and unknowns are
collected in §12.

Sources: Fuse `machines/scorpion.c` + `peripherals/disk/beta.c` + `z80/z80_ops.c`; MISTer
`scorpion-zs256-design.md` (dimitriuz branch); original UnrealSpeccy source
(`memory.cpp` `set_banks()`, `set_scorp_profrom()`, `io.cpp` — the direct ancestor of this
codebase, mirror: github.com/mkoloberdin/unrealspeccy); **Scorpion ZS-256 programmer's
guide** ("Скорпион ZS 256: краткое руководство для программистов", MSD #03,
zxpress.ru article 6048 — the primary source for the `#1FFD` bit table); speccy4ever ROM
archive; ProfROM Uni (rev.B) manual; World of Spectrum Scorpion FAQ; Wikipedia.

Research log (2026-09-08): the `#1FFD` bit-2 question and the ProfROM quadrant-state
question were resolved against these sources — see §4.3, §5.2 and §12 items 9-11.

---

## 1. Machine identity and positioning

| Property | ZX Spectrum 128K | Pentagon 128 | **Scorpion ZS-256** |
|---|---|---|---|
| Origin | Sinclair / Amstrad, 1985-86 | Moscow clone, 1989+ | **St. Petersburg, Zonov, 1991-1998** |
| CPU | Z80A @ 3.5469 MHz | Z80 @ 3.5 MHz | Z80B @ 3.5 MHz (7 MHz turbo) |
| RAM | 128 KB | 128 KB (…1 MB) | **256 KB stock (…1 MB)** |
| ROM | 32 KB (2×16K) | 48K + TR-DOS | **64 KB (4×16K), ext. to 2 MB** |
| Memory control | `#7FFD` only | `#7FFD` only | `#7FFD` **+ `#1FFD`** |
| Disk interface | none | Beta-128 add-on | **Beta-128 built in** |
| Service monitor | none | none | **Shadow Service Monitor (ROM2)** |
| NMI button | n/a | n/a | **MNI "Magic" button** |
| Video timing | 311 lines, 70908T | 320 lines, 71680T | **312 lines, 69888T (Sinclair-matching)** |
| Contention | Ferranti ULA wait states | none | **none** |

**What this means in practice:** the Scorpion is the clone that industrialized the
Soviet upgrade path — factory 256 KB RAM, a built-in disk interface, a resident debug
monitor reachable at any moment, and a ROM subsystem that grew into a read-only software
distribution medium (ROM-disk). Almost every later Zonov-family machine (KAY, GMX,
ZXM-Phoenix) is a Scorpion derivative, which is why its paging scheme became a de-facto
compatibility standard in Russian demoscene software.

Architectural drift vs. the Sinclair original: the Scorpion keeps the *programming
model* (48K address space, `#7FFD` bit meanings, `#FE`/AY port behavior) while replacing
the *implementation substrate* — discrete TTL logic instead of a custom ULA, a paged
memory manager instead of a fixed bank latch, and a four-way ROM selector instead of a
two-image ROM switch.

---

## 2. History & Development Timeline

> Biographical and historical facts are centralized here; other sections reference them
> by name only.

**Sergey Zonov** (St. Petersburg) — engineer, founder of the "Scorpion" firm; author of
the ZS-256 line and, later, of the original UnrealSpeccy emulator (this codebase's
ancestor), which is why the emulator's Scorpion support is the reference-accurate one.

| Year | Event |
|---|---|
| 1991 | Zonov's earlier clone work (Leningrad PTS line) establishes the full-size keyboard, metal case, expansion-slot design language |
| 1992 | **Scorpion ZS-256** launched: 256 KB, 64 KB ROM, built-in Beta-128; BASIC 128 ROM dated 1992-94 ("1993 Scorpion ZS 256" banner) |
| 1993-96 | Production ramps; ZS-256 becomes one of the most widespread Russian clones; ProfROM ROM-disk subsystem by **Vladimir Kladov** appears (v3.x) |
| 1996-98 | **ZS-256 Turbo** (7 MHz hardware/software turbo), ProfROM 4.0x line, SMUC (IDE/RTC) controller; GMX memory/graphics expander extends the family to 4 MB RAM + 2 MB ROM |
| 1998+ | Scorpion firm winds down peripheral production; ProfROM Uni (rev.B) re-issued by third parties for Scorpion/KAY/Phoenix |
| 2000s | Zonov releases **UnrealSpeccy** for Windows — the Scorpion family's authoritative software model, later ported forward by the community |

---

## 3. CPU and frame timing

- Z80B at 3.5 MHz nominal; the Turbo variant switches 3.5 ⇄ 7 MHz via software (`#00`
  bit 6 of `#EFF7`-class ports on later boards) and a front-panel button. The emulator's
  generic turbo-mechanism covers this; no Scorpion-specific port is required for v1.
- Frame = **69888 T-states = 312 lines × 224T** — Sinclair-matching geometry (identical
  to ZX Spectrum 48K), *different* from both the 128K (311×228T) and the Pentagon
  (320×224T).
- INT position: as ZX Spectrum 48K (`intstart = 1794`, `intlen = 32` in this codebase's
  config convention).
- **No memory contention.** The video logic is built from counters and multiplexers
  fetching directly from fast RAM; the CPU never waits. In the emulator's terms:
  `ULA_DISCRETE_LOGIC`, `contentionEnabled = false`, border visible 1T after the port
  write (`borderUpdateTStates = 1`), with the Scorpion-specific 4T border-update
  correction fields surviving in `platform.h` (`TEMP::border_add/border_and`).

---

## 4. RAM organization and paging

### 4.1 Bank windows

| Z80 window | ZX Spectrum 128K | Pentagon | **Scorpion ZS-256** |
|---|---|---|---|
| `#0000-#3FFF` | ROM0/ROM1 by `#7FFD[4]` | same | **ROM chain or RAM bank 0** (see §5.2) |
| `#4000-#7FFF` | RAM bank 5 (fixed) | RAM 5 | **RAM bank 5 (fixed)** |
| `#8000-#BFFF` | RAM bank 2 (fixed) | RAM 2 | **RAM bank 2 (fixed)** |
| `#C000-#FFFF` | RAM 0-7 by `#7FFD[2:0]` | RAM 0-7 | **RAM 0-15 (…63) by `#7FFD[2:0]` + `#1FFD[4]` (+ `#1FFD[7:6]`)** |

The visible drift: only the top window pages, like Sinclair — but the bank number is
assembled from **two (or three) registers**, giving 16 banks over the same 3-bit
programming interface used by 128K software.

### 4.2 Port `#7FFD` (write-only)

| Bit | Meaning | Notes |
|---|---|---|
| 2:0 | RAM bank bits 2:0 | low half of the `#C000` bank number |
| 3 | screen select | 0 → bank 5, 1 → bank 7 |
| 4 | ROM select | 0 → ROM0 (BASIC 128), 1 → ROM1 (48K BASIC) — *only when `#1FFD[1:0]` = 0* |
| 5 | **paging lock** | write applies, then **all further `#7FFD` writes ignored until reset** |
| 7:6 | unused on base machine | (Pentagon uses these for RAM bits 4-5 — the Scorpion does **not**) |

Reads of `#7FFD` are not decoded on hardware: they behave like any unattached port
(float bus; the emulator returns `#FF` via the unattached path).

### 4.3 Port `#1FFD` — the Scorpion memory register

| Bit | Meaning | Verified by |
|---|---|---|
| 0 | **RAM bank 0 mapped at `#0000-#3FFF`** (highest ROM priority override) | programmer's guide D0; Fuse, MISTer, UnrealSpeccy |
| 1 | **Service ROM (Shadow Monitor) at `#0000`** (second priority; "expansion ROM 27512") | programmer's guide D1; Fuse, MISTer, UnrealSpeccy |
| 2 | **RS-232C output line — no memory function.** The original UnrealSpeccy's `if (p1FFD & 4) flags \|= CF_TRDOS` is an emulator-only, set-only extension not backed by hardware, Fuse or MISTer. **Not implemented** (decision, §12 item 9) | programmer's guide D2; Fuse/MISTer ignore the bit |
| 3 | unused | programmer's guide D3 |
| 4 | **RAM bank bit 3** — selects banks 8-15 | programmer's guide D4; Fuse, MISTer, UnrealSpeccy |
| 5 | Centronics strobe — no memory function | programmer's guide D5 |
| 7:6 | **RAM bank bits 5:4** — banks 16-63, present on 1024 KB machines only; masked by installed RAM size (`ram_mask`) | UnrealSpeccy `set_banks()` (1024 KB boards) |

The programmer's guide also states that all `#1FFD` bits read as 0 after reset and that the
port is reachable only from machine code (BASIC `OUT` to `#7FFD`/`#1FFD` resets or hangs
the machine because of the `#7FFD` lock).

- Writes are **not** affected by the `#7FFD[5]` lock (the lock scopes to `#7FFD` only;
  locking `#1FFD` would trap the machine inside the Service Monitor, whose exit is a
  `#1FFD` write).
- **Reads return `#FF** on non-Turbo boards (open bus on the register; Turbo boards add
  a readable latch — out of scope).
- `#C000` bank = `p7FFD[2:0]` + `(p1FFD[4] << 3)` + `(p1FFD[7:6] << 4)`, clamped by
  installed RAM: 256 KB → 16 banks, 1024 KB → 64 banks.

### 4.4 Effective `#0000` source — priority chain

```
1. p1FFD[0] = 1              → RAM bank 0 (read/write; "RAM at #0000" mode)
2. p1FFD[1] = 1              → ROM2 Service (Shadow) Monitor
3. DOS session active        → ROM3 TR-DOS — regardless of p7FFD[4]
4. else p7FFD[4] = 1         → ROM1 48K BASIC
5. else p7FFD[4] = 0         → ROM0 BASIC 128
```

The TR-DOS slot is reached only through the `#3Dxx` trap (§6); the remaining cases follow
`#7FFD[4]` exactly as on a 128K machine.

**Rule 3 is normative and differs from the heritage code.** MISTer (`trdos_en forces
page_rom = 3 while a disk is active`) and Fuse (`beta_memory_map` → `memory_map_romcs_full`
overrides whatever ROM `#7FFD` selected) both map ROM3 whenever the session is open. The
original UnrealSpeccy `set_banks()` — and this codebase's generic `UpdateZ80Banks()` path
ported from it — instead map the *service* ROM when `CF_TRDOS` is set with `p7FFD[4] = 0`.
For the Scorpion that heritage behavior is wrong: the BASIC-128 boot menu's "128 TR-DOS"
entry traps from the Shadow Monitor with `p7FFD[4] = 0`, and would land in the monitor ROM
instead of TR-DOS. The Scorpion branch must therefore select ROM3 unconditionally while
the session is active (see §12 item 10).

---

## 5. ROM subsystem — 64 KB core and the extension ladder

> This section carries the special requirement: **every rung of the ROM size ladder,
> from the minimal 64 KB to multi-megabyte ROM-disk images, must be supported.**

### 5.1 Minimal ROM — 64 KB in 4 pages

| ROM page (logical) | Contents | Role |
|---|---|---|
| ROM0 | Scorpion BASIC 128 | boot default (`#7FFD[4]=0`) |
| ROM1 | 48K BASIC (Scorpion build — *not* byte-identical to Sinclair's) | `#7FFD[4]=1` |
| ROM2 | Shadow Service Monitor | `#1FFD[1]=1`; MNI target |
| ROM3 | TR-DOS 5.03 | Beta-128 session (paged by the `#3Dxx` trap only — §6) |

**File layout — one canonical page order, verified against the shipped bytes.** All
Scorpion bundles in this repo use 16K-unit order **BASIC 128 / 48K BASIC / Service /
TR-DOS** (= ROM0/ROM1/ROM2/ROM3 of the logical table above):

| Bundle | Size | Pages (SHA-1 prefixes) | Notes |
|---|---|---|---|
| `scorpion.rom` (shipped, SHA-256 `07c190ae…` — matches `rom.cpp` signature table) | 64 KB | `477114ff` / `367b5a10` / `07783ee2` / `33703e97` | first two pages byte-identical to MISTer v2.94 |
| `scorp295.rom` (v2.95 build) | 64 KB | `477114ff` / `c0c2b85f` / `5f692c84` / `cb2da2c4` | same order, newer 48K/Service/TR-DOS builds |
| `scorp_prof401.rom` (ProfROM 4.01) | **512 KB** | Q0: `13cfe7aa` / `c0c2b85f` / `c386c765` / `d6eb0eeb` | 8 quadrants; Q0 page1 = the v2.95 48K build |

Role verification is behavioral, not nominal: page0 carries a reset vector
(`F3 C3 D1 08` = DI, JP) and the boot banner; page1 starts `F3 AF 11 FF FF` and holds
the Sinclair copyright string (48K BASIC); page2 has **no reset vector** (starts
`37 CB…`) — it is entered at `#0066` via MNI, i.e. the Service Monitor; page3 starts
`F3 11 00 40…` and contains the "TR-DOS"/"BETA" strings.

> **Loader bug found by this verification:** `rom.cpp` (`MM_SCORP`/`MM_PROFSCORP`
> cases) assigns the pointers Service-first (`page0→base_sys_rom`,
> `page1→base_dos_rom`, …), which scrambles all four roles for every bundle above —
> including the signature-validated `scorpion.rom`. **Fixed 2026-09-08** (commit `3f49622c`)
> to `page0→base_128_rom`, `page1→base_sos_rom`, `page2→base_sys_rom`,
> `page3→base_dos_rom` — the same order the original UnrealSpeccy `set_scorp_profrom()`
> uses inside every ProfROM quadrant.

The clone maps *logical* pages through the `base_*_rom` pointers; the per-page SHA-1
prefixes above double as fixture goldens.

### 5.2 ProfROM — 128/256 KB, quadrant switching by read strobe

ProfROM (Vladimir Kladov, v3.x-4.x) replaces the 64 KB ROM with a **multiple of 64 KB
"quadrants"**. Each quadrant is a complete 4-page ROM set; the extra quadrants carry
software (commanders, editors, assemblers, games — the "ROM-disk" content).

**Selection mechanism (hardware GAL, software-visible contract):** while the Service
ROM window is paged at `#0000`, **any CPU read of the `#0100-#010F` block** clocks a
quadrant state machine — row selector `S = A3:A2` (the read of `#0100+4*S` applies row
`S`), `A0/A1` are not bonded on the GAL so all four addresses of a group are equivalent.
The transition table, byte-identical across all four independent sources:

```
            S=0 #0100-03   S=1 #0104-07   S=2 #0108-0B   S=3 #010C-0F
from Q0:        Q0              Q3              Q2              Q1
from Q1:        Q1              Q3              Q2              Q0
from Q2:        Q2              Q3              Q0              Q1
from Q3:        Q3              Q2              Q1              Q0
```

| Source | Representation |
|---|---|
| Original UnrealSpeccy | `set_scorp_profrom()` `switch_table` = `{0,1,2,3, 3,3,3,2, 2,2,0,1, 1,0,1,0}`, strobed by reads of `#0100/#0104/#0108/#010C` while `CF_PROFROM` |
| ZXMAK2 | `s_profPlaneMap` indexed `(addr & 0x0C) | plane`, subscribed for `RdMem` **and** `RdMemM1` with mask `(addr & 0xFFF0) == 0x0100`, gated on `SYSEN`, remapped before the read completes |
| Xpeccy | `ZSLays[(adr & 0x000C) >> 2][prt2 & 3]`, gate `(adr & 0xFFF3) == 0x0100 && (p1FFD & 2)` (data reads only, `!m1`) |
| Scorpion 256 Turbo+ GAL decode | `RDR-`-clocked plane latches `P1/P0`, `T` term requires the service page + the `#01xx` address pattern, `A0/A1` unwired (`materials/Scorpion256TPlus_GAL_decoded.md` §3) |

The historical `set_scorp_profrom(read_address)` parameter is the *selector index*
0-3 (which of the four strobe groups fired), not a literal address — the four
references above agree on the block, and the emulator passes the raw address so
`S` comes from `A3:A2` (M1 fetches included, matching the GAL's `RDR-` clock and
ZXMAK2's subscription; Xpeccy's `!m1` is a data-reads-only simplification).

Notes grounded in the table's design:

- The `S=0` row is a **hold**: the monitor reads its plane ID from `#0101`
  (byte value `>> 2` = plane number) without switching, and ordinary execution
  fetching the block is inert — which makes the scheme transparent to normal
  execution and to the reset fetch itself (the `#0000-#0003` reset path never
  clocks the machine).
- The ProfROM service software walks this graph deliberately to expose other
  quadrants' ROM-disk contents: the shipped image's ROM-disk switcher at `#E4B5`
  executes `LD L,(HL)` with `HL = #010C` (484 strobes observed in a single
  session), and each non-zero quadrant carries a `#0111` stub
  (`LD BC,#1FFD / LD A,2 / OUT (C),A / LD HL,#010C / LD L,(HL) / XOR A / OUT (C),A /
  JP 0`) that re-enters quadrant 0 through reads of `#0108` (Q2) / `#010C` (Q1, Q3).
- `profrom_mask` = {0 → 64 KB (no switching), 1 → 128 KB (Q0-Q1), 3 → 256 KB (Q0-Q3)}.
- The switching only occurs for the **ProfROM variant** (`MM_PROFSCORP`); the base
  machine never switches (`CF_PROFROM` never set).
- On reset, quadrant 0 is selected (boot always comes from the image's first 64 KB;
  ZXMAK2 does the same in `ResetState`).
- **The quadrant is a byte of machine state, not a derived value.** In the original
  UnrealSpeccy it lives in `COMPUTER::profrom_bank` and is written only by
  `set_scorp_profrom()` as `switch_table[selector*4 + profrom_bank] & profrom_mask`; the
  new value depends on the *previous* value, i.e. on the whole read history. No
  combination of port latches reproduces it, so every state serializer that must
  reproduce execution (TTD checkpoints, divergence hashes) has to carry the byte
  itself. This codebase still has the field (`EmulatorState::profrom_bank`,
  `TEMP::profrom_mask`), unused until now (§12 item 11).

**Verified against the shipped image (`scorp_prof401.rom`) and the GAL decode:**

- Every quadrant's Service page carries the 16-byte ID block at `#0100`:
  quadrant 0 = `E5 02 …`, others = `01 06 / 01 0A / 01 0E …` — each page's `#0101`
  byte shifted right twice equals its plane number, readable without switching
  because `#0100-#0103` is the hold row.
- The strobed read returns the **post-switch** quadrant's byte — ZXMAK2 subscribes
  the gate *before* the memory device "to handle memory switches before read",
  and the GAL clocks on `RDR-` (the read strobe itself), so operand bytes of the
  very instruction performing the switch already come from the new plane
  (mid-instruction remap, §12.4).

### 5.3 Extended ROM-disk — up to 2 MB via direct window select

For images beyond 256 KB, the quadrant's high bits are selected **directly through port
`#7EFD` bits 4-5** (the mechanism the original UnrealSpeccy carried as its GMX block —
`profrom_bank = (p7EFD >> 4) & 3` — and the one ProfROM Uni-class hardware uses for
large ROMs):

| ROM size | Quadrants | Quadrant select source |
|---|---|---|
| 64 KB | 1 | fixed Q0 |
| 128 KB | 2 | state machine (1 bit) |
| 256 KB | 4 | state machine (2 bits) |
| 512 KB | 8 | `#7EFD[4]` + state machine |
| 1 MB | 16 | `#7EFD[5:4]` + state machine |
| 2 MB | 32 | `#7EFD[5:4]` + state machine + emulator extension bit (debug/API, default 0) |

The effective window quadrant is
`profrom_bank = (p7EFD[5:4] << 2) | switch_table_state`, masked by the image size.
Writes to `#7EFD` outside `MM_PROFSCORP` (and outside images > 256 KB) are inert.

**What this means in practice:** a 2 MB ROM-disk image boots exactly like a 64 KB
machine (Q0), and its terabytes-worth-of-floppies content becomes reachable only
through the paging protocol above — driven by the ROM's own software, exactly as on
hardware.

### 5.4 Shipped ROM images (in-repo, `data/rom/`)

| File | Size | Quadrants | Usable by | Notes |
|---|---|---|---|---|
| `scorpion.rom` | 64 KB | 1 | `MM_SCORP`, `MM_PROFSCORP` | classic bundle, page order BASIC 128 / 48K / Service / TR-DOS (§5.1 — `rom.cpp` currently maps it Service-first, Task 2 fix) |
| `scorp295.rom` | **64 KB** | 1 | `MM_SCORP`, `MM_PROFSCORP` | v2.95 base bundle — *not* an extended image |
| `scorp_prof401.rom` | **512 KB** | 8 | `MM_PROFSCORP` only | ProfROM 4.01 — the real extended image for E2E quadrant tests; **rejected by the current loader** (`rom.cpp` accepts only 64/128/256 KB for `MM_PROFSCORP`), so the shipped `PROFROM=rom\scorp_prof401.ROM:0` INI line has never booted |

The heritage INI syntax `PROFROM=<file>:<n>` (page/quadrant suffix, original UnrealSpeccy)
appears in `data/configs/spectrum3/unreal.ini:526`; whether the suffix is parsed or
silently breaks the path must be verified in Task 2.

### 5.5 ROM image formats accepted

- Single-file bundles: 64/128/256/512/1024/2048 KB, loaded linearly into the ROM area
  (`MAX_ROM_PAGES` must therefore be ≥ 128 pages).
- ROM-set mode (per-page files, existing `use_romset` path) continues to work for the
  base 4 pages; extended quadrants require the bundle file.
- Validation: `MM_SCORP` requires exactly 64 KB; `MM_PROFSCORP` accepts the full ladder
  (power-of-two sizes 64 KB - 2 MB).

---

## 6. TR-DOS (Beta-128) integration

- A Beta-128 interface (WD1793) is **built in and always present**; ROM3 is its entry
  ROM. FDC ports: `#1F/#3F/#5F/#7F/#FF` (status/track/sector/data/system), visible
  while a DOS session is active (`CF_DOSPORTS`).
- **`#3Dxx` trap arming (Scorpion-specific, hardware-verified via Fuse + MISTer):**
  the automatic "fetch at `#3D00-#3DFF` pages TR-DOS in" mechanism arms **only when**
  ```
  !p1FFD[0] && (p1FFD[1] || p7FFD[4])
  ```
  i.e. from ROM1 (48K BASIC — `RANDOMIZE USR 15616` path) or ROM2 (Shadow Monitor — the
  path the BASIC-128 boot menu's "128 TR-DOS" entry takes), and **never** from ROM0 —
  BASIC 128 has genuine subroutines of its own at `#3D9D-#3DE9` — nor while RAM bank 0
  occupies `#0000`.
- **ROM selection while the session is open:** ROM3 at `#0000` regardless of
  `p7FFD[4]` (§4.4 rule 3), unless `p1FFD[1:0]` outrank it (Service / RAM0).
- **Unpage semantics:** the DOS session closes when the CPU executes code from any
  RAM-mapped bank (`CF_LEAVEDOSRAM` class). The TR-DOS loader's final `JP #8018` lands
  in RAM (bank 2) and pages the ROM out — matching the reference trace `BETA UNPAGE
  PC=8018`. Fuse unpages at `PC >= #4000` (`beta_unpage`), MISTer on a fetch with
  `addr[15:14] != 0` — all three agree for real TR-DOS code paths.
- **FDC port visibility is a decoder concern, not an FDC concern.** In the original
  UnrealSpeccy `io.cpp` gates the whole WD1793 block on `CF_DOSPORTS`; in this codebase
  the equivalent already exists per decoder — `PortDecoder_Pentagon128::DecodePortIn`
  undecodes the Beta128 ports (`IsBeta128Port`, `wasBeta128Gated`) while `CF_TRDOS` is
  clear. The Scorpion decoder adopts the same pattern; `WD1793` itself stays unconditional.
- **No `#1FFD` force-session bit.** Bit 2 has no memory function on hardware (§4.3);
  sessions open only via the trap and close only via RAM execution or reset.

---

## 7. I/O port map (software-visible)

| Port | Dir | Function | Notes |
|---|---|---|---|
| `#FE` (partial decode: `A5=1, A1=1, A0=0` pattern `xxxxxxxx xx1xxx10`) | R/W | ULA: keyboard, border, ear, mic | selective decode — mirrors answer too; see §12 |
| `#FF` | W | **border color (Scorpion extension)** | direct latch, 1T visible; coexists with `#FE[2:0]` |
| `#FF` | R | Beta-128 system port (in DOS session) | FDC side/drive/reset |
| `#1FFD` | W | memory register (§4.3) | reads return `#FF`; a read also **clears the turbo flip-flop** (§13) |
| `#7FFD` | W | memory register (§4.2) | write-only; a read also **sets the turbo flip-flop** (§13) — returned value meaningless |
| `#1F/#3F/#5F/#7F` | R/W | WD1793 registers | DOS session active |
| `#BFFD` / `#FFFD` (mirror-tolerant: `#xC002` masks) | W/R | AY register/data | full mirrors decoded (`#FF05` etc. select AY) |
| `#7EFD` | W | ROM quadrant window select, bits 5:4 | ProfROM extended images only |
| `#FB` / `#DD` | W | Covox (Pentagon-style / Scorpion-style) | optional, config-gated (existing path) |
| Kempston `#1F` | R | joystick | shares address with FDC status — see §12 arbitration note |

`#7FFD` decode (GAL equation form, kept by this emulator):
`/A15 · A14 · A12 · A5 · A2 · /A1 · A0`; `#1FFD`: `/A15 · /A14 · A12 · A5 · A2 · /A1 · A0`.

---

## 8. Video subsystem

- 312 × 224T frame (69888T), INT at ZX48 position — Sinclair-matching.
- Display file always in RAM bank 5 (normal) or 7 (shadow) — `#7FFD[3]`.
- Discrete-logic fetch: no shift/dead cycles; per 4T cell, phases 0-1 pixel byte,
  phases 2-3 attribute byte (`ULA_DISCRETE_LOGIC`).
- Border: `#FE[2:0]` Sinclair-matching **and** `#FF` direct port; power-on latch value
  is 0 (black) — v2.9x ROMs never write `#FF` during boot, so the machine shows a black
  border until software sets one (MISTer-verified).
- No ULA+ on base hardware (config-gated extension may stay enabled as an emulator
  convenience, off by default for this model).

---

## 9. MNI — the "Magic" button

> Rewritten 2026-09-10 to the DD50 ground truth (verified against the schematic and the
> v4.01 image). The previous text claimed the button sets `#1FFD` bit 1 — disproven: no
> register is written. Full disassembly and evidence:
> [profrom-nmi-boot-analysis.md](profrom-nmi-boot-analysis.md).

The button arms **two DD50 flip-flops at once** and writes no register:

| Flip-flop | Effect while armed |
|---|---|
| DD50.2 (NMI trigger) | asserts `/NMI` — the CPU accepts it at the next instruction boundary |
| DD50.1 ("1-DOS / 0-SOS" DOS trigger) | **page 3 (TR-DOS) of the current ProfROM plane forced over `#0000-#3FFF`** — the Beta-128 magic-button mechanism |

- Neither the `#1FFD` latch nor the ProfROM plane register (GAL DD41) is touched: the
  plane survives the whole session (it moves only via the `#0100+4·S` read strobe, §5.2
  — the button and `/RESET` are not wired to the GAL), and the interrupted program's
  banking state survives verbatim.
- Priority while armed: `#1FFD[1]` (service latch) **still outranks** the trigger —
  the firmware entry chain relies on this (its `OUT (#1FFD),#12` at TR-DOS `#0033`
  swaps the service page in mid-chain; both pages carry compatible code at `#0033`).
  The trigger also overrides RAM-at-`#0000` (`#1FFD[0]`) and any session selection.
- The CPU vectors to `#0066` **of the forced page 3**. In plane 0 the TR-DOS handler
  chains into the Service Monitor (`#0066 → #2A56 → #0807 → OUT (C),A at #0033 →
  service #0035 → #00B6` → menu; the monitor saves context in `#DDxx` RAM, restores
  `#7FFD`/`#1FFD` on exit and `RETN`s). In planes 1-3, `#0066` of pages 2 and 3 is a
  **deliberate park loop** (`LD A,6 / OUT (#FE),A / XOR A / OUT (#FE),A / JR #0066` —
  yellow/black border stripes, no exit): the monitor would clobber the running tool's
  `#DDxx` data, so NMI in a tool plane parks the CPU instead (analysis doc §3-4).
- The trigger **releases on the first CPU read from `#4000-#FFFF`** (the Beta-128
  "leave the ROM window" strobe; writes never release it).
- `/RESET` clears the trigger but **not** the plane register — "plane 0 after reset"
  is a software guarantee of the per-plane `#0000` stubs (§5.2, analysis doc §5).

---

## 10. Sound

- AY-3-8910/12 at `#FFFD`/`#BFFD` (with mirror-tolerant decode — TurboSound second-chip
  detection depends on it).
- Beeper via `#FE[4]`; Covox options at `#FB`/`#DD` (config-gated, existing path).

---

## 11. Keyboard

58-key full-size matrix (own layout, extension-port driven) — **functionally**
compatible with the 40-key Sinclair matrix mapping for all standard software. The clone
uses the standard matrix; key *positions* on real hardware differ. Not a compatibility
factor for software.

---

## 12. Known divergences, approximations, open questions

| # | Item | Status |
|---|---|---|
| 1 | `#FE` selective decode exactness (`A4,A3,A1,A0` per bootcamp vs. the GAL equation currently encoded) | kept as documented GAL equation; exact Turbo+ netlist unavailable |
| 2 | MNI flag port | **Resolved 2026-09-10: none exists.** The button is a trigger pair (DD50.1 DOS trigger + DD50.2 NMI, §9) — the earlier "ROM0 NMI handler polls a flag port" concern came from the disproven latch model |
| 3 | Kempston vs. Beta `#1F` arbitration | on hardware the FDC owns `#1F` only in a DOS session; the monitor polls `#xx1F` *after* unpagin — decoder must keep the FDC answering in that state (MISTer bug 2; see design.md) |
| 4 | ProfROM state machine on *any read* vs. M1-only | original hardware watches `/RD` + ROMCS + A0-A1; implemented as any read — **required, not just permitted**: the shipped ProfROM image's reset fetch needs operand (non-M1) reads to drive the machine (§5.2), and quadrant switches mid-instruction mean operand bytes are fetched from the newly-selected quadrant — the read-strobe hook must remap before the current access completes |
| 5 | 512 KB ProfROM ("SMUC support" per UnrealSpeccy docs) | accepted by the ladder via `#7EFD[4]`; the 2-bit state machine alone stops at 256 KB — consistent with original source |
| 6 | `#FF` port read behavior outside DOS sessions | returns `#FF` (open bus); write-only border latch. Heritage divergence: original UnrealSpeccy `io.cpp` composes the Scorpion `#FF` read as `res = (res & 0x1F) \| (wd.in(0xFF) & 0xE0)` — the Beta-128 system port drives bits 7:5 over the floating-bus low bits. Not implemented; revisit if software is found that reads `#FF` for drive/side status outside a session |
| 6b | `#FE` selective decode during a DOS session | heritage `io.cpp` applies the Scorpion `(port & 0x23) == 0x22` pattern **only** while `CF_DOSPORTS` is clear, falling back to the generic `#FE` decode inside a session. Harmless in practice — every FDC port is odd (`A0 = 1`) and the `#FE` pattern requires `A0 = 0`, so the two can never collide — but the codebase's unconditional `IsPort_FE` is a documented simplification |
| 7 | even-M1 timing correction (`TEMP::evenM1_C0`, "C0 for scorpion") | dormant heritage field from original UnrealSpeccy; optional accuracy follow-up after timing tests pass |
| 8 | Border power-on value | 0 (black) per MISTer FPGA power-up state — a deliberate deviation from this emulator's generic white reset, justified by boot-screen fidelity |
| 9 | `#1FFD` bit 2 | **Decided 2026-09-08: not a memory bit — not implemented.** Hardware (programmer's guide): RS-232C output line. Fuse and MISTer ignore it. Original UnrealSpeccy: `if (p1FFD & 4) flags \|= CF_TRDOS` — *set-only and sticky*: clearing the bit did nothing, the session then closed via the normal RAM-execution path. Rejected because no real-hardware software can depend on it and stray writes with bit 2 set would wrongly page TR-DOS in. If UnrealSpeccy-compat is ever wanted, re-add exactly the sticky set-only semantic as a documented extension |
| 10 | ROM under an open DOS session with `p7FFD[4] = 0` | **Normative: ROM3 (TR-DOS)** per MISTer/Fuse. Heritage UnrealSpeccy / generic `UpdateZ80Banks()` map the service ROM here — the Scorpion branch must not inherit that (§4.4) |
| 11 | ProfROM quadrant state | **Decided 2026-09-08: kept in `EmulatorState::profrom_bank` and checkpointed** (TTD chipset state + divergence hash). Not derivable from latches (§5.2). The `.z80` format has no slot for it — snapshots of a ProfROM machine mid-walk reload at quadrant 0 (documented limitation) |
| 12 | Turbo DRAM/video arbitration | not modeled: 7 MHz runs as an ideal 2× T-states per frame (all reference emulators do the same — xpeccy doubles `cpuFrq`, ZXMAK2/UnrealSpeccy have no Scorpion turbo at all). Real machines lose a few % to stretched cycles in screen-heavy loops (§13). Tape auto-adapts to the multiplier like the host speed control, whereas real hardware would break tape timing loops in turbo — pragmatic UX choice, revisit if a tape-turbo incompatibility is reported |

---

## 13. Hardware turbo — the 7 MHz flip-flop

Source: `materials/Scorpion_Turbo_Mode.md` (turbo.jed GAL decode + MAME model),
cross-checked against xpeccy `scrpIn1FFD`/`scrpIn7FFD`/`compSetTurbo`.

**Software-visible contract.** The turbo flip-flop is clocked by IORQ **reads**
whose address matches the paging-register decode — `IN` from the `#7FFD`
family sets it (7 MHz), `IN` from the `#1FFD` family clears it (3.5 MHz):

```
set   (port & 0xC023) == 0x4021   01xxxxxxxx1xxx01  (#7FFD, and #7EFD on ProfROM)
clear (port & 0xC023) == 0x0021   00xxxxxxxx1xxx01  (#1FFD, #3FFD...)
```

The strobe is a pure address decode — it fires whether or not a device claims
the read, the data bus stays undriven (returned value meaningless), and every
address mirror clocks it. `RESET` clears it. There is **no read-back status
bit** on the ZS-256 (programs measure the speed with an interrupt-bounded
count loop; the GMX adds a status bit in `#7EFD`, the ZS-256 does not).

**What runs faster.** Only the Z80 and everything it times by instruction
counting. The 50 Hz frame interrupt, the AY (own 1.75 MHz clock), the FDC
(own 1 MHz + PLL) and video timing are unaffected — in emulator terms the CPU
executes 2× T-states per 50 Hz frame with the INT window scaled accordingly.

**Emulator model** (`EmulatorState::scorpion_turbo`, applied in
`Z80::ApplyQueuedFrequencyMultiplier`):

- `PortDecoder_Scorpion256::DecodePortIn` performs the strobe for **every**
  Scorpion configuration (`MM_SCORP` and `MM_PROFSCORP`) before the normal
  decode chain — result bytes are served exactly as before.
- The effective multiplier composes with the host speed control
  (`next_ << turbo`) at the frame boundary: `Z80FrameCycle` entry and the
  inline boundaries of the `Emulator` stepping paths all call the same apply,
  so a mid-run `IN` rescales subsequent frames and the queued host setting is
  never clobbered by guest code.
- Every consumer that already divides by `current_z80_frequency_multiplier`
  (screen descale, sound pacing, tape timing, INT position) inherits the
  correct behavior without further changes.

**Verification.** Unit: strobe truth table (family mirrors set/clear, unrelated
ports inert, `#FF` results unchanged, reset clears) + real-CPU-path
`ScriptedInSevenFFD` + frame composition (`host 4× × turbo = 8×`, turbo off
returns to the host setting). Live: `scratch/e2e-profrom-boot/pass29.py` —
139776 T-states span 2 frames at 3.5 MHz and 1 frame after `IN (#7FFD)`, back
to 2 after `IN (#1FFD)`, on both `SCORPION`/256K and `PROFSCORP`/1024K.
