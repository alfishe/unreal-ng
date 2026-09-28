# Unified media manager — TODO

**Status:** requirements, technical design, media history design and integration designs reviewed
(two rounds, 2026-09-28): **ready for M1**. Nothing implemented beyond the E5 storage seam (`core/src/emulator/io/storage`). PLAN.md
row **#58**.

## Documents

| File | Topic |
|---|---|
| [requirements.md](requirements.md) | Problem, actors, FR / NFR / acceptance |
| [technical-design.md](technical-design.md) | **Start here.** `MediaManager`, slots, media, format registry, block stack, folder pipeline, `HostFolderFat`, config, model switch, phases M1-M6 and H1-H5 |
| [media-history-design.md](media-history-design.md) | Immutable source + versioned change layer (spill to disk), block and file views, export, tracking API, TTD v2 / UNS |
| [integration-next.md](integration-next.md) | ZX Next SD cards (later machine) |
| [research.md](research.md) | WinUAE, xpeccy-plus, DOSBox-X, QEMU vvfat, and unreal-ng today (with the bugs found) |
| [integration-zxevo-sd.md](integration-zxevo-sd.md) | `sd.zc` on ZX-Evo: phase M1 = ZX-Evo E5b |
| [integration-neogs-sd.md](integration-neogs-sd.md) | `sd.ngs` when the `neogs` branch merges |
| [integration-tsconf-sd.md](integration-tsconf-sd.md) | `sd.zc` on TSConf (phase 6) |
| [integration-ide-cd.md](integration-ide-cd.md) | IDE units as slots (`ide0.*`, `ide1.*`), a CD as a unit configured `cdrom`, with IDE rollout 1 |
| [integration-floppy.md](integration-floppy.md) | `fdd.a-d` migration (WD1793, uPD765), folder as a TR-DOS disk |
| [integration-tape.md](integration-tape.md) | `tape` migration |
| [integration-automation-gui.md](integration-automation-gui.md) | the `media` verbs on every surface, the Qt media panel |
| [reuse-and-readiness.md](reuse-and-readiness.md) | Review round 2: reuse across BaseConf, TSConf, NeoGS, Scorpion, Profi, ATM2, Next, Sprinter; design changes G1-G12; readiness for M1 |
| [integration-ttd-snapshots.md](integration-ttd-snapshots.md) | TTD v1 rules (media-agnostic, barriers, recording guard); TTD v2 and UNS through media versions |

## Remaining

- [x] Review round 1 (2026-09-28): decisions folded into the documents
- [x] Review round 2 (2026-09-28): reuse across machines, readiness; G1-G12 folded in ([reuse-and-readiness.md](reuse-and-readiness.md)); M1 hook points to pin at its start
- [ ] M1 block: core, folder pipeline, `HostFolderFat`, ZX-Evo `sd.zc` (= ZX-Evo E5b): ACC-1…ACC-4
- [ ] M2 floppy: slots, migration (fixes the eject / save bugs in research §3), folder as a disk image: ACC-7
- [ ] M3 tape: slot, migration, folder as a tape: ACC-8
- [ ] M4 automation verbs + Qt media panel: ACC-6
- [ ] M5 media across model switch: ACC-5
- [ ] M6 IDE / CD slots (with PLAN #13a)
- [ ] H1-H5 media history: versioned change layer, spill, file views, tracking API, UNS / TTD v2
