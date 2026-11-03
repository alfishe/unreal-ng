; CopiedTilesFar: tiles-copied.asm with two tile sets far apart: tiles 0-3 come from `tiles`, tiles 4-7 from `far`,
; 2 K on. The bytes between the sets are no tiles: two claims of four tiles, not one of 260 with 252 never copied.
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
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,tiles
        cp 4
        jr c,pick
        ld bc,far-32            ; tiles 4-7: the far set
pick:   add hl,bc
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
        jr main
phase:  db 0
column: db 0
        org $9000
tiles:  db $81,$42,$24,$18,$18,$24,$42,$81
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $AA,$55,$AA,$55,$AA,$55,$AA,$55
        org $9800
far:    db $0F,$0F,$0F,$0F,$F0,$F0,$F0,$F0
        db $01,$03,$07,$0F,$1F,$3F,$7F,$FF
        db $80,$C0,$E0,$F0,$F8,$FC,$FE,$FF
        db $3C,$42,$99,$A5,$A5,$99,$42,$3C
after:  db 1,2,3,4,5,6,7,8
        org $A000
buffer: ds 11*192
