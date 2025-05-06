# WD179X Write Track Command Execution and Timing

## Goal

The "Write Track" command instructs the WD179X Floppy Disk Controller (FDC) to write a continuous stream of data bytes, provided by the host CPU, onto the disk surface for exactly one full revolution. This process starts immediately following the detection of a physical index pulse from the drive and ends immediately following the detection of the *next* index pulse.

This command is primarily used for **diskette formatting**. The FDC does not automatically generate gaps, address marks, or CRCs during this command; instead, it interprets specific data byte patterns (F5h through FEh) received from the host to perform these actions (e.g., writing address marks with appropriate clock patterns, initializing/writing CRC bytes). The host is responsible for sending the correct sequence of data bytes and special command bytes to create the desired track format.

## Command Flags (Bits 0-4)

Command Opcode Structure: **`1111 0E00`** (Binary)

Example Opcodes: `0xF0` (E=0), `0xF4` (E=1)

The Write Track command utilizes bits 0 through 4 as follows:

| Bit | Name | Value | Description                                                                                             |
| :-: | :--- | :---- | :------------------------------------------------------------------------------------------------------ |
| 0   | -    | 0     | **Not Used:** Must be 0 for Write Track command.                                                        |
| 1   | -    | 0     | **Not Used:** Must be 0 for Write Track command.                                                        |
| 2   | `E`  | 0     | **Delay:** No 15ms/30ms head settling delay before operation. Head engagement relies only on HLT input. |
|     |      | 1     | **Delay:** Introduce a 15ms (2MHz) / 30ms (1MHz) delay after head load, *then* wait for HLT input.      |
| 3   | -    | 0     | **Not Used:** Must be 0 for Write Track command.                                                        |
| 4   | -    | 0     | **Not Used:** Must be 0 for Write Track command.                                                        |

*Note: Bits 5, 6, and 7 define the command type and are fixed at `1` for Write Track.*

## Execution Sequence and Host Interaction

The following details the sequence of events involving the FDC and the host CPU during the execution of the "Write Track" command. Timings assume a standard **2MHz FDC clock** unless noted.

**(Assumed Data Rate: 250 kbps MFM Double Density -> Byte Time ≈ 32µs)**
*(For 125 kbps FM Single Density, Byte Time ≈ 64µs)*

1.  **Command Initiation (Host -> FDC):**
    *   The host writes the "Write Track" command byte to the FDC's Command Register.

2.  **FDC Setup & Drive Checks (FDC Internal):**
    *   FDC sets its internal Busy flag (Status Register bit 0).
    *   FDC resets relevant status bits (DRQ, Lost Data, etc.).
    *   FDC checks `READY` input from the drive. If Not Ready, the command is aborted, Busy is reset, and an interrupt (INTRQ) is generated.
    *   FDC asserts the `HLD` (Head Load) output.
    *   FDC waits for head settle time (either based on `HLT` input or internal 15ms delay if command bit E=1).
    *   FDC checks the `WPRT` (Write Protect) input. If active (disk is protected), the command is aborted, the Write Protect status bit is set, Busy is reset, and INTRQ is generated.

3.  **Request First Byte & Initial Host Window (FDC -> Host -> FDC):**
    *   **Event:** FDC asserts the `DRQ` (Data Request) signal HIGH. This notifies the host that the FDC requires the *first* data byte for the track.
    *   **Event:** FDC internally starts a short delay (`DELAY 3 BYTE TIMES`).
        *   Duration: Approx. 3 * (32µs MFM / 64µs FM) = ~96µs MFM / ~192µs FM.
        *   Purpose: Provides a small initial buffer window for the host to respond to the first DRQ before the critical track synchronization point (Index Pulse).
    *   **Event:** After the delay, FDC checks internally if `DRQ` has been serviced (`HAS DRQ BEEN SERVICED?`). Servicing means the host wrote a byte to the FDC's Data Register (DR), which implicitly clears the pending DRQ state within the FDC.
    *   **Host Action:** The host must detect `DRQ` HIGH and write the first byte to the DR.
    *   **Timing Constraint (Initial):** The host must complete this first write **within approximately the 3-byte-time delay window** after DRQ goes HIGH.
    *   **Failure:** If the host fails to write the first byte within this window (`No` from `HAS DRQ BEEN SERVICED?`), the FDC aborts the command, sets the Lost Data status bit (S2), resets Busy, and generates INTRQ (`SET INTRQ LOST DATA RESET BUSY`).

4.  **Synchronize to Index Pulse (FDC Internal):**
    *   **Event:** If the first byte was successfully received (`Yes` from `HAS DRQ BEEN SERVICED?`), the FDC waits for the rising edge of the physical `INDEX PULSE` signal from the drive (`HAS INDEX PULSE OCCURED?`).
    *   **Purpose:** Ensures writing starts precisely at the beginning of the track. The FDC holds the first byte internally until this synchronization occurs.

5.  **Start Writing & Enter Main Loop (FDC <-> Host):**
    *   **Event:** Immediately after the `INDEX PULSE` is detected (`Yes` from `HAS INDEX PULSE OCCURED?`):
        *   FDC transfers the first byte from its internal DR to its Data Shift Register (DSR).
        *   FDC enters the Pattern Check logic (`B` path / `CHECK_PATTERN`).
        *   FDC examines the byte in the DSR. Based on the byte value (Normal data or F5h-FEh) and `DDEN` setting (FM/MFM), it begins outputting the corresponding bitstream (data, clock, missing clock) via the `WD` (Write Data) pin and activates the `WG` (Write Gate) output.
        *   **Event:** Almost immediately after starting to shift out the first byte, the FDC **re-asserts `DRQ` HIGH**, requesting the *second* byte from the host.

6.  **Main Write Loop (Continuous Byte Transfer):**
    *   **(Loop Start - after writing byte N)**
    *   **Event:** FDC checks for the *next* `INDEX PULSE` (`PHYS INDEX MARK?` check).
        *   **IF `INDEX PULSE` DETECTED (Yes):**
            *   The full revolution is complete.
            *   FDC deactivates `WG`.
            *   FDC resets the Busy flag.
            *   FDC generates a final INTRQ to signal command completion.
            *   **Command Terminates Successfully.**
        *   **IF `INDEX PULSE` NOT DETECTED (No):**
            *   Proceed to check host response.
    *   **Event:** FDC checks if the host has serviced the pending `DRQ` by writing the next byte (byte N+1) to the DR (`HAS DR BEEN LOADED?`).
    *   **Host Action:** Host must continuously monitor `DRQ` and write the next byte when `DRQ` is HIGH.
    *   **Critical Timing Constraint (Main Loop):** The host **must** write byte N+1 to the DR **before** the FDC finishes shifting out all 8 bits of byte N. The available window is approximately **one byte-time** (≈32µs for MFM, ≈64µs for FM) from the previous DRQ assertion.
        *   *Note:* The `tSERVICE` times (13.5µs MFM / 27.5µs FM) shown elsewhere relate to FDC's internal status update latency *after* a host access, *not* the required host response time *to* a DRQ during streaming operations like Write Track. The byte time is the governing factor here.
    *   **Failure (Host Too Slow):** If the FDC finishes shifting byte N before the host writes byte N+1 (`No` from `HAS DR BEEN LOADED?`):
        *   FDC internally forces a byte of **zeros** into the DSR and writes this to the disk (`WRITE BYTE OF ZEROS`).
        *   FDC sets the **Lost Data status bit (S2)**.
        *   The flow loops back to check `INDEX PULSE` again. DRQ for byte N+1 remains pending internally until the host eventually writes *something* or the command terminates on the next index pulse.
    *   **Success (Host On Time):** If the host writes byte N+1 before the FDC needs it (`Yes` from `HAS DR BEEN LOADED?`):
        *   FDC transfers byte N+1 from DR to DSR.
        *   FDC enters the Pattern Check logic (`CHECK_PATTERN`) for byte N+1.
        *   FDC performs the appropriate write action (`WRITE...`) via `WD` and `WG`.
        *   **Event:** Almost immediately after starting to shift out byte N+1, FDC **re-asserts `DRQ` HIGH** requesting byte N+2.
        *   Flow loops back to the `PHYS INDEX MARK?` check.
    *   **(Loop End)**

## Status Register (Write Track Completion)

The following table describes the meaning of the Status Register bits after the Write Track command has completed (successfully or with an error).

| Bit | Name            | Meaning After Write Track Command                                                                                                                                                     |
| :-: | :-------------- |:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7   | NOT READY       | Drive not ready (/READY high or MR active) - checked at command start.                                                                                                                |
| 6   | WRITE PROTECT   | Disk write protected (/WPRT low) - checked at command start.                                                                                                                          |
| 5   | WRITE FAULT     | Optional drive fault signal (/WF input low) detected during write phase.                                                                                                              |
| 4   | RECORD NOT FOUND| Not used by Write Track. Always 0.                                                                                                                                                    |
| 3   | CRC ERROR       | Not used by Write Track. Always 0.                                                                                                                                                    |
| 2   | LOST DATA       | Host failed to provide the first byte within ~3 byte-times (~96µs MFM / ~192µs FM) or a subsequent byte within ~1 byte-time (~32µs MFM / ~64µs FM) of DRQ assertion.                  |
| 1   | DRQ             | 0 - Controller does not need data currently; 1 - Controller is supplying data byte to host in Data Register for read operations and waiting a dat byte from host for write operations |
| 0   | BUSY            | 0 - Command completed; 1 - Controller is busy                                                                                                                                         |

## Summary of Critical Host Timings

*   **First Byte Response:** Within ~3 byte-times (~96µs MFM / ~192µs FM) of initial DRQ. Failure aborts command.
*   **Subsequent Bytes Response:** Within ~1 byte-time (~32µs MFM / ~64µs FM) of each subsequent DRQ. Failure causes zeros to be written and sets the Lost Data flag, but the command continues until the next index pulse.

## Error Conditions

*   **Not Ready:** Drive not ready at command start. Command aborted.
*   **Write Protect:** Diskette is write-protected. Command aborted.
*   **Lost Data (Initial):** Host failed to provide the first byte within the initial **~3 byte-time window** (approx. 96µs for 250kbps MFM, 192µs for 125kbps FM) after the first DRQ assertion. Command aborted.
*   **Lost Data (Main Loop):** Host failed to provide a subsequent byte within the **~1 byte-time window** (approx. 32µs for 250kbps MFM, 64µs for 125kbps FM) after DRQ was asserted for that byte. `0x00` written to disk, flag set, command continues until next index pulse.
*   **Write Fault:** Drive indicated a write fault (/WF low) during the write. Command terminates immediately, flag set.

## Emulation Considerations

*   Requires write capability to the disk image representation.
*   No ID field searching or validation is performed by this command.
*   Must accurately model the initial DRQ -> 3-byte delay -> Check DRQ -> Wait for Index Pulse sequence. Abort if the first byte isn't received in time.
*   Implement the `WG` interlock: Write Gate should only become active *after* the first byte is received AND the index pulse is detected.
*   Model the per-byte DRQ timing (~32/64µs) for subsequent bytes during the main loop.
*   If the host emulator writes to the Data Register too late (after the byte time):
    *   Set the LOST DATA flag in the emulated status register.
    *   Write `0x00` to the disk image instead of the intended byte.
    *   Continue the write operation until the next index pulse.
*   The emulator must simulate the FDC's interpretation of special byte patterns (F5h-FEh) provided by the host during the write to accurately generate DAMs, CRCs (using the standard polynomial G(X) = X¹⁶ + X¹² + X⁵ + 1), and handle clock patterns. Alternatively, it can just write the raw bytes sent by the host, but this is less accurate FDC emulation.
*   The write operation must continue for exactly one full revolution, triggered and terminated by the emulated Index Pulse signal.
*   Monitor the emulated /WPRT input state at the start. Monitor the emulated /WF input during the `WG`-active phase.
*   Accurately update all status bits upon command completion or termination due to error.


## Track Structure and Formatting Details

