# Wild Sound ISA card (Sprinter): requirements

**Date:** 2026-10-09 · part of [README.md](README.md) · design in [design.md](design.md) · evidence in
[reference-firmware.md](reference-firmware.md) (the card's firmware, read from the author's sources) and
[prosiak-wildsound](../../disasm/software/sprinter/prosiak-wildsound/README.md) (the one Sprinter program, taken apart)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | A **Wild Sound** card (Robus, STM32F405, "AYX-32 / Wild Sound II" firmware) that fits a Sprinter ISA slot, answers the Z80 the way the firmware does, and plays what the Z80 uploads |
| G2 | **Sound as intended, not as the 2021 firmware renders it** (owner's decision 2026-10-09): the uploaded XM plays with FastTracker 2 semantics, the two AY chips play like AY-3-8910s. The firmware's bugs (reference-firmware §D.2, §D.3, §E) are listed, not reproduced |
| G3 | The bus protocol is the firmware's, byte for byte (the read pipeline, unlock, stream commands, status frame with its CRC): a program written for the real card works unchanged |
| G4 | Success criterion: **several XM, S3M and AY (PT3) tracks play on the emulated Sprinter** from a disk built out of `testdata/sound/wildsound/`, through Sprinter programs that drive the card; `prosiak.exe` plays its tune |
| G5 | No cost for other machines and for a Sprinter without the card: the card is one more `[ISA]` slot kind |
| G6 | TTD, automation and tests as for every other ISA card |

Not goals: the firmware's TSL flash update (`#E8`, `#E4`, `#E9`), the UART bridges (`#F4-#F7`), the
"Phase", "Synthesia" and SD-card features of later firmware (no source for them), bit-exact firmware audio,
a ZX-Spectrum-side (AYX-32 on the AY socket) variant of the card (possible later on the same core, design §9).

## 2. Requirements

Confidence: **H** = from the firmware source or a traced program; **M** = one source, no contrary one;
**O** = open, decided in design and marked there.

### 2.1 Bus

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R1 | ISA I/O, two ports at the card's base: base + 0 write = AY **address** cycle, base + 1 write = AY **data** cycle, base + 1 read = AY **read** cycle. Base `#0000` (the only one a program uses) | H (port 0 / 1 writes, port 1 read) / O (port 0 read, other bases) | prosiak trace; ISA journal |
| R2 | Every bus event ends by preparing the byte of the next read; a read returns the prepared byte, then runs its side effect (reference-firmware §A.2) | H | `ChipAY_Interrupt.c:846-924` |
| R3 | Registers `#00-#0F` per chip, `#10-#1F` per chip extension (PWM duty `#18-#1A`, inert `#10-#15`), `#20-#FF` shared echo storage; chip select by address `#FE` (chip 1) / `#FF` (chip 0) with the per-chip address latch | H | §A.4 |
| R4 | `#F0` unlock state machine with "Wild Sound Enable ", the mode byte, `#00` → `#DF`, then the version string "165931.23042021" and `#00` | H | §A.5, §F |
| R5 | `#F1` stream command (u16 command + u32 size), `#F2` packets (length + bytes, FIFO 127, busy), `#F3` 32-byte status frame (command / busy, size, pos, OUT free, IN size, `FA 3D`, crcX) | H | §A.6-A.8 |
| R6 | Identification registers `#E0` (55 AA), `#E1` status, `#EC` version words, `#ED` copyright, `#EE` build string, `#EF` core clock, `#EB` uptime | H | §A.4, §F |
| R7 | RESET DRV resets the card (allocator, player, chips, unlock, FIFO); the uploaded data is forgotten | H | §A.10 |
| R8 | `#E3`/`#E1` commands, the UART registers and TSL firmware upload: stored and echoed, no action | H | not goals |

### 2.2 Stream commands and storage

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R10 | NOP, CLEAR (back to AY mode), ERASE (free everything), XMAST (track pack), XMADO (no-op), XMBST (instrument table), XMBDO (sample bytes, any number of commands), DIGPLAY (start the XM) | H | §B.2 |
| R11 | Storage of 999 424 bytes and 64 descriptors as on the card; a request past them fails the command (the firmware runs into garbage, §B.1: we stop instead and report it) | H / design | §B.1 |
| R12 | A command step runs when its packet completes, inside the bus write: the busy flag is never seen set, ERASE and flash writes take no time. The loaders only wait for busy = 0, so they run unchanged; TTD stays deterministic without a card clock | O (real timing) | design §4.4 |
| R13 | XM and AY are exclusive outputs, as in the firmware: DIGPLAY switches to XM, CLEAR / ERASE back to AY | H | §D.5 |

### 2.3 Sound

| ID | Requirement | Conf. |
|:--|:--|:--|
| R20 | XM player with FT2 semantics over the uploaded data: pattern decoding, speed / BPM, every effect and volume-column command of FT2 (memory, vibrato / tremolo, panning, global volume, key-off, pattern delay, retrigger, note cut / delay, fine variants), linear and Amiga frequency tables from the header flag; only what the upload carries (one sample per instrument, no envelopes, fadeout or auto-vibrato: the format drops them) | design §5 |
| R21 | Up to 32 channels, 8-bit samples with forward / ping-pong / no loop, stereo by instrument and effect panning, interpolated resampling to the emulator's core rate | design §5 |
| R22 | Two AY-3-8910 at 1 774 400 Hz with standard envelopes, noise LFSR and logarithmic volume; ABC stereo with the card's balance (A 100/20, B 68/68, C 20/100); the PWM duty registers change the tone's duty cycle | design §6 |
| R23 | Mixer rows of their own (`Wild Sound XM`, `Wild Sound AY 1`, `Wild Sound AY 2`) with the usual volume / mute controls; the AY rows go through the AY voicing chain | design §7 |

### 2.4 Integration

| ID | Requirement |
|:--|:--|
| R30 | Config: `[ISA] SlotN=WILDSOUND` (and the create-time `isa_slot1/2`, MCP, CLI, OpenAPI kind lists); the default slots stay ZXBUS / NE2000 |
| R31 | TTD: the card's state records and replays bit-exactly; its 1 MB storage is a TTD memory region (`wildsound.flash`), readable with `memory_access` |
| R32 | Automation: `/state/isa` describes the card (mode, unlock, command / busy / size / pos, storage use, track and player position); an `audio_wildsound` aspect in WebAPI, MCP, CLI, Lua and Python (parity rule) |
| R33 | Tests: bus protocol (unlock, status frame and CRC, packets, flow control), the uploaders' sequence, the XM player against reference renders, the AY chips, TTD round trip, a Sprinter run of the test programs |
| R34 | Docs: `docs/features/` page, `.recipe/machines/sprinter-isa.md` / `sprinter-sound.md` sections, peripherals-survey status, environment variables if any |

### 2.5 Test material and programs

| ID | Requirement |
|:--|:--|
| R40 | `testdata/sound/wildsound/` holds: `prosiak.exe`; Sprinter programs that play other XM, S3M and PT3 (single-AY and TurboSound) tracks through the card; the source tracks; a README with origin of each file; a `*.ucompose.yaml` that turns the folder into a disk for the Sprinter |
| R41 | A converter (Python, under `tools/machines/sprinter/wildsound/`) from XM and S3M to the card's upload format (reference-firmware §C.5), with the S3M effects mapped to their XM equivalents |
| R42 | A builder that links converted tracks into Sprinter EXE files on the `prosiak.exe` loader (its VM, packer and player, [prosiak-wildsound](../../disasm/software/sprinter/prosiak-wildsound/README.md)) |
| R43 | A PT3 player for the Sprinter that writes the card's AY registers (TurboSound: both chips), built from S.V. Bulba's PT3 player source with the repository's assembler |

## 3. Acceptance

1. `prosiak.exe` from `testdata/sound/wildsound/` prints "Wild Sound ... ISA #n" with the version string and
   plays PROSIAK; the TTD shows its upload complete (POS = SIZE for every command) and DIGPLAY.
2. At least three more XM tracks (4, 8 and 10 channels), two S3M tracks and two PT3 tracks (one TurboSound)
   play from their EXE files on the same disk; each is heard and its render matches a reference render of the
   source module (XM / S3M: a libxmp or OpenMPT render, compared by envelope / spectrum within a stated
   tolerance; PT3: the emulator's own AY rendering of the same module on a Pentagon).
3. `ttd start` on a Sprinter with the card, a recorded upload and playback, and a replay produce the same audio
   bit for bit.
4. Zero warnings on the four toolchains; core-tests pass; no change to existing Sprinter tests and fixtures.
