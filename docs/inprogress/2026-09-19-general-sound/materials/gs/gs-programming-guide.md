# General Sound Programming Guide

*Compiled from ZX-News #26 (1997), ZX-Format #06 (1997), and firmware source documentation.*

## 1. Overview

General Sound (GS) is an independent microprocessor system featuring:
- **CPU:** Z80 @ 12 MHz
- **ROM:** 32 KB (2 × 16 KB pages)
- **RAM:** 128 KB (112 KB user-accessible for modules/samples)
- **Audio:** 4 independent 8-bit channels with 6-bit volume control
- **Interrupt Rate:** 37.5 kHz (12 MHz / 320 cycles)

GS operates as an autonomous coprocessor, communicating with the host ZX Spectrum via I/O ports.

---

## 2. Port Interface

### 2.1 Host-Side Ports (ZX Spectrum)

| Port | Hex | Direction | Function |
|:-----|:----|:----------|:---------|
| 179 | #B3 | Write | Send data byte to GS (sets bit 7 of status) |
| 179 | #B3 | Read | Read data byte from GS (clears bit 7 of status) |
| 187 | #BB | Write | Send command byte to GS (sets bit 0 of status) |
| 187 | #BB | Read | Read status register |
| 51 | #33 | Write | Control: bit 7 = reset, bit 6 = NMI |

**Port Decoding:** `(port AND #F7) = #B3` — bit 3 selects data (0) vs command (1).

### 2.2 Status Register (Port #BB Read)

| Bit | Name | Description |
|:----|:-----|:------------|
| 0 | CMD | Command pending (set when host sends command) |
| 1-6 | — | Reserved, read as 1 |
| 7 | DAT | Data pending (set when host sends data, cleared on read) |

### 2.3 Programming Pattern

```z80
; Send command to GS
SendCommand:
    LD   A,command_byte
    OUT  (#BB),A        ; Send command
WaitCommand:
    IN   A,(#BB)        ; Read status
    RRCA                ; Check bit 0
    JR   C,WaitCommand  ; Wait until command processed
    RET

; Send data to GS  
SendData:
    LD   A,data_byte
    OUT  (#B3),A        ; Send data
WaitData:
    IN   A,(#BB)        ; Read status
    RLCA                ; Check bit 7
    JR   C,WaitData     ; Wait until data received
    RET

; Read data from GS
ReadData:
    IN   A,(#BB)        ; Read status
    RLCA                ; Check bit 7
    JR   NC,ReadData    ; Wait until data available
    IN   A,(#B3)        ; Read data (clears bit 7)
    RET
```

---

## 3. Command Reference

### 3.1 Initialization Commands

| Cmd | Hex | Description |
|:----|:----|:------------|
| 0 | #00 | Full reset (cold start) |
| 243 | #F3 | Warm restart (preserve modules) |
| 244 | #F4 | Software reset |

### 3.2 Memory Query Commands

| Cmd | Hex | Parameters | Returns | Description |
|:----|:----|:-----------|:--------|:------------|
| 32 | #20 | — | 2 bytes | Total RAM (KB, low/high) |
| 33 | #21 | — | 2 bytes | Free RAM (KB, low/high) |
| 35 | #23 | — | 1 byte | Number of pages |

### 3.3 Module Playback Commands

| Cmd | Hex | Parameters | Description |
|:----|:----|:-----------|:------------|
| 48 | #30 | module data | Load MOD file to GS memory |
| 49 | #31 | — | Start module playback |
| 50 | #32 | — | Stop module playback |
| 51 | #33 | — | Continue module playback |

**Module Loading Sequence:**
```z80
    LD   A,#30          ; Load module command
    OUT  (#BB),A
    ; ... wait for command accepted ...
    ; Send module bytes one by one via port #B3
    ; ... send data, wait for each byte ...
    ; Send end marker or use length prefix
```

### 3.4 Effect/Sample Commands

| Cmd | Hex | Parameters | Description |
|:----|:----|:-----------|:------------|
| 56 | #38 | sample, note | Load sample with note |
| 57 | #39 | sample, note, vol | Load sample with note and volume |
| 58 | #3A | sample, priority | Load sample with priority |
| 59-71 | #3B-#47 | varies | Extended sample control |

### 3.5 Global Volume Control

| Cmd | Hex | Parameters | Description |
|:----|:----|:-----------|:------------|
| 80 | #50 | volume (0-63) | Set global volume |
| 81 | #51 | — | Get current global volume |

### 3.6 Position/Sync Commands

| Cmd | Hex | Returns | Description |
|:----|:----|:--------|:------------|
| 96 | #60 | 1 byte | Get song position |
| 97 | #61 | 1 byte | Get pattern position (row) |
| 98 | #62 | 4 bytes | Get active channels bitmap |
| 99 | #63 | 2 bytes | Get current pattern number |
| 100 | #64 | 2 bytes | Get current sample playing |

### 3.7 Direct Channel Playback

| Cmd | Hex | Parameters | Description |
|:----|:----|:-----------|:------------|
| 128 | #80 | sample | Play sample in channel 1 |
| 129 | #81 | sample | Play sample in channel 2 |
| 130 | #82 | sample | Play sample in channel 3 |
| 131 | #83 | sample | Play sample in channel 4 |
| 136-139 | #88-#8B | sample, note | Play with note |
| 144-147 | #90-#93 | sample, note, vol | Play with note and volume |
| 152-155 | #98-#9B | sample, note, vol, priority | Full control |

### 3.8 ROM v1.05a New Commands

| Cmd | Hex | Parameters | Description |
|:----|:----|:-----------|:------------|
| 106 | #6A | mode (0/1) | Set player mode (F00 effect handling) |
| 107 | #6B | loopLen_lo, loopLen_hi | Set minimum loop length for relooper |

---

## 4. Memory Layout

### 4.1 GS Internal Memory Map

```
#0000-#3FFF  ROM page 0 (or RAM0 when NOROM set)
#4000-#7FFF  RAM page 3 (fixed, DAC buffers)
#8000-#BFFF  ROM/RAM page N (switchable)
#C000-#FFFF  ROM/RAM page M (switchable)
```

### 4.2 DAC Sample Addresses

Channels are mapped to specific memory read addresses:

| Channel | Address Range | Description |
|:--------|:--------------|:------------|
| 1 | #6000-#60FF | Left output (L) |
| 2 | #6100-#61FF | Right output (R) |
| 3 | #6200-#62FF | Right output (R) |
| 4 | #6300-#63FF | Left output (L) |

Reading from these addresses automatically outputs the byte to the corresponding DAC channel.

### 4.3 System Variables (ROM)

| Address | Content |
|:--------|:--------|
| #0004 | ROM version in BCD format |
| #0100 | Copyright string (24 bytes) |
| #0800 | Sample buffer area |

---

## 5. Volume and Mixing

### 5.1 Volume Levels

- **Per-channel volume:** 0-63 (6-bit)
- **Global volume:** 0-63 (6-bit)
- **Sample data:** 8-bit signed (-128 to +127)

### 5.2 Stereo Configuration

- Channels 1, 4 → Left
- Channels 2, 3 → Right

### 5.3 Volume Calculation

```
output = ((sample - 128) * volume * global_volume) / 4096 + 128
```

---

## 6. MOD Format Support

GS supports standard Amiga MOD files with these characteristics:
- **Samples:** Up to 31 instruments
- **Patterns:** Up to 64 (v1.05a: up to 127)
- **Channels:** 4 (mapped to GS channels)
- **Effects:** Most ProTracker effects supported

**Unsupported Effects:**
- E0x (Filter on/off) — no hardware filter
- EFx (Invert loop) — not implemented

**Note:** Load modules before loading additional samples.

---

## 7. Timing

### 7.1 GS CPU Timing

- **Clock:** 12 MHz
- **Interrupt period:** 320 T-states (37.5 kHz)
- **Cycles per ZX frame (50 Hz):** 240,000 T-states

### 7.2 Host-GS Synchronization

GS runs asynchronously. Use position commands (#60-#64) for audio/video sync.

---

## 8. Example: Play Sample

```z80
; Play sample #01 in channel 1 at note C-2, volume 48
PlaySample:
    LD   A,#01          ; Sample number
    OUT  (#B3),A        ; Send data
    CALL WaitData
    
    LD   A,428 AND #FF  ; Period low (C-2)
    OUT  (#B3),A
    CALL WaitData
    
    LD   A,428 / 256    ; Period high
    OUT  (#B3),A
    CALL WaitData
    
    LD   A,48           ; Volume
    OUT  (#B3),A
    CALL WaitData
    
    LD   A,#90          ; Play in channel 1 with note+vol
    OUT  (#BB),A        ; Send command
    JP   WaitCommand
```

---

## 9. References

1. [ZX-News #26, July 1997](https://zxpress.ru/ru/ezines/zx-news/26/general-sound-muzykalnaya-karta-dlya-zx-spectrum-s-podderzhkoy-8-bitnogo-zvuka-4-kanalov-i-mod) — "General Sound: Programming Guide"
2. [GS Programming Manual v1.03](https://web.archive.org/web/20090228175029/http://gs.zxnet.ru/documentation/gs_prog.pdf) — Official documentation (PDF, Russian)
3. [psbhlw/gs-firmware](https://github.com/psbhlw/gs-firmware) — ROM source, schematics, bugfixes
4. [NedoPC NeoGS](http://nedopc.com/gs/ngs.php) — NeoGS documentation and firmware
5. [Spectrum Computing](https://spectrumcomputing.co.uk/entry/1000171/Hardware/General_Sound) — Hardware database entry
6. [Unreal Speccy](https://github.com/alfishe/unrealspeccy/blob/master/gsz80.cpp) — Reference implementation (gsz80.cpp)
7. [Xpeccy](https://github.com/nicstim/Xpeccy/blob/master/src/libxpeccy/sound/gs.c) — Alternative implementation (gs.c)

---

*Document compiled 2026-09-19 for Unreal-NG emulator implementation.*
