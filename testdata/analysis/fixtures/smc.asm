; AnalysisDetectorCodeMap: an operand the code rewrites before running it (self-modifying), and a branch the replay
; never takes (`flag` stays 0: inferred code); data after the RET is never decoded as code.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(patched+1)
        inc a
        ld (patched+1),a
patched:
        ld b,0
        ld a,(flag)
        or a
        jr nz,never
        jr main
never:
        ld c,b
        inc c
        ret
flag:   db 0
data:   db $00,$00,$00,$C9
