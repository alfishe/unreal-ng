;конвертилка из bmp16 в постолбцовый формат
bmpBUF=#7800
IGRBtab=#7900 ;для декодирования bmp
inbuf=#9800
inlen=#2000
        ORG #8000
begin
GO
        LD A,#10
        CALL OUTA
        LD C,#A ;find
        CALL #3D13
        LD A,C
        LD C,#8 ;read desc
        CALL #3D13
        LD DE,(#5CDD+14)
        LD (23796),DE

        LD A,#14
        CALL OUTA
        LD HL,#4000
        LD DE,#4001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#6000
        LD DE,#6001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#C000
        LD DE,#C001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#E000
        LD DE,#E001
        LD BC,#17FF
        LD (HL),L
        LDIR

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

        LD IX,inbuf+inlen-1
        CALL LDBYTE ;считываем первую порцию данных

        LD BC,(inbuf+10);#76
        CALL LDBYTE
         DEC BC
         LD A,B
         OR C
        JNZ $-6 ;пропустили заголовок bmp и палитру

        LD A,#14
        CALL OUTA
        LD B,192
REBMP0PUSH BC
        LD A,B
        DEC A
        LD C,0
        CALL 8880
       EXD

;сначала читаем строчку в bmpBUF
        LD HL,bmpBUF
        LD B,32*4
        CALL LDBYTE
         LD (HL),A
         INC L
        DJNZ $-5

;теперь перекодируем строчку из формата %LLLLRRRR в %RLRRRLLL
;и переносим результат на экран
        LD HL,bmpBUF
        LD B,'IGRBtab
REBMP1
       SET 7,D
        LD C,(HL) ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
       RES 7,D
        LD C,(HL) ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
       SET 5,D
       SET 7,D
        LD C,(HL) ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
       RES 7,D
        LD C,(HL) ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
       RES 5,D
        INC E
        LD A,E
        AND 31
        JNZ REBMP1
        POP BC
        DJNZ REBMP0

;----------------------------
;теперь показываем
        LD BC,#EFF7
        LD A,#01
        OUT (C),A

;конвертим
        LD IX,#A000 ;pg0
        LD HL,#C000
        LD B,32*4
C2ST0
       PUSH BC
       PUSH HL
        LD BC,#C000 ;C=OLDBYTE
C2ST1LD A,(HL)
        EXA
        LD A,#10
        CALL OUTA
        EXA
        LD (IX),A
        INC IX
        LD A,#14
        CALL OUTA
        CALL DHL
        DJNZ C2ST1
       POP HL
       ;nxtst
        LD A,H
        XOR 128
        BIT 7,H
        LD H,A
        JNZ nxsQ
        LD A,H
        XOR 32
        BIT 5,H
        LD H,A
        JZ nxsQ
        INC L
nxsQ
       POP BC
        DJNZ C2ST0
        LD BC,#EFF7
        LD A,0
        OUT (C),A
        RET
OUTA
       PUSH BC
        LD BC,32765
        OUT (C),A
       POP BC
        RET

DHL
        INC H
        LD A,H
        AND 7
        RET NZ
        LD A,L
        ADD A,32
        LD L,A
        RET C
        LD A,H
        SUB 8
        LD H,A
        RET


LDBYTE
        LD A,(IX)
        INC LX
        RET NZ
        INC HX
       PUSH AF
        LD A,HX
        CP 'inbuf+'inlen
        JR Z,lDBYLD
       POP AF
        RET
lDBYLD
       PUSH BC,DE,HL
        LD HL,inbuf
       PUSH HL
        LD DE,(23796)
        LD BC,inlen+#05
        CALL #3D13
       POP IX
       POP HL,DE,BC
       POP AF
        RET
ZXC
        ORG #5CDD
        DB "creds   b"
        ORG ZXC
  ;DISPLAY "RUN & SAVE #A000,#6000"
   DISPLAY "pack by ZXRar: header=off"
ObjTabDB "creds   C"
        DW #A000
        DW #6000
        DB #10
        DW #A000
make
        INCLUDE "B:SAVEOBJ*",#C0
        ORG $
        CALL GO
        JP nenado

