# NedoOS ATAPI + CD file system — TODO

**What this folder is:** a design proposal for adding CD-ROM support to NedoOS: an ATAPI driver, a 2048-byte block
cache and a read-only ISO 9660 + Joliet file system. Rock Ridge, the 512-byte raw view and CD-DA coexistence are
optional. The proposal includes a sized work breakdown grounded in the NedoOS sources (fork `alfishe/NedoOS` at
`a750349`): [proposal.md](proposal.md).

**Status:** proposal, not started. The work happens in NedoOS, not in unreal-ng. unreal-ng only adds the tests and
the `testdata/machines/zxevo/nedoos/cdfs/` fixture once a NedoOS build with the driver exists.

**Unblocks:** [../2026-10-05-media-multisource/TODO.md](../2026-10-05-media-multisource/TODO.md), item ACC-C5, the
NedoOS half: NedoOS lists a composite CD on the ZX-Evo's ATAPI drive.

- [ ] Phase 0: agree on the letters (`E:` CD master / `I:` CD slave), the page and the `CDFS` flag with the NedoOS maintainers
- [ ] Phase 1: `cdtool.com` (shared ATAPI + ISO sources, user space); tests `ZXEvoErs_Test.NedoOsCdtool*`
- [ ] Phase 2: kernel `pgcdfs` module; `ZXEvoErs_Test.NedoOsDirListsTheCdDrive` closes ACC-C5 (NedoOS half)
- [ ] Phase 3: robustness (write refusal, disc change, multi-session, master unit, `cdplay` coexistence)
- [ ] Phase 4: real ZX-Evo with 2-3 drives; Phase 5 optional items; Phase 6 docs and the upstream patch
