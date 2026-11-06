; ArraysCompletedByFills: a table of 8 rows of 6 bytes (a lives byte, then a 5-byte score), row r at table + 6 x round.
; One routine writes the current row (the byte, then the score by LDIR), another reads it back the same way; round
; only cycles 0..2, so rows 3-7 are never read or written through the index. A clear routine fills rows 1-7 (42 bytes
; from table + 6) with spaces once, by one LD (HL),n loop: the fill starts on the rows' lattice and is a whole number
; of rows long, so the table is all 8 rows, rows 3-7 inferred with the same fields.
        org $8000
start:
        ld sp,$9F00
        di
        call clear
main:
        ld hl,score
        inc (hl)
        ld a,(round)
        inc a
        cp 3                    ; 0..2: the rows the session reaches
        jr c,keep
        xor a
keep:   ld (round),a
        call store
        call fetch
        jr main

store:                          ; row = lives, score
        ld a,(round)
        add a,a
        ld b,a
        add a,a
        add a,b
        ld c,a
        ld b,0
        ld hl,table
        add hl,bc
        ld a,(lives)
        ld (hl),a
        inc hl
        ld de,score
        ex de,hl
        ld bc,5
        ldir
        ret

fetch:                          ; lives, score = row
        ld b,0
        ld a,(round)
        add a,a
        ld c,a
        add a,a
        add a,c
        ld c,a
        ld hl,table
        add hl,bc
        ld a,(hl)               ; the lives byte, read through the index
        ld (lives),a
        inc hl
        ld de,score
        ld bc,5
        ldir                    ; the score, a block read through the index
        ret

clear:                          ; rows 1-7: spaces
        ld hl,table+6
        ld b,42
clr:    ld (hl),$20
        inc hl
        djnz clr
        ret

round:  db 0
lives:  db 3
score:  db "00000"
        org $9000
table:  ds 48
after:  db $AA,$BB,$CC,$DD
