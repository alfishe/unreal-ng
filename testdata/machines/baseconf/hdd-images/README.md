# NedoOS IDE HDD template for ATM710 / ATM3 (ZX-Evo)

`hdd_nedo.vhd` (128 MiB + the 512-byte VHD footer, MBR + 2 FAT partitions, `hddfdisk`-formatted)
is the empty hard-disk template shipped in the NedoOS source tree, decompressed from
`tools/vhd/hdd_nedo.vhd.xz` of <https://github.com/alfishe/NedoOS>.

Not committed to git (per the 2026-09-29 `testdata/machines/tsconf/wildcommander` convention:
downloaded third-party test assets stay untracked unless the repo already has a pattern for them).

## It is NOT bootable as shipped

Sectors 1 and 2 are zero and the partitions hold no `zxldr` file, so ERS "B. HDD boot" (it reads
24 KB from LBA 2 to `#6000` and jumps there) executes zeros and the machine stays at or returns to
the ERS menu without a message. Nothing is wrong with the emulator or the image: the boot block is
written later, from inside NedoOS, by `hddfdisk` (key `b` on a partition: creates the unfragmented
`zxldr` file and the boot sectors). The loader then offers the partitions ("1.NedoOS") and starts
`sd_boot.$C` from the chosen one.

## Making it bootable (verified 2026-10-04, `ATM3`)

1. Boot NedoOS from an SD card folder that also carries `osatm3hd.$C` (the HDD kernel, system drive
   `E:`) and `bin/` of the release; insert the template as `hd`.
2. In the shell run `hddfdisk.com`: `0` (Nemo master), `1` (partition 1), `b`, `y`, any key, `q`, `q`.
3. Copy the kernel and the programs onto the disk: `copy m:/osatm3hd.$C e:/sd_boot.$C`,
   `copydir m:/bin e:/bin`, `copydir m:/ini e:/ini` (about 3 minutes of host time).
4. Export the disk (`POST /emulator/{id}/media/ide0.master/export`).
5. Alone in the machine, ERS "B. HDD boot" -> `1` reaches the NedoOS shell.

## Layout (from `release/doc/hddfdisk.txt`, MBR as read 2026-09-30)

- Partition 1: FAT, ~60 MiB, offset 1 MiB (start LBA 128, 122880 sectors)
- Partition 2: FAT, ~70 MiB, offset 61 MiB (start LBA 123008, 137088 sectors)
- Remainder: unpartitioned, reserved for growing a partition with `hddfdisk`

This is the format ERS ("B. HDD boot", ZX-Evo) / xBIOS (ATM) expect: an MBR with FAT
partitions. **Contrast with** `testdata/machines/tsconf/wildcommander/sd-images/*.img`, which are
raw FAT32-with-no-MBR SD card images for TS-Conf's `sd.zc` slot - mounting those into
`ide0.master` on ATM3 loads content the IDE HDD boot path cannot parse and the machine hangs in
`HALT` (no partition table it recognizes). See
[2026-09-29-media-drop-targets/design.md](../../../../docs/inprogress/2026-09-29-media-drop-targets/design.md)
for the media-manager compatibility-advisory work this confusion motivated.

## Quick start in unreal-ng (an image made as above)

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"ATM3"}'
curl -s -X POST "$BASE/emulator/$EMU/media/hd/insert" -H 'Content-Type: application/json' \
     -d '{"path":"scratch/nedoos-hdd.img"}'
# reset; in the ERS main menu "B. HDD boot" is the 4th entry (down x3, Enter), then press 1
```
