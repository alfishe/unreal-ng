# ZX-MultiSound: five sound devices on one card

The **ZX-MultiSound** by UzixLS is one ZX-bus card that carries five sound devices at once: a TurboSound FM, a SAA1099,
a General Sound, a SounDrive and a General MIDI synthesizer. Programs written for any of those devices play on it
unchanged. The emulator models the card as the real board is built: the same ports, the same control byte, the same
mixer wiring, the same quirks.

The card is **not fitted by any shipped configuration** (owner decision): you put it into a slot yourself, in the
configuration file, in the Qt slots window or through automation. This page says what the card is, where it fits, how
to fit it, what you hear and see, and which software to try.

Step-by-step automation (fit, play, capture each source, MIDI panic): [.recipe/peripherals/multisound.md](../../.recipe/peripherals/multisound.md).
How cards, slots and conflicts work in general: [slots.md](slots.md). Design and test notes for developers:
[docs/inprogress/2026-10-03-zx-multisound/](../inprogress/2026-10-03-zx-multisound/TODO.md).

## Words used here

| Word | Meaning |
|---|---|
| **AY / SSG** | The classic three-channel square-wave sound chip of the ZX Spectrum 128 (AY-3-8910 / YM2149). The YM2203 contains the same three channels; on that chip they are called the **SSG** part |
| **FM** | Frequency-modulation synthesis: the YM2203's three extra channels, the "FM" in TurboSound FM |
| **TurboSound FM (TSFM)** | Two YM2203 chips behind the AY ports `#FFFD` / `#BFFD`, switched by a control byte. Usually a separate board in the AY socket; the MultiSound has its own |
| **SAA1099** | The Philips six-channel chip of the SAM Coupe |
| **General Sound (GS)** | A sound card with its own Z80, RAM and four 8-bit sample channels, used for MOD music and sound effects |
| **SounDrive** | Four 8-bit sample outputs (DACs) that the Spectrum writes directly; on this card they share the GS's DACs |
| **General MIDI (GM)** | A standard set of 128 instruments and a drum kit, addressed by MIDI messages |
| **Bank (SoundFont)** | The file of instrument samples the MIDI synthesizer plays (`.sf2`) |
| **Control byte** | A value `#F0-#FF` written to `#FFFD`: it selects the YM chip, the read mode, mutes or unmutes FM and starts or stops the SAA clock |
| **DIP switches** | The switches on the board that turn each part on or off (option `dip`) |
| **Slot** | A place for a card on one of the machine's buses: `zxbus.1`, `zxbus.2`, ... ([slots.md](slots.md)) |
| **Shadow** | The card answers a port, so the device the board has on that port stays silent |
| **TTD** | Time-travel debugging: recording a run and moving back and forth in it |

## What is on the card

| Part | What it is | Ports | Mixer rows |
|---|---|---|---|
| TurboSound FM | 2 x YM2203 (3 FM + 3 SSG channels each) | `#FFFD`, `#BFFD`; control byte `#F0-#FF` | `MS SSG 1`, `MS FM 1` (chip 0), `MS SSG 2`, `MS FM 2` (chip 1) |
| SAA1099 | six tone channels, noise, two envelopes, 8 MHz | `#01FF` address, `#00FF` data | `MS SAA` |
| General Sound | Z80 at 16 MHz, GS ROM 1.05b, 1 MB or 2 MB RAM, four channels | `#B3`, `#BB` | `MS PCM` |
| SounDrive | four DAC channels, the same DACs as the GS | `#0F`, `#1F` (left), `#4F`, `#5F` (right) | `MS PCM` |
| MIDI synthesizer | Dream SAM2695 General MIDI chip, fed by a serial line from the first YM2203's I/O port (register 14 bit 2) at 31 250 baud | none of its own | `MS MIDI` |

The MIDI part needs no port: a program sends MIDI bytes by switching one bit of YM register 14 on and off at the MIDI
speed, as on the original Spectrum 128's serial port. MIDI players for the card do exactly that.

## Which machines take it

The card is a ZX-bus card that needs IORQGE (the signal that lets a card hide a port from the board) and +12 V.

