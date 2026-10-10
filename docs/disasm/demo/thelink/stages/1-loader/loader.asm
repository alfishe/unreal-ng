; The Link (invdemo, 2009) - release loader, stage 1: load and start (sjasmplus)
;
; Replaces the author's start through the assembler (THELINK.B -> ALASM -> GO). Run from
; THELINK.B after CLEAR 24575 at #6000:
;   1. selects the Pentagon 1024 paging (#EFF7 = 0) - no check of the machine yet, that is
;      stage 2 (../2-machine-check);
;   2. loads the title picture to the screen, the demo's main block to #7800 (thelink_main.asm)
;      and every page file to #C000 of its page (the table at PageFiles, the ORG #C000,page +
;      INCBIN lines of THELINK.asm); the rest of each page is zeroed, as ALASM left it;
;   3. jumps to GO (#7800), as ALASM's RUN did.
; A missing file returns to BASIC.
;
; TR-DOS from code: #3D13 with the function in C. #0A finds the file named at #5CDD (9 bytes
; compared: name and type, #5D06), #08 reads its descriptor to #5CDD, #05 reads B sectors from
; track D sector E to HL.

        DEVICE NOSLOT64K

TRDOS     EQU #3D13
DESC      EQU #5CDD   ; descriptor: name 8, type 1, start 2, length 2, sectors 1, sector 1, track 1
DESC_LEN  EQU DESC+11
DESC_SECS EQU DESC+13
DESC_SEC  EQU DESC+14
CMP_LEN   EQU #5D06   ; how many descriptor bytes #0A compares

GO        EQU #7800   ; thelink_main.asm
MAIN_END  EQU #B155   ; end of the main block (build.py checks it)
RESBUF    EQU #BD00   ; GO's own area starts here

GSCOM     EQU #BB

        ORG #6000
Start
        LD IY,#5C3A
        LD BC,#EFF7             ; 1024K paging (#7FFD bits 5-7 are page bits), standard screen
        XOR A
        OUT (C),A

        ; Title while the rest loads
        LD HL,NameTitle
        LD DE,#4000
        CALL LoadFile
        RET C                   ; a file is missing: back to BASIC

        LD HL,NameMain
        LD DE,GO
        CALL LoadFile
        RET C
        LD HL,MAIN_END          ; the last sector's padding: zero up to GO's area
        LD BC,RESBUF-MAIN_END
        CALL ZeroArea

        LD IX,PageFiles
.page
        LD A,(IX+0)
        OR A
        JR Z,.started
        LD BC,#7FFD
        OUT (C),A
        PUSH IX
        POP HL
        INC HL
        LD DE,#C000
        CALL LoadFile
        RET C
        ; HL = the end of the file: zero the rest of the page (none when it ends at #FFFF)
        LD A,H
        OR L
        JR Z,.next
        LD B,H
        LD C,L
        XOR A
        SUB C
        LD C,A
        LD A,0
        SBC A,B
        LD B,A                  ; BC = #10000 - HL
        CALL ZeroArea
.next
        LD DE,10
        ADD IX,DE
        JR .page
.started
        ; GO clears #D800-#DAFF of the page at #C000 (it loads A with #17 for screen 1's attributes
        ; but never OUTs it), and the last page loaded is the music: page 7 there, as the author meant
        LD A,#17
        LD BC,#7FFD
        OUT (C),A
        JP GO

; HL -> 9-byte name and type, DE = address. Loads the whole file; HL = address + length.
; Carry set: not found (HL -> the name).
LoadFile
        PUSH DE
        PUSH HL
        LD DE,DESC
        LD BC,9
        LDIR
        LD A,9
        LD (CMP_LEN),A
        LD C,#0A
        CALL TRDOS
        POP HL
        LD A,C
        CP #FF
        JR NZ,.found
        POP DE
        SCF
        RET
.found
        LD C,#08                ; A = the catalog entry
        CALL TRDOS
        POP HL
        PUSH HL
        LD A,(DESC_SECS)
        LD B,A
        LD DE,(DESC_SEC)        ; E = sector, D = track
        LD C,#05
        CALL TRDOS
        POP HL
        LD BC,(DESC_LEN)
        ADD HL,BC
        OR A
        RET

; Zero BC bytes from HL (BC >= 1)
ZeroArea
        LD (HL),0
        DEC BC
        LD A,B
        OR C
        RET Z
        LD D,H
        LD E,L
        INC DE
        LDIR
        RET

        ; 9-byte TR-DOS names: 8 characters and the type
NameTitle DB "thelink$C"
NameMain  DB "thelink C"

        ; #7FFD value, name and type (THELINK.asm: ORG #C000,page + INCBIN)
        MACRO PAGEFILE pg,name
        DB pg
        DB name
        ENDM
PageFiles
        PAGEFILE #90,"kishki  C"   ; card data 1..10
        PAGEFILE #91,"baba    C"
        PAGEFILE #92,"baba    0"   ; + zeros to #FFFF: DS -$ in THELINK.asm (the mask)
        PAGEFILE #94,"mill    C"
        PAGEFILE #95,"mill    0"
        PAGEFILE #96,"girl13  2"
        PAGEFILE #97,"blugr   C"
        PAGEFILE #11,"blugr   0"
        PAGEFILE #13,"blugr   1"
        PAGEFILE #14,"blugr   2"
        PAGEFILE #52,"ROTPREGSC"   ; card code 1..9 (10 is GO's GSRESET)
        PAGEFILE #55,"ROBPREGSC"
        PAGEFILE #30,"TUNNELGSC"
        PAGEFILE #31,"ROTATEGSC"
        PAGEFILE #32,"ROTBARGSC"
        PAGEFILE #33,"MULBARGSC"
        PAGEFILE #34,"HEDGEGS C"
        PAGEFILE #35,"BABAGS  C"
        PAGEFILE #36,"TEXGS   C"
        PAGEFILE #B0,"TUNNELZXC"   ; the ZX halves of the effects
        PAGEFILE #B1,"ROTATEZXC"
        PAGEFILE #B2,"ROTBARZXC"
        PAGEFILE #B3,"MULBARZXC"
        PAGEFILE #B4,"HEDGEZX1C"
        PAGEFILE #B5,"HEDGEZX2C"
        PAGEFILE #B6,"BABAZX  C"
        PAGEFILE #B7,"TEXZX   C"
        PAGEFILE #54,"thelinkmC"   ; music (TurboSound FM)
        DB 0
LoaderEnd

        ASSERT LoaderEnd < GO
        SAVEBIN "loader.bin",Start,LoaderEnd-Start