The "Write Track" command provides the mechanism to write raw byte patterns across an entire track, synchronized between index pulses. It does **not** automatically create a standard track format; instead, **the host CPU is responsible for sending a precise sequence of bytes**, including special control bytes, which the WD179X interprets to generate the required structure (gaps, sync fields, address marks, CRC codes, etc.) on the fly. This makes the command ideal for formatting diskettes according to standard or custom layouts.

### General Track Layout Elements

A typical formatted track, such as those conforming to IBM 3740 (Single Density) or IBM System/34 (Double Density) standards targeted by the WD179X, consists of the following elements written sequentially:

1.  **Pre-Index Gap (Gap 4):** A gap written before the physical index hole, allowing time for drive electronics to settle and synchronize before the start of the track data. Often filled with `0xFF` (FM) or `0x4E` (MFM).
2.  **Index Address Mark (IAM):** A unique pattern indicating the start of the track, immediately following the physical index pulse detection.
    *   **FM (IBM 3740):** Typically `0xFC`.
    *   **MFM (IBM System/34):** Typically `0xC2` repeated, followed by `0xFC`. (Generated by host sending specific sequences).
3.  **Post-Index Gap (Gap 1):** Separates the IAM from the first sector's ID field. Allows PLL synchronization.
4.  **Sector Blocks (Repeated per sector):**
    *   **Sync Field:** A sequence of bytes (e.g., `0x00`) allowing the data separator/PLL to lock onto the data rate and phase before the address mark.
    *   **ID Address Mark (IDAM):** A unique pattern signaling the start of a sector's identification field.
        *   **FM:** `0xFE` (with specific clock pattern).
        *   **MFM:** `0xA1` (with missing clock) followed by `0xFE`.
    *   **ID Field:** Contains sector identification information:
        *   Track Number
        *   Side Number
        *   Sector Number
        *   Sector Length Code (00=128, 01=256, 02=512, 03=1024 bytes)
    *   **ID Field CRC:** 2 bytes calculated over the ID Field contents.
    *   **ID-to-Data Gap (Gap 2):** Separates the ID field from the data field, allowing time for the host to process the ID and prepare for read/write.
    *   **Sync Field:** Similar to the ID sync field, preceding the data field.
    *   **Data Address Mark (DAM):** A unique pattern signaling the start of the user data area.
        *   **FM:** `0xFB` (Normal) or `0xF8` (Deleted).
        *   **MFM:** `0xA1` (with missing clock) followed by `0xFB` (Normal) or `0xF8` (Deleted).
    *   **Data Field:** Contains the actual user data bytes (128, 256, 512, or 1024 bytes).
    *   **Data Field CRC:** 2 bytes calculated over the DAM and Data Field contents.
    *   **Inter-Sector Gap (Gap 3):** Separates this sector's data field from the next sector's ID field (or Gap 4 if it's the last sector). Allows for write splices and timing variations.

### FDC Interpretation of Host Data during Write Track

During the "Write Track" command, the WD179X treats most bytes received from the host via the Data Register as raw data to be written. However, it specifically interprets bytes in the range `0xF5` to `0xFE` to perform special actions, as detailed in the datasheet (WD179X Page 14, Table "CONTROL BYTES FOR INITIALIZATION"):

| Byte from Host (Hex) | FM Action (DDEN=1)                     | MFM Action (DDEN=0)                         | Purpose during Formatting                     |
| :------------------- | :------------------------------------- | :------------------------------------------ | :-------------------------------------------- |
| `00` - `F4`          | Write byte with normal FM clock (`FF`) | Write byte with normal MFM encoding         | Write normal data, gap bytes, sync bytes    |
| `F5`                 | *Not Allowed*                          | Write `A1*` (MFM Sync Mark), **Preset CRC** | Start MFM IDAM or DAM, prepare for CRC calc |
| `F6`                 | *Not Allowed*                          | Write `C2**` (MFM Index Mark)               | Write part of MFM Index Address Mark        |
| `F7`                 | **Generate & Write 2 CRC Bytes**       | **Generate & Write 2 CRC Bytes**            | Write CRC for preceding ID or Data field    |
| `F8` - `FB`          | Write byte with `C7` clock, Preset CRC | Write byte normally (MFM)                   | Write FM DAM/IDAM, prepare for CRC calc     |
| `FC`                 | Write byte with `D7` clock             | Write byte normally (MFM)                   | Write FM Index Address Mark                 |
| `FD`                 | Write byte with `FF` clock             | Write byte normally (MFM)                   | (Typically unused special FM pattern)       |
| `FE`                 | Write byte with `C7` clock, Preset CRC | Write byte normally (MFM)                   | Write FM ID Address Mark, prepare for CRC   |
| `FF`                 | Write byte with `FF` clock             | Write byte normally (MFM)                   | Write FM Gap bytes                          |

*(Note: `*` / `**` indicate missing clock transitions specific to MFM Address Marks)*

Therefore, to format a track, the host must send not only the desired data for ID and Data fields but also the specific bytes (`FF`/`4E` for gaps, `00` for sync, `FC`/`C2`/`A1`/`FE`/`FB`/`F8` for marks, `F7` for CRC) that instruct the FDC to generate the correct low-level patterns and control signals.

### CRC Generation

The FDC automatically calculates the 16-bit CRC using the standard polynomial G(X) = X¹⁶ + X¹² + X⁵ + 1. During Write Track, the calculation begins when an address mark is generated (triggered by F5, F8-FB, FE in FM; or F5 in MFM) and concludes, writing the two CRC bytes, when the host sends an `F7` byte.

The entire track structure, including all gaps and control marks, must be explicitly laid out by the sequence of bytes provided by the host CPU during the single revolution of the Write Track command.

## Example Track Structure: MFM 16x256 (IBM System/34 Derivative)

This section illustrates the byte sequences involved in formatting a track for 16 sectors of 256 bytes using MFM encoding, typical of IBM System/34 or compatible PC formats. This assumes the host is using the `Write Track` command and sending the correct sequence of data and control bytes.

**Note:** Exact gap sizes can vary slightly between specific format implementations. The values used here are common examples. The total byte count for this example (~6900 bytes) exceeds the nominal 6250 bytes often quoted for a 250kbps/300RPM track, reflecting the overhead of standard gaps and sync fields.

**(Assumed Parameters: Track `T`, Side `S`, MFM Encoding, 2MHz FDC Clock)**

| Element             | Size (Bytes) | Host Data Sent to FDC (Hex Sequence)                    | FDC Serial Output (Pre-MFM Encode)                     | Physical Flux Pattern (Conceptual)                                                                 |
| :------------------ | :----------- | :------------------------------------------------------ | :----------------------------------------------------- | :------------------------------------------------------------------------------------------------- |
| **Gap 4a**          | 80           | `4E` (repeated 80 times)                                | `4E` (repeated 80 times)                               | Standard MFM encoding for `0x4E` bytes.                                                          |
| **Sync (IAM)**      | 12           | `00` (repeated 12 times)                                | `00` (repeated 12 times)                               | MFM Clock pulses only (`C-C-C-C...`).                                                            |
| **Index Mark (IAM)**| 4            | `F6`, `F6`, `F6`, `FC`                                  | `C2**`, `C2**`, `C2**`, `FC`                           | MFM for `C2` (with missing clock), MFM for `FC`. *Note: Exact FDC handling of F6 may vary.*       |
| **Gap 1**           | 50           | `4E` (repeated 50 times)                                | `4E` (repeated 50 times)                               | Standard MFM encoding for `0x4E`.                                                                  |
|                     |              |                                                         |                                                        |                                                                                                    |
| ***--- Start Sector 1 ---*** |              |                                                         |                                                        |                                                                                                    |
| **Sync (ID)**       | 12           | `00` (repeated 12 times)                                | `00` (repeated 12 times)                               | MFM Clock pulses only (`C-C-C-C...`).                                                            |
| **ID Addr Mark**    | 4            | `F5`, `F5`, `F5`, `FE`                                  | `A1*`, `A1*`, `A1*`, `FE`                              | 3x MFM for `A1` (with missing C5 clock), 1x MFM for `FE`. CRC calculation starts.              |
| **ID Field Data**   | 4            | `TT`, `SS`, `01`, `01`                                  | `TT`, `SS`, `01`, `01`                                 | MFM encoding for Track(`TT`), Side(`SS`), Sector(`01`), Length Code(`01`=256 bytes).                 |
| **ID Field CRC**    | 1            | `F7`                                                    | `C1`, `C2` (Calculated by FDC)                         | MFM encoding for the 2 calculated CRC bytes (`C1`, `C2`).                                          |
| **Gap 2**           | 22           | `4E` (repeated 22 times)                                | `4E` (repeated 22 times)                               | Standard MFM encoding for `0x4E`.                                                                  |
| **Sync (Data)**     | 12           | `00` (repeated 12 times)                                | `00` (repeated 12 times)                               | MFM Clock pulses only (`C-C-C-C...`).                                                            |
| **Data Addr Mark**  | 4            | `F5`, `F5`, `F5`, `FB` (for Normal DAM)                 | `A1*`, `A1*`, `A1*`, `FB`                              | 3x MFM for `A1` (with missing C5 clock), 1x MFM for `FB`. CRC calculation starts.              |
| **Data Field**      | 256          | `E5` (repeated 256 times, typical init value)           | `E5` (repeated 256 times)                              | Standard MFM encoding for the 256 data bytes.                                                    |
| **Data Field CRC**  | 1            | `F7`                                                    | `C3`, `C4` (Calculated by FDC)                         | MFM encoding for the 2 calculated CRC bytes (`C3`, `C4`).                                          |
| **Gap 3**           | 54           | `4E` (repeated 54 times)                                | `4E` (repeated 54 times)                               | Standard MFM encoding for `0x4E`.                                                                  |
| ***--- End Sector 1 ---***   |              |                                                         |                                                        |                                                                                                    |
|                     |              |                                                         |                                                        |                                                                                                    |
| **Sectors 2 - 16**  | (Repeat)     | Repeat Sector Block, incrementing Sector # (`02` to `10` Hex) | Repeat Sector Block, incrementing Sector #             | Repeat patterns for sectors 2 through 16.                                                        |
|                     |              |                                                         |                                                        |                                                                                                    |
| **Gap 4b (Remains)**| Variable     | Host continues sending `4E` until INTRQ                 | Host continues sending `4E` until INTRQ                | Standard MFM encoding for `0x4E`, filling the remaining track space until the next Index Pulse. |

**Explanation of Representations:**

1.  **Host Data Sent to FDC:** This column shows the exact sequence of 8-bit values the host microprocessor must write to the FDC's Data Register via the `/DRQ` handshake during the `Write Track` command execution. Note how single control bytes (`F5`, `F6`, `F7`, `FE`, `FB`) trigger complex actions.
2.  **FDC Serial Output (Pre-MFM Encode):** This represents the logical byte stream the FDC constructs internally *before* applying MFM encoding rules and sending it serially to the drive via the `WD` pin. It shows the expansion of control bytes into sync markers (`A1*`, `C2**`), address marks (`FE`, `FB`), and calculated CRC bytes (`C1`-`C4`). This is the data over which the read process would calculate CRCs later.
3.  **Physical Flux Pattern (Conceptual):** This describes the *nature* of the magnetic transitions written to the disk surface. MFM encoding involves placing clock transitions (C) in the middle of a bit cell and data transitions (D) at the boundary.
    *   `C-C-C-C...`: Represents `0x00` sync bytes (clock transitions only).
    *   `Standard MFM`: Represents normal data bytes (`4E`, `E5`, CRC, etc.) encoded according to MFM rules (a '1' bit = Data transition, '0' bit = Clock transition if previous bit was 0, No transition if previous bit was 1).
    *   `A1* / C2**`: These are special Address Mark patterns that intentionally *violate* MFM encoding rules by omitting a specific clock transition, making them uniquely identifiable by the FDC during read operations.

This detailed breakdown illustrates how the host uses a relatively simple set of control bytes within the `Write Track` command to orchestrate the FDC in generating a complex, precisely structured track format on the physical disk media.





# MFM Track Image Example: 16 Sectors x 256 Bytes (Total 6250 Bytes)
# Values are logical bytes BEFORE MFM encoding

# --- Preamble Fields ---
| Field          | Size | Content (Hex)                                                                     | Notes                   |
|----------------|------|-----------------------------------------------------------------------------------|-------------------------|
| Gap 4a         | 80   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x80)                         | Pre-Index Gap           |
| IAM Sync       | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Index Mark Sync         |
| IAM            | 4    | A1 A1 A1 FC                                                                       | Index Address Mark      |
| Gap 1          | 50   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x50)                                     | Post-Index Gap          |

