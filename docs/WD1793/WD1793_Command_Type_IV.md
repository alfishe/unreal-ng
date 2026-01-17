# WD1793 Command: Type IV (Force Interrupt)

## Overview

Command Code: **`1101 I3 I2 I1 I0`** (Range `0xD0` - `0xDF`)

The Force Interrupt command provides a mechanism to immediately terminate any currently executing command (Type I, II, or III) or to set up conditions under which an interrupt should be generated even if the controller is idle. It is the only command that can be issued while the **BUSY** status bit is set.

## Command Flags (Bits 0-3)

These flags define the condition(s) that will trigger an interrupt. Multiple conditions can be enabled simultaneously. The interrupt occurs when *any* enabled condition becomes true.

| Bit | Name | Value | Description                                                                                                                              |
| :-: | :--- | :---- | :--------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | `I0` | 0     | Disable interrupt on Not Ready-to-Ready transition.                                                                                      |
|     |      | 1     | **Enable Interrupt:** Trigger **/INTRQ** when the **/READY** input signal transitions from inactive (high) to active (low).                |
| 1   | `I1` | 0     | Disable interrupt on Ready-to-Not Ready transition.                                                                                      |
|     |      | 1     | **Enable Interrupt:** Trigger **/INTRQ** when the **/READY** input signal transitions from active (low) to inactive (high).                |
| 2   | `I2` | 0     | Disable interrupt on Index Pulse.                                                                                                        |
|     |      | 1     | **Enable Interrupt:** Trigger **/INTRQ** on the leading edge (low transition) of *every* **/IP** (Index Pulse) input signal.                |
| 3   | `I3` | 0     | Disable immediate interrupt.                                                                                                             |
|     |      | 1     | **Enable Interrupt:** Trigger **/INTRQ** immediately upon loading this command. This also forces termination of any active command.      |

**Special Case: `0xD0` (`I3 I2 I1 I0 = 0000`)**

If the command `0xD0` is issued (all interrupt condition flags are 0), it has a unique effect:
*   If a command is currently executing (**BUSY**=1), that command is terminated immediately.
*   The **BUSY** status bit is cleared (S0=0).
*   **No interrupt (/INTRQ) is generated.** This is the only way to abort a command *without* getting an interrupt.

## Execution Flow

1.  **Initiation:**
    *   Host writes the Force Interrupt command byte (`0xD0` - `0xDF`) to the Command Register. This can be done even if **BUSY** (S0) is set.

2.  **Immediate Termination (If BUSY=1):**
    *   If any Type I, II, or III command was executing, it is immediately stopped.
    *   Any associated actions (stepping, writing, reading) cease.
    *   Outputs like `STEP`, `WG` are typically deasserted. `HLD` may remain active depending on prior state and head unload logic.
    *   The **BUSY** status bit (S0) is cleared.

3.  **Interrupt Logic:**
    *   **If `I3=1` (Immediate Interrupt):** **/INTRQ** is asserted immediately.
    *   **If Command `0xD0` (All Flags = 0):** No interrupt is generated. The command effectively just terminates the current operation silently.
    *   **If `I0`, `I1`, or `I2` = 1 (Conditional Interrupts):** The controller now monitors the specified input lines (/READY, /IP). When a transition matching an enabled condition occurs, **/INTRQ** is asserted. These conditions remain active until the next command (other than Force Interrupt) is issued.

4.  **Status Register Update:**
    *   When Force Interrupt is issued *while another command is BUSY*: The Status Register bits (S1-S7) generally retain the state they were in *at the moment of termination*. The only guaranteed change is that **BUSY** (S0) becomes 0.
    *   When Force Interrupt is issued *while IDLE* (BUSY=0): The Status Register reflects the *current* state of the drive interface signals, similar to the status after a Type I command completion (S0=0, S1=Index, S2=Track0, S3=0, S4=0, S5=Head Loaded, S6=Write Protect, S7=Not Ready).

## Use Cases

*   **Aborting Operations:** Terminate long operations like Write Track or multi-sector reads/writes if an error occurs elsewhere in the system or if user cancels. `0xD0` is useful for a silent abort.
*   **Synchronization:** Use `I2=1` to receive an interrupt on every index pulse, useful for timing or sector counting in software.
*   **Drive Status Change Notification:** Use `I0=1` or `I1=1` to get an interrupt if the drive becomes ready or not ready (e.g., disk insertion/removal detection, motor spin-up completion).
*   **Immediate Attention:** Use `I3=1` to force the CPU's attention via an interrupt, often used in error recovery routines.

