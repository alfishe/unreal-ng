# Profi port decoder PROMs: how they are wired, and the port map they give

**Date:** 2026-10-01 · part of [README.md](README.md) · tool: [tools/machines/profi/profidecoder/profidecoder.py](../../../tools/machines/profi/profidecoder/profidecoder.py),
output: [tools/machines/profi/profidecoder/profidecoder-output.txt](../../../tools/machines/profi/profidecoder/profidecoder-output.txt) · tables:
[testdata/machines/profi/decoder/](../../../testdata/machines/profi/decoder/README.md)

Both boards decode the disk and peripheral ports with a K556RT4 PROM (256 x 4). This document reads both PROMs
through their wiring, turns them into a port map per mode, and compares that map with unreal-ng's decoder.

## 1. Sources

| Board | PROM | Wiring read from |
|:--|:--|:--|
| v3.2 | U5 on the controller board, a dump (MDESK, read off a board in 2009) | the MDESK re-trace of the v3.2 controller, "Interface list 1" (`profi32il-mdesk-sch.pdf` in [Profi3.zip](https://github.com/alemorf/retro_computers/tree/master/Profi_3_2)); the outputs' consumers on lists 3, 4 and 6 |
| v4.0 / 4.01 / 5.0 | D10 on the main board, the table printed in both manuals (the same bytes in two different typesettings) | the Profi 5.x schematic drawn as text (`PROF5-10.TXT` from the NedoPC Profi section); the consumers on `PROF5-11`, `-12`, `-13`, `-16`, `-19` |

## 2. Address inputs

The chip pins A0..A7 (5, 6, 7, 4, 3, 2, 1, 15) carry the same signals on both boards, except A3:

| PROM input | v3.2 | v4 / v5 |
|:--|:--|:--|
| A0, A1 | ADR5, ADR6 | ADR5 (LADR5), ADR6 (LADR6) |
| A2 | BAS, the other output of the DISK / BAS latch U30; U30:B also takes CP/M on pin 12, so CP/M holds BAS high | D32 = NAND(DISK, /CP-M) |
| A3 | **ADR15** | **ROM14** |
| A4, A5, A6 | ADR7, ADR1, ADR0 | ADR7, ADR1, ADR0 |
| A7 | /CPM: 0 in CP/M mode | /CP-M |
| /CS | /IORQ | /IORQ |

How these were settled:
- **A2 = 0 only while TR-DOS is paged in AND CP/M is off**, on both boards. The wiring was traced on each:
  - v5 (`PROF5-10.TXT`): D32 pin 1 sits on the /CP-M net, the same net that feeds D13's D input and D20 pin 13.
    Pin 2 is DISK.
  - v3.2 (MDESK list 1): U5 pin 7 runs to BAS, the U30:B output, and U30:B pin 12 is on the CP/M net.
  - So with CP/M on, A2 is 1 whatever the TR-DOS latch says, and the table rows with CP/M on and A2 = 0 are never
    addressed. With CP/M off, A2 = 0 selects the VG93 and A2 = 1 the 8255, which agrees with this reading. The v5.0
    manual says the same in words: CP/M "блокирует работу контроллера из ПЗУ TRDOS и включает порты контроллера на
    доступ из ОЗУ".
- **A7 on v3.2:** taken from the MDESK drawing, where the junction dot sits on the CP/M line, not on /RESET.

## 3. Outputs

All outputs are active low. Which output drives which device was traced to its consumer:

| Output | v3.2 (data bit) | v4 / v5 (data bit) | Selects |
|:--|:--|:--|:--|
| F1 | 1 | 2 | VG93 /CS |
| F2 | 0 | 3 | the FDC system register (write latch and INTRQ / DRQ read buffer) |
| F5 | 3 | 0 | 8255 /CS |
| F4 | 2 | - | v3.2 only. It enables nothing but the controller's data bus buffer (U8 to U31), and it decodes `#7FFD` |
| F6 | - | 1 | v5 only. It enables the ИД4 D11, which splits by ADR4..ADR2 into P0 VG93 (with F1), P1 8255 (with F5), P2 IDE, P3 COM (8251), P4 timer / control register (split by ADR6 ADR5), P7 RTC (ADR5 = AS / DS) |

**The bit order differs between the two files.**
- **v4 / v5:** the printed legend is "Q1(12) -- D3 ... Q4(9) -- D0".
- **v3.2:** the MDESK drawing labels pin 12 as D0.

Each order is the only one under which its table makes sense; the other one puts the system register on
`#1F..#7F`.

**ИД4 select input:** the text schematic labels its low select input ADR7. Only ADR2 gives the port list in the
manuals (`#E3` FDC, `#E7` 8255, `#EB` IDE, `#EF` / `#F3` COM, `#B3` / `#93` control register, `#BF` / `#DF` RTC), so
the label is taken as a drawing error.

## 4. The port map

These are the ports with A1 A0 = 11. The full listing is in
[tools/machines/profi/profidecoder/profidecoder-output.txt](../../../tools/machines/profi/profidecoder/profidecoder-output.txt).

| CP/M | TR-DOS | v3.2 | v4 / v5 |
|:--|:--|:--|:--|
| off | off | 8255 `#1F/#3F/#5F/#7F` (A7 = 0) | the same |
| off | on | VG93 `#1F..#7F`, system `#FF` (A7..A5 = 111) | the same, for both ROM14 values |
| on | off | VG93 `#1F..#7F`, system `#BF`, for **both** A15 values | ROM14 = 0: the same as v3.2. ROM14 = 1: the extended map, with VG93 `#83/#A3/#C3/#E3`, 8255 `#87/#A7/#C7/#E7`, IDE `#8B/#AB/#CB/#EB`, COM `#8F..`, P4 `#93..`, RTC `#9F/#BF/#DF/#FF`, and the system register at `#3F` (A7 A6 A5 = 001) |
| on | on | the same as CP/M on, TR-DOS off: CP/M forces A2 = 1 | the same as CP/M on, TR-DOS off |

v3.2 also decodes `#7FFD` (A15 = 0, A7..A5 = 111, A1 A0 = 01) with CP/M off, but only for its data bus buffer.

## 5. What this settles

| Claim ([cross-check.md](cross-check.md)) | Settled as | By |
|:--|:--|:--|
| P6: the v5 extended map | **confirmed, port by port** | the v5 PROM |
| P7: v3 has one CP/M map, whatever ROM14 is | **confirmed**: v3.2 has no ROM14 input at all, and its CP/M map does not depend on A15 either | the v3.2 PROM |
| P8: v5 SYS ROM (TR-DOS on, ROM14 = 0) sees the extended map (Karabas, pico-spec) | **contradicted**: the extended map needs CP/M on, ROM14 = 1 and TR-DOS off | the v5 PROM |
| P9: the CP/M map holds with the DOS latch on (xpeccy-plus, UnrealSpeccy) | **confirmed** on both boards: CP/M forces A2 = 1, so the TR-DOS latch does not matter; Karabas and the original Xpeccy, which need DOS off, are wrong | both PROMs and their A2 wiring |
| P13: 8255 at `#87/#A7/#C7/#E7` in the extended map (UnrealSpeccy, xpeccy-plus) | **confirmed** (P1 with F5); Karabas uses these for its own SPI flash | the v5 PROM |
| The Black_Cat table: the v3 FDC needs A15 = 0 | **contradicted**: v3.2's FDC answers for both A15 values | the v3.2 PROM |

## 6. unreal-ng against the v5 PROM

`profidecoder.py` models unreal-ng's `PortDecoder_Profi` (FDC, system port, 8255 / Covox, RTC, IDE). It
compares that model with the v5 PROM for all 256 low bytes x CP/M x DOS x ROM14.

- **They agree in every combination**: the CP/M and the extended maps, the `#3F` / `#BF` / `#FF` system port, the
  8255 and its Covox aliases, IDE and RTC, with the TR-DOS latch on or off.
- **An earlier reading was wrong.** Before A2 was traced, the tool took it as the TR-DOS latch alone. It then
  reported that the board decodes nothing with CP/M and the latch both on, and that unreal-ng was wrong there. That
  was a misreading of rows the board never addresses.

COM, the timer and the control register are not modeled. The v3 decoder follows the v3.2 table: the same as v5
with ROM14 held at 0, and no extended map.

## 7. Open

- **COM, the timer and the control register** (P3, and P4 with its sub-selects P41 / P4A6) are outside this design
  ([requirements.md](requirements.md) non-goals). Their addresses are recorded here for when they are wanted.
