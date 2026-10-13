# Recipe: Assembler Sources (decode, encode, convert, the files of a disk)

Goal: read the sources of ZX Spectrum assemblers from the emulator, a host file or an upload. That covers the tokenized
files of ALASM, TASM, ZX-ASM, STORM, MASM, GENS, ZEUS, XAS, Laser Genius, Prometheus and others, and the text dialects.
Turn them into text, write text back in a format (onto the disk in a drive as well), and convert a source to another
dialect: sjasmplus, pasmo or z88dk.

The library is unreal-asm (`core/src/3rdparty/unreal-asm`). Its formats and versions:
`docs/inprogress/2026-10-05-unreal-asm/`, and the per-assembler recipes in `.recipe/assemblers/`.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines,
> [CLI](#cli) on the telnet console, [Lua / Python](#lua--python) in scripts. In the Qt debugger, the **Disk files**
> toolbar button lists the disk's files with their formats and has "Open as Source", "Export as Text..." and
> "Convert to...". Every surface goes through one layer (`AsmControl`, `core/src/debugger/asm/asmcontrol.h`), so the
> options, refusals and reply fields are the same everywhere.

## Sources and outputs

| Option | Meaning |
|--------|---------|
| `path` | A host file, or `disk:A/NAME.T`: the last live TR-DOS catalog entry NAME of type T on the disk in drive A (A-D). |
| `file` | `NAME.T` inside an image given as `path` (`.trd`, `.tap`, `.tzx`). A hobeta `$X` file or a +3DOS file is unwrapped by itself. |
| `data`, `name` | The file itself as base64 instead of `path`, for a client on another host. `name` gives its extension. |
| `output` | A host file or `disk:A/NAME.T`. Without it the reply carries the text (decode, convert) or the bytes as base64 (encode). |
| `start` | encode to a disk: the catalog start field. The default is what the format needs to be listed: TASM's, ZX-ASM's, XAS's "AS" and so on. |

Writing to a disk adds a catalog entry the way TR-DOS does. An older live file of the same name and type is deleted
first. The disk image counts as modified, and the media panel offers to save it. A write-protected or full disk is
refused (409).

## MCP (preferred)

```text
asm_source {"action":"files"}                                          # drive A: name, type, start, length, format, path
asm_source {"action":"detect","path":"disk:A/GSTUNNE4.H"}
asm_source {"action":"decode","path":"disk:A/GSTUNNE4.H"}               # text, format, version, versions, diagnostics
asm_source {"action":"convert","path":"disk:A/GSTUNNE4.H","to":"sjasmplus","output":"scratch/gstunne4.asm"}
asm_source {"action":"encode","text":"START LD A,1\n RET\n","codec":"alasm","output":"disk:A/PROBE.H"}
asm_source {"action":"decode","path":"scratch/TheLink.trd","file":"GSTUNNE4.H"}
asm_source {"action":"formats"}                                         # codecs and versions
asm_source {"action":"dialects"}                                        # what convert reads and writes
```

## WebAPI

| Method | Path | Body / query |
|--------|------|--------------|
| GET | `/api/v1/asm/formats`, `/api/v1/asm/dialects` | (no instance) |
| GET | `/api/v1/emulator/{id}/asm/files` | `?drive=A` |
| POST | `/api/v1/emulator/{id}/asm/detect` | `{path \| data + name, file?}` |
| POST | `/api/v1/emulator/{id}/asm/decode` | `{path \| data + name, file?, codec?, version?, codepage?, output?}` |
| POST | `/api/v1/emulator/{id}/asm/encode` | `{text \| input, codec, version?, codepage?, lineend?, output?, start?}` |
| POST | `/api/v1/emulator/{id}/asm/convert` | `{path \| data + name, file?, to, codec?, version?, from?, z80n?, output?}` |

```bash
E=http://localhost:8090/api/v1/emulator/0
curl -s "$E/asm/files?drive=A"
curl -s -X POST $E/asm/decode -H 'Content-Type: application/json' -d '{"path":"disk:A/GSTUNNE4.H"}'
curl -s -X POST $E/asm/convert -H 'Content-Type: application/json' -d '{"path":"disk:A/GSTUNNE4.H","to":"sjasmplus"}'
```

Status codes: 400 bad option, 404 no such file / format / dialect, 409 the disk refuses the write, 422 the bytes are no
source of the format or the conversion failed.

## CLI

```text
asm files [A-D]
asm detect <path>
asm decode <path> [--codec c] [--version v] [--codepage cp] [--output file|disk:A/NAME.T]
asm encode <text-file> --codec c [--version v] [--output file|disk:A/NAME.T] [--start n]
asm convert <path> --to dialect [--codec c] [--from d] [--z80n] [--output file]
asm formats | asm dialects
```

`asm <address> <code>` still assembles Z80 text into memory, as `assemble` does. Add `--json` to a source verb for the
reply as JSON.

## Lua / Python

```lua
for _, f in ipairs(asm_files("A").files) do print(f.name, f.type, f.format) end
local r = asm_decode("disk:A/GSTUNNE4.H"); print(r.format, r.version, r.lines)
asm_convert{path = "disk:A/GSTUNNE4.H", to = "sjasmplus", output = "/abs/out.asm"}
asm_encode{text = "START LD A,1\n RET\n", codec = "alasm", output = "disk:A/PROBE.H"}
```

```python
emu.asm_files("A")
emu.asm_decode("disk:A/GSTUNNE4.H")["text"]
emu.asm_convert("disk:A/GSTUNNE4.H", to="sjasmplus", output="/abs/out.asm")
emu.asm_encode("START LD A,1\n RET\n", codec="alasm", output="disk:A/PROBE.H")
```

A refusal does not raise: the table / dict has `ok = false` and `error` = the message.

## The source an assembler holds in RAM (asm-synchronizer)

The source an assembler is editing in the machine can be read without saving it to a disk first. ALASM (3.8c, 4.42-4.46,
4.5, 5.00-5.09), TASM (2.0, 3.0, 3.2, 3.5, 4.0, 4.4, 4.12), XAS (4.18, 5.05, 7.43c, 7.447, 9.07m, 9.10) STORM (1.0beta, 1.3), ZX-ASM 2.4 / 2.5 / 2.6 / ZX ASM 3.0 / 3.10 / ZAsm 3.15 / 3.2x / Lite 1.07 / 3.3.02 / 3.3.51 / 3.3.Final / 3.4 / 3.80.4 / x64.1 / 4.0x8 / 4.x64 / 4.20 and MASM 1.0 demo / 1.1 / 1.3 / 2.0 / 3.0, GENS4 / GENS3M21 / GENS4B / GENS 4.3t, ZEUS 1983 / 1.1 / GG / v7.E, Primus 2.9, Laser Genius 1.04, PROMETHEUS 48K, Power Assembler 3.0 are recognized (phase Y0 of `docs/inprogress/2026-10-05-unreal-asm/asm-synchronizer.md`). The RAM is
copied at a coherent moment, and the guest is never written. The text comes out as the file the assembler's own SAVE
would write: a line being edited in TASM is in it. ALASM keeps a line out of the text until Enter; the status says
`typing` then.

```text
asm_source {"action":"sync_status"}                                   # assembler, text name, page, editor / typing / changed
asm_source {"action":"sync_probe"}                                    # every assembler that identifies, with a score
asm_source {"action":"sync_extract"}                                  # the text
asm_source {"action":"sync_extract","as":"dialect","to":"sjasmplus","output":"scratch/live.asm"}
asm_source {"action":"sync_extract","as":"file","output":"disk:A/COPY.H"}   # the assembler's own format, onto the disk
```

| Surface | Calls |
|---------|-------|
| WebAPI | `GET /emulator/{id}/asm/sync?assembler=`, `POST /asm/sync/probe`, `POST /asm/sync/extract {assembler?, as: text \| file \| dialect, to?, codepage?, output?}` |
| CLI | `asm sync [status]`, `asm sync probe`, `asm sync extract [--as text\|file\|dialect] [--to d] [--output f]` |

**Watch** (live labels and hints while the user types in the guest):

```text
asm_source {"action":"sync_watch"}                                    # interval 250 ms, quiet 500 ms
asm_source {"action":"sync_watch","as":"dialect","to":"sjasmplus","output":"scratch/live.asm"}   # the host file follows
asm_source {"action":"sync_watch","project":"disk:A,/abs/project/lib"}   # INCLUDEd sources: the disk, then a host folder
asm_source {"action":"sync_hints"}                                    # the last build: labels, hints with source lines
asm_source {"action":"sync_unwatch"}
```

A text that INCLUDEs others (`LTEXT`, `INCLUDE`, `*F` ...) is built as a project: the live text is the main file, the
sources it names come from the other texts ALASM holds in memory (one per page, by their names), the disk in drive A
(default; `project: "disk:B"`, `"none"`) and from host folders in
`project` (text sources by file name; other files give their sizes to INCBIN); a text in memory wins over the disk's
file, the disk's over the folder's, the live text over all. `sync_hints` counts the project's sources (`project`); a name found nowhere is a
hint and the build goes on.

The labels of each build become the symbol set `live:sync:<assembler>` (priority 900000: above files, below `user`),
so the disassembly shows them at once. The set stays after `sync_unwatch`; `manage_symbols drop` removes it. WebAPI:
`POST` / `DELETE /emulator/{id}/asm/sync/watch`, `GET /asm/sync/hints`; WebSocket topic `asm_sync`
(`asm_sync_found`, `asm_sync_changed`, `asm_sync_built`, `asm_sync_lost`). CLI: `asm sync watch`, `asm sync hints`,
`asm sync unwatch`. Lua / Python: `asm_sync_watch{...}`, `asm_sync_hints()`, `asm_sync_unwatch()`.

In Qt: the debugger's **Live source** button opens a window with the text of the last build (the guest's cursor line
highlighted, errors and warnings on their lines and in a list), the status, and Watch / Extract... / Convert....

