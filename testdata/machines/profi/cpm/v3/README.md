# Profi v3.2 CP/M disks (Kramis BIOS V0.2 / V0.3)

Collected 2026-10-03. Format, boot mechanism and the board split are explained in
[`../README.md`](../README.md).

No image of the Kramis-era system itself (SP-DOS / "Profi-DOS", the MicroDOS the Kramis BIOS was
made for) was found; see "Not found" below. The disk here is a third-party CP/M whose boot sector
fits the Kramis BIOS's boot convention, so it is the v3 test disk until an SP-DOS image turns up.

## `klug-cpm-2.3.td0`

- **Source:** <https://vtrd.in/system/KLUGCPM.zip> (95 227 bytes, holds `KLUGCPM.TD0` dated
  2019-08-26), listed on <https://vtrd.in/system.php> as "Klug CP/M v2.3 (for Profi) by Klug'95".
  Fetched 2026-10-03. Original name `KLUGCPM.TD0`, unchanged bytes. Background: the zx-pk.ru thread
  "Klug CP/M", <https://zx-pk.ru/threads/30822-klug-cp-m.html>.
- **CRC32:** `F3BCF5B4`, **SHA-256:** `6a4ff261eb5b3e7be16bf3f6c6eead96db15add8f6a3dedbad97b7210951b1a3`
- **Format:** Teledisk (TD0, version 2.1, not compressed), **82** cylinders x 2 sides, 5 x 1024 B
  (cylinder 0 / side 0: R = 1-4, 9). The first two cylinders hold the system (boot sector, BIOS, CCP,
  BDOS, drivers); the CP/M directory starts at cylinder 2 / side 0.
- **Sign-on:** "CP/M BIOS Ver 2.3, Michael Markowsky (C) 1995", "53K CP/M Ver 2.2, Digital Research
  (C) 1979", then a configuration report ("Memory Size", "Physical Drive(s)...A:", "Phantom Disk...
  D:", "RAM Disk...E:", "Printer Interface") and the question "RAM Disk E: format?".
- **Board:** written for "Pentagon 128 & Sinclair PROFI" (`ZXCPM.DOC`): any Profi with 128 KB or more,
  and a Pentagon 128 with one extra port bit (`#DFFD` bit 4 switches the ROM out, which every Profi
  already has). Its boot word is `#5D25` (offset 0 of the sector), inside the 288 bytes the Kramis
  BIOS copies, so it boots from the Kramis V0.2 / V0.3 menu and from the v5 BIOS alike. On a machine
  without the ROM switch it prints "Can't turn off ROM. Toggle RAM/ROM switch and press any key".
- **How to boot:** drive A, Kramis menu entry **"Profi-DOS"** (on a v5 board: BIOS menu "Загрузка
  системы CP/M"). Answer `Y` to "RAM Disk E: format?" after a cold start. On the emulator (2026-10-03) it boots to its sign-on and
  the "RAM Disk E: format?" question on the v5 (BIOS 2.0) and on the v3 with the **Kramis V0.3** ROM
  (`data/rom/profi/kramis-v03.rom`, TR-DOS 5.04T; `Profi3Boot_Test.KlugCpmBootsFromKramisV03`).
- **Needs TR-DOS 5.04T or later:** the boot sector reads the system tracks through the TR-DOS ROM. With Kramis V0.2
  (TR-DOS 5.03, the `PROFI3` default) TR-DOS switches to double stepping on this 5 x 1024-byte disk, every read
  lands on a doubled cylinder and ends in Record Not Found, and the half-loaded system crashes - the same with V0.2
  on a v5 board, while the v3 board with BIOS 2.0 (TR-DOS 6.08) boots it. Traced on the emulator (the FDC follows
  TR-DOS's own seek / track-register writes); a property of the software, not of the board.
- **Contents (18 files):** `README`, `ZXCPM.DOC` (system description, console codes, the Pentagon
  modification), `CPMUTIL.DOC`, `SYSTEM.BIN` + `SYS.COM` (write the system to a new disk:
  `SYS SYSTEM.BIN A:`), `FORMAT.COM`, `SAMF.COM`, `UNIBOOT.COM`, `PIP`, `STAT`, `SUBMIT`, `POWER`,
  `E63` editor, `MF3`, `LHA`, `UNZIP`, `XLAT` (Profi CP/M utilities, PSW Soft and Profi ltd, 1994).
  No `CONFIG.SYS`: the drivers are built into the system tracks.

## Not found

- **SP-DOS / Profi-DOS** (the Kramis MicroDOS, "Sinclair Profi MicroDOS"): no disk image found on
  vtrd.in, zx-pk.ru (Profi forum threads 11582, 14599, 17911, 21356, 21644, 25719, 26671, 32155,
  35561), Habr, zxart.ee, or GitHub. Its manuals exist (three PDF scans, "Описание ОС SP-DOS", linked
  from <https://habr.com/ru/articles/837664/>). A big Klug BBS archive (2005, 581 MB, linked from the
  Klug CP/M thread) was not downloaded and may hold more.
