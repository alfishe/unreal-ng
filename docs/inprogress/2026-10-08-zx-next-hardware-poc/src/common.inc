; Shared routines of the hardware probe programs (ZX Spectrum Next, Z80N).
; Written for z88dk-z80asm (-mz80n). Everything is plain: no ROM, no OS calls, own font.
; Screen: ULA at #4000, 32 x 24 character cells; numbers are printed as hexadecimal digits.

NEXTREG_SELECT  equ 0x243B
NEXTREG_DATA    equ 0x253B

; ---------------------------------------------------------------------------
; InitScreen: border black, ULA bitmap cleared, attributes white on black
InitScreen:
    xor a
    out (0xFE),a
    ld hl,0x4000
    ld de,0x4001
    ld bc,0x17FF
    ld (hl),0
    ldir
    ld hl,0x5800
    ld de,0x5801
    ld bc,0x02FF
    ld (hl),0x07
    ldir
    ret

; ---------------------------------------------------------------------------
; ReadNextReg: A = register -> A = value
ReadNextReg:
    push bc
    ld bc,NEXTREG_SELECT
    out (c),a
    inc b
    in a,(c)
    pop bc
    ret

; ---------------------------------------------------------------------------
; ReadRaster: HL = current raster line (NextREG #1E bit 0 = MSB, #1F = LSB), re-read when the MSB changed
ReadRaster:
    push bc
    push af
RRagain:
    ld bc,NEXTREG_SELECT
    ld a,0x1E
    out (c),a
    inc b
    in a,(c)
    and 1
    ld h,a
    dec b
    ld a,0x1F
    out (c),a
    inc b
    in l,(c)
    ld a,0x1E
    dec b
    out (c),a
    inc b
    in a,(c)
    and 1
    cp h
    jr nz,RRagain
    pop af
    pop bc
    ret

; ---------------------------------------------------------------------------
; WaitFrameStart: returns right after the raster counter wrapped (new line < previous line)
; uses (PrevLine) so that a counting loop can continue with it
WaitFrameStart:
    call ReadRaster
    ld (PrevLine),hl
WFSloop:
    call ReadRaster
    ld de,(PrevLine)
    ld (PrevLine),hl
    or a
    sbc hl,de
    jr nc,WFSloop
    ret

; ---------------------------------------------------------------------------
; MeasureMaxLine: observes one whole frame; (MaxLine) = highest raster line seen, (LineCount) = MaxLine + 1
MeasureMaxLine:
    call WaitFrameStart
    ld hl,0
    ld (MaxLine),hl
MMLloop:
    call ReadRaster
    ld de,(MaxLine)
    push hl
    or a
    sbc hl,de
    pop hl
    jr c,MMLnotmax
    ld (MaxLine),hl
MMLnotmax:
    ld de,(PrevLine)
    ld (PrevLine),hl
    or a
    sbc hl,de
    jr nc,MMLloop
    ld hl,(MaxLine)
    inc hl
    ld (LineCount),hl
    ret

; ---------------------------------------------------------------------------
; ScreenAddr: D = row (0..23), E = column (0..31) -> DE = address of the top pixel line of the cell
ScreenAddr:
    ld a,d
    and 0x18
    or 0x40
    ld h,a
    ld a,d
    and 7
    rrca
    rrca
    rrca
    or e
    ld l,a
    ex de,hl
    ret

; PrintNibble: A = 0..15, DE = screen address -> draws the digit, DE advances one column
PrintNibble:
    push hl
    push bc
    and 0x0F
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    ld bc,Font
    add hl,bc
    push de
    ld b,8
PNrow:
    ld a,(hl)
    ld (de),a
    inc hl
    inc d
    djnz PNrow
    pop de
    inc e
    pop bc
    pop hl
    ret

; PrintHex16: HL = value, DE = screen address (4 columns)
PrintHex16:
    ld a,h
    rrca
    rrca
    rrca
    rrca
    call PrintNibble
    ld a,h
    call PrintNibble
    ld a,l
    rrca
    rrca
    rrca
    rrca
    call PrintNibble
    ld a,l
    call PrintNibble
    ret

; PrintHex8: A = value, DE = screen address (2 columns)
PrintHex8:
    push af
    rrca
    rrca
    rrca
    rrca
    call PrintNibble
    pop af
    call PrintNibble
    ret

; PrintAt16: HL = value, B = row, C = column
PrintAt16:
    push de
    ld d,b
    ld e,c
    push hl
    call ScreenAddr
    pop hl
    call PrintHex16
    pop de
    ret

; PrintAt8: A = value, B = row, C = column
PrintAt8:
    push de
    push af
    ld d,b
    ld e,c
    call ScreenAddr
    pop af
    call PrintHex8
    pop de
    ret

; PrintHeader: row 0 = program id (A), row 1 = core version NR #01 / #0E / #0F and machine type NR #03
PrintHeader:
    ld b,0
    ld c,0
    call PrintAt8
    ld a,0x01
    call ReadNextReg
    ld b,1
    ld c,0
    call PrintAt8
    ld a,0x0E
    call ReadNextReg
    ld b,1
    ld c,3
    call PrintAt8
    ld a,0x0F
    call ReadNextReg
    ld b,1
    ld c,6
    call PrintAt8
    ld a,0x03
    call ReadNextReg
    ld b,1
    ld c,9
    call PrintAt8
    ret

; Done: shows D0DE in the last row and stops
Done:
    ld hl,0xD0DE
    ld b,23
    ld c,0
    call PrintAt16
    halt
    jr Done

PrevLine:   defw 0
MaxLine:    defw 0
LineCount:  defw 312

; 16 hexadecimal digits, 8 bytes each
Font:
    defb 0x00,0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00   ; 0
    defb 0x00,0x18,0x28,0x08,0x08,0x08,0x3E,0x00   ; 1
    defb 0x00,0x3C,0x42,0x02,0x3C,0x40,0x7E,0x00   ; 2
    defb 0x00,0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00   ; 3
    defb 0x00,0x08,0x18,0x28,0x48,0x7E,0x08,0x00   ; 4
    defb 0x00,0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00   ; 5
    defb 0x00,0x3C,0x40,0x7C,0x42,0x42,0x3C,0x00   ; 6
    defb 0x00,0x7E,0x02,0x04,0x08,0x10,0x10,0x00   ; 7
    defb 0x00,0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00   ; 8
    defb 0x00,0x3C,0x42,0x42,0x3E,0x02,0x3C,0x00   ; 9
    defb 0x00,0x3C,0x42,0x42,0x7E,0x42,0x42,0x00   ; A
    defb 0x00,0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00   ; B
    defb 0x00,0x3C,0x42,0x40,0x40,0x42,0x3C,0x00   ; C
    defb 0x00,0x78,0x44,0x42,0x42,0x44,0x78,0x00   ; D
    defb 0x00,0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00   ; E
    defb 0x00,0x7E,0x40,0x7C,0x40,0x40,0x40,0x00   ; F
