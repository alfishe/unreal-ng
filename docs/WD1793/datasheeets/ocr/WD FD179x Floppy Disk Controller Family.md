***

# WESTERN DIGITAL CORPORATION
## FD 179X-02 Floppy Disk Formatter/Controller Family

*MAY 1980*

---

### FEATURES

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
    *   Double Buffering of Data 8 Bit Bi-Directional Bus for Data, Control and Status
    *   DMA or Programmed Data Transfers
    *   All Inputs and Outputs are TTL Compatible
    *   On-Chip Track and Sector Registers/Comprehensive Status Information
*   **PROGRAMMABLE CONTROLS**
    *   Selectable Track to Track Stepping Time
    *   Side Select Compare
*   **WRITE PRECOMPENSATION**
*   **WINDOW EXTENSION**
*   **INCORPORATES ENCODING/DECODING AND ADDRESS MARK CIRCUITRY**
*   **FD1792/4 IS SINGLE DENSITY ONLY**
*   **FD1795/7 HAS A SIDE SELECT OUTPUT**

### 179X-02 FAMILY CHARACTERISTICS

| FEATURES               | 1791 | 1793 | 1795 | 1797 |
| ---------------------- | :--: | :--: | :--: | :--: |
| Single Density (FM)    |  X   |  X   |  X   |  X   |
| Double Density (MFM)   |  X   |  X   |  X   |  X   |
| True Data Bus          |      |  X   |      |  X   |
| Inverted Data Bus      |  X   |      |  X   |      |
| Write Precomp          |  X   |  X   |  X   |  X   |
| Side Selection Output  |      |      |  X   |  X   |

### APPLICATIONS
*   FLOPPY DISK DRIVE INTERFACE
*   SINGLE OR MULTIPLE DRIVE CONTROLLER/FORMATTER
*   NEW MINI-FLOPPY CONTROLLER

---

### PIN CONNECTIONS

| Pin | Signal     | Pin | Signal      |
| --- | ---------- | --- | ----------- |
| 1   | NC         | 40  | VDD (+12V)  |
| 2   | /WE        | 39  | /INTRQ      |
| 3   | /CS        | 38  | /DRQ        |
| 4   | /RE        | 37  | /DDEN       |
| 5   | A0         | 36  | /WPRT       |
| 6   | A1         | 35  | /IP         |
| 7   | **DAL0     | 34  | /TR00       |
| 8   | DAL1       | 33  | WF//VFOE    |
| 9   | DAL2       | 32  | /READY      |
| 10  | DAL3       | 31  | WD          |
| 11  | DAL4       | 30  | WG          |
| 12  | DAL5       | 29  | TG43        |
| 13  | DAL6       | 28  | /HLD        |
| 14  | DAL7       | 27  | /RAW READ   |
| 15  | STEP       | 26  | RCLK        |
| 16  | DIRC       | 25  | /EARLY      |
| 17  | EARLY      | 24  | CLK         |
| 18  | LATE       | 23  | /HLT        |
| 19  | /MR        | 22  | TEST        |
| 20  | GND (VSS)  | 21  | VCC (+5V)   |

*\* 1791/3 Pin 25 is RG; 1795/7 Pin 25 is SSO*
*\*\* 1793/7 TRUE BUS*

---

### FD179X SYSTEM BLOCK DIAGRAM

