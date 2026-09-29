border=0
barhgt=16
barblack=0

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

ROTBARCODE=#7800

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
GSROTBARPRECODE=#5900
GSROTBARCODE=#5900

gspgcode=#10
gspgdata=#20

;адреса в NGS:
GSNMIDMA4=#0000
GSNMIDMA3=GSNMIDMA4+2
GSNMIDMA2=GSNMIDMA3+2
GSNMIDMA1=GSNMIDMA2+2
GSNMIDMABEG=GSNMIDMA1+2
GSNMIVAR=#0008 ;содержит GSNMIDMAn
bar0left=#000A ;0=падает слева

gspgbarpic=#2A ;12 страниц

BARPIC=#8000
BARPICATTR=BARPIC+#1800
TPICTOBMP=#9D00 ;,256 %lrlrlrlr->rlrrrlll
;BARSTAYBLACK=#9E00 ;вплоть до BARSTAYBMP
BARSTAYBMP=#9F00 ;,barhgt*256 (нельзя 0, #100)
BARLIEBLACK=#AF00 ;вплоть до BARLIEBMP
BARLIEBMP=#B000 ;,barhgt*256, а перед ним строка черного
BARdata=#2B00
BARdata_clr0=0
BARdata_out0=2
BARdata_clr1=4
BARdata_out1=6
BARdata_clrform=8+1
BARdata_outform=10+1
BARdata_clr0addr=12
BARdata_out0addr=14
BARdata_clr1addr=16
BARdata_out1addr=18
;адреса должны делиться на 128:
BARclr0=#2F80 ;растет вниз (#400+)
BARout0=#4000 ;растет вниз (#1000+) ;нельзя пересекать границу
BARclr1=#4480 ;растет вниз (#400+)                       ;окон
BARout1=#5500 ;растет вниз (#1000+)
endBARdata=BARout1
bardatadelta=#2A00
GSBARSCR1=BARdata
GSBARSCR2=BARdata-bardatadelta

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
        IN A,(ZXCMD)
        CP E
        JNZ $-3

        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
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
        DW 8,8+560 ;rotbar
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

        ORG #C000,pgcode1
       pgrotbarpregs=pgcode1
        LD HL,WASGSROTBARPRECODE
        LD DE,GSROTBARPRECODE
        LD BC,LENGSROTBARPRECODE
        LDIR

        LD A,gspgbarpic
        OUT (MPAG),A

        LD HL,wasscr
        LD DE,BARPIC
        LD BC,#1B00
        LDIR
        JP GSROTBARPREGO

WASGSROTBARPRECODE
       DISP GSROTBARPRECODE
GSROTBARPREGO
        CALL GENPICTOBMP

        XOR A
        LD (bar0left),A

;все копии делаем из экрана, лежащего в #8000
        LD HL,#10C0+BARPIC
        LD C,gspgbarpic
        LD B,12
GSBARPREP
        LD A,C
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD C,A
       PUSH BC,HL
       PUSH HL
        LD DE,BARLIEBMP+#4000
        CALL PICTOBARLIE
       POP HL
        LD DE,BARSTAYBMP+#4000
        CALL PICTOBARSTAY
        LD HL,BARLIEBLACK
        LD DE,BARLIEBLACK+1
        LD BC,BARLIEBMP-BARLIEBLACK-1
        LD (HL),L
        LDIR
       IFN barblack
        LD HL,BARSTAYBLACK
        LD DE,BARSTAYBLACK+1
        LD BC,BARSTAYBMP-BARSTAYBLACK-1
        LD (HL),L
        LDIR
       ENDIF
        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A
       POP HL,BC
        LD A,L
        SUB 64
        LD L,A
        JNC $+6
        LD A,H
        SUB 8
        LD H,A
        DJNZ GSBARPREP
        RET

GENPICTOBMP
        LD HL,TPICTOBMP ;%lrlrlrlr->rlrrrlll
GENPICTOBMP0
       DUP 4
        RLC L
        RLA ;будет %????llll
        RLC L
        RL C ;будет %????rrrr
       EDUP
       DUP 3
        RRA
        RR (HL)
       EDUP
       DUP 3
        RR C
        RR (HL)
       EDUP
        RRA
        RR (HL)
        RR C
        RR (HL)
        INC L
        JNZ GENPICTOBMP0
        RET

PICTOBARLIE
;генерируем битмап 256x16 - по 2 пикс от каждого пикс картинки
;0-й байт строки соответствует концу, 1,2,3... - началу
        LD BC,TPICTOBMP+barhgt
      LD A,(bar0left)
      OR A
      JP Z,PICTOBARLIELEFT
      JP PICTOBARLIERIGHT

       MACRO MPICTOBARLIE
PICTOBARLIE\0
       IF0 \1 ;left
        LD A,L
        OR 31
        LD L,A
       ENDIF
PICTOBAR0\0
       PUSH BC
       PUSH HL
        LD C,0 ;%l0l0l0l0
PICTOBAR1\0
       PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,'BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL) ;bright
        RLA ;%0000000b
        PUSH AF
       DUP 3
        ADD A,A
        RLC (HL)
        RLA ;будет %0b0p0p0p
       EDUP
       IF0 \1 ;left
        ADD A,A
       ENDIF
        LD HX,A
        POP AF
       DUP 3
        ADD A,A
        RLC (HL)
        RLA ;будет %0b0i0i0i
       EDUP
       IF0 \1 ;left
        ADD A,A
       ENDIF
        LD LX,A
       POP HL
        LD LY,8
PICTOBAR2\0
        LD A,C
       IFN \1 ;left
        AND %01010101
        ADD A,A
        RLC (HL)
       ELSE
        AND %10101010
        RRA
        RRC (HL)
       ENDIF
        JC $+6
        OR HX ;paper %0p0p0p0p ;right: %p0p0p0p0
        JR $+4
        OR LX ;ink %0i0i0i0i   ;left: %i0i0i0i0
        LD C,A ;%lrlrlrlr
        LD A,(BC) ;rlrrrlll
        LD (DE),A
       IFN \1 ;left
        INC E
       ELSE
        DEC E
       ENDIF
        DEC LY
        JNZ PICTOBAR2\0
       IFN \1 ;left
        INC L
        DEC E
        INC E
       ELSE
        DEC L
        INC E
        DEC E
       ENDIF
        JP NZ,PICTOBAR1\0
;черный пиксель в конце строки битмапа
        LD A,C
       IFN \1 ;left
        AND %01010101
        ADD A,A
        LD C,A ;%lrlrlrlr
       ELSE
        AND %10101010
        RRA
        LD C,A ;%lrlrlrlr
       ENDIF
        LD A,(BC) ;rlrrrlll
        LD (DE),A
       POP HL
        INC D
        CALL GSDHL
       POP BC
        DEC C
        JP NZ,PICTOBAR0\0
        RET
       ENDM

MPICTOBARLIE LEFT,1
MPICTOBARLIE RIGHT,0

PICTOBARSTAY
;генерируем битмап 256x16 - по 2 пикс от каждого пикс картинки
        LD C,32
      LD A,(bar0left)
      OR A
      JP Z,PICTOBARSTAYLEFT
      JP PICTOBARSTAYRIGHT

       MACRO MPICTOBARSTAY
PICTOBARSTAY\0
       IF0 \1 ;left
        DEC E ;#XXFF
       ENDIF
PICTOBARSTAY0\0
        LD B,8
PICTOBARSTAYCHR\0
       PUSH BC
       PUSH DE
       PUSH HL
        LD BC,TPICTOBMP ;C=%l0l0l0l0
        LD LY,barhgt
PICTOBARSTAY1\0
       PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,'BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL) ;bright
        RLA ;%0000000b
        PUSH AF
       DUP 3
        ADD A,A
        RLC (HL)
        RLA ;будет %0b0p0p0p
       EDUP
       IF0 \1 ;left
        ADD A,A
       ENDIF
        LD HX,A
        POP AF
       DUP 3
        ADD A,A
        RLC (HL)
        RLA ;будет %0b0i0i0i
       EDUP
       IF0 \1 ;left
        ADD A,A
       ENDIF
        LD LX,A
       POP HL
        LD A,C
       IFN \1 ;left
        AND %01010101
        ADD A,A
       ELSE
        AND %10101010
        RRA
       ENDIF
        RLC (HL)
        JC $+6
        OR HX ;paper %0p0p0p0p
        JR $+4
        OR LX ;ink %0i0i0i0i
        LD C,A ;%lrlrlrlr
        LD A,(BC) ;rlrrrlll
        LD (DE),A
        INC D
        CALL GSDHL
        DEC LY
        JP NZ,PICTOBARSTAY1\0
       POP HL
       POP DE
       IFN \1 ;left
        INC E
       ELSE
        DEC E
       ENDIF
       POP BC
        DEC B
        JP NZ,PICTOBARSTAYCHR\0
        INC L
        DEC C
        JP NZ,PICTOBARSTAY0\0
        RET
       ENDM

MPICTOBARSTAY LEFT,1
MPICTOBARSTAY RIGHT,0

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
LENGSROTBARPRECODE=$-WASGSROTBARPRECODE
       DISPLAY /T,LENGSROTBARPRECODE
wasscr
        INCBIN "gameover"
LENROTBARPREGS=$-#C000

        ORG #C000,pgcode2
       pgrotbargs=pgcode2
        LD HL,WASGSROTBARCODE
        LD DE,GSROTBARCODE
        LD BC,LENGSROTBARCODE
        LDIR
        JP GSROTBARGO

WASGSROTBARCODE
       DISP GSROTBARCODE
GSROTBARGO
        LD HL,GSNMI
        LD DE,#0066
        LD BC,GSNMI_LEN
        LDIR

        LD A,2 ;#C000: gspg2:0
        OUT (MPAGEX),A
        CALL GENBARSCRADDR

        XOR A
        LD (bar0left),A

        LD L,gspgbarpic
        LD DE,#BF80
GSGOBEG
        LD A,L
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        LD L,A
      PUSH DE,HL
      LD (BARY),DE
        LD HL,0
        LD (BARdata+BARdata_clr0),HL
        LD (BARdata+BARdata_out0),HL
        LD (BARdata+BARdata_clr1),HL
        LD (BARdata+BARdata_out1),HL
       XOR A
       LD (BARdata+BARdata_clrform),A
       LD (BARdata+BARdata_outform),A
        LD (BARdata+BARdata_clr0-bardatadelta),HL
        LD (BARdata+BARdata_out0-bardatadelta),HL
        LD (BARdata+BARdata_clr1-bardatadelta),HL
        LD (BARdata+BARdata_out1-bardatadelta),HL
       LD (BARdata+BARdata_clrform-bardatadelta),A
       LD (BARdata+BARdata_outform-bardatadelta),A
       LD A,70 ;254
       LD (BARa),A
GSLOOP
        CALL GSLOOPPP
        LD HL,BARa
        LD A,(HL)
        SUB 2 ;3 не успеет стереть
        LD (HL),A
        JNC GSLOOP
        LD (HL),0
        CALL GSLOOPPP
        CALL GSLOOPPP ;если последнее было 1
        LD HL,0
        LD (BARdata+BARdata_clr0),HL
        LD (BARdata+BARdata_clr1),HL
        LD (BARdata+BARdata_clr0-bardatadelta),HL
        LD (BARdata+BARdata_clr1-bardatadelta),HL
        CALL GSLOOPPPSCR
        CALL GSLOOPPPU ;печатаем непропечатавшееся
        CALL GSLOOPPPSCR
        CALL GSLOOPPPU
        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A
      POP HL,DE
        LD A,D
        SUB 16
        LD D,A
        JP NC,GSGOBEG

        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A ;как было
        CALL GSLOOPPP
        JR $-3

