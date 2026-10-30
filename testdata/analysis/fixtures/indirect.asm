; IndirectTargets: a jump table walked by JP (HL) from a dispatcher that main calls; four handlers, each run.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(index)
        inc a
        and 3
        ld (index),a
        add a,a
        ld e,a
        ld d,0
        ld hl,table
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        call dispatch
        jr main
dispatch:
        jp (hl)
h0:     inc b
        ret
h1:     inc c
        ret
h2:     inc d
        ret
h3:     inc e
        ret
table:  dw h0,h1,h2,h3
index:  db 0
