# Recipe: Laser Genius (Oasis Software) in unreal-ng

Run Laser Genius 1.04 (the Beta Disk version: assembler, editor, tool-kit), put a source into it, assemble it, read
the bytes, and move sources between Laser Genius and the host (`zxasm` reads and writes its files). Every step was
run on 2026-10-08.

Related: [README.md](README.md) (own instance, `$PORT`), the format and conversion
`docs/inprogress/2026-10-05-unreal-asm/research-laser-genius.md`.

## Where Laser Genius is

- [LG_1_04.ZIP](https://vtrd.in/system/LG_1_04.ZIP): `LG_1_04.SCL`, the Beta Disk version (MOA, 1992): `L_G_ASS.`
  (the BASIC loader), `ASM`, `TOOLS`, `HASH`, `BETA_LG`, `REL.BETA`.
- ZXDB [8330](https://spectrumcomputing.co.uk/entry/8330/ZX-Spectrum/Laser_Genius): the Oasis tapes
  (`LaserGenius.tzx.zip`; tape 2 side B holds the example sources), Kamasoft's sources
  (`LaserGenius(LGZdrojaky)(Kamasoft).tzx.zip`) and the manual.

## Start it

On a `PENTAGON` model (`scl2trd` makes a TRD of the SCL: `tools/verification/unreal-asm/lib/zxdisk.py scl2trd`):

```text
POST /api/v1/emulator/start {"model":"PENTAGON"}            -> id (resume it: a second instance starts paused)
POST /disk/insert ... lg.trd ; run L_G_ASS. from TR-DOS (Emulator.run_trdos in lib/emulator.py)
key 1 (load and run), then the loader's questions, each answered with its key and ENTER:
  Load the tool-kit? y   Load the hash extensions? n   Load from diskette or tape? d
```

The editor starts on an empty screen with the cursor top left.

## The editor

A sentence typed on a blank line with ENTER is a command (`list`, `assem`, `load "NAME"`, `save "NAME"`, `stats`,
`cls`, `set space n`) or a statement. A statement goes into the source only under a paragraph number: type
`10 ld a,7`, then ENTER; a statement typed on the next line without a number joins paragraph 10. Labels need a
colon (`loop: djnz loop`). Keys: `CS+3` deletes the sentence, `CS+0` the character before the cursor, `SS+A` is
ESC (closes a message), `SS+Y` `[`, `SS+U` `]`, `SS+S` `|`, `SS+D` `\`.

WebAPI: `/keyboard/type` sends letters, digits and the usual signs, but not `[ ] | \ _`: send those as
`/keyboard/combo {"keys":["ss","y"]}`; `"\n"` in the text is not ENTER (`/keyboard/tap enter`).

## Put a source in

From the host: `zxasm encode probe.txt --codec lasergenius -o probe.raw` gives the paragraphs; the disk file is
`#AF`, the length (2 bytes), `#7F2E`, four characters of the name, then those bytes (a CODE file, the name up to 4
characters). Add it to the TRD (`zxdisk.py add`), insert the disk, `load "NAME"`.

Typing works too: one paragraph a line (`10 nop`, `20 ld a,7`, …) with ENTER after each; a line the editor took
moves the end of the text, `(#A973)`, so a script knows when to type the next.

## Assemble and read the code

Laser Genius stores code only after a `PUT`; `stats` shows the free range (`low` `#68D2` to `table` `#7F22` in
1.04 Beta):

```text
10 ORG #6900
20 PUT #6900
...
POST /memory/write {"address":26880,"data":[170,...]}       <- #AA first
assem  (ENTER)                                              -> "** pass 2 / errors 0 / warnings 0"
GET  /memory/read/26880?length=...
```

After each error or warning the assembler waits for a key (ESC stops it); `*REPORT OFF` at the top makes it go on.

## Sources to and from the host

```bash
zxasm files "Laser Genius Tape 2 - Side B.tzx"       # the joined files show as type L, codec lasergenius
zxasm decode tape.tzx --file SIEVE.ASM.L -o sieve.txt
zxasm convert tape.tzx --to sjasmplus -o out/
zxasm decode lg.trd --file PROBEALL.C -o probe.txt   # a file SAVEd on disk (the #AF header)
```

## Pitfalls

- **Answer a Y/N question once, after it is printed:** the key routine clears LAST_K when it starts and the
  question prints slowly; a key pressed earlier is lost, a key held for a second repeats into the editor.
- **"delete the name table? (Y/N)" from `LOAD ASCII` loops on Y** in 1.04 Beta (the deletion is patched out and
  the file does not fit); N abandons. `LOAD ASCII` does not read disk files there at all: type the text, or write the
  disk file as above.
- **"file not saved continue? (Y/N)"** before `load` when the text changed: Y drops it.
- **The space is 10K** for the text and the screen buffer; `set space` larger fails ("space too large") with the
  tool-kit loaded: split big sources.
- **No code without PUT**, and PUT only between `low` and `table` (`stats`).
- **Division by a word of `#8000` or more is wrong** in Laser Genius (`#8000/#FFFF` = `#5555`); the conversion
  computes it right.
- **Phoenix (`.PHX`) files** decode like the assembler's (the `#` pseudo-ops of the hash extensions); the conversion
  reports their statements (no assembler equivalent). Typing Phoenix needs the hash extensions loaded (the loader's
  second question: y); then the text starts at `#77AC` and its end pointer is `(#ADA1)`; register names (`a`, `z`)
  are not variable names there.
