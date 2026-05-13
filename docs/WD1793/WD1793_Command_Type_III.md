# WD1793 Command: Type III (Track/Format Operations)

## Overview

Type III commands deal with reading raw track data, reading sector identification fields directly, and formatting entire tracks. These are less common in normal file system operations but essential for disk analysis, duplication, and initial formatting.

## Type III Commands

| Command       | Bits [7..4] | Opcode Range | Description                                                                |
| :------------ | :---------- | :----------- | :------------------------------------------------------------------------- |
| READ ADDRESS  | `1100`      | `0xC0-0xCF`  | Find the next ID field and return its contents to the host via DRQ.        |
| READ TRACK    | `1110`      | `0xE0-0xEF`  | Read all data bytes between two consecutive index pulses, including gaps. |
| WRITE TRACK   | `1111`      | `0xF0-0xFF`  | Format an entire track based on data provided byte-by-byte by the host.    |

## Type III Command Flags (Bits 0-3)

| Bit | Name | Value | Description                                                                                             |
| :-: | :--- | :---- | :------------------------------------------------------------------------------------------------------ |
| 0   | `0`  | 0     | Reserved, must be zero.                                                                                 |
| 1   | `0`  | 0     | Reserved, must be zero.                                                                                 |
| 2   | `E`  | 0     | **Delay:** No 15ms/30ms head settling delay before operation. Head engagement relies only on HLT input. |
|     |      | 1     | **Delay:** Introduce a 15ms (2MHz) / 30ms (1MHz) delay after head load, *then* wait for HLT input.      |
| 3   | `S`  | 0/1   | **Side Select:** *This flag seems mislabeled or has limited use in Type III*. Typically ignored by Read/Write Track. May influence Read Address side reporting if used in conjunction with non-standard hardware modifications, but generally not applicable as described for Type II. Assume 0 unless specific drive interface dictates otherwise. Datasheet often shows this bit position as '0'. |

