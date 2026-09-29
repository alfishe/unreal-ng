border=0
debug=0 ;1=проверка отрицательной ширины

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

HEDGECODE=#7800

resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

parthgt=28 ;высота экранной четверти

lambert=32-16
lambertshr=3
lambertmax=7

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
;X 3D-движка растет на экране вверх
;Y 3D-движка растет на экране вправо

gspgchscr0=2
gspgchscr1=3
gspgdiv=#04 ;(L/2)/-H ;+-7.8=+-6.0/6.0
gspgpersp=#84 ;+-7.0=+-7.0/6.0
gspgsin=#05
 SIGNOFS=#C000
 SINOFS=#C100
 COSOFS=#C200
 PROCROT=#C300
 MULTAB=#C400
gspgtransch=#85
 transch=#E000
 TCH256=#FE00
gspgtex=#06 ;8 страниц

gspgcode=#10
gspgdata=#20

mirrorchunks=1 ;строки 0,1,3,2,4,5,7,6...
perspdist=16;32
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

        INCLUDE "gsports",#C7
GSPROG=#5830
GSSTACK=#5830
GSHEDGECODE=#5900

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
        DW 0,0
        DW 896,896*2-20 ;hedge
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

        ORG #C000,pgdata1
        INCBIN "kishki.C"

        ORG #C000,pgcode1
        pghedgegs=pgcode1
        LD HL,WASGSHEDGECODE
        LD DE,GSHEDGECODE
        LD BC,LENGSHEDGECODE
        LDIR
        JP GSHEDGEGO

WASGSHEDGECODE
       DISP GSHEDGECODE
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

GSHEDGEGO
        LD A,gspgdata
        OUT (MPAGEX),A
        LD BC,gspgtex<8
GSTEX80LD A,B
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        LD B,A
        LD HL,#C000
        LD DE,#8000
GSTEX81LD A,(HL)
        ADD A,C
        LD (DE),A
        INC HL
        INC DE
        BIT 6,H
        JNZ GSTEX81
        LD A,C
        ADD A,32
        LD C,A
        JNZ GSTEX80
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
        JZ GSTIMERACTIONQ
       CP 2
       JC $+4
       LD A,2
        LD B,A
GSTIMERACTION
        LD HL,ALPHA
        INC (HL)
        LD HL,BETA
        DEC (HL)
        LD A,201
        ADD A,0
        LD ($-1),A
        JNC $+3 ;некратные угловые скорости
        DEC (HL)
        LD HL,GAMMA
        LD A,125
        ADD A,0
        LD ($-1),A
        JNC $+3 ;некратные угловые скорости
        INC (HL)
        DJNZ GSTIMERACTION
GSTIMERACTIONQ
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
        LD (HL),A ;сумма координат Z для вершин грани
        INC L
        DJNZ COUNTZ0

        LD HL,TPOLYZ
        CALL SORTZ

        LD (GSCLSP),SP
        LD HL,#0040
        LD DE,0
        LD B,64
GSCL0LD SP,HL
        DUP 32
        PUSH DE
        EDUP
        INC H
        DJNZ GSCL0
GSCLSP=$+1
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
        LD E,15 ;31 для 256X128
        LD B,'TCH256
        EXX
        LD HL,gschmap256 + 32-parthgt
        LD B,16 ;64 Y / 4 слоя
        JP transch
GSLOOPTRANSQ
GSLOOPSP=$+1
        LD SP,0

        CALL WAITER0 ;GS ожидает конец кадра у ZX
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A ;после этого ZX можно сбрасывать
       IN A,(ZXDATRD)
       LD (gstimer),A
       ;OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
;GS ожидает запроса данных
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
       ;ADD A,'gschmap256
        AND #3F
        LD (HL),A
        DEC H
        LD E,(HL) ;X
        LD A,(DE)
        LD (HL),A
        INC L
        JNZ PERSPCORR0
        RET

JTNROTATE
        LD A,gspgsin
        GSSETPGA

        LD HL,(ALPHA)
        LD A,(HL)
        LD (_ALPCAL),A
        INC H
        LD A,(HL)
        LD (_ASIN),A
        INC H
        LD A,(HL)
        LD (_ACOS),A

        LD HL,(BETA)
        LD A,(HL)
        LD (_BETCAL),A
        INC H
        LD A,(HL)
        LD (_BSIN),A
        INC H
        LD A,(HL)
        LD (_BCOS),A

        LD HL,(GAMMA)
        LD A,(HL)
        LD (_GAMCAL),A
        INC H
        LD A,(HL)
        LD (_GSIN),A
        INC H
        LD A,(HL)
        LD (_GCOS),A

VTX3DBEG=$+1
        LD HL,VTX3D
        LD A,L,LX,A
___R1
        LD H,VTX3DH
        LD HX,VTX3DIH
        EXX
        LD C,(IX+0)
        INC HX
        LD B,(IX+0)
        INC HX

_ASIN=$+1
        LD H,0
_ACOS=$+1
        LD D,0
_ALPCAL=$+1
        CALL PROCROT         ;INP CB, OUTP AB
        EXA
        LD C,B
        LD B,(IX+0)
                                ;A'=X,C=Y,B=Z
_BSIN=$+1
        LD H,0
_BCOS=$+1
        LD D,0
_BETCAL=$+1
        CALL PROCROT
        EXA                    ;PUT Y
        LD C,B
        LD B,A
                                ;A'=Y,C=Z,B=X
_GSIN=$+1
        LD H,0
_GCOS=$+1
        LD D,0
_GAMCAL=$+1
        CALL PROCROT
                                ;A=Z,B=X,A'=Y
     SRA A
         SRA A
         SRA A
         SUB perspdist
        LD (_CZ),A
        LD A,B
        EXX
     SRA A
        LD (HL),A ;X
        INC H
        EXA
     SRA A
        LD (HL),A ;Y
        INC H
_CZ=$+1
        LD (HL),0
        INC L
        INC LX
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
;-"- C (0/4)
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
icosz=23*2 ;на 24 редко зашкаливает
           ;23 реальный GS за фрейм чуть-чуть не тянет
icod=icosz*158/256
V0X=-icod
V0Y=-icosz
V0Z=0
V1X=icod
V1Y=-icosz
V1Z=0
V2X=icod
V2Y=icosz
V2Z=0
V3X=-icod
V3Y=icosz
V3Z=0

V4X=0
V4Y=-icod
V4Z=icosz
V5X=0
V5Y=icod
V5Z=icosz
V6X=0
V6Y=icod
V6Z=-icosz
V7X=0
V7Y=-icod
V7Z=-icosz

V8X=icosz
V8Y=0
V8Z=icod
V9X=icosz
V9Y=0
V9Z=-icod
V10X=-icosz
V10Y=0
V10Z=-icod
V11X=-icosz
V11Y=0
V11Z=icod

V0=0
V1=1
V2=2
V3=3
V4=4
V5=5
V6=6
V7=7
V8=8
V9=9
V10=10
V11=11

KUB
        DB 12+20 ;вершин
        DB V0X,V0Y,V0Z
        DB V1X,V1Y,V1Z
        DB V2X,V2Y,V2Z
        DB V3X,V3Y,V3Z
        DB V4X,V4Y,V4Z
        DB V5X,V5Y,V5Z
        DB V6X,V6Y,V6Z
        DB V7X,V7Y,V7Z
        DB V8X,V8Y,V8Z
        DB V9X,V9Y,V9Z
        DB V10X,V10Y,V10Z
        DB V11X,V11Y,V11Z
k=98
m=256
_=12
        MACRO DOTV0_1
V\0_\1=_
_=_+1
        DB V\1X-V\0X*k/m+V\0X,V\1Y-V\0Y*k/m+V\0Y,V\1Z-V\0Z*k/m+V\0Z
        ENDM
        DOTV0_1 0,8
        DOTV0_1 1,5
        DOTV0_1 0,9
        DOTV0_1 1,6
        DOTV0_1 1,2
        DOTV0_1 0,6
        DOTV0_1 0,5
        DOTV0_1 0,3
        DOTV0_1 2,1
        DOTV0_1 2,4
        DOTV0_1 2,7
        DOTV0_1 2,10
        DOTV0_1 2,11
        DOTV0_1 3,0
        DOTV0_1 3,4
        DOTV0_1 3,7
        DOTV0_1 4,2
        DOTV0_1 4,3
        DOTV0_1 6,0
        DOTV0_1 6,1

        DB 12*5 ;граней
        MACRO INSFR
        DW KPOLY\0A
        DW KPOLY\0B
        DW KPOLY\0C
        DW KPOLY\0D
        DW KPOLY\0E
        ENDM
        INSFR 0
        INSFR 1
        INSFR 2
        INSFR 3
        INSFR 4
        INSFR 5
        INSFR 6
        INSFR 7
        INSFR 8
        INSFR 9
        INSFR 10
        INSFR 11

        MACRO KPOLY
KPOLY\0
        DW #FF-\1+VTX3DY
        DW #FF-\2+VTX3DY
        DW #FF-\3+VTX3DY
        DW \5,\6,\7
        DW \8
        DW \4+texborder*257
        DW TRIDRAW
       ;DW gspgtex+3
        DW #FF-\9+VTX3DY
        ENDM

        MACRO KPOL
        KPOLY \0A,\0,\1,\2,#00,#0004,#0000,#0400,#F200,\6
        KPOLY \0B,\0,\2,\3,#00,#0004,#0000,#0400,#F200,\7
        KPOLY \0C,\0,\3,\4,#00,#0004,#0000,#0400,#F200,\8
        KPOLY \0D,\0,\4,\5,#00,#0004,#0000,#0400,#F200,\9
_=\P
_=\9\R
        KPOLY \0E,\0,\5,\1,#00,#0004,#0000,#0400,#F200,_
        ENDM
        DS $&1
        KPOL 0,V0_9,V0_8,V0_5,V0_3,V0_6,1,4,11,10,7
        KPOL 1,V0_8,V0_9,V1_6,V1_2,V1_5,0,7,9,8,4
        KPOL 2,V2_10,V2_11,V2_4,V2_1,V2_7,3,5,8,9,6 ;!
        KPOL 3,V2_11,V2_10,V3_7,V3_0,V3_4,2,6,10,11,5 ;!

        KPOL 4,V0_5,V0_8,V1_5,V4_2,V4_3,0,1,8,5,11
        KPOL 5,V4_3,V4_2,V2_4,V2_11,V3_4,4,8,2,3,11
        KPOL 6,V2_10,V2_7,V6_1,V6_0,V3_7,2,9,7,10,3
        KPOL 7,V6_0,V6_1,V1_6,V0_9,V0_6,6,9,1,0,10

        KPOL 8,V4_2,V1_5,V1_2,V2_1,V2_4,4,1,9,2,5
        KPOL 9,V2_1,V1_2,V1_6,V6_1,V2_7,8,1,7,6,2
        KPOL 10,V3_7,V6_0,V0_6,V0_3,V3_0,6,7,0,11,3 ;!
       ;KPOL 10,V3_7,V6_0,V0_6,V0_3,V3_0,100,100,100,100,3 ;!баг
       ;KPOL 10,V3_7,V6_0,V0_6,V0_3,V3_0,100,100,100,11,100;!баг
        KPOL 11,V3_0,V0_3,V0_5,V4_3,V3_4,10,0,4,5,3

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

        INCLUDE "vujump*",#C4

        DISPLAY "без инсталлеров конец кода=",$
;NGS инсталлеры - можно затирать
        INCLUDE "instlp*",#C2
pal256
        INCBIN "palbrick"
        DISPLAY "с инсталлерами конец кода=",$
       ENT
LENGSHEDGECODE=$-WASGSHEDGECODE
       DISPLAY /T,LENGSHEDGECODE
LENHEDGEGS=$-#C000

        ORG #C000,pgeffects
       pghedgezx1=pgeffects
        MOVEPAGE #16,DEMON16,#3800,#C000
DEMON16
        INCBIN "DEMON16.C",#3800
LENHEDGEZX1=$-#C000

        ORG #C000,pgeffects+1
       pghedgezx2=pgeffects+1
       PUSH BC
       PUSH DE
        LD HL,WASHEDGECODE
        LD DE,HEDGECODE
       PUSH DE
        LD BC,LENHEDGECODE
        LDIR
        MOVEPAGE #17,DEMON17,#3800,#C000

WASHEDGECODE
       DISP HEDGECODE
       POP HL
       LD (zxendtime),HL
        HALT
        LD A,1 ;16C
        LD BC,#EFF7
        OUT (C),A

        LD A,#1F
        setpga
        CALL SCR1TOSCR0

        LD HL,OUT16C
        CALL MKOUT16C

       POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JC ZXWAITSTART

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
       ;DI
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1617
       ;LD A,(HL) ;первый байт неверен (попадет в D)
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

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
       ;LD A,H
;LOOP
      ;LD (curpg),A
       ;OUT (C),A
       ;LD SP,низ
       ;DUP 8
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,почти низ
       ;DUP 8
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,почти верх
       ;DUP 8
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,верх
       ;DUP 8
       ;LD D,(HL),E,(HL)
       ;PUSH DE
       ;EDUP
       ;LD SP,низ+#2000 и т.п. +#2000
       ;...
       ;CP H
       ;LD A,L
       ;JP Z,LOOP
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD DE,#D7E0
        LD B,parthgt;32 ;высота 32*4;при 30 эмуль тормозит редко
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
        CALL MKOUT16C0_2
       ;LD A,D
       ;SUB 8
       ;LD D,A
        CALL MKOUT16C0_2
       POP DE
        SET 5,D
        CALL MKOUT16C0_2
       ;LD A,D
       ;SUB 8
       ;LD D,A
        CALL MKOUT16C0_2
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
       CALL UDE
       POP BC
        DJNZ MKOUT16CLINE
        LD (HL),#DD
        INC HL
        LD (HL),#E9 ;jp (ix)
        RET

UDE
        LD A,D
        DEC D
        AND 7
        RET NZ
        LD A,E
        SUB 32
        LD E,A
        RET C
        LD A,D
        ADD A,8
        LD D,A
        RET

MKOUT16C0_2
        CALL MKOUT16C0
      ;PUSH DE
      ; LD A,E
      ; SUB #80
      ; LD E,A
      ; CALL MKOUT16C0
      ;POP DE
      ; RET

MKOUT16C0
        LD (HL),49
        INC HL
        LD A,E
        ADD A,14;16
        LD (HL),A
        ADC A,D
        SUB (HL)
        INC HL
        LD (HL),A
       INC HL
       LD (HL),#56 ;d,(hl)
       INC HL
       LD (HL),#5E ;e,(hl)
        LD B,7;8
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
       ;LD A,E
       ;ADD A,A
       ;JNZ MKOUT16CnPROTECT
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
       ;RET
MKOUTUDEPART
        LD B,parthgt;32
        CALL UDE
        DJNZ $-3
        RET

SCR1TOSCR0
        LD IX,#1F1D
        CALL SCR1TOPP
        LD IX,#1E1C
SCR1TOPP
        LD HL,#C000
        CALL SCR1TOPPP
        LD HL,#E000
SCR1TOPPP
        LD D,H,E,L
        LD HY,#18
        JP RESMOVEPAGE
OUT16C
       ENT
LENHEDGECODE=$-WASHEDGECODE
DEMON17
        INCBIN "DEMON17.C",#3800
LENHEDGEZX2=$-#C000

        ORG #C000,pgeffects+2
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "HEDGEZX1C"
        DW #C000
        DW LENHEDGEZX1
        DB pghedgezx1
        DW #C000

        DB "HEDGEZX2C"
        DW #C000
        DW LENHEDGEZX2
        DB pghedgezx2
        DW #C000

        DB "HEDGEGS C"
        DW #C000
        DW LENHEDGEGS
        DB pghedgegs
        DW #C000
        INCLUDE "SAVEOBJ*",#C0

