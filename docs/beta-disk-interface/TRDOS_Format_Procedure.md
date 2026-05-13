# TR-DOS Format Procedure

High-level reconstruction of the TR-DOS FORMAT command flow based on disassembly analysis.

## Overview

The TR-DOS FORMAT command performs a complete disk format with **verification**. This explains why Read Sector calls appear during formatting - TR-DOS reads back each sector after formatting to verify successful writes.

## High-Level Format Procedure

```
FORMAT "DiskName"

┌─────────────────────────────────────────────────────────────────┐
│  1. PARSE COMMAND                                               │
│     - Parse disk name from command line                         │
│     - Check for '$' prefix → indicates double-sided format      │
│     - Ask user: Turbo (T) or Normal format?                     │
│       → Normal: 2:1 interleave (SEC_TABLE)                      │
│       → Turbo:  1:1 sequential (SEC_TURBO_TABLE)                │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. DETERMINE DISK GEOMETRY                                     │
│     - Get track count (40 or 80) from drive type                │
│     - Check if disk name starts with '$' → double-sided format  │
│     - Determine disk type ID (#16-#19) and sector count         │
│     → See detailed "Disk Geometry Detection" section below      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  3. RESTORE (Seek to Track 0)                                   │
│     CALL DISK_RESTORE     ; FDC Command: RESTORE ($00)          │
│     CALL SELECT_SIDE_0    ; Select Side 0 via system register   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  4. FORMAT SIDE 0 (All Tracks)                                  │
│                                                                 │
│     FOR track = 0 TO (num_tracks - 1):                          │
│         PRINT "HEAD 0 CYLINDER {track}"                         │
│         CALL FORMAT_TRACK(track, side=0)                        │
│         CALL VERIFY_TRACK(track, side=0)    ; ← Read Sectors!   │
│     NEXT track                                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  5. FORMAT SIDE 1 (If Double-Sided)                             │
│                                                                 │
│     IF disk_name starts with '$':                               │
│         FOR track = 0 TO (num_tracks - 1):                      │
│             PRINT "HEAD 1 CYLINDER {track}"                     │
│             CALL FORMAT_TRACK(track, side=1)                    │
│             CALL VERIFY_TRACK(track, side=1) ; ← Read Sectors!  │
│         NEXT track                                              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  6. INITIALIZE SYSTEM SECTOR (Track 0, Sector 9)                │
│                                                                 │
│     Buffer[E2h] = 01          ; Deleted files count             │
│     Buffer[E3h] = disk_type   ; Disk type ID (#16-#19)          │
│     Buffer[E5h] = free_sects  ; Free sectors (2 bytes)          │
│     Buffer[E7h] = 10h         ; TR-DOS magic byte               │
│     Buffer[EAh..F1h] = spaces ; Password area (8 bytes)         │
│     Buffer[F5h..FCh] = name   ; Disk label (8 bytes)            │
│                                                                 │
│     WRITE_SECTOR(track=0, sector=9, buffer)                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  7. DISPLAY RESULT                                              │
│     PRINT disk_name                                             │
│     PRINT "{free_sectors}/{total_sectors}"                      │
│     PROMPT "Repeat format? (Y/N)"                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## FORMAT_TRACK Procedure (Low-Level)

This procedure uses the FDC's **Write Track ($F0)** command to write the entire track structure.

```
PROCEDURE FORMAT_TRACK(track, side):
    
    ; Select physical head
    SET_SIDE(side)
    
    ; Seek to track if needed
    IF current_track != track:
        SEEK_TRACK(track)
    
    ; Send Write Track command to FDC
    OUT($1F) = $F0    ; Write Track command
    
    ; === Write Track Data Stream to FDC ===
    
    ; Pre-Index Gap (Gap 0)
    WRITE_BYTES($4E, 10)      ; 10 gap bytes
    WRITE_BYTES($00, 12)      ; 12 sync bytes  
    WRITE_BYTES($F5, 3)       ; 3x A1 sync (for IAM)
    WRITE_BYTE($FC)           ; Index Address Mark
    
    ; For each of 16 sectors (using interleave table):
    FOR i = 0 TO 15:
        sector_id = INTERLEAVE_TABLE[i]
        
        ; === ID Field ===
        WRITE_BYTES($4E, 10)   ; Gap 1
        WRITE_BYTES($00, 12)   ; Sync
        WRITE_BYTES($F5, 3)    ; 3x A1 (for IDAM)
        WRITE_BYTE($FE)        ; ID Address Mark
        WRITE_BYTE(track)      ; Track number
        WRITE_BYTE(side)       ; Side number (0 or 1)
        WRITE_BYTE(sector_id)  ; Sector number (1-16)
        WRITE_BYTE($01)        ; Sector size code (256 bytes)
        WRITE_BYTE($F7)        ; Generate CRC (FDC writes 2 bytes)
        
        ; === Data Field ===
        WRITE_BYTES($4E, 22)   ; Gap 2
        WRITE_BYTES($00, 12)   ; Sync
        WRITE_BYTES($F5, 3)    ; 3x A1 (for DAM)
        WRITE_BYTE($FB)        ; Data Address Mark
        WRITE_BYTES($00, 256)  ; Sector data (256 zero bytes)
        WRITE_BYTE($F7)        ; Generate CRC (FDC writes 2 bytes)
        
        ; === Gap 3 ===
        WRITE_BYTES($4E, 60)   ; Inter-sector gap
    
    NEXT i
    
    ; Gap 4 (fill until index pulse)
    WRITE_BYTES($4E, ~200)    ; Fill remaining track space
    
    ; Wait for FDC to complete (index pulse terminates Write Track)
    WAIT_FDC_COMPLETE()
    
    ; Check for write protect error
    IF FDC_STATUS & $40:
        ERROR "Disk is write protected"
