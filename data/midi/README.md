# General MIDI bank (product default)

`generaluser-gs.sf2` is the default SoundFont 2 bank of the emulated SAM2695 General MIDI synthesizer on the
ZX-MultiSound card (`core/src/3rdparty/sam2695/`, card: `core/src/emulator/slots/cards/multisound/`). The build copies
this folder next to the executables (`midi/` beside `rom/`; on macOS into the app bundle's `Contents/Resources/midi`),
where the card finds it as `midi/generaluser-gs.sf2` without any configuration.

| | |
|---|---|
| Bank | GeneralUser GS 2.0.3 (2026-02-22), by S. Christian Collins |
| Source | [GeneralUser GS on GitHub](https://github.com/mrbumpy409/GeneralUser-GS) (`GeneralUser-GS.sf2`, repository commit 684543d) |
| Size | 32,319,396 bytes |
| SHA-256 | `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe` |
| License | GeneralUser GS License v2.0, [LICENSE-generaluser-gs.txt](LICENSE-generaluser-gs.txt) |

**License in short:** free to use without restriction for music, private or commercial; may be used, modified and
repackaged in software projects. The samples carry the usage rights of their (freely available) sources; the author
states none came from commercial sample collections.

**Choosing another bank.** `[MIDI] Bank=` in the machine's `unreal.ini` names another SoundFont 2 file (resolved like a
ROM path: the working directory, next to the executable, then the resources folder). `Bank=NONE` loads no bank: the
synthesizer stays silent and nothing (30 MB) is loaded. A bank change applies when the card is built (a machine
restart, e.g. any slot change). A TTD recording names its bank by SHA-256 and is refused with another bank.

Example:

```ini
[MIDI]
Bank = midi/my-bank.sf2
```

The test runner keeps the default bank out of the machines it creates unless a test asks for it
(`SoundCardScope(TestSound::DefaultMidiBank)`): loading it takes about 165 ms per card.
