; FinishUnused: bytes nothing touched and no detector explains. A fill of one value (64 zeros) and a routine that never
; ran (it calls itself and the ROM, its loops land on its own instructions) are Unused; a small picture nothing read
; (a few bytes with no calls or jumps in them) is not code that never ran. Neither is a handler a table entry points
; at: it is a routine the session did not reach.
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
handlers:
        dw handler, handler_beep
        org $8100
picture:
        db $3C,$42,$81,$A5,$81,$99,$42,$3C,$18,$24,$42,$7E,$42,$42,$66,$3C
        db $7E,$40,$7C,$02,$02,$42,$3C,$11
fill:
        ds 64,$00
leftover:
        ld hl,$4000
        ld de,$4001
        ld bc,$17FF
        ld (hl),l
        ldir
        ld b,4
leftover_loop:
        call leftover_border
        call $0D6B
        djnz leftover_loop
        jp leftover
leftover_border:
        ld a,7
        out ($FE),a
        ret
leftover_end:
        db $AA
        ds 40,$00
handler:
        ld a,(counter)
        or a
        jr z,handler_skip
        call handler_beep
        call $03B5
handler_skip:
        ld hl,$5800
        ld (hl),a
        call handler_beep
        jp handler
handler_beep:
        ld a,2
        out ($FE),a
        ret
handler_end:
        db $AA
