# The write journal: what it serves, what it costs (experiment E7)

Phase 3, Step 7 ([TDD §4.8](phase-3-replay-inputs-tdd.md#48-step-7--the-write-journal-as-a-derived-index)) treats the write journal as an index that can be rebuilt by replaying the recording. This document answers four questions with measurements (October 3, 2026):

1. Which operations depend on the journal?
2. Why is it so large?
3. How large is it compared with the part of a recording that every operation needs?
4. How fast is "who wrote this last" without it?

The tooling and the raw tables are in experiment E7: [tools/poc/011-ttd-v2-capture-analysis/experiments/e7-write-journal-retention](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e7-write-journal-retention/README.md).

## 1. Which operations depend on the journal

Scrubbing and restoring do not use the journal. They use the snapshot taken at the end of every frame (the checkpoint) and, inside a frame, a replay from that snapshot.

| Operation | What it uses | Needs the journal? |
|---|---|---|
| Scrubbing, jump to a frame | the frame's snapshot | no |
| Jump to a T-state inside a frame | snapshot + replay of part of the frame (1-3 ms) | no |
| Step back one instruction; reverse continue to a breakpoint on a code address | replay of frames + the index of the code each frame executed | no |
| Reverse continue to a read of an address | replay of frames + the index of the addresses each frame read | no |
| Which bytes changed between two moments | comparison of two snapshots | no |
| **Who wrote address X last** (gdb `watch` + reverse-continue; `find-last write` in CLI, WebAPI, Lua, Python) | **the journal**; without it: the index of the addresses each frame wrote + replay of one frame | **yes, the only operation** |
| Who wrote port P last | the journal (the same OUTs are also in the port journal, so they are stored twice) | yes |

The "index" in this table is the coverage index: for every frame, the sorted set of physical addresses it executed, wrote and read (`ttdcoverageindex.h`). It is small and is kept for the whole history (table in §3).

## 2. Why the journal is large

Every write of the CPU to memory becomes one 12-byte record. A record is written even when the stored value does not change.

| Field | Bytes |
|---|---|
| Time (T-state since the session start) | 5 |
| Address | 2 |
| PC of the writing instruction | 2 |
| Value written | 1 |
| Physical memory page | 1 |
| Memory or port flag | 1 |

Example: a demo that redraws half of the screen every frame writes about 3,500 bytes per frame. At 50 frames per second that is 175,000 records, or 2.1 MB, per second.

## 3. The journal against the required part of a recording

"Required part" means what scrubbing and restoring cannot do without: RAM content, the page reference tables and the device states (the checkpoints).

Sessions of one minute (3,000 frames), recorded by the TTD benchmark with a journal ring large enough never to wrap:

| Session | Memory writes per frame | Required part, v1, KB per frame | Journal in memory, KB per frame | Journal / required part (memory) | Journal in the file, MB per minute | Coverage index, KB per frame |
|---|---|---|---|---|---|---|
| Eye Ache | 3,527 | 3.1 | 42.3 | 14x | 31.3 | 1.6 |
| Across the Edge | 2,500 | 3.3 | 30.0 | 9x | 21.5 | 1.6 |
| Game with scripted input | 2,327 | 2.9 | 27.9 | 10x | 22.5 | 2.0 |
| 7th Reality | 872 | 3.3 | 10.5 | 3x | 6.7 | 1.3 |
| 48K at the BASIC prompt | 1,809 | 0.7 | 21.7 | 31x | 0.9 | 1.1 |
| Pentagon at the BASIC prompt | 52 | 1.1 | 0.6 | 0.6x | 0.06 | 1.1 |

Notes:

- **v1 keeps the journal uncompressed in memory.** For Eye Ache that is 127 MB per minute. v1's ring holds 8,388,608 records (100 MB), so on Eye Ache it starts losing its oldest writes after about 47 seconds.
- **In the file the journal is compressed about 4 times.** It is still 3-7 times larger than the required part.
- **The new engine stores the required part 2-3 times smaller** than v1. Phase 2 baseline, first 600 frames: game 2.0 KB per frame, Across the Edge 1.3, 7th Reality 1.1 ([phase-2-results.md](phase-2-results.md)). Against the engine's required part, the journal is 15-25 times larger.
- **The 48K BASIC prompt writes a lot but compresses to almost nothing.** The ROM's memory loop writes the same pattern every frame: 21.7 KB per frame in memory, 0.9 MB per minute in the file.

## 4. "Who wrote this last" with and without the journal

Without a journal, the answer comes from the coverage index:

1. Walk the index backwards from the current position, frame by frame (no emulation), to the newest frame that wrote the address.
2. Replay that one frame and take the last write.

A "never written" answer is final as soon as the walk reaches the session start.

| Option | Memory | File | Typical query | Worst case: written long ago, or never |
|---|---|---|---|---|
| v1: a 100 MB ring | up to 100 MB | 7-31 MB per minute | under 1 ms | under 1 ms until the ring wraps. After that v1 replays **every** frame backwards at about 3 ms each, without the index: minutes on an hour of history |
| The whole journal | 7-31 MB per minute, compressed | 7-31 MB per minute | under 1 ms | under 1 ms |
| **No journal** | 0 | 0 | 2-6 ms (replay of one frame) | walk of the index + replay of one frame: table below |

The worst case without a journal grows with the history the walk crosses:

| Content | Index walk, µs per frame | 5 minutes of history | 1 hour of history |
|---|---|---|---|
| Idle, BASIC prompt | 0.1-0.6 | 2-10 ms | 25-130 ms |
| 7th Reality | 0.9-2.5 | 15-40 ms | 0.2-0.5 s |
| Heavy demos, game | 5-9 | 80-120 ms | 1-1.6 s |

These times were measured on a loaded host (load average 20-40 against the usual limit of 12). They will be remeasured on a quiet host, twice.

**Exactness.** Replaying a frame regenerates its writes identically to the journal: time, address, value, PC and page, in order. Checked on 17 sessions, 200 frames each, 3,400 frames in total: no difference (`TimeTravelManager::RegenerateFrameWrites`, the `TTDE7` benchmark).

**A window of journal hardly helps the worst case.** Keeping the last 1 to 30 seconds of journal (50 to 1,500 frames) answers recent writes at once. Old and never-written addresses still need the walk:

| Session (5 minutes) | p99 without a journal, ms | p99 with a 30-second window, ms |
|---|---|---|
| Eye Ache | 119 | 106 |
| Game | 81 | 72 |
| Across the Edge | 40 | 36 |
| 7th Reality | 13 | 12 |

On one-minute sessions a 30-second window covers half the history, so it cuts p99 more (ZX-Evo idle: 4.3 to 0.9 ms). That stops mattering as sessions get longer.

## 5. Proposal and the open decision (Q1)

- **Do not keep a write journal.** This saves up to 127 MB per minute of memory against v1, and 7-31 MB per minute of file on active content.
- **Make the walk of the index skip whole blocks.** Add a short summary to every block of 50 frames: the addresses written anywhere in the block. A block whose summary lacks the address is skipped without reading its 50 frames. Estimated effect: the worst case on an hour of heavy history falls from about 1.5 s to tens of milliseconds. Measured by an A/B benchmark before it is kept.

**Owner decision Q1 (open):** the acceptable worst case for "who wrote this address last" on an hour of heavy history:

| Choice | What it means |
|---|---|
| About 1.5 s | No journal; the block summary goes to the ideas backlog |
| Up to 50 ms (proposed) | No journal; the block summary is built now, with its A/B benchmark |
| Under 1 ms | Keep the whole journal: 7-31 MB per minute of memory and file |
