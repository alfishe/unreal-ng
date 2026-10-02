; turbotest - how much work a turbo machine gets done in one frame (docs/inprogress/2026-09-29-machine-waits)
;
; A Scorpion ZS-256 Turbo+ at 7 MHz waits for its memory: the logic chip lets the CPU at the RAM only in its
; own time slots, and the rules differ between the chip's two firmwares, SC15.1 and SC15.3. This program runs
; five short pieces of code (bodies) in a loop for exactly one frame, at 3.5 MHz and in turbo, and counts how
; many times each body ran. The counts are compared with the counts the two firmwares give in unreal-ng's model
; of their equations, so the screen says which firmware the machine has, or that it matches neither.
;
; How a count is taken: HALT waits for the frame interrupt; the handler starts the loop (32 copies of the body,
; then INC DE / JP back), and the next frame's interrupt stops it. The stop handler takes the loop counter (DE)
; and the address the loop was interrupted at, which tells how many bodies of the current pass were done:
; count = DE * 32 + done. One body more or less shows.
;
; Turbo is only switched on a machine whose first NOP count is a Scorpion's, at 3.5 MHz or in turbo (its ROM
; leaves turbo on): on a 128K or a grey +2 reading port #7FFD or #1FFD, the Scorpion's turbo switches, can change
; the memory paging.
;
; Loads and runs at 36000. Standalone: CLEAR 35999, load the code, RANDOMIZE USR 36000. The expected tables are
; filled in by the generator (core/tests/emulator/memory/scorpion/turbotest_test.cpp); in the plain source they
; are zero.

        org 36000

START:
        jp MAIN
HOSTENTRY:
        call MAIN
HostSpin:
        jr HostSpin

; ---- settings and outputs ----

NBODY     equ 5

SHOW:     db 1            ; print the report
FORCE:    db 0            ; 1: measure turbo whatever the first count (the generator, before the tables exist)
DONE:     db 0            ; 1 when finished
FAILS:    dw 0            ; counts that differ from the closer table; #FFFF: turbo not tried or not working
MATCH:    db 0            ; 1 SC15.1, 2 SC15.3, 0 neither, #FF turbo makes no difference, #FE not a Scorpion
TRYTURBO: db 0            ; 1: the 3.5 MHz NOP count is a Scorpion's, turbo was measured
COUNTS:   ds 30           ; NBODY * 6: per body: the 3.5 MHz count, the turbo count (24 bits each, low byte first)
COUNTSEND:

; The counts unreal-ng gives with each firmware, the same layout as COUNTS (filled in by the generator)
EXP151:   ds 30
EXP153:   ds 30

; ---- the bodies ----

COPIES    equ 32
LOOPBUF   equ #A000       ; the loop: COPIES bodies, INC DE, JP LOOPBUF
RAMDATA   equ #A800       ; what the RAM bodies read and write
ROMDATA   equ #0100       ; what the ROM read body reads
NOPTOL    equ 64          ; how far the 3.5 MHz NOP count may be from the tables' (a 128K's is ~250 more)

; per body: length, the bytes (up to 2), HL, A
BODIES:
        db 1, #00, #00          ; NOP
        dw 0
        db 0
        db 1, #7E, #00          ; LD A,(HL) from RAM
        dw RAMDATA
        db 0
        db 1, #7E, #00          ; LD A,(HL) from ROM
        dw ROMDATA
        db 0
        db 1, #77, #00          ; LD (HL),A to RAM
        dw RAMDATA
        db 0
        db 2, #D3, #FE          ; OUT (#FE),A, black border
        dw 0
        db 0
BODYSIZE  equ 6

BODY:                           ; the body being measured
BODYLEN:  db 0
BODYOP:   db 0, 0
BODYHL:   dw 0
BODYA:    db 0

SPEED:    db 0                  ; 0: 3.5 MHz, 1: turbo
SPSAVE:   dw 0
STOPPC:   dw 0
LOOPS:    dw 0
NUMBUF:   db "000000"

; ---- main ----

