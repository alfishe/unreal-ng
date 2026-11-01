# Recipe: TTD for Gameplay — Automated Runs, Event Discovery, Gameplay Tests

Goal: drive a game from a script, record it, and find out from the recording *where in the game's code and memory a
gameplay event lives* (crash, jump, pickup, death, level end). The same loop gives the checks for an automated
gameplay test and the checkpoints for a speedrun script. Worked on ATV Simulator (Z80 snapshot, Pentagon) while writing
its haptic profile ([games/pentagon/atv-simulator.yaml](../../data/controllers/profiles/games/pentagon/atv-simulator.yaml)).

Preconditions: [ttd-recording.md](ttd-recording.md) (start / stop / seek), [ttd-reverse-debugging.md](ttd-reverse-debugging.md)
(find-last, reverse-continue, coverage). Everything below is WebAPI (a host-side Python pipeline, the allowed case in
[_common/transports.md](../_common/transports.md)); `time_travel` has MCP actions for seek, find_last, reverse_continue
and the coverage queries. `memory-at` / `memory-diff` are WebAPI verbs: check `tools/list` before assuming an MCP twin.

## 1. Start the instance deterministic and silent

- Launch headless (`QT_QPA_PLATFORM=offscreen`, own `UNREAL_WEBAPI_PORT`) and **mute at once**:
  `PUT /emulator/{id}/audio/mixer/master {"muted": true}` before loading or running anything (a gap of seconds is
  audible; the owner objects). Do it inside the launch helper, not "after the first screenshot".
- **Pause right away** (`POST /pause`) and move the machine only with `POST /run_frames {"frames": N}`. A running
  emulator advances in wall-clock time between your API calls (screenshots included), so a scripted "walk 40 frames,
  jump" lands differently on every run. Paused + `run_frames` makes a script replay bit for bit.
- Keys: `POST /keyboard/press {"key":"d"}`, `run_frames`, `/keyboard/release`. A held key across `run_frames` works.
- Close the loop on the screen, not on frame counts: `GET /capture/screen?format=png&area=screen`, then test a
  few pixels. ATV: "walk right until the rider's head pixels pass x = 90, then jump" mounts the quad every time;
  a fixed number of frames did not.

## 2. Record the run, in either clock

TTD records both a stepped run and a real-time one (`POST /ttd/start`, drive, `POST /ttd/stop`; the history stays
browsable, `state: idle`). Record **in real time (`/resume` + held keys) when the question is about probes**: exec and
write probes of the haptics profile fire only in real time, and a stepped run tells them nothing. Record stepped for
everything else; it is fast (1250 frames in about 7 s wall-clock).

## 3. Find the event, five moves

1. **Rare code.** `GET /ttd/coverage/scan?from_frame&to_frame&kind=executed&addr_from&addr_to&phys_page=0&limit=1`
   answers *counts and first/last match*, not the frame list. Scan the game's code in 64-byte blocks and keep blocks
   with a low `matching_frames`: a crash handler runs in 1 frame of 1250, the main loop in all of them. Without
   `phys_page` the address aliases across pages (offset within the 16 KB page); pin the page.
   Re-scan a rare block in 6-frame windows to see *when* it runs; line the windows up with your input log.
2. **Which instruction.** `POST /ttd/reverse-continue {"pcs":[...]}` with every address of the block lists the exact
   PCs that ran and the frame of each hit; walk back with `seek` to the end between calls.
3. **Which variable.** `POST /ttd/memory-at {"frame","space":"ram","offset","length"}` for the same span at a handful
   of frames (before, during, after) and diff: a state byte shows as a few distinct values. Offsets are physical
   (48K on Pentagon: `0xC000..` is page 0 offset 0, `0x8000..` page 2, the screen page 5). Sample every 3 frames
   across the run and print only the *transitions* per byte; a speed byte climbs 1..21, a state byte steps.
4. **Who writes it.** `POST /ttd/find-last {"addr","access":"write","before_frame"}` walks backwards one write at a
   time (`before_frame` + `before_tin` = tinframe-1 of the last hit). One writer PC for a byte = it is the state
   machine; many = a copy loop. Print value *changes* only.
5. **Check meaning.** `seek` to the frames from 1-4 and screenshot. A byte that looks like an event can be a sprite
   phase (ATV `#D73C`: the same value meant different things in a hop and in a crash). Believe an exec address that
   shows the same signature in two independent recordings (ATV `#E072`: crash into a cactus, then into a rock).

`GET /ttd/position` gives the current frame and `first_frame`; a session recorded after `mount` starts at the frame
the game was at, not 0 (ATV: frame 827), so scan windows use `first_frame..session_end`.

### Shortcut for beeper games: the sound queue is the event map

Most 48K games play their effects through one routine and a request byte. `POST /ttd/port-events {"event":"beeper"}`
names the single PC that writes port `#FE` (Super Off Road: `#EB5A`); the routine reads an effect-number byte; `find-last`
on that byte lists every writer with its value; if the writes go through a queue (a caller stores the number, one
copier moves it) take the byte *before* the copier. The writers of the queue are the game's own event sites, with
numbers: `find-last {"addr": queue, "access": "write"}` per hit, dedupe by `(pc, value)`, then look at the frames on
the screen (a "20,000" caption at a money-bag write). For "whose event is it" read `IX` / `IY` at the hit
(`reverse-continue {"pcs":[pc]}` then `GET /registers`): car games keep one struct per car, the player's is the first.

### Shortcut for keyboard games: find the key reader first

