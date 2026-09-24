# Recipe: ATM Turbo (ATM710, ATM3/ZX-Evo)

The ATM Turbo family — the most port-diverged clones in the tree. Two
creatable model ids:

| Model id | Full name | RAM (KB) | Character |
|:--|:--|:--|:--|
| `ATM710` (default 1024) | ATM-Turbo 2+ v7.10 | 128, 256, 512, 1024 | full `#xx77` control-port decode |
| `ATM3` (4096) | ZX-Evo (ATM Turbo 3) | 4096 | narrower `#FF77` decode, exact `#FF` FDC group, CMOS ports |

Ground truth:
[portdecoder_atm710.h](../../core/src/emulator/ports/models/portdecoder_atm710.h),
[portdecoder_atm3.h](../../core/src/emulator/ports/models/portdecoder_atm3.h),
configs [atm710](../../data/configs/atm710/unreal.ini) /
[atm3](../../data/configs/atm3/unreal.ini).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the model, `invoke_api` reads ports/paging/video
> state. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"ATM710","ram_size":512}
emulator_manage {"action":"create","model":"ATM3"}          # 4096K fixed

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → generic rows only (keyboard, AY, mouse, Beta128 FDC) - #7FFD/#FF77/
#     #FFF7/#EFF7 do NOT appear here (verified 2026-09-23): getPortMapEntries()
#     is a single non-virtual PortDecoder method with no ATM-specific rows.
#     To actually observe these registers, use port-trace (analysis/port-trace.md)
#     filtered on the #xx77/#xxF7 low byte, or read state.pFF77/p7FFD/pEFF7
#     indirectly via inspect_state {"aspects":["video"]} (video mode only).

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[] (per-window page/type) is populated; latches[] is EMPTY for
#     ATM (no p7FFD/FF77/FFF7 tagging yet - same gap as above)
inspect_state {"aspects":["registers","video","fdc"]}
```

### The ATM port model (what software writes)

- `#FF77` — **ATM control register**: video mode select, CPU turbo,
  memory swap, INT gate. Decoder bits worth knowing: bit 8 `PEN` enables
  ATM paging, bit 9 selects TR-DOS vs **CP/M mode**, bit 14 (`pen2`)
  disables palette writes.
- `#FFF7` — **memory manager**: four window registers (window chosen by
  A15:A14), so ATM code can remap all four 16K windows, not just 0 and 1.
- `#EFF7` — extended control (3.5 MHz turbo request, ROCACHE).
- `ATM3` differences: partial `#FF77` decode (any port with low byte `0x77`,
  e.g. `#BC77`), an exact `#FF`-only FDC decode (no `#9F/#BF/#DF` aliases),
  and **CMOS** at data `#BFF7` / address `#DFF7` (or `#BEF7`/`#DEF7` when
  the `shaden` latch bit 0 is on — the DOS ports then take `#BFF7`).
- **Video:** multiple hardware video modes beyond 128K standard — read the
  live one via `inspect_state {"aspects":["video"]}` or
  `GET /state/screen/mode`; turbo shows up as `speed_multiplier` in the
  instance identity.

### Extended video modes (`M_ATM16`/`M_ATMHR`/`M_ATMTX`/`M_ATMTL`)

Selected by `#FF77` bits 0-2 (`val & 7`; 0=EGA, 2=HWMC, 3=ZX-compatible,
6=Text, 7=Linear Text on ATM3/ZX-Evo only). Full investigation, evidence
and diagrams: `docs/inprogress/2026-09-22-atm-hires-border-and-addressing/`.
Ground truth: `core/src/emulator/video/atm/screenatm.cpp`.

| Mode | Resolution | Color depth | Notes |
|:--|:--|:--|:--|
| `M_ATM16` (EGA) | 320×200 | 4bpp, 16 colors, bit-planar | 2 physical RAM pages, no attribute blocks |
| `M_ATMHR` (HW Multicolor) | 640×200 | 1bpp bitmap + attribute per **8×1** cell | true per-scanline color, not 8×8 like plain ZX |
| `M_ATMTX` / `M_ATMTL` (Text) | 640×200 | text 80×25, 16-color ink/paper per cell | TL = ZX-Evo only, differs in RAM addressing not resolution |

