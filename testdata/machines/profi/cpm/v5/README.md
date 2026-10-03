# Profi v5 CP/M disks (Kondor boards 5.0x, BIOS by Micco)

Collected 2026-10-03. Format, boot mechanism and the board split are explained in
[`../README.md`](../README.md). All three disks carry the Micco Software "Concurrent BIOS" CP/M
(MicroDOS-compatible, `BOOTK.COM` + `BDOS.BIN` + `BIOS.BIN` + loadable drivers listed in
`CONFIG.SYS`); their boot word is `#5FC4`, so they need a v5 BIOS (1.x / 2.x).

**How to boot (all three):** insert as drive A, reset into the BIOS main menu ("Основное Меню") and
choose **"Загрузка системы CP/M"**. The BIOS reads sector R=9 of cylinder 0 / side 0, the boot code
reads the directory, loads `BDOS.BIN`, `BIOS.BIN` and the drivers named in `CONFIG.SYS` (user 15),
then runs `AUTOEXEC.BAT`. All three boot on the emulated v5 (2026-10-03): `kondor-system-copyk.fdi` to `A>`
with its AUTOEXEC output (KEYHELP, PRSCR, PAUSE) - kept by `ProfiBoot_Test.CpmBootsFromTheKondorSystemDisk`;
`hc-1.03-system.fdi` into the HC file manager; `dn-cki-jazz.fdi` into its DN / CK / Jazz boot menu.

| File | What it is |
|:--|:--|
| `kondor-system-copyk.fdi` | the Kondor "Copy K" system disk: the full system plus the bundled software |
| `hc-1.03-system.fdi` | a clean, minimal system disk with the HC file manager and Write 3 |
| `dn-cki-jazz.fdi` | an application disk: Dos Navigator, CK, the Jazz shell, a boot menu |

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

## `hc-1.03-system.fdi`

- **Source:** <https://yadi.sk/d/Py_4jeNmWOgBvA> (`HC1.03.pro`, 819 200 bytes), linked from the
  article "Обзор файловых менеджеров на компьютере «Profi»" by tae1980 on Habr (2024):
  <https://habr.com/ru/articles/837392/>. Fetched 2026-10-03.
- **Original:** `HC1.03.pro`, CRC32 `F11A7C14`, SHA-256
  `0a43cf82caa63a33393ca883a96628e9d570dce04cb46ad2c79274e1bdfcf971`. A `.pro` file is a raw sector
  dump (80 x 2 x 5 x 1024, cylinder-major, sides interleaved) that unreal-ng does not load, so it was
  converted to FDI with the repository's `tools/diskconverter` package: every sector in order, R = 1..5,
  except the fifth sector of cylinder 0 / side 0, which gets R = 9 as on the sector-level images
  (`CPM.UDI`, `COPYK.fdi`). Reading the FDI back gives the `.pro` bytes exactly.
- **CRC32:** `7E4590E4`, **SHA-256:** `e18dbd52422fed6e61f073de19aee442ccf7947e61bd31099f94541a8370d017`
- **Format:** FDI, 80 x 2, 5 x 1024 B (cylinder 0 / side 0: R = 1-4, 9), 825 934 bytes.
- **Board:** v5 (boot word `#5FC4`).
- **CONFIG.SYS:** `DOSBIOS1 / DSKKE9A / EDKP2 /F / DSPK STD / DSPE80F STD80 / KBDK1 / LSTP2 KOI8 /
  TIMER2 /3`. All drivers and both fonts (`STD.FNT`, `STD80.FNT`, user 15) are on the disk; no `KOI8.*`.
- **AUTOEXEC.BAT:** `PCMSMOUS`, `HC`.
- **Contents (27 files):** the system (user 15: `BDOS.BIN`, `BIOS.BIN`, `COME1.DRV`, `DOSBIOS1.DRV`,
  `DSKKE9A.DRV`, `DSPE80F.DRV`, `DSPK.DRV`, `EDKP2.DRV`, `KBDK1.DRV`, `LSTP2.DRV`, `TIMER2.DRV`, two
  fonts), `BOOTK.COM`, HC 1.03 file manager (`HC.COM`, `HC.HLP`, `HCOM.*`), `WRITE3.COM`, `M80R.COM`,
  `L80.COM`, `SEA.COM`, the `PCMSMOUS` mouse driver. The smallest complete system found.

## `dn-cki-jazz.fdi`

- **Source:** <https://yadi.sk/d/94StiATY-7cH1Q> (`DN, CKI, JAZZ.pro`, 819 200 bytes), linked from
  the same Habr article <https://habr.com/ru/articles/837392/>. Fetched 2026-10-03.
- **Original:** `DN, CKI, JAZZ.pro`, CRC32 `5092688B`, SHA-256
  `2b0f1b57d1cb4e8b39b4ca00e7fdb26c3b84d262db4191e5be700ec0e5f116d8`; converted to FDI as above.
- **CRC32:** `DFCD847B`, **SHA-256:** `368d0e17fbd9af55b1aea8c04a610a26557a04d14ac5ee4bcf77f745900d4896`
- **Format:** FDI, 80 x 2, 5 x 1024 B (cylinder 0 / side 0: R = 1-4, 9), 825 934 bytes.
- **Board:** v5 (boot word `#5FC4`).
- **CONFIG.SYS:** `DOSBIOS1 / DSKKE9A / EDKP2 /F / DSPK STD / DSPE80F STD80 / KBDK2 /N / LSTP2 KOI8 /
  TIMER2 /3`. All drivers and fonts are on the disk; no `KOI8.*`.
- **AUTOEXEC.BAT:** `MOUKEMPS`, `MENU BOOT.MNU` (a boot menu that starts DN, CK or Jazz through
  `DN.BAT`, `CKI.BAT`, `JAZZ.BAT`).
- **Contents (111 files in users 0-3 and 15):** Dos Navigator for CP/M (user 1), the CK commander
  (user 3), the Jazz 4 graphical shell with its pictures (user 2), `WRITE.COM`, `M80R` / `L80`.

## Other v5 disks seen but not placed

- `Adj GS.pro` (zx-pk.ru attachment 73360, <https://zx-pk.ru/attachment.php?attachmentid=73360>):
  the ADJ music player with General Sound modules plus Windows 1.2; same system and `CONFIG.SYS` as
  `hc-1.03-system.fdi`. Kept with the originals outside the repository.
- `001 GRF.pro` and `ReadFile.pro` from the Habr author: programming examples on the same system.
