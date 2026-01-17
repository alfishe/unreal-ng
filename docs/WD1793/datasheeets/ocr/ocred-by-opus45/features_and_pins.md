# WD179X Datasheet: Features and Pinout

**WESTERN DIGITAL CORPORATION**
**FD 179X-02 Floppy Disk Formatter/Controller Family (MAY 1980)**

## FEATURES

*   **TWO VFO CONTROL SIGNALS**
*   **SOFT SECTOR FORMAT COMPATIBILITY**
*   **AUTOMATIC TRACK SEEK WITH VERIFICATION**
*   **ACCOMMODATES SINGLE AND DOUBLE DENSITY FORMATS**
    *   IBM 3740 Single Density (FM)
    *   IBM System 34 Double Density (MFM)
*   **READ MODE**
    *   Single/Multiple Sector Read with Automatic Search or Entire Track Read
    *   Selectable 128 Byte or Variable length Sector
*   **WRITE MODE**
    *   Single/Multiple Sector Write with Automatic Sector Search
    *   Entire Track Write for Diskette Formatting
*   **SYSTEM COMPATIBILITY**
    *   Double Buffering of Data
    *   8 Bit Bi-Directional Bus for Data, Control and Status
    *   DMA or Programmed Data Transfers
    *   All Inputs and Outputs are TTL Compatible
    *   On-Chip Track and Sector Registers / Comprehensive Status Information
*   **PROGRAMMABLE CONTROLS**
    *   Selectable Track to Track Stepping Time
    *   Side Select Compare
*   **WRITE PRECOMPENSATION**
*   **WINDOW EXTENSION**
*   **INCORPORATES ENCODING/DECODING AND ADDRESS MARK CIRCUITRY**
*   **FD1792/4 IS SINGLE DENSITY ONLY**
*   **FD1795/7 HAS A SIDE SELECT OUTPUT**

### 179X-02 FAMILY CHARACTERISTICS

| FEATURES | 1791 | 1792 | 1793 | 1794 | 1795 | 1797 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Single Density (FM) | X | X | X | X | X | X |
| Double Density (MFM) | X | | X | | X | X |
| True Data Bus | | | X | X | | X |
| Inverted Data Bus | X | X | | | X | |
| Write Precomp | X | X | X | X | X | X |
| Side Selection Output | | | | | X | X |

---

## PIN CONFIGURATION

**40-PIN DIP PACKAGE**

```
              ┌───────────────────┐
       NC   1 │●                  │ 40  Vdd (+12V)
      /WE   2 │                   │ 39  INTRQ
      /CS   3 │                   │ 38  DRQ
      /RE   4 │                   │ 37  /DDEN
       A0   5 │                   │ 36  /WPRT
       A1   6 │                   │ 35  /IP
     DAL0   7 │                   │ 34  /TR00
     DAL1   8 │      WD179X       │ 33  /WF / VFOE
     DAL2   9 │   FLOPPY DISK     │ 32  READY
     DAL3  10 │    CONTROLLER     │ 31  WD
     DAL4  11 │                   │ 30  WG
     DAL5  12 │                   │ 29  TG43
     DAL6  13 │                   │ 28  HLD
     DAL7  14 │                   │ 27  /RAW READ
     STEP  15 │                   │ 26  RCLK
     DIRC  16 │                   │ 25  RG (1791/3) / SSO (1795/7)
    EARLY  17 │                   │ 24  CLK
     LATE  18 │                   │ 23  HLT
      /MR  19 │                   │ 22  /TEST
      Vss  20 │                   │ 21  Vcc (+5V)
              └───────────────────┘
```

---

## PIN DESCRIPTIONS

### Computer Interface Pins

| PIN | SYMBOL | NAME | TYPE | DESCRIPTION |
| :---: | :--- | :--- | :---: | :--- |
| 1 | NC | No Connection | - | Internally connected to back bias generator. **Must be left open.** |
| 2 | /WE | Write Enable | I | Active Low. Gates data from DAL into selected register when CS low. |
| 3 | /CS | Chip Select | I | Active Low. Enables computer communication with the device. |
| 4 | /RE | Read Enable | I | Active Low. Gates data from selected register onto DAL when CS low. |
| 5 | A0 | Register Select A0 | I | LSB of register address. |
| 6 | A1 | Register Select A1 | I | MSB of register address. See Register Selection table. |
| 7-14 | DAL0-DAL7 | Data Access Lines | I/O | 8-bit bidirectional bus for data transfer. **1791/2/5: Inverted logic. 1793/4/7: TRUE logic.** |
| 19 | /MR | Master Reset | I | Active Low. Resets device: loads $03 into Command Register, $FF into Track Register, $01 into Sector Register. When MR returns High, initiates RESTORE command. Status Bit 7 is reset to "1" while MR is active. |
| 20 | Vss | Ground | PWR | Ground reference (0V). |
| 21 | Vcc | +5V Supply | PWR | +5V ± 5% (4.75V to 5.25V). |
| 24 | CLK | Clock | I | Master clock input. **2 MHz for 8" drives, 1 MHz for 5.25" mini-drives.** All internal timings scale accordingly. |
| 38 | DRQ | Data Request | O | Active High. Indicates Data Register contains valid data (Read) or is empty and ready (Write). Reset when DR is accessed or by Force Interrupt. |
| 39 | INTRQ | Interrupt Request | O | Active High. Set at command completion or per Force Interrupt. Reset by reading Status Register or writing a new command to Command Register. |
| 40 | Vdd | +12V Supply | PWR | +12V ± 5% (11.4V to 12.6V). |

### Floppy Disk Interface Pins

| PIN | SYMBOL | NAME | TYPE | DESCRIPTION |
| :---: | :--- | :--- | :---: | :--- |
| 15 | STEP | Step | O | Pulse output for each head step. **2 µs (MFM) or 4 µs (FM) pulse width.** |
| 16 | DIRC | Direction | O | Head stepping direction. **High = Step In (toward track 76); Low = Step Out (toward track 0).** Valid 12 µs before first step pulse. |
| 17 | EARLY | Write Early | O | Write precompensation: indicates data pulse should be shifted early by 125 ns (MFM). |
| 18 | LATE | Write Late | O | Write precompensation: indicates data pulse should be shifted late by 125 ns (MFM). |
| 22 | /TEST | Test Mode | I | Active Low. Provides faster stepping and verify times for testing. **Tie to +5V for normal operation.** |
| 23 | HLT | Head Load Timing | I | Head engage confirmation. **High indicates head is mechanically engaged and settled.** Typical settle time: 30-100 ms. |
| 25 | RG | Read Gate (1791/3) | O | Active High when sync field (leading zeros) is detected during read. |
| 25 | SSO | Side Select Output (1795/7) | O | Side select signal controlled by 'S' flag in Type II/III commands. Updated at command start. |
| 26 | RCLK | Read Clock | I | Square-wave clock recovered from the data stream by external data separator. |
| 27 | /RAW READ | Raw Read | I | Negative pulse from drive for each flux transition. **100 ns min pulse width.** |
| 28 | HLD | Head Load | O | Head load control signal. Goes active to engage head against media. Deactivates after **15 idle revolutions** or explicitly by command. |
| 29 | TG43 | Track Greater Than 43 | O | Active High when head is positioned at tracks 44-76. Valid during Read/Write commands. Used for write precompensation control. |
| 30 | WG | Write Gate | O | Write gate output. Active before and during write operations. Controls external write amplifier enable. |
| 31 | WD | Write Data | O | Write data pulses. **250 ns pulse width (MFM), 500 ns pulse width (FM).** |
| 32 | READY | Ready | I | Drive ready status. **High indicates disk is inserted and at speed.** Sampled before Read/Write commands. |
| 33 | /WF | Write Fault (WG=1) | I | Write fault input. **Low = terminate write operation** and set Write Fault status. |
| 33 | VFOE | VFO Enable (WG=0) | O | VFO (data separator) enable. **Low = active.** |
| 34 | /TR00 | Track 00 | I | Track 0 detect input. **Low indicates head is at outermost track (Track 0).** |
| 35 | /IP | Index Pulse | I | Index pulse input from drive. **Low = index hole detected.** One pulse per disk revolution (~166 ms at 360 RPM, ~200 ms at 300 RPM). **10 µs minimum pulse width.** |
| 36 | /WPRT | Write Protect | I | Write protect sense. **Low = disk is write protected.** Checked before write commands. |
| 37 | /DDEN | Double Density Enable | I | Density select. **Low = MFM (Double Density); High = FM (Single Density).** |

---

## REGISTER SELECTION

| A1 | A0 | READ (/RE) | WRITE (/WE) |
| :---: | :---: | :--- | :--- |
| 0 | 0 | Status Register | Command Register |
| 0 | 1 | Track Register | Track Register |
| 1 | 0 | Sector Register | Sector Register |
| 1 | 1 | Data Register | Data Register |

**Notes:**
- CS must be low for register access
- Status Register is read-only
- Command Register is write-only
- Track, Sector, and Data registers are read/write