```

---

## VERIFY_TRACK Procedure

After formatting each track, TR-DOS **reads back all 16 sectors** to verify they are readable:

```
PROCEDURE VERIFY_TRACK(track, side):
    
    ; Read all 16 sectors to verify format succeeded
    FOR sector = 1 TO 16:
        
        ; Issue Read Sector command
        SET_TRACK_REGISTER(track)
        SET_SECTOR_REGISTER(sector)
        SET_SIDE(side)
        
        ; FDC Command: Read Sector ($80)
        OUT($1F) = $80
        
        ; Read 256 bytes (verifies ID field CRC + Data CRC)
        FOR byte = 0 TO 255:
            WAIT_DRQ()
            dummy = IN($7F)   ; Read and discard data
        NEXT byte
        
        ; Check for errors
        IF FDC_STATUS & ($18):   ; CRC Error or Record Not Found
            ERROR "Format verify failed on Track {track}, Sector {sector}"
    
    NEXT sector
```

---

## Interleave Tables

```
NORMAL FORMAT (2:1 interleave):
SEC_TABLE:  01, 09, 02, 0A, 03, 0B, 04, 0C, 05, 0D, 06, 0E, 07, 0F, 08, 10

Physical order on disk: 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16

TURBO FORMAT (1:1 sequential):
SEC_TURBO:  01, 02, 03, 04, 05, 06, 07, 08, 09, 0A, 0B, 0C, 0D, 0E, 0F, 10

