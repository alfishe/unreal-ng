; IndexedRecords: a table of 8 records of 4 bytes (a word and a byte read, the fourth byte never) read through an
; index: a kind cycling 0..7 times 4 added to the table's address. The same routine reads another table through HL
; at bases one byte apart, so its HL pattern has no lattice of 4. The bytes after the table are other data.
        org $8000
start:
        ld sp,$7F00
        di
main:
        ld a,(kind)
        inc a
        and 7
        ld (kind),a
        ld hl,table
        add a,a
        add a,a
        add a,l
        ld l,a
        ld e,(hl)
        inc l
        ld d,(hl)
        inc l
        ld a,(hl)
        ld (sink),de
        ld (sink+2),a
        ld a,(kind)             ; the same routine walks another table through HL: one pattern of HL for both
        ld hl,other
        add a,l
        ld l,a
        ld a,(hl)
        inc l
        add a,(hl)
        dec l
        dec l
        add a,(hl)
        ld (sink),a
        jr main
kind:   db 0
sink:   ds 3
        org $9000
table:  db 1,0,10,0
        db 2,0,20,0
        db 3,0,30,0
        db 4,0,40,0
        db 5,0,50,0
        db 6,0,60,0
        db 7,0,70,0
        db 8,0,80,0
after:  db $AA,$BB,$CC,$DD
        org $9101
other:  ds 16,7
