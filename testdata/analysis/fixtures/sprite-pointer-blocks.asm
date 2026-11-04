; AnalysisDetectorSprites, pointer-table blocks: five 16 x 16 masked sprites of 64 bytes, each reached through a table
; of words; the loop draws blocks a, b, d and e, never c. A block of the table as large as its drawn neighbours, never
; drawn and never touched, holds the same kind of sprite (inferred).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(tick)
        inc a
        ld (tick),a
        ld ix,objects
        ld b,4
object:
        push bc
        ld a,(tick)
        add a,(ix+0)
        and 3
        ld e,a
        ld d,0
        ld hl,order
        add hl,de
        ld a,(hl)               ; the block's index in the table
        add a,a
        ld e,a
        ld hl,table
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld l,(ix+1)
        ld h,(ix+2)
        call draw
        ld de,3
        add ix,de
        pop bc
        djnz object
        jr main
draw:
        ld b,16
row:
        ld a,(de)
        and (hl)
        ld c,a
        inc de
        ld a,(de)
        or c
        ld (hl),a
        inc de
        inc l
        ld a,(de)
        and (hl)
        ld c,a
        inc de
        ld a,(de)
        or c
        ld (hl),a
        inc de
        dec l
        inc h
        ld a,h
        and 7
        jr nz,row_next
        ld a,l
        add a,32
        ld l,a
        ld a,h
        sub 8
        ld h,a
row_next:
        djnz row
        ret
tick:    db 0
order:   db 0,1,3,4
objects: db 0
         dw $4002
         db 1
         dw $4008
         db 2
         dw $4014
         db 3
         dw $401A
table:   dw block_a,block_b,block_c,block_d,block_e
block_a:
        db $0E,$F1,$F0,$0F
        db $0D,$F2,$E1,$1E
        db $0B,$F4,$D2,$2D
        db $07,$F8,$C3,$3C
        db $0F,$F0,$B4,$4B
        db $0F,$F0,$A5,$5A
        db $0F,$F0,$96,$69
        db $0F,$F0,$87,$78
        db $0E,$F1,$78,$87
        db $0D,$F2,$69,$96
        db $0B,$F4,$5A,$A5
        db $07,$F8,$4B,$B4
        db $0F,$F0,$3C,$C3
        db $0F,$F0,$2D,$D2
        db $0F,$F0,$1E,$E1
        db $0F,$F0,$0F,$F0
block_b:
        db $87,$79,$E1,$1E
        db $85,$7A,$F0,$0F
        db $83,$7C,$C3,$3C
        db $87,$78,$D2,$2D
        db $87,$78,$A5,$5A
        db $87,$78,$B4,$4B
        db $87,$78,$87,$78
        db $87,$F8,$96,$69
        db $87,$79,$69,$96
        db $85,$7A,$78,$87
        db $83,$7C,$4B,$B4
        db $87,$78,$5A,$A5
        db $87,$78,$2D,$D2
        db $87,$78,$3C,$C3
        db $87,$78,$0F,$F0
        db $87,$F8,$1E,$E1
block_c:
        db $C3,$3D,$C3,$3C
        db $C1,$3E,$D2,$2D
        db $C3,$3C,$E1,$1E
        db $C3,$3C,$F0,$0F
        db $C3,$3C,$87,$78
        db $C3,$3C,$96,$69
        db $83,$7C,$A5,$5A
        db $C3,$BC,$B4,$4B
        db $C3,$3D,$4B,$B4
        db $C1,$3E,$5A,$A5
        db $C3,$3C,$69,$96
        db $C3,$3C,$78,$87
        db $C3,$3C,$0F,$F0
        db $C3,$3C,$1E,$E1
        db $83,$7C,$2D,$D2
        db $C3,$BC,$3C,$C3
block_d:
        db $E1,$1F,$87,$78
        db $E1,$1E,$96,$69
        db $E1,$1E,$A5,$5A
        db $E1,$1E,$B4,$4B
        db $E1,$1E,$C3,$3C
        db $C1,$3E,$D2,$2D
        db $A1,$5E,$E1,$1E
        db $E1,$9E,$F0,$0F
        db $E1,$1F,$0F,$F0
        db $E1,$1E,$1E,$E1
        db $E1,$1E,$2D,$D2
        db $E1,$1E,$3C,$C3
        db $E1,$1E,$4B,$B4
        db $C1,$3E,$5A,$A5
        db $A1,$5E,$69,$96
        db $E1,$9E,$78,$87
block_e:
        db $0E,$F1,$F0,$0F
        db $0D,$F2,$E1,$1E
        db $0B,$F4,$D2,$2D
        db $07,$F8,$C3,$3C
        db $0F,$F0,$B4,$4B
        db $0F,$F0,$A5,$5A
        db $0F,$F0,$96,$69
        db $0F,$F0,$87,$78
        db $0E,$F1,$78,$87
        db $0D,$F2,$69,$96
        db $0B,$F4,$5A,$A5
        db $07,$F8,$4B,$B4
        db $0F,$F0,$3C,$C3
        db $0F,$F0,$2D,$D2
        db $0F,$F0,$1E,$E1
        db $0F,$F0,$0F,$F0
