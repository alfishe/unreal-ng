; GENS constructs the GENS -> sjasmplus conversion handles
;*D+
        ORG #C000
;ENT $ (GENS' run address)
START   LD A,(4+5)*3-8
        LD HL,(((#8000&#FFFF)^#8000)-#8000)/2
        LD DE,(((0-7&#FFFF)^#8000)-#8000)/2
        LD BC,(((0-7&#FFFF)^#8000)-#8000)%2
        DW (((60000&#FFFF)^#8000)-#8000)/2,(((#FFFF+2&#FFFF)^#8000)-#8000)/2
        DW 17|%1000,%1001101^%1011,#F0F0&#FF00,#3456%#1000
        DW 4480,2345/7-1,'y'-';'+7
        DB 'A'+128,-1,'"'
        DW $,$+2,$+4
        DB $&#FF,$+1&#FF
LONGNAME1 NOP
        DW LONGNAME1,LONGNAME1
L_a     NOP
L_L_1_  NOP
L_two_5 NOP
        DW L_a,L_L_1_,L_two_5
X       EQU 2
        IF X-2
        DB 1
        ELSE
        DB 2
        ENDIF
        IF X
        DB 3
        ENDIF
        MACRO MOVE _g0,_g1,_g2
        LD HL,_g0
        LD DE,_g1*2
        DB 2*_g2
        ENDM
        MACRO NSUB
        OR A
        SBC HL,DE
        ENDM
        MOVE START,+(START+1),+(1+1)
        MOVE 3,4,5
        NSUB
        DB 'abc'
        DB 'd;e'
        DS 3
        LD A,(IX+5)
        LD (IY-3),B
        LD A,(IX)
        EX AF,AF'
        JR $+2
        DJNZ START
        JP (HL)
        IN A,(C)
        OUT (254),A
        RET NZ
        RET
END1    DB 0                    ;last
