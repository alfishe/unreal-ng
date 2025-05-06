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