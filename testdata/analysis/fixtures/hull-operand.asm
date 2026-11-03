; AnalysisDetectorPointerTables / FinishUnused: a 3 x 4 table of handlers entered by JP (HL), of which the dispatch
; takes entries 0, 5 and 10 (as unrun-handlers.asm). unrun_a never runs: a table entry points at it, and it is inferred
; code up to its JP main. The bytes after it (trailer: LD A,1 / LD A,2 / RET, then a flag main reads) are no part of
; it, though they decode and end in a RET (negative). The word after the table lies inside the targets jumped to, but
; points at the operand of run_b's JP, not at an instruction: no entry, the table ends before it (negative).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(flag)
        ld a,(sel)
        inc a
        cp 3
        jr c,keep
        xor a
keep:   ld (sel),a
        ld e,a
        add a,a
        add a,a
        add a,e                 ; x 5: entries 0, 5, 10
        add a,a                 ; x 2: the word
        ld e,a
        ld d,0
        ld hl,table
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        jp (hl)
run_a:  inc b
        jp main
run_b:  inc c
        jp main
unrun_a:
        ld a,b
        and a
        jr z,ua_zero
        dec b
        jr ua_done
ua_zero:
        inc c
        inc c
ua_done:
        ld a,c
        add a,b
        ld c,a
        rlca
        rlca
        jp main
trailer:
        ld a,1
        ld a,2
        ret
flag:   db 0
run_c:  dec b
        jp main
sel:    db 0
        dw $FFFF
table:  dw run_a,run_a,unrun_a,run_b
        dw run_a,run_b,unrun_a,run_b
        dw run_c,unrun_a,run_c,run_a
after:  dw run_b+2
        dw $FFFF
