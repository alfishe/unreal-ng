## WD1793 Floppy Disk Controller: Interaction with the Host Processor

### Introduction

The Western Digital WD1793 is a versatile Floppy Disk Controller (FDC) chip that bridges the gap between a host microprocessor and a floppy disk drive. Understanding its interface with the host is crucial for both system design and software development (including emulation). This interaction primarily revolves around a set of registers accessible to the host and two key handshake signals: Interrupt Request (`INTRQ`) and Data Request (`DRQ`).

### Overview of Host Interface

The WD1793 communicates with the host processor through an 8-bit bi-directional data bus (`DAL0-DAL7` for WD1793, which uses true data) and several control lines. The host accesses the FDC's internal registers by asserting Chip Select (`CS`), Read Enable (`RE`) or Write Enable (`WE`), and providing the appropriate register address on lines `A0` and `A1`.

The primary registers involved in host interaction are:

*   **Command Register (Write-Only):** Used to issue commands to the FDC.
*   **Status Register (Read-Only):** Provides information about the FDC's current state and the result of the last command.
*   **Track Register (Read/Write):** Holds the current track number of the R/W head.
*   **Sector Register (Read/Write):** Holds the desired sector number for an operation.
*   **Data Register (Read/Write):** A buffer for single-byte data transfers between the host and the FDC during read/write operations.

### Key Handshake Signals: INTRQ and DRQ

These two signals are fundamental to the asynchronous, interrupt-driven operation of the WD1793 Floppy Disk Controller with the host CPU. Their proper handling is essential for reliable data transfer.

#### INTRQ (Interrupt Request)

*   **Nature:** Active-low, open-drain output. It requires an external pull-up resistor.
*   **Purpose:** `INTRQ` alerts the host processor that the FDC requires attention, typically because a command has finished or a specific pre-programmed condition has been met.
*   **Assertion (INTRQ goes Low):**
    1.  **Command Completion (Type I, II, III):**
        *   **General Principle:** "At the completion of every command an INTRQ is generated." This applies whether the command completes successfully or encounters an error.
        *   **Type I (Restore, Seek, Step, Step In, Step Out):**
            *   Asserted when head positioning finishes. If verification (V=1 flag) is enabled, this includes the head settling delay (e.g., 15ms) and the subsequent ID field verification.
            *   *Error Conditions Triggering INTRQ:*
                *   During RESTORE: If the `TR00` input doesn't activate after 255 step pulses (Seek Error status).
                *   During Verification: If a valid ID field (correct track and CRC) isn't found after approximately 4-5 disk revolutions (Seek Error status).
        *   **Type II (Read Sector, Write Sector) & Type III (Read Address, Read Track, Write Track):**
            *   Asserted when the entire requested operation (e.g., read/write of specified sector(s), entire track, or address field) concludes.
            *   *Error Conditions Triggering INTRQ:*
                *   *Record Not Found (RNF):* If a matching ID field (correct track, sector, side, and CRC) isn't located within 4-5 revolutions. Also, if a Data Address Mark isn't found within the expected window after an ID field during Read Sector. During multi-sector operations, if the internal sector count exceeds the track capacity.
                *   *CRC Error:* If a CRC mismatch occurs in an ID field or a Data field.
                *   *Write Protect:* If a write is attempted on a disk where the `WPRT` input is active. The command is immediately terminated.
                *   *Write Fault:* If the `WF` input (from drive electronics) activates during a write. The command is immediately terminated.
                *   *Lost Data:* If the host fails to service `DRQ` in time during data transfer phases of Read/Write Sector/Track. The command usually completes the current sector (writing zeros for missed bytes in a write operation), then terminates with Lost Data status.
    2.  **Force Interrupt Command (Type IV):**
        *   `INTRQ` is asserted when a condition programmed via the `I0-I3` flags in the Force Interrupt command is met:
            *   `I0=1`: Not-Ready-to-Ready transition of the `READY` line.
            *   `I1=1`: Ready-to-Not-Ready transition of the `READY` line.
            *   `I2=1`: Every active Index Pulse (`IP` input).
            *   `I3=1`: Immediate Interrupt. `INTRQ` is asserted as soon as the command is processed.
        *   If a Force Interrupt command is issued while another command is active (Busy=1), the ongoing command is terminated first, and then `INTRQ` is generated based on the `I0-I3` conditions.
    3.  **Master Reset (MR) Completion:**
        *   When the `MR` input transitions from low to high, an implicit RESTORE (Type I) command is executed by the FDC. `INTRQ` is asserted upon the completion of this internally generated RESTORE command.
    4.  **Initial `READY` State for Data Transfer Commands:**
        *   If the `READY` input from the FDD is inactive (drive not ready) *at the moment* a Type II or Type III command is issued, the command is not executed, and an `INTRQ` is generated immediately (with Not Ready status).
