; ==========================================================================
; WildPlayer RAM bank 4: UniPT2 engine (PT2 format support)
; ==========================================================================

	org 0xC000

; --------------------------------------------------------------------------
; symbol table - named anchors (auto labels appear inline at use sites)
; --------------------------------------------------------------------------
UNIPT2_ENGINE           : equ 0xC000
UNIPT2_SIG              : equ 0xCABB

;Second music engine: UniPT2 player (see '=UniPT2/-layer ' string
;at 0xCABB). WildPlayer supports PT3 (VTII) and PT2 (UniPT2); the
;right engine is installed per module type.
UNIPT2_ENGINE:
	jr R_C020		;c000	18 1e		. .
	jr nz,$-61		;c002	20 c1		  .
	and (hl)		;c004	a6		.
	call nz,sub_caa1h	;c005	c4 a1 ca	. . .
M_C008:
	ld l,h			;c008	6c		l
R_C009:
	jp nc,0d907h		;c009	d2 07 d9	. . .
	rst 0			;c00c	c7		.
	rst 18h			;c00d	df		.
R_C00E:
	sub 0e4h		;c00e	d6 e4		. .
	inc bc			;c010	03		.
	jp (hl)			;c011	e9		.
	ld a,(hl)		;c012	7e		~
	defb 0edh ;next byte illegal after ed	;c013	ed		.
	ret pe			;c014	e8		.
M_C015:
	pop af			;c015	f1		.
	pop af			;c016	f1		.
	push af			;c017	f5		.
	ret pe			;c018	e8		.
	or 06fh			;c019	f6 6f		. o
	call m,00000h		;c01b	fc 00 00	. . .
	nop			;c01e	00		.
	nop			;c01f	00		.
R_C020:
	ld ix,D_FFF4		;c020	dd 21 f4 ff	. ! . .
	add ix,sp		;c024	dd 39		. 9
	push de			;c026	d5		.
	ld sp,hl		;c027	f9		.
	pop bc			;c028	c1		.
	ex de,hl		;c029	eb		.
M_C02A:
	pop bc			;c02a	c1		.
R_C02B:
	dec bc			;c02b	0b		.
	add hl,bc		;c02c	09		.
	ex de,hl		;c02d	eb		.
	pop bc			;c02e	c1		.
	dec bc			;c02f	0b		.
	add hl,bc		;c030	09		.
	sbc hl,de		;c031	ed 52		. R
M_C033:
	add hl,de		;c033	19		.
	jr c,lc038h		;c034	38 02		8 .
	ld d,h			;c036	54		T
	ld e,l			;c037	5d		]
lc038h:
	lddr			;c038	ed b8		. .
	ex de,hl		;c03a	eb		.
	ld d,(ix+00bh)		;c03b	dd 56 0b	. V .
	ld e,(ix+00ah)		;c03e	dd 5e 0a	. ^ .
	ld sp,hl		;c041	f9		.
	pop hl			;c042	e1		.
	pop hl			;c043	e1		.
	pop hl			;c044	e1		.
	ld b,006h		;c045	06 06		. .
lc047h:
	dec sp			;c047	3b		;
	pop af			;c048	f1		.
D_C049:
	ld (ix+006h),a		;c049	dd 77 06	. w .
	inc ix			;c04c	dd 23		. #
R_C04E:
	djnz lc047h		;c04e	10 f7		. .
	exx			;c050	d9		.
	ld d,0bfh		;c051	16 bf		. .
	ld bc,01010h		;c053	01 10 10	. . .
	pop hl			;c056	e1		.
lc057h:
	dec sp			;c057	3b		;
	pop af			;c058	f1		.
	exx			;c059	d9		.
lc05ah:
	ld (de),a		;c05a	12		.
	inc de			;c05b	13		.
lc05ch:
	exx			;c05c	d9		.
lc05dh:
	add hl,hl		;c05d	29		)
	djnz lc062h		;c05e	10 02		. .
	pop hl			;c060	e1		.
	ld b,c			;c061	41		A
lc062h:
	jr c,lc057h		;c062	38 f3		8 .
	ld e,001h		;c064	1e 01		. .
lc066h:
	ld a,080h		;c066	3e 80		> .
lc068h:
	add hl,hl		;c068	29		)
	djnz lc06dh		;c069	10 02		. .
M_C06B:
	pop hl			;c06b	e1		.
	ld b,c			;c06c	41		A
lc06dh:
	rla			;c06d	17		.
	jr c,lc068h		;c06e	38 f8		8 .
	cp 003h			;c070	fe 03		. .
	jr c,lc079h		;c072	38 05		8 .
	add a,e			;c074	83		.
	ld e,a			;c075	5f		_
	xor c			;c076	a9		.
	jr nz,lc066h		;c077	20 ed		  .
lc079h:
	add a,e			;c079	83		.
	cp 004h			;c07a	fe 04		. .
	jr z,lc0d8h		;c07c	28 5a		( Z
	adc a,0ffh		;c07e	ce ff		. .
	cp 002h			;c080	fe 02		. .
	exx			;c082	d9		.
lc083h:
	ld c,a			;c083	4f		O
lc084h:
	exx			;c084	d9		.
	ld a,0bfh		;c085	3e bf		> .
	jr c,lc09dh		;c087	38 14		8 .
lc089h:
	add hl,hl		;c089	29		)
	djnz lc08eh		;c08a	10 02		. .
	pop hl			;c08c	e1		.
	ld b,c			;c08d	41		A
lc08eh:
	rla			;c08e	17		.
	jr c,lc089h		;c08f	38 f8		8 .
	jr z,lc098h		;c091	28 05		( .
	inc a			;c093	3c		<
	add a,d			;c094	82		.
	jr nc,lc09fh		;c095	30 08		0 .
R_C097:
	sub d			;c097	92		.
lc098h:
	inc a			;c098	3c		<
	jr nz,lc0a7h		;c099	20 0c		  .
	ld a,0efh		;c09b	3e ef		> .
lc09dh:
	rrca			;c09d	0f		.
	cp a			;c09e	bf		.
lc09fh:
	add hl,hl		;c09f	29		)
M_C0A0:
	djnz lc0a4h		;c0a0	10 02		. .
	pop hl			;c0a2	e1		.
	ld b,c			;c0a3	41		A
lc0a4h:
	rla			;c0a4	17		.
	jr c,lc09fh		;c0a5	38 f8		8 .
lc0a7h:
	exx			;c0a7	d9		.
	ld h,0ffh		;c0a8	26 ff		& .
