; Stages: a program that arrives in steps, like a loader, a depacker and the game it unpacks. AnalysisL1_Test checks
; the code arrivals (one per writer), the window build from the second one, and AnalysisL3_Test the overlay finding.
;
;   start      (frame 0)  waits a frame, copies stage1 to $A000 with one LDIR, jumps there
;   $A000      (frame 1)  waits a frame, copies stage2 to $B000 with two writers (the first byte by LD (DE),A, the
;                         rest by LDIR: one depacker), jumps there
;   $B000      (frame 2)  waits, calls back into the loader's code (the return into stage 2 is no new arrival),
;                         then copies 16 bytes over $A000: the code that ran there is overwritten (an overlay);
;                         counts in `counter` for ever
        org $8000
start:
        ld sp,$9F00
        di
        ld bc,3000
wait0:
        dec bc
        ld a,b
        or c
        jr nz,wait0
        jr loader
helper:
        ret
loader:
        ld hl,stage1
        ld de,$A000
        ld bc,stage1_end-stage1
copy1:
        ldir
        jp $A000

; Stage 1, run at $A000 (relative jumps only)
stage1:
        ld bc,3000
wait1:
        dec bc
        ld a,b
        or c
        jr nz,wait1
        ld hl,stage2
        ld de,$B000
        ld a,(hl)
put2:
        ld (de),a
        inc hl
        inc de
        ld bc,stage2_end-stage2-1
        ldir
        jp $B000
stage1_end:

; Stage 2, run at $B000
stage2:
        ld bc,3000
wait2:
        dec bc
        ld a,b
        or c
        jr nz,wait2
        call helper
        ld hl,filler
        ld de,$A000
        ld bc,16
        ldir
count:
        ld a,(counter)
        inc a
        ld (counter),a
        jr count
stage2_end:

filler:
        defs 16,$FF
counter:
        defb 0
