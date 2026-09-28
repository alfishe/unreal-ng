        MAIN "*",#C6
;правый треугольник
;  1
; />2  - строка W (самая широкая)
;3
;левый треугольник
;   1
;2</  - строка W (самая широкая)
;  3
;максимальные размеры 63x63

;X 3D-движка растет на экране вверх
;Y 3D-движка растет на экране вправо

;t       DB 0 ;используется в VU.... (содержит t*2)
;теперь в LX
;t=абстрактная координата в текстуре на строке W на ребре 1-3
;t=64*H12/H13 неточно
;t=0.5+H12*dUV13 точно (6.0=6.0*6.8)
;деления в dU,dV нужны, чтобы узнать шаг, с которым
;за W проходов мы погасим расстояние до бордюра текстуры (0.5)
;это +-t или +-(texarea-t) - поэтому t считается без бордюра

;умножение +-6.0=6.0*+-7.8 (H=A~*DE)
       MACRO TRIMULLOOP
      IFN 0
        LD HL,#0080>5 ;правильное округление
        ADD A,A
        JP C,$+4 ;для мелких быстрее
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JP C,$+4 ;для мелких быстрее
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JP C,$+4 ;для мелких быстрее
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
        LD A,H ;правильное округление
;173..202t+?
;ZF=1
      ELSE
       LOCAL
        LD H,D,L,E ;для старшей единицы
        ADD A,A
        JNC _ADD5
        ADD A,A
        JNC _ADD4
        ADD A,A
        JNC _ADD3
        ADD A,A
        JNC _ADD2
        ADD A,A
        JNC _ADD1
        ADD A,A
        JNC _ADD0
       ;A=0
        JP _ADDQ
_ADD5ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
_ADD4ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
_ADD3ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
_ADD2ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
_ADD1ADD HL,HL
        ADD A,A
        JC $+3
        ADD HL,DE
_ADD0
       ;A=0
       ;LD DE,#0080
       ;ADD HL,DE ;правильное округление
       ;LD A,H
        RL L ;правильное округление
        ADC A,H
_ADDQ
       ENDL
      ENDIF
       ENDM
;можно сделать 64 процедуры умножения (для каждого A)

       MACRO TRISORT
        LD (YX1),\0\1
       IFN texdec
        LD A,\4
        SUB \0
       ELSE
        LD A,\0
        SUB \4
       ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,\5
        SUB \1
        ADD A,A
        LD (l13SLA),A
       IFN texdec
        LD A,\2
        SUB \0
       ELSE
        LD A,\0
        SUB \2
       ENDIF
        LD (mH12),A
       DEC A ;даст то же, что H12 CPL
       ADD A,A
       ADD A,A
       LD (H12cplshl2),A
        LD A,\3
        SUB \1
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
       IFN texdec
        LD A,\4
        SUB \2
       ELSE
        LD A,\2
        SUB \4
       ENDIF
        LD H,A
        LD A,\5
        SUB \3
        ADD A,A
        LD L,A
        LD (mHl23),HL
       IF0 trisortmesspop
        POP BC
        POP DE
        POP HL
        LD A,\0
        RLCA
        OR \1
        RLCA
        OR \2
        RLCA
        OR \3
        RLCA
        OR \4
        RLCA
        OR \5
       ELSE
        IF0 "\0\1"-"BC"
        POP BC
        ELSE
        IF0 "\2\3"-"BC"
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF0 "\0\1"-"DE"
        POP BC
        ELSE
        IF0 "\2\3"-"DE"
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF0 "\0\1"-"HL"
        POP BC
        ELSE
        IF0 "\2\3"-"HL"
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
       ENDIF
       ENDM

TRI
        LD (TRIQSP),SP
        LD SP,HL
