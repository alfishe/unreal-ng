;===========================================================================
; NextZXOS ROM 1
; image: next-rom1.bin, 16384 bytes, origin #0000
;===========================================================================

	org 0x0000


L0000:
	nop                     ;0000	00
	jp L3F00                ;0001	c3 00 3f
Fill0004:
	defb 0xff                                    ;0004	ff	.
	rst $38                 ;0005	ff
	rst $38                 ;0006	ff
	rst $38                 ;0007	ff

L0008:
	jp L0DD4                ;0008	c3 d4 0d
Zero000B:
	defb 0x00,0x00                               ;000b	00 00	..
	nop                     ;000d	00
	nop                     ;000e	00
	nop                     ;000f	00

L0010:
	ld ($5b52),hl           ;0010	22 52 5b
	pop hl                  ;0013	e1
	push af                 ;0014	f5
	jp L0EA8                ;0015	c3 a8 0e

L0018:
	jp L2B0C                ;0018	c3 0c 2b
Data001B:
	defb 0xed,0xb0,0xc9,0xcf,0x10                ;001b	ed b0 c9 cf 10	.....

L0020:
	ld hl,($5c5d)           ;0020	2a 5d 5c
	jp L3ECF                ;0023	c3 cf 3e
	and b                   ;0026	a0
	nop                     ;0027	00

L0028:
	ld ($5b54),bc           ;0028	ed 43 54 5b
	ex (sp),hl              ;002c	e3
	jp L2D57                ;002d	c3 57 2d

L0030:
	jp L2AF5                ;0030	c3 f5 2a
Zero0033:
	defb 0x00,0x00,0x00,0x00,0x00                ;0033	00 00 00 00 00	.....

L0038:
	push af                 ;0038	f5
	push hl                 ;0039	e5
	ld h,$00                ;003a	26 00
	ld a,$80                ;003c	3e 80
	jp L0046                ;003e	c3 46 00
Zero0041:
	defb 0x00,0x00,0x00,0x00,0x00                ;0041	00 00 00 00 00	.....

L0046:
	out ($e3),a             ;0046	d3 e3
	ld b,e                  ;0048	43
	ld a,($442f)            ;0049	3a 2f 44
	ld c,a                  ;004c	4f
	ld d,h                  ;004d	54
	cpl                     ;004e	2f
	ld b,(hl)               ;004f	46
	ld c,l                  ;0050	4d
	rst $38                 ;0051	ff

L0052:
	call L381B              ;0052	cd 1b 38
	or e                    ;0055	b3
	or d                    ;0056	b2
	or c                    ;0057	b1
	ret                     ;0058	c9

L0059:
	call L26C5              ;0059	cd c5 26
	sub e                   ;005c	93
	add hl,hl               ;005d	29
	ret                     ;005e	c9
	nop                     ;005f	00
	pop hl                  ;0060	e1
	pop af                  ;0061	f1
	ei                      ;0062	fb
	ret                     ;0063	c9
Zero0064:
	defb 0x00                                    ;0064	00	.
	nop                     ;0065	00

L0066:
	retn                    ;0066	ed 45

; tentative entry (word / jump table scan)
T0068:
	ld ($5b54),bc           ;0068	ed 43 54 5b
	ex (sp),hl              ;006c	e3
	ld c,(hl)               ;006d	4e
	inc hl                  ;006e	23
	ld b,(hl)               ;006f	46
	inc hl                  ;0070	23
	ex (sp),hl              ;0071	e3

L0072:
	push $007b              ;0072	ed 8a 00 7b
	push bc                 ;0076	c5
	ld bc,($5b54)           ;0077	ed 4b 54 5b
	nextreg $8c,$80         ;007b	ed 91 8c 80
	ret                     ;007f	c9

L0080:
	ld ($5b54),bc           ;0080	ed 43 54 5b
	ex (sp),hl              ;0084	e3
	ld c,(hl)               ;0085	4e
	inc hl                  ;0086	23
	ld b,(hl)               ;0087	46
	inc hl                  ;0088	23
	ex (sp),hl              ;0089	e3

L008A:
	push $3e93              ;008a	ed 8a 3e 93
	push $007b              ;008e	ed 8a 00 7b
	push bc                 ;0092	c5
	push $007b              ;0093	ed 8a 00 7b
	jp L3E8F                ;0097	c3 8f 3e
	inc de                  ;009a	13
	push de                 ;009b	d5
	call L37FC              ;009c	cd fc 37
	pop hl                  ;009f	e1
	ld c,(hl)               ;00a0	4e
	inc hl                  ;00a1	23
	ld b,(hl)               ;00a2	46
	ld d,a                  ;00a3	57
	ld a,c                  ;00a4	79
	cp d                    ;00a5	ba
	jr nc,L00AA             ;00a6	30 02
	rst $08                 ;00a8	cf
	inc de                  ;00a9	13

L00AA:
	dec b                   ;00aa	05
	jr z,L00B3              ;00ab	28 06
	rlc d                   ;00ad	cb 02
	rlc c                   ;00af	cb 01
	jr L00AA                ;00b1	18 f7

L00B3:
	rst $18                 ;00b3	df
	ld a,c                  ;00b4	79
	cpl                     ;00b5	2f
	ld c,a                  ;00b6	4f
	ld hl,$3760             ;00b7	21 60 37
	rst $00                 ;00ba	c7
	ld l,l                  ;00bb	6d
	nop                     ;00bc	00
	and c                   ;00bd	a1
	or d                    ;00be	b2
	rst $00                 ;00bf	c7
	ld (hl),d               ;00c0	72
	nop                     ;00c1	00
	rst $30                 ;00c2	f7
	ret                     ;00c3	c9
Data00C4:
	defb 0xcd,0xdc,0x08,0x3e,0x10                ;00c4	cd dc 08 3e 10	...>.
	jr z,L00E0              ;00c9	28 15
	call L0E2D              ;00cb	cd 2d 0e
	call L0902              ;00ce	cd 02 09
	bit 6,(iy+$01)          ;00d1	fd cb 01 76
	jr z,L011D              ;00d5	28 46
	call L37FC              ;00d7	cd fc 37
	rrca                    ;00da	0f
	cp $18                  ;00db	fe 18
	jp nz,L334A             ;00dd	c2 4a 33

L00E0:
	call L0902              ;00e0	cd 02 09
	rst $18                 ;00e3	df
	rst $00                 ;00e4	c7
	ld (hl),b               ;00e5	70
	dec c                   ;00e6	0d
	jr nc,L0135             ;00e7	30 4c
	rst $30                 ;00e9	f7

L00EA:
	ex af,af'               ;00ea	08
	xor a                   ;00eb	af
	ld ($5b5c),a            ;00ec	32 5c 5b
	ld (iy+$0a),$ff         ;00ef	fd 36 0a ff
	ld a,e                  ;00f3	7b
	res 4,(iy+$01)          ;00f4	fd cb 01 a6
	ld hl,($5cb2)           ;00f8	2a b2 5c
	ld (hl),$3e             ;00fb	36 3e
	dec hl                  ;00fd	2b
	ld sp,hl                ;00fe	f9
	ld hl,$1303             ;00ff	21 03 13
	push hl                 ;0102	e5
	ld ($5c3d),sp           ;0103	ed 73 3d 5c
	set 1,(iy+$01)          ;0107	fd cb 01 ce
	ld hl,$006b             ;010b	21 6b 00
	ld de,$5b00             ;010e	11 00 5b
	ld bc,$0058             ;0111	01 58 00
	exx                     ;0114	d9
	ld hl,$018d             ;0115	21 8d 01
	ld bc,$001c             ;0118	01 1c 00
	jr L0185                ;011b	18 68

L011D:
	call L381B              ;011d	cd 1b 38
	ex de,hl                ;0120	eb
	ld de,$da35             ;0121	11 35 da
	call L2B23              ;0124	cd 23 2b
	rst $18                 ;0127	df
	ld a,$ff                ;0128	3e ff
	ld (de),a               ;012a	12
	xor a                   ;012b	af
	call L14F0              ;012c	cd f0 14
	ld hl,$da35             ;012f	21 35 da
	rst $00                 ;0132	c7
	nop                     ;0133	fd 00

L0135:
	jp L0DFF                ;0135	c3 ff 0d
	rst $18                 ;0138	df
	ld a,$10                ;0139	3e 10
	rst $00                 ;013b	c7
	ld (hl),b               ;013c	70
	dec c                   ;013d	0d
	jp nc,L0135             ;013e	d2 35 01
	nextreg $c4,$00         ;0141	ed 91 c4 00
	ld sp,$5bff             ;0145	31 ff 5b
	ld ix,($5c78)           ;0148	dd 2a 78 5c
	ld a,($5c7a)            ;014c	3a 7a 5c
	ld l,a                  ;014f	6f
	ld bc,$7ffd             ;0150	01 fd 7f
	ld h,$08                ;0153	26 08

L0155:
	ld a,h                  ;0155	7c
	dec a                   ;0156	3d
	or $10                  ;0157	f6 10
	out (c),a               ;0159	ed 79
	exx                     ;015b	d9
	ld hl,$c000             ;015c	21 00 c0
	ld de,$c001             ;015f	11 01 c0
	ld bc,$3fff             ;0162	01 ff 3f
	cp $15                  ;0165	fe 15
	jr nz,L016E             ;0167	20 05
	ld h,$db                ;0169	26 db
	ld d,h                  ;016b	54
	ld b,$24                ;016c	06 24

L016E:
	ld (hl),l               ;016e	75
	ldir                    ;016f	ed b0
	exx                     ;0171	d9
	dec h                   ;0172	25
	jr nz,L0155             ;0173	20 e0
	ld ($5c78),ix           ;0175	dd 22 78 5c
	ld a,l                  ;0179	7d
	ld ($5c7a),a            ;017a	32 7a 5c
	ld a,e                  ;017d	7b
	exx                     ;017e	d9
	ld hl,$01a9             ;017f	21 a9 01
	ld bc,$0038             ;0182	01 38 00

L0185:
	ld de,$5bb8             ;0185	11 b8 5b
	push de                 ;0188	d5
	ldir                    ;0189	ed b0
	exx                     ;018b	d9
	ret                     ;018c	c9
	nextreg $8e,$03         ;018d	ed 91 8e 03
	nextreg $82,a           ;0191	ed 92 82
	ex af,af'               ;0194	08
	nextreg $8c,a           ;0195	ed 92 8c
	push $1b76              ;0198	ed 8a 1b 76
	bit 5,a                 ;019c	cb 6f
	jp nz,L0EDF             ;019e	c2 df 0e
	call $5b43              ;01a1	cd 43 5b
	ldir                    ;01a4	ed b0
	jp $5b00                ;01a6	c3 00 5b
	nextreg $8e,$03         ;01a9	ed 91 8e 03
	nextreg $82,a           ;01ad	ed 92 82
	xor a                   ;01b0	af
	out (c),a               ;01b1	ed 79
	nextreg $8c,$c0         ;01b3	ed 91 8c c0
	ld hl,$5bd3             ;01b7	21 d3 5b
	ld ($25c6),hl           ;01ba	22 c6 25
	nextreg $8c,$80         ;01bd	ed 91 8c 80
	jp L00EA                ;01c1	c3 ea 00
Data01C4:
	defb 0xe1                                    ;01c4	e1	.
	nextreg $8c,$c0         ;01c5	ed 91 8c c0
	ld hl,$36a8             ;01c9	21 a8 36
	ld ($25c6),hl           ;01cc	22 c6 25
	nextreg $8c,$80         ;01cf	ed 91 8c 80
	ld bc,$243b             ;01d3	01 3b 24
	ld a,$24                ;01d6	3e 24
	out (c),a               ;01d8	ed 79
	nextreg $c4,$81         ;01da	ed 91 c4 81
	jp L2831                ;01de	c3 31 28
	call L37FC              ;01e1	cd fc 37
	cp $80                  ;01e4	fe 80

; tentative entry (word / jump table scan)
T01E6:
	jp nc,L142D             ;01e6	d2 2d 14
	push af                 ;01e9	f5
	call L37FC              ;01ea	cd fc 37
	cp $02                  ;01ed	fe 02
	jr nc,T01E6             ;01ef	30 f5
	rra                     ;01f1	1f
	pop bc                  ;01f2	c1
	ld a,b                  ;01f3	78
	jr nc,L01F8             ;01f4	30 02
	or $80                  ;01f6	f6 80

L01F8:
	ld ($5c81),a            ;01f8	32 81 5c
	ld hl,$2252             ;01fb	21 52 22
	call L26C5              ;01fe	cd c5 26
	ld d,b                  ;0201	50
	ld ($cdc9),hl           ;0202	22 c9 cd
	call m,$fe37            ;0205	fc 37 fe
	jr nz,L0232             ;0208	20 28
	add hl,bc               ;020a	09
	cp $40                  ;020b	fe 40
	jr z,L0214              ;020d	28 05
	cp $55                  ;020f	fe 55
	jp nz,L142D             ;0211	c2 2d 14

L0214:
	rst $18                 ;0214	df
	ld l,a                  ;0215	6f
	call L0080              ;0216	cd 80 00
	pop bc                  ;0219	c1
	inc de                  ;021a	13
	rst $30                 ;021b	f7
	ret                     ;021c	c9
	nop                     ;021d	00
	nop                     ;021e	00
	nop                     ;021f	00
	nop                     ;0220	00
	rra                     ;0221	1f
	rra                     ;0222	1f
	rra                     ;0223	1f
	rra                     ;0224	1f
	dec h                   ;0225	25
	rra                     ;0226	1f
	rra                     ;0227	1f
	rra                     ;0228	1f
	rra                     ;0229	1f
	rra                     ;022a	1f
	rra                     ;022b	1f
	rra                     ;022c	1f
	rra                     ;022d	1f
	rra                     ;022e	1f
	rra                     ;022f	1f
	rra                     ;0230	1f
	rra                     ;0231	1f

L0232:
	rra                     ;0232	1f
	rra                     ;0233	1f
	rra                     ;0234	1f
	rra                     ;0235	1f
	rra                     ;0236	1f
	rra                     ;0237	1f
	rra                     ;0238	1f
	rra                     ;0239	1f
	ld ($1f40),hl           ;023a	22 40 1f
	rra                     ;023d	1f
	rra                     ;023e	1f
	rra                     ;023f	1f
	jr z,L0267              ;0240	28 25
	dec h                   ;0242	25
	dec h                   ;0243	25
	dec h                   ;0244	25
	dec h                   ;0245	25
	dec h                   ;0246	25
	dec h                   ;0247	25
	dec h                   ;0248	25
	dec h                   ;0249	25
	dec h                   ;024a	25
	dec h                   ;024b	25
	dec h                   ;024c	25
	dec h                   ;024d	25
	dec h                   ;024e	25
	dec h                   ;024f	25
	dec h                   ;0250	25
	dec h                   ;0251	25
	dec h                   ;0252	25
	dec h                   ;0253	25
	dec h                   ;0254	25
	dec h                   ;0255	25
	dec h                   ;0256	25
	dec h                   ;0257	25
	dec h                   ;0258	25
	dec h                   ;0259	25
	dec h                   ;025a	25
	rra                     ;025b	1f
	rra                     ;025c	1f
	rra                     ;025d	1f
	rra                     ;025e	1f
	rra                     ;025f	1f
	rra                     ;0260	1f
	dec h                   ;0261	25
	dec h                   ;0262	25
	dec h                   ;0263	25
	dec h                   ;0264	25
	dec h                   ;0265	25
	dec h                   ;0266	25

L0267:
	dec h                   ;0267	25
	dec h                   ;0268	25
	dec h                   ;0269	25
	dec h                   ;026a	25
	dec h                   ;026b	25
	dec h                   ;026c	25
	dec h                   ;026d	25
	dec h                   ;026e	25
	dec h                   ;026f	25
	dec h                   ;0270	25
	dec h                   ;0271	25
	dec h                   ;0272	25
	dec h                   ;0273	25
	dec h                   ;0274	25
	dec h                   ;0275	25
	dec h                   ;0276	25
	dec h                   ;0277	25
	dec h                   ;0278	25
	dec h                   ;0279	25
	dec h                   ;027a	25
	rra                     ;027b	1f
	rra                     ;027c	1f
	rra                     ;027d	1f
	rra                     ;027e	1f
	rra                     ;027f	1f
	rra                     ;0280	1f
	add hl,de               ;0281	19
	inc e                   ;0282	1c
	pop de                  ;0283	d1
	ld sp,$1f94             ;0284	31 94 1f
	rra                     ;0287	1f
	cp (hl)                 ;0288	be
	call z,T1F1F            ;0289	cc 1f 1f
	rra                     ;028c	1f
	rra                     ;028d	1f
	rra                     ;028e	1f
	dec bc                  ;028f	0b
	ld (hl),e               ;0290	73
	halt                    ;0291	76
	ld a,c                  ;0292	79
	ld a,h                  ;0293	7c
	ld a,a                  ;0294	7f
	xor (hl)                ;0295	ae
	add d                   ;0296	82
	add hl,bc               ;0297	09
	add l                   ;0298	85
	adc b                   ;0299	88
	dec c                   ;029a	0d
	rrca                    ;029b	0f
	ld de,$8b13             ;029c	11 13 8b
	adc (hl)                ;029f	8e
	or d                    ;02a0	b2
	or (hl)                 ;02a1	b6
	cp d                    ;02a2	ba
	dec d                   ;02a3	15
	sub c                   ;02a4	91
	rra                     ;02a5	1f
	rra                     ;02a6	1f
	rra                     ;02a7	1f
	rra                     ;02a8	1f
	and $1f                 ;02a9	e6 1f
	rra                     ;02ab	1f
	rra                     ;02ac	1f
	rra                     ;02ad	1f
	rra                     ;02ae	1f
	rra                     ;02af	1f
	rra                     ;02b0	1f
	rra                     ;02b1	1f
	rra                     ;02b2	1f
	rra                     ;02b3	1f
	rra                     ;02b4	1f
	rra                     ;02b5	1f
	rra                     ;02b6	1f
	rra                     ;02b7	1f
	rra                     ;02b8	1f
	rra                     ;02b9	1f
	rra                     ;02ba	1f
	rra                     ;02bb	1f
	rra                     ;02bc	1f
	rra                     ;02bd	1f
	rra                     ;02be	1f
	rra                     ;02bf	1f
	rra                     ;02c0	1f
	rra                     ;02c1	1f
	rra                     ;02c2	1f
	rra                     ;02c3	1f
	rra                     ;02c4	1f
	rra                     ;02c5	1f
	rra                     ;02c6	1f
	rra                     ;02c7	1f
	rra                     ;02c8	1f
	rra                     ;02c9	1f
	rla                     ;02ca	17
	rra                     ;02cb	1f
	rra                     ;02cc	1f
	rra                     ;02cd	1f
	ld h,a                  ;02ce	67
	ld (hl),b               ;02cf	70
	xor e                   ;02d0	ab
	dec hl                  ;02d1	2b
	ld l,l                  ;02d2	6d
	ret po                  ;02d3	e0
	ld l,d                  ;02d4	6a
	ld bc,$9f01             ;02d5	01 01 9f
	rst $00                 ;02d8	c7
	nop                     ;02d9	00
	nop                     ;02da	00
	nop                     ;02db	00
	nop                     ;02dc	00
	nop                     ;02dd	00
	nop                     ;02de	00
	and e                   ;02df	a3
	ld h,c                  ;02e0	61
	ld h,h                  ;02e1	64
	scf                     ;02e2	37
	ld e,b                  ;02e3	58
	ld e,e                  ;02e4	5b
	ld e,(hl)               ;02e5	5e
	ld b,e                  ;02e6	43
	and a                   ;02e7	a7
	ld c,h                  ;02e8	4c
	jp c,$f340              ;02e9	da 40 f3
	inc bc                  ;02ec	03
	inc (hl)                ;02ed	34
	dec a                   ;02ee	3d
	ld bc,$0246             ;02ef	01 46 02
	ld d,l                  ;02f2	55
	call pe,L3AC2           ;02f3	ec c2 3a
	sub a                   ;02f6	97
	dec b                   ;02f7	05
	ld bc,$2e49             ;02f8	01 49 2e
	ld d,d                  ;02fb	52
	sbc e                   ;02fc	9b
	ld c,a                  ;02fd	4f
	call nc,L0707           ;02fe	d4 07 07
	dec bc                  ;0301	0b
	ld (bc),a               ;0302	02
	rrca                    ;0303	0f
	add hl,sp               ;0304	39
	rrca                    ;0305	0f
	dec a                   ;0306	3d
	rrca                    ;0307	0f
	nop                     ;0308	00
	rrca                    ;0309	0f
	ld c,l                  ;030a	4d
	rrca                    ;030b	0f
	ld c,c                  ;030c	49
	rrca                    ;030d	0f
	ld (de),a               ;030e	12
	rrca                    ;030f	0f
	ld d,a                  ;0310	57
	rrca                    ;0311	0f
	ld h,a                  ;0312	67
	rrca                    ;0313	0f
	ld e,l                  ;0314	5d
	rrca                    ;0315	0f
	ld (hl),a               ;0316	77
	rrca                    ;0317	0f
	ld d,c                  ;0318	51
	inc c                   ;0319	0c
	call c,T0E3F            ;031a	dc 3f 0e
	ld a,l                  ;031d	7d
	ld e,$10                ;031e	1e 10
	sbc l                   ;0320	9d
	add hl,bc               ;0321	09
	djnz L0374              ;0322	10 50
	add hl,bc               ;0324	09
	ld c,$a9                ;0325	0e a9
	dec b                   ;0327	05
	ld c,$51                ;0328	0e 51
	inc de                  ;032a	13
	ld a,(bc)               ;032b	0a
	rrca                    ;032c	0f
	ld b,c                  ;032d	41
	inc bc                  ;032e	03
	sub (hl)                ;032f	96
	ld d,$0e                ;0330	16 0e
	jr nc,L034B             ;0332	30 17
	ld c,$82                ;0334	0e 82
	jr nz,L0344             ;0336	20 0c
	ld d,l                  ;0338	55
	dec l                   ;0339	2d
	ld c,$d5                ;033a	0e d5
	ld l,$0e                ;033c	2e 0e
	ld iyl,$0e              ;033e	fd 2e 0e
	daa                     ;0341	27
	add hl,bc               ;0342	09
	inc c                   ;0343	0c

L0344:
	ld c,l                  ;0344	4d
	inc (hl)                ;0345	34
	ld c,$f4                ;0346	0e f4
	dec (hl)                ;0348	35
	dec c                   ;0349	0d
	or a                    ;034a	b7

L034B:
	ccf                     ;034b	3f
	inc c                   ;034c	0c
	ld sp,$0d36             ;034d	31 36 0d
	ld (hl),a               ;0350	77
	inc (hl)                ;0351	34
	inc c                   ;0352	0c
	and d                   ;0353	a2
	ld (hl),$00             ;0354	36 00
	ld b,h                  ;0356	44
	dec l                   ;0357	2d
	ld c,$c8                ;0358	0e c8
	inc (hl)                ;035a	34
	ld c,$c1                ;035b	0e c1
	dec (hl)                ;035d	35
	ld c,$65                ;035e	0e 65
	ld (hl),$0e             ;0360	36 0e
	pop de                  ;0362	d1
	ld l,$0e                ;0363	2e 0e
	ret p                   ;0365	f0
	dec (hl)                ;0366	35
	ld c,$2e                ;0367	0e 2e
	ld a,(de)               ;0369	1a

L036A:
	nop                     ;036a	00
	ld l,h                  ;036b	6c
	ccf                     ;036c	3f
	ld c,$6b                ;036d	0e 6b
	sbc $0e                 ;036f	de 0e
	ld c,l                  ;0371	4d
	sbc a                   ;0372	9f
	ld c,$7c                ;0373	0e 7c
	rla                     ;0375	17
	ld c,$ab                ;0376	0e ab
	ld a,(de)               ;0378	1a
	ld c,$b7                ;0379	0e b7
	ld a,(de)               ;037b	1a
	ld c,$1c                ;037c	0e 1c
	dec e                   ;037e	1d
	ld c,$46                ;037f	0e 46
	ld e,$00                ;0381	1e 00
	ld c,$1a                ;0383	0e 1a
	ld c,$04                ;0385	0e 04
	rla                     ;0387	17
	inc c                   ;0388	0c
	sbc l                   ;0389	9d
	dec hl                  ;038a	2b
	ld c,$6b                ;038b	0e 6b
	add l                   ;038d	85
	ld c,$37                ;038e	0e 37
	xor h                   ;0390	ac
	ld c,$7f                ;0391	0e 7f
	sub (hl)                ;0393	96
	ld c,$13                ;0394	0e 13
	add hl,de               ;0396	19
	add hl,bc               ;0397	09
	inc c                   ;0398	0c
	rrca                    ;0399	0f
	dec l                   ;039a	2d
	add hl,bc               ;039b	09
	ld c,$15                ;039c	0e 15
	dec l                   ;039e	2d
	ex af,af'               ;039f	08
	inc c                   ;03a0	0c
	ret m                   ;03a1	f8
	ld b,e                  ;03a2	43
	ex af,af'               ;03a3	08
	inc c                   ;03a4	0c
	ld a,d                  ;03a5	7a
	ld e,(hl)               ;03a6	5e
	ld b,$0c                ;03a7	06 0c
	sub h                   ;03a9	94
	ld h,d                  ;03aa	62
	ld c,$6c                ;03ab	0e 6c
	xor h                   ;03ad	ac
	ex af,af'               ;03ae	08
	ld c,$04                ;03af	0e 04
	ld a,$0a                ;03b1	3e 0a
	inc c                   ;03b3	0c
	dec c                   ;03b4	0d
	xor h                   ;03b5	ac
	ld a,(bc)               ;03b6	0a
	inc c                   ;03b7	0c
	inc hl                  ;03b8	23
	xor h                   ;03b9	ac
	ld a,(bc)               ;03ba	0a
	inc c                   ;03bb	0c
	jr nc,L036A             ;03bc	30 ac
	ex af,af'               ;03be	08
	inc c                   ;03bf	0c
	srl a                   ;03c0	cb 3f
	ld b,$2c                ;03c2	06 2c
	ld c,$9a                ;03c4	0e 9a
	inc sp                  ;03c6	33
	add hl,bc               ;03c7	09
	inc l                   ;03c8	2c
	nop                     ;03c9	00
	ld (hl),h               ;03ca	74
	dec l                   ;03cb	2d
	ld b,$2c                ;03cc	06 2c
	ld c,$93                ;03ce	0e 93
	inc sp                  ;03d0	33
	inc bc                  ;03d1	03
	ld (hl),d               ;03d2	72
	ld d,$05                ;03d3	16 05
	inc a                   ;03d5	3c
	ccf                     ;03d6	3f
	inc c                   ;03d7	0c
	sub e                   ;03d8	93
	rra                     ;03d9	1f
	dec b                   ;03da	05
	jr nc,L041C             ;03db	30 3f
	ld c,$02                ;03dd	0e 02
	ld l,h                  ;03df	6c
	ld b,$2c                ;03e0	06 2c
	ld a,(bc)               ;03e2	0a
	inc c                   ;03e3	0c
	ld e,e                  ;03e4	5b
	ccf                     ;03e5	3f
	ex af,af'               ;03e6	08
	call z,T0C01            ;03e7	cc 01 0c
	jp L053F                ;03ea	c3 3f 05
	inc (hl)                ;03ed	34
	ccf                     ;03ee	3f
	inc b                   ;03ef	04
	inc c                   ;03f0	0c
	ld e,h                  ;03f1	5c
	jr L03F8                ;03f2	18 04
Data03F4:
	defb 0x3d,0x06,0xcc,0x03                     ;03f4	3d 06 cc 03	=...

L03F8:
	ret pe                  ;03f8	e8
	rla                     ;03f9	17
	call L0080              ;03fa	cd 80 00
	and (hl)                ;03fd	a6
	ld a,(de)               ;03fe	1a
	ret                     ;03ff	c9
	ld (bc),a               ;0400	02
	dec c                   ;0401	0d
	ld a,($8503)            ;0402	3a 03 85
	add h                   ;0405	84
	ld a,(bc)               ;0406	0a
	call z,L0A0F            ;0407	cc 0f 0a
	inc bc                  ;040a	03
	xor d                   ;040b	aa
	ret po                  ;040c	e0
	inc hl                  ;040d	23
	ld a,(hl)               ;040e	7e
	add c                   ;040f	81
	add b                   ;0410	80
	add d                   ;0411	82
	ld bc,$02e6             ;0412	01 e6 02
	add d                   ;0415	82
	ld b,$0f                ;0416	06 0f
	add hl,de               ;0418	19
	rrca                    ;0419	0f
	sub e                   ;041a	93
	call pe,$f4ed           ;041b	ec ed f4
	adc c                   ;041e	89
	rst $38                 ;041f	ff
	jp nc,$fd9c             ;0420	d2 9c fd
	push hl                 ;0423	e5
	jp z,$d5f0              ;0424	ca f0 d5
	ret nz                  ;0427	c0
	ret nc                  ;0428	d0
	ld (hl),d               ;0429	72
	ld a,b                  ;042a	78
	ld (hl),c               ;042b	71
	ld (hl),e               ;042c	73
	ld a,b                  ;042d	78
	ld a,h                  ;042e	7c
	add e                   ;042f	83
	add l                   ;0430	85
	add a                   ;0431	87
	adc c                   ;0432	89
	adc e                   ;0433	8b
	adc l                   ;0434	8d
	sub b                   ;0435	90
	sub d                   ;0436	92
	ld a,b                  ;0437	78
	ld (hl),a               ;0438	77
	ld bc,$9023             ;0439	01 23 90
	sub d                   ;043c	92
	ld bc,$94ac             ;043d	01 ac 94
	sub (hl)                ;0440	96
	inc bc                  ;0441	03
	cp a                    ;0442	bf
	rst $18                 ;0443	df
	call z,$9494            ;0444	cc 94 94
	sub a                   ;0447	97
	sbc c                   ;0448	99
	ld bc,$9acc             ;0449	01 cc 9a
	sbc h                   ;044c	9c
	ld bc,$9c8e             ;044d	01 8e 9c
	sbc (hl)                ;0450	9e
	ld (bc),a               ;0451	02
	push de                 ;0452	d5
	and $9d                 ;0453	e6 9d
	and b                   ;0455	a0
	and e                   ;0456	a3
	ld (bc),a               ;0457	02
	sbc d                   ;0458	9a
	jp (hl)                 ;0459	e9
	and d                   ;045a	a2
	and h                   ;045b	a4
	and (hl)                ;045c	a6
	inc b                   ;045d	04
	jp (hl)                 ;045e	e9
	ret nc                  ;045f	d0
	sbc $fd                 ;0460	de fd
	and (hl)                ;0462	a6
	and (hl)                ;0463	a6
	xor b                   ;0464	a8
	xor d                   ;0465	aa
	xor h                   ;0466	ac
	rlca                    ;0467	07
	sbc l                   ;0468	9d
	sbc $9a                 ;0469	de 9a
	xor h                   ;046b	ac
	jp nc,$e9fd             ;046c	d2 fd e9
	and (hl)                ;046f	a6
	xor b                   ;0470	a8
	xor d                   ;0471	aa
	xor h                   ;0472	ac
	xor a                   ;0473	af
	or d                    ;0474	b2
	or a                    ;0475	b7
	cp c                    ;0476	b9
	ex af,af'               ;0477	08
	xor e                   ;0478	ab
	exx                     ;0479	d9
	jp c,$dcdb              ;047a	da db dc
	xor d                   ;047d	aa
	jp nz,$b5ef             ;047e	c2 ef b5
	or a                    ;0481	b7
	cp e                    ;0482	bb
	cp a                    ;0483	bf
	ret z                   ;0484	c8
	jp nz,$cecb             ;0485	c2 cb ce
	ret nc                  ;0488	d0
	inc c                   ;0489	0c
	xor h                   ;048a	ac
	ld c,(hl)               ;048b	4e
	ld a,(bc)               ;048c	0a
	inc c                   ;048d	0c
	rst $00                 ;048e	c7
	and h                   ;048f	a4
	inc c                   ;0490	0c
	pop hl                  ;0491	e1
	and h                   ;0492	a4
	inc hl                  ;0493	23
	nop                     ;0494	00
	ldix                    ;0495	ed a4
	ld bc,$620c             ;0497	01 0c 62
	ld ($0eef),a            ;049a	32 ef 0e
	ld e,e                  ;049d	5b
	jr nz,L04AE             ;049e	20 0e
	ld e,a                  ;04a0	5f
	jr nz,L04B1             ;04a1	20 0e
	inc de                  ;04a3	13
	dec e                   ;04a4	1d
	ld b,$2c                ;04a5	06 2c
	ld c,$79                ;04a7	0e 79
	inc sp                  ;04a9	33
	ld b,$2c                ;04aa	06 2c
	ld c,$72                ;04ac	0e 72

L04AE:
	inc sp                  ;04ae	33
	inc c                   ;04af	0c
	adc d                   ;04b0	8a

L04B1:
	jr nc,L04C1             ;04b1	30 0e
	daa                     ;04b3	27
	ld sp,$ba0e             ;04b4	31 0e ba
	jr nc,L04BC             ;04b7	30 03
	sub h                   ;04b9	94
	ld sp,$870c             ;04ba	31 0c 87
	ld ($5e0e),a            ;04bd	32 0e 5e
	ld (hl),$08             ;04c0	36 08
	ld c,$cf                ;04c2	0e cf
	sbc $0e                 ;04c4	de 0e
	ex (sp),hl              ;04c6	e3
	dec (hl)                ;04c7	35
	inc c                   ;04c8	0c
	add (hl)                ;04c9	86
	ret po                  ;04ca	e0
	ld c,$7e                ;04cb	0e 7e
	jr nz,L04F2             ;04cd	20 23
	ex af,af'               ;04cf	08
	inc c                   ;04d0	0c
	ld (hl),h               ;04d1	74
	ccf                     ;04d2	3f
	ld c,$59                ;04d3	0e 59
	inc (hl)                ;04d5	34
	nop                     ;04d6	00
	ld l,b                  ;04d7	68
	dec l                   ;04d8	2d
	cp a                    ;04d9	bf
	ld a,(bc)               ;04da	0a
	inc c                   ;04db	0c
	ld a,a                  ;04dc	7f
	xor d                   ;04dd	aa
	inc c                   ;04de	0c
	or e                    ;04df	b3
	xor d                   ;04e0	aa
	ld a,(bc)               ;04e1	0a
	inc c                   ;04e2	0c
	ld hl,($0caa)           ;04e3	2a aa 0c
	ld l,a                  ;04e6	6f
	inc l                   ;04e7	2c
	ld c,$4f                ;04e8	0e 4f
	rla                     ;04ea	17
	inc c                   ;04eb	0c
	jp p,L0019              ;04ec	f2 19 00
	ret m                   ;04ef	f8
	add hl,de               ;04f0	19
	ex af,af'               ;04f1	08

L04F2:
	ld c,$d3                ;04f2	0e d3
	ex af,af'               ;04f4	dd 08
	inc c                   ;04f6	0c
	inc h                   ;04f7	24
	ret po                  ;04f8	e0
	ld c,$1e                ;04f9	0e 1e
	sbc $0e                 ;04fb	de 0e
	dec (hl)                ;04fd	35
	adc a                   ;04fe	8f
	inc bc                  ;04ff	03
	rst $28                 ;0500	ef
	sub c                   ;0501	91
	ex af,af'               ;0502	08
	inc l                   ;0503	2c
	ex af,af'               ;0504	08
	ld c,$38                ;0505	0e 38
	sub d                   ;0507	92
	jp (hl)                 ;0508	e9
	nop                     ;0509	00
	inc a                   ;050a	3c
	sub (hl)                ;050b	96
	nop                     ;050c	00
	xor d                   ;050d	aa
	sub h                   ;050e	94
	nop                     ;050f	00
	ld h,b                  ;0510	60
	sub (hl)                ;0511	96
	inc c                   ;0512	0c
	or (hl)                 ;0513	b6
	dec d                   ;0514	15
	inc bc                  ;0515	03
	inc e                   ;0516	1c
	inc d                   ;0517	14
	inc bc                  ;0518	03
	call c,L0094            ;0519	dc 94 00
	rst $10                 ;051c	d7
	sub d                   ;051d	92
	ex af,af'               ;051e	08
	inc c                   ;051f	0c
	inc sp                  ;0520	33
	sub e                   ;0521	93
	ex af,af'               ;0522	08
	ld c,$f1                ;0523	0e f1
	sub d                   ;0525	92
	ex af,af'               ;0526	08
	inc l                   ;0527	2c
	ex af,af'               ;0528	08
	ld c,$f8                ;0529	0e f8
	sub e                   ;052b	93
	inc c                   ;052c	0c
	ld b,$15                ;052d	06 15
	ex af,af'               ;052f	08
	inc l                   ;0530	2c
	ex af,af'               ;0531	08
	inc c                   ;0532	0c
	adc h                   ;0533	8c
	sub e                   ;0534	93
	ld c,$c4                ;0535	0e c4
	nop                     ;0537	00
	nop                     ;0538	00
	sbc d                   ;0539	9a
	nop                     ;053a	00
	rst $38                 ;053b	ff
	ld bc,$9a00             ;053c	01 00 9a

L053F:
	nop                     ;053f	00
	rlca                    ;0540	07
	ld bc,$9a00             ;0541	01 00 9a
	nop                     ;0544	00
	rlca                    ;0545	07
	inc b                   ;0546	04
	nop                     ;0547	00
	sbc d                   ;0548	9a
	nop                     ;0549	00
	ld bc,$0007             ;054a	01 07 00
	sbc d                   ;054d	9a
	nop                     ;054e	00
	ld bc,$0808             ;054f	01 08 08
	inc c                   ;0552	0c
	pop hl                  ;0553	e1
	ld bc,$0400             ;0554	01 00 04
	ld (bc),a               ;0557	02
	inc c                   ;0558	0c
	jr c,L055C              ;0559	38 01
	scf                     ;055b	37

L055C:
	ld b,a                  ;055c	47
	ld d,e                  ;055d	53
	dec (hl)                ;055e	35
	rst $20                 ;055f	e7
	call p,$9ed8            ;0560	f4 d8 9e
	ld e,ixl                ;0563	dd 5d
	ld d,(hl)               ;0565	56
	sub e                   ;0566	93
	dec c                   ;0567	0d
	rlca                    ;0568	07
	inc c                   ;0569	0c
	rlc c                   ;056a	cb 01
	pop bc                  ;056c	c1
	jr L0575                ;056d	18 06
Text056F:
	defb 0xd5,0xcd,0x21,0x0e,0xd1,0xbf           ;056f	d5 cd 21 0e d1 bf	..!...

L0575:
	pop bc                  ;0575	c1
	call z,L0902            ;0576	cc 02 09
	ex de,hl                ;0579	eb
	ld c,(hl)               ;057a	4e
	inc hl                  ;057b	23
	ld b,(hl)               ;057c	46
	ex de,hl                ;057d	eb
	ld a,b                  ;057e	78
	add a                   ;057f	87
	ld a,(hl)               ;0580	7e
	jr c,L0588              ;0581	38 05
	jp m,L3EBF              ;0583	fa bf 3e
	push bc                 ;0586	c5
	ret                     ;0587	c9

L0588:
	res 7,b                 ;0588	cb b8
	jp p,L0072              ;058a	f2 72 00
	res 6,b                 ;058d	cb b0
	jp L008A                ;058f	c3 8a 00
Text0592:
	defb 0xbf,0xf5,0xcd,0x39,0x06,0xcd,0xc9,0x3e ;0592	bf f5 cd 39 06 cd c9 3e	...9...>
	defb 0x47,0xf1,0xed,0x5b,0x74,0x5c           ;059a	47 f1 ed 5b 74 5c	G..[t\
	ld a,b                  ;05a0	78
	jr L0575                ;05a1	18 d2

L05A3:
	ld bc,$1c1f             ;05a3	01 1f 1c
	jp L3EC1                ;05a6	c3 c1 3e
	ld a,ixh                ;05a9	dd 7c
	dec hl                  ;05ab	2b
	ld ($5c5d),hl           ;05ac	22 5d 5c
	push bc                 ;05af	c5
	pop bc                  ;05b0	c1
	push hl                 ;05b1	e5
	cp $25                  ;05b2	fe 25
	jp nz,L0691             ;05b4	c2 91 06
	jp L0680                ;05b7	c3 80 06
	rst $20                 ;05ba	e7
	ld bc,$1c8c             ;05bb	01 8c 1c
	jp L3EC1                ;05be	c3 c1 3e
	bit 7,(iy+$01)          ;05c1	fd cb 01 7e
	call nz,L1381           ;05c5	c4 81 13
	call L3EC9              ;05c8	cd c9 3e
	call L2F69              ;05cb	cd 69 2f
	call L3FE7              ;05ce	cd e7 3f
	jr nc,L05F4             ;05d1	30 21
	ld hl,($5c6c)           ;05d3	2a 6c 5c
	ld b,(iy+$57)           ;05d6	fd 46 57
	ld de,$0000             ;05d9	11 00 00
	bit 0,b                 ;05dc	cb 40
	jr z,L05EA              ;05de	28 0a
	dec e                   ;05e0	1d
	bit 2,b                 ;05e1	cb 50
	jr nz,L05F0             ;05e3	20 0b
	ld a,h                  ;05e5	7c
	xor l                   ;05e6	ad
	ld d,a                  ;05e7	57
	jr L05F0                ;05e8	18 06

L05EA:
	ld d,l                  ;05ea	55
	bit 2,b                 ;05eb	cb 50
	jr z,L05F0              ;05ed	28 01
	ld d,h                  ;05ef	54

L05F0:
	ld ($5c6c),de           ;05f0	ed 53 6c 5c

L05F4:
	call L3EC9              ;05f4	cd c9 3e
	jr L0640                ;05f7	18 47
	pop af                  ;05f9	f1
	ld bc,$2cd2             ;05fa	01 d2 2c
	jp L0072                ;05fd	c3 72 00
Data0600:
	defb 0xfd,0xcb,0x01,0x7e,0xdd                ;0600	fd cb 01 7e dd	...~.
	push hl                 ;0605	e5
	res 0,(iy+$02)          ;0606	fd cb 02 86
	jr z,T0618              ;060a	28 0c
	ld a,$fe                ;060c	3e fe
	rst $28                 ;060e	ef
	ld bc,$ef16             ;060f	01 16 ef
	ld c,l                  ;0612	4d
	dec c                   ;0613	0d
	set 5,(iy+$45)          ;0614	fd cb 45 ee

; tentative entry (word / jump table scan)
T0618:
	pop af                  ;0618	f1
	pop bc                  ;0619	c1
	call L2F84              ;061a	cd 84 2f
	res 5,(iy+$45)          ;061d	fd cb 45 ae
	call L0902              ;0621	cd 02 09
	ld hl,($5c8f)           ;0624	2a 8f 5c
	ld ($5c8d),hl           ;0627	22 8d 5c
	ld hl,$5c91             ;062a	21 91 5c
	ld a,(hl)               ;062d	7e
	rlca                    ;062e	07
	xor (hl)                ;062f	ae
	and $aa                 ;0630	e6 aa
	xor (hl)                ;0632	ae
	ld (hl),a               ;0633	77
	ret                     ;0634	c9
Data0635:
	defb 0xc3,0xbc,0x08                          ;0635	c3 bc 08	...

; tentative entry (word / jump table scan)
T0638:
	rst $20                 ;0638	e7

L0639:
	ld bc,$1c82             ;0639	01 82 1c
	jp L3EC1                ;063c	c3 c1 3e

; tentative entry (word / jump table scan)
T063F:
	rst $20                 ;063f	e7

L0640:
	ld bc,$1c7a             ;0640	01 7a 1c
	jp L3EC1                ;0643	c3 c1 3e
L0646:
	defb 0xfd,0xcb,0x01,0x86,0xef,0x6c,0x1c,0xfd ;0646	fd cb 01 86 ef 6c 1c fd	.....l..
	defb 0xcb,0x01,0x46,0xc8,0xcf,0x60,0xeb,0x4e ;064e	cb 01 46 c8 cf 60 eb 4e	..F..`.N
	defb 0x23,0x46,0x23,0x22,0x74,0x5c,0xfe,0x23 ;0656	23 46 23 22 74 5c fe 23	#F#"t\.#
	defb 0xc0,0xe1,0xc5,0xcd,0x38,0x06,0xfe,0xcc ;065e	c0 e1 c5 cd 38 06 fe cc	....8...
	defb 0xc2,0x9d,0x09,0xe7,0xcd,0xa3,0x05,0xfd ;0666	c2 9d 09 e7 cd a3 05 fd	........
	defb 0xcb,0x01,0x76,0xca,0x9d,0x09,0xc1,0xcd ;066e	cb 01 76 ca 9d 09 c1 cd	..v.....
	defb 0x02,0x09,0xc5,0xcd,0xfc,0x37,0xef,0x01 ;0676	02 09 c5 cd fc 37 ef 01	.....7..
	defb 0x16,0xc9,0xcd,0xc5                     ;067e	16 c9 cd c5	....
	ld h,$a0                ;0682	26 a0
	add hl,hl               ;0684	29
	jr c,L068C              ;0685	38 05
	pop hl                  ;0687	e1
	call L0902              ;0688	cd 02 09
	ret                     ;068b	c9

L068C:
	pop hl                  ;068c	e1
	push hl                 ;068d	e5
	ld ($5c5d),hl           ;068e	22 5d 5c

L0691:
	call L05A3              ;0691	cd a3 05
	call L3EC9              ;0694	cd c9 3e
	cp $3d                  ;0697	fe 3d
	jr nz,L06A7             ;0699	20 0c
	pop hl                  ;069b	e1
	rst $20                 ;069c	e7
	ld bc,$1c56             ;069d	01 56 1c
	call L3EC1              ;06a0	cd c1 3e
	call L0902              ;06a3	cd 02 09
	ret                     ;06a6	c9

L06A7:
	cp $2c                  ;06a7	fe 2c
	jr z,L06B9              ;06a9	28 0e
	pop hl                  ;06ab	e1
	call L0874              ;06ac	cd 74 08
	call L0830              ;06af	cd 30 08
	call L07DC              ;06b2	cd dc 07
	call L0902              ;06b5	cd 02 09
	ret                     ;06b8	c9

L06B9:
	ld b,$00                ;06b9	06 00
	jr L06C4                ;06bb	18 07
Text06BD:
	defb 0xd5,0xc5,0xe7,0xcd,0xa3,0x05,0xc1,0xd1 ;06bd	d5 c5 e7 cd a3 05 c1 d1	........
	defb 0x04,0x3a,0x3b,0x5c,0xf5,0xcd,0xc9,0x3e ;06c5	04 3a 3b 5c f5 cd c9 3e	.:;\...>
	defb 0xfe,0x2c,0x28,0xec,0xc5,0xcd,0x74,0x08 ;06cd	fe 2c 28 ec c5 cd 74 08	.,(...t.
	defb 0xf1,0xd5,0x57,0x1e,0x00,0x1c,0xc5,0xd5 ;06d5	f1 d5 57 1e 00 1c c5 d5	..W.....
	defb 0xcd,0x2d,0x0e,0x3a,0x3b,0x5c,0xe6,0xc0 ;06dd	cd 2d 0e 3a 3b 5c e6 c0	.-.:;\..
	defb 0xfe,0x80,0x20,0x35,0x2a,0x65,0x5c,0x2b ;06e5	fe 80 20 35 2a 65 5c 2b	.. 5*e\+
	defb 0x46,0x2b,0x4e,0x78,0xb1,0x28,0x19,0x2b ;06ed	46 2b 4e 78 b1 28 19 2b	F+Nx.(.+
	defb 0x56,0x2b,0x5e,0xed,0x52,0x38,0x08,0x2a ;06f5	56 2b 5e ed 52 38 08 2a	V+^.R8.*
	defb 0x61,0x5c,0x37,0xed,0x52,0x38,0x09,0xd5 ;06fd	61 5c 37 ed 52 38 09 d5	a\7.R8..
	defb 0xef,0x30                               ;0705	ef 30	.0

L0707:
	nop                     ;0707	00
	pop hl                  ;0708	e1
	push de                 ;0709	d5
	ldir                    ;070a	ed b0
	pop de                  ;070c	d1
	ld hl,($5c61)           ;070d	2a 61 5c
	ex de,hl                ;0710	eb
	and a                   ;0711	a7
	sbc hl,de               ;0712	ed 52
	ex de,hl                ;0714	eb
	ld hl,($5c65)           ;0715	2a 65 5c
	dec hl                  ;0718	2b
	dec hl                  ;0719	2b
	dec hl                  ;071a	2b
	ld (hl),d               ;071b	72
	dec hl                  ;071c	2b
	ld (hl),e               ;071d	73
	pop de                  ;071e	d1
	pop bc                  ;071f	c1
	ld a,d                  ;0720	7a
	cp e                    ;0721	bb
	jp c,L0872              ;0722	da 72 08
	call L07C1              ;0725	cd c1 07
	call L3EC9              ;0728	cd c9 3e
	cp $2c                  ;072b	fe 2c
	jr nz,L0732             ;072d	20 03
	rst $20                 ;072f	e7
	jr L06DA                ;0730	18 a8

L0732:
	ld ($5c5f),hl           ;0732	22 5f 5c
	ld b,e                  ;0735	43

L0736:
	ld a,e                  ;0736	7b
	cp d                    ;0737	ba
	jr nc,L0740             ;0738	30 06
	inc e                   ;073a	1c
	call L07C1              ;073b	cd c1 07
	jr L0736                ;073e	18 f6

L0740:
	ld e,b                  ;0740	58
	pop hl                  ;0741	e1
	ld b,d                  ;0742	42
	bit 7,(iy+$01)          ;0743	fd cb 01 7e
	jr z,L07AF              ;0747	28 66
	ld ($5c5d),hl           ;0749	22 5d 5c
	ld b,$01                ;074c	06 01

L074E:
	push de                 ;074e	d5
	push bc                 ;074f	c5
	call L05A3              ;0750	cd a3 05
	pop bc                  ;0753	c1
	push bc                 ;0754	c5
	ld a,c                  ;0755	79
	and $3f                 ;0756	e6 3f
	cp $38                  ;0758	fe 38
	jr nz,L07BC             ;075a	20 60
	bit 6,(iy+$01)          ;075c	fd cb 01 76
	call z,L086F            ;0760	cc 6f 08

L0763:
	exx                     ;0763	d9
	pop bc                  ;0764	c1
	pop de                  ;0765	d1
	push de                 ;0766	d5
	push bc                 ;0767	c5
	ld a,e                  ;0768	7b
	cp b                    ;0769	b8
	jr c,L076D              ;076a	38 01
	ld a,b                  ;076c	78

L076D:
	ld d,a                  ;076d	57
	dec d                   ;076e	15
	ld e,$05                ;076f	1e 05
	mul d,e                 ;0771	ed 30
	ld hl,($5c63)           ;0773	2a 63 5c
	add hl,de               ;0776	19
	ld a,(hl)               ;0777	7e
	inc hl                  ;0778	23
	ld e,(hl)               ;0779	5e
	inc hl                  ;077a	23
	ld d,(hl)               ;077b	56
	inc hl                  ;077c	23
	ld c,(hl)               ;077d	4e
	inc hl                  ;077e	23
	ld b,(hl)               ;077f	46
	exx                     ;0780	d9
	bit 6,c                 ;0781	cb 71
	exx                     ;0783	d9
	jr nz,L078B             ;0784	20 05
	ld hl,($5c61)           ;0786	2a 61 5c
	add hl,de               ;0789	19
	ex de,hl                ;078a	eb

L078B:
	call L3845              ;078b	cd 45 38
	exx                     ;078e	d9
	ld a,c                  ;078f	79
	exx                     ;0790	d9
	ld e,a                  ;0791	5f
	and $3f                 ;0792	e6 3f
	cp $38                  ;0794	fe 38
	jr z,L07A1              ;0796	28 09
	ld hl,($5c3b)           ;0798	2a 3b 5c
	ld b,l                  ;079b	45
	call L07E9              ;079c	cd e9 07
	jr L07A7                ;079f	18 06

L07A1:
	ld a,($5c3b)            ;07a1	3a 3b 5c
	call L0829              ;07a4	cd 29 08

L07A7:
	pop bc                  ;07a7	c1
	pop de                  ;07a8	d1
	rst $20                 ;07a9	e7
	inc b                   ;07aa	04
	dec d                   ;07ab	15
	jr nz,L074E             ;07ac	20 a0
	dec b                   ;07ae	05

L07AF:
	pop hl                  ;07af	e1
	djnz L07AF              ;07b0	10 fd
	ld hl,($5c5f)           ;07b2	2a 5f 5c
	ld ($5c5d),hl           ;07b5	22 5d 5c
	call L0902              ;07b8	cd 02 09
	ret                     ;07bb	c9

L07BC:
	call L0830              ;07bc	cd 30 08
	jr L0763                ;07bf	18 a2

L07C1:
	ld a,d                  ;07c1	7a
	sub e                   ;07c2	93
	inc a                   ;07c3	3c
	ld h,$00                ;07c4	26 00
	ld l,a                  ;07c6	6f
	inc hl                  ;07c7	23
	add hl,hl               ;07c8	29
	add hl,sp               ;07c9	39
	inc hl                  ;07ca	23
	bit 6,(hl)              ;07cb	cb 76
	ld l,c                  ;07cd	69
	call z,L0862            ;07ce	cc 62 08
	ld a,($5c3b)            ;07d1	3a 3b 5c
	xor c                   ;07d4	a9
	ld c,l                  ;07d5	4d
	and $40                 ;07d6	e6 40
	ret z                   ;07d8	c8

L07D9:
	jp L0872                ;07d9	c3 72 08

L07DC:
	ld a,($5c3b)            ;07dc	3a 3b 5c
	push af                 ;07df	f5
	push bc                 ;07e0	c5
	call L0E2D              ;07e1	cd 2d 0e
	pop de                  ;07e4	d1
	pop bc                  ;07e5	c1
	ld a,e                  ;07e6	7b
	and $3f                 ;07e7	e6 3f

L07E9:
	bit 2,(iy+$30)          ;07e9	fd cb 30 56
	jr z,L0822              ;07ed	28 33
	bit 6,(iy+$01)          ;07ef	fd cb 01 76
	jr z,L07D9              ;07f3	28 e4
	bit 7,(iy+$01)          ;07f5	fd cb 01 7e
	ret z                   ;07f9	c8
	push af                 ;07fa	f5
	ld hl,($5c4d)           ;07fb	2a 4d 5c
	call L26C5              ;07fe	cd c5 26
	ld d,a                  ;0801	57
	jr z,L07D9              ;0802	28 d5
	call L26C5              ;0804	cd c5 26
	pop af                  ;0807	f1
	inc h                   ;0808	24
	pop bc                  ;0809	c1
	pop af                  ;080a	f1
	cp $0f                  ;080b	fe 0f
	jr z,L0816              ;080d	28 07
	call L26C5              ;080f	cd c5 26
	jp z,L1825              ;0812	ca 25 18
	inc bc                  ;0815	03

L0816:
	ex de,hl                ;0816	eb
	add hl,bc               ;0817	09
	ex de,hl                ;0818	eb
	ld hl,($5c4d)           ;0819	2a 4d 5c
	call L26C5              ;081c	cd c5 26
	ld c,h                  ;081f	4c
	jr z,L07EB              ;0820	28 c9

L0822:
	push bc                 ;0822	c5
	ld b,$00                ;0823	06 00
	rst $28                 ;0825	ef
	adc $27                 ;0826	ce 27
	pop af                  ;0828	f1

L0829:
	ld bc,$1c5e             ;0829	01 5e 1c
	call L3EC1              ;082c	cd c1 3e
	ret                     ;082f	c9

L0830:
	bit 2,(iy+$30)          ;0830	fd cb 30 56
	ret nz                  ;0834	c0
	bit 1,(iy+$37)          ;0835	fd cb 37 4e
	jp nz,L1FCD             ;0839	c2 cd 1f
	ld hl,($5c4d)           ;083c	2a 4d 5c
	bit 6,(iy+$01)          ;083f	fd cb 01 76
	jr z,L0854              ;0843	28 0f
	inc hl                  ;0845	23
	bit 7,(iy+$01)          ;0846	fd cb 01 7e
	ret z                   ;084a	c8
	push bc                 ;084b	c5
	ld bc,$33b4             ;084c	01 b4 33
	call L3EC1              ;084f	cd c1 3e
	pop bc                  ;0852	c1
	ret                     ;0853	c9

L0854:
	ex de,hl                ;0854	eb
	push bc                 ;0855	c5
	ld bc,($5c72)           ;0856	ed 4b 72 5c
	bit 7,(iy+$01)          ;085a	fd cb 01 7e
	call nz,L3837           ;085e	c4 37 38
	pop bc                  ;0861	c1

L0862:
	ld a,c                  ;0862	79
	cp $cf                  ;0863	fe cf
	ld c,$17                ;0865	0e 17
	ret z                   ;0867	c8
	cp $c4                  ;0868	fe c4
	ld c,$7b                ;086a	0e 7b
	ret z                   ;086c	c8
	cp $f8                  ;086d	fe f8

L086F:
	ld c,$38                ;086f	0e 38
	ret z                   ;0871	c8

L0872:
	rst $08                 ;0872	cf
	dec bc                  ;0873	0b

L0874:
	ld hl,$08a6             ;0874	21 a6 08
	ld bc,$000b             ;0877	01 0b 00
	cpir                    ;087a	ed b1
	jr nz,L0872             ;087c	20 f4
	jp pe,L0896             ;087e	ea 96 08
	push hl                 ;0881	e5
	call L3EC9              ;0882	cd c9 3e
	push hl                 ;0885	e5
	rst $20                 ;0886	e7
	pop hl                  ;0887	e1
	cp $7c                  ;0888	fe 7c
	jr nz,L0891             ;088a	20 05
	ld c,$ff                ;088c	0e ff
	pop hl                  ;088e	e1
	jr L089B                ;088f	18 0a

L0891:
	ld ($5c5d),hl           ;0891	22 5d 5c
	ld a,(hl)               ;0894	7e
	pop hl                  ;0895	e1

L0896:
	add hl,$000a            ;0896	ed 34 0a 00
	ld c,(hl)               ;089a	4e

L089B:
	cp $3d                  ;089b	fe 3d
	jr z,L08A4              ;089d	28 05
	rst $20                 ;089f	e7
	cp $3d                  ;08a0	fe 3d
	jr nz,L0872             ;08a2	20 ce

L08A4:
	rst $20                 ;08a4	e7
	ret                     ;08a5	c9
Text08A6:
	defb 0x2b,0x2d,0x2a,0x2f,0x26,0x7c           ;08a6	2b 2d 2a 2f 26 7c	+-*/&|
	adc h                   ;08ac	8c
	adc l                   ;08ad	8d
	adc e                   ;08ae	8b
	dec a                   ;08af	3d
	ld e,(hl)               ;08b0	5e
	rst $08                 ;08b1	cf
	jp $c5c4                ;08b2	c3 c4 c5
	cp $c1                  ;08b5	fd fe c1
	jp nz,$f8fc             ;08b8	c2 fc f8
	add $eb                 ;08bb	c6 eb
	ld l,(hl)               ;08bd	6e
	ld h,$04                ;08be	26 04
	ld c,(hl)               ;08c0	4e
	ld b,$00                ;08c1	06 00
	ld e,c                  ;08c3	59
	ld d,b                  ;08c4	50
	inc hl                  ;08c5	23
	cpir                    ;08c6	ed b1
	jr nz,L08CB             ;08c8	20 01
	add hl,de               ;08ca	19

L08CB:
	ld e,(hl)               ;08cb	5e
	add hl,de               ;08cc	19
	ld ($5c74),hl           ;08cd	22 74 5c
	ld ($5b5e),a            ;08d0	32 5e 5b
	ret nz                  ;08d3	c0
	add a                   ;08d4	87
	ret nc                  ;08d5	d0
	jp L0020                ;08d6	c3 20 00

L08D9:
	cp $29                  ;08d9	fe 29
	ret z                   ;08db	c8

; tentative entry (word / jump table scan)
T08DC:
	cp $0d                  ;08dc	fe 0d
	ret z                   ;08de	c8
	cp $3a                  ;08df	fe 3a
	ret                     ;08e1	c9

L08E2:
	pop hl                  ;08e2	e1
	ld e,(hl)               ;08e3	5e
	bit 7,e                 ;08e4	cb 7b
	jr z,L08F3              ;08e6	28 0b
	cp $0a                  ;08e8	fe 0a
	jr nc,L08F0             ;08ea	30 04
	add $3d                 ;08ec	c6 3d
	jr L08F2                ;08ee	18 02

L08F0:
	add $18                 ;08f0	c6 18

L08F2:
	ld e,a                  ;08f2	5f

L08F3:
	ld h,e                  ;08f3	63
	ld l,$0d                ;08f4	2e 0d
	push hl                 ;08f6	e5
	ld l,$cd                ;08f7	2e cd
	ld h,$d4                ;08f9	26 d4
	push hl                 ;08fb	e5
	xor a                   ;08fc	af
	ld hl,$0000             ;08fd	21 00 00
	add hl,sp               ;0900	39
	jp (hl)                 ;0901	e9

L0902:
	bit 7,(iy+$01)          ;0902	fd cb 01 7e
	ret nz                  ;0906	c0
	pop bc                  ;0907	c1
	pop bc                  ;0908	c1
	jr L0944                ;0909	18 39

L090B:
	res 7,(iy+$01)          ;090b	fd cb 01 be
	rst $28                 ;090f	ef
	ei                      ;0910	fb
	add hl,de               ;0911	19
	xor a                   ;0912	af
	ld ($5c47),a            ;0913	32 47 5c
	dec a                   ;0916	3d
	ld ($5c3a),a            ;0917	32 3a 5c

L091A:
	call L3EC9              ;091a	cd c9 3e
	jr L0950                ;091d	18 31
L091F:
	defb 0xcd                                    ;091f	cd	.
	ld d,a                  ;0920	57
	dec hl                  ;0921	2b
	call L3EC9              ;0922	cd c9 3e
	jr L095B                ;0925	18 34
Data0927:
	defb 0xc1                                    ;0927	c1	.

L0928:
	bit 7,(iy+$01)          ;0928	fd cb 01 7e
	ret z                   ;092c	c8

L092D:
	ld hl,($5c55)           ;092d	2a 55 5c
	call L3975              ;0930	cd 75 39
	ret nc                  ;0933	d0

L0934:
	ld ($5c55),hl           ;0934	22 55 5c
	ex de,hl                ;0937	eb
	ld ($5c5d),hl           ;0938	22 5d 5c
	ld (iy+$0d),$00         ;093b	fd 36 0d 00
	jr L094F                ;093f	18 0e

L0941:
	call L2B57              ;0941	cd 57 2b

L0944:
	call L3EC9              ;0944	cd c9 3e
	cp $0d                  ;0947	fe 0d
	jr z,L0928              ;0949	28 dd
	cp $3a                  ;094b	fe 3a
	jr nz,L099D             ;094d	20 4e

L094F:
	rst $20                 ;094f	e7

L0950:
	ld hl,$5c47             ;0950	21 47 5c
	bit 7,(hl)              ;0953	cb 7e
	jr nz,L0928             ;0955	20 d1
	inc (hl)                ;0957	34
	jp m,L099D              ;0958	fa 9d 09

L095B:
	ld hl,($5c61)           ;095b	2a 61 5c
	ld ($5c63),hl           ;095e	22 63 5c
	ld ($5c65),hl           ;0961	22 65 5c
	ld hl,$5c92             ;0964	21 92 5c
	ld ($5c68),hl           ;0967	22 68 5c
	cp $0d                  ;096a	fe 0d
	jr z,L0928              ;096c	28 ba
	ld l,a                  ;096e	6f
	ld ixh,a                ;096f	dd 67
	ld h,$02                ;0971	26 02
	ld l,(hl)               ;0973	6e
	inc h                   ;0974	24
	ld de,$0941             ;0975	11 41 09
	push de                 ;0978	d5
	ex de,hl                ;0979	eb
	rst $20                 ;097a	e7
	ex de,hl                ;097b	eb
	jr L0981                ;097c	18 03
Data097E:
	defb 0x2a,0x74,0x5c                          ;097e	2a 74 5c	*t\

L0981:
	ld a,(hl)               ;0981	7e
	inc hl                  ;0982	23
	ld ($5c74),hl           ;0983	22 74 5c
	ld de,$097e             ;0986	11 7e 09
	push de                 ;0989	d5
	cp $20                  ;098a	fe 20
	jr nc,L099F             ;098c	30 11
	ex de,hl                ;098e	eb
	ld hl,$055b             ;098f	21 5b 05
	add hl,a                ;0992	ed 31
	ld a,(hl)               ;0994	7e
	add hl,a                ;0995	ed 31
	push hl                 ;0997	e5
	call L3EC9              ;0998	cd c9 3e
	and a                   ;099b	a7
	ret                     ;099c	c9

L099D:
	rst $08                 ;099d	cf
	dec bc                  ;099e	0b

L099F:
	ld c,a                  ;099f	4f
	call L3EC9              ;09a0	cd c9 3e
	cp c                    ;09a3	b9
	jr nz,L099D             ;09a4	20 f7
	rst $20                 ;09a6	e7
	ret                     ;09a7	c9

L09A8:
	ld a,b                  ;09a8	78
	cpl                     ;09a9	2f
	ld h,a                  ;09aa	67
	ld a,c                  ;09ab	79
	cpl                     ;09ac	2f
	ld l,a                  ;09ad	6f
	inc hl                  ;09ae	23
	add hl,sp               ;09af	39
	ld sp,hl                ;09b0	f9
	push bc                 ;09b1	c5
	push hl                 ;09b2	e5
	ex de,hl                ;09b3	eb
	ldir                    ;09b4	ed b0
	pop hl                  ;09b6	e1
	rst $28                 ;09b7	ef
	ld c,$25                ;09b8	0e 25
	pop hl                  ;09ba	e1
	add hl,sp               ;09bb	39
	ld sp,hl                ;09bc	f9
	ret                     ;09bd	c9

L09BE:
	ld hl,($5c59)           ;09be	2a 59 5c
	ld ($5c5d),hl           ;09c1	22 5d 5c
	call L3EC9              ;09c4	cd c9 3e
	ret                     ;09c7	c9

L09C8:
	call L09BE              ;09c8	cd be 09
	cp $2e                  ;09cb	fe 2e
	ret z                   ;09cd	c8
	cp $f1                  ;09ce	fe f1
	jr z,L09DC              ;09d0	28 0a
	cp $ce                  ;09d2	fe ce
	ret nc                  ;09d4	d0
	cp $a5                  ;09d5	fe a5
	jr nc,L09DC             ;09d7	30 03
	cp $8f                  ;09d9	fe 8f
	ret nc                  ;09db	d0

L09DC:
	ld b,a                  ;09dc	47

L09DD:
	rst $20                 ;09dd	e7
	cp $3a                  ;09de	fe 3a
	ret z                   ;09e0	c8
	cp $0d                  ;09e1	fe 0d
	jr nz,L09DD             ;09e3	20 f8
	ld a,b                  ;09e5	78
	cp $f1                  ;09e6	fe f1
	scf                     ;09e8	37
	ret                     ;09e9	c9

L09EA:
	rst $08                 ;09ea	cf
	add hl,de               ;09eb	19
	ld (iy+$31),$02         ;09ec	fd 36 31 02
	ld hl,$0afd             ;09f0	21 fd 0a
	ld ($5b6c),hl           ;09f3	22 6c 5b
	ld (iy+$00),$ff         ;09f6	fd 36 00 ff
	ld hl,$5b3a             ;09fa	21 3a 5b
	push hl                 ;09fd	e5
	ld ($5c3d),sp           ;09fe	ed 73 3d 5c
	call L09BE              ;0a02	cd be 09
	rst $18                 ;0a05	df
	ld a,($d5b8)            ;0a06	3a b8 d5
	rst $30                 ;0a09	f7
	cp $04                  ;0a0a	fe 04
	jp nz,L090B             ;0a0c	c2 0b 09

L0A0F:
	call L09C8              ;0a0f	cd c8 09
	jr nc,L09EA             ;0a12	30 d6
	jp z,L090B              ;0a14	ca 0b 09
	ld hl,$fffe             ;0a17	21 fe ff
	ld ($5c45),hl           ;0a1a	22 45 5c
	res 7,(iy+$01)          ;0a1d	fd cb 01 be
	call L09BE              ;0a21	cd be 09
	call L0E2D              ;0a24	cd 2d 0e
	bit 6,(iy+$01)          ;0a27	fd cb 01 76
	jr z,L09EA              ;0a2b	28 bd
	cp $0d                  ;0a2d	fe 0d
	jr nz,L09EA             ;0a2f	20 b9
	ld a,$0d                ;0a31	3e 0d
	call L0080              ;0a33	cd 80 00
	ret m                   ;0a36	f8
	inc bc                  ;0a37	03
	set 7,(iy+$01)          ;0a38	fd cb 01 fe
	call L09BE              ;0a3c	cd be 09
	ld hl,$0bae             ;0a3f	21 ae 0b
	ld ($5b6c),hl           ;0a42	22 6c 5b
	call L0E2D              ;0a45	cd 2d 0e
	bit 6,(iy+$01)          ;0a48	fd cb 01 76
	jr z,L09EA              ;0a4c	28 9c
	ld de,$5b6e             ;0a4e	11 6e 5b
	ld hl,($5c65)           ;0a51	2a 65 5c
	ld bc,$0005             ;0a54	01 05 00
	or a                    ;0a57	b7
	sbc hl,bc               ;0a58	ed 42
	ldir                    ;0a5a	ed b0
	ld bc,$0001             ;0a5c	01 01 00
	rst $28                 ;0a5f	ef
	jr nc,L0A62             ;0a60	30 00

L0A62:
	ld ($5c5b),hl           ;0a62	22 5b 5c
	push hl                 ;0a65	e5
	ld hl,($5c51)           ;0a66	2a 51 5c
	push hl                 ;0a69	e5
	ld a,$ff                ;0a6a	3e ff
	rst $28                 ;0a6c	ef
	ld bc,$ef16             ;0a6d	01 16 ef
	ex (sp),hl              ;0a70	e3
	dec l                   ;0a71	2d
	pop hl                  ;0a72	e1
	rst $28                 ;0a73	ef
	dec d                   ;0a74	15
	ld d,$d1                ;0a75	16 d1
	ld hl,($5c5b)           ;0a77	2a 5b 5c
	and a                   ;0a7a	a7
	sbc hl,de               ;0a7b	ed 52

L0A7D:
	ld a,(de)               ;0a7d	1a
	call L0080              ;0a7e	cd 80 00
	ret m                   ;0a81	f8
	inc bc                  ;0a82	03
	inc de                  ;0a83	13
	dec hl                  ;0a84	2b
	ld a,h                  ;0a85	7c
	or l                    ;0a86	b5
	jr nz,L0A7D             ;0a87	20 f4

L0A89:
	ld a,$0d                ;0a89	3e 0d
	rst $28                 ;0a8b	ef
	djnz L0A8E              ;0a8c	10 00

L0A8E:
	ret                     ;0a8e	c9

; tentative entry (word / jump table scan)
T0A8F:
	rst $18                 ;0a8f	df
	ld b,$00                ;0a90	06 00
	rst $00                 ;0a92	c7
	add hl,bc               ;0a93	09
	ld bc,$0538             ;0a94	01 38 05
	ld b,$00                ;0a97	06 00
	rst $00                 ;0a99	c7
	inc c                   ;0a9a	0c
	ld bc,$c9f7             ;0a9b	01 f7 c9
	call T0A8F              ;0a9e	cd 8f 0a
	call L0AA8              ;0aa1	cd a8 0a
	ld hl,($5b6c)           ;0aa4	2a 6c 5b

L0AA7:
	jp (hl)                 ;0aa7	e9

L0AA8:
	xor a                   ;0aa8	af

L0AA9:
	push af                 ;0aa9	f5
	rst $00                 ;0aaa	c7
	ld e,c                  ;0aab	59
	nop                     ;0aac	00
	pop af                  ;0aad	f1
	inc a                   ;0aae	3c
	cp $03                  ;0aaf	fe 03
	jr c,L0AA9              ;0ab1	38 f6
	ret                     ;0ab3	c9
Text0AB4:
	defb 0xed,0x91,0x51,0x10,0x2a,0x6a,0x5b,0xe5 ;0ab4	ed 91 51 10 2a 6a 5b e5	..Q.*j[.
	defb 0x2a,0x91,0x23,0xe5,0xed,0x4b,0x93,0x23 ;0abc	2a 91 23 e5 ed 4b 93 23	*.#..K.#
	defb 0xc5,0x2a,0xb2,0x5c,0x23,0xa7,0xed,0x72 ;0ac4	c5 2a b2 5c 23 a7 ed 72	.*.\#..r
	defb 0x22,0x91,0x23                          ;0acc	22 91 23	".#
	push hl                 ;0acf	e5
	push bc                 ;0ad0	c5
	add hl,bc               ;0ad1	09
	ld bc,$2392             ;0ad2	01 92 23
	sbc hl,bc               ;0ad5	ed 42
	pop hl                  ;0ad7	e1
	pop bc                  ;0ad8	c1
	jr nc,L0AF6             ;0ad9	30 1b
	push de                 ;0adb	d5
	ex de,hl                ;0adc	eb
	ld hl,$0002             ;0add	21 02 00
	add hl,sp               ;0ae0	39
	ldir                    ;0ae1	ed b0
	ld ($2393),de           ;0ae3	ed 53 93 23
	pop de                  ;0ae7	d1
	ld sp,hl                ;0ae8	f9
	nextreg $51,$ff         ;0ae9	ed 91 51 ff
	ld b,$3e                ;0aed	06 3e
	push bc                 ;0aef	c5
	ld ($5b58),sp           ;0af0	ed 73 58 5b
	jr L0B67                ;0af4	18 71

L0AF6:
	ld (iy+$00),$03         ;0af6	fd 36 00 03
	jp L0C54                ;0afa	c3 54 0c
Data0AFD:
	defb 0xfd                                    ;0afd	fd	.
	rlc b                   ;0afe	cb 00
	ld a,(hl)               ;0b00	7e
	ret z                   ;0b01	c8
	rst $28                 ;0b02	ef
	ei                      ;0b03	fb
	add hl,de               ;0b04	19
	ld a,b                  ;0b05	78
	or c                    ;0b06	b1
	jp nz,L0D55             ;0b07	c2 55 0d
	rst $18                 ;0b0a	df
	ld hl,$0000             ;0b0b	21 00 00
	ld ($d750),hl           ;0b0e	22 50 d7
	rst $30                 ;0b11	f7
	call L3EC9              ;0b12	cd c9 3e
	cp $0d                  ;0b15	fe 0d
	ret z                   ;0b17	c8
	pop hl                  ;0b18	e1
	pop hl                  ;0b19	e1
	pop hl                  ;0b1a	e1
	bit 6,(iy+$02)          ;0b1b	fd cb 02 76
	jr nz,L0B26             ;0b1f	20 05
	call L3E80              ;0b21	cd 80 3e
	ld d,h                  ;0b24	54
	add hl,bc               ;0b25	09

L0B26:
	res 6,(iy+$02)          ;0b26	fd cb 02 b6
	rst $18                 ;0b2a	df
	ld a,($d5b8)            ;0b2b	3a b8 d5
	cp $08                  ;0b2e	fe 08
	jr nz,L0B4E             ;0b30	20 1c
	ld a,($5b7b)            ;0b32	3a 7b 5b
	ld ($d5ba),a            ;0b35	32 ba d5
	ld a,($5c7f)            ;0b38	3a 7f 5c
	ld ($d5b9),a            ;0b3b	32 b9 d5
	bit 3,a                 ;0b3e	cb 5f
	jr z,L0B4E              ;0b40	28 0c
	ld ix,$fb00             ;0b42	dd 21 00 fb
	ld hl,$0048             ;0b46	21 48 00
	call L3E80              ;0b49	cd 80 3e
	ld c,a                  ;0b4c	4f
	daa                     ;0b4d	27

L0B4E:
	ld hl,($d5b7)           ;0b4e	2a b7 d5
	ld a,l                  ;0b51	7d
	and $40                 ;0b52	e6 40
	or h                    ;0b54	b4
	push af                 ;0b55	f5
	call z,L36A2            ;0b56	cc a2 36
	call L3E80              ;0b59	cd 80 3e
	ld l,b                  ;0b5c	68
	add hl,bc               ;0b5d	09
	pop af                  ;0b5e	f1
	call z,L36A2            ;0b5f	cc a2 36
	rst $30                 ;0b62	f7
	ld de,($5c59)           ;0b63	ed 5b 59 5c

L0B67:
	ld hl,$0bae             ;0b67	21 ae 0b
	ld ($5b6c),hl           ;0b6a	22 6c 5b
	push de                 ;0b6d	d5
	call L1090              ;0b6e	cd 90 10
	pop de                  ;0b71	d1
	ld hl,$5b3a             ;0b72	21 3a 5b
	push hl                 ;0b75	e5
	ld ($5c3d),sp           ;0b76	ed 73 3d 5c
	push de                 ;0b7a	d5
	call T0068              ;0b7b	cd 68 00
	sub (hl)                ;0b7e	96
	scf                     ;0b7f	37
	pop de                  ;0b80	d1
	ld hl,$5c3c             ;0b81	21 3c 5c
	res 3,(hl)              ;0b84	cb 9e
	ld a,$19                ;0b86	3e 19
	sub (iy+$4f)            ;0b88	fd 96 4f
	ld ($5c8c),a            ;0b8b	32 8c 5c
	set 7,(iy+$01)          ;0b8e	fd cb 01 fe
	dec de                  ;0b92	1b
	ld hl,$fffe             ;0b93	21 fe ff
	ld ($5c45),hl           ;0b96	22 45 5c
	ld hl,($5c61)           ;0b99	2a 61 5c
	dec hl                  ;0b9c	2b
	jp L0934                ;0b9d	c3 34 09
	rst $18                 ;0ba0	df
	call L3E80              ;0ba1	cd 80 3e
	ld l,b                  ;0ba4	68
	add hl,bc               ;0ba5	09
	rst $30                 ;0ba6	f7
	ld (iy+$00),$0f         ;0ba7	fd 36 00 0f
	jp L0C2F                ;0bab	c3 2f 0c
	call L1DCD              ;0bae	cd cd 1d
	ld hl,$0000             ;0bb1	21 00 00
	ld (iy+$37),h           ;0bb4	fd 74 37
	ld (iy+$26),h           ;0bb7	fd 74 26
	ld ($5c0b),hl           ;0bba	22 0b 5c
	ld hl,$5c47             ;0bbd	21 47 5c
	ld a,(hl)               ;0bc0	7e
	push af                 ;0bc1	f5
	ld a,($5c3a)            ;0bc2	3a 3a 5c
	inc a                   ;0bc5	3c
	jr z,L0BE6              ;0bc6	28 1e
	ex af,af'               ;0bc8	08
	call L38BC              ;0bc9	cd bc 38
	jr z,L0BD4              ;0bcc	28 06
	ld a,($5b77)            ;0bce	3a 77 5b
	ld ($5eba),a            ;0bd1	32 ba 5e

L0BD4:
	ex af,af'               ;0bd4	08
	cp $09                  ;0bd5	fe 09
	jr z,L0BDD              ;0bd7	28 04
	cp $15                  ;0bd9	fe 15
	jr nz,L0BDE             ;0bdb	20 01

L0BDD:
	inc (hl)                ;0bdd	34

L0BDE:
	ld bc,$0003             ;0bde	01 03 00
	ld de,$5c70             ;0be1	11 70 5c
	lddr                    ;0be4	ed b8

L0BE6:
	pop bc                  ;0be6	c1
	ld (iy+$0d),b           ;0be7	fd 70 0d
	and a                   ;0bea	a7
	jr z,L0C2F              ;0beb	28 42
	ld hl,$3140             ;0bed	21 40 31
	ld e,a                  ;0bf0	5f
	call L3EB7              ;0bf1	cd b7 3e
	ld hl,$3142             ;0bf4	21 42 31
	ld de,($5c45)           ;0bf7	ed 5b 45 5c
	call L3EB9              ;0bfb	cd b9 3e
	ld hl,$3144             ;0bfe	21 44 31

; tentative entry (word / jump table scan)
T0C01:
	ld de,($5c47)           ;0c01	ed 5b 47 5c
	res 7,e                 ;0c05	cb bb
	call L3EB7              ;0c07	cd b7 3e
	ld hl,$3146             ;0c0a	21 46 31
	ld de,($5b77)           ;0c0d	ed 5b 77 5b
	call L3EB7              ;0c11	cd b7 3e
	ld hl,$5b3a             ;0c14	21 3a 5b
	push hl                 ;0c17	e5
	ld hl,$091a             ;0c18	21 1a 09
	push hl                 ;0c1b	e5
	ld c,$4b                ;0c1c	0e 4b
	call L3A66              ;0c1e	cd 66 3a
	jr c,L0C2D              ;0c21	38 0a
	ld a,$4b                ;0c23	3e 4b
	call L3A20              ;0c25	cd 20 3a
	ld (iy+$00),$ff         ;0c28	fd 36 00 ff
	ret                     ;0c2c	c9

L0C2D:
	pop hl                  ;0c2d	e1
	pop hl                  ;0c2e	e1

L0C2F:
	call T0068              ;0c2f	cd 68 00
	ld (iy+$3a),$68         ;0c32	fd 36 3a 68
	ld e,e                  ;0c36	5b
	bit 0,a                 ;0c37	cb 47
	jr z,L0C65              ;0c39	28 2a
	pop bc                  ;0c3b	c1
	nextreg $51,$10         ;0c3c	ed 91 51 10
	ld hl,($5cb2)           ;0c40	2a b2 5c
	inc hl                  ;0c43	23
	ld bc,($2391)           ;0c44	ed 4b 91 23
	and a                   ;0c48	a7
	sbc hl,bc               ;0c49	ed 42
	ld sp,hl                ;0c4b	f9
	ex de,hl                ;0c4c	eb
	ld hl,($2393)           ;0c4d	2a 93 23
	sbc hl,bc               ;0c50	ed 42
	ldir                    ;0c52	ed b0

L0C54:
	pop hl                  ;0c54	e1
	ld ($2393),hl           ;0c55	22 93 23
	pop hl                  ;0c58	e1
	ld ($2391),hl           ;0c59	22 91 23
	pop hl                  ;0c5c	e1
	ld ($5b6a),hl           ;0c5d	22 6a 5b
	nextreg $51,$ff         ;0c60	ed 91 51 ff
	ret                     ;0c64	c9

L0C65:
	res 5,(iy+$01)          ;0c65	fd cb 01 ae
	ld a,($5c3a)            ;0c69	3a 3a 5c
	inc a                   ;0c6c	3c
	push af                 ;0c6d	f5
	ld hl,($5c61)           ;0c6e	2a 61 5c
	push hl                 ;0c71	e5
	rst $18                 ;0c72	df
	ld a,($d5b8)            ;0c73	3a b8 d5
	rst $30                 ;0c76	f7
	cp $08                  ;0c77	fe 08
	jr nz,L0C95             ;0c79	20 1a
	ld a,($5c7f)            ;0c7b	3a 7f 5c
	and $0f                 ;0c7e	e6 0f
	cp $09                  ;0c80	fe 09
	jr nz,L0C92             ;0c82	20 0e
	ld ix,$fb00             ;0c84	dd 21 00 fb
	ld hl,$004c             ;0c88	21 4c 00
	call L3E80              ;0c8b	cd 80 3e
	ld c,e                  ;0c8e	4b
	daa                     ;0c8f	27
	jr L0C95                ;0c90	18 03

L0C92:
	call L0A89              ;0c92	cd 89 0a

L0C95:
	rst $28                 ;0c95	ef
	or b                    ;0c96	b0
	ld d,$cd                ;0c97	16 cd
	add b                   ;0c99	80
	ld a,$54                ;0c9a	3e 54
	add hl,bc               ;0c9c	09
	set 5,(iy+$02)          ;0c9d	fd cb 02 ee
	ld a,$fd                ;0ca1	3e fd
	call L13AF              ;0ca3	cd af 13
	pop de                  ;0ca6	d1
	pop af                  ;0ca7	f1
	ld b,a                  ;0ca8	47
	cp $ff                  ;0ca9	fe ff
	jr nz,L0CB2             ;0cab	20 05
	call L37AF              ;0cad	cd af 37
	jr L0CDB                ;0cb0	18 29

L0CB2:
	and a                   ;0cb2	a7
	jr nz,L0CBD             ;0cb3	20 08
	rst $18                 ;0cb5	df
	ld a,($d5b8)            ;0cb6	3a b8 d5
	rst $30                 ;0cb9	f7
	and a                   ;0cba	a7
	jr nz,L0D29             ;0cbb	20 6c

L0CBD:
	cp $0a                  ;0cbd	fe 0a
	jr c,L0CCF              ;0cbf	38 0e
	cp $1d                  ;0cc1	fe 1d
	jr c,L0CCD              ;0cc3	38 08
	cp $2c                  ;0cc5	fe 2c
	jr nc,L0CD7             ;0cc7	30 0e
	add $14                 ;0cc9	c6 14
	jr L0CCF                ;0ccb	18 02

L0CCD:
	add $07                 ;0ccd	c6 07

L0CCF:
	rst $28                 ;0ccf	ef
	rst $28                 ;0cd0	ef
	dec d                   ;0cd1	15
	ld a,$20                ;0cd2	3e 20
	rst $28                 ;0cd4	ef
	djnz L0CD7              ;0cd5	10 00

L0CD7:
	ld a,b                  ;0cd7	78
	call T0D34              ;0cd8	cd 34 0d

L0CDB:
	xor a                   ;0cdb	af
	ld de,$1536             ;0cdc	11 36 15
	rst $28                 ;0cdf	ef
	ld a,(bc)               ;0ce0	0a
	inc c                   ;0ce1	0c
	call L38BC              ;0ce2	cd bc 38
	jr z,L0CFB              ;0ce5	28 14
	ld a,($5b77)            ;0ce7	3a 77 5b
	inc a                   ;0cea	3c
	jr z,L0CFB              ;0ceb	28 0e
	dec a                   ;0ced	3d
	ld c,a                  ;0cee	4f
	ld b,$00                ;0cef	06 00
	rst $28                 ;0cf1	ef
	dec de                  ;0cf2	1b
	ld a,(de)               ;0cf3	1a
	ld a,$3a                ;0cf4	3e 3a
	rst $28                 ;0cf6	ef
	djnz L0CF9              ;0cf7	10 00

L0CF9:
	scf                     ;0cf9	37
	sbc a                   ;0cfa	9f

L0CFB:
	ld bc,($5c45)           ;0cfb	ed 4b 45 5c
	jr nz,L0D17             ;0cff	20 16
	bit 7,b                 ;0d01	cb 78
	jr nz,L0D17             ;0d03	20 12
	ld a,($5c3a)            ;0d05	3a 3a 5c
	inc a                   ;0d08	3c
	jr z,L0D17              ;0d09	28 0c
	cp $0d                  ;0d0b	fe 0d
	jr z,L0D17              ;0d0d	28 08
	cp $15                  ;0d0f	fe 15
	jr z,L0D17              ;0d11	28 04
	ld ($5c49),bc           ;0d13	ed 43 49 5c

L0D17:
	rst $28                 ;0d17	ef
	dec de                  ;0d18	1b
	ld a,(de)               ;0d19	1a
	ld a,$3a                ;0d1a	3e 3a
	rst $28                 ;0d1c	ef
	djnz L0D1F              ;0d1d	10 00

L0D1F:
	ld c,(iy+$0d)           ;0d1f	fd 4e 0d
	res 7,c                 ;0d22	cb b9
	ld b,$00                ;0d24	06 00
	rst $28                 ;0d26	ef
	dec de                  ;0d27	1b
	ld a,(de)               ;0d28	1a

L0D29:
	ld hl,$5bff             ;0d29	21 ff 5b
	ld ($5b6a),hl           ;0d2c	22 6a 5b
	call L3E80              ;0d2f	cd 80 3e
	ret po                  ;0d32	e0
	ex af,af'               ;0d33	08

; tentative entry (word / jump table scan)
T0D34:
	cp $1d                  ;0d34	fe 1d
	jr c,L0D4E              ;0d36	38 16
	sub $1d                 ;0d38	d6 1d
	cp $4a                  ;0d3a	fe 4a
	jr c,L0D40              ;0d3c	38 02
	ld a,$4a                ;0d3e	3e 4a

L0D40:
	ld b,$00                ;0d40	06 00
	ld c,a                  ;0d42	4f
	ld hl,$2705             ;0d43	21 05 27
	add hl,bc               ;0d46	09
	add hl,bc               ;0d47	09
	ld e,(hl)               ;0d48	5e
	inc hl                  ;0d49	23
	ld d,(hl)               ;0d4a	56
	jp L37AF                ;0d4b	c3 af 37

L0D4E:
	ld de,$1391             ;0d4e	11 91 13
	rst $28                 ;0d51	ef
	ld a,(bc)               ;0d52	0a
	inc c                   ;0d53	0c
	ret                     ;0d54	c9

L0D55:
	ld ($5c49),bc           ;0d55	ed 43 49 5c
	ld hl,($5c5d)           ;0d59	2a 5d 5c
	ex de,hl                ;0d5c	eb
	ld hl,$0ba0             ;0d5d	21 a0 0b
	ld ($5b6c),hl           ;0d60	22 6c 5b
	ld hl,$5b3a             ;0d63	21 3a 5b
	push hl                 ;0d66	e5
	ld hl,($5c61)           ;0d67	2a 61 5c
	scf                     ;0d6a	37
	sbc hl,de               ;0d6b	ed 52
	rst $18                 ;0d6d	df
	ld de,($d750)           ;0d6e	ed 5b 50 d7
	ld a,d                  ;0d72	7a
	or e                    ;0d73	b3
	jr z,L0D7F              ;0d74	28 09
	ld ($d74e),bc           ;0d76	ed 43 4e d7
	ld a,l                  ;0d7a	7d
	dec a                   ;0d7b	3d
	or h                    ;0d7c	b4
	jr z,L0D95              ;0d7d	28 16

L0D7F:
	rst $30                 ;0d7f	f7
	push hl                 ;0d80	e5
	ld h,b                  ;0d81	60
	ld l,c                  ;0d82	69
	rst $28                 ;0d83	ef
	ld l,(hl)               ;0d84	6e
	add hl,de               ;0d85	19
	jr nz,L0D8E             ;0d86	20 06
	rst $28                 ;0d88	ef
	cp b                    ;0d89	b8
	add hl,de               ;0d8a	19
	rst $28                 ;0d8b	ef
	ret pe                  ;0d8c	e8
	add hl,de               ;0d8d	19

L0D8E:
	pop bc                  ;0d8e	c1
	ld a,c                  ;0d8f	79
	dec a                   ;0d90	3d
	or b                    ;0d91	b0
	jr nz,L0DAA             ;0d92	20 16
	rst $18                 ;0d94	df

L0D95:
	ld h,b                  ;0d95	60
	ld l,c                  ;0d96	69
	sbc hl,de               ;0d97	ed 52
	ld ($d74e),hl           ;0d99	22 4e d7
	ld hl,($5c49)           ;0d9c	2a 49 5c
	call L0080              ;0d9f	cd 80 00
	ld e,b                  ;0da2	58
	inc d                   ;0da3	14
	ld ($5c49),hl           ;0da4	22 49 5c
	rst $30                 ;0da7	f7
	jr L0DD2                ;0da8	18 28

L0DAA:
	push bc                 ;0daa	c5
	inc bc                  ;0dab	03
	inc bc                  ;0dac	03
	inc bc                  ;0dad	03
	inc bc                  ;0dae	03
	dec hl                  ;0daf	2b
	ld de,($5c53)           ;0db0	ed 5b 53 5c
	push de                 ;0db4	d5
	rst $28                 ;0db5	ef
	ld d,l                  ;0db6	55
	ld d,$e1                ;0db7	16 e1
	ld ($5c53),hl           ;0db9	22 53 5c
	pop bc                  ;0dbc	c1
	push bc                 ;0dbd	c5
	inc de                  ;0dbe	13
	ld hl,($5c61)           ;0dbf	2a 61 5c
	dec hl                  ;0dc2	2b
	dec hl                  ;0dc3	2b
	lddr                    ;0dc4	ed b8
	ld hl,($5c49)           ;0dc6	2a 49 5c
	ex de,hl                ;0dc9	eb
	pop bc                  ;0dca	c1
	ld (hl),b               ;0dcb	70
	dec hl                  ;0dcc	2b
	ld (hl),c               ;0dcd	71
	dec hl                  ;0dce	2b
	ld (hl),e               ;0dcf	73
	dec hl                  ;0dd0	2b
	ld (hl),d               ;0dd1	72

L0DD2:
	pop af                  ;0dd2	f1
	ret                     ;0dd3	c9

L0DD4:
	pop hl                  ;0dd4	e1

L0DD5:
	ld a,(hl)               ;0dd5	7e
	ld ($5b5e),a            ;0dd6	32 5e 5b
	inc a                   ;0dd9	3c
	cp $1e                  ;0dda	fe 1e
	jr nc,L0DE1             ;0ddc	30 03
	rst $28                 ;0dde	ef
	ld e,l                  ;0ddf	5d
	ld e,e                  ;0de0	5b

L0DE1:
	dec a                   ;0de1	3d
	ld (iy+$00),a           ;0de2	fd 77 00
	ld sp,($5c3d)           ;0de5	ed 7b 3d 5c
	rst $18                 ;0de9	df
	xor a                   ;0dea	af
	call L0080              ;0deb	cd 80 00
	call c,$f718            ;0dee	dc 18 f7
	ld hl,($5c5d)           ;0df1	2a 5d 5c
	ld ($5c5f),hl           ;0df4	22 5f 5c
	rst $28                 ;0df7	ef
	push bc                 ;0df8	c5
	ld d,$c9                ;0df9	16 c9

L0DFB:
	call L08E2              ;0dfb	cd e2 08
	rst $38                 ;0dfe	ff

L0DFF:
	rst $30                 ;0dff	f7
	jr L0DFB                ;0e00	18 f9
Data0E02:
	defb 0xef,0x34,0x40                          ;0e02	ef 34 40	.4@
	ld b,c                  ;0e05	41
	nop                     ;0e06	00
	nop                     ;0e07	00
	inc b                   ;0e08	04
	rrca                    ;0e09	0f
	jr c,L0DD5              ;0e0a	38 c9
	rst $28                 ;0e0c	ef
	ld bc,$02c1             ;0e0d	01 c1 02
	inc (hl)                ;0e10	34
	ld b,b                  ;0e11	40
	ld b,c                  ;0e12	41
	nop                     ;0e13	00
	nop                     ;0e14	00
	ld ($38e1),a            ;0e15	32 e1 38
	ret                     ;0e18	c9

; tentative entry (word / jump table scan)
T0E19:
	bit 7,(iy+$01)          ;0e19	fd cb 01 7e
	ret                     ;0e1d	c9
Data0E1E:
	defb 0xcd,0xc9                               ;0e1e	cd c9	..
	ld a,$cd                ;0e20	3e cd
	call c,$ca08            ;0e22	dc 08 ca
	ld h,b                  ;0e25	60
	jr c,L0E29              ;0e26	38 01
	add d                   ;0e28	82

L0E29:
	inc e                   ;0e29	1c
	jp L3EC1                ;0e2a	c3 c1 3e

L0E2D:
	ld bc,$24fb             ;0e2d	01 fb 24
	jp L3EC1                ;0e30	c3 c1 3e
Data0E33:
	defb 0xd5,0xcd,0x66                          ;0e33	d5 cd 66	..f
	ld c,$e5                ;0e36	0e e5
	call L3EC9              ;0e38	cd c9 3e
	cp $cd                  ;0e3b	fe cd
	scf                     ;0e3d	37
	ccf                     ;0e3e	3f

; tentative entry (word / jump table scan)
T0E3F:
	jr nz,L0E45             ;0e3f	20 04
	call T0638              ;0e41	cd 38 06
	scf                     ;0e44	37

L0E45:
	pop hl                  ;0e45	e1
	ld b,$01                ;0e46	06 01
	call L0E9F              ;0e48	cd 9f 0e
	pop de                  ;0e4b	d1
	cp $e2                  ;0e4c	fe e2
	jr z,L0E58              ;0e4e	28 08
	cp $f7                  ;0e50	fe f7
	jr z,L0E58              ;0e52	28 04
	sla d                   ;0e54	cb 22
	jr L0E63                ;0e56	18 0b

L0E58:
	scf                     ;0e58	37
	rl d                    ;0e59	cb 12
	push af                 ;0e5b	f5
	push hl                 ;0e5c	e5
	rst $20                 ;0e5d	e7
	pop hl                  ;0e5e	e1
	pop af                  ;0e5f	f1
	cp $f7                  ;0e60	fe f7
	ccf                     ;0e62	3f

L0E63:
	rl e                    ;0e63	cb 13
	ret                     ;0e65	c9
Text0E66:
	defb 0xeb,0x06,0x02,0xcd,0xc9,0x3e,0xfe,0x2c ;0e66	eb 06 02 cd c9 3e fe 2c	.....>.,
	defb 0x37,0x3f,0x78,0x20,0x2b,0xe7,0xfe,0x2c ;0e6e	37 3f 78 20 2b e7 fe 2c	7?x +..,
	defb 0x28,0x26,0xfe,0xcd,0x28,0x22,0xfe,0xe2 ;0e76	28 26 fe cd 28 22 fe e2	(&..("..
	defb 0x28,0x1e,0xd5,0xcd,0x39,0x06,0xfe,0xcc ;0e7e	28 1e d5 cd 39 06 fe cc	(...9...
	defb 0x28,0x0f,0xfd,0xcb,0x01,0x7e,0xc4,0x1b ;0e86	28 0f fd cb 01 7e c4 1b	(....~..
	defb 0x38,0xc4,0x4f,0x38,0xc4,0x45,0x38,0x18 ;0e8e	38 c4 4f 38 c4 45 38 18	8.O8.E8.
	inc bc                  ;0e96	03
	call T0638              ;0e97	cd 38 06
	pop de                  ;0e9a	d1
	ld b,$02                ;0e9b	06 02
	scf                     ;0e9d	37
	ex de,hl                ;0e9e	eb

L0E9F:
	push af                 ;0e9f	f5

L0EA0:
	pop af                  ;0ea0	f1
	push af                 ;0ea1	f5
	adc hl,hl               ;0ea2	ed 6a
	djnz L0EA0              ;0ea4	10 fa
	pop af                  ;0ea6	f1
	ret                     ;0ea7	c9

L0EA8:
	ld ($5b54),bc           ;0ea8	ed 43 54 5b
	ld a,($2000)            ;0eac	3a 00 20
	ld b,a                  ;0eaf	47
	ld a,(hl)               ;0eb0	7e
	inc hl                  ;0eb1	23
	add a                   ;0eb2	87
	jr c,L0ED5              ;0eb3	38 20

L0EB5:
	cp $03                  ;0eb5	fe 03
	jp nc,L344D             ;0eb7	d2 4d 34
	ld c,a                  ;0eba	4f
	pop af                  ;0ebb	f1
	push hl                 ;0ebc	e5
	push bc                 ;0ebd	c5
	ld hl,$0ee1             ;0ebe	21 e1 0e
	push hl                 ;0ec1	e5
	nextreg $51,$ff         ;0ec2	ed 91 51 ff
	ld h,$3e                ;0ec6	26 3e
	ld l,c                  ;0ec8	69
	ld c,(hl)               ;0ec9	4e
	inc hl                  ;0eca	23
	ld b,(hl)               ;0ecb	46
	push bc                 ;0ecc	c5
	ld bc,($5b54)           ;0ecd	ed 4b 54 5b
	ld hl,($5b52)           ;0ed1	2a 52 5b
	ret                     ;0ed4	c9

L0ED5:
	push bc                 ;0ed5	c5
	ld c,(hl)               ;0ed6	4e
	inc hl                  ;0ed7	23
	ld b,(hl)               ;0ed8	46
	inc hl                  ;0ed9	23
	ld ($5b5a),bc           ;0eda	ed 43 5a 5b
	pop bc                  ;0ede	c1

L0EDF:
	jr L0EB5                ;0edf	18 d4
Text0EE1:
	defb 0xe3,0x6f,0x7c,0xed,0x92,0x51,0x7d,0xe1 ;0ee1	e3 6f 7c ed 92 51 7d e1	.o|..Q}.
	defb 0xc9,0xed,0x92,0x51,0xcd,0xf5           ;0ee9	c9 ed 92 51 cd f5	...Q..
	ld c,$ed                ;0eef	0e ed
	sub c                   ;0ef1	91
	ld d,c                  ;0ef2	51
	rst $38                 ;0ef3	ff
	ret                     ;0ef4	c9
Data0EF5:
	defb 0xc5,0xc9                               ;0ef5	c5 c9	..

L0EF7:
	ld ($5c5f),hl           ;0ef7	22 5f 5c
	ld a,($5b65)            ;0efa	3a 65 5b
	inc a                   ;0efd	3c
	jp z,L0F8D              ;0efe	ca 8d 0f
	dec a                   ;0f01	3d
	call L32CC              ;0f02	cd cc 32
	call L39B1              ;0f05	cd b1 39
	add a                   ;0f08	87
	call nc,L1046           ;0f09	d4 46 10
	jr L0F7B                ;0f0c	18 6d

L0F0E:
	ld hl,$2000             ;0f0e	21 00 20
	ld a,($5b65)            ;0f11	3a 65 5b
	inc a                   ;0f14	3c
	jr z,L0F1A              ;0f15	28 03
	ld hl,($fff8)           ;0f17	2a f8 ff

L0F1A:
	add hl,$00a1            ;0f1a	ed 34 a1 00
	ld e,(hl)               ;0f1e	5e
	inc hl                  ;0f1f	23
	ld d,(hl)               ;0f20	56
	ex de,hl                ;0f21	eb
	add hl,bc               ;0f22	09
	nextreg $51,$ff         ;0f23	ed 91 51 ff

L0F27:
	ld a,(hl)               ;0f27	7e
	cp $28                  ;0f28	fe 28
	jr nc,L0F65             ;0f2a	30 39
	inc hl                  ;0f2c	23
	inc hl                  ;0f2d	23
	ld c,(hl)               ;0f2e	4e
	inc hl                  ;0f2f	23
	ld b,(hl)               ;0f30	46
	inc hl                  ;0f31	23
	push bc                 ;0f32	c5
	push hl                 ;0f33	e5
	dec hl                  ;0f34	2b
	ld d,$00                ;0f35	16 00
	ld e,ixh                ;0f37	dd 5c
	ld b,ixl                ;0f39	dd 45
	call L1288              ;0f3b	cd 88 12
	jr c,L0F76              ;0f3e	38 36

L0F40:
	push de                 ;0f40	d5
	inc hl                  ;0f41	23
	ld de,($5c5f)           ;0f42	ed 5b 5f 5c

L0F46:
	call L126F              ;0f46	cd 6f 12
	ld c,a                  ;0f49	4f
	ex de,hl                ;0f4a	eb
	call L126F              ;0f4b	cd 6f 12
	ex de,hl                ;0f4e	eb
	cp b                    ;0f4f	b8
	jr z,L0F57              ;0f50	28 05
	cp c                    ;0f52	b9
	jr nz,L0F6D             ;0f53	20 18
	jr L0F46                ;0f55	18 ef

L0F57:
	cp c                    ;0f57	b9
	jr nz,L0F6D             ;0f58	20 13
	pop af                  ;0f5a	f1
	neg                     ;0f5b	ed 44
	exx                     ;0f5d	d9
	ld d,a                  ;0f5e	57
	exx                     ;0f5f	d9
	dec hl                  ;0f60	2b
	pop ix                  ;0f61	dd e1
	pop bc                  ;0f63	c1
	scf                     ;0f64	37

L0F65:
	ld a,($5b65)            ;0f65	3a 65 5b
	inc a                   ;0f68	3c
	call nz,L2AF5           ;0f69	c4 f5 2a
	ret                     ;0f6c	c9

L0F6D:
	pop de                  ;0f6d	d1
	dec hl                  ;0f6e	2b
	ld c,$00                ;0f6f	0e 00
	call L12A1              ;0f71	cd a1 12
	jr nc,L0F40             ;0f74	30 ca

L0F76:
	pop hl                  ;0f76	e1
	pop bc                  ;0f77	c1
	add hl,bc               ;0f78	09
	jr L0F27                ;0f79	18 ac

L0F7B:
	ld bc,$c002             ;0f7b	01 02 c0
	ld a,($c001)            ;0f7e	3a 01 c0
	add a                   ;0f81	87
	jr c,L0F89              ;0f82	38 05
	ld hl,($fff8)           ;0f84	2a f8 ff
	jr L0F9A                ;0f87	18 11

L0F89:
	ld h,b                  ;0f89	60
	ld l,c                  ;0f8a	69
	jr L0F27                ;0f8b	18 9a

L0F8D:
	ld a,($5b33)            ;0f8d	3a 33 5b
	nextreg $51,a           ;0f90	ed 92 51
	ld hl,$2000             ;0f93	21 00 20
	ld bc,($5c53)           ;0f96	ed 4b 53 5c

L0F9A:
	ld a,ixh                ;0f9a	dd 7c
	and $03                 ;0f9c	e6 03
	ld e,a                  ;0f9e	5f
	ld d,$34                ;0f9f	16 34
	mul d,e                 ;0fa1	ed 30
	add hl,de               ;0fa3	19
	ld de,($5c5f)           ;0fa4	ed 5b 5f 5c
	ld a,(de)               ;0fa8	1a
	or $20                  ;0fa9	f6 20
	sub $61                 ;0fab	d6 61
	add a                   ;0fad	87
	add hl,a                ;0fae	ed 31
	ld e,(hl)               ;0fb0	5e
	inc hl                  ;0fb1	23
	ld d,(hl)               ;0fb2	56
	ex de,hl                ;0fb3	eb

L0FB4:
	ld a,(hl)               ;0fb4	7e
	inc hl                  ;0fb5	23
	and a                   ;0fb6	a7
	jp z,L0F0E              ;0fb7	ca 0e 0f
	exx                     ;0fba	d9
	ld d,a                  ;0fbb	57
	exx                     ;0fbc	d9
	ld e,(hl)               ;0fbd	5e
	inc hl                  ;0fbe	23
	ld d,(hl)               ;0fbf	56
	inc hl                  ;0fc0	23
	push hl                 ;0fc1	e5
	push bc                 ;0fc2	c5
	ld hl,($5c5f)           ;0fc3	2a 5f 5c
	inc hl                  ;0fc6	23
	ex de,hl                ;0fc7	eb
	add hl,bc               ;0fc8	09
	ld b,ixl                ;0fc9	dd 45

L0FCB:
	call L126F              ;0fcb	cd 6f 12
	ld c,a                  ;0fce	4f
	ex de,hl                ;0fcf	eb
	call L126F              ;0fd0	cd 6f 12
	ex de,hl                ;0fd3	eb
	cp b                    ;0fd4	b8
	jr z,L0FE2              ;0fd5	28 0b
	cp c                    ;0fd7	b9
	jr nz,L0FDC             ;0fd8	20 02
	jr L0FCB                ;0fda	18 ef

L0FDC:
	pop bc                  ;0fdc	c1
	pop hl                  ;0fdd	e1
	inc hl                  ;0fde	23
	inc hl                  ;0fdf	23
	jr L0FB4                ;0fe0	18 d2

L0FE2:
	cp c                    ;0fe2	b9
	jr nz,L0FDC             ;0fe3	20 f7
	ex de,hl                ;0fe5	eb
	pop bc                  ;0fe6	c1
	ex (sp),hl              ;0fe7	e3
	ld a,(hl)               ;0fe8	7e
	inc hl                  ;0fe9	23
	ld h,(hl)               ;0fea	66
	ld l,a                  ;0feb	6f
	add hl,bc               ;0fec	09
	ld c,(hl)               ;0fed	4e
	inc hl                  ;0fee	23
	ld b,(hl)               ;0fef	46
	inc hl                  ;0ff0	23
	push hl                 ;0ff1	e5
	pop ix                  ;0ff2	dd e1
	ex de,hl                ;0ff4	eb
	dec hl                  ;0ff5	2b
	pop de                  ;0ff6	d1
	nextreg $51,$ff         ;0ff7	ed 91 51 ff
	scf                     ;0ffb	37
	jp L0F65                ;0ffc	c3 65 0f

L0FFF:
	ld hl,$c001             ;0fff	21 01 c0
	bit 7,(hl)              ;1002	cb 7e
	inc hl                  ;1004	23
	ret nz                  ;1005	c0
	ex de,hl                ;1006	eb
	ld hl,($fff8)           ;1007	2a f8 ff
	jr L1019                ;100a	18 0d

L100C:
	ld a,($5b33)            ;100c	3a 33 5b
	nextreg $51,a           ;100f	ed 92 51
	ld de,($5c53)           ;1012	ed 5b 53 5c
	ld hl,$2000             ;1016	21 00 20

L1019:
	push bc                 ;1019	c5
	push de                 ;101a	d5
	add hl,$009c            ;101b	ed 34 9c 00
	ld e,(hl)               ;101f	5e
	inc hl                  ;1020	23
	ld d,(hl)               ;1021	56
	inc hl                  ;1022	23
	ld a,(hl)               ;1023	7e
	inc hl                  ;1024	23
	ex af,af'               ;1025	08
	ld a,(hl)               ;1026	7e
	inc hl                  ;1027	23
	ld h,(hl)               ;1028	66
	ld l,a                  ;1029	6f
	and a                   ;102a	a7
	sbc hl,bc               ;102b	ed 42
	jr nc,L1032             ;102d	30 03
	add hl,bc               ;102f	09
	ld b,h                  ;1030	44
	ld c,l                  ;1031	4d

L1032:
	ex de,hl                ;1032	eb
	ld d,b                  ;1033	50
	ld e,c                  ;1034	59
	ex af,af'               ;1035	08
	ld b,a                  ;1036	47
	bsrl de,b               ;1037	ed 2a
	add hl,de               ;1039	19
	add hl,de               ;103a	19
	ld e,(hl)               ;103b	5e
	inc hl                  ;103c	23
	ld d,(hl)               ;103d	56
	pop hl                  ;103e	e1
	add hl,de               ;103f	19
	pop bc                  ;1040	c1
	nextreg $51,$ff         ;1041	ed 91 51 ff
	ret                     ;1045	c9

L1046:
	push ix                 ;1046	dd e5
	ld hl,$c002             ;1048	21 02 c0

L104B:
	ld a,(hl)               ;104b	7e
	cp $28                  ;104c	fe 28
	jr nc,L1059             ;104e	30 09
	inc hl                  ;1050	23
	inc hl                  ;1051	23
	ld e,(hl)               ;1052	5e
	inc hl                  ;1053	23
	ld d,(hl)               ;1054	56
	inc hl                  ;1055	23
	add hl,de               ;1056	19
	jr L104B                ;1057	18 f2

L1059:
	ex de,hl                ;1059	eb
	ld hl,$c000             ;105a	21 00 c0
	set 7,(hl)              ;105d	cb fe
	ld a,d                  ;105f	7a
	cp $fe                  ;1060	fe fe
	jr nc,L1081             ;1062	30 1d
	ex de,hl                ;1064	eb
	inc hl                  ;1065	23
	ld ($fff8),hl           ;1066	22 f8 ff
	ld de,$c002             ;1069	11 02 c0
	call L109D              ;106c	cd 9d 10
	ld ($fffc),hl           ;106f	22 fc ff
	ld hl,$fff7             ;1072	21 f7 ff
	ld ($fffe),hl           ;1075	22 fe ff
	ld hl,$0000             ;1078	21 00 00
	ld ($fffa),hl           ;107b	22 fa ff
	pop ix                  ;107e	dd e1
	ret                     ;1080	c9

L1081:
	inc hl                  ;1081	23
	set 7,(hl)              ;1082	cb fe
	set 0,(iy+$01)          ;1084	fd cb 01 c6
	inc hl                  ;1088	23
	ex de,hl                ;1089	eb
	call L10A5              ;108a	cd a5 10
	pop ix                  ;108d	dd e1
	ret                     ;108f	c9

L1090:
	ld a,($5b33)            ;1090	3a 33 5b
	nextreg $51,a           ;1093	ed 92 51
	ld hl,$2000             ;1096	21 00 20
	ld de,($5c53)           ;1099	ed 5b 53 5c

L109D:
	res 5,(iy+$30)          ;109d	fd cb 30 ae
	res 0,(iy+$01)          ;10a1	fd cb 01 86

L10A5:
	ld a,$07                ;10a5	3e 07
	call L12C4              ;10a7	cd c4 12
	and $03                 ;10aa	e6 03
	push af                 ;10ac	f5
	nextreg $07,$03         ;10ad	ed 91 07 03
	push de                 ;10b1	d5
	push hl                 ;10b2	e5
	bit 0,(iy+$01)          ;10b3	fd cb 01 46
	jr nz,L10CA             ;10b7	20 11
	ld d,h                  ;10b9	54
	ld e,l                  ;10ba	5d
	add de,$00a3            ;10bb	ed 35 a3 00
	xor a                   ;10bf	af
	ld b,$4f                ;10c0	06 4f

L10C2:
	ld (hl),e               ;10c2	73
	inc hl                  ;10c3	23
	ld (hl),d               ;10c4	72
	inc hl                  ;10c5	23
	ld (de),a               ;10c6	12
	inc de                  ;10c7	13
	djnz L10C2              ;10c8	10 f8

L10CA:
	pop hl                  ;10ca	e1
	exx                     ;10cb	d9
	ld hl,$0000             ;10cc	21 00 00
	ld ($5b54),hl           ;10cf	22 54 5b
	pop hl                  ;10d2	e1
	push hl                 ;10d3	e5

L10D4:
	ld a,(hl)               ;10d4	7e
	cp $28                  ;10d5	fe 28
	jp nc,L11D4             ;10d7	d2 d4 11
	ld b,a                  ;10da	47
	inc hl                  ;10db	23
	ld c,(hl)               ;10dc	4e
	inc hl                  ;10dd	23
	ld ($5b54),bc           ;10de	ed 43 54 5b
	inc hl                  ;10e2	23
	push hl                 ;10e3	e5
	ld b,$01                ;10e4	06 01
	res 6,(iy+$30)          ;10e6	fd cb 30 b6

L10EA:
	inc hl                  ;10ea	23
	ld a,(hl)               ;10eb	7e
	cp $0d                  ;10ec	fe 0d
	jr z,L1122              ;10ee	28 32
	cp $21                  ;10f0	fe 21
	jr c,L10EA              ;10f2	38 f6
	cp $ea                  ;10f4	fe ea
	jr z,L113B              ;10f6	28 43
	cp $3b                  ;10f8	fe 3b
	jr z,L113B              ;10fa	28 3f
	bit 0,(iy+$01)          ;10fc	fd cb 01 46
	jr nz,L110E             ;1100	20 0c
	cp $40                  ;1102	fe 40
	jr z,L115B              ;1104	28 55
	cp $91                  ;1106	fe 91
	jr z,L115B              ;1108	28 51
	cp $ce                  ;110a	fe ce
	jr z,L115B              ;110c	28 4d

L110E:
	cp $98                  ;110e	fe 98
	jr nz,L1116             ;1110	20 04
	set 6,(iy+$30)          ;1112	fd cb 30 f6

L1116:
	ld d,$01                ;1116	16 01
	ld c,$00                ;1118	0e 00
	call L12A2              ;111a	cd a2 12
	inc b                   ;111d	04
	cp $0d                  ;111e	fe 0d
	jr nz,L10EA             ;1120	20 c8

L1122:
	inc hl                  ;1122	23
	pop de                  ;1123	d1
	bit 6,(iy+$30)          ;1124	fd cb 30 76
	jr z,L10D4              ;1128	28 aa
	push hl                 ;112a	e5
	ex de,hl                ;112b	eb

L112C:
	ld de,$00fa             ;112c	11 fa 00
	call L1288              ;112f	cd 88 12
	jr c,L1138              ;1132	38 04
	ld (hl),$83             ;1134	36 83
	jr L112C                ;1136	18 f4

L1138:
	pop hl                  ;1138	e1
	jr L10D4                ;1139	18 99

L113B:
	ld d,$01                ;113b	16 01
	ld c,$00                ;113d	0e 00
	call L12A0              ;113f	cd a0 12
	cp $0d                  ;1142	fe 0d
	jr z,L1122              ;1144	28 dc
	jr L113B                ;1146	18 f3

L1148:
	bit 5,(iy+$30)          ;1148	fd cb 30 6e
	jr nz,L1157             ;114c	20 09
	ex (sp),hl              ;114e	e3
	ld ($5b52),hl           ;114f	22 52 5b
	ex (sp),hl              ;1152	e3
	set 5,(iy+$30)          ;1153	fd cb 30 ee

L1157:
	exx                     ;1157	d9
	ex af,af'               ;1158	08
	jr L1116                ;1159	18 bb

L115B:
	ex af,af'               ;115b	08
	exx                     ;115c	d9
	ld a,d                  ;115d	7a
	or $c0                  ;115e	f6 c0
	cp $ff                  ;1160	fe ff
	jr c,L1169              ;1162	38 05
	ld a,e                  ;1164	7b
	cp $e6                  ;1165	fe e6
	jr nc,L1148             ;1167	30 df

L1169:
	exx                     ;1169	d9
	ex af,af'               ;116a	08
	and $03                 ;116b	e6 03
	ld e,a                  ;116d	5f
	ld d,$1a                ;116e	16 1a
	mul d,e                 ;1170	ed 30
	inc hl                  ;1172	23
	call L126F              ;1173	cd 6f 12
	sub $61                 ;1176	d6 61
	add de,a                ;1178	ed 32
	ld a,$4e                ;117a	3e 4e
	sub e                   ;117c	93
	ex af,af'               ;117d	08
	push de                 ;117e	d5
	exx                     ;117f	d9
	pop bc                  ;1180	c1
	push hl                 ;1181	e5
	inc bc                  ;1182	03
	add hl,bc               ;1183	09
	add hl,bc               ;1184	09
	push hl                 ;1185	e5
	ld c,(hl)               ;1186	4e
	inc hl                  ;1187	23
	ld b,(hl)               ;1188	46
	dec bc                  ;1189	0b
	ld h,d                  ;118a	62
	ld l,e                  ;118b	6b
	sbc hl,bc               ;118c	ed 42
	ld b,h                  ;118e	44
	ld c,l                  ;118f	4d
	ld h,d                  ;1190	62
	ld l,e                  ;1191	6b
	dec hl                  ;1192	2b
	add de,$0004            ;1193	ed 35 04 00
	push de                 ;1197	d5
	lddr                    ;1198	ed b8
	pop de                  ;119a	d1
	inc de                  ;119b	13
	inc hl                  ;119c	23
	push hl                 ;119d	e5
	exx                     ;119e	d9
	pop de                  ;119f	d1
	exx                     ;11a0	d9
	ex af,af'               ;11a1	08
	ld b,a                  ;11a2	47
	pop hl                  ;11a3	e1

L11A4:
	ld a,$05                ;11a4	3e 05
	add (hl)                ;11a6	86
	ld (hl),a               ;11a7	77
	inc hl                  ;11a8	23
	ld a,$00                ;11a9	3e 00
	adc (hl)                ;11ab	8e
	ld (hl),a               ;11ac	77
	inc hl                  ;11ad	23
	djnz L11A4              ;11ae	10 f4
	pop hl                  ;11b0	e1
	exx                     ;11b1	d9
	ld a,b                  ;11b2	78
	ld (de),a               ;11b3	12
	inc de                  ;11b4	13
	pop ix                  ;11b5	dd e1
	pop bc                  ;11b7	c1
	push bc                 ;11b8	c5
	push ix                 ;11b9	dd e5
	and a                   ;11bb	a7
	sbc hl,bc               ;11bc	ed 42
	ex de,hl                ;11be	eb
	ld (hl),e               ;11bf	73
	inc hl                  ;11c0	23
	ld (hl),d               ;11c1	72
	inc hl                  ;11c2	23
	ex de,hl                ;11c3	eb
	add hl,bc               ;11c4	09
	ex (sp),hl              ;11c5	e3
	dec hl                  ;11c6	2b
	sbc hl,bc               ;11c7	ed 42
	ex de,hl                ;11c9	eb
	ld (hl),e               ;11ca	73
	inc hl                  ;11cb	23
	ld (hl),d               ;11cc	72
	pop hl                  ;11cd	e1
	push ix                 ;11ce	dd e5
	ld b,a                  ;11d0	47
	jp L1116                ;11d1	c3 16 11

L11D4:
	bit 0,(iy+$01)          ;11d4	fd cb 01 46
	jp nz,L1265             ;11d8	c2 65 12
	bit 5,(iy+$30)          ;11db	fd cb 30 6e
	jr z,L11E7              ;11df	28 06
	ld hl,($5b52)           ;11e1	2a 52 5b
	dec hl                  ;11e4	2b
	dec hl                  ;11e5	2b
	dec hl                  ;11e6	2b

L11E7:
	pop bc                  ;11e7	c1
	push bc                 ;11e8	c5
	and a                   ;11e9	a7
	sbc hl,bc               ;11ea	ed 42
	push hl                 ;11ec	e5
	exx                     ;11ed	d9
	pop bc                  ;11ee	c1
	add hl,$00a1            ;11ef	ed 34 a1 00
	ld (hl),c               ;11f3	71
	inc hl                  ;11f4	23
	ld (hl),b               ;11f5	70
	dec hl                  ;11f6	2b
	dec hl                  ;11f7	2b
	push de                 ;11f8	d5
	push hl                 ;11f9	e5
	set 7,d                 ;11fa	cb fa
	set 6,d                 ;11fc	cb f2
	xor a                   ;11fe	af
	ld hl,$fff6             ;11ff	21 f6 ff
	sbc hl,de               ;1202	ed 52
	srl h                   ;1204	cb 3c
	rr l                    ;1206	cb 1d
	ld bc,($5b54)           ;1208	ed 4b 54 5b
	pop de                  ;120c	d1
	ex de,hl                ;120d	eb
	ld (hl),b               ;120e	70
	dec hl                  ;120f	2b
	ld (hl),c               ;1210	71
	dec hl                  ;1211	2b
	ex de,hl                ;1212	eb

L1213:
	and a                   ;1213	a7
	sbc hl,bc               ;1214	ed 42
	jr nc,L1220             ;1216	30 08
	add hl,bc               ;1218	09
	inc a                   ;1219	3c
	srl b                   ;121a	cb 38
	rr c                    ;121c	cb 19
	jr L1213                ;121e	18 f3

L1220:
	ld (de),a               ;1220	12
	ld b,a                  ;1221	47
	ld hl,$0000             ;1222	21 00 00
	exx                     ;1225	d9
	pop hl                  ;1226	e1
	dec hl                  ;1227	2b
	ld (hl),$00             ;1228	36 00
	inc hl                  ;122a	23
	ld (hl),$00             ;122b	36 00
	inc hl                  ;122d	23
	pop de                  ;122e	d1
	push de                 ;122f	d5

L1230:
	ld a,(de)               ;1230	1a
	cp $28                  ;1231	fe 28
	jr nc,L1265             ;1233	30 30
	ld b,a                  ;1235	47
	inc de                  ;1236	13
	ld a,(de)               ;1237	1a
	ld c,a                  ;1238	4f
	push bc                 ;1239	c5
	exx                     ;123a	d9
	pop de                  ;123b	d1
	bsrl de,b               ;123c	ed 2a
	ex de,hl                ;123e	eb
	and a                   ;123f	a7
	sbc hl,de               ;1240	ed 52
	ex de,hl                ;1242	eb
	exx                     ;1243	d9
	jr z,L125B              ;1244	28 15
	pop bc                  ;1246	c1
	push bc                 ;1247	c5
	push de                 ;1248	d5
	ex de,hl                ;1249	eb
	scf                     ;124a	37
	sbc hl,bc               ;124b	ed 42
	ex de,hl                ;124d	eb

L124E:
	ld (hl),e               ;124e	73
	inc hl                  ;124f	23
	ld (hl),d               ;1250	72
	inc hl                  ;1251	23
	exx                     ;1252	d9
	inc hl                  ;1253	23
	dec de                  ;1254	1b
	ld a,d                  ;1255	7a
	or e                    ;1256	b3
	exx                     ;1257	d9
	jr nz,L124E             ;1258	20 f4
	pop de                  ;125a	d1

L125B:
	ex de,hl                ;125b	eb
	inc hl                  ;125c	23
	ld c,(hl)               ;125d	4e
	inc hl                  ;125e	23
	ld b,(hl)               ;125f	46
	inc hl                  ;1260	23
	add hl,bc               ;1261	09
	ex de,hl                ;1262	eb
	jr L1230                ;1263	18 cb

L1265:
	pop bc                  ;1265	c1
	pop af                  ;1266	f1
	nextreg $07,a           ;1267	ed 92 07
	nextreg $51,$ff         ;126a	ed 91 51 ff
	ret                     ;126e	c9

L126F:
	ld a,(hl)               ;126f	7e
	inc hl                  ;1270	23
	cp $0d                  ;1271	fe 0d
	jr z,L1282              ;1273	28 0d
	cp $21                  ;1275	fe 21
	jr c,L126F              ;1277	38 f6
	cp $5b                  ;1279	fe 5b
	ret nc                  ;127b	d0
	cp $41                  ;127c	fe 41
	ret c                   ;127e	d8
	or $20                  ;127f	f6 20
	ret                     ;1281	c9

L1282:
	ld a,$3a                ;1282	3e 3a
	ret                     ;1284	c9

L1285:
	ld hl,($5c5d)           ;1285	2a 5d 5c

L1288:
	ld c,$00                ;1288	0e 00

L128A:
	dec d                   ;128a	15
	scf                     ;128b	37
	ret z                   ;128c	c8

L128D:
	inc hl                  ;128d	23

L128E:
	ld a,(hl)               ;128e	7e
	cp $0d                  ;128f	fe 0d
	jr z,L12C1              ;1291	28 2e
	cp $21                  ;1293	fe 21
	jr c,L128D              ;1295	38 f6
	cp e                    ;1297	bb
	jr nz,L12A2             ;1298	20 08
	and a                   ;129a	a7
	ret                     ;129b	c9

L129C:
	add hl,$0005            ;129c	ed 34 05 00

L12A0:
	inc hl                  ;12a0	23

L12A1:
	ld a,(hl)               ;12a1	7e

L12A2:
	cp $0e                  ;12a2	fe 0e
	jr z,L129C              ;12a4	28 f6
	cp $22                  ;12a6	fe 22
	jr nz,L12AD             ;12a8	20 03
	dec c                   ;12aa	0d
	jr L12A0                ;12ab	18 f3

L12AD:
	cp $3a                  ;12ad	fe 3a
	jr z,L12B9              ;12af	28 08
	cp $cb                  ;12b1	fe cb
	jr z,L12B9              ;12b3	28 04
	cp $98                  ;12b5	fe 98
	jr nz,L12BD             ;12b7	20 04

L12B9:
	bit 0,c                 ;12b9	cb 41
	jr z,L128A              ;12bb	28 cd

L12BD:
	cp $0d                  ;12bd	fe 0d
	jr nz,L12A0             ;12bf	20 df

L12C1:
	dec d                   ;12c1	15
	scf                     ;12c2	37
	ret                     ;12c3	c9

L12C4:
	ld bc,$243b             ;12c4	01 3b 24
	out (c),a               ;12c7	ed 79
	inc b                   ;12c9	04
	in a,(c)                ;12ca	ed 78
	ret                     ;12cc	c9

L12CD:
	ld ($5b92),sp           ;12cd	ed 73 92 5b
	ex af,af'               ;12d1	08
	pop af                  ;12d2	f1
	ex af,af'               ;12d3	08
	call L3A20              ;12d4	cd 20 3a
	ex af,af'               ;12d7	08
	push af                 ;12d8	f5
	ex af,af'               ;12d9	08
	sub $21                 ;12da	d6 21
	add a                   ;12dc	87
	ld hl,$134b             ;12dd	21 4b 13
	add hl,a                ;12e0	ed 31
	ld a,(hl)               ;12e2	7e
	inc hl                  ;12e3	23
	ld ixh,a                ;12e4	dd 67
	ld a,(hl)               ;12e6	7e
	ld ixl,a                ;12e7	dd 6f
	ld hl,($5c5d)           ;12e9	2a 5d 5c
	ld a,($5b65)            ;12ec	3a 65 5b
	inc a                   ;12ef	3c
	call nz,L39C3           ;12f0	c4 c3 39
	ld ($5b8a),hl           ;12f3	22 8a 5b
	call L0EF7              ;12f6	cd f7 0e
	jr c,L130A              ;12f9	38 0f
	ld a,ixh                ;12fb	dd 7c
	cp $91                  ;12fd	fe 91
	jp z,L1B3B              ;12ff	ca 3b 1b
	cp $ce                  ;1302	fe ce
	jr z,L1308              ;1304	28 02
	rst $08                 ;1306	cf
	ld h,l                  ;1307	65

L1308:
	rst $08                 ;1308	cf
	jr L12F8                ;1309	18 ed
Data130B:
	defb 0x53,0x5f,0x5c                          ;130b	53 5f 5c	S_\
	push ix                 ;130e	dd e5
	exx                     ;1310	d9
	ld ixh,d                ;1311	dd 62
	exx                     ;1313	d9
	ld a,($5b65)            ;1314	3a 65 5b
	inc a                   ;1317	3c
	jr z,L1339              ;1318	28 1f
	dec a                   ;131a	3d
	ex (sp),hl              ;131b	e3
	add hl,$fffc            ;131c	ed 34 fc ff
	call L3905              ;1320	cd 05 39
	ex (sp),hl              ;1323	e3
	ld bc,($5ebb)           ;1324	ed 4b bb 5e
	and a                   ;1328	a7
	sbc hl,bc               ;1329	ed 42
	add hl,$5db6            ;132b	ed 34 b6 5d
	ld ($5c5d),hl           ;132f	22 5d 5c
	ex de,hl                ;1332	eb
	ld d,(hl)               ;1333	56
	inc hl                  ;1334	23
	ld e,(hl)               ;1335	5e
	pop hl                  ;1336	e1
	jr L1348                ;1337	18 0f

L1339:
	ld ($5c5d),hl           ;1339	22 5d 5c
	pop hl                  ;133c	e1
	dec hl                  ;133d	2b
	dec hl                  ;133e	2b
	dec hl                  ;133f	2b
	ld e,(hl)               ;1340	5e
	dec hl                  ;1341	2b
	ld d,(hl)               ;1342	56
	add hl,$0004            ;1343	ed 34 04 00
	add hl,bc               ;1347	09

L1348:
	ld a,ixh                ;1348	dd 7c
	ret                     ;134a	c9
Text134B:
	defb 0x40,0x3a,0x91,0x28,0xce,0x28           ;134b	40 3a 91 28 ce 28	@:.(.(
	ld e,$3a                ;1351	1e 3a
	call L135C              ;1353	cd 5c 13
	call L0902              ;1356	cd 02 09
	ret                     ;1359	c9

L135A:
	ld e,$28                ;135a	1e 28

L135C:
	call L3EC9              ;135c	cd c9 3e
	call L387F              ;135f	cd 7f 38
	jr nc,L137F             ;1362	30 1b

L1364:
	rst $20                 ;1364	e7
	cp e                    ;1365	bb
	ld d,$40                ;1366	16 40
	ret z                   ;1368	c8
	call L387A              ;1369	cd 7a 38
	jr c,L1364              ;136c	38 f6
	cp $24                  ;136e	fe 24
	jr nz,L1377             ;1370	20 05
	rst $20                 ;1372	e7
	cp e                    ;1373	bb
	ld d,$00                ;1374	16 00
	ret z                   ;1376	c8

L1377:
	cp $0d                  ;1377	fe 0d
	jr nz,L137F             ;1379	20 04
	ld a,e                  ;137b	7b
	cp $3a                  ;137c	fe 3a
	ret z                   ;137e	c8

L137F:
	rst $08                 ;137f	cf
	dec bc                  ;1380	0b

L1381:
	res 0,(iy+$02)          ;1381	fd cb 02 86
	ld a,$fe                ;1385	3e fe
	rst $28                 ;1387	ef
	ld bc,$cd16             ;1388	01 16 cd
	nop                     ;138b	ed 13
	ret nc                  ;138d	d0
	ld hl,$5c90             ;138e	21 90 5c
	ld a,(hl)               ;1391	7e
	or $f8                  ;1392	f6 f8
	ld (hl),a               ;1394	77
	res 6,(iy+$57)          ;1395	fd cb 57 b6
	ret                     ;1399	c9

L139A:
	call T3EF9              ;139a	cd f9 3e
	rst $28                 ;139d	ef
	ld bc,$7916             ;139e	01 16 79
	cp $53                  ;13a1	fe 53
	jr z,L13B2              ;13a3	28 0d
	cp $4b                  ;13a5	fe 4b
	jr z,L13B2              ;13a7	28 09
	ret                     ;13a9	c9
Data13AA:
	defb 0x3e,0xfe,0xcd,0xf9,0x3e,0xef,0x01,0x16 ;13aa	3e fe cd f9 3e ef 01 16	>...>...

L13B2:
	call L13ED              ;13b2	cd ed 13
	ret c                   ;13b5	d8

L13B6:
	call L3FE7              ;13b6	cd e7 3f
	ld hl,$5b9d             ;13b9	21 9d 5b
	ld d,h                  ;13bc	54
	ld e,l                  ;13bd	5d
	ld bc,($5c6c)           ;13be	ed 4b 6c 5c
	ld (hl),$18             ;13c2	36 18
	jr nc,L13CD             ;13c4	30 07
	ld (hl),$11             ;13c6	36 11
	inc hl                  ;13c8	23
	ld (hl),b               ;13c9	70
	inc hl                  ;13ca	23
	ld (hl),$10             ;13cb	36 10

L13CD:
	inc hl                  ;13cd	23
	ld (hl),c               ;13ce	71
	inc hl                  ;13cf	23
	ld (hl),$14             ;13d0	36 14
	inc hl                  ;13d2	23
	ld a,($5c91)            ;13d3	3a 91 5c
	ld b,a                  ;13d6	47
	rrca                    ;13d7	0f
	rrca                    ;13d8	0f
	and $01                 ;13d9	e6 01
	ld (hl),a               ;13db	77
	inc hl                  ;13dc	23
	ld (hl),$15             ;13dd	36 15
	inc hl                  ;13df	23
	ld a,b                  ;13e0	78
	and $01                 ;13e1	e6 01
	ld (hl),a               ;13e3	77
	inc hl                  ;13e4	23
	sbc hl,de               ;13e5	ed 52
	ld b,h                  ;13e7	44
	ld c,l                  ;13e8	4d
	rst $28                 ;13e9	ef
	inc a                   ;13ea	3c
	jr nz,L13B6             ;13eb	20 c9

L13ED:
	ld a,($5c7f)            ;13ed	3a 7f 5c
	and $0f                 ;13f0	e6 0f
	jr z,L1417              ;13f2	28 23
	rrca                    ;13f4	0f
	bit 2,a                 ;13f5	cb 57
	jr z,L13FB              ;13f7	28 02
	rrca                    ;13f9	0f
	inc a                   ;13fa	3c

L13FB:
	and $07                 ;13fb	e6 07
	add $5f                 ;13fd	c6 5f
	ld l,a                  ;13ff	6f
	ld h,$5b                ;1400	26 5b
	ld e,(hl)               ;1402	5e
	ld a,l                  ;1403	7d
	add $29                 ;1404	c6 29
	ld l,a                  ;1406	6f
	ld d,(hl)               ;1407	56
	ld ($5c6c),de           ;1408	ed 53 6c 5c
	ld hl,$5c91             ;140c	21 91 5c
	ld a,(hl)               ;140f	7e
	rrca                    ;1410	0f
	xor (hl)                ;1411	ae
	and $55                 ;1412	e6 55
	xor (hl)                ;1414	ae
	ld (hl),a               ;1415	77
	ret                     ;1416	c9

L1417:
	rst $28                 ;1417	ef
	ld c,l                  ;1418	4d
	dec c                   ;1419	0d
	scf                     ;141a	37
	ret                     ;141b	c9
Data141C:
	defb 0xfe,0x2c,0x20,0x0f,0xcd,0x38,0x06,0xcd ;141c	fe 2c 20 0f cd 38 06 cd	., ..8..
	defb 0x02,0x09,0xcd,0xfc                     ;1424	02 09 cd fc	....
	scf                     ;1428	37
	cp $04                  ;1429	fe 04
	jr c,L1434              ;142b	38 07

L142D:
	rst $08                 ;142d	cf
	ld a,(bc)               ;142e	0a
	call L0902              ;142f	cd 02 09
	ld a,$ff                ;1432	3e ff

L1434:
	push af                 ;1434	f5
	call L37FC              ;1435	cd fc 37
	pop de                  ;1438	d1
	cp $03                  ;1439	fe 03
	jr nc,L142D             ;143b	30 f0
	ld e,a                  ;143d	5f
	and a                   ;143e	a7
	jr nz,L1446             ;143f	20 05
	inc d                   ;1441	14
	jr nz,L142D             ;1442	20 e9
	jr L1466                ;1444	18 20

L1446:
	dec a                   ;1446	3d
	jr nz,L1454             ;1447	20 0b
	ld a,d                  ;1449	7a
	cp $04                  ;144a	fe 04
	jr nc,L142D             ;144c	30 df
	add a                   ;144e	87
	add a                   ;144f	87
	or e                    ;1450	b3
	ld e,a                  ;1451	5f
	jr L1466                ;1452	18 12

L1454:
	ld a,d                  ;1454	7a
	cp $ff                  ;1455	fe ff
	jr z,L1466              ;1457	28 0d
	cp $02                  ;1459	fe 02
	jr nc,L142D             ;145b	30 d0
	add a                   ;145d	87
	ld bc,$123b             ;145e	01 3b 12
	out (c),a               ;1461	ed 79
	ld ($5b7b),a            ;1463	32 7b 5b

L1466:
	ld a,($5c7f)            ;1466	3a 7f 5c
	and $f0                 ;1469	e6 f0
	or e                    ;146b	b3
	ld ($5c7f),a            ;146c	32 7f 5c
	and $03                 ;146f	e6 03
	ld hl,$0b11             ;1471	21 11 0b
	ld e,a                  ;1474	5f
	ld bc,$09f4             ;1475	01 f4 09
	jr z,L1496              ;1478	28 1c
	dec a                   ;147a	3d
	scf                     ;147b	37
	jr nz,L1493             ;147c	20 15
	ld hl,$110b             ;147e	21 0b 11
	ld a,d                  ;1481	7a
	and a                   ;1482	a7
	ld e,$80                ;1483	1e 80
	jr z,L1493              ;1485	28 0c
	ld e,$00                ;1487	1e 00
	dec a                   ;1489	3d
	jr z,L1493              ;148a	28 07
	dec a                   ;148c	3d
	ld a,$06                ;148d	3e 06
	jr z,L1493              ;148f	28 02
	ld a,$02                ;1491	3e 02

L1493:
	ld bc,$5b4d             ;1493	01 4d 5b

L1496:
	push hl                 ;1496	e5
	ld hl,($5c4f)           ;1497	2a 4f 5c
	ld (hl),c               ;149a	71
	inc hl                  ;149b	23
	ld (hl),b               ;149c	70
	inc hl                  ;149d	23
	inc hl                  ;149e	23
	inc hl                  ;149f	23
	inc hl                  ;14a0	23
	ld (hl),c               ;14a1	71
	inc hl                  ;14a2	23
	ld (hl),b               ;14a3	70
	pop hl                  ;14a4	e1
	ret c                   ;14a5	d8
	ld d,a                  ;14a6	57
	push de                 ;14a7	d5
	call L14C0              ;14a8	cd c0 14
	pop de                  ;14ab	d1
	ld a,($5b62)            ;14ac	3a 62 5b
	and $38                 ;14af	e6 38
	or d                    ;14b1	b2
	out ($ff),a             ;14b2	d3 ff
	ld d,$7f                ;14b4	16 7f

L14B6:
	ld a,$15                ;14b6	3e 15

L14B8:
	call L12C4              ;14b8	cd c4 12
	and d                   ;14bb	a2
	or e                    ;14bc	b3
	out (c),a               ;14bd	ed 79
	ret                     ;14bf	c9

L14C0:
	ld bc,$243b             ;14c0	01 3b 24
	ld de,$5354             ;14c3	11 54 53
	out (c),d               ;14c6	ed 51
	inc b                   ;14c8	04
	in a,(c)                ;14c9	ed 78
	cp h                    ;14cb	bc
	ret z                   ;14cc	c8
	di                      ;14cd	f3
	out (c),h               ;14ce	ed 61
	dec b                   ;14d0	05
	out (c),e               ;14d1	ed 59
	inc b                   ;14d3	04
	in a,(c)                ;14d4	ed 78
	out (c),l               ;14d6	ed 69
	ld hl,$8000             ;14d8	21 00 80
	ld de,$6000             ;14db	11 00 60
	ld b,a                  ;14de	47

L14DF:
	ld a,(de)               ;14df	1a
	ld c,(hl)               ;14e0	4e
	ld (hl),a               ;14e1	77
	ld a,c                  ;14e2	79
	ld (de),a               ;14e3	12
	inc de                  ;14e4	13
	inc hl                  ;14e5	23
	bit 7,d                 ;14e6	cb 7a
	jr z,L14DF              ;14e8	28 f5
	ld a,b                  ;14ea	78
	nextreg $54,a           ;14eb	ed 92 54
	ei                      ;14ee	fb
	ret                     ;14ef	c9

L14F0:
	push ix                 ;14f0	dd e5
	and $0f                 ;14f2	e6 0f
	ld e,a                  ;14f4	5f
	ld d,a                  ;14f5	57
	srl d                   ;14f6	cb 3a
	srl d                   ;14f8	cb 3a
	call L1466              ;14fa	cd 66 14
	pop ix                  ;14fd	dd e1
	ret                     ;14ff	c9
	call T15B6              ;1500	cd b6 15
	call L158F              ;1503	cd 8f 15
	call L3E80              ;1506	cd 80 3e
	ld l,b                  ;1509	68
	ld h,$11                ;150a	26 11
	add hl,bc               ;150c	09
	add hl,bc               ;150d	09
	ld l,$12                ;150e	2e 12
	call L1548              ;1510	cd 48 15
	ld de,$0000             ;1513	11 00 00
	ld l,$32                ;1516	2e 32
	call L1548              ;1518	cd 48 15
	ld l,$26                ;151b	2e 26
	call L1548              ;151d	cd 48 15
	ld l,$16                ;1520	2e 16
	call L1548              ;1522	cd 48 15
	ld l,$2f                ;1525	2e 2f
	call L1548              ;1527	cd 48 15
	xor a                   ;152a	af
	nextreg $31,a           ;152b	ed 92 31
	nextreg $68,a           ;152e	ed 92 68
	nextreg $6b,a           ;1531	ed 92 6b
	call L1466              ;1534	cd 66 14
	ld bc,$123b             ;1537	01 3b 12
	xor a                   ;153a	af
	out (c),a               ;153b	ed 79
	ld ($5b7b),a            ;153d	32 7b 5b
	ld d,$e3                ;1540	16 e3
	ld e,a                  ;1542	5f
	call L14B6              ;1543	cd b6 14
	jr T155F                ;1546	18 17

L1548:
	ld bc,$243b             ;1548	01 3b 24
	out (c),l               ;154b	ed 69
	inc b                   ;154d	04
	out (c),e               ;154e	ed 59
	inc l                   ;1550	2c
	dec b                   ;1551	05
	out (c),l               ;1552	ed 69
	inc b                   ;1554	04
	out (c),d               ;1555	ed 51
	ret                     ;1557	c9
Data1558:
	defb 0xcd,0x5f,0x15                          ;1558	cd 5f 15	._.

L155B:
	ld l,$19                ;155b	2e 19
	jr L156E                ;155d	18 0f

; tentative entry (word / jump table scan)
T155F:
	ld l,$1b                ;155f	2e 1b
	ld de,$9fff             ;1561	11 ff 9f
	call L1571              ;1564	cd 71 15
	ld l,$18                ;1567	2e 18
	call L156E              ;1569	cd 6e 15
	ld l,$1a                ;156c	2e 1a

L156E:
	ld de,$ffbf             ;156e	11 bf ff

L1571:
	ld bc,$0000             ;1571	01 00 00
	push bc                 ;1574	c5
	ld bc,$243b             ;1575	01 3b 24
	ld a,$1c                ;1578	3e 1c
	out (c),a               ;157a	ed 79
	inc b                   ;157c	04
	ld a,$0f                ;157d	3e 0f
	out (c),a               ;157f	ed 79
	dec b                   ;1581	05
	out (c),l               ;1582	ed 69
	inc b                   ;1584	04
	pop hl                  ;1585	e1
	out (c),h               ;1586	ed 61
	out (c),d               ;1588	ed 51
	out (c),l               ;158a	ed 69
	out (c),e               ;158c	ed 59
	ret                     ;158e	c9

L158F:
	ld de,$fc00             ;158f	11 00 fc
	call L14B6              ;1592	cd b6 14
	nextreg $34,a           ;1595	ed 92 34
	ld b,$80                ;1598	06 80

L159A:
	nextreg $78,$00         ;159a	ed 91 78 00
	djnz L159A              ;159e	10 fa
	nextreg $51,$10         ;15a0	ed 91 51 10
	ld de,$24df             ;15a4	11 df 24
	ld h,d                  ;15a7	62
	ld l,e                  ;15a8	6b
	inc de                  ;15a9	13
	ld (hl),b               ;15aa	70
	ld bc,$0820             ;15ab	01 20 08
	ldir                    ;15ae	ed b0
	nextreg $51,$ff         ;15b0	ed 91 51 ff
	jr L155B                ;15b4	18 a5

; tentative entry (word / jump table scan)
T15B6:
	xor a                   ;15b6	af

L15B7:
	push af                 ;15b7	f5
	nextreg $43,a           ;15b8	ed 92 43
	xor a                   ;15bb	af
	ld h,a                  ;15bc	67
	nextreg $40,a           ;15bd	ed 92 40

L15C0:
	ld de,$1626             ;15c0	11 26 16

L15C3:
	ld a,(de)               ;15c3	1a
	inc de                  ;15c4	13
	cp $aa                  ;15c5	fe aa
	jr z,L15C0              ;15c7	28 f7
	nextreg $41,a           ;15c9	ed 92 41
	dec h                   ;15cc	25
	jr nz,L15C3             ;15cd	20 f4
	pop af                  ;15cf	f1
	xor $40                 ;15d0	ee 40
	jr nz,L15B7             ;15d2	20 e3
	ld de,$1002             ;15d4	11 02 10

L15D7:
	ld a,d                  ;15d7	7a
	nextreg $43,a           ;15d8	ed 92 43
	xor a                   ;15db	af
	nextreg $40,a           ;15dc	ed 92 40

L15DF:
	nextreg $41,a           ;15df	ed 92 41
	inc a                   ;15e2	3c
	jr nz,L15DF             ;15e3	20 fa
	bit 6,d                 ;15e5	cb 72
	set 6,d                 ;15e7	cb f2
	jr z,L15D7              ;15e9	28 ec
	ld d,$20                ;15eb	16 20
	dec e                   ;15ed	1d
	jr nz,L15D7             ;15ee	20 e7
	set 4,(iy+$45)          ;15f0	fd cb 45 e6
	xor a                   ;15f4	af
	nextreg $43,a           ;15f5	ed 92 43
	nextreg $4a,a           ;15f8	ed 92 4a
	nextreg $4c,$0f         ;15fb	ed 91 4c 0f
	xor a                   ;15ff	af
	call L160C              ;1600	cd 0c 16
	ld a,$e3                ;1603	3e e3
	nextreg $14,a           ;1605	ed 92 14
	nextreg $4b,a           ;1608	ed 92 4b
	ret                     ;160b	c9

L160C:
	ld h,a                  ;160c	67
	and a                   ;160d	a7
	jr z,L1616              ;160e	28 06
	inc a                   ;1610	3c
	and h                   ;1611	a4
	jp nz,L30B8             ;1612	c2 b8 30
	inc a                   ;1615	3c

L1616:
	ld d,$fe                ;1616	16 fe
	ld e,a                  ;1618	5f
	ld a,$43                ;1619	3e 43
	call L14B8              ;161b	cd b8 14
	ld a,h                  ;161e	7c
	ld ($5b64),a            ;161f	32 64 5b
	nextreg $42,a           ;1622	ed 92 42
	ret                     ;1625	c9
Data1626:
	defb 0x00,0x02,0xa0,0xa2,0x14,0x16,0xb4,0xb6 ;1626	00 02 a0 a2 14 16 b4 b6	........
	defb 0x00,0x03,0xe0,0xe7,0x1c,0x1f,0xfc,0xff ;162e	00 03 e0 e7 1c 1f fc ff	........
	defb 0xaa,0x2a                               ;1636	aa 2a	.*
	ld e,l                  ;1638	5d
	ld e,h                  ;1639	5c

L163A:
	inc (iy+$0d)            ;163a	fd 34 0d
	call L3ED0              ;163d	cd d0 3e
	cp $98                  ;1640	fe 98
	jp z,L094F              ;1642	ca 4f 09

L1645:
	ld a,(hl)               ;1645	7e
	inc hl                  ;1646	23
	cp $0e                  ;1647	fe 0e
	jr z,L1668              ;1649	28 1d
	cp $3a                  ;164b	fe 3a
	jr z,L163A              ;164d	28 eb
	cp $cb                  ;164f	fe cb
	jr z,L163A              ;1651	28 e7
	cp $0d                  ;1653	fe 0d
	jr z,L1665              ;1655	28 0e
	cp $22                  ;1657	fe 22
	jr nz,L1645             ;1659	20 ea

L165B:
	ld a,(hl)               ;165b	7e
	inc hl                  ;165c	23
	cp $22                  ;165d	fe 22
	jr z,L1645              ;165f	28 e4
	cp $0d                  ;1661	fe 0d
	jr nz,L165B             ;1663	20 f6

L1665:
	jp L092D                ;1665	c3 2d 09

L1668:
	add hl,$0005            ;1668	ed 34 05 00
	jr L1645                ;166c	18 d7

L166E:
	inc (iy+$0d)            ;166e	fd 34 0d
	push bc                 ;1671	c5
	cp $cb                  ;1672	fe cb
	jr nz,L169A             ;1674	20 24
	rst $20                 ;1676	e7
	pop bc                  ;1677	c1
	bit 7,(iy+$01)          ;1678	fd cb 01 7e
	jr z,L1683              ;167c	28 05
	call L0052              ;167e	cd 52 00
	jr z,L1637              ;1681	28 b4

L1683:
	jp L091A                ;1683	c3 1a 09

L1686:
	pop bc                  ;1686	c1
	rst $20                 ;1687	e7
	bit 7,(iy+$01)          ;1688	fd cb 01 7e
	jr z,L1683              ;168c	28 f5
	call L0052              ;168e	cd 52 00
	jr nz,L1683             ;1691	20 f0
	jp L092D                ;1693	c3 2d 09
	cp $cb                  ;1696	fe cb
	jr z,L1686              ;1698	28 ec

L169A:
	ld a,($5c47)            ;169a	3a 47 5c
	dec a                   ;169d	3d
	jp nz,L173A             ;169e	c2 3a 17
	call L0902              ;16a1	cd 02 09
	call L0052              ;16a4	cd 52 00
	ret nz                  ;16a7	c0

L16A8:
	ld c,$98                ;16a8	0e 98

L16AA:
	ld b,$01                ;16aa	06 01
	ld (iy+$0d),b           ;16ac	fd 70 0d
	pop hl                  ;16af	e1

L16B0:
	ld hl,($5c55)           ;16b0	2a 55 5c
	push bc                 ;16b3	c5
	call L3975              ;16b4	cd 75 39
	pop bc                  ;16b7	c1
	jr nc,L16DF             ;16b8	30 25
	ld ($5c55),hl           ;16ba	22 55 5c
	ex de,hl                ;16bd	eb
	ld ($5c5d),hl           ;16be	22 5d 5c
	rst $20                 ;16c1	e7
	cp $84                  ;16c2	fe 84
	jr z,L16E1              ;16c4	28 1b
	cp c                    ;16c6	b9
	jr z,L16E7              ;16c7	28 1e
	cp $fa                  ;16c9	fe fa
	jr z,L16D1              ;16cb	28 04
	cp $83                  ;16cd	fe 83
	jr nz,L16B0             ;16cf	20 df

L16D1:
	push bc                 ;16d1	c5
	ld de,$02cb             ;16d2	11 cb 02
	call L1288              ;16d5	cd 88 12
	cp e                    ;16d8	bb
	pop bc                  ;16d9	c1
	jr z,L16B0              ;16da	28 d4
	inc b                   ;16dc	04
	jr nz,L16B0             ;16dd	20 d1

L16DF:
	rst $08                 ;16df	cf
	ld h,h                  ;16e0	64

L16E1:
	djnz L16B0              ;16e1	10 cd
	rst $20                 ;16e3	e7
	jp L0941                ;16e4	c3 41 09

L16E7:
	ld a,b                  ;16e7	78
	dec a                   ;16e8	3d
	jr nz,L16B0             ;16e9	20 c5
	rst $20                 ;16eb	e7
	cp $fa                  ;16ec	fe fa
	jr z,L16F4              ;16ee	28 04
	cp $83                  ;16f0	fe 83
	jr nz,L1683             ;16f2	20 8f

L16F4:
	call T0638              ;16f4	cd 38 06
	cp $cb                  ;16f7	fe cb
	jp z,L166E              ;16f9	ca 6e 16
	call L0052              ;16fc	cd 52 00
	jr nz,L1683             ;16ff	20 82
	push hl                 ;1701	e5
	jr L16A8                ;1702	18 a4
	ld hl,($5c47)           ;1704	2a 47 5c
	dec l                   ;1707	2d
	jr z,L1715              ;1708	28 0b

L170A:
	pop bc                  ;170a	c1
	bit 7,(iy+$01)          ;170b	fd cb 01 7e
	jp nz,L092D             ;170f	c2 2d 09
	jp L091A                ;1712	c3 1a 09

L1715:
	ld c,$84                ;1715	0e 84
	bit 7,(iy+$01)          ;1717	fd cb 01 7e
	jr nz,L16AA             ;171b	20 8d
	cp $fa                  ;171d	fe fa
	jr z,L1725              ;171f	28 04
	cp $83                  ;1721	fe 83
	jr nz,L170A             ;1723	20 e5

L1725:
	call T0638              ;1725	cd 38 06
	cp $cb                  ;1728	fe cb
	call nz,L0902           ;172a	c4 02 09
	rst $20                 ;172d	e7
	jr L170A                ;172e	18 da
	ld a,($5c47)            ;1730	3a 47 5c
	dec a                   ;1733	3d
	jr nz,L173A             ;1734	20 04
	call L0902              ;1736	cd 02 09
	ret                     ;1739	c9

L173A:
	rst $08                 ;173a	cf
	dec bc                  ;173b	0b

L173C:
	call L135A              ;173c	cd 5a 13
	rst $20                 ;173f	e7
	push de                 ;1740	d5
	ld d,$c0                ;1741	16 c0
	cp $29                  ;1743	fe 29
	call nz,L2008           ;1745	c4 08 20
	pop de                  ;1748	d1
	cp $29                  ;1749	fe 29
	jr nz,L173A             ;174b	20 ed
	rst $20                 ;174d	e7
	ret                     ;174e	c9
	ld de,$3140             ;174f	11 40 31

L1752:
	push de                 ;1752	d5
	call L05A3              ;1753	cd a3 05
	bit 6,(iy+$01)          ;1756	fd cb 01 76
	jr z,L173A              ;175a	28 de
	pop hl                  ;175c	e1
	push hl                 ;175d	e5
	call L3F93              ;175e	cd 93 3f
	ld b,d                  ;1761	42
	ld c,e                  ;1762	4b
	call T3840              ;1763	cd 40 38
	call T0E19              ;1766	cd 19 0e
	call nz,L2D85           ;1769	c4 85 2d
	pop de                  ;176c	d1
	ld a,e                  ;176d	7b
	cp $46                  ;176e	fe 46
	ret z                   ;1770	c8
	inc de                  ;1771	13
	inc de                  ;1772	13
	call L3EC9              ;1773	cd c9 3e
	cp $2c                  ;1776	fe 2c
	ret nz                  ;1778	c0
	rst $20                 ;1779	e7
	jr L1752                ;177a	18 d6
	cp $8f                  ;177c	fe 8f
	jr nz,L17A7             ;177e	20 27
	rst $20                 ;1780	e7
	bit 7,(iy+$01)          ;1781	fd cb 01 7e
	pop bc                  ;1785	c1
	jp z,L091F              ;1786	ca 1f 09
	push bc                 ;1789	c5
	ld hl,$0005             ;178a	21 05 00
	add hl,sp               ;178d	39
	ld a,(hl)               ;178e	7e
	cp $4b                  ;178f	fe 4b
	call z,L3BE1            ;1791	cc e1 3b
	call L3EC9              ;1794	cd c9 3e
	call T08DC              ;1797	cd dc 08
	ret z                   ;179a	c8
	call L1846              ;179b	cd 46 18
	ld a,$4b                ;179e	3e 4b
	call L3A20              ;17a0	cd 20 3a
	pop bc                  ;17a3	c1
	jp L0928                ;17a4	c3 28 09

L17A7:
	call L0639              ;17a7	cd 39 06
	cp $0d                  ;17aa	fe 0d
	jr z,L173A              ;17ac	28 8c
	call L0902              ;17ae	cd 02 09
	call L37C8              ;17b1	cd c8 37
	jr c,L17BC              ;17b4	38 06
	jr nz,L17BC             ;17b6	20 04
	ld a,b                  ;17b8	78
	and a                   ;17b9	a7
	jr z,L17BE              ;17ba	28 02

L17BC:
	ld c,$ff                ;17bc	0e ff

L17BE:
	inc c                   ;17be	0c
	ld b,c                  ;17bf	41
	ld d,b                  ;17c0	50
	ld e,$98                ;17c1	1e 98
	call L1285              ;17c3	cd 85 12
	ld ($5c5d),hl           ;17c6	22 5d 5c
	ld a,b                  ;17c9	78
	sub d                   ;17ca	92
	exx                     ;17cb	d9
	ld hl,$5c47             ;17cc	21 47 5c
	add (hl)                ;17cf	86
	ld (hl),a               ;17d0	77
	exx                     ;17d1	d9
	ld a,(hl)               ;17d2	7e
	cp $0d                  ;17d3	fe 0d
	ret z                   ;17d5	c8
	cp e                    ;17d6	bb
	call nz,L3ECF           ;17d7	c4 cf 3e
	cp e                    ;17da	bb
	exx                     ;17db	d9
	jr z,L17E0              ;17dc	28 02
	set 7,(hl)              ;17de	cb fe

L17E0:
	exx                     ;17e0	d9
	call z,L3ECF            ;17e1	cc cf 3e
	pop hl                  ;17e4	e1
	jp L091F                ;17e5	c3 1f 09
	cp $cd                  ;17e8	fe cd
	jr z,L17F7              ;17ea	28 0b
	ld a,$01                ;17ec	3e 01
	bit 7,(iy+$01)          ;17ee	fd cb 01 7e
	call nz,L383D           ;17f2	c4 3d 38
	jr L17FA                ;17f5	18 03

L17F7:
	call T0638              ;17f7	cd 38 06

L17FA:
	call L0902              ;17fa	cd 02 09
	call T2CFE              ;17fd	cd fe 2c
	call L3A20              ;1800	cd 20 3a
	cp $1d                  ;1803	fe 1d
	jr nz,L181F             ;1805	20 18
	rst $28                 ;1807	ef
	xor $1c                 ;1808	ee 1c
	ret nc                  ;180a	d0
	ld hl,$0005             ;180b	21 05 00
	add hl,sp               ;180e	39
	ld ($5b8e),hl           ;180f	22 8e 5b
	call L1819              ;1812	cd 19 18

L1815:
	call L3BF6              ;1815	cd f6 3b
	ret                     ;1818	c9

L1819:
	ld ix,$2ce9             ;1819	dd 21 e9 2c
	jr L183E                ;181d	18 1f

L181F:
	ld bc,$0034             ;181f	01 34 00
	call L18C3              ;1822	cd c3 18

L1825:
	ld bc,$0068             ;1825	01 68 00
	call L18C3              ;1828	cd c3 18
	ld bc,$0000             ;182b	01 00 00
	call L18C3              ;182e	cd c3 18
	call L18DB              ;1831	cd db 18
	ret nc                  ;1834	d0
	call L183A              ;1835	cd 3a 18
	jr L1815                ;1838	18 db

L183A:
	ld ix,$2cd2             ;183a	dd 21 d2 2c

L183E:
	ld e,$f3                ;183e	1e f3
	call L1974              ;1840	cd 74 19
	ret nc                  ;1843	d0
	rst $08                 ;1844	cf
	ld de,$18af             ;1845	11 af 18
	ld (bc),a               ;1848	02
	ld a,$01                ;1849	3e 01
	ld hl,($5c59)           ;184b	2a 59 5c
	ld de,($5c5d)           ;184e	ed 5b 5d 5c
	sbc hl,de               ;1852	ed 52
	bit 0,a                 ;1854	cb 47
	jr z,L1859              ;1856	28 01
	ccf                     ;1858	3f

L1859:
	ret nc                  ;1859	d0

L185A:
	rst $08                 ;185a	cf
	ld e,a                  ;185b	5f
	call T2CFE              ;185c	cd fe 2c
	ld c,a                  ;185f	4f
	call L3AF8              ;1860	cd f8 3a
	jr c,T18AF              ;1863	38 4a

L1865:
	push hl                 ;1865	e5
	cp $1d                  ;1866	fe 1d
	jr nz,L187D             ;1868	20 13
	ex de,hl                ;186a	eb
	push bc                 ;186b	c5
	rst $28                 ;186c	ef
	add (hl)                ;186d	86
	dec e                   ;186e	1d
	ld a,b                  ;186f	78
	pop bc                  ;1870	c1
	jr nz,L18A4             ;1871	20 31
	rst $28                 ;1873	ef
	or h                    ;1874	b4
	dec e                   ;1875	1d
	jr c,L189F              ;1876	38 27

L1878:
	pop hl                  ;1878	e1
	call L3AA8              ;1879	cd a8 3a
	ret                     ;187c	c9

L187D:
	ld de,$0000             ;187d	11 00 00
	call L18D4              ;1880	cd d4 18
	push de                 ;1883	d5
	ld de,$0034             ;1884	11 34 00
	call L18D4              ;1887	cd d4 18
	pop hl                  ;188a	e1
	add hl,de               ;188b	19
	bit 7,d                 ;188c	cb 7a
	jr z,L1891              ;188e	28 01
	ccf                     ;1890	3f

L1891:
	jr c,L189F              ;1891	38 0c
	ex de,hl                ;1893	eb
	ld bc,$0000             ;1894	01 00 00
	call L18CD              ;1897	cd cd 18
	call L18DB              ;189a	cd db 18
	jr nc,L1878             ;189d	30 d9

L189F:
	pop hl                  ;189f	e1
	call L3BE1              ;18a0	cd e1 3b
	ret                     ;18a3	c9

L18A4:
	pop hl                  ;18a4	e1
	add hl,$000b            ;18a5	ed 34 0b 00
	ld d,a                  ;18a9	57
	ld a,(hl)               ;18aa	7e
	cp c                    ;18ab	b9
	jr z,L1865              ;18ac	28 b7
	ld a,d                  ;18ae	7a

; tentative entry (word / jump table scan)
T18AF:
	ld a,c                  ;18af	79
	cp $1d                  ;18b0	fe 1d
	jr nz,T18C1             ;18b2	20 0d
	bit 1,(iy+$37)          ;18b4	fd cb 37 4e
	jp nz,L1FCD             ;18b8	c2 cd 1f
	ld hl,($5b8c)           ;18bb	2a 8c 5b
	bit 7,(hl)              ;18be	cb 7e
	ret nz                  ;18c0	c0

; tentative entry (word / jump table scan)
T18C1:
	rst $08                 ;18c1	cf
	nop                     ;18c2	00

L18C3:
	push bc                 ;18c3	c5
	call L26C5              ;18c4	cd c5 26
	pop af                  ;18c7	f1
	inc h                   ;18c8	24
	jp c,L142D              ;18c9	da 2d 14
	pop bc                  ;18cc	c1

L18CD:
	ld hl,($5c4d)           ;18cd	2a 4d 5c
	add hl,bc               ;18d0	09
	jp L3EB9                ;18d1	c3 b9 3e

L18D4:
	ld hl,($5c4d)           ;18d4	2a 4d 5c
	add hl,de               ;18d7	19
	jp L3F93                ;18d8	c3 93 3f

L18DB:
	ld de,$0068             ;18db	11 68 00
	call L18D4              ;18de	cd d4 18
	push de                 ;18e1	d5
	ld de,$0000             ;18e2	11 00 00
	call L18D4              ;18e5	cd d4 18
	push de                 ;18e8	d5
	ld de,$0034             ;18e9	11 34 00
	call L18D4              ;18ec	cd d4 18
	bit 7,d                 ;18ef	cb 7a
	pop de                  ;18f1	d1
	pop hl                  ;18f2	e1
	jr nz,L18F9             ;18f3	20 04
	and a                   ;18f5	a7
	sbc hl,de               ;18f6	ed 52
	ret                     ;18f8	c9

L18F9:
	scf                     ;18f9	37
	sbc hl,de               ;18fa	ed 52
	ccf                     ;18fc	3f
	ret                     ;18fd	c9
L18FE:
	defb 0xcd,0xcf,0x3e,0xfe,0x85,0x28,0x0d,0xfe ;18fe	cd cf 3e fe 85 28 0d fe	..>..(..
	defb 0x98,0xc2,0x9d,0x09,0xed,0x53,0x5d,0x5c ;1906	98 c2 9d 09 ed 53 5d 5c	.....S]\
	defb 0xcd                                    ;190e	cd	.
	ld (bc),a               ;190f	02
	add hl,bc               ;1910	09
	ret                     ;1911	c9
Data1912:
	defb 0xe7                                    ;1912	e7	.
	bit 7,(iy+$01)          ;1913	fd cb 01 7e
	jr z,L1924              ;1917	28 0b
	ld c,$20                ;1919	0e 20
	call L3AF8              ;191b	cd f8 3a
	ld a,(hl)               ;191e	7e
	cp $1f                  ;191f	fe 1f
	jp nc,L1A03             ;1921	d2 03 1a

L1924:
	ex de,hl                ;1924	eb
	call L3EC9              ;1925	cd c9 3e
	call T08DC              ;1928	cd dc 08
	ld ($5c5f),hl           ;192b	22 5f 5c
	ex de,hl                ;192e	eb
	jp nz,L2075             ;192f	c2 75 20
	bit 7,(iy+$01)          ;1932	fd cb 01 7e
	jr z,L1964              ;1936	28 2c
	push hl                 ;1938	e5
	ld ($5b8e),hl           ;1939	22 8e 5b
	call L3AA8              ;193c	cd a8 3a
	pop hl                  ;193f	e1
	push hl                 ;1940	e5
	ld a,(hl)               ;1941	7e
	cp $1e                  ;1942	fe 1e
	jr nz,L194B             ;1944	20 05
	call L1A1C              ;1946	cd 1c 1a
	jr L1960                ;1949	18 15

L194B:
	cp $1d                  ;194b	fe 1d
	jr nz,L1954             ;194d	20 05
	call L1819              ;194f	cd 19 18
	jr L1960                ;1952	18 0c

L1954:
	add a                   ;1954	87
	ld hl,$325e             ;1955	21 5e 32
	add hl,a                ;1958	ed 31
	ld ($5c4d),hl           ;195a	22 4d 5c
	call L183A              ;195d	cd 3a 18

L1960:
	pop hl                  ;1960	e1
	call L3BE1              ;1961	cd e1 3b

L1964:
	ld de,($5c5d)           ;1964	ed 5b 5d 5c
	ld hl,($5c5f)           ;1968	2a 5f 5c
	ld a,(hl)               ;196b	7e
	cp $3a                  ;196c	fe 3a
	jr z,L18FE              ;196e	28 8e
	call L0902              ;1970	cd 02 09
	ret                     ;1973	c9

L1974:
	ld hl,($5c45)           ;1974	2a 45 5c
	ld ($5c42),hl           ;1977	22 42 5c
	ld a,($5c47)            ;197a	3a 47 5c
	neg                     ;197d	ed 44
	ld d,a                  ;197f	57

L1980:
	call L3EC9              ;1980	cd c9 3e
	call L19AC              ;1983	cd ac 19

; tentative entry (word / jump table scan)
T1986:
	ret c                   ;1986	d8

L1987:
	call L19AA              ;1987	cd aa 19
	jr nc,L199D             ;198a	30 11
	call L3EC9              ;198c	cd c9 3e
	ld c,$00                ;198f	0e 00
	call L12A2              ;1991	cd a2 12
	ld ($5c5d),hl           ;1994	22 5d 5c
	jr nc,L1987             ;1997	30 ee
	ld d,$ff                ;1999	16 ff
	jr L1980                ;199b	18 e3

L199D:
	xor a                   ;199d	af
	sub d                   ;199e	92
	ld ($5c47),a            ;199f	32 47 5c
	ld hl,($5c42)           ;19a2	2a 42 5c
	ld ($5c45),hl           ;19a5	22 45 5c
	and a                   ;19a8	a7
	ret                     ;19a9	c9

L19AA:
	jp (ix)                 ;19aa	dd e9

L19AC:
	cp $3a                  ;19ac	fe 3a
	jr z,L19E9              ;19ae	28 39

L19B0:
	ld a,($5b77)            ;19b0	3a 77 5b
	inc a                   ;19b3	3c
	jr z,L19CC              ;19b4	28 16
	push de                 ;19b6	d5
	dec a                   ;19b7	3d
	ld hl,($5c55)           ;19b8	2a 55 5c
	call L3905              ;19bb	cd 05 39
	pop bc                  ;19be	c1
	ccf                     ;19bf	3f
	ret c                   ;19c0	d8
	res 7,h                 ;19c1	cb bc
	res 6,h                 ;19c3	cb b4
	ld ($5c55),hl           ;19c5	22 55 5c
	ex de,hl                ;19c8	eb
	ld e,c                  ;19c9	59
	jr L19DE                ;19ca	18 12

L19CC:
	inc hl                  ;19cc	23
	ld a,(hl)               ;19cd	7e
	and $c0                 ;19ce	e6 c0
	scf                     ;19d0	37
	ret nz                  ;19d1	c0
	push hl                 ;19d2	e5
	inc hl                  ;19d3	23
	inc hl                  ;19d4	23
	ld c,(hl)               ;19d5	4e
	inc hl                  ;19d6	23
	ld b,(hl)               ;19d7	46
	inc hl                  ;19d8	23
	add hl,bc               ;19d9	09
	ld ($5c55),hl           ;19da	22 55 5c
	pop hl                  ;19dd	e1

L19DE:
	ld b,(hl)               ;19de	46
	inc hl                  ;19df	23
	ld c,(hl)               ;19e0	4e
	inc hl                  ;19e1	23
	ld ($5c42),bc           ;19e2	ed 43 42 5c
	inc hl                  ;19e6	23
	ld d,$00                ;19e7	16 00

L19E9:
	call L1288              ;19e9	cd 88 12
	ld ($5c5d),hl           ;19ec	22 5d 5c
	ret nc                  ;19ef	d0
	jr L19B0                ;19f0	18 be
	ld a,$1e                ;19f2	3e 1e
	call L3A20              ;19f4	cd 20 3a
	ret                     ;19f7	c9
	call L0052              ;19f8	cd 52 00
	ld c,$1e                ;19fb	0e 1e
	jr nz,L1A05             ;19fd	20 06
	call L3AA4              ;19ff	cd a4 3a
	ret nc                  ;1a02	d0

L1A03:
	rst $08                 ;1a03	cf
	ld h,b                  ;1a04	60

L1A05:
	call L3B02              ;1a05	cd 02 3b
	jr c,L1A03              ;1a08	38 f9
	call L3BE1              ;1a0a	cd e1 3b
	ret                     ;1a0d	c9
Data1A0E:
	defb 0xcd                                    ;1a0e	cd	.
	ld d,d                  ;1a0f	52
	nop                     ;1a10	00
	ret nz                  ;1a11	c0
	ld c,$1e                ;1a12	0e 1e
	call L3B02              ;1a14	cd 02 3b
	jr c,L1A03              ;1a17	38 ea
	call L3BE1              ;1a19	cd e1 3b

L1A1C:
	ld (iy+$58),$01         ;1a1c	fd 36 58 01
	ld e,$97                ;1a20	1e 97
	ld ix,$2cc2             ;1a22	dd 21 c2 2c
	call L1974              ;1a26	cd 74 19
	jr c,L1A03              ;1a29	38 d8
	jp L35D1                ;1a2b	c3 d1 35
	call T0E19              ;1a2e	cd 19 0e
	jp nz,L35D1             ;1a31	c2 d1 35
	call L0059              ;1a34	cd 59 00
	bit 2,e                 ;1a37	cb 53
	jr nz,L1A53             ;1a39	20 18
	call L173C              ;1a3b	cd 3c 17
	cp $3d                  ;1a3e	fe 3d
	jr nz,L1A4E             ;1a40	20 0c
	rst $20                 ;1a42	e7
	push de                 ;1a43	d5
	call L0E2D              ;1a44	cd 2d 0e
	pop de                  ;1a47	d1
	ld a,($5c3b)            ;1a48	3a 3b 5c
	xor d                   ;1a4b	aa
	and $40                 ;1a4c	e6 40

L1A4E:
	jp nz,L173A             ;1a4e	c2 3a 17
	jr L1AB4                ;1a51	18 61

L1A53:
	set 6,(iy+$01)          ;1a53	fd cb 01 f6
	call L387F              ;1a57	cd 7f 38
	jr nc,L1A72             ;1a5a	30 16
	rst $20                 ;1a5c	e7
	cp $24                  ;1a5d	fe 24
	jr nz,L1A66             ;1a5f	20 05
	res 6,(iy+$01)          ;1a61	fd cb 01 b6
	rst $20                 ;1a65	e7

L1A66:
	cp $28                  ;1a66	fe 28
	jr nz,L1A4E             ;1a68	20 e4
	rst $20                 ;1a6a	e7
	cp $29                  ;1a6b	fe 29
	jr z,L1A8F              ;1a6d	28 20

L1A6F:
	call L387F              ;1a6f	cd 7f 38

L1A72:
	jp nc,L173A             ;1a72	d2 3a 17
	ex de,hl                ;1a75	eb
	rst $20                 ;1a76	e7
	cp $24                  ;1a77	fe 24
	jr nz,L1A7D             ;1a79	20 02
	ex de,hl                ;1a7b	eb
	rst $20                 ;1a7c	e7

L1A7D:
	ex de,hl                ;1a7d	eb
	ld bc,$0006             ;1a7e	01 06 00
	rst $28                 ;1a81	ef
	ld d,l                  ;1a82	55
	ld d,$23                ;1a83	16 23
	inc hl                  ;1a85	23
	ld (hl),$0e             ;1a86	36 0e
	cp $2c                  ;1a88	fe 2c
	jr nz,L1A8F             ;1a8a	20 03
	rst $20                 ;1a8c	e7
	jr L1A6F                ;1a8d	18 e0

L1A8F:
	cp $29                  ;1a8f	fe 29
	jr nz,L1A4E             ;1a91	20 bb
	rst $20                 ;1a93	e7
	cp $3d                  ;1a94	fe 3d
	jr nz,L1A4E             ;1a96	20 b6
	rst $20                 ;1a98	e7
	ld a,($5c3b)            ;1a99	3a 3b 5c
	push af                 ;1a9c	f5
	call L0E2D              ;1a9d	cd 2d 0e
	pop af                  ;1aa0	f1
	xor (iy+$01)            ;1aa1	fd ae 01
	and $40                 ;1aa4	e6 40
	jr nz,L1A4E             ;1aa6	20 a6
	call L0902              ;1aa8	cd 02 09
	call T0E19              ;1aab	cd 19 0e
	jp nz,L185A             ;1aae	c2 5a 18
	call L173C              ;1ab1	cd 3c 17

L1AB4:
	call L0902              ;1ab4	cd 02 09
	bit 7,(iy+$01)          ;1ab7	fd cb 01 7e
	jr nz,L1AC9             ;1abb	20 0c
	cp $3d                  ;1abd	fe 3d
	jr nz,L1AB4             ;1abf	20 f3
	rst $20                 ;1ac1	e7
	ld d,$80                ;1ac2	16 80
	call L1FEA              ;1ac4	cd ea 1f
	jr L1AB4                ;1ac7	18 eb

L1AC9:
	cp $3d                  ;1ac9	fe 3d
	call z,L0020            ;1acb	cc 20 00
	ld a,($5b77)            ;1ace	3a 77 5b
	inc a                   ;1ad1	3c
	call nz,L39C3           ;1ad2	c4 c3 39
	ld ($5c5f),hl           ;1ad5	22 5f 5c
	ld hl,($5b58)           ;1ad8	2a 58 5b
	ld ($5b8c),hl           ;1adb	22 8c 5b
	ld c,$22                ;1ade	0e 22
	call L3AA4              ;1ae0	cd a4 3a
	jr c,L1B3B              ;1ae3	38 56
	ld ($5b94),sp           ;1ae5	ed 73 94 5b
	call L1B5D              ;1ae9	cd 5d 1b
	ld a,(hl)               ;1aec	7e
	cp $cc                  ;1aed	fe cc
	jr nz,L1B37             ;1aef	20 46

L1AF1:
	rst $20                 ;1af1	e7
	call L05A3              ;1af2	cd a3 05
	call L2CA6              ;1af5	cd a6 2c
	ld a,($5c3b)            ;1af8	3a 3b 5c
	push af                 ;1afb	f5
	call L0E2D              ;1afc	cd 2d 0e
	cp $2c                  ;1aff	fe 2c
	call z,L3ECF            ;1b01	cc cf 3e
	call L2CA6              ;1b04	cd a6 2c
	ld d,(iy+$01)           ;1b07	fd 56 01
	pop af                  ;1b0a	f1
	xor d                   ;1b0b	aa
	and $40                 ;1b0c	e6 40
	jp nz,L1C6D             ;1b0e	c2 6d 1c
	bit 2,(iy+$30)          ;1b11	fd cb 30 56
	jr nz,L1B3D             ;1b15	20 26

L1B17:
	call L2D85              ;1b17	cd 85 2d
	ld hl,($5b94)           ;1b1a	2a 94 5b
	and a                   ;1b1d	a7
	sbc hl,sp               ;1b1e	ed 72
	jr z,L1B30              ;1b20	28 0e
	ld ($5b94),sp           ;1b22	ed 73 94 5b
	ex de,hl                ;1b26	eb
	ld hl,($5b8c)           ;1b27	2a 8c 5b
	and a                   ;1b2a	a7
	sbc hl,de               ;1b2b	ed 52
	ld ($5b8c),hl           ;1b2d	22 8c 5b

L1B30:
	call L3EC9              ;1b30	cd c9 3e
	cp $2c                  ;1b33	fe 2c
	jr z,L1AF1              ;1b35	28 ba

L1B37:
	call L3A6A              ;1b37	cd 6a 3a
	ret                     ;1b3a	c9

L1B3B:
	rst $08                 ;1b3b	cf
	ld h,c                  ;1b3c	61

L1B3D:
	ld hl,($5c4d)           ;1b3d	2a 4d 5c
	ld de,$3264             ;1b40	11 64 32
	sbc hl,de               ;1b43	ed 52
	ld a,h                  ;1b45	7c
	and a                   ;1b46	a7
	jr nz,L1B17             ;1b47	20 ce
	ld a,l                  ;1b49	7d
	rra                     ;1b4a	1f
	cp $1a                  ;1b4b	fe 1a
	jr nc,L1B17             ;1b4d	30 c8
	push af                 ;1b4f	f5
	call L26C5              ;1b50	cd c5 26
	pop af                  ;1b53	f1
	inc h                   ;1b54	24
	pop af                  ;1b55	f1
	call L26C5              ;1b56	cd c5 26
	xor h                   ;1b59	ac
	jr z,L1B74              ;1b5a	28 18
	out ($cd),a             ;1b5c	d3 cd
	ret                     ;1b5e	c9
Data1B5F:
	defb 0x3e,0x01,0x01,0x01                     ;1b5f	3e 01 01 01	>...

L1B63:
	dec c                   ;1b63	0d

L1B64:
	ld a,(hl)               ;1b64	7e
	inc hl                  ;1b65	23
	cp $22                  ;1b66	fe 22
	jr z,L1B63              ;1b68	28 f9
	bit 0,c                 ;1b6a	cb 41
	jr nz,L1B64             ;1b6c	20 f6
	cp $29                  ;1b6e	fe 29
	jr z,L1B80              ;1b70	28 0e
	cp $28                  ;1b72	fe 28

L1B74:
	jr z,L1B86              ;1b74	28 10
	cp $0e                  ;1b76	fe 0e
	jr nz,L1B64             ;1b78	20 ea
	add hl,$0005            ;1b7a	ed 34 05 00
	jr L1B64                ;1b7e	18 e4

L1B80:
	djnz L1B64              ;1b80	10 e2
	ld ($5c5d),hl           ;1b82	22 5d 5c
	ret                     ;1b85	c9

L1B86:
	inc b                   ;1b86	04
	jr L1B64                ;1b87	18 db
	bit 7,(iy+$01)          ;1b89	fd cb 01 7e
	push de                 ;1b8d	d5
	jr z,L1B94              ;1b8e	28 04
	inc d                   ;1b90	14
	call nz,L3894           ;1b91	c4 94 38

L1B94:
	pop af                  ;1b94	f1
	ld ($5b65),a            ;1b95	32 65 5b
	bit 7,(iy+$01)          ;1b98	fd cb 01 7e
	jr nz,L1BBF             ;1b9c	20 21
	call L135A              ;1b9e	cd 5a 13
	ex af,af'               ;1ba1	08
	push af                 ;1ba2	f5
	push de                 ;1ba3	d5
	rst $20                 ;1ba4	e7
	cp $29                  ;1ba5	fe 29
	ld d,$00                ;1ba7	16 00
	call nz,L1FEA           ;1ba9	c4 ea 1f
	cp $29                  ;1bac	fe 29
	jp nz,L173A             ;1bae	c2 3a 17
	rst $20                 ;1bb1	e7
	ex af,af'               ;1bb2	08
	pop de                  ;1bb3	d1
	pop af                  ;1bb4	f1
	cp $23                  ;1bb5	fe 23
	ret z                   ;1bb7	c8
	ex af,af'               ;1bb8	08
	cp $cc                  ;1bb9	fe cc
	call z,L2005            ;1bbb	cc 05 20
	ret                     ;1bbe	c9

L1BBF:
	pop hl                  ;1bbf	e1
	ld ($5b97),hl           ;1bc0	22 97 5b
	call L26C5              ;1bc3	cd c5 26
	add a                   ;1bc6	87
	jr z,L1BD1              ;1bc7	28 08
	call L12CD              ;1bc9	cd cd 12
	ld ($5b8e),hl           ;1bcc	22 8e 5b
	ld ($5b90),de           ;1bcf	ed 53 90 5b
	ld ($5b96),a            ;1bd3	32 96 5b
	rst $20                 ;1bd6	e7

L1BD7:
	inc hl                  ;1bd7	23
	ld a,(hl)               ;1bd8	7e
	cp $0e                  ;1bd9	fe 0e
	jp z,T1DD8              ;1bdb	ca d8 1d
	cp $0d                  ;1bde	fe 0d
	jr z,L1BEA              ;1be0	28 08
	cp $21                  ;1be2	fe 21
	jr c,L1BD7              ;1be4	38 f1
	cp $24                  ;1be6	fe 24
	jr z,L1BD7              ;1be8	28 ed

L1BEA:
	call L3EC9              ;1bea	cd c9 3e

L1BED:
	cp $86                  ;1bed	fe 86
	jr nz,L1C37             ;1bef	20 46
	rst $20                 ;1bf1	e7
	call L2CB4              ;1bf2	cd b4 2c
	cp $25                  ;1bf5	fe 25
	jr z,L1C6D              ;1bf7	28 74
	cp $22                  ;1bf9	fe 22
	jr z,L1C6D              ;1bfb	28 70
	push hl                 ;1bfd	e5
	ld bc,$28b2             ;1bfe	01 b2 28
	call L3EC1              ;1c01	cd c1 3e
	jp c,L1FCD              ;1c04	da cd 1f
	call L3EC9              ;1c07	cd c9 3e
	pop de                  ;1c0a	d1
	sbc hl,de               ;1c0b	ed 52
	inc hl                  ;1c0d	23
	push bc                 ;1c0e	c5
	ld b,h                  ;1c0f	44
	ld c,l                  ;1c10	4d
	call L384F              ;1c11	cd 4f 38
	pop de                  ;1c14	d1
	ld d,$90                ;1c15	16 90
	ld ($5ca6),de           ;1c17	ed 53 a6 5c
	cp $28                  ;1c1b	fe 28
	jr nz,L1C23             ;1c1d	20 04
	rst $20                 ;1c1f	e7
	rst $20                 ;1c20	e7
	set 0,d                 ;1c21	cb c2

L1C23:
	cp $2c                  ;1c23	fe 2c
	call z,L0020            ;1c25	cc 20 00
	call L2CB4              ;1c28	cd b4 2c
	bit 6,e                 ;1c2b	cb 73
	jr z,L1C33              ;1c2d	28 04
	bit 5,e                 ;1c2f	cb 6b
	jr z,L1C35              ;1c31	28 02

L1C33:
	set 6,d                 ;1c33	cb f2

L1C35:
	jr L1C87                ;1c35	18 50

L1C37:
	cp $e4                  ;1c37	fe e4
	jr z,L1C97              ;1c39	28 5c
	call L2CB4              ;1c3b	cd b4 2c
	cp $29                  ;1c3e	fe 29
	jr z,L1C47              ;1c40	28 05
	cp $2c                  ;1c42	fe 2c
	jr nz,L1C53             ;1c44	20 0d
	rst $20                 ;1c46	e7

L1C47:
	call L2CB4              ;1c47	cd b4 2c
	cp $29                  ;1c4a	fe 29
	jp z,L1CD6              ;1c4c	ca d6 1c
	ld d,$20                ;1c4f	16 20
	jr L1C87                ;1c51	18 34

L1C53:
	call L1F9B              ;1c53	cd 9b 1f
	jr nc,L1C6F             ;1c56	30 17
	ld d,$85                ;1c58	16 85
	jr z,L1C67              ;1c5a	28 0b
	inc hl                  ;1c5c	23
	ld c,(hl)               ;1c5d	4e
	inc hl                  ;1c5e	23
	ld b,(hl)               ;1c5f	46
	inc hl                  ;1c60	23
	ex de,hl                ;1c61	eb
	call L384F              ;1c62	cd 4f 38
	ld d,$81                ;1c65	16 81

L1C67:
	rst $20                 ;1c67	e7
	jr L1C74                ;1c68	18 0a

L1C6A:
	call L2CB4              ;1c6a	cd b4 2c

L1C6D:
	rst $08                 ;1c6d	cf
	add hl,de               ;1c6e	19

L1C6F:
	call L0E2D              ;1c6f	cd 2d 0e
	ld d,$80                ;1c72	16 80

L1C74:
	cp $2c                  ;1c74	fe 2c
	call z,L3ECF            ;1c76	cc cf 3e
	call L2CB4              ;1c79	cd b4 2c
	cp $29                  ;1c7c	fe 29
	jr z,L1C6A              ;1c7e	28 ea
	ld a,($5c3b)            ;1c80	3a 3b 5c
	and $40                 ;1c83	e6 40
	or d                    ;1c85	b2
	ld d,a                  ;1c86	57

L1C87:
	call L3C00              ;1c87	cd 00 3c
	jr nc,L1C6A             ;1c8a	30 de
	call L3EC9              ;1c8c	cd c9 3e
	cp $2c                  ;1c8f	fe 2c
	jr nz,L1CD6             ;1c91	20 43
	rst $20                 ;1c93	e7
	jp L1BED                ;1c94	c3 ed 1b

L1C97:
	rst $20                 ;1c97	e7
	pop de                  ;1c98	d1
	pop bc                  ;1c99	c1
	exx                     ;1c9a	d9
	ld a,($5b78)            ;1c9b	3a 78 5b
	ld c,a                  ;1c9e	4f
	ld hl,($5c57)           ;1c9f	2a 57 5c
	inc a                   ;1ca2	3c
	jr nz,L1CAC             ;1ca3	20 07
	ld de,($5c53)           ;1ca5	ed 5b 53 5c
	and a                   ;1ca9	a7
	sbc hl,de               ;1caa	ed 52

L1CAC:
	push hl                 ;1cac	e5
	ld a,($5c6a)            ;1cad	3a 6a 5c
	and $80                 ;1cb0	e6 80
	push af                 ;1cb2	f5
	inc sp                  ;1cb3	33
	ld b,$45                ;1cb4	06 45
	push bc                 ;1cb6	c5
	exx                     ;1cb7	d9
	push bc                 ;1cb8	c5
	ld ($5c3d),sp           ;1cb9	ed 73 3d 5c
	push de                 ;1cbd	d5
	call L2CB4              ;1cbe	cd b4 2c
	ld a,($5b77)            ;1cc1	3a 77 5b
	ld ($5b78),a            ;1cc4	32 78 5b

L1CC7:
	dec hl                  ;1cc7	2b
	ld a,(hl)               ;1cc8	7e
	cp $21                  ;1cc9	fe 21
	jr c,L1CC7              ;1ccb	38 fa
	ld ($5c57),hl           ;1ccd	22 57 5c
	set 7,(iy+$30)          ;1cd0	fd cb 30 fe
	jr L1CDD                ;1cd4	18 07

L1CD6:
	call L2CB4              ;1cd6	cd b4 2c

L1CD9:
	cp $29                  ;1cd9	fe 29
	jr nz,L1C6D             ;1cdb	20 90

L1CDD:
	call L2103              ;1cdd	cd 03 21
	call L2CB4              ;1ce0	cd b4 2c
	rst $20                 ;1ce3	e7
	ld a,($5b65)            ;1ce4	3a 65 5b
	call L38AD              ;1ce7	cd ad 38
	ld hl,($5b90)           ;1cea	2a 90 5b
	ld ($5c45),hl           ;1ced	22 45 5c
	ld hl,($5b8e)           ;1cf0	2a 8e 5b
	ld ($5c55),hl           ;1cf3	22 55 5c
	ld a,($5b96)            ;1cf6	3a 96 5b
	ld ($5c47),a            ;1cf9	32 47 5c
	ld hl,($5b97)           ;1cfc	2a 97 5b
	push hl                 ;1cff	e5
	bit 7,(iy+$01)          ;1d00	fd cb 01 7e
	ret z                   ;1d04	c8

L1D05:
	call L26C5              ;1d05	cd c5 26
	cp $28                  ;1d08	fe 28

L1D0A:
	ld hl,($5c3d)           ;1d0a	2a 3d 5c
	inc hl                  ;1d0d	23
	inc hl                  ;1d0e	23
	ld ($5b58),hl           ;1d0f	22 58 5b
	ret                     ;1d12	c9
	bit 7,(iy+$01)          ;1d13	fd cb 01 7e
	call nz,L37FC           ;1d17	c4 fc 37
	jr L1D1F                ;1d1a	18 03
Data1D1C:
	defb 0x3a,0x77,0x5b,0x57,0x3e,0x22,0x08,0xcd ;1d1c	3a 77 5b 57 3e 22 08 cd	:w[W>"..
	defb 0x89,0x1b,0xcd,0x02                     ;1d24	89 1b cd 02	....
	add hl,bc               ;1d28	09
	ret                     ;1d29	c9
Text1D2A:
	defb 0xfd,0xcb,0x01,0x7e,0x28,0x3b,0xd9,0x2a ;1d2a	fd cb 01 7e 28 3b d9 2a	...~(;.*
	defb 0x58,0x5b,0xe5,0x3a,0x65,0x5b,0xf5,0x2a ;1d32	58 5b e5 3a 65 5b f5 2a	X[.:e[.*
	defb 0x5f,0x5c,0xe5,0x2a,0x0b,0x5c,0xe5,0x21 ;1d3a	5f 5c e5 2a 0b 5c e5 21	_\.*.\.!
	defb 0xf1,0xff,0x39,0xf9,0xe5,0xeb,0x21,0x8a ;1d42	f1 ff 39 f9 e5 eb 21 8a	..9...!.
	defb 0x5b,0x01,0x0f,0x00,0xed,0xb0,0xc1,0x2e ;1d4a	5b 01 0f 00 ed b0 c1 2e	[.......
	defb 0xfe,0xe5,0xaf,0x2a,0x58,0x5b,0x0b,0x0b ;1d52	fe e5 af 2a 58 5b 0b 0b	...*X[..
	defb 0xed,0x42,0xe5,0xf5,0x21,0x3a,0x5b,0xe5 ;1d5a	ed 42 e5 f5 21 3a 5b e5	.B..!:[.
	defb 0x2a,0x3d,0x5c,0xed,0x73,0x3d,0x5c,0xe5 ;1d62	2a 3d 5c ed 73 3d 5c e5	*=\.s=\.
	defb 0xd9,0x3e,0x23,0x08,0xe7,0xcd,0x89,0x1b ;1d6a	d9 3e 23 08 e7 cd 89 1b	.>#.....
	defb 0xfd,0xcb,0x01,0x7e,0x28,0x4c,0xe7,0xcd ;1d72	fd cb 01 7e 28 4c e7 cd	...~(L..
	defb 0x2d,0x0e,0xfd,0xcb,0x01,0x76,0x20,0x18 ;1d7a	2d 0e fd cb 01 76 20 18	-....v .
	defb 0xcd,0x1b,0x38,0x2a,0x65,0x5c,0xed,0x52 ;1d82	cd 1b 38 2a 65 5c ed 52	..8*e\.R
	defb 0x30,0x0b,0xc5,0xd5,0xef,0x30,0x00,0xe1 ;1d8a	30 0b c5 d5 ef 30 00 e1	0....0..
	defb 0xd5,0xed,0xb0,0xd1,0xc1,0xcd,0x37,0x38 ;1d92	d5 ed b0 d1 c1 cd 37 38	......78
	defb 0x0e,0x23,0xcd,0x66,0x3a,0xcd,0x5d,0x1b ;1d9a	0e 23 cd 66 3a cd 5d 1b	.#.f:.].
	defb 0xe1,0x22,0x3d,0x5c,0x21,0x08,0x00,0x39 ;1da2	e1 22 3d 5c 21 08 00 39	."=\!..9
	defb 0x11,0x8a,0x5b,0x01,0x0f,0x00,0xed,0xb0 ;1daa	11 8a 5b 01 0f 00 ed b0	..[.....
	defb 0xf9,0xe1,0x22,0x0b,0x5c,0xe1,0x22,0x5f ;1db2	f9 e1 22 0b 5c e1 22 5f	..".\."_
	defb 0x5c,0xf1,0x32,0x65,0x5b,0xe1,0x22,0x58 ;1dba	5c f1 32 65 5b e1 22 58	\.2e[."X
	defb 0x5b,0xc9,0x21,0x3b,0x5c,0x7e,0xe6,0xbf ;1dc2	5b c9 21 3b 5c 7e e6 bf	[.!;\~..
	defb 0xb2,0x77,0xc9                          ;1dca	b2 77 c9	.w.

L1DCD:
	push hl                 ;1dcd	e5
	ld c,$23                ;1dce	0e 23
	call L3AF8              ;1dd0	cd f8 3a
	call nc,L3A66           ;1dd3	d4 66 3a
	pop hl                  ;1dd6	e1
	ret                     ;1dd7	c9

; tentative entry (word / jump table scan)
T1DD8:
	call L3EC9              ;1dd8	cd c9 3e
	push hl                 ;1ddb	e5
	call L2CB4              ;1ddc	cd b4 2c
	ld d,a                  ;1ddf	57
	pop hl                  ;1de0	e1
	push hl                 ;1de1	e5
	ld a,(hl)               ;1de2	7e
	cp $29                  ;1de3	fe 29
	jr z,L1E32              ;1de5	28 4b
	ld a,d                  ;1de7	7a
	cp $29                  ;1de8	fe 29
	jr z,L1E30              ;1dea	28 44

L1DEC:
	inc hl                  ;1dec	23
	ld a,(hl)               ;1ded	7e
	cp $0e                  ;1dee	fe 0e
	ld d,$40                ;1df0	16 40
	jr z,L1DFB              ;1df2	28 07
	dec hl                  ;1df4	2b
	call L1E3F              ;1df5	cd 3f 1e
	inc hl                  ;1df8	23
	ld d,$00                ;1df9	16 00

L1DFB:
	inc hl                  ;1dfb	23
	push hl                 ;1dfc	e5
	push de                 ;1dfd	d5
	call L0E2D              ;1dfe	cd 2d 0e
	pop af                  ;1e01	f1
	xor (iy+$01)            ;1e02	fd ae 01
	and $40                 ;1e05	e6 40
	jr nz,L1E30             ;1e07	20 27
	pop hl                  ;1e09	e1
	ex de,hl                ;1e0a	eb
	ld hl,($5c65)           ;1e0b	2a 65 5c
	ld bc,$0005             ;1e0e	01 05 00
	sbc hl,bc               ;1e11	ed 42
	ld ($5c65),hl           ;1e13	22 65 5c
	ldir                    ;1e16	ed b0
	ex de,hl                ;1e18	eb
	dec hl                  ;1e19	2b
	call L1E3F              ;1e1a	cd 3f 1e
	cp $29                  ;1e1d	fe 29
	jr z,L1E32              ;1e1f	28 11
	push hl                 ;1e21	e5
	call L3EC9              ;1e22	cd c9 3e
	cp $2c                  ;1e25	fe 2c
	jr nz,L1E30             ;1e27	20 07
	rst $20                 ;1e29	e7
	pop hl                  ;1e2a	e1
	call L1E3F              ;1e2b	cd 3f 1e
	jr L1DEC                ;1e2e	18 bc

L1E30:
	rst $08                 ;1e30	cf
	add hl,de               ;1e31	19

L1E32:
	ld ($5c5f),hl           ;1e32	22 5f 5c
	pop hl                  ;1e35	e1
	ld ($5c0b),hl           ;1e36	22 0b 5c
	call L3EC9              ;1e39	cd c9 3e

L1E3C:
	jp L1CD9                ;1e3c	c3 d9 1c

L1E3F:
	inc hl                  ;1e3f	23
	ld a,(hl)               ;1e40	7e
	cp $21                  ;1e41	fe 21
	jr c,L1E3F              ;1e43	38 fa
	ret                     ;1e45	c9

; tentative entry (word / jump table scan)
T1E46:
	bit 7,(iy+$01)          ;1e46	fd cb 01 7e
	jr nz,L1E54             ;1e4a	20 08
	ld d,$80                ;1e4c	16 80
	call L2008              ;1e4e	cd 08 20
	call L0902              ;1e51	cd 02 09

L1E54:
	call L1E6C              ;1e54	cd 6c 1e
	call L26C5              ;1e57	cd c5 26
	add a                   ;1e5a	87
	jr z,L1E73              ;1e5b	28 16
	ld (bc),a               ;1e5d	02
	call L3C00              ;1e5e	cd 00 3c
	call L3EC9              ;1e61	cd c9 3e
	cp $2c                  ;1e64	fe 2c
	jp nz,L1D05             ;1e66	c2 05 1d
	rst $20                 ;1e69	e7
	jr L1E5C                ;1e6a	18 f0
L1E6C:
	defb 0x0e                                    ;1e6c	0e	.

L1E6D:
	jr nz,L1E3C             ;1e6d	20 cd
	ret m                   ;1e6f	f8
	ld a,($fe7e)            ;1e70	3a 7e fe

L1E73:
	rra                     ;1e73	1f
	jp c,L1A03              ;1e74	da 03 1a
	call L26C5              ;1e77	cd c5 26
	sbc c                   ;1e7a	99
	jr z,T1E46              ;1e7b	28 c9
	cp $fd                  ;1e7d	fe fd
	jp z,L1F14              ;1e7f	ca 14 1f
	bit 7,(iy+$01)          ;1e82	fd cb 01 7e
	jr nz,L1EBC             ;1e86	20 34

L1E88:
	call L3EC9              ;1e88	cd c9 3e
	ld bc,$0006             ;1e8b	01 06 00
	rst $28                 ;1e8e	ef
	ld d,l                  ;1e8f	55
	ld d,$23                ;1e90	16 23
	ld (hl),$0e             ;1e92	36 0e
	ld bc,$0500             ;1e94	01 00 05

L1E97:
	inc hl                  ;1e97	23
	ld (hl),c               ;1e98	71
	djnz L1E97              ;1e99	10 fc
	call L3ECF              ;1e9b	cd cf 3e
	rst $28                 ;1e9e	ef
	or a                    ;1e9f	b7
	jr z,L1E6D              ;1ea0	28 cb
	ld l,c                  ;1ea2	69
	jp z,L099D              ;1ea3	ca 9d 09
	call L3EC9              ;1ea6	cd c9 3e
	cp $3d                  ;1ea9	fe 3d
	call z,T0638            ;1eab	cc 38 06
	call T08DC              ;1eae	cd dc 08
	call z,L0902            ;1eb1	cc 02 09
	cp $2c                  ;1eb4	fe 2c
	jp nz,L173A             ;1eb6	c2 3a 17
	rst $20                 ;1eb9	e7
	jr L1E88                ;1eba	18 cc

L1EBC:
	cp $0e                  ;1ebc	fe 0e
	jp nz,L173A             ;1ebe	c2 3a 17
	push hl                 ;1ec1	e5
	call L1846              ;1ec2	cd 46 18
	call L1E6C              ;1ec5	cd 6c 1e
	pop hl                  ;1ec8	e1
	inc hl                  ;1ec9	23
	push hl                 ;1eca	e5
	ld a,($5b77)            ;1ecb	3a 77 5b
	ld d,a                  ;1ece	57
	ld e,$00                ;1ecf	1e 00
	ld ($5ca6),de           ;1ed1	ed 53 a6 5c
	inc a                   ;1ed5	3c
	jr nz,L1F0E             ;1ed6	20 36
	ld de,($5c53)           ;1ed8	ed 5b 53 5c
	and a                   ;1edc	a7
	sbc hl,de               ;1edd	ed 52

L1EDF:
	ld ($5ca8),hl           ;1edf	22 a8 5c
	pop hl                  ;1ee2	e1
	ld de,$5caa             ;1ee3	11 aa 5c
	ld bc,$0005             ;1ee6	01 05 00
	ldir                    ;1ee9	ed b0
	ld ($5c5d),hl           ;1eeb	22 5d 5c
	ld de,$5ca7             ;1eee	11 a7 5c
	ld bc,$0008             ;1ef1	01 08 00
	call L3845              ;1ef4	cd 45 38
	ld d,$d0                ;1ef7	16 d0
	call L3C00              ;1ef9	cd 00 3c
	pop hl                  ;1efc	e1
	pop de                  ;1efd	d1
	pop bc                  ;1efe	c1
	dec c                   ;1eff	0d
	push bc                 ;1f00	c5
	push de                 ;1f01	d5
	push hl                 ;1f02	e5
	call L3EC9              ;1f03	cd c9 3e
	cp $2c                  ;1f06	fe 2c
	jp nz,L1D0A             ;1f08	c2 0a 1d
	rst $20                 ;1f0b	e7
	jr L1EBC                ;1f0c	18 ae

L1F0E:
	ex de,hl                ;1f0e	eb
	call L3A0F              ;1f0f	cd 0f 3a
	jr L1EDF                ;1f12	18 cb
L1F14:
	defb 0xe7,0xcd,0x02,0x09,0x3e,0x21,0xcd,0x20 ;1f14	e7 cd 02 09 3e 21 cd 20	....>!. 
	defb 0x3a,0x3a,0x77                          ;1f1c	3a 3a 77	::w

; tentative entry (word / jump table scan)
T1F1F:
	ld e,e                  ;1f1f	5b
	ld bc,$0000             ;1f20	01 00 00
	call L2147              ;1f23	cd 47 21
	ex de,hl                ;1f26	eb
	dec hl                  ;1f27	2b
	ld ix,$1f8d             ;1f28	dd 21 8d 1f
	ld de,$0082             ;1f2c	11 82 00
	call L19E9              ;1f2f	cd e9 19
	call T1986              ;1f32	cd 86 19
	jr T1F40                ;1f35	18 09
L1F37:
	defb 0xdd,0x21,0x8d,0x1f,0x1e                ;1f37	dd 21 8d 1f 1e	.!...
	add d                   ;1f3c	82
	call L1974              ;1f3d	cd 74 19

; tentative entry (word / jump table scan)
T1F40:
	jr c,L1F93              ;1f40	38 51

L1F42:
	call L3EC9              ;1f42	cd c9 3e
	inc hl                  ;1f45	23
	push hl                 ;1f46	e5
	add hl,$0005            ;1f47	ed 34 05 00
	call L3ED0              ;1f4b	cd d0 3e
	rst $28                 ;1f4e	ef
	or a                    ;1f4f	b7
	jr z,T1F1F              ;1f50	28 cd
	ret                     ;1f52	c9
	ld a,$fe                ;1f53	3e fe
	dec a                   ;1f55	3d
	ld hl,$3fb2             ;1f56	21 b2 3f
	jr nz,L1F61             ;1f59	20 06
	call T0638              ;1f5b	cd 38 06
	call L382C              ;1f5e	cd 2c 38

L1F61:
	pop de                  ;1f61	d1
	ld bc,$0005             ;1f62	01 05 00
	ldir                    ;1f65	ed b0
	call T0068              ;1f67	cd 68 00
	sbc d                   ;1f6a	9a
	ld a,($773a)            ;1f6b	3a 3a 77
	ld e,e                  ;1f6e	5b
	inc a                   ;1f6f	3c
	jr z,L1F83              ;1f70	28 11
	dec a                   ;1f72	3d
	call L32CC              ;1f73	cd cc 32
	dec de                  ;1f76	1b
	push de                 ;1f77	d5
	call L3A0F              ;1f78	cd 0f 3a
	pop de                  ;1f7b	d1
	ex de,hl                ;1f7c	eb
	ld bc,$0005             ;1f7d	01 05 00
	lddr                    ;1f80	ed b8
	rst $30                 ;1f82	f7

L1F83:
	call L3EC9              ;1f83	cd c9 3e
	cp $2c                  ;1f86	fe 2c
	jr nz,L1F37             ;1f88	20 ad
	rst $20                 ;1f8a	e7
	jr L1F42                ;1f8b	18 b5
	rst $20                 ;1f8d	e7
	cp $0e                  ;1f8e	fe 0e
	ret z                   ;1f90	c8
	scf                     ;1f91	37
	ret                     ;1f92	c9
L1F93:
	defb 0x0e,0x21,0xcd,0x66,0x3a,0xd0,0xcf,0x06 ;1f93	0e 21 cd 66 3a d0 cf 06	.!.f:...

L1F9B:
	ld d,h                  ;1f9b	54
	ld e,l                  ;1f9c	5d
	cp $25                  ;1f9d	fe 25
	jr z,L1FCF              ;1f9f	28 2e
	call L387F              ;1fa1	cd 7f 38
	ret nc                  ;1fa4	d0

L1FA5:
	rst $20                 ;1fa5	e7
	call L387A              ;1fa6	cd 7a 38
	jr c,L1FA5              ;1fa9	38 fa
	cp $24                  ;1fab	fe 24
	call z,L3ECF            ;1fad	cc cf 3e
	cp $28                  ;1fb0	fe 28
	jr nz,L1FB7             ;1fb2	20 03
	rst $20                 ;1fb4	e7
	cp $29                  ;1fb5	fe 29

L1FB7:
	ex de,hl                ;1fb7	eb
	ld ($5c5d),hl           ;1fb8	22 5d 5c
	scf                     ;1fbb	37
	ccf                     ;1fbc	3f
	ret nz                  ;1fbd	c0
	push de                 ;1fbe	d5
	ld bc,$28b2             ;1fbf	01 b2 28
	call L3EC1              ;1fc2	cd c1 3e
	pop de                  ;1fc5	d1
	ld ($5c5d),de           ;1fc6	ed 53 5d 5c
	dec d                   ;1fca	15
	ccf                     ;1fcb	3f
	ret c                   ;1fcc	d8

L1FCD:
	rst $08                 ;1fcd	cf
	ld bc,$cde7             ;1fce	01 e7 cd
	ld a,a                  ;1fd1	7f
	jr c,L1FF9              ;1fd2	38 25
	jr nc,L1FB7             ;1fd4	30 e1
	or $20                  ;1fd6	f6 20
	ex af,af'               ;1fd8	08
	rst $20                 ;1fd9	e7
	cp $28                  ;1fda	fe 28
	jr nz,L1FB7             ;1fdc	20 d9
	rst $20                 ;1fde	e7
	cp $29                  ;1fdf	fe 29
	jr nz,L1FB7             ;1fe1	20 d4
	ex af,af'               ;1fe3	08
	sub $61                 ;1fe4	d6 61
	ld e,a                  ;1fe6	5f
	xor a                   ;1fe7	af
	scf                     ;1fe8	37
	ret                     ;1fe9	c9

L1FEA:
	push de                 ;1fea	d5
	bit 7,d                 ;1feb	cb 7a
	jr nz,L1FFB             ;1fed	20 0c
	cp $2c                  ;1fef	fe 2c
	jr z,L1FFE              ;1ff1	28 0b
	call L1F9B              ;1ff3	cd 9b 1f
	jr nc,L1FFB             ;1ff6	30 03
	rst $20                 ;1ff8	e7

L1FF9:
	jr L1FFE                ;1ff9	18 03

L1FFB:
	call L0E2D              ;1ffb	cd 2d 0e

L1FFE:
	pop de                  ;1ffe	d1
	cp $2c                  ;1fff	fe 2c
	ret nz                  ;2001	c0
	rst $20                 ;2002	e7
	jr L1FEA                ;2003	18 e5

L2005:
	ld d,$00                ;2005	16 00

L2007:
	rst $20                 ;2007	e7

L2008:
	call L3EC9              ;2008	cd c9 3e
	bit 6,d                 ;200b	cb 72
	jr z,L201E              ;200d	28 0f
	cp $e4                  ;200f	fe e4
	jr z,L2055              ;2011	28 42
	cp $86                  ;2013	fe 86
	jr nz,L201E             ;2015	20 07
	rst $20                 ;2017	e7
	cp $25                  ;2018	fe 25
	jr nz,L201E             ;201a	20 02

L201C:
	rst $08                 ;201c	cf
	add hl,de               ;201d	19

L201E:
	push de                 ;201e	d5
	bit 7,d                 ;201f	cb 7a
	jr z,L202C              ;2021	28 09
	call L1F9B              ;2023	cd 9b 1f
	jr nc,L202C             ;2026	30 04
	rst $20                 ;2028	e7
	pop de                  ;2029	d1
	jr L2050                ;202a	18 24

L202C:
	call L05A3              ;202c	cd a3 05
	call L3EC9              ;202f	cd c9 3e
	pop de                  ;2032	d1
	bit 7,d                 ;2033	cb 7a
	jr z,L2050              ;2035	28 19
	cp $3d                  ;2037	fe 3d
	jr nz,L2050             ;2039	20 15
	rst $20                 ;203b	e7
	ld a,($5c3b)            ;203c	3a 3b 5c
	ld e,a                  ;203f	5f
	push de                 ;2040	d5
	call L0E2D              ;2041	cd 2d 0e
	pop de                  ;2044	d1
	ld a,($5c3b)            ;2045	3a 3b 5c
	xor e                   ;2048	ab
	and $40                 ;2049	e6 40
	jr nz,L201C             ;204b	20 cf
	call L3EC9              ;204d	cd c9 3e

L2050:
	cp $2c                  ;2050	fe 2c
	ret nz                  ;2052	c0
	jr L2007                ;2053	18 b2

L2055:
	rst $20                 ;2055	e7
	cp $29                  ;2056	fe 29
	jr nz,L201C             ;2058	20 c2
	ret                     ;205a	c9
	xor a                   ;205b	af
	ld ($5c75),a            ;205c	32 75 5c
	ld b,$00                ;205f	06 00
	call L2066              ;2061	cd 66 20
	jr L2087                ;2064	18 21

L2066:
	ld c,b                  ;2066	48

; tentative entry (word / jump table scan)
T2067:
	bit 7,(iy+$01)          ;2067	fd cb 01 7e
	push bc                 ;206b	c5
	call nz,L3894           ;206c	c4 94 38
	call nz,L37FC           ;206f	c4 fc 37
	pop bc                  ;2072	c1
	jr L20B8                ;2073	18 43

L2075:
	bit 7,(iy+$01)          ;2075	fd cb 01 7e
	jr z,L207E              ;2079	28 03
	call L3BE1              ;207b	cd e1 3b

L207E:
	xor a                   ;207e	af
	ld ($5c75),a            ;207f	32 75 5c
	ld b,$00                ;2082	06 00
	call L20B4              ;2084	cd b4 20

L2087:
	pop hl                  ;2087	e1
	jp L091F                ;2088	c3 1f 09
Text208B:
	defb 0xdd,0xe3,0xcd,0xb4,0x20,0xcd,0x02,0x09 ;208b	dd e3 cd b4 20 cd 02 09	.... ...
	defb 0xdd,0xe1,0x2a,0x45,0x5c,0xdd,0x75,0x0d ;2093	dd e1 2a 45 5c dd 75 0d	..*E\.u.
	defb 0xdd,0x74,0x0e,0xdd,0xe5                ;209b	dd 74 0e dd e5	.t...
	ld c,$21                ;20a0	0e 21
	call L3A66              ;20a2	cd 66 3a
	ld ix,$0941             ;20a5	dd 21 41 09
	ex (sp),ix              ;20a9	dd e3
	ld hl,$007b             ;20ab	21 7b 00
	push hl                 ;20ae	e5
	push $30a8              ;20af	ed 8a 30 a8
	jp (hl)                 ;20b3	e9

L20B4:
	ld c,b                  ;20b4	48

L20B5:
	ld a,($5b77)            ;20b5	3a 77 5b

L20B8:
	ld ($5b65),a            ;20b8	32 65 5b
	call L3EC9              ;20bb	cd c9 3e
	inc c                   ;20be	0c
	jr nz,L20CE             ;20bf	20 0d
	dec c                   ;20c1	0d
	ld de,$2228             ;20c2	11 28 22
	cp $93                  ;20c5	fe 93
	jr z,L20D5              ;20c7	28 0c
	inc d                   ;20c9	14
	cp $a8                  ;20ca	fe a8
	jr z,L20D5              ;20cc	28 07

L20CE:
	cp $40                  ;20ce	fe 40
	jr nz,L2122             ;20d0	20 50
	ld de,$213a             ;20d2	11 3a 21

L20D5:
	rst $20                 ;20d5	e7
	bit 7,(iy+$01)          ;20d6	fd cb 01 7e
	jr z,L2117              ;20da	28 3b
	pop hl                  ;20dc	e1
	ld ($5b56),hl           ;20dd	22 56 5b
	ld a,d                  ;20e0	7a
	call L12CD              ;20e1	cd cd 12
	ld ($5c47),a            ;20e4	32 47 5c
	ld ($5c45),de           ;20e7	ed 53 45 5c
	ld ($5c55),hl           ;20eb	22 55 5c
	ld a,($5b65)            ;20ee	3a 65 5b
	call L38AD              ;20f1	cd ad 38
	ld hl,($5b56)           ;20f4	2a 56 5b
	push hl                 ;20f7	e5
	ld a,($5c75)            ;20f8	3a 75 5c
	and a                   ;20fb	a7
	jp z,L3BF6              ;20fc	ca f6 3b
	ld hl,($5c5f)           ;20ff	2a 5f 5c
	dec hl                  ;2102	2b

L2103:
	ld de,($5b8a)           ;2103	ed 5b 8a 5b
	and a                   ;2107	a7
	sbc hl,de               ;2108	ed 52
	ex de,hl                ;210a	eb
	ld hl,($5b92)           ;210b	2a 92 5b
	dec hl                  ;210e	2b
	ld a,e                  ;210f	7b
	add (hl)                ;2110	86
	ld (hl),a               ;2111	77
	inc hl                  ;2112	23
	ld a,d                  ;2113	7a
	adc (hl)                ;2114	8e
	ld (hl),a               ;2115	77
	ret                     ;2116	c9

L2117:
	push de                 ;2117	d5
	call L135C              ;2118	cd 5c 13
	pop af                  ;211b	f1
	inc c                   ;211c	0c
	ret z                   ;211d	c8
	pop hl                  ;211e	e1
	call L0902              ;211f	cd 02 09

L2122:
	ld hl,$0639             ;2122	21 39 06
	djnz L212A              ;2125	10 03
	ld hl,$0e21             ;2127	21 21 0e

L212A:
	call L0AA7              ;212a	cd a7 0a
	pop hl                  ;212d	e1
	ld ($5b56),hl           ;212e	22 56 5b
	call L0902              ;2131	cd 02 09
	ld a,($5c75)            ;2134	3a 75 5c
	and a                   ;2137	a7
	ld a,$21                ;2138	3e 21
	call nz,L3A20           ;213a	c4 20 3a
	ld hl,($5b56)           ;213d	2a 56 5b
	push hl                 ;2140	e5
	call L37F4              ;2141	cd f4 37
	ld a,($5b65)            ;2144	3a 65 5b

L2147:
	ld ($5c45),bc           ;2147	ed 43 45 5c
	call L38AD              ;214b	cd ad 38
	call L3942              ;214e	cd 42 39
	jr nc,L2164             ;2151	30 11
	call L3975              ;2153	cd 75 39
	inc de                  ;2156	13
	scf                     ;2157	37

L2158:
	ld (iy+$0d),$01         ;2158	fd 36 0d 01
	ld ($5c55),hl           ;215c	22 55 5c
	ld ($5c5d),de           ;215f	ed 53 5d 5c
	ret                     ;2163	c9

L2164:
	ld a,$ff                ;2164	3e ff
	ld ($5b77),a            ;2166	32 77 5b
	ld hl,($5c61)           ;2169	2a 61 5c
	dec hl                  ;216c	2b
	ld d,h                  ;216d	54
	ld e,l                  ;216e	5d
	dec hl                  ;216f	2b
	ex de,hl                ;2170	eb
	jr L2158                ;2171	18 e5
Zero2173:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2173	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;217b	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2183	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;218b	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2193	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;219b	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;21a3	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;21ab	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;21b3	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;21bb	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;21c3	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00                          ;21cb	00 00 00	...
	nop                     ;21ce	00
	nop                     ;21cf	00
	nop                     ;21d0	00
	nop                     ;21d1	00
	nop                     ;21d2	00
	nop                     ;21d3	00
	nop                     ;21d4	00
	nop                     ;21d5	00
	nop                     ;21d6	00
	nop                     ;21d7	00
	nop                     ;21d8	00
	nop                     ;21d9	00
	nop                     ;21da	00
	nop                     ;21db	00
	nop                     ;21dc	00
	nop                     ;21dd	00
	nop                     ;21de	00
	nop                     ;21df	00
	nop                     ;21e0	00
	nop                     ;21e1	00
	nop                     ;21e2	00
	nop                     ;21e3	00
	nop                     ;21e4	00
	nop                     ;21e5	00
	nop                     ;21e6	00
	nop                     ;21e7	00
	nop                     ;21e8	00
	nop                     ;21e9	00
	nop                     ;21ea	00
	nop                     ;21eb	00
	nop                     ;21ec	00
	nop                     ;21ed	00
	nop                     ;21ee	00
	nop                     ;21ef	00
	nop                     ;21f0	00
	nop                     ;21f1	00
	nop                     ;21f2	00
	nop                     ;21f3	00
	nop                     ;21f4	00
	nop                     ;21f5	00
	nop                     ;21f6	00
	nop                     ;21f7	00
	nop                     ;21f8	00
	nop                     ;21f9	00
	nop                     ;21fa	00
	nop                     ;21fb	00
	nop                     ;21fc	00
	nop                     ;21fd	00
	nop                     ;21fe	00
	nop                     ;21ff	00
	nop                     ;2200	00
	nop                     ;2201	00
	nop                     ;2202	00
	nop                     ;2203	00
	nop                     ;2204	00
	nop                     ;2205	00
	nop                     ;2206	00
	nop                     ;2207	00
	nop                     ;2208	00
	nop                     ;2209	00
	nop                     ;220a	00
	nop                     ;220b	00
	nop                     ;220c	00
	nop                     ;220d	00
	nop                     ;220e	00
	nop                     ;220f	00
	nop                     ;2210	00
	nop                     ;2211	00
	nop                     ;2212	00
	nop                     ;2213	00
	nop                     ;2214	00
	nop                     ;2215	00
	nop                     ;2216	00
	nop                     ;2217	00
	nop                     ;2218	00
	nop                     ;2219	00
	nop                     ;221a	00
	nop                     ;221b	00
	nop                     ;221c	00
	nop                     ;221d	00
	nop                     ;221e	00
	nop                     ;221f	00
	nop                     ;2220	00
	nop                     ;2221	00
	nop                     ;2222	00
	nop                     ;2223	00
	nop                     ;2224	00
	nop                     ;2225	00
	nop                     ;2226	00
	nop                     ;2227	00
	nop                     ;2228	00
	nop                     ;2229	00
	nop                     ;222a	00
	nop                     ;222b	00
	nop                     ;222c	00
	nop                     ;222d	00
	nop                     ;222e	00
	nop                     ;222f	00
	nop                     ;2230	00
	nop                     ;2231	00
	nop                     ;2232	00
	nop                     ;2233	00
	nop                     ;2234	00
	nop                     ;2235	00
	nop                     ;2236	00
	nop                     ;2237	00
	nop                     ;2238	00
	nop                     ;2239	00
	nop                     ;223a	00
	nop                     ;223b	00
	nop                     ;223c	00
	nop                     ;223d	00
	nop                     ;223e	00
	nop                     ;223f	00
	nop                     ;2240	00
	nop                     ;2241	00
	nop                     ;2242	00
	nop                     ;2243	00
	nop                     ;2244	00
	nop                     ;2245	00
	nop                     ;2246	00
	nop                     ;2247	00
	nop                     ;2248	00
	nop                     ;2249	00
	nop                     ;224a	00
	nop                     ;224b	00
	nop                     ;224c	00
	nop                     ;224d	00
	nop                     ;224e	00
	nop                     ;224f	00
	nop                     ;2250	00
	nop                     ;2251	00
	nop                     ;2252	00
	nop                     ;2253	00
	nop                     ;2254	00
	nop                     ;2255	00
	nop                     ;2256	00
	nop                     ;2257	00
	nop                     ;2258	00
	nop                     ;2259	00
	nop                     ;225a	00
	nop                     ;225b	00
	nop                     ;225c	00
	nop                     ;225d	00
	nop                     ;225e	00
	nop                     ;225f	00
	nop                     ;2260	00
	nop                     ;2261	00
	nop                     ;2262	00
	nop                     ;2263	00
	nop                     ;2264	00
	nop                     ;2265	00
	nop                     ;2266	00
	nop                     ;2267	00
	nop                     ;2268	00
	nop                     ;2269	00
	nop                     ;226a	00
	nop                     ;226b	00
	nop                     ;226c	00
	nop                     ;226d	00
	nop                     ;226e	00
	nop                     ;226f	00
	nop                     ;2270	00
	nop                     ;2271	00
	nop                     ;2272	00
	nop                     ;2273	00
	nop                     ;2274	00
	nop                     ;2275	00
	nop                     ;2276	00
	nop                     ;2277	00
	nop                     ;2278	00
	nop                     ;2279	00
	nop                     ;227a	00
	nop                     ;227b	00
	nop                     ;227c	00
	nop                     ;227d	00
	nop                     ;227e	00
	nop                     ;227f	00
	nop                     ;2280	00
	nop                     ;2281	00
	nop                     ;2282	00
	nop                     ;2283	00
	nop                     ;2284	00
	nop                     ;2285	00
	nop                     ;2286	00
	nop                     ;2287	00
	nop                     ;2288	00
	nop                     ;2289	00
	nop                     ;228a	00
	nop                     ;228b	00
	nop                     ;228c	00
	nop                     ;228d	00
	nop                     ;228e	00
	nop                     ;228f	00
	nop                     ;2290	00
	nop                     ;2291	00
	nop                     ;2292	00
	nop                     ;2293	00
	nop                     ;2294	00
	nop                     ;2295	00
	nop                     ;2296	00
	nop                     ;2297	00
	nop                     ;2298	00
	nop                     ;2299	00
	nop                     ;229a	00
	nop                     ;229b	00
	nop                     ;229c	00
	nop                     ;229d	00
	nop                     ;229e	00
	nop                     ;229f	00
	nop                     ;22a0	00
	nop                     ;22a1	00
	nop                     ;22a2	00
	nop                     ;22a3	00
	nop                     ;22a4	00
	nop                     ;22a5	00
	nop                     ;22a6	00
	nop                     ;22a7	00
	nop                     ;22a8	00
	nop                     ;22a9	00
	nop                     ;22aa	00
	nop                     ;22ab	00
	nop                     ;22ac	00
	nop                     ;22ad	00
	nop                     ;22ae	00
	nop                     ;22af	00
	nop                     ;22b0	00
	nop                     ;22b1	00
	nop                     ;22b2	00
	nop                     ;22b3	00
	nop                     ;22b4	00
	nop                     ;22b5	00
	nop                     ;22b6	00
	nop                     ;22b7	00
	nop                     ;22b8	00
	nop                     ;22b9	00
	nop                     ;22ba	00
	nop                     ;22bb	00
	nop                     ;22bc	00
	nop                     ;22bd	00
	nop                     ;22be	00
	nop                     ;22bf	00
	nop                     ;22c0	00
	nop                     ;22c1	00
	nop                     ;22c2	00
	nop                     ;22c3	00
	nop                     ;22c4	00
	nop                     ;22c5	00
	nop                     ;22c6	00
	nop                     ;22c7	00
	nop                     ;22c8	00
	nop                     ;22c9	00
	nop                     ;22ca	00
	nop                     ;22cb	00
	nop                     ;22cc	00
	nop                     ;22cd	00
	nop                     ;22ce	00
	nop                     ;22cf	00
	nop                     ;22d0	00
	nop                     ;22d1	00
	nop                     ;22d2	00
	nop                     ;22d3	00
	nop                     ;22d4	00
	nop                     ;22d5	00
	nop                     ;22d6	00
	nop                     ;22d7	00
	nop                     ;22d8	00
	nop                     ;22d9	00
	nop                     ;22da	00
	nop                     ;22db	00
	nop                     ;22dc	00
	nop                     ;22dd	00
	nop                     ;22de	00
	nop                     ;22df	00
	nop                     ;22e0	00
	nop                     ;22e1	00
	nop                     ;22e2	00
	nop                     ;22e3	00
	nop                     ;22e4	00
	nop                     ;22e5	00
	nop                     ;22e6	00
	nop                     ;22e7	00
	nop                     ;22e8	00
	nop                     ;22e9	00
	nop                     ;22ea	00
	nop                     ;22eb	00
	nop                     ;22ec	00
	nop                     ;22ed	00
	nop                     ;22ee	00
	nop                     ;22ef	00
	nop                     ;22f0	00
	nop                     ;22f1	00
	nop                     ;22f2	00
	nop                     ;22f3	00
	nop                     ;22f4	00
	nop                     ;22f5	00
	nop                     ;22f6	00
	nop                     ;22f7	00
	nop                     ;22f8	00
	nop                     ;22f9	00
	nop                     ;22fa	00
	nop                     ;22fb	00
	nop                     ;22fc	00
	nop                     ;22fd	00
	nop                     ;22fe	00
	nop                     ;22ff	00
	nop                     ;2300	00
	nop                     ;2301	00
	nop                     ;2302	00
	nop                     ;2303	00
	nop                     ;2304	00
	nop                     ;2305	00
	nop                     ;2306	00
	nop                     ;2307	00
	nop                     ;2308	00
	nop                     ;2309	00
	nop                     ;230a	00
	nop                     ;230b	00
	nop                     ;230c	00
	nop                     ;230d	00
	nop                     ;230e	00
	nop                     ;230f	00
	nop                     ;2310	00
	nop                     ;2311	00
	nop                     ;2312	00
	nop                     ;2313	00
	nop                     ;2314	00
	nop                     ;2315	00
	nop                     ;2316	00
	nop                     ;2317	00
	nop                     ;2318	00
	nop                     ;2319	00
	nop                     ;231a	00
	nop                     ;231b	00
	nop                     ;231c	00
	nop                     ;231d	00
	nop                     ;231e	00
	nop                     ;231f	00
	nop                     ;2320	00
	nop                     ;2321	00
	nop                     ;2322	00
	nop                     ;2323	00
	nop                     ;2324	00
	nop                     ;2325	00
	nop                     ;2326	00
	nop                     ;2327	00
	nop                     ;2328	00
	nop                     ;2329	00
	nop                     ;232a	00
	nop                     ;232b	00
	nop                     ;232c	00
	nop                     ;232d	00
	nop                     ;232e	00
	nop                     ;232f	00
	nop                     ;2330	00
	nop                     ;2331	00
	nop                     ;2332	00
	nop                     ;2333	00
	nop                     ;2334	00
	nop                     ;2335	00
	nop                     ;2336	00
	nop                     ;2337	00
	nop                     ;2338	00
	nop                     ;2339	00
	nop                     ;233a	00
	nop                     ;233b	00
	nop                     ;233c	00
	nop                     ;233d	00
	nop                     ;233e	00
	nop                     ;233f	00
	nop                     ;2340	00
	nop                     ;2341	00
	nop                     ;2342	00
	nop                     ;2343	00
	nop                     ;2344	00
	nop                     ;2345	00
	nop                     ;2346	00
	nop                     ;2347	00
	nop                     ;2348	00
	nop                     ;2349	00
	nop                     ;234a	00
	nop                     ;234b	00
	nop                     ;234c	00
	nop                     ;234d	00
	nop                     ;234e	00
	nop                     ;234f	00
	nop                     ;2350	00
	nop                     ;2351	00
	nop                     ;2352	00
	nop                     ;2353	00
	nop                     ;2354	00
	nop                     ;2355	00
	nop                     ;2356	00
	nop                     ;2357	00
	nop                     ;2358	00
	nop                     ;2359	00
	nop                     ;235a	00
	nop                     ;235b	00
	nop                     ;235c	00
	nop                     ;235d	00
	nop                     ;235e	00
	nop                     ;235f	00
	nop                     ;2360	00
	nop                     ;2361	00
	nop                     ;2362	00
	nop                     ;2363	00
	nop                     ;2364	00
	nop                     ;2365	00
	nop                     ;2366	00
	nop                     ;2367	00
	nop                     ;2368	00
	nop                     ;2369	00
	nop                     ;236a	00
	nop                     ;236b	00
	nop                     ;236c	00
	nop                     ;236d	00
	nop                     ;236e	00
	nop                     ;236f	00
	nop                     ;2370	00
	nop                     ;2371	00
	nop                     ;2372	00
	nop                     ;2373	00
	nop                     ;2374	00
	nop                     ;2375	00
	nop                     ;2376	00
	nop                     ;2377	00
	nop                     ;2378	00
	nop                     ;2379	00
	nop                     ;237a	00
	nop                     ;237b	00
	nop                     ;237c	00
	nop                     ;237d	00
	nop                     ;237e	00
	nop                     ;237f	00
	nop                     ;2380	00
	nop                     ;2381	00
	nop                     ;2382	00
	nop                     ;2383	00
	nop                     ;2384	00
	nop                     ;2385	00
	nop                     ;2386	00
	nop                     ;2387	00
	nop                     ;2388	00
	nop                     ;2389	00
	nop                     ;238a	00
	nop                     ;238b	00
	nop                     ;238c	00
	nop                     ;238d	00
	nop                     ;238e	00
	nop                     ;238f	00
	nop                     ;2390	00
	nop                     ;2391	00
	nop                     ;2392	00
	nop                     ;2393	00
	nop                     ;2394	00
	nop                     ;2395	00
	nop                     ;2396	00
	nop                     ;2397	00
	nop                     ;2398	00
	nop                     ;2399	00
	nop                     ;239a	00
	nop                     ;239b	00
	nop                     ;239c	00
	nop                     ;239d	00
	nop                     ;239e	00
	nop                     ;239f	00
	nop                     ;23a0	00
	nop                     ;23a1	00
	nop                     ;23a2	00
	nop                     ;23a3	00
	nop                     ;23a4	00
	nop                     ;23a5	00
	nop                     ;23a6	00
	nop                     ;23a7	00
	nop                     ;23a8	00
	nop                     ;23a9	00
	nop                     ;23aa	00
	nop                     ;23ab	00
	nop                     ;23ac	00
	nop                     ;23ad	00
	nop                     ;23ae	00
	nop                     ;23af	00
	nop                     ;23b0	00
	nop                     ;23b1	00
	nop                     ;23b2	00
	nop                     ;23b3	00
	nop                     ;23b4	00
	nop                     ;23b5	00
	nop                     ;23b6	00
	nop                     ;23b7	00
	nop                     ;23b8	00
	nop                     ;23b9	00
	nop                     ;23ba	00
	nop                     ;23bb	00
	nop                     ;23bc	00
	nop                     ;23bd	00
	nop                     ;23be	00
	nop                     ;23bf	00
	nop                     ;23c0	00
	nop                     ;23c1	00
	nop                     ;23c2	00
	nop                     ;23c3	00
	nop                     ;23c4	00
	nop                     ;23c5	00
	nop                     ;23c6	00
	nop                     ;23c7	00
	nop                     ;23c8	00
	nop                     ;23c9	00
	nop                     ;23ca	00
	nop                     ;23cb	00
	nop                     ;23cc	00
	nop                     ;23cd	00
	nop                     ;23ce	00
	nop                     ;23cf	00
	nop                     ;23d0	00
	nop                     ;23d1	00
	nop                     ;23d2	00
	nop                     ;23d3	00
	nop                     ;23d4	00
	nop                     ;23d5	00
	nop                     ;23d6	00
	nop                     ;23d7	00
	nop                     ;23d8	00
	nop                     ;23d9	00
	nop                     ;23da	00
	nop                     ;23db	00
	nop                     ;23dc	00
	nop                     ;23dd	00
	nop                     ;23de	00
	nop                     ;23df	00
	nop                     ;23e0	00
	nop                     ;23e1	00
	nop                     ;23e2	00
	nop                     ;23e3	00
	nop                     ;23e4	00
	nop                     ;23e5	00
	nop                     ;23e6	00
	nop                     ;23e7	00
	nop                     ;23e8	00
	nop                     ;23e9	00
	nop                     ;23ea	00
	nop                     ;23eb	00
	nop                     ;23ec	00
	nop                     ;23ed	00
	nop                     ;23ee	00
	nop                     ;23ef	00
	nop                     ;23f0	00
	nop                     ;23f1	00
	nop                     ;23f2	00
	nop                     ;23f3	00
	nop                     ;23f4	00
	nop                     ;23f5	00
	nop                     ;23f6	00
	nop                     ;23f7	00
	nop                     ;23f8	00
	nop                     ;23f9	00
	nop                     ;23fa	00
	nop                     ;23fb	00
	nop                     ;23fc	00
	nop                     ;23fd	00
	nop                     ;23fe	00
	nop                     ;23ff	00
	nop                     ;2400	00
	nop                     ;2401	00
	nop                     ;2402	00
	nop                     ;2403	00
	nop                     ;2404	00
	nop                     ;2405	00
	nop                     ;2406	00
	nop                     ;2407	00
	nop                     ;2408	00
	nop                     ;2409	00
	nop                     ;240a	00
	nop                     ;240b	00
	nop                     ;240c	00
	nop                     ;240d	00
	nop                     ;240e	00
	nop                     ;240f	00
	nop                     ;2410	00
	nop                     ;2411	00
	nop                     ;2412	00
	nop                     ;2413	00
	nop                     ;2414	00
	nop                     ;2415	00
	nop                     ;2416	00
	nop                     ;2417	00
	nop                     ;2418	00
	nop                     ;2419	00
	nop                     ;241a	00
	nop                     ;241b	00
	nop                     ;241c	00
	nop                     ;241d	00
	nop                     ;241e	00
	nop                     ;241f	00
	nop                     ;2420	00
	nop                     ;2421	00
	nop                     ;2422	00
	nop                     ;2423	00
	nop                     ;2424	00
	nop                     ;2425	00
	nop                     ;2426	00
	nop                     ;2427	00
	nop                     ;2428	00
	nop                     ;2429	00
	nop                     ;242a	00
	nop                     ;242b	00
	nop                     ;242c	00
	nop                     ;242d	00
	nop                     ;242e	00
	nop                     ;242f	00
	nop                     ;2430	00
	nop                     ;2431	00
	nop                     ;2432	00
	nop                     ;2433	00
	nop                     ;2434	00
	nop                     ;2435	00
	nop                     ;2436	00
	nop                     ;2437	00
	nop                     ;2438	00
	nop                     ;2439	00
	nop                     ;243a	00
	nop                     ;243b	00
	nop                     ;243c	00
	nop                     ;243d	00
	nop                     ;243e	00
	nop                     ;243f	00
	nop                     ;2440	00
	nop                     ;2441	00
	nop                     ;2442	00
	nop                     ;2443	00
	nop                     ;2444	00
	nop                     ;2445	00
	nop                     ;2446	00
	nop                     ;2447	00
	nop                     ;2448	00
	nop                     ;2449	00
	nop                     ;244a	00
	nop                     ;244b	00
	nop                     ;244c	00
	nop                     ;244d	00
	nop                     ;244e	00
	nop                     ;244f	00
	nop                     ;2450	00
	nop                     ;2451	00
	nop                     ;2452	00
	nop                     ;2453	00
	nop                     ;2454	00
	nop                     ;2455	00
	nop                     ;2456	00
	nop                     ;2457	00
	nop                     ;2458	00
	nop                     ;2459	00
	nop                     ;245a	00
	nop                     ;245b	00
	nop                     ;245c	00
	nop                     ;245d	00
	nop                     ;245e	00
	nop                     ;245f	00
	nop                     ;2460	00
	nop                     ;2461	00
	nop                     ;2462	00
	nop                     ;2463	00
	nop                     ;2464	00
	nop                     ;2465	00
	nop                     ;2466	00
	nop                     ;2467	00
	nop                     ;2468	00
	nop                     ;2469	00
	nop                     ;246a	00
	nop                     ;246b	00
	nop                     ;246c	00
	nop                     ;246d	00
	nop                     ;246e	00
	nop                     ;246f	00
	nop                     ;2470	00
	nop                     ;2471	00
	nop                     ;2472	00
	nop                     ;2473	00
	nop                     ;2474	00
	nop                     ;2475	00
	nop                     ;2476	00
	nop                     ;2477	00
	nop                     ;2478	00
	nop                     ;2479	00
	nop                     ;247a	00
	nop                     ;247b	00
	nop                     ;247c	00
	nop                     ;247d	00
	nop                     ;247e	00
	nop                     ;247f	00
	nop                     ;2480	00
	nop                     ;2481	00
	nop                     ;2482	00
	nop                     ;2483	00
	nop                     ;2484	00
	nop                     ;2485	00
	nop                     ;2486	00
	nop                     ;2487	00
	nop                     ;2488	00
	nop                     ;2489	00
	nop                     ;248a	00
	nop                     ;248b	00
	nop                     ;248c	00
	nop                     ;248d	00
	nop                     ;248e	00
	nop                     ;248f	00
	nop                     ;2490	00
	nop                     ;2491	00
	nop                     ;2492	00
	nop                     ;2493	00
	nop                     ;2494	00
	nop                     ;2495	00
	nop                     ;2496	00
	nop                     ;2497	00
	nop                     ;2498	00
	nop                     ;2499	00
	nop                     ;249a	00
	nop                     ;249b	00
	nop                     ;249c	00
	nop                     ;249d	00
	nop                     ;249e	00
	nop                     ;249f	00
	nop                     ;24a0	00
	nop                     ;24a1	00
	nop                     ;24a2	00
	nop                     ;24a3	00
	nop                     ;24a4	00
	nop                     ;24a5	00
	nop                     ;24a6	00
	nop                     ;24a7	00
	nop                     ;24a8	00
	nop                     ;24a9	00
	nop                     ;24aa	00
	nop                     ;24ab	00
	nop                     ;24ac	00
	nop                     ;24ad	00
	nop                     ;24ae	00
	nop                     ;24af	00
	nop                     ;24b0	00
	nop                     ;24b1	00
	nop                     ;24b2	00
	nop                     ;24b3	00
	nop                     ;24b4	00
	nop                     ;24b5	00
	nop                     ;24b6	00
	nop                     ;24b7	00
	nop                     ;24b8	00
	nop                     ;24b9	00
	nop                     ;24ba	00
	nop                     ;24bb	00
	nop                     ;24bc	00
	nop                     ;24bd	00
	nop                     ;24be	00
	nop                     ;24bf	00
	nop                     ;24c0	00
	nop                     ;24c1	00
	nop                     ;24c2	00
	nop                     ;24c3	00
	nop                     ;24c4	00
	nop                     ;24c5	00
	nop                     ;24c6	00
	nop                     ;24c7	00
	nop                     ;24c8	00
	nop                     ;24c9	00
	nop                     ;24ca	00
	nop                     ;24cb	00
	nop                     ;24cc	00
	nop                     ;24cd	00
	nop                     ;24ce	00
	nop                     ;24cf	00
	nop                     ;24d0	00
	nop                     ;24d1	00
	nop                     ;24d2	00
	nop                     ;24d3	00
	nop                     ;24d4	00
	nop                     ;24d5	00
	nop                     ;24d6	00
	nop                     ;24d7	00
	nop                     ;24d8	00
	nop                     ;24d9	00
	nop                     ;24da	00
	nop                     ;24db	00
	nop                     ;24dc	00
	nop                     ;24dd	00
	nop                     ;24de	00
	nop                     ;24df	00
	nop                     ;24e0	00
	nop                     ;24e1	00
	nop                     ;24e2	00
	nop                     ;24e3	00
	nop                     ;24e4	00
	nop                     ;24e5	00
	nop                     ;24e6	00
	nop                     ;24e7	00
	nop                     ;24e8	00
	nop                     ;24e9	00
	nop                     ;24ea	00
	nop                     ;24eb	00
	nop                     ;24ec	00
	nop                     ;24ed	00
	nop                     ;24ee	00
	nop                     ;24ef	00
	nop                     ;24f0	00
	nop                     ;24f1	00
	nop                     ;24f2	00
	nop                     ;24f3	00
	nop                     ;24f4	00
	nop                     ;24f5	00
	nop                     ;24f6	00
	nop                     ;24f7	00
	nop                     ;24f8	00
	nop                     ;24f9	00
	nop                     ;24fa	00
	nop                     ;24fb	00
	nop                     ;24fc	00
	nop                     ;24fd	00
	nop                     ;24fe	00
	nop                     ;24ff	00
	nop                     ;2500	00
	nop                     ;2501	00
	nop                     ;2502	00
	nop                     ;2503	00
	nop                     ;2504	00
	nop                     ;2505	00
	nop                     ;2506	00
	nop                     ;2507	00
	nop                     ;2508	00
	nop                     ;2509	00
	nop                     ;250a	00
	nop                     ;250b	00
	nop                     ;250c	00
	nop                     ;250d	00
	nop                     ;250e	00
	nop                     ;250f	00
	nop                     ;2510	00
	nop                     ;2511	00
	nop                     ;2512	00
	nop                     ;2513	00
	nop                     ;2514	00
	nop                     ;2515	00
	nop                     ;2516	00
	nop                     ;2517	00
	nop                     ;2518	00
	nop                     ;2519	00
	nop                     ;251a	00
	nop                     ;251b	00
	nop                     ;251c	00
	nop                     ;251d	00
	nop                     ;251e	00
	nop                     ;251f	00
	nop                     ;2520	00
	nop                     ;2521	00
	nop                     ;2522	00
	nop                     ;2523	00
	nop                     ;2524	00
	nop                     ;2525	00
	nop                     ;2526	00
	nop                     ;2527	00
	nop                     ;2528	00
	nop                     ;2529	00
	nop                     ;252a	00
	nop                     ;252b	00
	nop                     ;252c	00
	nop                     ;252d	00
	nop                     ;252e	00
	nop                     ;252f	00
	nop                     ;2530	00
	nop                     ;2531	00
	nop                     ;2532	00
	nop                     ;2533	00
	nop                     ;2534	00
	nop                     ;2535	00
	nop                     ;2536	00
	nop                     ;2537	00
	nop                     ;2538	00
	nop                     ;2539	00
	nop                     ;253a	00
	nop                     ;253b	00
	nop                     ;253c	00
	nop                     ;253d	00
	nop                     ;253e	00
	nop                     ;253f	00
	nop                     ;2540	00
	nop                     ;2541	00
	nop                     ;2542	00
	nop                     ;2543	00
	nop                     ;2544	00
	nop                     ;2545	00
	nop                     ;2546	00
	nop                     ;2547	00
	nop                     ;2548	00
	nop                     ;2549	00
	nop                     ;254a	00
	nop                     ;254b	00
	nop                     ;254c	00
	nop                     ;254d	00
	nop                     ;254e	00
	nop                     ;254f	00
	nop                     ;2550	00
	nop                     ;2551	00
	nop                     ;2552	00
	nop                     ;2553	00
	nop                     ;2554	00
	nop                     ;2555	00
	nop                     ;2556	00
	nop                     ;2557	00
	nop                     ;2558	00
	nop                     ;2559	00
	nop                     ;255a	00
	nop                     ;255b	00
	nop                     ;255c	00
	nop                     ;255d	00
	nop                     ;255e	00
	nop                     ;255f	00
	nop                     ;2560	00
	nop                     ;2561	00
	nop                     ;2562	00
	nop                     ;2563	00
	nop                     ;2564	00
	nop                     ;2565	00
	nop                     ;2566	00
	nop                     ;2567	00
	nop                     ;2568	00
	nop                     ;2569	00
	nop                     ;256a	00
	nop                     ;256b	00
	nop                     ;256c	00
	nop                     ;256d	00
	nop                     ;256e	00
	nop                     ;256f	00
	nop                     ;2570	00
	nop                     ;2571	00
	nop                     ;2572	00
	nop                     ;2573	00
	nop                     ;2574	00
	nop                     ;2575	00
	nop                     ;2576	00
	nop                     ;2577	00
	nop                     ;2578	00
	nop                     ;2579	00
	nop                     ;257a	00
	nop                     ;257b	00
	nop                     ;257c	00
	nop                     ;257d	00
	nop                     ;257e	00
	nop                     ;257f	00
	nop                     ;2580	00
	nop                     ;2581	00
	nop                     ;2582	00
	nop                     ;2583	00
	nop                     ;2584	00
	nop                     ;2585	00
	nop                     ;2586	00
	nop                     ;2587	00
	nop                     ;2588	00
	nop                     ;2589	00
	nop                     ;258a	00
	nop                     ;258b	00
	nop                     ;258c	00
	nop                     ;258d	00
	nop                     ;258e	00
	nop                     ;258f	00
	nop                     ;2590	00
	nop                     ;2591	00
	nop                     ;2592	00
	nop                     ;2593	00
	nop                     ;2594	00
	nop                     ;2595	00
	nop                     ;2596	00
	nop                     ;2597	00
	nop                     ;2598	00
	nop                     ;2599	00
	nop                     ;259a	00
	nop                     ;259b	00
	nop                     ;259c	00
	nop                     ;259d	00
	nop                     ;259e	00
	nop                     ;259f	00
	nop                     ;25a0	00
	nop                     ;25a1	00
	nop                     ;25a2	00
	nop                     ;25a3	00
	nop                     ;25a4	00
	nop                     ;25a5	00
	nop                     ;25a6	00
	nop                     ;25a7	00
	nop                     ;25a8	00
	nop                     ;25a9	00
	nop                     ;25aa	00
	nop                     ;25ab	00
	nop                     ;25ac	00
	nop                     ;25ad	00
	nop                     ;25ae	00
	nop                     ;25af	00
	nop                     ;25b0	00
	nop                     ;25b1	00
	nop                     ;25b2	00
	nop                     ;25b3	00
	nop                     ;25b4	00
	nop                     ;25b5	00
	nop                     ;25b6	00
	nop                     ;25b7	00
	nop                     ;25b8	00
	nop                     ;25b9	00
	nop                     ;25ba	00
	nop                     ;25bb	00
	nop                     ;25bc	00
	nop                     ;25bd	00
	nop                     ;25be	00
	nop                     ;25bf	00
	nop                     ;25c0	00
	nop                     ;25c1	00
	nop                     ;25c2	00
	nop                     ;25c3	00
	nop                     ;25c4	00
	nop                     ;25c5	00
	nop                     ;25c6	00
	nop                     ;25c7	00
	nop                     ;25c8	00
	nop                     ;25c9	00
	nop                     ;25ca	00
	nop                     ;25cb	00
	nop                     ;25cc	00
	nop                     ;25cd	00
	nop                     ;25ce	00
	nop                     ;25cf	00
	nop                     ;25d0	00
	nop                     ;25d1	00
	nop                     ;25d2	00
	nop                     ;25d3	00
	nop                     ;25d4	00
	nop                     ;25d5	00
	nop                     ;25d6	00
	nop                     ;25d7	00
	nop                     ;25d8	00
	nop                     ;25d9	00
	nop                     ;25da	00
	nop                     ;25db	00
	nop                     ;25dc	00
	nop                     ;25dd	00
	nop                     ;25de	00
	nop                     ;25df	00
	nop                     ;25e0	00
	nop                     ;25e1	00
	nop                     ;25e2	00
	nop                     ;25e3	00
	nop                     ;25e4	00
	nop                     ;25e5	00
	nop                     ;25e6	00
	nop                     ;25e7	00
	nop                     ;25e8	00
	nop                     ;25e9	00
	nop                     ;25ea	00
	nop                     ;25eb	00
	nop                     ;25ec	00
	nop                     ;25ed	00
	nop                     ;25ee	00
	nop                     ;25ef	00
	nop                     ;25f0	00
	nop                     ;25f1	00
	nop                     ;25f2	00
	nop                     ;25f3	00
	nop                     ;25f4	00
	nop                     ;25f5	00
	nop                     ;25f6	00
	nop                     ;25f7	00
	nop                     ;25f8	00
	nop                     ;25f9	00
	nop                     ;25fa	00
	nop                     ;25fb	00
	nop                     ;25fc	00
	nop                     ;25fd	00
	nop                     ;25fe	00
	nop                     ;25ff	00
	nop                     ;2600	00
	nop                     ;2601	00
	nop                     ;2602	00
	nop                     ;2603	00
	nop                     ;2604	00
	nop                     ;2605	00
	nop                     ;2606	00
	nop                     ;2607	00
	nop                     ;2608	00
	nop                     ;2609	00
	nop                     ;260a	00
	nop                     ;260b	00
	nop                     ;260c	00
	nop                     ;260d	00
	nop                     ;260e	00
	nop                     ;260f	00
	nop                     ;2610	00
	nop                     ;2611	00
	nop                     ;2612	00
	nop                     ;2613	00
	nop                     ;2614	00
	nop                     ;2615	00
	nop                     ;2616	00
	nop                     ;2617	00
	nop                     ;2618	00
	nop                     ;2619	00
	nop                     ;261a	00
	nop                     ;261b	00
	nop                     ;261c	00
	nop                     ;261d	00
	nop                     ;261e	00
	nop                     ;261f	00
	nop                     ;2620	00
	nop                     ;2621	00
	nop                     ;2622	00
	nop                     ;2623	00
	nop                     ;2624	00
	nop                     ;2625	00
	nop                     ;2626	00
	nop                     ;2627	00
	nop                     ;2628	00
	nop                     ;2629	00
	nop                     ;262a	00
	nop                     ;262b	00
	nop                     ;262c	00
	nop                     ;262d	00
	nop                     ;262e	00
	nop                     ;262f	00
	nop                     ;2630	00
	nop                     ;2631	00
	nop                     ;2632	00
	nop                     ;2633	00
	nop                     ;2634	00
	nop                     ;2635	00
	nop                     ;2636	00
	nop                     ;2637	00
	nop                     ;2638	00
	nop                     ;2639	00
	nop                     ;263a	00
	nop                     ;263b	00
	nop                     ;263c	00
	nop                     ;263d	00
	nop                     ;263e	00
	nop                     ;263f	00
	nop                     ;2640	00
	nop                     ;2641	00
	nop                     ;2642	00
	nop                     ;2643	00
	nop                     ;2644	00
	nop                     ;2645	00
	nop                     ;2646	00
	nop                     ;2647	00
	nop                     ;2648	00
	nop                     ;2649	00
	nop                     ;264a	00
	nop                     ;264b	00
	nop                     ;264c	00
	nop                     ;264d	00
	nop                     ;264e	00
	nop                     ;264f	00
	nop                     ;2650	00
	nop                     ;2651	00
	nop                     ;2652	00
	nop                     ;2653	00
	nop                     ;2654	00
	nop                     ;2655	00
	nop                     ;2656	00
	nop                     ;2657	00
	nop                     ;2658	00
	nop                     ;2659	00
	nop                     ;265a	00
	nop                     ;265b	00
	nop                     ;265c	00
	nop                     ;265d	00
	nop                     ;265e	00
	nop                     ;265f	00
	nop                     ;2660	00
	nop                     ;2661	00
	nop                     ;2662	00
	nop                     ;2663	00
	nop                     ;2664	00
	nop                     ;2665	00
	nop                     ;2666	00
	nop                     ;2667	00
	nop                     ;2668	00
	nop                     ;2669	00
	nop                     ;266a	00
	nop                     ;266b	00
	nop                     ;266c	00
	nop                     ;266d	00
	nop                     ;266e	00
	nop                     ;266f	00
	nop                     ;2670	00
	nop                     ;2671	00
	nop                     ;2672	00
	nop                     ;2673	00
	nop                     ;2674	00
	nop                     ;2675	00
	nop                     ;2676	00
	nop                     ;2677	00
	nop                     ;2678	00
	nop                     ;2679	00
	nop                     ;267a	00
	nop                     ;267b	00
	nop                     ;267c	00
	nop                     ;267d	00
	nop                     ;267e	00
	nop                     ;267f	00
	nop                     ;2680	00
	nop                     ;2681	00
	nop                     ;2682	00
	nop                     ;2683	00
	nop                     ;2684	00
	nop                     ;2685	00
	nop                     ;2686	00
	nop                     ;2687	00
	nop                     ;2688	00
	nop                     ;2689	00
	nop                     ;268a	00
	nop                     ;268b	00
	nop                     ;268c	00
	nop                     ;268d	00
	nop                     ;268e	00
	nop                     ;268f	00
	nop                     ;2690	00
	nop                     ;2691	00
	nop                     ;2692	00
	nop                     ;2693	00
	nop                     ;2694	00
	nop                     ;2695	00
	nop                     ;2696	00
	nop                     ;2697	00
	nop                     ;2698	00
	nop                     ;2699	00
	nop                     ;269a	00
	nop                     ;269b	00
	nop                     ;269c	00
	nop                     ;269d	00
	nop                     ;269e	00
	nop                     ;269f	00
	nop                     ;26a0	00
	nop                     ;26a1	00
	nop                     ;26a2	00
	nop                     ;26a3	00
	nop                     ;26a4	00
	nop                     ;26a5	00
	nop                     ;26a6	00
	nop                     ;26a7	00
	nop                     ;26a8	00
	nop                     ;26a9	00
	nop                     ;26aa	00
	nop                     ;26ab	00
	nop                     ;26ac	00
	inc sp                  ;26ad	33
	inc sp                  ;26ae	33
	inc sp                  ;26af	33
	inc sp                  ;26b0	33
	ex (sp),hl              ;26b1	e3
	ld ($5b54),hl           ;26b2	22 54 5b
	pop hl                  ;26b5	e1
	call L32CC              ;26b6	cd cc 32
	push hl                 ;26b9	e5
	ld hl,($5b54)           ;26ba	2a 54 5b
	ex (sp),hl              ;26bd	e3
	push $007b              ;26be	ed 8a 00 7b
	jp L3E93                ;26c2	c3 93 3e

L26C5:
	ld ($5b56),a            ;26c5	32 56 5b
	ld a,$87                ;26c8	3e 87

L26CA:
	ld ($5b54),bc           ;26ca	ed 43 54 5b
	ex (sp),hl              ;26ce	e3
	ld c,(hl)               ;26cf	4e

L26D0:
	inc hl                  ;26d0	23
	ld b,(hl)               ;26d1	46
	inc hl                  ;26d2	23
	ex (sp),hl              ;26d3	e3

L26D4:
	push $2705              ;26d4	ed 8a 27 05
	push bc                 ;26d8	c5
	push af                 ;26d9	f5
	ld a,($5b56)            ;26da	3a 56 5b
	jr L26FB                ;26dd	18 1c
L26DF:
	defb 0x32                                    ;26df	32	2

L26E0:
	ld d,(hl)               ;26e0	56
	ld e,e                  ;26e1	5b
	ld a,$8a                ;26e2	3e 8a
	jr L26CA                ;26e4	18 e4
Data26E6:
	defb 0xcd,0x68                               ;26e6	cd 68	.h

L26E8:
	nop                     ;26e8	00
	cp d                    ;26e9	ba

L26EA:
	add hl,hl               ;26ea	29
	ret                     ;26eb	c9
Data26EC:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;26ec	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0xed                     ;26f4	00 00 00 ed	....

L26F8:
	ld b,e                  ;26f8	43
	ld d,h                  ;26f9	54

L26FA:
	ld e,e                  ;26fa	5b

L26FB:
	pop bc                  ;26fb	c1

L26FC:
	ld c,$e3                ;26fc	0e e3

L26FE:
	out (c),b               ;26fe	ed 41

L2700:
	ld bc,($5b54)           ;2700	ed 4b 54 5b
	ret                     ;2704	c9
	ld h,$00                ;2705	26 00
	sbc e                   ;2707	9b

L2708:
	daa                     ;2708	27
	ld h,$00                ;2709	26 00
	xor d                   ;270b	aa
	daa                     ;270c	27
	dec h                   ;270d	25

; tentative entry (word / jump table scan)
T270E:
	jr z,L26FE              ;270e	28 ee
	daa                     ;2710	27
	rla                     ;2711	17
	jr z,L272B              ;2712	28 17
	jr z,L2779              ;2714	28 63
	add hl,hl               ;2716	29
	ld h,$00                ;2717	26 00
	cp (hl)                 ;2719	be
	daa                     ;271a	27
	add l                   ;271b	85
	jr z,L26E8              ;271c	28 ca
	daa                     ;271e	27
	rst $08                 ;271f	cf

L2720:
	daa                     ;2720	27
	in a,($27)              ;2721	db 27
	ld h,$00                ;2723	26 00
	xor $27                 ;2725	ee 27
	jp m,L0827              ;2727	fa 27 08
	jr z,L2743              ;272a	28 17
	jr z,L2753              ;272c	28 25

L272E:
	jr z,L2763              ;272e	28 33

L2730:
	jr z,L2770              ;2730	28 3e
	jr z,L277B              ;2732	28 47
	jr z,L2785              ;2734	28 4f
	jr z,L2790              ;2736	28 58
	jr z,L27A1              ;2738	28 67
	jr z,L27A9              ;273a	28 6d
	jr z,L26E8              ;273c	28 aa
	jr z,L26EA              ;273e	28 aa
	jr z,L26C7              ;2740	28 85
	jr z,L26D0              ;2742	28 8c
	jr z,L27AD              ;2744	28 67
	jr z,L26E0              ;2746	28 98
	jr z,L2799              ;2748	28 4f
	jr z,L26ED              ;274a	28 a1
	jr z,L26F8              ;274c	28 aa
	jr z,L26FA              ;274e	28 aa
	jr z,L26FC              ;2750	28 aa
	jr z,L2708              ;2752	28 b4
	jr z,L2700              ;2754	28 aa
	jr z,L2702              ;2756	28 aa
	jr z,T270E              ;2758	28 b4
	jr z,L2720              ;275a	28 c4

L275C:
	jr z,L2784              ;275c	28 26

L275E:
	nop                     ;275e	00
	push de                 ;275f	d5

L2760:
	jr z,L2749              ;2760	28 e7

L2762:
	jr z,L275C              ;2762	28 f8
	jr z,L275E              ;2764	28 f8
	jr z,L2760              ;2766	28 f8
	jr z,L2762              ;2768	28 f8
	jr z,L2771              ;276a	28 05
	add hl,hl               ;276c	29
	ld d,$29                ;276d	16 29
	dec h                   ;276f	25

L2770:
	jr z,L2799              ;2770	28 27
	add hl,hl               ;2772	29
	ld (hl),$29             ;2773	36 29
	ld b,h                  ;2775	44
	add hl,hl               ;2776	29
	ld d,$29                ;2777	16 29

L2779:
	dec h                   ;2779	25
	jr z,L27C0              ;277a	28 44
	add hl,hl               ;277c	29
	ld d,d                  ;277d	52
	add hl,hl               ;277e	29
	ld h,e                  ;277f	63
	add hl,hl               ;2780	29
	xor d                   ;2781	aa
	jr z,L272E              ;2782	28 aa

L2784:
	jr z,L2730              ;2784	28 aa
	jr z,L27F9              ;2786	28 71
	add hl,hl               ;2788	29
	sub l                   ;2789	95
	add hl,hl               ;278a	29
	and c                   ;278b	a1
	add hl,hl               ;278c	29
	or l                    ;278d	b5
	add hl,hl               ;278e	29
	cp a                    ;278f	bf

L2790:
	add hl,hl               ;2790	29
	ld a,l                  ;2791	7d
	add hl,hl               ;2792	29
	ret                     ;2793	c9
Data2794:
	defb 0x29,0xda,0x29,0xe2,0x29                ;2794	29 da 29 e2 29	).).)

L2799:
	ld h,$00                ;2799	26 00
	ld d,a                  ;279b	57
	ld (hl),d               ;279c	72
	ld l,a                  ;279d	6f
	ld l,(hl)               ;279e	6e
	ld h,a                  ;279f	67
	jr nz,T2808             ;27a0	20 66
	ld l,c                  ;27a2	69
	ld l,h                  ;27a3	6c
	ld h,l                  ;27a4	65
	jr nz,L281B             ;27a5	20 74
	ld a,c                  ;27a7	79
	ld (hl),b               ;27a8	70

L27A9:
	push hl                 ;27a9	e5
	ld d,h                  ;27aa	54
	ld l,a                  ;27ab	6f
	ld l,a                  ;27ac	6f

L27AD:
	jr nz,L281C             ;27ad	20 6d
	ld h,c                  ;27af	61
	ld l,(hl)               ;27b0	6e
	ld a,c                  ;27b1	79
	jr nz,L2824             ;27b2	20 70
	ld h,c                  ;27b4	61
	ld (hl),d               ;27b5	72
	ld h,l                  ;27b6	65
	ld l,(hl)               ;27b7	6e
	ld (hl),h               ;27b8	74
	ld l,b                  ;27b9	68
	ld h,l                  ;27ba	65
	ld (hl),e               ;27bb	73
	ld h,l                  ;27bc	65
	di                      ;27bd	f3
	ld c,c                  ;27be	49
	ld l,(hl)               ;27bf	6e

L27C0:
	halt                    ;27c0	76
Text27C1:
	defb 0x61,0x6c,0x69,0x64,0x20,0x6e,0x6f,0x74 ;27c1	61 6c 69 64 20 6e 6f 74	alid not
	defb 0xe5                                    ;27c9	e5	.
	ld c,(hl)               ;27ca	4e
	ld l,a                  ;27cb	6f
	ld (hl),h               ;27cc	74
	ld h,l                  ;27cd	65
	jr nz,T281F             ;27ce	20 4f
	ld (hl),l               ;27d0	75
	ld (hl),h               ;27d1	74
	jr nz,L2843             ;27d2	20 6f
	ld h,(hl)               ;27d4	66
	jr nz,L2849             ;27d5	20 72
	ld h,c                  ;27d7	61
	ld l,(hl)               ;27d8	6e
	ld h,a                  ;27d9	67
	push hl                 ;27da	e5
	ld d,h                  ;27db	54
	ld l,a                  ;27dc	6f
	ld l,a                  ;27dd	6f
	jr nz,L284D             ;27de	20 6d
	ld h,c                  ;27e0	61
	ld l,(hl)               ;27e1	6e
	ld a,c                  ;27e2	79
	jr nz,L2859             ;27e3	20 74
	ld l,c                  ;27e5	69
	ld h,l                  ;27e6	65
	ld h,h                  ;27e7	64
	jr nz,T2858             ;27e8	20 6e
	ld l,a                  ;27ea	6f
	ld (hl),h               ;27eb	74
	ld h,l                  ;27ec	65
	di                      ;27ed	f3
	ld b,d                  ;27ee	42
	ld h,c                  ;27ef	61
	ld h,h                  ;27f0	64
	jr nz,L2859             ;27f1	20 66
	ld l,c                  ;27f3	69
	ld l,h                  ;27f4	6c
	ld h,l                  ;27f5	65
	ld l,(hl)               ;27f6	6e
	ld h,c                  ;27f7	61
	ld l,l                  ;27f8	6d

L27F9:
	push hl                 ;27f9	e5
	ld b,d                  ;27fa	42
	ld h,c                  ;27fb	61
	ld h,h                  ;27fc	64
	jr nz,L286F             ;27fd	20 70
	ld h,c                  ;27ff	61
	ld (hl),d               ;2800	72
	ld h,c                  ;2801	61
	ld l,l                  ;2802	6d
	ld h,l                  ;2803	65
	ld (hl),h               ;2804	74
	ld h,l                  ;2805	65
	ld (hl),d               ;2806	72
	di                      ;2807	f3

; tentative entry (word / jump table scan)
T2808:
	ld b,h                  ;2808	44
	ld (hl),d               ;2809	72
	ld l,c                  ;280a	69
	halt                    ;280b	76
Text280C:
	defb 0x65,0x20,0x6e,0x6f,0x74,0x20,0x66,0x6f ;280c	65 20 6e 6f 74 20 66 6f	e not fo
	defb 0x75,0x6e,0xe4,0x46,0x69,0x6c,0x65,0x20 ;2814	75 6e e4 46 69 6c 65 20	un.File 

L281C:
	ld l,(hl)               ;281c	6e
	ld l,a                  ;281d	6f
	ld (hl),h               ;281e	74

; tentative entry (word / jump table scan)
T281F:
	jr nz,L2887             ;281f	20 66
	ld l,a                  ;2821	6f
	ld (hl),l               ;2822	75
	ld l,(hl)               ;2823	6e

L2824:
	call po,$6c41           ;2824	e4 41 6c
	ld (hl),d               ;2827	72
	ld h,l                  ;2828	65
	ld h,c                  ;2829	61
	ld h,h                  ;282a	64
	ld a,c                  ;282b	79
	jr nz,L2893             ;282c	20 65
	ld a,b                  ;282e	78
	ld l,c                  ;282f	69
	ld (hl),e               ;2830	73

L2831:
	ld (hl),h               ;2831	74
	di                      ;2832	f3
	ld b,l                  ;2833	45
	ld l,(hl)               ;2834	6e
	ld h,h                  ;2835	64
	jr nz,L28A7             ;2836	20 6f
	ld h,(hl)               ;2838	66
	jr nz,L28A1             ;2839	20 66
	ld l,c                  ;283b	69
	ld l,h                  ;283c	6c
	push hl                 ;283d	e5
	ld b,h                  ;283e	44
	ld l,c                  ;283f	69
	ld (hl),e               ;2840	73
	ld l,e                  ;2841	6b
	jr nz,T28AA             ;2842	20 66
	ld (hl),l               ;2844	75
	ld l,h                  ;2845	6c
	call pe,$6944           ;2846	ec 44 69

L2849:
	ld (hl),d               ;2849	72
	jr nz,L28B2             ;284a	20 66
	ld (hl),l               ;284c	75

L284D:
	ld l,h                  ;284d	6c
	call pe,$6552           ;284e	ec 52 65
	ld h,c                  ;2851	61
	ld h,h                  ;2852	64
	jr nz,T28C4             ;2853	20 6f
	ld l,(hl)               ;2855	6e
	ld l,h                  ;2856	6c
	ld sp,hl                ;2857	f9

; tentative entry (word / jump table scan)
T2858:
	ld b,d                  ;2858	42

L2859:
	ld h,c                  ;2859	61
	ld h,h                  ;285a	64
	jr nz,L28C3             ;285b	20 66
	ld l,c                  ;285d	69
	ld l,h                  ;285e	6c
	ld h,l                  ;285f	65
	jr nz,L28D0             ;2860	20 6e
	ld (hl),l               ;2862	75
	ld l,l                  ;2863	6d
	ld h,d                  ;2864	62
	ld h,l                  ;2865	65
	jp p,$6e49              ;2866	f2 49 6e
	jr nz,L28E0             ;2869	20 75
	ld (hl),e               ;286b	73
	push hl                 ;286c	e5
	ld c,(hl)               ;286d	4e
	ld l,a                  ;286e	6f

L286F:
	jr nz,L28E3             ;286f	20 72
	ld h,l                  ;2871	65
	ld l,(hl)               ;2872	6e
	ld h,c                  ;2873	61
	ld l,l                  ;2874	6d
	ld h,l                  ;2875	65
	jr nz,L28DA             ;2876	20 62
	ld h,l                  ;2878	65
	ld (hl),h               ;2879	74
	ld (hl),a               ;287a	77
	ld h,l                  ;287b	65
	ld h,l                  ;287c	65
	ld l,(hl)               ;287d	6e
	jr nz,L28E4             ;287e	20 64
	ld (hl),d               ;2880	72
	ld l,c                  ;2881	69
	halt                    ;2882	76
Text2883:
	defb 0x65,0xf3,0x54,0x6f,0x6f,0x20,0x62,0x69 ;2883	65 f3 54 6f 6f 20 62 69	e.Too bi
	defb 0xe7,0x4e,0x6f,0x74,0x20,0x62,0x6f,0x6f ;288b	e7 4e 6f 74 20 62 6f 6f	.Not boo
	defb 0x74,0x61,0x62,0x6c,0xe5,0x4e,0x6f,0x74 ;2893	74 61 62 6c e5 4e 6f 74	tabl.Not
	defb 0x20,0x72,0x65,0x61,0x64,0xf9,0x53,0x65 ;289b	20 72 65 61 64 f9 53 65	 read.Se
	defb 0x65,0x6b,0x20,0x66,0x61,0x69,0xec      ;28a3	65 6b 20 66 61 69 ec	ek fai.

; tentative entry (word / jump table scan)
T28AA:
	ld b,h                  ;28aa	44
	ld l,c                  ;28ab	69
	ld (hl),e               ;28ac	73
	ld l,e                  ;28ad	6b
	jr nz,L2915             ;28ae	20 65
	ld (hl),d               ;28b0	72
	ld (hl),d               ;28b1	72

L28B2:
	ld l,a                  ;28b2	6f
	jp p,$6e55              ;28b3	f2 55 6e
	ld (hl),e               ;28b6	73
	ld (hl),l               ;28b7	75
	ld l,c                  ;28b8	69
	ld (hl),h               ;28b9	74
	ld h,c                  ;28ba	61
	ld h,d                  ;28bb	62
	ld l,h                  ;28bc	6c
	ld h,l                  ;28bd	65
	jr nz,L292D             ;28be	20 6d
	ld h,l                  ;28c0	65
	ld h,h                  ;28c1	64
	ld l,c                  ;28c2	69

L28C3:
	pop hl                  ;28c3	e1

; tentative entry (word / jump table scan)
T28C4:
	ld c,c                  ;28c4	49
	ld l,(hl)               ;28c5	6e
	halt                    ;28c6	76
Text28C7:
	defb 0x61,0x6c,0x69,0x64,0x20,0x61,0x74,0x74 ;28c7	61 6c 69 64 20 61 74 74	alid att
	defb 0x72,0x69,0x62,0x75,0x74,0xe5,0x44,0x65 ;28cf	72 69 62 75 74 e5 44 65	ribut.De
	defb 0x73,0x74,0x20                          ;28d7	73 74 20	st 

L28DA:
	ld h,e                  ;28da	63
	ld h,c                  ;28db	61
	ld l,(hl)               ;28dc	6e
	daa                     ;28dd	27
	ld (hl),h               ;28de	74
	jr nz,L2943             ;28df	20 62
	ld h,l                  ;28e1	65
	jr nz,L295B             ;28e2	20 77

L28E4:
	ld l,c                  ;28e4	69
	ld l,h                  ;28e5	6c
	call po,$6544           ;28e6	e4 44 65
	ld (hl),e               ;28e9	73
	ld (hl),h               ;28ea	74
	jr nz,L295A             ;28eb	20 6d
	ld (hl),l               ;28ed	75
	ld (hl),e               ;28ee	73
	ld (hl),h               ;28ef	74
	jr nz,L2954             ;28f0	20 62
	ld h,l                  ;28f2	65
	jr nz,L2965             ;28f3	20 70
	ld h,c                  ;28f5	61
	ld (hl),h               ;28f6	74
	ret pe                  ;28f7	e8
	ld c,c                  ;28f8	49
	ld l,(hl)               ;28f9	6e
	halt                    ;28fa	76
Data28FB:
	defb 0x61,0x6c,0x69                          ;28fb	61 6c 69	ali
	ld h,h                  ;28fe	64
	jr nz,L2965             ;28ff	20 64
	ld (hl),d               ;2901	72
	ld l,c                  ;2902	69
	halt                    ;2903	76
Data2904:
	defb 0xe5                                    ;2904	e5	.
	ld b,e                  ;2905	43
	ld l,a                  ;2906	6f
	ld h,h                  ;2907	64
	ld h,l                  ;2908	65
	jr nz,L2977             ;2909	20 6c
	ld h,l                  ;290b	65
	ld l,(hl)               ;290c	6e
	ld h,a                  ;290d	67
	ld (hl),h               ;290e	74
	ld l,b                  ;290f	68
	jr nz,L2977             ;2910	20 65
	ld (hl),d               ;2912	72
	ld (hl),d               ;2913	72
	ld l,a                  ;2914	6f

L2915:
	jp p,$6e49              ;2915	f2 49 6e
	halt                    ;2918	76
Text2919:
	defb 0x61,0x6c,0x69,0x64,0x20,0x70,0x61,0x72 ;2919	61 6c 69 64 20 70 61 72	alid par
	defb 0x74,0x69,0x74,0x69,0x6f,0xee           ;2921	74 69 74 69 6f ee	titio.
	ld c,(hl)               ;2927	4e
	ld l,a                  ;2928	6f
	ld (hl),h               ;2929	74
	jr nz,T2995             ;292a	20 69
	ld l,l                  ;292c	6d

L292D:
	ld (hl),b               ;292d	70
	ld l,h                  ;292e	6c
	ld h,l                  ;292f	65
	ld l,l                  ;2930	6d
	ld h,l                  ;2931	65
	ld l,(hl)               ;2932	6e
	ld (hl),h               ;2933	74
	ld h,l                  ;2934	65
	call po,$6150           ;2935	e4 50 61
	ld (hl),d               ;2938	72
	ld (hl),h               ;2939	74
	ld l,c                  ;293a	69
	ld (hl),h               ;293b	74
	ld l,c                  ;293c	69
	ld l,a                  ;293d	6f
	ld l,(hl)               ;293e	6e
	jr nz,L29B0             ;293f	20 6f
	ld (hl),b               ;2941	70
	ld h,l                  ;2942	65

L2943:
	xor $4f                 ;2943	ee 4f
	ld (hl),l               ;2945	75
	ld (hl),h               ;2946	74
	jr nz,L29B8             ;2947	20 6f
	ld h,(hl)               ;2949	66
	jr nz,L29B4             ;294a	20 68
	ld h,c                  ;294c	61
	ld l,(hl)               ;294d	6e
	ld h,h                  ;294e	64
	ld l,h                  ;294f	6c
	ld h,l                  ;2950	65
	di                      ;2951	f3
	ld c,(hl)               ;2952	4e
	ld l,a                  ;2953	6f

L2954:
	jr nz,T29C9             ;2954	20 73
	ld (hl),a               ;2956	77
	ld h,c                  ;2957	61
	ld (hl),b               ;2958	70
	jr nz,L29CB             ;2959	20 70

L295B:
	ld h,c                  ;295b	61
	ld (hl),d               ;295c	72
	ld (hl),h               ;295d	74
	ld l,c                  ;295e	69
	ld (hl),h               ;295f	74
	ld l,c                  ;2960	69
	ld l,a                  ;2961	6f
	xor $49                 ;2962	ee 49
	ld l,(hl)               ;2964	6e

L2965:
	halt                    ;2965	76
Text2966:
	defb 0x61,0x6c,0x69,0x64,0x20,0x64,0x65,0x76 ;2966	61 6c 69 64 20 64 65 76	alid dev
	defb 0x69,0x63,0xe5                          ;296e	69 63 e5	ic.
	ld c,c                  ;2971	49
	ld l,(hl)               ;2972	6e
	halt                    ;2973	76
Data2974:
	defb 0x61,0x6c,0x69                          ;2974	61 6c 69	ali

L2977:
	ld h,h                  ;2977	64
	jr nz,L29EA             ;2978	20 70
	ld h,c                  ;297a	61
	ld (hl),h               ;297b	74
	ret pe                  ;297c	e8
	ld b,(hl)               ;297d	46
	ld (hl),d               ;297e	72
	ld h,c                  ;297f	61
	ld h,a                  ;2980	67
	ld l,l                  ;2981	6d
	ld h,l                  ;2982	65
	ld l,(hl)               ;2983	6e
	ld (hl),h               ;2984	74
	ld h,l                  ;2985	65
	ld h,h                  ;2986	64
	jr nz,L29B6             ;2987	20 2d
	jr nz,L2A00             ;2989	20 75
	ld (hl),e               ;298b	73
	ld h,l                  ;298c	65
	jr nz,L29BD             ;298d	20 2e
	ld b,h                  ;298f	44
	ld b,l                  ;2990	45
	ld b,(hl)               ;2991	46
	ld d,d                  ;2992	52
	ld b,c                  ;2993	41
	rst $00                 ;2994	c7

; tentative entry (word / jump table scan)
T2995:
	ld c,c                  ;2995	49
	ld l,(hl)               ;2996	6e
	halt                    ;2997	76
Text2998:
	defb 0x61,0x6c,0x69,0x64,0x20,0x6d,0x6f,0x64 ;2998	61 6c 69 64 20 6d 6f 64	alid mod
	defb 0xe5,0x44,0x69,0x72,0x65,0x63,0x74,0x20 ;29a0	e5 44 69 72 65 63 74 20	.Direct 
	defb 0x63,0x6f,0x6d,0x6d,0x61,0x6e,0x64,0x20 ;29a8	63 6f 6d 6d 61 6e 64 20	command 
	defb 0x65,0x72,0x72,0x6f,0xf2,0x4c,0x6f,0x6f ;29b0	65 72 72 6f f2 4c 6f 6f	erro.Loo
	defb 0x70,0x20,0x65,0x72,0x72,0x6f,0xf2,0x4e ;29b8	70 20 65 72 72 6f f2 4e	p erro.N
	defb 0x6f,0x20,0x44,0x45,0x46,0x50,0x52,0x4f ;29c0	6f 20 44 45 46 50 52 4f	o DEFPRO
	defb 0xc3                                    ;29c8	c3	.

; tentative entry (word / jump table scan)
T29C9:
	ld b,h                  ;29c9	44
	ld l,a                  ;29ca	6f

L29CB:
	ld (hl),h               ;29cb	74
	jr nz,L2A31             ;29cc	20 63
	ld l,a                  ;29ce	6f
	ld l,l                  ;29cf	6d
	ld l,l                  ;29d0	6d
	ld h,c                  ;29d1	61
	ld l,(hl)               ;29d2	6e
	ld h,h                  ;29d3	64
	jr nz,L2A3B             ;29d4	20 65
	ld (hl),d               ;29d6	72
	ld (hl),d               ;29d7	72
	ld l,a                  ;29d8	6f
	jp p,$6f4e              ;29d9	f2 4e 6f
	jr nz,L2A23             ;29dc	20 45
	ld c,(hl)               ;29de	4e
	ld b,h                  ;29df	44
	ld c,c                  ;29e0	49
	add $4e                 ;29e1	c6 4e
	ld l,a                  ;29e3	6f
	jr nz,L2A52             ;29e4	20 6c
	ld h,c                  ;29e6	61
	ld h,d                  ;29e7	62
	ld h,l                  ;29e8	65
	call pe,$6f4c           ;29e9	ec 4c 6f
	ld h,a                  ;29ec	67
	ld l,c                  ;29ed	69
	ld h,e                  ;29ee	63
	ld h,c                  ;29ef	61
	ld l,h                  ;29f0	6c
	jr nz,L2A57             ;29f1	20 64
	ld (hl),d               ;29f3	72
	ld l,c                  ;29f4	69
	halt                    ;29f5	76
Text29F6:
	defb 0x65,0x73,0x3a,0x20,0x00,0x52,0x65,0x66 ;29f6	65 73 3a 20 00 52 65 66	es: .Ref
	ld l,a                  ;29fe	6f
	ld (hl),d               ;29ff	72

L2A00:
	ld l,l                  ;2a00	6d
	ld h,c                  ;2a01	61
	ld (hl),h               ;2a02	74
	jr nz,L2A6B             ;2a03	20 66
	ld l,a                  ;2a05	6f
	ld (hl),d               ;2a06	72
	ld l,l                  ;2a07	6d
	ld h,c                  ;2a08	61
	ld (hl),h               ;2a09	74
	ld (hl),h               ;2a0a	74
	ld h,l                  ;2a0b	65
	ld h,h                  ;2a0c	64
	jr nz,L2A73             ;2a0d	20 64
	ld l,c                  ;2a0f	69
	ld (hl),e               ;2a10	73
	ld l,e                  ;2a11	6b
	nop                     ;2a12	00
	nop                     ;2a13	00
	nop                     ;2a14	00
	nop                     ;2a15	00
	nop                     ;2a16	00
	nop                     ;2a17	00
	nop                     ;2a18	00
	nop                     ;2a19	00
	nop                     ;2a1a	00
	nop                     ;2a1b	00
	nop                     ;2a1c	00
	nop                     ;2a1d	00
	nop                     ;2a1e	00
	nop                     ;2a1f	00
	nop                     ;2a20	00
	nop                     ;2a21	00
	nop                     ;2a22	00

L2A23:
	nop                     ;2a23	00
	nop                     ;2a24	00
	nop                     ;2a25	00
	nop                     ;2a26	00
	nop                     ;2a27	00
	nop                     ;2a28	00
	nop                     ;2a29	00
	nop                     ;2a2a	00
	nop                     ;2a2b	00
	nop                     ;2a2c	00
	nop                     ;2a2d	00
	nop                     ;2a2e	00
	nop                     ;2a2f	00
	nop                     ;2a30	00

L2A31:
	nop                     ;2a31	00
	nop                     ;2a32	00
	nop                     ;2a33	00
	nop                     ;2a34	00
	nop                     ;2a35	00
	nop                     ;2a36	00
	nop                     ;2a37	00
	nop                     ;2a38	00
	nop                     ;2a39	00
	nop                     ;2a3a	00

L2A3B:
	nop                     ;2a3b	00
	nop                     ;2a3c	00
	nop                     ;2a3d	00
	nop                     ;2a3e	00
	nop                     ;2a3f	00
	nop                     ;2a40	00
	nop                     ;2a41	00
	nop                     ;2a42	00
	nop                     ;2a43	00
	nop                     ;2a44	00
	nop                     ;2a45	00
	nop                     ;2a46	00
	nop                     ;2a47	00
	nop                     ;2a48	00
	nop                     ;2a49	00
	nop                     ;2a4a	00
	nop                     ;2a4b	00
	nop                     ;2a4c	00
	nop                     ;2a4d	00
	nop                     ;2a4e	00
	nop                     ;2a4f	00
	nop                     ;2a50	00
	nop                     ;2a51	00

L2A52:
	nop                     ;2a52	00
	nop                     ;2a53	00
	nop                     ;2a54	00
	nop                     ;2a55	00
	nop                     ;2a56	00

L2A57:
	nop                     ;2a57	00
	nop                     ;2a58	00
	nop                     ;2a59	00
	nop                     ;2a5a	00
	nop                     ;2a5b	00
	nop                     ;2a5c	00
	nop                     ;2a5d	00
	nop                     ;2a5e	00
	nop                     ;2a5f	00
	nop                     ;2a60	00
	nop                     ;2a61	00
	nop                     ;2a62	00
	nop                     ;2a63	00
	nop                     ;2a64	00
	nop                     ;2a65	00
	nop                     ;2a66	00
	nop                     ;2a67	00
	nop                     ;2a68	00
	nop                     ;2a69	00
	nop                     ;2a6a	00

L2A6B:
	nop                     ;2a6b	00
	nop                     ;2a6c	00
	nop                     ;2a6d	00
	nop                     ;2a6e	00
	nop                     ;2a6f	00
	nop                     ;2a70	00
	nop                     ;2a71	00
	nop                     ;2a72	00

L2A73:
	nop                     ;2a73	00
	nop                     ;2a74	00
	nop                     ;2a75	00
	nop                     ;2a76	00
	nop                     ;2a77	00
	nop                     ;2a78	00
	nop                     ;2a79	00
	nop                     ;2a7a	00
	nop                     ;2a7b	00
	ld b,l                  ;2a7c	45
	ld (hl),d               ;2a7d	72
	ld h,c                  ;2a7e	61
	ld (hl),e               ;2a7f	73
	ld h,l                  ;2a80	65
	jr nz,L2A83             ;2a81	20 00

L2A83:
	nop                     ;2a83	00
	nop                     ;2a84	00
	ld d,b                  ;2a85	50
	ld h,c                  ;2a86	61
	ld (hl),e               ;2a87	73
	ld (hl),h               ;2a88	74
	ld h,l                  ;2a89	65
	jr nz,L2AF4             ;2a8a	20 68
	ld h,l                  ;2a8c	65
	ld (hl),d               ;2a8d	72
	ld h,l                  ;2a8e	65
	nop                     ;2a8f	00
	ld b,e                  ;2a90	43
	ld l,a                  ;2a91	6f
	ld (hl),b               ;2a92	70
	ld a,c                  ;2a93	79
	nop                     ;2a94	00
	ld c,l                  ;2a95	4d
	ld l,a                  ;2a96	6f
	halt                    ;2a97	76
Text2A98:
	defb 0x65,0x00,0x4d,0x6f,0x76,0x65,0x20,0x68 ;2a98	65 00 4d 6f 76 65 20 68	e.Move h
	defb 0x65,0x72,0x65,0x00,0x7f,0x31,0x39,0x38 ;2aa0	65 72 65 00 7f 31 39 38	ere..198
	defb 0x32,0x2c,0x20,0x31,0x39,0x38,0x36,0x2c ;2aa8	32 2c 20 31 39 38 36 2c	2, 1986,
	defb 0x20,0x31,0x39,0x38,0x37,0x20,0x41,0x6d ;2ab0	20 31 39 38 37 20 41 6d	 1987 Am
	defb 0x73,0x74,0x72,0x61,0x64,0x20,0x50,0x6c ;2ab8	73 74 72 61 64 20 50 6c	strad Pl
	defb 0x63,0x2e,0x0d,0x7f,0x32,0x30,0x30,0x30 ;2ac0	63 2e 0d 7f 32 30 30 30	c...2000
	defb 0x2d,0x32,0x30,0x32,0x34,0x20,0x47,0x61 ;2ac8	2d 32 30 32 34 20 47 61	-2024 Ga
	defb 0x72,0x72,0x79,0x20,0x4c,0x61,0x6e,0x63 ;2ad0	72 72 79 20 4c 61 6e 63	rry Lanc
	defb 0x61,0x73,0x74,0x65,0x72,0x20,0x76,0x32 ;2ad8	61 73 74 65 72 20 76 32	aster v2
	defb 0x2e,0x30,0xb9,0x01,0x01,0x00,0x11,0x0f ;2ae0	2e 30 b9 01 01 00 11 0f	.0......
	defb 0x27,0x21,0x0a                          ;2ae8	27 21 0a	'!.
	nop                     ;2aeb	00
	push hl                 ;2aec	e5
	pop ix                  ;2aed	dd e1
	call L0080              ;2aef	cd 80 00
	pop bc                  ;2af2	c1
	ld a,(de)               ;2af3	1a

L2AF4:
	ret                     ;2af4	c9

L2AF5:
	nextreg $8e,$09         ;2af5	ed 91 8e 09

L2AF9:
	ex af,af'               ;2af9	08
	pop af                  ;2afa	f1
	ld ($5b52),hl           ;2afb	22 52 5b
	ld hl,($5b6a)           ;2afe	2a 6a 5b
	ld ($5b6a),sp           ;2b01	ed 73 6a 5b
	ld sp,hl                ;2b05	f9
	ld hl,($5b52)           ;2b06	2a 52 5b
	push af                 ;2b09	f5
	ex af,af'               ;2b0a	08
	ret                     ;2b0b	c9

L2B0C:
	ex af,af'               ;2b0c	08
	pop af                  ;2b0d	f1
	ld ($5b52),hl           ;2b0e	22 52 5b
	ld hl,($5b6a)           ;2b11	2a 6a 5b
	ld ($5b6a),sp           ;2b14	ed 73 6a 5b
	ld sp,hl                ;2b18	f9
	ld hl,($5b52)           ;2b19	2a 52 5b
	push af                 ;2b1c	f5
	ex af,af'               ;2b1d	08
	nextreg $8e,$79         ;2b1e	ed 91 8e 79
	ret                     ;2b22	c9

L2B23:
	call L2AF9              ;2b23	cd f9 2a

L2B26:
	nextreg $8e,$09         ;2b26	ed 91 8e 09
	ld a,(hl)               ;2b2a	7e
	nextreg $8e,$79         ;2b2b	ed 91 8e 79
	ld (de),a               ;2b2f	12
	inc hl                  ;2b30	23
	inc de                  ;2b31	13
	dec bc                  ;2b32	0b
	ld a,b                  ;2b33	78
	or c                    ;2b34	b1
	jr nz,L2B26             ;2b35	20 ef
	nextreg $8e,$09         ;2b37	ed 91 8e 09
	call L2AF9              ;2b3b	cd f9 2a
	ret                     ;2b3e	c9
	call L2AF9              ;2b3f	cd f9 2a

L2B42:
	nextreg $8e,$79         ;2b42	ed 91 8e 79
	ld a,(hl)               ;2b46	7e
	nextreg $8e,$09         ;2b47	ed 91 8e 09
	ld (de),a               ;2b4b	12
	inc hl                  ;2b4c	23
	inc de                  ;2b4d	13
	dec bc                  ;2b4e	0b
	ld a,b                  ;2b4f	78
	or c                    ;2b50	b1
	jr nz,L2B42             ;2b51	20 ef
	call L2AF9              ;2b53	cd f9 2a
	ret                     ;2b56	c9

L2B57:
	ld a,$7f                ;2b57	3e 7f
	in a,($fe)              ;2b59	db fe
	rra                     ;2b5b	1f
	ret c                   ;2b5c	d8
	ld a,$fe                ;2b5d	3e fe
	in a,($fe)              ;2b5f	db fe
	rra                     ;2b61	1f
	ret c                   ;2b62	d8
	call L3E75              ;2b63	cd 75 3e
	ret nz                  ;2b66	c0
	pop hl                  ;2b67	e1
	ld bc,$0944             ;2b68	01 44 09
	sbc hl,bc               ;2b6b	ed 42
	jr nz,L2B71             ;2b6d	20 02
	rst $08                 ;2b6f	cf
	inc d                   ;2b70	14

L2B71:
	rst $08                 ;2b71	cf
	inc c                   ;2b72	0c
	call L3792              ;2b73	cd 92 37
	ld hl,$3f99             ;2b76	21 99 3f
	call L3792              ;2b79	cd 92 37
	ld hl,$5c3b             ;2b7c	21 3b 5c
	res 5,(hl)              ;2b7f	cb ae

L2B81:
	bit 5,(hl)              ;2b81	cb 6e
	jr z,L2B81              ;2b83	28 fc
	res 5,(hl)              ;2b85	cb ae
	ld a,($5c08)            ;2b87	3a 08 5c
	and $df                 ;2b8a	e6 df
	cp $59                  ;2b8c	fe 59
	jr z,L2B95              ;2b8e	28 05
	cp $4e                  ;2b90	fe 4e
	jr nz,L2B81             ;2b92	20 ed
	and a                   ;2b94	a7

L2B95:
	push af                 ;2b95	f5
	call L3E80              ;2b96	cd 80 3e
	ld d,h                  ;2b99	54
	add hl,bc               ;2b9a	09
	pop af                  ;2b9b	f1
	ret                     ;2b9c	c9
	rst $18                 ;2b9d	df
	ld c,$02                ;2b9e	0e 02
	call L2BA8              ;2ba0	cd a8 2b
	jp nc,L0DFF             ;2ba3	d2 ff 0d
	rst $30                 ;2ba6	f7
	ret                     ;2ba7	c9
L2BA8:
	defb 0xc5,0xaf,0xc7,0xd2,0x01,0xc1,0xd0,0xc5 ;2ba8	c5 af c7 d2 01 c1 d0 c5	........
	defb 0x21,0x51,0x2c,0xcd,0x92,0x37,0xcd,0x80 ;2bb0	21 51 2c cd 92 37 cd 80	!Q,..7..
	defb 0x3e,0x6d,0x0c,0xe6,0xdf,0xfe,0x59,0x20 ;2bb8	3e 6d 0c e6 df fe 59 20	>m....Y 
	defb 0xf5,0xcd,0x80,0x3e,0x54,0x09,0xc1,0x79 ;2bc0	f5 cd 80 3e 54 09 c1 79	...>T..y
	defb 0xfe,0x02,0x20,0x05,0xcd,0x9a,0x13,0x3e ;2bc8	fe 02 20 05 cd 9a 13 3e	.. ....>
	defb 0x02,0xf5,0xc7,0x04,0x1b,0xf1,0xcd,0xdf ;2bd0	02 f5 c7 04 1b f1 cd df	........
	defb 0x26,0x7b,0x2b,0xf5,0xfe,0x02,0x30,0x05 ;2bd8	26 7b 2b f5 fe 02 30 05	&{+...0.
	defb 0xcd,0xc5,0x26,0xf1,0x29,0xf1,0xfe,0x03 ;2be0	cd c5 26 f1 29 f1 fe 03	..&.)...
	defb 0x37,0xc8,0xfe                          ;2be8	37 c8 fe	7..
	ld (bc),a               ;2beb	02
	jr nc,L2C18             ;2bec	30 2a
	call L3E80              ;2bee	cd 80 3e
	ld l,b                  ;2bf1	68
	add hl,bc               ;2bf2	09
	rst $30                 ;2bf3	f7
	nextreg $07,$00         ;2bf4	ed 91 07 00
	ld hl,$2056             ;2bf8	21 56 20
	rst $00                 ;2bfb	c7
	add d                   ;2bfc	82
	rrca                    ;2bfd	0f
	ld hl,$2072             ;2bfe	21 72 20
	rst $00                 ;2c01	c7
	add d                   ;2c02	82
	rrca                    ;2c03	0f
	rst $18                 ;2c04	df
	call L3E80              ;2c05	cd 80 3e
	adc (hl)                ;2c08	8e
	ld a,(bc)               ;2c09	0a
	rst $28                 ;2c0a	ef
	ld l,e                  ;2c0b	6b
	dec c                   ;2c0c	0d
	call L26C5              ;2c0d	cd c5 26
	ld (de),a               ;2c10	12
	ld hl,($a421)           ;2c11	2a 21 a4
	ld hl,($cecd)           ;2c14	2a cd ce
	ld (hl),$21             ;2c17	36 21
	jp pe,$cd29             ;2c19	ea 29 cd
	adc $36                 ;2c1c	ce 36
	ld bc,$1041             ;2c1e	01 41 10

L2C21:
	ld a,c                  ;2c21	79
	sub $41                 ;2c22	d6 41
	rst $00                 ;2c24	c7
	call nz,L3803           ;2c25	c4 03 38
	ld e,$3e                ;2c28	1e 3e
	inc d                   ;2c2a	14
	call L36E1              ;2c2b	cd e1 36
	ld a,($5b79)            ;2c2e	3a 79 5b
	cp c                    ;2c31	b9
	ld a,$00                ;2c32	3e 00
	jr nz,L2C37             ;2c34	20 01
	inc a                   ;2c36	3c

L2C37:
	call L36E1              ;2c37	cd e1 36
	ld a,c                  ;2c3a	79
	call L36E1              ;2c3b	cd e1 36
	ld a,$14                ;2c3e	3e 14
	call L36E1              ;2c40	cd e1 36
	xor a                   ;2c43	af
	call L36E1              ;2c44	cd e1 36
	inc c                   ;2c47	0c
	djnz L2C21              ;2c48	10 d7
	ld a,$0d                ;2c4a	3e 0d
	call L36E1              ;2c4c	cd e1 36
	scf                     ;2c4f	37
	ret                     ;2c50	c9
Text2C51:
	defb 0x52,0x65,0x6d,0x6f,0x76,0x65,0x2f,0x69 ;2c51	52 65 6d 6f 76 65 2f 69	Remove/i
	defb 0x6e,0x73,0x65,0x72,0x74,0x20,0x53,0x44 ;2c59	6e 73 65 72 74 20 53 44	nsert SD
	defb 0x20,0x61,0x6e,0x64,0x20,0x70,0x72,0x65 ;2c61	20 61 6e 64 20 70 72 65	 and pre
	defb 0x73,0x73,0x20,0x59,0x0d,0x00           ;2c69	73 73 20 59 0d 00	ss Y..
	ld hl,$3140             ;2c6f	21 40 31
	call L3F93              ;2c72	cd 93 3f
	ld a,e                  ;2c75	7b
	cp $ff                  ;2c76	fe ff
	jr nz,L2C7E             ;2c78	20 04
	ld a,$63                ;2c7a	3e 63
	jr L2C7F                ;2c7c	18 01

L2C7E:
	dec a                   ;2c7e	3d

L2C7F:
	ld ($5c3a),a            ;2c7f	32 3a 5c
	ld hl,$3142             ;2c82	21 42 31
	call L3F93              ;2c85	cd 93 3f
	ld ($5c45),de           ;2c88	ed 53 45 5c
	ld hl,$3144             ;2c8c	21 44 31
	call L3F93              ;2c8f	cd 93 3f
	ld a,e                  ;2c92	7b
	ld ($5c47),a            ;2c93	32 47 5c
	ld hl,$3146             ;2c96	21 46 31
	call L3F93              ;2c99	cd 93 3f
	ld a,e                  ;2c9c	7b
	call L38AD              ;2c9d	cd ad 38
	call L0AA8              ;2ca0	cd a8 0a
	jp L0C2D                ;2ca3	c3 2d 0c

L2CA6:
	ld bc,($5b58)           ;2ca6	ed 4b 58 5b
	ld hl,($5b8c)           ;2caa	2a 8c 5b
	ld ($5b8c),bc           ;2cad	ed 43 8c 5b
	ld ($5b58),hl           ;2cb1	22 58 5b

L2CB4:
	ld bc,($5c5d)           ;2cb4	ed 4b 5d 5c
	ld hl,($5c5f)           ;2cb8	2a 5f 5c
	ld ($5c5f),bc           ;2cbb	ed 43 5f 5c
	jp L3ED0                ;2cbf	c3 d0 3e
Text2CC2:
	defb 0xe7,0xfe,0x8e,0x37,0x28,0x04,0xfd,0x34 ;2cc2	e7 fe 8e 37 28 04 fd 34	...7(..4
	defb 0x58,0xc9,0xfd,0x35,0x58,0xc0,0xa7,0xc9 ;2cca	58 c9 fd 35 58 c0 a7 c9	X..5X...
	defb 0xe7,0xfe,0x25,0x37,0xc0,0xe7,0xd5,0xcd ;2cd2	e7 fe 25 37 c0 e7 d5 cd	..%7....
	defb 0xc5,0x26,0xb7,0x27                     ;2cda	c5 26 b7 27	.&.'

L2CDE:
	ex de,hl                ;2cde	eb
	ld hl,($5c4d)           ;2cdf	2a 4d 5c
	and a                   ;2ce2	a7
	sbc hl,de               ;2ce3	ed 52
	pop de                  ;2ce5	d1
	ret z                   ;2ce6	c8
	scf                     ;2ce7	37
	ret                     ;2ce8	c9
	rst $20                 ;2ce9	e7
	cp $25                  ;2cea	fe 25
	scf                     ;2cec	37
	ret z                   ;2ced	c8
	push de                 ;2cee	d5
	call L0646              ;2cef	cd 46 06
	ld de,($5b8e)           ;2cf2	ed 5b 8e 5b
	rst $28                 ;2cf6	ef
	add (hl)                ;2cf7	86
	dec e                   ;2cf8	1d
	pop de                  ;2cf9	d1
	scf                     ;2cfa	37
	ret nz                  ;2cfb	c0
	and a                   ;2cfc	a7
	ret                     ;2cfd	c9

; tentative entry (word / jump table scan)
T2CFE:
	bit 2,(iy+$30)          ;2cfe	fd cb 30 56
	ld a,$1d                ;2d02	3e 1d
	ret z                   ;2d04	c8
	ld a,($5c4d)            ;2d05	3a 4d 5c
	sub $64                 ;2d08	d6 64
	srl a                   ;2d0a	cb 3f
	add $03                 ;2d0c	c6 03
	ret                     ;2d0e	c9
	call L26DF              ;2d0f	cd df 26
	ld (hl),e               ;2d12	73
	jr z,L2CDE              ;2d13	28 c9
	cp $2c                  ;2d15	fe 2c
	jr z,L2D2D              ;2d17	28 14
	call L0902              ;2d19	cd 02 09
	ld a,($5c7f)            ;2d1c	3a 7f 5c
	and $0f                 ;2d1f	e6 0f
	jr z,L2D29              ;2d21	28 06
	call L26DF              ;2d23	cd df 26
	inc e                   ;2d26	1c
	ld hl,($efc9)           ;2d27	2a c9 ef
	ld (hl),a               ;2d2a	77
	inc h                   ;2d2b	24
	ret                     ;2d2c	c9

L2D2D:
	call T0638              ;2d2d	cd 38 06
	call L0902              ;2d30	cd 02 09
	ld a,($5c7f)            ;2d33	3a 7f 5c
	and $0f                 ;2d36	e6 0f
	jr z,L2D40              ;2d38	28 06
	call L26DF              ;2d3a	cd df 26
	add $29                 ;2d3d	c6 29
	ret                     ;2d3f	c9

L2D40:
	rst $28                 ;2d40	ef
	sub h                   ;2d41	94
	inc hl                  ;2d42	23
	ret                     ;2d43	c9
	call L37F4              ;2d44	cd f4 37
	ld a,b                  ;2d47	78
	or c                    ;2d48	b1
	jr z,L2D4F              ;2d49	28 04
	rst $28                 ;2d4b	ef
	dec a                   ;2d4c	3d
	rra                     ;2d4d	1f
	ret                     ;2d4e	c9

L2D4F:
	call L3E80              ;2d4f	cd 80 3e
	ld l,l                  ;2d52	6d
	inc c                   ;2d53	0c
	ret                     ;2d54	c9
Data2D55:
	defb 0xcf,0x08                               ;2d55	cf 08	..

L2D57:
	ld c,(hl)               ;2d57	4e
	inc hl                  ;2d58	23
	ld b,(hl)               ;2d59	46
	inc hl                  ;2d5a	23
	ex (sp),hl              ;2d5b	e3
	push $5b3e              ;2d5c	ed 8a 5b 3e
	push bc                 ;2d60	c5
	ld bc,($5b54)           ;2d61	ed 4b 54 5b
	jp $5b48                ;2d65	c3 48 5b
	call L37FC              ;2d68	cd fc 37
	cp $04                  ;2d6b	fe 04
	jp nc,L142D             ;2d6d	d2 2d 14
	nextreg $07,a           ;2d70	ed 92 07
	ret                     ;2d73	c9
Text2D74:
	defb 0x3a,0x7f,0x5c,0xe6,0x0f,0x28,0x06,0xcd ;2d74	3a 7f 5c e6 0f 28 06 cd	:.\..(..
	defb 0xdf,0x26,0xbb,0x2a,0xc9,0xef,0x2d,0x23 ;2d7c	df 26 bb 2a c9 ef 2d 23	.&.*..-#
	defb 0xc9                                    ;2d84	c9	.

L2D85:
	ld a,($5c3b)            ;2d85	3a 3b 5c

L2D88:
	bit 2,(iy+$30)          ;2d88	fd cb 30 56
	jr z,L2D93              ;2d8c	28 05
	bit 6,a                 ;2d8e	cb 77
	jp z,L099D              ;2d90	ca 9d 09

L2D93:
	ld bc,$2aff             ;2d93	01 ff 2a
	jp L3EC1                ;2d96	c3 c1 3e
Zero2D99:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2d99	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2da1	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2da9	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2db1	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2db9	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;2dc1	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00                     ;2dc9	00 00 00 00	....
	nop                     ;2dcd	00
	nop                     ;2dce	00
	nop                     ;2dcf	00
	nop                     ;2dd0	00
	nop                     ;2dd1	00
	nop                     ;2dd2	00
	nop                     ;2dd3	00
	nop                     ;2dd4	00
	nop                     ;2dd5	00
	nop                     ;2dd6	00
	nop                     ;2dd7	00
	nop                     ;2dd8	00
	nop                     ;2dd9	00
	nop                     ;2dda	00
	nop                     ;2ddb	00
	nop                     ;2ddc	00
	nop                     ;2ddd	00
	nop                     ;2dde	00
	nop                     ;2ddf	00
	nop                     ;2de0	00

L2DE1:
	call L3EC9              ;2de1	cd c9 3e
	cp $3b                  ;2de4	fe 3b
	jr z,L2DFF              ;2de6	28 17
	cp $2c                  ;2de8	fe 2c
	jr nz,L2DF9             ;2dea	20 0d
	bit 7,(iy+$01)          ;2dec	fd cb 01 7e
	jr z,L2DFF              ;2df0	28 0d
	ld a,$06                ;2df2	3e 06
	rst $28                 ;2df4	ef
	djnz L2DF7              ;2df5	10 00

L2DF7:
	jr L2DFF                ;2df7	18 06

L2DF9:
	cp $27                  ;2df9	fe 27
	ret nz                  ;2dfb	c0
	rst $28                 ;2dfc	ef
	push af                 ;2dfd	f5
	rra                     ;2dfe	1f

L2DFF:
	rst $20                 ;2dff	e7
	call L08D9              ;2e00	cd d9 08
	jr nz,L2E06             ;2e03	20 01
	pop bc                  ;2e05	c1

L2E06:
	cp a                    ;2e06	bf
	ret                     ;2e07	c9
L2E08:
	defb 0xcd                                    ;2e08	cd	.
	ret                     ;2e09	c9
Data2E0A:
	defb 0x3e,0xfe,0xac,0x20                     ;2e0a	3e fe ac 20	>.. 
	dec c                   ;2e0e	0d
	call T063F              ;2e0f	cd 3f 06
	call T3EF9              ;2e12	cd f9 3e
	rst $28                 ;2e15	ef
	rlca                    ;2e16	07
	inc hl                  ;2e17	23
	ld a,$16                ;2e18	3e 16
	jr L2E57                ;2e1a	18 3b
Text2E1C:
	defb 0xfe,0xa9,0x20,0x28,0xe5,0xe7,0xe1,0x22 ;2e1c	fe a9 20 28 e5 e7 e1 22	.. (..."
	defb 0x5d,0x5c,0xfe,0x28,0x28,0x38,0xfe,0x23 ;2e24	5d 5c fe 28 28 38 fe 23	]\.((8.#
	defb 0x28,0x34,0xcd,0x3f,0x06,0xcd,0xf9,0x3e ;2e2c	28 34 cd 3f 06 cd f9 3e	(4.?...>
	defb 0xcd,0xfc,0x37,0xf5,0xcd,0xf4,0x37,0xf1 ;2e34	cd fc 37 f5 cd f4 37 f1	..7...7.
	defb 0xc5,0x47,0x0e,0x19,0xef,0x1f,0x20,0xc1 ;2e3c	c5 47 0e 19 ef 1f 20 c1	.G.... .
	defb 0xef,0x1f,0x20,0xc9,0xfe,0xad,0x20,0x0f ;2e44	ef 1f 20 c9 fe ad 20 0f	.. ... .
	defb 0xcd,0x38,0x06,0xcd,0xf9,0x3e,0xcd,0xf4 ;2e4c	cd 38 06 cd f9 3e cd f4	.8...>..
	defb 0x37,0x3e,0x17                          ;2e54	37 3e 17	7>.

L2E57:
	rst $28                 ;2e57	ef
	ld e,$20                ;2e58	1e 20
	ret                     ;2e5a	c9
Text2E5B:
	defb 0xcd,0x7a,0x2f,0xdc,0xa3,0x2e,0xd0,0xcd ;2e5b	cd 7a 2f dc a3 2e d0 cd	.z/.....
	defb 0x2d,0x0e,0xcd,0xf9,0x3e,0xfd,0xcb,0x01 ;2e63	2d 0e cd f9 3e fd cb 01	-...>...
	defb 0x76,0x28,0x04,0xef,0xe3,0x2d,0xc9,0xcd ;2e6b	76 28 04 ef e3 2d c9 cd	v(...-..
	defb 0x1b,0x38,0xdd,0x2a,0x51,0x5c,0xdd,0x7e ;2e73	1b 38 dd 2a 51 5c dd 7e	.8.*Q\.~
	defb 0x04,0xfe,0x53,0x20,0x14,0x3a,0x7f,0x5c ;2e7b	04 fe 53 20 14 3a 7f 5c	..S .:.\
	defb 0xe6,0x0f,0x28,0x15,0xc6,0xf2,0xdd,0x67 ;2e83	e6 0f 28 15 c6 f2 dd 67	..(....g
	defb 0xdd,0x2e,0x00,0xcd,0x80,0x3e,0xac,0x27 ;2e8b	dd 2e 00 cd 80 3e ac 27	.....>.'
	defb 0xc9,0xfe,0x4b,0x28,0xe8,0xfe,0x57,0x28 ;2e93	c9 fe 4b 28 e8 fe 57 28	..K(..W(
	defb 0xf2,0xef,0x3c,0x20,0xc9                ;2e9b	f2 ef 3c 20 c9	..< .

L2EA0:
	call L3EC9              ;2ea0	cd c9 3e
	cp $23                  ;2ea3	fe 23
	scf                     ;2ea5	37
	ret nz                  ;2ea6	c0
	call T0638              ;2ea7	cd 38 06
	and a                   ;2eaa	a7
	call T3EF9              ;2eab	cd f9 3e
	call L37F4              ;2eae	cd f4 37
	cp $10                  ;2eb1	fe 10
	jr nc,L2EBA             ;2eb3	30 05
	rst $28                 ;2eb5	ef
	ld bc,$a716             ;2eb6	01 16 a7
	ret                     ;2eb9	c9

L2EBA:
	rst $08                 ;2eba	cf
	rla                     ;2ebb	17
	ld a,$02                ;2ebc	3e 02

L2EBE:
	call L139A              ;2ebe	cd 9a 13
	call L2EA0              ;2ec1	cd a0 2e
	ret c                   ;2ec4	d8
	call L3EC9              ;2ec5	cd c9 3e
	cp $3b                  ;2ec8	fe 3b
	jr z,L2ECF              ;2eca	28 03
	cp $2c                  ;2ecc	fe 2c
	ret nz                  ;2ece	c0

L2ECF:
	rst $20                 ;2ecf	e7
	ret                     ;2ed0	c9
	ld a,$03                ;2ed1	3e 03
	jr L2ED7                ;2ed3	18 02
Data2ED5:
	defb 0x3e,0x02                               ;2ed5	3e 02	>.

L2ED7:
	call L139A              ;2ed7	cd 9a 13
	call L2EE1              ;2eda	cd e1 2e
	call L0902              ;2edd	cd 02 09
	ret                     ;2ee0	c9

L2EE1:
	call L3EC9              ;2ee1	cd c9 3e
	call L08D9              ;2ee4	cd d9 08
	jr z,L2EF6              ;2ee7	28 0d

L2EE9:
	call L2DE1              ;2ee9	cd e1 2d
	jr z,L2EE9              ;2eec	28 fb
	call L2E08              ;2eee	cd 08 2e
	call L2DE1              ;2ef1	cd e1 2d
	jr z,L2EE9              ;2ef4	28 f3

L2EF6:
	cp $29                  ;2ef6	fe 29
	ret z                   ;2ef8	c8
	rst $28                 ;2ef9	ef
	push af                 ;2efa	f5
	rra                     ;2efb	1f
	ret                     ;2efc	c9
	ld a,$01                ;2efd	3e 01
	call L139A              ;2eff	cd 9a 13
	call T0E19              ;2f02	cd 19 0e
	jr z,L2F0C              ;2f05	28 05
	call L3E80              ;2f07	cd 80 3e
	ld d,h                  ;2f0a	54
	add hl,bc               ;2f0b	09

L2F0C:
	call L2F1C              ;2f0c	cd 1c 2f
	call L0902              ;2f0f	cd 02 09
	ld a,($5c7f)            ;2f12	3a 7f 5c
	and $0f                 ;2f15	e6 0f
	ret nz                  ;2f17	c0
	rst $28                 ;2f18	ef
	and b                   ;2f19	a0
	jr nz,L2EE5             ;2f1a	20 c9

L2F1C:
	call L2DE1              ;2f1c	cd e1 2d
	jr z,L2F1C              ;2f1f	28 fb
	cp $28                  ;2f21	fe 28
	jr nz,L2F33             ;2f23	20 0e
	rst $20                 ;2f25	e7
	call L2EE1              ;2f26	cd e1 2e
	call L3EC9              ;2f29	cd c9 3e
	cp $29                  ;2f2c	fe 29
	jr nz,L2F78             ;2f2e	20 48
	rst $20                 ;2f30	e7
	jr L2F62                ;2f31	18 2f

L2F33:
	cp $ca                  ;2f33	fe ca
	jr nz,L2F47             ;2f35	20 10
	rst $20                 ;2f37	e7
	call L05A3              ;2f38	cd a3 05
	set 7,(iy+$37)          ;2f3b	fd cb 37 fe
	bit 6,(iy+$01)          ;2f3f	fd cb 01 76
	jr z,L2F57              ;2f43	28 12
	jr L2F78                ;2f45	18 31

L2F47:
	cp $25                  ;2f47	fe 25
	jr z,L2F50              ;2f49	28 05
	call L387F              ;2f4b	cd 7f 38
	jr nc,L2F5F             ;2f4e	30 0f

L2F50:
	call L05A3              ;2f50	cd a3 05
	res 7,(iy+$37)          ;2f53	fd cb 37 be

L2F57:
	call T0E19              ;2f57	cd 19 0e
	call nz,L2F8F           ;2f5a	c4 8f 2f
	jr L2F62                ;2f5d	18 03

L2F5F:
	call L2E08              ;2f5f	cd 08 2e

L2F62:
	call L2DE1              ;2f62	cd e1 2d
	jr z,L2F1C              ;2f65	28 b5
	ret                     ;2f67	c9

L2F68:
	rst $20                 ;2f68	e7

L2F69:
	call L2F7A              ;2f69	cd 7a 2f
	ret c                   ;2f6c	d8
	call L3EC9              ;2f6d	cd c9 3e
	cp $2c                  ;2f70	fe 2c
	jr z,L2F68              ;2f72	28 f4
	cp $3b                  ;2f74	fe 3b
	jr z,L2F68              ;2f76	28 f0

L2F78:
	rst $08                 ;2f78	cf
	dec bc                  ;2f79	0b

L2F7A:
	cp $d9                  ;2f7a	fe d9
	ret c                   ;2f7c	d8
	cp $df                  ;2f7d	fe df
	ccf                     ;2f7f	3f
	ret c                   ;2f80	d8
	push af                 ;2f81	f5
	rst $20                 ;2f82	e7
	pop af                  ;2f83	f1

L2F84:
	sub $c9                 ;2f84	d6 c9
	push af                 ;2f86	f5
	call L0639              ;2f87	cd 39 06
	pop af                  ;2f8a	f1
	rst $28                 ;2f8b	ef
	inc bc                  ;2f8c	03
	ld ($efc9),hl           ;2f8d	22 c9 ef
	cp a                    ;2f90	bf
	ld d,$3a                ;2f91	16 3a
	dec sp                  ;2f93	3b
	ld e,h                  ;2f94	5c
	ld hl,$5c71             ;2f95	21 71 5c
	res 6,(hl)              ;2f98	cb b6
	and $40                 ;2f9a	e6 40
	or (hl)                 ;2f9c	b6
	ld (hl),a               ;2f9d	77
	ld ix,($5c51)           ;2f9e	dd 2a 51 5c
	rst $28                 ;2fa2	ef
	sub $21                 ;2fa3	d6 21
	jr z,L2FFA              ;2fa5	28 53
	cp $57                  ;2fa7	fe 57
	jr z,L2FFF              ;2fa9	28 54

L2FAB:
	ld bc,$0001             ;2fab	01 01 00
	rst $28                 ;2fae	ef
	jr nc,L2FB1             ;2faf	30 00

L2FB1:
	res 3,(iy+$02)          ;2fb1	fd cb 02 9e
	res 5,(iy+$02)          ;2fb5	fd cb 02 ae
	rst $28                 ;2fb9	ef
	sbc $15                 ;2fba	de 15
	ld (hl),a               ;2fbc	77
	cp $0d                  ;2fbd	fe 0d
	jr nz,L2FAB             ;2fbf	20 ea
	call L2FCA              ;2fc1	cd ca 2f

L2FC4:
	ld a,($5c71)            ;2fc4	3a 71 5c
	jp L2D88                ;2fc7	c3 88 2d

L2FCA:
	ld hl,($5c63)           ;2fca	2a 63 5c
	ld de,($5c61)           ;2fcd	ed 5b 61 5c
	scf                     ;2fd1	37
	sbc hl,de               ;2fd2	ed 52
	ld b,h                  ;2fd4	44
	ld c,l                  ;2fd5	4d
	ld a,b                  ;2fd6	78
	or c                    ;2fd7	b1
	jr z,L2FF0              ;2fd8	28 16
	call L3837              ;2fda	cd 37 38
	bit 7,(iy+$37)          ;2fdd	fd cb 37 7e
	ret nz                  ;2fe1	c0
	ld b,$1d                ;2fe2	06 1d
	bit 6,(iy+$37)          ;2fe4	fd cb 37 76
	jr nz,L2FEC             ;2fe8	20 02
	ld b,$18                ;2fea	06 18

L2FEC:
	rst $28                 ;2fec	ef
	sbc $35                 ;2fed	de 35
	ret                     ;2fef	c9

L2FF0:
	bit 6,(iy+$37)          ;2ff0	fd cb 37 76
	jp nz,L3860             ;2ff4	c2 60 38
	jp L3837                ;2ff7	c3 37 38

L2FFA:
	ld bc,$e0bf             ;2ffa	01 bf e0
	jr L3012                ;2ffd	18 13

L2FFF:
	ld c,$c0                ;2fff	0e c0
	xor a                   ;3001	af
	ld b,(ix+$12)           ;3002	dd 46 12

; tentative entry (word / jump table scan)
T3005:
	add (ix+$1c)            ;3005	dd 86 1c
	jr c,L3010              ;3008	38 06
	cp c                    ;300a	b9
	jr nc,L3010             ;300b	30 03
	djnz T3005              ;300d	10 f6
	ld c,a                  ;300f	4f

L3010:
	ld b,$60                ;3010	06 60

L3012:
	ld e,$00                ;3012	1e 00

L3014:
	xor a                   ;3014	af
	ld hl,$e152             ;3015	21 52 e1
	push bc                 ;3018	c5
	call L3E80              ;3019	cd 80 3e
	daa                     ;301c	fd 27
	push de                 ;301e	d5
	ld c,e                  ;301f	4b
	ld b,$00                ;3020	06 00
	inc bc                  ;3022	03
	push bc                 ;3023	c5
	rst $28                 ;3024	ef
	jr nc,L3027             ;3025	30 00

L3027:
	pop bc                  ;3027	c1
	ld hl,$100b             ;3028	21 0b 10
	ld ($5b8a),hl           ;302b	22 8a 5b
	ld hl,$e152             ;302e	21 52 e1
	call L2AF9              ;3031	cd f9 2a
	call L3E80              ;3034	cd 80 3e
	ld b,a                  ;3037	47
	add hl,de               ;3038	19
	call L2AF9              ;3039	cd f9 2a
	ld hl,($5c5d)           ;303c	2a 5d 5c
	push hl                 ;303f	e5
	ld hl,($5c3d)           ;3040	2a 3d 5c
	push hl                 ;3043	e5
	ld hl,($5b6c)           ;3044	2a 6c 5b
	push hl                 ;3047	e5
	ld hl,$306b             ;3048	21 6b 30
	ld ($5b6c),hl           ;304b	22 6c 5b
	ld hl,$5b3a             ;304e	21 3a 5b
	push hl                 ;3051	e5
	ld ($5c3d),sp           ;3052	ed 73 3d 5c
	call L2FCA              ;3056	cd ca 2f
	pop hl                  ;3059	e1
	pop hl                  ;305a	e1
	ld ($5b6c),hl           ;305b	22 6c 5b
	pop hl                  ;305e	e1
	ld ($5c3d),hl           ;305f	22 3d 5c
	pop hl                  ;3062	e1
	ld ($5c5d),hl           ;3063	22 5d 5c
	pop de                  ;3066	d1
	pop bc                  ;3067	c1
	jp L2FC4                ;3068	c3 c4 2f
	call L3E80              ;306b	cd 80 3e
	jr L30AE                ;306e	18 3e
	rst $28                 ;3070	ef
	cp a                    ;3071	bf
	ld d,$e1                ;3072	16 e1
	ld ($5b6c),hl           ;3074	22 6c 5b
	pop hl                  ;3077	e1
	ld ($5c3d),hl           ;3078	22 3d 5c
	pop hl                  ;307b	e1
	ld ($5c5d),hl           ;307c	22 5d 5c
	ld (iy+$00),$ff         ;307f	fd 36 00 ff
	pop de                  ;3083	d1
	ld d,e                  ;3084	53
	pop bc                  ;3085	c1
	ld b,$74                ;3086	06 74
	jr L3014                ;3088	18 8a
	call L37F4              ;308a	cd f4 37
	ld hl,$0542             ;308d	21 42 05
	and a                   ;3090	a7
	sbc hl,bc               ;3091	ed 42
	jp nz,L334A             ;3093	c2 4a 33
	ld a,($5b5e)            ;3096	3a 5e 5b
	and $10                 ;3099	e6 10
	add a                   ;309b	87
	add a                   ;309c	87
	add a                   ;309d	87
	ld de,$0000             ;309e	11 00 00
	ld h,d                  ;30a1	62
	ld l,a                  ;30a2	6f
	push hl                 ;30a3	e5
	rst $18                 ;30a4	df
	rst $00                 ;30a5	c7
	ccf                     ;30a6	3f
	ld bc,$e1f7             ;30a7	01 f7 e1
	jp nc,L0DFB             ;30aa	d2 fb 0d
	ld a,($5b68)            ;30ad	3a 68 5b
	or $80                  ;30b0	f6 80
	xor l                   ;30b2	ad
	ld ($5b68),a            ;30b3	32 68 5b
	ret                     ;30b6	c9

L30B7:
	rst $30                 ;30b7	f7

L30B8:
	rst $08                 ;30b8	cf
	ld a,(bc)               ;30b9	0a
	ld hl,$5b66             ;30ba	21 66 5b
	res 7,(hl)              ;30bd	cb be
	res 6,(hl)              ;30bf	cb b6
	call T08DC              ;30c1	cd dc 08
	jr z,L30E5              ;30c4	28 1f
	set 7,(hl)              ;30c6	cb fe
	call L0639              ;30c8	cd 39 06
	cp $2c                  ;30cb	fe 2c
	jr nz,L30E5             ;30cd	20 16
	ld hl,$5b66             ;30cf	21 66 5b
	res 7,(hl)              ;30d2	cb be
	set 6,(hl)              ;30d4	cb f6
	call T0638              ;30d6	cd 38 06
	cp $2c                  ;30d9	fe 2c
	jr nz,L30E5             ;30db	20 08
	ld hl,$5b66             ;30dd	21 66 5b
	set 7,(hl)              ;30e0	cb fe
	call T0638              ;30e2	cd 38 06

L30E5:
	call L0902              ;30e5	cd 02 09
	xor a                   ;30e8	af
	ld hl,$5b66             ;30e9	21 66 5b
	bit 7,(hl)              ;30ec	cb 7e
	jr z,L30F3              ;30ee	28 03
	call L37FC              ;30f0	cd fc 37

L30F3:
	push af                 ;30f3	f5
	ld hl,$5b66             ;30f4	21 66 5b
	bit 6,(hl)              ;30f7	cb 76
	ld bc,$4000             ;30f9	01 00 40
	ld h,c                  ;30fc	61
	ld l,c                  ;30fd	69
	jr z,L310A              ;30fe	28 0a
	call L37F4              ;3100	cd f4 37
	push bc                 ;3103	c5
	call L37F4              ;3104	cd f4 37
	ld h,b                  ;3107	60
	ld l,c                  ;3108	69
	pop bc                  ;3109	c1

L310A:
	push hl                 ;310a	e5
	push bc                 ;310b	c5
	call L37FC              ;310c	cd fc 37
	pop bc                  ;310f	c1
	pop hl                  ;3110	e1
	pop de                  ;3111	d1
	call L32BF              ;3112	cd bf 32
	dec bc                  ;3115	0b
	push hl                 ;3116	e5
	add hl,bc               ;3117	09
	pop hl                  ;3118	e1
	jr c,L30B7              ;3119	38 9c
	ld (hl),d               ;311b	72
	ld d,h                  ;311c	54
	ld e,l                  ;311d	5d
	inc de                  ;311e	13
	ld a,b                  ;311f	78
	or c                    ;3120	b1
	jr z,L3125              ;3121	28 02
	ldir                    ;3123	ed b0

L3125:
	rst $30                 ;3125	f7
	ret                     ;3126	c9
Data3127:
	defb 0xfe                                    ;3127	fe	.
	call z,L2728            ;3128	cc 28 27
	call L0640              ;312b	cd 40 06
	cp $cc                  ;312e	fe cc
	jr nz,L31A1             ;3130	20 6f
	call T063F              ;3132	cd 3f 06
	call L0902              ;3135	cd 02 09
	call L37F4              ;3138	cd f4 37
	push bc                 ;313b	c5
	call L37FC              ;313c	cd fc 37
	push af                 ;313f	f5
	call L37F4              ;3140	cd f4 37
	push bc                 ;3143	c5
	call L37F4              ;3144	cd f4 37
	push bc                 ;3147	c5
	call L37FC              ;3148	cd fc 37
	pop hl                  ;314b	e1
	pop bc                  ;314c	c1
	pop ix                  ;314d	dd e1
	pop de                  ;314f	d1
	jr L3169                ;3150	18 17
	call T0638              ;3152	cd 38 06
	call L0902              ;3155	cd 02 09
	call L37FC              ;3158	cd fc 37
	push af                 ;315b	f5
	call L37FC              ;315c	cd fc 37
	pop ix                  ;315f	dd e1
	ld hl,$0000             ;3161	21 00 00
	ld d,h                  ;3164	54
	ld e,l                  ;3165	5d
	ld bc,$4000             ;3166	01 00 40

L3169:
	ld ixl,a                ;3169	dd 6f
	call L32BF              ;316b	cd bf 32
	rst $30                 ;316e	f7
	ex de,hl                ;316f	eb
	ld a,ixh                ;3170	dd 7c
	call L32BF              ;3172	cd bf 32
	dec bc                  ;3175	0b
	push hl                 ;3176	e5
	add hl,bc               ;3177	09
	pop hl                  ;3178	e1
	jp c,L30B7              ;3179	da b7 30
	ex de,hl                ;317c	eb
	push hl                 ;317d	e5
	add hl,bc               ;317e	09
	pop hl                  ;317f	e1
	jp c,L30B7              ;3180	da b7 30
	inc bc                  ;3183	03
	ld a,ixl                ;3184	dd 7d
	call L32FB              ;3186	cd fb 32
	res 6,h                 ;3189	cb b4
	ldir                    ;318b	ed b0
	ld a,$02                ;318d	3e 02
	call L32FB              ;318f	cd fb 32
	rst $30                 ;3192	f7
	ret                     ;3193	c9
Text3194:
	defb 0xfe,0x2c,0x28,0x1e,0xcd,0x41,0x32,0xc5 ;3194	fe 2c 28 1e cd 41 32 c5	.,(..A2.
	defb 0xcd,0x40,0x06,0xfe,0x2c                ;319c	cd 40 06 fe 2c	.@..,

L31A1:
	jp nz,L099D             ;31a1	c2 9d 09
	call T063F              ;31a4	cd 3f 06
	pop af                  ;31a7	f1
	call L0902              ;31a8	cd 02 09
	ld (iy+$58),a           ;31ab	fd 77 58
	call L3201              ;31ae	cd 01 32
	call L37F4              ;31b1	cd f4 37
	jr L31D8                ;31b4	18 22
Data31B6:
	defb 0xcd,0x38,0x06,0xfe,0x2c,0x20,0xe4,0xcd ;31b6	cd 38 06 fe 2c 20 e4 cd	.8.., ..
	defb 0x3f,0x06,0xcd,0x41,0x32,0xc5,0xcd,0x39 ;31be	3f 06 cd 41 32 c5 cd 39	?..A2..9
	defb 0x06,0xf1,0xcd,0x02,0x09,0xf6,0x80,0xfd ;31c6	06 f1 cd 02 09 f6 80 fd	........
	defb 0x77,0x58,0xcd                          ;31ce	77 58 cd	wX.
	call p,$c537            ;31d1	f4 37 c5
	call L3201              ;31d4	cd 01 32
	pop bc                  ;31d7	c1

L31D8:
	push bc                 ;31d8	c5
	call L37FC              ;31d9	cd fc 37
	pop hl                  ;31dc	e1
	call L32BF              ;31dd	cd bf 32
	srl a                   ;31e0	cb 3f
	call L32FB              ;31e2	cd fb 32
	res 6,h                 ;31e5	cb b4
	ld ($5b9b),hl           ;31e7	22 9b 5b
	ld a,$14                ;31ea	3e 14
	call L12C4              ;31ec	cd c4 12
	ld ($5c93),a            ;31ef	32 93 5c
	ld ix,$5c92             ;31f2	dd 21 92 5c
	ld hl,$21a2             ;31f6	21 a2 21
	call L3E80              ;31f9	cd 80 3e
	call c,$c320            ;31fc	dc 20 c3
	adc l                   ;31ff	8d
	ld sp,$07ef             ;3200	31 ef 07
	inc hl                  ;3203	23
	ld ($5ca3),bc           ;3204	ed 43 a3 5c
	ld a,b                  ;3208	78
	and a                   ;3209	a7
	jr z,L323E              ;320a	28 32
	ld a,c                  ;320c	79
	and a                   ;320d	a7
	jr z,L323E              ;320e	28 2e
	rst $28                 ;3210	ef
	rlca                    ;3211	07
	inc hl                  ;3212	23
	ld ($5ca5),bc           ;3213	ed 43 a5 5c
	ld a,($5c7f)            ;3217	3a 7f 5c
	and $0f                 ;321a	e6 0f
	ld e,a                  ;321c	5f
	ld a,($5ca3)            ;321d	3a a3 5c
	add c                   ;3220	81
	jr c,L323E              ;3221	38 1b
	cp $21                  ;3223	fe 21
	jr nc,L323E             ;3225	30 17
	dec e                   ;3227	1d
	jr nz,L322E             ;3228	20 04
	cp $11                  ;322a	fe 11
	jr nc,L323E             ;322c	30 10

L322E:
	inc e                   ;322e	1c
	ld a,($5ca4)            ;322f	3a a4 5c
	add b                   ;3232	80
	jr c,L323E              ;3233	38 09
	cp $19                  ;3235	fe 19
	jr nc,L323E             ;3237	30 05
	dec e                   ;3239	1d
	ret nz                  ;323a	c0
	cp $0d                  ;323b	fe 0d
	ret c                   ;323d	d8

L323E:
	jp L30B8                ;323e	c3 b8 30
	cp $cc                  ;3241	fe cc
	jp nz,L099D             ;3243	c2 9d 09
	rst $20                 ;3246	e7
	ld b,$01                ;3247	06 01
	cp $26                  ;3249	fe 26
	jr z,L3260              ;324b	28 13
	ld b,$02                ;324d	06 02
	cp $7c                  ;324f	fe 7c
	jr z,L3260              ;3251	28 0d
	ld b,$03                ;3253	06 03
	cp $5e                  ;3255	fe 5e
	jr z,L3260              ;3257	28 07
	ld b,$00                ;3259	06 00
	cp $7e                  ;325b	fe 7e
	ret nz                  ;325d	c0
	ld b,$04                ;325e	06 04

L3260:
	rst $20                 ;3260	e7
	ret                     ;3261	c9
	rst $18                 ;3262	df
	ld a,($5b69)            ;3263	3a 69 5b

L3266:
	call L3328              ;3266	cd 28 33
	jr z,L327E              ;3269	28 13
	ld a,(de)               ;326b	1a
	and b                   ;326c	a0
	jr nz,L327E             ;326d	20 0f
	ld a,(de)               ;326f	1a
	or b                    ;3270	b0
	ld (de),a               ;3271	12
	ld a,(hl)               ;3272	7e
	or b                    ;3273	b0
	ld (hl),a               ;3274	77
	rst $30                 ;3275	f7
	ld b,$00                ;3276	06 00
	call T3840              ;3278	cd 40 38
	jp L2D85                ;327b	c3 85 2d

L327E:
	dec c                   ;327e	0d
	ld a,$08                ;327f	3e 08
	cp c                    ;3281	b9
	ld a,c                  ;3282	79
	jr c,L3266              ;3283	38 e1
	jr L32E3                ;3285	18 5c
	call L37FC              ;3287	cd fc 37
	cp $09                  ;328a	fe 09
	jp c,L334A              ;328c	da 4a 33
	ld bc,$243b             ;328f	01 3b 24
	ld d,$12                ;3292	16 12
	out (c),d               ;3294	ed 51
	inc b                   ;3296	04
	in e,(c)                ;3297	ed 58
	dec b                   ;3299	05
	inc d                   ;329a	14
	out (c),d               ;329b	ed 51
	inc b                   ;329d	04
	in d,(c)                ;329e	ed 50
	ld b,$03                ;32a0	06 03

L32A2:
	cp d                    ;32a2	ba
	jp z,T32BD              ;32a3	ca bd 32
	cp e                    ;32a6	bb
	jr z,T32BD              ;32a7	28 14
	inc d                   ;32a9	14
	inc e                   ;32aa	1c
	djnz L32A2              ;32ab	10 f5
	rst $18                 ;32ad	df
	call L3328              ;32ae	cd 28 33
	jr nz,L32BB             ;32b1	20 08
	ld a,b                  ;32b3	78
	cpl                     ;32b4	2f
	ld b,a                  ;32b5	47
	and (hl)                ;32b6	a6
	ld (hl),a               ;32b7	77
	ld a,(de)               ;32b8	1a
	and b                   ;32b9	a0
	ld (de),a               ;32ba	12

L32BB:
	rst $30                 ;32bb	f7
	ret                     ;32bc	c9

; tentative entry (word / jump table scan)
T32BD:
	rst $08                 ;32bd	cf
	inc a                   ;32be	3c

L32BF:
	ex af,af'               ;32bf	08
	ld a,h                  ;32c0	7c
	and $c0                 ;32c1	e6 c0
	jr z,L32C7              ;32c3	28 02
	rst $08                 ;32c5	cf
	ld a,(bc)               ;32c6	0a

L32C7:
	ex af,af'               ;32c7	08
	set 7,h                 ;32c8	cb fc
	set 6,h                 ;32ca	cb f4

L32CC:
	ex (sp),hl              ;32cc	e3
	exx                     ;32cd	d9
	pop hl                  ;32ce	e1
	ld ($5b52),hl           ;32cf	22 52 5b
	ld hl,($5b6a)           ;32d2	2a 6a 5b
	ld ($5b6a),sp           ;32d5	ed 73 6a 5b
	ld sp,hl                ;32d9	f9
	call L3328              ;32da	cd 28 33
	jr z,L32EC              ;32dd	28 0d
	ld a,(de)               ;32df	1a
	and b                   ;32e0	a0
	jr z,L32E6              ;32e1	28 03

L32E3:
	rst $30                 ;32e3	f7
	rst $08                 ;32e4	cf
	inc bc                  ;32e5	03

L32E6:
	ld a,(hl)               ;32e6	7e
	or b                    ;32e7	b0
	ld (hl),a               ;32e8	77
	ld a,(de)               ;32e9	1a
	or b                    ;32ea	b0
	ld (de),a               ;32eb	12

L32EC:
	ld a,c                  ;32ec	79
	exx                     ;32ed	d9
	push hl                 ;32ee	e5
	ld hl,($5b52)           ;32ef	2a 52 5b

L32F2:
	add a                   ;32f2	87
	nextreg $56,a           ;32f3	ed 92 56
	inc a                   ;32f6	3c
	nextreg $57,a           ;32f7	ed 92 57
	ret                     ;32fa	c9

L32FB:
	add a                   ;32fb	87
	nextreg $54,a           ;32fc	ed 92 54
	inc a                   ;32ff	3c
	nextreg $55,a           ;3300	ed 92 55
	ret                     ;3303	c9
Text3304:
	defb 0x08,0x7c,0xe6,0xc0,0x20,0xbb,0x08,0xe5 ;3304	08 7c e6 c0 20 bb 08 e5	.|.. ...
	defb 0x2a,0x6a,0x5b,0xed,0x73,0x6a,0x5b,0xf9 ;330c	2a 6a 5b ed 73 6a 5b f9	*j[.sj[.
	defb 0x21,0x1a,0x33,0xd9,0x18,0xc0,0xed,0x91 ;3314	21 1a 33 d9 18 c0 ed 91	!.3.....
	defb 0x8e,0x09,0x2a,0x6a,0x5b,0xed,0x73,0x6a ;331c	8e 09 2a 6a 5b ed 73 6a	..*j[.sj
	defb 0x5b,0xf9,0xe1,0xc9                     ;3324	5b f9 e1 c9	[...

L3328:
	ld hl,($5b68)           ;3328	2a 68 5b
	cp $08                  ;332b	fe 08
	jr z,L3349              ;332d	28 1a
	jr nc,L334C             ;332f	30 1b
	cp $07                  ;3331	fe 07
	jr z,L3349              ;3333	28 14
	cp $01                  ;3335	fe 01
	jr z,L3345              ;3337	28 0c
	cp $03                  ;3339	fe 03
	jr z,L3345              ;333b	28 08
	cp $04                  ;333d	fe 04
	jr z,L3345              ;333f	28 04
	cp $06                  ;3341	fe 06
	jr nz,L3350             ;3343	20 0b

L3345:
	bit 7,l                 ;3345	cb 7d
	jr nz,L3350             ;3347	20 07

L3349:
	rst $30                 ;3349	f7

L334A:
	rst $08                 ;334a	cf
	add hl,bc               ;334b	09

L334C:
	inc h                   ;334c	24
	cp h                    ;334d	bc
	jr nc,L32E3             ;334e	30 93

L3350:
	ex af,af'               ;3350	08
	ld a,$08                ;3351	3e 08
	call L32F2              ;3353	cd f2 32
	ex af,af'               ;3356	08
	ld c,a                  ;3357	4f
	rrca                    ;3358	0f
	rrca                    ;3359	0f
	and $1f                 ;335a	e6 1f
	ld d,$c0                ;335c	16 c0
	ld e,a                  ;335e	5f
	ld h,d                  ;335f	62
	add $28                 ;3360	c6 28
	ld l,a                  ;3362	6f
	ld a,c                  ;3363	79
	and $03                 ;3364	e6 03
	inc a                   ;3366	3c
	ld b,a                  ;3367	47
	ld a,$c0                ;3368	3e c0

L336A:
	rlca                    ;336a	07
	rlca                    ;336b	07
	djnz L336A              ;336c	10 fc
	ld b,a                  ;336e	47
	and (hl)                ;336f	a6
	cp b                    ;3370	b8
	ret                     ;3371	c9
	ld hl,$5b68             ;3372	21 68 5b
	set 5,(hl)              ;3375	cb ee
	jr L337E                ;3377	18 05
	ld hl,$5b68             ;3379	21 68 5b
	res 5,(hl)              ;337c	cb ae

L337E:
	call T0E19              ;337e	cd 19 0e
	jr z,L339F              ;3381	28 1c
	call L37F4              ;3383	cd f4 37
	push bc                 ;3386	c5
	call L37FC              ;3387	cd fc 37
	pop hl                  ;338a	e1
	push af                 ;338b	f5
	call L32BF              ;338c	cd bf 32
	rst $30                 ;338f	f7
	push hl                 ;3390	e5
	jr L33A9                ;3391	18 16
	ld hl,$5b68             ;3393	21 68 5b
	set 5,(hl)              ;3396	cb ee
	jr L339F                ;3398	18 05
	ld hl,$5b68             ;339a	21 68 5b
	res 5,(hl)              ;339d	cb ae

L339F:
	ld a,$ff                ;339f	3e ff
	push af                 ;33a1	f5
	call T0E19              ;33a2	cd 19 0e
	call nz,L37F4           ;33a5	c4 f4 37
	push bc                 ;33a8	c5

L33A9:
	set 7,(iy+$0a)          ;33a9	fd cb 0a fe

L33AD:
	call L0E2D              ;33ad	cd 2d 0e
	bit 6,(iy+$01)          ;33b0	fd cb 01 76
	jr z,L3402              ;33b4	28 4c
	cp $7e                  ;33b6	fe 7e
	ld a,($5b68)            ;33b8	3a 68 5b
	jr nz,L33C2             ;33bb	20 05
	push af                 ;33bd	f5
	rst $20                 ;33be	e7
	pop af                  ;33bf	f1
	xor $20                 ;33c0	ee 20

L33C2:
	call T0E19              ;33c2	cd 19 0e
	jr z,L33DE              ;33c5	28 17
	and $20                 ;33c7	e6 20
	jr z,L33D7              ;33c9	28 0c
	call L3804              ;33cb	cd 04 38
	call L3435              ;33ce	cd 35 34
	ld c,b                  ;33d1	48
	call L3435              ;33d2	cd 35 34
	jr L33DE                ;33d5	18 07

L33D7:
	call L3812              ;33d7	cd 12 38
	ld c,a                  ;33da	4f
	call L3435              ;33db	cd 35 34

L33DE:
	call L3EC9              ;33de	cd c9 3e
	cp $2c                  ;33e1	fe 2c
	jr nz,L33E8             ;33e3	20 03
	rst $20                 ;33e5	e7
	jr L33AD                ;33e6	18 c5

L33E8:
	pop hl                  ;33e8	e1
	pop af                  ;33e9	f1
	call L0902              ;33ea	cd 02 09
	bit 7,(iy+$0a)          ;33ed	fd cb 0a 7e
	ret nz                  ;33f1	c0
	ld a,($5b77)            ;33f2	3a 77 5b
	ld bc,($5c42)           ;33f5	ed 4b 42 5c
	call L2147              ;33f9	cd 47 21
	ld a,($5c44)            ;33fc	3a 44 5c
	jp L364A                ;33ff	c3 4a 36

L3402:
	ld d,a                  ;3402	57
	cp $7e                  ;3403	fe 7e
	call z,L3ECF            ;3405	cc cf 3e
	call T0E19              ;3408	cd 19 0e
	jr z,L33DE              ;340b	28 d1
	push de                 ;340d	d5
	call L381B              ;340e	cd 1b 38
	pop hl                  ;3411	e1
	ld a,b                  ;3412	78
	or c                    ;3413	b1
	jr z,L33DE              ;3414	28 c8

L3416:
	ld a,(de)               ;3416	1a
	inc de                  ;3417	13
	exx                     ;3418	d9
	ld c,a                  ;3419	4f
	call L3435              ;341a	cd 35 34
	exx                     ;341d	d9
	dec bc                  ;341e	0b
	ld a,b                  ;341f	78
	or c                    ;3420	b1
	jr nz,L3416             ;3421	20 f3
	ld a,h                  ;3423	7c
	cp $7e                  ;3424	fe 7e
	jr nz,L33DE             ;3426	20 b6
	dec de                  ;3428	1b
	ld a,(de)               ;3429	1a
	set 7,a                 ;342a	cb ff
	pop hl                  ;342c	e1
	dec hl                  ;342d	2b
	push hl                 ;342e	e5
	ld c,a                  ;342f	4f
	call L3435              ;3430	cd 35 34
	jr L33DE                ;3433	18 a9

L3435:
	pop ix                  ;3435	dd e1
	pop hl                  ;3437	e1
	pop af                  ;3438	f1
	push af                 ;3439	f5
	inc a                   ;343a	3c
	jr nz,L3442             ;343b	20 05
	ld (hl),c               ;343d	71

L343E:
	inc hl                  ;343e	23
	push hl                 ;343f	e5
	jp (ix)                 ;3440	dd e9

L3442:
	dec a                   ;3442	3d
	call L2AF9              ;3443	cd f9 2a
	call L32F2              ;3446	cd f2 32
	ld (hl),c               ;3449	71
	rst $30                 ;344a	f7
	jr L343E                ;344b	18 f1

L344D:
	di                      ;344d	f3
	nextreg $07,$03         ;344e	ed 91 07 03
	ld b,$01                ;3452	06 01
	call L3E80              ;3454	cd 80 3e
	ei                      ;3457	fb
	ld bc,$c5cd             ;3458	01 cd c5
	ld h,$fd                ;345b	26 fd
	add hl,hl               ;345d	29
	ld c,$ff                ;345e	0e ff
	ld hl,($5c53)           ;3460	2a 53 5c
	dec hl                  ;3463	2b
	call L3694              ;3464	cd 94 36
	xor a                   ;3467	af
	ld ($5c75),a            ;3468	32 75 5c
	dec a                   ;346b	3d
	ld b,$01                ;346c	06 01
	call L20B8              ;346e	cd b8 20
	ld hl,$091f             ;3471	21 1f 09
	ex (sp),hl              ;3474	e3
	jr L347E                ;3475	18 07
	call L37F4              ;3477	cd f4 37
	ld a,b                  ;347a	78
	or c                    ;347b	b1
	jr nz,L3482             ;347c	20 04

L347E:
	ld bc,($5cb2)           ;347e	ed 4b b2 5c

L3482:
	call L3499              ;3482	cd 99 34
	pop de                  ;3485	d1
	pop hl                  ;3486	e1
	ld sp,($5cb2)           ;3487	ed 7b b2 5c
	ld b,$3e                ;348b	06 3e
	push bc                 ;348d	c5
	ld ($5b58),sp           ;348e	ed 73 58 5b
	push hl                 ;3492	e5
	ld ($5c3d),sp           ;3493	ed 73 3d 5c
	push de                 ;3497	d5
	ret                     ;3498	c9

L3499:
	push bc                 ;3499	c5
	ld de,($5c4b)           ;349a	ed 5b 4b 5c
	ld hl,($5c59)           ;349e	2a 59 5c
	dec hl                  ;34a1	2b
	rst $28                 ;34a2	ef
	push hl                 ;34a3	e5
	add hl,de               ;34a4	19
	call L26C5              ;34a5	cd c5 26
	inc b                   ;34a8	04
	ld hl,($a2cd)           ;34a9	2a cd a2
	ld (hl),$2a             ;34ac	36 2a
	ld h,l                  ;34ae	65
	ld e,h                  ;34af	5c
	ld de,$0032             ;34b0	11 32 00
	add hl,de               ;34b3	19
	pop de                  ;34b4	d1
	sbc hl,de               ;34b5	ed 52
	jr nc,L34C1             ;34b7	30 08
	ld hl,($5cb4)           ;34b9	2a b4 5c
	and a                   ;34bc	a7
	sbc hl,de               ;34bd	ed 52
	jr nc,L34C3             ;34bf	30 02

L34C1:
	rst $08                 ;34c1	cf
	dec d                   ;34c2	15

L34C3:
	ld ($5cb2),de           ;34c3	ed 53 b2 5c
	ret                     ;34c7	c9
Text34C8:
	defb 0xcd,0x19,0x0e,0x20,0x09,0x16,0x00,0xcd ;34c8	cd 19 0e 20 09 16 00 cd	... ....
	defb 0x08,0x20,0xcd,0x02,0x09,0xe7,0xcd,0xa3 ;34d0	08 20 cd 02 09 e7 cd a3	. ......
	defb 0x05,0x3a,0x3b,0x5c,0xf5,0xcd,0x03,0x35 ;34d8	05 3a 3b 5c f5 cd 03 35	.:;\...5
	defb 0xc1,0x30,0x0c,0x78,0xef,0x5e,0x1c,0xcd ;34e0	c1 30 0c 78 ef 5e 1c cd	.0.x.^..
	defb 0xc9,0x3e,0xfe,0x2c,0x28,0xe7,0xc9,0x20 ;34e8	c9 3e fe 2c 28 e7 c9 20	.>.,(.. 
	defb 0x0a,0x3a,0x3a,0x5c,0xfe,0x0b,0xc2,0xd6 ;34f0	0a 3a 3a 5c fe 0b c2 d6	.::\....
	defb 0x0d,0xcf,0x19,0xfd,0xcb,0x30,0x7e,0x20 ;34f8	0d cf 19 fd cb 30 7e 20	.....0~ 
	defb 0xf8,0xcf,0x0d,0x2a,0x5d,0x5c,0x22,0x5f ;3500	f8 cf 0d 2a 5d 5c 22 5f	...*]\"_
	defb 0x5c,0x2a,0x57,0x5c,0x3a,0x78,0x5b,0xfe ;3508	5c 2a 57 5c 3a 78 5b fe	\*W\:x[.
	defb 0xff,0xc4,0xbf,0x32,0x7e,0xfe,0x2c,0x28 ;3510	ff c4 bf 32 7e fe 2c 28	...2~.,(
	defb 0x31,0xfe,0x28,0x28,0x2d,0xfe,0x29,0xca ;3518	31 fe 28 28 2d fe 29 ca	1.((-.).
	defb 0xaa,0x35,0xeb,0x21,0x77,0x5b,0x4e,0xc5 ;3520	aa 35 eb 21 77 5b 4e c5	.5.!w[N.
	defb 0x36,0xff,0x2a,0x55,0x5c,0xe5,0xeb,0x11 ;3528	36 ff 2a 55 5c e5 eb 11	6.*U\...
	defb 0xe4,0x00,0xcd,0xac,0x19,0xc1,0xed,0x43 ;3530	e4 00 cd ac 19 c1 ed 43	.......C
	defb 0x55,0x5c,0xc1,0x79,0x32,0x77,0x5b,0x38 ;3538	55 5c c1 79 32 77 5b 38	U\.y2w[8
	defb 0x69,0x54,0xcb,0xbc,0xcb,0xb4,0x22,0x57 ;3540	69 54 cb bc cb b4 22 57	iT...."W
	defb 0x5c,0x62,0x3a,0x78,0x5b,0x3c,0x28,0x04 ;3548	5c 62 3a 78 5b 3c 28 04	\b:x[<(.
	defb 0xcd,0xc3,0x39,0xf7,0x22,0x5d,0x5c,0x2a ;3550	cd c3 39 f7 22 5d 5c 2a	..9."]\*
	defb 0x58,0x5b,0xe5,0xe7,0xfd,0xcb,0x30,0x7e ;3558	58 5b e5 e7 fd cb 30 7e	X[....0~
	defb 0x28,0x08,0x0e,0x22,0xcd,0xf8,0x3a,0x22 ;3560	28 08 0e 22 cd f8 3a 22	(.."..:"
	defb 0x58,0x5b,0x2a,0x6c,0x5b,0xe5,0x21,0xbe ;3568	58 5b 2a 6c 5b e5 21 be	X[*l[.!.
	defb 0x35,0x22,0x6c,0x5b,0x2a,0x3d,0x5c,0xe5 ;3570	35 22 6c 5b 2a 3d 5c e5	5"l[*=\.
	defb 0x21,0x3a,0x5b,0xe5,0xed,0x73,0x3d,0x5c ;3578	21 3a 5b e5 ed 73 3d 5c	!:[..s=\
	defb 0xcd,0x2d,0x0e,0x3a,0x78,0x5b,0x3c,0x28 ;3580	cd 2d 0e 3a 78 5b 3c 28	.-.:x[<(
	defb 0x09,0xed,0x34,0x4a,0xa3,0xed,0x5b,0x57 ;3588	09 ed 34 4a a3 ed 5b 57	..4J..[W
	defb 0x5c,0x19,0x22,0x57,0x5c,0xe1,0x37,0xe1 ;3590	5c 19 22 57 5c e1 37 e1	\."W\.7.
	defb 0x22,0x3d,0x5c,0xe1,0x22,0x6c,0x5b,0xe1 ;3598	22 3d 5c e1 22 6c 5b e1	"=\."l[.
	defb 0x22,0x58,0x5b,0x2a,0x5f,0x5c,0x22,0x5d ;35a0	22 58 5b 2a 5f 5c 22 5d	"X[*_\"]
	defb 0x5c,0xc9,0x21,0x78,0x5b,0x34,0xc4,0xf5 ;35a8	5c c9 21 78 5b 34 c4 f5	\.!x[4..
	defb 0x2a,0x3e,0xff,0x77,0x2a,0x59,0x5c,0x2b ;35b0	2a 3e ff 77 2a 59 5c 2b	*>.w*Y\+
	defb 0x22,0x57,0x5c,0xa7,0x18,0xe5,0xaf,0x18 ;35b8	22 57 5c a7 18 e5 af 18	"W\.....
	defb 0xd6,0xcd,0x19,0x0e,0x20,0x0b,0xcd,0x2d ;35c0	d6 cd 19 0e 20 0b cd 2d	.... ..-
	defb 0x0e,0xfe,0x2c,0xc4,0x02,0x09,0xe7,0x18 ;35c8	0e fe 2c c4 02 09 e7 18	..,.....
	defb 0xf5                                    ;35d0	f5	.

L35D1:
	ld de,$0100             ;35d1	11 00 01
	ld c,e                  ;35d4	4b
	ld hl,($5c5d)           ;35d5	2a 5d 5c
	call L128E              ;35d8	cd 8e 12
	ld ($5c5d),hl           ;35db	22 5d 5c
	ret                     ;35de	c9
	ld a,$03                ;35df	3e 03
	jr L35E5                ;35e1	18 02
Data35E3:
	defb 0x3e,0xfe                               ;35e3	3e fe	>.

L35E5:
	call L2EBE              ;35e5	cd be 2e
	ld bc,$01ff             ;35e8	01 ff 01
	call T2067              ;35eb	cd 67 20
	jr L35FF                ;35ee	18 0f
	ld a,$03                ;35f0	3e 03
	jr L35F6                ;35f2	18 02
Data35F4:
	defb 0x3e,0xfe                               ;35f4	3e fe	>.

L35F6:
	call L2EBE              ;35f6	cd be 2e
	ld bc,$01ff             ;35f9	01 ff 01
	call L20B5              ;35fc	cd b5 20

L35FF:
	ld bc,($5c45)           ;35ff	ed 4b 45 5c
	exx                     ;3603	d9
	call T0E19              ;3604	cd 19 0e
	call nz,L3A5B           ;3607	c4 5b 3a
	call L3EC9              ;360a	cd c9 3e
	cp $28                  ;360d	fe 28
	jr nz,L3618             ;360f	20 07
	rst $20                 ;3611	e7
	cp $29                  ;3612	fe 29
	jp nz,L099D             ;3614	c2 9d 09
	rst $20                 ;3617	e7

L3618:
	call L0902              ;3618	cd 02 09
	exx                     ;361b	d9
	ld a,($5b65)            ;361c	3a 65 5b
	cp $ff                  ;361f	fe ff
	jr nz,L3627             ;3621	20 04
	ld ($5c49),bc           ;3623	ed 43 49 5c

L3627:
	call L3942              ;3627	cd 42 39
	ret nc                  ;362a	d0
	call L0080              ;362b	cd 80 00
	ld l,h                  ;362e	6c
	ld a,(de)               ;362f	1a
	ret                     ;3630	c9
Text3631:
	defb 0xed,0x4b,0x6e,0x5c,0xcb,0x78,0xc2,0xc1 ;3631	ed 4b 6e 5c cb 78 c2 c1	.Kn\.x..
	defb 0x39,0xcd,0xbc,0x38,0x3e,0xff,0x28,0x03 ;3639	39 cd bc 38 3e ff 28 03	9..8>.(.
	defb 0x3a,0xba,0x5e,0xcd,0x47,0x21,0x3a,0x70 ;3641	3a ba 5e cd 47 21 3a 70	:.^.G!:p
	defb 0x5c                                    ;3649	5c	\

L364A:
	ld ($5c47),a            ;364a	32 47 5c
	ld d,a                  ;364d	57
	ld e,$00                ;364e	1e 00
	call L1285              ;3650	cd 85 12
	ld ($5c5d),hl           ;3653	22 5d 5c
	cp $3a                  ;3656	fe 3a
	call z,L3ECF            ;3658	cc cf 3e
	jp L2087                ;365b	c3 87 20
	ld b,$01                ;365e	06 01
	call L2066              ;3660	cd 66 20
	jr L366A                ;3663	18 05
Data3665:
	defb 0x06,0x01,0xcd,0xb4,0x20                ;3665	06 01 cd b4 20	.... 

L366A:
	ld hl,($5c5d)           ;366a	2a 5d 5c
	jr nc,L3673             ;366d	30 04
	add hl,$fffb            ;366f	ed 34 fb ff

L3673:
	ld a,($5b77)            ;3673	3a 77 5b
	ld ($5b65),a            ;3676	32 65 5b
	inc a                   ;3679	3c
	jr z,L3689              ;367a	28 0d
	add hl,$a24a            ;367c	ed 34 4a a2
	ld de,($5ebb)           ;3680	ed 5b bb 5e
	add hl,de               ;3684	19
	res 7,h                 ;3685	cb bc
	res 6,h                 ;3687	cb b4

L3689:
	ld ($5b9d),hl           ;3689	22 9d 5b
	ld c,$21                ;368c	0e 21
	call L3A66              ;368e	cd 66 3a
	ld hl,($5b9d)           ;3691	2a 9d 5b

L3694:
	ld a,($5b65)            ;3694	3a 65 5b
	ld ($5b78),a            ;3697	32 78 5b
	ld ($5c57),hl           ;369a	22 57 5c
	res 7,(iy+$30)          ;369d	fd cb 30 be
	ret                     ;36a1	c9

L36A2:
	ld a,($5c7f)            ;36a2	3a 7f 5c
	and $0f                 ;36a5	e6 0f
	jr z,L36CA              ;36a7	28 21
	add $f2                 ;36a9	c6 f2
	push ix                 ;36ab	dd e5
	ld ixh,a                ;36ad	dd 67
	ld ixl,$00              ;36af	dd 2e 00
	ld hl,$0000             ;36b2	21 00 00
	ld ($5b84),hl           ;36b5	22 84 5b
	ld ($5b86),hl           ;36b8	22 86 5b
	ld a,$fe                ;36bb	3e fe
	call L13AF              ;36bd	cd af 13
	ld a,$0e                ;36c0	3e 0e
	call L3E80              ;36c2	cd 80 3e
	ld a,a                  ;36c5	7f
	daa                     ;36c6	27
	pop ix                  ;36c7	dd e1
	ret                     ;36c9	c9

L36CA:
	rst $28                 ;36ca	ef
	ld l,e                  ;36cb	6b
	dec c                   ;36cc	0d
	ret                     ;36cd	c9

; tentative entry (word / jump table scan)
T36CE:
	ld a,(hl)               ;36ce	7e
	inc hl                  ;36cf	23
	cp $0d                  ;36d0	fe 0d
	ret c                   ;36d2	d8
	cp $ff                  ;36d3	fe ff
	ret z                   ;36d5	c8
	push af                 ;36d6	f5
	call L36DF              ;36d7	cd df 36
	pop af                  ;36da	f1
	add a                   ;36db	87
	jr nc,T36CE             ;36dc	30 f0
	ret                     ;36de	c9

L36DF:
	and $7f                 ;36df	e6 7f

L36E1:
	rst $30                 ;36e1	f7
	rst $28                 ;36e2	ef
	djnz L36E5              ;36e3	10 00

L36E5:
	rst $18                 ;36e5	df
	ret                     ;36e6	c9
Text36E7:
	defb 0xf7,0xcd,0xed,0x36,0x18,0xf8           ;36e7	f7 cd ed 36 18 f8	...6..

L36ED:
	push hl                 ;36ed	e5
	ld bc,$d8f0             ;36ee	01 f0 d8
	rst $28                 ;36f1	ef
	ld hl,($0119)           ;36f2	2a 19 01
	jr L36F3                ;36f5	18 fc
	rst $28                 ;36f7	ef
	ld hl,($1819)           ;36f8	2a 19 18
	ld bc,$01e5             ;36fb	01 e5 01
	sbc h                   ;36fe	9c
	rst $38                 ;36ff	ff
	rst $28                 ;3700	ef
	ld hl,($0119)           ;3701	2a 19 01
	or $ff                  ;3704	f6 ff
	rst $28                 ;3706	ef
	ld hl,($7d19)           ;3707	2a 19 7d
	rst $28                 ;370a	ef
	rst $28                 ;370b	ef
	dec d                   ;370c	15
	pop hl                  ;370d	e1
	ret                     ;370e	c9

L370F:
	ld l,a                  ;370f	6f
	ld a,$2d                ;3710	3e 2d
	jr L3716                ;3712	18 02

L3714:
	ld a,$3a                ;3714	3e 3a

L3716:
	ld h,$00                ;3716	26 00
	rst $28                 ;3718	ef
	djnz L371B              ;3719	10 00

L371B:
	push hl                 ;371b	e5
	ld e,$30                ;371c	1e 30
	jr L3703                ;371e	18 e3
L3720:
	defb 0x78,0xb1,0x28                          ;3720	78 b1 28	x.(
	inc a                   ;3723	3c
	push de                 ;3724	d5
	push bc                 ;3725	c5
	ld a,b                  ;3726	78
	srl a                   ;3727	cb 3f
	ld hl,$07bc             ;3729	21 bc 07
	add hl,a                ;372c	ed 31
	ld e,$ff                ;372e	1e ff
	call L36ED              ;3730	cd ed 36
	pop hl                  ;3733	e1
	ld a,l                  ;3734	7d
	push af                 ;3735	f5
	srl h                   ;3736	cb 3c
	rra                     ;3738	1f
	rrca                    ;3739	0f
	rrca                    ;373a	0f
	rrca                    ;373b	0f
	rrca                    ;373c	0f
	and $0f                 ;373d	e6 0f
	call L370F              ;373f	cd 0f 37
	pop af                  ;3742	f1

L3743:
	and $1f                 ;3743	e6 1f
	call L370F              ;3745	cd 0f 37
	pop hl                  ;3748	e1
	push hl                 ;3749	e5
	ld a,h                  ;374a	7c
	rrca                    ;374b	0f
	rrca                    ;374c	0f
	rrca                    ;374d	0f
	and $1f                 ;374e	e6 1f
	ld l,a                  ;3750	6f
	ld a,$20                ;3751	3e 20
	call L3716              ;3753	cd 16 37
	pop hl                  ;3756	e1
	add hl,hl               ;3757	29
	add hl,hl               ;3758	29
	add hl,hl               ;3759	29
	ld a,h                  ;375a	7c
	and $3f                 ;375b	e6 3f
	ld l,a                  ;375d	6f
	jr L3714                ;375e	18 b4
	ld b,$10                ;3760	06 10

L3762:
	ld a,$20                ;3762	3e 20
	rst $28                 ;3764	ef
	djnz L3767              ;3765	10 00

L3767:
	djnz L3762              ;3767	10 f9
	ret                     ;3769	c9
	ld hl,$3e98             ;376a	21 98 3e
	call L3788              ;376d	cd 88 37
	ld a,($f71f)            ;3770	3a 1f f7
	add $09                 ;3773	c6 09
	ld c,a                  ;3775	4f
	ld b,$08                ;3776	06 08
	rst $28                 ;3778	ef
	rra                     ;3779	1f
	jr nz,L3743             ;377a	20 c7
	call z,L3A01            ;377c	cc 01 3a
	ret po                  ;377f	e0
	sub $32                 ;3780	d6 32
	adc a                   ;3782	8f
	ld e,h                  ;3783	5c
	call c,L3720            ;3784	dc 20 37
	ret                     ;3787	c9

L3788:
	ld a,(hl)               ;3788	7e
	cp $ff                  ;3789	fe ff
	ret z                   ;378b	c8
	rst $28                 ;378c	ef
	djnz L378F              ;378d	10 00

L378F:
	inc hl                  ;378f	23
	jr L3788                ;3790	18 f6

L3792:
	push bc                 ;3792	c5
	push de                 ;3793	d5
	push hl                 ;3794	e5
	ld a,$fd                ;3795	3e fd
	rst $28                 ;3797	ef
	ld bc,$e116             ;3798	01 16 e1
	pop de                  ;379b	d1
	pop bc                  ;379c	c1

L379D:
	ld a,(hl)               ;379d	7e
	or a                    ;379e	b7
	ret z                   ;379f	c8
	rst $28                 ;37a0	ef
	djnz L37A3              ;37a1	10 00

L37A3:
	inc hl                  ;37a3	23
	jr L379D                ;37a4	18 f7
	call L36A2              ;37a6	cd a2 36
	ld a,$02                ;37a9	3e 02
	rst $28                 ;37ab	ef
	ld bc,$c916             ;37ac	01 16 c9

L37AF:
	ld a,(de)               ;37af	1a
	and $7f                 ;37b0	e6 7f
	rst $28                 ;37b2	ef
	djnz L37B5              ;37b3	10 00

L37B5:
	ld a,(de)               ;37b5	1a
	inc de                  ;37b6	13
	add a                   ;37b7	87
	jr nc,L37AF             ;37b8	30 f5
	ret                     ;37ba	c9

L37BB:
	inc hl                  ;37bb	23
	ld c,(hl)               ;37bc	4e
	inc hl                  ;37bd	23
	ld a,(hl)               ;37be	7e
	xor c                   ;37bf	a9
	sub c                   ;37c0	91
	ld e,a                  ;37c1	5f
	inc hl                  ;37c2	23
	ld a,(hl)               ;37c3	7e
	adc c                   ;37c4	89
	xor c                   ;37c5	a9
	ld d,a                  ;37c6	57
	ret                     ;37c7	c9

L37C8:
	ld hl,($5c65)           ;37c8	2a 65 5c
	add hl,$fffb            ;37cb	ed 34 fb ff
	ld a,(hl)               ;37cf	7e
	and a                   ;37d0	a7
	jr nz,L37DF             ;37d1	20 0c
	ld ($5c65),hl           ;37d3	22 65 5c
	call L37BB              ;37d6	cd bb 37
	bit 7,c                 ;37d9	cb 79
	ld b,d                  ;37db	42
	ld c,e                  ;37dc	4b
	ld a,e                  ;37dd	7b
	ret                     ;37de	c9

L37DF:
	ld bc,$2da8             ;37df	01 a8 2d
	call L3EC1              ;37e2	cd c1 3e
	ret                     ;37e5	c9

L37E6:
	call L37C8              ;37e6	cd c8 37
	ret c                   ;37e9	d8
	push af                 ;37ea	f5
	dec b                   ;37eb	05
	inc b                   ;37ec	04
	jr nz,L37F1             ;37ed	20 02
	pop af                  ;37ef	f1
	ret                     ;37f0	c9

L37F1:
	pop af                  ;37f1	f1
	scf                     ;37f2	37
	ret                     ;37f3	c9

L37F4:
	call L37C8              ;37f4	cd c8 37
	jr c,L37FA              ;37f7	38 01
	ret z                   ;37f9	c8

L37FA:
	rst $08                 ;37fa	cf
	ld a,(bc)               ;37fb	0a

L37FC:
	call L37E6              ;37fc	cd e6 37
	jr c,L37FA              ;37ff	38 f9
	ret z                   ;3801	c8
	jr L37FA                ;3802	18 f6

L3804:
	call L37C8              ;3804	cd c8 37
	jr c,L37FA              ;3807	38 f1
	ret z                   ;3809	c8
	ld a,b                  ;380a	78
	cpl                     ;380b	2f
	ld b,a                  ;380c	47
	ld a,c                  ;380d	79
	cpl                     ;380e	2f
	ld c,a                  ;380f	4f
	inc bc                  ;3810	03
	ret                     ;3811	c9

L3812:
	call L37E6              ;3812	cd e6 37
	jr c,L37FA              ;3815	38 e3
	ret z                   ;3817	c8
	neg                     ;3818	ed 44
	ret                     ;381a	c9

L381B:
	ld hl,($5c65)           ;381b	2a 65 5c
	dec hl                  ;381e	2b
	ld b,(hl)               ;381f	46
	dec hl                  ;3820	2b
	ld c,(hl)               ;3821	4e
	dec hl                  ;3822	2b
	ld d,(hl)               ;3823	56
	dec hl                  ;3824	2b
	ld e,(hl)               ;3825	5e
	dec hl                  ;3826	2b
	ld a,(hl)               ;3827	7e
	ld ($5c65),hl           ;3828	22 65 5c
	ret                     ;382b	c9

L382C:
	ld hl,($5c65)           ;382c	2a 65 5c
	add hl,$fffb            ;382f	ed 34 fb ff
	ld ($5c65),hl           ;3833	22 65 5c
	ret                     ;3836	c9

L3837:
	res 6,(iy+$01)          ;3837	fd cb 01 b6
	jr L3845                ;383b	18 08

L383D:
	ld c,a                  ;383d	4f
	ld b,$00                ;383e	06 00

; tentative entry (word / jump table scan)
T3840:
	xor a                   ;3840	af
	ld e,a                  ;3841	5f
	ld d,c                  ;3842	51
	ld c,b                  ;3843	48
	ld b,a                  ;3844	47

L3845:
	push bc                 ;3845	c5
	push de                 ;3846	d5
	ld bc,$0005             ;3847	01 05 00
	call L3868              ;384a	cd 68 38
	pop de                  ;384d	d1
	pop bc                  ;384e	c1

L384F:
	ld hl,($5c65)           ;384f	2a 65 5c
	ld (hl),a               ;3852	77
	inc hl                  ;3853	23
	ld (hl),e               ;3854	73
	inc hl                  ;3855	23
	ld (hl),d               ;3856	72
	inc hl                  ;3857	23
	ld (hl),c               ;3858	71
	inc hl                  ;3859	23
	ld (hl),b               ;385a	70
	inc hl                  ;385b	23
	ld ($5c65),hl           ;385c	22 65 5c
	ret                     ;385f	c9

L3860:
	bit 7,(iy+$01)          ;3860	fd cb 01 7e
	ret z                   ;3864	c8
	xor a                   ;3865	af
	jr L383D                ;3866	18 d5

L3868:
	ld hl,($5c65)           ;3868	2a 65 5c
	add hl,bc               ;386b	09
	jr c,L3878              ;386c	38 0a
	ex de,hl                ;386e	eb
	ld hl,$0050             ;386f	21 50 00
	add hl,de               ;3872	19
	jr c,L3878              ;3873	38 03
	sbc hl,sp               ;3875	ed 72
	ret c                   ;3877	d8

L3878:
	rst $08                 ;3878	cf
	inc bc                  ;3879	03

L387A:
	call L388D              ;387a	cd 8d 38
	ccf                     ;387d	3f
	ret c                   ;387e	d8

L387F:
	cp $41                  ;387f	fe 41
	ccf                     ;3881	3f
	ret nc                  ;3882	d0
	cp $5b                  ;3883	fe 5b
	ret c                   ;3885	d8
	cp $61                  ;3886	fe 61
	ccf                     ;3888	3f
	ret nc                  ;3889	d0
	cp $7b                  ;388a	fe 7b
	ret                     ;388c	c9

L388D:
	cp $30                  ;388d	fe 30
	ret c                   ;388f	d8
	cp $3a                  ;3890	fe 3a
	ccf                     ;3892	3f
	ret                     ;3893	c9

L3894:
	call L38BC              ;3894	cd bc 38
	ret nz                  ;3897	c0
	ld hl,($5c4f)           ;3898	2a 4f 5c
	dec hl                  ;389b	2b
	ld bc,$0207             ;389c	01 07 02
	rst $28                 ;389f	ef
	ld d,l                  ;38a0	55
	ld d,$af                ;38a1	16 af
	ld hl,$5ebc             ;38a3	21 bc 5e
	ld (hl),a               ;38a6	77
	dec hl                  ;38a7	2b
	dec hl                  ;38a8	2b
	dec a                   ;38a9	3d
	ld (hl),a               ;38aa	77
	and a                   ;38ab	a7
	ret                     ;38ac	c9

L38AD:
	ld hl,$5b77             ;38ad	21 77 5b
	cp (hl)                 ;38b0	be
	ret z                   ;38b1	c8
	ld (hl),a               ;38b2	77
	cp $ff                  ;38b3	fe ff
	ret z                   ;38b5	c8
	xor a                   ;38b6	af
	ld ($5ebc),a            ;38b7	32 bc 5e
	ld a,(hl)               ;38ba	7e
	ret                     ;38bb	c9

L38BC:
	ld a,($5c50)            ;38bc	3a 50 5c
	cp $5c                  ;38bf	fe 5c
	ret                     ;38c1	c9

L38C2:
	set 7,h                 ;38c2	cb fc
	set 6,h                 ;38c4	cb f4
	ld ($5eb6),hl           ;38c6	22 b6 5e
	ld bc,($5ebb)           ;38c9	ed 4b bb 5e
	and a                   ;38cd	a7
	sbc hl,bc               ;38ce	ed 42
	jr c,L38EF              ;38d0	38 1d
	inc h                   ;38d2	24
	dec h                   ;38d3	25
	jr nz,L38EF             ;38d4	20 19
	ld b,a                  ;38d6	47
	ld a,l                  ;38d7	7d
	cp $fb                  ;38d8	fe fb
	ld a,b                  ;38da	78
	jr nc,L38EF             ;38db	30 12
	add hl,$5db6            ;38dd	ed 34 b6 5d
	push hl                 ;38e1	e5
	inc hl                  ;38e2	23
	inc hl                  ;38e3	23
	ld c,(hl)               ;38e4	4e
	inc hl                  ;38e5	23
	ld b,(hl)               ;38e6	46
	add hl,bc               ;38e7	09
	ld bc,$5eb6             ;38e8	01 b6 5e
	sbc hl,bc               ;38eb	ed 42
	pop hl                  ;38ed	e1
	ret c                   ;38ee	d8

L38EF:
	ld hl,($5eb6)           ;38ef	2a b6 5e
	ld ($5ebb),hl           ;38f2	22 bb 5e
	call L32CC              ;38f5	cd cc 32
	ld de,$5db6             ;38f8	11 b6 5d
	ld bc,$0100             ;38fb	01 00 01
	ldir                    ;38fe	ed b0
	rst $30                 ;3900	f7
	ld hl,$5db6             ;3901	21 b6 5d
	ret                     ;3904	c9

L3905:
	call L38C2              ;3905	cd c2 38
	ld a,(hl)               ;3908	7e
	cp $28                  ;3909	fe 28
	ret nc                  ;390b	d0
	ld d,h                  ;390c	54
	ld e,l                  ;390d	5d
	inc hl                  ;390e	23
	inc hl                  ;390f	23
	ld c,(hl)               ;3910	4e
	inc hl                  ;3911	23
	ld b,(hl)               ;3912	46
	inc hl                  ;3913	23
	add hl,bc               ;3914	09
	add hl,$a24a            ;3915	ed 34 4a a2
	ld bc,($5ebb)           ;3919	ed 4b bb 5e
	add hl,bc               ;391d	09
	scf                     ;391e	37
	ret                     ;391f	c9
Text3920:
	defb 0x3c,0xc8,0x3d,0xcd,0xc8,0x32,0xe5,0x7e ;3920	3c c8 3d cd c8 32 e5 7e	<.=..2.~
	defb 0x23                                    ;3928	23	#
	inc hl                  ;3929	23
	ld c,(hl)               ;392a	4e
	inc hl                  ;392b	23
	ld b,(hl)               ;392c	46
	pop hl                  ;392d	e1
	add bc,$0004            ;392e	ed 36 04 00
	ld de,$5cb6             ;3932	11 b6 5c
	push de                 ;3935	d5
	ldi                     ;3936	ed a0
	cp $28                  ;3938	fe 28
	jr nc,L393E             ;393a	30 02
	ldir                    ;393c	ed b0

L393E:
	ex de,hl                ;393e	eb
	pop hl                  ;393f	e1
	rst $30                 ;3940	f7
	ret                     ;3941	c9

L3942:
	inc a                   ;3942	3c
	jr nz,L3960             ;3943	20 1b
	call L100C              ;3945	cd 0c 10

L3948:
	ld a,(hl)               ;3948	7e
	cp $28                  ;3949	fe 28
	ret nc                  ;394b	d0
	ld d,a                  ;394c	57
	inc hl                  ;394d	23
	ld e,(hl)               ;394e	5e
	dec hl                  ;394f	2b
	ex de,hl                ;3950	eb
	and a                   ;3951	a7
	sbc hl,bc               ;3952	ed 42
	ex de,hl                ;3954	eb
	ccf                     ;3955	3f
	ret c                   ;3956	d8
	inc hl                  ;3957	23
	inc hl                  ;3958	23
	ld e,(hl)               ;3959	5e
	inc hl                  ;395a	23
	ld d,(hl)               ;395b	56
	inc hl                  ;395c	23
	add hl,de               ;395d	19
	jr L3948                ;395e	18 e8

L3960:
	dec a                   ;3960	3d
	call L32CC              ;3961	cd cc 32
	push bc                 ;3964	c5
	call L39B1              ;3965	cd b1 39
	add a                   ;3968	87
	call nc,L1046           ;3969	d4 46 10
	pop bc                  ;396c	c1
	call L0FFF              ;396d	cd ff 0f
	call L3948              ;3970	cd 48 39
	rst $30                 ;3973	f7
	ret                     ;3974	c9

L3975:
	ld a,($5b77)            ;3975	3a 77 5b
	inc a                   ;3978	3c
	jr nz,L398F             ;3979	20 14
	ld d,(hl)               ;397b	56
	ld a,$c0                ;397c	3e c0
	and d                   ;397e	a2
	ret nz                  ;397f	c0
	inc hl                  ;3980	23
	ld e,(hl)               ;3981	5e
	ld ($5c45),de           ;3982	ed 53 45 5c
	inc hl                  ;3986	23
	ld e,(hl)               ;3987	5e
	inc hl                  ;3988	23
	ld d,(hl)               ;3989	56
	ex de,hl                ;398a	eb
	add hl,de               ;398b	19
	inc hl                  ;398c	23
	scf                     ;398d	37
	ret                     ;398e	c9

L398F:
	dec a                   ;398f	3d
	call L38C2              ;3990	cd c2 38
	ld d,(hl)               ;3993	56
	ld a,$c0                ;3994	3e c0
	and d                   ;3996	a2
	ret nz                  ;3997	c0
	inc hl                  ;3998	23
	ld e,(hl)               ;3999	5e
	ld ($5c45),de           ;399a	ed 53 45 5c
	inc hl                  ;399e	23
	ld c,(hl)               ;399f	4e
	inc hl                  ;39a0	23
	ld b,(hl)               ;39a1	46
	ex de,hl                ;39a2	eb
	ld hl,($5eb6)           ;39a3	2a b6 5e
	add hl,$0004            ;39a6	ed 34 04 00
	add hl,bc               ;39aa	09
	res 7,h                 ;39ab	cb bc
	res 6,h                 ;39ad	cb b4
	scf                     ;39af	37
	ret                     ;39b0	c9

L39B1:
	ld hl,($c000)           ;39b1	2a 00 c0
	ld a,l                  ;39b4	7d
	res 7,l                 ;39b5	cb bd
	res 7,h                 ;39b7	cb bc
	ld bc,$4342             ;39b9	01 42 43
	and a                   ;39bc	a7
	sbc hl,bc               ;39bd	ed 42
	ret z                   ;39bf	c8
	rst $30                 ;39c0	f7
	rst $08                 ;39c1	cf
	ld d,$11                ;39c2	16 11
	or (hl)                 ;39c4	b6
	ld e,h                  ;39c5	5c
	push de                 ;39c6	d5
	ld c,$00                ;39c7	0e 00

L39C9:
	dec c                   ;39c9	0d

L39CA:
	ld a,(hl)               ;39ca	7e
	ldi                     ;39cb	ed a0
	cp $22                  ;39cd	fe 22
	jr z,L39CA              ;39cf	28 f9
	cp $10                  ;39d1	fe 10
	jr c,L39E0              ;39d3	38 0b
	cp $3a                  ;39d5	fe 3a
	jp nz,L39C9             ;39d7	c2 c9 39
	bit 0,c                 ;39da	cb 41
	jr nz,L39C9             ;39dc	20 eb

L39DE:
	pop hl                  ;39de	e1
	ret                     ;39df	c9

L39E0:
	cp $0d                  ;39e0	fe 0d
	jr z,L39DE              ;39e2	28 fa
	ldi                     ;39e4	ed a0
	ldi                     ;39e6	ed a0
	ldi                     ;39e8	ed a0
	ldi                     ;39ea	ed a0
	ldi                     ;39ec	ed a0
	jr L39CA                ;39ee	18 da
Data39F0:
	defb 0x2a,0x45,0x5c                          ;39f0	2a 45 5c	*E\

L39F3:
	and a                   ;39f3	a7
	bit 7,h                 ;39f4	cb 7c
	ld hl,($5c59)           ;39f6	2a 59 5c
	ret nz                  ;39f9	c0
	ld hl,($5b77)           ;39fa	2a 77 5b
	inc l                   ;39fd	2c
	ld hl,($5c53)           ;39fe	2a 53 5c

L3A01:
	ret z                   ;3a01	c8

L3A02:
	ld hl,($5eb6)           ;3a02	2a b6 5e
	push de                 ;3a05	d5
	ld de,($5ebb)           ;3a06	ed 5b bb 5e
	sbc hl,de               ;3a0a	ed 52
	pop de                  ;3a0c	d1
	scf                     ;3a0d	37
	ret                     ;3a0e	c9

L3A0F:
	call L3A02              ;3a0f	cd 02 3a
	ex de,hl                ;3a12	eb
	add hl,$a24a            ;3a13	ed 34 4a a2
	and a                   ;3a17	a7
	sbc hl,de               ;3a18	ed 52
	ld de,($5eb6)           ;3a1a	ed 5b b6 5e
	add hl,de               ;3a1e	19
	ret                     ;3a1f	c9

L3A20:
	ld bc,$000b             ;3a20	01 0b 00
	call L3868              ;3a23	cd 68 38
	pop hl                  ;3a26	e1
	pop de                  ;3a27	d1
	pop bc                  ;3a28	c1
	exx                     ;3a29	d9
	ld hl,($5c46)           ;3a2a	2a 46 5c
	push hl                 ;3a2d	e5
	inc sp                  ;3a2e	33
	ld hl,($5c45)           ;3a2f	2a 45 5c
	push hl                 ;3a32	e5
	call L39F3              ;3a33	cd f3 39
	ex de,hl                ;3a36	eb
	ld hl,($5c55)           ;3a37	2a 55 5c
	jr c,L3A3E              ;3a3a	38 02
	sbc hl,de               ;3a3c	ed 52

L3A3E:
	push hl                 ;3a3e	e5
	ld hl,($5c5d)           ;3a3f	2a 5d 5c
	and a                   ;3a42	a7
	sbc hl,de               ;3a43	ed 52
	push hl                 ;3a45	e5
	ld hl,($5eb6)           ;3a46	2a b6 5e
	push hl                 ;3a49	e5
	ld hl,($5b77)           ;3a4a	2a 77 5b
	ld h,a                  ;3a4d	67
	push hl                 ;3a4e	e5
	ld ($5b58),sp           ;3a4f	ed 73 58 5b
	exx                     ;3a53	d9
	push bc                 ;3a54	c5
	ld ($5c3d),sp           ;3a55	ed 73 3d 5c
	push de                 ;3a59	d5
	jp (hl)                 ;3a5a	e9

L3A5B:
	ld hl,($5c3d)           ;3a5b	2a 3d 5c
	inc hl                  ;3a5e	23
	inc hl                  ;3a5f	23
	inc hl                  ;3a60	23
	ld c,(hl)               ;3a61	4e
	ld a,c                  ;3a62	79
	ld ($5b5e),a            ;3a63	32 5e 5b

L3A66:
	call L3AA4              ;3a66	cd a4 3a
	ret c                   ;3a69	d8

L3A6A:
	ld bc,($5c3d)           ;3a6a	ed 4b 3d 5c
	call L26C5              ;3a6e	cd c5 26
	ld bc,$2a29             ;3a71	01 29 2a
	sbc c                   ;3a74	99
	ld e,e                  ;3a75	5b
	bit 0,h                 ;3a76	cb 44
	jr nz,L3A95             ;3a78	20 1b
	ld a,($5c6a)            ;3a7a	3a 6a 5c
	and $7f                 ;3a7d	e6 7f
	or h                    ;3a7f	b4
	ld ($5c6a),a            ;3a80	32 6a 5c
	ld a,l                  ;3a83	7d
	ld ($5b78),a            ;3a84	32 78 5b
	ld hl,($5b9b)           ;3a87	2a 9b 5b
	inc a                   ;3a8a	3c
	jr nz,L3A92             ;3a8b	20 05
	ld de,($5c53)           ;3a8d	ed 5b 53 5c
	add hl,de               ;3a91	19

L3A92:
	ld ($5c57),hl           ;3a92	22 57 5c

L3A95:
	pop hl                  ;3a95	e1
	pop de                  ;3a96	d1
	pop bc                  ;3a97	c1
	ld sp,($5b58)           ;3a98	ed 7b 58 5b
	push bc                 ;3a9c	c5
	ld ($5c3d),sp           ;3a9d	ed 73 3d 5c
	push de                 ;3aa1	d5
	xor a                   ;3aa2	af
	jp (hl)                 ;3aa3	e9

L3AA4:
	call L3AF8              ;3aa4	cd f8 3a
	ret c                   ;3aa7	d8

L3AA8:
	dec hl                  ;3aa8	2b
	push hl                 ;3aa9	e5
	ld a,(hl)               ;3aaa	7e
	inc hl                  ;3aab	23
	ex de,hl                ;3aac	eb
	call L38AD              ;3aad	cd ad 38
	ex de,hl                ;3ab0	eb
	inc hl                  ;3ab1	23
	ld e,(hl)               ;3ab2	5e
	inc hl                  ;3ab3	23
	ld d,(hl)               ;3ab4	56
	inc hl                  ;3ab5	23
	inc a                   ;3ab6	3c
	jr z,L3AC6              ;3ab7	28 0d
	inc a                   ;3ab9	3c
	jr z,L3AF6              ;3aba	28 3a
	dec a                   ;3abc	3d
	dec a                   ;3abd	3d
	push hl                 ;3abe	e5
	ex de,hl                ;3abf	eb
	call L3905              ;3ac0	cd 05 39
	jr nc,L3AF6             ;3ac3	30 31
	pop hl                  ;3ac5	e1

L3AC6:
	ld c,(hl)               ;3ac6	4e
	inc hl                  ;3ac7	23
	ld b,(hl)               ;3ac8	46
	inc hl                  ;3ac9	23
	ld e,(hl)               ;3aca	5e
	inc hl                  ;3acb	23
	ld d,(hl)               ;3acc	56
	inc hl                  ;3acd	23
	ld a,(hl)               ;3ace	7e
	inc hl                  ;3acf	23
	ex af,af'               ;3ad0	08
	ld a,(hl)               ;3ad1	7e
	inc hl                  ;3ad2	23
	ld l,(hl)               ;3ad3	6e
	ld (iy+$0d),l           ;3ad4	fd 75 0d
	ld h,a                  ;3ad7	67
	ex af,af'               ;3ad8	08
	ld l,a                  ;3ad9	6f
	ld ($5c45),hl           ;3ada	22 45 5c
	call L39F3              ;3add	cd f3 39
	ex de,hl                ;3ae0	eb
	jr c,L3AE4              ;3ae1	38 01
	add hl,de               ;3ae3	19

L3AE4:
	ld ($5c55),hl           ;3ae4	22 55 5c
	ex de,hl                ;3ae7	eb
	add hl,bc               ;3ae8	09
	ld ($5c5d),hl           ;3ae9	22 5d 5c
	pop hl                  ;3aec	e1
	add hl,$000b            ;3aed	ed 34 0b 00
	ld ($5b58),hl           ;3af1	22 58 5b
	xor a                   ;3af4	af
	ret                     ;3af5	c9

L3AF6:
	rst $08                 ;3af6	cf
	ld d,$3e                ;3af7	16 3e
	ld bc,$9a32             ;3af9	01 32 9a
	ld e,e                  ;3afc	5b
	call L26C5              ;3afd	cd c5 26
	sbc c                   ;3b00	99
	jr z,L3B2D              ;3b01	28 2a
	dec a                   ;3b03	3d
	ld e,h                  ;3b04	5c
	add hl,$fff8            ;3b05	ed 34 f8 ff

L3B09:
	add hl,$000b            ;3b09	ed 34 0b 00

L3B0D:
	ld a,(hl)               ;3b0d	7e
	cp c                    ;3b0e	b9
	ret z                   ;3b0f	c8
	cp $4b                  ;3b10	fe 4b
	jr z,L3B09              ;3b12	28 f5
	cp $02                  ;3b14	fe 02
	jr c,L3B28              ;3b16	38 10
	cp $3e                  ;3b18	fe 3e
	jr z,L3B46              ;3b1a	28 2a
	jr nc,L3B40             ;3b1c	30 22
	ld a,c                  ;3b1e	79
	cp $20                  ;3b1f	fe 20
	ret z                   ;3b21	c8
	and $e0                 ;3b22	e6 e0
	cp (hl)                 ;3b24	be
	jr nc,L3B09             ;3b25	30 e2
	ret                     ;3b27	c9

L3B28:
	bit 5,c                 ;3b28	cb 69
	ret z                   ;3b2a	c8
	dec hl                  ;3b2b	2b
	ld b,(hl)               ;3b2c	46

L3B2D:
	inc hl                  ;3b2d	23
	inc hl                  ;3b2e	23
	ld e,(hl)               ;3b2f	5e

L3B30:
	inc hl                  ;3b30	23
	ld d,(hl)               ;3b31	56
	inc hl                  ;3b32	23
	add hl,de               ;3b33	19
	cp $01                  ;3b34	fe 01
	jr nz,L3B3D             ;3b36	20 05
	inc b                   ;3b38	04
	dec b                   ;3b39	05
	call z,L3B8E            ;3b3a	cc 8e 3b

L3B3D:
	inc hl                  ;3b3d	23
	jr L3B0D                ;3b3e	18 cd

L3B40:
	bit 6,c                 ;3b40	cb 71
	jr nz,L3B48             ;3b42	20 04
	bit 5,c                 ;3b44	cb 69

L3B46:
	scf                     ;3b46	37
	ret z                   ;3b47	c8

L3B48:
	cp $48                  ;3b48	fe 48
	jr z,L3B64              ;3b4a	28 18
	jr nc,L3B73             ;3b4c	30 25
	dec hl                  ;3b4e	2b
	ld e,(hl)               ;3b4f	5e
	inc hl                  ;3b50	23
	inc hl                  ;3b51	23
	ld d,(hl)               ;3b52	56
	inc hl                  ;3b53	23
	ld ($5b99),de           ;3b54	ed 53 99 5b
	ld e,(hl)               ;3b58	5e
	inc hl                  ;3b59	23
	ld d,(hl)               ;3b5a	56
	inc hl                  ;3b5b	23
	ld ($5b9b),de           ;3b5c	ed 53 9b 5b
	inc hl                  ;3b60	23
	jp L3B0D                ;3b61	c3 0d 3b

L3B64:
	dec hl                  ;3b64	2b
	ld a,(hl)               ;3b65	7e
	inc hl                  ;3b66	23

L3B67:
	inc hl                  ;3b67	23
	push bc                 ;3b68	c5
	call L26C5              ;3b69	cd c5 26
	ld (hl),a               ;3b6c	77
	jr z,L3B30              ;3b6d	28 c1
	inc hl                  ;3b6f	23
	jp L3B0D                ;3b70	c3 0d 3b

L3B73:
	push bc                 ;3b73	c5
	push hl                 ;3b74	e5
	dec hl                  ;3b75	2b
	ld a,(hl)               ;3b76	7e
	ld e,a                  ;3b77	5f
	inc hl                  ;3b78	23
	ld bc,($5c3d)           ;3b79	ed 4b 3d 5c
	and a                   ;3b7d	a7
	sbc hl,bc               ;3b7e	ed 42
	call L26C5              ;3b80	cd c5 26
	push hl                 ;3b83	e5
	jr z,L3B67              ;3b84	28 e1
	pop bc                  ;3b86	c1
	add hl,$0082            ;3b87	ed 34 82 00
	jp L3B0D                ;3b8b	c3 0d 3b

L3B8E:
	push bc                 ;3b8e	c5
	push hl                 ;3b8f	e5
	add hl,$fff8            ;3b90	ed 34 f8 ff
	ld a,(hl)               ;3b94	7e
	inc hl                  ;3b95	23
	ld c,(hl)               ;3b96	4e
	inc hl                  ;3b97	23
	ld b,(hl)               ;3b98	46
	inc hl                  ;3b99	23
	inc a                   ;3b9a	3c
	jr nz,L3BAB             ;3b9b	20 0e
	ex de,hl                ;3b9d	eb
	ld hl,($5c53)           ;3b9e	2a 53 5c
	add hl,bc               ;3ba1	09
	ex de,hl                ;3ba2	eb
	ld bc,$0005             ;3ba3	01 05 00
	ldir                    ;3ba6	ed b0

L3BA8:
	pop hl                  ;3ba8	e1
	pop bc                  ;3ba9	c1
	ret                     ;3baa	c9

L3BAB:
	inc a                   ;3bab	3c
	jr z,L3BA8              ;3bac	28 fa
	push bc                 ;3bae	c5
	ld de,$5caa             ;3baf	11 aa 5c
	ld bc,$0005             ;3bb2	01 05 00
	ldir                    ;3bb5	ed b0
	pop hl                  ;3bb7	e1
	dec a                   ;3bb8	3d
	dec a                   ;3bb9	3d
	call L32CC              ;3bba	cd cc 32
	ex de,hl                ;3bbd	eb
	ld hl,$5caa             ;3bbe	21 aa 5c
	ld bc,$0005             ;3bc1	01 05 00
	ldir                    ;3bc4	ed b0
	rst $30                 ;3bc6	f7
	ld hl,($5ebb)           ;3bc7	2a bb 5e
	ex de,hl                ;3bca	eb
	and a                   ;3bcb	a7
	sbc hl,de               ;3bcc	ed 52
	inc h                   ;3bce	24
	dec h                   ;3bcf	25
	jr nz,L3BA8             ;3bd0	20 d6
	ex de,hl                ;3bd2	eb
	add de,$5db1            ;3bd3	ed 35 b1 5d
	ld hl,$5caa             ;3bd7	21 aa 5c
	ld bc,$0005             ;3bda	01 05 00
	ldir                    ;3bdd	ed b0
	jr L3BA8                ;3bdf	18 c7

L3BE1:
	pop ix                  ;3be1	dd e1
	pop bc                  ;3be3	c1
	pop de                  ;3be4	d1
	add hl,$000a            ;3be5	ed 34 0a 00

L3BE9:
	ld sp,hl                ;3be9	f9
	ld ($5b58),sp           ;3bea	ed 73 58 5b
	push de                 ;3bee	d5
	ld ($5c3d),sp           ;3bef	ed 73 3d 5c
	push bc                 ;3bf3	c5
	jp (ix)                 ;3bf4	dd e9

L3BF6:
	pop ix                  ;3bf6	dd e1
	pop bc                  ;3bf8	c1
	pop de                  ;3bf9	d1
	ld hl,$000b             ;3bfa	21 0b 00
	add hl,sp               ;3bfd	39
	jr L3BE9                ;3bfe	18 e9

L3C00:
	call L3EC9              ;3c00	cd c9 3e
	cp $25                  ;3c03	fe 25
	jp z,L3D10              ;3c05	ca 10 3d
	ld e,a                  ;3c08	5f
	set 5,e                 ;3c09	cb eb
	rst $28                 ;3c0b	ef
	call nc,$c51b           ;3c0c	d4 1b c5
	ld ($5c5b),hl           ;3c0f	22 5b 5c
	cp $0e                  ;3c12	fe 0e
	jr nz,L3C1A             ;3c14	20 04
	add hl,$0006            ;3c16	ed 34 06 00

L3C1A:
	call L3ED0              ;3c1a	cd d0 3e
	cp $24                  ;3c1d	fe 24
	jr nz,L3C6B             ;3c1f	20 4a
	inc hl                  ;3c21	23
	ld a,(hl)               ;3c22	7e
	cp $0e                  ;3c23	fe 0e
	jr nz,L3C2B             ;3c25	20 04
	add hl,$0006            ;3c27	ed 34 06 00

L3C2B:
	call L3ED0              ;3c2b	cd d0 3e
	cp $28                  ;3c2e	fe 28
	jr nz,L3C3A             ;3c30	20 08
	rst $20                 ;3c32	e7
	ld c,$c0                ;3c33	0e c0
	res 0,d                 ;3c35	cb 82

L3C37:
	rst $20                 ;3c37	e7
	jr L3C3E                ;3c38	18 04

L3C3A:
	ld c,$40                ;3c3a	0e 40
	res 1,d                 ;3c3c	cb 8a

L3C3E:
	ld a,e                  ;3c3e	7b
	and $1f                 ;3c3f	e6 1f
	or c                    ;3c41	b1
	ex af,af'               ;3c42	08
	bit 7,c                 ;3c43	cb 79
	call z,L3D97            ;3c45	cc 97 3d
	ld a,d                  ;3c48	7a
	and $65                 ;3c49	e6 65
	jr nz,L3C7E             ;3c4b	20 31
	ld ixh,$02              ;3c4d	dd 26 02
	bit 4,d                 ;3c50	cb 62
	jr z,L3C56              ;3c52	28 02

L3C54:
	inc ixh                 ;3c54	dd 24

L3C56:
	bit 1,d                 ;3c56	cb 4a
	jr z,L3C62              ;3c58	28 08
	ld de,$3d82             ;3c5a	11 82 3d
	ld bc,$0003             ;3c5d	01 03 00
	jr L3CA0                ;3c60	18 3e

L3C62:
	ld a,d                  ;3c62	7a
	ld b,a                  ;3c63	47
	ld c,a                  ;3c64	4f
	and a                   ;3c65	a7
	call nz,L381B           ;3c66	c4 1b 38
	jr L3CA0                ;3c69	18 35

L3C6B:
	cp $28                  ;3c6b	fe 28
	jr nz,L3C80             ;3c6d	20 11
	rst $20                 ;3c6f	e7
	ld c,$80                ;3c70	0e 80
	bit 1,d                 ;3c72	cb 4a
	jr nz,L3C37             ;3c74	20 c1
	ld a,d                  ;3c76	7a
	xor $41                 ;3c77	ee 41
	ld d,a                  ;3c79	57
	and $41                 ;3c7a	e6 41
	jr z,L3C37              ;3c7c	28 b9

L3C7E:
	pop bc                  ;3c7e	c1
	ret                     ;3c7f	c9

L3C80:
	inc b                   ;3c80	04
	jr z,L3C87              ;3c81	28 04
	set 7,e                 ;3c83	cb fb
	res 6,e                 ;3c85	cb b3

L3C87:
	ld a,e                  ;3c87	7b
	ex af,af'               ;3c88	08
	call L3D85              ;3c89	cd 85 3d
	jr nc,L3C7E             ;3c8c	30 f0
	ld ixh,$00              ;3c8e	dd 26 00
	bit 4,d                 ;3c91	cb 62
	jr nz,L3C54             ;3c93	20 bf
	ld hl,$3fb2             ;3c95	21 b2 3f
	and a                   ;3c98	a7
	call nz,L382C           ;3c99	c4 2c 38
	ex de,hl                ;3c9c	eb
	ld bc,$0005             ;3c9d	01 05 00

L3CA0:
	pop af                  ;3ca0	f1
	neg                     ;3ca1	ed 44
	ld ixl,a                ;3ca3	dd 6f
	push bc                 ;3ca5	c5
	add bc,a                ;3ca6	ed 33
	push de                 ;3ca8	d5
	call nz,L3868           ;3ca9	c4 68 38
	pop de                  ;3cac	d1
	pop bc                  ;3cad	c1
	exx                     ;3cae	d9
	pop hl                  ;3caf	e1
	pop de                  ;3cb0	d1
	pop bc                  ;3cb1	c1
	exx                     ;3cb2	d9
	ld hl,$0000             ;3cb3	21 00 00
	and a                   ;3cb6	a7
	sbc hl,bc               ;3cb7	ed 42
	add hl,sp               ;3cb9	39
	jr z,L3CC2              ;3cba	28 06
	ld sp,hl                ;3cbc	f9
	ex de,hl                ;3cbd	eb
	push bc                 ;3cbe	c5
	ldir                    ;3cbf	ed b0
	pop bc                  ;3cc1	c1

L3CC2:
	ld a,ixh                ;3cc2	dd 7c
	cp $02                  ;3cc4	fe 02
	jr nz,L3CCB             ;3cc6	20 03
	push bc                 ;3cc8	c5
	inc bc                  ;3cc9	03
	inc bc                  ;3cca	03

L3CCB:
	rra                     ;3ccb	1f
	jr nc,L3CD4             ;3ccc	30 06
	ld a,($5ca6)            ;3cce	3a a6 5c
	push af                 ;3cd1	f5
	inc sp                  ;3cd2	33
	inc bc                  ;3cd3	03

L3CD4:
	ld a,ixl                ;3cd4	dd 7d
	add bc,a                ;3cd6	ed 33
	dec a                   ;3cd8	3d
	jr nz,L3CEE             ;3cd9	20 13

L3CDB:
	ex af,af'               ;3cdb	08
	push af                 ;3cdc	f5
	inc sp                  ;3cdd	33

L3CDE:
	push bc                 ;3cde	c5
	ld d,ixh                ;3cdf	dd 54
	res 1,d                 ;3ce1	cb 8a
	ld e,d                  ;3ce3	5a
	push de                 ;3ce4	d5
	exx                     ;3ce5	d9
	push bc                 ;3ce6	c5
	ld ($5c3d),sp           ;3ce7	ed 73 3d 5c
	push de                 ;3ceb	d5
	scf                     ;3cec	37
	jp (hl)                 ;3ced	e9

L3CEE:
	ld hl,($5c5b)           ;3cee	2a 5b 5c
	ld d,a                  ;3cf1	57
	ld e,$a0                ;3cf2	1e a0

L3CF4:
	dec hl                  ;3cf4	2b
	ld a,(hl)               ;3cf5	7e
	cp $20                  ;3cf6	fe 20
	jr z,L3CF4              ;3cf8	28 fa
	or e                    ;3cfa	b3
	ld e,$20                ;3cfb	1e 20
	push af                 ;3cfd	f5
	inc sp                  ;3cfe	33
	dec d                   ;3cff	15
	jr nz,L3CF4             ;3d00	20 f2
	ld a,ixh                ;3d02	dd 7c
	bit 1,a                 ;3d04	cb 4f
	jr z,L3CDB              ;3d06	28 d3
	ex af,af'               ;3d08	08
	ld d,a                  ;3d09	57
	ld e,$7f                ;3d0a	1e 7f
	push de                 ;3d0c	d5
	inc bc                  ;3d0d	03
	jr L3CDE                ;3d0e	18 ce

L3D10:
	rst $20                 ;3d10	e7
	or $20                  ;3d11	f6 20
	sub $61                 ;3d13	d6 61
	ex af,af'               ;3d15	08
	rst $20                 ;3d16	e7
	cp $28                  ;3d17	fe 28
	jr z,L3D41              ;3d19	28 26
	call L3D85              ;3d1b	cd 85 3d
	ret nc                  ;3d1e	d0
	ex af,af'               ;3d1f	08
	ld hl,$fff8             ;3d20	21 f8 ff
	call L3D5C              ;3d23	cd 5c 3d
	ld (hl),$48             ;3d26	36 48
	inc hl                  ;3d28	23
	push af                 ;3d29	f5
	call L26C5              ;3d2a	cd c5 26
	ld e,e                  ;3d2d	5b
	jr z,L3D38              ;3d2e	28 08
	ld d,a                  ;3d30	57
	ld e,a                  ;3d31	5f
	jr z,L3D39              ;3d32	28 05
	call L26C5              ;3d34	cd c5 26
	pop af                  ;3d37	f1

L3D38:
	inc h                   ;3d38	24

L3D39:
	pop af                  ;3d39	f1
	call L26C5              ;3d3a	cd c5 26
	xor h                   ;3d3d	ac
	jr z,L3D77              ;3d3e	28 37
	ret                     ;3d40	c9

L3D41:
	rst $20                 ;3d41	e7
	rst $20                 ;3d42	e7
	bit 2,d                 ;3d43	cb 52
	jr nz,L3D4C             ;3d45	20 05
	bit 1,d                 ;3d47	cb 4a
	ret z                   ;3d49	c8
	ld e,$ff                ;3d4a	1e ff

L3D4C:
	ex af,af'               ;3d4c	08
	ld hl,$ff7e             ;3d4d	21 7e ff
	call L3D5C              ;3d50	cd 5c 3d
	ld (hl),$c2             ;3d53	36 c2
	call L26C5              ;3d55	cd c5 26
	rst $08                 ;3d58	cf
	jr z,L3D92              ;3d59	28 37
	ret                     ;3d5b	c9

L3D5C:
	pop ix                  ;3d5c	dd e1
	exx                     ;3d5e	d9
	pop hl                  ;3d5f	e1
	pop de                  ;3d60	d1
	pop bc                  ;3d61	c1
	exx                     ;3d62	d9
	add hl,sp               ;3d63	39
	ld bc,($5c65)           ;3d64	ed 4b 65 5c
	add bc,$0050            ;3d68	ed 36 50 00
	and a                   ;3d6c	a7
	sbc hl,bc               ;3d6d	ed 42
	jr c,L3D80              ;3d6f	38 0f
	add hl,bc               ;3d71	09
	ld sp,hl                ;3d72	f9
	ld (hl),a               ;3d73	77
	inc hl                  ;3d74	23
	exx                     ;3d75	d9
	push bc                 ;3d76	c5

L3D77:
	ld ($5c3d),sp           ;3d77	ed 73 3d 5c
	push de                 ;3d7b	d5
	push hl                 ;3d7c	e5
	exx                     ;3d7d	d9
	jp (ix)                 ;3d7e	dd e9

L3D80:
	rst $08                 ;3d80	cf
	inc bc                  ;3d81	03
	ld bc,$0000             ;3d82	01 00 00

L3D85:
	call L3D97              ;3d85	cd 97 3d
	ld a,d                  ;3d88	7a
	and $20                 ;3d89	e6 20
	ret nz                  ;3d8b	c0
	ld a,d                  ;3d8c	7a
	add a                   ;3d8d	87
	add d                   ;3d8e	82
	and $80                 ;3d8f	e6 80
	ret nz                  ;3d91	c0

L3D92:
	ld a,d                  ;3d92	7a
	and $80                 ;3d93	e6 80
	scf                     ;3d95	37
	ret                     ;3d96	c9

L3D97:
	ld a,(hl)               ;3d97	7e
	cp $3d                  ;3d98	fe 3d
	ret nz                  ;3d9a	c0
	rst $20                 ;3d9b	e7
	push de                 ;3d9c	d5
	push ix                 ;3d9d	dd e5
	ex af,af'               ;3d9f	08
	push af                 ;3da0	f5
	call L0E2D              ;3da1	cd 2d 0e
	pop af                  ;3da4	f1
	ex af,af'               ;3da5	08
	pop ix                  ;3da6	dd e1
	pop de                  ;3da8	d1
	bit 7,d                 ;3da9	cb 7a
	jp nz,L382C             ;3dab	c2 2c 38
	ld a,($5c3b)            ;3dae	3a 3b 5c
	and $40                 ;3db1	e6 40
	or $80                  ;3db3	f6 80
	ld d,a                  ;3db5	57
	ret                     ;3db6	c9
Zero3DB7:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;3db7	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;3dbf	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00,0x00,0x00,0x00,0x00           ;3dc7	00 00 00 00 00 00	......
	nop                     ;3dcd	00
	nop                     ;3dce	00
	nop                     ;3dcf	00
	nop                     ;3dd0	00
	nop                     ;3dd1	00
	nop                     ;3dd2	00
	nop                     ;3dd3	00
	nop                     ;3dd4	00
	nop                     ;3dd5	00
	nop                     ;3dd6	00
	nop                     ;3dd7	00
	nop                     ;3dd8	00
	nop                     ;3dd9	00
	nop                     ;3dda	00
	nop                     ;3ddb	00
	nop                     ;3ddc	00
	nop                     ;3ddd	00
	nop                     ;3dde	00
	nop                     ;3ddf	00
	nop                     ;3de0	00
	nop                     ;3de1	00
	nop                     ;3de2	00
	nop                     ;3de3	00
	nop                     ;3de4	00
	nop                     ;3de5	00
	nop                     ;3de6	00
	nop                     ;3de7	00
	nop                     ;3de8	00
	nop                     ;3de9	00
	nop                     ;3dea	00
	nop                     ;3deb	00
	nop                     ;3dec	00
	nop                     ;3ded	00
	nop                     ;3dee	00
	nop                     ;3def	00
	nop                     ;3df0	00
	nop                     ;3df1	00
	nop                     ;3df2	00
	nop                     ;3df3	00
	nop                     ;3df4	00
	nop                     ;3df5	00
	nop                     ;3df6	00
	nop                     ;3df7	00
	nop                     ;3df8	00
	nop                     ;3df9	00
	nop                     ;3dfa	00
	nop                     ;3dfb	00
	nop                     ;3dfc	00
	nop                     ;3dfd	00
	nop                     ;3dfe	00
	nop                     ;3dff	00
	ld c,l                  ;3e00	4d
	inc (hl)                ;3e01	34
	ld c,l                  ;3e02	4d
	inc (hl)                ;3e03	34
	call T0E19              ;3e04	cd 19 0e
	jr z,L3E12              ;3e07	28 09
	call L37FC              ;3e09	cd fc 37
	push af                 ;3e0c	f5
	call L37FC              ;3e0d	cd fc 37
	pop bc                  ;3e10	c1
	ld c,a                  ;3e11	4f

L3E12:
	push bc                 ;3e12	c5
	call L3E3E              ;3e13	cd 3e 3e
	push hl                 ;3e16	e5
	call L3E3E              ;3e17	cd 3e 3e
	pop de                  ;3e1a	d1
	pop bc                  ;3e1b	c1
	call T0E19              ;3e1c	cd 19 0e
	jr z,L3E28              ;3e1f	28 07
	rst $00                 ;3e21	c7
	rst $08                 ;3e22	cf
	ld bc,$0238             ;3e23	01 38 02
	rst $08                 ;3e26	cf
	ld (de),a               ;3e27	12

L3E28:
	push hl                 ;3e28	e5
	push de                 ;3e29	d5
	call L3EC9              ;3e2a	cd c9 3e
	cp $cc                  ;3e2d	fe cc
	call L3E59              ;3e2f	cd 59 3e
	pop bc                  ;3e32	c1
	call nc,L3E54           ;3e33	d4 54 3e
	pop bc                  ;3e36	c1
	call nc,L3E54           ;3e37	d4 54 3e
	call L0902              ;3e3a	cd 02 09
	ret                     ;3e3d	c9

L3E3E:
	call L3EC9              ;3e3e	cd c9 3e
	ld hl,$0000             ;3e41	21 00 00
	cp $2c                  ;3e44	fe 2c
	ret nz                  ;3e46	c0
	call T0638              ;3e47	cd 38 06
	call T0E19              ;3e4a	cd 19 0e
	ret z                   ;3e4d	c8
	call L37F4              ;3e4e	cd f4 37
	ld h,b                  ;3e51	60
	ld l,c                  ;3e52	69
	ret                     ;3e53	c9

L3E54:
	call L3EC9              ;3e54	cd c9 3e
	cp $2c                  ;3e57	fe 2c

L3E59:
	scf                     ;3e59	37
	ret nz                  ;3e5a	c0
	push bc                 ;3e5b	c5
	rst $20                 ;3e5c	e7
	call L05A3              ;3e5d	cd a3 05
	bit 6,(iy+$01)          ;3e60	fd cb 01 76
	jp z,L099D              ;3e64	ca 9d 09
	pop bc                  ;3e67	c1
	call T0E19              ;3e68	cd 19 0e
	jr z,L3E73              ;3e6b	28 06
	call T3840              ;3e6d	cd 40 38
	call L2D85              ;3e70	cd 85 2d

L3E73:
	and a                   ;3e73	a7
	ret                     ;3e74	c9

L3E75:
	push de                 ;3e75	d5
	call L0059              ;3e76	cd 59 00
	bit 1,e                 ;3e79	cb 4b
	pop de                  ;3e7b	d1
	ret                     ;3e7c	c9
Zero3E7D:
	defb 0x00,0x00,0x00                          ;3e7d	00 00 00	...

L3E80:
	ld ($5b54),bc           ;3e80	ed 43 54 5b
	ex (sp),hl              ;3e84	e3
	ld c,(hl)               ;3e85	4e
	inc hl                  ;3e86	23
	ld b,(hl)               ;3e87	46

L3E88:
	inc hl                  ;3e88	23
	ex (sp),hl              ;3e89	e3
	push $3e93              ;3e8a	ed 8a 3e 93
	push bc                 ;3e8e	c5

L3E8F:
	ld bc,($5b54)           ;3e8f	ed 4b 54 5b

L3E93:
	nextreg $8e,$00         ;3e93	ed 91 8e 00
	ret                     ;3e97	c9
	inc de                  ;3e98	13
	ld bc,$0710             ;3e99	01 10 07
	ld de,$1600             ;3e9c	11 00 16
	rst $38                 ;3e9f	ff
	ld ($5b56),a            ;3ea0	32 56 5b
	ld a,$87                ;3ea3	3e 87
	ld ($5b54),bc           ;3ea5	ed 43 54 5b
	pop bc                  ;3ea9	c1
	push $007b              ;3eaa	ed 8a 00 7b
	jp L26D4                ;3eae	c3 d4 26
	call T0068              ;3eb1	cd 68 00
	sub a                   ;3eb4	97
	dec h                   ;3eb5	25
	ret                     ;3eb6	c9

L3EB7:
	ld d,$00                ;3eb7	16 00

L3EB9:
	call L26C5              ;3eb9	cd c5 26
	ld c,h                  ;3ebc	4c
	jr z,L3E88              ;3ebd	28 c9

L3EBF:
	res 6,b                 ;3ebf	cb b0

L3EC1:
	push $5b3e              ;3ec1	ed 8a 5b 3e
	push bc                 ;3ec5	c5
	jp $5b48                ;3ec6	c3 48 5b

L3EC9:
	ld hl,($5c5d)           ;3ec9	2a 5d 5c
	jp L3ED3                ;3ecc	c3 d3 3e

L3ECF:
	inc hl                  ;3ecf	23

L3ED0:
	ld ($5c5d),hl           ;3ed0	22 5d 5c

L3ED3:
	ld a,(hl)               ;3ed3	7e
	cp $21                  ;3ed4	fe 21
	ret nc                  ;3ed6	d0
	cp $0d                  ;3ed7	fe 0d
	ret z                   ;3ed9	c8
	cp $0e                  ;3eda	fe 0e
	ret z                   ;3edc	c8
	inc hl                  ;3edd	23
	cp $18                  ;3ede	fe 18
	jr nc,L3ED0             ;3ee0	30 ee
	cp $10                  ;3ee2	fe 10
	jr c,L3ED0              ;3ee4	38 ea
	cp $16                  ;3ee6	fe 16
	jr c,L3ECF              ;3ee8	38 e5
	inc hl                  ;3eea	23
	jr L3ECF                ;3eeb	18 e2
Text3EED:
	defb 0xd9,0x2a,0x77,0x5b,0x5c,0x65,0x6b,0x22 ;3eed	d9 2a 77 5b 5c 65 6b 22	.*w[\ek"
	defb 0x77,0x5b,0xd9,0xc9                     ;3ef5	77 5b d9 c9	w[..

; tentative entry (word / jump table scan)
T3EF9:
	bit 7,(iy+$01)          ;3ef9	fd cb 01 7e
	ret nz                  ;3efd	c0
	pop hl                  ;3efe	e1
	ret                     ;3eff	c9

L3F00:
	ld ($5b54),bc           ;3f00	ed 43 54 5b
	ex (sp),hl              ;3f04	e3
	ld c,(hl)               ;3f05	4e
	inc hl                  ;3f06	23
	ld b,(hl)               ;3f07	46
	inc hl                  ;3f08	23
	ex (sp),hl              ;3f09	e3
	push $3f13              ;3f0a	ed 8a 3f 13
	push bc                 ;3f0e	c5
	ld bc,($5b54)           ;3f0f	ed 4b 54 5b
	nextreg $8e,$02         ;3f13	ed 91 8e 02
	ret                     ;3f17	c9
	cp $21                  ;3f18	fe 21
	ret nc                  ;3f1a	d0
	cp $0d                  ;3f1b	fe 0d
	ret z                   ;3f1d	c8
	cp $10                  ;3f1e	fe 10
	ret c                   ;3f20	d8
	cp $18                  ;3f21	fe 18
	ccf                     ;3f23	3f
	ret c                   ;3f24	d8
	inc hl                  ;3f25	23
	cp $16                  ;3f26	fe 16
	jr c,L3F2B              ;3f28	38 01
	inc hl                  ;3f2a	23

L3F2B:
	scf                     ;3f2b	37
	ld ($5c5d),hl           ;3f2c	22 5d 5c
	ret                     ;3f2f	c9
Data3F30:
	defb 0x06,0x02,0x18,0x0a                     ;3f30	06 02 18 0a	....
	rst $00                 ;3f34	c7
	ld e,h                  ;3f35	5c
	nop                     ;3f36	00
	call L383D              ;3f37	cd 3d 38
	jr L3F54                ;3f3a	18 18
	ld b,$00                ;3f3c	06 00
	rst $00                 ;3f3e	c7
	ld h,d                  ;3f3f	62
	nop                     ;3f40	00
	push de                 ;3f41	d5
	push hl                 ;3f42	e5
	pop bc                  ;3f43	c1
	call T3840              ;3f44	cd 40 38
	pop bc                  ;3f47	c1
	call T3840              ;3f48	cd 40 38
	ld de,$0e02             ;3f4b	11 02 0e
	ld bc,$000a             ;3f4e	01 0a 00
	call L09A8              ;3f51	cd a8 09

L3F54:
	set 6,(iy+$01)          ;3f54	fd cb 01 f6
	jp L2D85                ;3f58	c3 85 2d
Text3F5B:
	defb 0xcd,0x1b,0x38,0xc5,0xd5,0xcd,0xfc      ;3f5b	cd 1b 38 c5 d5 cd fc	..8....

L3F62:
	scf                     ;3f62	37
	pop de                  ;3f63	d1
	pop bc                  ;3f64	c1
	rst $00                 ;3f65	c7
	ld d,(hl)               ;3f66	56
	nop                     ;3f67	00

L3F68:
	jp nc,L08F2             ;3f68	d2 f2 08
	ret                     ;3f6b	c9
	call L37FC              ;3f6c	cd fc 37
	rst $00                 ;3f6f	c7
	ld e,c                  ;3f70	59
	nop                     ;3f71	00
	jr L3F68                ;3f72	18 f4
	ld de,$0e0c             ;3f74	11 0c 0e
	ld bc,$000d             ;3f77	01 0d 00
	call L09A8              ;3f7a	cd a8 09
	call L37FC              ;3f7d	cd fc 37
	rst $28                 ;3f80	ef
	ld bc,$cd16             ;3f81	01 16 cd
	ret z                   ;3f84	c8
	scf                     ;3f85	37
	push bc                 ;3f86	c5
	call L37C8              ;3f87	cd c8 37
	push bc                 ;3f8a	c5
	pop hl                  ;3f8b	e1

L3F8C:
	pop de                  ;3f8c	d1
	ld b,$01                ;3f8d	06 01
	rst $00                 ;3f8f	c7
	ld h,d                  ;3f90	62
	nop                     ;3f91	00
	ret                     ;3f92	c9

L3F93:
	call L26C5              ;3f93	cd c5 26
	ld d,a                  ;3f96	57
	jr z,L3F62              ;3f97	28 c9
	ccf                     ;3f99	3f
	jr nz,L3FC4             ;3f9a	20 28
	ld e,c                  ;3f9c	59
	cpl                     ;3f9d	2f
	ld c,(hl)               ;3f9e	4e
	add hl,hl               ;3f9f	29
	dec c                   ;3fa0	0d
	nop                     ;3fa1	00
	jr nz,L3FC4             ;3fa2	20 20
	jr nz,L3FC6             ;3fa4	20 20
	jr nz,L3FC8             ;3fa6	20 20
	jr nz,L3FCA             ;3fa8	20 20
	jr nz,L3FCC             ;3faa	20 20
	jr nz,L3FCE             ;3fac	20 20
	jr nz,L3FD0             ;3fae	20 20
	jr nz,L3FD2             ;3fb0	20 20
	nop                     ;3fb2	00
	nop                     ;3fb3	00
	nop                     ;3fb4	00
	nop                     ;3fb5	00
	nop                     ;3fb6	00
	rst $28                 ;3fb7	ef
	ld c,a                  ;3fb8	4f
	ld e,$21                ;3fb9	1e 21
	sub c                   ;3fbb	91
	ld hl,($0011)           ;3fbc	2a 11 00
	nop                     ;3fbf	00
	jp L3EB9                ;3fc0	c3 b9 3e
	call L26DF              ;3fc3	cd df 26

L3FC6:
	sub d                   ;3fc6	92
	jr z,L3F8C              ;3fc7	28 c3
	add l                   ;3fc9	85

L3FCA:
	dec l                   ;3fca	2d
	call L37FC              ;3fcb	cd fc 37

L3FCE:
	push af                 ;3fce	f5
	call L37FC              ;3fcf	cd fc 37

L3FD2:
	ld bc,$243b             ;3fd2	01 3b 24
	out (c),a               ;3fd5	ed 79
	inc b                   ;3fd7	04
	pop af                  ;3fd8	f1
	out (c),a               ;3fd9	ed 79
	ret                     ;3fdb	c9
	ld hl,$0000             ;3fdc	21 00 00
	ld ($5c78),hl           ;3fdf	22 78 5c
	xor a                   ;3fe2	af
	ld ($5c7a),a            ;3fe3	32 7a 5c
	ret                     ;3fe6	c9

L3FE7:
	ld a,($5c7f)            ;3fe7	3a 7f 5c
	and $0f                 ;3fea	e6 0f
	ret z                   ;3fec	c8
	cp $03                  ;3fed	fe 03
	ret c                   ;3fef	d8
	srl a                   ;3ff0	cb 3f
	srl a                   ;3ff2	cb 3f
	dec a                   ;3ff4	3d
	ret                     ;3ff5	c9
Zero3FF6:
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ;3ff6	00 00 00 00 00 00 00 00	........
	defb 0x00,0x00                               ;3ffe	00 00	..
