# Findings: NEX programs that fail or stay black (collection run, 2026-10-10)

What the collection run through the NextZXOS Browser and the direct loader showed, what was verified, and what is still a hypothesis. The
batch fixes already landed are in [audit-dma-timing-2026-10-09.md](audit-dma-timing-2026-10-09.md) and [as-designed-findings.md](as-designed-findings.md).

## 1. The walker's own bugs (not the emulator)

| Symptom | Cause | Fix |
|:--|:--|:--|
| "No such file or dir" for titles whose folder names share a prefix (Next3D, Tile/Tilemap Screen), working titles in the OS | the Browser search was typed with letters and digits only; `Next3D Demo Cube` has no unique prefix without its spaces and the walker fell back to counting `down` keys, which loses presses | the Browser's search takes spaces: the shortest unique prefix of the searchable part (`[A-Za-z0-9 ._-]`), typed text checked against both cursor-blink states |
| letters of a name run as Browser commands (`c` Copy, `e` Erase `Y/N`) when the search prompt did not take the text | `H` pressed but the text lost | check the prompt changed; never retype on a doubt; nothing was erased (card file counts equal the master collection) |
| text viewer (`INDEX.txt`) counted as a "picture" | the verdict looked only at colours | still open: detect the viewer / Browser screens |
| `.bas`, `.tap` programs "back in NextZXOS" | the ROM key-wait loop `#0C8F` is shared with the OS | judged by the screen; `.tap` goes through the TAP loader (Sym Shift+Enter, `N`, play speed `S`) |

## 2. Emulator findings

**Status 2026-10-10 (evening): explained and fixed** - see [audit-divmmc-gap-2026-10-10.md](audit-divmmc-gap-2026-10-10.md). The NextZXOS-path failures of Cube, Space (and any NEX read
from the card) were a data corruption in the SPI path: NextZXOS switches the CPU to 14 MHz in the middle of a sector read, and the byte in flight kept its end time in the old clocks, so
three bytes were read twice (D11). The DivMMC rules the findings suspected had their own gaps (D1-D10), all but the RETN aliases fixed. Elite Wireframe could not be reproduced as black.

| Title | Observation | Verified | Hypothesis |
|:--|:--|:--|:--|
| Next3D Demo Cube (`cube.nex`) | **explained, fixed (D11)**: NEXLOAD answers `No such file or dir, 0:2` | the same file in a one-level folder fails the same way (not the path); the direct loader runs it (a rotating cube); the four Next3D files have identical V1.1 headers (2 banks, SP `#8180`, PC `#8183`) | something in the NextZXOS launch path for this file; Circle works through the OS, Cube does not |
| Next3D Demo Space | **fixed (D11, through the Browser NR #12 flips 9 / 12 again)**: direct loader: a rotating filled ship, sun, stars (the owner's recording `elite-zxnext.mp4`); through the Browser: black | direct: NR #12 flips between 9 and 12 every 4 frames (PC `#E49F`); Browser: the program runs and draws into Layer 2 banks 12 / 13 but NR #12 stays 9 (an empty bank), the flip never repeats | the interrupt-driven frame flip is not serviced the same way in the OS-started state |
| Next3D Elite Wireframe | **not reproduced (2026-10-10 evening)**: direct and Browser draw the same static scene and Layer 2 banks 9-11 are filled, the DivMMC is not mapped at the end; black through both loaders | direct: the program maps 8K RAM pages `#14-#17` into MMU slot 0 (`NEXTREG #50`) ~364 times a frame and draws there; Layer 2 banks 9-11 stay all zero; **at that moment slot 0 is `divmmc rom` and `divmmc.automapped` is true** (`NR #50 = #17`), so the writes go to the write-protected DivMMC ROM | the DivMMC automap is active when the program started or became active during the run and is never released; on hardware the program works (the author's demo), so the automap rules differ somewhere |
| the same state seen for the OS-started Space | PC samples in the DivMMC ROM region (`#004E`, `#0052`, `#00E6`, `#2316`, `#240D`) while the program runs | the CPU executes the DivMMC ROM while the program's own code also runs | the interrupt (IM2) is being handled with the DivMMC mapped |
| STNICCC Next, Tile/Tilemap Screen (V1.3), Next Kong Rainbow, Layer2 Fade Out | fixed or by design | see the other documents | |

NEXLOAD (`firmware/tbblue-firmware-sources/asm/nexload/nexload.asm`, lines ~323-349) resets NextREGs before it starts a program unless the NEX header says
not to; among them it clears "DivMMC automatic paging" (comment on `NR #06` bit 4, `res 4,a`) and sets turbo 28 MHz, Layer 2 pages 9 / 12 (NR #12 / #13). In
our RTL reading `NR #06` bit 4 is the DRIVE button NMI enable (`nr_06_button_drive_nmi_en`); which register actually disables the DivMMC automap, and what the board
does to the automap latch when a program starts, is what the gap analysis answered: NR #06 bit 4 is the DRIVE button's NMI enable, NR #0A bit 4 the automap; NEXLOAD does not clear the automap on the current core.

## 3. Next step

Done: a detailed gap analysis of the DivMMC emulation against the RTL (`divmmc.vhd`, `zxnext.vhd`), the esxDOS / NextZXOS ROM behaviour, the wiki and the other emulators,
then a batch implementation with tests and the real programs as oracles: [audit-divmmc-gap-2026-10-10.md](audit-divmmc-gap-2026-10-10.md) (written by that work).
