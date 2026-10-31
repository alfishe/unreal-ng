; AnalysisDetectorCodeMap entries: code after an unconditional transfer that only a tail jump (`second`) or a
; backward branch from a later routine (`bump`) reaches starts a routine of its own; a forward conditional skip over
; a JP (`skip`) and the head of a loop entered at its test (`loop`) stay inside their routine. `n` stays 0: the
; tail jump and the backward branch never run, `second` and `bump` are inferred code no L1 routine names.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call first
        call check
        jr main
first:
        ld a,(n)
        or a
        jr z,skip
        jp second
skip:
        ld b,4
        jr test
loop:
        inc c
test:
        djnz loop
        ret
second:
        ld a,c
        ld (n+1),a
        ret
bump:
        ld hl,n+2
        inc (hl)
        ret
check:
        ld a,(n)
        or a
        jr nz,bump
        ret
n:      db 0,0,0
