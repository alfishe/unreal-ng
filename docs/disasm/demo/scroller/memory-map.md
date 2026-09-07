# "Scroller by Demarche" — 128K Memory Map & Banking Architecture

## 1. Z80 Address Space Layout

The ZX Spectrum 128K addresses a 64 KB address space divided into four 16 KB banks:

```
+---------------------------------------------------------------+
| Address Range | Bank   | Mapping                              |
+---------------+--------+--------------------------------------+
| $0000 - $3FFF | Bank 0 | ROM (Switchable: 128K / 48K / TR-DOS)|
| $4000 - $7FFF | Bank 1 | Fixed: RAM Page 5 (Screen 1 & System)|
| $8000 - $BFFF | Bank 2 | Fixed: RAM Page 2 (Main Demo Engine) |
| $C000 - $FFFF | Bank 3 | Switchable: RAM Pages 0-7 (Port 7FFD)|
+---------------------------------------------------------------+
```

---

## 2. 128K RAM Page Allocation

| RAM Page | Address Range | Fixed/Paged | Contents & Lifecycle |
|:---:|:---:|:---:|---|
| **Page 0** | `$C000`–`$FFFF` | Paged (Bank 3) | **Audio Samples Block 1**: Depacked from `SCROLL10.C` by Depack Call #2. Contains 8-bit digital Covox sound samples streamed during playback. |
| **Page 1** | `$C000`–`$FFFF` | Paged (Bank 3) | **Audio Samples Block 2**: Depacked from `SCROLL11.C` by Depack Call #3. Sound samples and drum patterns. |
| **Page 2** | `$8000`–`$BFFF` | **Fixed (Bank 2)** | **Main Demo Body & Runtime**: Depacked from `SCROLL12.C` by Depack Call #6.<br>- `$8000`–`$9B6A`: Main graphics scroller engine (`INIT1` entry at `$8000`).<br>- `$9B6B`–`$9CD5`: Covox menu, keyboard input polling.<br>- `$9CD6`–`$9D4A`: `STARTDEMO` transition, fade-out, seek routine, jump to `$8000`.<br>- `$BE00`–`$BF01`: IM2 interrupt vector table (filled with byte `$BF`).<br>- `$BF02`–`$BFBE`: `IM2INI` interrupt setup.<br>- `$BFBF`–`$BFFF`: IM2 50Hz interrupt handler & Covox audio sequencer. |
| **Page 3** | `$C000`–`$FFFF` | Paged (Bank 3) | **Audio Samples Block 3**: Depacked from `SCROLL13.C` by Depack Call #4. Background melody samples. |
| **Page 4** | `$C000`–`$FFFF` | Paged (Bank 3) | **Staging Area for `SCROLL12`**: Line 80 sets `#7FFD = 0x14` and loads 48 raw sectors (12,155 bytes) of `SCROLL12.C` into `$C000`. In Call #6, the MegaLZ depacker reads this raw stream and decompresses it into **Page 2** (`$8000`–`$BFFF`). |
| **Page 5** | `$4000`–`$7FFF` | **Fixed (Bank 1)** | **Standard Display Memory & System Workspace**:<br>- `$4000`–`$57FF`: Primary bitmap screen pixels (256x192).<br>- `$5800`–`$5AFF`: Screen attribute memory.<br>- `$5B00`–`$5B13`: 128K Sinclair editor SWAP routine (in printer buffer).<br>- `$5B5C`: `BANK_M` (128K system variable, shadow of port `#7FFD`).<br>- `$5C00`–`$5CBF`: Standard ZX Spectrum system variables.<br>- `$5D3B`–`$5FFF`: TR-DOS command buffer and BASIC program workspace (`SCROLLER.B`).<br>- `$6000`–`$61FF`: Z80 stack area (set to `$5FFF` / `$6200`).<br>- `$6200`–`$62B1`: `SCROLL00.C` depack dispatcher and MegaLZ decruncher.<br>- `$62B2`–`$69FF`: Depacked `SCROLL15.C` (engine helper routines). |
| **Page 6** | — | Paged (Bank 3) | Unused by demo. |
| **Page 7** | `$C000`–`$FFFF` | Paged (Bank 3) | **Audio Samples Block 4 & Extra Visual Effects**: Depacked from `SCROLL17.C` into `$DB00`–`$FFFF` by Depack Call #5. |