*(Note: Documentation varies on Bit 3's function in Type III. The primary reliable flag is 'E'.)*

---

## Read Address Command (`0xC0-0xCF`) Execution Flow

1.  **Initiation:**
    *   Host writes Read Address command.
    *   Controller samples **/READY**. Terminates if not ready (S7).
    *   Sets **BUSY** (S0=1).
    *   Asserts `HLD`. If `E=1`, waits 15/30ms delay, then waits for `HLT`.

2.  **ID Field Search:**
    *   Scans track for the *next* ID Address Mark (IDAM).
    *   Upon finding IDAM, reads the 6 bytes of the ID field: Track, Side, Sector, Length, CRC1, CRC2.

3.  **Data Transfer:**
    *   Transfers the *first* byte (Track Number) of the ID field to the **Data Register**.
    *   Asserts **DRQ** (signal and S1).
    *   Host reads Data Register.
    *   Controller transfers the *second* byte (Side Number) to Data Register.
    *   Asserts **DRQ**.
    *   ... This continues for all 6 bytes of the ID field.
    *   **Lost Data:** If the host fails to read any byte before the next one is ready (~32/64µs), sets **LOST DATA** (S2). The read continues, but the host misses data.

4.  **CRC Verification (Internal):**
    *   While transferring bytes via DRQ, the controller *internally* calculates the CRC of the first 4 bytes (Track, Side, Sector, Length) and compares it with the read CRC bytes (CRC1, CRC2).
    *   If the CRC check fails, sets **CRC ERROR** (S3).

5.  **Completion:**
    *   After transferring the 6th byte (CRC2) and asserting the final DRQ:
    *   Controller clears **BUSY** (S0).
    *   Asserts **/INTRQ**.
    *   Updates Status Register.

**Status Register (Read Address Completion):**

| Bit | Name            | Meaning                                                      |
| :-: | :-------------- | :----------------------------------------------------------- |
| 7   | NOT READY       | Drive not ready.                                             |
| 6   | WRITE PROTECT   | Reflects /WPRT input.                                        |
| 5   | -               | Not defined (implementation-dependent; typically 0).         |
| 4   | RECORD NOT FOUND| Not Used by Read Address (always 0, ID field assumed found). |
| 3   | CRC ERROR       | CRC of the read ID field was incorrect.                      |
| 2   | LOST DATA       | Host missed reading one or more ID field bytes via DRQ.      |
| 1   | DRQ             | 0 (Last byte read or command ended).                         |
| 0   | BUSY            | 0 (Command completed).                                       |

---

## Read Track Command (`0xE0-0xEF`) Execution Flow

1.  **Initiation:**
    *   Host writes Read Track command.
    *   Controller samples **/READY**. Terminates if not ready (S7).
    *   Sets **BUSY** (S0=1).
    *   Asserts `HLD`. If `E=1`, waits 15/30ms delay, then waits for `HLT`.

2.  **Wait for Index:**
    *   Controller waits for the leading edge of the next index pulse (/IP input goes low).

3.  **Raw Data Transfer:**
    *   Starting immediately after the index pulse edge, the controller reads *every* byte passing under the head.
    *   For each byte assembled from the `RAW READ` input:
        *   Transfers byte to the **Data Register**.
        *   Asserts **DRQ** (signal and S1).
        *   Host must read Data Register within the byte time (~32/64µs).
        *   If host misses DRQ, sets **LOST DATA** (S2).
    *   This continues for one full revolution of the disk. No CRC checks or address mark interpretation is performed.

4.  **Completion (Next Index):**
    *   When the *next* index pulse's leading edge is detected, the command terminates.
    *   Controller clears **BUSY** (S0).
    *   Asserts **/INTRQ**.
    *   Updates Status Register.

**Status Register (Read Track Completion):**

| Bit | Name            | Meaning                                                 |
| :-: | :-------------- | :------------------------------------------------------ |
| 7   | NOT READY       | Drive not ready.                                        |
| 6   | WRITE PROTECT   | Reflects /WPRT input.                                   |
| 5   | -               | Not defined (implementation-dependent; typically 0).    |
| 4   | -               | Not Used.                                               |
| 3   | 0               | Not Used (No CRC checks performed).                     |
| 2   | LOST DATA       | Host missed reading one or more raw track bytes via DRQ. |
| 1   | DRQ             | 0 (Last byte read or command ended).                    |
| 0   | BUSY            | 0 (Command completed).                                  |

---

## Write Track Command (`0xF0-0xFF`) Execution Flow

1.  **Initiation:**
    *   Host writes Write Track command.
    *   Controller samples **/READY**. Terminates if not ready (S7).
    *   Controller samples **/WPRT**. Terminates if write protected (S6).
    *   If OK, sets **BUSY** (S0=1).
    *   Asserts `HLD`. If `E=1`, waits 15/30ms delay, then waits for `HLT`.

2.  **Wait for Index & First DRQ:**
    *   Controller waits for the leading edge of the next index pulse (/IP low).
    *   Immediately after detecting the index pulse, asserts **DRQ** (signal and S1), requesting the *first* byte to be written from the host.
    *   **Critical Timing:** Host must provide the first byte very quickly, before the physical write position moves significantly.
    *   **Write Gate Interlock:** `WG` is asserted *only* if this first DRQ is serviced.

3.  **Raw Data Writing (If first DRQ serviced):**
    *   Asserts `WG` output.
    *   **Data Loop:** For one full disk revolution:
        *   Controller asserts **DRQ**, requesting the next byte.
        *   Host must write the byte to the **Data Register** within the byte time (~32/64µs).
        *   If host is late:
            *   Sets **LOST DATA** (S2).
            *   Writes a byte of `0x00` to the disk. Operation continues.
        *   Controller takes the byte from Data Register (or `0x00` if lost) and writes it to the disk.
        *   **Special Byte Handling:** The controller *interprets* certain byte values written by the host to generate special disk patterns:
            *   `F5`: Write MFM A1* byte (Missing Clock Sync), preset CRC generator.
            *   `F6`: Write MFM C2* byte (Missing Clock Sync).
            *   `F7`: Generate and write 2 CRC bytes based on internal CRC register.
            *   `F8`-`FB`: Write MFM Address Marks (IDAM, DAMs) with correct clock patterns, preset CRC.
            *   `FC`: Write FM Index Address Mark (IAM).
            *   `FD`: Write FM Data Address Mark (DAM).
            *   `FE`: Write FM ID Address Mark (IDAM), preset CRC.
            *   `FF`: Write normal data byte (`FF`) with normal clock.
            *   `00`-`F4`: Write normal data byte with normal clock.
        *   This loop continues, writing data/gaps/marks/CRCs as supplied by the host via DRQ.

4.  **Completion (Next Index):**
    *   When the *next* index pulse's leading edge is detected, the command terminates.
    *   Deactivates `WG` output.
    *   Controller clears **BUSY** (S0).
    *   Asserts **/INTRQ**.
    *   Updates Status Register.

**Status Register (Write Track Completion):**

| Bit | Name            | Meaning                                                     |
| :-: | :-------------- | :---------------------------------------------------------- |
| 7   | NOT READY       | Drive not ready.                                            |
| 6   | WRITE PROTECT   | Write protected (checked at start).                         |
| 5   | WRITE FAULT     | Optional drive fault signal (/WF low) detected during write. |
| 4   | 0               | Not Used.                                                   |
| 3   | 0               | Not Used (Host is responsible for CRC generation via F7).   |
| 2   | LOST DATA       | Host missed providing one or more bytes via DRQ.            |
| 1   | DRQ             | 0 (Controller does not need data).                          |
| 0   | BUSY            | 0 (Command completed).                                      |

---

## Emulation Considerations (Type III)

*   **Read Address:** Find next ID field in image, send 6 bytes via DRQ model, check internal CRC, set status.
*   **Read Track:** Simulate rotation, send *all* bytes between index pulses via DRQ model, detect LOST DATA based on host read timing.
*   **Write Track:** Simulate rotation, manage DRQ for host byte input. Interpret special byte values (`F5`-`FF`) from host to write correct patterns/CRCs to the disk image. Handle LOST DATA by writing `0x00`. Critical timing for first DRQ determines if `WG` is activated.
*   Track internal state accurately (position, BUSY, register contents).
*   Manage HLD/HLT/WG signals appropriately for the emulated drive.