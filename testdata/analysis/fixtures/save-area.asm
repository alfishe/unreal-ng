; AnalysisDetectorSaveAreas: each pass saves three strips of a background (16, 24 and 20 bytes) into a save area through
; a moving pointer (one LDIR), draws over the background, then copies the strips back (another LDIR) with the same
; lengths. On the first two passes only a fourth strip of 12 bytes goes to the area's end too: those bytes are
; written twice and read twice, by one routine, like a table it built (negative: no init table there).
        org $8000
start:
        ld sp,$8F00
        di
main:
        ld hl,tick
        inc (hl)
        ld de,save
        ld (ptr),de
        ld hl,$9000
        ld bc,16
        call savestrip
        ld hl,$9100
        ld bc,24
        call savestrip
        ld hl,$9200
        ld bc,20
        call savestrip
        xor a
        ld (did),a
        ld a,(extra)
        or a
        jr z,draw
        dec a
        ld (extra),a
        ld a,1
        ld (did),a
        ld hl,$9300
        ld bc,12
        call savestrip
draw:
        ld a,(tick)
        ld ($9005),a
        ld ($9105),a
        ld ($9205),a
        ld ($9305),a
        ld hl,save
        ld de,$9000
        ld bc,16
        call restore
        ld de,$9100
        ld bc,24
        call restore
        ld de,$9200
        ld bc,20
        call restore
        ld a,(did)
        or a
        jp z,main
        ld de,$9300
        ld bc,12
        call restore
        jp main
savestrip:
        ld de,(ptr)
        ldir
        ld (ptr),de
        ret
restore:
        ldir
        ret
tick:   db 0
extra:  db 2
did:    db 0
ptr:    dw 0
        org $9000
        ds $400,$55
        org $A000
save:
        ds 72
