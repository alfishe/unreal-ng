# FDC Driver Disassembly (0x1400-0x1800)

Low-level WD1793 FDC access routines used by the ATM disk monitor.

## Port Map

| Port | Read | Write | Purpose |
|------|------|-------|---------|
| 0x1F | Status | Command | FDC command/status register |
| 0x3F | Track | Track | Track register |
| 0x5F | Sector | Sector | Sector register |
| 0x7F | Data | Data | Data register |
| 0xFF | Status | Control | Beta 128 system register |

## Key Variables

| Address | Size | Purpose |
|---------|------|---------|
| 0x5FA6 | 1 | Channel number |
| 0x5FA7 | 1 | Operation type |
| 0x5FA9 | 2 | Block address |
| 0x5FAD | 2 | DMA address |
| 0x5FB0 | 2 | Current sector/track |
| 0x5FB7 | 1 | Retry counter |
| 0x5FB8 | 32 | Drive table |
| 0x7F40 | 7 | READ ADDRESS buffer (C,H,R,N,CRC1,CRC2) |
| 0x7FC0 | 256 | Sector data buffer |

## Routines

### 0x1454 - Get Drive Table Entry
```z80
0x1454: 21 B8 5F   LD HL, #5FB8    ; Drive table base
0x1457: FD 7E 02   LD A, (IY+#02)  ; Get drive number (DUS)
0x145A: C3 06 11   JP #1106        ; Index into table
```

### 0x145D - Setup FDC for I/O
Prepares FDC for disk operation.

```z80
0x145D: ED 73 8E 5F  LD (#5F8E), SP  ; Save stack
0x1461: FD 4E 02     LD C, (IY+#02)  ; Drive number
0x1464: AF           XOR A
0x1465: DD 96 F0     SUB (IX-#10)    ; Current side
0x1468: 2F           CPL
0x1469: E6 10        AND #10         ; Extract side bit
0x146B: B1           OR C            ; Combine with drive
0x146C: FD 4E 1C     LD C, (IY+#1C)  ; DFMFM (density)
0x146F: A9           XOR C
0x1470: E6 BF        AND #BF         ; Mask density bit
0x1472: A9           XOR C
0x1473: F6 0C        OR #0C          ; Set HLT and RST
0x1475: D3 FF        OUT (#FF), A    ; Write Beta register
```

### 0x148E - Restore to Track 0
Issues RESTORE command to home the drive head.

```z80
0x148E: 3E 01      LD A, #01
0x1490: 3D         DEC A           ; A = 0
0x1491: D3 3F      OUT (#3F), A    ; Track register = 0
0x1493: AF         XOR A
0x1494: C9         RET
```

### 0x149E - FDC Reset
Resets the FDC controller.

```z80
0x149E: F3         DI
0x149F: 3E 08      LD A, #08       ; Drive 0, motor on
0x14A1: D3 FF      OUT (#FF), A    ; Beta register
0x14A3: 06 00      LD B, #00
0x14A5: 10 FE      DJNZ $          ; Delay loop
0x14A7: 3E 0C      LD A, #0C       ; HLT + RST bits
0x14A9: D3 FF      OUT (#FF), A
0x14AB: 3E D0      LD A, #D0       ; Force interrupt command
0x14AD: D3 1F      OUT (#1F), A
```

### 0x14AD - Get Current Track
Reads current track from system variable.

```z80
0x14AD: 3A B0 5F   LD A, (#5FB0)   ; Get track number
```

### 0x14B0 - Setup Seek Operation
Prepares SEEK command parameters.

```z80
0x14B0: FD 4E 20   LD C, (IY+#20)  ; Get stepping rate
0x14B3: CB D9      SET 3, C        ; Enable verify flag
0x14B5: B7         OR A            ; Check if track = 0
0x14B6: 77         LD (HL), A
0x14B7: 28 0F      JR Z, #14C8     ; Skip if track 0
0x14B9: D3 7F      OUT (#7F), A    ; Data = target track
0x14BB: CB E1      SET 4, C        ; Enable head load
; ... setup side and density bits ...
0x14CB: 79         LD A, C
0x14CC: D3 1F      OUT (#1F), A    ; Issue SEEK command
```

