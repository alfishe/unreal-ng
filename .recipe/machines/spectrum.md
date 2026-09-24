# Recipe: Real Sinclairs (48K, 128K, +3)

The original machines — and the control group for clone debugging. Some
clone-world features (sound cards, TurboSound) are *architecturally*
scoped to specific models in the codebase, but this repo's **shipped
default configs currently enable them for every model, Sinclairs
included** (verified 2026-09-23 on `master` — see the `audio_ay` and "Card
policy boundary" notes below) — don't assume model choice alone gates
card availability without checking the instance's actual config.

| Model id | RAM (KB) | Sound | Disk |
|:--|:--|:--|:--|
| `48K` | 48 | beeper only | none (interface needed) |
| `128k` | 128 | AY (`#FFFD`/`#BFFD`) | none (interface needed) |
| `PLUS3` | 128 | AY | built-in +3 FDC, +3DOS |

Ground truth: `mem_model` in
[core/src/emulator/config.h](../../core/src/emulator/config.h), configs
[spectrum48](../../data/configs/spectrum48/unreal.ini) /
[spectrum128](../../data/configs/spectrum128/unreal.ini) /
[spectrum3](../../data/configs/spectrum3/unreal.ini).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the model, `inspect_state` reads machine state.
> Use [WebAPI](#webapi) only inside host-side Python/bash pipelines or when
> MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).
> Shared patterns: [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"48K"}    # the no-frills baseline
emulator_manage {"action":"create","model":"128k"}
emulator_manage {"action":"create","model":"PLUS3"}

inspect_state {"aspects":["audio_ay","registers"]}
#   → verified 2026-09-23: with this repo's SHIPPED default configs, ALL
#     models including 48K return full AY/TSFM chip data here and decode
#     #FFFD/#BFFD in /ports - data/configs/spectrum48/unreal.ini sets
#     TurboSound=FM same as every other model (config convenience, not
#     hardware fidelity). "audio_ay answers" is NOT a reliable 48K-vs-128k
#     test against this build's defaults - if you need that distinction,
#     edit TurboSound out of a 48K instance's config first and diff against
#     a fresh instance, don't assume the shipped config is hardware-accurate.

inspect_state {"aspects":["fdc"]}
#   → NOT PLUS3-exclusive (verified 2026-09-23): 48K/128k also answer here
#     with a Beta128-style "WD1793 (Beta Disk)" controller and 4 drive
#     slots (present:true, inserted:false) - Beta128 is modeled as always
#     decoded/available regardless of model, matching the "Disk: none
#     (interface needed)" row above. Could not verify PLUS3's own +3 FDC
#     response in this environment (creation currently fails there - see
#     Pitfalls) to confirm it reports a different `controller` string.
```

### Why keep these around when clones exist

- **Timing/contention reference** — 48K/128K contention and floating-bus
  behavior are the best-documented targets; a bug that reproduces on
  `128k` but not `PENTAGON` (or vice versa) isolates clone-specific
  decode/timing in one experiment.
- **Software compatibility floor** — anything 48K-clean runs everywhere.
- **Card policy boundary — branch-dependent, NOT true on `master`**: this
  claim describes a policy that may hold on the `generalsound`/`moonsound`
  feature branches, but verified 2026-09-23 on `master`,
  `data/configs/spectrum48/unreal.ini` actually ships `GSType=BASS` and
  `MoonSound=1` (both enabled) — the same as clone configs, and no "real
  Sinclair never had this card" comment exists in that file. `audio_gs`
  itself returns `status: "not_implemented"` on `master` regardless of
  model, so this whole policy is currently unverifiable/inactive here. Only
  trust this bullet on a branch where you've confirmed the config values
  yourself.

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PLUS3"}' | jq '{id, model, ram_kb, config_folder}'

# 128K paging latch + ROM identification (128 Sinclair ROMs are signature-matched)
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[] | {address_range, type, page, name}'

# AY state (128k/PLUS3)
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay" | jq .

# +3 FDC
curl -s "$BASE/emulator/$EMU_ID/state/fdc" | jq .
```

Tapes and disks follow the standard recipes — [tape](../media/insert-tape.md),
[disks](../media/insert-disk.md); on `PLUS3` the internal FDC is present
from power-on (no Beta128-style reset dance), and `+3DOS` images (`.dsk`)
insert directly.

## Pitfalls

- **`48K` has no `#7FFD`** — 128K software paging into bank switches just
  writes to a port nobody decodes; the failure mode is silent. Check
  `/state/paging` exists-or-shape before porting a test down from 128k.
- **`128k` vs `PLUS3` is not just the FDC** — +3 has different ROM paging
  (`+3DOS` bank) and subtle timing; a "128K-compatible" program can still
  trip on `PLUS3` ROM entry points.
- **`ram_size` is fixed per model** (48/128/128) — any other value is a 400.
- **Don't assume 48K is AY-silent in this build** — see the `audio_ay` note
  above: the shipped `spectrum48` config enables TurboSound=FM like every
  other model, so `audio_ay`/`/ports` both report the chip. Real 48K
  hardware has no AY; this emulator's *default config* does not currently
  enforce that distinction. If a test depends on 48K being genuinely silent,
  verify the instance's actual `TurboSound`/`SD`/`CovoxFB` config values
  rather than trusting the model name alone.
- **`PLUS3` create failure, root-caused and fixed 2026-09-23**: the shipped
  `data/configs/spectrum3/unreal.ini` `[ROM]` section was simply missing
  the `48k=`/`128k=`/`PLUS3=` lines that `spectrum48`/`spectrum128`'s
  configs both carry (a config-file omission, not a code bug —
  `config.cpp:199` reads `[ROM] PLUS3=` and got nothing). Fixed by adding
  the three lines in the same position/format as the sibling configs.
  Live-verified: `PLUS3` now creates and boots into the real +3 ROM
  (`" 1982 Amstrad"` copyright banner in `screen_ocr`/`capture/ocr`
  output). If this regresses, check that key first before assuming a code
  change is needed.
