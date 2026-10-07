; Layout rules checked on sjasmplus 1.24: local labels after EQU / DEFL / @labels, DEFL's last value, macro
; arguments inside names, temporary labels, MODULE, DISP, IF, 32-bit C arithmetic with true = -1, DUP, a label on
; an IF line, a macro named like an instruction, {address} reading what DW wrote
        ORG #8000
main    nop
.loop   nop
CONST   EQU 5
.afterequ nop
var     = 3
.afterdefl nop
var     = 4
@glob   nop
.afterglob nop
m       MACRO x
.ml     ld a,x
mg_x    nop
        ENDM
        m 1
1       nop
        jr 1B
        jr 1F
1       nop
        MODULE mod
inmod   nop
.l      nop
        ENDMODULE
        DISP #C000
disp1   nop
        ENT
after   nop
        IF after > #8000
ifyes   nop
        ENDIF
cmp     EQU (1<2)
nott    EQU !0
shra    EQU -16>>1
shrl    EQU -16>>>1
divn    EQU -7/2
modn    EQU -7%2
big     EQU #FFFFFFFF+2
        DUP 3
        nop
        EDUP
enddup  nop
        INCLUDE "probepart.asm"
inbin   INCBIN "probe.bin",2,3
afterbin
        ALIGN 16
aligned DZ "ab"
        DC "cd"
dd4     DD 1,2
        IFDEF FLAG
noflag  nop
        ELSE
flag0   nop
        ENDIF
        DEFINE FLAG
        IFDEF FLAG
flag1   nop
        ENDIF
cnt     = 0
        WHILE cnt < 3
cnt     = cnt + 1
        db cnt
        ENDW
endwhile
onif    IF 1
        nop
        ENDIF
        MACRO push x
        DW x
        ENDM
pushed  push #1234
        DEVICE ZXSPECTRUM48
        ORG #9000
word    DW #5678
read    = {word}
