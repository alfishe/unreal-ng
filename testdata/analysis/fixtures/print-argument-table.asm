; AnalysisDetectorText_PrinterArgumentTableIsNoText: print-argument.asm with a table of cell colours the caller hands
; to the printer in BC (LD BC,shade / CALL print_str): the printer reads one colour for each character it prints, with
; an instruction of its own (LD A,(BC)), not the one that reads its strings. The table's bytes are no characters and
; it ends on the $FF the printer stops its strings at: still no text, the printer does not read it as a string. The
; gauge (LD HL,gauge) stays text.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,score
        ld bc,shade
        ld de,$4000
        call print_str
        ld hl,lives
        ld bc,shade
        ld de,$4020
        call print_str
        ld a,(count)
        inc a
        and 3
        ld (count),a
        add a,'0'
        ld (gauge_cells),a
        ld (gauge_cells+1),a
        ld (gauge_cells+2),a
        ld hl,gauge
        ld bc,shade
        ld de,$4040
        call print_str
        jr main
print_str:
        ld a,(hl)
        cp $FF
        ret z
        inc hl
        cp $80
        jr c,print_char
        ld (colour),a
        ld a,(hl)
        inc hl
        ld (row),a
        ld a,(hl)
        inc hl
        ld (column),a
        jr print_str
print_char:
        push af
        ld a,(bc)               ; the cell's colour, from the caller's table
        inc bc
        ld (colour),a
        pop af
        push bc
        call glyph
        pop bc
        jr print_str
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
colour:      db 0
row:         db 0
column:      db 0
count:       db 0
score:       db "SCORE",$FF
lives:       db "LIVES",$FF
gauge:       db $84,3,6
gauge_cells: db "   "
             db $87,2,6,"/"
gauge_end:   db $FF
shade:       db $01,$02,$03,$04
shade_end:   db $FF
