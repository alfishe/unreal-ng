# WD1793 Command: Read Sector (Type II)

## Overview

Command Code: **`100m S E C 0`** (Bit 4=m, Bit 3=S, Bit 2=E, Bit 1=C, Bit 0=0)

Example: `0x88` = single sector, side 0, 15ms delay, no side compare

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
    *   **Timeout:** If a matching, valid ID field is not found within up to 5 index pulses (~1 second @ 300 RPM), sets **RECORD NOT FOUND** (S4) and terminates with an interrupt.

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
| 5   | RECORD TYPE     | Reflects the Data Address Mark found: 1 = Deleted Data Mark (F8), 0 = Normal Data Mark (FB). |
| 4   | RECORD NOT FOUND| Matching ID field or subsequent DAM not found within timeout.         |
| 3   | CRC ERROR       | CRC error in the *last processed* ID field OR the data field.         |
| 2   | LOST DATA       | Host failed to read Data Register before next byte arrived.           |
| 1   | DRQ             | 0 (Data Register has been read or command ended).                     |
| 0   | BUSY            | 0 (Command completed).                                                |

## READ SECTOR execution flow from another angle

Let's describe the process from the FDC's perspective, assuming the host CPU has already issued the command and its parameters.

**Command Parameters (Typically loaded by the host CPU into FDC registers):**

1.  **Command Code:** Specific byte for "Read Sector".
2.  **Drive Number:** (0-3) Which drive to use.
3.  **Head Number:** (0 or 1) Which side of the disk.
4.  **Track Number:** The desired physical track on the disk (e.g., 0-39, 0-79).
5.  **Sector Number:** The desired logical sector number on that track (e.g., 1-16).
6.  **(Implicit) Sector Size:** Often derived from a previous "Specify" command or hardwired, but the FDC will verify it against the sector's ID field.
7.  **(For multi-sector reads) Sector Count:** How many sectors to read. For a single sector read, this is 1.

**FDC "Read Sector" Execution Steps:**

The FDC takes over once the command is issued. The "track byte" counter here is conceptual, representing the serial MFM data stream being read from the disk head. The Index Pulse signals the start of a physical track.

**Phase 1: Preparation and Seek (if necessary)**

1.  **Drive & Head Select:**
    *   The FDC activates the appropriate `DS0-DS3` (Drive Select) line.
    *   It activates the `SIDE1` line if head 1 is requested.
    *   It may issue a `HDL` (Head Load) signal if the drive requires it and the head isn't already loaded. There's a head load delay.
    *   **Track Bytes:** The FDC isn't actively processing track bytes for sector data yet. It's setting up the drive.

