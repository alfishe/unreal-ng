## WD1793 Floppy Disk Controller: Interfacing with the Floppy Disk Drive

### Introduction

The WD1793 Floppy Disk Controller (FDC) acts as the crucial intermediary between the host system and the electromechanical world of the floppy disk drive (FDD). Its FDD interface comprises a set of output signals to control the drive's mechanisms and R/W head, and input signals to receive status and data from the drive. A precise understanding and emulation of these signals are vital for accurately simulating FDD behavior.

### Overview of FDD Interface Signals

The FDD interface can be broadly categorized into control outputs, data transfer lines, and status inputs.

**(Note: Drive Select and Motor On/Off are typically handled by *external* logic, not directly by the WD1793 chip itself. The WD1793 assumes a drive is selected and its motor is running when it needs to perform an operation. We will focus on signals directly controlled or interpreted by the WD1793.)**

### Core FDD Control and Sequencing (Outputs from FDC to FDD)

These signals dictate the physical actions of the FDD.

#### 1. `HLD` (Head Load)

*   **Nature:** Active-high output.
*   **Purpose:** Controls the engagement of the Read/Write head assembly against the disk media. When `HLD` is active (high), the head is loaded (pressed onto the disk). When inactive (low), the head is unloaded.
*   **Activation Flow:**
    *   **Type I Commands (Seek, Step, Restore with `h=1` or `V=1` flags):**
        *   If the `h` (head load) flag in the command is `1`, `HLD` is activated at the *beginning* of the command.
        *   If the `V` (verify) flag is `1` (and `h=0`), `HLD` is activated *towards the end* of the command's stepping phase, just before the head settling delay and verification process begin.
    *   **Type II (Read/Write Sector) & Type III (Read Address, Read/Write Track) Commands:**
        *   `HLD` is activated upon receipt of these commands, typically after any programmed initial delay (controlled by the `E` flag – "15ms delay enable").
*   **Deactivation Flow:**
    *   **Explicit Unload (Type I with `h=0` and `V=0`):** If a Type I command is issued with both `h=0` (don't load at start) and `V=0` (don't verify at end), `HLD` is made inactive (if it was active).
    *   **Automatic Unload (Idle Timeout):** If the FDC is in an idle state (not busy) and 15 Index Pulses (`IP`) have occurred without any new command, the WD179X family typically unloads the head by deactivating `HLD`. This is a power-saving and head-wear-reducing feature.
*   **Emulation Importance:** Critical for simulating head wear (if desired), power consumption, and ensuring operations only occur when the head is actually on the media. Must be coordinated with the `HLT` input.

#### 2. `HLT` (Head Load Timing - Input to FDC from FDD)

*   **Nature:** Active-high input.
*   **Purpose:** This signal is an acknowledgment from the FDD (or external timer logic) that the head has physically settled onto the disk surface after `HLD` was asserted. The FDC waits for `HLT` to go high before proceeding with operations that require head contact (like reading ID fields or data).
*   **Interaction Flow with `HLD`:**
    1.  FDC asserts `HLD` (high).
    2.  External logic (often a one-shot timer triggered by the rising edge of `HLD`) starts a delay (e.g., 30-100ms, drive-dependent head engage time).
    3.  After this delay, the external logic drives `HLT` high.
    4.  The FDC detects `HLT` high and considers the head "engaged."
*   **FDC Behavior:**
    *   During Type I commands with verification, or Type II/III commands, after asserting `HLD` (and any 'E' flag delay), the FDC samples `HLT`. It will wait until `HLT` is high before attempting to read ID fields or data.
    *   The logical "AND" of `HLD` (FDC output) and `HLT` (FDD input) is reflected in the Status Register S5 (Head Loaded) for Type I commands.
*   **Emulation Importance:** Essential for accurately timing the start of read/write operations. The delay simulated by the FDD model before asserting `HLT` is crucial. If `HLT` never goes high, the FDC might stall or timeout certain operations.

#### 3. `STEP`

