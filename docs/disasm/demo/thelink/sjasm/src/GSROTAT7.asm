; =====================================================================================================================
; GSROTAT7 - "The Link" (Alone Coder, 2009): the rotator effect (the rotating logo), Pentagon 1024 + NeoGS
; =====================================================================================================================
;
; The card (NeoGS, Z80 at 24 MHz) renders the whole picture: a 256x128-pixel texture (the top 128 lines of
; "titletex") rotated and scrolled over the full 256x192 screen, with the "The Link" logo (thelink$) stamped over it.
; The ZX only shows it: every second frame it fetches the finished 6912-byte screen from the card through NeoGS
; ZX-DMA reads and writes it into the hidden screen with PUSH, then flips screens.
;
; Objects (written by the SAVEBIN block at the end; THELINK.asm later INCBINs them into its own pages):
;   ROTATEZX  #C000, 311 bytes   ZX half.  This file's test page: pgeffects (#B0); in the demo #7FFD = #B1.
;                                It copies itself to ROTATECODE (#7800) and runs there.
;   ROTPREGS  #C000, 4564 bytes  card precalc code.  Here pgcode1 (#52); in the demo card code page 1 (#52).
;                                Builds 16 pre-rotated byte maps of the texture in card RAM, then returns.
;   ROTATEGS  #C000, 8261 bytes  card effect code.  Here pgcode2 (#55); in the demo card code page 4 (#31).
;                                Draws a frame into one of two screen buffers, hands it to the ZX, loops.
; In the demo the card runs ROTPREGS early (it is card code page 1), so the precalc is done long before the
; rotator's turn (frames 1792-2674 of the demo's frame counter).
;
; The first block (begin..end at #7800, "GO") is NOT part of the demo: it is the author's stand-alone test launcher
; for this effect, a cut-down copy of THELINK.asm's GO (card reset, card program via GS commands #18/#19/#13, ZX-DMA
; upload of one data page and the two code pages, IM 2 with the music, the resident loop) with only the rotator in
; its timing table.  It is assembled (ObjTab is placed at "end") but saved nowhere.
;
; ZX <-> card protocol per shown frame (ZX writes GSCOM, card reads ZXCMD; card answers with ZXDATWR):
;   ZX: HALT, command 0 ("frame shown, stop the DMA"), flip screens, wait for the answer;
;   ZX: command 1 ("start the DMA"), wait for the answer, then 1 + 6912 reads from #00xx
;       (3456 x LD D,(HL) : LD E,(HL) : PUSH DE);
;   card: draw the next frame into the other buffer, wait for 0, DMA off, answer; wait for 1 (65536-poll timeout
;         -> GSRESET), DMA address = the finished buffer, DMA on, answer.  Command 2 = end of the effect.
;
; Card memory used by ROTATEGS (card addresses; #0000-#7FFF is fixed RAM with NOROM):
;   GSSCR1 #0000, GSSCR2 #2000      two screen buffers, each a ZX screen stored BYTE-REVERSED (card offset c holds
;                                   ZX screen byte #17FF-c, attributes #1800+j hold ZX attribute #2FF-j), because
;                                   the ZX writes the DMA stream with PUSH from the top of the screen down
;   GSTDHL #1B00, GSTDHL2 #1C80     192 line addresses (+1) for SCR1 / SCR2
;   GSGENPROG #4000                 the inner loop, generated every frame by MKROT
;   GSLOGO #7D00 (+ page GSpglogo)  the logo picture, reversed like the buffers; GSLOGOPROG #9800: generated code
;                                   that stamps the logo into a buffer
;   32K blocks GSpgcopy1..+15       (#8000-#FFFF) the 16 pre-rotated byte maps made by ROTPREGS
; Card page numbers are the author's 32K-block notation: MPAG value p maps 16K page rotl(p) at #8000 (EXPAG mode),
; and the recurring "RLCA : INC A : RRCA : OUT (MPAGEX),A" maps the next 16K page at #C000 - the whole 32K block p
; at #8000-#FFFF (comments like "gspg1:0" = block 1, half 0).
;
; Data INCBINned: titletex (the first #1000 bytes: the texture), sin32768 (512 bytes: 32768*SIN(n/256*2*PI) as
; words, the 256 low bytes then the 256 high bytes; computed by the author's BASIC, recomputed by tables.py),
; thelink$ (the logo screen, 6912 bytes) and thelinkm (the music, only for the test launcher).
;
; Converter additions (unreal-asm, ALASM -> sjasmplus): the DEVICE line below; "low x" where ALASM stored a 16-bit
; value into a byte (comment "ALASM keeps the low byte"); "(x&#FFFF)/(256&#FFFF)" = ALASM's 16-bit division in
; MOVEPAGE; JR for ALASM's JNZ/JC; "LD H,A,D,A"-style lines split; the SAVEBIN block at the end (replaces running
; SAVEOBJ4, the author's object saver, from ALASM).
; =====================================================================================================================

        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
; Build switches: all off except genldi in the released objects.
zx=0                            ; 1 = texture copies 16K at #C000 instead of 32K at #8000 (a 64-line variant)
counter=0                       ; 1 = card prints a 5-digit number into each frame (debug; GSDIGGFX font)
genldi=1                        ; 1 = ROTPREGS generates the 256 x LDI routine at #3D00 instead of assembling it
dump=0                          ; 1 (with counter) = card prints the bytes of GSLDI256 (debug)
border=0                        ; 1 = ZX border colour shows the frame timing

;GS data in ZX memory:
pgdata1=#50
pgcode1=#52
pgcode2=#55

;ZX data:
pgmusic=#54
pgeffects=#B0

ROTATECODE=#7800               ; where ROTATEZX runs (free once the resident loop has started)

; ZX resident area, as in THELINK.asm: MOVEPAGE buffer, IM 2 table, current #7FFD value, frame counter,
; resident loop, interrupt handler, interrupt stack
resbuf=#BD00
IMVEC=#BE00
curpg=#BF01
timer=#BF02
ZXRESIDENT=#BF04
IMER=#BFBF
outimstack=#5F00

        INCLUDE "gsports.asm"
; Card addresses (see the header for the memory map)
GSPROG=#5830                    ; card program (GSPROGGO), sent by GS commands #18/#19
GSSTACK=#5830                   ; card stack, grows down below the program
; both card halves copy their code to #5900 and run there: they remap #8000-#FFFF while working
GSROTPRECODE=#5900
GSROTATECODE=#5900

gspgcode=#10
gspgdata=#20

GSpglogo=4
GSLOGO=#7D00
GSLOGOPROG=GSLOGO+#1B00
GSpgrlscr=2                     ;#2F ;32K (en: the texture byte map made by GENMAP)
GSpgcopy1=#30                   ;32K*16 (en: the 16 rotated copies, blocks #30-#3F)
GSTDHL=#1B00
GSTDHL2=GSTDHL+384
GSGENPROG=#4000                 ;GSTDHL+384
GSGENROTBUF=GSTDHL              ; precalc only: 256-byte line buffer (reuses the line table area)
GSTMIRROR=GSTDHL+256            ; precalc only: bit-reversal table
        IF genldi
GSLDI256=#3D00
        ENDIF
GSSCR1=#0000
GSSCR2=#2000

        MACRO GSSETPGA          ; card: page A at #C000 (not used in this file)
        OUT (MPAGEX),A
        ENDM

; ZX: select #7FFD value A and remember it in curpg (the interrupt handler restores it after the music)
        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; ---------------------------------------------------------------------------------------------------------------------
; GO - the author's stand-alone test launcher (not in the demo; THELINK.asm has the real one).
; Resets the card, sends it GSPROG through the card firmware, uploads pgdata1 and the two code pages by ZX-DMA,
; installs the resident loop and IM 2 handler, starts the music and runs the effect pages from pgeffects.
; ---------------------------------------------------------------------------------------------------------------------
        ORG #7800
begin
GO
        XOR A
        OUT (#FE),A
        LD SP,#6000

        LD A,C_GRST
        OUT (GSCTR),A           ; hard reset of the card
        XOR A
        OUT (GSCOM),A           ;не помогает (en: does not help)
        HALT
        HALT                    ; two frames for the card firmware to come up
        DI
;зачем-то POKE #4092,0
; en: for some reason, POKE #4092,0 (card memory) - GS commands #18 (set address) / #19 (write byte)
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
; en: for some reason, #38 (Load FX)
        SC #38
        WC
;зачем-то #D2 (Close Stream)
; en: for some reason, #D2 (Close Stream)
        SC #D2
        WC

; Card program -> card #5830, byte by byte: SD/SC/WC/WD are the gsports.asm macros (data out, command out,
; wait for the command bit, wait for the data bit to clear)
        LD HL,WASGSPROG
        LD DE,GSPROG
        LD BC,LENGSPROG
;как в RIFF TRACKER
; en: as in RIFF TRACKER
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
; en: for some reason, #F5 (Busy on)
        SC #F5
        WC
;#13 Jump to Address
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

        LD A,#14
        CALL CL1800PG           ; clear the pixel-sized areas of RAM pages 4 and 5
        LD A,#15
        CALL CL1800PG

; Upload by ZX-DMA: each command is answered by the card program (WN = wait for its data byte, GD = read it)
        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
;и ждем включения:
; en: and wait until it is on:
        WN
        GD
;
;шлем страницы данных в GS:
; en: send the data pages to the GS (here only one):
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения:
; en: and wait for the switch:
        WN
        GD
;
;шлем страницы кода в GS:
; en: send the code pages to the GS:
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,pgcode2
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA:
; en: and wait until the DMA is off:
        WN
        GD
;

; Resident loop and IM 2 handler to their places; IM 2 table: 257 bytes of low IMER at #BE00 -> vector #BFBF
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
        CALL #C000              ; music init

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO

; MOVEPAGE dst_page,src,count,dst: copy count/256 blocks of 256 bytes from src in the current page (curpg) to dst
; in page dst_page, through resbuf (RESMOVEPAGE). Kept from THELINK.asm; not used in this file.
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; GSDMASENDPG: A = #7FFD value. Copies the 16K page at #C000 into #0000-#3FFF: the writes go to the ZX ROM area
; (no effect on the ZX) and ZX-DMA stores every byte in card RAM at the card's DMA address, which then advances.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; CL1800PG: A = #7FFD value; zeroes #C000-#D7FF and #E000-#F7FF of that page. CL1800: zero #1800 bytes at HL
; (L = 0 on entry, so (HL),L writes 0)
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
; GSPROGGO - card program (runs on the NeoGS at #5830; GS command #13 jumps here)
; Silences the 8 channels, sets 24 MHz + NOROM + EXPAG, selects ZX-DMA (DMA module 1), then serves the upload
; commands 3 / 4 / 5 from the ZX and ends in GSRESIDENTGO, the card's loop over its code pages.
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
        JR NZ,$-3               ; wait for command 3

; DMA address = 32K block gspgdata: RRCA turns the block number into address bits 15-21 -> DMA_HAD (bits 16+),
; DMA_MAD bit 7 (bit 15)
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: the sign for the ZX)

        LD E,4
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3               ; wait for command 4: code pages go to block gspgcode

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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: the sign for the ZX)

        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3               ; wait for command 5: upload done

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: the sign for the ZX)
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready? - waits until the ZX has read the answer)

        LD A,gspgcode
