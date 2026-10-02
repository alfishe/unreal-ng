# DONE — SZX (ZX-State) snapshots (2026-09-29)

**Status:** done, on master (2026-09-29; PLAN row #64 retired 2026-10-01). `.szx` loads and
saves on every surface for 48K, 128K, +2, +2A, +3, Pentagon 128 / 512 / 1024 and Scorpion,
checked both ways against libspectrum; every standard block for hardware we emulate; RZX (#27)
starts from and seeks with SZX.

**Scope (user, 2026-09-29): standard SZX only.** Machines with an SZX id read and write it; a snapshot
for another model is refused (no model switch); a model without an SZX id (ATM, ZX-Evo, Profi, TSConf,
Scorpion 1024) cannot save SZX; no private blocks.

## Documents
- [design.md](design.md) — goals, machine mapping, block coverage, architecture (reader, stage, commit, capture, writer, report), private `UNMC` / `UNDV` blocks, policy for machines without an SZX id, compression dependency, TTD / RZX integration, surfaces, tests, phases S0-S5, open decisions.
- [szx-format-reference.md](szx-format-reference.md) — byte-level SZX v1.5 reference with every spec page, libspectrum and other emulators cited.

## Next
- [x] Design review round 1 applied (design §20).
- [x] S0: miniz 3.1.2 vendored (`core/src/3rdparty/miniz`), `szxformat.h`, `SzxReader` with bounds checks and exact-size inflate, fuzz test.
- [x] S1: read + commit of header, CRTR, Z80R (frame position through `intstart`, MEMPTR incl. 1.1-1.3 `chBitReg`, Q from FSET, EI shadow, HALT, INT window), SPCR (paging replayed through each model's decoder), RAMP, AY; `.szx` on every surface (MCP `load_software`, Qt open / save dialogs and file manager, CLI / WebAPI / Lua / Python through `Emulator::LoadSnapshot`); report in the log.
- [x] S2: `SzxWriter` for the same blocks (exact sizes, deflate only when smaller); Qt save dialog offers SZX first.
- [x] S3 part: B128 both ways (WD1793 registers, #FF, SEEKLOWER, PAGED ↔ TR-DOS); `WD1793::RestoreSnapshotRegisters`.
- [x] Fixtures `testdata/loaders/szx` (libspectrum synth files for all nine machine ids, two converted snapshots, Spectaculator 1.1, ZXMAK2 1.4, ZX-M8XXX 1.4) with libspectrum oracle dumps; `tools/verification/szx` (`szxtool`, `check-interop.sh`: our files read by libspectrum match on all nine models).
- [x] Another model is refused by the core with both models named ("saved on a Pentagon 512K, the running machine is a ZX-Spectrum 128k: create a Pentagon 512K to load it"); in the Qt window (drag and drop, File > Open, command line) an SZX for another model replaces the running machine by that model first (`MainWindow::switchMachineModel`, the Machine menu's path), the same model just loads.
- [x] Every standard block for hardware we emulate, both ways (2026-09-29): BDSK (linked or embedded TRD / SCL / FDI / UDI, head cylinder, write protect), +3 (motor) and DSK (links), TAPE (linked or embedded, head block), GS + GSRP (classic GS: CPU, page, volumes, DAC levels, RAM; the card's timing and mailbox are not in the format), COVX (#FB), AMXM (Kempston), KEYB. Linked media: next to the snapshot first, then the stored path, always Session access (never written); embedded images staged in temporary files that go with the medium (`MediaSourceType::Upload`). Saving links file-backed media, relative to the snapshot's folder when inside it; unsaved writes and non-file media are reported.
- [x] Read and reported, not applied (hardware we do not emulate): Issue 2 keyboard, keyboard joysticks, JOY, SpecDrum (DRUM), the AMX mouse, NeoGS for a GS block, and IF1, Multiface, printers, Timex, +D, Opus, LEC and the other interfaces. A TurboSound FM config (YM2203) has no AY-3-8910: the AY block is reported, not applied.
- [x] HALT: PC stays on the HALT while halted (as in Fuse); a file with PC past the HALT is moved back onto it (design §20 R11); `LoaderSZXHalt_Test`.
- [x] ~~S4 private blocks and the no-id machine policy~~ - dropped: models without an SZX id refuse SZX.
- [x] S5: RZX integration (2026-09-29): RZX playback (#27) starts SZX snapshots through `Emulator::LoadSnapshot`, converts the start T-states with `LoaderSZX::FramePositionFromIntCount`, and its seek keyframes are our SZX (`Capture` / `SzxWriter`, `SzxReader` / `Commit`). Verified with real recordings: the five archive RZX files re-written by libspectrum with SZX start snapshots (`testdata/loaders/rzx/szx`, `szxtool rzx-resnap`) match the SkoolKit oracles after 300 frames and to the end, exactly like their `.z80` originals.
- [x] Corrected the early SZX claims (snapshot-loading DONE.md, automation action plan) and the #EFF7 claim (Pentagon 1024 16-color design and DONE, PLAN #53: closed).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #64, #27, #53.
- RZX article: `2026-09-28-debugger-family/rzx-ttd.md`.
