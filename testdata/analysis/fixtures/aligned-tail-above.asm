; The fixed tail behind an aligned table when no unused block below the data found by arithmetic takes $200 bytes:
; records at $8400 (index arithmetic) stay fixed, the only block of $200 ($8440-$89FF) is above them, so the tail
; stays after the last block of $101 below them ($8281-$83FF), at $8400 (the reviewer moves the tail).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(index)
        inc a
        and 7
        ld (index),a
        ld l,a
        ld h,table/256
        ld a,(hl)
        ld (out),a
        ld a,(index)
        ld l,a
        set 7,l
        ld h,$10
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,(hl)
        ld (out),a
        ld a,(other)
        ld (out),a
        ld a,(tail)
        ld (out),a
        jr main
index:  db 0
out:    db 0
        ds $8100-$
table:  db 3,1,4,1,5,9,2,6, 5,3,5,8,9,7,9,3
        ds $8280-$
other:  db $66
        ds $8400-$
records:
        db 1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32
        db 33,34,35,36,37,38,39,40, 41,42,43,44,45,46,47,48, 49,50,51,52,53,54,55,56, 57,58,59,60,61,62,63,64
        ds $8A00-$
tail:   db $AA
