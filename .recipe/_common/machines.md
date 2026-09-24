# Machine Models Reference

How to pick, create and introspect a machine model. Read this before any
`machines/` or `peripherals/` recipe — they assume the patterns below.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the instance and `inspect_state` reads identity.
> Use [WebAPI](#webapi) only inside host-side Python/bash pipelines or when
> MCP is unavailable (policy: [_common/transports.md](transports.md)).

## The model table

Authoritative source: `mem_model` in
[core/src/emulator/config.h](../../core/src/emulator/config.h) (short name =
what create requests accept). `ram_size` is validated against the model's
`AvailRAMs` bitmask — wrong sizes fail with HTTP 400 + the supported list.

| Short name | Full name | RAM (KB) | Status on `master` |
|:--|:--|:--|:--|
| `PENTAGON` | Pentagon | 128, 512, **1024** | creatable (1024 → Pentagon-1024 decoder) |
| `48K` | ZX-Spectrum 48k | 48 | creatable |
| `128k` | ZX-Spectrum 128k | 128 | creatable |
| `PLUS3` | ZX-Spectrum +3 | 128 | creatable |
| `ATM710` | ATM-Turbo 2+ v7.10 | 128, 256, 512, 1024 | creatable |
| `ATM3` | ZX-Evo (ATM Turbo 3) | 4096 | creatable |
| `SCORPION` | ZS Scorpion | 256, 1024 | creatable |
| `PROFSCORP` | ZS Scorpion + PROF ROM | 256, 1024 | creatable |
| `PROFI` | Profi | 1024 | creatable on `master` (verified 2026-09-23 — the `profi` branch note below is now stale for base creatability; it may still carry additional in-progress Profi features not yet on master) |
| `TSL`, `ATM450`, `GMX`, `KAY`, `QUORUM`, `LSY256`, `PHOENIX`, `NEXT` | various | — | no factory port decoder → HTTP 400 + reason, never a silent 48K fallback |

The runtime list is **authoritative over this table** — builds and branches
differ (see "Branches" below):

```text
emulator_manage {"action":"list_models"}
#   → models[] with id, full_name, default_ram_kb, available_ram_sizes_kb, creatable
invoke_api     {"path":"/emulator/models"}
```

Every entry carries a `creatable` flag; a create request for a
non-creatable model fails with **HTTP 400 + reason** — the server never
silently falls back to 48K. Build fingerprint (to pin which server you are
talking to): `GET /emulator/status` → `server.git_branch` / `server.git_commit`.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"SCORPION","ram_size":1024}
#   → Created and started emulator <id> (model <symbolic_id>)

inspect_state {"aspects":["ram_size"]}
#   → ram_kb: 1024 for the instance above
```

- `ram_size` is optional; omit it for the model default (e.g. SCORPION → 256).
- `target:"auto"` picks the single existing instance, or creates a 128K
  machine — pin the id for anything model-specific.

## WebAPI

```bash
# Models + creatable flags (authoritative)
curl -s "$BASE/emulator/models" | jq '.models[] | {id, creatable, default_ram_kb}'

# Create with model + RAM
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "SCORPION", "ram_size": 1024}' | jq '{id, model, ram_kb, config_folder}'

# Identity of an existing instance
curl -s "$BASE/emulator/$EMU_ID" | jq '{model, model_full_name, ram_kb, config_folder, video_mode, speed_multiplier}'
```

Create response and `GET /emulator/{id}` share the identity fields:
`model`, `model_full_name`, `ram_kb`, `config_folder`, `video_mode`
(null before first frame), `speed_multiplier`.

## Per-machine introspection (both transports)

- `GET /emulator/{id}/ports` — the **decoded port map** of this model: one row
  per device/port family (paging, FDC, sound, mouse...). The fastest way to
  learn what a machine actually decodes.
- `GET /emulator/{id}/state/paging` — live bank/page latches.
- `GET /emulator/{id}/state/memory/rom` — which ROM is banked where.
- `GET|POST /emulator/{id}/memory/page/{type}/{page}` — read or force-bank a
  page (automation-only views of the memory model).
- `inspect_state {"aspects":[...]}` — per-topic snapshots: `registers`,
  `rom`, `video`, `fdc`, `mouse`, `ram_size`, `audio_ay`, `audio_fm`,
  `audio_gs` (see the sound recipes).

## Branches that add machines and cards

Work-in-progress hardware lives on side branches — a recipe may name one:

| Branch | Adds | Config delta |
|:--|:--|:--|
| `profi` | Profi 1024 machine (RTC/CMOS, Covox/SoundDrive at its own ports, hi-res 512x240, TTD paging) — **base creatability now on `master`** (verified 2026-09-23), this row is for any Profi feature not yet merged | `configs/profi/unreal.ini` |
| `generalsound` | General Sound **Z80 LLE** mode (default `GSType=Z80` on clone models), GS state + port-trace endpoints | `GSType=Z80`, `GS=rom/gs105a.rom` |
| `moonsound` | MoonSound (OPL4) card engine; clone-only policy | `MoonSound=1` clones, `=0` real Sinclairs |

On `master` the same config keys exist but some are inert (`MoonSound=` has
no engine; `GSType=BASS` is the legacy HLE mode). Always check
`server.git_branch` before asserting hardware behavior that depends on a
branch. Design docs: [docs/inprogress/](../../docs/inprogress/) —
`2026-09-21-profi/`, `2026-09-19-general-sound/`, `2026-09-13-moonsound/`.

## Pitfalls

- **`ram_size` beyond the model's list is a 400**, e.g. `"ram_size": 1024` on
  `128k` — the error body lists the supported sizes.
- **Pentagon 1024 reports `config_folder: "pentagon512k"`** — the folder
  resolver maps every PENTAGON ≥ 512K there; the *decoder* is still
  Pentagon-1024 with its `#EFF7` feature register. Assert on `ram_kb`, not
  on the folder name.
- **Don't cache the model table across builds** — creatability changes as
  decoders/configs land (ATM710/ATM3 are the recent examples). Re-query
  `/emulator/models` per session.
- **`zx-diagnostics` in `configs/` is not a model** — it has no `mem_model`
  row; ignore it when enumerating machines.
