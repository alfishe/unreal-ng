; The ROM draws the characters: user-defined graphics at udgs (the UDG system variable points there) printed through
; the 48K ROM's character loop PR_ALL ($0B7F: DE the 8 bytes at (UDG) + 8 x the number, as the ROM finds them, HL the display address, C a column that is not the
; first, so no scroll; IY the system variables, all zero: the screen, not the printer). Three of the 21 are drawn, apart; the claim is a font of all 21, no text needed.
        org $8000
start:
        ld sp,$9F00
        di
        ld iy,$5C3A
        ld hl,udgs
        ld ($5C7B),hl
main:
        xor a
        ld hl,$4000
        call glyph
        ld a,3
        ld hl,$4002
        call glyph
        ld a,7
        ld hl,$4004
        call glyph
        jr main
glyph:
        push hl
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,($5C7B)
        add hl,de
        ex de,hl
        pop hl
        ld bc,$1820
        jp $0B7F
udgs:   db $3C,$42,$81,$81,$81,$81,$42,$3C
        ds 16
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        ds 24
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        ds 13*8
