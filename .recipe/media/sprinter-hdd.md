# Recipe: Boot DSS from a hard disk on the Sprinter

Goal: put a hard disk image on the Sprinter's IDE (`ide0.master`), boot Estex DSS from it, check
what the BIOS detected and what DSS printed, and read the IDE board's state.

Reference: design [tdd-storage.md](../../docs/inprogress/2026-09-28-sprinter/tdd-storage.md) §3.4,
outcome [roadmap-and-plan.md](../../docs/inprogress/2026-09-28-sprinter/roadmap-and-plan.md) §9;
media verbs in [use-media-slots.md](use-media-slots.md). Verified 2026-10-02 on a GUI build
(WebAPI and CLI as below); the MCP calls are the same `media` / `inspect_state` verbs.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi)
> inside host-side pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## The machine and the disks

- **Slots** (`[HDD] Scheme=SPRINTER`, shipped): `ide0.master`, `ide0.slave` (primary channel),
  `ide1.master`, `ide1.slave` (secondary). Alias `hd` = the first hard-disk unit. An empty unit
  has no device on the bus.
- **Which BIOS** (`[ROM] SPRINTER=` in `configs/sprinter/unreal.ini`):
  - `rom/sprinter/sp2k-3.07-beta1.rom` (the default since 2026-10-02) or
    `rom/sprinter/sp2k-3.06-hf2.rom`: needed for **DSS 1.71** (the MAME pack's system disk boots to
    Flex Navigator 1.15 on both); they probe all **four** units.
  - `rom/sprinter/sp2k-3.04.rom` boots DSS 1.62 from a hard disk and probes the **primary**
    channel only; on it DSS 1.71 loads SYSTEM.DOS and stops with "Fatal error! Press RESET to
    restart." (MAME does the same).
- **Which disk:**
  - a small bootable image you build (below): DSS 1.62.92 from the repo's floppy;
  - the owner's real disks are **not in the repository**: the MAME pack's `sp_hdd_sys.chd`
    (DSS 1.71.57) and `sp_hdd_media.chd`, **inserted as they are** (no extraction: a CHD is a
    hard-disk format here, [chd.md](../../docs/file-formats/disk-images/chd.md)), and the ZXMAK2
    bundle's `sp_disk1.vhd` (DSS 1.62.93, a fixed VHD, inserted as is).

## Build a small bootable disk (mtools)

DSS boots a disk whose MBR entry 0 is a FAT16 partition (type `#06`) and whose LBA 1-3 hold the
DSS loader ("Starting..."). The DSS floppy's LBA 1-3 are that loader.

```bash
IMG=scratch/dss-hdd.img; FLOPPY=testdata/machines/sprinter/dss_1_62_92.img
dd if=/dev/zero of=$IMG bs=512 count=32768                               # 16 MiB
dd if=$FLOPPY of=$IMG bs=512 skip=1 seek=1 count=3 conv=notrunc           # the DSS loader at LBA 1-3
python3 -c "import struct; f=open('$IMG','r+b'); f.seek(446); \
f.write(bytes([0x80,0,0,0,6,0,0,0])+struct.pack('<II',63,32768-63)); f.seek(510); f.write(b'\x55\xaa')"
mformat -i $IMG@@32256 -T 32705 -h 16 -s 32 -H 63 -c 4 ::                # FAT16 at LBA 63 (offset 32256)
mcopy -i $FLOPPY ::SYSTEM.DOS ::SYSTEM.EXE scratch/
printf 'ver\r\n' > scratch/SYSTEM.BAT
mcopy -i $IMG@@32256 scratch/SYSTEM.DOS scratch/SYSTEM.EXE scratch/SYSTEM.BAT ::
```

The core tests build the same kind of image in C++ (`BuildDssHdd`, `sprinter_boot_test.cpp`).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"SPRINTER"}
media {"action":"insert","slot":"ide0.master","path":"<abs path>/scratch/dss-hdd.img"}
media {"action":"insert","slot":"ide0.master","path":"<abs path>/sp_hdd_sys.chd"}  # MAME's CHD as is: writes stay in memory (session)
media {"action":"save","slot":"ide0.master"}                                         # ...until you write them into the CHD
emulator_manage {"action":"reset"}
inspect_state {"aspects":["ide"]}
#  -> [ide] SPRINTER, selected primary master, data latch #0
#       ide0.master (disk): <image>, status #50, last READ SECTORS
capture_media {"action":"screenshot","format":"png","area":"full","filename":"scratch/sprinter-hdd.png"}
```

- `media {"action":"targets","path":...}` lists the four IDE slots and the NeoGS SD card for a
  `.img`, so `insert auto` is ambiguous: name the slot.
- An empty CD unit on the slave: insert with `"device":"cdrom"` into an empty `ide0.slave` (or
  `[HDD] CD1=1`); the BIOS reports "UNREAL-NG CD-ROM".

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU_ID=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' \
         -d '{"model":"SPRINTER"}' | jq -r .id)

curl -s $BASE/emulator/$EMU_ID/media | jq -c '.slots[] | select(.id|startswith("ide")) | {id, aliases, label}'
#  {"id":"ide0.master","aliases":["hd"],"label":"IDE primary master (hard disk)"} ... ide1.slave

curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/dss-hdd.img\"}" | jq -c '{ok, slot, pending}'
#  {"ok":true,"slot":"ide0.master","pending":false}
curl -s -X POST $BASE/emulator/$EMU_ID/reset

# ~10 s of emulated time later (on BIOS 3.04 the empty slave probe takes 5.7 s; tap F4 below)
curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=$PWD/scratch/sprinter-hdd.png" | jq -c '{saved}'
curl -s $BASE/emulator/$EMU_ID/state/ide | jq -c '{scheme, channels, selected_channel, adapter: .adapter.data_latch,
      units: [.units[] | {slot, present, status: .task_file.status, last: .command.name}]}'

# Type a DSS command: the text, then Enter as a separate tap ("\n" inside the text runs it twice)
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/type -H 'Content-Type: application/json' -d '{"text":"dir"}'
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap  -H 'Content-Type: application/json' -d '{"key":"enter","frames":3}'

# An empty channel needs no key ("None" at once); F4 skips the one wait left: BIOS 3.04's
# absent slave next to a master (280 frames, "[Press F4 to skip]")
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"F4","frames":3}'
```

## CLI

```text
state ide          # the same report: scheme SPRINTER, channels 2, selected_channel, adapter (data_latch,
                   # channel), and per unit slot / channel / medium / task file / last command
media list
```

## Assert on

- The screenshot (text mode, 736 x 288): "Detecting IDE Primary Master ... UNREAL-NG HDD",
  "Boot from HDD Primary IDE Master OK" (3.06 / 3.07) or "Start from Hard disk...Ok" (3.04), then the DSS
  banner: "Estex DSS Version 1.62.92" / "Estex DSS version 1.71.57. Shell version 1.2.522." and
  the `C:\>` prompt (a `SYSTEM.BAT` that runs `fn` switches to Flex Navigator's graphics screen).
- `state ide`: `scheme` = `SPRINTER`, `channels` = 2, `ide0.master` `present: true`, status 80
  (`#50` = DRDY | DSC) after the boot, last command `READ SECTORS`.

## Pitfalls

- **Images are written by default** (WriteThrough): a real disk you want to keep unchanged goes in
  with `"access":"session"` (or `readonly`).
- **A CHD is never written by the guest**: it goes in as `session` whatever the default, and its
  file changes only on `save` (the CHD written again, unchanged hunks kept as stored). BIOS 3.06
  and 3.07 boot DSS 1.71 from `sp_hdd_sys.chd` directly (`RealHdd_Dss171BootsFromTheMamePackChd`).
- **DSS 1.71 on BIOS 3.04 fails** with "Fatal error" after "Start from Hard disk...Ok": switch the
  BIOS to 3.07 BETA 1 (the shipped default) or 3.06 Hotfix 2, not the disk.
- **No IDE wait for an empty channel**: it reads `#7F` (the ATA DD7 pull-down, BSY = 0), so every
  BIOS prints "None" for its units at once - no F4 for the secondary channel of 3.06 / 3.07. The one
  wait left is BIOS 3.04's absent slave next to a master (280 frames); tap F4 at its
  "[Press F4 to skip]" line. In the Qt GUI F4 reaches the machine while the screen has focus (the
  menu's F-key shortcuts give way on PC-keyboard machines, [keyboard.md](../../docs/features/keyboard.md)).
- **TTD does not record the Sprinter yet** (phase S7).
