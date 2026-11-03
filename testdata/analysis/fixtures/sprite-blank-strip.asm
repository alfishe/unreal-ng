; AnalysisDetectorSprites, a blank strip: four masked sprite frames of three 8 x 16 strips each (a mask byte before
; each graphic byte, 32 bytes a strip, 96 a frame, back to back). A table of words names every strip; three objects
; each show a frame, each strip drawn through its own word, the strips at every other byte column. The last strip of
; the last frame is $FF,$00 on every line: fully transparent, no picture of its own, but drawn by the same blit with
; the same shape right after the strip before it, so it is one of that sprite's frames.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(tick)
        inc a
        ld (tick),a
        ld ix,objects
        ld b,3
object:
        push bc
        ld a,(tick)
        add a,(ix+0)
        and 3                   ; the frame
        ld c,a
        add a,a
        add a,c                 ; three strips a frame
        add a,a                 ; a word a strip
        ld e,a
        ld d,0
        ld hl,strips
        add hl,de               ; the frame's first strip
        ld c,(ix+1)             ; the screen column
        ld b,3
strip:
        push bc
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        push hl
        ld l,c
        ld h,(ix+2)
        call draw
        pop hl
        pop bc
        inc c
        inc c
        djnz strip
        ld de,3
        add ix,de
        pop bc
        djnz object
        jr main
draw:
        ld b,16
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
        ld a,h
        and 7
        jr nz,row_next
        ld a,l
        add a,32
        ld l,a
        ld a,h
        sub 8
        ld h,a
row_next:
        djnz row
        ret
tick:    db 0
objects: db 0
         dw $4002
         db 1
         dw $400A
         db 2
         dw $4012
strips:  dw frame0,frame0+32,frame0+64
         dw frame1,frame1+32,frame1+64
         dw frame2,frame2+32,frame2+64
         dw frame3,frame3+32,frame3+64
frame0:
; frame 0, strip 0
        db $81,$2C,$00,$BD
        db $00,$4E,$00,$DF
        db $07,$70,$FC,$01
        db $00,$92,$88,$23
        db $01,$B4,$10,$45
        db $00,$D6,$00,$67
        db $03,$F8,$20,$89
        db $C0,$1A,$00,$AB
; frame 0, strip 1
        db $20,$8F,$8F,$20
        db $04,$B1,$18,$42
        db $00,$D3,$01,$64
        db $00,$F5,$30,$86
        db $C0,$17,$03,$A8
        db $80,$39,$00,$CA
        db $00,$5B,$01,$EC
        db $00,$7D,$E0,$0E
; frame 0, strip 2
        db $00,$F2,$38,$83
        db $C1,$14,$00,$A5
        db $80,$36,$10,$C7
        db $03,$58,$00,$E9
        db $00,$7A,$E0,$0B
        db $01,$9C,$80,$2D
        db $00,$BE,$00,$4F
        db $0F,$E0,$04,$71
frame1:
; frame 1, strip 0
        db $80,$35,$10,$C6
        db $00,$57,$03,$E8
        db $00,$79,$E0,$0A
        db $00,$9B,$81,$2C
        db $00,$BD,$00,$4E
        db $00,$DF,$07,$70
        db $FC,$01,$00,$92
        db $88,$23,$01,$B4
; frame 1, strip 1
        db $03,$98,$80,$29
        db $00,$BA,$00,$4B
        db $01,$DC,$00,$6D
        db $00,$FE,$20,$8F
        db $8F,$20,$04,$B1
        db $18,$42,$00,$D3
        db $01,$64,$00,$F5
        db $30,$86,$C0,$17
; frame 1, strip 2
        db $00,$FB,$21,$8C
        db $C0,$1D,$00,$AE
        db $80,$3F,$07,$D0
        db $0C,$61,$00,$F2
        db $38,$83,$C1,$14
        db $00,$A5,$80,$36
        db $10,$C7,$03,$58
        db $00,$E9,$00,$7A
frame2:
; frame 2, strip 0
        db $80,$3E,$00,$CF
        db $0F,$60,$04,$F1
        db $38,$82,$C0,$13
        db $01,$A4,$80,$35
        db $10,$C6,$00,$57
        db $03,$E8,$00,$79
        db $E0,$0A,$00,$9B
        db $81,$2C,$00,$BD
; frame 2, strip 1
        db $0C,$A1,$80,$32
        db $18,$C3,$01,$54
        db $00,$E5,$00,$76
        db $F0,$07,$03,$98
        db $80,$29,$00,$BA
        db $00,$4B,$01,$DC
        db $00,$6D,$00,$FE
        db $20,$8F,$8F,$20
; frame 2, strip 2
        db $F1,$04,$00,$95
        db $80,$26,$00,$B7
        db $03,$48,$00,$D9
        db $00,$6A,$00,$FB
        db $21,$8C,$C0,$1D
        db $00,$AE,$80,$3F
        db $07,$D0,$0C,$61
        db $00,$F2,$38,$83
frame3:
; frame 3, strip 0
        db $10,$47,$03,$D8
        db $00,$69,$00,$FA
        db $20,$8B,$C1,$1C
        db $00,$AD,$80,$3E
        db $00,$CF,$0F,$60
        db $04,$F1,$38,$82
        db $C0,$13,$01,$A4
        db $80,$35,$10,$C6
; frame 3, strip 1
        db $00,$AA,$80,$3B
        db $01,$CC,$00,$5D
        db $00,$EE,$00,$7F
        db $C7,$10,$0C,$A1
        db $80,$32,$18,$C3
        db $01,$54,$00,$E5
        db $00,$76,$F0,$07
        db $03,$98,$80,$29
; frame 3, strip 2: fully transparent (mask $FF, graphic $00 on every line)
blank:
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
        db $FF,$00,$FF,$00
