; =====================================================================================================================
; instpp3 - the one-time "installers" of TEX28.asm's card half (The Link, Alone Coder, 2009): table builders and
; code generators, run once from GSTEXGO on the NeoGS Z80; the area may be overwritten afterwards (TEX28: "можно
; затирать"). Included by TEX28.asm only (hedge12.asm includes its sibling instlpp.asm). No object of its own.
;   MKBALLTEX   lays the 256x256 ball texture (4 pages, blugr.C/.0/.1/.2) out as 8 overlapping 16K pages
;   MKBALLER    builds the full lens u,v table from the uv256d quarter and generates the ball code (2 pages)
;   MKTRANSCH   generates the transposer (chunk map -> 16-colour screen page) and the colour table TCH256
;   GENDIV      division table (page gspgdiv) for the texture mapper's slopes and steps
;   GENPERSP    perspective table (page gspgpersp)
;   GENJTNROT   rotation tables and routines (page gspgsin)
;   GENTEX      a test texture from a font - never called in TEX28 (dead code)
; With mkballplasma = 0 (TEX28) the plasma variant of MKBALLTEX and its helpers (MKBALLTEXCOLORPG / NOISEPG,
; WASTCH256) are not used: the ball shows the blugr texture with the palgirl colours.
; Card page numbers are rotated 16K indices (see TEX28.asm): RLCA : INC A : RRCA = next page. ALASM's
; gspgballtex1*257<1+#n0n>1 (cyclic 16-bit shifts) = page gspgballtex1 + n; the converter spells it out in full.
; =====================================================================================================================
; ALASM MAIN "*" (the project's main source)

; ---------------------------------------------------------------------------------------------------------------------
; MKBALLTEX: the 4 source pages gspgballtex1+0..3 (halves s0..s7 of 8K = 32 rows of 256) become 8 pages
; gspgballtex1+0..7, page k = [s(k-1), s(k)] (k-1 wraps to s7): 16K windows each 32 rows further. The ball code
; can then run 16 rows past either end of a 32-row band without a page check. Copied backwards in 8K pieces
; (MPAGEX = from at #C000, MPAG = to at #8000, LDDR); the source pages are overwritten only after they were read;
; for the last piece "from" wraps to page +7, which already holds a copy of the texture's end.
; ---------------------------------------------------------------------------------------------------------------------
MKBALLTEX

; Plasma variant (mkballplasma = 1, off here): noise in 4 pages, 10 smoothing passes, colours #40+0..7.
        IF mkballplasma
        LD A,gspgballtex1
        DUP 4
        CALL MKBALLTEXNOISEPG
        EDUP
        LD B,10                 ;проходов (en: passes)
GSPLASM
        PUSH BC
; en: use the last plasma page as the previous one
;используем последнюю страницу плазмы в качестве предыдущей
; ALASM: LD A,gspgballtex1*257<1+#303>1 = page gspgballtex1+3
        LD A,+((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#303&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#303&#FFFF)<<(16-1))&#FFFF)
        OUT (MPAG),A
        LD A,gspgballtex1
        LD B,4                  ;страницы (en: pages)
GSPLASM0PG
        OUT (MPAGEX),A          ;следующая после (MPAG) (en: the next one after (MPAG))
        PUSH AF
        LD HL,#8000
GSPLASM0
        LD A,(HL)
        PUSH HL
        INC L
        ADC A,(HL)              ;+noise
        RRA
        INC H
        ADC A,(HL)              ;+noise
        RRA
        POP HL
        LD (HL),A
        INC HL
        BIT 6,H
        JR Z,GSPLASM0
        POP AF
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        DJNZ GSPLASM0PG
        POP BC
        DJNZ GSPLASM
        LD A,gspgballtex1
        DUP 4
        CALL MKBALLTEXCOLORPG
        EDUP
        ENDIF

; ALASM: LD LX,gspgballtex1*257<1+#303>1 (from = page +3), LD HX,gspgballtex1*257<1+#707>1 (to = page +7)
        LD IXL,low ((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#303&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#303&#FFFF)<<(16-1))&#FFFF) ;from
        LD IXH,low ((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#707&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#707&#FFFF)<<(16-1))&#FFFF) ;to
        LD HL,#FFFF             ;from
        LD DE,#BFFF             ;to
        LD B,8                  ;страниц to (en: pages to)
MKBALLTEX8PG
        PUSH BC
        CALL MKBALLTEX8PGCOPY2000
        BIT 6,H
        JR NZ,MKBALLTEX8PGnH
        LD H,#FF
        LD A,IXL                ;from pg
        CP gspgballtex1
        JR NZ,$+4
; ALASM: LD A,gspgballtex1*257<1+#808>1 = page +8, decremented below to +7
        LD A,low ((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#808&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#808&#FFFF)<<(16-1))&#FFFF) ;в +7 копия конца текстуры (en: page +7 holds a copy of the texture's end)
        RLCA
        DEC A
        RRCA
        LD IXL,A
MKBALLTEX8PGnH
        PUSH HL
        CALL MKBALLTEX8PGCOPY2000
        POP HL
        BIT 6,D
        JR Z,MKBALLTEX8PGnD
        LD D,#BF
        LD A,IXH                ;to pg
        RLCA
        DEC A
        RRCA
        LD IXH,A
MKBALLTEX8PGnD
        POP BC
        DJNZ MKBALLTEX8PG
        RET

; copy 8K backwards: MPAG = to (IXH) at DE, MPAGEX = from (IXL) at HL
MKBALLTEX8PGCOPY2000
        LD A,IXH                ;to pg
        OUT (MPAG),A
        LD A,IXL                ;from pg
        OUT (MPAGEX),A
        LD BC,#2000
        LDDR
        RET

; Plasma only: page A at #C000 -> colour #40 + top 3 bits; returns the next page in A
MKBALLTEXCOLORPG
        PUSH AF
        OUT (MPAGEX),A
        LD HL,#C000
GSPLASM1
        LD A,(HL)
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,#40
        LD (HL),A
        INC HL
        BIT 6,H
        JR NZ,GSPLASM1
        POP AF
        RLCA
        INC A
        RRCA
        RET

; Plasma only: fill page A with noise (R register, the page itself, card memory from #0000); next page in A
MKBALLTEXNOISEPG
        PUSH AF
        OUT (MPAGEX),A
        LD HL,#0000
        LD DE,#C000
        LD BC,#4000
GSPLASMPRE0
        LD A,R
        LD C,A
        LD A,(DE)
        RLCA
        XOR C
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
        JR NZ,GSPLASMPRE0
        POP AF
        RLCA
        INC A
        RRCA
        RET

; MKBALLER: (1) mirror the quarter-lens table (uv256d at #4000, 64 rows x 32 u,v pairs) into the full 128 x 64 table
; GSTBALL in page gspgtball: the second half of each row with -u, the second half of the rows with -v; (2) turn it
; into differences between neighbours (du,dv); (3) generate the ball code at #8000 of pages gspgballer1 (6) and the
; next one (#86): per chunk-map line LD SP,line end (line B, columns #3F..#00; lines 64..127 in columns #BF..#80),
; per chunk LD D,(HL) / LD E,(HL), PUSH DE every two, and |du| x INC L / DEC L, |dv| x INC H / DEC H between them;
; between lines L += du, H += dv with the page check BALLCODESAMPLE. Run with HL = start in the texture, A' = page.
; The opcodes are written as characters: ',' = #2C INC L, '-' = #2D DEC L, '$' = #24 INC H, '%' = #25 DEC H.
;------------------------------
MKBALLER
; en: texture (256x256) in 8 pages; on H a margin of 16 to both sides: CP #F0:...SUB #20
; en: along the scanline (in memory L, on the screen Y) we move mainly along U; the lookup routine takes 2 pages
; en: HL = offset in the texture; A' = GSPGBALLTEX1..n (which is also mapped); line start: LD SP,
; en: line: LD D/E,(HL) [PUSH DE] [INC/DEC L][ADD A,?:LD L,A] - except the last pixel of the scanline; [INC/DEC H] -
; en: except the last pixel of the scanline; ~3.5 bytes per byte (2 pages); transition between lines: (below)
;текстура (256x256) в 8 страницах
;на H запас 16 в обе стороны: CP #F0:...SUB #20
;по сканлайну (в памяти - L, на экране - Y) движемся в осн. по U
;процедура лукапа занимает 2 страницы
;HL=смещение текстуры
;A'=GSPGBALLTEX1..n (она же включена)
;начало линии:
;LD SP,
;линия:
;LD D/E,(HL)
;[PUSH DE]
;[INC/DEC L][ADD A,?:LD L,A] - кроме последнего пикс. сканлайна
;[INC/DEC H] - кроме последнего пикс. сканлайна
;~3.5 байта на байт (2 страницы)
;переход между линиями:
;LD A,L
;ADD A,?
;LD L,A
;LD A,H
;ADD A,?
;CP #F8
;JC $+11
; SUB #30
; EXA
; RLCA
; INC A
; RRCA
; OUT (mpagex),A
; EXA
;LD H,A
        LD A,gspgtball
        OUT (MPAGEX),A
        LD HL,GSTBALL
        LD DE,GSTQUARTERBALL
        LD C,64                 ;половина высоты (на экране - X) (en: half the height (on the screen X))
MKBA0   LD B,32                 ;половина ширины (на экране - Y) (en: half the width (on the screen Y))
MKBA1   LD A,(DE)
        LD (HL),A               ;u
        PUSH AF
        INC DE
        INC HL
        LD A,(DE)
        LD (HL),A               ;v
        PUSH AF
        INC DE
        INC HL
        DJNZ MKBA1
        LD B,32                 ;вторая половина ширины (en: the second half of the width)
MKBA2   POP AF                  ;v
        EX AF,AF'
        POP AF                  ;u
        NEG
        LD (HL),A               ;-u
        INC HL
        EX AF,AF'
        LD (HL),A               ;v
        INC HL
        DJNZ MKBA2
        DEC C
        JR NZ,MKBA0
; mirror the 64 rows into the other 64 with -v
        LD HL,GSTBALL           ;начало таблицы (en: start of the table)
        LD DE,2*64*127+GSTBALL  ;начало нижней строки таблицы (en: start of the bottom row of the table)
        LD C,64                 ;вторая половина высоты (на экране - X) (en: the second half of the height (on the screen X))
MKBA3   LD B,64                 ;ширина (на экране - Y) (en: width (on the screen Y))
MKBA4   LD A,(HL)
        LD (DE),A               ;u
        INC HL
        INC DE
        XOR A
        SUB (HL)
        LD (DE),A               ;-v
        INC HL
        INC DE
        DJNZ MKBA4
        DEC D
        DEC C
        JR NZ,MKBA3

; du,dv to the next pair over the whole 16K (the step out of a line's last pair is the line change; the very last
; one reads card #0000 and is only used by the final line change, after all drawing)
        LD HL,GSTBALL
        LD DE,GSTBALL+2
MKBAduv LD A,(DE)
        SUB (HL)
        LD (HL),A               ;du
        INC DE
        INC HL
        LD A,(DE)
        SUB (HL)
        LD (HL),A               ;dv
        INC DE
        INC HL
        LD A,H
        OR A
        JR NZ,MKBAduv

; generate the code
        LD A,gspgballer1
        OUT (MPAG),A
        LD IXL,A
        LD HL,#8000
        LD DE,GSTBALL
        LD BC,#0040
        LD IXH,128              ;высота (на экране - X) (en: height (on the screen X))
MKBAL0
;начало линии:
        LD (HL),#31             ;ld sp,
        INC HL
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        PUSH BC
        LD B,64                 ;ширина (на экране - Y) (en: width (on the screen Y))
MKBAL1
        BIT 0,B
        LD (HL),#56             ;ld d,(hl)
        JR Z,$+7
        LD (HL),#5E             ;ld e,(hl)
        INC HL
        LD (HL),#D5             ;push de
        INC HL
        LD A,B
        DEC A
        JR Z,MKBAL1Q            ;не надо инкрементов в посл. пикс. линии (en: no increments in the last pixel of the line)
        LD A,(DE)               ;du
        INC DE
        OR A
        JR Z,MKBALlQ
        JP M,MKBALlM
        LD (HL),','             ;inc l
        INC HL
        DEC A
        JR NZ,$-4
        JR MKBALlQ
MKBALlM LD (HL),'-'             ;dec l
        INC HL
        INC A
        JR NZ,$-4
MKBALlQ LD A,(DE)               ;dv
        INC DE
        OR A
        JR Z,MKBALhQ
        JP M,MKBALhM
        LD (HL),'$'             ;inc h
        INC HL
        DEC A
        JR NZ,$-4
        JR MKBALhQ
MKBALhM LD (HL),'%'             ;dec h
        INC HL
        INC A
        JR NZ,$-4
MKBALhQ DJNZ MKBAL1
MKBAL1Q
        LD (HL),#7D             ;ld a,l
        INC HL
        LD (HL),#C6             ;add a,N
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
        LD (HL),#6F             ;ld l,a
        INC HL
        LD (HL),#7C             ;ld a,h
        INC HL
        LD (HL),#C6             ;add a,N
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A
        INC HL
        PUSH DE
        EX DE,HL
        LD HL,BALLCODESAMPLE
        LD BC,BALLCODESAMPLELEN
        LDIR
        EX DE,HL
        POP DE
        POP BC
        INC B
        BIT 6,B
        JR Z,$+5
        LD BC,#00C0
; Near the end of the code page (#BFxx): emit LD A,next : OUT (MPAG),A into this page, switch the generator to the
; next page itself, and emit JP #8000 at the same address there - at run time the OUT pages in the next code page
; and the CPU fetches that JP from it.
        LD A,H
        CP #BF
        JR NZ,MKBALnPG
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
MKBALnPG
        DEC IXH
        JP NZ,MKBAL0
MKBALQ
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)
        RET

; BALLCODESAMPLE: copied after every line: H (in A) past #EF -> H - #20 and the next texture page (rotated INC),
; page +8 wraps to gspgballtex1; then LD H,A.
BALLCODESAMPLE
        CP #F0
        JR C,$+11+6
        SUB #20
        EX AF,AF'
        RLCA
        INC A
        RRCA
; ALASM: CP gspgballtex1*257<1+#808>1 = page +8
        CP low ((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#808&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#808&#FFFF)<<(16-1))&#FFFF) ; ALASM keeps the low byte
        JR NZ,$+4
        LD A,gspgballtex1
        OUT (MPAGEX),A
        EX AF,AF'
        LD H,A
BALLCODESAMPLELEN=$-BALLCODESAMPLE

; MKTRANSCH: generate the transposer at transch (#E000, page gspgtransch). It reads the chunk map (64 rows of 64
; chunks, at gschmap256 and +128) with POP HL (SP = source) and writes the screen page at gschscr (#8000) down the
; columns (INC D), each chunk as two 16-colour bytes from TCH256 (rows #FE and #FF: its two pixel rows), in the
; order OUT16C on the ZX reads the page. E' starts at 31 and steps by the TTRANSCHX offsets (4 layers x upper/lower
; half; their sum is -1). Ends with JP (IX). Then TCH256 is built from palgirl.
;---------------------------
MKTRANSCH
        LD A,gspgtransch
        GSSETPGA
        LD HL,transch
; en: #114E/2 bytes; we transpose moving horizontally over the source, vertically over the destination
;#114E/2 байт
;транспонируем, двигаясь по источнику горизонтально,
;по приемнику вертикально
;E'=0
;B'='TCH256
;HL=gschmap256
;B=32
;LOOP ;DUP 8
;[LD SP,HL]
;[EXX]
;LD D,'gschscr
;DUP 32
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
        LD C,8
MKTRANSCH0
        PUSH DE
        BIT 0,C
        JR NZ,$+2+3+3
        LD (HL),#F9             ;ld sp,hl
        INC HL
        LD (HL),#D9             ;exx
        INC HL
        LD (HL),#16             ;ld d,
        INC HL
        LD (HL),high gschscr
        INC HL
        LD B,(64&#FFFF)/(4&#FFFF) ;высота 64*2 (en: height 64*2)
MKTRANSCH1
        PUSH BC
        EX DE,HL
        LD HL,TRANSCHCODE
        LD BC,TRANSCHCODELEN
        LDIR
        EX DE,HL
        POP BC
        DJNZ MKTRANSCH1
        DEC HL                  ;убираем inc d (en: remove the last inc d)
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
        BIT 0,C
        JR Z,$+2+3+3
        LD (HL),#D9             ;exx
        INC HL
        LD (HL),'$'             ;inc h
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
        IF mkballplasma
        LD HL,WASTCH256
        LD DE,TCH256
        LD BC,512
        LDIR
        ELSE
; TCH256: for each chunk value 4 colours from palgirl (2x2 pixels) -> 2 bytes in the 16-colour format, one in row
; #FE, one in #FF; in each byte the first colour -> bits 5..3 + bit 7, the second -> bits 2..0 + bit 6 (COLSTOBYTE)
        LD HL,palgirl
        LD DE,TCH256
MKCH2X256
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
        ENDIF
        RET
        IF mkballplasma
; plasma variant: identity table and a bit shuffle; ALASM: DB _<3&56+(_>3&7)+(_<1&128)+(_>1&64) (cyclic shifts)
WASTCH256
_=0
        DUP 256
        DB _
_=_+1
        EDUP
        DUP 256
        DB ((((_&#FFFF)<<3|(_&#FFFF)>>>(16-3))&#FFFF)&56)+((((_&#FFFF)>>>3|(_&#FFFF)<<(16-3))&#FFFF)&7)+((((_&#FFFF)<<1|(_&#FFFF)>>>(16-1))&#FFFF)&128)+((((_&#FFFF)>>>1|(_&#FFFF)<<(16-1))&#FFFF)&64)
_=_+1
        EDUP
        ENDIF

; TTRANSCHX: E' steps between the column blocks; "слой" = layer, "верх,низ" = top, bottom
TTRANSCHX
        DB #20                  ;#00,#20 слой0 верх,низ (en: layer 0 top, bottom)
        DB #60,#20              ;#80,#A0 слой2 верх,низ (en: layer 2 top, bottom)
        DB #A0,#20              ;#40,#60 слой1 верх,низ (en: layer 1 top, bottom)
        DB #60,#20              ;#C0,#E0 слой3 верх,низ (en: layer 3 top, bottom)
        DB #1F

; TRANSCHCODE: 16 copies per column block. One POP = 2 chunks (L, H): 4 bytes down the column; INC B / DEC B
; selects the TCH256 row, so the second chunk comes out lower row first - the ZX's mirrorchunks puts it back.
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

; GENTEX: a test texture (digits from the font at #3D88 = "1" in the ZX ROM font) - not called in TEX28.
;---------------------------
GENTEX
        LD A,gspgtex
        GSSETPGA
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),7
        LDIR
        LD IX,#0F38
        LD DE,#3D88             ;"1"
        LD HL,#C000
        CALL GENTEXPP
        LD IXH,#17
        LD HL,#C040
        CALL GENTEXPP
        LD IXH,#1F
        LD HL,#C080
        CALL GENTEXPP
        LD IXH,#27
        LD HL,#C0C0
GENTEXPP
GENTEX0
        LD B,8
GENTEX0DUP
        PUSH BC
        PUSH HL
        LD A,(DE)
        LD B,8
GENTEX1 RLA
        LD C,IXH
        JR NC,$+4
        LD C,IXL
        DUP 8
        LD (HL),C
        INC L
        EDUP
        DJNZ GENTEX1
        POP HL
        POP BC
        INC H
        DJNZ GENTEX0DUP
        INC DE
        JR NZ,GENTEX0
        RET
; GENDIV: page gspgdiv, #C000: for each row H = -64..-1 (#C0..#FF), 128 words at L = #80..#FE, #00..#7E (L/2 =
; -64..63): (L/2)/-H, 8.8. Used by texpp4 with the operands as the address (row = -height, column = 2*span).
;--------------------------
GENDIV
        LD A,gspgdiv
        GSSETPGA
; en: we compute 1/H (1.15) and spread it over the row
;+-6.8=+-6.0/6.0=(L/2)/-H
;считаем 1/H (1.15) и размножаем
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
;начинаем с -64: AIX=-DE0/2 (en: start with -64)
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
; en: we add AIX += 0DE.5 + 0DE.5 (two steps per entry: L/2 per word)
;складываем AIX+=0DE.5+0DE.5
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
; GENPERSP: page gspgpersp, #C000: for each row Z = -64..-1, 256 bytes, one per signed coordinate L: the integer
; part of about 16*L/-Z (the author: numbers 32 times those of pgdiv) + 32, so centred at 32.
; PERSPCORR reads it with D = Z, E = X or Y.
GENPERSP
        LD A,gspgpersp
        GSSETPGA
;+-6.8=+-6.0/6.0=(L/2)/-H
;считаем 1/H (1.15) и размножаем (en: we compute 1/H (1.15) and spread it over the row)
; en: the numbers must be 32 times larger than in pgdiv
;надо числа в 32 раза больше чем в pgdiv
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
;начинаем с -64: AIX=-DE0/2 (en: start with -64)
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
        LD B,0                  ;L=128 для +64/H (не -64/H!) (en: comment copied from GENDIV; here B=0: 256 entries)
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
; GENJTNROT: page gspgsin. MULTAB (#C400 on): for each distinct value s < #80 of SIN4 a 256-byte table of x*s/128 (x signed,
; 0..127 then -128..-1), rounded. SINOFS (#C100) = per angle the table page of |sin|; COSOFS (#C200) = the same shifted
; by 64; SIGNOFS (#C000) = per angle the offset of the PROCROT routine (from the run-length list SIGNOFS_INST);
; PROCROT (#C300) = the routines of JTNROTPROC_INST.
;--------------------------------
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

; SIN4: 128*SIN of angles 1..61 (256 per turn); at 61 it rounds to #80 = 1.0, which ends the list (RLA -> carry)
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

; SIGNOFS_INST: (count, routine) for the 256 angles, per quadrant: 1 on the axis, 3 where cos rounds to 1 (no cos
; multiply), 57 general, 3 where sin rounds to 1. In the routine notes "P" = the product from the table.
SIGNOFS_INST

        DB 1,U0,3,UB0,57,CH_I,3,UM90
        DB 1,U90,3,UB90,57,CH_II,3,UM180
        DB 1,U180,3,UB180,57,CH_III,3,UM270
        DB 1,U270,3,UB270,57,CH_IV,3,UM360



; X'=X*COS(F)-Y*SIN(F)
; Y'=X*SIN(F)+Y*COS(F)

;C=X    DE=COS
;B=Y    HL=SIN
;OUTP A=X,B=Y
; JTNROTPROC_INST: the rotation routines, copied to PROCROT; each label is its offset (CALL PROCROT with the low byte
; patched). In: C = X, B = Y, H = sin table page, D = cos table page (L / E = the coordinate as table index).
; Out: A = X', B = Y'.
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

