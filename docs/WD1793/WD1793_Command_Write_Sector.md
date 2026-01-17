# WD1793 Command: Write Sector (Type II)

## Overview

Command Code: **`101m S E C a0`** (Bit 4=m, Bit 3=S, Bit 2=E, Bit 1=C, Bit 0=a0)

Example: `0xA8` = single sector, side 0, 15ms delay, no side compare, normal DAM

The Write Sector command locates a specific sector ID field on the disk and, if found and valid, writes a new data field (including preamble, DAM, data bytes supplied by the host, and CRC) immediately following it. Handshaking with the host for data bytes occurs via the DRQ signal.

## Command Flags (Bits 0-4)

| Bit | Name | Value | Description                                                                                             |
| :-: | :--- | :---- | :------------------------------------------------------------------------------------------------------ |
| 0   | `a0` | 0     | **Address Mark:** Write a normal Data Address Mark (FB).                                                |
|     |      | 1     | **Address Mark:** Write a Deleted Data Address Mark (F8).                                               |
| 1   | `C`  | 0     | **Side Compare:** Disable side comparison. Find sector on the currently selected side.                   |
|     |      | 1     | **Side Compare:** Enable side comparison. The side number in the ID field must match the `S` flag.      |
| 2   | `E`  | 0     | **Delay:** No 15ms/30ms head settling delay before operation. Head engagement relies only on HLT input. |
|     |      | 1     | **Delay:** Introduce a 15ms (2MHz) / 30ms (1MHz) delay after head load, *then* wait for HLT input.      |
| 3   | `S`  | 0     | **Side Select:** If `C=1`, compare for Side 0 in the ID field.                                          |
|     |      | 1     | **Side Select:** If `C=1`, compare for Side 1 in the ID field.                                          |
| 4   | `m`  | 0     | **Multiple Sector:** Write only a single sector specified in the Sector Register.                       |
|     |      | 1     | **Multiple Sector:** Write multiple sectors sequentially, automatically incrementing the Sector Register. |

## Execution flow from another angle

Okay, let's detail the "Write Sector" command for a typical Floppy Disk Controller (FDC), again using the WD17xx (like WD1793) or NEC µPD765 as a reference. The process shares similarities with "Read Sector" but has crucial differences, especially regarding data flow and CRC generation.

**Command Parameters (Typically loaded by CPU into FDC registers):**

1.  **Command Code:** Specific byte for "Write Sector".
2.  **Drive Number:** (0-3) Which drive to use.
3.  **Head Number:** (0 or 1) Which side of the disk.
4.  **Track Number:** The desired physical track on the disk (e.g., 0-39, 0-79).
5.  **Sector Number:** The desired logical sector number on that track (e.g., 1-16).
6.  **Data Byte (for Write Track/Format):** Sometimes a fill byte is specified here if the command is "Write Track" (format), but for "Write Sector," the actual data comes from the host.
7.  **(Implicit) Sector Size:** Often derived from a previous "Specify" command or hardwired.
8.  **(For multi-sector writes) Sector Count:** How many sectors to write.
9.  **(Optional) Deleted Data Address Mark (DDAM) flag:** Some FDCs allow writing a sector with a DDAM (e.g., 0xF8) instead of a normal DAM (0xFB) to mark it as deleted or special.

**FDC "Write Sector" Execution Steps:**

The FDC takes over once the command is issued.

**Phase 1: Preparation and Seek (Identical to Read Sector)**

1.  **Drive & Head Select:**
    *   Activates `DS0-DS3`, `SIDE1` as needed.
    *   Issues `HDL` if required, waits for head load delay.
    *   **Track Bytes:** No data writing yet.

2.  **Seek to Target Track (if not already there):**
    *   Compares current track with target track.
    *   **If different:** Issues `DIRC` and `STEP` pulses, waits for "Head Settle Time."
    *   `BUSY` status bit is set.
    *   A "Verify" operation (reading an ID field) might occur. Failure -> **Seek Error**.
    *   **Track Bytes:** No data writing during seek.

**Phase 2: Sector Search (Finding the ID Field - Identical to Read Sector)**

Once the FDC believes it's on the correct physical track and the head is settled:

3.  **Enable Read Circuits & Search for ID Address Mark (IDAM):**
    *   FDC starts reading the MFM data stream.
    *   Looks for Sync Bytes, then the IDAM pattern (e.g., preamble + 0xFE).
    *   **Track Bytes:** Actively processing read stream from the current track.

4.  **Read and Verify ID Field:**
    *   **Upon finding an IDAM:** Reads Track, Head, Sector, Size Code, ID CRC (6 bytes).
    *   **ID CRC Check:** Calculates CRC on (IDAM byte + T + H + S + Size) and compares with disk's ID CRC.
        *   **Error:** -> **CRC Error** (ID CRC). Continues search.
    *   **ID Field Match:** If ID CRC OK, compares disk's T, H, S with commanded T, H, S.
        *   **Track/Head Mismatch:** -> **Seek Error** or re-verify.
        *   **Sector Mismatch:** Ignores this sector, continues search for next IDAM.
        *   **Sector Match:** If T, H, S match and ID CRC OK, the FDC has found the correct sector's ID field. It now knows where to start writing the data field.

5.  **Record Not Found Timeout:**
    *   If a full revolution occurs without finding the matching ID field (with good ID CRC) -> **Record Not Found** (RNF) status, terminates command.
    *   **Track Bytes:** Scanned all ~6250 MFM bytes of the track.

**Phase 3: Data Field Write (This is where it significantly differs from Read Sector)**

Once the correct sector ID is found and verified:

6.  **Switch to Write Mode & Write Gap 2 (Partial):**
    *   The FDC turns on the drive's "Write Gate" signal, enabling the write head.
    *   It might write a few bytes of Gap 2 pattern (e.g., 0x4E MFM encoded) to ensure proper synchronization and to overwrite the old Gap 2 and start of the old data field. This is to establish a clean MFM clocking environment before writing the critical DAM.
    *   **Track Bytes:** FDC *starts writing* MFM patterns to the disk, beginning in what was Gap 2.

7.  **Write Data Sync and Data Address Mark (DAM):**
    *   The FDC writes the **Data Sync** bytes (e.g., 12 bytes of 0x00 MFM encoded).
    *   Then, it writes the **Data Mark Preamble** (e.g., 3 bytes of 0xA1 MFM encoded).
    *   Finally, it writes the **Data Address Mark (DAM)** itself (e.g., 0xFB MFM encoded for normal data, or 0xF8 if commanded for DDAM).
    *   **CRC Accumulator Start:** The FDC internally initializes its data CRC calculation. The **DAM byte (e.g., 0xFB) is the first byte included in the Data CRC calculation.**
    *   **Track Bytes:** Writes the MFM patterns for Sync and DAM.

