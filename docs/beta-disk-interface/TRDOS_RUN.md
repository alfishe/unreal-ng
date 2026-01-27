When you are sitting at the `A>` prompt and press **`R`** (the Spectrum shortcut for the `RUN` token) followed by **`Enter`**, TR-DOS 5.04T follows a specific internal path. Because no filename is provided, the ROM logic **forces a search for a file named "boot"**.

Here is the exact call trace you should see in your debugger:

### 1. Command Processing (Main Loop)
*   **`x02CB` (Command Processor):** The ROM is waiting here.
*   **`x2135` (Input):** User types `R` (Token `#EF`) and `Enter`.
*   **`x3032` (Squeeze):** Scans the buffer. If you typed ASCII 'R', it converts it to the BASIC token `0xEF`.
*   **`x030A` (Dispatcher):**
    *   `A` register contains `0xEF`.
    *   It searches the table at `x2FF3` for `0xEF`.
    *   It finds it at index 12.
*   **`x3008` (Jump Table):** Looks up index 12 and finds address **`x1D4D`**.

### 2. The RUN Handler (`x1D4D`)
*   **`x1D4D` (RUN Entry):**
    *   Calls **`x1852`**: Clears the flag `0x5D10` (indicating a fresh load).
    *   Calls **`x1836`**: The main Load/Run processing logic.

### 3. Defaulting to "boot" (`x187A`)
This is the most important part for your specific scenario. Since you provided no parameters:
*   **`x1836`** calls **`x187A`**.
*   **`x187A` (Check Params):**
    *   It checks the command line after the `RUN` token.
    *   **Logic:** If the next character is `Enter` (`0x0D`), it branches to **`x027B`**.
*   **`x027B` (Force Boot):**
    *   This routine manually writes the string **`"boot"`** into the command buffer.
    *   It sets the `A` register to `#FE` (a flag indicating "boot" is running).
    *   It then jumps back to the standard parser logic as if the user had typed `RUN "boot"`.

### 4. Disk Search (`x292F`)
*   **`x292F` (Find File):**
    *   Calls **`x3DCB` (Select Drive)**: This is where your **Calibration Loop** (`x3E16`) usually triggers if the drive isn't ready.
    *   Calls **`x1CB3` (Search Catalog)**:
        1.  Reads Track 0, Sector 9 (Volume Info).
        2.  Reads Track 0, Sectors 0-7 (Catalog).
    *   It looks for a file with the name `"boot    "` and extension `"B"`.

### 5. Loading the file
*   **`x165D` (Read Descriptor):** Once "boot" is found, its 16-byte metadata is copied to RAM (`0x5CDD`).
*   **`x18AB` (Prepare Memory):** TR-DOS moves the BASIC pointers (`PROG`, `VARS`) to make room for the incoming "boot" program.
*   **`x1921` (Data Transfer):**
    *   **`x3E63` (Seek):** Moves the disk head to the track where "boot" starts.
    *   **`x3F0E` (Read Sector):** Uses the `INI` loop to fill RAM with the program data from the disk.

### 6. Handover to BASIC (`x012A`)
*   **`x012A` (Enter BASIC):**
    *   It finds the "Auto-start line" from the "boot" file metadata.
    *   It calls **`x20E5`** to delete the TR-DOS temporary buffers and restore the standard Spectrum memory map.
    *   It uses the **RAM Trampoline** (at `0x5F10`) to:
        1.  Switch the hardware out of "TR-DOS ROM" mode.
        2.  Jump to the BASIC Interpreter in the Main ROM at **`0x1B76`**.

---

### Summary Checklist for your Debugger:
1.  **Stop at `x187A`**: Verify that the ROM detects the empty command line.
2.  **Stop at `x027B`**: Verify it is injecting the word "boot".
3.  **Monitor `0x5CFA` at `x3DCB`**: This is where you previously saw the "Write Track" disaster. Ensure that `0x5CFA` contains `0x01`, `0x02`, or `0x03`—**not** `0xE4`.
4.  **Final Exit**: The CPU should eventually leave the `0x3Dxx` range and start executing code in the `#0000-#3FFF` (BASIC ROM) or `#5B00+` (RAM) range.

**If you see it skipping "boot" and going straight to a Seek error,** it means the "Parameter Check" at `x187A` failed to realize the command line was empty.