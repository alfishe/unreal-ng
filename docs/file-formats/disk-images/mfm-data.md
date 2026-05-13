# A Deep Dive into MFM Encoding: From Magnetic Flux to Raw Data

Modified Frequency Modulation (MFM) was the dominant data encoding scheme for floppy disks and early hard drives from the late 1970s through the early 1990s. Understanding MFM is essential for anyone interested in digital archaeology, retrocomputing, or low-level data recovery.

This article provides a comprehensive guide to how MFM works, from the physical magnetic patterns on a disk to the final decoded bytes. We will cover the encoding rules, special disk markers, and the decoding process using a Phase-Locked Loop (PLL).

## 1. The Physical Layer: Magnetic Flux Transitions

At the most fundamental level, data is stored on a magnetic disk as a series of magnetized regions with alternating north (N) and south (S) polarities. A **flux transition** is the point where the magnetic polarity flips (e.g., from N to S).

```
Magnetic Polarity:  N → S → N → S → N → S → N ...
Flux Transition:      ↑   ↑   ↑   ↑   ↑   ↑   ...
```

Disk drive hardware does not read the absolute polarity; it detects the *change* in polarity. Therefore, the raw data read from a disk is a sequence of time intervals between these flux transitions. Tools like Greaseweazle, KryoFlux, or Supercard Pro capture this raw data. For example, the Supercard Pro (SCP) format stores these intervals as 16-bit counts in 40MHz ticks, where each tick is 25 nanoseconds.

**Our goal is to convert this stream of time intervals back into the original binary data.**

## 2. The Core Rules of MFM Encoding

MFM is designed to be self-clocking, meaning the timing information (the "clock") is embedded within the data stream itself. This is achieved by ensuring that transitions do not occur too close together or too far apart. MFM encoding translates each data bit into a two-bit MFM code cell.

The encoding rule depends on the **previous data bit** that was encoded.

| Current Data Bit | Previous Data Bit | MFM Code Word | Flux Pattern (`T`=transition, `.`=no transition) |
| :--------------- | :---------------- | :------------ | :--------------------------------------------- |
| `1`              | (any)             | `01`          | `.T` (Transition in the middle of the cell)    |
| `0`              | `1`               | `00`          | `..` (No transition at all)                    |
| `0`              | `0`               | `10`          | `T.` (Transition at the start of the cell)     |

**Key Observations:**
*   A **data `1`** always creates a transition in the middle of its bit cell.
*   A **data `0`** never has a transition in the middle of its cell.
*   The first bit of the MFM code word is the **clock bit**, and the second is the **data bit**.
*   This scheme guarantees that the time between any two flux transitions is at least 1 bit cell time (`1.0T`) and at most 2 bit cell times (`2.0T`).

### The 500 kHz "MFM Encoded Rate" is the Channel Clock

Think of the 500 kHz rate as the **metronome** or the **"speed limit"** for the MFM channel. It does **not** represent the frequency of the flux transitions themselves.

*   **It defines the Bit Cell (`T`):** This rate tells us the duration of the fundamental time window for one MFM bit (clock or data).
    *   `T = 1 / 500,000 Hz = 2000 ns (2.0 µs)`
*   **It is the Fastest Possible Frequency:** As we will see, 500 kHz is the absolute highest frequency of flux transitions you can physically encounter in a standard MFM data stream. It is the ceiling, not the average.

### The "Physical Flux Rate" is a Variable Frequency

The actual rate of flux transitions you see on an oscilloscope is **not constant**. It changes dynamically depending on the data being written. It is determined by the three legal MFM time intervals: `1.0T`, `1.5T`, and `2.0T`.

Each of these time periods corresponds to a different instantaneous frequency (`frequency = 1 / period`).

| MFM Time Interval | Physical Time Between Transitions | Corresponding Instantaneous Frequency |
| :---------------- | :------------------------------- | :------------------------------------- |
| `1.0T`            | `2000 ns` (2.0 µs)               | `1 / 0.0000020 s = 500 kHz`            |
| `1.5T`            | `3000 ns` (3.0 µs)               | `1 / 0.0000030 s = 333.3 kHz`          |
| `2.0T`            | `4000 ns` (4.0 µs)               | `1 / 0.0000040 s = 250 kHz`            |

**What you will see on an oscilloscope is a signal where the time between consecutive pulses is constantly shifting between 2000 ns, 3000 ns, and 4000 ns.**

### Visualizing the Signal on an Oscilloscope

If you were to probe the amplified read head signal from a DD floppy drive, you would see something like this:



*   The signal is not a clean digital square wave. It's a series of analog "bumps" or pulses. Each peak represents a flux transition detected by the read head.
*   By measuring the **time between the peaks**, you would find it is always one of the three values: ~2000 ns, ~3000 ns, or ~4000 ns (allowing for jitter and speed variation).
*   You will **never** see two peaks closer together than ~2000 ns.

### Data Patterns and Their Resulting Frequencies

Let's look at what data patterns produce these different frequencies:

#### Highest Frequency (500 kHz)

*   **Pattern:** A long string of `1`s in the data. For example, the byte `0xFF` (`11111111b`).
*   **MFM Encoding:** This encodes to an MFM stream of `...01010101...`.
*   **On the Oscilloscope:** You would see a stream of transitions spaced uniformly **2000 ns** apart. This is the closest the MFM rules allow transitions to be, and it corresponds to a **500 kHz** signal. This is often seen in the `0xFE` ID Address Mark.

#### Lowest Frequency (250 kHz)

*   **Pattern:** A data pattern that generates consecutive `2.0T` intervals. For example, the byte `0x49` (`01001001b`).
*   **MFM Encoding:** This generates a stream like `...0100010001...`. The `10001` pattern creates the `2.0T` gap.
*   **On the Oscilloscope:** You would see a stream of transitions spaced uniformly **4000 ns** apart. This corresponds to a **250 kHz** signal.

#### Mixed Frequencies (The Normal Case)

*   **Pattern:** Almost any typical data byte, like `0xA9`.
*   **On the Oscilloscope:** You would see the frequency jumping between all three rates. The time between peaks might be 4000 ns, then 4000 ns, then 3000 ns, then 3000 ns as the PLL decodes the byte. This is the standard operating behavior.

### Conclusion

The **500 kHz MFM Encoded Rate** is the reference **channel clock**, which defines the 2000 ns bit cell. The **physical flux rate** is what you see on an oscilloscope: a dynamic signal whose instantaneous frequency constantly varies between **500 kHz**, **333.3 kHz**, and **250 kHz** according to the MFM encoding rules and the data being read.

### From MFM Bits to Flux Transitions

The final MFM bitstream is written to the disk. Every `1` in this stream corresponds to a flux transition, and every `0` corresponds to no transition.

The key is that the MFM bitstream operates on a clock that is twice the data rate. The fundamental unit of time is not the full bit cell (T), but half a bit cell (0.5T).
Here is the definitive rule:
The time between two flux transitions is determined by the number of 0s between two 1s in the final MFM bitstream.
- ...1**0**1... → 1 zero between transitions → 2 half-cell slots → 1.0T interval (e.g., 2000 ns for DD)
- ...1**00**1... → 2 zeros between transitions → 3 half-cell slots → 1.5T interval (e.g., 3000 ns for DD)
- ...1**000**1... → 3 zeros between transitions → 4 half-cell slots → 2.0T interval (e.g., 4000 ns for DD)
 
These three—1.0T, 1.5T, and 2.0T—are the only three legal time intervals for standard MFM-encoded data. The presence of a 1.5T interval is not an error; it is a required and fundamental part of the encoding scheme.

#### Example: Encoding the Byte `0xA9` (Binary `10101001`)

Let's assume the previous data bit was `0`.
You are absolutely right to demand this level of precision. My apologies for the previous omissions and confusion. The step-by-step creation of the MFM stream and the resulting physical flux pattern is critical to understanding the process.

I have recreated the table with all your requested changes:
1.  The **"Previous Data Bit"** and **"Current Data Bit"** columns are now in the correct logical order.
2.  The **"Flux Pattern"** column has been added back in.
3.  **No ellipses** are used. The "Final MFM Stream" and "Final Flux Pattern" columns show the complete, cumulative result at each step.
4.  The **time intervals have been rigorously recalculated** to show the duration *between* the newly created flux transition and the one immediately preceding it.

Here is the revised and corrected section:

***

#### Example: Encoding the Byte `0xA9` (Binary `10101001`)

This table shows how each bit of the byte `0xA9` is encoded sequentially. We will assume the previous data bit written to the disk was a `0`. The "Time Interval Created" column shows the physical time between the flux transition created in the current step and the one created in a previous step.

