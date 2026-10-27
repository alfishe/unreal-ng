; =====================================================================================================================
; GSRBARP3 - the bar renderer of the rotating bars (card side), included by GSRBAR24.asm into ROTBARGS
; (card address #5900+). No object of its own.
;
; BAR draws one bar - a 256x16 texture (BARLIEBMP / BARSTAYBMP of the current bar page, mapped at #8000) rotated
; by the angle BARa about a fixed corner (bar0left: 0 = lower left, 1 = lower right) with its bottom line at BARY -
; not into a picture but into two lists of 4-byte records for the ZX: [-, pixel byte, address low, address high],
; which the ZX reads back through ZX-DMA with POP AF : POP HL : LD (HL),A. The screen is the Pentagon 16-colour one:
; 128 bytes per line; X bit 0 picks the RAM page, so the records go to list 0 ("out0", written downwards with HL)
; or list 1 ("out1", written downwards with SP: PUSH AF / INC SP stores single bytes). Off-screen pixels make no
; record; each pass ends with one texel from the texture's edge and a few black pixels.
;
; Cases by BARa (0..127 = 0..PI/4 from horizontal, 128..255 = PI/4..PI/2):
;   BARHORIZ  a = 0: a straight copy of the texture rows.
;   BARLIE    nearly lying (a = 1..127, 256*TAN a < 128): one pass per texture row, X steps by 1, Y by -TAN a.
;   BARDIAG   nearly lying with TAN a >= 1/2: the same in 2x2 chunks (Y halved, two texels per record).
;   BARSTAY   nearly standing (a >= PI/4): one pass per texture row, Y steps by -1, X by COT a.
; GSRBAR24's GSGOBEG only uses a = 70 ... 0, so only BARHORIZ and BARLIE run in the demo; BARDIAG and BARSTAY are
; assembled but not reached (the comment ";254" at the start value hints at an earlier start in the BARSTAY range).
; BARQ stores the record counts and list start addresses in the list set's header (BARdata, IX).
;
; Coordinates: Y is 8.8 fixed point kept inverted (IX = Y~ | #C0: IXH = 63 - line within the third, #C0 set), the
; third in IYH (#00/#08/#10/#18), so (IXH, X) addresses the screen address table at #C000 (GENBARSCRADDR in
; GSRBAR24) and crossing into the next third is a carry out of IX. Texture: D' = V row (high byte), HL' = U in
; fixed point, BC' = dU. Much of the code is self-modifying (the "...=$+1" / "=$+2" labels are operands).
;
; Converter additions (unreal-asm): the first line replaces ALASM's MAIN "*",#C6 (assemble the project's main source
; from this file); ALASM mnemonics rewritten (JNC/JC -> JR, HX/LX/HY/LY -> IXH/IXL/IYH/IYL, EXA -> EX AF,AF',
; 'x -> high x, LD L,A,H,0 split); the macros BARLIELINE, BARDIAGLINE, BARLIE0/1ADDBYTE and BARDIAG0/1ADDBYTE,
; which glue parameters into labels, are expanded at their calls (LEFT = left corner, RIGHT = right corner) and
; their ALASM text is kept as "(expanded at its calls)" comments. Inside the expansions "IF 1 ;left" / "IF (1)==0"
; are the macro's IFN \1 / IF0 \1 with \1 = 1 for LEFT and 0 for RIGHT; the ";left" comment names the parameter.
;
; Recurring author comments, translated once here:
;   ;DE=координаты                                       DE = coordinates (index into the address table)
;   ;сдвиг по Y - запарно обрабатывать переход через 64   shift in Y - crossing 64 (a third) is a pain to handle
;   ;полностью лежачий нарисуем простым копированием     a fully lying bar is drawn by a plain copy (BARHORIZ)
;   ;можно сделать цикл вне экрана / ;и проверять только там   one could make an off-screen loop and check only there
;   ;X мимо экрана, данные не читаем                     X off screen, the data is not read
;   ;чёрные пиксели не нужны                             black pixels are not needed
;   ;Y выскочил за верх экрана                           Y went past the top of the screen
;   ;если попали сюда, то мы точно на экране             if we got here, we are surely on screen
;   ;если A'>=#80, то выводим левый пиксель из U=0 / ;иначе выводим байт из U=255
;                                                        if A' >= #80 output the left pixel from U=0, else the byte from U=255
;   ;выводим несколько чёрных пикселей в продолжение строки   output a few black pixels continuing the line
;   ;лишнее DEC E (в конце)                              a superfluous DEC E (at the end)
;   ;LY=число пикселей                                   LY (IYL) = number of pixels
;   ;U (0-й пиксель=конец строки)                        U (pixel 0 = end of the row)
; =====================================================================================================================
; ALASM MAIN "*" (the project's main source)

; the author's measurements for one BAR call at the given angles (units not stated)
;BARSTAY (a=#8E): 394808
;BARLIE (a=#1A): 503004
;BARLIE (a=#12): 507682
;(с 8 пустыми строками, генерируемыми как обычные) (en: with 8 empty lines, generated like ordinary ones)

; BARe0ADR: DE = (IXH, X), X even: write the screen address (HSB | third, LSB) into list 0 at HL, downwards
        MACRO BARe0ADR
        LD A,(DE)               ;e0=0
        OR IYH
        DEC HL
        LD (HL),A               ;HSB
        INC E
        LD A,(DE)
        DEC L
        LD (HL),A               ;LSB
        DEC E
        ENDM

; BARe1ADR: the same into list 1 with the stack: PUSH AF / INC SP stores one byte (A); leaves E incremented
        MACRO BARe1ADR
        LD A,(DE)               ;e0=0
        OR IYH
        PUSH AF                 ;HSB
        INC SP
        INC E
        LD A,(DE)
        PUSH AF                 ;LSB
        INC SP
        ENDM

; BARPIX: A = texel (V row D', U = H'), U += dU
        MACRO BARPIX
        EXX
        LD E,H
        LD A,(DE)
        ADD HL,BC               ;U
        EXX
        ENDM

; the author's ADDBYTE macros (ALASM text, not assembled): one more texel at the bar's far edge, from U = 0, 1 or 255
; (\0), into list 1 (1ADDBYTE) or list 0 (0ADDBYTE); the DIAG ones write two bytes (2x2 chunk)
; (expanded at its calls)        MACRO BARDIAG1ADDBYTE
; (expanded at its calls)        LOCAL
; (expanded at its calls)         EXA
; (expanded at its calls) ;сдвиг по Y - запарно обрабатывать переход через 64
; (expanded at its calls)        ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)        JNC bardiag1addbyte64OK\0
; (expanded at its calls)       LD HX,#E0
; (expanded at its calls)        LD A,HY
; (expanded at its calls)        SUB 8
; (expanded at its calls)        LD HY,A
; (expanded at its calls) bardiag1addbyte64OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)       SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,\0
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC D
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         INC D
; (expanded at its calls)         EXX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)        ENDL
; (expanded at its calls)        ENDM

; (expanded at its calls)        MACRO BARDIAG0ADDBYTE
; (expanded at its calls)        LOCAL
; (expanded at its calls)         EXA
; (expanded at its calls) ;сдвиг по Y - запарно обрабатывать переход через 64
; (expanded at its calls)        ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)        JNC bardiag0addbyte64OK\0
; (expanded at its calls)       LD HX,#E0
; (expanded at its calls)        LD A,HY
; (expanded at its calls)        SUB 8
; (expanded at its calls)        LD HY,A
; (expanded at its calls) bardiag0addbyte64OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)       SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe0ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,\0
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC D
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         INC D
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)        ENDL
; (expanded at its calls)        ENDM

; (expanded at its calls)        MACRO BARLIE1ADDBYTE
; (expanded at its calls)        LOCAL
; (expanded at its calls)         EXA
; (expanded at its calls) ;сдвиг по Y - запарно обрабатывать переход через 64
; (expanded at its calls)        ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)        JNC barlie1add64OK\0
; (expanded at its calls)        LD HX,#C0
; (expanded at its calls)        LD A,HY
; (expanded at its calls)        SUB 8
; (expanded at its calls)        LD HY,A
; (expanded at its calls) barlie1add64OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,\0
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)        ENDL
; (expanded at its calls)        ENDM

; (expanded at its calls)        MACRO BARLIE0ADDBYTE
; (expanded at its calls)        LOCAL
; (expanded at its calls)         EXA
; (expanded at its calls) ;сдвиг по Y - запарно обрабатывать переход через 64
; (expanded at its calls)        ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)        JNC barlie0add64OK\0
; (expanded at its calls)        LD HX,#C0
; (expanded at its calls)        LD A,HY
; (expanded at its calls)        SUB 8
; (expanded at its calls)        LD HY,A
; (expanded at its calls) barlie0add64OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe0ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,\0
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         DEC L
; (expanded at its calls)        ENDL
; (expanded at its calls)        ENDM

; =====================================================================================================================
; BAR: render the bar at angle BARa into the list set BARIX (list 0 from BARout0 down with HL, list 1 from
; BARout1 down with SP; the second set is bardatadelta lower). The real SP is saved in BARSP.
; =====================================================================================================================
BAR
        LD (BARSP),SP
        LD A,(BARIX+1)
        CP high BARdata
        JR Z,$+8+2
        LD HL,BARout0-bardatadelta
        LD SP,BARout1-bardatadelta
        JR $+8
        LD HL,BARout0
; en, next line: "a partial alternative (set, then override) is not allowed - NMI!" Presumably because the
; card's NMI (GSNMI) pushes on SP, which must never point into the list set the ZX is reading meanwhile
        LD SP,BARout1           ;неполную альтернативу нельзя - NMI!
        LD (BARBEGout0),HL
        LD (BARBEGout1),SP
; dispatch: bit 7 of BARa -> BARSTAY; BARa = 0 -> BARHORIZ; else BARLIE (or BARDIAG below)
        LD A,(BARa)
        ADD A,A
        JP C,BARSTAY
BARLIE
        JP Z,BARHORIZ
;для почти лежачего: (en: for the nearly lying bar)
;рисуем косо от статичного угла: h/cos a проходов (строк (en: draw obliquely from the static corner: h/cos a passes (texture rows))
;текстуры) по l*cos a пикселей (слева направо по текстуре) (en: of l*cos a pixels each (left to right along the texture))
;dU=1/cos a
;dV=0
;dX=cos a*l/(l*cos a) =1
;dY=-sin a*l/(l*cos a) =-tg a
; from sin+: ldV (the V step between passes) and the pixel count per pass (a negative counter for IYL)
        LD A,(BARa)
        RRA
        RRA
        CPL
        OR #E0                  ;sin(-a)
        SUB #40                 ;-cos a
        LD E,A
        LD D,high BARSIN
        LD A,(DE)               ;-127/cos a
        ADD A,A
        LD (BARLIEldVLEFT),A
        LD (BARLIEldVRIGHT),A
        SBC A,A
        LD (BARLIEldVLEFT+1),A
        LD (BARLIEldVRIGHT+1),A
        LD A,(DE)
        INC A                   ;на 1 пикс. меньше (en: 1 pixel fewer)
        LD (BARLIEmpixLEFT),A
        LD (BARLIEmpixRIGHT),A
        LD A,(bar0left)
        OR A
        LD A,#C0                ;x~
        JR Z,$+4
        LD A,#40                ;x
        EX AF,AF'
BARa=$+1                        ;0..127 = 0..pi/4 (en: the angle is the low byte of this immediate: BARSECTG is page-aligned)
        LD DE,BARSECTG+127
        SET 7,E
        LD A,(DE)               ;256*tg a
        RES 7,E
        LD B,0
        LD C,A                  ;-dY
        OR A
        JP M,BARDIAG                    ; 256*TAN a >= 128: steep, use 2x2 chunks
        SLA C
        RL B
;INC A ;даст NN.75+-.5, а не NN.25+-.5 (en: would give NN.75+-.5 rather than NN.25+-.5)
        SRL A
        LD (BARLIEmldXd2LEFT),A ;-ldX/2 ;надо sin 2a (en: should be sin 2a)
        LD (BARLIEmldXd2RIGHT),A
        RES 7,E
        LD A,(bar0left)
        OR A
        LD A,(DE)               ;128/cos a
BARY=$+2
        LD IX,#BF80             ;Y (en: BARY: bottom line of the bar, 8.8; set by GSGOBEG)
        LD E,-1                 ;X (-1..126 - на экране) (en: -1..126 is on screen)
        JR Z,$+4
        LD E,128                ;X (1..128 - на экране) (en: 1..128 is on screen)
        EXX
;числа в таблице округлены правильно (т.е. они NN.0+-.5) (en: the table numbers are rounded correctly (NN.0+-.5))
;поэтому при умножении на 2 коррекция не требуется (en: so no correction is needed when multiplying by 2)
        LD B,0
        JR Z,$+6
        LD B,-1
        NEG
        ADD A,A
        LD C,A
        RL B
        SLA C
        RL B                    ;dU*2
        LD HL,(((barhgt-1&#FFFF)<<8|(barhgt-1&#FFFF)>>>(16-8))&#FFFF)+BARLIEBMP ;V (en: V = last texture row: passes go from the bottom row up)
        LD DE,(BARLIEldVLEFT)
        ADD HL,DE
        LD A,(bar0left)
        OR A
        JP Z,BARLIELINELEFT
        JP BARLIELINERIGHT

; the author's BARLIELINE macro (ALASM text, not assembled); its LEFT and RIGHT expansions follow
; (expanded at its calls)        MACRO BARLIELINE
; (expanded at its calls) BARLIELINE\0
; (expanded at its calls) ;HL'=out0
; (expanded at its calls) ;SP=out1
; (expanded at its calls) ;IX=Y
; (expanded at its calls) ;E'A'=X
; (expanded at its calls) ;BC'=-dY*2
; (expanded at its calls) ;HL=V
; (expanded at its calls) ;BC=dU*2
; (expanded at its calls)         LD (BARLIEoldV\0),HL ;V
; (expanded at its calls)         LD D,H ;V
; (expanded at its calls)        ;LD HL,#180 ;U (0-й пиксель=конец строки)
; (expanded at its calls)        EXA
; (expanded at its calls)        LD L,A
; (expanded at its calls)        LD H,0
; (expanded at its calls)        EXA
; (expanded at its calls)        ADD HL,HL ;HL=#000..#1FF
; (expanded at its calls)       IF0 \1 ;left
; (expanded at its calls)        DEC H
; (expanded at its calls)       ENDIF
; (expanded at its calls)         EXX
; (expanded at its calls)         LD (BARLIEoldY\0),IX
; (expanded at its calls)         LD A,E
; (expanded at its calls)         LD (BARLIEoldX\0),A
; (expanded at its calls)         LD A,HX
; (expanded at its calls)         AND #C0
; (expanded at its calls)         RRCA ;#60
; (expanded at its calls)         RRCA ;#30
; (expanded at its calls)         RRCA ;#18
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         LD A,HX
; (expanded at its calls)         AND #3F
; (expanded at its calls)         CPL
; (expanded at its calls)         LD HX,A
; (expanded at its calls)         LD A,LX
; (expanded at its calls)         CPL
; (expanded at its calls)         LD LX,A
; (expanded at its calls) BARLIEmpix\0=$+2
; (expanded at its calls)         LD LY,0
; (expanded at its calls) ;для почти лежачего:
; (expanded at its calls) ;E=X (-1..126 - на экране) ;right: 1..128 - на экране
; (expanded at its calls) ;HY=Y&#C0
; (expanded at its calls) ;IX=Y~|#C0, BC=-dY*2
; (expanded at its calls) ;HL=out0
; (expanded at its calls) ;SP=out1
; (expanded at its calls) ;HL'=U, BC'=dU*2
; (expanded at its calls) ;D'=V
; (expanded at its calls) ;LY=число пикселей
; (expanded at its calls)         BIT 0,E
; (expanded at its calls)         JP Z,barlie164OK\0
; (expanded at its calls)         JP barlie064OK\0
; (expanded at its calls) barlie0\0
; (expanded at its calls)         ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)                 ;полностью лежачий нарисуем простым копированием
; (expanded at its calls)         JC barlie064\0
; (expanded at its calls) barlie064OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP P,barlie0onscreen\0 ;можно сделать цикл вне экрана
; (expanded at its calls)                              ;и проверять только там
; (expanded at its calls) ;X мимо экрана, данные не читаем
; (expanded at its calls)         EXX
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,barlie1\0
; (expanded at its calls)         JP barlieQ\0 ;чёрные пиксели не нужны
; (expanded at its calls) barlie064\0
; (expanded at its calls)         LD HX,#C0
; (expanded at its calls)         LD A,HY
; (expanded at its calls)         SUB 8
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         JP NC,barlie064OK\0
; (expanded at its calls)         JP barlieQ\0 ;Y выскочил за верх экрана
; (expanded at its calls) barlie0onscreen\0 ;если попали сюда, то мы точно на экране
; (expanded at its calls)         BARe0ADR
; (expanded at its calls)         BARPIX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         DEC L
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,barlie1\0
; (expanded at its calls) ;если A'>=#80, то выводим левый пиксель из U=0
; (expanded at its calls) ;иначе выводим байт из U=255
; (expanded at its calls)         EXA
; (expanded at its calls)         OR A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         JP P,barlie1addbyte255\0
; (expanded at its calls)        ELSE
; (expanded at its calls)         JP M,barlie1addbyte1\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARLIE1ADDBYTE 0,\1
; (expanded at its calls)         JP barlie0black\0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls) barlie1addbyte255\0
; (expanded at its calls)         BARLIE1ADDBYTE 255,\1
; (expanded at its calls)        ELSE
; (expanded at its calls) barlie1addbyte1\0
; (expanded at its calls)         BARLIE1ADDBYTE 1,\1
; (expanded at its calls)        ENDIF
; (expanded at its calls) barlie0black\0
; (expanded at its calls) ;выводим несколько чёрных пикселей в продолжение строки
; (expanded at its calls)        ;LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP M,barlieQ\0
; (expanded at its calls)         BARe0ADR ;лишнее DEC E в конце
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),0
; (expanded at its calls)         DEC L
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)         JP M,barlieQ\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         XOR A
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         JP barlieQ\0
; (expanded at its calls) barlie1\0
; (expanded at its calls)         ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)                 ;полностью лежачий нарисуем простым копированием
; (expanded at its calls)         JC barlie164\0
; (expanded at its calls) barlie164OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP P,barlie1onscreen\0 ;можно сделать цикл вне экрана
; (expanded at its calls)                              ;и проверять только там
; (expanded at its calls) ;X мимо экрана, данные не читаем
; (expanded at its calls)         EXX
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,barlie0\0
; (expanded at its calls)         JP barlieQ\0 ;чёрные пиксели не нужны
; (expanded at its calls) barlie164\0
; (expanded at its calls)         LD HX,#C0
; (expanded at its calls)         LD A,HY
; (expanded at its calls)         SUB 8
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         JP NC,barlie164OK\0
; (expanded at its calls)         JP barlieQ\0 ;Y выскочил за верх экрана
; (expanded at its calls) barlie1onscreen\0 ;если попали сюда, то мы точно на экране
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         BARPIX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,barlie0\0
; (expanded at its calls) ;если A'>=#80, то выводим левый пиксель из U=0
; (expanded at its calls) ;иначе выводим байт из U=255
; (expanded at its calls)         EXA
; (expanded at its calls)         OR A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         JP P,barlie0addbyte255\0
; (expanded at its calls)        ELSE
; (expanded at its calls)         JP M,barlie0addbyte1\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARLIE0ADDBYTE 0,\1
; (expanded at its calls)         JP barlie1black\0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls) barlie0addbyte255\0
; (expanded at its calls)         BARLIE0ADDBYTE 255,\1
; (expanded at its calls)        ELSE
; (expanded at its calls) barlie0addbyte1\0
; (expanded at its calls)         BARLIE0ADDBYTE 1,\1
; (expanded at its calls)        ENDIF
; (expanded at its calls) barlie1black\0
; (expanded at its calls) ;выводим несколько чёрных пикселей в продолжение строки
; (expanded at its calls)        ;LD D,HX
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)         JP M,barlieQ\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         XOR A
; (expanded at its calls)         PUSH AF
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP M,barlieQ\0
; (expanded at its calls)         BARe0ADR
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),0
; (expanded at its calls)         DEC L
; (expanded at its calls) barlieQ\0
; (expanded at its calls) ;сдвиг между проходами (строками текстуры снизу вверх):
; (expanded at its calls) ;ldV=-1*h/(h/cos a) =-cos a
; (expanded at its calls) ;ldX=-sin a*h/(h/cos a) =-tg a
; (expanded at its calls) ;[ldY=-cos a*h/(h/cos a) =-cos a*cos a]
; (expanded at its calls) ;чтобы не было дыр, надо начинать строки в правильных фазах
; (expanded at its calls) ;1. X=Xold+ldX
; (expanded at its calls) ;2. если мы от строки к строке не сместились по целой части X,
; (expanded at its calls) ;то берём старую фазу Y: Y=Yold-1
; (expanded at its calls) ;3. если сместились (на -1), то берём старую фазу по Y
; (expanded at its calls) ;и отнимаем от неё 1 шаг: Y=Yold-1-dY
; (expanded at its calls) ;BC=-dY*2 (=-ldX*2)
; (expanded at its calls) BARLIEoldX\0=$+1
; (expanded at its calls)         LD E,0
; (expanded at its calls) BARLIEoldY\0=$+2
; (expanded at its calls)         LD IX,0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         EXA ;x~
; (expanded at its calls) BARLIEmldXd2\0=$+1 ;-ldX/2
; (expanded at its calls)         ADD A,0 ;X=Xold+ldX
; (expanded at its calls)        ELSE
; (expanded at its calls)         EXA ;x
; (expanded at its calls) BARLIEmldXd2\0=$+1 ;-ldX/2
; (expanded at its calls)         SUB 0 ;X=Xold+ldX
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JNC $+5 ;Y=Yold
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         DEC E ;X
; (expanded at its calls)        ELSE
; (expanded at its calls)         INC E ;X
; (expanded at its calls)        ENDIF
; (expanded at its calls)         ADD IX,BC ;Y=Yold-dY
; (expanded at its calls)         EXA
; (expanded at its calls)         DEC HX ;Y--
; (expanded at its calls)         EXX
; (expanded at its calls)        LD A,HX
; (expanded at its calls)        CP #C0
; (expanded at its calls)        JNC BARLIEQ\0
; (expanded at its calls) BARLIEoldV\0=$+1
; (expanded at its calls)         LD HL,0
; (expanded at its calls) BARLIEldV\0=$+1
; (expanded at its calls)         LD DE,0
; (expanded at its calls)         ADD HL,DE
; (expanded at its calls)         LD A,H
; (expanded at its calls)        IFN barblack
; (expanded at its calls)        CP 'BARLIEBLACK
; (expanded at its calls)        ELSE
; (expanded at its calls)         CP 'BARLIEBMP
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP NC,BARLIELINE\0
; (expanded at its calls) BARLIEQ\0
; (expanded at its calls)         JP BARLIEQ
; (expanded at its calls)        ENDM

; ---------------------------------------------------------------------------------------------------------------------
; BARLIELINE, LEFT: one pass per texture row. Two copies of the pixel loop alternate by X parity: barlie0 writes
; list 0 (BARe0ADR, HL), barlie1 writes list 1 (BARe1ADR, SP). Per pixel: Y += -dY (ADD IX,BC; carry = next
; third up), X += 1, texel from (V, U), U += dU. After the pass, one extra texel from the texture's edge (ADDBYTE)
; and black pixels beyond it; then barlieQ starts the next pass a row up with the right sub-pixel phase.
; Ends when V leaves the bitmap or Y the screen.
; ---------------------------------------------------------------------------------------------------------------------
; macro BARLIELINE expanded
BARLIELINELEFT
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARLIEoldVLEFT),HL  ;V
        LD D,H                  ;V
;LD HL,#180 ;U (0-й пиксель=конец строки)
        EX AF,AF'
        LD L,A
        LD H,0
        EX AF,AF'
        ADD HL,HL               ;HL=#000..#1FF
        IF (1)==0               ;left
        DEC H
        ENDIF
        EXX
        LD (BARLIEoldYLEFT),IX
        LD A,E
        LD (BARLIEoldXLEFT),A
        LD A,IXH
        AND #C0
        RRCA                    ;#60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,IXH
        AND #3F
        CPL
        LD IXH,A
        LD A,IXL
        CPL
        LD IXL,A
BARLIEmpixLEFT=$+2
        LD IYL,0
;для почти лежачего: (en: for the nearly lying bar)
;E=X (-1..126 - на экране) ;right: 1..128 - на экране (en: on screen: -1..126, right: 1..128)
;HY=Y&#C0
;IX=Y~|#C0, BC=-dY*2
;HL=out0
;SP=out1
;HL'=U, BC'=dU*2
;D'=V
;LY=число пикселей
        BIT 0,E
        JP Z,barlie164OKLEFT
        JP barlie064OKLEFT
barlie0LEFT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,barlie064LEFT
barlie064OKLEFT
        LD D,IXH
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,barlie0onscreenLEFT ;можно сделать цикл вне экрана
;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,barlie1LEFT
        JP barlieQLEFT          ;чёрные пиксели не нужны
barlie064LEFT
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,barlie064OKLEFT
        JP barlieQLEFT          ;Y выскочил за верх экрана
barlie0onscreenLEFT             ;если попали сюда, то мы точно на экране
        BARe0ADR
        BARPIX
        DEC L
        LD (HL),A
        DEC L
        INC IYL
        JP NZ,barlie1LEFT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 1                    ;left
        JP P,barlie1addbyte255LEFT
        ELSE
        JP M,barlie1addbyte1LEFT
        ENDIF
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK0__L1
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK0__L1
        LD D,IXH
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,0
        LD A,(DE)
        EXX
        PUSH AF

        JP barlie0blackLEFT
        IF 1                    ;left
barlie1addbyte255LEFT
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK255__L2
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK255__L2
        LD D,IXH
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,255
        LD A,(DE)
        EXX
        PUSH AF

        ELSE
barlie1addbyte1LEFT
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK1__L3
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK1__L3
        LD D,IXH
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,1
        LD A,(DE)
        EXX
        PUSH AF

        ENDIF
barlie0blackLEFT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP M,barlieQLEFT
        BARe0ADR                ;лишнее DEC E в конце
        DEC L
        LD (HL),0
        DEC L
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        JP M,barlieQLEFT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        JP barlieQLEFT
; barlie1: the same as barlie0 for the other X parity (list 1)
barlie1LEFT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,barlie164LEFT
barlie164OKLEFT
        LD D,IXH
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,barlie1onscreenLEFT ;можно сделать цикл вне экрана
;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,barlie0LEFT
        JP barlieQLEFT          ;чёрные пиксели не нужны
barlie164LEFT
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,barlie164OKLEFT
        JP barlieQLEFT          ;Y выскочил за верх экрана
barlie1onscreenLEFT             ;если попали сюда, то мы точно на экране
        DEC E                   ;e0=0
        BARe1ADR
        BARPIX
        PUSH AF
        INC IYL
        JP NZ,barlie0LEFT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 1                    ;left
        JP P,barlie0addbyte255LEFT
        ELSE
        JP M,barlie0addbyte1LEFT
        ENDIF
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK0__L4
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK0__L4
        LD D,IXH
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,0
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        JP barlie1blackLEFT
        IF 1                    ;left
barlie0addbyte255LEFT
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK255__L5
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK255__L5
        LD D,IXH
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,255
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        ELSE
barlie0addbyte1LEFT
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK1__L6
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK1__L6
        LD D,IXH
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,1
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        ENDIF
barlie1blackLEFT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        JP M,barlieQLEFT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP M,barlieQLEFT
        BARe0ADR
        DEC L
        LD (HL),0
        DEC L
barlieQLEFT
;сдвиг между проходами (строками текстуры снизу вверх): (en: shift between passes (texture rows, from the bottom up))
;ldV=-1*h/(h/cos a) =-cos a
;ldX=-sin a*h/(h/cos a) =-tg a
;[ldY=-cos a*h/(h/cos a) =-cos a*cos a]
;чтобы не было дыр, надо начинать строки в правильных фазах (en: to avoid holes the rows must start in the right phases)
;1. X=Xold+ldX
;2. если мы от строки к строке не сместились по целой части X, (en: 2. if from row to row we did not move in the integer part of X,)
;то берём старую фазу Y: Y=Yold-1 (en: take the old Y phase)
;3. если сместились (на -1), то берём старую фазу по Y (en: 3. if we moved (by -1), take the old Y phase)
;и отнимаем от неё 1 шаг: Y=Yold-1-dY (en: and subtract one step from it)
;BC=-dY*2 (=-ldX*2)
BARLIEoldXLEFT=$+1
        LD E,0
BARLIEoldYLEFT=$+2
        LD IX,0
        IF 1                    ;left
        EX AF,AF'               ;x~
BARLIEmldXd2LEFT=$+1            ;-ldX/2
        ADD A,0                 ;X=Xold+ldX
        ELSE
        EX AF,AF'               ;x
BARLIEmldXd2LEFT=$+1            ;-ldX/2
        SUB 0                   ;X=Xold+ldX
        ENDIF
        JR NC,$+5               ;Y=Yold
        IF 1                    ;left
        DEC E                   ;X
        ELSE
        INC E                   ;X
        ENDIF
        ADD IX,BC               ;Y=Yold-dY
        EX AF,AF'
        DEC IXH                 ;Y--
        EXX
        LD A,IXH
        CP #C0
        JR NC,BARLIEQLEFT
BARLIEoldVLEFT=$+1
        LD HL,0
BARLIEldVLEFT=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
        IF barblack
        CP high BARLIEBLACK
        ELSE
        CP high BARLIEBMP
        ENDIF
        JP NC,BARLIELINELEFT
BARLIEQLEFT
        JP BARLIEQ
; ---------------------------------------------------------------------------------------------------------------------
; BARLIELINE, RIGHT: the mirror image (X from 128 down); comments as in LEFT
; ---------------------------------------------------------------------------------------------------------------------
; macro BARLIELINE expanded
BARLIELINERIGHT
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARLIEoldVRIGHT),HL ;V
        LD D,H                  ;V
;LD HL,#180 ;U (0-й пиксель=конец строки)
        EX AF,AF'
        LD L,A
        LD H,0
        EX AF,AF'
        ADD HL,HL               ;HL=#000..#1FF
        IF (0)==0               ;left
        DEC H
        ENDIF
        EXX
        LD (BARLIEoldYRIGHT),IX
        LD A,E
        LD (BARLIEoldXRIGHT),A
        LD A,IXH
        AND #C0
        RRCA                    ;#60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,IXH
        AND #3F
        CPL
        LD IXH,A
        LD A,IXL
        CPL
        LD IXL,A
BARLIEmpixRIGHT=$+2
        LD IYL,0
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
        JP Z,barlie164OKRIGHT
        JP barlie064OKRIGHT
barlie0RIGHT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,barlie064RIGHT
barlie064OKRIGHT
        LD D,IXH
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,barlie0onscreenRIGHT ;можно сделать цикл вне экрана
;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,barlie1RIGHT
        JP barlieQRIGHT         ;чёрные пиксели не нужны
barlie064RIGHT
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,barlie064OKRIGHT
        JP barlieQRIGHT         ;Y выскочил за верх экрана
barlie0onscreenRIGHT            ;если попали сюда, то мы точно на экране
        BARe0ADR
        BARPIX
        DEC L
        LD (HL),A
        DEC L
        INC IYL
        JP NZ,barlie1RIGHT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 0                    ;left
        JP P,barlie1addbyte255RIGHT
        ELSE
        JP M,barlie1addbyte1RIGHT
        ENDIF
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK0__L7
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK0__L7
        LD D,IXH
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,0
        LD A,(DE)
        EXX
        PUSH AF

        JP barlie0blackRIGHT
        IF 0                    ;left
barlie1addbyte255RIGHT
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK255__L8
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK255__L8
        LD D,IXH
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,255
        LD A,(DE)
        EXX
        PUSH AF

        ELSE
barlie1addbyte1RIGHT
; macro BARLIE1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie1add64OK1__L9
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie1add64OK1__L9
        LD D,IXH
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,1
        LD A,(DE)
        EXX
        PUSH AF

        ENDIF
barlie0blackRIGHT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP M,barlieQRIGHT
        BARe0ADR                ;лишнее DEC E в конце
        DEC L
        LD (HL),0
        DEC L
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        JP M,barlieQRIGHT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        JP barlieQRIGHT
barlie1RIGHT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,barlie164RIGHT
barlie164OKRIGHT
        LD D,IXH
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,barlie1onscreenRIGHT ;можно сделать цикл вне экрана
;и проверять только там
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,barlie0RIGHT
        JP barlieQRIGHT         ;чёрные пиксели не нужны
barlie164RIGHT
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,barlie164OKRIGHT
        JP barlieQRIGHT         ;Y выскочил за верх экрана
barlie1onscreenRIGHT            ;если попали сюда, то мы точно на экране
        DEC E                   ;e0=0
        BARe1ADR
        BARPIX
        PUSH AF
        INC IYL
        JP NZ,barlie0RIGHT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 0                    ;left
        JP P,barlie0addbyte255RIGHT
        ELSE
        JP M,barlie0addbyte1RIGHT
        ENDIF
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK0__L10
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK0__L10
        LD D,IXH
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,0
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        JP barlie1blackRIGHT
        IF 0                    ;left
barlie0addbyte255RIGHT
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK255__L11
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK255__L11
        LD D,IXH
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,255
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        ELSE
barlie0addbyte1RIGHT
; macro BARLIE0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,barlie0add64OK1__L12
        LD IXH,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
barlie0add64OK1__L12
        LD D,IXH
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,1
        LD A,(DE)
        EXX
        DEC L
        LD (HL),A
        DEC L

        ENDIF
barlie1blackRIGHT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        JP M,barlieQRIGHT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP M,barlieQRIGHT
        BARe0ADR
        DEC L
        LD (HL),0
        DEC L
barlieQRIGHT
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
BARLIEoldXRIGHT=$+1
        LD E,0
BARLIEoldYRIGHT=$+2
        LD IX,0
        IF 0                    ;left
        EX AF,AF'               ;x~
BARLIEmldXd2RIGHT=$+1           ;-ldX/2
        ADD A,0                 ;X=Xold+ldX
        ELSE
        EX AF,AF'               ;x
BARLIEmldXd2RIGHT=$+1           ;-ldX/2
        SUB 0                   ;X=Xold+ldX
        ENDIF
        JR NC,$+5               ;Y=Yold
        IF 0                    ;left
        DEC E                   ;X
        ELSE
        INC E                   ;X
        ENDIF
        ADD IX,BC               ;Y=Yold-dY
        EX AF,AF'
        DEC IXH                 ;Y--
        EXX
        LD A,IXH
        CP #C0
        JR NC,BARLIEQRIGHT
BARLIEoldVRIGHT=$+1
        LD HL,0
BARLIEldVRIGHT=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
        IF barblack
        CP high BARLIEBLACK
        ELSE
        CP high BARLIEBMP
        ENDIF
        JP NC,BARLIELINERIGHT
BARLIEQRIGHT
        JP BARLIEQ

; =====================================================================================================================
; BARLIEQ / BARQ: end of BAR. A = form (0 = 1-pixel records, 1 = 2x2 chunks, from BARDIAG). Store into the header:
; the form, the lowest address of each list (where GSNMI starts the DMA) and the record counts
; ((start - end) / 4); restore SP.
; =====================================================================================================================
BARLIEQ
        XOR A                   ;CY=0
BARQ
        EXX
;HL=BARout0-(4*N0)
;SP=BARout1-(4*N1)
BARIX=$+2
        LD IX,BARdata
        LD (IX+BARdata_outform),A
        LD (IX+BARdata_out0addr),L
        LD (IX+BARdata_out0addr+1),H
        EX DE,HL
BARBEGout0=$+1
        LD HL,BARout0
        OR A
        SBC HL,DE               ;4*N0
        SRL H
        RR L
        SRL H
        RR L                    ;CY=0
        LD (IX+BARdata_out0),L
        LD (IX+BARdata_out0+1),H
        LD HL,0
        ADD HL,SP
        LD (IX+BARdata_out1addr),L
        LD (IX+BARdata_out1addr+1),H
BARBEGout1=$+1
        LD HL,BARout1
        OR A
        SBC HL,SP               ;4*N1
        SRL H
        RR L
        SRL H
        RR L
        LD (IX+BARdata_out1),L
        LD (IX+BARdata_out1+1),H
BARSP=$+1
        LD SP,0
        RET

; =====================================================================================================================
; BARHORIZ: a = 0, the bar lies in place: copy the 16 texture rows straight, two bytes per step (one per list),
; X = 0..127, bottom row first.
; =====================================================================================================================
BARHORIZ
        LD IX,(BARY)            ;Y
        EXX
        LD DE,(((barhgt-1&#FFFF)<<8|(barhgt-1&#FFFF)>>>(16-8))&#FFFF)+BARLIEBMP+1 ;VU
BARHORLINE
;HL'=out0
;SP=out1
;IX=Y
;DE=VU
        EXX
        LD A,IXH
        AND #C0
        RRCA                    ;#60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,IXH
        AND #3F
        CPL
        LD D,A
        LD E,0                  ;X (0..127 - на экране) (en: 0..127 is on screen)
;HY=Y&#C0
;IX=Y~|#C0
;HL=out0
;SP=out1
;DE'=VU
barhor0
        BARe0ADR
        EXX
        LD A,(DE)
        INC E
        INC E
        EXX
        DEC L
        LD (HL),A
        DEC L
;INC E ;DE=координаты
;DEC E ;e0=0
        BARe1ADR                ;инкрементирует E (en: increments E)
        EXX
        LD A,(DE)
        INC E
        INC E
        EXX
        PUSH AF
        INC E                   ;DE=координаты
        JP P,barhor0
        LD A,IXH
        DEC IXH                 ;Y--
        EXX
        OR A
        JR Z,BARHORQ
        DEC D
        LD A,D
        IF barblack
        CP high BARLIEBLACK
        ELSE
        CP high BARLIEBMP
        ENDIF
        JP NC,BARHORLINE
BARHORQ
        JP BARLIEQ

; =====================================================================================================================
; BARDIAG: steep nearly lying bar (TAN a >= 1/2) drawn in 2x2 chunks: Y halved (SLA D: every other screen line),
; each record carries the texel and the one of the neighbouring texture row. Not reached with this effect's angles (70 ... 0),
; and the ZX's 2x2 blitters (BARVIEW2 / BARVIEWBLACK2) are no longer selected.
; =====================================================================================================================
BARDIAG
;чанки 2x2 (en: 2x2 chunks)
;dY/2
        PUSH DE
        SRL E
        LD D,high BARSIN
        LD A,(DE)               ;sin 2a / 2
        POP DE
        LD (BARDIAGmldXd2LEFT),A ;-ldX/2
        LD (BARDIAGmldXd2RIGHT),A ;-ldX/2
        LD IX,(BARY)            ;Y
        LD A,IXH
        SRL A
        LD IXH,A
        LD A,(bar0left)
        OR A
        LD A,(DE)               ;128/cos a
        LD E,-1                 ;X (-1..126 - на экране)
        JR Z,$+4
        LD E,128                ;X (1..128 - на экране)
        EXX
;числа в таблице округлены правильно (т.е. они NN.0+-.5)
;поэтому при умножении на 2 коррекция не требуется
        LD B,0
        JR Z,$+6
        LD B,-1
        NEG
        ADD A,A
        LD C,A
        RL B
        SLA C
        RL B                    ;dU*2
;смещаем out1 вниз на ddiagout1 записей (en: shift out1 down by ddiagout1 records)
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
        LD HL,(((barhgt-1&#FFFF)<<8|(barhgt-1&#FFFF)>>>(16-8))&#FFFF)+BARLIEBMP ;V
;LD DE,barhgt-1<8+BARLIEBMP ;V
;ADD HL,DE
        LD A,(bar0left)
        OR A
        JP Z,BARDIAGLINELEFT
        JP BARDIAGLINERIGHT

; the author's BARDIAGLINE macro (ALASM text, not assembled); its LEFT and RIGHT expansions follow
; (expanded at its calls)        MACRO BARDIAGLINE
; (expanded at its calls) BARDIAGLINE\0
; (expanded at its calls) ;HL'=out0
; (expanded at its calls) ;SP=out1
; (expanded at its calls) ;IX=Y
; (expanded at its calls) ;E'A'=X
; (expanded at its calls) ;BC'=-dY*2
; (expanded at its calls) ;HL=V
; (expanded at its calls) ;BC=dU*2
; (expanded at its calls)         LD (BARDIAGoldV\0),HL ;V
; (expanded at its calls)         LD D,H ;V
; (expanded at its calls)        ;LD HL,#180 ;U (0-й пиксель=конец строки)
; (expanded at its calls)        EXA
; (expanded at its calls)        LD L,A,H,0
; (expanded at its calls)        EXA
; (expanded at its calls)        ADD HL,HL ;HL=#000..#1FF
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         DEC H
; (expanded at its calls)        ENDIF
; (expanded at its calls)         EXX
; (expanded at its calls)         LD (BARDIAGoldY\0),IX
; (expanded at its calls)         LD A,E
; (expanded at its calls)         LD (BARDIAGoldX\0),A
; (expanded at its calls)         LD A,HX
; (expanded at its calls)         AND #60
; (expanded at its calls)         RRCA ;#30
; (expanded at its calls)         RRCA ;#18
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         LD A,HX
; (expanded at its calls)         AND #1F
; (expanded at its calls)         CPL
; (expanded at its calls)         LD HX,A
; (expanded at its calls)         LD A,LX
; (expanded at its calls)         CPL
; (expanded at its calls)         LD LX,A
; (expanded at its calls) BARDIAGmpix\0=$+2
; (expanded at its calls)         LD LY,0
; (expanded at its calls) ;для почти лежачего:
; (expanded at its calls) ;E=X (-1..126 - на экране) ;right: 1..128 - на экране
; (expanded at its calls) ;HY=Y&#C0
; (expanded at its calls) ;IX=Y~|#C0, BC=-dY*2
; (expanded at its calls) ;HL=out0
; (expanded at its calls) ;SP=out1
; (expanded at its calls) ;HL'=U, BC'=dU*2
; (expanded at its calls) ;D'=V
; (expanded at its calls) ;LY=число пикселей
; (expanded at its calls)         BIT 0,E
; (expanded at its calls)         JP Z,bardiag164OK\0
; (expanded at its calls)         JP bardiag064OK\0
; (expanded at its calls) bardiag0\0
; (expanded at its calls)         ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)                 ;полностью лежачий нарисуем простым копированием
; (expanded at its calls)         JC bardiag064\0
; (expanded at its calls) bardiag064OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)       SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP P,bardiag0onscreen\0
; (expanded at its calls) ;X мимо экрана, данные не читаем
; (expanded at its calls)         EXX
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,bardiag1\0
; (expanded at its calls)         JP bardiagQ\0 ;чёрные пиксели не нужны
; (expanded at its calls) bardiag064\0
; (expanded at its calls)       LD HX,#E0
; (expanded at its calls)         LD A,HY
; (expanded at its calls)         SUB 8
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         JP NC,bardiag064OK\0
; (expanded at its calls)         JP bardiagQ\0 ;Y выскочил за верх экрана
; (expanded at its calls) bardiag0onscreen\0 ;если попали сюда, то мы точно на экране
; (expanded at its calls)         BARe0ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,H
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC D
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         INC D
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,bardiag1\0
; (expanded at its calls) ;если A'>=#80, то выводим левый пиксель из U=0
; (expanded at its calls) ;иначе выводим байт из U=255
; (expanded at its calls)         EXA
; (expanded at its calls)         OR A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         JP P,bardiag1addbyte255\0
; (expanded at its calls)        ELSE
; (expanded at its calls)         JP M,bardiag1addbyte1\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARDIAG1ADDBYTE 0,\1
; (expanded at its calls)         JP bardiag0black\0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls) bardiag1addbyte255\0
; (expanded at its calls)         BARDIAG1ADDBYTE 255,\1
; (expanded at its calls)        ELSE
; (expanded at its calls) bardiag1addbyte1\0
; (expanded at its calls)         BARDIAG1ADDBYTE 1,\1
; (expanded at its calls)        ENDIF
; (expanded at its calls) bardiag0black\0
; (expanded at its calls) ;выводим несколько чёрных пикселей в продолжение строки
; (expanded at its calls)        ;LD D,HX
; (expanded at its calls)      ;SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)        ;JP M,bardiagQ\0
; (expanded at its calls)         BARe0ADR ;лишнее DEC E
; (expanded at its calls)         XOR A
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)        ;JP M,bardiagQ\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         XOR A
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         JP bardiagQ\0
; (expanded at its calls) bardiag1\0
; (expanded at its calls)         ADD IX,BC ;Y~|#C0 ;BC!=0
; (expanded at its calls)                 ;полностью лежачий нарисуем простым копированием
; (expanded at its calls)         JC bardiag164\0
; (expanded at its calls) bardiag164OK\0
; (expanded at its calls)         LD D,HX
; (expanded at its calls)       SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP P,bardiag1onscreen\0
; (expanded at its calls) ;X мимо экрана, данные не читаем
; (expanded at its calls)         EXX
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,bardiag0\0
; (expanded at its calls)         JP bardiagQ\0 ;чёрные пиксели не нужны
; (expanded at its calls) bardiag164\0
; (expanded at its calls)       LD HX,#E0
; (expanded at its calls)         LD A,HY
; (expanded at its calls)         SUB 8
; (expanded at its calls)         LD HY,A
; (expanded at its calls)         JP NC,bardiag164OK\0
; (expanded at its calls)         JP bardiagQ\0 ;Y выскочил за верх экрана
; (expanded at its calls) bardiag1onscreen\0 ;если попали сюда, то мы точно на экране
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         EXX
; (expanded at its calls)         LD E,H
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         EXX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         EXX
; (expanded at its calls)         DEC D
; (expanded at its calls)         LD A,(DE)
; (expanded at its calls)         INC D
; (expanded at its calls)         ADD HL,BC ;U ;bc=2*dU
; (expanded at its calls)         EXX
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         INC LY
; (expanded at its calls)         JP NZ,bardiag0\0
; (expanded at its calls) ;если A'>=#80, то выводим левый пиксель из U=0
; (expanded at its calls) ;иначе выводим байт из U=255
; (expanded at its calls)         EXA
; (expanded at its calls)         OR A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         JP P,bardiag0addbyte255\0
; (expanded at its calls)        ELSE
; (expanded at its calls)         JP M,bardiag0addbyte1\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARDIAG0ADDBYTE 0,\1
; (expanded at its calls)         JP bardiag1black\0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls) bardiag0addbyte255\0
; (expanded at its calls)         BARDIAG0ADDBYTE 255,\1
; (expanded at its calls)        ELSE
; (expanded at its calls) bardiag0addbyte1\0
; (expanded at its calls)         BARDIAG0ADDBYTE 1,\1
; (expanded at its calls)        ENDIF
; (expanded at its calls) bardiag1black\0
; (expanded at its calls) ;выводим несколько чёрных пикселей в продолжение строки
; (expanded at its calls)        ;LD D,HX
; (expanded at its calls)      ;SLA D
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)        ;INC E ;DE=координаты (e0=1)
; (expanded at its calls)        ;DEC E ;e0=0
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты (e0=1)
; (expanded at its calls)         DEC E ;e0=0
; (expanded at its calls)        ;JP M,bardiagQ\0
; (expanded at its calls)        ENDIF
; (expanded at its calls)         BARe1ADR
; (expanded at its calls)         XOR A
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)         PUSH AF
; (expanded at its calls)         INC SP
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E ;DE=координаты
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E ;DE=координаты
; (expanded at its calls)        ENDIF
; (expanded at its calls)        ;JP M,bardiagQ\0
; (expanded at its calls)         BARe0ADR ;лишнее DEC E
; (expanded at its calls)         XOR A
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls)         DEC L
; (expanded at its calls)         LD (HL),A
; (expanded at its calls) bardiagQ\0
; (expanded at its calls) ;сдвиг между проходами (строками текстуры снизу вверх):
; (expanded at its calls) ;ldV=-1*h/(h/cos a) =-cos a
; (expanded at its calls) ;ldX=-sin a*h/(h/cos a) =-tg a
; (expanded at its calls) ;[ldY=-cos a*h/(h/cos a) =-cos a*cos a]
; (expanded at its calls) ;чтобы не было дыр, надо начинать строки в правильных фазах
; (expanded at its calls) ;1. X=Xold+ldX
; (expanded at its calls) ;2. если мы от строки к строке не сместились по целой части X,
; (expanded at its calls) ;то берём старую фазу Y: Y=Yold-1
; (expanded at its calls) ;3. если сместились (на -1), то берём старую фазу по Y
; (expanded at its calls) ;и отнимаем от неё 1 шаг: Y=Yold-1-dY
; (expanded at its calls) ;BC=-dY*2 (=-ldX*2)
; (expanded at its calls) BARDIAGoldX\0=$+1
; (expanded at its calls)         LD E,0
; (expanded at its calls) BARDIAGoldY\0=$+2
; (expanded at its calls)         LD IX,0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         EXA ;x~
; (expanded at its calls) BARDIAGmldXd2\0=$+1 ;-ldX/2
; (expanded at its calls)         ADD A,0 ;X=Xold+ldX
; (expanded at its calls)        ELSE
; (expanded at its calls)         EXA ;x
; (expanded at its calls) BARDIAGmldXd2\0=$+1 ;-ldX/2
; (expanded at its calls)         SUB 0 ;X=Xold+ldX
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JNC $+5 ;Y=Yold
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         DEC E ;X
; (expanded at its calls)        ELSE
; (expanded at its calls)         INC E ;X
; (expanded at its calls)        ENDIF
; (expanded at its calls)         ADD IX,BC ;Y=Yold-dY
; (expanded at its calls)         EXA
; (expanded at its calls)         DEC HX ;Y--
; (expanded at its calls)         EXX
; (expanded at its calls)        LD A,HX
; (expanded at its calls)        CP #C0
; (expanded at its calls)        JNC BARDIAGQ\0
; (expanded at its calls) BARDIAGoldV\0=$+1
; (expanded at its calls)         LD HL,0
; (expanded at its calls) BARDIAGldV\0=$+1
; (expanded at its calls)         LD DE,0
; (expanded at its calls)         ADD HL,DE
; (expanded at its calls)         LD A,H
; (expanded at its calls)        IFN barblack
; (expanded at its calls)        CP 'BARLIEBLACK+1
; (expanded at its calls)        ELSE
; (expanded at its calls)         CP 'BARLIEBMP;+1 почему-то оставляет след между 129/127
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP NC,BARDIAGLINE\0
; (expanded at its calls) BARDIAGQ\0
; (expanded at its calls)         XOR A ;CY=0
; (expanded at its calls)         INC A
; (expanded at its calls)         JP BARQ
; (expanded at its calls)        ENDM

; ---------------------------------------------------------------------------------------------------------------------
; BARDIAGLINE, LEFT: as BARLIELINE with 2x2 chunks; comments as there
; ---------------------------------------------------------------------------------------------------------------------
; macro BARDIAGLINE expanded
BARDIAGLINELEFT
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARDIAGoldVLEFT),HL ;V
        LD D,H                  ;V
;LD HL,#180 ;U (0-й пиксель=конец строки)
        EX AF,AF'
        LD L,A
        LD H,0
        EX AF,AF'
        ADD HL,HL               ;HL=#000..#1FF
        IF (1)==0               ;left
        DEC H
        ENDIF
        EXX
        LD (BARDIAGoldYLEFT),IX
        LD A,E
        LD (BARDIAGoldXLEFT),A
        LD A,IXH
        AND #60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,IXH
        AND #1F
        CPL
        LD IXH,A
        LD A,IXL
        CPL
        LD IXL,A
BARDIAGmpixLEFT=$+2
        LD IYL,0
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
        JP Z,bardiag164OKLEFT
        JP bardiag064OKLEFT
bardiag0LEFT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,bardiag064LEFT
bardiag064OKLEFT
        LD D,IXH
        SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,bardiag0onscreenLEFT
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,bardiag1LEFT
        JP bardiagQLEFT         ;чёрные пиксели не нужны
bardiag064LEFT
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,bardiag064OKLEFT
        JP bardiagQLEFT         ;Y выскочил за верх экрана
bardiag0onscreenLEFT            ;если попали сюда, то мы точно на экране
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
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        DEC L
        LD (HL),A
        INC IYL
        JP NZ,bardiag1LEFT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 1                    ;left
        JP P,bardiag1addbyte255LEFT
        ELSE
        JP M,bardiag1addbyte1LEFT
        ENDIF
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK0__L13
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK0__L13
        LD D,IXH
        SLA D
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,0
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

        JP bardiag0blackLEFT
        IF 1                    ;left
bardiag1addbyte255LEFT
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK255__L14
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK255__L14
        LD D,IXH
        SLA D
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,255
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

        ELSE
bardiag1addbyte1LEFT
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK1__L15
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK1__L15
        LD D,IXH
        SLA D
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,1
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

        ENDIF
bardiag0blackLEFT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
;SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
;JP M,bardiagQLEFT
        BARe0ADR                ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
;JP M,bardiagQLEFT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
        JP bardiagQLEFT
bardiag1LEFT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,bardiag164LEFT
bardiag164OKLEFT
        LD D,IXH
        SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,bardiag1onscreenLEFT
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,bardiag0LEFT
        JP bardiagQLEFT         ;чёрные пиксели не нужны
bardiag164LEFT
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,bardiag164OKLEFT
        JP bardiagQLEFT         ;Y выскочил за верх экрана
bardiag1onscreenLEFT            ;если попали сюда, то мы точно на экране
        DEC E                   ;e0=0
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
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        PUSH AF
        INC SP
        INC IYL
        JP NZ,bardiag0LEFT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 1                    ;left
        JP P,bardiag0addbyte255LEFT
        ELSE
        JP M,bardiag0addbyte1LEFT
        ENDIF
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK0__L16
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK0__L16
        LD D,IXH
        SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,0
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

        JP bardiag1blackLEFT
        IF 1                    ;left
bardiag0addbyte255LEFT
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK255__L17
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK255__L17
        LD D,IXH
        SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,255
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

        ELSE
bardiag0addbyte1LEFT
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK1__L18
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK1__L18
        LD D,IXH
        SLA D
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,1
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

        ENDIF
bardiag1blackLEFT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
;SLA D
        IF 1                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
;JP M,bardiagQLEFT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
        IF 1                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
;JP M,bardiagQLEFT
        BARe0ADR                ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
bardiagQLEFT
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
BARDIAGoldXLEFT=$+1
        LD E,0
BARDIAGoldYLEFT=$+2
        LD IX,0
        IF 1                    ;left
        EX AF,AF'               ;x~
BARDIAGmldXd2LEFT=$+1           ;-ldX/2
        ADD A,0                 ;X=Xold+ldX
        ELSE
        EX AF,AF'               ;x
BARDIAGmldXd2LEFT=$+1           ;-ldX/2
        SUB 0                   ;X=Xold+ldX
        ENDIF
        JR NC,$+5               ;Y=Yold
        IF 1                    ;left
        DEC E                   ;X
        ELSE
        INC E                   ;X
        ENDIF
        ADD IX,BC               ;Y=Yold-dY
        EX AF,AF'
        DEC IXH                 ;Y--
        EXX
        LD A,IXH
        CP #C0
        JR NC,BARDIAGQLEFT
BARDIAGoldVLEFT=$+1
        LD HL,0
BARDIAGldVLEFT=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
        IF barblack
        CP high BARLIEBLACK+1
        ELSE
        CP high BARLIEBMP       ;+1 почему-то оставляет след между 129/127 (en: +1 for some reason leaves a trace between 129/127)
        ENDIF
        JP NC,BARDIAGLINELEFT
BARDIAGQLEFT
        XOR A                   ;CY=0
        INC A
        JP BARQ
; ---------------------------------------------------------------------------------------------------------------------
; BARDIAGLINE, RIGHT
; ---------------------------------------------------------------------------------------------------------------------
; macro BARDIAGLINE expanded
BARDIAGLINERIGHT
;HL'=out0
;SP=out1
;IX=Y
;E'A'=X
;BC'=-dY*2
;HL=V
;BC=dU*2
        LD (BARDIAGoldVRIGHT),HL ;V
        LD D,H                  ;V
;LD HL,#180 ;U (0-й пиксель=конец строки)
        EX AF,AF'
        LD L,A
        LD H,0
        EX AF,AF'
        ADD HL,HL               ;HL=#000..#1FF
        IF (0)==0               ;left
        DEC H
        ENDIF
        EXX
        LD (BARDIAGoldYRIGHT),IX
        LD A,E
        LD (BARDIAGoldXRIGHT),A
        LD A,IXH
        AND #60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,IXH
        AND #1F
        CPL
        LD IXH,A
        LD A,IXL
        CPL
        LD IXL,A
BARDIAGmpixRIGHT=$+2
        LD IYL,0
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
        JP Z,bardiag164OKRIGHT
        JP bardiag064OKRIGHT
bardiag0RIGHT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,bardiag064RIGHT
bardiag064OKRIGHT
        LD D,IXH
        SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,bardiag0onscreenRIGHT
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,bardiag1RIGHT
        JP bardiagQRIGHT        ;чёрные пиксели не нужны
bardiag064RIGHT
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,bardiag064OKRIGHT
        JP bardiagQRIGHT        ;Y выскочил за верх экрана
bardiag0onscreenRIGHT           ;если попали сюда, то мы точно на экране
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
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        DEC L
        LD (HL),A
        INC IYL
        JP NZ,bardiag1RIGHT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 0                    ;left
        JP P,bardiag1addbyte255RIGHT
        ELSE
        JP M,bardiag1addbyte1RIGHT
        ENDIF
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK0__L19
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK0__L19
        LD D,IXH
        SLA D
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,0
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

        JP bardiag0blackRIGHT
        IF 0                    ;left
bardiag1addbyte255RIGHT
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK255__L20
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK255__L20
        LD D,IXH
        SLA D
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,255
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

        ELSE
bardiag1addbyte1RIGHT
; macro BARDIAG1ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag1addbyte64OK1__L21
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag1addbyte64OK1__L21
        LD D,IXH
        SLA D
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
        ENDIF
        BARe1ADR
        EXX
        LD E,1
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

        ENDIF
bardiag0blackRIGHT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
;SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
;JP M,bardiagQRIGHT
        BARe0ADR                ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
;JP M,bardiagQRIGHT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
        JP bardiagQRIGHT
bardiag1RIGHT
        ADD IX,BC               ;Y~|#C0 ;BC!=0
;полностью лежачий нарисуем простым копированием
        JR C,bardiag164RIGHT
bardiag164OKRIGHT
        LD D,IXH
        SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        JP P,bardiag1onscreenRIGHT
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        INC IYL
        JP NZ,bardiag0RIGHT
        JP bardiagQRIGHT        ;чёрные пиксели не нужны
bardiag164RIGHT
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,bardiag164OKRIGHT
        JP bardiagQRIGHT        ;Y выскочил за верх экрана
bardiag1onscreenRIGHT           ;если попали сюда, то мы точно на экране
        DEC E                   ;e0=0
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
        ADD HL,BC               ;U ;bc=2*dU
        EXX
        PUSH AF
        INC SP
        INC IYL
        JP NZ,bardiag0RIGHT
;если A'>=#80, то выводим левый пиксель из U=0
;иначе выводим байт из U=255
        EX AF,AF'
        OR A
        IF 0                    ;left
        JP P,bardiag0addbyte255RIGHT
        ELSE
        JP M,bardiag0addbyte1RIGHT
        ENDIF
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK0__L22
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK0__L22
        LD D,IXH
        SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,0
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

        JP bardiag1blackRIGHT
        IF 0                    ;left
bardiag0addbyte255RIGHT
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK255__L23
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK255__L23
        LD D,IXH
        SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,255
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

        ELSE
bardiag0addbyte1RIGHT
; macro BARDIAG0ADDBYTE expanded

        EX AF,AF'
;сдвиг по Y - запарно обрабатывать переход через 64
        ADD IX,BC               ;Y~|#C0 ;BC!=0
        JR NC,bardiag0addbyte64OK1__L24
        LD IXH,#E0
        LD A,IYH
        SUB 8
        LD IYH,A
bardiag0addbyte64OK1__L24
        LD D,IXH
        SLA D
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
        BARe0ADR
        EXX
        LD E,1
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

        ENDIF
bardiag1blackRIGHT
;выводим несколько чёрных пикселей в продолжение строки
;LD D,HX
;SLA D
        IF 0                    ;left
;INC E ;DE=координаты (e0=1)
;DEC E ;e0=0
        ELSE
        DEC E                   ;DE=координаты (e0=1)
        DEC E                   ;e0=0
