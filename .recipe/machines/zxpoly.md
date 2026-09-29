# Recipe: ZX-Poly (four synchronized machines)

ZX-Poly is a 1994 platform concept: four Z80 modules run the same program in
lockstep, and each module's screen supplies one bit of every pixel's colour.
That gives 16 colours per pixel (mode 4), a 512×384 mode (5) and two masked
modes (6, 7), with no attribute clash. The program code is unchanged; only the
graphics data differs per module.

unreal-ng builds it from **four stock instances of one model**:

- **Module 0, the master**, is the machine every surface addresses. It owns
  input, sound and the display.
- **Modules 1–3, the slaves**, are hidden members: they are left out of
  instance listings but stay reachable by their own ID.

Three named configurations create it by name wherever a model name is
accepted (WebAPI/MCP `model`, CLI `start`, Lua/Python `zxpoly_start`, the Qt
**Machine** menu):

| Configuration | Modules | Runs |
|:--|:--|:--|
| `ZXPOLY-48K` | 4 × 48K | replicated 48K software only; `.zxp` and `.prom` are refused (they need 128K paging), and there is no TR-DOS |
| `ZXPOLY-128K` | 4 × 128K | `.zxp`, the Test ROM |
| `ZXPOLY-PENTAGON` | 4 × Pentagon | everything, including multiloader disks (TR-DOS) |

Pentagon is the default (zxpoly's own). A base model name (`PENTAGON`,
`128k`) with the `zxpoly` option works as well.

Design and ground truth:
[docs/inprogress/2026-09-27-zxpoly/](../../docs/inprogress/2026-09-27-zxpoly/)
(start with `prototype-results.md`).
Content: [testdata/machines/zxpoly/](../../testdata/machines/zxpoly/README.md),
which holds 7 `.zxp` snapshots, 2 multiloader TRDs and the Test ROM (`.prom`).

| Media | Loads as |
|:--|:--|
| `.zxp` | four modules at once, locked, ready to run |
| `.prom` | the ZX-Poly ROM (the Test ROM): power-on, CPU0 drives the rest |
| `.trd` / `.scl` | booted through TR-DOS on the master; its multiloader fills the slaves and locks the machine |
| none | the bare machine: the master runs its ROM, the slaves wait |

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) inside host-side pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
emulator_manage {"action":"create","zxpoly":true,"zxpoly_file":"/path/to/Alien8.zxp"}
#   → the master's id; model defaults to PENTAGON ("model":"ZXPOLY-128K" etc. to change)
emulator_manage {"action":"create","model":"ZXPOLY-128K"}   # the bare machine by name
emulator_manage {"action":"list_models"}                    # configurations carry "zxpoly": true

emulator_manage {"action":"zxpoly_status"}
#   → modules (ids, platform registers R0-R3), #3D00, locked, video_mode,
#     slaves_running, divergence {diverged, module, what}

capture_media {"what":"screenshot"}   # the master's framebuffer carries the composed picture
type_input {"text":"1"}               # keys reach all four modules at the frame boundary
emulator_manage {"action":"destroy"}  # removes the whole group
```

## WebAPI

```bash
B=http://localhost:8090/api/v1/emulator
curl -s -X POST $B/start -H 'Content-Type: application/json' \
  -d '{"model":"PENTAGON","zxpoly":{"file":"/path/to/flyshark.zxp"}}' | jq '{id, zxpoly}'
ID=...    # the master
curl -s $B/$ID/zxpoly | jq '{locked, video_mode, port_3d00, divergence}'
curl -s $B/$ID | jq .zxpoly          # {module:0, master_id, locked, video_mode}
curl -s -X DELETE $B/$ID             # removes all four
```

`"zxpoly": true`, or `{"model":"ZXPOLY-PENTAGON"}` alone, starts the bare
machine. A configuration name with `ram_size` returns 400 (the RAM size is
fixed).

## CLI

```text
zxpoly start ZXPOLY-PENTAGON /path/to/zxpolytest.prom
start ZXPOLY-48K          # the bare machine, like any model
models                    # lists the configurations
zxpoly status
```

## Lua / Python

```lua
local id = zxpoly_start("ZXPOLY-PENTAGON", "/path/to/Alien8.zxp")
local status = zxpoly_status(id)   -- status.locked, status.video_mode, status.modules[1].registers
```

```python
id = zxpoly_start("ZXPOLY-PENTAGON", "/path/to/Alien8.zxp")
status = zxpoly_status(id)          # dict, None if not a ZX-Poly machine
```

## Reading the status

- **`locked`:** `#3D00` bit 7. A ZX-Poly edition (a `.zxp`, or a multiloader
  after its `SETPOLYMAIN`) runs locked.
- **Before the lock:** the slaves wait (`slaves_running` false), or they run
  their own code while the program coordinates them through the platform
  ports, as the Test ROM does.
- **`divergence`:** compares the control state of every slave with the
  master: PC, SP, I, IM, IFF1, HALT, T-state and `#7FFD`. A split means the
  program branched on data that differs per module (its graphics). That is
  the content boundary of the platform, not an emulator fault. Data
  registers are not compared: mid-draw they legitimately hold per-module
  graphics bytes.
- **After a reset:** a reset of the master is a system reset. `#3D00` goes to
  0, and the master alone is shown until a multiloader locks the machine
  again.
- **No time travel:** time travel is not supported on ZX-Poly machines
  (deferred). `time_travel` is not blocked, but it acts on the master
  alone: a seek splits the master from the slaves. Do not use it here.