;Чтение+сортировка+высоты+ширины
;сортируем вершины A,B,C (с параметрами X,Y,U,V) по Y
;результат (вершины 1,2,3 с параметрами)
;в определённых фиксированных адресах
;H13=Y3-Y1 - если 0, то ВЫХОД
;l13=X3-X1
;H12=Y2-Y1, l12=X2-X1
;H23=Y3-Y2, l23=X3-X2
;если я имею YA,YB,YC, то у меня уже есть 3 указателя
;либо уже прочитаны XA,XB,XC
;экономия 2*exx, если читать UV после сортировки
        POP HL
        LD B,(HL)
        DEC H
        LD C,(HL)
        POP HL
        LD D,(HL)
        DEC H
        LD E,(HL)
        POP HL
        LD A,(HL)
        DEC H
        LD L,(HL),H,A
        CP D
       IFN texdec
        JP C,TRIsortDH
       ELSE
        JP NC,TRIsortDH
       ENDIF
        CP B
       IFN texdec
        JP C,TRIsortBHD
       ELSE
        JP NC,TRIsortBHD ;B<=H<D
       ENDIF
        LD A,D ;H меньше всех
        CP B
       IFN texdec
        JC TRIsortHBD
       ELSE
        JNC TRIsortHBD ;B<=H<D
       ENDIF
        TRISORT H,L,D,E,B,C
        JP TRIsortQ
TRIsortHBD
        TRISORT H,L,B,C,D,E
        JP TRIsortQ
TRIsortBHD
        TRISORT B,C,H,L,D,E
        JP TRIsortQ
TRIsortDH
        CP B
       IFN texdec
       JNC TRIsortDHB
       ELSE
        JC TRIsortDHB ;D<=H<B
       ENDIF
        LD A,D ;H больше всех
        CP B
       IFN texdec
        JP C,TRIsortBDH
       ELSE
        JP NC,TRIsortBDH ;B<=D<=H
       ENDIF
        TRISORT D,E,B,C,H,L
        JP TRIsortQ
TRIsortDHB
        TRISORT D,E,H,L,B,C
        JP TRIsortQ
TRIsortBDH
        TRISORT B,C,D,E,H,L
TRIsortQ
       IFN trisortmesspop
        LD A,B
        RLCA
        OR C
        RLCA
        OR D
        RLCA
        OR E
        RLCA
        OR H
        RLCA
        OR L
       ENDIF
        LD (vujump),A
        LD A,gspgdiv
        GSSETPGA ;нельзя call

;dXL=l13/H13 (+-7.8=+-6.0/6.0)
l13SLA=$+2
mH13=$+3
mHl13=$+2
        LD DE,(0)
;W=l12-l13*(H12/H13) неточно
;W=l12-H12*dXL точно (+-6.0=6.0*+-7.8) - начинается с H12<<2
H12cplshl2=$+1
        LD A,0
        TRIMULLOOP ;A~*DE
        LD HL,(vujump)
l12=$+1
        SUB 0 ;A=-W
        JP P,TRILEFT
;правый треугольник
       CP -1 ;слишком узкий, может дать отрицательный сканлайн
       JP Z,TRIQ
       IFN incW
        DEC A ;|W| на 1 больше, т.к. точность XR-XL равна +-1
       ENDIF
        LD (mW),A ;W=+W>0
       POP AF
       CP (HL)
       IFN texdec
      JP NZ,TRIQ
       ELSE
       JP Z,TRIQ
       ENDIF
        LD A,#3D ;dec a
        LD (TRINEGWIDTH),A
        LD HL,X1 ;R (ломаный край, физически правый)
        LD A,(HL)
        DEC (HL) ;чтобы не рисовать правую сторону
        LD (XL1),A
        LD A,'TEXTURERDEC
        JP TRILEFTQ
TRILEFT
;левый треугольник
       CP 2 ;0/1 слишком узкий,может дать отрицательный сканлайн
       JP C,TRIQ
       IFN incW
        CPL ;|W| на 1 больше, т.к. точность XR-XL равна +-1
       ELSE
        NEG
       ENDIF
        LD (mW),A ;W=-W>0
       POP AF
       CP (HL)
       IFN texdec
       JP Z,TRIQ
       ELSE
       JP NZ,TRIQ
       ENDIF
        LD A,#2F ;cpl
        LD (TRINEGWIDTH),A
        LD A,(X1) ;R (ломаный край, физически левый)
        DEC A ;чтобы cpl вместо neg
        LD (XL1),A
        LD A,'TEXTURERINC
