border=0
mkballplasma=0
debug=0 ;1=проверка отрицательной ширины

;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

TEXCODE=#7800

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
GSTEXCODE=#5900

gspgcode=#10
gspgdata=#A2
gspgtex=gspgdata
gspgballtex1=gspgdata*257<1+#101>1&#FF ;8 страниц

texdec=1 ;с заворотом
gschmap256=#0000
gschmaphgt=64 ;с заворотом в 2 раза больше
gschscr=#8000 ;2 страницы - 2 экрана
TPOLYZ=#4000
TPOLYS=TPOLYZ+#100 ;#200 (L,H)
;исходные координаты:
VTX3DI=#4300 ;,#300
VTX3DIH='VTX3DI
;координаты после поворота:
VTX3D=#4600 ;,#300
VTX3DH='VTX3D
VTX3DY=VTX3D+#100
VTX3DZ=VTX3D+#200

gspgtball=#85 ;временно
 GSTQUARTERBALL=#4000 ;временно
 GSTBALL=#C000 ;2*64*128=16K
gspgballer1=6 ;32K

gspgchscr0=2
gspgchscr1=3
gspgdiv=4 ;(L/2)/-H ;+-7.8=+-6.0/6.0
gspgpersp=#84 ;+-7.0=+-7.0/6.0
gspgsin=5
 SIGNOFS=#C000
 SINOFS=#C100
 COSOFS=#C200
 PROCROT=#C300
 MULTAB=#C400
gspgtransch=#85
 transch=#E000
 TCH256=#FE00

mirrorchunks=1 ;строки 0,1,3,2,4,5,7,6...
perspdist=32
trisortmesspop=1 ;1=сокращает код без потери скорости
incW=0 ;1=исключает выскакивание за текстуру, но дергается
texborder=1 ;размер запаса на краю текстуры, против выскакивания
texarea=63-texborder-texborder
minV=#C0+texborder
maxV=minV+texarea

trimaxh=63
trimaxl=52 ;ограничение TEXTURER'а ;ограничение расчётов=63
ntexjp=20
ntexjr=16
ntexfast=16

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
        LD A,pgdata5
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
        DW 224,224*4-20 ;tex
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

        ORG #C000,pgdata1
        INCBIN "girl13.2"
        ORG #C000,pgdata2
        INCBIN "blugr.C"
        ORG #C000,pgdata3
        INCBIN "blugr.0"
        ORG #C000,pgdata4
        INCBIN "blugr.1"
        ORG #C000,pgdata5
        INCBIN "blugr.2"

        ORG #C000,pgcode1
          pgtexgs=pgcode1
        LD HL,WASGSTEXCODE
        LD DE,GSTEXCODE
        LD BC,LENGSTEXCODE
        LDIR

        LD HL,wasuv
        LD DE,GSTQUARTERBALL
        LD BC,uvlen
        LDIR

        JP GSTEXGO

WASGSTEXCODE
       DISP GSTEXCODE
WAITER0
        LD HL,0
        LD DE,1
GSWAIT0ADD HL,DE
       ;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JNZ GSWAIT0
        RET

       MACRO MTEXTURER
        DS .(253-((ntexjp+ntexjr)*7)-$)
;(20+16)*7+3=255 байт кода для ширины ??..??
TEXTURER\0MAX
       DUP ntexjp+ntexjr
        LD HL,0
        ADD HL,BC
        LD A,(HL),(DE),A
        \0 E
       EDUP
        JP TEXTURER\0
;это круглый адрес
;1 байт адрес для ширины 0
        DB TEXTURER\0Q
;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125)
TEXTURER\0JR
_=ntexjp*7+TEXTURER\0MAX
       DUP ntexjr
        JR _
_=_+7
       EDUP
;20*3=60 байт jp для ширины ??..??
TEXTURER\0JP
_=TEXTURER\0MAX
       DUP ntexjp
        JP _
_=_+7
       EDUP
;16*7-4=108 байт кода для ширины 16..0
TEXTURER\0
       DUP ntexfast-1
        LD HL,0
        ADD HL,BC
        LD A,(HL),(DE),A
        \0 E
       EDUP
;смещение=0
        LD A,(BC),(DE),A
TEXTURER\0Q
        RET
        DS 3
