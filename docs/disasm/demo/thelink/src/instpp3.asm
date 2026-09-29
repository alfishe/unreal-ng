        MAIN "*",#C6
MKBALLTEX

      IFN mkballplasma
        LD A,gspgballtex1
       DUP 4
        CALL MKBALLTEXNOISEPG
       EDUP
        LD B,10 ;проходов
GSPLASM
       PUSH BC
     ;используем последнюю страницу плазмы в качестве предыдущей
        LD A,gspgballtex1*257<1+#303>1
        OUT (MPAG),A
        LD A,gspgballtex1
        LD B,4 ;страницы
GSPLASM0PG
        OUT (MPAGEX),A ;следующая после (MPAG)
       PUSH AF
        LD HL,#8000
GSPLASM0
        LD A,(HL)
       PUSH HL
        INC L
        ADC A,(HL) ;+noise
        RRA
        INC H
        ADC A,(HL) ;+noise
        RRA
       POP HL
        LD (HL),A
        INC HL
        BIT 6,H
        JZ GSPLASM0
       POP AF
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        DJNZ GSPLASM0PG
       POP BC
        DJNZ GSPLASM
        LD A,gspgballtex1
       DUP 4
        CALL MKBALLTEXCOLORPG
       EDUP
     ENDIF

        LD LX,gspgballtex1*257<1+#303>1 ;from
        LD HX,gspgballtex1*257<1+#707>1 ;to
        LD HL,#FFFF ;from
        LD DE,#BFFF ;to
        LD B,8 ;страниц to
MKBALLTEX8PG
       PUSH BC
        CALL MKBALLTEX8PGCOPY2000
        BIT 6,H
        JNZ MKBALLTEX8PGnH
        LD H,#FF
        LD A,LX ;from pg
        CP gspgballtex1
        JNZ $+4
        LD A,gspgballtex1*257<1+#808>1;в +7 копия конца текстуры
        RLCA
        DEC A
        RRCA
        LD LX,A
MKBALLTEX8PGnH
       PUSH HL
        CALL MKBALLTEX8PGCOPY2000
       POP HL
        BIT 6,D
        JZ MKBALLTEX8PGnD
        LD D,#BF
        LD A,HX ;to pg
        RLCA
        DEC A
        RRCA
        LD HX,A
MKBALLTEX8PGnD
       POP BC
        DJNZ MKBALLTEX8PG
        RET

MKBALLTEX8PGCOPY2000
        LD A,HX ;to pg
        OUT (MPAG),A
        LD A,LX ;from pg
        OUT (MPAGEX),A
        LD BC,#2000
        LDDR
        RET

MKBALLTEXCOLORPG
       PUSH AF
        OUT (MPAGEX),A
        LD HL,#C000
GSPLASM1
        LD A,(HL)
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,#40
        LD (HL),A
        INC HL
        BIT 6,H
        JNZ GSPLASM1
       POP AF
        RLCA
        INC A
        RRCA
        RET

MKBALLTEXNOISEPG
       PUSH AF
        OUT (MPAGEX),A
        LD HL,#0000
        LD DE,#C000
        LD BC,#4000
GSPLASMPRE0
        LD A,R
        LD C,A
        LD A,(DE)
        RLCA
        XOR C
        RLCA
        ADD A,(HL)
        RLCA
        XOR H
        RLCA
        RLCA
        LD (DE),A
        INC HL,DE
        BIT 6,D
        JNZ GSPLASMPRE0
       POP AF
        RLCA
        INC A
        RRCA
        RET

;------------------------------
MKBALLER
;текстура (256x256) в 8 страницах
;на H запас 16 в обе стороны: CP #F0:...SUB #20
;по сканлайну (в памяти - L, на экране - Y) движемся в осн. по U
;процедура лукапа занимает 2 страницы
;HL=смещение текстуры
;A'=GSPGBALLTEX1..n (она же включена)
;начало линии:
;LD SP,
;линия:
;LD D/E,(HL)
;[PUSH DE]
;[INC/DEC L][ADD A,?:LD L,A] - кроме последнего пикс. сканлайна
;[INC/DEC H] - кроме последнего пикс. сканлайна
;~3.5 байта на байт (2 страницы)
;переход между линиями:
;LD A,L
;ADD A,?
;LD L,A
;LD A,H
;ADD A,?
;CP #F8
;JC $+11
; SUB #30
; EXA
; RLCA
; INC A
; RRCA
; OUT (mpagex),A
; EXA
;LD H,A
        LD A,gspgtball
        OUT (MPAGEX),A
        LD HL,GSTBALL
        LD DE,GSTQUARTERBALL
        LD C,64 ;половина высоты (на экране - X)
