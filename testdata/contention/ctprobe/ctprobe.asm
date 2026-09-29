; ctprobe - contention probe (docs/inprogress/2026-09-28-m1-contention/test-programs.md, section 3)
;
; Times code fragments at exact frame T-states with the Bobrowski / Rak measuring engine (engine.asm, appended
; after this file) and compares the durations with the expected values for the machine's contention class,
; printing one line per case as it goes (the ROM's print routines, as ZEXALL does).
; Each case places a fragment at an address, maps a page if it needs one, and times it at COUNT consecutive
; T-states from ONSET + OFFSET. A duration is the fragment's plus whatever the RET placed after it waits (the
; engine subtracts the RET's 10 T).
;
; Loads and runs at 40000 (uncontended on every machine; the engine uses #BE00-#BFC2). Standalone:
; CLEAR 39999, load the code, RANDOMIZE USR 40000 (or PRINT USR 40000: the number of wrong values). It detects
; everything itself and prints a report; the border ends green (all values right) or red.
; A host may preset any setting below (#FF = detect), start at HOSTENTRY with SP below #BE00 and wait for DONE.
;
; The expected tables (EXPECTED) are filled by the generator (core/tests/emulator/video/ctprobe_test.cpp) from
; its oracle; in the plain source they are zero.

        org 40000

START:
        jp MAIN
HOSTENTRY:
        call MAIN
HostSpin:
        jr HostSpin

; ---- settings: #FF (ONSET: 0) = detect ----

ONSET:   dw 0           ; first contended T-state, INT-relative (69888 T frame: 14335, else 14361)
CLASS:   db #FF         ; 0 Ferranti ULA 48K, 1 Ferranti ULA 128K, 2 gate array, 3 no contention,
                        ; 4 no contention on a 69888 T frame (Scorpion: its ports answer the fetched attribute)
CAPS:    db #FF         ; bit 0: #7FFD paging works, bit 1: +2A/+3 #1FFD layouts
DEF7FFD: db #FF         ; #7FFD outside the paging cases; a case ORs its page in
DEF1FFD: db #FF         ; #1FFD outside the layout cases
ONLY:    db 0           ; run only this case id (0 = all)
SHOW:    db 1           ; print the report

; ---- outputs ----

DONE:    db 0           ; 1 when finished
FAILS:   dw 0           ; values that differ from the expected table

; ---- current case (a copy of its record) ----

CUR:
CURID:     db 0
CURFLAGS:  db 0         ; bit 0 page at #C000, bit 1 +3 layout, bit 2 store A (CTVALUE) not the duration,
                        ; bit 3 time the RET found in ROM, bit 4 fill the first screen cells first
CUR7FFD:   db 0         ; page at #C000 (#FF = leave)
CURMIRROR: db 0         ; also copy the placed fragment into this page at the same #8000-slot offset (#FF = no)
CURTARGET: dw 0         ; where the fragment runs (0 = FRAGBUF)
CURSRC:    dw 0
CURLEN:    db 0
CUROFS:    dw 0         ; first T-state, relative to ONSET
CURCOUNT:  db 0
CURRES:    dw 0         ; results
CURNAME:   db "     "
CUREND:
RECLEN     equ CUREND-CUR

CASEPTR:   dw 0
CURT:      dw 0
SPSAVE:    dw 0
IXDATA:    db 0, 0
CTVALUE:   db 0
ROMRET:    dw 0
CASEBAD:   db 0
FIRSTBAD:  dw 0         ; record of the first case with a wrong value (0 = none)
BADT:      dw 0
BADGOT:    db 0
BADEXP:    db 0

; ---- main ----

MAIN:
        ld bc,#1FFD             ; a Scorpion's ROM leaves 7 MHz on: a read of #1FFD selects 3.5 MHz (harmless
        in a,(c)                ; elsewhere); the engine needs the INT pulse over before its handler returns
        call INSTINT
        call FRAMETIME

        ld hl,(ONSET)
        ld a,h
        or l
        jr nz,HaveOnset
        call IsFrame48
        ld hl,14335
        jr z,SetOnset
        ld hl,14361
SetOnset:
        ld (ONSET),hl
HaveOnset:

        ld a,(DEF7FFD)          ; the mapping BASIC runs with: BANK_M / BANK678 (128 / +3 BASIC keep them), the
        cp #FF                  ; lock bit cleared, and the 48 BASIC ROM bits set when that ROM is paged in
        jr nz,Def7Done          ; (a reset straight into it leaves both system variables zero)
        ld a,(#5B5C)
        and #DF
        ld c,a
        call HasFont48
        ld a,c
        jr nz,Def7Set
        or #10
Def7Set:
        ld (DEF7FFD),a
Def7Done:
        ld a,(DEF1FFD)
        cp #FF
        jr nz,Def1Done
        ld a,(#5B67)
        ld c,a
        call HasFont48
        ld a,c
        jr nz,Def1Set
        or #04
Def1Set:
        ld (DEF1FFD),a
Def1Done:

        ld a,(CLASS)
        cp #FF
        call z,DetectClass
        ld a,(CAPS)
        cp #FF
        call z,DetectCaps

        ld hl,0                 ; a RET in ROM for X-02
FindRet:
        ld a,(hl)
        cp #C9
        jr z,FoundRet
        inc hl
        jr FindRet
FoundRet:
        ld (ROMRET),hl
        ld hl,0
        ld (FAILS),hl
        call Header

        ld hl,CASES
CaseLoop:
        ld (CASEPTR),hl
        ld a,(hl)
        or a
        jp z,Finish
        ld de,CUR
        ld bc,RECLEN
        ldir
        call CaseRuns
        jp nz,SkipCase

        ld a,(CURFLAGS)         ; the first screen cells hold a known pattern (the floating-bus case)
        and 16
        call nz,FillCells

        ld a,(CUR7FFD)          ; the page at #C000
        cp #FF
        call nz,MapPage

        ld a,(CURFLAGS)         ; RET in ROM: nothing to place
        and 8
        jr z,Place
        ld hl,(ROMRET)
        ld (CURTARGET),hl
        jr Placed
Place:
        ld de,(CURTARGET)       ; the fragment and its RET (target 0 = FRAGBUF, uncontended)
        ld a,d
        or e
        jr nz,HaveTarget
        ld de,FRAGBUF
        ld (CURTARGET),de
HaveTarget:
        ld hl,(CURSRC)
        ld a,(CURLEN)
        or a
        jr z,PlaceRet
        ld c,a
        ld b,0
        ldir
PlaceRet:
        ld a,#C9
        ld (de),a
        ld a,(CURMIRROR)
        cp #FF
        call nz,Mirror
Placed:

        ld hl,(ONSET)           ; first T-state
        ld de,(CUROFS)
        add hl,de
        ld (CURT),hl
        ld hl,(CURRES)
        ld (RESPTR),hl

TimeLoop:
        ld ix,IXDATA            ; the (IX+d) fragments address uncontended data
        ld hl,(CURT)
        inc hl                  ; CODETIME counts one more: Rak's 48K grid shows the first wait at 14336
        ld de,(CURTARGET)
        call CODETIME
        di                      ; the page mapped may not hold a ROM's IM1 handler
        ld a,(CURFLAGS)
        and 4
        ld a,l
        jr z,Store
        ld a,(CTVALUE)
Store:
        ld de,(RESPTR)
        ld (de),a
        inc de
        ld (RESPTR),de
        ld hl,(CURT)
        inc hl
        ld (CURT),hl
        ld a,(CURCOUNT)
        dec a
        ld (CURCOUNT),a
        jr z,CaseDone
        ei
        jr TimeLoop

CaseDone:
        ld a,(CUR7FFD)          ; back to the default mapping
        cp #FF
        jr z,Restored
        ld a,(DEF7FFD)
        call Out7FFD
Restored:
        ei
        ld hl,(CASEPTR)         ; the record again (the loop counted CURCOUNT down), the verdict, its line
        ld de,CUR
        ld bc,RECLEN
        ldir
        ld hl,0
        ld (FIRSTBAD),hl
        call CaseVerdict
        call PrintCase

NextCase:
        ld hl,(CASEPTR)
        ld de,RECLEN
        add hl,de
        jp CaseLoop

SkipCase:
        call PrintSkip
        jr NextCase

Finish:
        call PrintSummary
        ld a,1
        ld (DONE),a
        ld bc,(FAILS)
        ret

RESPTR: dw 0

; ---- helpers ----

; Z if the frame is 69888 T
IsFrame48:
        ld hl,(FRAMET)
        ld de,69888-32768
        or a
        sbc hl,de
        ret

; Z if a 48 BASIC ROM is paged in: its character set at #3D00 (space, then "!"); the 128K and +3 editor ROMs
; start alike but hold code there
HasFont48:
        ld hl,#3D00
        ld de,Font48Sig
        ld b,16
FontSigLoop:
        ld a,(de)
        cp (hl)
        ret nz
        inc hl
        inc de
        djnz FontSigLoop
        ret
Font48Sig:
        db 0, 0, 0, 0, 0, 0, 0, 0, 0, #10, #10, #10, #10, 0, #10, 0

; CLASS from the frame length and a NOP at #4000 timed on the onset (+ the RET after it): 4 no contention,
; 14 Ferranti ULA (6 + 4), 9 the gate array (1 + 4)
DetectClass:
        ld hl,#4000
        ld (hl),0
        inc hl
        ld (hl),#C9
        ld hl,(ONSET)
        inc hl
        ld de,#4000
        call CODETIME
        ld c,l                  ; the duration (IsFrame48 uses HL)
        push bc
        call IsFrame48          ; Z: 69888 T
        pop bc
        ld a,c
        jr nz,Class70
        cp 4
        ld a,4                  ; Scorpion-type
        jr z,SetClass
        xor a                   ; 48K
        jr SetClass
Class70:
        cp 4
        ld a,3
        jr z,SetClass
        ld a,c
        cp 14
        ld a,1
        jr z,SetClass
        ld a,2
SetClass:
        ld (CLASS),a
        ret

; CAPS: #7FFD paging if a byte written at #C000 with page 1 mapped does not show with page 0 (never on the
; 48K); the layouts on the gate array with paging open
DetectCaps:
        xor a
        ld (CAPS),a
        ld a,(CLASS)
        or a
        ret z
        di
        ld bc,#7FFD
        ld a,(DEF7FFD)
        and #F8
        ld e,a
        out (c),e               ; page 0
        ld a,(#C000)
        ld (PageSave0),a
        ld a,#AA
        ld (#C000),a
        ld a,e
        or 1
        out (c),a               ; page 1
        ld a,(#C000)
        ld (PageSave1),a
        ld a,#55
        ld (#C000),a
        out (c),e
        ld a,(#C000)
        ld d,a
        ld a,e
        or 1
        out (c),a
        ld a,(PageSave1)
        ld (#C000),a
        out (c),e
        ld a,(PageSave0)
        ld (#C000),a
        ld a,(DEF7FFD)
        out (c),a
        ei
        ld a,d
        cp #AA
        ret nz
        ld a,(CLASS)
        cp 2
        ld a,1
        jr nz,SetCaps
        ld a,3
SetCaps:
        ld (CAPS),a
        ret
PageSave0: db 0
PageSave1: db 0

; NZ if the current case does not run here (ONLY, CAPS)
CaseRuns:
        ld a,(ONLY)
        or a
        jr z,CheckCaps
        ld b,a
        ld a,(CURID)
        cp b
        ret nz
CheckCaps:
        ld a,(CAPS)
        cpl
        ld b,a
        ld a,(CURFLAGS)
        and 3
        and b
        ret

; the page CUR7FFD at #C000
MapPage:
        ld b,a
        ld a,(DEF7FFD)
        and #F8
        or b
; #7FFD = A, and BANK_M with it: the 128K and +3 ROMs' interrupt handlers page from BANK_M (the +3's disk
; motor timer does), so a mapping kept across interrupts must be there too
Out7FFD:
        ld (#5B5C),a
        ld bc,#7FFD
        out (c),a
        ret

; the fragment and its RET, placed at FRAGBUF, also into page CURMIRROR at the same offset of the #8000 slot
Mirror:
        ld b,a
        ld a,(DEF7FFD)
        and #F8
        or b
        ld bc,#7FFD
        di
        out (c),a
        ld hl,FRAGBUF
        ld de,FRAGBUF+#4000
        ld a,(CURLEN)
        ld c,a
        ld b,0
        inc bc
        ldir
        ld a,(DEF7FFD)
        ld bc,#7FFD
        out (c),a
        ei
        ret

; the first 32 bitmap and attribute bytes of the screen: #10+n and #40+n
FillCells:
        ld hl,#4000
        ld de,#5800
        ld b,32
        ld c,#10
FillLoop:
        ld (hl),c
        ld a,c
        add a,#30
        ld (de),a
        inc hl
        inc de
        inc c
        djnz FillLoop
        ret

; ---- verdict and output ----

; CASEBAD = wrong values of the current case (which ran); FAILS counts them, BADT / BADGOT / BADEXP hold the
; first one (FIRSTBAD = 0 on entry)
CaseVerdict:
        xor a
        ld (CASEBAD),a
        ld hl,(CURRES)          ; expected = EXPECTED + CLASS * (RESULTSEND - RESULTS) + (results - RESULTS)
        ld de,RESULTS
        or a
        sbc hl,de
        ld de,EXPECTED
        add hl,de
        ld a,(CLASS)
        or a
        jr z,ExpBase
        ld de,RESULTSEND-RESULTS
        ld b,a
ExpMul:
        add hl,de
        djnz ExpMul
ExpBase:
        ex de,hl                ; DE expected, HL measured
        ld hl,(CURRES)
        ld a,(CURCOUNT)
        ld b,a
        ld c,0                  ; index
VerdictLoop:
        ld a,(de)
        cp (hl)
        jr z,VerdictNext
        push hl
        ld hl,CASEBAD
        inc (hl)
        ld hl,(FAILS)
        inc hl
        ld (FAILS),hl
        ld hl,(FIRSTBAD)
        ld a,h
        or l
        pop hl
        jr nz,VerdictNext
        ld a,(de)
        ld (BADEXP),a
        ld a,(hl)
        ld (BADGOT),a
        push hl
        ld hl,(CASEPTR)
        ld (FIRSTBAD),hl
        ld hl,(ONSET)
        push de
        ld de,(CUROFS)
        add hl,de
        ld e,c
        ld d,0
        add hl,de
        ld (BADT),hl
        pop de
        pop hl
VerdictNext:
        inc hl
        inc de
        inc c
        djnz VerdictLoop
        ret

; The output goes through the ROM's own print routines (channel 2, RST #10), as ZEXALL's does: the lines
; scroll up (SCR_CT is kept at #FF, so the ROM never stops to ask "scroll?"). The 48 BASIC ROM is paged in
; for it when another one is (from 128 / +3 BASIC), BANK_M / BANK678 with it.

RomOn:
        xor a
        ld (ROMSWAPPED),a
        call HasFont48
        ret z
        ld a,1
        ld (ROMSWAPPED),a
        ld a,(DEF7FFD)
        or #10
        call Out7FFD
        ld a,(CLASS)
        cp 2
        ret nz
        ld a,(DEF1FFD)
        or #04
        jr Out1FFD
RomOff:
        ld a,(ROMSWAPPED)
        or a
        ret z
        ld a,(DEF7FFD)
        call Out7FFD
        ld a,(CLASS)
        cp 2
        ret nz
        ld a,(DEF1FFD)
; #1FFD = A, and BANK678 with it
Out1FFD:
        ld (#5B67),a
        ld bc,#1FFD
        out (c),a
        ret
ROMSWAPPED: db 0

; zero-terminated string at HL
PrintStr:
        ld a,(hl)
        or a
        ret z
        push hl
        rst #10
        pop hl
        inc hl
        jr PrintStr

; B characters at HL
PrintN:
        ld a,(hl)
        push hl
        push bc
        rst #10
        pop bc
        pop hl
        inc hl
        djnz PrintN
        ret

NewLine:
        ld a,#FF
        ld (#5C8C),a            ; SCR_CT
        ld a,13
        rst #10
        ret

; HL in decimal (STACK-BC, PRINT-FP)
PrintNum:
        ld b,h
        ld c,l
        call #2D2B
        jp #2DE3

; A = 0: "no", else "yes"
PrintYesNo:
        or a
        ld hl,TxYes
        jr nz,PrintStr
        ld hl,TxNo
        jr PrintStr

; the heading: clear the screen, the machine as detected, the run time
Header:
        ld a,(SHOW)
        or a
        ret z
        call RomOn
        call #0D6B              ; CLS (it leaves channel K, the lower screen, open)
        ld a,2
        call #1601              ; CHAN-OPEN: the upper screen
        ld hl,TxTitle
        call PrintStr
        call PrintMachine
        ld hl,TxTime
        call PrintStr
        call NewLine
        call NewLine
        jp RomOff

; "Machine: ...", frame and onset, paging (in the heading and again above the total)
PrintMachine:
        ld hl,TxMachine
        call PrintStr
        ld a,(CLASS)
        add a,a
        ld l,a
        ld h,0
        ld de,ClassNames
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        call PrintStr
        call NewLine
        ld hl,TxFrame
        call PrintStr
        ld bc,(FRAMET)          ; the length: FRAMET + 32768, up to 17 bits
        call #2D2B
        ld bc,32768
        call #2D2B
        rst #28                 ; calculator: addition, end
        db #0F, #38
        call #2DE3
        ld hl,TxOnset
        call PrintStr
        ld hl,(ONSET)
        call PrintNum
        call NewLine
        ld hl,TxPaging
        call PrintStr
        ld a,(CAPS)
        and 1
        call PrintYesNo
        ld hl,TxLayouts
        call PrintStr
        ld a,(CAPS)
        and 2
        call PrintYesNo
        call NewLine
        ret

; the current case's line: its name, then OK or its first wrong value
PrintCase:
        ld a,(SHOW)
        or a
        ret z
        call RomOn
        ld hl,CURNAME
        ld b,5
        call PrintN
        ld a,(CASEBAD)
        or a
        jr nz,CaseBadLine
        ld hl,TxOk
        call PrintStr
        jr CaseLineEnd
CaseBadLine:
        ld hl,TxBadT
        call PrintStr
        ld hl,(BADT)
        call PrintNum
        ld hl,TxGot
        call PrintStr
        ld a,(BADGOT)
        ld l,a
        ld h,0
        call PrintNum
        ld hl,TxExp
        call PrintStr
        ld a,(BADEXP)
        ld l,a
        ld h,0
        call PrintNum
CaseLineEnd:
        call NewLine
        jp RomOff

; a case that does not run on this machine (the ONLY filter prints nothing)
PrintSkip:
        ld a,(SHOW)
        or a
        ret z
        ld a,(ONLY)
        or a
        ret nz
        call RomOn
        ld hl,CURNAME
        ld b,5
        call PrintN
        ld hl,TxSkip
        call PrintStr
        call NewLine
        jp RomOff

; the total, and the border: green all as expected, red otherwise
PrintSummary:
        ld a,(SHOW)
        or a
        ret z
        call RomOn
        call NewLine
        call PrintMachine
        ld hl,(FAILS)
        ld a,h
        or l
        jr nz,SummaryBad
        ld hl,TxAllOk
        call PrintStr
        call NewLine
        ld a,4
        out (#FE),a
        jp RomOff
SummaryBad:
        call PrintNum
        ld hl,TxWrong
        call PrintStr
        call NewLine
        ld a,2
        out (#FE),a
        jp RomOff

TxTitle:   db "ctprobe 1 - contention probe", 13, 0
TxMachine: db "Machine: ", 0
TxFrame:   db "Frame ", 0
TxOnset:   db ", onset ", 0
TxPaging:  db "Paging ", 0
TxLayouts: db ", +3 layouts ", 0
TxYes:     db "yes", 0
TxNo:      db "no", 0
TxTime:    db "Takes about 3 min at 3.5 MHz", 0
TxOk:      db " OK", 0
TxBadT:    db " BAD T", 0
TxGot:     db " got ", 0
TxExp:     db " exp ", 0
TxSkip:    db " skipped as N/A", 0
TxAllOk:   db "ALL VALUES AS EXPECTED", 0
TxWrong:   db " VALUES WRONG", 0
ClassNames:
        dw TxUla48, TxUla128, TxGate, TxNone, TxScorpion
TxUla48:   db "ULA 48K", 0
TxUla128:  db "ULA 128K", 0
TxGate:    db "gate array", 0
TxNone:    db "no contention", 0
TxScorpion: db "no contention, attr bus", 0

; ---- cases ----
; id, flags, page at #C000 (#FF = leave), mirror page (#FF = none), target (0 = FRAGBUF), fragment, length,
; offset from ONSET (signed), count, results, name (5 characters)

CASES:
        db 1, 0, #FF, #FF       ; NOP at #4000 over two cells
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw -2
        db 20
        dw R01
        db "M1-01"
        db 2, 0, #FF, #FF       ; NOP at #4000 in the top border
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw -2000
        db 4
        dw R02
        db "M1-02"
        db 3, 0, #FF, #FF       ; NOP at #4000 over the end of the first line's fetch window
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw 118
        db 14
        dw R03
        db "M1-03"
        db 4, 0, #FF, #FF       ; LD A,n: opcode at #7FFF, operand at #8000
        dw #7FFF, FrLdAn
        db FrLdAnEnd-FrLdAn
        dw -2
        db 12
        dw R04
        db "M1-04"
        db 5, 0, #FF, #FF       ; RLC B (CB prefix) at #4000
        dw #4000, FrCb
        db FrCbEnd-FrCb
        dw -2
        db 12
        dw R05
        db "M1-5A"
        db 6, 0, #FF, #FF       ; NEG (ED prefix) at #4000
        dw #4000, FrEd
        db FrEdEnd-FrEd
        dw -2
        db 12
        dw R06
        db "M1-5B"
        db 7, 0, #FF, #FF       ; INC IX / DEC IX (DD prefix) at #4000
        dw #4000, FrDd
        db FrDdEnd-FrDd
        dw -2
        db 12
        dw R07
        db "M1-5C"
        db 8, 0, #FF, #FF       ; RLC (IX+0) at #4000, IX uncontended
        dw #4000, FrDdcb
        db FrDdcbEnd-FrDdcb
        dw -2
        db 12
        dw R08
        db "M1-06"
        db 9, 0, #FF, #FF       ; DD DD DD NOP at #4000
        dw #4000, FrDdDd
        db FrDdDdEnd-FrDdDd
        dw -2
        db 12
        dw R09
        db "M1-07"
        db 10, 1, 0, #FF        ; RET at #C000, pages 0 .. 7
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R10
        db "M1-P0"
        db 11, 1, 1, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R11
        db "M1-P1"
        db 12, 1, 2, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R12
        db "M1-P2"
        db 13, 1, 3, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R13
        db "M1-P3"
        db 14, 1, 4, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R14
        db "M1-P4"
        db 15, 1, 5, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R15
        db "M1-P5"
        db 16, 1, 6, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R16
        db "M1-P6"
        db 17, 1, 7, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R17
        db "M1-P7"
        db 18, 2, #FF, 6        ; +3 layouts 0 .. 3, switched by the fragment: code in the #8000 slot, reads of
        dw 0, FrLayout0         ; #0000 and #C000
        db FrLayout0End-FrLayout0
        dw -40
        db 12
        dw R18
        db "M1-L0"
        db 19, 2, #FF, 6
        dw 0, FrLayout1
        db FrLayout1End-FrLayout1
        dw -40
        db 12
        dw R19
        db "M1-L1"
        db 20, 2, #FF, 6
        dw 0, FrLayout2
        db FrLayout2End-FrLayout2
        dw -40
        db 12
        dw R20
        db "M1-L2"
        db 21, 2, #FF, 6
        dw 0, FrLayout3
        db FrLayout3End-FrLayout3
        dw -40
        db 12
        dw R21
        db "M1-L3"
        db 30, 0, #FF, #FF      ; LD A,(#4000) from uncontended code
        dw 0, FrLdAMem
        db FrLdAMemEnd-FrLdAMem
        dw -12
        db 20
        dw R30
        db "D-01A"
        db 31, 0, #FF, #FF      ; LD (#4000),A from uncontended code
        dw 0, FrLdMemA
        db FrLdMemAEnd-FrLdMemA
        dw -12
        db 20
        dw R31
        db "D-01B"
        db 32, 0, #FF, #FF      ; LD A,(#4000) placed at #4000
        dw #4000, FrLdAMem
        db FrLdAMemEnd-FrLdAMem
        dw -2
        db 16
        dw R32
        db "D-02 "
        db 33, 0, #FF, #FF      ; PUSH BC / POP BC with SP in contended RAM
        dw 0, FrPushPop
        db FrPushPopEnd-FrPushPop
        dw -30
        db 20
        dw R33
        db "D-03 "
        db 34, 0, #FF, #FF      ; LDI, source and destination in contended RAM
        dw 0, FrLdi
        db FrLdiEnd-FrLdi
        dw -40
        db 20
        dw R34
        db "D-04 "
        db 40, 0, #FF, #FF      ; INC (HL), HL = #4000
        dw 0, FrIncHl
        db FrIncHlEnd-FrIncHl
        dw -14
        db 20
        dw R40
        db "N-01 "
        db 41, 0, #FF, #FF      ; JR +0 at #4000
        dw #4000, FrJr
        db FrJrEnd-FrJr
        dw -2
        db 16
        dw R41
        db "N-02 "
        db 42, 0, #FF, #FF      ; EX (SP),HL with SP in contended RAM
        dw 0, FrExSp
        db FrExSpEnd-FrExSp
        dw -30
        db 20
        dw R42
        db "N-03 "
        db 43, 0, #FF, #FF      ; ADD HL,BC with I = #40 (IR on the bus)
        dw 0, FrIrAdd
        db FrIrAddEnd-FrIrAdd
        dw -12
        db 16
        dw R43
        db "N-04 "
        db 44, 0, #FF, #FF      ; LDIR, two bytes: the repeat's internal cycles on DE
        dw 0, FrLdir
        db FrLdirEnd-FrLdir
        dw -40
        db 20
        dw R44
        db "N-05A"
        db 45, 0, #FF, #FF      ; CPIR, two bytes, no match: the repeat's internal cycles on HL
        dw 0, FrCpir
        db FrCpirEnd-FrCpir
        dw -60
        db 20
        dw R45
        db "N-05B"
        db 46, 0, #FF, #FF      ; LD A,(IX+0) at #4000: the internal cycles on PC
        dw #4000, FrIxd
        db FrIxdEnd-FrIxd
        dw -2
        db 16
        dw R46
        db "N-06 "
        db 50, 0, #FF, #FF      ; IN A,(C), BC = #00FE
        dw 0, FrIn00FE
        db FrIn00FEEnd-FrIn00FE
        dw -18
        db 16
        dw R50
        db "P-01A"
        db 51, 0, #FF, #FF      ; IN A,(C), BC = #40FE
        dw 0, FrIn40FE
        db FrIn40FEEnd-FrIn40FE
        dw -18
        db 16
        dw R51
        db "P-01B"
        db 52, 0, #FF, #FF      ; IN A,(C), BC = #00FF
        dw 0, FrIn00FF
        db FrIn00FFEnd-FrIn00FF
        dw -18
        db 16
        dw R52
        db "P-01C"
        db 53, 0, #FF, #FF      ; IN A,(C), BC = #40FF
        dw 0, FrIn40FF
        db FrIn40FFEnd-FrIn40FF
        dw -18
        db 16
        dw R53
        db "P-01D"
        db 54, 0, #FF, #FF      ; OUT (C),A, BC = #40FE (A = 0: black border, silent)
        dw 0, FrOut40FE
        db FrOut40FEEnd-FrOut40FE
        dw -18
        db 16
        dw R54
        db "P-01E"
        db 55, 20, #FF, #FF     ; the byte IN A,(#FF) reads, its I/O cycle from 2 T before the onset
        dw 0, FrFloat
        db FrFloatEnd-FrFloat
        dw -13
        db 20
        dw R55
        db "P-02 "
        db 60, 8, #FF, #FF      ; RET in ROM
        dw 0, FrNop
        db 0
        dw -2
        db 8
        dw R60
        db "X-02 "
        db 0

; ---- fragments ----

FrNop:
        nop
FrNopEnd:
FrLdAn:
        ld a,0
FrLdAnEnd:
FrCb:
        rlc b
FrCbEnd:
FrEd:
        neg
FrEdEnd:
FrDd:
        inc ix
        dec ix
FrDdEnd:
FrDdcb:
        rlc (ix+0)
FrDdcbEnd:
FrDdDd:
        db #DD, #DD, #DD, #00
FrDdDdEnd:
FrLayout0:                      ; runs at FRAGBUF: the code after the first OUT comes from the #8000 slot's page
        ld a,(DEF1FFD)
        ld l,a
        and #F8
        or #01                  ; special mode, layout 0: pages 0, 1, 2, 3
        ld bc,#1FFD
        out (c),a
        nop
        ld a,(#0000)
        ld a,(#C000)
        ld a,l
        out (c),a
FrLayout0End:
FrLayout1:
        ld a,(DEF1FFD)
        ld l,a
        and #F8
        or #03                  ; layout 1: pages 4, 5, 6, 7
        ld bc,#1FFD
        out (c),a
        nop
        ld a,(#0000)
        ld a,(#C000)
        ld a,l
        out (c),a
FrLayout1End:
FrLayout2:
        ld a,(DEF1FFD)
        ld l,a
        and #F8
        or #05                  ; layout 2: pages 4, 5, 6, 3
        ld bc,#1FFD
        out (c),a
        nop
        ld a,(#0000)
        ld a,(#C000)
        ld a,l
        out (c),a
FrLayout2End:
FrLayout3:
        ld a,(DEF1FFD)
        ld l,a
        and #F8
        or #07                  ; layout 3: pages 4, 7, 6, 3
        ld bc,#1FFD
        out (c),a
        nop
        ld a,(#0000)
        ld a,(#C000)
        ld a,l
        out (c),a
FrLayout3End:
FrLdAMem:
        ld a,(#4000)
FrLdAMemEnd:
FrLdMemA:
        ld (#4000),a
FrLdMemAEnd:
FrPushPop:
        ld (SPSAVE),sp
        ld sp,#4100
        push bc
        pop bc
        ld sp,(SPSAVE)
FrPushPopEnd:
FrLdi:                          ; DE is the engine's: kept on the stack
        push de
        ld hl,#4000
        ld de,#4100
        ld bc,1
        ldi
        pop de
FrLdiEnd:
FrIncHl:
        ld hl,#4000
        inc (hl)
FrIncHlEnd:
FrJr:
        db #18, #00             ; JR +0: relative, so raw bytes (the fragment runs away from where it is assembled)
FrJrEnd:
FrExSp:
        ld (SPSAVE),sp
        ld sp,#4100
        ex (sp),hl
        ld sp,(SPSAVE)
FrExSpEnd:
FrIrAdd:
        ld a,#40
        ld i,a
        add hl,bc
        ld a,#BE                ; the engine's IM2 table page back
        ld i,a
FrIrAddEnd:
FrLdir:
        push de
        ld hl,#4000
        ld de,#4100
        ld bc,2
        ldir
        pop de
FrLdirEnd:
FrCpir:
        ld hl,0
        ld (#4000),hl           ; two zero bytes: A = #AA never matches
        ld hl,#4000
        ld bc,2
        ld a,#AA
        cpir
FrCpirEnd:
FrIxd:
        ld a,(ix+0)
FrIxdEnd:
FrIn00FE:
        ld bc,#00FE
        in a,(c)
FrIn00FEEnd:
FrIn40FE:
        ld bc,#40FE
        in a,(c)
FrIn40FEEnd:
FrIn00FF:
        ld bc,#00FF
        in a,(c)
FrIn00FFEnd:
FrIn40FF:
        ld bc,#40FF
        in a,(c)
FrIn40FFEnd:
FrOut40FE:
        xor a
        ld bc,#40FE
        out (c),a
FrOut40FEEnd:
FrFloat:
        xor a
        in a,(#FF)
        ld (CTVALUE),a
FrFloatEnd:

; ---- buffers ----

FRAGBUF:   ds 40

RESULTS:
R01:       ds 20
R02:       ds 4
R03:       ds 14
R04:       ds 12
R05:       ds 12
R06:       ds 12
R07:       ds 12
R08:       ds 12
R09:       ds 12
R10:       ds 10
R11:       ds 10
R12:       ds 10
R13:       ds 10
R14:       ds 10
R15:       ds 10
R16:       ds 10
R17:       ds 10
R18:       ds 12
R19:       ds 12
R20:       ds 12
R21:       ds 12
R30:       ds 20
R31:       ds 20
R32:       ds 16
R33:       ds 20
R34:       ds 20
R40:       ds 20
R41:       ds 16
R42:       ds 20
R43:       ds 16
R44:       ds 20
R45:       ds 20
R46:       ds 16
R50:       ds 16
R51:       ds 16
R52:       ds 16
R53:       ds 16
R54:       ds 16
R55:       ds 20
R60:       ds 8
RESULTSEND:

EXPECTED:  ds 5*(RESULTSEND-RESULTS)     ; per CLASS, laid out as RESULTS (filled by the generator)
PROBEEND:
