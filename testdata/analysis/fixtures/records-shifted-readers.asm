; AnalysisDetectorRecords (one array, two readers): a table of six 5-byte entries; `play` reads an entry's first
; byte and leaves the entry's address + 1 in `ptr`; `sweep` takes that pointer back from memory and reads the four bytes
; after it. Two patterns of step 5 one byte apart: one array of 6 records at `table`, no record running into `after` (the byte after the table, a
; variable of its own).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(index)
        inc a
        cp 6
        jr c,keep
        xor a
keep:
        ld (index),a
        ld l,a
        ld h,0
        ld e,l
        ld d,h
        add hl,hl
        add hl,hl
        add hl,de
        ld de,table
        add hl,de
        call play
        ld hl,(ptr)
        call sweep
        ld a,(after)
        inc a
        ld (after),a
        jr main
play:
        ld a,(hl)
        inc hl
        ld (ptr),hl
        ret
sweep:
        ld b,(hl)
        inc hl
        ld c,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld e,(hl)
        ld a,b
        add a,c
        add a,d
        add a,e
        ld (total),a
        ret
index:  db 0
ptr:    dw 0
total:  db 0
table:
        db 1,2,3,4,5
        db 1,6,7,8,9
        db 1,10,11,12,13
        db 1,14,15,16,17
        db 1,18,19,20,21
        db 1,22,23,24,25
after:  db 0
