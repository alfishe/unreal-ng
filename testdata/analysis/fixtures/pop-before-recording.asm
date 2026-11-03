; AnalysisDetectorPointerTables: a recording that begins inside a routine pops the registers the routine saved before
; the recording (POP DE / POP HL from the slots at $9F00-$9F03, which nothing in the replay pushed) and reads and
; writes through them. The replay never loads SP: those slots are the stack, not a list an SP walk reads. No word
; pointer, no pointer table on the stack (negative).
        org $8000
start:
        di
        pop de
        pop hl
main:
        ld a,(hl)
        ld (de),a
        jr main
        org $9F00
saved_de:
        dw $C000
saved_hl:
        dw $C100
