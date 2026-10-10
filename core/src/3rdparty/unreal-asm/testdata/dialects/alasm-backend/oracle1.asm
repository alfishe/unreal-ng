; sjasmplus constructs written as ALASM 5.07 by the ALASM backend (part B, Z0) and assembled by ALASM 5.09 in
; unreal-ng: the bytes must equal what sjasmplus builds from this file (from #6000)
        ORG #6000
SIZE    EQU 10
COUNT   EQU SIZE*3+2            ; C priorities: 32
start:  ld a,1+2*3              ; 7, not 9
        ld hl,(1+2)*3           ; a value in parentheses, not memory
        ld de,(table)           ; memory
        ld a,high table         ; table is further down: written as its value
        ld a,low (table+1)
        ld bc,table+COUNT       ; a later label in + and -: kept
        ld a,COUNT>>1
        ld hl,COUNT<<4
        ld a,COUNT%7
        ld a,COUNT/3
        ld a,-2/2               ; 32-bit signed: -1
        ld a,~SIZE & #0F
        ld a,SIZE ^ 3
        ld a,(ix+5)
        ld (iy-3),b
        ld a,(ix+SIZE-2)
        ex af,af'
        ld a,ixh
        ld iyl,a
        sll b
        in f,(c)
        out (c),0
        rlc (ix+4),b            ; undocumented copies: bytes
        set 3,(iy-1),a
        sub a,b                 ; sjasmplus: SUB A then SUB B
        push af,bc,de
        pop de,bc,af
        ld bc,de                ; fake instructions
        ld hl,(ix+2)
        ld (iy+6),de
        sub hl,bc
main    nop
.loop   djnz .loop
        jr .skip
        nop
.skip   nop
1       jr 1B
        jr 1F
1       nop
        IF SIZE > 5
        db 1
        ELSE
        db 2
        ENDIF
        IF SIZE == 10
        db "ten",0
        ENDIF
        IF SIZE != 10
        db "not",0
        ENDIF
        IF exist start && SIZE < 20
        db 3
        ENDIF
        dw SIZE*256+1, -1, $, table-start
        ds 4,#AA
        DUP 3
        db #55
        EDUP
        MACRO WAIT n
        ld b,n
.w      djnz .w
        ENDM
        WAIT 5
        WAIT SIZE*2
n       = 0
        WHILE n < 3
        db n
n       = n+1
        ENDW
        db 'A', "B", 'it''s', 13
        dw 'AB'
table   db 1,2,3,4
tail    EQU $
        dw tail
