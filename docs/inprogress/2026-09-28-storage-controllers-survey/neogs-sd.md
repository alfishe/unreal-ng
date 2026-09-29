# NeoGS SD card

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Device** | NeoGS sound card (any host machine), SD card on the card's own Z80 |
| **Designs** | [neogs-tdd.md](../2026-09-19-general-sound/neogs-tdd.md) §3.7, §5.4-§5.5; [integration-neogs-sd.md](../2026-09-28-storage-manager/integration-neogs-sd.md) |
| **State** | **done, on master** (slot `sd.ngs`, folder cards, TTD barrier) |
| **Effort left** | none for storage; follow-ups belong to PLAN #45 |

## 1. Hardware

The SD card hangs off one of three SPI masters in the NeoGS FPGA; only the card's own Z80 (not the
Spectrum) reaches it.

| Card port | Direction | Meaning | Source |
|---|---|---|---|
| `#11` SCTRL | R/W | write: every bit set in d5..d0 takes the value of d7; bit 0 = SD /CS; reset `#0B` (SD deselected) | [neogs-tdd.md](../2026-09-19-general-sound/neogs-tdd.md) §3.7 (FPGA `ports.v:577-623`) |
| `#12` SSTAT | R | `{0000, MCRDY, SD_WP, SD_DET, DREQ}`; WP and DET are the slot switches, low-active | same |
| `#13` | W / R | SD_SEND (starts an exchange) / SD_READ (last received byte) | same |
| `#14` | R | SD_RSTR: last byte **and** a new exchange sending `#FF` (the Z-Controller read rule) | same |

Timing: a byte takes 16 card clocks at SCK = clock / 2; a read before the byte completes returns
the previous byte (`NeoGSSpi`, FPGA `spi.v`). Reset: the card's own FPGA reset; the Spectrum's reset
reaches it only through the GS reset path.

**Switch polarity: the sources disagree.** The NeoGS document says SDDET = 1 means **no card** and
SDWP = 1 means **write-protected** (`neogs/docs/GS_info_v0.4.2.2.txt:329-341, 380-386`); MAME reports
both bits as 1 when a card is present (`devices/bus/spectrum/zxbus/neogs.cpp:397-405`); UnrealSpeccy
sets SDDET at init, i.e. "no card" (`nedopc/gsz80.cpp:649-650`). The NeoGS firmware never reads
either bit, so it is low-risk; unreal-ng follows its design (both low-active,
[neogs-tdd.md](../2026-09-19-general-sound/neogs-tdd.md) §5 marks the polarity as unverified). Worth a
one-line check of SDWP against the document.

## 2. unreal-ng

`SdCardSpi` (shared with ZX-Evo), `NeoGSSpi` (the three masters with exact byte timing), the card's
DMA (`neogsdma`) talking to the same `SdCardSpi`, slot `sd.ngs` registered only while
`[SOUND] GSType=NeoGS`, card detect and WP from the slot, `HostFolderFat` volumes (FAT16 and FAT32
boots tested), writes as TTD barriers through `MediaManager::NoteWrite`.

Tests on master: `SoundChip_NeoGS_SdBoot.LoaderBootsNeogsRomFromAHostFolder`,
`NeoGSMedia_Test.SdSlotListedOnlyWhileNeoGSIsFitted`, `neogsspi_test`.

## 3. Why it matters for this survey

It is the proof that one `SdCardSpi` serves three very different hosts (Z-Controller on the
Spectrum side, an FPGA SPI master with timing on the NeoGS side, DMA on both). DivMMC, the ZX Next
and TSConf add only their port decode on top.
