; fusetest-coemu - FUSE's fusetest, wrapped for the co-emulation harness (tools/verification/coemu)
;
; fusetest (Philip Kendall, GPL; built as fusetest.tap next to this file) prints its verdicts through RST #10
; and returns to BASIC. This wrapper keeps a copy of everything it prints: it points the output routine of the
; channels K and S at a hook that appends the character to BUFFER and then prints it as before. It calls the
; program at #A000, restores the channels and sets DONE. The harness dumps START..PROBEEND and
; fusetest-coemu-compare.py reads the text out of BUFFER.
;
; Memory: this wrapper and its buffer #9000-#9FFF, fusetest #A000-#A8F1. fusetest also uses #7Fxx-#8000 (its
; timed fragments), #BDBD-#BF00 (its interrupt table and handler) and the page at #C000, none of which overlap.
;
; The generator (core/tests/emulator/video/fusetest_test.cpp) appends fusetest's code at #A000 and writes
; fusetest-coemu.tap / .trd / .sym: one code block from #9000, loaded and started at 36864.

        org #9000

START:
        jp MAIN
HOSTENTRY:
        call MAIN
HostSpin:
        jr HostSpin

DONE:     db 0            ; 1 when fusetest has returned
BUFPTR:   dw BUFFER       ; where the next printed character goes
ORIGK:    dw 0            ; the channels' own output routines
ORIGS:    dw 0

FUSEMAIN  equ #A000
BUFEND    equ #A000
PROBEEND  equ #A000       ; the end of what the harness dumps (the whole buffer)

MAIN:
        ld hl,(#5C4F)           ; CHANS: channel K's record, its output routine first
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (ORIGK),de
        ld de,HookK
        ld (hl),d
        dec hl
        ld (hl),e
        ld de,5                 ; channel S's record
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (ORIGS),de
        ld de,HookS
        ld (hl),d
        dec hl
        ld (hl),e

        call FUSEMAIN

        di                      ; fusetest leaves IM 2 with its own table
        im 1
        ld a,#3F
        ld i,a
        ld iy,#5C3A
        ld hl,(#5C4F)
        ld de,(ORIGK)
        ld (hl),e
        inc hl
        ld (hl),d
        ld de,4
        add hl,de
        ld de,(ORIGS)
        ld (hl),e
        inc hl
        ld (hl),d
        ld a,1
        ld (DONE),a
        ei
        ret

; the channels' output: keep A, then the channel's own routine
HookK:
        call Keep
        push hl
        ld hl,(ORIGK)
        ex (sp),hl
        ret
HookS:
        call Keep
        push hl
        ld hl,(ORIGS)
        ex (sp),hl
        ret

; A appended to BUFFER while there is room; A kept
Keep:
        push hl
        push de
        ld hl,(BUFPTR)
        ld de,BUFEND
        or a
        sbc hl,de
        add hl,de               ; carry: HL was below BUFEND
        jr nc,KeepFull
        ld (hl),a
        inc hl
        ld (BUFPTR),hl
KeepFull:
        pop de
        pop hl
        ret

BUFFER:                         ; up to BUFEND (#A000); zero where nothing was printed
