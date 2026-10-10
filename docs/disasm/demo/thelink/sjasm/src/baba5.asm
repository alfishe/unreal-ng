; =====================================================================================================================
; baba5.asm - a later, unfinished take on baba4.asm ("girl and mill", effect 7 of The Link, Alone Coder, 2009).
;
; NOT USED BY THE DEMO. Its BABAZX is byte for byte baba4's (394 bytes), but its BABAGS (3907 bytes) is not the one on the
; author's disk (3286 bytes, from baba4). The build does not assemble this file; its SAVEBIN block writes baba5.BABAZX /
; baba5.BABAGS. Everything else (pages, harness GO, protocol, BABAZX) is as in baba4.asm - read that file's notes first.
;
; WHAT BABA5 CHANGES (diff against baba4.asm): the background (the mill) is ZOOMED.
;   - GSTIMERACTION: a 16-bit zoom value starts at #7000 and drops by 170 per frame (self-modified operand) until it
;     would go below 0; its high byte goes to gsbgscaley and twice it to gsbgscalex. The magnification is
;     256 / (256 - gsbgscaley), the same both ways: from about 1.78 (#6F) down to 1 in 168 frames (a zoom-out).
;   - DRAWLINE is rewritten: per line, the vertical accumulator A' += gsbgscaley; without carry the source moves to the
;     next 128-byte line (with carry the line repeats); per POP, A += gsbgscalex; on carry DEC SP re-reads one byte (one
;     source byte stretched over two). The line starts gsbgscalex / 8 bytes in, which centres the stretched line.
;   - The page wrap of the background source is now the carry of ADD HL,DE (H += #C0, MPAGEX = IYH) instead of the
;     #0000 marker; DRAWLINE uses LOCAL labels, so the converter expanded it at its two call sites (_NPAG0__L1, _NPAG1__L2).
;   - A' carries the vertical accumulator, so the buffer page can no longer wait in A': new macro GSSETSCRPG1 (first
;     16K half) next to GSSETSCRPG2; A is saved in B or H around them; GSLOOP's LD SP,HL is gone (DRAWLINE sets SP).
;   - Unfinished: before the sprite passes both register sets get A = C = 0 (a zero zoom for the sprite), but BLITLINE is
;     baba4's unchanged (no zoom, still the #0000 marker); the blit then runs in the alternate set (harmless). The first
;     picture is drawn before any timer step, with gsbgscalex/y = 0 (no zoom); the horizontal accumulator A is not reset
;     per line.
;   - Two byte operands lack the converter's `low` (LD (HL),IMER and LD (HL),curpg): sjasmplus truncates them to the same
;     low byte, with a warning (the converter added `low` only in the files the build uses).
;
; CARD PAGE NUMBERS (as in baba4). With GSCFG0 EXPAG set, MPAG (#8000-#BFFF) and MPAGEX (#C000-#FFFF) select 16K
; pages; a value v means 16K page {v6..v0,v7}, i.e. RLCA v. Hence: RLCA : INC A : RRCA = the next 16K page;
; +#80 = the second 16K half of the 32K page v; RRCA + the AND #C0 / XOR split = ZX-DMA address (DMA_HAD = page >> 2, DMA_MAD = (page & 3) << 6, LAD = 0).
;   gspgtex   #A0 -> 16K #41 baba.C, then #42 baba.0: the sprite (the girl), 192 lines + 64 zero lines
;   gspgtex+1 #A1 -> 16K #43 mill.C, then #44 mill.0: the background (the mill), 235 lines
;   gspgscr0 2 -> 16K #4/#5, gspgscr1 3 -> 16K #6/#7: two 32K screen buffers, drawn in turn at MPAG #8000 (16K at a time)
;   gstmask #0100: the 256-byte sprite mask table; GSBABACODE #5900: the card code; GSPROG / GSSTACK #5830
; Pictures: 128 bytes per line (256 pixels, 16 colours, 2 pixels per byte), read with POP (SP = source pointer).
;
; ZX <-> CARD, ONE FRAME (see docs/disasm/demo/thelink/README.md section 6):
;   ZX  (BIGLOOP): timer low byte to GSDAT + command 0 ("frame shown, stop DMA"), wait until the card takes GSDAT;
;       at zxendtime or on Space: command 2 and quit; command 1 ("start DMA"), wait for the answer; run OUT16C,
;       generated code of 12288 x (LD D,(HL) : LD E,(HL) : PUSH DE) with HL = #1E1F in the ROM area: every read is
;       answered by ZX-DMA with the next byte of the card's buffer, pushed into the 16-colour screen in RAM pages 6, 7.
;   Card (GSLOOP): draws the background and blends the sprite into one buffer, waits for command 0, stops DMA, reads the
;       ZX's timer byte, waits for command 1 (65536 polls, then GSRESET) or 2 (RET: effect over), points DMA at the buffer
;       just drawn and starts it, then moves both pictures by the frames elapsed (at most 5).
;
; ENTRY POINTS: GO (#7800, harness only); BABAZX at #C000 (CALLed by the ZX resident loop, BC = start frame, DE = end
; frame); BABAGS at #C000 of its card code page (CALLed by the card's resident loop GSRES0, RETs on command 2).
;
; CONVERTER ADDITIONS (unreal-asm, ALASM -> sjasmplus): the DEVICE line; `low x` with the comment "ALASM keeps the low
; byte" (ALASM silently stores the low byte of a 16-bit value in a byte operand); DS (-$&#FFFF) for ALASM's DS -$ (fill to
; the end of memory: the zero tail of baba.0, "for the mask"); MOVEPAGE's parameters \0..\3 renamed _arg0.._arg3;
; JNZ/JC -> JR NZ/JR C, HX/LY -> IXH/IYL, EXA -> EX AF,AF', IF0/IFN -> IF, multi-operand lines split, and parentheses
; for ALASM's left-to-right arithmetic ((235-192)*128); the SAVEBIN block at the end, which replaces running SAVEOBJ4.
; The macro DRAWLINE is expanded at its calls (the original stays above them as comments); BLITLINE stays a macro.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
; border=1 shows the ZX loop in the border: black from the handshake to the end of the copy, white after it.
border=0

; ---- #7FFD pages of the stand-alone harness (THELINK.asm has the demo's own page plan) ----
;GS data in ZX memory:
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

; BABACODE: the ZX half runs here (#6000-#6179) and builds OUT16C right after it (#617A-#B2AB, 20786 bytes).
BABACODE=#6000

; Resident area in RAM page 2, the same addresses as THELINK.asm: resbuf (MOVEPAGE's 256-byte buffer), the IM 2
; table, curpg (the last #7FFD value, for the interrupt), timer (frames, 50 per second), the resident loop, the IM 2
; handler (#BFBF-#BFFA); outimstack: the handler's own stack.
resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports.asm"
; Card addresses: GSPROG the card program (sent with GS commands #18/#19), GSSTACK the card stack just below it,
; GSBABACODE where BABAGS copies itself (#5900-#65C7).
GSPROG=#5830
GSSTACK=#5830
GSBABACODE=#5900

; Card pages in the EXPAG code (see the header): gspgcode #10 = 16K page #20, the first card code page.
gspgcode=#10
; gspgdata #A0 = 16K page #41: baba.C lands where the demo has it (the demo: #20 = 16K #40, kishki first)
gspgdata=#A0                    ;#20 (en: the demo's value)

gspgscr0=2                      ;32K
gspgscr1=3                      ;32K
gsscr16=#8000                   ;2 экрана по 2 страницы (en: 2 screens of 2 pages each; not used in the code)
gspgtex=#A0                     ;2*32K (en: girl 16K #41-#42, mill #43-#44)
gstmask=#0100                   ;256

; GSSETPGA (card: A = page at #C000; not used here); setpga (ZX: #7FFD = A, also kept in curpg; uses BC).
        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; HARNESS: GO at #7800 starts this one effect stand-alone (not part of BABAZX / BABAGS)
;  1. hard reset of the card, a few firmware commands, GSPROG into card #5830 with #18/#19, #13 = jump to it;
;  2. CL1800PG on #14 / #15 (RAM pages 4, 5): page 5 is the screen, its #E000 half is #6000-#77FF below GO;
;  3. ZX-DMA upload: command 3 + the 4 picture pages, command 4 + the code page, command 5 = end;
;  4. resident loop and IM 2 handler into page 2, IM 2 table, music init, timer = 0, into the resident loop.
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
;зачем-то POKE #4092,0
; en: for some reason POKE #4092,0 (card memory, commands #18/#19)
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

        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER
; en: as in RIFF TRACKER: per byte, GSDAT = address low + command #18, address high as data, then the byte +
; command #19
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
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
;и ждем включения:
; en: and wait until it is on (the card's answer in GSDAT)
        WN
        GD
;
;шлем страницы данных в GS:
; en: send the data pages to the card (each LDIR #C000 -> #0000 is taken by ZX-DMA)
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,pgdata2
        CALL GSDMASENDPG
        LD A,pgdata3
        CALL GSDMASENDPG
        LD A,pgdata4
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения:
; en: and wait for the switch
        WN
        GD
;
;шлем страницы кода в GS:
; en: send the code pages to the card
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA:
; en: and wait until DMA is off
        WN
        GD
;

; resident loop and IM 2 handler into page 2; IM 2 table #BE00-#BF00 filled with low IMER (#BF): vector #BFBF
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
        LD (HL),IMER
        LDIR
        IM 2

        LD A,pgmusic
        setpga                  ;заполняет curpg (en: fills curpg)
; music init (thelinkm: init #C000, play #C005)
        CALL #C000

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; MOVEPAGE dst_page,src,size,dst: copy size bytes (whole 256-byte blocks) from the current page to dst in another
; page through resbuf (RESMOVEPAGE). Shared boilerplate: not used in this file.
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; GSDMASENDPG: A = #7FFD page; LDIR its 16 KB from #C000 to #0000. The writes hit the ROM (nothing changes on the
; ZX); the card's ZX-DMA stores every byte into card RAM.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; CL1800PG: A = #7FFD page; zero #C000-#D7FF and #E000-#F7FF in it. CL1800: zero #1800 bytes from HL (L = 0).
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

; ---- card program (harness), 134 bytes, run at card #5830 ----
; Silence the 8 channels; 24 MHz, NOROM, EXPAG; MPAG 1, MPAGEX 2; DMA module 1 (ZX-DMA). Upload protocol:
; command 3 -> DMA to gspgdata, answer; command 4 -> DMA to gspgcode, answer; command 5 -> DMA off, answer, wait
; until the ZX has read it. Then GSRES0 runs the code pages one by one.
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
; wait for command 3 (the ZX is about to send the data pages)
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
; en: (disabled) ready to receive
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

; DMA address = 16K page of gspgdata * #4000 (see the header)
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX)

        LD E,4
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
; en: (disabled) ready to receive
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX)

        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
; en: (disabled) ready to receive
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX)
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready? i.e. has it read the answer)

        LD A,gspgcode
;JP GSRESIDENTGO

; card resident loop: MPAGEX = code page, CALL #C000 (one effect), then the next 16K page (RLCA : INC A : RRCA)
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

; ---- ZX resident loop, copied to ZXRESIDENT #BF04 ----
; For each effect page from pgeffects (ROM bit #10 added): CALL #C000 with BC = start, DE = end frame from
; zxtimings, then the next page. In the harness that page (pgeffects+1) holds JR $.
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
; RESMOVEPAGE: MOVEPAGE's body; IXH = source page, IXL = target page, IYH = number of 256-byte blocks
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
        setpga                  ;портит BC (en: clobbers BC)
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
        RET
; start and end frame of each effect (one entry in the harness)
zxtimings
        DW 8,8+420              ;baba
        ENT
LENZXRES=$-WASZXRES

; ---- IM 2 handler, copied to IMER #BFBF ----
; The ZX's copy code uses SP as a pointer into the screen, so the CPU's push of the return address lands there.
; The handler puts DE in that slot (EX DE,HL : EX (SP),HL), keeps the return address in outimjp and SP in outimsp,
; works on its own stack outimstack, counts timer, plays the music (page pgmusic with curpg's screen / ROM bits
; #18), selects curpg's page again and returns by JP with HL, DE, BC, AF, AF' intact (IX, IY and the alternate
; set are not saved: those lines are commented out).
WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата (en: we write DE in place of the return address)
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE                 ;hl до прерывания (en: HL before the interrupt)
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

; =====================================================================================================================
; BABAGS - the card half. Runs at #C000 of its card code page (MPAGEX), copies itself to GSBABACODE #5900 (MPAGEX
; is then free for the pictures), builds the mask table and loops one picture per pass until command 2.
; =====================================================================================================================
        ORG #C000,pgcode1
pgbabags=pgcode1
        LD HL,WASGSBABACODE
        LD DE,GSBABACODE
        LD BC,LENGSBABACODE
        LDIR

        JP GSBABAGO

WASGSBABACODE
        DISP GSBABACODE
GSBABAGO
;генератор таблицы маски для спрайтов
; en: mask table generator for the sprites
;%RLRRRLLL
; en: a 16-colour byte is two pixels, bits %RLRRRLLL (L = the left pixel's bits, R = the right one's)
;маска=0
; en: mask = 0
;если L=0, то маска|=%01000111
; en: if L = 0, mask |= %01000111
;если R=0, то маска|=%10111000
; en: if R = 0, mask |= %10111000
; Colour 0 is transparent: the blit does screen = (screen AND mask[s]) OR s, mask[s] at gstmask + s.
        LD HL,gstmask
MKMASK0 LD A,L
        LD C,%01000111
        AND C
        JR Z,$+4
        LD C,0
        XOR L
        LD A,C
        JR NZ,$+4
        OR %10111000
        LD (HL),A
        INC L
        JR NZ,MKMASK0

; ---- one picture per pass: flip buffers, background, sprite, hand the buffer to the ZX, animate ----
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: drawing into SCR2; self-modified)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now drawing into SCR1)
;потом дадим его ZX'у
; en: later we give it to the ZX

; marker at card #0000: a source pointer (SP) running off the end of a 16K page at #FFFF wraps to #0000 and POPs
; this byte; the line routines then switch MPAGEX to the next page
        LD A,%10000000          ;не бывает в картинке (en: never occurs in a picture)
        LD (0),A

; baba5: GSSETSCRPG1 / GSSETSCRPG2 = MPAG to the first / second 16K half of the buffer being drawn (A' now holds
; the vertical zoom accumulator, so the page is recomputed from gsdrawscr each time)
        MACRO GSSETSCRPG1
        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0
        JR Z,$+4
        LD A,gspgscr1
        OUT (MPAG),A
        ENDM
        MACRO GSSETSCRPG2
        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0+#80
        JR Z,$+4
        LD A,gspgscr1+#80
        OUT (MPAG),A
        ENDM

        LD (GSLOOPSP),SP

        GSSETSCRPG1

        LD A,gspgtex+1
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD IYH,A

; background, ZOOMED (new in baba5): the mill from 16K page #43 (IYH = #44 for the wrap). Set-up: HL = source line
; (gsbgy), DE = 128, A = 0, C = gsbgscaley, then EX AF,AF' : EXX puts them in the alternate set; in the main set
; C = gsbgscalex, the source moves gsbgscalex/8 bytes right (centring) and A = CPL of (gsbgscalex*32) & #FF starts
; the horizontal accumulator. 4 passes as in baba4 (rows 0-63, 64-95; bottom, then top); A is kept in B across
; the page macros. gsbgscalex / gsbgscaley are self-modified by GSTIMERACTION.
gsbgy=$+1
        LD HL,(235-192)*128+#C000 ;начало фона (en: background start: line 43 of 235, self-modified)
        LD A,L
        AND #80
        LD L,A
        LD DE,128
        LD A,0
gsbgscaley=$+1
        LD C,0
        EX AF,AF'
        EXX
gsbgscalex=$+1
        LD C,0
        LD L,C
        LD H,0
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        LD A,H
        EXX
        ADD A,L
        LD L,A
        EXX
        LD A,L
        CPL
        LD H,#80                ;куда пишем (en: where we write: buffer row, H = #80 + row)
        LD B,64
        LD IX,GSLOOPDBOTQ
        JP DRAWLINEBOTTOM
GSLOOPDBOTQ
        LD B,A
        GSSETSCRPG2
        LD A,B
        LD H,#80                ;куда пишем
        LD B,32
        LD IX,GSLOOPDBOTQ2
        JP DRAWLINEBOTTOM
GSLOOPDBOTQ2
        LD B,A
        GSSETSCRPG1
        LD A,B
        LD H,#80                ;куда пишем
        LD B,64
        LD IX,GSLOOPDTOPQ
        JP DRAWLINETOP
GSLOOPDTOPQ
        LD B,A
        GSSETSCRPG2
        LD A,B
        LD H,#80                ;куда пишем
        LD B,32
        LD IX,GSLOOPDTOPQ2
        JP DRAWLINETOP
GSLOOPDTOPQ2

        GSSETSCRPG1

; sprite: the girl from 16K page #41 (IYH = #42 for the wrap), blended through the mask table (B = high gstmask);
; the same 4 passes, IYL counts the rows
        LD A,gspgtex
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD IYH,A
gsfgy=$+1
        LD HL,#EB00             ;начало спрайта (en: sprite start: line 86, self-modified)
        LD A,L
        AND #80
        LD L,A
        LD SP,HL
        LD A,0
        LD C,0
        EX AF,AF'
        EXX
        LD A,0
        LD C,0
; (above) A = C = 0 in both sets: a zero zoom prepared for the sprite, but BLITLINE does not use it (unfinished);
; the blit runs in the set active after EXX (harmless). A is kept in H across the page macros.
        LD B,high gstmask
        LD H,#80                ;куда пишем
        LD IYL,64
        LD IX,GSLOOPBBOTQ
        JP BLITLINEBOTTOM
GSLOOPBBOTQ
        LD H,A
        GSSETSCRPG2
        LD A,H
        LD H,#80                ;куда пишем
        LD IYL,32
        LD IX,GSLOOPBBOTQ2
        JP BLITLINEBOTTOM
GSLOOPBBOTQ2
        LD H,A
        GSSETSCRPG1
        LD A,H
        LD H,#80                ;куда пишем
        LD IYL,64
        LD IX,GSLOOPBTOPQ
        JP BLITLINETOP
GSLOOPBTOPQ
        LD H,A
        GSSETSCRPG2
        LD A,H
        LD H,#80                ;куда пишем
        LD IYL,32
        LD IX,GSLOOPBTOPQ2
; the last 32 rows (source lines start+160..start+191) only while gsfgy < #D800: from there on they fall into
; baba.0's zero tail (transparent) or past the end of the data
        LD A,(gsfgy+1)
        CP #D8
        JP C,BLITLINETOP
GSLOOPBTOPQ2

GSLOOPSP=$+1
        LD SP,0

;GS ожидает конец кадра у ZX
; en: the card waits for the ZX's end of frame (command 0); no timeout here (JP C,GSRESET is commented out)
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
; the ZX sent its timer low byte in GSDAT with command 0
        IN A,(ZXDATRD)
        LD (gstimer),A
;OUT (ZXDATWR),A ;признак для ZX "DMA OFF"
;GS ожидает обнуления (ZX запросит данные)
; en: the card waits for "zeroing" (the ZX will request data) - in the code: for a non-zero command, at most
; 65536 polls, then GSRESET; 1 = go on, anything else (2) = RET, effect over
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT

        LD A,(gsdrawscr)
        OR A
        LD A,gspgscr0
        JR Z,$+4
        LD A,gspgscr1
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX)

; DMA -> the buffer just drawn (above), started: the ZX reads it now. Then the animation, by elapsed frames:
; gstimer (from the ZX) - gsoldtimer, at most 5 steps; each step moves the sprite source 64 bytes and the
; background 32 bytes (half / quarter of a line) toward the start of its picture, each stopping at #C000
; (BIT 6,H): both get there after 172 steps; the sprite moves twice as fast as the background.
gstimer=$+1
        LD A,0
gsoldtimer=$+1
        LD C,0
        LD (gsoldtimer),A
        SUB C
        JR Z,GSTIMERACTIONQ
        CP 5
        JR C,$+4
        LD A,5
        LD B,A
GSTIMERACTION
        LD HL,(gsfgy)
        LD DE,-64               ;-256
        ADD HL,DE
        BIT 6,H
        JR Z,$+5
        LD (gsfgy),HL
        LD HL,(gsbgy)
        LD DE,-32               ;-128
        ADD HL,DE
        BIT 6,H
        JR Z,$+5
        LD (gsbgy),HL
; baba5: the zoom, a 16-bit value from #7000 down by 170 per frame (LD ($-6),HL patches LD HL's operand) until it
; would go below 0; high byte -> gsbgscaley, twice it -> gsbgscalex: magnification 256/(256-H), 1.78 down to 1
        LD DE,170
        OR A
        LD HL,#7000
        SBC HL,DE
        JR C,GSnscale
        LD ($-6),HL
        LD A,H
        LD (gsbgscaley),A
        ADD A,A
        LD (gsbgscalex),A
GSnscale
        DJNZ GSTIMERACTION
GSTIMERACTIONQ
        JP GSLOOP

; GSRESET: the ZX is gone - DMA off, 10 MHz, ROM back, normal paging, RST 0: the card restarts its firmware
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

; (expanded at its calls)        MACRO DRAWLINE
; (expanded at its calls)        LOCAL
; (expanded at its calls)         EXX
; (expanded at its calls)         EXA
; (expanded at its calls)         ADD A,C
; (expanded at its calls)         JC _NPAG\0
; (expanded at its calls)         ADD HL,DE
; (expanded at its calls)         JNC _NPAG\0
; (expanded at its calls)         LD B,A
; (expanded at its calls)         LD A,HY
; (expanded at its calls)         OUT (MPAGEX),A
; (expanded at its calls)         LD A,H
; (expanded at its calls)         ADD A,#C0
; (expanded at its calls)         LD H,A
; (expanded at its calls)         LD A,B
; (expanded at its calls) _NPAG\0
; (expanded at its calls)         EXA
; (expanded at its calls)         LD SP,HL
; (expanded at its calls)         EXX
; (expanded at its calls)        DUP 32
; (expanded at its calls)         POP DE
; (expanded at its calls)        ADD A,C
; (expanded at its calls)        JNC $+3
; (expanded at its calls)        DEC SP
; (expanded at its calls)         LD L,_+#00
; (expanded at its calls)         LD (HL),E
; (expanded at its calls)         LD L,_+#80
; (expanded at its calls)         LD (HL),D
; (expanded at its calls)         POP DE
; (expanded at its calls)        ADD A,C
; (expanded at its calls)        JNC $+3
; (expanded at its calls)        DEC SP
; (expanded at its calls)         LD L,_+#40
; (expanded at its calls)         LD (HL),E
; (expanded at its calls)         LD L,_+#C0
; (expanded at its calls)         LD (HL),D
; (expanded at its calls) _=_-1
; (expanded at its calls)        EDUP
; (expanded at its calls)        ENDL
; (expanded at its calls)        ENDM
; baba5's DRAWLINE (expanded below, twice): EXX : EX AF,AF' to the source set; A' += C' (gsbgscaley): no carry =
; next source line (HL' += 128; past #FFFF: MPAGEX = IYH, H' += #C0), carry = the same line again; SP = HL'. Then
; per POP A += C (gsbgscalex): carry = DEC SP, the next POP re-reads one byte (horizontal stretch). Stores as in
; baba4: L = _+#00/#80/#40/#C0, _ counting down.
DRAWLINEBOTTOM
_=31
; macro DRAWLINE expanded

        EXX
        EX AF,AF'
        ADD A,C
        JR C,_NPAG0__L1
        ADD HL,DE
        JR NC,_NPAG0__L1
        LD B,A
        LD A,IYH
        OUT (MPAGEX),A
        LD A,H
        ADD A,#C0
        LD H,A
        LD A,B
_NPAG0__L1
        EX AF,AF'
        LD SP,HL
        EXX
        DUP 32
        POP DE
        ADD A,C
        JR NC,$+3
        DEC SP
        LD L,_+#00
        LD (HL),E
        LD L,_+#80
        LD (HL),D
        POP DE
        ADD A,C
        JR NC,$+3
        DEC SP
        LD L,_+#40
        LD (HL),E
        LD L,_+#C0
        LD (HL),D
_=_-1
        EDUP

        INC H
        DEC B
        JP NZ,DRAWLINEBOTTOM
        JP (IX)
DRAWLINETOP
_=31+32
; macro DRAWLINE expanded

        EXX
        EX AF,AF'
        ADD A,C
        JR C,_NPAG1__L2
        ADD HL,DE
        JR NC,_NPAG1__L2
        LD B,A
        LD A,IYH
        OUT (MPAGEX),A
        LD A,H
        ADD A,#C0
        LD H,A
        LD A,B
_NPAG1__L2
        EX AF,AF'
        LD SP,HL
        EXX
        DUP 32
        POP DE
        ADD A,C
        JR NC,$+3
        DEC SP
        LD L,_+#00
        LD (HL),E
        LD L,_+#80
        LD (HL),D
        POP DE
        ADD A,C
        JR NC,$+3
        DEC SP
        LD L,_+#40
        LD (HL),E
        LD L,_+#C0
        LD (HL),D
_=_-1
        EDUP

        INC H
        DEC B
        JP NZ,DRAWLINETOP
        JP (IX)

; BLITLINE: as baba4's DRAWLINE (SP = source, H = buffer row, the #0000 marker test on the first POP), but blends:
; C = sprite byte s, A = (BC) = mask[s] (B = high gstmask), screen = (screen AND mask) OR s.
        MACRO BLITLINE
__=0
        DUP 32
        POP DE
        IF (__)==0
        LD A,E
        CP %10000000            ;(#0000)?
        JR NZ,$+6+3+1
        LD A,IYH
        OUT (MPAGEX),A
        LD SP,#C000
        POP DE
        ENDIF
__=1
        LD C,E
        LD A,(BC)
        LD L,_+#00
        AND (HL)
        OR C
        LD (HL),A
        LD C,D
        LD A,(BC)
        LD L,_+#80
        AND (HL)
        OR C
        LD (HL),A
        POP DE
        LD C,E
        LD A,(BC)
        LD L,_+#40
        AND (HL)
        OR C
        LD (HL),A
        LD C,D
        LD A,(BC)
        LD L,_+#C0
        AND (HL)
        OR C
        LD (HL),A
_=_-1
        EDUP
        ENDM
BLITLINEBOTTOM
_=31
        BLITLINE
        INC H
        DEC IYL
        JP NZ,BLITLINEBOTTOM
        JP (IX)
BLITLINETOP
_=31+32
        BLITLINE
        INC H
        DEC IYL
        JP NZ,BLITLINETOP
        JP (IX)

; en: 'end of code with the installers='
        DISPLAY 'с инсталлерами конец кода=',$
        ENT
LENGSBABACODE=$-WASGSBABACODE
        DISPLAY /T,LENGSBABACODE
LENBABAGS=$-#C000

; =====================================================================================================================
; BABAZX - the ZX half. CALLed at #C000 by the resident loop with BC = start frame, DE = end frame. Copies itself
; to BABACODE #6000, waits for the start frame, clears the 16-colour screen (RAM pages 6, 7), builds OUT16C, turns
; 16 colours on (#EFF7 = 1) and loops: handshake with the card, one picture copied, until the end frame or Space.
; =====================================================================================================================
        ORG #C000,pgeffects
pgbabazx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASBABACODE
        LD DE,BABACODE
        LD BC,LENBABACODE
        LDIR
        JP BABACODE

WASBABACODE
        DISP BABACODE
        POP HL
        LD (zxendtime),HL
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

;рисовать будем только на scr1 (pg6,7)
; en: we draw only on scr1 (pages 6, 7): #17 = page 7 with screen 1 shown; attributes of both screens to 0,
; then the bitmaps of pages 6 (#1E) and 7 (#1F). No double buffer on the ZX: the screen is rewritten while shown.
        LD A,#17
        setpga
        HALT
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        LD HL,#D800
        LD DE,#D801
        LD BC,767
        LD (HL),L
        LDIR
        HALT
        LD A,#1E
        setpga
        LD HL,#C000
        LD DE,#C001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#E000
        LD DE,#E001
        LD BC,#17FF
        LD (HL),L
        LDIR

        LD A,#1F
        setpga
        LD HL,#C000
        LD DE,#C001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#E000
        LD DE,#E001
        LD BC,#17FF
        LD (HL),L
        LDIR

        LD HL,OUT16C
        CALL MKOUT16C

        LD A,1                  ;16C
        LD BC,#EFF7
        OUT (C),A

; ---- per frame (no HALT: the loop runs at the card's pace) ----
BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
; the timer's low byte goes with command 0: the card animates by it
        LD A,(timer)
        OUT (GSDAT),A
;даем знать GS'у, что кадр готов
; en: tell the card the frame is done
;и DMA надо выключить:
; en: and DMA must be switched off
        XOR A
        OUT (GSCOM),A

;и ждем выключения:
; en: and wait until it is off (WN / GD commented out: WD only waits until the card has read GSDAT)
;WN
;GD
        WD                      ;пока не примет (en: until it takes it)
;
; quit at the end frame (self-modified) or on Space
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
;даем знать GS'у, что надо включить DMA
; en: tell the card to switch DMA on
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
; en: and wait until it is on
        WN
        GD
;
;DI
; the copy: SP saved, IX = return address, BC = #7FFD for the generated OUT (C),A. HL = #1E1F is at once the DMA
; read address (in the ROM area) and the two pages: H = #1E (page 6, VIEWPG0), L = #1F (page 7, VIEWPG1).
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
        LD BC,#7FFD
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1E1F
        LD A,(HL)               ;первый байт неверен (en: the first byte is wrong: a dummy read)
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
; QUIT: command 2 (the card's effect RETs), 16 colours off, page #10, attributes of screen 0 to 0, back to the
; resident loop
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of effect)
        HALT
        LD BC,#EFF7
        XOR A
        OUT (C),A
        LD A,#10
        setpga
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR
        RET
;LD A,C_GRST
;OUT (GSCTR),A
;JR $

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
; en: copy the linear 16-colour buffer from the NGS to the screen, bottom-up; the interrupt handler must put DE
; on the stack instead of the return address
MKOUT16C
;19K кода для 256X192
; en: 19K of code for 256x192 (in fact 20786 bytes, #617A-#B2AB). The template of what MKOUT16C builds
; (низ = bottom, верх = top):
;LD A,H
;LOOP
;LD (curpg),A
;OUT (C),A
;LD SP,низ
;DUP 16
;LD D,(HL)
;LD E,(HL)
;PUSH DE
;EDUP
;LD SP,верх
;DUP 16
;LD D,(HL)
;LD E,(HL)
;PUSH DE
;EDUP
;LD SP,низ+#2000
;DUP 16
;LD D,(HL)
;LD E,(HL)
;PUSH DE
;EDUP
;LD SP,верх+#2000
;DUP 16
;LD D,(HL)
;LD E,(HL)
;PUSH DE
;EDUP
;CP H
;LD A,L
;JP Z,LOOP
; MKOUT16C: HL = where to build (OUT16C). For each of 96 line pairs, from DE = #D7E0 (line 191) upward:
;   LD A,H : LD (curpg),A : OUT (C),A    - page #1E (curpg for the interrupt handler)
;   4 segments: line, the line 96 pixels above, both again at +#2000 (MKOUT16C0)
;   CP H : LD A,L : JP Z,<LD (curpg),A>  - the same once more with A = L, page #1F; then fall through
; and JP (IX) at the end. 96 x 2 x 4 x 32 = 24576 bytes: one card buffer row (256 bytes) per line pair, in this
; order - row 0 = lines 191 and 95, row 95 = lines 96 and 0.
        LD DE,#D7E0
        LD B,96                 ;высота 96*2 (en: height 96*2)
MKOUT16CLINE
        PUSH BC
        LD (HL),#7C             ;ld a,h
        INC HL
        LD (mkout16cloop),HL
        LD (HL),#32             ;ld (),a
        INC HL
        LD (HL),curpg
        INC HL
        LD (HL),high curpg
        INC HL
        LD (HL),#ED
        INC HL
        LD (HL),#79             ;out (c),a
        INC HL
        PUSH DE
        PUSH DE
        CALL MKOUT16C0
;на 96 пикс выше
; en: 96 pixels higher
        LD A,E
        SUB #80
        LD E,A
        LD A,-8
        JR NC,$+3
        ADD A,A
        ADD A,D
        LD D,A
        CALL MKOUT16C0
        POP DE
        SET 5,D
        CALL MKOUT16C0
;на 96 пикс выше
; en: 96 pixels higher
        LD A,E
        SUB #80
        LD E,A
        LD A,-8
        JR NC,$+3
        ADD A,A
        ADD A,D
        LD D,A
        CALL MKOUT16C0
        POP DE
; the loop tail: CP H : LD A,L : JP Z,mkout16cloop
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

; MKOUT16C0: one segment: LD SP,DE+32 (49 = #31) and 16 x (LD D,(HL) : LD E,(HL) : PUSH DE) = 32 bytes of one line
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
;у посл. линий третей надо LD (..),DE вместо последнего PUSH
; en: the last lines of the thirds need LD (..),DE instead of the last PUSH: when the segment starts at a 256-byte
; boundary (E = 0) the last PUSH (#D5) is overwritten by LD (DE),DE, so SP never reaches the boundary and an
; interrupt cannot write the two bytes below it (for #C000: #BFFE, outside the screen)
        LD A,E
        OR A
        JR NZ,MKOUT16CnPROTECT
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
; OUT16C: the generated copy code starts here (20786 bytes, up to #B2AB)
OUT16C
        ENT
LENBABACODE=$-WASBABACODE
LENBABAZX=$-#C000

; =====================================================================================================================
; HARNESS DATA: the pictures for the card (uploaded by GO) and the music
; =====================================================================================================================
        ORG #C000,pgdata1
        INCBIN "baba.C"
        ORG #C000,pgdata2
        INCBIN "baba.0"
; zero to the end of the page (ALASM's DS -$): 64 transparent lines below the girl
        DS (-$&#FFFF)           ;для маски (en: for the mask)
        ORG #C000,pgdata3
        INCBIN "mill.C"
        ORG #C000,pgdata4
        INCBIN "mill.0"

; the resident loop's next effect page: JR $ (the harness stops here)
        ORG #C000,pgeffects+1
        JR $

; thelinkm: the music (TurboSound FM): init #C000, play #C005
        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ObjTab for SAVEOBJ4 (ALASM only): name + type (9 bytes), address, length, #7FFD page, #C000 (start / more type
; letters). The table has no end mark of its own: SAVEOBJ4 starts with NOP, a 0 byte, which ends it.
        ORG end
ObjTab
        DB 'BABAZX  C'
        DW #C000
        DW LENBABAZX
        DB pgbabazx
        DW #C000

        DB 'BABAGS  C'
        DW #C000
        DW LENBABAGS
        DB pgbabags
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
; A later, unfinished take on baba4 (BABAGS differs): not part of the demo, saved under other names.
        SLOT 3
        PAGE pgbabazx
        SAVEBIN "baba5.BABAZX",#C000,LENBABAZX
        PAGE pgbabags
        SAVEBIN "baba5.BABAGS",#C000,LENBABAGS
