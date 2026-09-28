# Spec256 Implementation Survey — Reference Sources

Line references are to the local emulators collection clone of each project
(paths relative to the collection root, e.g. `github/zxpoly/…`). This is the
"how do working implementations actually do it" companion to
[spec256-format-and-mechanics.md](spec256-format-and-mechanics.md).

## 1. zxpoly (Java) — the primary reference

Spec256 in zxpoly (v2.0.1+) is a **board mode**: `BoardMode.SPEC256` with one
ordinary ZX module (module 0, the master) plus **8 GFX cores** that together
emulate the 64-bit Z80_GFX. Loading a Spec256 ZIP writes `#3D00 ← 0x81`
(isolate CPU0), sets the board mode, resets, and switches the video mode
(`Snapshot.java:57-89`, `FormatSpec256.java:316-319`).

### Container (Spec256Arch.java)

`github/zxpoly/zxpoly-emul/src/main/java/com/igormaznitsa/zxpoly/formats/Spec256Arch.java`

- ZIP members recognized: `.sna` (mandatory), `.gfx` (mandatory, exactly
  `0x4000*8*3` = 0x60000), `.gf0`–`.gf7` (0x20000 each), `.gfa`/`.gfb` (ROM
  planes; historical `rom0.gfx`/`rom1.gfx` → ROM pages **1/0**), `.b00`…
  (320×200 backgrounds), `.pal`/`.p00`… (length/3 RGB triplets), `.cfg`
  (Properties), `.xor` (parsed; renderer never uses it) — lines 26–33,
  84–157.
- `.gfx` page order: **RAM page 5, RAM page 2, top page** (`#7FFD & 7` for
  128K, page 0 for 48K) — lines 186–204.
- Archive identity = `sha256(sna + gfx)` (lines 211–216) → key into the
  per-game profile DB.

### Plane codec (the format's core transform)

`FormatSpec256.decodeGfx()` — `FormatSpec256.java:87-106` — file→memory:

```java
for (int offset = 0; offset < dataLen; offset += 8)
  for (int ctx = 0; ctx < 8; ctx++) {          // ctx = plane/core index
    int accumulator = 0;
    for (int i = 0; i < 8; i++)                // i = file byte index
      if ((gfxData[offset + i] & (1 << ctx)) != 0) accumulator |= 1 << i;
    result[offset + ctx] = accumulator;        // per-plane byte for this address
  }
```

Read it as: *bit `ctx` of on-disk colour byte `i` becomes bit `i` of plane
`ctx`'s memory byte*. In-memory layout is one normal-looking 8-pixel byte per
plane (bit-planes); on-disk layout is 8 consecutive palette indices per bitmap
byte. Inverse: `Spec256GfxPage.packGfxData()` — `Spec256Arch.java:363-376`
(used on snapshot save-back, `FormatSpec256.java:153-214`).

### GFX memory model (ZxPolyModule.java)

`github/zxpoly/zxpoly-emul/src/main/java/com/igormaznitsa/zxpoly/components/ZxPolyModule.java`

- `gfxRam = byte[128K * 8]` (1 MB), `gfxRom = byte[32K * 8]` (256 KB) —
  lines 44, 56–57.
- `readGfxMemory` / `writeGfxMemory` (549–578, 636–656): address translation
  `$0000 ROM + #7FFD.4`, `$4000 page 5`, `$8000 page 2`, `$C000 #7FFD&7`;
  every access lands at `page*0x20000 + (offsetInPage << 3) + coreIndex`.
  Dispatch: `ctx == 0` → ordinary ZX heap, `ctx != 0` → GFX plane (806–815).
- `readGfxVideo` (371–394): renderer fetch — screen page from `#7FFD` bit 3
  (page 5/7); returns the combined 64-bit word for one bitmap byte; pixel *i*
  uses mask `1 << (7 − i)`.

### Lockstep scheduler (Motherboard.java)

- `SPEC256_GFX_CORES = 8` GFX cores cloned from module 0's CPU (63, 186–188).
- Per step (510–521): master saves INT/NMI/WAIT snapshot
  (`saveInternalCopyForGfx`), then for each core `alignRegisterValuesWith(mainCpu,
  profile)` + `gfxGpuStep(i+1, core)`; finally the master itself steps.
- Register-alignment machinery + `T`-profile pointer override:
  `ZxPolyModule.java:826–866`, `925–964`.
