# Recipe: Profi 1024

The Profi is a 1024K clone with its own `#DFFD` paging extension, a SYS
(service/menu) ROM it boots into, mode-dependent FDC port sets, RTC/CMOS, a
Covox DAC and a 512x240 hi-res video mode with a 16-entry palette.

Ground truth:
[portdecoder_profi.h](../../core/src/emulator/ports/models/portdecoder_profi.h)/[.cpp](../../core/src/emulator/ports/models/portdecoder_profi.cpp),
MCP resource `unreal://machine/profi` (port tables, ROM pages, video modes),
design + status docs in
[docs/inprogress/2026-09-21-profi/](../../docs/inprogress/2026-09-21-profi/)
(`technical-design.md`, `2026-09-25-profi-reconciliation.md` for the parity
matrix and open gaps, `TODO.md`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the machine, `inspect_state` / `invoke_api` read
> paging, video and ports. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PROFI"}   # 1024K only, boots into the SYS/BIOS menu

emulator_manage {"action":"list_models"}
#   → models[].name (NOT .id, which is a numeric index) == "PROFI",
#     creatable:true, available_ram_sizes_kb:[1024]

inspect_state {"aspects":["paging"]}
#   → p7FFD + pDFFD with decoded fields extended_ram_bank, sco, worom, cpm,
#     scr, video_512x240; banks[0].role shows which ROM page is mapped

inspect_state {"aspects":["video"]}
#   → video_mode = PROFI (256x192) or PROFIHR (512x240, #DFFD bit 7)

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → decoded port map: #7FFD, #DFFD, palette #xx7E, AY, Beta128 (gated by
#     CF_DOSPORTS). RTC/CMOS and Covox are decoded but have no port-map rows yet
```

### What works / what doesn't

| Area | State |
|:--|:--|
| `#7FFD` + `#DFFD` paging, 64 RAM pages, SCO / WOROM / CPM / SCR, lock + DFFD.4 override | implemented |
| ROM order SYS=0, DOS=1, 128=2, 48=3; reset into SYS; DOS latch via `#3Dxx` M1 trap | implemented |
| FDC ports: normal `#1F..#7F/#FF`, CP/M `#BF`, extended `#83/#A3/#C3/#E3` + `#3F` | implemented |
| RTC/CMOS: address `#BF/#FF`, data `#9F/#DF`, EXT mode only (CPM ∧ ROM14) | implemented |
| Covox: `#5F` left, `#3F` right, only while the disk interface is off the bus | implemented |
| 512x240 hi-res (DS80), palette `OUT #xx7E` (9-bit), `#FE` read bit 7 (GX0) | implemented |
| NMI (magic button) → DOS latch while DS80 is off | implemented |
| TTD: `#DFFD` + palette as `PeripheralId::ProfiPaging` | implemented |
| IDE (`#xx8B/AB/CB/EB`) | **not implemented** — design: `docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md` |
| Kempston joystick, extended keyboard, Covox extended-mode aliases | not implemented |
| BIOS menu entries (CP/M, TR-DOS, Sinclair 48/128) | main menu reached; entries not yet verified |

## WebAPI

```bash
curl -s "$BASE/emulator/models" | jq '.models[] | select(.name=="PROFI")'
#   name is the string id ("PROFI"); .id in this response is an unrelated numeric index

EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PROFI"}' | jq -r '.id')
# NOTE: the /start response itself only has {id, message, started, state,
# symbolic_id} - model/ram_kb/config_folder are NOT in it. Confirm with a
# follow-up GET:
curl -s "$BASE/emulator/$EMU_ID" | jq '{id, model, ram_kb, config_folder}'

curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/screen/mode" | jq .     # PROFI / PROFIHR
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'
```

TTD on Profi follows the standard recipes —
[recording](../analysis/ttd-recording.md),
[reverse debugging](../analysis/ttd-reverse-debugging.md).

## Pitfalls

- **Many ports depend on the mode.** EXT mode = `#DFFD.5` (CPM) and
  `#7FFD.4` (ROM14) both set; RTC and the extended FDC set only answer then.
  `#3F`/`#5F` are Covox in normal mode but FDC registers while the DOS latch
  or CP/M mode puts the disk interface on the bus. Check `pDFFD`/`p7FFD`
  and the DOS latch before reading a port result.
- **The machine resets into the SYS ROM with the DOS latch on**, not into
  48K BASIC — `banks[0].role` right after reset is the SYS page.
- **`#DFFD` is not Scorpion's `#1FFD`** — different register, different bit
  meanings.
- **`GET /emulator/models` uses `.id` for a numeric index, `.name` for the
  string model id** (`"PROFI"`) — filtering on `.id=="PROFI"` silently
  returns nothing; use `.name`.
- **`POST /emulator/start`'s response doesn't carry `model`/`ram_kb`** —
  confirm those with a follow-up `GET /emulator/{id}`.
- **No `ram_size` choice** — the model is 1024K only; other values are
  rejected by the RAM bitmask check.
