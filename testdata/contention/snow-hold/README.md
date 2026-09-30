# Snow Hold (Mark Woodmass, 2025)

A 48K test of the ULA "snow" effect (see [docs/inprogress/2026-09-29-ula-snow](../../../docs/inprogress/2026-09-29-ula-snow/research.md)).
It syncs to an exact T-state, sets `I` = #40 and `R` = 0 each frame, and runs 16 x `LD A,0` (7 T) per screen
line, so the refresh walks through every tick of the ULA's fetch cycle: static ladders of snow appear under
the pattern bands. Licensed under the GNU GPL (`COPYING`); from the zxe.io test depot.

| File | What |
|:--|:--|
| `snowhold-beta.tap` | The beta of 2025-05-26: three pattern bands. **Photographed on three real machines** (48K Issue 3 NEC CPU, 48K Issue 3B Zilog CPU, Spectrum+ Issue 6A), see the research page; unreal-ng's rendering test checks against those photos |
| `snowhold.tap` | The release of 2025-05-27: twelve pattern bands, same timing |
| `snowhold.asm`, `execcycle.asm`, `delay.asm` | The release's source |

What the photos show, and what `UlaSnow_Test` checks on unreal-ng's 48K: under each of the three bands two
ladders, at character columns 2 and 16, one bright line every 4 screen lines, colored by a pattern attribute
the snowed address picks (magenta, green and cyan from the top band down; bright colors, over-exposed to pink /
white on the photos).
