; AnalysisDetectorLevelMaps, one map in two walks (Manic Miner's cavern halves): a 32 x 16 map (level) drawn by one
; loop of 256 passes run twice, the second walk going on where the first ended, the top half to $A000 and the bottom
; half to $A800 (both copied to the screen). The walks draw to different places: one map, not two 32 x 8 maps.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,level0
        ld de,$A000
        call drawmap
        ld de,$A800
        call drawmap
        ld hl,$A000
        ld de,$4000
        ld bc,4096
        ldir
        jr main
drawmap:
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
