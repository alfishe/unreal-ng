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