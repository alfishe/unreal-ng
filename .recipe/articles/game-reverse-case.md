# Article: Reversing a game into a case: driving unreal-ng over MCP without waste

A game reversed into a *case*: sjasmplus sources that rebuild the original medium bit for bit, every resource in
an editable format, the documents a team would keep, and figures drawn from recordings (routine animations, code
vs screen). The standard, the phases and the shared scripts live in the
`unrealng-reverse-cases` repository, kept next to this one (`GUIDE.md`, `recipes/`, `kit/`; `<reverse-cases>`
below). This article is the emulator side: which call answers which question, what each costs, and
the habits that keep an agent fast and its context small. H.A.T.E. (Vortex, 1989) is the worked example.

Companions: [reverse-engineering.md](../analysis/reverse-engineering.md) (the analysis layers, the review queue,
the gates), [game-reverse-pipeline.md](game-reverse-pipeline.md) (the method in general),
[ttd-reverse-debugging.md](../analysis/ttd-reverse-debugging.md), [ttd-write-journal.md](../analysis/ttd-write-journal.md),
[ttd-gameplay-automation.md](../analysis/ttd-gameplay-automation.md), [_common/transports.md](../_common/transports.md).

## 1. The shape of the work

| Who | Does | Through |
| --- | --- | --- |
| scripts (Python, WebAPI) | capture, the bot, every loop over frames, figures, checks against the recording, builds | `tools/verification/analysis/re_session.py`, the case's `tools/`, the kit |
| the agent (MCP) | questions that need judgement: what is this routine, why is this byte here, is this claim true | `time_travel`, `analysis`, `debug_code`, `capture_media` |

The rule that saves the most: **a question asked more than three times is a script.** "What is at `$C2F9` at
each round start" is a loop over `memory_at` in Python, not twenty MCP calls; the agent reads the script's ten-line
answer.

## 2. Sessions

- **One instance per recording**, launched by `re_session.py capture` or `open` on its own port base (WebAPI, MCP =
  base + 200); `W/session.json` holds its process and ports. Several recordings, several instances. Stop only the
  ones you started (`re_session.py stop --work W`).
- **Mute it** (the capture does); never click the window.
- **Take the control session** before any POST (`re_session.py call` does it); reads need none.
- **The saved recording is the evidence.** `time_travel dump` once, then `open --ttd` for every later question: a
  replay is exact, so every counter, window build and memory read sees the same run, days later.
- **Name the recording and its load next to every frame number.** A fast tape load (the LD-BYTES trap) puts the
  game thousands of frames earlier than a load at ROM speed; frames of two recordings never compare.

## 3. Which call answers which question

| Question | Call | Cost |
| --- | --- | --- |
| what is in memory at a moment | `time_travel memory_at {space:"cpu", frame, tinframe, offset, length}` | one call, any frame, no seek; `tinframe` 0 is the frame's start, a frame alone its end. MCP text shows 256 bytes; read more from a script |
| what changed between two frames | `time_travel memory_diff {space, from_frame, to_frame}` | ranges only |
| what happened in a frame | `time_travel explain_frame {frame, sections:"routines,memory", limit}` | the routines by time, memory changed with its writers, ports, screen: one replay instead of a seek + inspect chain |
| who wrote this byte last | `time_travel find_last {addr}` | at once with the write journal ([ttd-write-journal.md](../analysis/ttd-write-journal.md)), else a frame replay |
| why does this byte hold this value | `time_travel cause_chain` | the chain of writes and reads back to its origin |
| when does an event happen | `time_travel find_event` | the frame; then `explain_frame` on it |
| what is this routine | `analysis explain {address}` | a fact sheet under 1 KB: profile, loops, fields, callers, registers, 24 lines of code |
| what is this region | `analysis query {range}` | the claims, one line each |
| what does this graphic look like | `analysis gallery {address, scale}` | a PNG as an image block: look, never read DEFBs |
| would the game do X if Y | `analysis experiment {spec}` | pokes / keys / registers on a muted clone, checks; the source untouched |
| what did the CPU do, access by access, in a window | `analysis trace {path, from_frame, from_tinframe, to_frame, to_tinframe}` | a file (about 30 bytes an instruction); scripts draw from it, the agent never reads it |
| every byte's reads, writes, executions over a run | an L1 build's `byte_counts` (`re_session.py build`) | a heatmap script reads `l1.json` |

## 4. Habits

1. **Ask the machine, then read code.** "Is this branch taken" is a breakpoint hit count or an experiment, not an
   argument over the listing.
2. **Small answers.** Never print a whole map, listing, findings file, trace or recording. Page the review queue (40
   items), ask `explain` for one address, `limit` every query.
3. **Binary searches over frames, by script.** The moment a depacker returns, the first frame a level is set up, the
   frame a byte first changes: `memory_at` halving the interval, about 16 calls for 45 000 frames.
4. **Bots read the game's own records.** Find the player's position, the objects, the level in memory (one
   `explain` each), then a script steers the joystick from them every frame and records. Random play reaches the
   first screens only.
5. **Figures from traces, not from screenshots.** For a routine: `find_event` / `explain_frame` names a call; one
   `analysis trace` over it; the kit's `routine_anim.py` draws the memory map lit by age, the listing window, the
   registers and the screen, picture by picture; `writers_map.py` colours each screen byte by the routine and the
   instruction that wrote it. No `memory_at` per picture.
6. **Every number from a script, written once.** When a figure, a frame or a count is re-measured, every document
   that gives it changes in the same commit.
7. **A tool gap is fixed in the tool.** A region the analysis cannot name, a recording the trace cannot follow, a
   count that is wrong: a generic rule in unreal-ng with a fixture and a test that fails without it, never a rule
   for this game; the case's WALKTHROUGH lists it.
8. **Write as you go.** The WALKTHROUGH and the documents follow each finding; a finding only in the chat is lost at
   the next context compaction.

## 5. A pass of a game, end to end (H.A.T.E.)

```bash
U=<unreal-ng checkout>; C=<reverse-cases>/hate
cd $U
python3 tools/verification/analysis/re_session.py open --work $C/work/p2 --ttd $C/work/p2/session.ttd --port-base 9790
# the sprite drawer's fact sheet
python3 tools/verification/analysis/re_session.py call --work $C/work/p2 explain '{"address":"EAE3","what":"routine"}'
# one pass of it, access by access, then its animation: the kit traces the window itself and keeps the file in
# $C/work/p2/traces/ (a trace by hand: the `trace` action with a path, then access_trace.py PATH --summary)
python3 <reverse-cases>/kit/routine_anim.py --work $C/work/p2 --window 1001:5336-1001:45336 \
    --listing $C/build/game.lst --view buffer:8000:32x256 --pc EAE3-EBA9 \
    --out $C/docs/img/routines/DrawSprites.gif
python3 tools/verification/analysis/re_session.py stop --work $C/work/p2
```

## 6. Pitfalls

| Pitfall | What to do |
| --- | --- |
| `memory_at` with `frame` alone reads the frame's end | add `tinframe: 0` for its start |
| a depacker reading its input where code later lies makes every byte "read" | build the analysis per stage window; take data reads after the unpack |
| a fast load in the recording | the frames are not the ROM-speed ones; say so beside each frame |
| one copier fills an L1 list and starves the rest | the per-copier cap (fixed in L1); check `l1.json`'s counts when a copy is missing |
| `$` pairs in Markdown prose render as math on GitHub | addresses in code spans |
