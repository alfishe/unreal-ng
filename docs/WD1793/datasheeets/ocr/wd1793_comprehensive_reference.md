# WD1793 Floppy Disk Controller - Comprehensive Technical Reference

## Document Overview

This document is a comprehensive compilation of technical information for the Western Digital WD179X-02 family of Floppy Disk Formatter/Controllers and their compatible clones (Fujitsu MB8876A/MB8877A). It consolidates information from multiple manufacturer datasheets to provide a complete reference for implementation, emulation, and understanding of these ubiquitous floppy disk controller chips.

---

## 1. GENERAL DESCRIPTION

### 1.1 Device Family Overview

The FD179X series are MOS LSI devices which perform the functions of a Floppy Disk Formatter/Controller in a single chip implementation. The FD179X can be considered the end result of both the FD1771 and FD1781 designs, being IBM 3740 compatible in single density mode (FM) and System 34 compatible in Double Density Mode (MFM).

The FD179X contains all the features of its predecessor the FD1771, plus the added features necessary to read/write and format a double density diskette. These include:
- Address mark detection
- FM and MFM encode and decode logic
- Window extension
- Write precompensation

In order to maintain compatibility, the FD1771, FD1781, and FD179X designs were made as close as possible with the computer interface, instruction set, and I/O registers being identical. Also, head load timing is identical.

### 1.2 Family Variants

| FEATURES | 1791 | 1793 | 1795 | 1797 |
|----------|------|------|------|------|
| Single Density (FM) | X | X | X | X |
| Double Density (MFM) | X | X | X | X |
| True Data Bus | | X | | X |
| Inverted Data Bus | X | | X | |
| Write Precomp | X | X | X | X |
| Side Selection Output | | | X | X |

**Notes:**
- The 1793 is identical to the 1791 except the DAL lines are TRUE for systems that utilize true data busses
- The 1795/7 has a side select output for controlling double-sided drives
- The 1792 and 1794 are "Single Density Only" versions of the 1791 and 1793
- On the 1792/4 devices, DDEN must be left open

### 1.3 Compatible Clones

**Fujitsu MB8876A/MB8877A:**
- MB8876A: Negative-logic 8-bit Data Bus (equivalent to WD1791/1792)
- MB8877A: Positive-logic 8-bit Data Bus (equivalent to WD1793/1794)
- Upward compatible with Western Digital FD1791-02 and FD1793-02
- N-Channel Silicon-gate E/D MOS Process
- Single +5V Power Supply

---

## 2. PIN DESCRIPTIONS

### 2.1 Pin Assignment (40-Pin DIP)

```
                    ┌──────────┐
            NC* ─ 1 │          │ 40 ─ VDD (+12V) / NC*
            WE ─ 2 │          │ 39 ─ INTRQ
            CS ─ 3 │          │ 38 ─ DRQ
            RE ─ 4 │          │ 37 ─ DDEN
            A0 ─ 5 │          │ 36 ─ WPRT
            A1 ─ 6 │          │ 35 ─ IP
        *DAL0 ─ 7 │          │ 34 ─ TR00
         DAL1 ─ 8 │  WD179X  │ 33 ─ WF/VFOE
         DAL2 ─ 9 │          │ 32 ─ READY
         DAL3 ─ 10│          │ 31 ─ WD
         DAL4 ─ 11│          │ 30 ─ WG
         DAL5 ─ 12│          │ 29 ─ TG43
         DAL6 ─ 13│          │ 28 ─ HLD
         DAL7 ─ 14│          │ 27 ─ RAW READ
         STEP ─ 15│          │ 26 ─ RCLK
         DIRC ─ 16│          │ 25 ─ RG (1791/3) / SSO (1795/7)
        EARLY ─ 17│          │ 24 ─ CLK
         LATE ─ 18│          │ 23 ─ HLT
           MR ─ 19│          │ 22 ─ TEST
   (GND) VSS ─ 20│          │ 21 ─ VCC (+5V)
                    └──────────┘

* Pin 1 is internally connected to a back bias generator and must be left open
** 1791/3 = RG    1795/7 = SSO
*** 1793/7 TRUE BUS
```

### 2.2 Power Supply Pins

| Pin No. | Symbol | Pin Name | Function |
|---------|--------|----------|----------|
| 20 | VSS | Power Supply | Ground (GND) |
| 21 | VCC | Power Supply | +5V DC supply |
| 40 | VDD | Power Supply | +12V ± 5% (WD179X only; NC on Fujitsu clones) |
| 1 | NC | No Connection | Pin 1 is internally connected to a back bias generator and must be left open by the user |

**Note:** The Fujitsu MB8876A/MB8877A operates from a single +5V supply only.

### 2.3 Computer Interface Pins

| Pin No. | Symbol | I/O | Function |
|---------|--------|-----|----------|
| 2 | WE | I | **Write Enable** - A logic low on this input gates data on the DAL into the selected register when CS is low |
| 3 | CS | I | **Chip Select** - A logic low on this input selects the chip and enables computer communication with the device. When CS=0, DALs are activated and data transfer between FDC and MPU is enabled. When CS=1, DALs are in high impedance state and RE and WE are ignored |
| 4 | RE | I | **Read Enable** - A logic low on this input controls the placement of data from a selected register on the DAL when CS is low |
| 5, 6 | A0, A1 | I | **Register Select Lines** - These inputs select the register to receive/transfer data on the DAL lines under RE and WE control |
| 7-14 | DAL0-DAL7 | I/O | **Data Access Lines** - Eight bit inverted (1791/5) or true (1793/7) bidirectional bus used for transfer of data, control, and status. This bus is receiver enabled by WE or transmitter enabled by RE |
| 24 | CLK | I | **Clock** - This input requires a free-running square wave clock for internal timing reference: 2 MHz for 8" drives, 1 MHz for mini-drives |
| 38 | DRQ | O | **Data Request** - This open drain output indicates that the DR contains assembled data in Read operations, or the DR is empty in Write operations. This signal is reset when serviced by the computer through reading or loading the DR in Read or Write operations, respectively. Use 10K pull-up resistor to +5 |
| 39 | INTRQ | O | **Interrupt Request** - This open drain output is set at the completion of any command and is reset when the STATUS register is read or the command register is written to. Use 10K pull-up resistor to +5 |

### 2.4 Register Selection

| A1 | A0 | READ (RE=0) | WRITE (WE=0) | Data Access Line Status |
|----|----|--------------|--------------|-----------------------|
| 0 | 0 | Status Register (STR) | Command Register (CR) | Enabled |
| 0 | 1 | Track Register (TR) | Track Register (TR) | Enabled |
| 1 | 0 | Sector Register (SCR) | Sector Register (SCR) | Enabled |
| 1 | 1 | Data Register (DR) | Data Register (DR) | Enabled |

When CS=1: Deselected, DAL High Impedance

### 2.5 Floppy Disk Interface Pins

#### Disk Head Control Signals

| Pin No. | Symbol | I/O | Function |
|---------|--------|-----|----------|
| 15 | STEP | O | **Step** - This output contains a pulse for each step |
| 16 | DIRC | O | **Direction** - Output is active high when stepping in, active low when stepping out |
| 23 | HLT | I | **Head Load Timing** - When a logic high is found on the HLT input, the head is assumed to be engaged. Head engage time is typically 30 to 100 ms depending on drive |
| 28 | HLD | O | **Head Load** - The HLD output controls the loading of the Read/Write head against the media. When HLD=1, head is engaged on disk. When HLD=0, head is released |
| 29 | TG43 | O | **Track Greater Than 43** - This output informs the drive that the Read/Write head is positioned on any Track No. 44 thru 76. TG43=1 shows head on track 44-76, TG43=0 shows head on track 0-43. Valid only during Read and Write Commands |
| 34 | TR00 | I | **Track 00** - This input informs the FD179X that the Read/Write head is positioned over Track 00 |
| 35 | IP | I | **Index Pulse** - This input informs the FD179X when the index hole of disk is detected in the FDD |
| 36 | WPRT | I | **Write Protect** - This input is sampled whenever a Write Command is received. A logic low terminates the command and sets the Write Protect Status bit |

#### Disk Read Operation Signals

| Pin No. | Symbol | I/O | Function |
|---------|--------|-----|----------|
| 25 | RG | O | **Read Gate** (1791/3 only) - A high level on this output indicates to the data separator circuitry that a field of zeros (or ones) has been encountered, and is used for synchronization |
| 25 | SSO | O | **Side Select Output** (1795/7 only) - The logic level is directly controlled by the 'S' flag in Type II or III commands. When S=1, SSO is set to logic 1. When S=0, SSO is set to logic 0. Updated at beginning of Type II or III command. Forced to logic 0 upon MASTER RESET |
| 26 | RCLK | I | **Read Clock** - A nominal square-wave clock signal derived from the data stream must be provided to this input. Phasing (i.e. RCLK transitions) relative to RAW READ is important but polarity (RCLK high or low) is not |
| 27 | RAW READ | I | **Raw Read** - The data input signal directly from the drive. This input shall be a negative pulse for each recorded flux transition. Pulse width is typically 100-300 ns |

#### Disk Write Operation Signals

| Pin No. | Symbol | I/O | Function |
|---------|--------|-----|----------|
| 17 | EARLY | O | **Early** - Indicates that the WRITE DATA pulse occurring while Early is active (high) should be shifted early for write precompensation |
| 18 | LATE | O | **Late** - Indicates that the write data pulse occurring while Late is active (high) should be shifted late for write precompensation |
| 30 | WG | O | **Write Gate** - This output is made valid before writing is to be performed on the diskette |
| 31 | WD | O | **Write Data** - A 250 ns (MFM) or 500 ns (FM) pulse per flux transition. WD contains the unique Address marks as well as data and clock in both FM and MFM formats |
| 33 | WF/VFOE | I/O | **Write Fault / VFO Enable** - This is a bi-directional signal used to signify writing faults at the drive, and to enable the external PLO data separator. When WG=1, Pin 33 functions as a WF input. If WF=0, any write command will immediately be terminated. When WG=0, Pin 33 functions as a VFOE output. VFOE will go low during a read operation after the head has loaded and settled (HLT=1). On the 1795/7, it will remain low until the last bit of the second CRC byte in the ID field. VFOE will then go high until 8 bytes (MFM) or 4 bytes (FM) before the Address Mark. It will then go active until the last bit of the second CRC byte of the Data Field. On the 1791/3, VFOE will remain low until the end of the Data Field |
| 37 | DDEN | I | **Double Density Enable** - This pin selects either single or double density operation. When DDEN=0, double density is selected. When DDEN=1, single density is selected. This line must be left open on the 1792/4 |

### 2.6 Test Pin

| Pin No. | Symbol | I/O | Function |
|---------|--------|-----|----------|
| 22 | TEST | I | This input is used for testing purposes only and should be tied to +5V or left open by the user unless interfacing to voice coil actuated motors |

---

## 3. INTERNAL ORGANIZATION

### 3.1 Block Diagram Description

The Floppy Disk Formatter block diagram illustrates the primary sections including the parallel processor interface and the Floppy Disk interface.

#### Data Shift Register (DSR)
This 8-bit register assembles serial data from the RAW READ input during Read operations and transfers serial data to the Write Data output during Write operations.

#### Data Register (DR)
This 8-bit read/write register is used as a holding register during Disk Read and Write operations. In Disk Read operations the assembled data byte is transferred in parallel to the Data Register from the Data Shift Register. In Disk Write operations information is transferred in parallel from the Data Register to the Data Shift Register.

#### Track Register (TR)
This 8-bit register holds the track number of the current Read/Write head position. It is incremented by one every time the head is stepped in (towards track 76) and decremented by one when the head is stepped out (towards track 00). The contents of the register are compared with the recorded track number in the ID field during disk Read, Write, and Verify operations. The Track Register can be loaded from or transferred to the DAL. This Register should not be loaded when the device is busy.

#### Sector Register (SR)
This 8-bit register holds the address of the desired sector position. The contents of the register are compared with the recorded sector number in the ID field during disk Read or Write operations. The Sector Register contents can be loaded from or transferred to the DAL. This register should not be loaded when the device is busy.

#### Command Register (CR)
This 8-bit register holds the command presently being executed. This register should not be loaded when the device is busy unless the new command is a force interrupt. The command register can be loaded from the DAL, but not read onto the DAL.

#### Status Register (STR)
This 8-bit read-only register holds device Status information. The contents of STR are automatically updated according to the status of the executing Command. After the STR is read, the IRQ output is usually reset (IRQ=0) except for the Type IV Command.

### 3.2 Other Functional Blocks

#### Cycle Redundancy Check (CRC) Logic
This logic is used to check or to generate the 16-bit Cyclic Redundancy Check that is in the ID and Data fields used for error detection. The polynomial is:
```
G(X) = X^16 + X^12 + X^5 + 1
```

The CRC includes all information starting with the address mark and up to the CRC characters. The CRC register is preset to ones prior to data being shifted through the circuit.

#### Arithmetic/Logic Unit (ALU)
The ALU is a serial comparator, incrementer, and decrementer used for register modification and comparisons with the disk recorded ID field.

#### Address Mark (AM) Detector
A circuit to detect specific bit pattern data in the serial data from a disk (i.e. Index Mark, ID Address Mark, Data Address Mark).

#### Data Modulator
A circuit to modulate data to be written onto a disk in the specific recording format:
- Single density recording format: Frequency Modulation (FM)
- Double density recording format: Modified Frequency Modulation (MFM)

#### Programmable Logic Array (PLA) for Commands
A micro-program to generate control signals (Commands) which control the FDC operation. The size of micro-program is approximately 232 x 19 bits.

---

## 4. PROCESSOR INTERFACE

### 4.1 Data Transfer

The interface to the processor is accomplished through the eight Data Access Lines (DAL) and associated control signals. The DAL are three state buffers that are enabled as output drivers when Chip Select (CS) and Read Enable (RE) are active (low logic state) or act as input receivers when CS and Write Enable (WE) are active.

When transfer of data with the Floppy Disk Controller is required by the host processor, the device address is decoded and CS is made low. The address bits A1 and A0, combined with the signals RE during a Read operation or WE during a Write operation, are interpreted as selecting the following registers:

### 4.2 DMA and Programmed Data Transfers

During Direct Memory Access (DMA) types of data transfers between the Data Register of the FD179X and the processor, the Data Request (DRQ) output is used in Data Transfer control. This signal also appears as status bit 1 during Read and Write operations.

**On Disk Read operations:**
- Data Request is activated (set high) when an assembled serial input byte is transferred in parallel to the Data Register
- This bit is cleared when the Data Register is read by the processor
- If the Data Register is read after one or more characters are lost, by having new data transferred into the register prior to processor readout, the Lost Data bit is set in the Status Register
- The Read operation continues until the end of sector is reached

**On Disk Write operations:**
- Data Request is activated when the Data Register transfers its contents to the Data Shift Register, and requires a new data byte
- It is reset when the Data Register is loaded with new data by the processor
- If new data is not loaded at the time the next serial byte is required by the Floppy Disk, a byte of zeroes is written on the diskette and the Lost Data Status Bit is set in the Status Register

At the completion of every command an INTRQ is generated. INTRQ is reset by either reading the status register or by loading the command register with a new command.

---

## 5. FLOPPY DISK INTERFACE

### 5.1 Head Positioning

Five commands cause positioning of the Read-Write head (see Command Section). The period of each positioning step is specified by the r field in bits 1 and 0 of the command word. After the last directional step an additional 15 milliseconds of head settling time takes place if the Verify flag is set in Type I commands.

#### Table 1: STEPPING RATES

| CLK | 2 MHz | 2 MHz | 1 MHz | 1 MHz | 2 MHz | 1 MHz |
|-----|-------|-------|-------|-------|-------|-------|
| DDEN | 0 | 1 | 0 | 1 | X | X |
| R1 R0 | TEST=1 | TEST=1 | TEST=1 | TEST=1 | TEST=0 | TEST=0 |
| 0 0 | 3 ms | 3 ms | 6 ms | 6 ms | 184 μs | 368 μs |
| 0 1 | 6 ms | 6 ms | 12 ms | 12 ms | 190 μs | 380 μs |
| 1 0 | 10 ms | 10 ms | 20 ms | 20 ms | 198 μs | 396 μs |
| 1 1 | 15 ms | 15 ms | 30 ms | 30 ms | 208 μs | 416 μs |

The rates (shown in Table 1) can be applied to a Step-Direction Motor through the device interface. The 179X has two modes of operation according to the state of DDEN. When DDEN=0, double density (MFM) is assumed. When DDEN=1, single density (FM) is assumed.

**Step:** A 2 μs (MFM) or 4 μs (FM) pulse is provided as an output to the drive. For every step pulse issued, the drive moves one track location in a direction determined by the direction output.

**Direction (DIRC):** The Direction signal is active high when stepping in, active low when stepping out. The direction signal is valid 12 μs before the first stepping pulse is generated.

