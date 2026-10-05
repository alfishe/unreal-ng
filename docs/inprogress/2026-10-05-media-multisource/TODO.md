# Multi-source media — TODO

**Status:** design written 2026-10-05 (branch `media-multisource`); every owner question answered. No code.
PLAN.md row **#95**.

## Owner decisions (2026-10-05)

- Composition is file-level (overlayfs-like union into one synthesized volume). A block overlay is
  used only for guest writes.
- Disks are joined both as a merged tree and as partitions (partitions expected to be rarer).
- Write-back strategies are researched. Flatten to one flat `.img` / `.vhd` is mandatory.
- Guest deletes follow a per-layer policy: keep (default, whiteout) / trash / move / delete / ignore.
- The descriptor is its own `*.ucompose.yaml` file.
- Boot structures (El Torito, MBR / volume boot code, reserved sectors) are carried from the bottom source or laid on top by a boot layer.
- `save`: the save policy in automation, a strategy dialog in the GUI; sources (S3 / S4) change only on an explicit request.
- Phase order: read side, then attribution with S1 / S2, partitions, S3 / S4 last.

## Remaining

- [ ] Review round 1: requirements, architecture, FS compatibility, flatten strategies, TDD
- [x] Owner questions answered 2026-10-05 (D-4…D-9 in [goals-and-requirements.md](goals-and-requirements.md) §3; guest FS support researched in [fs-compatibility.md](fs-compatibility.md) §6)
- [ ] C0 baseline: `HostFolderFat` corpus hashes and read benchmark numbers on master
- [ ] C1 core + `HostFolderFat` parity refactor
- [ ] C2 descriptor, validation, manager integration, surfaces (`compose`, `layers`); Sprinter IDE slots `fsCompatibility = {Fat16}`
- [ ] C3 FAT image sources
- [ ] C4 graft
- [ ] C5 ISO reader, ISO target
- [ ] C6 provenance, attribution (`changes`), S1 flat (+ VHD writer, compact), S2 delta
- [ ] C7 partitions
- [ ] C8 S3 commit, S4 write-back, Qt flatten dialog
- [ ] C9 optional bulk `ReadSectors` (A/B gated)
- [ ] Benchmarks and charts C1-C8 with the results table filled in ([test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5.5)
- [ ] User docs (`docs/features/media.md`) and recipe `.recipe/media/compose-media.md`
- [ ] Follow-up after C0-C9: library extraction and unification, phases X0-X13 ([library-extraction/](library-extraction/README.md)), PLAN.md row **#96**