D_C0AA:
	jr z,lc0b2h		;c0aa	28 06		( .
	ld h,a			;c0ac	67		g
	dec sp			;c0ad	3b		;
	inc a			;c0ae	3c		<
	jr z,lc0bdh		;c0af	28 0c		( .
	pop af			;c0b1	f1		.
lc0b2h:
	ld l,a			;c0b2	6f		o
M_C0B3:
	add hl,de		;c0b3	19		.
	ldir			;c0b4	ed b0		. .
M_C0B6:
	jr lc05ch		;c0b6	18 a4		. .
lc0b8h:
	exx			;c0b8	d9		.
	rrc d			;c0b9	cb 0a		. .
	jr lc05dh		;c0bb	18 a0		. .
lc0bdh:
	pop af			;c0bd	f1		.
	cp 0e0h			;c0be	fe e0		. .
	jr c,lc0b2h		;c0c0	38 f0		8 .
	rlca			;c0c2	07		.
	xor c			;c0c3	a9		.
	inc a			;c0c4	3c		<
	jr z,lc0b8h		;c0c5	28 f1		( .
	sub 010h		;c0c7	d6 10		. .
M_C0C9:
	ld l,a			;c0c9	6f		o
	ld c,a			;c0ca	4f		O
	ld h,0ffh		;c0cb	26 ff		& .
	add hl,de		;c0cd	19		.
	ldi			;c0ce	ed a0		. .
	dec sp			;c0d0	3b		;
	pop af			;c0d1	f1		.
	ld (de),a		;c0d2	12		.
lc0d3h:
	inc hl			;c0d3	23		#
	inc de			;c0d4	13		.
	ld a,(hl)		;c0d5	7e		~
	jr lc05ah		;c0d6	18 82		. .
lc0d8h:
	ld a,080h		;c0d8	3e 80		> .
M_C0DA:
	add hl,hl		;c0da	29		)
M_C0DB:
	djnz R_C0DF		;c0db	10 02		. .
	pop hl			;c0dd	e1		.
	ld b,c			;c0de	41		A
R_C0DF:
	adc a,a			;c0df	8f		.
	jr nz,lc0fbh		;c0e0	20 19		  .
	jr c,M_C0DA		;c0e2	38 f6		8 .
	ld a,0fch		;c0e4	3e fc		> .
	jr lc0feh		;c0e6	18 16		. .
R_C0E8:
	dec sp			;c0e8	3b		;
	pop bc			;c0e9	c1		.
	ld c,b			;c0ea	48		H
	ld b,a			;c0eb	47		G
	ccf			;c0ec	3f		?
	jr lc084h		;c0ed	18 95		. .
lc0efh:
	cp 00fh			;c0ef	fe 0f		. .
R_C0F1:
	jr c,R_C0E8		;c0f1	38 f5		8 .
	jr nz,lc083h		;c0f3	20 8e		  .
	add a,0f4h		;c0f5	c6 f4		. .
	ld sp,ix		;c0f7	dd f9		. .
D_C0F9:
	jr lc10fh		;c0f9	18 14		. .
lc0fbh:
	sbc a,a			;c0fb	9f		.
	ld a,0efh		;c0fc	3e ef		> .
lc0feh:
	add hl,hl		;c0fe	29		)
	djnz lc103h		;c0ff	10 02		. .
	pop hl			;c101	e1		.
	ld b,c			;c102	41		A
lc103h:
	rla			;c103	17		.
	jr c,lc0feh		;c104	38 f8		8 .
	exx			;c106	d9		.
	jr nz,M_C0C9		;c107	20 c0		  .
	bit 7,a			;c109	cb 7f		. .
M_C10B:
	jr z,lc0efh		;c10b	28 e2		( .
	sub 0eah		;c10d	d6 ea		. .
lc10fh:
	ex de,hl		;c10f	eb		.
lc110h:
	pop de			;c110	d1		.
	ld (hl),e		;c111	73		s
	inc hl			;c112	23		#
M_C113:
	ld (hl),d		;c113	72		r
	inc hl			;c114	23		#
	dec a			;c115	3d		=
	jr nz,lc110h		;c116	20 f8		  .
	ex de,hl		;c118	eb		.
	jr nc,M_C0B6		;c119	30 9b		0 .
	ld hl,02758h		;c11b	21 58 27	! X '
	exx			;c11e	d9		.
	ret			;c11f	c9		.
	ld c,b			;c120	48		H
	ld d,d			;c121	52		R
	inc a			;c122	3c		<
M_C123:
	inc b			;c123	04		.
	add a,(hl)		;c124	86		.
	inc bc			;c125	03		.
	xor e			;c126	ab		.
	dec a			;c127	3d		=
	jp p,lc42fh		;c128	f2 2f c4	. / .
	ret			;c12b	c9		.
	cp a			;c12c	bf		.
	ret m			;c12d	f8		.
	ld hl,D_C43C		;c12e	21 3c c4	! < .
	jp R_C009		;c131	c3 09 c0	. . .
	ld b,h			;c134	44		D
R_C135:
	pop bc			;c135	c1		.
	di			;c136	f3		.
	ld a,(hl)		;c137	7e		~
	adc a,e			;c138	8b		.
	add a,c			;c139	81		.
	ld (02278h),a		;c13a	32 78 22	2 x "
	cp e			;c13d	bb		.
	call pe,023c4h		;c13e	ec c4 23	. . #
	call 01ab5h		;c141	cd b5 1a	. . .
	inc de			;c144	13		.
	inc a			;c145	3c		<
	ld de,07a9fh		;c146	11 9f 7a	. . z
	ld (03e70h),de		;c149	ed 53 70 3e	. S p >
	ld d,e			;c14d	53		S
	ld l,l			;c14e	6d		m
	ld l,c			;c14f	69		i
	ld (hl),d		;c150	72		r
	call m,09dd5h		;c151	fc d5 9d	. . .
	adc a,b			;c154	88		.
	ld (hl),h		;c155	74		t
	ld hl,0001bh		;c156	21 1b 00	! . .
	and a			;c159	a7		.
	sub (hl)		;c15a	96		.
	cp d			;c15b	ba		.
	ex de,hl		;c15c	eb		.
	push af			;c15d	f5		.
	halt			;c15e	76		v
	ld hl,034fah		;c15f	21 fa 34	! . 4
	add a,c			;c162	81		.
	out (07bh),a		;c163	d3 7b		. {
	adc a,e			;c165	8b		.
D_C166:
	adc a,e			;c166	8b		.
	add a,d			;c167	82		.
	ld de,09983h		;c168	11 83 99	. . .
	defb 0fdh,001h,02ch ;illegal sequence	;c16b	fd 01 2c	. . ,
R_C16E:
	nop			;c16e	00		.
	ld (hl),b		;c16f	70		p
	ldir			;c170	ed b0		. .
	pop hl			;c172	e1		.
R_C173:
	sub (hl)		;c173	96		.
M_C174:
	sbc a,d			;c174	9a		.
	ld hl,D_F9AF		;c175	21 af f9	! . .
R_C178:
	xor a			;c178	af		.
	dec a			;c179	3d		=
	ex de,hl		;c17a	eb		.
	adc a,e			;c17b	8b		.
M_C17C:
	sbc a,l			;c17c	9d		.
	ld (hl),l		;c17d	75		u
	sub l			;c17e	95		.
	cp d			;c17f	ba		.
	exx			;c180	d9		.
	sbc a,a			;c181	9f		.
	ld a,001h		;c182	3e 01		> .
	ld a,c			;c184	79		y
	inc hl			;c185	23		#
	sub 059h		;c186	d6 59		. Y
	push af			;c188	f5		.
	adc a,c			;c189	89		.
	sub e			;c18a	93		.
	call m,09d77h		;c18b	fc 77 9d	. w .
	call R_C41F		;c18e	cd 1f c4	. . .
	ei			;c191	fb		.
	ret			;c192	c9		.
	ld (hl),a		;c193	77		w
	pop af			;c194	f1		.
	adc a,a			;c195	8f		.
	ret p			;c196	f0		.
	ld l,a			;c197	6f		o
	inc de			;c198	13		.
	jp p,0ee5eh		;c199	f2 5e ee	. ^ .
	ld b,004h		;c19c	06 04		. .
	inc c			;c19e	0c		.
R_C19F:
	call c,07898h		;c19f	dc 98 78	. . x
	ld b,h			;c1a2	44		D
	di			;c1a3	f3		.
	rst 8			;c1a4	cf		.
	rst 38h			;c1a5	ff		.
	nop			;c1a6	00		.
	rst 30h			;c1a7	f7		.
	di			;c1a8	f3		.
	inc bc			;c1a9	03		.
R_C1AA:
	ld e,a			;c1aa	5f		_
	ld l,0feh		;c1ab	2e fe		. .
	xor 090h		;c1ad	ee 90		. .
	pop af			;c1af	f1		.
	dec e			;c1b0	1d		.
	ld (bc),a		;c1b1	02		.
	ld bc,00e03h		;c1b2	01 03 0e	. . .
	ld c,(hl)		;c1b5	4e		N
	ret p			;c1b6	f0		.
	dec h			;c1b7	25		%
	xor e			;c1b8	ab		.
D_C1B9:
	rst 38h			;c1b9	ff		.
M_C1BA:
	ccf			;c1ba	3f		?
	inc e			;c1bb	1c		.
	sbc a,l			;c1bc	9d		.
	add hl,sp		;c1bd	39		9
	call m,0fff6h		;c1be	fc f6 ff	. . .
	ld (bc),a		;c1c1	02		.
	ret m			;c1c2	f8		.
	ld c,0b0h		;c1c3	0e b0		. .
	ld b,01fh		;c1c5	06 1f		. .
	or 011h			;c1c7	f6 11		. .
R_C1C9:
	ld sp,hl		;c1c9	f9		.
	inc (hl)		;c1ca	34		4
	ex af,af'		;c1cb	08		.
	add a,l			;c1cc	85		.
	exx			;c1cd	d9		.
	ld (hl),c		;c1ce	71		q
	di			;c1cf	f3		.
	cp (hl)			;c1d0	be		.
	ret z			;c1d1	c8		.
	add hl,bc		;c1d2	09		.
	jp 05e9dh		;c1d3	c3 9d 5e	. . ^
D_C1D6:
	inc hl			;c1d6	23		#
	ld d,(hl)		;c1d7	56		V
	ld e,0beh		;c1d8	1e be		. .
	ex de,hl		;c1da	eb		.
	ld bc,D_EE43		;c1db	01 43 ee	. C .
	add hl,bc		;c1de	09		.
	ret			;c1df	c9		.
	ld d,05ch		;c1e0	16 5c		. \
	ld l,h			;c1e2	6c		l
	push bc			;c1e3	c5		.
	add a,a			;c1e4	87		.
	add a,e			;c1e5	83		.
	add ix,de		;c1e6	dd 19		. .
	call m,07e6fh		;c1e8	fc 6f 7e	. o ~
	ld bc,07fcbh		;c1eb	01 cb 7f	. . .
D_C1EE:
	ld c,010h		;c1ee	0e 10		. .
	jp nz,lc0d3h		;c1f0	c2 d3 c0	. . .
	ld c,d			;c1f3	4a		J
	call z,0770eh		;c1f4	cc 0e 77	. . w
	ld b,002h		;c1f7	06 02		. .
R_C1F9:
	add hl,sp		;c1f9	39		9
	ld a,a			;c1fa	7f		.
	in a,(042h)		;c1fb	db 42		. B
	and 01fh		;c1fd	e6 1f		. .
sub_c1ffh:
	ld h,a			;c1ff	67		g
	ld e,(ix+002h)		;c200	dd 5e 02	. ^ .
	xor a			;c203	af		.
	cp 000h			;c204	fe 00		. .
	push af			;c206	f5		.
	and 0f0h		;c207	e6 f0		. .
	rrca			;c209	0f		.
	add hl,bc		;c20a	09		.
	ret po			;c20b	e0		.
	ld d,a			;c20c	57		W
	pop af			;c20d	f1		.
	daa			;c20e	27		'
	ld (hl),06fh		;c20f	36 6f		6 o
	jp p,06ecbh		;c211	f2 cb 6e	. . n
	ret z			;c214	c8		.
	jp po,07ec9h		;c215	e2 c9 7e	. . ~
	di			;c218	f3		.
	ld a,(M_C0A0)		;c219	3a a0 c0	: . .
	ld c,a			;c21c	4f		O
	ld hl,0be1ch		;c21d	21 1c be	! . .
	jp c,0c105h		;c220	da 05 c1	. . .
	xor a			;c223	af		.
	ld c,a			;c224	4f		O
	bit 4,e			;c225	cb 63		. c
	ld (de),a		;c227	12		.
	ld l,c			;c228	69		i
	ld (hl),d		;c229	72		r
	di			;c22a	f3		.
	ld h,000h		;c22b	26 00		& .
	add hl,hl		;c22d	29		)
	ld de,(0190eh)		;c22e	ed 5b 0e 19	. [ . .
	ld c,(hl)		;c232	4e		N
	inc hl			;c233	23		#
	or e			;c234	b3		.
	ld sp,hl		;c235	f9		.
	or 044h			;c236	f6 44		. D
	jp 02a79h		;c238	c3 79 2a	. y *
	ld (de),a		;c23b	12		.
M_C23C:
	ld bc,0a107h		;c23c	01 07 a1	. . .
	add hl,de		;c23f	19		.
	ld (de),a		;c240	12		.
	sub e			;c241	93		.
	rst 28h			;c242	ef		.
	ex de,hl		;c243	eb		.
	adc a,h			;c244	8c		.
	inc b			;c245	04		.
	inc d			;c246	14		.
	sub (hl)		;c247	96		.
	scf			;c248	37		7
	ld a,l			;c249	7d		}
	ld a,h			;c24a	7c		|
	ld h,d			;c24b	62		b
	ld a,a			;c24c	7f		.
	ret			;c24d	c9		.
	dec (ix+002h)		;c24e	dd 35 02	. 5 .
	ret p			;c251	f0		.
	ld h,h			;c252	64		d
	sub 0a4h		;c253	d6 a4		. .
	rst 38h			;c255	ff		.
	ld (hl),a		;c256	77		w
	ld sp,0b4c6h		;c257	31 c6 b4	1 . .
	ld a,(de)		;c25a	1a		.
	dec bc			;c25b	0b		.
	adc a,h			;c25c	8c		.
	ld sp,hl		;c25d	f9		.
	jp nz,02a8eh		;c25e	c2 8e 2a	. . *
	bit 7,b			;c261	cb 78		. x
	sbc a,0fch		;c263	de fc		. .
	ld ix,D_D984		;c265	dd 21 84 d9	. ! . .
	add hl,sp		;c269	39		9
	pop bc			;c26a	c1		.
	jp p,07c6fh		;c26b	f2 6f 7c	. o |
	ld hl,(07be4h)		;c26e	2a e4 7b	* . {
	ld a,(hl)		;c271	7e		~
	inc a			;c272	3c		<
	call z,061f8h		;c273	cc f8 61	. . a
	dec (hl)		;c276	35		5
	add hl,sp		;c277	39		9
lc278h:
	jr z,lc278h		;c278	28 fe		( .
	sbc a,b			;c27a	98		.
	rst 8			;c27b	cf		.
	adc a,e			;c27c	8b		.
	ld l,(hl)		;c27d	6e		n
	adc a,(hl)		;c27e	8e		.
	ld d,d			;c27f	52		R
	add hl,sp		;c280	39		9
	add a,d			;c281	82		.
	or l			;c282	b5		.
	sbc a,a			;c283	9f		.
	and (hl)		;c284	a6		.
	ld l,b			;c285	68		h
	ld b,a			;c286	47		G
lc287h:
	sbc a,d			;c287	9a		.
	ld l,0b6h		;c288	2e b6		. .
	ld h,h			;c28a	64		d
	jp nz,07fffh		;c28b	c2 ff 7f	. . .
	push af			;c28e	f5		.
	ld l,c			;c28f	69		i
	ccf			;c290	3f		?
	dec a			;c291	3d		=
	jp R_FE7E		;c292	c3 7e fe	. ~ .
	ld h,b			;c295	60		`
	jp c,0e7c6h		;c296	da c6 e7	. . .
M_C299:
	ld h,(hl)		;c299	66		f
	pop bc			;c29a	c1		.
	ld (hl),b		;c29b	70		p
R_C29C:
	out (09bh),a		;c29c	d3 9b		. .
	defb 0ddh,080h,08bh ;illegal sequence	;c29e	dd 80 8b	. . .
	rst 8			;c2a1	cf		.
	call p,0ebcah		;c2a2	f4 ca eb	. . .
	add a,c			;c2a5	81		.
R_C2A6:
	call R_D136		;c2a6	cd 36 d1	. 6 .
	cp h			;c2a9	bc		.
	rst 8			;c2aa	cf		.
	add a,d			;c2ab	82		.
	pop af			;c2ac	f1		.
	adc a,a			;c2ad	8f		.
	jp c,0da4fh		;c2ae	da 4f da	. O .
	ld d,0d6h		;c2b1	16 d6		. .
	and c			;c2b3	a1		.
	add a,h			;c2b4	84		.
	ld h,e			;c2b5	63		c
	sbc a,0ffh		;c2b6	de ff		. .
	inc hl			;c2b8	23		#
	jp 02ccch		;c2b9	c3 cc 2c	. . ,
	and 001h		;c2bc	e6 01		. .
M_C2BE:
	ld (hl),0e7h		;c2be	36 e7		6 .
	and a			;c2c0	a7		.
	jp po,02007h		;c2c1	e2 07 20	. .  
	sub (hl)		;c2c4	96		.
	cp 023h			;c2c5	fe 23		. #
	ret			;c2c7	c9		.
	sub 060h		;c2c8	d6 60		. `
	push hl			;c2ca	e5		.
	ld bc,00063h		;c2cb	01 63 00	. c .
	ret po			;c2ce	e0		.
	halt			;c2cf	76		v
	ld e,(hl)		;c2d0	5e		^
D_C2D1:
	cp h			;c2d1	bc		.
	ld b,e			;c2d2	43		C
	ld (ix+003h),l		;c2d3	dd 75 03	. u .
	ld (hl),h		;c2d6	74		t
	inc b			;c2d7	04		.
	pop hl			;c2d8	e1		.
	call p,0dbd4h		;c2d9	f4 d4 db	. . .
R_C2DC:
	ld a,(hl)		;c2dc	7e		~
	jr c,$+1		;c2dd	38 ff		8 .
	ret			;c2df	c9		.
	xor a			;c2e0	af		.
M_C2E1:
	jr lc2e5h		;c2e1	18 02		. .
	dec h			;c2e3	25		%
R_C2E4:
	sub h			;c2e4	94		.
lc2e5h:
	or 070h			;c2e5	f6 70		. p
	ld d,h			;c2e7	54		T
	or 072h			;c2e8	f6 72		. r
R_C2EA:
	inc (hl)		;c2ea	34		4
	ex (sp),hl		;c2eb	e3		.
	rst 18h			;c2ec	df		.
	dec b			;c2ed	05		.
	rst 18h			;c2ee	df		.
	ld b,0dch		;c2ef	06 dc		. .
	push hl			;c2f1	e5		.
	cp 000h			;c2f2	fe 00		. .
	rst 10h			;c2f4	d7		.
	ld sp,hl		;c2f5	f9		.
	in a,(0d6h)		;c2f6	db d6		. .
	add a,b			;c2f8	80		.
	ld (09aaeh),a		;c2f9	32 ae 9a	2 . .
	add hl,de		;c2fc	19		.
	ld a,(hl)		;c2fd	7e		~
	dec sp			;c2fe	3b		;
	ld d,e			;c2ff	53		S
	xor h			;c300	ac		.
D_C301:
	ld bc,0afe5h		;c301	01 e5 af	. . .
	dec c			;c304	0d		.
	ld (bc),a		;c305	02		.
	sub 0e1h		;c306	d6 e1		. .
	rst 0			;c308	c7		.
	xor a			;c309	af		.
	sub c			;c30a	91		.
	ld a,(hl)		;c30b	7e		~
	rlca			;c30c	07		.
	inc a			;c30d	3c		<
	ret z			;c30e	c8		.
	dec a			;c30f	3d		=
	inc (hl)		;c310	34		4
	jr nc,$-116		;c311	30 8a		0 .
	push af			;c313	f5		.
	and a			;c314	a7		.
	inc d			;c315	14		.
	and c			;c316	a1		.
	pop bc			;c317	c1		.
	sub (hl)		;c318	96		.
	inc sp			;c319	33		3
	xor b			;c31a	a8		.
	pop af			;c31b	f1		.
	rst 38h			;c31c	ff		.
	ld c,l			;c31d	4d		M
M_C31E:
	rst 8			;c31e	cf		.
	ld e,(hl)		;c31f	5e		^
	sub h			;c320	94		.
	ld d,(hl)		;c321	56		V
	inc b			;c322	04		.
	ld hl,00060h		;c323	21 60 00	! ` .
	add hl,de		;c326	19		.
	ld a,(hl)		;c327	7e		~
M_C328:
	dec a			;c328	3d		=
	ret z			;c329	c8		.
	sbc a,0fah		;c32a	de fa		. .
	call pe,009c1h		;c32c	ec c1 09	. . .
R_C32F:
	jr nc,R_C2E4		;c32f	30 b3		0 .
	add a,(hl)		;c331	86		.
	cp 0d7h			;c332	fe d7		. .
	ret			;c334	c9		.
	ld a,c			;c335	79		y
	or a			;c336	b7		.
	ret nz			;c337	c0		.
	ld a,h			;c338	7c		|
	ld (M_C8A7),a		;c339	32 a7 c8	2 . .
	sbc a,0a7h		;c33c	de a7		. .
	call nz,0b7feh		;c33e	c4 fe b7	. . .
	cp (hl)			;c341	be		.
R_C342:
	jr $+4			;c342	18 02		. .
	jp z,lc287h		;c344	ca 87 c2	. . .
	ld (hl),b		;c347	70		p
	sub b			;c348	90		.
	sbc a,e			;c349	9b		.
	jp 0a58bh		;c34a	c3 8b a5	. . .
	ld l,c			;c34d	69		i
	xor a			;c34e	af		.
	adc a,d			;c34f	8a		.
	set 6,c			;c350	cb f1		. .
	ret			;c352	c9		.
	sub d			;c353	92		.
	and l			;c354	a5		.
	add a,035h		;c355	c6 35		. 5
	ld a,d			;c357	7a		z
	ld c,(hl)		;c358	4e		N
	ld a,c			;c359	79		y
	rst 30h			;c35a	f7		.
	jp 0872ah		;c35b	c3 2a 87	. * .
	ccf			;c35e	3f		?
	adc a,h			;c35f	8c		.
	ld a,c			;c360	79		y
	or b			;c361	b0		.
	ld h,h			;c362	64		d
	ld c,e			;c363	4b		K
	rrca			;c364	0f		.
	ei			;c365	fb		.
	xor b			;c366	a8		.
	ld c,d			;c367	4a		J
R_C368:
	dec (hl)		;c368	35		5
	rst 0			;c369	c7		.
	xor 0bah		;c36a	ee ba		. .
	jp p,0f945h		;c36c	f2 45 f9	. E .
	ld l,c			;c36f	69		i
	ld (022c3h),a		;c370	32 c3 22	2 . "
	and c			;c373	a1		.
	or d			;c374	b2		.
	ld b,l			;c375	45		E
	add a,h			;c376	84		.
	xor c			;c377	a9		.
	ld (hl),a		;c378	77		w
	inc c			;c379	0c		.
	ld l,(hl)		;c37a	6e		n
	ld (hl),c		;c37b	71		q
sub_c37ch:
	xor (hl)		;c37c	ae		.
	call R_E857		;c37d	cd 57 e8	. W .
	rst 28h			;c380	ef		.
	xor h			;c381	ac		.
lc382h:
	xor l			;c382	ad		.
	add a,091h		;c383	c6 91		. .
	add a,03ah		;c385	c6 3a		. :
	ld d,d			;c387	52		R
	jp c,0b1c9h		;c388	da c9 b1	. . .
	or b			;c38b	b0		.
lc38ch:
	call nz,033cfh		;c38c	c4 cf 33	. . 3
	push hl			;c38f	e5		.
	call c,0a3cbh		;c390	dc cb a3	. . .
	xor b			;c393	a8		.
	ld e,(hl)		;c394	5e		^
	res 5,d			;c395	cb aa		. .
	set 1,d			;c397	cb ca		. .
	ld a,d			;c399	7a		z
	adc a,h			;c39a	8c		.
	sra b			;c39b	cb 28		. (
	inc d			;c39d	14		.
	push af			;c39e	f5		.
	and 0cbh		;c39f	e6 cb		. .
	sbc a,e			;c3a1	9b		.
	defb 0ddh,020h,0cbh ;illegal sequence	;c3a2	dd 20 cb	.   .
	rst 18h			;c3a5	df		.
	nop			;c3a6	00		.
	or b			;c3a7	b0		.
	or c			;c3a8	b1		.
	sub h			;c3a9	94		.
	jp p,0d8c7h		;c3aa	f2 c7 d8	. . .
lc3adh:
	ld (hl),e		;c3ad	73		s
	call 0a5c7h		;c3ae	cd c7 a5	. . .
	rst 0			;c3b1	c7		.
	xor e			;c3b2	ab		.
	rst 0			;c3b3	c7		.
	sla a			;c3b4	cb 27		. '
	jp 07d3ch		;c3b6	c3 3c 7d	. < }
	push af			;c3b9	f5		.
	push de			;c3ba	d5		.
D_C3BB:
	ld l,(ix-00ah)		;c3bb	dd 6e f6	. n .
	ld e,e			;c3be	5b		[
	and 066h		;c3bf	e6 66		. f
	ld b,011h		;c3c1	06 11		. .
	ld (bc),a		;c3c3	02		.
	rla			;c3c4	17		.
	adc a,c			;c3c5	89		.
	sub d			;c3c6	92		.
	add a,e			;c3c7	83		.
	add a,(hl)		;c3c8	86		.
	add a,087h		;c3c9	c6 87		. .
	ld e,a			;c3cb	5f		_
lc3cch:
	ld a,c			;c3cc	79		y
	ld hl,026b7h		;c3cd	21 b7 26	! . &
	jp 06819h		;c3d0	c3 19 68	. . h
	ex de,hl		;c3d3	eb		.
	sbc a,a			;c3d4	9f		.
	ret m			;c3d5	f8		.
	pop de			;c3d6	d1		.
	pop af			;c3d7	f1		.
	bit 4,d			;c3d8	cb 62		. b
	jr z,lc3e0h		;c3da	28 04		( .
	and d			;c3dc	a2		.
	add hl,de		;c3dd	19		.
	ret			;c3de	c9		.
	and a			;c3df	a7		.
lc3e0h:
	ld c,b			;c3e0	48		H
R_C3E1:
	adc a,b			;c3e1	88		.
R_C3E2:
	sbc hl,de		;c3e2	ed 52		. R
	ld b,d			;c3e4	42		B
	djnz lc3adh		;c3e5	10 c6		. .
	rst 18h			;c3e7	df		.
	ld h,b			;c3e8	60		`
	dec c			;c3e9	0d		.
	add a,b			;c3ea	80		.
	inc c			;c3eb	0c		.
	ret c			;c3ec	d8		.
	dec bc			;c3ed	0b		.
	jr z,lc3cch		;c3ee	28 dc		( .
	ret m			;c3f0	f8		.
	adc a,b			;c3f1	88		.
	ld a,(bc)		;c3f2	0a		.
D_C3F3:
	ret p			;c3f3	f0		.
	add hl,bc		;c3f4	09		.
	ld h,b			;c3f5	60		`
	ret po			;c3f6	e0		.
	ex af,af'		;c3f7	08		.
R_C3F8:
	ld e,b			;c3f8	58		X
	ld sp,00761h		;c3f9	31 61 07	1 a .
	ld a,h			;c3fc	7c		|
	jr lc382h		;c3fd	18 83		. .
	cp e			;c3ff	bb		.
	sub c			;c400	91		.
	jr z,lc443h		;c401	28 40		( @
	call pe,09405h		;c403	ec 05 94	. . .
	exx			;c406	d9		.
	dec a			;c407	3d		=
	ld b,h			;c408	44		D
D_C409:
	ret m			;c409	f8		.
	inc b			;c40a	04		.
	or b			;c40b	b0		.
	defb 0ddh,0e8h,070h ;illegal sequence	;c40c	dd e8 70	. . p
	inc l			;c40f	2c		,
	ret p			;c410	f0		.
	inc bc			;c411	03		.
	cp (hl)			;c412	be		.
	or e			;c413	b3		.
	sbc a,(hl)		;c414	9e		.
	add a,h			;c415	84		.
	ld e,b			;c416	58		X
	or e			;c417	b3		.
	jp nc,0f620h		;c418	d2 20 f6	.   .
	call po,0d67ah		;c41b	e4 7a d6	. z .
	and d			;c41e	a2		.
R_C41F:
	ld a,h			;c41f	7c		|
	ld e,b			;c420	58		X
R_C421:
	jr c,M_C499		;c421	38 76		8 v
	rst 8			;c423	cf		.
	ld d,0f8h		;c424	16 f8		. .
	ld bc,D_CFDF		;c426	01 df cf	. . .
	ld a,d			;c429	7a		z
	jp nz,0ebach		;c42a	c2 ac eb	. . .
	ld e,c			;c42d	59		Y
	sub b			;c42e	90		.
lc42fh:
	ld a,e			;c42f	7b		{
	ld h,l			;c430	65		e
	ld d,c			;c431	51		Q
	ld h,a			;c432	67		g
	dec a			;c433	3d		=
	ld a,02ch		;c434	3e 2c		> ,
	ld (hl),d		;c436	72		r
	and e			;c437	a3		.
	inc e			;c438	1c		.
	dec bc			;c439	0b		.
	call m,0ef00h		;c43a	fc 00 ef	. . .
	sub 0b3h		;c43d	d6 b3		. .
	ld c,d			;c43f	4a		J
	sub 0c8h		;c440	d6 c8		. .
	rst 8			;c442	cf		.
lc443h:
	ld a,d			;c443	7a		z
	cp l			;c444	bd		.
	or d			;c445	b2		.
	ex de,hl		;c446	eb		.
	ld e,c			;c447	59		Y
	xor b			;c448	a8		.
	sbc a,a			;c449	9f		.
M_C44A:
	sub (hl)		;c44a	96		.
	adc a,(hl)		;c44b	8e		.
	ld d,d			;c44c	52		R
	inc a			;c44d	3c		<
	add a,l			;c44e	85		.
	ld l,h			;c44f	6c		l
	ex de,hl		;c450	eb		.
	exx			;c451	d9		.
	adc a,c			;c452	89		.
	ld (hl),b		;c453	70		p
	ld l,e			;c454	6b		k
	ld h,h			;c455	64		d
	ld h,a			;c456	67		g
	dec a			;c457	3d		=
R_C458:
	ld e,(hl)		;c458	5e		^
	ld e,c			;c459	59		Y
	push af			;c45a	f5		.
	xor h			;c45b	ac		.
	ld d,h			;c45c	54		T
	ld c,a			;c45d	4f		O
D_C45E:
	ld c,e			;c45e	4b		K
	ld b,a			;c45f	47		G
	or e			;c460	b3		.
	sbc a,(hl)		;c461	9e		.
	ld b,d			;c462	42		B
	ccf			;c463	3f		?
	ld a,d			;c464	7a		z
	sub 03bh		;c465	d6 3b		. ;
	jr c,$+55		;c467	38 35		8 5
	ld (M_CF4A),a		;c469	32 4a cf	2 J .
	cpl			;c46c	2f		/
	inc l			;c46d	2c		,
	add hl,hl		;c46e	29		)
	ld c,d			;c46f	4a		J
	rst 8			;c470	cf		.
	daa			;c471	27		'
	dec h			;c472	25		%
	ld h,c			;c473	61		a
	ld a,d			;c474	7a		z
	ld h,(hl)		;c475	66		f
	ld b,(hl)		;c476	46		F
	rra			;c477	1f		.
	dec e			;c478	1d		.
	ld c,h			;c479	4c		L
	rst 8			;c47a	cf		.
	inc e			;c47b	1c		.
	ld a,(de)		;c47c	1a		.
	ld sp,D_CF1A		;c47d	31 1a cf	1 . .
	rla			;c480	17		.
	ld (hl),015h		;c481	36 15		6 .
	ld e,c			;c483	59		Y
	rst 8			;c484	cf		.
	inc de			;c485	13		.
	ld (de),a		;c486	12		.
	dec a			;c487	3d		=
	ex de,hl		;c488	eb		.
	ld de,00f10h		;c489	11 10 0f	. . .
	call m,0212fh		;c48c	fc 2f 21	. / !
	ld l,c			;c48f	69		i
	xor a			;c490	af		.
	or (hl)			;c491	b6		.
	ld a,00dh		;c492	3e 0d		> .
lc494h:
	jr nz,$+7		;c494	20 05		  .
	sub 003h		;c496	d6 03		. .
	dec hl			;c498	2b		+
M_C499:
	call m,00effh		;c499	fc ff 0e	. . .
	defb 0fdh,006h,0ffh ;illegal sequence	;c49c	fd 06 ff	. . .
	out (c),a		;c49f	ed 79		. y
	adc a,h			;c4a1	8c		.
	ld c,b			;c4a2	48		H
	cp a			;c4a3	bf		.
	add a,b			;c4a4	80		.
	rlca			;c4a5	07		.
	ld c,b			;c4a6	48		H
	ld d,d			;c4a7	52		R
	sbc a,e			;c4a8	9b		.
	add hl,bc		;c4a9	09		.
	ei			;c4aa	fb		.
	dec b			;c4ab	05		.
	ld (M_C499),hl		;c4ac	22 99 c4	" . .
	jp R_C90B		;c4af	c3 0b c9	. . .
R_C4B2:
	cp a			;c4b2	bf		.
	ret m			;c4b3	f8		.
	ld hl,D_CA0C		;c4b4	21 0c ca	! . .
	jp R_C1C9		;c4b7	c3 c9 c1	. . .
	rra			;c4ba	1f		.
	push bc			;c4bb	c5		.
	ret m			;c4bc	f8		.
	ld c,0f8h		;c4bd	0e f8		. .
	dec de			;c4bf	1b		.
	djnz lc522h		;c4c0	10 60		. `
	dec c			;c4c2	0d		.
	add a,b			;c4c3	80		.
	inc c			;c4c4	0c		.
	ret c			;c4c5	d8		.
	dec bc			;c4c6	0b		.
	jr z,lc4e4h		;c4c7	28 1b		( .
	rst 18h			;c4c9	df		.
	adc a,b			;c4ca	88		.
	ld a,(bc)		;c4cb	0a		.
	ret p			;c4cc	f0		.
	add hl,bc		;c4cd	09		.
	ld h,b			;c4ce	60		`
	ret po			;c4cf	e0		.
	ld h,08ch		;c4d0	26 8c		& .
	ex af,af'		;c4d2	08		.
	ld e,b			;c4d3	58		X
	rlca			;c4d4	07		.
	ld a,h			;c4d5	7c		|
	ld h,e			;c4d6	63		c
	jr nc,lc494h		;c4d7	30 bb		0 .
	ld (hl),c		;c4d9	71		q
	or b			;c4da	b0		.
	ld b,040h		;c4db	06 40		. @
	call pe,09405h		;c4dd	ec 05 94	. . .
	exx			;c4e0	d9		.
	dec a			;c4e1	3d		=
	ld b,h			;c4e2	44		D
	ret m			;c4e3	f8		.
lc4e4h:
	inc b			;c4e4	04		.
	or b			;c4e5	b0		.
	defb 0ddh,0e8h,070h ;illegal sequence	;c4e6	dd e8 70	. . p
	inc l			;c4e9	2c		,
	defb 0fdh,003h,0beh ;illegal sequence	;c4ea	fd 03 be	. . .
	or e			;c4ed	b3		.
	sbc a,(hl)		;c4ee	9e		.
	add a,h			;c4ef	84		.
	ld e,b			;c4f0	58		X
	sbc a,(hl)		;c4f1	9e		.
	defb 0ddh,020h,0f6h ;illegal sequence	;c4f2	dd 20 f6	.   .
	ld (bc),a		;c4f5	02		.
	jp z,0b3d6h		;c4f6	ca d6 b3	. . .
	and d			;c4f9	a2		.
R_C4FA:
	ld a,h			;c4fa	7c		|
	ld e,b			;c4fb	58		X
	jr c,$-75		;c4fc	38 b3		8 .
	ld a,e			;c4fe	7b		{
	ld d,0f8h		;c4ff	16 f8		. .
	ld bc,07adfh		;c501	01 df 7a	. . z
	sub 0c2h		;c504	d6 c2		. .
D_C506:
	xor h			;c506	ac		.
	sub b			;c507	90		.
	ld a,e			;c508	7b		{
	ld e,c			;c509	59		Y
	rst 8			;c50a	cf		.
M_C50B:
	ld h,l			;c50b	65		e
	ld d,c			;c50c	51		Q
	dec a			;c50d	3d		=
M_C50E:
	ex de,hl		;c50e	eb		.
	ld a,02ch		;c50f	3e 2c		> ,
	inc e			;c511	1c		.
	or e			;c512	b3		.
	dec de			;c513	1b		.
	ld a,(bc)		;c514	0a		.
	call m,0ef00h		;c515	fc 00 ef	. . .
	ld a,d			;c518	7a		z
	sub 0e1h		;c519	d6 e1		. .
	sub 0c8h		;c51b	d6 c8		. .
	cp l			;c51d	bd		.
	ld e,c			;c51e	59		Y
	rst 8			;c51f	cf		.
	or d			;c520	b2		.
	xor b			;c521	a8		.
lc522h:
	dec a			;c522	3d		=
	ex de,hl		;c523	eb		.
	sbc a,a			;c524	9f		.
	sub (hl)		;c525	96		.
	adc a,(hl)		;c526	8e		.
	xor h			;c527	ac		.
	ld h,a			;c528	67		g
	add a,l			;c529	85		.
	ld a,(hl)		;c52a	7e		~
	ld (hl),a		;c52b	77		w
	sbc a,(hl)		;c52c	9e		.
	push af			;c52d	f5		.
	ld (hl),b		;c52e	70		p
	ld l,e			;c52f	6b		k
	sub 0b3h		;c530	d6 b3		. .
	ld h,h			;c532	64		d
	ld e,(hl)		;c533	5e		^
	ld e,c			;c534	59		Y
	ld d,h			;c535	54		T
	rst 8			;c536	cf		.
	ld a,d			;c537	7a		z
	ld c,a			;c538	4f		O
	ld c,e			;c539	4b		K
D_C53A:
	ex de,hl		;c53a	eb		.
lc53bh:
	ld e,c			;c53b	59		Y
	ld b,a			;c53c	47		G
	ld b,d			;c53d	42		B
	ccf			;c53e	3f		?
	dec sp			;c53f	3b		;
	ld h,a			;c540	67		g
	dec a			;c541	3d		=
	jr c,lc579h		;c542	38 35		8 5
	push af			;c544	f5		.
M_C545:
	xor h			;c545	ac		.
	ld (02c2fh),a		;c546	32 2f 2c	2 / ,
	ld hl,(09eb3h)		;c549	2a b3 9e	* . .
	daa			;c54c	27		'
	dec h			;c54d	25		%
	ld a,d			;c54e	7a		z
R_C54F:
	sub 023h		;c54f	d6 23		. #
	ld hl,01d1fh		;c551	21 1f 1d	! . .
	ld e,c			;c554	59		Y
	rst 8			;c555	cf		.
	inc e			;c556	1c		.
	ld a,(de)		;c557	1a		.
	dec a			;c558	3d		=
R_C559:
	ex de,hl		;c559	eb		.
	add hl,de		;c55a	19		.
	rla			;c55b	17		.
	ld d,0ach		;c55c	16 ac		. .
	ld h,a			;c55e	67		g
M_C55F:
	dec d			;c55f	15		.
	inc de			;c560	13		.
	ld (de),a		;c561	12		.
	sbc a,(hl)		;c562	9e		.
	push af			;c563	f5		.
R_C564:
	ld de,06210h		;c564	11 10 62	. . b
	ld h,b			;c567	60		`
	rrca			;c568	0f		.
M_C569:
	defb 0edh ;next byte illegal after ed	;c569	ed		.
	defb 0fdh,001h,0f9h ;illegal sequence	;c56a	fd 01 f9	. . .
	rst 30h			;c56d	f7		.
	xor a			;c56e	af		.
	ld d,d			;c56f	52		R
	ld (bc),a		;c570	02		.
	and d			;c571	a2		.
	cp (hl)			;c572	be		.
	adc a,h			;c573	8c		.
	jp c,0f8efh		;c574	da ef f8	. . .
	inc bc			;c577	03		.
	cp l			;c578	bd		.
lc579h:
	jr z,$-47		;c579	28 cf		( .
	and (hl)		;c57b	a6		.
	add a,e			;c57c	83		.
	cp d			;c57d	ba		.
	inc b			;c57e	04		.
	ld b,a			;c57f	47		G
	jp z,09b3eh		;c580	ca 3e 9b	. > .
	dec b			;c583	05		.
	ld c,0dah		;c584	0e da		. .
	jp pe,0dea2h		;c586	ea a2 de	. . .
	ld d,c			;c589	51		Q
	ld b,06dh		;c58a	06 6d		. m
	ld d,b			;c58c	50		P
	jp nc,0ff16h		;c58d	d2 16 ff	. . .
	adc a,(hl)		;c590	8e		.
	rlca			;c591	07		.
	sbc a,0e8h		;c592	de e8		. .
	ex af,af'		;c594	08		.
	ld (hl),l		;c595	75		u
	ld d,b			;c596	50		P
	ld c,b			;c597	48		H
	dec d			;c598	15		.
	ld l,b			;c599	68		h
	ld a,e			;c59a	7b		{
	add hl,bc		;c59b	09		.
	cpl			;c59c	2f		/
	jr z,$-55		;c59d	28 c7		( .
	jr z,lc5aah		;c59f	28 09		( .
	add a,d			;c5a1	82		.
	cp d			;c5a2	ba		.
	ld a,(bc)		;c5a3	0a		.
M_C5A4:
	jp pe,0e782h		;c5a4	ea 82 e7	. . .
	ld sp,00b0ah		;c5a7	31 0a 0b	1 . .
lc5aah:
	ld d,(hl)		;c5aa	56		V
M_C5AB:
	add a,e			;c5ab	83		.
	pop de			;c5ac	d1		.
	and e			;c5ad	a3		.
	xor (hl)		;c5ae	ae		.
	call nz,0541ah		;c5af	c4 1a 54	. . T
D_C5B2:
	inc c			;c5b2	0c		.
	and e			;c5b3	a3		.
	cp (hl)			;c5b4	be		.
	pop de			;c5b5	d1		.
	rlca			;c5b6	07		.
R_C5B7:
	ld l,l			;c5b7	6d		m
	dec c			;c5b8	0d		.
	and e			;c5b9	a3		.
	jp z,041d2h		;c5ba	ca d2 41	. . A
	ld a,l			;c5bd	7d		}
	ld c,01fh		;c5be	0e 1f		. .
lc5c0h:
	defb 0edh ;next byte illegal after ed	;c5c0	ed		.
	rrca			;c5c1	0f		.
	di			;c5c2	f3		.
	push hl			;c5c3	e5		.
	pop af			;c5c4	f1		.
	call nz,0be22h		;c5c5	c4 22 be	. " .
D_C5C8:
	jp nz,0327eh		;c5c8	c2 7e 32	. ~ 2
	ld d,c			;c5cb	51		Q
	add a,099h		;c5cc	c6 99		. .
	ld (hl),c		;c5ce	71		q
	inc hl			;c5cf	23		#
	push bc			;c5d0	c5		.
	ld b,a			;c5d1	47		G
	sub 07eh		;c5d2	d6 7e		. ~
	ld h,c			;c5d4	61		a
	ld de,028ech		;c5d5	11 ec 28	. . (
	jr nz,$-39		;c5d8	20 d7		  .
	sub (hl)		;c5da	96		.
	ret nz			;c5db	c0		.
	ld (hl),h		;c5dc	74		t
	ld e,(hl)		;c5dd	5e		^
	exx			;c5de	d9		.
	ld b,e			;c5df	43		C
	ld d,(hl)		;c5e0	56		V
	ld bc,0001eh		;c5e1	01 1e 00	. . .
	add hl,bc		;c5e4	09		.
	rst 30h			;c5e5	f7		.
D_C5E6:
	ld c,c			;c5e6	49		I
	xor e			;c5e7	ab		.
	ld c,a			;c5e8	4f		O
	ld (hl),l		;c5e9	75		u
	ld c,0a0h		;c5ea	0e a0		. .
	pop hl			;c5ec	e1		.
	ret m			;c5ed	f8		.
	ld b,e			;c5ee	43		C
	cp c			;c5ef	b9		.
	ld hl,D_C9FE		;c5f0	21 fe c9	! . .
	ld de,D_FEFF		;c5f3	11 ff fe	. . .
	xor c			;c5f6	a9		.
	defb 0fdh,00dh,070h ;illegal sequence	;c5f7	fd 0d 70	. . p
	ldir			;c5fa	ed b0		. .
M_C5FC:
	xor a			;c5fc	af		.
	ld (M_C9FB),a		;c5fd	32 fb c9	2 . .
	or e			;c600	b3		.
	adc a,0fch		;c601	ce fc		. .
	jr nc,M_C5AB		;c603	30 a6		0 .
	jp (iy)			;c605	fd e9		. .
	xor c			;c607	a9		.
	call po,0a40eh		;c608	e4 0e a4	. . .
	cp b			;c60b	b8		.
	ld c,e			;c60c	4b		K
	jp 01764h		;c60d	c3 64 17	. d .
	rst 20h			;c610	e7		.
	ld c,h			;c611	4c		L
	call nz,05916h		;c612	c4 16 59	. . Y
	and h			;c615	a4		.
	add hl,bc		;c616	09		.
	sub h			;c617	94		.
	ld d,c			;c618	51		Q
	sbc a,c			;c619	99		.
	halt			;c61a	76		v
	ld h,a			;c61b	67		g
D_C61C:
	dec de			;c61c	1b		.
	sbc a,(hl)		;c61d	9e		.
	ld b,a			;c61e	47		G
	cpl			;c61f	2f		/
	ret m			;c620	f8		.
R_C621:
	ld d,h			;c621	54		T
	rra			;c622	1f		.
	ld l,h			;c623	6c		l
	ccf			;c624	3f		?
	jr nz,lc5c0h		;c625	20 99		  .
	xor c			;c627	a9		.
	ld a,0f8h		;c628	3e f8		> .
	jp p,R_DF84		;c62a	f2 84 df	. . .
	adc a,l			;c62d	8d		.
	ld hl,D_C1B9		;c62e	21 b9 c1	! . .
	ld (02eaeh),hl		;c631	22 ae 2e	" . .
	ld l,013h		;c634	2e 13		. .
	add a,094h		;c636	c6 94		. .
	jp 02eceh		;c638	c3 ce 2e	. . .
	ld d,0c4h		;c63b	16 c4		. .
	add a,a			;c63d	87		.
	rst 38h			;c63e	ff		.
lc63fh:
	sbc a,c			;c63f	99		.
	call R_C2A6		;c640	cd a6 c2	. . .
	ld (lc53bh),de		;c643	ed 53 3b c5	. S ; .
	pop bc			;c647	c1		.
	rst 38h			;c648	ff		.
	push hl			;c649	e5		.
	ld (hl),e		;c64a	73		s
	add a,a			;c64b	87		.
	jp nz,0d831h		;c64c	c2 31 d8	. 1 .
	ld de,D_C99B		;c64f	11 9b c9	. . .
	ld a,020h		;c652	3e 20		>  
	pop hl			;c654	e1		.
	add hl,bc		;c655	09		.
lc656h:
	rlca			;c656	07		.
	ex (sp),hl		;c657	e3		.
	ex de,hl		;c658	eb		.
	ld (hl),e		;c659	73		s
	inc hl			;c65a	23		#
	ld (hl),d		;c65b	72		r
	ld (hl),0dbh		;c65c	36 db		6 .
	dec a			;c65e	3d		=
	jr nz,lc656h		;c65f	20 f5		  .
	in a,(0dfh)		;c661	db df		. .
	halt			;c663	76		v
	djnz lc63fh		;c664	10 d9		. .
	call p,0632ah		;c666	f4 2a 63	. * c
	jp c,0343fh		;c669	da 3f 34	. ? 4
M_C66C:
	ld c,l			;c66c	4d		M
	cp c			;c66d	b9		.
	inc hl			;c66e	23		#
	cp c			;c66f	b9		.
	ld hl,(0a9b9h)		;c670	2a b9 a9	* . .
	pop hl			;c673	e1		.
	dec iy			;c674	fd 2b		. +
	call nz,029fbh		;c676	c4 fb 29	. . )
	call nz,00ea2h		;c679	c4 a2 0e	. . .
	and (hl)		;c67c	a6		.
	add hl,sp		;c67d	39		9
	defb 0ddh,087h,018h ;illegal sequence	;c67e	dd 87 18	. . .
	dec bc			;c681	0b		.
	or (hl)			;c682	b6		.
	adc a,05eh		;c683	ce 5e		. ^
	ld l,l			;c685	6d		m
	ld d,c			;c686	51		Q
	jr c,$-16		;c687	38 ee		8 .
M_C689:
	add a,(hl)		;c689	86		.
	sub d			;c68a	92		.
	ld hl,03a26h		;c68b	21 26 3a	! & :
lc68eh:
	ld l,b			;c68e	68		h
	xor c			;c68f	a9		.
	add hl,bc		;c690	09		.
	ld sp,hl		;c691	f9		.
	jp m,02b49h		;c692	fa 49 2b	. I +
	cp e			;c695	bb		.
D_C696:
	ld l,(hl)		;c696	6e		n
	sub l			;c697	95		.
	jp pe,07259h		;c698	ea 59 72	. Y r
	ld c,e			;c69b	4b		K
	ld a,(de)		;c69c	1a		.
	cp c			;c69d	b9		.
	ld l,b			;c69e	68		h
	ld a,c			;c69f	79		y
	jp p,07bc9h		;c6a0	f2 c9 7b	. . {
	add a,a			;c6a3	87		.
	add a,e			;c6a4	83		.
	ld e,a			;c6a5	5f		_
	call po,07e19h		;c6a6	e4 19 7e	. . ~
	rra			;c6a9	1f		.
	jr nc,lc68eh		;c6aa	30 e2		0 .
M_C6AC:
	ld hl,0046fh		;c6ac	21 6f 04	! o .
	rl d			;c6af	cb 12		. .
	ld d,a			;c6b1	57		W
	rrc b			;c6b2	cb 08		. .
	jp p,0310fh		;c6b4	f2 0f 31	. . 1
	ex (sp),hl		;c6b7	e3		.
	and 09ah		;c6b8	e6 9a		. .
	ld c,c			;c6ba	49		I
	ex af,af'		;c6bb	08		.
	bit 3,d			;c6bc	cb 5a		. Z
D_C6BE:
	jr nz,lc6c6h		;c6be	20 06		  .
	push af			;c6c0	f5		.
	add a,b			;c6c1	80		.
	ld (M_CA04),a		;c6c2	32 04 ca	2 . .
	pop af			;c6c5	f1		.
lc6c6h:
	ld a,(hl)		;c6c6	7e		~
	rla			;c6c7	17		.
	and 01fh		;c6c8	e6 1f		. .
	inc hl			;c6ca	23		#
	ld l,(hl)		;c6cb	6e		n
	ld h,a			;c6cc	67		g
	defb 0ddh,062h ;ld ixh,d	;c6cd	dd 62		. b
	ex de,hl		;c6cf	eb		.
	adc a,030h		;c6d0	ce 30		. 0
	and 0b4h		;c6d2	e6 b4		. .
	defb 0ddh,07dh ;ld a,ixl	;c6d4	dd 7d		. }
	and a			;c6d6	a7		.
	xor e			;c6d7	ab		.
	adc a,e			;c6d8	8b		.
	ld sp,D_C7C0		;c6d9	31 c0 c7	1 . .
	sbc a,e			;c6dc	9b		.
	rst 38h			;c6dd	ff		.
	ld h,(hl)		;c6de	66		f
	ld l,a			;c6df	6f		o
	srl d			;c6e0	cb 3a		. :
	jr c,lc6e7h		;c6e2	38 03		8 .
	sbc hl,de		;c6e4	ed 52		. R
	ret			;c6e6	c9		.
lc6e7h:
	add hl,de		;c6e7	19		.
	ld e,e			;c6e8	5b		[
	nop			;c6e9	00		.
	ld b,(hl)		;c6ea	46		F
	jp nc,0283ch		;c6eb	d2 3c 28	. < (
	ld (hl),a		;c6ee	77		w
	sbc a,d			;c6ef	9a		.
	sbc a,l			;c6f0	9d		.
	sub a			;c6f1	97		.
	cp a			;c6f2	bf		.
	in a,(079h)		;c6f3	db 79		. y
	inc a			;c6f5	3c		<
	cp (hl)			;c6f6	be		.
	inc hl			;c6f7	23		#
	jr nz,lc6fbh		;c6f8	20 01		  .
	sbc a,l			;c6fa	9d		.
lc6fbh:
	inc h			;c6fb	24		$
	ld c,(hl)		;c6fc	4e		N
	inc (hl)		;c6fd	34		4
	jp 0057dh		;c6fe	c3 7d 05	. } .
	ex de,hl		;c701	eb		.
	ld a,b			;c702	78		x
	ld e,a			;c703	5f		_
	add a,h			;c704	84		.
	or 025h			;c705	f6 25		. %
	out (0f0h),a		;c707	d3 f0		. .
	defb 0ddh,02eh,086h ;ld ixl,086h	;c709	dd 2e 86	. . .
	push bc			;c70c	c5		.
	sub h			;c70d	94		.
	call 08ed3h		;c70e	cd d3 8e	. . .
	jp c,02d58h		;c711	da 58 2d	. X -
	and l			;c714	a5		.
	xor b			;c715	a8		.
	ex de,hl		;c716	eb		.
	sbc a,a			;c717	9f		.
	cp 015h			;c718	fe 15		. .
	jp m,lc38ch		;c71a	fa 8c c3	. . .
	jr z,$+40		;c71d	28 26		( &
D_C71F:
	push hl			;c71f	e5		.
	ret z			;c720	c8		.
	ld a,e			;c721	7b		{
	and a			;c722	a7		.
	jp p,0f044h		;c723	f2 44 f0	. D .
	ld l,e			;c726	6b		k
	jp 044edh		;c727	c3 ed 44	. . D
	ld c,a			;c72a	4f		O
	inc h			;c72b	24		$
	di			;c72c	f3		.
	ld b,d			;c72d	42		B
	jr nc,lc741h		;c72e	30 11		0 .
	pop hl			;c730	e1		.
	xor h			;c731	ac		.
	xor 049h		;c732	ee 49		. I
	inc sp			;c734	33		3
	or c			;c735	b1		.
	sub b			;c736	90		.
	ld e,c			;c737	59		Y
	ld h,a			;c738	67		g
	ld l,a			;c739	6f		o
	exx			;c73a	d9		.
	ld l,h			;c73b	6c		l
	push hl			;c73c	e5		.
D_C73D:
	ld d,b			;c73d	50		P
	jr lc74ah		;c73e	18 0a		. .
	ld h,d			;c740	62		b
lc741h:
	ld a,d			;c741	7a		z
	adc a,h			;c742	8c		.
	call m,sub_ca16h	;c743	fc 16 ca	. . .
	rst 20h			;c746	e7		.
	ld a,(hl)		;c747	7e		~
	defb 0ddh,07ch ;ld a,ixh	;c748	dd 7c		. |
lc74ah:
	ld (M_CA05),a		;c74a	32 05 ca	2 . .
	ex af,af'		;c74d	08		.
	dec h			;c74e	25		%
	ld a,(hl)		;c74f	7e		~
	sub 036h		;c750	d6 36		. 6
	or 0dah			;c752	f6 da		. .
	ld b,0cah		;c754	06 ca		. .
	add a,c			;c756	81		.
	ld a,d			;c757	7a		z
	adc a,e			;c758	8b		.
	ret nz			;c759	c0		.
	add a,c			;c75a	81		.
	defb 0edh ;next byte illegal after ed	;c75b	ed		.
	ld a,a			;c75c	7f		.
	and e			;c75d	a3		.
M_C75E:
	add a,c			;c75e	81		.
	and h			;c75f	a4		.
M_C760:
	and d			;c760	a2		.
	rst 38h			;c761	ff		.
	add a,c			;c762	81		.
	dec sp			;c763	3b		;
	sbc a,0cah		;c764	de ca		. .
	add a,c			;c766	81		.
	dec bc			;c767	0b		.
D_C768:
	call nz,0f1bfh		;c768	c4 bf f1	. . .
	add a,c			;c76b	81		.
	jp pe,0d69bh		;c76c	ea 9b d6	. . .
	add a,c			;c76f	81		.
	ret z			;c770	c8		.
	add a,c			;c771	81		.
	ret c			;c772	d8		.
	ld e,e			;c773	5b		[
	ld e,e			;c774	5b		[
	add a,c			;c775	81		.
	rst 8			;c776	cf		.
	add a,c			;c777	81		.
	pop hl			;c778	e1		.
	call p,081c4h		;c779	f4 c4 81	. . .
	inc (ix-07fh)		;c77c	dd 34 81	. 4 .
	ld hl,00781h		;c77f	21 81 07	! . .
	or (hl)			;c782	b6		.
R_C783:
	ld (hl),a		;c783	77		w
	call c,07efah		;c784	dc fa 7e	. . ~
lc787h:
	rlca			;c787	07		.
	ld de,07ed8h		;c788	11 d8 7e	. . ~
	ld a,e			;c78b	7b		{
	call m,07e77h		;c78c	fc 77 7e	. w ~
	dec h			;c78f	25		%
	call nz,0cf07h		;c790	c4 07 cf	. . .
	ld a,(hl)		;c793	7e		~
	ld h,0deh		;c794	26 de		& .
	jp m,0027eh		;c796	fa 7e 02	. ~ .
	ex de,hl		;c799	eb		.
	or a			;c79a	b7		.
	ld a,(hl)		;c79b	7e		~
	adc a,l			;c79c	8d		.
	ld a,(hl)		;c79d	7e		~
	ld d,e			;c79e	53		S
	cp (hl)			;c79f	be		.
	ld l,h			;c7a0	6c		l
	call nz,07f7eh		;c7a1	c4 7e 7f	. ~ .
	ld (04a9ah),hl		;c7a4	22 9a 4a	" . J
	call nz,05a7fh		;c7a7	c4 7f 5a	. . Z
	out (061h),a		;c7aa	d3 61		. a
	ld a,a			;c7ac	7f		.
	ld d,c			;c7ad	51		Q
D_C7AE:
	pop hl			;c7ae	e1		.
	ld a,(hl)		;c7af	7e		~
	sbc a,d			;c7b0	9a		.
	ld a,063h		;c7b1	3e 63		> c
	ld l,a			;c7b3	6f		o
	sbc a,(hl)		;c7b4	9e		.
	ld a,(hl)		;c7b5	7e		~
	jp m,07e17h		;c7b6	fa 17 7e	. . ~
	pop de			;c7b9	d1		.
	or d			;c7ba	b2		.
	ld a,l			;c7bb	7d		}
	jp z,0850bh		;c7bc	ca 0b 85	. . .
	ld e,b			;c7bf	58		X
D_C7C0:
	ld de,D_FFBF		;c7c0	11 bf ff	. . .
	ld c,0fdh		;c7c3	0e fd		. .
	xor a			;c7c5	af		.
	or (hl)			;c7c6	b6		.
	ld a,00dh		;c7c7	3e 0d		> .
	jr nz,lc7d2h		;c7c9	20 07		  .
	sub 003h		;c7cb	d6 03		. .
	call m,00890h		;c7cd	fc 90 08	. . .
	jr lc7e7h		;c7d0	18 15		. .
lc7d2h:
	ld b,d			;c7d2	42		B
	out (c),a		;c7d3	ed 79		. y
	ld b,e			;c7d5	43		C
	add a,l			;c7d6	85		.
	ld e,l			;c7d7	5d		]
	xor e			;c7d8	ab		.
	dec a			;c7d9	3d		=
	jr z,lc787h		;c7da	28 ab		( .
	rst 18h			;c7dc	df		.
	inc d			;c7dd	14		.
	halt			;c7de	76		v
	add a,a			;c7df	87		.
	ret			;c7e0	c9		.
	xor a			;c7e1	af		.
M_C7E2:
	ld b,a			;c7e2	47		G
	ex af,af'		;c7e3	08		.
	ld b,c			;c7e4	41		A
	sub 03eh		;c7e5	d6 3e		. >
lc7e7h:
	inc bc			;c7e7	03		.
	dec a			;c7e8	3d		=
	xor (hl)		;c7e9	ae		.
	jr z,$-97		;c7ea	28 9d		( .
	add hl,bc		;c7ec	09		.
	ld a,0ffh		;c7ed	3e ff		> .
	ld (01ec2h),hl		;c7ef	22 c2 1e	" . .
	jp R_FB21		;c7f2	c3 21 fb	. ! .
	ret			;c7f5	c9		.
	dec (hl)		;c7f6	35		5
	jp p,06d39h		;c7f7	f2 39 6d	. 9 m
	ld (hl),0edh		;c7fa	36 ed		6 .
	ld a,(de)		;c7fc	1a		.
	and a			;c7fd	a7		.
	call z,03b74h		;c7fe	cc 74 3b	. t ;
	dec d			;c801	15		.
	call R_C6E3		;c802	cd e3 c6	. . .
	ld (de),a		;c805	12		.
	ld a,(bc)		;c806	0a		.
	push de			;c807	d5		.
	call p,sub_c37ch	;c808	f4 7c c3	. | .
	call m,0fd85h		;c80b	fc 85 fd	. . .
	ld (hl),d		;c80e	72		r
	adc a,0cdh		;c80f	ce cd		. .
	inc b			;c811	04		.
	ret z			;c812	c8		.
	or c			;c813	b1		.
	dec h			;c814	25		%
	ld h,(hl)		;c815	66		f
	jp m,03ee1h		;c816	fa e1 3e	. . >
	jp pe,07bfdh		;c819	ea fd 7b	. . {
	push bc			;c81c	c5		.
	ld a,0f2h		;c81d	3e f2		> .
	dec bc			;c81f	0b		.
	ret			;c820	c9		.
	sub e			;c821	93		.
	ld (hl),e		;c822	73		s
	ld d,d			;c823	52		R
	xor a			;c824	af		.
	di			;c825	f3		.
	jp nc,03000h		;c826	d2 00 30	. . 0
	ld e,0cah		;c829	1e ca		. .
	or l			;c82b	b5		.
	ld c,b			;c82c	48		H
	or 0ach			;c82d	f6 ac		. .
	rst 38h			;c82f	ff		.
	lddr			;c830	ed b8		. .
	and e			;c832	a3		.
	ld (M_E14B),hl		;c833	22 4b e1	" K .
	ld (bc),a		;c836	02		.
	daa			;c837	27		'
	ld d,h			;c838	54		T
	sbc a,d			;c839	9a		.
	rst 18h			;c83a	df		.
	inc d			;c83b	14		.
	or 09dh			;c83c	f6 9d		. .
	ld h,h			;c83e	64		d
	ex de,hl		;c83f	eb		.
	sbc a,c			;c840	99		.
	ld h,0ddh		;c841	26 dd		& .
	ld c,e			;c843	4b		K
	sub h			;c844	94		.
	dec (hl)		;c845	35		5
	exx			;c846	d9		.
	dec c			;c847	0d		.
	ld l,l			;c848	6d		m
	and (hl)		;c849	a6		.
	ld a,h			;c84a	7c		|
	ei			;c84b	fb		.
	or h			;c84c	b4		.
	ld l,b			;c84d	68		h
	dec bc			;c84e	0b		.
	ld hl,(05934h)		;c84f	2a 34 59	* 4 Y
	ld a,(de)		;c852	1a		.
R_C853:
	call nz,0dc91h		;c853	c4 91 dc	. . .
	sbc a,073h		;c856	de 73		. s
	sbc a,0b0h		;c858	de b0		. .
	cp d			;c85a	ba		.
	ld d,l			;c85b	55		U
	add a,a			;c85c	87		.
	or b			;c85d	b0		.
	xor l			;c85e	ad		.
	ld e,b			;c85f	58		X
	ld b,d			;c860	42		B
	and 01eh		;c861	e6 1e		. .
	and c			;c863	a1		.
	ld de,00522h		;c864	11 22 05	. " .
	rra			;c867	1f		.
	add a,06ah		;c868	c6 6a		. j
	ld c,l			;c86a	4d		M
	ld (hl),e		;c86b	73		s
	sbc a,e			;c86c	9b		.
	sbc a,(hl)		;c86d	9e		.
D_C86E:
	rst 18h			;c86e	df		.
	or a			;c86f	b7		.
	ld b,(hl)		;c870	46		F
	ei			;c871	fb		.
	rst 18h			;c872	df		.
	and (hl)		;c873	a6		.
	and a			;c874	a7		.
	adc a,(hl)		;c875	8e		.
	sbc a,e			;c876	9b		.
	ld (bc),a		;c877	02		.
	add a,0ddh		;c878	c6 dd		. .
	ld l,d			;c87a	6a		j
	dec de			;c87b	1b		.
	call nz,065cah		;c87c	c4 ca 65	. . e
	jp nc,09916h		;c87f	d2 16 99	. . .
	ld l,d			;c882	6a		j
	ld e,a			;c883	5f		_
	sbc a,e			;c884	9b		.
	ret nc			;c885	d0		.
	add hl,de		;c886	19		.
	dec bc			;c887	0b		.
	adc a,a			;c888	8f		.
	ei			;c889	fb		.
	ld hl,026a9h		;c88a	21 a9 26	! . &
	ld e,e			;c88d	5b		[
	exx			;c88e	d9		.
	add hl,de		;c88f	19		.
	add hl,hl		;c890	29		)
	ld h,h			;c891	64		d
	jp c,04db6h		;c892	da b6 4d	. . M
	rst 8			;c895	cf		.
	ret c			;c896	d8		.
M_C897:
	jp p,0edc6h		;c897	f2 c6 ed	. . .
D_C89A:
	ld b,060h		;c89a	06 60		. `
	ld d,l			;c89c	55		U
	ld b,c			;c89d	41		A
	rst 10h			;c89e	d7		.
	ld l,d			;c89f	6a		j
	dec hl			;c8a0	2b		+
	call nc,sub_c941h	;c8a1	d4 41 c9	. A .
	add hl,bc		;c8a4	09		.
	rst 8			;c8a5	cf		.
	dec b			;c8a6	05		.
M_C8A7:
	out (0b2h),a		;c8a7	d3 b2		. .
R_C8A9:
	ld (hl),h		;c8a9	74		t
	ld (M_C646),hl		;c8aa	22 46 c6	" F .
	inc c			;c8ad	0c		.
	and l			;c8ae	a5		.
R_C8AF:
	ret c			;c8af	d8		.
	srl d			;c8b0	cb 3a		. :
	call c,0d963h		;c8b2	dc 63 d9	. c .
	ret z			;c8b5	c8		.
	dec b			;c8b6	05		.
	ld a,c			;c8b7	79		y
R_C8B8:
	sub d			;c8b8	92		.
	ld l,d			;c8b9	6a		j
	or d			;c8ba	b2		.
	adc a,l			;c8bb	8d		.
	ld e,l			;c8bc	5d		]
	scf			;c8bd	37		7
	call 05874h		;c8be	cd 74 58	. t X
	ret			;c8c1	c9		.
	ex af,af'		;c8c2	08		.
	inc l			;c8c3	2c		,
M_C8C4:
	ld a,(00e1ch)		;c8c4	3a 1c 0e	: . .
	ld a,04fh		;c8c7	3e 4f		> O
	ld (hl),e		;c8c9	73		s
	xor (hl)		;c8ca	ae		.
	inc b			;c8cb	04		.
	jr z,$-36		;c8cc	28 da		( .
	pop de			;c8ce	d1		.
	sbc a,c			;c8cf	99		.
	call c,0357eh		;c8d0	dc 7e 35	. ~ 5
	ret			;c8d3	c9		.
	add a,a			;c8d4	87		.
lc8d5h:
	ld hl,014cbh		;c8d5	21 cb 14	! . .
	ret nz			;c8d8	c0		.
	cp 047h			;c8d9	fe 47		. G
	ld (hl),l		;c8db	75		u
	ld e,(hl)		;c8dc	5e		^
	xor (hl)		;c8dd	ae		.
	ex af,af'		;c8de	08		.
	jp p,018b9h		;c8df	f2 b9 18	. . .
	ld b,(hl)		;c8e2	46		F
	ret nc			;c8e3	d0		.
	ld a,(de)		;c8e4	1a		.
	inc de			;c8e5	13		.
lc8e6h:
	ld h,001h		;c8e6	26 01		& .
	ld sp,hl		;c8e8	f9		.
	ret nz			;c8e9	c0		.
	ret po			;c8ea	e0		.
	in a,(0a7h)		;c8eb	db a7		. .
D_C8ED:
	ld a,b			;c8ed	78		x
	jp p,0c6b0h		;c8ee	f2 b0 c6	. . .
	and (hl)		;c8f1	a6		.
	cp (hl)			;c8f2	be		.
	ld b,e			;c8f3	43		C
	sub 0c5h		;c8f4	d6 c5		. .
	dec b			;c8f6	05		.
	jr $+47			;c8f7	18 2d		. -
	call z,0025eh		;c8f9	cc 5e 02	. ^ .
	ld h,a			;c8fc	67		g
	cp d			;c8fd	ba		.
lc8feh:
	call nz,06f1ah		;c8fe	c4 1a 6f	. . o
	ld h,03bh		;c901	26 3b		& ;
	inc de			;c903	13		.
	ld h,a			;c904	67		g
M_C905:
	rst 20h			;c905	e7		.
	jr c,$+1		;c906	38 ff		8 .
	ret nc			;c908	d0		.
lc909h:
	djnz lc8e6h		;c909	10 db		. .
R_C90B:
	cp d			;c90b	ba		.
	inc a			;c90c	3c		<
	ld (hl),a		;c90d	77		w
	ld e,e			;c90e	5b		[
	ret nz			;c90f	c0		.
	ld b,02bh		;c910	06 2b		. +
	or e			;c912	b3		.
	ld c,a			;c913	4f		O
	add hl,sp		;c914	39		9
	cp a			;c915	bf		.
	add a,020h		;c916	c6 20		.  
	jr c,$+45		;c918	38 2b		8 +
	ld (hl),d		;c91a	72		r
	sub (hl)		;c91b	96		.
	ld h,b			;c91c	60		`
	add a,(hl)		;c91d	86		.
	djnz $+102		;c91e	10 64		. d
	ld e,c			;c920	59		Y
	xor (hl)		;c921	ae		.
D_C922:
	jr nc,lc8d5h		;c922	30 b1		0 .
	jp z,0e440h		;c924	ca 40 e4	. @ .
	defb 0fdh,024h ;inc iyh	;c927	fd 24		. $
	adc a,a			;c929	8f		.
	inc h			;c92a	24		$
	ld (hl),e		;c92b	73		s
	or c			;c92c	b1		.
	sbc a,a			;c92d	9f		.
	sbc a,d			;c92e	9a		.
	or b			;c92f	b0		.
M_C930:
	xor (hl)		;c930	ae		.
	halt			;c931	76		v
	jr nz,lc8feh		;c932	20 ca		  .
	ld a,c			;c934	79		y
	exx			;c935	d9		.
	ld a,h			;c936	7c		|
	sub b			;c937	90		.
	jr lc909h		;c938	18 cf		. .
	jr z,lc95fh		;c93a	28 23		( #
	ld a,c			;c93c	79		y
	sbc a,e			;c93d	9b		.
M_C93E:
	ret			;c93e	c9		.
	cp h			;c93f	bc		.
	sbc a,e			;c940	9b		.
sub_c941h:
	ld a,c			;c941	79		y
	push af			;c942	f5		.
	ld (0b297h),hl		;c943	22 97 b2	" . .
	rst 28h			;c946	ef		.
	cp (hl)			;c947	be		.
	or h			;c948	b4		.
	ld d,a			;c949	57		W
	in a,(087h)		;c94a	db 87		. .
	ld c,e			;c94c	4b		K
	ld l,l			;c94d	6d		m
	cp (hl)			;c94e	be		.
	in a,(086h)		;c94f	db 86		. .
	ld e,d			;c951	5a		Z
	or l			;c952	b5		.
lc953h:
	jr $-84			;c953	18 aa		. .
	ld a,(072eah)		;c955	3a ea 72	: . r
	defb 0fdh,047h,02dh ;illegal sequence	;c958	fd 47 2d	. G -
	sub d			;c95b	92		.
	and a			;c95c	a7		.
	out (0f8h),a		;c95d	d3 f8		. .
lc95fh:
	dec (hl)		;c95f	35		5
	out (088h),a		;c960	d3 88		. .
	adc a,(hl)		;c962	8e		.
M_C963:
	jr z,lc953h		;c963	28 ee		( .
	cp 00fh			;c965	fe 0f		. .
	inc h			;c967	24		$
	sub h			;c968	94		.
	pop af			;c969	f1		.
	ei			;c96a	fb		.
	or 010h			;c96b	f6 10		. .
	ld l,0edh		;c96d	2e ed		. .
	ld h,e			;c96f	63		c
	ld c,e			;c970	4b		K
	add a,06ch		;c971	c6 6c		. l
	ld h,(hl)		;c973	66		f
	ld h,d			;c974	62		b
	ld c,h			;c975	4c		L
	ld c,035h		;c976	0e 35		. 5
	jp 044cah		;c978	c3 ca 44	. . D
	jp nc,05160h		;c97b	d2 60 51	. ` Q
	pop hl			;c97e	e1		.
	inc sp			;c97f	33		3
	ld a,h			;c980	7c		|
	cp (hl)			;c981	be		.
	ld e,b			;c982	58		X
	ld (hl),03ah		;c983	36 3a		6 :
	ld b,c			;c985	41		A
	dec a			;c986	3d		=
	jp z,0c78ah		;c987	ca 8a c7	. . .
	dec l			;c98a	2d		-
	ld e,d			;c98b	5a		Z
	ret po			;c98c	e0		.
	rst 30h			;c98d	f7		.
	ret po			;c98e	e0		.
	jp p,04a92h		;c98f	f2 92 4a	. . J
	and l			;c992	a5		.
	sbc a,l			;c993	9d		.
	or d			;c994	b2		.
	add a,e			;c995	83		.
	jp z,03a71h		;c996	ca 71 3a	. q :
	ld h,c			;c999	61		a
	ret po			;c99a	e0		.
D_C99B:
	or e			;c99b	b3		.
	xor b			;c99c	a8		.
	ret po			;c99d	e0		.
	exx			;c99e	d9		.
	adc a,09ch		;c99f	ce 9c		. .
	in a,(0feh)		;c9a1	db fe		. .
	adc a,b			;c9a3	88		.
M_C9A4:
	call nc,0e00ch		;c9a4	d4 0c e0	. . .
	ld l,0b5h		;c9a7	2e b5		. .
	ld e,(hl)		;c9a9	5e		^
	adc a,(hl)		;c9aa	8e		.
	and b			;c9ab	a0		.
	rst 8			;c9ac	cf		.
	ld de,D_DF10		;c9ad	11 10 df	. . .
	ld b,b			;c9b0	40		@
	di			;c9b1	f3		.
	call m,0d14eh		;c9b2	fc 4e d1	. N .
	ex (sp),hl		;c9b5	e3		.
	cpl			;c9b6	2f		/
	dec sp			;c9b7	3b		;
	add a,032h		;c9b8	c6 32		. 2
	dec e			;c9ba	1d		.
	rst 18h			;c9bb	df		.
	push hl			;c9bc	e5		.
	adc a,a			;c9bd	8f		.
	jp (hl)			;c9be	e9		.
	inc a			;c9bf	3c		<
	ld b,e			;c9c0	43		C
	rst 18h			;c9c1	df		.
	dec b			;c9c2	05		.
	ld d,b			;c9c3	50		P
	dec l			;c9c4	2d		-
	rst 18h			;c9c5	df		.
	ld c,0bfh		;c9c6	0e bf		. .
	rst 18h			;c9c8	df		.
	ld a,(hl)		;c9c9	7e		~
	or l			;c9ca	b5		.
	dec c			;c9cb	0d		.
	ld d,a			;c9cc	57		W
	ld d,l			;c9cd	55		U
	sub h			;c9ce	94		.
	rst 18h			;c9cf	df		.
	add a,l			;c9d0	85		.
	ex de,hl		;c9d1	eb		.
	ld h,a			;c9d2	67		g
	jp nc,031cbh		;c9d3	d2 cb 31	. . 1
	rst 18h			;c9d6	df		.
	adc a,(hl)		;c9d7	8e		.
	ret m			;c9d8	f8		.
	ld e,e			;c9d9	5b		[
M_C9DA:
	push hl			;c9da	e5		.
	halt			;c9db	76		v
	sub h			;c9dc	94		.
	inc sp			;c9dd	33		3
	rst 18h			;c9de	df		.
	ld a,b			;c9df	78		x
	sub b			;c9e0	90		.
	call nz,0df5fh		;c9e1	c4 5f df	. _ .
	dec h			;c9e4	25		%
	rst 18h			;c9e5	df		.
	call m,0867fh		;c9e6	fc 7f 86	. . .
	ld hl,03577h		;c9e9	21 77 35	! w 5
	ld (bc),a		;c9ec	02		.
	sbc a,096h		;c9ed	de 96		. .
lc9efh:
	ld (M_C9DA),hl		;c9ef	22 da c9	" . .
	exx			;c9f2	d9		.
	push de			;c9f3	d5		.
	scf			;c9f4	37		7
	xor d			;c9f5	aa		.
M_C9F6:
	ld (hl),b		;c9f6	70		p
	jp c,0bdddh		;c9f7	da dd bd	. . .
	inc (hl)		;c9fa	34		4
M_C9FB:
	add ix,bc		;c9fb	dd 09		. .
	ld e,(hl)		;c9fd	5e		^
D_C9FE:
	ld (hl),h		;c9fe	74		t
	ld b,l			;c9ff	45		E
	jr z,lc9efh		;ca00	28 ed		( .
	call c,063f0h		;ca02	dc f0 63	. . c
M_CA05:
	rlca			;ca05	07		.
	call c,0fe0eh		;ca06	dc 0e fe	. . .
	call c,02782h		;ca09	dc 82 27	. . '
D_CA0C:
	call c,07a0fh		;ca0c	dc 0f 7a	. . z
	cp l			;ca0f	bd		.
	ld a,(M_C44A)		;ca10	3a 4a c4	: J .
	rst 30h			;ca13	f7		.
	in a,(0b2h)		;ca14	db b2		. .
sub_ca16h:
	rst 30h			;ca16	f7		.
	rlca			;ca17	07		.
	sub b			;ca18	90		.
	or h			;ca19	b4		.
	call z,sub_c1ffh	;ca1a	cc ff c1	. . .
	ld a,(de)		;ca1d	1a		.
	sub h			;ca1e	94		.
	inc h			;ca1f	24		$
	call m,0c95ah		;ca20	fc 5a c9	. Z .
	ld hl,(M_C9F6)		;ca23	2a f6 c9	* . .
	or (hl)			;ca26	b6		.
	in a,(0b7h)		;ca27	db b7		. .
	cp d			;ca29	ba		.
	call 071b2h		;ca2a	cd b2 71	. . q
	ld a,(de)		;ca2d	1a		.
	ld e,(hl)		;ca2e	5e		^
	dec h			;ca2f	25		%
	ret			;ca30	c9		.
D_CA31:
	or 059h			;ca31	f6 59		. Y
	call nz,066cdh		;ca33	c4 cd 66	. . f
	ld a,(bc)		;ca36	0a		.
	in a,(0f9h)		;ca37	db f9		. .
	cpl			;ca39	2f		/
	adc a,b			;ca3a	88		.
	call nz,02513h		;ca3b	c4 13 25	. . %
	dec sp			;ca3e	3b		;
	ld hl,(0326fh)		;ca3f	2a 6f 32	* o 2
	ld a,c			;ca42	79		y
	push af			;ca43	f5		.
	rst 28h			;ca44	ef		.
	adc a,a			;ca45	8f		.
	jp (hl)			;ca46	e9		.
	add a,a			;ca47	87		.
	ld c,d			;ca48	4a		J
	or 063h			;ca49	f6 63		. c
	inc b			;ca4b	04		.
	ld c,h			;ca4c	4c		L
	dec c			;ca4d	0d		.
	sbc a,c			;ca4e	99		.
	ld c,h			;ca4f	4c		L
	sbc a,02ch		;ca50	de 2c		. ,
	ld sp,hl		;ca52	f9		.
	add hl,bc		;ca53	09		.
	ld (hl),b		;ca54	70		p
	ld d,e			;ca55	53		S
	inc d			;ca56	14		.
	ld sp,hl		;ca57	f9		.
	ld h,0f9h		;ca58	26 f9		& .
	adc a,d			;ca5a	8a		.
	dec b			;ca5b	05		.
	cpl			;ca5c	2f		/
	ld d,a			;ca5d	57		W
	ld c,d			;ca5e	4a		J
	inc l			;ca5f	2c		,
	ld h,a			;ca60	67		g
	ld c,c			;ca61	49		I
D_CA62:
	ld sp,hl		;ca62	f9		.
	ld h,a			;ca63	67		g
	rst 38h			;ca64	ff		.
	ld e,h			;ca65	5c		\
	defb 0fdh,0ech,0a2h ;illegal sequence	;ca66	fd ec a2	. . .
	rst 10h			;ca69	d7		.
	sub (hl)		;ca6a	96		.
	or e			;ca6b	b3		.
	ld sp,hl		;ca6c	f9		.
	call 0187ah		;ca6d	cd 7a 18	. z .
	call nz,0d4dfh		;ca70	c4 df d4	. . .
	jr z,lca99h		;ca73	28 24		( $
	cp 028h			;ca75	fe 28		. (
	ret m			;ca77	f8		.
	in a,(069h)		;ca78	db 69		. i
	dec bc			;ca7a	0b		.
	ret			;ca7b	c9		.
	sbc a,c			;ca7c	99		.
	xor l			;ca7d	ad		.
	cp 02bh			;ca7e	fe 2b		. +
	ld hl,D_D272		;ca80	21 72 d2	! r .
	ld c,h			;ca83	4c		L
	ld a,0ffh		;ca84	3e ff		> .
	ld h,a			;ca86	67		g
	scf			;ca87	37		7
	dec d			;ca88	15		.
	ret z			;ca89	c8		.
	rla			;ca8a	17		.
	dec d			;ca8b	15		.
	rst 8			;ca8c	cf		.
	adc a,h			;ca8d	8c		.
	rla			;ca8e	17		.
	sbc a,(hl)		;ca8f	9e		.
	add a,d			;ca90	82		.
	ld b,b			;ca91	40		@
D_CA92:
	push bc			;ca92	c5		.
	rla			;ca93	17		.
	sbc a,031h		;ca94	de 31		. 1
	exx			;ca96	d9		.
	cp h			;ca97	bc		.
	xor h			;ca98	ac		.
lca99h:
	add a,a			;ca99	87		.
	ld c,a			;ca9a	4f		O
	inc de			;ca9b	13		.
M_CA9C:
	ret nz			;ca9c	c0		.
	ret po			;ca9d	e0		.
	jr nz,lcaa0h		;ca9e	20 00		  .
lcaa0h:
	ld a,b			;caa0	78		x
sub_caa1h:
	ld c,b			;caa1	48		H
	ld d,d			;caa2	52		R
	pop hl			;caa3	e1		.
R_CAA4:
	dec bc			;caa4	0b		.
	rlc a			;caa5	cb 07		. .
	nop			;caa7	00		.
	nop			;caa8	00		.
	nop			;caa9	00		.
	nop			;caaa	00		.
	nop			;caab	00		.
	nop			;caac	00		.
	rra			;caad	1f		.
	cp 021h			;caae	fe 21		. !
; --------------------------------------------------------------------------
; DATA 0xCAB0-0xFFFF  engine data/tables tail
; --------------------------------------------------------------------------
DATA_CAB0:
	defb E1 CB 18 32 C3 53 C8 1B  ;CAB0  ...2.S..
	defb 00 7E FE 3D 55 6E 69 50  ;CAB8  .~.=UniP
	defb 54 32 2F 17 44 33 53 F1  ;CAC0  T2/.D3S.
	defb 07 2D 6C 61 79 65 72 20  ;CAC8  .-layer 
	defb F8 BF 2E 30 3D AF 67 6F  ;CAD0  ...0=.go
	defb 32 A4 C9 22 A5 61 A1 2B  ;CAD8  2..".a.+
	defb CA 4F 45 2C F1 67 D5 E5  ;CAE0  .OE,.g..
	defb 3C FC 21 22 C9 36 00 11  ;CAE8  <.!".6..
	defb 23 01 0E 13 6C ED B0 37  ;CAF0  #...l..7
	defb 27 F8 85 FA 0C E1 FD FD  ;CAF8  '.......
	defb 87 3A 0A F0 F0 C0 E6 02  ;CB00  .:......
	defb C2 E0 CD 29 F0 FF 21 18  ;CB08  ...)..!.
	defb 1F 22 FC C5 3E BA 32 C7  ;CB10  ."..>.2.
	defb D6 D9 F3 7C 62 7B CA 8B  ;CB18  ...|b{..
	defb 4F F6 87 FF 37 ED E1 DD  ;CB20  O...7...
	defb 7E A9 D6 30 38 04 4F 89  ;CB28  ~..08.O.
	defb FE 0A 02 FE 06 9A C4 DF  ;CB30  ........
	defb CE F5 FE 04 FF 17 E6 6B  ;CB38  .......k
	defb 9B 07 F5 BB 0E CA BB 33  ;CB40  .......3
	defb F1 30 28 37 FE 10 27 3A  ;CB48  .0(7..':
	defb 33 91 F4 07 2C 26 8C 36  ;CB50  3...,&.6
	defb 4B 20 25 21 8E 11 AA B1  ;CB58  K %!....
	defb 64 8B E7 00 4C 1A B0 FD  ;CB60  d...L...
	defb CB 9E CE 4F 87 81 D6 02  ;CB68  ...O....
	defb 32 5E C7 18 03 91 E2 01  ;CB70  2^......
	defb 2A 65 6B C6 9A 07 E6 C3  ;CB78  *ek.....
	defb 21 00 63 EA 7B 6A 2A 48  ;CB80  !.c.{j*H
	defb FD 61 21 CB 51 D6 5B 7A  ;CB88  .a!.Q.[z
	defb BB 7A 7A B5 F5 7A 80 92  ;CB90  .zz..z..
	defb 66 7A 05 85 57 14 02 AB  ;CB98  fz..W...
	defb 8D 8F 10 84 49 F1 B9 C4  ;CBA0  ....I...
	defb C5 8C E3 20 2D C0 A0 86  ;CBA8  ... -...
	defb 11 40 CA ED 43 D1 C2 22  ;CBB0  .@..C.."
	defb 60 C7 D5 11 ED C8 01 92  ;CBB8  `.......
	defb CA 1A 13 FE 1E 30 06 67  ;CBC0  .....0.g
	defb 6F 3F FE 18 07 D5 16 00  ;CBC8  o?......
	defb 5F 19 D1 7C DF CF 02 0B  ;CBD0  _..|....
	defb 7D D6 F0 20 5A A4 E3 3C  ;CBD8  }.. Z..<
	defb E9 90 17 67 79 E5 21 F0  ;CBE0  ...gy.!.
	defb 22 41 EA 5E FD EC 7B B3  ;CBE8  "A.^..{.
	defb 9F C8 E5 56 F6 02 DE 9D  ;CBF0  ...V....
	defb 65 33 59 67 50 6D 75 D6  ;CBF8  e3YgPmu.
	defb BA D7 F3 9D F4 F1 21 9A  ;CC00  ......!.
	defb C8 C5 4C BD 87 BC E0 A6  ;CC08  ..L.....
	defb 5E 23 CB 3B 9F E6 A7 32  ;CC10  ^#.;...2
	defb BA C1 EB 01 31 CA 09 1A  ;CC18  ....1...
	defb C6 AA 4F CE C8 91 47 C5  ;CC20  ..O...G.
	defb 11 3F D5 06 0C 1F F8 4E  ;CC28  .?.....N
	defb 23 E5 46 EB 11 17 00 8D  ;CC30  #.F.....
	defb F1 DD 26 08 CB 38 8D F0  ;CC38  ..&..8..
	defb F3 79 8A 77 23 78 C7 0F  ;CC40  .y.w#x..
	defb DD 25 20 EF D1 13 6C F8  ;CC48  .% ...l.
	defb E1 23 C1 10 DA 7F 58 D1  ;CC50  .#....X.
	defb 7B FE B6 20 05 3E FD 32  ;CC58  {.. .>.2
	defb 4F CB 1A A7 28 11 1F F5  ;CC60  O...(...
	defb 87 4F 09 F1 30 02 35 34  ;CC68  .O..0.54
	defb A7 ED 14 33 42 52 FF 9B  ;CC70  ...3BR..
	defb EB F1 FE 05 21 11 00 54  ;CC78  ....!..T
	defb 5C 3E 17 30 03 2D 5D AF  ;CC80  \>.0.-].
	defb 32 10 C2 DD 21 93 0E 0F  ;CC88  2...!...
	defb E5 19 EB ED 62 06 10 34  ;CC90  ....b..4
	defb 1F 7D 7C CE 9F 77 4E FD  ;CC98  .}|..wN.
	defb E0 19 F3 E1 89 6F B4 77  ;CCA0  .....o.w
	defb 20 01 1C 0D 10 43 E2 0E  ;CCA8   ....C..
	defb 90 A5 CD 9C C2 E5 11 64  ;CCB0  .......d
	defb 00 19 7E FD 77 08 E5 F9  ;CCB8  ..~.w...
	defb E1 33 0B AA 5E 02 D9 E2  ;CCC0  .3..^...
	defb 31 B1 D1 DD 6E 03 AC E7  ;CCC8  1...n...
	defb 66 04 95 33 B3 94 A9 DE  ;CCD0  f..3....
	defb C8 2C A3 69 5E 3C FD 75  ;CCD8  .,.i^<.u
	defb FA 74 FB C9 FF E4 D0 19  ;CCE0  .t......
	defb 8F 23 7E CD 26 FA 5A 1C  ;CCE8  .#~.&.Z.
	defb 56 E1 77 29 48 52 B3 84  ;CCF0  V.w)HR..
	defb 6B E0 43 D5 1E 20 43 68  ;CCF8  k.C.. Ch
	defb 26 46 B9 B5 AF 5F 30 CD  ;CD00  &F..._0.
	defb B1 E1 09 C5 FC C5 66 C5  ;CD08  ......f.
	defb 7C 93 59 F6 F7 7C E6 F8  ;CD10  |.Y..|..
	defb 99 D5 F9 04 56 F3 05 CD  ;CD18  ....V...
	defb 67 06 07 9F 59 13 66 35  ;CD20  g...Y.f5
	defb 14 0C CF 7C 0D 8D 94 6E  ;CD28  ...|...n
	defb 33 05 19 E8 C6 C3 2F FC  ;CD30  3...../.
	defb 69 C5 18 9B 35 4A 3C 08  ;CD38  i...5J<.
	defb 5F 86 45 36 2E 77 22 0A  ;CD40  _.E6.w".
	defb B4 9F 03 6F 67 0F E6 F8  ;CD48  ...og...
	defb B8 18 33 F8 4A 2E E1 38  ;CD50  ..3.J..8
	defb 3C 05 F9 6A 28 0F 21 EF  ;CD58  <..j(.!.
	defb 10 5D 17 83 FC 1A DD 89  ;CD60  .]......
	defb D4 CB 09 D6 0A 31 FB F9  ;CD68  .....1..
	defb D4 CF F7 0B 87 9F CF BF  ;CD70  ........
	defb 0C 37 18 01 A7 08 38 19  ;CD78  .7....8.
	defb C6 88 3F 38 A9 3C 51 60  ;CD80  ..?8.<Q`
	defb 3E 3C 28 A7 64 1F 0F CA  ;CD88  ><(.d...
	defb 49 C4 E5 AC A5 10 B3 40  ;CD90  I......@
	defb 3E 9C B4 54 27 B6 BC 9F  ;CD98  >..T'...
	defb FD BE F6 B3 0A 12 93 D6  ;CDA0  ........
	defb C3 F7 E1 CB 53 4F B1 96  ;CDA8  ....SO..
	defb D4 FC BE BF 6C B3 AE BB  ;CDB0  ....l...
	defb 42 DE 09 2C 6F 3C 1A 61  ;CDB8  B..,o<.a
	defb 16 83 C4 3E AF A7 A5 DD  ;CDC0  ...>....
	defb C6 9E D5 08 30 16 56 20  ;CDC8  ....0.V 
	defb 0C 7E 93 FD A9 8E 08 67  ;CDD0  .~.....g
	defb 6F 3C CD 64 74 70 36 F9  ;CDD8  o<.dtp6.
	defb 01 D9 CF DF F5 F4 3F FB  ;CDE0  ......?.
	defb FA 26 73 FB C3 8F 36 00  ;CDE8  .&s...6.
	defb 5A 93 44 AD 0F 34 DF 22  ;CDF0  Z.D..4."
	defb 3F 44 39 0F A4 19 D9 31  ;CDF8  ?D9....1
	defb 3D 20 69 6A 07 86 29 27  ;CE00  = ij..)'
	defb B4 99 F6 2F 22 D1 99 19  ;CE08  .../"...
	defb 2D 85 2A C4 C8 37 EB 87  ;CE10  -.*..7..
	defb 6E B2 66 FB BF F9 22 A0  ;CE18  n.f...".
	defb C4 11 10 20 D2 83 38 AA  ;CE20  ... ..8.
	defb 82 28 03 0E 49 AF AD 36  ;CE28  .(..I..6
	defb 25 A6 C3 B5 BB 1E 20 6C  ;CE30  %..... l
	defb 75 C3 82 B2 D0 0F C2 BE  ;CE38  u.......
	defb 75 6F 21 A5 A4 6C 56 D5  ;CE40  uo!..lV.
	defb 18 D0 53 53 BC 95 CE 57  ;CE48  ..SS...W
	defb 36 09 86 AE 06 69 BE 47  ;CE50  6....i.G
	defb AF ED 73 47 B0 F9 AF 90  ;CE58  ..sG....
	defb 4F 25 F3 CF 31 4D 4E 9D  ;CE60  O%..1MN.
	defb 05 0F C9 12 AA 0B CD 30  ;CE68  .......0
	defb 95 7D C4 D1 E2 8E F3 42  ;CE70  .}.....B
	defb E0 A5 BE F6 24 34 74 B9  ;CE78  ....$4t.
	defb F5 07 87 6F 26 B4 23 66  ;CE80  ...o&.#f
	defb A5 A3 BF 3E AC 5B D4 2C  ;CE88  ...>.[.,
	defb A4 E3 E4 E1 0D 74 5E 74  ;CE90  .....t^t
	defb 0E D1 FF FC FE 06 38 09  ;CE98  ......8.
	defb 11 8F 31 E4 73 E4 72 4E  ;CEA0  ..1.s.rN
	defb 1E 08 28 01 5D 52 F2 B8  ;CEA8  ..(.]R..
	defb 34 68 2F 08 44 C9 D5 78  ;CEB0  4h/.D..x
	defb 0C 0B 46 7B E8 FE 00 C9  ;CEB8  ..F{....
	defb 44 4A A2 02 A7 FA 6E 67  ;CEC0  DJ....ng
	defb FE D2 9F 27 84 36 39 78  ;CEC8  ...'.69x
	defb 18 7A 6C D7 F5 EB 50 69  ;CED0  .zl...Pi
	defb F5 F6 53 FF FE 09 DB 85  ;CED8  ..S.....
	defb 43 ED AF 7C A0 D3 20 1E  ;CEE0  C..|.. .
	defb 0E F3 F6 0F 0C A4 D7 AC  ;CEE8  ........
	defb 4B 97 AC 0B 7C E0 08 21  ;CEF0  K...|..!
	defb AC CB 4E C8 32 8F F3 26  ;CEF8  ..N.2..&
	defb 2C 16 EE 36 CA 95 73 08  ;CF00  ,..6..s.
	defb BA D2 AF 67 D5 AF 4B 9A  ;CF08  ...g..K.
	defb BC F4 C8 F9 31 A3 C3 BF  ;CF10  ....1...
	defb CF 16 00 57 26 E3 6E 52  ;CF18  ...W&.nR
	defb 66 F9 F6 D4 34 2C 23 41  ;CF20  f...4,#A
	defb F7 B6 69 30 01 30 02 C9  ;CF28  ..i0.0..
	defb 3B C1 75 A7 FA FB C4 FE  ;CF30  ;.u.....
	defb B8 31 76 F8 04 68 C5 C4  ;CF38  .1v..h..
	defb 7A F6 50 E1 E7 1E CF ED  ;CF40  z.P.....
	defb F4 87 05 36 62 6D EB C3  ;CF48  ...6bm..
	defb BE CB 28 AF 32 E2 C7 CB  ;CF50  ..(.2...
	defb 6F 18 15 46 E5 CA F8 C6  ;CF58  o..F....
	defb 84 A5 10 42 A6 7C F9 DA  ;CF60  ...B.|..
	defb 66 0E F9 D1 67 B1 00 6F  ;CF68  f...g..o
	defb 39 3C CB 7C 38 01 9A E5  ;CF70  9<.|8...
	defb 9F 49 7E 12 86 F2 D6 C5  ;CF78  .I~.....
	defb AF FE 10 F4 36 02 3E 5F  ;CF80  ....6.>_
	defb 87 80 D9 9C D2 F5 10 D1  ;CF88  ........
	defb A3 BD DC 01 47 87 9A 39  ;CF90  ....G..9
	defb F9 78 2E EA D4 C1 E1 FF  ;CF98  .x......
	defb 7F 60 20 06 EB A7 ED F3  ;CFA0  .` .....
	defb 2F 62 A9 51 CB 19 9F 2F  ;CFA8  /b.Q.../
	defb E6 3E 6F E8 18 A1 4F 0C  ;CFB0  .>o...O.
	defb 8E 78 1F 1A 9B 5F E6 9F  ;CFB8  .x..._..
	defb 47 DD 5E BE 56 32 3B 09  ;CFC0  G.^.V2;.
	defb 70 28 5E CF 4D 75 74 09  ;CFC8  p(^.Mut.
	defb EB FC 1F C6 21 6F CE CB  ;CFD0  ....!o..
	defb 95 67 F9 E1 4A C9 44 F8  ;CFD8  .g..J.D.
	defb 06 F8 07 D3 25 05 E3 F9  ;CFE0  ....%...
	defb B6 05 F7 3E 49 0E 35 20  ;CFE8  ...>I.5 
	defb 6F 7E 69 08 2E 79 88 17  ;CFF0  o~i..y..
	defb 9B B4 88 18 7C 20 DC 74  ;CFF8  ....| .t
	defb 26 49 07 48 1D 1F 41 3C  ;D000  &I.H..A<
	defb D0 69 5B 6C 3A 3A 99 FA  ;D008  .i[l::..
	defb 89 3F F2 13 77 12 5B 17  ;D010  .?..w.[.
	defb BF C9 C3 D3 EF 02 E7 EC  ;D018  ........
	defb CB 79 28 13 71 07 FE D9  ;D020  .y(.q...
	defb 13 0F 0B 3C 18 05 E6 9C  ;D028  ...<....
	defb F1 04 3D 26 FF 02 6F 78  ;D030  ..=&..ox
	defb E6 0F 85 F2 AC 8C FE 10  ;D038  ........
	defb 6D 93 2A 2F B6 1C F2 66  ;D040  m.*/...f
	defb 7B CA 7B 7E CB 41 20 CD  ;D048  {.{~.A .
	defb E8 C0 B6 14 E5 CB 78 C4  ;D050  ......x.
	defb 0D 8E 1A 17 2F BA B7 DD  ;D058  ..../...
	defb 86 04 0F 50 E5 68 1C FD  ;D060  ...P.h..
	defb 86 B9 28 12 61 18 0E 7D  ;D068  ..(.a..}
	defb 99 83 03 77 11 A1 E4 CD  ;D070  ...w....
	defb 4D 20 E6 48 FD BA 84 3B  ;D078  M .H...;
	defb 0F 33 A7 E1 45 0A C8 46  ;D080  .3..E..F
	defb 9C 39 C0 AE 15 9B A2 D7  ;D088  .9......
	defb 7E 3A 98 0B 38 DC 0C 94  ;D090  ~:..8...
	defb EE B2 C9 AF C4 2F D0 DC  ;D098  ...../..
	defb 3D 0E 35 09 AE CF C2 CF  ;D0A0  =.5.....
	defb C7 BA 20 69 2E 1E 4E FE  ;D0A8  .. i..N.
	defb 46 FF AE BB 94 53 57 DF  ;D0B0  F....SW.
	defb 14 E8 37 9B 66 05 23 7E  ;D0B8  ..7.f.#~
	defb 2B 4B 3C FA 08 06 CB 9E  ;D0C0  +K<.....
	defb 66 07 E6 66 37 CD CB 9E  ;D0C8  f..f7...
	defb 4E 8E 8F 94 D6 2F 3D 93  ;D0D0  N..../=.
	defb 59 07 CB BA FB FC A5 67  ;D0D8  Y......g
	defb 66 19 5E A3 CB 0A 56 F7  ;D0E0  f.^...V.
	defb 41 8D C7 C0 44 08 3F 4D  ;D0E8  A...D.?M
	defb 95 B6 4D EC 74 01 C5 27  ;D0F0  ..M.t..'
	defb 02 03 39 1D B5 11 AB FF  ;D0F8  ..9.....
	defb E9 CD FD 71 74 B3 9D 70  ;D100  ...qt..p
	defb FF 92 D7 20 12 DB 98 FE  ;D108  ... ....
	defb C8 8F DB 46 01 9E A4 49  ;D110  ...F...I
	defb E9 FD 01 FD F4 52 D3 E5  ;D118  .....R..
	defb 96 4C CC FD 03 3D C9 5D  ;D120  .L...=.]
	defb D3 FD 03 7E D3 98 66 09  ;D128  ...~..f.
	defb FA 9F 94 15 8F 79 94 16  ;D130  .....y..
	defb CD A5 C5 AB 39 7E 74 16  ;D138  ....9~t.
	defb 3E 49 D3 1D FD BC 3A D3  ;D140  >I....:.
	defb 17 92 6E 18 F2 7B 74 18  ;D148  ..n..{t.
	defb 3A 13 9D CC 1E 06 1D FA  ;D150  :.......
	defb D9 64 33 82 FA 1A E6 1E  ;D158  .d3.....
	defb 89 6C FA 1A 1F 9E 8D B3  ;D160  .l......
	defb 27 86 11 DE F3 1B 12 93  ;D168  '.......
	defb 49 B9 9F 17 F4 13 6A A6  ;D170  I.....j.
	defb 66 14 40 0C 40 0D 97 66  ;D178  f.@.@..f
	defb 52 20 D6 21 E2 B6 9C 99  ;D180  R .!....
	defb 0F C8 73 92 49 C0 F0 0E  ;D188  ..s.I...
	defb F3 0F F7 0A C3 95 F7 0B  ;D190  ........
	defb F7 7F 37 FE FB CD 1C C7  ;D198  ..7.....
	defb 5B 84 3A 22 13 07 54 78  ;D1A0  [.:"..Tx
	defb AF 75 CD 4A FF 4F 0F 02  ;D1A8  .u.J.O..
	defb ED 41 08 F9 9C CD 82 6F  ;D1B0  .A.....o
	defb 04 C8 42 2F ED 9E 37 79  ;D1B8  ..B/..7y
	defb F9 23 AF 11 BF FF 7F F1  ;D1C0  .#......
	defb 43 A3 42 3C FE 0D BC 9E  ;D1C8  C.B<....
	defb 20 F5 7E A7 F8 0B F2 C9  ;D1D0   .~.....
	defb FD F9 64 2A 65 00 01 0C  ;D1D8  ..d*e...
	defb 19 E4 94 35 30 0E 58 8C  ;D1E0  ...50.X.
	defb 88 21 01 05 09 0B 0D 0F  ;D1E8  .!......
	defb 13 15 19 25 3D 00 5D 87  ;D1F0  ...%=.].
	defb C4 31 37 4D 53 5F 71 82  ;D1F8  .17MS_q.
	defb 8C 9C 9E A0 A6 A8 AA AC  ;D200  ........
	defb AE 06 62 00 57 1F 23 25  ;D208  ..b.W.#%
	defb 29 2D 2F 33 BF 00 1D F5  ;D210  )-/3....
	defb FC AA 27 2B 2D 31 55 BD  ;D218  ..'+-1U.
	defb CF CE 1B 21 2B 3B 4D 91  ;D220  ...!+;M.
	defb AA 5F BB 01 F1 98 0D D8  ;D228  ._......
	defb 69 70 76 7D 85 8D 95 9D  ;D230  ipv}....
	defb A8 B1 BB 0C DA 62 68 6D  ;D238  .....bhm
	defb 75 7B 83 8A 92 9C A4 AF  ;D240  u{......
	defb B8 0E 08 6A 72 78 7E 86  ;D248  ...jrx~.
	defb 90 96 A0 AA B4 BE 2B BB  ;D250  ......+.
	defb 2C 78 88 80 98 99 C5 B0  ;D258  ,x......
	defb A8 E0 E8 EA 00 2B 80 B6  ;D260  .....+..
	defb 07 EC 00 80 48 52 AB 0B  ;D268  ....HR..
	defb 9B 06 00 00 00 00 00 00  ;D270  ........
	defb 3B FB 21 AB CB C3 20 C0  ;D278  ;.!... .
	defb 94 F8 C0 50 53 43 56 31  ;D280  ...PSCV1
	defb 2E 32 30 83 5F 50 4C 41  ;D288  .20._PLA
	defb 59 45 52 5F 0C 42 4B 56  ;D290  YER_.BKV
	defb 41 06 FE 22 B3 C0 EB 21  ;D298  A.."...!
	defb 4C 00 19 53 C1 2F 2E 5C  ;D2A0  L..S./.\
	defb C2 41 C3 CF 1F 2B 7E 23  ;D2A8  .A...+~#
	defb 66 6F E1 09 68 3F 3C 70  ;D2B0  fo..h?<p
	defb 56 C3 3E 18 32 DD 0C CD  ;D2B8  V.>.2...
	defb F7 00 22 30 C9 17 92 E3  ;D2C0  .."0....
	defb A1 74 F6 C4 AF 32 5F C5  ;D2C8  .t...2_.
	defb 7A 5E 5C 89 C6 9E C7 3C  ;D2D0  z^\....<
	defb 68 26 AB C0 F7 03 A2 6D  ;D2D8  h&.....m
	defb 7C D5 DD E1 7E 49 62 21  ;D2E0  |...~Ib!
	defb F3 C3 6E 47 89 99 E6 48  ;D2E8  ..nG...H
	defb 14 32 22 B6 C0 21 9D CB  ;D2F0  .2"..!..
	defb 06 0D AF 77 23 10 FC 21  ;D2F8  ...w#..!
	defb AA C0 A8 DD EF 89 BA 39  ;D300  .......9
	defb C3 AF C8 E9 2B 01 18 D3  ;D308  ....+...
	defb C3 EA AF A4 CB 00 3D C1  ;D310  ......=.
	defb 9B 3B C3 DC 5C B3 20 31  ;D318  .;..\. 1
	defb 11 10 F5 FF FD F1 FE FF  ;D320  ........
	defb 20 05 E1 19 B3 CF F9 18  ;D328   .......
	defb F6 79 2B E8 EF FA C1 9B  ;D330  .y+.....
	defb F7 ED 73 A9 22 B3 B5 DD  ;D338  ..s."...
	defb C2 8B 01 E2 62 E4 EF C1  ;D340  ....b...
	defb 98 E6 D2 C2 C7 01 D0 8E  ;D348  ........
	defb C7 C4 20 19 7B 93 A7 42  ;D350  .. .{..B
	defb FE 7C 38 27 67 39 A0 16  ;D358  .|8'g9..
	defb 90 3A C0 61 D6 BF 9B 9F  ;D360  .:.a....
	defb C0 21 EE C1 ED 43 3E C3  ;D368  .!...C>.
	defb 06 1B B8 FE 7E 36 FB 27  ;D370  ....~6.'
	defb 7D 28 23 06 04 C3 F1 C0  ;D378  }(#.....
	defb 4E 27 67 53 F5 57 1B D6  ;D380  N'gS.W..
	defb F0 27 20 02 3E 0F 32 63  ;D388  .' .>.2c
	defb C9 A0 F9 10 28 01 6A 68  ;D390  ....(.jh
	defb 13 6A CB F0 7D 36 D9 60  ;D398  .j..}6.`
	defb B2 4E 47 5D F8 D6 4C FA  ;D3A0  .NG]..L.
	defb 80 87 D9 6F 26 00 60 69  ;D3A8  ...o&.`i
	defb B0 9D 70 53 C9 FF 52 FC  ;D3B0  ..pS..R.
	defb A0 DE 25 59 F6 4B 6B 28  ;D3B8  ..%Y.Kk(
	defb 16 3D C7 74 17 9A FD EC  ;D3C0  .=.t....
	defb 2A B3 9F 2F 32 51 F7 39  ;D3C8  *../2Q.9
	defb 23 39 8A B2 CD 87 97 32  ;D3D0  #9.....2
	defb 74 AC 4D B3 E8 7E 71 CB  ;D3D8  t.M..~q.
	defb 9A CA 02 E0 CA AA A3 5B  ;D3E0  .......[
	defb C1 C0 E9 3A C8 40 15 D8  ;D3E8  ...:.@..
	defb C8 26 1E 64 C1 2F 42 87  ;D3F0  .&.d./B.
	defb 18 2B DC FC FE 7A DA 7C  ;D3F8  .+...z.|
	defb C2 20 F2 C2 D6 C3 23 B6  ;D400  . ....#.
	defb FB D0 FB DC D1 77 49 AC  ;D408  .....wI.
	defb 5A 18 0E 58 A1 F5 E9 28  ;D410  Z..X...(
	defb 12 F5 5E 11 3A F5 BA 57  ;D418  ..^.:..W
	defb CA 21 D1 C2 F5 36 E9 85  ;D420  .!...6..
	defb F5 34 F5 8B A2 FC C1 F5  ;D428  .4......
	defb A4 F5 1A 78 D3 F5 16 C4  ;D430  ...x....
	defb 49 40 F5 1B 04 5D C4 F5  ;D438  I@...]..
	defb 18 B1 69 94 01 F6 F3 BF  ;D440  ..i.....
	defb 18 A8 F7 06 EF 38 C4 94  ;D448  .....8..
	defb A9 FC F8 0C 06 96 80 3D  ;D450  .......=
	defb 03 F9 B1 44 26 F9 27 AF  ;D458  ...D&.'.
	defb F9 24 CD 26 AA 71 F9 56  ;D460  .$.&.q.V
	defb C4 05 CF F9 0A 39 F9 4B  ;D468  .....9.K
	defb D3 F9 73 34 64 C4 4B F9  ;D470  ..s4d.K.
	defb C5 1D 0B AC 09 42 07 12  ;D478  .....B..
	defb 62 DE E0 21 BB C3 1D E3  ;D480  b..!....
	defb 3F 56 12 DF E5 7F C2 12  ;D488  ?V......
	defb D4 6A EB 1D D9 1D 20 71  ;D490  .j.... q
	defb 12 64 FB 1E DC BF 12 FD  ;D498  .d......
	defb CF B2 99 1B C4 2F F3 DF  ;D4A0  ...../..
	defb 1A 2D 79 CA 74 81 19 7A  ;D4A8  .-y.t..z
	defb E6 D2 DC 19 E5 19 E2 19  ;D4B0  ........
	defb BA 96 9A 19 14 C5 F1 AC  ;D4B8  ........
	defb 92 53 19 19 35 AD 43 B6  ;D4C0  .S..5.C.
	defb 19 31 66 C1 12 D0 3A A2  ;D4C8  .1f...:.
	defb BB 2F B7 C2 75 C5 66 1E  ;D4D0  ./..u.f.
	defb A1 BE 18 13 01 DC 77 1B  ;D4D8  ......w.
	defb 66 CC 32 9C EC 3A AA FF  ;D4E0  f.2..:..
	defb 47 64 F1 3A 89 C6 B1 4F  ;D4E8  Gd.:...O
	defb CB 10 30 23 0E B3 6B 06  ;D4F0  ..0#..k.
	defb DF DF C6 88 A6 ED 28 91  ;D4F8  ......(.
	defb 98 2E 1A C7 D5 8A 97 0D  ;D500  ........
	defb A8 75 DC 98 A9 49 D9 0A  ;D508  .u...I..
	defb C6 76 28 A9 86 4B 62 73  ;D510  .v(..Kbs
	defb 16 2E 00 2E 18 06 29 7D  ;D518  ......)}
	defb FF 7D 2F 6F 23 F6 F0 A3  ;D520  .}/o#...
	defb 9A AB F3 DA E1 B5 66 31  ;D528  ......f1
	defb 3A C5 87 FD CD 11 F0 C9  ;D530  :.......
	defb 11 D1 2A 9F CB B7 ED 52  ;D538  ..*....R
	defb B5 39 A6 1E 30 C5 CB 7C  ;D540  .9..0..|
	defb 31 23 20 07 CE 6F BA C9  ;D548  1# ..o..
	defb 23 1E 38 CA 7B C7 99 CB  ;D550  #.8.{...
	defb 18 C2 81 1E 04 77 28 06  ;D558  .....w(.
	defb 59 FA 05 F6 80 ED 44 7D  ;D560  Y.....D}
	defb 8A D3 23 21 32 D7 95 02  ;D568  ..#!2...
	defb 0E C7 26 B4 89 79 B6 65  ;D570  ..&..y.e
	defb FE C2 6B A4 71 79 42 C7  ;D578  ..k.qyB.
	defb BF 9E C7 42 02 99 16 C8  ;D580  ...B....
	defb F4 EE AF A6 68 8C C8 E0  ;D588  ....h...
	defb C8 5C 9F 97 0D AB 75 F1  ;D590  .\....u.
	defb AD A2 56 BB 42 34 B6 61  ;D598  ..V.B4.a
	defb 04 36 D0 59 42 05 D0 69  ;D5A0  .6.YB..i
	defb 42 08 33 F1 42 C5 B9 F9  ;D5A8  B.3.B...
	defb 42 A1 02 33 42 A3 8C 69  ;D5B0  B..3B..i
	defb 42 CA 60 60 F5 CB 33 41  ;D5B8  B.``..3A
	defb 42 7D 71 3A 39 BF 5A 37  ;D5C0  B}q:9.Z7
	defb 95 35 93 42 4E BE 21 71  ;D5C8  .5.BN.!q
	defb 9E 83 3A 82 C5 85 32 02  ;D5D0  ..:...2.
	defb 7E E4 53 93 04 FC C9 C7  ;D5D8  ~.S.....
	defb 4A 6D F8 42 A5 CB 4E 74  ;D5E0  Jm.B..Nt
	defb DC 53 CA 84 E6 53 B1 4B  ;D5E8  .S...S.K
	defb 96 0E BC C5 D0 2F 20 F9  ;D5F0  ...../ .
	defb C1 81 08 7A 80 F2 96 C5  ;D5F8  ...z....
	defb D6 AA 57 56 38 0F 0F 01  ;D600  ..WV8...
	defb 79 17 03 AF CF 22 AB C5  ;D608  y...."..
	defb 0C CB 43 FC AD 20 06 0B  ;D610  ..C.. ..
	defb C3 B7 C5 CB 83 37 F1 9D  ;D618  .....7..
	defb 8B ED C2 E6 73 89 C5 08  ;D620  ....s...
	defb D2 32 7B 13 6A 3D 52 9A  ;D628  .2{.j=R.
	defb 3C C1 DE AA FE BB 88 D0  ;D630  <.......
	defb C5 09 CB 67 28 10 EB 49  ;D638  ...g(..I
	defb 94 AB C0 09 9B E8 98 CB  ;D640  ........
	defb A7 FC DE EB 9B 9D 3C D1  ;D648  ......<.
	defb 57 7B E6 09 90 54 A9 06  ;D650  W{...T..
	defb 89 23 EE D3 35 53 05 F5  ;D658  .#..5S..
	defb B5 A6 FA 12 C6 20 03 22  ;D660  ..... ."
	defb 5E 32 26 4D 6F 7C 19 E1  ;D668  ^2&Mo|..
	defb FE 10 CE 65 98 FB 13 C6  ;D670  ...e....
	defb 07 DD AF 84 FC DF 2A 9C  ;D678  ......*.
	defb CA 19 7E D9 CB 63 20 02  ;D680  ..~..c .
	defb F6 4D C6 D8 3F 9E 12 FE  ;D688  .M..?...
	defb 9C 5B 0E 7D 17 9F 67 ED  ;D690  .[.}..g.
	defb 4B EF 4A 8B 09 87 18 0A  ;D698  K.J.....
	defb 86 9D 08 85 6F 4E B6 F9  ;D6A0  ....oN..
	defb A3 08 7B 57 06 2A CD 6F  ;D6A8  ..{W.*.o
	defb ED C5 22 6C C6 3F 5A 2D  ;D6B0  .."l.?Z-
	defb 19 3F 78 FB 9A 39 66 3F  ;D6B8  .?x..9f?
	defb 92 3F 31 4C 41 87 90 3B  ;D6C0  .?1LA..;
	defb E3 33 75 A5 F1 99 8A B4  ;D6C8  .3u.....
	defb 02 F1 6F 7A 03 F1 A5 C6  ;D6D0  ..oz....
	defb B9 F4 F1 BA 4C 7A B2 F1  ;D6D8  ....Lz..
	defb C6 B2 23 F8 F1 47 9D 47  ;D6E0  ..#..G.G
	defb F1 40 60 70 02 23 F1 10  ;D6E8  .@`p.#..
	defb 0E 41 F1 59 34 82 4F 6C  ;D6F0  .A.Y4.Ol
	defb F9 F1 17 E6 12 4F 3A EF  ;D6F8  .....O:.
	defb B1 9B 09 EB 27 C7 BC C1  ;D700  ....'...
	defb EB AF 34 D9 76 2E 48 81  ;D708  ..4.v.H.
	defb 4D EB 28 C7 CC 60 EB 39  ;D710  M.(..`.9
	defb 9F 12 EB DC C6 22 81 12  ;D718  ....."..
	defb 7F C7 EB 8D 0E 3E 49 EB  ;D720  .....>I.
	defb 10 53 3B 05 2D A4 EB D5  ;D728  .S;.-...
	defb EB A7 5D E9 EB AF BD 45  ;D730  ..]....E
	defb 33 EB E0 6F 7A AC EB BA  ;D738  3..oz...
	defb C7 B9 F4 EB CF 4C 7A B2  ;D740  .....Lz.
	defb EB DB B2 DA F8 EB AD C7  ;D748  ........
	defb EB 3C EB 19 06 02 EB F4  ;D750  .<......
	defb C7 90 C0 EB A2 43 02 A1  ;D758  .....C..
	defb 2D F0 EB EA 24 80 81 EA  ;D760  -...$...
	defb C1 6B C8 EA 34 BC 81 75  ;D768  .k..4..u
	defb 44 C0 27 EA 05 B6 C8 30  ;D770  D.'....0
	defb 83 EA 38 7D 4A EA F1 C7  ;D778  ..8}J...
	defb 22 97 C8 6F FC EA A3 C8  ;D780  "..o....
	defb E5 C0 EA CB 34 3A 9E 34  ;D788  ....4:.4
	defb F0 9F C6 11 BF FF 0E FD  ;D790  ........
	defb 3A AA CB FE FF 70 21 A9  ;D798  :....p!.
	defb 3E 0C 28 09 23 76 F1 3C  ;D7A0  >.(.#v.<
	defb 42 ED 79 43 AB 3D AC 16  ;D7A8  B.yC.=..
	defb 4B EB 3E FF B5 8D 4C 93  ;D7B0  K.>...L.
	defb C2 C9 23 BB 71 4B DC FC  ;D7B8  ..#.qK..
	defb 71 D0 D2 95 1E DE 3E 8B  ;D7C0  q.....>.
	defb 3B 3A C5 96 8D 6F DA 46  ;D7C8  ;:...o.F
	defb 24 F7 84 89 C6 76 71 13  ;D7D0  $....vq.
	defb 4F B7 36 A5 7A FB 71 E1  ;D7D8  O.6.z.q.
	defb C5 CF DC 71 E4 DA F3 36  ;D7E0  ...q...6
	defb 71 77 8F 34 71 CC 76 9D  ;D7E8  qw.4q.v.
	defb 71 A6 9E C0 7C CD 71 CA  ;D7F0  q...|.q.
	defb 7B 21 B0 CB 33 B3 71 7D  ;D7F8  {!..3.q}
	defb 15 A7 0E 0C FB AB 95 33  ;D800  .......3
	defb 66 71 4E FE B1 CD 5F DD  ;D808  fqN..._.
	defb F9 DF 78 DC 0E 07 3E 0D  ;D810  ..x...>.
	defb 80 0C F8 C6 CC 0B 22 82  ;D818  ......".
	defb 0A EC 09 5C 6E DC D6 08  ;D820  ...\n...
	defb 58 E0 07 6E 74 CF 04 9F  ;D828  X..nt...
	defb 06 40 EE D9 E2 E6 91 41  ;D830  .@.....A
	defb F6 04 AE 46 CF 6B 2C F4  ;D838  ...F.k,.
	defb EC F0 03 B7 82 4F C8 62  ;D840  .....O.b
	defb 59 F3 E4 59 CF A1 7B 3D  ;D848  Y..Y..{=
	defb EB 57 36 16 EB D9 F8 01  ;D850  .W6.....
	defb DC C1 A8 67 3D 90 79 F5  ;D858  ...g=.y.
	defb AC 64 50 3D 2C 8D 9E 1B  ;D860  .dP=,...
	defb 0B EB D9 FC 00 EE E0 D4  ;D868  ........
	defb 67 3D C8 BD F5 AC B2 A8  ;D870  g=......
	defb 9F 96 B3 9E 8D 85 7A D6  ;D878  ......z.
	defb 7E 77 70 6A 59 CF 64 5E  ;D880  ~wpjY.d^
	defb 3D EB 59 54 50 AC 67 4B  ;D888  =.YTP.gK
	defb 47 43 9E F5 3F 3C 9E 95  ;D890  GC..?<..
	defb 38 35 E3 D4 B3 2F 2D 2A  ;D898  85.../-*
	defb 28 63 4C E2 24 56 13 CC  ;D8A0  (cL.$V..
	defb 33 29 1C 5F 0C BE BD 01  ;D8A8  3)._....
	defb A5 FE F7 6D 02 9B D4 AF  ;D8B0  ...m....
	defb 52 03 A0 BD 94 F2 04 7D  ;D8B8  R......}
	defb FF E5 45 EE 28 05 F6 A0  ;D8C0  ..E.(...
	defb E5 89 DA 23 06 18 29 2F  ;D8C8  ...#..)/
	defb F9 11 35 E5 41 A8 1B 08  ;D8D0  ..5.A...
	defb A3 3E 4F 64 08 09 05 05  ;D8D8  .>Od....
	defb 28 2F D2 41 DB 0A 5B B0  ;D8E0  (/.A..[.
	defb 78 46 09 41 DD 0B 5D B0  ;D8E8  xF.A..].
	defb 91 09 05 75 0C 41 45 6F  ;D8F0  ...u.AEo
	defb 54 0D 0F CA 95 28 50 51  ;D8F8  T....(PQ
	defb FF 16 BF 0F 09 C0 03 48  ;D900  .......H
	defb 52 65 0B C0 06 00 00 00  ;D908  Re......
	defb 00 00 00 99 FD 21 65 CB  ;D910  .....!e.
	defb C3 B8 C7 F3 62 C1 64 35  ;D918  ....b.d5
	defb C2 EF 47 C0 36 00 3E 02  ;D920  ..G.6.>.
	defb 08 CD 1A C1 79 B7 20 33  ;D928  ....y. 3
	defb 3E 00 F5 CD CE 57 FD C8  ;D930  >....W..
	defb 5E 23 56 3A 1E C3 42 62  ;D938  ^#V:..Bb
	defb 4E 23 46 EB F1 5F B7 ED  ;D940  N#F.._..
	defb 42 22 28 C3 3E C2 38 02  ;D948  B"(.>.8.
	defb F3 7F CA 32 33 C3 26 00  ;D950  ...23.&.
	defb 6B 38 05 6C 55 66 AA 52  ;D958  k8.lUf.R
	defb FD 2B 98 B0 C3 50 77 10  ;D960  .+...Pw.
	defb C3 4F C5 EF FA C3 59 C5  ;D968  .O....Y.
	defb FC 5D C3 64 C5 A4 CE C3  ;D970  .].d....
	defb 5C 60 C2 C3 84 DF 41 C3  ;D978  \`....A.
	defb 83 C7 BD EB C3 8D C7 F3  ;D980  ........
	defb 77 C3 98 C7 C3 2E 36 90  ;D988  w.....6.
	defb DB 31 C2 62 C4 FC 2F 96  ;D990  .1.b../.
	defb C6 DD 7D 32 5A CB AF C3  ;D998  ..}2Z...
	defb 3F 7E CA 21 00 7E 23 6B  ;D9A0  ?~.!.~#k
	defb EF 4E 87 30 07 E6 8B 22  ;D9A8  .N.0..."
	defb DA C0 6F 1F 02 CC 85 30  ;D9B0  ..o....0
	defb 01 9F 9D 24 11 19 79 32  ;D9B8  ...$..y2
	defb FB 17 17 C1 2C C4 60 66  ;D9C0  ....,.`f
	defb FF C6 ED 73 17 C1 F9 E1  ;D9C8  ...s....
	defb 22 21 CF C3 EB 57 C3 70  ;D9D0  "!...W.p
	defb CE 8B C5 31 E9 72 C9 21  ;D9D8  ...1.r.!
	defb 61 ED F0 34 CC FF D6 1A  ;D9E0  a..4....
	defb 3C CC D9 C0 CD 9F C1 ED  ;D9E8  <.......
	defb 53 DE 72 6E FD 32 CB 0E  ;D9F0  S.rn.2..
	defb 01 5E 1D CD B8 C8 23 C1  ;D9F8  .^....#.
	defb 6F A8 32 3C C2 AF 46 04  ;DA00  o.2<..F.
	defb C5 E5 4F ED 43 C1 22 32  ;DA08  ..O.C."2
	defb 59 64 5D FB DD 50 18 49  ;DA10  Yd]..P.I
	defb 0E 03 78 C2 3C 4E 7D 72  ;DA18  ..x.<N}r
	defb D2 39 6E 89 0F 28 27 FE  ;DA20  .9n..('.
	defb 0F 0F 37 1C 8E CB 3E 37  ;DA28  ..7...>7
	defb 08 CD E2 C3 1A 13 6F 67  ;DA30  ......og
	defb FA FF CF 17 F6 32 CD CA  ;DA38  .....2..
	defb 6F 7A 68 DE 18 0F 76 4E  ;DA40  ozh...vN
	defb FA B7 F9 64 09 8B C0 69  ;DA48  ...d...i
	defb C2 04 FE 34 13 DC C6 E5  ;DA50  ...4....
	defb EC 21 38 8F 6C 47 B3 9C  ;DA58  .!8.lG..
	defb 20 81 72 95 10 BA 32 4E  ;DA60   .r...2N
	defb AA 9B CE 6E 28 17 3D 96  ;DA68  ...n(.=.
	defb 8B 0C 1E 1A B4 CB 9E 2F  ;DA70  ......./
	defb 0F C0 18 D0 B6 F0 1B AF  ;DA78  ........
	defb 08 63 62 C8 6F C6 79 22  ;DA80  .cb.o.y"
	defb AF 6F 01 B9 CE BB 9B 29  ;DA88  .o.....)
	defb B1 84 B5 CD C8 D4 1C FE  ;DA90  ........
	defb 13 39 F5 9F 27 32 F5 1E  ;DA98  .9..'2..
	defb F1 C6 00 31 46 DF 77 E3  ;DAA0  ...1F.w.
	defb AC 97 AF 71 CE 3A 89 45  ;DAA8  ...q.:.E
	defb BC BD 40 9A 37 34 67 6F  ;DAB0  ..@.74go
	defb 22 A4 4C EB 31 2F 08 4F  ;DAB8  ".L.1/.O
	defb B7 C9 29 66 11 3D F2 C3  ;DAC0  ..)f.=..
	defb 14 6A EC AE C9 F3 13 EF  ;DAC8  .j......
	defb 22 7D BC 20 03 2E 9E EA  ;DAD0  "}. ....
	defb 2C F7 35 87 83 5F BE 5C  ;DAD8  ,.5.._.\
	defb AE 14 EB BB 1B C3 F9 C1  ;DAE0  ........
	defb B5 5F 48 80 CB 71 28 03  ;DAE8  ._H..q(.
	defb 93 99 BB 20 A3 69 AA 81  ;DAF0  ... .i..
	defb 79 D2 8B AB 9D 00 D2 4A  ;DAF8  y......J
	defb FE 11 53 FF C4 C7 65 8A  ;DB00  ..S...e.
	defb 6F 4F 6E 44 29 DE E6 09  ;DB08  oOnD)...
	defb 19 F9 D4 C1 3B 37 26 D2  ;DB10  ....;7&.
	defb 77 BE 1E FF 38 99 20 09  ;DB18  w...8. .
	defb 60 5E 59 CB E9 F4 9B CD  ;DB20  `^Y.....
	defb C1 09 1E A4 FB 78 03 6D  ;DB28  .....x.m
	defb 70 20 02 8F 27 83 DD 6B  ;DB30  p ..'..k
	defb 22 66 F3 C3 C1 AF 16 00  ;DB38  "f......
	defb FD 69 19 3D 16 62 FC 61  ;DB40  .i.=.b.a
	defb 02 E6 01 82 F2 D3 C2 FE  ;DB48  ........
	defb F1 CE 00 18 04 A6 68 10  ;DB50  ......h.
	defb AE 99 D2 34 57 79 FC 0F  ;DB58  ...4Wy..
	defb FC E3 AF B7 E0 CE 0F 13  ;DB60  ........
	defb 4F 5F B9 51 48 CB 9F 26  ;DB68  O_.QH..&
	defb 35 47 B4 7A B4 61 EA 24  ;DB70  5G.z.a.$
	defb B7 4D 44 84 2C AB AF 72  ;DB78  .MD.,..r
	defb 08 E6 0F 30 05 22 5E CB  ;DB80  ...0."^.
	defb F6 10 3E 53 AC 19 B6 32  ;DB88  ..>S...2
	defb 5B CB C6 B4 FC 6B 52 4F  ;DB90  [....kRO
	defb 7D 4C CD A9 C8 ED E7 01  ;DB98  }L......
	defb 09 B1 18 F5 CB 7C 45 1D  ;DBA0  .....|E.
	defb A4 19 F4 AA 23 F6 21 19  ;DBA8  ....#.!.
	defb 0D EC 7F 18 22 CB AF DD  ;DBB0  ...."...
	defb 2E FF 77 EF 18 C7 21 62  ;DBB8  ..w...!b
	defb CA CD 84 98 D0 C3 DB B2  ;DBC0  ........
	defb DF 3B CF EF EE CF 6D C4  ;DBC8  .;....m.
	defb C9 77 CF 66 C4 22 63 82  ;DBD0  .w.f."c.
	defb 62 3F 8E 81 CD BF B3 DB  ;DBD8  b?......
	defb CF A9 C4 CF A3 74 8E 9F  ;DBE0  .....t..
	defb DD BF 6C DE CF 39 C5 CF  ;DBE8  ..l..9..
	defb 30 FF 07 3B A7 CF C5 B7  ;DBF0  0..;....
	defb CF 9A C4 C3 35 CB 60 ED  ;DBF8  ....5.`.
	defb 6F CF 58 F3 58 CF 7B 76  ;DC00  o.X.X.{v
	defb 66 CF CC 84 12 F3 CF 44  ;DC08  f......D
	defb C5 C3 39 CF 4F 5D 09 F0  ;DC10  ..9.O]..
	defb BA A7 CF 49 4C 77 97 CF  ;DC18  ...ILw..
	defb A2 ED A7 49 BA EE 29 EA  ;DC20  ...I..).
	defb 40 CD 96 34 CF D5 4C 60  ;DC28  @..4..L`
	defb BB 1C C5 CF 59 61 B7 CF  ;DC30  ....Ya..
	defb 64 C6 1E 43 60 AE FC 4C  ;DC38  d..C`..L
	defb CF D5 FB DD CF 4C C5 99  ;DC40  .....L..
	defb A9 CF BB 51 A3 A6 EE CF  ;DC48  ...Q....
	defb AB CE 29 29 CF 7B A9 7F  ;DC50  ..)).{..
	defb FE 11 C4 14 98 8A 1B D3  ;DC58  ........
	defb CF 77 BE DD ED 7E 5D CF  ;DC60  .w...~].
	defb A3 A6 FB CF D6 76 6D CF  ;DC68  .....vm.
	defb 8B 08 EC CF 53 C5 FE EE  ;DC70  ....S...
	defb CF 04 C5 3C D3 CF 34 81  ;DC78  ...<..4.
	defb DD CF 14 C5 33 7D CF 24  ;DC80  ....3}.$
	defb BB 03 CF 5B 60 5C B6 4A  ;DC88  ...[`\.J
	defb CF F5 CF 76 DB 1A CF 55  ;DC90  ...v...U
	defb F0 B4 CF 43 68 EC CF 3F  ;DC98  ...Ch..?
	defb C8 DD 7D F6 F7 13 12 6F  ;DCA0  ..}....o
	defb AF 18 C4 21 63 9A DB CC  ;DCA8  ...!c...
	defb 04 C6 D8 8B A5 70 E0 63  ;DCB0  .....p.c
	defb EE FE CC A1 C6 7C F7 CC  ;DCB8  .....|..
	defb 9A C6 22 97 45 56 C2 7F  ;DCC0  ..".EV..
	defb 9D B5 B7 9B CC DD C6 1C  ;DCC8  ........
	defb 67 CC D7 D3 7F E9 BC BB  ;DCD0  g.......
	defb CC 6D C7 0F D8 CC 64 4E  ;DCD8  .m....dN
	defb FF CC 6F 77 CC CE C6 83  ;DCE0  ..ow....
	defb 91 C3 69 EA BF 1D CC 95  ;DCE8  ..i.....
	defb 7B B7 CC 90 C7 3B B3 CC  ;DCF0  {....;..
	defb 00 AD 4C 60 CC 78 C3 39  ;DCF8  ..L`.x.9
	defb 29 48 CC 83 9E 6E F5 98  ;DD00  )H...n..
	defb CC 31 E9 77 97 9F 26 CC  ;DD08  .1.w..&.
	defb D6 49 EE A1 B3 22 05 C9  ;DD10  .I..."..
	defb 40 B9 B3 5B CC 09 C7 D8  ;DD18  @..[....
	defb AE 50 CC 8D D8 2D CC 98  ;DD20  .P...-..
	defb B1 47 10 98 AE 3F D3 CC  ;DD28  .G...?..
	defb D5 7E 37 CC 80 C7 66 EA  ;DD30  .~7...f.
	defb CC BB 85 A9 7B A3 CC 11  ;DD38  ....{...
	defb C9 AB 02 C7 CC AF A6 FF  ;DD40  ........
	defb C4 53 60 8A FF 4C CC 77  ;DD48  .S`..L.w
	defb FD B6 CC AB 3B D3 CC 6D  ;DD50  ....;..m
	defb 76 6B CC 93 CC 87 C7 77  ;DD58  vk.....w
	defb 04 CC 38 69 7F C7 CC 6C  ;DD60  ..8i...l
	defb 9E 34 CC 48 05 06 CC F4  ;DD68  .4.H....
	defb CC 24 ED 0E CC 5D 6D 81  ;DD70  .$...]m.
	defb D8 2A CC F5 CC AA 6E 6B  ;DD78  .*....nk
	defb CC 57 C1 D3 CC 0F A1 EC  ;DD80  .W......
	defb CC ED 26 CC 24 A6 DB CC  ;DD88  ..&.$...
	defb F3 E5 F3 45 09 95 82 04  ;DD90  ...E....
	defb 6E 80 39 1D 66 7E 87 C1  ;DD98  n.9.f~..
	defb F3 D4 5F 54 44 B3 C2 DA  ;DDA0  .._TD...
	defb C0 D6 E3 E5 64 52 09 8B  ;DDA8  ....dR..
	defb E6 D2 A4 C8 E5 CA C1 98  ;DDB0  ........
	defb 8B 88 C3 BC C5 FF 92 9E  ;DDB8  ........
	defb E3 37 CF 73 68 9C C9 C9  ;DDC0  .7.sh...
	defb C5 E1 F9 C8 FE 40 30 FC  ;DDC8  .....@0.
	defb 17 89 41 E1 E5 3D 97 99  ;DDD0  ..A..=..
	defb 20 F9 F9 4B F2 36 F9 DF  ;DDD8   ..K.6..
	defb F7 C0 F1 37 0E E1 7C 3C  ;DDE0  ...7..|<
	defb 20 04 7D 03 35 28 05 18  ;DDE8   .}.5(..
	defb 65 6A F2 B1 FE B5 95 3D  ;DDF0  ej.....=
	defb 6B AB D6 9C B6 5B 05 D0  ;DDF8  k....[..
	defb 8D 2E 5D 3F 70 26 9B C2  ;DE00  ..]?p&..
	defb BA B6 CE FF 2E DD 16 69  ;DE08  .......i
	defb 21 44 75 00 13 71 2E 5F  ;DE10  !Du..q._
	defb 3C 7B DE 6D 65 91 3F 67  ;DE18  <{.me.?g
	defb 74 6D 95 C6 95 D5 2F 01  ;DE20  tm..../.
	defb CB 3B B4 F9 2A 6B 2B 19  ;DE28  .;..*k+.
	defb B0 2B AB E1 09 BD 6B 37  ;DE30  .+....k7
	defb 68 21 9F CA 6D 65 58 86  ;DE38  h!..meX.
	defb 94 75 B7 47 58 BA 6A DC  ;DE40  .u.GX.j.
	defb C3 AF 34 FF CA 87 C6 D7  ;DE48  ..4.....
	defb 6F CE C8 95 67 0D 66 DA  ;DE50  o...g.f.
	defb 0F 19 EB C9 87 4F 06 87  ;DE58  .....O..
	defb 9E 01 BE B2 92 63 7C C9  ;DE60  .....c|.
	defb 1E 72 5F 8F DB C9 F8 0E  ;DE68  .r_.....
	defb F8 1B 10 60 0D 80 0C D8  ;DE70  ...`....
	defb 0B 28 1B DF 88 0A F0 09  ;DE78  .(......
	defb 60 E0 26 8C 08 58 07 7C  ;DE80  `.&..X.|
	defb 63 30 BB 71 B0 06 40 EC  ;DE88  c0.q..@.
	defb 05 94 D9 3D 44 F8 04 B0  ;DE90  ...=D...
	defb DD E8 70 2C FD 03 BE 8C  ;DE98  ..p,....
	defb 9E 84 58 CF 4E CD F6 02  ;DEA0  ..X.N...
	defb CA E2 59 A2 7C 58 EC C6  ;DEA8  ..Y.|X..
	defb 31 16 F8 01 DF 9E F5 C2  ;DEB0  1.......
	defb AC D1 B3 90 7B 65 51 67  ;DEB8  ....{eQg
	defb 8B 96 2C 76 A3 1C 0A FC  ;DEC0  ..,v....
	defb 00 EF CF 7A E1 D6 E9 59  ;DEC8  ...z...Y
	defb C8 BD B2 A8 EB 59 11 96  ;DED0  .....Y..
	defb 8E 67 3D 85 7E F5 AC 77  ;DED8  .g=.~..w
	defb 70 6B 64 B3 9E 5E 59 8B  ;DEE0  pkd..^Y.
	defb D1 54 4F AA AC 67 47 42  ;DEE8  .TO..gGB
	defb 3F 9E F5 3B 38 9E 2D 35  ;DEF0  ?..;8.-5
	defb DD 2F D6 B3 2C 2A 27 25  ;DEF8  ./..,*'%
	defb B6 78 23 F6 1F CF 7A 1D  ;DF00  .x#...z.
	defb 1C E2 59 1A 19 17 EA 59  ;DF08  ..Y....Y
	defb EC 15 13 12 9E ED F5 10  ;DF10  ........
	defb C2 60 0F ED FD 01 F9 F7  ;DF18  .`......
	defb AF 52 02 A2 BE 8C DA EF  ;DF20  .R......
	defb F8 03 BD 28 CF A6 83 BA  ;DF28  ...(....
	defb 04 47 CA 3E 9B 05 0E DA  ;DF30  .G.>....
	defb EA A2 BA 52 06 9C 68 83  ;DF38  ...R..h.
	defb 94 B6 07 FF 77 8E 44 2E  ;DF40  ....w.D.
	defb EA A0 90 2A D0 F6 09 5E  ;DF48  ...*...^
	defb 50 8F 51 09 05 75 0A D4  ;DF50  P.Q..u..
	defb 05 CF 63 0A 0B AD 06 D1  ;DF58  ..c.....
	defb 46 5D C4 0C 35 A8 46 7D  ;DF60  F]..5.F}
	defb D1 0D 0F DA 46 95 D2 0E  ;DF68  ....F...
	defb 83 FA 33 DA 0F BF 7F 1F  ;DF70  ..3.....
	defb 11 21 53 CB 71 23 10 FC  ;DF78  .!S.q#..
	defb 9F 76 2F FE 60 0E FD 11  ;DF80  .v/.`...
	defb A9 93 BF FF 49 00 28 0D  ;DF88  ....I.(.
	defb 5F 78 F5 3E 42 ED 79 43  ;DF90  _x.>B.yC
	defb 17 3F F1 96 39 A9 2A 5E  ;DF98  .?..9.*^
	defb CB FE 16 AA DE 28 15 A5  ;DFA0  .....(..
	defb AA D2 18 2A F8 0C AC BB  ;DFA8  ...*....
	defb 61 3D 66 D2 69 E8 5D FF  ;DFB0  a=f.i.].
	defb 0A BC D5 AB C2 9C 9D 05  ;DFB8  ........
	defb 00 C9 0C FE 43 80 07 48  ;DFC0  ....C..H
	defb 52 68 07 0F 05 11 00 10  ;DFC8  Rh......
	defb 00 0F 00 BF F8 21 68 C7  ;DFD0  .....!h.
	defb C3 4E C0 01 C1 ED 4B 62  ;DFD8  .N....Kb
	defb 3E FF C9 E2 C1 00 53 41  ;DFE0  >.....SA
	defb 20 3B F8 4F 46 54 57 52  ;DFE8   ;.OFTWR
	defb 45 F8 2F 43 4D 50 49 4C  ;DFF0  E./CMPIL
	defb 41 54 67 80 4E 20 3A 63  ;DFF8  ATg.N :c
	defb 22 9D 1F 52 47 49 56 67  ;E000  "..RGIVg
	defb 11 4D 2E 1E FC 22 28 43  ;E008  .M..."(C
	defb 29 11 27 4A 41 4D 1F 9F  ;E010  ).'JAM..
	defb 00 70 FF C3 80 F1 D0 F3  ;E018  .p......
	defb 22 F0 C0 3E FC 32 B3 8B  ;E020  "..>.2..
	defb 9C C4 06 C5 7F B8 73 7E  ;E028  ......s~
	defb 23 14 C1 CD EA 1E 00 C0  ;E030  #.......
	defb 78 C6 23 CF 7D 7C 46 BC  ;E038  x.#.}|F.
	defb 22 82 9A 41 E9 CE B6 8A  ;E040  "..A....
	defb E5 A3 C3 2C 2E B3 C2 CD  ;E048  ...,....
	defb A4 CA DD 44 9E F4 54 FA  ;E050  ...D..T.
	defb 9F 80 C1 EB 7E 36 00 E1  ;E058  ....~6..
	defb B7 28 0D D8 78 C9 EB 2B  ;E060  .(..x..+
	defb 72 73 23 E6 FF 3D 20 F3  ;E068  rs#..= .
	defb 21 49 C0 0E 03 3B 33 F6  ;E070  !I...;3.
	defb E5 F6 F5 1B A5 87 67 6F  ;E078  ......go
	defb 27 C4 EE E3 66 9A 21 4E  ;E080  '...f.!N
	defb BC DD 32 0B C1 3D 75 C6  ;E088  ..2..=u.
	defb AF 9F 26 D3 C5 F5 F4 06  ;E090  ..&.....
	defb 0D 77 62 F3 23 10 FC 79  ;E098  .wb.#..y
	defb 32 33 01 FD 3E 7F 3E 0C  ;E0A0  23..>.>.
	defb ED 79 AF 06 BF FC F1 C3  ;E0A8  .y......
	defb C5 EB 5E 23 56 E6 60 01  ;E0B0  ..^#V.`.
	defb FD D2 57 09 C9 52 36 BF  ;E0B8  ..W..R6.
	defb F3 F9 73 C5 16 A7 0F D9  ;E0C0  ..s.....
	defb 01 F0 60 E9 FF 35 07 CD  ;E0C8  ..`..5..
	defb C2 E8 C1 80 23 F2 59 B2  ;E0D0  ....#.Y.
	defb D0 C3 00 B6 CA 68 F2 3C  ;E0D8  .....h.<
	defb FE 21 1E 38 0D 91 30 3F  ;E0E0  .!.8..0?
	defb FB 3E FD 62 41 C3 9F C1  ;E0E8  .>.bA...
	defb D6 80 C0 98 61 D2 C4 F4  ;E0F0  ....a...
	defb 83 99 3D 80 32 69 C5 CC  ;E0F8  ..=.2i..
	defb 98 6E 83 13 87 EF 57 6F  ;E100  .n....Wo
	defb 67 22 AF 93 A5 C3 CB CC  ;E108  g"......
	defb B3 D9 B2 17 9F B3 5B 73  ;E110  ......[s
	defb BA C1 28 F1 61 8F 2A 5D  ;E118  ..(.a.*]
	defb D8 0C 22 E1 D4 AF F2 87  ;E120  ..".....
	defb D9 C7 4F 5F 9B 19 F9 E1  ;E128  ..O_....
	defb 7E 87 55 49 FC 84 DE C9  ;E130  ~.UI....
	defb 99 80 DC 14 75 FA C3 C1  ;E138  ....u...
	defb 32 CC DA 36 69 D3 59 C4  ;E140  2..6i.Y.
	defb EE 1F 9B B6 34 FF 5E C4  ;E148  ....4.^.
	defb 11 46 C0 ED 53 5A C5 93  ;E150  .F..SZ..
	defb 64 4D C6 93 A0 A2 3C 4A  ;E158  dM....<J
	defb 9C 60 DB 67 52 B8 F4 0F  ;E160  .`.gR...
	defb DB 82 AA B3 98 83 93 C3  ;E168  ........
	defb A6 93 D4 6A 62 5F 33 D6  ;E170  ...jb_3.
	defb B1 7E 3D C2 26 19 DB F6  ;E178  .~=.&...
	defb FD 9F DC 26 7F C4 26 9B  ;E180  ...&..&.
	defb 2F 26 32 B4 99 23 3B 26  ;E188  /&2..#;&
	defb 73 C2 00 CD 26 5B 26 AA  ;E190  s...&[&.
	defb 4A D3 26 37 26 07 A2 41  ;E198  J.&7&..A
	defb 53 91 93 0C 62 49 29 99  ;E1A0  S...bI).
	defb 3E D8 67 32 DF 9B 86 10  ;E1A8  >.g2....
	defb C4 F2 D3 28 D3 34 65 2C  ;E1B0  ...(.4e,
	defb FD 60 FB 51 19 2C DE 9E  ;E1B8  .`.Q.,..
	defb 49 2C DA 7F 55 E6 AE 2C  ;E1C0  I,..U..,
	defb F8 C3 2C EE A6 58 56 D1  ;E1C8  ..,..XV.
	defb 6D 37 7B D2 32 D4 C2 34  ;E1D0  m7{.2..4
	defb A7 2C 02 6D BE 80 2C D7  ;E1D8  .,.m..,.
	defb 9A 61 A2 E5 94 F3 B3 52  ;E1E0  .a.....R
	defb 32 29 B5 A6 E4 27 E9 E2  ;E1E8  2)...'..
	defb 49 A2 DE BD 83 B8 78 53  ;E1F0  I.....xS
	defb F2 99 1A C9 1A 98 64 D4  ;E1F8  ......d.
	defb 1A D9 49 C6 1A C2 1A B5  ;E200  ..I.....
	defb 9C D8 BD CC 65 9A 00 18  ;E208  ....e...
	defb 8C 01 20 0E 5F 7E 2B C0  ;E210  .. ._~+.
	defb 8E 10 65 C6 19 8B 10 63  ;E218  ..e....c
	defb 01 E3 10 9A C4 CC 10 4B  ;E220  .......K
	defb 3D A9 35 9E 7C 50 10 3E  ;E228  =.5.|P.>
	defb 13 A6 B1 23 31 CD 10 66  ;E230  ...#1..f
	defb 10 D6 FE 10 41 C8 7B EB  ;E238  ....A.{.
	defb 7F 45 65 DE 79 26 10 DA  ;E240  .Ee.y&..
	defb FE 55 91 BB 10 3C C4 10  ;E248  .U...<..
	defb 48 CE 15 31 A6 76 D2 65  ;E250  H..1.v.e
	defb EE DF CD 69 10 46 DA FC  ;E258  ...i.F..
	defb 10 1B 34 C3 A2 29 94 37  ;E260  ..4..).7
	defb 48 41 36 9A CE 28 10 2D  ;E268  HA6..(.-
	defb 27 D5 A2 DE 4E 8A 10 4D  ;E270  '...N..M
	defb F5 67 E2 10 C9 92 C9 10  ;E278  .g......
	defb D4 10 19 63 D9 10 C2 72  ;E280  ...c...r
	defb 26 10 D8 BD 26 D5 9A 10  ;E288  &...&...
	defb B0 4A 27 10 3B 35 C1 D2  ;E290  .J'.;5..
	defb 22 08 CD 74 F6 F7 ED 75  ;E298  "..t...u
	defb B5 43 4A 48 46 AD 1D F3  ;E2A0  .CJHF...
	defb DE 13 AD B5 F7 6D AD 17  ;E2A8  .....m..
	defb F6 69 22 6B 6B 9C 2E CF  ;E2B0  .i"kk...
	defb 54 FC FE 01 28 D6 58 03  ;E2B8  T...(.X.
	defb 46 CE 9E EB E6 C2 83 5A  ;E2C0  F......Z
	defb 57 CF 6D 22 0F DA 89 6D  ;E2C8  W.m"...m
	defb B7 A9 6D BB 8D 6E DB AD  ;E2D0  ..m..n..
	defb B8 98 7D BC D6 46 6D BE  ;E2D8  ..}..Fm.
	defb 92 BC 9C 6B A5 D3 A9 62  ;E2E0  ...k...b
	defb 5A E1 51 00 B4 1F 29 EA  ;E2E8  Z.Q...).
	defb 0B 95 1A CA B0 A1 56 D4  ;E2F0  ......V.
	defb BA A2 3B 6E AF 11 00 16  ;E2F8  ..;n....
	defb 5E 20 1C 7B FE 20 02 1F  ;E300  ^ .{. ..
	defb 93 D1 D8 73 79 86 87 AD  ;E308  ...sy...
	defb A8 C6 CA 1E 0C E2 2F 3C  ;E310  ....../<
	defb FA 41 C6 5F 19 C0 DE 03  ;E318  .A._....
	defb 13 33 A7 3F 9D C1 F9 D1  ;E320  .3.?....
	defb 7B E6 0F D6 00 6F 3F 9F  ;E328  {....o?.
	defb A5 CB 3A 30 02 F6 10 E6  ;E330  ..:0....
	defb FA C0 7B 07 4D E9 38 02  ;E338  ..{.M.8.
	defb DD 62 CF AE 19 D3 38 4E  ;E340  .b....8N
	defb 09 C1 07 DC FC D7 DD 6F  ;E348  .......o
	defb 7C 2D FB 30 F4 1F B2 66  ;E350  |-.0...f
	defb EA 96 71 5B 04 96 4B 4A  ;E358  ..q[..KJ
	defb FC 96 F9 AB C0 96 FF 34  ;E360  .......4
	defb FB 96 0F 05 F3 E6 1E DD  ;E368  ........
	defb B5 A6 F5 33 10 D6 5C 2A  ;E370  ...3..\*
	defb 8C 96 8C 11 D8 93 F6 7F  ;E378  ........
	defb 68 93 57 F5 FE ED 93 58  ;E380  h.W....X
	defb 05 36 93 73 50 9B 93 FC  ;E388  .6.sP...
	defb 93 6F 2E 3C 94 20 BF F1  ;E390  .o.<. ..
	defb 94 AF 4C 1A 94 F8 FE 31  ;E398  ..L....1
	defb AF 66 6E D7 11 BF FF FE  ;E3A0  .fn.....
	defb 9B EF 0F 0D ED 79 B1 B7  ;E3A8  .....y..
	defb 8C D5 28 0E 43 0B 42 00  ;E3B0  ..(.C.B.
	defb FB 3E F2 66 C0 61 CF 0A  ;E3B8  .>.f.a..
	defb CF 76 AB 3D D6 7F DD 74  ;E3C0  .v.=...t
	defb DD 7D D7 06 AB 5A 7C 12  ;E3C8  .}...Z|.
	defb 58 05 17 58 DE DD B2 A5  ;E3D0  X..X....
	defb 7C 92 58 27 D9 00 C9 21  ;E3D8  |.X'...!
	defb E9 FA 83 DD 2E 09 C3 E9  ;E3E0  ........
	defb 31 9B 4D E3 B0 F6 12 53  ;E3E8  1.M....S
	defb C3 95 41 56 0A 6C A7 3A  ;E3F0  ..AV.l.:
	defb 24 E3 FA C2 C5 2A F5 C1  ;E3F8  $....*..
	defb B6 73 DB 32 1B 5E 33 47  ;E400  .s.2.^3G
	defb 6C 3C 31 BD 08 75 C6 4F  ;E408  l<1..u.O
	defb 2D 09 6D E4 4E 25 2E 82  ;E410  -.m.N%..
	defb 19 86 1F AB 0D 3F D2 D5  ;E418  .....?..
	defb 7F AD C6 60 26 47 0E F0  ;E420  ...`&G..
	defb C3 E4 C2 F1 B2 CB D3 F8  ;E428  ........
	defb 10 8D BF 60 0D 80 0C D8  ;E430  ...`....
	defb 0B 28 B8 F1 88 0A F0 09  ;E438  .(......
	defb 60 E0 08 58 63 C2 07 7C  ;E440  `..Xc..|
	defb 37 06 B0 06 B3 1B 40 EC  ;E448  7.....@.
	defb 05 94 9E DD 44 F8 04 B0  ;E450  ....D...
	defb D9 8D 70 2C F0 03 BE 3D  ;E458  ..p,...=
	defb EB 84 58 20 EB D9 F6 02  ;E460  ..X ....
	defb CA A2 7C 48 3C 58 E0 16  ;E468  ..|H<X..
	defb 9E DD F8 01 DF D6 B3 C2  ;E470  ........
	defb AC 90 7B CF 7A 65 51 E8  ;E478  ..{.zeQ.
	defb 59 3E 2C 1C 0B 9E DD FC  ;E480  Y>,.....
	defb 00 EF 9E 2D E1 82 C8 D6  ;E488  ...-....
	defb B3 BD B2 A8 9F CF 7A 96  ;E490  ......z.
	defb 8E EB 59 85 7E 77 70 67  ;E498  ..Y.~wpg
	defb 3D 6B 64 F5 AC 5E 59 54  ;E4A0  =kd..^YT
	defb 4F B3 9E 4B 47 7A D6 42  ;E4A8  O..KGz.B
	defb 3F 3B 38 59 CF 35 32 3D  ;E4B0  ?;8Y.52=
	defb EB 2F 2C 2A A8 67 27 25  ;E4B8  ./,*.g'%
	defb 23 D6 B3 28 1F 1D 1C CF  ;E4C0  #..(....
	defb 7A 1A 19 E8 59 17 16 15  ;E4C8  z...Y...
	defb 13 98 D1 12 00 0F 48 52  ;E4D0  ......HR
	defb 9A 06 2D 04 21 58 27 D9  ;E4D8  ..-.!X'.
	defb 00 C9 BF F8 21 9A C6 C3  ;E4E0  ....!...
	defb DF C0 0B C2 F8 0E F8 1B  ;E4E8  ........
	defb 10 60 0D 80 0C D8 0B 28  ;E4F0  .`.....(
	defb 1B DF 88 0A F0 09 60 E0  ;E4F8  ......`.
	defb 26 8C 08 58 07 7C 63 30  ;E500  &..X.|c0
	defb BB 71 B0 06 40 EC 05 94  ;E508  .q..@...
	defb D9 3D 44 F8 04 B0 DD E8  ;E510  .=D.....
	defb 70 2C FD 03 BE B3 9E 84  ;E518  p,......
	defb 58 9E DD 20 F6 02 CA D6  ;E520  X.. ....
	defb B3 A2 7C 58 38 B3 7B 16  ;E528  ..|X8.{.
	defb F8 01 DF 7A D6 C2 AC 90  ;E530  ...z....
	defb 7B 59 CF 65 51 3D EB 3E  ;E538  {Y.eQ=.>
	defb 2C 1C B3 1B 0A FC 00 EF  ;E540  ,.......
	defb 7A D6 E1 D6 C8 BD 59 CF  ;E548  z.....Y.
	defb B2 A8 3D EB 9F 96 8E AC  ;E550  ..=.....
	defb 67 85 7E 77 9E F5 70 6B  ;E558  g.~w..pk
	defb D6 B3 64 5E 59 54 CF 7A  ;E560  ..d^YT.z
	defb 4F 4B EB 59 47 42 3F 3B  ;E568  OK.YGB?;
	defb 67 3D 38 35 F5 AC 32 2F  ;E570  g=85..2/
	defb 2C 2A B3 9E 27 25 7A D6  ;E578  ,*..'%z.
	defb 23 21 1F 1D 59 CF 1C 1A  ;E580  #!..Y...
	defb 3D EB 19 17 16 AC 67 15  ;E588  =.....g.
	defb 13 12 9E F5 11 10 F2 8C  ;E590  ........
	defb 0F 06 ED FD FF 97 87 95  ;E598  ........
	defb D8 F3 7E 32 C9 C0 5D 54  ;E5A0  ..~2..]T
	defb 01 05 00 09 4E 73 23 46  ;E5A8  ....Ns#F
	defb 72 C5 F1 96 21 09 22 E1  ;E5B0  r...!.".
	defb C2 76 71 81 C3 21 C4 F3  ;E5B8  .vq..!..
	defb 4C 1E 73 9E C3 63 79 CE  ;E5C0  L.s..cy.
	defb 03 39 D5 20 51 C9 A7 43  ;E5C8  .9. Q..C
	defb C0 4F A0 CB 40 23 F6 B9  ;E5D0  .O..@#..
	defb 44 97 9D FE 49 3E FF F5  ;E5D8  D...I>..
	defb E6 3D C1 DD 2A CB 3E 0F  ;E5E0  .=..*.>.
	defb CD F3 CC 5C CF 6B BB 10  ;E5E8  ...\.k..
	defb 97 99 D3 E2 3D 60 21 3A  ;E5F0  ....=`!:
	defb 7B C0 22 0B C5 6F 99 CE  ;E5F8  {."..o..
	defb D8 E2 EF 67 9A 53 76 59  ;E600  ...g.SvY
	defb BC 43 C7 74 77 C1 54 EC  ;E608  .C.tw.T.
	defb 7D C5 9B F6 D1 E2 35 AF  ;E610  }.....5.
	defb 32 0E C5 3A 33 7F DB CD  ;E618  2..:3...
	defb 2E F2 C4 AC 33 56 BF 3A  ;E620  ....3V.:
	defb EB 18 7C 99 CE E5 FF FB  ;E628  ..|.....
	defb 67 9A 5F 76 59 C8 4F 9D  ;E630  g._vY.O.
	defb 75 83 45 67 59 8D D6 D9  ;E638  u.EgY...
	defb CA C0 CB 94 75 CC 3A 4B  ;E640  ....u.:K
	defb E0 FB 43 AC B3 CB 6D 71  ;E648  ..C...mq
	defb B6 A1 3C F7 14 93 5B F6  ;E650  ..<...[.
	defb 33 5E C5 7B 92 CE C3 3E  ;E658  3^.{...>
	defb 03 1B C2 E3 B9 14 67 3F  ;E660  ......g?
	defb 78 E1 EB EC 54 C5 9B 88  ;E668  x...T...
	defb 8C D2 C0 11 D3 85 CB 01  ;E670  ........
	defb 0C A2 ED B0 F9 E0 FD FF  ;E678  ........
	defb 3E 0C 79 06 BF AF F8 F7  ;E680  >.y.....
	defb C3 21 C6 DD 6E 00 B2 BF  ;E688  .!..n...
	defb 66 01 B7 ED 42 19 B0 CC  ;E690  f...B...
	defb 75 74 FB B3 23 3D C8 F9  ;E698  ut..#=..
	defb 20 E9 C9 ED 73 1F F2 52  ;E6A0   ...s..R
	defb E1 B0 20 80 D9 6A 8A A4  ;E6A8  .. ..j..
	defb A9 3D 22 78 A8 CA 97 C3  ;E6B0  .="x....
	defb 2C 41 F7 FE DD CF C6 AD  ;E6B8  ,A......
	defb 70 35 F2 4D 99 36 79 30  ;E6C0  p5.M.6y0
	defb A1 A4 7E 3C FF 63 E6 25  ;E6C8  ..~<.c.%
	defb 78 FE 00 20 02 53 38 5A  ;E6D0  x.. .S8Z
	defb C8 55 29 4F 7D 9B F4 80  ;E6D8  .U)O}...
	defb F9 E1 99 9F 11 01 C3 13  ;E6E0  ........
	defb F9 A1 18 6F 02 D1 10 D1  ;E6E8  ...o....
	defb 23 B8 38 B3 8B 09 83 25  ;E6F0  #.8....%
	defb 81 3D BF 33 18 36 92 30  ;E6F8  .=.3.6.0
	defb 42 EE E7 45 60 C6 56 36  ;E700  B..E`.V6
	defb 32 3E DF 45 93 E5 B0 4C  ;E708  2>.E...L
	defb B7 A6 31 90 20 C3 2B FB  ;E710  ..1. .+.
	defb 9F 28 09 2F E6 0F 63 3C  ;E718  .(./..c<
	defb DB C3 66 C2 4A FA DE CA  ;E720  ..f.J...
	defb 92 9B 19 A6 B9 8D B3 AB  ;E728  ........
	defb 79 68 65 8A 72 A6 6B 87  ;E730  yhe.r.k.
	defb D9 5F 8E 19 14 65 8E D9  ;E738  ._...e..
	defb AA CB 6A 51 EE 8C D9 AF  ;E740  ..jQ....
	defb E1 FF 5B 15 A7 2A 2A E5  ;E748  ..[..**.
	defb 68 D9 AF 34 21 A7 81 C5  ;E750  h..4!...
	defb 34 63 D6 60 72 CE 6A 1A  ;E758  4c.`r.j.
	defb 38 B1 BE 81 C5 FC 4A 60  ;E760  8.....J`
	defb 83 AC 67 60 97 06 CF 66  ;E768  ..g`...f
	defb 92 3E DE 5B FF 60 9C 99  ;E770  .>.[.`..
	defb A2 9E 7F 5A 55 89 7E 2E  ;E778  ...ZU.~.
	defb AD B2 51 9F 49 EF 60 AF  ;E780  ..Q.I.`.
	defb 22 FC 65 AB 0A 63 5A 48  ;E788  ".e..cZH
	defb CB 60 53 9A 21 0A 23 58  ;E790  .`S.!.#X
	defb 60 37 29 64 60 CD 96 9C  ;E798  `7)d`...
	defb 90 96 B1 B4 A8 99 C4 60  ;E7A0  .......`
	defb DD C3 CB 3D E3 60 F2 53  ;E7A8  ...=.`.S
	defb 66 A6 7B A4 CF 2F A9 DE  ;E7B0  f.{../..
	defb E8 D6 60 FB 56 A6 9E E2  ;E7B8  ..`.V...
	defb 9F 7E 6C 45 82 52 8B AB  ;E7C0  .~lE.R..
	defb 43 60 FF 67 AF 8A 08 5B  ;E7C8  C`.g...[
	defb 16 D9 A2 E6 98 1C 60 44  ;E7D0  ......`D
	defb C8 3A C9 6A E3 79 0D 06  ;E7D8  .:.j.y..
	defb C5 B4 AD 01 3C 45 04 D9  ;E7E0  ....<E..
	defb 8C 2D 04 62 15 74 64 33  ;E7E8  .-.b.td3
	defb 2C 01 AD 55 55 23 6D A3  ;E7F0  ,..UU#m.
	defb 2C 2B B2 36 37 77 64 D2  ;E7F8  ,+.67wd.
	defb 6F B6 5A 53 A9 55 7C CC  ;E800  o.ZS.U|.
	defb 69 15 43 5F 62 F6 CC D6  ;E808  i.C_b...
	defb 2A 24 89 B6 DB 92 15 D9  ;E810  *$......
	defb 05 5B BD DE C0 72 B3 DD  ;E818  .[...r..
	defb AF 20 AC 52 12 F2 E4 AF  ;E820  . .R....
	defb AC CC 0E FA AF 59 DE 56  ;E828  .....Y.V
	defb 29 9F 72 B5 4B 1E F4 3C  ;E830  ).r.K..<
	defb B5 43 E0 4F 3A E3 4D CB  ;E838  .C.O:.M.
	defb B7 F8 0A DA C0 DD 67 66  ;E840  ......gf
	defb 7A 2E 09 18 54 32 1E ED  ;E848  z...T2..
	defb 92 30 7B 49 01 F7 31 49  ;E850  .0{I..1I
	defb B7 35 9B 86 87 1C 09 C0  ;E858  .5......
	defb 1C C1 4D EC C6 04 86 2D  ;E860  ..M....-
	defb F8 04 4D 63 A1 08 7C C8  ;E868  ..Mc..|.
	defb 19 FE 7D D6 00 30 01 AF  ;E870  ..}..0..
	defb CB 14 EA 92 FC F6 B7 F5  ;E878  ........
	defb D9 3E 08 93 FE CE 00 DD  ;E880  .>......
	defb 6F E1 09 22 D2 68 6E 4A  ;E888  o..".hnJ
	defb 9D 0B 82 9D 7D F6 12 EF  ;E890  ....}...
	defb 76 18 59 33 FD 9C 61 69  ;E898  v.Y3..ai
	defb 03 9C DF 8D 4B 9C DD 7C  ;E8A0  ....K..|
	defb B4 D5 FA 9A B4 6E D5 10  ;E8A8  .....n..
	defb 33 B4 D6 9B 97 D4 97 DC  ;E8B0  3.......
	defb C0 D6 97 24 56 9A 97 4E  ;E8B8  ...$V..N
	defb D2 06 97 28 30 E3 75 B5  ;E8C0  ...(0.u.
	defb 97 B4 AD 5B 20 D4 5C 04  ;E8C8  ...[ .\.
	defb 97 D6 19 B3 FB D9 D2 99  ;E8D0  ........
	defb 9B D8 31 FE 5B 6F F2 BF  ;E8D8  ..1.[o..
	defb FF CD 97 C0 21 80 AF B6  ;E8E0  ....!...
	defb 3E 0D E0 07 7E 65 0A DC  ;E8E8  >...~e..
	defb 18 0E ED 79 43 BA 2F AB  ;E8F0  ...yC./.
	defb 3E 0B 42 84 C5 3D 28 CB  ;E8F8  >.B..=(.
	defb 3C 60 16 48 52 B2 05 7B  ;E900  <`.HR..{
	defb 04 20 00 1E 00 1C 00 FE  ;E908  . ......
	defb D5 18 0A 00 C3 DA C0 F1  ;E910  ........
	defb CE 53 AF 21 67 7F 31 11  ;E918  .S.!g.1.
	defb 68 01 62 00 77 ED EC E2  ;E920  h.b.w...
	defb B0 32 7C C1 78 C0 3B EB  ;E928  .2|.x.;.
	defb 99 BA 3F 67 3C D9 11 B2  ;E930  ..?g<...
	defb C5 42 4B CA B2 1A 13 E1  ;E938  .BK.....
	defb 8B DF 86 C1 CD 70 D5 BA  ;E940  .....p..
	defb 22 91 F8 B5 D6 05 C2 07  ;E948  ".......
	defb 26 82 ED 53 D3 10 79 B9  ;E950  &..S..y.
	defb CA 6B 4D D2 CB B9 0D B9  ;E958  .kM.....
	defb 6A 4F 3D EE D1 C3 35 C1  ;E960  jO=...5.
	defb 9B 75 C3 DF FC 01 21 4C  ;E968  .u....!L
	defb 35 20 27 36 95 99 06 3A  ;E970  5 '6...:
	defb 2B 82 B3 88 B6 A9 98 7E  ;E978  +......~
	defb CB 47 CC 78 C1 DD 1A CD  ;E980  .G.x....
	defb C9 CA EB D6 42 27 74 B3  ;E988  ....B't.
	defb AF 74 AF 72 C3 3E 01 5E  ;E990  .t.r.>.^
	defb 85 02 C7 E8 76 BA E9 AA  ;E998  ....v...
	defb 07 47 AD 6E 89 B0 28 14  ;E9A0  .G.n..(.
	defb E4 1E 33 26 F1 64 FB D7  ;E9A8  ..3&.d..
	defb 03 11 11 BF FF 0E FD 7E  ;E9B0  .......~
	defb 3C 3E 0D 20 02 3D 2B 47  ;E9B8  <>. .=+G
	defb E6 6F D9 09 B6 78 09 2B  ;E9C0  .o...x.+
	defb 42 ED BA 8B 79 43 A3 3C  ;E9C8  B...yC.<
	defb 94 CF AB 3D F2 45 BC 1F  ;E9D0  ...=.E..
	defb DD D5 C9 E5 D5 EE 16 00  ;E9D8  ........
	defb 19 77 60 FC D1 E1 C9 EB  ;E9E0  .w`.....
	defb 5E 23 56 4D E0 09 21 5E  ;E9E8  ^#VM..!^
	defb 7F 3F 11 19 7B 3C FE 01  ;E9F0  .?..{<..
	defb E3 E8 38 02 3E 00 92 7E  ;E9F8  ..8.>..~
	defb 87 86 AF 73 62 6F 01 09  ;EA00  ...sbo..
	defb 64 96 CD 71 B0 7A 84 6A  ;EA08  d..q.z.j
	defb 5D AA 9B BC 69 63 3F 63  ;EA10  ]...ic?c
	defb CB C6 CC 56 67 6B F2 7D  ;EA18  ...Vgk.}
	defb AF 32 72 D9 CF 69 93 B4  ;EA20  .2r..i..
	defb 4E BC A4 DD 35 11 F0 CB  ;EA28  N...5...
	defb 00 B6 44 AE BE 4C 94 36  ;EA30  ..D..L.6
	defb 33 5E 13 BF 5C 56 14 74  ;EA38  3^..\V.t
	defb FE 60 DA E9 7F F1 C2 21  ;EA40  .`.....!
	defb D6 C1 E5 F0 30 48 C6 10  ;EA48  ....0H..
	defb 95 B3 38 33 20 17 27 72  ;EA50  ..83 .'r
	defb 06 40 DD 4E D3 77 10 C9  ;EA58  .@.N.w..
	defb 99 CD EA C2 75 15 FC 2E  ;EA60  ....u...
	defb 74 16 3C A7 1B F3 FF 1C  ;EA68  t.<.....
	defb C9 87 26 00 6F 09 7E 23  ;EA70  ..&.o.~#
	defb 66 C6 EC A4 56 9B AC A6  ;EA78  f...V...
	defb D3 0F C0 00 26 E6 A4 73  ;EA80  ....&..s
	defb 73 4B 38 C2 06 62 46 F9  ;EA88  sK8..bF.
	defb 4F 4E 1B 53 48 E9 0F 17  ;EA90  ON.SH...
	defb 1B 12 1E 26 35 37 A5 3F  ;EA98  ...&57.?
	defb A3 09 A1 07 9F 8F D7 0B  ;EAA0  ........
	defb 6D 32 C9 E5 55 D4 F6 AD  ;EAA8  m2..U...
	defb 61 C7 FE D3 2A F0 7E D1  ;EAB0  a...*.~.
	defb CE DF ED 44 13 D5 FC 1F  ;EAB8  ...D....
	defb 12 5F 17 9F 57 3E FF 18  ;EAC0  ._..W>..
	defb 67 1A B1 56 4E F6 76 31  ;EAC8  g..VN.v1
	defb 4E 06 46 07 5F B8 C7 12  ;EAD0  N.F._...
	defb D5 FE 56 38 03 9D F9 DD  ;EAD8  ..V8....
	defb 7E 02 CD FA C4 D6 BF 64  ;EAE0  ~......d
	defb DD D1 B7 ED 52 F7 FC E5  ;EAE8  ....R...
	defb 30 06 EB AF 67 6F EA 08  ;EAF0  0...go..
	defb C9 9A 12 B9 4F F1 65 11  ;EAF8  ....O.e.
	defb 8F 89 D0 91 B7 CB 13 32  ;EB00  .......2
	defb BC 12 10 F4 C1 10 EF 8B  ;EB08  ........
	defb 0A 08 DF 10 4E 98 4D 9F  ;EB10  ....N.M.
	defb 67 09 39 45 DA A9 74 07  ;EB18  g.9E..t.
	defb D1 9E C5 73 08 72 D1 3E  ;EB20  ...s.r.>
	defb 77 0A D1 C9 7B 73 36 9E  ;EB28  w...{s6.
	defb 12 5E 6D 4D 38 28 83 AB  ;EB30  .^mM8(..
	defb D5 96 18 65 DE 28 35 5F  ;EB38  ...e.(5_
	defb 77 18 9F A9 86 0B EA 53  ;EB40  w......S
	defb 0C 0A 8C 5C 4E 19 C3 F5  ;EB48  ...\N...
	defb A6 A8 06 3A 38 BE 76 20  ;EB50  ...:8.v 
	defb 22 3F A7 0E EC 67 04 05  ;EB58  "?...g..
	defb 73 59 6E D4 66 16 51 4D  ;EB60  sYn.f.QM
	defb A2 17 A2 18 E5 2A D6 9E  ;EB68  .....*..
	defb AB 5C 8E 8C 99 7E 20 C4  ;EB70  .\...~ .
	defb F9 1B 18 63 F9 1C F9 1D  ;EB78  ...c....
	defb F9 1E E9 EE 66 28 05 34  ;EB80  ....f(.4
	defb 97 01 D5 C6 7E 10 C5 11  ;EB88  ....~...
	defb 96 C6 76 1B 72 6C 3C 68  ;EB90  ..v.rl<h
	defb D2 B7 86 73 D8 3D 06 AA  ;EB98  ...s.=..
	defb 3E E9 4E EC 71 17 BF 56  ;EBA0  >.N.q..V
	defb 28 6B CD F8 C3 B5 33 3D  ;EBA8  (k....3=
	defb C4 01 5E 3E 69 FF 06 7E  ;EBB0  ..^>i..~
	defb 0C F9 D0 9B A9 03 52 05  ;EBB8  ......R.
	defb 32 2F C6 A0 E6 55 F5 A4  ;EBC0  2/...U..
	defb 38 E5 80 66 05 19 26 66  ;EBC8  8..f..&f
	defb 70 56 07 3A 73 EB C1 87  ;EBD0  pV.:s...
	defb 5F 7D 7B 1C 7C 29 1F F9  ;EBD8  _}{.|)..
	defb 0F 93 FC 4A B7 28 19 FA  ;EBE0  ...J.(..
	defb CD C3 F6 0A 64 1A D6 DD  ;EBE8  ....d...
	defb 66 46 F1 A5 F2 08 F2 09  ;EBF0  fF......
	defb F7 FA 34 00 BA 30 D3 5B  ;EBF8  ..4..0.[
	defb 58 F6 10 57 C3 A7 F4 C6  ;EC00  X..W....
	defb 08 5F 7A C3 C9 F3 17 66  ;EC08  ._z....f
	defb 67 CB 18 54 5D 16 4E E1  ;EC10  g..T].N.
	defb A4 46 FE E7 26 CB 7F C8  ;EC18  .F..&...
	defb D9 0E 77 12 3C 9D 6F 28  ;EC20  ..w.<.o(
	defb 1A 66 CE 18 14 96 C6 48  ;EC28  .f.....H
	defb BB 72 52 EA 1A A9 64 E8  ;EC30  .rR...d.
	defb CA 63 CE 66 38 73 7C E8  ;EC38  .c.f8s|.
	defb FF 59 4F 7B 33 0E A6 67  ;EC40  .YO{3..g
	defb FF 04 74 78 E6 F5 89 94  ;EC48  ..tx....
	defb 01 D3 94 06 FE AE D0 EE  ;EC50  ........
	defb 5E 1F E4 1B AE 38 15 D6  ;EC58  ^....8..
	defb FE 5F C7 86 0E F2 6E C4  ;EC60  ._....n.
	defb FE F1 CE 00 18 04 51 CD  ;EC68  ......Q.
	defb 10 FF AE FE 21 78 B4 C6  ;EC70  ....!x..
	defb 66 42 5A E7 83 C4 AF A2  ;EC78  fBZ.....
	defb C9 FE 0F B2 46 B1 7D 82  ;EC80  ....F.}.
	defb 10 FD E6 F0 4D 0D DC 0D  ;EC88  ....M...
	defb C1 6E 0B 28 E4 4C E9 46  ;EC90  .n.(.L.F
	defb FB 9F 79 87 CB 2F 4B 6F  ;EC98  ..y../Ko
	defb 21 AC BB 34 86 F8 C9 6E  ;ECA0  !..4...n
	defb 98 66 1E 34 57 43 45 18  ;ECA8  .f.4WCE.
	defb 63 CC 45 0C 45 F1 10 4F  ;ECB0  c.E.E..O
	defb 73 FA 1F 66 20 18 01 73  ;ECB8  s..f ..s
	defb F1 89 72 66 78 EC 70 4F  ;ECC0  ..rfx.pO
	defb 78 79 29 97 1D 1A 79 E6  ;ECC8  xy)...y.
	defb 10 A4 27 DC B1 0C 88 03  ;ECD0  ..'.....
	defb FE A4 12 17 16 00 5F 21  ;ECD8  ......_!
	defb 06 C5 19 C6 B3 6F C9 DC  ;ECE0  .....o..
	defb 0E 07 37 FE 3E 0D 80 0C  ;ECE8  ..7.>...
	defb CC 0B 22 82 0A E3 C6 EC  ;ECF0  ..".....
	defb 09 5C D6 08 58 7B 76 E0  ;ECF8  .\..X{v.
	defb 07 6E 04 9F D9 8D 06 40  ;ED00  .n.....@
	defb E6 05 91 CF EE 41 F6 04  ;ED08  .....A..
	defb AE EC 46 6B 2C F0 03 B7  ;ED10  ..Fk,...
	defb 9E F5 82 4F F5 EC 20 F3  ;ED18  ...O.. .
	defb 02 C8 A1 7B B3 9E 57 36  ;ED20  ...{..W6
	defb 9E DD 16 F8 01 DC D6 B3  ;ED28  ........
	defb C1 A8 90 79 CF 7A 64 50  ;ED30  ...y.zdP
	defb E8 59 3D 2C 1B 0B B3 D2  ;ED38  .Y=,....
	defb FC F1 7A D6 E0 D4 C8 BD  ;ED40  ..z.....
	defb 59 CF B2 A8 3D EB 9F 96  ;ED48  Y...=...
	defb 8D 3D 23 85 C8 77 A5 67  ;ED50  .=#..w.g
	defb 70 6A 64 AC 67 A1 59 54  ;ED58  pjd.g.YT
	defb 9E F5 50 4B D6 B3 47 43  ;ED60  ..PK..GC
	defb 3F 3C CF 7A 38 35 EB 59  ;ED68  ?<.z85.Y
	defb 32 2F 2D 2A 1A 3D 28 26  ;ED70  2/-*.=(&
	defb 24 66 34 22 C0 03 48 52  ;ED78  $f4"..HR
	defb 44 05 6A 04 00 00 00 00  ;ED80  D.j.....
	defb 01 08 43 F1 3E 08 32 EB  ;ED88  ..C.>.2.
	defb C4 B3 B3 C5 25 01 FE FF  ;ED90  ....%...
	defb CD 6E C1 ED 5B 46 C5 21  ;ED98  .n..[F.!
	defb 4E 0F 2C B7 52 44 4D FB  ;EDA0  N.,.RDM.
	defb 81 2A 50 09 7C 32 33 C0  ;EDA8  .*P.|23.
	defb 7D 3A 39 38 11 BF 78 1A  ;EDB0  }:98..x.
	defb 6F 13 67 1B 3E 00 BE CF  ;EDB8  o.g.>...
	defb BA 20 05 BB 28 09 9F 3F  ;EDC0  . ..(..?
	defb 7D 12 13 7C B3 B9 18 E7  ;EDC8  }..|....
	defb F2 4C DD 21 B3 42 B9 CD  ;EDD0  .L.!.B..
	defb C0 EF AA BE 11 3F BF 66  ;EDD8  .....?.f
	defb 07 B4 1E 00 14 7A FE 83  ;EDE0  .....z..
	defb E9 0C 20 F5 C9 E1 42 35  ;EDE8  .. ...B5
	defb 7F E1 29 36 00 23 7E B7  ;EDF0  ..)6.#~.
	defb CC 11 C1 FE AF 99 04 DC  ;EDF8  ........
	defb DD D0 0E 24 73 CF CD 78  ;EE00  ...$s..x
	defb C1 ED 32 83 12 66 AE 7D  ;EE08  ..2..f.}
	defb F7 5C 09 9E FB AF 6F 67  ;EE10  .\....og
	defb 22 A7 DF CD 42 C3 EE F2  ;EE18  "...B...
	defb 01 00 7F 3E 78 17 3F F0  ;EE20  ...>x.?.
	defb B1 2F F6 5F 16 07 01 FD  ;EE28  ./._....
	defb 26 F1 FF ED 51 06 BF 59  ;EE30  &...Q..Y
	defb 9B DA A6 87 BC D8 A9 20  ;EE38  ....... 
	defb C5 9B 03 2A 4C 46 CB 10  ;EE40  ...*LF..
	defb DD 6B F5 00 AE 30 04 EE  ;EE48  .k...0..
	defb 67 E0 23 7E E6 0F 77 1A  ;EE50  g.#~..w.
	defb AF D6 F0 1F 49 FC FE 09  ;EE58  ....I...
	defb 38 03 D6 2F FE DF 18 23  ;EE60  8../...#
	defb 22 BF C0 68 26 00 FC 6F  ;EE68  "..h&..o
	defb 19 4A C5 19 5E 23 56 13  ;EE70  .J..^#V.
	defb DD 73 16 85 5F 72 17 11  ;EE78  .s.._r..
	defb 1D 00 3C 19 19 FB C3 C9  ;EE80  ..<.....
	defb 3A 05 34 BB F4 F6 C4 22  ;EE88  :.4...."
	defb 56 14 13 9A 69 3F CD C5  ;EE90  V...i?..
	defb 30 2A 01 2B 46 C9 E1 B2  ;EE98  0*.+F...
	defb FD C4 1E 19 4F 1A 3B 3E  ;EEA0  ....O.;>
	defb F5 37 D4 A7 03 FF 9F EE  ;EEA8  .7......
	defb 20 D2 53 1C 3D 4F 7D 39  ;EEB0   .S.=O}9
	defb 5A A3 A8 4E 3F A5 A3 70  ;EEB8  Z..N?..p
	defb B8 3E 03 16 00 C4 8D CB  ;EEC0  .>......
	defb A6 6E 28 02 39 FB E6 1E  ;EEC8  .n(.9...
	defb 15 19 72 08 B1 F2 3D 20  ;EED0  ..r...= 
	defb EE ED 43 F5 69 06 79 32  ;EED8  ..C.i.y2
	defb 6B C0 78 C9 DD 7E 15 B7  ;EEE0  k.x..~..
	defb 28 0A 35 F0 3C 54 7E 20  ;EEE8  (.5.<T~ 
	defb 3E 49 C5 5E 12 73 57 E7  ;EEF0  >I.^.sW.
	defb F6 A6 39 BE 1A 13 EB 7F  ;EEF8  ..9.....
	defb 4D 26 3B 65 72 D0 F3 FC  ;EF00  M&;er...
	defb 47 77 28 0C 1B FF FD 72  ;EF08  Gw(....r
	defb 1C E6 1F C3 DC C2 37 E6  ;EF10  ......7.
	defb CB 6F 20 21 24 FD 60 02  ;EF18  .o !$.`.
	defb ED 3F E6 44 DD 86 0C 2C  ;EF20  .?.D...,
	defb 93 52 E8 1B E8 1C E9 66  ;EF28  .R.....f
	defb C7 B6 1A C8 20 DA 13 34  ;EF30  .... ..4
	defb 6B F4 B7 03 15 DC C8 23  ;EF38  k......#
	defb D5 9B E2 FE F0 8B 18 DC  ;EF40  ........
	defb 60 DA 9D C2 D6 FF C1 0F  ;EF48  `.......
	defb 38 11 21 B0 C0 47 FF A7  ;EF50  8.!..G..
	defb 7E B1 F9 DD 00 DE 78 D6  ;EF58  ~.....x.
	defb 0F CA B0 C2 3D A5 32 EB  ;EF60  ....=.2.
	defb 46 D6 4C 76 6F 75 8D 74  ;EF68  F.Lvou.t
	defb D5 B4 79 B6 F2 08 11 B4  ;EF70  ..y.....
	defb 8C C6 69 8F 0D CF 07 33  ;EF78  ..i....3
	defb 0B 5D C3 52 AF FE 06 30  ;EF80  .].R...0
	defb 4F ED E6 66 A9 20 07 79  ;EF88  O..f. .y
	defb 66 73 9D 0B C9 2A 06 79  ;EF90  fs...*.y
	defb 3D 03 7A 18 F1 31 C3 25  ;EF98  =.z..1.%
	defb 85 50 C6 C9 CC A2 27 11  ;EFA0  .P....'.
	defb 06 03 AA 21 66 77 7E 81  ;EFA8  ...!fw~.
	defb EB F6 D6 77 19 10 F8 F8  ;EFB0  ...w....
	defb 07 53 DB 3B E7 8B 02 3E  ;EFB8  .S.;...>
	defb 77 C3 73 C1 8F 37 18 F2  ;EFC0  w.s..7..
	defb D6 06 49 CB 00 79 48 40  ;EFC8  ..I..yH@
	defb 05 33 67 A2 A6 D6 71 0D  ;EFD0  .3g...q.
	defb C6 EC 0E A7 67 CA A4 70  ;EFD8  ....g..p
	defb 10 EB 25 66 AE 07 13 CD  ;EFE0  ..%f....
	defb 2D 85 B7 5E C8 EC C9 35  ;EFE8  -..^...5
	defb C7 DF 1C 47 1F B4 C4 A1  ;EFF0  ...G....
	defb 1C EF 70 C8 1A 19 1F 50  ;EFF8  ..p....P
	defb 18 18 3F C4 66 9C 0D C3  ;F000  ..?.f...
	defb F3 85 8F C8 05 C2 C5 87  ;F008  ........
	defb 4F 53 14 A4 97 C1 90 B7  ;F010  OS......
	defb 30 C9 2A 40 09 1A 38 7C  ;F018  0.*@..8|
	defb DD E5 E1 71 ED D8 70 03  ;F020  ...q..p.
	defb 36 B4 36 20 82 5E 4E 19  ;F028  6.6 .^N.
	defb C3 C1 EF A6 F6 F0 A9 77  ;F030  .......w
	defb C9 D0 23 6B F2 48 D8 21  ;F038  ..#k.H.!
	defb 8E 3C 53 DF D8 31 53 D9  ;F040  .<S..1S.
	defb 84 CE 40 CF 01 6A 16 23  ;F048  ..@..j.#
	defb 7D D3 AD 11 C6 08 ED 79  ;F050  }......y
	defb F8 BB 7E C3 58 C4 DD 6E  ;F058  ..~.X..n
	defb 03 D2 BA 66 04 6C EB C9  ;F060  ...f.l..
	defb 7D 00 0D CB 5A 20 D8 E0  ;F068  }...Z ..
	defb 7F 86 C2 63 C3 CB 42 28  ;F070  ...c..B(
	defb 0B 1E 10 C0 C6 69 DD 96  ;F078  .....i..
	defb E7 EF 38 01 5F CB 7E F9  ;F080  ..8._.~.
	defb 76 23 6B 16 06 5E CB CC  ;F088  v#k..^..
	defb 89 39 DE 0A CE 00 FD FF  ;F090  .9......
	defb F7 DD 2F 79 7B 17 EB 07  ;F098  ../y{...
	defb 88 9B DC E4 EB B9 1F 57  ;F0A0  .......W
	defb 23 10 92 5D D5 B7 D9 66  ;F0A8  #..]...f
	defb D3 33 C9 B8 6E 01 30 9B  ;F0B0  .3..n.0.
	defb 97 02 C3 FE 20 60 8A B6  ;F0B8  .... `..
	defb 08 42 C9 E7 8E 47 87 80  ;F0C0  .B...G..
	defb 5F 46 D2 94 71 05 46 79  ;F0C8  _F..q.Fy
	defb 74 75 48 06 7E F3 1B F3  ;F0D0  tuH.~...
	defb 4E 28 2A D3 DE 66 39 95  ;F0D8  N(*..f9.
	defb 09 86 29 EB 0A FC C3 C8  ;F0E0  ..).....
	defb E8 32 99 06 E8 07 C4 58  ;F0E8  .2.....X
	defb 26 60 C9 4E 5F CE 0A A5  ;F0F0  &`.N_...
	defb 75 EF 08 46 2C EF 09 86  ;F0F8  u..F,...
	defb 14 FE 2D 30 0B 87 5F 21  ;F100  ..-0.._!
	defb 5E C4 19 56 23 C3 4C 20  ;F108  ^..V#.L 
	defb BE B9 8B AD 5E EB D1 CB  ;F110  ....^...
	defb 62 DE 34 A2 D2 19 01 ED  ;F118  b.4.....
	defb 52 D2 9C AC 56 28 08 4E  ;F120  R...V(.N
	defb 8B F9 64 46 0E 09 EB B3  ;F128  ..dF....
	defb 62 39 99 66 10 C7 E9 0D  ;F130  b9.f....
	defb 74 0E 6B E4 27 87 AE 71  ;F138  t.k.'..q
	defb 6E 25 69 3C D7 96 36 3F  ;F140  n%i<..6?
	defb BB 61 AF C9 0D 5D 0C 9C  ;F148  .a...]..
	defb C6 8D 0B E7 3C 0A 9B DD  ;F150  ....<...
	defb F8 02 09 73 08 EB 6B 07  ;F158  ...s..k.
	defb F2 E3 9E 80 14 06 AE 7A  ;F160  .......z
	defb 76 4E 05 F4 9E 4F 67 37  ;F168  vN...Og7
	defb 01 04 B9 3D BB 75 35 03  ;F170  ...=.u5.
	defb F9 C0 BB 67 8A 57 27 02  ;F178  ...g.W'.
	defb FA 67 3D CF A7 F7 AC 81  ;F180  .g=.....
	defb 5D 3B 1B 01 AC 67 FC E0  ;F188  ];...g..
	defb C5 9E F5 AC 94 D6 B3 7D  ;F190  .......}
	defb 68 53 40 33 7A 2E 1D 91  ;F198  hS@3z...
	defb 18 0D FE F0 E2 D6 CA BE  ;F1A0  ........
	defb B4 AA A0 97 8F 87 7F 78  ;F1A8  .......x
	defb 71 6B 65 5F 5A 55 50 4C  ;F1B0  qke_ZUPL
	defb 47 43 40 3C 39 35 80 4A  ;F1B8  GC@<95.J
	defb 8A 2D 2A 28 26 24 22 20  ;F1C0  .-*(&$" 
	defb 1E 1C 1B 19 18 16 15 14  ;F1C8  ........
	defb 13 12 11 10 0F 0E 2E EF  ;F1D0  ........
	defb FF 11 D8 02 FE 9C 65 BE  ;F1D8  ......e.
	defb FE 08 B6 8D B0 AF 00 1E  ;F1E0  ........
	defb 48 52 3D 07 09 04 C7 43  ;F1E8  HR=....C
	defb ED 79 C9 00 E2 FF 21 3D  ;F1F0  .y....!=
	defb C7 18 04 C3 F1 C0 00 EB  ;F1F8  ........
	defb 21 09 C4 7F 19 7E 23 FE  ;F200  !....~#.
	defb 32 3E C9 28 02 1F 1C 12  ;F208  2>.(....
	defb C1 D9 F0 1B 21 C0 01 FD  ;F210  ....!...
	defb 06 11 31 FA B3 ED A0 D9  ;F218  ..1.....
	defb 7E 72 E5 66 6F 19 22 00  ;F220  ~r.fo.".
	defb E9 3F E1 23 D9 10 EA AF  ;F228  .?.#....
	defb 62 3F F6 B3 C2 E7 E2 B4  ;F230  b?......
	defb C3 B5 C4 5E EC C5 B6 C6  ;F238  ...^....
	defb 3D F4 93 FF AB 9D ED AC  ;F240  =.......
	defb B4 B3 AD B7 53 AE 67 FF  ;F248  ....S.g.
	defb C6 11 BF FF 0E FD CD E5  ;F250  ........
	defb C0 DF 59 6C EB AF 08 3E  ;F258  ..Yl...>
	defb 78 66 0D C6 3D F2 70 35  ;F260  xf..=.p5
	defb 9F 3E B3 FE 4F 20 99 3F  ;F268  .>..O .?
	defb 71 B4 FE 30 C9 F9 F2 3E  ;F270  q..0...>
	defb F8 D9 54 07 61 86 0F 8F  ;F278  ..T.a...
	defb DA 4B FA 94 50 D5 D1 28  ;F280  .K..P..(
	defb 3F A3 3C 5A 2D 3D 8B CE  ;F288  ?.<Z-=..
	defb DD 7F 4F AD 99 C8 40 EA  ;F290  ..O...@.
	defb F8 2F E9 9D 2D 42 ED 70  ;F298  ./..-B.p
	defb 56 0F EE C7 79 08 55 20  ;F2A0  V...y.U 
	defb CF 43 37 7E C9 B0 C1 C2  ;F2A8  .C7~....
	defb B1 E6 C6 C3 B2 C4 C5 B3  ;F2B0  ........
	defb C6 CD 36 A2 DF 79 C9 4E  ;F2B8  ..6..y.N
	defb 2B F9 71 67 6D 42 CF EB  ;F2C0  +.qgmB..
	defb CD AA C1 B9 D8 C2 AB C3  ;F2C8  ........
	defb 54 E5 F9 F6 B3 AC C4 C5  ;F2D0  T.......
	defb DA 2C AD D3 06 3D FD 43  ;F2D8  .,...=.C
	defb 66 32 13 C1 28 D3 C9 1D  ;F2E0  f2..(...
	defb AE A7 20 C1 A3 82 59 27  ;F2E8  .. ...Y'
	defb 0E 60 A8 25 51 C9 38 4C  ;F2F0  .`.%Q.8L
	defb F7 04 46 4E 33 D8 22 BD  ;F2F8  ..FN3.".
	defb 09 A7 6E B4 C3 BF 67 55  ;F300  ..n...gU
	defb E5 CD 25 AD E1 FC B0 3E  ;F308  ..%....>
	defb FC BF EE FE E0 38 0C 47  ;F310  .....8.G
	defb FF 28 E7 A3 A8 E9 C9 59  ;F318  .(.....Y
	defb E2 C6 30 28 C8 00 43 65  ;F320  ..0(..Ce
	defb 6C 42 35 D3 21 A4 00 B0  ;F328  lB5.!...
	defb 77 A9 69 A7 59 7E A9 61  ;F330  w.i.Y~.a
	defb F5 94 42 2E 9E 9D BC 89  ;F338  ..B.....
	defb 8C 53 90 58 A7 94 F2 A0  ;F340  .S.X....
	defb A6 F3 00 3E A8 EA 01 32  ;F348  ...>...2
	defb 08 C0 A8 1E FF 3C 20 B4  ;F350  .....< .
	defb CA 46 F6 28 09 95 4C 66  ;F358  .F.(..Lf
	defb 7E 03 40 F3 4F 39 BB 28  ;F360  ~.@.O9.(
	defb D4 7F 6A FE D6 BB 30 89  ;F368  ..j...0.
	defb F2 E4 C1 08 34 28 D5 66  ;F370  ....4(.f
	defb BD D3 B6 79 AF 5F 8B DB  ;F378  ...y._..
	defb B5 08 B7 F5 8D 2A 1F 9D  ;F380  .....*..
	defb 85 53 A4 93 51 3F 4F 3E  ;F388  .S..Q?O>
	defb 76 61 4B F9 2D 1D 49 77  ;F390  vaK.-.Iw
	defb 85 04 C2 A9 32 60 E3 B5  ;F398  ....2`..
	defb 10 C2 CB E6 1F C4 DE 35  ;F3A0  .......5
	defb 52 F1 F0 B2 34 AD 26 DE  ;F3A8  R...4.&.
	defb BD E7 F0 2F 5C B0 00 5E  ;F3B0  .../\..^
	defb FB C2 FB 75 B8 FB 85 37  ;F3B8  ...u...7
	defb FB C2 3F 02 FB 84 E0 FB  ;F3C0  ..?.....
	defb EA 6A 7C E7 FB 71 C2 6D  ;F3C8  .j|..q.m
	defb 81 FB A5 6B 26 FB 7C AA  ;F3D0  ...k&.|.
	defb 0D 2A 4A 83 AC FB A1 E4  ;F3D8  .*J.....
	defb AD 8E 94 EB 95 9A 2A A5  ;F3E0  ......*.
	defb F2 A5 A6 18 5B D3 00 B9  ;F3E8  ....[...
	defb BF F5 00 C2 66 5A 00 D9  ;F3F0  ....fZ..
	defb 00 DB CB F7 00 8E F2 E5  ;F3F8  ........
	defb C2 00 AF 5A 53 01 F8 72  ;F400  ...ZS..r
	defb DC 34 D7 A9 FF A5 EE F9  ;F408  .4......
	defb 8C 16 7B 07 3D 23 6C AE  ;F410  ..{.=#l.
	defb A5 89 05 C3 26 E9 37 55  ;F418  ....&.7U
	defb 64 11 C3 7C F1 FF C2 CB  ;F420  d..|....
	defb 52 FF 27 DE F6 9E F1 30  ;F428  R.'....0
	defb 68 C1 FF 10 47 04 FF BE  ;F430  h...G...
	defb 15 5A FF 5D 1E F8 FF 23  ;F438  .Z.]...#
	defb A5 FF 85 0F FD FF 8F 15  ;F440  ........
	defb E9 50 8D FF 72 C3 A4 2D  ;F448  .P..r..-
	defb FF A6 FF 7D 69 CD AA 11  ;F450  ...}i...
	defb 84 55 45 AC FF A2 9D BC  ;F458  .UE.....
	defb 8F 94 72 96 9A F2 A6 3E  ;F460  ..r....>
	defb B4 A6 FF F0 9F FF 04 66  ;F468  .......f
	defb 83 E9 CD FF E6 C3 FF 55  ;F470  .......U
	defb 6B D4 53 02 5F EE DD 34  ;F478  k.S._..4
	defb 3A 15 FF A6 EF 9F F1 16  ;F480  :.......
	defb 7C A5 27 FB 8E B0 BB 96  ;F488  |.'.....
	defb 89 06 C4 99 A4 64 DF 54  ;F490  .....d.T
	defb 12 C4 9F A1 FF 56 25 02  ;F498  .....V%.
	defb FF DE 3D 97 F2 82 ED 31  ;F4A0  ..=....1
	defb 71 D3 FF B5 C4 FF A5 05  ;F4A8  q.......
	defb BF FF C1 DC D4 A5 1E FF  ;F4B0  ........
	defb FF 88 FF B2 C4 43 FF 6F  ;F4B8  .....C.o
	defb CB FE EB 50 8D FF 73 C4  ;F4C0  ...P..s.
	defb C9 7B F9 7E D2 9A AA 11  ;F4C8  .{.~....
	defb 85 AC 97 F7 F9 90 AE 93  ;F4D0  ........
	defb 97 96 52 9A F2 A7 A6 D3  ;F4D8  ..R.....
	defb 87 FF 0C FE FF 83 A1 C0  ;F4E0  ........
	defb FF 52 7A C4 FF D5 53 5C  ;F4E8  .Rz...S\
	defb DE FE DD CE BF 35 33 5E  ;F4F0  .....53^
	defb FE EF 17 7D 2D E5 00 F3  ;F4F8  ...}-...
	defb 84 A4 9B FE 06 C5 AD 99  ;F500  ........
	defb 65 00 C5 99 FA 00 57 A5  ;F508  e.....W.
	defb F8 00 DE B6 DC FE 31 9B  ;F510  ......1.
	defb 0B 00 C5 17 C4 00 A6 2E  ;F518  ........
	defb 00 A4 F1 00 C5 00 FC 47  ;F520  .......G
	defb 9F D6 00 C5 EE 50 8D 00  ;F528  .....P..
	defb C5 C9 7B FE 7E A5 9C AA  ;F530  ..{.~...
	defb 00 AC 2F EF FE 90 5C 27  ;F538  ../...\'
	defb 97 14 98 9A 00 09 B9 A6  ;F540  ........
	defb 00 B6 FF 00 C5 DD 04 00  ;F548  ........
	defb E7 C5 D5 F2 00 53 B9 BC  ;F550  .....S..
	defb FE DE 9C 7F 34 67 BC FE  ;F558  ....4g..
	defb F0 16 7D E9 C9 27 6C FC  ;F560  ..}..'l.
	defb DE 9C FE 07 C6 93 0D FF  ;F568  ........
	defb 8B BF 13 C6 FF C5 97 E2  ;F570  ........
	defb FF 29 DE D8 72 FE 32 37  ;F578  .)..r.27
	defb 2D FF B6 C6 5A 10 FF C0  ;F580  -...Z...
	defb 4D 5D FF C2 EA C1 FF 8F  ;F588  M]......
	defb 58 FF B3 3C F4 FF 5F 56  ;F590  X..<.._V
	defb F0 15 11 FF 74 BC 07 AD  ;F598  ....t...
	defb 99 FE 7F AA 11 86 79 2F  ;F5A0  ......y/
	defb AC FE 3A 79 91 98 29 E5  ;F5A8  ..:y..).
	defb 9A F2 A8 7D 68 A6 FF E0  ;F5B0  ...}h...
	defb 3F FF 09 CC 83 D2 9B FF  ;F5B8  ?.......
	defb E8 C6 FF D6 BC D5 53 7F  ;F5C0  ......S.
	defb B9 FE DF 34 BC 9C FE F1  ;F5C8  ...4....
	defb 3E 45 16 4B 4F FD 3C 61  ;F5D0  >E.KO.<a
	defb E9 E6 FE 08 C7 55 26 64  ;F5D8  .....U&d
	defb E8 37 14 C7 FF C5 67 56  ;F5E0  .7....gV
	defb FF 2A E5 2E DE FE 33 F0  ;F5E8  .*....3.
	defb 80 48 52 30 01 F7 00 C4  ;F5F0  .HR0....
	defb 22 23 C1 18 BF D6 D9 C3  ;F5F8  "#......
	defb 0E C0 30 37 76 40 E5 00  ;F600  ..07v@..
	defb C2 C0 C4 FF 21 04 AF 01  ;F608  ....!...
	defb 6D 1F ED B1 FC E8 22 C0  ;F610  m.....".
	defb CD 4F B2 54 FF 00 FE E2  ;F618  .O.T....
	defb E6 23 C1 3E 01 32 E1 2E  ;F620  .#.>.2..
	defb 04 C9 02 D3 FE 64 66 FD  ;F628  .....df.
	defb E0 00 B4 19 2A CD AF C9  ;F630  ....*...
	defb EE FC DE 42 ED 70 FA 44  ;F638  ...B.p.D
	defb F0 83 FF 79 B7 C9 11 BF  ;F640  ...y....
	defb 33 73 0E FD FC D4 5B BC  ;F648  3s....[.
	defb 6F 3E F8 08 07 CE 30 C1  ;F650  o>....0.
	defb 95 B6 DA EF E7 52 18 52  ;F658  .....R.R
	defb AF 4C D5 0D 77 E7 3D F2  ;F660  .L..w.=.
	defb 73 B3 FE 15 D9 4F 20 02  ;F668  s....O .
	defb 3F 1E 47 FE 30 F2 4D 66  ;F670  ?.G.0.Mf
	defb F7 0F 8F 4A 53 E0 8F A8  ;F678  ...JS...
	defb 6A D9 28 9F 51 3C AA 96  ;F680  j.(.Q<..
	defb 3D 4C E2 F7 7F FA 4F 6B  ;F688  =L....Ok
	defb D6 D0 40 EE F8 2F 5A 67  ;F690  ..@../Zg
	defb 2D 35 C0 82 C2 84 08 81  ;F698  -5......
	defb 58 CA F3 55 43 C9 42 2A  ;F6A0  X..UC.B*
	defb 1F 85 5A D5 F9 4E 37 4A  ;F6A8  ..Z..N7J
	defb 3D 20 34 3C 1E BF F1 E7  ;F6B0  = 4<....
	defb 68 7E 23 FE FF 28 19 57  ;F6B8  h~#..(.W
	defb 07 E5 2C FD 20 FB 89 9C  ;F6C0  ..,. ...
	defb 21 FA 2D 9D 26 08 CF 74  ;F6C8  !.-.&..t
	defb 9D 18 E1 16 C9 58 4D C6  ;F6D0  .....XM.
	defb 03 98 6A 13 45 18 E7 89  ;F6D8  ..j.E...
	defb C3 AF 32 0D 01 BB 00 E0  ;F6E0  ..2.....
	defb 48 52 51 06 87 05 A0 B0  ;F6E8  HRQ.....
	defb A8 E0 B0 E8 1E FE 21 6E  ;F6F0  ......!n
	defb C8 18 3A C3 B9 C4 29 00  ;F6F8  ..:...).
	defb F8 7F 3D 56 54 49 73 F8  ;F700  ..=VTIs.
	defb 20 50 33 E0 F3 6C 61 79  ;F708   P3..lay
	defb 65 72 FE 8D 2E 37 3D 21  ;F710  er...7=!
	defb 0A C0 CB FE F6 37 46 C8  ;F718  .....7F.
	defb E1 21 A8 C6 34 4F 70 6C  ;F720  .!..4Opl
	defb AF 67 6F 8B 83 32 B6 22  ;F728  .go..2."
	defb B7 C5 C2 C3 AB C5 A8 C1  ;F730  ........
	defb C4 80 3E E1 00 E5 11 64  ;F738  ..>....d
	defb 00 19 7E 32 45 C5 E5 DD  ;F740  ..~2E...
	defb E1 22 0B C0 C0 60 5E 02  ;F748  ."...`^.
	defb 23 5C FC E7 C4 D1 DD 6E  ;F750  #\.....n
	defb 03 66 04 F3 ED F4 C4 21  ;F758  .f.....!
	defb A9 99 19 DE F3 37 76 3E  ;F760  .....7v>
	defb 69 A1 C1 44 AC AC BE 11  ;F768  i..D....
	defb 1C C6 01 1F C7 1A 13 FE  ;F770  ........
	defb 1E 30 06 67 1F 18 6F 18  ;F778  .0.g..o.
	defb 07 D5 16 F9 C7 00 5F 19  ;F780  ......_.
	defb D1 7C 02 0B 7D FE FB D6  ;F788  .|..}...
	defb F0 20 E3 21 51 C6 77 E6  ;F790  . .!Q.w.
	defb 9F F5 52 01 6C 00 ED B0  ;F798  ..R.l...
	defb 3C 32 33 F3 80 21 01 F0  ;F7A0  <23..!..
	defb 22 7E E9 AC 89 A6 D9 82  ;F7A8  "~......
	defb F8 18 D1 C4 75 76 5E 7B  ;F7B0  ....uv^{
	defb 67 9D 98 D6 59 60 7D FE  ;F7B8  g...Y`}.
	defb 77 9A DD 7E A9 D6 30 38  ;F7C0  w..~..08
	defb 04 FE 0A FC 27 02 3E 06  ;F7C8  ....'.>.
	defb 32 8D C2 F5 FE 04 FF ED  ;F7D0  2.......
	defb FF 17 E6 07 21 C8 C5 FF  ;F7D8  ....!...
	defb CD D5 50 87 9F 5E 23 CB  ;F7E0  ..P..^#.
	defb 3B 9F E6 A7 32 FF C6 20  ;F7E8  ;...2.. 
	defb C1 EB 09 1A C6 D8 4F CE  ;F7F0  ......O.
	defb C5 F8 8B 91 47 11 AE C7  ;F7F8  ....G...
	defb D5 06 0C 07 3E 4E 23 E5  ;F800  ....>N#.
	defb 46 EB 11 63 FC 17 00 DD  ;F808  F..c....
	defb 26 08 CB 38 FC 7C 72 79  ;F810  &..8.|ry
	defb 8A 77 23 78 F1 03 DD 25  ;F818  .w#x...%
	defb 20 EF D1 13 1F FE E1 23  ;F820   ......#
	defb C1 10 DA D1 7B 66 FE FE  ;F828  ....{f..
	defb E4 20 05 3E FD 32 DC 40  ;F830  . .>.2.@
	defb 3F 20 A7 28 11 1F F5 87  ;F838  ? .(....
	defb 4F 09 F1 30 02 35 34 A7  ;F840  O..0.54.
	defb 8C 99 ED 42 37 CD C7 EB  ;F848  ...B7...
	defb F1 FE 05 21 11 00 54 5C  ;F850  ...!..T\
	defb 3E 17 30 03 2D 5D AF 32  ;F858  >.0.-].2
	defb 74 C1 DD 21 BE C6 0E 10  ;F860  t..!....
	defb E5 19 EB ED 62 7D 7C CE  ;F868  ....b}|.
	defb A1 7F 3F 77 23 19 0C 79  ;F870  ..?w#..y
	defb E6 D0 34 0F A9 E1 B2 77  ;F878  ..4....w
	defb 3A BC 01 1C 79 A7 DF F3  ;F880  :...y...
	defb 3F AD DD 36 08 00 CD 2F  ;F888  ?..6.../
	defb C3 0A 03 0F 7F 16 54 F0  ;F890  ......T.
	defb 21 DB A6 4F 56 7E 39 DD  ;F898  !..OV~9.
	defb 75 B3 74 04 18 41 07 9B  ;F8A0  u.t..A..
	defb AF C1 10 73 9F 18 3B 08  ;F8A8  ...s..;.
	defb 07 F6 F4 33 3D F6 98 20  ;F8B0  ...3=.. 
	defb 07 D2 CC 94 05 3C EA 13  ;F8B8  .....<..
	defb 8C D9 24 C3 F8 1C 93 E8  ;F8C0  ..$.....
	defb C4 E6 C0 E5 64 B8 7E 2F  ;F8C8  ....d.~/
	defb CB F7 71 C2 73 FA 73 FB  ;F8D0  ..q.s.s.
	defb 22 AB F3 93 C2 11 10 20  ;F8D8  "...... 
	defb 83 E0 F0 38 96 82 28 49  ;F8E0  ...8..(I
	defb 9B 73 33 25 AD 1F EE B3  ;F8E8  .s3%....
	defb B9 C6 CC 65 60 20 FD C1  ;F8F0  ...e` ..
	defb 0B CD 0F 6B 37 C0 84 21  ;F8F8  ...k7..!
	defb 68 A2 86 B8 F9 D5 18 D0  ;F900  h.......
	defb 32 AC C6 E4 CE CB 09 86  ;F908  2.......
	defb AA 49 AC 06 34 FF C6 AF  ;F910  .I..4...
	defb ED 73 46 B0 F9 5F 97 F7  ;F918  .sF.._..
	defb E5 E6 9F 31 9A 9C 9D 05  ;F920  ...1....
	defb 0F C9 31 87 96 A4 7A FE  ;F928  ..1...z.
	defb 7E 04 93 D0 F9 AC 85 D6  ;F930  ~.......
	defb DF 07 87 6F 26 8E E6 DF  ;F938  ...o&...
	defb 23 66 D0 3E 51 97 BD 34  ;F940  #f.>Q..4
	defb 4F A4 E1 ED 52 2B 0D 1B  ;F948  O...R+..
	defb D2 2B 0E CF 65 56 3E 39  ;F950  .+..eV>9
	defb FB FE 06 38 09 11 B2 7F  ;F958  ...8....
	defb 89 2C 73 72 E9 A7 B8 08  ;F960  .,sr....
	defb FC 65 A2 01 CD 52 F2 AE  ;F968  .e...R..
	defb C2 2F 08 6A 1A 44 81 0C  ;F970  ./.j.D..
	defb BB E4 0B DD FE 00 51 5B  ;F978  ......Q[
	defb 95 D6 09 96 0B C9 07 3A  ;F980  .......:
	defb 19 FE 97 1E 9F 3C 8D 63  ;F988  .....<.c
	defb 3B CB 18 D7 B5 ED F5 C9  ;F990  ;.......
	defb A9 75 F4 FF 45 FA EF 26  ;F998  .u..E..&
	defb 61 ED AF 4C 3F DC A1 C3  ;F9A0  a..L?...
	defb 63 FA FB 32 9F 8B A1 C5  ;F9A8  c..2....
	defb AB C6 F7 13 6F 67 38 DB  ;F9B0  ....og8.
	defb 22 A4 C5 CC 86 3C A7 73  ;F9B8  "....<.s
	defb 08 FD BB A8 A6 92 F9 22  ;F9C0  ......."
	defb BC 14 BC 32 93 DA 14 86  ;F9C8  ...2....
	defb 96 A9 C9 6D D2 BF 65 F4  ;F9D0  ...m..e.
	defb 6A 01 EC 6F 3F 02 C9 2E  ;F9D8  j..o?...
	defb C3 BA C2 4F 9E F5 D7 DD  ;F9E0  ...O....
	defb 0F 3D E3 34 EB FB 0D 0F  ;F9E8  .=.4....
	defb DB 12 AF FC B8 E5 4F 66  ;F9F0  ......Of
	defb CB 15 46 E5 CA 96 C4 6F  ;F9F8  ..F....o
	defb CA C3 E1 C3 73 08 66 0E  ;FA00  ....s.f.
	defb F9 FD 97 D1 67 DF 00 6F  ;FA08  ....g..o
	defb 39 3C BA 38 01 7B F3 BB  ;FA10  9<.8.{..
	defb ED 7E 12 86 F2 96 4C 4A  ;FA18  .~....LJ
	defb D3 FE 75 43 5F 87 34 C6  ;FA20  ..uC_.4.
	defb 14 F5 0F F5 10 D1 37 A7  ;FA28  ......7.
	defb D1 01 47 7A 2F 87 C8 39  ;FA30  ..Gz/..9
	defb F9 78 94 89 D4 C1 E1 CA  ;FA38  .x......
	defb 96 DF DF 56 09 19 CB 70  ;FA40  ...V...p
	defb 28 9C 75 F0 7A 74 09 EB  ;FA48  (.u.zt..
	defb 29 D1 A7 31 8B 4A A6 DA  ;FA50  )..1.J..
	defb E1 68 F8 06 36 51 F8 07  ;FA58  .h..6Q..
	defb 65 E3 3E B6 05 79 98 F7  ;FA60  e.>..y..
	defb 3E 35 20 39 4D AB B9 16  ;FA68  >5 9M...
	defb D7 A9 93 36 17 A9 18 7C  ;FA70  ...6...|
	defb 44 69 48 DC 74 B4 69 56  ;FA78  DiH.t.iV
	defb 0A 8C FB 1F EE 19 93 D6  ;FA80  ........
	defb 33 91 FA 4A 27 F1 C4 F2  ;FA88  3..J'...
	defb 13 77 12 04 1E 1A 29 3E  ;FA90  .w....)>
	defb 67 1F 02 CB 79 28 13 9E  ;FA98  g...y(..
	defb 38 71 07 FE 0F 0B 3C 18  ;FAA0  8q....<.
	defb 05 E7 CC F1 04 3D 27 37  ;FAA8  .....='7
	defb 02 6F 78 3C 85 F2 93 9A  ;FAB0  .ox<....
	defb 4B C4 4B 10 4B 2C 69 50  ;FAB8  K.K.K,iP
	defb B6 1C 7B 06 C6 BA BC 10  ;FAC0  ..{.....
	defb CB 41 20 8D B6 14 63 73  ;FAC8  .A ...cs
	defb 06 CB 78 C4 19 17 6D 83  ;FAD0  ..x...m.
	defb 2F 94 EE DD 86 04 E5 68  ;FAD8  /......h
	defb E1 03 73 21 85 C5 86 08  ;FAE0  ..s!....
	defb 66 18 0E 84 3A B3 87 AD  ;FAE8  f...:...
	defb DB BF 58 78 1F E6 48 39  ;FAF0  ..Xx..H9
	defb FD 21 B5 C6 B6 0F 77 E1  ;FAF8  .!....w.
	defb 47 0A C8 CC 99 48 C0 AE  ;FB00  G....H..
	defb 15 14 E6 C1 DC D8 7E 0B  ;FB08  ......~.
	defb 38 DD 29 D1 0C 0B C9 AF  ;FB10  8.).....
	defb 35 85 C3 D9 3D 5B 06 55  ;FB18  5...=[.U
	defb FE 67 EB 67 35 20 7F 65  ;FB20  .g.g5 .e
	defb 7F D6 4C 01 EB AE EF 3A  ;FB28  ..L....:
	defb 57 F8 BF 4C 2A 0B C0 23  ;FB30  W..L*..#
	defb 7E 3C 20 08 CD 22 3D 08  ;FB38  ~< .."=.
	defb 57 E5 96 67 3D 79 DA 40  ;FB40  W..g=y.@
	defb CB 12 4A ED 5B A8 C1 6B  ;FB48  ..J.[..k
	defb 74 79 0F C5 D7 44 4D B7  ;FB50  ty...DM.
	defb 7A 4C 27 C5 BD 0D 3B 71  ;FB58  zL'...;q
	defb 36 37 E2 5D CD E5 58 F6  ;FB60  67.]..X.
	defb 43 D1 C4 89 66 C4 AE 64  ;FB68  C...f..d
	defb E9 52 7A AA 64 86 D9 32  ;FB70  .Rz.d..2
	defb DD A6 97 98 FC 2D 62 CA  ;FB78  .....-b.
	defb 48 B3 9D 65 51 CF 1B 8E  ;FB80  H..eQ...
	defb 0A CD 68 C3 22 96 A9 3A  ;FB88  ..h."..:
	defb 0D D9 8E DD 6E 74 DB B0  ;FB90  ....nt..
	defb E9 D3 6A B3 B7 ED 6C 8B  ;FB98  ..j...l.
	defb 69 BA B2 CB F4 31 E9 AC  ;FBA0  i....1..
	defb 7C 85 FC B4 C0 49 76 15  ;FBA8  |....Iv.
	defb 9F 57 BC 34 24 69 9C 78  ;FBB0  .W.4$i.x
	defb B9 E6 89 8C F5 AB B6 28  ;FBB8  .......(
	defb 0E 96 9C 96 0A 77 BF AB  ;FBC0  .....w..
	defb 50 84 AF 11 BF FF 6E 8A  ;FBC8  P.....n.
	defb 01 FD FB AE ED 79 43 F3  ;FBD0  .....yC.
	defb 2F A3 42 3C FE 0D 20 F5  ;FBD8  /.B<.. .
	defb 9E D7 7E A7 F8 7F 41 C9  ;FBE0  ..~...A.
	defb 64 2A 65 00 01 BA 3F 0C  ;FBE8  d*e...?.
	defb 94 35 30 62 46 ED 60 62  ;FBF0  .50bF.`b
	defb 24 21 01 05 09 0B 0D 0F  ;FBF8  $!......
	defb 13 15 19 25 3D 00 5D EC  ;FC00  ...%=.].
	defb D8 31 37 4D 53 5F 71 82  ;FC08  .17MS_q.
	defb 8C 9C 9E A0 A6 A8 AA AC  ;FC10  ........
	defb AE EB A1 00 57 1F 23 25  ;FC18  ....W.#%
	defb 29 2D 2F 33 BF 00 1D 21  ;FC20  )-/3...!
	defb 23 27 2B 2D 31 55 BD 9F  ;FC28  #'+-1U..
	defb 9D 1B 21 2B 3B 4D 5F 23  ;FC30  ..!+;M_#
	defb 55 BB 01 FF 31 90 0D D8  ;FC38  U...1...
	defb 69 70 76 7D 85 8D 95 9D  ;FC40  ipv}....
	defb A8 B1 BB 0C DA 62 68 6D  ;FC48  .....bhm
	defb 75 7B 83 8A 92 9C A4 AF  ;FC50  u{......
	defb B8 0E 08 6A 72 78 7E 86  ;FC58  ...jrx~.
	defb 90 96 A0 AA B4 BE 0F C0  ;FC60  ........
	defb 78 88 0F D8 80 90 98 48  ;FC68  x......H
	defb 52 E6 05 8F 03 31 00 00  ;FC70  R....1..
	defb C3 2B C0 7C F1 21 E6 C5  ;FC78  .+.|.!..
	defb C3 73 97 32 2A C0 17 5F  ;FC80  .s.2*.._
	defb 7C C4 21 86 35 10 FE C2  ;FC88  |.!.5...
	defb 20 C0 36 00 47 CD D3 B6  ;FC90   .6.G...
	defb B8 7E C1 24 FD 48 CD 1C  ;FC98  .~.$.H..
	defb C5 45 C3 2F 5E 3E 00 A7  ;FCA0  .E./^>..
	defb 0C 0E F8 4F FD F7 81 11  ;FCA8  ...O....
	defb BF FF CA 42 C0 3C 23 5D  ;FCB0  ...B.<#]
	defb 9C ED 79 43 AB 3D AB 85  ;FCB8  ..yC.=..
	defb 67 3F C9 D6 60 21 00 9F  ;FCC0  g?..`!..
	defb FF 87 4F 09 7E 32 DA C2  ;FCC8  ..O.~2..
	defb 23 6C 75 ED 03 6F 66 6F  ;FCD0  #lu..ofo
	defb 22 EF C3 B0 FD E2 C0 D6  ;FCD8  ".......
	defb 71 FA 20 C1 16 14 CD CB  ;FCE0  q. .....
	defb 2E C3 3A 47 11 05 34 CD  ;FCE8  ..:G..4.
	defb 21 83 3A F0 3C 11 F8 E5  ;FCF0  !.:.<...
	defb 1A 3C CC 94 C2 67 7F 13  ;FCF8  .<...g..
	defb FE 60 38 1D E7 2C 70 B1  ;FD00  .`8..,p.
	defb 80 CA 28 CE 0A 24 81 31  ;FD08  ..(..$.1
	defb 7F 25 8F 38 33 D6 A1 32  ;FD10  .%.83..2
	defb D9 0B 63 92 D4 CE EA 34  ;FD18  ..c....4
	defb 23 7B C4 CD C2 3E 20 87  ;FD20  #{...> .
	defb 59 F6 ED 53 DB C0 C9 FF  ;FD28  Y..S....
	defb 64 ED 54 BF E0 55 A6 02  ;FD30  d.T..U..
	defb F1 EF 80 82 C4 1A 93 12  ;FD38  ........
	defb 13 F6 10 0F B9 CB B6 20  ;FD40  ....... 
	defb F5 2B 56 D3 55 5F C3 55  ;FD48  .+V.U_.U
	defb 72 CE DA 6A 74 37 3D 88  ;FD50  r..jt7=.
	defb C1 55 C6 D3 F8 55 B6 AF  ;FD58  .U...U..
	defb 74 6C 8A 5F BE E9 84 55  ;FD60  tl._...U
	defb DF 34 5A B6 5A CF 49 EB  ;FD68  .4Z.Z.I.
	defb 5A 84 F5 32 93 8C 5A D3  ;FD70  Z..2..Z.
	defb EF BC 6A 4A C9 52 77 5A  ;FD78  ..jJ.RwZ
	defb 88 AF 76 64 86 C1 5A 35  ;FD80  ..vd..Z5
	defb 6B 2F 7E 79 AB 3B F5 5A  ;FD88  k/~y.;.Z
	defb A7 9E 5A 62 0B 5B BF 5A  ;FD90  ..Zb.[.Z
	defb E7 D6 5B 5A FA 5A FC E9  ;FD98  ..[Z.Z..
	defb 71 2E C2 FE 86 5A 6C 98  ;FDA0  q....Zl.
	defb 69 5A 3E E4 97 12 87 4B  ;FDA8  iZ>....K
	defb E7 E9 85 4D 04 5A 2A 59  ;FDB0  ...M.Z*Y
	defb 2D F5 32 D3 5A A3 44 C9  ;FDB8  -.2.Z.D.
	defb DA B5 D3 5A 89 64 2C C2  ;FDC0  ...Z.d,.
	defb 59 7B 5A F1 AB 79 A9 7F  ;FDC8  Y{Z..y..
	defb AB 5A F5 DC 54 38 5A D7  ;FDD0  .Z..T8Z.
	defb 5E ED 73 BD 78 7E 58 DE  ;FDD8  ^.s.x~X.
	defb 3D F2 A5 0D F3 23 22 99  ;FDE0  =....#".
	defb C2 6A 52 15 81 8F F9 09  ;FDE8  .jR.....
	defb F9 D1 E1 22 0D 4F C3 AF  ;FDF0  ...".O..
	defb 31 17 23 AC 58 7E 00 CD  ;FDF8  1.#.X~..
	defb 3C C2 CC 89 7D 41 DF 96  ;FE00  <...}A..
	defb 00 4F 3C E6 1F 37 9A 25  ;FE08  .O<..7.%
	defb 3B EE 33 00 C1 E5 CA 14  ;FE10  ;.3.....
	defb 77 92 D9 3B 4C 26 3D C0  ;FE18  w..;L&=.
	defb 79 D5 DF B9 85 6F 8C 95  ;FE20  y....o..
	defb 67 7E 0F D0 F8 E6 57 CD  ;FE28  g~....W.
	defb 0F F6 00 FC 25 DB F7 A7  ;FE30  ....%...
	defb 06 08 FA 16 C3 CD 33 80  ;FE38  ......3.
	defb 88 7B C4 CB 77 CA 1C C3  ;FE40  .{..w...
	defb 04 23 5E E6 20 C2 29 6E  ;FE48  .#^. .)n
	defb 9F CB ED 52 EB 78 C7 1E  ;FE50  ...R.x..
	defb E0 D3 0C 09 A6 86 47 66  ;FE58  ......Gf
	defb 73 21 8A EF 09 DA 19 22  ;FE60  s!....."
	defb 26 33 75 A0 88 42 99 7B  ;FE68  &3u..B.{
	defb 51 95 25 7E 7B 61 35 58  ;FE70  Q.%~{a5X
	defb 7B 73 CB 25 A1 7B 6A 7B  ;FE78  {s.%.{j{
	defb 7B 47 81 25 CD B4 7B DB  ;FE80  {G.%..{.
	defb F1 7B 7B 10 FA 9B B9 C3  ;FE88  .{{.....
	defb 7B A2 C3 C8 41 BE 7A AF  ;FE90  {...A.z.
	defb 7A C0 36 7A B6 77 49 AB  ;FE98  z.6z.wI.
	defb 78 77 78 35 53 97 78 D9  ;FEA0  xwx5S.x.
	defb 78 7F 94 F8 78 76 23 D7  ;FEA8  x...xv#.
	defb 78 FB 78 51 B5 78 C9 DE  ;FEB0  x.xQ.x..
	defb 6D 60 5C 33 78 DB 78 20  ;FEB8  m`\3x.x 
	defb FA 1F 20 EE 33 78 2A D0  ;FEC0  .. .3x*.
	defb D1 E6 78 37 FC E6 81 78  ;FEC8  ..x7...x
	defb 79 A5 63 9D 3B 83 6F FC  ;FED0  y.c.;.o.
	defb F8 0E 10 60 0D 80 7C E3  ;FED8  ...`..|.
	defb 0C D8 0B 28 88 0A F0 09  ;FEE0  ...(....
	defb 60 30 6E E0 08 58 C1 98  ;FEE8  `0n..X..
	defb 07 7C C6 8D B0 06 40 F7  ;FEF0  .|....@.
	defb EC EC 05 94 44 F8 04 A3  ;FEF8  ....D...
	defb 67 B0 70 2C 7A 76 F0 03  ;FF00  g.p,zv..
	defb BE 84 58 76 CF 20 F6 02  ;FF08  ..Xv. ..
	defb CA CF 7A A2 7C EE 59 58  ;FF10  ..z.|.YX
	defb 38 16 F8 01 DF 59 CF C2  ;FF18  8....Y..
	defb AC 3D EB 90 7B 65 AC 67  ;FF20  .=..{e.g
	defb 51 3E 2C 6E F4 1C 0B FC  ;FF28  Q>,n....
	defb 00 EF 59 CF E1 D6 3D EB  ;FF30  ..Y...=.
	defb C8 BD B2 AC 67 A8 9F 96  ;FF38  ....g...
	defb 8C F4 8E 85 A3 9E F5 77  ;FF40  .......w
	defb 70 D6 B3 6B 64 5E 59 CF  ;FF48  p..kd^Y.
	defb 7A 54 4F EB 59 4B 47 42  ;FF50  zTO.YKGB
	defb 3F 67 3D 3B 38 F5 AC 35  ;FF58  ?g=;8..5
	defb 32 2F 2C B3 9E 2A 27 59  ;FF60  2/,..*'Y
	defb D4 25 23 CD 1F 3D EB 1D  ;FF68  .%#..=..
	defb 1C 1A AC 67 19 17 16 9E  ;FF70  ...g....
	defb F5 15 13 D3 B3 12 11 10  ;FF78  ........
	defb 0F E2 EB E6 C5 1A 6F 13  ;FF80  ......o.
	defb A4 FF 67 1B FE 50 D8 09  ;FF88  ..g..P..
	defb 7D 12 FA 7C 53 5C 18 EE  ;FF90  }..|S\..
	defb E4 BD F2 CB 60 55 66 EC  ;FF98  ....`Uf.
	defb F3 CC 37 86 8A DA 51 10  ;FFA0  ..7...Q.
	defb F7 E5 1F 2B ED 5B EA C5  ;FFA8  ...+.[..
	defb B7 19 F0 AE 44 4D CD 4A  ;FFB0  ....DM.J
	defb 48 DE 5F 2A 01 C0 01 E1  ;FFB8  H._*....
	defb 7D 31 F9 7E 32 15 C0 CE  ;FFC0  }1.~2...
	defb BA 08 96 91 A1 14 63 D9  ;FFC8  ......c.
	defb C0 6B C1 11 7B DA A0 68  ;FFD0  .k..{..h
	defb 4F 4B F1 CC A6 DB AF C2  ;FFD8  OK......
	defb FE 6B F3 83 8C 8B 26 C9  ;FFE0  .k....&.
	defb 2C A6 75 11 76 01 61 D9  ;FFE8  ,.u.v.a.
	defb 75 70 ED DF 0D BD ED 23  ;FFF0  up.....#
	defb 71 0F D8 22 DB C0 06 16  ;FFF8  q.."....

