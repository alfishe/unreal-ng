## ZX Spectrum I/O Port Fundamentals

The Z80 microprocessor uses a separate 16-bit address space for I/O ports, accessed via `IN` and `OUT` instructions. The lower 8 bits of the address (A0-A7) are typically used to select the specific port, and the upper 8 bits (A8-A15) can also be used, though often they are not fully decoded, leading to "mirrored" ports.

A crucial aspect for the original ZX Spectrum hardware (and many clones) is that the ULA (Uncommitted Logic Array, the main custom chip) responds to I/O requests **only when address line A0 is LOW**.

*   **Even Ports (A0=0):** Typically interact with the ULA or ULA-like functions.
*   **Odd Ports (A0=1):** Typically interact with other peripheral devices or memory control logic.

**Partial Decoding:**
Many peripherals don't decode all 8 (or 16) address lines.
For example, if a device only checks A0 and A7:
*   `#FE` (binary `xxxxxxx0` for A0, `1xxxxxxx` for A7 being high is common in Z80 systems where BC is the port address) might be the intended port.
*   The `DECODING` column in your source file (`xxxxxxxxxxxxxxx0` for ULA on port `#FE`) shows the ULA only cares that A0 is low. Other bits in the lower byte (A1-A7) are used by the ULA internally to select functions (keyboard, border, sound, tape). The upper byte (A8-A15) is irrelevant (`x`). This means `#00FE`, `#01FE`, ..., `#FFFE` would all address the ULA in the same way.

**Floating Bus / Contention:**
Reading from a port that no device actively drives can result in reading the last value present on the data bus. On the Spectrum, this is often data being fetched by the ULA for screen display (pixel and attribute data). Writing to an unmapped port usually has no effect. Some systems have pull-up/pull-down resistors that might return a default value like `#FF`.

---

## 1. Sinclair ZX Spectrum 48K (Models: 1, +1)

The original ZX Spectrum 48K (Issue 1-3, and the later "Spectrum+" with the same internals but a new keyboard) has a relatively simple I/O map, primarily centered around the ULA.

**Key:**
*   `(1)`: Refers to ZX Spectrum Issue 1-2.
*   `(+1)`: Refers to ZX Spectrum Issue 3-6 (often just called Spectrum 48K). Functionally very similar for ports.

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111110` (A0 is low)
    *   **DECODING (ULA responds to):** `xxxxxxxxxxxxxxx0` (ULA only cares that A0 is low)
    *   **READ Operations (from ULA):**
        *   Input from Kempston-compatible joysticks (if bit 5 of the high byte of the port address is 0, though Kempston is usually `#1F`).
        *   Keyboard scanning: Bits A8-A15 of the port address select the keyboard matrix half-rows.
            *   `IN A, (C)` with `BC = #xxFE`. The value of `B` determines which rows are scanned.
            *   Bits D0-D4 return the state of keys on the selected rows (0 for pressed).
            *   D5: Not used for keyboard.
            *   D6: Tape Input signal (EAR).
            *   D7: Always 1 on Issue 2+ Spectrums; SYNC signal on Issue 1 (rarely used).
    *   **WRITE Operations (to ULA):**
        *   Bits D0-D2: Border Color.
            *   `000` Black, `001` Blue, `010` Red, `011` Magenta, `100` Green, `101` Cyan, `110` Yellow, `111` White.
        *   Bit D3: MIC Output. Controls the pulse output for tape saving and can be used for sound.
        *   Bit D4: Speaker Output (Beeper). Combined with MIC for sound effects.
        *   Bits D5-D7: Not used.
    *   **Notes:**
        *   The `KeyTp(1,7-9)Prn(7)BrdTpSpk(1,7-9)` and similar in the source table indicate the *functions* associated with this port across various machines, with the numbers in parentheses linking to the "COMPUTERS" legend. For 48K models (1, +1), it's Keyboard, Tape, Border, Speaker.
        *   The various decoding masks like `xxxxxxxxxx1xxx10` usually refer to specific functions *within* the ULA based on other address lines (like A1-A4 for border/mic/speaker, or A8-A15 for keyboard rows when reading).

*   **Port `#FF` (255 decimal) - Floating Bus / Attributes (Unofficial Read)**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111111` (A0 is high)
    *   **DECODING:** The ULA does *not* respond to this port because A0 is high.
    *   **READ Operations:**
        *   Reading from this port (or any odd-addressed port not claimed by other hardware) typically returns the last byte read by the ULA from video memory for screen display. This is often an attribute byte. This behavior is not officially documented as a feature for reading attributes but is a well-known side effect.
        *   `Atr(1-2)` in the source table indicates this behavior for original Spectrums.
    *   **WRITE Operations:** No official function. Writes are ignored.

### Peripherals Ports (relevant for 48K with add-ons)

Many peripherals for the 48K Spectrum use their own specific ports. The table lists several generic peripheral ports.

*   **Port `#1F` (31 decimal) - Kempston Joystick Interface**
    *   **ADDRESS:** `xxxxxxxx00011111`
    *   **DECODING (Kempston Interface):** `xxxxxxxxxx0xxxx1` or similar, ensuring A0=1 and specific bits for `#1F`.
    *   **READ:** Returns joystick state:
        *   D0: Right
        *   D1: Left
        *   D2: Down
        *   D3: Up
        *   D4: Fire
        *   D5-D7: Often unused, or used for additional buttons/mice on extended Kempston interfaces.
    *   **WRITE:** Not used by standard Kempston joystick.

*   **Other Ports (e.g., `#FB` for Printer, `#E7`/`#EF`/`#F7` for ZX Interface 1):**
    These are for specific add-on hardware and would be documented with that hardware. The 48K itself doesn't use them.

**Similarities/Differences with later models:**
The `#FE` ULA port behavior is foundational. Later models expand capabilities but often keep the core `#FE` functions for backward compatibility, though some bit meanings (like EAR/MIC for sound output on 128K machines) might change. The 48K has no built-in memory paging via ports, no AY sound chip, and no integrated disk controller.

---

## 2. Sinclair ZX Spectrum 128K, +2 (Amstrad Grey)

These models introduced significant enhancements: 128KB RAM, an AY-3-8912 sound chip, and more sophisticated memory management.

**Key:**
*   `(2)`: Refers to ZX Spectrum +128 (Toastrack) and +2 (Amstrad Grey).

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Largely compatible with the 48K ULA for keyboard, border, and tape input.
    *   **Sound Output:** On 128K machines, the MIC and EAR bits are often combined and routed through the AY chip's audio output circuitry for beeper sounds, providing a cleaner sound than the 48K's direct speaker.
    *   The `KeyTpSpk` and `BrdSpk` entries apply.

*   **Port `#7FFD` (32765 decimal) - Memory Paging Control**
    *   **ADDRESS (CPU outputs):** `0111111111111101`
    *   **DECODING (Memory Controller):** `0xxxxxxxxxxxxx0x` (A0=1, A15=0, A1 ignored). This port is critical for managing the 128KB RAM.
    *   **WRITE Operations:**
        *   **Bits D0-D2 (RAM Bank):** Selects which 16KB RAM bank (0-7) is paged into the top of the address space (`#C000-#FFFF`).
        *   **Bit D3 (Screen Select):**
            *   `0`: Normal screen (RAM bank 5 at `#4000`).
            *   `1`: Shadow screen (RAM bank 7 at `#4000`).
        *   **Bit D4 (ROM Select):**
            *   `0`: Selects the 128K Editor ROM (ROM 0).
            *   `1`: Selects the 48K BASIC ROM (ROM 1).
        *   **Bit D5 (Paging Disable Lock):**
            *   If set to `1`, further writes to `#7FFD` are ignored until a reset. This locks the current memory configuration. Must be `0` to change paging.
        *   Bits D6-D7: Not used.
    *   **READ Operations:** No official function; reading might return floating bus data or the last written value on some hardware.
    *   **Notes:** Indicated by `Pag(2,8,9,A)` in the source table. The numbers show this port is also used by Pentagon 128, Profi, and ATM Turbo 2+ for their basic 128K compatibility mode.

*   **Port `#FFFD` (65533 decimal) - AY-3-8912 Sound Chip Register Select**
    *   **ADDRESS (CPU outputs):** `1111111111111101`
    *   **DECODING (AY Chip Logic):** `11xxxxxxxxxxxx0x` (A0=1, A15=1, A1 ignored, specific decoding for `#FFFD` usually means A14=1 for AY).
    *   **WRITE Operations:** Selects which of the AY chip's 16 registers will be written to or read from via port `#BFFD`.
    *   **READ Operations:** No direct data, but may reflect last written register index on some clones.
    *   **Notes:** Indicated by `AYadr(2,3,?5,A)` and similar.

*   **Port `#BFFD` (49149 decimal) - AY-3-8912 Sound Chip Data Port**
    *   **ADDRESS (CPU outputs):** `1011111111111101`
    *   **DECODING (AY Chip Logic):** `10xxxxxxxxxxxx0x` (A0=1, A15=1, A1 ignored, specific decoding for `#BFFD` usually means A14=0 for AY data).
    *   **WRITE Operations:** Writes data to the AY register previously selected via port `#FFFD`.
    *   **READ Operations:** Reads data from the AY register previously selected via port `#FFFD`.
    *   **Notes:** Indicated by `AYdat(2,3,?5,A)` and similar.

**Similarities with 48K:**
*   Core `#FE` ULA functionality for keyboard, border, tape input.

**Differences from 48K:**
*   Addition of `#7FFD` for extensive memory paging.
*   Addition of AY sound chip ports (`#FFFD`, `#BFFD`).
*   Modified sound output path for beeper via AY chip.
*   No built-in disk controller (though the +2 had a built-in Datacorder).

---

## 3. Sinclair ZX Spectrum +2A, +2B, +3 (Amstrad Black)

These models further refined the 128K architecture, with the +3 notably including a 3" floppy disk drive and a different ROM. The memory paging was also enhanced.

**Key:**
*   `(3)`: Refers to ZX Spectrum +2A, +2B.
*   `(+3)`: Refers to ZX Spectrum +3.

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Behavior is very similar to the 128K models.
    *   The source table's `KeyTp(+1) BrdTpSpk(+1)` for `xxxxxxxx1xxxxxx0` implies the core functions, and specific entries for `PagPrn/FD(3/+3)` suggest some ULA lines might be repurposed or involved in printer/floppy logic on these models, though the primary control is elsewhere. This usually means specific address lines in the upper byte (when accessing port FE) might be sensed by other chips.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging Control**
    *   **WRITE Operations:**
        *   This port functions largely as on the 128K (Toastrack/+2 Grey) for basic 128K compatibility mode (RAM banks 0-7, screen select, ROM 0/1 select, paging disable).
        *   **Special Paging Mode (+2A/+3):** The behavior of bits 0-2 can be altered by port `#1FFD`. When port `#1FFD` bit 0 is set, the machine enters a "special" paging mode allowing access to all four 16KB ROMs and different RAM configurations required for the +3DOS.
            *   RAM banks 0-7.
            *   Screen in RAM 5 (normal) or RAM 7 (shadow).
            *   ROM select (0-3 for +3/+2A when in special mode, 0-1 otherwise).
            *   Paging disable bit.
    *   **Notes:** `Pag(2,8,9,A)` and `Pag(3,?5)` from the source.

*   **Port `#1FFD` (8189 decimal) - Secondary Paging / Disk Control (+2A/+2B/+3 only)**
    *   **ADDRESS (CPU outputs):** `0001111111111101`
    *   **DECODING (Memory Controller/Disk Logic):** `0001xxxxxxxxxx0x` (A0=1, A15=0, A14=0, A13=0, A12=1, A1 ignored).
    *   **WRITE Operations:**
        *   **Bit 0 (Paging Mode):**
            *   `0`: Normal mode (compatible with 128K paging via `#7FFD`).
            *   `1`: Special mode (unlocks all 4 ROMs and potentially different RAM configurations for +3DOS, changes interpretation of `#7FFD` bank selection slightly).
        *   **Bit 1 (Disk ROM Mapping):**
            *   `0`: +3DOS ROM is mapped at `#0000`.
            *   `1`: Normal ROM (BASIC/Editor) is mapped at `#0000`.
        *   **Bit 2 (Printer Port Strobe +2A/+2B / Disk Motor +3):**
            *   On +2A/+2B: Printer Port Strobe signal.
            *   On +3: Controls the floppy disk drive motor (0 = Motor ON, 1 = Motor OFF).
        *   **Bit 3: Not used / (Printer port related on some sub-models).**
        *   **Bit 4 (Disk Side Select on +3 / Printer Port related on +2A/+2B):**
            *   On +3: Selects disk side for WD1772.
        *   Bits 5-7: Not used.
    *   **READ Operations:** On the +3, reading this port can return status from the printer interface or disk controller.
    *   **Notes:** `PagPrn/FD(3/+3)` in the source.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Function identically to the 128K models for AY sound chip control.
    *   `AYadr(2,3,?5,A)` and `AYdat(2,3,?5,A)`.

### Peripherals Ports (Primarily for +3)

*   **Port `#2FFD` (12285 decimal) - Floppy Disk Controller (WD1772) Status Register (+3 only)**
    *   **ADDRESS:** `0010111111111101`
    *   **DECODING:** `0010xxxxxxxxxx0x`
    *   **READ:** Reads the status register of the WD1772 FDC.
    *   **WRITE:** (Listed as `8272status(?+3)`) The WD1772 doesn't have a writable status register in this sense. This might refer to a control register for an associated chip (like an 8272 which is a different FDC, so this could be a slight mislabel in source or for a variant). For WD1772, this port is typically read-only for status.
    *   **WRITE (WD1772):** The main command register is at `#3FFD`.

*   **Port `#3FFD` (16381 decimal) - Floppy Disk Controller (WD1772) Command/Data (+3 only)**
    *   **ADDRESS:** `0011111111111101`
    *   **DECODING:** `0011xxxxxxxxxx0x`
    *   **READ:** Reads data from the WD1772 (e.g., sector data, register status after a command).
    *   **WRITE:** Writes commands or data to the WD1772 (e.g., seek, read sector, write sector, format track).
    *   **Notes:** `8272data(?+3)` in the source table. The WD1772 is more common on the +3 than the 8272. This port is the main interface to the FDC.

**Similarities with 128K:**
*   Core `#FE` ULA functions.
*   Basic `#7FFD` paging for 128K compatibility.
*   AY sound chip and its ports.

**Differences from 128K:**
*   Enhanced paging capabilities via `#1FFD` to manage 4 ROMs and different RAM configurations.
*   Integrated floppy disk controller (WD1772 on +3) using ports like `#1FFD`, `#2FFD`, `#3FFD`.
*   Different ROM set (+3DOS).
*   Printer port behavior/control might be slightly different due to integration with `#1FFD`.

---

This covers the main Sinclair models. I will proceed with the clones in subsequent messages if you're ready. The analysis for each clone will be quite extensive, referencing the codes `(4, 5, 6, 7, 8, 9, A, B, C, D)` from your document. Please let me know how you'd like to proceed!Okay, let's continue with the ZX Spectrum clones, building upon the established Sinclair models.

---

## 4. Timex Computer 2048 (and related Timex Sinclair models like TS2068)

Timex models were official licensed clones/derivatives, primarily sold in the USA and Portugal. They introduced hardware differences, most notably enhanced video modes.

**Key:**
*   `(4)`: Refers to Timex Computer 2048 (TC2048). TS2068 is similar but has AY sound and cartridge port differences. The source table focuses on TC2048 for port `#FF`.

### System Ports

*   **Port `#FE` (254 decimal) - ULA-like Port**
    *   Largely compatible with the Sinclair 48K ULA for keyboard, border, tape I/O, and beeper sound.
    *   The Timex custom chip (SCLD - Screen, CPU, Logic Device, or similar custom logic) handles these functions.
    *   The source table implies standard ULA behavior: `KeyTp(+1) BrdTpSpk(+1)` for `xxxxxxxx1xxxxxx0`.

*   **Port `#FF` (255 decimal) - Extended Video / Paging Control**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111111`
    *   **DECODING (Timex SCLD/logic):** `xxxxxxxx11111111` (The `????????` in the source table's `xxxxxxxx???????? Atr(4,5) Vid/Pag(4,5)` becomes `11111111` because the port is defined as `#FF`).
    *   **WRITE Operations:** This port is used to control Timex-specific features:
        *   **Enhanced Video Modes:** The TC2048/TS2068 offered higher resolution or more colorful modes than the standard Spectrum. Port `#FF` was used to select these modes or modify screen parameters.
            *   Bit 6: Selects 64-column text mode (common on TS2068, less so or emulated on TC2048).
            *   Bit 7: Screen memory location select (RAM bank for screen).
        *   **Memory Paging/ROM Selection:**
            *   The TS2068 had a cartridge port and more complex ROM banking (EXROM, HOME ROMs). Port `#FF` was involved in selecting which ROM was active.
            *   **DOCK (RAM bank 0):** Pages in the lower 16K RAM bank (often called DOCK RAM) into `#0000-#3FFF`.
            *   **EXROM:** Activates the external cartridge ROM.
            *   **HOME:** Activates the internal Timex BASIC ROM.
        *   The exact bit assignments can vary slightly between TC2048 and the more advanced TS2068.
    *   **READ Operations:**
        *   `Atr(4,5)`: May still return attribute-like data as per the floating bus, but specific Timex models might have defined reads.
    *   **Notes:** The source indicates `Atr(4,5)` for reads and `Vid/Pag(4/5)` for writes. This is a key differentiator for Timex machines.

*   **AY Sound Ports (`#F4`-`#F6` or `#FFFD`/`#BFFD` on some variants):**
    *   The TS2068 (US version of TC2068) included an AY-3-8910/8912 sound chip.
    *   The port addresses for AY sound on Timex machines are **different** from Sinclair 128K:
        *   **Port `#F4` (TS2068, some TC2048 variants):** AY Data Read.
        *   **Port `#F5` (TS2068, some TC2048 variants):** AY Register Select (Write-only).
        *   **Port `#F6` (TS2068, some TC2048 variants):** AY Data Write.
    *   The source table doesn't explicitly list these under Timex (`4`), but they are a known feature of models like the TS2068. If a TC2048 has AY sound, it would likely use these ports or Sinclair-compatible ones if modified.

**Similarities with Sinclair 48K:**
*   Base functionality of port `#FE` (keyboard, border, tape, beeper).
*   Standard Spectrum screen mode is one of the available modes.

**Differences from Sinclair 48K/128K:**
*   **Port `#FF`:** Used for advanced video modes and memory/ROM paging, not just floating bus reads.
*   **AY Sound (TS2068):** Present, but uses different port addresses (`#F4-F6`) than the Sinclair 128K's `#FFFD/#BFFD`. TC2048 generally did not have AY sound unless modified.
*   Different memory map and ROMs.
*   No standard 128K paging via `#7FFD`. Paging is controlled via `#FF`.

---

## 5. Didaktik Gama

The Didaktik Gama was a Czechoslovakian clone, broadly compatible with the 48K Spectrum but with some extensions, particularly an 80KB RAM model and an optional disk interface (Didaktik 40/80).

**Key:**
*   `(5)`: Refers to Didaktik Gama. The `?5` in the source (e.g., `AYdat(2,3,?5,A)`) indicates uncertainty by the original author about whether that specific port/function applies to the Didaktik Gama.

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K Spectrum ULA functionality for keyboard, border, tape, beeper.
    *   The source implies standard behavior.