*   **Nature:** Pulsed output (active high or active low depending on drive, but the WD179X generates pulses). Each pulse commands the FDD to move the head assembly one track.
*   **Purpose:** Used for track-to-track head positioning.
*   **Activation Flow (During Type I Commands: Restore, Seek, Step, Step In, Step Out):**
    *   The FDC generates a sequence of `STEP` pulses. The number of pulses is determined by the command (e.g., one for Step, difference between current and target track for Seek).
    *   The *rate* of these pulses (stepping speed) is determined by the `r0, r1` bits in the Type I command word (e.g., 3ms, 6ms, 10ms, 15ms per step at 2MHz clock).
*   **Emulation Importance:** The FDD model must react to each pulse by updating its internal current track. The timing between pulses (step rate) should be respected for realistic access times.

#### 4. `DIRC` (Direction Select)

*   **Nature:** Level output (static signal).
*   **Purpose:** Specifies the direction of head movement when `STEP` pulses are issued.
*   **Activation Flow (During Type I Commands):**
    *   Set by the FDC *before* the first `STEP` pulse of a sequence.
    *   Conventionally:
        *   `DIRC` High: Step "IN" (towards higher track numbers, away from track 0).
        *   `DIRC` Low: Step "OUT" (towards lower track numbers, towards track 0).
    *   The FDC maintains `DIRC` for the duration of the multi-step operation. For a single "Step" command, it uses the direction from the *previous* step command if not explicitly a "Step In" or "Step Out".
*   **Emulation Importance:** The FDD model uses this in conjunction with `STEP` pulses to correctly determine the new track position.

### Data Transfer and Synchronization

#### 5. `WG` (Write Gate)

