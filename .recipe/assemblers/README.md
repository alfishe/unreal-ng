# Recipes: ZX assemblers in unreal-ng

How to run the Spectrum's own assemblers in the emulator and move their sources, code and labels between the
emulated machine and the host. One recipe per assembler, each version it covers named with what was run on unreal-ng
(2026-10-07); this page holds what they share. The assemblers' languages, the source formats and the conversion to
sjasmplus are in the unreal-asm design
([docs/inprogress/2026-10-05-unreal-asm/](../../docs/inprogress/2026-10-05-unreal-asm/README.md)).

| Assembler | Recipe | Versions run here |
|---|---|---|
| TASM (Rst7, XL Design, KVA) | [tasm.md](tasm.md) | 2.0, 3.0, 3.2, 3.5, 4.0, 4.4, 4.12, 4.12 for Pentagon 512 |
| ALASM (Alem, Alone Coder) | [alasm.md](alasm.md) | 3.8c, 4.42, 4.44, 4.5, 5.07, 5.08, 5.09 |
| STORM (X-Trade) | [storm.md](storm.md) | 1.0beta, 1.3, 1.3i |
| ZX-ASM / ZAsm (Hohlov, Afendikov, Rubtsov) | [zxasm.md](zxasm.md) | 2.4, 2.6, 3.0, 3.10, 3.15 |
| GENS (HiSoft Devpac) | [gens.md](gens.md) | GENS4 and GENS3 (tape), GENS4B (TR-DOS, editor only) |
| MASM (AIG, KSA) | [masm.md](masm.md) | 1.1, 3.0 |
| ZEUS (Brattel, Mottershead; Russian disk builds) | [zeus.md](zeus.md) | 1983 (tape), 1.1 beta, GG, v7.E |
| XAS (Maxim Petrov) | [xas.md](xas.md) | 4.18, 7.447 |

Pick the recipe of your assembler; read this page for the parts every recipe points back to.

> **How to use the sections:** MCP is preferred (`type_input`, `media`, `inspect_state`, `capture_media`,
> `manage_symbols`, `invoke_api` for the rest). Use WebAPI inside host-side Python tools
> (`tools/unreal-asm/emulator.py` wraps it) or when MCP is unavailable
> ([_common/transports.md](../_common/transports.md)).

## Your own instance

Start the emulator on ports of its own; other sessions use the defaults. The variables are listed in
[docs/emulator/environment-variables.md](../../docs/emulator/environment-variables.md):

```bash
UNREAL_WEBAPI_PORT=8901 UNREAL_MCP_PORT=8902 UNREAL_CLI_PORT=8903 UNREAL_GDB_PORT=8904 UNREAL_DEZOG_PORT=8905 \
UNREAL_ZRCP_PORT=8906 <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
```

Stop it by its own PID when done. The emulator's working directory is where you started it: a relative path given to
it (a screenshot's `filename`) lands there, so give `scratch/...`.

## Starting an assembler from a TR-DOS disk

Most Spectrum assemblers are a BASIC loader on a TR-DOS disk. `PENTAGON` model (128K, TR-DOS ROM): insert the disk,
choose TR-DOS in the Pentagon menu, `RUN` the loader. Entering TR-DOS by hand is
[run/manual-trdos-run.md](../run/manual-trdos-run.md); the short form:

```text
media             {"action":"insert","slot":"A","path":"/abs/scratch/asm.trd","discard":true}
emulator_manage   {"action":"reset"}
type_input        {"action":"tap","key":"down"}                       # 4 times: the TR-DOS line of the menu
type_input        {"action":"tap","key":"enter"}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"TASM4.12\""}}
capture_media     {"action":"screenshot","filename":"scratch/asm-started.png"}
```

`basic/run` types the line through the ROM editor and presses ENTER (`structuredContent.body.basic_mode` is `trdos`).
Give the loader its time (5-15 s) before the next key. SCL images are accepted as they are; `tools/unreal-asm/zxdisk.py
scl2trd` makes a TRD when a TRD is needed (adding files with `zxdisk.py add`).

## Keys

- **Inverted case.** TASM and ALASM start with the keyboard inverted: `type_input type "sinus"` arrives as `SINUS`.
  To type a lower-case file name type capitals (`000LOAD` for the file `000load`). STORM starts in BIG mode
  (capitals). ZX-ASM keeps what you type.
- **Extend (CS+SS).** The editors' command keys come after Caps Shift + Symbol Shift together. Some editors read the
  pair only when it is held long enough: TASM needs `type_input combo ["cs","ss"]` with `frames: 12` (the default 2
  and even 4 type a letter instead); ALASM was driven with 12 too, ZAsm with 4. STORM's commands come after BREAK.
- **BREAK** is `type_input tap "break"` (Caps Shift + Space): STORM's external commands, most programs' cancel.
- **`type` and ENTER.** `type_input type` sends the characters only: `\n` is not ENTER. Type a line, then `tap
  "enter"`. After a key that changes the editor's mode (deleting a line, inserting one) give it half a second before
  typing on, or the first characters land elsewhere.

## Sources between the host and the machine

Four ways, from the most to the least convenient:

1. **A host folder as a disk.** `media insert` with a folder path makes a TR-DOS disk of its files: a hobeta file
   (`NAME.$A`) becomes that catalog entry, any other file a `C` file of its bytes (start 32768). The folder is never
   written; guest writes stay in the session, `media export` keeps them
   ([media/use-media-slots.md](../media/use-media-slots.md)):

   ```text
   media {"action":"insert","slot":"B","path":"/abs/scratch/fold","discard":true}
   ```

2. **A disk image built on the host.** `zxasm encode` writes a source in the assembler's own format (hobeta when the
   output name ends in `.$X`), `zxdisk.py add` appends it to a copy of the assembler's disk:

   ```bash
   zxasm encode prog.txt --codec tasm --version 4.12 -o 'PROG.$A'        # alasm --version 5.07, storm --version 1.3 ...
   python3 tools/unreal-asm/zxdisk.py add TASM_412.trd work.trd 'PROG.$A'
   ```

   An assembler already running sees a disk swapped under it (`media swap` with `immediate: true`) after its next
   catalog read.

