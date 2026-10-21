# Stream Deck integration: scenario format changes

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | v2 implemented 2026-10-20 (SD5, the parts without the surface topic); the default scenario waits (§9) |
| **Base** | `*.deck.json` v1 (`streamdeck-emulator/docs/scenario-editor/03-technical-design.md §2`, `streamdeck-bridge/include/sdb/scenario.hpp`) |

v2 adds fields; a v1 file loads (its `webapi` steps keep working through the REST escape hatch, §2) and the studio
offers to convert it.

Format and version as the code handles them (`lib/include/sdb/scenario.h`, `lib/src/scenario.cpp`): a document says
`"format": "streamdeck-scenario"` and `"version"`; a file without a version loads as the current one. `kFormatVersion`
is **2** (2026-10-20): versions 1 and 2 load, a newer one is refused ("made by a newer version"), 0 or a non-number is
an error. A v1 document loads as v1 (`Scenario::version` keeps it, `ToJson` writes it back, the CSV / TSV writer keeps
an older version in the scenario row): its kinds run as before. New documents (studio, `scenario` ops, the idle
screen, the CSV reader) say `version: 2`. In a v2 document the v1 kinds `ttd_seek` / `run_toggle` and the settings
`webapi` / `emulator` / `companion_file` are warnings, not errors (they still work).

