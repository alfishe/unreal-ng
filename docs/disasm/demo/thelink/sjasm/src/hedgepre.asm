; =====================================================================================================================
; hedgepre.asm - The Link (Alone Coder, 2009): an earlier, stand-alone take of the hedgehog. NOT USED BY THE BUILD.
;
; It INCBINs demon16.p (the Hrust-packed 16-colour background), a file that is not on the author's disk, so it cannot
; be assembled as is; no object of the demo comes from it. The demo's hedgehog is hedge12.asm, and the card's 3D part
; here (texture copies, rotation, sort, TRI, transch, the KUB model, texpp4/vujump/instlpp) is the same code; see the
; comments there. What differs, as far as the source shows:
;   - ZX side: GO (#7800) only clears RAM 4-7 and unpacks demon16.p into RAM 6/7 (DEPKS16, the Hrust 2.1 depacker
;     DEHR21 inline; m2hrust.asm included at the end), then RETs. BIGLOOP, MKOUT16C, SCR1TOSCR0 and the card loaders
;     GSLOAD (#14), GSLOAD2 (#18/#19), GSJUMP (#13), GSSENDDATA are here but nothing in the file calls them. OUT16C is
;     right before the card program image WASGSPROG: the generated blit overwrites it, so the program had to be sent
;     first. BIGLOOP waits with HALT, blits under DI (no interrupt trick, no music, no timer), and QUIT resets the card
;     (C_GRST) and stops with JR $. MKOUT16C has no LD (curpg),A in a row, stores the LD (..),DE only on the lines
;     with E = #00/#80 (the "22 lines" of the author's comment), and places its two extra reads differently.
;   - Card side: one program (GSPROG at card #5B30, effect code included) sent as a whole instead of a code page run
;     by the card resident; it receives #300 bytes from the ZX through ZXDATRD into card #3D00, then a texture page by
;     DMA into card page gspgballtex1 (#20); other card page numbers (screens 4/5, tables #30-#33, textures #34);
;     "ball" constants marked temporary (mkballplasma, gspgball*, CALL MKBALLER commented out); the angles advance
;     one step per picture (no gstimer); the card never quits (no command 2) and keeps the screen page on the stack
;     (PUSH AF / POP AF) until command 1.
;   - Memory: curpg #5E5D; IMVEC #5B00 and IMER #5E5E are defined but not used; kishki.C at #7FFD page #95.
;   - Tail: ORG #5CDD,0 / DB 'GSHEDGE B' (a TR-DOS file name in the system variables area) and INCLUDE m2hrust.
; Converter additions as in hedge12.asm: the DEVICE line, expanded macros (MTEXTURER, PATCHVUCODE, DOTV0_1, INSFR,
; KPOLY, KPOL), `L_m` for `m`, `low x` with "ALASM keeps the low byte", the depacker's LOCAL labels renamed `*__L1`.
; There is no SAVEBIN block: the file defines no objects.
; =====================================================================================================================
        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device
pgdata1=#90
pgdata2=#91
pgdata3=#92
pgdata4=#94
pgdata5=#95
mkballplasma=0

curpg=#5E5D
IMVEC=#5B00
IMER=#5E5E

parthgt=28                      ;высота экранной четверти

DEPKS_picarea=#A000
DEPKS_linebuf=#9F00
DEPKS_pgtrash=#50               ;#58

lambert=32-16
lambertshr=3
lambertmax=7

border=0
texdec=1                        ;с заворотом
gschmap256=#0000
gschmaphgt=64                   ;с заворотом в 2 раза больше
gschscr=#8000                   ;2 страницы - 2 экрана
TPOLYZ=#4000
TPOLYS=TPOLYZ+#100              ;#200 (L,H)
;исходные координаты:
VTX3DI=#4300                    ;,#300
VTX3DIH=high VTX3DI
;координаты после поворота:
VTX3D=#4600                     ;,#300
VTX3DH=high VTX3D
VTX3DY=VTX3D+#100
VTX3DZ=VTX3D+#200

gspgtball=4                     ;временно (en: temporary)
GSTQUARTERBALL=#4000            ;временно (en: temporary)
GSTBALL=#C000                   ;2*64*128=16K
gspgballer1=4+#80
gspgballer2=5+#80
gspgballtex1=#20                ;8 страниц

;X 3D-движка растет на экране вверх
;Y 3D-движка растет на экране вправо
gspgchscr0=4
gspgchscr1=5
gspgdiv=#30                     ;(L/2)/-H ;+-7.8=+-6.0/6.0
gspgpersp=#33                   ;+-7.0=+-7.0/6.0
gspgsin=#31
SIGNOFS=#C000
SINOFS=#C100
COSOFS=#C200
PROCROT=#C300
MULTAB=#C400
gspgtex=#34                     ;8 страниц
gspgtransch=#32
transch=#E000
TCH256=#FE00

debug=0                         ;1=проверка отрицательной ширины
mirrorchunks=1                  ;строки 0,1,3,2,4,5,7,6...
perspdist=16                    ;32
trisortmesspop=1                ;1=сокращает код без потери скорости
incW=0                          ;1=исключает выскакивание за текстуру, но дергается
texborder=1                     ;размер запаса на краю текстуры, против выскакивания
texarea=63-texborder-texborder
minV=#C0+texborder
maxV=minV+texarea

trimaxh=63
trimaxl=52                      ;ограничение TEXTURER'а ;ограничение расчётов=63
ntexjp=20
ntexjr=16
ntexfast=16

        INCLUDE "gsports.asm"
GSPROG=#5B30
GSSTACK=#5B30

        MACRO GSSETPGA
        OUT (MPAGEX),A
        ENDM

        MACRO setpga
        LD (curpg),A
        LD BC,#7FFD
        OUT (C),A
        ENDM

        MACRO setpg _arg0
        LD A,_arg0
        setpga
        ENDM

; en: GO: clear RAM 4-7 (the two 16-colour screens), unpack the background into RAM 6/7, RET
        ORG #7800
begin
GO
        LD A,#14
        CALL CL1800PG
        LD A,#15
        CALL CL1800PG
        LD A,#16
        CALL CL1800PG
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),L
        LDIR
        LD A,#17
        CALL CL1800PG
        LD HL,#C000
        LD DE,#C001
        LD BC,#3FFF
        LD (HL),L
        LDIR

        DI
        LD HL,BLOCK_PIC1
        LD BC,SIZE_PIC1
        CALL DEPKS16
        RET

; ---------------------------------------------------------------------------------------------------------------------
; en: BIGLOOP: the ZX frame as in hedge12.asm, simpler (HALT, DI around the blit); not called from this file
; ---------------------------------------------------------------------------------------------------------------------
BIGLOOP
        HALT
        IF border
        XOR A
        OUT (254),A
        ENDIF
;даем знать GS'у, что кадр готов
;и DMA надо выключить:
        LD A,0
        OUT (GSCOM),A

        LD A,#17^#1D
        XOR #1D                 ;видим scr1, рисуем scr0
        LD ($-1),A              ;теперь видим scr0, рисуем scr1
        LD (VIEWPG1),A
        DEC A
        LD (VIEWPG0),A
        LD BC,#7FFD
        OUT (C),A

;и ждем выключения:
        WN
        GD
;
        LD A,#7F
        IN A,(#FE)
        RRA                     ;space
        JP NC,QUIT              ;можно жать RESET
;даем знать GS'у, что надо включить DMA
        LD A,1
        OUT (GSCOM),A
;и ждем включения:
        WN
        GD
;
        DI
        LD (BIGLOOPSP),SP
        LD IX,BIGLOOPVIEWQ
VIEWPG1=$+1
VIEWPG0=$+2
        LD HL,#1617
        LD A,(HL)               ;первый байт неверен (en: the first byte is wrong)
        JP OUT16C
BIGLOOPVIEWQ
BIGLOOPSP=$+1
        LD SP,0
        EI
        IF border
        LD A,7
        OUT (254),A
        ENDIF
        JP BIGLOOP
; en: QUIT: reset the card and stop
QUIT
        LD A,C_GRST
        OUT (GSCTR),A
        JR $

;копируем из NGS линейный буфер 16C на экран
;снизу вверх
;обработчик прерывания должен класть в стек DE вместо адреса
;возврата
; en: MKOUT16C: the blit generator, an earlier form of hedge12.asm's (see there)
MKOUT16C
;13K кода для 256X128 (en: 13K of code for 256x128)
;LD A,H
;LOOP  ;OUT (C),A
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
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD DE,#D7E0
        LD B,parthgt            ;32 ;высота 32*4;при 30 эмуль тормозит редко
MKOUT16CLINE
        PUSH BC
        LD (HL),#7C             ;ld a,h
        INC HL
        LD (mkout16cloop),HL
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

MKOUT16C0_2
        CALL MKOUT16C0
;PUSH DE
; LD A,E
; SUB #80
; LD E,A
; CALL MKOUT16C0
;POP DE
; RET

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
        LD B,7                  ;8
MKOUT16C1
        INC HL
        LD (HL),#56             ;d,(hl)
        INC HL
        LD (HL),#5E             ;e,(hl)
        INC HL
        LD (HL),#D5             ;push de
        DJNZ MKOUT16C1
;для 8 линий в #C080..#C780, 7 линий #C900..#CF00,
;7 линий #D100..#D700 надо LD (..),DE вместо последнего PUSH
        LD A,E
        ADD A,A
        JR NZ,MKOUT16CnPROTECT
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
        LD (HL),#56             ;d,(hl)
        INC HL
;RET
MKOUTUDEPART
        LD B,parthgt            ;32
        CALL UDE
        DJNZ $-3
        RET

; en: SCR1TOSCR0: RAM 7 -> 5, 6 -> 4 through DEPKS_linebuf (hedge12.asm uses the resident's RESMOVEPAGE)
SCR1TOSCR0
        LD IX,#1F1D
        CALL SCR1TOPP
        LD IX,#1E1C
SCR1TOPP
        LD HL,#C000
        CALL SCR1TOPPP
        LD HL,#E000
SCR1TOPPP
        LD IYH,#18
SCR1TOPP0
        PUSH HL
        LD DE,DEPKS_linebuf
        PUSH DE
        LD A,IXH
        setpga
        LD BC,256
        LDIR
        POP HL
        POP DE
        LD A,IXL
        setpga
        LD BC,256
        LDIR
        EX DE,HL
        DEC IYH
        JR NZ,SCR1TOPP0
        RET

; ---------------------------------------------------------------------------------------------------------------------
; en: DEPKS16: unpack the 16-colour picture: copy the packed block to DEPKS_picarea, move it to the end of memory in
; en: page DEPKS_pgtrash, unpack it (DEHR21) to #A000, then copy it out in 192-byte runs into RAM 6/7 (setpgst picks
; en: #16 or #17, DEPKS_DDE steps a line down in the screen layout, nxtst moves to the next column)
; ---------------------------------------------------------------------------------------------------------------------
DEPKS16
;HL=адрес упакованной картинки (en: HL = address of the packed picture)
;BC=ее длина (en: BC = its length)
        LD DE,DEPKS_picarea
        PUSH BC
        PUSH DE
        LDIR
        setpg DEPKS_pgtrash
        POP DE
        POP BC
        LD HL,0
        OR A
        SBC HL,BC
        EX DE,HL
        PUSH DE
        LDIR                    ;теперь упак. блок в конце памяти (en: now the packed block is at the end of memory)
        POP HL
        LD DE,DEPKS_picarea
        PUSH DE
        CALL DEHR21
        POP HL
        LD DE,#C000
        LD IXL,32*4
DEPKST0
        PUSH DE
        setpg DEPKS_pgtrash
        LD DE,DEPKS_linebuf
        LD BC,192
        LDIR
        POP DE
        PUSH DE
        PUSH HL
        LD HL,DEPKS_linebuf
        CALL setpgst
        LD B,192
DEPKST1 LD A,(HL)
        LD (DE),A
        INC HL
        CALL DEPKS_DDE
        DJNZ DEPKST1
        POP HL
        POP DE
        CALL nxtst
        DEC IXL
        JR NZ,DEPKST0
        RET

nxtst
        LD A,D
        XOR 128
        BIT 7,D
        LD D,A
        RET NZ
        LD A,D
        XOR 32
        BIT 5,D
        LD D,A
        RET Z
        INC E
        RET
setpgst
        LD A,D
        RLA
        CCF
        LD A,+(((#16&#FFFF)>>>1|(#16&#FFFF)<<(16-1))&#FFFF)
        RLA                     ;#16 или #17
        setpga
        SET 7,D
        RET

DEPKS_DDE
        INC D
        LD A,D
        AND 7
        RET NZ
        LD A,E
        ADD A,32
        LD E,A
        RET C
        LD A,D
        SUB 8
        LD D,A
        RET

; en: DEHR21: Hrust 2.1 depacker (HL = from, DE = to); ALASM LOCAL block, labels renamed *__L1 by the converter
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
;выходим. HL'=после упакованного блока (en: exit. HL' = after the packed block)
CHL__L1 LD C,(HL)
        INC HL
        RL C
        RET


; ---------------------------------------------------------------------------------------------------------------------
; en: the ZX -> card loaders with the standard GS commands (none is called in this file)
; ---------------------------------------------------------------------------------------------------------------------
;#13 Jump to Address
GSJUMP
        SD L
        SC #13
        WC
        SD H
        WD
        RET

;как в RIFF TRACKER (en: as in RIFF TRACKER)
GSLOAD2
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
        RET

;#14 Load memory block
GSLOAD
        SD C
        SC #14
        WD                      ;<PAUSE>?
        SD B
        WD
        SD E
        WD
        SD D
;<PAUSE>
        HALT
        HALT
GSLOAD0 SD (HL)
        INC HL
        DEC BC
        WD
        LD A,B
        OR C
        JR NZ,GSLOAD0
        RET

GSSENDDATA
;WN ;ждем готовность GS к приему
GSSD0   IN A,(GSDAT)
        CP E
        JR NZ,GSSD0
        LD A,E
        OUT (GSCOM),A

GSSEND0
        SD (HL)
        INC HL
        DEC BC
        WD
        LD A,B
        OR C
        JR NZ,GSSEND0
        RET

GSPREPARE
;IN A,(GSDAT)
;CP E
;JNZ $-3
        LD A,E
        OUT (GSCOM),A
        RET

GSDMASENDPG
        LD BC,#7FFD
        OUT (C),A
        LD HL,#C000
        LD DE,#0000
        LD BC,#4000
        LDIR
        RET

CL1800PG
        LD BC,#7FFD
        OUT (C),A
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
; en: OUT16C: the generated blit starts here, over the card program image below
OUT16C
        DISPLAY 'GS CODE=',$

; ---------------------------------------------------------------------------------------------------------------------
; en: the whole card program, for card #5B30 (GSPROG): texturers, GSPROGGO set-up, GSLOOP, the model, texpp4, vujump,
; en: instlpp, palbrick
; ---------------------------------------------------------------------------------------------------------------------
WASGSPROG
        DISP GSPROG
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
        DS low (-$)
INCSYSTEM
; macro MTEXTURER expanded
        DS low (253-((ntexjp+ntexjr)*7)-$)
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
;это круглый адрес
;1 байт адрес для ширины 0
        DB TEXTURERINCQ
;16*2=32 байт jr для ширины ??..?? (первый имеет смещение -125)
TEXTURERINCJR
_=ntexjp*7+TEXTURERINCMAX
        DUP ntexjr
        JR _
_=_+7
        EDUP
;20*3=60 байт jp для ширины ??..??
TEXTURERINCJP
_=TEXTURERINCMAX
        DUP ntexjp
        JP _
_=_+7
        EDUP
;16*7-4=108 байт кода для ширины 16..0
TEXTURERINC
        DUP ntexfast-1
        LD HL,0
        ADD HL,BC
        LD A,(HL)
        LD (DE),A
        INC E
        EDUP
;смещение=0
        LD A,(BC)
        LD (DE),A
TEXTURERINCQ
        RET
        DS 3
;20+16+16=52 байт адресов
_=TEXTURERINCJP
        DUP ntexjp
        DB _
_=_+3
        EDUP
_=TEXTURERINCJR
        DUP ntexjr
        DB _
_=_+2
        EDUP
_=TEXTURERINC
        DUP ntexfast
        DB _
_=_+7
        EDUP
        DISPLAY $,'=#XX00'
;итого 256 байт
        DISPLAY /T,TEXTURERINC
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
        ADD HL,DE               ;пропускаем первый
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
        DB TEXTURERDECQ
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
        DB _
_=_+3
        EDUP
_=TEXTURERDECJR
        DUP ntexjr
        DB _
_=_+2
        EDUP
_=TEXTURERDEC
        DUP ntexfast
        DB _
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

;почему-то если тут LDIR, то прием картинки виснет (en: for some reason, with an LDIR here the picture transfer hangs)

        LD A,1                  ;SET DMA MODULE #1
        OUT (DMA_MOD),A

        IF 1
        IN A,(ZXDATRD)          ;не помогает
        LD E,2
        LD A,E
        OUT (ZXDATWR),A         ;готов к приему (en: ready to receive)
;GS ожидает команду от ZX (en: the card waits for a command from the ZX)
GSWAIC0 IN A,(ZXCMD)
        CP E
        JR NZ,GSWAIC0

;принимаем данные для эффекта (en: receive the data for the effect (#300 bytes to #3D00))
        LD HL,#3D00             ;#4000
        LD BC,#300              ;#1B00
GSGET0
GSWD0   IN A,(ZXSTAT)
        RLCA
        JR NC,GSWD0
        IN A,(ZXDATRD)
        LD (HL),A
        INC HL
        DEC BC
        LD A,B
        OR C
        JR NZ,GSGET0
        ENDIF
        LD E,3
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        LD A,gspgballtex1
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON"

        LD E,4
;LD A,E
;OUT (ZXDATWR),A ;готов к приему
        IN A,(ZXCMD)
        CP E
        JR NZ,$-3

        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF"
        IN A,(ZXSTAT)
        RLCA
        JR C,$-3                ;ZX готов?

        LD A,+((((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#404&#FFFF)>>>1|((((gspgballtex1*257&#FFFF)<<1|(gspgballtex1*257&#FFFF)>>>(16-1))&#FFFF)+#404&#FFFF)<<(16-1))&#FFFF)
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
;CALL MKBALLER
;CALL MKBALLTEX
;CALL GENTEX
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

; en: GSLOOP: as in hedge12.asm, but the angles step once per picture
GSLOOP
gsdrawscr=$+1
        LD A,1                  ;рисуем в SCR2
        XOR 1
        LD (gsdrawscr),A        ;теперь рисуем в SCR1
;потом дадим его ZX'у
        IF 1
        LD HL,ALPHA
        INC (HL)
        LD HL,BETA
        DEC (HL)
        LD A,201
        ADD A,0
        LD ($-1),A
        JR NC,$+3               ;некратные угловые скорости
        DEC (HL)
        LD HL,GAMMA
        LD A,125
        ADD A,0
        LD ($-1),A
        JR NC,$+3               ;некратные угловые скорости
        INC (HL)

        CALL JTNROTATE
        CALL PERSPCORR

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
        LD L,A                  ;адрес полигона
        LD E,(HL)               ;адрес вершины A
        INC L
        INC HL
        LD A,(DE)               ;ZA
        LD E,(HL)               ;адрес вершины B
        INC L
        INC HL
        LD L,(HL)               ;адрес вершины C
        LD H,D
        ADD A,(HL)              ;ZC
        EX DE,HL
        ADD A,(HL)              ;ZB
        EX DE,HL
        POP HL
        LD (HL),A               ;сумма координат Z для вершин грани
        INC L
        DJNZ COUNTZ0

        LD HL,TPOLYZ
        CALL SORTZ

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

        LD A,(gsdrawscr)
        OR A
        LD A,gspgchscr0
        JR Z,$+4
        LD A,gspgchscr1
        PUSH AF
        OUT (MPAG),A
        LD A,gspgtransch
        GSSETPGA
        LD (GSLOOPSP),SP
        LD IX,GSLOOPTRANSQ
        LD E,15                 ;31 для 256X128
        LD B,high TCH256
        EXX
        LD HL,gschmap256+32-parthgt
        LD B,16                 ;64 Y / 4 слоя
        JP transch
GSLOOPTRANSQ
GSLOOPSP=$+1
        LD SP,0
        ENDIF

;GS ожидает конец кадра у ZX (en: the card waits for the end of the ZX's frame)
        LD HL,0
        LD DE,1
GSWAIT0 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR NZ,GSWAIT0           ;48t
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A         ;после этого ZX можно сбрасывать
        OUT (ZXDATWR),A         ;признак для ZX "DMA OFF"
;GS ожидает обнуления (ZX запросит данные) (en: the card waits for the reset to 0 (the ZX will request the data))
        LD HL,0
        LD DE,1
GSWAIT1 ADD HL,DE
        JP C,GSRESET
        IN A,(ZXCMD)
        OR A
        JR Z,GSWAIT1

        POP AF
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
        OUT (ZXDATWR),A         ;признак для ZX "DMA ON"

        JP GSLOOP

PERSPCORR
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

SORTZ
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

;адрес вершины A ;Y,X,Z
;адрес вершины B
;адрес вершины C
;VU вершины A (0/4)
;VU вершины B (0/4)
;VU вершины C (0/4)
;#C300 ;для переворота текстуры надо #F200 и переставить VU
;номер текстуры в странице (0/64/128/192) (в обоих байтах слова)
;TRIDRAW
;страница текстуры (в старшем байте слова)
;(20 байт, выровнено по 2)
icosz=23*2                      ;на 24 редко зашкаливает
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

KUB
        DB 12+20                ;вершин
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
L_m=256
_=12
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
        DB ((V5X-V1X)*k&#FFFF)/(L_m&#FFFF)+V1X,((V5Y-V1Y)*k&#FFFF)/(L_m&#FFFF)+V1Y,((V5Z-V1Z)*k&#FFFF)/(L_m&#FFFF)+V1Z
; macro DOTV0_1 expanded
V0_9=_
_=_+1
        DB ((V9X-V0X)*k&#FFFF)/(L_m&#FFFF)+V0X,((V9Y-V0Y)*k&#FFFF)/(L_m&#FFFF)+V0Y,((V9Z-V0Z)*k&#FFFF)/(L_m&#FFFF)+V0Z
; macro DOTV0_1 expanded
V1_6=_
_=_+1
        DB ((V6X-V1X)*k&#FFFF)/(L_m&#FFFF)+V1X,((V6Y-V1Y)*k&#FFFF)/(L_m&#FFFF)+V1Y,((V6Z-V1Z)*k&#FFFF)/(L_m&#FFFF)+V1Z
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
        DB ((V1X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X,((V1Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y,((V1Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z
; macro DOTV0_1 expanded
V2_4=_
_=_+1
        DB ((V4X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X,((V4Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y,((V4Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z
; macro DOTV0_1 expanded
V2_7=_
_=_+1
        DB ((V7X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X,((V7Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y,((V7Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z
; macro DOTV0_1 expanded
V2_10=_
_=_+1
        DB ((V10X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X,((V10Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y,((V10Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z
; macro DOTV0_1 expanded
V2_11=_
_=_+1
        DB ((V11X-V2X)*k&#FFFF)/(L_m&#FFFF)+V2X,((V11Y-V2Y)*k&#FFFF)/(L_m&#FFFF)+V2Y,((V11Z-V2Z)*k&#FFFF)/(L_m&#FFFF)+V2Z
; macro DOTV0_1 expanded
V3_0=_
_=_+1
        DB ((V0X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X,((V0Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y,((V0Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z
; macro DOTV0_1 expanded
V3_4=_
_=_+1
        DB ((V4X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X,((V4Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y,((V4Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z
; macro DOTV0_1 expanded
V3_7=_
_=_+1
        DB ((V7X-V3X)*k&#FFFF)/(L_m&#FFFF)+V3X,((V7Y-V3Y)*k&#FFFF)/(L_m&#FFFF)+V3Y,((V7Z-V3Z)*k&#FFFF)/(L_m&#FFFF)+V3Z
; macro DOTV0_1 expanded
V4_2=_
_=_+1
        DB ((V2X-V4X)*k&#FFFF)/(L_m&#FFFF)+V4X,((V2Y-V4Y)*k&#FFFF)/(L_m&#FFFF)+V4Y,((V2Z-V4Z)*k&#FFFF)/(L_m&#FFFF)+V4Z
; macro DOTV0_1 expanded
V4_3=_
_=_+1
        DB ((V3X-V4X)*k&#FFFF)/(L_m&#FFFF)+V4X,((V3Y-V4Y)*k&#FFFF)/(L_m&#FFFF)+V4Y,((V3Z-V4Z)*k&#FFFF)/(L_m&#FFFF)+V4Z
; macro DOTV0_1 expanded
V6_0=_
_=_+1
        DB ((V0X-V6X)*k&#FFFF)/(L_m&#FFFF)+V6X,((V0Y-V6Y)*k&#FFFF)/(L_m&#FFFF)+V6Y,((V0Z-V6Z)*k&#FFFF)/(L_m&#FFFF)+V6Z
; macro DOTV0_1 expanded
V6_1=_
_=_+1
        DB ((V1X-V6X)*k&#FFFF)/(L_m&#FFFF)+V6X,((V1Y-V6Y)*k&#FFFF)/(L_m&#FFFF)+V6Y,((V1Z-V6Z)*k&#FFFF)/(L_m&#FFFF)+V6Z

        DB 12*5                 ;граней
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
; (expanded at its calls) _=\9
; (expanded at its calls)         DW #FF-_+VTX3DY
; (expanded at its calls)         ENDM

; (expanded at its calls)         MACRO KPOL
; (expanded at its calls)         KPOLY \0A,\0,\1,\2,#00,#0004,#0000,#0400,#F200,\6
; (expanded at its calls)         KPOLY \0B,\0,\2,\3,#00,#0004,#0000,#0400,#F200,\7
; (expanded at its calls)         KPOLY \0C,\0,\3,\4,#00,#0004,#0000,#0400,#F200,\8
; (expanded at its calls)         KPOLY \0D,\0,\4,\5,#00,#0004,#0000,#0400,#F200,\9
; (expanded at its calls)         KPOLY \0E,\0,\5,\1,#00,#0004,#0000,#0400,#F200,\P*0+\9
; (expanded at its calls)         ENDM
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
_=1
        DW #FF-_+VTX3DY
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
_=4
        DW #FF-_+VTX3DY
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
_=11
        DW #FF-_+VTX3DY
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
_=10
        DW #FF-_+VTX3DY
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
_=0*0+7
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
_=0
        DW #FF-_+VTX3DY
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
_=7
        DW #FF-_+VTX3DY
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
_=9
        DW #FF-_+VTX3DY
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
_=8
        DW #FF-_+VTX3DY
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
_=1*0+4
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
_=3
        DW #FF-_+VTX3DY
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
_=5
        DW #FF-_+VTX3DY
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
_=8
        DW #FF-_+VTX3DY
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
_=9
        DW #FF-_+VTX3DY
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
_=2*0+6
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
_=2
        DW #FF-_+VTX3DY
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
_=6
        DW #FF-_+VTX3DY
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
_=10
        DW #FF-_+VTX3DY
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
_=11
        DW #FF-_+VTX3DY
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
_=3*0+5
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
_=0
        DW #FF-_+VTX3DY
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
_=1
        DW #FF-_+VTX3DY
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
_=8
        DW #FF-_+VTX3DY
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
_=5
        DW #FF-_+VTX3DY
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
_=4*0+11
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
_=4
        DW #FF-_+VTX3DY
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
_=8
        DW #FF-_+VTX3DY
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
_=2
        DW #FF-_+VTX3DY
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
_=3
        DW #FF-_+VTX3DY
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
_=5*0+11
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
_=2
        DW #FF-_+VTX3DY
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
_=9
        DW #FF-_+VTX3DY
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
_=7
        DW #FF-_+VTX3DY
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
_=10
        DW #FF-_+VTX3DY
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
_=6*0+3
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
_=6
        DW #FF-_+VTX3DY
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
_=9
        DW #FF-_+VTX3DY
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
_=1
        DW #FF-_+VTX3DY
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
_=0
        DW #FF-_+VTX3DY
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
_=7*0+10
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
_=4
        DW #FF-_+VTX3DY
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
_=1
        DW #FF-_+VTX3DY
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
_=9
        DW #FF-_+VTX3DY
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
_=2
        DW #FF-_+VTX3DY
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
_=8*0+5
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
_=8
        DW #FF-_+VTX3DY
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
_=1
        DW #FF-_+VTX3DY
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
_=7
        DW #FF-_+VTX3DY
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
_=6
        DW #FF-_+VTX3DY
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
_=9*0+2
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
_=6
        DW #FF-_+VTX3DY
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
_=7
        DW #FF-_+VTX3DY
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
_=0
        DW #FF-_+VTX3DY
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
_=11
        DW #FF-_+VTX3DY
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
_=10*0+3
        DW #FF-_+VTX3DY
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
_=10
        DW #FF-_+VTX3DY
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
_=0
        DW #FF-_+VTX3DY
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
_=4
        DW #FF-_+VTX3DY
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
_=5
        DW #FF-_+VTX3DY
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
_=11*0+3
        DW #FF-_+VTX3DY

ALPHA   DW SIGNOFS+#40
BETA    DW SIGNOFS+#00
GAMMA   DW SIGNOFS+#00

        INCLUDE "texpp4.asm"

GSRESET
        XOR A                   ;TURN OFF DMA MODE
        OUT (DMA_CST),A
        IN A,(GSCFG0)
        AND #FF-M_CKSEL0-M_CKSEL1-M_RAMRO-M_NOROM-M_EXPAG
        OR C_10MHZ
        OUT (GSCFG0),A
        RST 0

        INCLUDE "vujump.asm"

        DISPLAY 'без инсталлеров конец кода=',$
;NGS инсталлеры - можно затирать
        INCLUDE "instlpp.asm"
pal256
        INCBIN "palbrick"
        DISPLAY 'с инсталлерами конец кода=',$
        ENT
LENGSPROG=$-WASGSPROG
        DISPLAY /T,LENGSPROG

; en: the packed background at #A000 (DEPKS_picarea); demon16.p is not on the author's disk
        DS DEPKS_picarea-$
BLOCK_PIC1
        INCBIN "demon16.p"
SIZE_PIC1=$-BLOCK_PIC1
end
        ORG #C000,pgdata5
        INCBIN "kishki.C"
; en: a TR-DOS file name at #5CDD and the author's Hrust packer / release maker
        ORG #5CDD,0
        DB 'GSHEDGE B'
        INCLUDE "m2hrust.asm"

