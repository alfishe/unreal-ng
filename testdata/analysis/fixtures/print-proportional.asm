; AnalysisDetectorText_GlyphIndex: a proportional font with no print path the detectors know (no font claim, no
; glyph blit, no ROM): a character code is the low byte of its glyph's address (LD L,A / LD H,widths), the width at
; $C000 + code, one pixel column a page above it, slid into a buffer. The message is text through its glyphs'
; index. The map's cells are printable tile codes looked up the same way, digits and signs, no words: no text. The
; column bytes ('A') are what the glyph readers read: pixels, not text. Once, a walk over memory (a random number
; generator's) reads the ROM font and the message with one instruction: no glyph reader, the message stays text.
; The score goes through the same font with its zero, but its digits are no word: no text.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,message
        call print
        ld hl,score
        call print
        ld hl,map
        ld b,16
cell:
        ld a,(hl)
        push hl
        ld l,a
        ld h,$C9
tile:   ld a,(hl)
        ld ($4100),a
        pop hl
        inc hl
        djnz cell
        ld a,(walked)
        or a
        jr nz,main
        inc a
        ld (walked),a
        ld hl,$3D00
        ld bc,$5000
walk:   ld d,(hl)
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,walk
        jr main
print:
        ld de,buffer
next:
        ld a,(hl)
        inc hl
        and a
        ret z
        push hl
        ld l,a
        ld h,$C0
width:  ld b,(hl)
column:
        inc h
glyph:  ld a,(hl)
        ld (de),a
        inc de
        djnz column
        pop hl
        jr next
message: db "PROPORTIONAL TEXT",0
score:  db " 00250",0
        db 1
map:    db "0123456789+-=*#@"
        db 0
walked: db 0
buffer: ds 96
        org $C000
widths: ds 256,3
        ds 768,$41
        org $C900
tiles:  ds 256,$55
