# ZX Spectrum Next: time travel (TTD)

**Date:** 2026-10-07 · part of [README.md](README.md)

## 1. Recording first

Every phase's recipe and test starts a TTD recording before the run under test (`time_travel` start, run, stop,
seek). Recording holds speed at 1x and turns fast loaders off; the Next's 28 MHz boot therefore records at 1x
host speed: use short sections or the `EnableTurboMode()` rules of the tests only when TTD is off.

## 2. Existing MM_NEXT cases

`TimeTravelManager::PortJournalUnsupportedReason` and the same function in `timetravelcontroller.cpp` return "ZX
Next: its DMA moves data into RAM without IN (not isolated by the first version)". Consequences: checkpoints and
input journaling work; the sealed replay (isolation of port reads) is not available, so replay runs against the
live devices. Both cases stay until the DMA is covered (section 4).

## 3. Serializers and ids

`PeripheralId` is append-only; the next free id at this writing is 62 (after `EvoFlash = 61`); take the next free
value at implementation time. Planned blobs (version byte first, `OnTtdStateLoaded` rebuilds derived data):

| Blob | Content |
|:--|:--|
| `NextState` | NextREG array and latches, port enables, MMU slots, paging latches, config-mode and reset flags, speed, joystick/I-O mode, copper RAM (2K) and position, palettes (8 x 256 x 16-bit), sprite attributes and pattern RAM (16K), clip windows/indices, tilemap and Layer 2 latches, scanline latches |
| `NextDma` | register set, counters, mode, prescaler, status |
| `NextCtcIm2` | channels, IM2 status/enables/priority state |
| `NextSpiSd` | `#E7` select, `#EB` last byte, card protocol state ×2 (not sectors: media rule), swap bit |
| `NextDivMmc` | `#E3`, automap latches, 128K DivMMC RAM as a TTD v2 memory region when available |
| `NextI2cRtc` | line states, DS1307 RAM/pointer/time base |
| `NextUart` | both UARTs: prescalers, FIFOs, flags, peer state |
| `NextAudio` | active AY, DAC aliases, stereo flags; AY chips use `TurboSound`, a DAC uses `Covox` |
| `NextFlash` | FPGA flash command state; the array is a region |
| `NextInput` | keyboard matrix extras, PS/2 stream, mouse counters (`KeyboardMatrix`, `KempstonMouse` reused where equal) |

Memory: the 2 MB array is the RAM region checkpoint (key frames + dirty XOR deltas; 128 16K pages, within
`MAX_RAM_PAGES`). CPU state: Z80N library boundary synchronized like the Z84C15 (`Z80State::boundary`, engine
`InvalidateBoundary` after a restore). Restore order is ascending id; the slot table is rebuilt after all blobs.

## 4. Isolating port reads later

DMA transfers memory-to-memory without `IN`. A journal for the Next would also record the DMA's port reads and be
replayed at the DMA's cycle; alternatively sealed replay is allowed only while the DMA is idle in the checkpoint.
Decision deferred to N11 after measuring how often NextZXOS uses the DMA.

## 5. Inputs journaled

Keys, PS/2 bytes, mouse, joysticks (connector 1/2), buttons (M1, DRIVE, reset), media changes (card swap ends the
session), RTC reads (emulated time while recording). Hard reset ends the session.

## 6. Cost

Checkpoint size dominated by the 2 MB array plus sprite/pattern RAM and copper; a frame-cost measurement (like
the Sprinter's 3.6 ms/frame figure) is taken in N11; target none before measuring.
