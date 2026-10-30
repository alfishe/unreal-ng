; A pointer through the alternate register set (Hungry Horace's sprite drawing): LD HL,frames + frame * 8, EX DE,HL,
; a callee that runs PUSH HL / EXX / POP HL / EXX, then EXX around work on HL' before LD A,(DE). The pointer use
; still names LD HL,frames, so the draft gives it the label (no @keep). Negative: LD BC,frames used as a count.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,(frame)
        inc a
        and 3
        ld (frame),a
        rlca
        rlca
        rlca
        ld c,a
        ld b,0
load:
        ld hl,frames
        add hl,bc
        ex de,hl
        call swap
        exx
        ld hl,$4000
        exx
        ld a,(de)
        ld ($4000),a
count:
        ld bc,frames
        ld a,b
        add a,c
        ld ($4001),a
        jr main
swap:
        push hl
        exx
        pop hl
        inc l
        exx
        ret
frame:  db 0
frames: db 1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32
