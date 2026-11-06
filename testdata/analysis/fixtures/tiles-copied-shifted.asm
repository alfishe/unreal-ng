; CopiedTiles, shifted: as tiles-copied.asm, but each pass rotates the buffer's bytes a pixel (a scroll) before it
; goes to the screen, so the value flow ends in the buffer: the buffer, not the tiles, is what the screen shows. A back
; buffer of 11 columns, 192 lines each (a byte a line, stored one column after another), rebuilt
; every pass from 8-byte tiles, one LDI chain a tile, then put on the screen with plain loads and stores (the byte
; under a buffer byte on the screen is the next one). Tile 4 is never copied (inferred between the others); the bytes
; after the tile set are other data, no tile.
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(phase)
        inc a
        ld (phase),a
        xor a
        ld (column),a
        ld de,buffer
build:  ld b,24                 ; 24 tiles of 8 lines: one column
tile:   push bc
        ld a,(phase)
        add a,b
        ld c,a
        ld a,(column)
        add a,c
        and 7
        cp 4
        jr nz,pick
        inc a                   ; tile 4 never
pick:   ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,tiles
        add hl,bc
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        pop bc
        djnz tile
        ld a,(column)
        inc a
        ld (column),a
        cp 11
        jr nz,build
        ld hl,buffer            ; the scroll: every byte rotated, a computed value
        ld bc,11*192
shift:  rlc (hl)
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,shift
        ld hl,buffer            ; the buffer to the screen, column after column, line after line
        ld c,0
show:   ld b,0
line:   ld a,b
        and 7
        ld d,a
        ld a,b
        rra
        rra
        rra
        and $18
        or d
        or $40
        ld d,a
        ld a,b
        rla
        rla
        and $E0
        or c
        ld e,a
        ld a,(hl)
        ld (de),a
        inc hl
        inc b
        ld a,b
        cp 192
        jr nz,line
        inc c
        ld a,c
        cp 11
        jr nz,show
        jp main
phase:  db 0
column: db 0
        org $9000
tiles:  db $81,$42,$24,$18,$18,$24,$42,$81
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $AA,$55,$AA,$55,$AA,$55,$AA,$55
        db $0F,$0F,$0F,$0F,$F0,$F0,$F0,$F0
        db $01,$03,$07,$0F,$1F,$3F,$7F,$FF
        db $80,$C0,$E0,$F0,$F8,$FC,$FE,$FF
        db $3C,$42,$99,$A5,$A5,$99,$42,$3C
after:  db 1,2,3,4,5,6,7,8
        org $A000
buffer: ds 11*192
