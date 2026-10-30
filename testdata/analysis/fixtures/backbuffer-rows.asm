; A back buffer copied a row at a time: 8 x 8 frames (4, one byte by 8 rows) are drawn into buffer (32 bytes a row),
; then each of its 24 rows goes to the display by a chain of 24 LDI (24 bytes, shorter than a copy that counts alone):
; the copier moved 576 bytes in all, the buffer is the display and the frames drawn into it are graphics.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(tick)
        inc a
        ld (tick),a
        ld c,0
object:
        ld a,(tick)
        add a,c
        and 3
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,frames
        add hl,de
        ex de,hl
        ld a,c
        add a,a
        add a,a
        ld l,a
        ld h,buffer/256
        call draw
        inc c
        ld a,c
        cp 3
        jr nz,object
        call copy
        jr main
draw:
        ld b,8
row:
        ld a,(de)
        ld (hl),a
        inc de
        ld a,l
        add a,32
        ld l,a
        djnz row
        ret
copy:
        ld hl,buffer
        ld ix,rows
        ld b,24
crow:
        ld e,(ix+0)
        ld d,(ix+1)
        inc ix
        inc ix
        push bc
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        pop bc
        ld de,8
        add hl,de
        djnz crow
        ret
tick:   db 0
frames: db $18,$3C,$7E,$FF,$FF,$7E,$3C,$18
        db $81,$42,$24,$18,$18,$24,$42,$81
        db $FF,$81,$81,$81,$81,$81,$81,$FF
        db $10,$38,$7C,$FE,$7C,$38,$10,$00
rows:
        dw $4000
        dw $4100
        dw $4200
        dw $4300
        dw $4400
        dw $4500
        dw $4600
        dw $4700
        dw $4020
        dw $4120
        dw $4220
        dw $4320
        dw $4420
        dw $4520
        dw $4620
        dw $4720
        dw $4040
        dw $4140
        dw $4240
        dw $4340
        dw $4440
        dw $4540
        dw $4640
        dw $4740
        org $C000
buffer: ds 768
