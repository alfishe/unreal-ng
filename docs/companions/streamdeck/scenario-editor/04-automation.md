# Scenario editor: automation

The editor's routes on Stream Deck Studio's automation port, the runtime proxy, and the daemon routes Run uses.
The studio's other routes (device, input, screenshots, log, recording, window, connection): [studio.md
§7](../studio.md#7-automation). The daemon's: [daemon.md](../daemon.md#automation). Sources:
[`studio/editor/editorapi.cpp`](../../../../companions/streamdeck/studio/editor/editorapi.cpp),
[`lib/src/daemondecks.cpp`](../../../../companions/streamdeck/lib/src/daemondecks.cpp).

## Contents

- [1. Planes](#1-planes)
- [2. Editor routes](#2-editor-routes)
- [3. Runtime proxy](#3-runtime-proxy)
- [4. What Run calls on the daemon](#4-what-run-calls-on-the-daemon)
- [5. Examples](#5-examples)

## 1. Planes

| Plane | Where |
|:--|:--|
| WebAPI | `http://127.0.0.1:37052/api/v1/...` (`--api-port`, 0 = off); the description at `GET /api/v1/openapi.json` |
| MCP | `POST /mcp` on the same port: Streamable HTTP, protocol 2025-06-18 (2025-03-26 and 2024-11-05 accepted), two tools: `search_api {query, method?, limit?, auto_invoke?, body?, query_params?}` finds routes by keyword, `invoke_api {method, path, query_params?, body?}` calls one. Screenshots come back as image content |
| stdio | `unreal-mcp-bridge --url http://127.0.0.1:37052/mcp`; Automation > Copy MCP Config (.mcp.json) writes the entry (`streamdeck-studio`) |

- Every handler runs on the GUI thread, so a route sees what the window shows, and an edit through a route is an
  undo step like an edit by hand.
- Errors: 400 (bad arguments), 404 (no such node, menu, preset or model), 422 (the operation was refused: the
  message says why), 500 (a file could not be written), 502 (the daemon's automation did not answer).
- Edits answer `{ok, id, issues, node}`: the node created, moved or renamed, and the document's issues after the
  change.

## 2. Editor routes

### Document and files

| Route | Body / query | What |
|:--|:--|:--|
| `GET /api/v1/editor` | | Status: `path`, `dirty`, `name`, `model`, `selected`, `canvas_menu`, `can_undo`, `can_redo`, `issues` (`errors`, `warnings`), `run` ([below](#run)) |
| `POST /api/v1/editor/new` | `model` (default `plusxl`), `name` | A new empty scenario (root `home`) |
| `POST /api/v1/editor/open` | `path` (required) | Open `.json`, `.yaml`, `.yml`, `.csv`, `.tsv` |
| `POST /api/v1/editor/save` | `path` | Save, to `path` or the current file (400 when untitled without `path`); the format follows the extension; always format v2: a v1 document is converted first (`converted`: the notes) |
| `POST /api/v1/editor/convert` | | v1 -> v2 as one undo step: `converted` (false when v2 already), `version`, `notes` (what changed, which REST steps stayed), `issues` |
| `GET /api/v1/editor/catalogue` | | The action catalogue the editor offers: `source` (the shipped file or the emulator), `actions` (`id`, `label`, `group`, `kind`, `held`, `gui`, `params`) |
| `POST /api/v1/editor/catalogue/refresh` | `webapi` (default the daemon's host, `http://127.0.0.1:8090`) | Take the catalogue from a running emulator (`GET /api/v1/controllers/actions`); validation runs again; 502 when it does not answer |
| `POST /api/v1/editor/export` | `path` (required) | A copy in another format; the document keeps its file |
| `GET /api/v1/editor/document` | `format` (`json` default, `yaml`, `csv`, `tsv`), `resolved` | The whole scenario: the JSON, or `{format, text}`; `resolved=true` replaces includes by what they link to |
| `PUT /api/v1/editor/document` | `document`, or `format` + `text` | Replace the whole scenario (one undo step); answers the issues |
| `POST /api/v1/editor/undo`, `/redo` | | Undo / redo; answers the status |
| `GET /api/v1/editor/issues` | | `issues`: `node`, `message`, `severity` (`error` blocks Run, `warning`) |

### Tree and nodes

| Route | Body / query | What |
|:--|:--|:--|
| `GET /api/v1/editor/tree` | | The tree, compact: `id`, `type`, `title`, `at`, `tags`, `kind`, `included_from`, `items` (includes resolved) |
| `GET /api/v1/editor/nodes` | `tag`, `type` (`menu`, `action`), `kind`, `menu` (direct children of), `text` (in id or title) | `nodes`: a flat, filtered list, included nodes too |
| `GET /api/v1/editor/nodes/{id}` | | One node with its subtree (an included one read-only) |
| `POST /api/v1/editor/nodes` | `parent` (default the menu on the canvas), `at` (`[row, col]` or `{row, col}`; default the first free key), `node` or `preset` (+ `node` as overrides) | Add a node: what a palette drop does. Ids are made unique |
| `PUT /api/v1/editor/nodes/{id}` | `patch`, or the patch as the body | JSON merge patch: `null` removes a field; a new `id` renames (`nav` targets follow); `at` is refused (use move) |
| `DELETE /api/v1/editor/nodes/{id}` | | Delete the node and its subtree |
| `POST /api/v1/editor/nodes/{id}/move` | `at` (required), `parent` (default its menu), `swap` (default `true`) | To another key or menu; with `swap` the occupant takes the old place |
| `POST /api/v1/editor/nodes/{id}/duplicate` | `at` | A copy in the same menu (default the first free key) |
| `GET /api/v1/editor/presets` | | The palette: `category`, `name`, `description`, `node` |

### Subtrees between files

| Route | Body | What |
|:--|:--|:--|
| `POST /api/v1/editor/nodes/{id}/export` | `path` (required), `link` | Write the subtree as a scenario of its own (any format); `link=true` also replaces the node by an include of that file. Answers `links_outside` (`nav` targets that leave the subtree) |
| `POST /api/v1/editor/import` | `path` (required), `mode` (`menu` default, `items`, `link`), `parent` (default the canvas menu), `at` (`menu`, `link`), `node` (a node in the file, default its root) | A copy as one submenu, its root's keys merged into `parent`, or a link (include) |
| `POST /api/v1/editor/nodes/{id}/inline` | | Replace an include by an editable copy of what it links to |

### Selection and canvas

| Route | Body / query | What |
|:--|:--|:--|
| `POST /api/v1/editor/select` | `id` (empty = none) | Select a node; the canvas shows its menu |
| `POST /api/v1/editor/enter` | `id` (required; `..` = up) | Show a menu on the canvas; answers the canvas |
| `GET /api/v1/editor/canvas` | | The shown menu as JSON (`DescribePage`): `menu`, `path`, `breadcrumbs`, `back_key`, `can_back`, `keys` (`key`, `row`, `col`, `role`, `id`, `type`, `title`, `label`, `tags`, `kind`, `then`, `items`), `dials`, `model`, `grid`, `strip`, `auto_exit`, plus `selected`. Empty keys are left out |
| `GET /api/v1/editor/screenshot` | `area` (`canvas` default, `lcd`, `window`), `scale` (0.25..4, canvas), `path` (also save) | PNG: the canvas, its strip at device pixels, or the editor's window. Also `GET /api/v1/screenshot?target=editor` |

### Run

| Route | Body | What |
|:--|:--|:--|
| `GET /api/v1/editor/run` | | `running`, `how` (`deck <serial>`, `started`), `run_file`, `deck`, `api` (the daemon's automation URL), `output` (the last lines) |
| `POST /api/v1/editor/run` | `serial` (default the deck the window shows) | Run on the deck ([02-ux-design.md §3](02-ux-design.md#3-running-on-the-deck)); 422 with the reason (errors in the scenario, the daemon refused the file, no daemon could start) |
| `POST /api/v1/editor/stop` | | Stop: the deck gets the daemon's start scenario back; a daemon started for the run ends |

## 3. Runtime proxy

So an agent needs one endpoint, the studio forwards these to the daemon's automation (`127.0.0.1:37053`, the
studio's `--daemon-api`), for the deck the run plays on (`?deck=<serial>`; none = the daemon's first deck):

| Studio route | Daemon route | What |
|:--|:--|:--|
| `GET /api/v1/runtime/state` | `GET /api/v1/state` | Menu stack, page, toggles, the last action results |
| `GET /api/v1/runtime/page` | `GET /api/v1/page` | What each key and dial means now |
| `POST /api/v1/runtime/navigate` | `POST /api/v1/navigate` | `{to: back \| home \| node, node}` |
| `POST /api/v1/runtime/press` | `POST /api/v1/press` | `{node}` (a node on the current page) or `{key}` |

The answer and status are the daemon's; 502 when it does not answer.

## 4. What Run calls on the daemon

| Call | When |
|:--|:--|
| `GET /api/v1/health` | Does a daemon answer (`{"ok": true}`)? |
| `PUT /api/v1/decks/{serial}/scenario` `{"file": <run file>, "watch": true}` | Run: that deck plays the run file, the other decks keep theirs, the menu stack is kept; `serial` may be `first`. 404 for an unknown deck, 422 for a file with errors |
| `DELETE /api/v1/decks/{serial}/scenario` | Stop: the deck plays the daemon's start scenario again |
| `GET /api/v1/decks/{serial}/scenario` | Not used by Run; shows the deck's `file`, `start_scenario`, `watch`, `name` (`?document=true` adds the document) |

## 5. Examples

```bash
S=http://127.0.0.1:37052/api/v1
curl -s -X POST $S/editor/new -d '{"model":"plusxl","name":"demo"}'
curl -s -X POST $S/editor/nodes -d '{"parent":"home","at":[0,0],"preset":"Menu"}'                    # id "menu"
curl -s -X PUT  $S/editor/nodes/menu -d '{"patch":{"id":"tools","title":"Tools","face":{"label":"TOOLS"}}}'
curl -s -X POST $S/editor/nodes -d '{"parent":"tools","preset":"Shell command","node":{"do":{"command":"date"}}}'
curl -s -X POST $S/editor/nodes -d '{"parent":"home","at":[0,2],"node":{"type":"action","title":"Pause",
  "tags":["run"],"face":{"icon":"pause","label":"PAUSE"},"do":{"kind":"webapi","path":"/pause"}}}'
curl -s "$S/editor/nodes?tag=run"
curl -s "$S/editor/document?format=csv" | jq -r .text
curl -s -X POST $S/editor/save -d '{"path":"/path/to/demo.deck.yaml"}'
curl -s -X POST $S/editor/run -d '{}'
curl -s -X POST $S/runtime/press -d '{"node":"tools"}'
curl -s "$S/screenshot?area=deck" -o deck.png
curl -s -X POST $S/editor/stop
```
