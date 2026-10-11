; =====================================================================================================================
; hedge12.asm - The Link (Alone Coder, 2009): the hedgehog part (frames 3584-4466 of the demo)
;
; A spiky solid (the "hedgehog": 32 vertices, 60 textured, shaded triangles) turns in front of a 16-colour picture.
; The NeoGS card does all the 3D work: rotation, perspective, depth sort, texture mapping into a chunky map and the
; conversion of that map into Pentagon 16-colour screen bytes. The ZX only shows the result: every frame it reads the
; card's finished picture through ZX-DMA (LD D,(HL) : LD E,(HL) from the ROM area) and stack-blits it (PUSH DE) into
; the hidden 16-colour screen.
;
; Objects (ObjTab at the end; written by the SAVEBIN block in the sjasmplus build). The pages are this file's; the
; release loader puts the same files at #7FFD #B4, #B5 and #34 (sjasm/loader/README.md section 2):
;   HEDGEZX1  #C000, page pgeffects (#B0)   ZX half 1: a MOVEPAGE stub + DEMON16.C (#3800 bytes). Copies the picture
;                                          to #C000 of #7FFD page #16 (RAM 6) and returns to the resident.
;   HEDGEZX2  #C000, page pgeffects+1 (#B1) ZX half 2: copies HEDGECODE to #7800 and DEMON17.C to page #17 (RAM 7),
;                                          then runs HEDGECODE (the ZX effect loop) until the end frame.
;   HEDGEGS   #C000, page pgcode1 (#52)     the card half: the card resident CALLs it at #C000 of its code page; it
;                                          copies itself to card #5900 (GSHEDGECODE) and runs GSHEDGEGO.
;
; The block at #7800 (GO ... end) is in no object: it is the author's stand-alone test harness, THELINK.asm's start-up
; cut down to this part. ALASM RAN it after assembly: reset the card, send the card program GSPROG (GS commands
; #18/#19, start with #13), upload kishki (pgdata1) and HEDGEGS (pgcode1) by ZX-DMA (commands 3/4/5), install the ZX
; resident loop and the IM 2 handler, start the music and run the effect pages with zxtimings.
;
; Card half: GSHEDGEGO (set-up: 8 shaded texture copies, the generators of instlpp.asm, the model) and GSLOOP (one
; picture: rotate, project, sort, clear, draw the faces with TRI, convert with the generated `transch`, hand over).
; ZX half: HEDGECODE (16-colour mode on, MKOUT16C generates the blit OUT16C) and BIGLOOP (one frame).
; Per-frame protocol (docs/disasm/demo/thelink/README.md section 6): the ZX sends its timer's low byte and command 0
; ("frame shown, stop the DMA"), flips the screens, sends command 1 ("start the DMA") and reads the picture; command 2
; ends the part. The card draws, waits for 0, stops the DMA, waits for 1 (a 65536-poll timeout resets the card) or
; 2 (return to the card resident), points the DMA at the finished screen and starts it.
;
; Included: gsports.asm (NeoGS ports and the GS command macros SD/SC/WC/WD/WN/GD), texpp4.asm and vujump.asm (the
; shared triangle texture mapper TRI/TRIDRAW and its jump tables), instlpp.asm (card table/code generators),
; SAVEOBJ4.asm (the author's ALASM object saver; replaced here by the SAVEBIN block at the end).
; INCBIN: kishki.C (texture, 16K), palbrick (1024 bytes: 4 colours per chunk value), DEMON16.C / DEMON17.C (the
; 16-colour background, two halves), thelinkm (the music, test harness only).
;
; Converter additions (ALASM -> sjasmplus, see sjasm/README.md section 3): the DEVICE line; the SAVEBIN block at the end;
; `low x` with the comment "ALASM keeps the low byte" where ALASM silently keeps an operand's low byte; the macros
; MTEXTURER, PATCHVUCODE, DOTV0_1, INSFR, KPOLY and KPOL expanded at their call sites (sjasmplus cannot glue
; parameters into names): the original macro text stays as "; (expanded at its calls)" comment lines and each copy
; is marked "; macro X expanded"; ALASM's label `m` renamed `L_m` (a reserved word in sjasmplus); ALASM's unsigned
; 16-bit `/`, cyclic shifts `x<n` and IF0/IFN written out as (a&#FFFF)/(b&#FFFF), ((x<<n|x>>>(16-n))&#FFFF) and ==0.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
border=0
debug=0                         ;1=проверка отрицательной ширины (en: 1 = check for a negative width)

; en: #7FFD page values of the stand-alone test harness (the release loader has its own page plan)
;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

; en: ZX addresses: HEDGECODE - where HEDGEZX2 puts the ZX effect code; resbuf ... outimstack - the resident's
; en: buffers, IM 2 table, current page, frame counter, loop and interrupt handler (the same as in THELINK.asm)
HEDGECODE=#7800

resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

parthgt=28                      ;высота экранной четверти (en: height of a screen quarter, in pixel lines)

; en: shading (texpp4.asm): brightness = (Y of the face's "vnormal" vertex - lambert) >> lambertshr, 0..lambertmax
lambert=32-16
lambertshr=3
lambertmax=7

texdec=1                        ;с заворотом (en: with wrap-around)
; en: card memory of the effect (card addresses; #8000 is the MPAG window, #C000 the MPAGEX window):
; en: gschmap256 - the chunk map (row = H, 64 rows); gschscr - the converted 16-colour screen;
; en: TPOLYZ/TPOLYS - the face list (Z sums, record addresses); VTX3DI/VTX3D - vertex X/Y/Z tables, #100 apart
gschmap256=#0000
gschmaphgt=64                   ;с заворотом в 2 раза больше (en: with wrap-around twice as many)
gschscr=#8000                   ;2 страницы - 2 экрана (en: 2 pages - 2 screens)
TPOLYZ=#4000
TPOLYS=TPOLYZ+#100              ;#200 (L,H)
;исходные координаты: (en: source coordinates)
VTX3DI=#4300                    ;,#300
VTX3DIH=high VTX3DI
;координаты после поворота: (en: coordinates after the rotation)
VTX3D=#4600                     ;,#300
VTX3DH=high VTX3D
VTX3DY=VTX3D+#100
VTX3DZ=VTX3D+#200
;X 3D-движка растет на экране вверх (en: the 3D engine's X grows upward on the screen)
;Y 3D-движка растет на экране вправо (en: the 3D engine's Y grows to the right on the screen)

; en: card pages. A card page number counts 32K pages in bits 0-6, bit 7 picks the upper 16K half (see the DMA
; en: address code in GSPROGGO; #04/#84 are the two halves of one 32K page). gspgchscr0/1 - the two converted screens
; en: (mapped at #8000); gspgdiv, gspgpersp, gspgsin, gspgtransch - tables and generated code (at #C000, see
; en: instlpp.asm); gspgtex - 8 texture copies, one per brightness level
gspgchscr0=2
gspgchscr1=3
gspgdiv=#04                     ;(L/2)/-H ;+-7.8=+-6.0/6.0 (en: division table, fixed-point formats)
gspgpersp=#84                   ;+-7.0=+-7.0/6.0
gspgsin=#05
SIGNOFS=#C000
SINOFS=#C100
COSOFS=#C200
PROCROT=#C300
MULTAB=#C400
gspgtransch=#85
transch=#E000
TCH256=#FE00
gspgtex=#06                     ;8 страниц (en: 8 pages)

; en: where the test harness uploads: code pages from card page #10, data pages from #20
gspgcode=#10
gspgdata=#20

mirrorchunks=1                  ;строки 0,1,3,2,4,5,7,6... (en: lines 0,1,3,2,4,5,7,6...)
perspdist=16                    ;32
trisortmesspop=1                ;1=сокращает код без потери скорости (en: 1 = shorter code without a loss of speed)
incW=0                          ;1=исключает выскакивание за текстуру, но дергается (en: 1 = never runs out of the texture, but jerks)
texborder=1                     ;размер запаса на краю текстуры, против выскакивания (en: size of the margin at the texture edge, against running out of it)
texarea=63-texborder-texborder
minV=#C0+texborder
maxV=minV+texarea

trimaxh=63
trimaxl=52                      ;ограничение TEXTURER'а ;ограничение расчётов=63 (en: limit of the TEXTURER ; limit of the calculations = 63)
; en: shape of the generated scanline texturer (MTEXTURER below): 20 JP + 16 JR entries + 16 short-width steps
ntexjp=20
ntexjr=16
ntexfast=16

        INCLUDE "gsports.asm"
; en: card addresses: the start-up program and its stack at #5830, the effect code at #5900
GSPROG=#5830
GSSTACK=#5830
GSHEDGECODE=#5900

; en: GSSETPGA (card): map card page A at #C000. setpga (ZX): switch #7FFD to A and remember it in curpg, which the
; en: IM 2 handler restores after the music
        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; GO: the stand-alone test harness (not saved; THELINK.asm does the same for the whole demo)
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
; en: for some reason POKE #4092,0 (GS commands #18 = set address, #19 = write byte)
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

; en: send the card program WASGSPROG to card #5830, one byte per #18 (address) + #19 (byte)
        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER (en: as in RIFF TRACKER)
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
;#13 Jump to Address
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

; en: zero #C000-#D7FF and #E000-#F7FF of RAM 4 and 5 (#7FFD #14, #15): the 16-colour screen 0
        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
;и ждем включения: (en: and wait until it is on)
        WN
        GD
;
; en: send the data pages to the card (here only pgdata1 = kishki)
;шлем страницы данных в GS:
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения: (en: and wait for the switch)
        WN
        GD
;
; en: send the code pages to the card (here only pgcode1 = HEDGEGS)
;шлем страницы кода в GS:
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA: (en: and wait until the DMA is off)
        WN
        GD
;

; en: install the resident loop at #BF04 and the IM 2 handler at #BFBF; the vector table #BE00-#BF00 is filled
; en: with #BF, so any vector byte leads to #BFBF
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

; en: init the music (#C000 of its page), timer = 0, then run the effect pages from pgeffects with zxtimings
        LD A,pgmusic
        setpga                  ;заполняет curpg (en: fills curpg)
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; en: MOVEPAGE dstpage,src,len,dst: copy len bytes (whole 256-byte blocks) from src in the current page to dst in
; en: #7FFD page dstpage through resbuf (always mapped), by the resident's RESMOVEPAGE, which ends with RET: so a
; en: page stub that ends with MOVEPAGE returns to whoever called it. JP RESMOVEPAGE is this file's address; the
; en: resident of THELINK.asm has RESMOVEPAGE at the same place (its ZXRES0 loop is the same length).
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; en: send one 16K ZX page to the card: LDIR #C000 -> #0000. The writes go to the ROM area and change nothing on the
; en: ZX, but ZX-DMA stores each byte into card RAM.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; en: switch to page A, zero #C000-#D7FF and #E000-#F7FF (fill byte = L = 0)
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
; en: the card start-up program (THELINK.asm's GSPROGGO, cut down), at card #5830: silence the 8 channels, 24 MHz,
; en: NOROM (RAM at #0000), EXPAG; DMA module 1 (ZX-DMA). Command 3: DMA to gspgdata; command 4: DMA to gspgcode;
; en: command 5: DMA off; then the card resident GSRES0.
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

        LD E,3
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

; en: DMA address = card page * 32K (+16K if bit 7): RRCA, then bits 7-6 (page bits 0 and 7 = address bits 15-14)
; en: go to DMA_MAD, the rest (page bits 1-6 = address bits 21-16) to DMA_HAD
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: sign for the ZX "DMA ON")

        LD E,4
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: sign for the ZX "DMA ON")

        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: sign for the ZX "DMA OFF")
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready?)

        LD A,gspgcode
;JP GSRESIDENTGO

; en: the card resident: map code page A at #C000, CALL #C000 (one effect), next page. RLCA : INC A : RRCA steps
; en: the page number in the card's order: #10, #90, #11, #91 ...
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

; ---------------------------------------------------------------------------------------------------------------------
; en: the ZX resident loop at #BF04 (a copy of THELINK.asm's): for each effect page from pgeffects (bit 4 = 48K ROM)
; en: read the start frame into BC and the end frame into DE from zxtimings and CALL #C000 of the page
; ---------------------------------------------------------------------------------------------------------------------
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
; en: RESMOVEPAGE (see MOVEPAGE): IXH = source page, IXL = destination page, HL = source, DE = destination,
; en: IYH = number of 256-byte blocks; each block goes through resbuf (#BD00, outside the paged window)
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
; en: start frame, end frame per effect page: HEDGEZX1 (#B0) only copies its picture; HEDGEZX2 (#B1) runs the effect
; en: from frame 896 to 1772 (in the release, THELINK.asm: 3584-4466)
zxtimings
        DW 0,0
        DW 896,896*2-20         ;hedge
        ENT
LENZXRES=$-WASZXRES

; ---------------------------------------------------------------------------------------------------------------------
; en: the IM 2 handler at #BFBF. It must work while the ZX blits with SP inside the screen: the return address the
; en: CPU pushed sits where the next PUSH DE goes, so EX DE,HL : EX (SP),HL puts DE there instead (screen data, not an
; en: address) and keeps the return address for the final JP (outimjp). It runs on its own stack (outimstack),
; en: counts timer, plays the music (#C005 of pgmusic, keeping curpg's screen and ROM bits) and restores curpg.
; ---------------------------------------------------------------------------------------------------------------------
WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата (en: write DE in place of the return address)
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE                 ;hl до прерывания (en: HL from before the interrupt)
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

; en: card data page 1: the texture (16K), uploaded to card page gspgdata (#20)
        ORG #C000,pgdata1
        INCBIN "kishki.C"

; =====================================================================================================================
; HEDGEGS: the card half. The card resident CALLs #C000 of this page; the stub copies the code to #5900 (outside the
; #8000/#C000 windows, which the effect remaps) and jumps to GSHEDGEGO. GSLOOP returns (command 2) to the resident.
; =====================================================================================================================
        ORG #C000,pgcode1
pghedgegs=pgcode1
        LD HL,WASGSHEDGECODE
        LD DE,GSHEDGECODE
        LD BC,LENGSHEDGECODE
        LDIR
        JP GSHEDGEGO

WASGSHEDGECODE
        DISP GSHEDGECODE
; en: WAITER0: wait until the ZX writes command 0 ("frame shown"); the timeout is commented out here
WAITER0
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0
        RET

; en: MTEXTURER (the original macro text, kept as comments): one 256-byte page of scanline texturer. A pixel is
; en: LD HL,nn : ADD HL,BC : LD A,(HL) : LD (DE),A : INC E (or DEC E); nn is patched per triangle with the texture
; en: offset of that pixel, BC is the texture position. Entered for a width W through a table of low bytes: up to 16
; en: pixels straight into the short block, wider ones through a JR or JP into the long block. PATCHVUCODE generates
; en: the patcher of the nn operands. Both are used by TRI/TRIDRAW in texpp4.asm.
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
; en: align to 256: each texturer fills one page, its entry table holds low bytes only
        DS low (-$)
; ---------------------------------------------------------------------------------------------------------------------
; en: INCSYSTEM: the texturer with INC E (writes left to right) and its patcher PATCHVUINC
; ---------------------------------------------------------------------------------------------------------------------
INCSYSTEM
; macro MTEXTURER expanded
        DS low (253-((ntexjp+ntexjr)*7)-$)
;(20+16)*7+3=255 байт кода для ширины ??..?? (en: 255 bytes of code for widths ??..??)
TEXTURERINCMAX
        DUP ntexjp+ntexjr
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        INC E
        EDUP
        JP TEXTURERINC
;это круглый адрес (en: this is a round address)
;1 байт адрес для ширины 0 (en: 1-byte address for width 0)
        DB low TEXTURERINCQ             ; ALASM keeps the low byte
;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125) (en: 32 bytes of JR for widths ??..?? (the first has offset -125))
TEXTURERINCJR
_=ntexjp*7+TEXTURERINCMAX
        DUP ntexjr
        JR _
_=_+7
        EDUP
;20*3=60 байт jp для ширины ??..?? (en: 60 bytes of JP for widths ??..??)
TEXTURERINCJP
_=TEXTURERINCMAX
        DUP ntexjp
        JP _
_=_+7
        EDUP
;16*7-4=108 байт кода для ширины 16..0 (en: 108 bytes of code for widths 16..0)
TEXTURERINC
        DUP ntexfast-1
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        INC E
        EDUP
;смещение=0 (en: offset = 0)
        LD A,(BC)
        LD (DE),A
TEXTURERINCQ
        RET
        DS 3
;20+16+16=52 байт адресов (en: 52 bytes of addresses: the entry table, low bytes)
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
;итого 256 байт (en: 256 bytes in total)
        DISPLAY /T,TEXTURERINC
; macro PATCHVUCODE expanded
; en: PATCHVUINC: writes the texture offsets of one scanline into the LD HL,nn operands, from the far end, 4 pixels
; en: per round: first the 16-bit running sum HL += DE (V), then after EXX the high byte of HL' += DE' (U) into the
; en: low byte; B counts the width up to 0 (INC B : RET Z). The IF lines skip the slots between the long and the
; en: short block (the cyclic shift by 1 & 1 tests the sign of a difference).
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
        ADD HL,DE               ;пропускаем первый (en: skip the first one)
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
        ADD HL,DE               ;пропускаем первый (en: skip the first one)
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
        DS low (-$)
; ---------------------------------------------------------------------------------------------------------------------
; en: DECSYSTEM: the same with DEC E (writes right to left) and PATCHVUDEC; comments as in INCSYSTEM
; ---------------------------------------------------------------------------------------------------------------------
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
; GSHEDGEGO: card set-up.
; 1. Texture: card page gspgdata (kishki) is mapped at #C000 and copied 8 times to #8000 of the pages
;    #06,#86,#07,#87 ... #89 (B steps through RLCA : INC A : RRCA), copy k with 32*k added to every byte (C wraps to 0
;    after the 8th): 8 brightness levels, chunk value = 32*level + texel.
; 2. The generators of instlpp.asm: GENDIV, GENPERSP, GENJTNROT (rotation), MKTRANSCH (chunk -> 16c converter).
; 3. The model KUB: vertices into the VTX3DI X/Y/Z tables from index #FF down (vertex n at low byte #FF-n, VTX3DBEG =
;    the lowest), so the vertex loops run L up to the wrap at #00; faces into TPOLYZ/TPOLYS.
; =====================================================================================================================
GSHEDGEGO
        LD A,gspgdata
        OUT (MPAGEX),A
        LD BC,+(((gspgtex&#FFFF)<<8|(gspgtex&#FFFF)>>>(16-8))&#FFFF)
GSTEX80 LD A,B
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        LD B,A
        LD HL,#C000
        LD DE,#8000
GSTEX81 LD A,(HL)
        ADD A,C
        LD (DE),A
        INC HL
        INC DE
        BIT 6,H
        JR NZ,GSTEX81
        LD A,C
        ADD A,32
        LD C,A
        JR NZ,GSTEX80
        CALL GENDIV
        CALL GENPERSP
        CALL GENJTNROT
        CALL MKTRANSCH

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

; en: the face list: TPOLYZ = Z sum (filled each frame), TPOLYS / TPOLYS+#100 = low / high byte of the face record
; en: address; the 0 after the last Z ends the list for SORTZ
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

; =====================================================================================================================
; GSLOOP: one card picture. gsdrawscr (self-modified) alternates the two screens. gstimer is the low byte of the ZX's
; timer, received with command 0: the angles advance once per elapsed ZX frame, at most twice per picture.
; =====================================================================================================================
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: drawing into SCR2)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now drawing into SCR1)
;потом дадим его ZX'у (en: later we give it to the ZX)
gstimer=$+1
        LD A,0
gsoldtimer=$+1
        LD C,0
        LD (gsoldtimer),A
        SUB C
        JR Z,GSTIMERACTIONQ
        CP 2
        JR C,$+4
        LD A,2
        LD B,A
; en: per frame: ALPHA += 1, BETA -= 1+201/256, GAMMA += 125/256. The fraction is accumulated in the operand of
; en: ADD A,0 (LD ($-1),A writes it back), its carry makes the extra step.
GSTIMERACTION
        LD HL,ALPHA
        INC (HL)
        LD HL,BETA
        DEC (HL)
        LD A,201
        ADD A,0
        LD ($-1),A
        JR NC,$+3               ;некратные угловые скорости (en: non-multiple angular speeds)
        DEC (HL)
        LD HL,GAMMA
        LD A,125
        ADD A,0
        LD ($-1),A
        JR NC,$+3               ;некратные угловые скорости (en: non-multiple angular speeds)
        INC (HL)
        DJNZ GSTIMERACTION
GSTIMERACTIONQ
; en: rotate all vertices (VTX3DI -> VTX3D), then project X and Y
        CALL JTNROTATE
        CALL PERSPCORR

; en: per face: TPOLYZ = ZA+ZB+ZC. The record starts with the three vertex addresses (in the Y table); D = the Z table
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
        LD A,(DE)               ;ZA
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
        LD (HL),A               ;сумма координат Z для вершин грани (en: sum of the Z coordinates of the face's vertices)
        INC L
        DJNZ COUNTZ0

        LD HL,TPOLYZ
        CALL SORTZ

; en: clear the chunk map: 64 rows (H = #00..#3F), bytes #00-#3F of each, 32 PUSHes of 0 per row with SP = row+#40
        LD (GSCLSP),SP
        LD HL,#0040
        LD DE,0
        LD B,64
GSCL0   LD SP,HL
        DUP 32
        PUSH DE
        EDUP
        INC H
        DJNZ GSCL0
GSCLSP=$+1
        LD SP,0

; en: draw the faces in sorted order: TRI (texpp4.asm), HL = face record. NPOLYS (self-modified at set-up) = 60
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

; en: convert the chunk map into the 16-colour screen being drawn: its page at #8000, the generated converter
; en: (gspgtransch) at #C000, JP transch (#E000); it POPs the map, so SP is saved, and it returns by JP (IX)
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
        LD E,15                 ;31 для 256X128 (en: 31 for 256x128)
        LD B,high TCH256
        EXX
        LD HL,gschmap256+32-parthgt
        LD B,16                 ;64 Y / 4 слоя (en: 64 Y / 4 layers)
        JP transch
GSLOOPTRANSQ
GSLOOPSP=$+1
        LD SP,0

        CALL WAITER0            ;GS ожидает конец кадра у ZX (en: the card waits for the end of the ZX's frame)
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        IN A,(ZXDATRD)          ; en: the ZX's timer low byte, sent with command 0
        LD (gstimer),A
;OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
;GS ожидает запроса данных (en: the card waits for the data request; a timeout resets the card)
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT (en: any command but 1 (the ZX sends 2): back to the card resident)

; en: command 1: point the DMA at the screen just converted and start it; the ZX now reads it
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: sign for the ZX "DMA ON")
        JP GSLOOP

; ---------------------------------------------------------------------------------------------------------------------
; en: PERSPCORR: projection of every vertex through the gspgpersp table at #C000: row = Z-perspdist (negative, so a
; en: row #C0..#FF), column = X or Y; the results replace X and Y in VTX3D (Y masked to the 64 chunk map rows)
; ---------------------------------------------------------------------------------------------------------------------
PERSPCORR
; en: the Z range could be widened at the expense of X,Y -
; en: if Z=L, X,Y=H (RRA:RRA:OR #C0)
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
;ADD A,'gschmap256
        AND #3F
        LD (HL),A
        DEC H
        LD E,(HL)               ;X
        LD A,(DE)
        LD (HL),A
        INC L
        JR NZ,PERSPCORR0
        RET

; ---------------------------------------------------------------------------------------------------------------------
; en: JTNROTATE: rotation of every vertex by three angles. ALPHA/BETA/GAMMA point into the SIGNOFS page (low byte =
; en: angle). Per angle the tables of GENJTNROT give: at SIGNOFS the low byte of the rotation routine in PROCROT
; en: (written into the CALL PROCROT operands _ALPCAL/_BETCAL/_GAMCAL), at SINOFS and COSOFS the pages of the
; en: multiplication tables for sin and cos (written into LD H / LD D). Each call rotates a pair C,B in one plane
; en: and returns A,B; the coordinates move round through A' between the calls. Then Z/8-perspdist, X/2, Y/2 go to
; en: VTX3D. Runs L from VTX3DBEG up to the wrap at #00.
; ---------------------------------------------------------------------------------------------------------------------
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
        SRA A
        SUB perspdist
        LD (_CZ),A
        LD A,B
        EXX
        SRA A
        LD (HL),A               ;X
        INC H
        EX AF,AF'
        SRA A
        LD (HL),A               ;Y
        INC H
_CZ=$+1
        LD (HL),0
        INC L
        INC IXL
        JR NZ,___R1
        RET

; ---------------------------------------------------------------------------------------------------------------------
; en: SORTZ: selection sort of the face list, swapping the Z sum together with the record address (+#100/+#200).
; en: A pass skips entries greater than the candidate, so it ends on the smallest nonzero unsigned sum and moves it
; en: to the front. The sums are negative (Z-perspdist), so the smallest unsigned one is the farthest face: it is
; en: drawn first (painter's order). A sum of exactly 0 would end the list early; with these sizes it cannot happen.
; ---------------------------------------------------------------------------------------------------------------------
SORTZ
; en: HL = list of Z (unsigned), 0 at the end; #FF
; en: the polygon addresses lie INC H further (ld e,(hl):inc h:ld d,(hl))
; en: as a result they come in descending Z
;HL=список Z (беззнаковых), в конце 0; #FF
;через INC H лежат адреса полигонов (ld e,(hl):inc h:ld d,(hl))
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
;DE=адрес максимума (en: DE = address of the maximum)
;обмениваем с текущим началом списка (en: swap it with the current head of the list)
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

; en: face record (KPOLY), 20 bytes, word aligned:
; en: address of vertex A ;Y,X,Z / -"- B / -"- C / VU of vertex A (0/4) / -"- B / -"- C (0/4) /
; en: #C300 ;to flip the texture use #F200 and swap the VU / texture number in the page (0/64/128/192, in both bytes) /
; en: TRIDRAW / texture page (in the high byte). The last word is now the "vnormal" vertex instead (";DW gspgtex+3"
; en: below): texpp4.asm turns its projected Y into the brightness level, i.e. the texture copy page.
;адрес вершины A ;Y,X,Z
;-"- B
;-"- C
;VU вершины A (0/4)
;-"- B
;-"- C (0/4)
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
; en: the model: an icosahedron, vertices (+-icod, +-icosz, 0) and cyclic; icod = icosz*158/256 (256/158 = 1.62,
; en: about the golden ratio)
icosz=23*2                      ;на 24 редко зашкаливает (en: at 24 it rarely goes off the scale)
;23 реальный GS за фрейм чуть-чуть не тянет (en: at 23 a real GS falls just short of keeping up within a frame)
icod=(icosz*158&#FFFF)/(256&#FFFF)
V0X=-icod
V0Y=-icosz
V0Z=0
V1X=icod
V1Y=-icosz
V1Z=0
V2X=icod
V2Y=icosz
V2Z=0
V3X=-icod
V3Y=icosz
V3Z=0

V4X=0
V4Y=-icod
V4Z=icosz
V5X=0
V5Y=icod
V5Z=icosz
V6X=0
V6Y=icod
V6Z=-icosz
V7X=0
V7Y=-icod
V7Z=-icosz

V8X=icosz
V8Y=0
V8Z=icod
V9X=icosz
V9Y=0
V9Z=-icod
V10X=-icosz
V10Y=0
V10Z=-icod
V11X=-icosz
V11Y=0
V11Z=icod

V0=0
V1=1
V2=2
V3=3
V4=4
V5=5
V6=6
V7=7
V8=8
V9=9
V10=10
V11=11

; en: KUB (the name of a cube kept): 32 vertices = the 12 icosahedron vertices V0-V11 and 20 points
; en: Va_b = Va + (Vb-Va)*k/m (k/m = 98/256). Each of the 20 points lies towards the centre of one icosahedron face,
; en: but deeper (radius ~31 against ~43 for the face centre and ~54 for a vertex), so the 12 vertices stick out as
; en: spikes: the hedgehog. 60 faces: a fan of 5 triangles around every icosahedron vertex (KPOL below).
KUB
        DB 12+20                ;вершин (en: vertices)
        DB V0X,V0Y,V0Z
        DB V1X,V1Y,V1Z
        DB V2X,V2Y,V2Z
        DB V3X,V3Y,V3Z
        DB V4X,V4Y,V4Z
        DB V5X,V5Y,V5Z
        DB V6X,V6Y,V6Z
        DB V7X,V7Y,V7Z
        DB V8X,V8Y,V8Z
        DB V9X,V9Y,V9Z
        DB V10X,V10Y,V10Z
        DB V11X,V11Y,V11Z
k=98
L_m=256                         ; en: ALASM's label `m`, renamed by the converter (reserved in sjasmplus)
_=12
; en: DOTV0_1 a,b: Va_b = the next vertex number, then its X,Y,Z. ALASM's unsigned 16-bit division gives the right
; en: byte (mod 256) for negative differences too; where the sum exceeds 255 the converter added `low`.
; (expanded at its calls)         MACRO DOTV0_1
; (expanded at its calls) V\0_\1=_
; (expanded at its calls) _=_+1
; (expanded at its calls) DB V\1X-V\0X*k/m+V\0X,V\1Y-V\0Y*k/m+V\0Y,V\1Z-V\0Z*k/m+V\0Z
; (expanded at its calls)         ENDM
; macro DOTV0_1 expanded
V0_8=_
_=_+1
        DB ((V8X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V8Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V8Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V1_5=_
_=_+1
        DB low (((V5X-V1X)*k&#FFFF)/(L_m&#FFFF)+V1X),low (((V5Y-V1Y)*k&#FFFF)/(L_m&#FFFF)+V1Y),low (((V5Z-V1Z)*k&#FFFF)/(L_m&#FFFF)+V1Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V0_9=_
_=_+1
        DB ((V9X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V9Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V9Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V1_6=_
_=_+1
        DB low (((V6X-V1X)*k&#FFFF)/(L_m&#FFFF)+V1X),low (((V6Y-V1Y)*k&#FFFF)/(L_m&#FFFF)+V1Y),low (((V6Z-V1Z)*k&#FFFF)/(L_m&#FFFF)+V1Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V1_2=_
_=_+1
        DB ((V2X-V1X)*k&#FFFF)/(L_m&#FFFF)+V1X,((V2Y-V1Y)*k&#FFFF)/(L_m&#FFFF)+V1Y,((V2Z-V1Z)*k&#FFFF)/(L_m&#FFFF)+V1Z
; macro DOTV0_1 expanded
V0_6=_
_=_+1
        DB ((V6X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V6Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V6Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V0_5=_
_=_+1
        DB ((V5X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V5Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V5Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V0_3=_
_=_+1
        DB ((V3X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V3Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V3Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V2_1=_
_=_+1
        DB low (((V1X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X),low (((V1Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y),low (((V1Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V2_4=_
_=_+1
        DB low (((V4X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X),low (((V4Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y),low (((V4Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V2_7=_
_=_+1
        DB low (((V7X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X),low (((V7Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y),low (((V7Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V2_10=_
_=_+1
        DB low (((V10X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X),low (((V10Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y),low (((V10Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V2_11=_
_=_+1
        DB low (((V11X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X),low (((V11Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y),low (((V11Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V3_0=_
_=_+1
        DB low (((V0X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X),low (((V0Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y),low (((V0Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V3_4=_
_=_+1
        DB low (((V4X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X),low (((V4Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y),low (((V4Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V3_7=_
_=_+1
        DB low (((V7X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X),low (((V7Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y),low (((V7Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V4_2=_
_=_+1
        DB low (((V2X-V4X)*k&#FFFF)/(L_m&#FFFF)+V4X),low (((V2Y-V4Y)*k&#FFFF)/(L_m&#FFFF)+V4Y),low (((V2Z-V4Z)*k&#FFFF)/(L_m&#FFFF)+V4Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V4_3=_
_=_+1
        DB low (((V3X-V4X)*k&#FFFF)/(L_m&#FFFF)+V4X),low (((V3Y-V4Y)*k&#FFFF)/(L_m&#FFFF)+V4Y),low (((V3Z-V4Z)*k&#FFFF)/(L_m&#FFFF)+V4Z) ; ALASM keeps the low byte
; macro DOTV0_1 expanded
V6_0=_
_=_+1
        DB ((V0X-V6X)*k&#FFFF)/(L_m&#FFFF)+V6X,((V0Y-V6Y)*k&#FFFF)/(L_m&#FFFF)+V6Y,((V0Z-V6Z)*k&#FFFF)/(L_m&#FFFF)+V6Z
; macro DOTV0_1 expanded
V6_1=_
_=_+1
        DB ((V1X-V6X)*k&#FFFF)/(L_m&#FFFF)+V6X,((V1Y-V6Y)*k&#FFFF)/(L_m&#FFFF)+V6Y,((V1Z-V6Z)*k&#FFFF)/(L_m&#FFFF)+V6Z

        DB 12*5                 ;граней (en: faces)
; en: INSFR n: the addresses of the 5 face records of fan n
; (expanded at its calls)         MACRO INSFR
; (expanded at its calls)         DW KPOLY\0A
; (expanded at its calls)         DW KPOLY\0B
; (expanded at its calls)         DW KPOLY\0C
; (expanded at its calls)         DW KPOLY\0D
; (expanded at its calls)         DW KPOLY\0E
; (expanded at its calls)         ENDM
; macro INSFR expanded
        DW KPOLY0A
        DW KPOLY0B
        DW KPOLY0C
        DW KPOLY0D
        DW KPOLY0E
; macro INSFR expanded
        DW KPOLY1A
        DW KPOLY1B
        DW KPOLY1C
        DW KPOLY1D
        DW KPOLY1E
; macro INSFR expanded
        DW KPOLY2A
        DW KPOLY2B
        DW KPOLY2C
        DW KPOLY2D
        DW KPOLY2E
; macro INSFR expanded
        DW KPOLY3A
        DW KPOLY3B
        DW KPOLY3C
        DW KPOLY3D
        DW KPOLY3E
; macro INSFR expanded
        DW KPOLY4A
        DW KPOLY4B
        DW KPOLY4C
        DW KPOLY4D
        DW KPOLY4E
; macro INSFR expanded
        DW KPOLY5A
        DW KPOLY5B
        DW KPOLY5C
        DW KPOLY5D
        DW KPOLY5E
; macro INSFR expanded
        DW KPOLY6A
        DW KPOLY6B
        DW KPOLY6C
        DW KPOLY6D
        DW KPOLY6E
; macro INSFR expanded
        DW KPOLY7A
        DW KPOLY7B
        DW KPOLY7C
        DW KPOLY7D
        DW KPOLY7E
; macro INSFR expanded
        DW KPOLY8A
        DW KPOLY8B
        DW KPOLY8C
        DW KPOLY8D
        DW KPOLY8E
; macro INSFR expanded
        DW KPOLY9A
        DW KPOLY9B
        DW KPOLY9C
        DW KPOLY9D
        DW KPOLY9E
; macro INSFR expanded
        DW KPOLY10A
        DW KPOLY10B
        DW KPOLY10C
        DW KPOLY10D
        DW KPOLY10E
; macro INSFR expanded
        DW KPOLY11A
        DW KPOLY11B
        DW KPOLY11C
        DW KPOLY11D
        DW KPOLY11E

; en: KPOLY name,A,B,C,tex,VUa,VUb,VUc,flip,vnormal: one face record (format above). KPOL n,r1..r5,n1..n5: the fan of 5
; en: faces around icosahedron vertex n over the ring r1..r5 of face points; the vnormal of face i is the neighbouring
; en: icosahedron vertex n_i. The fifth one is taken through `_` (\P, \9\R in ALASM: the lines `_=n` / `_=x` below).
; (expanded at its calls)         MACRO KPOLY
; (expanded at its calls) KPOLY\0
; (expanded at its calls)         DW #FF-\1+VTX3DY
; (expanded at its calls)         DW #FF-\2+VTX3DY
; (expanded at its calls)         DW #FF-\3+VTX3DY
; (expanded at its calls)         DW \5,\6,\7
; (expanded at its calls)         DW \8
; (expanded at its calls)         DW \4+texborder*257
; (expanded at its calls)         DW TRIDRAW
; (expanded at its calls)        ;DW gspgtex+3
; (expanded at its calls)         DW #FF-\9+VTX3DY
; (expanded at its calls)         ENDM

; (expanded at its calls)         MACRO KPOL
; (expanded at its calls)         KPOLY \0A,\0,\1,\2,#00,#0004,#0000,#0400,#F200,\6
; (expanded at its calls)         KPOLY \0B,\0,\2,\3,#00,#0004,#0000,#0400,#F200,\7
; (expanded at its calls)         KPOLY \0C,\0,\3,\4,#00,#0004,#0000,#0400,#F200,\8
; (expanded at its calls)         KPOLY \0D,\0,\4,\5,#00,#0004,#0000,#0400,#F200,\9
; (expanded at its calls) _=\P
; (expanded at its calls) _=\9\R
; (expanded at its calls)         KPOLY \0E,\0,\5,\1,#00,#0004,#0000,#0400,#F200,_
; (expanded at its calls)         ENDM
; en: word-align the records
        DS $&1
; macro KPOL expanded
; macro KPOLY expanded
KPOLY0A
        DW #FF-0+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-1+VTX3DY
; macro KPOLY expanded
KPOLY0B
        DW #FF-0+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-4+VTX3DY
; macro KPOLY expanded
KPOLY0C
        DW #FF-0+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-11+VTX3DY
; macro KPOLY expanded
KPOLY0D
        DW #FF-0+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-10+VTX3DY
_=0
_=7
; macro KPOLY expanded
KPOLY0E
        DW #FF-0+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded
; macro KPOLY expanded
KPOLY1A
        DW #FF-1+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-0+VTX3DY
; macro KPOLY expanded
KPOLY1B
        DW #FF-1+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-7+VTX3DY
; macro KPOLY expanded
KPOLY1C
        DW #FF-1+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-9+VTX3DY
; macro KPOLY expanded
KPOLY1D
        DW #FF-1+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-8+VTX3DY
_=1
_=4
; macro KPOLY expanded
KPOLY1E
        DW #FF-1+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded;!
; macro KPOLY expanded
KPOLY2A
        DW #FF-2+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-3+VTX3DY
; macro KPOLY expanded
KPOLY2B
        DW #FF-2+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-5+VTX3DY
; macro KPOLY expanded
KPOLY2C
        DW #FF-2+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-8+VTX3DY
; macro KPOLY expanded
KPOLY2D
        DW #FF-2+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-9+VTX3DY
_=2
_=6
; macro KPOLY expanded
KPOLY2E
        DW #FF-2+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded;!
; macro KPOLY expanded
KPOLY3A
        DW #FF-3+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-2+VTX3DY
; macro KPOLY expanded
KPOLY3B
        DW #FF-3+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-6+VTX3DY
; macro KPOLY expanded
KPOLY3C
        DW #FF-3+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-10+VTX3DY
; macro KPOLY expanded
KPOLY3D
        DW #FF-3+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-11+VTX3DY
_=3
_=5
; macro KPOLY expanded
KPOLY3E
        DW #FF-3+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY

; macro KPOL expanded
; macro KPOLY expanded
KPOLY4A
        DW #FF-4+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-0+VTX3DY
; macro KPOLY expanded
KPOLY4B
        DW #FF-4+VTX3DY
        DW #FF-V0_8+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-1+VTX3DY
; macro KPOLY expanded
KPOLY4C
        DW #FF-4+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-8+VTX3DY
; macro KPOLY expanded
KPOLY4D
        DW #FF-4+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-5+VTX3DY
_=4
_=11
; macro KPOLY expanded
KPOLY4E
        DW #FF-4+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded
; macro KPOLY expanded
KPOLY5A
        DW #FF-5+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-4+VTX3DY
; macro KPOLY expanded
KPOLY5B
        DW #FF-5+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-8+VTX3DY
; macro KPOLY expanded
KPOLY5C
        DW #FF-5+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-2+VTX3DY
; macro KPOLY expanded
KPOLY5D
        DW #FF-5+VTX3DY
        DW #FF-V2_11+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-3+VTX3DY
_=5
_=11
; macro KPOLY expanded
KPOLY5E
        DW #FF-5+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded
; macro KPOLY expanded
KPOLY6A
        DW #FF-6+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-2+VTX3DY
; macro KPOLY expanded
KPOLY6B
        DW #FF-6+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-9+VTX3DY
; macro KPOLY expanded
KPOLY6C
        DW #FF-6+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-7+VTX3DY
; macro KPOLY expanded
KPOLY6D
        DW #FF-6+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-10+VTX3DY
_=6
_=3
; macro KPOLY expanded
KPOLY6E
        DW #FF-6+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #FF-V2_10+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded
; macro KPOLY expanded
KPOLY7A
        DW #FF-7+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-6+VTX3DY
; macro KPOLY expanded
KPOLY7B
        DW #FF-7+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-9+VTX3DY
; macro KPOLY expanded
KPOLY7C
        DW #FF-7+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-1+VTX3DY
; macro KPOLY expanded
KPOLY7D
        DW #FF-7+VTX3DY
        DW #FF-V0_9+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-0+VTX3DY
_=7
_=10
; macro KPOLY expanded
KPOLY7E
        DW #FF-7+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY

; macro KPOL expanded
; macro KPOLY expanded
KPOLY8A
        DW #FF-8+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-4+VTX3DY
; macro KPOLY expanded
KPOLY8B
        DW #FF-8+VTX3DY
        DW #FF-V1_5+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-1+VTX3DY
; macro KPOLY expanded
KPOLY8C
        DW #FF-8+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-9+VTX3DY
; macro KPOLY expanded
KPOLY8D
        DW #FF-8+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-2+VTX3DY
_=8
_=5
; macro KPOLY expanded
KPOLY8E
        DW #FF-8+VTX3DY
        DW #FF-V2_4+VTX3DY
        DW #FF-V4_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded
; macro KPOLY expanded
KPOLY9A
        DW #FF-9+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-8+VTX3DY
; macro KPOLY expanded
KPOLY9B
        DW #FF-9+VTX3DY
        DW #FF-V1_2+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-1+VTX3DY
; macro KPOLY expanded
KPOLY9C
        DW #FF-9+VTX3DY
        DW #FF-V1_6+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-7+VTX3DY
; macro KPOLY expanded
KPOLY9D
        DW #FF-9+VTX3DY
        DW #FF-V6_1+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-6+VTX3DY
_=9
_=2
; macro KPOLY expanded
KPOLY9E
        DW #FF-9+VTX3DY
        DW #FF-V2_7+VTX3DY
        DW #FF-V2_1+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; macro KPOL expanded;!
; macro KPOLY expanded
KPOLY10A
        DW #FF-10+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-6+VTX3DY
; macro KPOLY expanded
KPOLY10B
        DW #FF-10+VTX3DY
        DW #FF-V6_0+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-7+VTX3DY
; macro KPOLY expanded
KPOLY10C
        DW #FF-10+VTX3DY
        DW #FF-V0_6+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-0+VTX3DY
; macro KPOLY expanded
KPOLY10D
        DW #FF-10+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-11+VTX3DY
_=10
_=3
; macro KPOLY expanded
KPOLY10E
        DW #FF-10+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #FF-V3_7+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY
; en: two of the author's test variants of fan 10, vnormal 100 (no such vertex), marked "bug"; commented out
;KPOL 10,V3_7,V6_0,V0_6,V0_3,V3_0,100,100,100,100,3 ;!баг
;KPOL 10,V3_7,V6_0,V0_6,V0_3,V3_0,100,100,100,11,100;!баг
; macro KPOL expanded
; macro KPOLY expanded
KPOLY11A
        DW #FF-11+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-10+VTX3DY
; macro KPOLY expanded
KPOLY11B
        DW #FF-11+VTX3DY
        DW #FF-V0_3+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-0+VTX3DY
; macro KPOLY expanded
KPOLY11C
        DW #FF-11+VTX3DY
        DW #FF-V0_5+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-4+VTX3DY
; macro KPOLY expanded
KPOLY11D
        DW #FF-11+VTX3DY
        DW #FF-V4_3+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-5+VTX3DY
_=11
_=3
; macro KPOLY expanded
KPOLY11E
        DW #FF-11+VTX3DY
        DW #FF-V3_4+VTX3DY
        DW #FF-V3_0+VTX3DY
        DW #0004,#0000,#0400
        DW #F200
        DW (#00+texborder)*257
        DW TRIDRAW
;DW gspgtex+3
        DW #FF-_+VTX3DY

; en: the angles, as pointers SIGNOFS+angle (ALPHA starts at #40 = 90 degrees)
ALPHA   DW SIGNOFS+#40
BETA    DW SIGNOFS+#00
GAMMA   DW SIGNOFS+#00

; en: TRI / TRIDRAW: the triangle texture mapper into the chunk map (shared with TEX28.asm)
        INCLUDE "texpp4.asm"

; en: GSRESET (on a command timeout): DMA off, 10 MHz, ROM back at #0000, EXPAG off, RST 0: the card restarts its ROM
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

        INCLUDE "vujump.asm"

; en: DISPLAY: end of the code without the installers
        DISPLAY 'без инсталлеров конец кода=',$
;NGS инсталлеры - можно затирать (en: NGS installers - may be overwritten, they run once at set-up)
        INCLUDE "instlpp.asm"
; en: palbrick: 4 colours (0..15) per chunk value, turned by MKCH2X256 (instlpp.asm) into the TCH256 tables
pal256
        INCBIN "palbrick"
; en: DISPLAY: end of the code with the installers
        DISPLAY 'с инсталлерами конец кода=',$
        ENT
LENGSHEDGECODE=$-WASGSHEDGECODE
        DISPLAY /T,LENGSHEDGECODE
LENHEDGEGS=$-#C000

; =====================================================================================================================
; HEDGEZX1: ZX half 1. The resident CALLs #C000 (BC/DE = start/end frame, not used): MOVEPAGE copies DEMON16 (#3800
; bytes) to #C000 of #7FFD page #16 (RAM 6, part of the 16-colour screen 1) and RETs to the resident, which goes on
; to the next page, HEDGEZX2.
; =====================================================================================================================
        ORG #C000,pgeffects
pghedgezx1=pgeffects
        MOVEPAGE #16,DEMON16,#3800,#C000
DEMON16
        INCBIN "DEMON16.C",0,#3800
LENHEDGEZX1=$-#C000

; =====================================================================================================================
; HEDGEZX2: ZX half 2. Keeps BC (start frame) and DE (end frame) on the stack, copies HEDGECODE to #7800, pushes
; #7800 as a return address and lets MOVEPAGE copy DEMON17 to #C000 of page #17 (RAM 7): its RET enters HEDGECODE.
; =====================================================================================================================
        ORG #C000,pgeffects+1
pghedgezx2=pgeffects+1
        PUSH BC
        PUSH DE
        LD HL,WASHEDGECODE
        LD DE,HEDGECODE
        PUSH DE
        LD BC,LENHEDGECODE
        LDIR
        MOVEPAGE #17,DEMON17,#3800,#C000

; ---------------------------------------------------------------------------------------------------------------------
; en: HEDGECODE at #7800: end frame -> zxendtime; 16-colour mode on (Pentagon port #EFF7 bit 0); page 7 with screen 1
; en: shown; screen 1 copied to screen 0 (both hold the picture; the code's 16-colour screen 0 is RAM 4+5, screen 1
; en: RAM 6+7, pixel data at #C000-#D7FF and #E000-#F7FF of each); OUT16C generated; wait for the start frame.
; ---------------------------------------------------------------------------------------------------------------------
WASHEDGECODE
        DISP HEDGECODE
        POP HL
        LD (zxendtime),HL
        HALT
        LD A,1                  ;16C
        LD BC,#EFF7
        OUT (C),A

        LD A,#1F
        setpga
        CALL SCR1TOSCR0

        LD HL,OUT16C
        CALL MKOUT16C

        POP BC
; en: wait until timer >= the start frame (BC)
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

; ---------------------------------------------------------------------------------------------------------------------
; en: BIGLOOP: one ZX frame. No HALT: the pace is set by the card's answers.
; ---------------------------------------------------------------------------------------------------------------------
BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
        LD A,(timer)
        OUT (GSDAT),A           ; en: the timer's low byte for the card (GSLOOP's gstimer)
; en: tell the card that the frame is ready
; en: and the DMA must be switched off:
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

; en: flip the screens: A alternates #17 / #1D (LD ($-1),A rewrites the XOR operand); VIEWPG1 = A, VIEWPG0 = A-1:
; en: the two pages of the hidden screen (7,6 or 5,4) for OUT16C
        LD A,#17^#1D
        XOR #1D                 ;видим scr1, рисуем scr0 (en: we see scr1, draw scr0)
        LD ($-1),A              ;теперь видим scr0, рисуем scr1 (en: now we see scr0, draw scr1)
        LD (VIEWPG1),A
        DEC A
        LD (VIEWPG0),A
        setpga

;и ждем выключения: (en: and wait until it is off:)
;WN
;GD
        WD                      ;пока не примет (en: until the card takes the byte)
;
; en: leave at the end frame or on SPACE
zxendtime=$+1
        LD DE,0
        LD HL,(timer)
        OR A
        SBC HL,DE
        JR NC,QUIT
        LD A,#7F
        IN A,(#FE)
        RRA                     ;space
        JR NC,QUIT              ;можно жать RESET (en: one may press RESET)
;даем знать GS'у, что надо включить DMA (en: tell the card to switch the DMA on)
        LD A,1
        OUT (GSCOM),A
;и ждем включения: (en: and wait until it is on:)
        WN
        GD
;
;DI
; en: blit the picture: OUT16C runs with SP inside the screen and comes back by JP (IX); H, L = the pages of the
; en: hidden screen; BC is still #7FFD from setpga; any address in the ROM area answers with the next DMA byte
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1617
;LD A,(HL) ;первый байт неверен (попадет в D) (en: the first byte is wrong (would go to D))
        JP OUT16C
BIGLOOPVIEWQ
BIGLOOPSP=$+1
        LD SP,0
;EI
        IF border
        LD A,7
        OUT (254),A
        ENDIF
        JP BIGLOOP
; en: QUIT: command 2 (the card's GSLOOP returns), then page #10 (RAM 0), black attributes at #5800, 16-colour mode
; en: off, RET to the resident (next part)
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

; ---------------------------------------------------------------------------------------------------------------------
; en: MKOUT16C: generates the blit at HL (OUT16C): parthgt (28) rows, bottom up from #D7E0. A row is `LD A,H`, then the
; en: body `LD (curpg),A : OUT (C),A`, 8 segments, `CP H : LD A,L : JP Z,body`: the body runs twice, for page H and
; en: then for page L. The 8 segments are the 4 screen quarters (parthgt lines apart) in the #C000 half and in the
; en: #E000 half. A segment: `LD SP,base+14`, 8 x `LD D,(HL) : LD E,(HL)` (DMA reads) with 7 `PUSH DE` after all but
; en: the first pair, then `LD (base),DE`: 16 bytes read, 14 stored. With mirrorchunks the lines 2,3 and 6,7 of a
; en: character cell swap places (bit 0 of D flipped when bit 1 is set). The code ends with JP (IX).
; ---------------------------------------------------------------------------------------------------------------------
; en: copy the linear 16C buffer from NGS to the screen
; en: bottom up
; en: the interrupt handler must put DE on the stack instead of the return
; en: address
;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
MKOUT16C
;LD A,H
; en: the plan (comments): per line select the page, then for bottom, almost bottom, almost top, top: LD SP, 8 x
; en: read a pair + PUSH; the same +#2000; then CP H : LD A,L : JP Z,LOOP
;LOOP
;LD (curpg),A
;OUT (C),A
;LD SP,низ
;DUP 8
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;LD SP,почти низ
;DUP 8
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;LD SP,почти верх
;DUP 8
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;LD SP,верх
;DUP 8
;LD D,(HL),E,(HL)
;PUSH DE
;EDUP
;LD SP,низ+#2000 и т.п. +#2000
;...
;CP H
;LD A,L
;JP Z,LOOP
; en: for 8 lines in #C080..#C780, 7 lines #C900..#CF00,
; en: 7 lines #D100..#D700 an LD (..),DE is needed instead of the last PUSH
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD DE,#D7E0
        LD B,parthgt            ;32 ;высота 32*4;при 30 эмуль тормозит редко (en: height 32*4; at 30 the emulator rarely slows down)
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
        CALL MKOUT16C0_2
;LD A,D
;SUB 8
;LD D,A
        CALL MKOUT16C0_2
        POP DE
        SET 5,D
        CALL MKOUT16C0_2
;LD A,D
;SUB 8
;LD D,A
        CALL MKOUT16C0_2
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
        CALL UDE
        POP BC
        DJNZ MKOUT16CLINE
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)
        RET

; en: UDE: DE one pixel line up in the ZX screen layout
UDE
        LD A,D
        DEC D
        AND 7
        RET NZ
        LD A,E
        SUB 32
        LD E,A
        RET C
        LD A,D
        ADD A,8
        LD D,A
        RET

; en: two segments: CALL MKOUT16C0, then fall into it; each MKOUT16C0 ends by moving DE parthgt lines up
MKOUT16C0_2
        CALL MKOUT16C0
;PUSH DE
; LD A,E
; SUB #80
; LD E,A
; CALL MKOUT16C0
;POP DE
; RET

; en: one segment (see MKOUT16C); the ";16" and ";8" are the older values (16 bytes stored per segment)
MKOUT16C0
        LD (HL),49
        INC HL
        LD A,E
        ADD A,14                ;16
        LD (HL),A
        ADC A,D
        SUB (HL)
        INC HL
        LD (HL),A
        INC HL
        LD (HL),#56             ;d,(hl)
        INC HL
        LD (HL),#5E             ;e,(hl)
        LD B,7                  ;8
MKOUT16C1
        INC HL
        LD (HL),#56             ;d,(hl)
        INC HL
        LD (HL),#5E             ;e,(hl)
        INC HL
        LD (HL),#D5             ;push de
        DJNZ MKOUT16C1
; en: for 8 lines in #C080..#C780, 7 lines #C900..#CF00,
; en: 7 lines #D100..#D700 an LD (..),DE is needed instead of the last PUSH
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
;LD A,E
;ADD A,A
;JNZ MKOUT16CnPROTECT
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
;RET
MKOUTUDEPART
        LD B,parthgt            ;32
        CALL UDE
        DJNZ $-3
        RET

; en: SCR1TOSCR0: RAM 7 -> 5 and 6 -> 4 (#7FFD #1F -> #1D, #1E -> #1C), #1800 bytes at #C000 and at #E000 each,
; en: by RESMOVEPAGE
SCR1TOSCR0
        LD IX,#1F1D
        CALL SCR1TOPP
        LD IX,#1E1C
SCR1TOPP
        LD HL,#C000
        CALL SCR1TOPPP
        LD HL,#E000
SCR1TOPPP
        LD D,H
        LD E,L
        LD IYH,#18
        JP RESMOVEPAGE
; en: OUT16C: the generated blit goes here, right after HEDGECODE
OUT16C
        ENT
LENHEDGECODE=$-WASHEDGECODE
; en: the second picture half: after HEDGECODE in the page, not part of the #7800 copy
DEMON17
        INCBIN "DEMON17.C",0,#3800
LENHEDGEZX2=$-#C000

; en: the test harness's next effect page: JR $ (stop after the hedgehog); not saved
        ORG #C000,pgeffects+2
        JR $

; en: the music for the stand-alone run; not saved
        ORG #C000,pgmusic
        INCBIN "thelinkm"

; en: ObjTab for SAVEOBJ4 (ALASM): name and type, start, length, page, address of each object. In ALASM the author
; en: RAN the unit and SAVEOBJ4 wrote the objects; here the SAVEBIN block below does it.
        ORG end
ObjTab
        DB 'HEDGEZX1C'
        DW #C000
        DW LENHEDGEZX1
        DB pghedgezx1
        DW #C000

        DB 'HEDGEZX2C'
        DW #C000
        DW LENHEDGEZX2
        DB pghedgezx2
        DW #C000

        DB 'HEDGEGS C'
        DW #C000
        DW LENHEDGEGS
        DB pghedgegs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
        SLOT 3
        PAGE pghedgezx1
        SAVEBIN "HEDGEZX1",#C000,LENHEDGEZX1
        PAGE pghedgezx2
        SAVEBIN "HEDGEZX2",#C000,LENHEDGEZX2
        PAGE pghedgegs
        SAVEBIN "HEDGEGS",#C000,LENHEDGEGS
