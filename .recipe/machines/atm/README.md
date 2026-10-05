# Recipes: the ATM family (`ATM450`, `ATM710`, `ATM3`)

Three different machines, the most port-diverged clones in the tree. Each has its own recipe; this page holds only what they share.

| Model id | Full name | RAM (KB) | Recipe | Character |
|:--|:--|:--|:--|:--|
| `ATM450` (default 512) | ATM-Turbo 2 v4.50 | 512, 1024 | [atm450.md](atm450.md) | address-bus latches (`#FE` writes, A2=0 reads) + `#FDFD`, no `#xx77` |
| `ATM710` (default 1024) | ATM-Turbo 2+ v7.10 | 128, 256, 512, 1024 | [atm710.md](atm710.md) | full `#xx77` control-port decode |
| `ATM3` (4096) | ZX-Evo, BaseConf FPGA | 4096 | [atm3-zxevo-baseconf.md](atm3-zxevo-baseconf.md) | narrower `#FF77` decode, exact `#FF` FDC group, CMOS ports, Z-Controller SD, ERS menu |

Ground truth:
[portdecoder_atm450.h](../../../core/src/emulator/ports/models/portdecoder_atm450.h),
[portdecoder_atm710.h](../../../core/src/emulator/ports/models/portdecoder_atm710.h),
[portdecoder_atm3.h](../../../core/src/emulator/ports/models/portdecoder_atm3.h),
configs [atm450](../../../data/configs/atm450/unreal.ini) /
[atm710](../../../data/configs/atm710/unreal.ini) /
[atm3](../../../data/configs/atm3/unreal.ini).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the model, `invoke_api` reads ports/paging/video
> state. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy:
> [_common/transports.md](../../_common/transports.md)). Shared patterns:
> [_common/machines.md](../../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"ATM450"}        # 512K
emulator_manage {"action":"create","model":"ATM710","ram_size":512}
emulator_manage {"action":"create","model":"ATM3"}          # 4096K fixed

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → ATM3: the full ZX-Evo map - #7FFD, #EFF7, #FF77, #3FF7 (window register),
#     #37F7, #BF/#BE/#BD, Z-Controller SD (#57/#77), Kempston #1F, Covox #FB.
#     ATM450 / ATM710: generic rows only (keyboard, AY, mouse, Beta128 FDC) -
#     #7FFD/#FF77/#FFF7/#EFF7 do NOT appear (getPortMapEntries() has an
#     MM_ATM3 case only). For those two models observe the registers with
#     port-trace (analysis/port-trace.md) filtered on the #xx77/#xxF7 low
#     byte, or read the video mode via inspect_state {"aspects":["video"]}.

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[] (per-window page/type) is populated on all three models;
#     latches[] carries P7FFD, PEFF7 and PFFF7Window0 on ATM3 only (ATM450 /
#     ATM710 have no latch binding yet - same gap as above)
inspect_state {"aspects":["registers","video","fdc"]}
```

- **Video:** multiple hardware video modes beyond 128K standard — read the
  live one via `inspect_state {"aspects":["video"]}` or
  `GET /state/screen/mode`; turbo shows up as `speed_multiplier` in the
  instance identity.

## Extended video modes (`M_ATM16`/`M_ATMHR`/`M_ATMTX`/`M_ATMTL`)

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
UnrealSpeccy [`dxr_atm0.cpp`](https://github.com/alfishe/unreal-speccy/blob/master/dxr_atm0.cpp) /
[`dxr_atm2.cpp`](https://github.com/alfishe/unreal-speccy/blob/master/dxr_atm2.cpp) /
[`dxr_atm6.cpp`](https://github.com/alfishe/unreal-speccy/blob/master/dxr_atm6.cpp)
(EGA/HWMC/Text pixel decode), ZXMAK2
[`src/ZXMAK2.Hardware/Atm/`](https://github.com/zxmak/ZXMAK2/tree/master/src/ZXMAK2.Hardware/Atm)
(C#, very readable, has explicit named border-width constants), Xpeccy
[`src/libxpeccy/video/video.c`](https://github.com/samstyle/Xpeccy/blob/master/src/libxpeccy/video/video.c)
(`vidDrawATM*` functions) and
[`src/libxpeccy/hardware/atm2.c`](https://github.com/samstyle/Xpeccy/blob/master/src/libxpeccy/hardware/atm2.c)
(port-to-register wiring).

**Debugging a visual glitch**: use
[ttd-visual-inspection.md](../../analysis/ttd-visual-inspection.md) to get a
reproducible frame instead of live pause/screenshot — a live GUI screenshot
and the `/capture/screen` WebAPI endpoint were observed to disagree once.
The cause was that they read different buffers (the window the presented
frame, the endpoint the live one); now both read the same presented frame
(see [agent-screenshot-view.md](../../media/agent-screenshot-view.md)), but
that frame lags the machine, so TTD-seeked frames are still the exact way. To empirically verify which physical RAM
page is live for the current mode (rather than trusting the formula),
poke a distinctive byte via `PUT /emulator/{id}/memory/ram/{page}/{offset}`
at the address the formula predicts for a known screen column, advance one
frame, and check whether the predicted pixel changed.

## CP/M mode (`ATM710`, `ATM3`)

`#FF77` bit 9 switches the machine into CP/M memory layout. The natural way
to exercise it is booting a CP/M disk image (TR-DOS autostart rules apply —
[autostart-disk.md](../../run/autostart-disk.md)) and asserting on
`/state/paging` bank roles rather than on the screen.

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "ATM710", "ram_size": 512}' | jq '{id, model, ram_kb}'

curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'
#   → ATM710 / ATM450: generic rows only (keyboard/AY/mouse/FDC) - filtering for
#     "F7|77" returns empty; ATM3 lists #7FFD/#EFF7/#FF77/#3FF7/#37F7 and the rest

curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[] | {address_range, type, page}'

# Live video mode + turbo
curl -s "$BASE/emulator/$EMU_ID/state/screen/mode" | jq .
curl -s "$BASE/emulator/$EMU_ID" | jq '.speed_multiplier'
```

## Pitfalls

- **`GET /ports` shows ATM's own control ports on `ATM3` only** (`#7FFD`,
  `#EFF7`, `#FF77`, `#3FF7`, `#37F7`, `#BF`/`#BE`/`#BD`, SD, Kempston) and
  `state/paging`'s `latches[]` binds `P7FFD`, `PEFF7`, `PFFF7Window0` there.
  `ATM450` and `ATM710` still return only the generic rows and an empty
  `latches[]`; use [port-trace.md](../../analysis/port-trace.md) to observe
  writes to their control registers.
- **`ATM3` has no `ram_size` freedom** — 4096K only; the RAM bitmask rejects
  everything else.
- **ATM710 vs ATM3 decode quirks are the classic compatibility trap** —
  software written against ATM710's aliased ports (`#9F/#BF/#DF/#FF` group)
  behaves differently on ATM3's exact `#FF` decode. When a program runs on
  one and not the other, diff `GET /ports` output between the two models.
- **CMOS ports move with the `shaden` bit** on ATM3 (`#BFF7/#DFF7` ↔
  `#BEF7/#DEF7`) — a port trace filtered on fixed addresses can miss CMOS
  traffic; filter on the `#xxF7` low byte instead (see
  [port-trace.md](../../analysis/port-trace.md)).
- **Turbo changes timing, not just speed** — after turbo engages,
  `speed_multiplier` > 1 and cycle-based assertions (contention, tape
  loaders) need re-verification; GS audio on the `generalsound` branch
  deliberately stays at its own 12 MHz clock regardless of host turbo.