**Framebuffer page selection** (all four modes): `videoPage = (#7FFD bit3)
? 7 : 5`, `altPage = videoPage - 4` (i.e. 1 or 3). Pixel data always comes
from `videoPage`, attribute/color data always from `altPage` — re-read
live on every scanline, not latched once per frame (confirmed
period-accurate against real hardware wiring, cross-checked against
`unreal-speccy`/`xpeccy`/ZXMAK2 reference sources — see the investigation
doc above before ever suspecting this is the bug).

**Border geometry**: these modes have **no side border** — active picture
is edge-to-edge horizontally (`fullFrameWidth == screenWidth`), only
top/bottom border exists (44-line top/bottom margin). This was a real bug
until 2026-09-23 (side border used to be modeled with a nonzero
`screenOffsetLeft`, causing visible stripe artifacts) — if you see a
column of wrong-colored pixels at the left/right screen edge on an ATM
build older than that fix, that's it.

**Reference emulator sources**, useful for cross-checking any future ATM
video question against a second/third implementation rather than guessing:
`/Volumes/TB4-4Tb/Projects/emulators/github/unreal-speccy/dxr_atm{0,2,6}.cpp`
(EGA/HWMC/Text pixel decode), `.../ZXMAK2/src/ZXMAK2.Hardware/Atm/*.cs`
(C#, very readable, has explicit named border-width constants),
`.../Xpeccy/src/libxpeccy/video/video.c` (`vidDrawATM*` functions) and
`.../Xpeccy/src/libxpeccy/hardware/atm2.c` (port-to-register wiring).

**Debugging a visual glitch**: use
[ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md) to get a
reproducible frame instead of live pause/screenshot — a live GUI screenshot
and the `/capture/screen` WebAPI endpoint were observed to genuinely
diverge once this session for reasons not yet root-caused; TTD-seeked
frames don't have that ambiguity. To empirically verify which physical RAM
page is live for the current mode (rather than trusting the formula),
poke a distinctive byte via `PUT /emulator/{id}/memory/ram/{page}/{offset}`
at the address the formula predicts for a known screen column, advance one
frame, and check whether the predicted pixel changed.

### CP/M mode

`#FF77` bit 9 switches the machine into CP/M memory layout. The natural way
to exercise it is booting a CP/M disk image (TR-DOS autostart rules apply —
[autostart-disk.md](../run/autostart-disk.md)) and asserting on
`/state/paging` bank roles rather than on the screen.

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "ATM710", "ram_size": 512}' | jq '{id, model, ram_kb}'

curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'
#   → generic rows only (keyboard/AY/mouse/FDC) - filtering for "F7|77" returns
#     empty, #7FFD/#FF77/#FFF7/#EFF7 aren't in this static map (verified 2026-09-23)

curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[] | {address_range, type, page}'

# Live video mode + turbo
curl -s "$BASE/emulator/$EMU_ID/state/screen/mode" | jq .
curl -s "$BASE/emulator/$EMU_ID" | jq '.speed_multiplier'
```

## Pitfalls

- **`GET /ports` doesn't show ATM's own control ports** (`#7FFD`/`#FF77`/
  `#FFF7`/`#EFF7`) — verified empty 2026-09-23; `getPortMapEntries()` is a
  single generic `PortDecoder` method with no ATM-specific rows, and
  `state/paging`'s `latches[]` is empty too. Use
  [port-trace.md](../analysis/port-trace.md) to actually observe writes to
  these registers, not the static port map.
- **`ATM3` has no `ram_size` freedom** — 4096K only; the RAM bitmask rejects
  everything else.
- **ATM710 vs ATM3 decode quirks are the classic compatibility trap** —
  software written against ATM710's aliased ports (`#9F/#BF/#DF/#FF` group)
  behaves differently on ATM3's exact `#FF` decode. When a program runs on
  one and not the other, diff `GET /ports` output between the two models.
- **CMOS ports move with the `shaden` bit** on ATM3 (`#BFF7/#DFF7` ↔
  `#BEF7/#DEF7`) — a port trace filtered on fixed addresses can miss CMOS
  traffic; filter on the `#xxF7` low byte instead (see
  [port-trace.md](../analysis/port-trace.md)).
- **Turbo changes timing, not just speed** — after turbo engages,
  `speed_multiplier` > 1 and cycle-based assertions (contention, tape
  loaders) need re-verification; GS audio on the `generalsound` branch
  deliberately stays at its own 12 MHz clock regardless of host turbo.
