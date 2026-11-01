; AnalysisDetectorRecords: four 6-byte records walked by IX with a constant step, ended by $FF at +0 (positive),
; and the same bytes walked by IY with steps 6, 7, 6 (negative: the bases sit on no lattice, no record claim).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call walk_ix
        call walk_iy
        jr main
walk_ix:
        ld ix,recs
        ld de,6
ix_loop:
        ld a,(ix+0)
        cp $FF
        ret z
        ld a,(ix+1)
        add a,(ix+3)
        ld (ix+2),a
        ld a,(ix+4)
        ld (ix+5),a
        add ix,de
        jr ix_loop
walk_iy:
        ld iy,bytes
        ld hl,steps
        ld b,4
iy_loop:
        ld a,(iy+0)
        add a,(iy+1)
        ld (iy+2),a
        ld e,(hl)
        inc hl
        ld d,0
        add iy,de
        djnz iy_loop
        ret
steps:  db 6,7,6,6
recs:   db 1,2,0,3,4,0
        db 5,6,0,7,8,0
        db 9,10,0,11,12,0
        db 13,14,0,15,16,0
        db $FF
bytes:  db 1,2,0,3,4,0
        db 5,6,0,7,8,0
        db 9,10,0,11,12,0
        db 13,14,0,15,16,0
        db $FF
