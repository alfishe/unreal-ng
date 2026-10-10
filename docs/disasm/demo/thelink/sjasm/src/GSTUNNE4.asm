; =====================================================================================================================
; GSTUNNE4 - The Link (Alone Coder, 2009), part 1: the tunnel.
; Pentagon 1024 + NeoGS. Converted from ALASM to sjasmplus; the comments without "en:" are the author's.
;
; WHAT IT BUILDS (ObjTab at the end; SAVEOBJ4 saved them in ALASM, the SAVEBIN block saves them here):
;   TUNNELZX  489 bytes   ORG #C000, #7FFD page pgeffects (#B0, RAM page 48)   - the ZX half
;   TUNNELGS  14222 bytes ORG #C000, #7FFD page pgcode1 (#52 here)             - the card half
;   THELINK.asm INCBINs both as they are: TUNNELZX stays in page #B0 (the first effect page), TUNNELGS goes to
;   page #30 and is uploaded to the card as card code page 3. Both run at #C000 (ZX: #7FFD page, card: MPAGEX page).
;
; THE TEST BED (not saved): begin..end at #7800 is an earlier copy of the start in THELINK.asm - GO resets the card,
; sends it a resident program (GSPROGGO), uploads one data page and one code page by ZX-DMA, installs the ZX resident
; loop (#BF04) and the IM 2 handler (#BFBF), starts the music and runs the effect pages. zxtimings has one entry, the
; next effect page (pgeffects+1) holds JR $: in ALASM the author could RUN this unit and watch the tunnel alone.
;
; HOW THE TWO HALVES COOPERATE (one frame):
;   card:  GSLOOP renders a 64 x 96 "chunky" picture (two texels per attribute byte) into SCR1 or SCR2 by running
;          ~67 KB of code generated at start (MKTUNNEL); waits for ZX command 0, stops the DMA, answers; waits for
;          command 1 (2 = quit), points the DMA at the finished buffer, starts it, answers; next frame.
;   ZX:    BIGLOOP sends 0 and 1 with the waits, HALTs, waits for the beam, then reads 3072 bytes with LD D,(HL) /
;          LD E,(HL) from #0000 (each read answered by ZX-DMA with the next byte) and PUSHes them into the attribute
;          areas of both screens while flipping the shown screen every 2 scan lines: each attribute row is shown as
;          4 rows of 2 lines -> 32 x 96 cells; the pixels are all #0F, so each cell shows a paper half and an ink half.
;   The tunnel itself: a texture (plasma, 16 KB at card #0000) is sampled through a torus lookup table (torusr.p,
;   16 KB) walked by SP with per-pixel deltas from a plan (dplan2r.p); the frame offset moves and rotates it.
;
; CARD MEMORY while the tunnel runs (card CPU addresses):
;   #0000-#3FFF plan (temporary) then the texture, texel values #78..#7F     #4000 / #4C00 SCR1 / SCR2, 3072 bytes each
;   #5830 GSPROG resident program (stack below it)    #5900 GSTUNNELCODE    #7878-#7F7F GSTCOLOR texel pair -> attribute
;   #8000 (MPAG) the generated code, 5 pages per screen    #C000 (MPAGEX) TUNNELGS itself, then the torus lookup page
;
; Converter additions: the DEVICE line below; "low x ; ALASM keeps the low byte" (ALASM took the low byte of a
; word operand silently, sjasmplus warns); JR for ALASM's JNZ/JC; split multi-operand lines; macro parameters _argN;
; the SAVEBIN block at the end.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
border=0

; en: #7FFD values of this test build; THELINK.asm has its own plan (pgdata1=#90, ...). Bit 4 = 48K ROM,
;     bits 0-2 and 5-7 = RAM page (Pentagon 1024), bit 3 = shown screen.
;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54
pgeffects=#B0

; en: fixed ZX addresses, the same in every unit (THELINK.asm installs the resident part at #BD00-#BFFF)
TUNNELCODE=#7800

resbuf=#BD00                    ; en: 256-byte bounce buffer of RESMOVEPAGE
IMVEC=#BE00                     ; en: IM 2 vector table (257 bytes of #BF)
curpg=#BF01                     ; en: the last #7FFD value (setpga)
timer=#BF02                     ; en: frame counter, 16 bits, incremented by IMER
ZXRESIDENT=#BF04                ; en: resident loop over the ZX effect pages
IMER=#BFBF                      ; en: IM 2 handler: timer + music
outimstack=#5F00                ; en: IMER's own stack

        INCLUDE "gsports.asm"
; en: card (NeoGS) addresses
GSPROG=#5830                    ; en: the card's resident program
GSSTACK=#5830                   ; en: card stack, below the program
GSTUNNELCODE=#5900              ; en: the tunnel's main code on the card

; en: card page numbers (MPAG / MPAGEX values): bits 0-6 = 32K page, bit 7 = its upper 16K half, so the
;     linear 16K page is RLCA of the value and the next page is RLCA : INC A : RRCA (see GSRES0).
gspgcode=#10                    ; en: first card code page (command 4)
gspgdata=#20                    ; en: first card data page (command 3)

GSpglookup=2                    ; en: card page of the torus lookup table
GSLOOKUP=#C000                  ; en: ... seen at #C000 through MPAGEX
GSpgprog1=#82                   ;16K*5: #82,03,83,04,85 (en: 5 x 16K of generated code for SCR1; the fifth value is #84, not #85)
GSpgprog2=#06                   ;16K*5: #06,86,07,87,08 (en: 5 x 16K of generated code for SCR2)
GSPLAN2RTMP=#0000               ; en: the unpacked plan, temporary
; en: texture = #0000..#3FFF
;текстура=0000..3FFF
GSSCR1=#4000                    ; en: frame buffer 1: 32 x 96 attribute bytes
GSSCR2=#4C00                    ; en: frame buffer 2
GSTCOLOR=#7878                  ; en: texel pair -> attribute table

; en: GSSETPGA - not used in this unit
        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

; en: setpga - select #7FFD page A and remember it in curpg (the IM 2 handler restores it); BC destroyed
        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; ---------------------------------------------------------------------------------------------------------------------
; en: TEST BED (begin..end at #7800, not saved): GO = card reset, card program, DMA upload, resident, IM 2, music.
        ORG #7800
begin
GO
; en: border black, own stack; hard-reset the card and give its firmware two frames
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
; en: for some reason POKE #4092,0 in card memory: GS command #18 takes the address (low byte before the
;     command, high byte as the next data byte), #19 writes the data byte there
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

; en: copy the card program WASGSPROG (LENGSPROG bytes) to card #5830 one byte at a time with #18 / #19
        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
; en: as in RIFF TRACKER
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
;#13 Jump to Address
; en: low byte in the data register before the command, high byte as the next data byte
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

; en: clear #C000-#D7FF and #E000-#F7FF of pages #14 and #15 (#15 = RAM page 5: the screen at #4000 and
;     #6000-#77FF below GO)
        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
; en: and wait until it is on (the card answers through its data register):
;и ждем включения:
        WN
        GD
;
; en: send the data pages to the GS (one page in this test bed):
;шлем страницы данных в GS:
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
; en: and wait for the switch:
;и ждем переключения:
        WN
        GD
;
; en: send the code pages to the GS (one page: TUNNELGS):
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

; en: install the ZX resident loop at #BF04 and the IM 2 handler at #BFBF; the IM 2 table at #BE00 is
;     257 bytes of #BF, so whatever the bus holds the vector is #BFBF
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

; en: music init (the player's entry #C000 in its page)
        LD A,pgmusic
        setpga                  ;заполняет curpg (en: fills curpg)
        CALL #C000

; en: frame counter = 0, interrupts on, run the effect pages from pgeffects with their start / end frames
        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; en: MOVEPAGE dstpage,src,len,dst - copy len bytes (whole 256-byte blocks, IYH = count) from src in the
;     current page (curpg) to dst in page dstpage through resbuf; tail-jumps to RESMOVEPAGE in the resident.
;     Not used in this unit.
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; en: GSDMASENDPG - page A in at #C000 and copy it to #0000-#3FFF: on the ZX these are writes into the ROM
;     (no effect), while the card's ZX-DMA, started by command 3 / 4, stores every byte in card RAM.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; en: CL1800PG - page A in, zero #C000-#D7FF and #E000-#F7FF. CL1800: zero #1800 bytes from HL.
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
; en: THE CARD PROGRAM, assembled for card #5830 and sent there with GS commands #18/#19, started with #13.
;     Silences the 8 channels, sets 24 MHz / NOROM (RAM everywhere) / EXPAG (two 16K windows), selects DMA
;     module 1 (ZX-DMA) and serves ZX commands 3 (DMA into the data pages), 4 (into the code pages), 5 (DMA
;     off); then runs the effects: each card code page is mapped at #C000 and CALLed in turn.
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

; en: keep the other bits, clock 24 MHz, ROM off, extended paging; RAM write protection off
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO
        OR C_24MHZ|M_NOROM|M_EXPAG
        OUT (GSCFG0),A

        LD A,1                  ;#8000: gspg1:0 (en: #8000 window: card page 1)
        OUT (MPAG),A
        LD A,2                  ;#C000: gspg2:0 (en: #C000 window: card page 2)
        OUT (MPAGEX),A

        LD A,1                  ;SET DMA MODULE #1
        OUT (DMA_MOD),A

; en: wait for ZX command 3
        LD E,3
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

; en: DMA address = card page gspgdata: HAD = bits 21-16, MAD = bits 15-8, LAD = bits 7-0; RRCA of the
;     page value gives HAD in bits 0-5 and the two 16K bits for MAD in bits 6-7
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX "DMA ON")

; en: wait for ZX command 4, then restart the DMA at card page gspgcode
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX "DMA ON")

; en: wait for ZX command 5: DMA off, answer, wait until the ZX has read the answer
        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX "DMA OFF")
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready? - it has read the flag)

        LD A,gspgcode
