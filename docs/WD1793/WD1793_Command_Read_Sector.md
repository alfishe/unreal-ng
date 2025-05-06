# WD1793 Command: Read Sector (Type II)

## Overview

Command Code: **`100m SPCE a0`** (e.g., `0x88` for single sector, side 0, 15ms delay, normal DAM)

The Read Sector command locates a specific sector on the disk based on its ID field and transfers the corresponding data field byte-by-byte to the host CPU via the Data Register, using the DRQ signal for handshaking.

## Command Flags (Bits 0-4)

| Bit | Name | Value | Description                                                                                             |
| :-: | :--- | :---- | :------------------------------------------------------------------------------------------------------ |
| 0   | `a0` | 0     | **Address Mark:** Not directly used by Read Sector, but reflects status bit 5 later (0=FB read).        |
|     |      | 1     | **Address Mark:** Not directly used by Read Sector, but reflects status bit 5 later (1=F8 read).        |
| 1   | `C`  | 0     | **Side Compare:** Disable side comparison. Read sector from the currently selected side.                |
|     |      | 1     | **Side Compare:** Enable side comparison. The side number in the ID field must match the `S` flag.      |
| 2   | `E`  | 0     | **Delay:** No 15ms/30ms head settling delay before operation. Head engagement relies only on HLT input. |
|     |      | 1     | **Delay:** Introduce a 15ms (2MHz) / 30ms (1MHz) delay after head load, *then* wait for HLT input.      |
| 3   | `S`  | 0     | **Side Select:** If `C=1`, compare for Side 0 in the ID field.                                          |
|     |      | 1     | **Side Select:** If `C=1`, compare for Side 1 in the ID field.                                          |
| 4   | `m`  | 0     | **Multiple Sector:** Read only a single sector specified in the Sector Register.                        |
|     |      | 1     | **Multiple Sector:** Read multiple sectors sequentially, automatically incrementing the Sector Register. |

## Execution Flow

1.  **Initiation:**
    *   Host CPU pre-loads the **Sector Register** with the target sector number.
    *   Host writes the Read Sector command byte to the Command Register.
    *   Controller samples the **/READY** input. If inactive (high), sets **NOT READY** (S7) and terminates with an interrupt.
    *   If ready, controller sets **BUSY** (S0=1).
    *   Controller asserts `HLD` (Head Load) output.
    *   If `E=1`, waits 15ms/30ms settling delay.
    *   Waits for the `HLT` (Head Load Timing) input to go high (head engaged). Times out if HLT remains low.

2.  **ID Field Search & Validation:**
    *   Controller scans the incoming raw data stream (`RAW READ` input, clocked by `RCLK`) for an ID Address Mark (IDAM - `FE` in FM, `A1*+FE` precursor in MFM).
    *   Upon finding an IDAM, it reads the ID field contents: Track Number, Side Number, Sector Number, Sector Length, and ID CRC.
    *   **Validation Checks:**
        *   Compares the Track Number from ID field with the controller's internal **Track Register**.
        *   If `C=1`, compares the Side Number from ID field with the `S` flag in the command.
        *   Compares the Sector Number from ID field with the controller's **Sector Register**.
        *   Calculates and verifies the ID field's CRC.
    *   **Mismatch/Error:** If any comparison fails or ID CRC is bad:
        *   If CRC error, sets **CRC ERROR** (S3) temporarily.
        *   Continues searching for the next IDAM.
    *   **Timeout:** If a matching, valid ID field is not found within 5 index pulses (revolutions), sets **RECORD NOT FOUND** (S4) and terminates with an interrupt.

3.  **Data Field Search:**
    *   Once a matching, valid ID field is found, the controller looks for the subsequent Data Address Mark (DAM - `FB` for normal, `F8` for deleted).
    *   **Timeout:** The DAM *must* be found within a specific byte window after the ID field CRC (e.g., 43 bytes in MFM, 30 bytes in FM). If not found, sets **RECORD NOT FOUND** (S4) and terminates with an interrupt.
    *   The type of DAM found (`FB` or `F8`) is stored internally and reflected in Status Bit 5 upon command completion.

