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

**Landed (2026-10-11 / 12): blob `NextState` (id 62, image version 5) and blob `NextSystemArea` (id 63).** `NextState` is one fixed-length image
(`NextStateCodec`, about 30 KB): the NextREG file and select latch, reset bits, the memory map (MMU slots, paging bits, the DivMMC / Multiface /
Layer 2 views, config mode; the slot table is rebuilt after a load), the video registers with the 8 palettes and clip windows, the sprites, the
copper (list, position, mode, clock, the held instruction), the DMA, the CTC, the interrupt controller, the I2C bus with the DS1307, the Multiface
and DivMMC latches, the decoder's SD SPI and timing latches, the three AY chips with pan and mode bits and the four DACs, both SD cards' protocol
state, the extended keys and both joystick pads. `NextSystemArea` is the 256 KB system area as plain bytes: the ROMs the firmware loads in config
mode (pages 0-7) and the DivMMC RAM (pages 8-15). Measured (a program writing NextREGs, 9 checkpoints): 196 KB of history in all, i.e. the engine
keeps an unchanged 4 KB piece once, so the 256 KB blob costs next to nothing per frame while it does not change. **Not in it:** the audio render
clock (resyncs), the UARTs (not built), the media of the SD cards (the media rule).

**Fragile golden (2026-10-13).** `testdata/slots/fitted-devices.txt` pins, per machine folder, the size of every blob the TTD
registry captures (`ttd id:size ...`, written by `DescribeFittedDevices` in `core/tests/emulator/slots/slotmanager_test.cpp`). For the Next
that is `62:<size>` of the captured `NextState` blob, so every change of the Next's recorded state (a new field, an image version bump, a
different reset value) changes the number and fails `Configs/SlotManagerShipped_Test.FitsTheDevicesOfMaster/next`, also when the change
itself is correct and its own tests pass. That broke CI on 2026-10-13 (fixed by `fced9a3da`). Rule: a commit that touches `NextStateCodec`
or anything it saves regenerates the golden in the same commit:

```bash
UNREAL_SLOTS_FITTED_UPDATE=1 tools/build/test.sh --gtest_filter='*SlotManagerShipped_Test.FitsTheDevicesOfMaster/next*'
git diff testdata/slots/fitted-devices.txt   # only the `62:` number of the next block may differ
```

Open: compare only the presence of id 62, as the test already does for `EthernetNics` (`:var`), since the layout of `NextState` is guarded
by its own image version and codec tests; see [TODO.md](TODO.md), "Remaining".

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
