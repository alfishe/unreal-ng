IGRBtab=#7900 ;для декодирования bmp
        ORG #6000
GO
;строим таблицу перекодирования bmp
        LD HL,IGRBtab
MKIGRBLD A,L ;%IGRBigrb -> %iIgrbGRB
        SRA A,A,A,A
        AND %01000111
        LD C,A
        LD A,L
        RLA
        RLA
        RLA
        RLA
        SRA A
        AND %10111000
        OR C
        LD (HL),A
        INC L
        JNZ MKIGRB


        LD HL,#8000
        LD B,'IGRBtab
REC0
        LD C,(HL)
        LD A,(BC)
        LD (HL),A
        INC HL
        LD A,H
        OR L
        JNZ REC0
        RET


        ORG #8000
        INCBIN "baba16.C"
        ORG #C000
        INCBIN "baba16.0"

        ORG GO
