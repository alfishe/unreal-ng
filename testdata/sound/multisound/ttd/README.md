# ZX-MultiSound TTD fixture program

`allsources.sna` is a 128K snapshot that plays every source of the ZX-MultiSound at once. The TTD corpus sessions
`multisound-pentagon` and `multisound-zxevo` ([testdata/ttd/README.md](../../../ttd/README.md)) record it, so a
time-travel session holds state changes of every device of the card in every checkpoint. The sessions are not stored:
the corpus tests record them from this snapshot in their own process, with v1 and with the engine
([`core/tests/_helpers/ttdmultisoundsessions.h`](../../../../core/tests/_helpers/ttdmultisoundsessions.h)).

What it does, as a user would hear it with the card fitted in a ZX-bus slot:

- every frame: the SounDrive DACs - two saw waves and two square waves, channels 0-1 left, 2-3 right
- every 16 frames (0.3 s) a new step of an eight-step loop:
  - YM2203 chip 1 (U10): a three-voice FM chord and three SSG tones with an envelope
  - YM2203 chip 0 (U4): one FM voice and one SSG tone
  - SAA1099: six voices at new pitches, two of them with noise, envelope generator 0 on
  - MIDI on U4's I/O port A bit 2 at 31 250 baud: reverb and chorus sends off, a program change, the previous note
    off, a new note on channel 1 and a drum hit on channel 10
- once at the start: a General Sound command (`#20`)

At the start it counts how many loops of a known length fit between two interrupts, which tells it the CPU clock
(3.5, 7 or 14 MHz - the ZX-Evo starts at 7 MHz), and times its MIDI bits to it.

Regenerate it (deterministic: the same file every time) with

```bash
python3 tools/verification/multisound/ttd-fixture/make-program.py
```

Nothing to re-record afterwards: the next test run records the sessions from the new snapshot.
