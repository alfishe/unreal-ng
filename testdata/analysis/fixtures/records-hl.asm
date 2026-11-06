; AnalysisDetectorRecords: 4-byte records walked by HL (a byte, a word, a byte), ended by $FF (positive); the same
; bytes only copied by LDIR (negative: no record claim on them).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call walk
        call copy
        jr main
walk:
        ld hl,recs
loop:
        ld a,(hl)
        cp $FF
        ret z
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld a,(de)
        add a,(hl)
        ld (hl),a
        inc hl
        jr loop
copy:
        ld hl,bytes
        ld de,$C000
        ld bc,17
        ldir
        ret
recs:   db 1
        dw $C100
        db 0
        db 2
        dw $C101
        db 0
        db 3
        dw $C102
        db 0
        db 4
        dw $C103
        db 0
        db $FF
bytes:  db 1
        dw $C100
        db 0
        db 2
        dw $C101
        db 0
        db 3
        dw $C102
        db 0
        db 4
        dw $C103
        db 0
        db $FF
