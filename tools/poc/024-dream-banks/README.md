# 024 - Dream-native sound banks (.DXB / .B16)

Reads the compiled sound banks of Dream's SAM5000 chips (DreamBlaster X2 / X3 `.DXB`, X16 `.B16`), extracts
their samples, converts them to the SF2-shaped model libsam2695 plays, and compares renders objectively. Research
for phase SAM-7 of the MultiSound card ([tdd-libsam2695.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-libsam2695.md) §8).

**The format specification, confidence per field, results and the integration proposal are in
[sam7-dream-banks.md](../../../docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md).**

**Status (2026-10-08):** container, program / variation / drum-kit maps, instruments, splits, samples, loops and
pitch decoded and validated on all 24 bank files in `testdata/midi/`; envelopes approximate; filter, LFOs and
keyboard tables not decoded.

## Licensing

Dream and Serdaco banks are licensed for DreamBlaster cards only. The bank files stay untracked in
`testdata/midi/`; this folder holds code only. Write every output (JSON, WAV, SF2, renders) under `scratch/`, which
is git-ignored, and never commit or share it.

## Files

| File | What |
|---|---|
| `dreambank.py` | reader + command-line tool: `info`, `programs [--map PDF]`, `dump`, `extract`, `pitchcheck`, `selfcheck` |
| `tosf2.py` | experimental Dream bank -> SF2 converter (the model libsam2695's `ISoundBank` holds) |
| `compare.py` | renders one MIDI file through `sam2695render --dry` with several banks; pitch, centroid, envelope, spectral distance |

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
python3 $P/dreambank.py dump $B --out scratch/sam7/gmbk203.json
python3 $P/dreambank.py extract $B --out scratch/sam7/wav-gmbk203        # WAV + 'smpl' loop chunk

python3 $P/tosf2.py $B --out scratch/sam7/gmbk5x128-203.sf2
tools/build/build.sh sam2695render
python3 $P/compare.py --render cmake-build-agent-release/bin/sam2695render --out scratch/sam7/compare \
    --bank dream=scratch/sam7/gmbk5x128-203.sf2 \
    --bank gugs=data/midi/generaluser-gs.sf2 \
    --bank sam2695sf2=testdata/midi/dream-sam2695-sf2/sam2695.sf2
```

`programs --map` reads the PDF through `pdftotext -layout` (poppler).

## Results

Self-check (`selfcheck` over all 24 files): every check passes. Per file: header sizes consistent, the hole size
(words 2-3) equal to image size minus file size, 16-bit checksum zero where present, all programs mapped, kit note
ranges sane, sample block decoded for 99.4..100 % of the splits, sample data smooth 16-bit PCM.

| Check | Result |
|---|---|
| splits with a decoded sample block | GMBK5X128 2.03: 3368 / 3380; BURAN 1.1: 4332 / 4352; GUD 1.04: 2471 / 2485; 15 of 17 community banks 100 % |
| variations vs Dream's bank map | GMBK5X128 2015: 101 / 101 listed variations present; 2.03: 103 / 105 (the 2 misses are a map row shifted by one program); 269 variations incl. 128 MT-32 = the map's count |
| pitch word (`pitchcheck`) | GMBK5X128 2.03: 157 loops, median 0.9 cents; GMBK5X64: 156 loops, 0.9 cents; BURAN 1.1: 347 loops, 2.1 cents |
| GXSCC GM.dxb -> SF2 vs its source SF2, rendered by libsam2695 | 0.0 dB spectral distance on melodic notes, same pitch; envelopes approximate (decay 3 dB short over 1.25 s) |
| GUD_104.DXB -> SF2 vs GeneralUser GS 1.44 | 3.4 dB spectral distance (GUD is hand-tuned) |
| GMBK5X128 2.03 -> SF2 vs GeneralUser GS vs sam2695.sf2 | three different sample sets: 8.4..9.7 dB apart; pitch errors 0.4..10 cents for all three |

Full tables: [sam7-dream-banks.md](../../../docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md) §4.

## Limits

- Envelope rates map to SF2 timecents through a fit with r = -0.72; attack, decay and release are right in kind
  (percussive vs sustained) and roughly right in time, not exact.
- Filter, LFOs, the second envelope, keyboard tables and the level words are not decoded, so brightness and
  velocity response follow the raw samples.
- About 0.5 % of the splits in the Dream / Serdaco banks use a layout the sample-block finder does not recognize;
  they are skipped with a count.
