; AnalysisDetectorVariables: a counter (LD HL,nn / INC (HL)), a flag (two values through (nn)), a pointer (a word
; read by LD HL,(nn) and used as an address) and a value; their neighbour is only in a block copy (negative).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,counter
        inc (hl)
        ld a,(flag)
        xor 1
        ld (flag),a
        ld hl,(pointer)
        ld a,(hl)
        ld a,(counter)
        add a,a
        ld (value),a
        ld hl,block
        ld de,$C000
        ld bc,4
        ldir
        jr main
counter: db 0
flag:    db 0
pointer: dw $C100
value:   db 0
block:   db 1,2,3,4
