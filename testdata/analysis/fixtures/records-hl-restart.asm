; AnalysisDetectorRecords: 16-byte records walked by HL; inside each record the walk restarts at +2 (ADD HL,BC
; writes HL: a new base) to read a word there. The bases sit at 16k and 16k + 2. Bit 6 of the first byte ends the
; list (positive: one array of 16-byte records, the word field at +2).
        org $8000
start:
        ld sp,$9F00
        di
main:
        call walk
        jr main
walk:
        ld hl,recs
loop:
        ld a,(hl)
        bit 6,a
        ret nz
        push hl
        ld bc,2
        add hl,bc
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,(de)
        inc a
        ld (de),a
        pop hl
        ld de,16
        add hl,de
        jr loop
recs:   db 1,0
        dw counters
        ds 12,0
        db 2,0
        dw counters+1
        ds 12,0
        db 3,0
        dw counters+2
        ds 12,0
        db 4,0
        dw counters+3
        ds 12,0
        db 5,0
        dw counters+4
        ds 12,0
        db 6,0
        dw counters+5
        ds 12,0
        db $40
        ds 15,0
counters:
        ds 8,0
