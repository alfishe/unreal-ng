**WD1793 Timeout Table (300 RPM Standardized)**

**Section 1: Core FDC Operational Timeouts**
*(These are explicit timeouts managed by the WD1793 during its fundamental operations like data transfer, sector searching, and seeking, directly impacting command success or failure with data loss or access issues.)*

| Timeout Name                          | Commands Involved                                                    | **Fundamental Timeout Value (Absolute Time or Condition)** | **Absolute Time** <br/> (FDC @1MHz case, TEST=1) | **Absolute Time** <br/> (FDC @2MHz case, TEST=1) | **Z80 T-States @3.5MHz** <br/> (for 1MHz / 2MHz FDC case, TEST=1) | FM/MFM Impact                                                                                                                                                                                                                                                           | Status Bit Set / Notes                                                                                                                                                                                                                                                            |
| :------------------------------------ | :------------------------------------------------------------------- | :------------------------------------------------------- | :--------------------------------------------------- | :--------------------------------------------------- | :--------------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **DRQ Service Timeout (Lost Data)**   | READ SECTOR, WRITE SECTOR, READ ADDRESS, READ TRACK, WRITE TRACK     | MFM: 16 µs <br/> FM: 32 µs                                | MFM: 16 µs <br/> FM: 32 µs                            | MFM: 16 µs <br/> FM: 32 µs                            | MFM: ~56 T-states <br/> FM: ~112 T-states                               | **These absolute timeout values are solely determined by FDD transfer speed (one byte-time) and encoding (MFM/FM). Independent of FDC/CPU clocks.** MFM (500 Kbit/s = 16 µs/byte). FM (250 Kbit/s = 32 µs/byte). | Lost Data (Status Register bit 2 or S2). CPU must R/W Data Register within this fixed **ONE FDD byte-time** of DRQ. The FDC uses its clock to measure if this external deadline is missed. |
| **Internal Head Settle Delay (R/W)**  | READ SECTOR, WRITE SECTOR, READ ADDRESS, READ TRACK (with E=1 in command); Type I commands (with V=1) | FDC Internal Delay: 15ms @2MHz CLK, 30ms @1MHz CLK (Requires **TEST=1**) | **~30 ms**                                           | **~15 ms**                                           | **~105,000 / ~52,500 T-states**                                        | Not impacted by FM/MFM. This FDC-internal delay occurs *if TEST input is high*. If TEST=0, this delay is 0ms (bypassed).                                                                               | Not an error bit directly. If TEST=1 and E=1 (Type II/III) or V=1 (Type I): FDC asserts HLD, waits this internal delay, then samples external HLT input until true. External HLT is system-timed (e.g., 30-100ms one-shot) for FDD head engage. If HLT never true, command typically fails with Record Not Found (Status Register) after RNF timeout. If TEST=0, FDC samples HLT immediately after asserting HLD (no internal timed delay). |
| **Record Not Found (RNF) Timeout**    | READ SECTOR, WRITE SECTOR, READ ADDRESS                              | Waits for up to 5 index pulses (200ms/pulse)             | ~1 s                                                 | ~1 s                                                 | ~3,500,000 T-states                                                    | Not impacted. (Absolute time depends on disk index pulses and is independent of FDC clock).                                                                                                             | Record Not Found (Status Register bit 3 for Type II, bit 4 for Read Track). Set if matching IDAM not found after full head settle sequence (FDC internal delay if TEST=1, then external HLT wait) and N index pulses during ID search.                             |
| **Seek Error Timeout (Verify)**       | RESTORE, SEEK, STEP (Type I with Verify flag V=1)                    | Waits for 2-5 index pulses (200ms/pulse)                 | ~400 ms - 1 s                                        | ~400 ms - 1 s                                        | ~1.4M - 3.5M T-states                                                  | Not impacted. (Absolute time depends on disk index pulses and is independent of FDC clock).                                                                                                             | Seek Error (Status Register bit 5). After mechanical motion & head settling (incl. internal delay if TEST=1, then HLT wait), if target track ID not verified.                                                                                                     |
| **Seek Error Timeout (Restore TR00)** | RESTORE (Type I)                                                     | Max 255 step pulses attempt                              | Depends on step rate (e.g., 255 * 6ms/step = 1.53s)  | Depends on step rate (e.g., 255 * 3ms/step = 765ms)  | Depends on step rate (e.g., ~5.355M / ~2.677M T-states for examples)    | Not impacted by FM/MFM. (Step rate's absolute time depends on FDC clock speed and programming).                                                                                                        | Seek Error (Status Register bit 5). If TR00 input not active after attempting 255 steps towards track 0.                                                                                                                                                                     |
| **Index Timeout (No Pulse during Type I)** | RESTORE, SEEK, STEP (Type I commands)                              | Waits for up to 5 index pulses (200ms/pulse)             | ~1 s                                                 | ~1 s                                                 | ~3,500,000 T-states                                                    | Not impacted. (Absolute time depends on disk index pulses and is independent of FDC clock).                                                                                                             | Seek Error (Status Register bit 5). If no index pulse for 5 revolutions (1 second @300 RPM) during Type I. Indicates motor/disk/sensor issue.                                                                                                                    |


---

**Section 2: Force Interrupt Command Timeouts**
*(These timeouts are specific to the Type IV (Force Interrupt) command, allowing software to wait for specific FDC/drive state changes or events with a timeout mechanism. All revolution-based timeouts here assume 300 RPM.)*

| Timeout Name                          | Commands Involved                                                    | Typical Value (FDC Internal Logic)                   | Absolute Time (1MHz FDC Clock)             | Absolute Time (2MHz FDC Clock)             | Z80 T-States @3.5MHz (for 1MHz FDC value) | Z80 T-States @3.5MHz (for 2MHz FDC value) | FM/MFM Impact                                                              | Status Bit Set / Notes                                                                                                                                                               |
| :------------------------------------ | :------------------------------------------------------------------- | :--------------------------------------------------- | :----------------------------------------- | :----------------------------------------- | :---------------------------------------- | :---------------------------------------- | :------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Force Interrupt: Not Ready → Ready Timeout** | FORCE INTERRUPT (Type IV, Condition 0: `I0=0, I1=0, I2=0`)          | Waits for 128 index pulses                           | ~25.6 s                                    | ~25.6 s                                    | ~89,600,000 T-states                      | ~89,600,000 T-states                      | Based on 300 RPM disk rotation.                                            | Interrupt generated. Status Reg bit 7 (Not Ready) and bit 6 (Write Protect/Busy) reflect state. Timeout indicated by Busy still set.                                               |
| **Force Interrupt: Ready → Not Ready Timeout** | FORCE INTERRUPT (Type IV, Condition 1: `I0=1, I1=0, I2=0`)          | Waits for 16 index pulses                            | ~3.2 s                                     | ~3.2 s                                     | ~11,200,000 T-states                      | ~11,200,000 T-states                      | Based on 300 RPM disk rotation.                                            | Interrupt generated. Status Reg bit 7 reflects state. Timeout indicated by Busy still set.                                                                                         |
| **Force Interrupt: Index Pulse Timeout** | FORCE INTERRUPT (Type IV, Condition 2: `I0=0, I1=1, I2=0`)          | Waits for 2 index pulses                             | ~400 ms                                    | ~400 ms                                    | ~1,400,000 T-states                       | ~1,400,000 T-states                       | Based on 300 RPM disk rotation.                                            | Interrupt generated. Status Reg bit 2 (Index Pulse) reflects state. Timeout indicated by Busy still set.                                                                           |
| **Force Interrupt: Immediate or Busy Timeout** | FORCE INTERRUPT (Type IV, Condition 3: `I0=1, I1=1, I2=0`)          | If Busy, waits up to 2 index pulses. If not Busy, immediate. | Up to ~400 ms if Busy                      | Up to ~400 ms if Busy                      | Up to ~1.4M T-states if Busy             | Up to ~1.4M T-states if Busy             | Based on 300 RPM disk rotation if Busy.                                    | Interrupt generated. Busy bit in Status Reg is cleared. If it was Busy and didn't clear in 2 index pulses, this acts as a timeout.                                              |

---

**Section 3: Other Delays and System-Level Considerations**
*(These are either programmable delays initiated by the CPU or delays managed by the system external to direct FDC timeout mechanisms but essential for proper operation. Revolution-based delays assume 300 RPM.)*

| Timeout Name                          | Commands Involved                                                    | Typical Value (FDC Internal Logic / System)         | Absolute Time (1MHz FDC Clock)             | Absolute Time (2MHz FDC Clock)             | Z80 T-States @3.5MHz (for 1MHz FDC value) | Z80 T-States @3.5MHz (for 2MHz FDC value) | FM/MFM Impact                                                              | Status Bit Set / Notes                                                                                                                                                               |
| :------------------------------------ | :------------------------------------------------------------------- | :-------------------------------------------------- | :----------------------------------------- | :----------------------------------------- | :---------------------------------------- | :---------------------------------------- | :------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Programmable Head Load Settling Delay (HLT)** | RESTORE, SEEK, STEP (Type I, when HLT bit set and E=1 in command) | e.g., 10240 or 15360 FDC clocks (selectable)        | 10.24 ms or 15.36 ms                       | 5.12 ms or 7.68 ms                         | ~35,840 or ~53,760 T-states               | ~17,920 or ~26,880 T-states               | Fixed FDC clock counts; absolute time halves with 2MHz FDC clock.          | Not an error. Programmed delay via HLT output, selected by Data Register bits. HLD pin must be enabled (E=1).                                                                      |
| **Spin-Up Motor Delay (System Level)** | Any command attempting disk access if motor was off                  | System waits for 1-5 revolutions (drive dependent)  | ~200 ms – 1 s                              | ~200 ms – 1 s                              | ~700k - 3.5M T-states                     | ~700k - 3.5M T-states                     | System waits for drive to reach 300 RPM speed.                             | Not a formal FDC timeout. System usually polls FDC Status Register (Not Ready bit) or drive's READY line until stable.                                                                |

---

**Assumptions for Calculations:**
*   Z80 CPU clock: 3.5 MHz (1 T-state ≈ 0.2857 µs)
*   **Floppy Disk Drive rotation: 300 RPM (1 revolution = 200 ms; 1 index pulse per 200 ms)**
*   MFM data rate: 1 byte = 16 µs
*   FM data rate: 1 byte = 32 µs
*   **DRQ to Lost Data Timeout:** 32 µs absolute time for MFM mode, 64 µs absolute time for FM mode.

---
**Q&A**

**1. Does Data Speed to/from FDD change with WD1793 Clocking (1MHz vs 2MHz)?**

**No, the fundamental data speed to and from the floppy disk drive itself does NOT change based on the WD1793's clock frequency.**

Here's why:
*   **FDD Dictates Speed:** The data rate is primarily determined by:
    *   **Encoding Scheme:** FM (Single Density) or MFM (Double Density). This is selected by a bit in the command register.
    *   **Disk Rotation Speed:** For the drives you mentioned (DSDD/DSSD/SSDD/SSSD 5.25" or 3.5" DD), this is typically fixed at 300 RPM.
    *   **Media Characteristics:** The density of magnetic flux changes the media can support.
*   **WD1793 Adapts:** The WD1793's job is to synchronize with this external, fixed data rate from the drive.
    *   When **reading**, it uses its data separator (internal or external) to lock onto the incoming bitstream from the drive head.
    *   When **writing**, it generates the MFM or FM encoded bitstream at the correct timing to be recorded by the drive.
*   **Effect of FDC Clock on its Internal Operation:**
    *   A **faster FDC clock (e.g., 2MHz)** means the FDC has more of its own internal clock cycles to process each bit cell from/to the disk.
        *   For MFM, a bit cell is 4 µs.
            *   With a 1MHz FDC clock, the FDC has 4 of its clocks per bit cell.
            *   With a 2MHz FDC clock, the FDC has 8 of its clocks per bit cell.
    *   This can potentially allow for more precise internal timing for data separation (if using its internal separator) and generally faster internal state machine operation within the FDC. However, it doesn't alter the speed at which bits must physically be read from or written to the disk surface.

**In summary: The disk spins at the same speed, and the MFM/FM encoding defines the bit rate on the media. The WD1793's clock frequency affects how it internally handles that data stream, not the stream's inherent speed from the drive.**

**2. What Commands / Timeouts ARE Affected by Changing WD1793 Clock Speed?**

Any timeout or delay that is defined in terms of a specific number of **FDC clock cycles** will have its **absolute time duration changed** if the FDC's master clock frequency is changed. If the FDC clock doubles, the absolute time for these specific delays/timeouts will halve.

Here's a breakdown based on your table:

**Timeouts/Delays AFFECTED (Absolute time changes):**

*   **Internal Head Settle Delay (R/W):**
    *   This is typically specified as a fixed number of FDC clock cycles (e.g., 32,000 FDC clocks for the WD1793-02 at 1MHz results in ~32ms).
    *   If the FDC clock is 2MHz, the same 32,000 FDC clocks would result in an absolute time of ~16ms.
    *   **Impact:** A faster FDC clock reduces this internal settling time. This could be an issue if the drive physically needs more time to settle than the new, shorter delay provides, potentially leading to more "Record Not Found" errors.

*   **Seek Error Timeout (Restore TR00) - specifically the Step Rate part:**
    *   The RESTORE command tries up to 255 steps. The *timeout* is if TR00 isn't found after these steps.
    *   The **Step Rate** (ms per step) for Type I commands is programmed via bits in the command register, which select a divisor for the FDC's master clock.
    *   If the FDC clock is 2MHz instead of 1MHz, a given step rate setting (e.g., "6ms/step at 1MHz FDC clock") will result in steps that take half the absolute time (i.e., "3ms/step at 2MHz FDC clock").
    *   **Impact:** Steps happen faster. The total time for 255 steps will be shorter.

*   **Programmable Head Load Settling Delay (HLT):**
    *   For Type I commands, if the `E` bit is set and `HLT` is used, the delay is specified by selectable FDC clock counts (e.g., 10240 or 15360 FDC clocks).
    *   **Impact:** The absolute time of this programmable delay will halve if the FDC clock doubles.

**Timeouts/Delays NOT DIRECTLY AFFECTED (Absolute time generally remains the same, as they are tied to external events like disk rotation or the fundamental data rate):**

*   **DRQ Service Timeout (Lost Data):**
    *   The *requirement* (e.g., 32 µs for MFM) is dictated by the data rate from the disk (1 byte every 16 µs for MFM). The FDC and CPU *must* service DRQ within this window.
    *   A faster FDC clock gives the FDC more internal cycles to process the byte and assert DRQ, but the *deadline* set by the disk remains the same. The FDC's internal logic for detecting "Lost Data" (i.e., a new byte arrived from the disk before the previous one was read by the CPU) is fundamentally tied to these disk byte times.
    *   So, the 32µs (MFM) or 64µs (FM) absolute time window *does not change*. The FDC effectively has more of its own clock cycles to count within that fixed absolute time window.

*   **Record Not Found (RNF) Timeout:**
    *   This is typically specified in terms of a number of **disk revolutions** (e.g., "wait for up to 5 index pulses").
    *   Since the disk rotation speed (300 RPM) is constant, the time between index pulses (200ms) is constant. Thus, the absolute timeout duration remains the same regardless of FDC clock.

*   **Seek Error Timeout (Verify):**
    *   Similar to RNF, this is usually tied to verifying the track ID within a certain number of **disk revolutions** after the seek. Absolute time remains the same.

*   **Index Timeout (No Pulse during Type I):**
    *   This is defined as not seeing an index pulse for a certain number of **disk revolutions**. Absolute time remains the same.

*   **Force Interrupt Timeouts (All conditions based on Index Pulses or Busy state):**
    *   Conditions 0, 1, and 2 are explicitly tied to counting a specific number of **index pulses**. Absolute time remains the same.
    *   Condition 3 (Immediate or Busy Timeout) might wait for Busy to clear, which could involve waiting up to 2 index pulses if the FDC was busy. Absolute time remains the same.

*   **Spin-Up Motor Delay (System Level):**
    *   This is entirely system-dependent and usually based on waiting for a certain number of **disk revolutions** or a fixed time for the motor to reach speed. Not affected by FDC clock.

**In essence:**
*   If a delay/timeout is counted in FDC clocks: **Affected** (absolute time changes).
*   If a delay/timeout is based on disk revolutions (index pulses) or the inherent data rate from the disk: **Not Affected** (absolute time constraint remains).