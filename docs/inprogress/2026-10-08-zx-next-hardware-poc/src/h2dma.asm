; H2: zxnDMA transfer time at the four CPU speeds.
; A 4096-byte memory-to-memory copy is started right after a frame wrap; the raster line is read when the DMA
; has finished. The same is done with a 1-byte copy to measure the cost of loading the DMA program; the
; difference is printed. Variants: continuous mode with port timing 4 / 3 / 2 cycles, and burst mode (4 cycles).
;
; Screen: row 0 = id, row 1 = core version / board / machine type, row 2 = lines per frame,
;         rows 4-7 = speed 0..3, columns 2 / 7 / 12 / 17 = continuous 4, 3, 2 cycles, burst 4 (lines, hex).
    org 0x8000
Start:
    di
    ld sp,0xBFF0
    call InitScreen
    ld a,0x02
    call PrintHeader
    call MeasureMaxLine
    ld hl,(LineCount)
    ld b,2
    ld c,0
    call PrintAt16

    ld a,0x07
    call ReadNextReg
    ld (SavedNr07),a
    ; source #A000 (filled), destination #C000 (MMU6 = 8K page 32,33)
    ld a,0x20
    nextreg 0x56,a
    ld a,0x21
    nextreg 0x57,a
    ld hl,0xA000
    ld de,0xA001
    ld bc,0x0FFF
    ld (hl),0x5A
    ldir

    ld d,0                      ; speed
SpeedLoop:
    ld e,0                      ; variant 0..3
VariantLoop:
    push de
    ld a,d
    nextreg 0x07,a
    ld a,e
    cp 3
    jr z,VarBurst
    ld (DmaTiming),a            ; 0 = 4 cycles, 1 = 3, 2 = 2
    ld a,0xAD                   ; WR4 continuous
    jr VarGo
VarBurst:
    xor a
    ld (DmaTiming),a
    ld a,0xCD                   ; WR4 burst
VarGo:
    ld (DmaMode),a
    ld hl,0x1000
    ld (DmaLen),hl
    call TimeDma                ; HL = lines
    push hl
    ld hl,1
    ld (DmaLen),hl
    call TimeDma
    pop de                      ; DE = lines with 4096 bytes, HL = overhead lines
    ex de,hl
    or a
    sbc hl,de                   ; HL = 4096 bytes - 1 byte
    pop de
    push de
    ld b,d
    ld a,b
    add a,4
    ld b,a                      ; row = 4 + speed
    ld a,e
    ld c,a
    add a,a
    add a,a
    add a,c
    add a,2
    ld c,a                      ; column = 2 + 5 * variant
    call PrintAt16
    pop de
    inc e
    ld a,e
    cp 4
    jr c,VariantLoop
    inc d
    ld a,d
    cp 4
    jr c,SpeedLoop

    ld a,(SavedNr07)
    nextreg 0x07,a
    jp Done

; ---------------------------------------------------------------------------
; TimeDma: runs the DMA program once and returns HL = raster lines elapsed (modulo the lines per frame)
TimeDma:
    ld a,(DmaLen)
    ld (DmaProgLen),a
    ld a,(DmaLen+1)
    ld (DmaProgLen+1),a
    ld a,(DmaTiming)
    ld (DmaProgTimeA),a
    ld (DmaProgTimeB),a
    ld a,(DmaMode)
    ld (DmaProgMode),a
    call WaitFrameStart
    call ReadRaster
    ld (StartLine),hl
    ld hl,DmaProg
    ld b,DmaProgEnd-DmaProg
    ld c,0x6B
    otir
    call ReadRaster             ; after the (blocking) transfer
    ld de,(StartLine)
    or a
    sbc hl,de
    jr nc,TDok
    ld de,(LineCount)
    add hl,de
TDok:
    ret

StartLine:  defw 0
SavedNr07:  defb 0
DmaTiming:  defb 0
DmaMode:    defb 0xAD
DmaLen:     defw 0x1000

; DMA program (zxnDMA through port #6B): reset, WR0 A->B transfer, WR1 / WR2 with timing, WR4, WR5, load, enable
DmaProg:
    defb 0xC3                       ; WR6 reset
    defb 0x7D                       ; WR0: A -> B, address and length follow
    defw 0xA000                     ; port A start
DmaProgLen:
    defw 0x1000                     ; block length
    defb 0x54                       ; WR1: port A memory, increment, timing byte follows
DmaProgTimeA:
    defb 0x00
    defb 0x50                       ; WR2: port B memory, increment, timing byte follows
DmaProgTimeB:
    defb 0x00
DmaProgMode:
    defb 0xAD                       ; WR4: 0xAD continuous, 0xCD burst; port B address follows
    defw 0xC000
    defb 0x82                       ; WR5: stop at end of block
    defb 0xCF                       ; WR6 load
    defb 0x87                       ; WR6 enable DMA
DmaProgEnd:

    include "common.inc"