```mermaid
graph TD
    subgraph COMPUTER INTERFACE
        A0
        A1
        CS["/CS"]
        RE["/RE"]
        WE["/WE"]
        MR["/MR"]
        DRQ["/DRQ"]
        INTRQ["/INTRQ"]
        CLK
        DATA_BUS[DATA (8)]
    end

    subgraph "179X FLOPPY DISK CONTROLLER FORMATTER"
        FDC
    end

    subgraph FLOPPY DISK INTERFACE
        RAW_READ["/RAW READ"]
        RCLK_IN[RCLK]
        RG_SSO[RG//SSO]
        LATE_IN[/LATE]
        EARLY_IN[/EARLY]
        WD_OUT[WD]
        WG_OUT[WG]
        WPRT_IN["/WPRT"]
        WF_VFOE_OUT[WF//VFOE]
        IP_IN["/IP"]
        TR00_IN["/TR00"]
        READY_IN["/READY"]
        TG43_OUT[TG43]
        STEP_OUT[STEP]
        DIRC_OUT[DIRC]
    end

    subgraph "External Logic"
        PULLUP1[10K Resistor to +5V]
        PULLUP2[10K Resistor to +5V]
        PULLUP3[10K Resistor to +5V]
        HLD_OUT["/HLD"] --> ONE_SHOT["ONE SHOT<br/>(IF USED)"] --> HLT_IN["/HLT"]
        VSS[GND]
        VDD["+12V"]
        VCC["+5V"]
    end
    
    A0 --> FDC
    A1 --> FDC
    CS --> FDC
    RE --> FDC
    WE --> FDC
    MR --> FDC
    FDC --> DRQ
    DRQ --> PULLUP1
    FDC --> INTRQ
    INTRQ --> PULLUP2
    CLK --> FDC
    DATA_BUS <--> FDC

    FDC --> HLD_OUT
    HLT_IN --> FDC
    FDC --> DDEN_PIN[DDEN]
    DDEN_PIN --> VSS
    
    FDC --> RAW_READ
    RCLK_IN --> FDC
    RG_SSO --> FDC
    LATE_IN --> FDC
    EARLY_IN --> FDC
    FDC --> WD_OUT
    FDC --> WG_OUT
    WPRT_IN --> FDC
    WF_VFOE_OUT <--> FDC
    IP_IN --> FDC
    TR00_IN --> FDC
    READY_IN --> FDC
    FDC --> TG43_OUT
    FDC --> STEP_OUT
    FDC --> DIRC_OUT

    VSS --> FDC
    VDD --> FDC
    VCC --> FDC
```
---

### GENERAL DESCRIPTION
The FD179X are MOS LSI devices which perform the functions of a Floppy Disk Formatter/Controller in a single chip implementation. The FD179X, which can be considered the end result of both the FD1771 and FD1781 designs, is IBM 3740 compatible in single density mode (FM) and System 34 compatible in Double Density Mode (MFM). The FD179X contains all the features of its predecessor the FD1771, plus the added features necessary to read/write and format a double density diskette. These include address mark detection, FM and MFM encode and decode logic, window extension, and write precompensation. In order to maintain compatibility, the FD1771, FD1781, and FD179X designs were made as close as possible with the computer interface, instruction set, and I/O registers being identical. Also, head load control is identical. In each case, the actual pin assignments vary by only a few pins from any one to another.

The processor interface consists of an 8-bit bi-directional bus for data, status, and control word transfers. The FD179X is set up to operate on a multiplexed bus with other bus-oriented devices.

The FD179X is fabricated in N-channel Silicon Gate MOS technology and is TTL compatible on all inputs and outputs. The 1793 is identical to the 1791 except the DAL lines are TRUE for systems that utilize true data busses.
The 1795/7 has a side select output for controlling double sided drives, and the 1792 and 1794 are "Single Density Only" versions of the 1791 and 1793. On these devices, DDEN must be left open.

---

### PIN OUTS

#### POWER AND SYSTEM
| PIN | NAME | SYMBOL | FUNCTION |
| --- | --- | --- | --- |
| 1 | NO CONNECTION | NC | Pin 1 is internally connected to a back bias generator and must be left open by the user. |
| 19 | MASTER RESET | /MR | A logic low on this input resets the device and loads HEX 03 into the command register. The Not Ready (Status Bit 7) is reset during MR ACTIVE. When MR is brought to a logic high a RESTORE Command is executed, regardless of the state of the Ready signal from the drive. Also, HEX 01 is loaded into sector register. |
| 20 | POWER SUPPLIES | Vss | Ground |
| 21 | | Vcc | +5V ±5% |
| 40 | | VDD | +12V ±5% |
| 24 | CLOCK | CLK | This input requires a free-running square wave clock for internal timing reference. 2 MHz for 8" drives, 1 MHz for mini-drives. |

