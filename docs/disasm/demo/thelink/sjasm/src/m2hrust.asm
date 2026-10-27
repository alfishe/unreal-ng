; =====================================================================================================================
; m2hrust.asm - the author's Hrust packer and release maker (included at the end of THELINK.asm; NOT used by the demo)
; =====================================================================================================================
;
; What it does
;   Makes a single-file release of a program assembled in memory (begin..end, here the #7800 main block of
;   The Link): it packs begin..end with an LZ packer in the Hrust format and writes to the TR-DOS disk one BASIC
;   file, named by the 9 bytes at #5CDD (THELINK.asm puts 'THE LINK' B there): a one-line BASIC program with the
;   depacker, followed on the disk by the packed data. Loading and running that file reads the data, depacks it to
;   begin and jumps to GO. It packs only begin..end, so for The Link (whose effects live in 28 page files) it
;   could not make a release on its own; the release disk uses ../loader/loader.asm instead.
;
; Layout (three pieces, assembled into three places)
;   1. the packer: ORG #FB00 (stored in the last page selected by THELINK.asm, the music page), DISP #6000 - it
;      runs at #6000 (ALASM `DISP 6>4`: 6 rotated right by 4 = #6000, which the converter spelled out);
;      entry #6000 (its first byte), output buffer mTO right after its code;
;   2. #5D3B (PROG): the BASIC line with the depacker, and the BASIC system variables E_LINE, WORKSP, STKBOT,
;      STKEND, VARS written directly so that the program ends at END;
;   3. #5B00 (nenado): the "make" entry.
;
; make (#5B00), step by step
;   - without a `make` label (as in THELINK.asm) it first tests CAPS SHIFT (ROM #1F5A): not pressed -> JP GO (just
;     run the program); pressed -> make the release. (SAVEOBJ4.asm has the same convention: "GO = where to go
;     without CAPS"; ALASM's DISPLAY says 'RUN[CS+Ent]'.)
;   - RAMTOP = #5FFF (ROM CLEAR at #1EB7); the packer #FB00 -> #4000 (screen as a temporary place); begin..end
;     moved up to end at #FFFF (LDDR); the packer #4000 -> #6000; DI; CALL #6000 (pack; HL = packed length);
;   - prints the length (mPR), deletes the old file (TR-DOS #3D13, C = 18, as in SAVEOBJ4.asm), patches the
;     sector count into the depacker (secsz), saves the BASIC (C = 12), writes the packed data to the first free
;     sectors (C = 6), updates the disk info sector and the file's catalog entry (sector count = 1 + data
;     sectors), and returns to BASIC at line 1 of the new program (still in memory).
;
; Packer working memory: RAM pages 1, 3, 4, 6, 7 (#7FFD #11, #13, #14, #16, #17) at #C000 are cleared (mCL) and
; used as the match-search tables; page 0 (#10) is left at #C000 at the end. Interrupts must be off (DI): it
; uses SP to read the tables.
;
; Converter additions: ALASM's `n>m` (rotate right) written out as `(((n&#FFFF)>>>m|...)&#FFFF)`, multi-operand
; lines split, JNZ/JC/... as JR, `IF !exist make` for ALASM's `IFN ?make`. Labels are the author's (mPSK - search,
; mOU - output, mNPK - unpacked byte, mPBI/mPBY - put bit/byte).
; =====================================================================================================================
mOT=#A000
        ORG #FB00
        DISP (((6&#FFFF)>>>4|(6&#FFFF)<<(16-4))&#FFFF)
; ---------------------------------------------------------------------------------------------------------------------
; Packer entry (#6000). Input: begin..end moved to end at #FFFF. Output: mTO.. ; first the last 6 bytes of the
; block (the depacker keeps them aside), then the packed stream. Returns HL = output length.
; ---------------------------------------------------------------------------------------------------------------------
        CALL mCLS
        LD B,3
        LDIR
        LD HL,#1110
        LD BC,32765
        LD DE,#2EC
        LD A,19
        CALL mCL
        CALL mCL
        INC A
        CALL mCL
        CALL mCL
        LD A,H
        CALL mCL
        OUT (C),L
        EXX
        LD DE,mTO
        LD BC,6
        LD HL,-6
        LDIR
        LD DE,(mTEK)
        CALL mPSK
        LD HL,(mTEK)
        LD A,(HL)
        CALL mPBY
        INC HL
        CALL mCRUNGO
        CALL mPNTT
        LD B,6
        LD A,#66
        ADD A,A
        CALL mPBI
        DJNZ $-4
        LD A,B
        CALL mPBY
        CALL NC,mP0
        JR NC,$-3
        LD HL,(mPBIB)
        LD DE,-mTO
        ADD HL,DE
        RET
; ---------------------------------------------------------------------------------------------------------------------
; Main loop: search a match at the current position (mPSK), compare with the match one byte later (lazy
; matching: a longer one there makes the current byte a literal), output (mOU), advance (mCRUNGO).
; Occasionally (when R*2 = 0) prints the bytes left (mPR).
; ---------------------------------------------------------------------------------------------------------------------
mCRUN0  CALL mPSK
        LD HL,(mTEK)
        LD DE,-6
        INC HL
        EX DE,HL
        OR A
        SBC HL,DE
        JP Z,mOU
        LD (mTEK),DE
        LD A,H
        CP 16
        JR NC,$+5
        LD (mMAXLN),HL
        LD A,(mLN)
        CP 2
        JR C,mNLQ
        LD IX,(mLZfu)
        LD BC,(mLZaf)
        LD HL,(mSME)
        PUSH BC
        PUSH HL
        PUSH IX
        PUSH AF
        CALL mPSK
        POP BC
        POP IX
        POP HL
        POP DE
        LD A,(mLN)
        SCF
        SBC A,B
        JR Z,mLNGn2
        JP NC,mLNGER
mNLNGER LD A,B
        LD (mLN),A
        LD (mSME),HL
        LD (mLZaf),DE
        LD (mLZfu),IX
        EXX
        OUT (C),H
        EXX
        LD HL,(mN+4)
mSPoKY=$+1
        LD (0),HL
        EXX
        OUT (C),L
        EXX
mNLQ    LD HL,(mTEK)
        DEC HL
        LD (mTEK),HL
mNLNGQ  CALL mOU
mCRUNGO LD (mTEK),HL
        LD DE,-6
        EX DE,HL
        OR A
        SBC HL,DE
        RET Z
        LD A,H
        CP 16
        JR NC,$+5
        LD (mMAXLN),HL
        CP #F0
        RET NC
        PUSH DE
        LD A,R
        ADD A,A
        CALL Z,mPR
        POP DE
        JP mCRUN0
mLNGn2  LD A,H
        CP #E2
        JR C,mLNGER
        LD A,(mSME+1)
        CP #E2
        JR C,mNLNGER
mLNGER  LD HL,(mTEK)
        DEC HL
        LD (mTEK),HL
        CALL mNPK
        LD (mTEK),HL
        JR mNLNGQ
; ---------------------------------------------------------------------------------------------------------------------
; mPSK: search the longest match for the position in DE: the hash of the next two bytes selects a table entry in
; the extra pages (page by OUT (C) from the EXX set), the chain of earlier positions is followed and compared
; (m3CP / mCPI); result in mLN (length), mSME (offset), mLZaf. Inserts the current position into the table.
; ---------------------------------------------------------------------------------------------------------------------
mPSK
        LD (mPSKSP),SP
        LD A,62
        LD (mLZj),A
        LD HL,m3CP
        LD (mLZe),HL
        XOR A
        LD (mLN),A
        LD HL,255
        LD (mLZfu),HL
        LD HL,(mMAXLN)
        OR H
        DEC HL
        DEC HL
        LD (mMAXLNm2),HL
        JR NZ,mOISKU
        INC L
        JP Z,mN
        DEC L
        JR NZ,$+8
        LD HL,mN
        LD (mLZe),HL
mOISKU  LD BC,(mPBIB)
        LD HL,#8001
        ADD HL,DE
        JR NC,$+8
        PUSH HL
        SBC HL,BC
        JR NC,$+4
        POP HL
        PUSH BC
        POP IX
        LD A,(DE)
        RLCA
        LD C,A
        INC DE
        LD A,(DE)
        LD (mLZb2),A
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
        EX DE,HL
        LD (mN+1),HL
        LD (mLZaf),HL
        INC HL
        LD (mLZo3),HL
        EX DE,HL
        JR mLZ
m3CP    LD A,(DE)
        CP (HL)
        JR NZ,mBAd3
mMAXLNm2=$+1
        LD BC,0
        LD (mA3),HL
mCPI    LD A,(DE)
        CPI
        JR NZ,mCPIQ
        INC DE
        JP PE,mCPI
        JR mCPImx
mCPIQ   DEC HL
        INC BC
mCPImx  LD (mCuR),HL
mMAXLN=$+1
        LD HL,#FFF
        XOR A
        SBC HL,BC
        CP H
        JR NZ,mBiG
        OR L
mCuR=$+1
        LD HL,0
        SBC HL,DE
        DEC A
mLN=$+1
        CP 0
        JR C,mLZg
        JR NZ,mLZM
        LD B,A
        LD A,(mSME+1)
        CP #E2
        JR C,$+7
        LD A,H
        CP #E2
        JR C,mLZg
        LD A,B
mLZM    INC A
        CP 4
        JR NC,mLZb4
        LD B,A
        LD A,H
        CP #E3
        JR C,mLZg
        LD A,B
mLZb4   LD (mLN),A
        LD (mSME),HL
        LD (mLZaf),DE
mLZo3=$+1
mLZg    LD DE,0
mA3=$+1
        LD HL,0
mBAd3   DEC HL
mBAd2   ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H
        SET 6,H
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
mLZ     LD A,IXH
        CP H
        JR NC,mWi7
mLZb2=$+1
mWi     LD A,0
        CP (HL)
        JP NZ,mBAd2
        INC HL
mLZj    JR mLZe-1
m2cP    SBC HL,DE
        LD A,H
        INC A
        JR NZ,mNsh
        LD A,2
        LD (mLN),A
        LD (mSME),HL
        LD (mLZaf),DE
mNsh    LD A,24
        LD (mLZj),A
        ADD HL,DE
mLZe=$+1
        JP m3CP
mBiG    LD (mLZfu),HL
        LD HL,(mCuR)
        XOR A
        SBC HL,DE
        DEC A
        LD (mLN),A
        LD (mSME),HL
        LD (mLZaf),DE
        JR mN

mWi7    JR NZ,mN
        LD A,L
        CP IXL
        JP NC,mWi
mN      LD HL,0
        LD DE,0
        ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H
        SET 6,H
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
        LD SP,0
        LD A,(mLN)
        OR A
        RET NZ
        LD A,(mNTLN)
        CP #C
        RET NC
        LD HL,(mTEK)
        LD D,H
        LD E,L
        LD A,(HL)
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
        LD (mLN),A
        LD (mSME),HL
        RET
; mFIPO: after a match, enter the positions it covered into the search tables
mFIPO   LD A,(mLN)
        CP -1
        RET NC
        LD (mFISP),SP
        DEC A
        LD IXL,A
mTEK=$+1
        LD BC,begin-end
mFIP1   INC BC
        LD H,B
        LD L,C
        LD A,(HL)
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
        EX DE,HL
        ADD HL,HL
        EXX
        OUT (C),H
        EXX
        LD SP,HL
        POP HL
        PUSH DE
        EX DE,HL
        ADD HL,HL
        LD A,H
        RLCA
        RLCA
        AND 3
        SET 7,H
        SET 6,H
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
        DEC IXL
        JR NZ,mFIP1
mFISP=$+1
        LD SP,0
        RET

; ---------------------------------------------------------------------------------------------------------------------
; mOU: output the current choice: a literal (mNPK collects literals into a run) or a match - its length and
; offset as variable-length bit codes (mP0/mP1/mPBI) and bytes (mPBY), as the Hrust depacker below reads them.
; ---------------------------------------------------------------------------------------------------------------------
mOU     LD A,(mLN)
        AND A
        JP Z,mNPK
        CALL mPNTT
        LD A,(mLN)
        CP 2
        JR Z,mP2
        JR NC,mPb2
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

mP2     CALL mP0
        CALL mP01
        LD A,(mSME)
        CALL mPBY
        JP mPq
mPb2    LD A,(mLN)
        CP 3
        JR NZ,mPb3
        CALL mP01
        CALL mP0
        JR mPsm
mPb3    CP #F
        JR NC,mPbF
        CALL mP0
        SUB 3
        CALL mP1
        CALL mP1
        SUB 3
        JR NC,$-8
        ADD A,3
        RRA
        RRA
        CALL mPBI
        RLA
        CALL mPBI
        JR mPsm
mPbF    JR NZ,mPnF
        CALL mP0
        LD B,#A
        CALL mP1
        DJNZ $-3
        JR mPsm
mPnF    LD A,#66
        ADD A,A
        CALL mPBI
        ADD A,A
        JR NZ,$-4
        LD A,(mLN)
        CP -1
        JR Z,mPm1
        CALL mPBY
        JR mPsm
mLZfu=$+1
mPm1    LD BC,0
        LD A,B
        OR A
        CALL NZ,mPBY
        LD A,C
        CALL mPBY
mSME=$+1
mPsm    LD HL,0
        LD A,H
        INC A
        JR NZ,mNff
        CALL mP1
        LD A,L
        CALL mPBY
        JP mPq
mNff    CALL mP0
        LD A,H
        CP -3
        JR NC,mBfd
        CP -7
        JR NC,mBf9
        CP -15
        JR C,mP4b
        CALL mP01
        SUB -7
        RRA
        RRA
        RRA
        JR m3BiT+1
mBfd    CALL mP1
        CALL mP1
        INC A
        RRA
        CALL mPBI
        JR mPl
mBf9    CALL mP1
        CALL mP0
        SUB -3
        RRA
        RRA
        CALL mPBI
        RLA
        CALL mPBI
        LD HL,(mSME)
        JR mPl
mP4b    CP #E2
        JR C,mP8b
        CALL mP00
        SUB -#F
        DUP 4
        RRA
        EDUP
        CALL mPBI
m3BiT   DUP 3
        RLA
        CALL mPBI
        EDUP
        JR mPl
mP8b    CALL mP000
        CALL mP000
        CALL mPBY
mPl     LD A,L
        CALL mPBY
mPq     CALL mFIPO
mLZaf=$+1
        LD HL,0
        RET
; mNPK: one unpacked byte: counted in mNTLN (a literal run, flushed by mPNTT at 42 bytes or before a match)
mNTLN=$+1
mNPK    LD A,0
        OR A
        JR NZ,$+8
        LD HL,(mTEK)
        LD (mPNTg+1),HL
        INC A
        LD (mNTLN),A
        CP 42
        CALL Z,mPNTg
        LD HL,(mTEK)
        INC HL
        RET
mPNTT   LD A,(mNTLN)
        OR A
        RET Z
mPNTg   LD HL,0
        LD B,A
        CP #C
        JR C,m1BYs
        LD A,#62
        ADD A,A
        CALL mPBI
        ADD A,A
        JR NZ,$-4
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
; mPBY: put a byte A into the output (mPBIB = output pointer)
mPBY    PUSH HL
mPBIB=$+1
        LD HL,mTO+6
        LD (HL),A
        INC HL
        LD (mPBIB),HL
        POP HL
        RET
m1BYs   CALL mP1
        LD A,(HL)
        CALL mPBY
        INC HL
        DJNZ m1BYs
        XOR A
        LD (mNTLN),A
        RET
; Bit writer: mP0 / mP1 put one 0 / 1 bit, mP00 / mP000 two / three zero bits, mP01 0 then 1, mPBI the carry.
; Bits are gathered 8 at a time into a byte reserved in the output stream (mBITAD), so control bits and data
; bytes are interleaved the way the depacker's CHL fetches them.
mP000   CALL $+6
mP00    CALL $+3
mP0     OR A
        JR mPBI
mP01    CALL mP0
mP1     SCF
mPBI    PUSH HL
        LD HL,mPBIC
        DEC (HL)
        JR Z,mPBIZ
        INC (HL)
        RL (HL)
        POP HL
        RET NC
        PUSH AF
mPBIC=$+1
        LD A,1
mBITAD=$+1
        LD (0),A
        LD A,1
        LD (mPBIC),A
        POP AF
        RET
mPBIZ   INC (HL)
        RL (HL)
        LD HL,(mPBIB)
        LD (mBITAD),HL
        INC HL
        LD (mPBIB),HL
        POP HL
        RET
; mCL: page A (+1 after) at #C000 and zero #C000-#FFFF
mCL     OUT (C),A
        INC A
        EXX
        LD HL,+(((3&#FFFF)>>>2|(3&#FFFF)<<(16-2))&#FFFF)
        LD (HL),0
        INC L
        JR NZ,$-3
        INC H
        JR NZ,$-6
        EXX
        RET
; mCLS: zero the screen's pixels (#4000-#57FF)
mCLS    LD HL,+(((4&#FFFF)>>>4|(4&#FFFF)<<(16-4))&#FFFF)
        LD BC,6144
        LD (HL),L
        LD D,H
        LD E,L
        INC DE
        LDIR
        RET
; mPR: print HL in hex as big digits made of attribute cells (ROM font at #3D00, each font bit -> one attribute
; byte #00 / #FF in #59xx); mPr prints the low nibble of A
mPR     PUSH HL
        LD A,L
        LD L,57
        CALL $+4
        POP AF
        PUSH AF
        CALL mPr
        POP AF
        DUP 4
        RRA
        EDUP
mPr     ADD A,A
        ADD A,A
        ADD A,A
        OR #80
        LD D,61
        CP #D0
        JR C,$+5
        SUB #C8
        INC D
        LD E,A
        LD B,6
mpR     LD H,89
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
; ---------------------------------------------------------------------------------------------------------------------
; The release file: a one-line BASIC program at #5D3B (line 1: RANDOMIZE USR with the address of the code that
; follows in the hidden number), then the depacker:
;   SP = #6000; read `secsz` sectors (patched by make) from the disk position TR-DOS left after loading the BASIC
;   (#5CF4) to mOT = #A000; keep the 6 tail bytes on the stack; depack the stream from #A006 to begin; write the
;   6 bytes; the RET of CHL (fallen into after Hquit) returns to GO, put on the stack by EX (SP),HL.
; ---------------------------------------------------------------------------------------------------------------------
mTO     ENT
        ORG #5D3B
        DW 256,0,#C0F9,#E30,0
        DW $+2
        LD SP,+(((6&#FFFF)>>>4|(6&#FFFF)<<(16-4))&#FFFF)
        LD HL,mOT
        XOR A
        OUT (-2),A
        PUSH HL
secsz=$+2
        LD BC,5
        LD DE,(23796)
        CALL #3D13
        LD DE,begin
        LD HL,GO
        EX (SP),HL
        PUSH DE
        LD DE,6
        ADD HL,DE
        PUSH HL
        EXX
        POP HL
        POP DE
        LD B,6
        DEC HL
        LD A,(HL)
        PUSH AF
        DJNZ $-3
        EXX
        LD DE,#1003
        LD C,#80
Hloop   LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
Hxxtbit EXX
Hnxtbit SLA C
        CALL Z,CHL
        JR C,Hloop
        LD B,1
H2bit   LD A,#40
        SLA C
        CALL Z,CHL
        RLA
        JR NC,$-6
        CP E
        JR C,H2lessE
        ADD A,B
        LD B,A
        XOR D
        JR NZ,H2bit
H2lessE ADD A,B
        CP 4
        JR Z,Hnpaked
        ADC A,-1
LL5DB1  CP 2
LL5DB3  EXX
        LD C,A
        LD H,-1
        EXX
        JR C,H3bit
        JR Z,LL5DE5
        SLA C
        CALL Z,CHL
        JR C,LL5DE5
        LD A,#7F
        LD B,E
        DJNZ Hbits
LL5DCB  DJNZ Hplain
        LD B,A
        SBC A,A
Hijnz   SLA C
        CALL Z,CHL
        RLA
        DEC A
        INC B
        JR NZ,Hijnz
        CP #E1
        JR NZ,$+4
        LD A,(HL)
        INC HL
        EXX
        LD H,A
        EXX
LL5DE5  LD A,(HL)
        INC HL
LL5DE7  EXX
        LD L,A
        ADD HL,DE
        LDIR
        JR Hxxtbit
Hplain  ADD A,6
        RLA
        LD B,A
Hldir   LD A,(HL)
        INC HL
        EXX
        LD (DE),A
        INC DE
        EXX
        DJNZ Hldir
        JR Hnxtbit
Hnpaked SLA C
        CALL Z,CHL
        LD A,D
        JR NC,Hbits
        LD A,(HL)
        INC HL
        CP D
        JR NC,LL5DB1
        OR A
        JR Z,Hquit
        EXX
        LD B,A
        EXX
        LD A,(HL)
        INC HL
        JR LL5DB3
H3bit   LD A,#3F
Hbits   SLA C
        CALL Z,CHL
        RLA
        JR NC,Hbits
        DJNZ LL5DCB
        JR LL5DE7
Hquit   EXX
        LD B,6
        POP AF
        LD (DE),A
        INC DE
        DJNZ $-3
CHL     LD C,(HL)
        INC HL
        RL C
        RET
; BASIC system variables written straight into memory: E_LINE (23641), WORKSP / STKBOT / STKEND, VARS (23627),
; so the BASIC program ends at END
END     ORG 23641
        DW END+1
        ORG $+6
        DW END+3,END+15,END+15
        ORG 23627
        DW END
; ---------------------------------------------------------------------------------------------------------------------
; nenado (#5B00): the "make" entry (see the header). Without a `make` label: CAPS SHIFT not pressed -> GO.
; ---------------------------------------------------------------------------------------------------------------------
        ORG #5B00
nenado  IF !exist make
        CALL 8026
        JP C,GO
        ENDIF
; RAMTOP = #5FFF; packer to #4000; begin..end up to #FFFF; packer to #6000; pack
        LD BC,#5FFF
        CALL 7863
        LD HL,#FB00
        LD DE,+(((4&#FFFF)>>>4|(4&#FFFF)<<(16-4))&#FFFF)
        LD B,5
        PUSH BC
        PUSH DE
        LDIR
        LD HL,end-1
        LD DE,-1
        LD BC,end-begin
        LDDR
        POP HL
        POP BC
        LD DE,+(((6&#FFFF)>>>4|(6&#FFFF)<<(16-4))&#FFFF)
        LDIR
        DI
        CALL +(((6&#FFFF)>>>4|(6&#FFFF)<<(16-4))&#FFFF)
; print the packed length; delete the old file; sectors of data -> secsz; save the BASIC
        PUSH HL
        CALL mPR
        LD C,18
        CALL #3D13
        POP BC
        DEC BC
        INC B
        LD A,B
        LD (secsz),A
        PUSH BC
        LD C,12
        LD HL,1
        LD (#5CD1),HL
        CALL #3D13
; read track 0 sectors 0-8 (catalog + disk info at #4800) to #4000; write the data to the first free sector
; (#48E1); update the first free sector and the free count (#48E5); find the file (C = 10)
        LD HL,+(((4&#FFFF)>>>4|(4&#FFFF)<<(16-4))&#FFFF)
        LD D,L
        LD E,L
        LD BC,#905
        CALL #3D13
        POP BC
        LD HL,mTO
        LD C,6
        LD DE,(#48E1)
        PUSH BC
        CALL #3D13
        POP BC
        LD HL,(23796)
        LD (#48E1),HL
        LD HL,(#48E5)
        PUSH BC
        XOR A
        LD C,B
        LD B,A
        SBC HL,BC
        LD C,10
        LD (#48E5),HL
        CALL #3D13
; HL = its catalog entry + 13 (the sector count): the bytes ',))))+++' are code - INC L, ADD HL,HL x4, DEC HL x3;
; sector count = 1 (the BASIC) + data sectors; write the 9 sectors back
        LD L,C
        LD H,4
        DB ',))))+++'
        POP BC
        INC B
        LD (HL),B
        LD HL,+(((4&#FFFF)>>>4|(4&#FFFF)<<(16-4))&#FFFF)
        LD D,L
        LD E,L
        LD BC,#906
        CALL #3D13
; back to BASIC: restore channel P's addresses (4 bytes from the ROM's channel table at #15BE), then a chain
; of ROM return addresses and the TR-DOS #3D2F exit; RNBAS2 sets NEWPPC = 1, NSPPC = 0: BASIC goes on at
; line 1 of the BASIC program in memory (the release's own line)
        CALL mCLS
        LD HL,4867
        PUSH HL
        LD BC,15
        LD DE,5566
        LD HL,(23631)
        ADD HL,BC
        EX DE,HL
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
RNBAS2  LD HL,1
        LD (23618),HL
        LD (IY+10),H
        RET
        DISPLAY 'RUN[CS+Ent]'
