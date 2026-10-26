; PagedRoutinesSeparate (128K): the same address $C000 holds different code in RAM 1 and RAM 3; main pages each in
; and calls it. AnalysisL1_Test writes the two routines into the pages (code1 / code3 below are their bodies).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld bc,$7FFD
        ld a,$11            ; ROM 1, RAM 1 at $C000
        out (c),a
        call $C000
        ld a,$13            ; ROM 1, RAM 3 at $C000
        out (c),a
        call $C000
        jr main
        org $9000
code1:
        inc d
        ret
code3:
        inc e
        inc e
        ret
