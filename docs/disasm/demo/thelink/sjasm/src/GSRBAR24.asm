; =====================================================================================================================
; GSRBAR24 - "rotating bars" (The Link, Alone Coder 2009; Pentagon 1024 + NeoGS)
; =====================================================================================================================
; The picture "gameover" (a 6912-byte ZX screen) arrives as 12 horizontal bars, 16 lines each, bottom bar first.
; Each bar swings about its fixed lower corner (alternately the left and the right one, bar0left) from angle 70
; (of 0..127 = 0..pi/4, about 24.6 degrees) down to 0 in steps of 2 per frame, and lands in its place; the
; finished picture is then left on screen for the next part (multi-bars cuts it up). The ZX shows it in the
; Pentagon 16-colour mode (#EFF7 bit 0): 2 pixels per byte, 128 bytes per line, spread over two RAM pages and two
; #2000 halves of each.
;
; Objects (saved by the SAVEBIN block at the end; this source's own page numbers, a test set-up):
;   ROTBARZX  #C000, page pgeffects (#B0)   ZX half: copies itself to #7800 (ROTBARCODE) and runs there
;   ROBPREGS  #C000, page pgcode1  (#52)    card precalc: runs once on the card, builds the 12 bar bitmaps
;   ROTBARGS  #C000, page pgcode2  (#55)    card effect: renders every frame as (address, byte) lists
; In the demo (THELINK.asm) the same binaries load to #B2 (ROTBARZX), #55 (ROBPREGS) and #32 (ROTBARGS); all three
; are position-independent at #C000 of whatever page holds them (they copy their real code elsewhere first).
;
; How the halves cooperate:
;   - Card (ROTBARGS at #5900): per frame BAR renders the current bar at angle BARa into "out" lists of 4-byte
;     records [-, pixel byte, screen address] and BARGENBLACK makes "clr" lists [screen address] that erase what
;     the same buffer drew two frames ago. Two list sets (BARdata at #2B00 and #0100) alternate, like the ZX's two
;     screens. Lists 0 / 1 = the two RAM pages of the 16-colour screen (X parity).
;   - ZX (ROTBARZX at #7800): per frame sends GS command 0 (frame shown: card stops the DMA), flips the screens,
;     sends command 1 (card points the ZX-DMA at the finished list set), then VIEWBAR reads the header and the four
;     lists through ZX-DMA - SP / HL pointing at #00xx, every read answered by the card with the next list byte -
;     and pokes the bytes into the hidden screen. Between the lists the ZX forces an NMI on the card (FORCENMI),
;     whose handler at card #0066 re-points the DMA at the next list. Command 2 ends the effect.
;   - Card precalc (ROBPREGS at #5900): converts each 16-line strip of the picture into a 256x16 bitmap of
;     16-colour bytes (one byte per picture pixel, the pixel doubled), one card page per bar (#2A and the 11 after it).
;
; The block at #7800 (GO ... end) is a stand-alone test start, not part of any object: a copy of the demo's GO
; (THELINK.asm) cut down to this effect - resets the card, sends it GSPROG, uploads one data page and the two code
; pages by ZX-DMA (commands 3/4/5), installs the resident loop and the IM 2 handler, starts the music
; (thelinkm) and runs effect page #B0 from frame 8 to 568 (zxtimings); page #B1 holds JR $.
;
; Data: gameover (the picture, twice: in ROBPREGS for the card, in ROTBARZX for the final plain screen),
; sectg (128/COS a and 256*TAN a, a = 0..PI/4) and sin+ (127*SIN at half steps), page-aligned in ROTBARGS.
; Includes: gsports.asm (NeoGS ports, GS command macros SC/SD/WC/WD/WN/GD), GSRBARP3.asm (the bar renderer),
; SAVEOBJ4.asm (the author's ALASM object saver; harmless here).
;
; Converter additions (unreal-asm, ALASM -> sjasmplus): the DEVICE line; INCLUDE "gsports",#C5 -> "gsports.asm";
; "low"/"high" for ALASM's .x / 'x; `low IMER` ("ALASM keeps the low byte"); ALASM mnemonics (JNZ/JC/JNC -> JR,
; HX/LX/HY/LY -> IXH/IXL/IYH/IYL, EXA -> EX AF,AF'); the macros MPICTOBARLIE and MPICTOBARSTAY, which glue a
; parameter into label names, expanded at their calls (their ALASM text is kept as comments); the SAVEBIN block
; at the end. The converter's comment lines are marked "(expanded at its calls)" / "macro ... expanded".
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
border=0                                ; 1 = show the ZX frame time in the border (debug)
barhgt=16                               ; bar height in lines; 12 bars x 16 = 192
barblack=0                              ; 1 = black margin rows before the bitmaps (needs BARSTAYBLACK, commented out below)

; --- page plan of this test set-up (#7FFD values); the demo's own plan is in THELINK.asm
;GS data in ZX memory:
pgdata1=#50
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

ROTBARCODE=#7800                        ; the ZX half runs here (free once the resident loop runs)

; --- resident ZX area, the same addresses as in THELINK.asm (resbuf: MOVEPAGE buffer, IMVEC: IM 2 table,
; curpg: last #7FFD value, timer: frame counter, ZXRESIDENT: effect loop, IMER: IM 2 handler, outimstack:
; the handler's own stack)
resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports.asm"
; --- card memory: GSPROG = the card program (from the ZX by GS commands #18/#19), stack below it; the precalc
; and the effect both copy their code to #5900 (they run one after the other)
GSPROG=#5830
GSSTACK=#5830
GSROTBARPRECODE=#5900
GSROTBARCODE=#5900

gspgcode=#10                            ; card page of the first code page (DMA upload, command 4)
gspgdata=#20                            ; card page of the first data page (DMA upload, command 3)

; --- card variables at #0000: four DMA start addresses for the NMI handler (GSNMIDMA4 = out1 list ...
; GSNMIDMA1 = clr0 list), GSNMIVAR = the pointer it walks down, bar0left = which corner the bar swings about
;адреса в NGS: (en: addresses in the NGS)
GSNMIDMA4=#0000
GSNMIDMA3=GSNMIDMA4+2
GSNMIDMA2=GSNMIDMA3+2
GSNMIDMA1=GSNMIDMA2+2
GSNMIDMABEG=GSNMIDMA1+2
GSNMIVAR=#0008                  ;содержит GSNMIDMAn (en: holds a GSNMIDMAn address)
bar0left=#000A                  ;0=падает слева (en: 0 = falls on the left)

gspgbarpic=#2A                  ;12 страниц (en: 12 pages: card page #2A and the 11 after it, one per bar)

; --- inside a bar page, mapped at #8000 by MPAG (#C000 during the precalc, hence +#4000 there): the picture
; (only in page #2A), the pixel conversion table, and the two bitmaps of the bar (256 bytes x barhgt rows)
BARPIC=#8000                            ; gameover copied here by the precalc (page #2A only)
BARPICATTR=BARPIC+#1800
TPICTOBMP=#9D00                 ;,256 %lrlrlrlr->rlrrrlll (en: 256-byte table: two pixels with interleaved bits -> the 16-colour byte layout)
;BARSTAYBLACK=#9E00 ;вплоть до BARSTAYBMP (en: up to BARSTAYBMP)
BARSTAYBMP=#9F00                ;,barhgt*256 (нельзя 0, #100) (en: bitmap for the nearly vertical case; barhgt*256 bytes (must not be at 0, #100))
BARLIEBLACK=#AF00               ;вплоть до BARLIEBMP (en: up to BARLIEBMP)
BARLIEBMP=#B000                 ;,barhgt*256, а перед ним строка черного (en: bitmap for the nearly horizontal case; barhgt*256, with a black row before it)
; --- a list set (two of them, at BARdata and BARdata-bardatadelta = #0100): a 20-byte header, then four lists
; growing down. Header words: record counts clr0, out0, clr1, out1 (the ZX POPs these four), the clr/out
; "form" bytes (0 = 1-pixel records, 1 = 2x2 chunks; the ZX no longer reads them), and the lowest address of each
; list (where the DMA starts). clr records: 2 bytes (address); out records: 4 bytes (-, byte, address).
BARdata=#2B00
BARdata_clr0=0
BARdata_out0=2
BARdata_clr1=4
BARdata_out1=6
BARdata_clrform=8+1
BARdata_outform=10+1
BARdata_clr0addr=12
BARdata_out0addr=14
BARdata_clr1addr=16
BARdata_out1addr=18
;адреса должны делиться на 128: (en: addresses must be divisible by 128)
BARclr0=#2F80                   ;растет вниз (#400+) (en: grows down (#400+ bytes))
BARout0=#4000                   ;растет вниз (#1000+) ;нельзя пересекать границу (en: grows down (#1000+); must not cross the window boundary)
BARclr1=#4480                   ;растет вниз (#400+)                       ;окон (en: grows down (#400+))
BARout1=#5500                   ;растет вниз (#1000+) (en: grows down (#1000+))
endBARdata=BARout1
bardatadelta=#2A00                      ; distance between the two list sets
GSBARSCR1=BARdata                       ; list set 1 (#2B00) and list set 2 (#0100)
GSBARSCR2=BARdata-bardatadelta

; setpga: page A in at #C000 and remember it in curpg (the IM 2 handler restores it)
        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; Test start at #7800 (not saved): the demo's GO cut down to this effect. Card: hard reset, the card program
; GSPROG sent byte by byte (GS commands #18/#19), started by #13; then ZX-DMA upload of the pages, resident loop,
; IM 2, music, and the effect page.
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
;зачем-то POKE #4092,0 (en: for some reason POKE #4092,0 - GS command #18 sets the address, #19 writes the byte)
        LD DE,#4092
        SD E
        SC #18
        WC
        SD D
        WD
        SD 0
        SC #19
        WC
;зачем-то #38 (Load FX) (en: for some reason #38 (Load FX))
        SC #38
        WC
;зачем-то #D2 (Close Stream) (en: for some reason #D2 (Close Stream))
        SC #D2
        WC

        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER (en: as in RIFF TRACKER: #18 = address (low in data, high via data), #19 = byte)
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
;зачем-то #F5 (Busy on) (en: for some reason #F5 (Busy on))
        SC #F5
        WC
;#13 Jump to Address (en: start GSPROGGO on the card)
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

; clear the pixel areas (#C000-#D7FF, #E000-#F7FF) of pages 4 and 5
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
;шлем страницы данных в GS: (en: send the data pages to the GS (a ZX write to #0000-#3FFF is taken by ZX-DMA))
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения: (en: and wait for the switch)
        WN
        GD
;
;шлем страницы кода в GS: (en: send the code pages to the GS)
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,pgcode2
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA: (en: and wait until the DMA is off)
        WN
        GD
;

; resident loop to #BF04, IM 2 handler to #BFBF; IM 2 table #BE00-#BF00 = 257 bytes of #BF (vector #BFBF for any
; bus value)
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
        setpga                  ;заполняет curpg (en: fills curpg)
        CALL #C000                      ; music init

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects                  ; effect pages from #B0, their start/end frames from zxtimings
        LD HL,zxtimings
        JP ZXRESIDENTGO

; MOVEPAGE (copy pages between RAM pages through resbuf): the demo's macro, unused in this effect
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; GSDMASENDPG: page A in, LDIR its 16K to #0000: the ZX ROM ignores the writes, ZX-DMA stores them on the card
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; CL1800PG: page A in, zero #C000-#D7FF and #E000-#F7FF (the pixel areas of a 16-colour screen page)
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
; GSPROG: the card program, assembled for card address #5830 (DISP) and sent there by GS commands #18/#19.
; Silences the 8 channels, sets 24 MHz + NOROM + EXPAG, MPAG 1 / MPAGEX 2, selects DMA module 1 (ZX-DMA), then
; serves commands 3 (DMA on at card page gspgdata), 4 (DMA at gspgcode), 5 (DMA off) - the ZX writes the pages
; in between - and runs the code pages one after another (GSRESIDENTGO).
; =====================================================================================================================
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

        IN A,(GSCFG0)                   ; 24 MHz, no ROM, extended paging
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO
        OR C_24MHZ|M_NOROM|M_EXPAG
        OUT (GSCFG0),A

        LD A,1                  ;#8000: gspg1:0
        OUT (MPAG),A
        LD A,2                  ;#C000: gspg2:0
        OUT (MPAGEX),A

        LD A,1                  ;SET DMA MODULE #1
        OUT (DMA_MOD),A

; wait for command 3
        LD E,3
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

; card page -> DMA address: HAD = page bits 6..1 (A21..A16), MAD bit 7 = page bit 0 (A15), MAD bit 6 = page
; bit 7 (A14), LAD = 0. So bit 7 of a page number is its lowest 16K bit (see RLCA : INC A : RRCA below)
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)

        LD E,5
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX: DMA OFF)
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: is the ZX ready? (waits until the ZX has read the answer))

        LD A,gspgcode

; the card's resident loop: map each code page at #C000 (MPAGEX) and CALL it; RLCA : INC A : RRCA = next 16K page
; in the card's page numbering (bit 7 is the low bit). Here: ROBPREGS, then ROTBARGS
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
; ZX resident loop (to #BF04): for each effect page from A: page in (OR #10 = 48K ROM), take its start frame (BC)
; and end frame (DE) from the table at HL, CALL #C000, next page.
; RESMOVEPAGE: copy IYH 256-byte blocks from page IXH (HL) to page IXL (DE) through resbuf (MOVEPAGE's body).
; =====================================================================================================================
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
        setpga                  ;портит BC (en: corrupts BC)
        LD BC,256
        LDIR
        POP HL
        INC H
        DEC IYH
        JR NZ,MOVEPAGE0
        RET
; start and end frame of each effect page: here only rotating bars, 560 frames as in the demo (2688-3248)
zxtimings
        DW 8,8+560              ;rotbar
        ENT
LENZXRES=$-WASZXRES

; =====================================================================================================================
; IMER, the IM 2 handler (to #BFBF): frame counter, music player (#C005 of the music page, keeping curpg's screen
; and ROM bits 3-4), the current page back. Runs on its own stack (outimstack) and returns with JP, so the
; interrupted code may have SP anywhere: the 2 bytes the interrupt pushed get the interrupted DE written over the
; return address, so a PUSH DE fill with SP in the screen stays intact.
; =====================================================================================================================
WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата (en: write DE in place of the return address)
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

; =====================================================================================================================
; ROBPREGS - card precalc, at #C000 of a card code page. Copies its code to #5900, copies the picture to #8000
; of card page #2A (MPAG) and builds, for each of the 12 bars (card pages #2A.., mapped at #C000 by MPAGEX), the
; two bitmaps BARLIEBMP and BARSTAYBMP from the bar's 16 picture lines, mirrored for every other bar
; (bar0left toggles, as in the effect).
; =====================================================================================================================
        ORG #C000,pgcode1
pgrotbarpregs=pgcode1
        LD HL,WASGSROTBARPRECODE
        LD DE,GSROTBARPRECODE
        LD BC,LENGSROTBARPRECODE
        LDIR

        LD A,gspgbarpic
        OUT (MPAG),A

        LD HL,wasscr
        LD DE,BARPIC
        LD BC,#1B00
        LDIR
        JP GSROTBARPREGO

WASGSROTBARPRECODE
        DISP GSROTBARPRECODE
; GSROTBARPREGO: HL = #90C0 = picture line 176 (the top of the bottom bar); each next bar 16 lines up (L - 64,
; borrow into the previous third)
GSROTBARPREGO
        CALL GENPICTOBMP

        XOR A
        LD (bar0left),A

;все копии делаем из экрана, лежащего в #8000 (en: all copies are made from the screen lying at #8000)
        LD HL,#10C0+BARPIC
        LD C,gspgbarpic
        LD B,12
GSBARPREP
        LD A,C
        OUT (MPAGEX),A
        RLCA
        INC A
        RRCA
        LD C,A
        PUSH BC
        PUSH HL
        PUSH HL
        LD DE,BARLIEBMP+#4000
        CALL PICTOBARLIE
        POP HL
        LD DE,BARSTAYBMP+#4000
        CALL PICTOBARSTAY
; the black row before BARLIEBMP: note it is cleared at #AF00 in the #8000 window (page #2A) every time, not in
; the bar's own page at #EF00
        LD HL,BARLIEBLACK
        LD DE,BARLIEBLACK+1
        LD BC,BARLIEBMP-BARLIEBLACK-1
        LD (HL),L
        LDIR
        IF barblack
        LD HL,BARSTAYBLACK
        LD DE,BARSTAYBLACK+1
        LD BC,BARSTAYBMP-BARSTAYBLACK-1
        LD (HL),L
        LDIR
        ENDIF
        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A
        POP HL
        POP BC
        LD A,L
        SUB 64
        LD L,A
        JR NC,$+6
        LD A,H
        SUB 8
        LD H,A
        DJNZ GSBARPREP
        RET

; GENPICTOBMP: the 256-byte table TPICTOBMP. Index = 8 bits of two pixels interleaved (%lrlrlrlr: left and right
; pixel's 4 colour bits), value = the same bits in the 16-colour screen byte order (%rlrrrlll)
GENPICTOBMP
        LD HL,TPICTOBMP         ;%lrlrlrlr->rlrrrlll
GENPICTOBMP0
        DUP 4
        RLC L
        RLA                     ;будет %????llll (en: becomes %????llll)
        RLC L
        RL C                    ;будет %????rrrr (en: becomes %????rrrr)
        EDUP
        DUP 3
        RRA
        RR (HL)
        EDUP
        DUP 3
        RR C
        RR (HL)
        EDUP
        RRA
        RR (HL)
        RR C
        RR (HL)
        INC L
        JR NZ,GENPICTOBMP0
        RET

; PICTOBARLIE: HL = picture address of the bar's top line, DE = bitmap (BARLIEBMP+#4000). Each picture pixel
; becomes one bitmap byte (the pixel doubled): ink or paper colour of its attribute (+ bright), through TPICTOBMP.
; BC = TPICTOBMP + row counter (C = barhgt). IXH / IXL = paper / ink colour bits, IYL = pixel counter
PICTOBARLIE
;генерируем битмап 256x16 - по 2 пикс от каждого пикс картинки (en: generate a 256x16 bitmap - 2 pixels from each picture pixel)
;0-й байт строки соответствует концу, 1,2,3... - началу (en: byte 0 of a row is the end, bytes 1,2,3... the beginning)
        LD BC,TPICTOBMP+barhgt
        LD A,(bar0left)
        OR A
        JP Z,PICTOBARLIELEFT
        JP PICTOBARLIERIGHT

; the author's macro (ALASM text, not assembled): \0 = label suffix, \1 = 1 for LEFT, 0 for RIGHT. The ";left"
; comments name the parameter: IFN \1 is the left variant, IF0 \1 the right one. Expansions follow.
; (expanded at its calls)        MACRO MPICTOBARLIE
; (expanded at its calls) PICTOBARLIE\0
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         LD A,L
; (expanded at its calls)         OR 31
; (expanded at its calls)         LD L,A
; (expanded at its calls)        ENDIF
; (expanded at its calls) PICTOBAR0\0
; (expanded at its calls)        PUSH BC
; (expanded at its calls)        PUSH HL
; (expanded at its calls)         LD C,0 ;%l0l0l0l0
; (expanded at its calls) PICTOBAR1\0
; (expanded at its calls)        PUSH HL
; (expanded at its calls)         LD A,H
; (expanded at its calls)         RRA
; (expanded at its calls)         RRA
; (expanded at its calls)         RRA
; (expanded at its calls)         AND 3
; (expanded at its calls)         ADD A,'BARPICATTR
; (expanded at its calls)         LD H,A
; (expanded at its calls)         XOR A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLC (HL) ;bright
; (expanded at its calls)         RLA ;%0000000b
; (expanded at its calls)         PUSH AF
; (expanded at its calls)        DUP 3
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLA ;будет %0b0p0p0p
; (expanded at its calls)        EDUP
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         ADD A,A
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD HX,A
; (expanded at its calls)         POP AF
; (expanded at its calls)        DUP 3
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLA ;будет %0b0i0i0i
; (expanded at its calls)        EDUP
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         ADD A,A
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD LX,A
; (expanded at its calls)        POP HL
; (expanded at its calls)         LD LY,8
; (expanded at its calls) PICTOBAR2\0
; (expanded at its calls)         LD A,C
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         AND %01010101
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)        ELSE
; (expanded at its calls)         AND %10101010
; (expanded at its calls)         RRA
; (expanded at its calls)         RRC (HL)
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JC $+6
; (expanded at its calls)         OR HX ;paper %0p0p0p0p ;right: %p0p0p0p0
; (expanded at its calls)         JR $+4
; (expanded at its calls)         OR LX ;ink %0i0i0i0i   ;left: %i0i0i0i0
; (expanded at its calls)         LD C,A ;%lrlrlrlr
; (expanded at its calls)         LD A,(BC) ;rlrrrlll
; (expanded at its calls)         LD (DE),A
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E
; (expanded at its calls)        ENDIF
; (expanded at its calls)         DEC LY
; (expanded at its calls)         JNZ PICTOBAR2\0
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC L
; (expanded at its calls)         DEC E
; (expanded at its calls)         INC E
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC L
; (expanded at its calls)         INC E
; (expanded at its calls)         DEC E
; (expanded at its calls)        ENDIF
; (expanded at its calls)         JP NZ,PICTOBAR1\0
; (expanded at its calls) ;черный пиксель в конце строки битмапа
; (expanded at its calls)         LD A,C
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         AND %01010101
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         LD C,A ;%lrlrlrlr
; (expanded at its calls)        ELSE
; (expanded at its calls)         AND %10101010
; (expanded at its calls)         RRA
; (expanded at its calls)         LD C,A ;%lrlrlrlr
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD A,(BC) ;rlrrrlll
; (expanded at its calls)         LD (DE),A
; (expanded at its calls)        POP HL
; (expanded at its calls)         INC D
; (expanded at its calls)         CALL GSDHL
; (expanded at its calls)        POP BC
; (expanded at its calls)         DEC C
; (expanded at its calls)         JP NZ,PICTOBAR0\0
; (expanded at its calls)         RET
; (expanded at its calls)        ENDM

; LEFT (bar0left = 0): picture row read left to right (RLC (HL)), bitmap bytes written upwards from E = 0
; macro MPICTOBARLIE expanded
PICTOBARLIELEFT
        IF (1)==0               ;left
        LD A,L
        OR 31
        LD L,A
        ENDIF
PICTOBAR0LEFT
        PUSH BC
        PUSH HL
        LD C,0                  ;%l0l0l0l0
PICTOBAR1LEFT
        PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,high BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL)                ;bright
        RLA                     ;%0000000b
        PUSH AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0p0p0p (en: becomes %0b0p0p0p: bright + paper)
        EDUP
        IF (1)==0               ;left
        ADD A,A
        ENDIF
        LD IXH,A
        POP AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0i0i0i (en: becomes %0b0i0i0i: bright + ink)
        EDUP
        IF (1)==0               ;left
        ADD A,A
        ENDIF
        LD IXL,A
        POP HL
        LD IYL,8
PICTOBAR2LEFT
        LD A,C
        IF 1                    ;left
        AND %01010101
        ADD A,A
        RLC (HL)
        ELSE
        AND %10101010
        RRA
        RRC (HL)
        ENDIF
        JR C,$+6
        OR IXH                  ;paper %0p0p0p0p ;right: %p0p0p0p0
        JR $+4
        OR IXL                  ;ink %0i0i0i0i   ;left: %i0i0i0i0
        LD C,A                  ;%lrlrlrlr
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        IF 1                    ;left
        INC E
        ELSE
        DEC E
        ENDIF
        DEC IYL
        JR NZ,PICTOBAR2LEFT
        IF 1                    ;left
        INC L
        DEC E
        INC E
        ELSE
        DEC L
        INC E
        DEC E
        ENDIF
        JP NZ,PICTOBAR1LEFT
;черный пиксель в конце строки битмапа (en: black pixel at the end of the bitmap row)
        LD A,C
        IF 1                    ;left
        AND %01010101
        ADD A,A
        LD C,A                  ;%lrlrlrlr
        ELSE
        AND %10101010
        RRA
        LD C,A                  ;%lrlrlrlr
        ENDIF
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        POP HL
        INC D
        CALL GSDHL
        POP BC
        DEC C
        JP NZ,PICTOBAR0LEFT
        RET
; RIGHT (bar0left = 1): starts at the row's last byte (OR 31) and reads right to left (RRC (HL)), bitmap bytes
; written downwards; same comments as LEFT
; macro MPICTOBARLIE expanded
PICTOBARLIERIGHT
        IF (0)==0               ;left
        LD A,L
        OR 31
        LD L,A
        ENDIF
PICTOBAR0RIGHT
        PUSH BC
        PUSH HL
        LD C,0                  ;%l0l0l0l0
PICTOBAR1RIGHT
        PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,high BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL)                ;bright
        RLA                     ;%0000000b
        PUSH AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0p0p0p
        EDUP
        IF (0)==0               ;left
        ADD A,A
        ENDIF
        LD IXH,A
        POP AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0i0i0i
        EDUP
        IF (0)==0               ;left
        ADD A,A
        ENDIF
        LD IXL,A
        POP HL
        LD IYL,8
PICTOBAR2RIGHT
        LD A,C
        IF 0                    ;left
        AND %01010101
        ADD A,A
        RLC (HL)
        ELSE
        AND %10101010
        RRA
        RRC (HL)
        ENDIF
        JR C,$+6
        OR IXH                  ;paper %0p0p0p0p ;right: %p0p0p0p0
        JR $+4
        OR IXL                  ;ink %0i0i0i0i   ;left: %i0i0i0i0
        LD C,A                  ;%lrlrlrlr
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        IF 0                    ;left
        INC E
        ELSE
        DEC E
        ENDIF
        DEC IYL
        JR NZ,PICTOBAR2RIGHT
        IF 0                    ;left
        INC L
        DEC E
        INC E
        ELSE
        DEC L
        INC E
        DEC E
        ENDIF
        JP NZ,PICTOBAR1RIGHT
;черный пиксель в конце строки битмапа
        LD A,C
        IF 0                    ;left
        AND %01010101
        ADD A,A
        LD C,A                  ;%lrlrlrlr
        ELSE
        AND %10101010
        RRA
        LD C,A                  ;%lrlrlrlr
        ENDIF
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        POP HL
        INC D
        CALL GSDHL
        POP BC
        DEC C
        JP NZ,PICTOBAR0RIGHT
        RET

; PICTOBARSTAY: the same conversion into BARSTAYBMP, laid out for the nearly vertical case (BARSTAY in
; GSRBARP3): the 16 picture lines become the 16 bitmap rows column by column (INC D per line). That case is
; never reached with this effect's angles (see GSGOBEG)
PICTOBARSTAY
;генерируем битмап 256x16 - по 2 пикс от каждого пикс картинки (en: generate a 256x16 bitmap - 2 pixels from each picture pixel)
        LD C,32
        LD A,(bar0left)
        OR A
        JP Z,PICTOBARSTAYLEFT
        JP PICTOBARSTAYRIGHT

; the author's macro (not assembled), parameters as in MPICTOBARLIE; expansions follow
; (expanded at its calls)        MACRO MPICTOBARSTAY
; (expanded at its calls) PICTOBARSTAY\0
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         DEC E ;#XXFF
; (expanded at its calls)        ENDIF
; (expanded at its calls) PICTOBARSTAY0\0
; (expanded at its calls)         LD B,8
; (expanded at its calls) PICTOBARSTAYCHR\0
; (expanded at its calls)        PUSH BC
; (expanded at its calls)        PUSH DE
; (expanded at its calls)        PUSH HL
; (expanded at its calls)         LD BC,TPICTOBMP ;C=%l0l0l0l0
; (expanded at its calls)         LD LY,barhgt
; (expanded at its calls) PICTOBARSTAY1\0
; (expanded at its calls)        PUSH HL
; (expanded at its calls)         LD A,H
; (expanded at its calls)         RRA
; (expanded at its calls)         RRA
; (expanded at its calls)         RRA
; (expanded at its calls)         AND 3
; (expanded at its calls)         ADD A,'BARPICATTR
; (expanded at its calls)         LD H,A
; (expanded at its calls)         XOR A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLC (HL) ;bright
; (expanded at its calls)         RLA ;%0000000b
; (expanded at its calls)         PUSH AF
; (expanded at its calls)        DUP 3
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLA ;будет %0b0p0p0p
; (expanded at its calls)        EDUP
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         ADD A,A
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD HX,A
; (expanded at its calls)         POP AF
; (expanded at its calls)        DUP 3
; (expanded at its calls)         ADD A,A
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         RLA ;будет %0b0i0i0i
; (expanded at its calls)        EDUP
; (expanded at its calls)        IF0 \1 ;left
; (expanded at its calls)         ADD A,A
; (expanded at its calls)        ENDIF
; (expanded at its calls)         LD LX,A
; (expanded at its calls)        POP HL
; (expanded at its calls)         LD A,C
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         AND %01010101
; (expanded at its calls)         ADD A,A
; (expanded at its calls)        ELSE
; (expanded at its calls)         AND %10101010
; (expanded at its calls)         RRA
; (expanded at its calls)        ENDIF
; (expanded at its calls)         RLC (HL)
; (expanded at its calls)         JC $+6
; (expanded at its calls)         OR HX ;paper %0p0p0p0p
; (expanded at its calls)         JR $+4
; (expanded at its calls)         OR LX ;ink %0i0i0i0i
; (expanded at its calls)         LD C,A ;%lrlrlrlr
; (expanded at its calls)         LD A,(BC) ;rlrrrlll
; (expanded at its calls)         LD (DE),A
; (expanded at its calls)         INC D
; (expanded at its calls)         CALL GSDHL
; (expanded at its calls)         DEC LY
; (expanded at its calls)         JP NZ,PICTOBARSTAY1\0
; (expanded at its calls)        POP HL
; (expanded at its calls)        POP DE
; (expanded at its calls)        IFN \1 ;left
; (expanded at its calls)         INC E
; (expanded at its calls)        ELSE
; (expanded at its calls)         DEC E
; (expanded at its calls)        ENDIF
; (expanded at its calls)        POP BC
; (expanded at its calls)         DEC B
; (expanded at its calls)         JP NZ,PICTOBARSTAYCHR\0
; (expanded at its calls)         INC L
; (expanded at its calls)         DEC C
; (expanded at its calls)         JP NZ,PICTOBARSTAY0\0
; (expanded at its calls)         RET
; (expanded at its calls)        ENDM

; LEFT
; macro MPICTOBARSTAY expanded
PICTOBARSTAYLEFT
        IF (1)==0               ;left
        DEC E                   ;#XXFF
        ENDIF
PICTOBARSTAY0LEFT
        LD B,8
PICTOBARSTAYCHRLEFT
        PUSH BC
        PUSH DE
        PUSH HL
        LD BC,TPICTOBMP         ;C=%l0l0l0l0
        LD IYL,barhgt
PICTOBARSTAY1LEFT
        PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,high BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL)                ;bright
        RLA                     ;%0000000b
        PUSH AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0p0p0p
        EDUP
        IF (1)==0               ;left
        ADD A,A
        ENDIF
        LD IXH,A
        POP AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0i0i0i
        EDUP
        IF (1)==0               ;left
        ADD A,A
        ENDIF
        LD IXL,A
        POP HL
        LD A,C
        IF 1                    ;left
        AND %01010101
        ADD A,A
        ELSE
        AND %10101010
        RRA
        ENDIF
        RLC (HL)
        JR C,$+6
        OR IXH                  ;paper %0p0p0p0p
        JR $+4
        OR IXL                  ;ink %0i0i0i0i
        LD C,A                  ;%lrlrlrlr
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        INC D
        CALL GSDHL
        DEC IYL
        JP NZ,PICTOBARSTAY1LEFT
        POP HL
        POP DE
        IF 1                    ;left
        INC E
        ELSE
        DEC E
        ENDIF
        POP BC
        DEC B
        JP NZ,PICTOBARSTAYCHRLEFT
        INC L
        DEC C
        JP NZ,PICTOBARSTAY0LEFT
        RET
; RIGHT (E starts at #FF and goes down)
; macro MPICTOBARSTAY expanded
PICTOBARSTAYRIGHT
        IF (0)==0               ;left
        DEC E                   ;#XXFF
        ENDIF
PICTOBARSTAY0RIGHT
        LD B,8
PICTOBARSTAYCHRRIGHT
        PUSH BC
        PUSH DE
        PUSH HL
        LD BC,TPICTOBMP         ;C=%l0l0l0l0
        LD IYL,barhgt
PICTOBARSTAY1RIGHT
        PUSH HL
        LD A,H
        RRA
        RRA
        RRA
        AND 3
        ADD A,high BARPICATTR
        LD H,A
        XOR A
        RLC (HL)
        RLC (HL)                ;bright
        RLA                     ;%0000000b
        PUSH AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0p0p0p
        EDUP
        IF (0)==0               ;left
        ADD A,A
        ENDIF
        LD IXH,A
        POP AF
        DUP 3
        ADD A,A
        RLC (HL)
        RLA                     ;будет %0b0i0i0i
        EDUP
        IF (0)==0               ;left
        ADD A,A
        ENDIF
        LD IXL,A
        POP HL
        LD A,C
        IF 0                    ;left
        AND %01010101
        ADD A,A
        ELSE
        AND %10101010
        RRA
        ENDIF
        RLC (HL)
        JR C,$+6
        OR IXH                  ;paper %0p0p0p0p
        JR $+4
        OR IXL                  ;ink %0i0i0i0i
        LD C,A                  ;%lrlrlrlr
        LD A,(BC)               ;rlrrrlll
        LD (DE),A
        INC D
        CALL GSDHL
        DEC IYL
        JP NZ,PICTOBARSTAY1RIGHT
        POP HL
        POP DE
        IF 0                    ;left
        INC E
        ELSE
        DEC E
        ENDIF
        POP BC
        DEC B
        JP NZ,PICTOBARSTAYCHRRIGHT
        INC L
        DEC C
        JP NZ,PICTOBARSTAY0RIGHT
        RET

; GSDHL: HL = next line down in ZX screen layout
GSDHL
        INC H
        LD A,H
        AND 7
        RET NZ
        LD A,L
        ADD A,32
        LD L,A
        RET C
        LD A,H
        SUB 8
        LD H,A
        RET
        ENT
LENGSROTBARPRECODE=$-WASGSROTBARPRECODE
        DISPLAY /T,LENGSROTBARPRECODE
; the picture (a 6912-byte ZX screen), copied to #8000 of card page #2A by the start code above
wasscr
        INCBIN "gameover"
LENROTBARPREGS=$-#C000

; =====================================================================================================================
; ROTBARGS - the card effect, at #C000 of a card code page; copies itself to #5900 (GSROTBARGO).
; Card memory while it runs: #0000-#000A NMI variables, #0066 GSNMI, #0100 and #2B00 the two list sets, #5900
; this code, #8000 the current bar's page (MPAG), #C000 card page 2 (MPAGEX) = the screen address table.
; =====================================================================================================================
        ORG #C000,pgcode2
pgrotbargs=pgcode2
        LD HL,WASGSROTBARCODE
        LD DE,GSROTBARCODE
        LD BC,LENGSROTBARCODE
        LDIR
        JP GSROTBARGO

WASGSROTBARCODE
        DISP GSROTBARCODE
; GSROTBARGO: install the NMI handler at #0066, build the address table, then for each bar (D = Y of its bottom
; line, from #BF up by 16; L = its card page, mapped at #8000): clear both headers, BARa = 70, one frame per angle
; 70, 68 ... 0 (GSLOOP), two more frames, two frames of the final position without erasing; then the next bar
; on the other corner. After the 12th bar: redraw the last position until the ZX sends command 2.
; BARa starts at 70 < 128 and 256*TAN(70) = 117 < 128, so only BARLIE and BARHORIZ of GSRBARP3 ever run here;
; BARDIAG, BARSTAY and the BARSTAYBMP bitmaps are not used with these angles.
GSROTBARGO
        LD HL,GSNMI
        LD DE,#0066
        LD BC,GSNMI_LEN
        LDIR

        LD A,2                  ;#C000: gspg2:0
        OUT (MPAGEX),A
        CALL GENBARSCRADDR

        XOR A
        LD (bar0left),A

        LD L,gspgbarpic
        LD DE,#BF80
GSGOBEG
        LD A,L
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        LD L,A
        PUSH DE
        PUSH HL
        LD (BARY),DE
        LD HL,0
        LD (BARdata+BARdata_clr0),HL
        LD (BARdata+BARdata_out0),HL
        LD (BARdata+BARdata_clr1),HL
        LD (BARdata+BARdata_out1),HL
        XOR A
        LD (BARdata+BARdata_clrform),A
        LD (BARdata+BARdata_outform),A
        LD (BARdata+BARdata_clr0-bardatadelta),HL
        LD (BARdata+BARdata_out0-bardatadelta),HL
        LD (BARdata+BARdata_clr1-bardatadelta),HL
        LD (BARdata+BARdata_out1-bardatadelta),HL
        LD (BARdata+BARdata_clrform-bardatadelta),A
        LD (BARdata+BARdata_outform-bardatadelta),A
        LD A,70                 ;254 (en: start angle; 254 would start in the nearly vertical range)
        LD (BARa),A
GSLOOP
        CALL GSLOOPPP
        LD HL,BARa
        LD A,(HL)
        SUB 2                   ;3 не успеет стереть (en: a step of 3 would not manage to erase)
        LD (HL),A
        JR NC,GSLOOP
        LD (HL),0
        CALL GSLOOPPP
        CALL GSLOOPPP           ;если последнее было 1 (en: if the last one was 1)
        LD HL,0
        LD (BARdata+BARdata_clr0),HL
        LD (BARdata+BARdata_clr1),HL
        LD (BARdata+BARdata_clr0-bardatadelta),HL
        LD (BARdata+BARdata_clr1-bardatadelta),HL
        CALL GSLOOPPPSCR
        CALL GSLOOPPPU          ;печатаем непропечатавшееся (en: print what did not get printed)
        CALL GSLOOPPPSCR
        CALL GSLOOPPPU
        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A
        POP HL
        POP DE
        LD A,D
        SUB 16
        LD D,A
        JP NC,GSGOBEG

        LD HL,bar0left
        LD A,(HL)
        XOR 1
        LD (HL),A               ;как было (en: as it was)
        CALL GSLOOPPP
        JR $-3

; GSLOOPPPSCR: switch the drawing buffer (self-modified LD A,n) and set BARIX / IX to its list set
GSLOOPPPSCR
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: draw into SCR2)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now draw into SCR1)
;потом дадим его ZX'у (en: later we give it to the ZX)
        LD IX,BARdata
        JR Z,$+6
        LD IX,BARdata-bardatadelta
        LD (BARIX),IX
        RET
; GSLOOPPP: one frame. Swap buffers, make the clear lists (BARGENBLACK), draw the bar (BAR), then the ZX
; handshake: wait for command 0 (ZX shows the last frame): DMA off, answer; wait for a command (65536 polls,
; then GSRESET); 1 = point the DMA at the new header, prepare the four list addresses for GSNMI, DMA on, answer;
; anything else = quit. GSLOOPPPU: the same without the swap and the clear lists
GSLOOPPP
        CALL GSLOOPPPSCR
        CALL BARGENBLACK
GSLOOPPPU
        CALL BAR

;GS ожидает конец кадра у ZX (en: the GS waits for the end of the ZX frame)
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET (en: no timeout for command 0)
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX: DMA OFF)
;GS ожидает обнуления (ZX запросит данные) (en: the GS waits for the zeroing (the ZX will request data))
; (the loop below actually waits for a non-zero command: 1 = next frame, anything else = quit)
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        JR NZ,GSQUIT

; list start addresses for GSNMI; an address at #4000-#7FFF (card CPU) gets bit 15 for the DMA (author: pg1:1)
        LD IX,(BARIX)
        LD L,(IX+BARdata_clr0addr)
        LD H,(IX+BARdata_clr0addr+1)
        BIT 6,H
        JR Z,$+4
        SET 7,H                 ;pg1:1
        LD (GSNMIDMA1),HL
        LD L,(IX+BARdata_out0addr)
        LD H,(IX+BARdata_out0addr+1)
        BIT 6,H
        JR Z,$+4
        SET 7,H                 ;pg1:1
        LD (GSNMIDMA2),HL
        LD L,(IX+BARdata_clr1addr)
        LD H,(IX+BARdata_clr1addr+1)
        BIT 6,H
        JR Z,$+4
        SET 7,H                 ;pg1:1
        LD (GSNMIDMA3),HL
        LD L,(IX+BARdata_out1addr)
        LD H,(IX+BARdata_out1addr+1)
        BIT 6,H
        JR Z,$+4
        SET 7,H                 ;pg1:1
        LD (GSNMIDMA4),HL
        LD HL,GSNMIDMABEG
        LD (GSNMIVAR),HL
; DMA at the header of this list set (HAD = 0: card address = CPU address below #4000)
        XOR A
        OUT (DMA_HAD),A
        LD A,IXH
        OUT (DMA_MAD),A
        LD A,IXL
        OUT (DMA_LAD),A
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)
        RET
; GSQUIT: drops GSLOOPPP's return address and returns from the effect to GSRESIDENTGO. That is only right from
; the final loop above: inside the bar loop GSGOBEG's PUSH DE / PUSH HL are still on the stack, so a command 2
; before the last bar (Space on the ZX) would RET into them
GSQUIT
        POP AF
        RET

        INCLUDE "GSRBARP3.asm"          ; the bar renderer BAR

; BARGENBLACK: IX = list set about to be drawn. Its out lists still hold what it drew two frames ago (on the same
; ZX screen); make the clr lists from them (addresses only), out0 -> clr0, out1 -> clr1. SP is used to write the
; records (PUSH DE downwards), IY holds the return point
BARGENBLACK
;IX=BARdata[-bardatadelta]
        LD (BARGENBLACKSP),SP
        LD A,(IX+BARdata_outform)
        LD (IX+BARdata_clrform),A
        LD IY,BARGENBLACK0Q
        JP BARGENBLACK0PP
BARGENBLACK0Q
        LD IY,BARGENBLACK1Q
        JP BARGENBLACK1PP
BARGENBLACK1Q
BARGENBLACKSP=$+1
        LD SP,0
        RET

BARGENBLACK0PP
        LD E,(IX+BARdata_out0)
        LD D,(IX+BARdata_out0+1)
        LD C,(IX+BARdata_out0addr)
        LD B,(IX+BARdata_out0addr+1)
;DE=N=число записей (!=0) в источнике (en: DE = N = number of records (!=0) in the source)
;(берем из источника последние сгенерированные - начало стека) (en: (take from the source the most recently generated ones - the start of the stack))
        SRL D
        RR E                    ;DE=N=число записей (!=0) сгенерировать (en: DE = N = number of records (!=0) to generate: half of the source)
        LD (IX+BARdata_clr0),E
        LD (IX+BARdata_clr0+1),D
        LD HL,BARclr0
        LD A,BARdata_clr0addr
        JP BARGENBLACKPP
BARGENBLACK1PP
        LD E,(IX+BARdata_out1)
        LD D,(IX+BARdata_out1+1)
        LD C,(IX+BARdata_out1addr)
        LD B,(IX+BARdata_out1addr+1)
;DE=N=число записей (!=0) в источнике (en: as in BARGENBLACK0PP)
;(берем из источника последние сгенерированные - начало стека)
        SRL D
        RR E                    ;DE=N=число записей (!=0) сгенерировать
        LD (IX+BARdata_clr1),E
        LD (IX+BARdata_clr1+1),D
        LD HL,BARclr1
        LD A,BARdata_clr1addr
BARGENBLACKPP
        LD (BARGENBLACKIXd),A
        INC A
        LD (BARGENBLACKIXd1),A
        LD A,D
        OR E
        JP Z,BARGENBLACKQ
        LD A,IXH
        CP high BARdata
        JR Z,$+6
        LD A,H
        SUB high bardatadelta
        LD H,A                  ;второй блок данных (en: second data block)
        LD SP,HL
        LD H,B
        LD L,C
; the loop is DUP 16 x 5 bytes; the JR $ before it is patched to jump into the middle for N mod 16 (Duff's
; device), A = number of passes
        DEC DE
        LD A,E
        CPL
        AND 15                  ;0 (N=0), 15 (N=1), 14 (N=2)...
        LD B,A
        ADD A,A
        ADD A,A
        ADD A,B
        LD (BARGENBLACK0-1),A
        LD A,E
        DUP 4
        SRL D
        RRA
        EDUP
        INC A
        LD BC,3
        DEC HL
        JR $                            ; offset patched above
BARGENBLACK0
        DUP 16
        ADD HL,BC
        LD E,(HL)
        INC L
        LD D,(HL)
        PUSH DE
        EDUP
        DEC A
        JP NZ,BARGENBLACK0
        LD H,A
        LD L,A
        ADD HL,SP
BARGENBLACKIXd=$+2
        LD (IX),L
BARGENBLACKIXd1=$+2
        LD (IX),H
BARGENBLACKQ
        JP (IY)

; GENBARSCRADDR: the screen address table at #C000-#FFFF (card page 2). Row H = #C0 | (63 - line within a
; third), entry at L = X with bit 0 clear (X = byte 0..127 of the 16-colour line; X and X+1 share it): HSB, LSB
; of the ZX address at #C000. Column = X / 4, X bit 1 picks the #E000 half, X bit 0 the RAM page (list 0 / 1);
; the third (#00/#08/#10/#18) is ORed in later (IYH in BARe0ADR / BARe1ADR)
GENBARSCRADDR
        LD HL,#C000             ;HSB,LSB
GENBARSCRADDR0
        LD B,128
GENBARSCRADDR1
        LD A,L
        RRA
        RRA
        AND 31
        LD C,A
        LD A,H
        CPL
        AND #38
        RLCA
        RLCA
        OR C
        LD C,A
        LD A,H
        CPL
        AND 7
        OR #C0
        BIT 1,L
        JR Z,$+4
        OR #20
        LD (HL),A
        INC L
        LD (HL),C
        INC L
        DJNZ GENBARSCRADDR1
        INC H
        JR NZ,GENBARSCRADDR0
        RET

; GSRESET: the ZX stopped answering - DMA off, back to 10 MHz with the ROM, RST 0 (the card firmware restarts)
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

;для #0066 (en: for #0066)
; GSNMI (copied to #0066): the ZX forces an NMI before each list; take the next list address (GSNMIDMA1 first,
; from GSNMIVAR downwards), set the DMA to it and answer the ZX
GSNMI
        PUSH AF
        PUSH HL
        LD HL,(GSNMIVAR)        ;8/6/4/2
;IN A,(ZXDATRD)
;LD L,A
        DEC L
        LD A,(HL)
        OUT (DMA_MAD),A
        DEC L
        LD A,(HL)
        OUT (DMA_LAD),A
        LD (GSNMIVAR),HL
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX: DMA ON)
        POP HL
        POP AF
        RET
GSNMI_LEN=$-GSNMI

        DISPLAY $
; the two tables page-aligned: BARa is the low byte of an address into BARSECTG
        DS low (-$)
BARSECTG
        INCBIN "sectg"                  ; 128 bytes 128/COS a, then 128 bytes 256*TAN a; a = 0..127 = 0..PI/4
BARSIN
        INCBIN "sin+"                   ; 127*SIN((n+.5)/256*2*PI)
        ENT
LENGSROTBARCODE=$-WASGSROTBARCODE
        DISPLAY /T,LENGSROTBARCODE
LENROTBARGS=$-#C000

; =====================================================================================================================
; ROTBARZX - the ZX half, at #C000 of an effect page; called by the resident loop with BC = start frame,
; DE = end frame. Copies itself to #7800 and runs there (#C000 is needed for the screen pages).
; Screens: 16-colour mode, screen 0 = RAM pages 4 + 5, screen 1 = pages 6 + 7 (#7FFD #1C/#1D and #1E/#1F),
; pixel areas #C000-#D7FF and #E000-#F7FF of each page. BARPG0 / BARPG1 = the two pages of the hidden screen.
; =====================================================================================================================
        ORG #C000,pgeffects
pgrotbarzx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASROTBARCODE
        LD DE,ROTBARCODE
        LD BC,LENROTBARCODE
        LDIR
        JP ROTBARCODE

WASROTBARCODE
        DISP ROTBARCODE
        POP HL
        LD (zxendtime),HL
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

; black attributes on both screens, pixel areas of pages 4-7 cleared, 16-colour mode on
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
        LD A,#14
        CALL ZXCL1800PG
        LD A,#15
        CALL ZXCL1800PG
        LD A,#16
        CALL ZXCL1800PG
        LD A,#17
        CALL ZXCL1800PG

        LD A,1                  ;16C (en: 16-colour mode (#EFF7 bit 0))
        LD BC,#EFF7
        OUT (C),A

        HALT
; BIGLOOP, one frame (VIEWBAR returns on the frame interrupt, so no HALT here): command 0, flip screens, wait;
; end frame or Space -> QUIT; command 1, wait; VIEWBAR
BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
;даем знать GS'у, что кадр готов (en: let the GS know the frame is ready)
;и DMA надо выключить: (en: and the DMA must be turned off)
        XOR A
        OUT (GSCOM),A

; #7FFD toggles #17 / #1D: the store patches the XOR's own operand, so A = #0A XOR (last value). #17 shows screen 0
; with page 7 at #C000, #1D shows screen 1 with page 5 at #C000
        LD A,#17^#1D
        XOR #1D                 ;видим scr1, рисуем scr0 (en: we see scr1, draw scr0)
        LD ($-1),A              ;теперь видим scr0, рисуем scr1 (en: now we see scr0, draw scr1)
        LD (BARPG1),A
        DEC A
        LD (BARPG0),A
        setpga

;и ждем выключения: (en: and wait until it is off)
        WN
        GD
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
        JR NC,QUIT              ;можно жать RESET (en: one may press RESET)
;даем знать GS'у, что надо включить DMA (en: let the GS know it must turn the DMA on)
        LD A,1
        OUT (GSCOM),A
;и ждем включения: (en: and wait until it is on)
        WN
        GD
;
        CALL VIEWBAR
        IF border
        LD A,7
        OUT (254),A
        ENDIF
        JP BIGLOOP
; QUIT: command 2 (the card leaves the effect), the plain picture into screen 0, 16-colour mode off
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of the effect)
        HALT
        LD A,#1F                ;по идее на обоих экранах одно и то же (en: in theory both screens show the same)
        setpga
        LD HL,zxwasscr
        LD DE,#4000
        LD BC,#1B00
        LDIR                    ;то же, что видим, но 6912 (en: the same as what we see, but 6912)
        HALT
        LD A,#17
        setpga
        LD BC,#EFF7
        XOR A
        OUT (C),A
;LD HL,zxwasscr
;LD DE,#C000
;LD BC,#1B00
;LDIR ;то же, что видим, но 6912
        RET
;LD A,C_GRST
;OUT (GSCTR),A
;JR $

; ZXCL1800PG: page A in, zero #C000-#D7FF and #E000-#F7FF with PUSH (SP at the top of each area)
ZXCL1800PG
        setpga
        LD HL,#C000+#1800
        CALL ZXCL1800
        LD HL,#E000+#1800
ZXCL1800
        LD (ZXCLSP),SP
        LD SP,HL
        LD DE,0
        LD B,#18*2
ZXCL1800_0
        DUP (128&#FFFF)/(2&#FFFF)
        PUSH DE
        EDUP
        DJNZ ZXCL1800_0
ZXCLSP=$+1
        LD SP,0
        RET

; FORCENMI: NMI to the card CPU (GS control port); WAITDMA: wait for the card's answer (GSNMI / the commands)
        MACRO FORCENMI
        LD A,C_GNMI
        OUT (GSCTR),A
        ENDM

        MACRO WAITDMA
        WN                      ;ждем подтверждения от NGS (он выслал байт) (en: wait for the NGS's confirmation (it sent a byte))
        GD                      ;сброс признака (en: reset the flag)
        ENDM

; =====================================================================================================================
; VIEWBAR: read one frame from the card through ZX-DMA and apply it to the hidden screen.
; The header: SP = #00xx, four POPs = the record counts of clr0, out0, clr1, out1 (patched into the LD DE,n
; below). Then for each list: FORCENMI (GSNMI re-points the DMA), WAITDMA, one dummy read (the first byte after
; a re-point is wrong), and the blit routine with DE = count, IX = where to go next:
;   clr0 -> BARVIEWBLACK1 on BARPG0, out0 -> BARVIEW1S on BARPG0, clr1 -> BARVIEWBLACK1 on BARPG1,
;   out1 -> BARVIEW1 on BARPG1.
; The first three run with DI and SP in the ROM area (every POP is a DMA read). Before the last list SP is
; restored and interrupts enabled, so out1 is read with LD r,(HL) instead; the frame interrupt ends it: the
; IM 2 vector for bus value #FF (IMVEC+#FF) points at VIEWBARIM, which drops the interrupted address, restores
; the vector and goes through IMER back to VIEWBAR's caller. If the list ends first, VIEWBARQ waits for it.
; =====================================================================================================================
VIEWBAR
        DI
        LD (VIEWBARSP),SP
        LD HL,VIEWBARIM
        LD (IMVEC+#FF),HL
        LD H,0                  ;данные <12K, поэтому точное HL=0 не надо (en: the data is < 12K, so an exact HL = 0 is not needed)
        LD A,(HL)               ;1st byte is wrong
        LD SP,HL
        POP HL
        LD (viewbarclr0),HL
        POP HL
        LD (viewbarout0),HL
        POP HL
        LD (viewbarclr1),HL
        POP HL
        LD (viewbarout1),HL
;POP AF
;LD (viewbarclrform),A
;POP AF
;LD (viewbaroutform),A
;LD A,8
;OUT (GSDAT),A
        FORCENMI
BARPG0=$+1
        LD A,#1E
        setpga
viewbarclr0=$+1
        LD DE,0
        LD IX,VIEWBAR0OUT
        WAITDMA
        LD A,(0)                ;первый байт неверен (en: the first byte is wrong)
;viewbarclrform=$+1
;LD A,0
;OR A
;JP NZ,BARVIEWBLACK2
        JP BARVIEWBLACK1
VIEWBAR0OUT
;LD A,6
;OUT (GSDAT),A
        FORCENMI
viewbarout0=$+1
        LD DE,0
        LD IX,VIEWBAR0Q
        WAITDMA
        LD H,0
        LD A,(HL)               ;первый байт неверен (en: the first byte is wrong)
;viewbaroutform=$+1
;LD A,0
;OR A
;JP NZ,BARVIEW2
        JP BARVIEW1S            ;она быстрее (en: it is faster)
VIEWBAR0Q
;LD A,4
;OUT (GSDAT),A
        FORCENMI
BARPG1=$+1
        LD A,#1F
        setpga
viewbarclr1=$+1
        LD DE,0
        LD IX,VIEWBAR1OUT
        WAITDMA
        LD A,(0)                ;первый байт неверен (en: the first byte is wrong)
;LD A,(viewbarclrform)
;OR A
;JP NZ,BARVIEWBLACK2
        JP BARVIEWBLACK1
VIEWBAR1OUT
;LD A,2
;OUT (GSDAT),A
        FORCENMI
viewbarout1=$+1
        LD DE,0
        LD IX,VIEWBARQ
        WAITDMA
        LD H,0
        LD A,(HL)               ;первый байт неверен (en: the first byte is wrong)
VIEWBARSP=$+1
        LD SP,0
        EI
;LD A,(viewbaroutform)
;OR A
;JP NZ,BARVIEW2
        JP BARVIEW1
VIEWBARQ
        JR VIEWBARQ
VIEWBARIM
        LD SP,(VIEWBARSP)
        LD HL,IMER
        LD (IMVEC+#FF),HL
        JP (HL)                 ;делает EI (en: does EI)

; BARVIEWBLACK1: N records of 2 bytes: POP HL, LD (HL),0. DUP 32 x 2 bytes, entered through the patched JR $
; (Duff's device), B = passes. The ELSE branch is an older checked loop (not assembled)
BARVIEWBLACK1
;DE=N=число записей (!=0) (en: DE = N = number of records (!=0))
        IF 1
        DEC DE
        LD A,E
        CPL
        AND 31                  ;0 (N=0), 31 (N=1), 30 (N=2)...
        ADD A,A
        LD (BARVIEWBLACK10-1),A
        LD A,E
        DUP 5
        SRL D                   ;будет 0 (en: becomes 0)
        RRA
        EDUP
        INC A
        JR Z,BARVIEWBLACK1Q
        LD B,A
        JR $
BARVIEWBLACK10
        DUP 32
        POP HL
        LD (HL),D
        EDUP
        DJNZ BARVIEWBLACK10
        ELSE
        LD A,D
        OR E
        JR Z,BARVIEWBLACK1Q
BVB1    POP HL
        LD A,H
        CP #C0
        JR C,$
        LD (HL),1
        DEC DE
        LD A,D
        OR E
        JR NZ,BVB1
        ENDIF
BARVIEWBLACK1Q
        JP (IX)
; BARVIEWBLACK2: 2x2-chunk variant (clears the byte and the one a line up); unused - VIEWBAR's choice by the
; "form" byte is commented out
BARVIEWBLACK2
;DE=N=число записей (!=0) (en: DE = N = number of records (!=0))
        DEC DE
        LD A,E
        CPL
        AND 15                  ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
        ADD A,A
        LD (BARVIEWBLACK20-1),A
        LD A,E
        DUP 4
        SRL D                   ;будет 0 (en: becomes 0)
        RRA
        EDUP
        INC A
        JR Z,BARVIEWBLACK2Q
        LD B,A
        JR $
BARVIEWBLACK20
        DUP 16
        POP HL
        LD (HL),D
        DEC H
        LD (HL),D
        EDUP
        DJNZ BARVIEWBLACK20
BARVIEWBLACK2Q
        JP (IX)
; BARVIEW1: N records of 4 bytes read with LD A,(HL) x2, LD E,(HL), LD D,(HL) (HL = #00xx, any address reads the
; DMA stream), LD (DE),A. Used for out1 with interrupts on; DUP 16 x 5 bytes, Duff entry
BARVIEW1
;DE=N=число записей (!=0) (en: DE = N = number of records (!=0))
        DEC DE
        LD A,E
        CPL
        AND 15
        LD B,A                  ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
        ADD A,A
        ADD A,B
        LD (BARVIEW10-1),A
        LD A,E
        DUP 4
        SRL D
        RRA
        EDUP
        INC A
        JR Z,BARVIEW1Q
        LD B,A
        JR $
BARVIEW10
        DUP 16
;POP AF
;POP HL
;LD (HL),A
        LD A,(HL)
        LD A,(HL)
        LD E,(HL)
        LD D,(HL)
        LD (DE),A
        EDUP
        DJNZ BARVIEW10
BARVIEW1Q
        JP (IX)
; BARVIEW1S: the same with the stack: POP AF (A = byte), POP HL (address), LD (HL),A; DUP 32 x 3 bytes
BARVIEW1S
;DE=N=число записей (!=0) (en: DE = N = number of records (!=0))
        DEC DE
        LD A,E
        CPL
        AND 31
        LD B,A                  ;0 (N=0), 31 (N=1), 30 (N=2)...
        ADD A,A
        ADD A,B
        LD (BARVIEW10S-1),A
        LD A,E
        DUP 5
        SRL D
        RRA
        EDUP
        INC A
        JR Z,BARVIEW1QS
        LD B,A
        JR $
BARVIEW10S
        DUP 32
        POP AF
        POP HL
        LD (HL),A
        EDUP
        DJNZ BARVIEW10S
BARVIEW1QS
        JP (IX)
; BARVIEW2: 2x2-chunk variant (two bytes, two lines); unused, as BARVIEWBLACK2
BARVIEW2
;DE=N=число записей (!=0) (en: DE = N = number of records (!=0))
        DEC DE
        LD A,E
        CPL
        AND 15
        LD B,A                  ;0 (N=0), 15 (N=1), 14 (N=2)...
        ADD A,A
        ADD A,A
        ADD A,B
        LD (BARVIEW20-1),A
        LD A,E
        DUP 4
        SRL D
        RRA
        EDUP
        INC A
        JR Z,BARVIEW2Q
        LD B,A
        JR $
BARVIEW20
        DUP 16
        POP DE
        POP HL
        LD (HL),D
        DEC H
        LD (HL),E
        EDUP
        DJNZ BARVIEW20
BARVIEW2Q
        JP (IX)
; the picture again, for the plain screen at QUIT
zxwasscr
        INCBIN "gameover"
        ENT
LENROTBARCODE=$-WASROTBARCODE
LENROTBARZX=$-#C000

; test set-up: the next effect page only stops, and the music page
        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ObjTab: the object table for SAVEOBJ4 (name + type, address, length, #7FFD page, address in the page) - used
; only when the author ran the assembled source in ALASM
        ORG end
ObjTab
        DB 'ROTBARZXC'
        DW #C000
        DW LENROTBARZX
        DB pgrotbarzx
        DW #C000

        DB 'ROBPREGSC'
        DW #C000
        DW LENROTBARPREGS
        DB pgrotbarpregs
        DW #C000

        DB 'ROTBARGSC'
        DW #C000
        DW LENROTBARGS
        DB pgrotbargs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
; the #7FFD values serve as sjasmplus device page numbers here: they only keep the objects apart
        SLOT 3
        PAGE pgrotbarzx
        SAVEBIN "ROTBARZX",#C000,LENROTBARZX
        PAGE pgrotbarpregs
        SAVEBIN "ROBPREGS",#C000,LENROTBARPREGS
        PAGE pgrotbargs
        SAVEBIN "ROTBARGS",#C000,LENROTBARGS
