; AnalysisDetectorInitTables_PaddedEntries: a block copy puts a table of 4-byte entries in 8-byte slots at $9000 (the
; last four bytes of each slot never read), and a loop reads the entries through an index (base + 8 x k) (positive:
; one table from $9000, its padding inside, no index past it). The copy also writes a tail past the table that nothing
; reads (negative: not in the table).
        org $8000
start:
        di
        ld sp,$8F00
        ld hl,source
        ld de,table
        ld bc,source_end-source
        ldir
main:
        ld a,(idx)
        inc a
        and 7
        ld (idx),a
        add a,a                 ; x 8: the slot
        add a,a
        add a,a
        ld l,a
        ld h,0
        ld de,table
        add hl,de               ; the indexed read
        ld b,4
        xor a
sum:    add a,(hl)
        inc hl
        djnz sum
        ld (sink),a
        jr main
idx:    db 0
sink:   db 0
source:
        db $C3,$81,$C3,$FF,$00,$00,$00,$00
        db $F0,$E0,$F0,$FF,$00,$00,$00,$00
        db $FC,$F8,$FC,$FF,$00,$00,$00,$00
        db $FF,$FE,$FF,$FF,$00,$00,$00,$00
        db $C7,$81,$81,$E3,$00,$00,$00,$00
        db $F1,$E0,$E0,$F8,$00,$00,$00,$00
        db $FC,$F8,$F8,$FE,$00,$00,$00,$00
        db $FF,$FE,$FE,$FF,$00,$00,$00,$00
        db 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
source_end:
        org $9000
table:  ds 64
tail:   ds 16
