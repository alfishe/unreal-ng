; A working buffer: for each of 8 screen columns a 16 x 8 picture (2 bytes by 8 rows) is copied into linebuf, shifted
; right there by the column's number of pixels and drawn from it. The buffer's bytes are drawn with steady steps (right
; 1, down 2) 8 times a frame, but each draw shows a value made for it (written as often as drawn): no picture.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld de,$4000
        ld c,0
column:
        push de
        ld hl,picture
        ld de,linebuf
        ld b,16
fill:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        djnz fill
        ld a,c
        and a
        jr z,shifted
shift:
        ld hl,linebuf
        ld b,8
rows:
        and a
        rr (hl)
        inc hl
        rr (hl)
        inc hl
        djnz rows
        dec a
        jr nz,shift
shifted:
        pop de
        push de
        ld hl,linebuf
        ld b,8
draw:
        ld a,(hl)
        ld (de),a
        inc hl
        inc e
        ld a,(hl)
        ld (de),a
        inc hl
        dec e
        inc d
        djnz draw
        pop de
        inc e
        inc e
        inc e
        inc c
        ld a,c
        cp 8
        jr nz,column
        jr main
linebuf:
        defs 16
picture:
        defb $3C,$C3, $42,$24, $81,$5A, $FF,$00, $18,$3C, $C3,$66, $0F,$F0, $A5,$5A
