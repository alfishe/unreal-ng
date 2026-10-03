; zxtime - the timing of a Sprinter's Spectrum mode, measured by the Spectrum program itself
; (docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md, phases Z1 and Z3; README.md next to this file)
;
; What it measures, at whatever clock the mode runs:
;   FRAME   the frame length: a counting loop (24 T per pass) runs between the frame interrupts for 50
;           frames; frame = (50 x 101 T of interrupt handler + 24 T x passes + 16 T x carries) / 50. At
;           3.5 MHz that is the frame in T-states (71 680 for 320 lines, 69 888 for 312); in turbo the loop
;           waits for the memory, so only the pass count is shown
;   INT     how many interrupts arrive in 50 frames, and how many of them come back at once when the
;           handler enables interrupts as its first instruction (a pulse the acknowledge does not end)
;   SCREEN  how much slower a read of the Spectrum screen memory is than a read of other RAM: 2 000
;           reads of #4000 (and of #C000 with page 5 and page 1) against 2 000 reads of #8000, 8 times
;           each, timed by the counting loop that follows them to the next interrupt. Printed in
;           thousandths of a T-state per read. ORIGIN.ZX ("original waits") slows #4000-#7FFF and pages
;           4-7 at #C000; every other mode reads them at full speed
;
; The program runs from #8000 with its own stack, interrupt table (#9000, I = #90, handler #9191) and read
; block (#A000-#B773), all in RAM page 2: nothing it measures waits on its own code. It prints through
; the 48 BASIC ROM (#7FFD = #10) and returns to BASIC.
;
; Standalone: CLEAR 32767, load the code at 32768, RANDOMIZE USR 32768 (zxtime.trd's boot does that).

NFRAMES   equ 50        ; frames per measurement
READS     equ 2000      ; screen reads per run
RDBLOCK   equ #A000     ; the read block: READS x LD A,(nn), then JP CountLoop
STACKTOP  equ #A000     ; the program's stack, below the read block

        org 32768

START:
        di
        ld (BASICSP),sp
        ld sp,STACKTOP
        ld bc,#7FFD             ; the 48 BASIC ROM (the report prints through it), RAM page 0 at #C000
        ld a,#10
        out (c),a

        ld hl,#9000             ; the IM 2 table: 257 x #91, the handler at #9191
        ld de,#9001
        ld bc,256
        ld (hl),#91
        ldir
        ld a,#C3                ; JP ISR
        ld (#9191),a
        ld hl,ISR
        ld (#9192),hl
        ld a,#90
        ld i,a
        im 2

        call MeasureFrame
        ld hl,(LOOPHL)
        ld (PASSES),hl
        ld a,(LOOPDE)
        ld (PASSES + 2),a
        ld (CARRIES),a
        call MeasureInts

        ld hl,#8000             ; the reference: other RAM
        ld a,#10
        call MeasureRead
        ld (RREF),hl
        ld hl,#4000             ; the screen at #4000
        ld a,#10
        call MeasureRead
        ld (R4000),hl
        ld hl,#C000             ; page 5 at #C000 (#7FFD bit 2 set)
        ld a,#15
        call MeasureRead
        ld (RC5),hl
        ld hl,#C000             ; page 1 at #C000 (bit 2 clear)
        ld a,#11
        call MeasureRead
        ld (RC1),hl

        ld bc,#7FFD
        ld a,#10
        out (c),a
        im 1
        ld a,#3F
        ld i,a
        ld iy,#5C3A
        call Report
        ld a,1
        ld (DONE),a
        ld sp,(BASICSP)
        ei
        ret

; ---- outputs (read by the emulator's tests and by a debugger; the screen shows them too) ----

DONE:     db 0          ; 1 when finished
PASSES:   db 0, 0, 0    ; counting-loop passes in 50 frames (24 bits)
CARRIES:  db 0          ; how many of them carried into the high part (16 T more each)
FRAMET:   db 0, 0, 0, 0 ; the frame in T-states (at 3.5 MHz)
TURBO:    db 0          ; 1: the frame is too long for 3.5 MHz, the CPU runs faster
INTS:     db 0          ; interrupts counted as frames in the INT run (50)
REPEATS:  db 0          ; interrupts that came back inside one pulse
RREF:     dw 0          ; the sum of 8 counts after 2 000 reads of #8000
R4000:    dw 0          ; ... of #4000
RC5:      dw 0          ; ... of #C000, page 5
RC1:      dw 0          ; ... of #C000, page 1

; ---- work variables ----

BASICSP:  dw 0
STOPSP:   dw 0
LOOPHL:   dw 0          ; the counting loop's counters when a run stopped
LOOPDE:   dw 0
FCOUNT:   db 0
LASTHL:   dw 0
RDADDR:   dw 0
RSUM:     dw 0
ACC:      db 0, 0, 0, 0
TMP:      db 0, 0, 0, 0
DIV:      db 0, 0, 0, 0
REM:      db 0, 0, 0, 0
NUMBUF:   db "0000000000"


; ---- the counting loop and the stopping handler ----

; Interrupt N + 1 after the first ends a run: the handler counts FCOUNT down and, at zero, leaves through
; STOPSP with the loop counters in DE:HL
CountLoop:
        inc hl                  ; 6
        ld a,h                  ; 4
        or l                    ; 4
        jp nz,CountLoop         ; 10: 24 T per pass
        inc de                  ; 6
        jp CountLoop            ; 10: 16 T more per carry

ISR:    push af                 ; 11 (with the IM 2 acknowledge 19, JP ISR 10 and the rest: 101 T)
        ld a,(FCOUNT)           ; 13
        dec a                   ; 4
        ld (FCOUNT),a           ; 13
        jr z,IsrStop            ; 7
        pop af                  ; 10
        ei                      ; 4
        ret                     ; 10
IsrStop:
        ld (LOOPHL),hl
        ld (LOOPDE),de
        ld sp,(STOPSP)
        ret

; FRAME: the counting loop for NFRAMES frames
MeasureFrame:
        ld a,NFRAMES + 1
        ld (FCOUNT),a
        ld hl,0
        ld de,0
        ld (STOPSP),sp
        ei
        halt
        jp CountLoop

; INT: the handler enables interrupts first; a second entry with the loop counter unchanged came back
; inside the same pulse
MeasureInts:
        ld hl,ISR2
        ld (#9192),hl
        ld a,NFRAMES + 1
        ld (FCOUNT),a
        xor a
        ld (REPEATS),a
        ld (INTS),a
        ld hl,#FFFF
        ld (LASTHL),hl
        ld hl,0
        ld de,0
        call IntRun
        ld hl,ISR
        ld (#9192),hl
        ld a,(INTS)             ; the first interrupt only started the run
        dec a
        ld (INTS),a
        ret
IntRun:
        ld (STOPSP),sp
        ei
        halt
        jp CountLoop

ISR2:   ei                      ; a pulse the acknowledge does not end comes back after the next instruction
        push af
        push de
        push hl
        ld de,(LASTHL)
        or a
        sbc hl,de
        ld a,h
        or l
        pop hl
        jr nz,Isr2Frame
        ld a,(REPEATS)
        inc a
        ld (REPEATS),a
        jr Isr2Out
Isr2Frame:
        ld (LASTHL),hl
        ld a,(INTS)
        inc a
        ld (INTS),a
        ld a,(FCOUNT)
        dec a
        ld (FCOUNT),a
        jr z,Isr2Stop
Isr2Out:
        pop de
        pop af
        ret
Isr2Stop:
        di
        ld sp,(STOPSP)
        ret

; SCREEN: READS x LD A,(HL's address) right after an interrupt, then the counting loop to the next one;
; 8 runs, HL = the sum of the counts. A = #7FFD for the run
MeasureRead:
        ld bc,#7FFD
        out (c),a
        ld (RDADDR),hl
        ld de,RDBLOCK
        ld bc,READS
BuildRead:
        ld a,#3A                ; LD A,(nn): 13 T, the read 10 T after the opcode
        ld (de),a
        inc de
        ld a,(RDADDR)
        ld (de),a
        inc de
        ld a,(RDADDR + 1)
        ld (de),a
        inc de
        dec bc
        ld a,b
        or c
        jr nz,BuildRead
        ld a,#C3                ; JP CountLoop
        ld (de),a
        inc de
        ld hl,CountLoop
        ex de,hl
        ld (hl),e
        inc hl
        ld (hl),d

        ld hl,0
        ld (RSUM),hl
        ld b,8
ReadRuns:
        push bc
        call ReadRun
        ld hl,(LOOPHL)
        ld de,(RSUM)
        add hl,de
        ld (RSUM),hl
        pop bc
        djnz ReadRuns
        ld hl,(RSUM)
        ret
ReadRun:
        ld a,2
        ld (FCOUNT),a
        ld hl,0
        ld de,0
        ld (STOPSP),sp
        ei
        halt
        jp RDBLOCK

; ---- 32-bit arithmetic on ACC (little endian) ----

; ACC = C:HL
LdAcc:
        ld (ACC),hl
        ld a,c
        ld (ACC + 2),a
        xor a
        ld (ACC + 3),a
        ret

; ACC += the 4 bytes at DE
AddAcc:
        ld hl,ACC
        ld b,4
        or a
AddAccByte:
        ld a,(de)
        adc a,(hl)
        ld (hl),a
        inc hl
        inc de
        djnz AddAccByte
        ret

; ACC += HL
AddAccHl:
        ld (TMP),hl
        ld hl,0
        ld (TMP + 2),hl
        ld de,TMP
        jr AddAcc

; ACC *= A (A >= 1)
MulAcc:
        ld b,a
        ld hl,(ACC)
        ld (TMP),hl
        ld hl,(ACC + 2)
        ld (TMP + 2),hl
        dec b
        ret z
MulAccStep:
        push bc
        ld de,TMP
        call AddAcc
        pop bc
        djnz MulAccStep
        ret

; DIV = HL (16 bits)
LdDiv:
        ld (DIV),hl
        ld hl,0
        ld (DIV + 2),hl
        ret

; ACC /= DIV, REM = the remainder (restoring division, 32 steps)
DivAcc:
        ld hl,0
        ld (REM),hl
        ld (REM + 2),hl
        ld b,32
DivAccStep:
        push bc
        ld hl,ACC               ; ACC <<= 1, the top bit into REM
        or a
        ld b,4
DivShiftAcc:
        rl (hl)
        inc hl
        djnz DivShiftAcc
        ld hl,REM
        ld b,4
DivShiftRem:
        rl (hl)
        inc hl
        djnz DivShiftRem
        ld hl,REM               ; TMP = REM - DIV
        ld de,DIV
        ld ix,TMP
        ld b,4
        or a
DivSub:
        ld a,(hl)
        ex de,hl
        sbc a,(hl)
        ex de,hl
        ld (ix+0),a
        inc hl
        inc de
        inc ix
        djnz DivSub
        jr c,DivNoFit           ; REM < DIV
        ld hl,(TMP)
        ld (REM),hl
        ld hl,(TMP + 2)
        ld (REM + 2),hl
        ld hl,ACC
        set 0,(hl)
DivNoFit:
        pop bc
        djnz DivAccStep
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

        ; frame = (NFRAMES x 101 + 24 x passes + 16 x carries + NFRAMES / 2) / NFRAMES
        ld hl,(PASSES)
        ld a,(PASSES + 2)
        ld c,a
        call LdAcc
        ld a,24
        call MulAcc
        ld a,(CARRIES)
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,NFRAMES * 101 + NFRAMES / 2
        add hl,de
        call AddAccHl
        ld hl,NFRAMES
        call LdDiv
        call DivAcc
        ld hl,(ACC)
        ld (FRAMET),hl
        ld hl,(ACC + 2)
        ld (FRAMET + 2),hl
        ; 100 000 "T" per frame or more (#186A0): the CPU runs faster than 3.5 MHz
        ld a,(ACC + 3)
        or a
        jr nz,FastCpu
        ld a,(ACC + 2)
        cp 2
        jr nc,FastCpu
        or a
        jr z,SlowCpu
        ld hl,(ACC)
        ld de,#86A0
        or a
        sbc hl,de
        jr c,SlowCpu
FastCpu:
        ld a,1
        ld (TURBO),a
        ld hl,TxTurbo
        call PrintStr
        ld hl,(PASSES)
        ld a,(PASSES + 2)
        ld c,a
        call LdAcc
        ld hl,NFRAMES
        call LdDiv
        call DivAcc
        call PrintAcc
        ld hl,TxPerFrame
        call PrintStr
        jr ReportInts
SlowCpu:
        ld hl,TxSlow
        call PrintStr
        call PrintAcc
        ld hl,TxT
        call PrintStr
        ; frames per second x 100 = (350 000 000 + frame / 2) / frame
        ld hl,(FRAMET)
        ld (DIV),hl
        ld hl,(FRAMET + 2)
        ld (DIV + 2),hl
        ld hl,(FRAMET + 1)      ; ACC = frame / 2 (the frame is below 2^24 here)
        ld a,(FRAMET)
        srl h
        rr l
        rra
        ld c,h
        ld h,l
        ld l,a
        call LdAcc
        ld hl,#9380             ; + 350 000 000 = #14DC9380
        ld (TMP),hl
        ld hl,#14DC
        ld (TMP + 2),hl
        ld de,TMP
        call AddAcc
        call DivAcc
        ld hl,TxFps
        call PrintStr
        call PrintFixed2
        ld hl,TxFpsUnit
        call PrintStr

ReportInts:
        ld hl,TxInt
        call PrintStr
        ld a,(INTS)
        call PrintA
        ld hl,TxIntIn
        call PrintStr
        ld a,(REPEATS)
        call PrintA
        ld hl,TxRepeats
        call PrintStr

        ld a,(TURBO)
        or a
        jr z,ReportReads
        ld hl,TxReadsTurbo
        call PrintStr
        ld hl,TxEnd
        jp PrintStr
ReportReads:
        ld hl,TxReads
        call PrintStr
        ld hl,TxR4000
        call PrintStr
        ld hl,(R4000)
        call PrintExtra
        ld hl,TxRC5
        call PrintStr
        ld hl,(RC5)
        call PrintExtra
        ld hl,TxRC1
        call PrintStr
        ld hl,(RC1)
        call PrintExtra
        ld hl,TxEnd
        jp PrintStr

; the extra thousandths of a T per read: (RREF - HL) x 24 T x 1000 / (READS x 8) = (RREF - HL) x 3 / 2
PrintExtra:
        ex de,hl
        ld hl,(RREF)
        or a
        sbc hl,de
        jr nc,ExtraPos
        ld hl,0
ExtraPos:
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        srl h
        rr l
        ld c,0
        call LdAcc
        call PrintAcc
        ld a,13
        rst #10
        ret

; A as a decimal number
PrintA:
        ld l,a
        ld h,0
        ld c,0
        call LdAcc
        jp PrintAcc

; ACC as a decimal number, no leading zeros (ACC is consumed)
PrintAcc:
        ld hl,10
        call LdDiv
        ld de,NUMBUF + 9
        ld b,10
PrintAccDigit:
        push bc
        push de
        call DivAcc
        pop de
        ld a,(REM)
        add a,'0'
        ld (de),a
        dec de
        pop bc
        djnz PrintAccDigit
        ld hl,NUMBUF
        ld b,9
PrintAccSkip:
        ld a,(hl)
        cp '0'
        jr nz,PrintAccOut
        inc hl
        djnz PrintAccSkip
PrintAccOut:
        inc b
PrintAccChar:
        ld a,(hl)
        push hl
        push bc
        rst #10
        pop bc
        pop hl
        inc hl
        djnz PrintAccChar
        ret

; ACC (hundredths) as "n.nn"
PrintFixed2:
        ld hl,100
        call LdDiv
        call DivAcc
        ld hl,(REM)
        push hl
        call PrintAcc
        ld a,'.'
        rst #10
        pop hl
        ld a,l
        ld b,'0' - 1
Tens:
        inc b
        sub 10
        jr nc,Tens
        add a,10 + '0'
        push af
        ld a,b
        rst #10
        pop af
        rst #10
        ret

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

TxTitle:      db "zxtime 1: Sprinter ZX timing", 13, 13, 0
TxSlow:       db "CPU    3.5 MHz", 13, "FRAME  ", 0
TxT:          db " T", 13, 0
TxFps:        db "RATE   ", 0
TxFpsUnit:    db " frames/s", 13, 0
TxTurbo:      db "CPU    faster than 3.5 MHz", 13, "LOOP   ", 0
TxPerFrame:   db " passes/frame", 13, 0
TxInt:        db 13, "INT    ", 0
TxIntIn:      db " in 50 frames", 13, "REPEAT ", 0
TxRepeats:    db " came back at once", 13, 0
TxReads:      db 13, "SCREEN READS, extra T x 1000", 13, 0
TxR4000:      db "#4000        ", 0
TxRC5:        db "#C000 page 5 ", 0
TxRC1:        db "#C000 page 1 ", 0
TxReadsTurbo: db 13, "Screen reads: not timed in turbo", 13, 0
TxEnd:        db 13, "Done.", 13, 0

PROGEND:
