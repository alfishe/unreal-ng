; AnalysisL3 (candidate bugs, read-unwritten): `buffer` is a RAM area nothing writes and the loop reads (zeros at
; power on: a candidate bug); `table` is static data, a few values among zeros, read by its own loop: its zero run is
; a table's values, not an uninitialised area, so it is no candidate.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,buffer
        ld b,8
readbuf:
        ld a,(hl)
        inc hl
        djnz readbuf
        ld hl,table
        ld b,32
readtab:
        ld a,(hl)
        inc hl
        djnz readtab
        jr main

        org $A000
buffer:    ds 8

        org $A100
table:     db 5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
           db 7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
