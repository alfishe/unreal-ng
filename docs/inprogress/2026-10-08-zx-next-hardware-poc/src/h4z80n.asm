; H4: LDIRSCALE (ED B6), LDIRX (ED B4) and LDPIRX (ED B7) on the real CPU.
; The FPGA microcode decodes LDIRSCALE but its register effects are commented out in the VHDL (it should
; behave like LDIRX). Source: 16 bytes 00..0F at #A000, destination #A100 (zero), A = 05 (the byte that
; LDIRX does not copy), BC = 16, HL = #A000, DE = #A100; the alternate set holds BC' = DE' = #0100.
;
; Screen (hex): row 0 = id, row 1 = core version / board / machine type; then three groups of rows
;   rows  4-7   LDIRSCALE : row 4 = HL DE BC, row 5 = AF, rows 6-7 = the 16 destination bytes
;   rows  9-12  LDIRX     : same layout
;   rows 14-17  LDPIRX    : same layout
    org 0x8000
Start:
    di
    ld sp,0xBFF0
    call InitScreen
    ld a,0x04
    call PrintHeader

    ld ix,ScaleResult
    ld a,0
    call RunOne
    ld ix,LdirxResult
    ld a,1
    call RunOne
    ld ix,PirxResult
    ld a,2
    call RunOne

    ld iy,ScaleResult
    ld b,4
    call ShowGroup
    ld iy,LdirxResult
    ld b,9
    call ShowGroup
    ld iy,PirxResult
    ld b,14
    call ShowGroup
    jp Done

; RunOne: A = 0 LDIRSCALE, 1 LDIRX, 2 LDPIRX; IX = result record (HL, DE, BC, AF, 16 bytes)
RunOne:
    ld (Which),a
    ld hl,0xA000
    ld de,0xA001
    ld bc,0x00FF
    ld (hl),0
    ldir                        ; clear #A000.. area start
    ld hl,0xA100
    ld de,0xA101
    ld bc,0x00FF
    ld (hl),0
    ldir
    ld hl,0xA000
    ld b,16
    xor a
FillSrc:
    ld (hl),a
    inc hl
    inc a
    djnz FillSrc
    exx
    ld bc,0x0100
    ld de,0x0100
    exx
    ld hl,0xA000
    ld de,0xA100
    ld bc,16
    ld a,5
    ld c,16
    ld b,0
    ld a,(Which)
    or a
    ld a,5
    jr z,DoScale
    ld a,(Which)
    cp 1
    ld a,5
    jr z,DoLdirx
    defb 0xED,0xB7              ; LDPIRX
    jr Done1
DoLdirx:
    defb 0xED,0xB4              ; LDIRX
    jr Done1
DoScale:
    defb 0xED,0xB6              ; LDIRSCALE
Done1:
    ld (ix+0),l
    ld (ix+1),h
    ld (ix+2),e
    ld (ix+3),d
    ld (ix+4),c
    ld (ix+5),b
    push af
    pop bc
    ld (ix+6),c                 ; F
    ld (ix+7),b                 ; A
    push ix
    pop hl
    ld de,8
    add hl,de
    ex de,hl
    ld hl,0xA100
    ld bc,16
    ldir
    ret

; ShowGroup: IY = record, B = first row
ShowGroup:
    ld l,(iy+0)
    ld h,(iy+1)
    push bc
    ld c,0
    call PrintAt16
    pop bc
    push bc
    ld l,(iy+2)
    ld h,(iy+3)
    ld c,5
    call PrintAt16
    pop bc
    push bc
    ld l,(iy+4)
    ld h,(iy+5)
    ld c,10
    call PrintAt16
    pop bc
    push bc
    inc b
    ld l,(iy+6)
    ld h,(iy+7)
    ld c,0
    call PrintAt16              ; F then A (two bytes, little endian shown as A F)
    pop bc
    inc b
    inc b
    ld c,0
    ld d,8                      ; 16 bytes as 8 words, eight per row group
SGloop:
    ld l,(iy+8)
    ld h,(iy+9)
    push bc
    call PrintAt16
    pop bc
    inc iy
    inc iy
    ld a,c
    add a,5
    ld c,a
    cp 20
    jr c,SGnext
    inc b
    ld c,0
SGnext:
    dec d
    jr nz,SGloop
    ret

Which:         defb 0
ScaleResult:   defs 24
LdirxResult:   defs 24
PirxResult:    defs 24

    include "common.inc"