Physical order on disk: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
```

---

## Track Structure (Physical Layout)

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                           FORMATTED TRACK LAYOUT                          ║
╠═══════════════════════════════════════════════════════════════════════════╣
║ Index Pulse                                                               ║
║     │                                                                     ║
║     ▼                                                                     ║
║ ┌─────────────────────────────────────────────────────────────────────┐   ║
║ │ GAP0 │ SYNC │ IAM │ GAP1 │ [SECTOR 1] │ GAP3 │ [SECTOR 9] │ ...     │   ║
║ └─────────────────────────────────────────────────────────────────────┘   ║
║                                                                           ║
║ Each [SECTOR] contains:                                                   ║
║ ┌─────────────────────────────────────────────────────────────────────┐   ║
║ │ SYNC │ IDAM │ Track │ Side │ Sector │ Size │ CRC │ GAP2 │           │   ║
║ │ SYNC │ DAM  │ ──────────── 256 Data Bytes ───────────── │ CRC │     │   ║
║ └─────────────────────────────────────────────────────────────────────┘   ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

---

## Bytes Sent to FDC per Track

| Component | Bytes | Count | Total |
|-----------|-------|-------|-------|
| Pre-index gap | 4E, 00, F5, FC | 10+12+3+1 | 26 |
| Per sector ID field | | | 31 |
| Per sector Data field | | | 289 |
| Per sector Gap3 | 4E | 60 | 60 |
| **Per sector total** | | | **380** |
| **16 sectors** | | | **6,080** |
| **Track total** | | | **~6,106** |

FDC writes these bytes to disk using FM/MFM encoding, resulting in ~6,250 raw bytes on disk.

---

## WD1793 Commands Used During Format

| Phase | Command | Opcode | Description |
|-------|---------|--------|-------------|
| Init | RESTORE | $00 | Seek to track 0 |
| Position | SEEK | $18 | Seek to target track |
| Format | WRITE TRACK | $F0 | Write entire track structure |
| Verify | READ SECTOR | $80 | Read back sector to verify CRC |
| System | WRITE SECTOR | $A0 | Write system sector (Track 0, Sector 9) |

---

## Why Read Sector Appears in Format Logs

**Answer:** TR-DOS FORMAT includes a **verification pass**. After formatting each track with Write Track ($F0), it reads back all 16 sectors using Read Sector ($80) to verify:

1. ID Address Marks are readable
2. ID field CRC is valid  
3. Data Address Marks are readable
4. Data field CRC is valid

If any sector fails verification, TR-DOS reports a format error. This is why you see 256-byte Read Sector operations during the format procedure - it's the verification step ensuring the disk was formatted correctly.

---

## Disk Geometry Detection (Detailed Analysis)

> **IMPORTANT:** TR-DOS does **NOT** automatically detect disk geometry from hardware!

### Track Count Determination (Physical Detection)

**Location:** `x1FCA` ($1FCA) in TR-DOS 5.04T

TR-DOS **physically tests the drive** to determine the track count. It does NOT rely on stored configuration:

```asm
;Адрес #1FCA. Определение количества дорожек дисковода.
;Вход: установите время перемещения головки дисковода.
;Выход: в #5CD7 и регистре A будет количество дорожек дисковода.

