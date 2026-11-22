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

## browser-navigation.json

The NextZXOS Browser navigation as data: the keys, hold frames and waits of each step (boot, menu, Browser, search, type, accept,
down, open, up, back), the idle loop (`HALT` at `#0C8F`), the Browser's order (`.` / `..` except at the root, then the names
case-insensitively), the search rule (`H` + the shortest unique prefix of the searchable part, else counting down) and the run steps
per extension (`.sna`, `.tap`, the rest) plus the `.bas` RUN. Read by `tools/machines/next/browser-drive/navigation.py` (drive.py,
suite.py, collection.py, `regress.py`) and by `NexBrowserRun_Test` (`core/tests/loaders/nex/nexbrowser_test.cpp`).
