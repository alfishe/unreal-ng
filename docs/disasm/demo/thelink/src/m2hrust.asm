mOT=#A000
        ORG #FB00
        DISP 6>4
        CALL mCLS
        LD B,3
        LDIR
        LD HL,#1110,BC,32765,DE,#2EC,A,19
        CALL mCL,mCL
        INC A
        CALL mCL,mCL
        LD A,H
        CALL mCL
        OUT (C),L
        EXX
        LD DE,mTO,BC,6,HL,-6
        LDIR
        LD DE,(mTEK)
        CALL mPSK
        LD HL,(mTEK),A,(HL)
        CALL mPBY
        INC HL
        CALL mCRUNGO,mPNTT
        LD B,6,A,#66
        ADD A,A
        CALL mPBI
        DJNZ $-4
        LD A,B
        CALL mPBY
        CALL NC,mP0
        JNC $-3
        LD HL,(mPBIB),DE,-mTO
        ADD HL,DE
        RET
mCRUN0CALL mPSK
        LD HL,(mTEK),DE,-6
        INC HL
        EXD
        OR A
        SBC HL,DE
        JP Z,mOU
        LD (mTEK),DE,A,H
        CP 16
        JNC $+5
        LD (mMAXLN),HL
        LD A,(mLN)
        CP 2
        JC mNLQ
        LD IX,(mLZfu),BC,(mLZaf),HL,(mSME)
        PUSH BC,HL,IX,AF
        CALL mPSK
        POP BC,IX,HL,DE
        LD A,(mLN)
        SCF
        SBC A,B
        JZ mLNGn2
        JP NC,mLNGER
mNLNGERLD A,B,(mLN),A,(mSME),HL
        LD (mLZaf),DE,(mLZfu),IX
        EXX
        OUT (C),H
        EXX
        LD HL,(mN+4)
mSPoKY=$+1
        LD (0),HL
        EXX
        OUT (C),L
        EXX
mNLQLD HL,(mTEK)
        DEC HL
        LD (mTEK),HL
mNLNGQCALL mOU
mCRUNGOLD (mTEK),HL,DE,-6
        EXD
        OR A
        SBC HL,DE
        RET Z
        LD A,H
        CP 16
        JNC $+5
        LD (mMAXLN),HL
        CP #F0
        RET NC
        PUSH DE
        LD A,R
        ADD A,A
        CALL Z,mPR
        POP DE
        JP mCRUN0
mLNGn2LD A,H
        CP #E2
        JC mLNGER
        LD A,(mSME+1)
        CP #E2
        JC mNLNGER
mLNGERLD HL,(mTEK)
        DEC HL
        LD (mTEK),HL
        CALL mNPK
        LD (mTEK),HL
        JR mNLNGQ
mPSK
        LD (mPSKSP),SP
        LD A,62,(mLZj),A,HL,m3CP,(mLZe),HL
        XOR A
        LD (mLN),A,HL,255,(mLZfu),HL,HL,(mMAXLN)
        OR H
        DEC HL,HL
        LD (mMAXLNm2),HL
        JNZ mOISKU
        INC L
        JP Z,mN
        DEC L
        JNZ $+8
        LD HL,mN,(mLZe),HL
mOISKULD BC,(mPBIB),HL,#8001
        ADD HL,DE
        JNC $+8
        PUSH HL
        SBC HL,BC
        JNC $+4
        POP HL
        PUSH BC
        POP IX
        LD A,(DE)
        RLCA
        LD C,A
        INC DE
        LD A,(DE),(mLZb2),A
        RRCA
        XOR C
        LD L,A
        XOR C
        OR #E0
        LD H,A
        ADD HL,HL
        LD (mSPoKY),HL
        EXX
        OUT (C),H
        EXX
        LD SP,HL
        POP HL
        PUSH DE
        LD (mN+4),HL
        EXX
        OUT (C),L
        EXX
        EXD
        LD (mN+1),HL,(mLZaf),HL
        INC HL
        LD (mLZo3),HL
        EXD
        JR mLZ
m3CPLD A,(DE)
        CP (HL)
        JNZ mBAd3
mMAXLNm2=$+1
        LD BC,0,(mA3),HL
mCPILD A,(DE)
        CPI
        JNZ mCPIQ
        INC DE
        JP PE,mCPI
        JR mCPImx
mCPIQDEC HL
        INC BC
mCPImxLD (mCuR),HL
mMAXLN=$+1
        LD HL,#FFF
        XOR A
        SBC HL,BC
        CP H
        JNZ mBiG
        OR L
