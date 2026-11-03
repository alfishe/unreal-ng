; AnalysisDetectorSaveAreas_LiveDataIsNoSave: a 5-byte score the game counts up, draws (reads every byte) and saves
; into one row of a table per round with one LDIR, then puts back from the row with another LDIR. The copies go both
; ways, so each side looks saved in the other; the rows are read only to restore the score (a save area), the score
; is read by the drawing too (the live data, no save area).
        org $8000
start:
        ld sp,$8F00
        di
main:
        ld hl,score+4           ; count: the last digit goes up
        inc (hl)
        ld hl,score             ; draw: every digit read
        ld b,5
        xor a
draw:   add a,(hl)
        inc hl
        djnz draw
        ld (sink),a
        ld a,(round)            ; the round: 0, 1, 2, 0 ...
        inc a
        cp 3
        jr c,keep
        xor a
keep:   ld (round),a
        ld l,a                  ; the row: rows + 5 x round
        add a,a
        add a,a
        add a,l
        ld l,a
        ld h,0
        ld de,rows
        add hl,de
        push hl
        ex de,hl                ; save: score -> row
        ld hl,score
        ld bc,5
        ldir
        pop hl                  ; restore: row -> score
        ld de,score
        ld bc,5
        ldir
        jr main
round:  db 0
sink:   db 0
        org $9000
score:  db "00000"
after:  db $AA,$BB
        org $9100
rows:   ds 15,$20
