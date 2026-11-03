; IndexPastTable: as index-around-table.asm, but the scratch structure is read by one routine (peek) reached through
; two selectors: one runs from 2 bytes before the table to 9 after it, the other stays inside the table (a record walk
; reading a field through another index). The reader, over all its indexes, reads on both sides: no finding.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(idx)
        inc a
        and 3                   ; 0..3: inside the table
        ld (idx),a
        add a,a
        ld l,a
        ld h,0
        ld de,table
        add hl,de
        ld e,(hl)               ; the pointer read
        inc hl
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        ld a,(off)
        inc a
        cp 12                   ; 0..11: from table-2 to table+9
        jr c,keep
        xor a
keep:   ld (off),a
        ld hl,table-2
        add a,l
        ld l,a
        call peek               ; the scratch read through its own index
        ld a,(idx2)
        inc a
        and 7                   ; 0..7: inside the table
        ld (idx2),a
        ld hl,table
        add a,l
        ld l,a
        call peek               ; the same read through another index
        jr main
peek:   ld a,(hl)
        ld (sink),a
        ret
idx2:   db 0
idx:    db 0
off:    db 0
sink:   db 0
        org $9300
        db 1,2,3,4,5,6,7,8,9,10,11,12,13,14
before: db 21,22
table:  dw item0,item1,item2,item3
after:  db 31,32,33,34,35,36,37,38
        org $9400
item0:  db 10
item1:  db 11
item2:  db 12
item3:  db 13
