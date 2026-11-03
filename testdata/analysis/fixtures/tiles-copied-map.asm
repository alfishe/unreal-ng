; CopiedTiles with a map: as tiles-copied-shifted.asm, but the tile numbers come from a map of 11 x 24 bytes, one
; column of the buffer a row of the map (tile = map byte; ADD A,A / ADD HL,HL x 2 make it 8 x tile), so the map's
; bytes are the index of the LDI chain's reads. The bytes after the map are other data.
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(phase)
        inc a
        and 7
        ld (phase),a
        ld de,buffer
        ld ix,map
        ld c,11
build:  ld b,24
tile:   push bc
        ld a,(ix+0)
        inc ix
        add a,a
        ld l,a
        ld h,0
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
        dec c
        jr nz,build
        ld hl,map               ; the map changes: one cell a pass
        ld a,(phase)
        ld e,a
        ld d,0
        add hl,de
        ld a,(hl)
        inc a
        and 7
        ld (hl),a
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
        org $9000
tiles:  db $81,$42,$24,$18,$18,$24,$42,$81
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $AA,$55,$AA,$55,$AA,$55,$AA,$55
        db $0F,$0F,$0F,$0F,$F0,$F0,$F0,$F0
        db $01,$03,$07,$0F,$1F,$3F,$7F,$FF
        db $80,$C0,$E0,$F0,$F8,$FC,$FE,$FF
        db $3C,$42,$99,$A5,$A5,$99,$42,$3C
        org $9100
map:
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
        db 0,1,2,3,5,6,7,1
after:  db 1,2,3,4,5,6,7,8
        org $A000
buffer: ds 11*192