#### COMPUTER INTERFACE
| PIN | NAME | SYMBOL | FUNCTION |
| --- | --- | --- | --- |
| 2 | WRITE ENABLE | /WE | A logic low on this input gates data on the DAL into the selected register when /CS is low. |
| 3 | CHIP SELECT | /CS | A logic low on this input selects the chip and enables computer communication with the device. |
| 4 | READ ENABLE | /RE | A logic low on this input controls the placement of data from a selected register on the DAL when /CS is low. |
| 5,6 | REGISTER SELECT LINES | A0, A1 | These inputs select the register to receive/transfer data on the DAL lines under /RE and /WE control. |
| 7-14| DATA ACCESS LINES | DAL0-DAL7| Eight bit Inverted (1791/5) or True (1793/7) Bidirectional bus used for transfer of data, control, and status. This bus is receiver enabled by /WE or transmitter enabled by /RE. |
| 38 | DATA REQUEST | /DRQ | This open drain output indicates that the DR contains assembled data in Read operations, or the DR is empty in Write operations. This signal is reset when serviced by the computer through reading or loading the DR. Use 10K pull-up resistor to +5. |
| 39 | INTERRUPT REQUEST | /INTRQ | This open drain output is set at the completion of any command and is reset when the STATUS register is read or the command register is written to. Use 10K pull-up resistor to +5. |

**Register Select Logic:**
| A1 | A0 | /RE (Read) | /WE (Write) |
| -- | -- | ---------- | ----------- |
| 0  | 0  | Status Reg | Command Reg |
| 0  | 1  | Track Reg  | Track Reg   |
| 1  | 0  | Sector Reg | Sector Reg  |
| 1  | 1  | Data Reg   | Data Reg    |

#### FLOPPY DISK INTERFACE
| PIN | NAME | SYMBOL | FUNCTION |
| --- | --- | --- | --- |
| 15 | STEP | STEP | The step output contains a pulse for each step. |
| 16 | DIRECTION | DIRC | Direction Output is active high when stepping in, active low when stepping out. |
| 17 | EARLY | EARLY | Indicates that the WRITE DATA pulse occurring while Early is active (high) should be shifted early for write precompensation. |
| 18 | LATE | LATE | Indicates that the write data pulse occurring while Late is active (high) should be shifted late for write precompensation. |
| 22 | TEST | TEST | This input is used for testing purposes only and should be tied to +5V or left open by the user unless interfacing to voice coil actuated motors. |
| 23 | HEAD LOAD TIMING | /HLT | When a logic high is found on the HLT input the head is assumed to be engaged. |
| 25 | READ GATE (1791/3) | RG | A high level on this output indicates to the data separator circuitry that a field of zeros (or ones) has been encountered, and is used for synchronization. |
| 25 | SIDE SELECT OUTPUT (1795, 1797) | SSO | The logic level of the Side Select Output is directly controlled by the 'S' flag in Type II or III commands. When S=1, SSO=1. When S=0, SSO=0. Only updated at beginning of a Type II or III command. Forced to logic 0 upon a MASTER RESET. |
| 26 | READ CLOCK | RCLK | A nominal square-wave clock signal derived from the data stream must be provided. Phasing relative to RAW READ is important but polarity is not. |
| 27 | RAW READ | /RAW READ | The data input signal directly from the drive. This input shall be a negative pulse for each recorded flux transition. |
| 28 | HEAD LOAD | /HLD | The HLD output controls the loading of the Read-Write head against the media. |
| 29 | TRACK GREATER THAN 43 | TG43 | This output informs the drive that the R/W head is positioned between tracks 44-76. Valid only during Read and Write Commands. |
| 30 | WRITE GATE | WG | This output is made valid before writing is to be performed on the diskette. |
| 31 | WRITE DATA | WD | A 250 ns (MFM) or 500 ns (FM) pulse per flux transition. WD contains the unique Address marks as well as data and clock. |
| 32 | READY | /READY | This input indicates disk readiness and is sampled for a logic high before Read or Write commands are performed. If Ready is low, the operation is not performed and an interrupt is generated. Appears in inverted format as Status Register bit 7. |
| 33 | WRITE FAULT VFO ENABLE | WF//VFOE | Bi-directional. When WG=1, it is a WF input. If WF=0, any write command is terminated. When WG=0, it is a VFOE output to enable the external PLL. |
| 34 | TRACK 00 | /TR00 | This input informs the FD179X that the R/W head is positioned over Track 00. |
| 35 | INDEX PULSE | /IP | This input informs the FD179X when the index hole is encountered on the diskette. |
| 36 | WRITE PROTECT | /WPRT | This input is sampled whenever a Write Command is received. A logic low terminates the command and sets the Write Protect Status bit. |
| 37 | DOUBLE DENSITY | /DDEN | This pin selects density. When /DDEN=0, double density (MFM) is selected. When /DDEN=1, single density (FM) is selected. Must be left open on 1792/4. |

---

### ORGANIZATION

