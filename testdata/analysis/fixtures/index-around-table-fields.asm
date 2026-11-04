; IndexPastTable: as index-around-table.asm, but the scratch structure is an array of records one routine (fields)
; reads field by field through one selector: its first instruction reads from 2 bytes before the table, the next one
; (the following field) from inside it to 9 bytes after. No instruction alone reads on both sides; the routine's
; connected index reads do: a larger structure, no finding.
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
        cp 10                   ; 0..9
        jr c,keep
        xor a
keep:   ld (off),a
        call fields
        jr main
fields: ld a,(off)              ; field 0: table-2 .. table+7
        ld hl,table-2
        add a,l
        ld l,a
        ld a,(hl)
        ld (sink),a
        ld a,(off)              ; field 1: table+2 .. table+11 (past the 8-byte table)
        ld hl,table+2
        add a,l
        ld l,a
        ld a,(hl)
        ld (sink),a
        ret
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
