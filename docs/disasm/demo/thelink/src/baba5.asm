border=0

;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

BABACODE=#6000

resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports",#C7
GSPROG=#5830
GSSTACK=#5830
GSBABACODE=#5900

gspgcode=#10
gspgdata=#A0;#20

gspgscr0=2 ;32K
gspgscr1=3 ;32K
 gsscr16=#8000 ;2 экрана по 2 страницы
gspgtex=#A0 ;2*32K
gstmask=#0100 ;256

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
        LD A,pgdata2
        CALL GSDMASENDPG
        LD A,pgdata3
        CALL GSDMASENDPG
        LD A,pgdata4
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
        DW 8,8+420 ;baba
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
         pgbabags=pgcode1
        LD HL,WASGSBABACODE
        LD DE,GSBABACODE
        LD BC,LENGSBABACODE
        LDIR

        JP GSBABAGO

WASGSBABACODE
       DISP GSBABACODE
GSBABAGO
;генератор таблицы маски для спрайтов
;%RLRRRLLL
;маска=0
;если L=0, то маска|=%01000111
;если R=0, то маска|=%10111000
        LD HL,gstmask
MKMASK0LD A,L
        LD C,%01000111
        AND C
        JZ $+4
        LD C,0
        XOR L
        LD A,C
        JNZ $+4
        OR %10111000
        LD (HL),A
        INC L
        JR NZ,MKMASK0

GSLOOP
gsdrawscr=$+1
        LD A,1 ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A ;теперь рисуем в SCR1
                         ;потом дадим его ZX'у

        LD A,%10000000 ;не бывает в картинке
        LD (0),A

       MACRO GSSETSCRPG1
        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0
        JZ $+4
        LD A,gspgscr1
        OUT (MPAG),A
       ENDM
       MACRO GSSETSCRPG2
        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0+#80
        JZ $+4
        LD A,gspgscr1+#80
        OUT (MPAG),A
       ENDM

        LD (GSLOOPSP),SP

        GSSETSCRPG1

        LD A,gspgtex+1
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD HY,A

gsbgy=$+1
        LD HL,235-192*128+#C000 ;начало фона
        LD A,L
        AND #80
        LD L,A
        LD DE,128
        LD A,0
gsbgscaley=$+1
        LD C,0
        EXA
        EXX
gsbgscalex=$+1
        LD C,0
       LD L,C,H,0
       ADD HL,HL
       ADD HL,HL
       ADD HL,HL
       ADD HL,HL
       ADD HL,HL
       LD A,H
       EXX
       ADD A,L
       LD L,A
       EXX
       LD A,L
       CPL
        LD H,#80 ;куда пишем
        LD B,64
        LD IX,GSLOOPDBOTQ
        JP DRAWLINEBOTTOM
GSLOOPDBOTQ
       LD B,A
        GSSETSCRPG2
       LD A,B
        LD H,#80 ;куда пишем
        LD B,32
        LD IX,GSLOOPDBOTQ2
        JP DRAWLINEBOTTOM
GSLOOPDBOTQ2
       LD B,A
        GSSETSCRPG1
       LD A,B
        LD H,#80 ;куда пишем
        LD B,64
        LD IX,GSLOOPDTOPQ
        JP DRAWLINETOP
GSLOOPDTOPQ
       LD B,A
        GSSETSCRPG2
       LD A,B
        LD H,#80 ;куда пишем
        LD B,32
        LD IX,GSLOOPDTOPQ2
        JP DRAWLINETOP
GSLOOPDTOPQ2

        GSSETSCRPG1

        LD A,gspgtex
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD HY,A
gsfgy=$+1
        LD HL,#EB00 ;начало спрайта
        LD A,L
        AND #80
        LD L,A
       LD SP,HL
        LD A,0
        LD C,0
        EXA
        EXX
        LD A,0
        LD C,0
        LD B,'gstmask
        LD H,#80 ;куда пишем
        LD LY,64
        LD IX,GSLOOPBBOTQ
        JP BLITLINEBOTTOM
GSLOOPBBOTQ
       LD H,A
        GSSETSCRPG2
       LD A,H
        LD H,#80 ;куда пишем
        LD LY,32
        LD IX,GSLOOPBBOTQ2
        JP BLITLINEBOTTOM
GSLOOPBBOTQ2
       LD H,A
        GSSETSCRPG1
       LD A,H
        LD H,#80 ;куда пишем
        LD LY,64
        LD IX,GSLOOPBTOPQ
        JP BLITLINETOP
GSLOOPBTOPQ
       LD H,A
        GSSETSCRPG2
       LD A,H
        LD H,#80 ;куда пишем
        LD LY,32
        LD IX,GSLOOPBTOPQ2
       LD A,(gsfgy+1)
       CP #D8
       JP C,BLITLINETOP
GSLOOPBTOPQ2

GSLOOPSP=$+1
        LD SP,0

;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0ADD HL,DE
       ;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JNZ GSWAIT0 ;48t
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A ;после этого ZX можно сбрасывать
       IN A,(ZXDATRD)
       LD (gstimer),A
       ;OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
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

        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0
        JZ $+4
        LD A,gspgscr1
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

gstimer=$+1
        LD A,0
gsoldtimer=$+1
        LD C,0
        LD (gsoldtimer),A
        SUB C
        JZ GSTIMERACTIONQ
       CP 5
       JC $+4
       LD A,5
        LD B,A
GSTIMERACTION
        LD HL,(gsfgy)
        LD DE,-64;-256
        ADD HL,DE
       BIT 6,H
       JZ $+5
        LD (gsfgy),HL
        LD HL,(gsbgy)
        LD DE,-32;-128
        ADD HL,DE
       BIT 6,H
       JZ $+5
        LD (gsbgy),HL
        LD DE,170
        OR A
        LD HL,#7000
        SBC HL,DE
        JC GSnscale
        LD ($-6),HL
        LD A,H
        LD (gsbgscaley),A
        ADD A,A
        LD (gsbgscalex),A
GSnscale
        DJNZ GSTIMERACTION
GSTIMERACTIONQ
        JP GSLOOP

GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

       MACRO DRAWLINE
       LOCAL
        EXX
        EXA
        ADD A,C
        JC _NPAG\0
        ADD HL,DE
        JNC _NPAG\0
        LD B,A
        LD A,HY
        OUT (MPAGEX),A
        LD A,H
        ADD A,#C0
        LD H,A
        LD A,B
_NPAG\0
        EXA
        LD SP,HL
        EXX
       DUP 32
        POP DE
       ADD A,C
       JNC $+3
       DEC SP
        LD L,_+#00
        LD (HL),E
        LD L,_+#80
        LD (HL),D
        POP DE
       ADD A,C
       JNC $+3
       DEC SP
        LD L,_+#40
        LD (HL),E
        LD L,_+#C0
        LD (HL),D
_=_-1
       EDUP
       ENDL
       ENDM
DRAWLINEBOTTOM
_=31
        DRAWLINE 0
        INC H
        DEC B
        JP NZ,DRAWLINEBOTTOM
        JP (IX)
DRAWLINETOP
_=31+32
        DRAWLINE 1
        INC H
        DEC B
        JP NZ,DRAWLINETOP
        JP (IX)

       MACRO BLITLINE
