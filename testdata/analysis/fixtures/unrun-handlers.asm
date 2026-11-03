; AnalysisDetectorPointerTables_UnrunHandlers: a 3 x 4 table of handlers entered by JP (HL), of which the dispatch
; only takes entries 0, 5 and 10 (20 bytes apart, more than one table's gap); the other words point at the run
; handlers or at two handlers that never run, laid out between the run ones. Positive: the whole table is one handler
; table (the words between the used entries point inside the targets jumped to through the dispatch), and each
; handler that never ran is inferred code with a routine entry (a table entry points at it and it decodes as code).
        org $8000
start:
        ld sp,$9F00
        di
main:
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
run_b:  inc c
        jp main
unrun_b:
        ld a,c
        or a
        jr nz,ub_set
        inc b
        jr ub_done
ub_set: dec c
        dec c
ub_done:
        ld a,b
        sub c
        ld b,a
        rrca
        rrca
        jp main
run_c:  dec b
        jp main
sel:    db 0
        dw $FFFF
table:  dw run_a,run_a,unrun_a,unrun_b
        dw run_a,run_b,unrun_a,unrun_b
        dw unrun_b,unrun_a,run_c,run_a
after:  dw $FFFF
