# Profi sync PROM decoder

`profisync.py` simulates the Profi sync generator tick by tick, so that a sync PROM dump becomes the frame it
produces. The results and what they settle are in
[cross-check.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/cross-check.md) section 4.

```
python3 profisync.py <folder with VR*.ROM>
```

The input is a folder of 2K dumps named `VR*.ROM`, as on the [speccy4ever Profi page](https://speccy4ever.speccy.org/_PR.htm).
The dumps are not in the repository.

For each PROM, `VR3*` files are decoded with v3.2 wiring, and `VR5*` files with v5 wiring, v5 album wiring and v3.2
wiring. The tool also decodes the PROM printed in the v5.0 album, transcribed in `printedmap.py`.

Each run reports, per raster (Spectrum 256x192 and 512x240):
- the line length in T and in us, the lines per frame and the frame length;
- a profile of one line (paper / border / blank / sync);
- the vertical runs from paper line 0;
- each INT edge: where it falls, the distance from INT to the first paper dot, and the INT length.

## Model

| Part | Wiring |
|:--|:--|
| Clock | one horizontal tick = 16 master clocks = 4 T (14 MHz in the Spectrum raster, 12 MHz in 512x240) |
| PROM address | A10 = 80DS; A5-A9 = DA10-DA14 (16 lines per row); the column is DA1-DA5 on v3.2, and DA2-DA5 with A0 = DA0 & DA1 on v5 |
| Data bits | D0 sync, D1 blank, D2 paper, D3 INT source, D4 vsync, D5 frame reset, D6 line load, D7 line count |
| Line load value | **63** on v3.2 and on the v5.0 album drawing; **61** on v5 boards with Kondor's fix (DD53 pin 4 on GND, as in MISTAK52 and the v5.06 netlist) |
| INT | starts when D3 falls and ends at the next DA3 edge. This is a model: the INT circuit is not fully traced |

Sources:
- the v3.2 and v5.0 schematics;
- KLUG's RF5 and SAMX6P notes;
- the bit labels in the Profi ROM viewer.

All of them are linked from the cross-check.

## Files

| File | Contents |
|:--|:--|
| `profisync.py` | the decoder |
| `printedmap.py` | the v5.0 album's printed sync PROM (p.9, "КОНДОР 1994"), retyped and checked against the scan |
| `profisync-output.txt` | the output for the seven speccy4ever dumps and the printed map |
