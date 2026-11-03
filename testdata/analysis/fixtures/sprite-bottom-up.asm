; AnalysisDetectorSprites (bottom up): the 16 x 16 masked sprites of sprite-16x16.asm stored bottom line first, drawn
; from the bottom of each object up the screen (the line above a line comes from the pair after it in memory). The
; claim starts at the frames' first byte, marked bottom up.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(tick)
        inc a
        ld (tick),a
        ld ix,objects
        ld b,3
object:
        push bc
        ld a,(tick)
        add a,(ix+0)
        and 3
        rrca
        rrca
        ld e,a
        ld d,0
        ld hl,sprites
        add hl,de
        ex de,hl
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
        dec h                   ; up one pixel line
        ld a,h
        and 7
        cp 7
        jr nz,row_next
        ld a,l
        sub 32
        ld l,a
        ld a,h
        add a,8
        ld h,a
row_next:
        djnz row
        ret
tick:    db 0
objects: db 0
         dw $4722
         db 1
         dw $4728
         db 2
         dw $4734
sprites:
; each frame stored bottom line first
; frame 0
        db $0F,$F0,$0F,$F0
        db $0F,$F0,$1E,$E1
        db $0F,$F0,$2D,$D2
        db $0F,$F0,$3C,$C3
        db $07,$F8,$4B,$B4
        db $0B,$F4,$5A,$A5
        db $0D,$F2,$69,$96
        db $0E,$F1,$78,$87
        db $0F,$F0,$87,$78
        db $0F,$F0,$96,$69
        db $0F,$F0,$A5,$5A
        db $0F,$F0,$B4,$4B
        db $07,$F8,$C3,$3C
        db $0B,$F4,$D2,$2D
        db $0D,$F2,$E1,$1E
        db $0E,$F1,$F0,$0F
; frame 1
        db $87,$F8,$1E,$E1
        db $87,$78,$0F,$F0
        db $87,$78,$3C,$C3
        db $87,$78,$2D,$D2
        db $87,$78,$5A,$A5
        db $83,$7C,$4B,$B4
        db $85,$7A,$78,$87
        db $87,$79,$69,$96
        db $87,$F8,$96,$69
        db $87,$78,$87,$78
        db $87,$78,$B4,$4B
        db $87,$78,$A5,$5A
        db $87,$78,$D2,$2D
        db $83,$7C,$C3,$3C
        db $85,$7A,$F0,$0F
        db $87,$79,$E1,$1E
; frame 2
        db $C3,$BC,$3C,$C3
        db $83,$7C,$2D,$D2
        db $C3,$3C,$1E,$E1
        db $C3,$3C,$0F,$F0
        db $C3,$3C,$78,$87
        db $C3,$3C,$69,$96
        db $C1,$3E,$5A,$A5
        db $C3,$3D,$4B,$B4
        db $C3,$BC,$B4,$4B
        db $83,$7C,$A5,$5A
        db $C3,$3C,$96,$69
        db $C3,$3C,$87,$78
        db $C3,$3C,$F0,$0F
        db $C3,$3C,$E1,$1E
        db $C1,$3E,$D2,$2D
        db $C3,$3D,$C3,$3C
; frame 3
        db $E1,$9E,$78,$87
        db $A1,$5E,$69,$96
        db $C1,$3E,$5A,$A5
        db $E1,$1E,$4B,$B4
        db $E1,$1E,$3C,$C3
        db $E1,$1E,$2D,$D2
        db $E1,$1E,$1E,$E1
        db $E1,$1F,$0F,$F0
        db $E1,$9E,$F0,$0F
        db $A1,$5E,$E1,$1E
        db $C1,$3E,$D2,$2D
        db $E1,$1E,$C3,$3C
        db $E1,$1E,$B4,$4B
        db $E1,$1E,$A5,$5A
        db $E1,$1E,$96,$69
        db $E1,$1F,$87,$78
