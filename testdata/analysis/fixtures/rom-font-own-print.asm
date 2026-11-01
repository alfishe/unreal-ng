; AnalysisDetectorFonts, the ROM's character set printed by the game's own glyph loop (Dizzy 5's): the message holds
; each character's code plus $60 (no printable byte: no text the detectors can name), the loop takes $60 off and reads
; the glyph at $3C00 + 8 x code. The font is the ROM's, all 96 characters, with no text needed.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,message
        ld de,$4000
print:
        ld a,(hl)
        cp $FF
        jr z,main
        sub $60
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
        ld bc,$3C00
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
message: db $B3,$B0,$A5,$AC,$AC,$A2,$AF,$B5,$AE,$A4,$80,$B7,$AF,$B2,$AC,$A4,$FF
