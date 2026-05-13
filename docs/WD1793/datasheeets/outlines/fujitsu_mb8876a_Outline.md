# Fujitsu MB8876A Floppy Disk Controller Datasheet Outline

## 1. FEATURES
*   Silicon-gate NMOS process
*   Compatible with Western Digital WD1791/1793
*   Single +5V Power Supply (MB8876A) vs +5V/+12V (WD179X)
*   Single Density (FM) and Double Density (MFM)
*   Two versions:
    *   MB8876A (Inverted Data Bus)
    *   MB8877A (True Data Bus)

## 2. GENERAL DESCRIPTION
*   Functionality overview
*   Differences from WD179X (Power supply, Data Recovery)

## 3. BLOCK DIAGRAM
*   Internal Bus structure
*   Registers (Command, Status, Track, Sector, Data)
*   Control Logic
*   CRC Generator/Checker

## 4. PIN ASSIGNMENT
*   40-pin DIL Package Pinout
*   Pin Differences between MB8876A and MB8877A

## 5. PIN DESCRIPTION
*   Power Supply (Vcc, Vss)
*   CPU Interface
    *   /CS, /RW, /RE, /WE, A0-A1, D0-D7
    *   DRQ, /INTRQ, /MR
*   FDD Interface
    *   /STEP, /DIRC, /HLD, HLT, /WG, /WD, /RAW READ
    *   /TR00, /IP, /WPRT, /READY
    *   /VFOE (VFO Enable)

## 6. FUNCTIONAL DESCRIPTION
*   Register Description
    *   Data Register (DR)
    *   Track Register (TR)
    *   Sector Register (SR)
    *   Command Register (CR)
    *   Status Register (STR)
*   Track Format
    *   IBM 3740 (FM)
    *   System 34 (MFM)

## 7. COMMAND DESCRIPTION
*   Command Summary Table
*   Type I (Head Positioning)
    *   Restore, Seek, Step
    *   Flag definitions (h, V, r0, r1)
*   Type II (Read/Write Sector)
    *   Read Sector, Write Sector
    *   Flag definitions (m, S, E, C, a0)
*   Type III (Read/Write Track)
    *   Read Address, Read Track, Write Track
*   Type IV (Interrupt)
    *   Force Interrupt

## 8. ELECTRICAL CHARACTERISTICS
*   Absolute Maximum Ratings
*   Recommended Operating Conditions
*   DC Characteristics
*   AC Characteristics
    *   Clock Timing
    *   Read/Write Timing
    *   Data Transfer Timing

## 9. TIMING DIAGRAMS
*   Read Timing
*   Write Timing
*   Seek Timing
*   Index Timing

## 10. PHYSICAL DIMENSIONS
*   Plastic Package DIP-40
*   Ceramic Package DIP-40
