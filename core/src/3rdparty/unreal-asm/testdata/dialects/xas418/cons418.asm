;XAS 4.18: the constructs of its version
        ORG #6000
port    EQU #FE
lab     EQU #1234
start   LD A,(IX)
        LD (IY+0-3),A
        EX AF,AF'
        LD A,IXH
        JR NZ,start
        DW (2+3)*4
        DW (0-7&#FFFF)/(2&#FFFF)
        DW lab^#FF
        DW (lab&#FFFF)/(7&#FFFF)
        DW $
        DW %101
        DB 1,'A',lab-#1230
        DB 'TEXT'
        DS 3,0
        DUP 1
        DB low #AB12,high #AB12
        EDUP
        DB low #AB12
        OUT (#FE),A

        DB 2

        IF 0
        DB 3
        ENDIF
longlabelname EQU 9
        DB longlabelname
__xas_work=#9000-$
        DISP #9000
w1      DW w1
        ENT
        ORG #6080
        DISP #6080+__xas_work
w2      DW w2
        ENT
w3      DW w3
        DB #77