;JP M,bardiagQRIGHT
        ENDIF
        BARe1ADR
        XOR A
        PUSH AF
        INC SP
        PUSH AF
        INC SP
        IF 0                    ;left
        INC E                   ;DE=координаты
        ELSE
        DEC E                   ;DE=координаты
        ENDIF
;JP M,bardiagQRIGHT
        BARe0ADR                ;лишнее DEC E
        XOR A
        DEC L
        LD (HL),A
        DEC L
        LD (HL),A
bardiagQRIGHT
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
BARDIAGoldXRIGHT=$+1
        LD E,0
BARDIAGoldYRIGHT=$+2
        LD IX,0
        IF 0                    ;left
        EX AF,AF'               ;x~
BARDIAGmldXd2RIGHT=$+1          ;-ldX/2
        ADD A,0                 ;X=Xold+ldX
        ELSE
        EX AF,AF'               ;x
BARDIAGmldXd2RIGHT=$+1          ;-ldX/2
        SUB 0                   ;X=Xold+ldX
        ENDIF
        JR NC,$+5               ;Y=Yold
        IF 0                    ;left
        DEC E                   ;X
        ELSE
        INC E                   ;X
        ENDIF
        ADD IX,BC               ;Y=Yold-dY
        EX AF,AF'
        DEC IXH                 ;Y--
        EXX
        LD A,IXH
        CP #C0
        JR NC,BARDIAGQRIGHT
BARDIAGoldVRIGHT=$+1
        LD HL,0
BARDIAGldVRIGHT=$+1
        LD DE,0
        ADD HL,DE
        LD A,H
        IF barblack
        CP high BARLIEBLACK+1
        ELSE
        CP high BARLIEBMP       ;+1 почему-то оставляет след между 129/127 (en: +1 for some reason leaves a trace between 129/127)
        ENDIF
        JP NC,BARDIAGLINERIGHT
BARDIAGQRIGHT
        XOR A                   ;CY=0
        INC A
        JP BARQ

; =====================================================================================================================
; BARSTAY: nearly standing bar (BARa 128..255), texture BARSTAYBMP; one pass per texture row, Y steps by -1, X by
; COT a. Left / right differ only in X's direction: BARSTAYpatch turns the DEC IXH below into INC IXH (#25 / #24,
; the second byte of DD 25). Not reached with this effect's angles.
; =====================================================================================================================
BARSTAY
;для почти стоячего: (en: for the nearly standing bar)
;рисуем косо от статичного угла: h/sin a проходов (строк (en: draw obliquely from the static corner: h/sin a passes (texture)
;текстуры) по l*sin a пикселей (слева направо по текстуре) (en: rows) of l*sin a pixels each (left to right along the texture))
;dU=1/sin a
;dV=0
;dX=cos a*l/(l*sin a) =ctg a
;dY=-sin a*l/(l*sin a) =-1

;сдвиг между проходами (строками текстуры снизу вверх): (en: shift between passes (texture rows, from the bottom up))
;ldV=-1*h/(h/sin a) =-sin a
;[ldX=-sin a*h/(h/sin a) =-sin a*sin a]
;ldY=-cos a*h/(h/sin a) =-ctg a
;чтобы не было дыр, надо начинать строки в правильных фазах (en: to avoid holes the rows must start in the right phases)
        LD A,(BARa)             ;128..255
        RRA
        RRA                     ;32..63+?*64
        OR #E0
        SUB #40                 ;sin(-a)
        LD E,A
        LD D,high BARSIN
        LD A,(DE)               ;-127/sin a
        ADD A,A
        LD (BARSTAYmpix),A
        LD C,A
        SBC A,A
        LD B,A
        SLA C
        RL B
        LD (BARSTAYldV),BC
        LD DE,(BARa)            ;128..255 = pi/4..pi/2
        LD A,127                ;128
        SUB E
        LD E,A
        LD A,(DE)               ;256*ctg a
        LD (BARSTAYmldY),A
        LD C,A                  ;dX
;INC C ;даст NN.75+-.5, а не NN.25+-.5
        SRL C
        LD A,(bar0left)
        OR A
        JR Z,$+5
        XOR A
        SUB C
        LD C,A
        RLA
        SBC A,A
        LD B,A
;LD (BARSTAYdXd2),BC
        PUSH BC
        RES 7,E
        LD BC,(BARY)            ;Y
        LD A,B
        AND #C0
        RRCA                    ;#60
        RRCA                    ;#30
        RRCA                    ;#18
        LD IYH,A
        LD A,B
        AND #3F
        CPL
        LD B,A
        LD A,(bar0left)
        OR A
        LD IX,#FF80             ;X (-1..126 - на экране)
        LD A,#25                ;DEC HX
        JR Z,$+8
        LD IX,#8080             ;X (1..128 - на экране)
        LD A,#24                ;INC HX
        LD (BARSTAYpatch),A
        LD A,(DE)               ;128/sin a
        LD D,B
        LD E,C
        POP BC
        EXX
        ADD A,A
        LD C,A
        LD B,0
        RL B                    ;BC=dU=1/sin a
        LD HL,(((barhgt-1&#FFFF)<<8|(barhgt-1&#FFFF)>>>(16-8))&#FFFF)+BARSTAYBMP
        LD A,#80                ;y~
        EX AF,AF'
BARSTAYLINE
;HL'=out0
;SP=out1
;IX=X
;D'A'=Y
;HL=V
;BC=dU
        LD (BARSTAYoldV),HL
        LD D,H                  ;V
        LD HL,#0000             ;U (первый пиксель не видно) (en: the first pixel is not visible)
        EXX
        LD (BARSTAYoldX),IX
        LD A,D
        LD (BARSTAYoldY),A
        LD A,IYH
        LD (BARSTAYoldHY),A
BARSTAYmpix=$+2
        LD IYL,0
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
        ADD IX,BC               ;X
;bc!=0, полностью стоячий не попал бы в экран (en: bc != 0: a fully standing bar would not hit the screen)
        INC D
        JR Z,barstay64
barstay64OK
        LD E,IXH
;DE=координаты
        LD A,E
        OR A
        JP P,barstayonscreen
;X мимо экрана, данные не читаем
        EXX
        ADD HL,BC               ;U
        EXX
        INC IYL
        JP NZ,barstay0
        JP barstayQ             ;чёрные пиксели не нужны
barstay64
        LD D,#C0
        LD A,IYH
        SUB 8
        LD IYH,A
        JP NC,barstay64OK
        JP barstayQ             ;Y выскочил за верх экрана
barstayonscreen                 ;если попали сюда, то мы точно на экране
;пишем адрес и данные в один из 2 буферов (en: write the address and the data into one of the 2 buffers)
;(в зависимости от битплана) (en: depending on the bitplane)
        RRA
        JR C,barstayplane1
        BARe0ADR
        BARPIX
        DEC L
        LD (HL),A
        DEC L
        INC IYL
        JP NZ,barstay0
        JP barstayblack
barstayplane1
        DEC E                   ;e0=0
        BARe1ADR
        BARPIX
        PUSH AF
        INC IYL
        JP NZ,barstay0
barstayblack
;выводим несколько чёрных пикселей в продолжение строки? (en: output a few black pixels continuing the line? (nothing is done))
barstayQ
;для почти стоячего: (en: for the nearly standing bar)
;1. Y=Yold+ldY
;2. если мы от строки к строке не сместились по целой части Y, (en: 2. if from row to row we did not move in the integer part of Y,)
;то берём старую фазу X: X=Xold-1 (en: take the old X phase)
;3. если сместились (на -1), то берём старую фазу по X (en: 3. if we moved (by -1), take the old X phase)
;и прибавляем 1 шаг: X=Xold-1+dX (en: and add one step)
BARSTAYoldX=$+2
        LD IX,0
BARSTAYoldY=$+1
        LD D,0
BARSTAYoldHY=$+2
        LD IYH,0
        EX AF,AF'               ;y~
BARSTAYmldY=$+1
        ADD A,0                 ;Y=Yold+ldY
        JR NC,BARSTAYncorX      ;X=Xold
        ADD IX,BC               ;X=Xold+dX/2
        INC D                   ;Y~
        JR NZ,BARSTAYncorX
        LD DE,#F800
        ADD IY,DE
        JR NC,BARSTAYEXXQ       ;выход за экран бывает при a=45° (en: going off screen happens at a = 45 degrees)
        LD D,#C0
BARSTAYncorX
        EX AF,AF'
BARSTAYpatch=$+1
;IFN left
        DEC IXH                 ;X--
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
        IF barblack
        CP high BARSTAYBLACK
        ELSE
        CP high BARSTAYBMP
        ENDIF
        JP NC,BARSTAYLINE
        JP BARLIEQ
; BARSTAYEXXQ: leaves from the other register set, so EXX first (BARQ does EXX again)
BARSTAYEXXQ
        EXX
        JP BARLIEQ

        DISPLAY 'bar procedures=',$-BAR

