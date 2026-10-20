# Stream Deck integration: surface protocol additions

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Base** | control surfaces `protocol.md` (`/api/v1/surfaces/ws`, v1: `auth`, `join`, `action`, `subscribe`, `welcome`, `result`, `ctx`, `readout`, `instance`, `revoked`, `error`) |

Everything here is an **addition** to protocol v1: peers that do not know a field or an op ignore it (base §8). The
base `instance` message is replaced by `target`.

## Contents

- [1. Join](#1-join)
- [2. Target](#2-target)
- [3. Readouts](#3-readouts)
- [4. Activity](#4-activity)
- [5. The action catalogue](#5-the-action-catalogue)
- [6. Machine lifecycle without REST](#6-machine-lifecycle-without-rest)
- [7. Errors](#7-errors)
- [8. Example](#8-example)

## 1. Join

| Field | Meaning |
|:--|:--|
| `name`, `model`, `serial` | As base. The daemon joins **once per deck** (one session per serial) so each deck has its own target and context page. |
| `target` | `"follow"` (default), `{"view": "<id>"}`, `{"instance": "<id>"}` ([targeting.md §4](targeting.md#4-pins)). Replaces the base `instance`. |
| `features` | What the client wants pushed: `["target", "activity", "catalogue"]`. |

`welcome` adds `target` (the [§2](#2-target) object), `views` (in focus order) and `catalogue_rev` (§5).

## 2. Target

```json
{"op":"target","instance":"0f3a…","index":3,"model":"PENTAGON","ram_kb":128,
 "view":{"id":"v2","name":"Window 2"},"mode":"follow"}
```

- Sent on every change of the session's target ([targeting.md §3](targeting.md#3-rules)), before the new instance's
  `ctx` and a full `readout` (all subscribed values, not only changed ones).
- `instance: null` when there is no machine.
- Client to core: `{"op":"pin","target":"follow"|{"view":…}|{"instance":…}}`; answered by `target`.

## 3. Readouts

Base v1 has `ttd.position` and `run.state`. A deck draws more state, so readouts become a small vocabulary in the
core, one producer each, all from published values (never a read that holds the emulation thread):

| Readout | Value | Source | Rate |
|:--|:--|:--|:--|
| `ttd.position` | `{frame, tinframe, first, end, state, fps}` | TTD published cursor | ≤ 50 Hz |
| `run.state` | `{state: running\|paused\|stopped, speed, turbo}` | run control | on change |
| `machine` | `{model, ram_kb, index}` | instance | on change |
| `media.tape` | `{loaded, playing, block, blocks, progress}` | tape manager | ≤ 10 Hz |
| `media.disk` | `{drives: [{letter, loaded, name, busy}]}` | disk manager | on change |
| `audio.mute` | `{muted, master}` | mixer | on change |
| `debug.mode` | `{enabled}` | debugger | on change |
| `toggle.<action id>` | `{on}` | the action's `State()` (§5) | on change |

- `subscribe` takes names or prefixes (`media.*`). Unknown names are answered once by `error not_found` and ignored.
- `toggle.*` exists for every catalogue action whose kind is a toggle: a deck key bound to `mach.turbo_toggle` shows
  its face from `toggle.mach.turbo_toggle`, never from a count of its own presses (today the runtime flips a local
  boolean on success, wrong when another surface toggles).

## 4. Activity

Subscribed by `features: ["activity"]` ([architecture §5](architecture.md#5-the-activity-feed)):

```json
{"op":"activity","instance":"0f3a…","seq":812,"id":"run.pause_toggle","value":{"p":true},
 "origin":"host_device","surface":"Xbox Wireless Controller","ok":true,"t_ms":1760870000123}
```

- For the session's target instance only.
- Own actions are not echoed.
- Merged per id within 100 ms (deltas summed), at most 20 per second; when the socket is not writable the oldest are
  dropped and the next message carries `"dropped": n`.

## 5. The action catalogue

The deck's palette, the studio's validation and the daemon's "is this id known" all need the catalogue.

| Where | What |
|:--|:--|
| `GET /api/v1/controllers/actions` (control surfaces planes) | `{rev, actions: [{id, label, group, kind, params?, toggle?, scope, gui?, description}]}` |
| `catalogue` op (pushed when `features` has it) | `{rev}` after a change: the Qt client registered or dropped its GUI actions |
| `action` on the socket | unchanged |

Additions to the registry's `ActionInfo`:

| Field | Why |
|:--|:--|
| `description` | One sentence for the palette's tooltip and the MCP tool. |
| `params` | Schema of the value's parameters (`ttd.seek_by`: `unit`, `min`, `max`), so the inspector draws a form. |
| `toggle` | The action has an on / off state; `State()` feeds `toggle.<id>`. |
| `scope` | The companion scope it needs ([companion protocol](../2026-10-16-companion-client/protocol.md)). |

## 6. Machine lifecycle without REST

Today's scenario replaces machines with `DELETE` + `POST /api/v1/emulator/start {"replace": true}` and loads software
with three REST steps (replace, delay 600 ms, `/snapshot/load`). As actions, with values:

| Action | Value | Does |
|:--|:--|:--|
| `mach.start` | `{model, ram_kb?, config?, replace?: true}` | Start a machine; with `replace` the target's instance is replaced in place (same view, the old one destroyed when the new one runs), `symbolic_id` kept. |
| `mach.cold_reset` | `{ram_power_on?: zero\|random}` | `$model`-style restart of the same model. |
| `media.open` | `{path, kind?: auto\|snapshot\|tape\|disk, drive?, autostart?: true}` | Open software on the target, with the fast-load settings; replaces the "replace + delay + load" chain. |
| `media.tape` | `{op: play\|stop\|rewind\|eject}` | |
| `snapshot.quick` | `{op: save\|load, slot?}` | |

- `mach.start` with `replace` answers when the new instance is the target's active (no delay steps).
- `media.open` paths are resolved by the **host** ([scenarios.md §3](scenarios.md#3-paths)): `$data/…`,
  `$testdata/…`, `$user/…`, or an absolute path.
- Everything else in today's 140 REST paths maps either to an existing action or to a registry action added with this
  work; the mapping table is in [migration.md §4](migration.md#4-rest-paths-to-action-ids).

## 7. Errors

No new codes. `Not Found` covers an unknown readout name (§3) or an unknown view in a pin; `not_now` covers a pin on
a headless host (no views).

## 8. Example

```json
→ {"op":"join","v":1,"name":"Stream Deck + XL","model":"elgato.streamdeck.plusxl","serial":"A1B2C3",
   "target":"follow","features":["target","activity","catalogue"]}
← {"op":"welcome","v":1,"session":"s7","target":{"instance":"0f3a…","index":1,"model":"PENTAGON","ram_kb":128,
   "view":{"id":"v1","name":"Main window"},"mode":"follow"},"context":"play","catalogue_rev":4,
   "views":[{"id":"v1","name":"Main window","active":"0f3a…"}],"scopes":["run","input","media","machine","rewind","takeback","debug"]}
→ {"op":"subscribe","readouts":["run.state","ttd.position","toggle.*","media.*"],"max_hz":50}
← {"op":"readout","instance":"0f3a…","values":{"run.state":{"state":"running","speed":1,"turbo":false}, "...":"..."}}
← {"op":"activity","instance":"0f3a…","seq":3,"id":"run.pause_toggle","origin":"host_device",
   "surface":"Xbox Wireless Controller","ok":true}
← {"op":"readout","instance":"0f3a…","values":{"run.state":{"state":"paused","speed":1,"turbo":false}}}
← {"op":"target","instance":"7c21…","index":2,"model":"SPRINTER","view":{"id":"v2","name":"Window 2"},"mode":"follow"}
← {"op":"ctx","instance":"7c21…","context":"debug","by":"gui"}
```