When a Seek, Step or Restore command is executed an optional verification of Read-Write head position can be performed by setting bit 2 (V=1) in the command word to a logic 1. The verification operation begins at the end of the 15 millisecond settling time after the head is loaded against the media. The track number from the first encountered ID field is compared to the Track Register. If there is a match and a valid ID CRC, the verification is complete, an interrupt is generated and the Busy status bit is reset. If there is not a match but there is valid ID CRC, an interrupt is generated, and Seek Error Status bit (Status bit 4) is set and the Busy status bit is reset. If there is a match but not a valid CRC, the CRC error status bit is set (Status bit 3), and the next encountered ID field is read from the disk for the verification operation. If an valid CRC cannot be found after four revolutions of the disk, the FD179X terminates the operation and sends an interrupt, (INTRQ).

### 5.2 Head Load Timing

The Head Load (HLD) output controls the movement of the read/write head against the media. HLD is activated at the beginning of a Type I command if the h flag is set (h=1), at the end of the Type I command if the V flag is set (V=1), or upon receipt of any Type II or III command. Once HLD is active it remains active until either a Type I command is received with (h=0 and V=0); or if the FD179X is in an idle state (busy=0) for 15 revolutions of the disk, the head will be automatically disengaged (HLD made inactive).

Head Load Timing (HLT) is an input to the FD179X which is used for the head engage time. When HLT=1, the FD179X assumes the head is completely engaged. The head engage time is typically 30 to 100 ms depending on drive. The low to high transition on HLD is typically used to fire a one shot. The output of the one shot is then used for HLT and supplied as an input to the FD179X.

```
        HLD ├───────────┐
            │           │
            ├───────────┼── 50 TO 100ms ──┤
            │           │                  │
            └───────────┘                  │
                                          │
                   HLT (FROM ONE SHOT)    │
```

**Summary for Type I commands:**
- If h=0 and V=0, HLD is reset at the beginning of the command and HLT is not sampled nor is there an internal 15 ms delay
- If h=0 and V=1, HLD is set near the end of the command, after all the steps have been issued, an internal 15 ms delay occurs and the FD179X then waits for HLT to occur
- If h=1 and V=0, HLD is set at the beginning of the command and HLT is not sampled nor is there any internal 15 ms delay
- If h=1 and V=1, HLD is set at the beginning of the command. Near the end of the command, after all the steps have been issued, an internal 15 ms delay occurs and the FD179X then waits for HLT to be true

**For Type II and III commands with E flag off:**
- HLD is made active and HLT is sampled until true. With E flag on, HLD is made active, an internal 15 ms delay occurs and then HLT is sampled until true.

During read operations (WG=0), the VFOE (Pin 33) is provided for phase lock loop synchronization. VFOE will go active when:
a) Both HLT and HLD are True
b) Settling Time, if programmed, has expired
c) The 179X is inspecting data off the disk

If WF/VFOE is not used, leave open or tie to a 10K resistor to +5.

### 5.3 Disk Read Operations

Sector lengths of 128, 256, 512 or 1024 are obtainable in either FM or MFM formats. For FM, DDEN should be placed to logical "1." For MFM formats, DDEN should be placed to a logical "0." Sector lengths are determined at format time by a special byte in the "ID" field. If this Sector length byte in the ID field is zero, then the sector length is 128 bytes. If 01, then 256 bytes. If 02, then 512 bytes. If 03, then 1024 bytes. The number of sectors per track as far as the FD179X is concerned can be from 1 to 255 sectors. The number of tracks as far as the FD179X is concerned is from 0 to 255 tracks. For IBM 3740 compatibility, sector lengths are 128 bytes with 26 sectors per track. For System 34 compatibility (MFM), sector lengths are 256 bytes/sector with 26 sectors/track; or lengths of 1024 bytes/sector with 8 sectors/track.

For read operations, the FD179X requires RAW READ Data (Pin 27) signal which is a 250 ns pulse per flux transition and a Read clock (RCLK) signal to indicate flux transition spacings. The RCLK (Pin 26) signal is provided by some drives but if not it may be derived externally by Phase lock loops, one shots, or counter techniques. In addition, a Read Gate Signal is provided as an output (Pin 25) which can be used to inform phase lock loops when to acquire synchronization.

When reading from the media in FM, RG is made true when 2 bytes of zeroes are detected. The FD179X must find an address mark within the next 10 bytes; otherwise RG is reset and the search for 2 bytes of zeroes begins all over again. If an address mark is found within 10 bytes, RG remains true as long as the FD179X is deriving any useful information from the data stream.

Similarly for MFM, RG is made active when 4 bytes of "00" or "FF" are detected. The FD179X must find an address mark within the next 16 bytes, otherwise RG is reset and search resumes.

### 5.4 Disk Write Operation

When writing is to take place on the diskette the Write Gate (WG) output is activated, allowing current to flow into the Read/Write head. As a precaution to erroneous writing the first data byte must be loaded into the Data Register in response to a Data Request from the FD179X before the Write Gate signal can be activated.

Writing is inhibited when the Write Protect input is a logic low, in which case any Write command is immediately terminated, an interrupt is generated and the Write Protect status bit is set. The Write Fault input, when activated, signifies a writing fault condition detected in disk drive electronics such as failure to detect write current flow when the Write Gate is activated. On detection of this fault the FD179X terminates the current command, and sets the Write Fault bit (bit 5) in the Status Word. The Write Fault input should be made inactive when the Write Gate output becomes inactive.

For write operations, the FD179X provides Write Gate (Pin 30) and Write Data (Pin 31) outputs. Write data consists of a series of 500 ns pulses in FM (DDEN=1) and 250 ns pulses in MFM (DDEN=0). Write Data provides the unique address marks in both formats.

Also during write, two additional signals are provided for write precompensation. These are EARLY (Pin 17) and LATE (Pin 18). EARLY is active true when the WD pulse appearing on (Pin 30) is to be written early. LATE is active true when the WD pulse is to be written late. If both EARLY and LATE are low when the WD pulse is present, the WD pulse is to be written at nominal. Since write precompensation values vary from disk manufacturer to disk manufacturer, the actual value is determined by several one shots or delay lines which are located external to the FD179X. The write precompensation signals EARLY and LATE are valid for the duration of WD in both FM and MFM formats.

---

## 6. COMMAND DESCRIPTION

### 6.1 Command Overview

The FD179X will accept eleven commands. Command words should only be loaded in the Command Register when the Busy status bit is off (Status bit 0). The one exception is the Force Interrupt command. Whenever a command is being executed, the Busy status bit is set. When a command is completed, an interrupt is generated and the Busy status bit is reset.

The Status Register indicates whether the completed command encountered an error or was fault free. For ease of discussion, commands are divided into four types. Commands andடypes are summarized in Table 2.

#### Table 2: COMMAND SUMMARY

|  | BITS | | | | | | | |
|--|------|--|--|--|--|--|--|--|
| TYPE COMMAND | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| I | Restore | 0 | 0 | 0 | 0 | h | V | r₁ | r₀ |
| I | Seek | 0 | 0 | 0 | 1 | h | V | r₁ | r₀ |
| I | Step | 0 | 0 | 1 | u | h | V | r₁ | r₀ |
| I | Step In | 0 | 1 | 0 | u | h | V | r₁ | r₀ |
| I | Step Out | 0 | 1 | 1 | u | h | V | r₁ | r₀ |
| II | Read Sector | 1 | 0 | 0 | m | F₂ | E | F₁ | 0 |
| II | Write Sector | 1 | 0 | 1 | m | F₂ | E | F₁ | a₀ |
| III | Read Address | 1 | 1 | 0 | 0 | 0 | E | 0 | 0 |
| III | Read Track | 1 | 1 | 1 | 0 | 0 | E | 0 | 0 |
| III | Write Track | 1 | 1 | 1 | 0 | E | 0 | 0 | 0 |
| IV | Force Interrupt | 1 | 1 | 0 | 1 | I₃ | I₂ | I₁ | I₀ |

**Note:** Bits shown in TRUE form.

### 6.2 Flag Summary

#### TYPE I COMMANDS

**h = Head Load Flag (Bit 3)**
- h = 1, Load head at beginning
- h = 0, Unload head at beginning

**V = Verify flag (Bit 2)**
- V = 1, Verify on destination track
- V = 0, No verify

**r₁r₀ = Stepping motor rate (Bits 1-0)**
- Refer to Table 1 for rate summary

**u = Update flag (Bit 4)**
- u = 1, Update Track register
- u = 0, No update

#### TYPE II & III COMMANDS

**m = Multiple Record flag (Bit 4)**
- m = 0, Single Record
- m = 1, Multiple Records

**a₀ = Data Address Mark (Bit 0)**
- a₀ = 0, FB (Data Mark)
- a₀ = 1, F8 (Deleted Data Mark)

**E = 15 ms Delay (2MHz)**
- E = 1, 15 ms delay
- E = 0, no 15 ms delay