| Machine | Fit | What happens to the board |
|---|---|---|
| Pentagon (128 / 512 / 1024) | `real`; the Pentagon 128 board has no connector, so its ZX-bus is retrofitted and the reports say the card is bolted on | the board AY stays fitted but is **shadowed**: it gets no cycle and its row reports `shadowed by zxbus.N` |
| Profi Scorpion (`PROFSCORP`) | `real` | the board AY is shadowed, as on the Pentagon |
| ZX-Evo Baseconf (`ATM3`), TS-Conf (`TSL`) | `real` | the socketed YM2149 is **taken out of its socket** (the board would otherwise answer `#FFFD` reads too): no board AY row, the report says `taken out of its socket for zxbus.N` |
| ATM Turbo 2 / 2+ (`ATM450`, `ATM710`) | `adapter`, behind the CPU-socket ZX-bus adapter (`atm-cpu-socket-zxbus`), as the shipped configs fit their NeoGS | the board AY is shadowed |
| Scorpion ZS-256 (`SCORPION`) | `unrealistic`: its ZX-bus has +12 V only on the control port | needs the override |
| Sinclair 48K / 128K / +2 / +2A / +3, Profi, Sprinter | `unrealistic`: the bus lacks the signals; the planner names the adapter | needs the override |

An `unrealistic` fit works in the emulator as if the bus carried every signal the card needs; automation accepts it
only with the override flag (`--replace`, `replaceIfIncompatible`), the Qt window asks first.
`slots catalog` shows how the card fits the machine you have now.

### What it cannot share the machine with

The card does the jobs of several other cards, so the planner does not let it sit next to them (two cards answering the
same ports would fight):

| Next to the card | Outcome | Why |
|---|---|---|
| a TurboSound FM (`tsfm`) or TurboSound (`ts`) in the AY socket | the socket card goes; the socket returns to the machine's own chip | the card's YM pair would shadow it: a pointless pair |
| `ay-socket = ay` on a ZX-Evo / TS-Conf | refused | the YM2149 must come out of its socket for the card |
| a General Sound, NeoGS or GS lightweight card (`gs`, `neogs`, `gs-lw`) | the GS card goes | both are a General Sound |
| a SounDrive card (`soundrive`) | the SounDrive card goes (and with it the `#FB` Covox it also answered) | both are a SounDrive |
| a second ZX-MultiSound | the first one goes | both have a SAA and the other parts |
| a MoonSound, Covox (`covox-fb`), ZXNETUSB or ZX-WiFi card | both stay | no shared job |

The DIP switches change this: a part switched off stops answering its ports and gives up its job. With
`dip=ym,saa,sd` (GS off) a NeoGS may stay; with `ym` off a TSFM may stay in the socket.

In the configuration file such conflicts **refuse the machine** with every pair and the reason. When you plug the card
into a running machine, the plan lists the cards it would remove: automation needs the override flag, the Qt window
asks for a confirmation and offers **Undo** afterward.

## Fitting the card

Any change of the card (fitting it, removing it, changing an option) **restarts the machine**: a new emulator instance
with a new id, the machine state lost, disks and other media carried over ([slots.md](slots.md#changing-a-card)). It is
refused while a TTD recording runs.

### Options

| Option | Values | Default | Meaning |
|---|---|---|---|
| `dip` | any of `ym`, `saa`, `gs`, `sd` (comma list) | all four | the parts switched on: `ym` = TurboSound FM and MIDI, `saa` = SAA1099, `gs` = General Sound, `sd` = SounDrive |
| `gsRam` | `1m`, `2m` | `1m` | the General Sound's RAM (2 MB needs the card's 2 MB firmware on the real board) |
| `ctrlMask` | `pro`, `classic` | `pro` | which values on `#FFFD` count as a control byte: `pro` = `#F0-#FF` (the official firmware); `classic` = `#F8-#FF`, an **unofficial** firmware patch, in effect only while `saa` is off |

### In the configuration file

Remove the cards that conflict from the machine's `[SLOTS]` section and name the card. A Pentagon example (based on
the shipped `pentagon128k` config: the TSFM, NeoGS and SounDrive lines removed, the MoonSound kept):

```ini
[SLOTS]
; no ay-socket line: the board AY stays and the card shadows it (ay-socket = ay says the same)
zxbus.1 = multisound
zxbus.1.gsRam = 2m             ; 1m | 2m
zxbus.1.dip = ym,saa,gs,sd     ; the parts switched on (default: all)
zxbus.1.ctrlMask = pro         ; pro | classic (unofficial)
zxbus.2 = moonsound
```

On a ZX-Evo or TS-Conf leave the `ay-socket` line out (the card takes the YM2149 out of its socket); `ay-socket = ay`
there is refused.

### In the Qt window

**Machine > Slots...** shows the buses and cards. Plug `multisound` into a free ZX-bus slot; the DIP set is a
row of check boxes, `gsRam` and `ctrlMask` are lists (with a note on the unofficial `classic`). The window shows the
plan before it applies it, asks when cards must go, and names the removed cards with an **Undo** button afterward.

### Through automation

