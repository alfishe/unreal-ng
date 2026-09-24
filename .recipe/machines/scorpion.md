# Recipe: ZS Scorpion (and Scorpion + ProfROM)

The Scorpion ZS-256/ZS-1024 — the other big Russian clone family, with a
built-in Beta128 disk interface and a Shadow Monitor ROM instead of the
Sinclair 48K ROM. Two model ids:

| Model id | RAM (KB) | Difference |
|:--|:--|:--|
| `SCORPION` (default 256) | 256, 1024 | stock Scorpion ROMs |
| `PROFSCORP` (default 256) | 256, 1024 | + ProfROM: service ROM with its own `#7EFD` plane/page latches |

Ground truth:
[portdecoder_scorpion256.h](../../core/src/emulator/ports/models/portdecoder_scorpion256.h),
config [data/configs/scorpion/unreal.ini](../../data/configs/scorpion/unreal.ini).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the variant, `invoke_api` reads the port map
> and live Scorpion latches. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PROFSCORP","ram_size":1024}

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → live.trdos_active, live.mouse_ports_decoded and — Scorpion family only —
#     live.shadow_monitor_paged (null on every other model)

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[0] shows which ROM the machine booted: SOS vs 128K vs ProfROM
#     (ROM signature/name/role are resolved in the response)

inspect_state {"aspects":["fdc"]}
#   → the built-in Beta128 (no separate interface to insert)
```

### What makes a Scorpion a Scorpion

- `#7FFD` bit 4 selects **SOS ROM (0) vs 128K ROM (1)** — the Scorpion boots
  into service ROM territory, not the Sinclair ROM.
- `#1FFD` bit 1 pages the **Shadow Monitor** in (the decoder saves the
  previous `#7FFD` state; `/ports` → `live.shadow_monitor_paged` tracks it).
- `PROFSCORP` adds **ProfROM** selection via `#7EFD` plane/page latches —
  that is the only decoder-level difference from `SCORPION`.
- Beta128 disk interface is **built in** (`trdos_present`): TR-DOS boots
  without inserting anything, and `#FF` FDC ports are always decoded.

## WebAPI

```bash
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PROFSCORP", "ram_size": 1024}' | jq -r '.id')
# NOTE: /start's response only has {id, message, started, state,
# symbolic_id} - confirm model/ram_kb with a follow-up GET:
curl -s "$BASE/emulator/$EMU_ID" | jq '{id, model, ram_kb}'

# Port map + live routing flags (shadow monitor row is Scorpion-only)
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '{model, live, rows: [.entries[] | {port, device, tags}]}'

# ROM identification per bank
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[0] | {type, page, name, role}'

# Built-in FDC state (Beta128 — same view the disk-triage recipes use)
curl -s "$BASE/emulator/$EMU_ID/state/fdc" | jq .
```

Then boot a disk the usual way —
[autostart](../run/autostart-disk.md) or
[manual TR-DOS](../run/manual-trdos-run.md) both work out of the box here.

## Pitfalls

- **`SCORPION` vs `PROFSCORP` is a config-level choice, not RAM** — same
  RAM sizes; software probing for ProfROM behaves differently on the two.
- **Scorpion Covox is off by default** (`CovoxDD=0` in the config; the
  Pentagon-style `CovoxFB=1` is on). A Covox demo silent on a Scorpion is
  usually the config, not the card — see
  [covox-sounddrive.md](../peripherals/covox-sounddrive.md).
- **Shadow Monitor code disappears from normal memory views** — while
  `shadow_monitor_paged` is true, bank 0 is the monitor ROM; step out or
  check `/state/paging` before declaring memory corruption. The saved
  `#7FFD` state is restored on exit by hardware.
- **1024K needs `ram_size: 1024`** — the default create gives you a 256K
  machine, and 1024K-only software then fails its RAM probe silently.