3. **The assembler's own text import / export.** TASM 4.12 (Import/export, `p`), ALASM (`impOrt`, `eXport`), STORM
   (`imporT`, `eXport`) and ZAsm 3.10+ (the Convert overlay) read and write plain text on the disk; read the file
   back with `disk/{drive}` sector reads (`tools/unreal-asm/emulator.py read_disk_file`).

4. **The disk back to the host.** `media export` writes the disk with everything the assembler saved:

   ```text
   media {"action":"export","slot":"A","path":"/abs/scratch/after.trd"}
   ```

   Then `zxasm files after.trd`, `zxasm decode after.trd --file NAME.A -o name.txt` (UTF-8 text),
   `zxasm convert after.trd --to sjasmplus -o out/` (every source of the disk, sjasmplus syntax).

## Code out of the machine

- The CPU's view: `inspect_state {"aspects":["memory"],"address":24576,"length":64}` (`structuredContent.memory.hexdump`).
- A RAM page as it is (an assembler that keeps the user's memory elsewhere while it runs, ZAsm):
  `invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/1?offset=0&length=16384"}` (`body.data`;
  without `offset` / `length` the reply holds only the first 128 bytes).
- The object file the assembler saved: export the disk (above) or read its sectors.
- `tools/unreal-asm/assemble-in-emulator.py <profile>` runs a whole assemble and saves the bytes (profiles for TASM
  4.12, ALASM 5.09, STORM 1.3, ZAsm 3.15, MASM 1.1, XAS, ZEUS, GENS4).

## Labels into the debugger

`symconv source` lays out a source (or the project of a disk) the way the assembler would and writes its labels in
any symbol format; the emulator's debugger loads them:

```bash
symconv source 'PROG.$H' --to unreal-map -o scratch/prog.map                    # one source
symconv source work.trd --main MAIN --to unreal-map -o scratch/main.map          # a disk: --main picks the source
```

```text
manage_symbols {"action":"load_labels","path":"/abs/scratch/prog.map"}
manage_symbols {"action":"resolve","name":"START"}          # structuredContent.label.address
```

The labels keep the source's names and lines (`--to native` shows them). Values come from the sjasmplus conversion
of the source ([symbols/formats.md](../../docs/inprogress/2026-10-05-unreal-asm/symbols/formats.md) §4.1); a source
that uses a construct the conversion does not take is reported there.

## Pitfalls

- **The working directory.** A screenshot `filename` without `scratch/` lands in the directory the emulator was
  started from (the repository root): give `scratch/...`. An absolute `path` to `capture_media` was saved under a
  percent-encoded name (seen 2026-10-07).
- **Memory the assembler uses.** Each recipe names the addresses its assembler keeps for itself; code assembled
  there is not where you read it (TASM 4.12's overlay at `#8000`, ALASM 5.09's system page for `#8000-#BFFF`, ZAsm
  keeping `#8000-#BFFF` in RAM page 1).
- **A reset does not clear RAM** on the Pentagon: bytes from a previous run stay. Fill the range first
  (`memory/write`) when you check what an assembler wrote.
- **Names that differ only in case** (`lorenz1k.a`, `LORENZ1K.H`) are two files to TR-DOS but one on a
  case-insensitive host: `zxasm convert` writes one over the other.
