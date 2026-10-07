;Every XAS construct the XAS -> sjasmplus conversion handles
        ORG #6000
port    EQU #FE
lab     EQU #1234
six     EQU 6
start   LD A,(IX)
        LD (IY+0-3),A
        LD B,(IX+7)
        EX AF,AF'
        LD A,IXH
        LD IYL,B
        PUSH HL
        PUSH IX
        PUSH AF
        POP AF
        POP IX
        POP HL
        OUT (port),A
        IN A,(port)
        OUT (C),A
        IN E,(C)
        JR NZ,start
        JP (HL)
        DJNZ start
        LD HL,(lab)
        LD (lab+2),A
        DW (2+3)*4,10-2-3,(100&#FFFF)/(7&#FFFF),(0-7&#FFFF)/(2&#FFFF),300*300,0
        DW lab^#FF,(((lab&#FFFF)<<1|(lab&#FFFF)>>>(16-1))&#FFFF),(((lab&#FFFF)>>>1|(lab&#FFFF)<<(16-1))&#FFFF),((((((lab&#FFFF)<<1|(lab&#FFFF)>>>(16-1))&#FFFF)&#FFFF)<<1|((((lab&#FFFF)<<1|(lab&#FFFF)>>>(16-1))&#FFFF)&#FFFF)>>>(16-1))&#FFFF),low lab,high lab
        DW low high lab,low (lab+six),high (1+lab)
        DW six-lab
        DW (lab&#FFFF)/(six&#FFFF)
        DW 0
        DW $
        DW start+1
        DW $
        LD HL,16706
        DB 'AB'
        DW 'A',%101,#10*#10
        DB 'ABC',1,'A',six
        DB 'XY'
        DB 'Z!'
        DB '"'
        DS 3,0
        DUP 2
        DB low #AB12,high #AB12
        EDUP
        DB low #AB12
        DUP 3
        DB 1
        EDUP

        DB 2

        IF 0
        DB 3
        ENDIF
        IF (six)!=0
        DB 4
        ENDIF
        IF (six)==0
        DB 5
        ENDIF
longlabelname EQU 9
        DB longlabelname,longlabelname
__xas_work=#9000-$
        DISP #9000
w1      DW w1
; ENT (XAS: Run starts here)
        ENT
        ORG #6100
        DISP #6100+__xas_work
w2      DW w2
        ENT
w3      DW w3
        DB #77
