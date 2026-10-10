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
| Lite 1.07 | `zxart-249324/ZLITE107.TRD` | `boot` | needs `PENTAGON` 512K: load, edit, save |
| 3.3.02 (2019) | `zxpk-68036-Z33_02/Z33_02.trd` | `boot` | needs `PENTAGON` 512K: load, edit, save; asks for the drive once per file until the menu (below) |
| 3.3.51 | `zxart-421760/Z33_51.trd` | `boot` | needs `PENTAGON` 512K: load, edit, save |
| 3.80.4 | `zxart-310045/ZASM3_84.trd` | `boot` | needs `PENTAGON` 512K: load, edit, save |

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
menu `File Util Edit Compile Run Options` follows (about 6 s; the start of each version is in
[Start and the default drive](#start-and-the-default-drive)):

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

## Start and the default drive

The drives a ZAsm 3.15+ uses come from the **settings file on its disk**, not from its code: type `s`, start `#7465`
(`ZAsm3.15 s`, `z33.02 s`, `z33.51 s`, ...), byte +3 "System drive" (where ZAsm runs from) and +4 "Overlays drive",
ASCII `A`-`E` (3.15 keeps them at offset `#24`; layout in
[research-zxasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-zxasm.md#31-the-settings-file-type-s)). The
shipped 3.15, 3.3.02, 3.3.51 and x64 say **`DD`**: the author works in Shalaev's DOS emulator and kept the overlays on
another disk, a RAM disk once (his ReadMe, zx-pk.ru thread 29356 page 4: "I use Shalaev's emulator exclusively"). With
no drive D the program says "No Disk" and the drive picked in the dialog covers **the file being loaded only**. To
remove the question: Options / SetUp, "System drive" and "Overlays drive" A, Options / Save_setup (the setup overlay's
SaveSet rewrites the file); or patch bytes +3 / +4 of the `s` file to `A` (the image only, the original stays).

| Version | RAM | Keys from `RUN "boot"` to the main menu (`~N`: wait N s) |
|---|---|---|
| 3.0 | 128K | none (the menu at once; its drive is the boot drive) |
| 3.10 | 128K | none |
| 3.15 | 128K | `enter`, `a`, `~6` (one "No Disk") |
| 3.2x | 512K | `space`, `~3` (its picture waits for a key; no drive question) |
| Lite 1.07 | 512K | none (the menu at once) |
| 3.3.02 | 512K | `enter`, `a`, `~3`, `enter`, `a`, `~3`, `space`, `~3`: it loads every overlay as a file from the overlays drive, so it asks again for the next file (`FONT4.f` read from A, then drive D again: port trace 2026-10-09) |
| 3.3.51 | 512K | `enter`, `a`, `~6`, `space`, `~4` (one "No Disk": its overlays are one `OVERLAYS.t` catalogue) |
| 3.3.Final, 3.80.4, 4.20 | 512K | `space`, `~4` |

On a 128K machine the 512K versions stop on their picture with a red border (a `DI : HALT` after the memory test,
`BC` = `#7FFD`); 3.3.02's red border on 512K is its own picture. The "No Disk" here is the emulator answering
correctly for an empty drive D (NOT READY, `#FF` = `#3F`), not a disk fault.

The name input of 3.10 and later keeps the last name and 3.10 types capitals: the dump scripts load **the first file
of the list** (put the source first on the disk; File, Load, CS+7, CS+ENTER, ENTER).

## The text in memory

Every version keeps the text whole in one buffer (no gap): a start word and an end word, the part above `#C000` in
a fixed RAM page. Words just before the start word follow the cursor and the screen. SAVE writes the buffer, from
3.2x on with a first `;*top,line,...` line (3.15 `;!`) that loading takes out.

| Version | Start word / end word | Text from | Above `#C000` | Saving from the editor |
|---|---|---|---|---|
| 3.0 | `#61C6` / `#61C8` (page 5) | `#894F` (its files' start) | page 6 | COMMAND `q` to the menu, File / Save, ENTER (type `C`) |
| 3.10 | `#868F` / `#869E` | `#89C4` | page 6 | COMMAND (Extend) then SS+`2` |
| 3.15 | `#8829` / `#8837` | `#884C` | page 6 | COMMAND then SS+`2` (`;!` line) |
| 3.2x | `#8538` / `#853A` | `#8551` | page 30 | COMMAND then SS+`2` (`;*` line) |
| Lite 1.07 | `#859D` / `#859F` | `#85A7` | page 30 | the same |
| 3.3.02 | `#850A` / `#850C` | `#8522` | page 30 | the same |
| 3.3.51 | `#84FC` / `#84FE` | `#8514` | page 30 | the same |
| 3.3.Final | `#8401` / `#8403` | `#841A` | page 30 | the same |
| 3.80.4 | `#6D02` / `#6D04` (page 5) | `#8198` | page 30 | the same |
| 4.20 | `#68FB` / `#68FD` (page 5) | `#8201` | page 30 | the same |

COMMAND `2` asks for a name, `3` loads, `S` searches. The asm-synchronizer reads the buffer without a save
([asm-sources.md](../analysis/asm-sources.md#the-source-an-assembler-holds-in-ram-asm-synchronizer); descriptors
`zasm-3.0` ... `zasm-4.20`, design in asm-synchronizer.md §7.9). Only the text being edited is read: 3.2x and later
can hold several texts, switching between them is not covered yet.

## WebAPI

`tools/verification/unreal-asm/emulator/assemble-in-emulator.py zasm315 <ZASM315.trd> <source.$a> <address> <length> <out.bin> [--extra
FILE.$T ...]` runs the whole sequence (the source must end with `saveobj "a:out.C",...`; the script reads `out.C`
from the disk). The calls are those of [tasm.md](tasm.md#webapi) with `RUN "boot"` and the keys above; RAM page 1:

```bash
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/1?offset=0&length=16384" | jq '.data[0:8]'   # without offset / length: 128 bytes
```

## Pitfalls

- **Drive D first**: the shipped settings file says drive D (`DD`, see [Start and the default
  drive](#start-and-the-default-drive)); every disk operation of a fresh ZAsm asks for D, answer A each time or save
  the setup with drive A. 3.3.02 asks once per overlay file even at its start.
- **`#8000` is ZAsm's own code** while it runs: the user's bytes are in RAM page 1.
- **More than 128K for 3.2x and later** (3.2x ReadMe: ZAsm takes the last 128K of the memory): on a 128K machine they
  hang on their picture (red border). Create a `PENTAGON` with 512K (`emulator_manage create` with `ram_size: 512`,
  `assemble-in-emulator.py zasm315 --ram 512`).
- **`ENDA`** (3.2x and later) ends one assembly and starts the next with an empty label table: a label defined again
  after it is no error, one defined only before it is undefined after it.
- **`~text~`** goes through an XLAT table: ZAsm's own (Latin to CP866 Russian and back, the same in 3.10 … 4.20)
  until `LOADTAB "file"` loads a file's first 256 bytes; `zxasm convert` of an image reads that file.
- **A second instance starts paused** (WebAPI `POST /emulator/start`): `resume` it before typing, or the keys wait.