;JP GSRESIDENTGO

; en: GSRESIDENTGO - the card's resident loop: map code page A at #C000, CALL it (one effect), next page.
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
; en: ZX RESIDENT, copied to #BF04 (outside the #C000 window, so it survives every page switch).
WASZXRES
        DISP ZXRESIDENT
; en: A = effect page (bit 4 forced: 48K ROM), HL -> zxtimings. Each effect is CALLed at #C000 with
;     BC = start frame and DE = end frame, and returns when its end frame has passed.
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
; en: RESMOVEPAGE - the body of MOVEPAGE: 256 bytes per pass, page IXH -> resbuf -> page IXL
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
; en: start / end frame of each effect page (the timer counts frames, 50 per second)
zxtimings
        DW 896,896*2-20         ;tunnel
        ENT
LENZXRES=$-WASZXRES

; ---------------------------------------------------------------------------------------------------------------------
; en: IMER, copied to #BFBF: the IM 2 handler. Uses only one word of the interrupted stack: DE is swapped in
;     for the return address (kept in outimjp), then it works on its own stack at #5F00, so it is safe even
;     while an effect uses SP for data. timer += 1; the music page is paged in with the shown-screen and ROM
;     bits of curpg kept, the player is called (#C005), curpg is restored. Saves HL, DE, AF, BC, AF' only: the
;     player must not touch the rest. Also callable with CALL IMER (the tunnel does that).
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
;EXX
;PUSH BC,DE,HL
;PUSH IX,IY
        LD HL,(timer)
        INC HL
        LD (timer),HL
; en: keep bit 3 (shown screen) and bit 4 (ROM) of the current page, select the music page
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
; en: TUNNELGS - the card half. Runs at card #C000 (MPAGEX = its code page), CALLed by GSRES0 on the card.
;     Copies its main code to #5900 (the #C000 window is needed for the lookup table), unpacks the plan to
;     #0000 (temporary) and the torus lookup table to #8000 of card page GSpglookup, then jumps to GSTUNNELGO.
        ORG #C000,pgcode1
pgtunnelgs=pgcode1
        LD HL,WASGSTUNNELCODE
        LD DE,GSTUNNELCODE
        LD BC,LENGSTUNNELCODE
        LDIR

        LD HL,wasplan2r
        LD DE,GSPLAN2RTMP       ;временно (en: temporarily)
        CALL DEHR21
        LD A,GSpglookup
        OUT (MPAG),A
        LD HL,wastorus
        LD DE,#8000
        CALL DEHR21
        JP GSTUNNELGO
; ---------------------------------------------------------------------------------------------------------------------
; en: DEHR21 - Hrust 2.1 depacker. Packed block: "hr21", unpacked length, packed length (8 bytes), then the
;     last 6 bytes of the output (pushed now, written at the very end), then the bit stream.
;     Main set: HL = packed data, C = bit buffer (CHL refills it), D/E = constants #10/#03.
;     Alternate set: DE' = output, HL' = back-reference source, BC' = count. All registers destroyed.
DEHR21
;HL=FROM
;DE=TO

        PUSH DE
        LD DE,6+8
        ADD HL,DE
        PUSH HL
        EXX
        POP HL
        POP DE
        LD B,6
Hfst__L1 DEC HL
        LD A,(HL)
        PUSH AF
        DJNZ Hfst__L1
        EXX
        LD DE,#1003
        LD C,#80
Hloop__L1 LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
Hxxtbit__L1 EXX
Hnxtbit__L1 SLA C
        CALL Z,CHL__L1
        JR C,Hloop__L1
        LD B,1
H2bit__L1 LD A,#40
H2bit0__L1 SLA C
        CALL Z,CHL__L1
        RLA
        JR NC,H2bit0__L1
        CP E
        JR C,H2lessE__L1
        ADD A,B
        LD B,A
        XOR D
        JR NZ,H2bit__L1
H2lessE__L1 ADD A,B
        CP 4
        JR Z,Hnpaked__L1
        ADC A,-1
LL5DB1__L1 CP 2
LL5DB3__L1 EXX
        LD C,A
        LD H,-1
        EXX
        JR C,H3bit__L1          ;B=1
        JR Z,LL5DE5__L1
        SLA C
        CALL Z,CHL__L1
        JR C,LL5DE5__L1
        LD A,#7F
        LD B,E
        DJNZ Hbits__L1          ;B=3 CALL GET2BITS
;B=1
LL5DCB__L1 DJNZ Hplain__L1
        LD B,A                  ;FC-FF
        SBC A,A
Hijnz__L1 SLA C
        CALL Z,CHL__L1
        RLA
        DEC A                   ;1.FD-FE
;2>F8
;3>F0
;4>E0
        INC B
        JR NZ,Hijnz__L1
        CP #E1
        JR NZ,$+4
        LD A,(HL)
        INC HL
        EXX
        LD H,A
        EXX
LL5DE5__L1 LD A,(HL)
        INC HL
LL5DE7__L1 EXX
        LD L,A
        ADD HL,DE
        LDIR
        JR Hxxtbit__L1
Hplain__L1 ADD A,6
        RLA
        LD B,A
Hldir__L1 LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
        EXX
        DJNZ Hldir__L1
        JR Hnxtbit__L1
Hnpaked__L1 SLA C
        CALL Z,CHL__L1
        LD A,D
        JR NC,Hbits__L1         ;B=3 GETBIT,Hplain
        LD A,(HL)
        INC HL
        CP D
        JR NC,LL5DB1__L1
        OR A
        JR Z,Hquit__L1
        EXX
        LD B,A
        EXX
        LD A,(HL)
        INC HL
        JR LL5DB3__L1
H3bit__L1 LD A,#3F
Hbits__L1 SLA C
        CALL Z,CHL__L1
        RLA
        JR NC,Hbits__L1
        DJNZ LL5DCB__L1
        JR LL5DE7__L1
Hquit__L1 EXX
        LD B,6
        POP AF
        LD (DE),A
        INC DE
        DJNZ $-3
; en: exit. HL' = after the packed block
;выходим. HL'=после упакованного блока
; en: Hquit falls through into CHL: one byte more is read and its RET ends DEHR21
CHL__L1 LD C,(HL)
        INC HL
        RL C
        RET


; ---------------------------------------------------------------------------------------------------------------------
; en: THE TUNNEL MAIN CODE on the card, copied to #5900. GSTUNNELGO: generate the drawing code for both
;     screens, build the colour table and the plasma texture, then loop over frames (GSLOOP).
WASGSTUNNELCODE
        DISP GSTUNNELCODE
GSTUNNELGO
        LD A,GSpgprog1
        OUT (MPAG),A
        LD A,GSpglookup
        OUT (MPAGEX),A

        LD BC,GSSCR1
        LD A,GSpgprog1
        CALL MKTUNNEL
        LD BC,GSSCR2
        LD A,GSpgprog2
        CALL MKTUNNEL
;теперь можно класть текстуру в #0000

        CALL GENTCOLOR
; en: texture seed: mix the bytes left at #0000 (the used plan) with the lookup table at #C000 -> noise
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
GSPLASMPRE0
        LD A,(DE)
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
        JR Z,GSPLASMPRE0
; en: 10 smoothing passes: each byte = average-ish of itself, its right neighbour and the one 256 bytes on
;     (with carries as extra noise), wrapping at #4000 -> a soft plasma
        LD B,10
GSPLASM
        PUSH BC
        LD HL,#0000
GSPLASM0
        LD A,(HL)
        PUSH HL
        INC L
        ADC A,(HL)              ;+noise
        RRA
        INC H
        RES 6,H
        ADC A,(HL)              ;+noise
        RRA
        POP HL
        LD (HL),A
        INC HL
        BIT 6,H
        JR Z,GSPLASM0
        POP BC
        DJNZ GSPLASM
; en: quantize to 8 levels: texel = (top 3 bits) + #78, i.e. the low or high byte of an address in GSTCOLOR
        LD HL,#0000
GSPLASM1
        LD A,(HL)
        RLCA
        RLCA
        RLCA
        AND 7
        ADD A,low GSTCOLOR              ; ALASM keeps the low byte
        LD (HL),A
        INC HL
        BIT 6,H
        JR Z,GSPLASM1

; ---------------------------------------------------------------------------------------------------------------------
; en: GSLOOP - one frame. Flip the target buffer, map its generated code at #8000, advance the motion and
;     run the generated code with SP = the lookup table pointer; it ends with JP (IX) = GSTUNNELQ.
;     The card runs with interrupts off (DI in GSPROGGO), so SP may point anywhere.
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: drawing into SCR2)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now drawing into SCR1)
;потом дадим его ZX'у (en: later we give it to the ZX)
        LD (GSTUNNELSP),SP
        LD IX,GSTUNNELQ
        LD A,GSpgprog1
        JR Z,$+4
        LD A,GSpgprog2
        OUT (MPAG),A
