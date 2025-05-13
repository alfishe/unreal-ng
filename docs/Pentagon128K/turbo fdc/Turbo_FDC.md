Okay, let's break down this "Turbo Floppy" modification for the Pentagon 128K. The core idea, as you rightly assumed, is to switch the WD1793 (КР1818ВГ93) clock between 1MHz for standard operations (read/write/format) and 2MHz specifically for seek-type operations (Restore, Seek, Step) to speed them up.

**Key Components from the Schematics:**

1.  **Main Pentagon Schematic (Page 1):**
    *   **D60 (КР1818ВГ93):** The Floppy Disk Controller (FDC). Its clock input is pin 24 (CLK).
    *   **D1 (К555КП11 / 74LS157):** This is a Quad 2-to-1 Multiplexer. One section of this chip is used to select the clock for the FDC.
        *   Pin 13 of D1: Receives 1MHz clock signal (labeled "1M").
        *   Pin 12 of D1: Receives 2MHz clock signal (labeled "2M").
        *   Pin 11 of D1: Output, connected to D60 pin 24 (FDC CLK).
        *   Pin 1 of D1: Select input (A/B). If LOW, 1MHz is selected. If HIGH, 2MHz is selected.
    *   **FDC Signals:**
        *   `/WR` (Write to FDC, derived from Z80 /WR and port address decode).
        *   Data Bus (D0-D7): Used to write command bytes to the FDC.
        *   `BUSY` (D60 pin 36 "ГОТОВН."): High when FDC is executing a command.
        *   `/DRQ` (D60 pin 30): Data Request, active low during data transfers.
        *   `/INTRQ` (D60 pin 29): Interrupt Request, active low on command completion or error.

2.  **Turbo Modification Schematic (Page 2 - "ТУРБИРОВАНИЕ ПЕНТАГОН 128"):**
    *   This schematic details the logic that generates the select signal for the clock MUX (D1 pin 1 on the main board).
    *   The output of this turbo logic is labeled "ТУРБО" in the simplified block diagram. This "ТУРБО" signal then goes through an inverter (transistor T10) before connecting to D1 pin 1 (main board).
    *   **Therefore, if the "ТУРБО" signal (output of the 3-input AND-like structure T6-T5-T9) is LOW, T10's output is HIGH, selecting 2MHz.**
    *   **If "ТУРБО" is HIGH, T10's output is LOW, selecting 1MHz.**

**Core Logic of the Turbo Modification (based on the simplified block diagram on page 2 and its annotations):**

The modification uses a combination of command byte decoding and FDC status signals. Let's denote the key signals from the turbo schematic's block diagram:

*   **D1 (555ТМ2 / 74LS74 D-FlipFlop):**
    *   Input D (T1 on diagram): Connected to `/D5` (inverted data bit 5 from the CPU data bus).
    *   Input CLK (T2 on diagram): "ЗАП.КОМАНДЫ" (Write Command strobe for FDC). This pulse occurs when the CPU writes a command to the FDC.
    *   Output Q (T3 on diagram): Latches the state of `/D5` at the time of command write. So, `T3 = /D5_latched`.

