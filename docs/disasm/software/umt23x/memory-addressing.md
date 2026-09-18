# UMT v2.3x — how every supported clone addresses extended RAM

Reverse-engineering notes for the universal memory tester shipped on
`testdata/memory/UMT23X.tap` (a real 2021 tape image). Everything below was
extracted from the unpacked program; regenerate the evidence with
`disasm_umt23x.py` (see [README.md](README.md)) and follow along in
[umt23x-z80dasm.asm](umt23x-z80dasm.asm).

- [The common framework](#the-common-framework)
- [Per-model port / bit map](#per-model-port--bit-map)
- [Pentagon 1024 deep dive](#pentagon-1024-deep-dive)
- [How UMT tests memory](#how-umt-tests-memory)
- [How to test memory yourself](#how-to-test-memory-yourself)
- [Emulator parity (unreal-ng)](#emulator-parity-unreal-ng)
- [Sources](#sources)

---

## The common framework

All tested machines expose extra RAM as **16 KiB pages mapped into the
`0xC000..0xFFFF` window**. UMT never moves code or data into that window —
it only writes test patterns through it — so the same test engine works on
every clone; only the "map page N" step differs.

Per-clone knowledge lives in exactly one table. `MODEL_TABLE` at `0x6F40`
holds 15 entries of 27 bytes:

```
[class byte][w0][w1][w2][w3][w4][16-char name]     ; words are little-endian
```

| slot | routine family | purpose |
|------|----------------|---------|
| w0 | `MAP_*` (`0x60C6..0x61D2`) | map page `PAGE_NUM` (`0x60A6`) into `0xC000` |
| w1 | `MARK_*` (`0x6354..0x638C`) | page-number uniqueness sweep |
| w2 | `PAT_*` (`0x62F8..0x6327`) | `00/55/FF/AA` pattern test |
| w3 | `SUM_*` (`0x643C..0x6475`) | chained-checksum test |
| w4 | `ROT_*` (`0x653A..0x6593`) | rotate-carry test |

The w1..w4 entries are short **page-count ladders**: each begins
`LD B,<npages>` (`8`, `10h`, `20h`, `40h`, `80h`; `0` means 256 via `DJNZ`
wrap) and jumps into one shared body — so "which entry a model points to"
encodes its RAM size, nothing more.

Dispatch is a three-instruction idiom, repeated for each slot
(`TABLE_WORD` at `0x60AD` walks 27-byte strides):

```asm
CALL_W0:  ld hl,0x6F41      ; table + model*27 + slot 0
          call TABLE_WORD
          jp (hl)           ; -> the model's MAP_*
```

**There is no clone auto-detection anywhere in the program.** The only port
read in all 9 KiB of code is `IN A,(0xFEh)` — the keyboard. You pick the
model in the menu (`SELECT_MODEL` at `0x68D5`: page "1024-4096K" keys 1-9
and `X` for GMX, page "128-512K" keys 1-5 with a +10 offset), UMT stores the
index at `MODEL_INDEX` (`0x6F3F`) and trusts you.

## Per-model port / bit map

`pb` = bit of the 16 KiB page number. All models keep `pb0..pb2` in `#7FFD`
bits 0..2 (the standard 128K position) except the two "raw page" machines.
`bit4` of `#7FFD` is forced to 1 by every latch-style model (screen stays in
the bank-7 area; avoids side effects of bit 3).

| # | model (UMT name) | RAM | page bits | paging ports and bits |
|---|------------------|-----|-----------|----------------------|
| 0 | Pentagon1024 | 1 MiB | 6 (pb0..pb5) | `#7FFD` = pb0..pb2, bit4=1, **bit5=pb5**, bit6=pb3, bit7=pb4 |
| 1 | Scorpion1024 | 1 MiB | 6 | `#1FFD` bit4=pb3, bit6=pb4, bit7=pb5; `#7FFD`=(p&7) or 10h |
| 2 | KAY1024 | 1 MiB | 6 | `#1FFD` bit4=pb3, bit7=pb4, bit6=pb6(n/a); `#7FFD`=(p&7) or 10h, bit7=pb5 |
| 3 | KAY2048 (Phoenix) | 2 MiB | 7 | same as KAY1024 + `#1FFD` bit6=pb6 |
| 4 | Profi1024 | 1 MiB | 6 | `#DFFD` = p>>3 (bits 0..2); `#7FFD`=(p&7) or 10h |
| 5 | ATM4.5(1024) | 1 MiB | 6 | `#FDFD` = p>>3; `#7FFD`=(p&7) or 10h |
| 6 | ATM7.1(1024) | 1 MiB | 6 | ProfROM service: `#FD77`=0ABh, `JP 3D2Fh`, then `#FFF7`=(~p&3Fh) or 40h, `#FF77`=0ABh |
| 7 | PentEvo (TSConf) | 4 MiB | 8 | `#13AF` = raw page |
| 8 | Sprinter (4096K) | 4 MiB | 8 | `#00E2` = raw page (UMT-observed) |
| 9 | GMX (2048) | 2 MiB | 7 | `#DFFD` = p>>4; `#1FFD` bit4=pb3; `#7FFD`=(p&7) or 10h |
| 10 | Scorpion256 (KAY) | 256 KiB | 4 | `#1FFD` bit4=pb3; `#7FFD`=(p&7) or 10h |
| 11 | Pentagon512 | 512 KiB | 5 | `#7FFD` as Pentagon1024, only pb0..pb4 wired |
| 12 | Profi512 | 512 KiB | 5 | `#DFFD`=p>>3; `#7FFD`=(p&7) or 10h |
| 13 | ATM4.5(512) | 512 KiB | 5 | `#FDFD`=p>>3; `#7FFD`=(p&7) or 10h |
| 14 | Spectrum 128K | 128 KiB | 3 (8 RAM pages) | `#7FFD` bits 0..2, bit4=1 (MAP_PENTAGON; extra bits harmless) |

The "class byte" stored in each table entry matches the page-bit count:
`04`=256K, `05`=512K, `06`=1024K, `07`=2048K, `08`=4096K; the 128K entry
uses `0Ah`.

### Notable implementations

**MAP_PENTAGON (`0x6169`)** — one port, a twist in the middle:

```asm
ld a,(PAGE_NUM)
ld d,a
and 7                 ; e = pb0..pb2
ld e,a
ld a,d
and 038h
sla a                 ; pb3 -> bit6
sla a                 ; pb4 -> bit7
sla a                 ; pb5 -> shifted out into CY!
jr nc,skip
set 5,a               ; pb5 re-enters as bit5
skip: ld bc,07FFDh
or e
or 010h               ; bit4=1: keep the screen convention
out (c),a
```

**MAP_KAY (`0x60FE`)** — the only routine with self-modifying code. The
`or 10h` at `0x6141` is patched to `or 90h` when pb5 is set (`#7FFD` bit7),
and the `or 00h` at `0x6136` to `or 40h` when pb6 is set (`#1FFD` bit6) —
KAY spreads six page bits over two ports plus one SMC patch site.

**MAP_GMX (`0x61A2`)** — reuses the Scorpion tail (`jr 615Bh`) for its
`#1FFD` bit4, and the Profi body shape (`srl a` x4) for `#DFFD`; a
three-port hybrid.

**MAP_ATM71 (`0x60D8`)** — the odd one out. The ATM turbo 1 hardware hides
the page latch behind the ProfROM monitor:

```asm
call ATM71_MODEIN      ; out (#FD77),0ABh; push 2A53h; jp 3D2Fh
ld a,(PAGE_NUM)
cpl
and 03Fh               ; complemented page!
or 040h                ; bit6 = RAM page (vs ROM)
ld bc,0FFF7h
out (c),a
jr ATM71_MODEOUT       ; out (#FF77),0ABh
```

The `#FD77`/`0ABh` + `JP 3D2Fh` pair enters the ProfROM service (the pushed
`2A53h` is consumed by the service routine), and only then does `#FFF7`
accept the latch — with the page number **inverted**, a hardware wiring
quirk. Every page switch pays this firmware round-trip, which is why UMT's
sweeps are noticeably slower on model 6.

**Sprinter exclusions** — the w1/w2 entries for Sprinter
(`STEP_SPRINTER` `0x63B0`, `VERIFY_STEP_SPRINTER` `0x63C3`) skip page `40h`
exactly and pages `50h..5Fh` (ROM/VRAM shadows): they are not writable RAM,
and marking them would produce false failures.

## Pentagon 1024 deep dive

Pentagon 1024 is a hand-built upgrade of Pentagon 128 — no factory, no
single schematic — so UMT's decode is effectively the de-facto standard.
It matches the classic write-up in Born Dead #10 (Alone Coder, 2002; see
[Sources](#sources)):

`#EFF7` (write, decoded loosely on A3/A12/IOWR, reset by RESET):

| bit | meaning |
|-----|---------|
| 0 | "attribute per byte" (a4b) hardware multicolour, 1 = on |
| 1 | 512x192 video mode |
| 2 | **memory-above-128K latch: 0 = extension present, 1 = absent** |
| 3 | unused |
| 4 | GigaScreen (hardware screen overlay) |
| 5, 6 | reserved for Rom-Disk |
| 7 | Gluk CMOS clock enable |

Two consequences for the `#7FFD` byte:

1. **bit 5 (the "48K lock") is free** on machines with the extension: the
   lock only latches while the extension is disabled (`#EFF7` bit 2 = 1).
   That is why UMT can use `#7FFD` bit 5 as **pb5 (A18)** — with the
   extension enabled the bit goes to the address decoder instead of the
   lock. This dual use is the single most surprising line of `MAP_PENTAGON`.
2. **bits 6 and 7 are wired in parallel to `#DFFD` bits 0 and 1** for
   compatibility with Profi-1024 software. Writing `#7FFD` alone (what UMT
   does) is the faster variant and reaches the same RAM.

The same article gives the standard recipe for telling a Pentagon 512 from
a 1024 in software, which UMT deliberately does **not** implement: flip
`#EFF7` bit 2 — if the machine reacts, the extension exists; then clear the
bit, which guarantees the 48K lock is armed again, and probe the extra
pages through `#7FFD` bits 5..7.

## How UMT tests memory

Four methodologies per model, each a `w1..w4` sweep over all pages. Results
land in a per-page character grid on screen (`SELECT_ROW` `0x6416` maps
pages `00..3Fh` to row `5880h`, `40h..7Fh` to `5898h`, `80h..BFh` to
`5890h`, `C0h+` to `5888h`).

1. **Mark** (`MARK_*`): for every page — map it, write its own number to
   `0xC000`, map the next, ... then a verify pass reads each page back and
   must see its number. Catches **address-line shorts** (two pages selecting
   the same physical RAM — a classic fault on hand-soldered 1024K upgrades)
   and completely dead banks.
2. **Pattern** (`PAT_*` → `FILL_BY_PUSH` `0x629E` + `VERIFY16` `0x61D2`):
   fill each page with `00`, `55`, `FF`, `AA` (the SP-trick filler pushes BC
   pairs — the fastest possible fill), verify each with a 16x-unrolled
   `CP (HL)` compare. Cell-level **pattern sensitivity** (stuck-at-0/1,
   coupling inside a page). Failures report as `B`/`C`/`D` per pattern;
   untestable pages (UMT's own screen/system areas) report `F`.
3. **Checksum** (`SUM_*` → `CHECKSUM_WRITE` `0x64A3` / `CHECKSUM_VERIFY`
   `0x64DC`): a 256-byte table at `0x7700` is seeded once from the ROM
   image (`ROM[0..0FEh]`, `INIT_CHECKSUM_TABLE` `0x670F`); each page must
   transform the chain `A = table[L] + table[L+1Fh]` → `table[L+37h]` and
   hold its result. Because the seed differs per position and the chain
   mixes neighbours, this catches **cross-page coupling** and data-dependent
   faults the fixed patterns miss. The `0x5C00` system area is saved and
   restored around the run.
4. **Rotate** (`ROT_*` → `ROTATE_PAGE` `0x65A2`): `RR (HL)` chains rotate
   the whole page right one bit, three sweeps with verification. A single
   stuck bit or an open carry chain breaks the shifted-out pattern —
   catches **stuck bits and shift-path faults** the read-modify-write
   pattern test can alias.

Layered this way, the four tests are close to orthogonal: selection faults,
intra-page pattern faults, inter-page data coupling, and bit-shift faults.

## How to test memory yourself

**On real hardware or in the emulator, just run UMT**: boot the tape,
select your model, read the grid. For emulator models in unreal-ng, use
`PENTAGON` (Pentagon 1024/512/128K rows), `SCORPION` (Scorpion rows) or
`PROFI`; the other rows target machines unreal-ng does not emulate yet.

**Rolling your own tester** (what to copy from UMT):

1. Keep code, stack and screen out of the window under test. UMT sits at
   `0x6000..`, tests only through `0xC000..`, and temporarily moves SP for
   the fill (`SP=dest` push-fill) rather than using `LDIR` from low memory.
2. First pass: **page numbers as data** (the mark test) before any pattern
   test — if two pages alias, every later result is garbage.
3. Verify patterns **in a second mapping pass**, not right after writing:
   re-map the page (many faults only show when the latch is re-loaded).
4. Patterns `00/55/FF/AA` are the cheap quadrant; add a position-dependent
   value (page number, address low byte) for coupling; add a rotate or
   shift pass for stuck bits.
5. For ATM 7.1-style firmware-paged machines, budget for the ProfROM call
   per page switch — don't put the map routine inside a tight inner loop
   you also timing-depend on.
6. Skip known non-RAM pages explicitly (Sprinter `40h`, `50h..5Fh`).

**Inside unreal-ng (automation)**, two approaches work today:

- Scripted: start an instance, then for each page write the paging port and
  read/write `0xC000..0xFFFF` through the WebAPI/Lua/Python bindings and
  compare against expectations — the same layering as above, expressed as
  API calls (`/ports` to observe the latch state, memory read/write
  endpoints for the window).
- Direct: inject `umt-unpacked-6000.bin` at `0x6000`, set PC to `0x6000`
  (exactly how the ground-truth capture for this package was made), and
  let UMT itself drive the machine while your harness screenshots or
  scrapes the results grid.

## Emulator parity (unreal-ng)

- **Scorpion 256/1024 — exact match.** `core/src/emulator/ports/models/
  portdecoder_scorpion256.cpp` implements `pb3 -> #1FFD bit4`,
  `pb4/pb5 -> #1FFD bits 6/7` (`(page & 0x30) << 2`), byte-identical to
  UMT's `MAP_SCORPION`.
- **Pentagon 1024 — complete.** The decoder (`portdecoder_pentagon1024.cpp`)
  implements full 6-bit bank selection: `value&7 | (value&0xC0)>>3 | (value&0x20)`,
  gated by `#EFF7` bit 2. Per Born Dead #10, **`#7FFD` bit 5 serves as pb5/A18**
  when extended memory is enabled (bit 2 = 0), not as the paging lock. The
  conditional lock logic ensures UMT can cycle through all 64 pages without
  tripping the 48K lock. `#DFFD` parallel wiring is not yet implemented.
- **Profi** `#DFFD` is present; **KAY / GMX / ATM 7.1 / TSConf / Sprinter**
  are not emulated — UMT is currently the best machine-readable reference
  for their paging (ports `#13AF`, `#00E2` documented as UMT-observed).

## Sources

- The program itself: `umt23x-z80dasm.asm` (this folder) — all `MAP_*`
  routines quoted above are at the listed addresses.
- Born Dead #10, "IRON MADE IN..." (Alone Coder / Invaders 8, 2002):
  Pentagon 1024 history, the `#EFF7` bit map, the `#7FFD` bit-5 dual use,
  the `#DFFD` bit 0/1 paralleling, and the 512-vs-1024 detection recipe —
  <https://zxpress.ru/ru/ezines/born-dead/10/tehnicheskie-podrobnosti-kompyuterov-semeystva-pentagon-osobennosti-pentagon-1024-upravlenie>
- TS-Conf documentation (the `#13AF`/`#xC000` window paging), e.g.
  <https://hype.retroscene.org/blog/ts-conf/320.html>
- tslabs.net forum thread on >1 MiB paging conventions: "Profi/ATM-1 +
  Pentagon — port `#DFFD`/`#FDFD` bits 0,1,2 and port `#7FFD` bits 6,7 —
  up to 4096 Kb".
