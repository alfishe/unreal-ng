; =====================================================================================================================
; TEX28 - "textured face": a textured 3D head (the girl's face, texture girl13.2) in front of a rotating,
; lens-distorted ball (texture blugr), in the Pentagon 16-colour mode.
; The Link (invdemo, Alone Coder, 2009), Pentagon 1024 + NeoGS. Converted from ALASM (docs/disasm/demo/thelink/src/
; TEX28.asm) to sjasmplus by unreal-asm; the English notes are added, the author's comments are kept as they are.
;
; WHAT IT BUILDS (ObjTab at the end; the sjasmplus SAVEBIN block writes the same two files):
;   TEXZX C  ZX half,  #C000 of #7FFD page pgeffects (#B0 here; the demo loads it into #B7 = RAM page 55).
;            Entered by the resident loop with BC = start frame, DE = end frame; copies itself to
;            TEXCODE = #7800 and runs there; generates the 13K blitter OUT16C behind itself.
;   TEXGS C  card half, #C000 of #7FFD page pgcode1 (#52 here; the demo loads it into #36 = RAM page 38, the 9th
;            card code page). The card's resident loop maps it with MPAGEX and CALLs #C000: it copies its code to
;            card #5900 (GSTEXCODE) and the uv256d table to card #4000, builds its tables and generated code, then
;            renders one picture per loop and hands it to the ZX by ZX-DMA until the ZX sends command 2.
;   The rest is the author's stand-alone test harness, assembled but not saved: GO at #7800 (the same start
;   THELINK.asm's GO does for the whole demo, cut down to this part: card reset, card program through GS
;   commands #18/#19/#13, ZX-DMA upload of 5 data pages + 1 code page, resident loop, IM 2, music), the data
;   pages (girl13.2, blugr.C/.0/.1/.2), a JR $ where the test ends, and the music thelinkm.
;
; HOW THE TWO HALVES WORK TOGETHER (one card picture per loop; README section 6):
;   card  GSLOOP: animate angles by the frames passed (timer byte from the ZX), rotate + project the head, sort the
;         faces by Z, run the generated ball code (background), draw the faces with the texture mapper (texpp4 +
;         vujump + the generated TEXTURERINC/DEC), transpose the chunk map into one of two 16K screen pages in the
;         order the ZX reads it, then: wait for command 0 -> DMA off; wait for command 1 -> DMA at that page, on,
;         answer; any other command -> RET (back to the card's resident loop, next effect).
;   ZX    BIGLOOP: HALT, timer byte -> GSDAT, command 0, flip the 16-colour screens, wait until the card took the
;         byte, end time / Space -> QUIT, command 1, wait for the answer, then OUT16C reads the 16384-byte picture
;         from the ROM area (each LD r,(HL) is answered by ZX-DMA) and PUSHes it into the hidden screen.
;
; PICTURE: a chunk map of 128 x 64 chunks (engine Y = screen right, engine X = screen up); a chunk is 2x2 pixels,
;   so 256 x 128 pixels, shown in screen lines 32..159. Colours: palgirl gives 4 colours per chunk value (the 2x2
;   pixels), TCH256 turns them into 16-colour bytes (#EFF7 bit 0 = 1: each byte is 2 pixels, ink+bit 6 and
;   paper+bit 7, banks #C000/#E000 of pages 4+5 / 6+7).
;
; INCLUDED: gsports.asm (NeoGS ports, GS command macros), texpp4.asm (triangle setup + scanline loop of the texture
;   mapper), vujump.asm (its stack tables and the VU jump table), instpp3.asm (table and code generators, run once,
;   "installers"), SAVEOBJ4.asm (the author's object saver, replaced here by SAVEBIN).
; INCBIN: girl13.2 (face textures), blugr.C/.0/.1/.2 (ball texture, 256x256 chunks), sin+ (127*SIN, 256 steps,
;   from sin+.bas), palgirl (1024 bytes: 4 colours per chunk value), uv256d (u,v offsets of a 64x32 quarter of the
;   lens, from uv256d.bas), thelinkm (the music). The BASIC tables are recomputed by sjasm/tables.py.
;
; CONVERTER'S ADDITIONS: the DEVICE line; ALASM's 16-bit cyclic shifts x<n / x>n spelled as
;   (((x&#FFFF)<<n|(x&#FFFF)>>>(16-n))&#FFFF) (originals quoted next to them); ALASM evaluates strictly left to
;   right, so a*b+c*257 style operands got explicit parentheses; macros are expanded at their calls (the original
;   bodies stay as "; (expanded at its calls)" comments, each expansion starts "; macro X expanded"); IF0/IFN ->
;   IF ...==0 / IF ...; DS n -> DS low (n) where ALASM keeps the low byte; `low x` with "ALASM keeps the low byte"
;   (added after the conversion); the SAVEBIN block at the end.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
border=0
mkballplasma=0
debug=0                         ;1=проверка отрицательной ширины (en: 1 = check for a negative scanline width; it stops with JR NC,$)

; Page plan of the stand-alone test (#7FFD values); THELINK.asm has the demo's own plan.
;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

; ZX address the ZX half runs at (TEXZX copies itself here; the #7800 block is free once the resident loop runs)
TEXCODE=#7800

; Resident area, the same addresses as in THELINK.asm: page buffer, IM 2 vector table, current #7FFD value (for the
; interrupt handler), 16-bit frame counter, resident loop, IM 2 handler, the handler's own stack.
resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

; NeoGS port names (GSCOM/GSDAT on the ZX; MPAG, MPAGEX, ZXCMD, ZXDATRD/WR, DMA_* on the card) and the GS
; command macros: SC = send command, WC = wait until the command is taken, SD = send data, WD = wait until the
; data is taken, WN = wait for data from the card, GD = get data.
        INCLUDE "gsports.asm"
; Card addresses: the card program from GO (its stack grows down from the same address) and the effect code.
GSPROG=#5830
GSSTACK=#5830
GSTEXCODE=#5900

; Card page numbers are the byte written to MPAG (#8000-#BFFF) / MPAGEX (#C000-#FFFF) in EXPAG mode: the 16K page
; index rotated right by one (bit 7 = bit 0 of the index). So "next page" is RLCA : INC A : RRCA everywhere, and
; the ZX-DMA address of page p is RLC(p)*#4000 (computed with RRCA, see GSPROGGO). gspgcode #10 = index #20;
; gspgdata #A2 = index #45 = the 6th data page of the demo (THELINK.asm: gspgdata #20 = index #40), where the demo
; puts girl13.2: this test uploads its data to the same card pages as the demo.
gspgcode=#10
gspgdata=#A2
gspgtex=gspgdata
; ALASM: gspgballtex1=gspgdata*257<1+#101>1&#FF   (< and > are 16-bit cyclic shifts)
; x*257 puts the page byte in both halves, <1 makes it the plain index in both, +#101 adds 1 page, >1 rotates
; back: the page after gspgdata (#23, blugr.C), the first of the 8 pages of the ball texture (see MKBALLTEX).
gspgballtex1=((((((gspgdata*257&#FFFF)<<1|(gspgdata*257&#FFFF)>>>(16-1))&#FFFF)+#101&#FFFF)>>>1|((((gspgdata*257&#FFFF)<<1|(gspgdata*257&#FFFF)>>>(16-1))&#FFFF)+#101&#FFFF)<<(16-1))&#FFFF)&#FF ;8 страниц (en: 8 pages)

texdec=1                        ;с заворотом (en: with wrap-around: Y runs downwards and the 128 logical rows wrap in the 64-row map)
gschmap256=#0000
gschmaphgt=64                   ;с заворотом в 2 раза больше (en: with the wrap-around twice as many)
gschscr=#8000                   ;2 страницы - 2 экрана (en: 2 pages - 2 screens)
; Card RAM below #8000 (32K page 0, RAM everywhere with NOROM): chunk map at #0000-#3FFF; TPOLYZ = Z sum of
; each face, TPOLYS = face record addresses (low bytes, high bytes at +#100); vertices as 256-byte rows X, Y, Z
; (vertex n at index #FF-n). TPOLYZ..VTX3DI overlap GSTQUARTERBALL (#4000-#4FFF): uv256d is used once by MKBALLER.
TPOLYZ=#4000
TPOLYS=TPOLYZ+#100              ;#200 (L,H)
;исходные координаты: (en: source coordinates:)
VTX3DI=#4300                    ;,#300
VTX3DIH=high VTX3DI
;координаты после поворота: (en: coordinates after rotation:)
VTX3D=#4600                     ;,#300
VTX3DH=high VTX3D
VTX3DY=VTX3D+#100
VTX3DZ=VTX3D+#200

; Card working pages (rotated numbers, see above):
gspgtball=#85                   ;временно (en: temporarily: the full ball u,v table for MKBALLER; the same page as gspgtransch, built later)
GSTQUARTERBALL=#4000            ;временно (en: temporarily)
GSTBALL=#C000                   ;2*64*128=16K (en: in page gspgtball)
gspgballer1=6                   ;32K (en: the generated ball code, 2 pages: 6 and #86)

gspgchscr0=2                    ; the two screen pages the ZX reads by DMA, drawn in turn
gspgchscr1=3
gspgdiv=4                       ;(L/2)/-H ;+-7.8=+-6.0/6.0 (en: division table; numbers are int.frac bit counts)
gspgpersp=#84                   ;+-7.0=+-7.0/6.0 (en: perspective table)
gspgsin=5                       ; rotation tables and routines
; in page gspgsin (instpp3 GENJTNROT): angle -> rotation routine (low byte), sin, cos (high byte of the MULTAB
; table for that value); PROCROT = the rotation routines, MULTAB = signed multiplication tables
SIGNOFS=#C000
SINOFS=#C100
COSOFS=#C200
PROCROT=#C300
MULTAB=#C400
; in page gspgtransch: generated transposer code (MKTRANSCH) and TCH256 = chunk -> 16-colour byte (2 rows)
gspgtransch=#85
transch=#E000
TCH256=#FE00

mirrorchunks=1                  ;строки 0,1,3,2,4,5,7,6... (en: lines 0,1,3,2,4,5,7,6...)
perspdist=32
trisortmesspop=1                ;1=сокращает код без потери скорости (en: 1 = shortens the code without losing speed)
incW=0                          ;1=исключает выскакивание за текстуру, но дергается (en: 1 = prevents running out of the texture, but it jitters)
texborder=1                     ;размер запаса на краю текстуры, против выскакивания (en: margin at the texture edge, against running out of it)
texarea=63-texborder-texborder
minV=#C0+texborder
maxV=minV+texarea

trimaxh=63
trimaxl=52                      ;ограничение TEXTURER'а ;ограничение расчётов=63 (en: limit of the TEXTURER; limit of the calculations = 63)
; generated scanline code: 20 JP + 16 JR stubs into the long part, 16 entries into the fast part = 52 widths
ntexjp=20
ntexjr=16
ntexfast=16

; GSSETPGA (card): map page A at #C000. setpga (ZX): select #7FFD page A and keep it in curpg for the IM 2 handler.
        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; GO - the author's stand-alone test start (not saved; THELINK.asm's GO does this for the whole demo).
; Card reset, a few GS commands, the card program GSPROG uploaded byte by byte (#18 set address, #19 write byte)
; and started (#13), then commands 3 / 4 / 5: ZX-DMA upload of the data pages, of the code page, DMA off.
; Then the resident loop and the IM 2 handler are copied to #BF04 / #BFBF, the vector table filled, the music
; initialised, and the resident loop started on pgeffects with zxtimings.
; =====================================================================================================================
        ORG #7800
begin
GO
        XOR A
        OUT (#FE),A
        LD SP,#6000

        LD A,C_GRST
        OUT (GSCTR),A
        XOR A
        OUT (GSCOM),A           ;не помогает (en: does not help)
        HALT
        HALT
        DI
; en: for some reason POKE #4092,0 (on the card: #18 = set address, #19 = write a byte there)
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
; en: for some reason #38 (Load FX)
;зачем-то #38 (Load FX)
        SC #38
        WC
; en: for some reason #D2 (Close Stream)
;зачем-то #D2 (Close Stream)
        SC #D2
        WC

        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
; en: as in RIFF TRACKER (GS command #18 with the address, #19 with each byte)
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
; en: for some reason #F5 (Busy on)
;зачем-то #F5 (Busy on)
        SC #F5
        WC
; en: #13 = jump to the address (low byte in GSDAT with the command, high byte after it)
;#13 Jump to Address
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
; en: and wait until it is on (the card answers through ZXDATWR):
;и ждем включения:
        WN
        GD
;
; en: send the data pages to the GS (each LDIR into the ROM area goes to card RAM by ZX-DMA):
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
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
; en: and wait for the switch:
;и ждем переключения:
        WN
        GD
;
; en: send the code pages to the GS:
;шлем страницы кода в GS:
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
; en: and wait until the DMA is off:
;и ждем выключения DMA:
        WN
        GD
;

; Install the resident loop and the IM 2 handler, fill the 257-byte vector table at #BE00 with #BF (vector #BFBF
; = IMER whatever the bus holds), IM 2.
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
        LD (HL),low IMER                ; ALASM keeps the low byte
        LDIR
        IM 2

        LD A,pgmusic
        setpga                  ;заполняет curpg (en: fills curpg); music init
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; MOVEPAGE: copy pages through resbuf with RESMOVEPAGE (shared harness code; not used in this unit).
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; GSDMASENDPG: select ZX page A, LDIR its 16K into #0000-#3FFF: the writes change nothing on the ZX (ROM), but
; NeoGS ZX-DMA takes each byte into card RAM at the running DMA address.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; CL1800PG: select page A, clear #C000-#D7FF and #E000-#F7FF (fill with L = 0). Not called in this unit (TEXZX has
; its own TEXCL1800PG).
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

; ---------------------------------------------------------------------------------------------------------------------
; GSPROG - the card program, assembled for card #5830 (DISP) and sent by GO with GS commands #18/#19.
; Silences the 8 channels, 24 MHz + NOROM + EXPAG, MPAG 1 / MPAGEX 2, ZX-DMA module 1; then the upload handshake:
; command 3 -> DMA at gspgdata, on; 4 -> DMA at gspgcode; 5 -> off, wait until the ZX has read the answer.
; Then the card's resident loop GSRES0 calls #C000 of each code page in turn.
; ---------------------------------------------------------------------------------------------------------------------
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

; wait for command 3; DMA address = page index * #4000: RRCA gives A21..A14 in one byte; the top two bits go to
; DMA_MAD (A15, A14), the low six to DMA_HAD (A21..A16), DMA_LAD (A7..A0) = 0
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
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)

        LD E,4
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)

        LD E,5
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX: DMA OFF)
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: is the ZX ready, has it read the answer?)

; the card's resident loop: map the code page at #C000, CALL it (one effect), next page (rotated increment)
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

; ZX resident loop, copied to ZXRESIDENT (#BF04): for each effect page (#B0 up, OR #10 = 48K ROM) read the start and
; end frame from zxtimings and CALL #C000 with BC = start, DE = end.
WASZXRES
        DISP ZXRESIDENT
ZXRESIDENTGO
ZXRES0
        OR #10
        setpga
        PUSH AF
        LD C,(HL)
        INC HL
        LD B,(HL)               ;время начала эффекта (en: effect start time)
        INC HL
        LD E,(HL)
        INC HL
        LD D,(HL)               ;время конца эффекта (en: effect end time)
        INC HL
        PUSH HL
        CALL #C000
        POP HL
        POP AF
        INC A
        JP ZXRES0
; RESMOVEPAGE: IXH = source page, IXL = destination page, HL -> DE, IYH pages of 256 bytes through resbuf
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
        setpga                  ;портит BC (en: spoils BC)
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
        RET
; this test: the face from frame 224 to frame 876 (the demo's table is in THELINK.asm)
zxtimings
        DW 224,224*4-20         ;tex
        ENT
LENZXRES=$-WASZXRES

; IM 2 handler, copied to IMER (#BFBF). Made for the PUSH blitters, where SP points into the screen: the return
; address the CPU pushed is taken off and replaced by DE (see MKOUT16C), the handler runs on its own stack
; (outimstack #5F00), counts frames in timer, plays the music (page pgmusic with curpg's ROM / screen bits 3-4,
; CALL #C005), restores the page, restores SP and DE and jumps back through outimjp.
WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата (en: we write DE instead of the return address)
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE                 ;hl до прерывания (en: HL before the interrupt)
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
end

; Card data pages for the test upload: girl13.2 = face textures (gspgtex), blugr.* = ball texture (gspgballtex1..).
        ORG #C000,pgdata1
        INCBIN "girl13.2"
        ORG #C000,pgdata2
        INCBIN "blugr.C"
        ORG #C000,pgdata3
        INCBIN "blugr.0"
        ORG #C000,pgdata4
        INCBIN "blugr.1"
        ORG #C000,pgdata5
        INCBIN "blugr.2"

; =====================================================================================================================
; TEXGS - the card half. Entered by CALL #C000 with this page in MPAGEX; copies the effect code (WASGSTEXCODE ..)
; to GSTEXCODE = #5900 and the uv256d table to GSTQUARTERBALL = #4000, then JP GSTEXGO. RET (on command 2) goes
; back to the card's resident loop.
; =====================================================================================================================
        ORG #C000,pgcode1
pgtexgs=pgcode1
        LD HL,WASGSTEXCODE
        LD DE,GSTEXCODE
        LD BC,LENGSTEXCODE
        LDIR

        LD HL,wasuv
        LD DE,GSTQUARTERBALL
        LD BC,uvlen
        LDIR

        JP GSTEXGO

WASGSTEXCODE
        DISP GSTEXCODE
; WAITER0: wait until the ZX's command is 0 ("frame shown"). No timeout: the JP C,GSRESET is commented out.
WAITER0
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0
        RET

; ---------------------------------------------------------------------------------------------------------------------
; The texture mapper's scanline code. The original macros MTEXTURER and PATCHVUCODE are kept below as comments;
; the converter expanded them at their two uses: INC (pixels left to right, INC E) and DEC (right to left, DEC E).
; Each block is 256 bytes on a 256-byte boundary (*SYSTEM):
;   TEXTURER*MAX  36 pixels of "LD HL,nn : ADD HL,BC : LD A,(HL) : LD (DE),A : INC/DEC E" (7 bytes), JP TEXTURER*
;   #xx00         1 byte: the entry for width 0 (low byte of TEXTURER*Q, the RET)
;   TEXTURER*JR   16 JR into TEXTURER*MAX, TEXTURER*JP 20 JP into it (the long widths)
;   TEXTURER*     15 more pixels and the last one "LD A,(BC) : LD (DE),A" (offset 0), TEXTURER*Q: RET
;   the last 52 bytes: low bytes of the entry points, so that L = -width (mod 256) -> LD L,(HL) : JP (HL)
; nn of each pixel is the texture offset of that pixel (V high, U low) for the current triangle: an affine mapping
; has the same offsets on every scanline, so PATCHVU* writes them into the code once per triangle and each scanline
; only sets BC (texture address of its first pixel) and DE (chunk map address). The RET returns through the
; TRISTACK tables (vujump.asm).
; ---------------------------------------------------------------------------------------------------------------------
; (expanded at its calls)        MACRO MTEXTURER
; (expanded at its calls)         DS .(253-((ntexjp+ntexjr)*7)-$)
; (expanded at its calls) ;(20+16)*7+3=255 байт кода для ширины ??..??
; (expanded at its calls) TEXTURER\0MAX
; (expanded at its calls)        DUP ntexjp+ntexjr
; (expanded at its calls)         LD HL,0
; (expanded at its calls)         ADD HL,BC
; (expanded at its calls)         LD A,(HL),(DE),A
; (expanded at its calls)         \0 E
; (expanded at its calls)        EDUP
; (expanded at its calls)         JP TEXTURER\0
; (expanded at its calls) ;это круглый адрес
; (expanded at its calls) ;1 байт адрес для ширины 0
; (expanded at its calls)         DB TEXTURER\0Q
; (expanded at its calls) ;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125)
; (expanded at its calls) TEXTURER\0JR
; (expanded at its calls) _=ntexjp*7+TEXTURER\0MAX
; (expanded at its calls)        DUP ntexjr
; (expanded at its calls)         JR _
; (expanded at its calls) _=_+7
; (expanded at its calls)        EDUP
; (expanded at its calls) ;20*3=60 байт jp для ширины ??..??
; (expanded at its calls) TEXTURER\0JP
; (expanded at its calls) _=TEXTURER\0MAX
; (expanded at its calls)        DUP ntexjp
; (expanded at its calls)         JP _
; (expanded at its calls) _=_+7
; (expanded at its calls)        EDUP
; (expanded at its calls) ;16*7-4=108 байт кода для ширины 16..0
; (expanded at its calls) TEXTURER\0
; (expanded at its calls)        DUP ntexfast-1
; (expanded at its calls)         LD HL,0
; (expanded at its calls)         ADD HL,BC
; (expanded at its calls)         LD A,(HL),(DE),A
; (expanded at its calls)         \0 E
; (expanded at its calls)        EDUP
; (expanded at its calls) ;смещение=0
; (expanded at its calls)         LD A,(BC),(DE),A
; (expanded at its calls) TEXTURER\0Q
; (expanded at its calls)         RET
; (expanded at its calls)         DS 3
; (expanded at its calls) ;20+16+16=52 байт адресов
; (expanded at its calls) _=TEXTURER\0JP
; (expanded at its calls)        DUP ntexjp
; (expanded at its calls)         DB _
; (expanded at its calls) _=_+3
; (expanded at its calls)        EDUP
; (expanded at its calls) _=TEXTURER\0JR
; (expanded at its calls)        DUP ntexjr
; (expanded at its calls)         DB _
; (expanded at its calls) _=_+2
; (expanded at its calls)        EDUP
; (expanded at its calls) _=TEXTURER\0
; (expanded at its calls)        DUP ntexfast
; (expanded at its calls)         DB _
; (expanded at its calls) _=_+7
; (expanded at its calls)        EDUP
; (expanded at its calls)         DISPLAY $,"=#XX00"
; (expanded at its calls) ;итого 256 байт
; (expanded at its calls)        ENDM

; (expanded at its calls)        MACRO PATCHVUCODE
; (expanded at its calls) PATCHVU\0
; (expanded at its calls)         _=\0SYSTEM+257+(ntexjr*2)+(ntexjp*3)+(ntexfast-1*7)-6
; (expanded at its calls)         _V=_
; (expanded at its calls)         __=\0SYSTEM+257+(ntexjr*2)+(ntexjp*3)
; (expanded at its calls)         ___=\0SYSTEM+253-(ntexjr+ntexjp*7)
; (expanded at its calls)        DUP trimaxl+3/4
; (expanded at its calls)         _U=_V
; (expanded at its calls)        DUP 4
; (expanded at its calls)         IF0 _V-___<1&1
; (expanded at its calls)         IFN _V-_<1&1
; (expanded at its calls)         ADD HL,DE ;пропускаем первый
; (expanded at its calls)         ENDIF
; (expanded at its calls)         LD (_V),HL
; (expanded at its calls)         _V=_V-7
; (expanded at its calls)         ENDIF
; (expanded at its calls)         IFN _V-__<1&1
; (expanded at its calls)         IF0 _V-\0SYSTEM-256<1&1
; (expanded at its calls)         _V=\0SYSTEM+253-6
; (expanded at its calls)         ENDIF
; (expanded at its calls)         ENDIF
; (expanded at its calls)        EDUP
; (expanded at its calls)         EXX
; (expanded at its calls)        DUP 4
; (expanded at its calls)        IF0 _U-___<1&1
; (expanded at its calls)         IF0 _U-___<1&1
; (expanded at its calls)         IFN _U-_<1&1
; (expanded at its calls)         ADD HL,DE ;пропускаем первый
; (expanded at its calls)         ENDIF
; (expanded at its calls)         LD A,H,(_U),A
; (expanded at its calls)         _U=_U-7
; (expanded at its calls)         ENDIF
; (expanded at its calls)         IFN _U-__<1&1
; (expanded at its calls)         IF0 _U-\0SYSTEM-256<1&1
; (expanded at its calls)         _U=\0SYSTEM+253-6
; (expanded at its calls)         ENDIF
; (expanded at its calls)         ENDIF
; (expanded at its calls)        ENDIF
; (expanded at its calls)        EDUP
; (expanded at its calls)         IF0 _U-___<1&1
; (expanded at its calls)         EXX
; (expanded at its calls)         INC B
; (expanded at its calls)         RET Z
; (expanded at its calls)         ENDIF
; (expanded at its calls)        EDUP
; (expanded at its calls)         RET
; (expanded at its calls)        ENDM
; align to 256 (ALASM: DS -$&#FF style; the converter spells it DS low (...))
        DS low (-$)
INCSYSTEM
; macro MTEXTURER expanded
        DS low (253-((ntexjp+ntexjr)*7)-$)
; en: (20+16)*7+3 = 255 bytes of code for widths ??..??
;(20+16)*7+3=255 байт кода для ширины ??..??
TEXTURERINCMAX
        DUP ntexjp+ntexjr
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        INC E
        EDUP
        JP TEXTURERINC
; en: this is a round address; 1-byte address for width 0
;это круглый адрес
;1 байт адрес для ширины 0
        DB low TEXTURERINCQ             ; ALASM keeps the low byte
; en: 16*2 = 32 bytes of JR for widths ??..?? (the first one has offset -125)
;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125)
TEXTURERINCJR
_=ntexjp*7+TEXTURERINCMAX
        DUP ntexjr
        JR _
_=_+7
        EDUP
; en: 20*3 = 60 bytes of JP for widths ??..??
;20*3=60 байт jp для ширины ??..??
TEXTURERINCJP
_=TEXTURERINCMAX
        DUP ntexjp
        JP _
_=_+7
        EDUP
; en: 16*7-4 = 108 bytes of code for widths 16..0
;16*7-4=108 байт кода для ширины 16..0
TEXTURERINC
        DUP ntexfast-1
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        INC E
        EDUP
; en: offset = 0 (the first pixel of the scanline is at BC itself)
;смещение=0
        LD A,(BC)
        LD (DE),A
TEXTURERINCQ
        RET
        DS 3
; en: 20+16+16 = 52 bytes of addresses
;20+16+16=52 байт адресов
_=TEXTURERINCJP
        DUP ntexjp
        DB low _                        ; ALASM keeps the low byte
_=_+3
        EDUP
_=TEXTURERINCJR
        DUP ntexjr
        DB low _                        ; ALASM keeps the low byte
_=_+2
        EDUP
_=TEXTURERINC
        DUP ntexfast
        DB low _                        ; ALASM keeps the low byte
_=_+7
        EDUP
        DISPLAY $,'=#XX00'
; en: 256 bytes in all
;итого 256 байт
        DISPLAY /T,TEXTURERINC
; ---------------------------------------------------------------------------------------------------------------------
; PATCHVUINC - generated at assembly time (DUP/IF), one block of straight code per texturer. Entered from PATCHVU
; (texpp4) with HL = V accumulator, DE = dV per pixel, HL' = U accumulator, DE' = dU, B = -(number of 4-pixel
; groups). It walks the LD HL,nn operands of the texturer backwards from the last-but-one pixel, 4 at a time:
; LD (op),HL writes V into the operand's high byte, then (EXX) LD A,H : LD (op),A puts U into its low byte;
; INC B : RET Z stops after enough groups. When _V passes the start of TEXTURERINC it continues at the end of
; TEXTURERINCMAX. The RET returns through the polygon record (DW TRIDRAW), see texpp4 TRI.
; ALASM condition "IF0 x<1&1": x<1 is a 16-bit cyclic left shift, &1 keeps the old bit 15 - IF0 = "if x >= 0"
; (signed), IFN = "if x < 0". The converter spells x<1 as (((x&#FFFF)<<1|(x&#FFFF)>>>(16-1))&#FFFF).
; ---------------------------------------------------------------------------------------------------------------------
; macro PATCHVUCODE expanded
PATCHVUINC
_=INCSYSTEM+257+(ntexjr*2)+(ntexjp*3)+((ntexfast-1)*7)-6
_V=_
__=INCSYSTEM+257+(ntexjr*2)+(ntexjp*3)
___=INCSYSTEM+253-((ntexjr+ntexjp)*7)
        DUP (trimaxl+3&#FFFF)/(4&#FFFF)
_U=_V
        DUP 4
        IF ((((_V-___&#FFFF)<<1|(_V-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF (((_V-_&#FFFF)<<1|(_V-_&#FFFF)>>>(16-1))&#FFFF)&1
        ADD HL,DE               ;пропускаем первый (en: skip the first: no add before the first operand)
        ENDIF
        LD (_V),HL
_V=_V-7
        ENDIF
        IF (((_V-__&#FFFF)<<1|(_V-__&#FFFF)>>>(16-1))&#FFFF)&1
        IF ((((_V-INCSYSTEM-256&#FFFF)<<1|(_V-INCSYSTEM-256&#FFFF)>>>(16-1))&#FFFF)&1)==0
_V=INCSYSTEM+253-6
        ENDIF
        ENDIF
        EDUP
        EXX
        DUP 4
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF (((_U-_&#FFFF)<<1|(_U-_&#FFFF)>>>(16-1))&#FFFF)&1
        ADD HL,DE               ;пропускаем первый
        ENDIF
        LD A,H
        LD (_U),A
_U=_U-7
        ENDIF
        IF (((_U-__&#FFFF)<<1|(_U-__&#FFFF)>>>(16-1))&#FFFF)&1
        IF ((((_U-INCSYSTEM-256&#FFFF)<<1|(_U-INCSYSTEM-256&#FFFF)>>>(16-1))&#FFFF)&1)==0
_U=INCSYSTEM+253-6
        ENDIF
        ENDIF
        ENDIF
        EDUP
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        EXX
        INC B
        RET Z
        ENDIF
        EDUP
        RET
        DISPLAY $
; DECSYSTEM / PATCHVUDEC: the same two blocks with DEC E - scanlines drawn right to left (right triangles).
        DS low (-$)
DECSYSTEM
; macro MTEXTURER expanded
        DS low (253-((ntexjp+ntexjr)*7)-$)
;(20+16)*7+3=255 байт кода для ширины ??..??
TEXTURERDECMAX
        DUP ntexjp+ntexjr
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        DEC E
        EDUP
        JP TEXTURERDEC
;это круглый адрес
;1 байт адрес для ширины 0
        DB low TEXTURERDECQ             ; ALASM keeps the low byte
;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125)
TEXTURERDECJR
_=ntexjp*7+TEXTURERDECMAX
        DUP ntexjr
        JR _
_=_+7
        EDUP
;20*3=60 байт jp для ширины ??..??
TEXTURERDECJP
_=TEXTURERDECMAX
        DUP ntexjp
        JP _
_=_+7
        EDUP
;16*7-4=108 байт кода для ширины 16..0
TEXTURERDEC
        DUP ntexfast-1
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        DEC E
        EDUP
;смещение=0
        LD A,(BC)
        LD (DE),A
TEXTURERDECQ
        RET
        DS 3
;20+16+16=52 байт адресов
_=TEXTURERDECJP
        DUP ntexjp
        DB low _                        ; ALASM keeps the low byte
_=_+3
        EDUP
_=TEXTURERDECJR
        DUP ntexjr
        DB low _                        ; ALASM keeps the low byte
_=_+2
        EDUP
_=TEXTURERDEC
        DUP ntexfast
        DB low _                        ; ALASM keeps the low byte
_=_+7
        EDUP
        DISPLAY $,'=#XX00'
;итого 256 байт
        DISPLAY /T,TEXTURERDEC
; macro PATCHVUCODE expanded
PATCHVUDEC
_=DECSYSTEM+257+(ntexjr*2)+(ntexjp*3)+((ntexfast-1)*7)-6
_V=_
__=DECSYSTEM+257+(ntexjr*2)+(ntexjp*3)
___=DECSYSTEM+253-((ntexjr+ntexjp)*7)
        DUP (trimaxl+3&#FFFF)/(4&#FFFF)
_U=_V
        DUP 4
        IF ((((_V-___&#FFFF)<<1|(_V-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF (((_V-_&#FFFF)<<1|(_V-_&#FFFF)>>>(16-1))&#FFFF)&1
        ADD HL,DE               ;пропускаем первый
        ENDIF
        LD (_V),HL
_V=_V-7
        ENDIF
        IF (((_V-__&#FFFF)<<1|(_V-__&#FFFF)>>>(16-1))&#FFFF)&1
        IF ((((_V-DECSYSTEM-256&#FFFF)<<1|(_V-DECSYSTEM-256&#FFFF)>>>(16-1))&#FFFF)&1)==0
_V=DECSYSTEM+253-6
        ENDIF
        ENDIF
        EDUP
        EXX
        DUP 4
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        IF (((_U-_&#FFFF)<<1|(_U-_&#FFFF)>>>(16-1))&#FFFF)&1
        ADD HL,DE               ;пропускаем первый
        ENDIF
        LD A,H
        LD (_U),A
_U=_U-7
        ENDIF
        IF (((_U-__&#FFFF)<<1|(_U-__&#FFFF)>>>(16-1))&#FFFF)&1
        IF ((((_U-DECSYSTEM-256&#FFFF)<<1|(_U-DECSYSTEM-256&#FFFF)>>>(16-1))&#FFFF)&1)==0
_U=DECSYSTEM+253-6
        ENDIF
        ENDIF
        ENDIF
        EDUP
        IF ((((_U-___&#FFFF)<<1|(_U-___&#FFFF)>>>(16-1))&#FFFF)&1)==0
        EXX
        INC B
        RET Z
        ENDIF
        EDUP
        RET
        DISPLAY $

; =====================================================================================================================
; GSTEXGO - card: one-time set-up (generators in instpp3.asm), then the per-picture loop GSLOOP.
; =====================================================================================================================
GSTEXGO

        CALL MKBALLER
        CALL MKBALLTEX
        CALL GENDIV
        CALL GENPERSP
        CALL GENJTNROT
        CALL MKTRANSCH

; Copy the model's vertices (KUB) into VTX3DI rows X/Y/Z from index #FF down; VTX3DBEG = first used index.
        LD HL,KUB
        LD B,(HL)
        INC HL
        LD E,0
COPVERTO
        DEC E
        LD D,high VTX3DI
        LD A,(HL)
        LD (DE),A
        INC HL
        INC D
        LD A,(HL)
        LD (DE),A
        INC HL
        INC D
        LD A,(HL)
        LD (DE),A
        INC HL
        DJNZ COPVERTO
        LD A,E
        LD (VTX3DBEG),A

; Face list: TPOLYZ = 0 (Z sum, filled each frame), TPOLYS/+#100 = record address; a 0 after the last one
; ends the list for SORTZ.
        LD A,(HL)
        INC HL
        LD (NPOLYS),A
        LD DE,TPOLYZ
        LD B,A
GENPOLYS0
        XOR A
        LD (DE),A
        INC D
        LD A,(HL)
        INC HL
        LD (DE),A
        INC D
        LD A,(HL)
        INC HL
        LD (DE),A
        DEC D
        DEC D
        INC E
        DJNZ GENPOLYS0
;LD A,#FF
        XOR A
        LD (DE),A

; ---------------------------------------------------------------------------------------------------------------------
; GSLOOP - one picture. Toggle the screen page; advance the animation once per ZX frame passed (timer byte from
; the ZX, at most 8 steps); rotate + project; sort the faces; draw the ball background, the faces, transpose;
; then the frame handshake with the ZX.
; ---------------------------------------------------------------------------------------------------------------------
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: we draw into SCR2)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now we draw into SCR1)
;потом дадим его ZX'у (en: later we give it to the ZX)
; en: frames passed since the last picture = gstimer - gsoldtimer, clipped to 8. (0 makes the DJNZ run 256
; times; with steps 3 and 2 of a 256-entry table that lands on the same phases, it only costs time.)
gstimer=$+1
        LD A,0
gsoldtimer=$+1
        LD C,0
        LD (gsoldtimer),A
        SUB C
        CP 8
        JR C,$+4
        LD A,8
        LD B,A
; Animation step: ALPHA and BETA follow sin256 (phase +3 and +2 per frame); angle1/angle2 scroll the ball,
; ALPHA/BETA low byte = the rotation angle (sin/8), GAMMA fixed at 70. The phases live in the LD A,n operands.
GSTIMERACTION
        LD D,high sin256
        LD HL,ALPHA
        LD A,0
        ADD A,3
        LD ($-3),A
        LD E,A
        LD A,(DE)
        SRA A
        LD (angle1),A
        SRA A
        SRA A
        LD (angle1corr),A
        LD (HL),A
        LD HL,BETA
        LD A,0
        ADD A,2
        LD ($-3),A
        LD E,A
        LD A,(DE)
        SRA A
        LD (angle2),A
        SRA A
        SRA A
        LD (HL),A
        LD HL,GAMMA
        LD (HL),70              ;0
        DJNZ GSTIMERACTION

; perspective distance fixed at 16 (the animated variant is disabled with IF 1)
        LD HL,curperspdist      ;24..48
        IF 1
        LD (HL),16
        ELSE
CPDi=$
        INC (HL)
        LD A,(HL)
        SUB 24
        CP 24
        JR C,CPDnI
        LD A,(CPDi)
        XOR 1
        LD (CPDi),A
CPDnI
        ENDIF

; rotate all vertices, project them
        CALL JTNROTATE
        CALL PERSPCORR

; Z sum of the three vertices of each face, for the painter's sort
        LD HL,TPOLYZ
        LD D,high VTX3DZ
        LD A,(NPOLYS)
        LD B,A
        LD C,128
COUNTZ0
        PUSH HL
        INC H
        LD A,(HL)
        INC H
        LD H,(HL)
        LD L,A                  ;адрес полигона (en: polygon address)
        LD E,(HL)               ;адрес вершины A (en: address of vertex A)
        INC L
        INC HL
        LD A,(DE)               ;ZA (Z of vertex A)
        LD E,(HL)               ;адрес вершины B (en: address of vertex B)
        INC L
        INC HL
        LD L,(HL)               ;адрес вершины C (en: address of vertex C)
        LD H,D
        ADD A,(HL)              ;ZC
        EX DE,HL
        ADD A,(HL)              ;ZB
        EX DE,HL
        POP HL
;ADD A,C ;чтобы без знака (en: so that it is unsigned)
;NEG ;в PERSPCORR -Z понимается как Z и наоборот (en: in PERSPCORR -Z is taken as Z and vice versa)
        LD (HL),A               ;сумма координат Z для вершин грани (en: sum of the Z coordinates of the face's vertices)
        INC L
        DJNZ COUNTZ0

        LD HL,TPOLYZ
        CALL SORTZ

; Ball background: run the generated ball code (MKBALLER, pages 6 and #86 at #8000) that fills the whole chunk map
; from the 8-page ball texture. H = texture row (angle1 + correction), L = column (angle2); the page is
; gspgballtex1 + H/32 (ALASM: ADD A,gspgballtex1*257<1 adds in plain-index form, RRCA rotates back), the row in it
; is H mod 32 + #D0. A' = page for the generated code, IX = return address.
        LD (GSBALLSP),SP
        LD A,gspgballer1
        OUT (MPAG),A
        LD A,72+136
angle1=$+1
        SUB 0
angle1corr=$+1
        ADD A,0
        LD H,A
        LD A,128
angle2=$+1
        SUB 0
        LD L,A
        LD A,H
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,low (((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF) ; ALASM keeps the low byte; ALASM: ADD A,gspgballtex1*257<1 (the page as a plain index)
        RRCA
        OUT (MPAGEX),A
        EX AF,AF'
        LD A,H
        AND #1F
        ADD A,#D0               ;запас 16 на переполнение (en: 16 rows spare for overflow)
        LD H,A
        LD IX,GSBALLQ
        JP #8000
GSBALLQ
GSBALLSP=$+1
        LD SP,0

; Draw the faces back to front: TRI with HL = face record (texpp4)
        LD HL,TPOLYS
NPOLYS=$+1
        LD B,4
DRAWPOLYS0
        PUSH BC
        PUSH HL
        LD A,(HL)
        INC H
        LD H,(HL)
        LD L,A
        CALL TRI
        POP HL
        POP BC
        INC L
        DJNZ DRAWPOLYS0

; Transpose the chunk map (two halves: columns #00 and #80) into the screen page at #8000 (MPAG), via TCH256
        LD A,(gsdrawscr)
        OR A
        LD A,gspgchscr0
        JR Z,$+4
        LD A,gspgchscr1
        OUT (MPAG),A
        LD A,gspgtransch
        GSSETPGA
        LD (GSLOOPSP),SP
        LD IX,GSLOOPTRANSQ
        LD E,31
        LD B,high TCH256
        EXX
        LD HL,gschmap256
        LD B,16
        JP transch
GSLOOPTRANSQ
        LD IX,GSLOOPTRANSQ2
        LD HL,gschmap256+128
        LD B,16
        JP transch
GSLOOPTRANSQ2
GSLOOPSP=$+1
        LD SP,0

; Frame handshake: command 0 -> DMA off, take the ZX's timer byte; then wait for the next command with a 65536-poll
; timeout (-> GSRESET): 1 = point the DMA at the page just drawn and start it, anything else = quit.
        CALL WAITER0            ;GS ожидает конец кадра у ZX (en: the GS waits for the end of the ZX frame)
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        IN A,(ZXDATRD)
        LD (gstimer),A
;OUT (ZXDATWR),A ;признак для ZX "DMA OFF" (en: flag for the ZX: DMA OFF - not sent here)
; en: the GS waits for the zeroing (the ZX will request the data) - in fact for a non-zero command
;GS ожидает обнуления (ZX запросит данные)
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT (any command but 1: the effect ends, back to the card resident loop)

        LD A,(gsdrawscr)
        OR A
        LD A,gspgchscr0
        JR Z,$+4
        LD A,gspgchscr1
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)
        JP GSLOOP

; PERSPCORR: projection with the table in page gspgpersp: row = Z - perspdist (negative, #C0..#FF), column =
; X or Y; Y gets +32 more (the 128 rows are centred at 64, the 64 columns at 32).
PERSPCORR
; en: the Z range could be widened at the cost of X,Y - if Z=L, X,Y=H (RRA:RRA:OR #C0)
;можно расширить диапазон Z за счет X,Y -
;если Z=L, X,Y=H (RRA:RRA:OR #C0)
        LD A,gspgpersp
        GSSETPGA
        LD HL,(VTX3DBEG)
PERSPCORR0
        LD H,high VTX3DZ
        LD D,(HL)               ;Z-perspdist
        DEC H
        LD E,(HL)               ;Y
        LD A,(DE)
        ADD A,high gschmap256+32
        LD (HL),A
        DEC H
        LD E,(HL)               ;X
        LD A,(DE)
;ADD A,32
        LD (HL),A
        INC L
        JR NZ,PERSPCORR0
        RET

; JTNROTATE: rotate every vertex by ALPHA, BETA, GAMMA. For each angle the table in page gspgsin gives the routine
; (low byte of PROCROT, patched into the CALL) and the sin / cos multiplication table pages (H, D). PROCROT: C = X,
; B = Y -> A = X', B = Y'. Output: X, Y and Z/4 - perspdist into VTX3D rows.
JTNROTATE
        LD A,gspgsin
        GSSETPGA

        LD HL,(ALPHA)
        LD A,(HL)
        LD (_ALPCAL),A
        INC H
        LD A,(HL)
        LD (_ASIN),A
        INC H
        LD A,(HL)
        LD (_ACOS),A

        LD HL,(BETA)
        LD A,(HL)
        LD (_BETCAL),A
        INC H
        LD A,(HL)
        LD (_BSIN),A
        INC H
        LD A,(HL)
        LD (_BCOS),A

        LD HL,(GAMMA)
        LD A,(HL)
        LD (_GAMCAL),A
        INC H
        LD A,(HL)
        LD (_GSIN),A
        INC H
        LD A,(HL)
        LD (_GCOS),A

VTX3DBEG=$+1
        LD HL,VTX3D
        LD A,L
        LD IXL,A
___R1
        LD H,VTX3DH
        LD IXH,VTX3DIH
        EXX
        LD C,(IX+0)
        INC IXH
        LD B,(IX+0)
        INC IXH

_ASIN=$+1
        LD H,0
_ACOS=$+1
        LD D,0
_ALPCAL=$+1
        CALL PROCROT            ;INP CB, OUTP AB
        EX AF,AF'
        LD C,B
        LD B,(IX+0)
;A'=X,C=Y,B=Z
_BSIN=$+1
        LD H,0
_BCOS=$+1
        LD D,0
_BETCAL=$+1
        CALL PROCROT
        EX AF,AF'               ;PUT Y
        LD C,B
        LD B,A
;A'=Y,C=Z,B=X
_GSIN=$+1
        LD H,0
_GCOS=$+1
        LD D,0
_GAMCAL=$+1
        CALL PROCROT
;A=Z,B=X,A'=Y
        SRA A
        SRA A
curperspdist=$+1
        SUB perspdist
        LD (_CZ),A
        LD A,B
        EXX
;SRA     A
        LD (HL),A               ;X
        INC H
        EX AF,AF'
;SRA     A
        LD (HL),A               ;Y
        INC H
_CZ=$+1
        LD (HL),0
        INC L
        INC IXL
        JR NZ,___R1
        RET

; SORTZ: selection sort of the face list by Z sum, largest first (back to front), swapping Z and the address.
SORTZ
; en: HL = list of Z (unsigned), 0 at the end; #FF
;HL=список Z (беззнаковых), в конце 0; #FF
; en: the polygon addresses lie at INC H (ld e,(hl):inc h:ld d,(hl))
;через INC H лежат адреса полигонов (ld e,(hl):inc h:ld d,(hl))
; en: as a result they will be in decreasing Z order
;в результате они будут по убыванию Z
SORTZFINDMAX
        PUSH HL
SORTZFINDMAX0
        LD D,H
        LD E,L
        LD A,(HL)
SORTZFINDMAX1
        INC L
        CP (HL)
        JP C,SORTZFINDMAX1
        LD A,(HL)
;INC A
        OR A
        JP NZ,SORTZFINDMAX0
; en: DE = address of the maximum; swap it with the current start of the list
;DE=адрес максимума
;обмениваем с текущим началом списка
        POP HL
        LD A,(DE)
        LD C,(HL)
        LD (HL),A
        LD A,C
        LD (DE),A
        INC H
        INC D
        LD A,(DE)
        LD C,(HL)
        LD (HL),A
        LD A,C
        LD (DE),A
        INC H
        INC D
        LD A,(DE)
        LD C,(HL)
        LD (HL),A
        LD A,C
        LD (DE),A
        DEC H
        DEC H
        INC L
        LD A,(HL)
;INC A
        OR A
        JR NZ,SORTZFINDMAX
        RET

; ---------------------------------------------------------------------------------------------------------------------
; The model and its faces. A face record is read as a stack: TRI does LD SP,record and POPs it field by field, and
; the patcher's final RET pops DW TRIDRAW and goes there. Fields (the author's list below):
;   3 vertex addresses (in the Y row; X is one row below, Z one above); the texture corner (V,U = 0/4) of each
;   vertex; #C300 or #F200 (high byte compared with the opcode of the VUjump entry = side check); texture origin
;   (U/V offset of the 64x64 texture in the page, in both bytes); DW TRIDRAW; texture page in the high byte.
; en: address of vertex A ;Y,X,Z / -"- B / -"- C / VU of vertex A (0/4) / -"- B / -"- C /
; en: #C300 ;to flip the texture use #F200 and swap the VU / texture number in the page (0/64/128/192) (in both
; en: bytes of the word) / TRIDRAW / texture page (in the high byte of the word) / (20 bytes, aligned to 2)
; ---------------------------------------------------------------------------------------------------------------------
;адрес вершины A ;Y,X,Z
;-"- B
;-"- C
;VU вершины A (0/4)
;-"- B
;-"- C
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
KUBSZ=20
; KUB ("cube"): the head model. 34 vertices (X, Y, Z); the first 8 are the corners of a cube of side 2*KUBSZ that
; no face uses (left from the cube test; they are still rotated every frame), 8..33 are the head.
KUB
        DB 34                   ;вершин (en: vertices)
        DB +KUBSZ,+KUBSZ,+KUBSZ
        DB +KUBSZ,+KUBSZ,-KUBSZ
        DB +KUBSZ,-KUBSZ,+KUBSZ
        DB +KUBSZ,-KUBSZ,-KUBSZ
        DB -KUBSZ,+KUBSZ,+KUBSZ
        DB -KUBSZ,+KUBSZ,-KUBSZ
        DB -KUBSZ,-KUBSZ,+KUBSZ
        DB -KUBSZ,-KUBSZ,-KUBSZ
        DB 4,-11,-24
        DB -6,0,-27
        DB -6,-14,-7            ;10
        DB -14,0,-14            ;11 нос (en: nose)
        DB -6,14,-7
        DB 4,11,-24
        DB -11,0,-7             ;14 переносица (en: bridge of the nose)
        DB -5,-12,8
        DB -5,12,8
        DB -10,0,11             ;17
        DB 10,-22,-6
        DB 10,22,-6
        DB -20,0,10             ;20
        DB -5,-20,2
        DB -5,20,2
        DB -3,-15,30
        DB -3,15,30             ;24
        DB 2,-20,-15            ;25
        DB 2,20,-15
        DB 20,-30,6
        DB 20,30,6              ;28
        DB 30,-30,-20
        DB 30,30,-20            ;30
        DB 30,0,-20
        DB 30,-30,20            ;32
        DB 30,30,20
; 28 faces: 12 left, 4 middle (hair), 12 right
        DB 28                   ;граней (en: faces)
        DW KPOLYJAWLEFT
        DW KPOLYMOUTHLEFT
        DW KPOLYNOSELEFT
        DW KPOLYEYELEFT
        DW KPOLYFORELEFT
        DW KPOLYVISOKLEFT
        DW KPOLYJAW2LEFT
        DW KPOLYHAIRLEFT
        DW KPOLYHAIR2LEFT
        DW KPOLYHAIR3LEFT
        DW KPOLYHAIR4LEFT       ;задняя плоскость (en: back plane)
        DW KPOLYHAIR5LEFT

        DW KPOLYHAIRFRONT
        DW KPOLYHAIRBACK
        DW KPOLYHAIRBACK2
        DW KPOLYHAIRBACK3

        DW KPOLYJAWRIGHT
        DW KPOLYMOUTHRIGHT
        DW KPOLYNOSERIGHT
        DW KPOLYEYERIGHT
        DW KPOLYFORERIGHT
        DW KPOLYVISOKRIGHT
        DW KPOLYJAW2RIGHT
        DW KPOLYHAIRRIGHT
        DW KPOLYHAIR2RIGHT
        DW KPOLYHAIR3RIGHT
        DW KPOLYHAIR4RIGHT      ;задняя плоскость (en: back plane)
        DW KPOLYHAIR5RIGHT

; Original KPOLY macro (one face record). ALASM: \4+texborder*257 is (\4+texborder)*257 (left to right), and
; gspgtex<8 is a 16-bit cyclic shift by 8 = the page in the high byte (POP AF puts it into A).
; (expanded at its calls)         MACRO KPOLY
; (expanded at its calls) KPOLY\0
; (expanded at its calls)         DW #FF-\1+VTX3DY
; (expanded at its calls)         DW #FF-\2+VTX3DY
; (expanded at its calls)         DW #FF-\3+VTX3DY
; (expanded at its calls)         DW \5,\6,\7
; (expanded at its calls)         DW \8
; (expanded at its calls)         DW \4+texborder*257
; (expanded at its calls)         DW TRIDRAW
; (expanded at its calls)         DW gspgtex<8
; (expanded at its calls)         ENDM
;в кубе и грань, и текстура против часовой

; en: in the cube both the face and the texture are counter-clockwise
        DS $&1
; en: LEFT is the one on the right on screen (the model's left)
;LEFT на экране правый (у модели)
; macro KPOLY expanded
KPOLYJAWLEFT
        DW #FF-10+VTX3DY
        DW #FF-8+VTX3DY
        DW #FF-9+VTX3DY
        DW #0004,#0000,#0400
        DW #C300
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYMOUTHLEFT
        DW #FF-9+VTX3DY
        DW #FF-11+VTX3DY
        DW #FF-10+VTX3DY
        DW #0004,#0404,#0400
        DW #F200
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYNOSELEFT
        DW #FF-11+VTX3DY
        DW #FF-14+VTX3DY
        DW #FF-10+VTX3DY
        DW #0004,#0404,#0400
        DW #F200
        DW (#80+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYEYELEFT
        DW #FF-15+VTX3DY
        DW #FF-10+VTX3DY
        DW #FF-14+VTX3DY
        DW #0400,#0000,#0004
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYFORELEFT
        DW #FF-17+VTX3DY
        DW #FF-15+VTX3DY
        DW #FF-14+VTX3DY
        DW #0404,#0400,#0004
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYVISOKLEFT
        DW #FF-18+VTX3DY
        DW #FF-10+VTX3DY
        DW #FF-15+VTX3DY
        DW #0000,#0400,#0004
        DW #C300
        DW (#80+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYJAW2LEFT
        DW #FF-18+VTX3DY
        DW #FF-8+VTX3DY
        DW #FF-10+VTX3DY
        DW #0400,#0000,#0004
        DW #F200
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIRLEFT
        DW #FF-21+VTX3DY
        DW #FF-23+VTX3DY
        DW #FF-20+VTX3DY
        DW #0000,#0400,#0004
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR2LEFT
        DW #FF-25+VTX3DY
        DW #FF-27+VTX3DY
        DW #FF-21+VTX3DY
        DW #0404,#0004,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR3LEFT
        DW #FF-23+VTX3DY
        DW #FF-21+VTX3DY
        DW #FF-27+VTX3DY
        DW #0400,#0004,#0000
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR4LEFT
        DW #FF-29+VTX3DY
        DW #FF-31+VTX3DY
        DW #FF-32+VTX3DY
        DW #0400,#0404,#0004
        DW #C300
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR5LEFT
        DW #FF-32+VTX3DY
        DW #FF-27+VTX3DY
        DW #FF-23+VTX3DY
        DW #0004,#0000,#0400
        DW #C300
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)

; macro KPOLY expanded
KPOLYHAIRFRONT
        DW #FF-24+VTX3DY
        DW #FF-20+VTX3DY
        DW #FF-23+VTX3DY
        DW #0000,#0400,#0004
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIRBACK
        DW #FF-33+VTX3DY
        DW #FF-31+VTX3DY
        DW #FF-32+VTX3DY
        DW #0404,#0004,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIRBACK2
        DW #FF-24+VTX3DY
        DW #FF-33+VTX3DY
        DW #FF-32+VTX3DY
        DW #0404,#0004,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIRBACK3
        DW #FF-24+VTX3DY
        DW #FF-32+VTX3DY
        DW #FF-23+VTX3DY
        DW #0404,#0004,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)

; macro KPOLY expanded
KPOLYJAWRIGHT
        DW #FF-13+VTX3DY
        DW #FF-12+VTX3DY
        DW #FF-9+VTX3DY
        DW #0000,#0004,#0400
        DW #F200
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYMOUTHRIGHT
        DW #FF-11+VTX3DY
        DW #FF-9+VTX3DY
        DW #FF-12+VTX3DY
        DW #0404,#0004,#0400
        DW #C300
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYNOSERIGHT
        DW #FF-14+VTX3DY
        DW #FF-11+VTX3DY
        DW #FF-12+VTX3DY
        DW #0404,#0004,#0400
        DW #C300
        DW (#80+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYEYERIGHT
        DW #FF-12+VTX3DY
        DW #FF-16+VTX3DY
        DW #FF-14+VTX3DY
        DW #0000,#0400,#0004
        DW #C300
        DW (#00+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYFORERIGHT
        DW #FF-16+VTX3DY
        DW #FF-17+VTX3DY
        DW #FF-14+VTX3DY
        DW #0400,#0404,#0004
        DW #C300
        DW (#00+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYVISOKRIGHT
        DW #FF-12+VTX3DY
        DW #FF-19+VTX3DY
        DW #FF-16+VTX3DY
        DW #0400,#0000,#0004
        DW #F200
        DW (#80+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYJAW2RIGHT
        DW #FF-13+VTX3DY
        DW #FF-19+VTX3DY
        DW #FF-12+VTX3DY
        DW #0000,#0400,#0004
        DW #C300
        DW (#40+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIRRIGHT
        DW #FF-20+VTX3DY
        DW #FF-24+VTX3DY
        DW #FF-22+VTX3DY
        DW #0000,#0400,#0004
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR2RIGHT
        DW #FF-22+VTX3DY
        DW #FF-28+VTX3DY
        DW #FF-26+VTX3DY
        DW #0404,#0004,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR3RIGHT
        DW #FF-28+VTX3DY
        DW #FF-22+VTX3DY
        DW #FF-24+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR4RIGHT
        DW #FF-30+VTX3DY
        DW #FF-31+VTX3DY
        DW #FF-33+VTX3DY
        DW #0004,#0404,#0400
        DW #C300
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
; macro KPOLY expanded
KPOLYHAIR5RIGHT
        DW #FF-33+VTX3DY
        DW #FF-28+VTX3DY
        DW #FF-24+VTX3DY
        DW #0000,#0004,#0400
        DW #C300
        DW (#C0+texborder)*257
        DW TRIDRAW
        DW (((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)

; Angle pointers into page gspgsin: SIGNOFS + angle; +#100 = sin table, +#200 = cos table
ALPHA   DW SIGNOFS+#40
BETA    DW SIGNOFS+#00
GAMMA   DW SIGNOFS+#00

        INCLUDE "texpp4.asm"

; GSRESET: on a handshake timeout - DMA off, back to 10 MHz, ROM, no EXPAG, RST 0 (card firmware restart)
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

; sin256: 127*SIN, 256 steps (sin+.bas), on a 256-byte boundary (animation phases)
        DS low (-$)
sin256
        INCBIN "sin+"

; vujump.asm: the TRISTACK return-address tables and the VUjump table (256-byte aligned)
        INCLUDE "vujump.asm"

        DISPLAY 'без инсталлеров конец кода=',$ ; en: end of code without the installers
; en: may be overwritten (from here on: the one-time generators of instpp3.asm and palgirl)
;можно затирать
        INCLUDE "instpp3.asm"
palgirl
        INCBIN "palgirl"
        DISPLAY 'с инсталлерами конец кода=',$ ; en: end of code with the installers
        ENT
LENGSTEXCODE=$-WASGSTEXCODE
        DISPLAY /T,LENGSTEXCODE
; uv256d: the u,v table of a quarter of the ball lens (64 x 32 pairs, uv256d.bas); copied to card #4000
wasuv
        INCBIN "uv256d"
uvlen=$-wasuv
LENTEXGS=$-#C000

; =====================================================================================================================
; TEXZX - the ZX half. Called by the resident loop with BC = start frame, DE = end frame. Copies the code to
; TEXCODE (#7800) and "returns" into it (PUSH DE : RET). There: clear the 16-colour pixel areas of pages 4-7, turn
; on the 16-colour mode (#EFF7 = 1), generate the blitter OUT16C, wait for the start frame, then BIGLOOP.
; =====================================================================================================================
        ORG #C000,pgeffects
pgtexzx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASTEXCODE
        LD DE,TEXCODE
        PUSH DE
        LD BC,LENTEXCODE
        LDIR
        RET

WASTEXCODE
        DISP TEXCODE
        POP HL
        LD (zxendtime),HL
        HALT

        LD A,#14
        CALL TEXCL1800PG
        LD A,#15
        CALL TEXCL1800PG
        LD A,#16
        CALL TEXCL1800PG
        LD A,#17
        CALL TEXCL1800PG

        LD A,1                  ;16C
        LD BC,#EFF7
        OUT (C),A

        LD HL,OUT16C
        CALL MKOUT16C

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

; BIGLOOP - one shown picture. A alternates #17 / #1D: screen 0 shown with page 7 at #C000 / screen 1 shown with
; page 5 at #C000 (the value is kept in the XOR operand, LD ($-1),A). VIEWPG1/VIEWPG0 = that page and the one below
; (6 / 4): the 16-colour screen being drawn. Their two values also form HL = the ROM-area address OUT16C reads.
BIGLOOP
        HALT
        LD A,(timer)
        OUT (GSDAT),A
; en: let the GS know the frame is ready and the DMA must be switched off:
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

        LD A,#17^#1D
        XOR #1D                 ;видим scr1, рисуем scr0 (en: we see scr1, draw into scr0)
        LD ($-1),A              ;теперь видим scr0, рисуем scr1 (en: now we see scr0, draw into scr1)
        LD (VIEWPG1),A
        DEC A
        LD (VIEWPG0),A
        setpga

; en: and wait until it is off:
;и ждем выключения:
;WN
;GD
        WD                      ;пока не примет (en: until it takes the timer byte)
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
        JR NC,QUIT              ;можно жать RESET (en: you may press RESET)
; en: let the GS know it must switch the DMA on
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
; en: and wait until it is on:
;и ждем включения:
        WN
        GD
;
; Read the picture: HL = #16xx/#1Cxx (both page values; any address in #0000-#3FFF is a ZX-DMA read), one byte
; read and dropped, then the generated OUT16C with SP as the write pointer; it returns with JP (IX).
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1617
        LD A,(HL)               ;первый байт неверен (en: the first byte is wrong - read and dropped)
        JP OUT16C
BIGLOOPVIEWQ
BIGLOOPSP=$+1
        LD SP,0
        JP BIGLOOP
; QUIT: command 2 (the card's GSLOOP returns), page 0 + screen 0, black attributes, 16 colours off, back to the
; resident loop.
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of the effect)
        HALT
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

; TEXCL1800PG: select page A, clear #C000-#D7FF and #E000-#F7FF (the two pixel banks of a 16-colour screen half)
TEXCL1800PG
        setpga
        LD HL,#C000
        CALL TEXCL1800
        LD HL,#E000
TEXCL1800
        LD D,H
        LD E,L
        INC E
        LD BC,#17FF
        LD (HL),L
        LDIR
        RET

; ---------------------------------------------------------------------------------------------------------------------
; MKOUT16C - generates OUT16C at HL (~13.7K, from the end of TEXCODE up). For each of 64 pixel lines (from line 159
; up; DE = #D760) it emits: LD A,H, then the loop body "LD (curpg),A : OUT (C),A" + 4 blocks "LD SP,line end :
; 16 x (LD D,(HL) : LD E,(HL) : PUSH DE)" (line y and line y-64, banks #C000 and #E000), then CP H : LD A,L :
; JP Z,body - the body runs once with page H and once with page L (4 x 32 bytes x 2 pages = the 256 pixels of two
; lines). 64 x 256 = 16384 bytes = one card screen page. Ends with JP (IX).
; mirrorchunks swaps pixel lines 2<->3, 6<->7 of each character row: the card's TRANSCHCODE writes the second chunk
; of each pair bottom row first.
; ---------------------------------------------------------------------------------------------------------------------
; en: copy the linear 16C buffer from NGS to the screen, bottom to top;
; en: the interrupt handler must put DE on the stack instead of the return address
;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
; en: 13K of code for 256X128 (the sketch below: LD SP,bottom / LD SP,top; the same bottom+#2000 and top+#2000)
;13K кода для 256X128
;LD A,H
;LOOP  ;LD (curpg),A
;OUT (C),A
;LD SP,низ
;DUP 16
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;LD SP,верх
;DUP 16
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;то же низ+#2000 и верх+#2000
;EDUP
;CP H
;LD A,L
;JP Z,LOOP
; en: for 8 lines in #C080..#C780, 7 lines #C900..#CF00, 7 lines #D100..#D700 the last PUSH must be LD (..),DE
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD DE,#D760
        LD B,64                 ;высота 64*2 (en: height 64*2)
MKOUT16CLINE
        PUSH BC
        LD (HL),#7C             ;ld a,h
        INC HL
        LD (mkout16cloop),HL
        LD (HL),#32             ;ld (),a
        INC HL
        LD (HL),low curpg               ; ALASM keeps the low byte
        INC HL
        LD (HL),high curpg
        INC HL
        LD (HL),#ED
        INC HL
        LD (HL),#79             ;out (c),a
        INC HL
        PUSH DE
        IF mirrorchunks
        BIT 1,D
        JR Z,$+6
        LD A,D
        XOR 1
        LD D,A
        ENDIF
        PUSH DE
        CALL MKOUT16C0
        LD A,D
        SUB 8
        LD D,A
        CALL MKOUT16C0
        POP DE
        SET 5,D
        CALL MKOUT16C0
        LD A,D
        SUB 8
        LD D,A
        CALL MKOUT16C0
        POP DE
        LD (HL),#BC             ;cp h
        INC HL
        LD (HL),#7D             ;ld a,l
        INC HL
        LD (HL),#CA             ;jp z
        INC HL
mkout16cloop=$+1
        LD BC,0
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD A,D
        DEC D
        AND 7
        JR NZ,MKOUT16CUDEQ
        LD A,E
        SUB 32
        LD E,A
        JR C,MKOUT16CUDEQ
        LD A,D
        ADD A,8
        LD D,A
MKOUT16CUDEQ
        POP BC
        DJNZ MKOUT16CLINE
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)
        RET

; MKOUT16C0: one block: LD SP,DE+32, 16 x LD D,(HL) : LD E,(HL) : PUSH DE. For a line starting at #xx00 or #xx80 the
; last PUSH becomes LD (line start),DE: SP stays 2 bytes higher, so an interrupt there only rewrites the 2 bytes
; just stored, with the same DE (IMER), instead of 2 bytes in front of the line.
MKOUT16C0
        LD (HL),49
        INC HL
        LD A,E
        ADD A,32
        LD (HL),A
        ADC A,D
        SUB (HL)
        INC HL
        LD (HL),A
        LD B,16
MKOUT16C1
        INC HL
        LD (HL),#56             ;d,(hl)
        INC HL
        LD (HL),#5E             ;e,(hl)
        INC HL
        LD (HL),#D5             ;push de
        DJNZ MKOUT16C1
; en: for 8 lines in #C080..#C780, 7 lines #C900..#CF00, 7 lines #D100..#D700 the last PUSH must be LD (..),DE
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD A,E
        ADD A,A
        JR NZ,MKOUT16CnPROTECT
; en: there will be 32 such lines instead of 22, otherwise it is a pain to check
;будет 32 таких линии вместо 22, а то запарно проверять
        LD (HL),#ED
        INC HL
        LD (HL),#53             ;(),de
        INC HL
        LD (HL),E
        INC HL
        LD (HL),D
MKOUT16CnPROTECT
        INC HL
        RET

; OUT16C: the generated blitter starts here (not part of the TEXZX file)
OUT16C
        ENT
LENTEXCODE=$-WASTEXCODE
LENTEXZX=$-#C000

; Test harness: the next effect page only holds JR $ (the test stops here); the music page.
        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ObjTab: the author's object table for SAVEOBJ4 - name + type (9 bytes), #C000, length, #7FFD page, #C000.
        ORG end
ObjTab
        DB 'TEXZX   C'
        DW #C000
        DW LENTEXZX
        DB pgtexzx
        DW #C000

        DB 'TEXGS   C'
        DW #C000
        DW LENTEXGS
        DB pgtexgs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
        SLOT 3
        PAGE pgtexzx
        SAVEBIN "TEXZX",#C000,LENTEXZX
        PAGE pgtexgs
        SAVEBIN "TEXGS",#C000,LENTEXGS