*   **De-assertion (INTRQ goes High/Inactive):**
    *   `INTRQ` is reset by the host processor performing either of these actions:
        1.  Reading the FDC's Status Register.
        2.  Writing a new command to the FDC's Command Register.
    *   *Note on Force Interrupt I3=1:* The datasheet suggests a specific reset mechanism for an interrupt generated by `I3=1`, emphasizing it clears on a *subsequent* Command Load or Status Read.

#### DRQ (Data Request)

*   **Nature:** Active-high output.
*   **Purpose:** `DRQ` informs the host that the FDC's 8-bit Data Register is involved in a data exchange: either it holds a byte read from the disk that the host needs to take, or it's ready to receive a byte from the host to be written to the disk. `DRQ` is only relevant when the FDC is `Busy` with a Type II or Type III command.
*   **Assertion (DRQ goes High):**
    1.  **Read Operations (Read Sector, Read Track, Read Address - Type II & III):**
        *   `DRQ` is asserted each time a byte is successfully assembled from the serial `RAW READ` input and transferred into the Data Register. This applies to each byte of an ID field (during Read Address) or a data field (during Read Sector, Read Track).
    2.  **Write Operations (Write Sector, Write Track - Type II & III):**
        *   `DRQ` is asserted when the FDC has moved the previous byte from the Data Register to its internal serializer (Data Shift Register) and is now ready for the *next* byte from the host.
        *   **Write Track Specifics:** `DRQ` is asserted *immediately* upon receipt of the Write Track command to request the very first byte. Writing to the disk (activating `WG`) only commences after the host provides this first byte.
        *   **Write Sector Specifics (First Data Byte):** After a matching ID field is found and verified, `DRQ` is asserted to request the first byte of the data field from the host. The Write Gate (`WG`) output is *not* activated until the host services this `DRQ` by writing to the Data Register. This is a safeguard against accidental writes.
*   **De-assertion (DRQ goes Low/Inactive):**
    *   `DRQ` is reset by the host processor performing the corresponding action:
        1.  Reading from the Data Register (for FDC read operations).
        2.  Writing to the Data Register (for FDC write operations).
    *   **Lost Data Condition:** If the host fails to service `DRQ` within one byte-time:
        *   *During Read:* The byte in the Data Register is overwritten by the next incoming byte; the original byte is lost. The Lost Data bit (S2) is set in the Status Register.
        *   *During Write:* The FDC writes a byte of zeros (0x00) to the disk for that byte-time. The Lost Data bit (S2) is set. The write operation typically continues for the remainder of the sector.


**Summary Table for when DRQ/INTRQ are SET ACTIVE:**

| Signal      | Condition                                                                                                | Command Types Applicable        | Notes                                                                 |
| :---------- | :------------------------------------------------------------------------------------------------------- | :------------------------------ | :-------------------------------------------------------------------- |
| **INTRQ**   | **Command Completion (Successful)**                                                                        | Type I, II, III                 |                                                                       |
|             | **Command Completion (Error):**                                                                          |                                 |                                                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;Seek Error (Restore/Verify failure)                                                | Type I                          |                                                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;Record Not Found (ID/Data Mark search fail, sector overflow)                       | Type II, III                    |                                                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;CRC Error (in ID or Data field)                                                   | Type I (Verify), II, III        |                                                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;Write Protect violation                                                          | Type II (Write), III (Write)    | Command terminated immediately.                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;Write Fault (external signal)                                                    | Type II (Write), III (Write)    | Command terminated immediately.                                       |
|             | &nbsp;&nbsp;&nbsp;&nbsp;Lost Data (DRQ not serviced)                                                     | Type II, III                    | Command often completes sector/operation before termination.          |
|             | **Force Interrupt Condition Met** (as per I0-I3 flags in Type IV cmd)                                    | Type IV                         | Can terminate an active command.                                      |
|             | **Master Reset (MR) Completion** (Implicit RESTORE command finishes)                                     | N/A (Internal FDC action)       | RESTORE is Type I.                                                    |
|             | **`READY` Input Inactive** at start of data transfer command                                             | Type II, III                    | Command not executed.                                                 |
| **DRQ**     | **Byte Assembled in Data Register** (FDC Read op, ready for host)                                          | Read Sector, Read Track, Read Address (Type II, III) | For each byte of ID or Data.                                        |
|             | **Data Register Emptied to Serializer** (FDC Write op, ready for next host byte)                         | Write Sector, Write Track (Type II, III) |                                                                       |
|             | **Immediately Upon Write Track Command Receipt** (for the *first* byte)                                  | Write Track (Type III)          | `WG` inactive until first byte loaded.                                |
|             | **After ID Field Match During Write Sector** (for the *first* data byte)                                 | Write Sector (Type II)          | `WG` inactive until this DRQ serviced.                                |

