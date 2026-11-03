; IndexPastTable with one selector byte for three tables: `level` (0..4) indexes table A and table C through AND 3 (it
; stays inside them) and table B as it is: index 4 reads one entry past table B, the code after it. The index reads of
; the one selector span all three tables, which says nothing about table B: each read by what it read itself, the
; reads of table B run past its end, a finding.
        org $8000
start:
        ld sp,$9F00
        di
main:
        call past_b
        ld a,(level)
        inc a
        cp 5                    ; 0..4
        jr c,keep
        xor a
keep:   ld (level),a
        ld a,(level)            ; table A: 0..3
        and 3
        add a,a
        ld l,a
        ld h,0
        ld de,table_a
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        ld a,(level)            ; table B: 0..4, one entry past it
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
        ld a,(level)            ; table C: 0..3
        and 3
        add a,a
        ld l,a
        ld h,0
        ld de,table_c
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,(de)
        ld (sink),a
        jr main
level:  db 0
sink:   db 0
        org $9000
table_a: dw item0,item1,item2,item3
        org $9100
table_b: dw item0,item1,item2,item3
past_b: ld a,1                 ; code right after table B (it runs): index 4 reads its bytes as a pointer
        ret
        org $9200
table_c: dw item0,item1,item2,item3
        org $9300
item0:  db 10
item1:  db 11
item2:  db 12
item3:  db 13
