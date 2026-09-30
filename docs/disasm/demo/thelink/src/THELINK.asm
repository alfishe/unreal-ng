
;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
pgdata6=#96
pgdata7=#97
pgdata8=#11
pgdata9=#13
pgdata10=#14

pgcode1=#52
pgcode2=#55
pgcode3=#30
pgcode4=#31
pgcode5=#32
pgcode6=#33
pgcode7=#34
pgcode8=#35
pgcode9=#36
pgcode10=#37

;ZX data:
pgmusic=#54
pgeffects=#B0 ;#B0..#B7, #D0

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

gspgcode=#10
gspgdata=#20

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

        ORG #7800
begin
GO
        LD SP,#6000
        EI
        HALT
        XOR A
        OUT (#FE),A
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        LD A,#17
        LD HL,#D800
        LD DE,#D801
        LD BC,767
        LD (HL),L
        LDIR

        LD A,pgcode10
        setpga
        LD HL,WASGSRESET
        LD DE,#C000
        LD BC,LENGSRESET
        LDIR
        LD A,pgeffects+#20
        setpga
        LD HL,WASCREDITS
        LD DE,#C000
        LD BC,LENCREDITS
        LDIR

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
        LD A,pgdata6
        CALL GSDMASENDPG
        LD A,pgdata7
        CALL GSDMASENDPG
        LD A,pgdata8
        CALL GSDMASENDPG
        LD A,pgdata9
        CALL GSDMASENDPG
        LD A,pgdata10
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
        LD A,pgcode3
        CALL GSDMASENDPG
        LD A,pgcode4
        CALL GSDMASENDPG
        LD A,pgcode5
        CALL GSDMASENDPG
        LD A,pgcode6
        CALL GSDMASENDPG
        LD A,pgcode7
        CALL GSDMASENDPG
        LD A,pgcode8
        CALL GSDMASENDPG
        LD A,pgcode9
        CALL GSDMASENDPG
        LD A,pgcode10
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

        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG
        LD A,#16
        CALL CL1800PG
        LD A,#17
        CALL CL1800PG

        LD HL,wasmedieval
        LD DE,#4000
        LD BC,6144
        LDIR
        HALT
        LD BC,768
        LDIR

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
        CALL ZXRESNEXTPG
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
        DW 896,896*2-14 ;tunnel
        DW 896*2,896*3-14 ;rotate
        DW 896*3,896*3+560 ;rotbar
        DW 896*4-28-80,896*4-28 ;mulbar
        DW 0,0
        DW 896*4,896*5-14 ;hedge
        DW 896*5,896*5+448-14 ;baba
        DW 896*5+448,896*6+448 ;tex
ZXRESNEXTPG
        INC A
        BIT 3,A
        RET Z
        ADD A,#18
        RET
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

WASGSRESET
       DISP #C000
        LD HL,GSRESET
        LD DE,#5C00
        LD B,1
        PUSH DE
        LDIR
        RET
GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0
       ENT
LENGSRESET=$-WASGSRESET

WASCREDITS
       DISP #C000
        LD HL,credits
        LD DE,#4000
        LD BC,#1800
        LDIR
        HALT
        LD BC,#300
        LDIR
        JR $
credits
        INCBIN "credits"
       ENT
LENCREDITS=$-WASCREDITS
wasmedieval
        INCBIN "medieval"
end

        ORG #C000,pgdata1
        INCBIN "kishki.C"
        ORG #C000,pgdata2
        INCBIN "baba.C"
        ORG #C000,pgdata3
        INCBIN "baba.0"
        DS -$ ;для маски
        ORG #C000,pgdata4
        INCBIN "mill.C"
        ORG #C000,pgdata5
        INCBIN "mill.0"
        ORG #C000,pgdata6
        INCBIN "girl13.2"
        ORG #C000,pgdata7
        INCBIN "blugr.C"
        ORG #C000,pgdata8
        INCBIN "blugr.0"
        ORG #C000,pgdata9
        INCBIN "blugr.1"
        ORG #C000,pgdata10
        INCBIN "blugr.2"

        ORG #C000,pgcode1
        INCBIN "ROTPREGS"
        ORG #C000,pgcode2
        INCBIN "ROBPREGS"
        ORG #C000,pgcode3
        INCBIN "TUNNELGS"
        ORG #C000,pgcode4
        INCBIN "ROTATEGS"
        ORG #C000,pgcode5
        INCBIN "ROTBARGS"
        ORG #C000,pgcode6
        INCBIN "MULBARGS"
        ORG #C000,pgcode7
        INCBIN "HEDGEGS"
        ORG #C000,pgcode8
        INCBIN "BABAGS"
        ORG #C000,pgcode9
        INCBIN "TEXGS"
        ORG #C000,pgcode10


        ORG #C000,pgeffects
        INCBIN "TUNNELZX"
        ORG #C000,pgeffects+1
        INCBIN "ROTATEZX"
        ORG #C000,pgeffects+2
        INCBIN "ROTBARZX"
        ORG #C000,pgeffects+3
        INCBIN "MULBARZX"
        ORG #C000,pgeffects+4
        INCBIN "HEDGEZX1"
        ORG #C000,pgeffects+5
        INCBIN "HEDGEZX2"
        ORG #C000,pgeffects+6
        INCBIN "BABAZX"
        ORG #C000,pgeffects+7
        INCBIN "TEXZX"
        ORG #C000,pgeffects+#20

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG #5CDD
        DB "THE LINKB"
        INCLUDE "m2hr*",#C5