`state: none` (404) means that no known assembler is in RAM. `ambiguous` (400) means that two identify alike: give
`assembler`. `inconsistent` means that the pointers did not add up at that moment (the guest was mid-update): ask again.

### Writing a host source into the assembler (part B)

`sync_inject` turns a host source into the assembler's own text in RAM, so the user can go on in the original tool.
ALASM (3.8c to 5.09; at its `A>` prompt), XAS (4.18 to 9.10; at its command line, EXT) STORM (1.3, 1.3i; while its editor waits for a key) TASM 4.12 (at `TASM128>`) ZAsm 3.15 (at its COMMAND prompt) MASM 1.1 (at `MASM128>`) GENS4 and ZEUS 1983 (at their prompts) for now:

```text
asm_source {"action":"sync_inject","path":"/abs/project/main.asm"}                 # sjasmplus, by default
asm_source {"action":"sync_inject","text":"        ORG #6000\nstart   ret\n","title":"DEMO"}
asm_source {"action":"sync_inject","path":"disk:A/OLD.$C","from":"storm","force":true,"backup":"/abs/scratch/prev.H"}
```

The source is converted by the ALASM backend: left-to-right expressions, and a later label in anything but `+` / `-`
written as its value. The result is encoded as ALASM 5.07 and written over the text in the page IX+#0D names, as a
TTD tool edit. `title` names the text (default: the name of the text it replaces). The reply gives the page, the
bytes, the lines, the replaced text (`previous`) and the conversion's diagnostics.

