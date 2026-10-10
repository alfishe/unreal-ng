; =====================================================================================================================
; THELINK.asm - the main module of "The Link" (Alone Coder, 2009; Pentagon 1024 + NeoGS)
; =====================================================================================================================
;
; What it is
;   The demo's "linker in assembler clothing". Nothing of the effects is compiled here: every effect was built
;   earlier from its own source (GSTUNNE4, GSROTAT7, ...) into a ZX half (*ZX) and a card half (*GS). This file
;     1. assembles one block of code and data at #7800 (begin..end): GO, its helpers, the blocks GO copies to
;        their run-time addresses (card program, ZX resident loop, IM 2 handler, GSRESET, credits) and the intro
;        picture "medieval";
;     2. places every effect object and data file into its RAM page: `ORG #C000,<#7FFD value>` + INCBIN, a plain
;        copy without relocation (page plan below);
;     3. at the end puts the name 'THE LINKB' at #5CDD and includes m2hrust (the author's Hrust packer).
;
; What is built (sjasmplus)
;   Only the main block is saved: SAVEBIN "thelink" (#7800..#B154, 14677 bytes). build.py checks its sha256
;   against what ALASM assembled. The page images and m2hrust are only in the device memory, not saved: the
;   release loader (../loader/loader.asm) loads the page files from the disk itself and then jumps to GO.
;   On the author's working disk ALASM assembled all of this at every start and then ran it.
;
; Memory at run time (ZX side)
;   #4000-#5AFF  screen 0 (the intro picture, then the effects)
;   #5F00        outimstack: the IM 2 handler's own stack (grows down)
;   #6000        GO's stack (SP), grows down
;   #7800        GO (this block); free for the effects' ZX halves once the resident loop runs
;   #9655        wasmedieval: the intro picture (6912 bytes)
;   #BD00        resbuf: 256-byte buffer of RESMOVEPAGE
;   #BE00-#BF00  IM 2 vector table (257 bytes of #BF -> handler at #BFBF)
;   #BF01        curpg: the #7FFD value in use (the IM 2 handler restores it)
;   #BF02        timer: frame counter, +1 per interrupt (50 per second)
;   #BF04        ZXRESIDENT: the resident loop over the effect pages (+ RESMOVEPAGE, zxtimings)
;   #BFBF        IMER: the IM 2 handler (music player call + frame counter)
;   #C000-#FFFF  the paged RAM: effect halves, music, credits (see the page plan)
;
; Card side (NeoGS)
;   #5830        GSPROG: the card program GSPROGGO (stack below it, GSSTACK = #5830)
;   card pages #20.. (gspgdata): the 10 data pages, uploaded by ZX-DMA (command 3)
;   card pages #10.. (gspgcode): the 10 code pages, uploaded by ZX-DMA (command 4); the card's resident loop
;                runs them one after another at #C000 (MPAGEX)
;
; Entry point: GO (#7800). It expects IM 1 with a ROM at #0038 (first EI : HALT), 1024K paging (#EFF7 bit 2 = 0)
; and a NeoGS; everything else it sets up itself. It never returns: the demo ends on the credits screen (JR $).
;
; Converter additions (unreal-asm, ALASM -> sjasmplus): the DEVICE line, `INCLUDE "gsports.asm"`, `low`/`high`
; for ALASM's `.`/`'`, MOVEPAGE's named macro arguments and the `&#FFFF` masks, `DS (-$&#FFFF)` for ALASM's
; `DS -$`, and the SAVEBIN at the end. Comments starting with "en:" are English translations added to the
; author's Russian comments (which are kept as they are).
; =====================================================================================================================

        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device

; ---------------------------------------------------------------------------------------------------------------------
; Page plan: #7FFD values (Pentagon 1024: RAM page = (v & 7) | ((v & #C0) >> 3) | (v & #20); bit 4 = 48K ROM,
; set in every value so TR-DOS stays callable). The physical RAM page of each value is in ../loader/README.md.
;   pgdata1..10  data for the card (pictures, textures), sent by ZX-DMA to card pages #20..
;   pgcode1..10  card code (precalcs and the *GS halves), sent to card pages #10..; pgcode10 gets GSRESET at run time
;   pgeffects    #B0..#B7: the ZX halves of the effects, run in this order by the resident loop; then #D0 (credits)
;   pgmusic      the TurboSound FM music "thelinkm": init #C000, play #C005 (called by the IM 2 handler)
; ---------------------------------------------------------------------------------------------------------------------
;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
pgdata6=#96
pgdata7=#97
pgdata8=#11
pgdata9=#13
pgdata10=#14

pgcode1=#52
pgcode2=#55
pgcode3=#30
pgcode4=#31
pgcode5=#32
pgcode6=#33
pgcode7=#34
pgcode8=#35
pgcode9=#36
pgcode10=#37

;ZX data:
pgmusic=#54
pgeffects=#B0                   ;#B0..#B7, #D0

; Fixed addresses shared with the effects' ZX halves (their sources repeat these definitions)
resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports.asm"
; Card memory: the card program runs at #5830 in the card's RAM; its stack grows down from the same address
GSPROG=#5830
GSSTACK=#5830

; First card page of the code and of the data uploaded by ZX-DMA (card page numbers, as written to MPAGEX)
gspgcode=#10
gspgdata=#20

; setpga: select the #7FFD value in A at #C000 and remember it in curpg. curpg is written first, so an
; interrupt between the two restores the new value anyway. Destroys BC.
        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; GO - the demo's start (#7800)
; =====================================================================================================================
        ORG #7800
begin
GO
; --- 1. Own stack, wait one frame (still IM 1), black border, black attributes on screen 0 ---------------------------
        LD SP,#6000
        EI
        HALT
        XOR A
        OUT (#FE),A
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
; en: note - this `LD A,#17` is dead: A is reloaded below before any use and no OUT selects page 7. An
; `OUT (#7FFD)` for screen 1 (page 7, attributes at #D800) looks missing. As written, the LDIR zeroes
; #D800-#DAFF of whatever page is at #C000 at this moment (after the release loader: the music page #54,
; i.e. thelinkm bytes #1800-#1AFF, which are not zero in the file).
        LD A,#17
        LD HL,#D800
        LD DE,#D801
        LD BC,767
        LD (HL),L
        LDIR

; --- 2. Copy the run-time blocks into their pages: GSRESET -> #C000 of pgcode10 (the card's 10th code page),
;        credits -> #C000 of page #D0 (the ZX resident loop's last "effect") -------------------------------------------
        LD A,pgcode10
        setpga
        LD HL,WASGSRESET
        LD DE,#C000
        LD BC,LENGSRESET
        LDIR
        LD A,pgeffects+#20
        setpga
        LD HL,WASCREDITS
        LD DE,#C000
        LD BC,LENCREDITS
        LDIR

; --- 3. Hard reset of the card (GSCTR = #80) and two frames for its firmware to start --------------------------------
        LD A,C_GRST
        OUT (GSCTR),A
        XOR A
        OUT (GSCOM),A           ;не помогает (en: does not help)
        HALT
        HALT
        DI
; --- 4. A few standard GS commands the author sends without knowing why they are needed ("зачем-то") ------------------
; SD/SC/WC/WD/WN/GD (gsports.asm): SD = data to GSDAT, SC = command to GSCOM, WC = wait until the card took the
; command, WD = wait until it took the data, WN = wait for data from the card, GD = read it.
;зачем-то POKE #4092,0
; en: for some reason POKE #4092,0 (#18 = set the card address: low byte with the command, high byte after;
; #19 = write the data byte there)
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
; en: for some reason #38 (Load FX)
        SC #38
        WC
;зачем-то #D2 (Close Stream)
; en: for some reason #D2 (Close Stream)
        SC #D2
        WC

; --- 5. Upload the card program GSPROGGO to card address GSPROG (#5830), byte by byte with #18/#19 -----------------
        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER
; en: as in RIFF TRACKER (the author took the upload method from it)
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
; en: for some reason #F5 (Busy on)
        SC #F5
        WC
;#13 Jump to Address
; en: the card starts GSPROGGO; from here on the card runs the demo's program, not the firmware's command loop.
; Commands 3/4/5 below go straight to GSCOM: GSPROGGO polls ZXCMD for them and answers through ZXDATWR.
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

; --- 6. ZX-DMA upload. Command 3: the card points ZX-DMA at card page #20 and starts it. Every byte the ZX then
;        writes into #0000-#3FFF (its ROM area: no effect on the ZX) goes into the card's RAM ------------------------------
        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
;и ждем включения:
; en: and wait until it is on (the card answers through its data register):
        WN
        GD
;
;шлем страницы данных в GS:
; en: send the data pages to the GS (10 x 16 KB, card pages #20 up):
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
        LD A,pgdata6
        CALL GSDMASENDPG
        LD A,pgdata7
        CALL GSDMASENDPG
        LD A,pgdata8
        CALL GSDMASENDPG
        LD A,pgdata9
        CALL GSDMASENDPG
        LD A,pgdata10
        CALL GSDMASENDPG
; Command 4: the card restarts ZX-DMA at card page #10 for the code
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения:
; en: and wait for the switch:
        WN
        GD
;
;шлем страницы кода в GS:
; en: send the code pages to the GS (10 x 16 KB, card pages #10 up; the 10th is GSRESET):
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,pgcode2
        CALL GSDMASENDPG
        LD A,pgcode3
        CALL GSDMASENDPG
        LD A,pgcode4
        CALL GSDMASENDPG
        LD A,pgcode5
        CALL GSDMASENDPG
        LD A,pgcode6
        CALL GSDMASENDPG
        LD A,pgcode7
        CALL GSDMASENDPG
        LD A,pgcode8
        CALL GSDMASENDPG
        LD A,pgcode9
        CALL GSDMASENDPG
        LD A,pgcode10
        CALL GSDMASENDPG
; Command 5: ZX-DMA off; the card then starts running its code pages (GSRESIDENTGO)
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA:
; en: and wait until the DMA is off:
        WN
        GD
;

; --- 7. Copy the ZX resident loop to #BF04 and the IM 2 handler to #BFBF ------------------------------------------
        LD HL,WASZXRES
        LD DE,ZXRESIDENT
        LD BC,LENZXRES
        LDIR
        LD HL,WASIMER
        LD DE,IMER
        LD BC,LENIMER
        LDIR
; --- 8. IM 2: I = #BE, table #BE00-#BF00 filled with #BF (257 bytes: any bus value gives vector #BFBF) ------------
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

; --- 9. Music init (interrupts still off), frame counter = 0, interrupts on: from now on every frame plays music ---
        LD A,pgmusic
        setpga                  ;заполняет curpg (en: fills curpg)
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

; --- 10. Clear the pixel areas (#C000-#D7FF, #E000-#F7FF) of RAM pages 4-7. Pages 5 and 7 are the two screens;
;         in page 5 the second area is #6000-#77FF, so GO itself (#7800) is not touched. Page 4 held blugr.2
;         (pgdata10), already on the card -----------------------------------------------------------------------------
        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG
        LD A,#16
        CALL CL1800PG
        LD A,#17
        CALL CL1800PG

; --- 11. The intro picture: pixels first (attributes are still black), one frame, then the attributes -----------------
        LD HL,wasmedieval
        LD DE,#4000
        LD BC,6144
        LDIR
        HALT
        LD BC,768
        LDIR

; --- 12. Into the resident loop: effect pages #B0.. with their frame windows from zxtimings ---------------------------
        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; MOVEPAGE dst_page,src_addr,length,dst_addr: copy length (a multiple of 256) bytes from src_addr in the current
; page to dst_addr in page dst_page, through resbuf (tail-jumps to RESMOVEPAGE). Not used here; the same macro
; in hedge12.asm moves DEMON16/DEMON17 into pages #16/#17.
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; GSDMASENDPG: A = #7FFD value. Copies its 16 KB #C000-#FFFF to #0000-#3FFF: ROM writes, which ZX-DMA takes
; into the card's RAM at its current address.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; CL1800PG: A = #7FFD value. Zeroes #C000-#D7FF and #E000-#F7FF of that page (CL1800: #1800 bytes from HL).
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

; =====================================================================================================================
; GSPROGGO - the card program (NeoGS Z80), assembled for card address #5830, sent by step 5 of GO
; =====================================================================================================================
; 1. silence all 8 channels; 2. GSCFG0: 24 MHz, no ROM (RAM everywhere), extended paging (MPAG at #8000,
; MPAGEX at #C000); 3. ZX-DMA module 1; then the handshake with the ZX: command 3 = DMA on at card page #20,
; command 4 = DMA restarted at card page #10, command 5 = DMA off; each answered through ZXDATWR.
; Finally the card's resident loop: every code page in turn at #C000, CALL #C000.
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

; The author's page notation "gspgN:H": 32 KB page N, 16 KB half H
        LD A,1                  ;#8000: gspg1:0
        OUT (MPAG),A
        LD A,2                  ;#C000: gspg2:0
        OUT (MPAGEX),A

        LD A,1                  ;SET DMA MODULE #1
        OUT (DMA_MOD),A

; wait for command 3
        LD E,3
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

; card page number -> DMA address: RRCA moves the page's bit 0 (the 16 KB half) into bit 7; bits 7-6 of the
; result go to DMA_MAD (address bits 15-14), bits 5-0 to DMA_HAD; DMA_LAD = 0
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: the flag for the ZX: "DMA ON")

; wait for command 4: the same for the code pages
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: the flag for the ZX: "DMA ON")

; wait for command 5: upload done
        LD E,5
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: the flag for the ZX: "DMA OFF")
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready? - waits until the ZX has read the answer)

        LD A,gspgcode

; The card's resident loop: A = card page (kept rotated, bit 7 = the 16 KB half). RLCA / INC / RRCA steps it
; #10, #90, #11, #91, ... #94 - the order ZX-DMA filled them in: ROTPREGS, ROBPREGS, TUNNELGS, ROTATEGS,
; ROTBARGS, MULBARGS, HEDGEGS, BABAGS, TEXGS, then GSRESET (which never returns).
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

; =====================================================================================================================
; ZX resident loop, assembled for #BF04 (copied there by GO step 7). Stays in #8000-#BFFF, which the effects
; leave alone, while the #C000 window walks through the effect pages.
; =====================================================================================================================
; ZXRES0: A = #7FFD value of the effect page, HL -> its zxtimings entry. Calls the page's #C000 with
; BC = start frame, DE = end frame (in timer units), then goes on with the next page and entry.
WASZXRES
        DISP ZXRESIDENT
ZXRESIDENTGO
ZXRES0
        setpga
        PUSH AF
        LD C,(HL)
        INC HL
        LD B,(HL)               ;время начала эффекта (en: the effect's start time)
        INC HL
        LD E,(HL)
        INC HL
        LD D,(HL)               ;время конца эффекта (en: the effect's end time)
        INC HL
        PUSH HL
        CALL #C000
        POP HL
        POP AF
        CALL ZXRESNEXTPG
        JP ZXRES0
; RESMOVEPAGE (see the MOVEPAGE macro): IXH = source page, IXL = destination page, HL = source, DE = destination,
; IYH = number of 256-byte blocks. Each block goes source page -> resbuf -> destination page.
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
        setpga                  ;портит BC (en: destroys BC)
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
        RET
; zxtimings: start and end frame (timer, 50 per second; 896 frames = 17.92 s) of each ZX effect page, in page order:
;   #B0 tunnel 896-1778, #B1 rotator 1792-2674, #B2 rotating bars 2688-3248, #B3 multi-bars 3476-3556,
;   #B4 HEDGEZX1 0,0, #B5 hedgehog (HEDGEZX2) 3584-4466, #B6 girl and mill 4480-4914, #B7 textured face 4928-5824.
; Frames 0-895 are the intro picture with the music. Page #D0 (the credits) gets the next four bytes - the code
; of ZXRESNEXTPG - as its "timings"; the credits code ignores BC/DE.
zxtimings
        DW 896,896*2-14         ;tunnel
        DW 896*2,896*3-14       ;rotate
        DW 896*3,896*3+560      ;rotbar
        DW 896*4-28-80,896*4-28 ;mulbar
        DW 0,0
        DW 896*4,896*5-14       ;hedge
        DW 896*5,896*5+448-14   ;baba
        DW 896*5+448,896*6+448  ;tex
; ZXRESNEXTPG: next effect page: #B0 -> #B1 ... -> #B7, then #B8 + #18 = #D0 (the credits page)
ZXRESNEXTPG
        INC A
        BIT 3,A
        RET Z
        ADD A,#18
        RET
        ENT
LENZXRES=$-WASZXRES

; =====================================================================================================================
; IMER - the IM 2 handler, assembled for #BFBF (vector table at #BE00)
; =====================================================================================================================
; The interrupted code may run with SP anywhere (the effects pop and push picture data through SP), so the handler
; uses only the 2 bytes the CPU pushed: it swaps the saved PC for the interrupted DE, keeps the PC in outimjp and
; the SP in outimsp, and switches to its own stack at #5F00. Then: timer + 1; the music page at #C000 (curpg's
; bit 3, the shown screen, kept); CALL #C005 (the music player); curpg back; registers back; SP back, DE popped
; from where the PC was; EI; JP to the interrupted PC.
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

; =====================================================================================================================
; GSRESET - the card's 10th code page (GO copies it to #C000 of pgcode10; ZX-DMA takes it to the card).
; The card's resident loop calls it after TEXGS: it copies the reset code to #5C00 (#4000-#7FFF stays RAM when
; the ROM comes back), DMA off, GSCFG0 back to 10 MHz with the ROM and normal paging, RST 0: the card restarts
; its own firmware. (LD B,1 with C left over: LDIR copies at least 256 bytes, more than GSRESET needs.)
; =====================================================================================================================
WASGSRESET
        DISP #C000
        LD HL,GSRESET
        LD DE,#5C00
        LD B,1
        PUSH DE
        LDIR
        RET
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0
        ENT
LENGSRESET=$-WASGSRESET

; =====================================================================================================================
; CREDITS - the last ZX "effect" (GO copies it to #C000 of page #D0): the credits screen (pixels, one frame,
; attributes) and then JR $ - the demo ends here by design. The music keeps playing from the IM 2 handler.
; =====================================================================================================================
WASCREDITS
        DISP #C000
        LD HL,credits
        LD DE,#4000
        LD BC,#1800
        LDIR
        HALT
        LD BC,#300
        LDIR
        JR $
credits
        INCBIN "credits"
        ENT
LENCREDITS=$-WASCREDITS
; The intro picture (6912 bytes, shown by GO step 11)
wasmedieval
        INCBIN "medieval"
end

; =====================================================================================================================
; The page images: one file at #C000 of each page (not saved by the sjasmplus build; the loader loads the files)
; =====================================================================================================================
; Card data 1..10 -> card pages #20.. (command 3)
        ORG #C000,pgdata1
        INCBIN "kishki.C"
        ORG #C000,pgdata2
        INCBIN "baba.C"
        ORG #C000,pgdata3
        INCBIN "baba.0"
; en: 8 KB file; the rest of the page (#E000-#FFFF) zero-filled. ALASM's `DS -$` (= #10000 - $)
        DS (-$&#FFFF)           ;для маски (en: for the mask)
        ORG #C000,pgdata4
        INCBIN "mill.C"
        ORG #C000,pgdata5
        INCBIN "mill.0"
        ORG #C000,pgdata6
        INCBIN "girl13.2"
        ORG #C000,pgdata7
        INCBIN "blugr.C"
        ORG #C000,pgdata8
        INCBIN "blugr.0"
        ORG #C000,pgdata9
        INCBIN "blugr.1"
        ORG #C000,pgdata10
        INCBIN "blugr.2"

; Card code 1..10 -> card pages #10.. (command 4), run by the card in this order
        ORG #C000,pgcode1
        INCBIN "ROTPREGS"
        ORG #C000,pgcode2
        INCBIN "ROBPREGS"
        ORG #C000,pgcode3
        INCBIN "TUNNELGS"
        ORG #C000,pgcode4
        INCBIN "ROTATEGS"
        ORG #C000,pgcode5
        INCBIN "ROTBARGS"
        ORG #C000,pgcode6
        INCBIN "MULBARGS"
        ORG #C000,pgcode7
        INCBIN "HEDGEGS"
        ORG #C000,pgcode8
        INCBIN "BABAGS"
        ORG #C000,pgcode9
        INCBIN "TEXGS"
; pgcode10: filled at run time by GO (GSRESET)
        ORG #C000,pgcode10


; The ZX halves, run in this order by the resident loop (timings in zxtimings)
        ORG #C000,pgeffects
        INCBIN "TUNNELZX"
        ORG #C000,pgeffects+1
        INCBIN "ROTATEZX"
        ORG #C000,pgeffects+2
        INCBIN "ROTBARZX"
        ORG #C000,pgeffects+3
        INCBIN "MULBARZX"
        ORG #C000,pgeffects+4
        INCBIN "HEDGEZX1"
        ORG #C000,pgeffects+5
        INCBIN "HEDGEZX2"
        ORG #C000,pgeffects+6
        INCBIN "BABAZX"
        ORG #C000,pgeffects+7
        INCBIN "TEXZX"
; page #D0: filled at run time by GO (CREDITS)
        ORG #C000,pgeffects+#20

; The music (TurboSound FM): init #C000, play #C005
        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ---------------------------------------------------------------------------------------------------------------------
; Release tool, not part of the demo. #5CDD is the TR-DOS file name buffer (8 characters + type): 'THE LINK' B
; is the name m2hrust's packer saves its single-file release under. m2hrust assembles at #FB00 of the page last
; selected here (the music page) and puts its "make" entry at #5B00: without CAPS SHIFT it jumps to GO, with
; CAPS SHIFT it packs begin..end into a self-extracting BASIC file. The demo never calls it.
; ---------------------------------------------------------------------------------------------------------------------
        ORG #5CDD
        DB 'THE LINKB'
        INCLUDE "m2hrust.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the main block #7800..end, which the release loader loads as "thelink" C. The
; pages above are not saved: each holds one file as it is, and the loader loads those files themselves.
        SAVEBIN "thelink",begin,end-begin
