# Scorpion ROM disassembly

Annotated disassemblies of the **service monitor** (the machine-code
debugger) found in the Scorpion ZS-256 Turbo ROM bundles shipped with this
project.  Three ROM files, three monitor versions, one shared code base.

| ROM bundle                | Monitor version | Listing                                         |
|---------------------------|-----------------|-------------------------------------------------|
| `data/rom/scorpion.rom`   | v2.x ("base")   | [base/scorpion-monitor-p2.asm](base/scorpion-monitor-p2.asm) |
| `data/rom/scorp295.rom`   | v2.95           | [base/scorpion295-monitor-p2.asm](base/scorpion295-monitor-p2.asm) |
| `data/rom/scorp_prof401.rom` | v4.01 ("ProfRAM") | [prof/scorpion-prof401-monitor-p2.asm](prof/scorpion-prof401-monitor-p2.asm) |

Each listing is byte-complete: the instruction/`defb` lines reconstruct
their 16 KiB page exactly (16384/16384 bytes verified per file).

## The ROM bundles

Each 64 KiB file is a bundle of four 16 KiB pages.  Page numbers below are
**offsets inside the bundle file** (`p0` = `$0000-`, `p1` = `$4000-`,
`p2` = `$8000-`, `p3` = `$C000-`), NOT the emulator's slot numbering:

- **p0** - patched standard 128K ROM0 (~290 bytes differ from `128.rom`;
  TR-DOS autorun patches).
- **p1** - patched 48K BASIC ROM (~115 bytes differ from `sos.rom`; the
  same autorun patch family).
- **p2** - the **service monitor** disassembled here (machine-code
  debugger with memory/register/disassembly views, breakpoints and
  watchpoints).
- **p3** - TR-DOS 5.03 variant (strings identical to `dos.rom`, ~4.7K of
  bytes differ - Scorpion adaptations).

The service monitor is a self-contained 16K block: the patched pages are
standard ROMs with autorun glue only.  Note that
`core/src/emulator/memory/rom.cpp` maps `MM_SCORP` slots as
`sys=p0 / dos=p1 / 128=p2 / sos=p3` - inverted with respect to these
bundle offsets.

## The three monitor versions

- **v2.x (base)** - the reference.  All labels and comments in the other
  two listings were derived from it.  RST 18h = RAM-extension call,
  RST 30h = set IX work buffer.
- **v2.95** - shares the low core with the base byte for byte (only the
  RST 10h vector target differs); relocates most routines above `$0A54`,
  inlines the TR-DOS boot and adds a boot-time ROM checksum walk.
- **v4.01 (ProfRAM)** - a heavier rewrite: RST 18h becomes an inline hex
  digit parser, RST 30h a RAM hook (`JP $E3D3`) whose call sites carry
  inline argument bytes; commands are tokenized through a linked list of
  `{next, token, name, handler}` records at `$23A3`; the RAM workspace is
  relocated (window descriptors shift +`$40`, stacks/pointers into
  `$E3xx`, tables into `$E5xx-$EAxx`); breakpoint UI, tape writer and
  print hook of the base are gone.

The per-listing file headers carry the full RST API and RAM workspace map
of that version, in Logan & O'Hara educational style.

## Addresses

All addresses in the listings are **offsets within the 16 KiB page**
(z80dasm origin `$0000`), i.e. `addr - $8000` of the bundle file.  RAM
equates refer to bank 0 at `$C000-$FFFF`, visible while the monitor runs
(`#7FFD` bit 4 = 0).

## How these were made

The full generation pipeline is preserved in [scripts/](scripts/) next to
the listings (it needs `z80dasm` 1.2.0 on PATH and Python 3):

0. `extract.py` - slices the monitor pages (`mon_*_p2.bin`) out of the
   ROM bundles in `data/rom/` (file offset $8000-$BFFF).
1. `analyze.py` - dedicated Z80 length decoder + reachability analysis
   produces code/data block maps (`mon_*_p2.blocks`) from the extracted
   page binaries.
2. `fixblocks.py` - applies hand-verified block fixes (routine tails,
   trampolines, jump-table targets, inline strings, the v4.01 tokenizer
   record chain).
3. `renblocks.py` - renames/splits the remaining auto-numbered data
   blocks so every region carries a meaningful name (key-scan tables,
   message strings, the XOR-masked ROM tables, keyword/word lists, the
   RAM-init table, ...); misclassified regions are freed as code here
   (the v2.95 `$0A19` key-scan routine and `$2818` help screen, both
   ROM-tail menu builders).
4. `dict_scorpion.py` - hand-written symbol dictionary and educational
   comments for the base monitor; `dict_scorp295.py` / `dict_prof401.py`
   derive the sisters through byte-run alignment (`align.py`,
   `mapsig.py`) plus hand-resolved relocations and version-specific
   comment replacements.
5. `gen.py` - runs z80dasm 1.2.0 with the block map and symbols, then
   inserts the comment blocks and the file header.
6. `checkcov.py` - verifies each listing against its binary byte for
   byte; `auditblocks.py` checks that every `; BLOCK` header is preceded
   by a comment naming it.

The curated inputs for refinement are the `.blocks` files (block map),
the `dict_*.py` symbol/comment dictionaries and the `map_*`/`mapsig_*`/
`digest_*` alignment data; everything else is derived.  After editing a
dict or block map, regenerate and verify from `scripts/`:

```sh
python3 gen.py mon_scorpion_p2.bin mon_scorpion_p2.blocks dict_scorpion.py \
    ../base/scorpion-monitor-p2.asm
python3 checkcov.py ../base/scorpion-monitor-p2.asm mon_scorpion_p2.bin
```
