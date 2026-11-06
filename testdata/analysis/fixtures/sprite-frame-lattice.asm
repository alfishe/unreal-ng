; AnalysisDetectorSprites, a frame lattice: four masked sprite frames of three 8 x 8 strips, stored in columns (a
; strip's 8 lines top to bottom, a mask byte before each graphic byte: 16 bytes a strip, 48 a frame, back to back),
; named by a table of words 48 bytes apart. Six objects stand in windows that clip whole strips at their edges, so
; the frames are drawn in varying column groups: frames 0 and 2 whole, frame 1 as its first strip alone and as its
; last two, frame 3 as its first two strips and as its last one alone. That last strip is $FF,$00 on every line:
; fully transparent. The column groups are no frames: one sprite from frame 0 to the end of frame 3, four frames of
; 24 x 8, 48 bytes apart, the transparent strip included.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,draws
        ld b,6
object:
        push bc
        ld a,(ix+0)             ; the frame
        add a,a
        ld e,a
        ld d,0
        ld hl,frame_table
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE: the frame's first strip
        ld l,(ix+3)             ; the screen column of the first strip shown
        ld h,$40
        ld a,(ix+1)             ; strips clipped at the window's left edge
        or a
        jr z,shown
        ld b,a
clipped:
        ld c,16                 ; a strip's bytes, not drawn
skip:
        inc de
        dec c
        jr nz,skip
        djnz clipped
shown:
        ld b,(ix+2)             ; strips shown (the rest clipped at the right edge)
strip:
        push bc
        push hl
        call draw
        pop hl
        pop bc
        inc l
        djnz strip
        ld de,4
        add ix,de
        pop bc
        djnz object
        jr main
draw:
        ld b,8
row:
        ld a,(de)
        and (hl)
        ld c,a
        inc de
        ld a,(de)
        or c
        ld (hl),a
        inc de
        inc h
        djnz row
        ret
; The objects: frame, strips clipped on the left, strips shown, screen column (a free column between two objects)
draws:  db 0,0,3,0
        db 1,0,1,4
        db 1,1,2,6
        db 2,0,3,9
        db 3,0,2,13
        db 3,2,1,16
frame_table:
        dw frame0,frame1,frame2,frame3
frame0:
; frame 0, strip 0
        db $C3,$18,$1C,$41
        db $01,$D4,$00,$96
        db $01,$DC,$04,$B1
        db $01,$5C,$20,$8F
; frame 0, strip 1
        db $1F,$40,$01,$BC
        db $00,$EE,$00,$9D
        db $C3,$18,$20,$8A
        db $FF,$00,$81,$2C
; frame 0, strip 2
        db $10,$47,$03,$58
        db $C0,$12,$00,$52
        db $84,$31,$00,$4A
        db $20,$8A,$00,$73
frame1:
; frame 1, strip 0
        db $00,$F5,$00,$F6
        db $00,$4F,$00,$B7
        db $00,$6A,$01,$AC
        db $80,$27,$80,$2D
; frame 1, strip 1
        db $00,$DB,$30,$87
        db $04,$D1,$01,$BC
        db $38,$83,$80,$35
        db $08,$E2,$00,$CE
; frame 1, strip 2
        db $10,$C7,$30,$85
        db $0F,$60,$00,$BA
        db $80,$35,$00,$B5
        db $8F,$20,$00,$F5
frame2:
; frame 2, strip 0
        db $1C,$41,$00,$79
        db $00,$DF,$00,$F9
        db $00,$B9,$00,$CB
        db $88,$23,$04,$F1
; frame 2, strip 1
        db $E0,$0D,$00,$76
        db $00,$CF,$80,$29
        db $1C,$41,$0F,$60
        db $20,$89,$83,$38
; frame 2, strip 2
        db $00,$AE,$00,$9A
        db $01,$CC,$00,$EA
        db $01,$7C,$80,$29
        db $01,$CC,$07,$F0
frame3:
; frame 3, strip 0
        db $00,$DF,$00,$92
        db $E0,$0D,$10,$47
        db $C1,$14,$80,$27
        db $18,$C2,$03,$68
; frame 3, strip 1
        db $C0,$1A,$C1,$14
        db $00,$E6,$81,$34
        db $84,$31,$83,$28
        db $C1,$1C,$00,$9E
; frame 3, strip 2: fully transparent (mask $FF, graphic $00 on every line)
blank:
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
frames_end:
