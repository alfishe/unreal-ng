; The relocation layout's fixed tail: records at $9800 found by index arithmetic (HL = ($1300 + index) * 8, no
; immediate or pointer explains $9800), so the relocation cannot move them. Two unused blocks of over $101 bytes:
; the fixed tail starts after the last one below the records ($9800), not after the one above them ($9E00).
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
        ld h,$13
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,(hl)
        ld (out),a
        ld a,($9E00)
        ld (out),a
        jr main
index:  db 0
out:    db 0
        ds $9800-$
records:
        db 1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32
        db 33,34,35,36,37,38,39,40, 41,42,43,44,45,46,47,48, 49,50,51,52,53,54,55,56, 57,58,59,60,61,62,63,64
        ds $9E00-$
tail:   db $AA
