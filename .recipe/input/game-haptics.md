# Recipe: finding a game's haptic triggers and writing its profile

Goal: make a game's controller vibrate on its own events (a jump, a landing, a hit, a lost life), never on its sound
([haptics.md §1, §5](../../docs/inprogress/2026-10-17-controller-manager/haptics.md)). You find the code that runs once
per event, prove it runs once per event by play, and write the addresses into the game's profile as probes. Dizzy X
(`testdata/loaders/sna/Dizzy X.sna`, profile `builtin/game/dizzy-x`) is the worked example throughout.

The profile format, selection and bundles: [mapping-and-profiles.md §6, §7](../../docs/inprogress/2026-10-17-controller-manager/mapping-and-profiles.md).
The probe kinds, their cost and the effect library: [haptics.md §5, §6, §7](../../docs/inprogress/2026-10-17-controller-manager/haptics.md).
Coverage in general: [analysis/code-coverage-and-analyzers.md](../analysis/code-coverage-and-analyzers.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. The search is a loop of "run a frame, read the
> coverage", hundreds of calls: a small host-side script over [WebAPI](#webapi) is the practical form for that loop.
> Mute the instance first (`audio_mixer {"action":"mute"}`): the search runs many frames of the game's sound.

## 1. Pick the probe kind

| Event looks like | Probe | Cost while armed ([haptics.md §12](../../docs/inprogress/2026-10-17-controller-manager/haptics.md#12-testing)) |
|:--|:--|:--|
| a routine that starts the event (jump start, death, pickup) | `exec` | none measurable: the PC check at each instruction start, the memory stays on the fast path |
| a counter or flag that changes once per event (lives, energy) | `write` with `when: decreased` / `increased` / `changed` (read at the frame end) | none on the CPU: one byte read per frame |
| a change undone within one frame | `write` with `each_write: true` | none measurable: each write tests one filter bit (the Next: the debug memory path) |
| a continuous value (speed, altitude) | `value` (`word: true` for 16 bits) | none on the CPU |
| a border flash, an AY register | `port-out` with `value` / `mask` | the port path's existing gate |

Prefer `exec`. Take a `write` that reads at the frame end when the game has a clean counter. Use `read` only when
nothing else works: it switches the debug memory path on (+38 % CPU on a 48K).

## 2. Find the candidates: per-frame coverage around the event

Run the game to just before the event, then run one frame at a time and keep the addresses that execute for the first
time. The code that appears exactly at the event is the candidate.

```text
load_software {"path":"/abs/testdata/loaders/sna/Dizzy X.sna","paused":true}
audio_mixer {"action":"mute"}
control_execution {"action":"run_frames","frames":50}
analyze_performance {"action":"coverage_start","clear":true}
control_execution {"action":"run_frames","frames":30}          # the idle baseline: everything that runs anyway
analyze_performance {"action":"coverage_read","max_ranges":0}   # remember this set
type_input {"action":"press","key":"9"}                         # Dizzy X: Sinclair 9 jumps
# then per frame: control_execution run_frames 1 + coverage_read + coverage_start clear,
# print only the addresses not seen before (see the WebAPI loop below)
```

Dizzy X, one jump on the spot: the jump start `#795A` in the logic step that reads the key, its sound routine
`#89EC` one frame later, then every 4th frame the ascent (`#7A01`) and the descent (`#7A7A`), and at the touch-down
new `#7A3A`, `#7A97`. A tick that repeats each logic step (every 4 frames
there) is movement, not an event.

## 3. Read the code: confirm what the candidate does

```text
debug_code {"action":"disassemble","address":"0x7A7D","count":22}
```

Look for what the event changes: a lives or energy counter, a state byte, a sound request. Dizzy X's sound request is
the byte `#767D`: the jump writes 0 (`#795B`), a landing faster than 2 writes 1 (`#7A99`), and the routine at `#89E6`
plays it and writes `#FF` back. **A sound-request byte is a map of every event**: find its other writers by their bytes.

```text
debug_code {"action":"find_bytes","pattern_hex":"32 7D 76","start":"0x4000","end":"0xFFFF","max":32}   # LD (#767D),A
```

Dizzy X gives six writers: jump (0), landing (1), dropping an item (2, `#8FBE`), a coin (3, `#84AC`), the energy bar
(4, `#8794`, every third frame while the bar moves: not an event) and one not identified yet (`#9257`, sound 0 with a
jump's start speed). The same search finds the
callers of a routine (`CD lo hi`, `C3 lo hi`).

## 4. Prove it by play: once per event, nothing in between

For each candidate run the event several times and count the frames it runs in. Keep it only if it runs exactly once
per event and never while nothing happens. Take a screenshot at each hit (`capture_media`) and check that the event
really happened: Dizzy X's `#8B80` comes with "Dizzy fell into the water and drowned. You have lost a life".

Things that bite:

- The game reads only its own joystick: Dizzy X ignores O / P / Space, it plays on Sinclair 6 7 8 9 0 only.
- The fire key may open a menu (Dizzy X's pockets) and swallow the next presses: screenshot before you trust a run.
- After a death the game waits for a key; the respawn may drop the hero into a hazard.
- A snapshot saved mid-run (`snapshot/save` into `scratch/`) makes each try start from the same state.
- An address found by code but not seen by play stays out of the profile; name it in the profile's comment.

## 5. Write the profile

`data/controllers/profiles/games/<machine>/<game>.yaml`: the bindings, the match, the game's `events` and the haptics
rules that play them. The header comment says how the addresses were found and checked, and which candidates were left
out and why. The events are game facts, not only haptics: the `game.events` monitor publishes them for a deck key, a
script or an agent, with no haptic device needed (older profiles write the same sources as `haptics.sources`).

```yaml
schema: 1
kind: play
id: builtin/game/dizzy-x
label: Dizzy X
extends: [scheme/sinclair-1]          # the d-pad and the left stick on 6 7 8 9 0
match:
  machines: [PENTAGON]
  file_sha256: ["0173041bd8c3c9d71d2a46065c8381c5e084e5b8ef931a074788dede4ba2227f"]
layers:
  play:
    south:        { key: "9", label: Jump }
    west:         { key: "0", label: Pockets }
events:
  jump:   { probe: exec, address: "0x795A" }
  land:   { probe: exec, address: "0x7A97" }
  hurt:   { probe: exec, address: "0x847A" }
  died:   { probe: exec, address: "0x8B80" }
haptics:
  rules:
    - { on: jump, play: tick, target: handle.high }
    - { on: land, play: knock, target: handle.low }
    - { on: hurt, play: hit }
    - { on: died, play: life-lost }
```

- `file_sha256` is the SHA-256 of the file the user loads (`shasum -a 256`); releases that share the addresses go in
  one document, a release with other addresses gets its own document in the same file (`---`).
- A paged game qualifies the address: `"page:5:0x5c3a"` ([haptics.md §5](../../docs/inprogress/2026-10-17-controller-manager/haptics.md#5-sources-game-events)).
- Light, frequent events (a jump, a shot) take `tick` on `handle.high`; heavy, rare ones `explosion`, `life-lost`. A
  rule that plays every few frames buzzes all the time: leave such a source out.
- Regenerate the index: `python3 tools/controllers/profile-index.py` (`--check` verifies it).

Check what the emulator chooses for the loaded file:

```text
controllers {"action":"match"}     # = GET /api/v1/controllers/match: matches, profile, haptics_reason
```

Then play the event once and read it back (the GUI instance resolves the profile; each `fires` counts the frames the
event fired on, `recent` lists them in order):

```text
inspect_state {"aspects":["monitor"]}   # MCP: "game events (builtin/game/dizzy-x, frame N): jump 1 (last frame N); ..."
GET /api/v1/monitor/game.events         # REST; the WebSocket topic game.events pushes the same value
```

## 6. Test it

- `core/tests/emulator/io/controllers/gamehaptics_test.cpp`: add a row to `kGames` (the id, one release's hash, each
  source with its address). The test resolves the profile by the hash on a Pentagon and calls each address from a
  stub program: each source must fire alone and play through the mixer.
- When the image is in `testdata/`, add a short play test as `DizzyXJumpsAndLandsOnce` does: load it, press the keys,
  assert the exact list of sources fired (under 50 ms) and the same list from the `game.events` monitor.
- Add the profile's row to the H5 table in [haptics.md §13](../../docs/inprogress/2026-10-17-controller-manager/haptics.md#13-phases-and-open-points).

## WebAPI

The per-frame search loop (Python, standard library only; `BASE` = `http://localhost:8090/api/v1/emulator/<id>`):

```python
import json, urllib.request
BASE = "http://localhost:8090/api/v1/emulator/<id>"
def call(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as r:
        text = r.read().decode()
    return json.loads(text) if text else {}
def coverage():
    out = set()
    for r in call("GET", "/coverage?max_ranges=100000").get("ranges", []):
        start, end = (int(str(r[k]), 0) for k in ("start", "end"))
        out.update(range(start, end + 1))
    return out

call("POST", "/pause")
call("POST", "/coverage/start", {"clear": True})
call("POST", "/run_frames", {"frames": 30})
seen = coverage()                                       # the idle baseline
call("POST", "/keyboard/press", {"key": "9"})
for frame in range(1, 90):
    call("POST", "/coverage/start", {"clear": True})
    call("POST", "/run_frames", {"frames": 1})
    if frame == 4:
        call("POST", "/keyboard/release", {"key": "9"})
    new = coverage() - seen
    seen |= new
    if new:
        print(frame, " ".join(f"{a:04X}" for a in sorted(new)))
```

Other routes the steps use: `GET /disasm?address=0x7A7D&count=22`, `GET /memory/read/0x767D?length=1` (a hexdump),
`POST /snapshot/load {"path": ...}` and `/snapshot/save`, `GET /capture/screen?format=png`, and
`POST /audio/mixer/master {"muted": true}`. Watching the chosen addresses: the same loop with
`coverage() & {0x795A, 0x7A97, ...}` per frame prints the frames each one ran in.