# --- Sector 1 Block ---
| Field          | Size | Content (Hex)                                                                     | Notes                   |
|----------------|------|-----------------------------------------------------------------------------------|-------------------------|
| ID Sync        | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 1 ID Sync        |
| IDAM           | 4    | A1 A1 A1 FE                                                                       | Sector 1 ID Address Mark|
| ID Field       | 4    | 05 00 01 01                                                                       | Trk=5, Side=0, Sec=1, Len=256 |
| ID CRC         | 2    | C1 C2                                                                             | Placeholder CRC         |
| Gap 2          | 22   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x22)                                     | ID-to-Data Gap          |
| Data Sync      | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 1 Data Sync      |
| DAM            | 4    | A1 A1 A1 FB                                                                       | Sector 1 Data Addr Mark |
| Data Field     | 256  | E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 ... (x256)                                     | Sector 1 Data (Filled)  |
| Data CRC       | 2    | C3 C4                                                                             | Placeholder CRC         |
| Gap 3          | 54   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x54)                                     | Inter-Sector Gap        |

# --- Sector 2 Block ---
| Field          | Size | Content (Hex)                                                                     | Notes                   |
|----------------|------|-----------------------------------------------------------------------------------|-------------------------|
| ID Sync        | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 2 ID Sync        |
| IDAM           | 4    | A1 A1 A1 FE                                                                       | Sector 2 ID Address Mark|
| ID Field       | 4    | 05 00 02 01                                                                       | Trk=5, Side=0, Sec=2, Len=256 |
| ID CRC         | 2    | C1 C2                                                                             | Placeholder CRC         |
| Gap 2          | 22   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x22)                                     | ID-to-Data Gap          |
| Data Sync      | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 2 Data Sync      |
| DAM            | 4    | A1 A1 A1 FB                                                                       | Sector 2 Data Addr Mark |
| Data Field     | 256  | E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 ... (x256)                                     | Sector 2 Data (Filled)  |
| Data CRC       | 2    | C3 C4                                                                             | Placeholder CRC         |
| Gap 3          | 54   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x54)                                     | Inter-Sector Gap        |

