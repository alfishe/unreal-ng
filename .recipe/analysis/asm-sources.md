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
