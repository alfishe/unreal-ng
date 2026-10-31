; The relocation layout's fixed tail behind an aligned table: a page table at $8200 (LD H,n / LD L,A / LD A,(HL))
; gets ALIGN 256, so a shift of $101 moves what follows it by $200. Records at $9800 found by index arithmetic stay
; fixed; the unused block below them ($9681-$97FF, $17F bytes) takes the $101 shift but not the $200: the fixed tail
; starts after the block before it ($9001-$967F), at $9680.
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
        ld h,$13
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,(hl)
        ld (out),a
        ld a,(value)
        ld (out),a
        ld a,(other)
        ld (out),a
        jr main
index:  db 0
out:    db 0
        ds $8200-$
table:  db 3,1,4,1,5,9,2,6, 5,3,5,8,9,7,9,3
        ds $9000-$
value:  db $55
        ds $9680-$
other:  db $66
        ds $9800-$
records:
        db 1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32
        db 33,34,35,36,37,38,39,40, 41,42,43,44,45,46,47,48, 49,50,51,52,53,54,55,56, 57,58,59,60,61,62,63,64
