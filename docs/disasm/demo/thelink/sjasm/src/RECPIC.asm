; =====================================================================================================================
; RECPIC.asm - recodes a 16-colour picture in memory from BMP nibble order to the 16-colour screen byte format
; =====================================================================================================================
;
; A Spectrum-side tool, NOT in the demo build: it INCBINs baba16.C (at #8000) and baba16.0 (at #C000), files that
; are not on the author's disk, so build.py does not assemble it. Despite the name (RECPIC: "recode picture"),
; it records nothing.
;
; GO (#6000, also the run address: `ORG GO` at the end):
;   1. builds IGRBtab (#7900): byte %IGRBigrb (left pixel IGRB, right pixel igrb, as in a 4-bit BMP) ->
;      %iIgrbGRB (the 16-colour screen byte: left pixel in bits 6, 2-0, right pixel in bits 7, 5-3);
;   2. recodes every byte #8000-#FFFF in place through the table; RET.
; The result stays in memory (#C000-#FFFF in whatever page is selected there); nothing is saved.
; The table code is the same as in 16CCON.asm.
;
; Converter additions: ALASM's multi-operand `SRA A,A,A,A` split, JNZ as JR NZ, `high` for ALASM's `'`. Comments
; starting with "en:" translate the author's Russian ones.
; =====================================================================================================================
IGRBtab=#7900                   ;для декодирования bmp (en: for decoding the bmp)
        ORG #6000
GO
;строим таблицу перекодирования bmp
; en: build the bmp recoding table
        LD HL,IGRBtab
MKIGRB  LD A,L                  ;%IGRBigrb -> %iIgrbGRB
        SRA A
        SRA A
        SRA A
        SRA A
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
        JR NZ,MKIGRB


; recode #8000-#FFFF in place
        LD HL,#8000
        LD B,high IGRBtab
REC0
        LD C,(HL)
        LD A,(BC)
        LD (HL),A
        INC HL
        LD A,H
        OR L
        JR NZ,REC0
        RET


; the picture: two 16 KB halves (not on the disk)
        ORG #8000
        INCBIN "baba16.C"
        ORG #C000
        INCBIN "baba16.0"

        ORG GO
