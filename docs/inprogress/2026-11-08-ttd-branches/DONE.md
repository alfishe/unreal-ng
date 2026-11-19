# DONE — TTD branches

Closed by the owner on 2026-11-16. Everything below landed on master; the phase table and the design are in
[README.md](README.md).

- [x] P0: F-1 (a resume keeps the folder's past) and F-2 (deck readout shape), B-0 / B-1 baselines (`e0ac62a53`)
- [x] P1: branch table, GUIDs, metadata, lineage, per-branch engine lines, fork at the boundary and mid-frame, switch,
  delete; resume and edits in the past fork (`e0ac62a53`)
- [x] P2: storage - branch streams, the folder, save all / lineage, load, old-reader compatibility (`e0ac62a53`)
- [x] P3: two tiers - the history limit evicts finished segments to disk, a restore reads them back bit-identical
  (`912e8dd68`, `47ebc9dd3`)
- [x] P5: automation planes, `branch-create` with run, prune, info, the `ttd.branches` topic, the analyzer loop
  (`c675c6a37`, `a314e9058`, `50b4da4dc`)
- [x] P6: Qt TTD panel (`66f6811ae`), Embedded and eCat debuggers (`0744664bb`), actions, readouts, Stream Deck,
  controllers (`4ef3337ee`)
- [x] P7: Python tools `tools/python/ttd` (`e37d04ef0`)
- [x] P8: stale texts, recipes, command-interface (`1a3aa305e`)

## Not planned (owner, 2026-11-16)

- P4: media in the stream, the version tree, write-through following the active moment. Media changes in the
  recorded past stay refused with a hint.
- Qt: the main window's status bar, the lane filters (creator / tags / run), edits in the past from the panel.
- A rename, note, tag or pin of a folder session is not written to `branches.tdx` (no update record); a save from
  memory keeps them.
- The Surface Dial binding (the system wheel menu cannot be suppressed, no long press).
- Out of scope from the start: forks to other machines, loads as branch points (D42), merging branches.
