; Tiles on the lattice of the text's codes (Hungry Horace's maze tiles): the maze loop draws the cells of `maze` from
; `tiles` (8 bytes a cell, all six drawn); for the base tiles - 8 x 65 five of them are the codes of the printed
; message (A, B, C, E, F: not one run, D is missing). The walk that picked them read the maze, no text: tiles, no font.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,message
        ld de,$4000
print:
        ld a,(ix+0)
        and $7F
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,$3C00
        add hl,bc
        push de
        ld b,8
glyph:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz glyph
        pop de
        inc e
        bit 7,(ix+0)
        inc ix
        jr z,print
        ld hl,maze
        ld de,$4020
        ld c,64
cell:
        push hl
        ld l,(hl)
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld ix,tiles
        ex de,hl
        add ix,de
        ex de,hl
        push de
        ld b,8
tile:
        ld a,(ix+0)
        ld (de),a
        inc d
        inc ix
        djnz tile
        pop de
        inc e
        pop hl
        inc hl
        dec c
        jr nz,cell
        jr main
message: db "ABCE",$C6
tiles:
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $AA,$55,$AA,$55,$AA,$55,$AA,$55
        db $66,$66,$66,$66,$66,$66,$66,$66
        db $00,$7F,$7F,$60,$60,$67,$67,$66
maze:
        db 0,5,4,3,2,1,0,5,4,3,2,1,0,5,4,3
        db 2,1,0,5,4,3,2,1,0,5,4,3,2,1,0,5
        db 1,0,5,4,3,2,1,0,5,4,3,2,1,0,5,4
        db 3,2,1,0,5,4,3,2,1,0,5,4,3,2,1,0