*   **Nature:** Active-high output.
*   **Purpose:** Enables the write circuitry in the FDD, allowing data to be written to the disk.
*   **Activation Flow:**
    *   **Write Sector (Type II):** After a matching ID field is found and the host services the *first* `DRQ` for the data field (by writing to the FDC's Data Register), the WD1793 activates `WG` just before writing the Data Address Mark and the subsequent data field.
    *   **Write Track (Type III):** After the host services the *first* `DRQ` (which is issued immediately upon command receipt) and the FDC detects the leading edge of an Index Pulse (`IP`), `WG` is activated to begin writing the entire track.
*   **Deactivation Flow:**
    *   `WG` is deactivated after the last byte of data (and CRC bytes, and any post-amble like 0xFF) has been written for a sector or track.
    *   Immediately if a Write Protect or Write Fault condition is detected.
*   **Emulation Importance:** The FDD model should only allow "writing" of flux transitions when `WG` is active. Crucial for preventing accidental writes and correctly simulating the write process.

#### 6. `WD` (Write Data)

*   **Nature:** Pulsed output. Each pulse represents a flux transition to be recorded on the disk.
*   **Purpose:** Carries the serialized data (including clock bits for FM, or just data for MFM, plus address marks and CRCs) to be written.
*   **Activation Flow (When `WG` is active):**
    *   The FDC generates `WD` pulses based on the data in its internal shift register and the selected density (FM/MFM, controlled by `DDEN` input).
    *   Pulse width: ~500ns for FM, ~250ns for MFM (at 2MHz FDC clock).
    *   The FDC automatically generates unique patterns for Address Marks (IDAM, DAM, Index Mark) and calculates/writes CRC bytes.
*   **Emulation Importance:** The FDD model needs to interpret these pulses to simulate the recording of flux transitions on its virtual media. The timing and pattern of these pulses are critical for data integrity.

#### 7. `RAW READ` (Input to FDC from FDD)

*   **Nature:** Pulsed input. Each pulse (typically negative-going) from the FDD's read electronics signifies a detected flux transition on the disk media.
*   **Purpose:** Carries the raw, unsynchronized stream of flux transition signals from the disk.
*   **FDC Processing Flow (During Read Operations when head is loaded and `RG` would be active):**
    *   The FDC uses this input along with `RCLK` (Read Clock, if provided by the drive, or internally generated/separated by the FDC's data separator) to decode clock and data bits.
    *   The internal data separator synchronizes to Address Marks.
*   **Emulation Importance:** The FDD model must generate these pulses based on the "written" flux patterns on its virtual media and the current head position. The timing accuracy of these pulses is paramount for the FDC to correctly decode data.

#### 8. `RCLK` (Read Clock - Input to FDC from FDD)

*   **Nature:** Clock input, nominally a square wave derived from the data stream.
*   **Purpose:** Provides a timing reference for the FDC's internal data separator to distinguish data bits from clock bits (especially in FM) or to define bit cell windows (MFM).
*   **Source:** Some drives provide `RCLK`. If not, the FDC (or external data separator circuitry interfaced with `WF/VFOE` and `RG`) must generate it from `RAW READ` using a Phase-Locked Loop (PLL) or similar. The WD1793 has internal data separation capabilities.
*   **FDC Usage:** Used during read operations to sample `RAW READ` data. Phasing of `RCLK` transitions relative to `RAW READ` pulses is important, though absolute polarity (high/low) might not be.
*   **Emulation Importance:** If emulating a drive that provides `RCLK`, the FDD model must generate it accurately based on its read speed and the data pattern. If emulating the FDC's internal separator, this input might be bypassed in the model, with the separation logic directly processing `RAW READ`.

#### 9. `WF/VFOE` (Write Fault / VFO Enable)

*   **Nature:** Bi-directional signal.
    *   **As `WF` (Write Fault - Input to FDC):** Active low (or as defined by drive). Signals a problem with the FDD's write process (e.g., no write current detected when `WG` is active).
    *   **As `VFOE` (VFO Enable - Output from FDC):** Active low. Used to enable an external Voltage-Controlled Oscillator (VCO) or PLL data separator, typically during read operations before an address mark is found, to help it lock onto the incoming data stream.
*   **Mode Selection:**
    *   When `WG` is active (FDC is writing): Pin 33 functions as `WF` input.
    *   When `WG` is inactive (FDC is reading or idle): Pin 33 functions as `VFOE` output.
*   **`WF` Input Flow:**
    *   If `WF` becomes active while `WG` is active, the FDC terminates the current write command, sets the Write Fault bit (S5) in the Status Register, and generates an `INTRQ`.
*   **`VFOE` Output Flow:**
    *   `VFOE` goes low (active) during a read operation after the head has loaded (`HLT=1`) and settled, to enable the data separator.
    *   It typically goes high once the FDC's internal separator has locked onto an address mark, or as defined by its specific internal logic (e.g., 1795/7 have different VFOE behavior around ID fields).
*   **Emulation Importance:**
    *   As `WF`: FDD model can assert this to simulate write problems.
    *   As `VFOE`: If emulating an external data separator, this signal would control it. If emulating the WD1793's internal separator, the *logic* of VFOE (when the separator is trying to acquire lock) is what's important.

#### 10. `RG` (Read Gate - Output from FDC, for 1791/3 variants)

*   **Nature:** Active-high output. (Note: The WD179X family datasheet often discusses `RG` in the context of the 1791/1793 which have internal data separators. The 1795/7 might use `VFOE` more extensively for external PLLs).
*   **Purpose:** Signals to external data separator circuitry (if used) or informs the system when the FDC is actively trying to read and synchronize to data from the disk.
*   **Activation Flow:**
    *   During read operations, after the head is loaded and `HLT` is confirmed.
    *   The FDC makes `RG` true when it expects to see preamble bytes (zeros or FFs) before an address mark.
    *   If an address mark isn't found within a certain number of bytes, `RG` might be reset, and the search for preamble restarts.
    *   Once an address mark is found, `RG` typically remains true for the duration of reading the ID field and data field.
*   **Emulation Importance:** Similar to `VFOE`, if emulating an external separator. For the internal separator, this represents an internal state of the read synchronization logic.

### Drive Status and Condition Sensing (Inputs to FDC from FDD)

#### 11. `READY`

*   **Nature:** Level input (active LOW means ready; active HIGH means not ready).
*   **Purpose:** Indicates the general readiness of the selected FDD. When LOW, the drive is ready (motor at speed, disk inserted correctly). Status bit S7 (NOT READY) is the inverted state of this input.
*   **FDC Interpretation:**
    *   **Type I Commands:** Performed regardless of the `READY` state.
    *   **Type II & III Commands:** `READY` must be active *before* the command is initiated. If not, the command is aborted, and an `INTRQ` is generated with the Not Ready status bit (S7) set.
    *   The logical inverse of `READY` contributes to S7 in the Status Register.
*   **Emulation Importance:** The FDD model must provide this status. It can change dynamically (e.g., disk insertion/removal, motor spin-up/down).

#### 12. `IP` (Index Pulse)

*   **Nature:** Pulsed input (typically one pulse per disk revolution, active low from most drives).
*   **Purpose:** Marks the physical beginning of a track.
*   **FDC Interpretation:**
    *   Used to synchronize "whole track" operations like Write Track and Read Track. These commands start/end on an index pulse.
    *   Used for the 15-pulse idle head unload timeout.
    *   Can trigger an interrupt if a Force Interrupt command is programmed with `I2=1`.
    *   Its inverted state is reflected in Status Register S1 during Type I commands.
*   **Emulation Importance:** The FDD model must generate this pulse accurately once per simulated revolution.

#### 13. `TR00` (Track 00 Detect)

*   **Nature:** Level input (active low when head is at track 0).
*   **Purpose:** Indicates that the R/W head is positioned over the outermost track (track 0).
*   **FDC Interpretation:**
    *   **Restore Command:** The FDC issues `STEP` pulses (with `DIRC` set to "out") until `TR00` becomes active.
    *   Prevents stepping "out" beyond track 0. If a step out is attempted while `TR00` is active, the step pulse is usually inhibited, and the Track Register is not decremented.
    *   Its inverted state is reflected in Status Register S2 during Type I commands.
*   **Emulation Importance:** The FDD model must assert this when its simulated head is at track 0.

#### 14. `WPRT` (Write Protect Detect)

*   **Nature:** Level input (active low when a write-protected disk is inserted).
*   **Purpose:** Signals that the inserted diskette is write-protected.
*   **FDC Interpretation:**
    *   If `WPRT` is active when a Write Sector or Write Track command is initiated, the FDC terminates the command immediately, sets the Write Protect bit (S6) in the Status Register, and generates an `INTRQ`.
    *   Its inverted state contributes to S6 during Type I commands.
*   **Emulation Importance:** The FDD model should allow setting a write-protect status for the virtual disk and reflect this on the `WPRT` line.

#### 15. `DDEN` (Double Density Enable)

*   **Nature:** Level input.
*   **Purpose:** Selects the recording density.
    *   `DDEN` High (or open on 1792/4): Single Density (FM encoding).
    *   `DDEN` Low: Double Density (MFM encoding).
*   **FDC Interpretation:** This input directly configures the FDC's internal encoder/decoder logic, data rates, and timings for Address Mark generation/detection, CRC calculation, and `WD` pulse widths.
*   **Emulation Importance:** Must be correctly set by the host system's emulation to match the desired disk format. The FDC's behavior changes significantly based on this pin.

#### 16. `TEST`

*   **Nature:** Level input.
*   **Purpose:** "This input is used for testing purposes only and should be tied to +5V or left open by the user unless interfacing to voice coil actuated motors."
*   **FDC Interpretation:** When active (high), it can alter certain internal timings or behaviors, often related to head settling. For example, if `TEST=0` (low), the 15ms head settling delay for Type I verify operations might be bypassed (zero settling time). This is often used with voice-coil actuators that settle faster.
*   **Emulation Importance:** Usually emulated as tied high (inactive for special test modes) or low if the system design expects the faster/no-settle behavior.

### Error Handling and Recovery via FDD Interface

*   **No `HLT`:** If `HLT` doesn't go high after `HLD` is asserted, the FDC will effectively hang waiting for head engage confirmation for operations requiring it. Emulation might need a timeout.
*   **No `TR00` during Restore:** Results in Seek Error.
*   **`WPRT` during Write:** Results in Write Protect error.
*   **`WF` during Write:** Results in Write Fault error.
*   **`READY` Inactive:** Prevents Type II/III commands, results in Not Ready status.
*   **Data Synchronization Issues (`RAW READ`, `RCLK`):** If the FDD model provides noisy or incorrectly timed `RAW READ` or `RCLK` pulses, the FDC's internal data separator will fail to find address marks or will encounter CRC errors.


### FDD Signals by WD1793 Command Type

The following table outlines how the primary FDD interface signals are utilized by the WD1793 during its main command types and how their states can influence the command's execution and outcome.

*   **Output Signals (from FDC to FDD):** `HLD`, `STEP`, `DIRC`, `WG`, `WD`, `VFOE`/`RG`
*   **Input Signals (from FDD to FDC):** `HLT`, `READY`, `IP`, `TR00`, `WPRT`, `RAW READ`, `RCLK`, `WF`, `DDEN`, `TEST`

| Command Type        | Signal(s) Involved & Role                                                                                                                                                                                                                            | How Signal State Affects Outcome / FDC Behavior                                                                                                                                                                                                                                                                                                                          |
| :------------------ | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Type I: Restore** | `HLD` (if h=1 or V=1), `HLT` (if HLD active), `STEP`, `DIRC` (always low/out), `TR00`                                                                                                                                                                    | **`HLD`/`HLT`**: If `V=1` (Verify), head must load and engage for ID read. <br> **`DIRC`**: Set low to step towards track 0. <br> **`STEP`**: Pulses issued until `TR00` becomes active. <br> **`TR00`**: Must go active (low) to terminate stepping. If not after 255 steps, Seek Error results, INTRQ. <br> **`READY`**: Ignored for command execution. <br> **`IP`**: Used for verify timing if V=1, idle unload. <br> **`TEST`**: If low, may bypass settling delays if V=1. |
| **Type I: Seek**    | `HLD` (if h=1 or V=1), `HLT` (if HLD active), `STEP`, `DIRC`                                                                                                                                                                                             | **`HLD`/`HLT`**: As above for verify. <br> **`DIRC`**: Set high or low based on target vs. current track. <br> **`STEP`**: Pulses issued until Track Register equals Data Register (target track). <br> **`TR00`**: Prevents stepping out beyond track 0. <br> **`READY`**: Ignored. <br> **`IP`**: Used for verify timing if V=1, idle unload. <br> **`TEST`**: As above for verify.                                               |
| **Type I: Step, Step In, Step Out** | `HLD` (if h=1 or V=1), `HLT` (if HLD active), `STEP`, `DIRC`                                                                                                                                                                             | **`HLD`/`HLT`**: As above for verify. <br> **`DIRC`**: Set according to command (In/Out) or previous direction (Step). <br> **`STEP`**: One pulse issued. <br> **`TR00`**: Prevents stepping out beyond track 0 for Step Out/Step. <br> **`READY`**: Ignored. <br> **`IP`**: Used for verify timing if V=1, idle unload. <br> **`TEST`**: As above for verify.                                                              |
| **Type II: Read Sector** | `READY`, `HLD`, `HLT`, `IP` (for sync/timing), `DDEN`, `RAW READ`, `RCLK` (or internal sep.), `VFOE`/`RG`                                                                                                                                                | **`READY`**: Must be active to start; else Not Ready error, INTRQ. <br> **`HLD`/`HLT`**: Head must load and engage. FDC waits for `HLT`. <br> **`IP`**: Can influence multi-sector timing, used for error recovery timeouts. <br> **`DDEN`**: Determines FM/MFM decoding, data rates. <br> **`RAW READ`/`RCLK`**: Provide data stream. Errors lead to CRC Error or RNF. <br> **`VFOE`/`RG`**: Control data separator. <br> **`WPRT`**: Irrelevant. |
| **Type II: Write Sector** | `READY`, `HLD`, `HLT`, `IP` (for sync/timing), `DDEN`, `WG`, `WD`, `WPRT`, `WF`                                                                                                                                                                       | **`READY`**: Must be active to start; else Not Ready error, INTRQ. <br> **`HLD`/`HLT`**: Head must load and engage. FDC waits for `HLT`. <br> **`IP`**: As above. <br> **`DDEN`**: Determines FM/MFM encoding, data rates. <br> **`WG`/`WD`**: Activated to write data. `WG` only after first DRQ for data field serviced. <br> **`WPRT`**: If active, Write Protect error, INTRQ, command terminated. <br> **`WF`**: If active, Write Fault error, INTRQ, command terminated. |
| **Type III: Read Address** | `READY`, `HLD`, `HLT`, `IP`, `DDEN`, `RAW READ`, `RCLK` (or internal sep.), `VFOE`/`RG`                                                                                                                                                           | **`READY`**: Must be active to start; else Not Ready error, INTRQ. <br> **`HLD`/`HLT`**: Head must load and engage. FDC waits for `HLT`. <br> **`IP`**: Used for timeouts if ID field not found. <br> **`DDEN`**: Determines FM/MFM decoding. <br> **`RAW READ`/`RCLK`**: Provide ID field data. Errors lead to CRC Error or RNF. <br> **`VFOE`/`RG`**: Control data separator. |
| **Type III: Read Track** | `READY`, `HLD`, `HLT`, `IP`, `DDEN`, `RAW READ`, `RCLK` (or internal sep.), `VFOE`/`RG`                                                                                                                                                               | **`READY`**: Must be active to start; else Not Ready error, INTRQ. <br> **`HLD`/`HLT`**: Head must load and engage. FDC waits for `HLT`. <br> **`IP`**: Starts and stops the read operation (one full revolution). <br> **`DDEN`**: Determines FM/MFM decoding. <br> **`RAW READ`/`RCLK`**: Provide entire track data. No CRC check by FDC on data field. <br> **`VFOE`/`RG`**: Control data separator. |
| **Type III: Write Track** | `READY`, `HLD`, `HLT`, `IP`, `DDEN`, `WG`, `WD`, `WPRT`, `WF`                                                                                                                                                                                          | **`READY`**: Must be active to start; else Not Ready error, INTRQ. <br> **`HLD`/`HLT`**: Head must load and engage. FDC waits for `HLT`. <br> **`IP`**: Starts and stops the write operation (one full revolution). `WG` activated after first `IP` and first DRQ serviced. <br> **`DDEN`**: Determines FM/MFM encoding. <br> **`WG`/`WD`**: Activated to write data. <br> **`WPRT`**: If active, Write Protect error, INTRQ, command terminated. <br> **`WF`**: If active, Write Fault error, INTRQ, command terminated. |
| **Type IV: Force Interrupt** | `READY`, `IP` (if I0, I1, or I2 flags are set in command)                                                                                                                                                                                          | **`READY` / `IP`**: Their transitions or presence (for `IP`) trigger INTRQ based on I0-I2 flags. Other FDD signals are generally not directly involved in *executing* Force Interrupt, but the FDC state they represent might be part of the *reason* for issuing it. |

*(Note: `DDEN` (Double Density Enable) and `TEST` inputs affect the internal operation and timings for nearly all commands involving data encoding/decoding or head settling, but their primary effect is on *how* the FDC processes data or times delays, rather than being a direct conditional input for a specific command's flow like `TR00` or `WPRT`.)*


### Conclusion

The WD1793's FDD interface is a complex set of signals that must work in concert to achieve reliable disk operations. Emulating this interface requires careful attention to the sequence of activation for control signals like `HLD`, `STEP`, `DIRC`, and `WG`, accurate generation of status inputs like `IP`, `TR00`, `READY`, and `WPRT` by the FDD model, and precise timing/pattern generation for data signals like `WD` and `RAW READ`. Understanding these interactions in a flow-describing manner is key to building a functional FDC and FDD emulation.

---