| Surface | Fit the card |
|---|---|
| CLI | `slots plug zxbus.next multisound gsRam=2m --replace` (`--dry-run` shows the plan only) |
| WebAPI | `POST /api/v1/emulator/{id}/slots/zxbus.next/plug` with `{"card": "multisound", "options": {"gsRam": "2m"}, "replaceIfIncompatible": true}`; create with it: `POST /api/v1/emulator/start` with `{"model": "PENTAGON", "slots": {"zxbus.1": "multisound", "zxbus.1.gsRam": "2m"}}` |
| MCP | `emulator_manage` `{"action": "slots_plug", "slot": "zxbus.next", "card": "multisound", "options": "gsRam=2m", "replace_if_incompatible": true}`, or `create` with `"slots"` |
| Lua | `slots_plug("zxbus.next", "multisound", {replace = true, gsRam = "2m"})` |
| Python | `unreal.slots_plug("zxbus.next", "multisound", replace=True, gsRam="2m")` |

Worked example: on a Pentagon with the shipped configuration, `slots plug zxbus.next multisound --dry-run` plans the
card into `zxbus.4` and lists what goes: the NeoGS (`zxbus.1`, shares the General Sound), the SounDrive (`zxbus.3`,
shares the SounDrive), the TSFM in the socket (the card would shadow it), the `#FB` Covox the SounDrive card answered,
and the NeoGS's SD card slot `sd.ngs`. The same command with `--replace` applies it and the machine restarts with the
card; the reply names the new emulator id.

## The MIDI bank

The synthesizer plays a General MIDI SoundFont. By default it is **GeneralUser GS 2.0.3**, shipped in `data/midi/`
next to the executables ([data/midi/README.md](../../data/midi/README.md)); nothing needs to be set. To use another bank,
add to the machine's `unreal.ini`:

```ini
[MIDI]
Bank = mybank.sf2       ; any SoundFont 2 file; NONE = no bank (the MIDI row stays silent)
```

A bank file name is looked up in the working directory, next to the executable, then in the application's resources.

The bank is read when the card is built: a changed bank takes effect at the next machine start or slot change. The
state report names the bank in use (`bank loaded`, or `no bank`).

## What you hear

### Mixer rows

With the card fitted, the audio mixer (Tools > Audio Settings...) has seven rows of its own; each has volume, mute and
solo like any other device, and each can be captured or recorded alone by its key:

| Row | Key | Source |
|---|---|---|
| MS SSG 1 | `ms_ssg1` | the SSG (AY) part of YM chip 0 |
| MS SSG 2 | `ms_ssg2` | the SSG part of YM chip 1 |
| MS FM 1 | `ms_fm1` | the FM part of YM chip 0 |
| MS FM 2 | `ms_fm2` | the FM part of YM chip 1 |
| MS SAA | `ms_saa` | the SAA1099 |
| MS PCM | `ms_pcm` | the four DACs: General Sound and SounDrive together |
| MS MIDI | `ms_midi` | the SAM2695 synthesizer |

The rows exist only while the card is fitted. The HUD shows one activity indicator per row (`MS AY 1`, `MS AY 2`,
`MS FM 1`, `MS FM 2`, `MS SAA`, `MS PCM`, `MS MIDI`), in the HUD category of the card.

### Levels and stereo

- **FM trim is shared with the TurboSound FM.** The FM rows use the TSFM's calibration, `[SOUND] TSFM_FmTrimDb`
  (7.4 dB in the shipped configs), and the "FM trim" control of the audio settings drives both boards: one tune comes
  out at the same FM level on the card and on a TSFM in the socket.
- **The SSG rows are 7.6 dB quieter than a TSFM's**, on purpose: on the card's mixer the SSG goes through larger
  resistors than the FM (24 k against 10 k), the TSFM board mixes them equally. Example: a tune whose SSG and FM are
  balanced on a TSFM has a quieter AY part on the card, as on the real hardware.
- **The AY tone voicing applies.** The SSG rows go through the same AY / SSG tone voicing as the socket's chips (the
  audio settings' "EQ profile", `[SOUND] AYVoicing`); the FM is not voiced.
- **The AY punch and room settings apply.** With Sound HQ on, the SSG rows also go through the same AY character
  chain as the socket's chips: `ay_punch` (sharper attacks) and `ay_room` (the headphone crossfeed, default -9 dB)
  change the card's SSG exactly as they change an AY, TurboSound or TurboSound FM in the socket, live, on every
  surface. Example: with `ay_room` at `9db` a tone on SSG channel A, which the board wires to the left only, is also
  heard on the right, 2 ms later and 9 dB quieter; with `ay_room` at `off` the right stays silent. FM, SAA, SounDrive,
  General Sound and MIDI are not affected.
