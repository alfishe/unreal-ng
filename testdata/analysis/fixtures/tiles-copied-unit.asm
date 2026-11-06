; CopiedTilesUnit: the back buffer of tiles-copied.asm, each column copied from one 192-byte strip by one LDIR in pieces
; of 40 and 36 bytes. The lengths' common divisor is 4, but no copy took 4 bytes alone: no tiles of 4 bytes.
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
build:  ld hl,strip             ; one column: the strip in pieces of 40 and 36 bytes, one LDIR for all of them
        ld ix,lengths
piece:  ld c,(ix+0)
        ld b,0
        ld a,c
        or a
        jr z,next
        ldir
        inc ix
        jr piece
next:   ld a,(column)
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
lengths: db 40,36,40,36,40,0
strip:  ds 192,$81
        org $A000
buffer: ds 11*192