A `path` source brings the files it INCLUDEs (each relative to the file that INCLUDEs it). They are converted
together with it, and each goes into an ALASM text page of its own. Its INCLUDE is written `INCLUDE "NAME",page`, so
ALASM finds the text loaded and reads no disk. The reply lists them under `includes` (source, name, page, bytes). The
free pages: 4 and 0 in 5.05-5.09, 0 and 3 in 4.5, 4 in 5.00, 4.42-4.46 and 3.8c. A project with more files is refused:
put it onto the disk with `zxasm inject main.asm disk.trd --assembler alasm-5.09` (every file of the project).

The write is refused (409) unless ALASM waits at its `A>` prompt: in the editor, while it assembles, saves or shows
a catalogue. Leave the editor first (Extend, `q`, ENTER). A text with changes not saved is kept unless `force` is
given; `backup` writes it to a host file first. After the write, `a` + ENTER assembles the new text.

WebAPI: `POST /emulator/{id}/asm/sync/inject {text | path | data + name, from?, assembler?, title?, force?, backup?}`.
CLI: `asm sync inject --path f [--from d] [--title NAME] [--force]`. Lua / Python: `asm_sync_inject{...}`.
`tools/verification/unreal-asm/emulator/inject-oracle.py alasm509 <ALASM509.trd> <source.asm> 0x6000` runs the
whole check: it injects the source, lets ALASM assemble it and compares the bytes with sjasmplus' build. Sources for
ALASM 5.09 must ORG below #8000. The conversion's rules and limits are in
[research-sjasmplus-to-alasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-sjasmplus-to-alasm.md).

