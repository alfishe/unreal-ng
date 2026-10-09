# sn-test-card

A folder that unreal-ng presents as a FAT16 SD card (`HostFolderFat`) and boots through the real chain: boot ROM ->
`TBBLUE.FW` -> personality ROM -> NextZXOS. Assembled on 2026-10-09 from this collection:

| Folder | Source |
|:--|:--|
| `TBBLUE.FW` | `firmware/TBBLUE.FW` |
| `machines/next/*.rom`, `menu.def`, `keymap.bin` | `firmware/machines-next/` |
| `machines/next/config.ini` | written for the test: `timing=0` (without a `config.ini` the firmware runs its video-mode test forever) |
| `nextzxos/` | `os/nextzxos/` (without `checksums.tsv`) |
| `sys/` | `os/sys/` |
| `dot/` | `os/dot-commands/` |

The same tree is in unreal-ng `testdata/machines/zxnext/card`.