---

## 3. Depacker Table Architecture (`$6226`–`$6243`)

The depacker dispatcher in `SCROLL00.C` processes a 5-byte descriptor table at `$6226`:

```
Offset 0: Port #7FFD value (selects RAM page for Bank 3)
Offset 1..2: Destination address (16-bit little-endian)
Offset 3..4: Source address (16-bit little-endian)
```

| Call # | Trigger Line in BASIC | #7FFD Byte | Dest Page & Addr | Source Page & Addr | Purpose |
|:---:|:---:|:---:|:---:|:---:|---|
| **1** | Line 40 | `0x15` (Page 5) | Page 5 @ `$62B2` | Page 2 @ `$8000` | Depacks `SCROLL15` directly after `SCROLL00.C` in RAM. |
| **2** | Line 50 | `0x10` (Page 0) | Page 0 @ `$C000` | Page 2 @ `$8000` | Depacks `SCROLL10` into sample Page 0. |
| **3** | Line 60 | `0x11` (Page 1) | Page 1 @ `$C000` | Page 2 @ `$8000` | Depacks `SCROLL11` into sample Page 1. |
| **4** | Line 70 | `0x13` (Page 3) | Page 3 @ `$C000` | Page 2 @ `$8000` | Depacks `SCROLL13` into sample Page 3. |
| **5** | Line 80 | `0x17` (Page 7) | Page 7 @ `$DB00` | Page 2 @ `$8000` | Depacks `SCROLL17` into sample Page 7 at high offset. |
| **6** | Line 90 | `0x14` (**Page 4**) | **Page 2 @ `$8000`** | **Page 4 @ `$C000`** | **Final depack**: Decompresses main demo body from Page 4 into Page 2. |

---

## 4. Hardware I/O Port Specifications

### 4.1 Port `#7FFD` (Memory Banking Control)
Address line decoding on Pentagon 128: `A15 = 0`, `A1 = 0` (mask `0x8002 == 0x0000`, conventionally addressed with `BC = 0x7FFD` / `32765`).

```
Bit 0..2: RAM Page mapped to Bank 3 ($C000-$FFFF)
          000 = Page 0
          001 = Page 1
          010 = Page 2
          011 = Page 3
          100 = Page 4
          101 = Page 5
          110 = Page 6
          111 = Page 7
Bit 3:    Screen selection: 0 = Normal screen (Page 5), 1 = Shadow screen (Page 7)
Bit 4:    ROM selection: 0 = ROM 0 (128K Editor / Sinclair BASIC), 1 = ROM 1 (48K BASIC)
Bit 5:    Paging lock: 0 = Paging enabled, 1 = Paging disabled until hardware reset
Bit 6..7: Unused / floating
```

#### Values Used by the Demo:
- `0x15` (`00010101b`): Page 5 in bank 3, 48K ROM, normal screen.
- `0x10` (`00010000b`): Page 0 in bank 3, 48K ROM, normal screen.
- `0x11` (`00010001b`): Page 1 in bank 3, 48K ROM, normal screen.
- `0x13` (`00010011b`): Page 3 in bank 3, 48K ROM, normal screen.
- `0x17` (`00010111b`): Page 7 in bank 3, 48K ROM, normal screen.
- `0x14` (`00010100b`): Page 4 in bank 3, 48K ROM, normal screen.
- `0x1B` / `0x1D`: Used during live demo execution for dynamic audio streaming and bank switching.

### 4.2 Port `#FB` (Covox D/A Converter)
- **Port Address**: `#FB` (`251` decimal). On Pentagon 128, writes to port `#FB` feed an 8-bit R-2R resistor ladder DAC directly to audio out.
- **Data Format**: 8-bit unsigned PCM sample (`0x00` = minimum voltage, `0x80` = center/silence, `0xFF` = maximum voltage).
- **Playback Rate**: ~15.6 kHz to 21 kHz, synthesized in software interrupt loops.

### 4.3 Port `#FE` (ULA & Border)
- Used during demo initialization: `OUT (#FE), 0` to set border color to black.
- Read during menu: `IN A, (#FE)` / `IN A, (#7F)` to poll the Space key.
