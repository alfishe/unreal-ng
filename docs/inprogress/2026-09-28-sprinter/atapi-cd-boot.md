# Booting the Sprinter from an ATAPI CD

**Status:** researched and verified on the emulator 2026-10-04 (BIOS 3.06 Hotfix 2, the default). The community
BIOS can start a system from a CD; no published CD carries the boot sector it looks for, and no DSS boot loader for
a CD exists yet. This page is the reference: what the BIOS does, what a bootable CD must contain, how to make one,
and what is still missing. Related: [tdd-storage.md](tdd-storage.md) (IDE and ATAPI in the emulator),
[bios-versions.md](bios-versions.md), the CD utility notes below.

## 1. Which firmware can do it

| BIOS | Boot from CD |
|---|---|
| 3.04 (Peters Plus, 2003) | no (the string "Boot from CD-ROM" is not in the image) |
| 3.06 Hotfix 2 (2026-01-19) | **yes** |
| 3.07 BETA 1 (2026-09-24) | yes, the same code |

The code is in the community BIOS sources (Sprinter-BIOS, file `bios/rom/SETUP/MAIN.asm`: `PrepareToBOOT`,
`HDSTART`, `CDSTART`, `OS_LOAD`, `MOVE1`, `SYSID`). Its comment dates the feature 15.02.2025 and still marks it
`[ ]` (in progress), but the path works end to end (section 6).

## 2. Choosing the boot device (CMOS)

The boot device is a CMOS cell, set in BIOS SETUP or written directly:

| Cell | Bits | Meaning |
|---|---|---|
| `#10` (`BootDrives`) | 2-0 | system disk |
| `#10` | 6-4 | alternative system disk, used when ALT is held at start |
| `#3F` | 7-0 | checksum of cells `#0E-#20` |

Values of a disk field (`PrepareToBOOT`): 0 floppy A, 1 floppy B, **2 IDE primary master, 3 IDE primary slave,
4 IDE secondary master, 5 IDE secondary slave**, 6 RAM disk, 7 the recovery image in ROM.

There is no separate "CD" value. An IDE value whose drive the start-up detection found to be ATAPI
(`HDD_INIT_TABLE.DriveType = IDE.Device.ATAPI`) goes to `CDSTART` instead of the hard disk path. So "boot from the
CD on the primary slave" is `#10` bits 2-0 = 3.

**Checksum** (`SETTINGS.asm` `CHEKSUM`): start with H = `#DE`; for each cell v from `#0E` to `#20` (19 cells):
H = rotate-left(H - v) - v (8-bit, `RLCA`). The result goes to `#3F`; a wrong checksum makes SETUP restore the
defaults.

