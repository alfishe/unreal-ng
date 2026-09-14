# ZX Spectrum Port Decode Reference

> **Source:** [tslabs/zx-evo](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZX/zx-ports-full-table.txt)  
> **Original:** (c) Black_Cat 2008 www.zx.clan.su, BC Info Guide #4  
> **Translated:** 2026-09-13

---

## Configuration Codes

| Code | Machine |
|------|---------|
| 1/+1 | ZX Spectrum (issue 1-2/3-6) |
| 2    | ZX Spectrum +128, +2 |
| 3/+3 | ZX Spectrum +2a, +2b, +3 |
| 4    | Timex Computer 2048 |
| 5    | Didaktik Gama |
| 6    | Scorpion ZS256 Turbo+ |
| 7    | KAY-1024SL / Beta Turbo |
| 8    | Pentagon 128 (1991) |
| 9    | Profi-1 (v3.x) |
| A    | ATM Turbo-2+ |
| B    | Scorpion GMX |
| C    | Quorum 128/+ |
| D    | Pentagon-1024SL |

## Abbreviations

| Code | Meaning |
|------|---------|
| Atr  | Video attributes port |
| Brd  | Border port |
| Key  | Keyboard port |
| Kjoy | Kempston joystick port |
| Kmou | Kempston mouse port |
| Pag  | Memory paging port |
| Pal  | Video palette port |
| Prn  | Printer ports |
| Reg  | Management port |
| Shdw | Shadow port mode |
| Tp   | Tape port |
| Trb  | Turbo mode port |
| TRD  | TR-DOS mode |
| Vid  | Videomode port |

---

## System Ports

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #FE/254 | `xxxxxxxx11111110` | `xxxxxxxxxxxxxxx0` | Key,Tape(1,7-9) | Border,Tape,Speaker(1,7-9) |
| #FF/255 | `xxxxxxxx11111111` | `xxxxxxxxxxxxxxxx` | Attributes(1-2) | - |

## Memory Paging

| Port | Address | Decode | Read | Write | Config |
|------|---------|--------|------|-------|--------|
| #1FFD/8189 | `0001111111111101` | `0001xxxxxxxxxx0x` | - | Paging,Printer(3/+3) | 3/+3 |
| #7FFD/32765 | `0111111111111101` | `0xxxxxxxxxxxxx0x` | - | Paging(2,8,9,A) | 2,8,9,A |
| #7FFD/32765 | `0111111111111101` | `01xxxxxxxxxxxx0x` | - | Paging(3,?5) | 3,5 |
| #7FFD/32765 | `0111111111111101` | `01xxxxxxxx1xxx01` | Turbo-ON(6) | Paging(6) | 6 |

## Kempston Joystick

| Port | Address | Decode | Read | Write | Config |
|------|---------|--------|------|-------|--------|
| #1F/31 | `xxxxxxxx00011111` | `xxxxxxxxxx0xxxxx` | Kjoy | - | *1 |
| #1F/31 | `xxxxxxxx00011111` | `xxxxxxxx0x0xxx11` | Kjoy(6) | - | 6 |
| #1F/31 | `xxxxxxxx00011111` | `xxxxxxxx00011111` | Kjoy | - | CScard |

## Kempston Mouse

Standard decode (*1 - Velesoft documentation):

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #FADF/64223 | `1111101011011111` | `xxxxxx10xx0xxxxx` | Buttons | - |
| #FBDF/64479 | `1111101111011111` | `xxxxx011xx0xxxxx` | X coord | - |
| #FFDF/65503 | `1111111111011111` | `xxxxx111xx0xxxxx` | Y coord | - |

USSR Kempston Mouse variant:

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #FADF/64223 | `1111101011011111` | `xxxxx0x01x0xxxx1` | Buttons | - |
| #FBDF/64479 | `1111101111011111` | `xxxxx0x11x0xxxx1` | X coord | - |
| #FFDF/65503 | `1111111111011111` | `xxxxx1x11x0xxxx1` | Y coord | - |

Kempston Mouse Turbo (Velesoft, A=0/1 -> Master/Slave):

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #7ADF/#FADF | `1111101011011111` | `Axxxx0x011011111` | Buttons | - |
| #7BDF/#FBDF | `1111101111011111` | `Axxxx0x111011111` | X coord | - |
| #7FDF/#FFDF | `1111111111011111` | `Axxxx1x111011111` | Y coord | - |

## AY Sound

| Port | Address | Decode | Read | Write | Config |
|------|---------|--------|------|-------|--------|
| #BFFD/49149 | `1011111111111101` | `10xxxxxxxx1xxx01` | - | AY data(6) | 6 |
| #FFFD/65533 | `1111111111111101` | `11xxxxxxxx1xxx01` | AY data(6) | AY addr(6) | 6 |
| #FFFD/65533 | `1111111111111101` | `11xxxxxxxxxxxxxx` | AY data | AY addr | 1,2,8 |

## Beta128 / TR-DOS

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #1F/31 | `xxxxxxxx00011111` | (in TR-DOS) | FDC Command | FDC Command |
| #3F/63 | `xxxxxxxx00111111` | (in TR-DOS) | FDC Track | FDC Track |
| #5F/95 | `xxxxxxxx01011111` | (in TR-DOS) | FDC Sector | FDC Sector |
| #7F/127 | `xxxxxxxx01111111` | (in TR-DOS) | FDC Data | FDC Data |
| #FF/255 | `xxxxxxxx11111111` | (in TR-DOS) | FDC Status | System |

## Scorpion-Specific

| Port | Address | Decode | Read | Write | Purpose |
|------|---------|--------|------|-------|---------|
| #1FFD/8189 | `00xxxxxxxx1xxx01` | Turbo-OFF(6) | - | Paging(6) | Turbo flip-flop reset |
| #7FFD/32765 | `01xxxxxxxx1xxx01` | Turbo-ON(6) | - | Paging(6) | Turbo flip-flop set |
| #FFDD/65501 | `1111111111011101` | `xxxxxxxxxx0xxx01` | - | Printer(6) | Centronics |

## SMUC (Scorpion MOA Universal Controller)

| Port | Address | Decode | Read | Write |
|------|---------|--------|------|-------|
| #FFBA/65466 | `1111111110111010` | `1x111xxx101xx010` | NVRAM/RTC | NVRAM/RTC |
| #7FBA/32698 | `0111111110111010` | `0x111xxx101xx010` | IDE regs | IDE regs |

---

## Reading the Decode Column

- `x` = don't care (bit not checked)
- `0` = bit must be 0
- `1` = bit must be 1
- `A`, `B`, `C` = address bit used as parameter

Format: `A15 A14 A13 A12 A11 A10 A9 A8 A7 A6 A5 A4 A3 A2 A1 A0`

## Implementing in Code

```cpp
// Example: Kempston Mouse common decode (A9=1, A7=1, A5=0)
//                        A15           A8 A7           A0
constexpr uint16_t kMask  = 0b0000'0010'1010'0000;  // check A9, A7, A5
constexpr uint16_t kMatch = 0b0000'0010'1000'0000;  // A9=1, A7=1, A5=0

bool IsKempstonMousePort(uint16_t port) {
    return (port & kMask) == kMatch;
}
```

## References

- Original table: https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZX/zx-ports-full-table.txt
- Velesoft ports: http://velesoft.speccy.cz/zxporty-cz.htm
- Port decoder implementation: `core/src/emulator/ports/README.md`
