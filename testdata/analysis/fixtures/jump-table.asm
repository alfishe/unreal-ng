; AnalysisDetectorPointerTables: a table of four handlers picked by an index and entered by JP (HL) (positive),
; and the same eight bytes only summed as bytes (negative: no table claim). A word that points at no code stands
; between them: the handler table grows over neighbouring words that point at code (inferred entries).
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
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        jp (hl)
h0:     inc b
        jr sum
h1:     inc c
        jr sum
h2:     dec b
        jr sum
h3:     dec c
sum:
        ld hl,copy
        ld b,8
        xor a
sum_loop:
        add a,(hl)
        inc hl
        djnz sum_loop
        jr main
sel:    db 0
table:  dw h0,h1,h2,h3
        dw $FFFF
copy:   dw h0,h1,h2,h3
