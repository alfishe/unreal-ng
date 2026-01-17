# WD179X Datasheet: Internal Organization

                          ┌─────────────────────────────────────────────────────────────────┐
                          │                         WD179X                                  │
                          │                                                                 │
  ┌──────────────┐        │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐          │
  │   COMPUTER   │        │  │   DATA      │◄──►│   DATA      │◄──►│  RAW READ   │◄─────────│◄── RAW EAD
  │   INTERFACE  │        │  │  REGISTER   │    │  SHIFT REG  │    │  DECODE     │          │
  └──────┬───────┘        │  └──────┬──────┘    └──────┬──────┘    └─────────────┘          │
         │                │         │                  │                                    │
  DAL0-7 ◄────────────────│─────────┼──────────────────┤                                    │
         │                │         │                  │                                    │
         │                │         ▼                  ▼                                    │
  ┌──────▼───────┐        │  ┌─────────────┐    ┌─────────────┐                             │
  │ A0, A1, CS   │        │  │    CRC      │    │    AM       │                             │
  │ RE, WE       │────────│─►│   LOGIC     │    │  DETECTOR   │                             │
  └──────────────┘        │  │ G(x)=x¹⁶+   │    └─────────────┘                             │
                          │  │ x¹²+x⁵+1    │                                                │
  ┌──────────────┐        │  └─────────────┘                                                │
  │    TRACK     │◄───────│◄─────────┬────────────────────────────────────────────────────┐ │
  │   REGISTER   │        │          │                                                    │ │
  └──────────────┘        │          ▼                                                    │ │
                          │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐        │ │
  ┌──────────────┐        │  │    ALU      │◄──►│   TIMING    │◄──►│   DISK      │◄───────│─│◄── STEP, DIRC, etc.
  │   SECTOR     │◄───────│─►│  (Compare,  │    │  & CONTROL  │    │  INTERFACE  │────────│─│──► WG, WD, etc.
  │   REGISTER   │        │  │  Inc, Dec)  │    │             │    │  CONTROL    │        │ │
  └──────────────┘        │  └─────────────┘    └──────┬──────┘    └─────────────┘        │ │
                          │                           │                                   │ │
  ┌──────────────┐        │                           ▼                                   │ │
  │   COMMAND    │◄───────│─────────────────►┌─────────────┐                              │ │
  │   REGISTER   │        │                  │    PLA       │                             │ │
  └──────────────┘        │                  │   CONTROL    │                             │ │
                          │                  │  (230 × 16)  │                             │ │
  ┌──────────────┐        │                  └─────────────┘                              │ │
  │   STATUS     │◄───────│◄──────────────────────────────────────────────────────────────┘ │
  │   REGISTER   │        │                                                                 │
  └──────────────┘        │                                                                 │
                          │                                                                 │
  CLK, MR ────────────────│─────────────────────────────────────────────────────────────────┤
  DRQ, INTRQ ◄────────────│◄────────────────────────────────────────────────────────────────┤
                          │                                                                 │
                          └─────────────────────────────────────────────────────────────────┘
```

---

## INTERNAL REGISTERS

### Data Register (DR)

*   **Size:** 8 bits
*   **Access:** Read/Write via DAL when A1=1, A0=1
*   **Function:** Serves as a holding register during Read and Write operations. On Read, data from the Data Shift Register is transferred to DR; DRQ is set, and the host reads the byte. On Write, the host writes the byte to DR; once transferred to the DSR, DRQ is set again to request the next byte.
*   **Double Buffering:** The combination of DR and the internal Data Shift Register provides double buffering, allowing the host adequate time to service data requests.

### Data Shift Register (DSR) — Internal

*   **Size:** 8 bits (serial-parallel)
*   **Access:** Not directly accessible
*   **Function:** Assembles serial data from the disk into an 8-bit parallel word (Read), or serializes parallel data from the Data Register for writing (Write). Operates in conjunction with the CRC logic.

### Track Register (TR)

*   **Size:** 8 bits
*   **Access:** Read/Write via DAL when A1=0, A0=1
*   **Function:** Holds the address of the current track position. Updated automatically during Seek and Step (if U flag set) commands. Compared against the ID field track address during Verify operations.
*   **Reset Value:** $FF (after Master Reset)
*   **Note:** Should not be loaded while device is Busy.

### Sector Register (SR)

*   **Size:** 8 bits
*   **Access:** Read/Write via DAL when A1=1, A0=0
*   **Function:** Holds the address of the desired sector for Read/Write Sector operations. Compared against the ID field sector address during sector search. Auto-incremented in Multiple Record mode.
*   **Reset Value:** $01 (after Master Reset)
*   **Note:** Should not be loaded while device is Busy.

### Command Register (CR)

*   **Size:** 8 bits
*   **Access:** Write-only via DAL when A1=0, A0=0
*   **Function:** Receives commands from the host processor. Writing a command initiates execution (except Force Interrupt, which can interrupt a Busy command).
*   **Reset Value:** $03 (Restore with no verify, r1r0=11)
*   **Note:** Should not be loaded while device is Busy unless new command is Force Interrupt.

### Status Register (STR)

*   **Size:** 8 bits
*   **Access:** Read-only via DAL when A1=0, A0=0
*   **Function:** Reflects the current device status and results of the last command. Bit meanings vary by command type (see Status Bits section).
*   **Note:** Reading the Status Register resets INTRQ.

---

## INTERNAL LOGIC BLOCKS

### CRC Logic

*   **Polynomial:** G(x) = x¹⁶ + x¹² + x⁵ + 1 (also known as CRC-CCITT)
*   **Function:** Generates a 16-bit Cyclic Redundancy Check for data written to disk. Verifies CRC on data read from disk.
*   **Operation:** CRC includes all data from Address Mark through the last data byte. The CRC register is **preset to all ones ($FFFF)** before data is shifted through.
*   **Detection:** After reading CRC bytes from disk, a valid CRC leaves the register at $0000.

### Arithmetic/Logic Unit (ALU)

*   **Type:** Serial comparator, incrementer, and decrementer
*   **Function:** 
    - **Compare:** Matches Track Register with ID field track address during Verify
    - **Compare:** Matches Sector Register with ID field sector address during sector search
    - **Increment:** Updates Track Register during Step-In or Seek
    - **Decrement:** Updates Track Register during Step-Out

### Address Mark (AM) Detector

*   **Function:** Detects special address mark patterns recorded on disk:
    - **Index Address Mark (IAM):** $FC (precedes track data)
    - **ID Address Mark (IDAM):** $FE (precedes sector header)
    - **Data Address Mark (DAM):** $FB (normal data) or $F8 (deleted data)
*   **FM Mode:** Detects missing clock bits in address mark bytes
*   **MFM Mode:** Detects special $A1 patterns with missing clock ($4489 pattern)

### Timing and Control

*   **PLA Control:** 230 × 16 Programmable Logic Array implements the state machine
*   **Timing Derivation:** All internal timing derived from external CLK input
*   **Density Mode:** Controlled by DDEN pin:
    - **DDEN = 0:** Double density (MFM) — 500 kbit/s data rate at 2 MHz
    - **DDEN = 1:** Single density (FM) — 250 kbit/s data rate at 2 MHz

### Computer Interface Control

*   **Register Selection:** Decodes A0, A1, CS, RE, WE for register access
*   **DRQ Generation:** Sets Data Request when DR needs service
*   **INTRQ Generation:** Sets Interrupt Request at command completion

### Disk Interface Control

*   **Step Control:** Generates STEP pulses, manages DIRC
*   **Read Control:** Manages RG (Read Gate), monitors RCLK, RAW READ
*   **Write Control:** Controls WG (Write Gate), generates WD, EARLY, LATE
*   **Head Control:** Manages HLD output, samples HLT input
*   **Status Sensing:** Monitors READY, TR00, WPRT, IP, WF

---

## PROCESSOR INTERFACE TIMING

### DMA Transfers

*   **DRQ (Data Request):** Set high when a byte is available (Read) or when the Data Register is empty (Write)
*   **DRQ Clear:** Automatically cleared when the Data Register is accessed via RE or WE
*   **Timing:** Host must service DRQ within one byte time to prevent Lost Data:
    - FM (250 kbit/s): 32 µs per byte
    - MFM (500 kbit/s): 16 µs per byte

### Programmed I/O

*   **Polling:** Status Register Bit 1 (DRQ copy) can be polled to determine data availability
*   **Busy Polling:** Status Register Bit 0 can be polled to determine command completion

### Interrupts

*   **INTRQ Set:** Generated at command completion, or per Force Interrupt conditions
*   **INTRQ Clear:** Reading the Status Register or writing a new Command Register value clears INTRQ
