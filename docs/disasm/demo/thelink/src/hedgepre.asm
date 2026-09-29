pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
mkballplasma=0

curpg=#5E5D
IMVEC=#5B00
IMER=#5E5E

parthgt=28 ;высота экранной четверти

DEPKS_picarea=#A000
DEPKS_linebuf=#9F00
DEPKS_pgtrash=#50;#58

lambert=32-16
lambertshr=3
lambertmax=7

border=0
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

gspgtball=4 ;временно
 GSTQUARTERBALL=#4000 ;временно
 GSTBALL=#C000 ;2*64*128=16K
gspgballer1=4+#80
gspgballer2=5+#80
gspgballtex1=#20 ;8 страниц

;X 3D-движка растет на экране вверх
;Y 3D-движка растет на экране вправо
gspgchscr0=4
gspgchscr1=5
gspgdiv=#30 ;(L/2)/-H ;+-7.8=+-6.0/6.0
gspgpersp=#33 ;+-7.0=+-7.0/6.0
gspgsin=#31
 SIGNOFS=#C000
 SINOFS=#C100
 COSOFS=#C200
 PROCROT=#C300
 MULTAB=#C400
gspgtex=#34 ;8 страниц
gspgtransch=#32
 transch=#E000
 TCH256=#FE00

debug=0 ;1=проверка отрицательной ширины
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
GSPROG=#5B30
GSSTACK=#5B30

       MACRO GSSETPGA
        OUT (MPAGEX),A
       ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

        MACRO setpg
        LD A,\0
        setpga
        ENDM

        ORG #7800
begin
GO
        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG
        LD A,#16
        CALL CL1800PG
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),L
        LDIR
        LD A,#17
        CALL CL1800PG
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),L
        LDIR

       DI
        LD HL,BLOCK_PIC1
        LD BC,SIZE_PIC1
        CALL DEPKS16
       RET

BIGLOOP
        HALT
       IFN border
        XOR A
        OUT (254),A
       ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        LD A,0
        OUT (GSCOM),A

        LD A,#17!#1D
        XOR #1D ;видим scr1, рисуем scr0
        LD ($-1),A ;теперь видим scr0, рисуем scr1
        LD (VIEWPG1),A
        DEC A
        LD (VIEWPG0),A
        LD BC,#7FFD
        OUT (C),A

;и ждем выключения:
        WN
        GD
;
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
        EI
       IFN border
        LD A,7
        OUT (254),A
       ENDIF
        JP BIGLOOP
QUIT
        LD A,C_GRST
        OUT (GSCTR),A
        JR $

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
;13K кода для 256X128
       ;LD A,H
;LOOP  ;OUT (C),A
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
       LD (HL),#56 ;d,(hl)
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
        LD HY,#18
SCR1TOPP0
       PUSH HL
        LD DE,DEPKS_linebuf
       PUSH DE
        LD A,HX
        setpga
        LD BC,256
        LDIR
       POP HL
       POP DE
        LD A,LX
        setpga
        LD BC,256
        LDIR
        EXD
        DEC HY
        JNZ SCR1TOPP0
        RET

DEPKS16
;HL=адрес упакованной картинки
;BC=ее длина
        LD DE,DEPKS_picarea
        PUSH BC,DE
        LDIR
        setpg DEPKS_pgtrash
        POP DE,BC
        LD HL,0
        OR A
        SBC HL,BC
        EXD
        PUSH DE
        LDIR ;теперь упак. блок в конце памяти
        POP HL
        LD DE,DEPKS_picarea
        PUSH DE
        CALL DEHR21
        POP HL
        LD DE,#C000
        LD LX,32*4
DEPKST0
        PUSH DE
        setpg DEPKS_pgtrash
        LD DE,DEPKS_linebuf
        LD BC,192
        LDIR
        POP DE
        PUSH DE
        PUSH HL
        LD HL,DEPKS_linebuf
        CALL setpgst
        LD B,192