8.  **Transfer Data from Host & Write Sector Data:**
    *   The FDC now needs data from the host CPU to write into the sector.
    *   **Data Transfer (PIO - Programmed I/O):**
        *   FDC sets the **DRQ (Data Request)** status bit/signal high, indicating it's ready for a byte.
        *   The host CPU writes a byte of data to the FDC's **Data Register**.
        *   DRQ is cleared by the FDC once it takes the byte.
        *   The FDC takes this raw binary byte, **MFM encodes it**, and writes the MFM pattern to the disk.
        *   **CRC Update:** The FDC **includes this raw binary byte in its ongoing Data CRC calculation.**
        *   This repeats for the number of bytes specified by the **Sector Size Code** found in the ID field (e.g., 256 times for code 01).
        *   **Error - Lost Data (Host too slow):** If the FDC is ready to write the next MFM pattern but DRQ is *still* clear (meaning the CPU hasn't provided the next data byte in time), this is a problem. The FDC might write garbage data or abort. The **Lost Data** status bit is often set.
    *   **Data Transfer (DMA - Direct Memory Access):**
        *   FDC signals DMAC via DRQ.
        *   DMAC takes a byte from RAM, writes it to FDC's Data Register.
        *   FDC MFM encodes it, writes to disk, includes in CRC, and requests next byte via DRQ.
    *   **Track Bytes:** The FDC writes [Sector Size] MFM-encoded bytes received from the host.

9.  **Calculate and Write Data CRC:**
    *   After all sector data bytes have been received from the host, MFM encoded, written to disk, and included in the internal CRC calculation:
        *   The FDC has the final 16-bit **Data CRC** value.
        *   It MFM encodes these two CRC bytes.
        *   It writes these two MFM-encoded CRC bytes to the disk immediately following the sector data.
    *   **Track Bytes:** Writes the 2 MFM-encoded CRC bytes.

10. **Write Gap 3 (Partial):**
    *   After writing the Data CRC, the FDC continues to write a few bytes of the Gap 3 pattern (e.g., 0x4E MFM encoded). This ensures the write operation is properly terminated and provides spacing before the next sector's ID field might begin.
    *   The FDC then deactivates the "Write Gate" signal.
    *   **Track Bytes:** Writes a few more MFM gap bytes.

**Phase 4: Command Termination**

11. **Set Final Status & Interrupt:**
    *   The FDC clears its `BUSY` status bit.
    *   It updates its **Status Register**. Notably, there's no "Data CRC Error" bit to set for a *write* operation in the same way as a read, because the FDC *generated* the CRC. However, other errors like RNF, Seek Error, Write Protect, or Lost Data could have occurred.
    *   It generates an **Interrupt Request (IRQ)** signal to the host CPU.
    *   **When it finishes:** The command operation *on the drive* finishes after the trailing gap bytes are written and Write Gate is turned off.

**CRC Forming Differences (Write vs. Read):**

*   **ID Field CRC:**
    *   **Read:** FDC reads IDAM, T, H, S, Size from disk, reads ID CRC from disk. Calculates CRC on (IDAM+T+H+S+Size) and compares.
    *   **Write:** FDC reads IDAM, T, H, S, Size from disk, reads ID CRC from disk. Calculates CRC on (IDAM+T+H+S+Size) and compares. *This part is the same for identifying the sector.* The FDC does *not* write or modify the ID field during a "Write Sector" command (that's done by a "Format Track" or "Write ID" command).
*   **Data Field CRC:**
    *   **Read:** FDC reads DAM and Data from disk, reads Data CRC from disk. Calculates CRC on (DAM+Data) and compares.
    *   **Write:**
        1.  FDC writes the chosen DAM (e.g., 0xFB) to disk.
        2.  FDC **starts a new CRC calculation** internally, seeding it or including the DAM byte.
        3.  For each data byte received from the host:
            *   FDC MFM encodes and writes the byte to disk.
            *   FDC **updates its internal Data CRC calculation** using the *original binary byte* received from the host.
        4.  After all data bytes are processed, the FDC writes its internally calculated 2-byte Data CRC (MFM encoded) to the disk.

**Common Status Register Bits (e.g., WD1793 - relevant to Write):**

*   **Bit 7 - Not Ready:** Drive not ready. Command aborts.
*   **Bit 6 - Write Protect:** Attempted to write to a write-protected disk. Command aborts, this bit is set.
*   **Bit 5 - Head Loaded / (Type I: Record Type/DDAM):** Head loaded. (This bit might reflect if DDAM was *written* if the FDC has that feedback, but usually, it's more about what was *found* during a read).
*   **Bit 4 - Seek Error / (Type I: Record Not Found):** Seek failed, or target sector ID not found.
*   **Bit 3 - CRC Error:** *During the ID field search phase*, an ID CRC error was detected. (Not applicable to the data field being written, as the FDC generates that CRC).
*   **Bit 2 - Lost Data / (Track 0 for Seek):** CPU didn't supply data byte to FDC via DRQ in time during the data writing phase.
*   **Bit 1 - DRQ (Data Request):**
    *   Write Sector: FDC is ready for the CPU to write the *next* data byte to its Data Register.
*   **Bit 0 - BUSY:** FDC is currently executing a command.

**Summary of "Track Byte" Interaction for Writing:**

*   **Seek Phase:** No track byte writing.
*   **ID Search Phase (Read):** FDC reads MFM stream to find IDAM, reads ID field (T, H, S, Size, ID CRC).
*   **Write Starts:** After ID field is found and Gap 2 begins, FDC turns on Write Gate.
*   **Write Gap 2 (partial), Data Sync, DAM:** FDC writes these MFM patterns.
*   **Write Sector Data:** FDC receives [Sector Size] bytes from host, MFM encodes each, and writes them.
*   **Write Data CRC:** FDC writes its internally calculated 2-byte Data CRC (MFM encoded).
*   **Write Gap 3 (partial):** FDC writes a few more MFM gap bytes.
*   **Write Ends (drive interaction):** After trailing gap and Write Gate is turned off.

The "Write Sector" command is a delicate dance of first finding the right spot by reading, then precisely switching to writing the new data field content along with a freshly calculated CRC.


## Execution Flow

1.  **Initiation:**
    *   Host CPU pre-loads the **Sector Register** with the target sector number.
    *   Host writes the Write Sector command byte to the Command Register.
    *   Controller samples **/READY** input. If inactive, sets **NOT READY** (S7) and terminates.
    *   Controller samples **/WPRT** (Write Protect) input. If active (low), sets **WRITE PROTECT** (S6) and terminates.
    *   If ready and not protected, controller sets **BUSY** (S0=1).
    *   Asserts `HLD` (Head Load) output.
    *   If `E=1`, waits 15ms/30ms settling delay.
    *   Waits for `HLT` (Head Load Timing) input to go high. Times out if HLT remains low.

2.  **ID Field Search & Validation:**
    *   Controller scans the incoming raw data stream for an ID Address Mark (IDAM).
    *   Upon finding an IDAM, reads the ID field contents (Track, Side, Sector, Length, CRC).
    *   **Validation Checks:**
        *   Compares Track Number with internal **Track Register**.
        *   If `C=1`, compares Side Number with command flag `S`.
        *   Compares Sector Number with internal **Sector Register**.
        *   Calculates and verifies the ID field's CRC.
    *   **Mismatch/Error:** If any comparison fails or ID CRC is bad:
        *   If CRC error, sets **CRC ERROR** (S3) temporarily.
        *   Continues searching for the next IDAM.
    *   **Timeout:** If a matching, valid ID field is not found within 5 index pulses (revolutions), sets **RECORD NOT FOUND** (S4) and terminates.

3.  **First DRQ (Data Request) & Write Gate Precondition:**
    *   After finding a valid, matching ID field, the controller waits for a specific gap time (sync gap). In MFM, this is nominally 22 byte times after the ID CRC; in FM, 11 byte times.
    *   At the end of this gap time, the controller asserts **DRQ** (Data Request signal and Status Bit 1), demanding the *first* data byte from the host.
    *   **Critical Timing:** The host *must* write the first data byte to the controller's **Data Register** before the physical position for the start of the data field preamble passes under the head. This deadline is crucial.
    *   **Write Gate Interlock:** The `WG` (Write Gate) output signal, which enables write current in the drive, will *only* be asserted if this first DRQ is serviced by the host in time.

4.  **Write Operation (If first DRQ serviced):**
    *   **Write Gate Activation:** Controller asserts `WG` output.
    *   **Preamble Writing:** Controller writes a sync preamble to the disk (typically 12 bytes of `0x00` in MFM, 6 bytes in FM).
    *   **Data Address Mark (DAM):** Controller writes the DAM (`0xFB` or `0xF8` based on command flag `a0`) along with its unique clock pattern (`A1*` precursor in MFM).
    *   **Data Transfer Loop:** For each subsequent byte of the data field (up to the length specified in the preceding ID field):
        *   Controller asserts **DRQ** (signal and Status Bit 1), requesting the next byte.
        *   **Host Action:** Host must write the byte to the **Data Register** within the per-byte time limit (~32µs in MFM, ~64µs in FM).
        *   **Lost Data:** If the host fails to service DRQ in time:
            *   Controller sets **LOST DATA** (S2).
            *   Controller writes a byte of `0x00` to the disk instead of the intended data.
            *   *Crucially, the write operation continues*, potentially corrupting the rest of the sector.
        *   Controller writes the (received or `0x00`) byte to the disk.
    *   **CRC Writing:** After the last data byte is written, the controller calculates the 2-byte Data Field CRC and writes it to the disk.
    *   **Postamble:** Controller writes a postamble byte (typically `0xFF` in MFM).
    *   **Write Gate Deactivation:** Controller deactivates `WG` output.

5.  **Command Termination (If first DRQ *not* serviced):**
    *   If the host fails to provide the *first* data byte after the initial DRQ assertion (Step 3):
    *   Controller does *not* assert `WG`. No data is written to the disk.
    *   Controller sets **LOST DATA** (S2).
    *   Command terminates immediately.
    *   Controller clears **BUSY** (S0).
    *   Asserts **/INTRQ**.
    *   Updates Status Register.

6.  **Completion / Multi-Sector Handling (after successful write):**
    *   **Single Sector (`m=0`):**
        *   Command terminates.
        *   Controller clears **BUSY** (S0).
        *   Asserts **/INTRQ**.
        *   Updates Status Register (S0-S7).
    *   **Multiple Sector (`m=1`):**
        *   Controller increments its internal **Sector Register**.
        *   If the Sector Register exceeds the limit, sets **RECORD NOT FOUND** (S4) and terminates.
        *   Otherwise, loops back to Step 2 (ID Field Search) for the *next* sector.
        *   The command only terminates when:
            *   An error occurs (RNF, CRC Error, Lost Data, Write Protect).
            *   The Sector Register wraps/reaches limit.
            *   A **Force Interrupt** (Type IV) command is received.

## Status Register (Write Sector Completion)

| Bit | Name            | Meaning After Write Sector Command                                     |
| :-: | :-------------- | :--------------------------------------------------------------------- |
| 7   | NOT READY       | Drive not ready (/READY high or MR active).                            |
| 6   | WRITE PROTECT   | Disk write protected (/WPRT low) - checked at command start.          |
| 5   | WRITE FAULT     | Optional drive fault signal (/WF input low) detected during write.     |
| 4   | RECORD NOT FOUND| Matching ID field not found within timeout.                           |
| 3   | CRC ERROR       | CRC error encountered in the ID field search.                         |
| 2   | LOST DATA       | Host failed to provide data byte (first or subsequent) via DRQ in time. |
| 1   | DRQ             | 0 (Controller does not need data currently).                         |
| 0   | BUSY            | 0 (Command completed).                                                |

Okay, here is the revised and consolidated article for the "Write Sector" command, incorporating all the clarifications and focusing on the key aspects you've highlighted.

---

**FDC "Write Sector" Command: Detailed Operation**

This section put more details on the workings of a "Write Sector" command on a typical Floppy Disk Controller (FDC), such as the Western Digital WD17xx series (e.g., WD1793) or the NEC µPD765. The principles are generally applicable to IBM-compatible MFM floppy operations.

**Command Parameters (Loaded by CPU into FDC registers):**

1.  **Command Code:** A specific byte identifying the "Write Sector" operation.
2.  **Drive Number:** (0-3) Selects the target floppy disk drive.
3.  **Head Number:** (0 or 1) Selects the side of the disk.
4.  **Track Number:** The physical track on the disk to write to (e.g., 0-39, 0-79).
5.  **Sector Number:** The logical sector number on the specified track (e.g., 1-16).
6.  **Data Byte (for Write Track/Format):** Not directly used for the payload of a "Write Sector" command.
7.  **(Implicit) Sector Size:** This is not directly a command parameter but is crucial. The FDC determines the expected sector size from the **ID Field** it reads on the disk for the target sector.
8.  **(For multi-sector writes) Sector Count:** Specifies how many consecutive sectors to write (if supported). For a single sector write, this is effectively 1.
9.  **(Optional) Deleted Data Address Mark (DDAM) flag:** A bit in the command byte or a separate parameter that instructs the FDC to write the sector with a Deleted Data Address Mark (e.g., MFM 0xF8) instead of a normal Data Address Mark (e.g., MFM 0xFB).

**FDC "Write Sector" Execution Steps:**

The FDC manages the entire process once the command and its parameters are loaded by the host CPU.

**Phase 1: Preparation and Seek (Identical to Read Sector)**

1.  **Drive & Head Select:**
    *   The FDC activates the appropriate `DS0-DS3` (Drive Select) line for the target drive.
    *   It activates the `SIDE1` line if head 1 is requested.
    *   If required by the drive and the head is not already loaded, it issues a `HDL` (Head Load) signal and waits for a specific head load delay.
    *   **Track Bytes:** No data is being written to the disk at this stage.

2.  **Seek to Target Track (if not already on it):**
    *   The FDC compares its internal "current track register" with the "target track number" from the command.
    *   **If different:**
        *   The FDC issues `DIRC` (Direction) and `STEP` pulses to the drive's stepper motor to move the read/write head assembly.
        *   After the final step pulse, it waits for a "Head Settle Time" (e.g., 15-30ms) to allow mechanical vibrations to dampen.
        *   The `BUSY` status bit in the FDC's Status Register is set during this operation.
        *   A "Verify" operation might be performed: the FDC attempts to read the first ID field it encounters to confirm it has reached the correct track. If verification fails, a **Seek Error** status is set.
    *   **Track Bytes:** No data writing occurs during the seek itself.

**Phase 2: Sector Search (Finding the ID Field - Identical to Read Sector)**

Once the FDC believes it's on the correct physical track and the head has settled:

3.  **Enable Read Circuits & Search for ID Address Mark (IDAM):**
    *   The FDC begins continuously reading the MFM (Modified Frequency Modulation) data stream coming from the disk's read head.
    *   It looks for a sequence of **Sync Bytes** (e.g., 12 bytes of MFM-encoded 0x00).
    *   Following a successful Sync pattern, it searches for the **ID Address Mark (IDAM)** pattern (e.g., 3 bytes of MFM-encoded 0xA1 preamble, followed by the MFM-encoded 0xFE IDAM byte).
    *   **Track Bytes:** The FDC is actively processing the MFM stream read from the current track, byte by byte, looking for the IDAM.

4.  **Read and Verify ID Field:**
    *   **Upon finding an IDAM:**
        *   The FDC reads the next 6 MFM-encoded bytes from the disk, which represent:
            *   Track Number (from the disk's ID field)
            *   Head Number (from the disk's ID field)
            *   Sector Number (from the disk's ID field)
            *   **Sector Size Code** (e.g., `00`=128B, `01`=256B, `02`=512B, `03`=1024B)
            *   ID CRC (2 bytes)
        *   **ID CRC Check:** The FDC internally calculates a CRC on the (IDAM byte + Track + Head + Sector + Size Code) it just read (using their raw binary values after MFM decoding) and compares this calculated CRC with the 2 ID CRC bytes read from the disk.
            *   **Error:** If the CRCs do not match, a **CRC Error** status is set (specifically an ID CRC error). The FDC will typically continue searching for the *next* IDAM on the track.
        *   **ID Field Match:** If the ID CRC is OK, the FDC compares the Track, Head, and Sector numbers read from the disk's ID field with the parameters given in the "Write Sector" command.
            *   **Track/Head Mismatch:** If the Track or Head from the ID field doesn't match the *commanded* track/head (especially critical after a seek), this could indicate a **Seek Error**, or the FDC might need to re-verify its position.
            *   **Sector Mismatch:** If Track and Head match, but the Sector number from the ID field does not match the commanded sector, the FDC knows this isn't the sector it's supposed to write to. It ignores this sector's data field and goes back to step 3 (or continues from its current position on the track) to search for the *next* IDAM.
            *   **Sector Match:** If the Track, Head, and Sector numbers all match the command parameters, and the ID CRC was OK, the FDC has successfully located the ID field of the target sector. **Crucially, it now knows the Sector Size Code for this specific sector and will use it to determine how many data bytes to expect from the host.**

5.  **Record Not Found Timeout:**
    *   If the FDC searches the entire track (detects one full revolution by seeing the Index Pulse again) without finding an ID field that matches the commanded sector (and has a good ID CRC), it sets the **Record Not Found** (or "RNF") status bit and terminates the command.
    *   **Track Bytes:** This involves scanning all approximately 6250 MFM bytes of a standard track.

**Phase 3: Data Field Write**

Once the correct sector ID is found and verified:

6.  **Switch to Write Mode & Write Gap 2 (Partial):**
    *   The FDC asserts the drive's "Write Gate" signal, enabling the write head and disabling the read circuits.
    *   It typically writes a few bytes of the Gap 2 pattern (e.g., MFM-encoded 0x4E) onto the disk. This serves to overwrite the end of the old Gap 2 and the beginning of the old data field, ensuring a clean MFM clocking environment and a well-defined start for the new data field.
    *   **Track Bytes:** The FDC *begins writing* MFM patterns to the disk, starting in what was previously Gap 2.

7.  **Write Data Sync and Data Address Mark (DAM):**
    *   The FDC writes a sequence of **Data Sync** bytes (e.g., 12 bytes of MFM-encoded 0x00).
    *   Following the sync, it writes the **Data Mark Preamble** (e.g., 3 bytes of MFM-encoded 0xA1).
    *   Finally, it writes the **Data Address Mark (DAM)** itself. This will be an MFM-encoded 0xFB for normal data or an MFM-encoded 0xF8 if the DDAM flag was set in the command.
    *   **Track Bytes:** The FDC writes the MFM patterns for the Data Sync, Preamble, and DAM.

8.  **Data CRC Calculation - Initialization Trigger:**
    *   **The act of the FDC preparing to write (or notionally writing) the DAM byte is the trigger for it to initialize its internal Data CRC generator.**
    *   The CRC register within the FDC is typically initialized to 0xFFFF (for CRC-CCITT).
    *   The **raw binary value of the DAM byte (e.g., 0xFB or 0xF8, *not* its MFM encoded form on disk)** is the very first byte that is processed by the FDC's internal CRC calculation logic.

9.  **Transfer Data from Host, MFM Encode, Write Data, and Update CRC:**
    *   The FDC now needs the actual data from the host CPU to write into the sector.
    *   It maintains an **internal byte counter**, initialized based on the **Sector Size Code** read from the ID Field (e.g., to 256 for a 256-byte sector).
    *   For each byte of the sector data:
        *   **Data Request (DRQ):** The FDC sets the **DRQ** status bit (and/or asserts a DRQ signal) high, indicating to the host CPU that it is ready to receive one byte of data.
        *   **Host Sends Byte:** The host CPU writes one **raw binary data byte** (the actual logical content, 0x00-0xFF) into the FDC's **Data Register**.
        *   **DRQ Cleared:** The FDC clears DRQ once it has taken the byte from the Data Register.
        *   **MFM Encoding (FDC Internal):** The FDC takes this **raw binary byte** from its Data Register and, using its internal MFM encoding logic, converts it into the corresponding 16-bit MFM pattern (a sequence of clock and data flux transitions).
        *   **Write to Disk:** The FDC writes this **MFM encoded pattern** to the disk.
        *   **Data CRC Update:** Simultaneously, the FDC uses the **original raw binary byte** (the one received from the host *before* MFM encoding) to update its ongoing internal Data CRC calculation.
        *   **Byte Counter Update:** The FDC decrements its internal byte counter.
        *   **Error - Lost Data (Host too slow):** If the FDC is ready to write the next MFM pattern to disk (having processed the previous byte) but the DRQ is *still* clear (meaning the host CPU has not yet provided the next data byte to the Data Register), a **Lost Data** error occurs. The FDC will set the Lost Data status bit. The data written to disk from this point onward in the sector will likely be incorrect or garbage MFM patterns.

10. **Data CRC Calculation - Finalization and Write Trigger (Byte Count Reached):**
    *   The FDC continues the loop in step 9 (set DRQ, get byte from host, update CRC, MFM encode, write MFM, decrement counter) until its **internal byte counter reaches zero.** This signifies that all data bytes for the sector (as determined by the Sector Size Code from the ID Field) have been received from the host and processed.
    *   **This completion of the data byte count is the trigger for the FDC to finalize the CRC and write it to disk.**
    *   At this point:
        *   The FDC will not set DRQ again to request more data bytes from the host for *this current sector operation*.
        *   Its internal Data CRC calculation (which has now processed the DAM + all the raw data bytes from the host) is complete and holds the final 16-bit Data CRC value.
    *   The FDC takes these two CRC bytes (typically high byte first, then low byte).
    *   It **MFM encodes each of these two CRC bytes individually.**
    *   It writes these two MFM-encoded CRC byte patterns to the disk immediately following the MFM pattern of the last data byte.
    *   **Track Bytes:** The FDC writes the 2 MFM-encoded CRC byte patterns.

11. **Write Gap 3 (Partial):**
    *   After successfully writing the Data CRC, the FDC continues to write a few bytes of the Gap 3 pattern (e.g., MFM-encoded 0x4E) onto the disk. This ensures that the write operation is properly terminated, provides necessary spacing before the next sector's ID field might begin, and helps maintain MFM clock synchronization.
    *   The FDC then deactivates the "Write Gate" signal, stopping the write current to the head.
    *   **Track Bytes:** The FDC writes a few more MFM gap bytes.

**Phase 4: Command Termination**

12. **Set Final Status & Interrupt:**
    *   The FDC clears its `BUSY` status bit in the Status Register.
    *   It updates its **Status Register** with all relevant bits reflecting the outcome of the command (e.g., success, or errors like RNF, Seek Error, Write Protect, Lost Data).
    *   It generates an **Interrupt Request (IRQ)** signal to the host CPU, indicating that the "Write Sector" command has finished.

**Difference: Host Data vs. Disk Data & CRC Context**

*   **Data Sent by Host to FDC (via Data Register):**
    *   This is **raw, unencoded binary data**. These are the logical bytes (0-255) that constitute the intended file content, directory entry, system information, etc.
    *   Example: To write the ASCII character 'Z' (hex 0x5A), the host sends the single byte `0x5A` to the FDC's Data Register.

*   **Data Written by FDC to Disk:**
    *   This is **MFM-encoded data**. Each raw binary byte received from the host is transformed by the FDC into a more complex MFM pattern. This pattern is a specific sequence of magnetic flux transitions (or lack thereof) designed for reliable storage and clock recovery from the disk surface.
    *   Example: The raw byte `0x5A` would be converted by the FDC into its corresponding MFM pattern before being written.

*   **Data Used for CRC Calculation by FDC (during Write Sector):**
    *   The FDC uses the **raw, unencoded binary data** for its internal CRC calculation.
    *   The sequence of (raw binary) bytes included in the Data CRC calculation is:
        1.  The **raw binary value of the Data Address Mark** (e.g., `0xFB` for normal data, `0xF8` for deleted data).
        2.  All the **raw binary data bytes** received from the host for that sector (e.g., 256 of them if the sector size is 256 bytes).
    *   The FDC does *not* use the MFM-encoded patterns for its CRC calculation. It operates on the logical data that these MFM patterns represent. This ensures that the CRC check during a subsequent read operation (which also works on decoded raw binary data) is valid.

**Common Status Register Bits (e.g., WD1793 - relevant to Write):**

*   **Bit 7 - Not Ready:** Drive is not ready (e.g., no disk, door open). Command aborts.
*   **Bit 6 - Write Protect:** An attempt was made to write to a write-protected disk. Command aborts, and this bit is set.
*   **Bit 5 - Head Loaded:** Indicates the head is loaded onto the disk surface.
*   **Bit 4 - Seek Error / (Type I: Record Not Found):**
    *   After Seek/Step: The seek operation failed (couldn't verify being on the correct track).
    *   After "Write Sector" (during ID search phase): The FDC could not find the ID field of the requested sector after a full track revolution.
*   **Bit 3 - CRC Error:** *During the ID field search phase only*, an error was detected in the ID field's CRC read from the disk. (This bit is not set for any issues related to the data field being *written*, as the FDC generates that CRC itself).
*   **Bit 2 - Lost Data:** The host CPU did not supply a data byte to the FDC's Data Register via DRQ in time during the data writing phase.
*   **Bit 1 - DRQ (Data Request):**
    *   Indicates that the FDC is ready for the host CPU to write the *next* data byte to its Data Register for the current sector.
*   **Bit 0 - BUSY:** The FDC is currently executing a command. This bit is cleared when the command terminates (either successfully or due to an error).

This detailed process shows how the FDC handles the complexities of locating the correct physical spot on the disk, managing the data flow from the host, performing the necessary MFM encoding, calculating and appending the CRC, and writing the information reliably to the magnetic media.

## Emulation Considerations

*   Requires write capability to the disk image representation.
*   Simulate ID field search and validation as in Read Sector.
*   Model the 22/11 byte gap timing after ID CRC.
*   Implement the DRQ logic for the *first* byte carefully – this gates the entire write operation (`WG`).
*   Model the per-byte DRQ timing (~32/64µs).
*   If the host emulator writes to the Data Register too late:
    *   Set the LOST DATA flag.
    *   If it was the *first* DRQ, abort the write entirely.
    *   If it was a subsequent DRQ, write `0x00` to the image and continue.
*   Simulate writing the preamble, DAM, data bytes, calculated CRC, and postamble to the image.
*   Handle multi-sector logic (`m=1`) by incrementing the Sector Register and looping.
*   Accurately update all status bits, paying close attention to how LOST DATA affects the outcome.
*   Monitor the /WPRT input state at the start. Monitor /WF input during the `WG`-active phase.