# Profi CP/M raw image (.pro)

## Overview

The 1024-byte sectors of a Profi CP/M (MicroDOS) floppy one after another, no header: the disk dump the Profi
community passes around and that ZXMAK2, Unreal Speccy and SteinBlume (CP/M Disk Image Explorer) read.

- **Extension**: `.pro` (the extension decides: the format has no signature)
- **Size**: cylinders x 2 sides x 5 sectors x 1024: 80 cylinders = 819 200 bytes (81-83 taken too: 829 440 ...
  849 920; Klug CP/M disks have 82)
- **Loader / writer**: `core/src/3rdparty/unreal-disk/src/loaders/loader_pro.cpp` (`LoaderPRO`)
- **Format id**: `pro` (`FloppyFormats::Probe`, `Medium::Format`)
- **File system**: `cpm` volume, "Profi CP/M" or "Klug CP/M", read and written
  ([disk file systems](../../emulator/design/disk-file-systems.md#profi-cpm-layout))
- **Test image**: `testdata/loaders/pro/ReadFile.pro` ([README](../../../testdata/loaders/pro/README.md))

## Layout

Track by track, cylinder-major, side 0 then side 1 (c0/h0, c0/h1, c1/h0, ...: the CP/M logical track t is cylinder
t / 2, side t % 2), each track its sectors 1..5 in turn. Logical sector `LSN = (cylinder * 2 + side) * 5 + (n - 1)`
where n is the sector's place on the track (1..5).

## Sector IDs

The dump keeps no sector IDs, but the Profi reads one track by its IDs: on cylinder 0 side 0 the fifth sector has
**R = 9**, on every other track the IDs are 1..5. This is no interleave (the physical order differs from disk to disk:
1 9 4 3 2, 9 4 3 2 1, 1 2 3 4 9; the set of IDs does not):

- `FORMAT.COM` (Micco / Kondor) formats with the track table `03 05 01 02 03 04 09` (N = 3, 5 sectors, IDs 1, 2, 3,
  4, 9) for the system track and `03 05 01 02 03 04 05` for the others.
- BIOS 2.0 boots CP/M from cylinder 0, head 0, **sector 9** (`profi.rom` #0861: `LD (IX+3),9 : CALL #07A2`, the
  sector read to #5D25); BIOS 1.0 and Kramis V0.2 / V0.3 alike (`testdata/machines/profi/cpm/README.md`).
- The floppy driver `HDD22.DRV` (#094D) makes the sector number from the CP/M record (record / 8 + 1) and asks for
  R = 9 instead of 5 while the logical track is 0: block 2 (the file `BOOTK.COM`, the boot sector) lies in it.
- Every Profi floppy imaged with its IDs (103 FDIs of `PROFI_DISK.ZIP`, the UDI / TD0 copies) has the IDs 1, 2, 3,
  4, 9 on that track. SteinBlume's author compares the Quorum `.qdi` with it: "QDI походу отличается от PRO только тем,
  что в нулевой дорожке пятый сектор имеет номер 5, а не 9" (zx-pk.ru, thread 11294, 2013-05-28).

CP/M itself leaves the IDs to the BIOS (CP/M 2.2 `SECTRAN` / the DPH's `XLT` translate 128-byte records only; with
1024-byte sectors the BIOS numbers them). So the loader gives cylinder 0 side 0 the IDs 1, 2, 3, 4, 9: with 1..5 the
BIOS finds no sector 9 and does not boot (checked on the emulator with the Kondor system disk).

## Mapping to the model

Every track is a DD MFM track of 6 250 bytes (250 kbit/s): index mark, gaps 80 / 50, five sectors of N = 3 with gap 2
22 and gap 3 54, filler E5. The head byte of an ID is the side.

## Saving

The sectors are written back in LSN order by ID (1..5; 9 for the fifth on cylinder 0 side 0, 5 there taken too)
while the layout is regular: 80..83 cylinders, 2 sides, 5 sectors of 1024 bytes on every track. Otherwise the save
is refused with the reason (export as UDI to keep it). A loaded image writes back byte for byte.

## References

- Алексей Тарасов, "Работа с файлами в ОС CP/M на компьютере «Profi»", Habr, 2024-08-21:
  <https://habr.com/ru/articles/837664/> (`ReadFile.pro`, "образ диска в формате «PRO»"); also
  <https://habr.com/ru/articles/837144/> and <https://habr.com/ru/articles/837392/> (more `.pro` disks)
- SteinBlume, CP/M Disk Image Explorer (`.pro` = "образ CP/M диска Профи"): <https://zx-pk.ru/threads/26454>
- zx-pk.ru, "Просмотр образов CP/M дисков" (PRO vs QDI, the directory from track 0):
  <https://zx-pk.ru/threads/11294>
- CP/M 2.2 Alteration Guide (Digital Research, 1979), `SECTRAN` and the disk parameter header