**F₂ = S = Side Select Flag (1791/3 only)**
- S = 0, Compare for Side 0
- S = 1, Compare for Side 1

**F₁ = C = Side Compare Flag (1791/3 only)**
- C = 0, disable side select compare
- C = 1, enable side select compare

**F₁ = S = Side Select Flag (Bit 1, 1795/7 only)** (Bit 3, 1975/7 only)
- S = 0, Update SSO to 0
- S = 1, Update SSO to 1

**F₂ = b = Sector Length Flag (Bit 3, 1975/7 only)**

| Sector Length Field | | | | |
|---------------------|--|--|--|--|
| | 00 | 01 | 10 | 11 |
| b = 0 | 256 | 512 | 1024 | 128 |
| b = 1 | 128 | 256 | 512 | 1024 |

#### TYPE IV COMMAND

**I₃-I₀ = Interrupt Condition flags (Bits 3-0)**
- I₀ = 1, Not-Ready to Ready Transition
- I₁ = 1, Ready to Not-Ready Transition
- I₂ = 1, Index Pulse
- I₃ = 1, Immediate Interrupt (requires reset, see Note)

**NOTE:** If I₀ - I₃ = 0, there is no interrupt generated but the current command is terminated and busy is reset. *This is the only command that will enable the immediate interrupt to clear on a subsequent Load Command Register or Read Status Register.*

---

## 7. TYPE I COMMANDS

The Type I Commands include the Restore, Seek, Step, Step-In, and Step-Out commands. Each of the Type I Commands contains a rate field (r₀r₁), which determines the stepping motor rate as defined in Table 1.

### 7.1 RESTORE (SEEK TRACK 0)

Upon receipt of this command the Track 00 (TR00) input is sampled. If TR00 is active low indicating the Read-Write head is positioned over track 0, the Track Register is loaded with zeroes and an interrupt is generated. If TR00 is not active low, stepping pulses (pins 15 to 16) at a rate specified by the r₁r₀ field are issued until the TR00 input is activated. At this time the Track Register is loaded with zeroes and an interrupt is generated. If the TR00 input does not go active low after 255 stepping pulses, the FD179X terminates operation, interrupts, and sets the Seek error status bit.

A verification operation takes place if the V flag is set. The h bit allows the head to be loaded at the start of command. Note that the Restore command is executed when MR goes from an active to an inactive state.

### 7.2 SEEK

This command assumes that the Track Register contains the track number of the current position of the Read-Write head and the Data Register contains the desired track number. The FD179X will update the Track register and issue stepping pulses in the appropriate direction until the contents of the Track register are equal to the contents of the Data Register (the desired track location). A verification operation takes place if the V flag is on. The h bit allows the head to be loaded at the start of command. An interrupt is generated at the completion of the command.

### 7.3 STEP

Upon receipt of this command, the FD179X issues one stepping pulse to the disk drive. The stepping motor direction is the same as in the previous step command. After a delay determined by the r₁r₀ field, a verification takes place if the V flag is on. If the u flag is on, the Track Register is updated by one for each step. The h bit allows head to be loaded at the start of the command. An interrupt is generated at the completion of the command.

### 7.4 STEP-IN

Upon receipt of this command, the FD179X issues one stepping pulse in the direction towards track 76. If the u flag is on, the Track Register is incremented by one. After a delay determined by the r₁r₀ field, a verification takes place if the V flag is on. The h bit allows the head to be loaded at the start of the command. An interrupt is generated at the completion of the command.

### 7.5 STEP-OUT

Upon receipt of this command, the FD179X issues one stepping pulse in the direction towards track 0. If the u flag is on, the Track Register is decremented by one. After a delay determined by the r₁r₀ field, a verification takes place if the V flag is on. The h bit allows the head to be loaded at the start of the command. An interrupt is generated at the completion of the command.

On the 1795/7 devices, the SSO output is not affected during Type 1 commands, and an internal side compare does not take place when the (V) Verify Flag is on.

---

## 8. TYPE II COMMANDS

The Type II Commands are the Read Sector and Write Sector commands. Prior to loading the Type II Command into the Command Register, the computer must load the Sector Register with the desired sector number. Upon receipt of the Type II command, the busy status bit is set. If the E flag is set (E=1, this is the normal case) HLD is made active and HLT is sampled after a 15 msec delay. If the E flag is 0, the head is loaded and HLT sampled with no 15 msec delay. The ID field and Data Field format are shown on page 13.

When an ID field is located on the disk, the FD179X compares the Track Number on the ID field with the Track Register. If there is not a match, the next encountered ID field is read and a comparison is again made. If there was a match, the Sector Number of the ID field is compared with the Sector Register. If there is not a Sector match, the next encountered ID field is read off the disk and comparisons again made. If the ID field CRC is correct, the data field is then located and will be either written into, or read from depending upon the command. The FD179X must find an ID field with a Track number, Sector number, and CRC within four revolutions of the disk; otherwise, the Record-Not-Found status bit is set (Status bit 3) and the command is terminated with an interrupt.

The Type II commands also contain side select compare flags. When C=0, no side comparison is made. When C=1, the LSB of the side number is read off the ID Field of the disk and compared with the contents of the (S) flag. If the S flag compares with the side number recorded in the ID field, the 179X continues with the ID search. If not a comparison is not made within 5 index pulses, the interrupt line is made active and the Record-Not-Found status bit is set.

The 1795/7 READ SECTOR and WRITE SECTOR commands include a 'b' flag. The 'b' flag, in conjunction with the sector length byte of the ID Field, allows different byte lengths to be implemented in each sector. For IBM compatibility, the 'b' flag should be set to a one. The 's' flag allows direct control over the SSO Line (Pin 25) and is set or reset at the beginning of the command, dependent upon the value of this flag.

Each of the Type II Commands contains an (m) flag which determines if multiple records (sectors) are to be read or written, depending upon the command. If m=0, a single sector is read or written and an interrupt is generated at the completion of the command. If m=1, multiple records are read or written with the sector register internally updated so that an address verification can occur on the next record. The FD179X will continue to read or write multiple records and update the sector register in numerical ascending sequence until the sector register exceeds the number of sectors on the track, the Force Interrupt command is loaded into the Command Register, which terminates the command and generates an interrupt.

If the Sector Register exceeds the number of sectors on the track, the Record-Not-Found status bit will be set.

### 8.1 READ SECTOR

Upon receipt of the Read Sector command, the head is loaded (HLD active) and the Busy status bit set. When an ID field is encountered that has the correct track number, correct sector number, correct side number, and correct CRC, the data field is presented to the computer. The Data Address Mark of the data field must be found within 30 bytes in single density and 43 bytes in double density from the CRC field and the Write Gate output; otherwise, the Record Not Found status bit is set and the operation is terminated.

When the first character or byte of the data field has been shifted through the DSR, it is transferred to the DR, and DRQ is generated. When the next byte is accumulated in the DSR it is transferred to the DR, and another DRQ is generated. If the computer has not read the previous contents of the DR before a new character is transferred that character is lost and the Lost Data Status bit is set.

At the end of the Read operation, the type of Data Address Mark encountered in the data field is recorded in the Status Register (bit 5) as shown below:

| STATUS BIT 5 | |
|--------------|--|
| 1 | Deleted Data Mark |
| 0 | Data Mark |

Although the CRC characters are transferred to the computer, the FD179X checks for validity and the CRC error status bit is set if there is a CRC error. The Track Address of the ID field is written into the sector register. At the end of the operation an interrupt is generated and the Busy Status is reset.

### 8.2 WRITE SECTOR

Upon receipt of the Write Sector command, the head is loaded (HLD active) and the Busy status bit set. When an ID field is encountered that has the correct track number, correct sector number, correct side number, and correct CRC, a DRQ is generated. The FD179X counts off 11 bytes in single density and 22 bytes in double density from the CRC field and the Write Gate (WG) output is made active if the DRQ is serviced (i.e., the DR has been loaded by the computer). If DRQ has not been serviced, the command is terminated and the Lost Data status bit is set. If DRQ has been serviced, the WG is made active and six bytes of zeroes in single density and 12 bytes in double density are then written on the disk. At this time the Data Address Mark is then written on the disk as determined by the command as shown below:

| a₀ = | Data Address Mark (Bit 0) |
|------|---------------------------|
| 1 | Deleted Data Mark |
| 0 | Data Mark |

The FD179X then writes the data field and generates DRQ's to the computer. If the DRQ is not serviced in time for continuous writing the Lost Data Status Bit is set and a byte of zeroes is written on the disk. The command is not terminated. After the last data byte has been written on the disk, the two-byte CRC is computed internally and written on the disk followed by one byte of logic ones in FM or in MFM. The WG output is then deactivated.

