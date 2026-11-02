# Conclusions

**Go.** eCat3's debugger window runs over unreal-ng's core with its views unchanged, behind a shim of ecat's API and
an adapter of about 800 lines. The views' diff against upstream is 7.0 %, all of it the partial repaint the owner
asked for (the upstream pull request #84 plus one fix of it). It covers the main Z80 of Pentagon, 48K, 128K, ATM3,
Scorpion, TS-Conf and Sprinter and, as a second processor tab, the Z80 of a General Sound (NeoGS or MultiSound's).
Following a running machine it costs 0.1-0.5 % of a core.

| Goal | Answer | Criterion |
|------|--------|-----------|
| 1. Views kept, diff | Compiled and ran with 0 edits; 219 added / 20 removed lines of 3 402 (7.0 %) for the partial repaint: PR #84's branch plus its `PaintedRows` fix | < 15 %: met |
| 2. Main Z80 features | Listing following PC, arrows, stop / step / step over / step out / run to cursor / breakpoints, register, flag and memory editing, stack, history, CPU-view (each byte's page in the status line) / RAM / ROM dumps, instruction bytes typed over in the listing, the dump's find / load / save, the Devices tab: 20-25 checks per model on 7 models, 24 of 24 runs pass | every automated item: met |
| 3. GS Z80 as a processor tab | Own tab: listing, registers, stack, address-space dump, card RAM tab (its flash in the dump's "+" menu); its breakpoints stop the machine and bring it to the front. Its step is a machine step, as in ecat | met in the form the criterion allows |
| 4. What the core lacks | 5 items: run-on-emulation-thread, card CPU single step, coherent display reads; labels for immediates and mnemonic case are resolved in the core (`ListingStyleScope`) (see [analysis.md](analysis.md#gaps-of-the-core-goal-4)) | listed |
| 5. Cost | +0.1 % (GS) / +0.5 % (no GS) process CPU over debug mode, GUI thread 0.7 %, 48.8 frames/s kept; paused: no paints, no cost | < 2 %, 50 frames/s: met |

Repaints: with the change, nothing is painted while paused. While running only the changed rows and cells are
painted (20 paints/s, 0.25 Mpx/s, most of it the stack view following SP, against 127 paints/s and 22 Mpx/s for
upstream's full repaints). The screen
window repaints only the box of pixels a new frame changed. The one exception left is outside the views: macOS's
overlay scroll bars and the address field's text caret are animated by Qt itself.
