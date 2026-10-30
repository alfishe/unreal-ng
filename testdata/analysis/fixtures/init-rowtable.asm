; AnalysisDetectorInitTables: a table of the 192 screen row addresses built once by the code (positive,
; generator screen-rows), then read; a second buffer the same code fills but nothing reads (negative: no claim).
        org $8000
start:
        ld sp,$9F00
        di
        call build
main:
        ld a,(row)
        inc a
        cp 192
        jr c,keep
        xor a
keep:
        ld (row),a
        ld l,a
        ld h,0
        add hl,hl
        ld de,rows
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,$FF
        ld (de),a
        jr main
build:
        ld hl,$4000
        ld ix,rows
        ld iy,unread
        ld b,192
build_loop:
        ld (ix+0),l
        ld (ix+1),h
        ld (iy+0),l
        ld (iy+1),h
        inc ix
        inc ix
        inc iy
        inc iy
        inc h
        ld a,h
        and 7
        jr nz,next
        ld a,l
        add a,32
        ld l,a
        jr c,next
        ld a,h
        sub 8
        ld h,a
next:
        djnz build_loop
        ret
row:    db 0
rows:   ds 384
unread: ds 384