---

## 9. TYPE III COMMANDS

### 9.1 READ ADDRESS

Upon receipt of the Read Address command, the head is loaded and the Busy Status bit is set. The next encountered ID field is then read in from the disk, and the six data bytes of the ID field are assembled and transferred to the DR, and a DRQ is generated for each byte. The six bytes of the ID field are shown below:

| TRACK ADDR | SIDE NUMBER | SECTOR ADDRESS | SECTOR LENGTH | CRC 1 | CRC 2 |
|------------|-------------|----------------|---------------|-------|-------|
| 1 | 2 | 3 | 4 | 5 | 6 |

Although the CRC characters are transferred to the computer, the FD179X checks for validity and the CRC error status bit is set if there is a CRC error. The Track Address of the ID field is written into the sector register. At the end of the operation an interrupt is generated and the Busy Status is reset.

**Note:** In MFM only, IDAM and DATA AM are preceded by three bytes of A1 with clock transition between bits 4 and 5 missing.

### 9.2 READ TRACK

Upon receipt of the Read Track command, the head is loaded and the Busy Status bit is set. Reading starts with the leading edge of the first encountered index pulse and continues until the next index pulse. As each byte is assembled it is transferred to the Data Register and the Data Request is generated for each byte. No CRC checking is performed. Gaps are included in the data. The accumulation of bytes is synchronized to each Address Mark encountered. An internal side compare is not performed during a Read Track.

### 9.3 WRITE TRACK (FORMAT)

Formatting the disk is a relatively simple task when operating programmed I/O or when operating under Formatting the disk is accomplished by positioning the R/W head over the desired track number and issuing the Write Track command. Upon receipt of the Write Track command, the head is loaded and the Busy status bit is set.

Writing starts with the leading edge of the first encountered index pulse and continues until the next index pulse, at which time the interrupt is activated. The Data Request is activated immediately upon receipt of the command, but writing will not start until after the first byte has been loaded into the Data Register. If the DR has not been loaded by the time the index pulse is encountered the operation is terminated making the device Not Busy, the Lost Data Status Bit is set, and the interrupt is activated.

Normally, whatever data pattern appears in the Data Register is written on the disk with a normal clock pattern. However, if the FD179X detects a data pattern of F5 thru FE in the data register, this is interpreted as data address marks with missing clocks or CRC generation.

#### CONTROL BYTES FOR INITIALIZATION

| DATA PATTERN IN DR (HEX) | FD179X INTERPRETATION IN FM (DDEN = 1) | FD1791/3 INTERPRETATION IN MFM (DDEN = 0) |
|--------------------------|----------------------------------------|-------------------------------------------|
| 00 thru F4 | Write 00 thru F4 with CLK = FF | Write 00 thru F4, in MFM |
| F5 | Not Allowed | Write A1* in MFM, Preset CRC |
| F6 | Not Allowed | Write C2** in MFM |
| F7 | Generate 2 CRC bytes | Generate 2 CRC bytes |
| F8 thru FB | Write F8 thru FB, Clk = C7, Preset CRC | Write F8 thru FB, in MFM |
| FC | Write FC with Clk = D7 | Write FC in MFM |
| FD | Write FD with Clk = FF | Write FD in MFM |
| FE | Write FE, Clk = C7, Preset CRC | Write FE in MFM |
| FF | Write FF with Clk = FF | Write FF in MFM |

*Missing clock transition between bits 4 and 5
**Missing clock transition between bits 3 & 4

---

## 10. TYPE IV COMMAND

### 10.1 FORCE INTERRUPT

This command can be loaded into the command register at any time. If there is a current command under execution (Busy Status Bit set), the command will be terminated and an interrupt will be generated when the condition specified in the I₃-I₀ field is detected. The interrupt conditions are shown below:

- I₀ = Not-Ready-To-Ready Transition
- I₁ = Ready-To-Not-Ready Transition
- I₂ = Every Index Pulse
- I₃ = Immediate Interrupt (requires reset, see Note)

**NOTE:** If I₀ - I₃ = 0, there is no interrupt generated but the current command is terminated and busy is reset.

This command can be loaded into the command register at any time. If there is a current command under execution (Busy Status Bit set), the command will be terminated and an interrupt will be generated when the condition specified in the I₃ - I₀ field is detected.

If the Force Interrupt Command is received when there is not a current command under execution, the Busy status bit is reset, and the rest of the status bits are unchanged. If the Force Interrupt command is received when there is not a current command under execution, the Busy Status bit is reset and the rest of the status bits are updated or cleared. In this case, Status reflects the Type I commands.

---

## 11. STATUS DESCRIPTION

Upon receipt of any command, except the Force Interrupt command, the Busy Status bit is set and the rest of the status bits are updated or cleared for the new command. If the Force Interrupt Command is received when there is a current command under execution, the Busy status bit is reset, and the rest of the status bits are unchanged.

The format of the Status Register is shown below:

| (BITS) | | | | | | | | |
|--------|--|--|--|--|--|--|--|--|
| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| S7 | S6 | S5 | S4 | S3 | S2 | S1 | S0 |

Status varies according to the type of command executed as shown in Table 6.

### Table 6: STATUS REGISTER SUMMARY

| BIT | ALL TYPE I COMMANDS | READ ADDRESS | READ SECTOR | READ TRACK | WRITE SECTOR | WRITE TRACK |
|-----|---------------------|--------------|-------------|------------|--------------|-------------|
| S7 | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY |
| S6 | WRITE PROTECT | 0 | 0 | 0 | WRITE PROTECT | WRITE PROTECT |
| S5 | HEAD LOADED | 0 | RECORD TYPE | 0 | WRITE FAULT | WRITE FAULT |
| S4 | SEEK ERROR | RNF | RNF | 0 | RNF | 0 |
| S3 | CRC ERROR | CRC ERROR | CRC ERROR | 0 | CRC ERROR | 0 |
| S2 | TRACK 0 | LOST DATA | LOST DATA | LOST DATA | LOST DATA | LOST DATA |
| S1 | INDEX | DRQ | DRQ | DRQ | DRQ | DRQ |
| S0 | BUSY | BUSY | BUSY | BUSY | BUSY | BUSY |

### 11.1 STATUS FOR TYPE I COMMANDS

| BIT NAME | MEANING |
|----------|---------|
| S7 NOT READY | This bit when set indicates the drive is not ready. When reset it indicates that the drive is ready. This bit is an inverted copy of the Ready input and logically 'ored' with MR. |
| S6 PROTECTED | When set, indicates Write Protect is activated. This bit is an inverted copy of WPRT input. |
| S5 HEAD LOADED | When set, it indicates the head is loaded and engaged. This bit is a logical "and" of HLD and HLT signals. |
| S4 SEEK ERROR | When set, the desired track was not verified. This bit is reset to 0 when updated. |
| S3 CRC ERROR | CRC encountered in ID field. This bit is reset to 0 when updated. |
| S2 TRACK 00 | When set, indicates Read/Write head is positioned to Track 0. This bit is an inverted copy of the TR00 input. |
| S1 INDEX | When set, indicates index mark detected from drive. This bit is an inverted copy of the IP input. |
| S0 BUSY | When set command is in progress. When reset no command is in progress. |

### 11.2 STATUS FOR TYPE II AND III COMMANDS

| BIT NAME | MEANING |
|----------|---------|
| S7 NOT READY | This bit when set indicates the drive is not ready. When reset, it indicates that the drive is ready. This bit is an inverted copy of the Ready input and 'ored' with MR. The Type II and III Commands will not execute unless the drive is ready. |
| S6 WRITE PROTECT | On Read Record: Not Used. On Read Track: Not Used. On any Write: It indicates a Write Protect. This bit is reset when updated. |
| S5 RECORD TYPE/ WRITE FAULT | On Read Record: It indicates the record-type code from data field address mark. 1 = Deleted Data Mark. 0 = Data Mark. On any Write: It indicates a Write Fault. This bit is reset when updated. |
| S4 RECORD NOT FOUND (RNF) | When set, it indicates that the desired track, sector, or side were not found. This bit is reset when updated. |
| S3 CRC ERROR | If S4 is set, an error is found in one or more ID fields; otherwise it indicates error in data field. This bit is reset when updated. |
| S2 LOST DATA | When set, it indicates the computer did not respond to DRQ in one byte time. This bit is reset to zero when updated. |
| S1 DATA REQUEST | This bit is a copy of the DRQ output. When set, it indicates the DR is full on a Read Operation or the DR is empty on a Write operation. This bit is reset to zero when updated. |
| S0 BUSY | When set, command is under execution. When reset, no command is under execution. |

---

## 12. DISK FORMATS

