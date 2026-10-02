; turbotest - how much work a turbo machine gets done in one frame (docs/inprogress/2026-09-29-machine-waits)
;
; In turbo, a Scorpion ZS-256 Turbo+ (7 MHz) and a ZX-Evo (14 MHz) make the CPU wait for its memory. The
; Scorpion's logic chip lets the CPU at the RAM only in its own time slots, and the rules differ between the
; chip's two firmwares, SC15.1 and SC15.3. The ZX-Evo waits on every RAM read that misses its two one-word
; caches (code and data). This program runs five short pieces of code (bodies) in a loop for exactly one frame,
; at 3.5 MHz and in turbo, and counts how many times each body ran. The counts are compared with what unreal-ng's
; models give (the Scorpion's two firmwares, the ZX-Evo), so the screen names the Scorpion's firmware or says
; whether the ZX-Evo matches.
;
; How a count is taken: HALT waits for the frame interrupt; the handler starts the loop (32 copies of the body,
; then INC DE / JP back), and the next frame's interrupt stops it. The stop handler takes the loop counter (DE)
; and the address the loop was interrupted at, which tells how many bodies of the current pass were done:
; count = DE * 32 + done. A count matches a table within one body: HALT repeats 4 T fetches, so where the loop
; starts after the interrupt moves by up to 3 T with the code before it.
;
; Which machine: a ZX-Evo reads back its registers (DetectEvo); its speed is set through #EFF7 and #xx77. Any
; other machine counts NOPs at whatever speed it runs, and turbo is only switched when that count is a
; Scorpion's, at 3.5 MHz or in turbo (its ROM leaves turbo on): on a 128K or a grey +2 reading port #7FFD or
; #1FFD, the Scorpion's turbo switches, can change the memory paging.
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
MATCH:    db 0            ; 1 SC15.1, 2 SC15.3, 3 ZX-Evo as modeled, 0 neither, #FF turbo makes no difference,
                          ; #FE not a Scorpion or a ZX-Evo
TRYTURBO: db 0            ; 1: turbo was measured (a ZX-Evo, or the first NOP count is a Scorpion's)
EVO:      db 0            ; 1: a ZX-Evo (BaseConf), turbo = 14 MHz
EVORB:    db 0            ; the ZX-Evo's register readback port: #BD (current FPGA tree) or #BE (legacy)
EVOBF:    db 0            ; its config port #xxBF, saved while the shadow ports are open
COUNTS:   ds 30           ; NBODY * 6: per body: the 3.5 MHz count, the turbo count (24 bits each, low byte first)
COUNTSEND:

; The counts unreal-ng gives with each firmware, the same layout as COUNTS (filled in by the generator)
EXP151:   ds 30
EXP153:   ds 30
EXPEVO:   ds 30           ; the ZX-Evo at 3.5 and 14 MHz

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

        call DetectEvo
        ld a,(EVO)
        or a
        jr z,NotEvo
        ld (TRYTURBO),a
        call EvoSlow            ; 3.5 MHz: #EFF7 D4 (the ZX-Evo may have booted at 7 or 14 MHz)
        call SpeedOff
        jr HaveSpeed
NotEvo:
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

HaveSpeed:
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
SpeedOff:
        ld a,(EVO)
        or a
        jr nz,EvoOff
        ld bc,#1FFD             ; the Scorpion: turbo off on IN from #1FFD
        in a,(c)
        ret
EvoOff:
        xor a
        jr EvoSet77

SpeedOn:
        ld a,(EVO)
        or a
        jr nz,EvoOn
        ld bc,#7FFD             ; the Scorpion's logic chip latches turbo on IN from #7FFD
        in a,(c)
        ret
EvoOn:
        ld a,8

; ZX-Evo: #xx77 D3 (14 MHz) = A, every other bit and address line as the machine has them now. #xx77 is written
; only with the shadow ports open (#xxBF D0); its state reads back as register #0C: {A14, A9, A8, DOS, D3..D0}
EvoSet77:
        ld e,a
        ld a,#0C
        call EvoReg
        ld d,a
        ld b,0
        bit 7,d
        jr z,Evo77A9
        set 6,b                 ; A14
Evo77A9:
        bit 6,d
        jr z,Evo77A8
        set 1,b                 ; A9
Evo77A8:
        bit 5,d
        jr z,Evo77Out
        set 0,b                 ; A8
Evo77Out:
        ld a,d
        and #07                 ; the video mode
        or e
        push af
        push bc
        ld bc,#00BF             ; open the shadow ports, keep the other config bits
        in a,(c)
        ld (EVOBF),a
        or 1
        out (c),a
        pop bc
        ld c,#77
        pop af
        out (c),a
        ld a,(EVOBF)
        ld bc,#00BF
        out (c),a
        ret

; ZX-Evo: #EFF7 D4 = 1 (3.5 MHz while #xx77 D3 is 0), the other bits as they are (register #0B)
EvoSlow:
        ld a,#0B
        call EvoReg
        or #10
        ld bc,#EFF7
        out (c),a
        ret

; ZX-Evo register A (0..#1F) from the readback port; A15 = 1 keeps the read away from a 128K's #7FFD
EvoReg:
        or #80
        ld b,a
        ld a,(EVORB)
        ld c,a
        in a,(c)
        ret

; EVO = 1 on a ZX-Evo: register #0A reads back the #7FFD value MAIN wrote (#10), through #xxBD (the current
; FPGA tree) or #xxBE (the legacy one), and the config port #xxBF reads back with its top bits clear
DetectEvo:
        ld bc,#8ABD
        in a,(c)
        cp #10
        ld e,#BD
        jr z,EvoFound
        ld bc,#8ABE
        in a,(c)
        cp #10
        ret nz
        ld e,#BE
EvoFound:
        ld bc,#00BF
        in a,(c)
        and #C0
        ret nz
        ld a,e
        ld (EVORB),a
        ld a,1
        ld (EVO),a
        ret

SpeedOffIfTried:
        ld a,(TRYTURBO)
        or a
        ret z
        jp SpeedOff

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
        ld a,(EVO)
        or a
        jr z,HasScorpTurbo
        ld hl,EXPEVO
        call Differ
        ld a,b
        or c
        ld a,3
        jr z,Matched
        xor a
        jr Matched
HasScorpTurbo:
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

; BC = how many counts differ by more than one between COUNTS and the table at HL. One is the start's jitter:
; HALT repeats 4 T fetches, so the loop starts 0-3 T after a fixed point, and a count can move by one body
Differ:
        ld de,COUNTS
        ld bc,0
        ld a,NBODY * 2
DifferNext:
        push af
        push bc
        ld a,(de)               ; ABC... C:B:A = got - expected, 24 bits
        sub (hl)
        ld c,a
        inc de
        inc hl
        ld a,(de)
        sbc a,(hl)
        ld b,a
        inc de
        inc hl
        ld a,(de)
        sbc a,(hl)
        inc de
        inc hl
        or a
        jr z,DifferHighZero
        inc a                   ; -1: #FFFFFF
        jr nz,DifferBad
        ld a,b
        and c
        inc a
        jr nz,DifferBad
        jr DifferOk
DifferHighZero:
        ld a,b
        or a
        jr nz,DifferBad
        ld a,c
        cp 2
        jr c,DifferOk
DifferBad:
        pop bc
        inc bc
        jr DifferStep
DifferOk:
        pop bc
DifferStep:
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
        ld a,(EVO)
        or a
        jr z,ReportScorp
        ld hl,TxExpEvo
        call PrintStr
        ld ix,EXPEVO
        call PrintTable
        jr ReportVerdict
ReportScorp:
        ld hl,TxExp151
        call PrintStr
        ld ix,EXP151
        call PrintTable
        ld hl,TxExp153
        call PrintStr
        ld ix,EXP153
        call PrintTable

ReportVerdict:
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
        ld hl,TxIsEvo
        cp 3
        jr z,ReportEnd
        ld hl,TxNeither
        ld a,(EVO)
        or a
        jr z,ReportEnd
        ld hl,TxEvoDiffers
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
TxExpEvo:  db "ZX-Evo    ", 0
TxNotScorp: db 13, "No Scorpion / ZX-Evo turbo:", 13, "turbo not tried", 13, 0
TxIsEvo:   db 13, "ZX-Evo 14 MHz: as modeled", 13, 0
TxEvoDiffers: db 13, "ZX-Evo 14 MHz: differs", 13, "from the model", 13, 0
TxNoTurbo: db 13, "Turbo makes no difference", 13, 0
TxIs151:   db 13, "Turbo+ logic: SC15.1", 13, 0
TxIs153:   db 13, "Turbo+ logic: SC15.3", 13, 0
TxNeither: db 13, "Matches neither firmware", 13, 0

PROBEEND:               ; the end of the program (what the coemu harness dumps from START)
