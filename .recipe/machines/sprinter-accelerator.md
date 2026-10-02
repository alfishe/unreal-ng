# Recipe: Sprinter accelerator (check that block operations work)

The Sprinter Sp2000's PLD has a block accelerator: a same-register `LD r,r` arms a mode
(`LD D,D` length, `LD C,C` fill, `LD L,L` copy, `LD E,E` / `LD A,A` the vertical forms down the
graphics screen, `LD B,B` off), and the next memory access is repeated up to 256 times. Graphics
software (Flex Navigator, DSS's picture viewers, demos) draws with it; without it, text comes out as
horizontal segments. This recipe runs the HDD system disk's own test program, `TESTS\ACCTEST.EXE`,
and checks its picture in the graphics RAM.

Ground truth: design and behavior
[docs/inprogress/2026-09-28-sprinter/tdd-accel-sound-input.md](../../docs/inprogress/2026-09-28-sprinter/tdd-accel-sound-input.md)
§1, what was built and verified
[s5-accelerator-outcome.md](../../docs/inprogress/2026-09-28-sprinter/s5-accelerator-outcome.md),
code `core/src/emulator/memory/sprinter/sprinteraccelerator.{h,cpp}`.

> **How to use the sections:** [WebAPI](#webapi-verified) below was run end to end (2026-10-02). With
> MCP use the same calls through `invoke_api` (`emulator_manage` creates the machine). Policy:
> [_common/transports.md](../_common/transports.md).

## What ACCTEST does

`ACCTEST.EXE` (4 632 bytes, `testdata/machines/sprinter/software/acctest.exe`) loads at `#8100`,
switches to the 320 x 256 graphics mode, puts graphics page `#50` into window 3 and copies a 64 x 64
picture from `#8300` row by row: `LD D,D : LD A,#40` (length 64), `LD L,L : LD A,(HL)` (64 bytes into
the accelerator's buffer), `LD (DE),A` (64 bytes into row PORT_Y), `LD B,B`. Then it waits for ESC.

So after it runs, row `y` of the graphics screen (main RAM page `#50 + y / 16`, offset
`(y % 16) x 1024`) starts with the 64 bytes at file offset `#216 + y x 64` (the file's code starts at
offset `#16` = `#8100`, so `#8300` is offset `#216`).

## WebAPI (verified)

```bash
BASE=http://localhost:8090/api/v1          # UNREAL_WEBAPI_PORT moves the port

# 1. A DSS 1.62 boot floppy that runs ACCTEST (mtools; the floppy has 6 KB free)
cp testdata/machines/sprinter/dss_1_62_92.img scratch/dss-acctest.img
printf '@echo off\r\nacctest\r\n' > scratch/system.bat
mcopy -o -i scratch/dss-acctest.img scratch/system.bat ::SYSTEM.BAT
mcopy -o -i scratch/dss-acctest.img testdata/machines/sprinter/software/acctest.exe ::ACCTEST.EXE

# 2. The machine, the floppy in drive B (a blank CMOS boots the IDE master, then floppy B)
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
         -d '{"model":"SPRINTER"}' | jq -r .id)
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/B/insert" -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/dss-acctest.img\"}" | jq -r .status          # success

# 3. Run: with no key the two IDE waits time out (~31 s each), then DSS boots and runs SYSTEM.BAT.
#    4500 frames is enough from a full start (FastStart=0); F4 at each "[Press F4" skips the waits
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' \
     -d '{"frames": 4500}' | jq -c '{status, pc}'                               # pc 33107 = #8153, the ESC loop

# 4. Row 0 of the picture in the graphics RAM, against the file
curl -s "$BASE/emulator/$EMU_ID/memory/ram/80/0?len=64" | jq -r .hex
xxd -s 0x216 -l 64 -c 64 -p testdata/machines/sprinter/software/acctest.exe   # the same 64 bytes
#   → 0D 0D 0D 02 05 06 06 08 06 06 ...

# 5. The screen: a stone texture at the top left (the rest is what the BIOS left in video RAM)
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png&mode=full&path=$PWD/scratch/acctest.png" | jq -r .saved
```

All 64 rows: read pages `#50`-`#53` (`/memory/ram/80/0?len=16384` ... `/memory/ram/83/0?len=16384`)
and compare `data[(y % 16) * 1024 : +64]` with the file; the run of 2026-10-02 had 0 rows differing and
the screenshot was byte-identical to `testdata/machines/sprinter/golden/acctest.png`. MAME's
`sprinter` driver (BIOS 3.06, the same program from the HDD image) draws the same 128 x 64 display
pixels (`testdata/machines/sprinter/reference/mame-acctest-306.png`).

## Signs that the accelerator is missing or broken

| Symptom | Cause |
|---|---|
| Only column 0 of the picture is drawn (one byte per row) | no accelerator: each `LD (DE),A` stores one byte |
| Flex Navigator's text is drawn as horizontal segments | the same: its glyphs are vertical copies (`LD A,A`) |
| Flex Navigator stops after its screen clear (`DI : HALT` at `#0000`) | not the accelerator: the BIOS floppy RESTORE timing (roadmap §8) |

## Accelerator state (debugger and automation)

`PortDecoder_Sprinter::GetAccelerator()` returns the accelerator in use (null while the PLD loads);
`State()` is the POD `SprinterAccelState`: `mode` (0-7, `SprinterAccelerator::ModeName`), `length`
(0 = 256), `fn` (`FunctionName`: plain / or / xor / and), `blocked` (an INT acknowledge suspended it,
`[SPRINTER] AccelIntSuspend=1`), `alt` / `xcnt` / `aagr` (the `#C7` addressing), `buffer[256]`,
`operations`, `lastExtraClocks`. A WebAPI / MCP view of it (`state/sprinter`) comes with the
`sprinter-automation` work.
