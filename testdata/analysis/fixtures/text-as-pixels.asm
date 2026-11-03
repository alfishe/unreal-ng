; AnalysisDetectorGraphics, text no picture: print-own-font.asm's message printed by its glyph loop, and its first 8
; bytes also copied down a screen column once a pass. The message is text (its codes pick glyphs); the column is a
; picture's shape, but bytes a print loop reads as character codes are no pixels: no sprite on the message.
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
        jr z,column
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
column:                         ; the message's first 8 bytes down a screen column, one a line
        ld hl,message
        ld de,$4014
        ld b,8
column_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz column_loop
        jr main
message: db "PRESS FIRE",0
        org $9100
font:   ds 768,$41
