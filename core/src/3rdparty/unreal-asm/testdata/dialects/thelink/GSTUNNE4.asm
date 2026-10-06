        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
border=0

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

TUNNELCODE=#7800

resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports.asm"
GSPROG=#5830
GSSTACK=#5830
GSTUNNELCODE=#5900

gspgcode=#10
gspgdata=#20

GSpglookup=2
GSLOOKUP=#C000
GSpgprog1=#82                   ;16K*5: #82,03,83,04,85
GSpgprog2=#06                   ;16K*5: #06,86,07,87,08
GSPLAN2RTMP=#0000
;текстура=0000..3FFF
GSSCR1=#4000
GSSCR2=#4C00
GSTCOLOR=#7878

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
        OUT (GSCOM),A           ;не помогает
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
        JR NZ,GSLOA20
;зачем-то #F5 (Busy on)
        SC #F5
        WC
;#13 Jump to Address
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA
;и ждем включения:
        WN
        GD
;
;шлем страницы данных в GS:
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA
;и ждем переключения:
        WN
        GD
;
;шлем страницы кода в GS:
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA
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
        LD A,high IMVEC
        LD I,A
        LD H,A
        LD D,A
        LD BC,256
        LD L,C
        LD E,B
        LD (HL),IMER
        LDIR
        IM 2

        LD A,pgmusic
        setpga                  ;заполняет curpg
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
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

        LD A,1                  ;#8000: gspg1:0
        OUT (MPAG),A
        LD A,2                  ;#C000: gspg2:0
        OUT (MPAGEX),A

        LD A,1                  ;SET DMA MODULE #1
        OUT (DMA_MOD),A

        LD E,3
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
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
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON"

        LD E,4
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
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
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON"

        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF"
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов?

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
        LD B,(HL)               ;время начала эффекта
        INC HL
        LD E,(HL)
        INC HL
        LD D,(HL)               ;время конца эффекта
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
        LD A,IXH
        setpga
        LD BC,256
        LDIR
        POP HL
        POP DE
        LD A,IXL
        setpga                  ;портит BC
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
        RET
zxtimings
        DW 896,896*2-20         ;tunnel
        ENT
LENZXRES=$-WASZXRES

WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE                 ;hl до прерывания
        PUSH AF
        PUSH BC
        EX AF,AF'
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
end

        ORG #C000,pgcode1
pgtunnelgs=pgcode1
        LD HL,WASGSTUNNELCODE
        LD DE,GSTUNNELCODE
        LD BC,LENGSTUNNELCODE
        LDIR

        LD HL,wasplan2r
        LD DE,GSPLAN2RTMP       ;временно
        CALL DEHR21
        LD A,GSpglookup
        OUT (MPAG),A
        LD HL,wastorus
        LD DE,#8000
        CALL DEHR21
        JP GSTUNNELGO
DEHR21
;HL=FROM
;DE=TO

        PUSH DE
        LD DE,6+8
        ADD HL,DE
        PUSH HL
        EXX
        POP HL
        POP DE
        LD B,6
Hfst__L1 DEC HL
        LD A,(HL)
        PUSH AF
        DJNZ Hfst__L1
        EXX
        LD DE,#1003
        LD C,#80
Hloop__L1 LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
Hxxtbit__L1 EXX
Hnxtbit__L1 SLA C
        CALL Z,CHL__L1
        JR C,Hloop__L1
        LD B,1
H2bit__L1 LD A,#40
H2bit0__L1 SLA C
        CALL Z,CHL__L1
        RLA
        JR NC,H2bit0__L1
        CP E
        JR C,H2lessE__L1
        ADD A,B
        LD B,A
        XOR D
        JR NZ,H2bit__L1
H2lessE__L1 ADD A,B
        CP 4
        JR Z,Hnpaked__L1
        ADC A,-1
LL5DB1__L1 CP 2
LL5DB3__L1 EXX
        LD C,A
        LD H,-1
        EXX
        JR C,H3bit__L1          ;B=1
        JR Z,LL5DE5__L1
        SLA C
        CALL Z,CHL__L1
        JR C,LL5DE5__L1
        LD A,#7F
        LD B,E
        DJNZ Hbits__L1          ;B=3 CALL GET2BITS
;B=1
LL5DCB__L1 DJNZ Hplain__L1
        LD B,A                  ;FC-FF
        SBC A,A
Hijnz__L1 SLA C
        CALL Z,CHL__L1
        RLA
        DEC A                   ;1.FD-FE
;2>F8
;3>F0
;4>E0
        INC B
        JR NZ,Hijnz__L1
        CP #E1
        JR NZ,$+4
        LD A,(HL)
        INC HL
        EXX
        LD H,A
        EXX
LL5DE5__L1 LD A,(HL)
        INC HL
LL5DE7__L1 EXX
        LD L,A
        ADD HL,DE
        LDIR
        JR Hxxtbit__L1
Hplain__L1 ADD A,6
        RLA
        LD B,A
Hldir__L1 LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
        EXX
        DJNZ Hldir__L1
        JR Hnxtbit__L1
Hnpaked__L1 SLA C
        CALL Z,CHL__L1
        LD A,D
        JR NC,Hbits__L1         ;B=3 GETBIT,Hplain
        LD A,(HL)
        INC HL
        CP D
        JR NC,LL5DB1__L1
        OR A
        JR Z,Hquit__L1
        EXX
        LD B,A
        EXX
        LD A,(HL)
        INC HL
        JR LL5DB3__L1
H3bit__L1 LD A,#3F
Hbits__L1 SLA C
        CALL Z,CHL__L1
        RLA
        JR NC,Hbits__L1
        DJNZ LL5DCB__L1
        JR LL5DE7__L1
Hquit__L1 EXX
        LD B,6
        POP AF
        LD (DE),A
        INC DE
        DJNZ $-3
;выходим. HL'=после упакованного блока
CHL__L1 LD C,(HL)
        INC HL
        RL C
        RET


WASGSTUNNELCODE
        DISP GSTUNNELCODE
GSTUNNELGO
        LD A,GSpgprog1
        OUT (MPAG),A
        LD A,GSpglookup
        OUT (MPAGEX),A

        LD BC,GSSCR1
        LD A,GSpgprog1
        CALL MKTUNNEL
        LD BC,GSSCR2
        LD A,GSpgprog2
        CALL MKTUNNEL
;теперь можно класть текстуру в #0000

        CALL GENTCOLOR
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
GSPLASMPRE0
        LD A,(DE)
        RLCA
        ADD A,(HL)
        RLCA
        XOR H
        RLCA
        RLCA
        LD (DE),A
        INC HL
        INC DE
        BIT 6,D
        JR Z,GSPLASMPRE0
        LD B,10
GSPLASM
        PUSH BC
        LD HL,#0000
GSPLASM0
        LD A,(HL)
        PUSH HL
        INC L
        ADC A,(HL)              ;+noise
        RRA
        INC H
        RES 6,H
        ADC A,(HL)              ;+noise
        RRA
        POP HL
        LD (HL),A
        INC HL
        BIT 6,H
        JR Z,GSPLASM0
        POP BC
        DJNZ GSPLASM
        LD HL,#0000
GSPLASM1
        LD A,(HL)
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,GSTCOLOR
        LD (HL),A
        INC HL
        BIT 6,H
        JR Z,GSPLASM1

GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1
;потом дадим его ZX'у
        LD (GSTUNNELSP),SP
        LD IX,GSTUNNELQ
        LD A,GSpgprog1
        JR Z,$+4
        LD A,GSpgprog2
        OUT (MPAG),A
GSSPEED=$+1
        LD C,#C0
GSdVU=$+1
        LD DE,0                 ;dVU
        LD A,E
        ADD A,5
        LD E,A
gsspeedtime=0
        IF 0
GSrhythm=$+1
        LD A,0
        ADD A,4
        LD (GSrhythm),A
        JR NC,GSNSPEED
        IF gsspeedtime
        CALL GSRND
        AND #1F
        ADD A,#20
        LD (GSSPEEDTIME),A
        CALL GSRND
        ENDIF
;AND #80
;SUB #40 ;+-64
;RLA
;SBC A,A
;ADD A,#80 ;-128/+127
        AND #C0
        JR NZ,$+4
        LD A,#7F                ;-128..+127
        LD C,A
GSNSPEED
        IF gsspeedtime
GSSPEEDTIME=$+1
        LD A,0
        DEC A
        LD (GSSPEEDTIME),A
        LD A,C
        JR NZ,$+3
        XOR A
        ENDIF
;LD A,C
;OR A
;JZ GSNSLOW
;JP M,$+5
;SUB 2
;INC A
GSNSLOW
;LD (GSSPEED),A
        ENDIF
        LD A,C
        RLA
        SBC A,A
        LD B,A
;LD A,C
;ADD A,32 ;правильное округление
;AND #C0
;LD C,A
GSangle=$+1
        LD HL,0
        ADD HL,BC
        LD (GSangle),HL
        XOR A
        SUB H
        AND #3F
        LD D,A
        ADD HL,HL
        ADD HL,HL
        LD A,H
        LD HL,GSLOOKUP
        AND #FE
        LD L,A
        LD (GSdVU),DE
        LD SP,HL
        JP #8000
GSTUNNELQ
GSTUNNELSP=$+1
        LD SP,0
;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0                         ;ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF"
;GS ожидает обнуления (ZX запросит данные)
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT

        XOR A
        OUT (DMA_HAD),A
        LD A,(gsdrawscr)
        OR A
        LD A,high GSSCR1+#80
        JR Z,$+4
        LD A,high GSSCR2+#80
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON"
        JP GSLOOP

GSRND
        PUSH BC
        PUSH HL
GSRNDSEED=$+1
        LD HL,0
        LD B,H
        LD C,L
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,BC
        LD BC,20981
        ADD HL,BC
        LD (GSRNDSEED),HL
        LD A,H
        POP HL
        POP BC
        RET

GENTCOLOR
;строим левые пиксели - зависят от L
        LD H,high GSTCOLOR
        LD C,8
GENTCOL0
        LD L,GSTCOLOR
        LD DE,TCOLORS
        LD B,8
GENTCOL1
        LD A,(DE)
        LD (HL),A
        INC DE
        INC L
        DJNZ GENTCOL1
        INC H
        DEC C
        JR NZ,GENTCOL0
;строим правые пиксели - зависят от H
        LD DE,TCOLORS
        LD H,high GSTCOLOR
        LD C,8
GENTCOL2
        LD L,GSTCOLOR
        LD B,8
GENTCOL3
;bright+nobright=nobright+nobright
;отсюда следует:
;если нужен bright, то он уже есть в (HL)
;если его нет в (DE), то он не нужен
        LD A,(DE)
        ADD A,A
        ADD A,A
        JR C,$+4
        RES 6,(HL)              ;nobright
        ADD A,A
        OR (HL)
        LD (HL),A
        INC L
        DJNZ GENTCOL3
        INC DE
        INC H
        DEC C
        JR NZ,GENTCOL2
        RET

TCOLORS
;DB 0,1,64+1,5,7,64+7,64+6,64+4
;DB 0,1,64+1,2,3,5,6,7
        DB 1,1,64+1,2,3,5,7,7

MKTUNNEL
        LD IXL,A
        OUT (MPAG),A
        LD DE,GSPLAN2RTMP
        LD A,B
        ADD A,#0C
        LD IXH,A
        LD HL,#8000
MKTUNNEL0
        LD (HL),33
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;da
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;dR
        INC HL
        PUSH BC
        PUSH DE
        EX DE,HL
        LD HL,TUNNELCODESAMPLE
        LD BC,TUNNELCODESAMPLELEN
        LDIR
        EX DE,HL
        POP DE
        LD (HL),#4E             ;ld c,(hl) ;левый пикс.=LSB
        INC HL
        LD (HL),33
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;da
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;dR
        INC HL
        PUSH DE
        EX DE,HL
        LD HL,TUNNELCODESAMPLE
        LD BC,TUNNELCODESAMPLELEN
        LDIR
        EX DE,HL
        POP DE
        POP BC
        LD (HL),#46             ;ld b,(hl) ;правый пикс.=HSB
        INC HL
        LD (HL),#0A             ;ld a,(bc)
        INC HL
        LD (HL),#32
        INC HL
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD A,H
        CP #BF
        JR NZ,MKTUNNELnPG
        LD (HL),#3E             ;ld a,PG
        INC HL
        LD A,IXL
        RLCA
        INC A
        RRCA
        LD IXL,A
        LD (HL),A
        INC HL
        LD (HL),#D3             ;out (MPAG),a
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
MKTUNNELnPG
        INC BC
        LD A,B
        CP IXH
        JR NZ,MKTUNNEL0
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)
        RET

TUNNELCODESAMPLE
        ADD HL,SP
        LD SP,HL
        POP HL
        ADD HL,DE
        RES 6,H
TUNNELCODESAMPLELEN=$-TUNNELCODESAMPLE

GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0
        ENT
LENGSTUNNELCODE=$-WASGSTUNNELCODE
        DISPLAY /T,LENGSTUNNELCODE

wastorus
        INCBIN "torusr.p"
wasplan2r
        INCBIN "dplan2r.p"
LENTUNNELGS=$-#C000

        ORG #C000,pgeffects
pgtunnelzx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASTUNNELCODE
        LD DE,TUNNELCODE
        LD BC,LENTUNNELCODE
        LDIR
        JP TUNNELCODE

WASTUNNELCODE
        DISP TUNNELCODE
        POP HL
        LD (zxendtime),HL
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

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
        LD HL,#4000
        LD DE,#4001
        LD BC,#17FF
        LD (HL),#0F
        LDIR
        LD HL,#C000
        LD DE,#C001
        LD BC,#17FF
        LD (HL),#0F
        LDIR
        LD A,#10                ;noturbo
        LD BC,#EFF7
        OUT (C),A

        HALT
        LD HL,#C9FB
        LD (#5F5F),HL
        LD HL,IMVEC
        LD BC,256
        LD D,H
        LD E,B
        LD (HL),#5F
        LDIR
        JP BIGLOOP
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта
        HALT
        LD HL,IMVEC
        LD BC,256
        LD D,H
        LD E,B
        LD (HL),IMER
        LDIR
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

BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

;и ждем выключения:
        WN
        GD
;
zxendtime=$+1
        LD DE,0
        LD HL,(timer)
        OR A
        SBC HL,DE
        JR NC,QUIT
        LD A,#7F
        IN A,(#FE)
        RRA                     ;space
        JR NC,QUIT              ;можно жать RESET
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
        WN
        GD
;
        HALT
        LD HL,#0299             ;298..A
        DEC HL
        LD A,H
        OR L
        JR NZ,$-3
        DI
        LD (PUSHKASP),SP
;LD H,0 ;уже 0
        LD A,(HL)               ;1ST BYTE DISCARDED
        LD BC,#7FFD
        EXX
        LD B,24
        LD DE,32
loopmcscr=$+3
        LD IX,#5820             ;линия 0: видим scr1, рисуем scr0
        LD A,IXH
        XOR #80
        LD IYH,A
;LD (loopmcscr),A
        LD IYL,E                ;#20
        RLA
        SBC A,A
        AND 8
        OR #17                  ;тот же экран, что в IY
PUSHKA0
        EXX
        LD SP,IX                ;выводим scr0(1)
        OUT (C),A               ;видим scr1(0)
;51
;+0
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        NOP
        NOP
        LD SP,IY
        OUT (C),A               ;37
;-11
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        LD SP,IX
        EXX
        ADD IX,DE
        EXX
        OUT (C),A               ;52
;-7
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        LD SP,IY
        EXX
        ADD IY,DE
        EXX
        OUT (C),A               ;52
;-3
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        EXX
        DEC B
        JP NZ,PUSHKA0
PUSHKASP=$+1
        LD SP,0
        EI
        IF border
        LD A,R
        OUT (254),A
        ENDIF
        CALL IMER
        JP BIGLOOP
        ENT
LENTUNNELCODE=$-WASTUNNELCODE
LENTUNNELZX=$-#C000

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB 'TUNNELZXC'
        DW #C000
        DW LENTUNNELZX
        DB pgtunnelzx
        DW #C000

        DB 'TUNNELGSC'
        DW #C000
        DW LENTUNNELGS
        DB pgtunnelgs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"