---

## Other Host-Interface Signals and Registers

While `INTRQ` and `DRQ` are the primary handshake signals, the host also interacts via control lines and direct register access.

#### 1. Control Inputs from Host to FDC:

*   **`CS` (Chip Select - Active Low):** Enables the FDC for communication. When `CS` is high, the FDC typically ignores other interface lines (`RE`, `WE`, `A0`, `A1`).
*   **`RE` (Read Enable - Active Low):** When asserted (along with `CS`), allows the host to read from the FDC register selected by `A0` and `A1`.
*   **`WE` (Write Enable - Active Low):** When asserted (along with `CS`), allows the host to write to the FDC register selected by `A0` and `A1`.
*   **`A0`, `A1` (Address Lines):** Select one of the four internal register types:
    | A1 | A0 | Read Operation (`RE` low) | Write Operation (`WE` low) |
    |----|----|---------------------------|----------------------------|
    | 0  | 0  | Status Register           | Command Register           |
    | 0  | 1  | Track Register            | Track Register             |
    | 1  | 0  | Sector Register           | Sector Register            |
    | 1  | 1  | Data Register             | Data Register              |
*   **`MR` (Master Reset - Active Low):**
    *   **Effect of MR Low:** Resets the FDC. The Command Register is loaded with `HEX 03`. The Not Ready bit (S7) in the Status Register is effectively forced to indicate "Not Ready" due to the reset state. Internal operations are halted.
    *   **Effect of MR Transitioning Low to High:** The Sector Register is loaded with `HEX 01`. Crucially, a **RESTORE (Type I) command is automatically executed**. This command seeks the R/W head to track 0. Upon completion of this implicit RESTORE, an `INTRQ` will be generated.
    *   *Host Interaction during MR:* While MR is low, the FDC is in a reset state. The host should wait for MR to go high and for the subsequent RESTORE command to complete (signaled by `INTRQ`) before issuing new commands.

#### 2. Data Bus (`DAL0-DAL7` for WD1793):

*   This 8-bit bus is used for transferring command bytes, status bytes, register values, and data bytes between the host and the FDC. The WD1793 uses a true (non-inverted) data bus.

### Corner Cases and Error Handling through Status Register

The Status Register is paramount for the host to understand the outcome of operations and any errors. The meaning of its bits can vary slightly depending on the type of command last executed.

*   **Busy (S0):** `1` = FDC is executing a command; `0` = FDC is idle.
    *   The host should generally only write to the Command Register when Busy is `0`, with the exception of the Force Interrupt command.
*   **Varying Bits (S1, S2, S4, S5, S6):**
    *   **Type I status:** S1=Index, S2=Track 00, S4=Seek Error, S5=Head Loaded, S6=Protected.
    *   **Type II/III status:** S1=Data Request (copy of DRQ), S2=Lost Data, S4=Record Not Found, S5=Record Type/Write Fault, S6=Write Protect (for write ops).
*   **Error Handling:** The host CPU, typically in its interrupt service routine triggered by `INTRQ`, reads the Status Register to determine if the command was successful or if an error occurred (CRC Error, Seek Error, RNF, Lost Data, Write Protect, Write Fault). Based on these bits, the host software decides on recovery actions or reporting the error.

### Conclusion

The host interface of the WD1793, centered around its registers and the `INTRQ`/`DRQ` handshake signals, provides a flexible mechanism for controlling floppy disk operations. Careful management of these signals and interpretation of the Status Register by the host software are essential for reliable data storage and retrieval. Emulating this interaction accurately requires attention to the specific conditions under which these signals change state and how different commands influence the FDC's internal state and its communication with the host.

---


