; The relocation layout with a page-aligned table first in the range: the table at $8000 is read as LD H,table/256 /
; LD L,A / LD A,(HL), and no line comes before it to carry its ALIGN 256. The table stays where it is, the moved
; region starts at the code after it (REGION_START $8100, an ORG there).
        org $8000
table:  ds 256,$11
start:
        ld sp,$9F00
        di
main:
        ld a,(index)
        inc a
        ld (index),a
        ld l,a
        ld h,table/256
        ld a,(hl)
        ld (out),a
        jr main
index:  db 0
out:    db 0
