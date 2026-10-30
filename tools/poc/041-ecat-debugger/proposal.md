# Proposal

1. **Adopt the window as the Qt skin of the debugger-model feature**
   ([`docs/inprogress/2026-09-28-debugger-model`](../../../docs/inprogress/2026-09-28-debugger-model/),
   `gui-main-debugger.md`). Take the views in with the partial repaint, keep the shim boundary: the views see only
   the ecat-shaped API, one adapter maps it onto the core. The card debugger (`gui-card-debugger.md`) becomes a
   processor tab, as the GS Z80 already is here. This is the owner's decision against POC 035's GUI technology
   choice; this POC shows the Widgets path works today at negligible cost.
2. **Core items for the feature's TODO** (from [analysis.md](analysis.md#gaps-of-the-core-goal-4)), in order:
   - `RunOnEmulationThread(fn)`, async and valid while paused, so every debugger state change has one rule;
   - a coherent per-tick snapshot of what the views show (registers, the visible memory), instead of racing reads;
   - a single-instruction step of a card CPU (`DebugCpuId::GS`);
   - (done) the disassembler: labels only for address operands, upper case (`ListingStyleScope`).
3. **The partial repaint upstream**: [Ptr314/ecat3#84](https://github.com/Ptr314/ecat3/pull/84) is open. Its
   `PaintedRows` has the defect fixed here (the arrows' column repaints the whole listing on the next tick); adding
   the 5-line fix to the pull request is again on the owner's go, as it publishes under the owner's name.
4. Done in the follow-ups ([measurements-2026-10-29-followups.md](measurements-2026-10-29-followups.md)): the
   Consolas font from the resources, the dump's find / load / save and in-place byte editing in the self-check, the
   Devices tab seen on screen, the MultiSound GS as a processor tab.