## Emulation Considerations

*   The emulator must allow writing to the Command Register even when its internal BUSY state is true, *if* the command is Type IV.
*   If a command is active, the emulator must halt its execution immediately upon receiving Force Interrupt.
*   Update the BUSY state correctly (cleared unless setting up conditional interrupts while idle).
*   Implement the interrupt generation logic:
    *   Immediate trigger for `I3=1`.
    *   No trigger for `0xD0`.
    *   Edge detection on emulated /READY and /IP lines if `I0`, `I1`, or `I2` are set. These conditional interrupt flags should persist until overwritten by a non-Type IV command.
*   Handle Status Register updates carefully, preserving the state at termination if BUSY was active, or reflecting current state if issued while idle.

This note from the WD1793 datasheet regarding the **Force Interrupt command (Type IV)** is a bit nuanced and describes a specific behavior when no interrupt conditions are actually selected. Let's break it down:

## The Force Interrupt Command (Type IV) - additional flag processing considerations

This command has opcode `1101 I3 I2 I1 I0`. The bits `I0` through `I3` are flags that specify the conditions under which an interrupt (`INTRQ`) should be generated:

*   `I0 = 1`: Interrupt on Not-Ready to Ready transition.
*   `I1 = 1`: Interrupt on Ready to Not-Ready transition.
*   `I2 = 1`: Interrupt on every Index Pulse.
*   `I3 = 1`: Interrupt immediately.

**"NOTE: If I0 - I3 = 0, there is no interrupt generated but the current command is terminated and busy is reset."**

This part describes a special use case of the Force Interrupt command:

1.  **No Interrupt Condition Selected:** If the host issues a Force Interrupt command where all the condition bits `I0`, `I1`, `I2`, and `I3` are **zero** (e.g., command `0xD0`), then the FDC does *not* arm itself to generate an interrupt based on any external event or immediately.
2.  **Terminate Current Command:** If the FDC was busy executing another command (Type I, II, or III) when this specific Force Interrupt command (`0xD0`) is received, that ongoing command is **immediately terminated**.
3.  **Busy Reset:** The FDC's **Busy status bit (S0) is reset to 0**, indicating the FDC is now idle.
4.  **No INTRQ Generated (by this command):** Because no interrupt condition was specified (all I-bits are 0), this particular Force Interrupt command does *not* cause `INTRQ` to be asserted.

**Essentially, a Force Interrupt command with all I-bits as zero (`0xD0`) acts as a "Terminate Current Operation and Make FDC Idle" command without generating an immediate interrupt for that termination.**

**\"This is the only command that will enable the immediate interrupt to clear on a subsequent Load Command Register or Read Status Register.\"**

**Datasheet Statement:** The datasheet simply states that INTRQ is cleared by reading the Status Register or writing to the Command Register. The 0xD0 command terminates operations without generating INTRQ.

---

**Implementation Notes (Community Findings - Not from Datasheet):**

> *The following interpretation has been discussed in emulation communities but is not explicitly stated in the original datasheets. Treat as implementation guidance rather than authoritative specification.*

Some implementations suggest:

*   The \"Immediate Interrupt\" (`I3=1`) might have special behavior regarding how its INTRQ is cleared.
*   Issuing `0xD0` first may help reset internal interrupt condition latches.

**Practical Implication for Emulation:**

1.  **`Force Interrupt with I0-I3 = 0` (e.g., `0xD0`):**
    *   If FDC is busy, terminate current command.
    *   Set FDC Busy status (S0) to 0.
    *   **Do NOT assert INTRQ.**

2.  **`Force Interrupt with I3 = 1` (e.g., `0xD8`):**
    *   If FDC is busy, terminate current command and set Busy (S0) to 0.
    *   If FDC was idle, update status bits S1-S7 to reflect Type I context.
    *   **Assert INTRQ immediately.**

For most emulators, having Status Read / Command Write clear the FDC's internal INTRQ state is sufficient unless extremely cycle-accurate behavior is required.