; L1: a word copied from a table into a list through HL (LD HL,(table) / LD (list),HL) remembers the table; a PUSH
; over the list then writes other bytes there. Read back and used as an address, the list's word is a pointer, the
; table's is not: the push forgot the copy (negative).
        org $8000
start:
        di
main:
        ld hl,(table)
        ld (list),hl
        ld (saved),sp
        ld sp,list+2
        ld de,$5800
        push de
        ld sp,(saved)
        ld hl,(list)
        ld a,(hl)
        jr main
saved:  dw 0
table:  dw $C0C0
list:   dw 0
