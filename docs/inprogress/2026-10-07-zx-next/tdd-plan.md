# ZX Spectrum Next: test plan

**Date:** 2026-10-07 · part of [README.md](README.md) · rules: [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase. No `sleep_for` (use `TestWait`), under 50 ms per test (boot-bound ones carry a
justifying comment), `EnableTurboMode()` on boot-bound tests, scratch via `TestPathHelper::GetUniqueTestScratchPath()`,
`*_test.cpp` file names, no copyrighted ROMs in the repo (tests skip when the firmware set is not provisioned).

**Acceptance corpus (N0 finding):** [ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) ships every
test as source plus a prebuilt `.snx` and photos of the same screen on real boards. Each phase that renders its
screen (NextReg defaults, Z80N, Copper, DMA, Layer 2, sprites, mixing, timing) runs the matching `.snx` under TTD
and compares our screen to the real-board photo by eye once, then pins a perceptual hash of our own frame in a
local (not committed, binary-free) golden file. The binaries are test inputs and stay outside the repository.

| Phase | Test file (under `core/tests/emulator/machines/next/` unless noted) | Cases |
|:--|:--|:--|
| N0 | `nextregtable_test.cpp` | register/port data files equal the research documents; every NR has a reset value row |
| N1 | `core/tests/3rdparty/z80n_test.cpp` | each Z80N opcode: result, flags, T-states (table from the wiki; open items from Q7 pinned once read); `NEXTREG` leaves the select latch alone; base-Z80 suite on the fork |
| N2 | `nextcreate_test.cpp`, `nextmmu_test.cpp`, `nextports_test.cpp` | creatable; `list_models`; reset slot values 255,255,10,11,4,5,0,1; `#7FFD/#DFFD/#1FFD` set slots; all-RAM table; ROM select; `ports.txt` rows (precedence cases); 48K BASIC prompt |
| N3 | `nextspeed_test.cpp`, `nexttiming_test.cpp` | NR `#07` ratio and frame T; INT/line positions per machine timing (pinned numbers with source comments); contention on/off; floating bus; 50/60 Hz |
| N4 | `nextaudio_test.cpp` | AY chip select pattern; mono/stereo; DAC alias ports and enables; TurboSound off freezes chip |
| N5 | `nextctc_test.cpp`, `nextim2_test.cpp`, `nexti2c_test.cpp`, `nextuart_test.cpp` | CTC counting/chaining; IM2 priority/RETI; DS1307 transaction bit-banged; UART prescaler/FIFO flags |
| N6-N7 | `nextvideo_*_test.cpp` | per layer golden line buffers from register setups (reference: MAME/jnext output for the same setup, stored as small hashes); compositor orders; blend; copper WAIT/MOVE ordering; sprite collision/overtime flags; palette auto-increment |
| N8 | `nextdma_test.cpp` | register programming, mem-mem, mem-port, burst + prescaler timing, CPU held |
| N9 | `nextboot_test.cpp` (real firmware, skipped if absent) | boot trace equals golden; menu text; `sd.next0` required error; swap bit; second card; `config.ini` write lands in the change layer |
| N10 | `nextinput_test.cpp` | joystick modes, mouse ports, keymap, MF NMI, FDC traps |
| N11 | `loadernex_test.cpp`, `nextttd_test.cpp`, `nextsnapshot_test.cpp` | NEX header/bank order/entry; SNX as SNA; TTD checkpoint round trip bit-exact; seek back over a boot; machine transfer matrix |
| all | `ModelsRegression` row, `emulatormanager_test.cpp`, WebAPI/CLI/Lua/Python round trips | model name, report endpoints exist, other models' fingerprints unchanged |

Use of external suites: the programs of [ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests)
(`Tests/`: ULA, sprites, interrupts, timing, ...) are built by the owner's toolchain into a gitignored
`testdata/` folder, run headless with TTD recording, and compared on screen hash or memory result; skipped if absent.
Performance gates: `BM_HostFrame_*` A/B for other machines at N1/N2; `BM_HostFrame_Next_*` added at N2 and N6.

## Card, DivMMC and boot tests

The layered plan (L0 SPI and select, L1 SD protocol transcripts, L2 DivMMC paging truth table, L3 automap timing, L4 our own guest driver, L5 boot ROM + loader, L6 firmware boot, L7 API conformance, L8 cross-check) is in [esxdos-and-sd.md](esxdos-and-sd.md) section 8. CPU library and engine tests: [design-integration.md](design-integration.md) section 6.

## Evidence and external suites

Which suites exist, their grade and the order of use: [verification-program.md](verification-program.md).
