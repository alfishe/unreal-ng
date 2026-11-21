# browser-drive - drive NextZXOS's menu and Browser from a script

`drive.py` presses the keys a person would press in NextZXOS (SPACE on the welcome page, `B` for the Browser, `H` + a name to find an
entry, ENTER to open or run it) through the WebAPI keyboard endpoints, and waits for the machine to be idle between steps. It
turns "boot the real firmware, find a program on the card, run it" into one command - the way to reproduce, in a regression
script or a bug report, what a user does in unreal-qt.

```bash
# a NEXT machine on a card (the config's [NEXT] SdCard + [ROM] NEXTBOOT, see data/configs/next/unreal.ini) is running
tools/machines/next/browser-drive/drive.py boot space browser go:tests go:base go:copper enter
tools/machines/next/browser-drive/drive.py status        # CPU clock, DivMMC mapping, MMU slots
```

| Step | Does |
|:--|:--|
| `boot` | waits for the first idle (NextZXOS waiting for a key) |
| `space` | SPACE: past the welcome page, or into the menu's More |
| `browser` | `B`: the Browser (it opens in the directory it was last in) |
| `go:<name>` | `H` search, type, ENTER, ENTER: opens a directory, runs a file (`.nex`, `.snx`, `.sna`, `.tap` as the Browser's table says) |
| `enter`, `break` | one key |
| `status` | prints the machine report |

The navigation itself - the keys, hold frames and waits of every step, the idle loop (`HALT` at `#0C8F`), the Browser's order
(`.` and `..` except at the card root, then the names case-insensitively), the search rule (`H` + the shortest unique prefix of the
name's letters, digits, spaces, `.`, `_`, `-`, else counting down) and the run steps per extension (`.sna` ENTER, the Snapshot
Loader page, ENTER; `.tap` SYMBOL SHIFT + ENTER, the TAP loader, `N`; the rest ENTER) - is one data file,
[`testdata/machines/zxnext/browser-navigation.json`](../../../../testdata/machines/zxnext/browser-navigation.json), read through
`navigation.py`. The same file drives the frame-counted [`regress.py`](../regress/README.md) and the C++ `NexBrowserRun_Test`
(`core/tests/loaders/nex/nexbrowser_test.cpp`); a step has frame counts for those and `seconds` for the real-time drivers here.

| Script | Does |
|:--|:--|
| `drive.py` | the steps above, one command line (`Machine`: `step`, `reach`, `type_search`, `run_file`) |
| `suite.py` | every program of a card's `tests/` tree (ZXSpectrumNextTests) through the Browser, one fresh boot each, with the board photo |
| `collection.py` | the titles of a games / demos collection on the card through the Browser |
| `navigation.py` | the loader of `browser-navigation.json` and the step runners (real-time and frame-counted) |

The screen checks of the real-time drivers (the Search prompt line that proves typed text arrived, the Browser's header and status
bars) are placed relative to the paper (`drive.paper`), so a 640 x 480 or a 720 x 576 capture both work.

Timing is real emulator time: NextZXOS reaches the menu in about 5-6 s (no turbo needed). The recipe
[`.recipe/machines/next.md`](../../../../.recipe/machines/next.md) shows it with the NextREG journal.

Needs a built, running application with the WebAPI (`--port`, default 8090). Windows: the same steps, the keyboard endpoints are the
same.
