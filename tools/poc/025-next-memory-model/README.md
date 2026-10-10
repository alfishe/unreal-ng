# PoC 024: the Next's memory model and the CPU library's per-step cost (P2)

Phase P2 of [the Next phases](../../../docs/inprogress/2026-10-07-zx-next/phases.md): does decision D6 (a flat 2 MB array and an
eight-entry slot table of read / write pointers, reached through the CPU library's callbacks) cost anything against today's four
16K windows, what would the library's own page tables save, and what does a mapping change cost the host?

Self-contained: the vendored CPU libraries and Google Benchmark from `lib/`, no emulator build.

```bash
cmake -S tools/poc/025-next-memory-model -B scratch/poc-024/build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C scratch/poc-024/build
UNREAL_NICE=0 scratch/poc-024/build/p2-shift13 --benchmark_filter='Frame|Rebuild' --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```

Run from the repository root (the workload reads `data/rom/48.rom`). `p2-shift14` and `p2-shift13` differ only in the page size of
unreal-z80's paged bus (a build option, `Z80CPU_PAGE_SHIFT`).

## Workload

The 48K ROM idling in its editor loop, the machine of `BM_HostFrame_48K_*`: after 150 warm-up frames, one 69888 T frame per iteration
(7220 instructions, equal for every library: the same program is executed) with the interrupt at its start. ROM 0-3FFF is read-only
(writes land in a sink page), RAM above. Only the CPU and its bus are timed: no video, sound or ULA.

| Variant | How the CPU reaches memory |
|:--|:--|
| callback 4x16K | the host's callback: `rd[addr >> 14][addr & 0x3FFF]` (the shape of `Memory::MemoryReadFast`) |
| callback 8x8K | the same with eight 8K slots: `rd[addr >> 13][addr & 0x1FFF]` (decision D6) |
| paged | the library holds the table and reads direct pages inline (unreal-z80 only); null entries would go to the callbacks |
| flat | a 64K array, no ROM protection (unreal-z80 only): the upper bound |

## Results

Median of seven repetitions, CPU time per frame in microseconds (host load average 8-13 during the runs, equal for the variants of a
run because they alternate in one process; the differences below 2 % are noise):

| Library and bus | 4x16K callback | 8x8K callback | paged | flat |
|:--|--:|--:|--:|--:|
| unreal-next-z80 | 45.26 | 45.34 | not built | not built |
| z84c15 | 46.76 | 46.68 | | |
| unreal-z80 | 47.54 | 45.12 | 33.46 (8K), 34.17 (16K) | 31.08 |

Mapping change (host work per change):

| What | ns |
|:--|--:|
| 128K machine, port 7FFD: four windows, read and write pointers | 0.97 |
| Next MMU: eight slots (NextREG `#50-#57`), ROM slots write-protected | 6.1-7.2 |
| the same plus unreal-z80's `Z80CpuSyncPageTables` (8K pages) | 6.8 |

## Conclusions

1. **D6 stands.** Eight 8K slots behind the callbacks cost the same as four 16K windows (45.26 and 45.34 us for the Next library; the
   other two libraries agree within noise). The table lookup is one shift, one mask and one load either way.
2. **The Z80N library is not slower than the libraries it was forked from**: 45.3 us against 46.8 (z84c15) and 47.5 (unreal-z80) on
   the same program. The 29 added instructions and the NEXTREG hook cost nothing on code that does not use them.
3. **A paged bus would save a quarter of the CPU part**: 33.5 us (8K pages) against 45.3 us, flat 31.1 us. The whole CPU part is
   small next to a host frame (`BM_HostFrame_48K_Fast` is about 670 us); at 28 MHz the instructions per frame are eight times as many
   (about 360 us callback, 270 us paged) and the saving is about 90 us of a 20 ms frame (0.5 %). It is not worth building for N2.
   If it is ever wanted, the Next fork needs the paged bus of unreal-z80 added back (it was dropped with the z84c15 fork) and the
   host calls one `Sync` per mapping change (7 ns).
4. **A mapping change is free**: about 7 ns for all eight slots. Even a program that writes an MMU register every few instructions
   (thousands per frame) costs tens of microseconds, and ordinary programs do a few per frame.
5. **Not measured here**: the native interpreter against the library (the native core needs the whole host), and the Next adapter's own
   hooks (M1 hook, NextREG, contention). The first needs the machine of N2; the engine A/B the plan names belongs to N2's gate, with
   `Z80NEngine` installed and `BM_HostFrame_*` before and after.

## Recommendation for N2

Callback bus with the eight-slot table, as designed. Keep the paged bus as a documented later option (conclusion 3). The slot table is
rebuilt in the callback of the write that changes it (NextREG, port 7FFD/1FFD/DFFD, Layer 2, DivMMC, Multiface, boot ROM bit), before
the instruction ends; the library never caches a pointer across a callback.