__=0
       DUP 32
        POP DE
        IF0 __
        LD A,E
        CP %10000000 ;(#0000)?
        JNZ $+6+3+1
        LD A,HY
        OUT (MPAGEX),A
        LD SP,#C000
        POP DE
        ENDIF
__=1
        LD C,E
        LD A,(BC)
        LD L,_+#00
        AND (HL)
        OR C
        LD (HL),A
        LD C,D
        LD A,(BC)
        LD L,_+#80
        AND (HL)
        OR C
        LD (HL),A
        POP DE
        LD C,E
        LD A,(BC)
        LD L,_+#40
        AND (HL)
        OR C
        LD (HL),A
        LD C,D
        LD A,(BC)
        LD L,_+#C0
        AND (HL)
        OR C
        LD (HL),A
_=_-1
       EDUP
       ENDM
BLITLINEBOTTOM
_=31
        BLITLINE
        INC H
        DEC LY
        JP NZ,BLITLINEBOTTOM
        JP (IX)
BLITLINETOP
_=31+32
        BLITLINE
        INC H
        DEC LY
        JP NZ,BLITLINETOP
        JP (IX)

        DISPLAY "с инсталлерами конец кода=",$
       ENT
LENGSBABACODE=$-WASGSBABACODE
       DISPLAY /T,LENGSBABACODE
LENBABAGS=$-#C000

        ORG #C000,pgeffects
         pgbabazx=pgeffects
       PUSH BC
       PUSH DE
        LD HL,WASBABACODE
        LD DE,BABACODE
        LD BC,LENBABACODE
        LDIR
        JP BABACODE

WASBABACODE
       DISP BABACODE
       POP HL
       LD (zxendtime),HL
        HALT

       POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JC ZXWAITSTART

;рисовать будем только на scr1 (pg6,7)
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
        HALT
        LD A,#1E
        setpga
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

        LD A,#1F
        setpga
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

        LD HL,OUT16C
        CALL MKOUT16C

        LD A,1 ;16C
        LD BC,#EFF7
        OUT (C),A

BIGLOOP
       ;HALT
       IFN border
        XOR A
        OUT (254),A
       ENDIF
        LD A,(timer)
        OUT (GSDAT),A
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

;и ждем выключения:
       ;WN
       ;GD
        WD ;пока не примет
;
zxendtime=$+1
        LD DE,0
        LD HL,(timer)
        OR A
        SBC HL,DE
        JNC QUIT
        LD A,#7F
        IN A,(#FE)
        RRA ;space
        JNC QUIT ;можно жать RESET
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
        WN
        GD
;
       ;DI
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
        LD BC,#7FFD
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1E1F
        LD A,(HL) ;первый байт неверен
        JP OUT16C
BIGLOOPVIEWQ
BIGLOOPSP=$+1
        LD SP,0
       ;EI
       IFN border
        LD A,7
        OUT (254),A
       ENDIF
        JP BIGLOOP
QUIT
        LD A,2
        OUT (GSCOM),A ;конец эффекта
        HALT
        LD BC,#EFF7
        XOR A
        OUT (C),A
        LD A,#10
        setpga
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        RET
       ;LD A,C_GRST
       ;OUT (GSCTR),A
       ;JR $

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
;19K кода для 256X192
       ;LD A,H
;LOOP
      ;LD (curpg),A
       ;OUT (C),A
       ;LD SP,низ
       ;DUP 16
       ;LD D,(HL)
       ;LD E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,верх
       ;DUP 16
       ;LD D,(HL)
       ;LD E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,низ+#2000
       ;DUP 16
       ;LD D,(HL)
       ;LD E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,верх+#2000
       ;DUP 16
       ;LD D,(HL)
       ;LD E,(HL)
       ;PUSH DE
       ;EDUP
       ;CP H
       ;LD A,L
       ;JP Z,LOOP
        LD DE,#D7E0
        LD B,96 ;высота 96*2
MKOUT16CLINE
       PUSH BC
        LD (HL),#7C ;ld a,h
        INC HL
        LD (mkout16cloop),HL
       LD (HL),#32 ;ld (),a
       INC HL
       LD (HL),curpg
       INC HL
       LD (HL),'curpg
       INC HL
        LD (HL),#ED
        INC HL
        LD (HL),#79 ;out (c),a
        INC HL
       PUSH DE
       PUSH DE
        CALL MKOUT16C0
       ;на 96 пикс выше
        LD A,E
        SUB #80
        LD E,A
        LD A,-8
        JNC $+3
        ADD A,A
        ADD A,D
        LD D,A
        CALL MKOUT16C0
       POP DE
        SET 5,D
        CALL MKOUT16C0
       ;на 96 пикс выше
        LD A,E
        SUB #80
        LD E,A
        LD A,-8
        JNC $+3
        ADD A,A
        ADD A,D
        LD D,A
        CALL MKOUT16C0
       POP DE
        LD (HL),#BC ;cp h
        INC HL
        LD (HL),#7D ;ld a,l
        INC HL
        LD (HL),#CA ;jp z
        INC HL
mkout16cloop=$+1
        LD BC,0
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD A,D
        DEC D
        AND 7
        JNZ MKOUT16CUDEQ
        LD A,E
        SUB 32
        LD E,A
        JC MKOUT16CUDEQ
        LD A,D
        ADD A,8
        LD D,A
MKOUT16CUDEQ
       POP BC
        DJNZ MKOUT16CLINE
        LD (HL),#DD
        INC HL
        LD (HL),#E9 ;jp (ix)
        RET

MKOUT16C0
        LD (HL),49
        INC HL
        LD A,E
        ADD A,32
        LD (HL),A
        ADC A,D
        SUB (HL)
        INC HL
        LD (HL),A
        LD B,16
MKOUT16C1
        INC HL
        LD (HL),#56 ;d,(hl)
        INC HL
        LD (HL),#5E ;e,(hl)
        INC HL
        LD (HL),#D5 ;push de
        DJNZ MKOUT16C1
;у посл. линий третей надо LD (..),DE вместо последнего PUSH
        LD A,E
        OR A
        JNZ MKOUT16CnPROTECT
        LD (HL),#ED
        INC HL
        LD (HL),#53 ;(),de
        INC HL
        LD (HL),E
        INC HL
        LD (HL),D
MKOUT16CnPROTECT
        INC HL
        RET
OUT16C
       ENT
LENBABACODE=$-WASBABACODE
LENBABAZX=$-#C000

        ORG #C000,pgdata1
        INCBIN "baba.C"
        ORG #C000,pgdata2
        INCBIN "baba.0"
        DS -$ ;для маски
        ORG #C000,pgdata3
        INCBIN "mill.C"
        ORG #C000,pgdata4
        INCBIN "mill.0"

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "BABAZX  C"
        DW #C000
        DW LENBABAZX
        DB pgbabazx
        DW #C000

        DB "BABAGS  C"
        DW #C000
        DW LENBABAGS
        DB pgbabags
        DW #C000
        INCLUDE "SAVEOBJ*",#C0


