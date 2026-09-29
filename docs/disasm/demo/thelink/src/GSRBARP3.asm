        MAIN "*",#C6
;BARSTAY (a=#8E): 394808
;BARLIE (a=#1A): 503004
;BARLIE (a=#12): 507682
;(с 8 пустыми строками, генерируемыми как обычные)

       MACRO BARe0ADR
        LD A,(DE) ;e0=0
        OR HY
        DEC HL
        LD (HL),A ;HSB
        INC E
        LD A,(DE)
        DEC L
        LD (HL),A ;LSB
        DEC E
       ENDM

       MACRO BARe1ADR
        LD A,(DE) ;e0=0
        OR HY
        PUSH AF ;HSB
        INC SP
        INC E
        LD A,(DE)
        PUSH AF ;LSB
        INC SP
       ENDM

       MACRO BARPIX
        EXX
        LD E,H
        LD A,(DE)
        ADD HL,BC ;U
        EXX
       ENDM

       MACRO BARDIAG1ADDBYTE
       LOCAL
        EXA
;сдвиг по Y - запарно обрабатывать переход через 64
       ADD IX,BC ;Y~|#C0 ;BC!=0
       JNC bardiag1addbyte64OK\0
      LD HX,#E0
       LD A,HY
       SUB 8
       LD HY,A
bardiag1addbyte64OK\0
        LD D,HX
      SLA D
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
       ENDIF
        BARe1ADR
        EXX
        LD E,\0
        LD A,(DE)
        EXX
        PUSH AF
        INC SP
        EXX
        DEC D
        LD A,(DE)
        INC D
        EXX
        PUSH AF
        INC SP
       ENDL
       ENDM

       MACRO BARDIAG0ADDBYTE
       LOCAL
        EXA
;сдвиг по Y - запарно обрабатывать переход через 64
       ADD IX,BC ;Y~|#C0 ;BC!=0
       JNC bardiag0addbyte64OK\0
      LD HX,#E0
       LD A,HY
       SUB 8
       LD HY,A
bardiag0addbyte64OK\0
        LD D,HX
      SLA D
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        BARe0ADR
        EXX
        LD E,\0
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        EXX
        DEC D
        LD A,(DE)
        INC D
        EXX
        DEC L
        LD (HL),A
       ENDL
       ENDM

       MACRO BARLIE1ADDBYTE
       LOCAL
        EXA
;сдвиг по Y - запарно обрабатывать переход через 64
       ADD IX,BC ;Y~|#C0 ;BC!=0
       JNC barlie1add64OK\0
       LD HX,#C0
       LD A,HY
       SUB 8
       LD HY,A
barlie1add64OK\0
        LD D,HX
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
       ENDIF
        BARe1ADR
        EXX
        LD E,\0
        LD A,(DE)
        EXX
        PUSH AF
       ENDL
       ENDM

       MACRO BARLIE0ADDBYTE
       LOCAL
        EXA
;сдвиг по Y - запарно обрабатывать переход через 64
       ADD IX,BC ;Y~|#C0 ;BC!=0
       JNC barlie0add64OK\0
       LD HX,#C0
       LD A,HY
       SUB 8
       LD HY,A
barlie0add64OK\0
        LD D,HX
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        BARe0ADR
        EXX
        LD E,\0
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L
       ENDL
       ENDM

BAR
        LD (BARSP),SP
       LD A,(BARIX+1)
       CP 'BARdata
       JZ $+8+2
      LD HL,BARout0-bardatadelta
      LD SP,BARout1-bardatadelta
      JR $+8
         LD HL,BARout0
         LD SP,BARout1 ;неполную альтернативу нельзя - NMI!
       LD (BARBEGout0),HL
       LD (BARBEGout1),SP
        LD A,(BARa)
        ADD A,A
        JP C,BARSTAY
BARLIE
        JP Z,BARHORIZ
;для почти лежачего:
;рисуем косо от статичного угла: h/cos a проходов (строк
;текстуры) по l*cos a пикселей (слева направо по текстуре)
;dU=1/cos a
;dV=0
;dX=cos a*l/(l*cos a) =1
;dY=-sin a*l/(l*cos a) =-tg a
        LD A,(BARa)
        RRA
        RRA
        CPL
        OR #E0 ;sin(-a)
        SUB #40 ;-cos a
        LD E,A,D,'BARSIN
        LD A,(DE) ;-127/cos a
        ADD A,A
        LD (BARLIEldVLEFT),A
        LD (BARLIEldVRIGHT),A
        SBC A,A
        LD (BARLIEldVLEFT+1),A
        LD (BARLIEldVRIGHT+1),A
       LD A,(DE)
       INC A ;на 1 пикс. меньше
        LD (BARLIEmpixLEFT),A
        LD (BARLIEmpixRIGHT),A
      LD A,(bar0left)
      OR A
      LD A,#C0 ;x~
      JZ $+4
      LD A,#40 ;x
        EXA
BARa=$+1 ;0..127 = 0..pi/4
        LD DE,BARSECTG+127
       SET 7,E
        LD A,(DE) ;256*tg a
       RES 7,E
        LD B,0,C,A ;-dY
      OR A
      JP M,BARDIAG
        SLA C
        RL B
      ;INC A ;даст NN.75+-.5, а не NN.25+-.5
       SRL A
       LD (BARLIEmldXd2LEFT),A ;-ldX/2 ;надо sin 2a
       LD (BARLIEmldXd2RIGHT),A
        RES 7,E
      LD A,(bar0left)
      OR A
        LD A,(DE) ;128/cos a
BARY=$+2
        LD IX,#BF80 ;Y
      LD E,-1 ;X (-1..126 - на экране)
      JZ $+4
      LD E,128 ;X (1..128 - на экране)
        EXX
;числа в таблице округлены правильно (т.е. они NN.0+-.5)
;поэтому при умножении на 2 коррекция не требуется
      LD B,0
      JZ $+6
      LD B,-1
      NEG
        ADD A,A
        LD C,A
        RL B
       SLA C
       RL B ;dU*2
        LD HL,barhgt-1<8+BARLIEBMP ;V
     LD DE,(BARLIEldVLEFT)
     ADD HL,DE
      LD A,(bar0left)
      OR A
      JP Z,BARLIELINELEFT
      JP BARLIELINERIGHT

       MACRO BARLIELINE
BARLIELINE\0
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARLIEoldV\0),HL ;V
        LD D,H ;V
       ;LD HL,#180 ;U (0-й пиксель=конец строки)
       EXA
       LD L,A
       LD H,0
       EXA
       ADD HL,HL ;HL=#000..#1FF
      IF0 \1 ;left
       DEC H
      ENDIF
        EXX
        LD (BARLIEoldY\0),IX
        LD A,E
        LD (BARLIEoldX\0),A
        LD A,HX
        AND #C0
        RRCA ;#60
        RRCA ;#30
        RRCA ;#18
        LD HY,A
        LD A,HX
        AND #3F
        CPL
        LD HX,A
        LD A,LX
        CPL
        LD LX,A
BARLIEmpix\0=$+2
        LD LY,0
;для почти лежачего:
;E=X (-1..126 - на экране) ;right: 1..128 - на экране
;HY=Y&#C0
;IX=Y~|#C0, BC=-dY*2
;HL=out0
;SP=out1
;HL'=U, BC'=dU*2
;D'=V
;LY=число пикселей
        BIT 0,E
        JP Z,barlie164OK\0
        JP barlie064OK\0
barlie0\0
        ADD IX,BC ;Y~|#C0 ;BC!=0
                ;полностью лежачий нарисуем простым копированием
        JC barlie064\0
barlie064OK\0
        LD D,HX
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP P,barlie0onscreen\0 ;можно сделать цикл вне экрана
                             ;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC ;U ;bc=2*dU
        EXX
        INC LY
        JP NZ,barlie1\0
        JP barlieQ\0 ;чёрные пиксели не нужны
barlie064\0
        LD HX,#C0
        LD A,HY
        SUB 8
        LD HY,A
        JP NC,barlie064OK\0
        JP barlieQ\0 ;Y выскочил за верх экрана
barlie0onscreen\0 ;если попали сюда, то мы точно на экране
        BARe0ADR
        BARPIX
        DEC L
        LD (HL),A
        DEC L
        INC LY
        JP NZ,barlie1\0
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EXA
        OR A
       IFN \1 ;left
        JP P,barlie1addbyte255\0
       ELSE
        JP M,barlie1addbyte1\0
       ENDIF
        BARLIE1ADDBYTE 0,\1
        JP barlie0black\0
       IFN \1 ;left
barlie1addbyte255\0
        BARLIE1ADDBYTE 255,\1
       ELSE
barlie1addbyte1\0
        BARLIE1ADDBYTE 1,\1
       ENDIF
barlie0black\0
;выводим несколько чёрных пикселей в продолжение строки
       ;LD D,HX
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP M,barlieQ\0
        BARe0ADR ;лишнее DEC E в конце
        DEC L
        LD (HL),0
        DEC L
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
        JP M,barlieQ\0
       ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        JP barlieQ\0
barlie1\0
        ADD IX,BC ;Y~|#C0 ;BC!=0
                ;полностью лежачий нарисуем простым копированием
        JC barlie164\0
barlie164OK\0
        LD D,HX
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP P,barlie1onscreen\0 ;можно сделать цикл вне экрана
                             ;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC ;U ;bc=2*dU
        EXX
        INC LY
        JP NZ,barlie0\0
        JP barlieQ\0 ;чёрные пиксели не нужны
barlie164\0
        LD HX,#C0
        LD A,HY
        SUB 8
        LD HY,A
        JP NC,barlie164OK\0
        JP barlieQ\0 ;Y выскочил за верх экрана
barlie1onscreen\0 ;если попали сюда, то мы точно на экране
        DEC E ;e0=0
        BARe1ADR
        BARPIX
        PUSH AF
        INC LY
        JP NZ,barlie0\0
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EXA
        OR A
       IFN \1 ;left
        JP P,barlie0addbyte255\0
       ELSE
        JP M,barlie0addbyte1\0
       ENDIF
        BARLIE0ADDBYTE 0,\1
        JP barlie1black\0
       IFN \1 ;left
barlie0addbyte255\0
        BARLIE0ADDBYTE 255,\1
       ELSE
barlie0addbyte1\0
        BARLIE0ADDBYTE 1,\1
       ENDIF
barlie1black\0
;выводим несколько чёрных пикселей в продолжение строки
       ;LD D,HX
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
        JP M,barlieQ\0
       ENDIF
        BARe1ADR
        XOR A
        PUSH AF
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP M,barlieQ\0
        BARe0ADR
        DEC L
        LD (HL),0
        DEC L
barlieQ\0
;сдвиг между проходами (строками текстуры снизу вверх):
;ldV=-1*h/(h/cos a) =-cos a
;ldX=-sin a*h/(h/cos a) =-tg a
;[ldY=-cos a*h/(h/cos a) =-cos a*cos a]
;чтобы не было дыр, надо начинать строки в правильных фазах
;1. X=Xold+ldX
;2. если мы от строки к строке не сместились по целой части X,
;то берём старую фазу Y: Y=Yold-1
;3. если сместились (на -1), то берём старую фазу по Y
;и отнимаем от неё 1 шаг: Y=Yold-1-dY
;BC=-dY*2 (=-ldX*2)
BARLIEoldX\0=$+1
        LD E,0
BARLIEoldY\0=$+2
        LD IX,0
       IFN \1 ;left
        EXA ;x~
BARLIEmldXd2\0=$+1 ;-ldX/2
        ADD A,0 ;X=Xold+ldX
       ELSE
        EXA ;x
BARLIEmldXd2\0=$+1 ;-ldX/2
        SUB 0 ;X=Xold+ldX
       ENDIF
        JNC $+5 ;Y=Yold
       IFN \1 ;left
        DEC E ;X
       ELSE
        INC E ;X
       ENDIF
        ADD IX,BC ;Y=Yold-dY
        EXA
        DEC HX ;Y--
        EXX
       LD A,HX
       CP #C0
       JNC BARLIEQ\0
BARLIEoldV\0=$+1
        LD HL,0
BARLIEldV\0=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
       IFN barblack
       CP 'BARLIEBLACK
       ELSE
        CP 'BARLIEBMP
       ENDIF
        JP NC,BARLIELINE\0
BARLIEQ\0
        JP BARLIEQ
       ENDM

BARLIELINE LEFT,1
BARLIELINE RIGHT,0

BARLIEQ
        XOR A ;CY=0
BARQ
        EXX
;HL=BARout0-(4*N0)
;SP=BARout1-(4*N1)
BARIX=$+2
        LD IX,BARdata
        LD (IX+BARdata_outform),A
         LD (IX+BARdata_out0addr),L
         LD (IX+BARdata_out0addr+1),H
        EXD
BARBEGout0=$+1
        LD HL,BARout0
        OR A
        SBC HL,DE ;4*N0
        SRL H
        RR L
        SRL H
        RR L ;CY=0
        LD (IX+BARdata_out0),L
        LD (IX+BARdata_out0+1),H
         LD HL,0
         ADD HL,SP
         LD (IX+BARdata_out1addr),L
         LD (IX+BARdata_out1addr+1),H
BARBEGout1=$+1
        LD HL,BARout1
        OR A
        SBC HL,SP ;4*N1
        SRL H
        RR L
        SRL H
        RR L
        LD (IX+BARdata_out1),L
        LD (IX+BARdata_out1+1),H
BARSP=$+1
        LD SP,0
        RET

BARHORIZ
        LD IX,(BARY) ;Y
        EXX
        LD DE,barhgt-1<8+BARLIEBMP+1 ;VU
BARHORLINE
;HL'=out0
;SP=out1
;IX=Y
;DE=VU
        EXX
        LD A,HX
        AND #C0
        RRCA ;#60
        RRCA ;#30
        RRCA ;#18
        LD HY,A
        LD A,HX
        AND #3F
        CPL
        LD D,A
        LD E,0 ;X (0..127 - на экране)
;HY=Y&#C0
;IX=Y~|#C0
;HL=out0
;SP=out1
;DE'=VU
barhor0
        BARe0ADR
        EXX
        LD A,(DE)
        INC E,E
        EXX
        DEC L
        LD (HL),A
        DEC L
       ;INC E ;DE=координаты
       ;DEC E ;e0=0
        BARe1ADR ;инкрементирует E
        EXX
        LD A,(DE)
        INC E,E
        EXX
        PUSH AF
        INC E ;DE=координаты
        JP P,barhor0
       LD A,HX
        DEC HX ;Y--
        EXX
       OR A
       JZ BARHORQ
        DEC D
        LD A,D
       IFN barblack
       CP 'BARLIEBLACK
       ELSE
        CP 'BARLIEBMP
       ENDIF
        JP NC,BARHORLINE
BARHORQ
        JP BARLIEQ

BARDIAG
;чанки 2x2
;dY/2
      PUSH DE
        SRL E
        LD D,'BARSIN
        LD A,(DE) ;sin 2a / 2
      POP DE
       LD (BARDIAGmldXd2LEFT),A ;-ldX/2
       LD (BARDIAGmldXd2RIGHT),A ;-ldX/2
        LD IX,(BARY) ;Y
       LD A,HX
       SRL A
       LD HX,A
      LD A,(bar0left)
      OR A
        LD A,(DE) ;128/cos a
      LD E,-1 ;X (-1..126 - на экране)
      JZ $+4
      LD E,128 ;X (1..128 - на экране)
        EXX
;числа в таблице округлены правильно (т.е. они NN.0+-.5)
;поэтому при умножении на 2 коррекция не требуется
      LD B,0
      JZ $+6
      LD B,-1
      NEG
        ADD A,A
        LD C,A
        RL B
       SLA C
       RL B ;dU*2
;смещаем out1 вниз на ddiagout1 записей
       ;LD HL,-ddiagout1*4
       ;ADD HL,SP
       ;LD SP,HL
      LD HL,(BARLIEldVLEFT)
      ADD HL,HL
      LD (BARDIAGldVLEFT),HL
      LD (BARDIAGldVRIGHT),HL
      LD A,(BARLIEmpixLEFT)
      LD (BARDIAGmpixLEFT),A
      LD (BARDIAGmpixRIGHT),A
        LD HL,barhgt-1<8+BARLIEBMP ;V
      ;LD DE,barhgt-1<8+BARLIEBMP ;V
      ;ADD HL,DE
      LD A,(bar0left)
      OR A
      JP Z,BARDIAGLINELEFT
      JP BARDIAGLINERIGHT

       MACRO BARDIAGLINE
BARDIAGLINE\0
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARDIAGoldV\0),HL ;V
        LD D,H ;V
       ;LD HL,#180 ;U (0-й пиксель=конец строки)
       EXA
       LD L,A,H,0
       EXA
       ADD HL,HL ;HL=#000..#1FF
       IF0 \1 ;left
        DEC H
       ENDIF
        EXX
        LD (BARDIAGoldY\0),IX
        LD A,E
        LD (BARDIAGoldX\0),A
        LD A,HX
        AND #60
        RRCA ;#30
        RRCA ;#18
        LD HY,A
        LD A,HX
        AND #1F
        CPL
        LD HX,A
        LD A,LX
        CPL
        LD LX,A
BARDIAGmpix\0=$+2
        LD LY,0
;для почти лежачего:
;E=X (-1..126 - на экране) ;right: 1..128 - на экране
;HY=Y&#C0
;IX=Y~|#C0, BC=-dY*2
;HL=out0
;SP=out1
;HL'=U, BC'=dU*2
;D'=V
;LY=число пикселей
        BIT 0,E
        JP Z,bardiag164OK\0
        JP bardiag064OK\0
bardiag0\0
        ADD IX,BC ;Y~|#C0 ;BC!=0
                ;полностью лежачий нарисуем простым копированием
        JC bardiag064\0
bardiag064OK\0
        LD D,HX
      SLA D
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP P,bardiag0onscreen\0
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC ;U ;bc=2*dU
        EXX
        INC LY
        JP NZ,bardiag1\0
        JP bardiagQ\0 ;чёрные пиксели не нужны
bardiag064\0
      LD HX,#E0
        LD A,HY
        SUB 8
        LD HY,A
        JP NC,bardiag064OK\0
        JP bardiagQ\0 ;Y выскочил за верх экрана
bardiag0onscreen\0 ;если попали сюда, то мы точно на экране
        BARe0ADR
        EXX
        LD E,H
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        EXX
        DEC D
        LD A,(DE)
        INC D
        ADD HL,BC ;U ;bc=2*dU
        EXX
        DEC L
        LD (HL),A
        INC LY
        JP NZ,bardiag1\0
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EXA
        OR A
       IFN \1 ;left
        JP P,bardiag1addbyte255\0
       ELSE
        JP M,bardiag1addbyte1\0
       ENDIF
        BARDIAG1ADDBYTE 0,\1
        JP bardiag0black\0
       IFN \1 ;left
bardiag1addbyte255\0
        BARDIAG1ADDBYTE 255,\1
       ELSE
bardiag1addbyte1\0
        BARDIAG1ADDBYTE 1,\1
       ENDIF
bardiag0black\0
;выводим несколько чёрных пикселей в продолжение строки
       ;LD D,HX
     ;SLA D
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
       ;JP M,bardiagQ\0
        BARe0ADR ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
       ;JP M,bardiagQ\0
       ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
        JP bardiagQ\0
bardiag1\0
        ADD IX,BC ;Y~|#C0 ;BC!=0
                ;полностью лежачий нарисуем простым копированием
        JC bardiag164\0
bardiag164OK\0
        LD D,HX
      SLA D
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
        JP P,bardiag1onscreen\0
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC ;U ;bc=2*dU
        EXX
        INC LY
        JP NZ,bardiag0\0
        JP bardiagQ\0 ;чёрные пиксели не нужны
bardiag164\0
      LD HX,#E0
        LD A,HY
        SUB 8
        LD HY,A
        JP NC,bardiag164OK\0
        JP bardiagQ\0 ;Y выскочил за верх экрана
bardiag1onscreen\0 ;если попали сюда, то мы точно на экране
        DEC E ;e0=0
        BARe1ADR
        EXX
        LD E,H
        LD A,(DE)
        EXX
        PUSH AF
        INC SP
        EXX
        DEC D
        LD A,(DE)
        INC D
        ADD HL,BC ;U ;bc=2*dU
        EXX
        PUSH AF
        INC SP
        INC LY
        JP NZ,bardiag0\0
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EXA
        OR A
       IFN \1 ;left
        JP P,bardiag0addbyte255\0
       ELSE
        JP M,bardiag0addbyte1\0
       ENDIF
        BARDIAG0ADDBYTE 0,\1
        JP bardiag1black\0
       IFN \1 ;left
bardiag0addbyte255\0
        BARDIAG0ADDBYTE 255,\1
       ELSE
bardiag0addbyte1\0
        BARDIAG0ADDBYTE 1,\1
       ENDIF
bardiag1black\0
;выводим несколько чёрных пикселей в продолжение строки
       ;LD D,HX
     ;SLA D
       IFN \1 ;left
       ;INC E ;DE=координаты (e0=1)
       ;DEC E ;e0=0
       ELSE
        DEC E ;DE=координаты (e0=1)
        DEC E ;e0=0
       ;JP M,bardiagQ\0
       ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
       IFN \1 ;left
        INC E ;DE=координаты
       ELSE
        DEC E ;DE=координаты
       ENDIF
       ;JP M,bardiagQ\0
        BARe0ADR ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
bardiagQ\0
;сдвиг между проходами (строками текстуры снизу вверх):
;ldV=-1*h/(h/cos a) =-cos a
;ldX=-sin a*h/(h/cos a) =-tg a
;[ldY=-cos a*h/(h/cos a) =-cos a*cos a]
;чтобы не было дыр, надо начинать строки в правильных фазах
;1. X=Xold+ldX
;2. если мы от строки к строке не сместились по целой части X,
;то берём старую фазу Y: Y=Yold-1
;3. если сместились (на -1), то берём старую фазу по Y
;и отнимаем от неё 1 шаг: Y=Yold-1-dY
;BC=-dY*2 (=-ldX*2)
BARDIAGoldX\0=$+1
        LD E,0
BARDIAGoldY\0=$+2
        LD IX,0
       IFN \1 ;left
        EXA ;x~
BARDIAGmldXd2\0=$+1 ;-ldX/2
        ADD A,0 ;X=Xold+ldX
       ELSE
        EXA ;x
BARDIAGmldXd2\0=$+1 ;-ldX/2
        SUB 0 ;X=Xold+ldX
       ENDIF
        JNC $+5 ;Y=Yold
       IFN \1 ;left
        DEC E ;X
       ELSE
        INC E ;X
       ENDIF
        ADD IX,BC ;Y=Yold-dY
        EXA
        DEC HX ;Y--
        EXX
       LD A,HX
       CP #C0
       JNC BARDIAGQ\0
BARDIAGoldV\0=$+1
        LD HL,0
BARDIAGldV\0=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
       IFN barblack
       CP 'BARLIEBLACK+1
       ELSE
        CP 'BARLIEBMP;+1 почему-то оставляет след между 129/127
       ENDIF
        JP NC,BARDIAGLINE\0
BARDIAGQ\0
        XOR A ;CY=0
        INC A
        JP BARQ
       ENDM

BARDIAGLINE LEFT,1
BARDIAGLINE RIGHT,0

BARSTAY
;для почти стоячего:
;рисуем косо от статичного угла: h/sin a проходов (строк
;текстуры) по l*sin a пикселей (слева направо по текстуре)
;dU=1/sin a
;dV=0
;dX=cos a*l/(l*sin a) =ctg a
;dY=-sin a*l/(l*sin a) =-1

;сдвиг между проходами (строками текстуры снизу вверх):
;ldV=-1*h/(h/sin a) =-sin a
;[ldX=-sin a*h/(h/sin a) =-sin a*sin a]
;ldY=-cos a*h/(h/sin a) =-ctg a
;чтобы не было дыр, надо начинать строки в правильных фазах
        LD A,(BARa) ;128..255
        RRA
        RRA ;32..63+?*64
        OR #E0
        SUB #40 ;sin(-a)
        LD E,A,D,'BARSIN
        LD A,(DE) ;-127/sin a
        ADD A,A
       LD (BARSTAYmpix),A
        LD C,A
        SBC A,A
        LD B,A
        SLA C
        RL B
        LD (BARSTAYldV),BC
        LD DE,(BARa) ;128..255 = pi/4..pi/2
       LD A,127;128
       SUB E
       LD E,A
        LD A,(DE) ;256*ctg a
       LD (BARSTAYmldY),A
        LD C,A ;dX
      ;INC C ;даст NN.75+-.5, а не NN.25+-.5
       SRL C
      LD A,(bar0left)
      OR A
      JZ $+5
      XOR A
      SUB C
      LD C,A
        RLA
        SBC A,A
        LD B,A
       ;LD (BARSTAYdXd2),BC
       PUSH BC
        RES 7,E
        LD BC,(BARY) ;Y
       LD A,B
       AND #C0
       RRCA ;#60
       RRCA ;#30
       RRCA ;#18
       LD HY,A
       LD A,B
       AND #3F
       CPL
       LD B,A
      LD A,(bar0left)
      OR A
      LD IX,#FF80 ;X (-1..126 - на экране)
      LD A,#25 ;DEC HX
      JZ $+8
      LD IX,#8080 ;X (1..128 - на экране)
      LD A,#24 ;INC HX
      LD (BARSTAYpatch),A
        LD A,(DE) ;128/sin a
       LD D,B,E,C
       POP BC
        EXX
        ADD A,A
        LD C,A
        LD B,0
        RL B ;BC=dU=1/sin a
        LD HL,barhgt-1<8+BARSTAYBMP
        LD A,#80 ;y~
        EXA
BARSTAYLINE
;HL'=out0
;SP=out1
;IX=X
;D'A'=Y
;HL=V
;BC=dU
        LD (BARSTAYoldV),HL
        LD D,H ;V
        LD HL,#0000 ;U (первый пиксель не видно)
        EXX
        LD (BARSTAYoldX),IX
        LD A,D,(BARSTAYoldY),A
        LD A,HY,(BARSTAYoldHY),A
BARSTAYmpix=$+2
        LD LY,0
;для почти стоячего:
;D=Y~|#C0
;HY=Y&#C0
;IX=X, BC=dX
;HL=out0
;SP=out1
;HL'=U, BC'=dU
;D'=V
        JP barstay64OK
barstay0
        ADD IX,BC ;X
                   ;bc!=0, полностью стоячий не попал бы в экран
        INC D
        JZ barstay64
barstay64OK
        LD E,HX
;DE=координаты
        LD A,E
        OR A
        JP P,barstayonscreen
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC ;U
        EXX
        INC LY
        JP NZ,barstay0
        JP barstayQ ;чёрные пиксели не нужны
barstay64
        LD D,#C0
        LD A,HY
        SUB 8
        LD HY,A
        JP NC,barstay64OK
        JP barstayQ ;Y выскочил за верх экрана
barstayonscreen ;если попали сюда, то мы точно на экране
;пишем адрес и данные в один из 2 буферов
;(в зависимости от битплана)
        RRA
        JC barstayplane1
        BARe0ADR
        BARPIX
        DEC L
        LD (HL),A
        DEC L
        INC LY
        JP NZ,barstay0
        JP barstayblack
barstayplane1
        DEC E ;e0=0
        BARe1ADR
        BARPIX
        PUSH AF
        INC LY
        JP NZ,barstay0
barstayblack
;выводим несколько чёрных пикселей в продолжение строки?
barstayQ
;для почти стоячего:
;1. Y=Yold+ldY
;2. если мы от строки к строке не сместились по целой части Y,
;то берём старую фазу X: X=Xold-1
;3. если сместились (на -1), то берём старую фазу по X
;и прибавляем 1 шаг: X=Xold-1+dX
BARSTAYoldX=$+2
        LD IX,0
BARSTAYoldY=$+1
        LD D,0
BARSTAYoldHY=$+2
        LD HY,0
        EXA ;y~
BARSTAYmldY=$+1
        ADD A,0 ;Y=Yold+ldY
        JNC BARSTAYncorX ;X=Xold
        ADD IX,BC ;X=Xold+dX/2
        INC D ;Y~
        JNZ BARSTAYncorX
        LD DE,#F800
        ADD IY,DE
       JNC BARSTAYEXXQ ;выход за экран бывает при a=45°
        LD D,#C0
BARSTAYncorX
        EXA
BARSTAYpatch=$+1
      ;IFN left
        DEC HX ;X--
      ;ELSE
      ; INC HX
      ;ENDIF
        EXX
BARSTAYoldV=$+1
        LD HL,0
BARSTAYldV=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
       IFN barblack
       CP 'BARSTAYBLACK
       ELSE
        CP 'BARSTAYBMP
       ENDIF
        JP NC,BARSTAYLINE
        JP BARLIEQ
BARSTAYEXXQ
        EXX
        JP BARLIEQ

       DISPLAY "bar procedures=",$-BAR

