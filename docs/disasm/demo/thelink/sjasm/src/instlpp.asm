; =====================================================================================================================
; instlpp.asm - The Link (Alone Coder, 2009): the card-side installers of the hedgehog
;
; Included by hedge12.asm (and by its sibling hedgepre.asm) inside the card code HEDGEGS; uses their constants
; (gspg*, transch, TCH256, gschscr, parthgt, SIGNOFS ... MULTAB, pal256). GSHEDGEGO calls each routine once at set-up
; on the NeoGS; each maps its card page at #C000 (GSSETPGA) and fills it with a table or generated code. After that
; this memory is no longer needed (hedge12: "NGS инсталлеры - можно затирать", the installers may be overwritten).
;   MKTRANSCH  `transch` (#E000 of gspgtransch): generated code that converts the chunk map into the 16-colour screen,
;              and the TCH256 tables (#FE00, #FF00) from pal256 (palbrick)
;   GENDIV     gspgdiv: the division table of the texture mapper (texpp4.asm, "from pgdiv")
;   GENPERSP   gspgpersp: the perspective table of PERSPCORR
;   GENJTNROT  gspgsin: the angle tables SIGNOFS/SINOFS/COSOFS, the multiplication tables MULTAB and the rotation
;              routines PROCROT used by JTNROTATE
; The first line is the converter's: ALASM's `MAIN "*",#C6` (the include's file header) as a comment. COLSTOBYTE stays
; a macro (no glued parameters); `'$'` below is the byte #24, the opcode of INC H.
; =====================================================================================================================
; ALASM MAIN "*" (the project's main source)

; ---------------------------------------------------------------------------------------------------------------------
; en: MKTRANSCH generates the converter at transch. It transposes: two neighbouring chunks of a map row (POP HL) go
; en: one under the other on the screen, as the 3D engine's X grows upward. A chunk value v gives two screen bytes in
; en: two successive lines: TCH256[v] and TCH256[v+#100] (B = #FE/#FF; the second chunk of the pair uses them in the
; en: other order). A block: LD D,high gschscr, then TRANSCHCODE (2 chunks, 4 lines) x parthgt/4 = 14 chunks, 28
; en: lines (a quarter's height), then E += TTRANSCHX[block]. Card line D = one row of the ZX's blit, byte E = the
; en: position in that row's 256 DMA reads. 16 blocks per pass, 4 per map row: LD SP,HL / EXX before the first,
; en: EXX / INC H after the last. TTRANSCHX sums to -1, so pass p fills byte 15-p of every 16-byte segment.
; en: B = 16 passes (DEC B : JP NZ) = 64 rows; the end is JP (IX). About 2,000 bytes of code.
; en: Then MKCH2X256 builds TCH256.
; ---------------------------------------------------------------------------------------------------------------------
MKTRANSCH
        LD A,gspgtransch
        GSSETPGA
        LD HL,transch
;#114E/2 байт (en: #114E/2 bytes)
; en: transpose, moving horizontally over the source,
; en: vertically over the destination
;транспонируем, двигаясь по источнику горизонтально,
;по приемнику вертикально
;E'=0
;B'='TCH256
;HL=gschmap256
;B=16
;LOOP ;DUP 8
;[LD SP,HL]
;[EXX]
;LD D,'gschscr
;DUP 16
;[POP HL]
;LD C,L/H
;LD A,(BC)
;LD (DE),A
;INC D
;INC/DEC B
;LD A,(BC)
;LD (DE),A
;INC D
;EDUP
;LD A,E
;ADD A,#20/#60/#20/#A0/#20/60/#20/#21
;LD E,A
;[EXX]
;[INC H]
;EDUP
;DEC B
;JP NZ,LOOP
        LD (mktranschloop),HL
        LD DE,TTRANSCHX
        LD C,16
MKTRANSCH0
        PUSH DE
        LD A,C
        AND 3
        JR NZ,$+2+3+3
        LD (HL),#F9             ;ld sp,hl
        INC HL
        LD (HL),#D9             ;exx
        INC HL
        LD (HL),#16             ;ld d,
        INC HL
        LD (HL),high gschscr
        INC HL
        LD B,(parthgt&#FFFF)/(4&#FFFF) ;32/4 ;высота 64*2
MKTRANSCH1
        PUSH BC
        EX DE,HL
        LD HL,TRANSCHCODE
        LD BC,TRANSCHCODELEN
        LDIR
        EX DE,HL
        POP BC
        DJNZ MKTRANSCH1
        DEC HL                  ;убираем inc d (en: drop the last inc d)
        POP DE
        LD (HL),#7B             ;ld a,e
        INC HL
        LD (HL),#C6             ;add a,
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
        LD (HL),#5F             ;ld e,a
        INC HL
        LD A,C
        DEC A
        AND 3
        JR NZ,$+2+3+3
        LD (HL),#D9             ;exx
        INC HL
        LD (HL),'$'             ;inc h (en: '$' = #24, the INC H opcode)
        INC HL
        DEC C
        JR NZ,MKTRANSCH0
        LD (HL),#05             ;dec b
        INC HL
        LD (HL),#C2             ;jp nz
        INC HL
mktranschloop=$+1
        LD BC,0
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)

        LD HL,pal256
        LD DE,TCH256
; en: MKCH2X256: for each chunk value 0..255 two bytes, at TCH256+v and TCH256+#100+v, each made of two palbrick colours
MKCH2X256
; en: COLSTOBYTE: two colours (0..15, bit 3 = bright) from (HL), (HL+1) -> one Pentagon 16-colour byte (two pixels):
; en: the first colour to bits 7,5,4,3, the second to bits 6,2,1,0
        MACRO COLSTOBYTE
        LD A,(HL)
        INC HL
        ADD A,A
        ADD A,A
        ADD A,A
        ADD A,A
        SRA A
        AND #80+#38
        LD C,A
        LD A,(HL)
        ADD A,A
        ADD A,A
        ADD A,A
        OR (HL)
        INC HL
        AND #47
        OR C
        ENDM
        COLSTOBYTE
        LD (DE),A
        INC D
        COLSTOBYTE
        LD (DE),A
        DEC D
        INC E
        JR NZ,MKCH2X256
        RET

; en: TTRANSCHX: the step of E (destination column) after each block of a pass; the comments name the screen part
TTRANSCHX
        DB #10,#10,#10          ;#00,#10,#20,#30 слой0 верх,низ (en: layer 0 top, bottom)
        DB #50,#10,#10,#10      ;#80,#90,#A0,#B0 слой2 верх,низ (en: layer 2 top, bottom)
        DB #90,#10,#10,#10      ;#40,#50,#60,#70 слой1 верх,низ (en: layer 1 top, bottom)
        DB #50,#10,#10,#10      ;#C0,#D0,#E0,#F0 слой3 верх,низ (en: layer 3 top, bottom)
        DB #0F

; en: TRANSCHCODE: the template copied into transch; two chunks (L, H) -> 4 bytes in 4 successive screen lines
TRANSCHCODE
        POP HL
        LD C,L
        LD A,(BC)
        LD (DE),A
        INC D
        INC B
        LD A,(BC)
        LD (DE),A
        INC D
        LD C,H
        LD A,(BC)
        LD (DE),A
        INC D
        DEC B
        LD A,(BC)
        LD (DE),A
        INC D
TRANSCHCODELEN=$-TRANSCHCODE

;--------------------------
; ---------------------------------------------------------------------------------------------------------------------
; en: GENDIV: the gspgdiv page at #C000. Row H = #C0..#FF (signed -64..-1, so the divisor -H = 64..1): the reciprocal
; en: comes from a 16-step restoring division (two DUP 8 blocks; the quotient bits come out inverted, CPL fixes them),
; en: then the row's 128 words (L = #80 first, then up to #7F) are made by repeated addition: (L/2)/-H in the
; en: author's notation, low byte then high byte.
; ---------------------------------------------------------------------------------------------------------------------
GENDIV
        LD A,gspgdiv
        GSSETPGA
;+-6.8=+-6.0/6.0=(L/2)/-H (en: fixed-point formats: integer.fraction bits)
;считаем 1/H (1.15) и размножаем (en: compute 1/H (1.15) and multiply it out)
        LD HL,#C080             ;-64..+63
GENDIV0
        PUSH HL
        XOR A
        LD C,A
        SUB H
        LD B,A
        LD HL,#0100             ;даст 1/1=#8000, 1/2=#4000... (en: gives 1/1=#8000, 1/2=#4000...)
        XOR A
        DUP 8
        SBC HL,BC
        JR NC,$+3
        ADD HL,BC
        RLA
        ADD HL,HL
        EDUP
        CPL
        LD D,A
        XOR A
        DUP 8
        SBC HL,BC
        JR NC,$+3
        ADD HL,BC
        RLA
        ADD HL,HL
        EDUP
        CPL
        LD E,A                  ;DE=целая часть частного (DE.5+-0.5) (en: DE = integer part of the quotient)
;начинаем с -64: AIX=-DE0/2 (en: start from -64: AIX = -DE0/2)
        XOR A
        SUB E
        LD B,A
        LD C,#80                ;правильное округление (en: correct rounding)
        SBC A,A
        SUB D                   ;ABC=-DE.5
        SRA A
        RR B
        RR C
        LD IXH,B
        LD IXL,C

        POP HL
        LD B,128                ;L=128 для +64/H (не -64/H!) (en: L=128 for +64/H (not -64/H!))
GENDIV2
        LD C,IXH
        LD (HL),C               ;low
        INC L
        LD (HL),A               ;high
        INC L
;складываем AIX+=0DE.5+0DE.5 (en: add AIX += 0DE.5 + 0DE.5)
        INC IX                  ;.5+.5
        ADD IX,DE
        ADC A,0
        ADD IX,DE
        ADC A,0
        DJNZ GENDIV2
        INC H
        JP NZ,GENDIV0
        RET
;-----------------------
; ---------------------------------------------------------------------------------------------------------------------
; en: GENPERSP: the gspgpersp page at #C000, the same scheme with 32 times bigger numbers and the high byte only, 256
; en: entries per row, +32 added: row = Z (negative), column = coordinate -> projected coordinate (PERSPCORR).
; ---------------------------------------------------------------------------------------------------------------------
GENPERSP
        LD A,gspgpersp
        GSSETPGA
;+-6.8=+-6.0/6.0=(L/2)/-H
;считаем 1/H (1.15) и размножаем
;надо числа в 32 раза больше чем в pgdiv (en: numbers 32 times bigger than in pgdiv are needed)
        LD HL,#C080             ;-64..+63
GENPERSP0
        PUSH HL
        XOR A
        LD C,A
        SUB H
        LD B,A
        LD HL,#1000             ;даст 1/16=#8000, 1/32=#4000... (en: gives 1/16=#8000, 1/32=#4000...)
        XOR A
        DUP 8
        SBC HL,BC
        JR NC,$+3
        ADD HL,BC
        RLA
        ADD HL,HL
        EDUP
        CPL
        LD D,A
        XOR A
        DUP 8
        SBC HL,BC
        JR NC,$+3
        ADD HL,BC
        RLA
        ADD HL,HL
        EDUP
        CPL
        LD E,A                  ;DE=целая часть частного (DE.5+-0.5) (en: DE = integer part of the quotient)
;начинаем с -64: AIX=-DE0/2
        XOR A
        SUB E
        LD B,A
        LD C,#80                ;правильное округление (en: correct rounding)
        SBC A,A
        SUB D                   ;ABC=-DE.5
        LD IXH,B
        LD IXL,C
        ADD A,32                ;для персп.корректора так удобнее (en: more convenient for the perspective corrector)

        POP HL
        LD B,0                  ;L=128 для +64/H (не -64/H!)
GENPERSP2
        LD (HL),A               ;high
        INC L
;складываем AIX+=0DE.5+0DE.5
        INC IX                  ;.5+.5
        ADD IX,DE
        ADC A,0
        ADD IX,DE
        ADC A,0
        DJNZ GENPERSP2
        INC H
        JP NZ,GENPERSP0
        RET
;--------------------------------
; ---------------------------------------------------------------------------------------------------------------------
; en: GENJTNROT: the rotation tables in the gspgsin page at #C000:
; en:   MULTAB (#C400 up): one 256-byte page per distinct value s of SIN4: entry x (signed) = x*s/128, rounded;
; en:   SINOFS (#C100): per angle the page of its multiplication table: angles 1..60 directly, 68..127 mirrored,
; en:     128..255 a copy of 0..127 (the tables hold |sin|, the sign is in the routine); COSOFS (#C200) = SINOFS
; en:     shifted by 64 (a quarter turn);
; en:   SIGNOFS (#C000): per angle the low byte of the routine for its quadrant, from SIGNOFS_INST (count, value
; en:     pairs): a general one (CH_I..CH_IV) or a short one near 0/90/180/270 degrees, where cos or sin is +-1;
; en:   PROCROT (#C300): the routines themselves (JTNROTPROC_INST, copied).
; ---------------------------------------------------------------------------------------------------------------------
GENJTNROT
        LD A,gspgsin
        GSSETPGA
        XOR A
        LD (INSTMULPREV),A

        LD IX,MULTAB-#0100
        LD HL,SIN4
        LD DE,SINOFS
INST_MUL
        LD A,(HL)
        INC HL
INSTMULPREV=$+1
        CP 0
        JR Z,INSTMULSKIP
        LD (INSTMULPREV),A
        INC IXH
        EXX

        ADD A,A
        LD E,A
        XOR A
        LD D,A
;LD      L,A
;LD      H,A
        LD HL,#0080             ;правильное округление (en: correct rounding)
        LD B,128                ;0..127
INSTMUL1
        LD (IX),H
        INC IXL
        ADD HL,DE
        DJNZ INSTMUL1

        XOR A
        SUB H
        LD (IX),A
        INC IXL

        XOR A
        SUB L
        LD L,A                  ;L=0-L
        SBC A,H                 ;0-L-H-C
        SUB L                   ;-(0-L)
        LD H,A                  ;24
        INC H                   ;правильное округление (en: correct rounding)

        LD B,127                ;-128..-1
INSTMUL2
        ADD HL,DE
        LD (IX),H
        INC IXL
        DJNZ INSTMUL2
        EXX
INSTMULSKIP
        LD A,IXH
        INC E
        LD (DE),A

        LD A,(HL)
        RLA
        JR NC,INST_MUL

        LD HL,SINOFS+#44
        LD B,#3C
        LD A,(DE)
        LD (HL),A
        DEC E
        INC L
        DJNZ $-4
        EX DE,HL
        LD C,#80
        LDIR

        LD L,#40
        LD A,(HL)
        LD (DE),A
        INC L
        INC E
        JR NZ,$-4

        LD DE,SIGNOFS
        LD HL,SIGNOFS_INST
I_OFS
        LD B,(HL)
        INC HL
        LD A,(HL)
        INC HL
        LD (DE),A
        INC E
        DJNZ $-2
        JR NZ,I_OFS

        LD HL,JTNROTPROC_INST
        LD DE,PROCROT
        LD BC,JTNROTPROC_LEN
        LDIR
        RET

; en: SIN4: sin(i)*128 for the angles i = 1, 2 ... (256 per turn); the loop stops before the first byte with bit 7 set
; en: (the #80), so angles 1..60 get tables; the angles next to 0/90/180/270 use the special routines
SIN4
        DB #03,#06,#09,#0D,#10,#13,#16
;       DEFB    #00,#03,#06,#09,#0D,#10,#13,#16
        DB #19,#1C,#1F,#22,#25,#28,#2B,#2E
        DB #31,#34,#37,#3A,#3C,#3F,#42,#44
        DB #47,#4A,#4C,#4F,#51,#54,#56,#58
        DB #5B,#5D,#5F,#61,#63,#65,#67,#69
        DB #6A,#6C,#6E,#6F,#71,#72,#74,#75
        DB #76,#77,#79,#7A,#7A,#7B,#7C,#7D
        DB #7E,#7E,#7F,#7F,#7F,#80 ; #80,#80

;       DEFB    #80,#80,#80,#80,#7F,#7F,#7F,#7E

; en: SIGNOFS_INST: (count, routine) pairs for the 256 angles: per quadrant 1 exact, 3 near-exact, 57 general, 3
; en: near-exact
SIGNOFS_INST

        DB 1,U0,3,UB0,57,CH_I,3,UM90
        DB 1,U90,3,UB90,57,CH_II,3,UM180
        DB 1,U180,3,UB180,57,CH_III,3,UM270
        DB 1,U270,3,UB270,57,CH_IV,3,UM360



; en: the rotation routines (copied to PROCROT). In: C = X, B = Y, D = cos table page, H = sin table page (E/L take
; en: the coordinate as the index); out: A = X', B = Y'. CH_I..CH_IV: general, by the signs of cos and sin;
; en: U*/UB*/UM*: angles where cos or sin is +-1 and the other one is a small value P (or 0).
; X'=X*COS(F)-Y*SIN(F)
; Y'=X*SIN(F)+Y*COS(F)

;C=X    DE=COS
;B=Y    HL=SIN
;OUTP A=X,B=Y
JTNROTPROC_INST

;COS SIN
; +   +
CH_I=$-JTNROTPROC_INST
        LD L,C
        LD E,B
        LD A,(DE)
        ADD A,(HL)
        LD L,B
        LD E,C
        LD B,A
        LD A,(DE)
        SUB (HL)
        RET
; -   +
CH_II=$-JTNROTPROC_INST
        LD L,C
        LD E,B
        EX DE,HL
        LD A,(DE)
        SUB (HL)
        LD L,C
        LD E,B
        LD B,A
        LD A,(DE)
        ADD A,(HL)
        NEG
        RET
; -   -
CH_III=$-JTNROTPROC_INST
        LD L,C
        LD E,B
        IF 0
        LD A,(DE)
        ADD A,(HL)
        NEG
        LD L,B
        LD E,C
        LD B,A
        EX DE,HL
        ELSE
        XOR A
        SUB (HL)
        EX DE,HL
        SUB (HL)
        LD E,B
        LD L,C
        LD B,A
        ENDIF
        LD A,(DE)
        SUB (HL)
        RET
; +   -
CH_IV=$-JTNROTPROC_INST
        LD L,C
        LD E,B
        LD A,(DE)
        SUB (HL)
        LD L,B
        LD E,C
        LD B,A
        LD A,(DE)
        ADD A,(HL)
        RET
;COS SIN
; 1   0
U0=$-JTNROTPROC_INST
        LD A,C
        RET
; 1  -P
UM360=$-JTNROTPROC_INST
        LD L,C
        LD A,B
        SUB (HL)
        LD L,B
        LD B,A
        LD A,C
        ADD A,(HL)
        RET
; 1  +P
UB0=$-JTNROTPROC_INST
        LD L,C
        LD A,B
        ADD A,(HL)
        LD L,B
        LD B,A
        LD A,C
        SUB (HL)
        RET

;COS SIN
; 0   1
U90=$-JTNROTPROC_INST
        XOR A
        SUB B
        LD B,C
        RET
;-P   1
UB90=$-JTNROTPROC_INST
        LD E,B
        EX DE,HL
        LD A,C
        SUB (HL)
        LD B,A
        XOR A
        SUB L
        LD L,C
        SUB (HL)
        RET
;+P   1
UM90=$-JTNROTPROC_INST
        LD E,B
        LD A,(DE)
        ADD A,C
        LD L,A
        LD E,C
        LD A,(DE)
        SUB B
        LD B,L
        RET

;COS SIN
;-1   0
U180=$-JTNROTPROC_INST
        XOR A
        SUB B
        LD B,A
        XOR A
        SUB C
        RET
;-1  -P
UB180=$-JTNROTPROC_INST
        LD L,C
        XOR A
        SUB B
        SUB (HL)
        LD L,B
        LD B,A
        LD A,(HL)
        SUB C
        RET
;-1  +P
UM180=$-JTNROTPROC_INST
        LD L,C
        LD A,(HL)
        SUB B
        LD L,B
        LD B,A
        XOR A
        SUB C
        SUB (HL)
        RET

;COS SIN
; 0  -1
U270=$-JTNROTPROC_INST
        XOR A
        SUB C
        LD C,A
        LD A,B
        LD B,C
        RET
;-P  -1
UM270=$-JTNROTPROC_INST
        LD E,B
        EX DE,HL
        XOR A
        SUB C
        SUB (HL)
        LD B,A
        LD A,L
        LD L,C
        SUB (HL)
        RET

;COS SIN
;+P  -1
UB270=$-JTNROTPROC_INST
        LD E,B
        EX DE,HL
        LD A,(HL)
        SUB C
        LD B,A
        LD A,L
        LD L,C
        ADD A,(HL)
        RET
JTNROTPROC_LEN=$-JTNROTPROC_INST

