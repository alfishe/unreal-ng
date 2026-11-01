; AnalysisDetectorTiles, page-strided: six 8 x 8 tiles stored a pixel row per 512-byte block (tile t, row r at
; tiles + r*512 + t), each row's mask 256 bytes above it; the draw ORs the picture, steps up a page, ANDs the mask,
; steps up again (the loop of Skool Daze and its kin). Drawn in the order 0,2,3,5,1,4 so no two neighbours follow.
; Expected: one set at tiles, pitch 512, stride 1, 8 x 8, 6 frames, on the picture page, not the mask page.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld ix,order
        ld de,$4000
        ld b,6
tile:
        push bc
        push de
        ld l,(ix+0)
        ld h,$A0
        ld b,8
row:
        xor a
        or (hl)
        inc h
        and (hl)
        inc h
        ld (de),a
        inc d
        djnz row
        pop de
        inc e
        inc ix
        pop bc
        djnz tile
        jr main
order:  db 0,2,3,5,1,4
        org $A000
tiles:
        db $99,$C1,$EB,$93,$BD,$E5
        org $A100
        db $FE,$FD,$FB,$F7,$EF,$DF
        org $A200
        db $AF,$D9,$81,$AB,$D3,$FD
        org $A300
        db $FD,$FB,$F7,$EF,$DF,$BF
        org $A400
        db $C7,$EF,$99,$C1,$EB,$93
        org $A500
        db $FB,$F7,$EF,$DF,$BF,$7F
        org $A600
        db $DD,$87,$AF,$D9,$81,$AB
        org $A700
        db $F7,$EF,$DF,$BF,$7F,$FE
        org $A800
        db $F5,$9D,$C7,$EF,$99,$C1
        org $A900
        db $EF,$DF,$BF,$7F,$FE,$FD
        org $AA00
        db $8B,$B5,$DD,$87,$AF,$D9
        org $AB00
        db $DF,$BF,$7F,$FE,$FD,$FB
        org $AC00
        db $A3,$CB,$F5,$9D,$C7,$EF
        org $AD00
        db $BF,$7F,$FE,$FD,$FB,$F7
        org $AE00
        db $B9,$E3,$8B,$B5,$DD,$87
        org $AF00
        db $7F,$FE,$FD,$FB,$F7,$EF
