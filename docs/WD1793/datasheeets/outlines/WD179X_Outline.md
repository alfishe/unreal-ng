# WD179X-02 Floppy Disk Formatter/Controller Datasheet Outline

## 1. FEATURES
*   Soft Sector Format Compatibility
*   Automatic Track Seek with Verification
*   Accommodates Single and Double Density Formats
*   Read Mode
*   Write Mode
*   System Compatibility
*   Control
    *   Single/Multiple Sector Read/Write with Automatic Sector Search
    *   Entire Track Read/Write
    *   Selectable Sector Lengths
    *   All Inputs/Outputs TTL Compatible
    *   On-Chip Drive Select Logic (WD1795/1797)
    *   Side Select Logic (WD1795/1797)
    *   Window Extension

## 2. GENERAL DESCRIPTION
*   MOS/LSI N-Channel Silicon Gate Technology
*   Compatible with FD1791 (Inverted Bus) and FD1793 (True Bus)
*   Comparison of Family Members (1791, 1792, 1793, 1794, 1795, 1797)

## 3. ORGANIZATION
*   Floppy Disk Formatter Block Diagram
*   Data Shift Register
*   Data Register
*   Track Register
*   Sector Register
*   Command Register
*   Status Register
*   CRC Logic
*   Arithmetic/Logic Unit (ALU)
*   Timing and Control

## 4. PIN DESIGNATIONS
*   Pinout Diagram (40-Pin DIP)

## 5. SIGNAL DESCRIPTION
*   Host Interface Signals
    *   /CS, /WE, /RE, A0, A1, DAL0-DAL7, MR, INTRQ, DRQ
*   Disk Drive Interface Signals
    *   STEP, DIRC, EARLY, LATE, HLD, HLT, RG, WG, WD, RAW READ, TR00, IP, WPRT, READY, TG43, VFOE, WF

## 6. ARCHITECTURE
*   Register Map
*   Status Register Organization
*   Command Register Organization

## 7. COMMAND DESCRIPTION
*   Type I Commands (Restore, Seek, Step, Step-In, Step-Out)
    *   Flowchart
    *   Stepping Rates
*   Type II Commands (Read Sector, Write Sector)
    *   Flowchart
    *   Multiple Sector Operation
*   Type III Commands (Read Address, Read Track, Write Track)
    *   Flowchart
    *   Control Bytes for Write Track
*   Type IV Command (Force Interrupt)
    *   Interrupt Conditions

## 8. STATUS REGISTER DESCRIPTION
*   Bit definitions for Type I
*   Bit definitions for Type II & III

## 9. FORMATTING THE DISK
*   IBM 3740 Format (Single Density)
*   IBM System 34 Format (Double Density)
*   Gap Lengths and Sync Bytes
*   Non-IBM Formats

## 10. ELECTRICAL CHARACTERISTICS
*   Absolute Maximum Ratings
*   DC Operating Characteristics
*   AC Timing Characteristics
    *   Read Enable Timing
    *   Write Enable Timing
    *   Input Data Timing
    *   Write Data Timing
    *   Miscellaneous Timing

## 11. APPLICATIONS INFORMATION
*   System Interface (8080/Z80)
*   Floppy Disk Interface
*   Data Separator
