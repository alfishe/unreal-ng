; The Link (invdemo, 2009) by Alone Coder - the demo's main block (#7800 .. blockEnd)
;
; A sjasmplus port of THELINK.asm (../../src/THELINK.asm, ALASM) without the page part: the
; ORG #C000,page + INCBIN lines are the loader's job (loader.asm). Assembled, it must be byte
; for byte the block ALASM leaves at #7800 (build.py checks it against the reference digest).
;
; ALASM -> sjasmplus: LD H,A,D,A is two LDs, PUSH AF,BC two PUSHes, JNZ / JZ / JC / JNC are
; JR cc, 'label is the high byte, .label the low byte, HX / LX / HY are IXH / IXL / IYH, EXA is
; EX AF,AF'. The gsports macros SD / SC / WC / WD / WN / GD are spelled out below.

        DEVICE NOSLOT64K

; NeoGS ports (gsports.asm)
GSCOM   EQU #BB
GSDAT   EQU #B3
GSCTR   EQU #33
C_GRST  EQU #80
MPAG    EQU #00
MPAGEX  EQU #10
ZXCMD   EQU #01
ZXDATWR EQU #03
ZXSTAT  EQU #04
VOL1    EQU #06
VOL2    EQU #07
VOL3    EQU #08
VOL4    EQU #09
VOL5    EQU #16
VOL6    EQU #17
VOL7    EQU #18
VOL8    EQU #19
GSCFG0  EQU #0F
M_NOROM EQU 1
M_RAMRO EQU 2
M_EXPAG EQU 8
M_CKSEL0 EQU #10
M_CKSEL1 EQU #20
C_10MHZ EQU #30
C_24MHZ EQU #00
DMA_MOD EQU #1B
DMA_HAD EQU #1C
DMA_MAD EQU #1D
DMA_LAD EQU #1E
DMA_CST EQU #1F

        MACRO SD val
        LD A,val
        OUT (GSDAT),A
        ENDM
        MACRO SC val
        LD A,val
        OUT (GSCOM),A
        ENDM
        MACRO WC            ; wait until the card takes the command
.loop   IN A,(GSCOM)
        RRCA
        JR C,.loop
        ENDM
        MACRO WD            ; wait until the card takes the data byte
.loop   IN A,(GSCOM)
        RLCA
        JR C,.loop
        ENDM
        MACRO WN            ; wait for a data byte from the card
.loop   IN A,(GSCOM)
        RLCA
        JR NC,.loop
        ENDM
        MACRO GD
        IN A,(GSDAT)
        ENDM

;GS data in ZX memory:
pgdata1 =#90
pgdata2 =#91
pgdata3 =#92
pgdata4 =#94
pgdata5 =#95
pgdata6 =#96
pgdata7 =#97
pgdata8 =#11
pgdata9 =#13
pgdata10=#14

pgcode1 =#52
pgcode2 =#55
pgcode3 =#30
pgcode4 =#31
pgcode5 =#32
pgcode6 =#33
pgcode7 =#34
pgcode8 =#35
pgcode9 =#36
pgcode10=#37

;ZX data:
pgmusic  =#54
pgeffects=#B0 ;#B0..#B7, #D0

resbuf    =#BD00
IMVEC     =#BE00
curpg     =#BF01
timer     =#BF02
ZXRESIDENT=#BF04
IMER      =#BFBF
outimstack=#5F00

GSPROG =#5830
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
        OUT (GSCOM),A
        HALT
        HALT
        DI
        LD DE,#4092
        SD E
        SC #18
        WC
        SD D
        WD
        SD 0
        SC #19
        WC
        SC #38
        WC
        SC #D2
        WC

        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
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
        JR NZ,GSLOA20
        SC #F5
        WC
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

        LD A,3
        OUT (GSCOM),A
        WN
        GD

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
        OUT (GSCOM),A
        WN
        GD

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
        OUT (GSCOM),A
        WN
        GD

        LD HL,WASZXRES
        LD DE,ZXRESIDENT
        LD BC,LENZXRES
        LDIR
        LD HL,WASIMER
        LD DE,IMER
        LD BC,LENIMER
        LDIR
        LD A,high IMVEC
        LD I,A
        LD H,A
        LD D,A
        LD BC,256
        LD L,C
        LD E,B
        LD (HL),low IMER
        LDIR
        IM 2

        LD A,pgmusic
        setpga
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
        LD D,H
        LD E,L
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

        LD A,1
        OUT (MPAG),A
        LD A,2
        OUT (MPAGEX),A

        LD A,1
        OUT (DMA_MOD),A

        LD E,3
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

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
        LD A,#80
        OUT (DMA_CST),A
        OUT (ZXDATWR),A

        LD E,4
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A
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
        LD A,#80
        OUT (DMA_CST),A
        OUT (ZXDATWR),A

        LD E,5
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A
        OUT (DMA_CST),A
        OUT (ZXDATWR),A
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3

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

WASZXRES
        DISP ZXRESIDENT
ZXRESIDENTGO
ZXRES0
        setpga
        PUSH AF
        LD C,(HL)
        INC HL
        LD B,(HL)
        INC HL
        LD E,(HL)
        INC HL
        LD D,(HL)
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
        LD A,IXH
        setpga
        LD BC,256
        LDIR
        POP HL
        POP DE
        LD A,IXL
        setpga
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
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
        EX (SP),HL
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE
        PUSH AF
        PUSH BC
        EX AF,AF'
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
        EX AF,AF'
        POP BC
        POP AF
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
        XOR A
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
        INCBIN "credits.C"
        ENT
LENCREDITS=$-WASCREDITS
wasmedieval
        INCBIN "medieval.C"
blockEnd

        SAVEBIN "thelink_main.bin",begin,blockEnd-begin