TRILEFTQ
        LD (texturerH),A
        INC A ;PATCHVUINC/DEC
        LD (PATCHVUJP+2),A
        LD (dXL),DE

;dXR23=l23/H23 (+-7.8=+-6.0/6.0)
;l23SLA=$+1
mH23=$+2
mHl23=$+1
        LD HL,(0)
        LD (dXR23),HL
;можно выиграть 3 такта, если считать вверху: -6-16-16+4+18+13
;но так невыгодно для отсечения невидимых

;dUV13=texarea/H13 (6.8=f(6.0))
;абстрактная дельта без знака,
;потом посчитаем знаки (+/-/0) по U,V
mH13_=$+3
        LD DE,(texarea*2) ;63/H13
;t=абстрактная координата в текстуре на строке W на ребре 1-3
;t=64*H12/H13 неточно
;t=0.5+H12*dUV13 точно (6.0=6.0*6.8)
        LD A,(H12cplshl2)
;деления в dU,dV нужны, чтобы узнать шаг, с которым
;за W проходов мы погасим расстояние до бордюра текстуры (0.5)
;это +-t или +-(texarea-t) - поэтому t считается без бордюра
        TRIMULLOOP ;A~*DE ;знак=P после умножения
        ADD A,A ;для использования в таблице деления
        LD LX,A;(t),A ;>=0
vujump=$+1
        JP VUjump ;там jp/jp p на нужную ветку по U1..3,V1..3

;на входе в VU....:
;DE=dUV13
;A=t (пока _At=1)
       MACRO dVU0_0_1
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=-t/W точно (+-7.8=+-6.0/+6.0)
        IFN _At
        NEG
        ELSE
       ;LD A,(t)
       ;NEG
       XOR A
       SUB LX
        ENDIF
        LD L,A
        _At=0
       ENDM

       MACRO dVU0_1_0
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=63/W точно (+-7.8=+-6.0/+6.0)
        LD L,texarea*2
       ENDM

       MACRO dVU0_1_1
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=(63-t)/W точно (+-7.8=+-6.0/+6.0)
        IFN _At
        SUB texarea*2+1
        CPL
        ELSE
       ;LD A,(t)
       ;SUB texarea*2+1
       ;CPL
       LD A,texarea*2
       SUB LX
        ENDIF
        LD L,A
        _At=0
       ENDM

       MACRO dVU1_0_0
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=(t-63)/W точно (+-7.8=+-6.0/+6.0)
        IF0 _At
        LD A,LX;(t)
        ENDIF
        SUB texarea*2
        LD L,A
        _At=0
       ENDM

       MACRO dVU1_0_1
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=-63/W точно (+-7.8=+-6.0/+6.0)
        LD L,-texarea*2
       ENDM

       MACRO dVU1_1_0
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=t/W точно (+-7.8=+-6.0/+6.0)
        IF0 _At
        LD A,LX;(t)
        ENDIF
        LD L,A
        _At=0
       ENDM

;для разных движений по текстуре на ребре 1-3:
       ;\0: 2 байта патча
       ;\1: 3-й байт и IY:
            ;0=POP HL:LD IY,\2<8+#80                    ;\2=minV
            ;1=POP HL:LD IY,\2<8+#80:inv DE             ;\2=maxV
            ;2=POP AF:ADD A,\2:LD L,A:LD IY,minV<8+#80
            ;3=POP AF:ADD A,\2:LD L,A:LD IY,maxV<8+#80:inv DE
            ;4=LD L,\2:POP IY:LD LY,#80
            ;5=LD L,\2:POP AF:ADD A,63:LD HY,A:LD LY,#80:inv DE
       ;\4: 4-й байт патча: jp (hl)/sub b/add a,b
       MACRO VUPATCH
        LD HL,\0
        LD (trilineBC+1),HL
       IF0 \1&1
        LD (dUV13),DE ;в теле сканлинии
       ELSE
        _At=0
        XOR A
        SUB E
        LD L,A
        SBC A,A
        SUB D
        LD H,A
        LD (dUV13),HL ;в теле сканлинии
       ENDIF
       IF0 \1&6 ;0,1
        POP HL
        LD IY,\2<8+#80
       ELSE
       IF0 \1&4 ;2,3
        _At=0
        POP AF
        ADD A,\2
        LD L,A
        IF0 \1&1
        LD IY,minV<8+#80 ;2
        ELSE
        LD IY,maxV<8+#80 ;3
        ENDIF
       ELSE    ;4,5
        LD L,\2
        IF0 \1&1
        POP IY ;4
        ELSE  ;5
        _At=0
        POP AF
        ADD A,texarea
        LD HY,A
        ENDIF
        LD LY,#80
       ENDIF
       ENDIF
        LD H,\3
        LD (trilineBC+3),HL
       ENDM

