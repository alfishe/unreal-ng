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
4.5, 5.00-5.09), TASM 4.12 and XAS (4.18, 5.05, 7.43c, 7.447, 9.07m, 9.10) are recognized (phase Y0 of `docs/inprogress/2026-10-05-unreal-asm/asm-synchronizer.md`). The RAM is
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
asm_source {"action":"sync_hints"}                                    # the last build: labels, hints with source lines
asm_source {"action":"sync_unwatch"}
```

The labels of each build become the symbol set `live:sync:<assembler>` (priority 900000: above files, below `user`),
so the disassembly shows them at once. The set stays after `sync_unwatch`; `manage_symbols drop` removes it. WebAPI:
`POST` / `DELETE /emulator/{id}/asm/sync/watch`, `GET /asm/sync/hints`; WebSocket topic `asm_sync`
(`asm_sync_found`, `asm_sync_changed`, `asm_sync_built`, `asm_sync_lost`). CLI: `asm sync watch`, `asm sync hints`,
`asm sync unwatch`. Lua / Python: `asm_sync_watch{...}`, `asm_sync_hints()`, `asm_sync_unwatch()`.

In Qt: the debugger's **Live source** button opens a window with the text of the last build (the guest's cursor line
highlighted, errors and warnings on their lines and in a list), the status, and Watch / Extract... / Convert....

`state: none` (404) means that no known assembler is in RAM. `ambiguous` (400) means that two identify alike: give
`assembler`. `inconsistent` means that the pointers did not add up at that moment (the guest was mid-update): ask again.

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
