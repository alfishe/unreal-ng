; =====================================================================================================================
; GSMULBA3 - "multi-bars", The Link (Alone Coder, 2009), Pentagon 1024 + NeoGS
; ---------------------------------------------------------------------------------------------------------------------
; An 80-frame transition between the rotating bars and the hedgehog: the picture on the screen (the card's copy is
; INCBIN "gameover") is cut into twelve horizontal bars, 16 pixel lines each; from the bottom bar upward each bar
; starts to fall with growing speed and stops below the screen, leaving black behind.
;
; What this file builds (only these two objects are saved, see ObjTab and the SAVEBIN block at the end):
;   MULBARGS  card code, ORG #C000 in #7FFD page pgcode1 (#52) here; the release puts it at #33 (card code 6).
;             The card copies its code to #5900 (GSMULBARCODE), the picture "gameover" (6912 bytes, wasscr) to gsscr.
;   MULBARZX  ZX half, ORG #C000 in #7FFD page pgeffects (#B0) here; the release puts it at #B3. Its entry copies the
;             main part to #7800 (MULBARCODE, over the no longer needed GO block) and runs it there.
; Everything else here is a stand-alone test bench the author ran from ALASM, not saved: GO at #7800 (a copy of the
; demo's start-up: card program, ZX-DMA upload, IM 2, music, resident loop) with its own timing table (effect at
; frames 8..88, in the demo at 3476..3556), a JR $ in page pgeffects+1 to stop after the effect, the music
; "thelinkm" in page pgmusic, and a "show the picture, then save" entry after the SAVEOBJ4 include.
;
; How the two halves cooperate - the card writes a PROGRAM for the ZX each frame:
;   The card keeps, per screen line, which line of its copy of the picture is visible there (or none = black) and
;   which of the two ZX screens (5 at #4000 or 7 at #C000) must be displayed at that line. From that it compiles
;   a byte stream of commands into card RAM (GScmd1 / GScmd2, alternately) and streams it to the ZX by ZX-DMA.
;   The ZX reads the stream with HL = #0000 (ROM area, every read answered by ZX-DMA with the next byte):
;   a command byte is the high byte of a page-aligned handler in MULBARZX (CMDBLACK, CMDGFX, CMDDUMMY,
;   CMDWAITSWITCH, CMDTURBO, CMDENDFRAME), so "LD H,(HL) : JP (HL)" dispatches it (L = 0, the handler starts with
;   LD H,L to read #00xx again). The handlers clear or fill a 32-byte screen row with PUSH (SP = row end), waste
;   one scan line, or wait an exact number of t-states (self-modified JR into 127 NOPs) and then switch the
;   displayed screen with OUT (#7FFD) - mid-frame screen switching to change attributes inside a character cell.
;   The card counts the t-states of every command (CMD*TIME) to put the switches on the right lines.
;
; Frame protocol (as in the other effects, docs/disasm/demo/thelink/README.md section 6):
;   ZX: command 0 "frame done, stop DMA", wait for the answer; command 1 "start DMA", wait; HALT; run the stream
;   until CMDENDFRAME; call the music; again. Command 2 = end of the effect (the card RETurns to its resident loop).
;
; Converter's additions: the DEVICE line below, "low x" where ALASM keeps the low byte of a word value
; (comment "ALASM keeps the low byte"), "ORG $,0 ; orgpage-ok" (hand-edited, see there), and the SAVEBIN block at
; the end that replaces the author's ALASM object saver SAVEOBJ4.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
; border=1 colours the border by frame time (debug), 0 = black
border=0

; --- #7FFD pages of the stand-alone test bench (the demo's own plan is in THELINK.asm) ---
;GS data in ZX memory:
pgdata1=#50
pgcode1=#52

;ZX data:
pgmusic=#54  ; the music player: init #C000, play #C005
pgeffects=#B0  ; first ZX effect page; the resident loop goes #B0, #B1, ...

MULBARCODE=#7800  ; where the ZX half runs (copied from its page)

; --- the resident area, the same addresses as in THELINK.asm ---
resbuf=#BD00  ; 256-byte bounce buffer of RESMOVEPAGE
IMVEC=#BE00  ; IM 2 vector table (I = #BE, all bytes #BF -> IMER)
curpg=#BF01  ; the last value written to #7FFD
timer=#BF02  ; frame counter, incremented by IMER
ZXRESIDENT=#BF04  ; resident loop over the ZX effect pages
IMER=#BFBF  ; interrupt handler: frame counter + music
outimstack=#5F00  ; IMER's own stack

        INCLUDE "gsports.asm"
GSPROG=#5830  ; the card program, loaded by GS commands #18/#19
GSSTACK=#5830
GSMULBARCODE=#5900  ; card address of the effect code (copied from MULBARGS)

; card page numbers the ZX-DMA upload starts at (code, data)
gspgcode=#10
gspgdata=#20

timedefect=164  ; subtracted from the 80-line top-border budget 224*80 (t-states)

; --- card memory of the effect ---
GSpgdata=2  ; card page put at #8000 (MPAG) while the picture is copied
gsscr=#6000                     ;<#8000 и кратно #800 (en: < #8000 and a multiple of #800) - the card's copy of the picture
gsatr=#7800  ; its attributes
gsbars=#8000-(3*12)             ;скорость, Y на экране, Y графики (en: speed, Y on screen, Y in the picture) - 12 bars x 3 bytes
; Per-line tables, 192 lines, two 256-byte halves each (low byte page, high byte page), indexed by IXL = line:
;   newgfx/gfx = card address of the picture line shown at that line (0 = black), newatr/atr = card address of its
;   attribute row; bit 7 of the high byte = the ZX screen that line goes to (0 = #4000, 1 = #C000).
;   new* is this frame, gfx/atr the previous one (GSEFFECT copies new* there first).
newgfx=#4000                    ;2 набора. переключать или копировать? (en: 2 sets. switch or copy?)
gfx=newgfx+#200
newatr=gfx+#200
atr=newatr+#200
newgfx2=newgfx+#800  ; unused
gstdhlGS=#4C00  ; per line: card address of picture line (low #4C00, high #4D00), by GENDHL
gstdhlZX=#4E00                  ;концы строк (en: line ends) - per line: ZX screen address (low #4E00, high #4F00)

; Two command buffers at card CPU addresses #0000 / #2000, the DMA reads one while the card writes the other
; (the DMA address set for them: HAD = 0, MAD = high byte, LAD = 0).
;commands=#8000 ;2 набора (en: 2 sets)
GScmd1=#0000
GScmd2=#2000
GSpgfuture=2  ; card page at #C000 (MPAGEX) for futurecommands
futurecommands=#C000            ;1 набор (en: 1 set) - queue of CMDGFX for lines the beam has passed, ends with 0

; T-states of one command on the ZX (handler + its share of the dispatch), the card's time budget uses them.
; The command bytes themselves (CMDBLACK=high $ ...) are defined at the handlers in MULBARZX.
CMDBLACKTIME=204+15
CMDBLACKLEN=3                   ;cmd, addr (en: cmd, addr)
CMDGFXTIME=428+15
CMDGFXLEN=3+32                  ;cmd, addr, data (en: cmd, addr, data)
CMDDUMMYTIME=209+15

; card: select its page at #C000
        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

; ZX: select a #7FFD page and remember it in curpg
        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

; =====================================================================================================================
; Test bench start-up (not saved): the same sequence as GO in THELINK.asm, see README section 6.
; Hard-reset the card, send it GSPROG with GS commands #18/#19 and start it with #13, upload one data and one code
; page by ZX-DMA (commands 3, 4, 5), install the resident loop and IM 2 handler, init the music, run.
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
; en: POKE #4092,0 for some reason (GS commands #18 address, #19 data)
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
; en: #38 (Load FX) for some reason
;зачем-то #38 (Load FX)
        SC #38
        WC
; en: #D2 (Close Stream) for some reason
;зачем-то #D2 (Close Stream)
        SC #D2
        WC

; send the card program byte by byte: #18 = set address (DE), #19 = write byte
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
; en: #F5 (Busy on) for some reason
;зачем-то #F5 (Busy on)
        SC #F5
        WC
;#13 Jump to Address (en: #13 Jump to Address) - start GSPROGGO on the card
        SD low GSPROGGO
        SC #13
        WC
        SD high GSPROGGO
        WD

;LD A,#14
;CALL CL1800PG
;LD A,#15
;CALL CL1800PG

        LD A,3
        OUT (GSCOM),A           ;вкл. DMA (en: DMA on)
;и ждем включения: (en: and wait until it is on)
        WN
        GD
;
;шлем страницы данных в GS: (en: send the data pages to GS)
        LD A,pgdata1
        CALL GSDMASENDPG
        LD A,4
        OUT (GSCOM),A           ;новый адрес DMA (en: new DMA address)
;и ждем переключения: (en: and wait for the switch)
        WN
        GD
;
;шлем страницы кода в GS: (en: send the code pages to GS)
        LD A,pgcode1
        CALL GSDMASENDPG
        LD A,5
        OUT (GSCOM),A           ;выкл. DMA (en: DMA off)
;и ждем выключения DMA: (en: and wait until DMA is off)
        WN
        GD
;

; resident loop to #BF04, interrupt handler to #BFBF, IM 2 table #BE00-#BF00 all #BF -> vector #BFBF
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
        CALL #C000  ; music init

        LD HL,0
        LD (timer),HL
        EI

        LD A,pgeffects
        LD HL,zxtimings
        JP ZXRESIDENTGO  ; A = first effect page, HL = its start/end frames

; Copy IYH 256-byte blocks from the current page (HL) to page IXL (DE) through resbuf. Not used in this file.
        MACRO MOVEPAGE _arg0,_arg1,_arg2,_arg3
        LD A,(curpg)
        LD IXH,A
        LD IXL,_arg0
        LD HL,_arg1
        LD DE,_arg3
        LD IYH,(_arg2&#FFFF)/(256&#FFFF)
        JP RESMOVEPAGE
        ENDM

; Upload one 16 KB page by ZX-DMA: A = #7FFD page; LDIR #C000 -> #0000 writes into the ROM area, which the
; card's ZX-DMA takes into its RAM.
GSDMASENDPG
        setpga
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

; Clear the two pixel areas (#1800 bytes at #C000 and #E000) of page A. Only called from commented-out code.
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
        LD (HL),L  ; L = 0 at #C000/#E000
        LDIR
        RET

; ---------------------------------------------------------------------------------------------------------------------
; The card program (runs on the card at #5830): silence, 24 MHz / no ROM / extended paging, then the ZX-DMA
; upload handshake - wait for ZX command 3, DMA -> card page gspgdata; command 4, DMA -> gspgcode; command 5, DMA
; off. Then GSRESIDENTGO calls #C000 of each code page in turn (one effect each).
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
        JR NZ,$-3  ; wait for ZX command 3

; DMA address of card page A: RRCA splits the page number into DMA_HAD and the top bits of DMA_MAD
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for ZX "DMA ON")

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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for ZX "DMA ON")

        LD E,5
;LD A,E
;OUT (ZXDATWR),A ;готов к приему (en: ready to receive)
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for ZX "DMA OFF")
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов? (en: ZX ready?) - wait until the ZX has read the answer

        LD A,gspgcode
;JP GSRESIDENTGO

; run the effect code pages one after another: page at #C000, CALL #C000, next page
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
; ZX resident loop (at #BF04): page A|#10 at #C000, BC = start frame, DE = end frame from the table at HL,
; CALL #C000 of the page, next page. RESMOVEPAGE: see the MOVEPAGE macro.
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
zxtimings  ; - test bench: this effect only, frames 8..88
        DW 8,8+80               ;mulbar
        ENT
LENZXRES=$-WASZXRES

; ---------------------------------------------------------------------------------------------------------------------
; IM 2 handler (at #BFBF): frame counter + music. It is safe to interrupt code that uses SP as a data pointer:
; it borrows only the 2 bytes the CPU pushed (writes DE there, keeps the return address in outimjp), runs on its
; own stack outimstack and returns with POP DE / JP. The ZX effect also CALLs it directly once per frame.
; ---------------------------------------------------------------------------------------------------------------------
WASIMER
        DISP IMER
        EX DE,HL
        EX (SP),HL              ;пишем DE вместо адреса возврата (en: write DE in place of the return address)
        LD (outimjp),HL
        LD (outimsp),SP
        LD SP,outimstack
        PUSH DE                 ;hl до прерывания (en: hl before the interrupt)
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
        CALL #C005  ; music play: pgmusic, keeping bits 3-4 (screen, ROM) of curpg
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
        JP 0  ; back to the interrupted code (self-modified)
        ENT
LENIMER=$-WASIMER
end

; =====================================================================================================================
; MULBARGS - card half, ORG #C000 in its page (the card's resident loop CALLs #C000).
; Copies the effect code to GSMULBARCODE (#5900) and the picture (wasscr, 6912 bytes) to gsscr, then runs it.
; =====================================================================================================================
        ORG #C000,pgcode1
pgmulbargs=pgcode1
        LD HL,WASGSMULBARCODE
        LD DE,GSMULBARCODE
        LD BC,LENGSMULBARCODE
        LDIR

        LD A,GSpgdata
        OUT (MPAG),A
        LD HL,wasscr
        LD DE,gsscr
        LD BC,#1B00
        LDIR
        JP GSMULBARGO
WASGSMULBARCODE
        DISP GSMULBARCODE
; GSMULBARGO: build the line tables (GENDHL), mirror every row of the picture and attributes (the ZX PUSHes rows
; from their end, so a row must be sent last byte first), set up the bars, then the frame loop.
GSMULBARGO
        LD A,GSpgfuture
        OUT (MPAGEX),A

        CALL GENDHL

        LD HL,gsscr
        LD C,#C0+24  ; 192 pixel lines + 24 attribute rows
GSSCRFLIP
        LD D,H
        LD A,L
        OR 31
        LD E,A
        LD B,16
GSSCRFLIP0
        LD A,(HL)
        EX AF,AF'
        LD A,(DE)
        LD (HL),A
        EX AF,AF'
        LD (DE),A
        INC L
        DEC E
        DJNZ GSSCRFLIP0
        LD DE,16
        ADD HL,DE
        DEC C
        JR NZ,GSSCRFLIP

;инициализация (en: initialisation)
        LD HL,gsbars
        XOR A
gsbarsini0                      ; bar n: dy=0, Y = n*16 on screen and in the picture
        LD (HL),0               ;dy
        INC L
        LD (HL),A               ;Y бара (en: bar Y)
        INC L
        LD (HL),A               ;Y графики бара (en: bar picture Y)
        ADD A,16
        INC L
        JR NZ,gsbarsini0
;нижний бар запускаем сразу (dy=1): (en: start the bottom bar at once, dy=1)
        DEC L
        DEC L
        DEC L
        INC (HL)

        LD DE,GScmd1
        LD IX,newgfx
        CALL gsdrawbars
        CALL gsmovebars

; Card frame loop: compile the next frame's command stream (GSEFFECT), wait for ZX command 0 (frame done), stop
; the DMA and answer; wait for the next command (65536 polls, then GSRESET): 1 = point the DMA at the stream just
; compiled and start it, anything else (2) = end of the effect, RET to the card's resident loop.
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2 (en: draw into SCR2) - self-modified
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1 (en: now draw into SCR1)
;потом дадим его ZX'у (en: then give it to the ZX)
        CALL GSEFFECT

;GS ожидает конец кадра у ZX (en: GS waits for the end of the ZX frame)
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
;JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t per poll; no timeout here

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
;после этого ZX можно сбрасывать (en: after this the ZX may be reset)
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF" (en: flag for ZX "DMA OFF")
;GS ожидает обнуления (ZX запросит данные) (en: GS waits for non-zero (the ZX will request data))
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1
        DEC A
        RET NZ                  ;QUIT - any command but 1 ends the effect

        XOR A
        OUT (DMA_HAD),A
        LD A,(gsdrawscr)
        OR A
        LD A,high GScmd1
        JR Z,$+4
        LD A,high GScmd2
        OUT (DMA_MAD),A
        XOR A
        OUT (DMA_LAD),A
        LD A,#80                ;TURN ON DMA MODE
        OUT (DMA_CST),A
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON" (en: flag for ZX "DMA ON")
        JP GSLOOP

; =====================================================================================================================
; GSEFFECT - compile one ZX frame (en: frame). Keep last frame's tables (gfx, atr), redraw and move the bars into
; newgfx/newatr, then write the command stream at DE (GScmd1/GScmd2, alternating via self-modified gscommands):
;  1. top border: the attributes of the previous frame, line by line where they change (CMDBLACK for an empty row,
;     CMDGFX with 32 bytes otherwise), then CMDDUMMY lines and a CMDWAITSWITCH to the start of the paper, screen 0;
;  2. the paper: for every line where the displayed screen must change, fill the time up to it with CMDGFX from the
;     futurecommands queue (or CMDDUMMY) and end with an exact CMDWAITSWITCH; lines the beam has passed whose
;     picture changed are added to the queue (gsgentime);
;  3. after the paper: CMDTURBO, the rest of the queue, CMDENDFRAME.
; Time t is counted in ZX t-states (224 per line).
; =====================================================================================================================
;кадр (en: frame)
GSEFFECT
        LD HL,newgfx
        LD DE,gfx
        LD BC,#200
        LDIR
        LD HL,newatr
        LD DE,atr
        LD BC,#200
        LDIR

        LD IX,newgfx
gscommands=$+1
        LD DE,GScmd1
        LD A,D
        XOR high GScmd1^high GScmd2
        LD (gscommands+1),A

        CALL gsdrawbars
        CALL gsmovebars

;выводим все атрибуты atr[line] (не newatr!!!) в начале кадра: (en: output all attributes atr[line] (not newatr!!!) at the start of the frame)
        EXX
        LD IXL,0                ;line
        LD A,IXH
        ADD A,6
        LD IXH,A                ;atr+line
;de=curatr (в начале экрана не имеет значения) (en: de=curatr (does not matter at the start of the screen))
        LD HL,224*80-timedefect ;t = the top-border budget
gsgenatr0
;если atr[line]!=curatr или line&7=0: (en: if atr[line]!=curatr or line&7=0)
        LD C,(IX)
        INC IXH
        LD B,(IX)               ;atr[line]
        DEC IXH
        EX DE,HL
;or a
        SBC HL,BC
        LD H,B
        LD L,C                  ;curatr=atr[line]
        EX DE,HL
        JR NZ,gsgenatrcmd
        LD A,IXL
        AND 7
        JR NZ,genatrncmd
gsgenatrcmd                     ;
;ложим команду CMDBLACK (если atr[line]&#7fff==0) или CMDGFX (en: put command CMDBLACK (if atr[line]&#7fff==0) or CMDGFX)
        LD A,D                  ;'atr[line]
        ADD A,A
        LD A,CMDBLACK
        LD BC,-CMDBLACKTIME
        JR Z,genatrblack
        LD A,CMDGFX
        LD BC,-CMDGFXTIME
genatrblack                     ;
        PUSH DE                 ;atr[line]
        ADD HL,BC               ;t-=tcmd ;нельзя портить Z (en: t-=tcmd ; must not spoil Z)
        EXX
        POP HL
        LD (DE),A
        INC DE
        EX AF,AF'               ;Z
; address byte 1: low byte of the ZX row end (+32), carry -> next third
        LD C,IXL
        LD B,high gstdhlZX      ;концы строк (en: line ends)
        LD A,(BC)
        ADD A,32
        LD (DE),A
        INC DE
        INC B
        LD A,(BC)
; address byte 2: high byte of the attribute row (#58-#5A), +#80 = screen 7 (#D8-#DA)
        JR NC,$+4
        ADD A,8
        RRA
        RRA
        RRA
        AND 3
        ADD A,#58
        BIT 7,H
        JR Z,$+6
        OR #80
        RES 7,H
        LD (DE),A
        INC DE
        EX AF,AF'               ;Z
        JR Z,genatrngfx         ;CMDBLACK
        LD BC,32
        LDIR                    ;data->commands
genatrngfx                      ;
        EXX
genatrncmd                      ;
;line++ до #c0: (en: line++ up to #c0)
        INC IXL
        LD A,IXL
        XOR #C0                 ;CY=0
        JR NZ,gsgenatr0
        LD A,IXH
        SUB 6
        LD IXH,A
        EXX

;ожидаем остаток времени (t) (en: wait for the rest of the time (t))
;пока t>=#200, кидаем CMDDUMMY, потом CMDWAITSWITCH(scr0) (en: while t>=#200, put CMDDUMMY, then CMDWAITSWITCH(scr0))
;de'=t
;de=commands
        EX DE,HL
        EXX
gsgenwait0                      ;
        LD A,H
        CP 2
        JR C,gsgenwaitq
        LD BC,-224
        ADD HL,BC
        EXX
        LD (HL),CMDDUMMY
        INC HL
        EXX
        JR gsgenwait0
gsgenwaitq                      ;
        RRA                     ;srl h - t/4 NOPs to wait: JR offset 127-t/4
        RR L
        RRA                     ;srl h
        RR L
        LD A,127
        SUB L
        LD HL,0                 ;counter=0 (для цикла мультиколора) (en: counter=0 (for the multicolour loop))
        EXX
        LD (HL),CMDWAITSWITCH
        INC HL
        LD (HL),A               ;значение для jr (en: value for jr)
        INC HL
        LD (HL),#17             ;значение для out (en: value for out) - #17: screen 0 shown, page 7 at #C000
        INC HL
        EX DE,HL

;начало цикла мультиколора: (en: start of the multicolour loop)
        LD IXL,0                ;line=0
        LD IYH,0                ;curscr=0
        LD HL,futurecommands
        LD (HL),0               ;конец futurecommands (en: end of futurecommands)
        PUSH HL                 ;хвост futurecommands (en: futurecommands tail) - read pointer, kept on the stack

;цикл мультиколора (en: multicolour loop)
;сейчас мы в начале 0-й строки графического экрана (en: now we are at the start of line 0 of the graphics screen)
;реально (для запаса времени) в конце предыдущей: (en: really (for a time margin) at the end of the previous one)
gsgenfindswitch
;ix=newgfx+line
;hl'=counter
;de'=tswitch
;hl=futurecommands
;de=commands
        EXX
        EX DE,HL
        LD HL,0                 ;tswitch=0
        PUSH IX
        LD A,IXH
        ADD A,4+2+1
        LD IXH,A                ;'atr
        LD BC,224
; tswitch = time to the next line whose screen bit (bit 7 of the atr high byte) differs from curscr
gsgenfindswitch0
        INC IXL                 ;i++
        ADD HL,BC               ;tswitch+=224
        LD A,(IX)
        XOR IYH
        JP P,gsgenfindswitch0   ;until (atr[i]&#8000)!=curscr
        POP IX
        EX DE,HL
        EXX

gsgenifwehavetime
;есть время до переключения? (en: is there time before the switch?)
;tswitch-counter>=428+15(+32+19+15 (добавка для CMDWAITSWITCH)) (en: plus the CMDWAITSWITCH overhead)
;ix=newgfx+line
;hl'=counter
;de'=tswitch
;hl=futurecommands
;de=commands
        LD A,CMDWAITSWITCH
        LD (DE),A
        EXX
        EX DE,HL
        PUSH HL                 ;tswitch
        OR A
        SBC HL,DE               ;tswitch-counter (min=32+19+15)
        JP C,$  ; hang: the switch is already late (should not happen)
        LD BC,32+19+15          ;эта константа используется ниже (en: this constant is used below)
        OR A
        SBC HL,BC               ;hl=tswitch-counter-(32+19+15)
        JP C,$
        LD A,H
        CP 2
        JR C,gsgenwaitswitch    ;времени нет (en: no time)
        POP HL
        EX DE,HL
        EXX
;ложим команду из очереди будущих команд: (en: put a command from the future-commands queue)
;hl=futurecommands
        EX (SP),HL
;hl=хвост futurecommands (en: futurecommands tail)
;de=commands
        LD A,(HL)
        OR A
        EXX
        JR Z,gsgendummy
        LD BC,CMDGFXTIME
        EXX
        LD BC,CMDGFXLEN
        LDIR
        JR gsgenndummy
gsgendummy
        LD BC,CMDDUMMYTIME
        EXX
        LD A,CMDDUMMY
        LD (DE),A
        INC DE
gsgenndummy
;hl=хвост futurecommands (en: futurecommands tail)
        EX (SP),HL
;hl=futurecommands
        EXX
        CALL gsgentime
        EXX
        JP gsgenifwehavetime

gsgenwaitswitch
;ложим команду "ждать до переключения и переключить" (en: put the command "wait until the switch and switch")
;hl'=futurecommands (not used)
;de'=commands
;hl=tswitch-counter-(32+19+15)=0..511
;de=counter
        SRL H
        RR L
        SRL H
        RR L
        INC L                   ;иначе можем получить остаток времени 1..3 такта (en: else we may get 1..3 t-states of remainder)
        LD A,127
        SUB L
        EXX
        INC DE                  ;там было CMDWAITSWITCH
        LD (DE),A               ;значение для jr (en: value for jr)
        INC DE
        LD A,IYH                ;0/#80 = 'curscr
        XOR #80
        LD IYH,A                ;curscr = curscr xor #8000
        RLCA
        RLCA
        RLCA
        RLCA
        OR #17
        LD (DE),A               ;значение для out (en: value for out) - #17 or #1F (screen 7)
        INC DE
        EXX
;вычисляем реальное время команды с такими параметрами: (en: compute the real time of the command with these parameters)
        ADD HL,HL
        ADD HL,HL
;ld bc,32+19+15
        ADD HL,BC
        LD B,H
        LD C,L                  ;tcmd
        POP HL                  ;tswitch
        EX DE,HL
        CALL gsgentime
        EXX
        LD A,IXL
        CP #C0
        JP C,gsgenfindswitch

;догенерируем очередь будущих команд: (en: generate the rest of the future-commands queue)
;hl=futurecommands
        POP HL
;hl=хвост futurecommands (en: futurecommands tail)
;de=commands
        LD A,CMDTURBO  ; the rest runs in turbo: no more timing
        LD (DE),A
        INC DE
gsdogen0
        LD A,(HL)
        OR A
        JR Z,gsdogenq
;cp CMDBLACK ;иначе CMDGFX
;ld bc,CMDBLACKLEN
;jz gsdogenblack
        LD BC,CMDGFXLEN
;gsdogenblack
        LDIR
        JR gsdogen0
gsdogenq
;команда "закончить кадр": (en: the "end frame" command)
        LD A,CMDENDFRAME
        LD (DE),A
        RET                     ;конец генератора кадра (en: end of the frame generator)

; gsgentime: advance the beam counter by the command time BC, then line by line (224 t each) while time is
; left: for every line passed whose picture line changed since the last frame (newgfx vs gfx), append a CMDGFX
; for it to futurecommands (written to that line of the screen given by bit 7). Out: HL = counter.
gsgentime
;ix=newgfx+line
;hl=counter
;de=tswitch
;hl'=futurecommands
;de'=commands (not used)
        ADD HL,BC               ;CY=0 (и в другом переходе на gsentime0 тоже) (en: CY=0 (also on the other jump to gsentime0))
gsgentime0                      ;
        LD BC,224
;or a
        SBC HL,BC               ;counter
        JR C,gsgennotime
        EX DE,HL
;or a
        SBC HL,BC               ;tswitch
        EX DE,HL
        LD A,IXL                ;line
        CP #C0
        JR NC,gsgentime0
        EXX
        PUSH DE                 ;commands (not used)
        LD E,(IX)               ;.newgfx[line] - low byte
        INC IXH
        LD D,(IX)               ;'newgfx[line] - high byte
        INC IXH
        LD A,(IX)               ;.gfx[line]
        INC IXH
        CP E
        LD A,D
        JR NZ,gsgentimegfxnew
        CP (IX)                 ;'gfx[line]
        JR Z,gsgentimegfxok
gsgentimegfxnew                 ;
;LD A,D
        ADD A,A
        JR Z,gsgentimegfxok     ;пустая графика => пустые атрибуты (en: empty graphics => empty attributes)
;они тогда будут уже отрисованы (en: they will then be already drawn)
        LD (HL),CMDGFX
        INC HL
        LD C,IXL
        LD B,high gstdhlZX      ;концы строк (en: line ends)
        LD A,(BC)
        ADD A,32
        LD (HL),A
        INC HL
        INC B
        LD A,(BC)
        JR NC,$+3
        INC A
        BIT 7,D
        JR Z,$+6
        OR #80
        RES 7,D                 ;источник тот же (en: the source is the same)
        LD (HL),A
        INC HL
        EX DE,HL
        LD BC,32
        LDIR                    ;data->futurecommands
        EX DE,HL
        LD (HL),B               ;0 ;конец futurecommands
gsgentimegfxok                  ;
        POP DE                  ;commands (not used)
        LD A,IXH
        SUB 3
        LD IXH,A                ;CY=0
        EXX
        INC IXL                 ;line++
        JR gsgentime0
gsgennotime                     ;
        ADD HL,BC               ;counter как был (en: counter as it was)
        RET

; ---------------------------------------------------------------------------------------------------------------------
; gsdrawbars: rebuild newgfx/newatr from gsbars, then set the screen bit of each line. In: IX = newgfx.
gsdrawbars
;забиваем все адреса графики(newgfx) и атрибутов(newatr) нулями (en: fill all graphics (newgfx) and attribute (newatr) addresses with zeros)
;de=commands
;ix=newgfx
        EXX
        PUSH IX
        POP HL
        LD L,0
        LD D,H
        LD E,1
        LD BC,#1FF
        LD (HL),L               ;0
        LDIR                    ;newgfx
        INC HL
        INC DE
        INC H
        INC H
        INC D
        INC D
        LD BC,#1FF
        LD (HL),L               ;0
        LDIR                    ;newatr
;exx

;"рисуем" адреса всех баров - графики и атрибутов, под scr0 (en: "draw" the addresses of all bars - graphics and attributes, for scr0)
;выскакивание за экран (#c0) можно не проверять (en: going off the screen (#c0) need not be checked)
;exx
        LD HL,gsbars
gsdrawbars0                     ;
        INC L                   ;пропускаем dy (en: skip dy)
        LD A,(HL)               ;Y бара (en: bar Y)
        LD IXL,A
        INC L
        LD E,(HL)               ;Y графики бара (en: bar picture Y)
        LD D,high gstdhlGS
        LD B,16
gsdrawbar0                      ;
        PUSH DE
        LD A,(DE)
        LD C,A
        INC D
        LD A,(DE)
        LD D,A
        RRA
        RRA
        RRA
        AND 3
        ADD A,high gsatr
        LD E,A
        LD A,IXH
        LD (IX),C
        INC IXH
        LD (IX),D               ;newgfx
        LD D,A                  ;old hx='newgfx
        ADD A,4
        LD IXH,A
        LD (IX),C
        INC IXH
        LD (IX),E               ;newatr
        LD IXH,D
        POP DE
        INC E                   ;Y графики бара (en: bar picture Y)
        INC IXL
        DJNZ gsdrawbar0
        INC L
        JR NZ,gsdrawbars0
;newatr[#c0] = #8000 (чтобы не повиснуть на первом кадре) (en: newatr[#c0] = #8000 (so as not to hang on the first frame))
;LD LX,#C0
;LD A,HX
;LD C,A
;ADD A,4+1
;LD HX,A
;LD (IX),#80 ;'newatr[#c0]
;LD HX,C
;проходим атрибуты и графику сверху вниз (en: walk attributes and graphics top to bottom)
;расставляем бит номера экрана (en: set the screen number bit)
;de=commands
;ix=newgfx
        LD BC,#80               ;curscr=0 (en: curscr=0)
        LD IXL,B                ;0 ;line=0
        LD A,IXH
        LD D,A
        ADD A,4
        LD IXH,A
        LD L,(IX)
        INC IXH
        LD H,(IX)               ;curatr=newatr[0]
        LD IXH,D
gsgensetscreens0                ;
        LD A,IXH
        ADD A,4
        LD IXH,A
;если curatr!=newatr[line], то curscr=curscr xor #8000 (en: if curatr!=newatr[line], then curscr=curscr xor #8000)
;(источник графики тот же, (en: (the graphics source is the same,)
;этот бит при копировании будет игнорироваться) (en: this bit is ignored when copying))
        LD E,(IX)
        INC IXH
        LD D,(IX)               ;newatr[line]
;or a
        SBC HL,DE
        JR Z,gsgensetscreensswapq
        LD A,IXL
        AND 7
        JR Z,gsgensetscreensswapq ;не надо перекл.каждое знакоместо (en: no need to switch at every character cell) - only inside a cell
        LD A,B
        XOR C                   ;#80
        LD B,A                  ;'(curscr xor #8000)
gsgensetscreensswapq            ;
        EX DE,HL                ;curatr=newatr[line]
        LD A,H                  ;newatr[line]
        OR B
        LD (IX),A               ;newatr[line] = newatr[line] or curscr (en: newatr[line] = newatr[line] or curscr)
        LD A,IXH
        SUB 4
        LD IXH,A
        LD A,(IX)
        OR B
        LD (IX),A               ;newgfx[line] = newgfx[line] or curscr (en: newgfx[line] = newgfx[line] or curscr)
        DEC IXH
;line++ до #c0: (en: line++ up to #c0)
        INC IXL
        LD A,IXL
        CP #C0
        JR NZ,gsgensetscreens0
;newatr[#c0] = newatr[#bf] xor #8000 (можно curscr xor #8000): (en: newatr[#c0] = newatr[#bf] xor #8000 (could be curscr xor #8000)) - a sentinel so gsgenfindswitch stops at #c0
        LD A,B
        XOR C                   ;#80
        LD B,A                  ;'(curscr xor #8000)
        LD A,IXH
        LD C,A
        ADD A,4+1
        LD IXH,A
        LD (IX),B               ;'newatr[#c0]
        LD IXH,C
        EXX
        RET

; ---------------------------------------------------------------------------------------------------------------------
; gsmovebars: fall with acceleration. A moving bar (dy != 0): dy++, y += dy, stop at #C0 (below the screen).
; A standing bar starts (dy = 1) when the bar below it has reached dy = 5 - a cascade from the bottom bar up.
gsmovebars
;двигаем бары, останавливая на #c0 (en: move the bars, stopping at #c0)
        EXX
        LD HL,gsbars
gsmovebars0                     ;
;если dy!=0, то: (en: if dy!=0, then:)
;y+=dy, dy++, если y>=#c0, то: (en: y+=dy, dy++, if y>=#c0, then:)
;y=#C0 (т.к. остановленные перезапускаются), dy=0 (en: y=#C0 (since stopped ones are restarted), dy=0)
        LD A,(HL)               ;dy
        OR A
        JR Z,gsmovebarsnmove
        INC (HL)                ;dy++
        INC L
        ADD A,(HL)
        LD (HL),A               ;y+=dy
        CP #C0
        JR C,gsmovebarsmoveq
        LD (HL),#C0
        DEC L
        LD (HL),0
        JR gsmovebarsmoveiq
gsmovebarsnmove                 ;
;иначе (dy=0): если dy(n+1)=4, то dy=1 (en: else (dy=0): if dy(n+1)=4, then dy=1) - the code compares with 5
        INC L
        INC L
        INC L
        LD A,(HL)               ;можем читать вне таблицы, но не пишем (en: may read outside the table, but does not write)
        DEC L
        DEC L
        DEC L
        CP 5                    ;4
        JR NZ,$+3
        INC (HL)                ;если dy(n+1)=4, то dy=1 (en: if dy(n+1)=4, then dy=1)
gsmovebarsmoveiq                ;
        INC L
gsmovebarsmoveq                 ;
        INC L
        INC L
        JR NZ,gsmovebars0
        EXX
        RET

; ---------------------------------------------------------------------------------------------------------------------
; GENDHL: for the 192 screen lines fill gstdhlGS (line address in the card's gsscr) and gstdhlZX (ZX address
; #4000-#57FF). GSDHL = the usual "next pixel line" step of the Spectrum screen layout.
GENDHL
        LD DE,#4000
        LD L,E                  ;0
        LD B,192
GENDHL0 LD H,high gstdhlGS
        LD (HL),E
        INC H
        LD A,D
        ADD A,high gsscr-#40
        LD (HL),A
        LD H,high gstdhlZX
;LD A,E
;ADD A,32
        LD (HL),E
        INC H
;LD A,0
;ADC A,D
        LD (HL),D
        INC L
        EX DE,HL
        CALL GSDHL
        EX DE,HL
        DJNZ GENDHL0
        RET
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

; GSRESET: ZX silent too long - DMA off, back to 10 MHz with the ROM and restart the card firmware (RST 0).
GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

        DISPLAY $
        ENT
LENGSMULBARCODE=$-WASGSMULBARCODE
        DISPLAY /T,LENGSMULBARCODE
; the picture the bars are cut from (a 6912-byte ZX screen), copied to gsscr
wasscr
        INCBIN "gameover"
LENMULBARGS=$-#C000

; =====================================================================================================================
; MULBARZX - ZX half, ORG #C000 in its page. Called by the resident loop with BC = start frame, DE = end frame.
; Copies the code below to #7800 (MULBARCODE) and runs it there: the handlers must stay mapped while page 7 (the
; second screen) is at #C000.
; =====================================================================================================================
        ORG #C000,pgeffects
pgmulbarzx=pgeffects
        PUSH BC
        PUSH DE
        LD HL,WASMULBARCODE
        LD DE,MULBARCODE
        LD BC,LENMULBARCODE
        LDIR
        JP MULBARCODE

WASMULBARCODE
        DISP MULBARCODE
        POP HL
        LD (zxendtime),HL  ; end frame -> self-modified zxendtime
        HALT

        POP BC
ZXWAITSTART
        LD HL,(timer)
        OR A
        SBC HL,BC
        JR C,ZXWAITSTART  ; wait for the start frame

        LD A,#17
        setpga  ; page 7 (screen 1) at #C000 from now on
        HALT
;LD A,#10 ;noturbo
;LD BC,#EFF7
;OUT (C),A
        LD A,#C9
        LD (#5F5F),A  ; RET at #5F5F: the target of the IM 2 vector set every frame below
;LD HL,IMVEC
;LD BC,256
;LD D,H,E,B
;LD (HL),#5F
;LDIR ;так мы задержим музыку на 2 кадра (en: this way we would delay the music by 2 frames)

        LD HL,#4000
        LD DE,#C000
        LD BC,#1B00
        LDIR  ; both screens start with the same picture

; Per-frame loop: commands 0 and 1 to the card, then run the card's command stream from #0000 by ZX-DMA.
BIGLOOP
;HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
;даем знать GS'у, что кадр готов (en: let GS know the frame is ready)
;и DMA надо выключить: (en: and the DMA must be turned off)
        XOR A
        OUT (GSCOM),A

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
        JR NC,QUIT              ;можно жать RESET (en: one may press RESET) - Space ends the effect early
;даем знать GS'у, что надо включить DMA (en: let GS know the DMA must be turned on)
        LD A,1
        OUT (GSCOM),A
;и ждем включения: (en: and wait until it is on)
        WN
        GD
;
        LD BC,#EFF7
        LD A,#10
        OUT (C),A               ;noturbo (en: noturbo) - the card's timing assumes 3.5 MHz
; vector (IMVEC+#FF) -> #5F5F (RET): an interrupt during the stream does nothing; music is called at ENDFRAME.
; SP is saved (self-modified PUSHKASP) because the handlers use it for PUSH blits. HL = 0: DMA reads from now on.
        LD HL,#5F5F
        LD BC,#7FFD
        HALT
        LD (IMVEC+#FF),HL
        LD (PUSHKASP),SP
        LD HL,0
        LD A,#17
        OUT (C),A
        LD A,(HL)               ;1ST BYTE DISCARDED (en: 1st byte discarded)
        LD H,(HL)
        JP (HL)  ; first command; each handler ends with LD H,(HL) : JP (HL)
; CMDENDFRAME jumps here
ENDFRAME
PUSHKASP=$+1
        LD SP,0
        IF border
        LD A,R
        OUT (254),A
        ENDIF
        CALL IMER               ;делает EI (en: does EI) - frame counter + music
        JP BIGLOOP
QUIT
;мы в конце кадра, звук только что отыграли (en: we are at the end of the frame, the sound has just played)
        LD A,2
        OUT (GSCOM),A           ;конец эффекта (en: end of the effect) - command 2: the card leaves its loop
        HALT                    ;без звука (en: without sound)
        LD HL,IMVEC
        LD BC,256
        LD D,H
        LD E,B
        LD (HL),low IMER                ; ALASM keeps the low byte; restore the IM 2 table (all #BF)
        LDIR
        EI
        LD A,#10
        setpga
        LD HL,#5800
        LD DE,#5801
        LD BC,767
        LD (HL),L
        LDIR  ; black attributes, screen 0 shown
        LD BC,#EFF7
        XOR A
        OUT (C),A  ; #EFF7 = 0 (turbo), back to the resident loop
        RET
;LD A,C_GRST
;OUT (GSCTR),A
;JR $

; ---------------------------------------------------------------------------------------------------------------------
; Command handlers, each on its own 256-byte page; the command byte = the page's high byte. On entry H = the
; handler page, L = 0; LD H,L makes HL = #0000 again, so each (HL) read takes the next stream byte from ZX-DMA.
; Each ends with LD H,(HL) : JP (HL) = fetch and dispatch the next command.
; CMDBLACK [lo, hi]: SP = row end, 16 x PUSH HL (= 0): clear 32 bytes.
        DS low (-$)
CMDBLACK=high $
        LD H,L
        LD E,(HL)
        LD D,(HL)
        EX DE,HL
        LD SP,HL
        EX DE,HL
        DUP 16
        PUSH HL                 ;0
        EDUP
        LD H,(HL)
        JP (HL)

; CMDGFX [lo, hi, 32 bytes]: SP = row end, 16 x (LD D,(HL) : LD E,(HL) : PUSH DE) - row written last byte first.
        DS low (-$)
CMDGFX=high $
        LD H,L
        LD E,(HL)
        LD D,(HL)
        EX DE,HL
        LD SP,HL
        EX DE,HL
        DUP 16
        LD D,(HL)
        LD E,(HL)
        PUSH DE
        EDUP
        LD H,(HL)
        JP (HL)

; CMDDUMMY: waste one scan line (224 t). HL stays 0.
        DS low (-$)
CMDDUMMY=high $
        LD H,L
        DUP 19
        ADD HL,HL               ;0
        EDUP
        LD H,(HL)
        JP (HL)                 ;224

; CMDWAITSWITCH [jr, out]: exact delay - the JR offset (self-modified) skips into 127 NOPs - then OUT (#7FFD)
; with #17 / #1F: switch the displayed screen (5 / 7) on the right line.
        DS low (-$)
CMDWAITSWITCH=high $
        LD H,L
        LD A,(HL)
        LD ($+4),A
        JR $
        DS 127
        LD A,(HL)
        OUT (C),A
        LD H,(HL)
        JP (HL)

; CMDENDFRAME: end of the stream.
        DS low (-$)
CMDENDFRAME=high $
        JP ENDFRAME

; CMDTURBO: OUT (#EFF7),0 = turbo on for the untimed rest of the stream; BC = #7FFD again for CMDWAITSWITCH.
        DS low (-$)
CMDTURBO=high $
        LD H,L
        LD BC,#EFF7
        OUT (C),H
        LD BC,#7FFD
        LD H,(HL)
        JP (HL)
        ENT
LENMULBARCODE=$-WASMULBARCODE
LENMULBARZX=$-#C000

; test bench: the next effect page only stops here
        ORG #C000,pgeffects+1
        JR $

; test bench: the music page (the demo has it at #54 too)
        ORG #C000,pgmusic
        INCBIN "thelinkm"

; ---------------------------------------------------------------------------------------------------------------------
; ObjTab for the author's object saver SAVEOBJ4 (name+type, address, length, #7FFD page, start): the two objects.
        ORG end
ObjTab
        DB 'MULBARZXC'
        DW #C000
        DW LENMULBARZX
        DB pgmulbarzx
        DW #C000

        DB 'MULBARGSC'
        DW #C000
        DW LENMULBARGS
        DB pgmulbargs
        DW #C000
        INCLUDE "SAVEOBJ4.asm"
; Not saved, only for the author's session after the SAVEOBJ4 include: put the picture "gameover" on the screen
; and go to the saver's entry nenado (which, per SAVEOBJ4, goes to GO without CAPS SHIFT, else saves ObjTab).
; Nothing in this file jumps here. In ALASM "ORG $,0" only names page 0 for #C000; the address stays $.
        ORG $,0                         ; orgpage-ok: ALASM maps page 0 at #C000 whatever the address
        LD HL,zxwasscr
        LD DE,#4000
        LD BC,#1B00
        LDIR
        JP nenado
zxwasscr
        INCBIN "gameover"

; ---------------------------------------------------------------------------------------------------------------------
; sjasmplus build (added): the objects of ObjTab. In ALASM the author RAN the assembled source and SAVEOBJ4
; wrote them to disk; sjasmplus writes the same bytes while it assembles.
        SLOT 3
        PAGE pgmulbarzx
        SAVEBIN "MULBARZX",#C000,LENMULBARZX
        PAGE pgmulbargs
        SAVEBIN "MULBARGS",#C000,LENMULBARGS
