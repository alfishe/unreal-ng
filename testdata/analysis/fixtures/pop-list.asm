; AnalysisDetectorPointerTables: a blit that walks a list of screen addresses with SP (LD SP,list / POP HL / LD (HL),A,
; SP saved and restored around it): the list is a table of pointers (positive). The routine's own PUSH / POP of a
; register around a call leaves no table on the stack, nor do words popped from the stack before its first push and
; used as addresses (the stack a recording begins with) (negative).
        org $8000
start:
        di
        ld sp,$9EFC
        pop hl
        pop de
        ld a,$AA
        ld (hl),a
        ld (de),a
        ld sp,$9F00
main:
        push bc
        call blit
        pop bc
        jr main
blit:
        ld (saved),sp
        ld sp,list
        ld a,$55
        ld b,6
next:
        pop hl
        ld (hl),a
        djnz next
        ld sp,(saved)
        ret
saved:  dw 0
list:   dw $4000,$4020,$4040,$4060,$4080,$40A0