2.  **Seek to Target Track (if not already there):**
    *   The FDC compares its internal "current track register" (where it *thinks* the head is) with the commanded "target track number".
    *   **If different:**
        *   It issues `DIRC` (Direction) and `STEP` pulses to the drive mechanism to move the head.
        *   After the last step pulse, it waits for a "Head Settle Time" (e.g., 15-30ms) to allow mechanical vibrations to cease.
        *   During the seek, the `BUSY` status bit is set.
        *   A "Verify" operation might occur (FDC reads the first ID field it finds to confirm it's on the correct track). If not, a **Seek Error** occurs.
    *   **Track Bytes:** During the seek itself, the data read is irrelevant. After settling, the FDC starts looking for ID fields.

**Phase 2: Sector Search (Finding the ID Field)**

Once the FDC believes it's on the correct physical track and the head is settled:

3.  **Enable Read Circuits & Search for ID Address Mark (IDAM):**
    *   The FDC starts continuously reading the MFM data stream from the disk.
    *   It looks for a sequence of **Sync Bytes** (e.g., 12 bytes of 0x00 MFM encoded).
    *   Following the Sync, it looks for the **ID Address Mark (IDAM)** pattern (e.g., 3 bytes of 0xA1 preamble + 0xFE MFM encoded IDAM byte).
    *   **Track Bytes:** This is where the FDC starts actively processing the MFM stream from the current track, byte by byte, looking for the IDAM. The exact byte offset from the Index Pulse where it *starts* this search depends on where the head landed after the seek and how much of the track has already passed.

4.  **Read and Verify ID Field:**
    *   **Upon finding an IDAM:**
        *   The FDC reads the next 6 bytes:
            *   Track Number (from disk ID field)
            *   Head Number (from disk ID field)
            *   Sector Number (from disk ID field)
            *   Sector Size Code (e.g., 00=128B, 01=256B, 02=512B, 03=1024B)
            *   ID CRC (2 bytes)
        *   **ID CRC Check:** The FDC internally calculates a CRC on the (IDAM byte + Track + Head + Sector + Size Code) it just read and compares it to the 2 ID CRC bytes read from the disk.
            *   **Error:** If CRCs don't match -> **CRC Error** status is set (specifically an ID CRC error). The FDC will typically continue searching for the *next* IDAM.
        *   **ID Field Match:** If ID CRC is OK, the FDC compares the Track, Head, and Sector numbers from the disk's ID field with the parameters given in the command.
            *   **Track/Head Mismatch:** If the Track or Head from the ID field doesn't match the *commanded* track/head (especially after a seek), this could indicate a **Seek Error** or that the FDC needs to re-verify its position.
            *   **Sector Mismatch:** If Track and Head match, but the Sector number doesn't, the FDC knows this isn't the requested sector. It ignores this sector's data field and goes back to step 3 (or continues from current position) to search for the *next* IDAM on the same track.
            *   **Sector Match:** If Track, Head, and Sector numbers all match the command parameters, and ID CRC was OK, the FDC has found the correct sector's ID.

5.  **Record Not Found Timeout:**
    *   If the FDC searches the entire track (detects one full revolution by seeing the Index Pulse again) without finding an ID field matching the commanded sector (with a good ID CRC), it sets the **Record Not Found** (or "RNF") status bit and terminates the command.
    *   **Track Bytes:** This covers scanning all ~6250 MFM bytes of a track.

**Phase 3: Data Field Read**

Once the correct sector ID is found and verified:

6.  **Search for Data Address Mark (DAM):**
    *   The FDC continues reading the MFM stream, passing over Gap 2 (the space between the ID field and data field).
    *   It looks for a sequence of **Sync Bytes** (Data Sync, e.g., 12 bytes of 0x00).
    *   Following the Sync, it looks for the **Data Address Mark (DAM)** pattern (e.g., 3 bytes of 0xA1 preamble + 0xFB MFM encoded DAM for normal data, or 0xF8 for deleted data).
    *   **Error - Data Mark Not Found:** If the FDC doesn't find a recognizable DAM within a certain window after the IDAM -> **Lost Data** or **Data AM Not Found** status.
    *   **Error - Wrong Data Mark:** If it finds a DAM but it's for deleted data (e.g., 0xF8) and the command expected normal data (or vice-versa if the FDC supports it) -> Sets a specific status bit like **Record Type** or **DDAM (Deleted Data Address Mark) found**.
    *   **Track Bytes:** This search happens in the bytes immediately following the ID Field and its CRC, after Gap 2.

7.  **Read Sector Data & Transfer to Host:**
    *   **Upon finding the expected DAM (e.g., 0xFB):**
        *   The FDC begins reading the actual sector data bytes. The number of bytes to read is determined by the **Sector Size Code** it found in the *ID field* of this sector (e.g., 256 bytes for code 01).
        *   **MFM to Binary Conversion:** As MFM data is read from the disk, the FDC's internal circuitry decodes it into raw binary bytes.
        *   **Data Transfer (PIO - Programmed I/O):**
            *   For each decoded byte, the FDC places it in its **Data Register**.
            *   It sets the **DRQ (Data Request)** status bit/signal high.
            *   It waits for the host CPU to read the byte from the Data Register.
            *   When the CPU reads, DRQ is cleared. The FDC then fetches and decodes the next byte.
            *   **Error - Lost Data (Host too slow):** If the FDC decodes a new byte and DRQ is *still* set from the previous byte (meaning the CPU didn't read it in time), the old byte is overwritten. The **Lost Data** status is set. The FDC usually continues trying to read the rest of the sector from the disk, but the data transferred to the host will be corrupted.
        *   **Data Transfer (DMA - Direct Memory Access):**
            *   The FDC signals the DMA controller (DMAC) via DRQ.
            *   The DMAC takes control of the bus, reads the byte from the FDC, writes it to RAM, and decrements its count.
            *   This is faster and less CPU-intensive.
        *   **Track Bytes:** The FDC reads and decodes [Sector Size] bytes from the MFM stream immediately following the DAM.

8.  **Read and Verify Data CRC:**
    *   After reading all the data bytes for the sector:
        *   The FDC reads the next 2 bytes from the disk, which are the **Data CRC** bytes.
        *   It internally calculates a CRC on the (DAM byte + all the data bytes it just read/decoded) and compares it to the 2 Data CRC bytes read from the disk.
        *   **Error:** If CRCs don't match -> **CRC Error** status is set (this time, it's a Data CRC error). The data previously transferred to the host (if any made it before the error was known) is likely corrupt.
        *   **Track Bytes:** These are the 2 MFM bytes immediately following the [Sector Size] data bytes.

**Phase 4: Command Termination**

9.  **Set Final Status & Interrupt:**
    *   The FDC clears its `BUSY` status bit.
    *   It updates its **Status Register** with all relevant bits reflecting the outcome (success or specific errors).
    *   It generates an **Interrupt Request (IRQ)** signal to the host CPU, indicating the command has finished.
    *   **When it finishes:** The command operation *on the drive* finishes after the Data CRC bytes are read and checked (or a terminal error occurs before that). The FDC then signals the CPU.

**Common Status Register Bits (e.g., WD1793):**

*   **Bit 7 - Not Ready:** Drive is not ready (e.g., no disk, door open). Command aborts.
*   **Bit 6 - Write Protect:** (Not relevant for read, but set if trying to write to protected disk).
*   **Bit 5 - Head Loaded / (Type I: Record Type/DDAM):** Indicates head is loaded. For Type I commands (Read/Write Sector), this bit can indicate a Deleted Data Address Mark was found.
*   **Bit 4 - Seek Error / (Type I: Record Not Found):**
    *   After Seek/Step: Seek operation failed (couldn't verify track).
    *   After Read/Write Sector: The FDC could not find the ID field of the requested sector after a full rotation.
*   **Bit 3 - CRC Error:** An error occurred in either the ID field CRC or the data field CRC.
*   **Bit 2 - Lost Data / (Track 0 for Seek):**
    *   After Read/Write: CPU didn't service DRQ in time, or Data AM not found.
    *   After Seek/Step: Head is positioned over track 0.
*   **Bit 1 - DRQ (Data Request):**
    *   Read Sector: A byte is available in the Data Register for the CPU to read.
    *   Write Sector: FDC is ready for the CPU to write a byte to the Data Register.
*   **Bit 0 - BUSY:** FDC is currently executing a command. Cleared when command terminates.

**Summary of "Track Byte" Interaction:**

*   **Seek Phase:** No meaningful track byte processing for data/ID.
*   **ID Search Starts:** When FDC begins looking for IDAM Sync after head is on track and settled. The exact byte offset from index is variable.
*   **ID Field Read:** Spans ~7 MFM bytes (IDAM, T, H, S, Size, CRC1, CRC2).
*   **Data Search Starts:** After ID field (and Gap2), FDC looks for DAM Sync.
*   **Data Field Read:** Spans `Sector_Size_In_Bytes` (e.g., 256) MFM-decoded bytes after the DAM.
*   **Data CRC Read:** Spans 2 MFM bytes after the data field.
*   **Command Finishes (drive interaction):** After Data CRC is processed or a fatal error.

The FDC operates as a state machine, reacting to patterns (Sync, AM) in the incoming MFM bitstream rather than counting absolute byte offsets from an index pulse for most of its fine-grained operations within a track. The index pulse is primarily for detecting a full revolution (for RNF errors) and sometimes for formatting.

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