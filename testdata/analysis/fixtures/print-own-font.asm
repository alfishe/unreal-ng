; AnalysisDetectorFonts, the game's own font: a zero-terminated message drawn by a glyph loop from a character set at
; $9100 (codes 32..127 on base $9000), no ROM printing. The text is found through the glyph blit, then the font from
; the text's codes. The glyph bytes are printable ('A'): read by the glyph loop, they are pixels, not text.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,message
        ld de,$4000
print:
        ld a,(hl)
        or a
        jr z,main
        call glyph
        inc hl
        jr print
glyph:
        push hl
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,$9000
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
        pop hl
        ret
message: db "PRESS FIRE",0
        org $9100
font:   ds 768,$41