*   **Port `#FF` (255 decimal) - Attributes / Possible Video/Paging (Gama '89)**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111111`
    *   **DECODING (Didaktik Logic):** `xxxxxxxx11111111` (for the `Vid/Pag(4/5)` entry).
    *   **READ Operations:** `Atr(4,5)` suggests the standard 48K floating bus attribute read.
    *   **WRITE Operations:** `Vid/Pag(4/5)` indicates that, similar to Timex, some Didaktik Gama models (likely the later 80KB RAM versions like Gama '89) might use this port for video enhancements or to control the extra RAM bank.
        *   The Gama '89 had 80KB RAM (64KB main + 16KB in a separate bank). Port `#FF` (or another specific Didaktik port) would be used to switch this additional 16KB bank. Often, bit 7 of port `#FF` was used.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   The source table entry `AYdat(2,3,?5,A)` and `AYadr(2,3,?5,A)` with `?5` suggests that AY sound might not be standard on all Didaktik Gama models or that its port usage wasn't confirmed by the author for this model.
    *   Some Didaktik models or add-ons (like the Melodik interface) provided AY sound using the standard Sinclair 128K ports. If AY sound is present and Sinclair-compatible, these ports would be used.

### Peripherals Ports (Didaktik Specific)

*   **Didaktik 40/80 Disk Interface (D40/D80):**
    This was a common floppy disk interface for Didaktik computers. It used the WD2797 FDC.
    *   **Ports `#81, #83, #85, #87` (WD2797 Registers):**
        *   **ADDRESS:** `xxxxxxxx10000BA1`
        *   **DECODING:** `xxxxxxxx10000BA1`
        *   `#81` (Status/Command), `#83` (Track), `#85` (Sector), `#87` (Data). These map directly to the WD2797's registers. A0 is 1, B selects the register.
        *   `B=00` (port `#81`): WD Command (Write) / Status (Read)
        *   `B=01` (port `#83`): WD Track Register (Read/Write)
        *   `B=10` (port `#85`): WD Sector Register (Read/Write)
        *   `B=11` (port `#87`): WD Data Register (Read/Write)
    *   **Ports `#89, #8B, #8D, #8F` (FDsys - Floppy Disk System Control):**
        *   **ADDRESS:** `xxxxxxxx10001xx1`
        *   **DECODING:** `xxxxxxxx10001xx1`
        *   **WRITE:** Control signals for the drive: Drive select, motor on/off, side select, density.
        *   **READ:** Status signals from the drive.
    *   **Port `#91` (145 decimal) - 8255 Off & Reset:**
        *   **WRITE:** Disables the 8255 PPI (if present with the D40/80) and resets FDC.
    *   **Port `#99` (153 decimal) - 8255 On:**
        *   **WRITE:** Enables the 8255 PPI.
    *   **Ports `#1F, #3F, #5F, #7F` (8255 PPI):**
        *   If the D40/80 interface includes an 8255 PPI (Programmable Peripheral Interface), these ports would be used to access its Port A, Port B, Port C, and Control Word registers, respectively. Decoding `xxxxxxxx0BAxxxxx`.
        *   The 8255 was often used for parallel printer ports or other I/O.

**Similarities with Sinclair 48K:**
*   Base functionality of port `#FE`.
*   Standard Spectrum screen mode.
*   Floating bus behavior on `#FF` for basic models.

**Differences from Sinclair 48K/128K:**
*   **RAM Extension (Gama '89):** Specific models had 80KB RAM, likely paged using `#FF` or another Didaktik-specific port (not `#7FFD`).
*   **Disk Interface (D40/D80):** Unique set of ports (`#8x`, `#9x`) for its WD2797-based FDC and optional 8255. This is different from the +3's FDC ports and from Beta Disk.
*   AY Sound was not standard on all models; if present, likely via an add-on.

---

## 6. Scorpion ZS256 Turbo+ / Scorpion GMX

The Scorpion ZS256 was a popular Russian clone known for its extended memory (256KB or more), Turbo mode, built-in ProfROM (with tools like a service monitor, test utilities), and often a built-in Beta Disk Interface controller. The GMX (Graphics Memory eXtension) version added enhanced video capabilities.

**Key:**
*   `(6)`: Refers to Scorpion ZS256 Turbo+.
*   `(?B)` or `(B)`: Refers to Scorpion GMX (often just 'B' in the table).

### System Ports (Scorpion ZS256)

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality for keyboard, border, tape, beeper. Sound output often routed via AY.
    *   The table entry `KeyTpPrn(6) BrdTpSpk(6)` for decoding `xxxxxxxxxx1xxx10` confirms this.
    *   "Prn(6)" suggests some printer functionality might also be tied to certain ULA lines/decodings, or a parallel port mapped to interact with ULA signals.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging (128K Compatibility)**
    *   **WRITE:** Used for standard 128K memory paging (RAM banks 0-7, ROM select 128/48K, screen select). This provides compatibility with 128K software.
        *   Bit 5 (Paging Lock) has an extended meaning on Scorpion:
            *   `0`: Paging enabled, bits 0-4 control standard 128K paging.
            *   `1`: Paging "locked" but enables extended Scorpion features. Allows access to ProfROM service monitor if bit 4 is also set.
    *   **Notes:** `Pag(6)` and `Trb-ON(6)` / `Trb-OFF(6)` for decoding `01xxxxxxxx1xxx01` / `00xxxxxxxx1xxx01` respectively.
        *   **Turbo Mode:** A specific bit combination or interaction with `#7FFD` (often bit 6 if address line A6 is used in decoding this port, or related to the paging lock bit) controls the CPU's Turbo (7MHz) mode. The table suggests `A12=1` for Turbo-ON and `A12=0` for Turbo-OFF when other bits of `#7FFD` are involved. More commonly, Turbo is controlled by a shadow port or another specific port.
        *   The `Trb-ON(6)` / `Trb-OFF(6)` on port `#1FFD` seems more typical for Scorpion's turbo control related to TR-DOS ROM paging.

*   **Port `#1FFD` (8189 decimal) - Extended Paging / Service Monitor / TR-DOS ROM**
    *   **WRITE:**
        *   **Bits 0-4:** Selects which 16KB RAM page (0-15 for 256KB) is mapped to `#C000-#FFFF`.
        *   **Bit 5 (Shadow RAM):** Enables/disables shadow RAM for screen 0 (RAM page 0 often shadowed by RAM page 8).
        *   **Bit 6 (TR-DOS ROM / Turbo Control):**
            *   `0` (Trb-OFF(6)): Basic ROM (e.g., 128K BASIC) active at `#0000`. Normal speed.
            *   `1` (Trb-ON(6)): TR-DOS ROM (part of ProfROM) active at `#0000`. Can also enable Turbo mode.
        *   **Bit 7 (Service Monitor):** Activates the built-in Service Monitor ROM (part of ProfROM).
    *   **Notes:** `Pag(6)`. The TR-DOS ROM and Service Monitor are key features of the Scorpion's ProfROM.

*   **Port `#DFFD` (57341 decimal) - (Scorpion GMX specific or further RAM paging)**
    *   **ADDRESS:** `1101111111111101`
    *   **DECODING:** `xx0xxxxxxxxxxx0x` (A14 must be 0).
    *   **WRITE:** `Pag(9)`. This implies it's used for paging by Profi machines (`9`). For Scorpion (`6`), the table doesn't explicitly list it, but it's grouped with `#1FFD`, `#7FFD`. It might be used for accessing RAM beyond 256KB if the Scorpion has 512KB/1024KB, or for GMX specific video RAM.
    *   The `Pag(?B)` for `1x0xx111xx1xxx01` under this port suggests it might be used by Scorpion GMX for its extended graphics memory paging.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(6)`, `AYadr(6)`.

### Shadow Ports (Scorpion ZS256)

Scorpions make extensive use of "shadow ports" – ports that are not part of the standard Spectrum set but are decoded by the Scorpion hardware, often for system control.

*   **Port `#xx77` (various, e.g. `#77`, `#BD77`, `#BF77`, `#FD77`, `#FE77`):** These are shadow ports used for advanced system control. The higher bits of the port address (`xx`) select specific functions.
    *   **`#77`:** `VidTrbReg(A)` - ATM specific, may not apply directly to Scorpion unless for compatibility.
    *   **`#BD77` / `#0177` (Software alias):** `L,K=0->Pal,PLLFC,Shdw-on` (Scorpion GMX related).
        *   Write-only. Controls palette, PLL frequency control, and shadow mode activation.
    *   **`#BF77`:** `L=0 ->Pal,PLLFC -on` (Scorpion GMX related).
    *   **`#FD77` / `#4177` (Software alias):** `K=0 -> Shdw-on` (Scorpion GMX related).
    *   **`#FE77`:** `J=0>Pag-off,CP/Mrom>CPU0-3` (Scorpion GMX related). Turns off paging, maps CP/M ROM.
    *   These ports indicate complex hardware features on the GMX, likely for its advanced graphics (palette changes, sync control) and extended operational modes (CP/M).

### Scorpion GMX Specific Ports (Extension of ZS256)

The Scorpion GMX (Graphics Memory eXtension) model added enhanced video capabilities.

*   **Port `#7AFD` (31485 decimal) - Video Page Select (GMX)**
    *   **ADDRESS:** `0111101011111101`
    *   **DECODING:** `0x1xx010xx1xxx01`
    *   **WRITE:** `Vid(?B)`. Selects the video page for the GMX's extended graphics modes (e.g., 16 colors per pixel, hardware multicolor). The GMX often had separate video RAM or more complex ways to map main RAM to its enhanced display.

*   **Port `#7CFD` (31997 decimal) - Video Mode Control (GMX)**
    *   **ADDRESS:** `0111110011111101`
    *   **DECODING:** `0x1xx100xx1xxx01`
    *   **WRITE:** `Vid(?B)`. Configures GMX specific video modes, resolution, color depth.

*   **Port `#7EFD` (32509 decimal) - Raster Control / Palette (GMX)**
    *   **ADDRESS:** `0111111011111101`
    *   **DECODING:** `0x1xx110xx1xxx01`
    *   **WRITE:** `Rag(?B)` (likely "Raster" or "Register"). Used for fine-tuning display (e.g., hardware scrolling, raster effects) or accessing GMX palette registers.

### Built-in Peripherals (Common on Scorpion)

*   **Beta Disk Interface (TR-DOS):** Scorpions usually had a built-in WD1793 compatible FDC.
    *   **Ports `#1F, #3F, #5F, #7F` (WD1793 Registers):**
        *   **DECODING:** `xxxxxxxx0BAxxx11` (for models 6,7,8,D).
        *   `#1F` (Cmd/Stat), `#3F` (Track), `#5F` (Sector), `#7F` (Data).
    *   **Port `#FF` (Beta Disk System Register):**
        *   **DECODING:** `xxxxxxxx1xxxxx11` (for models 6,7,8,D).
        *   **WRITE/READ:** Drive select, side select, TR-DOS ROM enable/disable, etc. `FDsys(6,7,8,D)`.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD` for compatibility.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **Extended Memory:** Up to 256KB (or more) accessed via `#1FFD` and potentially `#DFFD`.
*   **Turbo Mode:** CPU can run at 7MHz, controlled via `#1FFD` or shadow ports.
*   **ProfROM:** Built-in ROM with Service Monitor and TR-DOS, managed by `#1FFD` and `#7FFD`.
*   **Shadow RAM:** For flicker-free screen updates, often controlled by `#1FFD`.
*   **GMX Extensions (on GMX model):** Additional ports (`#7AFD`, `#7CFD`, `#7EFD`, shadow `#xx77` ports) for enhanced graphics, palettes, and video memory paging.
*   **Built-in Beta Disk Interface:** Standard on most Scorpions, using its specific set of ports.

---
Okay, let's continue with the next set of ZX Spectrum clones.

---

## 7. KAY-1024SL / Beta Turbo

The KAY series were Russian clones, with the KAY-1024SL being a prominent model featuring up to 1024KB of RAM, a "Beta Turbo" (likely referring to an integrated Beta Disk Interface with turbo capabilities or a CPU turbo mode), and often other enhancements like a CMOS clock.

**Key:**
*   `(7)`: Refers to KAY-1024SL/Beta Turbo.

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality: keyboard, border, tape, beeper. Sound often routed via AY.
    *   The source table entry `KeyTp(1,7-9)Prn(7)BrdTpSpk(1,7-9)` and particularly `KeyTp(C) BrdTpSpk(C)` for `xxxxxxxx1xx11xx0` shows its ULA compatibility. `Prn(7)` indicates printer functionality might be associated with certain ULA address line decodings on KAY.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging (128K Compatibility & Extensions)**
    *   **WRITE:**
        *   Provides standard 128K memory paging (RAM banks 0-7, ROM select 128/48K, screen select) for compatibility.
        *   **KAY Extensions:** Given the 1024KB RAM capability, this port, or more likely additional ports like `#1FFD`, would be used to manage the upper RAM banks. The locking bit (D5) behavior might be extended as seen in other clones to enable access to further paging registers or modes.
    *   **Notes:** `Pag(7)` is explicitly listed for `00xxxxxxxxxxxx01` (when A15=0), and `Pag(D)` (Pentagon 1024SL) for the same decoding, implying KAY shares some base 128K paging logic.
    *   The line `01xxxxxxxxxxxx01 Pag(7)` specifically for `DECODING` suggests KAY uses A15=0 for its main 128K-style paging with `#7FFD`.

*   **Port `#1FFD` (8189 decimal) - Extended Memory Paging / TR-DOS ROM**
    *   **WRITE:**
        *   This port is crucial for managing RAM beyond the base 128KB/256KB and for TR-DOS integration.
        *   Selects RAM banks for `#C000-#FFFF`. With 1024KB, this port would be heavily used for bank switching.
        *   Often controls TR-DOS ROM mapping at `#0000`.
        *   May control Turbo CPU mode.
    *   **Notes:** The table lists `Pag(7)` for decoding `00xxxxxxxxxxxx01`, again linking KAY to this port for memory management.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for the AY-3-8912 sound chip.
    *   The source table lists `AYdat(7)` and `AYadr(7)` for KAY.
    *   `#BFFD` decoding: `10xxxxxxxxxxxx01`
    *   `#FFFD` decoding: `11xxxxxxxxxxxx01`

### Built-in Peripherals (Common on KAY)

*   **Beta Disk Interface (TR-DOS):** KAY models usually included a built-in WD1793 compatible FDC.
    *   **Ports `#1F, #3F, #5F, #7F` (WD1793 Registers):**
        *   **DECODING:** `xxxxxxxx0BAxxx11` (for models 6,7,8,D).
        *   `#1F` (Cmd/Stat), `#3F` (Track), `#5F` (Sector), `#7F` (Data).
    *   **Port `#FF` (Beta Disk System Register):**
        *   **DECODING:** `xxxxxxxx1xxxxx11` (for models 6,7,8,D).
        *   **WRITE/READ:** Drive select, side select, TR-DOS ROM enable/disable, etc. `FDsys(6,7,8,D)`.

*   **CMOS Real-Time Clock (RTC):**
    *   KAY models often featured an RTC. The ports for this are not explicitly detailed under KAY in the "SYSTEM PORTS" but might be covered by generic RTC port entries like "GLUK CMOS RTC" (`#BFF7`, `#DFF7`, `#EFF7`) or DS1685RTC (`#DFBA` under SMUC) if a similar chip was used. Without specific KAY RTC port info in this table, it's hard to pinpoint. Often, RTCs used two ports, one for address and one for data.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD` for initial compatibility.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **Massive RAM Extension:** Up to 1024KB, requiring significantly more complex paging logic primarily via `#1FFD` and potentially other KAY-specific ports not fully detailed in this generic table.
*   **Built-in Beta Disk Interface:** Standard, using its specific set of ports.
*   **Turbo Mode:** Likely present, control mechanism probably via `#1FFD` or a dedicated port.
*   **Optional CMOS RTC:** Using its own set of I/O ports.

---

## 8. Pentagon 128 (1991) & D. Pentagon-1024SL (ver. 28.09.2006)

The Pentagon series were extremely popular Russian clones, known for their slightly different ULA timings (leading to the "Pentagon effect" in demos), and later models like the 1024SL expanded RAM significantly. They formed a major branch of the Spectrum clone scene.

**Key:**
*   `(8)`: Refers to Pentagon 128 (original 1991 version or similar).
*   `(D)`: Refers to Pentagon-1024SL (often newer, more advanced versions).

### System Ports (Pentagon 128 and Pentagon-1024SL)

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality. Pentagon's ULA (or discrete logic equivalent) had timing differences affecting some software, especially demos.
    *   The table implies standard function mappings: `KeyTp(1,7-9)Prn(7)BrdTpSpk(1,7-9)`.
    *   `Key(D) BrdSpk(D)` for `xxxxxxxxxxxxxxx0` and `AYdat(D)` for `#BFFD` with `1x1xxxxxxxxxxx0x` and `AYadr(D)` for `#FFFD` with `111xxxxxxxxxxx0x` confirm AY and ULA ports for Pentagon-1024SL.

*   **Port `#7FFD` (32765 decimal) - Memory Paging Control**
    *   **WRITE:**
        *   **Pentagon 128 (8):** Standard 128K memory paging (RAM banks 0-7, ROM 128/48, screen select). The lock bit (D5) is crucial:
            *   When D5=0, bits 0-4 control paging.
            *   When D5=1, paging is "locked," but this often enables access to extended features or alternative ROMs (like TR-DOS if bit 4 is also set).
        *   **Pentagon-1024SL (D):** Extends the 128K paging. With 1024KB RAM, this port, along with `#1FFD`, is essential for managing the numerous RAM banks.
            *   The basic 128K mapping via bits 0-2 is usually preserved for compatibility when in a "base" mode.
            *   Bit 3 (Screen) and Bit 4 (ROM) work as in 128K.
            *   Bit 5 (Lock) is critical. When set, it often unlocks access to higher RAM banks through other bits/ports or changes the interpretation of bits 0-2 to address a wider range of banks.
    *   **Notes:** `Pag(2,8,9,A)` for general 128K compatibility. Specifically `Pag(D)` for `0xxxxxxxxxxxxx01` decoding implies Pentagon-1024SL's core paging.

*   **Port `#1FFD` (8189 decimal) - Extended Memory Paging / TR-DOS (Pentagon-1024SL)**
    *   This port is generally not present or used on the original Pentagon 128 (which was strictly 128KB). It becomes vital for Pentagon models with more RAM (like 256KB, 512KB, 1024SL).
    *   **WRITE (Pentagon-1024SL):**
        *   Controls paging for RAM banks beyond the initial set accessible by `#7FFD`.
        *   Often selects which group of 256KB (or similar large block) is currently active, with `#7FFD` selecting within that block.
        *   Manages mapping of the TR-DOS ROM (if integrated or via an expansion like NemoFDC).
        *   May control Turbo CPU mode.
    *   **Notes:** The table doesn't explicitly list `Pag(D)` under `#1FFD`'s primary entries, but this port is standard for extended RAM on many clones it would be derived from or compatible with (like Scorpion). The `Pag(7)` entry for `#1FFD` (`00xxxxxxxxxxxx01`) suggests a commonality with KAY, and Pentagon 1024SL likely shares this if it's extending beyond 128K using similar schemes. If a Pentagon 1024SL has a NemoFDC or similar, it might use ports like `#0FFD` (see NemoIDE later).

*   **Port `#EFF7` (61431 decimal) - Pentagon 1024SL v2.x System Port (Cache, etc.)**
    *   **ADDRESS:** `1110111111110111`
    *   **DECODING (Pentagon-1024SL):** `1110xxxxxxxx0xxx` (for model `8` which might be a typo and mean `D` or a specific older Pentagon 1024K variant) or `1110xxxxxxxx0xx1` (for model `D` Pentagon-1024SL).
    *   **WRITE:** `PagVidTrbReg(8)` / `PagVidTrbReg(D)`. This is a multifunction system port on later Pentagon 1024SL models (especially v2.2/v2.3 "KarS").
        *   **Bit 0 (Turbo):** Enables/disables CPU Turbo mode (often 14MHz).
        *   **Bit 1 (Cache):** Enables/disables internal CPU cache (if supported by the Z80 variant or FPGA core).
        *   **Bit 2 (Video Page):** Selects between normal screen (RAM 5/7) and an alternative video page (e.g., for text modes or GigaScreen if supported).
        *   **Bits 3-5 (RAM Page Extension):** Used in conjunction with `#7FFD` and `#1FFD` to select very high RAM pages.
        *   **Bit 6 (ROM Page):** Selects ROMs beyond the basic 128/48.
        *   **Bit 7 (System Lock/Unlock):** Can protect system registers.
    *   **Notes:** This port is a hallmark of advanced Pentagon 1024SL versions.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(8,9,D)`, `AYadr(8,9,D)` for Pentagon 128 and 1024SL.
    *   `#BFFD` decoding: `101xxxxxxxxxxx0x` (for 8,9), `1x1xxxxxxxxxxx0x` (for D).
    *   `#FFFD` decoding: `111xxxxxxxxxxx0x` (for 8,9,D).

### Built-in Peripherals (Common on Pentagon)

*   **Beta Disk Interface (TR-DOS):** Very common, often built-in or as a standard add-on (like NemoFDC).
    *   **Ports `#1F, #3F, #5F, #7F` (WD1793 Registers):**
        *   **DECODING:** `xxxxxxxx0BAxxx11` (for models 6,7,8,D).
        *   `#1F` (Cmd/Stat), `#3F` (Track), `#5F` (Sector), `#7F` (Data).
    *   **Port `#FF` (Beta Disk System Register):**
        *   **DECODING:** `xxxxxxxx1xxxxx11` (for models 6,7,8,D).
        *   **WRITE/READ:** Drive select, side select, TR-DOS ROM enable/disable. `FDsys(6,7,8,D)`.

*   **NemoIDE Ports (if NemoFDC/IDE controller is present on Pentagon-1024SL):**
    *   See the "NemoIDE" section later in the document. Ports like `#10-#F0` (IDE channels), `#11` (IDE hi), `#C8` (IDE control). The Pentagon 1024SL often integrated such controllers.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD` as a base.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **ULA Timings:** The "Pentagon timings" can affect compatibility with software sensitive to exact ULA behavior.
*   **Extended Memory (Pentagon-1024SL):** Up to 1024KB RAM, managed by `#7FFD`, `#1FFD`, and the advanced `#EFF7` port on newer versions. Original Pentagon 128 is just 128KB.
*   **Turbo Mode (Pentagon-1024SL):** CPU can run at higher speeds (e.g., 7MHz, 14MHz), often controlled by `#EFF7`.
*   **Cache Control (Pentagon-1024SL):** Some models have CPU cache control via `#EFF7`.
*   **Built-in Beta Disk Interface:** Standard or very common.
*   **Optional IDE/RTC:** Later Pentagon models integrate more peripherals.

---

## 9. Profi / Profi-1 (v3.x)

The Profi was a powerful Czechoslovakian/Russian clone with a focus on expansion, CP/M compatibility, and more professional features. It could have up to 1MB RAM, an IDE interface, and other enhancements.

**Key:**
*   `(9)`: Refers to Profi / Profi-1 (v3.x and similar).

### System Ports

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality.
    *   `KeyTp(1,7-9)Prn(7)BrdTpSpk(1,7-9)` applies.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging (128K Compatibility & Extensions)**
    *   **WRITE:**
        *   Provides standard 128K memory paging (RAM banks 0-7, ROM 128/48, screen select).
        *   Bit 5 (Lock) is used to enable extended Profi modes. When locked:
            *   Bits 0-3 can select one of 16 RAM pages (0-15) for the `#C000` window.
            *   Bit 4 selects ROM: `0`=BASIC, `1`=CP/M bootstrap or Service ROM.
    *   **Notes:** `Pag(2,8,9,A)`.

*   **Port `#1FFD` (8189 decimal) - ROM Disk / Extended RAM Control**
    *   **WRITE:**
        *   Controls which 64KB block of the total RAM (up to 1MB) is currently active.
        *   Can map ROM images from RAM (ROM disk feature).
        *   Involved in enabling CP/M mode by mapping the CP/M ROM.
    *   **Notes:** Not explicitly detailed for Profi (`9`) under `#1FFD` in this table section, but such a port is essential for >128KB RAM and CP/M. Profi's specific extended paging port might be different or an extension of `#7FFD`'s locked mode. However, `Pag(9)` is listed for port `#DFFD`.

*   **Port `#DFFD` (57341 decimal) - Extended Memory Paging (Profi)**
    *   **ADDRESS:** `1101111111111101`
    *   **DECODING:** `xx0xxxxxxxxxxx0x` (A14 must be 0).
    *   **WRITE:** `Pag(9)`. This is a key port for Profi's extended memory management.
        *   **Bits 0-3:** Select which 64KB page of the Profi's RAM (up to 16 such pages for 1MB) is mapped into the Z80's address space.
        *   **Bit 6 (ROMDIS):** Disables the main ROM at `#0000`, allowing RAM there (for CP/M).
        *   **Bit 7 (FIXRAM):** When set, fixes RAM bank 0 in the `#C000-#FFFF` window, useful for some resident programs.
    *   **Notes:** This port, in conjunction with `#7FFD`, handles Profi's memory.

*   **Port `#80FD` (33021 decimal) - CP/M Mode / Memory Page (Profi specific or related to Quorum `C`)**
    *   **ADDRESS:** `1000000011111101`
    *   **DECODING:** `1x0xxxxxxxx11x0x`
    *   **WRITE:** `CP/MPag(C)`. The `(C)` points to Quorum. If Profi uses this, it would be for:
        *   Enabling CP/M mode.
        *   Selecting specific memory configurations required for CP/M (e.g., RAM at `#0000`).
        *   Paging RAM for CP/M's TPA (Transient Program Area).
    *   Profi has its own CP/M support, so this or a similar port would be vital.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(8,9,D)`, `AYadr(8,9,D)` are listed, implying Profi (`9`) uses these.
    *   `#BFFD` decoding: `101xxxxxxxxxxx0x` (for 8,9).
    *   `#FFFD` decoding: `111xxxxxxxxxxx0x` (for 8,9,D).

### Built-in Peripherals (Common on Profi)

*   **Beta Disk Interface (TR-DOS) / IDE Interface:**
    *   Profi often included a Beta Disk FDC. Some versions also had an IDE interface.
    *   **Beta Disk:** Standard ports `#1F, #3F, #5F, #7F` (WD1793) and `#FF` (System Register).
        *   The table has `WD1793(9)` for decoding `0xxxxxxx0BAxxx11` (i8255 on other systems). This suggests Profi might use these addresses for its FDC if A7 is low.
        *   And `FDsys(9)` for `0xxxxxxx111xxx11` on port `#FF`.
    *   **IDE Interface (Profi specific):**
        *   The "Profi COM port & Soft XT keyboard" section lists ports `#E0FB-#EFFB`. These are not IDE but COM/Keyboard.
        *   The main "ATM IDE" or "NemoIDE" port schemes are common for clones. Profi might use a variation of these or its own scheme if it has IDE. The table does not clearly assign standard IDE ports to Profi (`9`) under the "ATM IDE" or "NemoIDE" sections. However, the general section "8 bit IDE by Pera Putnik" (`#2B-#EF`) or "divIDE" (`#A3-#BF`, `#E3`) are generic IDE solutions Profi might have adopted or been compatible with.

*   **Profi COM port & Soft XT keyboard:**
    *   **Port `#E0FB` (57595):** XT Keyboard data.
    *   **Port `#E8FB` (59643):** Register (likely for keyboard controller or COM port status/control).
    *   **Ports `#EAFB/#EBFB` (8251 UART):** Serial COM port data and status/command.
    *   **Ports `#ECFB-#EFFB` (8253 PIT):** Programmable Interval Timer, often used for baud rate generation for the serial port.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD` for compatibility and as a base for extensions.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **Large RAM & Advanced Paging:** Up to 1MB RAM, managed by `#7FFD` (in locked mode) and primarily `#DFFD`.
*   **CP/M Support:** Dedicated hardware support for running CP/M, involving ROM/RAM mapping via `#DFFD` and possibly `#80FD` or similar.
*   **Integrated Peripherals:** Often included Beta Disk, IDE (variant dependent), and serial/timer ports (`#ExFB` range).
*   **ROM Disk Capability:** Ability to load and run ROM images from RAM.

---

## A. ATM Turbo-2+ (and related ATM models)

The ATM Turbo (often ATM "Turbo 2" or "Turbo 2+") was a very popular and advanced Russian clone series, known for its significantly enhanced graphics capabilities (e.g., 320x200 with 16 colors, hardware text mode), extended memory, CP/M support, and often an IDE interface.

**Key:**
*   `(A)`: Refers to ATM Turbo-2+ (and similar ATM models).

### System Ports (ATM Turbo-2+)

*   **Port `#FE` (254 decimal) - ULA Port / ATM Video**
    *   Provides standard 48K/128K ULA functionality for keyboard, border, tape, beeper.
    *   However, on ATM, this port (or specific decodings of it) also interacts with the ATM's advanced video controller for mode switching or palette access in some contexts.
    *   `KeyTp(A) BrdTpSpk(A)` for `xxxxxxxxxxxxx110` and `KeyTpPrn(6) BrdTpSpk(6)` for `xxxxxxxxxx1xxx10` (ATM might share some Scorpion decoding for compatibility).
    *   `Atr(A)` for port `#FF` decoding `xxxxxxxxxxxxx111` indicates ATM can also read attributes via floating bus.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging (128K Compatibility & ATM Extensions)**
    *   **WRITE:**
        *   Base 128K memory paging (RAM banks 0-7, ROM 128/48, screen select) for software compatibility.
        *   **ATM Extensions:** The lock bit (D5) is critical.
            *   When D5=0: Standard 128K paging.
            *   When D5=1 (Paging "locked" for 128K): Unlocks ATM's extended memory paging registers and features. Bits 0-4 can then select from a wider range of RAM banks (0-31 for 512KB, 0-63 for 1024KB) to be mapped at `#C000`. Also influences ROM selection for CP/M or service ROMs.
    *   **Notes:** `Pag(2,8,9,A)`.

*   **Port `#FF` (255 decimal) - ATM Video Mode / Paging (High Byte)**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111111`
    *   **WRITE `Vid/Pag(4/5)`:** The source indicates `(4,5)` which are Timex/Didaktik. However, ATM also heavily uses port `#FF` (often specifically `#xxFF` where `xx` is significant) for its advanced features.
        *   **ATM Video Palette/Mode:** Writing to `#FF` (or specific mirrors like `#3FFF`, `#7FFF`) with particular values in the A register and BC register (high byte) controls the ATM's extended video modes, palette registers, hardware text mode, and other video parameters.
        *   For example, `OUT (#FF), A` where `B` holds a mode/register index.
    *   This overlaps with the general "Shadow Ports" concept for more complex decoding.

*   **Port `#77` Shadow Ports (e.g., `#0177`, `#4177`, `#BF77`, `#FF77`) - ATM System Control**
    *   **ADDRESS:** `xLxxxxKJ01110111` (base for `#xx77`)
    *   **WRITE ONLY:** These are crucial for ATM configuration.
        *   `VidTrbReg(A)` for base `#77`: General video/turbo register.
        *   **`#BF77` (or `#FF77` if A15=1, A13=1, A12=1, A11=1):** Palette control, text mode attributes.
            *   `L=0 ->Pal,PLLFC -on` from the table might be relevant if ATM shares GMX shadow port logic.
        *   **`#FF77` (often):**
            *   Bit 0: CPU Speed (Normal/Turbo 7MHz).
            *   Bit 1: Video Mode select (e.g., Spectrum mode / ATM 320x200x16c mode / Text mode).
            *   Bit 2: Text Mode 80x25 / 80x32 select.
            *   Bit 3: Text Mode Font select.
            *   Bit 4: Enable writing to palette registers via `#BF77`.
            *   Bit 5: ROM Select (Spectrum ROM / CP/M & Service ROM).
            *   Bit 6: Green Monitor Mode (monochrome output).
            *   Bit 7: Wait states enable/disable.
    *   **Notes:** The `xLxxxxKJ01110111` decoding with specific `L, K, J` bits for different functions is common in advanced clones like ATM and Scorpion GMX. The table's `#BD77/#FF77`, `#BF77/#FF77`, `#FD77/#FF77`, `#FE77/#FF77` are examples of this scheme.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(2,3,?5,A)`, `AYadr(2,3,?5,A)`.
    *   `#BFFD` decoding: `10xxxxxxxxxxxx0x`.
    *   `#FFFD` decoding: `11xxxxxxxxxxxx0x`.

### Built-in Peripherals (Common on ATM Turbo-2+)

*   **ATM IDE Interface:**
    *   ATM models typically include a ZIDE-compatible (Zilog IDE) or standard IDE interface. The source table has a dedicated "ATM IDE" section:
        *   **Port `#FE0F/#FF0F` (Data Register Lo/Hi):** `xxxxxxxx A 00001111` -> `A` bit in address (A7) selects Lo (0) or Hi (1) byte for 16-bit data access.
        *   **Port `#FF2F` (Error / Parameters):** `xxxxxxxx00101111` -> Read Error Reg / Write Precomp/Sector Count.
        *   **Port `#FF4F` (Sector Count):** `xxxxxxxx01001111` -> Read/Write Sector Count.
        *   **Port `#FF6F` (Sector Number / Start Sector):** `xxxxxxxx01101111` -> Read/Write Sector Number.
        *   **Port `#FF8F` (Cylinder Low):** `xxxxxxxx10001111` -> Read/Write Cylinder Low.
        *   **Port `#FFAF` (Cylinder High):** `xxxxxxxx10101111` -> Read/Write Cylinder High.
        *   **Port `#FFCF` (Drive/Head):** `xxxxxxxx11001111` -> Read/Write Drive/Head select.
        *   **Port `#FFEF` (Status / Command):** `xxxxxxxx11101111` -> Read Status Reg / Write Command Reg.
    *   **Notes:** This is a standard 8-bit IDE port mapping, often seen in PC XT IDE controllers as well. The `(A)` indicates these are specifically for ATM.

*   **Optional Kempston Mouse Turbo by VELESOFT:**
    *   If this interface is present, ATM might use its ports.
    *   `#7ADF/#FADF`, `#7BDF/#FBDF`, `#7EDF/#FEDF`, `#7FDF/#FFDF`. `A` bit in address selects Master/Slave.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE` for ULA emulation.
*   Standard 128K memory paging via `#7FFD` for compatibility.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **Advanced Video Modes:** Significantly enhanced graphics (resolution, colors, hardware text mode) controlled by `#FF` (mirrors) and shadow port `#FF77` (and others like `#BF77`).
*   **Extended Memory:** Up to 1024KB RAM, managed by `#7FFD` (extended mode) and potentially other ATM-specific registers.
*   **Turbo Mode:** CPU can run at higher speeds (e.g., 7MHz), controlled by shadow port `#FF77`.
*   **CP/M Support:** Built-in, involving ROM/RAM mapping via `#7FFD` (extended mode) and `#FF77`.
*   **Integrated IDE Interface:** Standard on many ATM models, using the `#Fx0F` - `#FxEF` port range.
*   **Palette Control:** Programmable palette via shadow ports.

---

## C. Quorum 128 / Quorum 128+

The Quorum was another Soviet-era clone, generally aiming for 128K compatibility but with some local modifications or simplifications. The "+" version might have had minor enhancements.

**Key:**
*   `(C)`: Refers to Quorum 128 / Quorum 128+.

### System Ports

*   **Port `#00` (0 decimal) - Paging / Border?**
    *   **ADDRESS:** `xxxxxxxx00000000`
    *   **DECODING (Quorum):** `xxxxxxxx0xx0xxx0`
    *   **WRITE:** `Reg(B)` (Scorpion GMX register), `Pag(C)` (Quorum Paging).
    *   This suggests Quorum uses port `#00` (or its mirrors where A0 is 0 and other specific bits in A1-A7 are 0) for some memory paging functions. This is unusual, as `#00` is typically not a standard Spectrum port. It might also affect border color if it's a ULA-like port and bits D0-D2 are used.
    *   This is a significant deviation if used for primary paging.

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality.
    *   The table lists `KeyTp(C) BrdTpSpk(C)` for decoding `xxxxxxxx1xx11xx0` and `Key(C)` for `xxxxxxxx0xx11xx0`.

*   **Port `#7FFD` (32765 decimal) - Memory Paging Control**
    *   **WRITE:** Standard 128K memory paging (RAM banks 0-7, ROM 128/48, screen).
    *   **Notes:** `Pag(C)` is listed for decoding `xxxxxxxxxx11x0x`. This indicates Quorum uses this for its 128K compatibility.

*   **Port `#80FD` (33021 decimal) - CP/M Mode / Memory Page (Quorum)**
    *   **ADDRESS:** `1000000011111101`
    *   **DECODING:** `1x0xxxxxxxx11x0x`
    *   **WRITE:** `CP/MPag(C)`. This port is crucial for Quorum's CP/M support if it has it, or for extended memory management beyond the basic 128K.
        *   Likely used to map RAM to `#0000` for CP/M and select ROMs.
        *   Could also provide access to RAM banks beyond the first 128KB if the Quorum model supports more.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(C)` and `AYadr(C)` are listed.
    *   `#BFFD` decoding: `101xxxxxxxx1xx0x`.
    *   `#FFFD` decoding: `111xxxxxxxx1xx0x`.

### Peripheral Ports (Quorum Specific or Shared)

*   **Port `#1B` (27 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx0xx11xx1`
    *   **READ:** `Prn(C)`. Quorum likely uses this for reading printer status (e.g., BUSY). This is a common Centronics port.

*   **Port `#1F` (31 decimal) - Kempston Joystick / Printer**
    *   **DECODING:** `xxxxxxxx0xx11xx1` (Same as `#1B` for printer functions if A4 is don't care).
    *   **READ:** `KjoyPrn(C)`. Suggests it can be Kempston Joystick OR printer status, possibly differentiated by other address lines or internal configuration.

*   **Port `#7B` (123 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx0xx110x1`
    *   **WRITE:** `Prn(C)`. Quorum likely uses this for writing data to a Centronics printer.

*   **Port `#FB` (251 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx1xx110x1`
    *   **WRITE:** `Prn(C)`. Another printer data port, or for control signals.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD`.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).
*   Standard Kempston joystick port (`#1F`).

**Differences from Sinclair 128K:**
*   **Port `#00`:** Potentially used for some paging functions, which is non-standard.
*   **Port `#80FD`:** Used for CP/M or extended paging, a specific port choice for these functions.
*   **Printer Ports:** Uses a mix of standard (`#1B`, `#7B`, `#FB`) and possibly shared (`#1F`) ports for printer I/O, suggesting a built-in Centronics interface.
*   May have simplifications or specific local components compared to other major clones.

---

Now, let's address some of the specific peripherals you mentioned.

### General Sound (GS) / Compatible Sound Cards

General Sound was a popular high-quality sound card for ZX Spectrum clones, featuring a Z80 CPU, RAM, and multiple sound channels (often based on Covox-like DACs or custom sound synthesis). It typically used DMA for sample playback.

*   **Ports (from your table for "GENERAL SOUND"):**
    *   **Port `#B3` (179 decimal) - Data Port**
        *   **ADDRESS:** `xxxxxxxx10110011`
        *   **DECODING:** `xxxxxxxx10110011`
        *   **READ/WRITE:** Used for transferring sample data, commands, or parameters to/from the GS RAM/CPU.
    *   **Port `#BB` (187 decimal) - Status/Command Port**
        *   **ADDRESS:** `xxxxxxxx10111011`
        *   **DECODING:** `xxxxxxxx10111011`
        *   **READ (Status):** Returns the status of the GS (e.g., busy, command complete, buffer free).
        *   **WRITE (Command):** Sends commands to the GS (e.g., play sample, stop sound, set volume, load data).

*   **Other potential ports/mechanisms for GS-like cards:**
    *   **DMA Control:** Advanced GS cards might use DMA channels, requiring configuration ports for a DMA controller (e.g., 8237). The "DMA USC" section in your table (`#0C77-#FC77`) might be relevant if GS used such a controller.
    *   **Paged Memory Access:** Some GS cards had their own RAM visible in the Spectrum's address space, paged in via specific I/O ports.

### Soundrive / Covox

Soundrive was another sound device, often simpler than General Sound, providing multiple DAC channels (Covox-like). "Covox" itself refers to simple DACs connected to the printer port, but dedicated Soundrive cards offered more channels and cleaner output.

*   **Ports (from "SOUNDRIVE v1.02"):**
    *   `#0F` (15 dec): Write - L channel A
    *   `#1F` (31 dec): Write - L channel B
    *   `#3F` (63 dec): Write - L Rg8255 (Control for Left channels if 8255 based)
    *   `#4F` (79 dec): Write - R channel C
    *   `#5F` (95 dec): Write - R channel D
    *   `#7F` (127 dec): Write - R Rg8255 (Control for Right channels if 8255 based)
    *   **Decoding:** `xxxxxxxxx0001111` for `#0F`, etc. Each port is typically a direct write to a DAC channel.

*   **Ports (from "SOUNDRIVE v1.05 (SOUNDRIVE/COVOX)"):**
    *   `#0F, #1F, #4F, #5F` and mirrors `#F1, #F3, #F9, #FB`.
    *   **DECODING:** `xxxxxxxxxB0Axxx1` and `xxxxxxxxxxxxB0A1` for the mirrors.
    *   **WRITE:** `LA,LB,RA,RB` (Left A, Left B, Right A, Right B channels). This is a more compact scheme.

### TurboSound / TurboSound FM

*   **TurboSound:** This typically refers to using two AY-3-8912 chips for 6 channels of PSG sound plus stereo effects.
    *   **First AY (Standard):** Ports `#FFFD` (address) and `#BFFD` (data).
    *   **Second AY:** Would require a different set of ports. Common choices were:
        *   `#BFFD` (address 2nd AY) and `#A##D` (data 2nd AY) - not in this table.
        *   `#FFFС` (address 2nd AY) and `#BFFС` (data 2nd AY) - not in this table.
        *   Or by using A13 or A12 to differentiate if the main AY ports are only partially decoded. E.g. if `#FFFD` is decoded as `11xxxxxx xxxx1101` and `#BFFD` as `10xxxxxx xxxx1101`, then other combinations of A13/A12 could select the second chip.
    *   The provided table doesn't explicitly list separate "TurboSound" AY ports beyond the standard ones shared by many clones. If a clone has TurboSound, its documentation would specify the second AY's ports.

*   **TurboSound FM:** This would imply adding an FM synthesis chip, most commonly the Yamaha YM2203 (OPN) or YM2149 (AY compatible with an FM portion, though less common for "FM addon" cards which usually go for more powerful FM).
    *   **YM2203 Ports:**
        *   Typically uses two ports:
            *   Address Port (to select internal FM registers or PSG registers).
            *   Data Port (to write/read data to/from selected register).
        *   Common port choices for YM2203 on Spectrum clones (not in this table explicitly): `#xxDE` and `#xxDF`, or `#xxFE` and `#xxFF` (conflicting with ULA/BetaDisk unless more address lines are decoded). For example, on ZXM-Phoenix, YM2203 (Turbo-FM) uses `#FBDF` (address) and `#FFDF` (data/status).
    *   This table does not list specific ports for a YM2203 or similar FM chip directly.

### MoonSound

MoonSound was a high-end sound card, often featuring the Yamaha OPL3 (YMF262) FM synthesis chip (similar to AdLib/SoundBlaster cards on PC) and sometimes a GUS-like wavetable section.

*   **OPL3 (YMF262) Ports:**
    *   The OPL3 typically requires four port addresses:
        *   Address Port 0 (for register set 1)
        *   Data Port 0
        *   Address Port 1 (for register set 2)
        *   Data Port 1
    *   A common PC mapping is `#388`, `#389`, `#38A`, `#38B`.
    *   For Spectrum clones, the ports would be different. For example, ZXM-Phoenix uses:
        *   `#77FD` (Address Port 0)
        *   `#7FFD` (Data Port 0) - *This is the standard Spectrum 128 memory paging port! This implies careful hardware design to avoid conflict, e.g., by enabling MoonSound ports only when a specific bit in another control port is set, or by using full 16-bit port decoding that differentiates it.*
        *   `#79FD` (Address Port 1)
        *   `#7BFD` (Data Port 1)
    *   The provided table does not have specific "MoonSound" or "OPL3" entries. If a clone supports it, its own documentation is key.

### Nemo Bus / ZX Bus

*   **ZX BUS:** This is the standard expansion connector on the back of Sinclair ZX Spectrums (48K, 128K, +2, +3). It exposes most of the Z80 CPU signals:
    *   Data Bus (D0-D7)
    *   Address Bus (A0-A15)
    *   Control Signals (/RD, /WR, /MREQ, /IORQ, /M1, /RESET, /INT, /NMI, /WAIT, /BUSRQ, /BUSAK, CLOCK)
    *   Power Lines (+5V, -5V, +12V on some models, GND)
    *   ROMCS (/ROMOE): Signal indicating ROM is selected.
    *   Video/Audio signals are also sometimes present for direct output or passthrough.
    *   **Port Decoding on ZX BUS:** Peripherals connected to the ZX BUS decode I/O ports by monitoring /IORQ and the address bus (A0-A15). They must assert their data onto the bus during an `IN` cycle or latch data during an `OUT` cycle when their specific port address is detected. The ULA's A0=0 rule is a property of the ULA itself, not an inherent ZX BUS rule for all peripherals (though peripherals must not conflict with the ULA).

*   **Nemo BUS:** This was an extension/standardization popular in the Russian clone scene (e.g., on KAY, Scorpion, Pentagon with NemoFDC/NemoIDE). It aimed to provide a more robust and standardized expansion system than the original ZX BUS, often with better signal buffering and defined slots.
    *   **Signal Compatibility:** Generally carries all the Z80 signals like the ZX BUS.
    *   **Additional Signals:** May include specific lines for DMA acknowledge, enhanced interrupt handling, or board select lines for multiple cards.
    *   **Slot-Based System:** Often implemented as a backplane with multiple card slots.
    *   **Port Decoding on Nemo BUS:** Similar to ZX BUS, cards decode their I/O ports from the address bus and /IORQ. The Nemo standard might define specific port ranges or selection mechanisms for cards in different slots to avoid conflicts (e.g., using jumpers on cards or programmable slot selection).
    *   The provided table doesn't detail Nemo BUS specific "bus control ports" but lists peripherals that commonly used it (like NemoIDE). The "SMUC" (Scorpion & MOA Universal Controller) ports in the table are an example of a complex bus controller that might be part of a Nemo-like system, providing ISA bridge capabilities.
        *   `#18E6-#7FFE`: ISA port range mapping.
        *   Various ports like `#7EBE`, `#7FBE` for 8259 PIC (Interrupt Controller).

You're right to bring up those sound devices and bus architectures! They are significant in the clone world. I'll integrate them where appropriate or create separate sections for them. The provided table focuses heavily on I/O ports for specific machines and common interfaces, so some of those advanced sound cards or bus details might not be explicitly listed with full decoding, but I can bring in external knowledge.

Let's continue with the clones.

---

## A. ATM Turbo-2+ (and related ATM models)

The ATM Turbo (often ATM "Turbo 2" or "Turbo 2+") was a very popular and advanced Russian clone series, known for its significantly enhanced graphics capabilities (e.g., 320x200 with 16 colors, hardware text mode), extended memory, CP/M support, and often an IDE interface.

**Key:**
*   `(A)`: Refers to ATM Turbo-2+ (and similar ATM models).

### System Ports (ATM Turbo-2+)

*   **Port `#FE` (254 decimal) - ULA Port / ATM Video**
    *   Provides standard 48K/128K ULA functionality for keyboard, border, tape, beeper.
    *   However, on ATM, this port (or specific decodings of it) also interacts with the ATM's advanced video controller for mode switching or palette access in some contexts.
    *   `KeyTp(A) BrdTpSpk(A)` for `xxxxxxxxxxxxx110` and `KeyTpPrn(6) BrdTpSpk(6)` for `xxxxxxxxxx1xxx10` (ATM might share some Scorpion decoding for compatibility).
    *   `Atr(A)` for port `#FF` decoding `xxxxxxxxxxxxx111` indicates ATM can also read attributes via floating bus.

*   **Port `#7FFD` (32765 decimal) - Primary Memory Paging (128K Compatibility & ATM Extensions)**
    *   **WRITE:**
        *   Base 128K memory paging (RAM banks 0-7, ROM 128/48, screen select) for software compatibility.
        *   **ATM Extensions:** The lock bit (D5) is critical.
            *   When D5=0: Standard 128K paging.
            *   When D5=1 (Paging "locked" for 128K): Unlocks ATM's extended memory paging registers and features. Bits 0-4 can then select from a wider range of RAM banks (0-31 for 512KB, 0-63 for 1024KB) to be mapped at `#C000`. Also influences ROM selection for CP/M or service ROMs.
    *   **Notes:** `Pag(2,8,9,A)`.

*   **Port `#FF` (255 decimal) - ATM Video Mode / Paging (High Byte)**
    *   **ADDRESS (CPU outputs):** `xxxxxxxx11111111`
    *   **WRITE `Vid/Pag(4/5)`:** The source indicates `(4,5)` which are Timex/Didaktik. However, ATM also heavily uses port `#FF` (often specifically `#xxFF` where `xx` is significant) for its advanced features.
        *   **ATM Video Palette/Mode:** Writing to `#FF` (or specific mirrors like `#3FFF`, `#7FFF`) with particular values in the A register and BC register (high byte) controls the ATM's extended video modes, palette registers, hardware text mode, and other video parameters.
        *   For example, `OUT (#FF), A` where `B` holds a mode/register index.
    *   This overlaps with the general "Shadow Ports" concept for more complex decoding.

*   **Port `#77` Shadow Ports (e.g., `#0177`, `#4177`, `#BF77`, `#FF77`) - ATM System Control**
    *   **ADDRESS:** `xLxxxxKJ01110111` (base for `#xx77`)
    *   **WRITE ONLY:** These are crucial for ATM configuration.
        *   `VidTrbReg(A)` for base `#77`: General video/turbo register.
        *   **`#BF77` (or `#FF77` if A15=1, A13=1, A12=1, A11=1):** Palette control, text mode attributes.
            *   `L=0 ->Pal,PLLFC -on` from the table might be relevant if ATM shares GMX shadow port logic.
        *   **`#FF77` (often):**
            *   Bit 0: CPU Speed (Normal/Turbo 7MHz).
            *   Bit 1: Video Mode select (e.g., Spectrum mode / ATM 320x200x16c mode / Text mode).
            *   Bit 2: Text Mode 80x25 / 80x32 select.
            *   Bit 3: Text Mode Font select.
            *   Bit 4: Enable writing to palette registers via `#BF77`.
            *   Bit 5: ROM Select (Spectrum ROM / CP/M & Service ROM).
            *   Bit 6: Green Monitor Mode (monochrome output).
            *   Bit 7: Wait states enable/disable.
    *   **Notes:** The `xLxxxxKJ01110111` decoding with specific `L, K, J` bits for different functions is common in advanced clones like ATM and Scorpion GMX. The table's `#BD77/#FF77`, `#BF77/#FF77`, `#FD77/#FF77`, `#FE77/#FF77` are examples of this scheme.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(2,3,?5,A)`, `AYadr(2,3,?5,A)`.
    *   `#BFFD` decoding: `10xxxxxxxxxxxx0x`.
    *   `#FFFD` decoding: `11xxxxxxxxxxxx0x`.

### Built-in Peripherals (Common on ATM Turbo-2+)

*   **ATM IDE Interface:**
    *   ATM models typically include a ZIDE-compatible (Zilog IDE) or standard IDE interface. The source table has a dedicated "ATM IDE" section:
        *   **Port `#FE0F/#FF0F` (Data Register Lo/Hi):** `xxxxxxxx A 00001111` -> `A` bit in address (A7) selects Lo (0) or Hi (1) byte for 16-bit data access.
        *   **Port `#FF2F` (Error / Parameters):** `xxxxxxxx00101111` -> Read Error Reg / Write Precomp/Sector Count.
        *   **Port `#FF4F` (Sector Count):** `xxxxxxxx01001111` -> Read/Write Sector Count.
        *   **Port `#FF6F` (Sector Number / Start Sector):** `xxxxxxxx01101111` -> Read/Write Sector Number.
        *   **Port `#FF8F` (Cylinder Low):** `xxxxxxxx10001111` -> Read/Write Cylinder Low.
        *   **Port `#FFAF` (Cylinder High):** `xxxxxxxx10101111` -> Read/Write Cylinder High.
        *   **Port `#FFCF` (Drive/Head):** `xxxxxxxx11001111` -> Read/Write Drive/Head select.
        *   **Port `#FFEF` (Status / Command):** `xxxxxxxx11101111` -> Read Status Reg / Write Command Reg.
    *   **Notes:** This is a standard 8-bit IDE port mapping, often seen in PC XT IDE controllers as well. The `(A)` indicates these are specifically for ATM.

*   **Optional Kempston Mouse Turbo by VELESOFT:**
    *   If this interface is present, ATM might use its ports.
    *   `#7ADF/#FADF`, `#7BDF/#FBDF`, `#7EDF/#FEDF`, `#7FDF/#FFDF`. `A` bit in address selects Master/Slave.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE` for ULA emulation.
*   Standard 128K memory paging via `#7FFD` for compatibility.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).

**Differences from Sinclair 128K:**
*   **Advanced Video Modes:** Significantly enhanced graphics (resolution, colors, hardware text mode) controlled by `#FF` (mirrors) and shadow port `#FF77` (and others like `#BF77`).
*   **Extended Memory:** Up to 1024KB RAM, managed by `#7FFD` (extended mode) and potentially other ATM-specific registers.
*   **Turbo Mode:** CPU can run at higher speeds (e.g., 7MHz), controlled by shadow port `#FF77`.
*   **CP/M Support:** Built-in, involving ROM/RAM mapping via `#7FFD` (extended mode) and `#FF77`.
*   **Integrated IDE Interface:** Standard on many ATM models, using the `#Fx0F` - `#FxEF` port range.
*   **Palette Control:** Programmable palette via shadow ports.

---

## C. Quorum 128 / Quorum 128+

The Quorum was another Soviet-era clone, generally aiming for 128K compatibility but with some local modifications or simplifications. The "+" version might have had minor enhancements.

**Key:**
*   `(C)`: Refers to Quorum 128 / Quorum 128+.

### System Ports

*   **Port `#00` (0 decimal) - Paging / Border?**
    *   **ADDRESS:** `xxxxxxxx00000000`
    *   **DECODING (Quorum):** `xxxxxxxx0xx0xxx0`
    *   **WRITE:** `Reg(B)` (Scorpion GMX register), `Pag(C)` (Quorum Paging).
    *   This suggests Quorum uses port `#00` (or its mirrors where A0 is 0 and other specific bits in A1-A7 are 0) for some memory paging functions. This is unusual, as `#00` is typically not a standard Spectrum port. It might also affect border color if it's a ULA-like port and bits D0-D2 are used.
    *   This is a significant deviation if used for primary paging.

*   **Port `#FE` (254 decimal) - ULA Port**
    *   Standard 48K/128K ULA functionality.
    *   The table lists `KeyTp(C) BrdTpSpk(C)` for decoding `xxxxxxxx1xx11xx0` and `Key(C)` for `xxxxxxxx0xx11xx0`.

*   **Port `#7FFD` (32765 decimal) - Memory Paging Control**
    *   **WRITE:** Standard 128K memory paging (RAM banks 0-7, ROM 128/48, screen).
    *   **Notes:** `Pag(C)` is listed for decoding `xxxxxxxxxx11x0x`. This indicates Quorum uses this for its 128K compatibility.

*   **Port `#80FD` (33021 decimal) - CP/M Mode / Memory Page (Quorum)**
    *   **ADDRESS:** `1000000011111101`
    *   **DECODING:** `1x0xxxxxxxx11x0x`
    *   **WRITE:** `CP/MPag(C)`. This port is crucial for Quorum's CP/M support if it has it, or for extended memory management beyond the basic 128K.
        *   Likely used to map RAM to `#0000` for CP/M and select ROMs.
        *   Could also provide access to RAM banks beyond the first 128KB if the Quorum model supports more.

*   **AY Sound Ports (`#FFFD`, `#BFFD`):**
    *   Standard Sinclair 128K ports for AY-3-8912.
    *   `AYdat(C)` and `AYadr(C)` are listed.
    *   `#BFFD` decoding: `101xxxxxxxx1xx0x`.
    *   `#FFFD` decoding: `111xxxxxxxx1xx0x`.

### Peripheral Ports (Quorum Specific or Shared)

*   **Port `#1B` (27 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx0xx11xx1`
    *   **READ:** `Prn(C)`. Quorum likely uses this for reading printer status (e.g., BUSY). This is a common Centronics port.

*   **Port `#1F` (31 decimal) - Kempston Joystick / Printer**
    *   **DECODING:** `xxxxxxxx0xx11xx1` (Same as `#1B` for printer functions if A4 is don't care).
    *   **READ:** `KjoyPrn(C)`. Suggests it can be Kempston Joystick OR printer status, possibly differentiated by other address lines or internal configuration.

*   **Port `#7B` (123 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx0xx110x1`
    *   **WRITE:** `Prn(C)`. Quorum likely uses this for writing data to a Centronics printer.

*   **Port `#FB` (251 decimal) - Printer Port**
    *   **DECODING:** `xxxxxxxx1xx110x1`
    *   **WRITE:** `Prn(C)`. Another printer data port, or for control signals.

**Similarities with Sinclair 128K:**
*   Base functionality of port `#FE`.
*   Standard 128K memory paging via `#7FFD`.
*   AY Sound chip and ports (`#FFFD`, `#BFFD`).
*   Standard Kempston joystick port (`#1F`).

**Differences from Sinclair 128K:**
*   **Port `#00`:** Potentially used for some paging functions, which is non-standard.
*   **Port `#80FD`:** Used for CP/M or extended paging, a specific port choice for these functions.
*   **Printer Ports:** Uses a mix of standard (`#1B`, `#7B`, `#FB`) and possibly shared (`#1F`) ports for printer I/O, suggesting a built-in Centronics interface.
*   May have simplifications or specific local components compared to other major clones.

---

Now, let's address some of the specific peripherals you mentioned.

### General Sound (GS) / Compatible Sound Cards

General Sound was a popular high-quality sound card for ZX Spectrum clones, featuring a Z80 CPU, RAM, and multiple sound channels (often based on Covox-like DACs or custom sound synthesis). It typically used DMA for sample playback.

*   **Ports (from your table for "GENERAL SOUND"):**
    *   **Port `#B3` (179 decimal) - Data Port**
        *   **ADDRESS:** `xxxxxxxx10110011`
        *   **DECODING:** `xxxxxxxx10110011`
        *   **READ/WRITE:** Used for transferring sample data, commands, or parameters to/from the GS RAM/CPU.
    *   **Port `#BB` (187 decimal) - Status/Command Port**
        *   **ADDRESS:** `xxxxxxxx10111011`
        *   **DECODING:** `xxxxxxxx10111011`
        *   **READ (Status):** Returns the status of the GS (e.g., busy, command complete, buffer free).
        *   **WRITE (Command):** Sends commands to the GS (e.g., play sample, stop sound, set volume, load data).

*   **Other potential ports/mechanisms for GS-like cards:**
    *   **DMA Control:** Advanced GS cards might use DMA channels, requiring configuration ports for a DMA controller (e.g., 8237). The "DMA USC" section in your table (`#0C77-#FC77`) might be relevant if GS used such a controller.
    *   **Paged Memory Access:** Some GS cards had their own RAM visible in the Spectrum's address space, paged in via specific I/O ports.

### Soundrive / Covox

Soundrive was another sound device, often simpler than General Sound, providing multiple DAC channels (Covox-like). "Covox" itself refers to simple DACs connected to the printer port, but dedicated Soundrive cards offered more channels and cleaner output.

*   **Ports (from "SOUNDRIVE v1.02"):**
    *   `#0F` (15 dec): Write - L channel A
    *   `#1F` (31 dec): Write - L channel B
    *   `#3F` (63 dec): Write - L Rg8255 (Control for Left channels if 8255 based)
    *   `#4F` (79 dec): Write - R channel C
    *   `#5F` (95 dec): Write - R channel D
    *   `#7F` (127 dec): Write - R Rg8255 (Control for Right channels if 8255 based)
    *   **Decoding:** `xxxxxxxxx0001111` for `#0F`, etc. Each port is typically a direct write to a DAC channel.

*   **Ports (from "SOUNDRIVE v1.05 (SOUNDRIVE/COVOX)"):**
    *   `#0F, #1F, #4F, #5F` and mirrors `#F1, #F3, #F9, #FB`.
    *   **DECODING:** `xxxxxxxxxB0Axxx1` and `xxxxxxxxxxxxB0A1` for the mirrors.
    *   **WRITE:** `LA,LB,RA,RB` (Left A, Left B, Right A, Right B channels). This is a more compact scheme.

### TurboSound / TurboSound FM

*   **TurboSound:** This typically refers to using two AY-3-8912 chips for 6 channels of PSG sound plus stereo effects.
    *   **First AY (Standard):** Ports `#FFFD` (address) and `#BFFD` (data).
    *   **Second AY:** Would require a different set of ports. Common choices were:
        *   `#BFFD` (address 2nd AY) and `#A##D` (data 2nd AY) - not in this table.
        *   `#FFFС` (address 2nd AY) and `#BFFС` (data 2nd AY) - not in this table.
        *   Or by using A13 or A12 to differentiate if the main AY ports are only partially decoded. E.g. if `#FFFD` is decoded as `11xxxxxx xxxx1101` and `#BFFD` as `10xxxxxx xxxx1101`, then other combinations of A13/A12 could select the second chip.
    *   The provided table doesn't explicitly list separate "TurboSound" AY ports beyond the standard ones shared by many clones. If a clone has TurboSound, its documentation would specify the second AY's ports.

*   **TurboSound FM:** This would imply adding an FM synthesis chip, most commonly the Yamaha YM2203 (OPN) or YM2149 (AY compatible with an FM portion, though less common for "FM addon" cards which usually go for more powerful FM).
    *   **YM2203 Ports:**
        *   Typically uses two ports:
            *   Address Port (to select internal FM registers or PSG registers).
            *   Data Port (to write/read data to/from selected register).
        *   Common port choices for YM2203 on Spectrum clones (not in this table explicitly): `#xxDE` and `#xxDF`, or `#xxFE` and `#xxFF` (conflicting with ULA/BetaDisk unless more address lines are decoded). For example, on ZXM-Phoenix, YM2203 (Turbo-FM) uses `#FBDF` (address) and `#FFDF` (data/status).
    *   This table does not list specific ports for a YM2203 or similar FM chip directly.

### MoonSound

MoonSound was a high-end sound card, often featuring the Yamaha OPL3 (YMF262) FM synthesis chip (similar to AdLib/SoundBlaster cards on PC) and sometimes a GUS-like wavetable section.

*   **OPL3 (YMF262) Ports:**
    *   The OPL3 typically requires four port addresses:
        *   Address Port 0 (for register set 1)
        *   Data Port 0
        *   Address Port 1 (for register set 2)
        *   Data Port 1
    *   A common PC mapping is `#388`, `#389`, `#38A`, `#38B`.
    *   For Spectrum clones, the ports would be different. For example, ZXM-Phoenix uses:
        *   `#77FD` (Address Port 0)
        *   `#7FFD` (Data Port 0) - *This is the standard Spectrum 128 memory paging port! This implies careful hardware design to avoid conflict, e.g., by enabling MoonSound ports only when a specific bit in another control port is set, or by using full 16-bit port decoding that differentiates it.*
        *   `#79FD` (Address Port 1)
        *   `#7BFD` (Data Port 1)
    *   The provided table does not have specific "MoonSound" or "OPL3" entries. If a clone supports it, its own documentation is key.

### Nemo Bus / ZX Bus

*   **ZX BUS:** This is the standard expansion connector on the back of Sinclair ZX Spectrums (48K, 128K, +2, +3). It exposes most of the Z80 CPU signals:
    *   Data Bus (D0-D7)
    *   Address Bus (A0-A15)
    *   Control Signals (/RD, /WR, /MREQ, /IORQ, /M1, /RESET, /INT, /NMI, /WAIT, /BUSRQ, /BUSAK, CLOCK)
    *   Power Lines (+5V, -5V, +12V on some models, GND)
    *   ROMCS (/ROMOE): Signal indicating ROM is selected.
    *   Video/Audio signals are also sometimes present for direct output or passthrough.
    *   **Port Decoding on ZX BUS:** Peripherals connected to the ZX BUS decode I/O ports by monitoring /IORQ and the address bus (A0-A15). They must assert their data onto the bus during an `IN` cycle or latch data during an `OUT` cycle when their specific port address is detected. The ULA's A0=0 rule is a property of the ULA itself, not an inherent ZX BUS rule for all peripherals (though peripherals must not conflict with the ULA).

*   **Nemo BUS:** This was an extension/standardization popular in the Russian clone scene (e.g., on KAY, Scorpion, Pentagon with NemoFDC/NemoIDE). It aimed to provide a more robust and standardized expansion system than the original ZX BUS, often with better signal buffering and defined slots.
    *   **Signal Compatibility:** Generally carries all the Z80 signals like the ZX BUS.
    *   **Additional Signals:** May include specific lines for DMA acknowledge, enhanced interrupt handling, or board select lines for multiple cards.
    *   **Slot-Based System:** Often implemented as a backplane with multiple card slots.
    *   **Port Decoding on Nemo BUS:** Similar to ZX BUS, cards decode their I/O ports from the address bus and /IORQ. The Nemo standard might define specific port ranges or selection mechanisms for cards in different slots to avoid conflicts (e.g., using jumpers on cards or programmable slot selection).
    *   The provided table doesn't detail Nemo BUS specific "bus control ports" but lists peripherals that commonly used it (like NemoIDE). The "SMUC" (Scorpion & MOA Universal Controller) ports in the table are an example of a complex bus controller that might be part of a Nemo-like system, providing ISA bridge capabilities.
        *   `#18E6-#7FFE`: ISA port range mapping.
        *   Various ports like `#7EBE`, `#7FBE` for 8259 PIC (Interrupt Controller).

---

## Shadow Ports (General Concepts and Specifics from the table)

"Shadow Ports" are I/O port addresses that are not part of the standard, officially documented Spectrum port map but are decoded by specific clone hardware for advanced control or features. They often rely on more complete address decoding (using more bits of the I/O address than just A0-A7) or specific combinations of address lines that wouldn't activate standard peripherals. The term "shadow" implies they might co-exist with or override standard port behavior under certain conditions.

The table has a dedicated "SHADOW PORTS" section, mostly related to ATM (`A`) and Scorpion GMX (`B`) features.

*   **Port `#77` (and its variants like `#xx77`)**
    *   **Base ADDRESS Pattern:** `xLxxxxKJ01110111`
    *   **Base DECODING Pattern:** `xLxxxxKJ0xx10111`
    *   These are **WRITE-ONLY** ports. The high bits `L, K, J` (and potentially others represented by `x` if the clone decodes them) select specific functions.
    *   **`#77` (as a base, specific model `A` - ATM Turbo):**
        *   `VidTrbReg(A)`: Video, Turbo, and other system Registers for ATM. This is a general assignment; specific ATM models would use mirrors like `#FF77` with defined bit functions (as detailed previously under ATM).
    *   **Scorpion GMX (`?B`) specific shadow ports based on `#xx77` (these seem to relate to GMX features rather than standard Scorpion ZS256):**
        *   **`#BD77/#FF77` (Software alias `#0177`):** `xLxxxxK10xx10111`
            *   Condition: `L=0, K=0, J=1` (J corresponds to A8 if L,K,J map to A10,A9,A8)
            *   Function: `Pal,PLLFC,Shdw-on`. Controls Palette, PLL Frequency Control (for video sync/timing), and Shadow RAM mode activation for GMX.
        *   **`#BF77/#FF77`:** `xLxxxx110xx10111`
            *   Condition: `L=0, K=1, J=1`
            *   Function: `Pal,PLLFC -on`. Palette and PLL control.
        *   **`#FD77/#FF77` (Software alias `#4177`):** `x1xxxxK10xx10111`
            *   Condition: `L=1, K=0, J=1`
            *   Function: `Shdw-on`. Shadow RAM mode activation.
        *   **`#FE77/#FF77`:** `x1xxxx1J0xx10111`
            *   Condition: `L=1, K=1, J=0`
            *   Function: `Pag-off,CP/Mrom>CPU0-3`. Disables extended paging, maps CP/M ROM to all 4 virtual CPUs (if GMX has such a feature, or refers to mapping it across memory segments).

*   **Port `#3FF7-#FFF7` (ATM Turbo)**
    *   **ADDRESS:** `BA11111111110111`
    *   **DECODING:** `BAxxxx111xx10111`
    *   **WRITE:** `RAMPag(A)`. For ATM, this range (selected by A15, A14, A10, A9) is used for RAM paging, likely selecting very high memory banks or configuring RAM segments for its enhanced video or CP/M.

*   **Port `#FEE7/#FFE7` (ATM Turbo)**
    *   **ADDRESS:** `1111111A11100111`
    *   **DECODING:** `xxxxxxxAxxx00111`
    *   **READ/WRITE:** `Reserved(A)`. Reserved for ATM system functions, potentially related to hardware configuration or diagnostics. The `A` in the address (A7) likely selects sub-functions.

---

## SMUC (Scorpion & MOA Universal Controller)

This section details ports for a sophisticated controller found on some Scorpion models (and potentially MOA - an entity/project related to Scorpion). It appears to be an ISA bus bridge and general system controller.

*   **Ports `#18E6-#7FFE` - ISA Bus Window**
    *   **ADDRESS:** `0ED11CBA111GF110` (complex pattern indicating specific high bits define this range)
    *   **DECODING:** `xx1IHGFEDCBA` (ISA address lines A0-A9) for ISA ports `#200-#3FF`.
    *   **READ/WRITE:** Allows the Z80 to access a range of I/O ports on an emulated/bridged ISA bus (ports `#200-#3FF` hex on the ISA bus, commonly used for game ports, secondary IDE, etc.). The Z80 port address bits `A1-A12` (or a subset) map to ISA address lines. `E,D,C,B,A,G,F` in the Z80 port address likely map to ISA address lines.

*   **Port `#5FBA` (24506) - SMUC Version Register**
    *   **DECODING:** `0x011xxx101xx010`
    *   **READ:** `Version`. Reads the version of the SMUC controller.

*   **Port `#5FBE` (24510) - SMUC Revision Register**
    *   **DECODING:** `0x011xxx101xx110`
    *   **READ:** `Revision`. Reads the revision of the SMUC controller.

*   **Port `#7EBE` (32446) & `#7FBE` (32702) - 8259 PIC (Programmable Interrupt Controller)**
    *   `#7EBE` DECODING: `0x111xx0101xx110`
    *   `#7FBE` DECODING: `0x111xx1101xx110`
    *   **READ/WRITE:** Access to the 8259 PIC, which manages hardware interrupts for peripherals connected via SMUC (e.g., ISA cards). One port is typically for Command/Status, the other for Data/Mask.

*   **Port `#7FBA` (32698) - Virtual FDD**
    *   **DECODING:** `0x111xxx101xx010`
    *   **READ/WRITE:** Interface for a virtual floppy disk drive, likely allowing floppy images to be loaded from another source (e.g., IDE hard drive) and presented to the system as a standard FDD.

*   **Port `#D8BE` (55486) - IDE High Byte Access**
    *   **DECODING:** `1x011xxx101xx110`
    *   **READ/WRITE:** `IDE-Hi`. Likely used in conjunction with other IDE ports for 16-bit data access to an IDE interface managed by SMUC, possibly for the higher byte of the data word.

*   **Port `#DFBA` (57274) - DS1685 RTC (Real-Time Clock)**
    *   **DECODING:** `1x011xxx101xx010`
    *   **READ/WRITE:** Access to a DS1685 (or compatible) RTC chip for timekeeping.

*   **Ports `#F8BE-#FFBE` - IDE Interface (Primary/Secondary Channel)**
    *   **DECODING:** `1x111xxx101xx110` (Bits A8-A10 via `CBA` in address select specific IDE registers).
    *   **READ/WRITE:** `IDE#1Fx/#3F6`. Maps to standard PC IDE port ranges.
        *   `#1F0-#1F7` (Primary IDE Channel) or `#170-#177` (Secondary).
        *   `#3F6` (Primary Control) or `#376` (Secondary Control).
        *   The `CBA` bits in the Z80 port address `11111CBA10111110` likely select which IDE register within the range is accessed.

*   **Port `#FFBA` (65466) - SMUC System Control**
    *   **DECODING:** `1x111xxx101xx010`
    *   **READ/WRITE:** `SYS`. General system configuration and control for the SMUC.

**SMUC Implications:** This controller essentially adds PC-like expansion capabilities (ISA bus, standard IDE, RTC, PIC) to a Scorpion, making it a very powerful hybrid system.

---

## Beta 128 Disk Interface

This is the standard port mapping for the ubiquitous Beta Disk Interface, using the WD1793 (or compatible) Floppy Disk Controller. This interface was widely cloned and integrated into many machines (Scorpion, Pentagon, KAY, etc., often as "TR-DOS compatible").

*   **Ports `#1F, #3F, #5F, #7F` - WD1793 Registers**
    *   **ADDRESS:** `xxxxxxxx0BA11111`
    *   **DECODING (for models 6,7,8,D,A,C - various clones):**
        *   `xxxxxxxx0BAxxx11` (general case for 6,7,8,D)
        *   `xxxxxxxx0BAx11x1` (for Quorum `C`)
        *   `xxxxxxxx0BA11111` (for ATM `A`)
        *   `0xxxxxxx0BAxxx11` (for Profi `9`)
    *   **READ/WRITE:**
        *   `#1F` (B=00, A4=0): WD1793 Command (Write) / Status (Read) Register.
        *   `#3F` (B=01, A4=0): WD1793 Track Register.
        *   `#5F` (B=10, A4=0): WD1793 Sector Register.
        *   `#7F` (B=11, A4=0): WD1793 Data Register.
    *   The `BA` bits in the address (`A2,A1`) typically select the register when A4 of the port address is 0. The different decoding masks reflect how tightly each clone's logic decodes these addresses.

*   **Port `#FF` (255 decimal) - Beta Disk System Register**
    *   **ADDRESS:** `xxxxxxxx11111111`
    *   **DECODING (for models 6,7,8,D,A,C,9):**
        *   `xxxxxxxx1xxxxx11` (general for 6,7,8,D)
        *   `xxxxxxxx1xxx11x1` (for Quorum `C`)
        *   `xxxxxxxx1xx11111` (for ATM `A`) - `FDsysPLLFC(A)` indicates ATM might also use this for PLL/Frequency Control.
        *   `0xxxxxxx111xxx11` (for Profi `9`)
    *   **READ/WRITE:** `FDsys(...)` - Floppy Disk System Register.
        *   **Write Bits:**
            *   D0: Drive Select 0
            *   D1: Drive Select 1
            *   D2: Drive Select 2
            *   D3: Side Select (0 for side 0, 1 for side 1)
            *   D4: /TRDOS ROM Disable (0 to page in TR-DOS ROM, 1 to page out)
            *   D5: /RESET FDC (0 to reset WD1793)
            *   D6: (Used by some interfaces for density or motor control, or PLL on ATM)
            *   D7: Not used / (Used by some for wait states)
        *   **Read Bits:**
            *   D7: FDC Interrupt Request (IRQ) status.
            *   D6: FDC Data Request (DRQ) status.
            *   Other bits often reflect last written drive/control status.

---

## Other IDE Interfaces

The table lists several other IDE interface standards beyond ATM and SMUC.

### NemoIDE

Popular IDE controller in the Russian Spectrum scene, often integrated into Pentagon, KAY.

*   **Ports `#10-#F0` (in steps of #10, e.g., `#10, #20 ... #F0`) - IDE Data Registers (Task File)**
    *   **ADDRESS/DECODING:** `xxxxxxxxCBA10000`
    *   **READ/WRITE:** Access to the 8 IDE task file registers (Data, Error/Features, Sector Count, Sector Num, Cyl Low, Cyl High, Drive/Head, Status/Command). The `CBA` bits (A6,A5,A4 of port address) select which of the 8 registers. This is a compact way to map the IDE registers.
        *   `CBA=000` (Port `#10` if A7=0): Data Register (16-bit access often needs port `#11` too).
        *   `CBA=001` (Port `#20` if A7=0): Error / Features.
        *   ...
        *   `CBA=111` (Port `#80` if A7=0): Status / Command.
        *   The table actually shows `A3=1` (`...10000`) so it would be `#10, #30, #50, #70, #90, #B0, #D0, #F0` if A7=0 and A3 toggles for some reason, or more likely A3 is fixed low and `CBA` (A6,A5,A4) select the register. The `CBA10000` means A4-A6 select the IDE register address (0-7), A3=1, A0-A2=0.
        *   Correct interpretation: Ports are `#x8`, `#x9`, ..., `#xF` where `x` is `CBA` from `xxxxxxxxCBA10000`. More simply: `#1Fxx` style base, where lower bits select registers.
        *   Looking at the common IDE mapping (`#1Fx`): If port `#10` is base data, then `#11` to `#17` are other registers.
        *   The `xxxxxxxxCBA10000` with `IDE#1Fx` is clearer: The `CBA` bits along with the fixed `10000` part likely select the base address (e.g. Primary #1F0 or Secondary #170), and then the Z80's A0-A2 would map to the IDE register select. But the table implies A0-A3 are fixed by `10000`. This is unusual.
        *   **More likely interpretation for `xxxxxxxxCBA10000` and `IDE#1Fx`**: The `CBA` bits select one of 8 *groups* of IDE registers, and the fixed `10000` implies that for each group, only one specific IDE function is accessed. This seems inefficient.
        *   **Alternative NemoIDE mapping (more common):**
            *   Port `#xx1F` (Data, 16-bit, often uses `#xx1E` too or auto hi/lo byte)
            *   Port `#xx2F` (Error/Features) ... up to `#xx7F` (Status/Command).
            *   The `#10-#F0` in steps of `#10` might be a specific Nemo variant.

*   **Port `#11` (17 decimal) - IDE High Byte / Control**
    *   **DECODING:** `xxxxxxxxx xxxx001`
    *   **READ/WRITE:** `IDE-hi`. Likely for the high byte of 16-bit data transfers for the `#10` data port, or a control/status register.

*   **Port `#C8` (200 decimal) - IDE Control Register (#3F6 equivalent)**
    *   **DECODING:** `xxxxxxxx CBA01000` (where CBA=110 for #C8)
    *   **READ/WRITE:** `IDE#3F6`. Access to the IDE Alternate Status / Device Control register.

### DivIDE Interface

A popular compact flash / IDE interface.

*   **Ports `#A3-#BF` (stepping likely by 8: `#A3, #AB, #B3, #BF`) - IDE Task File Registers**
    *   **ADDRESS/DECODING:** `xxxxxxxx101CBA11`
    *   **READ/WRITE:** `IDE#1Fx`. Access to IDE task file registers. `CBA` bits (A2,A1,A0 of IDE register address) select the specific register. The base address is formed by `101xxA11`.
        *   If `CBA` maps to A2,A1,A0 of the IDE register select, then port `#A3` (`10100011`) would be IDE Data, `#A7` would be Status/Command.
        *   The `xxxxxxxx101CBA11` suggests `A0=1, A1=1`. This is common for odd-addressed ports. The `CBA` part must map to A4,A3,A2 of the Z80 port address to select IDE registers 0-7 if `#A3` is the base for Data.

*   **Port `#E3` (227 decimal) - DivIDE Control Register**
    *   **DECODING:** `xxxxxxxx11100011`
    *   **WRITE:** `divIDEcontrol`.
        *   Bit 0: MAPRAM. When set, DivIDE's internal RAM (usually 8KB or 32KB) is mapped into `#0000-#1FFF` (or `#0000-#7FFF`), overlaying ROM. Used for running DivIDE's own firmware (e.g., FatWare, ESXDOS).
        *   Bit 1: ROM Protect (on some versions).
        *   Bit 6: CONJUR. When set, enables "allram" mode where DivIDE RAM overlays `#0000-#FFFF` (used by some advanced firmwares).
        *   Bit 7: NMI enable for auto-mapping (magic button).

### 8 bit IDE by Pera Putnik (Simple IDE Interface)

*   **Ports `#2B-#EF` (complex stepping)**
    *   **ADDRESS/DECODING:** `xxxxxxxxCB101A11`
    *   **READ/WRITE:** `IDE#1Fx`.
    *   The `CB` bits (A5,A4 of port address) and `A` bit (A0 of port address being 1) along with the fixed `101x11` pattern select specific IDE registers. This implies a somewhat sparse mapping.
        *   `A` (in `1A11`) seems to be part of the register select along with `CB`.
        *   Example: Port `#2B` (00101011) vs `#AF` (10101111).
        *   This allows accessing the 8 IDE task file registers and the control register using different combinations of `CB` and `A`.

---

## MB-02+ (Russian Clone with advanced features)

The MB-02+ was a powerful Russian clone, seemingly integrating many features like an RTC, Z80 DMA, FDC (WD2797), multiple 8255 PPIs, and an IDE interface.

**Key:**
*   `(*3)`: Indicates a reference to `http://www.8bc.com/sinclair/index.html` for more info.

### MB-02+ Ports

*   **Ports `#0003-#0F03` (in steps of #0100) - RTC-72421 (Real-Time Clock)**
    *   **ADDRESS/DECODING:** `xxxxDCBA0xx00011`
    *   **READ/WRITE:** Access to the registers of an RTC-72421 chip. The `DCBA` bits (A8-A11 of the port address) select the specific RTC register (address/data). The low `00000011` (`#x3`) is the base.

*   **Port `#07` (7 decimal) - IDE Control**
    *   **DECODING:** `xxxxxxxx0xx00111`
    *   **READ/WRITE:** `IDEcontrol`. Likely for master/slave select, reset, or other high-level IDE control signals for the MB-02+'s built-in IDE.

*   **Port `#0B` (11 decimal) - Z80 DMA**
    *   **DECODING:** `xxxxxxxx0xx01011`
    *   **READ/WRITE:** `Z80DMA`. Access to the registers of a Z80 DMA controller chip (e.g., AM9517, 8237). Used for high-speed data transfers between memory and peripherals (like FDC/IDE) without CPU intervention.

*   **Ports `#0F, #2F, #4F, #6F` - WD2797 Floppy Disk Controller**
    *   **ADDRESS/DECODING:** `xxxxxxxx0BA01111`
    *   **READ/WRITE:** Access to the WD2797 FDC registers. `BA` bits (A6,A5 of port address) select:
        *   `BA=00` (Port `#0F`): WD Command (Write) / Status (Read).
        *   `BA=01` (Port `#2F`): WD Track Register.
        *   `BA=10` (Port `#4F`): WD Sector Register.
        *   `BA=11` (Port `#6F`): WD Data Register.

*   **Port `#13` (19 decimal) - Floppy Disk Control**
    *   **DECODING:** `xxxxxxxx0xx10011`
    *   **WRITE:** `FDcontrol`. Controls drive select, motor, side, etc., for the floppy interface.

*   **Port `#17` (23 decimal) - Memory Select**
    *   **DECODING:** `xxxxxxxx0xx10111`
    *   **READ/WRITE:** `memsel`. Likely the primary memory paging port for the MB-02+, selecting RAM banks, ROMs, etc.

*   **Ports `#1B, #3B, #5B, #7B` - 8255 PPI Chip 2**
    *   **ADDRESS/DECODING:** `xxxxxxxx0BA11011`
    *   **READ/WRITE:** Access to a second 8255 Programmable Peripheral Interface. `BA` bits (A6,A5 of port address) select Port A, Port B, Port C, or Control Word register.

*   **Ports `#1F, #3F, #5F, #7F` - 8255 PPI Chip 1**
    *   **ADDRESS/DECODING:** `xxxxxxxx0BA11111`
    *   **READ/WRITE:** Access to the first 8255 PPI. Usually for printer, general I/O. (This is the same address range as Beta Disk FDC registers, indicating they are mutually exclusive or selected by another control bit not shown in this simplified decoding).

*   **Ports `#A3-#BF` (in steps of #04, e.g. #A3, #A7, #AB...) - IDE Interface Registers**
    *   **ADDRESS/DECODING:** `xxxxxxxx101CBA11`
    *   **READ/WRITE:** `IDE#1Fx`. Access to IDE task file registers. `CBA` bits (A2,A1,A0 of IDE register address) select the specific IDE register. This is the same scheme as DivIDE, suggesting compatibility.

**MB-02+ Summary:** A highly integrated clone with a rich set of peripherals, using a mix of standard (WD2797, 8255, DivIDE-like IDE) and specific port addresses for its RTC and DMA. Its memory management via `#17` would be key to understanding its full capabilities.

---

## SOUNDRIVE (Covered previously but reiterated from table)

*   **SOUNDRIVE v1.02:**
    *   `#0F, #1F, #3F, #4F, #5F, #7F` for Left channels A, B, Control, and Right channels C, D, Control respectively. (Direct DAC writes or 8255 control).
*   **SOUNDRIVE v1.05 (SOUNDRIVE/COVOX):**
    *   `#0F, #1F, #4F, #5F` (and mirrors `#F1, #F3, #F9, #FB`) for Left A, Left B, Right A, Right B DAC channels.

---

## ZXMMC+ Interface

A modern interface for SD/MMC cards, often including Kempston joystick and RS232.

**Key:**
*   `(*4)`: Refers to `http://www.zxbada.bbk.org/zxmmc/index.htm`.

### ZXMMC+ Ports

*   **Port `#1F` (31 decimal) - Kempston Joystick / Card Chip Select**
    *   **DECODING:** `xxxxxxxx00011111`
    *   **READ:** `KempstonIF`. Standard Kempston joystick input.
    *   **WRITE:** `CScard`. Chip Select for the SD/MMC card controller (often an SPI interface chip).

*   **Port `#3F` (63 decimal) - SPI Data Register**
    *   **DECODING:** `xxxxxxxx00111111`
    *   **READ:** `SPIRegRX`. Reads data received from the SD/MMC card via SPI.
    *   **WRITE:** `SPIRegTX`. Writes data to be sent to the SD/MMC card via SPI.

*   **Port `#5F` (95 decimal) - RS232 Status**
    *   **DECODING:** `xxxxxxxx01011111`
    *   **READ:** `RS232status`. Reads the status of the RS232 serial interface (e.g., data ready, transmit buffer empty).

*   **Port `#7F` (127 decimal) - RS232 Data Register**
    *   **DECODING:** `xxxxxxxx01111111`
    *   **READ:** `RS232RegRX`. Reads a byte received via RS232.
    *   **WRITE:** `RS232RegTX`. Writes a byte to be transmitted via RS232.

**ZXMMC+ Summary:** A multi-function card combining storage, joystick, and serial communication, using distinct ports for each function.

---

## KEMPSTON mouse Turbo by VELESOFT

An enhanced Kempston mouse interface, possibly with higher resolution or more buttons, and master/slave capability.

**Key:**
*   `(*1)`: Refers to `http://velesoft.speccy.cz/zxporty-cz.htm`.
*   `A=0/1 -> MASTER/SLAVE mouse`: Address bit A10 (`A` in the decoding) selects between two mice if supported.

### KEMPSTON mouse Turbo Ports

*   **Port `#1F/#7F` (31 / 127 decimal, A10 dependent)**
    *   **DECODING:** `xxxxxxxx0AA11111` (`AA` implies A6,A5 are used for something, or it's just A10 for master/slave from `Axxxx0x...` later). The port base is `#1F`. The `#7F` is likely a mirror or part of the master/slave select.
    *   **READ:** `AMIGAmou/FULLERjoy`. Reads mouse data (Amiga mouse protocol compatible) or Fuller joystick data. VELESOFT often made multi-standard interfaces.

*   **Ports for Turbo Mouse Data (A10 selects Master/Slave):**
    *   **`#7ADF/#FADF` (A10=0/1): Buttons**
        *   **DECODING:** `Axxxx0x011011111` (Base `#FADF` or `#7ADF`).
        *   **READ:** `KmouTrb_B`. Reads mouse button states.
    *   **`#7BDF/#FBDF` (A10=0/1): X-axis**
        *   **DECODING:** `Axxxx0x111011111`
        *   **READ:** `KmouTrb_X`. Reads mouse X-axis movement.
    *   **`#7EDF/#FEDF` (A10=0/1): Detection/Presence**
        *   **DECODING:** `Axxxx1x011011111`
        *   **READ:** `KmouTrb_detect`. Used to detect the presence of the Turbo mouse.
    *   **`#7FDF/#FFDF` (A10=0/1): Y-axis**
        *   **DECODING:** `Axxxx1x111011111`
        *   **READ:** `KmouTrb_Y`. Reads mouse Y-axis movement.

**KEMPSTON mouse Turbo Summary:** A specialized mouse interface using a set of four ports for X, Y, Buttons, and Detection, with an address bit (A10) differentiating master/slave units. Port `#1F` provides basic compatibility.

---

## ZX LPRINT-III (Printer Interface)

A specific printer interface.

### ZX LPRINT-III Ports

*   **Port `#7B` (123 decimal) - Control Port**
    *   **DECODING:** `xxxxxxxx0xxxx0xx` (very loose decoding for `#7B`, A0 must be 1).
    *   **WRITE:** `/STROBE`. Generates the printer strobe signal.
    *   **READ:** `ROMCS-off` (if reading, might disable an onboard ROM). This is an unusual read function for a printer strobe port.

*   **Port `#FB` (251 decimal) - Data/Status Port**
    *   **DECODING Write:** `xxxxxxxx1xxxx0xx` (loose decoding for `#FB`, A0 must be 1).
    *   **WRITE:** `DATA(/TxD)`. Outputs printer data byte. `/TxD` implies it might also support serial.
    *   **DECODING Read:** `00xxxxxx1xxxx0xx` (even looser, A15,A14 must be 0).
    *   **READ:** `ROMCS-on,BUSY,DSR`. Reads printer status (Busy, Data Set Ready) and can enable an onboard ROM.

**ZX LPRINT-III Summary:** Uses two common printer port addresses (`#7B`, `#FB`) but with specific interpretations for read/write and potential ROM control.

---

## D40/80 (Didaktik 40/80 Disk Interface)

Previously covered under Didaktik Gama (computer `5`), but listed separately here as a peripheral.

### D40/80 Ports

*   **`#81, #83, #85, #87`:** WD2797 Registers (Cmd/Stat, Track, Sector, Data). Decoding `xxxxxxxx10000BA1`.
*   **`#89, #8B, #8D, #8F`:** FD System Control (Drive select, motor, side). Decoding `xxxxxxxx10001xx1`.
*   **`#91` (145):** 8255 Off & Reset (for optional PPI).
*   **`#99` (153):** 8255 On (for optional PPI).
*   **`#1F, #3F, #5F, #7F`:** 8255 PPI access if present. Decoding `xxxxxxxx0BAxxxxx`.

---

## +D (PlusD) Disk and Printer Interface (Issue 4)

A popular interface by Miles Gordon Technology (MGT), primarily for floppy disk control (WD1772) and a parallel printer port. It also had a "snapshot" NMI button.

### +D Ports

*   **Ports `#E3, #EB, #F3, #FB` (WD1772 Registers)**
    *   **ADDRESS/DECODING:** `xxxxxxxx111BA01x` (A0 is don't care, B=A2, A=A1 of port address)
    *   **READ/WRITE:** Access to WD1772 FDC. The BA bits select the register:
        *   `BA=00` (Port `#E3` if A3=0, `#EB` if A3=1): WD Command (Write) / Status (Read).
        *   `BA=01` (Port `#F3` if A3=0, `#FB` if A3=1): WD Track Register.
        *   `BA=10` (Port `#E3` if A3=0, `#EB` if A3=1, with A2 high): WD Sector Register. (This mapping seems a bit odd; typically, 4 distinct addresses are used).
        *   *Standard +D Mapping is usually:*
            *   `#E3` (or `#EB`): Cmd/Stat
            *   `#E7` (or `#EF`): Track (this is ROMSEL in table, see below)
            *   `#F3` (or `#FB`): Sector
            *   `#F7` (or `#FF`): Data (this is LPT in table, see below)
        *   The table's `xxxxxxxx111BA01x` implies A3 and A4 (`B` and `A` in the address field `111BA011`) differentiate the 4 FDC ports. E.g., `#E3` (A4=0,A3=0), `#EB` (A4=0,A3=1), `#F3` (A4=1,A3=0), `#FB` (A4=1,A3=1).

*   **Port `#E7` (231 decimal) - ROM Select**
    *   **DECODING:** `xxxxxxxxx110011x`
    *   **WRITE:** `romsel`. Selects whether the +D ROM (G+DOS) or the Spectrum's internal ROM is active at `#0000`.

*   **Port `#EF` (239 decimal) - Floppy System / LPT Strobe**
    *   **DECODING:** `xxxxxxxxx110111x`
    *   **WRITE:** `FDsysLPTstrobe`. Floppy system controls (drive, motor, side) and printer strobe signal.

*   **Port `#F7` (247 decimal) - LPT Data / Busy**
    *   **DECODING:** `xxxxxxxxx111011x`
    *   **READ:** `LPTbusy`. Reads printer busy status.
    *   **WRITE:** `LPTdata`. Writes data to the printer.

**+D Summary:** A compact interface using a block of ports in the `#Ex` and `#Fx` range. The table's FDC port mapping is a bit condensed.

---

## ZX Interface 1 (ZXIF1)

Sinclair's own expansion for Microdrives, RS232, and rudimentary networking (ZX Net).

### ZXIF1 Ports

*   **Port `#E7` (231 decimal) - Microdrive Data Port / Control**
    *   **DECODING:** `xxxxxxxxx xx00xx1` (A0=1, A3=0, A4=0).
    *   **READ/WRITE:** `MicrodriveData`. Data transfer to/from Microdrive. Also involved in control sequences.

*   **Port `#EF` (239 decimal) - Status / Command Port**
    *   **DECODING:** `xxxxxxxxx xx01xx1` (A0=1, A3=0, A4=1).
    *   **READ:** `Status`. Reads status of IF1 operations (Microdrive, RS232, Net).
    *   **WRITE:** `Comnd`. Writes commands to the IF1.

*   **Port `#F7` (247 decimal) - RS232 / Network Data Port**
    *   **DECODING:** `xxxxxxxxx xx10xx1` (A0=1, A3=1, A4=0).
    *   **READ/WRITE:** `NetRS232Data`. Data transfer for RS232 or ZX Net.

**ZXIF1 Summary:** Uses three odd ports in the `#Ex/#Fx` range, clearly distinguished by A3/A4 for its three main functions.

---

## ZXMC-1 & ZXMC-2 (ZX Multi Card by Kamil Karimov)

Advanced SD card interfaces with RTC, Kempston mouse, and PC Keyboard support.

### ZXMC-1 (v1.2) Ports

*   **`#00EF` (239) & `#00DF` (223):** `Version` (Read). Reads card version. Different ports for different hardware revisions?
*   **`#FE` (254):** `Key` (Read). Standard keyboard port, likely passed through or used for PC keyboard mapping.
*   **`#08EF-#0FEF` (in steps of #0100):** `SDcard` (R/W). Access to SD card controller registers. `CBA` in address selects register.
*   **`#80DF-#87DF` (in steps of #0100):** `PCKey` (Read). Reads data from a connected PC keyboard. `CBA` in address likely for scan codes/status.
*   **`#E0EF-#E7EF` (in steps of #0100):** `ZXMCRTC` (R/W). Access to onboard RTC. `CBA` selects RTC register.
*   **`#F8EF-#FFEF` (in steps of #0100):** `ISACOM1` (R/W). Access to an emulated ISA COM1 port (8250 UART like). `CBA` selects UART register.
*   **`#FADF, #FBDF, #FFDF`:** `Kmou_B, Kmou_X, Kmou_Y` (Read). Kempston mouse buttons, X, Y. Standard Kempston mouse ports.

### ZXMC-2 Ports

Largely similar to ZXMC-1, with additions/changes for GLUK RTC.
*   **`#00F7` (247):** `Version` (Read).
*   **`#FE` (254):** `Key` (Read).
*   **`#08EF-#0FEF`:** `SDcard` (R/W).
*   **`#80DF-#87DF`:** `PCKey` (Read).
*   **`#BFF7` (49143):** `GLUKRTCData` (R/W). Data port for GLUK RTC.
*   **`#DFF7` (57335):** `GLUKRTCAdress` (Write). Address port for GLUK RTC.
*   **`#E0EF-#E7EF`:** `ZXMCRTC` (R/W). (Perhaps its own RTC or a secondary one).
*   **`#EFF7` (61431):** `GLUKRTCon/off` (Write). Enables/disables GLUK RTC.
*   **`#F8EF-#FFEF`:** `ISACOM1` (R/W).
*   **`#FADF, #FBDF, #FFDF`:** `Kmou_B, Kmou_X, Kmou_Y` (Read).

**ZXMC Summary:** Very comprehensive interfaces. They use blocks of ports where higher address bits (A8-A11 via `CBA` in table notation) select registers for a specific function (SD, PCKey, RTC, COM).

---

## Other Peripherals from the "PERIPHERALS DEVICES PORTS" section:

*   **`#0B/#6B` Z80DMA:** (A5 differentiates). Z80 DMA controller.
*   **`#0E` PC<->ZXDrBeep:** Link for PC to Spectrum communication.
*   **`#1F` KempstonIF/Kempston-M/AMIGA-MOUSE:** Various joystick/mouse on this port.
*   **`#3F` LIGHT PEN:** Light pen interface.
*   **`#B7` XTR modem:** Modem.
*   **`#EF` C-DOS modem:** Modem.
*   **`#F7` DIGITIZER(VMG):** VMG Digitizer.

## Keyboard Proface / Keyface

*   **`#FA` (250):** Write `Pro/Keyface`. Control for an alternative keyboard interface.
*   **`#FE` (254):** Read `Pro/Keyface`. Read from this keyboard interface (conflicts with ULA unless very specific decoding).

## SAM COUPE Sound SAA1099

The SAM Coupé computer had an SAA1099 sound chip (similar to two AYs but different architecture). This section implies its sound chip could be interfaced to a Spectrum.
*   **`#00FF` (255):** Write `Data`. SAA1099 Data Register.
*   **`#01FF` (511):** Write `Adress`. SAA1099 Address Register.
    * Decoding `xxxxxxx011111111` and `xxxxxxx111111111` respectively.

## DMA USC (DMA Ultra Sound Controller)

A complex sound card with DMA (8237) and timers (8253).
*   **`#0777-#3777`:** Sound Channel control.
*   **`#0C77-#FC77`:** 8237 DMA controller registers.
*   **`#3D77-#FD77`:** 8253 Timer chip 1.
*   **`#3E77-#FE77`:** 8253 Timer chip 2.
*   **`#3F77-#FF77`:** Volume control.
*   **`#F777`:** DMA Interrupt Mask.
    *   All these use complex `BA` or `DCBA` bits in the port address for register selection within their blocks.

## COM ports by VIC

A multi-serial port interface using 8251 UARTs and an 8253 timer.
*   **`#80F7-#83F7`:** 8253 Timer.
*   **`#90F7/#91F7`:** 8251 UART 1 (Data/Control).
*   **`#A0F7/#A1F7`:** 8251 UART 2.
*   **`#B0F7/#B1F7`:** 8251 UART 3.
    *   `A` bit in address (A0) differentiates Data from Control/Status for 8251s.

## GLUK CMOS RTC

A common RTC interface seen on many clones.
*   **`#BFF7` (49143):** `DS1685Data` (R/W). (DS1685 or compatible RTC data).
*   **`#DFF7` (57335):** `DS1685Adress` (Write). RTC Address register.
*   **`#EFF7` (61431):** `on/off` (Write). Enable/disable RTC.

## ZX Interface 2

Sinclair's interface for ROM cartridges and two joysticks (non-Kempston, using keyboard matrix like mapping).
*   **`#EFFE` (61438):** Read Right Joystick.
*   **`#F7FE` (63486):** Read Left Joystick.
    *   Decoding `xxx0xxxxxxxxxxx0` and `xxxx0xxxxxxxxxx0` respectively, using A12 and A11 to differentiate.

## ISA COM1/Modem (8250/etc.) by Kondratyev

Maps a standard PC COM port (8250 UART family) to Spectrum I/O. `A` bit in port address (A3) used for IRQ4 enable/disable.
*   **`#F0EF/#F8EF` (A3=0/1):** RBR/THR (Read/Write Data), Divisor Latch LSB.
*   **`#F1EF/#F9EF`:** IER (Interrupt Enable), Divisor Latch MSB.
*   **`#F2EF/#FAEF`:** IIR (Interrupt ID - Read).
*   **`#F3EF/#FBEF`:** LCR (Line Control - R/W).
*   **`#F4EF/#FCEF`:** MCR (Modem Control - R/W).
*   **`#F5EF/#FDEF`:** LSR (Line Status - Read).
*   **`#F6EF/#FEEF`:** MSR (Modem Status - Read).
*   **`#F7EF/#FFEF`:** Scratch Register (R/W).

## KEMPSTON mouse / USSR KEMPSTON mouse

Standard Kempston mouse ports.
*   **`#FADF` (64223):** `Kmou_B` (Buttons).
*   **`#FBDF` (64479):** `Kmou_X` (X-axis).
*   **`#FFDF` (65503):** `Kmou_Y` (Y-axis).
    *   The "USSR" version has a slightly different decoding mask (e.g. `xxxxx0x01x0xxxx1` vs `xxxxxx10xx0xxxxx`), suggesting it might decode fewer address lines or look for specific patterns on other lines.
