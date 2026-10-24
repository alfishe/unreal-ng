# 034 — proposal

## Adopted (in `core/swsignature`, with unit tests)

1. **The catalog rule** for `match.file_set` (`match.h`, `CompareFileSets`): two tokens per TR-DOS entry, `n:` (name,
   type, start, length) and `c:` / `d:` (with the data); a match keeps every reference entry, has at most one file
   with other data but not all, at most max(2, n) entries added. A set without `n:` tokens keeps the first-draft
   Jaccard rule. The reason string says why: `file set: every entry kept, 1 of 5 files changed, 0 added`.
2. **The sector path** for TRD and SCL (`Options::sectorImages`, default on): the catalogue and the files read from the
   image bytes with the checks of `TrdosCatalog::IsTrdos` and `LoaderSCL::loadSCLData`; anything unusual (a file past
   the end of a short TRD, a bad SCL sum) goes to the DiskImage loaders as before. Unit test
   `Signature_Test.SectorImagesReadAsTheDiskImageLoaders`; corpus cross-check 20 908 disks, one explained difference.
3. **Uniform RAM pages** are left out of the memory signature, a memory match needs ≥ 16 common pages
   (`kMemoryMinPages`). Profiles use the memory signature only exactly (`mem:sha256:`), as before.

Feature docs updated: [input-and-profiles.md §4](../../../docs/inprogress/2026-10-08-steamdeck-client/input-and-profiles.md#4-signatures)
(Steam Deck client), [mapping-and-profiles.md §6.1](../../../docs/inprogress/2026-10-17-controller-manager/mapping-and-profiles.md#61-order-dt-2-d7)
(controller manager), [command-interface.md §18](../../../docs/emulator/design/control-interfaces/command-interface.md#18-media-signatures).

## Next, not done here

- **Speed on one thread**: a hardware SHA-256 (ARMv8 SHA2, x86 SHA-NI, a portable fallback; bit-exact by definition)
  would put one thread well above 2000 files/s; the library scan of the Deck client can run on a worker pool instead
  (media are independent). Decide with the Deck's scanner (P-16).
- **A stricter "added" bound** (≤ 2 entries) if a game profile applied to a magazine disk that carries the game turns
  out to be a nuisance; every save variant measured adds one file.
- **One-file games that rewrite their only file**: would need a similarity inside the file (equal sectors), more than
  a token list in a profile can carry. Rare; the exact keys still find the untouched release, and the user can pin a
  profile.
- **The memory threshold**: the five oracle pairs and 6992 other pairs are separated anywhere between 63 % and 67 %;
  more recordings of the same games at other moments (RZX) are needed before a fuzzy memory match is offered.
- **Core findings for their owners**: `LoaderTRD::format` refuses 86-cylinder TRDs that `FloppyFormats::ProbeData`
  accepts (`system/BBR.zip`); the tape probe then takes the 704 KB disk as a TAP. The SCL writer drops a file whose
  sector chain leaves the TR-DOS geometry (two TRDs of the corpus).
