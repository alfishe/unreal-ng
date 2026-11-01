; AnalysisDetectorLevelMaps, maps side by side: two 32 x 8 maps (level0, level1, one after the other) drawn in turn
; by one loop of 256 passes, a cell a pass (8-byte tiles into a pixel back buffer copied to the screen). One walk reads
; 256 bytes and the walks begin on that lattice: two maps, not one 32 x 16 map.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(which)
        xor 1
        ld (which),a
        ld ix,level0
        jr z,draw
        ld ix,level1
draw:
        call drawmap
        ld hl,$A000
        ld de,$4000
        ld bc,2048
        ldir
        jr main
drawmap:
        ld de,$A000
        ld bc,256
cell:
        push bc
        push de
        ld a,(ix+0)
        add a,a
        add a,a
        add a,a
        ld l,a
        ld h,0
        ld bc,tiles
        add hl,bc
        ld b,8
tilerow:
        ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz tilerow
        pop de
        inc e
        inc ix
        pop bc
        dec bc
        ld a,b
        or c
        jr nz,cell
        ret
which:  db 0
tiles:
        db $00,$00,$00,$00,$00,$00,$00,$00
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        org $9000
level0:
        db 2,1,2,0,1,2,0,1,2,0,2,2,0,1,2,2
        db 1,2,0,1,2,0,1,2,0,2,2,0,1,2,2,1
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
        db 2,0,1,2,0,2,2,0,1,2,2,1,2,0,1,2
        db 0,1,2,0,2,2,0,1,2,2,1,2,0,1,2,0
        db 2,1,2,0,1,2,0,1,2,0,2,2,0,1,2,2
        db 1,2,0,1,2,0,1,2,0,2,2,0,1,2,2,1
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
        db 2,0,1,2,0,2,2,0,1,2,2,1,2,0,1,2
        db 0,1,2,0,2,2,0,1,2,2,1,2,0,1,2,0
        db 2,1,2,0,1,2,0,1,2,0,2,2,0,1,2,2
        db 1,2,0,1,2,0,1,2,0,2,2,0,1,2,2,1
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
level1:
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
        db 2,0,1,2,0,2,2,0,1,2,2,1,2,0,1,2
        db 0,1,2,0,2,2,0,1,2,2,1,2,0,1,2,0
        db 2,1,2,0,1,2,0,1,2,0,2,2,0,1,2,2
        db 1,2,0,1,2,0,1,2,0,2,2,0,1,2,2,1
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
        db 2,0,1,2,0,2,2,0,1,2,2,1,2,0,1,2
        db 0,1,2,0,2,2,0,1,2,2,1,2,0,1,2,0
        db 2,1,2,0,1,2,0,1,2,0,2,2,0,1,2,2
        db 1,2,0,1,2,0,1,2,0,2,2,0,1,2,2,1
        db 2,2,0,1,2,2,1,2,0,1,2,0,1,2,0,2
        db 2,0,1,2,2,1,2,0,1,2,0,1,2,0,2,2
        db 2,0,1,2,0,2,2,0,1,2,2,1,2,0,1,2
        db 0,1,2,0,2,2,0,1,2,2,1,2,0,1,2,0
