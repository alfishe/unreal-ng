; SPECTRUM.EXE, the Peters Plus Spectrum launcher for DSS (Ivan Mak, 2002), as shipped in C:\ZX\ of DSS hard disks
; File: SPECTRUM.EXE, 2 696 bytes, MD5 see README.md; DSS EXE header (512 bytes) skipped: code loaded and started at #8100, SP #BFFE.
; Disassembled with z80dasm 1.2.0 (-a -t -l -g 0x8100, data blocks #81EC-#849E and #8865-#8987); labels and comments
; added by hand for the unreal-ng Sprinter ZX-mode research (docs/inprogress/2026-09-28-sprinter/research-zx-mode.md).
; No source is published for this launcher (the Tolik-Trek rewrite, v2.03, is a different program with source).
;
; Calls: RST #10 = DSS (C = function: #5C print, #11 open, #12 close, #13 read, #15 seek, #41 exit),
;        RST #08 / RST #18 = BIOS (C = function: #92/#93 RAM disk memory, #C7 next page, #CB RAM disk -> TR-DOS drive,
;        #F2 FN_SYNC, #8F / #47 / #A6 / #56 / #50 screen and window functions).

	org 08100h


; entry: save the command line pointer (IX), print the banner, remember window 3
Start:
	ld (l8483h),ix		;8100	dd 22 83 84	. " . .
	ld hl,l8217h		;8104	21 17 82	! . .
	ld c,05ch		;8107	0e 5c		. \
	rst 10h			;8109	d7		.
	in a,(0e2h)		;810a	db e2		. .
	ld (l848ch),a		;810c	32 8c 84	2 . .
	jp ParseCmdLine		;810f	c3 39 81	. 9 .

; "Error in file" + name, exit
ErrorFile:
	ld hl,l8259h		;8112	21 59 82	! Y .
	ld c,05ch		;8115	0e 5c		. \
	rst 10h			;8117	d7		.
	ld hl,l8907h		;8118	21 07 89	! . .
	ld c,05ch		;811b	0e 5c		. \
	rst 10h			;811d	d7		.
	ld hl,l827eh		;811e	21 7e 82	! ~ .
	jp PrintAndExit		;8121	c3 2a 81	. * .
l8124h:
	ld hl,l82a5h		;8124	21 a5 82	! . .
	jp PrintAndExit		;8127	c3 2a 81	. * .

; restore window 3, print HL, DSS EXIT (#41)
PrintAndExit:
	push hl			;812a	e5		.
	ld a,(l848ch)		;812b	3a 8c 84	: . .
	out (0e2h),a		;812e	d3 e2		. .
	pop hl			;8130	e1		.
	ld c,05ch		;8131	0e 5c		. \
	rst 10h			;8133	d7		.
	ld bc,00041h		;8134	01 41 00	. A .
	rst 10h			;8137	d7		.
	ret			;8138	c9		.

; find the .ZX and the .TR? names on the command line, read the .ZX (or SPECTRUM.CFG) into page #41
ParseCmdLine:
	ld hl,(l8483h)		;8139	2a 83 84	* . .
	ld a,(hl)		;813c	7e		~
	and a			;813d	a7		.
	jr z,l8153h		;813e	28 13		( .
	dec a			;8140	3d		=
	jr z,l8153h		;8141	28 10		( .
	inc hl			;8143	23		#
	inc hl			;8144	23		#
	call FindFiles		;8145	cd 60 81	. ` .
	ld hl,(l8487h)		;8148	2a 87 84	* . .
	ld a,041h		;814b	3e 41		> A
	call ReadFileToPage		;814d	cd d6 86	. . .
	jp nc,messages_end	;8150	d2 9f 84	. . .
l8153h:
	ld hl,l88fah		;8153	21 fa 88	! . .
	ld a,041h		;8156	3e 41		> A
	call ReadFileToPage		;8158	cd d6 86	. . .
	jr c,ErrorFile		;815b	38 b5		8 .
	jp messages_end		;815d	c3 9f 84	. . .

; split the command line into the configuration name and the TRD name
FindFiles:
	push hl			;8160	e5		.
	ld (l8485h),hl		;8161	22 85 84	" . .
	ld de,00000h		;8164	11 00 00	. . .
	ld (l8489h),de		;8167	ed 53 89 84	. S . .
	ld (l8487h),de		;816b	ed 53 87 84	. S . .
	ld b,a			;816f	47		G
	jr l818fh		;8170	18 1d		. .
l8172h:
	ld a,(hl)		;8172	7e		~
	inc hl			;8173	23		#
	cp 02eh			;8174	fe 2e		. .
	call z,sub_81b1h	;8176	cc b1 81	. . .
	cp 020h			;8179	fe 20		.  
	call z,sub_81a9h	;817b	cc a9 81	. . .
	cp 009h			;817e	fe 09		. .
	call z,sub_81a9h	;8180	cc a9 81	. . .
	cp 00ah			;8183	fe 0a		. .
	call z,sub_81ach	;8185	cc ac 81	. . .
	cp 00dh			;8188	fe 0d		. .
	call z,sub_81ach	;818a	cc ac 81	. . .
	jr z,l8191h		;818d	28 02		( .
l818fh:
	djnz l8172h		;818f	10 e1		. .
l8191h:
	pop hl			;8191	e1		.
	ld a,(l8488h)		;8192	3a 88 84	: . .
	and a			;8195	a7		.
	ret nz			;8196	c0		.
	ld a,(l848ah)		;8197	3a 8a 84	: . .
	and a			;819a	a7		.
	ld de,(l8485h)		;819b	ed 5b 85 84	. [ . .
	jr z,l81a4h		;819f	28 03		( .
	ld de,l88fah		;81a1	11 fa 88	. . .
l81a4h:
	ld (l8487h),de		;81a4	ed 53 87 84	. S . .
	ret			;81a8	c9		.
sub_81a9h:
	ld (l8485h),hl		;81a9	22 85 84	" . .
sub_81ach:
	dec hl			;81ac	2b		+
	ld (hl),000h		;81ad	36 00		6 .
	inc hl			;81af	23		#
	ret			;81b0	c9		.
sub_81b1h:
	ld a,(hl)		;81b1	7e		~
	cp 074h			;81b2	fe 74		. t
	jr z,l81c6h		;81b4	28 10		( .
	cp 054h			;81b6	fe 54		. T
	jr z,l81c6h		;81b8	28 0c		( .
	cp 07ah			;81ba	fe 7a		. z
	jr z,l81d0h		;81bc	28 12		( .
	cp 05ah			;81be	fe 5a		. Z
	jr z,l81d0h		;81c0	28 0e		( .
l81c2h:
	dec hl			;81c2	2b		+
	ld a,(hl)		;81c3	7e		~
	inc hl			;81c4	23		#
	ret			;81c5	c9		.
l81c6h:
	ld de,(l8485h)		;81c6	ed 5b 85 84	. [ . .
	ld (l8489h),de		;81ca	ed 53 89 84	. S . .
	jr l81c2h		;81ce	18 f2		. .
l81d0h:
	ld de,(l8485h)		;81d0	ed 5b 85 84	. [ . .
	ld (l8487h),de		;81d4	ed 53 87 84	. S . .
	jr l81c2h		;81d8	18 e8		. .
	ld e,d			;81da	5a		Z
	ld e,b			;81db	58		X
	jr nz,l8231h		;81dc	20 53		  S
	ld (hl),b		;81de	70		p
	ld h,l			;81df	65		e
	ld h,e			;81e0	63		c
	ld (hl),h		;81e1	74		t
	ld (hl),d		;81e2	72		r
	ld (hl),l		;81e3	75		u
	ld l,l			;81e4	6d		m
	jr nz,l8237h		;81e5	20 50		  P
	ld b,c			;81e7	41		A
	ld b,a			;81e8	47		G
	ld b,l			;81e9	45		E
	ld d,e			;81ea	53		S
	nop			;81eb	00		.

; BLOCK 'messages' (start 0x81ec end 0x849f)
messages_start:

; text of the messages (CP866)
Messages:
	defb 00dh		;81ec	0d		.
	defb 00ah		;81ed	0a		.
	defb 045h		;81ee	45		E
	defb 058h		;81ef	58		X
	defb 049h		;81f0	49		I
	defb 054h		;81f1	54		T
	defb 020h		;81f2	20		 
	defb 077h		;81f3	77		w
	defb 069h		;81f4	69		i
	defb 074h		;81f5	74		t
	defb 068h		;81f6	68		h
	defb 06fh		;81f7	6f		o
	defb 075h		;81f8	75		u
	defb 074h		;81f9	74		t
	defb 020h		;81fa	20		 
	defb 072h		;81fb	72		r
	defb 075h		;81fc	75		u
	defb 06eh		;81fd	6e		n
	defb 00dh		;81fe	0d		.
	defb 00ah		;81ff	0a		.
	defb 000h		;8200	00		.
	defb 00dh		;8201	0d		.
	defb 00ah		;8202	0a		.
	defb 082h		;8203	82		.
	defb 0ebh		;8204	eb		.
	defb 0e5h		;8205	e5		.
	defb 0aeh		;8206	ae		.
	defb 0a4h		;8207	a4		.
	defb 020h		;8208	20		 
	defb 0a1h		;8209	a1		.
	defb 0a5h		;820a	a5		.
	defb 0a7h		;820b	a7		.
	defb 020h		;820c	20		 
	defb 0a7h		;820d	a7		.
	defb 0a0h		;820e	a0		.
	defb 0afh		;820f	af		.
	defb 0e3h		;8210	e3		.
	defb 0e1h		;8211	e1		.
	defb 0aah		;8212	aa		.
	defb 0a0h		;8213	a0		.
l8214h:
	defb 00dh		;8214	0d		.
	defb 00ah		;8215	0a		.
	defb 000h		;8216	00		.
l8217h:
	defb 00dh		;8217	0d		.
	defb 00ah		;8218	0a		.
	defb 053h		;8219	53		S
	defb 050h		;821a	50		P
	defb 045h		;821b	45		E
	defb 043h		;821c	43		C
	defb 054h		;821d	54		T
	defb 052h		;821e	52		R
	defb 055h		;821f	55		U
	defb 04dh		;8220	4d		M
	defb 020h		;8221	20		 
	defb 06ch		;8222	6c		l
	defb 061h		;8223	61		a
	defb 075h		;8224	75		u
	defb 06eh		;8225	6e		n
	defb 063h		;8226	63		c
	defb 068h		;8227	68		h
	defb 065h		;8228	65		e
	defb 072h		;8229	72		r
	defb 02eh		;822a	2e		.
	defb 00dh		;822b	0d		.
	defb 00ah		;822c	0a		.
	defb 028h		;822d	28		(
	defb 063h		;822e	63		c
	defb 029h		;822f	29		)
	defb 020h		;8230	20		 
l8231h:
	defb 050h		;8231	50		P
	defb 065h		;8232	65		e
	defb 074h		;8233	74		t
	defb 065h		;8234	65		e
	defb 072h		;8235	72		r
	defb 073h		;8236	73		s
l8237h:
	defb 020h		;8237	20		 
	defb 050h		;8238	50		P
	defb 06ch		;8239	6c		l
	defb 075h		;823a	75		u
	defb 073h		;823b	73		s
	defb 020h		;823c	20		 
	defb 04ch		;823d	4c		L
	defb 074h		;823e	74		t
	defb 064h		;823f	64		d
	defb 02eh		;8240	2e		.
	defb 00dh		;8241	0d		.
	defb 00ah		;8242	0a		.
	defb 057h		;8243	57		W
	defb 072h		;8244	72		r
	defb 069h		;8245	69		i
	defb 074h		;8246	74		t
	defb 065h		;8247	65		e
	defb 064h		;8248	64		d
	defb 020h		;8249	20		 
	defb 062h		;824a	62		b
	defb 079h		;824b	79		y
	defb 020h		;824c	20		 
	defb 049h		;824d	49		I
	defb 076h		;824e	76		v
	defb 061h		;824f	61		a
	defb 06eh		;8250	6e		n
	defb 020h		;8251	20		 
	defb 04dh		;8252	4d		M
	defb 061h		;8253	61		a
	defb 06bh		;8254	6b		k
	defb 02eh		;8255	2e		.
	defb 00dh		;8256	0d		.
	defb 00ah		;8257	0a		.
	defb 000h		;8258	00		.
l8259h:
	defb 00dh		;8259	0d		.
	defb 00ah		;825a	0a		.
	defb 045h		;825b	45		E
	defb 072h		;825c	72		r
	defb 072h		;825d	72		r
	defb 06fh		;825e	6f		o
	defb 072h		;825f	72		r
	defb 020h		;8260	20		 
	defb 069h		;8261	69		i
	defb 06eh		;8262	6e		n
	defb 020h		;8263	20		 
	defb 066h		;8264	66		f
	defb 069h		;8265	69		i
	defb 06ch		;8266	6c		l
	defb 065h		;8267	65		e
	defb 03ah		;8268	3a		:
	defb 020h		;8269	20		 
	defb 000h		;826a	00		.
	defb 00dh		;826b	0d		.
	defb 00ah		;826c	0a		.
	defb 08eh		;826d	8e		.
	defb 0e8h		;826e	e8		.
	defb 0a8h		;826f	a8		.
	defb 0a1h		;8270	a1		.
	defb 0aah		;8271	aa		.
	defb 0a0h		;8272	a0		.
	defb 020h		;8273	20		 
	defb 0a2h		;8274	a2		.
	defb 020h		;8275	20		 
	defb 0e4h		;8276	e4		.
	defb 0a0h		;8277	a0		.
	defb 0a9h		;8278	a9		.
	defb 0abh		;8279	ab		.
	defb 0a5h		;827a	a5		.
	defb 03ah		;827b	3a		:
	defb 020h		;827c	20		 
	defb 000h		;827d	00		.
l827eh:
	defb 00dh		;827e	0d		.
	defb 00ah		;827f	0a		.
	defb 055h		;8280	55		U
	defb 06eh		;8281	6e		n
	defb 061h		;8282	61		a
	defb 062h		;8283	62		b
	defb 06ch		;8284	6c		l
	defb 065h		;8285	65		e
	defb 020h		;8286	20		 
	defb 074h		;8287	74		t
	defb 06fh		;8288	6f		o
	defb 020h		;8289	20		 
	defb 077h		;828a	77		w
	defb 06fh		;828b	6f		o
	defb 072h		;828c	72		r
	defb 06bh		;828d	6b		k
	defb 02eh		;828e	2e		.
	defb 000h		;828f	00		.
	defb 00dh		;8290	0d		.
	defb 00ah		;8291	0a		.
	defb 090h		;8292	90		.
	defb 0a0h		;8293	a0		.
	defb 0a1h		;8294	a1		.
	defb 0aeh		;8295	ae		.
	defb 0e2h		;8296	e2		.
	defb 0a0h		;8297	a0		.
	defb 020h		;8298	20		 
	defb 0adh		;8299	ad		.
	defb 0a5h		;829a	a5		.
	defb 0a2h		;829b	a2		.
	defb 0aeh		;829c	ae		.
	defb 0a7h		;829d	a7		.
	defb 0ach		;829e	ac		.
	defb 0aeh		;829f	ae		.
	defb 0a6h		;82a0	a6		.
	defb 0adh		;82a1	ad		.
	defb 0a0h		;82a2	a0		.
	defb 02eh		;82a3	2e		.
	defb 000h		;82a4	00		.
l82a5h:
	defb 00dh		;82a5	0d		.
	defb 00ah		;82a6	0a		.
	defb 055h		;82a7	55		U
	defb 06eh		;82a8	6e		n
	defb 065h		;82a9	65		e
	defb 078h		;82aa	78		x
	defb 070h		;82ab	70		p
	defb 065h		;82ac	65		e
	defb 063h		;82ad	63		c
	defb 074h		;82ae	74		t
	defb 065h		;82af	65		e
	defb 064h		;82b0	64		d
	defb 020h		;82b1	20		 
	defb 043h		;82b2	43		C
	defb 04eh		;82b3	4e		N
	defb 046h		;82b4	46		F
	defb 020h		;82b5	20		 
	defb 066h		;82b6	66		f
	defb 069h		;82b7	69		i
	defb 06ch		;82b8	6c		l
	defb 065h		;82b9	65		e
	defb 020h		;82ba	20		 
	defb 065h		;82bb	65		e
	defb 06eh		;82bc	6e		n
	defb 064h		;82bd	64		d
	defb 02eh		;82be	2e		.
	defb 000h		;82bf	00		.
	defb 00dh		;82c0	0d		.
	defb 00ah		;82c1	0a		.
	defb 08dh		;82c2	8d		.
	defb 0a5h		;82c3	a5		.
	defb 0aeh		;82c4	ae		.
	defb 0a6h		;82c5	a6		.
	defb 0a8h		;82c6	a8		.
	defb 0a4h		;82c7	a4		.
	defb 0a0h		;82c8	a0		.
	defb 0adh		;82c9	ad		.
	defb 0adh		;82ca	ad		.
	defb 0ebh		;82cb	eb		.
	defb 0a9h		;82cc	a9		.
	defb 020h		;82cd	20		 
	defb 0aah		;82ce	aa		.
	defb 0aeh		;82cf	ae		.
	defb 0adh		;82d0	ad		.
	defb 0a5h		;82d1	a5		.
	defb 0e6h		;82d2	e6		.
	defb 020h		;82d3	20		 
	defb 043h		;82d4	43		C
	defb 04eh		;82d5	4e		N
	defb 046h		;82d6	46		F
	defb 020h		;82d7	20		 
	defb 0e4h		;82d8	e4		.
	defb 0a0h		;82d9	a0		.
	defb 0a9h		;82da	a9		.
	defb 0abh		;82db	ab		.
	defb 0a0h		;82dc	a0		.
	defb 02eh		;82dd	2e		.
	defb 000h		;82de	00		.
	defb 00dh		;82df	0d		.
	defb 00ah		;82e0	0a		.
	defb 054h		;82e1	54		T
	defb 068h		;82e2	68		h
	defb 065h		;82e3	65		e
	defb 020h		;82e4	20		 
	defb 073h		;82e5	73		s
	defb 070h		;82e6	70		p
	defb 065h		;82e7	65		e
	defb 073h		;82e8	73		s
	defb 069h		;82e9	69		i
	defb 061h		;82ea	61		a
	defb 06ch		;82eb	6c		l
	defb 020h		;82ec	20		 
	defb 070h		;82ed	70		p
	defb 061h		;82ee	61		a
	defb 067h		;82ef	67		g
	defb 065h		;82f0	65		e
	defb 073h		;82f1	73		s
	defb 020h		;82f2	20		 
	defb 061h		;82f3	61		a
	defb 072h		;82f4	72		r
	defb 065h		;82f5	65		e
	defb 020h		;82f6	20		 
	defb 061h		;82f7	61		a
	defb 06ch		;82f8	6c		l
	defb 072h		;82f9	72		r
	defb 065h		;82fa	65		e
	defb 061h		;82fb	61		a
	defb 064h		;82fc	64		d
	defb 079h		;82fd	79		y
	defb 020h		;82fe	20		 
	defb 075h		;82ff	75		u
	defb 073h		;8300	73		s
	defb 065h		;8301	65		e
	defb 064h		;8302	64		d
	defb 02eh		;8303	2e		.
	defb 00dh		;8304	0d		.
	defb 00ah		;8305	0a		.
	defb 043h		;8306	43		C
	defb 06ch		;8307	6c		l
	defb 065h		;8308	65		e
	defb 061h		;8309	61		a
	defb 072h		;830a	72		r
	defb 020h		;830b	20		 
	defb 06dh		;830c	6d		m
	defb 065h		;830d	65		e
	defb 06dh		;830e	6d		m
	defb 06fh		;830f	6f		o
	defb 072h		;8310	72		r
	defb 079h		;8311	79		y
	defb 020h		;8312	20		 
	defb 061h		;8313	61		a
	defb 06eh		;8314	6e		n
	defb 064h		;8315	64		d
	defb 020h		;8316	20		 
	defb 072h		;8317	72		r
	defb 065h		;8318	65		e
	defb 073h		;8319	73		s
	defb 074h		;831a	74		t
	defb 061h		;831b	61		a
	defb 072h		;831c	72		r
	defb 074h		;831d	74		t
	defb 020h		;831e	20		 
	defb 073h		;831f	73		s
	defb 070h		;8320	70		p
	defb 065h		;8321	65		e
	defb 063h		;8322	63		c
	defb 074h		;8323	74		t
	defb 072h		;8324	72		r
	defb 075h		;8325	75		u
	defb 06dh		;8326	6d		m
	defb 02eh		;8327	2e		.
	defb 065h		;8328	65		e
	defb 078h		;8329	78		x
	defb 065h		;832a	65		e
	defb 020h		;832b	20		 
	defb 061h		;832c	61		a
	defb 067h		;832d	67		g
	defb 061h		;832e	61		a
	defb 069h		;832f	69		i
	defb 06eh		;8330	6e		n
	defb 02eh		;8331	2e		.
	defb 000h		;8332	00		.
	defb 00dh		;8333	0d		.
	defb 00ah		;8334	0a		.
	defb 091h		;8335	91		.
	defb 0afh		;8336	af		.
	defb 0a5h		;8337	a5		.
	defb 0e6h		;8338	e6		.
	defb 0a8h		;8339	a8		.
	defb 0a0h		;833a	a0		.
	defb 0abh		;833b	ab		.
	defb 0ech		;833c	ec		.
	defb 0adh		;833d	ad		.
	defb 0ebh		;833e	eb		.
	defb 0a5h		;833f	a5		.
	defb 020h		;8340	20		 
	defb 0e1h		;8341	e1		.
	defb 0e2h		;8342	e2		.
	defb 0e0h		;8343	e0		.
	defb 0a0h		;8344	a0		.
	defb 0adh		;8345	ad		.
	defb 0a8h		;8346	a8		.
	defb 0e6h		;8347	e6		.
	defb 0ebh		;8348	eb		.
	defb 020h		;8349	20		 
	defb 0e3h		;834a	e3		.
	defb 0a6h		;834b	a6		.
	defb 0a5h		;834c	a5		.
	defb 020h		;834d	20		 
	defb 0a7h		;834e	a7		.
	defb 0a0h		;834f	a0		.
	defb 0adh		;8350	ad		.
	defb 0efh		;8351	ef		.
	defb 0e2h		;8352	e2		.
	defb 0ebh		;8353	eb		.
	defb 02eh		;8354	2e		.
	defb 00dh		;8355	0d		.
	defb 00ah		;8356	0a		.
	defb 08eh		;8357	8e		.
	defb 0e7h		;8358	e7		.
	defb 0a8h		;8359	a8		.
	defb 0e1h		;835a	e1		.
	defb 0e2h		;835b	e2		.
	defb 0a8h		;835c	a8		.
	defb 0e2h		;835d	e2		.
	defb 0a5h		;835e	a5		.
	defb 020h		;835f	20		 
	defb 0afh		;8360	af		.
	defb 0a0h		;8361	a0		.
	defb 0ach		;8362	ac		.
	defb 0efh		;8363	ef		.
	defb 0e2h		;8364	e2		.
	defb 0ech		;8365	ec		.
	defb 020h		;8366	20		 
	defb 0a8h		;8367	a8		.
	defb 020h		;8368	20		 
	defb 0afh		;8369	af		.
	defb 0a5h		;836a	a5		.
	defb 0e0h		;836b	e0		.
	defb 0a5h		;836c	a5		.
	defb 0a7h		;836d	a7		.
	defb 0a0h		;836e	a0		.
	defb 0afh		;836f	af		.
	defb 0e3h		;8370	e3		.
	defb 0e1h		;8371	e1		.
	defb 0e2h		;8372	e2		.
	defb 0a8h		;8373	a8		.
	defb 0e2h		;8374	e2		.
	defb 0a5h		;8375	a5		.
	defb 020h		;8376	20		 
	defb 073h		;8377	73		s
	defb 070h		;8378	70		p
	defb 065h		;8379	65		e
	defb 063h		;837a	63		c
	defb 074h		;837b	74		t
	defb 072h		;837c	72		r
	defb 075h		;837d	75		u
	defb 06dh		;837e	6d		m
	defb 02eh		;837f	2e		.
	defb 065h		;8380	65		e
	defb 078h		;8381	78		x
	defb 065h		;8382	65		e
	defb 020h		;8383	20		 
	defb 0e1h		;8384	e1		.
	defb 0adh		;8385	ad		.
	defb 0aeh		;8386	ae		.
	defb 0a2h		;8387	a2		.
	defb 0a0h		;8388	a0		.
	defb 02eh		;8389	2e		.
	defb 000h		;838a	00		.
l838bh:
	defb 00dh		;838b	0d		.
	defb 00ah		;838c	0a		.
	defb 041h		;838d	41		A
	defb 06ch		;838e	6c		l
	defb 06ch		;838f	6c		l
	defb 020h		;8390	20		 
	defb 066h		;8391	66		f
	defb 069h		;8392	69		i
	defb 06ch		;8393	6c		l
	defb 065h		;8394	65		e
	defb 073h		;8395	73		s
	defb 020h		;8396	20		 
	defb 068h		;8397	68		h
	defb 061h		;8398	61		a
	defb 073h		;8399	73		s
	defb 020h		;839a	20		 
	defb 062h		;839b	62		b
	defb 065h		;839c	65		e
	defb 065h		;839d	65		e
	defb 06eh		;839e	6e		n
	defb 020h		;839f	20		 
	defb 072h		;83a0	72		r
	defb 065h		;83a1	65		e
	defb 061h		;83a2	61		a
	defb 064h		;83a3	64		d
	defb 020h		;83a4	20		 
	defb 073h		;83a5	73		s
	defb 075h		;83a6	75		u
	defb 063h		;83a7	63		c
	defb 063h		;83a8	63		c
	defb 065h		;83a9	65		e
	defb 073h		;83aa	73		s
	defb 073h		;83ab	73		s
	defb 066h		;83ac	66		f
	defb 075h		;83ad	75		u
	defb 06ch		;83ae	6c		l
	defb 06ch		;83af	6c		l
	defb 079h		;83b0	79		y
	defb 02eh		;83b1	2e		.
	defb 00dh		;83b2	0d		.
	defb 00ah		;83b3	0a		.
	defb 04dh		;83b4	4d		M
	defb 04fh		;83b5	4f		O
	defb 044h		;83b6	44		D
	defb 045h		;83b7	45		E
	defb 03ah		;83b8	3a		:
	defb 020h		;83b9	20		 
	defb 000h		;83ba	00		.
	defb 00dh		;83bb	0d		.
	defb 00ah		;83bc	0a		.
	defb 082h		;83bd	82		.
	defb 0e1h		;83be	e1		.
	defb 0a5h		;83bf	a5		.
	defb 020h		;83c0	20		 
	defb 0e4h		;83c1	e4		.
	defb 0a0h		;83c2	a0		.
	defb 0a9h		;83c3	a9		.
	defb 0abh		;83c4	ab		.
	defb 0ebh		;83c5	eb		.
	defb 020h		;83c6	20		 
	defb 0e1h		;83c7	e1		.
	defb 0e7h		;83c8	e7		.
	defb 0a8h		;83c9	a8		.
	defb 0e2h		;83ca	e2		.
	defb 0a0h		;83cb	a0		.
	defb 0adh		;83cc	ad		.
	defb 0ebh		;83cd	eb		.
	defb 020h		;83ce	20		 
	defb 0adh		;83cf	ad		.
	defb 0aeh		;83d0	ae		.
	defb 0e0h		;83d1	e0		.
	defb 0ach		;83d2	ac		.
	defb 0a0h		;83d3	a0		.
	defb 0abh		;83d4	ab		.
	defb 0ech		;83d5	ec		.
	defb 0adh		;83d6	ad		.
	defb 0aeh		;83d7	ae		.
	defb 02eh		;83d8	2e		.
	defb 00dh		;83d9	0d		.
	defb 00ah		;83da	0a		.
	defb 08ah		;83db	8a		.
	defb 0aeh		;83dc	ae		.
	defb 0adh		;83dd	ad		.
	defb 0e4h		;83de	e4		.
	defb 0a8h		;83df	a8		.
	defb 0a3h		;83e0	a3		.
	defb 0e3h		;83e1	e3		.
	defb 0e0h		;83e2	e0		.
	defb 0a0h		;83e3	a0		.
	defb 0e6h		;83e4	e6		.
	defb 0a8h		;83e5	a8		.
	defb 0efh		;83e6	ef		.
	defb 03ah		;83e7	3a		:
	defb 020h		;83e8	20		 
	defb 000h		;83e9	00		.
l83eah:
	defb 00dh		;83ea	0d		.
	defb 00ah		;83eb	0a		.
	defb 04eh		;83ec	4e		N
	defb 06fh		;83ed	6f		o
	defb 020h		;83ee	20		 
	defb 06dh		;83ef	6d		m
	defb 065h		;83f0	65		e
	defb 06dh		;83f1	6d		m
	defb 06fh		;83f2	6f		o
	defb 072h		;83f3	72		r
	defb 079h		;83f4	79		y
	defb 020h		;83f5	20		 
	defb 073h		;83f6	73		s
	defb 070h		;83f7	70		p
	defb 061h		;83f8	61		a
	defb 063h		;83f9	63		c
	defb 065h		;83fa	65		e
	defb 020h		;83fb	20		 
	defb 066h		;83fc	66		f
	defb 06fh		;83fd	6f		o
	defb 072h		;83fe	72		r
	defb 020h		;83ff	20		 
	defb 054h		;8400	54		T
	defb 052h		;8401	52		R
	defb 044h		;8402	44		D
	defb 020h		;8403	20		 
	defb 06fh		;8404	6f		o
	defb 072h		;8405	72		r
	defb 000h		;8406	00		.
	defb 00dh		;8407	0d		.
	defb 00ah		;8408	0a		.
	defb 08dh		;8409	8d		.
	defb 0a5h		;840a	a5		.
	defb 020h		;840b	20		 
	defb 0e5h		;840c	e5		.
	defb 0a2h		;840d	a2		.
	defb 0a0h		;840e	a0		.
	defb 0e2h		;840f	e2		.
	defb 0a0h		;8410	a0		.
	defb 0a5h		;8411	a5		.
	defb 0e2h		;8412	e2		.
	defb 020h		;8413	20		 
	defb 0afh		;8414	af		.
	defb 0a0h		;8415	a0		.
	defb 0ach		;8416	ac		.
	defb 0efh		;8417	ef		.
	defb 0e2h		;8418	e2		.
	defb 0a8h		;8419	a8		.
	defb 020h		;841a	20		 
	defb 0a4h		;841b	a4		.
	defb 0abh		;841c	ab		.
	defb 0efh		;841d	ef		.
	defb 020h		;841e	20		 
	defb 054h		;841f	54		T
	defb 052h		;8420	52		R
	defb 044h		;8421	44		D
	defb 020h		;8422	20		 
	defb 0a8h		;8423	a8		.
	defb 0abh		;8424	ab		.
	defb 0a8h		;8425	a8		.
	defb 000h		;8426	00		.
l8427h:
	defb 00dh		;8427	0d		.
	defb 00ah		;8428	0a		.
	defb 054h		;8429	54		T
	defb 052h		;842a	52		R
	defb 044h		;842b	44		D
	defb 020h		;842c	20		 
	defb 06ch		;842d	6c		l
	defb 06fh		;842e	6f		o
	defb 061h		;842f	61		a
	defb 064h		;8430	64		d
	defb 069h		;8431	69		i
	defb 06eh		;8432	6e		n
	defb 067h		;8433	67		g
	defb 03ah		;8434	3a		:
	defb 020h		;8435	20		 
	defb 020h		;8436	20		 
	defb 000h		;8437	00		.
	defb 00dh		;8438	0d		.
	defb 00ah		;8439	0a		.
	defb 087h		;843a	87		.
	defb 0a0h		;843b	a0		.
	defb 0a3h		;843c	a3		.
	defb 0e0h		;843d	e0		.
	defb 0e3h		;843e	e3		.
	defb 0a7h		;843f	a7		.
	defb 0aah		;8440	aa		.
	defb 0a0h		;8441	a0		.
	defb 020h		;8442	20		 
	defb 054h		;8443	54		T
	defb 052h		;8444	52		R
	defb 044h		;8445	44		D
	defb 03ah		;8446	3a		:
	defb 020h		;8447	20		 
	defb 000h		;8448	00		.
l8449h:
	defb 00dh		;8449	0d		.
	defb 00ah		;844a	0a		.
	defb 045h		;844b	45		E
	defb 058h		;844c	58		X
	defb 049h		;844d	49		I
	defb 054h		;844e	54		T
	defb 020h		;844f	20		 
	defb 066h		;8450	66		f
	defb 072h		;8451	72		r
	defb 06fh		;8452	6f		o
	defb 06dh		;8453	6d		m
	defb 020h		;8454	20		 
	defb 053h		;8455	53		S
	defb 070h		;8456	70		p
	defb 065h		;8457	65		e
	defb 063h		;8458	63		c
	defb 074h		;8459	74		t
	defb 072h		;845a	72		r
	defb 075h		;845b	75		u
	defb 06dh		;845c	6d		m
	defb 020h		;845d	20		 
	defb 063h		;845e	63		c
	defb 06fh		;845f	6f		o
	defb 06eh		;8460	6e		n
	defb 066h		;8461	66		f
	defb 069h		;8462	69		i
	defb 067h		;8463	67		g
	defb 075h		;8464	75		u
	defb 072h		;8465	72		r
	defb 061h		;8466	61		a
	defb 074h		;8467	74		t
	defb 069h		;8468	69		i
	defb 06fh		;8469	6f		o
	defb 06eh		;846a	6e		n
	defb 000h		;846b	00		.
	defb 00dh		;846c	0d		.
	defb 00ah		;846d	0a		.
	defb 045h		;846e	45		E
	defb 058h		;846f	58		X
	defb 049h		;8470	49		I
	defb 054h		;8471	54		T
	defb 020h		;8472	20		 
	defb 066h		;8473	66		f
	defb 072h		;8474	72		r
	defb 06fh		;8475	6f		o
	defb 06dh		;8476	6d		m
	defb 020h		;8477	20		 
	defb 05ah		;8478	5a		Z
	defb 058h		;8479	58		X
	defb 020h		;847a	20		 
	defb 06dh		;847b	6d		m
	defb 06fh		;847c	6f		o
	defb 064h		;847d	64		d
	defb 065h		;847e	65		e
	defb 000h		;847f	00		.
l8480h:
	defb 0b0h		;8480	b0		.
	defb 000h		;8481	00		.
l8482h:
	defb 000h		;8482	00		.
l8483h:
	defb 000h		;8483	00		.
	defb 000h		;8484	00		.
l8485h:
	defb 000h		;8485	00		.
	defb 000h		;8486	00		.
l8487h:
	defb 000h		;8487	00		.
l8488h:
	defb 000h		;8488	00		.
l8489h:
	defb 000h		;8489	00		.
l848ah:
	defb 000h		;848a	00		.
l848bh:
	defb 000h		;848b	00		.
l848ch:
	defb 000h		;848c	00		.
l848dh:
	defb 000h		;848d	00		.
	defb 000h		;848e	00		.
l848fh:
	defb 000h		;848f	00		.
	defb 000h		;8490	00		.
l8491h:
	defb 000h		;8491	00		.
	defb 000h		;8492	00		.
l8493h:
	defb 000h		;8493	00		.
	defb 000h		;8494	00		.
l8495h:
	defb 000h		;8495	00		.
	defb 000h		;8496	00		.
l8497h:
	defb 000h		;8497	00		.
	defb 000h		;8498	00		.
l8499h:
	defb 000h		;8499	00		.
	defb 000h		;849a	00		.
l849bh:
	defb 000h		;849b	00		.
	defb 000h		;849c	00		.
l849dh:
	defb 000h		;849d	00		.
	defb 000h		;849e	00		.
messages_end:

; cut the .ZX file into its lines
ReadCnfLines:
	ld (l848dh),de		;849f	ed 53 8d 84	. S . .
	ld hl,0c000h		;84a3	21 00 c0	! . .
	ld de,l848fh		;84a6	11 8f 84	. . .
	ld c,008h		;84a9	0e 08		. .
l84abh:
	ld b,078h		;84ab	06 78		. x
	ex de,hl		;84ad	eb		.
	ld (hl),e		;84ae	73		s
	inc hl			;84af	23		#
	ld (hl),d		;84b0	72		r
	inc hl			;84b1	23		#
	ex de,hl		;84b2	eb		.
l84b3h:
	ld a,(hl)		;84b3	7e		~
	cp 00dh			;84b4	fe 0d		. .
	jr z,l84c4h		;84b6	28 0c		( .
	cp 00ah			;84b8	fe 0a		. .
	jr z,l84c4h		;84ba	28 08		( .
	cp 000h			;84bc	fe 00		. .
	jp z,l8124h		;84be	ca 24 81	. $ .
	inc hl			;84c1	23		#
	djnz l84b3h		;84c2	10 ef		. .
l84c4h:
	ld (hl),000h		;84c4	36 00		6 .
	inc hl			;84c6	23		#
	ld a,(hl)		;84c7	7e		~
	cp 00dh			;84c8	fe 0d		. .
	jr z,l84c4h		;84ca	28 f8		( .
	cp 00ah			;84cc	fe 0a		. .
	jr z,l84c4h		;84ce	28 f4		( .
	dec c			;84d0	0d		.
	jr nz,l84abh		;84d1	20 d8		  .

; load lines 2-7 (BASIC 128, BASIC 48, TR-DOS, three extension ROMs) into RAM pages #42-#47
LoadRomImages:
	ld hl,(l8491h)		;84d3	2a 91 84	* . .
	ld a,042h		;84d6	3e 42		> B
	call ReadFileToPage		;84d8	cd d6 86	. . .
	ld a,041h		;84db	3e 41		> A
	out (0e2h),a		;84dd	d3 e2		. .
	jp c,ErrorFile		;84df	da 12 81	. . .
	ld hl,(l8493h)		;84e2	2a 93 84	* . .
	ld a,043h		;84e5	3e 43		> C
	call ReadFileToPage		;84e7	cd d6 86	. . .
	ld a,041h		;84ea	3e 41		> A
	out (0e2h),a		;84ec	d3 e2		. .
	jp c,ErrorFile		;84ee	da 12 81	. . .
	ld hl,(l8495h)		;84f1	2a 95 84	* . .
	ld a,044h		;84f4	3e 44		> D
	call ReadFileToPage		;84f6	cd d6 86	. . .
	ld a,041h		;84f9	3e 41		> A
	out (0e2h),a		;84fb	d3 e2		. .
	jp c,ErrorFile		;84fd	da 12 81	. . .
	ld hl,(l8497h)		;8500	2a 97 84	* . .
	ld a,045h		;8503	3e 45		> E
	call ReadFileToPage		;8505	cd d6 86	. . .
	ld a,041h		;8508	3e 41		> A
	out (0e2h),a		;850a	d3 e2		. .
	jp c,ErrorFile		;850c	da 12 81	. . .
	ld hl,(l8499h)		;850f	2a 99 84	* . .
	ld a,046h		;8512	3e 46		> F
	call ReadFileToPage		;8514	cd d6 86	. . .
	ld a,041h		;8517	3e 41		> A
	out (0e2h),a		;8519	d3 e2		. .
	jp c,ErrorFile		;851b	da 12 81	. . .
	ld hl,(l849bh)		;851e	2a 9b 84	* . .
	ld a,047h		;8521	3e 47		> G
	call ReadFileToPage		;8523	cd d6 86	. . .
	ld a,041h		;8526	3e 41		> A
	out (0e2h),a		;8528	d3 e2		. .
	jp c,ErrorFile		;852a	da 12 81	. . .
	ld hl,l838bh		;852d	21 8b 83	! . .
	ld c,05ch		;8530	0e 5c		. \
	rst 10h			;8532	d7		.
	ld hl,(l848fh)		;8533	2a 8f 84	* . .
	ld c,05ch		;8536	0e 5c		. \
	rst 10h			;8538	d7		.
	ld hl,l8214h		;8539	21 14 82	! . .
	ld c,05ch		;853c	0e 5c		. \
	rst 10h			;853e	d7		.
	ld a,041h		;853f	3e 41		> A
	out (0e2h),a		;8541	d3 e2		. .

; parse the /option words of line 8
ParseOptions:
	ld hl,(l849dh)		;8543	2a 9d 84	* . .
l8546h:
	ld a,(hl)		;8546	7e		~
	cp 02fh			;8547	fe 2f		. /
	jr z,l855ah		;8549	28 0f		( .
	cp 000h			;854b	fe 00		. .
	jr z,l85a6h		;854d	28 57		( W
	cp 00dh			;854f	fe 0d		. .
	jr z,l85a6h		;8551	28 53		( S
	cp 00ah			;8553	fe 0a		. .
	jr z,l85a6h		;8555	28 4f		( O
	inc hl			;8557	23		#
	jr l8546h		;8558	18 ec		. .
l855ah:
	inc hl			;855a	23		#
	push hl			;855b	e5		.
	ld ix,tail_start	;855c	dd 21 65 88	. ! e .
l8560h:
	ld e,(ix+000h)		;8560	dd 5e 00	. ^ .
	ld d,(ix+001h)		;8563	dd 56 01	. V .
l8566h:
	ld a,(de)		;8566	1a		.
	cp (hl)			;8567	be		.
	jr nz,l856eh		;8568	20 04		  .
	inc hl			;856a	23		#
	inc de			;856b	13		.
	jr l8566h		;856c	18 f8		. .
l856eh:
	cp 0ffh			;856e	fe ff		. .
	jr nz,l857fh		;8570	20 0d		  .
	ld a,(hl)		;8572	7e		~
	cp 020h			;8573	fe 20		.  
	jr z,l8592h		;8575	28 1b		( .
	cp 000h			;8577	fe 00		. .
	jr z,l8592h		;8579	28 17		( .
	cp 00dh			;857b	fe 0d		. .
	jr z,l8592h		;857d	28 13		( .
l857fh:
	pop hl			;857f	e1		.
	push hl			;8580	e5		.
	inc ix			;8581	dd 23		. #
	inc ix			;8583	dd 23		. #
	inc ix			;8585	dd 23		. #
	inc ix			;8587	dd 23		. #
	ld a,(ix+001h)		;8589	dd 7e 01	. ~ .
	and a			;858c	a7		.
	jr nz,l8560h		;858d	20 d1		  .
	pop hl			;858f	e1		.
	jr l8546h		;8590	18 b4		. .
l8592h:
	ex (sp),hl		;8592	e3		.
	ld a,(ix+003h)		;8593	dd 7e 03	. ~ .
	ld (ix+002h),a		;8596	dd 77 02	. w .
	ld l,(ix+000h)		;8599	dd 6e 00	. n .
	ld h,(ix+001h)		;859c	dd 66 01	. f .
	ld c,05ch		;859f	0e 5c		. \
	rst 10h			;85a1	d7		.
	pop hl			;85a2	e1		.
	jr l8546h		;85a3	18 a1		. .
	jp (hl)			;85a5	e9		.
l85a6h:
	ld a,0e2h		;85a6	3e e2		> .
	ld b,042h		;85a8	06 42		. B
	call SetCellViaPortTable		;85aa	cd f3 85	. . .
	ld a,0e3h		;85ad	3e e3		> .
	ld b,043h		;85af	06 43		. C
	call SetCellViaPortTable		;85b1	cd f3 85	. . .
	ld a,0e1h		;85b4	3e e1		> .
	ld b,044h		;85b6	06 44		. D
	call SetCellViaPortTable		;85b8	cd f3 85	. . .
	ld a,0e0h		;85bb	3e e0		> .
	ld b,045h		;85bd	06 45		. E
	call SetCellViaPortTable		;85bf	cd f3 85	. . .
	ld a,0ebh		;85c2	3e eb		> .
	ld b,046h		;85c4	06 46		. F
	call SetCellViaPortTable		;85c6	cd f3 85	. . .
	ld a,0efh		;85c9	3e ef		> .
	ld b,047h		;85cb	06 47		. G
	call SetCellViaPortTable		;85cd	cd f3 85	. . .

; a TRD on the command line: LoadTrdToRamDisk, then enter the ZX mode unless /no-run
LoadTrdIfGiven:
	ld hl,(l8489h)		;85d0	2a 89 84	* . .
	ld a,h			;85d3	7c		|
	or l			;85d4	b5		.
	jp z,l85e6h		;85d5	ca e6 85	. . .
	ld c,0e2h		;85d8	0e e2		. .
	in b,(c)		;85da	ed 40		. @
	push bc			;85dc	c5		.
	call LoadTrdToRamDisk		;85dd	cd 24 86	. $ .
	pop bc			;85e0	c1		.
	out (c),b		;85e1	ed 41		. A
	jp c,ErrorFile		;85e3	da 12 81	. . .
l85e6h:
	ld a,(l8887h)		;85e6	3a 87 88	: . .
	and a			;85e9	a7		.
	ld hl,messages_start	;85ea	21 ec 81	! . .
	jp z,PrintAndExit		;85ed	ca 2a 81	. * .
	jp InstallResetHook		;85f0	c3 31 87	. 1 .

; A = internal code, B = value: patch the port-table entry of port #0000 (page #40, #C400/#C600) to A, OUT (#0000),B, restore the entry
SetCellViaPortTable:
	di			;85f3	f3		.
	ex af,af'		;85f4	08		.
	in a,(0e2h)		;85f5	db e2		. .
	push af			;85f7	f5		.
	ld a,040h		;85f8	3e 40		> @
	out (0e2h),a		;85fa	d3 e2		. .
	ld a,(0c400h)		;85fc	3a 00 c4	: . .
	ld l,a			;85ff	6f		o
	ld a,(0c600h)		;8600	3a 00 c6	: . .
	ld h,a			;8603	67		g
	ex af,af'		;8604	08		.
	ld (0c400h),a		;8605	32 00 c4	2 . .
	ld (0c600h),a		;8608	32 00 c6	2 . .
	ex af,af'		;860b	08		.
	ld a,b			;860c	78		x
	ld bc,00000h		;860d	01 00 00	. . .
	ex af,af'		;8610	08		.
	in a,(c)		;8611	ed 78		. x
	ex af,af'		;8613	08		.
	out (c),a		;8614	ed 79		. y
	ex af,af'		;8616	08		.
	ld b,a			;8617	47		G
	ld a,l			;8618	7d		}
	ld (0c400h),a		;8619	32 00 c4	2 . .
	ld a,h			;861c	7c		|
	ld (0c600h),a		;861d	32 00 c6	2 . .
	pop af			;8620	f1		.
	out (0e2h),a		;8621	d3 e2		. .
	ret			;8623	c9		.

; open the TRD (DSS #11), size by seek-to-end (#15), BIOS #93 free RAM disk E, #92 get N pages for E, read 16 KB per page through window 3, BIOS #C7 next page, BIOS #CB A=0 B=0: RAM disk E -> TR-DOS drive A
LoadTrdToRamDisk:
	ld de,l8907h		;8624	11 07 89	. . .
	ld bc,00080h		;8627	01 80 00	. . .
	ldir			;862a	ed b0		. .
	ld hl,l8907h		;862c	21 07 89	! . .
	ld a,001h		;862f	3e 01		> .
	ld c,011h		;8631	0e 11		. .
	rst 10h			;8633	d7		.
	ret c			;8634	d8		.
	ld (l848bh),a		;8635	32 8b 84	2 . .
	ld a,(l848bh)		;8638	3a 8b 84	: . .
	ld c,015h		;863b	0e 15		. .
	ld b,002h		;863d	06 02		. .
	ld hl,00000h		;863f	21 00 00	! . .
	ld ix,00000h		;8642	dd 21 00 00	. ! . .
	rst 10h			;8646	d7		.
	ret c			;8647	d8		.
	push ix			;8648	dd e5		. .
	pop de			;864a	d1		.
	ld a,d			;864b	7a		z
	add a,a			;864c	87		.
	adc hl,hl		;864d	ed 6a		. j
	add a,a			;864f	87		.
	adc hl,hl		;8650	ed 6a		. j
	ld a,d			;8652	7a		z
	and 03fh		;8653	e6 3f		. ?
	or e			;8655	b3		.
	jr z,l8659h		;8656	28 01		( .
	inc hl			;8658	23		#
l8659h:
	ld a,h			;8659	7c		|
	and a			;865a	a7		.
	jr nz,l86c7h		;865b	20 6a		  j
	ld a,l			;865d	7d		}
	and a			;865e	a7		.
	jr z,l86c7h		;865f	28 66		( f
	push af			;8661	f5		.
	di			;8662	f3		.
	ld a,000h		;8663	3e 00		> .
	ld c,093h		;8665	0e 93		. .
	rst 8			;8667	cf		.
	pop af			;8668	f1		.
	ld b,a			;8669	47		G
	ld a,000h		;866a	3e 00		> .
	ld c,092h		;866c	0e 92		. .
	rst 8			;866e	cf		.
	jr c,l86c7h		;866f	38 56		8 V
	ld (l8482h),a		;8671	32 82 84	2 . .
	ld a,(l848bh)		;8674	3a 8b 84	: . .
	ld c,015h		;8677	0e 15		. .
	ld b,000h		;8679	06 00		. .
	ld hl,00000h		;867b	21 00 00	! . .
	ld ix,00000h		;867e	dd 21 00 00	. ! . .
	rst 10h			;8682	d7		.
	ret c			;8683	d8		.
	ld hl,l8427h		;8684	21 27 84	! ' .
	ld c,05ch		;8687	0e 5c		. \
	rst 10h			;8689	d7		.
	ld a,(l8482h)		;868a	3a 82 84	: . .
l868dh:
	push af			;868d	f5		.
	out (0e2h),a		;868e	d3 e2		. .
	ld a,(l848bh)		;8690	3a 8b 84	: . .
	ld hl,0c000h		;8693	21 00 c0	! . .
	ld de,04000h		;8696	11 00 40	. . @
	ld c,013h		;8699	0e 13		. .
	rst 10h			;869b	d7		.
	jr c,l86c4h		;869c	38 26		8 &
	ld hl,l8480h		;869e	21 80 84	! . .
	ld c,05ch		;86a1	0e 5c		. \
	rst 10h			;86a3	d7		.
	di			;86a4	f3		.
	pop af			;86a5	f1		.
	ld c,0c7h		;86a6	0e c7		. .
	rst 8			;86a8	cf		.
	cp 0ffh			;86a9	fe ff		. .
	jr nz,l868dh		;86ab	20 e0		  .
	ld hl,l8214h		;86ad	21 14 82	! . .
	ld c,05ch		;86b0	0e 5c		. \
	rst 10h			;86b2	d7		.
	ld a,(l848bh)		;86b3	3a 8b 84	: . .
	ld c,012h		;86b6	0e 12		. .
	rst 10h			;86b8	d7		.
	ret c			;86b9	d8		.
	di			;86ba	f3		.
	ld a,000h		;86bb	3e 00		> .
	ld b,000h		;86bd	06 00		. .
	ld c,0cbh		;86bf	0e cb		. .
	rst 8			;86c1	cf		.
	and a			;86c2	a7		.
	ret			;86c3	c9		.
l86c4h:
	pop af			;86c4	f1		.
	jr l86cdh		;86c5	18 06		. .
l86c7h:
	ld hl,l83eah		;86c7	21 ea 83	! . .
	ld c,05ch		;86ca	0e 5c		. \
	rst 10h			;86cc	d7		.
l86cdh:
	ld a,(l848bh)		;86cd	3a 8b 84	: . .
	ld c,012h		;86d0	0e 12		. .
	rst 10h			;86d2	d7		.
	scf			;86d3	37		7
	ret			;86d4	c9		.
	nop			;86d5	00		.

; A = RAM page, HL = name: read up to 16 KB of the file into the page through window 3
ReadFileToPage:
	ld de,l8907h		;86d6	11 07 89	. . .
	ld bc,00080h		;86d9	01 80 00	. . .
	ldir			;86dc	ed b0		. .
	ld hl,l8907h		;86de	21 07 89	! . .
	out (0e2h),a		;86e1	d3 e2		. .
	ld a,001h		;86e3	3e 01		> .
	ld c,011h		;86e5	0e 11		. .
	rst 10h			;86e7	d7		.
	ret c			;86e8	d8		.
	ld (l8702h),a		;86e9	32 02 87	2 . .
	ld a,(l8702h)		;86ec	3a 02 87	: . .
	ld hl,0c000h		;86ef	21 00 c0	! . .
	ld de,04000h		;86f2	11 00 40	. . @
	ld c,013h		;86f5	0e 13		. .
	rst 10h			;86f7	d7		.
	ret c			;86f8	d8		.
	push de			;86f9	d5		.
	ld a,(l8702h)		;86fa	3a 02 87	: . .
	ld c,012h		;86fd	0e 12		. .
	rst 10h			;86ff	d7		.
	pop de			;8700	d1		.
	ret			;8701	c9		.
l8702h:
	nop			;8702	00		.

; reset hook with /ret-fn: back to the DSS text mode and EXIT
ExitToDss:
	di			;8703	f3		.
	ld sp,0bff0h		;8704	31 f0 bf	1 . .
	ld a,004h		;8707	3e 04		> .
	out (03ch),a		;8709	d3 3c		. <
	ld a,(0fff0h)		;870b	3a f0 ff	: . .
	out (082h),a		;870e	d3 82		. .
	ld b,003h		;8710	06 03		. .
	ld a,000h		;8712	3e 00		> .
	ld c,0a6h		;8714	0e a6		. .
	rst 8			;8716	cf		.
	ld a,003h		;8717	3e 03		> .
	ld b,000h		;8719	06 00		. .
	ld c,050h		;871b	0e 50		. P
	rst 10h			;871d	d7		.
	ld c,056h		;871e	0e 56		. V
	ld de,00000h		;8720	11 00 00	. . .
	ld hl,02050h		;8723	21 50 20	! P  
	ld b,007h		;8726	06 07		. .
	ld a,020h		;8728	3e 20		>  
	rst 10h			;872a	d7		.
	ld hl,l8449h		;872b	21 49 84	! I .
	jp PrintAndExit		;872e	c3 2a 81	. * .

; mark page #41 with "ZX" at #FFFE, store windows 0-3 and the return address at #FFF0-#FFF6 for the BIOS reset intercept
InstallResetHook:
	di			;8731	f3		.
	ld a,041h		;8732	3e 41		> A
	out (0e2h),a		;8734	d3 e2		. .
	ld a,05ah		;8736	3e 5a		> Z
	ld (0fffeh),a		;8738	32 fe ff	2 . .
	ld a,058h		;873b	3e 58		> X
	ld (0ffffh),a		;873d	32 ff ff	2 . .
	in a,(082h)		;8740	db 82		. .
	ld (0fff0h),a		;8742	32 f0 ff	2 . .
	in a,(0a2h)		;8745	db a2		. .
	ld (0fff1h),a		;8747	32 f1 ff	2 . .
	in a,(0c2h)		;874a	db c2		. .
	ld (0fff2h),a		;874c	32 f2 ff	2 . .
	in a,(0e2h)		;874f	db e2		. .
	ld (0fff3h),a		;8751	32 f3 ff	2 . .
	ld de,ResetHook		;8754	11 64 87	. d .
	ld (0fff4h),de		;8757	ed 53 f4 ff	. S . .
	ld a,(l8893h)		;875b	3a 93 88	: . .
	ld (0fff6h),a		;875e	32 f6 ff	2 . .
	jp EnterZxMode		;8761	c3 6e 87	. n .

; entered by the BIOS after a reset: /ret-zx -> EnterZxMode again, /ret-fn -> ExitToDss
ResetHook:
	di			;8764	f3		.
	ld a,(0fff6h)		;8765	3a f6 ff	: . .
	or a			;8768	b7		.
	jr z,EnterZxMode		;8769	28 03		( .
	jp ExitToDss		;876b	c3 03 87	. . .

; cells #EE (vROM page), #FE, Covox off, #1FFD = #7FFD = 0, ALL_MODE (#204E) = #FA (/origin: Spectrum waits) or #FE, BIOS FN_SYNC, clear the screen, open the 32x24 window
EnterZxMode:
	ld sp,0bff0h		;876e	31 f0 bf	1 . .
	di			;8771	f3		.
	ld a,004h		;8772	3e 04		> .
	out (07ch),a		;8774	d3 7c		. |
	ld a,041h		;8776	3e 41		> A
	ld b,a			;8778	47		G
	ld a,0eeh		;8779	3e ee		> .
	call SetCellViaPortTable		;877b	cd f3 85	. . .
	ld a,01ch		;877e	3e 1c		> .
	out (07ch),a		;8780	d3 7c		. |
	xor a			;8782	af		.
	out (0feh),a		;8783	d3 fe		. .
	out (089h),a		;8785	d3 89		. .
	out (0c9h),a		;8787	d3 c9		. .
	ld bc,01ffdh		;8789	01 fd 1f	. . .
	out (c),a		;878c	ed 79		. y
	ld bc,07ffdh		;878e	01 fd 7f	. . .
	out (c),a		;8791	ed 79		. y
	ld bc,0204eh		;8793	01 4e 20	. N  
	ld a,(l888bh)		;8796	3a 8b 88	: . .
	and a			;8799	a7		.
	ld a,0fah		;879a	3e fa		> .
	jr nz,l87a0h		;879c	20 02		  .
	ld a,0feh		;879e	3e fe		> .
l87a0h:
	out (c),a		;87a0	ed 79		. y
	ld bc,00078h		;87a2	01 78 00	. x .
	ld a,000h		;87a5	3e 00		> .
	out (c),a		;87a7	ed 79		. y
	ld a,012h		;87a9	3e 12		> .
	ld c,08fh		;87ab	0e 8f		. .
	rst 18h			;87ad	df		.
	ld c,047h		;87ae	0e 47		. G
	ld a,000h		;87b0	3e 00		> .
	rst 18h			;87b2	df		.
	xor a			;87b3	af		.
	ld b,002h		;87b4	06 02		. .
	ld c,0a6h		;87b6	0e a6		. .
	rst 18h			;87b8	df		.
	ld a,(l888bh)		;87b9	3a 8b 88	: . .
	and a			;87bc	a7		.
	jr nz,l87c2h		;87bd	20 03		  .
	ld a,(l8883h)		;87bf	3a 83 88	: . .
l87c2h:
	ld c,0f2h		;87c2	0e f2		. .
	rst 18h			;87c4	df		.
	ld hl,04000h		;87c5	21 00 40	! . @
	ld de,04001h		;87c8	11 01 40	. . @
	ld bc,01affh		;87cb	01 ff 1a	. . .
	ld (hl),l		;87ce	75		u
	ldir			;87cf	ed b0		. .
	ld hl,04104h		;87d1	21 04 41	! . A
	ld bc,00480h		;87d4	01 80 04	. . .
	ld e,000h		;87d7	1e 00		. .
	rst 18h			;87d9	df		.
	ld hl,05104h		;87da	21 04 51	! . Q
	ld bc,00480h		;87dd	01 80 04	. . .
	ld e,000h		;87e0	1e 00		. .
	rst 18h			;87e2	df		.
	xor a			;87e3	af		.
	out (089h),a		;87e4	d3 89		. .
	out (0c9h),a		;87e6	d3 c9		. .
	di			;87e8	f3		.

; Spectrum page n -> physical page n: for n = 0-7 OUT #7FFD,n then OUT (#E2),n (cell #F0+n); with #1FFD = #10 pages 8-15; copy the start stub to #FF00
InitSpectrumPages:
	ld a,000h		;87e9	3e 00		> .
	out (082h),a		;87eb	d3 82		. .
	ld a,005h		;87ed	3e 05		> .
	out (0a2h),a		;87ef	d3 a2		. .
	xor a			;87f1	af		.
	ld bc,01ffdh		;87f2	01 fd 1f	. . .
	out (c),a		;87f5	ed 79		. y
	ld b,07fh		;87f7	06 7f		. .
l87f9h:
	out (c),a		;87f9	ed 79		. y
	out (0e2h),a		;87fb	d3 e2		. .
	inc a			;87fd	3c		<
	cp 008h			;87fe	fe 08		. .
	jr nz,l87f9h		;8800	20 f7		  .
	ld b,01fh		;8802	06 1f		. .
	ld a,010h		;8804	3e 10		> .
	out (c),a		;8806	ed 79		. y
	ld a,008h		;8808	3e 08		> .
	ld b,07fh		;880a	06 7f		. .
l880ch:
	out (c),a		;880c	ed 79		. y
	out (0e2h),a		;880e	d3 e2		. .
	inc a			;8810	3c		<
	cp 010h			;8811	fe 10		. .
	jr nz,l880ch		;8813	20 f7		  .
	xor a			;8815	af		.
	out (c),a		;8816	ed 79		. y
	ld b,01fh		;8818	06 1f		. .
	out (c),a		;881a	ed 79		. y
	ld hl,StartStub		;881c	21 4b 88	! K .
	ld bc,00100h		;881f	01 00 01	. . .
	ld de,0ff00h		;8822	11 00 ff	. . .
	ldir			;8825	ed b0		. .
	ld a,(l886bh)		;8827	3a 6b 88	: k .
	out (0bdh),a		;882a	d3 bd		. .
	ld a,(l8873h)		;882c	3a 73 88	: s .
	ld bc,07ffdh		;882f	01 fd 7f	. . .
	out (c),a		;8832	ed 79		. y
	ld a,(l8867h)		;8834	3a 67 88	: g .
	ld hl,(l886fh)		;8837	2a 6f 88	* o .
	add a,l			;883a	85		.
	ld hl,(l8877h)		;883b	2a 77 88	* w .
	add a,l			;883e	85		.
	ld hl,(l887bh)		;883f	2a 7b 88	* { .
	add a,l			;8842	85		.
	ld e,a			;8843	5f		_
	ld a,(l887fh)		;8844	3a 7f 88	: . .
	ld d,a			;8847	57		W
	jp 0ff00h		;8848	c3 00 ff	. . .

; runs at #FF00: SYS/CNF port (#3C) = E (turbo, ports), window 1 = page 2, D = 0 -> JP 0 (BASIC 128), else #7FFD = #10 and JP #3D29 (TR-DOS, /to-trdos)
StartStub:
	ld a,e			;884b	7b		{
	out (03ch),a		;884c	d3 3c		. <
	ld a,002h		;884e	3e 02		> .
	out (0c2h),a		;8850	d3 c2		. .
	ld a,d			;8852	7a		z
	and a			;8853	a7		.
	jp z,00000h		;8854	ca 00 00	. . .
	ld a,010h		;8857	3e 10		> .
	ld bc,07ffdh		;8859	01 fd 7f	. . .
	out (c),a		;885c	ed 79		. y
	ld hl,00000h		;885e	21 00 00	! . .
	push hl			;8861	e5		.
	jp 03d29h		;8862	c3 29 3d	. ) =

; BLOCK 'tail' (start 0x8865 end 0x8988)
tail_start:

; option flags, option words, file names, buffers
; #8865-#8894: option table, 4 bytes per entry: word pointer, value, value when given (ParseOptions copies byte 3 to byte 2)
; turbo #02/#03, lines312 #41/#61, sprinter #0C/#04, 7FFD #30/#00, 1FFD #40/#00, mem512 #00/#80, int-sc #00/#01,
; to-trdos #02/#01, no-run #FF/#00, origin #00/#03, ret-zx #00/#41, ret-fn #00/#41; SYS byte E = turbo+sprinter+1FFD+mem512 (README)
Variables:
	defb 099h		;8865	99		.
	defb 088h		;8866	88		.
l8867h:
	defb 002h		;8867	02		.
	defb 003h		;8868	03		.
	defb 0a0h		;8869	a0		.
	defb 088h		;886a	88		.
l886bh:
	defb 041h		;886b	41		A
	defb 061h		;886c	61		a
	defb 0aah		;886d	aa		.
	defb 088h		;886e	88		.
l886fh:
	defb 00ch		;886f	0c		.
	defb 004h		;8870	04		.
	defb 0b4h		;8871	b4		.
	defb 088h		;8872	88		.
l8873h:
	defb 030h		;8873	30		0
	defb 000h		;8874	00		.
	defb 0bah		;8875	ba		.
	defb 088h		;8876	88		.
l8877h:
	defb 040h		;8877	40		@
	defb 000h		;8878	00		.
	defb 0c0h		;8879	c0		.
	defb 088h		;887a	88		.
l887bh:
	defb 000h		;887b	00		.
	defb 080h		;887c	80		.
	defb 0d0h		;887d	d0		.
	defb 088h		;887e	88		.
l887fh:
	defb 000h		;887f	00		.
	defb 001h		;8880	01		.
	defb 0c8h		;8881	c8		.
	defb 088h		;8882	88		.
l8883h:
	defb 002h		;8883	02		.
	defb 001h		;8884	01		.
	defb 0dah		;8885	da		.
	defb 088h		;8886	88		.
l8887h:
	defb 0ffh		;8887	ff		.
	defb 000h		;8888	00		.
	defb 0e2h		;8889	e2		.
	defb 088h		;888a	88		.
l888bh:
	defb 000h		;888b	00		.
	defb 003h		;888c	03		.
	defb 0eah		;888d	ea		.
	defb 088h		;888e	88		.
	defb 000h		;888f	00		.
	defb 041h		;8890	41		A
	defb 0f2h		;8891	f2		.
	defb 088h		;8892	88		.
l8893h:
	defb 000h		;8893	00		.
	defb 041h		;8894	41		A
	defb 000h		;8895	00		.
	defb 000h		;8896	00		.
	defb 000h		;8897	00		.
	defb 000h		;8898	00		.
	defb 074h		;8899	74		t
	defb 075h		;889a	75		u
	defb 072h		;889b	72		r
	defb 062h		;889c	62		b
	defb 06fh		;889d	6f		o
	defb 0ffh		;889e	ff		.
	defb 000h		;889f	00		.
	defb 06ch		;88a0	6c		l
	defb 069h		;88a1	69		i
	defb 06eh		;88a2	6e		n
	defb 065h		;88a3	65		e
	defb 073h		;88a4	73		s
	defb 033h		;88a5	33		3
	defb 031h		;88a6	31		1
	defb 032h		;88a7	32		2
	defb 0ffh		;88a8	ff		.
	defb 000h		;88a9	00		.
	defb 073h		;88aa	73		s
	defb 070h		;88ab	70		p
	defb 072h		;88ac	72		r
	defb 069h		;88ad	69		i
	defb 06eh		;88ae	6e		n
	defb 074h		;88af	74		t
	defb 065h		;88b0	65		e
	defb 072h		;88b1	72		r
	defb 0ffh		;88b2	ff		.
	defb 000h		;88b3	00		.
	defb 037h		;88b4	37		7
	defb 046h		;88b5	46		F
	defb 046h		;88b6	46		F
	defb 044h		;88b7	44		D
	defb 0ffh		;88b8	ff		.
	defb 000h		;88b9	00		.
	defb 031h		;88ba	31		1
	defb 046h		;88bb	46		F
	defb 046h		;88bc	46		F
	defb 044h		;88bd	44		D
	defb 0ffh		;88be	ff		.
	defb 000h		;88bf	00		.
	defb 06dh		;88c0	6d		m
	defb 065h		;88c1	65		e
	defb 06dh		;88c2	6d		m
	defb 035h		;88c3	35		5
	defb 031h		;88c4	31		1
	defb 032h		;88c5	32		2
	defb 0ffh		;88c6	ff		.
	defb 000h		;88c7	00		.
	defb 069h		;88c8	69		i
	defb 06eh		;88c9	6e		n
	defb 074h		;88ca	74		t
	defb 02dh		;88cb	2d		-
	defb 073h		;88cc	73		s
	defb 063h		;88cd	63		c
	defb 0ffh		;88ce	ff		.
	defb 000h		;88cf	00		.
	defb 074h		;88d0	74		t
	defb 06fh		;88d1	6f		o
	defb 02dh		;88d2	2d		-
	defb 074h		;88d3	74		t
	defb 072h		;88d4	72		r
	defb 064h		;88d5	64		d
	defb 06fh		;88d6	6f		o
	defb 073h		;88d7	73		s
	defb 0ffh		;88d8	ff		.
	defb 000h		;88d9	00		.
	defb 06eh		;88da	6e		n
	defb 06fh		;88db	6f		o
	defb 02dh		;88dc	2d		-
	defb 072h		;88dd	72		r
	defb 075h		;88de	75		u
	defb 06eh		;88df	6e		n
	defb 0ffh		;88e0	ff		.
	defb 000h		;88e1	00		.
	defb 06fh		;88e2	6f		o
	defb 072h		;88e3	72		r
	defb 069h		;88e4	69		i
	defb 067h		;88e5	67		g
	defb 069h		;88e6	69		i
	defb 06eh		;88e7	6e		n
	defb 0ffh		;88e8	ff		.
	defb 000h		;88e9	00		.
	defb 072h		;88ea	72		r
	defb 065h		;88eb	65		e
	defb 074h		;88ec	74		t
	defb 02dh		;88ed	2d		-
	defb 07ah		;88ee	7a		z
	defb 078h		;88ef	78		x
	defb 0ffh		;88f0	ff		.
	defb 000h		;88f1	00		.
	defb 072h		;88f2	72		r
	defb 065h		;88f3	65		e
	defb 074h		;88f4	74		t
	defb 02dh		;88f5	2d		-
	defb 066h		;88f6	66		f
	defb 06eh		;88f7	6e		n
	defb 0ffh		;88f8	ff		.
	defb 000h		;88f9	00		.
l88fah:
	defb 053h		;88fa	53		S
	defb 050h		;88fb	50		P
	defb 045h		;88fc	45		E
	defb 043h		;88fd	43		C
	defb 054h		;88fe	54		T
	defb 052h		;88ff	52		R
	defb 055h		;8900	55		U
	defb 04dh		;8901	4d		M
	defb 02eh		;8902	2e		.
	defb 043h		;8903	43		C
	defb 046h		;8904	46		F
	defb 047h		;8905	47		G
	defb 000h		;8906	00		.
l8907h:
	defb 000h		;8907	00		.
	defb 000h		;8908	00		.
	defb 000h		;8909	00		.
	defb 000h		;890a	00		.
	defb 000h		;890b	00		.
	defb 000h		;890c	00		.
	defb 000h		;890d	00		.
	defb 000h		;890e	00		.
	defb 000h		;890f	00		.
	defb 000h		;8910	00		.
	defb 000h		;8911	00		.
	defb 000h		;8912	00		.
	defb 000h		;8913	00		.
	defb 000h		;8914	00		.
	defb 000h		;8915	00		.
	defb 000h		;8916	00		.
	defb 000h		;8917	00		.
	defb 000h		;8918	00		.
	defb 000h		;8919	00		.
	defb 000h		;891a	00		.
	defb 000h		;891b	00		.
	defb 000h		;891c	00		.
	defb 000h		;891d	00		.
	defb 000h		;891e	00		.
	defb 000h		;891f	00		.
	defb 000h		;8920	00		.
	defb 000h		;8921	00		.
	defb 000h		;8922	00		.
	defb 000h		;8923	00		.
	defb 000h		;8924	00		.
	defb 000h		;8925	00		.
	defb 000h		;8926	00		.
	defb 000h		;8927	00		.
	defb 000h		;8928	00		.
	defb 000h		;8929	00		.
	defb 000h		;892a	00		.
	defb 000h		;892b	00		.
	defb 000h		;892c	00		.
	defb 000h		;892d	00		.
	defb 000h		;892e	00		.
	defb 000h		;892f	00		.
	defb 000h		;8930	00		.
	defb 000h		;8931	00		.
	defb 000h		;8932	00		.
	defb 000h		;8933	00		.
	defb 000h		;8934	00		.
	defb 000h		;8935	00		.
	defb 000h		;8936	00		.
	defb 000h		;8937	00		.
	defb 000h		;8938	00		.
	defb 000h		;8939	00		.
	defb 000h		;893a	00		.
	defb 000h		;893b	00		.
	defb 000h		;893c	00		.
	defb 000h		;893d	00		.
	defb 000h		;893e	00		.
	defb 000h		;893f	00		.
	defb 000h		;8940	00		.
	defb 000h		;8941	00		.
	defb 000h		;8942	00		.
	defb 000h		;8943	00		.
	defb 000h		;8944	00		.
	defb 000h		;8945	00		.
	defb 000h		;8946	00		.
	defb 000h		;8947	00		.
	defb 000h		;8948	00		.
	defb 000h		;8949	00		.
	defb 000h		;894a	00		.
	defb 000h		;894b	00		.
	defb 000h		;894c	00		.
	defb 000h		;894d	00		.
	defb 000h		;894e	00		.
	defb 000h		;894f	00		.
	defb 000h		;8950	00		.
	defb 000h		;8951	00		.
	defb 000h		;8952	00		.
	defb 000h		;8953	00		.
	defb 000h		;8954	00		.
	defb 000h		;8955	00		.
	defb 000h		;8956	Warning: Code might not be 8080 compatible!
00		.
	defb 000h		;8957	00		.
	defb 000h		;8958	00		.
	defb 000h		;8959	00		.
	defb 000h		;895a	00		.
	defb 000h		;895b	00		.
	defb 000h		;895c	00		.
	defb 000h		;895d	00		.
	defb 000h		;895e	00		.
	defb 000h		;895f	00		.
	defb 000h		;8960	00		.
	defb 000h		;8961	00		.
	defb 000h		;8962	00		.
	defb 000h		;8963	00		.
	defb 000h		;8964	00		.
	defb 000h		;8965	00		.
	defb 000h		;8966	00		.
	defb 000h		;8967	00		.
	defb 000h		;8968	00		.
	defb 000h		;8969	00		.
	defb 000h		;896a	00		.
	defb 000h		;896b	00		.
	defb 000h		;896c	00		.
	defb 000h		;896d	00		.
	defb 000h		;896e	00		.
	defb 000h		;896f	00		.
	defb 000h		;8970	00		.
	defb 000h		;8971	00		.
	defb 000h		;8972	00		.
	defb 000h		;8973	00		.
	defb 000h		;8974	00		.
	defb 000h		;8975	00		.
	defb 000h		;8976	00		.
	defb 000h		;8977	00		.
	defb 000h		;8978	00		.
	defb 000h		;8979	00		.
	defb 000h		;897a	00		.
	defb 000h		;897b	00		.
	defb 000h		;897c	00		.
	defb 000h		;897d	00		.
	defb 000h		;897e	00		.
	defb 000h		;897f	00		.
	defb 000h		;8980	00		.
	defb 000h		;8981	00		.
	defb 000h		;8982	00		.
	defb 000h		;8983	00		.
	defb 000h		;8984	00		.
	defb 000h		;8985	00		.
	defb 000h		;8986	00		.
	defb 000h		;8987	00		.
tail_end:

