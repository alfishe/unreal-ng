; ctprobe measuring engine: run code at an exact frame T-state and return its duration.
;
; The routines below are Jan Bobrowski's zxtests engine as adjusted by Patrik Rak for his Timing Test v0.3
; (testdata/contention/rak-timing-test; license GPL). They are ported unchanged in behavior to the in-tree
; assembler's syntax: the local labels (.name) got unique names and the sjasmplus pseudo-op "ld bc,hl" is
; spelled "ld b,h / ld c,l". Every other instruction, and so every T-state of the timed paths, is the
; original's.
;
; INSTINT     install the IM2 table (#BE00-#BF00) and the handler at #BFBF
; FRAMETIME   measure the frame: FRAMET = frame length - 32768
; CODETIME    call the code at DE exactly at frame T-state HL; the code ends with RET and keeps DE;
;             returns its duration (minus the 10 T of that RET) in HL and BC
; DELAY       wait BC T-states, the CALL included (BC >= 141); destroys AF, BC, HL
;
; The engine must run from uncontended memory (the probe loads at 40000).

; ---- interrupt.asm (Jan Bobrowski, modified slightly by Patrik Rak) ----

INSTINT:
        ld a,#BF                ; handler at #BFBF
        ld d,a
        ld e,a
        ld hl,IntHandler
        ld bc,IntHandlerEnd-IntHandler
        ldir
        inc b
        ld h,#BE                ; table at #BE00..#BF01
        ld l,c
        ld d,h
        ld e,b
        ld (hl),a
        ld a,h
        ldir
        ld i,a
        ret

IntHandler:                     ; 20T+ (1T+ is ULA offset, 19T IM2 handling)
        inc sp                  ; 26T+
        inc sp                  ; 32T+
        ei                      ; 36T+
        ret                     ; 46T+
IntHandlerEnd:

; ---- codetime.asm (Patrik Rak, based on code by Jan Bobrowski) ----

CODETIME:
        push hl
        call CtTest
        im 1
        ld hl,(CtDelay)
        add hl,de
        pop bc
        add hl,bc
        ex de,hl
        ld hl,(FRAMET)
        ld bc,32768-10-46+1     ; (10 RET, 46 stage3 overhead, 1 ULA offset)
        add hl,bc
        or a
        sbc hl,de
        ld b,h
        ld c,l
        ret

CtTest:
        ld bc,-65+7
        add hl,bc

        ld a,4

CtSetup:
        ld bc,CtStage3
        push bc
        push de
        push hl
        ld bc,CtAlign
        push bc
        ld bc,CtTry
        push bc

        dec a
        jr nz,CtSetup

        ld bc,CtStage2
        push bc

        ld bc,CtStage1
        push bc
        push de
        push hl
        ld bc,CtAlign
        push bc
        ld bc,CtTry
        push bc

        halt
        im 2
        halt
        rst 0

CtTry:                          ; 46T+
        ld bc,CtTry             ; 56T+
        push bc                 ; 67T+
        ld bc,32667             ; 77T+
        call DELAY              ; 32744T+
        ld bc,(FRAMET)          ; 32764T+
        call DELAY              ; FRAMET-32772 +
        nop                     ; FRAMET-32768 +
        pop bc
        rst 0

CtAlign:                        ; 55T
        pop bc                  ; 65T
        jp DELAY                ; 65T+DELAY-7T (CALL 17 JP 10)

CtStage1:                       ; X
        ld hl,0                 ; X+10
CtLoop:
        inc hl                  ; X+10+16*k
        jp CtLoop

CtStage2:
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,10-16-46
        add hl,bc
        ld (CtDelay),hl
        ld de,0
        halt
        rst 0

CtDelay:
        dw 0

CtStage3:                       ; X
        ld bc,(CtDelay)         ; X+20
        dec bc                  ; X+26
        ld (CtDelay),bc         ; X+46
        call DELAY              ; FRAMET-16+0..15
        inc e
        inc e
        inc e
        inc e
        inc e
        inc e
        inc e
        rst 0

; ---- frametime.asm (Jan Bobrowski, adjusted by Patrik Rak) ----

FRAMETIME:
        call FtTest
        im 1
        ex de,hl
        add hl,bc
        ld (FRAMET),hl
        ld b,h
        ld c,l
        ret

FtTest:
        ld bc,FtStage3
        push bc
        push bc
        push bc
        push bc
        ld bc,FtStage2
        push bc
        ld bc,FtStage1
        push bc
        ld de,0
        ld hl,0

        halt
        im 2
        halt
        rst 0

FtStage1:                       ; 46..49
FtLoop:
        inc hl
        jp FtLoop

FtStage2:
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,46-16-32768
        add hl,bc
        ld b,h
        ld c,l
        halt
        rst 0

FtStage3:                       ; 46..49
        push bc
        call DELAY
        ld bc,32768-45-31
        call DELAY
        pop bc
        inc e
        inc e
        inc e
        inc e
        inc e
        inc e
        inc e
        rst 0

FRAMET:
        dw 0

; ---- delay.asm (Jan Bobrowski) ----

DELAY:
        ld hl,-141
        add hl,bc
        ld bc,-23
DlLoop:
        add hl,bc
        jr c,DlLoop
        ld a,l
        add a,15
        jr nc,DlG0
        cp 8
        jr c,DlG1
        or 0
DlG0:
        inc hl
DlG1:
        rra
        jr c,DlB0
        nop
DlB0:
        rra
        jr nc,DlB1
        or 0
DlB1:
        rra
        ret nc
        ret
