; AnalysisDetectorRecords: lists of words walked by HL, one base a word (the loop starts a new base each pass), three
; words a list (the last zero) and a hole of one word between lists: bases at 8k, 8k + 2 and 8k + 4, a period of 8
; that fills three of its four word slots. Lists of 2-byte records with holes, not 8-byte records with restarts
; (negative for the fold).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld b,6
        ld hl,lists
next:
        push bc
        call sum
        pop bc
        inc hl
        inc hl
        djnz next
        jr main
sum:
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld a,e
        or d
        ret z
        push hl
        ld hl,(total)
        add hl,de
        ld (total),hl
        pop hl
        jr sum
total:  dw 0
lists:  dw 3,5,0
        dw $FFFF
        dw 7,11,0
        dw $FFFF
        dw 13,17,0
        dw $FFFF
        dw 19,23,0
        dw $FFFF
        dw 29,31,0
        dw $FFFF
        dw 37,41,0
        dw $FFFF