MKBA0LD B,32 ;половина ширины (на экране - Y)
MKBA1LD A,(DE)
        LD (HL),A ;u
       PUSH AF
        INC DE
        INC HL
        LD A,(DE)
        LD (HL),A ;v
       PUSH AF
        INC DE
        INC HL
        DJNZ MKBA1
        LD B,32 ;вторая половина ширины
MKBA2  POP AF ;v
        EXA
       POP AF ;u
        NEG
        LD (HL),A ;-u
        INC HL
        EXA
        LD (HL),A ;v
        INC HL
        DJNZ MKBA2
        DEC C
        JR NZ,MKBA0
        LD HL,GSTBALL ;начало таблицы
        LD DE,2*64*127+GSTBALL ;начало нижней строки таблицы
        LD C,64 ;вторая половина высоты (на экране - X)
MKBA3LD B,64 ;ширина (на экране - Y)
MKBA4LD A,(HL)
        LD (DE),A ;u
        INC HL
        INC DE
        XOR A
        SUB (HL)
        LD (DE),A ;-v
        INC HL
        INC DE
        DJNZ MKBA4
        DEC D
        DEC C
        JR NZ,MKBA3

        LD HL,GSTBALL
        LD DE,GSTBALL+2
MKBAduvLD A,(DE)
        SUB (HL)
        LD (HL),A ;du
        INC DE,HL
        LD A,(DE)
        SUB (HL)
        LD (HL),A ;dv
        INC DE,HL
        LD A,H
        OR A
        JNZ MKBAduv

        LD A,gspgballer1
        OUT (MPAG),A
        LD LX,A
        LD HL,#8000
        LD DE,GSTBALL
        LD BC,#0040
        LD HX,128 ;высота (на экране - X)
MKBAL0
;начало линии:
        LD (HL),#31 ;ld sp,
        INC HL
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
       PUSH BC
        LD B,64 ;ширина (на экране - Y)
MKBAL1
        BIT 0,B
        LD (HL),#56 ;ld d,(hl)
        JZ $+7
         LD (HL),#5E ;ld e,(hl)
         INC HL
         LD (HL),#D5 ;push de
        INC HL
       LD A,B
       DEC A
       JZ MKBAL1Q ;не надо инкрементов в посл. пикс. линии
        LD A,(DE) ;du
        INC DE
        OR A
        JZ MKBALlQ
        JP M,MKBALlM
        LD (HL),"," ;inc l
        INC HL
        DEC A
        JNZ $-4
        JR MKBALlQ
MKBALlMLD (HL),"-" ;dec l
        INC HL
        INC A
        JNZ $-4
MKBALlQLD A,(DE) ;dv
        INC DE
        OR A
        JZ MKBALhQ
        JP M,MKBALhM
        LD (HL),"$" ;inc h
        INC HL
        DEC A
        JNZ $-4
        JR MKBALhQ
MKBALhMLD (HL),"%" ;dec h
        INC HL
        INC A
        JNZ $-4
MKBALhQDJNZ MKBAL1
MKBAL1Q
        LD (HL),#7D ;ld a,l
        INC HL
        LD (HL),#C6 ;add a,N
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
        LD (HL),#6F ;ld l,a
        INC HL
        LD (HL),#7C ;ld a,h
        INC HL
        LD (HL),#C6 ;add a,N
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
       PUSH DE
        EXD
        LD HL,BALLCODESAMPLE
        LD BC,BALLCODESAMPLELEN
        LDIR
        EXD
       POP DE
       POP BC
        INC B
        BIT 6,B
        JZ $+5
        LD BC,#00C0
        LD A,H
        CP #BF
        JNZ MKBALnPG
        LD (HL),#3E ;ld a,PG
        INC HL
        LD A,LX
        RLCA
        INC A
        RRCA
        LD LX,A
        LD (HL),A
        INC HL
        LD (HL),#D3 ;out (MPAG),a
        INC HL
        LD (HL),MPAG
        INC HL
        OUT (MPAG),A
        LD (HL),#C3
        INC HL
        LD (HL),#00
        INC HL
        LD (HL),#80
        LD HL,#8000
