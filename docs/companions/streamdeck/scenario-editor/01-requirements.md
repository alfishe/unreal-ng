# Scenario editor: requirements

A **scenario** is what a Stream Deck does: a tree whose inner nodes are **menus** (a layout of keys, dials and the
touch strip) and whose leaves are **actions** (HTTP and unreal-ng WebAPI calls, shell commands, keystrokes,
sequences, navigation). Stream Deck Studio's editor builds scenarios visually; the daemon `unreal-streamdeck` plays
them; agents do everything over WebAPI and MCP.

State: **done** = in the code today; **planned** = designed, with the phase that brings it
([TODO.md](../../../inprogress/2026-10-19-streamdeck-integration/TODO.md)).

## Contents

- [1. Goals](#1-goals)
- [2. Scenario model](#2-scenario-model)
- [3. Navigation](#3-navigation)
- [4. Actions](#4-actions)
- [5. Editor](#5-editor)
- [6. Automation](#6-automation)
- [7. Non-functional](#7-non-functional)
- [8. Not in v1](#8-not-in-v1)

## 1. Goals

| # | Goal |
|:--|:--|
| G1 | Build a scenario tree visually; every menu has its own layout (which keys, how many) |
| G2 | Stack navigation (enter = push, back = pop) with breadcrumbs, on by default, configurable per menu |
| G3 | One engine: the editor's canvas draws with the runtime's renderer, and Run plays the scenario in the daemon, the same code that plays it on the real deck |
| G4 | Live edit: every change reaches the running deck without losing the menu stack |
| G5 | Everything a person does in the editor, an agent does over WebAPI / MCP, screenshots included |
| G6 | Scenarios split over files: a subtree in its own file, included by several scenarios |

## 2. Scenario model

| # | Requirement | State |
|:--|:--|:--|
| R1 | One document: `format`, `version`, `name`, `model`, `settings`, `root`. Nested, no cross-links by UUID: a subtree copied to another file stays valid | done (v1) |
| R2 | Node ids unique and readable (`debug`, `debug.step`); the editor makes new ids unique (`-2`, `-3`, ...) and keeps them editable | done |
| R3 | Menu: title, face (its key in the parent), items at `[row, col]`, dial bindings (turn, press), strip content, navigation overrides | done |
| R4 | Action: title, face, `do` (the action), `then` (stay, back, home), `face_on` for toggles | done |
| R5 | Positions per menu: a submenu uses other keys and another count than its parent | done |
| R6 | The scenario's model fixes the grid; a slot outside it is an error. On a deck of another model the scenario still plays; keys outside that grid are not shown | done |
| R7 | Validation: missing or duplicate ids, slots outside the grid, two items on one slot, an item under the back key, unknown action kinds, missing fields per kind, nav targets that do not exist, nesting deeper than 8, dial bindings on a model without dials, a dial index out of range or bound twice, bad colours | done |
| R8 | Tags on nodes (`["debug", "ttd"]`), for search and for agents | done |
| R9 | Includes: a node links to a subtree in another file (`{"type": "include", "file", "node"?}`), resolved before playing; nested up to 8, cycles are errors | done |
| R10 | Format v2: `format: "streamdeck-scenario"`, `version: 2`; `settings.webapi`, `emulator`, `companion_file` leave the scenario (the daemon's configuration); targets; activity settings | done 2026-10-20 (v1 still read, converted on Save) |
| R11 | Path roots in scenario paths: `$data`, `$testdata`, `$user`, `$icons`, `$scenario` ([scenarios.md §3](../../../inprogress/2026-10-19-streamdeck-integration/scenarios.md#3-paths)) | planned (SD5) |

## 3. Navigation

| # | Requirement | State |
|:--|:--|:--|
| R12 | Stack navigation on by default (`settings.navigation.stack`). Off: entering a menu replaces the current one, no back key, no path | done |
| R13 | Breadcrumbs per menu: `inherit` (default), `strip`, `key`, `none`; the root setting `auto` resolves to `strip` on models with a touch window (+, + XL) and `key` elsewhere | done |
| R14 | Strip breadcrumbs: the path as chips, the middle collapsed to `..` when long; tap a chip = pop to it; swipe right = back; a long press on the strip = home | done |
| R15 | Back key: shows the parent's title and the depth; press = back, hold 600 ms = home (`home_on_hold`); its slot is per menu (`nav.back`), inherited up the stack, else `settings.navigation.back` (`[0, 0]`) | done |
| R16 | Auto exit: a menu pops itself after N idle seconds (a shrinking bar and "BACK IN n" on the strip); an action goes back or home after it ran (`then`) | done |
| R17 | Dials: a menu's bindings replace the parent's per dial index; unbound dials inherit unless the menu says `nav.dials: own` or the scenario `inherit_dials: false` | done |
| R18 | Contexts on menus: the host navigates the deck to the menu of the current context (`ttd`, `debug`, ...) | planned (SD5) |

## 4. Actions

| # | Requirement | State |
|:--|:--|:--|
| R19 | `webapi`: an unreal-ng WebAPI call relative to `/api/v1/emulator/{id}` at `settings.webapi`, the emulator chosen by `settings.emulator` (`auto` = unreal-qt), the companion token when paired | done (v1) |
| R20 | `http` (any URL, method, headers, body), `multi` (steps in order, nestable), `delay`, `first` (the first step that works), `dir` (a dial: left / right), `toggle` (two actions alternate, the face shows the state) | done |
| R21 | System: `shell` (with a timeout), `open` (URL or file with the system handler), `keys` (a combination or text) | done |
| R22 | `nav` (back, home, a node, another scenario file), `brightness` (value or delta) | done |
| R23 | Time travel on a dial: `ttd_seek` (frames or seconds a detent, faster with the turning speed), `run_toggle` (run or pause by the machine's state); the strip shows the position (`strip.status: "ttd"`) | done (v1; replaced by catalogue actions in SD5) |
| R24 | Actions run off the input thread, one at a time, in order; failures show on the strip ("<id> FAILED") and in the log | done |
| R25 | `action` kind: an action of the host's catalogue by id (`{"kind": "action", "id": "ttd.seek_by", "value": ...}`); `webapi` stays as the escape hatch | done 2026-10-20 (REST fallback; the surface client: S4 / SD4) |
| R26 | State-bound faces: `state` names a readout, the face is `face_on` while it is true | planned (SD5) |

## 5. Editor

| # | Requirement | State |
|:--|:--|:--|
| R27 | Lives in Stream Deck Studio: its own window or docked beside the deck | done |
| R28 | Tree of the whole scenario, design canvas of one menu in the model's geometry, inspector (form and JSON), palette, breadcrumbs, issues, runtime output | done |
| R29 | Drag from the palette onto a key; drag keys to move, swap or into a menu; double-click a menu to enter it | done |
| R30 | Undo / redo for every change; copy, cut, paste, duplicate (ids made unique) | done |
| R31 | New, Open, Save, Save as, Export a copy; JSON, YAML, CSV, TSV by extension | done |
| R32 | Subtrees between files: export, move to a file (replaced by an include), import as a submenu, import its keys, link (include), inline an include | done |
| R33 | Included nodes are shown (italic) and read-only; edit them in their own file, or inline | done |
| R34 | Run on the deck the window shows, through the daemon; edits go live; Stop gives the deck back | done |
| R35 | The palette from the host's action catalogue, action forms from their parameters, unknown ids as issues | done 2026-10-20: palette, picker (shipped or live catalogue), unknown ids and held actions as warnings; forms per param: SD4 |
| R36 | User presets from `<config>/unreal-ng/streamdeck/presets/*.json` | done |
| R37 | Open the shipped example scenarios (`data/streamdeck/scenarios/`), the default scenario v2 among them | waits for the icon set and the use-case document (owner, 2026-10-20) |

## 6. Automation

| # | Requirement | State |
|:--|:--|:--|
| R38 | WebAPI on `127.0.0.1:37052`, OpenAPI at `/api/v1/openapi.json`, MCP at `/mcp` with `search_api` and `invoke_api` | done |
| R39 | Every editor operation as a route: document, tree, nodes (add, patch, delete, move, duplicate, export, inline), import, selection, canvas, undo, issues, presets, run, stop | done |
| R40 | What the canvas shows as JSON (`/editor/canvas`), so an agent needs no image analysis | done |
| R41 | Screenshots as PNG: the canvas, its strip, the editor window | done |
| R42 | The running scenario through the studio's port: `/api/v1/runtime/state`, `/page`, `/navigate`, `/press` | done |

## 7. Non-functional

| # | Requirement | State |
|:--|:--|:--|
| N1 | The engine (`sdb`) has no Qt; the editor is Qt 6 Widgets; the studio does not link hidapi | done |
| N2 | macOS, Linux, Windows; zero warnings on clang, gcc, MSVC, MinGW | done (builds and tests; hardware runs on Windows / Linux open, SD9) |
| N3 | Saving is atomic (a temporary file, then rename): a watching daemon never reads half a file | done |
| N4 | The engine is tested without hardware (`streamdeck-tests`): model, validation, navigation, layout, rendering, actions, operations, includes, the runtime against a fake device | done |
| N5 | No vendor-confidential material: behaviour from public documentation and measurements ([hid/](../hid/README.md)) | done |

## 8. Not in v1

Pages as their own concept (a root menu with sibling menus covers it), pinned keys, switching scenarios by the
focused application, animated faces, faces bound to live state (R26 brings it), text in other fonts than the 5x7
pixel font (key text is upper-cased; characters the font lacks are replaced).
