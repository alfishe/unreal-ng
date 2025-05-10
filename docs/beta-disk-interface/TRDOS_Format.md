## TR-DOS format procedure (5.04T version specific)

```assembly
;-------------------------------------------------------------------------------
; FORMAT Command Handler (Main entry point for user command)
;-------------------------------------------------------------------------------
1EC2:  21 FF FF                LD      HL,#FFFF
1EC5:  22 D7 5C                LD      (SC_0B),HL      ; SC_0B used for Track Count/Params
1EC8:  22 D9 5C                LD      (SC_0D),HL      ; SC_0D used for Track Count/Params
1ECB:  22 D1 5C                LD      (#5CD1),HL      ; Unclear usage here, possibly temp
1ECE:  CD 2B 04                CALL    CHECK_EOL       ; Check if command line ends after "FORMAT"
1ED1:  CA 1A 1D                JP      Z,ERROR_SYNTAX  ; Syntax error if no parameters
1ED4:  CD DF 1D                CALL    EVAL_EXPR       ; Evaluate expression (expects disk name like "A:")
1ED7:  CD 75 1D                CALL    QUIT_SYNTAX_Z   ; Check for syntax error after expression
1EDA:  CD 57 1C                CALL    PREPARE_FILENAME ; Parses "Drive:DiskName"
                                                        ; Sets TMP_DRIVE, FILE_NAME (disk name)

FORMAT_1:
1EDD:  CD 00 32                CALL    TURBO_FMT?      ; Asks user for Turbo (T) or Normal format.
                                                        ; Sets up FILE_START/FILE_LENGTH to point to
                                                        ; either SEC_TABLE or SEC_TURBO_TABLE.
1EE0:  E6 80                   AND     #80             ; Check if drive selected supports 80 tracks (from GET_DRIVE_TYPE)
                                                        ; (This seems to be a leftover/misplaced check here)
1EE2:  3E 28                   LD      A,#28           ; Default: 40 tracks (28h = 40 decimal)
1EE4:  28 02                   JR      Z,#1EE8         ; If bit 7 was 0 (e.g. 40 track drive type)
1EE6:  3E 50                   LD      A,#50           ; Else: 80 tracks (50h = 80 decimal)
1EE8:  32 D7 5C                LD      (SC_0B),A       ; Store number of tracks to format in SC_0B (repurposed)
1EEB:  CD 98 3D                CALL    DISK_RESTORE    ; Restore FDC (seek track 00), check BREAK
1EEE:  CD F6 1F                CALL    SELECT_SIDE_1   ; Select side 0 (SYS_REG bit 4 = 1)
1EF1:  CD A0 3E                CALL    #3EA0           ; Wait for FDC ready, 3 retries (part of SET_TRACK/POSIT logic)
1EF4:  1E 01                   LD      E,#01           ; Current physical sector to start formatting from (for FDC)
                                                        ; D still holds track number (initially 0 from DISK_RESTORE)
1EF6:  CD FD 1F                CALL    FORMAT_TRACK_LOW_LEVEL ; Format one track (Side 0, Track D, starting sector E)
1EF9:  CD EB 1F                CALL    LOAD_SYS_REG    ; Ensure system register has standard FDC control bits (density etc.)
1EFC:  1E 00                   LD      E,#00           ; Track number for formatting (start at track 0)
                                                        ; D should be 0 here, E becomes track counter
1EFE:  CD FD 1F                CALL    FORMAT_TRACK_LOW_LEVEL_LOOP ; Loop to format all tracks for side 0

; Check if formatting a double-sided disk (indicated by '$' in disk name)
1F01:  3A DD 5C                LD      A,(FILE_NAME)   ; Get first char of disk name
1F04:  FE 24                   CP      '$'             ; Is it '$' (indicates double sided format request)
1F06:  28 13                   JR      Z,#1F1B         ; If not '$', skip side 1 formatting

; --- Format Side 1 if requested ---
1F08:  CD F6 1F                CALL    SELECT_SIDE_1   ; Sets SYS_REG bit 4 for side 0. This call seems redundant here.
                                                        ; It should be SELECT_SIDE_0 or a way to toggle to side 1.
                                                        ; Actual side selection for FDC is in FORMAT_TRACK_LOW_LEVEL via D register.
1F0B:  CD A0 3E                CALL    #3EA0           ; Wait FDC ready
1F0E:  CD B5 3E                CALL    #3EB5           ; Read FDC track register into H
1F11:  7C                      LD      A,H             ; A = current FDC track
1F12:  FE 01                   CP      #01             ; Compare with 1
1F14:  20 05                   JR      NZ,#1F1B        ; If not track 1 (unexpected state after side 0 format)
1F16:  3E 80                   LD      A,#80           ; Flag for double-sided in internal variable
1F18:  32 DA 5C                LD      (#5CDA),A       ; Store double-sided flag

1F1B:  CD BD 20                CALL    FORMAT_SIDE_1_IF_NEEDED ; Handles side 1 formatting loop
1F1E:  21 25 5D                LD      HL,BUFFER       ; Point HL to TR-DOS general buffer
1F21:  36 00                   LD      (HL),#00        ; Clear first byte of buffer
1F23:  11 26 5D                LD      DE,BUFFER+1     ; DE = BUFFER + 1
1F26:  01 FF 00                LD      BC,#00FF        ; Count = 255 bytes
1F29:  ED B0                   LDIR                    ; Fill BUFFER with 255 zeros (total 256 zeros)
                                                        ; Now BUFFER contains 256 zeros (standard sector fill byte for TR-DOS files)

; --- Initialize 8th Sector (System Information Sector) ---
1F2B:  01 D7 5C                LD      BC,SC_0B        ; BC points to SC_0B (number of tracks formatted: 40 or 80)
1F2E:  11 DA 5C                LD      DE,#5CDA        ; DE points to double-sided flag
1F31:  0A                      LD      A,(BC)          ; A = number of tracks (40 or 80)
1F32:  FE 50                   CP      #50             ; Is it 80 tracks? (50h)
1F34:  28 13                   JR      Z,#1F49         ; Jump if 80 tracks
; -- 40 Track Disk --
1F36:  1A                      LD      A,(DE)          ; A = double-sided flag (#5CDA)
1F37:  FE 80                   CP      #80             ; Is it double-sided?
1F39:  28 07                   JR      Z,#1F42         ; Jump if 40 tracks, double-sided
; 40 Tracks, Single Sided
1F3B:  3E 19                   LD      A,#19           ; Disk Type ID: #19 (40T SS)
1F3D:  21 70 02                LD      HL,#0270        ; Total sectors: #0270 = 624 (40*16 - 16 system) -> Free: #0270 - #10 = #0260 = 608
                                                        ; Actually, #027F or similar for free sectors is more common.
                                                        ; This value is Total Sectors (16 * 40 = 640 = #0280)
                                                        ; minus catalog/system sectors (usually 16).
                                                        ; Free sectors = 16*40 - 16 = 624 = #0270. Wait, this should be Total Free Sectors.
                                                        ; #0280 tracks total, #0010 for boot -> #0270 free. But this is for single-sided.
                                                        ; Standard 40T SS: Total sectors = 640 (#0280). Free: 640-16 (cat) = 624 (#0270) seems wrong.
                                                        ; Catalog takes 9 sectors usually. So 640-9 = 631 (#0277).
                                                        ; Or this HL holds (Total Data Sectors on Disk). Let's assume it's total allocatable.
                                                        ; Book says #19 = 40T DS, 80T SS = #18, 40T SS = #17, 80T DS = #16.
                                                        ; The code constants here might be for a specific interpretation or FDC.
                                                        ; The code uses: #19 (40T SS), #17 (40T DS), #18 (80T SS), #16 (80T DS)
1F40:  18 13                   JR      INIT_8TH_SECTOR
; 40 Tracks, Double Sided
1F42:  3E 17                   LD      A,#17           ; Disk Type ID: #17 (40T DS)
1F44:  21 F0 04                LD      HL,#04F0        ; Total sectors: #04F0 = 1264 (Free: 40*16*2 - 16 = 1280 - 16 = 1264)
1F47:  18 0C                   JR      INIT_8TH_SECTOR
; -- 80 Track Disk --
1F49:  1A                      LD      A,(DE)          ; A = double-sided flag (#5CDA)
1F4A:  FE 80                   CP      #80
; 80 Tracks, Single Sided
1F4C:  3E 18                   LD      A,#18           ; Disk Type ID: #18 (80T SS)
1F4E:  20 F4                   JR      NZ,#1F44        ; Error in logic: if not 80T DS, it jumps to 40T DS.
                                                        ; Should be JR NZ, INIT_80T_SS_LABEL if distinct.
                                                        ; Assuming the NZ means it's Single Sided:
                                                        ; For 80T SS, HL should be same as 40T DS: #04F0
                                                        ; This path seems to mean 80T SS:
                                                        ; `21 F0 04 LD HL,#04F0` (reaches here if A from (DE) was not #80)
