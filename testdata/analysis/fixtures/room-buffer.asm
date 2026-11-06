; Text in a buffer and the buffer's sources: 32-byte rooms copied by LDIR into the buffer at $9000 (room0 and room1
; in turn), the room name (16 characters at +16) drawn from the buffer with the ROM character set, a count in the
; code ends it. room2 is never copied: its name is printable on the rooms' step, a lattice source (inferred). The
; slot after it holds no text (zeros), the one before room0 holds code: the lattice stops there.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(which)
        xor 1
        ld (which),a
        ld hl,room0
        jr z,copy
        ld hl,room1
copy:
        ld de,$9000
        ld bc,32
        ldir
        ld hl,$9010
        ld de,$4000
        ld b,16
print:
        ld a,(hl)
        call glyph
        inc hl
        djnz print
        jr main
glyph:
        push bc
        push hl
        push de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,$3C00
        add hl,bc
        ld b,8
glyph_loop:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz glyph_loop
        pop de
        inc e
        pop hl
        pop bc
        ret
which:  db 0
room0:  db 1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2
        db "The Hall        "
room1:  db 2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1
        db "The Kitchen     "
room2:  db 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
        db "The Cellar      "
after:  ds 32,0