GSLOOPPPSCR
gsdrawscr=$+1
        LD A,1 ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A ;теперь рисуем в SCR1
                         ;потом дадим его ZX'у
        LD IX,BARdata
        JZ $+6
        LD IX,BARdata-bardatadelta
        LD (BARIX),IX
        RET
GSLOOPPP
        CALL GSLOOPPPSCR
        CALL BARGENBLACK
GSLOOPPPU
        CALL BAR

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
       JNZ GSQUIT

        LD IX,(BARIX)
        LD L,(IX+BARdata_clr0addr)
        LD H,(IX+BARdata_clr0addr+1)
       BIT 6,H
       JZ $+4
       SET 7,H ;pg1:1
        LD (GSNMIDMA1),HL
        LD L,(IX+BARdata_out0addr)
        LD H,(IX+BARdata_out0addr+1)
       BIT 6,H
       JZ $+4
       SET 7,H ;pg1:1
        LD (GSNMIDMA2),HL
        LD L,(IX+BARdata_clr1addr)
        LD H,(IX+BARdata_clr1addr+1)
       BIT 6,H
       JZ $+4
       SET 7,H ;pg1:1
        LD (GSNMIDMA3),HL
        LD L,(IX+BARdata_out1addr)
        LD H,(IX+BARdata_out1addr+1)
       BIT 6,H
       JZ $+4
       SET 7,H ;pg1:1
        LD (GSNMIDMA4),HL
        LD HL,GSNMIDMABEG
        LD (GSNMIVAR),HL
        XOR A
        OUT (DMA_HAD),A
        LD A,HX
        OUT (DMA_MAD),A
        LD A,LX
        OUT (DMA_LAD),A
        LD A,#80 ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"
        RET
GSQUIT
       POP AF
        RET

        INCLUDE "GSRBARP*",#C1

BARGENBLACK
;IX=BARdata[-bardatadelta]
        LD (BARGENBLACKSP),SP
        LD A,(IX+BARdata_outform)
        LD (IX+BARdata_clrform),A
        LD IY,BARGENBLACK0Q
        JP BARGENBLACK0PP
BARGENBLACK0Q
        LD IY,BARGENBLACK1Q
        JP BARGENBLACK1PP
BARGENBLACK1Q
BARGENBLACKSP=$+1
        LD SP,0
        RET

BARGENBLACK0PP
        LD E,(IX+BARdata_out0)
        LD D,(IX+BARdata_out0+1)
        LD C,(IX+BARdata_out0addr)
        LD B,(IX+BARdata_out0addr+1)
;DE=N=число записей (!=0) в источнике
;(берем из источника последние сгенерированные - начало стека)
        SRL D
        RR E ;DE=N=число записей (!=0) сгенерировать
        LD (IX+BARdata_clr0),E
        LD (IX+BARdata_clr0+1),D
        LD HL,BARclr0
        LD A,BARdata_clr0addr
        JP BARGENBLACKPP
BARGENBLACK1PP
        LD E,(IX+BARdata_out1)
        LD D,(IX+BARdata_out1+1)
        LD C,(IX+BARdata_out1addr)
        LD B,(IX+BARdata_out1addr+1)
;DE=N=число записей (!=0) в источнике
;(берем из источника последние сгенерированные - начало стека)
        SRL D
        RR E ;DE=N=число записей (!=0) сгенерировать
        LD (IX+BARdata_clr1),E
        LD (IX+BARdata_clr1+1),D
        LD HL,BARclr1
        LD A,BARdata_clr1addr
