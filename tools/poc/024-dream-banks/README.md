# 024 - Dream-native sound banks (.DXB / .B16)

Reads the compiled sound banks of Dream's SAM5000 chips (DreamBlaster X2 / X3 `.DXB`, X16 `.B16`), extracts
their samples, converts them to the SF2-shaped model libsam2695 plays, and compares renders objectively. Research
for phase SAM-7 of the MultiSound card ([tdd-libsam2695.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md) §8).

**The format specification, confidence per field, results and the integration proposal are in
[sam7-dream-banks.md](../../../docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md).**

**Status (2026-10-08):** container, program / variation / drum-kit maps, instruments, splits, samples, loops and
pitch decoded and validated on all 24 bank files in `testdata/midi/`; envelopes approximate; filter, LFOs and
keyboard tables not decoded. Coverage and tuning audit with converter fixes (drums at fixed pitch, one-shots):
2026-10-08.

## Files

| File | What |
|---|---|
| `dreambank.py` | reader + command-line tool: `info`, `programs [--map PDF]`, `dump`, `extract`, `pitchcheck`, `selfcheck` |
| `tosf2.py` | experimental Dream bank -> SF2 converter (the model libsam2695's `ISoundBank` holds) |
| `compare.py` | renders one MIDI file through `sam2695render --dry` with several banks; pitch, centroid, envelope, spectral distance |
| `coverage.py` | coverage audit: `banks` (GM programs, variations, GS kits, kit notes per bank, Dream or SF2), `drums` (every kit note rendered alone), `song` (what a MIDI file uses, checked against a Dream bank, drum part rendered note by note), `pitch` (rendered pitch per program and key) |

Python 3.9+ with numpy. `compare.py` imports `tools/verification/sam2695/smfwrite.py` and needs the
`sam2695render` binary (`tools/build/build.sh sam2695render`).

## Running it

```bash
P=tools/poc/024-dream-banks
B=testdata/midi/dream-gmbk5x/GMBK5X128_203.DXB

python3 $P/dreambank.py info $B                    # header, counts, decoded splits
python3 $P/dreambank.py programs $B --map testdata/midi/dream-gmbk5x/GMBK5X128_203.pdf
python3 $P/dreambank.py selfcheck testdata/midi/dream-gmbk5x/*.DXB testdata/midi/dream-gmbk5x/*.B16
python3 $P/dreambank.py pitchcheck $B              # loops at their decoded root vs equal temperament
python3 $P/dreambank.py dump $B --out gmbk203.json
python3 $P/dreambank.py extract $B --out wav-gmbk203        # WAV + 'smpl' loop chunk

python3 $P/tosf2.py $B --out gmbk5x128-203.sf2
tools/build/build.sh sam2695render
python3 $P/compare.py --render cmake-build-agent-release/bin/sam2695render --out compare \
    --bank dream=gmbk5x128-203.sf2 \
    --bank gugs=data/midi/generaluser-gs.sf2 \
    --bank sam2695sf2=testdata/midi/dream-sam2695-sf2/sam2695.sf2
```

`programs --map` reads the PDF through `pdftotext -layout` (poppler).

User-side workarounds and experiments in `tosf2.py` (not claims about the hardware):

```bash
python3 $P/tosf2.py $B --out gmbk5x128-203-organfix.sf2 --recenter 19   # recommended organ fix
python3 $P/tosf2.py $B --out X.sf2 --replace 19=self:19:8        # program 19 from variation 8 of the same bank
python3 $P/tosf2.py $B --out X.sf2 --replace 19=testdata/midi/dreamblaster-buran/BURAN11.DXB:19
python3 $P/tosf2.py $B --out X.sf2 --layer 19=0                  # keep only layer 0
python3 $P/tosf2.py $B --out X.sf2 --level-field tail            # EXPERIMENT: unconfirmed level bytes as attenuation
```

Program numbers in these options are GM numbers (1-128).

Coverage and tuning audit:

```bash
R=cmake-build-agent-release/bin/sam2695render
SONGS=testdata/sound/multisound/software/wc/wc-sdcard/MUSIC/MID
python3 $P/coverage.py banks $B gmbk5x128-203.sf2 data/midi/generaluser-gs.sf2
python3 $P/coverage.py drums --render $R --kit 0 --gate 0.05 \
    --bank dream=gmbk5x128-203.sf2 --bank gugs=data/midi/generaluser-gs.sf2
python3 $P/coverage.py song $SONGS/DoomE1M1.mid --bank-dxb $B --render $R \
    --bank dream=gmbk5x128-203.sf2 --bank gugs=data/midi/generaluser-gs.sf2
python3 $P/coverage.py pitch --render $R --programs 0-127 \
    --bank dream=gmbk5x128-203.sf2 --bank gugs=data/midi/generaluser-gs.sf2
```

## Results

Self-check (`selfcheck` over all 24 files): every check passes. Per file: header sizes consistent, the hole size
(words 2-3) equal to image size minus file size, 16-bit checksum zero where present, all programs mapped, kit note
ranges sane, sample block decoded for 99.95..100 % of the splits, sample data smooth 16-bit PCM.

| Check | Result |
|---|---|
| splits with a decoded sample block | 100 % in 23 of 24 banks; BURAN-v1.00.B16: 4397 / 4399 |
| variations vs Dream's bank map | GMBK5X128 2015: 101 / 101 listed variations present; 2.03: 103 / 105 (the 2 misses are a map row shifted by one program); 269 variations incl. 128 MT-32 = the map's count |
| pitch word (`pitchcheck`) | GMBK5X128 2.03: 155 loops, median 0.9 cents; GMBK5X64: 156 loops, 0.9 cents; BURAN 1.1: 347 loops, 2.1 cents |
| GXSCC GM.dxb -> SF2 vs its source SF2, rendered by libsam2695 | 0.0 dB spectral distance on melodic notes, same pitch; envelopes approximate (decay 3 dB short over 1.25 s) |
| GUD_104.DXB -> SF2 vs GeneralUser GS 1.44 | 3.4 dB spectral distance (GUD is hand-tuned) |
| GMBK5X128 2.03 -> SF2 vs GeneralUser GS vs sam2695.sf2 | three different sample sets: 8.4..9.7 dB apart; pitch errors 0.4..10 cents for all three |

| coverage (`coverage.py banks`) | GMBK5X128 2.03 and its SF2: 128 / 128 GM programs, 141 variations, 128 MT-32 entries, all 10 GS kits, kit 0 notes 27-87 complete; gaps only where the bank itself has none (SFX kit 35-38, OPL-3 kit 77-81, empty MT-32 kit notes) |
| Doom E1M1 drums (`coverage.py song`) | all 12 notes present; before the fix they were transposed down 4-26 semitones (fixed-pitch bit ignored) and one-shots looped: fixed, now level and brightness of GeneralUser GS's drums |
| Doom drum loudness (BS.1770) | per hit the Dream drums are as loud as GeneralUser's (median +2.8 dB); the Dream guitars are 9 dB louder, so the drums sit 6.5 dB under the melody (GeneralUser: +1.1 dB); no decoded level field explains it, no gain added |
| Rock Organ | +20 cents is Dream's data (same in all three GM banks; layer means of the other 129 multi-layer instruments are centered); `--recenter 19` gives +3.7 cents mean at the Dream On keys |
| tuning (`coverage.py pitch`, all programs, 7 keys) | Dream median 1.5 cents (91 % within 10), GeneralUser GS 2.4 cents (88 %); "Dream On" Rock Organ +16..+26 cents is in the bank data (detuned layers) |

Full tables: [sam7-dream-banks.md](../../../docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md) §4 and §9.

## Limits

- Envelope rates map to SF2 timecents through a fit with r = -0.72; attack, decay and release are right in kind
  (percussive vs sustained) and roughly right in time, not exact.
- Filter, LFOs, the second envelope, keyboard tables and the level words are not decoded, so brightness and
  velocity response follow the raw samples.
- 2 splits (one record) in BURAN-v1.00.B16 use a layout the sample-block finder does not recognize; skipped with a
  count.
- Fixed-pitch splits play at an empirical reference key of 61.1; the exact constant of the hardware is not known.
