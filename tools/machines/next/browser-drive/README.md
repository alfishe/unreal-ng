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

Timing is real emulator time: NextZXOS reaches the menu in about 5-6 s (no turbo needed). The recipe
[`.recipe/machines/next.md`](../../../../.recipe/machines/next.md) shows it with the NextREG journal.

Needs a built, running application with the WebAPI (`--port`, default 8090). Windows: the same steps, the keyboard endpoints are the
same.