;вертикальные:
       MACRO VUPATCH00__10
        VUPATCH #0E44,0,minV,#E9 ;b,hy,c,texx
       ENDM
       MACRO VUPATCH10__00
        VUPATCH #0E44,1,maxV,#E9 ;b,hy,c,texx
       ENDM
       MACRO VUPATCH01__11
        VUPATCH #0E44,2,texarea,#E9 ;b,hy,c,texx+63
       ENDM
       MACRO VUPATCH11__01
        VUPATCH #0E44,3,texarea,#E9 ;b,hy,c,texx+63
       ENDM
;горизонтальные:
       MACRO VUPATCH00__01
        VUPATCH #064C,4,minV,#E9 ;c,hy,b,minV
       ENDM
       MACRO VUPATCH10__11
        VUPATCH #064C,4,maxV,#E9 ;c,hy,b,maxV
       ENDM
       MACRO VUPATCH01__00
        VUPATCH #064C,5,minV,#E9 ;c,hy,b,minV
       ENDM
       MACRO VUPATCH11__10
        VUPATCH #064C,5,maxV,#E9 ;c,hy,b,maxV
       ENDM
;диагональные:
       MACRO VUPATCH00__11
VUPATCH #3E44,2,texborder-minV,#80 ;b,hy,a,texX-minV:add a,b:c,a
       ENDM
       MACRO VUPATCH11__00
VUPATCH #3E44,3,texborder-minV,#80 ;b,hy,a,texX-minV:add a,b:c,a
       ENDM
       MACRO VUPATCH01__10
VUPATCH #3E44,2,maxV,#90 ;b,hy,a,texX+maxV:sub b:c,a
       ENDM
       MACRO VUPATCH10__01
VUPATCH #3E44,3,maxV,#90 ;b,hy,a,texX+maxV:sub b:c,a
       ENDM

;VU(V1)(U1)(V2)(U2)(V3)(U3)
       MACRO VU
VU\0\1\2\3\4\5
        _At=1
        VUPATCH\0\1__\4\5
        dVU\0_\2_\4 ;dV
        EXX
        dVU\1_\3_\5 ;dU
        JP PATCHVU
       ENDM
        VU 0,0,0,1,1,0
        VU 0,0,0,1,1,1
        VU 0,0,1,0,0,1
        VU 0,0,1,0,1,1
        VU 0,0,1,1,0,1
        VU 0,0,1,1,1,0
        VU 0,1,0,0,1,0
        VU 0,1,0,0,1,1
        VU 0,1,1,0,0,0
        VU 0,1,1,0,1,1
        VU 0,1,1,1,0,0
        VU 0,1,1,1,1,0
        VU 1,0,0,0,0,1
        VU 1,0,0,0,1,1
        VU 1,0,0,1,0,0
        VU 1,0,0,1,1,1
        VU 1,0,1,1,0,0
        VU 1,0,1,1,0,1
        VU 1,1,0,0,0,1
        VU 1,1,0,0,1,0
        VU 1,1,0,1,0,0
        VU 1,1,0,1,1,0
        VU 1,1,1,0,0,0
        VU 1,1,1,0,0,1

PATCHVU
mW=$+1
        LD H,0
        LD A,H
        LD E,(HL) ;
        INC L     ;
        LD D,(HL) ;из pgdiv
       LD HL,#0180
       ADD HL,DE ;правильное округление
       DEC H ;ADD HL,DE не дает флаг знака
        EXX
        LD H,A
        LD E,(HL) ;
        INC HL    ;
        LD D,(HL) ;из pgdiv
       LD HL,#0080
       JP P,$+4
       DEC H ;компенсация паразитного V+1 при dU<0
       ADD HL,DE ;правильное округление
