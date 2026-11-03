; AnalysisDetectorInitTables_SevenBytesOfPadding: as init-padded.asm, with entries of 9 bytes in 16-byte slots: a block
; copy puts the table at $9000, a loop reads each entry's 9 bytes through an index (base + 16 x k), the last seven
; bytes of each slot are never read (positive: one table from $9000 to the last entry's last byte, its padding of
; seven bytes inside). The copy also writes a tail past the table that nothing reads (negative: not in the table).
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
        add a,a                 ; x 16: the slot
        add a,a
        add a,a
        add a,a
        ld l,a
        ld h,0
        ld de,table
        add hl,de               ; the indexed read
        ld b,9
        xor a
sum:    add a,(hl)
        inc hl
        djnz sum
        ld (sink),a
        jr main
idx:    db 0
sink:   db 0
source:
        db $C3,$81,$C3,$FF,$11,$12,$13,$14,$15,$00,$00,$00,$00,$00,$00,$00
        db $F0,$E0,$F0,$FF,$21,$22,$23,$24,$25,$00,$00,$00,$00,$00,$00,$00
        db $FC,$F8,$FC,$FF,$31,$32,$33,$34,$35,$00,$00,$00,$00,$00,$00,$00
        db $FF,$FE,$FF,$FF,$41,$42,$43,$44,$45,$00,$00,$00,$00,$00,$00,$00
        db $C7,$81,$81,$E3,$51,$52,$53,$54,$55,$00,$00,$00,$00,$00,$00,$00
        db $F1,$E0,$E0,$F8,$61,$62,$63,$64,$65,$00,$00,$00,$00,$00,$00,$00
        db $FC,$F8,$F8,$FE,$71,$72,$73,$74,$75,$00,$00,$00,$00,$00,$00,$00
        db $FF,$FE,$FE,$FF,$81,$82,$83,$84,$85,$00,$00,$00,$00,$00,$00,$00
        db 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
source_end:
        org $9000
table:  ds 128
tail:   ds 16