DEPKST1LD A,(HL),(DE),A
        INC HL
        CALL DEPKS_DDE
        DJNZ DEPKST1
        POP HL
        POP DE
        CALL nxtst
        DEC LX
        JNZ DEPKST0
        RET

nxtst
        LD A,D
        XOR 128
        BIT 7,D
        LD D,A
        RET NZ
        LD A,D
        XOR 32
        BIT 5,D
        LD D,A
        RET Z
        INC E
        RET
setpgst
        LD A,D
        RLA
        CCF
        LD A,#16>1
        RLA ;#16 или #17
        setpga
        SET 7,D
        RET

DEPKS_DDE
        INC D
        LD A,D
        AND 7
        RET NZ
        LD A,E
        ADD A,32
        LD E,A
        RET C
        LD A,D
        SUB 8
        LD D,A
        RET

DEHR21
;HL=FROM
;DE=TO
       LOCAL
        PUSH DE
        LD DE,6+8
        ADD HL,DE
        PUSH HL
        EXX
        POP HL,DE
        LD B,6
HfstDEC HL
        LD A,(HL)
        PUSH AF
        DJNZ Hfst
        EXX
        LD DE,#1003,C,#80
HloopLD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
HxxtbitEXX
HnxtbitSLA C
        CALL Z,CHL
        JC Hloop
        LD B,1
H2bitLD A,#40
H2bit0SLA C
        CALL Z,CHL
        RLA
        JNC H2bit0
        CP E
        JC H2lessE
        ADD A,B
        LD B,A
        XOR D
        JNZ H2bit
H2lessEADD A,B
        CP 4
        JZ Hnpaked
        ADC A,-1
LL5DB1CP 2
LL5DB3EXX
        LD C,A,H,-1
        EXX
        JC H3bit ;B=1
        JZ LL5DE5
        SLA C
        CALL Z,CHL
        JC LL5DE5
        LD A,#7F,B,E
        DJNZ Hbits ;B=3 CALL GET2BITS
                   ;B=1
LL5DCBDJNZ Hplain
        LD B,A     ;FC-FF
        SBC A,A
HijnzSLA C
        CALL Z,CHL
        RLA
        DEC A      ;1.FD-FE
                   ;2>F8
                   ;3>F0
                   ;4>E0
        INC B
        JNZ Hijnz
        CP #E1
        JNZ $+4
        LD A,(HL)
        INC HL
        EXX
        LD H,A
        EXX
LL5DE5LD A,(HL)
        INC HL
LL5DE7EXX
        LD L,A
        ADD HL,DE
        LDIR
        JR Hxxtbit
HplainADD A,6
        RLA
        LD B,A
HldirLD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
        EXX
        DJNZ Hldir
        JR Hnxtbit
HnpakedSLA C
        CALL Z,CHL
        LD A,D
        JNC Hbits;B=3 GETBIT,Hplain
        LD A,(HL)
        INC HL
        CP D
        JNC LL5DB1
        OR A
        JZ Hquit
        EXX
        LD B,A
        EXX
        LD A,(HL)
        INC HL
        JR LL5DB3
H3bitLD A,#3F
HbitsSLA C
        CALL Z,CHL
        RLA
        JNC Hbits
        DJNZ LL5DCB
        JR LL5DE7
HquitEXX
        LD B,6
        POP AF
        LD (DE),A
        INC DE
        DJNZ $-3
        ;выходим. HL'=после упакованного блока
CHLLD C,(HL)
        INC HL
        RL C
        RET
       ENDL

;#13 Jump to Address
GSJUMP
        SD L
        SC #13
        WC
        SD H
        WD
        RET

;как в RIFF TRACKER
GSLOAD2
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
        RET

;#14 Load memory block
GSLOAD
        SD C
        SC #14
        WD ;<PAUSE>?
        SD B
        WD
        SD E
        WD
        SD D
        ;<PAUSE>
        HALT
        HALT
GSLOAD0 SD (HL)
        INC HL
        DEC BC
        WD
        LD A,B
        OR C
        JNZ GSLOAD0
        RET

GSSENDDATA
       ;WN ;ждем готовность GS к приему
GSSD0IN A,(GSDAT)
        CP E
        JNZ GSSD0
        LD A,E
        OUT (GSCOM),A

GSSEND0
        SD (HL)
        INC HL
        DEC BC
        WD
        LD A,B
        OR C
        JNZ GSSEND0
        RET

GSPREPARE
       ;IN A,(GSDAT)
       ;CP E
       ;JNZ $-3
        LD A,E
        OUT (GSCOM),A
        RET

GSDMASENDPG
        LD BC,#7FFD
        OUT (C),A
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

CL1800PG
        LD BC,#7FFD
        OUT (C),A
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
OUT16C
       DISPLAY "GS CODE=",$

WASGSPROG
        DISP GSPROG
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

        ;почему-то если тут LDIR, то прием картинки виснет

        LD A,1    ;SET DMA MODULE #1
        OUT (DMA_MOD),A

       IFN 1
         IN A,(ZXDATRD) ;не помогает
        LD E,2
        LD A,E
        OUT (ZXDATWR),A ;готов к приему
;GS ожидает команду от ZX
GSWAIC0IN A,(ZXCMD)
        CP E
        JNZ GSWAIC0

;принимаем данные для эффекта
        LD HL,#3D00;#4000
        LD BC,#300;#1B00
GSGET0
GSWD0IN A,(ZXSTAT)
        RLCA
        JNC GSWD0
        IN A,(ZXDATRD)
        LD (HL),A
        INC HL
        DEC BC
        LD A,B
        OR C
        JNZ GSGET0
       ENDIF
        LD E,3
       ;LD A,E
       ;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JNZ $-3

        LD A,gspgballtex1
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
        OUT (DMA_CST),A ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
       IN A,(ZXSTAT)
       RLCA
       JC $-3 ;ZX готов?

        LD A,gspgballtex1*257<1+#404>1
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
       ;CALL MKBALLER
       ;CALL MKBALLTEX
       ;CALL GENTEX
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
      IFN 1
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
       PUSH AF
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
      ENDIF

;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JNZ GSWAIT0 ;48t
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
;GS ожидает обнуления (ZX запросит данные)
        LD HL,0
        LD DE,1
GSWAIT1ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JZ GSWAIT1

       POP AF
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
         SRA A
         SUB perspdist
        LD     (_CZ),A
        LD     A,B
        EXX
     SRA A
        LD     (HL),A ;X
        INC    H
        EX     AF,AF'
     SRA A
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
;адрес вершины B
;адрес вершины C
;VU вершины A (0/4)
;VU вершины B (0/4)
;VU вершины C (0/4)
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
icosz=23*2 ;на 24 редко зашкаливает
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
_=\9
        DW #FF-_+VTX3DY
        ENDM

        MACRO KPOL
        KPOLY \0A,\0,\1,\2,#00,#0004,#0000,#0400,#F200,\6
        KPOLY \0B,\0,\2,\3,#00,#0004,#0000,#0400,#F200,\7
        KPOLY \0C,\0,\3,\4,#00,#0004,#0000,#0400,#F200,\8
        KPOLY \0D,\0,\4,\5,#00,#0004,#0000,#0400,#F200,\9
        KPOLY \0E,\0,\5,\1,#00,#0004,#0000,#0400,#F200,\P*0+\9
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
LENGSPROG=$-WASGSPROG
       DISPLAY /T,LENGSPROG

        DS DEPKS_picarea-$
BLOCK_PIC1
        INCBIN "demon16.p"
SIZE_PIC1=$-BLOCK_PIC1
end
        ORG #C000,pgdata5
        INCBIN "kishki.C"
        ORG #5CDD,0
        DB "GSHEDGE B"
        INCLUDE "m2hr*",#C0