... (Sector Blocks 3 through 15 follow the same pattern, incrementing Sector Number from 03 to 0F Hex) ...

# --- Sector 16 Block ---
| Field          | Size | Content (Hex)                                                                     | Notes                   |
|----------------|------|-----------------------------------------------------------------------------------|-------------------------|
| ID Sync        | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 16 ID Sync       |
| IDAM           | 4    | A1 A1 A1 FE                                                                       | Sector 16 ID Address Mark|
| ID Field       | 4    | 05 00 10 01                                                                       | Trk=5, Side=0, Sec=16, Len=256 |
| ID CRC         | 2    | C1 C2                                                                             | Placeholder CRC         |
| Gap 2          | 22   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x22)                                     | ID-to-Data Gap          |
| Data Sync      | 12   | 00 00 00 00 00 00 00 00 00 00 00 00                                               | Sector 16 Data Sync     |
| DAM            | 4    | A1 A1 A1 FB                                                                       | Sector 16 Data Addr Mark |
| Data Field     | 256  | E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 E5 ... (x256)                                     | Sector 16 Data (Filled) |
| Data CRC       | 2    | C3 C4                                                                             | Placeholder CRC         |
| Gap 3          | 54   | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x54)                                     | Inter-Sector Gap        |

# --- Final Gap ---
| Field          | Size | Content (Hex)                                                                     | Notes                   |
|----------------|------|-----------------------------------------------------------------------------------|-------------------------|
| Gap 4b         | 152  | 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E 4E ... (x152)                                    | Fills rest of track     |

# --- End of Track (6250 Bytes Total) ---

---
# Questions

**Question:**
How does the FDC know when to write the index sync?

**Answer:**  
The FDC does **not** automatically generate or insert the *Index Sync* field (`0x00` bytes) or the *Index Address Mark* (IAM, e.g., `FC` or `C2`) during a **Write Track** command. Instead, the FDC relies entirely on the host CPU to send these specific bytes at the correct time within the data stream being written.

### Breakdown of Write Track Execution:

1. **Synchronization:**  
   The FDC first waits for the physical **INDEX PULSE** signal from the drive (Step 4 in the execution sequence). This pulse marks the start of the track.

2. **Writing Begins:**  
   Immediately after detecting the index pulse, the FDC begins writing using the first byte already sent by the host (typically the start of *Gap 4a*).

3. **Continuous Host Feed:**  
   From this point, the FDC issues DRQ (Data Request) signals, and the host must continuously provide the next byte to write.

4. **Host Responsibility:**  
   The host's software must precisely build the track structure:
    - Send the required number of *Gap 4a* bytes (`0x4E` for MFM).
    - At the correct time, begin sending `0x00` bytes for the *Index Sync* field.
    - Follow this with the special sequence interpreted as the IAM (e.g., `F6, F6, F6, FC`, which becomes `C2, C2, C2, FC` in MFM).
    - Continue with Gap 1 and the rest of the track layout.

