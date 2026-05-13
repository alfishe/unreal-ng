# WD179X-02 Floppy Disk Formatter/Controller Detailed Summary

## 1. FEATURES
*   **Soft Sector Format Compatibility:** Supports standard soft-sector formatting.
*   **Automatic Track Seek with Verification:** Built-in logic to step heads to a target track and verify positioning via ID field read.
*   **Accommodates Single and Double Density Formats:**
    *   **IBM 3740 Single Density (FM):** Standard FM encoding.
    *   **IBM System 34 Double Density (MFM):** Modified Frequency Modulation encoding for higher capacity.
*   **Read Mode:**
    *   **Single/Multiple Sector Read:** Can read one sector or a contiguous block of sectors automatically.
    *   **Entire Track Read:** capable of reading the full track content including gaps and address marks.
    *   **Selectable Sector Lengths:** Supports 128, 256, 512, or 1024 byte sectors (defined in ID field).
*   **Write Mode:**
    *   **Single/Multiple Sector Write:** Writes data to specific sectors.
    *   **Entire Track Write:** Formats the diskette by writing the entire track from Index Pulse to Index Pulse.
*   **System Compatibility:**
    *   **Double Buffering:** 8-bit Data Register buffers data between the serial shifter and the host bus.
    *   **Bi-directional Bus:** 8-bit DAL bus for command/status/data transfer.
    *   **DMA or Programmed I/O:** Supports DRQ/INTRQ handshaking for both modes.
    *   **TTL Compatibility:** All inputs and outputs are TTL compatible levels.
*   **On-Chip Drive Select Logic (WD1795/1797):** Specific to these models, includes internal logic for drive selection.
*   **Side Select Logic (WD1795/1797):** Output pin to control double-sided drive head selection.
*   **Window Extension:** Feature to improve read margins.

## 2. GENERAL DESCRIPTION
*   **Technology:** Manufactured using N-Channel Silicon Gate MOS/LSI technology.
*   **Compatibility:**
    *   WD1791: Compatible with FD1771 (FM only predecessor) software commands but adds MFM. Inverted Data Bus.
    *   WD1793: Identical to 1791 but with a **True (Non-Inverted) Data Bus**.
*   **Family Breakdown:**
    *   **1791:** FM/MFM, Inverted Bus.
    *   **1792:** FM Only, Inverted Bus.
    *   **1793:** FM/MFM, True Bus.
    *   **1794:** FM Only, True Bus.
    *   **1795:** FM/MFM, Inverted Bus, Internal Data Separator, Side Select.
    *   **1797:** FM/MFM, True Bus, Internal Data Separator, Side Select.

## 3. ORGANIZATION
*   **Block Diagram:** Illustrates the internal architecture.
    *   **Data Shift Register:** Converts serial disk data to parallel host data and vice versa.
    *   **Data Register (DR):** 8-bit register holding the byte being transferred. Access via A1=1, A0=1.
    *   **Track Register (TR):** 8-bit register holding the current physical head position (0-255). Incremented/decremented by Step logic. Access via A1=0, A0=1.
    *   **Sector Register (SR):** 8-bit register holding the target sector number for Read/Write commands. Access via A1=1, A0=0.
    *   **Command Register (CR):** 8-bit write-only register. Commands are latched here to initiate operations. Access via A1=0, A0=0 (Write).
    *   **Status Register (STR):** 8-bit read-only register. Contains flags like Busy, CRC Error, etc. Access via A1=0, A0=0 (Read).
    *   **CRC Logic:** Generates/Checks 16-bit CRC (CCITT polynomial $x^{16} + x^{12} + x^5 + 1$). Includes preset logic for specific address marks.
    *   **ALU:** Performs comparisons (TR vs Target Track, SR vs Read Sector ID) and increment/decrement operations.
    *   **Timing and Control:** State machine managing the sequence of operations (Read, Write, Seek, etc.) and generating external control signals.