MKBALnPG
        DEC HX
        JP NZ,MKBAL0
MKBALQ
        LD (HL),#DD
        INC HL
        LD (HL),#E9 ;jp (ix)
        RET

BALLCODESAMPLE
        CP #F0
        JC $+11+6
         SUB #20
         EXA
         RLCA
         INC A
         RRCA
        CP gspgballtex1*257<1+#808>1
        JNZ $+4
        LD A,gspgballtex1
         OUT (MPAGEX),A
         EXA
        LD H,A
BALLCODESAMPLELEN=$-BALLCODESAMPLE

;---------------------------
MKTRANSCH
        LD A,gspgtransch
        GSSETPGA
        LD HL,transch
;#114E/2 байт
;транспонируем, двигаясь по источнику горизонтально,
;по приемнику вертикально
;E'=0
;B'='TCH256
;HL=gschmap256
;B=32
;LOOP ;DUP 8
       ;[LD SP,HL]
       ;[EXX]
       ;LD D,'gschscr
      ;DUP 32
       ;[POP HL]
       ;LD C,L/H
       ;LD A,(BC)
       ;LD (DE),A
       ;INC D
       ;INC/DEC B
       ;LD A,(BC)
       ;LD (DE),A
       ;INC D
      ;EDUP
       ;LD A,E
       ;ADD A,#20/#60/#20/#A0/#20/60/#20/#21
       ;LD E,A
       ;[EXX]
       ;[INC H]
      ;EDUP
       ;DEC B
       ;JP NZ,LOOP
        LD (mktranschloop),HL
        LD DE,TTRANSCHX
        LD C,8
MKTRANSCH0
       PUSH DE
       BIT 0,C
       JNZ $+2+3+3
         LD (HL),#F9 ;ld sp,hl
         INC HL
         LD (HL),#D9 ;exx
         INC HL
        LD (HL),#16 ;ld d,
        INC HL
        LD (HL),'gschscr
        INC HL
        LD B,64/4 ;высота 64*2
MKTRANSCH1
        PUSH BC
        EXD
        LD HL,TRANSCHCODE
        LD BC,TRANSCHCODELEN
        LDIR
        EXD
        POP BC
        DJNZ MKTRANSCH1
        DEC HL ;убираем inc d
       POP DE
        LD (HL),#7B ;ld a,e
        INC HL
        LD (HL),#C6 ;add a,
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
        LD (HL),#5F ;ld e,a
        INC HL
       BIT 0,C
       JZ $+2+3+3
         LD (HL),#D9 ;exx
         INC HL
         LD (HL),"$" ;inc h
         INC HL
        DEC C
        JNZ MKTRANSCH0
        LD (HL),#05 ;dec b
        INC HL
        LD (HL),#C2 ;jp nz
        INC HL
mktranschloop=$+1
        LD BC,0
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD (HL),#DD
        INC HL
        LD (HL),#E9 ;jp (ix)
      IFN mkballplasma
       LD HL,WASTCH256
       LD DE,TCH256
       LD BC,512
       LDIR
      ELSE
        LD HL,palgirl
        LD DE,TCH256
MKCH2X256
       MACRO COLSTOBYTE
        LD A,(HL)
        INC HL
        ADD A,A
        ADD A,A
        ADD A,A
        ADD A,A
        SRA A
        AND #80+#38
        LD C,A
        LD A,(HL)
        ADD A,A
        ADD A,A
        ADD A,A
        OR (HL)
        INC HL
        AND #47
        OR C
       ENDM
        COLSTOBYTE
        LD (DE),A
        INC D
        COLSTOBYTE
        LD (DE),A
        DEC D
        INC E
        JNZ MKCH2X256
      ENDIF
        RET
       IFN mkballplasma
WASTCH256
_=0
       DUP 256
        DB _
_=_+1
       EDUP
       DUP 256
        DB _<3&56+(_>3&7)+(_<1&128)+(_>1&64)
_=_+1
       EDUP
       ENDIF

TTRANSCHX
        DB #20     ;#00,#20 слой0 верх,низ
        DB #60,#20 ;#80,#A0 слой2 верх,низ
        DB #A0,#20 ;#40,#60 слой1 верх,низ
        DB #60,#20 ;#C0,#E0 слой3 верх,низ
        DB #1F

TRANSCHCODE
        POP HL
        LD C,L
        LD A,(BC)
        LD (DE),A
        INC D
        INC B
        LD A,(BC)
        LD (DE),A
        INC D
        LD C,H
        LD A,(BC)
        LD (DE),A
        INC D
        DEC B
        LD A,(BC)
        LD (DE),A
        INC D
TRANSCHCODELEN=$-TRANSCHCODE

;---------------------------
GENTEX
        LD A,gspgtex
        GSSETPGA
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),7
        LDIR
        LD IX,#0F38
        LD DE,#3D88 ;"1"
        LD HL,#C000
        CALL GENTEXPP
        LD HX,#17
        LD HL,#C040
        CALL GENTEXPP
        LD HX,#1F
        LD HL,#C080
        CALL GENTEXPP
        LD HX,#27
        LD HL,#C0C0
GENTEXPP
GENTEX0
        LD B,8
GENTEX0DUP
       PUSH BC,HL
        LD A,(DE)
        LD B,8
GENTEX1RLA
        LD C,HX
        JNC $+4
        LD C,LX
       DUP 8
        LD (HL),C
        INC L
       EDUP
        DJNZ GENTEX1
       POP HL,BC
        INC H
        DJNZ GENTEX0DUP
        INC DE
        JNZ GENTEX0
        RET
;--------------------------
GENDIV
        LD A,gspgdiv
        GSSETPGA
;+-6.8=+-6.0/6.0=(L/2)/-H
;считаем 1/H (1.15) и размножаем
        LD HL,#C080 ;-64..+63
GENDIV0
       PUSH HL
        XOR A
        LD C,A
        SUB H
        LD B,A
        LD HL,#0100 ;даст 1/1=#8000, 1/2=#4000...
        XOR A
       DUP 8
        SBC HL,BC
        JNC $+3
        ADD HL,BC
        RLA
        ADD HL,HL
       EDUP
        CPL
        LD D,A
        XOR A
       DUP 8
        SBC HL,BC
        JNC $+3
        ADD HL,BC
        RLA
        ADD HL,HL
       EDUP
        CPL
        LD E,A ;DE=целая часть частного (DE.5+-0.5)
;начинаем с -64: AIX=-DE0/2
        XOR A
        SUB E
        LD B,A
        LD C,#80 ;правильное округление
        SBC A,A
        SUB D ;ABC=-DE.5
        SRA A
        RR B
        RR C
        LD HX,B
        LD LX,C

       POP HL
        LD B,128 ;L=128 для +64/H (не -64/H!)
GENDIV2
        LD C,HX
        LD (HL),C ;low
        INC L
        LD (HL),A ;high
        INC L
;складываем AIX+=0DE.5+0DE.5
        INC IX ;.5+.5
        ADD IX,DE
        ADC A,0
        ADD IX,DE
        ADC A,0
        DJNZ GENDIV2
        INC H
        JP NZ,GENDIV0
        RET
;-----------------------
GENPERSP
        LD A,gspgpersp
        GSSETPGA
;+-6.8=+-6.0/6.0=(L/2)/-H
;считаем 1/H (1.15) и размножаем
;надо числа в 32 раза больше чем в pgdiv
        LD HL,#C080 ;-64..+63
GENPERSP0
       PUSH HL
        XOR A
        LD C,A
        SUB H
        LD B,A
        LD HL,#1000 ;даст 1/16=#8000, 1/32=#4000...
        XOR A
       DUP 8
        SBC HL,BC
        JNC $+3
        ADD HL,BC
        RLA
        ADD HL,HL
       EDUP
        CPL
        LD D,A
        XOR A
       DUP 8
        SBC HL,BC
        JNC $+3
        ADD HL,BC
        RLA
        ADD HL,HL
       EDUP
        CPL
        LD E,A ;DE=целая часть частного (DE.5+-0.5)
;начинаем с -64: AIX=-DE0/2
        XOR A
        SUB E
        LD B,A
        LD C,#80 ;правильное округление
        SBC A,A
        SUB D ;ABC=-DE.5
        LD HX,B
        LD LX,C
       ADD A,32 ;для персп.корректора так удобнее

       POP HL
        LD B,0 ;L=128 для +64/H (не -64/H!)
GENPERSP2
        LD (HL),A ;high
        INC L
;складываем AIX+=0DE.5+0DE.5
        INC IX ;.5+.5
        ADD IX,DE
        ADC A,0
        ADD IX,DE
        ADC A,0
        DJNZ GENPERSP2
        INC H
        JP NZ,GENPERSP0
        RET
;--------------------------------
GENJTNROT
        LD A,gspgsin
        GSSETPGA
        XOR    A
        LD     (INSTMULPREV),A

        LD     IX,MULTAB-#0100
        LD     HL,SIN4
        LD     DE,SINOFS
INST_MUL
        LD     A,(HL)
        INC    HL
INSTMULPREV=$+1
        CP     0
        JR     Z,INSTMULSKIP
        LD     (INSTMULPREV),A
        INC    HX
        EXX

        ADD    A,A
        LD     E,A
        XOR    A
        LD     D,A
       ;LD      L,A
       ;LD      H,A
       LD HL,#0080 ;правильное округление
        LD     B,128 ;0..127
INSTMUL1
        LD     (IX),H
        INC    LX
        ADD    HL,DE
        DJNZ   INSTMUL1

        XOR A
        SUB H
        LD     (IX),A
        INC    LX

        XOR    A
        SUB    L
        LD     L,A     ;L=0-L
        SBC    A,H     ;0-L-H-C
        SUB    L       ;-(0-L)
        LD     H,A             ;24
       INC H ;правильное округление

        LD     B,127 ;-128..-1
INSTMUL2
        ADD    HL,DE
        LD     (IX),H
        INC    LX
        DJNZ   INSTMUL2
        EXX
INSTMULSKIP
        LD     A,HX
        INC    E
        LD     (DE),A

        LD     A,(HL)
        RLA
        JR     NC,INST_MUL

        LD     HL,SINOFS+#44
        LD     B,#3C
        LD     A,(DE)
        LD     (HL),A
        DEC    E
        INC    L
        DJNZ   $-4
        EX     DE,HL
        LD     C,#80
        LDIR

        LD     L,#40
        LD     A,(HL)
        LD     (DE),A
        INC    L
        INC    E
        JR     NZ,$-4

        LD     DE,SIGNOFS
        LD     HL,SIGNOFS_INST
I_OFS
        LD     B,(HL)
        INC    HL
        LD     A,(HL)
        INC    HL
        LD     (DE),A
        INC    E
        DJNZ   $-2
        JR     NZ,I_OFS

        LD     HL,JTNROTPROC_INST
        LD     DE,PROCROT
        LD     BC,JTNROTPROC_LEN
        LDIR
        RET

SIN4
        DEFB       #03,#06,#09,#0D,#10,#13,#16
;       DEFB    #00,#03,#06,#09,#0D,#10,#13,#16
        DEFB   #19,#1C,#1F,#22,#25,#28,#2B,#2E
        DEFB   #31,#34,#37,#3A,#3C,#3F,#42,#44
        DEFB   #47,#4A,#4C,#4F,#51,#54,#56,#58
        DEFB   #5B,#5D,#5F,#61,#63,#65,#67,#69
        DEFB   #6A,#6C,#6E,#6F,#71,#72,#74,#75
        DEFB   #76,#77,#79,#7A,#7A,#7B,#7C,#7D
        DEFB   #7E,#7E,#7F,#7F,#7F,#80; #80,#80

;       DEFB    #80,#80,#80,#80,#7F,#7F,#7F,#7E

SIGNOFS_INST

        DEFB   1,U0,3,UB0,57,CH_I,3,UM90
        DEFB   1,U90,3,UB90,57,CH_II,3,UM180
        DEFB   1,U180,3,UB180,57,CH_III,3,UM270
        DEFB   1,U270,3,UB270,57,CH_IV,3,UM360



; X'=X*COS(F)-Y*SIN(F)
; Y'=X*SIN(F)+Y*COS(F)