MAIN:
        di
        ld bc,#7FFD             ; the 48 BASIC ROM, RAM page 0 at #C000 (the report prints through the ROM)
        ld a,#10
        out (c),a
        call InstallInt

        ld hl,BODIES            ; the NOP at whatever speed the machine runs (a Scorpion's ROM leaves turbo on)
        ld de,BODY
        ld bc,BODYSIZE
        ldir
        call PlaceLoop
        ld ix,COUNTS
        xor a
        call MeasureAt
        call Guard
        call SpeedOffIfTried

        ld ix,COUNTS
        ld hl,BODIES
        ld b,NBODY
BodyLoop:
        push bc
        push hl
        ld de,BODY
        ld bc,BODYSIZE
        ldir
        call PlaceLoop
        xor a
        call MeasureAt
        ld a,(TRYTURBO)
        or a
        jr nz,BodyTurbo
        ld (ix+0),0             ; not tried: 0
        ld (ix+1),0
        ld (ix+2),0
        inc ix
        inc ix
        inc ix
        jr BodyNext
BodyTurbo:
        ld a,1
        call MeasureAt
BodyNext:
        pop hl
        ld bc,BODYSIZE
        add hl,bc
        pop bc
        djnz BodyLoop

        call SpeedOffIfTried
        im 1
        ld a,#3F
        ld i,a
        ld iy,#5C3A
        ei

        call Compare
        ld a,(SHOW)
        or a
        call nz,Report
        ld a,1
        ld (DONE),a
        ld bc,(FAILS)
        ret

; TRYTURBO = 1 when the first NOP count (COUNTS) is within NOPTOL of a table's NOP count, 3.5 MHz or turbo
Guard:
        ld a,(FORCE)
        ld (TRYTURBO),a
        ld hl,EXP151
        call NearCount
        ld hl,EXP151 + 3
        call NearCount
        ld hl,EXP153
        call NearCount
        ld hl,EXP153 + 3
NearCount:
        ld a,(COUNTS + 2)       ; the high bytes equal
        inc hl
        inc hl
        cp (hl)
        dec hl
        dec hl
        ret nz
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(COUNTS)
        or a
        sbc hl,de
        jr nc,NearPos
        ex de,hl                ; |difference|
        ld hl,0
        or a
        sbc hl,de
NearPos:
        ld de,NOPTOL + 1
        or a
        sbc hl,de
        ret nc
        ld a,1
        ld (TRYTURBO),a
        ret

; count at speed A into (IX), IX += 3
MeasureAt:
        ld (SPEED),a
        call Measure
        call Total
        ld (ix+0),l
        ld (ix+1),h
        ld (ix+2),c
        inc ix
        inc ix
        inc ix
        ret

; ---- the loop and its measurement ----

; COPIES bodies at LOOPBUF, then INC DE / JP LOOPBUF
PlaceLoop:
        ld de,LOOPBUF
        ld b,COPIES
PlaceBody:
        ld hl,BODYOP
        ld a,(BODYLEN)
        ld c,a
PlaceByte:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        dec c
        jr nz,PlaceByte
        djnz PlaceBody
        ex de,hl
        ld (hl),#13             ; INC DE
        inc hl
        ld (hl),#C3             ; JP LOOPBUF
        inc hl
        ld (hl),LOOPBUF & #FF
        inc hl
        ld (hl),LOOPBUF >> 8
        ret

; IM 2 with every vector at #BFBF, where a JP to the current handler sits
InstallInt:
        ld hl,#BE00
        ld de,#BE01
        ld bc,256
        ld (hl),#BF
        ldir
        ld a,#C3
        ld (#BFBF),a
        ld a,#BE
        ld i,a
        im 2
        ret

; run the loop for one frame at SPEED; LOOPS and STOPPC tell where it stopped
Measure:
        ld (SPSAVE),sp
        ld hl,IntSync
        ld (#BFC0),hl
        ld a,(SPEED)
        or a
        call nz,SpeedOn
        ei
        halt                    ; IntSync takes over from here

; the first interrupt: start the loop
IntSync:
        pop hl                  ; the HALT's return address is not needed
        ld hl,IntStop
        ld (#BFC0),hl
        ld de,0
        ld hl,(BODYHL)
        ld a,(BODYA)
        ei
        jp LOOPBUF

; the next one: stop it, back to Measure's caller at 3.5 MHz
IntStop:
        pop hl
        ld (STOPPC),hl
        ld (LOOPS),de
        ld sp,(SPSAVE)
        ld a,(SPEED)
        or a
        ret z
        ; turbo off: IN from #1FFD
SpeedOff:
        ld bc,#1FFD
        in a,(c)
        ret

; turbo on: the Scorpion's logic chip latches it on IN from #7FFD
SpeedOn:
        ld bc,#7FFD
        in a,(c)
        ret

SpeedOffIfTried:
        ld a,(TRYTURBO)
        or a
        ret z
        jr SpeedOff

; CHL = LOOPS * COPIES + the bodies done in the interrupted pass
Total:
        ld a,(BODYLEN)
        ld c,a
        ld b,0
        ld hl,0
        ld a,COPIES
TailAt:
        add hl,bc
        dec a
        jr nz,TailAt
        inc hl                  ; HL = the JP's offset (INC DE is one byte)
        ex de,hl
        ld hl,(STOPPC)
        push bc
        ld bc,LOOPBUF
        or a
        sbc hl,bc               ; the offset of the instruction the loop stopped at
        pop bc
        or a
        sbc hl,de
        ld a,0
        jr z,HaveDone           ; at the JP: this pass is in LOOPS already
        add hl,de
        ld e,0                  ; E = offset / length (COPIES at the INC DE)
DivBody:
        or a
        sbc hl,bc
        jr c,DivDone
        inc e
        jr DivBody
DivDone:
        ld a,e
HaveDone:
        ld hl,(LOOPS)           ; CHL = LOOPS * 32 + A
        ld c,0
        ld b,5
Times32:
        add hl,hl
        rl c
        djnz Times32
        ld e,a
        ld d,0
        add hl,de
        ld a,c
        adc a,0
        ld c,a
        ret

; ---- the verdict ----

; MATCH and FAILS from COUNTS against the two tables
Compare:
        ld hl,#FFFF
        ld (FAILS),hl
        ld a,#FE
        ld (MATCH),a
        ld a,(TRYTURBO)
        or a
        ret z
        ld a,#FF                ; turbo makes no difference: the NOP body ran no more than 64 times more
        ld (MATCH),a
        ld hl,(COUNTS + 3)
        ld de,(COUNTS)
        or a
        sbc hl,de
        ld a,(COUNTS + 2)
        ld c,a
        ld a,(COUNTS + 5)
        sbc a,c                 ; AHL = turbo - 3.5 MHz
        ret c
        jr nz,HasTurbo
        ld de,64
        sbc hl,de
        ret c
HasTurbo:
        ld hl,EXP151
        call Differ
        ld a,b
        or c
        ld a,1
        jr z,Matched
        push bc
        ld hl,EXP153
        call Differ
        pop hl                  ; HL = the SC15.1 differences, BC = the SC15.3 ones
        ld a,b
        or c
        ld a,2
        jr z,Matched
        or a                    ; neither: FAILS = the fewer differences
        sbc hl,bc
        add hl,bc
        jr c,KeepFirst
        ld h,b
        ld l,c
KeepFirst:
        ld b,h
        ld c,l
        xor a
Matched:
        ld (MATCH),a
        ld (FAILS),bc
        ret

; BC = how many counts differ between COUNTS and the table at HL
Differ:
        ld de,COUNTS
        ld bc,0
        ld a,NBODY * 2
DifferNext:
        push af
        ld a,(de)
        cp (hl)
        jr nz,Differ3
        inc de
        inc hl
        ld a,(de)
        cp (hl)
        jr nz,Differ2
        inc de
        inc hl
        ld a,(de)
        cp (hl)
        jr nz,Differ1
        jr DifferStep
Differ3:
        inc de
        inc hl
Differ2:
        inc de
        inc hl
Differ1:
        inc bc
DifferStep:
        inc de
        inc hl
        pop af
        dec a
        jr nz,DifferNext
        ret

; ---- the report ----

Report:
        call #0D6B              ; CLS
        ld a,2
        call #1601              ; CHAN-OPEN: the upper screen
        ld a,#FF
        ld (#5C8C),a            ; SCR_CT: no "scroll?"
        ld hl,TxTitle
        call PrintStr
        ld ix,COUNTS
        call PrintTable
        ld hl,TxExp151
        call PrintStr
        ld ix,EXP151
        call PrintTable
        ld hl,TxExp153
        call PrintStr
        ld ix,EXP153
        call PrintTable

        ld a,(MATCH)
        ld hl,TxNotScorp
        cp #FE
        jr z,ReportEnd
        ld hl,TxNoTurbo
        cp #FF
        jr z,ReportEnd
        ld hl,TxIs151
        cp 1
        jr z,ReportEnd
        ld hl,TxIs153
        cp 2
        jr z,ReportEnd
        ld hl,TxNeither
ReportEnd:
        jp PrintStr

; a table at IX: one line per body, "name  3.5 MHz  turbo"
PrintTable:
        ld hl,TxHead
        call PrintStr
        ld hl,BodyNames
        ld b,NBODY
PrintTableRow:
        push bc
        call PrintStr           ; the name; HL ends on its zero
        inc hl
        push hl
        call PrintCount
        call PrintCount
        ld a,13
        rst #10
        pop hl
        pop bc
        djnz PrintTableRow
        ret

; the 24-bit count at IX, a space and 6 columns right-aligned; IX += 3
PrintCount:
        ld l,(ix+0)
        ld h,(ix+1)
        ld c,(ix+2)
        inc ix
        inc ix
        inc ix
        ld de,NUMBUF + 5
        ld a,6
PrintDigit:
        push af
        call Div10
        add a,'0'
        ld (de),a
        dec de
        pop af
        dec a
        jr nz,PrintDigit
        ld hl,NUMBUF
        ld b,5
PrintBlank:
        ld a,(hl)
        cp '0'
        jr nz,PrintDigits
        ld (hl),' '
        inc hl
        djnz PrintBlank
PrintDigits:
        ld a,' '
        rst #10
        ld hl,NUMBUF
        ld b,6
PrintChar:
        ld a,(hl)
        push hl
        push bc
        rst #10
        pop bc
        pop hl
        inc hl
        djnz PrintChar
        ret

; CHL /= 10, A = the remainder
Div10:
        xor a
        ld b,24
Div10Bit:
        add hl,hl
        rl c
        rla
        cp 10
        jr c,Div10Next
        sub 10
        inc l
Div10Next:
        djnz Div10Bit
        ret

; zero-terminated string at HL; HL ends on the zero
PrintStr:
        ld a,(hl)
        or a
        ret z
        push hl
        rst #10
        pop hl
        inc hl
        jr PrintStr

TxTitle:   db "turbotest: bodies per frame", 13, 13, "Measured  ", 0
TxHead:    db " 3.5MHz  turbo", 13, 0
BodyNames: db "NOP       ", 0
           db "LD A,(RAM)", 0
           db "LD A,(ROM)", 0
           db "LD (RAM),A", 0
           db "OUT (FE),A", 0
TxExp151:  db "SC15.1    ", 0
TxExp153:  db "SC15.3    ", 0
TxNotScorp: db 13, "Not a Scorpion: turbo not tried", 13, 0
TxNoTurbo: db 13, "Turbo makes no difference", 13, 0
TxIs151:   db 13, "Turbo+ logic: SC15.1", 13, 0
TxIs153:   db 13, "Turbo+ logic: SC15.3", 13, 0
TxNeither: db 13, "Matches neither firmware", 13, 0

PROBEEND:               ; the end of the program (what the coemu harness dumps from START)