;20+16+16=52 байт адресов
_=TEXTURER\0JP
       DUP ntexjp
        DB _
_=_+3
       EDUP
_=TEXTURER\0JR
       DUP ntexjr
        DB _
_=_+2
       EDUP
_=TEXTURER\0
       DUP ntexfast
        DB _
_=_+7
       EDUP
        DISPLAY $,"=#XX00"
;итого 256 байт
       ENDM

       MACRO PATCHVUCODE
PATCHVU\0
        _=\0SYSTEM+257+(ntexjr*2)+(ntexjp*3)+(ntexfast-1*7)-6
        _V=_
        __=\0SYSTEM+257+(ntexjr*2)+(ntexjp*3)
        ___=\0SYSTEM+253-(ntexjr+ntexjp*7)
       DUP trimaxl+3/4
        _U=_V
       DUP 4
        IF0 _V-___<1&1
        IFN _V-_<1&1
        ADD HL,DE ;пропускаем первый
        ENDIF
        LD (_V),HL
        _V=_V-7
        ENDIF
        IFN _V-__<1&1
        IF0 _V-\0SYSTEM-256<1&1
        _V=\0SYSTEM+253-6
        ENDIF
        ENDIF
       EDUP
        EXX
       DUP 4
       IF0 _U-___<1&1
        IF0 _U-___<1&1
        IFN _U-_<1&1
        ADD HL,DE ;пропускаем первый
        ENDIF
        LD A,H,(_U),A
        _U=_U-7
        ENDIF
        IFN _U-__<1&1
        IF0 _U-\0SYSTEM-256<1&1
        _U=\0SYSTEM+253-6
        ENDIF
        ENDIF
       ENDIF
       EDUP
        IF0 _U-___<1&1
        EXX
        INC B
        RET Z
        ENDIF
       EDUP
        RET
       ENDM
        DS .(-$)
INCSYSTEM
        MTEXTURER INC
        DISPLAY /T,TEXTURERINC
        PATCHVUCODE INC
        DISPLAY $
        DS .(-$)
DECSYSTEM
        MTEXTURER DEC
        DISPLAY /T,TEXTURERDEC
        PATCHVUCODE DEC
        DISPLAY $

GSTEXGO

        CALL MKBALLER
        CALL MKBALLTEX
        CALL GENDIV
        CALL GENPERSP
        CALL GENJTNROT
        CALL MKTRANSCH

        LD HL,KUB
        LD B,(HL)
        INC HL
        LD E,0
COPVERTO
        DEC E
        LD D,'VTX3DI
        LD A,(HL)
        LD (DE),A
        INC HL
        INC D
        LD A,(HL)
        LD (DE),A
        INC HL
        INC D
        LD A,(HL)
        LD (DE),A
        INC HL
        DJNZ COPVERTO
        LD A,E
        LD (VTX3DBEG),A

        LD A,(HL)
        INC HL
        LD (NPOLYS),A
        LD DE,TPOLYZ
        LD B,A
GENPOLYS0
        XOR A
        LD (DE),A
        INC D
        LD A,(HL)
        INC HL
        LD (DE),A
        INC D
        LD A,(HL)
        INC HL
        LD (DE),A
        DEC D,D
        INC E
        DJNZ GENPOLYS0
       ;LD A,#FF
       XOR A
        LD (DE),A

GSLOOP
gsdrawscr=$+1
        LD A,1 ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A ;теперь рисуем в SCR1
                         ;потом дадим его ZX'у
gstimer=$+1
        LD A,0
gsoldtimer=$+1
        LD C,0
        LD (gsoldtimer),A
        SUB C
       CP 8
       JC $+4
       LD A,8
        LD B,A
GSTIMERACTION
        LD D,'sin256
        LD HL,ALPHA
       LD A,0
       ADD A,3
       LD ($-3),A
        LD E,A
        LD A,(DE)
        SRA A
       LD (angle1),A
        SRA A
        SRA A
       LD (angle1corr),A
       LD (HL),A
        LD HL,BETA
       LD A,0
       ADD A,2
       LD ($-3),A
        LD E,A
        LD A,(DE)
        SRA A
       LD (angle2),A
        SRA A
        SRA A
       LD (HL),A
        LD HL,GAMMA
        LD (HL),70;0
        DJNZ GSTIMERACTION

        LD HL,curperspdist ;24..48
       IFN 1
       LD (HL),16
       ELSE