;C=X    DE=COS
;B=Y    HL=SIN
;OUTP A=X,B=Y
JTNROTPROC_INST

;COS SIN
; +   +
CH_I=$-JTNROTPROC_INST
        LD     L,C
        LD     E,B
        LD     A,(DE)
        ADD    A,(HL)
        LD     L,B
        LD     E,C
        LD     B,A
        LD     A,(DE)
        SUB    (HL)
        RET
; -   +
CH_II=$-JTNROTPROC_INST
        LD     L,C
        LD     E,B
        EX     DE,HL
        LD     A,(DE)
        SUB    (HL)
        LD     L,C
        LD     E,B
        LD     B,A
        LD     A,(DE)
        ADD    A,(HL)
        NEG
        RET
; -   -
CH_III=$-JTNROTPROC_INST
        LD     L,C
        LD     E,B
       IFN 0
        LD     A,(DE)
        ADD    A,(HL)
        NEG
        LD     L,B
        LD     E,C
        LD     B,A
        EX     DE,HL
       ELSE
        XOR A
        SUB (HL)
        EX DE,HL
        SUB (HL)
        LD E,B
        LD L,C
        LD B,A
       ENDIF
        LD     A,(DE)
        SUB    (HL)
        RET
; +   -
CH_IV=$-JTNROTPROC_INST
        LD     L,C
        LD     E,B
        LD     A,(DE)
        SUB    (HL)
        LD     L,B
        LD     E,C
        LD     B,A
        LD     A,(DE)
        ADD    A,(HL)
        RET
;COS SIN
; 1   0
U0=$-JTNROTPROC_INST
        LD     A,C
        RET
; 1  -P
UM360=$-JTNROTPROC_INST
        LD     L,C
        LD     A,B
        SUB    (HL)
        LD     L,B
        LD     B,A
        LD     A,C
        ADD    A,(HL)
        RET
; 1  +P
UB0=$-JTNROTPROC_INST
        LD     L,C
        LD     A,B
        ADD    A,(HL)
        LD     L,B
        LD     B,A
        LD     A,C
        SUB    (HL)
        RET

;COS SIN
; 0   1
U90=$-JTNROTPROC_INST
        XOR    A
        SUB    B
        LD     B,C
        RET
;-P   1
UB90=$-JTNROTPROC_INST
        LD     E,B
        EX     DE,HL
        LD     A,C
        SUB    (HL)
        LD     B,A
        XOR    A
        SUB    L
        LD     L,C
        SUB    (HL)
        RET
;+P   1
UM90=$-JTNROTPROC_INST
        LD     E,B
        LD     A,(DE)
        ADD    A,C
        LD     L,A
        LD     E,C
        LD     A,(DE)
        SUB    B
        LD     B,L
        RET

;COS SIN
;-1   0
U180=$-JTNROTPROC_INST
        XOR    A
        SUB    B
        LD     B,A
        XOR    A
        SUB    C
        RET
;-1  -P
UB180=$-JTNROTPROC_INST
        LD     L,C
        XOR    A
        SUB    B
        SUB    (HL)
        LD     L,B
        LD     B,A
        LD     A,(HL)
        SUB    C
        RET
;-1  +P
UM180=$-JTNROTPROC_INST
        LD     L,C
        LD     A,(HL)
        SUB    B
        LD     L,B
        LD     B,A
        XOR    A
        SUB    C
        SUB    (HL)
        RET

;COS SIN
; 0  -1
U270=$-JTNROTPROC_INST
        XOR    A
        SUB    C
        LD     C,A
        LD     A,B
        LD     B,C
        RET
;-P  -1
UM270=$-JTNROTPROC_INST
        LD     E,B
        EX     DE,HL
        XOR    A
        SUB    C
        SUB    (HL)
        LD     B,A
        LD     A,L
        LD     L,C
        SUB    (HL)
        RET

;COS SIN
;+P  -1
UB270=$-JTNROTPROC_INST
        LD     E,B
        EX     DE,HL
        LD     A,(HL)
        SUB    C
        LD     B,A
        LD     A,L
        LD     L,C
        ADD    A,(HL)
        RET
JTNROTPROC_LEN=$-JTNROTPROC_INST

