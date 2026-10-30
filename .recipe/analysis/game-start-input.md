# Recipe: start a game from its title screen

Most ZX Spectrum games start with one of a few inputs: Space, Enter, `0`, a joystick fire, or a menu digit that picks
the controls followed by a start key. Do not guess them one at a time. Ask the game which inputs it reads, try only
those from one restart point, and measure what each one changed.

Tool: [`tools/gamestart/gamestart.py`](../../tools/gamestart/gamestart.py) (WebAPI, standard library only).

```bash
tools/gamestart/gamestart.py --id <emulator-id>                 # probe, try, report, leave the best state
tools/gamestart/gamestart.py --id <id> --probe-only             # only what the game reads
tools/gamestart/gamestart.py --id <id> --try "key:k,key:space" --try "key:2,wait:25,key:0" --json
```

The machine must stand at the title or menu screen: the game is loaded and waits for input.

## The controls in ports

| Control | Fire | Directions | Port the game reads |
|:--|:--|:--|:--|
| Keyboard (QAOP, Space) | Space, M | Q A O P | half-rows `#FBFE`, `#FDFE`, `#DFFE`, `#7FFE` |
| Kempston | fire `0x10` | right `0x01`, left `0x02`, down `0x04`, up `0x08` | `#1F` (active high) |
| Sinclair 1 (Interface 2, right) | `0` | 6 left, 7 right, 8 down, 9 up | half-row `#EFFE` (A12) |
| Sinclair 2 (Interface 2, left) | `5` | 1 left, 2 right, 3 down, 4 up | half-row `#F7FE` (A11) |
| Cursor (Protek, AGF) | `0` | 5 left, 6 down, 7 up, 8 right | `#F7FE` and `#EFFE` |

The keyboard reads active low. Half-row n (A(8+n) = 0) holds these keys, from bit 0:

| n | Port | Keys | n | Port | Keys |
|:--|:--|:--|:--|:--|:--|
| 0 | `#FEFE` | Caps Z X C V | 4 | `#EFFE` | 0 9 8 7 6 |
| 1 | `#FDFE` | A S D F G | 5 | `#DFFE` | P O I U Y |
| 2 | `#FBFE` | Q W E R T | 6 | `#BFFE` | Enter L K J H |
| 3 | `#F7FE` | 1 2 3 4 5 | 7 | `#7FFE` | Space Sym M N B |

A game that reads `IN A,(#FE)` with A = 0 scans all rows at once ("any key"): every row then counts as read.

## Step 1: what the game reads

One port watch per half-row and one on Kempston, counted for a few seconds without input
([memory counters and watches](memory-counters.md)). A watch with port `0x00FE` and mask `(1 << (8 + n)) | 1` counts
every read that selects row n, whatever the other address bits are.

```bash
B=http://localhost:8090/api/v1/emulator; ID=<id>; J='Content-Type: application/json'
curl -s -X POST $B/$ID/pause
curl -s -X POST $B/$ID/profiler/memory/watches -H "$J" -d '{"name":"kempston","port":"0x1F","mask":"0xFF","values":true}'
curl -s -X POST $B/$ID/profiler/memory/watches -H "$J" -d '{"name":"row3","port":"0xFE","mask":"0x0801"}'   # 1-5
curl -s -X POST $B/$ID/profiler/memory/watches -H "$J" -d '{"name":"row4","port":"0xFE","mask":"0x1001"}'   # 0-6
curl -s -X POST $B/$ID/profiler/memory/watches -H "$J" -d '{"name":"row7","port":"0xFE","mask":"0x8001"}'   # Space
curl -s -X POST $B/$ID/profiler/memory/start
curl -s -X POST $B/$ID/run_frames -H "$J" -d '{"count":150}'
curl -s $B/$ID/profiler/memory/watches | jq '.watches[] | {name, reads}'
```

```python
emu.memory_watch_port("kempston", 0x1F, 0xFF, values=True)
for n in range(8):
    emu.memory_watch_port(f"row{n}", 0xFE, (1 << (8 + n)) | 1)
trace("memory", "start"); emu.run_frames(150)
print({w["name"]: w["reads"] for w in emu.memory_watches()})
```

Reading the answer:

- **A row with 0 reads**: no key of it can start the game. Skip those keys.
- **`kempston` reads > 0**: the game polls the joystick. On `48K`, `128k` and `+2`/`+3` fit the card first
  (`slots plug edge.next kempston`, [joystick recipe](../input/joystick.md)). Without the card the port reads 0 and
  fire never arrives.
- **Only rows 3 and 4**: a Sinclair or Cursor menu. Try `0`, `5`, and the digits.
- **Every row with equal counts**: an "any key" loop. Space does it.

## Step 2: try only those, from one restart point

Save a snapshot (`POST /snapshot/save {"path": ".../restart.szx"}`, falling back to `.z80` or `.sna` on machines
SZX does not cover). Load it before every attempt, so each attempt starts from the same frame. Per attempt:

1. start the memory session, which zeroes the counters;
2. tap the input: `POST /keyboard/tap {"key":"space","frames":4}` or `POST /joystick/tap {"buttons":"fire","frames":4}`;
3. run about 150 frames;
4. read the executed addresses (`GET /profiler/memory/counters?space=cpu&format=binary`, the third `uint32` array)
   and the screen digest (`GET /state/screen/digest?start=0x4000&end=0x5AFF`, field `combined`: the pixels, the
   attributes and the border; the whole of page 5 also holds the game's variables, which change anyway).

Run one attempt without input as well: the baseline. An input started the game when it executed addresses the
baseline never did (the game loop, not the menu loop) and the screen differs from the baseline's. `gamestart.py`
counts both and picks the attempt with the most new code; within 10 % of it, the shorter input.

The most new code, not the shortest input: a key that only opens the next screen of the menu also changes the
screen. Green Beret (`testdata/loaders/tap/greenberet.tap` on a 48K, menu "1 keyboard, 2 Kempston, 3 cursor,
4 Sinclair"):

| Input | New addresses | What it did |
|:--|--:|:--|
| (probe) | | only half-rows `1`..`5` and `0`..`6` read: a digit menu |
| `4`, then `0` | 1080 | Sinclair, fire: the game starts |
| `3`, then `0` | 1077 | Cursor, fire: the game starts |
| `1` | 373 | the key definition screen ("SELECT KEY") |
| `0`, `5` | 20, 24 | nothing: the menu waits for a digit 1-4 |

A machine at the BASIC prompt (`K` cursor) runs new code for any key: the editor prints it. Check the picture
(`GET /capture/screen`) once before trusting the result on a snapshot of unknown state.

The built-in candidates are tried only when the probe allows them:

- Kempston fire;
- Space, Enter, `0`, `5`, `S`, `P`, `1`;
- each menu digit `1`..`5`, then `0`, Space, Enter, `5` or fire, 25 frames apart;
- on an "any key" wait (every half-row read equally): only Space, Enter and `0`.

## Step 3: when nothing starts

- **A longer run** (`--frames 400`): some games fade or play a jingle before the first frame of play.
- **A held key**: some menus react on release only, or need a hold longer than 4 frames (`--tap-frames 10`).
- **A custom key**: the port trace names the exact ports and the code that reads them
  ([port trace](port-trace.md)). Capture while the menu waits, then look at the `#xxFE` reads with a constant high
  byte: that half-row is the game's own key. Add it with `--try "key:<name>"`.
- **The values**: the `kempston` watch's `values`, or a `values` watch on one row, show what the game read. A row
  read that is always `#FF`/`#BF` while you hold a key means the read happens outside the moment of the tap.
- **A redefine-keys menu** (`1` keyboard, `2` define keys): the digit that picks Kempston or Sinclair skips the key
  definition. Prefer it over the keyboard entry (Green Beret above).
- **Another client holds the machine**: the script opens its own automation session (`POST /control/session`) and
  ends it when done; a `409 held_by_other` names the holder.

## Limits

- An attract mode (a demo that plays by itself) runs new code without input. The baseline catches this when the demo
  starts in the baseline as well. A demo that starts only after a timeout shorter than `--frames` breaks the test:
  shorten `--frames`.
- The probe sees only the inputs the title screen polls. A game that polls the joystick only after a menu choice
  shows `kempston` = 0 until then: probe again after the choice.
