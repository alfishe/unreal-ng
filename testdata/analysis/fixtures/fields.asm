; BaseOffsetIx / BaseOffsetHl: four 5-byte records walked by IX (fields +0, +2, +3) and the same records walked by
; HL (bytes +0, +1 read, +4 written, then HL steps to the next record).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call walk_ix
        call walk_hl
        jr main
walk_ix:
        ld ix,records
        ld b,4
        ld de,5
ix_loop:
        ld a,(ix+0)
        add a,(ix+2)
        ld (ix+3),a
        add ix,de
        djnz ix_loop
        ret
walk_hl:
        ld hl,records
        ld b,4
hl_loop:
        ld a,(hl)
        inc hl
        add a,(hl)
        inc hl
        inc hl
        inc hl
        ld (hl),a
        inc hl
        djnz hl_loop
        ret
records:
        ds 20
