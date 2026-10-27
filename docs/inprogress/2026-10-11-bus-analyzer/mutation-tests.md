# Bus analyzer: driver mutation tests

**Date:** 2026-10-11 · part of [README.md](README.md) · owner request 2026-10-11: "test cases and mutants that prove the
analyzers catch problems in drivers; a base scenario and forks of it for every platform with a bus, plus NedoOS, DSS
and the rest"

## 1. Why mutants

A check that never fires proves nothing; one that fires on a correct driver is noise. Each rule is proved by a pair:
the reference driver runs clean (no finding), and a mutant - the reference with one typical driver bug - makes exactly
the expected finding, with the PC of the mutated instruction. The same mutants under the `slow` card profile show the
timing bugs (a too-short poll loop) that no rule can see on the ideal card: the driver then fails visibly.

## 2. The base scenario

A small SD driver in Z80 assembly (assembled in the test by `Z80TextAssembler`), parameterized by the host's ports:

1. select the card; 10 bytes of `#FF`;
2. CMD0 until R1 `#01`; CMD8 `#1AA`; CMD55 + ACMD41 (HCS) until R1 `#00` (poll limit `ACMD41_TRIES`);
3. CMD58 (OCR);
4. CMD17 of block N: poll for R1, poll for the token `#FE` (limit `TOKEN_TRIES`), read 512 bytes into RAM, read the
   2 CRC bytes;
5. CMD24 of block N+1: R1, a gap byte, `#FE`, 512 bytes, 2 CRC bytes, the data response, poll busy until `#FF`
   (limit `BUSY_TRIES`);
6. CMD13 (status); deselect; `HALT`-free end marker (`JR $` at a known label).

The test runs it to the end marker and asserts: the block read is the medium's, the block written is on the medium,
the protocol events are the expected sequence, **no finding**.

## 3. Mutants (one bug each)

| Id | Bug | Rule expected | Where |
|:--|:--|:--|:--|
| M1 | no busy poll after the CMD24 data response | `command_while_busy` | the CMD13 after the write |
| M2 | reads 512 data bytes but not the 2 CRC bytes | `command_drops_answer` | the next command |
| M3 | reads only half of the block | `command_drops_answer` | the next command |
| M4 | deselects after 3 bytes of a command (an ISR-like abort) | `deselect_mid_command` | the deselect |
| M5 | CMD8 with a wrong CRC | `crc_error` | CMD8 |
| M6 | TS-Conf: starts the SPI DMA for the block and reads `#57` without polling DMA_STATUS | `dma_collision` | the `IN` |
| M7 | TS-Conf: deselects (`#77`) while the SPI DMA runs | `dma_collision` | the `OUT` |
| M8 | Next: reads `#EB` back to back with `INIR` at 28 MHz (no gap) | `early_access` | the `INIR` |
| M9 | Next: changes `#E7` right after a write to `#EB` | `select_mid_byte` | the `OUT` |
| T1 | token poll limit 255 (an 8-bit counter) | none on `ideal`; under `slow` the driver gives up: the read fails and the trace ends in `#FF` polls | - |
| T2 | ACMD41 poll limit 20 | none on `ideal`; under `slow` init fails | - |
| T3 | no busy poll limit (an endless loop) on a write | none; under `slow` it still ends (the busy ends on time) - the control case | - |

A mutant is a text patch of the reference source (a line replaced or removed), so each one stays readable in the test.

## 4. Forks per platform

| Fork | Machine | Bus | Ports / differences | Mutants |
|:--|:--|:--|:--|:--|
| F-TS | TS-Conf (`TsConfFixture`) | `zc.spi` | `#77` / `#57`, IN returns the previous exchange; SPI DMA | M1-M7, T1-T3 |
| F-EVO | ZX-Evo BaseConf (`ATM3`) | `zc.spi` | not a Z80 fork: after a reset the Evo runs in shadow mode (`#77` is the ATM port, the chip select is the `#8057` write, the windows map the boot ROM); the master is TS-Conf's `ZControllerSpi` (F-TS), the Evo's real SD software is R-NEDOOS | - |
| F-NEXT | ZX Next | `next.spi` | `#E7` (`#FE` card 0), `#EB`; 16 CPU clocks per byte | M1-M5, M8, M9 |
| F-DIVMMC | 48K + DivMMC card | `divmmc.spi` | `#E7` (bit 0 card 0), `#EB` | M1-M5 |
| F-NEOGS | NeoGS card (the master driven like the GS ROM does, no host program) | `neogs.sd` | `SD_SEND` / `SD_READ`, SCTRL; restart = early access | M1, M2, restart mid-byte |
| F-ATA | any IDE host, the Sprinter's for DSS | `ide0` (ATA) | the task file, `#1F7` status / command | A1 data read without DRQ, A2 a command while DRQ (unread sector), A3 a register write while BSY |

The ATA bus (B6, pulled forward for DSS): the tap sits in `AtaChannel` (every IDE board shares it): register
reads / writes, commands decoded, data words, with the checks `ata_data_without_drq`, `ata_command_drops_data`,
`ata_write_while_busy`.

## 5. Real software (the analyzer must stay quiet on drivers that work on boards)

| Case | Machine, medium | Bus | Assertions |
|:--|:--|:--|:--|
| R-WC | TS-Conf, TS-BIOS + Wild Commander from the SD card (`tsconf_boot_test` BOOT5) | `zc.spi` | the init sequence and block reads in the trace; findings: none (or each listed with its explanation) |
| R-NEDOOS | ZX-Evo, NedoOS from a host folder (`ZXEvoErs_Test.NedoOsBootsFromAHostFolder`) | `zc.spi` | as above, up to the `M:/bin>` prompt |
| R-EVO-MENU | ZX-Evo main menu SD boot (`SdCardBootFromAHostFolder`) | `zc.spi` | as above |
| R-UNODOS | 48K + DivMMC, UnoDOS 3 (`DivMmcPaging` UnoDOS test) | `divmmc.spi` | as above |
| R-NEXTZXOS | ZX Next, the boot ROM loading TBBLUE.FW from `testdata/machines/zxnext/card` | `next.spi` | **one known finding per block**: the boot ROM drains the CRC with `IN A,(#EB); NOP; IN A,(#EB)` (#0522 / #0525), 15 CPU clocks apart while a byte takes 16 (`spi_master.vhd`: counter on `i_CLK_CPU`; `spi_wait_n` holds only the DMA) - early on the board too, harmless (the CRC is discarded); pinned as `early_access@0x0525` only |
| R-DSS | Sprinter, DSS from the HDD image | `ide0` | ATA commands (IDENTIFY, READ SECTORS) in the trace; findings: none |
| R-*-SLOW | each R case again with the `slow` card | | does the software still boot? A failure is a finding about the software, recorded, not fixed here |

Findings on real software are facts about that software: a test that sees one records it with the reason and keeps
the count fixed, so a change in the emulator that adds or removes one shows up.
