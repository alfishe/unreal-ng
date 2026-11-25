; Several sessions of one program: `start` (session A) calls sub_a only; `second` (session B) calls sub_b, then
; sub_a. sub_a reads `table` through HL, sub_b reads `other`: an L1 over both sessions has sub_a called from two
; places with its counts added up, sub_b from the second session alone.
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,table
        call sub_a
idle_a:
        jr idle_a
second:
        ld sp,$9F00
        di
        ld hl,table
        call sub_b
        call sub_a
idle_b:
        jr idle_b
sub_a:
        ld a,(hl)
        ret
sub_b:
        ld a,(other)
        ret
table:
        db 1
other:
        db 2
