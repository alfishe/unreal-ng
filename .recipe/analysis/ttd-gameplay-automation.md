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