`POST /ttd/port-events {"event":"key"}` names the PC of the game's own `IN` (Turbo Esprit: `#F3B3`, inside the key-test
function `#F3A6`). Search the RAM for `CD lo hi` (calls of that function) and read the callers: the code after each call
sets the input flags of the player record, and `seek {frame, tinframe}` to a hit plus `GET /registers` gives IX. Diff the
record across phases (idle / accelerate / brake / fire) to get the speed byte, then `find-last` for its writers. A branch
experiment (`seek`, `POST /ttd/resume`, `POST /pause`, press keys, `run_frames`) answers "what does this key change".
A `value` probe reports level = value x scale / 255, so scale = 255 / max value. Z80 snapshots of many games are on
archive.org (`zx_*` items) when a tape image is damaged.

### Shortcuts that found racing-game events (2026-10-28 / 29)

- **Key-tap coverage diff** (fire, jump, any button): record a stepped run, 150 frames with the button up, then 6 taps
  (press, 4 frames, release, 30 frames). Scan 32-byte blocks with `coverage/scan` (`phys_page` 5 / 2 / 0) and keep the
  blocks with 0 matches in the quiet window and 3 to 8 in the tap window: the match count is the tap count and the
  first block of the run is the spawn routine (Overlander `#5FE3`, Badlands `#B877`). `reverse-continue` + `seek` +
  `/registers` then names the actor (IX = the player's record).
- **HUD digits against RAM**: seek to 5 frames where a counter shows 9, 8, 7, 6, 5, read all 8 pages (`memory-at`,
  `space: ram`, `offset` 0..0x1C000 in 16 KB pieces: the call refuses 0x20000 at once) and keep the address whose byte
  steps the same way (Hard Drivin' "OFF ROAD n": `#951F`, its flag `#951E`).
- **Speed drop = crash?** `find-last` on the speed byte with `changes_only`, print drops over about 12 and their writer;
  then screenshot the frames. A drop by a generic writer (a sum of vectors) is physics: look for the flag it follows
  (a crash sets a state byte, the speed collapse comes after). Test Drive II: the drop `#88DD` is the per-car reset loop
  after the collision flag `#FCE6` is set at `#88C7`; Cisco Heat: `#9A8F` zeroes the speed after a hit, `#9137` (sound
  set-up, 4 runs in 3700 frames) is the clean event.
- **128K pages**: `memory-at` with `space: ram` for the whole 128 KB, `cpu` space only for the latest recording;
  `phys_page` 0..7 for the `#C000` window.
- **Messages name the incidents** (Test Drive II: ticket, blown engine, out of gas): search the RAM for the game's text
  (bytes with bit 7 clear, `[A-Za-z ]{8,}`), then for `21 lo hi` (`ld hl,text`) and for the `CD`/`C3` that reach it; the
  address of the `ld hl,text` is a clean exec probe, once per incident. Check it on the screen of the frame that fires.
- **A HUD number on the screen is a variable you can chase**: sample the screen page every 10 frames, list the 8x8 cells that
  change when the number steps, then compare *all RAM* at the same samples and keep the byte that changes at exactly those
  steps (Super Hang-On TIME: `#C4AE` tens / `#C4AF` units, decremented by `#C2F5`; the distance word `#C4AA` and the road
  segment counter `#A724` came from "monotone in every sample" over the same dump). Poking such a counter (`PUT /memory/{addr}`)
  works as a shortcut, but a counter that the game reads for pointers (the segment index walks a course table) desyncs the
  road: poking `#A724` through 46 segments did not run the time-extension code, so the checkpoint is not reachable that way.
- **Stale instance trap**: `launch` attaches to a live instance on the port instead of starting a new one, so a "new" run
  can begin in a game-over state. Check what answers the port before a run, and kill the previous instance by its path
  (`pkill -f "<worktree>/cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt "`), not only by port: a helper
  module that sets its own port (one session lost an hour to a library that rewrote `PORT` on import) leaves instances on a
  port nobody looks at.
- **Snapshots**: a denied or damaged tape is often on archive.org as `zx_*` `.z80`; store it in the collection with its
  source in the README.

## 4. Turn the finding into a check

- A probe (`exec` address, or `write` with `when: "=="`) goes into the game profile; `GET /monitor/game.events` returns
  `fires` per event. The test is "scripted run X produces N fires of event E": ATV, 15 s of holding D = 6 `bump`, 1
  `crash`, and the screenshot at the end shows the rider on the ground.
- The same counters make a gameplay test: drive the level with the scripted inputs from section 1, read the counters,
  compare with the numbers from the recording (they are the oracle, the recording's `coverage/scan` counts the same
  thing). For death / pickup / level-clear events assert the counter, not the picture.

## 5. Speedrun shape (design, not run here)

A run is `inputs(frame) -> state`. The recording keeps the input events (`input_event_count` in `/ttd/status`), a
`seek` into it plus `resume` drops the recorded future and records again from that point (ttd-recording.md, "From a
past point"). That gives the search loop: bookmark a good state, branch from it with a different input script, keep the
branch that reaches the checkpoint (a probe counter or a memory value from section 3) in fewer frames. Not exercised
in this session: the loop, replaying a saved `.ttd` input stream, and ranking branches. Treat it as the plan to try
first, with the deterministic driver from section 1.

## Pitfalls

- Probes silent in a stepped run: `fires: 0` after `run_frames` is not a bug (see section 2).
- A value probe (`speed`) reports fires per frame; do not assert on its count.
- `memory-diff` over a scrolling game is noise (the scroll buffer changes every frame); diff the variable page only.
- Killing a headless instance: `lsof -ti tcp:<port> | xargs kill`.
- A running instance caches profiles: restart it after editing one (and sync `data/controllers` into the app's Resources).
