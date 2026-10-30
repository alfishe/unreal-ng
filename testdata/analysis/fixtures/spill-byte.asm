; Two 16 x 8 frames stored back to back, the first drawn once a frame, the second twice, by a routine that writes a
; third screen byte on odd rows from the byte after the row (the spill a shifted sprite reads past its row): the last
; row of the first frame shows the second frame's first byte right of it, and no row of either below the other. Two
; frames of 8 rows, not one picture of 16.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,frames
        ld de,$4002
        call draw
        ld hl,frames+16
        ld de,$4012
        call draw
        ld hl,frames+16
        ld de,$4818
        call draw
        jr main
draw:
        ld b,8
row:
        push de
        ld a,(hl)
        ld (de),a
        inc hl
        inc e
        ld a,(hl)
        ld (de),a
        inc hl
        bit 0,b                 ; rows 1, 3, 5, 7 (b 7, 5, 3, 1)
        jr z,next
        inc e
        ld a,(hl)               ; the spill: the next row's first byte, not advanced past
        ld (de),a
next:
        pop de
        inc d
        djnz row
        ret
frames:
        defb $3C,$C3, $42,$24, $81,$5A, $FF,$01, $18,$3C, $C3,$66, $0F,$F0, $A5,$5A
        defb $7E,$81, $99,$66, $24,$42, $DB,$18, $0F,$3C, $F0,$C3, $33,$CC, $5A,$A5
        defb $00