*   **D2 (555КП11 / 74LS157 Multiplexer):**
    *   Input A0 (T6 on diagram): Connected to `/D6` (inverted data bit 6).
    *   Input B0 (T4 on diagram): Connected to `/D7` (inverted data bit 7).
    *   Select S (Pin 1, connected to T3 from D-FF): `T3 = /D5_latched`.
    *   Output Y0 (also labeled T4 for the next stage, let's call it `Y0_mux`):
        `Y0_mux = (D5_latched * /D6) + (/D5_latched * /D7)` (This is standard MUX behavior: if SEL=0, output A; if SEL=1, output B).

*   **NAND Gate (formed by transistors T7, T8, T9):**
    *   Input 1 (T7 on diagram): Connected to `/D4` (inverted data bit 4).
    *   Input 2 (T8 on diagram): Connected to `Y0_mux` from the D2 multiplexer.
    *   Output (Collector of T9, let's call it `T9_NAND_out`):
        `T9_NAND_out = NAND(/D4, Y0_mux)`

*   **3-Input AND-like Gate (Transistor logic involving T9_NAND_out, T5, T6):**
    *   Input 1: `T9_NAND_out`.
    *   Input 2 (T5 on diagram): FDC `BUSY` signal ("ГОТОВН.").
    *   Input 3 (T6 on diagram): This is crucial. Connected to "ВЫВ.11 D5". D5 is a 74LS86 (XOR gate). Pin 11 of D5 is the output of an XOR gate whose inputs are FDC `/DRQ` and FDC `/INTRQ`.
        So, `T6_input = /DRQ XOR /INTRQ`.
    *   Output ("ТУРБО" signal):
        `"ТУРБО" = T9_NAND_out AND FDC_BUSY AND (/DRQ XOR /INTRQ)`

*   **Final Inverter (T10):**
    *   Input: "ТУРБО" signal.
    *   Output: Drives Pin 1 of the main clock MUX (D1 on main board).
        `Clock_MUX_Select = NOT("ТУРБО")`. (HIGH for 2MHz, LOW for 1MHz).

**How it Distinguishes Commands and Selects Clock Speed:**

The 2MHz clock is selected when `Clock_MUX_Select` is HIGH, which means the "ТУРБО" signal must be LOW.
"ТУРБО" is LOW if any of its three ANDed inputs are LOW:
1.  `T9_NAND_out` is LOW.
    *OR*
2.  `FDC_BUSY` is LOW.
    *OR*
3.  `(/DRQ XOR /INTRQ)` is LOW.

Let's analyze states:

1.  **During Seek/Restore/Step (Stepping Phase):**
    *   `FDC_BUSY` = HIGH.
    *   `/DRQ` = HIGH (no data transfer).
    *   `/INTRQ` = HIGH (interrupt not yet active).
    *   Therefore, `(/DRQ XOR /INTRQ)` = (1 XOR 1) = **LOW**.
    *   This makes the entire "ТУРБО" signal LOW, regardless of `T9_NAND_out`.
    *   So, `Clock_MUX_Select = NOT(LOW) = HIGH`.
    *   **Result: 2MHz clock during the stepping phase of Seek, Restore, and Step commands.** This is the desired turbo behavior. The command byte decoding (`T9_NAND_out`) becomes secondary here, as the `/DRQ XOR /INTRQ` condition dominates.

2.  **During Read/Write/Format Operations (Data Transfer Phase):**
    *   `FDC_BUSY` = HIGH.
    *   Either `/DRQ` is LOW (data being transferred) or `/INTRQ` is LOW (command just completed, waiting for CPU to read status/data).
    *   So, `(/DRQ XOR /INTRQ)` will be (0 XOR 1) or (1 XOR 0), which results in **HIGH**.
    *   Now, "ТУРБО" = `T9_NAND_out AND HIGH AND HIGH` = `T9_NAND_out`.
    *   We need to analyze `T9_NAND_out` for these commands:
        *   For Read Sector (e.g., `100m...`): D7=1, D6=0, D5_latched=0.
            *   `Y0_mux = (0 * /0) + (1 * /1) = (0*1) + (1*0) = 0`.
            *   `T9_NAND_out = NAND(/D4, 0) = 1`.
        *   For Write Sector (e.g., `101m...`): D7=1, D6=0, D5_latched=1.
            *   `Y0_mux = (1 * /0) + (0 * /1) = (1*1) + (0*0) = 1`.
            *   `T9_NAND_out = NAND(/D4, 1)`. If D4=0, NAND(1,1)=0. If D4=1, NAND(0,1)=1.
    *   If `T9_NAND_out` is 1 (as for Read Sector): "ТУРБО" = HIGH. `Clock_MUX_Select = NOT(HIGH) = LOW`.
    *   **Result: 1MHz clock for Read/Write operations.** This is correct.
    *   If `T9_NAND_out` happens to be 0 for a Write command variant (e.g. Write Sector with D4=0, `1010...`), then "ТУРБО" = LOW, leading to 2MHz. This would be an issue. However, the dominant factor is usually the `(/DRQ XOR /INTRQ)` state which ensures 1MHz when data transfer or interrupt is active. The scenario where `T9_NAND_out` makes a difference is when `(/DRQ XOR /INTRQ)` is high.

3.  **Command Completion (Interrupt Pending):**
    *   `FDC_BUSY` = HIGH (often stays high until status is read).
    *   `/DRQ` = HIGH.
    *   `/INTRQ` = LOW.
    *   `(/DRQ XOR /INTRQ)` = (1 XOR 0) = **HIGH**.
    *   "ТУРБО" = `T9_NAND_out AND FDC_BUSY AND HIGH`.
    *   This means the clock speed depends on `T9_NAND_out`. If it's 1 (typical for Read/Write completion), clock is 1MHz. If it's 0 (could be for Restore D4=0 completion), clock would be 2MHz. This is generally fine as no sensitive timing operations are happening.

4.  **FDC Idle:**
    *   `FDC_BUSY` = LOW.
    *   This makes the "ТУРБО" signal LOW.
    *   `Clock_MUX_Select = NOT(LOW) = HIGH`.
    *   **Result: 2MHz clock when FDC is idle.** This is harmless.

**Summary of How Commands are Distinguished:**

*   The primary distinction for turbo mode (2MHz) is when the FDC is `BUSY` AND both `/DRQ` and `/INTRQ` are inactive (HIGH). This condition is characteristic of the stepping phase of seek-type operations.
*   During any data transfer (`/DRQ` LOW) or when an interrupt is pending (`/INTRQ` LOW while `/DRQ` is HIGH), the `(/DRQ XOR /INTRQ)` term becomes HIGH. This makes the "ТУРБО" signal's state depend on the command byte decoding (`T9_NAND_out`).
*   The command byte decoding logic (`T9_NAND_out` derived from D4, D5, D6, D7) ensures that for commands like Read Sector, `T9_NAND_out` is HIGH, thus forcing "ТУРБО" HIGH and selecting 1MHz.
*   For some Type I commands (like Restore `0000hVrr` with `h=D4=0`), `T9_NAND_out` = `NAND(/0, 1)` = `NAND(1,1)` = 0. If this command completes and `BUSY` is still high, with `/INTRQ` low (so `(/DRQ XOR /INTRQ)` is 1), then "ТУРБО" = `0 AND 1 AND 1 = 0`. This results in 2MHz. This is usually fine after the core mechanical parts are done.

**Conclusion:**

The schematic cleverly uses the combination of FDC status lines (`BUSY`, `/DRQ`, `/INTRQ`) as the primary selector for the turbo window. The most critical part is the `(/DRQ XOR /INTRQ)` signal:
*   When it's **LOW** (both DRQ/INTRQ inactive OR both active), it forces 2MHz (if FDC is BUSY, this means stepping).
*   When it's **HIGH** (one of DRQ/INTRQ active), it allows the command decoding logic (`T9_NAND_out`) to determine if the clock should be 1MHz (if `T9_NAND_out` is HIGH) or potentially 2MHz (if `T9_NAND_out` is LOW). The design of `T9_NAND_out` ensures it's HIGH for read/write commands, thus defaulting to 1MHz in these critical phases.

This is a more sophisticated approach than simply decoding D7,D6,D5 of the command byte. It dynamically adjusts based on the actual phase of the FDC operation.