4.  **Data Transfer:**
    *   Upon finding the DAM, the controller reads the data field bytes serially.
    *   As each complete byte is assembled in the internal shift register, it's transferred in parallel to the **Data Register**.
    *   Controller asserts **DRQ** (Data Request signal and Status Bit 1).
    *   **Host Action:** The host CPU must read the Data Register before the *next* byte is assembled and transferred (~64µs in FM, ~32µs in MFM).
    *   **Lost Data:** If the host fails to read the Data Register in time (DRQ is still high when the next byte arrives), the controller sets the **LOST DATA** bit (S2). The newly arrived byte overwrites the unread byte in the Data Register. The read operation continues, but data integrity is compromised.
    *   This process repeats for the number of bytes specified by the Sector Length code in the preceding ID field (128, 256, 512, or 1024 bytes).

5.  **Data CRC Verification:**
    *   After the last data byte, the controller reads the 2-byte Data Field CRC from the disk.
    *   It compares the read CRC with its internally calculated CRC covering the DAM and data bytes.
    *   If the CRCs do not match, sets **CRC ERROR** (S3). *Note:* This overwrites any temporary CRC error set during ID search.

6.  **Completion / Multi-Sector Handling:**
    *   **Single Sector (`m=0`):**
        *   Command terminates.
        *   Controller clears **BUSY** (S0).
        *   Asserts **/INTRQ**.
        *   Updates Status Register (S0-S7) with final status.
    *   **Multiple Sector (`m=1`):**
        *   Controller increments its internal **Sector Register**.
        *   If the Sector Register exceeds the maximum expected sectors per track (implicitly known or requires careful host management), sets **RECORD NOT FOUND** (S4) and terminates.
        *   Otherwise, loops back to Step 2 (ID Field Search) to find the *next* sector automatically.
        *   The command only terminates (clears BUSY, asserts /INTRQ) when:
            *   An error occurs (RNF, CRC Error, Lost Data).
            *   The Sector Register wraps around or reaches a limit.
            *   A **Force Interrupt** (Type IV) command is received.

## Status Register (Read Sector Completion)

| Bit | Name            | Meaning After Read Sector Command                                     |
| :-: | :-------------- | :-------------------------------------------------------------------- |
| 7   | NOT READY       | Drive not ready (/READY input high or MR active).                     |
| 6   | WRITE PROTECT   | (Not typically relevant for read, reflects /WPRT input).              |
| 5   | RECORD TYPE     | Type of Data Address Mark found: 1 = Deleted (F8), 0 = Normal (FB). |
| 4   | RECORD NOT FOUND| Matching ID field or subsequent DAM not found within timeout.         |
| 3   | CRC ERROR       | CRC error in the *last processed* ID field OR the data field.         |
| 2   | LOST DATA       | Host failed to read Data Register before next byte arrived.           |
| 1   | DRQ             | 0 (Data Register has been read or command ended).                     |
| 0   | BUSY            | 0 (Command completed).                                                |

## Emulation Considerations

*   Requires access to a disk image representation.
*   Must simulate searching for IDAMs and DAMs based on rotational position.
*   Validate ID fields against internal Track/Sector registers and command flags (`C`, `S`).
*   Calculate and verify ID and Data CRCs.
*   Model the byte transfer timing (~32/64µs per byte).
*   Implement the DRQ signal/status bit logic.
*   Detect host read timing: If the emulator's Data Register is read *after* the next byte should have arrived, set LOST DATA.
*   Handle the 5-revolution timeout for ID search and the byte-window timeout for DAM search.
*   Implement multi-sector logic (`m=1`) by incrementing the Sector Register and looping.
*   Accurately update all status bits upon termination.