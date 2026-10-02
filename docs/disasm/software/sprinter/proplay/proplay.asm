; ProPlay v0.5.91 (07.02.23) - "General Sound MOD player" for the Peters Plus Sprinter, by
; Miroshnichenko Aleksandr aka Sayman (Sprinter Team).
; File: BIN/PROPLAY.EXE on the MAME pack system disk (sp_hdd_sys, DSS 1.71), 1 126 bytes, CRC32 2d06da86.
; DSS EXE header (22 bytes): "EXE", header size #16, load and start #8100, SP #BFFF. This listing is
; the 1 104 bytes after the header (CRC32 6f232150), z80dasm 1.2.0 -a -l -g 0x8100 -t.
; Comments starting with "; " above a label were added by hand (2026-10-02); see README.md.

; z80dasm 1.2.0
; command line: z80dasm -a -l -g 0x8100 -t PROPLAY.bin

	org 08100h

	di			;8100	f3		.
	ld (l854ah),ix		;8101	dd 22 4a 85	. " J .
	jp l83c0h		;8105	c3 c0 83	. . .
; IsaOpen: save the window-3 page (port #E2), #1FFD <- #11 (Scorpion extended page on),
; window 3 <- #D4 | (slot << 1) (ISA I/O, slot 0 = #D4, slot 1 = #D6), #9FBD <- 0 (A19-A14 = 0,
; AEN = 0, RESET = 0). The slot number is the operand of the first LD A,n (self-modified at #8109).
; After this, CPU address #C0BB is ISA I/O port #00BB and #C0B3 is ISA I/O port #00B3.
sub_8108h:
	ld a,000h		;8108	3e 00		> .
	ex af,af'		;810a	08		.
	in a,(0e2h)		;810b	db e2		. .
	ld (0812ch),a		;810d	32 2c 81	2 , .
	ld bc,01ffdh		;8110	01 fd 1f	. . .
	ld a,011h		;8113	3e 11		> .
	out (c),a		;8115	ed 79		. y
	ex af,af'		;8117	08		.
	rlca			;8118	07		.
	or 0d4h			;8119	f6 d4		. .
	out (0e2h),a		;811b	d3 e2		. .
	ld bc,09fbdh		;811d	01 bd 9f	. . .
	xor a			;8120	af		.
	out (c),a		;8121	ed 79		. y
	ret			;8123	c9		.
; IsaClose: #1FFD <- #01, window 3 <- the page IsaOpen saved (operand #812C, self-modified)
sub_8124h:
	ld bc,01ffdh		;8124	01 fd 1f	. . .
	ld a,001h		;8127	3e 01		> .
	out (c),a		;8129	ed 79		. y
	ld a,000h		;812b	3e 00		> .
	out (0e2h),a		;812d	d3 e2		. .
	ret			;812f	c9		.
	ld bc,09fbdh		;8130	01 bd 9f	. . .
	in a,(c)		;8133	ed 78		. x
	cp 0ffh			;8135	fe ff		. .
	ret nz			;8137	c0		.
	scf			;8138	37		7
	ret			;8139	c9		.
l813ah:
	ld a,l			;813a	7d		}
	or h			;813b	b4		.
	ret z			;813c	c8		.
	push hl			;813d	e5		.
	call sub_8145h		;813e	cd 45 81	. E .
	pop hl			;8141	e1		.
	dec hl			;8142	2b		+
	jr l813ah		;8143	18 f5		. .
