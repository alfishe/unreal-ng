; AnalysisDetectorTiles: a 32 x 16 level map of tile numbers (0..3) drawn cell by cell with 9-byte tiles (an
; attribute, then 8 pixel rows) into a pixel back buffer in screen layout ($A000, 4096 bytes) and an attribute back
; buffer ($B000, 512 bytes), both copied to the screen with LDIR once the map is drawn; a copy of the tiles and of
; the map's first 64 bytes only summed (negative: no tile, no level map claim).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call drawmap
        ld hl,$A000
        ld de,$4000
        ld bc,4096
        ldir
        ld hl,$B000
        ld de,$5800
        ld bc,512
        ldir
        call checksum
        jr main
drawmap:
        ld ix,level
        ld de,$A000
        ld c,16
maprow:
        ld b,32
cell:
        push bc
        push de
        ld a,(ix+0)
        ld l,a
        add a,a
        add a,a
        add a,a
        add a,l
        ld l,a
        ld h,0
        ld bc,tiles
        add hl,bc
        push ix
        pop bc
        ld a,b
        add a,$20
        ld b,a
        ld a,(hl)
        ld (bc),a
        inc hl
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
        djnz cell
        ld a,e
        or a
        jr nz,samethird
        ld a,d
        add a,8
        ld d,a
samethird:
        dec c
        jr nz,maprow
        ret
checksum:
        ld hl,plain
        ld b,100
        xor a
sum:
        add a,(hl)
        inc hl
        djnz sum
        ld (total),a
        ret
total:  db 0
tiles:
        db $00,$00,$00,$00,$00,$00,$00,$00,$00
        db $44,$FF,$FF,$DB,$6E,$C5,$40,$00,$00
        db $22,$44,$28,$94,$51,$35,$D6,$58,$10
        db $16,$22,$FF,$88,$88,$FF,$22,$22,$FF
plain:
        db $00,$00,$00,$00,$00,$00,$00,$00,$00
        db $44,$FF,$FF,$DB,$6E,$C5,$40,$00,$00
        db $22,$44,$28,$94,$51,$35,$D6,$58,$10
        db $16,$22,$FF,$88,$88,$FF,$22,$22,$FF
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        org $9000
level:
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1
        db 1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1
        db 1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3
        db 3,0,0,2,0,0,0,0,0,0,2,0,0,0,0,0
        db 0,2,0,0,0,0,0,0,2,0,0,0,0,0,0,3
        db 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
        db 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