The Floppy Disk Formatter is composed of the parallel processor interface and the Floppy Disk interface.

*   **Data Shift Register:** An 8-bit register that assembles serial data from the /RAW READ input during Read operations and transfers serial data to the WD output during Write operations.
*   **Data Register:** An 8-bit holding register. In Read operations, the assembled byte is transferred in parallel from the Data Shift Register to the Data Register. In Write operations, information is transferred in parallel from the Data Register to the Data Shift Register. It also holds the desired track address for Seek commands.
*   **Track Register:** An 8-bit register holding the current R/W head position. Incremented when stepping in (towards track 76) and decremented when stepping out (towards track 00).
*   **Sector Register:** An 8-bit register holding the address of the desired sector position.
*   **Command Register (CR):** An 8-bit register holding the command being executed.
*   **Status Register (STR):** An 8-bit register holding device status information.
*   **CRC Logic:** Generates or checks the 16-bit Cyclic Redundancy Check (CRC). The polynomial is: G(x) = x¹⁶ + x¹² + x⁵ + 1.
*   **Arithmetic/Logic Unit (ALU):** A serial comparator, incrementer, and decrementer used for register modification and comparisons with the disk recorded ID field.
*   **Timing and Control:** Generates all computer and Floppy Disk Interface controls.
*   **AM Detector:** Detects ID, data, and index address marks during read and write operations.

```mermaid
graph TD
    subgraph ProcessorInterface [Computer Interface Logic]
        DAL[DAL 0-7]
        A[A0, A1]
        CS[/CS]
        WE[/WE]
        RE[/RE]
        MR[/MR]
    end

    subgraph Registers
        DR[Data Reg]
        CR[Command Reg]
        SR[Sector Reg]
        TR[Track Reg]
        STR[Status Reg]
    end

    subgraph CoreLogic
        DSR[Data Shift Reg]
        AM[AM Detector]
        CRC[CRC Logic]
        ALU[ALU]
    end

    subgraph Control
        PLA[PLA Control]
    end
    
    subgraph DiskInterface [Disk Interface Logic]
        WG_out[WG]
        TG43_out[TG43]
        WPRT_in[/WPRT]
        WF_VFOE_io[WF//VFOE]
        IP_in[/IP]
        TR00_in[/TR00]
        READY_in[/READY]
        STEP_out[STEP]
        DIRC_out[DIRC]
        EARLY_out[EARLY]
        LATE_out[LATE]
        HLD_out[/HLD]
    end

    DAL <--> BUF[Data Out Buffers] <--> Registers
    ProcessorInterface --> PLA
    
    DSR <--> DR
    DSR <--> AM
    DSR <--> CRC
    AM <--> CRC
    
    DR <--> ALU
    CR --> PLA
    SR <--> ALU
    TR <--> ALU
    
    Registers --> STR
    
    ALU <--> SR
    ALU <--> TR
    
    PLA <--> Registers
    PLA <--> CoreLogic
    PLA <--> DiskInterface
    
    DSR --> WD[Write Data to Disk]
    RAW_READ[RAW READ] --> AM
    RCLK[RCLK] --> AM
    
    STR <--> BUF
```

---

### COMMANDS

The FD179X accepts eleven commands. Command words should only be loaded when the Busy status bit is off (Status bit 0), except for the Force Interrupt command. Commands are divided into four types.

**Table 2. COMMAND SUMMARY** (Bits shown in TRUE form)
| TYPE | COMMAND | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|---|
| I | Restore | 0 | 0 | 0 | 0 | h | V | r₁ | r₀ |
| I | Seek | 0 | 0 | 0 | 1 | h | V | r₁ | r₀ |
| I | Step | 0 | 0 | 1 | u | h | V | r₁ | r₀ |
| I | Step In | 0 | 1 | 0 | u | h | V | r₁ | r₀ |
| I | Step Out | 0 | 1 | 1 | u | h | V | r₁ | r₀ |
| II | Read Sector | 1 | 0 | 0 | m | F₂ | E | F₁ | a₀ |
| II | Write Sector | 1 | 0 | 1 | m | F₂ | E | F₁ | a₀ |
| III | Read Address | 1 | 1 | 0 | 0 | 0 | E | 0 | 0 |
| III | Read Track | 1 | 1 | 1 | 0 | 0 | E | 0 | 0 |
| III | Write Track | 1 | 1 | 1 | 1 | 0 | E | 0 | 0 |
| IV | Force Interrupt | 1 | 1 | 0 | 1 | I₃ | I₂ | I₁ | I₀ |

