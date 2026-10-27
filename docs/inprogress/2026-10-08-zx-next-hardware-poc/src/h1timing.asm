; H1: instruction throughput at the four CPU speeds, and contention.
; For every "body" (64 copies of one instruction) the program counts how many times the body can run in
; 8 video frames (the printed number is the sum over 8 frames), at 3.5 / 7 / 14 / 28 MHz. From the counts the cost of a memory access at each speed
; (SRAM wait state at 28 MHz, contention at 3.5 MHz) follows (see experiments.md, H1).
;
; Screen: row 0 = id, row 1 = core version / board / machine type, row 2 = lines per frame,
;         rows 4-9 = bodies 0-5, columns 2/7/12/17 = speed 0..3 (blocks counted in 8 frames, hex),
;         column 22 = speed 0 with contention disabled (NR #08 bit 6).
    org 0x8000
Start:
    di
    ld sp,0xBFF0
    call InitScreen
    ld a,0x01
    call PrintHeader
    call MeasureMaxLine
    ld hl,(LineCount)
    ld b,2
    ld c,0
    call PrintAt16

    ld a,0x07
    call ReadNextReg
    ld (SavedNr07),a
    ld a,0x08
    call ReadNextReg
    ld (SavedNr08),a

    ld iy,Bodies
    ld b,0                      ; body index
BodyLoop:
    push bc
    ; ---- speeds 0..3 with contention as the machine has it
    ld e,0
SpeedLoop:
    push de
    ld a,(SavedNr08)
    and 0xBF
    nextreg 0x08,a              ; contention enabled
    pop de
    push de
    ld a,e
    nextreg 0x07,a              ; speed
    call RunBody                ; HL = blocks per frame
    pop de
    pop bc
    push bc
    push de
    ld a,b
    add a,4
    ld b,a                      ; row = 4 + body
    ld a,e
    ld c,a
    add a,a
    add a,a
    add a,c                     ; 5 * speed
    add a,2
    ld c,a                      ; column
    call PrintAt16
    pop de
    inc e
    ld a,e
    cp 4
    jr c,SpeedLoop
    ; ---- speed 0, contention disabled
    ld a,(SavedNr08)
    or 0x40
    nextreg 0x08,a
    xor a
    nextreg 0x07,a
    call RunBody
    pop bc
    push bc
    ld a,b
    add a,4
    ld b,a
    ld c,22
    call PrintAt16
    ld a,(SavedNr08)
    nextreg 0x08,a
    pop bc
    ld de,6
    add iy,de
    inc b
    ld a,b
    cp 6
    jp c,BodyLoop

    ld a,(SavedNr07)
    nextreg 0x07,a
    ld a,(SavedNr08)
    nextreg 0x08,a
    jp Done

; ---------------------------------------------------------------------------
; RunBody: IY -> entry (dw routine, dw hl value, db mmu6 page, db unused); returns HL = blocks counted in one frame
RunBody:
    ld l,(iy+0)
    ld h,(iy+1)
    ld (CallSite+1),hl
    ld l,(iy+2)
    ld h,(iy+3)
    ld (BodyHL),hl
    ld a,(iy+4)
    nextreg 0x56,a              ; #C000-#DFFF = this 8K page
    inc a
    nextreg 0x57,a
    ld hl,0
    ld (Count),hl
    ld a,FRAMES
    ld (FramesLeft),a
    call WaitFrameStart
BlockLoop:
    ld hl,(BodyHL)
CallSite:
    call 0x0000
    ld hl,(Count)
    inc hl
    ld (Count),hl
    call ReadRaster
    ld de,(PrevLine)
    ld (PrevLine),hl
    or a
    sbc hl,de
    jr nc,BlockLoop             ; no wrap: the next block
    ld a,(FramesLeft)
    dec a
    ld (FramesLeft),a
    jr nz,BlockLoop             ; a frame ended, more to count
    ld hl,(Count)
    ret

FRAMES    equ 8                 ; blocks are counted over 8 frames (the printed number is the sum)
FramesLeft: defb 0

BodyHL:   defw 0
Count:    defw 0
SavedNr07: defb 0
SavedNr08: defb 0

; ---------------------------------------------------------------------------
; the bodies: 64 copies, then RET
BodyNop:
    rept 64
    nop
    endr
    ret
BodyRead:
    rept 64
    ld a,(hl)
    endr
    ret
BodyWrite:
    rept 64
    ld (hl),a
    endr
    ret

; entry: routine, HL for the body, MMU6 page (8K page number; #C000-#FFFF shows pages n, n+1), pad
Bodies:
    defw BodyNop,   0x0000, 0x0020      ; 0: NOPs (baseline)
    defw BodyRead,  0x4000              ; 1: read bank 5 (#4000, the contended bank)
    defb 0x20,0
    defw BodyRead,  0x8800              ; 2: read bank 2 (#8800)
    defb 0x20,0
    defw BodyRead,  0xC000              ; 3: read bank 1 (8K pages 2,3: odd bank, contended in 128K timing)
    defb 0x02,0
    defw BodyRead,  0xC000              ; 4: read extra RAM (8K pages 32,33)
    defb 0x20,0
    defw BodyWrite, 0xC000              ; 5: write extra RAM
    defb 0x20,0

    include "common.inc"
