# NedoOS IDE HDD image for ATM710 / ATM3 (ZX-Evo)

`hdd_nedo.vhd` (128 MiB, MBR + 2 FAT partitions, `hddfdisk`-formatted) is the ready NedoOS
hard-disk template shipped in the NedoOS source tree, decompressed from
`tools/vhd/hdd_nedo.vhd.xz`.

Source (local checkout, not a submodule of this repo):
`/Volumes/TB4-4Tb/Projects/emulators/github/NedoOS/tools/vhd/hdd_nedo.vhd.xz`
(same content in the `svn/nedoos` mirror). Upstream: https://github.com/alfishe/NedoOS.

Not committed to git (per the 2026-09-29 `testdata/machines/tsconf/wildcommander` convention:
downloaded third-party test assets stay untracked unless the repo already has a pattern for them).

## Layout (from `release/doc/hddfdisk.txt`, MBR as read 2026-09-30)

- Partition 1: FAT, ~60 MiB, offset 1 MiB (start LBA 128, 122880 sectors)
- Partition 2: FAT, ~70 MiB, offset 61 MiB (start LBA 123008, 137088 sectors)
- Remainder (~169 MiB by the tool's own numbers, actually smaller here - a 128 MiB image):
  unpartitioned, reserved for growing a partition with `hddfdisk`

This is the format ERS ("B. HDD boot", ZX-Evo) / xBIOS (ATM) expect: an MBR with FAT
partitions, built by the in-ROM `hddfdisk` tool. **Contrast with**
`testdata/machines/tsconf/wildcommander/sd-images/*.img`, which are raw FAT32-with-no-MBR
SD card images for TS-Conf's `sd.zc` slot - mounting those into `ide0.master` on ATM3
loads content the IDE HDD boot path cannot parse and the machine hangs in `HALT`
(no partition table it recognizes). See docs/inprogress/2026-09-29-media-drop-targets/
design.md for the media-manager compatibility-advisory work this confusion motivated.

## Quick start in unreal-ng

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"ATM3"}'
curl -s -X POST "$BASE/emulator/$EMU/media/hd/insert" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/baseconf/hdd-images/hdd_nedo.vhd"}'
# reset, then in the ERS main menu: B (HDD boot)
```

**Verified 2026-09-30** (WebAPI, `ATM3`): after `B. HDD boot`, the IDE unit issues real
`READ SECTORS` traffic at scattered LBAs (FAT/directory access pattern, not a single linear
read), and PC/R keep changing frame to frame (CPU genuinely executing) - unlike the TS-Conf
image, which parks in `DI`+`HALT` within a couple of frames and never touches the disk again.

Not yet confirmed reaching a NedoOS desktop/shell screen. `GET /capture/screen` kept returning
the same ERS menu bitmap for 10+ seconds into the boot while `state/screen/digest` (a whole-bank
hash) kept changing - initially looked like a stale capture, but is not: re-checked with
`state/screen/attributes` (reads VRAM directly, bypassing the render pipeline entirely) and it
was *also* byte-identical over the same window, while a plain idle ERS menu (no disk boot)
visibly ticks its on-screen clock frame to frame through the same `capture/screen` path. So the
capture is live and correct; the visible screen genuinely was not repainted during this stretch
of the boot, and the changing digest is consistent with the loader using the rest of RAM page
5/7 as scratch/DMA buffer space (well outside the ~6.9 KiB actually displayed) rather than any
capture bug. Whether the boot is still progressing silently past this point, or stuck, is open -
would need a longer wait / a debugger breakpoint, not a capture-path fix.