BARGENBLACKPP
        LD (BARGENBLACKIXd),A
        INC A
        LD (BARGENBLACKIXd1),A
        LD A,D
        OR E
        JP Z,BARGENBLACKQ
        LD A,HX
        CP 'BARdata
        JZ $+6
         LD A,H
         SUB 'bardatadelta
         LD H,A ;второй блок данных
        LD SP,HL
        LD H,B,L,C
        DEC DE
        LD A,E
        CPL
        AND 15 ;0 (N=0), 15 (N=1), 14 (N=2)...
        LD B,A
        ADD A,A
        ADD A,A
        ADD A,B
        LD (BARGENBLACK0-1),A
        LD A,E
       DUP 4
        SRL D
        RRA
       EDUP
        INC A
        LD BC,3
        DEC HL
        JR $
BARGENBLACK0
       DUP 16
        ADD HL,BC
        LD E,(HL)
        INC L
        LD D,(HL)
        PUSH DE
       EDUP
        DEC A
        JP NZ,BARGENBLACK0
        LD H,A,L,A
        ADD HL,SP
BARGENBLACKIXd=$+2
        LD (IX),L
BARGENBLACKIXd1=$+2
        LD (IX),H
BARGENBLACKQ
        JP (IY)

GENBARSCRADDR
        LD HL,#C000 ;HSB,LSB
GENBARSCRADDR0
        LD B,128
GENBARSCRADDR1
        LD A,L
        RRA
        RRA
        AND 31
        LD C,A
        LD A,H
        CPL
        AND #38
        RLCA
        RLCA
        OR C
        LD C,A
        LD A,H
        CPL
        AND 7
        OR #C0
        BIT 1,L
        JZ $+4
        OR #20
        LD (HL),A
        INC L
        LD (HL),C
        INC L
        DJNZ GENBARSCRADDR1
        INC H
        JNZ GENBARSCRADDR0
        RET

GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

;для #0066
GSNMI
        PUSH AF,HL
        LD HL,(GSNMIVAR) ;8/6/4/2
      ;IN A,(ZXDATRD)
      ;LD L,A
        DEC L
        LD A,(HL)
        OUT (DMA_MAD),A
        DEC L
        LD A,(HL)
        OUT (DMA_LAD),A
        LD (GSNMIVAR),HL
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"
        POP HL,AF
        RET
GSNMI_LEN=$-GSNMI

       DISPLAY $
        DS .(-$)
BARSECTG
        INCBIN "sectg"
BARSIN
        INCBIN "sin+"
       ENT
LENGSROTBARCODE=$-WASGSROTBARCODE
       DISPLAY /T,LENGSROTBARCODE
LENROTBARGS=$-#C000

        ORG #C000,pgeffects
       pgrotbarzx=pgeffects
       PUSH BC
       PUSH DE
        LD HL,WASROTBARCODE
        LD DE,ROTBARCODE
        LD BC,LENROTBARCODE
        LDIR
        JP ROTBARCODE

WASROTBARCODE
       DISP ROTBARCODE
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
        LD A,#14
        CALL ZXCL1800PG
        LD A,#15
        CALL ZXCL1800PG
        LD A,#16
        CALL ZXCL1800PG
        LD A,#17
        CALL ZXCL1800PG

        LD A,1 ;16C
        LD BC,#EFF7
        OUT (C),A

       HALT
BIGLOOP
       ;HALT
       IFN border
        XOR A
        OUT (254),A
       ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

        LD A,#17!#1D
        XOR #1D ;видим scr1, рисуем scr0
        LD ($-1),A ;теперь видим scr0, рисуем scr1
        LD (BARPG1),A
        DEC A
        LD (BARPG0),A
        setpga

;и ждем выключения:
        WN
        GD
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
        CALL VIEWBAR
       IFN border
        LD A,7
        OUT (254),A
       ENDIF
        JP BIGLOOP
QUIT
        LD A,2
        OUT (GSCOM),A ;конец эффекта
        HALT
        LD A,#1F ;по идее на обоих экранах одно и то же
        setpga
        LD HL,zxwasscr
        LD DE,#4000
        LD BC,#1B00
        LDIR ;то же, что видим, но 6912
        HALT
        LD A,#17
        setpga
        LD BC,#EFF7
        XOR A
        OUT (C),A
       ;LD HL,zxwasscr
       ;LD DE,#C000
       ;LD BC,#1B00
       ;LDIR ;то же, что видим, но 6912
        RET
       ;LD A,C_GRST
       ;OUT (GSCTR),A
       ;JR $

ZXCL1800PG
        setpga
        LD HL,#C000+#1800
        CALL ZXCL1800
        LD HL,#E000+#1800
ZXCL1800
        LD (ZXCLSP),SP
        LD SP,HL
        LD DE,0
        LD B,#18*2
ZXCL1800_0
        DUP 128/2
        PUSH DE
        EDUP
        DJNZ ZXCL1800_0
ZXCLSP=$+1
        LD SP,0
        RET

       MACRO FORCENMI
        LD A,C_GNMI
        OUT (GSCTR),A
       ENDM

       MACRO WAITDMA
        WN ;ждем подтверждения от NGS (он выслал байт)
        GD ;сброс признака
       ENDM

VIEWBAR
       DI
        LD (VIEWBARSP),SP
       LD HL,VIEWBARIM
       LD (IMVEC+#FF),HL
        LD H,0 ;данные <12K, поэтому точное HL=0 не надо
        LD A,(HL) ;1st byte is wrong
        LD SP,HL
        POP HL
        LD (viewbarclr0),HL
        POP HL
        LD (viewbarout0),HL
        POP HL
        LD (viewbarclr1),HL
        POP HL
        LD (viewbarout1),HL
       ;POP AF
       ;LD (viewbarclrform),A
       ;POP AF
       ;LD (viewbaroutform),A
      ;LD A,8
      ;OUT (GSDAT),A
        FORCENMI
BARPG0=$+1
        LD A,#1E
        setpga
viewbarclr0=$+1
        LD DE,0
        LD IX,VIEWBAR0OUT
        WAITDMA
        LD A,(0) ;первый байт неверен
;viewbarclrform=$+1
       ;LD A,0
       ;OR A
       ;JP NZ,BARVIEWBLACK2
        JP BARVIEWBLACK1
VIEWBAR0OUT
      ;LD A,6
      ;OUT (GSDAT),A
        FORCENMI
viewbarout0=$+1
        LD DE,0
        LD IX,VIEWBAR0Q
        WAITDMA
        LD H,0
        LD A,(HL) ;первый байт неверен
;viewbaroutform=$+1
       ;LD A,0
       ;OR A
       ;JP NZ,BARVIEW2
        JP BARVIEW1S ;она быстрее
VIEWBAR0Q
      ;LD A,4
      ;OUT (GSDAT),A
        FORCENMI
BARPG1=$+1
        LD A,#1F
        setpga
viewbarclr1=$+1
        LD DE,0
        LD IX,VIEWBAR1OUT
        WAITDMA
        LD A,(0) ;первый байт неверен
       ;LD A,(viewbarclrform)
       ;OR A
       ;JP NZ,BARVIEWBLACK2
        JP BARVIEWBLACK1
VIEWBAR1OUT
      ;LD A,2
      ;OUT (GSDAT),A
        FORCENMI
viewbarout1=$+1
        LD DE,0
        LD IX,VIEWBARQ
        WAITDMA
        LD H,0
        LD A,(HL) ;первый байт неверен
VIEWBARSP=$+1
        LD SP,0
       EI
       ;LD A,(viewbaroutform)
       ;OR A
       ;JP NZ,BARVIEW2
        JP BARVIEW1
VIEWBARQ
        JR VIEWBARQ
VIEWBARIM
        LD SP,(VIEWBARSP)
        LD HL,IMER
        LD (IMVEC+#FF),HL
        JP (HL) ;делает EI

BARVIEWBLACK1
;DE=N=число записей (!=0)
      IFN 1
        DEC DE
        LD A,E
        CPL
        AND 31 ;0 (N=0), 31 (N=1), 30 (N=2)...
        ADD A,A
        LD (BARVIEWBLACK10-1),A
        LD A,E
       DUP 5
        SRL D ;будет 0
        RRA
       EDUP
        INC A
        JZ BARVIEWBLACK1Q
        LD B,A
        JR $
BARVIEWBLACK10
       DUP 32
        POP HL
        LD (HL),D
       EDUP
        DJNZ BARVIEWBLACK10
      ELSE
        LD A,D
        OR E
        JZ BARVIEWBLACK1Q
BVB1POP HL
       LD A,H
       CP #C0
       JC $
        LD (HL),1
        DEC DE
        LD A,D
        OR E
        JNZ BVB1
      ENDIF
BARVIEWBLACK1Q
        JP (IX)
BARVIEWBLACK2
;DE=N=число записей (!=0)
        DEC DE
        LD A,E
        CPL
        AND 15 ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
        ADD A,A
        LD (BARVIEWBLACK20-1),A
        LD A,E
       DUP 4
        SRL D ;будет 0
        RRA
       EDUP
        INC A
        JZ BARVIEWBLACK2Q
        LD B,A
        JR $
BARVIEWBLACK20
       DUP 16
        POP HL
        LD (HL),D
        DEC H
        LD (HL),D
       EDUP
        DJNZ BARVIEWBLACK20
BARVIEWBLACK2Q
        JP (IX)
BARVIEW1
;DE=N=число записей (!=0)
        DEC DE
        LD A,E
        CPL
        AND 15
        LD B,A ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
       ADD A,A
        ADD A,B
        LD (BARVIEW10-1),A
        LD A,E
       DUP 4
        SRL D
        RRA
       EDUP
        INC A
        JZ BARVIEW1Q
        LD B,A
        JR $
BARVIEW10
       DUP 16
       ;POP AF
       ;POP HL
       ;LD (HL),A
       LD A,(HL)
       LD A,(HL)
       LD E,(HL)
       LD D,(HL)
       LD (DE),A
       EDUP
        DJNZ BARVIEW10
BARVIEW1Q
        JP (IX)
BARVIEW1S
;DE=N=число записей (!=0)
        DEC DE
        LD A,E
        CPL
        AND 31
        LD B,A ;0 (N=0), 31 (N=1), 30 (N=2)...
        ADD A,A
        ADD A,B
        LD (BARVIEW10S-1),A
        LD A,E
       DUP 5
        SRL D
        RRA
       EDUP
        INC A
        JZ BARVIEW1QS
        LD B,A
        JR $
BARVIEW10S
       DUP 32
        POP AF
        POP HL
        LD (HL),A
       EDUP
        DJNZ BARVIEW10S
BARVIEW1QS
        JP (IX)
BARVIEW2
;DE=N=число записей (!=0)
        DEC DE
        LD A,E
        CPL
        AND 15
        LD B,A ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
        ADD A,A
        ADD A,B
        LD (BARVIEW20-1),A
        LD A,E
       DUP 4
        SRL D
        RRA
       EDUP
        INC A
        JZ BARVIEW2Q
        LD B,A
        JR $
BARVIEW20
       DUP 16
        POP DE
        POP HL
        LD (HL),D
        DEC H
        LD (HL),E
       EDUP
        DJNZ BARVIEW20
BARVIEW2Q
        JP (IX)
zxwasscr
        INCBIN "gameover"
       ENT
LENROTBARCODE=$-WASROTBARCODE
LENROTBARZX=$-#C000

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "ROTBARZXC"
        DW #C000
        DW LENROTBARZX
        DB pgrotbarzx
        DW #C000

        DB "ROBPREGSC"
        DW #C000
        DW LENROTBARPREGS
        DB pgrotbarpregs
        DW #C000

        DB "ROTBARGSC"
        DW #C000
        DW LENROTBARGS
        DB pgrotbargs
        DW #C000
        INCLUDE "SAVEOBJ*",#C0

