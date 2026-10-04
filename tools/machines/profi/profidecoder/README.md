# Profi port decoder PROM tool

`profidecoder.py` reads the port decoder PROM (K556RT4, 256 x 4) of both Profi boards through the board's wiring.
It prints:
- the port map per mode (CP/M, TR-DOS latch, and ROM14 on v5 or A15 on v3.2);
- every place where unreal-ng's `PortDecoder_Profi` decodes a port differently from the v5 PROM.

The wiring, the result and what it settles are in
[decoder-prom.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/decoder-prom.md).

```
python3 profidecoder.py ../../../../testdata/machines/profi/decoder/556rt4-v3.2.bin \
                        ../../../../testdata/machines/profi/decoder/556rt4-v4-v5.bin
```

A third argument, Djoni's V0.03 PROM of the Profi+ (`556rt4-port-decoder-v0.03-djoni-coded.bin`, in the owner's
collection under `profi/port-decoder/`), adds its port map and compares it with unreal-ng under `[PROFI] ExtPorts=sys`.
The file is "coded": its data bits run in the wiring order, the reverse of the v5 printed table (bit0 = system
register, 1 = VG93, 2 = extended group, 3 = 8255); that is the only order that keeps the TR-DOS VG93 at `#1F..#7F`,
as Djoni's port description lists. `ExtPorts=v003` follows this table.

The two tables are in [testdata/machines/profi/decoder/](../../../../testdata/machines/profi/decoder/README.md):
- the v3.2 PROM is a dump, made by MDESK in 2009;
- the v4/v5 PROM is transcribed from the two manuals.

## Model

| Part | Wiring |
|:--|:--|
| PROM address A0..A7 | ADR5, ADR6, A2, ROM14 (v4/v5) or ADR15 (v3.2), ADR7, ADR1, ADR0, /CPM; /CS = /IORQ. A2 = 0 only while TR-DOS is paged in and CP/M is off (v5: D32 = NAND(DISK, /CP-M); v3.2: BAS of the U30 latch, held high by CP/M) |
| Outputs, v4/v5 | data bit 3 = system register, 2 = VG93, 1 = extended group (ИД4 by ADR4..ADR2: FDC, 8255, IDE, COM, P4, -, -, RTC), 0 = 8255 |
| Outputs, v3.2 | data bit 0 = system register, 1 = VG93, 2 = the data bus buffer on `#7FFD`, 3 = 8255 |

The two files number their data bits in opposite orders. Each order is the only one under which its table makes
sense.

The model of unreal-ng (`unrealng_devices()`) follows `core/src/emulator/ports/models/portdecoder_profi.cpp`:
`DecodeFDCPort`, `IsExtMode`, the RTC and the Covox arms, and the Profi IDE gate. Update it when that decoder
changes.

## Files

| File | Contents |
|:--|:--|
| `profidecoder.py` | the tool |
| `profidecoder-output.txt` | its output for the two tables: the port maps, and no difference between the v5 PROM and unreal-ng |
