# Multi-source media — TODO

**Status:** design draft written 2026-10-05 (branch `media-multisource`). Not reviewed. No code.
PLAN.md row **#95**.

## Owner decisions so far (2026-10-05)

- Composition is file-level (overlayfs-like union into one synthesized volume). A block overlay is
  used only for guest writes.
- Disks are joined both as a merged tree and as partitions (partitions expected to be rarer).
- Write-back strategies are researched. Flatten to one flat `.img` / `.vhd` is mandatory.

## Remaining

- [ ] Review round 1: requirements, architecture, FS compatibility, flatten strategies, TDD
- [ ] Answer the open questions Q-1…Q-4 ([goals-and-requirements.md](goals-and-requirements.md) §8); confirm the guest FS table ([fs-compatibility.md](fs-compatibility.md) §6)
- [ ] C0 baseline: `HostFolderFat` corpus hashes and read benchmark numbers on master
- [ ] C1 core + `HostFolderFat` parity refactor
- [ ] C2 descriptor, validation, manager integration, surfaces (`compose`, `layers`)
- [ ] C3 FAT image sources
- [ ] C4 graft
- [ ] C5 ISO reader, ISO target
- [ ] C6 provenance, attribution (`changes`), S1 flat (+ VHD writer, compact), S2 delta
- [ ] C7 partitions
- [ ] C8 S3 commit, S4 write-back, Qt flatten dialog
- [ ] C9 optional bulk `ReadSectors` (A/B gated)
- [ ] Benchmarks and charts C1-C8 with the results table filled in ([test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5.5)
- [ ] User docs (`docs/features/media.md`) and recipe `.recipe/media/compose-media.md`
