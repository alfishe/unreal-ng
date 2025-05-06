# WD1793 Command: Write Sector (Type II)

## Overview

Command Code: **`101m SPCE a0`** (e.g., `0xA8` for single sector, side 0, 15ms delay, normal DAM)

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