;Object Saver v4.0 ЦаПрЮгА 12vi5 Alone

;GO=куда идти без CAPS

;проект должен только что скомпилировать таблицу
;ORG ObjTab
;DB "имяфайлаE
;DW begin in mem
;DW len
;DB page
;DW start/ещё_2_буквы_расширения
;...
        NOP
        ORG $
nenado
        IFN ?make
        CALL 8026
        JP C,GO
        ENDIF
        LD HL,ObjTab
_SVNEXTLD A,(HL)
        OR A
        RET Z
        LD DE,#5CDD
        LD BC,9
        LDIR
        LD E,(HL)
        INC HL
        LD D,(HL)
        INC HL
        PUSH DE
        LD E,(HL)
        INC HL
        LD D,(HL)
        INC HL
        LD A,(HL)
        INC HL
        LD BC,#7FFD
        OUT (C),A
        EX (SP),HL ;HL=begin
        PUSH HL,DE
        LD C,10
        CALL #3D13
        INC C
        LD C,18
        CALL NZ,#3D13
        POP DE,HL
        LD C,11
        CALL #3D13

       LD C,10 ;find desc
       CALL #3D13
        POP HL
       LD E,(HL)
       INC HL
       LD D,(HL)
       INC HL
        PUSH HL
       LD (#5CE6),DE
       LD A,C
       LD C,9 ;save desc
       CALL #3D13
        POP HL
        JR _SVNEXT
        DISPLAY "Saver:",nenado,"-",$
        DISPLAY "RUN[CS/Ent]
