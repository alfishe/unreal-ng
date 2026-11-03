; AnalysisDetectorFonts_StringInAnUnusedSlot: the game's own font (codes 32..127 on base $9000) keeps a string in the
; slot of a character no text prints ('K'); the same print loop prints it. The slot is text, not a glyph: the font
; claim leaves it out (no font and text conflict), the glyphs on both sides stay the font. A second loop (a score's)
; draws five rows of the glyph 'M' the print loop never draws: a slot some code drew is a glyph, whoever drew it.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,message
        ld de,$4000
        call print
        ld hl,slot_text
        ld de,$4020
        call print
        ld hl,$9000+'M'*8+2   ; the second loop: five middle rows of one glyph (a score's digit), another shape
        ld de,$4040
        ld b,5
mark_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz mark_loop
        jr main
print:
        ld a,(hl)
        or a
        ret z
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
font:   ds ('K'-32)*8,$41
slot_text:
        db "AUTO",0           ; in the place of the glyph 'K'
        ds 3,$41
        ds (127-'K')*8,$41
