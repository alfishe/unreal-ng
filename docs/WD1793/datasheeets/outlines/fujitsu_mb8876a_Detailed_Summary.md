# Fujitsu MB8876A Floppy Disk Controller Detailed Summary

## 1. FEATURES
*   **Process:** Fabricated using Silicon-gate NMOS technology.
*   **Compatibility:** Pin and function compatible with Western Digital WD1791 (MB8876A) and WD1793 (MB8877A).
*   **Power Supply:** **Single +5V Supply** (No +12V/-5V required). This is a major difference from the original WD179X.
*   **Formats:** Supports Single Density (FM) and Double Density (MFM).
*   **Versions:**
    *   **MB8876A:** Inverted Data Bus (Like WD1791).
    *   **MB8877A:** True Data Bus (Like WD1793).
*   **Sector Lengths:** Selectable 128, 256, 512, 1024 bytes.
*   **Control:** Automatic track seeking with verification.
*   **Interface:** TTL compatible I/O.

## 2. GENERAL DESCRIPTION
*   The MB8876A is an LSI Floppy Disk Controller (FDC) designed to interface a CPU to a floppy disk drive.
*   It provides all the functions necessary for reading/writing data to the disk, including:
    *   Formatting.
    *   Seek control.
    *   Read/Write data transfer.
    *   CRC generation and checking.
*   **Difference Highlight:** The device operates on a single +5V supply, simplifying power supply design compared to the tri-voltage WD179X.
*   It requires an external data separator (VFO) and write precompensation logic (similar to WD1791/3).

## 3. BLOCK DIAGRAM
*   **Internal Bus:** 8-bit internal bus connecting all functional blocks.
*   **Registers:**
    *   **Data Register (DR):** Buffers data.
    *   **Track Register (TR):** Stores head position.
    *   **Sector Register (SR):** Stores target sector.
    *   **Command Register (CR):** Receives commands.
    *   **Status Register (STR):** Stores status flags.
*   **Control Logic:** Decodes commands and manages the state machine.
*   **ALU:** Arithmetic Logic Unit for track/sector comparisons.
*   **CRC Generator/Checker:** 16-bit CRC logic.
*   **Interface Logic:**
    *   **CPU Interface:** DAL bus buffers and control signals (/CS, /RW, etc.).
    *   **FDD Interface:** Output drivers and input receivers for drive signals.

## 4. PIN ASSIGNMENT
*   **Package:** 40-pin DIL (Dual In-line Package).
*   **Pinout Difference from WD179X:**
    *   Pin 2 (WD Vbb -5V) is **N.C.** (No Connect) or internally unconnected.
    *   Pin 35 (WD Vdd +12V) is **N.C.** (No Connect).
    *   Pin 15 is Vcc (+5V). Pin 13 is Vss (GND).
    *   All other signal pins match the WD179X pinout.

## 5. PIN DESCRIPTION
*   **Power Supply:**
    *   **Vcc (Pin 15):** +5V +/- 5%.
    *   **Vss (Pin 13):** Ground.
*   **CPU Interface:**
    *   **/CS (Pin 1):** Chip Select.
    *   **/WE (Pin 3):** Write Enable.
    *   **/RE (Pin 19):** Read Enable.
    *   **A0 (Pin 37), A1 (Pin 36):** Register Address.
    *   **D0-D7 (Pins 4-10, 12):** Data Bus. (Note: MB8876A inverts these; MB8877A does not).
    *   **DRQ (Pin 38):** Data Request.
    *   **/INTRQ (Pin 39):** Interrupt Request. **Note:** Fujitsu datasheet typically denotes active-low with a bar, but WD1793 uses active-high. Fujitsu MB8877A (True bus) is Active High. MB8876A (Inverted bus) is Active High. *Correction:* The Fujitsu datasheet often labels this simply INTRQ.
    *   **/MR (Pin 21):** Master Reset.
*   **FDD Interface:**
    *   **/STEP (Pin 14):** Step Pulse.
    *   **/DIRC (Pin 16):** Direction Control.
    *   **/HLD (Pin 28):** Head Load Output.
    *   **HLT (Pin 23):** Head Load Timing Input.
    *   **/WG (Pin 27):** Write Gate.
    *   **/WD (Pin 31):** Write Data.
    *   **/RAW READ (Pin 29):** Read Data Input.
    *   **/TR00 (Pin 33):** Track 00 Input.
    *   **/IP (Pin 34):** Index Pulse Input.
    *   **/WPRT (Pin 20):** Write Protect Input.
    *   **/READY (Pin 32):** Drive Ready Input.
    *   **/VFOE (Pin 25):** VFO Enable (Active Low). *Note: On WD1791/3 this is RG (Read Gate, Active High). Fujitsu calls it /VFOE but functionally it's usually the inverse or similar purpose - to enable the external data separator.* **Actually, check:** WD Pin 25 is RG (High active). Fujitsu Pin 25 is /VFOE (Low active).

## 6. FUNCTIONAL DESCRIPTION
*   **Register Description:** Detailed breakdown of how to access DR, TR, SR, CR, STR.
    *   **Address Map:** Same as WD179X (00=Status/Cmd, 01=Track, 10=Sector, 11=Data).
*   **Track Format:**
    *   **IBM 3740 (FM):** 26 sectors, 128 bytes/sector (typical).
    *   **System 34 (MFM):** 26 sectors, 256 bytes/sector (typical).
    *   **Index Gap (Gap 1):** Area after index pulse.
    *   **ID Gap (Gap 2):** Area between ID field and Data field.
    *   **Data Gap (Gap 3):** Area between Data field and next ID.
    *   **Track Gap (Gap 4):** Area after last sector until index.

## 7. COMMAND DESCRIPTION
*   **Command Summary Table:** Lists all 11 commands and their bit codes/flags.
*   **Type I (Head Positioning):**
    *   **Restore:** Seeks to Track 0.
    *   **Seek:** Seeks to TR specified in Data Register.
    *   **Step/Step-In/Step-Out:** Single track movement.
    *   **Update Flag (u):** Controls whether TR is updated.
    *   **Verify Flag (V):** Controls ID field verification.
    *   **Head Load Flag (h):** Controls HLD output.
*   **Type II (Read/Write Sector):**
    *   **Read Sector:** Reads data from sector.
    *   **Write Sector:** Writes data to sector.
    *   **Multi-Sector (m):** Sequential operation.
    *   **Side Compare (C, S):** For double-sided disks.
    *   **Settling Delay (E):** 15ms delay enable.
    *   **Deleted Data (a0):** Handles Deleted Data Address Marks.
*   **Type III (Read/Write Track):**
    *   **Read Address:** Returns current ID field.
    *   **Read Track:** Raw track dump.
    *   **Write Track:** Track formatting.
*   **Type IV (Interrupt):**
    *   **Force Interrupt:** Stops command or sets interrupt condition (Not Ready, Ready, Index).

## 8. ELECTRICAL CHARACTERISTICS
*   **Absolute Maximum Ratings:**
    *   Supply Voltage: -0.5 to +7.0 V.
    *   Input Voltage: -0.5 to +7.0 V.
    *   Power Dissipation: ~0.5W.
*   **Recommended Operating Conditions:**
    *   Vcc: 4.75V to 5.25V.
    *   Temp: 0 to 70 C.
*   **DC Characteristics:**
    *   Voh (Output High): 2.4V min.
    *   Vol (Output Low): 0.45V max.
    *   Iil (Input Leakage): +/- 10 uA.
*   **AC Characteristics:**
    *   **Clock:** 1MHz (FM), 2MHz (MFM). Clock Pulse Width: 230ns min (at 2MHz).
    *   **Read/Write:** Setup and Hold times for Address, CS, Data.
    *   **Data Transfer:** DRQ reset time, INTRQ delay time.

## 9. TIMING DIAGRAMS
*   **Read Timing:** Shows sequence of Address valid -> /CS + /RE low -> Data valid.
*   **Write Timing:** Shows sequence of Address valid -> /CS + /WE low -> Data latched.
*   **Seek Timing:** Shows STEP pulse timing (pulse width, spacing based on rate).
*   **Index Timing:** Shows relationship between IP input and internal index status.

## 10. PHYSICAL DIMENSIONS
*   **Package Types:**
    *   **DIP-40P-M01:** Plastic package dimensions (Length ~52mm, Width ~14mm).
    *   **DIP-40C-C01:** Ceramic package dimensions.
    *   Diagrams include pin spacing (2.54mm pitch) and mechanical tolerances.
