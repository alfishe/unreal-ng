; AnalysisDetectorRecords (gathered by short copies): four blocks of four 8-byte records; a setup copies the block
; of the round (rounds 1 and 2 only are played) 8 bytes at a time into the 16-byte slots of a record array an (IX+d)
; loop reads. The blocks copied are records of 8 bytes with the slots' fields (observed); the table of block pointers
; names the two blocks never copied on the same 32-byte lattice (inferred).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(round)
        inc a
        and 1
        ld (round),a
        add a,a
        ld l,a
        ld h,0
        ld de,blocks
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ld de,slots
        ld b,4
copy:
        push bc
        ld bc,8
        ldir
        ex de,hl
        ld bc,8
        add hl,bc
        ex de,hl
        pop bc
        djnz copy
        ld ix,slots
        ld b,4
use:
        ld a,(ix+0)
        add a,(ix+1)
        add a,(ix+3)
        ld (ix+8),a
        ld de,16
        add ix,de
        djnz use
        jr main
round:  db 0
blocks: dw block1,block2,block3,block4
block1:
        db 1,2,3,4,5,6,7,8
        db 9,10,11,12,13,14,15,16
        db 17,18,19,20,21,22,23,24
        db 25,26,27,28,29,30,31,32
block2:
        db 2,3,4,5,6,7,8,9
        db 10,11,12,13,14,15,16,17
        db 18,19,20,21,22,23,24,25
        db 26,27,28,29,30,31,32,33
block3:
        db 3,4,5,6,7,8,9,10
        db 11,12,13,14,15,16,17,18
        db 19,20,21,22,23,24,25,26
        db 27,28,29,30,31,32,33,34
block4:
        db 4,5,6,7,8,9,10,11
        db 12,13,14,15,16,17,18,19
        db 20,21,22,23,24,25,26,27
        db 28,29,30,31,32,33,34,35
slots:
        ds 64
