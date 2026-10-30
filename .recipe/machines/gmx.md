# Recipe: ZS Scorpion + GMX

The GMX card (Graphic Memory eXpander, MOA 1998) sits in the CPU socket of a ZS Scorpion and brings its own Z80,
2 MB of RAM, a 512 KB flash of eight 64 KB ROM planes and a register file on top of the host board. Model id `GMX`,
2048 KB only.

Ground truth:
[portdecoder_scorpiongmx.h](../../core/src/emulator/ports/models/portdecoder_scorpiongmx.h),
config [data/configs/gmx/unreal.ini](../../data/configs/gmx/unreal.ini),
design [docs/inprogress/2026-10-07-gmx/design.md](../../docs/inprogress/2026-10-07-gmx/design.md).
Everything not listed here is the host Scorpion: see [scorpion.md](scorpion.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"GMX"}

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → the card rows #0000 (p00), #78FD, #7AFD, #7CFD, #7EFD, #DFFD with decoded fields

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[0].role names the plane and page: the loader hands over to "GMX plane 4: 128K Editor/Menu ROM"
#     (#7EFD = #C0); the v6.44 firmware then settles in plane 5 page 3 with 640x200 on (#7EFD = #D8),
#     waiting for a key with interrupts on
```

## WebAPI

```bash
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "GMX"}' | jq -r '.id')

# Port map with the card rows and their decoded latches
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '{model, live, rows: [.entries[] | {port, device, tags}]}'

# The ROM plane and page behind #0000
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[0] | {type, page, name, role}'
```

## The card at a glance

| Port | Write | Read |
|:--|:--|:--|
| `#xx00` (A7-A0 = 0) | CONF0-2, RECONF (bit 3: reload the scheme, CPU reset), FIXROM, BLKEXT, BLKEX2, ST/PENT | - |
| `#78FD` | RAM page at `#8000` (stored ^ 2) | BRD1 (bit 7), the page, bit 0 = the magic byte after RECONF |
| `#7AFD` | scroll start low (bits 7-4) | paging status |
| `#7CFD` | scroll start high (bits 5-0) | - |
| `#7EFD` | turbo, ROM plane (6-4), 640x200, MagEnable | card status |
| `#DFFD` | RAM page bits 4-6 for `#C000` | - |

- `#C000` page = `DFFD[2:0] << 4 | 1FFD[4] << 3 | 7FFD[2:0]` (128 pages).
- `#1FFD` bit 2 maps the plane's TR-DOS page at `#0000` with the Beta ports on.
- `#7EFD` bit 2 blocks the Magic button and lifts the `#7FFD` lock.
- RESET clears every card latch except `#00`; power-off (hard reset) clears `#00` as well.
- The host Turbo+ logic is cut off by the fitting: no IN strobes, no Even M1.