CPDi=$
        INC (HL)
        LD A,(HL)
        SUB 24
        CP 24
        JC CPDnI
        LD A,(CPDi)
        XOR 1
        LD (CPDi),A
CPDnI
       ENDIF

        CALL JTNROTATE
        CALL PERSPCORR

        LD HL,TPOLYZ
        LD D,'VTX3DZ
        LD A,(NPOLYS)
        LD B,A
        LD C,128
COUNTZ0
       PUSH HL
        INC H
        LD A,(HL)
        INC H
        LD H,(HL),L,A ;адрес полигона
        LD E,(HL) ;адрес вершины A
        INC L
        INC HL
        LD A,(DE) ;ZA
        LD E,(HL) ;адрес вершины B
        INC L
        INC HL
        LD L,(HL) ;адрес вершины C
        LD H,D
        ADD A,(HL) ;ZC
        EXD
        ADD A,(HL) ;ZB
         EXD
       POP HL
       ;ADD A,C ;чтобы без знака
      ;NEG ;в PERSPCORR -Z понимается как Z и наоборот
        LD (HL),A ;сумма координат Z для вершин грани
        INC L
        DJNZ COUNTZ0

        LD HL,TPOLYZ
        CALL SORTZ

        LD (GSBALLSP),SP
        LD A,gspgballer1
        OUT (MPAG),A
       LD A,72+136
angle1=$+1
       SUB 0
angle1corr=$+1
       ADD A,0
       LD H,A
       LD A,128
angle2=$+1
       SUB 0
       LD L,A
        LD A,H
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,gspgballtex1*257<1
        RRCA
        OUT (MPAGEX),A
        EXA
        LD A,H
        AND #1F
        ADD A,#D0 ;запас 16 на переполнение
        LD H,A
        LD IX,GSBALLQ
        JP #8000
GSBALLQ
GSBALLSP=$+1
        LD SP,0

        LD HL,TPOLYS
NPOLYS=$+1
        LD B,4
DRAWPOLYS0
       PUSH BC,HL
        LD A,(HL)
        INC H
        LD H,(HL),L,A
        CALL TRI
       POP HL,BC
        INC L
        DJNZ DRAWPOLYS0

        LD A,(gsdrawscr)
        OR A
        LD A,gspgchscr0
        JZ $+4
        LD A,gspgchscr1
        OUT (MPAG),A
        LD A,gspgtransch
        GSSETPGA
        LD (GSLOOPSP),SP
        LD IX,GSLOOPTRANSQ
        LD E,31
        LD B,'TCH256
        EXX
        LD HL,gschmap256
        LD B,16
        JP transch
GSLOOPTRANSQ
        LD IX,GSLOOPTRANSQ2
        LD HL,gschmap256+128
        LD B,16
        JP transch
GSLOOPTRANSQ2
GSLOOPSP=$+1
        LD SP,0

        CALL WAITER0 ;GS ожидает конец кадра у ZX
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
        LD A,gspgchscr0
        JZ $+4
        LD A,gspgchscr1
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
        JP GSLOOP

