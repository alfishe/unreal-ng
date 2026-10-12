; sjasmplus' structure words a retro target has not (asm-synchronizer.md §15.3, Z5): STRUCT, MODULE, ALIGN, DZ / DC /
; D24 / DD / ABYTE, IFUSED, DEFINE with a value, EXA / EXD, a temporary label and a string argument in a macro
        DEFINE  COUNT 3
        STRUCT POS
x       BYTE 1
y       BYTE 2
        ENDS
        STRUCT SPR,2
p       POS
w       WORD #1234
        BLOCK 3,#E5
n       D24 #ABCDEF
        ENDS
        MACRO   PUT text
        DB text
        ENDM
        MACRO   WAIT n
        LD B,n
1       DJNZ 1b
        ENDM
        MACRO   SHOW
        LD HL,greeting
        ENDM
        ORG #8000
s1      SPR
s2      SPR {5,6},#5678
s3      POS 9
        DW POS, SPR, SPR.p.y, SPR.w, s2.w, s2.p.x, POS.y
        MODULE main
start   LD HL,data
        CALL helper
        SHOW
        WAIT COUNT
        WAIT 2
        EXA
        EXD
        JR start
helper  RET
greeting DZ "hi"
data    DC "abc"
        D24 #123456
        DD #89ABCDEF
        ABYTE 1 "AB"
        ENDMODULE
        ALIGN 16
aligned PUT "needs a"
        IFUSED main.helper
        DB #11
        ENDIF
        IFUSED unused
        DB #22
        ENDIF
        DW main.start, main.data, aligned
