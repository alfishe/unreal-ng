; =====================================================================================================================
; texpp4 - the affine texture mapper's triangle code (The Link, Alone Coder, 2009; card side, runs on the NeoGS Z80).
; Included by TEX28.asm (textured face) and hedge12.asm (hedgehog); no object of its own. The includer provides:
; texdec (1 in both: scanlines run with DEC D and wrap in the 64-row chunk map), trisortmesspop, incW, debug,
; texborder/texarea/minV/maxV, gspgdiv (division table page), GSSETPGA, gschmap256/gschmaphgt, the generated
; scanline code TEXTURERINC/DEC + PATCHVUINC/DEC (MTEXTURER/PATCHVUCODE), vujump.asm (VUjump, TRISTACK12/23) and,
; in hedge12 only, lambert/lambertshr/lambertmax (texture page chosen by a light level).
;
; TRI: HL = face record (see TEX28's KUB / KPOLY). TRI sets SP to the record and POPs it: 3 vertex addresses, the 3
;   texture corners (V,U), the side word (#C3xx / #F2xx), the texture origin (in VUPATCH); the patcher's last RET pops
;   DW TRIDRAW; TRIDRAW pops the texture page (or, with lambert, a normal pointer). TRIQ restores SP and returns.
;   1. sort the vertices by Y (6 cases, TRISORT expanded): heights H12/H13/H23 and X spans l12/l13/l23 go into the
;      operands of later instructions (self-modifying code); the texture corners give a 6-bit index into VUjump;
;   2. dXL = l13/H13, W = l12 - H12*dXL (row width at vertex 2): its sign tells a "left" from a "right" triangle;
;      the side word of the record is compared with the opcode of the VUjump entry: a mismatch = back face, skipped;
;      right triangles use TEXTURERDEC (right to left), left ones TEXTURERINC;
;   3. dUV13 = texarea/H13 (step of the start point along edge 1-3 in the texture), t = H12*dUV13;
;   4. JP through VUjump to VUxxxxxx: since every vertex sits on a corner of the 64x64 texture square, edge 1-3 runs
;      along a side or a diagonal: VUPATCH patches the code at trilineBC that turns IY into BC (vertical, horizontal
;      or diagonal) and sets IY; dVU* give the U and V change over the row W;
;   5. PATCHVU: divide by W (table) and run PATCHVUINC/DEC, which write the per-pixel texture offsets into the
;      scanline code; its RET goes to TRIDRAW;
;   6. TRIDRAW: SP = TRISTACK12 + 2*H12 (a table of return addresses in vujump.asm): every scanline JP (HL)s into the
;      texturer, whose RET pops the next TRILINE; the last entry is TRIDRAW23 (lower half), then TRIQ.
; Scanline registers: D = row (Y), E = start X; IX = right X (8.8), HL' = left X (8.8), BC' = dXL, DE' = dXR;
;   IY = position along edge 1-3 in the texture (8.8), BC = texture address of the first pixel (set at trilineBC).
; Fixed-point formats in the author's notes: "6.8" = 6 integer . 8 fraction bits, "+-" = signed.
; =====================================================================================================================
; ALASM MAIN "*" (the project's main source)

; en: right triangle: vertex 2 to the right, row W (the widest) at vertex 2 / left triangle: vertex 2 to the left;
; en: maximum size 63x63. The 3D engine's X grows upwards on the screen, its Y to the right.
;правый треугольник
;  1
; />2  - строка W (самая широкая) (en: row W, the widest)
;3
;левый треугольник
;   1
;2</  - строка W (самая широкая) (en: row W, the widest)
;  3
;максимальные размеры 63x63

;X 3D-движка растет на экране вверх
;Y 3D-движка растет на экране вправо

; en: t DB 0 ;used in VU.... (holds t*2) - now in LX (IXL)
; en: t = abstract coordinate in the texture on row W on edge 1-3; t=64*H12/H13 inexact; t=0.5+H12*dUV13 exact
; en: the divisions in dU,dV are needed to find the step with which, in W passes, we cancel the distance to the
; en: texture border (0.5); it is +-t or +-(texarea-t) - so t is computed without the border
;t       DB 0 ;используется в VU.... (содержит t*2)
;теперь в LX
;t=абстрактная координата в текстуре на строке W на ребре 1-3
;t=64*H12/H13 неточно
;t=0.5+H12*dUV13 точно (6.0=6.0*6.8)
;деления в dU,dV нужны, чтобы узнать шаг, с которым
;за W проходов мы погасим расстояние до бордюра текстуры (0.5)
;это +-t или +-(texarea-t) - поэтому t считается без бордюра

; TRIMULLOOP: multiply DE by the multiplier held complemented ("A~") in the top bits of A, shift-and-add, the
; rounded high byte of the product in A (S flag set from it). The IF 0 branch is the older straight version.
; en: multiplication +-6.0=6.0*+-7.8 (H=A~*DE)
;умножение +-6.0=6.0*+-7.8 (H=A~*DE)
        MACRO TRIMULLOOP
        IF 0
; ALASM: LD HL,#0080>5 (> = 16-bit cyclic shift right)
        LD HL,+(((#0080&#FFFF)>>>5|(#0080&#FFFF)<<(16-5))&#FFFF) ;правильное округление (en: correct rounding)
        ADD A,A
        JP C,$+4                ;для мелких быстрее (en: faster for small ones)
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JP C,$+4                ;для мелких быстрее (en: faster for small ones)
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JP C,$+4                ;для мелких быстрее (en: faster for small ones)
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
        ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
        LD A,H                  ;правильное округление (en: correct rounding)
;173..202t+?
;ZF=1
        ELSE

        LD H,D
        LD L,E                  ;для старшей единицы (en: for the top 1 bit)
        ADD A,A
        JR NC,._ADD5
        ADD A,A
        JR NC,._ADD4
        ADD A,A
        JR NC,._ADD3
        ADD A,A
        JR NC,._ADD2
        ADD A,A
        JR NC,._ADD1
        ADD A,A
        JR NC,._ADD0
;A=0
        JP ._ADDQ
._ADD5  ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
._ADD4  ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
._ADD3  ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
._ADD2  ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
._ADD1  ADD HL,HL
        ADD A,A
        JR C,$+3
        ADD HL,DE
._ADD0
;A=0
;LD DE,#0080
;ADD HL,DE ;правильное округление (en: correct rounding)
;LD A,H
        RL L                    ;правильное округление (en: correct rounding)
        ADC A,H
._ADDQ

        ENDIF
        ENDM
; en: one could make 64 multiply routines (one for each A)
;можно сделать 64 процедуры умножения (для каждого A)

; Original TRISORT macro: \0\1 = registers of vertex 1 (Y,X), \2\3 = vertex 2, \4\5 = vertex 3. The converter expanded
; it at its 6 uses; ALASM's IF0 "\0\1"-"BC" compared register names as 2-character numbers, which the expansions
; show as constants: "BC" = 16963, "DE" = 17477, "HL" = 18508.
; (expanded at its calls)        MACRO TRISORT
; (expanded at its calls)         LD (YX1),\0\1
; (expanded at its calls)        IFN texdec
; (expanded at its calls)         LD A,\4
; (expanded at its calls)         SUB \0
; (expanded at its calls)        ELSE
; (expanded at its calls)         LD A,\0
; (expanded at its calls)         SUB \4
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP Z,TRIQ
; (expanded at its calls)         LD (mH13),A
; (expanded at its calls)         LD (mH13_),A
; (expanded at its calls)         LD A,\5
; (expanded at its calls)         SUB \1
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         LD (l13SLA),A
; (expanded at its calls)        IFN texdec
; (expanded at its calls)         LD A,\2
; (expanded at its calls)         SUB \0
; (expanded at its calls)        ELSE
; (expanded at its calls)         LD A,\0
; (expanded at its calls)         SUB \2
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD (mH12),A
; (expanded at its calls)        DEC A ;даст то же, что H12 CPL
; (expanded at its calls)        ADD A,A
; (expanded at its calls)        ADD A,A
; (expanded at its calls)        LD (H12cplshl2),A
; (expanded at its calls)         LD A,\3
; (expanded at its calls)         SUB \1
; (expanded at its calls)         LD (l12),A
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         LD (l12SLA),A
; (expanded at its calls)        IFN texdec
; (expanded at its calls)         LD A,\4
; (expanded at its calls)         SUB \2
; (expanded at its calls)        ELSE
; (expanded at its calls)         LD A,\2
; (expanded at its calls)         SUB \4
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD H,A
; (expanded at its calls)         LD A,\5
; (expanded at its calls)         SUB \3
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         LD L,A
; (expanded at its calls)         LD (mHl23),HL
; (expanded at its calls)        IF0 trisortmesspop
; (expanded at its calls)         POP BC
; (expanded at its calls)         POP DE
; (expanded at its calls)         POP HL
; (expanded at its calls)         LD A,\0
; (expanded at its calls)         RLCA
; (expanded at its calls)         OR \1
; (expanded at its calls)         RLCA
; (expanded at its calls)         OR \2
; (expanded at its calls)         RLCA
; (expanded at its calls)         OR \3
; (expanded at its calls)         RLCA
; (expanded at its calls)         OR \4
; (expanded at its calls)         RLCA
; (expanded at its calls)         OR \5
; (expanded at its calls)        ELSE
; (expanded at its calls)         IF0 "\0\1"-"BC"
; (expanded at its calls)         POP BC
; (expanded at its calls)         ELSE
; (expanded at its calls)         IF0 "\2\3"-"BC"
; (expanded at its calls)         POP DE
; (expanded at its calls)         ELSE
; (expanded at its calls)         POP HL
; (expanded at its calls)         ENDIF
; (expanded at its calls)         ENDIF
; (expanded at its calls)         IF0 "\0\1"-"DE"
; (expanded at its calls)         POP BC
; (expanded at its calls)         ELSE
; (expanded at its calls)         IF0 "\2\3"-"DE"
; (expanded at its calls)         POP DE
; (expanded at its calls)         ELSE
; (expanded at its calls)         POP HL
; (expanded at its calls)         ENDIF
; (expanded at its calls)         ENDIF
; (expanded at its calls)         IF0 "\0\1"-"HL"
; (expanded at its calls)         POP BC
; (expanded at its calls)         ELSE
; (expanded at its calls)         IF0 "\2\3"-"HL"
; (expanded at its calls)         POP DE
; (expanded at its calls)         ELSE
; (expanded at its calls)         POP HL
; (expanded at its calls)         ENDIF
; (expanded at its calls)         ENDIF
; (expanded at its calls)        ENDIF
; (expanded at its calls)        ENDM

; ---------------------------------------------------------------------------------------------------------------------
; TRI - draw one textured triangle. HL = face record (POPped as a stack; see the header).
; ---------------------------------------------------------------------------------------------------------------------
TRI
        LD (TRIQSP),SP
        LD SP,HL
; en: Read + sort + heights + widths. Sort the vertices A,B,C (with parameters X,Y,U,V) by Y; the result
; en: (vertices 1,2,3 with parameters) at certain fixed addresses. H13=Y3-Y1 - if 0, then EXIT; l13=X3-X1;
; en: H12=Y2-Y1, l12=X2-X1; H23=Y3-Y2, l23=X3-X2. If I have YA,YB,YC, I already have 3 pointers, or XA,XB,XC are
; en: already read; saves 2*exx if UV are read after sorting.
; Vertices: B,C / D,E / H,L = Y,X of A / B / C (Y from the record's address, X one row below).
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
        LD L,(HL)
        LD H,A
        CP D
        IF texdec
        JP C,TRIsortDH
        ELSE
        JP NC,TRIsortDH
        ENDIF
        CP B
        IF texdec
        JP C,TRIsortBHD
        ELSE
        JP NC,TRIsortBHD        ;B<=H<D
        ENDIF
        LD A,D                  ;H меньше всех (en: H is the smallest)
        CP B
        IF texdec
        JR C,TRIsortHBD
        ELSE
        JR NC,TRIsortHBD        ;B<=H<D
        ENDIF
; First case: vertex 1 = HL, 2 = DE, 3 = BC. Each expansion stores YX1, H13 (twice), l13*2, H12, (H12-1)*4,
; l12, l12*2, H23:l23*2 into the operands below, then POPs the three VU words in the record's order.
; macro TRISORT expanded
        LD (YX1),HL
        IF texdec
        LD A,B
        SUB H
        ELSE
        LD A,H
        SUB B
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,C
        SUB L
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,D
        SUB H
        ELSE
        LD A,H
        SUB D
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,E
        SUB L
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,B
        SUB D
        ELSE
        LD A,D
        SUB B
        ENDIF
        LD H,A
        LD A,C
        SUB E
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
        LD A,H
        RLCA
        OR L
        RLCA
        OR D
        RLCA
        OR E
        RLCA
        OR B
        RLCA
        OR C
        ELSE
        IF (18508-16963)==0
        POP BC
        ELSE
        IF (17477-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (18508-17477)==0
        POP BC
        ELSE
        IF (17477-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (18508-18508)==0
        POP BC
        ELSE
        IF (17477-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
        JP TRIsortQ
TRIsortHBD
; macro TRISORT expanded
        LD (YX1),HL
        IF texdec
        LD A,D
        SUB H
        ELSE
        LD A,H
        SUB D
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,E
        SUB L
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,B
        SUB H
        ELSE
        LD A,H
        SUB B
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,C
        SUB L
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,D
        SUB B
        ELSE
        LD A,B
        SUB D
        ENDIF
        LD H,A
        LD A,E
        SUB C
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
        LD A,H
        RLCA
        OR L
        RLCA
        OR B
        RLCA
        OR C
        RLCA
        OR D
        RLCA
        OR E
        ELSE
        IF (18508-16963)==0
        POP BC
        ELSE
        IF (16963-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (18508-17477)==0
        POP BC
        ELSE
        IF (16963-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (18508-18508)==0
        POP BC
        ELSE
        IF (16963-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
        JP TRIsortQ
TRIsortBHD
; macro TRISORT expanded
        LD (YX1),BC
        IF texdec
        LD A,D
        SUB B
        ELSE
        LD A,B
        SUB D
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,E
        SUB C
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,H
        SUB B
        ELSE
        LD A,B
        SUB H
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,L
        SUB C
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,D
        SUB H
        ELSE
        LD A,H
        SUB D
        ENDIF
        LD H,A
        LD A,E
        SUB L
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
        LD A,B
        RLCA
        OR C
        RLCA
        OR H
        RLCA
        OR L
        RLCA
        OR D
        RLCA
        OR E
        ELSE
        IF (16963-16963)==0
        POP BC
        ELSE
        IF (18508-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (16963-17477)==0
        POP BC
        ELSE
        IF (18508-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (16963-18508)==0
        POP BC
        ELSE
        IF (18508-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
        JP TRIsortQ
TRIsortDH
        CP B
        IF texdec
        JR NC,TRIsortDHB
        ELSE
        JR C,TRIsortDHB         ;D<=H<B
        ENDIF
        LD A,D                  ;H больше всех (en: H is the largest)
        CP B
        IF texdec
        JP C,TRIsortBDH
        ELSE
        JP NC,TRIsortBDH        ;B<=D<=H
        ENDIF
; macro TRISORT expanded
        LD (YX1),DE
        IF texdec
        LD A,H
        SUB D
        ELSE
        LD A,D
        SUB H
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,L
        SUB E
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,B
        SUB D
        ELSE
        LD A,D
        SUB B
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,C
        SUB E
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,H
        SUB B
        ELSE
        LD A,B
        SUB H
        ENDIF
        LD H,A
        LD A,L
        SUB C
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
        LD A,D
        RLCA
        OR E
        RLCA
        OR B
        RLCA
        OR C
        RLCA
        OR H
        RLCA
        OR L
        ELSE
        IF (17477-16963)==0
        POP BC
        ELSE
        IF (16963-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (17477-17477)==0
        POP BC
        ELSE
        IF (16963-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (17477-18508)==0
        POP BC
        ELSE
        IF (16963-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
        JP TRIsortQ
TRIsortDHB
; macro TRISORT expanded
        LD (YX1),DE
        IF texdec
        LD A,B
        SUB D
        ELSE
        LD A,D
        SUB B
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,C
        SUB E
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,H
        SUB D
        ELSE
        LD A,D
        SUB H
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,L
        SUB E
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,B
        SUB H
        ELSE
        LD A,H
        SUB B
        ENDIF
        LD H,A
        LD A,C
        SUB L
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
        LD A,D
        RLCA
        OR E
        RLCA
        OR H
        RLCA
        OR L
        RLCA
        OR B
        RLCA
        OR C
        ELSE
        IF (17477-16963)==0
        POP BC
        ELSE
        IF (18508-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (17477-17477)==0
        POP BC
        ELSE
        IF (18508-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (17477-18508)==0
        POP BC
        ELSE
        IF (18508-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
        JP TRIsortQ
TRIsortBDH
; macro TRISORT expanded
        LD (YX1),BC
        IF texdec
        LD A,H
        SUB B
        ELSE
        LD A,B
        SUB H
        ENDIF
        JP Z,TRIQ
        LD (mH13),A
        LD (mH13_),A
        LD A,L
        SUB C
        ADD A,A
        LD (l13SLA),A
        IF texdec
        LD A,D
        SUB B
        ELSE
        LD A,B
        SUB D
        ENDIF
        LD (mH12),A
        DEC A                   ;даст то же, что H12 CPL (en: gives the same as H12 CPL)
        ADD A,A
        ADD A,A
        LD (H12cplshl2),A
        LD A,E
        SUB C
        LD (l12),A
        ADD A,A
        LD (l12SLA),A
        IF texdec
        LD A,H
        SUB D
        ELSE
        LD A,D
        SUB H
        ENDIF
        LD H,A
        LD A,L
        SUB E
        ADD A,A
        LD L,A
        LD (mHl23),HL
        IF (trisortmesspop)==0
        POP BC
        POP DE
        POP HL
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
        ELSE
        IF (16963-16963)==0
        POP BC
        ELSE
        IF (17477-16963)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (16963-17477)==0
        POP BC
        ELSE
        IF (17477-17477)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        IF (16963-18508)==0
        POP BC
        ELSE
        IF (17477-18508)==0
        POP DE
        ELSE
        POP HL
        ENDIF
        ENDIF
        ENDIF
; Build the VUjump index from the three VU words (each 0 or 4 in V and U): %(V)(U)(V)(U)(V)(U)00
TRIsortQ
        IF trisortmesspop
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
; division table at #C000; with SP inside the record nothing may be CALLed
        LD A,gspgdiv
        GSSETPGA                ;нельзя call (en: no CALL possible)

;dXL=l13/H13 (+-7.8=+-6.0/6.0)
; division by table: the operand address of LD DE,(nn) is H13 : l13*2 (patched above) = the entry of the table at
; #C000 (row = -H, column = L*2), so DE = l13/H13. The same trick below for dXR23 (mHl23) and texarea/H13 (mH13_).
l13SLA=$+2
mH13=$+3
mHl13=$+2
        LD DE,(0)
;W=l12-l13*(H12/H13) неточно (en: inexact)
;W=l12-H12*dXL точно (+-6.0=6.0*+-7.8) - начинается с H12<<2 (en: exact - starts with H12<<2)
H12cplshl2=$+1
        LD A,0
        TRIMULLOOP              ;A~*DE
        LD HL,(vujump)
l12=$+1
        SUB 0                   ;A=-W
        JP P,TRILEFT
;правый треугольник
        CP -1                   ;слишком узкий, может дать отрицательный сканлайн (en: too narrow, may give a negative scanline)
        JP Z,TRIQ
        IF incW
        DEC A                   ;|W| на 1 больше, т.к. точность XR-XL равна +-1 (en: |W| one more, since XR-XL is accurate to +-1)
        ENDIF
        LD (mW),A               ;W=+W>0
        POP AF                  ; A = the record's side byte (#C3 / #F2), compared with the VUjump entry's opcode
        CP (HL)
        IF texdec
        JP NZ,TRIQ
        ELSE
        JP Z,TRIQ
        ENDIF
        LD A,#3D                ;dec a
        LD (TRINEGWIDTH),A
        LD HL,X1                ;R (ломаный край, физически правый) (en: R (the broken edge, physically right))
        LD A,(HL)
        DEC (HL)                ;чтобы не рисовать правую сторону (en: so as not to draw the right side)
        LD (XL1),A
        LD A,high TEXTURERDEC
        JP TRILEFTQ
TRILEFT
;левый треугольник
        CP 2                    ;0/1 слишком узкий,может дать отрицательный сканлайн (en: 0/1 too narrow, may give a negative scanline)
        JP C,TRIQ
        IF incW
        CPL                     ;|W| на 1 больше, т.к. точность XR-XL равна +-1 (en: |W| one more, since XR-XL is accurate to +-1)
        ELSE
        NEG
        ENDIF
        LD (mW),A               ;W=-W>0
        POP AF
        CP (HL)
        IF texdec
        JP Z,TRIQ
        ELSE
        JP NZ,TRIQ
        ENDIF
        LD A,#2F                ;cpl
        LD (TRINEGWIDTH),A
        LD A,(X1)               ;R (ломаный край, физически левый) (en: R (the broken edge, physically left))
        DEC A                   ;чтобы cpl вместо neg (en: so that CPL instead of NEG)
        LD (XL1),A
        LD A,high TEXTURERINC
TRILEFTQ
        LD (texturerH),A
        INC A                   ;PATCHVUINC/DEC
        LD (PATCHVUJP+2),A
        LD (dXL),DE

;dXR23=l23/H23 (+-7.8=+-6.0/6.0)
;l23SLA=$+1
mH23=$+2
mHl23=$+1
        LD HL,(0)
        LD (dXR23),HL
;можно выиграть 3 такта, если считать вверху: -6-16-16+4+18+13 (en: 3 cycles could be won by computing it above: ...)
;но так невыгодно для отсечения невидимых (en: but that does not pay for culling invisible ones)

;dUV13=texarea/H13 (6.8=f(6.0))
;абстрактная дельта без знака, (en: abstract delta without a sign,)
;потом посчитаем знаки (+/-/0) по U,V (en: later we work out the signs (+/-/0) by U,V)
mH13_=$+3
        LD DE,(texarea*2)       ;63/H13
;t=абстрактная координата в текстуре на строке W на ребре 1-3
;t=64*H12/H13 неточно
;t=0.5+H12*dUV13 точно (6.0=6.0*6.8)
        LD A,(H12cplshl2)
;деления в dU,dV нужны, чтобы узнать шаг, с которым
;за W проходов мы погасим расстояние до бордюра текстуры (0.5)
;это +-t или +-(texarea-t) - поэтому t считается без бордюра
        TRIMULLOOP              ;A~*DE ;знак=P после умножения (en: sign = P after the multiplication)
        ADD A,A                 ;для использования в таблице деления (en: for use in the division table)
        LD IXL,A                ;(t),A ;>=0
; JP into VUjump + index (the low byte is patched): JP or JP P to VUxxxxxx; IXL = 2t
vujump=$+1
        JP VUjump               ;там jp/jp p на нужную ветку по U1..3,V1..3 (en: there jp/jp p to the right branch by U1..3,V1..3)

; en: on entry to VU....: DE=dUV13, A=t (while _At=1)
; dVU<V1/U1>_<side>: the change of V (or U) over the row W, before the division: in the comments "точно" = exact,
; d? = ((?2-?1)-(?3-?1)*t)/W: -t, 63, 63-t, t-63, -63 or t (texarea*2 in 2x units). L = the result.
;на входе в VU....:
;DE=dUV13
;A=t (пока _At=1)
        MACRO dVU0_0_1
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=-t/W точно (+-7.8=+-6.0/+6.0)
        IF _At
        NEG
        ELSE
;LD A,(t)
;NEG
        XOR A
        SUB IXL
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
        IF _At
        SUB texarea*2+1
        CPL
        ELSE
;LD A,(t)
;SUB texarea*2+1
;CPL
        LD A,texarea*2
        SUB IXL
        ENDIF
        LD L,A
_At=0
        ENDM

        MACRO dVU1_0_0
;d?=((?2-?1)-(?3-?1)*t)/W
;d?=(t-63)/W точно (+-7.8=+-6.0/+6.0)
        IF (_At)==0
        LD A,IXL                ;(t)
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
        IF (_At)==0
        LD A,IXL                ;(t)
        ENDIF
        LD L,A
_At=0
        ENDM

; en: for the different movements over the texture along edge 1-3: \0: the 2 patch bytes; \1: the 3rd byte and IY:
; en: 0..5 as listed; \4: the 4th patch byte: jp (hl)/sub b/add a,b ("inv DE" = negate dUV13)
; VUPATCH writes trilineBC+1..+4: #0E44 = LD B,IYH : LD C,n (U fixed, V moves), #064C = LD C,IYH : LD B,n (V fixed),
; #3E44 = LD B,IYH : LD A,n : ADD A,B / SUB B (diagonal, then LD C,A). IY = start position (POP from the record or a
; constant). ALASM's \2<8+#80 = \2*256+#80 (cyclic shift by 8), spelled out by the converter.
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
        MACRO VUPATCH _arg0,_arg1,_arg2,_arg3
        LD HL,_arg0
        LD (trilineBC+1),HL
        IF (_arg1&1)==0
        LD (dUV13),DE           ;в теле сканлинии (en: in the scanline body)
        ELSE
_At=0
        XOR A
        SUB E
        LD L,A
        SBC A,A
        SUB D
        LD H,A
        LD (dUV13),HL           ;в теле сканлинии (en: in the scanline body)
        ENDIF
        IF (_arg1&6)==0         ;0,1
        POP HL
        LD IY,(((_arg2&#FFFF)<<8|(_arg2&#FFFF)>>>(16-8))&#FFFF)+#80
        ELSE
        IF (_arg1&4)==0         ;2,3
_At=0
        POP AF
        ADD A,_arg2
        LD L,A
        IF (_arg1&1)==0
        LD IY,(((minV&#FFFF)<<8|(minV&#FFFF)>>>(16-8))&#FFFF)+#80 ;2
        ELSE
        LD IY,(((maxV&#FFFF)<<8|(maxV&#FFFF)>>>(16-8))&#FFFF)+#80 ;3
        ENDIF
        ELSE                    ;4,5
        LD L,_arg2
        IF (_arg1&1)==0
        POP IY                  ;4
        ELSE                    ;5
_At=0
        POP AF
        ADD A,texarea
        LD IYH,A
        ENDIF
        LD IYL,#80
        ENDIF
        ENDIF
        LD H,_arg3
        LD (trilineBC+3),HL
        ENDM

; en: vertical:
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
; en: horizontal:
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
; en: diagonal:
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

; The VU handlers, one per possible corner combination (the label is the 6-bit index). Each: VUPATCH, dV, EXX, dU,
; JP PATCHVU. _At tracks whether A still holds t (an assembler variable).
;VU(V1)(U1)(V2)(U2)(V3)(U3)
; (expanded at its calls)        MACRO VU
; (expanded at its calls) VU\0\1\2\3\4\5
; (expanded at its calls)         _At=1
; (expanded at its calls)         VUPATCH\0\1__\4\5
; (expanded at its calls)         dVU\0_\2_\4 ;dV
; (expanded at its calls)         EXX
; (expanded at its calls)         dVU\1_\3_\5 ;dU
; (expanded at its calls)         JP PATCHVU
; (expanded at its calls)        ENDM
; macro VU expanded
VU000110
_At=1
        VUPATCH00__10
        dVU0_0_1                ;dV
        EXX
        dVU0_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU000111
_At=1
        VUPATCH00__11
        dVU0_0_1                ;dV
        EXX
        dVU0_1_1                ;dU
        JP PATCHVU
; macro VU expanded
VU001001
_At=1
        VUPATCH00__01
        dVU0_1_0                ;dV
        EXX
        dVU0_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU001011
_At=1
        VUPATCH00__11
        dVU0_1_1                ;dV
        EXX
        dVU0_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU001101
_At=1
        VUPATCH00__01
        dVU0_1_0                ;dV
        EXX
        dVU0_1_1                ;dU
        JP PATCHVU
; macro VU expanded
VU001110
_At=1
        VUPATCH00__10
        dVU0_1_1                ;dV
        EXX
        dVU0_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU010010
_At=1
        VUPATCH01__10
        dVU0_0_1                ;dV
        EXX
        dVU1_0_0                ;dU
        JP PATCHVU
; macro VU expanded
VU010011
_At=1
        VUPATCH01__11
        dVU0_0_1                ;dV
        EXX
        dVU1_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU011000
_At=1
        VUPATCH01__00
        dVU0_1_0                ;dV
        EXX
        dVU1_0_0                ;dU
        JP PATCHVU
; macro VU expanded
VU011011
_At=1
        VUPATCH01__11
        dVU0_1_1                ;dV
        EXX
        dVU1_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU011100
_At=1
        VUPATCH01__00
        dVU0_1_0                ;dV
        EXX
        dVU1_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU011110
_At=1
        VUPATCH01__10
        dVU0_1_1                ;dV
        EXX
        dVU1_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU100001
_At=1
        VUPATCH10__01
        dVU1_0_0                ;dV
        EXX
        dVU0_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU100011
_At=1
        VUPATCH10__11
        dVU1_0_1                ;dV
        EXX
        dVU0_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU100100
_At=1
        VUPATCH10__00
        dVU1_0_0                ;dV
        EXX
        dVU0_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU100111
_At=1
        VUPATCH10__11
        dVU1_0_1                ;dV
        EXX
        dVU0_1_1                ;dU
        JP PATCHVU
; macro VU expanded
VU101100
_At=1
        VUPATCH10__00
        dVU1_1_0                ;dV
        EXX
        dVU0_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU101101
_At=1
        VUPATCH10__01
        dVU1_1_0                ;dV
        EXX
        dVU0_1_1                ;dU
        JP PATCHVU
; macro VU expanded
VU110001
_At=1
        VUPATCH11__01
        dVU1_0_0                ;dV
        EXX
        dVU1_0_1                ;dU
        JP PATCHVU
; macro VU expanded
VU110010
_At=1
        VUPATCH11__10
        dVU1_0_1                ;dV
        EXX
        dVU1_0_0                ;dU
        JP PATCHVU
; macro VU expanded
VU110100
_At=1
        VUPATCH11__00
        dVU1_0_0                ;dV
        EXX
        dVU1_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU110110
_At=1
        VUPATCH11__10
        dVU1_0_1                ;dV
        EXX
        dVU1_1_0                ;dU
        JP PATCHVU
; macro VU expanded
VU111000
_At=1
        VUPATCH11__00
        dVU1_1_0                ;dV
        EXX
        dVU1_0_0                ;dU
        JP PATCHVU
; macro VU expanded
VU111001
_At=1
        VUPATCH11__01
        dVU1_1_0                ;dV
        EXX
        dVU1_0_1                ;dU
        JP PATCHVU

; ---------------------------------------------------------------------------------------------------------------------
; PATCHVU: L = dV numerator, L' = dU numerator, H = W -> per-pixel dV (HL) and dU (HL') from the table in page
; gspgdiv ((L/2)/-H, 2 bytes per entry), rounded; B = minus the number of 4-pixel groups (author: H=-1..-4:
; B=-1, ...); JP PATCHVUINC/DEC (the high byte patched in TRILEFTQ: the patcher sits in the 256 bytes after its
; texturer).
; ---------------------------------------------------------------------------------------------------------------------
PATCHVU
mW=$+1
        LD H,0
        LD A,H
        LD E,(HL)               ;
        INC L                   ;
        LD D,(HL)               ;из pgdiv (en: from pgdiv)
        LD HL,#0180
        ADD HL,DE               ;правильное округление (en: correct rounding)
        DEC H                   ;ADD HL,DE не дает флаг знака (en: ADD HL,DE does not set the sign flag)
        EXX
        LD H,A
        LD E,(HL)               ;
        INC HL                  ;
        LD D,(HL)               ;из pgdiv (en: from pgdiv)
        LD HL,#0080
        JP P,$+4
        DEC H                   ;компенсация паразитного V+1 при dU<0 (en: compensation of the parasitic V+1 when dU<0)
        ADD HL,DE               ;правильное округление
; en: the last output is not patched - there is ld a,(bc); just in case we patch 1 pixel more than W (which itself
; en: has a 1-pixel margin), because XL error + XR error + division error*H12 = .5+.5+?
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
        JP PATCHVUINC           ;DEC

; ---------------------------------------------------------------------------------------------------------------------
; TRIDRAW - reached by the patcher's RET (DW TRIDRAW in the record). Texture page from the record (or from the light
; level with lambert), the left/right X accumulators and Y1; with texdec a start row >= 64 is folded into the other
; half of the chunk map (row - 64, X xor #80). SP = TRISTACK12 + 2*H12 (H12 = 0: go straight to the lower part).
; ---------------------------------------------------------------------------------------------------------------------
TRIDRAW
;dXR12=l12/H12 (+-7.8=+-6.0/6.0)
l12SLA=$+2
mH12=$+3
;mHl12=$+2
        LD DE,(0)               ;dXR12
        IF exist lambert
; hedge12 (lambert): the record holds a normal pointer; its Y gives the light level 0..lambertmax = page offset
        POP HL                  ;vnormal
        LD A,(HL)               ;Y
        SUB lambert
        DUP lambertshr
        SRA A
        EDUP
        JP P,$+4
        XOR A
        CP lambertmax
        JR C,$+4
        LD A,lambertmax
; ALASM: ADD A,gspgtex*257<1 (the page as a plain index; RRCA rotates back)
        ADD A,low (((gspgtex*257&#FFFF)<<1|(gspgtex*257&#FFFF)>>>(16-1))&#FFFF) ; ALASM keeps the low byte
        RRCA
        GSSETPGA                ;включаем pgtex (en: map pgtex)
        ELSE
        POP AF                  ;pgtex
        GSSETPGA                ;включаем pgtex (en: map pgtex)
        ENDIF
dXL=$+1
        LD BC,0
XL1=$+2
        LD HL,#80               ;Xleft+1 (левый), Xleft (правый) (en: left triangle / right triangle)
        EXX
YX1=$+1
X1=$+1
Y1=$+2
        LD DE,0
        IF texdec
        BIT 6,D
        JR Z,TRIDRAWnEXTRAY
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
        LD IXH,E                ;Xright (левый), Xright-1 (правый) (en: left triangle / right triangle)
        LD IXL,#80
        LD A,(mH12)
        ADD A,A
        JR Z,TRIDRAW120         ;H12=0
        LD L,A
        LD H,high TRISTACK12
        LD SP,HL
        JP TRISCANGO

TRIDRAW120
        LD A,(mH23)
        ADD A,A
        LD L,A
        LD H,high TRISTACK23
        LD SP,HL
        EXX
        LD A,(l12)
        ADD A,IXH
        LD IXH,A                ;Xright
        LD DE,(dXR23)
        EXX
        JP TRISCANGO

; TRIDRAW23: the lower part (vertex 2 to 3): SP = TRISTACK23 + 2*H23, right edge step dXR23.
TRIDRAW23
        LD A,(mH23)
        ADD A,A
        JR Z,TRIQ               ;H23=0
        LD L,A
        LD H,high TRISTACK23
        LD SP,HL
        EXX
dXR23=$+1
        LD DE,0
        LD IXL,#80              ;чтобы одинаковые ребра рисовались одинаково (en: so that equal edges are drawn the same way)
        EXX

; TRILINE: next scanline (entered by the texturer's RET). texdec: row - 1, below 0 wraps to row 63 of the other half
; (X xor #80 on both edges). Then: start position += dUV13, edges += dXL / dXR, width -> entry of the texturer,
; BC = texture address (the 4 bytes at trilineBC are patched by VUPATCH), JP (HL).
TRILINE                         ;цикл и вход (для нижней части полигона) (en: loop and entry (for the lower part of the polygon))
        IF texdec
        DEC D
        JP P,TRILINEnEXTRAY
        LD D,gschmaphgt+high gschmap256-1
        LD A,IXH
        XOR #80
        LD IXH,A
        EXX
        LD A,H
        XOR #80
        LD H,A
        EXX
TRILINEnEXTRAY
        ELSE
        INC D                   ;Y
        ENDIF
TRISCANGO                       ;вход (для верхней части полигона) (en: entry (for the upper part of the polygon))
dUV13=$+1
        LD BC,0
        ADD IY,BC               ;U и/или V (en: U and/or V)
        EXX
        ADD IX,DE               ;Xright (левый), Xright-1 (правый)
        ADD HL,BC               ;Xleft+1 (левый), Xleft (правый)
        LD A,H
        EXX
        LD E,IXH                ;Xright
        SUB E                   ;A=-1..63 (левый), -63..1 (правый) (en: left triangle / right triangle)
TRINEGWIDTH=$
        CPL                     ;(левый), INC A (правый) (en: left / right triangle; patched at TRINEGWIDTH)
        IF debug
;DEC A
;JP P,$
;INC A
        NEG
        CP trimaxl
        JR NC,$
        NEG
        ENDIF
        LD L,A
texturerH=$+1
        LD H,0                  ;'TEXTURERINC;DEC
        LD L,(HL)
trilineBC=$
        LD B,IYH                ;V
        LD A,-192
        ADD A,B
        LD C,A                  ;U
        JP (HL)                 ;TEXTURERINC;DEC

; TRIQ: end of the triangle (or skipped): restore SP, return to the caller of TRI.
TRIQ
TRIQSP=$+1
        LD SP,0
        RET

