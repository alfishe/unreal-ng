# Candidates for recorded demos

Titles the owner (or a run of the collection) found worth showing. Index = row number in `collection/index.tsv` (0-based, see
`tools/machines/next/browser-drive/collection.py`). Add a line when a title looks good; keep the reason short.

| # | Title | Kind | Why | Marked |
|--:|:--|:--|:--|:--|
| 67 | NextDash (`demos/utility-showcase/NextDash/nextdash.nex`) | game | a fun Geometry-Dash-like game with a title screen, scrolling tilemap and music | owner, 2026-10-09 |
| 34 | NextBASIC Text Scroller (`demos/tech-demo/NextBASIC Text Scroller/scroller.bas`) | demo | big gradient scroller over a starfield with comets (NextBASIC, 28 MHz) | owner, 2026-10-09 |
| 24 | Mapscroll 3 (`demos/tech-demo/Mapscroll 3/mapscroll3.nex`) | tech demo | smooth tilemap scrolling; matches the author's screenshot pixel for pixel (see as-designed-findings.md); the ULA noise around the window is the demo's own look | run, 2026-10-09 |
| 97 | Mario's Bombs Away (`games/action-arcade/Mario's Bombs Away/MariosBombsAway.nex`) | game | owner's pick | owner, 2026-10-10 |
| 147 | Dreamworld Pogie (Demo v1.0) (`games/platform/Dreamworld Pogie (Demo v1.0)/pogie.snx`) | game demo | owner's pick (confirmed) | owner, 2026-10-10 |
| 181 | Nexions (`games/shooter/Nexions/run.bas`) | game | owner's pick | owner, 2026-10-10 |
| 201 | Populous Remake (`games/strategy-board/Populous Remake/populous-demo.nex`) | game demo | owner's pick | owner, 2026-10-10 |
| 203 | Corruption (`games/text-adventure/Corruption/corrupt_512.nex`) | game | owner's pick (text adventure with graphics) | owner, 2026-10-10 |
| 99 | Next Bird (`games/action-arcade/Next Bird/nbird3.tap`) | game | owner's pick (a Flappy Bird; TAP loader: Next mode (N), play speed 28 MHz - at the default 3.5 MHz it is slow; set with collection.py --tap-speed 3 or Sym Shift+Enter in the Browser) | owner, 2026-10-10 |
| 31 | Next3D Demo Cube (`demos/tech-demo/Next3D Demo Cube/cube.nex`, [breakintoprogram/next-3D](https://github.com/breakintoprogram/next-3D)) | tech demo | a rotating yellow/violet/blue filled cube; owner saw it spin on 2026-10-10. Through the NextZXOS Browser it failed with "No such file or dir" until the SPI-in-flight fix (see audit-divmmc-gap-2026-10-10.md D11) | owner, 2026-10-10 |
| 32 | Next3D Demo Space (`demos/tech-demo/Next3D Demo Space/space.nex`, [breakintoprogram/next-3D](https://github.com/breakintoprogram/next-3D)) | tech demo | owner's pick, confirmed from the owner's recording `elite-zxnext.mp4` (2026-10-10): a filled 3D ship (an Elite-style Cobra) rotating in front of a yellow sun, with stars. NOT `elite.nex` (Next3D Elite Wireframe, #33) and not Elite Next (#128, TinfoilAsteroid/EliteNext). Runs through the direct loader and, since the SPI-in-flight fix (2026-10-10), through the NextZXOS Browser too | owner, 2026-10-10 |

## Not candidates

- **Next3D Elite Wireframe** (`demos/tech-demo/Next3D Elite Wireframe/elite.nex`, #33, [Olifiers/Next3D](https://github.com/Olifiers/Next3D), 2023): not a demo. Its `main.c` is a test of the engine's triangle primitives (`trigL2`, `fillTrigL2`, v0.3 "DEBUG MODE, NOT READY FOR PRODUCTION"): it draws a few triangles once, redraws one of them every frame, and key `1` toggles the border black/magenta. A static picture is the correct output; jnext, ZEsarUX and unreal-ng give the same frame (0 differing pixels of 327680). It is a different project from Next3D Demo Space / Cube / Circle ([breakintoprogram/next-3D](https://github.com/breakintoprogram/next-3D), 2025), whose `demos/space` draws the Cobra Mk III with z-sort and a rotating camera. Sources of both are in `zx-next/source-code/{Olifiers-Next3D,breakintoprogram-next-3D}`.