x1FCA       CALL    x3E08       ; Get head step rate
            OR      #11         ; Build SEEK command
            LD      B,A
            LD      A,#32       ; Seek to track 50 (0x32)
            CALL    x3E44       ; Execute SEEK
            LD      A,2         ; Then seek to track 2
            CALL    x3E44       ; Execute SEEK
            CALL    x3DFD       ; Delay (wait for head to settle)
            IN      A,(#1F)     ; Read FDC status register
            AND     4           ; Bit 2 = TRACK00 signal
            LD      A,#50       ; Assume 80 tracks
            JR      Z,x1FE7     ; If TRACK00=0, drive has 80 tracks
            LD      A,#28       ; Else 40 tracks
x1FE7       LD      (#5CD7),A   ; Store track count
            RET
```

**Detection Algorithm:**
1. Seek to track 50 (`#32`)
2. Then seek to track 2
3. Check if TRACK00 signal (FDC status bit 2) is active
4. **If TRACK00=1** (head at track 0): Drive has **40 tracks** (physical barrier reached)
5. **If TRACK00=0** (head not at track 0): Drive has **80 tracks**

This works because a 40-track drive cannot physically move beyond ~42 tracks, so seeking to track 50 causes it to hit the end stop at track 39-40, then seeking to track 2 goes past track 0.

### Drive Type Table

**Location:** `#5CC8-#5CCB` (4 bytes, one per drive A-D)

TR-DOS stores drive type for each of 4 drives:

```asm
x3E11       LD      DE,#5CC8    ; Address of drive type table
            JR      x3E0B       ; Index by drive number from #5CF6
```

| Address | Drive | Bit 7 | Meaning |
|---------|-------|-------|---------|
| #5CC8 | A: | 0 = 40T | 1 = 80T |
| #5CC9 | B: | 0 = 40T | 1 = 80T |
| #5CCA | C: | 0 = 40T | 1 = 80T |
| #5CCB | D: | 0 = 40T | 1 = 80T |

The routine `x3DCB` (drive activation) calls `x1FCA` to detect track count and stores the result:

```asm
; From x3DCB (drive activation):
            CALL    x1FCA       ; Determine track count
            POP     HL          ; Get address of drive type cell
            CP      #50         ; 80 tracks?
            LD      A,0         ; Default: 40T (bit 7 = 0)
            JR      NZ,x3DF9
            LD      A,#80       ; 80T: set bit 7
x3DF9       LD      (HL),A      ; Store in drive type table
```

### ⚠️ CRITICAL BUG IN TR-DOS 5.04T

**From disassembly line 6378:**
```
;Внимание, ошибка!!! Не проверяется количество сторон дисковода 
; и предполагается, что он односторонний.
```

Translation: **"WARNING, ERROR!!! The number of drive sides is NOT checked and it is assumed to be single-sided."**

**This means:**
- TR-DOS 5.04T **CANNOT** detect double-sided drives automatically
- It **ALWAYS** assumes drives are single-sided
- Double-sided format (`$` prefix) works by user request, but the drive type byte won't reflect double-sided capability

### x3200 TURBO_FMT Return Value

**Location:** `x3200` ($3200) in TR-DOS 5.04T

```asm
;Адрес #3200. Выбор формата диска. 
;Устанавливает #5CE6 и #5CE8, а в аккумуляторе возвращает тип дисковода.

x3200       LD      HL,x322C    ; Print "PRESS T FOR TURBO FORMAT..."
            RST     #18
            CALL    x1052       ; Wait for keypress
            CP      "T"         ; Turbo format?
            JR      Z,x3217     ; If yes, use TURBO sector table
            LD      HL,x1FB9    ; Normal sector interleave table
            LD      (#5CE6),HL
            INC     HL
            LD      (#5CE8),HL
            JR      x3221
x3217       LD      HL,x325A    ; TURBO sector table (1:1)
            LD      (#5CE6),HL
            INC     HL
            LD      (#5CE8),HL
x3221       RST     #20         ; Clear lower screen
            DW      #0D6E
            LD      HL,x3312    ; Print "HEAD 0 CYLINDER 0"
            RST     #18
            CALL    x3E11       ; GET DRIVE TYPE (bit 7 = 80T)
            RET                 ; Return with drive type in A
```

**The return value in A (bit 7):**
- **Bit 7 = 0**: 40-track drive
- **Bit 7 = 1 (#80)**: 80-track drive

This value comes from the `#5CC8+drive_number` table which was populated by `x1FCA` physical detection.

### When Does Detection Happen?

**Not at init - On demand with caching!**

The physical detection (`x1FCA`) is called from `x3DCB` (drive activation) which is used by **many TR-DOS operations**:

```asm
; x3DCB - Drive activation (called by LOAD, SAVE, CAT, FORMAT, COPY, etc.)

x3DCB       LD      (#5CF6),A   ; Store current drive number
            ...
            CALL    x3E08       ; Get head step rate from #5CFA table
            AND     #80         ; Check bit 7
            JR      Z,x3DFA     ; If bit 7=0, skip detection (already known)
            
            ; Detection needed:
            CALL    x3DAD       ; Check if disk is in drive
            CALL    x3E16       ; Determine head step rate
            CALL    x3E11       ; Get drive type from table
            CP      #FF         ; Is it still unknown (#FF)?
            JR      Z,x3DFA     ; If #FF, skip (can't detect)
            
            PUSH    HL
            CALL    x1FCA       ; *** PHYSICAL DETECTION ***
            POP     HL
            CP      #50         ; 80 tracks?
            LD      A,0         ; Default 40T
            JR      NZ,x3DF9
            LD      A,#80       ; Set bit 7 for 80T
x3DF9       LD      (HL),A      ; Cache in drive type table
```

#### Physical Detection Mechanism (`x1FCA`)

The detection exploits the **physical geometry limitation** of 40-track drives:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  40-TRACK DRIVE (physical limit ~42 tracks)                                 │
│  ═══════════════════════════════════════════════════════════════════════════│
│                                                                             │
│  Step 1: SEEK to track 50                                                   │
│  ─────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   Head:  [Track 0]──▶──▶──▶──▶──▶──▶[Track 39/40]█ ← PHYSICAL STOP          │
│                                                    │                        │
│   FDC thinks head is at track 50, but physically   │                        │
│   the stepper motor hit the mechanical end stop.   │                        │
│                                                                             │
│  Step 2: SEEK to track 2                                                    │
│  ─────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   FDC calculates: current=50, target=2 → step OUT 48 times                  │
│                                                                             │
│   Head:  █[Track 39/40]◀──◀──◀──◀──[Past Track 0!]                          │
│          │                          │                                       │
│   Drive was really at track 39/40,  Head overshoots and hits TRACK 0        │
│   so 48 step-out pulses go WAY      sensor, activating TRACK00 signal       │
│   past track 0                                                              │
│                                                                             │
│  Result: FDC Status bit 2 (TRACK00) = 1 → 40-track drive detected           │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│  80-TRACK DRIVE (no physical limit at track 50)                             │
│  ═══════════════════════════════════════════════════════════════════════════│
│                                                                             │
│  Step 1: SEEK to track 50                                                   │
│  ─────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   Head:  [Track 0]──▶──▶──▶──▶──▶──▶[Track 50]     (no physical stop)       │
│                                                                             │
│   FDC and physical position match - head really IS at track 50              │
│                                                                             │
│  Step 2: SEEK to track 2                                                    │
│  ─────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   FDC calculates: current=50, target=2 → step OUT 48 times                  │
│                                                                             │
│   Head:  [Track 50]◀──◀──◀──◀──[Track 2]                                    │
│                                  │                                          │
│   48 step-out pulses from track 50 lands exactly at track 2                 │
│   Head is NOT at track 0                                                    │
│                                                                             │
│  Result: FDC Status bit 2 (TRACK00) = 0 → 80-track drive detected           │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key FDC Signals:**

| Signal | FDC Status Bit | Meaning |
|--------|----------------|---------|
| TRACK00 | Bit 2 | Head is at physical track 0 (optical/mechanical sensor) |

**Important Notes for Emulator:**
1. The emulator must simulate the **TRACK00 sensor** correctly
2. If head position < 0, physically clamp to 0 and set TRACK00 bit
3. On 40T virtual drives, seeking to track 50+ should be limited to ~39-40
4. The detection relies on **mismatch between FDC's logical position and physical position**

**Detection Logic:**
1. `x3DCB` checks if **head step rate** in `#5CFA+drive` has bit 7 set (`#80` or `#FF`)
2. If bit 7 = 0, drive is already known → skip detection
3. If bit 7 = 1, detection needed → call `x1FCA`
4. Result cached in `#5CC8+drive` table

**Initial State at TR-DOS Init (`x2F90`):**
```asm
x2F90       LD      HL,#FFFF
            LD      (#5CFA),HL  ; Drive step rates = #FF (unknown)
            LD      (#5CFC),HL
            LD      (#5CC8),HL  ; Drive types = #FF (unknown)
            LD      (#5CCA),HL
```

All 4 drive entries start as `#FF` (unknown). First access to each drive triggers detection.

**Operations That Call `x3DCB`:**
- `FORMAT` - via x1EC2
- `CAT` / `LIST` - directory listing
- `LOAD` / `SAVE` - file operations
- `COPY` / `MOVE` - file copying
- `ERASE` - file deletion
- Any disk access command

**Summary:** Detection is **lazy** - happens on first use of each drive, then cached until TR-DOS system variables are reset.

### Beta128 Port #FF Bit Definitions

**Write to Port #FF (System Control):**
```
Bit 7: Not used (some clones use for HDD)
Bit 6: Not used  
Bit 5: MFM density select (1 = MFM double density)
Bit 4: Head/side select (0 = side 0, 1 = side 1)
Bit 3: FDC master reset (active low)
Bit 2: Motor on (active low on some clones)
Bit 1-0: Drive select (00 = A, 01 = B, 10 = C, 11 = D)
```

**Read from Port #FF (Status):**
```
Bit 7: INTRQ (Interrupt Request from FDC)
Bit 6: DRQ (Data Request from FDC)  
Bits 5-0: Directly from FDC data bus (during DRQ assertion)
```

**Note:** Port #FF does **NOT** report drive type (40/80 track) or number of sides.
TR-DOS uses the physical detection routine `x1FCA` instead.

### Double-Sided Determination

**Location:** `$1F01-$1F18` in TR-DOS 5.04T

Double-sided format is triggered by the **`$` prefix** in the disk name - this is a **user-specified** option, not hardware detection:

```asm
1F01:  3A DD 5C      LD    A,(FILE_NAME)  ; Get first char of disk name
1F04:  FE 24         CP    '$'            ; Is it '$'?
1F06:  28 13         JR    Z,#1F1B        ; If NOT '$', skip side 1
...
1F16:  3E 80         LD    A,#80          ; Set double-sided flag
1F18:  32 DA 5C      LD    (#5CDA),A      ; Store flag
```

### Usage Examples

```
FORMAT "mydisk"     → Single-sided format (side 0 only)
FORMAT "$mydisk"    → Double-sided format (both sides)
```

### Final Disk Type Selection

**Location:** `$1F2B-$1F55` in TR-DOS 5.04T

Based on track count (`SC_0B`) and double-sided flag (`#5CDA`):

| Tracks | Sides | Disk Type | Free Sectors | Hex Value |
|--------|-------|-----------|--------------|----------|
| 40 | 1 | #19 | 624 | $0270 |
| 40 | 2 | #17 | 1264 | $04F0 |
| 80 | 1 | #18 | 1264 | $04F0 |
| 80 | 2 | #16 | 2544 | $09F0 |

```asm
; Decision tree at $1F2B:
1F31:  0A            LD    A,(BC)        ; A = track count
1F32:  FE 50         CP    #50           ; Is it 80 tracks?
1F34:  28 13         JR    Z,#1F49       ; Jump if 80 tracks

; 40 Track Path:
1F36:  1A            LD    A,(DE)        ; A = double-sided flag
1F37:  FE 80         CP    #80           ; Is it double-sided?
1F39:  28 07         JR    Z,#1F42       ; Jump if DS
1F3B:  3E 19         LD    A,#19         ; Type #19 = 40T SS
1F3D:  21 70 02      LD    HL,#0270      ; 624 sectors
...

; 80 Track Path:
1F49:  1A            LD    A,(DE)        ; A = double-sided flag
1F4A:  FE 80         CP    #80           ; Is it double-sided?
1F4C:  3E 18         LD    A,#18         ; Type #18 = 80T SS
1F4E:  20 F4         JR    NZ,#1F44      ; If SS, use 1264 sectors
1F50:  3E 16         LD    A,#16         ; Type #16 = 80T DS
1F52:  21 F0 09      LD    HL,#09F0      ; 2544 sectors
```

### Potential Emulator Issues

If you're only getting single-sided disks formatted:

1. **Not using `$` prefix** - User must explicitly use `FORMAT "$name"` for double-sided
2. **TURBO_FMT returning wrong value** - Bit 7 may not be set for 80-track drives
3. **`#5CDA` flag not being set** - The double-sided flag storage may be failing
4. **`FORMAT_SIDE_1_IF_NEEDED` not executing** - Check the logic at $20BD