;JP GSRESIDENTGO

; Card resident loop: map code page A at #C000, CALL #C000 (one effect, or one precalc), next page.
; RLCA : INC A : RRCA steps through the 16K pages in the author's notation: #10, #90, #11, #91, ...
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
; ZX resident loop (runs at #BF04): for each effect page (A = #7FFD value, bit 4 = 48K ROM forced) read the start and
; end frame from zxtimings and CALL #C000 with BC = start, DE = end. Here: ROTATEZX on #B0, then #B1 = JR $ (stop).
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
zxtimings
        DW 896,896*2-20         ;rotate (en: start, end frame; in the demo 896*2, 896*3-14)
        ENT
LENZXRES=$-WASZXRES

; ---------------------------------------------------------------------------------------------------------------------
; IM 2 handler (at #BFBF): frame counter + music, on its own stack.
; The interrupted SP may be a data pointer, so the handler touches only the word the CPU has just pushed: it
; replaces the return address there with DE, keeps the return address in outimjp, and at the end pops DE back
; from that word and leaves with JP. The music page keeps bits 3-4 (shown screen, ROM) of the current #7FFD value.
; Saved: AF, BC, DE, HL, AF'; the music player must leave IX, IY and the alternate set alone.
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
        CALL #C005              ; music play
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
; ROTPREGS - card precalc (object at #C000; the card resident loop CALLs #C000 with this page at #C000).
; Copies its code to #5900 and the texture (titletex, 128 lines in ZX screen layout) to card #4000, then:
;   - GSLDI256 at #3D00: 256 x LDI + RET, generated;
;   - GENMAP: the texture as a 256x128 BYTE map in block GSpgrlscr: the byte at (line, x) = the 8 pixels before
;     pixel x of that line (wrapping), so any pixel position can be read as a whole byte;
;   - GENROTCOPY x 8: blocks GSpgcopy1+0..7: for 8 directions (GSROTTAB), byte (line, x) = the 8 pixels sampled along
;     that direction from (line, x) - the rotated picture can then be drawn one byte (8 pixels) per read;
;   - GENROTMIRROR x 8: blocks GSpgcopy1+8..15 = blocks 0..7 with every byte bit-reversed (the 8 opposite directions).
; Returns to the card resident loop.
; =====================================================================================================================
        ORG #C000,pgcode1
pgrotpregs=pgcode1
        LD HL,WASGSROTPRECODE
        LD DE,GSROTPRECODE
        LD BC,LENGSROTPRECODE
        LDIR

        LD HL,wasscr
        LD DE,#4000
        LD BC,#1000
        LDIR
        JP GSROTPREGO

WASGSROTPRECODE
        DISP GSROTPRECODE
GSROTPREGO
        IF genldi
        LD HL,GSLDI256
        LD B,0
MKLDI0  LD (HL),#ED
        INC HL
        LD (HL),#A0             ;ldi
        INC HL
        DJNZ MKLDI0
        LD (HL),201             ;ret
        ENDIF

;делает из экрана сдвинутые копии
;чтобы HL=YX
; en: makes shifted copies out of the screen, so that HL = YX (H = line, L = pixel x)
; Source: card #4000 in ZX screen layout (GSDHL steps a line down), 128 lines x 32 bytes.
; Destination: block GSpgrlscr at #8000-#FFFF, line n of the texture at #FF00-n*256 (upside down in memory).
; Per line: A starts as the line's last byte (wrap-around); for each source byte C, 8 times: store A, shift the next
; pixel of C into A - so 256 output bytes, one per pixel position.
GENMAP
        LD A,GSpgrlscr
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A          ; block GSpgrlscr at #8000-#FFFF
        LD DE,#FF00
        LD HL,#4000
        LD B,128
GENMAP0
        PUSH HL
        LD A,L
        ADD A,31
        LD L,A
        LD A,(HL)
        POP HL
        PUSH HL
GENMAP1
;A=байт левее (картинка будет сдвинута на 8 вправо)
; en: A = the byte to the left (the picture will be shifted 8 pixels to the right)
        LD C,(HL)
        INC L
        DUP 8
        LD (DE),A
        RL C
        RLA
        INC E
        EDUP
        JR NZ,GENMAP1           ; Z from INC E: 256 bytes done
        POP HL
        CALL GSDHL
        DEC D
        DJNZ GENMAP0
; 8 rotated copies -> blocks GSpgcopy1..+7 (IXH = target block, HL walks GSROTTAB)
        LD HL,GSROTTAB
        LD IXH,GSpgcopy1
        LD B,8
GRCOP0  PUSH BC
        CALL GENROTCOPY
        POP BC
        DJNZ GRCOP0
; GSTMIRROR: 256-byte table, byte n = n with its bit order reversed
        LD HL,GSTMIRROR
GENMIR0 LD B,8
        RLC L
        RRA
        DJNZ $-3
        LD (HL),A
        INC L
        JR NZ,GENMIR0
; 8 mirrored copies: IXL = source block (copy 0..7), IXH = target block (continues at GSpgcopy1+8)
        LD IXL,GSpgcopy1
        LD B,8
GRMIR0  PUSH BC
        CALL GENROTMIRROR
        POP BC
        DJNZ GRMIR0
        RET

; GENROTCOPY: one rotated copy. HL = 8 sample offsets in GSROTTAB (DW #yyxx, bits 7..0), IXH = target block.
; For each of the 128 lines (DE' = output line address, D' also the line number for the source):
;   for each of the 8 bits: source line = D' + dy, starting at x = dx; bit 7 of each of the 256 map bytes is
;   shifted into the matching byte of GSGENROTBUF (RL (HL)); after 8 passes each buffer byte holds the 8 samples;
;   then the target block is mapped and GSLDI256 copies the buffer to DE' (DE' += 256).
; The source and target share #8000-#FFFF, so the blocks are switched twice per line.
; Out: HL = next GSROTTAB entry, IXH + 1.
GENROTCOPY
        EXX
        IF zx
        LD DE,#C000
        ELSE
        LD DE,#8000
        ENDIF
        EXX
        IF zx
        LD B,#40
        ELSE
        LD B,#80
        ENDIF
GENROTLINES
        PUSH BC
        PUSH HL
        LD A,GSpgrlscr
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD B,8
GENROTBITS
        LD E,(HL)
        INC HL
        LD A,(HL)
        EXX
        ADD A,D
        EXX
        LD D,A
        INC HL
        PUSH HL
        SET 7,D
        IF zx
        SET 6,D
        ENDIF
;копируем 7-е биты в буфер
; en: copy the 7th bits into the buffer
        LD HL,GSGENROTBUF
GENROT1
        DUP 8
        LD A,(DE)
        RLA
        RL (HL)
        INC E
        INC L
        EDUP
        JP NZ,GENROT1
        POP HL
        DJNZ GENROTBITS
        LD A,IXH
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        EXX
        LD HL,GSGENROTBUF
        CALL GSLDI256
        EXX
        POP HL
        POP BC
        DJNZ GENROTLINES
        LD C,16
        ADD HL,BC               ;HL = следующая таблица координат (en: HL = the next coordinate table)
        INC IXH
        RET

; GENROTMIRROR: target block IXH = source block IXL with every byte bit-reversed through GSTMIRROR
; (the same 8 samples in the opposite order: the opposite direction). Line by line through GSGENROTBUF.
; Out: IXH + 1, IXL + 1.
GENROTMIRROR
        IF zx
        LD B,#40
        ELSE
        LD B,#80
        ENDIF
        IF zx
        LD DE,#C000
        ELSE
        LD DE,#8000
        ENDIF
GENMIRLINES
        PUSH BC
        LD A,IXL
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        EX DE,HL
        LD B,high GSTMIRROR
        LD DE,GSGENROTBUF
GENMIR1 LD C,(HL)
        LD A,(BC)
        LD (DE),A
        INC L
        INC E
        JR NZ,GENMIR1
        EX DE,HL
        LD A,IXH
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A
        LD HL,GSGENROTBUF
        CALL GSLDI256
        POP BC
        DJNZ GENMIRLINES
        INC IXH
        INC IXL
        RET

; with genldi=0 the 256 x LDI copy is assembled here instead of generated at #3D00
        IF (genldi)==0
GSLDI256=$

        DUP 256
        LDI
        EDUP
        RET
        ENDIF

; GSROTTAB: 8 sampling directions spanning a half turn, 8 words each (16 bytes per direction).
; Each word is DW #yyxx: high byte = line offset, low byte = x offset in the byte map (signed), for bits 7..0.
; The sketches show the 8 sample points of each direction around the anchor '.'.
GSROTTAB
;координаты битов в порядке 7..0
; en: coordinates of the bits in the order 7..0
;
;
;
;        . 2 1 0
;7 6 5 4
;
;
;
        DW #01FC
        DW #01FD
        DW #01FE
        DW #01FF
        DW #0000
        DW #0001
        DW #0002
        DW #0003
;
;
;          1 0
;        . 2
;    5 4
;  7 6
;
;
        DW #02FD
        DW #02FE
        DW #01FE
        DW #01FF
        DW #0000
        DW #0001
        DW #FF01
        DW #FF02
;
;          0
;        2 1
;        .
;      4
;    6 5
;    7
;
        DW #03FE
        DW #02FE
        DW #02FF
        DW #01FF
        DW #0000
        DW #FF00
        DW #FF01
        DW #FE01
;        0
;        1
;        2
;        .
;      4
;      5
;      6
;      7
        DW #04FF
        DW #03FF
        DW #02FF
        DW #01FF
        DW #0000
        DW #FF00
        DW #FE00
        DW #FD00
;      0
;      1
;      2
;      3 .
;        4
;        5
;        6
;        7
        DW #0400
        DW #0300
        DW #0200
        DW #0100
        DW #00FF
        DW #FFFF
        DW #FEFF
        DW #FDFF
;
;    0
;    1 2
;      3 .
;        4
;        5 6
;          7
;
        DW #0301
        DW #0201
        DW #0200
        DW #0100
        DW #00FF
        DW #FFFF
        DW #FFFE
        DW #FEFE
;
;
;  0 1
;    2 3 .
;        4 5
;          6 7
;
;
        DW #0202
        DW #0201
        DW #0101
        DW #0100
        DW #00FF
        DW #00FE
        DW #FFFE
        DW #FFFD
;
;
;
;0 1 2 3 .
;        4 5 6 7
;
;
;
        DW #0103
        DW #0102
        DW #0101
        DW #0100
        DW #00FF
        DW #00FE
        DW #00FD
        DW #00FC

; GSDHL: HL = the next line down in ZX screen layout
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
LENGSROTPRECODE=$-WASGSROTPRECODE
        DISPLAY /T,LENGSROTPRECODE
wasscr
        INCBIN "titletex",0,#1000      ; the texture: the top 128 lines (two thirds) of a ZX screen
LENROTPREGS=$-#C000

; =====================================================================================================================
; ROTATEGS - card effect (object at #C000, CALLed by the card resident loop).
; Copies its code to #5900, puts the logo (thelink$) byte-reversed at GSLOGO in block GSpglogo, copies its reversed
; attributes into both screen buffers, then GSROTATEGO: generates the two logo stamp programs (MKLOGO) and the two
; line tables (GENDHL) and runs GSLOOP until the ZX sends command 2.
; =====================================================================================================================
        ORG #C000,pgcode2
pgrotategs=pgcode2
        LD HL,WASGSROTATECODE
        LD DE,GSROTATECODE
        LD BC,LENGSROTATECODE
        LDIR

; Leftover from ROTPREGS: wasscr is ROTPREGS' label, so this copies whatever lies at that address in THIS page to
; card #4000. Harmless: #4000 is GSGENPROG, which MKROT fills before it is ever run.
        LD HL,wasscr
        LD DE,#4000
        LD BC,#1000
        LDIR

        LD A,GSpglogo
        OUT (MPAG),A

; the 6912-byte logo screen, last byte first, to GSLOGO..GSLOGO+#1AFF (#7D00-#97FF): reversed attributes at
; #7D00-#7FFF, reversed pixels from #8000; then the attributes to both buffers
        LD HL,waslogo
        LD DE,GSLOGO+#1B00
        LD BC,#1B00
GSGET1  LD A,(HL)
        INC HL
        DEC DE
        LD (DE),A
        DEC BC
        LD A,B
        OR C
        JR NZ,GSGET1
        LD HL,GSLOGO
        LD DE,GSSCR1+#1800
        LD BC,768
        LDIR
        LD HL,GSLOGO
        LD DE,GSSCR2+#1800
        LD BC,768
        LDIR
        JP GSROTATEGO

WASGSROTATECODE
        DISP GSROTATECODE
        IF counter
GSDIGGFX
_=#3D80
        DUP 80
        DB {_}
_=_+1
        EDUP
        ENDIF

GSROTATEGO
        LD A,GSpglogo
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A

; Two logo stamp programs (for SCR1 at GSLOGOPROG, for SCR2 right after it). IX packs two parameters:
; IXH = high byte of (buffer - logo pixels) (the low byte is 0), IXL = #C0 = 192 lines.
        LD DE,GSLOGO+#300
        LD HL,GSLOGOPROG
        LD IX,GSSCR1-(GSLOGO+#300)+#C0
        CALL MKLOGO
        LD (gslogoprog2),HL
        LD DE,GSLOGO+#300
        LD IX,GSSCR2-(GSLOGO+#300)+#C0
        CALL MKLOGO

; line tables: GSTDHL for SCR1, then (HL continues) GSTDHL2 for SCR2
        LD HL,GSTDHL
        LD DE,GSSCR1+#1F
        CALL GENDHL
        LD DE,GSSCR2+#1F
        CALL GENDHL

; ---------------------------------------------------------------------------------------------------------------------
; GSLOOP - one picture per pass: draw into the free buffer (gsdrawscr: 0 = SCR1, 1 = SCR2, self-modified operand),
; stamp the logo, then the handshake with the ZX (see the header) and point the DMA at the buffer just drawn.
; ---------------------------------------------------------------------------------------------------------------------
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: drawing into SCR2)
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now drawing into SCR1)
;потом дадим его ZX'у
; en: then we give it to the ZX
        CALL GSEFFECT

        LD A,GSpglogo
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A          ; the logo block back at #8000-#FFFF (GSEFFECT mapped a texture copy there)
        LD A,(gsdrawscr)
        OR A
        JR NZ,GSLOOPLOGO2
        CALL GSLOGOPROG
        JR GSLOOPLOGOOK
GSLOOPLOGO2
gslogoprog2=$+1
        CALL 0
GSLOOPLOGOOK
;GS ожидает конец кадра у ZX
; en: the GS waits for the end of the frame on the ZX (command 0; this wait has no timeout)
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t (en: 48 T-states per poll)
        IF counter
        LD A,(gsdrawscr)
        OR A
        LD DE,GSSCR1+#17FF
        JR Z,$+4
        LD D,high GSSCR2+#17
        CALL GS12345
        IF dump
        LD HL,GSLDI256
        LD B,23
        CALL DUMPPP
        ENDIF
        ENDIF

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
;после этого ZX можно сбрасывать
; en: after this the ZX may be reset
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: the sign for the ZX)
;GS ожидает обнуления (ZX запросит данные)
; en: the GS waits for the zeroing (the ZX will request the data); in fact it waits for a non-zero command:
; 1 = send the picture, anything else (2) = end of the effect. 65536 polls without one (the ZX was reset): GSRESET
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT (en: back to the card resident loop)

; DMA address = the buffer just drawn (card address #0000 or #2000), DMA on
        XOR A
        OUT (DMA_HAD),A
        LD A,(gsdrawscr)
        OR A
        LD A,high GSSCR1
        JR Z,$+4
        LD A,high GSSCR2
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: the sign for the ZX)
        JP GSLOOP

; GENDHL: 192 words at HL, one per buffer line: the address + 1 of the line's first byte as the ZX will show it
; (the generated code sets SP there and PUSHes downwards). DE starts at buffer+#1F and steps like a ZX "next line
; down"; in the byte-reversed buffer that walks the ZX screen from line 191 up to line 0.
GENDHL
        LD B,192
GENDHL0 INC DE
        LD (HL),E
        INC L
        LD (HL),D
        INC HL
        DEC DE
        INC D
        LD A,D
        AND 7
        JR NZ,GENDHLN
        LD A,E
        ADD A,32
        LD E,A
        JR C,GENDHLN
        LD A,D
        SUB 8
        LD D,A
GENDHLN DJNZ GENDHL0
        RET

; ---------------------------------------------------------------------------------------------------------------------
; MKLOGO - generates the code that stamps the logo into a screen buffer, line by line:
;   LD HL,<first used byte of the line> then per byte: LD (HL),n  or  LD A,(HL) : AND n : LD (HL),A  or nothing,
;   each followed by INC L; trailing INC Ls dropped; RET at the end. In: HL = where to write the code.
;   Out: HL = after the RET.  A cell with attribute 7 is a mask cell: the logo byte is ANDed in (#FF = skip);
;   any other cell is written as is.  Lines without logo bytes generate nothing.
; ---------------------------------------------------------------------------------------------------------------------
MKLOGO
;DE=образец экрана
;HX=смещение рабочего экрана относительно DE
;LX=высота
;если атрибут 7, то все 0 затереть (байт из лого прямо в AND)
;иначе прямо пишем байты
; en: DE = the sample screen (the reversed logo pixels)
; en: HX (IXH) = offset of the working screen relative to DE
; en: LX (IXL) = height
; en: if the attribute is 7, clear all the 0 bits (the logo byte goes straight into AND)
; en: otherwise write the bytes directly
MKLOGOLINE
;1. находим начало линии
; en: 1. find the start of the line (the lowest column with a logo byte)
        LD IYH,32               ;начало линии (32=нету) (en: line start, 32 = none)
        LD A,D
        RRA
        RRA
        RRA
        AND 3
        ADD A,high GSLOGO
        LD B,A
        LD C,E
MKLOGOMM0
        LD A,(BC)               ;attr
        CP 7
        JR NZ,MKLOGOMMnMASK
        LD A,(DE)               ;pix
        INC A
        JR Z,MKLOGOMMEMPTY
MKLOGOMMnMASK
        LD A,C
        AND #1F
        CP IYH
        JR NC,$+4
        LD IYH,A
MKLOGOMMEMPTY
        INC DE
        INC C
        LD A,C
        AND #1F
        JR NZ,MKLOGOMM0
        LD A,32
        SUB IYH
        LD B,A
        JR Z,MKLOGOLINEQ
;2. если линия есть, то генерим LD HL,.. и заполнение
; en: 2. if there is a line, generate LD HL,.. and the filling (B = bytes from the start column to the line end)
        LD A,E
        SUB B
        LD E,A
        JR NC,$+3
        DEC D
        LD (HL),33
        INC HL
        LD (HL),E
        INC HL
        LD A,D
        ADD A,IXH
        LD (HL),A
        INC HL
MKLOGO1 PUSH DE
        LD A,D
        RRA
        RRA
        RRA
        AND 3
        ADD A,high GSLOGO
        LD D,A
        LD A,(DE)
        CP 7
        POP DE
        LD A,(DE)
        INC DE
        JR NZ,MLOGnMASK
        CP #FF
        JR Z,MLOGNO
;можно ускорить через RES
; en: could be sped up with RES (the opcodes are written as characters: '~' = #7E, 'w' = #77, ',' = #2C)
        LD (HL),'~'             ;ld a,(hl)
        INC HL
        LD (HL),230             ;and N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),'w'             ;ld (hl),a
        INC HL
        JR MLOGPOK
MLOGnMASK
        LD (HL),54              ;ld (hl),N
        INC HL
        LD (HL),A
        INC HL
MLOGPOK LD (MLOGHL),HL
MLOGNO  LD (HL),','             ;inc l
        INC HL
        DJNZ MKLOGO1
MLOGHL=$+1
        LD HL,0                 ;после последнего записанного байта (en: after the last written byte)
MKLOGOLINEQ
;DE=следующая строка (в нелинейном порядке)
; en: DE = the next line (in the non-linear ZX screen order)
        DEC IXL
        JR NZ,MKLOGOLINE
        LD (HL),201
        INC HL
        RET

; ---------------------------------------------------------------------------------------------------------------------
; GSEFFECT - draws one rotated, scrolled picture into the buffer gsdrawscr.
; Angle a (0..255 = one turn) = the low byte of the LD HL,GSTSIN operand at GSangle, +1 per picture.
; Maps the pre-rotated copy 15 - a/16 at #8000-#FFFF. Fixed point 8.8 throughout; sin32768: sin at GSTSIN+a (low)
; and +256 (high), cos(a) read as sin(64-a).
;   step along a screen line, per byte (8 pixels): (cos a, sin a) * 8 px  -> BC', DE' (sin32768/16);
;   step from line to line: (-sin a, cos a) * 1 px                        -> DE, BC after the EXX below;
;   start (IX = X, IY = Y): the centre (CENTERX, CENTERY) minus 128 px along the line and 96 px along the column.
; Then for each of 192 lines: SP = the line's address from GSTDHL/GSTDHL2, HL = YX (H OR #80 keeps it in the copy),
; JP to the per-line code MKROT generated; it comes back to GSROTOK.
; ---------------------------------------------------------------------------------------------------------------------
GSEFFECT
        LD HL,GSangle
        INC (HL)                ; a + 1 (self-modifies the LD HL,GSTSIN below)
        LD A,(HL)
        RRA
        RRA
        RRA
        RRA
        CPL
;ADD A,8
        AND 15
        ADD A,GSpgcopy1
        OUT (MPAG),A
        RLCA
        INC A
        RRCA
        OUT (MPAGEX),A          ; copy 15 - a/16 at #8000-#FFFF
GSangle=$+1
        LD HL,GSTSIN
        LD E,(HL)               ;sin
        INC H
        LD D,(HL)
        LD A,64
        SUB L
        LD L,A
        LD B,(HL)               ;cos
        DEC H
        LD C,(HL)
        DUP 4
        SRA D
        RR E                    ;dY
        SRA B
        RR C                    ;dX
        EDUP
        EXX

        LD IXL,#80              ; fractions 0.5 (rounding); MKROT only needs the differences, not the integer parts
        LD IYL,#80
        CALL MKROT

; Horizontal scroll: CENTERX += 500 + sin(phase)/16 per picture (8.8: about 2 px, +-8 px), phase += 3
GSmovephase=$+1
        LD HL,GSTSIN
        INC L
        INC L
        INC L
        LD (GSmovephase),HL
        LD E,(HL)
        INC H
        LD D,(HL)
        DUP 4
        SRA D
        RR E
        EDUP
        LD HL,500
        ADD HL,DE
        LD DE,(CENTERX)
        ADD HL,DE
        LD (CENTERX),HL

; line-to-line step: -sin a, cos a, scaled to 1 px (x2 then the high byte, sign-extended) -> DE, BC (EXX'd away);
; the same values /4 (32 px) stay for the centre correction
        LD HL,(GSangle)
        LD A,L
        ADD A,128
        LD L,A
        LD E,(HL)               ;-sin
        INC H
        LD D,(HL)
        LD A,64+128
        SUB L
        LD L,A
        LD B,(HL)               ;cos
        DEC H
        LD C,(HL)
        PUSH BC
        PUSH DE
        LD A,B
        RLA
        SBC A,A
        SLA C
        RL B
        RLA
        LD C,B
        LD B,A
        LD A,D
        RLA
        SBC A,A
        SLA E
        RL D
        RLA
        LD E,D
        LD D,A
        EXX
        POP DE
        POP BC
;коррекция центра
; en: centre correction: X = CX - 3*(-sin*32) - 4*(cos*32), Y = CY - 3*(cos*32) + 4*(-sin*32)
; (the carry is not cleared before the first SBC: at most 1/256 px)
        DUP 2
        SRA D
        RR E
        SRA B
        RR C
        EDUP
CENTERX=$+1
        LD HL,#80               ;X
        SBC HL,DE
        SBC HL,DE
        SBC HL,DE
        SBC HL,BC
        SBC HL,BC
        SBC HL,BC
        SBC HL,BC
        PUSH HL
        POP IX
CENTERY=$+1
        LD HL,#80               ;Y (en: constant, nothing writes CENTERY)
        SBC HL,BC
        SBC HL,BC
        SBC HL,BC
        ADD HL,DE
        ADD HL,DE
        ADD HL,DE
        ADD HL,DE
        PUSH HL
        POP IY

; The line loop. SP is both the line-table reader and the PUSH target, so the stack is saved and unusable here.
; One register set: C = #80 (OR mask for H) for the generated code; the other: HL = line-table pointer,
; DE / BC = line-to-line step; A' = line counter.
        LD (GSEFFECTSP),SP
        IF zx
        LD C,#C0
        ELSE
        LD C,#80
        ENDIF
        EXX
        LD A,(gsdrawscr)
        OR A
        LD HL,GSTDHL
        JR Z,$+5
        LD HL,GSTDHL2
        LD A,192
ROT0    EX AF,AF'
        LD SP,HL
        EXX
        POP HL                  ; this line's address in the buffer (+1)
        LD SP,HL
        LD A,IXH
        LD L,A
        LD A,IYH
        OR C
        LD H,A                  ;HL=YX
        JP GSGENPROG
GSROTOK EXX
        ADD IX,DE
        ADD IY,BC               ; next line's start point
        INC L
        INC HL                  ; next table entry
        EX AF,AF'
        DEC A
        JP NZ,ROT0
GSEFFECTSP=$+1
        LD SP,0
        RET

;генератор
;LD D/E,(HL)
;[PUSH DE]
;[LD A,L:SUB N:LD L,A]
;[LD A,H:SUB N:OR C:LD H,A]
;...
;JP (IX)
;генератор ест 9275 тактов
; en: generator (of the per-line code at GSGENPROG): for each of the 32 bytes of a line
; en:   LD D,(HL) or LD E,(HL) [+ PUSH DE after every second byte]; then the step to the next byte:
; en:   LD A,L : SUB N : LD L,A  and  LD A,H : SUB N : OR C : LD H,A  (N = the integer part of the step, from the
; en:   running 8.8 sum, so the fractions do not drift); the step after the last byte is overwritten by JP GSROTOK
; en:   (the comment's "JP (IX)" is an older version). The generator eats 9275 T-states.
MKROT
;IX=Xx
;BC'=dXx
;IY=Yy
;DE'=dYy
        LD HL,GSGENPROG
        LD B,32
MKR0
        BIT 0,B                 ; B even: LD D,(HL); B odd: LD E,(HL) : PUSH DE
        LD (HL),#56             ;ld d,(hl)
        JR Z,$+7
        LD (HL),#5E             ;ld e,(hl)
        INC HL
        LD (HL),#D5             ;push de
        INC HL
        LD (mkrENDHL),HL        ; where JP GSROTOK goes if this is the last byte
        LD E,IYH
        LD A,IXH
        EXX
        ADD IX,BC
        ADD IY,DE
        EXX
        SUB IXH
        LD (HL),#7D             ;ld a,l
        INC HL
        LD (HL),#D6             ;sub N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),#6F             ;ld l,a
        INC HL
        LD A,E
        SUB IYH
        LD (HL),#7C             ;ld a,h
        INC HL
        LD (HL),#D6             ;sub N
        INC HL
        LD (HL),A
        INC HL
        LD (HL),#B1             ;or c
        INC HL
        LD (HL),#67             ;ld h,a
        INC HL
        DJNZ MKR0
mkrENDHL=$+1
        LD HL,0
        LD (HL),#C3
        INC HL
        LD (HL),low GSROTOK
        INC HL
        LD (HL),high GSROTOK
;INC HL
        RET

; Debug (counter=1): GS12345 / GS123 XOR the decimal digits of HL into the buffer at DE (8x8 font GSDIGGFX),
; DUMPPP (dump=1) prints B bytes from HL with their addresses. Not assembled in the release build.
        IF counter
GS12345
        LD BC,10000
        CALL GSDIG
        LD BC,1000
        CALL GSDIG
GS123
        LD BC,100
        CALL GSDIG
        LD BC,10
        CALL GSDIG
        LD BC,1
GSDIG
        XOR A
        ADD A,8
        SBC HL,BC
        JR NC,$-4
        ADD HL,BC
        SUB 8-GSDIGGFX
        PUSH DE
        PUSH HL
        LD L,A
        LD H,high GSDIGGFX
        LD B,8
GSDIG0  LD A,(DE)
        XOR (HL)
        LD (DE),A
        INC L
        DEC D
        DJNZ GSDIG0
        POP HL
        POP DE
        DEC E
        RET
        IF dump
DUMPPP
DUMP0
        LD A,E
        SUB 32
        LD E,A
        JR NC,$+6
        LD A,D
        SUB 8
        LD D,A
        PUSH BC
        PUSH DE
        PUSH HL
        PUSH HL
        CALL GS12345
        DEC E
        POP HL
        LD L,(HL)
        LD H,0
        CALL GS123
        POP HL
        POP DE
        POP BC
        INC HL
        DJNZ DUMP0
        RET
        ENDIF
        ENDIF

; GSRESET: the ZX stopped answering (reset): DMA off, back to the firmware's configuration (10 MHz, ROM, no EXPAG)
; and restart the card firmware
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

        DISPLAY $
        DS low (-$)             ; align GSTSIN to 256 (the code steps from low to high bytes with INC H)
GSTSIN
        INCBIN "sin32768"       ; 256 low bytes, then 256 high bytes of 32768*SIN(n/256*2*PI) (#7FFF at n=64)
        ENT
LENGSROTATECODE=$-WASGSROTATECODE
        DISPLAY /T,LENGSROTATECODE
waslogo
        INCBIN "thelink$"       ; the logo: a full ZX screen, 6912 bytes
LENROTATEGS=$-#C000

; =====================================================================================================================
; ROTATEZX - ZX half (object at #C000 of its effect page; called by the resident loop with BC = start frame,
; DE = end frame). Copies itself to ROTATECODE (#7800) and runs there, because it pages #C000 for the screens.
; Waits for the start frame, blacks out both screens' attributes, then every frame: HALT, command 0, flip screens,
; command 1, fetch the card's picture by ZX-DMA reads into the hidden screen with PUSH. At the end frame (or SPACE):
; command 2, back to screen 0, attributes black, RET.
; Screens: #7FFD #17 = RAM 7 at #C000, screen 0 (RAM 5) shown; #1D = RAM 5 at #C000, screen 1 (RAM 7) shown;
; so the screen written at #C000 is always the hidden one.
; =====================================================================================================================
        ORG #C000,pgeffects
pgrotatezx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASROTATECODE
        LD DE,ROTATECODE
        LD BC,LENROTATECODE
        LDIR
        JP ROTATECODE

WASROTATECODE
        DISP ROTATECODE
        POP HL
        LD (zxendtime),HL
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART        ; wait for the start frame

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
        LDIR                    ; both screens' attributes to 0 (black) until the first picture arrives

        XOR A                   ;turbo
        LD BC,#EFF7
        OUT (C),A               ; #EFF7 = 0: on the author's Pentagon 1024SL bit 4 = 0 is turbo on

BIGLOOP
        HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
; en: let the GS know that the frame is ready
; en: and the DMA must be switched off:
        XOR A
        OUT (GSCOM),A

; flip: the XOR operand is self-modified ($-1 = the operand of XOR #1D), so A alternates #17 / #1D
        LD A,#1D^#17
        XOR #1D                 ;видим scr1, рисуем scr0 (en: we see scr1, we draw scr0)
        LD ($-1),A
        setpga
;теперь видим scr0, рисуем scr1
; en: now we see scr0, we draw scr1

;и ждем выключения:
; en: and wait until it is off:
        WN
        GD
;
zxendtime=$+1
        LD DE,0
        LD HL,(timer)
        OR A
        SBC HL,DE
        JP NC,QUIT
        LD A,#7F
        IN A,(#FE)
        RRA                     ;space
        JP NC,QUIT              ;можно жать RESET (en: one may press RESET)
;даем знать GS'у, что надо включить DMA
; en: let the GS know that it must switch the DMA on
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
; en: and wait until it is on:
        WN
        GD
;
; The fetch: every read from #00xx (ROM paged) is a ZX-DMA read and returns the byte fetched by the PREVIOUS read,
; so the first read is thrown away. The stream (the card buffer, byte-reversed) is PUSHed from the top down:
; 128 x 24 pairs = 6144 pixel bytes below #D800, then 24 x 16 pairs = 768 attribute bytes below #DB00.
; DI: SP is the write pointer.
        DI
        LD (PUSHKASP),SP
        LD H,0
        LD A,(HL)               ;1ST BYTE DISCARDED
        LD B,128
        LD SP,#D800
PUSHKA0
        DUP #18
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP
        DJNZ PUSHKA0
        LD B,24
        LD SP,#DB00
PUSHKA1
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP
        DJNZ PUSHKA1
PUSHKASP=$+1
        LD SP,0
        EI
        IF border
        LD A,R
        OUT (254),A
        ENDIF
        JP BIGLOOP
QUIT
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of the effect; the card returns to its resident loop)
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
;LD A,C_GRST
;OUT (GSCTR),A
;JR $
        ENT
LENROTATECODE=$-WASROTATECODE
LENROTATEZX=$-#C000

; test launcher only: the next effect page stops the ZX after the rotator; the music page
        ORG #C000,pgeffects+1
        JR $

        ORG #C000,pgmusic
        INCBIN "thelinkm"

; Object table for SAVEOBJ4: name + type (9 bytes), start, length, #7FFD page, start address
        ORG end
ObjTab
        DB 'ROTATEZXC'
        DW #C000
        DW LENROTATEZX
        DB pgrotatezx
        DW #C000

        DB 'ROTPREGSC'
        DW #C000
        DW LENROTPREGS
        DB pgrotpregs
        DW #C000

        DB 'ROTATEGSC'
        DW #C000
        DW LENROTATEGS
        DB pgrotategs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
        SLOT 3
        PAGE pgrotatezx
        SAVEBIN "ROTATEZX",#C000,LENROTATEZX
        PAGE pgrotpregs
        SAVEBIN "ROTPREGS",#C000,LENROTPREGS
        PAGE pgrotategs
        SAVEBIN "ROTATEGS",#C000,LENROTATEGS