### 0x14F0 - Read Address
Issues READ ADDRESS command to read sector ID.

```z80
0x14F0: 3E C0      LD A, #C0       ; READ ADDRESS command
0x14F2: 21 40 7F   LD HL, #7F40    ; ID buffer address
0x14F5: 01 7F 08   LD BC, #087F    ; Port 7F, 8 iterations
0x14F8: 11 00 00   LD DE, #0000    ; Timeout counter
0x14FB: F3         DI
0x14FC: D3 1F      OUT (#1F), A    ; Issue command
0x14FE: CD 83 15   CALL #1583      ; Wait for data
; ... read ID bytes into buffer ...
; After command: (#7F40) = cylinder from ID field
```

### 0x14FC - Set Track from ID Buffer
After READ ADDRESS, uses the cylinder from ID to set track register.

```z80
0x14FC: 3A 40 7F   LD A, (#7F40)   ; Load cylinder from buffer
0x14FF: D3 3F      OUT (#3F), A    ; Set FDC track register
```

### 0x15A0 - Read Sector
Executes sector read operation.

```z80
0x15A0: CD 50 15   CALL #1550      ; Setup DMA
0x15A3: 0E 1C      LD C, #1C       ; Error mask
0x15A5: D9         EXX
0x15A6: F6 80      OR #80          ; Add READ bit
0x15A8: F3         DI
0x15A9: D3 1F      OUT (#1F), A    ; Issue command
0x15AB: CD 83 15   CALL #1583      ; Wait for DRQ/completion
; ... transfer data ...
```

### 0x15F1 - Write Sector
Executes sector write operation.

```z80
0x15F1: CD 50 15   CALL #1550      ; Setup DMA
0x15F4: 0E 7C      LD C, #7C       ; Error mask
0x15F6: D9         EXX
0x15F7: F6 A0      OR #A0          ; Add WRITE bit
0x15F9: F3         DI
0x15FA: D3 1F      OUT (#1F), A    ; Issue command
```

### 0x1583 - Wait for DRQ/Completion
Waits for FDC to signal data request or command completion.

```z80
0x1583: DB FF      IN A, (#FF)     ; Read Beta status
0x1585: E6 C0      AND #C0         ; Mask DRQ + INTRQ
0x1587: FA DF 15   JP M, #15DF     ; Jump if INTRQ (bit 7)
0x158A: 20 11      JR NZ, #159D    ; Jump if DRQ (bit 6)
0x158C: 1B         DEC DE          ; Decrement timeout
0x158D: 7A         LD A, D
0x158E: B3         OR E
0x158F: 20 F2      JR NZ, #1583    ; Loop until timeout
0x1591: 10 F0      DJNZ #1583      ; Outer loop
; ... timeout error handling ...
```

## Track Calculation

The track number is calculated by the disk monitor before calling FDC routines:

1. CP/M passes logical record number to BDOS
2. BDOS calls BIOS READ/WRITE with track and sector
3. BIOS calculates: `physical_track = track + DBEGCYL`
4. Result stored in (#5FB0) for FDC access

**Important**: The ATM BIOS uses DBEGCYL (starting cylinder) but does NOT appear to use DALTCYL (system tracks) separately. The system tracks offset must be incorporated into DBEGCYL or the disk format itself.

## Beta 128 System Register (0xFF)

Write format:
| Bit | Purpose |
|-----|---------|
| 0-1 | Drive select (0-3) |
| 2 | /RESET (active low) |
| 3 | /HLT (active low) |
| 4 | Side select |
| 5 | Density (unused) |
| 6 | Reserved |
| 7 | Reserved |

Read format:
| Bit | Purpose |
|-----|---------|
| 6 | DRQ (data request) |
| 7 | INTRQ (interrupt request) |
