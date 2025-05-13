# WD1793 Command: Type I (Head Positioning)

## Overview

Type I commands are responsible for moving the Read/Write head across the disk surface. They include commands for seeking to a specific track, seeking to track 0 (Restore), and stepping the head incrementally. All Type I commands share common flag bits for controlling head load, verification, and stepping rate.

## Type I Commands

| Command    | Bits [7..4] | Opcode Range | Description                                     |
| :--------- | :---------- | :----------- | :---------------------------------------------- |
| RESTORE    | `0000`      | `0x00-0x0F`  | Seek head to Track 0 (calibrates position).     |
| SEEK       | `0001`      | `0x10-0x1F`  | Seek head to track specified in Data Register.  |
| STEP       | `001u`      | `0x20-0x3F`  | Step head one track in the *last* direction.    |
| STEP IN    | `010u`      | `0x40-0x5F`  | Step head one track towards higher track numbers. |
| STEP OUT   | `011u`      | `0x60-0x7F`  | Step head one track towards track 0.            |

*(Note: `u` refers to the Update flag bit)*

## Type I Command Flags (Bits 0-4)

| Bit | Name     | Value | Description                                                                                                                                |
| :-: | :------- | :---- | :----------------------------------------------------------------------------------------------------------------------------------------- |
| 0-1 | `r1 r0`  | 00-11 | **Stepping Rate:** Selects the time interval between step pulses (e.g., at 1MHz clock: 00=6ms, 01=12ms, 10=20ms, 11=30ms). See Table 1 in datasheet/Timing doc. |
| 2   | `V`      | 0     | **Verify:** No verification after positioning. Command completes after optional settling time.                                               |
|     |          | 1     | **Verify:** After positioning and settling, perform a verification operation on the destination track.                                      |
| 3   | `h`      | 0     | **Head Load:** Do *not* load the head at the start (HLD inactive). If `V=1`, head loads *only* for verification.                             |
|     |          | 1     | **Head Load:** Load the head at the start of the command (HLD active). Head remains loaded until explicitly unloaded or timeout occurs.   |
| 4   | `u`      | 0     | **Update Track Register:** Do *not* update the Track Register during STEP commands. (Applies only to STEP, STEP IN, STEP OUT).             |
|     |          | 1     | **Update Track Register:** Update the Track Register by +/- 1 for each step pulse issued during STEP commands.                            |

## Execution Flow Details

### Common Steps (All Type I)

1.  **Initiation:**
    *   Host loads necessary registers (Data Register for SEEK).
    *   Host writes Type I command byte to Command Register.
    *   Controller sets **BUSY** (Status Bit 0).
    *   If `h=1`, controller asserts `HLD` (Head Load) output immediately.
    *   Controller determines step direction (except for STEP) and required number of steps.

2.  **Stepping Phase:**
    *   Controller sets `DIRC` (Direction) output appropriately.
    *   Controller issues step pulses on the `STEP` output at the rate specified by `r1 r0`.
    *   For STEP commands with `u=1`, the internal **Track Register** is incremented/decremented with each pulse.
    *   For RESTORE, stepping continues until the `/TR00` (Track 00) input becomes active (low).
    *   For SEEK, stepping continues until the **Track Register** matches the **Data Register**.
    *   For STEP variants, only one pulse (or number specified by repetition) is issued.

3.  **Settling Time:**
    *   After the *last* step pulse, if `V=1` (Verify) or if the `E` flag is enabled in Type II/III commands (persists), a fixed head settling delay occurs (e.g., 15ms at 2MHz, 30ms at 1MHz).
    *   *Note:* If the `TEST` pin is grounded, settling time is zero.

4.  **Verification Phase (if `V=1`)**
    *   If `h=0`, the controller asserts `HLD` now.
    *   Controller waits for the `HLT` (Head Load Timing) input to become active (high), indicating the head is engaged. Timeout if HLT doesn't assert.
    *   Controller reads the first encountered ID field from the disk.
    *   It verifies the ID field's CRC.
    *   It compares the Track Number read from the ID field with the contents of the **Track Register**.
    *   **Success:** If Track Numbers match and ID CRC is valid, the verification is complete.
    *   **Failure (Seek Error):** If Track Numbers don't match (but CRC is valid), sets **SEEK ERROR** (Status Bit 4).
    *   **Failure (CRC Error):** If ID CRC is invalid, sets **CRC ERROR** (Status Bit 3) and retries verification on the next ID field.
    *   **Failure (Timeout):** If no valid ID field with a matching track number is found within 5 index pulses (revolutions), sets **SEEK ERROR** (Status Bit 4).

5.  **Completion:**
    *   Controller clears **BUSY** (Status Bit 0).
    *   Controller asserts **/INTRQ**.
    *   Status Register bits are updated:
        *   `S0`: BUSY (cleared)
        *   `S1`: INDEX (latched state of /IP input during command)
        *   `S2`: TRACK0 (latched state of /TR00 input)
        *   `S3`: CRC ERROR (set if ID CRC failed during verify)
        *   `S4`: SEEK ERROR (set if track verify failed or timeout)
        *   `S5`: HEAD LOADED (current state: logical AND of HLD output and HLT input)
        *   `S6`: WRITE PROTECT (inverted state of /WPRT input)
        *   `S7`: NOT READY (inverted state of /READY input OR'd with MR state)

### Specific Command Details

*   **RESTORE:**
    *   Ignores the Data Register.
    *   Steps out until `/TR00` input is low.
    *   Sets the internal Track Register to `0x00`.
    *   If `/TR00` doesn't go low after 255 step pulses, terminates with **SEEK ERROR**.
    *   Executed automatically on **/MR** (Master Reset) going high.
*   **SEEK:**
    *   Requires the target track number to be pre-loaded into the Data Register.
    *   Compares Data Register to Track Register to determine direction and step count.
    *   Updates Track Register internally during stepping (regardless of `u` flag).
*   **STEP / STEP IN / STEP OUT:**
    *   Issue only one step pulse per command execution.
    *   Track Register update depends on the `u` flag.
    *   STEP uses the direction from the *previous* positioning command.

## Status Register (Type I Completion)

| Bit | Name            | Meaning After Type I Command                                  |
| :-: | :-------------- | :------------------------------------------------------------ |
| 7   | NOT READY       | Drive not ready (/READY input high or MR active).             |
| 6   | WRITE PROTECT   | Disk is write-protected (/WPRT input low).                   |
| 5   | HEAD LOADED     | Head is currently loaded (`HLD` high AND `HLT` high).         |
| 4   | SEEK ERROR      | Verification failed (track mismatch/timeout) or Restore failed. |
| 3   | CRC ERROR       | CRC error encountered during ID field verification (`V=1`).   |
| 2   | TRACK 00        | Head is currently over track 0 (/TR00 input low).             |
| 1   | INDEX           | Index pulse occurred during command execution (/IP input low). |
| 0   | BUSY            | 0 (Command completed).                                        |

## Emulation Considerations

*   Maintain internal state for Track Register, Last Step Direction.
*   Model stepping delays based on `r1 r0` and clock speed.
*   Implement the 15ms/30ms settling delay timer.
*   If `V=1`, simulate reading ID fields from the virtual disk image on the target track.
*   Correctly handle `/TR00` input for RESTORE.
*   Update status bits accurately upon completion, including error conditions.
*   Manage the HLD output and HLT input interaction (head load timing).