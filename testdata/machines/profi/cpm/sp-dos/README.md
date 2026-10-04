# SP-DOS system disk (ZX Profi, BIOS by V. Tereschenko)

SP-DOS is the CP/M 2.2-compatible MicroDOS of the ZX Profi ("CP/M 2.6 for Sinclair PROFI", Sinclair Profi
MicroDOS) with the BIOS by V. Tereschenko, sold by TOO "Profi" (Moscow). The v3's front-panel switch is labeled
"ON/OFF SP-DOS", and the Kramis menu's "Profi-DOS" entry boots it.

## `unicopy-sp-dos.td0`

- **What:** the SP-DOS system disk with UniCopy 2.12 (disk copier), FdFormat 1.0, FdDoctor 1.00 (SP-DOS / TR-DOS /
  MS-DOS formatter and disk doctor, Profi Vision windows with mouse), UniList (printer driver), UniKbd (Sinclair +
  PC/XT keyboard driver), UniBoot (mode switch / reboot) and the SYSCOC / FC8030 console drivers. Its shell is
  "SP-DOS Shell by Michael Markowsky", in hi-res.
- **Source:** KLUG's BBS (FidoNet 2:5020/378), area PROFI, `UNICOPY.ZIP` (197 419 bytes, md5
  `bd737c4244d5fc5e91ef828ecb8a0345`, dated 1995-10-27) member `UNICOPY.TD0`, byte for byte; the BBS archive of
  2005: <https://yadi.sk/d/N_p56RIHWU15Gw> (`klug_bbs.7z`). Fetched 2026-10-03.
- **Size / hashes:** 280 158 bytes, CRC32 `3FBA43B6`, SHA-256
  `2b217a2d7760d6b2533f55e7a92d7d4d9325702945553cb47018d7748388f5f1`
- **Format:** TeleDisk, the Profi CP/M format ([`../README.md`](../README.md)): 5 x 1024-byte sectors, the boot
  sector R = 9 on cylinder 0 / side 0.
- **Boot:** the boot word at `#102` of the boot sector is `#5D25` (offset 0), so it boots from both BIOS families:
  on the v3 from the Kramis "Profi-DOS" entry, on the v5 from BIOS 2.0's "Загрузка системы CP/M". The loader
  sets `#DFFD = #B0` (hi-res, CP/M, RAM at #0000), copies itself to #0000 and reads the system with the VG93 on
  `#1F..#7F`, polling DRQ / INTRQ on the CP/M system port `#BF` and taking the bytes with `INI`.
- **On the emulator (2026-10-03):** boots to the SP-DOS shell on `PROFI3` and `PROFI`
  (`Profi3Boot_Test.SpDosBootsToItsShell`, `ProfiBoot_Test.SpDosBootsToItsShell`). On the v5 it needed the VG93
  to keep its 3.5 MHz clock while the CPU runs at 5 MHz in hi-res: with the CPU clock as its time base the
  controller's time stepped back at every frame boundary, and every read ended in Lost Data.
