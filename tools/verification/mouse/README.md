# Mouse: a cursor to see it, and a stroke recorder to measure it

Two small tools for checking what a machine's Kempston mouse receives, whatever drives it: the host mouse, a pad's
trackpad or a touchscreen (controller manager, [mapping-and-profiles.md](../../../docs/inprogress/2026-10-17-controller-manager/mapping-and-profiles.md)),
or the automation planes ([.recipe/input/mouse.md](../../../.recipe/input/mouse.md)).

| File | Purpose |
|---|---|
| `cursor.asm` | A mouse driver in miniature: reads `#FBDF` (X) and `#FFDF` (Y) once a frame, takes the difference of the 8-bit counters as the travel, keeps its own position (clamped to the screen) and draws a cross with XOR plotting. The buttons (`#FADF`, active low) colour the border: left red, right blue, both magenta |
| `build.sh` | Builds `build/cursor.sna` (48K snapshot, starts at `#8000`) with sjasmplus: on `PATH`, or `SJASMPLUS=/path/to/sjasmplus` |
| `mouse-strokes.py` | Polls the counters of a running emulator at about 100 Hz through the WebAPI, unwraps them and prints one line per stroke (motion, then 300 ms of none): net travel, path length, biggest step per poll, direction reversals |

## Quick start

```bash
tools/verification/mouse/build.sh
# A running Kempston-mouse machine (Pentagon, 48K, ...): load the snapshot, the program polls the mouse at once
curl -s -X POST localhost:8090/api/v1/emulator/$ID/snapshot/load -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/tools/verification/mouse/build/cursor.sna\"}"
curl -s localhost:8090/api/v1/emulator/$ID/mouse/status | jq .device.in_use        # true: a program reads the mouse
curl -s -X POST localhost:8090/api/v1/emulator/$ID/mouse/move -H 'Content-Type: application/json' -d '{"dx":40,"dy":-30}'
tools/verification/mouse/mouse-strokes.py $ID 30      # then swipe a pad, a touchscreen or move the mouse
```

A white cross shows on the black screen and follows the mouse; a move of `dx` 40, `dy` -30 goes 40 pixels right and
30 down (`dy` > 0 is up for the mouse). In the Qt app the first click on the picture captures the mouse (a program
reads it now), the release key (`Ctrl+Esc`) lets go.

## What to look for

- **No jumps:** a stroke's biggest step stays small for a slow gesture; the cross never leaps when a finger lands or
  lifts.
- **Relative, not positional:** where a finger rests or lands means nothing; a stroke's net travel follows the
  gesture's travel, `mouse-strokes.py` shows `reversals` of 0 for a straight one.
- **Speed:** a pad's right-trackpad swipe across its width moves the cross about 140 pixels (`scale: 72` of the Steam
  Deck profile), the right stick at full deflection 8 pixels a frame.

## Steam Deck

The Deck's WebAPI is on its own port 8090 (Game Mode runs the AppImage through Steam: `steam -ifrunning
steam://rungameid/<id>`, see [.recipe/testing/steamdeck.md](../../../.recipe/testing/steamdeck.md)); copy the snapshot and the script over
with `scp` and run `mouse-strokes.py` there.
