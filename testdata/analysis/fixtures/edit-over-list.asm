; L1: a word copied from a table into a list through HL (LD HL,(table) / LD (list),HL), then a tool edit (a script's
; poke) writes the list and the flag main waits for. Read back and used as an address, then drawn to the screen, the
; list's word holds its own value: the edit forgot the copy (no pointer at the table; the drawn source is the list's
; byte, not the table's).
        org $8000
start:
        di
        ld hl,(table)
        ld (list),hl
wait:
        ld a,(flag)
        or a
        jr z,wait
main:
        ld hl,(list)
        ld a,(hl)
        ld a,(list)
        ld ($4000),a
        jr main
flag:   db 0
table:  dw $C0C0
list:   dw 0