Summary:

The FDC determines only the **start and end** of the write operation based on **physical index pulses**. All track contents—including the Index Sync field and IAM—must be explicitly sent by the host. The FDC acts as a **serializer and interpreter of special byte sequences**, but **track formatting is fully controlled by the host software**.

---

## **Question**
Why in documentation we're using MFM tracks starting from Gap 4a (pre-index), but we have host sending bytes starting from Index Address Mark (IAM)?  
And how can we determine the length of Gap 4b (remaining gap) if we cannot predict when the index pulse occurs?

## **Answer**

### 1. First Byte Sent vs. Track Representation Starting Point

- **Your Observation is Correct**:  
  As we established, the very first byte the host sends in response to the initial DRQ (after the FDC syncs to the index pulse) is the first byte of the IAM sync field (`0x00`).

- **Why the Example Starts with Gap 4a**:  
  The C++ vector and the hex dump example represent the complete, logical structure of the track as it exists on the disk *between two index pulses*. This is the standard way raw track images (`.img`, `.dsk`, etc.) are stored and analyzed. It’s a circular representation laid out linearly. Tools and emulators working with these images expect contiguous data in this format, often starting conventionally before the index mark (e.g., at Gap 4a).

- **Reconciling Host Action and Representation**:  
  Think of it like this:
    - The host program implementing `Write Track` knows the full target track layout.
    - When the FDC requests the first byte after the index pulse, the host provides the byte corresponding to the start of the IAM Sync.
    - The host continues feeding bytes sequentially from that point in the target layout.
    - After the last planned byte (e.g., end of Gap 3 after Sector 16), the host wraps around in its conceptual model and begins sending bytes from Gap 4a again.
    - It continues to feed these "wrapped around" Gap 4a bytes (and possibly IAM Sync, IAM, Gap 1 again if rotation misaligns) until the FDC stops requesting data.

- **Should the Example Change?**  
  No. Changing `formatted_track_mfm_16x256` to start at IAM Sync would make it an incorrect representation of a standard raw track image. It correctly shows the *final resulting structure*. The key is understanding that the host starts feeding from a specific point based on the index pulse.

### 2. Determining the Length of Gap 4b (The Final Gap)

- **Your Question is Spot On**:  
  The host cannot know the exact length of Gap 4b in advance, since it can't precisely predict the disk's rotational position when the command starts.

- **The FDC Handles It**:  
  The `Write Track` command writes continuously from one index pulse to the next. The FDC controls when to stop.

- **Host Strategy**
