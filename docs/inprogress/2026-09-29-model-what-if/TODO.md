# TODO — Model what-if and branched history

> TTD index: [docs/emulator/design/debugger/time-travel-debug/README.md](../../emulator/design/debugger/time-travel-debug/README.md)

Status: **design written and decided, W0 and W0b done.** Design: [design.md](design.md). Reference
UI analysis: [reference-branched-ttd-ui.md](reference-branched-ttd-ui.md).
Scene and requirements OR-30 … OR-37:
[ttd-offline-analysis.md §6c](../2026-09-28-debugger-family/ttd-offline-analysis.md#6c-model-what-if-open-this-moment-on-other-machines).

- [x] W0: machine state transfer (`MachineStateTransfer`), WebAPI `POST /snapshot/transfer`, MCP `transfer_state`; floppies and tape as in-memory copies with postfixed paths; SD / HDD / CD explicitly not moved; invariant tests over every model (`6f5759f3`, `570ca3ce`, `7ddbef11`)
- [x] W0b: transfer on CLI, Lua, Python, Qt (parity), 2026-11-16 (uncommitted): one verb `StateTransferControl` (`core/src/loaders/snapshot/statetransfercontrol.h`) behind WebAPI (answers byte for byte as before), MCP (+ `symbolic_id`, `start`, `keep_frame_position`), CLI `snapshot transfer`, Lua `transfer_state{}`, Python `unreal.transfer_state()`, Qt Machine > Transfer State To (an open instance: check, confirm, report; a new machine of a model); WebSocket = the route's `call`. Docs: [automation.md](../../features/automation.md#machine-state-transfer)
- [x] W1: in-memory branches — redesigned and built as [TTD branches](../2026-11-08-ttd-branches/README.md) (P1-P2 `e0ac62a53` 2026-11-10, panel `66f6811ae`, debuggers `0744664bb`); closed 2026-11-16 ([DONE.md](../2026-11-08-ttd-branches/DONE.md))
- [ ] W2: history across snapshot / tape / disk loads — dropped: a load ends the session (D42, owner 2026-10-06, final)
- [ ] W3: forks to other models (`RestoreInto`, lockstep fork runner, input events from the parent, parent link, per-target report); `switch_model` with state — needs W1 and O-1
- [ ] W4: capture, grid, scores, verdict; batch mode
- [x] W5: branches in the TTD v2 container — done by TTD branches P2 (branch streams, the folder, save all / lineage, load; `e0ac62a53`, [storage.md](../2026-11-08-ttd-branches/storage.md)); a session family (linked sessions) stays out with W3
- [x] W6: media per branch — closed, not planned (owner 2026-11-16: TTD branches P4 dropped; media changes in the recorded past are refused with a hint)
- [x] Decided 2026-09-29: branch names (automatic two-word, renamable, 1-32 characters); forks record only when the parent records, else free run; beyond the range forks run live
- [x] Decided 2026-09-29: no truncation anywhere — `resume` starts a branch, history is discarded only by deleting a branch, stopping TTD leaves the machine free-running
- [x] Decided 2026-09-29: forks start at the frame boundary for now (revisit once forks run); memory limit and spill are TTD v2 decisions (#40 Phases 4 / 5), not this design's