mCuR=$+1
        LD HL,0
        SBC HL,DE
        DEC A
mLN=$+1
        CP 0
        JC mLZg
        JNZ mLZM
        LD B,A
        LD A,(mSME+1)
        CP #E2
        JC $+7
        LD A,H
        CP #E2
        JC mLZg
        LD A,B
mLZMINC A
        CP 4
        JNC mLZb4
        LD B,A,A,H
        CP #E3
        JC mLZg
        LD A,B
mLZb4LD (mLN),A,(mSME),HL,(mLZaf),DE
mLZo3=$+1
mLZgLD DE,0
mA3=$+1
        LD HL,0
mBAd3DEC HL
mBAd2ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H,6,H
        EXX
        CP D
        SBC A,E
        OUT (C),A
        EXX
        LD SP,HL
        POP HL
        EXX
        OUT (C),L
        EXX
mLZLD A,HX
        CP H
        JNC mWi7
mLZb2=$+1
mWiLD A,0
        CP (HL)
        JP NZ,mBAd2
        INC HL
mLZjJR mLZe-1
m2cPSBC HL,DE
        LD A,H
        INC A
        JNZ mNsh
        LD A,2,(mLN),A,(mSME),HL,(mLZaf),DE
mNshLD A,24,(mLZj),A
        ADD HL,DE
mLZe=$+1
        JP m3CP
mBiGLD (mLZfu),HL
        LD HL,(mCuR)
        XOR A
        SBC HL,DE
        DEC A
        LD (mLN),A,(mSME),HL,(mLZaf),DE
        JR mN

mWi7JNZ mN
        LD A,L
        CP LX
        JP NC,mWi
mNLD HL,0,DE,0
        ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H,6,H
        EXX
        CP D
        SBC A,E
        OUT (C),A
        EXX
        LD SP,HL
        POP HL
        PUSH DE
        EXX
        OUT (C),L
        EXX
mPSKSP=$+1
        LD SP,0,A,(mLN)
        OR A
        RET NZ
        LD A,(mNTLN)
        CP #C
        RET NC
        LD HL,(mTEK),D,H,E,L,A,(HL)
        DEC HL
        LD BC,8
        CPDR
        RET NZ
        INC HL
        LD BC,begin-end
        XOR A
        SBC HL,BC
        RET C
        ADD HL,BC
        SBC HL,DE
        INC A
        LD (mLN),A,(mSME),HL
        RET
mFIPOLD A,(mLN)
        CP -1
        RET NC
        LD (mFISP),SP
        DEC A
        LD LX,A
mTEK=$+1
        LD BC,begin-end
mFIP1INC BC
        LD H,B,L,C,A,(HL)
        RLCA
        LD D,A
        INC HL
        LD A,(HL)
        RRCA
        XOR D
        LD E,A
        XOR D
        OR #E0
        LD D,A
        EXD
        ADD HL,HL
        EXX
        OUT (C),H
        EXX
        LD SP,HL
        POP HL
        PUSH DE
        EXD
        ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H,6,H
        EXX
        CP D
        SBC A,E
        OUT (C),A
        EXX
        LD SP,HL
        POP HL
        PUSH DE
        EXX
        OUT (C),L
        EXX
        DEC LX
        JNZ mFIP1
mFISP=$+1
        LD SP,0
        RET

mOULD A,(mLN)
        AND A
        JP Z,mNPK
        CALL mPNTT
        LD A,(mLN)
        CP 2
        JZ mP2
        JNC mPb2
        CALL mP000
        LD A,(mSME)
        RRA
        RRA
        RRA
        DUP 3
        CALL mPBI
        RLA
        EDUP
        LD HL,(mTEK)
        INC HL
        RET

mP2CALL mP0,mP01
        LD A,(mSME)
        CALL mPBY
        JP mPq
mPb2LD A,(mLN)
        CP 3
        JNZ mPb3
        CALL mP01,mP0
        JR mPsm
mPb3CP #F
        JNC mPbF
        CALL mP0
        SUB 3
        CALL mP1,mP1
        SUB 3
        JNC $-8
        ADD A,3
        RRA
        RRA
        CALL mPBI
        RLA
        CALL mPBI
        JR mPsm
mPbFJNZ mPnF
        CALL mP0
        LD B,#A
        CALL mP1
        DJNZ $-3
        JR mPsm
mPnFLD A,#66
        ADD A,A
        CALL mPBI
        ADD A,A
        JNZ $-4
        LD A,(mLN)
        CP -1
        JZ mPm1
        CALL mPBY
        JR mPsm
mLZfu=$+1
mPm1LD BC,0,A,B
        OR A
        CALL NZ,mPBY
        LD A,C
        CALL mPBY
mSME=$+1
mPsmLD HL,0,A,H
        INC A
        JNZ mNff
        CALL mP1
        LD A,L
        CALL mPBY
        JP mPq
mNffCALL mP0
        LD A,H
        CP -3
        JNC mBfd
        CP -7
        JNC mBf9
        CP -15
        JC mP4b
        CALL mP01
        SUB -7
        RRA
        RRA
        RRA
        JR m3BiT+1
mBfdCALL mP1,mP1
        INC A
        RRA
        CALL mPBI
        JR mPl
mBf9CALL mP1,mP0
        SUB -3
        RRA
        RRA
        CALL mPBI
        RLA
        CALL mPBI
        LD HL,(mSME)
        JR mPl
mP4bCP #E2
        JC mP8b
        CALL mP00
        SUB -#F
        DUP 4
        RRA
        EDUP
        CALL mPBI
m3BiTDUP 3
        RLA
        CALL mPBI
        EDUP
        JR mPl
mP8bCALL mP000,mP000,mPBY
mPlLD A,L
        CALL mPBY
mPqCALL mFIPO
mLZaf=$+1
        LD HL,0
        RET
mNTLN=$+1
mNPKLD A,0
        OR A
        JNZ $+8
        LD HL,(mTEK),(mPNTg+1),HL
        INC A
        LD (mNTLN),A
        CP 42
        CALL Z,mPNTg
        LD HL,(mTEK)
        INC HL
        RET
mPNTTLD A,(mNTLN)
        OR A
        RET Z
mPNTgLD HL,0,B,A
        CP #C
        JC m1BYs
        LD A,#62
        ADD A,A
        CALL mPBI
        ADD A,A
        JNZ $-4
        LD A,B
        SUB #C
        DUP 5
        RRA
        EDUP
        DUP 4
        CALL mPBI
        RLA
        EDUP
        PUSH AF
        RES 0,B
        LD A,(HL)
        CALL mPBY
        INC HL
        DJNZ $-5
        XOR A
        LD (mNTLN),A
        POP AF
        RET NC
        CALL mP1
        LD A,(HL)
mPBYPUSH HL
mPBIB=$+1
        LD HL,mTO+6,(HL),A
        INC HL
        LD (mPBIB),HL
        POP HL
        RET
m1BYsCALL mP1
        LD A,(HL)
        CALL mPBY
        INC HL
        DJNZ m1BYs
        XOR A
        LD (mNTLN),A
        RET
mP000CALL $+6
mP00CALL $+3
mP0OR A
        JR mPBI
mP01CALL mP0
mP1SCF
mPBIPUSH HL
        LD HL,mPBIC
        DEC (HL)
        JZ mPBIZ
        INC (HL)
        RL (HL)
        POP HL
        RET NC
        PUSH AF
mPBIC=$+1
        LD A,1
mBITAD=$+1
        LD (0),A,A,1,(mPBIC),A
        POP AF
        RET
mPBIZINC (HL)
        RL (HL)
        LD HL,(mPBIB),(mBITAD),HL
        INC HL
        LD (mPBIB),HL
        POP HL
        RET
mCLOUT (C),A
        INC A
        EXX
        LD HL,3>2
        LD (HL),0
        INC L
        JNZ $-3
        INC H
        JNZ $-6
        EXX
        RET
mCLSLD HL,4>4,BC,6144,(HL),L
        LD D,H,E,L
        INC DE
        LDIR
        RET
mPRPUSH HL
        LD A,L,L,57
        CALL $+4
        POP AF
        PUSH AF
        CALL mPr
        POP AF
        DUP 4
        RRA
        EDUP
mPrADD A,A,A,A,A,A
        OR #80
        LD D,61
        CP #D0
        JC $+5
        SUB #C8
        INC D
        LD E,A,B,6
mpRLD H,89
        INC E
        LD A,(DE)
        ADD A,A
        LD C,A
        DUP 6
        SLA C
        SBC A,A
        LD (HL),A
        INC L
        EDUP
        LD C,26
        ADD HL,BC
        DJNZ mpR
        LD C,56
        ADD HL,BC
        RET
mTOENT
        ORG #5D3B
        DW 256,0,#C0F9,#E30,0
        DW $+2
        LD SP,6>4,HL,mOT
        XOR A
        OUT (-2),A
        PUSH HL