## 4. PIN DESIGNATIONS
*   **Diagram:** Shows 40-pin DIP package layout.
    *   **Power:** Pin 15 (Vcc +5V), Pin 35 (Vdd +12V), Pin 2 (Vbb -5V), Pin 13 (Vss Ground). *Note: Fujitsu clone requires only +5V.*
    *   **Bus:** Pins 4-10, 12 (DAL0-DAL7).
    *   **Control:** Pins 1 (/CS), 3 (/WE), 19 (/RE), 36 (A1), 37 (A0).
    *   **Clock/Reset:** Pin 24 (CLK), Pin 21 (/MR).
    *   **Drive Interface:** STEP, DIRC, HLD, HLT, WD, WG, RAW READ, TR00, IP, WPRT, READY.

## 5. SIGNAL DESCRIPTION
*   **Host Interface:**
    *   **/CS (Chip Select):** Low enables communication.
    *   **/WE (Write Enable):** Low strobes data into selected register.
    *   **/RE (Read Enable):** Low gates data from selected register to bus.
    *   **A0, A1:** Register selection (00=Status/Cmd, 01=Track, 10=Sector, 11=Data).
    *   **DAL0-DAL7:** 8-bit bi-directional data bus. High impedance when not selected.
    *   **MR (Master Reset):** Low resets device. Clears Command Register, sets Not Ready status (bit 7), sets Sector Register to 0x01. **Triggers automatic Restore command.**
    *   **INTRQ (Interrupt Request):** Active High. Indicates command completion or error condition. Reset by reading Status or writing Command.
    *   **DRQ (Data Request):** Active High. Indicates DR contains data to read or is empty for write. Handshake signal.
*   **Disk Drive Interface:**
    *   **STEP:** Output pulse. Moves head one track.
    *   **DIRC (Direction):** Output. High = Step In (Track N -> N+1); Low = Step Out.
    *   **EARLY/LATE:** Outputs. Used for Write Precompensation logic (MFM).
    *   **HLD (Head Load):** Output High. Activates head load solenoid.
    *   **HLT (Head Load Timing):** Input High. Indicates head is engaged and stable.
    *   **RG (Read Gate):** Output (WD1791/3). Active High during Read operations to synchronize external data separator.
    *   **WG (Write Gate):** Output High. Enables write current to head.
    *   **WD (Write Data):** Output. Serial composite MFM/FM data pulses (250ns width MFM, 500ns FM).
    *   **RAW READ:** Input. Negative going pulses from drive interface (flux transitions).
    *   **TR00 (Track 00):** Input Low. Indicates head is at outermost track.
    *   **IP (Index Pulse):** Input Low. Indicates beginning of track.
    *   **WPRT (Write Protect):** Input Low. Prevents Write operations.
    *   **READY:** **Active Low** input. Indicates drive is ready (rotating, door closed). **Critical:** Logic 0 at pin = Ready Status 1 in register? No, Logic 0 at pin = "Ready" state, Status Register Bit 7 is "Not Ready", so Pin=0 -> S7=0.
    *   **TG43:** Output. Active High when Track Register > 43. Used to enable Write Precompensation.
    *   **DDEN:** Input. Low = Double Density (MFM); High = Single Density (FM).

## 6. ARCHITECTURE
*   **Register Map Table:**
    *   Shows Address lines A0/A1 and Read/Write modes mapping to registers.
*   **Status Register:**
    *   Bits change meaning based on the last command (Type I vs Type II/III).
    *   S7: Not Ready.
    *   S6: Write Protect.
    *   S5: Head Loaded (Type I) / Record Type (Type II Read) / Write Fault (Type II Write).
    *   S4: Seek Error (Type I) / Record Not Found (Type II).
    *   S3: CRC Error.
    *   S2: Track 0 (Type I) / Lost Data (Type II).
    *   S1: Index Pulse (Type I) / DRQ (Type II).
    *   S0: Busy.

## 7. COMMAND DESCRIPTION
*   **General:** Command byte latched in CR. Busy bit set. INTRQ reset.
*   **Type I Commands (Head Positioning):**
    *   **Restore:** Step out until TR00 detected. Sets Track Register to 0.
    *   **Seek:** Step to target track stored in Data Register.
    *   **Step:** Step one track in same direction as previous step.
    *   **Step-In:** Step one track inward (towards 76).
    *   **Step-Out:** Step one track outward (towards 0).
    *   **Flags:**
        *   **h (Head Load):** Load head at start.
        *   **V (Verify):** Verify position by reading ID field on destination track.
        *   **r1, r0:** Step rate selection (3, 6, 10, 15 ms at 2MHz MFM).
        *   **u:** Update Track Register.
*   **Type II Commands (Sector Read/Write):**
    *   **Read Sector:** Reads single or multiple sectors.
    *   **Write Sector:** Writes single or multiple sectors.
    *   **Flags:**
        *   **m (Multiple):** Read/Write contiguous sectors.
        *   **S (Side):** Compare Side ID field.
        *   **E (Delay):** 15ms settling delay.
        *   **C (Side Compare):** Enable side check.
        *   **a0:** Data Address Mark type (Normal FB vs Deleted F8).
*   **Type III Commands (Track Operations):**
    *   **Read Address:** Reads next ID field found. Returns 6 bytes (Track, Side, Sector, Length, CRC1, CRC2).
    *   **Read Track:** Reads entire track raw data (gaps, AMs, data) from Index to Index. No CRC checking.
    *   **Write Track:** Formats track. Host supplies all bytes including gaps. FDC generates CRC and special Address Marks (A1, C2) upon receiving special control bytes (F5, F6, F7).
*   **Type IV Command (Force Interrupt):**
    *   **Immediate Interrupt:** Stops current command.
    *   **Conditional Interrupt:** Interrupt on Index, Ready transition, etc.
    *   **Status:** Busy bit cleared immediately.

## 8. STATUS REGISTER DESCRIPTION
*   **Detailed Bit Definitions Tables:**
    *   Separate table for Type I commands.
    *   Separate table for Type II/III commands.
    *   Explains conditions for setting/clearing each bit (e.g., CRC Error is set if calculated CRC does not match read CRC).

## 9. FORMATTING THE DISK
*   **IBM 3740 Format (Single Density FM):**
    *   Layout diagram showing Index Gap, ID Fields, Data Fields, Gaps.
    *   Gap sizes: Gap 1 (40 bytes FF), Gap 2 (11 bytes FF), Gap 3 (variable), Gap 4 (variable).
    *   Sync bytes: 6 bytes of 00.
    *   Address Marks: FE (ID), FB (Data), F8 (Deleted Data).
*   **IBM System 34 Format (Double Density MFM):**
    *   Layout diagram.
    *   Gap sizes: Gap 1 (80 bytes 4E), Gap 2 (22 bytes 4E), Gap 3 (variable).
    *   Sync: 12 bytes 00.
    *   **A1 Sync:** 3 bytes of A1 with missing clock (0x0A clock pattern) precede Address Marks.
    *   **C2 Sync:** Used for Index Mark (if present).
    *   **Formatting Process:**
        *   Write Track command flow.
        *   Host sends 0x4E for Gaps, 0x00 for Sync.
        *   Host sends **0xF5** to write A1 Sync mark.
        *   Host sends **0xF6** to write C2 Index mark.
        *   Host sends **0xF7** to write calculated CRC bytes.

## 10. ELECTRICAL CHARACTERISTICS
*   **Absolute Maximum Ratings:**
    *   Voltage on any input: -0.5V to +7V.
    *   Storage Temp: -55C to +125C.
*   **DC Operating Characteristics:**
    *   V_IL (Input Low): -0.5V to 0.8V.
    *   V_IH (Input High): 2.0V to Vcc.
    *   I_CC (Supply Current): Typ 60-100mA (depends on model/load).
*   **AC Timing Characteristics:**
    *   **Read Enable Timing:** Setup/Hold times for CS, RE, DAL access.
    *   **Write Enable Timing:** Setup/Hold times for CS, WE, Data setup.
    *   **Input Data Timing:** Pulse widths for RAW READ.
    *   **Write Data Timing:** Pulse widths for WD (500ns FM, 250ns MFM).
    *   **Miscellaneous:** Step pulse width, Direction setup time, MR pulse width.

## 11. APPLICATIONS INFORMATION
*   **System Interface:**
    *   Connection to 8080/Z80 bus.
    *   Wait state generation using DRQ (optional).
    *   Interrupt handling.
*   **Floppy Disk Interface:**
    *   Connection to standard Shugart interface (SA400/800).
    *   External Phase Locked Loop (PLL) data separator circuit recommendation (for 1791/3).
    *   Write Precompensation circuit example (74LS195 shift register logic).
