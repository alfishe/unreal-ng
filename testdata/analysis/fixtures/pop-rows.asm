; FinishUnused: rows of data an SP walk reads (LD SP,rows / POP DE, summed into HL) and nothing else touches. Only
; popped, never pushed: no stack (negative). The CALL's slots below $9F00 are the stack (positive).
        org $8000
start:
        di
main:
        call tick
        ld (saved),sp
        ld sp,rows
        ld hl,0
        ld b,8
next:
        pop de
        add hl,de
        djnz next
        ld sp,(saved)
        ld (sum),hl
        jr main
tick:
        ret
saved:  dw 0
sum:    dw 0
        org $9000
rows:
        dw $1803,$2C47,$0E91,$5A26,$331C,$4B72,$1F58,$6A0D
rows_end:
        db $AA
