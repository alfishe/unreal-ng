# Recipe: Symbol Files in Every Format, Symbol Sets

Goal: load labels from any assembler's or tool's symbol file, see where each label came from, decide which file wins,
and write the labels back in another tool's format.

The labels of the main CPU live in symbol **sets**:

| Set | What it holds | Priority |
|-----|---------------|----------|
| `user` | labels added or edited by hand (label editor, `POST /labels`, `label add`) | 1 000 000: wins over every file |
| `file:<path>` | one per loaded file (a native file with several sets: `file:<path>#<set id>`) | 100, 101, ... by load order: a later load wins; loading the same path again replaces its set |
| any name | what `import` with `set` merged into it | the next file priority when it is made; change it with `set` |

When two sets name the same label, the higher priority wins; between equal priorities the later set wins. An address
shows the last label placed at it. Editing a file's label puts the edit in `user`. The file's own record stays below
it, so reloading the file keeps the edit, and switching `user` off shows the file's record again.

Formats: `formats` lists them. The current list has the unreal-ng map and `.l` formats, simple `.sym`, VICE, sjasm
EQU, z88dk (DEFC, `.map`, `-g` / `-s`), sjasmplus `.sym` / `.sld` / `.lst`, pasmo, CSpect, IDA (IDC / Python),
Ghidra, MAME and the lossless native `*.usym.json`.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines,
> [CLI](#cli) on the telnet console, [Lua / Python](#lua--python) inside scripts. Every surface goes through one layer
> (`SymbolControl`, `core/src/debugger/labels/symbolcontrol.h`), so the options, refusals and reply fields are the same
> everywhere.

## MCP (preferred)

```text
manage_symbols {"action":"formats"}
manage_symbols {"action":"detect","path":"scratch/game.sym"}
manage_symbols {"action":"import","path":"scratch/game.sym"}                          # its own set
manage_symbols {"action":"import","path":"scratch/bank3.sym","set":"game","space":"ram3","policy":"replace"}
manage_symbols {"action":"sets"}
manage_symbols {"action":"set_enable","id":"file:/abs/scratch/game.sym","enabled":false}
manage_symbols {"action":"set_enable","id":"game","priority":50}
manage_symbols {"action":"drop","id":"game"}
manage_symbols {"action":"export","path":"scratch/out.sym","format":"sjasmplus-sym"}
manage_symbols {"action":"export","path":"scratch/all.usym.json","format":"native"}     # every set, lossless
```

## WebAPI

| Method | Path | Body / query |
|--------|------|--------------|
| GET | `/api/v1/symbols/formats` (no instance), `/api/v1/emulator/{id}/symbols/formats` | |
| GET | `/api/v1/emulator/{id}/symbols/detect` | `?path=` |
| GET | `/api/v1/emulator/{id}/symbols/sets` | |
| PUT | `/api/v1/emulator/{id}/symbols/sets` | `{id, enabled?, priority?}` |
| DELETE | `/api/v1/emulator/{id}/symbols/sets` | `?id=` |
| POST | `/api/v1/emulator/{id}/symbols/import` | `{path \| data (base64) + name, format?, set?, space?, base?, policy?}` |
| POST | `/api/v1/emulator/{id}/symbols/export` | `{path, format?, sets? (array or "a,b"), pages?}` |

A set id holds a path, so it travels in the body or the query, never in the URL path.

```bash
E=http://localhost:8090/api/v1/emulator/0
curl -s -X POST $E/symbols/import -H 'Content-Type: application/json' -d '{"path":"/abs/data/symbols/48k_rom.map"}'
# {"path":..., "format":"unreal-map", "score":95, "set":"file:/abs/data/symbols/48k_rom.map", "records":1114,
#  "added":1114, "aliased":0, "updated":0, "skipped":0, "conflicts":[], "diagnostics":[...], "labels":1112}
curl -s -X PUT $E/symbols/sets -H 'Content-Type: application/json' -d '{"id":"file:/abs/game.sym","enabled":false}'
curl -s -X POST $E/symbols/export -H 'Content-Type: application/json' \
     -d '{"path":"/abs/out.idc","format":"ida-idc","sets":["file:/abs/game.sym"]}'
```

## CLI

```text
symbols formats
symbols detect <file>
symbols sets
symbols import <file> [--format f] [--set s] [--space ram3] [--base n] [--policy both|keep|replace|fail]
symbols set <id> on|off
symbols set <id> priority <n>
symbols drop <id>
symbols export <file> [--format f] [--sets a,b] [--pages fold|comment|drop]
```

Add `--json` to any of them for the reply as JSON (the WebAPI's fields).

## Lua / Python

```lua
local r = symbols_import{path = "/abs/game.sym", set = "game", policy = "keep"}
print(r.format, r.added, r.labels)
symbols_set("game", {enabled = false})
symbols_export{path = "/abs/out.sym", format = "sjasmplus-sym", sets = {"game"}}
for _, s in ipairs(symbols_sets().sets) do print(s.id, s.priority, s.enabled, s.symbols) end
```

```python
r = emu.symbols_import("/abs/game.sym", set="game", policy="keep")
emu.symbols_set("game", enabled=False, priority=50)
emu.symbols_export("/abs/out.sym", format="sjasmplus-sym", sets=["game"])
```

A refusal does not raise: the table / dict has `ok = false` and `error` = the message.

## Options

| Option | Verb | Meaning |
|--------|------|---------|
| `data`, `name` | import | The file itself as base64 instead of `path` (a client on another host); `name` gives its extension (the format) and the set `upload:<name>`. |
| `format` | import, export | A codec id from `formats`. Import default: by the extension (`.map .sym .vice .s .asm .z88` as `symbols load` always did; a z80asm `.map` goes to `z88dk-map`), else detected. Export default: by the extension. |
| `set` | import | Merge into this set (made when missing) instead of the file's own set. |
| `space` | import | Records without a page of their own go here: `cpu:main` (default), `rom0`, `ram3`, `cache0`, `const`, `port`; another CPU's: `gs.rom0`. |
| `base` | import | Added to every offset (decimal, `0x`, `#`, `$`, `h`). |
| `policy` | import | How a merge treats a record that collides with the set: `both` (a second name for a place becomes an alias; a name that moves keeps its old place; default), `keep`, `replace`, `fail` (nothing changes, HTTP 409 with the conflicts). |
| `sets` | export | Only these set ids. Default: the labels as they show. The native format writes the sets themselves (every set by default). |
| `pages` | export | A page symbol in a format without pages: `fold` (written at its CPU address, default), `comment`, `drop`. |
| `enabled`, `priority` | set | Show / hide a set's labels; move it up or down. |

Without `set`, `space`, `base` and `policy` an import is the same as `symbols load`: the file is one set, and inside
it a later name or address wins. With any of them, the records are normalized first: the space rule (DT-1), the base,
range and name checks, and duplicates inside the file (the first wins). Then they are merged by the policy. Rules:
`docs/inprogress/2026-10-05-unreal-asm/symbols/architecture.md` sections 6-7.

## Pitfalls

- Export renames names the target tool cannot take (`ERROR-1` becomes `ERROR_1` for sjasmplus). Each rename is a
  diagnostic, and it is a comment line in the file when the format has comments.
- `labels/resolve` and `/disasm` see the resolved labels only. A label in a switched-off set, or one shadowed by a
  higher set, does not show. `sets` gives each set's symbol count.
- Where labels of several pages share a CPU address (ROM 0 and ROM 1 at `#0000`), the address shows the label of the
  page mapped at that window now. A CPU-view label placed later wins over every page. With no label of the mapped
  page, the last placed label shows, as at every other address.

## Label tables in RAM (live scan)

ALASM (3.8c, 4.4x, 4.5, 5.0x) and XAS (4.x, 5.05, 7.x, 9.x) keep their label table in RAM after assembling. `scan`
copies the machine's RAM pages at a coherent moment and lists the tables it finds, best first. `import-live` reads one
of them into a set: the best one by default, or the one that `scanner` / `page` / `offset` name. The set is
`live:<scanner>@ram<page>:#<offset>`, origin `live`. Macro names and labels used but never defined are left out, and
each kind left out is reported.

```text
manage_symbols {"action":"scan"}
manage_symbols {"action":"import_live"}
manage_symbols {"action":"import_live","scanner":"xas-table","page":6,"set":"xas","policy":"replace"}
```

```text
GET  /api/v1/emulator/{id}/symbols/scan          -> {pages, candidates: [{scanner, version, page, offset, end, count, score}]}
POST /api/v1/emulator/{id}/symbols/import/live   {scanner?, page?, offset?, set?, policy?}
symbols scan | symbols import-live [--scanner s] [--page n] [--offset n] [--set s] [--policy p]
symbols_scan() / symbols_import_live{...}        emu.symbols_scan() / emu.symbols_import_live(...)
```

Where each assembler keeps its table: `docs/inprogress/2026-10-05-unreal-asm/research-labeltables.md`.

## Bundles

The emulator ships label files for known ROMs (`data/symbols/`, next to `rom/` in every build and package). The list
is in `data/symbols/manifest.json`: each bundle has the SHA-256 of the 16 KB ROM page it belongs to. At the start of
each instance, and after a ROM reload, the bundles whose page is loaded become sets `bundle:<id>` (origin `bundle`,
priority 50, below every loaded file). Bundles that no longer match are dropped. Today's bundles:

- the 48K ROM (`rom:48k`), and the 48 BASIC in the 128K's ROM 1 and in Pentagon's "48 for 128" (`rom:48k-in-128`,
  without the 48K's `spare` label);
- the 128K editor ROM 0 (`rom:128k-rom0`), and Pentagon's version with TR-DOS in the menu
  (`rom:128k-rom0-trdos-menu`, without the four labels in its changed #3BEC-#3C1F);
- the 48K and 128K system variables;
- Sprinter BIOS 3.04 ROM pages 0, 8 and #C.

A bundle set switched off stays off while the instance runs. `UNREAL_SYMBOL_BUNDLES=0` turns bundles off. To add a
bundle, put the file in `data/symbols/` and add an entry with the page's SHA-256 (`shasum -a 256` of the 16 KB page).
Use `"space": "rom"` when the file's addresses are page offsets of whatever page matched.
