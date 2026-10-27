;===========================================================================
; ZX Spectrum Next - NextZXOS DivMMC ROM
; Image: machines/next/enNxtmmc.rom (8192 bytes), loaded by TBBLUE.FW into SRAM (DivMMC ROM page)
;
; The ROM the DivMMC shows at #0000-#1FFF when an entry point is fetched (RST $08 hooks, NMI, tape
; traps; the table is NR #B8-#BB). It uses Z80N instructions (NEXTREG, PUSH nnnn): the Z80N
; must work before any card access does. The SD driver at #1EC6-#1FDD speaks the same SPI
; conversation as NextZXOS ROM 2 (zx2.asm #18D6-#1A82); card initialization is NOT here, it is in ROM 2.
; Status of this listing: code found by reachability from the vectors plus tentative entries
; (T-labels: word / jump tables and gap sweeps, not hand verified); the rest is data.
;===========================================================================

	org 0x0000


; DI, then the cold start. The DivMMC shows this ROM at #0000 after a reset.
Reset:
	di                      ;0000	f3
	jp ColdStart            ;0001	c3 6a 00
Data0004:
	defb 0x44,0x56                               ;0004	44 56	DV
	add hl,bc               ;0006	09
	ld (bc),a               ;0007	02

; RST $08 : DEFB hook - the esxDOS-compatible API gateway (jumps to #0512).
Rst08:
	jp L0512                ;0008	c3 12 05
Data000B:
	defb 0xe1                                    ;000b	e1	.
	push af                 ;000c	f5
	jp $3364                ;000d	c3 64 33

Rst10:
	rst $18                 ;0010	df
	djnz L0013              ;0011	10 00

L0013:
	ret                     ;0013	c9

L0014:
	ld a,$3a                ;0014	3e 3a
	and a                   ;0016	a7
	ret                     ;0017	c9

Rst18:
	nop                     ;0018	00
	nop                     ;0019	00
	jp L03C6                ;001a	c3 c6 03
Data001D:
	defb 0x7e,0xc9                               ;001d	7e c9	~.
	rst $38                 ;001f	ff

Rst20:
	inc sp                  ;0020	33
	inc sp                  ;0021	33
	call SelectConMem       ;0022	cd ce 00
	jp L0071                ;0025	c3 71 00

Rst28:
	ex (sp),hl              ;0028	e3
	ld c,(hl)               ;0029	4e
	inc hl                  ;002a	23
	ex (sp),hl              ;002b	e3
	jp L04E9                ;002c	c3 e9 04
Zero002F:
	defb 0x00                                    ;002f	00	.

Rst30:
	exx                     ;0030	d9
	ex (sp),hl              ;0031	e3
	push de                 ;0032	d5
	ld d,a                  ;0033	57
	push hl                 ;0034	e5
	jp L018D                ;0035	c3 8d 01

Rst38:
	jp Rst38Handler         ;0038	c3 e5 00
	call CallInOsRam        ;003b	cd 45 00
	ld hl,$0050             ;003e	21 50 00
	ex (sp),hl              ;0041	e3
	jp L1FF9                ;0042	c3 f9 1f

; Read #E3, force CONMEM-less map bits, call the OS code in DivMMC RAM at #2009 on a private stack
; (#26ED) and restore #E3 and the caller stack afterwards.
CallInOsRam:
	in a,($e3)              ;0045	db e3
	ld h,a                  ;0047	67
	and $c0                 ;0048	e6 c0
	or $01                  ;004a	f6 01
	out ($e3),a             ;004c	d3 e3
	ld ($25b8),sp           ;004e	ed 73 b8 25
	ld sp,$26ed             ;0052	31 ed 26
	push hl                 ;0055	e5
	call $2009              ;0056	cd 09 20
	pop af                  ;0059	f1
	ld sp,($25b8)           ;005a	ed 7b b8 25
	out ($e3),a             ;005e	d3 e3
	ret                     ;0060	c9
Data0061:
	defb 0xdd,0xe9,0x00,0x00,0x00                ;0061	dd e9 00 00 00	.....

; NMI entry: the ROM does nothing here (PUSH AF / POP AF / RETN); the NMI work is done by the
; OS code the automap maps for the NMI (NR #BB bits 1:0 choose instant or delayed).
Nmi:
	push af                 ;0066	f5
	pop af                  ;0067	f1
	retn                    ;0068	ed 45

ColdStart:
	push $0001              ;006a	ed 8a 00 01
	jp L1EA0                ;006e	c3 a0 1e

L0071:
	push hl                 ;0071	e5
	xor a                   ;0072	af
	ld ($32ff),a            ;0073	32 ff 32
	jp L1FF9                ;0076	c3 f9 1f

; BC = #243B, OUT (C),A selects the NextREG, IN (C) reads it (port #253B): A = NextREG A.
ReadNextReg:
	ld bc,$243b             ;0079	01 3b 24
	out (c),a               ;007c	ed 79
	inc b                   ;007e	04
	in a,(c)                ;007f	ed 78
	ret                     ;0081	c9

; tentative entry (word / jump table scan)
MapBank7ToMmu67:
	push af                 ;0082	f5
	ld a,$07                ;0083	3e 07
	jr L0089                ;0085	18 02

; C = 16K bank number: NEXTREG #56 = 2C, NEXTREG #57 = 2C + 1 (MMU slots 6 and 7 = #C000-#FFFF).
MapBankCToMmu67:
	push af                 ;0087	f5
	ld a,c                  ;0088	79

L0089:
	add a                   ;0089	87
	nextreg $56,a           ;008a	ed 92 56
	inc a                   ;008d	3c
	nextreg $57,a           ;008e	ed 92 57
	pop af                  ;0091	f1
	ret                     ;0092	c9
	ld b,$ff                ;0093	06 ff

L0095:
	ld c,(hl)               ;0095	4e
	inc hl                  ;0096	23

L0097:
	push hl                 ;0097	e5
	in a,($e3)              ;0098	db e3
	ld l,a                  ;009a	6f
	ld a,$02                ;009b	3e 02
	out ($e3),a             ;009d	d3 e3
	ld a,c                  ;009f	79
	inc a                   ;00a0	3c
	jr z,L00A4              ;00a1	28 01
	ld a,c                  ;00a3	79

L00A4:
	ld (de),a               ;00a4	12
	inc de                  ;00a5	13
	ld a,l                  ;00a6	7d
	out ($e3),a             ;00a7	d3 e3
	pop hl                  ;00a9	e1
	ret z                   ;00aa	c8
	djnz L0095              ;00ab	10 e8
	ld c,$ff                ;00ad	0e ff
	jr L0097                ;00af	18 e6
	ld a,$ff                ;00b1	3e ff
	ld b,a                  ;00b3	47
	push de                 ;00b4	d5

L00B5:
	in a,($e3)              ;00b5	db e3
	ld c,a                  ;00b7	4f
	ld a,$02                ;00b8	3e 02
	out ($e3),a             ;00ba	d3 e3
	ld a,c                  ;00bc	79
	ld c,(hl)               ;00bd	4e
	inc hl                  ;00be	23
	out ($e3),a             ;00bf	d3 e3
	ld a,c                  ;00c1	79
	and a                   ;00c2	a7
	jr z,T00C9              ;00c3	28 04
	ld (de),a               ;00c5	12
	inc de                  ;00c6	13
	djnz L00B5              ;00c7	10 ec

; tentative entry (word / jump table scan)
T00C9:
	ld a,$ff                ;00c9	3e ff
	ld (de),a               ;00cb	12
	pop hl                  ;00cc	e1
	ret                     ;00cd	c9

SelectConMem:
	ld a,$80                ;00ce	3e 80
	out ($e3),a             ;00d0	d3 e3
	jp $3cfd                ;00d2	c3 fd 3c
Zero00D5:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;00d5	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;00dd	00 00 00 00 00 00 00 00	........

; RST $38 / maskable interrupt while mapped.
Rst38Handler:
	push af                 ;00e5	f5
	push hl                 ;00e6	e5
	call CallInOsRam        ;00e7	cd 45 00
	add a                   ;00ea	87
	jp c,L1FFC              ;00eb	da fc 1f
	pop hl                  ;00ee	e1
	pop af                  ;00ef	f1
	ei                      ;00f0	fb
	ret                     ;00f1	c9

L00F2:
	ld bc,$0000             ;00f2	01 00 00
	call L041F              ;00f5	cd 1f 04
	ld bc,$204f             ;00f8	01 4f 20
	ret                     ;00fb	c9
Data00FC:
	defb 0x46,0x41                               ;00fc	46 41	FA
	call nc,$f700           ;00fe	d4 00 f7
	xor a                   ;0101	af
	add hl,bc               ;0102	09
	rst $30                 ;0103	f7
	di                      ;0104	f3
	ld c,$f7                ;0105	0e f7
	ret m                   ;0107	f8
	ld c,$f7                ;0108	0e f7
	ld l,c                  ;010a	69
	dec c                   ;010b	0d
	rst $30                 ;010c	f7
	ld a,(de)               ;010d	1a
	dec c                   ;010e	0d

L010F:
	rst $30                 ;010f	f7
	cp (hl)                 ;0110	be
	dec c                   ;0111	0d
	jp L0014                ;0112	c3 14 00
	jp L0014                ;0115	c3 14 00
	rst $30                 ;0118	f7
	ret                     ;0119	c9
	add hl,bc               ;011a	09
	rst $30                 ;011b	f7
	xor b                   ;011c	a8
	ld c,$f7                ;011d	0e f7
	ld (hl),c               ;011f	71
	ld c,$f7                ;0120	0e f7
	daa                     ;0122	27
	ld c,$f7                ;0123	0e f7
	inc d                   ;0125	14
	nop                     ;0126	00

L0127:
	rst $30                 ;0127	f7
	cp c                    ;0128	b9
	ld (de),a               ;0129	12
	jp L0014                ;012a	c3 14 00
Data012D:
	defb 0xf7,0xf1,0x3d,0xf7,0x1f,0x3e,0xf7,0x81 ;012d	f7 f1 3d f7 1f 3e f7 81	..=..>..
	defb 0x3e                                    ;0135	3e	>

L0136:
	rst $30                 ;0136	f7
	ld l,h                  ;0137	6c
	rla                     ;0138	17
	jp L1CA1                ;0139	c3 a1 1c
Data013C:
	defb 0xc3,0x30                               ;013c	c3 30	.0
	dec e                   ;013e	1d
	rst $30                 ;013f	f7
	ld h,a                  ;0140	67
	inc b                   ;0141	04
	rst $30                 ;0142	f7
	ld a,d                  ;0143	7a
	dec d                   ;0144	15
	rst $30                 ;0145	f7
	ld (hl),$16             ;0146	36 16
	rst $30                 ;0148	f7
	jr c,L0160              ;0149	38 15
	rst $30                 ;014b	f7
	ld e,(hl)               ;014c	5e
	dec d                   ;014d	15
	rst $30                 ;014e	f7
	adc d                   ;014f	8a
	ld d,$f7                ;0150	16 f7
	sub e                   ;0152	93
	ld d,$f7                ;0153	16 f7
	and d                   ;0155	a2
	ld d,$f7                ;0156	16 f7
	ld a,(de)               ;0158	1a
	rla                     ;0159	17

L015A:
	rst $30                 ;015a	f7
	call nz,$f716           ;015b	c4 16 f7
	rrca                    ;015e	0f
	rla                     ;015f	17

L0160:
	rst $30                 ;0160	f7
	call z,$f716            ;0161	cc 16 f7
	rrca                    ;0164	0f
	inc d                   ;0165	14
	jp L0333                ;0166	c3 33 03
	jp L02D4                ;0169	c3 d4 02
	jp L02E2                ;016c	c3 e2 02
	jp L02ED                ;016f	c3 ed 02
	jp L0223                ;0172	c3 23 02
	jp L02BD                ;0175	c3 bd 02
	jp L02CD                ;0178	c3 cd 02
	jp L0310                ;017b	c3 10 03
	jp L032C                ;017e	c3 2c 03
	jp L1DB8                ;0181	c3 b8 1d
	jp L0317                ;0184	c3 17 03
	jp L031E                ;0187	c3 1e 03
	jp L0325                ;018a	c3 25 03

L018D:
	in a,($e3)              ;018d	db e3
	ld e,a                  ;018f	5f
	ld a,l                  ;0190	7d
	ld hl,$2320             ;0191	21 20 23
	cp $3f                  ;0194	fe 3f
	jr nc,L01A5             ;0196	30 0d
	ld l,$00                ;0198	2e 00
	cp $18                  ;019a	fe 18
	jr c,L01A5              ;019c	38 07
	ld l,$10                ;019e	2e 10
	ld a,d                  ;01a0	7a
	and $0f                 ;01a1	e6 0f
	jr L01B7                ;01a3	18 12

L01A5:
	ld a,d                  ;01a5	7a
	cp $10                  ;01a6	fe 10
	jr c,L01B7              ;01a8	38 0d
	pop hl                  ;01aa	e1

L01AB:
	ld a,l                  ;01ab	7d
	cp $3f                  ;01ac	fe 3f
	ld a,$1d                ;01ae	3e 1d
	jr nc,L01DC             ;01b0	30 2a
	ld a,$16                ;01b2	3e 16
	and a                   ;01b4	a7
	jr L01DC                ;01b5	18 25

L01B7:
	add hl,a                ;01b7	ed 31
	ld a,(hl)               ;01b9	7e
	pop hl                  ;01ba	e1
	ld ($2006),a            ;01bb	32 06 20
	bit 6,a                 ;01be	cb 77
	jr nz,L01C8             ;01c0	20 06
	push af                 ;01c2	f5
	ld a,d                  ;01c3	7a
	and $0f                 ;01c4	e6 0f
	ld d,a                  ;01c6	57
	pop af                  ;01c7	f1

L01C8:
	and $8f                 ;01c8	e6 8f
	jp m,L01AB              ;01ca	fa ab 01
	jr nz,L01E4             ;01cd	20 15
	ld a,(hl)               ;01cf	7e
	inc l                   ;01d0	2c
	ld h,(hl)               ;01d1	66
	ld l,a                  ;01d2	6f

L01D3:
	call L01E0              ;01d3	cd e0 01
	exx                     ;01d6	d9
	ld d,a                  ;01d7	57
	ld a,e                  ;01d8	7b
	out ($e3),a             ;01d9	d3 e3
	ld a,d                  ;01db	7a

L01DC:
	pop de                  ;01dc	d1
	pop hl                  ;01dd	e1
	exx                     ;01de	d9
	ret                     ;01df	c9

L01E0:
	push hl                 ;01e0	e5
	ld a,d                  ;01e1	7a
	exx                     ;01e2	d9
	ret                     ;01e3	c9

L01E4:
	ld h,a                  ;01e4	67
	ld a,e                  ;01e5	7b
	and $80                 ;01e6	e6 80
	or h                    ;01e8	b4
	out ($e3),a             ;01e9	d3 e3
	dec l                   ;01eb	2d
	ld h,$20                ;01ec	26 20
	jr L01D3                ;01ee	18 e3

L01F0:
	push af                 ;01f0	f5
	in a,($e3)              ;01f1	db e3
	and $0f                 ;01f3	e6 0f
	jr z,L01FA              ;01f5	28 03
	ld ixh,$20              ;01f7	dd 26 20

L01FA:
	pop af                  ;01fa	f1
	push hl                 ;01fb	e5
	ld l,(ix+$01)           ;01fc	dd 6e 01
	ld h,(ix+$02)           ;01ff	dd 66 02
	ex (sp),hl              ;0202	e3
	ret                     ;0203	c9

L0204:
	push hl                 ;0204	e5
	and $0f                 ;0205	e6 0f
	ld hl,$2310             ;0207	21 10 23
	add hl,a                ;020a	ed 31
	ld a,(hl)               ;020c	7e
	pop hl                  ;020d	e1
	ret                     ;020e	c9

L020F:
	call L0218              ;020f	cd 18 02
	ret nc                  ;0212	d0
	ret m                   ;0213	f8
	ld a,$14                ;0214	3e 14
	and a                   ;0216	a7
	ret                     ;0217	c9

L0218:
	ld iyh,a                ;0218	fd 67
	call L0204              ;021a	cd 04 02
	call L040D              ;021d	cd 0d 04
	cp b                    ;0220	b8
	dec de                  ;0221	1b
	ret                     ;0222	c9

L0223:
	ld iyh,a                ;0223	fd 67
	push hl                 ;0225	e5
	ld a,b                  ;0226	78
	cp $10                  ;0227	fe 10
	jr nc,L0235             ;0229	30 0a
	ld hl,$2320             ;022b	21 20 23
	add hl,a                ;022e	ed 31
	ld a,(hl)               ;0230	7e
	and a                   ;0231	a7
	jp m,L023A              ;0232	fa 3a 02

L0235:
	ld a,$1d                ;0235	3e 1d

L0237:
	and a                   ;0237	a7
	pop hl                  ;0238	e1
	ret                     ;0239	c9

L023A:
	ex (sp),hl              ;023a	e3
	push bc                 ;023b	c5
	push de                 ;023c	d5
	push ix                 ;023d	dd e5
	push iy                 ;023f	fd e5
	ld a,iyh                ;0241	fd 7c
	call L020F              ;0243	cd 0f 02
	pop iy                  ;0246	fd e1
	pop ix                  ;0248	dd e1
	pop de                  ;024a	d1
	pop bc                  ;024b	c1
	jr nc,L0237             ;024c	30 e9
	jr z,L025B              ;024e	28 0b
	ld a,e                  ;0250	7b
	cp $03                  ;0251	fe 03
	ld a,$14                ;0253	3e 14
	jr nc,L0237             ;0255	30 e0
	inc d                   ;0257	14
	dec d                   ;0258	15
	jr nz,L0237             ;0259	20 dc

L025B:
	push bc                 ;025b	c5
	push de                 ;025c	d5
	push hl                 ;025d	e5
	push ix                 ;025e	dd e5
	push iy                 ;0260	fd e5
	call L0407              ;0262	cd 07 04
	adc e                   ;0265	8b
	inc e                   ;0266	1c
	pop iy                  ;0267	fd e1
	pop ix                  ;0269	dd e1
	pop hl                  ;026b	e1
	pop de                  ;026c	d1
	pop bc                  ;026d	c1
	jr nc,L02A2             ;026e	30 32
	ld a,$14                ;0270	3e 14
	jr nz,L0237             ;0272	20 c3
	inc e                   ;0274	1c
	dec e                   ;0275	1d
	ld a,$18                ;0276	3e 18
	jr z,L0237              ;0278	28 bd
	ld a,e                  ;027a	7b
	cp $03                  ;027b	fe 03
	jr c,L029C              ;027d	38 1d
	cp $05                  ;027f	fe 05

L0281:
	ld a,$15                ;0281	3e 15
	jr nc,L0237             ;0283	30 b2
	push bc                 ;0285	c5
	push de                 ;0286	d5
	push hl                 ;0287	e5
	push ix                 ;0288	dd e5
	push iy                 ;028a	fd e5
	ld a,iyh                ;028c	fd 7c
	call L0130              ;028e	cd 30 01
	pop iy                  ;0291	fd e1
	pop ix                  ;0293	dd e1
	pop hl                  ;0295	e1
	pop de                  ;0296	d1
	pop bc                  ;0297	c1
	jr nc,L0237             ;0298	30 9d
	jr L02A2                ;029a	18 06

L029C:
	ld d,e                  ;029c	53
	dec d                   ;029d	15
	set 1,d                 ;029e	cb ca
	jr L02AD                ;02a0	18 0b

L02A2:
	ld a,d                  ;02a2	7a
	cp $03                  ;02a3	fe 03
	jr nc,L0281             ;02a5	30 da
	and a                   ;02a7	a7
	ld a,$17                ;02a8	3e 17
	jr z,L0237              ;02aa	28 8b
	dec d                   ;02ac	15

L02AD:
	push de                 ;02ad	d5
	ld a,iyh                ;02ae	fd 7c
	call L0136              ;02b0	cd 36 01
	pop de                  ;02b3	d1
	pop hl                  ;02b4	e1
	ret nc                  ;02b5	d0
	ld a,($2006)            ;02b6	3a 06 20
	ld (hl),a               ;02b9	77
	bit 1,d                 ;02ba	cb 4a
	ret                     ;02bc	c9

L02BD:
	push af                 ;02bd	f5
	call L015A              ;02be	cd 5a 01
	pop bc                  ;02c1	c1

L02C2:
	ret nc                  ;02c2	d0
	ld hl,$2320             ;02c3	21 20 23

L02C6:
	ld a,b                  ;02c6	78
	add hl,a                ;02c7	ed 31
	ld (hl),$ff             ;02c9	36 ff
	scf                     ;02cb	37
	ret                     ;02cc	c9

L02CD:
	push af                 ;02cd	f5
	call L015D              ;02ce	cd 5d 01
	pop bc                  ;02d1	c1
	jr L02C2                ;02d2	18 ee

L02D4:
	push hl                 ;02d4	e5
	ld a,d                  ;02d5	7a
	and $7f                 ;02d6	e6 7f
	call L010F              ;02d8	cd 0f 01
	pop hl                  ;02db	e1
	ret nc                  ;02dc	d0
	ld a,($2006)            ;02dd	3a 06 20
	ld (hl),a               ;02e0	77
	ret                     ;02e1	c9

L02E2:
	push af                 ;02e2	f5
	call L0121              ;02e3	cd 21 01
	pop bc                  ;02e6	c1
	ret nc                  ;02e7	d0
	ld hl,$2310             ;02e8	21 10 23
	jr L02C6                ;02eb	18 d9

L02ED:
	cp $ff                  ;02ed	fe ff
	jr z,L0307              ;02ef	28 16
	push af                 ;02f1	f5
	ld b,$01                ;02f2	06 01
	call L0127              ;02f4	cd 27 01
	pop bc                  ;02f7	c1
	jr c,L02FE              ;02f8	38 04

L02FA:
	ld a,$16                ;02fa	3e 16
	and a                   ;02fc	a7
	ret                     ;02fd	c9

L02FE:
	ld a,b                  ;02fe	78
	cpl                     ;02ff	2f
	ld ($2008),a            ;0300	32 08 20
	ld a,b                  ;0303	78
	ld ($2007),a            ;0304	32 07 20

L0307:
	ld a,($2007)            ;0307	3a 07 20
	scf                     ;030a	37
	ret                     ;030b	c9

L030C:
	inc sp                  ;030c	33
	inc sp                  ;030d	33
	jr L02FA                ;030e	18 ea

L0310:
	call L041F              ;0310	cd 1f 04
	inc c                   ;0313	0c
	rst $20                 ;0314	e7
	ld h,$c9                ;0315	26 c9

L0317:
	call L041F              ;0317	cd 1f 04
	inc c                   ;031a	0c
	adc l                   ;031b	8d
	add hl,hl               ;031c	29
	ret                     ;031d	c9

L031E:
	call L041F              ;031e	cd 1f 04
	inc c                   ;0321	0c
	sbc $28                 ;0322	de 28
	ret                     ;0324	c9

L0325:
	call L041F              ;0325	cd 1f 04
	inc c                   ;0328	0c
	ld a,a                  ;0329	7f
	add hl,hl               ;032a	29
	ret                     ;032b	c9

L032C:
	call L041F              ;032c	cd 1f 04
	inc c                   ;032f	0c
	ld (hl),d               ;0330	72
	ld h,$c9                ;0331	26 c9

L0333:
	push de                 ;0333	d5
	push hl                 ;0334	e5
	ld (hl),e               ;0335	73
	ld a,d                  ;0336	7a
	call T0100              ;0337	cd 00 01
	xor a                   ;033a	af
	out ($e3),a             ;033b	d3 e3
	pop hl                  ;033d	e1
	pop de                  ;033e	d1
	ld (hl),$ff             ;033f	36 ff
	inc b                   ;0341	04
	jr L034B                ;0342	18 07

L0344:
	ld a,d                  ;0344	7a
	cp $10                  ;0345	fe 10
	ret nc                  ;0347	d0
	ld (hl),e               ;0348	73
	inc hl                  ;0349	23
	inc d                   ;034a	14

L034B:
	djnz L0344              ;034b	10 f7
	scf                     ;034d	37
	ret                     ;034e	c9
	push de                 ;034f	d5
	push hl                 ;0350	e5
	ld e,$00                ;0351	1e 00
	call L03A0              ;0353	cd a0 03
	jr c,L0366              ;0356	38 0e
	call L03A0              ;0358	cd a0 03
	ld a,e                  ;035b	7b
	cp $10                  ;035c	fe 10
	jr nc,L0364             ;035e	30 04
	set 7,e                 ;0360	cb fb
	jr L0366                ;0362	18 02

L0364:
	pop hl                  ;0364	e1
	push hl                 ;0365	e5

L0366:
	ld a,(hl)               ;0366	7e
	and $df                 ;0367	e6 df
	sub $41                 ;0369	d6 41
	jr c,L0375              ;036b	38 08
	cp $10                  ;036d	fe 10
	jr nc,L0375             ;036f	30 04
	ld d,a                  ;0371	57
	inc hl                  ;0372	23
	set 6,e                 ;0373	cb f3

L0375:
	ld a,(hl)               ;0375	7e
	inc hl                  ;0376	23
	cp $3a                  ;0377	fe 3a
	jr z,L037F              ;0379	28 04
	ld e,$00                ;037b	1e 00
	jr L0380                ;037d	18 01

L037F:
	ex (sp),hl              ;037f	e3

L0380:
	bit 6,e                 ;0380	cb 73
	jr nz,L0388             ;0382	20 04
	ld a,($2007)            ;0384	3a 07 20
	ld d,a                  ;0387	57

L0388:
	ld a,e                  ;0388	7b
	and $0f                 ;0389	e6 0f
	bit 7,e                 ;038b	cb 7b
	jr nz,L039A             ;038d	20 0b
	push de                 ;038f	d5
	ld a,d                  ;0390	7a
	ld d,$ff                ;0391	16 ff
	call L0124              ;0393	cd 24 01
	pop de                  ;0396	d1
	jr c,L039A              ;0397	38 01
	xor a                   ;0399	af

L039A:
	swapnib                 ;039a	ed 23
	or d                    ;039c	b2
	pop hl                  ;039d	e1
	pop de                  ;039e	d1
	ret                     ;039f	c9

L03A0:
	ld a,(hl)               ;03a0	7e
	sub $30                 ;03a1	d6 30
	ret c                   ;03a3	d8
	ld d,$0a                ;03a4	16 0a
	cp d                    ;03a6	ba
	ccf                     ;03a7	3f
	ret c                   ;03a8	d8
	inc hl                  ;03a9	23
	mul d,e                 ;03aa	ed 30
	add de,a                ;03ac	ed 32
	and a                   ;03ae	a7
	ret                     ;03af	c9
Text03B0:
	defb 0x06,0x01,0x21,0x20,0x23,0x78,0xed,0x31 ;03b0	06 01 21 20 23 78 ed 31	..! #x.1
	defb 0x7e,0xa7,0x37,0xf8,0x23,0x04,0xcb,0x60 ;03b8	7e a7 37 f8 23 04 cb 60	~.7.#..`
	defb 0x28,0xf6,0x3e,0x1d,0xa7,0xc9           ;03c0	28 f6 3e 1d a7 c9	(.>...

L03C6:
	ex (sp),hl              ;03c6	e3
	push af                 ;03c7	f5
	push de                 ;03c8	d5
	ld d,(hl)               ;03c9	56
	inc hl                  ;03ca	23
	ld e,(hl)               ;03cb	5e
	inc hl                  ;03cc	23
	in a,($e3)              ;03cd	db e3
	push af                 ;03cf	f5
	call SelectConMem       ;03d0	cd ce 00
	pop af                  ;03d3	f1
	ld ($2002),a            ;03d4	32 02 20
	ld ($3c5d),de           ;03d7	ed 53 5d 3c
	pop de                  ;03db	d1
	pop af                  ;03dc	f1
	ex (sp),hl              ;03dd	e3
	ld ($2016),sp           ;03de	ed 73 16 20
	call $3c57              ;03e2	cd 57 3c
	push af                 ;03e5	f5
	ld a,($2002)            ;03e6	3a 02 20
	cp $82                  ;03e9	fe 82
	call z,SetConMem        ;03eb	cc f3 1f
	out ($e3),a             ;03ee	d3 e3
	pop af                  ;03f0	f1
	ret                     ;03f1	c9
Data03F2:
	defb 0x22,0x10,0x20,0x21,0xbd,0x01,0xed,0x8a ;03f2	22 10 20 21 bd 01 ed 8a	". !....
	defb 0x3c,0xfc,0xe5,0x2a                     ;03fa	3c fc e5 2a	<..*
	djnz L0420              ;03fe	10 20
	nextreg $8e,$02         ;0400	ed 91 8e 02
	jp L1FF9                ;0404	c3 f9 1f

L0407:
	push af                 ;0407	f5
	pop iy                  ;0408	fd e1
	ld a,($2006)            ;040a	3a 06 20

L040D:
	ld ($2006),a            ;040d	32 06 20
	and $8f                 ;0410	e6 8f
	jp m,L030C              ;0412	fa 0c 03
	exx                     ;0415	d9
	ex (sp),hl              ;0416	e3
	push de                 ;0417	d5
	push bc                 ;0418	c5
	ld b,a                  ;0419	47
	push iy                 ;041a	fd e5
	pop af                  ;041c	f1
	jr L0425                ;041d	18 06

L041F:
	exx                     ;041f	d9

L0420:
	ex (sp),hl              ;0420	e3
	push de                 ;0421	d5
	push bc                 ;0422	c5
	ld b,(hl)               ;0423	46
	inc hl                  ;0424	23

L0425:
	push af                 ;0425	f5
	in a,($e3)              ;0426	db e3
	ld c,a                  ;0428	4f
	and $c0                 ;0429	e6 c0
	or b                    ;042b	b0
	ld e,a                  ;042c	5f
	pop af                  ;042d	f1
	ld b,a                  ;042e	47
	ld a,e                  ;042f	7b
	ld e,(hl)               ;0430	5e
	inc hl                  ;0431	23
	ld d,(hl)               ;0432	56
	inc hl                  ;0433	23
	out ($e3),a             ;0434	d3 e3
	ld a,b                  ;0436	78
	call L0445              ;0437	cd 45 04
	exx                     ;043a	d9
	ld e,a                  ;043b	5f
	ld a,c                  ;043c	79
	out ($e3),a             ;043d	d3 e3
	ld a,e                  ;043f	7b
	pop bc                  ;0440	c1
	pop de                  ;0441	d1
	ex (sp),hl              ;0442	e3
	exx                     ;0443	d9
	ret                     ;0444	c9

L0445:
	push de                 ;0445	d5
	exx                     ;0446	d9
	ret                     ;0447	c9
	out ($e3),a             ;0448	d3 e3
	call L045D              ;044a	cd 5d 04
	push af                 ;044d	f5
	xor a                   ;044e	af
	out ($e3),a             ;044f	d3 e3
	pop af                  ;0451	f1
	nextreg $8e,$02         ;0452	ed 91 8e 02
	push $3f40              ;0456	ed 8a 3f 40
	jp L1FF9                ;045a	c3 f9 1f

L045D:
	push hl                 ;045d	e5
	ld hl,($5b56)           ;045e	2a 56 5b
	push hl                 ;0461	e5
	pop af                  ;0462	f1
	ld hl,($5b52)           ;0463	2a 52 5b
	ret                     ;0466	c9
	call L1354              ;0467	cd 54 13
	rst $28                 ;046a	ef
	dec hl                  ;046b	2b
	push bc                 ;046c	c5
	pop ix                  ;046d	dd e1
	call L19C2              ;046f	cd c2 19
	bit 7,(iy+$0f)          ;0472	fd cb 0f 7e
	scf                     ;0476	37
	ret                     ;0477	c9
	nop                     ;0478	00
	nop                     ;0479	00
	nop                     ;047a	00
	nop                     ;047b	00
	nop                     ;047c	00
	nop                     ;047d	00
	nop                     ;047e	00
	nop                     ;047f	00
	nop                     ;0480	00
	nop                     ;0481	00
	nop                     ;0482	00
	nop                     ;0483	00
	nop                     ;0484	00
	nop                     ;0485	00
	nop                     ;0486	00
	nop                     ;0487	00
	nop                     ;0488	00
	nop                     ;0489	00
	nop                     ;048a	00
	nop                     ;048b	00
	nop                     ;048c	00
	nop                     ;048d	00
	nop                     ;048e	00
	nop                     ;048f	00
	nop                     ;0490	00
	nop                     ;0491	00
	nop                     ;0492	00
	nop                     ;0493	00
	nop                     ;0494	00
	nop                     ;0495	00
	nop                     ;0496	00
	nop                     ;0497	00
	nop                     ;0498	00
	nop                     ;0499	00
	nop                     ;049a	00
	nop                     ;049b	00
	nop                     ;049c	00
	nop                     ;049d	00
	nop                     ;049e	00
	nop                     ;049f	00
	nop                     ;04a0	00
	nop                     ;04a1	00
	nop                     ;04a2	00
	nop                     ;04a3	00
	nop                     ;04a4	00
	nop                     ;04a5	00
	nop                     ;04a6	00
	nop                     ;04a7	00
	nop                     ;04a8	00
	nop                     ;04a9	00
	nop                     ;04aa	00
	nop                     ;04ab	00
	nop                     ;04ac	00
	nop                     ;04ad	00
	nop                     ;04ae	00
	nop                     ;04af	00
	nop                     ;04b0	00
	nop                     ;04b1	00
	nop                     ;04b2	00
	nop                     ;04b3	00
	nop                     ;04b4	00
	nop                     ;04b5	00
	nop                     ;04b6	00
	nop                     ;04b7	00
	nop                     ;04b8	00
	nop                     ;04b9	00
	nop                     ;04ba	00
	nop                     ;04bb	00
	nop                     ;04bc	00
	nop                     ;04bd	00
	nop                     ;04be	00
	nop                     ;04bf	00
	nop                     ;04c0	00
	nop                     ;04c1	00
	nop                     ;04c2	00
	nop                     ;04c3	00
	nop                     ;04c4	00
	nop                     ;04c5	00
	nop                     ;04c6	00
	nop                     ;04c7	00
	nop                     ;04c8	00
	nop                     ;04c9	00
	nop                     ;04ca	00
	nop                     ;04cb	00
	nop                     ;04cc	00
	nop                     ;04cd	00
	nop                     ;04ce	00
	nop                     ;04cf	00
	nop                     ;04d0	00
	nop                     ;04d1	00
	nop                     ;04d2	00
	nop                     ;04d3	00
	nop                     ;04d4	00
	nop                     ;04d5	00
	nop                     ;04d6	00
	nop                     ;04d7	00
	jp $3a3e                ;04d8	c3 3e 3a
Text04DB:
	defb 0xaf,0x77,0xed,0xb0,0x37,0xc9           ;04db	af 77 ed b0 37 c9	.w..7.

L04E1:
	ex (sp),hl              ;04e1	e3
	ld c,(hl)               ;04e2	4e
	inc hl                  ;04e3	23
	ex (sp),hl              ;04e4	e3
	push ix                 ;04e5	dd e5
	jr L04EB                ;04e7	18 02

L04E9:
	push iy                 ;04e9	fd e5

L04EB:
	ex (sp),hl              ;04eb	e3
	ld b,$00                ;04ec	06 00
	add hl,bc               ;04ee	09
	ld c,(hl)               ;04ef	4e
	inc hl                  ;04f0	23
	ld b,(hl)               ;04f1	46
	inc hl                  ;04f2	23
	ld e,(hl)               ;04f3	5e
	inc hl                  ;04f4	23
	ld d,(hl)               ;04f5	56
	pop hl                  ;04f6	e1
	ret                     ;04f7	c9

; tentative entry (word / jump table scan)
T04F8:
	ex (sp),hl              ;04f8	e3
	ld a,(hl)               ;04f9	7e
	inc hl                  ;04fa	23
	ex (sp),hl              ;04fb	e3
	push ix                 ;04fc	dd e5
	jr L0506                ;04fe	18 06

L0500:
	ex (sp),hl              ;0500	e3
	ld a,(hl)               ;0501	7e
	inc hl                  ;0502	23
	ex (sp),hl              ;0503	e3
	push iy                 ;0504	fd e5

L0506:
	ex (sp),hl              ;0506	e3
	add hl,a                ;0507	ed 31
	ld (hl),c               ;0509	71
	inc hl                  ;050a	23
	ld (hl),b               ;050b	70
	inc hl                  ;050c	23
	ld (hl),e               ;050d	73
	inc hl                  ;050e	23
	ld (hl),d               ;050f	72
	pop hl                  ;0510	e1
	ret                     ;0511	c9

L0512:
	ex (sp),hl              ;0512	e3
	ex (sp),ix              ;0513	dd e3
	ex af,af'               ;0515	08
	push af                 ;0516	f5
	ld a,i                  ;0517	ed 57
	ld a,(hl)               ;0519	7e
	inc hl                  ;051a	23
	push hl                 ;051b	e5
	ld l,a                  ;051c	6f
	di                      ;051d	f3
	ld a,$80                ;051e	3e 80
	out ($e3),a             ;0520	d3 e3
	ld ($2014),sp           ;0522	ed 73 14 20
	ld a,($2015)            ;0526	3a 15 20
	rlca                    ;0529	07
	jr c,L0534              ;052a	38 08
	rlca                    ;052c	07
	jr c,L0534              ;052d	38 05
	ld sp,$2100             ;052f	31 00 21
	jr L0538                ;0532	18 04

L0534:
	jp po,L0538             ;0534	e2 38 05
	ei                      ;0537	fb

L0538:
	push af                 ;0538	f5
	nextreg $8e,$03         ;0539	ed 91 8e 03
	call SelectConMem       ;053d	cd ce 00
	ld a,l                  ;0540	7d
	push ix                 ;0541	dd e5
	pop hl                  ;0543	e1
	call $33b4              ;0544	cd b4 33
	pop af                  ;0547	f1
	scf                     ;0548	37
	call SetConMem          ;0549	cd f3 1f
	jr nc,L0552             ;054c	30 04
	ld sp,($2014)           ;054e	ed 7b 14 20

L0552:
	ld a,$82                ;0552	3e 82
	out ($e3),a             ;0554	d3 e3
	jp po,L055A             ;0556	e2 5a 05
	ei                      ;0559	fb

L055A:
	pop ix                  ;055a	dd e1
	pop af                  ;055c	f1
	ex af,af'               ;055d	08
	ex (sp),ix              ;055e	dd e3
	ret                     ;0560	c9
	push af                 ;0561	f5
	call SelectConMem       ;0562	cd ce 00
	ld a,$0a                ;0565	3e 0a
	out ($e3),a             ;0567	d3 e3
	pop af                  ;0569	f1
	ret                     ;056a	c9
Data056B:
	defb 0xc3,0x7f,0x38                          ;056b	c3 7f 38	..8

L056E:
	call L06C3              ;056e	cd c3 06
	ret nc                  ;0571	d0
	call L07BC              ;0572	cd bc 07
	scf                     ;0575	37
	ret                     ;0576	c9

L0577:
	push iy                 ;0577	fd e5
	pop hl                  ;0579	e1
	add hl,bc               ;057a	09
	inc (hl)                ;057b	34
	ld a,(hl)               ;057c	7e
	inc hl                  ;057d	23
	cp (ix+$21)             ;057e	dd be 21
	ret c                   ;0581	d8
	ld c,(hl)               ;0582	4e
	inc hl                  ;0583	23
	ld b,(hl)               ;0584	46
	inc hl                  ;0585	23
	ld e,(hl)               ;0586	5e
	inc hl                  ;0587	23
	ld d,(hl)               ;0588	56
	push hl                 ;0589	e5
	call L0912              ;058a	cd 12 09
	pop hl                  ;058d	e1
	ret nc                  ;058e	d0
	call T05A1              ;058f	cd a1 05
	ld a,$19                ;0592	3e 19
	ret nc                  ;0594	d0
	ld (hl),d               ;0595	72
	dec hl                  ;0596	2b
	ld (hl),e               ;0597	73
	dec hl                  ;0598	2b
	ld (hl),b               ;0599	70
	dec hl                  ;059a	2b
	ld (hl),c               ;059b	71
	dec hl                  ;059c	2b
	ld (hl),$00             ;059d	36 00
	scf                     ;059f	37
	ret                     ;05a0	c9

; tentative entry (word / jump table scan)
T05A1:
	ld a,c                  ;05a1	79
	cp $02                  ;05a2	fe 02
	jr nc,L05AA             ;05a4	30 04
	ld a,b                  ;05a6	78
	or e                    ;05a7	b3
	or d                    ;05a8	b2
	ret z                   ;05a9	c8

L05AA:
	push hl                 ;05aa	e5
	ld l,(ix+$22)           ;05ab	dd 6e 22
	ld h,(ix+$23)           ;05ae	dd 66 23
	and a                   ;05b1	a7
	sbc hl,bc               ;05b2	ed 42
	ld l,(ix+$24)           ;05b4	dd 6e 24
	ld h,(ix+$25)           ;05b7	dd 66 25
	sbc hl,de               ;05ba	ed 52
	ccf                     ;05bc	3f
	pop hl                  ;05bd	e1
	ret                     ;05be	c9

L05BF:
	call L04E1              ;05bf	cd e1 04
	cpl                     ;05c2	2f

L05C3:
	inc c                   ;05c3	0c
	jr nz,L05CD             ;05c4	20 07
	inc b                   ;05c6	04
	jr nz,L05CD             ;05c7	20 04
	inc e                   ;05c9	1c
	jr nz,L05CD             ;05ca	20 01
	inc d                   ;05cc	14

L05CD:
	call T05A1              ;05cd	cd a1 05
	jr c,L05D8              ;05d0	38 06
	ld de,$0000             ;05d2	11 00 00
	ld bc,$0002             ;05d5	01 02 00

L05D8:
	ld a,c                  ;05d8	79
	cp (ix+$2f)             ;05d9	dd be 2f
	jr nz,L05F1             ;05dc	20 13
	ld a,b                  ;05de	78
	cp (ix+$30)             ;05df	dd be 30
	jr nz,L05F1             ;05e2	20 0d
	ld a,e                  ;05e4	7b
	cp (ix+$31)             ;05e5	dd be 31
	jr nz,L05F1             ;05e8	20 07
	ld a,d                  ;05ea	7a
	cp (ix+$32)             ;05eb	dd be 32
	ld a,$1a                ;05ee	3e 1a
	ret z                   ;05f0	c8

L05F1:
	push bc                 ;05f1	c5
	push de                 ;05f2	d5
	call L0912              ;05f3	cd 12 09
	jr c,L05FB              ;05f6	38 03
	pop hl                  ;05f8	e1
	pop hl                  ;05f9	e1
	ret                     ;05fa	c9

L05FB:
	ld a,d                  ;05fb	7a
	and $0f                 ;05fc	e6 0f
	or e                    ;05fe	b3
	or b                    ;05ff	b0
	or c                    ;0600	b1
	pop de                  ;0601	d1
	pop bc                  ;0602	c1
	jr nz,L05C3             ;0603	20 be
	call T04F8              ;0605	cd f8 04
	cpl                     ;0608	2f
	ld l,(ix+$2b)           ;0609	dd 6e 2b
	ld h,(ix+$2c)           ;060c	dd 66 2c
	dec hl                  ;060f	2b
	ld (ix+$2b),l           ;0610	dd 75 2b
	ld (ix+$2c),h           ;0613	dd 74 2c
	ld a,h                  ;0616	7c
	and l                   ;0617	a5
	inc a                   ;0618	3c
	jr nz,L0628             ;0619	20 0d
	ld l,(ix+$2d)           ;061b	dd 6e 2d
	ld h,(ix+$2e)           ;061e	dd 66 2e
	dec hl                  ;0621	2b
	ld (ix+$2d),l           ;0622	dd 75 2d
	ld (ix+$2e),h           ;0625	dd 74 2e

L0628:
	push bc                 ;0628	c5
	push de                 ;0629	d5
	push iy                 ;062a	fd e5
	ld iy,$0fff             ;062c	fd 21 ff 0f
	ld hl,$ffff             ;0630	21 ff ff
	call L095A              ;0633	cd 5a 09
	pop iy                  ;0636	fd e1
	pop de                  ;0638	d1
	pop bc                  ;0639	c1
	call c,L0EC5            ;063a	dc c5 0e
	ret                     ;063d	c9

; tentative entry (word / jump table scan)
T063E:
	push iy                 ;063e	fd e5
	push bc                 ;0640	c5
	push de                 ;0641	d5
	call L05BF              ;0642	cd bf 05
	ld l,c                  ;0645	69
	ld h,b                  ;0646	60
	push de                 ;0647	d5
	pop iy                  ;0648	fd e1
	pop de                  ;064a	d1
	pop bc                  ;064b	c1
	push iy                 ;064c	fd e5
	push hl                 ;064e	e5
	call c,L095A            ;064f	dc 5a 09
	pop bc                  ;0652	c1
	pop de                  ;0653	d1
	pop iy                  ;0654	fd e1
	ret                     ;0656	c9
	push iy                 ;0657	fd e5

L0659:
	call T05A1              ;0659	cd a1 05
	jr nc,L0680             ;065c	30 22
	push bc                 ;065e	c5
	push de                 ;065f	d5
	call L0912              ;0660	cd 12 09
	jr nc,L067B             ;0663	30 16
	pop hl                  ;0665	e1
	pop iy                  ;0666	fd e1
	push bc                 ;0668	c5
	push de                 ;0669	d5
	ex de,hl                ;066a	eb
	push iy                 ;066b	fd e5
	pop bc                  ;066d	c1
	call Inc32BitPosition   ;066e	cd df 1f
	ld hl,$0000             ;0671	21 00 00
	ld iy,$0000             ;0674	fd 21 00 00
	call L095A              ;0678	cd 5a 09

L067B:
	pop de                  ;067b	d1
	pop bc                  ;067c	c1
	jr c,L0659              ;067d	38 da
	ret                     ;067f	c9

L0680:
	pop iy                  ;0680	fd e1
	call L0EC5              ;0682	cd c5 0e
	jp L08D1                ;0685	c3 d1 08

L0688:
	push bc                 ;0688	c5
	push de                 ;0689	d5
	xor a                   ;068a	af
	call L056E              ;068b	cd 6e 05
	pop de                  ;068e	d1
	pop bc                  ;068f	c1
	ret nc                  ;0690	d0
	push hl                 ;0691	e5
	push bc                 ;0692	c5
	push de                 ;0693	d5
	ld d,h                  ;0694	54
	ld e,l                  ;0695	5d
	inc de                  ;0696	13
	ld bc,$01ff             ;0697	01 ff 01
	ld (hl),$00             ;069a	36 00
	ldir                    ;069c	ed b0
	pop de                  ;069e	d1
	pop bc                  ;069f	c1
	xor a                   ;06a0	af
	call L06C3              ;06a1	cd c3 06
	pop hl                  ;06a4	e1
	ret nc                  ;06a5	d0
	call GetDriveFlag       ;06a6	cd ef 1f

L06A9:
	push af                 ;06a9	f5
	push bc                 ;06aa	c5
	push de                 ;06ab	d5
	push hl                 ;06ac	e5
	call L1ECE              ;06ad	cd ce 1e
	pop hl                  ;06b0	e1
	pop de                  ;06b1	d1
	pop bc                  ;06b2	c1
	jr nc,L06C1             ;06b3	30 0c
	inc de                  ;06b5	13
	ld a,d                  ;06b6	7a
	or e                    ;06b7	b3
	jr nz,L06BB             ;06b8	20 01
	inc bc                  ;06ba	03

L06BB:
	pop af                  ;06bb	f1
	dec a                   ;06bc	3d
	jr nz,L06A9             ;06bd	20 ea
	scf                     ;06bf	37
	ret                     ;06c0	c9

L06C1:
	pop de                  ;06c1	d1
	ret                     ;06c2	c9

L06C3:
	cp (ix+$21)             ;06c3	dd be 21
	jr nc,L06EB             ;06c6	30 23
	push iy                 ;06c8	fd e5
	ld iyl,a                ;06ca	fd 6f
	ld iyh,$00              ;06cc	fd 26 00
	ld hl,$0000             ;06cf	21 00 00
	call GetDriveFlag       ;06d2	cd ef 1f

L06D5:
	add iy,bc               ;06d5	fd 09
	adc hl,de               ;06d7	ed 5a
	dec a                   ;06d9	3d
	jr nz,L06D5             ;06da	20 f9
	call L04E1              ;06dc	cd e1 04
	ld h,$fd                ;06df	26 fd
	add hl,bc               ;06e1	09
	adc hl,de               ;06e2	ed 5a
	ex (sp),iy              ;06e4	fd e3
	pop de                  ;06e6	d1
	ld c,l                  ;06e7	4d
	ld b,h                  ;06e8	44
	scf                     ;06e9	37
	ret                     ;06ea	c9

L06EB:
	ld a,$02                ;06eb	3e 02
	ret                     ;06ed	c9

L06EE:
	call L06C3              ;06ee	cd c3 06
	ret nc                  ;06f1	d0
	jp L0832                ;06f2	c3 32 08
	ld c,(hl)               ;06f5	4e
	inc hl                  ;06f6	23
	ld b,(hl)               ;06f7	46
	inc hl                  ;06f8	23
	call T0701              ;06f9	cd 01 07
	dec de                  ;06fc	1b
	inc bc                  ;06fd	03
	ld hl,$00f3             ;06fe	21 f3 00

; tentative entry (word / jump table scan)
T0701:
	ld a,$02                ;0701	3e 02

L0703:
	push af                 ;0703	f5
	ld a,d                  ;0704	7a
	and $c0                 ;0705	e6 c0
	jr nz,L0763             ;0707	20 5a
	pop af                  ;0709	f1
	push hl                 ;070a	e5
	ex de,hl                ;070b	eb
	exx                     ;070c	d9
	ex (sp),hl              ;070d	e3
	push de                 ;070e	d5
	push bc                 ;070f	c5
	ld d,a                  ;0710	57
	ld c,$e3                ;0711	0e e3
	in b,(c)                ;0713	ed 40
	exx                     ;0715	d9
	ld a,c                  ;0716	79
	and $03                 ;0717	e6 03
	jr z,L0734              ;0719	28 19
	push bc                 ;071b	c5
	ld c,a                  ;071c	4f
	exx                     ;071d	d9

L071E:
	ld a,(hl)               ;071e	7e
	inc hl                  ;071f	23
	out (c),d               ;0720	ed 51
	exx                     ;0722	d9
	ld (hl),a               ;0723	77
	inc hl                  ;0724	23
	dec c                   ;0725	0d
	exx                     ;0726	d9
	out (c),b               ;0727	ed 41
	jp nz,L071E             ;0729	c2 1e 07
	exx                     ;072c	d9
	pop bc                  ;072d	c1
	ld a,c                  ;072e	79
	and $fc                 ;072f	e6 fc
	ld c,a                  ;0731	4f
	jr L0755                ;0732	18 21

L0734:
	exx                     ;0734	d9

L0735:
	ld a,(hl)               ;0735	7e
	inc hl                  ;0736	23
	exx                     ;0737	d9
	ld e,a                  ;0738	5f
	exx                     ;0739	d9
	ld a,(hl)               ;073a	7e
	inc hl                  ;073b	23
	exx                     ;073c	d9
	ld d,a                  ;073d	57
	exx                     ;073e	d9
	ld a,(hl)               ;073f	7e
	inc hl                  ;0740	23
	ld e,(hl)               ;0741	5e
	inc hl                  ;0742	23
	out (c),d               ;0743	ed 51
	exx                     ;0745	d9
	ld (hl),e               ;0746	73
	inc hl                  ;0747	23
	ld (hl),d               ;0748	72
	inc hl                  ;0749	23
	ld (hl),a               ;074a	77
	inc hl                  ;074b	23
	exx                     ;074c	d9
	ld a,e                  ;074d	7b
	exx                     ;074e	d9
	ld (hl),a               ;074f	77
	inc hl                  ;0750	23
	add bc,$fffc            ;0751	ed 36 fc ff

L0755:
	ld a,b                  ;0755	78
	or c                    ;0756	b1
	exx                     ;0757	d9
	out (c),b               ;0758	ed 41
	jr nz,L0735             ;075a	20 d9
	pop bc                  ;075c	c1
	pop de                  ;075d	d1
	ex (sp),hl              ;075e	e3
	exx                     ;075f	d9
	ex de,hl                ;0760	eb
	pop hl                  ;0761	e1
	ret                     ;0762	c9

L0763:
	pop af                  ;0763	f1
	ldir                    ;0764	ed b0
	ret                     ;0766	c9
	ld a,$02                ;0767	3e 02

L0769:
	push af                 ;0769	f5
	ld a,h                  ;076a	7c
	and $c0                 ;076b	e6 c0
	jr nz,L0763             ;076d	20 f4
	pop af                  ;076f	f1
	push hl                 ;0770	e5
	exx                     ;0771	d9
	ex (sp),hl              ;0772	e3
	push de                 ;0773	d5
	push bc                 ;0774	c5
	ld d,a                  ;0775	57
	ld c,$e3                ;0776	0e e3
	in b,(c)                ;0778	ed 40

L077A:
	out (c),d               ;077a	ed 51
	ld a,(hl)               ;077c	7e
	inc hl                  ;077d	23
	out (c),b               ;077e	ed 41
	exx                     ;0780	d9
	ld (de),a               ;0781	12
	inc de                  ;0782	13
	dec bc                  ;0783	0b
	ld a,b                  ;0784	78
	or c                    ;0785	b1
	exx                     ;0786	d9
	jr nz,L077A             ;0787	20 f1
	pop bc                  ;0789	c1
	pop de                  ;078a	d1
	ex (sp),hl              ;078b	e3
	exx                     ;078c	d9
	pop hl                  ;078d	e1
	ret                     ;078e	c9

L078F:
	ld hl,$2330             ;078f	21 30 23
	ld bc,$0200             ;0792	01 00 02
	push bc                 ;0795	c5

L0796:
	ld (hl),c               ;0796	71
	inc hl                  ;0797	23
	inc c                   ;0798	0c
	djnz L0796              ;0799	10 fb
	pop bc                  ;079b	c1
	ld d,c                  ;079c	51
	ld e,$07                ;079d	1e 07

L079F:
	ld (hl),d               ;079f	72
	add hl,de               ;07a0	19
	djnz L079F              ;07a1	10 fc
	ret                     ;07a3	c9

L07A4:
	ld hl,$2330             ;07a4	21 30 23
	ld b,a                  ;07a7	47
	ld c,a                  ;07a8	4f

L07A9:
	ld a,(hl)               ;07a9	7e
	ld (hl),b               ;07aa	70
	inc hl                  ;07ab	23
	ld b,a                  ;07ac	47
	cp c                    ;07ad	b9
	jr nz,L07A9             ;07ae	20 f9

L07B0:
	inc a                   ;07b0	3c
	ld hl,$2140             ;07b1	21 40 21
	ld de,$0200             ;07b4	11 00 02

L07B7:
	add hl,de               ;07b7	19
	dec a                   ;07b8	3d
	jr nz,L07B7             ;07b9	20 fc
	ret                     ;07bb	c9

L07BC:
	push iy                 ;07bc	fd e5
	call L07C4              ;07be	cd c4 07
	pop iy                  ;07c1	fd e1
	ret                     ;07c3	c9

L07C4:
	ld iy,$2332             ;07c4	fd 21 32 23
	ld l,$02                ;07c8	2e 02

L07CA:
	bit 0,(iy+$00)          ;07ca	fd cb 00 46
	jr z,L07FE              ;07ce	28 2e
	ld a,(iy+$03)           ;07d0	fd 7e 03
	cp e                    ;07d3	bb
	jr nz,L07FE             ;07d4	20 28
	ld a,(iy+$04)           ;07d6	fd 7e 04
	cp d                    ;07d9	ba
	jr nz,L07FE             ;07da	20 22
	ld a,(iy+$05)           ;07dc	fd 7e 05
	cp c                    ;07df	b9
	jr nz,L07FE             ;07e0	20 1c
	ld a,(iy+$06)           ;07e2	fd 7e 06
	cp b                    ;07e5	b8
	jr nz,L07FE             ;07e6	20 16
	ld a,ixl                ;07e8	dd 7d
	cp (iy+$01)             ;07ea	fd be 01
	jr nz,L07FE             ;07ed	20 0f
	ld a,ixh                ;07ef	dd 7c
	cp (iy+$02)             ;07f1	fd be 02
	jr nz,L07FE             ;07f4	20 08
	ld a,$02                ;07f6	3e 02
	sub l                   ;07f8	95
	call L07A4              ;07f9	cd a4 07
	scf                     ;07fc	37
	ret                     ;07fd	c9

L07FE:
	push bc                 ;07fe	c5
	ld bc,$0007             ;07ff	01 07 00
	add iy,bc               ;0802	fd 09
	pop bc                  ;0804	c1
	dec l                   ;0805	2d
	jr nz,L07CA             ;0806	20 c2
	ld a,($2331)            ;0808	3a 31 23
	push de                 ;080b	d5
	call L08B9              ;080c	cd b9 08
	pop hl                  ;080f	e1
	ld (iy+$00),$01         ;0810	fd 36 00 01
	ld a,ixl                ;0814	dd 7d
	ld (iy+$01),a           ;0816	fd 77 01
	ld a,ixh                ;0819	dd 7c
	ld (iy+$02),a           ;081b	fd 77 02
	ld (iy+$03),l           ;081e	fd 75 03
	ld (iy+$04),h           ;0821	fd 74 04
	ld (iy+$05),c           ;0824	fd 71 05
	ld (iy+$06),b           ;0827	fd 70 06
	ld a,($2331)            ;082a	3a 31 23
	call L07A4              ;082d	cd a4 07
	and a                   ;0830	a7
	ret                     ;0831	c9

L0832:
	push iy                 ;0832	fd e5
	call L07C4              ;0834	cd c4 07
	jr c,L0850              ;0837	38 17
	push hl                 ;0839	e5
	ld e,(iy+$03)           ;083a	fd 5e 03
	ld d,(iy+$04)           ;083d	fd 56 04
	ld c,(iy+$05)           ;0840	fd 4e 05
	ld b,(iy+$06)           ;0843	fd 46 06
	call L1EAF              ;0846	cd af 1e
	pop hl                  ;0849	e1
	jr c,L0850              ;084a	38 04
	res 0,(iy+$00)          ;084c	fd cb 00 86

L0850:
	pop iy                  ;0850	fd e1
	ret                     ;0852	c9

L0853:
	ld a,($2330)            ;0853	3a 30 23

L0856:
	push ix                 ;0856	dd e5
	push iy                 ;0858	fd e5
	push af                 ;085a	f5
	call L0904              ;085b	cd 04 09
	ld a,(iy+$01)           ;085e	fd 7e 01
	ld ixl,a                ;0861	dd 6f
	ld a,(iy+$02)           ;0863	fd 7e 02
	ld ixh,a                ;0866	dd 67
	pop af                  ;0868	f1
	call L07B0              ;0869	cd b0 07
	bit 2,(iy+$00)          ;086c	fd cb 00 56
	ld (iy+$00),$01         ;0870	fd 36 00 01
	ld e,(iy+$03)           ;0874	fd 5e 03
	ld d,(iy+$04)           ;0877	fd 56 04
	ld c,(iy+$05)           ;087a	fd 4e 05
	ld b,(iy+$06)           ;087d	fd 46 06
	jr z,L08B1              ;0880	28 2f
	ld a,(ix+$18)           ;0882	dd 7e 18

L0885:
	push af                 ;0885	f5
	push hl                 ;0886	e5
	push bc                 ;0887	c5
	push de                 ;0888	d5
	call L1ECE              ;0889	cd ce 1e
	pop de                  ;088c	d1
	pop bc                  ;088d	c1
	jr nc,L08AD             ;088e	30 1d
	ld l,(ix+$14)           ;0890	dd 6e 14
	ld h,(ix+$15)           ;0893	dd 66 15
	add hl,de               ;0896	19
	ex de,hl                ;0897	eb
	ld l,(ix+$16)           ;0898	dd 6e 16
	ld h,(ix+$17)           ;089b	dd 66 17
	adc hl,bc               ;089e	ed 4a
	ld b,h                  ;08a0	44
	ld c,l                  ;08a1	4d
	pop hl                  ;08a2	e1
	pop af                  ;08a3	f1
	dec a                   ;08a4	3d
	jr nz,L0885             ;08a5	20 de
	scf                     ;08a7	37

L08A8:
	pop iy                  ;08a8	fd e1
	pop ix                  ;08aa	dd e1
	ret                     ;08ac	c9

L08AD:
	pop hl                  ;08ad	e1
	pop de                  ;08ae	d1
	jr L08A8                ;08af	18 f7

L08B1:
	call L1ECE              ;08b1	cd ce 1e
	jr L08A8                ;08b4	18 f2
Data08B6:
	defb 0x3a,0x30,0x23                          ;08b6	3a 30 23	:0#

L08B9:
	push af                 ;08b9	f5
	call L0904              ;08ba	cd 04 09
	pop af                  ;08bd	f1
	scf                     ;08be	37
	bit 0,(iy+$00)          ;08bf	fd cb 00 46
	ret z                   ;08c3	c8
	bit 1,(iy+$00)          ;08c4	fd cb 00 4e
	push bc                 ;08c8	c5
	call nz,L0856           ;08c9	c4 56 08
	pop bc                  ;08cc	c1
	ret                     ;08cd	c9

L08CE:
	call L08E2              ;08ce	cd e2 08

L08D1:
	push iy                 ;08d1	fd e5
	ld b,$02                ;08d3	06 02

L08D5:
	ld a,$02                ;08d5	3e 02
	sub b                   ;08d7	90
	call L08B9              ;08d8	cd b9 08
	jr nc,L08DF             ;08db	30 02
	djnz L08D5              ;08dd	10 f6

L08DF:
	pop iy                  ;08df	fd e1
	ret                     ;08e1	c9

L08E2:
	push iy                 ;08e2	fd e5
	ld a,($2330)            ;08e4	3a 30 23
	call L0904              ;08e7	cd 04 09
	set 1,(iy+$00)          ;08ea	fd cb 00 ce
	pop iy                  ;08ee	fd e1
	ret                     ;08f0	c9

L08F1:
	push iy                 ;08f1	fd e5
	ld a,($2330)            ;08f3	3a 30 23
	call L0904              ;08f6	cd 04 09
	set 1,(iy+$00)          ;08f9	fd cb 00 ce
	set 2,(iy+$00)          ;08fd	fd cb 00 d6
	pop iy                  ;0901	fd e1
	ret                     ;0903	c9

L0904:
	ld iy,$232b             ;0904	fd 21 2b 23
	ld de,$0007             ;0908	11 07 00
	inc a                   ;090b	3c

L090C:
	add iy,de               ;090c	fd 19
	dec a                   ;090e	3d
	jr nz,L090C             ;090f	20 fb
	ret                     ;0911	c9

L0912:
	call T05A1              ;0912	cd a1 05
	ld a,$02                ;0915	3e 02
	ret nc                  ;0917	d0
	bit 7,(ix+$13)          ;0918	dd cb 13 7e
	jr nz,L0926             ;091c	20 08
	sla c                   ;091e	cb 21
	rl b                    ;0920	cb 10
	rl e                    ;0922	cb 13
	rl d                    ;0924	cb 12

L0926:
	ld l,b                  ;0926	68
	ld h,e                  ;0927	63
	ld a,d                  ;0928	7a
	ld e,c                  ;0929	59
	ld c,(ix+$19)           ;092a	dd 4e 19
	ld b,(ix+$1a)           ;092d	dd 46 1a
	add hl,bc               ;0930	09
	ex de,hl                ;0931	eb
	adc $00                 ;0932	ce 00
	ld c,a                  ;0934	4f
	ld a,$00                ;0935	3e 00
	adc a                   ;0937	8f
	ld b,a                  ;0938	47
	ld h,$00                ;0939	26 00
	add hl,hl               ;093b	29
	push hl                 ;093c	e5
	call L0832              ;093d	cd 32 08
	pop de                  ;0940	d1
	ret nc                  ;0941	d0
	add hl,de               ;0942	19
	ld c,(hl)               ;0943	4e
	inc hl                  ;0944	23
	ld b,(hl)               ;0945	46
	bit 7,(ix+$13)          ;0946	dd cb 13 7e
	jr nz,L0955             ;094a	20 09
	inc hl                  ;094c	23
	ld e,(hl)               ;094d	5e
	inc hl                  ;094e	23
	ld a,(hl)               ;094f	7e
	and $0f                 ;0950	e6 0f
	ld d,a                  ;0952	57
	scf                     ;0953	37
	ret                     ;0954	c9

L0955:
	ld de,$0000             ;0955	11 00 00
	scf                     ;0958	37
	ret                     ;0959	c9

L095A:
	ld ($2012),hl           ;095a	22 12 20
	call T05A1              ;095d	cd a1 05
	ld a,$02                ;0960	3e 02
	ret nc                  ;0962	d0
	bit 7,(ix+$13)          ;0963	dd cb 13 7e
	jr nz,L0971             ;0967	20 08
	sla c                   ;0969	cb 21
	rl b                    ;096b	cb 10
	rl e                    ;096d	cb 13
	rl d                    ;096f	cb 12

L0971:
	ld l,b                  ;0971	68
	ld h,e                  ;0972	63
	ld a,d                  ;0973	7a
	ld e,c                  ;0974	59
	ld c,(ix+$19)           ;0975	dd 4e 19
	ld b,(ix+$1a)           ;0978	dd 46 1a
	add hl,bc               ;097b	09
	ex de,hl                ;097c	eb
	adc $00                 ;097d	ce 00
	ld c,a                  ;097f	4f
	ld a,$00                ;0980	3e 00
	adc a                   ;0982	8f
	ld b,a                  ;0983	47
	ld h,$00                ;0984	26 00
	add hl,hl               ;0986	29
	push hl                 ;0987	e5
	call L0832              ;0988	cd 32 08
	pop de                  ;098b	d1
	ret nc                  ;098c	d0
	add hl,de               ;098d	19
	ld de,($2012)           ;098e	ed 5b 12 20
	ld (hl),e               ;0992	73
	inc hl                  ;0993	23
	ld (hl),d               ;0994	72
	bit 7,(ix+$13)          ;0995	dd cb 13 7e
	jr nz,L09AA             ;0999	20 0f
	inc hl                  ;099b	23
	push iy                 ;099c	fd e5
	pop de                  ;099e	d1
	ld (hl),e               ;099f	73
	inc hl                  ;09a0	23
	ld a,d                  ;09a1	7a
	and $0f                 ;09a2	e6 0f
	ld d,a                  ;09a4	57
	ld a,(hl)               ;09a5	7e
	and $f0                 ;09a6	e6 f0
	or d                    ;09a8	b2
	ld (hl),a               ;09a9	77

L09AA:
	call L08F1              ;09aa	cd f1 08
	scf                     ;09ad	37
	ret                     ;09ae	c9
	call L078F              ;09af	cd 8f 07
	ld b,$00                ;09b2	06 00
	ld ix,$2e8e             ;09b4	dd 21 8e 2e
	call L0A3F              ;09b8	cd 3f 0a
	ld b,$01                ;09bb	06 01
	ld ix,$2ea1             ;09bd	dd 21 a1 2e
	call c,L0A3F            ;09c1	dc 3f 0a
	ld b,$01                ;09c4	06 01
	ret nc                  ;09c6	d0
	inc b                   ;09c7	04
	ret                     ;09c8	c9
	call L0A0A              ;09c9	cd 0a 0a
	call L0D10              ;09cc	cd 10 0d
	ld a,d                  ;09cf	7a
	inc a                   ;09d0	3c
	scf                     ;09d1	37
	call z,L0CB1            ;09d2	cc b1 0c
	ret nc                  ;09d5	d0
	ld hl,$0000             ;09d6	21 00 00
	push hl                 ;09d9	e5
	pop iy                  ;09da	fd e1
	ld a,(ix+$21)           ;09dc	dd 7e 21

L09DF:
	add iy,bc               ;09df	fd 09
	adc hl,de               ;09e1	ed 5a
	dec a                   ;09e3	3d
	jr nz,L09DF             ;09e4	20 f9
	push iy                 ;09e6	fd e5
	pop de                  ;09e8	d1
	srl h                   ;09e9	cb 3c
	rr l                    ;09eb	cb 1d
	rr d                    ;09ed	cb 1a
	rr e                    ;09ef	cb 1b
	ld b,h                  ;09f1	44
	ld c,l                  ;09f2	4d
	ld h,d                  ;09f3	62
	ld l,e                  ;09f4	6b
	ld a,b                  ;09f5	78
	or c                    ;09f6	b1
	jr z,L09FC              ;09f7	28 03
	ld hl,$ffff             ;09f9	21 ff ff

L09FC:
	scf                     ;09fc	37
	ret                     ;09fd	c9

; tentative entry (word / jump table scan)
T09FE:
	add a                   ;09fe	87
	ld hl,$2e6f             ;09ff	21 6f 2e
	add hl,a                ;0a02	ed 31
	ld d,(hl)               ;0a04	56
	dec hl                  ;0a05	2b
	ld e,(hl)               ;0a06	5e
	ld a,d                  ;0a07	7a
	or e                    ;0a08	b3
	ret                     ;0a09	c9

L0A0A:
	call T09FE              ;0a0a	cd fe 09
	push de                 ;0a0d	d5
	pop iy                  ;0a0e	fd e1
	ld e,(iy+$00)           ;0a10	fd 5e 00
	ld d,(iy+$01)           ;0a13	fd 56 01
	push de                 ;0a16	d5
	pop ix                  ;0a17	dd e1
	ret                     ;0a19	c9

L0A1A:
	call L0A0A              ;0a1a	cd 0a 0a
	push iy                 ;0a1d	fd e5
	pop hl                  ;0a1f	e1
	ld de,$2e50             ;0a20	11 50 2e
	push de                 ;0a23	d5
	ld bc,$000f             ;0a24	01 0f 00
	ldir                    ;0a27	ed b0
	pop iy                  ;0a29	fd e1
	ret                     ;0a2b	c9

L0A2C:
	and $01                 ;0a2c	e6 01
	ld hl,$2e8e             ;0a2e	21 8e 2e
	jr z,L0A36              ;0a31	28 03
	ld hl,$2ea1             ;0a33	21 a1 2e

L0A36:
	push ix                 ;0a36	dd e5
	pop de                  ;0a38	d1
	ld bc,$0013             ;0a39	01 13 00
	ldir                    ;0a3c	ed b0
	ret                     ;0a3e	c9

L0A3F:
	ld (ix+$10),b           ;0a3f	dd 70 10
	ld c,b                  ;0a42	48
	call L0EFC              ;0a43	cd fc 0e
	ret nc                  ;0a46	d0
	ld a,(ix+$10)           ;0a47	dd 7e 10
	or b                    ;0a4a	b0
	ld (ix+$10),a           ;0a4b	dd 77 10
	dec hl                  ;0a4e	2b
	ld a,h                  ;0a4f	7c
	and l                   ;0a50	a5
	inc a                   ;0a51	3c
	jr nz,L0A55             ;0a52	20 01
	dec de                  ;0a54	1b

L0A55:
	ld b,h                  ;0a55	44
	ld c,l                  ;0a56	4d
	call T04F8              ;0a57	cd f8 04
	rlca                    ;0a5a	07
	scf                     ;0a5b	37
	ret                     ;0a5c	c9
L0A5D:
	defb 0x03,0x04,0x05,0xc2,0x2a,0x0b,0x41,0xc5 ;0a5d	03 04 05 c2 2a 0b 41 c5	....*.A.
	defb 0xdd,0xe5,0xcd,0x2c,0x0a,0xdd,0x21,0x00 ;0a65	dd e5 cd 2c 0a dd 21 00	...,..!.
	defb 0x00,0xcd,0xbc,0x07,0x22,0x2c,0x20,0xdd ;0a6d	00 cd bc 07 22 2c 20 dd	....", .
	defb 0xe1,0x3e,0x01,0xdd,0x77,0x2a,0xc1,0xc5 ;0a75	e1 3e 01 dd 77 2a c1 c5	.>..w*..
	defb 0x0d,0xdd,0x71,0x11,0x01,0x00,0x00,0xdd ;0a7d	0d dd 71 11 01 00 00 dd	..q.....
	defb 0x70,0x12,0x50,0x59,0x2a,0x2c,0x20,0xe5 ;0a85	70 12 50 59 2a 2c 20 e5	p.PY*, .
	defb 0xcd,0xaf,0x1e,0xe1,0xd2,0x29,0x0b,0x24 ;0a8d	cd af 1e e1 d2 29 0b 24	.....).$
	defb 0xe5,0xfd,0xe1,0x24,0x2b,0x7e,0xfe,0xaa ;0a95	e5 fd e1 24 2b 7e fe aa	...$+~..
	defb 0x20,0x04,0x2b,0x7e,0xfe,0x55,0xc2,0x29 ;0a9d	20 04 2b 7e fe 55 c2 29	 .+~.U.)
	defb 0x0b,0x11,0xbe,0x00,0xdd,0x7e,0x2a,0xfd ;0aa5	0b 11 be 00 dd 7e 2a fd	.....~*.
	defb 0x19,0x1e,0x10,0x3d,0x20,0xf9,0xfd,0x7e ;0aad	19 1e 10 3d 20 f9 fd 7e	...= ..~
	defb 0x04,0xa7,0x28,0x28,0xef,0x08,0x7a,0xb3 ;0ab5	04 a7 28 28 ef 08 7a b3	..((..z.
	defb 0xb0,0xb1,0x28,0x20,0xc5,0xd5,0x50,0x59 ;0abd	b0 b1 28 20 c5 d5 50 59	..( ..PY
	defb 0xc1,0xc5,0xcd,0xd2,0x1e,0xd1,0xc1,0x30 ;0ac5	c1 c5 cd d2 1e d1 c1 30	.......0
	defb 0x13,0xcd,0xf8,0x04,0x01,0xef,0x0c,0x7a ;0acd	13 cd f8 04 01 ef 0c 7a	.......z
	defb 0xb3,0xb0,0xb1,0x28,0x07,0xcd,0xf8,0x04 ;0ad5	b3 b0 b1 28 07 cd f8 04	...(....
	defb 0x07,0xcd,0x2e,0x0b,0xc1,0x30,0x28,0x10 ;0add	07 cd 2e 0b c1 30 28 10	.....0(.
	defb 0x26,0x0d,0xfd,0xe5,0xe1,0x7e,0xfe,0x29 ;0ae5	26 0d fd e5 e1 7e fe 29	&....~.)
	defb 0x11,0x02,0x0f,0x20,0x0e,0x3e,0x05,0xed ;0aed	11 02 0f 20 0e 3e 05 ed	... .>..
	defb 0x31,0x11,0x2e,0x20,0xd5,0x01,0x0b,0x00 ;0af5	31 11 2e 20 d5 01 0b 00	1.. ....
	defb 0xed,0xb0,0xd1,0xd5,0xcd,0x19,0x12,0xd1 ;0afd	ed b0 d1 d5 cd 19 12 d1	........
	defb 0x30                                    ;0b05	30	0
	inc bc                  ;0b06	03
	jr nz,L0B0A             ;0b07	20 01
	ex de,hl                ;0b09	eb

L0B0A:
	scf                     ;0b0a	37
	ret                     ;0b0b	c9
	push bc                 ;0b0c	c5
	ld a,(ix+$10)           ;0b0d	dd 7e 10
	call L0A2C              ;0b10	cd 2c 0a
	ld a,(ix+$2a)           ;0b13	dd 7e 2a
	inc a                   ;0b16	3c
	cp $05                  ;0b17	fe 05
	jp c,L0A78              ;0b19	da 78 0a
	pop bc                  ;0b1c	c1
	dec c                   ;0b1d	0d
	jr nz,L0B2A             ;0b1e	20 0a
	ld (ix+$2a),c           ;0b20	dd 71 2a
	call L0B2E              ;0b23	cd 2e 0b
	jr c,L0AE6              ;0b26	38 be
	ret                     ;0b28	c9
Data0B29:
	defb 0xe1                                    ;0b29	e1	.

L0B2A:
	ld a,$38                ;0b2a	3e 38
	and a                   ;0b2c	a7
	ret                     ;0b2d	c9

L0B2E:
	ld b,$00                ;0b2e	06 00
	ld c,b                  ;0b30	48
	ld d,b                  ;0b31	50
	ld e,b                  ;0b32	58
	ld hl,($202c)           ;0b33	2a 2c 20
	push hl                 ;0b36	e5
	call L1EAF              ;0b37	cd af 1e
	pop iy                  ;0b3a	fd e1
	ret nc                  ;0b3c	d0
	rst $28                 ;0b3d	ef
	dec bc                  ;0b3e	0b
	ld a,c                  ;0b3f	79
	and a                   ;0b40	a7
	jr nz,L0B2A             ;0b41	20 e7
	ld a,b                  ;0b43	78
	cp $02                  ;0b44	fe 02
	jr nz,L0B2A             ;0b46	20 e2
	ld a,e                  ;0b48	7b
	and a                   ;0b49	a7
	jr z,L0B2A              ;0b4a	28 de
	ld (ix+$21),a           ;0b4c	dd 77 21
	rst $28                 ;0b4f	ef
	ld c,$78                ;0b50	0e 78
	or c                    ;0b52	b1
	jr z,L0B2A              ;0b53	28 d5
	ld (ix+$19),c           ;0b55	dd 71 19
	ld (ix+$1a),b           ;0b58	dd 70 1a
	ld h,b                  ;0b5b	60
	ld l,c                  ;0b5c	69
	rst $28                 ;0b5d	ef
	ld d,$78                ;0b5e	16 78
	or c                    ;0b60	b1
	jr z,L0B6C              ;0b61	28 09
	ld (ix+$13),$80         ;0b63	dd 36 13 80
	ld de,$0000             ;0b67	11 00 00
	jr L0B71                ;0b6a	18 05

L0B6C:
	ld (ix+$13),a           ;0b6c	dd 77 13
	rst $28                 ;0b6f	ef
	inc h                   ;0b70	24

L0B71:
	call T04F8              ;0b71	cd f8 04
	inc d                   ;0b74	14
	ld a,(iy+$10)           ;0b75	fd 7e 10
	and a                   ;0b78	a7

L0B79:
	jr z,L0B2A              ;0b79	28 af
	ld (ix+$18),a           ;0b7b	dd 77 18
	push hl                 ;0b7e	e5
	ex (sp),iy              ;0b7f	fd e3
	ld hl,$0000             ;0b81	21 00 00

L0B84:
	add iy,bc               ;0b84	fd 09
	adc hl,de               ;0b86	ed 5a
	dec a                   ;0b88	3d
	jr nz,L0B84             ;0b89	20 f9
	ex (sp),iy              ;0b8b	fd e3
	rst $28                 ;0b8d	ef
	ld de,$ddd1             ;0b8e	11 d1 dd
	rl e                    ;0b91	cb 13
	ld a,(hl)               ;0b93	7e
	jr nz,L0BA4             ;0b94	20 0e
	ld a,b                  ;0b96	78
	or c                    ;0b97	b1

L0B98:
	jr nz,L0B2A             ;0b98	20 90
	push de                 ;0b9a	d5
	rst $28                 ;0b9b	ef
	inc l                   ;0b9c	2c
	call T04F8              ;0b9d	cd f8 04
	dec e                   ;0ba0	1d
	pop de                  ;0ba1	d1
	jr L0BCC                ;0ba2	18 28

L0BA4:
	push bc                 ;0ba4	c5
	ex de,hl                ;0ba5	eb
	ld b,h                  ;0ba6	44
	ld c,l                  ;0ba7	4d
	call T04F8              ;0ba8	cd f8 04
	dec e                   ;0bab	1d
	ex de,hl                ;0bac	eb
	pop bc                  ;0bad	c1
	ld a,c                  ;0bae	79
	and $0f                 ;0baf	e6 0f
	jr nz,L0B98             ;0bb1	20 e5
	ld a,$04                ;0bb3	3e 04

L0BB5:
	srl b                   ;0bb5	cb 38
	rr c                    ;0bb7	cb 19
	dec a                   ;0bb9	3d
	jr nz,L0BB5             ;0bba	20 f9
	ld a,b                  ;0bbc	78
	or c                    ;0bbd	b1
	jr z,L0B79              ;0bbe	28 b9
	ld (ix+$1b),c           ;0bc0	dd 71 1b
	ld (ix+$1c),b           ;0bc3	dd 70 1c
	ex de,hl                ;0bc6	eb
	add hl,bc               ;0bc7	09
	ex de,hl                ;0bc8	eb
	jr nc,L0BCC             ;0bc9	30 01
	inc hl                  ;0bcb	23

L0BCC:
	push hl                 ;0bcc	e5
	push de                 ;0bcd	d5
	ex de,hl                ;0bce	eb
	ld b,$00                ;0bcf	06 00
	ld c,(ix+$21)           ;0bd1	dd 4e 21
	and a                   ;0bd4	a7
	sbc hl,bc               ;0bd5	ed 42
	ex de,hl                ;0bd7	eb
	ld c,b                  ;0bd8	48
	sbc hl,bc               ;0bd9	ed 42
	ex de,hl                ;0bdb	eb
	ld c,(ix+$21)           ;0bdc	dd 4e 21
	sbc hl,bc               ;0bdf	ed 42
	ex de,hl                ;0be1	eb
	ld c,b                  ;0be2	48
	sbc hl,bc               ;0be3	ed 42
	ex de,hl                ;0be5	eb
	ld b,h                  ;0be6	44
	ld c,l                  ;0be7	4d
	call T04F8              ;0be8	cd f8 04
	ld h,$ef                ;0beb	26 ef
	inc de                  ;0bed	13
	ld de,$0000             ;0bee	11 00 00
	ld a,b                  ;0bf1	78
	or c                    ;0bf2	b1
	jr nz,L0BF7             ;0bf3	20 02
	rst $28                 ;0bf5	ef
	jr nz,L0C58             ;0bf6	20 60
	ld l,c                  ;0bf8	69
	ld b,d                  ;0bf9	42
	ld c,e                  ;0bfa	4b
	pop de                  ;0bfb	d1
	sbc hl,de               ;0bfc	ed 52
	ex de,hl                ;0bfe	eb
	ld h,b                  ;0bff	60
	ld l,c                  ;0c00	69
	pop bc                  ;0c01	c1
	sbc hl,bc               ;0c02	ed 42
	ld a,(ix+$21)           ;0c04	dd 7e 21

L0C07:
	srl a                   ;0c07	cb 3f
	jr z,L0C15              ;0c09	28 0a
	srl h                   ;0c0b	cb 3c
	rr l                    ;0c0d	cb 1d
	rr d                    ;0c0f	cb 1a
	rr e                    ;0c11	cb 1b
	jr L0C07                ;0c13	18 f2
L0C15:
	defb 0x13,0x7a,0xb3,0x20,0x01,0x23,0xeb,0x44 ;0c15	13 7a b3 20 01 23 eb 44	.z. .#.D
	defb 0x4d,0xcd,0xf8,0x04,0x22,0x7a,0xb3,0x20 ;0c1d	4d cd f8 04 22 7a b3 20	M..."z. 
	defb 0x08,0x21,0xf5                          ;0c25	08 21 f5	.!.
	rrca                    ;0c28	0f
	sbc hl,bc               ;0c29	ed 42
	jp nc,L0B2A             ;0c2b	d2 2a 0b
	srl d                   ;0c2e	cb 3a
	rr e                    ;0c30	cb 1b
	rr b                    ;0c32	cb 18
	rr c                    ;0c34	cb 19
	call T04F8              ;0c36	cd f8 04
	cpl                     ;0c39	2f
	bit 7,(ix+$13)          ;0c3a	dd cb 13 7e
	jp nz,L0CA0             ;0c3e	c2 a0 0c
	ld e,(iy+$30)           ;0c41	fd 5e 30
	ld d,(iy+$31)           ;0c44	fd 56 31
	ld (ix+$33),e           ;0c47	dd 73 33
	ld (ix+$34),d           ;0c4a	dd 72 34
	ld bc,$0000             ;0c4d	01 00 00
	call L0832              ;0c50	cd 32 08
	jr nc,L0C93             ;0c53	30 3e
	ld de,$01e4             ;0c55	11 e4 01

L0C58:
	add hl,de               ;0c58	19
	ld a,(hl)               ;0c59	7e
	inc hl                  ;0c5a	23
	cp $72                  ;0c5b	fe 72
	jr nz,L0C93             ;0c5d	20 34
	ld a,(hl)               ;0c5f	7e
	inc hl                  ;0c60	23
	cp $72                  ;0c61	fe 72
	jr nz,L0C93             ;0c63	20 2e
	ld a,(hl)               ;0c65	7e
	inc hl                  ;0c66	23
	cp $41                  ;0c67	fe 41
	jr nz,L0C93             ;0c69	20 28
	ld a,(hl)               ;0c6b	7e
	inc hl                  ;0c6c	23
	cp $61                  ;0c6d	fe 61
	jr nz,L0C93             ;0c6f	20 22
	ld c,(hl)               ;0c71	4e
	inc hl                  ;0c72	23
	ld b,(hl)               ;0c73	46
	inc hl                  ;0c74	23
	ld e,(hl)               ;0c75	5e
	inc hl                  ;0c76	23
	ld d,(hl)               ;0c77	56
	inc hl                  ;0c78	23
	call L05AA              ;0c79	cd aa 05
	jr nc,L0C93             ;0c7c	30 15
	call T04F8              ;0c7e	cd f8 04
	dec hl                  ;0c81	2b
	ld c,(hl)               ;0c82	4e
	inc hl                  ;0c83	23
	ld b,(hl)               ;0c84	46
	inc hl                  ;0c85	23
	ld e,(hl)               ;0c86	5e
	inc hl                  ;0c87	23
	ld d,(hl)               ;0c88	56
	call T04F8              ;0c89	cd f8 04
	cpl                     ;0c8c	2f
	set 6,(ix+$13)          ;0c8d	dd cb 13 f6
	jr L0C9B                ;0c91	18 08

L0C93:
	call L04E1              ;0c93	cd e1 04
	cpl                     ;0c96	2f
	call T04F8              ;0c97	cd f8 04
	dec hl                  ;0c9a	2b

L0C9B:
	ld de,$0042             ;0c9b	11 42 00
	jr L0CAD                ;0c9e	18 0d

L0CA0:
	ld de,$ffff             ;0ca0	11 ff ff
	ld bc,$0000             ;0ca3	01 00 00
	call T04F8              ;0ca6	cd f8 04
	dec hl                  ;0ca9	2b
	ld de,$0026             ;0caa	11 26 00

L0CAD:
	add iy,de               ;0cad	fd 19
	scf                     ;0caf	37
	ret                     ;0cb0	c9

L0CB1:
	ld bc,$0000             ;0cb1	01 00 00
	push bc                 ;0cb4	c5
	ld d,b                  ;0cb5	50
	ld e,c                  ;0cb6	59
	call T04F8              ;0cb7	cd f8 04
	dec hl                  ;0cba	2b
	call L04E1              ;0cbb	cd e1 04
	inc d                   ;0cbe	14
	push de                 ;0cbf	d5
	pop iy                  ;0cc0	fd e1
	ld h,b                  ;0cc2	60
	ld l,c                  ;0cc3	69
	call L04E1              ;0cc4	cd e1 04
	rla                     ;0cc7	17
	pop bc                  ;0cc8	c1

L0CC9:
	push de                 ;0cc9	d5
	push bc                 ;0cca	c5
	push hl                 ;0ccb	e5
	call L0832              ;0ccc	cd 32 08
	jr nc,L0D16             ;0ccf	30 45
	ld b,$00                ;0cd1	06 00
	bit 7,(ix+$13)          ;0cd3	dd cb 13 7e
	jr nz,L0CEE             ;0cd7	20 15
	ld b,$80                ;0cd9	06 80

L0CDB:
	ld a,(hl)               ;0cdb	7e
	inc hl                  ;0cdc	23
	or (hl)                 ;0cdd	b6
	inc hl                  ;0cde	23
	or (hl)                 ;0cdf	b6
	inc hl                  ;0ce0	23
	ld c,a                  ;0ce1	4f
	ld a,(hl)               ;0ce2	7e
	inc hl                  ;0ce3	23
	and $0f                 ;0ce4	e6 0f
	or c                    ;0ce6	b1
	call z,Inc32BitPosition ;0ce7	cc df 1f
	djnz L0CDB              ;0cea	10 ef
	jr L0CF7                ;0cec	18 09

L0CEE:
	ld a,(hl)               ;0cee	7e
	inc hl                  ;0cef	23
	or (hl)                 ;0cf0	b6
	inc hl                  ;0cf1	23
	call z,Inc32BitPosition ;0cf2	cc df 1f
	djnz L0CEE              ;0cf5	10 f7

L0CF7:
	pop hl                  ;0cf7	e1
	pop bc                  ;0cf8	c1
	pop de                  ;0cf9	d1
	inc de                  ;0cfa	13
	ld a,d                  ;0cfb	7a
	or e                    ;0cfc	b3
	jr nz,L0D00             ;0cfd	20 01
	inc bc                  ;0cff	03

L0D00:
	dec hl                  ;0d00	2b
	ld a,h                  ;0d01	7c
	and l                   ;0d02	a5
	inc a                   ;0d03	3c
	jr nz,L0D08             ;0d04	20 02
	dec iy                  ;0d06	fd 2b

L0D08:
	ld a,iyh                ;0d08	fd 7c
	or iyl                  ;0d0a	fd b5
	or h                    ;0d0c	b4
	or l                    ;0d0d	b5
	jr nz,L0CC9             ;0d0e	20 b9

L0D10:
	call L04E1              ;0d10	cd e1 04
	dec hl                  ;0d13	2b
	scf                     ;0d14	37
	ret                     ;0d15	c9

L0D16:
	pop hl                  ;0d16	e1
	pop bc                  ;0d17	c1
	pop de                  ;0d18	d1
	ret                     ;0d19	c9

L0D1A:
	push ix                 ;0d1a	dd e5
	push bc                 ;0d1c	c5
	push hl                 ;0d1d	e5
	ld ix,$203e             ;0d1e	dd 21 3e 20
	call L0A5D              ;0d22	cd 5d 0a
	pop hl                  ;0d25	e1
	jr nc,L0D65             ;0d26	30 3d
	ld a,(ix+$2a)           ;0d28	dd 7e 2a
	add $30                 ;0d2b	c6 30
	ld (hl),a               ;0d2d	77
	inc hl                  ;0d2e	23
	ld (hl),$3e             ;0d2f	36 3e
	inc hl                  ;0d31	23
	ex de,hl                ;0d32	eb
	ld bc,$000b             ;0d33	01 0b 00
	ldir                    ;0d36	ed b0
	ex de,hl                ;0d38	eb
	ld b,$03                ;0d39	06 03

L0D3B:
	ld (hl),$20             ;0d3b	36 20
	inc hl                  ;0d3d	23
	djnz L0D3B              ;0d3e	10 fb
	ld (hl),$10             ;0d40	36 10
	bit 7,(ix+$13)          ;0d42	dd cb 13 7e
	jr nz,L0D49             ;0d46	20 01
	inc (hl)                ;0d48	34

L0D49:
	inc hl                  ;0d49	23
	ld b,$06                ;0d4a	06 06

L0D4C:
	ld (hl),$ff             ;0d4c	36 ff
	inc hl                  ;0d4e	23
	djnz L0D4C              ;0d4f	10 fb
	ex de,hl                ;0d51	eb
	push ix                 ;0d52	dd e5
	pop hl                  ;0d54	e1
	ld bc,$0007             ;0d55	01 07 00
	add hl,bc               ;0d58	09
	ld c,$04                ;0d59	0e 04
	ldir                    ;0d5b	ed b0
	ex de,hl                ;0d5d	eb
	ld b,$25                ;0d5e	06 25

L0D60:
	ld (hl),c               ;0d60	71
	inc hl                  ;0d61	23
	djnz L0D60              ;0d62	10 fc
	scf                     ;0d64	37

L0D65:
	pop bc                  ;0d65	c1
	pop ix                  ;0d66	dd e1
	ret                     ;0d68	c9
	ld bc,$0000             ;0d69	01 00 00

L0D6C:
	push af                 ;0d6c	f5
	push bc                 ;0d6d	c5
	push hl                 ;0d6e	e5
	ld hl,$2200             ;0d6f	21 00 22
	call L0D1A              ;0d72	cd 1a 0d
	pop hl                  ;0d75	e1
	jr nc,L0DB5             ;0d76	30 3d
	push hl                 ;0d78	e5
	inc hl                  ;0d79	23
	ld a,(hl)               ;0d7a	7e
	cp $3e                  ;0d7b	fe 3e
	pop hl                  ;0d7d	e1
	push hl                 ;0d7e	e5
	ld bc,$0010             ;0d7f	01 10 00
	ld de,$2200             ;0d82	11 00 22
	scf                     ;0d85	37
	jr z,L0D8D              ;0d86	28 05
	ld c,$0e                ;0d88	0e 0e
	inc de                  ;0d8a	13
	inc de                  ;0d8b	13
	and a                   ;0d8c	a7

L0D8D:
	ld a,(de)               ;0d8d	1a
	inc de                  ;0d8e	13
	cpi                     ;0d8f	ed a1
	jr nz,L0D9B             ;0d91	20 08
	jp pe,L0D8D             ;0d93	ea 8d 0d

L0D96:
	pop hl                  ;0d96	e1
	pop bc                  ;0d97	c1
	pop hl                  ;0d98	e1
	scf                     ;0d99	37
	ret                     ;0d9a	c9

L0D9B:
	jr nc,L0DAF             ;0d9b	30 12
	ld a,c                  ;0d9d	79
	cp $0d                  ;0d9e	fe 0d
	jr nz,L0DAF             ;0da0	20 0d
	ld a,$20                ;0da2	3e 20
	dec hl                  ;0da4	2b
	ld b,$0e                ;0da5	06 0e

L0DA7:
	cp (hl)                 ;0da7	be
	jr nz,L0DAF             ;0da8	20 05
	inc hl                  ;0daa	23
	djnz L0DA7              ;0dab	10 fa
	jr L0D96                ;0dad	18 e7

L0DAF:
	pop hl                  ;0daf	e1
	pop bc                  ;0db0	c1
	pop af                  ;0db1	f1
	inc bc                  ;0db2	03
	jr L0D6C                ;0db3	18 b7

L0DB5:
	pop bc                  ;0db5	c1
	pop bc                  ;0db6	c1
	ret                     ;0db7	c9
Text0DB8:
	defb 0xc1,0xc1,0xc1,0xc3,0x24,0x0e,0xdd,0xe5 ;0db8	c1 c1 c1 c3 24 0e dd e5	....$...
	defb 0x7b,0x1c,0xd5,0xcd,0xfe,0x09,0xd1,0xe5 ;0dc0	7b 1c d5 cd fe 09 d1 e5	{.......
	defb 0xd5,0xaf,0xf5,0xcd,0xfe,0x09,0x28,0x21 ;0dc8	d5 af f5 cd fe 09 28 21	......(!
	defb 0xeb,0x5e,0x23,0x56,0xd5,0xfd,0xe1,0x79 ;0dd0	eb 5e 23 56 d5 fd e1 79	.^#V...y
	defb 0xfd,0xbe,0x11,0x20,0x14,0x78,0xfd,0xbe ;0dd8	fd be 11 20 14 78 fd be	... .x..
	defb 0x12,0x20,0x0e,0xe1,0xd1,0xd5,0xe5,0xfd ;0de0	12 20 0e e1 d1 d5 e5 fd	. ......
	defb 0x7e,0x10,0xe6,0x01,0xba,0x3e,0x3b,0x28 ;0de8	7e 10 e6 01 ba 3e 3b 28	~....>;(
	defb 0xc7,0xf1,0x3c,0xfe,0x10,0x38,0xd3,0xe1 ;0df0	c7 f1 3c fe 10 38 d3 e1	..<..8..
	defb 0xe5,0xdd,0x21,0xdb,0x29,0x11,0x35,0x00 ;0df8	e5 dd 21 db 29 11 35 00	..!.).5.
	defb 0xdd,0x19,0x2d,0x20,0xfb,0x7c,0xcd,0x5d ;0e00	dd 19 2d 20 fb 7c cd 5d	..- .|.]
	defb 0x0a,0x30,0xae,0xd1,0xfd,0x21           ;0e08	0a 30 ae d1 fd 21	.0...!
	ld d,c                  ;0e0e	51
	dec l                   ;0e0f	2d
	ld bc,$000f             ;0e10	01 0f 00

L0E13:
	add iy,bc               ;0e13	fd 09
	dec e                   ;0e15	1d
	jr nz,L0E13             ;0e16	20 fb
	call L10CA              ;0e18	cd ca 10
	pop hl                  ;0e1b	e1
	ld a,iyl                ;0e1c	fd 7d
	ld (hl),a               ;0e1e	77
	inc hl                  ;0e1f	23
	ld a,iyh                ;0e20	fd 7c
	ld (hl),a               ;0e22	77
	scf                     ;0e23	37
	pop ix                  ;0e24	dd e1
	ret                     ;0e26	c9
	ld hl,$2eb4             ;0e27	21 b4 2e
	ld b,$11                ;0e2a	06 11

L0E2C:
	bit 7,(hl)              ;0e2c	cb 7e
	jr z,L0E39              ;0e2e	28 09
	push hl                 ;0e30	e5
	add hl,$0005            ;0e31	ed 34 05 00
	cp (hl)                 ;0e35	be
	jr z,L0E5D              ;0e36	28 25
	pop hl                  ;0e38	e1

L0E39:
	add hl,$0006            ;0e39	ed 34 06 00
	djnz L0E2C              ;0e3d	10 ed
	call T09FE              ;0e3f	cd fe 09
	push hl                 ;0e42	e5
	ex de,hl                ;0e43	eb
	ld e,(hl)               ;0e44	5e
	inc hl                  ;0e45	23
	ld d,(hl)               ;0e46	56
	ld iy,$2740             ;0e47	fd 21 40 27
	ld b,$10                ;0e4b	06 10

L0E4D:
	ld a,(iy+$0f)           ;0e4d	fd 7e 0f
	and a                   ;0e50	a7
	jr z,L0E61              ;0e51	28 0e
	ld l,(iy+$00)           ;0e53	fd 6e 00
	ld h,(iy+$01)           ;0e56	fd 66 01
	sbc hl,de               ;0e59	ed 52
	jr nz,L0E61             ;0e5b	20 04

L0E5D:
	pop hl                  ;0e5d	e1
	ld a,$24                ;0e5e	3e 24
	ret                     ;0e60	c9

L0E61:
	ex de,hl                ;0e61	eb
	ld de,$002d             ;0e62	11 2d 00
	add iy,de               ;0e65	fd 19
	ex de,hl                ;0e67	eb
	djnz L0E4D              ;0e68	10 e3
	pop hl                  ;0e6a	e1
	xor a                   ;0e6b	af
	ld (hl),a               ;0e6c	77
	inc hl                  ;0e6d	23
	ld (hl),a               ;0e6e	77
	scf                     ;0e6f	37
	ret                     ;0e70	c9
	call T09FE              ;0e71	cd fe 09
	ex de,hl                ;0e74	eb
	ld e,(hl)               ;0e75	5e
	inc hl                  ;0e76	23
	ld d,(hl)               ;0e77	56
	push de                 ;0e78	d5
	pop iy                  ;0e79	fd e1
	ld a,(iy+$10)           ;0e7b	fd 7e 10
	res 1,a                 ;0e7e	cb 8f
	push af                 ;0e80	f5
	add $30                 ;0e81	c6 30
	ld (bc),a               ;0e83	02
	inc bc                  ;0e84	03
	ld a,$3e                ;0e85	3e 3e
	ld (bc),a               ;0e87	02
	inc bc                  ;0e88	03
	pop af                  ;0e89	f1
	push bc                 ;0e8a	c5
	ld ($200f),a            ;0e8b	32 0f 20
	rst $28                 ;0e8e	ef
	ld de,$0021             ;0e8f	11 21 00
	ld ($cde5),hl           ;0e92	22 e5 cd
	ld a,(de)               ;0e95	1a
	dec c                   ;0e96	0d
	pop hl                  ;0e97	e1
	pop de                  ;0e98	d1
	ret nc                  ;0e99	d0
	push bc                 ;0e9a	c5
	ld bc,$0010             ;0e9b	01 10 00
	ldir                    ;0e9e	ed b0
	ex de,hl                ;0ea0	eb
	ld (hl),$ff             ;0ea1	36 ff
	pop bc                  ;0ea3	c1
	ld a,($200f)            ;0ea4	3a 0f 20
	ret                     ;0ea7	c9
	push ix                 ;0ea8	dd e5
	push iy                 ;0eaa	fd e5
	ld b,$10                ;0eac	06 10

L0EAE:
	push bc                 ;0eae	c5
	ld a,b                  ;0eaf	78
	dec a                   ;0eb0	3d
	call L1354              ;0eb1	cd 54 13
	ld a,(iy+$0f)           ;0eb4	fd 7e 0f
	and a                   ;0eb7	a7
	call nz,L16CF           ;0eb8	c4 cf 16
	pop bc                  ;0ebb	c1
	djnz L0EAE              ;0ebc	10 f0
	pop iy                  ;0ebe	fd e1
	pop ix                  ;0ec0	dd e1
	jp L08D1                ;0ec2	c3 d1 08

L0EC5:
	bit 6,(ix+$13)          ;0ec5	dd cb 13 76
	scf                     ;0ec9	37
	ret z                   ;0eca	c8
	push hl                 ;0ecb	e5
	push de                 ;0ecc	d5
	push bc                 ;0ecd	c5
	call L04E1              ;0ece	cd e1 04
	ld sp,$0001             ;0ed1	31 01 00
	nop                     ;0ed4	00
	call L0832              ;0ed5	cd 32 08
	jr nc,L0EEF             ;0ed8	30 15
	ld de,$01e8             ;0eda	11 e8 01
	add hl,de               ;0edd	19
	ex de,hl                ;0ede	eb
	push ix                 ;0edf	dd e5
	pop hl                  ;0ee1	e1
	ld bc,$002b             ;0ee2	01 2b 00
	add hl,bc               ;0ee5	09
	ld bc,$0008             ;0ee6	01 08 00
	ldir                    ;0ee9	ed b0
	call L08E2              ;0eeb	cd e2 08
	scf                     ;0eee	37

L0EEF:
	pop bc                  ;0eef	c1
	pop de                  ;0ef0	d1
	pop hl                  ;0ef1	e1
	ret                     ;0ef2	c9
	ld ix,$0000             ;0ef3	dd 21 00 00
	ret                     ;0ef7	c9
Data0EF8:
	defb 0xdd,0x21,0xfc,0x00                     ;0ef8	dd 21 fc 00	.!..

L0EFC:
	ld hl,$17f7             ;0efc	21 f7 17
	jp L03F8                ;0eff	c3 f8 03
	ld c,(hl)               ;0f02	4e
	ld c,a                  ;0f03	4f
	jr nz,L0F54             ;0f04	20 4e
	ld b,c                  ;0f06	41
	ld c,l                  ;0f07	4d
	ld b,l                  ;0f08	45
	jr nz,L0F2B             ;0f09	20 20
	jr nz,L0F2D             ;0f0b	20 20

L0F0D:
	push hl                 ;0f0d	e5
	xor a                   ;0f0e	af
	ld b,$0b                ;0f0f	06 0b

L0F11:
	rrca                    ;0f11	0f
	add (hl)                ;0f12	86
	inc hl                  ;0f13	23
	djnz L0F11              ;0f14	10 fb
	pop hl                  ;0f16	e1
	ret                     ;0f17	c9

L0F18:
	ld bc,$001f             ;0f18	01 1f 00
	add hl,bc               ;0f1b	09
	ld b,$02                ;0f1c	06 02
	call L0F2D              ;0f1e	cd 2d 0f
	dec hl                  ;0f21	2b
	dec hl                  ;0f22	2b
	ld b,$06                ;0f23	06 06
	call L0F2D              ;0f25	cd 2d 0f
	dec hl                  ;0f28	2b
	dec hl                  ;0f29	2b
	dec hl                  ;0f2a	2b

L0F2B:
	ld b,$05                ;0f2b	06 05

L0F2D:
	ld a,(hl)               ;0f2d	7e
	dec hl                  ;0f2e	2b
	ld c,(hl)               ;0f2f	4e
	dec hl                  ;0f30	2b
	and a                   ;0f31	a7
	jr z,L0F3A              ;0f32	28 06
	and c                   ;0f34	a1
	inc a                   ;0f35	3c
	jr z,L0F4C              ;0f36	28 14
	jr L0F48                ;0f38	18 0e

L0F3A:
	ld a,c                  ;0f3a	79
	and a                   ;0f3b	a7
	jr z,L0F4C              ;0f3c	28 0e
	cp $20                  ;0f3e	fe 20
	jr nc,L0F44             ;0f40	30 02
	add $e0                 ;0f42	c6 e0

L0F44:
	cp $80                  ;0f44	fe 80
	jr c,L0F4A              ;0f46	38 02

L0F48:
	ld a,$5f                ;0f48	3e 5f

L0F4A:
	dec de                  ;0f4a	1b
	ld (de),a               ;0f4b	12

L0F4C:
	djnz L0F2D              ;0f4c	10 df
	ret                     ;0f4e	c9

L0F4F:
	dec c                   ;0f4f	0d
	ld a,c                  ;0f50	79
	jr nz,L0F5F             ;0f51	20 0c
	ld a,(de)               ;0f53	1a

L0F54:
	inc de                  ;0f54	13
	inc a                   ;0f55	3c
	jr nz,L0F5E             ;0f56	20 06
	ld (hl),a               ;0f58	77
	inc hl                  ;0f59	23
	ld (hl),a               ;0f5a	77
	inc hl                  ;0f5b	23
	jr L0F64                ;0f5c	18 06

L0F5E:
	dec a                   ;0f5e	3d

L0F5F:
	ld (hl),a               ;0f5f	77
	inc hl                  ;0f60	23
	ld (hl),c               ;0f61	71
	inc hl                  ;0f62	23
	inc c                   ;0f63	0c

L0F64:
	djnz L0F4F              ;0f64	10 e9
	ret                     ;0f66	c9

L0F67:
	push de                 ;0f67	d5
	ld bc,$0b00             ;0f68	01 00 0b
	dec hl                  ;0f6b	2b

L0F6C:
	inc hl                  ;0f6c	23
	ld a,(hl)               ;0f6d	7e
	cp $ff                  ;0f6e	fe ff
	jr z,L0FBF              ;0f70	28 4d
	cp $20                  ;0f72	fe 20
	jr z,L0F6C              ;0f74	28 f6
	cp $2e                  ;0f76	fe 2e
	jr z,L0F6C              ;0f78	28 f2
	ld b,$08                ;0f7a	06 08
	call L0FFB              ;0f7c	cd fb 0f
	jr z,L0FBC              ;0f7f	28 3b
	call L10B8              ;0f81	cd b8 10
	ld b,$03                ;0f84	06 03
	jr c,L0F97              ;0f86	38 0f
	call L1059              ;0f88	cd 59 10
	jr c,L0F9E              ;0f8b	38 11

L0F8D:
	push hl                 ;0f8d	e5
	call L1059              ;0f8e	cd 59 10
	jr c,L0F96              ;0f91	38 03
	pop af                  ;0f93	f1
	jr L0F8D                ;0f94	18 f7

L0F96:
	pop hl                  ;0f96	e1

L0F97:
	call L0FFB              ;0f97	cd fb 0f
	jr z,L0F9E              ;0f9a	28 02
	ld c,$01                ;0f9c	0e 01

L0F9E:
	call L10B8              ;0f9e	cd b8 10

L0FA1:
	ld a,$ff                ;0fa1	3e ff
	ld (de),a               ;0fa3	12
	pop hl                  ;0fa4	e1
	ld b,$07                ;0fa5	06 07

L0FA7:
	ld a,(hl)               ;0fa7	7e
	inc hl                  ;0fa8	23
	cp $20                  ;0fa9	fe 20
	jr z,L0FB0              ;0fab	28 03
	djnz L0FA7              ;0fad	10 f8
	inc b                   ;0faf	04

L0FB0:
	dec hl                  ;0fb0	2b
	dec c                   ;0fb1	0d
	ld c,$00                ;0fb2	0e 00
	ret nz                  ;0fb4	c0

L0FB5:
	ld (hl),$7e             ;0fb5	36 7e
	inc hl                  ;0fb7	23
	ld (hl),$31             ;0fb8	36 31
	inc c                   ;0fba	0c
	ret                     ;0fbb	c9

L0FBC:
	inc b                   ;0fbc	04
	inc b                   ;0fbd	04
	inc b                   ;0fbe	04

L0FBF:
	call L10B8              ;0fbf	cd b8 10
	jr L0FA1                ;0fc2	18 dd

L0FC4:
	ld a,c                  ;0fc4	79
	and a                   ;0fc5	a7
	jr z,L0FB5              ;0fc6	28 ed
	push hl                 ;0fc8	e5
	push bc                 ;0fc9	c5

L0FCA:
	inc (hl)                ;0fca	34
	ld a,(hl)               ;0fcb	7e
	cp $3a                  ;0fcc	fe 3a
	jr c,L0FF5              ;0fce	38 25
	ld (hl),$30             ;0fd0	36 30
	dec hl                  ;0fd2	2b
	dec c                   ;0fd3	0d
	jr nz,L0FCA             ;0fd4	20 f4
	pop bc                  ;0fd6	c1
	pop af                  ;0fd7	f1
	ld a,c                  ;0fd8	79
	cp b                    ;0fd9	b8
	jr z,L0FE8              ;0fda	28 0c

L0FDC:
	inc c                   ;0fdc	0c
	inc hl                  ;0fdd	23
	ld (hl),$31             ;0fde	36 31

L0FE0:
	inc hl                  ;0fe0	23
	ld (hl),$30             ;0fe1	36 30
	dec a                   ;0fe3	3d
	jr nz,L0FE0             ;0fe4	20 fa
	scf                     ;0fe6	37
	ret                     ;0fe7	c9

L0FE8:
	ld a,b                  ;0fe8	78
	cp $06                  ;0fe9	fe 06
	ld a,$1b                ;0feb	3e 1b
	ret z                   ;0fed	c8
	inc b                   ;0fee	04
	dec hl                  ;0fef	2b
	ld (hl),$7e             ;0ff0	36 7e
	ld a,c                  ;0ff2	79
	jr L0FDC                ;0ff3	18 e7

L0FF5:
	pop bc                  ;0ff5	c1
	pop hl                  ;0ff6	e1
	scf                     ;0ff7	37
	ret                     ;0ff8	c9
L0FF9:
	defb 0x0e,0x01,0x7e,0x23,0xfe,0xff,0xc8,0xfe ;0ff9	0e 01 7e 23 fe ff c8 fe	..~#....
	defb 0x2e,0x28                               ;1001	2e 28	.(
	ld c,e                  ;1003	4b
	cp $20                  ;1004	fe 20
	jr z,L0FF9              ;1006	28 f1
	jr c,L1032              ;1008	38 28
	cp $80                  ;100a	fe 80
	jr nc,L1032             ;100c	30 24
	cp $2b                  ;100e	fe 2b
	jr z,L1032              ;1010	28 20
	cp $2c                  ;1012	fe 2c
	jr z,L1032              ;1014	28 1c
	cp $3b                  ;1016	fe 3b
	jr z,L1032              ;1018	28 18
	cp $3d                  ;101a	fe 3d
	jr z,L1032              ;101c	28 14
	cp $5b                  ;101e	fe 5b
	jr z,L1032              ;1020	28 10
	cp $5d                  ;1022	fe 5d
	jr z,L1032              ;1024	28 0c
	cp $61                  ;1026	fe 61
	jr c,L1036              ;1028	38 0c
	cp $7b                  ;102a	fe 7b
	jr nc,L1036             ;102c	30 08
	and $df                 ;102e	e6 df
	jr L1036                ;1030	18 04

L1032:
	ld a,$5f                ;1032	3e 5f
	ld c,$01                ;1034	0e 01

L1036:
	ld (de),a               ;1036	12
	inc de                  ;1037	13
	djnz L0FFB              ;1038	10 c1
	ld a,(hl)               ;103a	7e
	inc hl                  ;103b	23
	cp $ff                  ;103c	fe ff
	ret z                   ;103e	c8
	cp $2e                  ;103f	fe 2e
	jr nz,L1049             ;1041	20 06
	push hl                 ;1043	e5
	call L1059              ;1044	cd 59 10
	pop hl                  ;1047	e1
	ret c                   ;1048	d8

L1049:
	dec hl                  ;1049	2b
	xor a                   ;104a	af
	inc a                   ;104b	3c
	ld c,$01                ;104c	0e 01
	ret                     ;104e	c9
	push hl                 ;104f	e5
	call L1059              ;1050	cd 59 10
	pop hl                  ;1053	e1
	ret c                   ;1054	d8
	ld c,$01                ;1055	0e 01
	jr L0FFB                ;1057	18 a2

L1059:
	ld a,(hl)               ;1059	7e
	inc hl                  ;105a	23
	cp $ff                  ;105b	fe ff
	jr z,L1064              ;105d	28 05
	cp $2e                  ;105f	fe 2e
	jr nz,L1059             ;1061	20 f6
	ret                     ;1063	c9

L1064:
	dec a                   ;1064	3d
	scf                     ;1065	37
	ret                     ;1066	c9

L1067:
	push de                 ;1067	d5
	bit 7,(iy+$0d)          ;1068	fd cb 0d 7e
	jr z,L107F              ;106c	28 11
	ld de,($2f26)           ;106e	ed 5b 26 2f
	ld hl,$302d             ;1072	21 2d 30
	and a                   ;1075	a7
	sbc hl,de               ;1076	ed 52
	ld b,h                  ;1078	44
	ld c,l                  ;1079	4d
	ex de,hl                ;107a	eb
	pop de                  ;107b	d1
	ldir                    ;107c	ed b0
	ret                     ;107e	c9

L107F:
	call T11AB              ;107f	cd ab 11
	pop de                  ;1082	d1

L1083:
	ld b,$08                ;1083	06 08
	call L109E              ;1085	cd 9e 10
	ld a,$2e                ;1088	3e 2e
	ld (de),a               ;108a	12
	inc de                  ;108b	13
	ld b,$03                ;108c	06 03
	call L109E              ;108e	cd 9e 10
	dec de                  ;1091	1b
	ld a,(de)               ;1092	1a
	cp $2e                  ;1093	fe 2e
	jr z,L1098              ;1095	28 01
	inc de                  ;1097	13

L1098:
	ld a,$ff                ;1098	3e ff
	ld (de),a               ;109a	12
	inc de                  ;109b	13
	scf                     ;109c	37
	ret                     ;109d	c9

L109E:
	ld c,$00                ;109e	0e 00

L10A0:
	ld a,(hl)               ;10a0	7e
	res 7,a                 ;10a1	cb bf
	inc hl                  ;10a3	23
	ld (de),a               ;10a4	12
	inc de                  ;10a5	13
	inc c                   ;10a6	0c
	cp $20                  ;10a7	fe 20
	jr z,L10AD              ;10a9	28 02
	ld c,$00                ;10ab	0e 00

L10AD:
	djnz L10A0              ;10ad	10 f1
	inc c                   ;10af	0c
	dec c                   ;10b0	0d
	ret z                   ;10b1	c8
	and a                   ;10b2	a7
	ex de,hl                ;10b3	eb
	sbc hl,bc               ;10b4	ed 42
	ex de,hl                ;10b6	eb
	ret                     ;10b7	c9

L10B8:
	ld a,$20                ;10b8	3e 20
	inc b                   ;10ba	04

L10BB:
	dec b                   ;10bb	05
	ret z                   ;10bc	c8
	ld (de),a               ;10bd	12
	inc de                  ;10be	13
	jr L10BB                ;10bf	18 fa

L10C1:
	ld iy,$2e50             ;10c1	fd 21 50 2e
	ld ix,($2e50)           ;10c5	dd 2a 50 2e
	ret                     ;10c9	c9

L10CA:
	ld a,ixl                ;10ca	dd 7d
	ld (iy+$00),a           ;10cc	fd 77 00
	ld a,ixh                ;10cf	dd 7c
	ld (iy+$01),a           ;10d1	fd 77 01
	bit 7,(ix+$13)          ;10d4	dd cb 13 7e
	jr nz,L10EC             ;10d8	20 12
	res 7,(iy+$02)          ;10da	fd cb 02 be
	push bc                 ;10de	c5
	push de                 ;10df	d5
	call L04E1              ;10e0	cd e1 04
	dec e                   ;10e3	1d
	call L0500              ;10e4	cd 00 05
	inc bc                  ;10e7	03
	pop de                  ;10e8	d1
	pop bc                  ;10e9	c1
	jr L10F7                ;10ea	18 0b

L10EC:
	set 7,(iy+$02)          ;10ec	fd cb 02 fe
	xor a                   ;10f0	af
	ld (iy+$03),a           ;10f1	fd 77 03
	ld (iy+$04),a           ;10f4	fd 77 04

L10F7:
	push bc                 ;10f7	c5
	push de                 ;10f8	d5
	rst $28                 ;10f9	ef
	inc bc                  ;10fa	03
	call L0500              ;10fb	cd 00 05
	ex af,af'               ;10fe	08
	pop de                  ;10ff	d1
	pop bc                  ;1100	c1
	xor a                   ;1101	af
	ld (iy+$07),a           ;1102	fd 77 07
	ld (iy+$0c),a           ;1105	fd 77 0c
	scf                     ;1108	37
	ret                     ;1109	c9

; tentative entry (word / jump table scan)
T110A:
	call T11AB              ;110a	cd ab 11
	ret nc                  ;110d	d0
	jr z,L113B              ;110e	28 2b
	bit 4,a                 ;1110	cb 67
	jr z,L113B              ;1112	28 27
	ld bc,$001a             ;1114	01 1a 00
	add hl,bc               ;1117	09
	ld c,(hl)               ;1118	4e
	inc hl                  ;1119	23
	ld b,(hl)               ;111a	46
	ld de,$0000             ;111b	11 00 00
	bit 7,(ix+$13)          ;111e	dd cb 13 7e
	jr nz,T112B             ;1122	20 07
	ld de,$fffa             ;1124	11 fa ff
	add hl,de               ;1127	19
	ld d,(hl)               ;1128	56
	dec hl                  ;1129	2b
	ld e,(hl)               ;112a	5e

; tentative entry (word / jump table scan)
T112B:
	ld a,d                  ;112b	7a
	or e                    ;112c	b3
	or b                    ;112d	b0
	or c                    ;112e	b1
	jr z,L10CA              ;112f	28 99
	call L0500              ;1131	cd 00 05
	inc bc                  ;1134	03
	res 7,(iy+$02)          ;1135	fd cb 02 be
	jr L10F7                ;1139	18 bc

L113B:
	ld a,$45                ;113b	3e 45
	and a                   ;113d	a7
	ret                     ;113e	c9

L113F:
	inc (iy+$0c)            ;113f	fd 34 0c
	bit 4,(iy+$0c)          ;1142	fd cb 0c 66
	scf                     ;1146	37
	ret z                   ;1147	c8
	bit 7,(iy+$02)          ;1148	fd cb 02 7e
	jr nz,L115A             ;114c	20 0c

L114E:
	ld bc,$0007             ;114e	01 07 00
	call L0577              ;1151	cd 77 05
	ret nc                  ;1154	d0
	ld (iy+$0c),$00         ;1155	fd 36 0c 00
	ret                     ;1159	c9

L115A:
	rst $28                 ;115a	ef
	ex af,af'               ;115b	08
	inc bc                  ;115c	03
	ld l,(ix+$1b)           ;115d	dd 6e 1b
	ld h,(ix+$1c)           ;1160	dd 66 1c
	dec hl                  ;1163	2b
	and a                   ;1164	a7
	sbc hl,bc               ;1165	ed 42
	ld a,$17                ;1167	3e 17
	ccf                     ;1169	3f
	ret nc                  ;116a	d0
	ld (iy+$08),c           ;116b	fd 71 08
	ld (iy+$09),b           ;116e	fd 70 09
	ld (iy+$0c),$00         ;1171	fd 36 0c 00
	ret                     ;1175	c9

L1176:
	bit 7,(iy+$02)          ;1176	fd cb 02 7e
	jr nz,L1193             ;117a	20 17
	rst $28                 ;117c	ef
	ex af,af'               ;117d	08
	ld a,(iy+$07)           ;117e	fd 7e 07
	call L06EE              ;1181	cd ee 06

L1184:
	ret nc                  ;1184	d0
	ld e,(iy+$0c)           ;1185	fd 5e 0c
	ld d,$00                ;1188	16 00
	ex de,hl                ;118a	eb
	add hl,hl               ;118b	29
	add hl,hl               ;118c	29
	add hl,hl               ;118d	29
	add hl,hl               ;118e	29
	add hl,hl               ;118f	29
	add hl,de               ;1190	19
	scf                     ;1191	37
	ret                     ;1192	c9

L1193:
	call L04E1              ;1193	cd e1 04
	dec e                   ;1196	1d
	ld h,b                  ;1197	60
	ld l,c                  ;1198	69
	push de                 ;1199	d5
	rst $28                 ;119a	ef
	ex af,af'               ;119b	08
	pop de                  ;119c	d1
	add hl,bc               ;119d	09
	ex de,hl                ;119e	eb
	ld bc,$0000             ;119f	01 00 00
	adc hl,bc               ;11a2	ed 4a
	ld c,l                  ;11a4	4d
	ld b,h                  ;11a5	44
	call L0832              ;11a6	cd 32 08
	jr L1184                ;11a9	18 d9

; tentative entry (word / jump table scan)
T11AB:
	call L1176              ;11ab	cd 76 11
	ret nc                  ;11ae	d0
	ld a,(hl)               ;11af	7e
	and a                   ;11b0	a7
	scf                     ;11b1	37
	ret z                   ;11b2	c8
	cp $e5                  ;11b3	fe e5
	scf                     ;11b5	37
	ret z                   ;11b6	c8
	push hl                 ;11b7	e5
	ld de,$000b             ;11b8	11 0b 00
	add hl,de               ;11bb	19
	ld b,a                  ;11bc	47
	ld a,(hl)               ;11bd	7e
	inc hl                  ;11be	23
	inc hl                  ;11bf	23
	ld c,(hl)               ;11c0	4e
	pop hl                  ;11c1	e1
	scf                     ;11c2	37
	ret                     ;11c3	c9

L11C4:
	call L10F7              ;11c4	cd f7 10
	ld d,$00                ;11c7	16 00
	push de                 ;11c9	d5

L11CA:
	call T11AB              ;11ca	cd ab 11
	pop de                  ;11cd	d1
	ret nc                  ;11ce	d0
	jr nz,L11E3             ;11cf	20 12
	dec d                   ;11d1	15
	inc d                   ;11d2	14
	jr nz,L11DC             ;11d3	20 07
	push de                 ;11d5	d5
	call L120B              ;11d6	cd 0b 12
	ldir                    ;11d9	ed b0
	pop de                  ;11db	d1

L11DC:
	inc d                   ;11dc	14
	ld a,d                  ;11dd	7a
	cp e                    ;11de	bb
	jr nc,L1203             ;11df	30 22
	jr L11E5                ;11e1	18 02

L11E3:
	ld d,$00                ;11e3	16 00

L11E5:
	push de                 ;11e5	d5
	call L113F              ;11e6	cd 3f 11
	jr c,L11CA              ;11e9	38 df
	bit 7,(iy+$02)          ;11eb	fd cb 02 7e
	jr nz,L1201             ;11ef	20 10
	rst $28                 ;11f1	ef
	ex af,af'               ;11f2	08
	call T063E              ;11f3	cd 3e 06
	call c,L0688            ;11f6	dc 88 06
	call c,L08D1            ;11f9	dc d1 08
	call c,L114E            ;11fc	dc 4e 11
	jr c,L11CA              ;11ff	38 c9

L1201:
	pop de                  ;1201	d1
	ret                     ;1202	c9

L1203:
	call L120B              ;1203	cd 0b 12
	ex de,hl                ;1206	eb
	ldir                    ;1207	ed b0
	jr T11AB                ;1209	18 a0

L120B:
	push iy                 ;120b	fd e5
	pop hl                  ;120d	e1
	ld bc,$0007             ;120e	01 07 00
	add hl,bc               ;1211	09
	ld de,$201a             ;1212	11 1a 20
	ld bc,$0006             ;1215	01 06 00
	ret                     ;1218	c9
	ld iy,$2e50             ;1219	fd 21 50 2e
	call L10CA              ;121d	cd ca 10
	ret nc                  ;1220	d0

L1221:
	call T11AB              ;1221	cd ab 11
	ret nc                  ;1224	d0
	jr z,L1235              ;1225	28 0e
	ld e,a                  ;1227	5f
	and $3f                 ;1228	e6 3f
	cp $0f                  ;122a	fe 0f
	jr z,L1235              ;122c	28 07
	ld a,e                  ;122e	7b
	and $18                 ;122f	e6 18
	cp $08                  ;1231	fe 08
	scf                     ;1233	37
	ret z                   ;1234	c8

L1235:
	call L113F              ;1235	cd 3f 11
	jr c,L1221              ;1238	38 e7
	xor a                   ;123a	af
	inc a                   ;123b	3c
	scf                     ;123c	37
	ret                     ;123d	c9

L123E:
	ld (iy+$0d),$00         ;123e	fd 36 0d 00
	jr L124C                ;1242	18 08

L1244:
	ld (iy+$0d),$00         ;1244	fd 36 0d 00

L1248:
	call L113F              ;1248	cd 3f 11
	ret nc                  ;124b	d0

L124C:
	call T11AB              ;124c	cd ab 11
	ret nc                  ;124f	d0
	jr nz,L1258             ;1250	20 06
	and a                   ;1252	a7
	jr nz,L1244             ;1253	20 ef
	ld a,$17                ;1255	3e 17
	ret                     ;1257	c9

L1258:
	ld e,a                  ;1258	5f
	and $3f                 ;1259	e6 3f
	cp $0f                  ;125b	fe 0f
	jr nz,L129D             ;125d	20 3e
	ld a,b                  ;125f	78
	bit 6,a                 ;1260	cb 77
	jr nz,L127F             ;1262	20 1b
	dec (iy+$0d)            ;1264	fd 35 0d
	cp (iy+$0d)             ;1267	fd be 0d
	jr nz,L1244             ;126a	20 d8
	ld a,c                  ;126c	79
	cp (iy+$0e)             ;126d	fd be 0e
	jr nz,L1244             ;1270	20 d2
	ld de,($2f26)           ;1272	ed 5b 26 2f

L1276:
	call L0F18              ;1276	cd 18 0f
	ld ($2f26),de           ;1279	ed 53 26 2f
	jr L1248                ;127d	18 c9

L127F:
	res 6,a                 ;127f	cb b7
	cp $15                  ;1281	fe 15
	jr nc,L1244             ;1283	30 bf
	ld (iy+$0d),a           ;1285	fd 77 0d
	ld (iy+$0e),c           ;1288	fd 71 0e
	push hl                 ;128b	e5
	call L120B              ;128c	cd 0b 12
	ld de,$2020             ;128f	11 20 20
	ldir                    ;1292	ed b0
	pop hl                  ;1294	e1
	ld de,$302c             ;1295	11 2c 30
	ld a,$ff                ;1298	3e ff
	ld (de),a               ;129a	12
	jr L1276                ;129b	18 d9

L129D:
	ld a,e                  ;129d	7b
	and $18                 ;129e	e6 18
	cp $08                  ;12a0	fe 08
	jr z,L1244              ;12a2	28 a0
	dec (iy+$0d)            ;12a4	fd 35 0d
	ld (iy+$0d),$00         ;12a7	fd 36 0d 00
	scf                     ;12ab	37
	ret nz                  ;12ac	c0
	call L0F0D              ;12ad	cd 0d 0f
	cp (iy+$0e)             ;12b0	fd be 0e
	scf                     ;12b3	37
	ret nz                  ;12b4	c0
	dec (iy+$0d)            ;12b5	fd 35 0d
	ret                     ;12b8	c9
	call L10C1              ;12b9	cd c1 10
	djnz L12CA              ;12bc	10 0c
	call L0A1A              ;12be	cd 1a 0a
	call L10F7              ;12c1	cd f7 10
	ld hl,$2e50             ;12c4	21 50 2e
	ld a,$0f                ;12c7	3e 0f
	ret                     ;12c9	c9

L12CA:
	djnz L12DC              ;12ca	10 10
	call L0A0A              ;12cc	cd 0a 0a
	push iy                 ;12cf	fd e5
	pop de                  ;12d1	d1
	ld hl,$2e50             ;12d2	21 50 2e
	ld bc,$000f             ;12d5	01 0f 00
	ldir                    ;12d8	ed b0
	scf                     ;12da	37
	ret                     ;12db	c9

L12DC:
	dec b                   ;12dc	05
	jp z,L10CA              ;12dd	ca ca 10
	djnz L12EF              ;12e0	10 0d
	ld hl,$2e50             ;12e2	21 50 2e
	ld de,$2e5f             ;12e5	11 5f 2e
	ld bc,$000f             ;12e8	01 0f 00
	ldir                    ;12eb	ed b0
	scf                     ;12ed	37
	ret                     ;12ee	c9

L12EF:
	dec b                   ;12ef	05
	jp z,T110A              ;12f0	ca 0a 11
	jp L0014                ;12f3	c3 14 00
	push hl                 ;12f6	e5
	call L1315              ;12f7	cd 15 13
	ex (sp),ix              ;12fa	dd e3
	ld bc,$0000             ;12fc	01 00 00
	ld de,$0000             ;12ff	11 00 00
	jr c,L1306              ;1302	38 02
	rst $28                 ;1304	ef
	inc bc                  ;1305	03

L1306:
	ld (ix+$3a),c           ;1306	dd 71 3a
	ld (ix+$3b),b           ;1309	dd 70 3b
	ld (ix+$34),e           ;130c	dd 73 34
	ld (ix+$35),d           ;130f	dd 72 35
	pop ix                  ;1312	dd e1
	ret                     ;1314	c9

L1315:
	bit 7,(ix+$13)          ;1315	dd cb 13 7e
	jr nz,T1336             ;1319	20 1b
	push hl                 ;131b	e5
	push de                 ;131c	d5
	push bc                 ;131d	c5
	call L04E1              ;131e	cd e1 04
	dec e                   ;1321	1d
	push de                 ;1322	d5
	push bc                 ;1323	c5
	rst $28                 ;1324	ef
	inc bc                  ;1325	03
	pop hl                  ;1326	e1
	and a                   ;1327	a7
	sbc hl,bc               ;1328	ed 42
	pop hl                  ;132a	e1
	jr nz,L132F             ;132b	20 02
	sbc hl,de               ;132d	ed 52

L132F:
	pop bc                  ;132f	c1
	pop de                  ;1330	d1
	pop hl                  ;1331	e1
	scf                     ;1332	37
	ret z                   ;1333	c8
	ccf                     ;1334	3f
	ret                     ;1335	c9

; tentative entry (word / jump table scan)
T1336:
	bit 7,(iy+$02)          ;1336	fd cb 02 7e
	scf                     ;133a	37
	ret nz                  ;133b	c0
	ccf                     ;133c	3f
	ret                     ;133d	c9
	add hl,$001a            ;133e	ed 34 1a 00
	ld a,(hl)               ;1342	7e
	inc hl                  ;1343	23
	cp c                    ;1344	b9
	ret nz                  ;1345	c0
	ld a,(hl)               ;1346	7e
	cp b                    ;1347	b8
	ret nz                  ;1348	c0
	add hl,$fffa            ;1349	ed 34 fa ff
	ld a,(hl)               ;134d	7e
	dec hl                  ;134e	2b
	cp d                    ;134f	ba
	ret nz                  ;1350	c0
	ld a,(hl)               ;1351	7e
	cp e                    ;1352	bb
	ret                     ;1353	c9

L1354:
	ld b,a                  ;1354	47
	push de                 ;1355	d5
	ld iy,$2713             ;1356	fd 21 13 27
	ld de,$002d             ;135a	11 2d 00
	inc b                   ;135d	04

L135E:
	add iy,de               ;135e	fd 19
	djnz L135E              ;1360	10 fc
	ld a,(iy+$00)           ;1362	fd 7e 00
	ld ixl,a                ;1365	dd 6f
	ld a,(iy+$01)           ;1367	fd 7e 01
	ld ixh,a                ;136a	dd 67
	pop de                  ;136c	d1
	ret                     ;136d	c9

; tentative entry (word / jump table scan)
T136E:
	ld b,$10                ;136e	06 10
	ld hl,$2740             ;1370	21 40 27
	push hl                 ;1373	e5
	push bc                 ;1374	c5
	ld d,iyh                ;1375	fd 54
	ld e,iyl                ;1377	fd 5d
	ld b,$0f                ;1379	06 0f

L137B:
	ld a,(de)               ;137b	1a
	cp (hl)                 ;137c	be
	inc de                  ;137d	13
	inc hl                  ;137e	23
	jr nz,L138A             ;137f	20 09
	djnz L137B              ;1381	10 f8
	ld a,(hl)               ;1383	7e
	and a                   ;1384	a7
	jr z,L138A              ;1385	28 03
	pop bc                  ;1387	c1
	pop hl                  ;1388	e1
	ret                     ;1389	c9
L138A:
	defb 0xc1,0xe1,0x11,0x2d,0x00,0x19,0x10,0xe1 ;138a	c1 e1 11 2d 00 19 10 e1	...-....
	defb 0xcd,0x76,0x11,0xd0,0xed,0x34,0x14,0x00 ;1392	cd 76 11 d0 ed 34 14 00	.v...4..
	defb 0x5e,0x23,0x56,0xed,0x34,0x05,0x00,0x4e ;139a	5e 23 56 ed 34 05 00 4e	^#V.4..N
	defb 0x23,0x46,0xaf,0xcd,0xc3,0x06,0xcd,0xd2 ;13a2	23 46 af cd c3 06 cd d2	#F......
	defb 0x1e,0x44,0x4d,0x3e,0x11,0x21,0xb4,0x2e ;13aa	1e 44 4d 3e 11 21 b4 2e	.DM>.!..
	defb 0xf5,0xe5,0xd5,0xc5,0x7e,0x23,0xee,0x80 ;13b2	f5 e5 d5 c5 7e 23 ee 80	....~#..
	defb 0xdd,0xbe,0x10,0x20,0x1c,0x4e,0x23,0x46 ;13ba	dd be 10 20 1c 4e 23 46	... .N#F
	defb 0x23,0x5e,0x23,0x56,0xe1,0xe5,0xed,0x52 ;13c2	23 5e 23 56 e1 e5 ed 52	#^#V...R
	defb 0x20                                    ;13ca	20	 
	rrca                    ;13cb	0f
	pop de                  ;13cc	d1
	pop hl                  ;13cd	e1
	push hl                 ;13ce	e5
	push de                 ;13cf	d5
	sbc hl,bc               ;13d0	ed 42
	jr nz,L13DB             ;13d2	20 07
	pop bc                  ;13d4	c1
	pop de                  ;13d5	d1
	pop hl                  ;13d6	e1
	pop bc                  ;13d7	c1
	ld a,$03                ;13d8	3e 03
	ret                     ;13da	c9

L13DB:
	pop bc                  ;13db	c1
	pop de                  ;13dc	d1
	pop hl                  ;13dd	e1
	pop af                  ;13de	f1
	add hl,$0006            ;13df	ed 34 06 00
	dec a                   ;13e3	3d
	jr nz,L13B2             ;13e4	20 cc
	scf                     ;13e6	37
	ret                     ;13e7	c9

L13E8:
	rst $28                 ;13e8	ef
	rla                     ;13e9	17
	push de                 ;13ea	d5
	push bc                 ;13eb	c5
	rst $28                 ;13ec	ef
	rra                     ;13ed	1f
	pop hl                  ;13ee	e1
	and a                   ;13ef	a7
	sbc hl,bc               ;13f0	ed 42
	pop hl                  ;13f2	e1
	sbc hl,de               ;13f3	ed 52
	ret                     ;13f5	c9

; tentative entry (word / jump table scan)
T13F6:
	call L13E8              ;13f6	cd e8 13
	ret c                   ;13f9	d8
	call L168D              ;13fa	cd 8d 16
	inc l                   ;13fd	2c
	jr nz,L1407             ;13fe	20 07
	inc h                   ;1400	24
	jr nz,L1407             ;1401	20 04
	inc e                   ;1403	1c
	jr nz,L1407             ;1404	20 01
	inc d                   ;1406	14

L1407:
	ld b,h                  ;1407	44
	ld c,l                  ;1408	4d
	call L0500              ;1409	cd 00 05
	rra                     ;140c	1f
	scf                     ;140d	37
	ret                     ;140e	c9
	call L1354              ;140f	cd 54 13
	bit 1,(iy+$0f)          ;1412	fd cb 0f 4e
	jr nz,L1407             ;1416	20 ef
	ld a,$1c                ;1418	3e 1c
	and a                   ;141a	a7
	ret                     ;141b	c9
L141C:
	defb 0xfd,0x34,0x17,0x20,0x0d,0xfd,0x34,0x18 ;141c	fd 34 17 20 0d fd 34 18	.4. ..4.
	defb 0x20,0x08,0xfd,0x34                     ;1424	20 08 fd 34	 ..4
	add hl,de               ;1428	19
	jr nz,L142E             ;1429	20 03
	inc (iy+$1a)            ;142b	fd 34 1a

L142E:
	bit 6,(iy+$0f)          ;142e	fd cb 0f 76
	scf                     ;1432	37
	ret z                   ;1433	c8
	inc (iy+$15)            ;1434	fd 34 15
	ret nz                  ;1437	c0
	inc (iy+$16)            ;1438	fd 34 16
	bit 1,(iy+$16)          ;143b	fd cb 16 4e
	ret z                   ;143f	c8
	ld (iy+$16),$00         ;1440	fd 36 16 00
	ld bc,$0010             ;1444	01 10 00
	call L0577              ;1447	cd 77 05
	ret c                   ;144a	d8
	res 6,(iy+$0f)          ;144b	fd cb 0f b6
	cp $19                  ;144f	fe 19
	scf                     ;1451	37
	ret z                   ;1452	c8
	ccf                     ;1453	3f
	ret                     ;1454	c9

; tentative entry (word / jump table scan)
T1455:
	ld l,(iy+$17)           ;1455	fd 6e 17
	ld h,(iy+$18)           ;1458	fd 66 18
	add hl,bc               ;145b	09
	ld (iy+$17),l           ;145c	fd 75 17
	ld (iy+$18),h           ;145f	fd 74 18
	jr nc,L146C             ;1462	30 08
	inc (iy+$19)            ;1464	fd 34 19
	jr nz,L146C             ;1467	20 03
	inc (iy+$1a)            ;1469	fd 34 1a

L146C:
	bit 6,(iy+$0f)          ;146c	fd cb 0f 76
	scf                     ;1470	37
	ret z                   ;1471	c8
	bit 7,b                 ;1472	cb 78
	jr nz,L14A6             ;1474	20 30
	ld l,(iy+$15)           ;1476	fd 6e 15
	ld h,(iy+$16)           ;1479	fd 66 16
	add hl,bc               ;147c	09
	ld (iy+$15),l           ;147d	fd 75 15
	ld (iy+$16),h           ;1480	fd 74 16

L1483:
	ld bc,$0200             ;1483	01 00 02
	and a                   ;1486	a7
	sbc hl,bc               ;1487	ed 42
	ret c                   ;1489	d8
	ld (iy+$15),l           ;148a	fd 75 15
	ld (iy+$16),h           ;148d	fd 74 16
	ld bc,$0010             ;1490	01 10 00
	call L0577              ;1493	cd 77 05
	jr nc,L14A0             ;1496	30 08
	ld l,(iy+$15)           ;1498	fd 6e 15
	ld h,(iy+$16)           ;149b	fd 66 16
	jr L1483                ;149e	18 e3

L14A0:
	cp $19                  ;14a0	fe 19
	scf                     ;14a2	37
	jr z,L14A6              ;14a3	28 01
	ccf                     ;14a5	3f

L14A6:
	res 6,(iy+$0f)          ;14a6	fd cb 0f b6
	ret                     ;14aa	c9

L14AB:
	bit 6,(iy+$0f)          ;14ab	fd cb 0f 76
	jr nz,L151C             ;14af	20 6b
	rst $28                 ;14b1	ef
	dec de                  ;14b2	1b
	call T05A1              ;14b3	cd a1 05
	jr c,L14C0              ;14b6	38 08
	call L05BF              ;14b8	cd bf 05
	ret nc                  ;14bb	d0
	call L0500              ;14bc	cd 00 05
	dec de                  ;14bf	1b

L14C0:
	call L0500              ;14c0	cd 00 05
	ld de,$8dcd             ;14c3	11 cd 8d
	ld d,$dd                ;14c6	16 dd
	ld b,(hl)               ;14c8	46
	ld hl,$000e             ;14c9	21 0e 00

L14CC:
	and a                   ;14cc	a7
	sbc hl,bc               ;14cd	ed 42
	jr nc,L14D6             ;14cf	30 05
	ld a,d                  ;14d1	7a
	or e                    ;14d2	b3
	jr z,L1503              ;14d3	28 2e
	dec de                  ;14d5	1b

L14D6:
	sbc hl,bc               ;14d6	ed 42
	jr nc,L14DF             ;14d8	30 05
	ld a,d                  ;14da	7a
	or e                    ;14db	b3
	jr z,L1502              ;14dc	28 24
	dec de                  ;14de	1b

L14DF:
	push hl                 ;14df	e5
	push de                 ;14e0	d5
	push bc                 ;14e1	c5
	rst $28                 ;14e2	ef
	ld de,$12cd             ;14e3	11 cd 12
	add hl,bc               ;14e6	09
	jr nc,L14FE             ;14e7	30 15
	call T05A1              ;14e9	cd a1 05
	jr c,L14F5              ;14ec	38 07
	rst $28                 ;14ee	ef

L14EF:
	ld de,$3ecd             ;14ef	11 cd 3e
	ld b,$30                ;14f2	06 30
	add hl,bc               ;14f4	09

L14F5:
	call L0500              ;14f5	cd 00 05
	ld de,$d1c1             ;14f8	11 c1 d1
	pop hl                  ;14fb	e1
	jr L14CC                ;14fc	18 ce

L14FE:
	pop bc                  ;14fe	c1
	pop de                  ;14ff	d1
	pop hl                  ;1500	e1
	ret                     ;1501	c9

L1502:
	add hl,bc               ;1502	09

L1503:
	add hl,bc               ;1503	09
	ld bc,$0200             ;1504	01 00 02
	xor a                   ;1507	af

L1508:
	sbc hl,bc               ;1508	ed 42
	inc a                   ;150a	3c
	jr nc,L1508             ;150b	30 fb
	add hl,bc               ;150d	09
	dec a                   ;150e	3d
	ld (iy+$10),a           ;150f	fd 77 10
	ld (iy+$15),l           ;1512	fd 75 15
	ld (iy+$16),h           ;1515	fd 74 16
	set 6,(iy+$0f)          ;1518	fd cb 0f f6

L151C:
	rst $28                 ;151c	ef
	ld de,$7efd             ;151d	11 fd 7e
	djnz L14EF              ;1520	10 cd
	jp $fd06                ;1522	c3 06 fd
	ld l,(hl)               ;1525	6e
	dec d                   ;1526	15
	ld h,(iy+$16)           ;1527	fd 66 16
	ret                     ;152a	c9

L152B:
	call L14AB              ;152b	cd ab 14
	ret nc                  ;152e	d0

L152F:
	push hl                 ;152f	e5
	call L0832              ;1530	cd 32 08
	pop de                  ;1533	d1
	ret nc                  ;1534	d0
	add hl,de               ;1535	19
	scf                     ;1536	37
	ret                     ;1537	c9
Data1538:
	defb 0xcd,0x54,0x13,0xfd,0xcb,0x0f           ;1538	cd 54 13 fd cb 0f	.T....
	ld b,(hl)               ;153e	46
	jr z,L1556              ;153f	28 15
	call L13E8              ;1541	cd e8 13
	jr nc,L155A             ;1544	30 14
	call L152B              ;1546	cd 2b 15
	jr nc,L155C             ;1549	30 11
	ld c,(hl)               ;154b	4e
	push bc                 ;154c	c5
	call L141C              ;154d	cd 1c 14
	pop bc                  ;1550	c1
	ld a,c                  ;1551	79
	cp $1a                  ;1552	fe 1a
	scf                     ;1554	37
	ret                     ;1555	c9

L1556:
	ld a,$1d                ;1556	3e 1d
	jr L155C                ;1558	18 02

L155A:
	ld a,$19                ;155a	3e 19

L155C:
	and a                   ;155c	a7
	ret                     ;155d	c9
	call L1354              ;155e	cd 54 13
	bit 1,(iy+$0f)          ;1561	fd cb 0f 4e
	jr z,L1556              ;1565	28 ef
	push bc                 ;1567	c5
	call T13F6              ;1568	cd f6 13
	call L152B              ;156b	cd 2b 15
	pop bc                  ;156e	c1
	jr nc,L155C             ;156f	30 eb
	ld (hl),c               ;1571	71
	call L08E2              ;1572	cd e2 08
	call L141C              ;1575	cd 1c 14
	scf                     ;1578	37
	ret                     ;1579	c9
	call L1354              ;157a	cd 54 13
	ld ($2009),hl           ;157d	22 09 20
	ld ($200b),de           ;1580	ed 53 0b 20
	push bc                 ;1584	c5
	bit 7,c                 ;1585	cb 79
	call z,MapBankCToMmu67  ;1587	cc 87 00
	bit 0,(iy+$0f)          ;158a	fd cb 0f 46
	jp z,L1628              ;158e	ca 28 16
	call L13E8              ;1591	cd e8 13
	jp nc,L1624             ;1594	d2 24 16
	rst $28                 ;1597	ef
	rra                     ;1598	1f
	push de                 ;1599	d5
	ld h,b                  ;159a	60
	ld l,c                  ;159b	69
	rst $28                 ;159c	ef
	rla                     ;159d	17
	and a                   ;159e	a7
	sbc hl,bc               ;159f	ed 42
	ex de,hl                ;15a1	eb
	ld b,h                  ;15a2	44
	ld c,l                  ;15a3	4d
	pop hl                  ;15a4	e1
	sbc hl,bc               ;15a5	ed 42
	ld hl,($200b)           ;15a7	2a 0b 20
	jr nz,L15B3             ;15aa	20 07
	and a                   ;15ac	a7
	sbc hl,de               ;15ad	ed 52
	ex de,hl                ;15af	eb
	jr nc,L15B3             ;15b0	30 01
	add hl,de               ;15b2	19

L15B3:
	ld ($200d),hl           ;15b3	22 0d 20
	call L14AB              ;15b6	cd ab 14
	jr nc,L162A             ;15b9	30 6f
	bit 1,(iy+$0f)          ;15bb	fd cb 0f 4e
	jr nz,L15DC             ;15bf	20 1b
	ld a,h                  ;15c1	7c
	or l                    ;15c2	b5
	jr nz,L15DC             ;15c3	20 17
	ld a,($200e)            ;15c5	3a 0e 20
	cp $02                  ;15c8	fe 02
	jr c,L15DC              ;15ca	38 10
	ld hl,($2009)           ;15cc	2a 09 20
	call L1EAF              ;15cf	cd af 1e
	ld ($2009),hl           ;15d2	22 09 20
	jr nc,L162A             ;15d5	30 53
	ld bc,$0200             ;15d7	01 00 02
	jr L1600                ;15da	18 24

L15DC:
	call L152F              ;15dc	cd 2f 15
	jr nc,L162A             ;15df	30 49
	push hl                 ;15e1	e5
	ld hl,$0200             ;15e2	21 00 02
	and a                   ;15e5	a7
	sbc hl,de               ;15e6	ed 52
	pop de                  ;15e8	d1
	ld bc,($200d)           ;15e9	ed 4b 0d 20
	sbc hl,bc               ;15ed	ed 42
	jr nc,L15F4             ;15ef	30 03
	add hl,bc               ;15f1	09
	ld b,h                  ;15f2	44
	ld c,l                  ;15f3	4d

L15F4:
	push bc                 ;15f4	c5
	ld hl,($2009)           ;15f5	2a 09 20
	ex de,hl                ;15f8	eb
	ldir                    ;15f9	ed b0
	ld ($2009),de           ;15fb	ed 53 09 20
	pop bc                  ;15ff	c1

L1600:
	ld hl,($200b)           ;1600	2a 0b 20
	and a                   ;1603	a7
	sbc hl,bc               ;1604	ed 42
	ld ($200b),hl           ;1606	22 0b 20
	push af                 ;1609	f5
	push bc                 ;160a	c5
	call T1455              ;160b	cd 55 14
	pop bc                  ;160e	c1
	pop af                  ;160f	f1
	jr z,L161C              ;1610	28 0a
	ld hl,($200d)           ;1612	2a 0d 20
	and a                   ;1615	a7
	sbc hl,bc               ;1616	ed 42
	jr z,L1624              ;1618	28 0a
	jr L15B3                ;161a	18 97

L161C:
	pop bc                  ;161c	c1
	bit 7,c                 ;161d	cb 79
	call z,MapBank7ToMmu67  ;161f	cc 82 00
	scf                     ;1622	37
	ret                     ;1623	c9

L1624:
	ld a,$19                ;1624	3e 19
	jr L162A                ;1626	18 02

L1628:
	ld a,$1d                ;1628	3e 1d

L162A:
	pop bc                  ;162a	c1
	bit 7,c                 ;162b	cb 79
	call z,MapBank7ToMmu67  ;162d	cc 82 00
	and a                   ;1630	a7
	ld de,($200b)           ;1631	ed 5b 0b 20
	ret                     ;1635	c9
	call L1354              ;1636	cd 54 13
	ld ($2009),hl           ;1639	22 09 20
	ld ($200b),de           ;163c	ed 53 0b 20
	push bc                 ;1640	c5
	bit 7,c                 ;1641	cb 79
	call z,MapBankCToMmu67  ;1643	cc 87 00
	bit 1,(iy+$0f)          ;1646	fd cb 0f 4e
	jr z,L1628              ;164a	28 dc

L164C:
	call L152B              ;164c	cd 2b 15
	jr nc,L162A             ;164f	30 d9
	push hl                 ;1651	e5
	ld hl,$0200             ;1652	21 00 02
	and a                   ;1655	a7
	sbc hl,de               ;1656	ed 52
	pop de                  ;1658	d1
	ld bc,($200b)           ;1659	ed 4b 0b 20
	sbc hl,bc               ;165d	ed 42
	jr nc,L1664             ;165f	30 03
	add hl,bc               ;1661	09
	ld b,h                  ;1662	44
	ld c,l                  ;1663	4d

L1664:
	push bc                 ;1664	c5
	ld hl,($2009)           ;1665	2a 09 20
	ldir                    ;1668	ed b0
	ld ($2009),hl           ;166a	22 09 20
	call L08E2              ;166d	cd e2 08
	pop bc                  ;1670	c1
	ld hl,($200b)           ;1671	2a 0b 20
	and a                   ;1674	a7
	sbc hl,bc               ;1675	ed 42
	ld ($200b),hl           ;1677	22 0b 20
	push af                 ;167a	f5
	dec bc                  ;167b	0b
	call T1455              ;167c	cd 55 14
	call T13F6              ;167f	cd f6 13
	call L141C              ;1682	cd 1c 14
	pop af                  ;1685	f1
	jr z,L161C              ;1686	28 94
	jr L164C                ;1688	18 c2
Data168A:
	defb 0xcd,0x54,0x13                          ;168a	cd 54 13	.T.

L168D:
	rst $28                 ;168d	ef
	rla                     ;168e	17
	ld h,b                  ;168f	60
	ld l,c                  ;1690	69
	scf                     ;1691	37
	ret                     ;1692	c9
	call L1354              ;1693	cd 54 13

L1696:
	ld b,h                  ;1696	44
	ld c,l                  ;1697	4d
	call L0500              ;1698	cd 00 05
	rla                     ;169b	17
	res 6,(iy+$0f)          ;169c	fd cb 0f b6
	scf                     ;16a0	37
	ret                     ;16a1	c9
	call L1354              ;16a2	cd 54 13
	call T11AB              ;16a5	cd ab 11
	ret nc                  ;16a8	d0
	add hl,$0016            ;16a9	ed 34 16 00
	ld e,(hl)               ;16ad	5e
	inc hl                  ;16ae	23
	ld d,(hl)               ;16af	56
	inc hl                  ;16b0	23
	ld c,(hl)               ;16b1	4e
	inc hl                  ;16b2	23
	ld b,(hl)               ;16b3	46
	inc hl                  ;16b4	23
	push de                 ;16b5	d5
	pop ix                  ;16b6	dd e1
	push bc                 ;16b8	c5
	call L16BF              ;16b9	cd bf 16
	pop bc                  ;16bc	c1
	scf                     ;16bd	37
	ret                     ;16be	c9

L16BF:
	rst $28                 ;16bf	ef
	rra                     ;16c0	1f
	ld h,b                  ;16c1	60
	ld l,c                  ;16c2	69
	ret                     ;16c3	c9
	call L16CC              ;16c4	cd cc 16

L16C7:
	ld (iy+$0f),$00         ;16c7	fd 36 0f 00
	ret                     ;16cb	c9

L16CC:
	call L1354              ;16cc	cd 54 13

L16CF:
	scf                     ;16cf	37
	bit 1,(iy+$0f)          ;16d0	fd cb 0f 4e
	ret z                   ;16d4	c8
	bit 7,(iy+$0f)          ;16d5	fd cb 0f 7e
	call nz,T18FE           ;16d9	c4 fe 18
	ret nc                  ;16dc	d0
	call L08D1              ;16dd	cd d1 08
	ret nc                  ;16e0	d0
	call L1176              ;16e1	cd 76 11
	ret nc                  ;16e4	d0
	ld bc,$000b             ;16e5	01 0b 00
	add hl,bc               ;16e8	09
	set 5,(hl)              ;16e9	cb ee
	ld bc,$0009             ;16eb	01 09 00
	add hl,bc               ;16ee	09
	rst $28                 ;16ef	ef
	dec de                  ;16f0	1b
	ld (hl),e               ;16f1	73
	inc hl                  ;16f2	23
	ld (hl),d               ;16f3	72
	inc hl                  ;16f4	23
	push bc                 ;16f5	c5
	call L18F0              ;16f6	cd f0 18
	pop bc                  ;16f9	c1
	ld (hl),c               ;16fa	71
	inc hl                  ;16fb	23
	ld (hl),b               ;16fc	70
	inc hl                  ;16fd	23
	push hl                 ;16fe	e5
	call L16BF              ;16ff	cd bf 16
	pop hl                  ;1702	e1
	ld (hl),c               ;1703	71
	inc hl                  ;1704	23
	ld (hl),b               ;1705	70
	inc hl                  ;1706	23
	ld (hl),e               ;1707	73
	inc hl                  ;1708	23
	ld (hl),d               ;1709	72
	call L0853              ;170a	cd 53 08
	scf                     ;170d	37
	ret                     ;170e	c9
	push af                 ;170f	f5
	call L08D1              ;1710	cd d1 08
	pop af                  ;1713	f1
	call L1354              ;1714	cd 54 13
	scf                     ;1717	37
	jr L16C7                ;1718	18 ad
	call L1354              ;171a	cd 54 13
	ld b,(iy+$0f)           ;171d	fd 46 0f
	push bc                 ;1720	c5
	call L16CF              ;1721	cd cf 16
	pop bc                  ;1724	c1
	ret nc                  ;1725	d0
	ld (iy+$0f),$00         ;1726	fd 36 0f 00

L172A:
	push bc                 ;172a	c5
	ld b,$10                ;172b	06 10
	call T136E              ;172d	cd 6e 13
	pop bc                  ;1730	c1
	ld (iy+$0f),b           ;1731	fd 70 0f
	jr c,L1748              ;1734	38 12
	bit 3,c                 ;1736	cb 59
	jr nz,L1748             ;1738	20 0e
	bit 2,c                 ;173a	cb 51
	jr z,L1766              ;173c	28 28
	bit 1,c                 ;173e	cb 49
	jr nz,L1766             ;1740	20 24
	and $06                 ;1742	e6 06
	cp $02                  ;1744	fe 02
	jr z,L1766              ;1746	28 1e

L1748:
	ld a,c                  ;1748	79
	cp $10                  ;1749	fe 10
	jr nc,L1768             ;174b	30 1b
	and $02                 ;174d	e6 02
	jr z,L175D              ;174f	28 0c
	push bc                 ;1751	c5
	call T11AB              ;1752	cd ab 11
	pop bc                  ;1755	c1
	ret nc                  ;1756	d0
	bit 0,a                 ;1757	cb 47
	ld a,$1c                ;1759	3e 1c
	jr nz,L176A             ;175b	20 0d

L175D:
	ld a,b                  ;175d	78
	and $f0                 ;175e	e6 f0
	or c                    ;1760	b1
	ld (iy+$0f),a           ;1761	fd 77 0f
	scf                     ;1764	37
	ret                     ;1765	c9

L1766:
	ld a,$24                ;1766	3e 24

L1768:
	ld a,$1e                ;1768	3e 1e

L176A:
	and a                   ;176a	a7
	ret                     ;176b	c9
Data176C:
	defb 0xdd,0x22,0x18,0x20,0xcd,0x55,0x13,0xd5 ;176c	dd 22 18 20 cd 55 13 d5	.". .U..
	defb 0xc5,0xfd,0xe5,0xcd,0xc1,0x10,0xcb,0x4a ;1774	c5 fd e5 cd c1 10 cb 4a	.......J
	defb 0x28,0x14,0xd1,0xcd,0xab,0x17           ;177c	28 14 d1 cd ab 17	(.....

L1782:
	pop bc                  ;1782	c1
	jr nc,L178A             ;1783	30 05
	ld b,$00                ;1785	06 00
	call L172A              ;1787	cd 2a 17

L178A:
	pop de                  ;178a	d1
	ret nc                  ;178b	d0
	bit 0,d                 ;178c	cb 42
	call z,T1956            ;178e	cc 56 19
	ret                     ;1791	c9
	call T1815              ;1792	cd 15 18
	pop de                  ;1795	d1
	jr nc,L1782             ;1796	30 ea
	call L17AB              ;1798	cd ab 17
	pop bc                  ;179b	c1
	jr nc,L178A             ;179c	30 ec
	ld b,$00                ;179e	06 00
	call L172A              ;17a0	cd 2a 17
	pop de                  ;17a3	d1
	ret nc                  ;17a4	d0
	bit 0,d                 ;17a5	cb 42
	call z,T18FE            ;17a7	cc fe 18
	ret                     ;17aa	c9

L17AB:
	push iy                 ;17ab	fd e5
	pop hl                  ;17ad	e1
	push de                 ;17ae	d5
	pop iy                  ;17af	fd e1
	ld bc,$000f             ;17b1	01 0f 00
	ldir                    ;17b4	ed b0
	ld h,d                  ;17b6	62
	ld l,e                  ;17b7	6b
	inc de                  ;17b8	13
	ld (hl),$00             ;17b9	36 00
	ld bc,$001d             ;17bb	01 1d 00
	ldir                    ;17be	ed b0
	call T11AB              ;17c0	cd ab 11
	ret nc                  ;17c3	d0
	ld bc,$001a             ;17c4	01 1a 00
	add hl,bc               ;17c7	09
	push iy                 ;17c8	fd e5
	ld bc,$001b             ;17ca	01 1b 00
	add iy,bc               ;17cd	fd 09
	push iy                 ;17cf	fd e5
	pop de                  ;17d1	d1
	pop iy                  ;17d2	fd e1
	ld bc,$0002             ;17d4	01 02 00
	ldir                    ;17d7	ed b0
	inc de                  ;17d9	13
	inc de                  ;17da	13
	ld bc,$0004             ;17db	01 04 00
	ldir                    ;17de	ed b0
	bit 7,(ix+$13)          ;17e0	dd cb 13 7e
	ld de,$0000             ;17e4	11 00 00
	jr nz,L17F0             ;17e7	20 07
	ld bc,$fff4             ;17e9	01 f4 ff
	add hl,bc               ;17ec	09
	ld e,(hl)               ;17ed	5e
	inc hl                  ;17ee	23
	ld d,(hl)               ;17ef	56

L17F0:
	ld (iy+$1d),e           ;17f0	fd 73 1d
	ld (iy+$1e),d           ;17f3	fd 72 1e
	rst $28                 ;17f6	ef
	dec de                  ;17f7	1b
	call L0500              ;17f8	cd 00 05
	ld de,$182a             ;17fb	11 2a 18
	jr nz,L17FD             ;17fe	20 fd
	ld (hl),l               ;1800	75
	dec hl                  ;1801	2b
	ld (iy+$2c),h           ;1802	fd 74 2c

L1805:
	call L19C2              ;1805	cd c2 19
	rst $28                 ;1808	ef
	add hl,hl               ;1809	29
	ld a,d                  ;180a	7a
	or e                    ;180b	b3
	scf                     ;180c	37
	ret z                   ;180d	c8
	ld bc,$0008             ;180e	01 08 00
	ldir                    ;1811	ed b0
	scf                     ;1813	37
	ret                     ;1814	c9

; tentative entry (word / jump table scan)
T1815:
	push hl                 ;1815	e5
	ld a,(hl)               ;1816	7e
	inc hl                  ;1817	23
	cp $2e                  ;1818	fe 2e
	jr nz,T182A             ;181a	20 0e
	ld a,(hl)               ;181c	7e
	cp $2e                  ;181d	fe 2e
	jr nz,L1823             ;181f	20 02
	inc hl                  ;1821	23
	ld a,(hl)               ;1822	7e

L1823:
	cp $ff                  ;1823	fe ff
	ld a,$14                ;1825	3e 14
	jp z,L178A              ;1827	ca 8a 17

; tentative entry (word / jump table scan)
T182A:
	pop hl                  ;182a	e1
	push hl                 ;182b	e5
	ld de,$202e             ;182c	11 2e 20
	call L0F67              ;182f	cd 67 0f

L1832:
	push hl                 ;1832	e5
	push bc                 ;1833	c5
	push ix                 ;1834	dd e5
	call L10F7              ;1836	cd f7 10

L1839:
	call T11AB              ;1839	cd ab 11
	jr z,L1852              ;183c	28 14
	and $08                 ;183e	e6 08
	jr nz,L1852             ;1840	20 10
	ld b,$0b                ;1842	06 0b
	ld de,$202e             ;1844	11 2e 20

L1847:
	ld a,(de)               ;1847	1a
	inc de                  ;1848	13
	xor (hl)                ;1849	ae
	inc hl                  ;184a	23
	jr nz,L1852             ;184b	20 05
	djnz L1847              ;184d	10 f8
	scf                     ;184f	37
	jr L1857                ;1850	18 05

L1852:
	call L113F              ;1852	cd 3f 11
	jr c,L1839              ;1855	38 e2

L1857:
	pop ix                  ;1857	dd e1
	pop bc                  ;1859	c1
	pop hl                  ;185a	e1
	jr nc,L1864             ;185b	30 07
	call L0FC4              ;185d	cd c4 0f
	jr c,L1832              ;1860	38 d0
	pop hl                  ;1862	e1
	ret                     ;1863	c9

L1864:
	ld hl,$202e             ;1864	21 2e 20
	call L0F0D              ;1867	cd 0d 0f
	ld d,a                  ;186a	57
	pop hl                  ;186b	e1
	ld e,$02                ;186c	1e 02

L186E:
	ld bc,$000d             ;186e	01 0d 00
	ld a,$ff                ;1871	3e ff
	cpir                    ;1873	ed b1
	jr z,L187C              ;1875	28 05
	inc e                   ;1877	1c
	cp (hl)                 ;1878	be
	jr nz,L186E             ;1879	20 f3
	dec e                   ;187b	1d

L187C:
	ld a,e                  ;187c	7b
	cp $16                  ;187d	fe 16
	ld a,$14                ;187f	3e 14
	ret nc                  ;1881	d0
	add hl,bc               ;1882	09
	push hl                 ;1883	e5
	push de                 ;1884	d5
	call L11C4              ;1885	cd c4 11
	pop bc                  ;1888	c1
	pop hl                  ;1889	e1
	ret nc                  ;188a	d0
	dec c                   ;188b	0d
	set 6,c                 ;188c	cb f1

L188E:
	ld de,$000d             ;188e	11 0d 00
	and a                   ;1891	a7
	sbc hl,de               ;1892	ed 52
	push bc                 ;1894	c5
	push hl                 ;1895	e5
	call L1176              ;1896	cd 76 11
	pop de                  ;1899	d1
	pop bc                  ;189a	c1
	ret nc                  ;189b	d0
	push de                 ;189c	d5
	ld (hl),c               ;189d	71
	inc hl                  ;189e	23
	res 6,c                 ;189f	cb b1
	push bc                 ;18a1	c5
	ld bc,$0501             ;18a2	01 01 05
	call L0F4F              ;18a5	cd 4f 0f
	ld (hl),$0f             ;18a8	36 0f
	inc hl                  ;18aa	23
	ld (hl),$00             ;18ab	36 00
	inc hl                  ;18ad	23
	pop af                  ;18ae	f1
	push af                 ;18af	f5
	ld (hl),a               ;18b0	77
	inc hl                  ;18b1	23
	ld b,$06                ;18b2	06 06
	call L0F4F              ;18b4	cd 4f 0f
	xor a                   ;18b7	af
	ld (hl),a               ;18b8	77
	inc hl                  ;18b9	23
	ld (hl),a               ;18ba	77
	inc hl                  ;18bb	23
	ld b,$02                ;18bc	06 02
	call L0F4F              ;18be	cd 4f 0f
	call L08E2              ;18c1	cd e2 08
	call L113F              ;18c4	cd 3f 11
	pop bc                  ;18c7	c1
	pop hl                  ;18c8	e1
	ret nc                  ;18c9	d0
	dec c                   ;18ca	0d
	jr nz,L188E             ;18cb	20 c1
	call L1176              ;18cd	cd 76 11
	ret nc                  ;18d0	d0
	ex de,hl                ;18d1	eb
	ld hl,$202e             ;18d2	21 2e 20
	ld bc,$000b             ;18d5	01 0b 00
	ldir                    ;18d8	ed b0
	ld h,d                  ;18da	62
	ld l,e                  ;18db	6b
	ld (hl),$20             ;18dc	36 20
	inc hl                  ;18de	23
	inc de                  ;18df	13
	ld (hl),b               ;18e0	70
	ld c,$13                ;18e1	0e 13
	inc de                  ;18e3	13
	ldir                    ;18e4	ed b0
	add hl,$fff7            ;18e6	ed 34 f7 ff
	call L18F0              ;18ea	cd f0 18
	jp L08CE                ;18ed	c3 ce 08

L18F0:
	push hl                 ;18f0	e5
	call L00F2              ;18f1	cd f2 00
	pop hl                  ;18f4	e1
	ld (hl),e               ;18f5	73
	inc hl                  ;18f6	23
	ld (hl),d               ;18f7	72
	inc hl                  ;18f8	23
	ld (hl),c               ;18f9	71
	inc hl                  ;18fa	23
	ld (hl),b               ;18fb	70
	inc hl                  ;18fc	23
	ret                     ;18fd	c9

; tentative entry (word / jump table scan)
T18FE:
	ld d,$00                ;18fe	16 00
	ld e,d                  ;1900	5a
	ld h,d                  ;1901	62
	ld l,$7f                ;1902	2e 7f
	call L1696              ;1904	cd 96 16
	call T13F6              ;1907	cd f6 13
	call L19D6              ;190a	cd d6 19
	ret nc                  ;190d	d0
	set 7,(iy+$0f)          ;190e	fd cb 0f fe
	push hl                 ;1912	e5
	ex de,hl                ;1913	eb
	ld hl,$19e9             ;1914	21 e9 19
	ld bc,$000b             ;1917	01 0b 00
	ldir                    ;191a	ed b0
	push de                 ;191c	d5
	call L19C2              ;191d	cd c2 19
	rst $28                 ;1920	ef
	dec hl                  ;1921	2b
	ex de,hl                ;1922	eb
	ld h,b                  ;1923	60
	ld l,c                  ;1924	69
	ld a,h                  ;1925	7c
	or l                    ;1926	b5
	jr z,L192E              ;1927	28 05
	ld bc,$0008             ;1929	01 08 00
	ldir                    ;192c	ed b0

L192E:
	pop de                  ;192e	d1
	push iy                 ;192f	fd e5
	pop hl                  ;1931	e1
	ld bc,$001f             ;1932	01 1f 00
	add hl,bc               ;1935	09
	ld bc,$000c             ;1936	01 0c 00
	ldir                    ;1939	ed b0
	ld h,d                  ;193b	62
	ld l,e                  ;193c	6b
	inc de                  ;193d	13
	ld (hl),$00             ;193e	36 00
	ld bc,$0067             ;1940	01 67 00
	ldir                    ;1943	ed b0
	pop hl                  ;1945	e1
	call L19E1              ;1946	cd e1 19
	ld (hl),a               ;1949	77
	call L0853              ;194a	cd 53 08
	ld d,$00                ;194d	16 00
	ld e,d                  ;194f	5a
	ld h,d                  ;1950	62
	ld l,$80                ;1951	2e 80
	jp L1696                ;1953	c3 96 16

; tentative entry (word / jump table scan)
T1956:
	res 7,(iy+$0f)          ;1956	fd cb 0f be
	call L16BF              ;195a	cd bf 16
	ld a,l                  ;195d	7d
	and $80                 ;195e	e6 80
	or h                    ;1960	b4
	or e                    ;1961	b3
	or d                    ;1962	b2
	jr z,L198C              ;1963	28 27
	call L19D6              ;1965	cd d6 19
	jr nc,L198C             ;1968	30 22
	call L19AE              ;196a	cd ae 19
	jr nz,L198C             ;196d	20 1d
	set 7,(iy+$0f)          ;196f	fd cb 0f fe
	push iy                 ;1973	fd e5
	pop hl                  ;1975	e1
	ld bc,$001f             ;1976	01 1f 00
	add hl,bc               ;1979	09
	ld bc,$000c             ;197a	01 0c 00
	ex de,hl                ;197d	eb
	ldir                    ;197e	ed b0
	call L1805              ;1980	cd 05 18
	ld d,$00                ;1983	16 00
	ld e,d                  ;1985	5a
	ld h,d                  ;1986	62
	ld l,$80                ;1987	2e 80
	jp L1696                ;1989	c3 96 16

L198C:
	call L16BF              ;198c	cd bf 16
	ex de,hl                ;198f	eb
	ld a,h                  ;1990	7c
	or l                    ;1991	b5
	jr z,L1997              ;1992	28 03
	ld de,$ffff             ;1994	11 ff ff

L1997:
	call L19C2              ;1997	cd c2 19
	xor a                   ;199a	af
	ld b,$03                ;199b	06 03
	ld (hl),b               ;199d	70
	inc hl                  ;199e	23
	ld (hl),e               ;199f	73
	inc hl                  ;19a0	23
	ld (hl),d               ;19a1	72
	inc hl                  ;19a2	23
	ld (hl),a               ;19a3	77
	inc hl                  ;19a4	23
	ld (hl),$40             ;19a5	36 40

L19A7:
	inc hl                  ;19a7	23
	ld (hl),a               ;19a8	77
	djnz L19A7              ;19a9	10 fc
	jp L1805                ;19ab	c3 05 18

L19AE:
	push hl                 ;19ae	e5
	call L19E1              ;19af	cd e1 19
	cp (hl)                 ;19b2	be
	pop de                  ;19b3	d1
	ret nz                  ;19b4	c0
	ld hl,$19e9             ;19b5	21 e9 19
	ld b,$0b                ;19b8	06 0b

L19BA:
	ld a,(de)               ;19ba	1a
	cp (hl)                 ;19bb	be
	ret nz                  ;19bc	c0
	inc de                  ;19bd	13
	inc hl                  ;19be	23
	djnz L19BA              ;19bf	10 f9
	ret                     ;19c1	c9

L19C2:
	push iy                 ;19c2	fd e5
	pop hl                  ;19c4	e1
	ld bc,$0023             ;19c5	01 23 00
	add hl,bc               ;19c8	09
	ret                     ;19c9	c9
Text19CA:
	defb 0xcd,0xab,0x11,0xd0,0x7e,0xfe,0x2e,0x3e ;19ca	cd ab 11 d0 7e fe 2e 3e	....~..>
	defb 0x1e,0xc8,0x37,0xc9                     ;19d2	1e c8 37 c9	..7.

L19D6:
	ld h,$00                ;19d6	26 00
	ld l,h                  ;19d8	6c
	ld d,h                  ;19d9	54
	ld e,h                  ;19da	5c
	call L1696              ;19db	cd 96 16
	jp L152B                ;19de	c3 2b 15

L19E1:
	xor a                   ;19e1	af
	ld b,$7f                ;19e2	06 7f

L19E4:
	add (hl)                ;19e4	86
	inc hl                  ;19e5	23
	djnz L19E4              ;19e6	10 fc
	ret                     ;19e8	c9
Text19E9:
	defb 0x50,0x4c,0x55,0x53,0x33,0x44,0x4f,0x53 ;19e9	50 4c 55 53 33 44 4f 53	PLUS3DOS
	defb 0x1a,0x01,0x00,0xe5,0xd5,0xc5,0xdd,0x21 ;19f1	1a 01 00 e5 d5 c5 dd 21	.......!
	defb 0x39,0x01,0xcd,0xf0                     ;19f9	39 01 cd f0	9...
	ld bc,$ddc1             ;19fd	01 c1 dd
	pop hl                  ;1a00	e1
	pop hl                  ;1a01	e1
	ret nc                  ;1a02	d0
	call L1A46              ;1a03	cd 46 1a
	jr z,L1A2A              ;1a06	28 22

L1A08:
	bit 1,b                 ;1a08	cb 48
	jr z,L1A19              ;1a0a	28 0d
	ld a,$7f                ;1a0c	3e 7f
	in a,($fe)              ;1a0e	db fe
	rra                     ;1a10	1f
	jr c,L1A19              ;1a11	38 06
	ld a,$fe                ;1a13	3e fe
	in a,($fe)              ;1a15	db fe
	rra                     ;1a17	1f
	ret nc                  ;1a18	d0

L1A19:
	push bc                 ;1a19	c5
	push hl                 ;1a1a	e5
	push ix                 ;1a1b	dd e5
	xor a                   ;1a1d	af
	ld ix,$013c             ;1a1e	dd 21 3c 01
	call L01F0              ;1a22	cd f0 01
	pop de                  ;1a25	d1
	pop hl                  ;1a26	e1
	pop bc                  ;1a27	c1
	jr L19F4                ;1a28	18 ca

L1A2A:
	bit 5,b                 ;1a2a	cb 68
	scf                     ;1a2c	37
	ret z                   ;1a2d	c8
	push bc                 ;1a2e	c5
	push de                 ;1a2f	d5
	push hl                 ;1a30	e5
	push ix                 ;1a31	dd e5
	pop bc                  ;1a33	c1
	ex de,hl                ;1a34	eb
	inc bc                  ;1a35	03
	ld h,b                  ;1a36	60
	ld l,c                  ;1a37	69
	add hl,$001d            ;1a38	ed 34 1d 00
	call L1A93              ;1a3c	cd 93 1a
	pop hl                  ;1a3f	e1
	pop de                  ;1a40	d1
	pop bc                  ;1a41	c1
	scf                     ;1a42	37
	ret z                   ;1a43	c8
	jr L1A08                ;1a44	18 c2

L1A46:
	ld a,(ix+$00)           ;1a46	dd 7e 00
	bit 4,a                 ;1a49	cb 67
	jr z,L1A61              ;1a4b	28 14
	bit 6,c                 ;1a4d	cb 71
	ret nz                  ;1a4f	c0
	bit 5,c                 ;1a50	cb 69
	jr z,L1A64              ;1a52	28 10
	ld a,(ix+$01)           ;1a54	dd 7e 01
	cp $2e                  ;1a57	fe 2e
	ld a,(ix+$00)           ;1a59	dd 7e 00
	jr nz,L1A64             ;1a5c	20 06
	xor a                   ;1a5e	af
	inc a                   ;1a5f	3c
	ret                     ;1a60	c9

L1A61:
	bit 7,c                 ;1a61	cb 79
	ret nz                  ;1a63	c0

L1A64:
	bit 4,c                 ;1a64	cb 61
	ret z                   ;1a66	c8
	and $06                 ;1a67	e6 06
	ret                     ;1a69	c9
Text1A6A:
	defb 0xe5,0xed,0x34,0x1c,0x00,0x4e,0x23,0x46 ;1a6a	e5 ed 34 1c 00 4e 23 46	..4..N#F
	defb 0x23,0xed,0xb0,0x1b,0x3e,0xff,0x12,0xe1 ;1a72	23 ed b0 1b 3e ff 12 e1	#...>...
	defb 0xed,0x34,0x0c,0x00,0x5e,0x23,0x56,0x23 ;1a7a	ed 34 0c 00 5e 23 56 23	.4..^#V#
	defb 0x4e,0x23,0x46,0x23,0x7e,0x23,0xdd,0x6f ;1a82	4e 23 46 23 7e 23 dd 6f	N#F#~#.o
	defb 0x7e,0x23,0xdd,0x67,0x7e,0x23,0x66,0x6f ;1a8a	7e 23 dd 67 7e 23 66 6f	~#.g~#fo
	defb 0xc9                                    ;1a92	c9	.

L1A93:
	push bc                 ;1a93	c5
	push de                 ;1a94	d5
	jr z,L1AEA              ;1a95	28 53

L1A97:
	ld a,(de)               ;1a97	1a
	inc de                  ;1a98	13
	cp $ff                  ;1a99	fe ff
	jr z,L1AB4              ;1a9b	28 17
	cp $2a                  ;1a9d	fe 2a
	jr z,L1ABB              ;1a9f	28 1a
	cp $3f                  ;1aa1	fe 3f
	ld c,(hl)               ;1aa3	4e
	inc hl                  ;1aa4	23
	jr nz,L1AAC             ;1aa5	20 05
	inc c                   ;1aa7	0c
	jr nz,L1A97             ;1aa8	20 ed
	jr L1AEA                ;1aaa	18 3e

L1AAC:
	cp c                    ;1aac	b9
	call nz,L1B3C           ;1aad	c4 3c 1b
	jr z,L1A97              ;1ab0	28 e5
	jr L1AEA                ;1ab2	18 36

L1AB4:
	ld a,(hl)               ;1ab4	7e
	inc a                   ;1ab5	3c
	jr nz,L1AEA             ;1ab6	20 32

L1AB8:
	pop de                  ;1ab8	d1
	pop hl                  ;1ab9	e1
	ret                     ;1aba	c9

L1ABB:
	ld a,(hl)               ;1abb	7e
	inc hl                  ;1abc	23
	cp $ff                  ;1abd	fe ff
	jr z,L1AD3              ;1abf	28 12
	cp $2e                  ;1ac1	fe 2e
	jr nz,L1ABB             ;1ac3	20 f6

L1AC5:
	push hl                 ;1ac5	e5

L1AC6:
	ld a,(hl)               ;1ac6	7e
	inc hl                  ;1ac7	23
	cp $ff                  ;1ac8	fe ff
	jr z,L1AE3              ;1aca	28 17
	cp $2e                  ;1acc	fe 2e
	jr nz,L1AC6             ;1ace	20 f6
	pop af                  ;1ad0	f1
	jr L1AC5                ;1ad1	18 f2

L1AD3:
	ld a,(de)               ;1ad3	1a
	cp $ff                  ;1ad4	fe ff
	jr z,L1AB8              ;1ad6	28 e0
	cp $2e                  ;1ad8	fe 2e
	jr nz,L1AEA             ;1ada	20 0e
	inc de                  ;1adc	13
	ld a,(de)               ;1add	1a
	inc a                   ;1ade	3c
	jr z,L1AB8              ;1adf	28 d7
	jr L1AEA                ;1ae1	18 07

L1AE3:
	pop hl                  ;1ae3	e1
	ld a,(de)               ;1ae4	1a
	inc de                  ;1ae5	13
	cp $2e                  ;1ae6	fe 2e
	jr z,L1A97              ;1ae8	28 ad

L1AEA:
	pop de                  ;1aea	d1
	pop hl                  ;1aeb	e1
	ld b,$08                ;1aec	06 08
	ld a,(hl)               ;1aee	7e
	cp $2e                  ;1aef	fe 2e
	jr nz,L1B06             ;1af1	20 13
	call L1B15              ;1af3	cd 15 1b
	ret nz                  ;1af6	c0

L1AF7:
	ld a,(de)               ;1af7	1a
	cp $2e                  ;1af8	fe 2e
	jr nz,L1AFD             ;1afa	20 01
	inc de                  ;1afc	13

L1AFD:
	ld b,$03                ;1afd	06 03
	call L1B15              ;1aff	cd 15 1b
	ret nz                  ;1b02	c0
	ld a,(de)               ;1b03	1a
	inc a                   ;1b04	3c
	ret                     ;1b05	c9

L1B06:
	call L1B15              ;1b06	cd 15 1b
	jr z,L1AF7              ;1b09	28 ec
	cp $2e                  ;1b0b	fe 2e
	ret nz                  ;1b0d	c0
	dec hl                  ;1b0e	2b
	call L1B32              ;1b0f	cd 32 1b
	ret nz                  ;1b12	c0
	jr L1AFD                ;1b13	18 e8

L1B15:
	ld a,(de)               ;1b15	1a
	inc de                  ;1b16	13
	cp $2a                  ;1b17	fe 2a
	jr z,L1B2D              ;1b19	28 12
	cp $ff                  ;1b1b	fe ff
	jr z,L1B31              ;1b1d	28 12
	ld c,(hl)               ;1b1f	4e
	inc hl                  ;1b20	23
	cp $3f                  ;1b21	fe 3f
	jr z,L1B2A              ;1b23	28 05
	cp c                    ;1b25	b9
	call nz,L1B3C           ;1b26	c4 3c 1b
	ret nz                  ;1b29	c0

L1B2A:
	djnz L1B15              ;1b2a	10 e9
	ret                     ;1b2c	c9

L1B2D:
	inc hl                  ;1b2d	23
	djnz L1B2D              ;1b2e	10 fd
	ret                     ;1b30	c9

L1B31:
	dec de                  ;1b31	1b

L1B32:
	ld a,(hl)               ;1b32	7e
	inc hl                  ;1b33	23
	cp $20                  ;1b34	fe 20
	ld a,$00                ;1b36	3e 00
	ret nz                  ;1b38	c0
	djnz L1B32              ;1b39	10 f7
	ret                     ;1b3b	c9

L1B3C:
	push af                 ;1b3c	f5
	ld a,c                  ;1b3d	79
	cp $20                  ;1b3e	fe 20
	jr c,L1B46              ;1b40	38 04
	cp $80                  ;1b42	fe 80
	jr c,L1B4A              ;1b44	38 04

L1B46:
	pop af                  ;1b46	f1
	cp $5f                  ;1b47	fe 5f
	ret                     ;1b49	c9

L1B4A:
	pop af                  ;1b4a	f1
	or $20                  ;1b4b	f6 20
	cp $61                  ;1b4d	fe 61
	ret c                   ;1b4f	d8
	cp $7b                  ;1b50	fe 7b
	jr nc,L1B58             ;1b52	30 04
	set 5,c                 ;1b54	cb e9
	cp c                    ;1b56	b9
	ret                     ;1b57	c9

L1B58:
	cp $00                  ;1b58	fe 00
	ret                     ;1b5a	c9

L1B5B:
	ld ixl,$41              ;1b5b	dd 2e 41
	ld a,(hl)               ;1b5e	7e
	inc hl                  ;1b5f	23
	call L1B94              ;1b60	cd 94 1b
	ret c                   ;1b63	d8
	ld ixl,$c1              ;1b64	dd 2e c1
	dec hl                  ;1b67	2b

L1B68:
	ld a,(hl)               ;1b68	7e
	inc hl                  ;1b69	23
	call L1B94              ;1b6a	cd 94 1b
	ret c                   ;1b6d	d8
	cp $20                  ;1b6e	fe 20
	ccf                     ;1b70	3f
	ret nc                  ;1b71	d0
	cp $80                  ;1b72	fe 80
	ret nc                  ;1b74	d0
	cp $22                  ;1b75	fe 22
	ret z                   ;1b77	c8
	cp $3a                  ;1b78	fe 3a
	ret z                   ;1b7a	c8
	cp $3c                  ;1b7b	fe 3c
	ret z                   ;1b7d	c8
	cp $3e                  ;1b7e	fe 3e
	ret z                   ;1b80	c8
	cp $60                  ;1b81	fe 60
	ret z                   ;1b83	c8
	cp $7c                  ;1b84	fe 7c
	ret z                   ;1b86	c8
	cp $3f                  ;1b87	fe 3f
	jr z,L1B8F              ;1b89	28 04
	cp $2a                  ;1b8b	fe 2a
	jr nz,L1B68             ;1b8d	20 d9

L1B8F:
	ld ixl,$81              ;1b8f	dd 2e 81
	jr L1B68                ;1b92	18 d4

L1B94:
	cp $2f                  ;1b94	fe 2f
	scf                     ;1b96	37
	ret z                   ;1b97	c8
	cp $5c                  ;1b98	fe 5c
	scf                     ;1b9a	37
	ret z                   ;1b9b	c8
	cp $ff                  ;1b9c	fe ff
	ccf                     ;1b9e	3f
	ret                     ;1b9f	c9

L1BA0:
	xor a                   ;1ba0	af
	ld d,a                  ;1ba1	57
	ld e,a                  ;1ba2	5f
	ld h,a                  ;1ba3	67
	ld l,a                  ;1ba4	6f
	inc a                   ;1ba5	3c
	ld ix,$013c             ;1ba6	dd 21 3c 01
	call L01F0              ;1baa	cd f0 01
	ret                     ;1bad	c9

; tentative entry (word / jump table scan)
T1BAE:
	ld b,$03                ;1bae	06 03
	ld ix,$0127             ;1bb0	dd 21 27 01
	call L01F0              ;1bb4	cd f0 01
	ret                     ;1bb7	c9
Data1BB8:
	defb 0x06,0x00                               ;1bb8	06 00	..

L1BBA:
	push bc                 ;1bba	c5
	push hl                 ;1bbb	e5
	ld b,$01                ;1bbc	06 01
	ld ix,$0127             ;1bbe	dd 21 27 01
	call L01F0              ;1bc2	cd f0 01
	pop de                  ;1bc5	d1
	pop bc                  ;1bc6	c1
	ret nc                  ;1bc7	d0
	push af                 ;1bc8	f5
	push hl                 ;1bc9	e5
	ex de,hl                ;1bca	eb
	ld a,(hl)               ;1bcb	7e
	cp $2f                  ;1bcc	fe 2f
	jr z,L1BD4              ;1bce	28 04
	cp $5c                  ;1bd0	fe 5c
	jr nz,L1BEE             ;1bd2	20 1a

L1BD4:
	inc hl                  ;1bd4	23
	push bc                 ;1bd5	c5
	push hl                 ;1bd6	e5
	call T1BAE              ;1bd7	cd ae 1b
	pop hl                  ;1bda	e1
	pop bc                  ;1bdb	c1
	ld a,b                  ;1bdc	78
	and a                   ;1bdd	a7
	jr z,L1BEE              ;1bde	28 0e

L1BE0:
	dec bc                  ;1be0	0b
	ld a,(bc)               ;1be1	0a
	cp $3a                  ;1be2	fe 3a
	jr nz,L1BE0             ;1be4	20 fa
	inc bc                  ;1be6	03
	ld a,$2f                ;1be7	3e 2f
	ld (bc),a               ;1be9	02
	inc bc                  ;1bea	03
	ld a,$ff                ;1beb	3e ff
	ld (bc),a               ;1bed	02

L1BEE:
	push bc                 ;1bee	c5
	push hl                 ;1bef	e5
	call L1B5B              ;1bf0	cd 5b 1b
	pop de                  ;1bf3	d1
	pop bc                  ;1bf4	c1
	inc a                   ;1bf5	3c
	jr nc,L1C0A             ;1bf6	30 12
	jr nz,L1C0F             ;1bf8	20 15
	scf                     ;1bfa	37
	sbc hl,de               ;1bfb	ed 52
	inc h                   ;1bfd	24
	dec h                   ;1bfe	25
	jr nz,L1C0A             ;1bff	20 09
	ex de,hl                ;1c01	eb

L1C02:
	pop de                  ;1c02	d1
	pop af                  ;1c03	f1
	ld ixh,a                ;1c04	dd 67
	push ix                 ;1c06	dd e5
	pop af                  ;1c08	f1
	ret                     ;1c09	c9

L1C0A:
	ld a,$14                ;1c0a	3e 14

L1C0C:
	pop hl                  ;1c0c	e1
	pop bc                  ;1c0d	c1
	ret                     ;1c0e	c9

L1C0F:
	push hl                 ;1c0f	e5
	scf                     ;1c10	37
	sbc hl,de               ;1c11	ed 52
	ld a,h                  ;1c13	7c
	pop hl                  ;1c14	e1
	jr z,L1C0A              ;1c15	28 f3
	and a                   ;1c17	a7
	jr nz,L1C0A             ;1c18	20 f0
	dec hl                  ;1c1a	2b
	push hl                 ;1c1b	e5
	ld a,(hl)               ;1c1c	7e
	push af                 ;1c1d	f5
	ld (hl),$ff             ;1c1e	36 ff
	ex de,hl                ;1c20	eb
	call L1C35              ;1c21	cd 35 1c
	pop de                  ;1c24	d1
	pop hl                  ;1c25	e1
	ld (hl),d               ;1c26	72
	inc hl                  ;1c27	23
	jr nc,L1C0C             ;1c28	30 e2
	ld a,(hl)               ;1c2a	7e
	inc a                   ;1c2b	3c
	jr nz,L1BEE             ;1c2c	20 c0
	ld ixl,$41              ;1c2e	dd 2e 41
	jr L1C02                ;1c31	18 cf
Data1C33:
	defb 0x06,0x00                               ;1c33	06 00	..

L1C35:
	ld a,(hl)               ;1c35	7e
	cp $2e                  ;1c36	fe 2e
	jr nz,L1C40             ;1c38	20 06
	inc hl                  ;1c3a	23
	ld a,(hl)               ;1c3b	7e
	dec hl                  ;1c3c	2b
	inc a                   ;1c3d	3c
	scf                     ;1c3e	37
	ret z                   ;1c3f	c8

L1C40:
	push bc                 ;1c40	c5
	push hl                 ;1c41	e5
	call L1C8B              ;1c42	cd 8b 1c
	pop hl                  ;1c45	e1
	pop bc                  ;1c46	c1
	ld a,$45                ;1c47	3e 45
	ret nc                  ;1c49	d0
	ccf                     ;1c4a	3f
	ret z                   ;1c4b	c8
	ld a,b                  ;1c4c	78
	and a                   ;1c4d	a7
	jr z,L1C7F              ;1c4e	28 2f
	ld a,(hl)               ;1c50	7e
	cp $2e                  ;1c51	fe 2e
	jr nz,L1C6D             ;1c53	20 18
	inc hl                  ;1c55	23
	cp (hl)                 ;1c56	be
	jr nz,L1C6D             ;1c57	20 14
	inc hl                  ;1c59	23
	ld a,(hl)               ;1c5a	7e
	inc a                   ;1c5b	3c
	jr nz,L1C6D             ;1c5c	20 0f
	dec bc                  ;1c5e	0b

L1C5F:
	dec bc                  ;1c5f	0b
	ld a,(bc)               ;1c60	0a
	call L1B94              ;1c61	cd 94 1b
	jr c,L1C7B              ;1c64	38 15
	cp $3a                  ;1c66	fe 3a
	jr nz,L1C5F             ;1c68	20 f5
	inc bc                  ;1c6a	03
	jr L1C78                ;1c6b	18 0b

L1C6D:
	ld d,b                  ;1c6d	50
	ld e,c                  ;1c6e	59
	ld hl,$21c1             ;1c6f	21 c1 21
	call L1083              ;1c72	cd 83 10
	ld b,d                  ;1c75	42
	ld c,e                  ;1c76	4b
	dec bc                  ;1c77	0b

L1C78:
	ld a,$2f                ;1c78	3e 2f
	ld (bc),a               ;1c7a	02

L1C7B:
	inc bc                  ;1c7b	03
	ld a,$ff                ;1c7c	3e ff
	ld (bc),a               ;1c7e	02

L1C7F:
	push bc                 ;1c7f	c5
	ld b,$05                ;1c80	06 05
	ld ix,$0127             ;1c82	dd 21 27 01
	call L01F0              ;1c86	cd f0 01
	pop bc                  ;1c89	c1
	ret                     ;1c8a	c9

L1C8B:
	push hl                 ;1c8b	e5
	call L1BA0              ;1c8c	cd a0 1b
	pop hl                  ;1c8f	e1
	ld de,$21c0             ;1c90	11 c0 21
	ld bc,$2000             ;1c93	01 00 20
	call L19F4              ;1c96	cd f4 19
	ret nc                  ;1c99	d0
	ld a,($21c0)            ;1c9a	3a c0 21
	bit 4,a                 ;1c9d	cb 67
	scf                     ;1c9f	37
	ret                     ;1ca0	c9

L1CA1:
	call L10C1              ;1ca1	cd c1 10
	push de                 ;1ca4	d5
	push bc                 ;1ca5	c5
	call L123E              ;1ca6	cd 3e 12
	pop bc                  ;1ca9	c1
	ld c,e                  ;1caa	4b
	pop de                  ;1cab	d1
	ret nc                  ;1cac	d0
	push bc                 ;1cad	c5
	ld a,c                  ;1cae	79
	ld (de),a               ;1caf	12
	inc de                  ;1cb0	13
	ld b,$0b                ;1cb1	06 0b

L1CB3:
	ld a,(hl)               ;1cb3	7e
	inc hl                  ;1cb4	23
	cp $80                  ;1cb5	fe 80
	jr nc,L1CBD             ;1cb7	30 04
	cp $20                  ;1cb9	fe 20
	jr nc,L1CBF             ;1cbb	30 02

L1CBD:
	ld a,$5f                ;1cbd	3e 5f

L1CBF:
	ld (de),a               ;1cbf	12
	inc de                  ;1cc0	13
	djnz L1CB3              ;1cc1	10 f0
	add hl,$000b            ;1cc3	ed 34 0b 00
	ld bc,$0004             ;1cc7	01 04 00
	ldir                    ;1cca	ed b0
	inc hl                  ;1ccc	23
	inc hl                  ;1ccd	23
	ld c,$04                ;1cce	0e 04
	ldir                    ;1cd0	ed b0
	pop bc                  ;1cd2	c1
	push de                 ;1cd3	d5
	bit 6,b                 ;1cd4	cb 70
	jr z,L1D19              ;1cd6	28 41
	dec hl                  ;1cd8	2b
	ld a,(hl)               ;1cd9	7e
	dec hl                  ;1cda	2b
	or (hl)                 ;1cdb	b6
	dec hl                  ;1cdc	2b
	or (hl)                 ;1cdd	b6
	dec hl                  ;1cde	2b
	jr nz,L1CE5             ;1cdf	20 04
	bit 7,(hl)              ;1ce1	cb 7e
	jr z,L1D0C              ;1ce3	28 27

L1CE5:
	add hl,$fff8            ;1ce5	ed 34 f8 ff
	ld e,(hl)               ;1ce9	5e
	inc hl                  ;1cea	23
	ld d,(hl)               ;1ceb	56
	add hl,$0005            ;1cec	ed 34 05 00
	ld c,(hl)               ;1cf0	4e
	inc hl                  ;1cf1	23
	ld b,(hl)               ;1cf2	46
	xor a                   ;1cf3	af
	call L06EE              ;1cf4	cd ee 06
	jr nc,L1D0C             ;1cf7	30 13
	call L19AE              ;1cf9	cd ae 19
	jr nz,L1D0C             ;1cfc	20 0e
	ex de,hl                ;1cfe	eb
	add hl,$0004            ;1cff	ed 34 04 00
	pop de                  ;1d03	d1
	push de                 ;1d04	d5
	ld bc,$0008             ;1d05	01 08 00
	ldir                    ;1d08	ed b0
	jr L1D19                ;1d0a	18 0d

L1D0C:
	pop de                  ;1d0c	d1
	push de                 ;1d0d	d5
	ld a,$ff                ;1d0e	3e ff
	ld (de),a               ;1d10	12
	inc de                  ;1d11	13
	ld b,$07                ;1d12	06 07
	xor a                   ;1d14	af

L1D15:
	ld (de),a               ;1d15	12
	inc de                  ;1d16	13
	djnz L1D15              ;1d17	10 fc

L1D19:
	pop de                  ;1d19	d1
	add de,$000a            ;1d1a	ed 35 0a 00
	push de                 ;1d1e	d5
	call L1067              ;1d1f	cd 67 10
	pop hl                  ;1d22	e1
	push de                 ;1d23	d5
	ex de,hl                ;1d24	eb
	and a                   ;1d25	a7
	sbc hl,de               ;1d26	ed 52
	ex de,hl                ;1d28	eb
	dec hl                  ;1d29	2b
	ld (hl),d               ;1d2a	72
	dec hl                  ;1d2b	2b
	ld (hl),e               ;1d2c	73
	pop de                  ;1d2d	d1
	scf                     ;1d2e	37
	ret                     ;1d2f	c9
	call L10C1              ;1d30	cd c1 10
	jr nc,L1D9A             ;1d33	30 65
	ld de,$0000             ;1d35	11 00 00
	ld l,(iy+$08)           ;1d38	fd 6e 08
	ld h,(iy+$09)           ;1d3b	fd 66 09
	ld b,$04                ;1d3e	06 04
	bit 7,(iy+$02)          ;1d40	fd cb 02 7e
	jr nz,L1D84             ;1d44	20 3e
	push de                 ;1d46	d5
	push de                 ;1d47	d5
	rst $28                 ;1d48	ef
	inc bc                  ;1d49	03

L1D4A:
	push de                 ;1d4a	d5
	push bc                 ;1d4b	c5
	rst $28                 ;1d4c	ef
	ex af,af'               ;1d4d	08
	ld h,b                  ;1d4e	60
	ld l,c                  ;1d4f	69
	pop bc                  ;1d50	c1
	and a                   ;1d51	a7
	sbc hl,bc               ;1d52	ed 42
	ex de,hl                ;1d54	eb
	pop de                  ;1d55	d1
	jr nz,L1D5C             ;1d56	20 04
	sbc hl,de               ;1d58	ed 52
	jr z,L1D72              ;1d5a	28 16

L1D5C:
	call L0912              ;1d5c	cd 12 09
	pop hl                  ;1d5f	e1
	jr nc,L1D98             ;1d60	30 36
	call GetDriveFlag       ;1d62	cd ef 1f
	add a                   ;1d65	87
	jr c,L1D6C              ;1d66	38 04
	add h                   ;1d68	84
	ld h,a                  ;1d69	67
	jr nc,L1D6F             ;1d6a	30 03

L1D6C:
	ex (sp),hl              ;1d6c	e3
	inc hl                  ;1d6d	23
	ex (sp),hl              ;1d6e	e3

L1D6F:
	push hl                 ;1d6f	e5
	jr L1D4A                ;1d70	18 d8

L1D72:
	pop hl                  ;1d72	e1
	pop de                  ;1d73	d1
	ld a,(iy+$07)           ;1d74	fd 7e 07
	add a                   ;1d77	87
	add h                   ;1d78	84
	ld h,a                  ;1d79	67
	jr nc,L1D7D             ;1d7a	30 01
	inc de                  ;1d7c	13

L1D7D:
	ld l,h                  ;1d7d	6c
	ld h,e                  ;1d7e	63
	ld e,d                  ;1d7f	5a
	ld d,$00                ;1d80	16 00
	ld b,$03                ;1d82	06 03

L1D84:
	add hl,hl               ;1d84	29
	ex de,hl                ;1d85	eb
	adc hl,hl               ;1d86	ed 6a
	ex de,hl                ;1d88	eb
	djnz L1D84              ;1d89	10 f9
	ld c,(iy+$0c)           ;1d8b	fd 4e 0c
	ld b,$00                ;1d8e	06 00
	add hl,bc               ;1d90	09
	ex de,hl                ;1d91	eb
	ld c,b                  ;1d92	48
	adc hl,bc               ;1d93	ed 4a
	ex de,hl                ;1d95	eb
	scf                     ;1d96	37
	ret                     ;1d97	c9

L1D98:
	pop hl                  ;1d98	e1
	ret                     ;1d99	c9

L1D9A:
	jp z,L113F              ;1d9a	ca 3f 11
	push de                 ;1d9d	d5
	push hl                 ;1d9e	e5
	call L10F7              ;1d9f	cd f7 10

L1DA2:
	pop hl                  ;1da2	e1
	pop de                  ;1da3	d1
	ld a,d                  ;1da4	7a
	or e                    ;1da5	b3
	or h                    ;1da6	b4
	or l                    ;1da7	b5
	scf                     ;1da8	37
	ret z                   ;1da9	c8
	dec hl                  ;1daa	2b
	ld a,h                  ;1dab	7c
	and l                   ;1dac	a5
	inc a                   ;1dad	3c
	jr nz,L1DB1             ;1dae	20 01
	dec de                  ;1db0	1b

L1DB1:
	push de                 ;1db1	d5
	push hl                 ;1db2	e5
	call L113F              ;1db3	cd 3f 11
	jr L1DA2                ;1db6	18 ea

L1DB8:
	ld iyh,a                ;1db8	fd 67
	call L0204              ;1dba	cd 04 02
	call L040D              ;1dbd	cd 0d 04
	jp $c91d                ;1dc0	c3 1d c9
	inc b                   ;1dc3	04
	dec b                   ;1dc4	05
	jr z,L1DD8              ;1dc5	28 11
	dec b                   ;1dc7	05
	jp nz,$3d01             ;1dc8	c2 01 3d
	push hl                 ;1dcb	e5
	call L1DF0              ;1dcc	cd f0 1d
	pop de                  ;1dcf	d1

L1DD0:
	ld a,(hl)               ;1dd0	7e
	ldi                     ;1dd1	ed a0
	inc a                   ;1dd3	3c
	jr nz,L1DD0             ;1dd4	20 fa
	scf                     ;1dd6	37
	ret                     ;1dd7	c9

L1DD8:
	push af                 ;1dd8	f5
	call L1DF0              ;1dd9	cd f0 1d
	pop de                  ;1ddc	d1
	ret nc                  ;1ddd	d0
	ld a,d                  ;1dde	7a
	push af                 ;1ddf	f5
	call L1E7A              ;1de0	cd 7a 1e
	pop af                  ;1de3	f1
	and $0f                 ;1de4	e6 0f
	ld b,$02                ;1de6	06 02
	ld ix,$0127             ;1de8	dd 21 27 01
	call L01F0              ;1dec	cd f0 01
	ret                     ;1def	c9

L1DF0:
	push hl                 ;1df0	e5
	push af                 ;1df1	f5
	call L1E05              ;1df2	cd 05 1e
	pop af                  ;1df5	f1
	ex (sp),hl              ;1df6	e3
	ld b,d                  ;1df7	42
	ld c,e                  ;1df8	4b
	call L1BBA              ;1df9	cd ba 1b
	pop de                  ;1dfc	d1
	ret nc                  ;1dfd	d0
	push de                 ;1dfe	d5
	call m,L1C35            ;1dff	fc 35 1c
	pop de                  ;1e02	d1
	ex de,hl                ;1e03	eb
	ret                     ;1e04	c9

L1E05:
	inc de                  ;1e05	13
	inc de                  ;1e06	13
	and $0f                 ;1e07	e6 0f
	push af                 ;1e09	f5
	add $20                 ;1e0a	c6 20
	ld h,a                  ;1e0c	67
	ld l,$00                ;1e0d	2e 00
	ld bc,$00fe             ;1e0f	01 fe 00
	ld a,$09                ;1e12	3e 09
	push de                 ;1e14	d5
	call L0769              ;1e15	cd 69 07
	pop hl                  ;1e18	e1
	pop de                  ;1e19	d1
	ld a,(hl)               ;1e1a	7e
	cpl                     ;1e1b	2f
	inc hl                  ;1e1c	23
	cp (hl)                 ;1e1d	be
	jr nz,L1E31             ;1e1e	20 11
	and $0f                 ;1e20	e6 0f
	cp d                    ;1e22	ba
	jr nz,L1E31             ;1e23	20 0c
	ld d,(hl)               ;1e25	56
	inc hl                  ;1e26	23
	ld a,(hl)               ;1e27	7e
	dec hl                  ;1e28	2b
	cp $2f                  ;1e29	fe 2f
	jr z,L1E3A              ;1e2b	28 0d
	cp $5c                  ;1e2d	fe 5c
	jr z,L1E3A              ;1e2f	28 09

L1E31:
	ld (hl),d               ;1e31	72
	inc hl                  ;1e32	23
	ld (hl),$2f             ;1e33	36 2f
	inc hl                  ;1e35	23
	ld (hl),$ff             ;1e36	36 ff
	dec hl                  ;1e38	2b
	dec hl                  ;1e39	2b

L1E3A:
	ld (hl),$3a             ;1e3a	36 3a
	ld a,d                  ;1e3c	7a
	and $0f                 ;1e3d	e6 0f
	add $41                 ;1e3f	c6 41
	dec hl                  ;1e41	2b
	ld (hl),a               ;1e42	77
	ld b,$fd                ;1e43	06 fd
	ld a,d                  ;1e45	7a
	call L041F              ;1e46	cd 1f 04
	nop                     ;1e49	00
	inc b                   ;1e4a	04
	ld (bc),a               ;1e4b	02
	bit 6,a                 ;1e4c	cb 77
	jr z,L1E65              ;1e4e	28 15
	dec hl                  ;1e50	2b
	inc b                   ;1e51	04
	ld a,d                  ;1e52	7a
	swapnib                 ;1e53	ed 23
	and $0f                 ;1e55	e6 0f
	cp $0a                  ;1e57	fe 0a
	jr c,L1E62              ;1e59	38 07
	add $26                 ;1e5b	c6 26
	ld (hl),a               ;1e5d	77
	dec hl                  ;1e5e	2b
	inc b                   ;1e5f	04
	ld a,$01                ;1e60	3e 01

L1E62:
	add $30                 ;1e62	c6 30
	ld (hl),a               ;1e64	77

L1E65:
	ld c,d                  ;1e65	4a
	ld d,h                  ;1e66	54
	ld e,l                  ;1e67	5d

L1E68:
	inc de                  ;1e68	13
	ld a,(de)               ;1e69	1a
	inc a                   ;1e6a	3c
	ret z                   ;1e6b	c8
	cp $21                  ;1e6c	fe 21
	jr c,L1E76              ;1e6e	38 06
	cp $80                  ;1e70	fe 80
	jr nc,L1E76             ;1e72	30 02
	djnz L1E68              ;1e74	10 f2

L1E76:
	ld a,$ff                ;1e76	3e ff
	ld (de),a               ;1e78	12
	ret                     ;1e79	c9

L1E7A:
	ld d,a                  ;1e7a	57

L1E7B:
	inc hl                  ;1e7b	23
	ld a,(hl)               ;1e7c	7e
	cp $3a                  ;1e7d	fe 3a
	jr nz,L1E7B             ;1e7f	20 fa
	ld (hl),d               ;1e81	72
	dec hl                  ;1e82	2b
	ld a,(hl)               ;1e83	7e
	push af                 ;1e84	f5
	ld a,d                  ;1e85	7a
	cpl                     ;1e86	2f
	ld (hl),a               ;1e87	77
	ld a,d                  ;1e88	7a
	and $0f                 ;1e89	e6 0f
	add $20                 ;1e8b	c6 20
	ld d,a                  ;1e8d	57
	ld e,$00                ;1e8e	1e 00
	ld bc,$00fe             ;1e90	01 fe 00
	ld a,$09                ;1e93	3e 09
	push hl                 ;1e95	e5
	call L0703              ;1e96	cd 03 07
	pop hl                  ;1e99	e1
	pop af                  ;1e9a	f1
	ld (hl),a               ;1e9b	77
	inc hl                  ;1e9c	23
	ld (hl),$3a             ;1e9d	36 3a
	ret                     ;1e9f	c9

L1EA0:
	xor a                   ;1ea0	af
	out ($e3),a             ;1ea1	d3 e3
	jp L1FF9                ;1ea3	c3 f9 1f
Zero1EA6:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;1ea6	00 00 00 00 00 00 00 00	........
	defb 0x00                                    ;1eae	00	.

L1EAF:
	xor a                   ;1eaf	af

L1EB0:
	push bc                 ;1eb0	c5
	push de                 ;1eb1	d5
	push hl                 ;1eb2	e5
	call SectorToCardAddress;1eb3	cd d2 1e
	ex (sp),ix              ;1eb6	dd e3
	jr nc,L1EC9             ;1eb8	30 0f
	push af                 ;1eba	f5
	bit 7,a                 ;1ebb	cb 7f
	jr z,L1EC5              ;1ebd	28 06
	pop af                  ;1ebf	f1
	call SdWriteBlock       ;1ec0	cd 92 1f
	jr L1EC9                ;1ec3	18 04

L1EC5:
	pop af                  ;1ec5	f1
	call SdReadBlock        ;1ec6	cd 5c 1f

L1EC9:
	pop ix                  ;1ec9	dd e1
	pop de                  ;1ecb	d1
	pop bc                  ;1ecc	c1
	ret                     ;1ecd	c9

L1ECE:
	ld a,$80                ;1ece	3e 80
	jr L1EB0                ;1ed0	18 de

SectorToCardAddress:
	ld l,(ix+$07)           ;1ed2	dd 6e 07
	ld h,(ix+$08)           ;1ed5	dd 66 08
	and a                   ;1ed8	a7
	sbc hl,de               ;1ed9	ed 52
	ld l,(ix+$09)           ;1edb	dd 6e 09
	ld h,(ix+$0a)           ;1ede	dd 66 0a
	sbc hl,bc               ;1ee1	ed 42
	jr nc,L1EE9             ;1ee3	30 04
	ld a,$02                ;1ee5	3e 02
	and a                   ;1ee7	a7
	ret                     ;1ee8	c9

L1EE9:
	ld l,(ix+$01)           ;1ee9	dd 6e 01
	ld h,(ix+$02)           ;1eec	dd 66 02
	add hl,de               ;1eef	19
	ex de,hl                ;1ef0	eb
	ld l,(ix+$03)           ;1ef1	dd 6e 03
	ld h,(ix+$04)           ;1ef4	dd 66 04
	adc hl,bc               ;1ef7	ed 4a
	bit 1,(ix+$10)          ;1ef9	dd cb 10 4e
	jr nz,L1F09             ;1efd	20 0a
	ld h,l                  ;1eff	65
	ld l,d                  ;1f00	6a
	ld d,e                  ;1f01	53
	ld e,$00                ;1f02	1e 00
	ex de,hl                ;1f04	eb
	add hl,hl               ;1f05	29
	ex de,hl                ;1f06	eb
	adc hl,hl               ;1f07	ed 6a

L1F09:
	bit 0,(ix+$10)          ;1f09	dd cb 10 46
	scf                     ;1f0d	37
	ret                     ;1f0e	c9

SdCommandZeroArg:
	ld h,$00                ;1f0f	26 00
	ld l,$00                ;1f11	2e 00
	ld d,l                  ;1f13	55
	ld e,l                  ;1f14	5d

; A = command byte, HL:DE = argument, B = CRC. Selects card 0 (OUT #E7,#FE) or card 1 (#FD) by Z,
; one dummy read, then six OUT (#EB) with 4-T spacing (a transfer takes 16 CPU clocks).
SdCommand:
	ld b,$ff                ;1f15	06 ff
	ld c,a                  ;1f17	4f
	ld a,$fe                ;1f18	3e fe
	jr z,L1F1E              ;1f1a	28 02
	ld a,$fd                ;1f1c	3e fd

L1F1E:
	out ($e7),a             ;1f1e	d3 e7
	in a,($eb)              ;1f20	db eb
	ld a,c                  ;1f22	79
	ld c,$eb                ;1f23	0e eb
	out (c),a               ;1f25	ed 79
	ld a,h                  ;1f27	7c
	out (c),a               ;1f28	ed 79
	ld a,l                  ;1f2a	7d
	out (c),a               ;1f2b	ed 79
	ld a,d                  ;1f2d	7a
	out (c),a               ;1f2e	ed 79
	ld a,e                  ;1f30	7b
	out (c),a               ;1f31	ed 79
	ld a,b                  ;1f33	78
	out (c),a               ;1f34	ed 79
	call SdWaitNotFF        ;1f36	cd 3d 1f
	and a                   ;1f39	a7
	ret nz                  ;1f3a	c0
	scf                     ;1f3b	37
	ret                     ;1f3c	c9

; Poll IN (#EB) until the byte is not #FF (about #32 * 256 tries).
SdWaitNotFF:
	ld bc,$0032             ;1f3d	01 32 00

L1F40:
	in a,($eb)              ;1f40	db eb
	cp $ff                  ;1f42	fe ff
	ret nz                  ;1f44	c0
	djnz L1F40              ;1f45	10 f9
	dec c                   ;1f47	0d
	jr nz,L1F40             ;1f48	20 f6
	ret                     ;1f4a	c9

; Wait for the data token #FE (up to 10 polls).
SdWaitDataToken:
	ld e,$0a                ;1f4b	1e 0a

L1F4D:
	call SdWaitNotFF        ;1f4d	cd 3d 1f
	cp $fe                  ;1f50	fe fe
	jr z,L1F5A              ;1f52	28 06
	jr c,L1F5A              ;1f54	38 04
	dec e                   ;1f56	1d
	jr nz,L1F4D             ;1f57	20 f4
	ccf                     ;1f59	3f

L1F5A:
	ccf                     ;1f5a	3f
	ret                     ;1f5b	c9

; CMD17: read one 512-byte block into (IX) with two INIR, two CRC reads, deselect.
SdReadBlock:
	ld a,$51                ;1f5c	3e 51
	call SdCommand          ;1f5e	cd 15 1f
	call SdWaitDataToken    ;1f61	cd 4b 1f
	ld a,$00                ;1f64	3e 00
	jr nc,SdDeselect        ;1f66	30 11
	push ix                 ;1f68	dd e5
	pop hl                  ;1f6a	e1
	ld bc,$00eb             ;1f6b	01 eb 00
	inir                    ;1f6e	ed b2
	inir                    ;1f70	ed b2
	in a,($eb)              ;1f72	db eb
	nop                     ;1f74	00
	nop                     ;1f75	00
	in a,($eb)              ;1f76	db eb
	scf                     ;1f78	37

; Common exit: OUT (#E7),#FF with dummy reads.
SdDeselect:
	push af                 ;1f79	f5
	in a,($eb)              ;1f7a	db eb
	ld a,$ff                ;1f7c	3e ff
	out ($e7),a             ;1f7e	d3 e7
	nop                     ;1f80	00
	in a,($eb)              ;1f81	db eb
	pop af                  ;1f83	f1
	ret                     ;1f84	c9

; CMD12 and wait for the busy byte to clear.
SdStopTransmission:
	ld a,$4c                ;1f85	3e 4c
	call SdCommandZeroArg   ;1f87	cd 0f 1f

L1F8A:
	in a,($eb)              ;1f8a	db eb
	and a                   ;1f8c	a7
	scf                     ;1f8d	37
	jr z,L1F8A              ;1f8e	28 fa
	jr SdDeselect           ;1f90	18 e7

; CMD24: write one block (token #FE, two OTIR, two #FF CRC bytes), data response and CMD13 status.
SdWriteBlock:
	push af                 ;1f92	f5
	ld a,$58                ;1f93	3e 58
	call SdCommand          ;1f95	cd 15 1f
	jr nc,L1FD9             ;1f98	30 3f
	ld a,$fe                ;1f9a	3e fe
	out ($eb),a             ;1f9c	d3 eb
	ld bc,$00eb             ;1f9e	01 eb 00
	push ix                 ;1fa1	dd e5
	pop hl                  ;1fa3	e1
	otir                    ;1fa4	ed b3
	otir                    ;1fa6	ed b3
	ld a,$ff                ;1fa8	3e ff
	out ($eb),a             ;1faa	d3 eb
	nop                     ;1fac	00
	nop                     ;1fad	00
	out ($eb),a             ;1fae	d3 eb
	call SdWaitNotFF        ;1fb0	cd 3d 1f
	and $1f                 ;1fb3	e6 1f
	cp $05                  ;1fb5	fe 05
	jr nz,L1FD9             ;1fb7	20 20

L1FB9:
	call SdWaitNotFF        ;1fb9	cd 3d 1f
	and a                   ;1fbc	a7
	jr z,L1FB9              ;1fbd	28 fa
	pop af                  ;1fbf	f1
	ld a,$4d                ;1fc0	3e 4d
	push hl                 ;1fc2	e5
	call SdCommandZeroArg   ;1fc3	cd 0f 1f
	pop hl                  ;1fc6	e1
	jr nc,L1FDA             ;1fc7	30 11
	in a,($eb)              ;1fc9	db eb
	and a                   ;1fcb	a7
	scf                     ;1fcc	37
	jr z,L1FDD              ;1fcd	28 0e
	and $23                 ;1fcf	e6 23
	ld a,$01                ;1fd1	3e 01
	jr nz,L1FDD             ;1fd3	20 08
	ld a,$03                ;1fd5	3e 03
	jr L1FDD                ;1fd7	18 04

L1FD9:
	pop af                  ;1fd9	f1

L1FDA:
	ld a,$00                ;1fda	3e 00
	and a                   ;1fdc	a7

L1FDD:
	jr SdDeselect           ;1fdd	18 9a

Inc32BitPosition:
	inc (ix+$2b)            ;1fdf	dd 34 2b
	ret nz                  ;1fe2	c0
	inc (ix+$2c)            ;1fe3	dd 34 2c
	ret nz                  ;1fe6	c0
	inc (ix+$2d)            ;1fe7	dd 34 2d
	ret nz                  ;1fea	c0
	inc (ix+$2e)            ;1feb	dd 34 2e
	ret                     ;1fee	c9

GetDriveFlag:
	ld a,(ix+$21)           ;1fef	dd 7e 21
	ret                     ;1ff2	c9

; OUT (#E3),#80: CONMEM on (DivMMC ROM and RAM mapped).
SetConMem:
	push af                 ;1ff3	f5
	ld a,$80                ;1ff4	3e 80
	out ($e3),a             ;1ff6	d3 e3
	pop af                  ;1ff8	f1

L1FF9:
	ret                     ;1ff9	c9
Data1FFA:
	defb 0xc9,0xe7                               ;1ffa	c9 e7	..

L1FFC:
	pop hl                  ;1ffc	e1
	pop af                  ;1ffd	f1
	ei                      ;1ffe	fb
	ret                     ;1fff	c9
