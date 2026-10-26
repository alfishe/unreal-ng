; AnalysisDetectorPointerTables: a handler table read as words and entered by PUSH + RET (a dispatch, no JP).
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(sel)
        inc a
        and 3
        ld (sel),a
        add a,a
        ld e,a
        ld d,0
        ld hl,table
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,main
        push hl
        push de
        ret
h0:     inc b
        ret
h1:     inc c
        ret
h2:     dec b
        ret
h3:     dec c
        ret
sel:    db 0
table:  dw h0,h1,h2,h3
