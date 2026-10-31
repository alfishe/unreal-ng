; Graphics Font negative: four 8x8 glyphs drawn by the character loop with the codes 0-3 (one contiguous run): user
; graphics in a row, not a character set (a font needs three or more codes that are not one run, like a text's).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld de,$4000
        xor a
draw:
        push af
        call glyph
        pop af
        inc a
        cp 4
        jr nz,draw
        jr main
glyph:
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,udgs
        add hl,bc
        ld b,8
glyph_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz glyph_loop
        pop de
        inc e
        ret
udgs:   db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $81,$42,$24,$18,$18,$24,$42,$81
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $00,$66,$66,$00,$00,$66,$66,$00
