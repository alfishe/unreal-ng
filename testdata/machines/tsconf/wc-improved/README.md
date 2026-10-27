# Wild Commander Improved: a minimal SD card folder

The two-panel file manager for the ZX Evolution / TS-Conf, as the boot program of a TS-Conf SD card.

| | |
|---|---|
| Source | <https://github.com/andrewinsidelazarev/Wild-Commander-Improved>, `exe/` at commit `f06f9c5bca6d3c284cbe3fe2cc0d970b3633a521` (release v1.11i, 2026-10-01) |
| Author | Andrew Lazarev, on Wild Commander by the TS-Labs team |
| License | MIT (`LICENSE` here, copied from the repository) |

`sdcard/` is the card's root, unchanged from the release:

- `boot.$C` - Wild Commander itself; TS-BIOS runs it with "Reset to: BD boot.$c"
- `WC/wc.ini` - the settings: both panels on drive 1 (SD card on the Z-Controller), 90x36 text mode
- `WC/wc.mnu` - the user menu (its entries name programs that are not here)

The plugins (`exe/WC/*.WMF`) are left out: the panels need none of them. Copy them from the repository to try a
viewer or a player.

Used by ACC-C2, `TsConfBootSd_Test.ComposeWildCommanderListsFilteredFat32Layers` (`core/tests/emulator/machines/tsconf/tsconf_boot_test.cpp`): the folder is
one layer of a composite FAT32 card (docs/inprogress/2026-10-05-media-multisource/).