Worked example (the emulator's default CMOS): cells `#0E-#20` = `4A 07 02 00 00 10 10 20 00 00 00 00 00 00 00 06
00 77 00` give `#E7`, the stored value. Setting `#10` to `03` (boot from the primary slave) gives `#F2`.

## 3. What the BIOS does

```text
CDSTART:  drive code = #C0 | n          (DRIVE_CODES.SPRINTER.ATAPI, n = the IDE position 0-3)
          print "Boot from CD-ROM " + the channel name
          OS_LOAD with sector size 512, sector 1        -> on a CD: no signature, returns with carry
          OS_LOAD.CD with sector size 2048, sector 17 (#0011)
OS_LOAD:  BIOS DRV_READ (#55): A = drive code, HL:IX = sector, B = 1, DE = #7800 (TEMP)
          compare the first 12 bytes with SYSID = "Starting..." + #00
          equal: print "OK", then
MOVE0:    DI, IM 1; copy the MOVE1 stub below #7800 and jump to it
MOVE1:    SP = #8000; copy the sector (2048 bytes for a CD) from #7800 to #8000; JP #800C
```

On entry to the loader at `#800C`:

| Register / memory | Value |
|---|---|
| PC | `#800C` (right after the 12-byte signature) |
| A | the drive code: `#C0` primary master, `#C1` primary slave, `#C2` / `#C3` secondary |
| SP | `#8000` |
| Interrupts | disabled, IM 1 |
| `#8000-#87FF` | the boot sector as read (2048 bytes) |

The loader is expected to load the operating system itself through the BIOS (`DRV_READ` with the same drive code
reads 2048-byte CD sectors by LBA). The FAT loaders store A into their first byte ("the first character of the
loader is replaced by the start drive number", `Shared_Includes` `_mSYSID`).

## 4. What a bootable CD must contain

**Logical sector 17 (byte offset 34 816), 2048 bytes: `"Starting..."`, a zero byte, then the loader code from
offset 12, which runs at `#800C`.** Nothing else is required by the BIOS: it reads no volume descriptor and no
El Torito catalog.

ISO 9660 places its volume descriptors from sector 16 on, and the common ones sit exactly at 17:

| Sector | Usual content | On a Sprinter boot CD |
|---|---|---|
| 16 | primary volume descriptor (type 1) | unchanged |
| 17 | Joliet supplementary descriptor (type 2), or the El Torito boot record (type 0), or the set terminator (type 255) | the Sprinter boot sector; its first byte `S` (`#53`) is an unknown descriptor type |
| 18 | terminator in a Joliet image | unchanged |

So a Sprinter boot CD and a PC-bootable (El Torito) or Joliet CD exclude each other in one image. An image made
with Joliet keeps a valid primary file system when sector 17 is overwritten (host tools that read only the primary
tree, the CD utility CDX and the DSS CD file system read the primary descriptor at 16); the Joliet names are lost.
Readers that stop at an unknown descriptor type would also stop there.

Worked example (how the test image of section 6 was made, macOS; any ISO tool works the same way):

```bash
hdiutil makehybrid -iso -joliet -default-volume-name CDBOOT -o base.iso folder/   # 16 PVD, 17 Joliet, 18 terminator
python3 - <<'EOF'
d = bytearray(open("base.iso", "rb").read())
boot = bytearray(2048)
boot[0:12] = b"Starting...\x00"
boot[12:22] = bytes([0x32, 0x02, 0x88,    # LD (#8802),A   the drive code
                     0x3E, 0xAA,          # LD A,#AA
                     0x32, 0x00, 0x88,    # LD (#8800),A   a marker
                     0x18, 0xFE])         # JR $
d[17 * 2048:18 * 2048] = boot
open("cdboot-test.iso", "wb").write(d)
EOF
```

## 5. What is missing for a real system CD

- **No DSS loader for a CD.** The DSS boot loader (`Estex-DSS` `BOOT/DSSBOOT.ASM`, master `ae08ad9`, 2026-05-18 and
  the `beta_cdfs` branch) reads FAT boot parameters only; it has no ISO 9660 path. A CD loader would have to find
  `SYSTEM.DOS` / `SYSTEM.EXE` in the ISO tree (the primary descriptor's root directory record at offset `#9C`, the
  root extent at `#9E`, its length at `#A6`) and load them with `DRV_READ`.
- **The DSS CD file system** (`DSS/FS/CDFS.ASM`, the CD driver in `DSS/drivers/media/ata_atapi-drv.asm`) is in the
  DSS master tree; the `beta_cdfs` branch (2026-07-18, "not working yet, BigDir to finish") carries the newer work.
  Released DSS 1.71.57 (the MAME-pack disk) has no CD drive letter.
- **No published bootable CD.** The Sprinter CD 2025 (`sprinter-cd-2025.iso`, ISO 9660 + Joliet, 27.06.2025) has the
  Joliet descriptor at sector 17 and does not boot.

## 6. Verified on the emulator (2026-10-04)

Machine: `SPRINTER`, BIOS 3.06 Hotfix 2, the MAME-pack hard disk on the primary master, the test image of section 4
on the primary slave (`device: cdrom`), CMOS `#10` = `03`, `#3F` = `#F2`, reset:

- the start-up screen: `Detecting IDE Primary Slave ... UNREAL-NG CD-ROM`, then
  `Boot from CD-ROM Primary IDE Slave OK`;
- the loader ran: `#8800` = `#AA`, `#8802` = `#C1` (the primary slave's drive code), PC = `#8014` (its `JR $`),
  SP = `#8000`, IM 1, interrupts off, `#8000-#8015` = the sector's first bytes.

The CD itself, with BIOS 3.06 Hotfix 2, DSS 1.71.57 and CDX 1.02.1 (2025, on the MAME-pack disk as
`C:\DSS\CDX.EXE`; switches `-1`-`-4` pick the IDE position): `LIST` matches the ISO's directories, `COPY` copies files
byte for byte, `OPEN` ejects (the media slot empties), a new disc is read after its UNIT ATTENTION. With the tray
open CDX prints `Can't find CDROM device N`: it asks the BIOS (`DRV_DETECT`) and treats every error except UNIT
ATTENTION as "no device"; the drive answers NOT READY, medium not present (`02/3A/02`), as a real drive does.

## 7. Open

- A CD boot loader that starts DSS from the CD (section 5), and a bootable system CD built with it.
- An automated emulator test of the boot path (the section 6 run as a test with a generated image).