;последний вывод не патчим - там ld a,(bc)
;на всякий случай патчим на 1 пиксель больше, чем W
;(которое само с запасом 1 пиксель)
;потому что ошибка XL + ошибка XR + ошибка деления*H12 = .5+.5+?
;H=-1..-4: B=-1
;H=-5..-8: B=-2
        RRA
        RRA
        OR #C0
        LD B,A
PATCHVUJP=$
        JP PATCHVUINC;DEC

TRIDRAW
;dXR12=l12/H12 (+-7.8=+-6.0/6.0)
l12SLA=$+2
mH12=$+3
;mHl12=$+2
        LD DE,(0) ;dXR12
       IF0 ?lambert
        POP HL ;vnormal
        LD A,(HL) ;Y
        SUB lambert
       DUP lambertshr
        SRA A
       EDUP
        JP P,$+4
        XOR A
        CP lambertmax
        JC $+4
        LD A,lambertmax
        ADD A,gspgtex*257<1
        RRCA
        GSSETPGA ;включаем pgtex
       ELSE
        POP AF ;pgtex
        GSSETPGA ;включаем pgtex
       ENDIF
dXL=$+1
        LD BC,0
XL1=$+2
        LD HL,#80 ;Xleft+1 (левый), Xleft (правый)
        EXX
YX1=$+1
X1=$+1
Y1=$+2
        LD DE,0
       IFN texdec
       BIT 6,D
       JZ TRIDRAWnEXTRAY
        RES 6,D
        LD A,E
        XOR #80
        LD E,A
        EXX
        LD A,H
        XOR #80
        LD H,A
        EXX
TRIDRAWnEXTRAY
       ENDIF
        LD HX,E ;Xright (левый), Xright-1 (правый)
        LD LX,#80
        LD A,(mH12)
        ADD A,A
        JZ TRIDRAW120 ;H12=0
        LD L,A
        LD H,'TRISTACK12
        LD SP,HL
        JP TRISCANGO

TRIDRAW120
        LD A,(mH23)
        ADD A,A
        LD L,A
        LD H,'TRISTACK23
        LD SP,HL
        EXX
        LD A,(l12)
        ADD A,HX
        LD HX,A ;Xright
        LD DE,(dXR23)
        EXX
        JP TRISCANGO

TRIDRAW23
        LD A,(mH23)
        ADD A,A
        JZ TRIQ ;H23=0
        LD L,A
        LD H,'TRISTACK23
        LD SP,HL
        EXX
dXR23=$+1
        LD DE,0
        LD LX,#80 ;чтобы одинаковые ребра рисовались одинаково
        EXX

TRILINE ;цикл и вход (для нижней части полигона)
       IFN texdec
        DEC D
        JP P,TRILINEnEXTRAY
        LD D,gschmaphgt+'gschmap256-1
        LD A,HX
        XOR #80
        LD HX,A
        EXX
        LD A,H
        XOR #80
        LD H,A
        EXX
TRILINEnEXTRAY
       ELSE
        INC D ;Y
       ENDIF
TRISCANGO ;вход (для верхней части полигона)
dUV13=$+1
        LD BC,0
        ADD IY,BC ;U и/или V
        EXX
        ADD IX,DE ;Xright (левый), Xright-1 (правый)
        ADD HL,BC ;Xleft+1 (левый), Xleft (правый)
        LD A,H
        EXX
        LD E,HX ;Xright
       SUB E ;A=-1..63 (левый), -63..1 (правый)
TRINEGWIDTH=$
       CPL ;(левый), INC A (правый)
      IFN debug
     ;DEC A
     ;JP P,$
     ;INC A
     NEG
     CP trimaxl
     JNC $
     NEG
      ENDIF
        LD L,A
texturerH=$+1
        LD H,0;'TEXTURERINC;DEC
        LD L,(HL)
trilineBC=$
        LD B,HY ;V
        LD A,-192
        ADD A,B
        LD C,A ;U
        JP (HL) ;TEXTURERINC;DEC

TRIQ
TRIQSP=$+1
        LD SP,0
        RET