- Correction options `GFXLeveledXOR/OR` → `max`, `AND` → `min` on colour
  bytes: `ZxPolyModule.java:925–964`; defaults & UI:
  `Spec256ConfigEditorPanel.java:86–108`.
- Per-game profile DB: `zxpoly-emul/src/main/resources/spec256appbase.txt`
  (e.g. `jetpac,d2b7…,zxpAlignRegs=1PSsT`,
  `scoobydoo256,…,zxpAlignRegs=1HLXxYyPSs`).

### Palette & renderer (VideoController.java)

- Fixed built-in 256×RGB888 palette loaded from a 768-byte resource
  `pal/spec256.raw.pal` (114–116; reader `Utils.readRawPalette` 137–162,
  `0xFF000000 | R<<16 | G<<8 | B`). Archive `.pal/.pnn` are parsed but
  **not applied** — rendering always uses the built-in table (456).
- `fillDataBufferForSpec256VideoMode` (413–525): iterates standard ZX screen
  addresses; per byte takes `readGfxVideo()` (8 indices) **and** the ordinary
  attribute byte; composites: `Paper00InkFF`, `HideSameInkPaper`, background
  image underlay, and RGB averaging of indices below/above
  `Down/UpColorsMixed` with the ZX ink colour (`mixRgb` 398–411). Output
  canvas 512×384 (2× pixels).
- Backgrounds: `setGfxBack` (352–373) pre-renders the centred 256×192 window
  of a 320×200 `.bnn`.
- Video-mode constants: `VIDEOMODE_SPEC256 = 8`, `VIDEOMODE_SPEC256_16 = 9`
  (`ZxPolyConstants.java:57-58`); the `_16` variant is wired for palette
  selection only — dead code, evidence of the never-shipped 16-plane mode.

### Tests / fixtures (gold for us)

`zxpoly-emul/src/test/java/…/formats/Spec256ArchTest.java` against real
archives in `src/test/resources/snapshots/`:
`Renegade_spec256.zip` (48K, 1 GFX ROM, 3 GFX pages, 4 backgrounds),
`Jetpac_spec256.zip` (48K, `rom0.gfx` variant), `TreeWeeks128k_spec256.zip`
(128K, 8 GFX pages, no ROM GFX). These double as byte-exact codec fixtures
for any reimplementation.

### Sprite-corrector exporter (cross-format precedent)

`zxpoly-sprite-corrector/…/files/plugins/Spec256ZipPlugin.java:112-201`:
converts ZX-Poly 4-CPU bitplanes → Spec256 index bytes
(`cpu3→0x08 | cpu0→0x04 | cpu1→0x02 | cpu2→0x01`), indices 0→0x00 / 0xF→0xFF,
others nearest-colour via `MAP_ZXPOLY2SPEC256INDEX`. Relevant if unreal-ng
ever unifies ZX-Poly and Spec256 colourization tooling.

## 2. GZX (C)

Renderer-first implementation: `video/spec256.c` builds the 256-colour frame
from shadow memory, `sp256.pal` (768 decimal RGB triplets as text) holds the
default palette, background loader mirrors EmuZWin semantics. Useful as a
second opinion on byte order and palette.

## 3. oozx (fpetrola)

Runs "nine Z80s in lockstep" (main + 8 GFX) — same architecture as zxpoly with
independent code; its docs summarize the model well
(`doc/wiki/Spec256-256-Colours.md`, `doc/plan-spec256.md`) and report 29
public releases rendering at 95–100 % accuracy — good evidence that the
8-core lockstep model, with per-game profiles, reproduces the catalog.

## 4. FPGA cores (ReVerSE-U16 → DivGMX → karabas-go)

Hardware proof the model is synthesizable: U16 initially used 8 physical T80s
(one per plane) at 3.5/7 MHz, later collapsed to a single wide "GFX_Z80";
DivGMX/karabas-go carry the single-GFX-CPU design plus the background layer.
The karabas-go-tools `spec256.py` loader is a compact, readable format
validator (SNA size, GFX size, page concatenation).

## 5. EmuZWin (Object Pascal)

Not available locally ⚠; semantics taken from its shipped HTML docs
(`256_color_games.htm` — file formats, options, content constraints;
`EZXFormat_Eng.htm` — `.ezx` chunk records) and the addon tool source
`Bmp2RawBk256.dpr` (default palette definition). EmuZWin remains the only
implementation with a real-time **GFX Editor** — the feature to study for
unreal-ng's debugger/tooling angle.
