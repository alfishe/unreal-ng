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

**"This is the only command that will enable the immediate interrupt to clear on a subsequent Load Command Register or Read Status Register."**

This part is a bit more subtle and refers to a specific characteristic of how the **"Immediate Interrupt" (I3=1)** condition, once set and triggered, might be managed or cleared.

Let's analyze this:

*   Normally, when `INTRQ` is asserted (for any reason other than perhaps a "stuck" I3 interrupt), it is cleared by the host either reading the Status Register or writing to the Command Register.
*   The "Immediate Interrupt" (`I3=1`) is a bit special. If you issue a Force Interrupt with `I3=1`, `INTRQ` asserts immediately.
*   The note implies that if you *first* issue a Force Interrupt with `I0-I3=0` (like `0xD0`), it somehow "primes" or "enables" a *subsequent* Force Interrupt that uses `I3=1` to behave in the standard way regarding how its `INTRQ` is cleared (i.e., cleared by a Status Read or Command Load).

**Interpretation and Potential Rationale:**

This phrasing is slightly awkward and has led to some debate in emulation communities. Here's a common interpretation:

*   **The "Immediate Interrupt" (I3=1) might be a "sticky" interrupt condition.** Once an `INTRQ` is generated due to `I3=1`, simply reading the status or loading another command might not clear the *underlying condition* that caused the I3 interrupt if the FDC wasn't "prepared" for it. The FDC might keep `INTRQ` asserted if the I3 condition isn't properly reset internally.
*   The note suggests that issuing a Force Interrupt command with `I0-I3=0` **first** (e.g., `0xD0`) acts as a way to properly **clear or reset the FDC's interrupt generation logic**, specifically making it ready so that *if an Immediate Interrupt (I3=1) is later triggered*, that specific `INTRQ` *will then be reliably cleared* by a subsequent Status Read or Command Load.

**Why would this be necessary?**

It could be a design quirk related to how the "immediate" interrupt logic was implemented. Perhaps without this "priming" `0xD0` command, an `I3=1` interrupt might behave erratically with respect to being cleared, potentially re-asserting `INTRQ` immediately after being cleared by a status read if the internal "interrupt request" latch for I3 wasn't properly reset.

**In Simpler Terms for Emulation:**

1.  **`Force Interrupt with I0-I3 = 0` (e.g., `0xD0`):**
    *   If FDC is busy, terminate current command.
    *   Set FDC Busy status (S0) to 0.
    *   **Do NOT assert INTRQ.**
    *   Internally, this command might also reset some internal interrupt condition latches, making the FDC "clean" for future interrupt generation, especially for an I3 type.

2.  **`Force Interrupt with I3 = 1` (e.g., `0xD8`):**
    *   If FDC is busy, terminate current command and set Busy (S0) to 0.
    *   If FDC was idle, update status bits S1-S7 to reflect Type I context.
    *   **Assert INTRQ immediately.**
    *   The note implies that for this INTRQ (caused by I3=1) to be *reliably cleared* by a subsequent Status Read/Command Load, a `Force Interrupt with I0-I3=0` should have been issued at some point prior, perhaps as part of an initialization or error recovery sequence by the software.

**Practical Implication for Emulation:**

*   When emulating the `0xD0` command (Force Interrupt with no conditions), ensure it terminates the current operation and clears the Busy flag *without* asserting `INTRQ`.
*   When emulating other Force Interrupt commands (with I0, I1, I2, or I3 set), `INTRQ` should be asserted when the condition is met.
*   The clearing of `INTRQ` (by Status Read or Command Write) should generally work for all interrupt causes. The nuance about `I3=1` and the "enabling" command (`0xD0`) suggests that if you *don't* issue `0xD0` before an `I3=1`, the `INTRQ` from `I3=1` might behave as if it's not being cleared properly by the FDC (though the host action *should* still clear it at the host's interrupt controller level). For most emulators, simply having Status Read / Command Write clear the FDC's internal `INTRQ` state is usually sufficient unless extremely cycle-accurate behavior for this specific edge case is required.

The note highlights a specific detail about the FDC's internal interrupt logic, particularly concerning the "immediate" interrupt and how to ensure it's properly manageable.