; z80dasm 1.2.0
; command line: z80dasm -a -t -o /tmp/voron1_catalog.asm /tmp/voron1_catalog.bin

	org 00100h

	nop			;0100	00		.
	ld bc,00023h		;0101	01 23 00	. # .
	jp pe,032afh		;0104	ea af 32	. . 2
	djnz $+95		;0107	10 5d		. ]
	ld de,(05cf4h)		;0109	ed 5b f4 5c	. [ . \
	ld c,005h		;010d	0e 05		. .
	call 03d13h		;010f	cd 13 3d	. . =
	ret			;0112	c9		.
	ld hl,0e5ceh		;0113	21 ce e5	! . .
	ld b,01bh		;0116	06 1b		. .
	call 05d40h		;0118	cd 40 5d	. @ ]
	ld hl,06000h		;011b	21 00 60	! . `
	ld b,05eh		;011e	06 5e		. ^
	call 05d40h		;0120	cd 40 5d	. @ ]
	jp 06000h		;0123	c3 00 60	. . `
	dec c			;0126	0d		.
	nop			;0127	00		.
	ld (bc),a		;0128	02		.
	dec d			;0129	15		.
	nop			;012a	00		.
	defb 0fdh,0b0h,022h ;illegal sequence	;012b	fd b0 22	. . "
	ld (03534h),a		;012e	32 34 35	2 4 5
	scf			;0131	37		7
	dec (hl)		;0132	35		5
	ld (0f93ah),hl		;0133	22 3a f9	" : .
	ret nz			;0136	c0		.
	or b			;0137	b0		.
	ld (03332h),hl		;0138	22 32 33	" 2 3
	jr c,$+58		;013b	38 38		8 8
	ld (hl),022h		;013d	36 22		6 "
	dec c			;013f	0d		.
	ld l,a			;0140	6f		o
	nop			;0141	00		.
	nop			;0142	00		.
	ld h,c			;0143	61		a
	sbc a,h			;0144	9c		.
	nop			;0145	00		.
	ld l,l			;0146	6d		m
	nop			;0147	00		.
	nop			;0148	00		.
	jr z,$+37		;0149	28 23		( #
	nop			;014b	00		.
	xor e			;014c	ab		.
	ld l,a			;014d	6f		o
	ld h,h			;014e	64		d
	ld sp,hl		;014f	f9		.
	nop			;0150	00		.
	nop			;0151	00		.
	ccf			;0152	3f		?
	ld e,l			;0153	5d		]
	nop			;0154	00		.
	jp (hl)			;0155	e9		.
	nop			;0156	00		.
	nop			;0157	00		.
	ld (00000h),hl		;0158	22 00 00	" . .
	nop			;015b	00		.
	nop			;015c	00		.
	ld hl,00000h		;015d	21 00 00	! . .
	nop			;0160	00		.
	nop			;0161	00		.
	ld bc,00000h		;0162	01 00 00	. . .
	jr c,$+2		;0165	38 00		8 .
	ld (bc),a		;0167	02		.
	and c			;0168	a1		.
	ld h,h			;0169	64		d
	ld (hl),d		;016a	72		r
	ld h,l			;016b	65		e
	ld (hl),e		;016c	73		s
	or c			;016d	b1		.
	nop			;016e	00		.
	nop			;016f	00		.
	ld b,b			;0170	40		@
	ld e,l			;0171	5d		]
	nop			;0172	00		.
	and c			;0173	a1		.
	ld h,h			;0174	64		d
	ld (hl),d		;0175	72		r
	ld h,l			;0176	65		e
	ld (hl),e		;0177	73		s
	or h			;0178	b4		.
	nop			;0179	00		.
	nop			;017a	00		.
	ld c,(hl)		;017b	4e		N
	ld e,l			;017c	5d		]
	nop			;017d	00		.
	and d			;017e	a2		.
	ld l,h			;017f	6c		l
	ld l,a			;0180	6f		o
	ex de,hl		;0181	eb		.
	nop			;0182	00		.
	nop			;0183	00		.
	ld (bc),a		;0184	02		.
	nop			;0185	00		.
	nop			;0186	00		.
	and c			;0187	a1		.
	ld h,h			;0188	64		d
	ld (hl),d		;0189	72		r
	ld h,l			;018a	65		e
	di			;018b	f3		.
	nop			;018c	00		.
	nop			;018d	00		.
	nop			;018e	00		.
	ld h,b			;018f	60		`
	nop			;0190	00		.
	and h			;0191	a4		.
	ld l,h			;0192	6c		l
	ld l,c			;0193	69		i
	ld l,(hl)		;0194	6e		n
	pop hl			;0195	e1		.
	nop			;0196	00		.
	nop			;0197	00		.
	sbc a,l			;0198	9d		.
	ld e,l			;0199	5d		]
	nop			;019a	00		.
	or e			;019b	b3		.
	ld h,l			;019c	65		e
	ld l,e			;019d	6b		k
	ld (hl),h		;019e	74		t
	ld l,a			;019f	6f		o
	jp p,00000h		;01a0	f2 00 00	. . .
	ld e,(hl)		;01a3	5e		^
	nop			;01a4	00		.
	nop			;01a5	00		.
	ld b,c			;01a6	41		A
	ld bc,06e00h		;01a7	01 00 6e	. . n
	and c			;01aa	a1		.
	ld h,h			;01ab	64		d
	ld (hl),d		;01ac	72		r
	ld h,l			;01ad	65		e
	ld (hl),e		;01ae	73		s
	or e			;01af	b3		.
	nop			;01b0	00		.
	nop			;01b1	00		.
	nop			;01b2	00		.
	ld h,b			;01b3	60		`
	nop			;01b4	00		.
	and h			;01b5	a4		.
	ld l,h			;01b6	6c		l
	ld l,c			;01b7	69		i
	ld l,(hl)		;01b8	6e		n
	ld h,c			;01b9	61		a
	or d			;01ba	b2		.
	nop			;01bb	00		.
	nop			;01bc	00		.
	ld hl,00000h		;01bd	21 00 00	! . .
	add a,b			;01c0	80		.
	xor d			;01c1	aa		.
	nop			;01c2	00		.
	nop			;01c3	00		.
	ld l,a			;01c4	6f		o
	ld l,a			;01c5	6f		o
	ld (hl),h		;01c6	74		t
	ld (0800dh),hl		;01c7	22 0d 80	" . .
	ld sp,03535h		;01ca	31 35 35	1 5 5
	dec (hl)		;01cd	35		5
	dec (hl)		;01ce	35		5
	ld c,000h		;01cf	0e 00		. .
	nop			;01d1	00		.
	jp 0003ch		;01d2	c3 3c 00	. < .
	dec c			;01d5	0d		.
	add a,b			;01d6	80		.
	dec c			;01d7	0d		.
	rst 38h			;01d8	ff		.
	ld e,(hl)		;01d9	5e		^
	inc b			;01da	04		.
	nop			;01db	00		.
	nop			;01dc	00		.
	nop			;01dd	00		.
	nop			;01de	00		.
	nop			;01df	00		.
	nop			;01e0	00		.
	nop			;01e1	00		.
	nop			;01e2	00		.
	nop			;01e3	00		.
	nop			;01e4	00		.
	nop			;01e5	00		.
	nop			;01e6	00		.
	nop			;01e7	00		.
	nop			;01e8	00		.
	nop			;01e9	00		.
	nop			;01ea	00		.
	nop			;01eb	00		.
	nop			;01ec	00		.
	nop			;01ed	00		.
	nop			;01ee	00		.
	nop			;01ef	00		.
	nop			;01f0	00		.
	nop			;01f1	00		.
	nop			;01f2	00		.
	nop			;01f3	00		.
	nop			;01f4	00		.
	nop			;01f5	00		.
	nop			;01f6	00		.
	nop			;01f7	00		.
	nop			;01f8	00		.
	nop			;01f9	00		.
	nop			;01fa	00		.
	nop			;01fb	00		.
	nop			;01fc	00		.
	nop			;01fd	00		.
	nop			;01fe	00		.
	nop			;01ff	00		.
	rst 38h			;0200	ff		.
	ld (hl),d		;0201	72		r
	ld e,l			;0202	5d		]
	nop			;0203	00		.
	dec b			;0204	05		.
	nop			;0205	00		.
	ld b,b			;0206	40		@
	dec b			;0207	05		.
	rrca			;0208	0f		.
	inc d			;0209	14		.
	rst 38h			;020a	ff		.
	ld h,l			;020b	65		e
	call p,00005h		;020c	f4 05 00	. . .
	ld de,01405h		;020f	11 05 14	. . .
	jr $+82			;0212	18 50		. P
	nop			;0214	00		.
	inc (hl)		;0215	34		4
	ld b,002h		;0216	06 02		. .
	dec b			;0218	05		.
	nop			;0219	00		.
	add a,d			;021a	82		.
	ld bc,0f0f1h		;021b	01 f1 f0	. . .
	rra			;021e	1f		.
	nop			;021f	00		.
	ccf			;0220	3f		?
	nop			;0221	00		.
	ret m			;0222	f8		.
	rra			;0223	1f		.
	ld a,000h		;0224	3e 00		> .
	sbc a,a			;0226	9f		.
	rlca			;0227	07		.
	rst 8			;0228	cf		.
	adc a,a			;0229	8f		.
	add a,e			;022a	83		.
	pop hl			;022b	e1		.
	add a,b			;022c	80		.
	ld b,l			;022d	45		E
	ld l,l			;022e	6d		m
	ld a,(03f1ch)		;022f	3a 1c 3f	: . ?
	add a,b			;0232	80		.
	inc e			;0233	1c		.
	ccf			;0234	3f		?
	ret m			;0235	f8		.
	inc (hl)		;0236	34		4
	ld bc,03e1fh		;0237	01 1f 3e	. . >
	ld a,030h		;023a	3e 30		> 0
	rra			;023c	1f		.
	rrca			;023d	0f		.
	adc a,a			;023e	8f		.
	rra			;023f	1f		.
	dec d			;0240	15		.
	adc a,d			;0241	8a		.
	and d			;0242	a2		.
	ld d,b			;0243	50		P
	scf			;0244	37		7
	ld bc,03940h		;0245	01 40 39	. @ 9
	ccf			;0248	3f		?
	ld d,l			;0249	55		U
	ret z			;024a	c8		.
	and b			;024b	a0		.
	add hl,bc		;024c	09		.
	rlca			;024d	07		.
	nop			;024e	00		.
	add a,c			;024f	81		.
	ret p			;0250	f0		.
	add hl,de		;0251	19		.
	ccf			;0252	3f		?
	nop			;0253	00		.
	ld a,0d8h		;0254	3e d8		> .
	call po,03f01h		;0256	e4 01 3f	. . ?
	djnz $+122		;0259	10 78		. x
	inc a			;025b	3c		<
	ld bc,0a1fbh		;025c	01 fb a1	. . .
	call po,07fc0h		;025f	e4 c0 7f	. . .
	ld d,b			;0262	50		P
	ld d,b			;0263	50		P
	ld h,a			;0264	67		g
	inc d			;0265	14		.
	inc e			;0266	1c		.
	inc a			;0267	3c		<
	add hl,bc		;0268	09		.
	sub b			;0269	90		.
	dec bc			;026a	0b		.
	inc bc			;026b	03		.
	ret nz			;026c	c0		.
	add hl,bc		;026d	09		.
	rst 38h			;026e	ff		.
	ret z			;026f	c8		.
	nop			;0270	00		.
	djnz $+9		;0271	10 07		. .
	push af			;0273	f5		.
	dec de			;0274	1b		.
	add a,l			;0275	85		.
	call m,0f000h		;0276	fc 00 f0	. . .
	ld a,a			;0279	7f		.
	ld l,e			;027a	6b		k
	pop hl			;027b	e1		.
	halt			;027c	76		v
	or (hl)			;027d	b6		.
	dec b			;027e	05		.
	ccf			;027f	3f		?
	add hl,de		;0280	19		.
	add a,b			;0281	80		.
	call po,008dfh		;0282	e4 df 08	. . .
	jr nc,$+65		;0285	30 3f		0 ?
	ld bc,01978h		;0287	01 78 19	. x .
	or d			;028a	b2		.
	dec d			;028b	15		.
	add a,a			;028c	87		.
	cp (hl)			;028d	be		.
	call m,0ff0fh		;028e	fc 0f ff	. . .
	ret pe			;0291	e8		.
	defb 0ddh,01ch,00bh ;illegal sequence	;0292	dd 1c 0b	. . .
	add a,l			;0295	85		.
	ld d,l			;0296	55		U
	dec b			;0297	05		.
	ld a,a			;0298	7f		.
	dec d			;0299	15		.
	rst 38h			;029a	ff		.
	jr z,$+106		;029b	28 68		( h
	ld a,(hl)		;029d	7e		~
	nop			;029e	00		.
	rst 8			;029f	cf		.
	sub a			;02a0	97		.
	ld de,0680ah		;02a1	11 0a 68	. . h
	jp z,00828h		;02a4	ca 28 08	. ( .
	and b			;02a7	a0		.
	ld b,002h		;02a8	06 02		. .
	ld a,(bc)		;02aa	0a		.
	or l			;02ab	b5		.
	dec d			;02ac	15		.
	nop			;02ad	00		.
	add a,b			;02ae	80		.
	inc bc			;02af	03		.
	ret po			;02b0	e0		.
	call c,01fffh		;02b1	dc ff 1f	. . .
	ret nc			;02b4	d0		.
	call c,0ff05h		;02b5	dc 05 ff	. . .
	jr c,$+65		;02b8	38 3f		8 ?
	jr $+126		;02ba	18 7c		. |
	ld a,h			;02bc	7c		|
	ld d,l			;02bd	55		U
	ld c,000h		;02be	0e 00		. .
	rst 38h			;02c0	ff		.
	cp 03eh			;02c1	fe 3e		. >
	djnz $+33		;02c3	10 1f		. .
	ld e,039h		;02c5	1e 39		. 9
	dec d			;02c7	15		.
	ld b,l			;02c8	45		E
	scf			;02c9	37		7
	ld (bc),a		;02ca	02		.
	add a,b			;02cb	80		.
	add hl,sp		;02cc	39		9
	ld a,(hl)		;02cd	7e		~
	xor d			;02ce	aa		.
	add hl,bc		;02cf	09		.
	bit 0,c			;02d0	cb 41		. A
	rlca			;02d2	07		.
	nop			;02d3	00		.
	add a,e			;02d4	83		.
	ret po			;02d5	e0		.
	add hl,sp		;02d6	39		9
	rst 38h			;02d7	ff		.
	inc bc			;02d8	03		.
	adc a,09eh		;02d9	ce 9e		. .
	rst 38h			;02db	ff		.
	cp c			;02dc	b9		.
	rlca			;02dd	07		.
	ex (sp),hl		;02de	e3		.
	inc c			;02df	0c		.
	ld b,l			;02e0	45		E
	ld c,b			;02e1	48		H
	jr z,$-94		;02e2	28 a0		( .
	and b			;02e4	a0		.
	ld h,a			;02e5	67		g
	jr z,$+32		;02e6	28 1e		( .
	inc de			;02e8	13		.
	ld bc,00d0ah		;02e9	01 0a 0d	. . .
	call p,0ff57h		;02ec	f4 57 ff	. W .
	push hl			;02ef	e5		.
	nop			;02f0	00		.
	inc c			;02f1	0c		.
	nop			;02f2	00		.
	ld a,a			;02f3	7f		.
	ld a,(hl)		;02f4	7e		~
	ld b,e			;02f5	43		C
	add a,0c0h		;02f6	c6 c0		. .
	djnz $-30		;02f8	10 e0		. .
	ld a,a			;02fa	7f		.
	dec de			;02fb	1b		.
	xor h			;02fc	ac		.
	ld l,e			;02fd	6b		k
	pop af			;02fe	f1		.
	nop			;02ff	00		.
	xor e			;0300	ab		.
	ld d,h			;0301	54		T
	ld a,d			;0302	7a		z
	ret po			;0303	e0		.
	rst 38h			;0304	ff		.
	ld b,l			;0305	45		E
	ccf			;0306	3f		?
	sub b			;0307	90		.
	ld d,c			;0308	51		Q
	ld (bc),a		;0309	02		.
	cp b			;030a	b8		.
	ld c,l			;030b	4d		M
	ld bc,01d5fh		;030c	01 5f 1d	. _ .
	ret p			;030f	f0		.
	inc bc			;0310	03		.
	rst 38h			;0311	ff		.
	ld e,c			;0312	59		Y
	sub c			;0313	91		.
	pop af			;0314	f1		.
	ex (sp),hl		;0315	e3		.
	add a,a			;0316	87		.
	ld a,(bc)		;0317	0a		.
	xor d			;0318	aa		.
	ld bc,01254h		;0319	01 54 12	. T .
	ld a,a			;031c	7f		.
	inc d			;031d	14		.
	ld d,b			;031e	50		P
	dec d			;031f	15		.
	ld bc,05455h		;0320	01 55 54	. U T
	inc bc			;0323	03		.
	cp d			;0324	ba		.
	xor d			;0325	aa		.
	ccf			;0326	3f		?
	nop			;0327	00		.
	dec b			;0328	05		.
	dec de			;0329	1b		.
	ld c,l			;032a	4d		M
	ld (de),a		;032b	12		.
	rst 18h			;032c	df		.
	ld b,015h		;032d	06 15		. .
	ld d,l			;032f	55		U
	add hl,de		;0330	19		.
	ld d,h			;0331	54		T
	rst 30h			;0332	f7		.
	rlca			;0333	07		.
	rst 38h			;0334	ff		.
	rra			;0335	1f		.
	dec l			;0336	2d		-
	dec (hl)		;0337	35		5
	ld e,(hl)		;0338	5e		^
	ld d,l			;0339	55		U
	ld d,l			;033a	55		U
	inc (hl)		;033b	34		4
	add hl,bc		;033c	09		.
	nop			;033d	00		.
	ld b,0ffh		;033e	06 ff		. .
	ld d,b			;0340	50		P
	xor b			;0341	a8		.
	xor b			;0342	a8		.
	jr nz,$+86		;0343	20 54		  T
	ld d,l			;0345	55		U
	ld d,l			;0346	55		U
	inc bc			;0347	03		.
	nop			;0348	00		.
	ld a,b			;0349	78		x
	inc (hl)		;034a	34		4
	dec d			;034b	15		.
	ld bc,00550h		;034c	01 50 05	. P .
	nop			;034f	00		.
	ccf			;0350	3f		?
	inc e			;0351	1c		.
	ld (bc),a		;0352	02		.
	xor d			;0353	aa		.
	ld a,d			;0354	7a		z
	rst 38h			;0355	ff		.
	call m,00f1fh		;0356	fc 1f 0f	. . .
	add hl,sp		;0359	39		9
	ld c,h			;035a	4c		L
	ret nz			;035b	c0		.
	jr nz,$+1		;035c	20 ff		  .
	cp 043h			;035e	fe 43		. C
	sub b			;0360	90		.
	out (007h),a		;0361	d3 07		. .
	rst 38h			;0363	ff		.
	ld a,c			;0364	79		y
	rst 38h			;0365	ff		.
	rlca			;0366	07		.
	sbc a,a			;0367	9f		.
	rrca			;0368	0f		.
	call nz,08f06h		;0369	c4 06 8f	. . .
	add a,a			;036c	87		.
	add a,e			;036d	83		.
	pop bc			;036e	c1		.
	inc b			;036f	04		.
	ld e,07fh		;0370	1e 7f		. .
	rrca			;0372	0f		.
	rst 0			;0373	c7		.
	add a,l			;0374	85		.
	xor c			;0375	a9		.
	jp z,0497fh		;0376	ca 7f 49	. . I
	ld d,h			;0379	54		T
	ld l,001h		;037a	2e 01		. .
	rrca			;037c	0f		.
	sub l			;037d	95		.
	add a,b			;037e	80		.
	dec d			;037f	15		.
	ld a,a			;0380	7f		.
	dec d			;0381	15		.
	rst 38h			;0382	ff		.
	add a,l			;0383	85		.
	ld hl,(00328h)		;0384	2a 28 03	* ( .
	ld bc,000c5h		;0387	01 c5 00	. . .
	ld hl,072f0h		;038a	21 f0 72	! . r
	ld c,l			;038d	4d		M
	dec e			;038e	1d		.
	adc a,c			;038f	89		.
	ld bc,06cbfh		;0390	01 bf 6c	. . l
	bit 6,b			;0393	cb 70		. p
	and (hl)		;0395	a6		.
	ld hl,082ffh		;0396	21 ff 82	! . .
	scf			;0399	37		7
	sub b			;039a	90		.
	and (hl)		;039b	a6		.
	djnz $-9		;039c	10 f5		. .
	ld b,07fh		;039e	06 7f		. .
	sbc a,b			;03a0	98		.
	or d			;03a1	b2		.
	ccf			;03a2	3f		?
	dec b			;03a3	05		.
	jr c,$+33		;03a4	38 1f		8 .
	ld (bc),a		;03a6	02		.
	sub h			;03a7	94		.
	ld (0ff1dh),hl		;03a8	22 1d ff	" . .
	ret po			;03ab	e0		.
	ld bc,0208ah		;03ac	01 8a 20	. .  
	ld a,(de)		;03af	1a		.
	call nz,08f56h		;03b0	c4 56 8f	. V .
	nop			;03b3	00		.
	ld a,(de)		;03b4	1a		.
	ld a,a			;03b5	7f		.
	jr z,$-94		;03b6	28 a0		( .
	ld hl,(09501h)		;03b8	2a 01 95	* . .
	ld bc,0a8aah		;03bb	01 aa a8	. . .
	inc bc			;03be	03		.
	dec hl			;03bf	2b		+
	nop			;03c0	00		.
	ret nc			;03c1	d0		.
	ld d,l			;03c2	55		U
	dec b			;03c3	05		.
	rst 38h			;03c4	ff		.
	ld b,h			;03c5	44		D
	sub d			;03c6	92		.
	rst 18h			;03c7	df		.
	ld b,02ah		;03c8	06 2a		. *
	xor d			;03ca	aa		.
	add hl,de		;03cb	19		.
	add a,e			;03cc	83		.
	rst 38h			;03cd	ff		.
	ld sp,hl		;03ce	f9		.
	ld a,(bc)		;03cf	0a		.
	push de			;03d0	d5		.
	sbc a,a			;03d1	9f		.
	inc (hl)		;03d2	34		4
	dec a			;03d3	3d		=
	sbc a,a			;03d4	9f		.
	rst 38h			;03d5	ff		.
	rrca			;03d6	0f		.
	ld d,h			;03d7	54		T
	ld (hl),e		;03d8	73		s
	inc c			;03d9	0c		.
	add a,c			;03da	81		.
	rra			;03db	1f		.
	dec l			;03dc	2d		-
	dec (hl)		;03dd	35		5
	ex af,af'		;03de	08		.
	ld d,l			;03df	55		U
	xor d			;03e0	aa		.
	ld l,b			;03e1	68		h
	add hl,bc		;03e2	09		.
	nop			;03e3	00		.
	add a,b			;03e4	80		.
	rlca			;03e5	07		.
	ret nz			;03e6	c0		.
	and c			;03e7	a1		.
	rla			;03e8	17		.
	rst 18h			;03e9	df		.
	ld (bc),a		;03ea	02		.
	rst 38h			;03eb	ff		.
	jr nz,$-86		;03ec	20 a8		  .
	xor d			;03ee	aa		.
	xor d			;03ef	aa		.
	inc de			;03f0	13		.
	ld b,b			;03f1	40		@
	inc bc			;03f2	03		.
	nop			;03f3	00		.
	ret p			;03f4	f0		.
	ld (bc),a		;03f5	02		.
	and b			;03f6	a0		.
	ld a,(bc)		;03f7	0a		.
	nop			;03f8	00		.
	xor d			;03f9	aa		.
	add a,b			;03fa	80		.
	inc e			;03fb	1c		.
	cp e			;03fc	bb		.
	ld (bc),a		;03fd	02		.
	ld (bc),a		;03fe	02		.
	rst 18h			;03ff	df		.
	nop			;0400	00		.
	dec d			;0401	15		.
	rra			;0402	1f		.
	rlca			;0403	07		.
	add a,b			;0404	80		.
	and b			;0405	a0		.
	jr nz,$+1		;0406	20 ff		  .
	dec bc			;0408	0b		.
	and 099h		;0409	e6 99		. .
	call m,00743h		;040b	fc 43 07	. C .
	ld e,a			;040e	5f		_
	ld b,b			;040f	40		@
	or e			;0410	b3		.
	rra			;0411	1f		.
	ld e,a			;0412	5f		_
	rst 38h			;0413	ff		.
	rst 0			;0414	c7		.
	pop bc			;0415	c1		.
	ld (bc),a		;0416	02		.
	rrca			;0417	0f		.
	sbc a,c			;0418	99		.
	ret			;0419	c9		.
	adc a,d			;041a	8a		.
	ld a,a			;041b	7f		.
	ccf			;041c	3f		?
	rrca			;041d	0f		.
	ld a,a			;041e	7f		.
	inc d			;041f	14		.
	ld d,l			;0420	55		U
	ld c,c			;0421	49		I
	xor b			;0422	a8		.
	ld l,001h		;0423	2e 01		. .
	rrca			;0425	0f		.
	add a,h			;0426	84		.
	di			;0427	f3		.
	ld b,b			;0428	40		@
	and l			;0429	a5		.
	rst 38h			;042a	ff		.
	jp nz,02405h		;042b	c2 05 24	. . $
	xor c			;042e	a9		.
	ld h,h			;042f	64		d
	dec (hl)		;0430	35		5
	ret po			;0431	e0		.
	sbc a,(hl)		;0432	9e		.
	jp 01de0h		;0433	c3 e0 1d	. . .
	ld e,c			;0436	59		Y
	xor (hl)		;0437	ae		.
	adc a,c			;0438	89		.
	ld bc,0f0bfh		;0439	01 bf f0	. . .
	or l			;043c	b5		.
	ld l,l			;043d	6d		m
	rst 38h			;043e	ff		.
	ld b,a			;043f	47		G
	rst 38h			;0440	ff		.
	push hl			;0441	e5		.
	rst 38h			;0442	ff		.
	jp pe,05676h		;0443	ea 76 56	. v V
	ld a,a			;0446	7f		.
	pop af			;0447	f1		.
	ld a,a			;0448	7f		.
	ccf			;0449	3f		?
	pop bc			;044a	c1		.
	add a,e			;044b	83		.
	ld a,(bc)		;044c	0a		.
	rst 38h			;044d	ff		.
	dec d			;044e	15		.
	and b			;044f	a0		.
	dec e			;0450	1d		.
	sbc a,(hl)		;0451	9e		.
	ld a,a			;0452	7f		.
	cp 054h			;0453	fe 54		. T
	nop			;0455	00		.
	ld bc,0000eh		;0456	01 0e 00	. . .
	ld a,(de)		;0459	1a		.
	inc b			;045a	04		.
	cp (hl)			;045b	be		.
	ld a,a			;045c	7f		.
	rra			;045d	1f		.
	add hl,bc		;045e	09		.
	inc h			;045f	24		$
	ld h,02fh		;0460	26 2f		& /
	jr nz,$+126		;0462	20 7c		  |
	ld b,014h		;0464	06 14		. .
	rst 38h			;0466	ff		.
	in a,(065h)		;0467	db 65		. e
	rst 18h			;0469	df		.
	rrca			;046a	0f		.
	inc d			;046b	14		.
	rst 38h			;046c	ff		.
	cp 0a8h			;046d	fe a8		. .
	exx			;046f	d9		.
	sbc a,a			;0470	9f		.
	rra			;0471	1f		.
	add hl,de		;0472	19		.
	add a,d			;0473	82		.
	ld (de),a		;0474	12		.
	dec sp			;0475	3b		;
	ld h,085h		;0476	26 85		& .
	dec (hl)		;0478	35		5
	dec h			;0479	25		%
	sbc a,h			;047a	9c		.
	ld d,l			;047b	55		U
	rst 38h			;047c	ff		.
	rst 18h			;047d	df		.
	ld l,d			;047e	6a		j
	ld (hl),h		;047f	74		t
	rst 38h			;0480	ff		.
	rst 18h			;0481	df		.
	sbc a,l			;0482	9d		.
	ld b,070h		;0483	06 70		. p
	ld c,(hl)		;0485	4e		N
	rst 18h			;0486	df		.
	ld bc,005f0h		;0487	01 f0 05	. . .
	ld d,(hl)		;048a	56		V
	dec b			;048b	05		.
	rst 18h			;048c	df		.
	rst 38h			;048d	ff		.
	ex af,af'		;048e	08		.
	ld h,h			;048f	64		d
	add a,a			;0490	87		.
	rst 18h			;0491	df		.
	rst 38h			;0492	ff		.
	add a,e			;0493	83		.
	add a,b			;0494	80		.
	ld (bc),a		;0495	02		.
	ld a,(de)		;0496	1a		.
	rra			;0497	1f		.
	rla			;0498	17		.
	dec b			;0499	05		.
	ld d,a			;049a	57		W
	ret m			;049b	f8		.
	ld d,b			;049c	50		P
	dec b			;049d	05		.
	ld d,l			;049e	55		U
	ret p			;049f	f0		.
	dec b			;04a0	05		.
	sbc a,a			;04a1	9f		.
	ld (bc),a		;04a2	02		.
	rst 38h			;04a3	ff		.
	ld c,l			;04a4	4d		M
	ld b,05fh		;04a5	06 5f		. _
	ld bc,0e507h		;04a7	01 07 e5	. . .
	ld a,a			;04aa	7f		.
	call m,00d1eh		;04ab	fc 1e 0d	. . .
	call p,0df7fh		;04ae	f4 7f df	. . .
	rlca			;04b1	07		.
	add a,h			;04b2	84		.
	adc a,h			;04b3	8c		.
	inc d			;04b4	14		.
	ld a,h			;04b5	7c		|
	ex af,af'		;04b6	08		.
	ret nz			;04b7	c0		.
	ex af,af'		;04b8	08		.
	nop			;04b9	00		.
	ld e,003h		;04ba	1e 03		. .
	ld (de),a		;04bc	12		.
	ld e,087h		;04bd	1e 87		. .
	sub h			;04bf	94		.
	call m,0c254h		;04c0	fc 54 c2	. T .
	jp nz,0001fh		;04c3	c2 1f 00	. . .
	dec l			;04c6	2d		-
	dec hl			;04c7	2b		+
	rst 18h			;04c8	df		.
	nop			;04c9	00		.
	ld b,b			;04ca	40		@
	nop			;04cb	00		.
	ret m			;04cc	f8		.
	dec c			;04cd	0d		.
	inc d			;04ce	14		.
	ld a,(bc)		;04cf	0a		.
	call p,005dfh		;04d0	f4 df 05	. . .
	defb 0edh ;next byte illegal after ed	;04d3	ed		.
	ld a,a			;04d4	7f		.
	ccf			;04d5	3f		?
	dec b			;04d6	05		.
	rra			;04d7	1f		.
	add hl,bc		;04d8	09		.
	jp z,0bec2h		;04d9	ca c2 be	. . .
	ld l,b			;04dc	68		h
	adc a,b			;04dd	88		.
	rst 28h			;04de	ef		.
	ld h,b			;04df	60		`
	rst 38h			;04e0	ff		.
	sbc a,a			;04e1	9f		.
	ld a,a			;04e2	7f		.
	add hl,bc		;04e3	09		.
	add a,c			;04e4	81		.
	rra			;04e5	1f		.
	ld h,055h		;04e6	26 55		& U
	ld (bc),a		;04e8	02		.
	cpl			;04e9	2f		/
	ld b,028h		;04ea	06 28		. (
	cp (hl)			;04ec	be		.
	ld b,b			;04ed	40		@
	jp po,0b7dfh		;04ee	e2 df b7	. . .
	sub c			;04f1	91		.
	ld e,a			;04f2	5f		_
	rrca			;04f3	0f		.
	jr z,$-123		;04f4	28 83		( .
	ret nz			;04f6	c0		.
	sbc a,a			;04f7	9f		.
	ld a,06ah		;04f8	3e 6a		> j
	add a,(hl)		;04fa	86		.
	sbc a,a			;04fb	9f		.
	rra			;04fc	1f		.
	add hl,de		;04fd	19		.
	djnz $+7		;04fe	10 05		. .
	dec sp			;0500	3b		;
	ld b,029h		;0501	06 29		. )
	ld a,(bc)		;0503	0a		.
	ld c,a			;0504	4f		O
	ld b,b			;0505	40		@
	dec (hl)		;0506	35		5
	xor d			;0507	aa		.
	add a,b			;0508	80		.
	rrca			;0509	0f		.
	add a,b			;050a	80		.
	rra			;050b	1f		.
	ret p			;050c	f0		.
	ld a,h			;050d	7c		|
	rst 38h			;050e	ff		.
	xor d			;050f	aa		.
	ld c,d			;0510	4a		J
	rst 18h			;0511	df		.
	ret p			;0512	f0		.
	dec de			;0513	1b		.
	inc c			;0514	0c		.
	jp nz,0df37h		;0515	c2 37 df	. 7 .
	inc bc			;0518	03		.
	ret po			;0519	e0		.
	ld a,(bc)		;051a	0a		.
	call po,0df02h		;051b	e4 02 df	. . .
	add a,b			;051e	80		.
	ret m			;051f	f8		.
	inc b			;0520	04		.
	rst 38h			;0521	ff		.
	ld b,e			;0522	43		C
	ret nz			;0523	c0		.
	ex af,af'		;0524	08		.
	ld a,(de)		;0525	1a		.
	rra			;0526	1f		.
	rla			;0527	17		.
	ld a,(bc)		;0528	0a		.
	xor a			;0529	af		.
	ret p			;052a	f0		.
	and b			;052b	a0		.
	ld (hl),048h		;052c	36 48		6 H
	add a,e			;052e	83		.
	rra			;052f	1f		.
	ld b,046h		;0530	06 46		. F
	ld b,d			;0532	42		B
	ld e,a			;0533	5f		_
	add a,e			;0534	83		.
	cp 07fh			;0535	fe 7f		. .
	rlca			;0537	07		.
	ret m			;0538	f8		.
	inc e			;0539	1c		.
	ld b,0fah		;053a	06 fa		. .
	ld a,a			;053c	7f		.
	rst 18h			;053d	df		.
	dec bc			;053e	0b		.
	jp nz,0d2aah		;053f	c2 aa d2	. . .
	inc e			;0542	1c		.
	ld de,0d4c0h		;0543	11 c0 d4	. . .
	ld (0180fh),hl		;0546	22 0f 18	" . .
	ld h,d			;0549	62		b
	inc bc			;054a	03		.
	pop af			;054b	f1		.
	rlca			;054c	07		.
	call m,00ea8h		;054d	fc a8 0e	. . .
	ld a,(bc)		;0550	0a		.
	rra			;0551	1f		.
	nop			;0552	00		.
	rst 18h			;0553	df		.
	sub (hl)		;0554	96		.
	ld c,01dh		;0555	0e 1d		. .
	pop hl			;0557	e1		.
	sub b			;0558	90		.
	ld (de),a		;0559	12		.
	ret			;055a	c9		.
	rst 38h			;055b	ff		.
	ret pe			;055c	e8		.
	or h			;055d	b4		.
	scf			;055e	37		7
	rst 18h			;055f	df		.
	ld a,a			;0560	7f		.
	ccf			;0561	3f		?
	ld a,(bc)		;0562	0a		.
	xor c			;0563	a9		.
	ret pe			;0564	e8		.
	rst 38h			;0565	ff		.
	dec e			;0566	1d		.
	cp 01ah			;0567	fe 1a		. .
	rra			;0569	1f		.
	add a,e			;056a	83		.
	inc b			;056b	04		.
	ld hl,01e01h		;056c	21 01 1e	! . .
	sbc a,a			;056f	9f		.
	inc b			;0570	04		.
	cp h			;0571	bc		.
	ld a,a			;0572	7f		.
	rra			;0573	1f		.
	ld bc,02380h		;0574	01 80 23	. . #
	add hl,bc		;0577	09		.
	ld d,c			;0578	51		Q
	rst 38h			;0579	ff		.
	ld c,0ach		;057a	0e ac		. .
	dec b			;057c	05		.
	rst 38h			;057d	ff		.
	ld (hl),a		;057e	77		w
	add a,(hl)		;057f	86		.
	jr $-31			;0580	18 df		. .
	dec d			;0582	15		.
	ld d,h			;0583	54		T
	ld e,l			;0584	5d		]
	scf			;0585	37		7
	rst 18h			;0586	df		.
	add a,e			;0587	83		.
	add a,b			;0588	80		.
	rst 38h			;0589	ff		.
	rst 18h			;058a	df		.
	jr c,$+49		;058b	38 2f		8 /
	sbc a,a			;058d	9f		.
	and l			;058e	a5		.
	dec c			;058f	0d		.
	ld h,a			;0590	67		g
	ld h,007h		;0591	26 07		& .
	rst 18h			;0593	df		.
	add a,l			;0594	85		.
	jp c,03fffh		;0595	da ff 3f	. . ?
	dec h			;0598	25		%
	rst 38h			;0599	ff		.
	rrca			;059a	0f		.
	rst 38h			;059b	ff		.
	nop			;059c	00		.
	daa			;059d	27		'
	rra			;059e	1f		.
	ld a,b			;059f	78		x
	add hl,sp		;05a0	39		9
	rst 18h			;05a1	df		.
	ld d,h			;05a2	54		T
	xor (hl)		;05a3	ae		.
	ld l,c			;05a4	69		i
	rlca			;05a5	07		.
	ret nc			;05a6	d0		.
	ld e,a			;05a7	5f		_
	ld b,0ffh		;05a8	06 ff		. .
	add a,c			;05aa	81		.
	pop de			;05ab	d1		.
	ld hl,086beh		;05ac	21 be 86	! . .
	defb 0ddh,05ch ;ld e,ixh	;05af	dd 5c		. \
	push bc			;05b1	c5		.
	ccf			;05b2	3f		?
	ld b,0ffh		;05b3	06 ff		. .
	ld b,b			;05b5	40		@
	cp a			;05b6	bf		.
	ld hl,000e2h		;05b7	21 e2 00	! . .
	ld a,a			;05ba	7f		.
	ret po			;05bb	e0		.
	inc a			;05bc	3c		<
	ld a,a			;05bd	7f		.
	ld c,e			;05be	4b		K
	ld a,c			;05bf	79		y
	dec d			;05c0	15		.
	rst 38h			;05c1	ff		.
	daa			;05c2	27		'
	pop bc			;05c3	c1		.
	jp po,00a3eh		;05c4	e2 3e 0a	. > .
	dec b			;05c7	05		.
	call m,00f02h		;05c8	fc 02 0f	. . .
	ret po			;05cb	e0		.
	ld l,l			;05cc	6d		m
	rst 18h			;05cd	df		.
	cp a			;05ce	bf		.
	ld a,b			;05cf	78		x
	ld a,(002ffh)		;05d0	3a ff 02	: . .
	and c			;05d3	a1		.
	jp nc,0de41h		;05d4	d2 41 de	. A .
	nop			;05d7	00		.
	ld b,e			;05d8	43		C
	call nc,07ccah		;05d9	d4 ca 7c	. . |
	rst 18h			;05dc	df		.
	rra			;05dd	1f		.
	ld (bc),a		;05de	02		.
	add a,c			;05df	81		.
	out (09eh),a		;05e0	d3 9e		. .
	scf			;05e2	37		7
	ld h,b			;05e3	60		`
	rlca			;05e4	07		.
	rst 38h			;05e5	ff		.
	jp p,00220h		;05e6	f2 20 02	.   .
	pop hl			;05e9	e1		.
	and a			;05ea	a7		.
	sbc a,e			;05eb	9b		.
	inc hl			;05ec	23		#
	add a,h			;05ed	84		.
	rra			;05ee	1f		.
	ld hl,(002a0h)		;05ef	2a a0 02	* . .
	add hl,bc		;05f2	09		.
	and d			;05f3	a2		.
	xor h			;05f4	ac		.
	add a,b			;05f5	80		.
	rst 38h			;05f6	ff		.
	ld a,(bc)		;05f7	0a		.
	sub l			;05f8	95		.
	jr $+16			;05f9	18 0e		. .
	ld (hl),a		;05fb	77		w
	rst 18h			;05fc	df		.
	ld hl,(08678h)		;05fd	2a 78 86	* x .
	xor b			;0600	a8		.
	rst 18h			;0601	df		.
	add a,e			;0602	83		.
	ld a,0fdh		;0603	3e fd		> .
	rst 18h			;0605	df		.
	sbc a,a			;0606	9f		.
	xor c			;0607	a9		.
	pop hl			;0608	e1		.
	dec (hl)		;0609	35		5
	or a			;060a	b7		.
	nop			;060b	00		.
	ld c,055h		;060c	0e 55		. U
	jp 01cdfh		;060e	c3 df 1c	. . .
	add a,b			;0611	80		.
	out (003h),a		;0612	d3 03		. .
	rst 38h			;0614	ff		.
	inc a			;0615	3c		<
	add hl,sp		;0616	39		9
	rst 18h			;0617	df		.
	ld hl,0a869h		;0618	21 69 a8	! i .
	rra			;061b	1f		.
	ld e,a			;061c	5f		_
	jp p,0dfddh		;061d	f2 dd df	. . .
	add hl,bc		;0620	09		.
	rst 38h			;0621	ff		.
	ccf			;0622	3f		?
	ld a,c			;0623	79		y
	and 020h		;0624	e6 20		.  
	rst 30h			;0626	f7		.
	pop hl			;0627	e1		.
	inc b			;0628	04		.
	or h			;0629	b4		.
	ld a,a			;062a	7f		.
	rlca			;062b	07		.
	ld e,a			;062c	5f		_
	jr nz,$+65		;062d	20 3f		  ?
	cp 07eh			;062f	fe 7e		. ~
	inc bc			;0631	03		.
	and e			;0632	a3		.
	add a,b			;0633	80		.
	ld a,b			;0634	78		x
	ld a,a			;0635	7f		.
	sub (hl)		;0636	96		.
	call pe,076ffh		;0637	ec ff 76	. . v
	ld bc,01f4fh		;063a	01 4f 1f	. O .
	ld sp,hl		;063d	f9		.
	ld c,e			;063e	4b		K
	ret c			;063f	d8		.
	jp nc,00404h		;0640	d2 04 04	. . .
	ld l,b			;0643	68		h
	jp 0bf01h		;0644	c3 01 bf	. . .
	rst 18h			;0647	df		.
	sub a			;0648	97		.
	push hl			;0649	e5		.
	dec e			;064a	1d		.
	nop			;064b	00		.
	sub h			;064c	94		.
	rst 38h			;064d	ff		.
	xor b			;064e	a8		.
	call p,0df49h		;064f	f4 49 df	. I .
	inc (hl)		;0652	34		4
	jr c,$+33		;0653	38 1f		8 .
	dec e			;0655	1d		.
	rst 38h			;0656	ff		.
	rst 38h			;0657	ff		.
	dec bc			;0658	0b		.
	ret m			;0659	f8		.
	inc bc			;065a	03		.
	rst 38h			;065b	ff		.
	defb 0fdh,050h,000h ;illegal sequence	;065c	fd 50 00	. P .
	rst 38h			;065f	ff		.
	rra			;0660	1f		.
	add a,h			;0661	84		.
	add a,h			;0662	84		.
	ld e,e			;0663	5b		[
	ld a,c			;0664	79		y
	ld d,b			;0665	50		P
	inc a			;0666	3c		<
	inc bc			;0667	03		.
	ld hl,0feffh		;0668	21 ff fe	! . .
	ex af,af'		;066b	08		.
	ld (bc),a		;066c	02		.
	and b			;066d	a0		.
	ld c,03eh		;066e	0e 3e		. >
	ccf			;0670	3f		?
	add hl,hl		;0671	29		)
	jp (hl)			;0672	e9		.
	ld a,h			;0673	7c		|
	ld e,e			;0674	5b		[
	rrca			;0675	0f		.
	ret p			;0676	f0		.
	daa			;0677	27		'
	ld d,h			;0678	54		T
	ex (sp),hl		;0679	e3		.
	pop af			;067a	f1		.
	rst 38h			;067b	ff		.
	ret p			;067c	f0		.
	add a,b			;067d	80		.
	inc d			;067e	14		.
	ld e,h			;067f	5c		\
	pop hl			;0680	e1		.
	or d			;0681	b2		.
	ld d,010h		;0682	16 10		. .
	sub h			;0684	94		.
	inc a			;0685	3c		<
	rst 38h			;0686	ff		.
	ret nz			;0687	c0		.
	add hl,de		;0688	19		.
	nop			;0689	00		.
	inc e			;068a	1c		.
	inc bc			;068b	03		.
	ret po			;068c	e0		.
	sbc a,l			;068d	9d		.
	or b			;068e	b0		.
	xor e			;068f	ab		.
	and (hl)		;0690	a6		.
	adc a,(hl)		;0691	8e		.
	xor d			;0692	aa		.
	ccf			;0693	3f		?
	ld a,a			;0694	7f		.
	call 008fah		;0695	cd fa 08	. . .
	adc a,e			;0698	8b		.
	ld c,h			;0699	4c		L
	dec d			;069a	15		.
	ld a,d			;069b	7a		z
	add a,b			;069c	80		.
	dec hl			;069d	2b		+
	ret po			;069e	e0		.
	inc c			;069f	0c		.
	ld b,b			;06a0	40		@
	cp (hl)			;06a1	be		.
	rst 38h			;06a2	ff		.
	ret pe			;06a3	e8		.
	adc a,b			;06a4	88		.
	dec b			;06a5	05		.
	dec d			;06a6	15		.
	ld b,l			;06a7	45		E
	jp (hl)			;06a8	e9		.
	ld l,027h		;06a9	2e 27		. '
	inc h			;06ab	24		$
	ld d,d			;06ac	52		R
	jr z,$-117		;06ad	28 89		( .
	ld b,d			;06af	42		B
	jr nz,$-14		;06b0	20 f0		  .
	ld bc,05bf8h		;06b2	01 f8 5b	. . [
	ret po			;06b5	e0		.
	ld b,04fh		;06b6	06 4f		. O
	sub l			;06b8	95		.
	ei			;06b9	fb		.
	ld e,08ah		;06ba	1e 8a		. .
	rlca			;06bc	07		.
	cpl			;06bd	2f		/
	ld hl,(07fe3h)		;06be	2a e3 7f	* . .
	ld d,b			;06c1	50		P
	ld a,018h		;06c2	3e 18		> .
	ld b,c			;06c4	41		A
	add a,b			;06c5	80		.
	xor b			;06c6	a8		.
	exx			;06c7	d9		.
	inc h			;06c8	24		$
	push bc			;06c9	c5		.
	ret pe			;06ca	e8		.
	adc a,h			;06cb	8c		.
	adc a,b			;06cc	88		.
	dec hl			;06cd	2b		+
	ld (02fc0h),hl		;06ce	22 c0 2f	" . /
	add hl,sp		;06d1	39		9
	jp po,06c24h		;06d2	e2 24 6c	. $ l
	ret p			;06d5	f0		.
	ld c,(hl)		;06d6	4e		N
	ld h,d			;06d7	62		b
	ret m			;06d8	f8		.
	adc a,e			;06d9	8b		.
	ld e,h			;06da	5c		\
	ld (hl),c		;06db	71		q
	dec d			;06dc	15		.
	and d			;06dd	a2		.
	rlca			;06de	07		.
	dec c			;06df	0d		.
	nop			;06e0	00		.
	ccf			;06e1	3f		?
	inc d			;06e2	14		.
	ld d,a			;06e3	57		W
	djnz $-38		;06e4	10 d8		. .
	ld h,062h		;06e6	26 62		& b
	rst 38h			;06e8	ff		.
	add a,e			;06e9	83		.
	ld e,08ah		;06ea	1e 8a		. .
	ld de,0400bh		;06ec	11 0b 40	. . @
	rra			;06ef	1f		.
	inc bc			;06f0	03		.
	ld (hl),l		;06f1	75		u
	jp po,05100h		;06f2	e2 00 51	. . Q
	add a,e			;06f5	83		.
	jp z,007ffh		;06f6	ca ff 07	. . .
	xor (hl)		;06f9	ae		.
	ld a,a			;06fa	7f		.
	rst 38h			;06fb	ff		.
	sub l			;06fc	95		.
	ld (bc),a		;06fd	02		.
	jp m,0bb50h		;06fe	fa 50 bb	. P .
	ld (bc),a		;0701	02		.
	rlca			;0702	07		.
	ccf			;0703	3f		?
	add hl,hl		;0704	29		)
	ret m			;0705	f8		.
	dec a			;0706	3d		=
	ld c,d			;0707	4a		J
	ld de,0783dh		;0708	11 3d 78	. = x
	cp (hl)			;070b	be		.
	ret nz			;070c	c0		.
	jp m,00220h		;070d	fa 20 02	.   .
	ld sp,hl		;0710	f9		.
	ld d,b			;0711	50		P
	dec hl			;0712	2b		+
	ld (01b55h),hl		;0713	22 55 1b	" U .
	rra			;0716	1f		.
	call m,05401h		;0717	fc 01 54	. . T
	inc a			;071a	3c		<
	rst 30h			;071b	f7		.
	rst 38h			;071c	ff		.
	cp d			;071d	ba		.
	inc c			;071e	0c		.
	ld hl,0010eh		;071f	21 0e 01	! . .
	add a,b			;0722	80		.
	pop hl			;0723	e1		.
	ld d,l			;0724	55		U
	jr c,$+38		;0725	38 24		8 $
	ld a,c			;0727	79		y
	jr z,$-87		;0728	28 a7		( .
	ld (hl),d		;072a	72		r
	ret po			;072b	e0		.
	dec c			;072c	0d		.
	inc de			;072d	13		.
	call 0bd55h		;072e	cd 55 bd	. U .
	ld e,c			;0731	59		Y
	ret nz			;0732	c0		.
	inc bc			;0733	03		.
	ld h,c			;0734	61		a
	add a,(hl)		;0735	86		.
	sub b			;0736	90		.
	ld hl,(058d5h)		;0737	2a d5 58	* . X
	ld a,(bc)		;073a	0a		.
	ld hl,(0e80ah)		;073b	2a 0a e8	* . .
	rst 20h			;073e	e7		.
	sbc a,a			;073f	9f		.
	rra			;0740	1f		.
	ret p			;0741	f0		.
	dec h			;0742	25		%
	ld d,e			;0743	53		S
	add a,c			;0744	81		.
	jr nz,$+124		;0745	20 7a		  z
	ld a,(de)		;0747	1a		.
	or e			;0748	b3		.
	xor d			;0749	aa		.
	sub d			;074a	92		.
	ld l,c			;074b	69		i
	push de			;074c	d5		.
	ld d,d			;074d	52		R
	ld b,l			;074e	45		E
	ld d,(hl)		;074f	56		V
	add hl,de		;0750	19		.
	ret nc			;0751	d0		.
	nop			;0752	00		.
	cp a			;0753	bf		.
	and b			;0754	a0		.
	cpl			;0755	2f		/
	dec d			;0756	15		.
	adc a,e			;0757	8b		.
	ld d,(hl)		;0758	56		V
	ld b,c			;0759	41		A
	ret nz			;075a	c0		.
	ld b,b			;075b	40		@
	push bc			;075c	c5		.
	push de			;075d	d5		.
	xor d			;075e	aa		.
	ld d,b			;075f	50		P
	ex (sp),hl		;0760	e3		.
	and e			;0761	a3		.
	out (0e8h),a		;0762	d3 e8		. .
	defb 0edh ;next byte illegal after ed	;0764	ed		.
	ld c,h			;0765	4c		L
	nop			;0766	00		.
	ld e,081h		;0767	1e 81		. .
	dec (hl)		;0769	35		5
	ld h,d			;076a	62		b
	ld sp,0687ah		;076b	31 7a 68	1 z h
	add hl,sp		;076e	39		9
	ld e,h			;076f	5c		\
	ret po			;0770	e0		.
	rrca			;0771	0f		.
	inc h			;0772	24		$
	halt			;0773	76		v
	dec de			;0774	1b		.
	ccf			;0775	3f		?
	jr z,$-79		;0776	28 af		( .
	djnz $+100		;0778	10 62		. b
	rst 38h			;077a	ff		.
	inc de			;077b	13		.
	jp c,01ec7h		;077c	da c7 1e	. . .
	ld d,d			;077f	52		R
	ld hl,0c975h		;0780	21 75 c9	! u .
	jr nz,$-108		;0783	20 92		  .
	ld b,03fh		;0785	06 3f		. ?
	jp nz,0295bh		;0787	c2 5b 29	. [ )
	ld (hl),h		;078a	74		t
	rst 38h			;078b	ff		.
	sbc a,00dh		;078c	de 0d		. .
	inc a			;078e	3c		<
	ld h,a			;078f	67		g
	djnz $-3		;0790	10 fb		. .
	ld d,l			;0792	55		U
	rra			;0793	1f		.
	xor (hl)		;0794	ae		.
	ld c,h			;0795	4c		L
	ld e,a			;0796	5f		_
	rla			;0797	17		.
	ld h,(hl)		;0798	66		f
	dec b			;0799	05		.
	ld h,(hl)		;079a	66		f
	dec e			;079b	1d		.
	ld h,a			;079c	67		g
	pop bc			;079d	c1		.
	add hl,bc		;079e	09		.
	ld e,064h		;079f	1e 64		. d
	cp 020h			;07a1	fe 20		.  
	rst 38h			;07a3	ff		.
	ld h,d			;07a4	62		b
	and e			;07a5	a3		.
	ccf			;07a6	3f		?
	djnz $+4		;07a7	10 02		. .
	ld (0c31eh),hl		;07a9	22 1e c3	" . .
	ret po			;07ac	e0		.
	rrca			;07ad	0f		.
	ld d,h			;07ae	54		T
	sub l			;07af	95		.
	add a,c			;07b0	81		.
	rlca			;07b1	07		.
	jp nc,00043h		;07b2	d2 43 00	. C .
	or c			;07b5	b1		.
	ld h,c			;07b6	61		a
	ld (09d20h),hl		;07b7	22 20 9d	"   .
	ld h,008h		;07ba	26 08		& .
	ld a,a			;07bc	7f		.
	nop			;07bd	00		.
	inc e			;07be	1c		.
	ld a,(bc)		;07bf	0a		.
	call nz,02060h		;07c0	c4 60 20	. `  
	di			;07c3	f3		.
	ret z			;07c4	c8		.
	ld h,b			;07c5	60		`
	ld a,l			;07c6	7d		}
	ld a,a			;07c7	7f		.
	nop			;07c8	00		.
	and d			;07c9	a2		.
	add hl,hl		;07ca	29		)
	ld (hl),h		;07cb	74		t
	ld b,e			;07cc	43		C
	call nc,0c015h		;07cd	d4 15 c0	. . .
	sub l			;07d0	95		.
	jp nz,0539fh		;07d1	c2 9f 53	. . S
	call m,02006h		;07d4	fc 06 20	. .  
	call m,0b13ch		;07d7	fc 3c b1	. < .
	ld d,e			;07da	53		S
	rst 38h			;07db	ff		.
	di			;07dc	f3		.
	ld e,030h		;07dd	1e 30		. 0
	nop			;07df	00		.
	ld (hl),b		;07e0	70		p
	ld a,04ch		;07e1	3e 4c		> L
	daa			;07e3	27		'
	sub l			;07e4	95		.
	rra			;07e5	1f		.
	sbc a,c			;07e6	99		.
	sbc a,d			;07e7	9a		.
	rst 18h			;07e8	df		.
	add a,c			;07e9	81		.
	add a,d			;07ea	82		.
	ld l,e			;07eb	6b		k
	and (hl)		;07ec	a6		.
	ld b,c			;07ed	41		A
	rst 38h			;07ee	ff		.
	add a,b			;07ef	80		.
	ld e,e			;07f0	5b		[
	ld e,053h		;07f1	1e 53		. S
	ex af,af'		;07f3	08		.
	jp po,0c001h		;07f4	e2 01 c0	. . .
	adc a,d			;07f7	8a		.
	and (iy-05ah)		;07f8	fd a6 a6	. . .
	rst 18h			;07fb	df		.
	ld bc,0780eh		;07fc	01 0e 78	. . x
	ld h,a			;07ff	67		g
	ld h,d			;0800	62		b
	ld a,l			;0801	7d		}
	ld e,0fbh		;0802	1e fb		. .
	ld b,09ah		;0804	06 9a		. .
	jr $+65			;0806	18 3f		. ?
	sbc a,a			;0808	9f		.
	ld (hl),e		;0809	73		s
	ld (de),a		;080a	12		.
	ccf			;080b	3f		?
	inc d			;080c	14		.
	ld e,(hl)		;080d	5e		^
	djnz $+100		;080e	10 62		. b
	rst 38h			;0810	ff		.
	rst 8			;0811	cf		.
	ld e,h			;0812	5c		\
	ld c,a			;0813	4f		O
	dec a			;0814	3d		=
	ld a,066h		;0815	3e 66		> f
	ld b,a			;0817	47		G
	ld c,c			;0818	49		I
	ccf			;0819	3f		?
	ld bc,0c2abh		;081a	01 ab c2	. . .
	xor d			;081d	aa		.
	ld (de),a		;081e	12		.
	dec e			;081f	1d		.
	ld b,c			;0820	41		A
	ld (07582h),hl		;0821	22 82 75	" . u
	ld d,l			;0824	55		U
	push af			;0825	f5		.
	ld b,b			;0826	40		@
	jr c,$-21		;0827	38 e9		8 .
	inc c			;0829	0c		.
	ei			;082a	fb		.
	xor d			;082b	aa		.
	rra			;082c	1f		.
	ret po			;082d	e0		.
	add a,(hl)		;082e	86		.
	ld e,a			;082f	5f		_
	ccf			;0830	3f		?
	ret nz			;0831	c0		.
	ld d,e			;0832	53		S
	dec e			;0833	1d		.
	daa			;0834	27		'
	dec hl			;0835	2b		+
	ld l,l			;0836	6d		m
	rrca			;0837	0f		.
	sbc a,a			;0838	9f		.
	pop bc			;0839	c1		.
	dec d			;083a	15		.
	ex af,af'		;083b	08		.
	ld l,b			;083c	68		h
	ld b,d			;083d	42		B
	rst 38h			;083e	ff		.
	inc e			;083f	1c		.
	ret c			;0840	d8		.
	inc sp			;0841	33		3
	ld b,c			;0842	41		A
	adc a,c			;0843	89		.
	ld d,l			;0844	55		U
	sub d			;0845	92		.
	or h			;0846	b4		.
	jr nz,$+95		;0847	20 5d		  ]
	ret po			;0849	e0		.
	inc bc			;084a	03		.
	add a,c			;084b	81		.
	ret nz			;084c	c0		.
	ld (hl),04ch		;084d	36 4c		6 L
	pop bc			;084f	c1		.
	ld d,l			;0850	55		U
	ld h,b			;0851	60		`
	sbc a,l			;0852	9d		.
	and d			;0853	a2		.
	jr nz,$+129		;0854	20 7f		  .
	and e			;0856	a3		.
	ld d,c			;0857	51		Q
	inc bc			;0858	03		.
	adc a,c			;0859	89		.
	sbc a,l			;085a	9d		.
	ld h,(hl)		;085b	66		f
	jr nz,$-36		;085c	20 da		  .
	ld hl,(03781h)		;085e	2a 81 37	* . 7
	add a,h			;0861	84		.
	rst 38h			;0862	ff		.
	ld d,l			;0863	55		U
	ld c,c			;0864	49		I
	ret m			;0865	f8		.
	ld hl,(050c0h)		;0866	2a c0 50	* . P
	adc a,h			;0869	8c		.
	sbc a,a			;086a	9f		.
	rst 38h			;086b	ff		.
	dec b			;086c	05		.
	jr nz,$+0		;086d	20 fe		  .
	rra			;086f	1f		.
	pop bc			;0870	c1		.
	add a,c			;0871	81		.
	cp 058h			;0872	fe 58		. X
	ld h,031h		;0874	26 31		& 1
	pop bc			;0876	c1		.
	ld d,l			;0877	55		U
	ld d,b			;0878	50		P
	and 0fah		;0879	e6 fa		. .
	ld c,h			;087b	4c		L
	rra			;087c	1f		.
	rra			;087d	1f		.
	rst 18h			;087e	df		.
	xor l			;087f	ad		.
	ld (02ba0h),a		;0880	32 a0 2b	2 . +
	jp 0e062h		;0883	c3 62 e0	. b .
	out (0e1h),a		;0886	d3 e1		. .
	call nc,05ce0h		;0888	d4 e0 5c	. . \
	ld a,c			;088b	79		y
	pop de			;088c	d1		.
	dec e			;088d	1d		.
	add hl,hl		;088e	29		)
	add a,d			;088f	82		.
	ccf			;0890	3f		?
	ld bc,0090eh		;0891	01 0e 09	. . .
	and b			;0894	a0		.
	dec (hl)		;0895	35		5
	jr nz,$+63		;0896	20 3d		  =
	ld e,0beh		;0898	1e be		. .
	ld c,c			;089a	49		I
	ld d,l			;089b	55		U
	ld c,b			;089c	48		H
	ld e,020h		;089d	1e 20		.  
	ld a,h			;089f	7c		|
	sbc a,a			;08a0	9f		.
	ret p			;08a1	f0		.
	call nz,0283fh		;08a2	c4 3f 28	. ? (
	xor (hl)		;08a5	ae		.
	djnz $+100		;08a6	10 62		. b
	ld d,a			;08a8	57		W
	sub h			;08a9	94		.
	and l			;08aa	a5		.
	ld e,h			;08ab	5c		\
	add hl,sp		;08ac	39		9
	jr nz,$+32		;08ad	20 1e		  .
	add a,b			;08af	80		.
	ld b,a			;08b0	47		G
	add hl,sp		;08b1	39		9
	rra			;08b2	1f		.
	ld d,(hl)		;08b3	56		V
	jr c,$+5		;08b4	38 03		8 .
	add a,h			;08b6	84		.
	ex af,af'		;08b7	08		.
	ld e,03eh		;08b8	1e 3e		. >
	or b			;08ba	b0		.
	add a,b			;08bb	80		.
	sbc a,0f8h		;08bc	de f8		. .
	adc a,b			;08be	88		.
	ld a,l			;08bf	7d		}
	ld bc,0a714h		;08c0	01 14 a7	. . .
	rst 18h			;08c3	df		.
	ld bc,0097fh		;08c4	01 7f 09	. . .
	pop bc			;08c7	c1		.
	dec sp			;08c8	3b		;
	ret			;08c9	c9		.
	rlca			;08ca	07		.
	add a,b			;08cb	80		.
	ld a,a			;08cc	7f		.
	call m,03b20h		;08cd	fc 20 3b	.   ;
	and d			;08d0	a2		.
	ret z			;08d1	c8		.
	add a,h			;08d2	84		.
	rra			;08d3	1f		.
	ld d,l			;08d4	55		U
	ld d,a			;08d5	57		W
	sub l			;08d6	95		.
	inc c			;08d7	0c		.
	dec d			;08d8	15		.
	jp nz,0bf88h		;08d9	c2 88 bf	. . .
	ld e,001h		;08dc	1e 01		. .
	ret p			;08de	f0		.
	rra			;08df	1f		.
	djnz $+78		;08e0	10 4c		. L
	or (hl)			;08e2	b6		.
	ret po			;08e3	e0		.
	pop bc			;08e4	c1		.
	cp 088h			;08e5	fe 88		. .
	and h			;08e7	a4		.
	jr z,$+118		;08e8	28 74		( t
	inc d			;08ea	14		.
	dec d			;08eb	15		.
	rst 18h			;08ec	df		.
	jr nc,$+2		;08ed	30 00		0 .
	ld a,(de)		;08ef	1a		.
	sbc a,l			;08f0	9d		.
	ld h,c			;08f1	61		a
	jr nz,$-36		;08f2	20 da		  .
	sub e			;08f4	93		.
	add a,e			;08f5	83		.
	dec a			;08f6	3d		=
	ld (bc),a		;08f7	02		.
	ld e,0c1h		;08f8	1e c1		. .
	cp 092h			;08fa	fe 92		. .
	jp 07f1fh		;08fc	c3 1f 7f	. . .
	ld e,a			;08ff	5f		_
