; AnalysisDetectorScreens_UnpackedRoomMapIsNoBackBuffer: a 2 K room map the code unpacks again every pass (a new room)
; and that the room-draw routine reads, one cell at a time, to pick the tile byte it puts on the screen (LD L,A /
; LD H,n / LD A,(HL)). The map is rewritten and read by a routine writing the screen, like a back buffer, but its own
; values never reach the screen: they are the index of the tile bytes drawn. No back buffer.
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(phase)            ; unpack the room: every cell, every pass
        inc a
        ld (phase),a
        ld hl,map
        ld bc,2048
unpack: ld (hl),a
        inc hl
        inc a
        and 7
        dec bc
        ld e,a
        ld a,b
        or c
        ld a,e
        jr nz,unpack
        call draw
        jr main
draw:   ld hl,map               ; one tile byte for each cell, the top third of the screen
        ld de,$4000
        ld bc,2048
cell:   ld a,(hl)               ; the cell's tile number
        push hl
        ld l,a
        ld h,$A0                ; tiles: the high byte
        ld a,(hl)               ; the tile's byte
        ld (de),a
        pop hl
        inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,cell
        ret
phase:  db 0
        org $9000
map:    ds 2048
        org $A000
tiles:  db $81,$42,$24,$18,$3C,$7E,$FF,$AA