### 12.1 IBM 3740 FORMAT - 128 BYTES/SECTOR (Single Density - FM)

This single-density (FM) format utilizes 128 bytes/sector. The bytes to be generated by the system MPU for use in the execution of the Write Track command are shown below.

| NUMBER OF BYTES | HEX VALUE OF BYTE WRITTEN |
|-----------------|---------------------------|
| 40 | FF (or 00)¹ |
| 6 | 00 |
| 1 | FC (Index Mark) |
| 26 | FF (or 00) |
| 6 | 00 |
| 1 | FE (ID Address Mark) |
| 1 | Track Number (00-4A) |
| 1 | Side Number (00 or 01) |
| 1 | Sector Number (01-1A) |
| 1 | Sector Length (00 = 128 bytes) |
| 1 | F7 (Causes 2 CRC's to be written) |
| 11 | FF (Gap 2 - ID Gap) |
| 6 | 00 |
| 1 | FB (Data Address Mark) |
| 128 | DATA (E5 for IBM) |
| 1 | F7 (Causes 2 CRC's to be written) |
| 27 | FF (Part of Gap 3 - Data Gap) |
| 247²| FF (Gap 4 - Pre-Index) |

**Notes:**
1. This pattern must be written 26 times per track
2. Continue writing Hex FF until FDC completes sequence and generates INTRQ interrupt

### 12.2 IBM SYSTEM 34 FORMAT - 256 BYTES/SECTOR (Double Density - MFM)

This double-density (MFM) format utilizes 256 bytes/sector. The bytes to be generated by the system MPU for use in the execution of the Write Track command are shown below.

| NUMBER OF BYTES | HEX VALUE OF BYTE WRITTEN |
|-----------------|---------------------------|
| 80 | 4E |
| 12 | 00 |
| 3 | F6 (Writes C2) |
| 1 | FC (Index Mark) |
| 50* | 4E |
| 12 | 00 |
| 3 | F5 (Writes ID AM Sync Bytes - A1*) |
| 1 | FE (ID Address Mark) |
| 1 | Track Number (00-4C) |
| 1 | Side Number (0 or 1) |
| 1 | Sector Number (01-1A) |
| 1 | Sector Length (01 = 256 Bytes) |
| 1 | F7 (Causes 2-Byte CRC to be written) |
| 22 | 4E (Gap 2 - ID Gap) |
| 12 | 00 |
| 3 | F5 (Writes ID AM Sync Bytes) |
| 1 | FB (Data Address Mark) |
| 256 | DATA (40 for IBM) |
| 1 | F7 (Causes 2-Byte CRC to be written) |
| 54 | 4E (Part of Gap 3 - Data Gap) |
| 598** | 4E (Gap 4 - Pre-Index) |

**Notes:**
* Write bracketed field 26 times
** Continue writing Hex 4E until FD179X interrupts out. Approx. 598 bytes.

### 12.3 NON-IBM FORMATS

Variations in the IBM format are possible to a limited extent if the following requirements are met:
- Sector size must be a choice of 128, 256, 512, or 1024 bytes
- Gap size must be according to the following table
- Note that the Index Mark is not required by the 179X

The minimum gap sizes shown are that which is required by the 179X, with PLL lock-up time, motor speed variation, etc., adding additional bytes.

| | FM | MFM |
|--|----|----|
| Gap I | 16 bytes FF | 32 bytes 4E |
| Gap II | 11 bytes FF | 22 bytes 4E |
| * | 6 bytes 00 | 12 bytes 00 + 3 bytes A1 |
| Gap III | 10 bytes FF | 24 bytes 4E + 3 bytes A1 |
| ** | 4 bytes 00 | 8 bytes 00 |
| Gap IV | 16 bytes FF | 16 bytes 4E |

*Byte counts must be exact.
**Byte counts are minimum, except exactly 3 bytes of A1 must be written.

### 12.4 ID Field and Data Field Structure

```
ID FIELD:
┌─────┬──────┬────────┬────────┬─────────────┬───────┬───────┐
│ GAP │ SYNC │ ID AM  │ TRACK  │ SIDE NUMBER │SECTOR │SECTOR │
│ III │      │        │ NUMBER │             │ADDRESS│LENGTH │
└─────┴──────┴────────┴────────┴─────────────┴───────┴───────┘
                                                      ↓
                                              ┌───────┬───────┐
                                              │ CRC 1 │ CRC 2 │
                                              └───────┴───────┘

DATA FIELD:
┌─────┬──────┬────────┬──────────────────────┬───────┬───────┐
│ GAP │ SYNC │ DATA   │        DATA          │ CRC 1 │ CRC 2 │
│ II  │      │ AM     │    (User Data)       │       │       │
└─────┴──────┴────────┴──────────────────────┴───────┴───────┘
```

In MFM only, IDAM and DATA AM are preceded by three bytes of A1 with clock transition between bits 4 and 5 missing.

### 12.5 Sector Length Encoding

| Sector Length Field (hex) | Number of Bytes in Sector (decimal) |
|---------------------------|-------------------------------------|
| 00 | 128 |
| 01 | 256 |
| 02 | 512 |
| 03 | 1024 |

### 12.6 Detailed Track Layout

#### Single Density (FM) Track Structure

```
IP (Index Pulse)
│
▼
┌─────────┬──────────┬─────────────┬──────────┬─────────────┬──────────┐
│  GAP 4  │  GAP 1   │ ID RECORD 1 │  GAP 2   │DATA RECORD 1│  GAP 3   │ ...
│PRE-INDEX│POST INDEX│             │  (ID)    │             │ (DATA)   │
│32 BYTES │          │             │          │             │          │
└─────────┴──────────┴─────────────┴──────────┴─────────────┴──────────┘
                     ▲                         ▲
                     │                         │
              ┌──────┴──────┐           ┌──────┴──────┐
              │ ID FIELD    │           │ DATA FIELD  │
              ├─────────────┤           ├─────────────┤
              │ ID ADDR MARK│           │DATA/DEL MARK│
              │ TRACK ADDR  │           │128 BYTES    │
              │ ZEROES      │           │ USER DATA   │
              │ SECTOR ADDR │           │CRC BYTE 1   │
              │ ZEROES      │           │CRC BYTE 2   │
              │ CRC BYTE 1  │           └─────────────┘
              │ CRC BYTE 2  │
              └─────────────┘
```

#### Detailed Sector Structure (FM - Single Density)

| Component | Byte No. | Data | Description |
|-----------|----------|------|-------------|
| GAP 2 | | HEX FF | Variable length gap |
| SYNC | 6 bytes | HEX 00 | Synchronization field |
| ID AM | 1 byte | FE | ID Address Mark (with clock C7) |
| Track Address | 1 byte | 00-4A | Track number |
| Side Number | 1 byte | 00/01 | Disk side |
| Sector Address | 1 byte | 01-1A | Sector number |
| Sector Length | 1 byte | 00-03 | Encoded sector size |
| CRC 1 | 1 byte | -- | CRC high byte |
| CRC 2 | 1 byte | -- | CRC low byte |
| GAP 2 | 11 bytes | HEX FF | Inter-field gap |
| SYNC | 6 bytes | HEX 00 | Synchronization |
| DATA AM | 1 byte | FB/F8 | Data Address Mark |
| User Data | 128-1024 | -- | Sector data |
| CRC 1 | 1 byte | -- | CRC high byte |
| CRC 2 | 1 byte | -- | CRC low byte |
| GAP 3 | 27+ bytes | HEX FF | Post-data gap |

#### Detailed Sector Structure (MFM - Double Density)

| Component | Byte No. | Data | Description |
|-----------|----------|------|-------------|
| GAP 2 | 22 bytes | HEX 4E | Pre-ID gap |
| SYNC | 12 bytes | HEX 00 | Synchronization field |
| A1 Sync | 3 bytes | A1* | Missing clock sync |
| ID AM | 1 byte | FE | ID Address Mark |
| Track Address | 1 byte | 00-4C | Track number |
| Side Number | 1 byte | 00/01 | Disk side |
| Sector Address | 1 byte | 01-1A | Sector number |
| Sector Length | 1 byte | 00-03 | Encoded sector size |
| CRC 1 | 1 byte | -- | CRC high byte |
| CRC 2 | 1 byte | -- | CRC low byte |
| GAP 2 | 22 bytes | HEX 4E | Inter-field gap |
| SYNC | 12 bytes | HEX 00 | Synchronization |
| A1 Sync | 3 bytes | A1* | Missing clock sync |
| DATA AM | 1 byte | FB/F8 | Data Address Mark |
| User Data | 256-1024 | -- | Sector data |
| CRC 1 | 1 byte | -- | CRC high byte |
| CRC 2 | 1 byte | -- | CRC low byte |
| GAP 3 | 54+ bytes | HEX 4E | Post-data gap |

*A1 with missing clock transition between bits 4 and 5

### 12.7 Sectors Per Track Guidelines

| Format | Sector Size | Typical Sectors/Track |
|--------|-------------|----------------------|
| IBM 3740 (FM) | 128 bytes | 26 |
| IBM System 34 (MFM) | 256 bytes | 26 |
| IBM System 34 (MFM) | 512 bytes | 15 |
| IBM System 34 (MFM) | 1024 bytes | 8 |

---

## 13. ELECTRICAL CHARACTERISTICS

### 13.1 Maximum Ratings

| Parameter | Symbol | Rating | Unit |
|-----------|--------|--------|------|
| Voltage on any pin | VCC, VIN, VOUT | VSS - 0.3 to VSS + 7.0 | V |
| VDD With Respect to VSS (Ground) | | =15 to -0.3 | V |
| Max. Voltage to Any Input With Respect to VSS | | =15 to -0.3 | V |
| Operating Temperature | TA | 0 to 70 | °C |
| Storage Temperature | Tstg | -55 to +125/+150 | °C |

**Note:** Permanent device damage may occur if Absolute Maximum Ratings are exceeded. Functional operation should be restricted to the conditions as detailed in the operational sections of this data sheet. Exposure to absolute maximum rating conditions for extended periods may affect device reliability.

### 13.2 Recommended Operating Conditions (Fujitsu MB8876A/MB8877A)

| Parameter | Symbol | Min | Typ | Max | Unit |
|-----------|--------|-----|-----|-----|------|
| Supply Voltage | VCC | 4.75 | 5.00 | 5.25 | V |
| Supply Voltage | VSS | - | 0 | - | V |
| Input High Voltage | VIH | 2.0 | - | VCC | V |
| Input Low Voltage | VIL | -0.3 | - | 0.8 | V |

Operating Temperature: 0°C to +70°C

### 13.3 Operating Characteristics (DC)

TA = 0°C to 70°C, VDD = + 12V ± .6V, Vss = 0V, Vcc = + 5V ± .25V

| Parameter | Symbol | Min | Typ | Max | Unit | Conditions |
|-----------|--------|-----|-----|-----|------|------------|
| Input Leakage | IIL | | | 10 | μA | VIN = VDD |
| Output Leakage | IOL | | | 10 | μA | VOUT = VDD |
| Input High Voltage | VIH | 2.6 | | | V | |
| Input Low Voltage | VIL | | | 0.8 | V | |
| Output High Voltage | VOH | 2.8 | | | V | IO = -100 μA |
| Output Low Voltage | VOL | | | 0.45 | V | IO = 1.6 mA |
| Power Dissipation | PD | | | 0.5 | W | |

### 13.4 DC Characteristics (Fujitsu MB8876A/MB8877A)

| Parameter | Symbol | Min | Typ | Max | Unit |
|-----------|--------|-----|-----|-----|------|
| Output High Voltage (IOH = -200μA) | VOH | 2.4 | - | - | V |
| Output Low Voltage (IOL = 1.8mA) | VOL | - | - | 0.4 | V |
| Three-State (Off-State) Input Current (VIN = 0.4V to 2.4) | ITSI | - | - | 10 | μA |
| Input Leakage Current (See Note 1) | IIN1 | - | - | 2.5 | μA |
| Input Leakage Current (See Note 2) | IIN2 | - | - | 100 | μA |
| Output Leakage Current for Off-State (VOH = 2.4V) | ILOH | - | - | 10 | μA |
| Power Consumption | PD | - | - | 350 | mW |

**Notes:**
1. Except for HLT, TEST, WF, WPRT, and DDEN. (VIN = 0 to 5.25V)
2. For HLT, TEST, WF, WPRT, and DDEN. (VIN = 0 to 5.25V)

---

## 14. AC TIMING CHARACTERISTICS

TA = 0°C to 70°C, VDD = + 12V ± .6V, Vss = 0V, Vcc = +5V ± .25V

### 14.1 READ ENABLE TIMING (From FDC)

| Parameter | Symbol | Min | Typ | Max | Unit | Conditions |
|-----------|--------|-----|-----|-----|------|------------|
| Setup ADDR & CS to RE | TSET | 50 | | | nsec | |
| Hold ADDR & CS from RE | THLD | 10 | | | nsec | |
| RE Pulse Width | TRE | 400 | | | nsec | CL = 50 pf |
| DRQ Reset from RE | TDRR | | 400 | 500 | nsec | |
| INTRQ Reset from RE | TIRR | | 500 | 3000 | nsec | See Note 5 |
| Data Access from RE | TDACC | | | 350 | nsec | CL = 50 pf |
| Data Hold From RE | TDOH | 50 | | 150 | nsec | CL = 50 pf |

### 14.2 WRITE ENABLE TIMING (To FDC)

| Parameter | Symbol | Min | Typ | Max | Unit | Conditions |
|-----------|--------|-----|-----|-----|------|------------|
| Setup ADDR & CS to WE | TSET | 50 | | | nsec | |
| Hold ADDR & CS from WE | THLD | 10 | | | nsec | |
| WE Pulse Width | TWE | 350 | | | nsec | |
| DRQ Reset from WE | TDRR | | 400 | 500 | nsec | |
| INTRQ Reset from WE | TIRR | | 500 | 3000 | nsec | See Note 5 |
| Data Setup to WE | TDS | 250 | | | nsec | |
| Data Hold from WE | TDH | 70 | | | nsec | |

### 14.3 INPUT DATA TIMING

| Parameter | Symbol | Min | Typ | Max | Unit | Conditions |
|-----------|--------|-----|-----|-----|------|------------|
| Raw Read Pulse Width | Tpw | 100 | 200 | | nsec | See Note 1 |
| Raw Read Cycle Time | tbc | | 1500 | | nsec | 1800 ns @ 70°C |
| RCLK Cycle Time | Tc | | 1500 | | nsec | 1800 ns @ 70°C |
| RCLK hold to Raw Read | Tx1 | 40 | | | nsec | See Note 1 |
| Raw Read hold to RCLK | Tx2 | 40 | | | nsec | |

**Note 1:** Pulse width on RAW READ (Pin 27) is normally 100-300 ns. However, pulse may be any width if pulse is entirely within window. If pulse occurs in both windows, then pulse width must be less than 300 ns for MFM at CLK = 2 MHz and 600 ns for FM at 2 MHz. Times double for 1 MHz.

### 14.4 WRITE DATA TIMING (ALL TIMES DOUBLE WHEN CLK = 1 MHz)

| Parameter | Symbol | Conditions | Min | Typ | Max | Unit |
|-----------|--------|------------|-----|-----|-----|------|
| Write Data Pulse Width | Twp | FM | 450 | 500 | 550 | nsec |
| | | MFM | 150 | 200 | 250 | nsec |
| Write Gate to Write Data | Twg | FM | | 2 | | μsec |
| | | MFM | | 1 | | μsec |
| Write data cycle Time | Tbc | | | 2,3, or 4 | | μsec (±CLK Error) |
| Early (Late) to Write Data | Ts | MFM | 125 | | | nsec |
| Early (Late) from Write Data | Th | MFM | 125 | | | nsec |
| Write Gate off from WD | Twf | FM | | 2 | | μsec |
| | | MFM | | 1 | | μsec |
| WD Valid to Clk | Twd1 | CLK = 1 MHz | 100 | | | nsec |
| | | CLK = 2 MHz | 50 | | | nsec |
| WD Valid after CLK | Twd2 | CLK = 1 MHz | 100 | | | nsec |
| | | CLK = 2 MHz | 30 | | | nsec |

### 14.5 MISCELLANEOUS TIMING

| Parameter | Symbol | Min | Typ | Max | Unit | Conditions |
|-----------|--------|-----|-----|-----|------|------------|
| Clock Duty (low) | TCD1 | 230 | 250 | 20000 | nsec | |
| Clock Duty (high) | TCD2 | 200 | 250 | 20000 | nsec | |
| Step Pulse Output | TSTP | 2 or 4 | | | μsec | See Note 5 |
| Dir Setup to Step | TDIR | 12 | | | μsec | ± CLK ERROR |
| Master Reset Pulse Width | TMR | 50* | | | μsec | |
| Index Pulse Width | TIP | 10* | | | μsec | See Note 5 |
| Write Fault Pulse Width | TWF | 10* | | | μsec | See Note 5 |
| CLK Cycle Time | tCYC | | 0.5* | | μS | |

*: These Values are doubled when CLK = 1MHz.
**: During Master Reset, CLK of more than 10 cycles are required.

**Note 5:** Times double when clock = 1 MHz.

### 14.8 DRQ Service Time (Worst Case)

| Mode | DDEN | CLK | Service Time |
|------|------|-----|--------------|
| MFM | 0 | 2 MHz | 13.5 μS |
| FM | 1 | 2 MHz | 27.5 μS |
| MFM | 0 | 1 MHz | 27 μS (mini-drives) |
| FM | 1 | 1 MHz | 55 μS (mini-drives) |

---

## 15. TYPICAL SYSTEM CONSTRUCTION

### 15.1 System Block Diagram

```
                                          RAW READ
                DATA (8) ────────────────►   RCLK
                   A0 ──────────────────►   RG/SSO ──────►
                   A1 ──────────────────►   LATE
    C              CS ──────────────────►   EARLY
    O              RE ──────────────────►   WD         F
    M              WE ──────────────────►             L
    P            +5V ──┬────────────────►   WG        O
    U              MR ─┤   ≤10K         ►   WPRT ──►  P
    T                  │                    WF/VFOE ──►P
    E                 ≤10K                  IP ◄─────  Y
    R                  │        179X        TR00 ◄───
                      DRQ ◄─────────────    READY ◄─  D
                    INTRQ ◄─────────────    TG43 ──►  I
                                           STEP ──►  S
                      CLK ──────────────►   DIRC ──►  K
                                                    
                                      +5V
                                       │    HLD     ┌───────────┐
                                       │     │     │  ONE SHOT │
               ┌──────────────────────┴─────┴─────┤ (IF USED) │
               │                                   └─────┬─────┘
             DDEN   VSS    VDD    VCC                     │
                                                        HLT
                    ═      +12    +5
```

### 15.2 External VFO Circuit

During read operations (WG = 0), the VFOE (Pin 33) is provided for phase lock loop synchronization. A PPL Data Separator is recommended for 8" MFM operation.

**Note 2:** A PPL Data Separator is recommended for 8" MFM.

---

## 16. PACKAGE INFORMATION

### 16.1 WD179X Package Options

- **FD179XA-02 CERAMIC PACKAGE:** 40-pin ceramic DIP
  - Width: 2.025 MAX
  - Height: 0.200 MAX
  - Pin spacing: 0.100 typ

- **FD179XB-02 PLASTIC PACKAGE:** 40-pin plastic DIP
  - Width: 2.080 MAX
  - Height: 0.200 MAX
  - Pin spacing: 0.100 typ

### 16.2 Fujitsu MB8876A/MB8877A Package Options

- **40-LEAD CERAMIC (METAL SEAL) DUAL IN-LINE PACKAGE**
  - Case No.: DIP-40C-A01
  - Width: 1.980-2.020 (50.29-51.31 mm)
  - Height: 0.585-0.605 (14.86-15.38 mm)

- **40-LEAD PLASTIC DUAL IN-LINE PACKAGE**
  - Case No.: DIP-40P-M01
  - Width: 2.045-2.071 (51.95-52.60 mm)
  - Height: 0.533-0.553 (13.55-14.05 mm)

---

## 17. PROGRAMMING NOTES

### 17.1 Master Reset Behavior

A logic low on the MR input resets the device and loads HEX 03 into the command register. The Not Ready (Status Bit 7) is reset during MR ACTIVE. When MR is brought to a logic high a RESTORE Command is executed, regardless of the state of the Ready signal from the drive. Also, HEX 01 is loaded into sector register.

### 17.2 Register Access During Busy

The command words should only be loaded in the Command Register when the Busy status bit is off (Status bit 0). The one exception is the Force Interrupt command. The Track Register should not be loaded when the device is busy. The Sector Register should not be loaded when the device is busy (BUSY = 1). Executing the Read Address Command causes the SCR to be loaded with the track number from the ID field.

### 17.3 Ready Signal Sampling

Whenever a Read or Write command (Type II or III) is received the FD179X samples the Ready input. If this input is logic low the command is not executed and an interrupt is generated. All Type I commands are performed regardless of the state of the Ready input. Also, whenever a Type II or III command is received, the TG43 signal output is updated.

### 17.4 Interrupt Generation

At the completion of every command an INTRQ is generated. INTRQ is reset by either reading the status register or by loading the command register with a new command. In addition, INTRQ is generated if a Force Interrupt command condition is met.

---

## APPENDIX A: QUICK REFERENCE TABLES

### A.1 Command Quick Reference

| Command | Type | Hex Code (Base) | Description |
|---------|------|-----------------|-------------|
| Restore | I | 0x | Seek to Track 0 |
| Seek | I | 1x | Seek to track in Data Register |
| Step | I | 2x/3x | Step in current direction |
| Step-In | I | 4x/5x | Step towards track 76 |
| Step-Out | I | 6x/7x | Step towards track 0 |
| Read Sector | II | 8x/9x | Read sector(s) |
| Write Sector | II | Ax/Bx | Write sector(s) |
| Read Address | III | Cx | Read next ID field |
| Read Track | III | Ex | Read entire track |
| Write Track | III | Fx | Format track |
| Force Interrupt | IV | Dx | Abort/set interrupt condition |

### A.2 Status Bit Quick Reference

| Bit | Type I | Type II/III Read | Type II/III Write |
|-----|--------|------------------|-------------------|
| 7 | Not Ready | Not Ready | Not Ready |
| 6 | Write Protect | 0 | Write Protect |
| 5 | Head Loaded | Record Type | Write Fault |
| 4 | Seek Error | RNF | RNF |
| 3 | CRC Error | CRC Error | CRC Error |
| 2 | Track 0 | Lost Data | Lost Data |
| 1 | Index | DRQ | DRQ |
| 0 | Busy | Busy | Busy |

### A.3 Address Mark Summary

| Mark Type | FM (SD) Value | FM Clock | MFM (DD) Value | MFM Clock Pattern |
|-----------|---------------|----------|----------------|-------------------|
| Index Address Mark (IAM) | FC | D7 | FC | Normal (no A1 preamble required) |
| ID Address Mark (IDAM) | FE | C7 | FE | Preceded by 3× A1 (clock C7) |
| Data Address Mark (DAM) | FB | C7 | FB | Preceded by 3× A1 (clock C7) |
| Deleted Data Address Mark | F8 | C7 | F8 | Preceded by 3× A1 (clock C7) |

---

## APPENDIX B: FM AND MFM ENCODING

### B.1 FM (Frequency Modulation) Encoding

In FM encoding, each data bit cell contains:
- A clock pulse at the beginning of every bit cell
- A data pulse (presence = 1, absence = 0) in the middle of the bit cell

**Bit period:** 4 μs at 2 MHz clock (8 μs at 1 MHz)

### B.2 MFM (Modified Frequency Modulation) Encoding

In MFM encoding:
- Data 1 bit = flux transition in center of bit cell
- Data 0 bit = no transition in center, BUT a clock transition occurs at the start of the bit cell only if the previous bit was also 0

**Bit period:** 2 μs at 2 MHz clock (4 μs at 1 MHz)

### B.3 Address Mark Missing Clock Patterns

Address marks are identified by violating the normal clock pattern:

**FM Address Marks:**
- Clock pattern C7 = missing clock between bits 5-6 and 6-7

**MFM Address Marks:**
- A1 sync byte with missing clock between bits 4-5
- C2 sync byte with missing clock between bits 3-4

---

## APPENDIX C: COMPATIBILITY NOTES

### C.1 WD1793 vs Fujitsu MB8877A

| Feature | WD1793 | MB8877A |
|---------|--------|---------|
| Power Supply | +5V and +12V | +5V only |
| Data Bus | True (positive logic) | Positive logic |
| Pin 40 | VDD (+12V) | NC |
| Core Functionality | Identical | Identical |
| Command Set | Identical | Identical |
| Timing | Identical | Identical |

### C.2 WD1791 vs Fujitsu MB8876A

| Feature | WD1791 | MB8876A |
|---------|--------|---------|
| Power Supply | +5V and +12V | +5V only |
| Data Bus | Inverted (negative logic) | Negative logic |
| Pin 40 | VDD (+12V) | NC |
| Core Functionality | Identical | Identical |

### C.3 Other Compatible Devices

The following devices are generally register and command compatible:
- Soviet clones: КР1818ВГ93 (K1818VG93)
- Various second-source manufacturers

---

## Document Revision History

| Version | Date | Description |
|---------|------|-------------|
| 1.0 | 2025 | Initial comprehensive compilation from WD179X-02 (May 1980) and Fujitsu MB8876A/MB8877A (October 1986, Edition 2.0) datasheets |

---

*This document is compiled from publicly available manufacturer datasheets for educational and reference purposes. Original copyrights remain with Western Digital Corporation and Fujitsu Limited respectively.*
