; FinishUnused: a routine that never ran, one byte after a fill. The byte before it ($01, LD BC,nn) eats its first
; CALL when decoded from there, so the decode that lands most of its calls and jumps starts at the routine, one byte
; into the stretch. A table entry (a word beside another one near it) points at the routine: it is a routine the
; session did not reach, no leftover (negative), though no word points at the stretch's first byte.
        org $8000
start:
        di
        ld sp,$9F00
main:
        ld a,(counter)
        inc a
        ld (counter),a
        jr main
counter:
        db 0
        org $8100
fill:
        ds 64,$00
junk:
        db $01
routine:
        call routine_border
        ld hl,$4000
        ld de,$4001
        ld bc,$17FF
        ld (hl),l
        ldir
        ld b,4
routine_loop:
        call routine_border
        call $0D6B
        djnz routine_loop
        jp routine
routine_border:
        ld a,7
        out ($FE),a
        ret
routine_end:
        db $AA
        ds 40,$00
entries:
        dw routine,routine_border
        db $AA