Conversion `scn::ConvertToV2(doc, notes)` (`lib/src/scenarioconvert.cpp`), also `unreal-streamdeck scenario upgrade
<in> <out>`, the studio's **Convert to v2** and its Save: `version: 2`; the three settings dropped (a note when one was
not the default); `run_toggle` -> `run.pause_toggle`; `ttd_seek {unit, min, max}` -> `ttd.seek_by {d: $ticks, rate:
$rate, unit, min, max}` (v1's defaults filled in); the `webapi` POST steps that have a catalogue action ->
action steps (`/pause`, `/resume`, `/step*`, `/run_frames`, `/run_tstates`, `/run_scanlines`, `/run_to_interrupt`,
`/nmi`, `/ttd/start|stop|resume|step-back|step-forward|step-instruction|reverse-step|seek|invalidate`,
`/api/v1/emulator/start {model, ram_size, replace}` -> `mach.start {model, ram_kb, replace}`, `/snapshot/load`,
`/tape/load`, `/disk/X/insert` -> `media.open`, `/tape/<op>` -> `media.tape {op}`, `/basic/run`); every other step stays
`webapi` and the notes list its routes. Composition kinds are converted step by step; a v2 document comes back as it
is.

## Contents

- [1. Settings](#1-settings)
- [2. The action kind](#2-the-action-kind)
- [3. Paths](#3-paths)
- [4. State-bound faces](#4-state-bound-faces)
- [5. Activity](#5-activity)
- [6. The palette](#6-the-palette)
- [7. Contexts on menus](#7-contexts-on-menus)
- [8. Example](#8-example)
- [9. The default scenario](#9-the-default-scenario)

## 1. Settings

| v1 | v2 |
|:--|:--|
| `webapi: "http://127.0.0.1:8090"` | Gone from the scenario: the host is the daemon's configuration ([daemon.md §9](daemon.md#9-configuration)) |
| `emulator: "auto" \| id \| symbolic` | Gone: targeting is the host's ([targeting.md](targeting.md)); a scenario may ask `target: "follow"` (default) or `{"view": 1}` |
| `companion_file` | Gone: the token is the daemon's |
| `brightness`, `navigation.*` | Kept |
| `format: "streamdeck-scenario"`, `version: 1` | `format: "streamdeck-scenario"`, `version: 2` |
| — | `strip.target: true` (default on decks with a strip): the target line ([targeting.md §6](targeting.md#6-showing-the-target)) |

## 2. The action kind

```json
"do": { "kind": "action", "id": "ttd.seek_by", "value": { "d": "$ticks", "rate": "$rate", "unit": "frames", "min": 1, "max": 100 } }
```

| Field | Meaning |
|:--|:--|
| `id` | A catalogue id ([protocol.md §5](protocol.md#5-the-action-catalogue)) |
| `value` | Press actions: omitted (the runtime sends `{"p":true}` on press and `{"p":false}` on release for held actions); delta: `d` and `rate` from the dial (`$ticks`, `$rate`) plus params; text: a string |
| `hold` | `true`: send press and release (held actions like `run.turbo_hold`, `mach.reset` with its 1 s hold) |

- Composition kinds stay (`multi`, `first`, `dir`, `toggle`, `delay`, `nav`); their steps may be `action`.
- `params` (an object) beside `value` adds params; it wins over the same key carried by the value.

Done 2026-10-20 (`sdb/actionexec.h`). How a step becomes a call of the registry (`MakeActionCall`, the routes' body
`{value?, params?}`; SD6: a press's `{"model": …}` is params):

| Step | Call |
|:--|:--|
| no `value` | a press (`value` omitted); with `hold`: `true` on the key's press, `false` on its release |
| `value: {d, rate?, …}` | delta `{delta: d, rate}` (`rate` only when a number), the other fields params (`ttd.seek_by`: `unit`, `min`, `max`) |
| `value: "text"` / `{text, …}` | the text (`ctx.set`, `input.type`), the other fields params |
| `value: {…}` otherwise | params of a press (`mach.start {model, ram_kb, replace}`, `media.open {path}`) |
| `value`: a number / bool | as it is (a level, a press flag) |

- The executor is an interface (`ActionExecutor::Run(ActionCall)`), set per runtime (`RuntimeOptions::executor`);
  the daemon's surface client (S4 + SD4) implements it on the `surface` topic. Until then the **REST fallback**
  posts `POST /api/v1/controllers/actions/{id}` on the host (the companion token when paired; no `instance` while
  `settings.emulator` is `auto`, so the host picks its followed / selected / sole instance). Path roots in params
  (`$data`, `$testdata`, `$user`, `$scenario`) are resolved by the daemon before the call (it runs beside the host);
  the surface client will leave them to the host.
- When a reply names another instance (a machine start in place answers `result.emulator_id`), the fallback sends
  the last `ctx.set` to it again: the new instance starts with the default context.
- `hold` works on keys and on dial presses (`DialDown` / `DialUp`); the release goes even when the page changed.
- The host is the daemon's: `unreal-streamdeck run --host URL` (default `http://127.0.0.1:8090`); a v1 scenario's
  `settings.webapi` still wins for that scenario. `target` / `{"view": n}` and `strip.target` are read and validated;
  acting on them is SD4 (`TargetState`).
- `webapi` stays as the escape hatch for routes without an action (marked in the studio: "REST, breaks when the route
  changes"); it uses the daemon's token, relative paths resolve to the session's **target** instance.
- `ttd_seek`, `run_toggle` (deck-side logic) are replaced by `ttd.seek_by` and `run.pause_toggle`; v1 files keep them
  working through compatibility shims in the daemon until the default scenario no longer uses them.

## 3. Paths

Scenario paths (`face.image`, `face.svg`, `media.open` values, includes) use roots:

| Root | Resolves to | Resolved by |
|:--|:--|:--|
| `$data` | The installed data directory (`data/` of the repo in a dev build) | daemon (images, includes); host (`media.open`) |
| `$testdata` | `testdata/` of the repo; absent in a release install (the studio warns) | both |
| `$user` | The user's unreal-ng directory | both |
| `$icons` | unreal-qt's icon set (shipped in `$data/icons`) | daemon |
| `$scenario` | The scenario file's own directory | daemon |

Done 2026-10-20 (`sdb/datadir.h`, `data::Resolve(path, scenarioDir)`): all five roots for pictures (`face.image`,
`face.svg`, `strip.image`, found as in [daemon.md](../../companions/streamdeck/daemon.md#data-directory)) and for the
params of action steps; `$testdata` = `testdata/` beside the data directory (a source tree), else beside the source
tree's `data/`, absent in a release; `$user` = `~/.unreal-ng` (the core's `FileHelper::GetUserDataPath`); `$scenario`
= the scenario file's folder (`Scenario::dir`, set by the daemon's loader and the studio's document). A relative path
is relative to the scenario's folder, not the working directory; a root that is not there leaves the path as written.
A region of a file (`#xywh=x,y,w,h`) spreads one picture over several keys. Validation: an unknown root (`$nowhere/`)
is an error, an absolute path a warning; the studio's image picker writes `$data/...` or a path relative to the
scenario. Still open: includes rebasing the relative pictures of an included file onto its own folder.

Rule: the TTD timeline is a strip readout, never keys (`strip.status: "ttd"`, the strip's picture `lcd_ttd` of the
theme); no scenario, theme or idle screen spreads it over keys.

## 4. State-bound faces

Today toggles flip a local flag on success; faces cannot show a state another surface changed.

```json
{ "id": "turbo", "type": "action",
  "face":    { "icon": "speed", "label": "TURBO" },
  "face_on": { "icon": "speed", "label": "TURBO", "bg": "8A4B00" },
  "state":   "toggle.mach.turbo_toggle",
  "do": { "kind": "action", "id": "mach.turbo_toggle" } }
```

| Field | Meaning |
|:--|:--|
| `state` | A readout path; the face is `face_on` while it is true. Paths: `toggle.<id>`, `run.state.paused`, `audio.mute.muted`, `debug.mode.enabled`, `media.tape.playing`, … ([protocol.md §3](protocol.md#3-readouts)). |
| `strip.status` | `ttd` draws the TTD readout from `ttd.position` (today from polling); new: `run`, `tape`, `target`. |

The runtime subscribes to exactly the readouts the current page needs plus the strip's.

Done 2026-10-20 without the subscription (S4 / SD4): `state` is parsed, validated (a readout path,
`IsReadoutPath`; `state` without `face_on` is a warning) and drawn. `Runtime::SetReadout(path, value)` is where the
surface client will put the readouts (true / false, a number, or a readout object with `on`); a pushed path is never
polled. Until then the runtime asks the host for the page's readouts nobody pushes (`ActionHost::Readout`, every
250 ms and at once after an action; the system host answers `run.state.paused` from `GET /api/v1/emulator/{id}`),
and a state-bound key whose readout never came flips locally on success, as a toggle does. `GET /page` and `/state`
show `state`, `on` and the readouts. `strip.status` takes `ttd`, `run`, `tape`, `target`; only `ttd` is drawn yet.

## 5. Activity

How the deck shows actions run by **other** surfaces ([architecture §5](architecture.md#5-the-activity-feed)):

```json
"settings": { "activity": { "flash_keys": true, "strip": "none", "seconds": 1.0, "ignore": ["ttd.seek_by"] } }
```

| Field | Meaning |
|:--|:--|
| `enabled` | `true` (default); `false` turns the activity look off for the whole scenario |
| `flash_keys` | Flash every visible key bound to the same id (the press look, 200 ms); **default `true`** |
| `strip` | `none` (**default**), or `line`: an extra message "PAUSE · Xbox pad" on the strip for `seconds` |
| `ignore` | Ids not shown (a Dial scrub is already visible on the TTD strip) |

Default (owner, 2026-10-19): **the key flash only**; options: off (`enabled: false`), or the flash plus the strip message (`strip: "line"`). The studio shows both in the scenario settings. Per node `"activity": false` turns it off for one key.

Done 2026-10-20 without the feed: the settings and `activity` per node are parsed and validated;
`Runtime::ShowActivity(id, text)` flashes the visible keys bound to that id (200 ms, the press look) and with
`strip: "line"` puts `text` on the strip for `seconds`; ignored ids and `enabled: false` show nothing. The activity feed
that calls it is S4.

## 6. The palette

- The studio's palette comes from the live catalogue; offline from `$data/streamdeck/catalogue.json`, generated at
  build time (a small tool dumps the core registry's `List()`, as the device database is compiled in), so the palette
  always matches the shipped core.
- Presets: one per catalogue action (id, label, group, a default face from the action's icon hint), plus the generic
  presets. Faces for unreal-ng actions use unreal-qt's icons (`$icons`), so the deck looks like the GUI.

Done 2026-10-20: `sdb/catalogue.h` (parse the route's reply or the file, load, fetch, `cat::Check`: an unknown id, a
held action without `hold` (an action with a `hold?` param is a tap unless its params hold), a delta on a press, a
text action without text, as warnings). `data/streamdeck/catalogue.json` ships (95 actions, a snapshot of a running
unreal-qt, GUI actions included; `tools/make_deck_scenario.py --fetch-catalogue URL` refreshes it); generating it at
build time from the core registry is still open. The studio's palette has one preset per catalogue action (category
`unreal-ng: <group>`, a face from the group's icon); the inspector's picker can take the running emulator's catalogue;
`unreal-streamdeck scenario check` warns against it.

## 7. Contexts on menus

```json
{ "id": "ttd.timecode", "type": "menu", "title": "Time code", "context": "ttd.timecode", "items": [ … ] }
```

Entering sets the context, a `ctx` from the core navigates to the nearest menu with that context
([architecture §6](architecture.md#6-contexts-and-deck-pages)). The default scenario declares `play` (root), `ttd`,
`ttd.timecode`, `debug`, `debug.disasm`, `debug.memory`.

Done 2026-10-20: `context` on menus (lower case, digits, `.`, `_`, `-`); when the menu changes, the innermost menu of
the path that declares one sets it through the action `ctx.set` (leaving them all sends `""`, the default; the same
context is not sent twice). `Runtime::OnContext(ctx)` (for the surface client) goes to the nearest menu declaring it:
the current path innermost first, below the current menu, then the whole tree; it is not sent back. Checked live on
the Stream Deck + XL: `play` on start, `ttd` and `debug` on entering, `play` on home, and again on a new instance
after a machine start in place.

## 8. Example

```json
{
  "format": "streamdeck-scenario", "version": 2,
  "name": "unreal-ng control", "model": "plusxl",
  "settings": { "brightness": 70, "target": "follow",
                "activity": { "flash_keys": true, "ignore": ["ttd.seek_by"] } },
  "root": {
    "id": "home", "type": "menu", "title": "Home", "context": "play",
    "strip": { "content": "title", "status": "ttd", "target": true },
    "items": [
      { "id": "run", "type": "action", "at": [0, 1],
        "face": { "icon": "$icons/pause.svg", "label": "PAUSE" },
        "face_on": { "icon": "$icons/play.svg", "label": "RUN", "bg": "004D1A" },
        "state": "run.state.paused",
        "do": { "kind": "action", "id": "run.pause_toggle" } },
      { "id": "elite", "type": "action", "at": [1, 1], "face": { "image": "$scenario/thumbnails/elite.png" },
        "do": { "kind": "multi", "steps": [
          { "kind": "action", "id": "mach.start", "value": { "model": "PENTAGON", "ram_kb": 128, "replace": true } },
          { "kind": "action", "id": "media.open", "value": { "path": "$testdata/loaders/tap/elite.tap" } } ] } },
      { "id": "ttd", "type": "menu", "title": "Time travel", "context": "ttd", "at": [0, 5],
        "dials": [ { "index": 0, "label": "FRAMES",
          "turn":  { "kind": "action", "id": "ttd.seek_by",
                     "value": { "d": "$ticks", "rate": "$rate", "unit": "frames", "min": 1, "max": 100 } },
          "press": { "kind": "action", "id": "run.pause_toggle" } } ],
        "items": [ ] }
    ]
  }
}
```

## 9. The default scenario

**Waits (owner, 2026-10-20):** most of the old deck content is test material. Order: (1) audit the icons and build one
icon set, (2) a document with every use case and its menu hierarchy mapped to icons, (3) only then the scenarios,
generated from that document's spec. No default scenario ships until then; the machinery below is done.

- The generator, `companions/streamdeck/tools/make_deck_scenario.py` (2026-10-20): its input is a **menu-hierarchy
  spec** (JSON: menus, keys, dials, each bound to a catalogue action id, faces by icon name; format in the script's
  header), plus the catalogue. It writes the v2 document: action steps with value / hold, faces from the icon set
  (`$data/streamdeck/themes/unrealng/keys/<icon>.png`, else a built-in icon with the label), keys placed row by row
  where the spec gives no slot, the back key kept free. It stops on an id the catalogue lacks, an icon in neither set,
  a `$data` path that is not there, a `shell` / `open` / `keys` / `webapi` step (ACC-5), a duplicate id, a full grid.
  Deterministic output; `--check` compares with a file. Its test fixture (`tools/tests/fixture.spec.json`,
  `fixture.catalogue.json`, `fixture.deck.json`) is no product layout; `tools/tests/test_make_deck_scenario.py`
  checks that the checked-in output is the generator's, determinism and the errors, and `streamdeck-tests` parses
  the output (v2, no issue, clean against the catalogue).
- Then: the use-case document's spec goes through it into `$data/streamdeck/scenarios/unrealng-control.deck.json`,
  with a test that the shipped file equals the generator's output.
- Software keys reference `$testdata` only in developer builds; the release scenario uses the software that ships in
  `data/` (`testsoft`, `testtapes`), and the generator checks every path exists. Found 2026-10-20: `data/testsoft`,
  `testtapes` and `testsnapshots` (about 1 MB) are in no package and not in a build's `bin/` (`$data` is the folder
  holding `streamdeck/`): shipping them beside `streamdeck/` is SD8's.
- The release scenario contains no `shell`, `open`, `keys` and no `webapi` step (ACC-5).
