# test-mod

The ProTracker MOD that the Sprinter ISA phase I2 tests play through the General Sound behind the ISA ZX-bus adapter
([open question Q10](../../../../docs/inprogress/2026-10-02-sprinter-isa/open-questions.md), results in
[i2-outcome.md](../../../../docs/inprogress/2026-10-02-sprinter-isa/i2-outcome.md)), and the script that compares two
recordings of it.

| File | What it does |
|:--|:--|
| `make-test-mod.py OUT.MOD [--print]` | writes the MOD: one 64-byte sine cycle (looped), C-3 / E-3 / G-3 (periods 214 / 170 / 143) for 16 rows each, then 16 silent rows (effect `C00`); speed 6, 125 BPM: a row is 120 ms, a note 1.92 s, the pattern 7.68 s. The pitch is `3 546 895 / period / 64` (258.97 / 326.00 / 387.55 Hz). 2 172 bytes |
| `compare-proplay.py A.wav[:ch][@s] B.wav[:ch][@s]` | per file: the note starts, each note's pitch (1 s FFT, parabolic peak) and the times between the starts; side by side: pitch difference in cents, start offsets, the correlation of the 20 ms envelopes and of 2 s of the waveform. `:6,7` picks MAME's NeoGS channels (its `-wavwrite` has 8), `@32.5` starts the note search there |

The core test (`SprinterProPlay_Test`, `core/tests/emulator/machines/sprinter/sprintergeneralsound_test.cpp`) builds
the same bytes itself and writes them over `DOCS\DISP.TXT` of a session copy of the system disk
(`UNREAL_SPRINTER_HDD`); `UNREAL_SPRINTER_PROPLAY_WAV=<dir>` saves the captured GS row as `proplay-neogs.wav` /
`proplay-gs.wav` (+ `-replay`). Worked example for a MAME disk: `mcopy -o -i sys.img@@32256 DISP.TXT ::/DOCS/DISP.TXT`
(the MOD padded with zeros to 3 324 bytes), `chdman createhd -i sys.img -o sys.chd -chs 4096,16,32`, then
`mame-zxsteps.sh` with `SPC_WAV` ([../mame-capture/](../mame-capture/README.md)).

Needs Python 3 (`compare-proplay.py`: numpy).