; en: GSSPEED: C = #C0, so BC = -64 is added to GSangle every frame (the IF 0 block below, a random speed
;     rhythm with GSRND, is switched off)
GSSPEED=$+1
        LD C,#C0
; en: dVU: E (texture low byte) advances by 5 per frame
GSdVU=$+1
        LD DE,0                 ;dVU
        LD A,E
        ADD A,5
        LD E,A
gsspeedtime=0
        IF 0
GSrhythm=$+1
        LD A,0
        ADD A,4
        LD (GSrhythm),A
        JR NC,GSNSPEED
        IF gsspeedtime
        CALL GSRND
        AND #1F
        ADD A,#20
        LD (GSSPEEDTIME),A
        CALL GSRND
        ENDIF
;AND #80
;SUB #40 ;+-64
;RLA
;SBC A,A
;ADD A,#80 ;-128/+127
        AND #C0
        JR NZ,$+4
        LD A,#7F                ;-128..+127
        LD C,A
GSNSPEED
        IF gsspeedtime
GSSPEEDTIME=$+1
        LD A,0
        DEC A
        LD (GSSPEEDTIME),A
        LD A,C
        JR NZ,$+3
        XOR A
        ENDIF
;LD A,C
;OR A
;JZ GSNSLOW
;JP M,$+5
;SUB 2
;INC A
GSNSLOW
;LD (GSSPEED),A
        ENDIF
        LD A,C
        RLA
        SBC A,A
        LD B,A
