# What you can do with a `.ttd` recording

| | |
|---|---|
| **Audience** | Anyone debugging ZX Spectrum software with Unreal-NG |
| **Scope** | Operations available today, on every control surface |
| **Last updated** | 2026-08-21 |
| **Related** | [time-travel-debugging-tdd.md](./time-travel-debugging-tdd.md) (design), [ttd-container-format.md](./ttd-container-format.md) (file layout), [overhead-and-gating.md](./overhead-and-gating.md) (cost) |

A `.ttd` file is a complete recording of a machine: every frame's state, every
memory write, and an index of what each frame touched. Once you have one, the
questions you can ask of it are the subject of this document.

Everything here works on a **loaded file** as well as on a live recording — a
recording made on one machine can be handed to someone else and interrogated
offline.

---

## 1. Getting a recording

```sh
# CLI
ttd record          # start capturing
ttd stop            # stop, keep history browsable
ttd dump session.ttd
ttd load session.ttd
```

Same operations on every surface:

| | Start | Stop | Save | Load |
|---|---|---|---|---|
| CLI | `ttd record` | `ttd stop` | `ttd dump <path>` | `ttd load <path>` |
| WebAPI | `POST /ttd/start` | `POST /ttd/stop` | `POST /ttd/dump` | `POST /ttd/load` |
| Python | `ttd_start()` | `ttd_stop()` | `ttd_dump(path)` | `ttd_load(path)` |
| Lua | `ttd_start()` | `ttd_stop()` | `ttd_dump(path)` | `ttd_load(path)` |

Two things worth knowing before you record:

* **Recording must be stopped before you can scrub.** Seeking while recording
  would overwrite the live state the timeline is still being built from, so it
  is refused with a clear error rather than silently corrupting history.
* **The write journal is optional** (`ttd record --journal` / `--no-journal`).
  It is what makes "who wrote this byte" instant; without it the same question
  falls back to replay. It is also the largest thing in the file — see §9.

---

## 2. Moving through time

```sh
ttd goto 1500            # jump to frame 1500
ttd pos                  # where am I?
ttd back                 # one frame earlier
ttd forward              # one frame later
ttd reverse-step -n 100  # 100 instructions back
ttd s                    # one instruction forward
```

| Operation | CLI | WebAPI | Cost |
|---|---|---|---|
| Seek to frame | `ttd goto <frame>` | `POST /ttd/seek` | 0.2 ms |
| Step back 1 frame | `ttd back` | `POST /ttd/step-back` | ~1 ms |
| Step back 1 instruction | `ttd reverse-step -n 1` | `POST /ttd/reverse-step` | 1.2 ms |
| Step back N instructions | `ttd reverse-step -n N` | `POST /ttd/reverse-step` | 45 ms at N=256 |
| Step back T t-states | `ttd reverse-step --tstates T` | `POST /ttd/reverse-step` | 0.2–4 ms |
| Step forward 1 instruction | `ttd s` | `POST /ttd/step-instruction` | ~1 ms |

Seeking is cheap because a checkpoint is captured **every frame**, so the engine
never replays more than one frame to land anywhere. Stepping backwards by
instruction is the expensive direction: it restores the enclosing frame and
replays forward to the instruction before the current one.

**The screen follows you.** A seek repaints the framebuffer and flushes the
video delay line, so the display shows the frame you scrubbed to rather than
the last one that happened to be presented. Audio is not reconstructed while
scrubbing; it resumes normally on play.

---

## 3. Reverse breakpoints — "how did we get here?"

The canonical time-travel question. You are stopped somewhere; you want the
previous time execution passed through some address.

```sh
ttd reverse-continue --pc 0x8000
ttd rc --pc 0x8000 --pc 0x8100      # several addresses at once
```

```python
hit = emu.ttd_reverse_continue([0x8000, 0x8100])
if hit["matched"]:
    print(f"last executed at frame {hit['frame']}, t={hit['tinframe']}, pc={hit['pc']:#06x}")
```

The emulator runs backwards until one of the given PCs matches, then stops
there — the machine is left at that instruction, so registers and memory are
what they were at that moment, and you can step forward from it.

**Performance.** This is index-accelerated: the coverage index knows which
frames executed which addresses, so the engine replays only candidate frames
instead of the whole recording. On a loaded 300-frame session, 754 ms → 8.3 ms.

**Caveat worth knowing.** The speed-up shrinks as the breakpoint set grows. The
index is keyed by offset-within-page, so each breakpoint address aliases four
Z80 addresses; with ~100 breakpoints nearly every frame becomes a candidate and
the cost converges on the unindexed scan (65 ms vs 76 ms). It degrades, it does
not fall off a cliff.

---

## 4. Reverse watchpoints — "who wrote this byte?"

The other canonical question, and the one TTD answers best.

```sh
ttd find-last --addr 0x5C78 --access write
ttd fl --addr 0x5C78 --access write --value 0    # only when it became 0
ttd fl --addr 0xC000 --access write --phys-page 3
ttd fl --addr 0x8000 --access execute --before-frame 900
```

```python
hit = emu.ttd_find_last(0x5C78, access="write")
# -> {'frame': 412, 'tinframe': 31204, 'pc': 0x8A31, 'value': 7, 'phys_page': 5, 'access': 'write'}
```

The answer names **the instruction that did it** (`pc`), the value written, and
the physical page — everything needed to jump there and look.

### Filters

| Filter | CLI | Meaning |
|---|---|---|
| Access kind | `--access write\|read\|execute\|out` | What kind of access to look for |
| Value | `--value V` | Only accesses that wrote exactly V |
| Writer PC range | `--pc-from A --pc-to B` | Only accesses made by code in that range |
| Physical page | `--phys-page P` | Bank-aware: pin the query to one RAM page |
| Time bound | `--before-frame F --before-tin T` | Search backwards from a point other than now |

**`--phys-page` matters more than it looks on a 128K machine.** A Z80 address
like `0xC000` names different bytes depending on which page is banked in, so
without the filter the answer may be a write to a page you never meant. If you
know the page, say so.

### Speed, and why it differs by access kind

| Access kind | How it is answered | Typical cost |
|---|---|---|
| `write`, `out` | Write journal, scanned backwards | instant |
| `read`, `execute` | Coverage index picks candidate frames, then replay | 0.4 ms |

Reads and instruction fetches are not journaled — recording every one would cost
several times what writes cost — so they are answered by replaying candidate
frames. The coverage index is what keeps that fast: 447 ms → 0.41 ms on a
loaded session.

---

## 5. Watching a memory region

There is no "watch this range and tell me every change" call, but the primitives
compose into one, and the composition is cheap because each query is
sub-millisecond.

**Which byte in a structure changed last:**

```python
addresses = range(0x5B00, 0x5B10)
hits = [(a, emu.ttd_find_last(a, access="write")) for a in addresses]
hits = [(a, h) for a, h in hits if h]
newest = max(hits, key=lambda x: (x[1]["frame"], x[1]["tinframe"]))
print(f"most recent change in the block: {newest[0]:#06x} by PC {newest[1]['pc']:#06x}")
```

**Walking a byte's history backwards** — every write to one address, newest
first, by moving the time bound behind each hit:

```python
def write_history(emu, addr, limit=20):
    frame, tin = None, None
    for _ in range(limit):
        hit = (emu.ttd_find_last(addr, access="write") if frame is None
               else emu.ttd_find_last(addr, access="write",
                                      before_frame=frame, before_tin=tin))
        if not hit:
            return
        yield hit
        frame, tin = hit["frame"], hit["tinframe"]
        if tin == 0:
            if frame == 0:
                return
            frame, tin = frame - 1, 0xFFFF
```

That gives you "this variable was 3, then 7, then 0, written by these three
instructions" — the reconstruction a printf-debugging session is usually
groping for.

**Reading memory as it was at a point in time:** seek there and read normally.
The machine *is* that machine after a seek, so every existing inspection tool —
memory dumps, disassembly, register views — works unchanged.

---

## 6. Counters and statistics

There is no hit-counter API. What exists instead is enough to compute counts
offline, which is often better because you can ask the question after the fact
rather than deciding to count before you run.

**Count writes to an address in a window** — iterate `write_history` above and
stop at the window's lower bound.

**Per-frame activity across a whole session** — the analyzer reports it directly:

```sh
tools/verification/ttd-analyzer/run.sh report session.ttd -o report.md
```

The report carries, per checkpoint: dirty page count, unique sub-page slots,
`PC`, `SP`, screen bank, border, and the paging latch. That is a frame-by-frame
activity profile without writing any code.

**Whole-session shape:**

```sh
tools/verification/ttd-analyzer/run.sh info session.ttd
```

reports the write-journal record count, the coverage index's per-kind frame
coverage, page-store composition and compression ratios.

---

## 7. Finding where a screen or data region got corrupted

A worked pattern combining the above, which is what most real debugging turns
out to be:

1. **Scrub to where it looks wrong.** `ttd goto <frame>`, watch the screen.
   Binary-search the frames — each seek is 0.2 ms, so this is interactive.
2. **Find the culprit write.** `ttd fl --addr <corrupted address> --access write`
   → gives the frame, t-state and the writing instruction's PC.
3. **Go to just before it.** `ttd goto <frame>` then step forward to the
   t-state, or seek and `ttd reverse-step` a few instructions.
4. **Ask how execution got there.** `ttd rc --pc <that PC>` walks back to the
   previous time that code ran, which is usually where the wrong pointer was
   computed.
5. **Repeat one level up.** Each answer names an instruction; each instruction
   is a new `reverse-continue` target.

---

## 8. Markers — annotating a recording

External events are recorded as markers on the timeline and act as **barriers**
for backwards search: a search will not silently replay across one, it stops and
reports the marker instead.

```sh
ttd markers                 # list them
ttd barriers                # same, emphasising the blocking role
```

This matters for anything non-deterministic. A disk write, for example, cannot
be replayed truthfully, so the engine refuses to search past it rather than
producing a confident wrong answer.

---

## 9. What a `.ttd` costs

Measured on 300-frame recordings (`testdata/ttd/`):

| Recording | File | Write journal | Coverage index | Per hour |
|---|---|---|---|---|
| Game (Dizzy Y) | 0.53 MB | 32 KB | 8.2 KB | ~0.31 GB |
| Demo (7threality) | 1.07 MB | 0.08 MB | 11.4 KB | ~0.64 GB |
| Idle machine | 0.74 MB | 0.25 MB | 16.7 KB | ~0.44 GB |
| Heavy demo (across-the-edge) | 3.48 MB | 2.46 MB | 13.9 KB | ~2.1 GB |

The write journal dominates. If you do not need instant "who wrote this byte",
recording with `--no-journal` cuts the file by 40–70%; reverse watchpoints then
fall back to replay, which still works.

Recording costs about **470 µs per frame**, i.e. 2.4% of a frame's 20 ms
real-time budget. You will not feel it.

---

## 10. What is *not* possible yet

Stated plainly, because a list of capabilities without its complement is
marketing:

* **Conditional breakpoints.** Fully specified, not implemented — there is no
  expression evaluator yet, so `reverse-continue` matches on PC alone. You
  cannot say "stop when it writes 0 to X while A == 5"; you can find every write
  of 0 to X and check A at each.
* **Regular breakpoints during replay.** `BreakpointManager` is deliberately
  inert while TTD replays, so breakpoints set in the debugger do not fire during
  a seek or a reverse search. Reverse breakpoints go through
  `ttd reverse-continue` instead.
* **Watching a range in one call.** Compose per-address queries (§5).
* **Hit counters.** Compute from history (§6).
* **Rewriting history.** You can resume recording from a past point
  (`ttd resume`), which truncates everything after it — but you cannot edit a
  recorded frame and replay forward with the change.
* **Cross-machine playback.** A recording carries its machine model and refuses
  to load into a different one. This is deliberate: the alternative is a session
  that appears to work and produces a corrupt machine.

---

## 11. Inspecting a file without an emulator

The analyzer reads `.ttd` files directly, which is useful for triage, CI gates
and for looking at a recording someone sent you:

```sh
cd tools/verification/ttd-analyzer
./run.sh info session.ttd         # header, sections, sizes, compression
./run.sh validate session.ttd     # integrity checks; exit 1 on error
./run.sh analyze session.ttd      # integrity + anomalies + timeline
./run.sh report session.ttd -o report.md
./run.sh render session.ttd --frame 200 -o frame200.png
./run.sh render-all session.ttd -o frames/
./run.sh heatmap session.ttd -o heatmap.png
```

`render` is often the fastest way to answer "what was on screen at frame N"
without launching anything, and `heatmap` shows at a glance which frames were
busy — a good place to start when you do not yet know where the problem is.