#### On every save (push watch)

```text
asm_source {"action":"sync_push_watch","path":"/abs/project/main.asm"}     # interval / quiet / title / force as options
asm_source {"action":"sync_status"}                                         # push: {state, pushes, files, last, error}
asm_source {"action":"sync_push_unwatch"}
```

The watch looks at the file and at every file it INCLUDEs (default every 500 ms). A change that stays 300 ms is
written as `sync_inject` writes it. The first write comes at once. States: `waiting` (no change), `pending` (the
assembler is busy: in its editor, assembling; retried until it waits for a command), `pushed`, `conflict` (the text in
the guest changed since the watch wrote it: left alone until the next save; `force` overwrites), `error` (the
conversion failed, too many INCLUDEd files, an assembler without a writer). WebSocket topic `asm_sync`: `pushed`,
`pending`, `conflict`, `push-error`. WebAPI `POST` / `DELETE /emulator/{id}/asm/sync/push-watch`, CLI
`asm sync push-watch <file>` / `asm sync push-unwatch`, Lua `asm_sync_push_watch{path=}`, Python
`asm_sync_push_watch(path)`; the Qt Live source window's Push button.

#### Into a snapshot or a disk (no running assembler)

With `snapshot` the sync verbs read a snapshot file (.szx, .sna, .z80) instead of the machine, and `sync_inject`
writes the snapshot with the text in it to `output`; the machine is not touched. An idle template is an assembler at
its prompt (`core/src/3rdparty/unreal-asm/testdata/templates/`: alasm-5.09, tasm-4.12, xas-7.447, storm-1.3, masm-1.1,
zasm-3.15, gens-4, zeus-1983, primus-2.9, lasergenius-1.04, prometheus-48, pasm-3.0 .szx; `make-template.py` makes
more):

```text
asm_source {"action":"sync_inject","path":"/abs/main.asm","snapshot":"/abs/alasm-5.09.szx","output":"/abs/scratch/work.szx"}
asm_source {"action":"sync_extract","snapshot":"/abs/scratch/work.szx","as":"dialect","to":"sjasmplus"}
```

The WebAPI does it with no emulator instance at all: `GET /api/v1/asm/sync?snapshot=/abs/work.szx`,
`POST /api/v1/asm/sync/probe`, `/extract`, `/inject` with `snapshot` in the body (the actions that need the machine,
and a `disk:A/...` path, answer 400 there).

The standalone `zxasm` does the same on files, and also writes the source as a file of the assembler onto a TR-DOS
image of any format (TRD, SCL, FDI, UDI, TD0 ...) or a +3DOS disk (DSK: NAME.TYP with a +3DOS header): `zxasm inject main.asm work.szx`, `zxasm inject main.asm disk.trd --assembler alasm-5.09`,
`zxasm probe work.szx`, `zxasm extract work.szx --to sjasmplus`, `zxasm extract disk.scl --file MAIN.H`.
`inject-oracle.py alasm509 --template <template> <source.asm> 0x6000` is the check: zxasm injects, the snapshot is
loaded and ALASM assembles it.

## Pitfalls

- `convert` handles one file. A project whose sources INCLUDE each other converts as a whole with
  `zxasm convert <image> --to sjasmplus -o dir` (the standalone tool). That pulls in the INCLUDE / INCBIN files and the
  macros of the other files.
- `decode` detects the version from the bytes. Several versions may read a file the same: `versions` lists all of them.
  Pass `version` to read as one of them.
- A file written to a disk lives in the emulator's copy of the image. Save the disk (media panel or `media save`) to
  keep it.
- Symbols from a source (labels with values) go into the debugger with `manage_symbols import_source` and
  `path: disk:A/NAME.T` (or the Disk files dialog's Import Labels). The values are computed from the text, from `ORG` on;
  see [symbols-import-export.md](symbols-import-export.md#labels-from-a-source). A label file on the disk imports with
  `manage_symbols import`. On the host: `symconv source` (the standalone tool).