;LD A,C
;ADD A,32 ;правильное округление (en: correct rounding)
;AND #C0
;LD C,A
; en: angle -= 64; D = -(angle high) & #3F is the texture high-byte offset; the lookup table start is
;     #C000 + an even L taken from angle * 4. SP = HL, then into the generated code.
GSangle=$+1
        LD HL,0
        ADD HL,BC
        LD (GSangle),HL
        XOR A
        SUB H
        AND #3F
        LD D,A
        ADD HL,HL
        ADD HL,HL
        LD A,H
        LD HL,GSLOOKUP
        AND #FE
        LD L,A
        LD (GSdVU),DE
        LD SP,HL
        JP #8000
; en: GSTUNNELQ - back from the generated code: restore SP and do the frame handshake with the ZX
GSTUNNELQ
GSTUNNELSP=$+1
        LD SP,0
; en: GS waits for the ZX's end of frame (command 0); the timeout of this wait is commented out
;GS ожидает конец кадра у ZX
        LD HL,0
        LD DE,1
GSWAIT0                         ;ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
; en: after this the ZX may be reset
;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for the ZX "DMA OFF")
; en: GS waits for the zeroing (the ZX will request data) - in fact for a non-zero command: 1 = next frame,
;     2 = quit (RET NZ back to the card's resident loop); 65536 polls without one -> GSRESET
;GS ожидает обнуления (ZX запросит данные)
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT

; en: DMA source = the buffer just drawn; card #4000 / #4C00 is DMA address #00C000 / #00CC00 (+#80)
        XOR A
        OUT (DMA_HAD),A
        LD A,(gsdrawscr)
        OR A
        LD A,high GSSCR1+#80
        JR Z,$+4
        LD A,high GSSCR2+#80
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for the ZX "DMA ON")
        JP GSLOOP

