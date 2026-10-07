; Labels for the z88dk z80asm map file golden (z88dk 2.3)
        PUBLIC start, play
        DEFC SCREEN = $4000
        DEFC LINES = 24
        SECTION code
        ORG $8000
start:  ld hl,SCREEN
loop:   djnz loop
        call play
        ret
play:   ld a,LINES
        ret
        SECTION data
data_tab: defb 1,2,3