PERSPCORR
;можно расширить диапазон Z за счет X,Y -
;если Z=L, X,Y=H (RRA:RRA:OR #C0)
        LD A,gspgpersp
        GSSETPGA
        LD HL,(VTX3DBEG)
PERSPCORR0
        LD H,'VTX3DZ
        LD D,(HL) ;Z-perspdist
        DEC H
        LD E,(HL) ;Y
        LD A,(DE)
       ADD A,'gschmap256+32
        LD (HL),A
        DEC H
        LD E,(HL) ;X
        LD A,(DE)
      ;ADD A,32
        LD (HL),A
        INC L
        JNZ PERSPCORR0
        RET

JTNROTATE
        LD A,gspgsin
        GSSETPGA

        LD     HL,(ALPHA)
        LD     A,(HL)
        LD     (_ALPCAL),A
        INC    H
        LD     A,(HL)
        LD     (_ASIN),A
        INC    H
        LD     A,(HL)
        LD     (_ACOS),A

        LD     HL,(BETA)
        LD     A,(HL)
        LD     (_BETCAL),A
        INC    H
        LD     A,(HL)
        LD     (_BSIN),A
        INC    H
        LD     A,(HL)
        LD     (_BCOS),A

        LD     HL,(GAMMA)
        LD     A,(HL)
        LD     (_GAMCAL),A
        INC    H
        LD     A,(HL)
        LD     (_GSIN),A
        INC    H
        LD     A,(HL)
        LD     (_GCOS),A

VTX3DBEG=$+1
        LD HL,VTX3D
        LD A,L,LX,A
___R1
        LD     H,VTX3DH
        LD     HX,VTX3DIH
        EXX
        LD     C,(IX+0)
        INC    HX
        LD     B,(IX+0)
        INC    HX

_ASIN=$+1
        LD     H,0
_ACOS=$+1
        LD     D,0
_ALPCAL=$+1
        CALL   PROCROT         ;INP CB, OUTP AB
        EX     AF,AF'
        LD     C,B
        LD     B,(IX+0)
                                ;A'=X,C=Y,B=Z
_BSIN=$+1
        LD     H,0
_BCOS=$+1
        LD     D,0
_BETCAL=$+1
        CALL   PROCROT
        EX     AF,AF'          ;PUT Y
        LD     C,B
        LD     B,A
                                ;A'=Y,C=Z,B=X
_GSIN=$+1
        LD     H,0
_GCOS=$+1
        LD     D,0
_GAMCAL=$+1
        CALL   PROCROT
                                ;A=Z,B=X,A'=Y
         SRA A
         SRA A
curperspdist=$+1
         SUB perspdist
        LD     (_CZ),A
        LD     A,B
        EXX
        ;SRA     A
        LD     (HL),A ;X
        INC    H
        EX     AF,AF'
        ;SRA     A
        LD     (HL),A ;Y
        INC    H
_CZ=$+1
        LD     (HL),0
        INC    L
        INC    LX
        JNZ ___R1
        RET

SORTZ
;HL=список Z (беззнаковых), в конце 0; #FF
;через INC H лежат адреса полигонов (ld e,(hl):inc h:ld d,(hl))
;в результате они будут по убыванию Z
SORTZFINDMAX
       PUSH HL
SORTZFINDMAX0
        LD D,H,E,L
        LD A,(HL)
SORTZFINDMAX1
        INC L
        CP (HL)
        JP C,SORTZFINDMAX1
        LD A,(HL)
       ;INC A
       OR A
        JP NZ,SORTZFINDMAX0
;DE=адрес максимума
;обмениваем с текущим началом списка
       POP HL
        LD A,(DE),C,(HL),(HL),A
        LD A,C,(DE),A
        INC H,D
        LD A,(DE),C,(HL),(HL),A
        LD A,C,(DE),A
        INC H,D
        LD A,(DE),C,(HL),(HL),A
        LD A,C,(DE),A
        DEC H,H
        INC L
        LD A,(HL)
       ;INC A
       OR A
        JNZ SORTZFINDMAX
        RET

;адрес вершины A ;Y,X,Z
;-"- B
;-"- C
;VU вершины A (0/4)
;-"- B
;-"- C
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
KUBSZ=20
KUB
        DB 34 ;вершин
        DB +KUBSZ,+KUBSZ,+KUBSZ
        DB +KUBSZ,+KUBSZ,-KUBSZ
        DB +KUBSZ,-KUBSZ,+KUBSZ
        DB +KUBSZ,-KUBSZ,-KUBSZ
        DB -KUBSZ,+KUBSZ,+KUBSZ
        DB -KUBSZ,+KUBSZ,-KUBSZ
        DB -KUBSZ,-KUBSZ,+KUBSZ
        DB -KUBSZ,-KUBSZ,-KUBSZ
        DB 4,-11,-24
        DB -6,0,-27
        DB -6,-14,-7 ;10
        DB -14,0,-14 ;11 нос
        DB -6,14,-7
        DB 4,11,-24
        DB -11,0,-7 ;14 переносица
        DB -5,-12,08
        DB -5,12,08
        DB -10,0,11 ;17
        DB 10,-22,-6
        DB 10,22,-6
        DB -20,0,10 ;20
        DB -5,-20,2
        DB -5,20,2
        DB -3,-15,30
        DB -3,15,30 ;24
        DB 2,-20,-15 ;25
        DB 2,20,-15
        DB 20,-30,6
        DB 20,30,6 ;28
        DB 30,-30,-20
        DB 30,30,-20 ;30
        DB 30,0,-20
        DB 30,-30,20 ;32
        DB 30,30,20
        DB 28 ;граней
        DW KPOLYJAWLEFT
        DW KPOLYMOUTHLEFT
        DW KPOLYNOSELEFT
        DW KPOLYEYELEFT
        DW KPOLYFORELEFT
        DW KPOLYVISOKLEFT
        DW KPOLYJAW2LEFT
        DW KPOLYHAIRLEFT
        DW KPOLYHAIR2LEFT
        DW KPOLYHAIR3LEFT
        DW KPOLYHAIR4LEFT ;задняя плоскость
        DW KPOLYHAIR5LEFT

        DW KPOLYHAIRFRONT
        DW KPOLYHAIRBACK
        DW KPOLYHAIRBACK2
        DW KPOLYHAIRBACK3

        DW KPOLYJAWRIGHT
        DW KPOLYMOUTHRIGHT
        DW KPOLYNOSERIGHT
        DW KPOLYEYERIGHT
        DW KPOLYFORERIGHT
        DW KPOLYVISOKRIGHT
        DW KPOLYJAW2RIGHT
        DW KPOLYHAIRRIGHT
        DW KPOLYHAIR2RIGHT
        DW KPOLYHAIR3RIGHT
        DW KPOLYHAIR4RIGHT ;задняя плоскость
        DW KPOLYHAIR5RIGHT

        MACRO KPOLY
KPOLY\0
        DW #FF-\1+VTX3DY
        DW #FF-\2+VTX3DY
        DW #FF-\3+VTX3DY
        DW \5,\6,\7
        DW \8
        DW \4+texborder*257
        DW TRIDRAW
        DW gspgtex<8
        ENDM
;в кубе и грань, и текстура против часовой

        DS $&1
;LEFT на экране правый (у модели)
        KPOLY JAWLEFT,10,8,9,#40,#0004,#0000,#0400,#C300
        KPOLY MOUTHLEFT,9,11,10,#40,#0004,#0404,#0400,#F200
        KPOLY NOSELEFT,11,14,10,#80,#0004,#0404,#0400,#F200
        KPOLY EYELEFT,15,10,14,#00,#0400,#0000,#0004,#F200
        KPOLY FORELEFT,17,15,14,#00,#0404,#0400,#0004,#F200
        KPOLY VISOKLEFT,18,10,15,#80,#0000,#0400,#0004,#C300
        KPOLY JAW2LEFT,18,8,10,#40,#0400,#0000,#0004,#F200
        KPOLY HAIRLEFT,21,23,20,#C0,#0000,#0400,#0004,#F200
        KPOLY HAIR2LEFT,25,27,21,#C0,#0404,#0004,#0400,#F200
        KPOLY HAIR3LEFT,23,21,27,#C0,#0400,#0004,#0000,#F200
        KPOLY HAIR4LEFT,29,31,32,#C0,#0400,#0404,#0004,#C300
        KPOLY HAIR5LEFT,32,27,23,#C0,#0004,#0000,#0400,#C300

        KPOLY HAIRFRONT,24,20,23,#C0,#0000,#0400,#0004,#F200
        KPOLY HAIRBACK,33,31,32,#C0,#0404,#0004,#0400,#F200
        KPOLY HAIRBACK2,24,33,32,#C0,#0404,#0004,#0400,#F200
        KPOLY HAIRBACK3,24,32,23,#C0,#0404,#0004,#0400,#F200

        KPOLY JAWRIGHT,13,12,9,#40,#0000,#0004,#0400,#F200
        KPOLY MOUTHRIGHT,11,9,12,#40,#0404,#0004,#0400,#C300
        KPOLY NOSERIGHT,14,11,12,#80,#0404,#0004,#0400,#C300
        KPOLY EYERIGHT,12,16,14,#00,#0000,#0400,#0004,#C300
        KPOLY FORERIGHT,16,17,14,#00,#0400,#0404,#0004,#C300
        KPOLY VISOKRIGHT,12,19,16,#80,#0400,#0000,#0004,#F200
        KPOLY JAW2RIGHT,13,19,12,#40,#0000,#0400,#0004,#C300
        KPOLY HAIRRIGHT,20,24,22,#C0,#0000,#0400,#0004,#F200
        KPOLY HAIR2RIGHT,22,28,26,#C0,#0404,#0004,#0400,#F200
        KPOLY HAIR3RIGHT,28,22,24,#C0,#0004,#0000,#0400,#F200
        KPOLY HAIR4RIGHT,30,31,33,#C0,#0004,#0404,#0400,#C300
        KPOLY HAIR5RIGHT,33,28,24,#C0,#0000,#0004,#0400,#C300

ALPHADEFW   SIGNOFS+#40
BETADEFW   SIGNOFS+#00
GAMMADEFW   SIGNOFS+#00

        INCLUDE "texpp*",#C5

GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

        DS .(-$)
sin256
        INCBIN "sin+"

        INCLUDE "vujump*",#C4

        DISPLAY "без инсталлеров конец кода=",$
;можно затирать
        INCLUDE "instp*",#C2
palgirl
        INCBIN "palgirl"
        DISPLAY "с инсталлерами конец кода=",$
       ENT
LENGSTEXCODE=$-WASGSTEXCODE
       DISPLAY /T,LENGSTEXCODE
wasuv
        INCBIN "uv256d"
uvlen=$-wasuv
LENTEXGS=$-#C000

        ORG #C000,pgeffects
          pgtexzx=pgeffects
       PUSH BC
       PUSH DE
        LD HL,WASTEXCODE
        LD DE,TEXCODE
       PUSH DE
        LD BC,LENTEXCODE
        LDIR
        RET

WASTEXCODE
       DISP TEXCODE
       POP HL
       LD (zxendtime),HL
        HALT

        LD A,#14
        CALL TEXCL1800PG
        LD A,#15
        CALL TEXCL1800PG
        LD A,#16
        CALL TEXCL1800PG
        LD A,#17
        CALL TEXCL1800PG

        LD A,1 ;16C
        LD BC,#EFF7
        OUT (C),A

        LD HL,OUT16C
        CALL MKOUT16C

       POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JC ZXWAITSTART

BIGLOOP
        HALT
        LD A,(timer)
        OUT (GSDAT),A
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

        LD A,#17!#1D
        XOR #1D ;видим scr1, рисуем scr0
        LD ($-1),A ;теперь видим scr0, рисуем scr1
        LD (VIEWPG1),A
        DEC A
        LD (VIEWPG0),A
        setpga

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
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1617
        LD A,(HL) ;первый байт неверен
        JP OUT16C
BIGLOOPVIEWQ
BIGLOOPSP=$+1
        LD SP,0
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

TEXCL1800PG
        setpga
        LD HL,#C000
        CALL TEXCL1800
        LD HL,#E000
TEXCL1800
        LD D,H,E,L
        INC E
        LD BC,#17FF
        LD (HL),L
        LDIR
        RET

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
;13K кода для 256X128
       ;LD A,H
;LOOP  ;LD (curpg),A
       ;OUT (C),A
       ;LD SP,низ
       ;DUP 16
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,верх
       ;DUP 16
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;то же низ+#2000 и верх+#2000
       ;EDUP
       ;CP H
       ;LD A,L
       ;JP Z,LOOP
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD DE,#D760
        LD B,64 ;высота 64*2
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
       IFN mirrorchunks
        BIT 1,D
        JZ $+6
        LD A,D
        XOR 1
        LD D,A
       ENDIF
       PUSH DE
        CALL MKOUT16C0
        LD A,D
        SUB 8
        LD D,A
        CALL MKOUT16C0
       POP DE
        SET 5,D
        CALL MKOUT16C0
        LD A,D
        SUB 8
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
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD A,E
        ADD A,A
        JNZ MKOUT16CnPROTECT
;будет 32 таких линии вместо 22, а то запарно проверять
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
LENTEXCODE=$-WASTEXCODE
LENTEXZX=$-#C000

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "TEXZX   C"
        DW #C000
        DW LENTEXZX
        DB pgtexzx
        DW #C000

        DB "TEXGS   C"
        DW #C000
        DW LENTEXGS
        DB pgtexgs
        DW #C000
        INCLUDE "SAVEOBJ*",#C0

