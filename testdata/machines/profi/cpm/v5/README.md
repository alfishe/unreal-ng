# Profi v5 CP/M disks (Kondor boards 5.0x, BIOS by Micco)

Collected 2026-10-03. Format, boot mechanism and the board split are explained in
[`../README.md`](../README.md). One disk, for the v5 CP/M boot check; it carries the Micco Software "Concurrent BIOS" CP/M
(MicroDOS-compatible, `BOOTK.COM` + `BDOS.BIN` + `BIOS.BIN` + loadable drivers listed in
`CONFIG.SYS`); its boot word is `#5FC4`, so it needs a v5 BIOS (1.x / 2.x).

**How to boot:** insert as drive A, reset into the BIOS main menu ("Основное Меню") and
choose **"Загрузка системы CP/M"**. The BIOS reads sector R=9 of cylinder 0 / side 0, the boot code
reads the directory, loads `BDOS.BIN`, `BIOS.BIN` and the drivers named in `CONFIG.SYS` (user 15),
then runs `AUTOEXEC.BAT`. On the emulated v5 (2026-10-03) it boots to `A>` with its AUTOEXEC output (KEYHELP,
PRSCR, PAUSE), kept by `ProfiBoot_Test.CpmBootsFromTheKondorSystemDisk`.

## `kondor-system-copyk.fdi`

- **Source:** zx-pk.ru attachment 55227, <https://zx-pk.ru/attachment.php?attachmentid=55227>
  (`COPYK.zip`, 314 518 bytes, holds `COPYK.fdi` dated 2009-11-03), posted 2015-12-17 by tank-uk in
  the thread "Подскажите по Профи", page 27:
  <https://zx-pk.ru/threads/25719-podskazhite-po-profi/page27.html>, as an answer to a Profi v3.2 owner
  asking for a CP/M boot disk. The forum also says the original "Copy K" system disks were copy
  protected; this image shows no protection (plain IDs, valid CRCs). Fetched 2026-10-03. Original name
  `COPYK.fdi`, unchanged bytes.
- **CRC32:** `7C95A491`, **SHA-256:** `5426887fec22904a4d6cade258116c53f2010f73bf46a80b328de8a82cfb64fd`
- **Format:** FDI, 80 x 2, 5 x 1024 B (cylinder 0 / side 0: R = 1-4, 9), 825 945 bytes.
- **Board:** v5 (boot word `#5FC4`; texts "KONDOR+ ... v 4.0", "MicroDOS + Concurrent", Micco
  Software 1992-1993). The v3.2 owner it was offered to reported two weeks later that CP/M "boots
  and works", but did not say which BIOS ROM his board had; per the ROM code a Kramis V0.2 / V0.3
  BIOS cannot start this boot sector.
- **CONFIG.SYS:** `DOSBIOS1 / DSKKE9 / EDKP / DSPK / DSPE80F PROF80 / KBDK1 / LSTP2 KOI8 / TIMER4 KM`.
  Every driver is on the disk (user 15), and `PROF80.FNT` too; there is no `KOI8.*` file (see the note
  on `LSTP KOI8` in [`../README.md`](../README.md)).
- **AUTOEXEC.BAT:** `KEYHELP`, `PRSCR`, `PAUSE`, `MOUSK2`, `CKI` (plus comment lines).
- **Contents (about 100 files):** Turbo Pascal 3.02A (`TURBO.COM`, overlays, docs `TUR.DOC`,
  `TURBO1-3.DOC`), Microsoft M80 / L80, `PIP`, `SID`, `ZSID`, the `ME` editor, `PW`, the CK
  commander, Micco utilities (`DSPINST`, `EDSKINST`, `FONT`, `PRSCR`, `ZXPRINT`, `MOUSK2`), games
  (`FOREVER`, `CTETR`, `SWIFT1/2`), fonts (`STD`, `STD80`, `PROF`, `PROF80`, `IBM`, `IBM2`), Profi
  information texts (`PROFINFO.##0-2`, `KIIAINFO.#1-6`, `PROFI+.TXT`, `MISTAKE.TXT`).
