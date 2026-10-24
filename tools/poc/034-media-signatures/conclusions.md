# 034 — conclusions

| Goal / criterion (P-12) | Result | Met |
|---|---|---|
| 1. Re-pack and rename recognised (≥ 95 %) | 99.97 % (10 458 / 10 461): TRD ↔ SCL ↔ FDI ↔ UDI, TAP ↔ TZX, file and label renames; 100 % after the SCL writer fix (the three misses were entries past the catalogue's file count) | yes |
| 2. Saved-to disks by the fuzzy rule (≥ 80 %) | first draft (Jaccard ≥ 0.8, no code file missing): 14.8 %. Catalog rule (adopted): 83.9 % over add / rewrite of code and data files; ≥ 94.8 % for every variant on disks of ≥ 2 files; a one-file game rewriting its only file is not recognised (3 %) | yes, with the catalog rule |
| 3. False matches (< 0.1 %) | catalog rule: 81 of 20 701 disks (0.391 %) match another release; by hand nearly all are the same title (another release, or on a magazine / compilation disk); different programs ~0.01 %. First draft: 0.063 % (it matches little) | yes for different programs; no if every cross-release match counts |
| 3b. Is "no code file missing" needed? | it is the wrong guard: it rejects the common TR-DOS save (a code file rewritten). The catalog rule's "≥ 1 file unchanged" is the guard that matters (one-file clusters of the same loader layout) | — |
| 4. Speed (≥ 2000 files/s) | one thread 800-880 files/s under load average ~50 (SHA-256 bound); 10 000-11 400 files/s on 20 threads; the sector path for TRD / SCL made it 10× faster than the DiskImage loaders | on all cores yes; one thread no (under this load) |
| 5. Memory signature (best effort) | as specified useless (46 % of different pairs ≥ 90 %); with uniform pages left out and ≥ 16 common pages: 1 / 6992 false, but 1 / 5 same-game pairs | best effort; exact form only |

Gate P-12: **passed with changes** — the catalog rule replaces the Jaccard rule, TRD / SCL are read from their sectors,
uniform RAM pages are left out of the memory signature (all in `core/swsignature`, branch `ctl-signatures`). Two
core defects found on the way are fixed: the SCL writer exports catalogue entries past the file count, and TRD
images of up to 86 cylinders load.
