border=0

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

MULBARCODE=#7800

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
GSMULBARCODE=#5900

gspgcode=#10
gspgdata=#20

timedefect=164

GSpgdata=2
gsscr=#6000 ;<#8000 и кратно #800
gsatr=#7800
gsbars=#8000-(3*12) ;скорость, Y на экране, Y графики
newgfx=#4000 ;2 набора. переключать или копировать?
gfx=newgfx+#200
newatr=gfx+#200
atr=newatr+#200
newgfx2=newgfx+#800
gstdhlGS=#4C00
gstdhlZX=#4E00 ;концы строк

;commands=#8000 ;2 набора
 GScmd1=#0000
 GScmd2=#2000
GSpgfuture=2
 futurecommands=#C000 ;1 набор

CMDBLACKTIME=204+15
CMDBLACKLEN=3 ;cmd, addr
CMDGFXTIME=428+15
CMDGFXLEN=3+32 ;cmd, addr, data
CMDDUMMYTIME=209+15

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

       ;LD A,#14
       ;CALL CL1800PG
       ;LD A,#15
       ;CALL CL1800PG

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
        DW 8,8+80 ;mulbar
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
       pgmulbargs=pgcode1
        LD HL,WASGSMULBARCODE
        LD DE,GSMULBARCODE
        LD BC,LENGSMULBARCODE
        LDIR

        LD A,GSpgdata
        OUT (MPAG),A
        LD HL,wasscr
        LD DE,gsscr
        LD BC,#1B00
        LDIR
        JP GSMULBARGO
WASGSMULBARCODE
       DISP GSMULBARCODE
GSMULBARGO
        LD A,GSpgfuture
        OUT (MPAGEX),A

        CALL GENDHL

        LD HL,gsscr
        LD C,#C0+24
GSSCRFLIP
        LD D,H
        LD A,L
        OR 31
        LD E,A
        LD B,16
GSSCRFLIP0
        LD A,(HL)
        EXA
        LD A,(DE)
        LD (HL),A
        EXA
        LD (DE),A
        INC L
        DEC E
        DJNZ GSSCRFLIP0
        LD DE,16
        ADD HL,DE
        DEC C
        JNZ GSSCRFLIP

;инициализация
        LD HL,gsbars
        XOR A
gsbarsini0 ;
        LD (HL),0 ;dy
        INC L
        LD (HL),A ;Y бара
        INC L
        LD (HL),A ;Y графики бара
        ADD A,16
        INC L
        JNZ gsbarsini0
;нижний бар запускаем сразу (dy=1):
        DEC L,L,L
        INC (HL)

        LD DE,GScmd1
        LD IX,newgfx
        CALL gsdrawbars
        CALL gsmovebars

GSLOOP
gsdrawscr=$+1
        LD A,1 ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A ;теперь рисуем в SCR1
                         ;потом дадим его ZX'у
        CALL GSEFFECT

;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0ADD HL,DE
       ;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JNZ GSWAIT0 ;48t

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
        LD A,'GScmd1
        JZ $+4
        LD A,'GScmd2
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80 ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A ;признак для ZX "DMA ON"
        JP GSLOOP

;кадр
GSEFFECT
        LD HL,newgfx
        LD DE,gfx
        LD BC,#200
        LDIR
        LD HL,newatr
        LD DE,atr
        LD BC,#200
        LDIR

        LD IX,newgfx
gscommands=$+1
        LD DE,GScmd1
        LD A,D
        XOR 'GScmd1!'GScmd2
        LD (gscommands+1),A

        CALL gsdrawbars
        CALL gsmovebars

;выводим все атрибуты atr[line] (не newatr!!!) в начале кадра:
        EXX
        LD LX,0 ;line
        LD A,HX
        ADD A,6
        LD HX,A ;atr+line
;de=curatr (в начале экрана не имеет значения)
        LD HL,224*80-timedefect ;t
gsgenatr0
;если atr[line]!=curatr или line&7=0:
        LD C,(IX)
        INC HX
        LD B,(IX) ;atr[line]
        DEC HX
        EXD
       ;or a
        SBC HL,BC
        LD H,B,L,C ;curatr=atr[line]
        EXD
        JNZ gsgenatrcmd
        LD A,LX
        AND 7
        JNZ genatrncmd
gsgenatrcmd ;
;ложим команду CMDBLACK (если atr[line]&#7fff==0) или CMDGFX
        LD A,D ;'atr[line]
        ADD A,A
        LD A,CMDBLACK
        LD BC,-CMDBLACKTIME
        JZ genatrblack
        LD A,CMDGFX
        LD BC,-CMDGFXTIME
genatrblack ;
        PUSH DE ;atr[line]
        ADD HL,BC ;t-=tcmd ;нельзя портить Z
        EXX
        POP HL
        LD (DE),A
        INC DE
       EXA ;Z
        LD C,LX
        LD B,'gstdhlZX ;концы строк
        LD A,(BC)
       ADD A,32
        LD (DE),A
        INC DE
        INC B
        LD A,(BC)
       JNC $+4
       ADD A,8
        RRA
        RRA
        RRA
        AND 3
        ADD A,#58
        BIT 7,H
        JZ $+6
        OR #80
        RES 7,H
        LD (DE),A
        INC DE
       EXA ;Z
        JZ genatrngfx ;CMDBLACK
        LD BC,32
        LDIR ;data->commands
genatrngfx ;
        EXX
genatrncmd ;
;line++ до #c0:
        INC LX
        LD A,LX
        XOR #C0 ;CY=0
        JNZ gsgenatr0
        LD A,HX
        SUB 6
        LD HX,A
        EXX

;ожидаем остаток времени (t)
;пока t>=#200, кидаем CMDDUMMY, потом CMDWAITSWITCH(scr0)
;de'=t
;de=commands
        EXD
        EXX
gsgenwait0 ;
        LD A,H
        CP 2
        JC gsgenwaitq
        LD BC,-224
        ADD HL,BC
        EXX
        LD (HL),CMDDUMMY
        INC HL
        EXX
        JR gsgenwait0
gsgenwaitq ;
        RRA ;srl h
        RR L
        RRA ;srl h
        RR L
        LD A,127
        SUB L
        LD HL,0 ;counter=0 (для цикла мультиколора)
        EXX
        LD (HL),CMDWAITSWITCH
        INC HL
        LD (HL),A ;значение для jr
        INC HL
        LD (HL),#17 ;значение для out
        INC HL
        EXD

;начало цикла мультиколора:
        LD LX,0 ;line=0
        LD HY,0 ;curscr=0
        LD HL,futurecommands
        LD (HL),0 ;конец futurecommands
       PUSH HL ;хвост futurecommands

;цикл мультиколора
;сейчас мы в начале 0-й строки графического экрана
;реально (для запаса времени) в конце предыдущей:
gsgenfindswitch
;ix=newgfx+line
;hl'=counter
;de'=tswitch
;hl=futurecommands
;de=commands
        EXX
        EXD
        LD HL,0 ;tswitch=0
        PUSH IX
        LD A,HX
        ADD A,4+2+1
        LD HX,A ;'atr
        LD BC,224
gsgenfindswitch0
        INC LX ;i++
        ADD HL,BC ;tswitch+=224
        LD A,(IX)
        XOR HY
        JP P,gsgenfindswitch0 ;until (atr[i]&#8000)!=curscr
        POP IX
        EXD
        EXX

gsgenifwehavetime
;есть время до переключения?
;tswitch-counter>=428+15(+32+19+15 (добавка для CMDWAITSWITCH))
;ix=newgfx+line
;hl'=counter
;de'=tswitch
;hl=futurecommands
;de=commands
        LD A,CMDWAITSWITCH
        LD (DE),A
        EXX
        EXD
        PUSH HL ;tswitch
        OR A
        SBC HL,DE ;tswitch-counter (min=32+19+15)
       JP C,$
        LD BC,32+19+15 ;эта константа используется ниже
        OR A
        SBC HL,BC ;hl=tswitch-counter-(32+19+15)
       JP C,$
        LD A,H
        CP 2
        JC gsgenwaitswitch ;времени нет
        POP HL
        EXD
        EXX
;ложим команду из очереди будущих команд:
;hl=futurecommands
       EX (SP),HL
;hl=хвост futurecommands
;de=commands
        LD A,(HL)
        OR A
        EXX
        JZ gsgendummy
        LD BC,CMDGFXTIME
        EXX
        LD BC,CMDGFXLEN
        LDIR
        JR gsgenndummy
gsgendummy
        LD BC,CMDDUMMYTIME
        EXX
        LD A,CMDDUMMY
        LD (DE),A
        INC DE
gsgenndummy
;hl=хвост futurecommands
       EX (SP),HL
;hl=futurecommands
        EXX
        CALL gsgentime
        EXX
        JP gsgenifwehavetime

gsgenwaitswitch
;ложим команду "ждать до переключения и переключить"
;hl'=futurecommands (not used)
;de'=commands
;hl=tswitch-counter-(32+19+15)=0..511
;de=counter
        SRL H
        RR L
        SRL H
        RR L
       INC L ;иначе можем получить остаток времени 1..3 такта
        LD A,127
        SUB L
        EXX
        INC DE ;там было CMDWAITSWITCH
        LD (DE),A ;значение для jr
        INC DE
        LD A,HY ;0/#80 = 'curscr
        XOR #80
        LD HY,A ;curscr = curscr xor #8000
        RLCA
        RLCA
        RLCA
        RLCA
        OR #17
        LD (DE),A ;значение для out
        INC DE
        EXX
;вычисляем реальное время команды с такими параметрами:
        ADD HL,HL
        ADD HL,HL
       ;ld bc,32+19+15
        ADD HL,BC
        LD B,H,C,L ;tcmd
        POP HL ;tswitch
        EXD
        CALL gsgentime
        EXX
        LD A,LX
        CP #C0
        JP C,gsgenfindswitch

;догенерируем очередь будущих команд:
;hl=futurecommands
       POP HL
;hl=хвост futurecommands
;de=commands
        LD A,CMDTURBO
        LD (DE),A
        INC DE
gsdogen0
        LD A,(HL)
        OR A
        JZ gsdogenq
       ;cp CMDBLACK ;иначе CMDGFX
       ;ld bc,CMDBLACKLEN
       ;jz gsdogenblack
        LD BC,CMDGFXLEN
;gsdogenblack
        LDIR
        JR gsdogen0
gsdogenq
;команда "закончить кадр":
        LD A,CMDENDFRAME
        LD (DE),A
        RET ;конец генератора кадра

gsgentime
;ix=newgfx+line
;hl=counter
;de=tswitch
;hl'=futurecommands
;de'=commands (not used)
        ADD HL,BC ;CY=0 (и в другом переходе на gsentime0 тоже)
gsgentime0 ;
        LD BC,224
       ;or a
        SBC HL,BC ;counter
        JC gsgennotime
        EXD
       ;or a
        SBC HL,BC ;tswitch
        EXD
        LD A,LX ;line
        CP #C0
        JNC gsgentime0
        EXX
        PUSH DE ;commands (not used)
        LD E,(IX) ;.newgfx[line]
        INC HX
        LD D,(IX) ;'newgfx[line]
        INC HX
        LD A,(IX) ;.gfx[line]
        INC HX
        CP E
        LD A,D
        JNZ gsgentimegfxnew
        CP (IX) ;'gfx[line]
        JZ gsgentimegfxok
gsgentimegfxnew ;
       ;LD A,D
        ADD A,A
        JZ gsgentimegfxok ;пустая графика => пустые атрибуты
                          ;они тогда будут уже отрисованы
        LD (HL),CMDGFX
        INC HL
        LD C,LX
        LD B,'gstdhlZX ;концы строк
        LD A,(BC)
       ADD A,32
        LD (HL),A
        INC HL
        INC B
        LD A,(BC)
       JNC $+3
       INC A
        BIT 7,D
        JZ $+6
        OR #80
        RES 7,D ;источник тот же
        LD (HL),A
        INC HL
        EXD
        LD BC,32
        LDIR ;data->futurecommands
        EXD
        LD (HL),B;0 ;конец futurecommands
gsgentimegfxok ;
        POP DE ;commands (not used)
        LD A,HX
        SUB 3
        LD HX,A ;CY=0
        EXX
        INC LX ;line++
        JR gsgentime0
gsgennotime ;
        ADD HL,BC ;counter как был
        RET

gsdrawbars
;забиваем все адреса графики(newgfx) и атрибутов(newatr) нулями
;de=commands
;ix=newgfx
        EXX
        PUSH IX
        POP HL
        LD L,0
        LD D,H
        LD E,1
        LD BC,#1FF
        LD (HL),L;0
        LDIR ;newgfx
        INC HL,DE
        INC H,H
        INC D,D
        LD BC,#1FF
        LD (HL),L;0
        LDIR ;newatr
       ;exx

;"рисуем" адреса всех баров - графики и атрибутов, под scr0
;выскакивание за экран (#c0) можно не проверять
       ;exx
        LD HL,gsbars
gsdrawbars0 ;
        INC L ;пропускаем dy
        LD A,(HL) ;Y бара
        LD LX,A
        INC L
        LD E,(HL) ;Y графики бара
        LD D,'gstdhlGS
        LD B,16
gsdrawbar0 ;
       PUSH DE
        LD A,(DE)
        LD C,A
        INC D
        LD A,(DE)
        LD D,A
        RRA
        RRA
        RRA
        AND 3
        ADD A,'gsatr
        LD E,A
        LD A,HX
        LD (IX),C
        INC HX
        LD (IX),D ;newgfx
        LD D,A ;old hx='newgfx
        ADD A,4
        LD HX,A
        LD (IX),C
        INC HX
        LD (IX),E ;newatr
        LD HX,D
       POP DE
        INC E ;Y графики бара
        INC LX
        DJNZ gsdrawbar0
        INC L
        JNZ gsdrawbars0
;newatr[#c0] = #8000 (чтобы не повиснуть на первом кадре)
       ;LD LX,#C0
       ;LD A,HX
       ;LD C,A
       ;ADD A,4+1
       ;LD HX,A
       ;LD (IX),#80 ;'newatr[#c0]
       ;LD HX,C
;проходим атрибуты и графику сверху вниз
;расставляем бит номера экрана
;de=commands
;ix=newgfx
        LD BC,#80 ;curscr=0
        LD LX,B;0 ;line=0
        LD A,HX
        LD D,A
        ADD A,4
        LD HX,A
        LD L,(IX)
        INC HX
        LD H,(IX) ;curatr=newatr[0]
        LD HX,D
gsgensetscreens0 ;
        LD A,HX
        ADD A,4
        LD HX,A
;если curatr!=newatr[line], то curscr=curscr xor #8000
;(источник графики тот же,
;этот бит при копировании будет игнорироваться)
        LD E,(IX)
        INC HX
        LD D,(IX) ;newatr[line]
       ;or a
        SBC HL,DE
        JZ gsgensetscreensswapq
       LD A,LX
       AND 7
       JZ gsgensetscreensswapq ;не надо перекл.каждое знакоместо
        LD A,B
        XOR C ;#80
        LD B,A ;'(curscr xor #8000)
gsgensetscreensswapq ;
        EXD ;curatr=newatr[line]
        LD A,H ;newatr[line]
        OR B
        LD (IX),A ;newatr[line] = newatr[line] or curscr
        LD A,HX
        SUB 4
        LD HX,A
        LD A,(IX)
        OR B
        LD (IX),A ;newgfx[line] = newgfx[line] or curscr
        DEC HX
;line++ до #c0:
        INC LX
        LD A,LX
        CP #C0
        JNZ gsgensetscreens0
;newatr[#c0] = newatr[#bf] xor #8000 (можно curscr xor #8000):
        LD A,B
        XOR C ;#80
        LD B,A ;'(curscr xor #8000)
        LD A,HX
        LD C,A
        ADD A,4+1
        LD HX,A
        LD (IX),B ;'newatr[#c0]
        LD HX,C
        EXX
        RET

gsmovebars
;двигаем бары, останавливая на #c0
        EXX
        LD HL,gsbars
gsmovebars0 ;
;если dy!=0, то:
;y+=dy, dy++, если y>=#c0, то:
             ;y=#C0 (т.к. остановленные перезапускаются), dy=0
        LD A,(HL) ;dy
        OR A
        JZ gsmovebarsnmove
        INC (HL) ;dy++
        INC L
        ADD A,(HL)
        LD (HL),A ;y+=dy
        CP #C0
        JC gsmovebarsmoveq
        LD (HL),#C0
        DEC L
        LD (HL),0
        JR gsmovebarsmoveiq
gsmovebarsnmove ;
;иначе (dy=0): если dy(n+1)=4, то dy=1
        INC L,L,L
        LD A,(HL) ;можем читать вне таблицы, но не пишем
        DEC L,L,L
        CP 5;4
        JNZ $+3
        INC (HL) ;если dy(n+1)=4, то dy=1
gsmovebarsmoveiq ;
        INC L
gsmovebarsmoveq ;
        INC L
        INC L
        JNZ gsmovebars0
        EXX
        RET

GENDHL
        LD DE,#4000
        LD L,E;0
        LD B,192
GENDHL0LD H,'gstdhlGS
        LD (HL),E
        INC H
       LD A,D
       ADD A,'gsscr-#40
        LD (HL),A
        LD H,'gstdhlZX
       ;LD A,E
       ;ADD A,32
        LD (HL),E
        INC H
       ;LD A,0
       ;ADC A,D
        LD (HL),D
        INC L
        EXD
        CALL GSDHL
        EXD
        DJNZ GENDHL0
        RET
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

GSRESET
        XOR A ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

       DISPLAY $
        ENT
LENGSMULBARCODE=$-WASGSMULBARCODE
       DISPLAY /T,LENGSMULBARCODE
wasscr
        INCBIN "gameover"
LENMULBARGS=$-#C000

        ORG #C000,pgeffects
       pgmulbarzx=pgeffects
       PUSH BC
       PUSH DE
        LD HL,WASMULBARCODE
        LD DE,MULBARCODE
        LD BC,LENMULBARCODE
        LDIR
        JP MULBARCODE

WASMULBARCODE
       DISP MULBARCODE
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
       ;LD A,#10 ;noturbo
       ;LD BC,#EFF7
       ;OUT (C),A
        LD A,#C9
        LD (#5F5F),A
       ;LD HL,IMVEC
       ;LD BC,256
       ;LD D,H,E,B
       ;LD (HL),#5F
       ;LDIR ;так мы задержим музыку на 2 кадра

        LD HL,#4000
        LD DE,#C000
        LD BC,#1B00
        LDIR

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
        LD BC,#EFF7
        LD A,#10
        OUT (C),A ;noturbo
       LD HL,#5F5F
       LD BC,#7FFD
        HALT
       LD (IMVEC+#FF),HL
        LD (PUSHKASP),SP
        LD HL,0
        LD A,#17
        OUT (C),A
        LD A,(HL) ;1ST BYTE DISCARDED
        LD H,(HL)
        JP (HL)
ENDFRAME
PUSHKASP=$+1
        LD SP,0
       IFN border
        LD A,R
        OUT (254),A
       ENDIF
        CALL IMER ;делает EI
        JP BIGLOOP
QUIT
;мы в конце кадра, звук только что отыграли
        LD A,2
        OUT (GSCOM),A ;конец эффекта
        HALT ;без звука
        LD HL,IMVEC
        LD BC,256
        LD D,H,E,B
        LD (HL),IMER
        LDIR
       EI
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

        DS .(-$)
CMDBLACK='$
        LD H,L
        LD E,(HL)
        LD D,(HL)
        EXD
        LD SP,HL
        EXD
        DUP 16
        PUSH HL ;0
        EDUP
        LD H,(HL)
        JP (HL)

        DS .(-$)
CMDGFX='$
        LD H,L
        LD E,(HL)
        LD D,(HL)
        EXD
        LD SP,HL
        EXD
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP
        LD H,(HL)
        JP (HL)

        DS .(-$)
CMDDUMMY='$
        LD H,L
        DUP 19
        ADD HL,HL ;0
        EDUP
        LD H,(HL)
        JP (HL) ;224

        DS .(-$)
CMDWAITSWITCH='$
        LD H,L
        LD A,(HL)
        LD ($+4),A
        JR $
        DS 127
        LD A,(HL)
        OUT (C),A
        LD H,(HL)
        JP (HL)

        DS .(-$)
CMDENDFRAME='$
        JP ENDFRAME

        DS .(-$)
CMDTURBO='$
        LD H,L
        LD BC,#EFF7
        OUT (C),H
        LD BC,#7FFD
        LD H,(HL)
        JP (HL)
       ENT
LENMULBARCODE=$-WASMULBARCODE
LENMULBARZX=$-#C000

        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

        ORG end
ObjTab
        DB "MULBARZXC"
        DW #C000
        DW LENMULBARZX
        DB pgmulbarzx
        DW #C000

        DB "MULBARGSC"
        DW #C000
        DW LENMULBARGS
        DB pgmulbargs
        DW #C000
        INCLUDE "SAVEOBJ*",#C0
        ORG $,0
        LD HL,zxwasscr
        LD DE,#4000
        LD BC,#1B00
        LDIR
        JP nenado
zxwasscr
        INCBIN "gameover"

