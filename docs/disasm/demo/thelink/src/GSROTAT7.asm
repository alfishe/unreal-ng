zx=0
counter=0
genldi=1
dump=0
border=0

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

ROTATECODE=#7800

resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports",#C5
GSPROG=#5830
GSSTACK=#5830
GSROTPRECODE=#5900
GSROTATECODE=#5900

gspgcode=#10
gspgdata=#20

GSpglogo=4
GSLOGO=#7D00
GSLOGOPROG=GSLOGO+#1B00
GSpgrlscr=2;#2F ;32K
GSpgcopy1=#30 ;32K*16
GSTDHL=#1B00
GSTDHL2=GSTDHL+384
GSGENPROG=#4000;GSTDHL+384
GSGENROTBUF=GSTDHL
GSTMIRROR=GSTDHL+256
        IFN genldi
GSLDI256=#3D00
        ENDIF
GSSCR1=#0000
GSSCR2=#2000

       MACRO GSSETPGA
        OUT (MPAGEX),A
       ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

        ORG #7800
begin
GO
        XOR A
        OUT (#FE),A
        LD SP,#6000

        LD A,C_GRST
        OUT (GSCTR),A
         XOR A
         OUT (GSCOM),A ;не помогает
        HALT
        HALT
        DI
       ;зачем-то POKE #4092,0
        LD DE,#4092
        SD E
        SC #18
        WC
        SD D
        WD
        SD 0
        SC #19
        WC
       ;зачем-то #38 (Load FX)
        SC #38
        WC
       ;зачем-то #D2 (Close Stream)
        SC #D2
        WC

        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER
GSLOA20
        SD E
        SC #18
        WC
        SD D
        WD
        SD (HL)
        SC #19
        WC
        INC HL
        INC DE
        DEC BC
        LD A,B
        OR C
        JNZ GSLOA20
       ;зачем-то #F5 (Busy on)
        SC #F5
        WC
;#13 Jump to Address
        SD .GSPROGGO
        SC #13
        WC
        SD 'GSPROGGO
        WD

        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG

        LD A,3
        OUT (GSCOM),A ;вкл. DMA
;и ждем включения:
        WN
        GD
;
;шлем страницы данных в GS:
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A ;новый адрес DMA
;и ждем переключения:
        WN
        GD
;
;шлем страницы кода в GS:
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,pgcode2
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A ;выкл. DMA
;и ждем выключения DMA:
        WN
        GD
;

        LD HL,WASZXRES
        LD DE,ZXRESIDENT
        LD BC,LENZXRES
        LDIR
        LD HL,WASIMER
        LD DE,IMER
        LD BC,LENIMER
        LDIR
        LD A,'IMVEC
        LD I,A
        LD H,A,D,A
        LD BC,256
        LD L,C,E,B
        LD (HL),IMER
        LDIR
        IM 2

        LD A,pgmusic
        setpga ;заполняет curpg
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

       MACRO MOVEPAGE
        LD A,(curpg)
        LD HX,A
        LD LX,\0
        LD HL,\1
        LD DE,\3
        LD HY,\2/256
        JP RESMOVEPAGE
       ENDM

GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

CL1800PG
        setpga
        LD HL,#C000
        CALL CL1800
        LD HL,#E000
CL1800
        LD D,H,E,L
        INC E
        LD BC,#17FF
        LD (HL),L
        LDIR
        RET

WASGSPROG
        DISP GSPROG
GSPROGGO
        DI
        LD SP,GSSTACK

        XOR A
        OUT (VOL1),A
        OUT (VOL2),A
        OUT (VOL3),A
        OUT (VOL4),A
        OUT (VOL5),A
        OUT (VOL6),A
        OUT (VOL7),A
        OUT (VOL8),A

        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO
        OR C_24MHZ|M_NOROM|M_EXPAG
        OUT (GSCFG0),A

        LD A,1 ;#8000: gspg1:0
        OUT (MPAG),A
        LD A,2 ;#C000: gspg2:0
        OUT (MPAGEX),A

        LD A,1    ;SET DMA MODULE #1
        OUT (DMA_MOD),A

        LD E,3
       ;LD A,E
       ;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JNZ $-3

        LD A,gspgdata
        RRCA
        LD D,A
        AND #C0
        LD H,A
        XOR D
        OUT (DMA_HAD),A
        LD A,H
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80 ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"

        LD E,4
       ;LD A,E
       ;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JNZ $-3

        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        LD A,gspgcode
        RRCA
        LD D,A
        AND #C0
        LD H,A
        XOR D
        OUT (DMA_HAD),A
        LD A,H
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80 ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"

        LD E,5
       ;LD A,E
       ;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JNZ $-3

        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
       IN A,(ZXSTAT)
       RLCA
       JC $-3 ;ZX готов?

        LD A,gspgcode
       ;JP GSRESIDENTGO

GSRESIDENTGO
GSRES0
        OUT (MPAGEX),A
        PUSH AF
        CALL #C000
        POP AF
        RLCA
        INC A
        RRCA
        JP GSRES0

       ENT
LENGSPROG=$-WASGSPROG
       DISPLAY /T,LENGSPROG

WASZXRES
       DISP ZXRESIDENT
ZXRESIDENTGO
ZXRES0
        OR #10
        setpga
        PUSH AF
        LD C,(HL)
        INC HL
        LD B,(HL) ;время начала эффекта
        INC HL
        LD E,(HL)
        INC HL
        LD D,(HL) ;время конца эффекта
        INC HL
        PUSH HL
        CALL #C000
        POP HL
        POP AF
        INC A
        JP ZXRES0
RESMOVEPAGE
MOVEPAGE0
       PUSH HL
       PUSH DE
        LD DE,resbuf
       PUSH DE
        LD A,HX
        setpga
        LD BC,256
        LDIR
       POP HL
       POP DE
        LD A,LX
        setpga ;портит BC
        LD BC,256
        LDIR
       POP HL
        INC H
        DEC HY
        JNZ MOVEPAGE0
        RET
zxtimings
        DW 896,896*2-20 ;rotate
       ENT
LENZXRES=$-WASZXRES

WASIMER
       DISP IMER
        EX DE,HL
        EX (SP),HL ;пишем DE вместо адреса возврата
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE ;hl до прерывания
        PUSH AF,BC
        EXA
        PUSH AF
       ;EXX
       ;PUSH BC,DE,HL
       ;PUSH IX,IY
        LD HL,(timer)
        INC HL
        LD (timer),HL
        LD A,(curpg)
        AND #18
        OR pgmusic
        LD BC,#7FFD
        OUT (C),A
        CALL #C005
        LD A,(curpg)
        LD BC,#7FFD
        OUT (C),A
       ;POP IY,IX
       ;POP HL,DE,BC
       ;EXX
        POP AF
        EXA
        POP BC,AF
        POP HL
outimsp=$+1
        LD SP,0
        POP DE
        EI
outimjp=$+1
        JP 0
       ENT
LENIMER=$-WASIMER
end

        ORG #C000,pgcode1
       pgrotpregs=pgcode1
        LD HL,WASGSROTPRECODE
        LD DE,GSROTPRECODE
        LD BC,LENGSROTPRECODE
        LDIR

        LD HL,wasscr
        LD DE,#4000
        LD BC,#1000
        LDIR
        JP GSROTPREGO

WASGSROTPRECODE
       DISP GSROTPRECODE
GSROTPREGO
       IFN genldi
        LD HL,GSLDI256
        LD B,0
MKLDI0LD (HL),#ED
        INC HL
        LD (HL),#A0 ;ldi
        INC HL
        DJNZ MKLDI0
        LD (HL),201 ;ret
       ENDIF

;делает из экрана сдвинутые копии
;чтобы HL=YX
GENMAP
        LD A,GSpgrlscr
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD DE,#FF00
        LD HL,#4000
        LD B,128
GENMAP0
        PUSH HL
        LD A,L
        ADD A,31
        LD L,A
        LD A,(HL)
        POP HL
        PUSH HL
GENMAP1
       ;A=байт левее (картинка будет сдвинута на 8 вправо)
        LD C,(HL)
        INC L
       DUP 8
        LD (DE),A
        RL C
        RLA
        INC E
       EDUP
        JNZ GENMAP1
        POP HL
        CALL GSDHL
        DEC D
        DJNZ GENMAP0
        LD HL,GSROTTAB
        LD HX,GSpgcopy1
        LD B,8
GRCOP0PUSH BC
        CALL GENROTCOPY
        POP BC
        DJNZ GRCOP0
        LD HL,GSTMIRROR
GENMIR0LD B,8
        RLC L
        RRA
        DJNZ $-3
        LD (HL),A
        INC L
        JNZ GENMIR0
        LD LX,GSpgcopy1
        LD B,8
GRMIR0PUSH BC
        CALL GENROTMIRROR
        POP BC
        DJNZ GRMIR0
        RET

GENROTCOPY
        EXX
       IFN zx
        LD DE,#C000
       ELSE
        LD DE,#8000
       ENDIF
        EXX
       IFN zx
        LD B,#40
       ELSE
        LD B,#80
       ENDIF
GENROTLINES
       PUSH BC
       PUSH HL
        LD A,GSpgrlscr
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD B,8
GENROTBITS
        LD E,(HL)
        INC HL
        LD A,(HL)
        EXX
        ADD A,D
        EXX
        LD D,A
        INC HL
       PUSH HL
        SET 7,D
       IFN zx
        SET 6,D
       ENDIF
;копируем 7-е биты в буфер
        LD HL,GSGENROTBUF
GENROT1
       DUP 8
        LD A,(DE)
        RLA
        RL (HL)
        INC E
        INC L
       EDUP
        JP NZ,GENROT1
       POP HL
        DJNZ GENROTBITS
        LD A,HX
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        EXX
        LD HL,GSGENROTBUF
        CALL GSLDI256
        EXX
       POP HL
       POP BC
        DJNZ GENROTLINES
        LD C,16
        ADD HL,BC ;HL = следующая таблица координат
        INC HX
        RET

GENROTMIRROR
       IFN zx
        LD B,#40
       ELSE
        LD B,#80
       ENDIF
       IFN zx
        LD DE,#C000
       ELSE
        LD DE,#8000
       ENDIF
GENMIRLINES
       PUSH BC
        LD A,LX
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        EXD
        LD B,'GSTMIRROR
        LD DE,GSGENROTBUF
GENMIR1LD C,(HL)
        LD A,(BC)
        LD (DE),A
        INC L,E
        JNZ GENMIR1
        EXD
        LD A,HX
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD HL,GSGENROTBUF
        CALL GSLDI256
       POP BC
        DJNZ GENMIRLINES
        INC HX,LX
        RET

       IF0 genldi
GSLDI256
        DUP 256
        LDI
        EDUP
        RET
       ENDIF

GSROTTAB
;координаты битов в порядке 7..0
;
;
;
;        . 2 1 0
;7 6 5 4
;
;
;
        DW #01FC
        DW #01FD
        DW #01FE
        DW #01FF
        DW #0000
        DW #0001
        DW #0002
        DW #0003
;
;
;          1 0
;        . 2
;    5 4
;  7 6
;
;
        DW #02FD
        DW #02FE
        DW #01FE
        DW #01FF
        DW #0000
        DW #0001
        DW #FF01
        DW #FF02
;
;          0
;        2 1
;        .
;      4
;    6 5
;    7
;
        DW #03FE
        DW #02FE
        DW #02FF
        DW #01FF
        DW #0000
        DW #FF00
        DW #FF01
        DW #FE01
;        0
;        1
;        2
;        .
;      4
;      5
;      6
;      7
        DW #04FF
        DW #03FF
        DW #02FF
        DW #01FF
        DW #0000
        DW #FF00
        DW #FE00
        DW #FD00
;      0
;      1
;      2
;      3 .
;        4
;        5
;        6
;        7
        DW #0400
        DW #0300
        DW #0200
        DW #0100
        DW #00FF
        DW #FFFF
        DW #FEFF
        DW #FDFF
;
;    0
;    1 2
;      3 .
;        4
;        5 6
;          7
;
        DW #0301
        DW #0201
        DW #0200
        DW #0100
        DW #00FF
        DW #FFFF
        DW #FFFE
        DW #FEFE
;
;
;  0 1
;    2 3 .
;        4 5
;          6 7
;
;
        DW #0202
        DW #0201
        DW #0101
        DW #0100
        DW #00FF
        DW #00FE
        DW #FFFE
        DW #FFFD
;
;
;
;0 1 2 3 .
;        4 5 6 7
;
;
;
        DW #0103
        DW #0102
        DW #0101
        DW #0100
        DW #00FF
        DW #00FE
        DW #00FD
        DW #00FC

GSDHL
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
       ENT
LENGSROTPRECODE=$-WASGSROTPRECODE
       DISPLAY /T,LENGSROTPRECODE
wasscr
        INCBIN "titletex",#1000
LENROTPREGS=$-#C000

        ORG #C000,pgcode2
       pgrotategs=pgcode2
        LD HL,WASGSROTATECODE
        LD DE,GSROTATECODE
        LD BC,LENGSROTATECODE
        LDIR

        LD HL,wasscr
        LD DE,#4000
        LD BC,#1000
        LDIR

        LD A,GSpglogo
        OUT (MPAG),A

        LD HL,waslogo
        LD DE,GSLOGO+#1B00
        LD BC,#1B00
GSGET1LD A,(HL)
        INC HL
        DEC DE
        LD (DE),A
        DEC BC
        LD A,B
        OR C
        JNZ GSGET1
        LD HL,GSLOGO
        LD DE,GSSCR1+#1800
        LD BC,768
        LDIR
        LD HL,GSLOGO
        LD DE,GSSCR2+#1800
        LD BC,768
        LDIR
        JP GSROTATEGO

WASGSROTATECODE
       DISP GSROTATECODE
       IFN counter
GSDIGGFX
_=#3D80
        DUP 80
        DB {_}
_=_+1
        EDUP
       ENDIF

GSROTATEGO
        LD A,GSpglogo
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A

        LD DE,GSLOGO+#300
        LD HL,GSLOGOPROG
        LD IX,GSSCR1-(GSLOGO+#300)+#C0
        CALL MKLOGO
        LD (gslogoprog2),HL
        LD DE,GSLOGO+#300
        LD IX,GSSCR2-(GSLOGO+#300)+#C0
        CALL MKLOGO

        LD HL,GSTDHL
        LD DE,GSSCR1+#1F
        CALL GENDHL
        LD DE,GSSCR2+#1F
        CALL GENDHL
GSLOOP
gsdrawscr=$+1
        LD A,1 ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A ;теперь рисуем в SCR1
                         ;потом дадим его ZX'у
        CALL GSEFFECT

        LD A,GSpglogo
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD A,(gsdrawscr)
        OR A
        JNZ GSLOOPLOGO2
        CALL GSLOGOPROG
        JR GSLOOPLOGOOK
GSLOOPLOGO2
gslogoprog2=$+1
        CALL 0
GSLOOPLOGOOK
;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0ADD HL,DE
       ;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JNZ GSWAIT0 ;48t
       IFN counter
        LD A,(gsdrawscr)
        OR A
        LD DE,GSSCR1+#17FF
        JZ $+4
        LD D,'GSSCR2+#17
        CALL GS12345
       IFN dump
        LD HL,GSLDI256
        LD B,23
        CALL DUMPPP
       ENDIF
       ENDIF

        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
;GS ожидает обнуления (ZX запросит данные)
        LD HL,0
        LD DE,1
GSWAIT1ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JZ GSWAIT1
       DEC A
       RET NZ ;QUIT

        XOR A
        OUT (DMA_HAD),A
        LD A,(gsdrawscr)
        OR A
        LD A,'GSSCR1
        JZ $+4
        LD A,'GSSCR2
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80 ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"
        JP GSLOOP

GENDHL
        LD B,192
GENDHL0INC DE
        LD (HL),E
        INC L
        LD (HL),D
        INC HL
        DEC DE
        INC D
        LD A,D
        AND 7
        JNZ GENDHLN
        LD A,E
        ADD A,32
        LD E,A
        JC GENDHLN
        LD A,D
        SUB 8
        LD D,A
GENDHLNDJNZ GENDHL0
        RET

MKLOGO
;DE=образец экрана
;HX=смещение рабочего экрана относительно DE
;LX=высота
;если атрибут 7, то все 0 затереть (байт из лого прямо в AND)
;иначе прямо пишем байты
MKLOGOLINE
;1. находим начало линии
        LD HY,32 ;начало линии (32=нету)
        LD A,D
        RRA
        RRA
        RRA
        AND 3
        ADD A,'GSLOGO
        LD B,A,C,E
MKLOGOMM0
        LD A,(BC) ;attr
        CP 7
        JNZ MKLOGOMMnMASK
        LD A,(DE) ;pix
        INC A
        JZ MKLOGOMMEMPTY
MKLOGOMMnMASK
        LD A,C
        AND #1F
        CP HY
        JNC $+4
        LD HY,A
MKLOGOMMEMPTY
        INC DE,C
        LD A,C
        AND #1F
        JNZ MKLOGOMM0
        LD A,32
        SUB HY
        LD B,A
        JZ MKLOGOLINEQ
;2. если линия есть, то генерим LD HL,.. и заполнение
        LD A,E
        SUB B
        LD E,A
        JNC $+3
        DEC D
        LD (HL),33
        INC HL
        LD (HL),E
        INC HL
        LD A,D
        ADD A,HX
        LD (HL),A
        INC HL
MKLOGO1PUSH DE
        LD A,D
        RRA
        RRA
        RRA
        AND 3
        ADD A,'GSLOGO
        LD D,A
        LD A,(DE)
        CP 7
        POP DE
        LD A,(DE)
        INC DE
        JNZ MLOGnMASK
        CP #FF
        JZ MLOGNO
       ;можно ускорить через RES
        LD (HL),"~" ;ld a,(hl)
        INC HL
        LD (HL),230 ;and N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),"w" ;ld (hl),a
        INC HL
        JR MLOGPOK
MLOGnMASK
        LD (HL),54  ;ld (hl),N
        INC HL
        LD (HL),A
        INC HL
MLOGPOKLD (MLOGHL),HL
MLOGNOLD (HL),"," ;inc l
        INC HL
        DJNZ MKLOGO1
MLOGHL=$+1
        LD HL,0 ;после последнего записанного байта
MKLOGOLINEQ
;DE=следующая строка (в нелинейном порядке)
        DEC LX
        JR NZ,MKLOGOLINE
        LD (HL),201
        INC HL
        RET

GSEFFECT
        LD HL,GSangle
        INC (HL)
        LD A,(HL)
        RRA
        RRA
        RRA
        RRA
        CPL
       ;ADD A,8
        AND 15
        ADD A,GSpgcopy1
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
GSangle=$+1
        LD HL,GSTSIN
        LD E,(HL) ;sin
        INC H
        LD D,(HL)
        LD A,64
        SUB L
        LD L,A
        LD B,(HL) ;cos
        DEC H
        LD C,(HL)
       DUP 4
        SRA D
        RR E ;dY
        SRA B
        RR C ;dX
       EDUP
        EXX

        LD LX,#80
        LD LY,#80
        CALL MKROT

GSmovephase=$+1
        LD HL,GSTSIN
        INC L,L,L
        LD (GSmovephase),HL
        LD E,(HL)
        INC H
        LD D,(HL)
       DUP 4
        SRA D
        RR E
       EDUP
        LD HL,500
        ADD HL,DE
        LD DE,(CENTERX)
        ADD HL,DE
        LD (CENTERX),HL

        LD HL,(GSangle)
        LD A,L
        ADD A,128
        LD L,A
        LD E,(HL) ;-sin
        INC H
        LD D,(HL)
        LD A,64+128
        SUB L
        LD L,A
        LD B,(HL) ;cos
        DEC H
        LD C,(HL)
       PUSH BC
       PUSH DE
        LD A,B
        RLA
        SBC A,A
        SLA C
        RL B
        RLA
        LD C,B,B,A
        LD A,D
        RLA
        SBC A,A
        SLA E
        RL D
        RLA
        LD E,D,D,A
        EXX
       POP DE
       POP BC
;коррекция центра
      DUP 2
       SRA D
       RR E
       SRA B
       RR C
      EDUP
CENTERX=$+1
        LD HL,#80 ;X
       SBC HL,DE
       SBC HL,DE
       SBC HL,DE
        SBC HL,BC
        SBC HL,BC
        SBC HL,BC
        SBC HL,BC
        PUSH HL
        POP IX
CENTERY=$+1
        LD HL,#80 ;Y
       SBC HL,BC
       SBC HL,BC
       SBC HL,BC
        ADD HL,DE
        ADD HL,DE
        ADD HL,DE
        ADD HL,DE
        PUSH HL
        POP IY

        LD (GSEFFECTSP),SP
       IFN zx
        LD C,#C0
       ELSE
        LD C,#80
       ENDIF
        EXX
        LD A,(gsdrawscr)
        OR A
        LD HL,GSTDHL
        JZ $+5
        LD HL,GSTDHL2
        LD A,192
ROT0EXA
        LD SP,HL
        EXX
        POP HL
        LD SP,HL
        LD A,HX
        LD L,A
        LD A,HY
        OR C
        LD H,A ;HL=YX
        JP GSGENPROG
GSROTOKEXX
        ADD IX,DE
        ADD IY,BC
        INC L,HL
        EXA
        DEC A
        JP NZ,ROT0
GSEFFECTSP=$+1
        LD SP,0
        RET

;генератор
;LD D/E,(HL)
;[PUSH DE]
;[LD A,L:SUB N:LD L,A]
;[LD A,H:SUB N:OR C:LD H,A]
;...
;JP (IX)
;генератор ест 9275 тактов
MKROT
;IX=Xx
;BC'=dXx
;IY=Yy
;DE'=dYy
        LD HL,GSGENPROG
        LD B,32
MKR0
        BIT 0,B
        LD (HL),#56 ;ld d,(hl)
        JZ $+7
         LD (HL),#5E ;ld e,(hl)
         INC HL
         LD (HL),#D5 ;push de
        INC HL
        LD (mkrENDHL),HL
        LD E,HY
        LD A,HX
        EXX
        ADD IX,BC
        ADD IY,DE
        EXX
        SUB HX
        LD (HL),#7D ;ld a,l
        INC HL
        LD (HL),#D6 ;sub N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),#6F ;ld l,a
        INC HL
        LD A,E
        SUB HY
        LD (HL),#7C ;ld a,h
        INC HL
        LD (HL),#D6 ;sub N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),#B1 ;or c
        INC HL
        LD (HL),#67 ;ld h,a
        INC HL
        DJNZ MKR0
mkrENDHL=$+1
        LD HL,0
        LD (HL),#C3
        INC HL
        LD (HL),.GSROTOK
        INC HL
        LD (HL),'GSROTOK
       ;INC HL
        RET

       IFN counter
GS12345
        LD BC,10000
        CALL GSDIG
        LD BC,1000
        CALL GSDIG
GS123
        LD BC,100
        CALL GSDIG
        LD BC,10
        CALL GSDIG
        LD BC,1
GSDIG
        XOR A
        ADD A,8
        SBC HL,BC
        JNC $-4
        ADD HL,BC
        SUB 8-GSDIGGFX
       PUSH DE,HL
        LD L,A
        LD H,'GSDIGGFX
        LD B,8
GSDIG0LD A,(DE)
        XOR (HL)
        LD (DE),A
        INC L
        DEC D
        DJNZ GSDIG0
       POP HL,DE
        DEC E
        RET
       IFN dump
DUMPPP
DUMP0
        LD A,E
        SUB 32
        LD E,A
        JNC $+6
        LD A,D
        SUB 8
        LD D,A
       PUSH BC,DE,HL
        PUSH HL
        CALL GS12345
        DEC E
        POP HL
        LD L,(HL)
        LD H,0
        CALL GS123
       POP HL,DE,BC
        INC HL
        DJNZ DUMP0
        RET
       ENDIF
       ENDIF

GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

       DISPLAY $
        DS .(-$)
GSTSIN
        INCBIN "sin32768"
        ENT
LENGSROTATECODE=$-WASGSROTATECODE
       DISPLAY /T,LENGSROTATECODE
waslogo
        INCBIN "thelink$"
LENROTATEGS=$-#C000

        ORG #C000,pgeffects
       pgrotatezx=pgeffects
       PUSH BC
       PUSH DE
        LD HL,WASROTATECODE
        LD DE,ROTATECODE
        LD BC,LENROTATECODE
        LDIR
        JP ROTATECODE

WASROTATECODE
       DISP ROTATECODE
       POP HL
       LD (zxendtime),HL
        HALT

       POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JC ZXWAITSTART

        LD A,#17
        setpga
        HALT
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        LD HL,#D800
        LD DE,#D801
        LD BC,767
        LD (HL),L
        LDIR

        XOR A ;turbo
        LD BC,#EFF7
        OUT (C),A

BIGLOOP
        HALT
       IFN border
        XOR A
        OUT (254),A
       ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

        LD A,#1D!#17
        XOR #1D ;видим scr1, рисуем scr0
        LD ($-1),A
        setpga
        ;теперь видим scr0, рисуем scr1

;и ждем выключения:
        WN
        GD
;
zxendtime=$+1
        LD DE,0
        LD HL,(timer)
        OR A
        SBC HL,DE
        JP NC,QUIT
        LD A,#7F
        IN A,(#FE)
        RRA ;space
        JP NC,QUIT ;можно жать RESET
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
        WN
        GD
;
        DI
        LD (PUSHKASP),SP
        LD H,0
        LD A,(HL) ;1ST BYTE DISCARDED
        LD B,128
        LD SP,#D800
PUSHKA0
       DUP #18
        LD D,(HL)
        LD E,(HL)
        PUSH DE
       EDUP
        DJNZ PUSHKA0
        LD B,24
        LD SP,#DB00
PUSHKA1
       DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
       EDUP
        DJNZ PUSHKA1
PUSHKASP=$+1
        LD SP,0
        EI
       IFN border
        LD A,R
        OUT (254),A
       ENDIF
        JP BIGLOOP
QUIT
        LD A,2
        OUT (GSCOM),A ;конец эффекта
        HALT
        LD A,#10
        setpga
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        LD BC,#EFF7
        XOR A
        OUT (C),A
        RET
       ;LD A,C_GRST
       ;OUT (GSCTR),A
       ;JR $
       ENT
LENROTATECODE=$-WASROTATECODE
LENROTATEZX=$-#C000

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "ROTATEZXC"
        DW #C000
        DW LENROTATEZX
        DB pgrotatezx
        DW #C000

        DB "ROTPREGSC"
        DW #C000
        DW LENROTPREGS
        DB pgrotpregs
        DW #C000

        DB "ROTATEGSC"
        DW #C000
        DW LENROTATEGS
        DB pgrotategs
        DW #C000
        INCLUDE "SAVEOBJ*",#C0

