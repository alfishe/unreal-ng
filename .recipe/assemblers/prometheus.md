# Recipe: PROMETHEUS (Proxima) in unreal-ng

Run PROMETHEUS 48K (Proxima, 1990: assembler, editor and monitor), load a source save from tape, assemble it, read
the bytes, and move sources between PROMETHEUS and the host (`zxasm` reads and writes its saves). Every step was run
on 2026-10-08 against the 48K tape edition.

Related: [README.md](README.md) (own instance, `$PORT`), the format and conversion
`docs/inprogress/2026-10-05-unreal-asm/research-prometheus.md`.

## Where PROMETHEUS is

- [oldcompcz/prometheus](https://github.com/oldcompcz/prometheus): `tap/prometheus-48.tap` (the 48K tape edition),
  `trd/` (TR-DOS adaptations), `d80/source-d80.tap` (55 saves of Proxima's routine library), the reconstructed
  source, the book "The Liver of PROMETHEUS" and the decoder `prometheus-tap2asm`.
- zxart [prod 164989](https://zxart.ee/prod/164989): the 48K / 128K tapes, TRD, SCL, D40 / D80 images.

## Start it

On a `48K` model, with tape fast loading on (the default; see Pitfalls for real-time loading):

```text
POST /api/v1/emulator/start {"model":"48K"}                 -> id (resume it: a second instance starts paused)
POST /api/v1/emulator/{id}/tape/load {"path":"<abs>/prometheus-48.tap"}
POST /api/v1/emulator/{id}/basic/run {"command":"LOAD \"\""}
POST /api/v1/emulator/{id}/tape/play                        -> wait for tape state "ended"
```

The installer shows `Monitor:Yes  Instalation address:24000_` and `Press ENTER to run Assembler`: ENTER. PROMETHEUS
then occupies about 24000-42400; the editor's status line `I 39992 65535` gives INSERT / OVERWRITE, the end of the
source and table, and U-TOP. Put assembled code above the end (the checks used 60000).

## The text in memory

From the reconstructed source (SAVE in `src/prometheus.asm`), for the 48K tape edition at 24000: the records from
`sourceBufferAccessLine` (`#9C28`) to twelve bytes below the symbol table, whose address is the operand at `#87D7`;
the table runs to the operand at `#881A`. SAVE writes the records, `FF` XOR the records, `FF`, then the table: the
format `zxasm --codec prometheus` reads. `LOAD` stages a save just below U-TOP before it inserts the lines: that copy
is not the source. The asm-synchronizer reads it live (descriptor `prometheus-48`).

SAVE goes to tape only (the TR-DOS adaptation's too). To get the bytes a SAVE writes without a tape: debug mode, the
`breakpoints` feature, an execution breakpoint at `#04C6` (ROM SA-BYTES); the second stop (after the 17-byte header)
has the records' start in `IX` and length in `DE` (`tools/verification/unreal-asm/emulator/sync-dumps.py prometheus`).

## Commands

A command is SYMBOL SHIFT + its letter at the start of an empty input line (the word appears), then its parameter
and ENTER: `SS+A` ASSEMBLY, `SS+L` LOAD, `SS+S` SAVE, `SS+V` VERIFY, `SS+R` RUN (needs exactly one `ENT`), `SS+M`
MONITOR, `SS+T` TABLE, `SS+X` CLEAR, `SS+B` BASIC; `SS+W` INSERT / OVERWRITE, `CS+1` edit the active line.
WebAPI: `POST /keyboard/combo {"keys":["ss","l"],"frames":4}`, then `/keyboard/type`, `/keyboard/tap enter`.

## Load a source save

```text
POST /tape/load {"path":"<abs>/probe.tap"}
POST /keyboard/combo {"keys":["ss","l"],"frames":4}
POST /keyboard/type {"text":":probe"}                        <- the name is required (Pitfalls)
POST /keyboard/tap {"key":"enter"}
POST /tape/play                                              -> "Found:probe", then the lines appear
```

LOAD stages the whole save below U-TOP and inserts each line through the editor (after the active line; `CLEAR`
first to replace the source).

## Assemble and read the code

```text
POST /memory/write {"address":60000,"data":[170,...]}        <- #AA first: DEFS holes stay visible
POST /keyboard/combo {"keys":["ss","a"],"frames":4}
POST /keyboard/tap {"key":"enter"}
GET  /memory/read/60000?length=48
```

## Sources to and from the host

```bash
zxasm files source-d80.tap                         # PROMETHEUS saves show as codec "prometheus"
zxasm decode source-d80.tap --file +fill1 -o fill1.txt
zxasm convert source-d80.tap --to sjasmplus -o out/
zxasm encode probe.txt --codec prometheus -o probe.bin
```

`zxasm encode` writes the save's bytes (records, the checksum pair, the table). A tape file needs a CODE header
whose Param2 (bytes 15-16) is the records' length: the bytes before the pair whose first byte is `#FF` XOR the
records and the second `#FF` (the test `AFileFromText` builds `testdata/dialects/prometheus/PROBE.tap` that way).

## A host source into PROMETHEUS

Write the source in its editor while it waits for a key; SS+A then assembles it (PROMETHEUS keeps no "changed" flag:
`force`):

```text
asm_source  {"action":"sync_inject","path":"/abs/main.asm","assembler":"prometheus-48","force":true}
WebAPI      POST /asm/sync/inject {"assembler":"prometheus-48","from":"sjasmplus","text":"...","force":true}
            POST /keyboard/combo {"keys":["ss","a"],"frames":4}, then /keyboard/tap enter
check       tools/verification/unreal-asm/emulator/inject-oracle.py prometheus prometheus-48.tap src.asm 60000
```

The records go from `#9C28`, its symbol table after them; the active line and the block go to the first line.
`zxasm convert --to prometheus -o x.bin` gives the same save as a file. The source and its names lie from `#9C28` up,
and give code addresses above them (60000).

## Other builds

- **TR-DOS** (`trdos-prometheus48.trd`): `RUN "TRD-PROM"`, ENTER takes the address 25000; the same code as the 48K
  edition moved up by `#514` (descriptor `prometheus-48-trdos`; `inject-oracle.py prometheustrd`).
- **PROMETHEUS 128** (tape, Proxima 1993): `LOAD "PROM128LNG"` (or `PROM128MDM`, `PROM128SHT`), ENTER takes 24000, L
  and M then ask "Install D40/D80 version?" (n; S asks nothing). The text lives in its own 64K space over RAM pages
  1, 3, 4, 6, from `#C01A` in page 6 (descriptors `prometheus-128`, `-128m`, `-128s`; `inject-oracle.py prometheus128
  --boot PROM128MDM --assembler prometheus-128m`). Idle templates of all four in `testdata/templates`.
- Another installation address moves the whole code; the probe finds it there (descriptor `<id>@<address>`, e.g.
  `prometheus-48@30000`). In the installer's field DELETE needs CAPS SHIFT pressed before 0 (a tap of DELETE, both in
  one frame, alternates a delete with a typed 0) and a digit typed twice needs a release between: `inject-oracle.py
  prometheus --install 30000 --assembler prometheus-48@30000` (make-template.py too).
- PROMETHEUS 128 keeps a text within page 6 (16K, from `#C01A`): a longer one loads as "Memory full".

## Pitfalls

- **`$` in an instruction is the address of the byte its value goes into**, not the instruction's: `jr $` builds
  `18 FF`, `ld hl,$` the address +1, `ld ix,$` +2, the `n` of `ld (ix+d),n` +3. The conversion writes `jr $-1` for
  sjasmplus' `jr $`, and reads PROMETHEUS' `$` so.
- **A save of 2 KB or more fails with "Tape error" while `fasttape` / `turbotape` are on** (emulator bug, 2026-10-12;
  the ROM's own LOAD reads it). Turn both off (`PUT /feature/fasttape {"enabled":false}`, the same for `turbotape`)
  before loading PROMETHEUS; the load then runs in real time.
- **Names: 8 characters, starting with a letter**; a ninth is a syntax error. The conversion shortens and renames.

- **`LOAD` without a name loads only a file with the last name** (`prometheus` after start): it skips every other
  header and waits on the tape (ROM LD-EDGE at `#05ED`). Use `LOAD :name`, or a name starting with a blank (the
  wildcard). VERIFY compares exactly the ranges of the SAVE just before it.
- **A save is checked by its two middle bytes**: `#FF` XOR the records, then `#FF` (SAVE's chained two-part block).
  A hand-made save with other bytes never finishes loading.
- **Code over PROMETHEUS**: the default install at 24000 reaches about 42400 with the monitor; an `ORG 40000` lands in
  it.
- **No comment after an instruction**: comments are whole lines starting with `;`.
- **`$` in `DEFB` / `DEFW` is the address of each item**; **`DEFS` writes nothing** (a hole); **`PUT`** moves only where
  the bytes go; without `ORG` the code goes after the source and table.
- **Real-time tape loading** (with `fast_tape` / `turbo_tape` off) works since the fix to the tape engine (2026-10-08):
  before it, WebAPI play after `LOAD` played the header twice and the load stopped with "R Tape loading error".
  PROMETHEUS' LOAD calls ROM LD-BYTES at `#0562`; the fast loader serves it as well.
