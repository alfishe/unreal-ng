; =====================================================================================================================
; 16CCON.asm - the author's converter of a 16-colour BMP into his column-by-column 16-colour screen format
; =====================================================================================================================
;
; A Spectrum-side tool, NOT in the demo build: it runs on the Pentagon after assembly, reads a BMP file from the
; TR-DOS disk and saves the result through SAVEOBJ4. Nothing of it reaches the demo's memory image, so build.py
; does not assemble it. Neither its input ("creds" b) nor its output ("creds" C) is on the author's disk; the
; demo's credits screen is "credits" C, an ordinary 6912-byte screen.
;
; Run: the entry at the end of the file (`CALL GO : JP nenado`):
;   1. GO finds the input file named at #5CDD ('creds' with type 'b', set below) and reads its descriptor;
;   2. clears the 16-colour screen (page 5 #4000/#6000, page 4 #C000/#E000 - the pixel areas only);
;   3. builds IGRBtab (#7900): BMP byte %IGRBigrb (left pixel, right pixel) -> screen byte %iIgrbGRB;
;   4. skips the BMP header and palette (the data offset from bytes 10-11 of the file), then reads 192 lines of
;      128 bytes (256 pixels at 4 bits) and puts each through IGRBtab on screen: 4 bytes per screen address,
;      #C000+a (page 4), #4000+a (page 5), #E000+a (page 4), #6000+a (page 5);
;   5. turns the 16-colour mode on (#EFF7 = 1) to show the picture;
;   6. copies the screen column by column to #A000-#FFFF (from #C000 on in page 0, #7FFD = #10): 128 columns
;      (32 addresses x the 4 planes) of 192 lines each, #6000 bytes; #EFF7 = 0; RET;
;   7. SAVEOBJ4 (nenado) saves ObjTab: 'creds' C from #A000, #6000 bytes, page #10.
; The DISPLAY note: the result was packed with ZXRar (header off) afterwards.
;
; Memory: bmpBUF #7800 (one BMP line), IGRBtab #7900, inbuf #9800-#B7FF (8 KB of the file at a time), code #8000.
;
; Note: the screen line address comes from ROM #22B0 (inside PIXEL-ADD) with A = B-1, but that ROM code takes
; the line's low 6 bits from B and only the third (bits 6-7) from A. So BMP line B-1 lands on line
; (B-1 & #C0) | (B & #3F): one line lower inside each third, and the first line of a third (B = 64, 128, 192)
; on the top line of the third. Unless the picture was drawn for it, the result is off by a line.
;
; Converter additions: multi-operand lines split, JNZ/JZ as JR, EXA/EXD/LX/HX spelled out, `high` for ALASM's `'`,
; single quotes for the strings, and `INCLUDE "SAVEOBJ4.asm"` for ALASM's `INCLUDE "B:SAVEOBJ*",#C0`.
; Comments starting with "en:" translate the author's Russian ones.
; =====================================================================================================================
;конвертилка из bmp16 в постолбцовый формат
; en: converter from bmp16 to the column-by-column format
bmpBUF=#7800
IGRBtab=#7900                   ;для декодирования bmp (en: for decoding the bmp)
inbuf=#9800
inlen=#2000
        ORG #8000
begin
GO
; Input file: find (C = #0A) the name at #5CDD, read its descriptor (C = #08); its first sector/track -> #5CF4,
; where LDBYTE reads on
        LD A,#10
        CALL OUTA
        LD C,#A                 ;find
        CALL #3D13
        LD A,C
        LD C,#8                 ;read desc
        CALL #3D13
        LD DE,(#5CDD+14)
        LD (23796),DE

; Clear the pixel areas of the 16-colour screen: page 5 (#4000, #6000) and page 4 (#C000, #E000)
        LD A,#14
        CALL OUTA
        LD HL,#4000
        LD DE,#4001
        LD BC,#17FF
        LD (HL),L
        LDIR
        LD HL,#6000
        LD DE,#6001
        LD BC,#17FF
        LD (HL),L
        LDIR
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

; en: build the bmp recoding table (left pixel IGRB -> bits 6, 2-0; right pixel igrb -> bits 7, 5-3)
;строим таблицу перекодирования bmp
        LD HL,IGRBtab
MKIGRB  LD A,L                  ;%IGRBigrb -> %iIgrbGRB
        SRA A
        SRA A
        SRA A
        SRA A
        AND %01000111
        LD C,A
        LD A,L
        RLA
        RLA
        RLA
        RLA
        SRA A
        AND %10111000
        OR C
        LD (HL),A
        INC L
        JR NZ,MKIGRB

; en: read the first portion of data
        LD IX,inbuf+inlen-1
        CALL LDBYTE             ;считываем первую порцию данных

; BC = bfOffBits (the pixel data offset, #76 = 118 for a 16-colour BMP); skip that many bytes
        LD BC,(inbuf+10)        ;#76
        CALL LDBYTE
        DEC BC
        LD A,B
        OR C
        JR NZ,$-6               ;пропустили заголовок bmp и палитру
; en: skipped the bmp header and the palette

; 192 lines; DE = screen address of line B-1 (ROM #22B0, see the note in the header)
        LD A,#14
        CALL OUTA
        LD B,192
REBMP0  PUSH BC
        LD A,B
        DEC A
        LD C,0
        CALL 8880
        EX DE,HL

;сначала читаем строчку в bmpBUF
; en: first read the line into bmpBUF (128 bytes)
        LD HL,bmpBUF
        LD B,32*4
        CALL LDBYTE
        LD (HL),A
        INC L
        DJNZ $-5

;теперь перекодируем строчку из формата %LLLLRRRR в %RLRRRLLL
;и переносим результат на экран
; en: now recode the line from %LLLLRRRR to %RLRRRLLL
; en: and move the result to the screen (4 consecutive BMP bytes -> the same address in 4 planes)
        LD HL,bmpBUF
        LD B,high IGRBtab
REBMP1
        SET 7,D
        LD C,(HL)               ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
        RES 7,D
        LD C,(HL)               ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
        SET 5,D
        SET 7,D
        LD C,(HL)               ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
        RES 7,D
        LD C,(HL)               ;%IGRBigrb -> %iIgrbGRB
        LD A,(BC)
        LD (DE),A
        INC L
        RES 5,D
        INC E
        LD A,E
        AND 31
        JR NZ,REBMP1
        POP BC
        DJNZ REBMP0

;----------------------------
;теперь показываем
; en: now show it (#EFF7 bit 0 = 16 colours)
        LD BC,#EFF7
        LD A,#01
        OUT (C),A

;конвертим
; en: convert: for each of the 128 columns, 192 bytes down the screen (DHL = next line) -> IX = #A000..#FFFF;
; each byte is read with page 4 (#14) at #C000 and written with page 0 (#10) at #C000
        LD IX,#A000             ;pg0
        LD HL,#C000
        LD B,32*4
C2ST0
        PUSH BC
        PUSH HL
        LD BC,#C000             ;C=OLDBYTE
C2ST1   LD A,(HL)
        EX AF,AF'
        LD A,#10
        CALL OUTA
        EX AF,AF'
        LD (IX),A
        INC IX
        LD A,#14
        CALL OUTA
        CALL DHL
        DJNZ C2ST1
        POP HL
;nxtst
; next column: #C0xx -> #40xx -> #E0xx -> #60xx -> next L (#C0xx+1)
        LD A,H
        XOR 128
        BIT 7,H
        LD H,A
        JR NZ,nxsQ
        LD A,H
        XOR 32
        BIT 5,H
        LD H,A
        JR Z,nxsQ
        INC L
nxsQ
        POP BC
        DJNZ C2ST0
        LD BC,#EFF7
        LD A,0
        OUT (C),A
        RET
; OUTA: OUT (#7FFD),A
OUTA
        PUSH BC
        LD BC,32765
        OUT (C),A
        POP BC
        RET

; DHL: HL = the screen address one pixel line down
DHL
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


; LDBYTE: A = next byte of the file (IX = read pointer in inbuf); at the end of inbuf reads the next #20 sectors
; (8 KB) from the disk position in #5CF4 (TR-DOS #3D13, C = #05)
LDBYTE
        LD A,(IX)
        INC IXL
        RET NZ
        INC IXH
        PUSH AF
        LD A,IXH
        CP high inbuf+high inlen
        JR Z,lDBYLD
        POP AF
        RET
lDBYLD
        PUSH BC
        PUSH DE
        PUSH HL
        LD HL,inbuf
        PUSH HL
        LD DE,(23796)
        LD BC,inlen+#05
        CALL #3D13
        POP IX
        POP HL
        POP DE
        POP BC
        POP AF
        RET
; The input file name for GO: 'creds' with type 'b' (TR-DOS name buffer #5CDD)
ZXC
        ORG #5CDD
        DB 'creds   b'
        ORG ZXC
;DISPLAY "RUN & SAVE #A000,#6000"
        DISPLAY 'pack by ZXRar: header=off'
; ObjTab for SAVEOBJ4: name+type, address, length, page, and two more bytes (#A000; for a C file SAVEOBJ4 writes
; them as the descriptor's start field)
ObjTab  DB 'creds   C'
        DW #A000
        DW #6000
        DB #10
        DW #A000
; `make` defined: SAVEOBJ4's CAPS SHIFT test is left out; its nenado saves ObjTab straight away
make
        INCLUDE "SAVEOBJ4.asm"
; The run entry: convert, then save
        ORG $
        CALL GO
        JP nenado