| Step | Previous Data Bit | Current Data Bit | MFM Word | Final MFM Stream | Final Flux Pattern | Time Interval Created (ns for DD) |
| :--- | :---------------- | :--------------- | :------- | :-------------------- | :------------------ | :-------------------------------- |
| 1    | `0`               | `1`              | `01`     | `01`                  | `.T`                | `2000 ns (1.0T)`                  |
| 2    | `1`               | `0`              | `00`     | `0100`                | `.T..`              | *(no transition created)*         |
| 3    | `0`               | `1`              | `01`     | `010001`              | `.T..T`             | `4000 ns (2.0T)`                  |
| 4    | `1`               | `0`              | `00`     | `01000100`            | `.T..T..`           | *(no transition created)*         |
| 5    | `0`               | `1`              | `01`     | `0100010001`          | `.T..T..T`          | `4000 ns (2.0T)`                  |
| 6    | `1`               | `0`              | `00`     | `010001000100`        | `.T..T..T..`        | *(no transition created)*         |
| 7    | `0`               | `0`              | `10`     | `01000100010010`      | `.T..T..T..T.`      | `3000 ns (1.5T)`                  |
| 8    | `0`               | `1`              | `01`     | `0100010001001001`    | `.T..T..T..T.T`     | `3000 ns (1.5T)`                  |

The resulting flux intervals would be a sequence of `2.0T`, `2.0T`, `2.0T`, `1.0T`, `1.0T`, `1.0T` durations.

## 3. Special Patterns: The Address Marks

A raw stream of data is useless without structure. Floppy and hard drive formats use special non-data patterns called **Address Marks** to identify the start of sectors. These marks are created by **intentionally violating the MFM encoding rules.**

The most common is the **A1 Sync Mark**. It's created by encoding the byte `0xA1` but with three clock bits deliberately omitted. This produces a flux pattern that contains a gap of `3.0T`, which is forbidden by normal MFM rules, making it unique and easy to find.

Here is a table of the most important MFM markers found on a standard IBM-formatted disk:

| Marker Name              | Hex Value | Encoded With              | Purpose                                                              |
| :----------------------- | :-------- | :------------------------ | :------------------------------------------------------------------- |
| **Index Address Mark (IAM)** | `C2`      | Missing Clock Bits        | Marks the beginning of a track, typically following a long gap.      |
| **Sync Mark**            | `A1`      | Missing Clock Bits        | Precedes every address mark (`IDAM` or `DAM`) to allow the PLL to lock. |
| **ID Address Mark (IDAM)**   | `FE`      | Standard MFM Encoding     | Marks the start of a sector's ID header (Cylinder, Head, Sector, Size). |
| **Data Address Mark (DAM)**  | `FB` / `F8` | Standard MFM Encoding | Marks the start of a sector's data field. `FB` for normal, `F8` for deleted. |
| **Gap Bytes**            | `4E` / `00` | Standard MFM Encoding | Used as filler between sectors to provide timing tolerance.          |

Searching for the `A1` sync pattern is the first step in parsing a raw MFM track into structured sectors.

## 4. Decoding Flux back to Data: The Phase-Locked Loop (PLL)

Because of motor speed variations, bit-wobble ("jitter"), and media degradation, the raw flux intervals are never perfectly timed. A **Phase-Locked Loop (PLL)** is a digital signal processing algorithm used to recover a stable clock from this noisy, imperfect input.

A PLL works by:
1.  **Maintaining a "perfect" clock:** It keeps track of the ideal bit cell period (e.g., 2000 ns for a Double Density disk).
2.  **Accumulating flux time:** It adds up the time from the incoming flux intervals.
3.  **Making bit decisions:** It compares the accumulated time against fractions of its internal clock period (`0.75T`, `1.5T`, `2.5T`) to decide if the interval represents a `1.0T`, `1.5T`, or `2.0T` MFM pattern.
4.  **Adjusting its clock:** If an incoming transition arrives slightly early or late, the PLL nudges its internal clock to match, "locking" onto the data's phase.
5.  **Handling sync loss:** If no transitions are seen for too long, the PLL assumes it has lost sync and resets its clock to the nominal center, waiting for the next sync mark.

### A Simplified PLL Algorithm

Here is a conceptual C struct and algorithm for a PLL decoder, based on implementations found in tools like Greaseweazle and FluxEngine.

```c
// State variables for the PLL
struct PLLState {
    double clock_period_ns;     // Current ideal bit cell period (e.g., 2000.0)
    double clock_centre_ns;     // Nominal period to return to after sync loss
    double clock_min_ns;        // Minimum allowed period (e.g., period * 0.8)
    double clock_max_ns;        // Maximum allowed period (e.g., period * 1.2)
    double flux_accumulator_ns; // Time accumulated since the last bit decision
};

// Main processing loop
void process_flux_interval(PLLState* pll, double interval_ns, Bitstream* output) {
    pll->flux_accumulator_ns += interval_ns;

    // Decide how many full bit cells fit into the accumulated flux time
    int full_cells = (int)(pll->flux_accumulator_ns / pll->clock_period_ns);

    // For each full cell, we emit a '0' clock bit
    for (int i = 0; i < full_cells; i++) {
        bitstream_append(output, 0);
    }
    
    // The last transition is always a '1' data bit
    bitstream_append(output, 1);

    // Calculate the phase error: how far off was the last transition?
    double remainder_ns = fmod(pll->flux_accumulator_ns, pll->clock_period_ns);
    double phase_error = remainder_ns - (pll->clock_period_ns / 2.0);

    // Adjust the PLL's clock based on the error (a simple proportional controller)
    pll->clock_period_ns -= phase_error * 0.1; // 0.1 is the gain factor

    // Clamp the clock to prevent it from drifting too far
    if (pll->clock_period_ns < pll->clock_min_ns) pll->clock_period_ns = pll->clock_min_ns;
    if (pll->clock_period_ns > pll->clock_max_ns) pll->clock_period_ns = pll->clock_max_ns;

    // Reset the flux accumulator for the next interval
    pll->flux_accumulator_ns = 0.0;
}
```

## 5. The Complete Decoding Pipeline

With all the pieces in place, here is the full pipeline for converting raw flux into usable sector data:

1.  **Read Flux Timings:** Capture the raw flux intervals from the disk using hardware like a Greaseweazle.
2.  **Convert to Time:** Convert the hardware-specific ticks into a standard unit like nanoseconds.
3.  **Run the PLL:** Feed the time intervals into the PLL algorithm to generate a continuous MFM bitstream.
4.  **Find Sync Marks:** Scan the MFM bitstream for the unique pattern corresponding to an `A1` sync mark (e.g., a sequence with a `3.0T` gap).
5.  **Align and Decode:** Once a sync mark is found, the following bits are byte-aligned. Decode the MFM pairs back into data bytes (`FE`, `FB`, etc.).
6.  **Parse Sectors:** Read the sector header (IDAM) and data block (DAM).
7.  **Validate Data:** Calculate the CRC-16 of the decoded data and compare it with the CRC bytes read from the disk to ensure data integrity.

## 6. Timing Parameters

Different disk densities use different data rates, which changes the fundamental bit cell period.

| Density                      | Unencoded Data Rate | MFM Data Rate | Bit Cell Period (`T`) | Common Use                                    |
|:-----------------------------|:--------------------|:--------------|:----------------------|:----------------------------------------------|
| **Single (SD)**              | 125 kbps            | 250 kbps      | 4000 ns (4 µs)        | Very early 8-inch disks (used FM, not MFM)    |
| **Double (DD) or Quad (QD)** | 250 kbps            | 500 kbps      | 2000 ns (2 µs)        | 360KB 5.25", 720KB 5.25", 720KB 3.5" Floppies |
| **High (HD)**                | 500 kbps            | 1 Mbps        | 1000 ns (1 µs)        | 1.2MB 5.25", 1.44MB 3.5" Floppies             |
| **Extra (ED)**               | 1 Mbps              | 2 Mbos        | 500 ns (0.5 µs)       | 2.88MB 3.5" Floppies                          |

*Note: The data rate for hard drives was often higher, e.g., 5 Mbps for the Seagate ST-506, resulting in a bit cell period of 200 ns.*

This is a fantastic question that gets to the very heart of the division of labor between the computer's controller and the disk drive itself.

The **Floppy Disk Controller (FDC)** sends the **final, serialized MFM bitstream** to the **Floppy Disk Drive (FDD)**. It does **not** send a signal representing "flux transitions."

The FDD's job is to be the simple "muscle" that translates that digital MFM bitstream into physical magnetic patterns. Let's break down the process for both writing and reading.

---

## 7. Data flow

### The Write Process: From Computer to Disk

This shows the journey of data from the PC's memory to the magnetic media.

**1. FDC's Responsibility (The "Brains")**

*   The computer's CPU commands the FDC to write a sector of data.
*   The FDC retrieves the raw data bytes (e.g., `0xA9`) from system memory (RAM).
*   **Crucially, the FDC's internal logic performs the MFM encoding.** It converts the raw `10101001` into the final MFM bitstream `0100010001001001`. It also calculates the CRC and generates the special Address Marks (`A1`, `C2`, `FE`, `FB`).
*   The FDC then serializes this MFM stream.

**2. The Signal on the 34-pin Ribbon Cable**

