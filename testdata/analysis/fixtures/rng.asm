; AnalysisDetectorRng: a 16-bit seed updated by a shift-and-XOR step (xorshift) on every call of random, its result
; stored by the caller; and a counter the caller only increments (negative: no RNG claim).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call random
        ld (last),a
        ld hl,counter
        inc (hl)
        jr main
random:
        ld hl,(seed)
        ld a,h
        rra
        ld a,l
        rra
        xor h
        ld h,a
        ld a,l
        rra
        ld a,h
        rra
        xor l
        ld l,a
        xor h
        ld h,a
        ld (seed),hl
        ret
seed:    dw 1
last:    db 0
counter: db 0
