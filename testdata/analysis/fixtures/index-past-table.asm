; IndexPastTable: table A (four pointers, code right after it) is read through an index that runs 0..4, one entry
; too far (a real index past the table: it reads the code's bytes); table B is read through an index that stays inside, and a checksum loop walks the bytes around
; it one after another (a list walk or a mask blit through a larger structure): no index, no finding.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call past_a
        ld a,(idx)
        inc a
        cp 5                    ; 0..4: four entries and one past
        jr c,keep
        xor a
keep:   ld (idx),a
        add a,a
        ld l,a
        ld h,0
        ld de,table_a
        add hl,de
        ld e,(hl)               ; the indexed read
        inc hl
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        ld a,(idx)
        and 3                   ; 0..3: inside table B
        add a,a
        ld l,a
        ld h,0
        ld de,table_b
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        ld hl,table_b-8         ; the walk: 8 bytes before table B to 8 after it
        ld b,24
        xor a
sum:    add a,(hl)
        inc hl
        djnz sum
        ld (sink),a
        jr main
idx:    db 0
sink:   db 0
        org $9000
table_a: dw item0,item1,item2,item3
past_a: ld a,1                 ; code right after the table (it runs): index 4 reads its bytes as a pointer
        ret
        org $9100
        db 1,2,3,4,5,6,7,8
table_b: dw item0,item1,item2,item3
        db 9,10,11,12,13,14,15,16
        org $9200
item0:  db 10
item1:  db 11
item2:  db 12
item3:  db 13
