# TTD diff of two moments: design

Code: `core/src/debugger/ttd/ttddiffmoments.h` (query, result, text), `ttdcontrollerdiffmoments.cpp`
(`TimeTravelController::QueryDiffMoments`), the verb `diff` in `ttdcontrol.cpp`.

## Taking a moment

The machine's live state is saved (`SaveLiveState`). For each moment the controller seeks there silently
(`SeekToInternal` in a replay scope: the nearest checkpoint, then the replay to the T asked for) and saves the state
again: the CPU, the chipset latches, every RAM byte and every registered device's state (`TTDPeripheralRegistry::SaveLive`).
It also reads the page and the 8K half each slot shows (`Memory::TtdKeyOf` for `#0000`, `#2000`, ... `#E000`) and the
picture's layout (the engine's captured screen layout of the frame, else the ULA page by `#7FFD`). A seek stops at an
instruction boundary: the moment taken is the first boundary at or after the T asked for, and the reply gives it
(`from`, `to`). Then the live state is put back. A moment outside the history, or one a replay barrier keeps the
replay from reaching, is an error.

Two seeks: each costs a checkpoint restore and less than one frame of replay.

## The sections

- **cpu:** PC, SP, AF, BC, DE, HL, IX, IY, the alternate set, I, IFF1, IM, HALT that differ (R, MEMPTR and the
  boundary bits are left out: they differ at any two moments and say nothing).
- **paging:** the latches of the common chipset state that differ (`#7FFD`, `#EFF7`, `#FE`, the AY register
  select, the TR-DOS flags, the turbo ratio) and the 8K slots whose page or half differs. The Next's MMU, the
  TS-Conf pages and every other model's mapping show in the slots, since `TtdKeyOf` answers for any model.
- **memory:** the RAM bytes that differ between the two snapshots. The screen's pixels and attributes (by the layout at
  either moment) form one range each; the rest are ranges per 16K page joined over gaps of 16 unchanged bytes or
  less. A range carries its page, offset, the Z80 address at the second moment (else the first; none when no slot
  shows it), its span and the bytes changed; a range of 8 bytes or less carries its bytes at both moments. The
  totals (bytes, pages) cover every range, the list the first `limit` in address order.
- **last write:** per listed range the newest CPU write to it after the earlier moment and up to the later one
  (`FindAccesses` with the page filter: the write journal where it has the stretch, else replays pruned by the coverage
  index), with its time, PC, address and the instruction (`ReadCpuView` at the write). None found: `no_cpu_write`
  (a device, DMA, a tool edit, or a barrier in between that the search does not cross).
- **screen:** the bytes of the picture at the second moment that differ, split into pixels and attributes, whether
  the shown page switched, the border colour at both moments.
- **devices:** each registered device whose state image differs, with its size and the bytes that differ; the count
  of the same ones. A raw byte count, not decoded: it says which devices to look at. A device with running counters
  (the AY's tone, noise and envelope phases) differs at almost any two moments, even with no register written.
- **between:** the frames and T between the moments, the keys held at the earlier one and pressed / released in
  between (the input journal), the markers in between (external events, bookmarks), the first replay barrier.

`backwards`: the second moment before the first. The answer still reads first -> second (the values swap); the
writer search runs over the stretch between them either way.

## The reply

JSON (WebAPI, Lua, Python): `from`, `to`, `backwards`, `registers[]`, `latches[]`, `slots[]` (`slotMap` in C++: `slots` is a Qt macro), `memory {changed,
pages, ranges[], more}`, `screen`, `devices {changed[], more, same}`, `between {frames, t, keys_held[], keys[],
more_keys, markers[], barrier}`, `writer_searches`, `ms`, and `text` with `text=true`. MCP and CLI show the text:

```text
diff f149+0 -> f150+0: 1 frame(s), 70908 T
cpu: PC #AAD2->#A9DC; SP #FEF7->#FF74; AF #190C->#0004; BC #0000->#00D8; DE #0007->#508A; HL #964B->#870A; IFF1 #1->#0
paging: same
memory: 40 byte(s) in 3 page(s):
  #FEF3 (RAM 0 #3EF3) 5/5 F9A0AEA581->2435D2AA8F, last write f149+40014 pc #A38C call #A9B9
  #A13C (RAM 2 #213C) 1/1 F5->F7 (2), last write f149+40035 pc #A9BA ld (#A13C),sp
  #5049 (RAM 5 #1049) screen pixels 34/1826, last write f149+70907 pc #A9DA ldi
screen: 34/6912 byte(s) (34 pixels, 0 attributes)
devices: ay-socket.tsfm 51/2008
3 writer search(es), 7.7 ms
```

(Green Beret in play, one frame, from the live run in [plan.md](plan.md).)

## Costs

Recording: none (nothing new captured, no format change). A query: two checkpoint restores and less than two frames of
replay, two RAM copies (up to 2 MB each on the Next), a byte compare, and one write search per listed range (the
journal answers at once; without it a replay of the frames that wrote the range, pruned by the coverage index) plus a
short replay for each writer's instruction text. `writers=false` leaves only the two seeks.

## The planes

| Plane | Form |
|-------|------|
| CLI | `ttd diff F[+T] [G[+T]] [--sections a,b] [--limit N] [--no-writers]` (text) |
| WebAPI | `GET /api/v1/emulator/{id}/ttd/diff?from_frame=F&from_tinframe=T&to_frame=G&to_tinframe=T&sections=&limit=&writers=&text=`; over the socket as `call` |
| Lua | `ttd_diff{from_frame = F, to_frame = G, sections = "cpu,memory", text = true}` |
| Python | `emu.ttd_diff(F, to_frame=G, sections="cpu,memory", text=True)` |
| MCP | `time_travel` action `diff`: `from_frame`, `from_tinframe`, `to_frame`, `to_tinframe`, `sections`, `limit`, `writers` (text) |

## Not in this step

Two runs (two sessions or a session against a file) need a second machine's state; the same comparison applies once
both moments are taken, but loading a second history is its own item.
