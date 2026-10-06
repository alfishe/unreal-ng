; ALASM constructs and their sjasmplus form (unreal-asm A5 test data)
        ORG #6000
start   LD A,(1+2)*3            ;left to right: (1+2)*3 = 9
        LD HL,high table        ;'x is the high byte
        LD L,low table          ;.x is the low byte
        LD DE,((0+(65536-46))*98&#FFFF)/(256&#FFFF) ;16-bit unsigned arithmetic: #EE
        LD BC,+(((#1234&#FFFF)<<4|(#1234&#FFFF)>>>(16-4))&#FFFF) ;< rotates a 16-bit word left
        LD A,5^3                ;! is xor
        EX AF,AF'
        EX DE,HL
        JR NZ,start
L_iy    EQU 7
        LD IY,L_iy              ;a lower-case name is a label, not the register

loop__L1 DJNZ loop__L1
@shared NOP


loop__L2 DJNZ loop__L2

        JP @shared
        MACRO FILL _arg0,_arg1
        LD A,_arg0
        LD (_arg1),A
        ENDM
        FILL 1,#4000
        FILL 2,#4001
; (expanded at its calls)         MACRO PAIR
; (expanded at its calls) lbl\0   DB \1
; (expanded at its calls)         ENDM
; macro PAIR expanded
lbl1    DB 10
; macro PAIR expanded
lbl2    DB 20
; (expanded at its calls)         MACRO SKIP
; (expanded at its calls) _=\P
; (expanded at its calls)         DB \0,\9\R
; (expanded at its calls)         ENDM
; macro SKIP expanded
_=0
        DB 1,10
        IF exist start
        DB 1
        ELSE
        DB 0
        ENDIF
        DUP 3
        NOP
        EDUP
count=3
__repeat1=1
        WHILE __repeat1
        DB count
count=count-1
__repeat1=(count)!=0
        ENDW
        DUP 4
        DB #AA,#55
        EDUP
        DISPLAY 'end: ',/H,$
table   DB 'AB',0
        LD L,0
        LD H,1