; en: GSRND - 16-bit pseudo-random generator (seed*17 + 20981), result in A. Used only in the IF 0 block.
GSRND
        PUSH BC
        PUSH HL
GSRNDSEED=$+1
        LD HL,0
        LD B,H
        LD C,L
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,HL
        ADD HL,BC
        LD BC,20981
        ADD HL,BC
        LD (GSRNDSEED),HL
        LD A,H
        POP HL
        POP BC
        RET

; ---------------------------------------------------------------------------------------------------------------------
; en: GENTCOLOR - build the 8x8 table at #7878..#7F7F: the byte at (#78+r)*256 + #78+l is an attribute whose
;     ink is TCOLORS[l] (texel in C, the low byte) and whose paper is TCOLORS[r] (texel in B, the high byte).
;     BRIGHT is per cell, so it stays only when both colours have it.
GENTCOLOR
; en: build the left pixels - depend on L
;строим левые пиксели - зависят от L
        LD H,high GSTCOLOR
        LD C,8
GENTCOL0
        LD L,low GSTCOLOR               ; ALASM keeps the low byte
        LD DE,TCOLORS
        LD B,8
GENTCOL1
        LD A,(DE)
        LD (HL),A
        INC DE
        INC L
        DJNZ GENTCOL1
        INC H
        DEC C
        JR NZ,GENTCOL0
; en: build the right pixels - depend on H
;строим правые пиксели - зависят от H
        LD DE,TCOLORS
        LD H,high GSTCOLOR
        LD C,8
GENTCOL2
        LD L,low GSTCOLOR               ; ALASM keeps the low byte
        LD B,8
GENTCOL3
; en: bright+nobright=nobright+nobright
;     hence: if bright is needed, it is already in (HL); if it is not in (DE), it is not needed
;bright+nobright=nobright+nobright
;отсюда следует:
;если нужен bright, то он уже есть в (HL)
;если его нет в (DE), то он не нужен
        LD A,(DE)
        ADD A,A
        ADD A,A
        JR C,$+4
        RES 6,(HL)              ;nobright
        ADD A,A
        OR (HL)
        LD (HL),A
        INC L
        DJNZ GENTCOL3
        INC DE
        INC H
        DEC C
        JR NZ,GENTCOL2
        RET

; en: TCOLORS - the colour of each of the 8 texel levels (ink code, +64 = BRIGHT); two earlier palettes above
TCOLORS
;DB 0,1,64+1,5,7,64+7,64+6,64+4
;DB 0,1,64+1,2,3,5,6,7
        DB 1,1,64+1,2,3,5,7,7

; ---------------------------------------------------------------------------------------------------------------------
; en: MKTUNNEL - generate the drawing code for one screen. In: A = first card page for the code (mapped at
;     #8000 through MPAG), BC = screen buffer (#4000 or #4C00). Reads the plan at #0000: per texel a word
;     (da, dR). Per screen byte (2 texels) it emits 22 bytes:
;       LD HL,delta : ADD HL,SP : LD SP,HL : POP HL : ADD HL,DE : RES 6,H : LD C,(HL)    first texel
;       LD HL,delta : ADD HL,SP : LD SP,HL : POP HL : ADD HL,DE : RES 6,H : LD B,(HL)    second texel
;       LD A,(BC) : LD (screen byte),A
;     When the code reaches #BFxx it emits LD A,nextpage : OUT (MPAG),A : JP #8000 and goes on in the next
;     page (5 pages per screen). The end is JP (IX). 3072 screen bytes = 67584 bytes of code.
MKTUNNEL
        LD IXL,A
        OUT (MPAG),A
        LD DE,GSPLAN2RTMP
        LD A,B
        ADD A,#0C
        LD IXH,A
        LD HL,#8000
MKTUNNEL0
        LD (HL),33
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;da
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;dR
        INC HL
        PUSH BC
        PUSH DE
        EX DE,HL
        LD HL,TUNNELCODESAMPLE
        LD BC,TUNNELCODESAMPLELEN
        LDIR
        EX DE,HL
        POP DE
        LD (HL),#4E             ;ld c,(hl) ;левый пикс.=LSB (en: left pixel = low byte)
        INC HL
        LD (HL),33
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;da
        INC HL
        LD A,(DE)
        INC DE
        LD (HL),A               ;dR
        INC HL
        PUSH DE
        EX DE,HL
        LD HL,TUNNELCODESAMPLE
        LD BC,TUNNELCODESAMPLELEN
        LDIR
        EX DE,HL
        POP DE
        POP BC
        LD (HL),#46             ;ld b,(hl) ;правый пикс.=HSB (en: right pixel = high byte)
        INC HL
        LD (HL),#0A             ;ld a,(bc)
        INC HL
        LD (HL),#32
        INC HL
        LD (HL),C
        INC HL
        LD (HL),B
        INC HL
        LD A,H
        CP #BF
        JR NZ,MKTUNNELnPG
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
MKTUNNELnPG
        INC BC
        LD A,B
        CP IXH
        JR NZ,MKTUNNEL0
; en: done: emit JP (IX)
        LD (HL),#DD
        INC HL
        LD (HL),#E9             ;jp (ix)
        RET

; en: TUNNELCODESAMPLE - the texel fetch: SP walks the lookup table by the plan's deltas, POP HL gives the
;     texture address, DE adds the frame's motion, RES 6,H wraps it into #0000-#3FFF
TUNNELCODESAMPLE
        ADD HL,SP
        LD SP,HL
        POP HL
        ADD HL,DE
        RES 6,H
TUNNELCODESAMPLELEN=$-TUNNELCODESAMPLE

; en: GSRESET - the card's way out when the ZX stops talking: DMA off, back to 10 MHz, ROM on, normal paging,
;     RST 0 = restart the card firmware
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0
        ENT
LENGSTUNNELCODE=$-WASGSTUNNELCODE
        DISPLAY /T,LENGSTUNNELCODE

; en: Hrust-packed data: torusr.p -> 16384 bytes, the torus lookup table (texture addresses);
;     dplan2r.p -> 12288 bytes, the plan: 6144 texels x (da, dR)
wastorus
        INCBIN "torusr.p"
wasplan2r
        INCBIN "dplan2r.p"
LENTUNNELGS=$-#C000

; =====================================================================================================================
; en: TUNNELZX - the ZX half. CALLed at #C000 by the resident loop with BC = start frame, DE = end frame.
;     Copies itself to #7800 (it pages #17 in at #C000 for the second screen) and runs there.
        ORG #C000,pgeffects
pgtunnelzx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASTUNNELCODE
        LD DE,TUNNELCODE
        LD BC,LENTUNNELCODE
        LDIR
        JP TUNNELCODE

WASTUNNELCODE
        DISP TUNNELCODE
; en: end frame -> zxendtime; wait until the timer reaches the start frame
        POP HL
        LD (zxendtime),HL
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART

; en: page 7 (screen 1) at #C000; attributes of both screens 0, pixels #0F (left 4 pixels paper, right 4 ink)
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
        LD HL,#4000
        LD DE,#4001
        LD BC,#17FF
        LD (HL),#0F
        LDIR
        LD HL,#C000
        LD DE,#C001
        LD BC,#17FF
        LD (HL),#0F
        LDIR
        LD A,#10                ;noturbo
        LD BC,#EFF7
        OUT (C),A

; en: IM 2 vectors -> #5F5F, where EI : RET is put: an interrupt now only ends a HALT. The timer and the
;     music run from CALL IMER once per BIGLOOP pass.
        HALT
        LD HL,#C9FB
        LD (#5F5F),HL
        LD HL,IMVEC
        LD BC,256
        LD D,H
        LD E,B
        LD (HL),#5F
        LDIR
        JP BIGLOOP
; en: QUIT - command 2 to the card (it returns to its resident loop), IM 2 back to IMER, screen 0 shown,
;     its attributes cleared, #EFF7 = 0; RET to the ZX resident loop
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of effect)
        HALT
        LD HL,IMVEC
        LD BC,256
        LD D,H
        LD E,B
        LD (HL),low IMER                ; ALASM keeps the low byte
        LDIR
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
;LD A,C_GRST
;OUT (GSCTR),A
;JR $

; ---------------------------------------------------------------------------------------------------------------------
; en: BIGLOOP - one frame of the ZX half: handshake 0 / 1 with the card, quit on the end frame or SPACE,
;     then the DMA read into both screens' attributes.
BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
; en: let the GS know the frame is ready and the DMA must be switched off:
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        XOR A
        OUT (GSCOM),A

; en: and wait until it is off:
;и ждем выключения:
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
        JR NC,QUIT              ;можно жать RESET (en: RESET may be pressed)
; en: let the GS know the DMA must be switched on:
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
; en: and wait until it is on:
;и ждем включения:
        WN
        GD
;
; en: HALT, then a delay (about #299 x 26 t) to near the first screen line; "298..A" is the author's note
        HALT
        LD HL,#0299             ;298..A
        DEC HL
        LD A,H
        OR L
        JR NZ,$-3
; en: DI: SP becomes the write pointer. HL = 0, so every LD r,(HL) is a ZX-DMA read of the next byte
        DI
        LD (PUSHKASP),SP
;LD H,0 ;уже 0 (en: already 0)
        LD A,(HL)               ;1ST BYTE DISCARDED
; en: BC = #7FFD; B' = 24 character rows, DE' = 32; IX = end of attribute row 0 of screen 0 (#5820, PUSH writes down),
;     IY = the same row of screen 1 (#D820: page 7 at #C000); A = the #7FFD value that shows screen 1
        LD BC,#7FFD
        EXX
        LD B,24
        LD DE,32
loopmcscr=$+3
        LD IX,#5820             ;линия 0: видим scr1, рисуем scr0 (en: line 0: we see scr1, we draw scr0)
        LD A,IXH
        XOR #80
        LD IYH,A
;LD (loopmcscr),A
        LD IYL,E                ;#20
        RLA
        SBC A,A
        AND 8
        OR #17                  ;тот же экран, что в IY (en: the same screen as in IY)
; en: PUSHKA0 - one character row, 4 blocks of 32 bytes (16 x LD D,(HL) : LD E,(HL) : PUSH DE = 400 t),
;     each about 2 scan lines: write screen 0 while screen 1 is shown, flip, write screen 1, flip, ... The same
;     attribute row is rewritten twice in each screen, so the row shows 4 different colour lines.
;     The numbers in the comments are the author's t-state counts around the OUTs.
PUSHKA0
        EXX
        LD SP,IX                ;выводим scr0(1) (en: output scr0(1))
        OUT (C),A               ;видим scr1(0) (en: we see scr1(0))
;51
;+0
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        NOP
        NOP
        LD SP,IY
        OUT (C),A               ;37
;-11
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        LD SP,IX
        EXX
        ADD IX,DE
        EXX
        OUT (C),A               ;52
;-7
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        LD SP,IY
        EXX
        ADD IY,DE
        EXX
        OUT (C),A               ;52
;-3
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP                    ;400
        XOR 8
        EXX
        DEC B
        JP NZ,PUSHKA0
; en: SP back, interrupts on; IMER called directly: timer + music for this pass
PUSHKASP=$+1
        LD SP,0
        EI
        IF border
        LD A,R
        OUT (254),A
        ENDIF
        CALL IMER
        JP BIGLOOP
        ENT
LENTUNNELCODE=$-WASTUNNELCODE
LENTUNNELZX=$-#C000

; en: test bed only (not saved): the next effect page stops the ZX; the music in its page
        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ---------------------------------------------------------------------------------------------------------------------
; en: ObjTab for SAVEOBJ4: per object 9 bytes name + type, address, length, #7FFD page, start (16 bytes).
;     The table ends with the first byte of SAVEOBJ4 (NOP = 0).
        ORG end
ObjTab
        DB 'TUNNELZXC'
        DW #C000
        DW LENTUNNELZX
        DB pgtunnelzx
        DW #C000

        DB 'TUNNELGSC'
        DW #C000
        DW LENTUNNELGS
        DB pgtunnelgs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
        SLOT 3
        PAGE pgtunnelzx
        SAVEBIN "TUNNELZX",#C000,LENTUNNELZX
        PAGE pgtunnelgs
        SAVEBIN "TUNNELGS",#C000,LENTUNNELGS
