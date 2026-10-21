# Stream Deck integration: scenario format changes

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Base** | `*.deck.json` v1 (`streamdeck-emulator/docs/scenario-editor/03-technical-design.md §2`, `streamdeck-bridge/include/sdb/scenario.hpp`) |

v2 adds fields; a v1 file loads (its `webapi` steps keep working through the REST escape hatch, §2) and the studio
offers to convert it.

Format and version as the code handles them today (`lib/include/sdb/scenario.h`, `lib/src/scenario.cpp`): a document
says `"format": "streamdeck-scenario"` and `"version"` (new documents and the CSV / TSV reader write both; a file
without them loads as the current version). `kFormatVersion` is **1**: the loader accepts any version up to it and
refuses a newer one ("made by a newer version"), so a v2 file does not load until the v2 fields below are implemented
and `kFormatVersion` becomes 2. From then on v1 files keep loading and new documents say `version: 2`.

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

An absolute path still works and is a studio warning. The current scenario's 98 absolute paths (72 test programs, 26
icons, 25 thumbnails, two `/tmp` files) become `$testdata`, `$icons`, `$scenario/thumbnails`, and `$user`.

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

## 6. The palette

- The studio's palette comes from the live catalogue; offline from `$data/streamdeck/catalogue.json`, generated at
  build time (a small tool dumps the core registry's `List()`, as the device database is compiled in), so the palette
  always matches the shipped core.
- Presets: one per catalogue action (id, label, group, a default face from the action's icon hint), plus the generic
  presets. Faces for unreal-ng actions use unreal-qt's icons (`$icons`), so the deck looks like the GUI.

## 7. Contexts on menus

```json
{ "id": "ttd.timecode", "type": "menu", "title": "Time code", "context": "ttd.timecode", "items": [ … ] }
```

Entering sets the context, a `ctx` from the core navigates to the nearest menu with that context
([architecture §6](architecture.md#6-contexts-and-deck-pages)). The default scenario declares `play` (root), `ttd`,
`ttd.timecode`, `debug`, `debug.disasm`, `debug.memory`.

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

- Shipped as `$data/streamdeck/scenarios/unrealng-control.deck.json`, generated by the generator moved into the repo
  (today `make_unrealng_control.py`), from the catalogue instead of the REST presets.
- Software keys reference `$testdata` only in developer builds; the release scenario uses the software that ships in
  `data/` (`testsoft`, `testtapes`), and the generator checks every path exists.
- The release scenario contains no `shell`, `open`, `keys` and no `webapi` step (ACC-5).
