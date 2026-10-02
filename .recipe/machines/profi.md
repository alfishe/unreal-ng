# Recipe: Profi v5 (`PROFI`) and v3 (`PROFI3`)

The Profi is a clone with its own `#DFFD` paging extension, a SYS
(service/menu) ROM it boots into, mode-dependent FDC port sets, a Covox DAC
and a 512x240 hi-res video mode. It came as two board families, and both are
machines here
([docs/inprogress/2026-10-01-profi-v3-v5/](../../docs/inprogress/2026-10-01-profi-v3-v5/README.md)):

| | `PROFI3` (v3.x, Kramis, 1990) | `PROFI` (v5.0x, Kondor, 1993-94; alias `PROFI5`) |
|:--|:--|:--|
| RAM | **512K**, 1024K | 512K, **1024K** |
| ROM | `rom/profi/kramis-v02.rom` (BIOS V0.2 + TR-DOS 5.03) | `rom/profi.rom` |
| Hi-res 512x240 | monochrome | 16 colours, palette `OUT #xx7E` |
| Extended ports (CP/M + ROM14), RTC, IDE | none | yes |
| Frame (default `[PROFI] SyncProm=`) | 69888 T, INT 12580 T before paper | 69888 T, INT 14368 T before paper |

`[PROFI] SyncProm=` picks another sync PROM: `0a1d`, `samx6`, `fb0579b6`
(71680 T, INT 48 T before paper) or `v503`.

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
emulator_manage {"action":"create","model":"PROFI"}    # v5, boots into the SYS/BIOS menu
emulator_manage {"action":"create","model":"PROFI3"}   # v3, boots into the Kramis BIOS menu

emulator_manage {"action":"list_models"}
#   → models[].name (NOT .id, which is a numeric index) == "PROFI",
#     creatable:true, available_ram_sizes_kb:[1024]

inspect_state {"aspects":["paging"]}
#   → profi_board (v3 / v5), p7FFD + pDFFD with decoded fields extended_ram_bank, sco, worom, cpm,
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
| FDC ports: normal `#1F..#7F/#FF`, CP/M `#BF`, extended `#83/#A3/#C3/#E3` + `#3F` (v5) | implemented; checked against both boards' port decoder PROMs |
| RTC/CMOS: address `#BF/#FF`, data `#9F/#DF`, EXT mode only (CPM ∧ ROM14) | implemented (v5) |
| Two boards: `PROFI3` without palette, extended ports, RTC, IDE; monochrome hi-res | implemented |
| Frame and INT from the board's sync PROM (`[PROFI] SyncProm=`) | implemented |
| AY decodes A13 (`IN #DFFD` does not read the AY) | implemented |
| Covox: `#5F` left, `#3F` right, only while the disk interface is off the bus | implemented |
| 512x240 hi-res (DS80), palette `OUT #xx7E` (9-bit), `#FE` read bit 7 (GX0) | implemented (palette and GX0: v5) |
| NMI (magic button) → DOS latch while DS80 is off | implemented |
| TTD: `#DFFD` + palette as `PeripheralId::ProfiPaging` | implemented |
| IDE (`[HDD] Scheme=PROFI`, answers in EXT mode): slots `ide0.master` / `ide0.slave`; the SYS ROM boots from a hard disk image; geometry from the disk's ProfiHiDD header (16 x 16 without one) | implemented — see [Hard disk](#hard-disk-ide) |
| Kempston joystick at `#1F`, Covox extended-mode aliases (v5) | implemented |
| TURBO front-panel switch (7 MHz; on v3 a loaded floppy head holds 3.5 MHz), recorded by TTD | implemented — see [TURBO switch](#turbo-switch) |
| v5 video WAIT at 3.5 MHz (`[PROFI] WaitPhase` / `WaitConfig` / `RomWait`), v3 turbo waits, v5 turbo waits (approximation) | implemented (feature `contention`) |
| v3 floating bus (pixel byte on an unanswered `IN` with A0 = 1) | implemented |
| Extended keyboard, the 15 MHz third crystal, DS80 waits | not implemented |
| BIOS menu entries TR-DOS, Sinclair 48 / 128 | verified on both boards; CP/M boots from a disk |

### TURBO switch

```bash
curl -s "$BASE/emulator/$ID/switches" | jq                       # {"switches":[{"name":"turbo","on":false}]}
curl -s -X POST "$BASE/emulator/$ID/switches" -H 'Content-Type: application/json' \
     -d '{"name":"turbo","on":true}' | jq                           # 7 MHz
```

CLI: `switch turbo on`; Lua / Python: `set_switch("turbo", true)`, `get_switch("turbo")`; Qt: Machine > TURBO
Switch; `[PROFI] Turbo=1` turns it on at power-on. The v5 CP/M switch works the same way (`"cpm"`, Machine > CP/M
Switch, `[PROFI] CpmSwitch=1`): while it is on, `#DFFD` stays `#00`. In turbo, code in RAM runs about 1.33x on v3 (the CPU waits for
its DRAM slot), code in ROM 2x.

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

## Hard disk (IDE)

The v5 Profi ships with its IDE board on (`[HDD] Scheme=PROFI`); the v3 board has none. Put a hard disk
image on the master with the media verbs
([use-media-slots.md](../media/use-media-slots.md), reference
[docs/features/media.md](../../docs/features/media.md)):

```text
media {"action":"insert","slot":"hd","path":"/home/me/zx/profi.hdd"}   # hd = ide0.master
inspect_state {"aspects":["ide"]}                                          # board, units, task file
```

A hard disk is inserted and ejected while the machine is paused. The SYS ROM
boots from the disk; the geometry comes from the disk's ProfiHiDD header
(16 x 16 from the SYS ROM, 16 x 63 from Karabas), a disk without one gets 16 x 16.

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
- **RAM sizes** — 512K or 1024K on either board (v3 defaults to 512K, v5 to
  1024K); other values are rejected by the RAM bitmask check.
- **`PROFI3` has no RTC and no IDE** — `inspect_state aspects:["rtc"]` reports
  the clock as absent, and `[HDD] Scheme=PROFI` does not fit the v3 board.
