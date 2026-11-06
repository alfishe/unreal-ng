; AnalysisDetectorText_PrinterArgument: one routine prints strings ended by $FF (H.A.T.E.'s $EE17): a byte with bit 7
; starts a record (a colour, then a row and a column), the others are characters drawn from the ROM set. Two plain
; strings are text by their characters. The gauge holds a record, three cells the main loop rewrites before each
; print, a second record and a '/': one printable byte no code wrote, so no run of its own is text. LD HL,gauge /
; CALL print_str passes it to the routine that prints the plain strings: it is text up to the $FF the printer stops
; at after them. The bytes after it are only summed: no text.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,score
        ld de,$4000
        call print_str
        ld hl,lives
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
        ld de,$4040
        call print_str
        call checksum
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
        call glyph
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
checksum:
        ld hl,after
        ld b,3
        xor a
sum:
        add a,(hl)
        inc hl
        djnz sum
        ld (total),a
        ret
colour:      db 0
row:         db 0
column:      db 0
count:       db 0
total:       db 0
score:       db "SCORE",$FF
lives:       db "LIVES",$FF
gauge:       db $84,3,6
gauge_cells: db "   "
             db $87,2,6,"/"
gauge_end:   db $FF
after:       db "XYZ"
