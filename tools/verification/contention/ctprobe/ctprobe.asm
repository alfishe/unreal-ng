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
                        ; 4 no contention, and unused ports read the attribute being fetched (the Scorpion)
CAPS:    db #FF         ; bit 0: #7FFD paging works, bit 1: +2A/+3 #1FFD layouts
DEF7FFD: db #FF         ; #7FFD outside the paging cases; a case ORs its page in
DEF1FFD: db #FF         ; #1FFD outside the layout cases
ONLY:    db 0           ; run only this case id (0 = all)
SHOW:    db 1           ; print the report

; ---- outputs ----

DONE:    db 0           ; 1 when finished
FAILS:   dw 0           ; values that differ from the expected table
TOOFAST: db 0           ; 1: the CPU runs faster than 3.5 MHz, nothing was measured
EVENM1:  db 0           ; 1: opcode fetches from RAM start on even T-states only (the Scorpion's "Even M1"),
                        ; nothing was measured: the engine's delays need single T-state steps

; ---- current case (a copy of its record) ----

CUR:
CURID:     db 0
CURFLAGS:  db 0         ; bit 0 page at #C000, bit 1 +3 layout, bit 2 store A (CTVALUE) not the duration,
                        ; bit 3 time the RET found in ROM, bit 4 fill the first screen cells first,
                        ; bit 5 not on the plain clones (CLASS 3): what an unused port reads there depends on the
                        ; machine and its mode (an ATM's disk ports stay open outside TR-DOS in one mode)
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
MAXWAIT:   db 0
PROBEI:    db 0
ROMRET:    dw 0
CASEBAD:   db 0
FIRSTBAD:  dw 0         ; record of the first case with a wrong value (0 = none)
BADT:      dw 0
BADGOT:    db 0
BADEXP:    db 0

; ---- main ----

MAIN:
        call TurboOff
        jp z,TooFast
        call IsEvenM1
        jp nz,EvenM1
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

; The CPU runs faster than 3.5 MHz and could not be switched: nothing is measured
TooFast:
        ld a,1
        ld (TOOFAST),a
        ld hl,TxTooFast
        jr NotMeasured

; Opcode fetches wait for an even T-state: the engine cannot place code at every T-state
EvenM1:
        ld a,1
        ld (EVENM1),a
        ld hl,TxEvenM1
NotMeasured:
        ld a,(SHOW)
        or a
        jr z,NotMeasuredDone
        push hl
        call #0D6B              ; CLS
        ld a,2
        call #1601              ; CHAN-OPEN: the upper screen
        ld hl,TxTitle
        call PrintStr
        pop hl
        call PrintStr
        ld a,2                  ; red border
        out (#FE),a
NotMeasuredDone:
        ld a,1
        ld (DONE),a
        ld bc,#FFFF
        ret

Finish:
        call PrintSummary
        ld a,1
        ld (DONE),a
        ld bc,(FAILS)
        ret

RESPTR: dw 0

; ---- helpers ----

; A Scorpion's ROM leaves the CPU at 7 MHz, and the engine needs the INT pulse over before its handler returns,
; as it is at 3.5 MHz. A read of #1FFD selects 3.5 MHz on the Scorpion - but a 128K / +2 decodes that read as
; #7FFD and latches the bus value (#FF: paging locked, screen 7), so the read is done only when the CPU is
; found fast: 80000 T after an interrupt the ROM's frame counter (FRAMES) has moved at 3.5 MHz on every
; machine (frames of 69888-71680 T) and has not at 7 MHz (twice as many T per frame)
; Returns NZ at 3.5 MHz, Z when still fast (ATM Turbo and ZX-Evo switch through ports whose value the probe does
; not know, so it does not try: the report asks for 3.5 MHz instead)
TurboOff:
        call IsFast
        ret nz                  ; 3.5 MHz
        ld bc,#1FFD
        in a,(c)
IsFast:                         ; Z if FRAMES did not move in 80000 T
        ei
        halt
        ld a,(#5C78)            ; FRAMES
        ld (TurboFrames),a
        ld bc,40000
        call DELAY
        ld bc,40000
        call DELAY
        ld a,(#5C78)
        ld hl,TurboFrames
        cp (hl)
        ret
TurboFrames: db 0

; NZ if opcode fetches from RAM wait for an even T-state (the Scorpion's "Even M1": a fetch that would start on
; an odd T-state waits one). After a HALT the loop below runs 65931 T, less than any frame with the interrupt
; routine; with the waits its 7 T loads and 13 T DJNZ each take one more, 73236 T, longer than any frame, and
; the next interrupt moves FRAMES. HL points at #8000, uncontended on every machine.
; To drop the check: remove this routine, its call in MAIN, EvenM1, EVENM1 and TxEvenM1.
IsEvenM1:
        ei
        halt
        ld a,(#5C78)            ; FRAMES
        ld (TurboFrames),a
        ld hl,#8000
        ld bc,134*256+10        ; 134 rounds, then 9 x 256
EvenM1Loop:
        ld a,(hl)               ; 7
        ld a,(hl)               ; 7
        djnz EvenM1Loop         ; 13
        dec c
        jr nz,EvenM1Loop
        ld a,(#5C78)
        ld hl,TurboFrames
        cp (hl)
        ret

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

; CLASS from the frame length and the largest wait of a NOP at #7FFF (its RET at #8000 never waits) over the 16
; T-states from the onset: 0 no contention, 6 the Ferranti ULA (6,5,4,3,2,1,0,0), 7 the gate array (1,0,7,6,...).
; The largest wait tells the two apart even when a machine's timing is a few T-states off, which one exact
; value would not. 48K or 128K from the frame length
DetectClass:
        ld hl,#7FFF
        ld (hl),0
        inc hl
        ld (hl),#C9
        xor a
        ld (MAXWAIT),a
        ld (PROBEI),a
ClassLoop:
        ld hl,(ONSET)
        ld a,(PROBEI)
        ld e,a
        ld d,0
        add hl,de
        inc hl                  ; CODETIME counts one more
        ld de,#7FFF
        call CODETIME
        ld a,l
        sub 4                   ; the NOP's own 4 T
        ld hl,MAXWAIT
        cp (hl)
        jr c,ClassNext
        ld (hl),a
ClassNext:
        ld hl,PROBEI
        inc (hl)
        ld a,(hl)
        cp 16
        jr c,ClassLoop
        ld a,(MAXWAIT)
        or a
        jr z,ClassNone
        call IsFrame48          ; Z: 69888 T
        ld a,0                  ; 48K
        jr z,SetClass
        ld a,(MAXWAIT)
        cp 7
        ld a,2                  ; gate array
        jr nc,SetClass
        ld a,1                  ; 128K
SetClass:
        ld (CLASS),a
        ret

; No contention: what does an unused port read in the picture area? The first screen cells get a pattern and
; IN A,(#FF) runs with its I/O cycle 5 T after the onset. One of the pattern's attributes: the Scorpion's attribute
; bus (class 4). Anything else (#FF, or a byte from a device that answers the port): an ordinary clone (class 3). Measured, not guessed from the frame length, which the
; Scorpion shares with the 48K, the ATM and the Profi
ClassNone:
        call FillCells
        ld hl,FrFloat           ; XOR A / IN A,(#FF) / LD (CTVALUE),A, at FRAGBUF
        ld de,FRAGBUF
        ld bc,FrFloatEnd-FrFloat
        ldir
        ld a,#C9
        ld (de),a
        ld hl,(ONSET)
        ld de,5-11+1            ; the I/O cycle is 11 T in; CODETIME counts one more
        add hl,de
        ld de,FRAGBUF
        call CODETIME
        ld a,(CTVALUE)          ; one of the pattern's attributes (#40-#5F): the attribute bus
        and #E0
        cp #40
        ld a,4                  ; no contention, attribute bus
        jr z,SetClass
        ld a,3                  ; no contention (#FF, or a device that answers the port)
        jr SetClass

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
        ld a,(CURFLAGS)
        and 32
        jr z,CheckPaging
        ld a,(CLASS)
        cp 3
        jr nz,CheckPaging
        or a                    ; NZ: not here
        ret
CheckPaging:
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
TxEvenM1:  db "Opcode fetches wait for even", 13, "T-states (Scorpion Even M1).", 13, "This version cannot time code", 13, "on it: nothing was measured.", 13, 0
TxTooFast: db "The CPU runs faster than 3.5 MHz.", 13, "Switch the machine to 3.5 MHz", 13, "(turbo off) and run it again.", 13, 0
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

; ---- P-03: more port instructions (the four FUSE patterns through other instructions) ----
; Each record below is independent: to drop a check, delete its record, its fragment (FrXxx..FrXxxEnd) and its
; results buffer (Rnn); to drop the group, delete all three parts of P-03. The oracle (ctprobe_test.cpp,
; Fragment(), case ids 61-65) describes each fragment's bus cycles and must follow any change here.
        db 61, 0, #FF, #FF      ; OUT (C),A, BC = #00FE: ULA port, high byte uncontended (N:1 C:3)
        dw 0, FrOut00FE
        db FrOut00FEEnd-FrOut00FE
        dw -18
        db 16
        dw R61
        db "P-03A"
        db 62, 0, #FF, #FF      ; OUT (C),A, BC = #00FF: other port, high byte uncontended (N:4)
        dw 0, FrOut00FF
        db FrOut00FFEnd-FrOut00FF
        dw -18
        db 16
        dw R62
        db "P-03B"
        db 63, 0, #FF, #FF      ; OUT (C),A, BC = #40FF: other port, high byte contended (C:1 x4)
        dw 0, FrOut40FF
        db FrOut40FFEnd-FrOut40FF
        dw -18
        db 16
        dw R63
        db "P-03C"
        db 64, 0, #FF, #FF      ; IN A,(#FE) with A = #40: the port is A:n = #40FE (C:1 C:3)
        dw 0, FrInAn
        db FrInAnEnd-FrInAn
        dw -18
        db 16
        dw R64
        db "P-03D"
        db 65, 0, #FF, #FF      ; OUT (#FE),A with A = #40: port #40FE (C:1 C:3); writes border 0, MIC/EAR 0
        dw 0, FrOutAn
        db FrOutAnEnd-FrOutAn
        dw -18
        db 16
        dw R65
        db "P-03E"

; ---- P-04: block I/O (INI / OUTI / INIR / OTIR) ----
; FUSE's order: INI and INIR read the port at BC, then write (HL); OUTI and OTIR read (HL), decrement B, then
; write the port at the new BC. A repeat adds 5 internal ticks: on HL (INIR) or on the new BC (OTIR). Oracle:
; case ids 66-69. The repeats use B = 2 / 3, so the ports stay uncontended; the ULA port's C:3 still applies.
        db 66, 0, #FF, #FF      ; INI, BC = #40FE: IR tick, port #40FE (C:1 C:3), write to INBUF
        dw 0, FrIni
        db FrIniEnd-FrIni
        dw -24
        db 16
        dw R66
        db "P-04A"
        db 67, 0, #FF, #FF      ; OUTI, BC = #41FE: B becomes #40 before the write, so the port is #40FE
        dw 0, FrOuti
        db FrOutiEnd-FrOuti
        dw -24
        db 16
        dw R67
        db "P-04B"
        db 68, 0, #FF, #FF      ; INIR, BC = #02FE: ports #02FE, #01FE; the repeat's 5 ticks on HL
        dw 0, FrInir
        db FrInirEnd-FrInir
        dw -24
        db 16
        dw R68
        db "P-04C"
        db 69, 0, #FF, #FF      ; OTIR, BC = #03FE: ports #02FE, #01FE, #00FE; the repeats' 5 ticks on BC
        dw 0, FrOtir
        db FrOtirEnd-FrOtir
        dw -24
        db 16
        dw R69
        db "P-04D"

; ---- P-05: a port's high byte in the page at #C000 (128K / +2) ----
; On the 128K the ULA contends a port whose high byte points into contended memory, and at #C000 that depends on
; the page mapped there: odd pages are contended. This rule is the consensus of the emulators (FUSE); neither
; Rak's nor Butler's test covers it, so it is not yet confirmed on real hardware. On the +2A/+3 and the clones
; ports never wait. Needs #7FFD paging (flag 1). Oracle: case ids 70-73.
        db 70, 1, 0, #FF        ; IN A,(C), BC = #C0FE, page 0 at #C000: high byte uncontended (N:1 C:3)
        dw 0, FrInC0FE
        db FrInC0FEEnd-FrInC0FE
        dw -18
        db 16
        dw R70
        db "P-05A"
        db 71, 1, 1, #FF        ; IN A,(C), BC = #C0FE, page 1: high byte contended on the 128K (C:1 C:3)
        dw 0, FrInC0FE
        db FrInC0FEEnd-FrInC0FE
        dw -18
        db 16
        dw R71
        db "P-05B"
        db 72, 1, 0, #FF        ; IN A,(C), BC = #C0FF, page 0 (N:4)
        dw 0, FrInC0FF
        db FrInC0FFEnd-FrInC0FF
        dw -18
        db 16
        dw R72
        db "P-05C"
        db 73, 1, 1, #FF        ; IN A,(C), BC = #C0FF, page 1: contended on the 128K (C:1 x4)
        dw 0, FrInC0FF
        db FrInC0FFEnd-FrInC0FF
        dw -18
        db 16
        dw R73
        db "P-05D"
        db 55, 52, #FF, #FF     ; the byte IN A,(#FF) reads, its I/O cycle from 2 T before the onset
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

; P-03 fragments (records above; oracle ids 61-65)
FrOut00FE:
        xor a
        ld bc,#00FE
        out (c),a
FrOut00FEEnd:
FrOut00FF:
        xor a
        ld bc,#00FF
        out (c),a
FrOut00FFEnd:
FrOut40FF:
        xor a
        ld bc,#40FF
        out (c),a
FrOut40FFEnd:
FrInAn:
        ld a,#40
        in a,(#FE)
FrInAnEnd:
FrOutAn:
        ld a,#40
        out (#FE),a
FrOutAnEnd:

; P-04 fragments (oracle ids 66-69); INBUF takes what the ports read, OUTBUF holds zeros (border black, silent)
FrIni:
        ld hl,INBUF
        ld bc,#40FE
        ini
FrIniEnd:
FrOuti:
        ld hl,OUTBUF
        ld bc,#41FE
        outi
FrOutiEnd:
FrInir:
        ld hl,INBUF
        ld bc,#02FE
        inir
FrInirEnd:
FrOtir:
        ld hl,OUTBUF
        ld bc,#03FE
        otir
FrOtirEnd:

; P-05 fragments (oracle ids 70-73)
FrInC0FE:
        ld bc,#C0FE
        in a,(c)
FrInC0FEEnd:
FrInC0FF:
        ld bc,#C0FF
        in a,(c)
FrInC0FFEnd:

; ---- buffers ----

FRAGBUF:   ds 40
INBUF:     ds 2                 ; P-04: bytes INI / INIR read
OUTBUF:    db 0, 0, 0           ; P-04: bytes OUTI / OTIR write (zeros)

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
R61:       ds 16                ; P-03
R62:       ds 16
R63:       ds 16
R64:       ds 16
R65:       ds 16
R66:       ds 16                ; P-04
R67:       ds 16
R68:       ds 16
R69:       ds 16
R70:       ds 16                ; P-05
R71:       ds 16
R72:       ds 16
R73:       ds 16
R60:       ds 8
RESULTSEND:

EXPECTED:  ds 5*(RESULTSEND-RESULTS)     ; per CLASS, laid out as RESULTS (filled by the generator)
PROBEEND:
