# Wild Sound: TODO

Part of [README.md](README.md). Phases from [design.md](design.md) §12.

## W1: core bus and streams

- [x] `wildsound::WildSoundCore`: register file, chip select latches, read pipeline, echo storage
- [x] unlock state machine, identification registers and strings
- [x] `#F1` / `#F2` / `#F3`: command bytes, packets, FIFO, status frame with crcX
- [x] `WildSoundStorage`: 999 424 bytes, 64 descriptors, track / instrument / sample parsing and validation
- [x] commands NOP, CLEAR, ERASE, XMAST, XMADO, XMBST, XMBDO, DIGPLAY (player stub until W2)
- [x] `sprinterisa::IsaWildSoundCard`: port decode, timestamps, RESET DRV, Describe, fixed-size state
- [x] `CardKind::WildSound = 8` and every kind list (config, BIOS create option, MCP, CLI, OpenAPI)
- [x] tests: `wildsoundcore_test` (bus and streams), `isawildsoundcard_test`
- [x] check: prosiak.exe prints the version, uploads to POS = SIZE (TTD)

## W2: sound

- [x] `ICardAudio` split out of `ICard`; `SoundManager` slot-card list on `ICardAudio*`
- [x] `AudioSourceType` rows `WildSoundXm`, `WildSoundAy1`, `WildSoundAy2`
- [x] AY pair (in `WildSoundAudio`): two `SoundChip_AY8910` at 1 774 400 Hz, writes applied on their sample, the chip's ABC panning, XM / AY exclusivity
- [x] `WildSoundXmPlayer`: FT2 replayer (timing, frequencies, effects, volume column), mixer, state
- [x] tests: `wildsoundxmplayer_test` (the AY pair is covered by the live runs; `wildsoundaypair_test` open)
- [x] check: prosiak.exe heard; against openmpt123 (linear interpolation) over 25 s: envelope correlation 0.972, zero-crossing 0.920, no tempo drift

## W3: tools and test data

- [x] `tools/machines/sprinter/wildsound/wsconv.py` (XM, S3M → chunks) + its tests on the author's converted tracks (byte-equal to MasonXM output where MasonXM is right)
- [x] `wslink.py` (Python LZSA2 compressor; EXE from the prosiak loader)
- [x] `wsay/` PT3 player for the card, `wsaybuild.py` (sjasmplus: the repository's zxasm converts dialects, it does not assemble); against a Pentagon playing the same player: envelope correlation 0.90-0.98, no drift, pitch x 1.014 (the card's 1.7744 MHz clock)
- [x] `testdata/sound/wildsound/`: prosiak, 6 XM, 3 S3M, 3 PT3 programs, sources, README, `wildsound.ucompose.yaml`, reference envelopes (JSON); every one run and compared (README)
- [ ] `sprinterwildsound_test` (machine, TTD replay, env-gated prosiak run)
- [x] acceptance §3.1 (prosiak), §3.2 (XM 4 / 8 / 10 / 32 ch, S3M 8 / 16 ch, PT3 one chip and TurboSound: envelope correlation 0.91-0.998 against openmpt123 / the Pentagon), §3.4 (zero warnings, gcc 16)
- [ ] acceptance §3.3: TTD replay bit-exact audio (checked so far: the card's state and `wildsound.flash` restore at seeks)

## W4: integration

- [x] TTD region `wildsound.flash` (`TTDRegionId::WildSoundFlash = 19`), `memory_access` listing
- [x] `audio_wildsound` on WebAPI, MCP, CLI, Lua, Python, OpenAPI
- [x] `docs/features/sprinter-slots.md` section, `.recipe/machines/sprinter-sound.md` recipe (verified)
- [ ] peripherals survey status (item 3 / Q3, wiring table), ISA research §7.1 row
- [x] gcc 16 -O3 check of the changed core and test files, zero warnings (clang)
- [ ] docker linux build (`docker/linux/build.sh --test`)

## Found on the way (not the card)

- [ ] a second SPRINTER created in the same process after a destroy stops in the BIOS (logo / "Boot from HDD OK");
  reproduced on master `9536bebb1` without the card. Cause (traced): when unreal-qt adopts a new machine,
  `TtdWidget::applyJournalPreference` -> `TimeTravelController::SwitchWriteJournal` does `Pause(false)`, allocates
  the journal (10-20 ms), `Resume(false)`. A pause from automation inside that window is a no-op (already paused);
  the silent resume then unparks the main loop while `RunNFrames` steps the Z80 on the automation thread: two
  drivers at once, the boot diverges (the BIOS lands in a random place, often the POST error blinker at `#01DF`).
  Not the disk format and not the instance number: timing only (an instance created 2 s before the flow boots
  5/5). Fix: a pause owner/serial so a silent resume does not lift someone else's pause, and `Resume` must not
  unpark the loop while `IsDirectStepping()`
- [ ] `capture_media` URL-encodes `path` and writes `%2F...` files into the emulator's working directory

## Later

- [ ] the card's own AY balance (A 100/20, B 68/68, C 20/100) and the HQ FIR path for the AY pair
- [ ] HUD activity indicators for the WS rows (`AudioSource` in notifications.h, Qt hudmodel)
- [ ] PWM duty (`#18-#1A`) in the AY tone generator
- [ ] modelled busy time (ERASE, flash writes)
- [ ] the same core on the ZX AY socket (AYX-32 with the Wild Sound firmware, TR-DOS loaders from the author's repository)
