# Recipe: A Sprinter disk that starts your program at boot

Goal: boot the Sprinter from its DSS system disk with your files on it, straight into a program (a demo, a test
build, a tool) or to the DSS prompt, without Flex Navigator and without typing. The base image is never changed.
For a profile, a benchmark, a test, or a GUI session that has to start the same way every time.

Why, how DSS boots, and which way to pick:
[test-disk-images.md](../../docs/inprogress/2026-09-28-sprinter/test-disk-images.md). Mounting and booting a disk
in general: [sprinter-hdd.md](sprinter-hdd.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi)
> inside host-side pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## The base disk

The MAME pack's system disk, DSS 1.71.57, not in the repository. `sp_hdd_sys.chd` boots as it is. For the
host-side ways below it must be raw:

```bash
chdman extracthd -i sp_hdd_sys.chd -o sp_hdd_sys.img     # MAME's chdman (brew install rom-tools): 1 GiB, FAT16 at LBA 63
```

## Way 1: a composite, no copy (`*.ucompose.yaml`)

The base image at the bottom and host folders on top. An upper file shadows the one below it. Upper files can
be of any size, and the guest's writes stay in the session.

```bash
mkdir -p scratch/sp-dnt/patch
printf '@echo off\r\ncd \\demos\\dntblink\r\ndntblink\r\n' > scratch/sp-dnt/patch/SYSTEM.BAT     # CR LF lines
cat > scratch/sp-dnt/sp-dnt.ucompose.yaml <<EOF
version: 1
layers:
  - {name: system, source: {image: /path/to/sp_hdd_sys.img}}
  - {name: demo,   source: {folder: $PWD/testdata/machines/sprinter/demo/dont_blink_test1}, mount: /DEMOS/DNTBLINK}
  - {name: patch,  source: {folder: patch}}
EOF
```

Insert `scratch/sp-dnt/sp-dnt.ucompose.yaml` like a disk. Folder paths are relative to the descriptor or
absolute. On the slot, `media layers` reports build `graft` (the medium format is `graft-fat16`).

## Way 2: a copy (`make-test-image.sh`)

One ordinary raw image: for tools that take only a file, or for a copy to hand around. On APFS the copy is a
clone, so it takes no space until written.

```bash
tools/machines/sprinter/test-image/make-test-image.sh -b /path/to/sp_hdd_sys.img -o scratch/sp-dntblink.img \
    -c testdata/machines/sprinter/demo/dont_blink_test1:/DEMOS/DNTBLINK -r 'cd \demos\dntblink' -r dntblink
tools/machines/sprinter/test-image/make-test-image.sh -b /path/to/sp_hdd_sys.img -o scratch/sp-prompt.img -P   # C:\>
```

Options and what it writes: [tools/machines/sprinter/test-image/README.md](../../tools/machines/sprinter/test-image/README.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"SPRINTER"}
media {"action":"insert","slot":"ide0.master","path":"<abs>/scratch/sp-dnt/sp-dnt.ucompose.yaml"}
media {"action":"layers","slot":"ide0.master"}            # build graft, the three layers
emulator_manage {"action":"reset"}
control_execution {"action":"run_frames","frames":1200}   # DSS boots, SYSTEM.BAT starts the demo (~20 s emulated)
capture_media {"action":"screenshot","format":"png","area":"full","filename":"scratch/sp-dnt.png"}
```

A copy from way 2: the same `insert` with `"access":"session"` keeps the guest's writes out of the file.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
ID=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"SPRINTER"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sp-dntblink.img\",\"access\":\"session\"}" | jq -c '{ok, slot}'
curl -s -X POST $BASE/emulator/$ID/reset
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":1200}'
curl -s "$BASE/emulator/$ID/capture/screen?area=full&format=png&path=$PWD/scratch/sp-dnt.png" | jq -c '{saved}'
```

## Tests and benchmarks: no image at all

`UNREAL_SPRINTER_HDD` takes the raw disk, a copy from way 2 or a descriptor from way 1. The demo tests and the
benchmarks rewrite `SYSTEM.BAT` themselves: they insert the disk in session mode, then `FatInPlace` writes the
new text over the old one. See
[core/benchmarks/emulator/machines/README.md](../../core/benchmarks/emulator/machines/README.md).

`FatInPlace` keeps the file's length and cluster chain, so the new text is padded to the old length with a
`rem` line, and it cannot be longer than the file. With a descriptor, the patch layer's `SYSTEM.BAT` must be at
least as long as the text the test writes (pad it with `rem` and spaces).

## Assert on / pitfalls

- **The program ran.** For example: the screenshot shows the demo, not the "C:\>" prompt or Flex Navigator's
  panels.
  - DSS errors such as "Bad command or file name" mean a wrong path in `SYSTEM.BAT`. DSS paths use
    backslashes.
  - Use CR LF line ends: `printf '...\r\n'`.
- **The PATH line.** The shipped `SYSTEM.BAT` sets `PATH` to the DSS tool folders. Without it, a program that
  calls DSS tools does not find them. `make-test-image.sh` keeps the line; a hand-made patch should copy it.
- **Flex Navigator** (`fn`, the last line of the shipped file) waits for the mouse or keys. Remove it to get to
  the prompt.
- **The same boot every time** needs the fixed clock and the fast start (which skips the ROM's PLD loader). The tests and the benchmarks set both:
  `PortDecoder_Sprinter::GetRtc().SetFixedTime`, `config.sprinter.fast_start`. A GUI session does not.
- **Your build is not the disk's build.** `dont_blink_test1` is the owner's local folder, not committed.
  - Its music streams differently: the Covox ring gets 4 608 writes in the first frames against more than
    100 000 for the disk's DNTBLINK.
  - So `SprinterFastPathsDemo_Test.DontBlink`'s music check fails on it, while its exactness checks pass.
- **Never commit the images.** They are 1 GiB and derived from the owner's disk. Keep them in `scratch/`.
