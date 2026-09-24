# Recipe: Profi 1024

The Profi — a 1024K clone with its own `#DFFD` paging extension, Profi FDC
ports, RTC/CMOS and a 512x240 hi-res video mode... on paper. **As of this
writing the model is creatable but `PortDecoder_Profi` is a near-stub** —
verify what you actually need before relying on any of the features below.

**Status (verified 2026-09-23 against a live `master` build):** `PROFI` now
shows `creatable: true` and boots (the missing piece used to be
`data/configs/profi/unreal.ini`, which now exists). But the port decoder
itself only implements `#7FFD`-style 128K paging plus a declared-but-inert
`#DFFD` register — no distinct Profi FDC group (the FDC you see is plain
Beta128), no RTC/CMOS, no Covox/SoundDrive arbitration, no working hi-res
mode, and ROM bank selection is inverted versus real hardware. This is a
**documented, tracked gap**, not a guess — see the defect table (B1-B10) in
the design doc below before assuming a Profi-specific behavior works.

Ground truth:
[portdecoder_profi.h](../../core/src/emulator/ports/models/portdecoder_profi.h)/[.cpp](../../core/src/emulator/ports/models/portdecoder_profi.cpp)
(the stub itself — read `Port_DFFD`'s comment, it says outright it's not
implemented), design doc with the full defect list
[docs/inprogress/2026-09-21-profi/technical-design.md](../../docs/inprogress/2026-09-21-profi/technical-design.md)
§2 "Current state (verified against master)".

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the machine, `invoke_api` reads the port map
> and paging (both will show you the stub's actual behavior, not the
> aspirational one). Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PROFI"}   # 1024K, no ram_size choice - creatable on master now

emulator_manage {"action":"list_models"}
#   → models[].name (NOT .id, which is a numeric index) == "PROFI",
#     creatable:true, available_ram_sizes_kb:[1024]

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → today: #7FFD, #DFFD, standard AY/Beta128/mouse rows only - no
#     Profi-specific FDC/RTC/CMOS/Covox rows exist yet

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[0].role - check which ROM actually loaded; B1 (ROM select
#     inverted) means this may not be the ROM you expect
```

### What's real today vs. what's designed but not built

| Claim (design intent) | Actual state on master |
|:--|:--|
| `#7FFD` + `#DFFD` paging → 64 RAM pages | `#DFFD` is declared (`state.pDFFD`) but **never written** (B5) — extended RAM/hi-res unreachable |
| Profi's own DOS/FDC port group | Not implemented — `/ports` shows plain Beta128 rows, no Profi-specific decode |
| RTC/CMOS at EXT-mode ports | Not implemented at all (B9) |
| Covox/SoundDrive at `#5F`/`#3F` with FDC arbitration | Not implemented (B9) — those addresses are just the Beta128 FDC ports here |
| 512x240 hi-res video | `M_PROFI`/`R_512_240`/`DetectModeProfi` exist in `Screen`, but `DrawProfi` is a no-op and the raster geometry row is still the plain 256x192 one |
| TTD paging reversibility (`ttdprofipaging`) | Not registered (B10) — once `#DFFD` is made live, restore will silently lose it until this is fixed |
| ROM boot order (SYS→DOS→128K→48K) | **Inverted** (B1): `isROM0 ? RM_128 : RM_SOS` is backwards vs. hardware's `7FFD.4=1` selecting the DOS/48K side |

Don't build automation against any row in the right column — check the
design doc's defect table for current status before relying on it, it's
being fixed incrementally and this table will go stale.

## WebAPI

```bash
curl -s "$BASE/emulator/status" | jq '{branch: .server.git_branch, commit: .server.git_commit}'

curl -s "$BASE/emulator/models" | jq '.models[] | select(.name=="PROFI")'
#   name is the string id ("PROFI"); .id in this response is an unrelated numeric index

EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PROFI"}' | jq -r '.id')
# NOTE: the /start response itself only has {id, message, started, state,
# symbolic_id} - model/ram_kb/config_folder are NOT in it. Confirm with a
# follow-up GET:
curl -s "$BASE/emulator/$EMU_ID" | jq '{id, model, ram_kb, config_folder}'

# Machine-specific decoded ports (today: no Profi-specific rows, see table above)
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'

# Screen mode - will read 256x192/ZX today, not 512x240, until DrawProfi lands
curl -s "$BASE/emulator/$EMU_ID/state/screen/mode" | jq .
```

TTD on Profi follows the standard recipes —
[recording](../analysis/ttd-recording.md),
[reverse debugging](../analysis/ttd-reverse-debugging.md) — but per B10
above, `#DFFD` state is not yet part of the reversible model state.

## Pitfalls

- **The model being `creatable: true` does not mean the machine is
  Profi-accurate.** It means the config folder exists; the port decoder
  behind it is still mostly the 128K-clone baseline it started from. Cross
  check any specific claim against the design doc's defect table (§2)
  before writing automation against it.
- **`GET /emulator/{id}/models` uses `.id` for a numeric index, `.name` for
  the string model id** (`"PROFI"`) — filtering on `.id=="PROFI"` silently
  returns nothing; use `.name`.
- **`POST /emulator/start`'s response doesn't carry `model`/`ram_kb`** —
  confirm those with a follow-up `GET /emulator/{id}` instead of expecting
  them on the create response.
- **No `ram_size` choice** — the model is 1024K only; sending `ram_size`
  values other than 1024 is rejected by the RAM bitmask check.
- **ROM select is inverted (B1)** — don't trust which ROM you think you
  booted without checking `/state/paging` `banks[0].role`.
- **`#DFFD` is not Scorpion's `#1FFD`** — different extension register,
  different bit meanings — moot in practice right now since `#DFFD` isn't
  functionally wired up yet (B5), but will matter once it is.