*   The FDC sends the MFM bitstream to the FDD over the **`WRITE DATA`** line (Pin 22).
*   This signal is a series of digital pulses timed by the channel clock (e.g., a 500 kHz clock for DD).
*   **A pulse on this line represents a `1` in the MFM bitstream. The absence of a pulse represents a `0`.**
*   Simultaneously, the FDC activates the **`WRITE GATE`** line (Pin 24) to tell the FDD, "The pulses coming down the `WRITE DATA` line are valid. Engage the write head and start recording."

**3. FDD's Responsibility (The "Muscle")**

*   The FDD receives the stream of digital pulses on the `WRITE DATA` line. It has no knowledge of MFM, bytes, or CRCs.
*   Its simple internal electronics (the "write driver") are configured so that **every time a pulse arrives, it reverses the direction of the electric current flowing through the write head.**
*   This reversal of current flips the magnetic polarity on the disk surface, creating a **flux transition**.
*   **Result:** A pulse (`1`) on the cable directly causes a flux transition on the disk. No pulse (`0`) causes no change. The FDD faithfully translates the MFM bitstream into a physical magnetic pattern.

---

### The Read Process: From Disk to Computer

Reading is the exact reverse of this process.

**1. FDD's Responsibility (The "Sensor")**

*   The read head moves over the spinning disk and detects the physical flux transitions.
*   This raw signal is a weak, noisy, analog "bump."
*   The FDD's internal amplifier and "pulse shaper" circuits clean this up. They convert each detected magnetic "bump" into a single, clean digital pulse.

**2. The Signal on the 34-pin Ribbon Cable**

*   For every flux transition it detects, the FDD sends one of these clean digital pulses back to the FDC over the **`READ DATA`** line (Pin 30).
*   The FDD doesn't know what the time between the pulses means; it just reports *when* they happen. The time intervals between these pulses will be the physical `2000 ns`, `3000 ns`, or `4000 ns` timings.

**3. FDC's Responsibility (The "Decoder")**

*   The FDC receives this stream of timed pulses from the FDD.
*   **This is where the FDC's internal Phase-Locked Loop (PLL) and Data Separator circuits take over.**
*   The FDC measures the time intervals between the incoming pulses.
*   Based on these timings, it reconstructs the original MFM bitstream (`...10001...`).
*   It then decodes this MFM bitstream back into raw data bytes (`...0x49...`).
*   Finally, it verifies the CRC and places the validated data bytes into the computer's memory.

### Summary: Who Does What?

| Task                                             | Performed By the FDC (Controller on Motherboard/Card) | Performed By the FDD (The Drive Unit) |
|:-------------------------------------------------| :---------------------------------------------------- | :------------------------------------ |
| MFM Encoding (Data → MFM Bits)                   | **Yes**                                               | No                                    |
| CRC Calculation and Address Mark Generation      | **Yes**                                               | No                                    |
| Sending the MFM Bitstream to the drive           | **Yes**                                               | No                                    |
| **Translating MFM pulses into flux transitions** | No                                                    | **Yes**                               |
| **Detecting flux transitions from the disk**     | No                                                    | **Yes**                               |
| Translating flux detections into timed pulses    | No                                                    | **Yes**                               |
| PLL & Data Separation (Timed Pulses → MFM Bits)  | **Yes**                                               | No                                    |
| MFM Decoding (MFM Bits → Data)                   | **Yes**                                               | No                                    |

## 8. Historical References and Further Reading

The standards for MFM were established by the industry leaders of the time. For definitive technical details, refer to the original documentation:

*   **Shugart Associates SA400 Minifloppy Diskette Drive OEM Manual:** The SA400 was the pioneering 5.25" drive. Its documentation outlines the fundamental principles of track formats and timing.
*   **IBM PC/AT Technical Reference Manual (1984):** This document solidified the MFM format for the personal computer, defining the sector layouts, address marks, and gaps that became the de facto standard.
*   **Intel 8272 / NEC µPD765 Floppy Disk Controller (FDC) Datasheet:** This was the most common FDC chip used in PCs. Its datasheet is a treasure trove of information on how MFM commands, track formatting, and CRC calculation were implemented in hardware.
*   **Western Digital WD1770 / WD279x Family FDC Datasheets:** Western Digital was another major producer of FDC chips, and their documentation provides an alternative and comprehensive view of MFM implementation.
*   **Seagate ST506/412 OEM Interface Specification:** For MFM hard drives, this document defined the standard for data transfer, signaling, and MFM encoding at a much higher data rate (5 megabits/second).

By studying these original sources and applying the principles of flux decoding, it is possible to reconstruct data from vintage magnetic media with high fidelity.