# Recipe: ZX-ASM / ZAsm in unreal-ng

Run ZX-ASM (Hohlov) / ZX Turbo Assembler / ZAsm (Afendikov, Rubtsov) in the emulator: start it, load a source,
assemble, run, get the code, and move sources between ZAsm and the host. Every step was run on unreal-ng
(2026-10-07, `PENTAGON` model) with ZAsm 3.15; 2.4, 2.6, 3.0 and 3.10 were started. The ZX-ASM language is in
[research-zxasm-to-sjasmplus.md](../../docs/inprogress/2026-10-05-unreal-asm/research-zxasm-to-sjasmplus.md), the
file format in [research-zxasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-zxasm.md); ZAsm's own
manuals (`ide.txt`, `edit.txt`, `compile.txt`) are on its disks (UTF-8 copies in the collection:
`software/programming/zxasm/docs/zasm-own-docs/`). Shared steps: [README.md](README.md).

## Versions and where they are

| Version | Image (collection: `software/programming/zxasm/unpacked/`) | Loader | Status here |
|---|---|---|---|
| 3.15 (Rubts0FF, 2017) | `zxart-249318/ZASM315.trd` ([zxart](https://zxart.ee/releasefile/id:249318/ZASM315.zip)) | `boot` | load, assemble, launch, code from RAM page 1: all run |
| 3.10 (1998) | `zxart-249317/ZASM_310.TRD` ([zxart](https://zxart.ee/releasefile/id:249317/ZASM_310.ZIP)) | `ZASM3.10` | starts: the File / Util / Edit / Compile / Run / Options menu |
| 3.0 (1996) | `zxpk-65803-ZAsm3.0/ZAsm3.0.trd` ([zx-pk.ru](https://zx-pk.ru/attachment.php?attachmentid=65803)) | `ZX-TASM3` | starts: File / Edit / Compile / Run / Print / Setup |
| 2.6 (Rubts0FF, 2018) | `zxart-249316/ZASM2_6.SCL` ([zxart](https://zxart.ee/releasefile/id:249316/ZASM2_6.zip)) | `boot` | starts: File / Edit / Compile / Run / Setup, a "No Named.C" text |
| 2.4 (Hohlov, 1994; 48K) | `zxart-421759/ZXASM2_4.SCL` ([zxart](https://zxart.ee/releasefile/id:421759/ZXASM2_4.zip)) | `ZXASM2.4` | starts: the same menu |
| 4.20 (2026) | `vtrd-ZASM4_20/Z4_20.trd` ([vtrd](https://vtrd.in/system/ZASM4_20.zip)) | `boot` | needs **`PENTAGON` 512K** (hangs on its picture on 128K): load, assemble (`ENDA` probe) |
| 3.3 Final (2021) | `vtrd-Z33_F9/Z33_F9/Z33_F9.trd` ([vtrd](https://vtrd.in/system/Z33_F9.zip)) | `boot` | needs `PENTAGON` 512K: load, assemble (`ENDA` probe) |
| 3.2x (2017) | `zxart-249319/Z32X.trd` ([zxart](https://zxart.ee/releasefile/id:249319/Z32X.zip)) | `boot` | needs `PENTAGON` 512K: load, assemble, `ENDA`, `LOADTAB` / `~text~` checked |

Sources: 2.x saves plain text (type `C`), 3.0 type `C` start 35151, 3.10 and later type `a` with the extension `sm`
(`NAME.asm` in ZAsm's own naming). `zxasm encode --codec zxasm --version 3.15` (3.15-4.20; `2` for 2.4-2.6, `3.0` for 3.0-3.10, `lite`
for Lite 1.07) writes them.

ZAsm 3.10+ occupies `#5D3B-#FFFF` while it runs and keeps the user's `#8000-#BFFF` in **RAM page 1** (and
`#5D3B-#7FFF` in page 4 from `#DD3B`): read compiled code there, or after Quit. Return from BASIC with `RANDOMIZE USR
23600`.

## MCP (preferred)

```text
media             {"action":"insert","slot":"A","path":"/abs/scratch/za315.trd","discard":true}
emulator_manage   {"action":"reset"}
type_input        {"action":"tap","key":"down"}                 # x4
type_input        {"action":"tap","key":"enter"}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"boot\""}}
```

ZAsm looks for drive D first: "Error! No Disk! Retry / caNcel". ENTER opens the drive list, `a` picks A; the main
menu `File Util Edit Compile Run Options` follows (about 6 s):

```text
type_input {"action":"tap","key":"enter"}
type_input {"action":"tap","key":"a"}
```

Menus: the highlighted capital is the key, ENTER takes the item under the cursor. File: Save, Load, Merge, New,
Save_OBJ, Load_code, Save_code, seRvice, Drive, Call_Tr_Dos, Quit. Compile: Assemble, Statistics, View Labels, Edit
Errors, Work file. Run: Run, CALL, caLculator, Stack, Monitor, Debugger. Options: Store_Overlay, SetUp, Save_setup,
Restore_setup, Load, Colours, change Buff's, About. Util: Print, Convert (other assemblers' texts), Editor menu,
Disk Doctor, Font Editor, Text Format, Games, User.

Load a source (File, Load: the file panel lists the disk, the name is typed below "Enter name:"; ZAsm keeps the case
you type):

```text
type_input {"action":"tap","key":"enter"}                     # File
type_input {"action":"tap","key":"enter"}                     # Load
type_input {"action":"type","text":"zatest","delay_frames":6}
type_input {"action":"tap","key":"enter"}                     # the editor: "A:zatest .asm ASM L:1"
```

Assemble from the editor: Extend opens `COMMAND:`, `a` assembles; ZAsm asks for drive D again (ENTER, `a`), then
"Compile Complete — Launch ?":

```text
type_input {"action":"combo","keys":["cs","ss"],"frames":4}
type_input {"action":"tap","key":"a"}
type_input {"action":"tap","key":"enter"}                     # "No Disk!": Retry
type_input {"action":"tap","key":"a"}                         # drive A
capture_media {"action":"screenshot","filename":"scratch/zasm-compiled.png"}
type_input {"action":"tap","key":"y"}                         # Launch: runs from ENT or the first ORG, RET returns
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/1?offset=0&length=16384"}   # body.data = #8000-#BFFF
```

`n` instead of `y` returns to the editor. A source that ends with `saveobj "a:out.C",<address>,<length>` writes the
code to the disk while assembling (the oracle script reads it from there).

File panel keys (manual): CS+7 into the file list, CS+6 / CS+7 cursor, CS+5 / CS+8 page, CS+1 change drive, CS+ENTER
copy the name into the input line, SPACE mark, SS+8 delete, SS+6 rename, SS+R reread, SS+I disk info, SS+L filter,
CS+SPACE cancel. Input lines: CS+1 Rus / Lat, CS+2 caps, SS+W insert / overwrite, SS+ENTER clear, CS+6 history.

## WebAPI

`docs/inprogress/2026-10-05-unreal-asm/scripts/emulator/assemble-in-emulator.py zasm315 <ZASM315.trd> <source.$a> <address> <length> <out.bin> [--extra
FILE.$T ...]` runs the whole sequence (the source must end with `saveobj "a:out.C",...`; the script reads `out.C`
from the disk). The calls are those of [tasm.md](tasm.md#webapi) with `RUN "boot"` and the keys above; RAM page 1:

```bash
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/1?offset=0&length=16384" | jq '.data[0:8]'   # without offset / length: 128 bytes
```

## Pitfalls

- **Drive D first**: every disk operation of a fresh ZAsm asks for D; answer A each time (or set the default drive
  in SetUp).
- **`#8000` is ZAsm's own code** while it runs: the user's bytes are in RAM page 1.
- **More than 128K for 3.2x and later** (3.2x ReadMe: ZAsm takes the last 128K of the memory): on a 128K machine they
  hang on their picture (red border). Create a `PENTAGON` with 512K (`emulator_manage create` with `ram_size: 512`,
  `assemble-in-emulator.py zasm315 --ram 512`).
- **A second instance starts paused** (WebAPI `POST /emulator/start`): `resume` it before typing, or the keys wait.
