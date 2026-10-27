; AnalysisDetectorMusic, a routine that plays and draws: frame writes the AY (select $FFFD, data $BFFD) from the tune
; and calls draw, which copies a picture to the screen. The tune is music data; the picture, read by a callee that
; writes the display, is not (a player does not draw).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call frame
        jr main
frame:
        ld hl,(pos)
        ld a,(hl)
        inc hl
        ld e,(hl)
        inc hl
        ld de,tune+32
        or a
        sbc hl,de
        add hl,de
        jr c,keep
        ld hl,tune
keep:
        ld (pos),hl
        ld bc,$FFFD
        out (c),a
        ld b,$BF
        out (c),e
        call draw
        ret
draw:
        ld hl,picture
        ld de,$4000
        ld b,24
copy:
        ld a,(hl)
        ld (de),a
        inc hl
        inc e
        djnz copy
        ret
pos:    dw tune
tune:   db 0,$10,1,$01,2,$20,3,$01,4,$30,5,$01,6,$08,7,$38
        db 8,$0F,9,$0E,10,$0D,0,$40,1,$02,2,$50,3,$02,4,$60
picture:
        db $3C,$42,$81,$A5,$81,$99,$42,$3C,$18,$3C,$7E,$FF,$FF,$7E,$3C,$18,$00,$66,$FF,$FF,$7E,$3C,$18,$00
