; AnalysisDetectorVariables groups: a 128-byte area copied once from `source` (a buffer; the copy touches every
; byte, so it shares no use). Four colour bytes 6 apart read through (nn) (strided variables: an array of 6-byte
; records that takes the fifth, unread record before `x`), x / y / dir used together by `move` (a group of three),
; the timer word only by `tick` (not in the group), and four bytes at uneven gaps read by `misc` (negative: no
; array, no group).
area    equ $8100
x       equ area+30
y       equ area+31
dir     equ area+32
timer   equ area+33
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,source
        ld de,area
        ld bc,128
        ldir
main:
        call colours
        call move
        call tick
        call misc
        jr main
colours:
        ld a,(area+0)
        ld b,a
        ld a,(area+6)
        add a,b
        ld b,a
        ld a,(area+12)
        add a,b
        ld b,a
        ld a,(area+18)
        add a,b
        ret
move:
        ld a,(dir)
        ld b,a
        ld a,(x)
        add a,b
        ld (x),a
        ld a,(y)
        sub b
        ld (y),a
        ret
tick:
        ld hl,(timer)
        inc hl
        ld (timer),hl
        ret
misc:
        ld a,(area+40)
        ld b,a
        ld a,(area+42)
        add a,b
        ld b,a
        ld a,(area+45)
        add a,b
        ld b,a
        ld a,(area+49)
        add a,b
        ret
        ds $8100-$
        ds 128
source: db 4,1,1,1,1,1, 5,1,1,1,1,1, 6,1,1,1,1,1, 7,1,1,1,1,1, 8,1,1,1,1,1
        db 10,20,1,0,0
        ds 93,$5A
