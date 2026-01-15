# WD179X Datasheet: Disk Formatting and Timing Specifications

## IBM 3740 FORMAT — 128 BYTES/SECTOR (FM / Single Density)

Standard single-density format. Write Track command sequence:

### Track Layout

| FIELD | CONTENT | COUNT | DESCRIPTION |
| :--- | :---: | :---: | :--- |
| Gap 4a | $FF (or $00) | 40 | Pre-index gap |
| Sync | $00 | 6 | Synchronization field |
| Index Mark | $FC | 1 | **Control byte: writes IAM** |
| **FOR EACH OF 26 SECTORS:** |
| Gap 1 | $FF (or $00) | 26 | Post-index gap |
| Sync | $00 | 6 | ID field sync |
| ID Mark | $FE | 1 | **Control byte: writes IDAM** |
| Track | (track #) | 1 | Track number (0-76) |
| Side | (side #) | 1 | Side number (0 or 1) |
| Sector | (sector #) | 1 | Sector number (1-26) |
| Length | $00 | 1 | Sector length code (128 bytes) |
| CRC | $F7 | 1 | **Control byte: generates 2 CRC bytes** |
| Gap 2 | $FF (or $00) | 11 | ID gap (between ID and Data) |
| Sync | $00 | 6 | Data field sync |
| Data Mark | $FB | 1 | **Control byte: writes DAM** (or $F8 for deleted) |
| Data | (user data) | 128 | Sector data (IBM uses $E5 for format) |
| CRC | $F7 | 1 | **Control byte: generates 2 CRC bytes** |
| Gap 3 | $FF (or $00) | 27 | Data gap (between sectors) |
| **END OF SECTOR LOOP** |
| Gap 4b | $FF (or $00) | ~247 | Final gap until index interrupt |

**Total track capacity:** ~3125 bytes (FM @ 300 RPM, 250 kbit/s)

---

## IBM SYSTEM 34 FORMAT — 256 BYTES/SECTOR (MFM / Double Density)

Standard double-density format. Write Track command sequence:

### Track Layout

| FIELD | CONTENT | COUNT | DESCRIPTION |
| :--- | :---: | :---: | :--- |
| Gap 4a | $4E | 80 | Pre-index gap |
| Sync | $00 | 12 | Synchronization field |
| IAM Pre | $F6 | 3 | **Control byte: writes C2 with missing clock** |
| Index Mark | $FC | 1 | **Control byte: writes IAM** |
| **FOR EACH OF 26 SECTORS:** |
| Gap 1 | $4E | 50 | Post-index gap |
| Sync | $00 | 12 | ID field sync |
| IDAM Pre | $F5 | 3 | **Control byte: writes A1 with missing clock + presets CRC** |
| ID Mark | $FE | 1 | **Control byte: writes IDAM** |
| Track | (track #) | 1 | Track number (0-76) |
| Side | (side #) | 1 | Side number (0 or 1) |
| Sector | (sector #) | 1 | Sector number (1-26) |
| Length | $01 | 1 | Sector length code (256 bytes) |
| CRC | $F7 | 1 | **Control byte: generates 2 CRC bytes** |
| Gap 2 | $4E | 22 | ID gap |
| Sync | $00 | 12 | Data field sync |
| DAM Pre | $F5 | 3 | **Control byte: writes A1 with missing clock + presets CRC** |
| Data Mark | $FB | 1 | **Control byte: writes DAM** |
| Data | (user data) | 256 | Sector data |
| CRC | $F7 | 1 | **Control byte: generates 2 CRC bytes** |
| Gap 3 | $4E | 54 | Data gap |
| **END OF SECTOR LOOP** |
| Gap 4b | $4E | ~598 | Final gap until index interrupt |

**Total track capacity:** ~6250 bytes (MFM @ 300 RPM, 500 kbit/s)

---

## MINIMUM GAP REQUIREMENTS (NON-IBM FORMATS)

| GAP | FM (SINGLE DENSITY) | MFM (DOUBLE DENSITY) |
| :--- | :---: | :---: |
| Gap I (Post-Index) | 16 bytes $FF | 32 bytes $4E |
| Gap II (ID Gap) | 11 bytes $FF | 22 bytes $4E |
| Pre-AM Sync | 6 bytes $00 | 12 bytes $00, then 3 bytes $A1 |
| Gap III (Data Gap) | 10 bytes $FF | 24 bytes $4E, then 3 bytes $A1 |
| Pre-Data Sync | 4 bytes $00 | 8 bytes $00 |
| Gap IV (Final) | 16+ bytes $FF | 16+ bytes $4E |

**Important MFM Note:** Exactly 3 bytes of A1 ($F5 control byte) must precede each address mark.

---

## CONTROL BYTES FOR WRITE TRACK

| HEX | FM ACTION | MFM ACTION |
| :---: | :--- | :--- |
| $00-$F4 | Write byte literally | Write byte literally |
| $F5 | (Not used) | Write $A1 with missing clock, preset CRC |
| $F6 | (Not used) | Write $C2 with missing clock |
| $F7 | Generate and write 2 CRC bytes | Generate and write 2 CRC bytes |
| $F8 | Write Deleted Data Address Mark | Write Deleted Data Address Mark |
| $F9 | Write $F9 literally | Write $F9 literally |
| $FA | Write $FA literally | Write $FA literally |
| $FB | Write Data Address Mark | Write Data Address Mark |
| $FC | Write Index Address Mark | Write Index Address Mark |
| $FD | Write $FD literally | Write $FD literally |
| $FE | Write ID Address Mark | Write ID Address Mark |
| $FF | Write $FF literally | Write $FF literally |

---

## ELECTRICAL CHARACTERISTICS

### Absolute Maximum Ratings

| PARAMETER | VALUE |
| :--- | :--- |
| VDD with respect to VSS | -0.3V to +15V |
| VCC with respect to VSS | -0.3V to +7V |
| Voltage on any input | -0.3V to +15V |
| Operating Temperature | 0°C to 70°C |
| Storage Temperature | -55°C to +125°C |

### DC Operating Characteristics

**Conditions:** TA = 0°C to 70°C, VDD = +12V ± 5%, VCC = +5V ± 5%, VSS = 0V

| SYMBOL | PARAMETER | MIN | TYP | MAX | UNIT |
| :--- | :--- | :---: | :---: | :---: | :---: |
| VIH | Input High Voltage | 2.6 | — | — | V |
| VIL | Input Low Voltage | — | — | 0.8 | V |
| VOH | Output High Voltage (IO = -100µA) | 2.8 | — | — | V |
| VOL | Output Low Voltage (IO = 1.6mA) | — | — | 0.45 | V |
| IIL | Input Leakage Current | — | — | 10 | µA |
| ILO | Output Leakage Current | — | — | 10 | µA |
| IDD | VDD Supply Current | — | 10 | — | mA |
| ICC | VCC Supply Current | — | 35 | — | mA |
| PD | Power Dissipation | — | — | 0.5 | W |

---

## TIMING CHARACTERISTICS

### Read/Write Enable Timing

| SYMBOL | PARAMETER | MIN | TYP | MAX | UNIT |
| :--- | :--- | :---: | :---: | :---: | :---: |
| TSET | Address & CS setup to RE/WE | 50 | — | — | ns |
| THLD | Address & CS hold from RE/WE | 10 | — | — | ns |
| TRE | RE pulse width | 400 | — | — | ns |
| TWE | WE pulse width | 350 | — | — | ns |
| TDRR | DRQ reset from RE/WE | — | 400 | 500 | ns |
| TIRR | INTRQ reset from RE/WE | — | 500 | 3000 | ns |
| TDACC | Data access time from RE | — | — | 350 | ns |
| TDOH | Data hold from RE rising | 50 | — | 150 | ns |
| TDS | Data setup before WE rising | 250 | — | — | ns |
| TDH | Data hold from WE rising | 70 | — | — | ns |

### Input Data Timing

| SYMBOL | PARAMETER | MIN | TYP | MAX | UNIT |
| :--- | :--- | :---: | :---: | :---: | :---: |
| TPW | Raw Read pulse width | 100 | 200 | — | ns |
| TBC | Raw Read cycle time | — | 1500 | 1800 | ns |
| TC | RCLK cycle time | — | 1500 | 1800 | ns |

### Write Data Timing (2 MHz Clock)

| SYMBOL | PARAMETER | FM | MFM | UNIT |
| :--- | :--- | :---: | :---: | :---: |
| TWP | Write Data pulse width | 450-550 | 150-250 | ns |
| TWG | Write Gate to first Write Data | 2 | 1 | µs |
| TWF | Write Gate off from last Write Data | 2 | 1 | µs |
| TS/TH | Early/Late precomp window | — | 125 min | ns |

### Miscellaneous Timing

| SYMBOL | PARAMETER | VALUE | UNIT |
| :--- | :--- | :---: | :---: |
| TSTP | Step pulse width (MFM) | 2 | µs |
| TSTP | Step pulse width (FM) | 4 | µs |
| TDIR | Direction setup before Step | 12 | µs |
| TMR | Master Reset pulse minimum | 50 | µs |
| TIP | Index Pulse minimum width | 10 | µs |

---

## PACKAGE INFORMATION

### 40-Pin Ceramic DIP (FD179XA-02)

*   Max Length: 2.025"
*   Max Width: 0.610"
*   Lead Spacing: 0.100"
*   Row Spacing: 0.620" - 0.690"

### 40-Pin Plastic DIP (FD179XB-02)

*   Max Length: 2.090"
*   Max Width: 0.620"
*   Lead Spacing: 0.100"
*   Row Spacing: 0.620" - 0.690"
