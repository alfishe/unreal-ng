; ctprobe - contention probe (docs/inprogress/2026-09-28-m1-contention/test-programs.md, section 3)
;
; Runs every case of the table below with the Bobrowski / Rak measuring engine (engine.asm, appended by the
; loader after this file): each case places a code fragment at an address, maps a page if it needs one, and
; times the fragment starting at COUNT consecutive frame T-states from ONSET + OFFSET. A duration is the
; fragment's alone plus whatever the trailing RET (placed right after the fragment) waits; the engine
; subtracts the RET's 10 T.
;
; Loads and runs at 40000 (uncontended on every machine; the engine uses #BE00-#BFC2 for its IM2 table and
; handler). Standalone: USR 40000 from 48 BASIC. A host may patch ONSET, CAPS and ONLY first, start at
; HOSTENTRY with SP below #BE00, and wait for DONE.

        org 40000

START:
        jp MAIN
HOSTENTRY:
        call MAIN
HostSpin:
        jr HostSpin

ONSET:  dw 0            ; first contended T-state (INT-relative); 0 = from the frame length
CAPS:   db 0            ; bit 0: #7FFD paging, bit 1: +2A/+3 #1FFD layouts; case flags must be a subset
ONLY:   db 0            ; run only this case id (0 = all)
DEF7FFD: db #10         ; #7FFD outside the paging cases (ROM 1 = 48 BASIC, page 0); a case ORs its page in
DEF1FFD: db #04         ; #1FFD after a layout case (48 BASIC ROM on the +2A/+3)
DONE:   db 0            ; 1 when every case has run

; ---- current case (a copy of its record) ----

CUR:
CURID:     db 0
CURFLAGS:  db 0
CUR7FFD:   db 0
CUR1FFD:   db 0
CURTARGET: dw 0
CURSRC:    dw 0
CURLEN:    db 0
CUROFS:    dw 0
CURCOUNT:  db 0
CURRES:    dw 0
CUREND:
RECLEN     equ CUREND-CUR

CASEPTR:   dw 0
CURT:      dw 0
SPSAVE:    dw 0
IXDATA:    db 0, 0

MAIN:
        call INSTINT
        call FRAMETIME
        ld hl,(ONSET)
        ld a,h
        or l
        jr nz,HaveOnset
        ld hl,(FRAMET)          ; frame length - 32768
        ld de,69888-32768
        or a
        sbc hl,de
        ld hl,14335             ; 48K / Scorpion frame
        jr z,SetOnset
        ld hl,14361             ; 128K / +2 / +2A / +3 (and the clones, which do not contend)
SetOnset:
        ld (ONSET),hl
HaveOnset:
        ld hl,CASES
CaseLoop:
        ld (CASEPTR),hl
        ld a,(hl)
        or a
        jp z,Finish
        ld de,CUR
        ld bc,RECLEN
        ldir

        ld a,(ONLY)             ; case filter
        or a
        jr z,CheckCaps
        ld b,a
        ld a,(CURID)
        cp b
        jp nz,NextCase
CheckCaps:
        ld a,(CAPS)
        cpl
        ld b,a
        ld a,(CURFLAGS)
        and b
        jp nz,NextCase

        ld a,(CUR7FFD)          ; paging: the page at #C000 into the default #7FFD value
        cp #FF
        jr z,No7FFD
        ld b,a
        ld a,(DEF7FFD)
        and #F8
        or b
        ld bc,#7FFD
        out (c),a
No7FFD:
        ld a,(CUR1FFD)
        cp #FF
        jr z,No1FFD
        ld bc,#1FFD
        out (c),a
No1FFD:

        ld de,(CURTARGET)       ; place the fragment and its RET (target 0 = FRAGBUF, uncontended)
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

        ld hl,(ONSET)           ; first T-state
        ld de,(CUROFS)
        add hl,de
        ld (CURT),hl

TimeLoop:
        ld ix,IXDATA            ; the (IX+d) fragments address uncontended data
        ld hl,(CURT)
        inc hl                  ; CODETIME counts one more: Rak's 48K grid shows the first wait at 14336
        ld de,(CURTARGET)
        call CODETIME
        di                      ; the page mapped may not hold a ROM's IM1 handler
        ld de,(CURRES)
        ld a,l
        ld (de),a
        inc de
        ld (CURRES),de
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
        jr z,Restored7FFD
        ld a,(DEF7FFD)
        ld bc,#7FFD
        out (c),a
Restored7FFD:
        ld a,(CUR1FFD)
        cp #FF
        jr z,Restored1FFD
        ld a,(DEF1FFD)
        ld bc,#1FFD
        out (c),a
Restored1FFD:
        ei

NextCase:
        ld hl,(CASEPTR)
        ld de,RECLEN
        add hl,de
        jp CaseLoop

Finish:
        ld a,1
        ld (DONE),a
        ret