---

### COMMAND FLAGS

**Table 3. FLAG SUMMARY - TYPE I COMMANDS**
*   **h = Head Load Flag (Bit 3)**
    *   `h = 1`, Load head at beginning
    *   `h = 0`, Unload head at beginning
*   **V = Verify flag (Bit 2)**
    *   `V = 1`, Verify on destination track
    *   `V = 0`, No verify
*   **r₁r₀ = Stepping motor rate (Bits 1-0)**
    *   Refer to Table 1 for rate summary
*   **u = Update flag (Bit 4)**
    *   `u = 1`, Update Track register
    *   `u = 0`, No update

**Table 1. STEPPING RATES**
| CLK | DDEN | R1 R0 | TEST=1 | TEST=0 |
|---|---|---|---|---|
| 2 MHz | 0 | 00 | 3 ms | 184µs |
| 2 MHz | 0 | 01 | 6 ms | 190µs |
| 2 MHz | 0 | 10 | 10 ms | 198µs |
| 2 MHz | 0 | 11 | 15 ms | 208µs |
| 2 MHz | 1 | 00 | 3 ms | 184µs |
| 2 MHz | 1 | 01 | 6 ms | 190µs |
| 2 MHz | 1 | 10 | 10 ms | 198µs |
| 2 MHz | 1 | 11 | 15 ms | 208µs |
| 1 MHz | X | 00 | 6 ms | 368µs |
| 1 MHz | X | 01 | 12 ms | 380µs |
| 1 MHz | X | 10 | 20 ms | 396µs |
| 1 MHz | X | 11 | 30 ms | 416µs |

**Table 4. FLAG SUMMARY - TYPE II & III COMMANDS**
*   **m = Multiple Record flag (Bit 4)**
    *   `m = 0`, Single Record
    *   `m = 1`, Multiple Records
*   **a₀ = Data Address Mark (Bit 0)**
    *   `a₀ = 0`, FB (Data Mark)
    *   `a₀ = 1`, F8 (Deleted Data Mark)
*   **E = 15 ms Delay (2MHz)**
    *   `E = 1`, 15 ms delay
    *   `E = 0`, no 15 ms delay
*   **(F₂) S = Side Select Flag (1791/3 only)**
    *   `S = 0`, Compare for Side 0
    *   `S = 1`, Compare for Side 1
*   **(F₁) C = Side Compare Flag (1791/3 only)**
    *   `C = 0`, disable side select compare
    *   `C = 1`, enable side select compare
*   **(F₁) S = Side Select Flag (Bit 1, 1795/7 only)**
    *   `S = 0`, Update SSO to 0
    *   `S = 1`, Update SSO to 1
*   **(F₂) b = Sector Length Flag (Bit 3, 1975/7 only)**
    *   Selects from two sets of sector lengths (see table).

**Sector Length Field based on 'b' flag:**
| | Field=00 | Field=01 | Field=10 | Field=11 |
|---|---|---|---|---|
| **b=0** | 256 | 512 | 1024 | 128 |
| **b=1** | 128 | 256 | 512 | 1024 |

**Table 5. FLAG SUMMARY - TYPE IV COMMAND**
*   **I₃I₂I₁I₀ = Interrupt Condition flags (Bits 3-0)**
    *   `I₀ = 1`, Not-Ready to Ready Transition
    *   `I₁ = 1`, Ready to Not-Ready Transition
    *   `I₂ = 1`, Index Pulse
    *   `I₃ = 1`, Immediate Interrupt
    *   `I₃-I₀ = 0`, Terminate with no Interrupt

---

### STATUS REGISTER

The format of the Status Register depends on the type of command last executed.

**(BITS)**
| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|
| S7 | S6 | S5 | S4 | S3 | S2 | S1 | S0 |

**Table 6. STATUS REGISTER SUMMARY**
| BIT | ALL TYPE I COMMANDS | READ ADDRESS | READ SECTOR | READ TRACK | WRITE SECTOR | WRITE TRACK |
|---|---|---|---|---|---|---|
| S7 | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY |
| S6 | WRITE PROTECT | 0 | 0 | 0 | WRITE PROTECT | WRITE PROTECT |
| S5 | HEAD LOADED | 0 | RECORD TYPE | 0 | WRITE FAULT | WRITE FAULT |
| S4 | SEEK ERROR | RNF | RNF | 0 | RNF | 0 |
| S3 | CRC ERROR | CRC ERROR | CRC ERROR | 0 | CRC ERROR | 0 |
| S2 | TRACK 0 | LOST DATA | LOST DATA | LOST DATA | LOST DATA | LOST DATA |
| S1 | INDEX | DRQ | DRQ | DRQ | DRQ | DRQ |
| S0 | BUSY | BUSY | BUSY | BUSY | BUSY | BUSY |

#### STATUS FOR TYPE I COMMANDS
| BIT NAME | MEANING |
|---|---|
| S7 NOT READY | Set indicates drive is not ready. Inverted copy of /READY input, OR'ed with /MR. |
| S6 PROTECTED | Set indicates Write Protect is activated. Inverted copy of /WPRT input. |
| S5 HEAD LOADED | Set indicates the head is loaded and engaged. Logical "AND" of HLD and HLT signals. |
| S4 SEEK ERROR | Set indicates the desired track was not verified. |
| S3 CRC ERROR | CRC encountered in ID field. |
| S2 TRACK 00 | Set indicates R/W head is positioned at Track 0. Inverted copy of /TR00 input. |
| S1 INDEX | Set indicates index mark detected. Inverted copy of /IP input. |
| S0 BUSY | Set when command is in progress. |

#### STATUS FOR TYPE II AND III COMMANDS
| BIT NAME | MEANING |
|---|---|
| S7 NOT READY | Set indicates drive is not ready. Commands will not execute unless the drive is ready. |
| S6 WRITE PROTECT | On Write commands, indicates a Write Protect. |
| S5 RECORD TYPE/WRITE FAULT | On Read: Indicates record-type code (1=Deleted, 0=Data). On Write: Indicates a Write Fault. |
| S4 RECORD NOT FOUND (RNF) | Set indicates the desired track, sector, or side were not found. |
| S3 CRC ERROR | If S4 is set, error is in ID field; otherwise, error is in data field. |
| S2 LOST DATA | Set indicates the computer did not respond to DRQ in one byte time. |
| S1 DATA REQUEST | A copy of the DRQ output. Set indicates DR is full (Read) or empty (Write). |
| S0 BUSY | Set when command is under execution. |

---

### FORMATTING THE DISK

Formatting is accomplished using the **Write Track** command. The user loads the data register for every byte to be written on the disk, from one index mark to the next. The FD179X interprets data patterns F5 through FE as special commands to write address marks or generate CRCs.

#### IBM 3740 FORMAT — 128 BYTES/SECTOR (Single Density)
User issues Write Track command and loads the DR with the following values:
| NUMBER OF BYTES | HEX VALUE OF BYTE WRITTEN |
|---|---|
| 40 | FF (or 00)¹ |
| 6 | 00 |
| 1 | FC (Index Mark) |
| 26 | FF (or 00) |
| *-- Repeat 26 times --* | |
| 6 | 00 |
| 1 | FE (ID Address Mark) |
| 1 | Track Number |
| 1 | Side Number (00 or 01) |
| 1 | Sector Number (1 thru 1A) |
| 1 | 00 (128 bytes) |
| 1 | F7 (2 CRCs written) |
| 11 | FF (or 00) |
| 6 | 00 |
| 1 | FB (Data Address Mark) |
| 128 | Data (IBM uses E5) |
| 1 | F7 (2 CRCs written) |
| 27 | FF (or 00) |
| *----------------------* | |
| 247** | FF (or 00) |

¹ Optional '00' on 1795/7 only.
** Continue writing until FD179X interrupts out.

#### IBM SYSTEM 34 FORMAT — 256 BYTES/SECTOR (Double Density)
| NUMBER OF BYTES | HEX VALUE OF BYTE WRITTEN |
|---|---|
| 80 | 4E |
| 12 | 00 |
| 3 | F5 (A1 Sync Mark) |
| 1 | FC (Index Mark) |
| 50* | 4E |
| *-- Repeat 26 times --* | |
| 12 | 00 |
| 3 | F5 (A1 Sync Mark) |
| 1 | FE (ID Address Mark) |
| 1 | Track Number (0 thru 4C) |
| 1 | Side Number (0 or 1) |
| 1 | Sector Number (1 thru 1A) |
| 1 | 01 (256 bytes) |
| 1 | F7 (2 CRCs written) |
| 22 | 4E |
| 12 | 00 |
| 3 | F5 (A1 Sync Mark) |
| 1 | FB (Data Address Mark) |
| 256 | DATA |
| 1 | F7 (2 CRCs written) |
| 54 | 4E |
| *----------------------* | |
| 598** | 4E |

\* Write bracketed field 26 times
\** Continue writing until FD179X interrupts out.

---

### ELECTRICAL AND TIMING CHARACTERISTICS

*(Note: This section summarizes key timing parameters. Refer to the original datasheet for full diagrams and notes.)*

#### MAXIMUM RATINGS
*   **VDD With Respect to Vss (Ground):** 15 to -0.3V
*   **Max. Voltage to Any Input:** 15 to -0.3V
*   **Operating Temperature:** 0°C to 70°C
*   **Storage Temperature:** -55°C to +125°C

#### OPERATING CHARACTERISTICS (DC)
*TA = 0°C to 70°C, VDD = +12V ±.6V, Vss = 0V, Vcc = +5V ±.25V*
| SYMBOL | CHARACTERISTIC | MIN | MAX | UNITS | CONDITIONS |
|---|---|---|---|---|---|
| IIL | Input Leakage | | 10 | µA | VIN = VDD |
| IOL | Output Leakage | | 10 | µA | VOUT = VDD |
| VIH | Input High Voltage | 2.6 | | V | |
| VIL | Input Low Voltage | | 0.8 | V | |
| VOH | Output High Voltage | 2.8 | | V | Io = -100 µA |
| VOL | Output Low Voltage | | 0.45 | V | Io = 1.6 mA |
| PD | Power Dissipation | | 0.5 | W | |

#### READ ENABLE TIMING
| SYMBOL | CHARACTERISTIC | MIN | TYP | MAX | UNITS |
|---|---|---|---|---|---|
| TSET | Setup ADDR & CS to RE | 50 | | | nsec |
| THLD | Hold ADDR & CS from RE | 10 | | | nsec |
| TRE | RE Pulse Width | 400 | | | nsec |
| TDRR | DRQ Reset from RE | | 400 | 500 | nsec |
| TIRR | INTRQ Reset from RE | | 500 | 3000 | nsec |
| TDACC | Data Access from RE | | | 350 | nsec |
| TDOH | Data Hold From RE | 50 | | 150 | nsec |

#### WRITE ENABLE TIMING
| SYMBOL | CHARACTERISTIC | MIN | TYP | MAX | UNITS |
|---|---|---|---|---|---|
| TSET | Setup ADDR & CS to WE | 50 | | | nsec |
| THLD | Hold ADDR & CS from WE | 10 | | | nsec |
| TWE | WE Pulse Width | 350 | | | nsec |
| TDRR | DRQ Reset from WE | | 400 | 500 | nsec |
| TIRR | INTRQ Reset from WE | | 500 | 3000 | nsec |
| TDS | Data Setup to WE | 250 | | | nsec |
| TDH | Data Hold from WE | 70 | | | nsec |

#### WRITE DATA TIMING (All times double when CLK = 1 MHz)
| SYMBOL | CHARACTERISTICS | MIN | TYP | MAX | UNITS | CONDITIONS |
|---|---|---|---|---|---|---|
| Twp | Write Data Pulse Width | 450 | 500 | 550 | nsec | FM |
| | | 150 | 200 | 250 | nsec | MFM |
| Twg | Write Gate to Write Data | | 2 | | µsec | FM |
| | | | 1 | | µsec | MFM |
| Tbc | Write data cycle Time | | 2,3, or 4| | µsec | ±CLK Error |
| Ts | Early (Late) to Write Data | 125 | | | nsec | MFM |
| Th | Early (Late) From Write Data | 125 | | | nsec | MFM |
| Twf | Write Gate off from WD | | 2 | | µsec | FM |
| | | | 1 | | µsec | MFM |
| Twd1 | WD Valid to Clk | 100 | | | nsec | CLK=1 MHZ |
| | | 50 | | | nsec | CLK=2 MHZ |
| Twd2 | WD Valid after CLK | 100 | | | nsec | CLK=1 MHZ |
| | | 30 | | | nsec | CLK=2 MHZ |

---
*This document is a recreated summary of the original Western Digital datasheet. While efforts have been made to ensure accuracy, the original PDF should be considered the authoritative source.*