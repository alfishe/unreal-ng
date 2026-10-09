; Labels for the pasmo symbol file golden (pasmo 0.5.5)
SCREEN  EQU 4000h
LINES   EQU 24
        ORG 8000h
start:  ld hl,SCREEN
loop:   djnz loop
        call play
        ret
data_tab: db 1,2,3
        PUBLIC start
        PUBLIC play
play:   ld a,LINES
        PROC
        LOCAL frame
frame:  nop
        jr frame
        ENDP
        ret