; ---- cases ----
; id, flags (bit 0 #7FFD, bit 1 #1FFD), page at #C000 (#FF = leave), #1FFD (#FF = leave),
; target (0 = FRAGBUF), fragment, length, offset from ONSET (signed), count, results

CASES:
        db 1, 0, #FF, #FF       ; M1-01: NOP at #4000 over two cells
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw -2
        db 20
        dw R01
        db 2, 0, #FF, #FF       ; M1-02: NOP at #4000 in the top border
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw -2000
        db 4
        dw R02
        db 3, 0, #FF, #FF       ; M1-03: NOP at #4000 over the end of the first line's fetch window
        dw #4000, FrNop
        db FrNopEnd-FrNop
        dw 118
        db 14
        dw R03
        db 4, 0, #FF, #FF       ; M1-04: LD A,n, opcode at #7FFF, operand at #8000
        dw #7FFF, FrLdAn
        db FrLdAnEnd-FrLdAn
        dw -2
        db 12
        dw R04
        db 5, 0, #FF, #FF       ; M1-05: RLC B (CB prefix) at #4000
        dw #4000, FrCb
        db FrCbEnd-FrCb
        dw -2
        db 12
        dw R05
        db 6, 0, #FF, #FF       ; M1-05: NEG (ED prefix) at #4000
        dw #4000, FrEd
        db FrEdEnd-FrEd
        dw -2
        db 12
        dw R06
        db 7, 0, #FF, #FF       ; M1-05: INC IX / DEC IX (DD prefix) at #4000
        dw #4000, FrDd
        db FrDdEnd-FrDd
        dw -2
        db 12
        dw R07
        db 8, 0, #FF, #FF       ; M1-06: RLC (IX+0) at #4000, IX uncontended
        dw #4000, FrDdcb
        db FrDdcbEnd-FrDdcb
        dw -2
        db 12
        dw R08
        db 9, 0, #FF, #FF       ; M1-07: DD DD DD NOP at #4000
        dw #4000, FrDdDd
        db FrDdDdEnd-FrDdDd
        dw -2
        db 12
        dw R09
        db 10, 1, 0, #FF      ; M1-08: RET at #C000, page 0 .. 7
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R10
        db 11, 1, 1, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R11
        db 12, 1, 2, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R12
        db 13, 1, 3, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R13
        db 14, 1, 4, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R14
        db 15, 1, 5, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R15
        db 16, 1, 6, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R16
        db 17, 1, 7, #FF
        dw #C000, FrNop
        db 0
        dw -2
        db 10
        dw R17
        db 20, 0, #FF, #FF      ; D-01: LD A,(#4000) from uncontended code
        dw 0, FrLdAMem
        db FrLdAMemEnd-FrLdAMem
        dw -12
        db 20
        dw R20
        db 21, 0, #FF, #FF      ; D-01: LD (#4000),A from uncontended code
        dw 0, FrLdMemA
        db FrLdMemAEnd-FrLdMemA
        dw -12
        db 20
        dw R21
        db 22, 0, #FF, #FF      ; D-02: LD A,(#4000) placed at #4000
        dw #4000, FrLdAMem
        db FrLdAMemEnd-FrLdAMem
        dw -2
        db 16
        dw R22
        db 23, 0, #FF, #FF      ; D-03: PUSH BC / POP BC with SP in contended RAM
        dw 0, FrPushPop
        db FrPushPopEnd-FrPushPop
        dw -30
        db 20
        dw R23
        db 30, 0, #FF, #FF      ; N-01: INC (HL), HL = #4000
        dw 0, FrIncHl
        db FrIncHlEnd-FrIncHl
        dw -14
        db 20
        dw R30
        db 31, 0, #FF, #FF      ; N-02: JR +0 at #4000
        dw #4000, FrJr
        db FrJrEnd-FrJr
        dw -2
        db 16
        dw R31
        db 32, 0, #FF, #FF      ; N-04: ADD HL,BC with I = #40 (IR on the bus)
        dw 0, FrIrAdd
        db FrIrAddEnd-FrIrAdd
        dw -12
        db 16
        dw R32
        db 40, 0, #FF, #FF      ; P-01: IN A,(C), BC = #00FE
        dw 0, FrIn00FE
        db FrIn00FEEnd-FrIn00FE
        dw -18
        db 16
        dw R40
        db 41, 0, #FF, #FF      ; P-01: IN A,(C), BC = #40FE
        dw 0, FrIn40FE
        db FrIn40FEEnd-FrIn40FE
        dw -18
        db 16
        dw R41
        db 42, 0, #FF, #FF      ; P-01: IN A,(C), BC = #00FF
        dw 0, FrIn00FF
        db FrIn00FFEnd-FrIn00FF
        dw -18
        db 16
        dw R42
        db 43, 0, #FF, #FF      ; P-01: IN A,(C), BC = #40FF
        dw 0, FrIn40FF
        db FrIn40FFEnd-FrIn40FF
        dw -18
        db 16
        dw R43
        db 44, 0, #FF, #FF      ; P-01: OUT (C),A, BC = #40FE (A = 0: black border, silent)
        dw 0, FrOut40FE
        db FrOut40FEEnd-FrOut40FE
        dw -18
        db 16
        dw R44
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
FrIncHl:
        ld hl,#4000
        inc (hl)
FrIncHlEnd:
FrJr:
        db #18, #00             ; JR +0: relative, so raw bytes (the fragment runs away from where it is assembled)
FrJrEnd:
FrIrAdd:
        ld a,#40
        ld i,a
        add hl,bc
        ld a,#BE                ; the engine's IM2 table page back
        ld i,a
FrIrAddEnd:
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

; ---- buffers ----

FRAGBUF:   ds 32

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
R20:       ds 20
R21:       ds 20
R22:       ds 16
R23:       ds 20
R30:       ds 20
R31:       ds 16
R32:       ds 16
R40:       ds 16
R41:       ds 16
R42:       ds 16
R43:       ds 16
R44:       ds 16
RESULTSEND:
