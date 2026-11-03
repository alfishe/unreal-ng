; AnalysisDetectorRecords_RestartTerminator: 16-byte records walked by HL; each record's kind byte at +0 is read, then
; the walk restarts at +2 (ADD HL,BC writes HL: a new base) and tests the byte there: $FF ends the list, else the byte
; at +3 is read too. The last base the walk held is the restart at +2 of the seventh record: the terminator is that
; byte ($FF), not the record's start (positive: six records, the terminator claimed at +2 of the seventh).
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
        ld a,(hl)               ; +0: the kind
        ld (sink),a
        push hl
        ld bc,2
        add hl,bc               ; the restart at +2
        ld a,(hl)
        cp $FF
        jr z,done
        inc hl
        add a,(hl)              ; +3
        ld (sink),a
        pop hl
        ld de,16
        add hl,de
        jr loop
done:
        pop hl
        ret
sink:   db 0
        org $9000
recs:   db 1,0,10,20
        ds 12,0
        db 2,0,11,21
        ds 12,0
        db 3,0,12,22
        ds 12,0
        db 4,0,13,23
        ds 12,0
        db 5,0,14,24
        ds 12,0
        db 6,0,15,25
        ds 12,0
last:   db 7,0,$FF,0
        ds 12,0
after:  db $AA,$BB
