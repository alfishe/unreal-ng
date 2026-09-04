# Work Item: Publish unreal-ng under GPL v3

> **Date:** 2026-09-02
> **Status:** License files added; 3 blockers + 1 provenance question open before public release
> **Audit:** [license-audit.md](license-audit.md) (full component inventory, evidence paths, verdicts)

## Decision

**GPL-3.0-or-later.** Every inbound license found in the tree (MIT, BSD, zlib, ISC, public domain, PSF, LGPL-2.1+,
LGPL-3 (Qt), Apache-2.0, GPL-2.0-or-later, GPL-3.0-or-later) is compatible with it. Nothing requires "GPL-3.0-only",
and "-or-later" matches the licensing of UnrealSpeccyP, ZX Diagnostics and Z80 XCF Flavor.

## Added in this work item

| File | Content |
|------|---------|
| `LICENSE` | verbatim GPL-3.0 text (copied from the GNU coreutils distribution, SHA-256 recorded below) |
| `THIRD_PARTY_NOTICES.md` | component table, heritage statement, "not covered by GPL" list, unresolved items |
| `data/rom/README-ROMS.md` | ROM provenance and the Amstrad permission |
| `testdata/NOTICE.md` | test fixtures are third-party works, licensed fixtures listed |
| `README.md` | License section |

## Blockers before publication (need the owner's decision; nothing was deleted)

| # | Item | Action |
|---|------|--------|
| B1 | `data/fonts/consolas.ttf` (Microsoft Consolas, proprietary, loaded by `unreal-qt/src/main.cpp`) | replace with an OFL font (JetBrains Mono / Cascadia Code / Fira Mono / DejaVu Sans Mono), update `main.cpp` and `speedcontrolwidget.cpp` |
| B2 | `core/src/3rdparty/z80ex/` — GPL-2.0-only, not compiled, no includes | delete |
| B3 | `core/src/3rdparty/simpleini/convertutf.{c,h}` — legacy Unicode Inc. field-of-use notice, not compiled | delete |
| P1 | Original UnrealSpeccy 0.3x license not recorded anywhere in the tree | locate `unreal_e.txt` / license text from a 0.37–0.38 source archive; if GPL-2.0-or-later or GPL-3: quote it in the notices; if GPL-2.0-only or "freeware": obtain written permission from the original authors |

## Conditions and clean-ups (non-blocking)

* OpenSSL: require 3.0+ (Apache-2.0). MSVC links it statically (`OPENSSL_USE_STATIC_LIBS`); either keep 3.x only or add
  an OpenSSL linking exception to the license preamble.
* Qt stays dynamically linked (LGPL-3 obligation); `WinDeployQt.cmake` already does this.
* Restore missing upstream notices: blip_buf (LGPL-2.1+ text), ayumi (MIT), ownShell `AUTHORS`, z80test vectors, FUSE vectors.
* Clarify the "amiga-paula" reference in `audio_character_chain.h` (own project or third-party?).
* Remove or keep-with-notice unused vendored code: nlohmann/json, vcd-writer, libwave, python build helpers; dedupe CLI11.
* Optional: SPDX headers (`// SPDX-License-Identifier: GPL-3.0-or-later`) on own source files via a script; never on vendored files;
  mark UnrealSpeccy-derived files with "Portions Copyright (C) SMT, Alone Coder, deathsoft".
* Docs that already point at "the LICENSE file" now resolve (`docs/emulator/design/control-interfaces/README.md`,
  `tools/python/README.md`, `unreal-videowall/README.md`).

## Verification

* `LICENSE` is 674 lines, begins with "GNU GENERAL PUBLIC LICENSE / Version 3, 29 June 2007", ends with the
  "How to Apply These Terms" appendix. SHA-256 is recorded in the shell log of this session and matches the
  coreutils 9.11 `COPYING` file.
* No repository file was deleted or modified other than the additions listed above and the README section.