sub_8145h:
	push hl			;8145	e5		.
	ld hl,0fe5bh		;8146	21 5b fe	! [ .
l8149h:
	inc hl			;8149	23		#
	ld a,l			;814a	7d		}
	or h			;814b	b4		.
	jr nz,l8149h		;814c	20 fb		  .
	pop hl			;814e	e1		.
	ret			;814f	c9		.
	push hl			;8150	e5		.
	ld a,e			;8151	7b		{
	ld (l817bh),a		;8152	32 7b 81	2 { .
	ld c,021h		;8155	0e 21		. !
	rst 10h			;8157	d7		.
	ld a,b			;8158	78		x
	ld (l817ch),a		;8159	32 7c 81	2 | .
	pop hl			;815c	e1		.
	ret			;815d	c9		.
	push hl			;815e	e5		.
	push de			;815f	d5		.
	push bc			;8160	c5		.
	ld c,021h		;8161	0e 21		. !
	rst 10h			;8163	d7		.
	ld a,(l817ch)		;8164	3a 7c 81	: | .
	cp b			;8167	b8		.
	ld a,b			;8168	78		x
	pop bc			;8169	c1		.
	pop de			;816a	d1		.
	pop hl			;816b	e1		.
	jr z,l8179h		;816c	28 0b		( .
	ld (l817ch),a		;816e	32 7c 81	2 | .
	ld a,(l817bh)		;8171	3a 7b 81	: { .
	dec a			;8174	3d		=
	ld (l817bh),a		;8175	32 7b 81	2 { .
	ret			;8178	c9		.
l8179h:
	and a			;8179	a7		.
	ret			;817a	c9		.
l817bh:
	nop			;817b	00		.
l817ch:
	nop			;817c	00		.
sub_817dh:
	push hl			;817d	e5		.
	ld hl,00064h		;817e	21 64 00	! d .
	call l813ah		;8181	cd 3a 81	. : .
	pop hl			;8184	e1		.
	ret			;8185	c9		.
; GsCommand: write the command to #C0BB (GS command port), wait until status bit 0 (command
; busy) clears
sub_8186h:
	ld (0c0bbh),a		;8186	32 bb c0	2 . .
l8189h:
	ld a,(0c0bbh)		;8189	3a bb c0	: . .
	rrca			;818c	0f		.
	jr c,l8189h		;818d	38 fa		8 .
	ret			;818f	c9		.
; GsSendData: write a byte to #C0B3 (GS data port), wait until status bit 7 (data busy) clears
sub_8190h:
	ld (0c0b3h),a		;8190	32 b3 c0	2 . .
l8193h:
	ld a,(0c0bbh)		;8193	3a bb c0	: . .
	rlca			;8196	07		.
	jr c,l8193h		;8197	38 fa		8 .
	ret			;8199	c9		.
	ld (0c0b3h),a		;819a	32 b3 c0	2 . .
l819dh:
	ld a,(0c0bbh)		;819d	3a bb c0	: . .
	and a			;81a0	a7		.
	ret p			;81a1	f0		.
	jp m,l819dh		;81a2	fa 9d 81	. . .
	ret			;81a5	c9		.
	ld a,(0c0b3h)		;81a6	3a b3 c0	: . .
	ret			;81a9	c9		.
; GsRestart: command #F3 (warm restart of the GS firmware, modules kept)
sub_81aah:
	call sub_8108h		;81aa	cd 08 81	. . .
	ld a,0f3h		;81ad	3e f3		> .
	call sub_8186h		;81af	cd 86 81	. . .
	call sub_8124h		;81b2	cd 24 81	. $ .
	ret			;81b5	c9		.
	call sub_8108h		;81b6	cd 08 81	. . .
	ld a,0f3h		;81b9	3e f3		> .
	ld (0c0bbh),a		;81bb	32 bb c0	2 . .
	ret			;81be	c9		.
; GsSendBlock: send DE bytes from (HL) through GsSendData
sub_81bfh:
	call sub_8108h		;81bf	cd 08 81	. . .
l81c2h:
	ld a,(hl)		;81c2	7e		~
	call sub_8190h		;81c3	cd 90 81	. . .
	inc hl			;81c6	23		#
	dec de			;81c7	1b		.
	ld a,e			;81c8	7b		{
	or d			;81c9	b2		.
	jr nz,l81c2h		;81ca	20 f6		  .
	call sub_8124h		;81cc	cd 24 81	. $ .
	ret			;81cf	c9		.
; GsLoadModuleStart: command #30 (load module), then #D1 (open stream)
sub_81d0h:
	call sub_8108h		;81d0	cd 08 81	. . .
	ld a,030h		;81d3	3e 30		> 0
	call sub_8186h		;81d5	cd 86 81	. . .
	call sub_8124h		;81d8	cd 24 81	. $ .
	call sub_8108h		;81db	cd 08 81	. . .
	ld a,0d1h		;81de	3e d1		> .
	call sub_8186h		;81e0	cd 86 81	. . .
	call sub_8124h		;81e3	cd 24 81	. $ .
	ret			;81e6	c9		.
; GsLoadModuleEnd: command #D2 (close stream)
sub_81e7h:
	call sub_8108h		;81e7	cd 08 81	. . .
	ld a,0d2h		;81ea	3e d2		> .
	call sub_8186h		;81ec	cd 86 81	. . .
	call sub_8124h		;81ef	cd 24 81	. $ .
	ret			;81f2	c9		.
; GsPlayModule: data register <- A (module number), command #31 (play module)
sub_81f3h:
	push af			;81f3	f5		.
	call sub_8108h		;81f4	cd 08 81	. . .
	pop af			;81f7	f1		.
	ld (0c0b3h),a		;81f8	32 b3 c0	2 . .
	ld a,031h		;81fb	3e 31		> 1
	call sub_8186h		;81fd	cd 86 81	. . .
	call sub_8124h		;8200	cd 24 81	. $ .
	ret			;8203	c9		.
; GsDetect: read the status port #C0BB in slot 0, then in slot 1; #FF = no card (empty ISA
; slots read #FF through the data-bus pull-ups). On a hit: GsRestart and two delays, CY = 0
sub_8204h:
	xor a			;8204	af		.
	ld (sub_8108h+1),a	;8205	32 09 81	2 . .
	call sub_8108h		;8208	cd 08 81	. . .
	call sub_817dh		;820b	cd 7d 81	. } .
	ld a,(0c0bbh)		;820e	3a bb c0	: . .
	ex af,af'		;8211	08		.
	call sub_8124h		;8212	cd 24 81	. $ .
	ex af,af'		;8215	08		.
	cp 0ffh			;8216	fe ff		. .
	jr nz,l8234h		;8218	20 1a		  .
	ld a,001h		;821a	3e 01		> .
	ld (sub_8108h+1),a	;821c	32 09 81	2 . .
	call sub_8108h		;821f	cd 08 81	. . .
	call sub_817dh		;8222	cd 7d 81	. } .
	ld a,(0c0bbh)		;8225	3a bb c0	: . .
	ex af,af'		;8228	08		.
	call sub_8124h		;8229	cd 24 81	. $ .
	ex af,af'		;822c	08		.
	cp 0ffh			;822d	fe ff		. .
	jr nz,l8234h		;822f	20 03		  .
	jp l823fh		;8231	c3 3f 82	. ? .
l8234h:
	call sub_81aah		;8234	cd aa 81	. . .
	call sub_817dh		;8237	cd 7d 81	. } .
	call sub_817dh		;823a	cd 7d 81	. } .
	and a			;823d	a7		.
	ret			;823e	c9		.
l823fh:
	call sub_8124h		;823f	cd 24 81	. $ .
	scf			;8242	37		7
	ret			;8243	c9		.
sub_8244h:
	ld c,051h		;8244	0e 51		. Q
	rst 10h			;8246	d7		.
	ld (l8266h),a		;8247	32 66 82	2 f .
	ld a,b			;824a	78		x
	ld (l8267h),a		;824b	32 67 82	2 g .
	ret			;824e	c9		.
sub_824fh:
	ld a,003h		;824f	3e 03		> .
	ld c,a			;8251	4f		O
	ld b,001h		;8252	06 01		. .
	ld (l8268h),bc		;8254	ed 43 68 82	. C h .
	ld c,050h		;8258	0e 50		. P
	rst 10h			;825a	d7		.
	ret			;825b	c9		.
sub_825ch:
	ld a,(l8269h)		;825c	3a 69 82	: i .
	cp 003h			;825f	fe 03		. .
	ret z			;8261	c8		.
	call sub_824fh		;8262	cd 4f 82	. O .
	ret			;8265	c9		.
l8266h:
	nop			;8266	00		.
l8267h:
	nop			;8267	00		.
l8268h:
	nop			;8268	00		.
l8269h:
	nop			;8269	00		.
sub_826ah:
	push hl			;826a	e5		.
	push bc			;826b	c5		.
	ld c,03dh		;826c	0e 3d		. =
	rst 10h			;826e	d7		.
	pop bc			;826f	c1		.
	pop hl			;8270	e1		.
	ret c			;8271	d8		.
	ld (hl),a		;8272	77		w
	inc hl			;8273	23		#
	ld c,a			;8274	4f		O
	dec b			;8275	05		.
l8276h:
	push bc			;8276	c5		.
	push hl			;8277	e5		.
	ld a,c			;8278	79		y
	ld c,0c4h		;8279	0e c4		. .
	rst 8			;827b	cf		.
	pop hl			;827c	e1		.
	pop bc			;827d	c1		.
	jr c,l8288h		;827e	38 08		8 .
	ld (hl),a		;8280	77		w
	inc hl			;8281	23		#
	dec b			;8282	05		.
	jp m,l8288h		;8283	fa 88 82	. . .
	jr l8276h		;8286	18 ee		. .
l8288h:
	xor a			;8288	af		.
	ret			;8289	c9		.
l828ah:
	ld (sub_82dbh+1),a	;828a	32 dc 82	2 . .
	ld (08381h),a		;828d	32 81 83	2 . .
	call sub_825ch		;8290	cd 5c 82	. \ .
	ld hl,l82f1h		;8293	21 f1 82	! . .
	ld c,05ch		;8296	0e 5c		. \
	rst 10h			;8298	d7		.
	call sub_82dbh		;8299	cd db 82	. . .
	ld hl,l82feh		;829c	21 fe 82	! . .
	ld c,05ch		;829f	0e 5c		. \
	rst 10h			;82a1	d7		.
	jp l8371h		;82a2	c3 71 83	. q .
l82a5h:
	ld (sub_82dbh+1),a	;82a5	32 dc 82	2 . .
	ld (08381h),a		;82a8	32 81 83	2 . .
	call sub_825ch		;82ab	cd 5c 82	. \ .
	ld hl,l831fh		;82ae	21 1f 83	! . .
	ld c,05ch		;82b1	0e 5c		. \
	rst 10h			;82b3	d7		.
	call sub_82dbh		;82b4	cd db 82	. . .
	ld hl,l832ch		;82b7	21 2c 83	! , .
	ld c,05ch		;82ba	0e 5c		. \
	rst 10h			;82bc	d7		.
	jp l8371h		;82bd	c3 71 83	. q .
l82c0h:
	ld (sub_82dbh+1),a	;82c0	32 dc 82	2 . .
	ld (08381h),a		;82c3	32 81 83	2 . .
	call sub_825ch		;82c6	cd 5c 82	. \ .
	ld hl,l8348h		;82c9	21 48 83	! H .
	ld c,05ch		;82cc	0e 5c		. \
	rst 10h			;82ce	d7		.
	call sub_82dbh		;82cf	cd db 82	. . .
	ld hl,l8355h		;82d2	21 55 83	! U .
	ld c,05ch		;82d5	0e 5c		. \
	rst 10h			;82d7	d7		.
	jp l8371h		;82d8	c3 71 83	. q .
sub_82dbh:
	ld a,000h		;82db	3e 00		> .
	push af			;82dd	f5		.
	rra			;82de	1f		.
	rra			;82df	1f		.
	rra			;82e0	1f		.
	rra			;82e1	1f		.
	call sub_82e6h		;82e2	cd e6 82	. . .
	pop af			;82e5	f1		.
sub_82e6h:
	and 00fh		;82e6	e6 0f		. .
	cp 00ah			;82e8	fe 0a		. .
	sbc a,069h		;82ea	de 69		. i
	daa			;82ec	27		'
	ld c,05bh		;82ed	0e 5b		. [
	rst 10h			;82ef	d7		.
	ret			;82f0	c9		.
l82f1h:
	dec c			;82f1	0d		.
	ld a,(bc)		;82f2	0a		.
	ld e,e			;82f3	5b		[
	ld b,l			;82f4	45		E
	ld d,d			;82f5	52		R
	ld d,d			;82f6	52		R
	ld c,a			;82f7	4f		O
	ld d,d			;82f8	52		R
	ld e,l			;82f9	5d		]
	ld e,e			;82fa	5b		[
	jr nc,$+122		;82fb	30 78		0 x
	nop			;82fd	00		.
l82feh:
	ld e,l			;82fe	5d		]
	ld a,(04d20h)		;82ff	3a 20 4d	:   M
	ld h,l			;8302	65		e
	ld l,l			;8303	6d		m
	ld l,a			;8304	6f		o
	ld (hl),d		;8305	72		r
	ld a,c			;8306	79		y
	jr nz,l836ah		;8307	20 61		  a
	ld l,h			;8309	6c		l
	ld l,h			;830a	6c		l
	ld l,a			;830b	6f		o
	ld h,e			;830c	63		c
	ld h,c			;830d	61		a
	ld (hl),h		;830e	74		t
	ld l,c			;830f	69		i
	ld l,a			;8310	6f		o
	ld l,(hl)		;8311	6e		n
	jr nz,l837ah		;8312	20 66		  f
	ld h,c			;8314	61		a
	ld l,c			;8315	69		i
	ld l,h			;8316	6c		l
	ld h,l			;8317	65		e
	ld h,h			;8318	64		d
	ld hl,00a0dh		;8319	21 0d 0a	! . .
	dec c			;831c	0d		.
	ld a,(bc)		;831d	0a		.
	nop			;831e	00		.
l831fh:
	dec c			;831f	0d		.
	ld a,(bc)		;8320	0a		.
	ld e,e			;8321	5b		[
	ld b,l			;8322	45		E
	ld d,d			;8323	52		R
	ld d,d			;8324	52		R
	ld c,a			;8325	4f		O
	ld d,d			;8326	52		R
	ld e,l			;8327	5d		]
	ld e,e			;8328	5b		[
	jr nc,l83a3h		;8329	30 78		0 x
	nop			;832b	00		.
l832ch:
	ld e,l			;832c	5d		]
	ld a,(04620h)		;832d	3a 20 46	:   F
	ld h,c			;8330	61		a
	ld l,c			;8331	69		i
	ld l,h			;8332	6c		l
	ld h,l			;8333	65		e
	ld h,h			;8334	64		d
	jr nz,l83abh		;8335	20 74		  t
	ld l,a			;8337	6f		o
	jr nz,l83a9h		;8338	20 6f		  o
	ld (hl),b		;833a	70		p
	ld h,l			;833b	65		e
	ld l,(hl)		;833c	6e		n
	jr nz,l83a5h		;833d	20 66		  f
	ld l,c			;833f	69		i
	ld l,h			;8340	6c		l
	ld h,l			;8341	65		e
	ld hl,00a0dh		;8342	21 0d 0a	! . .
	dec c			;8345	0d		.
	ld a,(bc)		;8346	0a		.
	nop			;8347	00		.
l8348h:
	dec c			;8348	0d		.
	ld a,(bc)		;8349	0a		.
	ld e,e			;834a	5b		[
	ld b,l			;834b	45		E
	ld d,d			;834c	52		R
	ld d,d			;834d	52		R
	ld c,a			;834e	4f		O
	ld d,d			;834f	52		R
	ld e,l			;8350	5d		]
	ld e,e			;8351	5b		[
	jr nc,l83cch		;8352	30 78		0 x
	nop			;8354	00		.
l8355h:
	ld e,l			;8355	5d		]
	ld a,(04620h)		;8356	3a 20 46	:   F
	ld h,c			;8359	61		a
	ld l,c			;835a	69		i
	ld l,h			;835b	6c		l
	ld h,l			;835c	65		e
	ld h,h			;835d	64		d
	jr nz,l83d4h		;835e	20 74		  t
	ld l,a			;8360	6f		o
	jr nz,$+116		;8361	20 72		  r
	ld h,l			;8363	65		e
	ld h,c			;8364	61		a
	ld h,h			;8365	64		d
	jr nz,$+104		;8366	20 66		  f
	ld l,c			;8368	69		i
	ld l,h			;8369	6c		l
l836ah:
	ld h,l			;836a	65		e
	ld hl,00a0dh		;836b	21 0d 0a	! . .
	dec c			;836e	0d		.
	ld a,(bc)		;836f	0a		.
	nop			;8370	00		.
l8371h:
	ld a,(l854fh)		;8371	3a 4f 85	: O .
	ld c,001h		;8374	0e 01		. .
	rst 10h			;8376	d7		.
	ld hl,08550h		;8377	21 50 85	! P .
l837ah:
	ld c,01dh		;837a	0e 1d		. .
	rst 10h			;837c	d7		.
	call sub_825ch		;837d	cd 5c 82	. \ .
	ld b,000h		;8380	06 00		. .
	ld c,041h		;8382	0e 41		. A
	rst 10h			;8384	d7		.
l8385h:
	jr l8385h		;8385	18 fe		. .
sub_8387h:
	call sub_8244h		;8387	cd 44 82	. D .
	ld a,(l8266h)		;838a	3a 66 82	: f .
	cp 003h			;838d	fe 03		. .
	jr z,l8394h		;838f	28 03		( .
	call sub_824fh		;8391	cd 4f 82	. O .
l8394h:
	ld (l8269h),a		;8394	32 69 82	2 i .
	ld hl,l8457h		;8397	21 57 84	! W .
	ld c,05ch		;839a	0e 5c		. \
	rst 10h			;839c	d7		.
	ld c,002h		;839d	0e 02		. .
	rst 10h			;839f	d7		.
	ld (l854fh),a		;83a0	32 4f 85	2 O .
l83a3h:
	ld c,01eh		;83a3	0e 1e		. .
l83a5h:
	ld hl,08550h		;83a5	21 50 85	! P .
	rst 10h			;83a8	d7		.
l83a9h:
	in a,(0e2h)		;83a9	db e2		. .
l83abh:
	ld (l8546h),a		;83ab	32 46 85	2 F .
	ld b,001h		;83ae	06 01		. .
	ld hl,l8541h		;83b0	21 41 85	! A .
	call sub_826ah		;83b3	cd 6a 82	. j .
	ret			;83b6	c9		.
l83b7h:
	ld hl,l84c8h		;83b7	21 c8 84	! . .
	ld c,05ch		;83ba	0e 5c		. \
	rst 10h			;83bc	d7		.
	jp l8371h		;83bd	c3 71 83	. q .
l83c0h:
	call sub_8387h		;83c0	cd 87 83	. . .
	jp c,l828ah		;83c3	da 8a 82	. . .
	ld hl,l8501h		;83c6	21 01 85	! . .
	ld c,05ch		;83c9	0e 5c		. \
	rst 10h			;83cb	d7		.
; Main: detect, print the slot, open the MOD (DSS #11), save PORT_Y (#89) and set it to #C0,
; load the file in 16 KB pieces (DSS #13 to #4000) and stream every byte to the card, close the
; file (DSS #12), close the stream, play module 0, return to DSS (the card keeps playing)
l83cch:
	call sub_8204h		;83cc	cd 04 82	. . .
	ld hl,l8520h		;83cf	21 20 85	!   .
	jr nc,l83dfh		;83d2	30 0b		0 .
l83d4h:
	ld c,05ch		;83d4	0e 5c		. \
	rst 10h			;83d6	d7		.
	ld a,0ffh		;83d7	3e ff		> .
	ld (08381h),a		;83d9	32 81 83	2 . .
	jp l8371h		;83dc	c3 71 83	. q .
l83dfh:
	ld hl,l8510h		;83df	21 10 85	! . .
	ld c,05ch		;83e2	0e 5c		. \
	rst 10h			;83e4	d7		.
	ld hl,l852dh		;83e5	21 2d 85	! - .
	ld a,(sub_8108h+1)	;83e8	3a 09 81	: . .
	or a			;83eb	b7		.
	jr z,l83f1h		;83ec	28 03		( .
	ld hl,l8531h		;83ee	21 31 85	! 1 .
l83f1h:
	ld c,05ch		;83f1	0e 5c		. \
	rst 10h			;83f3	d7		.
	ld a,(l8542h)		;83f4	3a 42 85	: B .
	out (0a2h),a		;83f7	d3 a2		. .
	ld hl,(l854ah)		;83f9	2a 4a 85	* J .
	ld a,(hl)		;83fc	7e		~
	or a			;83fd	b7		.
	jp z,l83b7h		;83fe	ca b7 83	. . .
	inc hl			;8401	23		#
	inc hl			;8402	23		#
	ld (l854ch),hl		;8403	22 4c 85	" L .
	ld c,011h		;8406	0e 11		. .
	ld a,001h		;8408	3e 01		> .
	rst 10h			;840a	d7		.
	jp c,l82a5h		;840b	da a5 82	. . .
	ld (l8547h),a		;840e	32 47 85	2 G .
	in a,(089h)		;8411	db 89		. .
	ld (l854eh),a		;8413	32 4e 85	2 N .
	ld a,0c0h		;8416	3e c0		> .
	out (089h),a		;8418	d3 89		. .
	call sub_81d0h		;841a	cd d0 81	. . .
l841dh:
	ld a,(l8547h)		;841d	3a 47 85	: G .
	ld hl,04000h		;8420	21 00 40	! . @
	ld d,h			;8423	54		T
	ld e,l			;8424	5d		]
	ld c,013h		;8425	0e 13		. .
	rst 10h			;8427	d7		.
	ld (l8548h),de		;8428	ed 53 48 85	. S H .
	jp c,l82c0h		;842c	da c0 82	. . .
	push af			;842f	f5		.
	push de			;8430	d5		.
	ld a,02eh		;8431	3e 2e		> .
	ld c,05bh		;8433	0e 5b		. [
	rst 10h			;8435	d7		.
	pop de			;8436	d1		.
	ld hl,04000h		;8437	21 00 40	! . @
	call sub_81bfh		;843a	cd bf 81	. . .
	pop af			;843d	f1		.
	or a			;843e	b7		.
	jr z,l841dh		;843f	28 dc		( .
	ld a,(l8547h)		;8441	3a 47 85	: G .
	ld c,012h		;8444	0e 12		. .
	rst 10h			;8446	d7		.
	call sub_81e7h		;8447	cd e7 81	. . .
	sub a			;844a	97		.
	call sub_81f3h		;844b	cd f3 81	. . .
	ld hl,l8535h		;844e	21 35 85	! 5 .
	ld c,05ch		;8451	0e 5c		. \
	rst 10h			;8453	d7		.
	jp l8371h		;8454	c3 71 83	. q .
l8457h:
	dec c			;8457	0d		.
	ld a,(bc)		;8458	0a		.
	ld d,b			;8459	50		P
	ld (hl),d		;845a	72		r
	ld l,a			;845b	6f		o
	ld d,b			;845c	50		P
	ld l,h			;845d	6c		l
	ld h,c			;845e	61		a
	ld a,c			;845f	79		y
	jr nz,l848fh		;8460	20 2d		  -
	jr nz,l84abh		;8462	20 47		  G
	ld h,l			;8464	65		e
	ld l,(hl)		;8465	6e		n
	ld h,l			;8466	65		e
	ld (hl),d		;8467	72		r
	ld h,c			;8468	61		a
	ld l,h			;8469	6c		l
	jr nz,l84bfh		;846a	20 53		  S
	ld l,a			;846c	6f		o
	ld (hl),l		;846d	75		u
	ld l,(hl)		;846e	6e		n
	ld h,h			;846f	64		d
	jr nz,l84bfh		;8470	20 4d		  M
	ld c,a			;8472	4f		O
	ld b,h			;8473	44		D
	jr nz,l84e6h		;8474	20 70		  p
	ld l,h			;8476	6c		l
	ld h,c			;8477	61		a
	ld a,c			;8478	79		y
	ld h,l			;8479	65		e
	ld (hl),d		;847a	72		r
	jr nz,l84f3h		;847b	20 76		  v
	jr nc,l84adh		;847d	30 2e		0 .
	dec (hl)		;847f	35		5
	ld l,039h		;8480	2e 39		. 9
	ld sp,02820h		;8482	31 20 28	1   (
	jr nc,l84beh		;8485	30 37		0 7
	ld l,030h		;8487	2e 30		. 0
	ld (0322eh),a		;8489	32 2e 32	2 . 2
	inc sp			;848c	33		3
	add hl,hl		;848d	29		)
	dec c			;848e	0d		.
l848fh:
	ld a,(bc)		;848f	0a		.
	ld h,d			;8490	62		b
	ld a,c			;8491	79		y
	jr nz,l84e1h		;8492	20 4d		  M
	ld l,c			;8494	69		i
	ld (hl),d		;8495	72		r
	ld l,a			;8496	6f		o
	ld (hl),e		;8497	73		s
	ld l,b			;8498	68		h
	ld l,(hl)		;8499	6e		n
	ld l,c			;849a	69		i
	ld h,e			;849b	63		c
	ld l,b			;849c	68		h
	ld h,l			;849d	65		e
	ld l,(hl)		;849e	6e		n
	ld l,e			;849f	6b		k
	ld l,a			;84a0	6f		o
	jr nz,$+67		;84a1	20 41		  A
	ld l,h			;84a3	6c		l
	ld h,l			;84a4	65		e
	ld l,e			;84a5	6b		k
	ld (hl),e		;84a6	73		s
	ld h,c			;84a7	61		a
	ld l,(hl)		;84a8	6e		n
	ld h,h			;84a9	64		d
	ld (hl),d		;84aa	72		r
l84abh:
	jr nz,l850eh		;84ab	20 61		  a
l84adh:
	ld l,e			;84ad	6b		k
	ld h,c			;84ae	61		a
	jr nz,l8504h		;84af	20 53		  S
	ld h,c			;84b1	61		a
	ld a,c			;84b2	79		y
	ld l,l			;84b3	6d		m
	ld h,c			;84b4	61		a
	ld l,(hl)		;84b5	6e		n
	ld b,b			;84b6	40		@
	ld d,e			;84b7	53		S
	ld (hl),b		;84b8	70		p
	ld (hl),d		;84b9	72		r
	ld l,c			;84ba	69		i
	ld l,(hl)		;84bb	6e		n
	ld (hl),h		;84bc	74		t
	ld h,l			;84bd	65		e
l84beh:
	ld (hl),d		;84be	72		r
l84bfh:
	ld d,h			;84bf	54		T
	ld h,l			;84c0	65		e
	ld h,c			;84c1	61		a
	ld l,l			;84c2	6d		m
	dec c			;84c3	0d		.
	ld a,(bc)		;84c4	0a		.
	dec c			;84c5	0d		.
	ld a,(bc)		;84c6	0a		.
	nop			;84c7	00		.
l84c8h:
	ld d,a			;84c8	57		W
	ld (hl),d		;84c9	72		r
	ld l,a			;84ca	6f		o
	ld l,(hl)		;84cb	6e		n
	ld h,a			;84cc	67		g
	jr nz,l853fh		;84cd	20 70		  p
	ld h,c			;84cf	61		a
	ld (hl),d		;84d0	72		r
	ld h,c			;84d1	61		a
	ld l,l			;84d2	6d		m
	ld h,l			;84d3	65		e
	ld (hl),h		;84d4	74		t
	ld h,l			;84d5	65		e
	ld (hl),d		;84d6	72		r
	ld (hl),e		;84d7	73		s
	ld hl,00a0dh		;84d8	21 0d 0a	! . .
	ld (hl),l		;84db	75		u
	ld (hl),e		;84dc	73		s
	ld h,l			;84dd	65		e
	jr nz,$+118		;84de	20 74		  t
	ld l,b			;84e0	68		h
l84e1h:
	ld l,c			;84e1	69		i
	ld (hl),e		;84e2	73		s
	ld a,(05020h)		;84e3	3a 20 50	:   P
l84e6h:
	ld d,d			;84e6	52		R
	ld c,a			;84e7	4f		O
	ld d,b			;84e8	50		P
	ld c,h			;84e9	4c		L
	ld b,c			;84ea	41		A
	ld e,c			;84eb	59		Y
	ld l,045h		;84ec	2e 45		. E
	ld e,b			;84ee	58		X
	ld b,l			;84ef	45		E
	jr nz,l853fh		;84f0	20 4d		  M
	ld c,a			;84f2	4f		O
l84f3h:
	ld b,h			;84f3	44		D
	ld c,(hl)		;84f4	4e		N
	ld b,c			;84f5	41		A
	ld c,l			;84f6	4d		M
	ld b,l			;84f7	45		E
	ld l,04dh		;84f8	2e 4d		. M
	ld c,a			;84fa	4f		O
	ld b,h			;84fb	44		D
	dec c			;84fc	0d		.
	ld a,(bc)		;84fd	0a		.
	dec c			;84fe	0d		.
	ld a,(bc)		;84ff	0a		.
	nop			;8500	00		.
l8501h:
	ld b,a			;8501	47		G
	ld h,l			;8502	65		e
	ld l,(hl)		;8503	6e		n
l8504h:
	ld h,l			;8504	65		e
	ld (hl),d		;8505	72		r
	ld h,c			;8506	61		a
	ld l,h			;8507	6c		l
	jr nz,$+85		;8508	20 53		  S
	ld l,a			;850a	6f		o
	ld (hl),l		;850b	75		u
	ld l,(hl)		;850c	6e		n
	ld h,h			;850d	64		d
l850eh:
	jr nz,l8510h		;850e	20 00		  .
l8510h:
	ld h,(hl)		;8510	66		f
	ld l,a			;8511	6f		o
	ld (hl),l		;8512	75		u
	ld l,(hl)		;8513	6e		n
	ld h,h			;8514	64		d
	jr nz,$+99		;8515	20 61		  a
	ld (hl),h		;8517	74		t
	jr nz,$+117		;8518	20 73		  s
	ld l,h			;851a	6c		l
	ld l,a			;851b	6f		o
	ld (hl),h		;851c	74		t
	ld a,(00020h)		;851d	3a 20 00	:   .
l8520h:
	ld l,(hl)		;8520	6e		n
	ld l,a			;8521	6f		o
	ld (hl),h		;8522	74		t
	jr nz,$+104		;8523	20 66		  f
	ld l,a			;8525	6f		o
	ld (hl),l		;8526	75		u
	ld l,(hl)		;8527	6e		n
	ld h,h			;8528	64		d
	ld l,00dh		;8529	2e 0d		. .
	ld a,(bc)		;852b	0a		.
	nop			;852c	00		.
l852dh:
	jr nc,$+15		;852d	30 0d		0 .
	ld a,(bc)		;852f	0a		.
	nop			;8530	00		.
l8531h:
	ld sp,00a0dh		;8531	31 0d 0a	1 . .
	nop			;8534	00		.
l8535h:
	dec c			;8535	0d		.
	ld a,(bc)		;8536	0a		.
	ld b,h			;8537	44		D
	ld l,a			;8538	6f		o
	ld l,(hl)		;8539	6e		n
	ld h,l			;853a	65		e
	ld l,00dh		;853b	2e 0d		. .
	ld a,(bc)		;853d	0a		.
	dec c			;853e	0d		.
l853fh:
	ld a,(bc)		;853f	0a		.
	nop			;8540	00		.
l8541h:
	nop			;8541	00		.
l8542h:
	nop			;8542	00		.
	nop			;8543	00		.
	nop			;8544	00		.
	nop			;8545	00		.
l8546h:
	nop			;8546	00		.
l8547h:
	nop			;8547	00		.
l8548h:
	nop			;8548	00		.
	nop			;8549	00		.
l854ah:
	nop			;854a	00		.
	nop			;854b	00		.
l854ch:
	nop			;854c	00		.
	nop			;854d	00		.
l854eh:
	nop			;854e	00		.
l854fh:
	nop			;854f	00		.