secsz=$+2
        LD BC,5,DE,(23796)
        CALL #3D13
        LD DE,begin,HL,GO
        EX (SP),HL
        PUSH DE
        LD DE,6
        ADD HL,DE
        PUSH HL
        EXX
        POP HL,DE
        LD B,6
        DEC HL
        LD A,(HL)
        PUSH AF
        DJNZ $-3
        EXX
        LD DE,#1003,C,#80
HloopLD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
HxxtbitEXX
HnxtbitSLA C
        CALL Z,CHL
        JC Hloop
        LD B,1
H2bitLD A,#40
        SLA C
        CALL Z,CHL
        RLA
        JNC $-6
        CP E
        JC H2lessE
        ADD A,B
        LD B,A
        XOR D
        JNZ H2bit
H2lessEADD A,B
        CP 4
        JZ Hnpaked
        ADC A,-1
LL5DB1CP 2
LL5DB3EXX
        LD C,A,H,-1
        EXX
        JC H3bit
        JZ LL5DE5
        SLA C
        CALL Z,CHL
        JC LL5DE5
        LD A,#7F,B,E
        DJNZ Hbits
LL5DCBDJNZ Hplain
        LD B,A
        SBC A,A
HijnzSLA C
        CALL Z,CHL
        RLA
        DEC A
        INC B
        JNZ Hijnz
        CP #E1
        JNZ $+4
        LD A,(HL)
        INC HL
        EXX
        LD H,A
        EXX
LL5DE5LD A,(HL)
        INC HL
LL5DE7EXX
        LD L,A
        ADD HL,DE
        LDIR
        JR Hxxtbit
HplainADD A,6
        RLA
        LD B,A
HldirLD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
        EXX
        DJNZ Hldir
        JR Hnxtbit
HnpakedSLA C
        CALL Z,CHL
        LD A,D
        JNC Hbits
        LD A,(HL)
        INC HL
        CP D
        JNC LL5DB1
        OR A
        JZ Hquit
        EXX
        LD B,A
        EXX
        LD A,(HL)
        INC HL
        JR LL5DB3
H3bitLD A,#3F
HbitsSLA C
        CALL Z,CHL
        RLA
        JNC Hbits
        DJNZ LL5DCB
        JR LL5DE7
HquitEXX
        LD B,6
        POP AF
        LD (DE),A
        INC DE
        DJNZ $-3
CHLLD C,(HL)
        INC HL
        RL C
        RET
ENDORG 23641
        DW END+1
        ORG $+6
        DW END+3,END+15,END+15
        ORG 23627
        DW END
        ORG #5B00
nenadoIFN ?make
        CALL 8026
        JP C,GO
        ENDIF
        LD BC,#5FFF
        CALL 7863
        LD HL,#FB00,DE,4>4,B,5
        PUSH BC,DE
        LDIR
        LD HL,end-1,DE,-1,BC,end-begin
        LDDR
        POP HL,BC
        LD DE,6>4
        LDIR
        DI
        CALL 6>4
        PUSH HL
        CALL mPR
        LD C,18
        CALL #3D13
        POP BC
        DEC BC
        INC B
        LD A,B,(secsz),A
        PUSH BC
        LD C,12,HL,1,(#5CD1),HL
        CALL #3D13
        LD HL,4>4,D,L,E,L,BC,#905
        CALL #3D13
        POP BC
        LD HL,mTO,C,6,DE,(#48E1)
        PUSH BC
        CALL #3D13
        POP BC
        LD HL,(23796),(#48E1),HL,HL,(#48E5)
        PUSH BC
        XOR A
        LD C,B,B,A
        SBC HL,BC
        LD C,10,(#48E5),HL
        CALL #3D13
        LD L,C,H,4
        DB ",))))+++
        POP BC
        INC B
        LD (HL),B,HL,4>4,D,L,E,L,BC,#906
        CALL #3D13,mCLS
        LD HL,4867
        PUSH HL
        LD BC,15,DE,5566,HL,(23631)
        ADD HL,BC
        EXD
        LD C,4
        LDIR
        LD HL,7030
        PUSH HL
        LD HL,RNBAS2
        PUSH HL
        LD HL,#2970
        PUSH HL
        LD L,32
        PUSH HL
        LD L,74
        PUSH HL
        JP 15663
RNBAS2LD HL,1,(23618),HL,(IY+10),H
        RET
        DISPLAY "RUN[CS+Ent]
