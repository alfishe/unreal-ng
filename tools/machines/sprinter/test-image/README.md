# Sprinter test disk images (`make-test-image.sh`)

Makes a test copy of a Sprinter DSS hard disk: a raw image with host files added and `SYSTEM.BAT` rewritten, so
the machine boots straight into a program (a demo, a test build) or stops at the DSS prompt. Why and when to use
a copy, and what the emulator can do without one:
[test-disk-images.md](../../../../docs/inprogress/2026-09-28-sprinter/test-disk-images.md).

Needs mtools (`brew install mtools`) and Python 3.

```bash
SYS=/path/to/sp_hdd_sys.img     # the MAME pack's system disk, raw (chdman extracthd -i sp_hdd_sys.chd -o sp_hdd_sys.img)

# A demo build of your own in place of the disk's, started at boot
tools/machines/sprinter/test-image/make-test-image.sh -b $SYS -o scratch/sp-dntblink.img \
    -c testdata/machines/sprinter/demo/dont_blink_test1:/DEMOS/DNTBLINK \
    -r 'cd \demos\dntblink' -r dntblink

# DSS stops at C:\> (the shipped SYSTEM.BAT without fn: Flex Navigator)
tools/machines/sprinter/test-image/make-test-image.sh -b $SYS -o scratch/sp-prompt.img -P
```

| Option | Meaning |
|:--|:--|
| `-b base.img` | the raw disk to copy. It is never written |
| `-o out.img` | the copy. On APFS it is a clone (`cp -c`): it takes no space until blocks are written |
| `-c host:DOS` | copies a host file, or the contents of a host folder, into a DOS folder. Missing folders are made, and files of the same name are replaced. Repeatable |
| `-r line` | a `SYSTEM.BAT` line. Repeatable, kept in order. The lines replace the shipped `ver` / `fn` |
| `-P` | stop at the prompt: the shipped `SYSTEM.BAT` without `fn`. Ignored with `-r` |

What it writes:
- The partition is MBR entry 0, the one DSS boots from. The script reads its first LBA there (LBA 63 on the MAME
  pack's disks, offset 32 256 for mtools).
- The new `SYSTEM.BAT` has CR LF lines: `@echo off`, the shipped `set PATH=...` line (so DSS still finds its
  tools), then the `-r` lines.
- At the end the script prints the file as written.

Use the copy:
- as `ide0.master` in the GUI or through the WebAPI / MCP (`media insert`, [sprinter-hdd.md](../../../../.recipe/media/sprinter-hdd.md));
- as `UNREAL_SPRINTER_HDD` for the disk tests and the demo benchmarks.

Insert it with `access: session` when the guest's writes must not reach the file.

Checked 2026-10-09 on the MAME pack's `sp_hdd_sys.img` (md5 `119826ea04caf9f5c3b13254c50bd10e`).
