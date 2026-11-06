; prosiak.exe (Wild Sound XM player, Sprinter). Rebuilt by tools/make_listings.py from the
; file of the MAME pack's system disk (DEMOS/WILDSND); z80dasm 1.2.0 with -a -l -t.
; Stage 1: the primary loader, #4327-#44E9
; z80dasm 1.2.0

	org 04327h


; Load address of the primary loader (file offset #24347, #6AC bytes).
LoaderStart:
	ld hl,04246h		;4327	21 46 42	! F B

; DSS GetMem (#3D): A = pages, C = handle tag. Keeps up to #40 blocks: the DSS handle
; at #4266+n, the tag at #42E6+n; BIOS GetMemBlkPages (#C5) writes the block's
; physical pages. Carry = no memory.
AllocBlock:
	ld (0434eh),a		;432a	32 4e 43	2 N C
	ld a,c			;432d	79		y
	ld (0436fh),a		;432e	32 6f 43	2 o C
	ld a,(04326h)		;4331	3a 26 43	: & C
	cp 040h			;4334	fe 40		. @
	ccf			;4336	3f		?
	ret c			;4337	d8		.
	push bc			;4338	c5		.
	push de			;4339	d5		.
	add a,066h		;433a	c6 66		. f
	ld e,a			;433c	5f		_
	ld a,042h		;433d	3e 42		> B
	adc a,000h		;433f	ce 00		. .
	ld d,a			;4341	57		W
	ld (04369h),de		;4342	ed 53 69 43	. S i C
	ld bc,(000a2h)		;4346	ed 4b a2 00	. K . .
	ld (hl),c		;434a	71		q
	push hl			;434b	e5		.
	push de			;434c	d5		.
	ld b,000h		;434d	06 00		. .
	ld c,03dh		;434f	0e 3d		. =
	rst 10h			;4351	d7		.
	pop hl			;4352	e1		.
	jr c,l4371h		;4353	38 1c		8 .
	ld (hl),a		;4355	77		w
	ld hl,04326h		;4356	21 26 43	! & C
	inc (hl)		;4359	34		4
	pop hl			;435a	e1		.
	push hl			;435b	e5		.
	push af			;435c	f5		.
	ld c,0c5h		;435d	0e c5		. .
	rst 8			;435f	cf		.
	pop af			;4360	f1		.
	pop hl			;4361	e1		.
	ld c,(hl)		;4362	4e		N
	push hl			;4363	e5		.
	push af			;4364	f5		.
	ld de,00040h		;4365	11 40 00	. @ .
	ld hl,00000h		;4368	21 00 00	! . .
	add hl,de		;436b	19		.
	ld (hl),c		;436c	71		q
	add hl,de		;436d	19		.
	ld (hl),000h		;436e	36 00		6 .
	pop af			;4370	f1		.
l4371h:
	pop hl			;4371	e1		.
	pop de			;4372	d1		.
	pop bc			;4373	c1		.
	ret			;4374	c9		.

; One page: AllocBlock with A = 1; returns the physical page in A.
AllocPage:
	push hl			;4375	e5		.
	push hl			;4376	e5		.
	ld hl,00000h		;4377	21 00 00	! . .
	add hl,sp		;437a	39		9
	ld a,001h		;437b	3e 01		> .
	call AllocBlock		;437d	cd 2a 43	. * C
	pop hl			;4380	e1		.
	ld a,l			;4381	7d		}
	pop hl			;4382	e1		.
	ret			;4383	c9		.

; DSS FreeMem (#3E) for every block AllocBlock got.
FreeAll:
	ld hl,04266h		;4384	21 66 42	! f B
	ld bc,(04326h)		;4387	ed 4b 26 43	. K & C
	ld b,000h		;438b	06 00		. .
	add hl,bc		;438d	09		.
	inc c			;438e	0c		.
	jr l439ah		;438f	18 09		. .
l4391h:
	dec hl			;4391	2b		+
	push hl			;4392	e5		.
	push bc			;4393	c5		.
	ld a,(hl)		;4394	7e		~
	ld c,03eh		;4395	0e 3e		. >
	rst 10h			;4397	d7		.
	pop bc			;4398	c1		.
	pop hl			;4399	e1		.
l439ah:
	dec c			;439a	0d		.
	jr nz,l4391h		;439b	20 f4		  .
	xor a			;439d	af		.
	ld (04326h),a		;439e	32 26 43	2 & C
	ret			;43a1	c9		.

; C = tag: A = the block's first physical page, Carry = unknown tag.
PageOfTag:
	ld a,(04326h)		;43a2	3a 26 43	: & C
	or a			;43a5	b7		.
	jr z,l43b8h		;43a6	28 10		( .
	push hl			;43a8	e5		.
	push de			;43a9	d5		.
	push bc			;43aa	c5		.
	ld b,a			;43ab	47		G
	ld hl,042e6h		;43ac	21 e6 42	! . B
l43afh:
	ld a,(hl)		;43af	7e		~
	cp c			;43b0	b9		.
	jr z,l43bah		;43b1	28 07		( .
	djnz l43afh		;43b3	10 fa		. .
	pop bc			;43b5	c1		.
	pop de			;43b6	d1		.
	pop hl			;43b7	e1		.
l43b8h:
	scf			;43b8	37		7
	ret			;43b9	c9		.
l43bah:
	ld de,0ffc0h		;43ba	11 c0 ff	. . .
	add hl,de		;43bd	19		.
	ld a,(hl)		;43be	7e		~
	pop bc			;43bf	c1		.
	pop de			;43c0	d1		.
	pop hl			;43c1	e1		.
	and a			;43c2	a7		.
	ret			;43c3	c9		.

; Call stage 2 at #0100 (window 0 = the page AllocPage gave), then restore window 0,
; close the file (DSS #12), print "Done ...", FreeAll, DSS Exit (#41).
RunStage2:
	call 00100h		;43c4	cd 00 01	. . .
	ld a,(04100h)		;43c7	3a 00 41	: . A
	out (082h),a		;43ca	d3 82		. .
	ld a,(04105h)		;43cc	3a 05 41	: . A
	ld c,012h		;43cf	0e 12		. .
	rst 10h			;43d1	d7		.
	ld hl,d43ea_start	;43d2	21 ea 43	! . C
	ld c,05ch		;43d5	0e 5c		. \
	rst 10h			;43d7	d7		.
	call FreeAll		;43d8	cd 84 43	. . C
l43dbh:
	ei			;43db	fb		.
	ld bc,00041h		;43dc	01 41 00	. A .
	rst 10h			;43df	d7		.
l43e0h:
	jr l43e0h		;43e0	18 fe		. .

; Print "Error Execute ..." and exit.
ErrorExit:
	ld hl,d43ea_start	;43e2	21 ea 43	! . C
	ld c,05ch		;43e5	0e 5c		. \
	rst 10h			;43e7	d7		.
	jr l43dbh		;43e8	18 f1		. .

; BLOCK 'd43ea' (start 0x43ea end 0x4409)
d43ea_start:
	defb 044h		;43ea	44		D
	defb 06fh		;43eb	6f		o
	defb 06eh		;43ec	6e		n
	defb 065h		;43ed	65		e
	defb 020h		;43ee	20		 
	defb 02eh		;43ef	2e		.
	defb 02eh		;43f0	2e		.
	defb 02eh		;43f1	2e		.
	defb 00dh		;43f2	0d		.
	defb 00ah		;43f3	0a		.
	defb 000h		;43f4	00		.
	defb 045h		;43f5	45		E
	defb 072h		;43f6	72		r
	defb 072h		;43f7	72		r
	defb 06fh		;43f8	6f		o
	defb 072h		;43f9	72		r
	defb 020h		;43fa	20		 
	defb 045h		;43fb	45		E
	defb 078h		;43fc	78		x
	defb 065h		;43fd	65		e
	defb 063h		;43fe	63		c
	defb 075h		;43ff	75		u
	defb 074h		;4400	74		t
	defb 065h		;4401	65		e
	defb 020h		;4402	20		 
	defb 02eh		;4403	2e		.
	defb 02eh		;4404	2e		.
	defb 02eh		;4405	2e		.
	defb 00dh		;4406	0d		.
	defb 00ah		;4407	0a		.
	defb 000h		;4408	00		.

; The DSS start address. (IX-3) = CLP_FM, the handle of this EXE file that DSS keeps
; open for a program with a primary loader; kept at #4105 for every later read.
; Saves the window-0 page (port #82) at #4100, prints the banner, takes three pages
; (window 0 for stage 2, windows 2 and 3), copies Depack to #3D4F and unpacks the
; LZSA2 stream at #44EA to #0000 of the new window-0 page. Then the 33-byte RST
; trampolines (stage 2 #05E6) go to #3DDF of both window-0 pages, so DSS calls work
; from stage 2.
Entry:
	di			;4409	f3		.
	ld a,(ix-003h)		;440a	dd 7e fd	. ~ .
	ld (04105h),a		;440d	32 05 41	2 . A
	xor a			;4410	af		.
	ld (04326h),a		;4411	32 26 43	2 & C
	in a,(082h)		;4414	db 82		. .
	ld (04100h),a		;4416	32 00 41	2 . A
	ld hl,d44af_start	;4419	21 af 44	! . D
	ld c,05ch		;441c	0e 5c		. \
	rst 10h			;441e	d7		.
	ld c,000h		;441f	0e 00		. .
	call AllocPage		;4421	cd 75 43	. u C
	jp c,ErrorExit		;4424	da e2 43	. . C
	call AllocPage		;4427	cd 75 43	. u C
	jp c,ErrorExit		;442a	da e2 43	. . C
	call AllocPage		;442d	cd 75 43	. u C
	jp c,ErrorExit		;4430	da e2 43	. . C
	in a,(0a2h)		;4433	db a2		. .
	ld (04102h),a		;4435	32 02 41	2 . A
	ld a,(042a7h)		;4438	3a a7 42	: . B
	ld (04103h),a		;443b	32 03 41	2 . A
	out (0c2h),a		;443e	d3 c2		. .
	ld a,(042a8h)		;4440	3a a8 42	: . B
	ld (04104h),a		;4443	32 04 41	2 . A
	out (0e2h),a		;4446	d3 e2		. .
	ld a,(042a6h)		;4448	3a a6 42	: . B
	ld (04101h),a		;444b	32 01 41	2 . A
	ld hl,04943h		;444e	21 43 49	! C I
	ld de,03d4fh		;4451	11 4f 3d	. O =
	ld bc,00090h		;4454	01 90 00	. . .
	push hl			;4457	e5		.
	push de			;4458	d5		.
	push bc			;4459	c5		.
	out (082h),a		;445a	d3 82		. .
	pop bc			;445c	c1		.
	pop de			;445d	d1		.
	pop hl			;445e	e1		.
	ldir			;445f	ed b0		. .
	ld hl,d44af_end		;4461	21 ea 44	! . D
	ld de,00000h		;4464	11 00 00	. . .
	call 03d56h		;4467	cd 56 3d	. V =
	ld hl,04943h		;446a	21 43 49	! C I
	ld de,03d4fh		;446d	11 4f 3d	. O =
	ld bc,00090h		;4470	01 90 00	. . .
	ldir			;4473	ed b0		. .
	ld hl,005e6h		;4475	21 e6 05	! . .
	ld de,03ddfh		;4478	11 df 3d	. . =
	ld bc,00021h		;447b	01 21 00	. ! .
	ldir			;447e	ed b0		. .
	ld a,(04100h)		;4480	3a 00 41	: . A
	ld (03debh),a		;4483	32 eb 3d	2 . =
	ld a,(04101h)		;4486	3a 01 41	: . A
	ld (03dfbh),a		;4489	32 fb 3d	2 . =
	ld hl,03ddfh		;448c	21 df 3d	! . =
	ld de,04246h		;448f	11 46 42	. F B
	ld bc,00021h		;4492	01 21 00	. ! .
	ldir			;4495	ed b0		. .
	ld a,(04100h)		;4497	3a 00 41	: . A
	out (082h),a		;449a	d3 82		. .
	ld hl,04246h		;449c	21 46 42	! F B
	ld de,03ddfh		;449f	11 df 3d	. . =
	ld bc,00021h		;44a2	01 21 00	. ! .
	ldir			;44a5	ed b0		. .
	ld a,(04101h)		;44a7	3a 01 41	: . A
	out (082h),a		;44aa	d3 82		. .
	jp RunStage2		;44ac	c3 c4 43	. . C

; BLOCK 'd44af' (start 0x44af end 0x44ea)
d44af_start:
	defb 042h		;44af	42		B
	defb 06fh		;44b0	6f		o
	defb 06fh		;44b1	6f		o
	defb 074h		;44b2	74		t
	defb 020h		;44b3	20		 
	defb 04ch		;44b4	4c		L
	defb 06fh		;44b5	6f		o
	defb 061h		;44b6	61		a
	defb 064h		;44b7	64		d
	defb 065h		;44b8	65		e
	defb 072h		;44b9	72		r
	defb 020h		;44ba	20		 
	defb 046h		;44bb	46		F
	defb 06fh		;44bc	6f		o
	defb 072h		;44bd	72		r
	defb 020h		;44be	20		 
	defb 053h		;44bf	53		S
	defb 070h		;44c0	70		p
	defb 072h		;44c1	72		r
	defb 069h		;44c2	69		i
	defb 06eh		;44c3	6e		n
	defb 074h		;44c4	74		t
	defb 065h		;44c5	65		e
	defb 072h		;44c6	72		r
	defb 020h		;44c7	20		 
	defb 042h		;44c8	42		B
	defb 079h		;44c9	79		y
	defb 020h		;44ca	20		 
	defb 052h		;44cb	52		R
	defb 06fh		;44cc	6f		o
	defb 062h		;44cd	62		b
	defb 020h		;44ce	20		 
	defb 046h		;44cf	46		F
	defb 02eh		;44d0	2e		.
	defb 00dh		;44d1	0d		.
	defb 00ah		;44d2	0a		.
	defb 000h		;44d3	00		.
	defb 021h		;44d4	21		!
	defb 021h		;44d5	21		!
	defb 021h		;44d6	21		!
	defb 020h		;44d7	20		 
	defb 057h		;44d8	57		W
	defb 04fh		;44d9	4f		O
	defb 057h		;44da	57		W
	defb 020h		;44db	20		 
	defb 048h		;44dc	48		H
	defb 04fh		;44dd	4f		O
	defb 057h		;44de	57		W
	defb 020h		;44df	20		 
	defb 044h		;44e0	44		D
	defb 04fh		;44e1	4f		O
	defb 057h		;44e2	57		W
	defb 020h		;44e3	20		 
	defb 021h		;44e4	21		!
	defb 021h		;44e5	21		!
	defb 021h		;44e6	21		!
	defb 00dh		;44e7	0d		.
	defb 00ah		;44e8	0a		.
	defb 000h		;44e9	00		.

; #44EA-#4942: the LZSA2 stream of stage 2 (stage2.asm); #4943-#49D2: Depack, below at its run
; address #3D4F (the loader copies it there before the call)

; Depack at #3D4F
; z80dasm 1.2.0

	org 03d4fh


; Entry for a backward stream (not used here).
DepackBackward:
	ld b,(hl)		;3d4f	46		F
	dec hl			;3d50	2b		+
	ld c,(hl)		;3d51	4e		N
	dec hl			;3d52	2b		+
	scf			;3d53	37		7
	sbc hl,bc		;3d54	ed 42		. B

; LZSA2 raw block, forward: HL = packed data, DE = output. The format is LZSA2 of
; github.com/emmanuel-marty/lzsa; `lzsa -d -r -f 2` gives the same bytes.
Depack:
	xor a			;3d56	af		.
	ld b,a			;3d57	47		G
	ex af,af'		;3d58	08		.
	jr l3d85h		;3d59	18 2a		. *
l3d5bh:
	call DepackNibble	;3d5b	cd ce 3d	. . =
	ld e,a			;3d5e	5f		_
	ld a,c			;3d5f	79		y
	cp 020h			;3d60	fe 20		.  
	rl e			;3d62	cb 13		. .
	jr l3d72h		;3d64	18 0c		. .
l3d66h:
	ld d,0ffh		;3d66	16 ff		. .
	cp 040h			;3d68	fe 40		. @
	jr c,l3d5bh		;3d6a	38 ef		8 .
	cp 060h			;3d6c	fe 60		. `
	rl d			;3d6e	cb 12		. .
l3d70h:
	ld e,(hl)		;3d70	5e		^
	inc hl			;3d71	23		#
l3d72h:
	push de			;3d72	d5		.
	pop ix			;3d73	dd e1		. .
l3d75h:
	and 007h		;3d75	e6 07		. .
	add a,002h		;3d77	c6 02		. .
	cp 009h			;3d79	fe 09		. .
	call z,DepackLength	;3d7b	cc b7 3d	. . =
	ld c,a			;3d7e	4f		O
	ex (sp),hl		;3d7f	e3		.
	ex de,hl		;3d80	eb		.
	add hl,de		;3d81	19		.
	ldir			;3d82	ed b0		. .
	pop hl			;3d84	e1		.
l3d85h:
	ld a,(hl)		;3d85	7e		~
	inc hl			;3d86	23		#
	push af			;3d87	f5		.
	and 018h		;3d88	e6 18		. .
	jr z,l3d95h		;3d8a	28 09		( .
	rrca			;3d8c	0f		.
	rrca			;3d8d	0f		.
	rrca			;3d8e	0f		.
	call pe,DepackLength	;3d8f	ec b7 3d	. . =
	ld c,a			;3d92	4f		O
	ldir			;3d93	ed b0		. .
l3d95h:
	pop af			;3d95	f1		.
	push de			;3d96	d5		.
	or a			;3d97	b7		.
	jp p,l3d66h		;3d98	f2 66 3d	. f =
	cp 0c0h			;3d9b	fe c0		. .
	jr nc,l3daeh		;3d9d	30 0f		0 .
	call DepackNibble	;3d9f	cd ce 3d	. . =
	ld d,a			;3da2	57		W
	ld a,c			;3da3	79		y
	cp 0a0h			;3da4	fe a0		. .
	dec d			;3da6	15		.
	rl d			;3da7	cb 12		. .
	jp z,02356h		;3da9	ca 56 23	. V #
	jr l3d70h		;3dac	18 c2		. .
l3daeh:
	cp 0e0h			;3dae	fe e0		. .
	jr c,$-6		;3db0	38 f8		8 .
	push ix			;3db2	dd e5		. .
	pop de			;3db4	d1		.
	jr l3d75h		;3db5	18 be		. .

; Extended length: nibble, then byte, then 16-bit word.
DepackLength:
	call DepackNibble	;3db7	cd ce 3d	. . =
	inc a			;3dba	3c		<
	jr z,l3dc1h		;3dbb	28 04		( .
	sub 0f1h		;3dbd	d6 f1		. .
	add a,c			;3dbf	81		.
	ret			;3dc0	c9		.
l3dc1h:
	ld a,00fh		;3dc1	3e 0f		> .
	add a,c			;3dc3	81		.
	add a,(hl)		;3dc4	86		.
	inc hl			;3dc5	23		#
	ret nc			;3dc6	d0		.
	ld a,(hl)		;3dc7	7e		~
	inc hl			;3dc8	23		#
	ld b,(hl)		;3dc9	46		F
	inc hl			;3dca	23		#
	ret nz			;3dcb	c0		.
	pop de			;3dcc	d1		.
	pop de			;3dcd	d1		.

; Next nibble (high first) in A, the other half kept in A'.
DepackNibble:
	ld c,a			;3dce	4f		O
	xor a			;3dcf	af		.
	ex af,af'		;3dd0	08		.
	ret m			;3dd1	f8		.
	ld a,(hl)		;3dd2	7e		~
	or 0f0h			;3dd3	f6 f0		. .
	ex af,af'		;3dd5	08		.
	ld a,(hl)		;3dd6	7e		~
	inc hl			;3dd7	23		#
	or 00fh			;3dd8	f6 0f		. .
	rrca			;3dda	0f		.
	rrca			;3ddb	0f		.
	rrca			;3ddc	0f		.
	rrca			;3ddd	0f		.
	ret			;3dde	c9		.
