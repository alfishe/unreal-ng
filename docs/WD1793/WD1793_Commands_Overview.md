# WD1793 Floppy Disk Controller: Command Overview

## Introduction

The WD179X family of controllers accepts eleven distinct commands, categorized into four types based on their primary function. Understanding these commands, their parameters, and their expected behavior is crucial for interfacing with the chip or emulating its functionality.

Commands are issued by writing a specific command byte to the **Command Register** (when `A1=0, A0=0, /CS=0, /WE=0`). Commands should generally only be loaded when the **BUSY** status bit (Status Register Bit 0) is clear (0), indicating the controller is idle. The only exception is the **Force Interrupt** command (Type IV), which can be issued at any time.

## Command Execution Flow

Most commands follow a general execution pattern:

1.  **Command Phase:**
    *   The host CPU checks if the controller is **BUSY** (Status Register Bit 0 = 0).
    *   If not busy, the host writes the desired command byte to the Command Register.
    *   For commands requiring parameters (like target track/sector), the host pre-loads the necessary registers (Data Register for SEEK, Sector Register for Type II commands) *before* issuing the command.

2.  **Execution Phase:**
    *   The controller sets the **BUSY** status bit (Status Bit 0 = 1).
    *   The controller performs the actions defined by the command (e.g., stepping the head, searching for an ID field, reading/writing data).
    *   During data transfer commands (Read/Write), the **DRQ** (Data Request) signal/status bit is used to handshake data bytes with the host. The host *must* respond to DRQ within a specific time limit (see Timing section) to avoid a **LOST DATA** error.
    *   The controller manages interface signals like `HLD` (Head Load), `WG` (Write Gate), `STEP`, `DIRC` based on the command and its flags.

3.  **Result Phase:**
    *   Upon completion (successful or due to error), the controller clears the **BUSY** status bit (Status Bit 0 = 0).
    *   It asserts the **/INTRQ** (Interrupt Request) signal (active low).
    *   Relevant status bits in the **Status Register** are updated to reflect the outcome (e.g., CRC Error, Record Not Found, Write Protect).
    *   The host reads the Status Register (when `A1=0, A0=0, /CS=0, /RE=0`) to determine the result. Reading the Status Register or writing a new command typically clears the **/INTRQ** signal.

## Command Types Summary

Commands are divided into four functional groups:

*   **Type I:** Head Positioning (Restore, Seek, Step, Step In, Step Out)
*   **Type II:** Sector Read/Write (Read Sector, Write Sector)
*   **Type III:** Track/Format Operations (Read Address, Read Track, Write Track)
*   **Type IV:** Immediate Control (Force Interrupt)

*(Detailed breakdowns of each command type and their specific execution flows will follow in subsequent documents.)*