- **The stereo layout is fixed by the board.** SSG channel A is left, B is in the center (quieter), C is right (what
  the emulator calls ABC); `[AY] Stereo` does not change it. The General Sound is hard left / hard right (channels 1-2
  left, 3-4 right), with no cross-feed; SounDrive `#0F` / `#1F` are left, `#4F` / `#5F` right. FM, SAA and MIDI are
  stereo or centered as their chips produce them.

## What you see: state reports

| What | CLI | WebAPI | MCP `inspect_state` aspect | Lua / Python |
|---|---|---|---|---|
| The card: options, fit, shadowed devices, control-byte state, both YM chips (SSG + FM), SAA, GS, DACs, a MIDI summary | `multisound` (`--full` every register, `--json`) | `GET /api/v1/emulator/{id}/state/audio/multisound` | `audio_multisound` | `multisound_state()` |
| The MIDI line and synthesizer: bank, 16 parts with program, volume, pan, the notes sounding, counters | `midi` (`--json`) | `GET /api/v1/emulator/{id}/state/audio/midi` | `audio_midi` | `midi_state()` |
| MIDI panic: every voice stops; programs, controllers and a MIDI message in progress stay | `midi panic` | `POST /api/v1/emulator/{id}/control/audio/midi` `{"action": "panic"}` | `invoke_api` with that request | `midi_panic()` |

The card's slot also shows in the slot report (`slots`). Example, a Pentagon right after fitting the card:

```
[audio_multisound] ZX-MultiSound (UzixLS) in zxbus.4, dip=ym,saa,gs,sd gsRam=2m ctrlMask=pro; FM muted, SAA off,
                   GS booting, MIDI 0 byte(s), 0 voice(s)
  built-in ay: shadowed by zxbus.4
[audio_midi] bank loaded, voices 0/38, bytes 0, framing errors 0
```

FM muted and SAA off are the card's state after a reset: the program has not written its control byte yet.

In unreal-qt, **Tools > MIDI Activity** shows the 16 parts with their programs and instrument names, a key
strip with the notes sounding on each channel, and a **Panic** button.

## Time travel (TTD)

A TTD recording covers the whole card: the control logic, both YM chips, the SAA, the General Sound with its RAM, the
DACs, the MIDI line and the synthesizer. Seeking back and replaying gives the same machine state and the same sound. A
MIDI panic is recorded as an input and happens again on replay. A recorded session opens only on a machine with the
same cards and the same MIDI bank; otherwise it is refused with the difference (for example `MIDI bank differs from the
recording`). While a recording runs the slots cannot change.

## Known behavior

These are the real card's behavior, kept on purpose:

- **FM is silent after a reset until the program writes a control byte.** The card starts with FM muted and the SAA
  clock off; a program writes a value such as `#F2` (bits 2 and 3 clear) to `#FFFD` to turn them on. A plain
  TurboSound chip switch (`#FF` / `#FE`) mutes FM and stops the SAA again.
- **The SAA stays silent until a program starts its clock.** Some programs written for the ZXM-SoundCard (another SAA
  board) never write that control byte and stay silent on the card, as on the real one (the LnxTracker Demo's SAA
  songs, for example).
- **The SAA and SounDrive ports are locked while the instruction runs from `#0000-#3FFF`** (the card's protection for
  the ROM area): a BASIC `OUT` from the ROM does not reach them; code in RAM does.
- **The synthesizer ignores MIDI for 50 ms after a reset**, as the chip does while it starts.
- **ZX MIDI Player at 14 MHz on a ZX-Evo sends MIDI too slowly** (about 23.6 kbaud instead of 31.25): its delay loop
  assumes no memory wait states, and the ZX-Evo has them at 14 MHz. The notes come out garbled; at 3.5 and 7 MHz it plays
  correctly. A limit of the program, not of the emulator.
- **Ball Quest's click is not modeled.** The game writes `#F0-#F7` to `#FFFD` as YM register numbers; on the card these
  are control bytes, and the real card clicks there. The emulator does what the card's logic does (switches the chip,
  unmutes FM) but plays the muted FM as silence, so the click is not heard.
- **The SSG is 7.6 dB below a TSFM's** (see [Levels and stereo](#levels-and-stereo)).

## Software to try

Run as a user would, with the card fitted on a Pentagon unless noted (the real-software passes of 2026-10-05 and
2026-10-06). "Same as" means the sound was compared with the same program on the separate card it replaces - the
TurboSound FM in the AY socket, the classic General Sound, the SounDrive - and has the same spectrum and notes:

| Program | Part of the card | What to expect |
|---|---|---|
| TFM Music Maker tunes and TurboSound FM demos (for example *Tech Support* from Moe-bius, *shanson*, *Number 1*, the ZXAAA 2013 pack's intro) | FM + SSG | FM and AY parts on both chips, the FM the same as on a TurboSound FM; also on a ZX-Evo |
| deNextPlayer 0.76 (TurboSound PT3 modules) | SSG | both AY parts the same tunes as on a TurboSound, 7.6 dB quieter and with the board's fixed stereo |
| TEST SAA1099 (Azesmbog) | SAA | all eight test pages audible, left / right per voice |
| SAA1099 E-Tracker music player (SAM Coupe E-Tracker tunes, TR-DOS adaptation by daniel/BDA) | SAA | plays from `RUN`; Enter = next tune, `1`-`4` = autoplay; the notes are the ones the tune programs |
| Wild Commander on TS-Conf: VGMPLAY (SAA1099 and YM2203 VGM), WPLAYER (`.TFC` TFM tunes, `.ETC` E-Tracker modules), GSPLAYER (`.MOD`, `.MID`) | FM + SSG, SAA, General Sound, MIDI | `.TFC` the same as on a TurboSound FM; `.ETC` the same tune as the E-Tracker disk; `.MOD` the same as on a General Sound; `.MID` through the card's chip (`-midi_chip=2`); VGMPLAY starts the SAA clock itself and plays the first chip of a dual-SAA file |
| ZX MIDI Player v3 (UzixLS, the card's author) | MIDI | pick the TurboSound chip with the MIDI wire (chip 2 in the player's list = the card's first YM) as output; every note at 3.5 and 7 MHz, also on a ZX-Evo |
| Z-Player 5.0 | General Sound, MIDI | finds the General Sound (16 MHz, 1008 KB free); `.mid` files go to the card's synthesizer, every note of the file in order; MODs the same as on a General Sound. Driven by the Kempston mouse |
| Z-Player 4.0 | General Sound | MODs the same as on a General Sound |
| MIDIPIANO 1.11 | MIDI | choose TS chip 2; the keyboard plays a scale from Q (C3) on |
| Sinty Snoki (`snake.tap`, a 128K MIDI game) | MIDI | its soundtrack through the 128K MIDI port reaches the card's synthesizer: after a reset the card's first YM chip, the MIDI one, is selected |
| Mod Player v2.5 | General Sound | MODs with hard left / right channels |
| Soundrive Player musicdisk (2000) | SounDrive | channels 0-1 left, 2-3 right |
| X Ball Techno Opera (1996) | SSG, then SounDrive | the AY intro; Space starts the four-channel SounDrive part, the same as on a SounDrive card |
| Ball Quest | SSG | plays; no click (see above) |

Programs written for the ZXM-SoundCard (*Arcane Zone Part 2*, *Digit Cat*, the E-Tunes disk, *Kiss Me 2*, the
LnxTracker Demo) never start the SAA clock: their SAA part is silent on the card, as on the real one (see Known
behavior). The S98 player 0.59 is written for a TurboSound FM at ports `#7F3B` / `#7E3B`, not the card's. Not yet
driven: the menus of Titanic, GS Music Player, GS-Player, Wild Player (it asks for a hard-disk or SD driver), The
Link, Nedodemo (a black screen on a Pentagon, with a TurboSound FM too), TFM Instrument Editor, TSolitaire,
Digital Player and Soundrive Music. Reports welcome.

## See also

- [.recipe/peripherals/multisound.md](../../.recipe/peripherals/multisound.md): fit the card, play a TSFM tune, a SAA
  tone and MIDI notes from a test program, capture each source, MIDI panic
- [slots.md](slots.md): slots, cards, plans, the restart
- [.recipe/peripherals/turbosound.md](../../.recipe/peripherals/turbosound.md),
  [.recipe/peripherals/generalsound.md](../../.recipe/peripherals/generalsound.md),
  [.recipe/peripherals/covox-sounddrive.md](../../.recipe/peripherals/covox-sounddrive.md): the separate cards the
  MultiSound replaces
- [.recipe/peripherals/audio-mixer-and-capture.md](../../.recipe/peripherals/audio-mixer-and-capture.md): mixer rows,
  capture by source
- For developers: [hardware reference](../inprogress/2026-10-03-zx-multisound/hardware-reference.md),
  [architecture](../inprogress/2026-10-03-zx-multisound/architecture.md),
  [integration and test results](../inprogress/2026-10-03-zx-multisound/tdd-integration.md); the card's sources
  [UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound)