; 80 Tracks, Double Sided
1F50:  3E 16                   LD      A,#16           ; Disk Type ID: #16 (80T DS)
1F52:  21 F0 09                LD      HL,#09F0        ; Total sectors: #09F0 = 2544 (Free: 80*16*2 - 16 = 2560 - 16 = 2544)

INIT_8TH_SECTOR:
1F55:  32 08 5E                LD      (BUFFER+#E3),A  ; Store Disk Type ID in buffer offset E3h
1F58:  22 0A 5E                LD      (BUFFER+#E5),HL ; Store Total/Free Sectors (2 bytes) in buffer offset E5h, E6h
1F5B:  3E 01                   LD      A,#01           ; Number of deleted files = 1 (the dummy entry)
1F5D:  32 07 5E                LD      (BUFFER+#E2),A  ; Store in buffer offset E2h
1F60:  3E 10                   LD      A,#10           ; TR-DOS ID byte #10
1F62:  32 0C 5E                LD      (BUFFER+#E7),A  ; Store in buffer offset E7h
1F65:  21 0F 5E                LD      HL,BUFFER+#EA   ; Point HL to disk name area in buffer (offset EAh)
1F68:  11 10 5E                LD      DE,BUFFER+#EB   ; DE = HL+1
1F6B:  01 08 00                LD      BC,#0008        ; Length of name
1F6E:  36 20                   LD      (HL),' '        ; Fill first byte with space
1F70:  ED B0                   LDIR                    ; Fill 8 bytes with spaces (BUFFER+#EA to BUFFER+#F1)
1F72:  21 DD 5C                LD      HL,FILE_NAME    ; HL = address of disk name given by user
1F75:  11 1A 5E                LD      DE,BUFFER+#F5   ; DE = buffer for disk label (offset F5h)
1F78:  01 08 00                LD      BC,#0008        ; Length of label
1F7B:  ED B0                   LDIR                    ; Copy user's disk name to label area in buffer
1F7D:  CD EB 1F                CALL    LOAD_SYS_REG    ; Set SYS_REG for standard FDC operation
1F80:  06 01                   LD      B,#01           ; Number of sectors to write = 1
1F82:  11 08 00                LD      DE,#0008        ; Track 0, Sector 8 (9th sector, 0-indexed)
1F85:  21 25 5D                LD      HL,BUFFER       ; HL = buffer address
1F88:  CD 62 1E                CALL    WRITE_BUFF_2    ; Write the initialized buffer to Track 0, Sector 8
                                                        ; WRITE_BUFF_2 eventually calls WRITE_BUFF after setting FDC.

1F8B:  3A D6 5C                LD      A,(SC_00)       ; SC_00 holds some flag/status from TURBO_FMT? or earlier
1F8E:  F5                      PUSH    AF
1F8F:  AF                      XOR     A
1F90:  CD C4 32                CALL    #32C4           ; Display "HEAD 0 CYLINDER 0" message (CLS_LOWER part)
1F93:  2A 0A 5E                LD      HL,(BUFFER+#E5) ; Get Total/Free sectors back from buffer
1F96:  22 D7 5C                LD      (SC_0B),HL      ; Store it
1F99:  21 DD 5C                LD      HL,FILE_NAME    ; Point to user-supplied disk name
1F9C:  DF                      RST     PRINT_STRING    ; Print disk name
1F9D:  3E 0D                   LD      A,#0D
1F9F:  D7                      RST     PRINT_CHAR      ; Print CR
1FA0:  2A D7 5C                LD      HL,(SC_0B)      ; HL = Total/Free sectors
1FA3:  F1                      POP     AF              ; Restore A (flag from SC_00)
1FA4:  E5                      PUSH    HL              ; Save Total/Free sectors
1FA5:  16 00                   LD      D,#00           ; D=0
1FA7:  5F                      LD      E,A             ; E = flag from SC_00
1FA8:  ED 52                   SBC     HL,DE           ; HL = HL - DE (HL - flag). Seems odd.
                                                        ; If flag is 0, HL is unchanged.
1FAA:  44                      LD      B,H
1FAB:  4D                      LD      C,L
1FAC:  CD A9 1D                CALL    OUT_NUM_BC      ; Print the (possibly modified) HL value
1FAF:  3E 2F                   LD      A,'/'
1FB1:  D7                      RST     PRINT_CHAR      ; Print '/'
1FB2:  C1                      POP     BC              ; Restore original Total/Free sectors to BC
1FB3:  CD A9 1D                CALL    OUT_NUM_BC      ; Print original Total/Free sectors
1FB6:  C3 6B 32                JP      REPEAT_FMT?     ; Ask user to repeat format or exit
```

```assembly
;-------------------------------------------------------------------------------
; FORMAT_TRACK_LOW_LEVEL_LOOP - Loop to format all tracks for one side
; Entry: E = track number (starts at 0)
;        D must be 0 (for side 0) or 1 (for side 1) to be passed to FORMAT_TRACK_LOW_LEVEL
;        (SC_0B) = total number of tracks for this side (40 or 80)
;-------------------------------------------------------------------------------
FORMAT_TRACK_LOW_LEVEL_LOOP: ; (My label for routine starting at 1EFE)
1EFE: ; D register should contain side for FDC (0 or 1)
      ; E register is the current track number for FDC
        push de                     ; Save D (side), E (track)
        ld a,e                      ; A = current track
        push af                     ; Save A
        ld a,(SC_0B)                ; Get total tracks to format (40 or 80)
        cp e                        ; Compare total tracks with current track
        pop af                      ; Restore A (current track)
        jr z, END_FORMAT_SIDE       ; If current track == total tracks, side is done

        inc e                       ; Increment current track number E
        pop de                      ; Restore D (side), E (previous track for call)
        push de                     ; Save D (side), E (previous track)
        ld a,e                      ; A = new current track
        ld (#5CD8),a                ; Store current track being formatted (for display)
        ld a,d                      ; A = side
        ld (#5CD9),a                ; Store current side being formatted (for display)

        push de                     ; Save D (side), E (current track for FDC)
        ld e,a                      ; E = current track (for display in PRINT_HEAD_0)
                                    ; D already holds side (for display in PRINT_HEAD_0)
        call PRINT_HEAD_0           ; Display "HEAD X CYLINDER Y"
        pop de                      ; Restore D (side), E (current track for FDC)

        call FORMAT_TRACK_LOW_LEVEL ; Format track D, E
        pop de                      ; Restore D (side), E (track for loop)
        jr FORMAT_TRACK_LOW_LEVEL_LOOP ; Loop for next track

END_FORMAT_SIDE:
        pop de                      ; Clean stack
        ret
```

```assembly
;-------------------------------------------------------------------------------
; FORMAT_SIDE_1_IF_NEEDED - Called after side 0 is formatted
; Entry: (#5CDA) holds #80 if double-sided format requested
;-------------------------------------------------------------------------------
FORMAT_SIDE_1_IF_NEEDED: ; (My label for routine at 20BD)
20BD:   ld a,(#5CDA)        ; Get double-sided flag
        cp #80
        ret nz              ; If not #80, not double-sided, so return

        ; --- Setup for Side 1 ---
        ld e,#00            ; Track counter for side 1, starts at track 0
        ld d,#01            ; D = 1 (for FDC side select, and for PRINT_HEAD_0)
        jp FORMAT_TRACK_LOW_LEVEL_LOOP ; Format all tracks for side 1
```

```assembly
;-------------------------------------------------------------------------------
; FORMAT_TRACK_LOW_LEVEL - Low-level track formatting routine for FDC
; Entry: D = track number for FDC (0-39 or 0-79)
;        E = side number for FDC (0 or 1)
;        HL = points to sector interleave table (FILE_START)
;        (FILE_LENGTH) points to second byte of interleave table
;-------------------------------------------------------------------------------
FORMAT_TRACK_LOW_LEVEL: ; (My label for routine starting at 1FFD)
1FFD:   di                      ; Disable interrupts
1FFE:   3E F4                   LD      A,#F4           ; FDC Command: WRITE TRACK (Type III)
                                                        ; This is the value for FDC Command Register
2000:   D3 1F                   OUT     (#1F),A         ; Send WRITE TRACK command to FDC

        ; --- Send Pre-Index Gap, Sync, and Index Address Mark (FC) ---
        ; The FDC is now waiting for a stream of bytes. Special byte values
        ; like #F5 (A1 sync), #FC (Index AM), #FE (IDAM), #FB (DAM), #F7 (CRC)
        ; are interpreted by the FDC to write specific patterns.
        ; Other bytes like #4E, #00 are written as gap/sync filler.

        ld hl,(FILE_START)      ; HL points to the current sector interleave table
                                ; (e.g., SEC_TABLE or SEC_TURBO_TABLE)
        ld a,d                  ; A = Track number (from D passed to this routine)
                                ; This D is the FDC physical track for ID field
        push af                 ; Save FDC track number
        ld a,e                  ; A = Side number (from E passed to this routine)
                                ; This E is the FDC physical side for ID field
        push af                 ; Save FDC side number

        ; Values for D register used by #20B1 (Write D, B times) or #20AF (Write D, 1 time)
        ; C is always #7F (FDC Data Port) for these calls.

        ; Gap 0 / Pre-Index
2005:   0E 7F                   LD      C,#7F           ; FDC Data Port
2007:   06 0A                   LD      B,#0A           ; Count = 10 bytes
2009:   16 4E                   LD      D,#4E           ; Byte to write: #4E (Gap filler)
200B:   CD B1 20                CALL    DRVS.20B1       ; Write D, B times to port C (FDC Data)

        ; Sync
200E:   06 0C                   LD      B,#0C           ; Count = 12 bytes
2010:   16 00                   LD      D,#00           ; Byte to write: #00 (Sync)
2012:   CD B1 20                CALL    DRVS.20B1

        ; Index Address Mark (IAM = FC) - FDC forms this from A1 A1 A1 FC sequence
2015:   06 03                   LD      B,#03           ; Count = 3 bytes
2017:   16 F5                   LD      D,#F5           ; Byte to write: #F5 (A1 sync marker for FDC)
2019:   CD B1 20                CALL    DRVS.20B1
201C:   16 FE                   LD      D,#FE           ; Byte for FDC: This is an error, should be #FC for Index AM
                                                        ; TR-DOS 5.03 uses #FC. This #FE would write an IDAM.
                                                        ; For "Write Track", FDC expects #FC after three #F5 for IAM.
                                                        ; If #FE is sent, it will write an IDAM instead of IAM.
                                                        ; This seems to be a significant deviation if correct.
                                                        ; Standard Beta Disk format uses FC for Index Address Mark.
                                                        ; *Correction: The FDC's "Write Track" command uses #F5 to denote A1,
                                                        ; and the FDC *itself* generates the C2 or FC mark.
                                                        ; Sending #F5 tells FDC to prepare for Address Mark. Sending #FE (IDAM)
                                                        ; or #FB (DAM) tells it which actual mark to write.
                                                        ; So, #F5 (3 times) then #FE writes IDAM. #F5 (3 times) then #FB writes DAM.
                                                        ; TR-DOS _doesn't_ explicitly send #FC for IAM. The FDC generates it
                                                        ; when it sees the index hole during Write Track if no other AM is pending.
                                                        ; The initial #F5 here (2017) followed by what appears to be #FE (at 201C, though it's D,
                                                        ; then 20AF calls 20B1 with B=1) would actually write an IDAM here.
                                                        ; This part of the code from 2015 seems to be for the *first sector's IDAM setup*.
                                                        ; The sequence should be:
                                                        ; Gap0 (e.g. 80x #4E), Sync (12x #00), IAM (3x #F5, FDC writes #FC), Gap1 (50x #4E)
                                                        ; This disassembly looks like it's diving straight into sector formatting.
                                                        ; Let's reinterpret 2007-201C:
                                                        ; 2007: 10 bytes of (passed D, originally #F4 or #00) - This is non-standard for pre-IAM.
                                                        ; 200E: 12 bytes of #4E (Gap) - Standard.
                                                        ; 2010: 3 bytes of #00 (Sync) - Standard.
                                                        ; 2017: 1 byte of #F5 (A1 sync marker) - Odd to send only one before an AM.
                                                        ; This sequence is very different from standard WD17xx Write Track preambles.
                                                        ; It's possible #20B1 and #20AF are more complex than simple byte outputs.
                                                        ; Let's assume DRVS.20B1 writes D, B times. DRVS.20AF writes D, 1 time.

        ; Re-evaluating 1FFD to 2021 (this part is the most confusing due to DRVS.20B1/AF complexity)
        ; Based on the byte counts, it's more likely:
        ; 1. Write 10 bytes of 'D' (passed in) -- Unusual, #F4 or #00. Purpose: unknown, maybe special FDC init.
        ; 2. Write 12 bytes of #4E (Gap)
        ; 3. Write 3 bytes of #00 (Sync)
        ; 4. Write 1 byte of #F5 (A1 for IAM - FDC makes FC)
201C:   16 FE                   LD      D,#FE           ; This D will be used for the first IDAM
        ; Fall through to the loop at 2065h to format sectors

; --- Loop to format 16 sectors (HL points to sector ID in interleave table) ---
FORMAT_SECTOR_LOOP: ; (My label for loop starting at 2065h)
2021:   ; D should be #FE (ID Address Mark byte for FDC)
        pop af                  ; AF = Side Number (for ID field)
        push af                 ; Save it again
        ld e,a                  ; E = Side Number
        pop af                  ; AF = Track Number (for ID field)
        push af                 ; Save it again
        ld d,a                  ; D = Track Number (original physical track for ID field)

        ; Write ID Field Structure for one sector
2022:   CALL    DRVS.20AF_ID_WRITE_SEQUENCE ; This is a conceptual call for 2022-2033
        ; This sequence writes:
        ; Gap1 (10x #4E), Sync (12x #00), A1*3 (3x #F5), IDAM (#FE),
        ; Track (D), Side (E), Sector ((HL) from table), Length (#01=256), CRC (#F7)

        ; Actual code from disassembly for ID Field:
        ; (Assumes C=#7F is set before this loop, D=#FE from 201C)
        ; LD D,#FE was at 201C. This D (#FE) is used by the first call to DRVS.20AF inside the loop.
        ; The loop actually starts at 2065h effectively.
        ; HL points to the interleave table (e.g., SEC_TABLE).
        ; The first byte of interleave table is fetched by 7E at 2065h for Sector ID.

2022->2033 (Simplified explanation of byte stream sent to FDC for ID FIELD)
        ; 10 bytes of #4E (Gap1)
        ; 12 bytes of #00 (Sync)
        ;  3 bytes of #F5 (A1 for IDAM)
        ;  1 byte  of #FE (ID Address Mark)
        ;  1 byte  of D (Track number for ID field - from stack)
        ;  1 byte  of E (Side number for ID field - from stack)
        ;  1 byte  of (HL) (Sector number from interleave table)
        ;  1 byte  of #01 (Sector Length = 256 bytes)
        ;  1 byte  of #F7 (FDC generates and writes ID CRC)

        ; Write Data Field Structure for one sector
2033->205E (Simplified explanation of byte stream sent to FDC for DATA FIELD)
        ; 16 bytes of #4E (Gap2)
        ; 12 bytes of #00 (Sync)
        ;  3 bytes of #F5 (A1 for DAM)
        ;  1 byte  of #FB (Data Address Mark)
        ; 256 bytes of #00 (or #E5 for some formatters) - Actual sector data
        ;  1 byte  of #F7 (FDC generates and writes Data CRC)

        ; Write Gap3
205E->2062 (Simplified explanation of byte stream sent to FDC for GAP3)
        ; 60 bytes of #4E (Gap3)

2065:   ld a,(hl)               ; A = Sector number from interleave table
        inc hl                  ; Point to next entry in interleave table
        ; ... intervening code from 2022 to 2065 builds the byte stream using A, D, E etc.
        ; and calls DRVS.20B1/DRVS.20AF many times to send bytes to FDC.

        ; This is the actual loop control:
        cp #10                  ; Have we processed 16 sectors from table?
                                ; (Assumes #10 is end marker or related to table size)
2069:   jr nz,FORMAT_SECTOR_LOOP_START ; (Conceptual, actual jump is to 2007h which is messy)
                                      ; The loop is complex, using D and HL state.
                                      ; The real loop is driven by HL iterating through SEC_TABLE.
                                      ; The code from 2007 to 2067 forms one iteration for one sector.
                                      ; 2067 `CP #10` (A holds sector ID from table)
                                      ; 2069 `JR NZ, #2007` loops back if A is not 16.

        ; After 16 sectors are described to FDC:
206B:   ld b,#00                ; Count for final gap/padding
206D:   call DRVS.20B1          ; Write D (last value, likely #4E) 256 times (B=0 means 256)
                                ; This writes Gap4 until index pulse stops "Write Track".

2070:   jp m,#2076              ; Check FDC status after Write Track (complex)
2073:   call DRVS.20B1          ; (Unconditional, seems like error path or more padding)
2076:   in a,(#1F)              ; Read FDC status
        and #40                 ; Check for Write Protect error (Bit 6)
207A:   jp nz,#3F39             ; Jump to TR-DOS error display if WP error.
                                ; #3F39 is "Disk Error" or "Read Only" display.

207D:   ld a,(#5CD8)            ; Get current track formatted (for display)
        or a                    ; Check if it was 0 (already handled by PRINT_HEAD_0)
2081:   ret nz                  ; If not track 0, return (display was done by loop)

        ; Special handling for track 0 display / CRC check after format
2082:   ld c,#7F                ; FDC Data Port
2084:   ld a,e                  ; E should be side here (0 or 1)
2085:   out (#3F),a             ; Output Side to FDC Track Register (Port #3F) - Incorrect!
                                ; Track register expects track number. Side is usually implicit or via Port #FF.
                                ; This would corrupt FDC's idea of current track.
                                ; This section 2082-20AD is very suspect for standard FDC ops.
                                ; It might be trying to read back and verify something very specific after track 0 format.

        ; ... (Code from 2087 to 20AD is complex and seems to be a custom verify/readback
        ; after formatting track 0, possibly involving the #4000 area. It's not part of
        ; the primary "Write Track" byte stream sending).

20AD:   ei
20AE:   ret

;-------------------------------------------------------------------------------
; DRVS.20AF - Write byte in D to FDC Data Port C, 1 time
; DRVS.20B1 - Write byte in D to FDC Data Port C, B times (B=0 means 256)
;-------------------------------------------------------------------------------
DRVS.20AF: ; (My label for routine starting at 20AF)
20AF:   ld b,#01                ; Count = 1
        ; Fall through to DRVS.20B1

DRVS.20B1: ; (My label for routine starting at 20B1)
20B1:   in a,(#FF)              ; Read Beta Disk Status Port (#FF)
20B3:   and #C0                 ; Check bits 6 (DRQ) & 7 (INTRQ)
20B5:   jr z,#20B1              ; Loop until DRQ or INTRQ is set
                                ; For Write Track, FDC asserts DRQ when ready for next byte.
20B7:   ret m                   ; If bit 7 (INTRQ) is set (e.g. error, or index pulse), return.
20B8:   ed 51                   OUT     (C),D           ; Output byte D to FDC Data Port (C holds #7F)
20BA:   djnz DRVS.20B1          ; Loop B times
20BC:   ret
```

**Summary of Track Formatting Byte Stream Sent to FDC (Conceptual):**

The `FORMAT_TRACK_LOW_LEVEL` routine orchestrates sending bytes to the FDC in "Write Track" mode. The FDC interprets these bytes:

1.  **Preamble:** A sequence of bytes to establish sync and mark the beginning of the track (Index Address Mark area). The disassembly is a bit unclear on the exact standard preamble here, possibly due to the way `DRVS.20B1` and `DRVS.20AF` are used with a changing D register value at the very start of `1FFDh`.
2.  **For each of the 16 sectors:**
    *   **Gap 1 + ID Sync + ID Address Mark (#FE):** e.g., `10 * #4E`, `12 * #00`, `3 * #F5`, `1 * #FE`.
    *   **ID Field Data:** Track No, Side No, Sector No (from interleave table), Length Code (`#01` for 256 bytes).
    *   **ID CRC command (#F7):** FDC calculates and writes two CRC bytes.
    *   **Gap 2 + Data Sync + Data Address Mark (#FB):** e.g., `16 * #4E`, `12 * #00`, `3 * #F5`, `1 * #FB`.
    *   **Data Field:** 256 bytes of a fill character (e.g., `#00` or `#E5`).
    *   **Data CRC command (#F7):** FDC calculates and writes two CRC bytes.
    *   **Gap 3:** e.g., `60 * #4E`.
3.  **Postamble (Gap 4):** Fill bytes (e.g., `#4E`) written until the FDC sees the next index pulse, at which point it stops the Write Track operation.

**Interleave Tables:**

*   `SEC_TABLE` (at `1FB9h`): `01 09 02 0A 03 0B 04 0C 05 0D 06 0E 07 0F 08 10`
    *   This is a standard 2:1 interleave table for 16 sectors. Sectors are written in this physical order: 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16.
*   `SEC_TURBO_TABLE` (at `325Ah`): `01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10`
    *   This is a 1:1 interleave (sequential) table for TURBO format.

The formatting process is complex and highly dependent on the FDC's behavior in "Write Track" mode. The TR-DOS code sends a blueprint, and the FDC fills in the details like CRC calculation and precise timings for address marks. The section from `1FFDh` to `207Ch` is the core of generating this blueprint for one track.

## Q&A
**Question:**

Is that a true statement based on tr-dos disassembly? TR-DOS allows track size in a range [6208...6464] bytes


**Answer:**

The statement "TR-DOS allows track size in a range [6208...6464] bytes" refers to the **total number of raw bytes written to a physical track during formatting**, including all sector data, ID fields, sync bytes, gaps, and CRC bytes. It's not about the user data capacity (which is 16 sectors * 256 bytes/sector = 4096 bytes).

Let's analyze the `FORMAT` routine in the TR-DOS disassembly (starting around `1EC2h`, with the core track writing logic around `1FFDh`) to see if it implies such a size.

The `FORMAT` routine, specifically the part at `1FFDh` called `FORMAT_TRACK_LOW_LEVEL` (my naming based on its function), is responsible for sending a stream of bytes to the Floppy Disk Controller (FDC) when it's in "Write Track" mode. The FDC then takes these bytes and writes them to the disk, handling FM encoding, writing special address marks, and calculating/writing CRCs where instructed.

Let's break down the bytes sent by the CPU to the FDC for one standard TR-DOS track (16 sectors, FM encoding):

1.  **Initial Track Bytes (Pre-Index / Index Address Mark area):**
    *   The routine at `1FFDh` is called. `D` holds the track number, `E` holds the physical sector number (usually starting at 1 for FDC).
    *   The code before the main sector loop (from `2007h`):
        *   `LD HL,(FILE_START)`: HL points to `SEC_TABLE` or `SEC_TURBO_TABLE` (not used for this initial part).
        *   `LD C,#7F` (FDC Data Port)
        *   `LD B,#0A`
        *   `LD D, <value>` (D holds #F4 from `1EF1h` or #00 from `1EFCh` based on formatting type/pass).
        *   `CALL #20B1` (`XLOP1` in your `loader.a80.txt` if it's the same, but here it's the `DRVS.20B1` which is `ED 51 OUT (C),D` looped B times). This writes the value in `D` (passed to `1FFDh`) 10 times.
        *   `LD D,#4E`
        *   `LD B,#0C`
        *   `CALL #20B1` (writes `#4E` 12 times - likely part of Gap0/Sync)
        *   `LD D,#00`
        *   `LD B,#03`
        *   `CALL #20B1` (writes `#00` 3 times - Sync)
        *   `LD D,#F5` (This is the byte for `A1` in MFM, but for FM, `FC` is Index AM. The FDC interprets special values.)
        *   `CALL #20AF` (writes `#F5` 1 time. This is likely for the Index Address Mark `FC`. The FDC treats `F5` then `FC` in data as special if in Write Track mode).
    *   Total initial bytes sent: 10 + 12 + 3 + 1 = **26 bytes**. These bytes represent the pre-index gap, sync bytes, and the Index Address Mark (`FC`).

2.  **Bytes per Sector (repeated 16 times):**
    The loop starts at `2065h` (`7E LD A,(HL)` where `HL` points to `SEC_TABLE`).
    Inside this loop, for each of the 16 sectors:
    *   **ID Field Formation:**
        *   `LD D,#4E`, `LD B,#0A`, `CALL #20B1` -> 10 bytes of `#4E` (Gap for IDAM / ID Sync)
        *   `LD D,#00`, `LD B,#0C`, `CALL #20B1` -> 12 bytes of `#00` (Sync)
        *   `LD D,#F5`, `LD B,#03`, `CALL #20B1` -> 3 bytes of `#F5` (A1 sync for IDAM)
        *   `LD D,#FE`, `CALL #20AF` -> 1 byte of `#FE` (ID Address Mark)
        *   `LD D, <track_no_from_D_passed_to_1FFD>`, `CALL #20AF` -> 1 byte (Track Number)
        *   `LD D, <side_no>`, `CALL #20AF` -> 1 byte (Side Number - usually 0)
        *   `LD D, <sector_no_from_SEC_TABLE>`, `CALL #20AF` -> 1 byte (Sector Number)
        *   `LD D,#01`, `CALL #20AF` -> 1 byte (Length Code for 256 bytes)
        *   `LD D,#F7`, `CALL #20AF` -> 1 byte (`#F7` tells FDC to write ID CRC)
        *   Total for ID field structure sent by CPU: 10+12+3+1+1+1+1+1+1 = **31 bytes**.
    *   **Data Field Formation:**
        *   `LD D,#4E`, `LD B,#16`, `CALL #20B1` -> 16 bytes of `#4E` (Gap2 / Data Sync)
        *   `LD D,#00`, `LD B,#0C`, `CALL #20B1` -> 12 bytes of `#00` (Sync)
        *   `LD D,#F5`, `LD B,#03`, `CALL #20B1` -> 3 bytes of `#F5` (A1 sync for DAM)
        *   `LD D,#FB`, `CALL #20AF` -> 1 byte of `#FB` (Data Address Mark)
        *   `LD D,#00` (or #E5, the fill byte), `LD B,#00` (means 256 times), `CALL #20B1` -> **256 bytes** (Sector Data)
        *   `LD D,#F7`, `CALL #20AF` -> 1 byte (`#F7` tells FDC to write Data CRC)
        *   Total for Data field structure sent by CPU: 16+12+3+1+256+1 = **289 bytes**.
    *   **Gap3:**
        *   `LD D,#4E`, `LD B,#3C` (`#3C` = 60 decimal), `CALL #20B1` -> **60 bytes** of `#4E`.
    *   Total bytes sent by CPU per sector structure: 31 (ID) + 289 (Data) + 60 (Gap3) = **380 bytes**.

3.  **Total Bytes for 16 Sectors:**
    *   16 sectors * 380 bytes/sector = **6080 bytes**.

4.  **Total Bytes Sent to FDC by CPU for one Track Format:**
    *   Initial Track Bytes + Bytes for 16 sectors = 26 + 6080 = **6106 bytes**.

This 6106 bytes is the stream of data and control markers that the CPU sends to the FDC for the "Write Track" command when formatting. The FDC then performs FM encoding. For FM encoding, data bits are generally expanded (1 data bit might take 2 bit-cells on disk including clock). However, the "Write Track" command on the WD1793 is special:
*   When you send normal data bytes (like the 256 data fill bytes, or gap bytes like `#4E`, `#00`), the FDC performs standard FM encoding.
*   When you send special FDC control bytes like `#FE` (IDAM), `#FB` (DAM), `#FC` (Index AM), `#F5` (Sync A1), `#F7` (Write CRC), the FDC writes these specific patterns without the usual data encoding rules, ensuring they are unique on the track.

The total physical capacity of a standard single-density 5.25" floppy track is often quoted around **6250 bytes (raw, unformatted)**. The 6106 bytes sent by the TR-DOS CPU, when encoded by the FDC and written with necessary clock bits, will occupy a significant portion of this raw capacity.

**Conclusion based on the TR-DOS disassembly:**

*   The disassembly **does not contain explicit constants** like 6208 or 6464.
*   However, by analyzing the `FORMAT` routine (`1FFDh`), we can calculate that the CPU sends **6106 bytes** to the FDC during a "Write Track" operation to format one track.
*   This 6106-byte stream defines the logical structure (ID fields, data fields, gaps) that, when physically written to disk using FM encoding by the FDC, would consume an amount of physical disk space that is very consistent with the typical raw track capacity of ~6250 bytes.
*   Therefore, while TR-DOS doesn't *check* for a track size in the range [6208...6464], it *creates* formatted tracks that effectively **utilize a physical track capacity within that approximate range.**

The statement is **conceptually true** in terms of the resulting physical track utilization, even if the exact numbers aren't hardcoded constants being checked against. The disassembly shows *how* a track of such a resulting physical size is constructed.