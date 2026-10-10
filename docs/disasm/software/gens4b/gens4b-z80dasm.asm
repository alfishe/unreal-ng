; z80dasm 1.2.0
; command line: z80dasm -a -l -g 30000 -t gens4b_org7530.bin

	org 07530h

	ld hl,028a0h		;7530	21 a0 28	! . (
	add hl,bc		;7533	09		.
l7534h:
	ld e,(hl)		;7534	5e		^
	inc hl			;7535	23		#
	ld d,(hl)		;7536	56		V
	inc hl			;7537	23		#
	ld a,d			;7538	7a		z
	or e			;7539	b3		.
	jr z,l7572h		;753a	28 36		( 6
	ex de,hl		;753c	eb		.
	add hl,bc		;753d	09		.
	push de			;753e	d5		.
	push hl			;753f	e5		.
	ld e,(hl)		;7540	5e		^
	inc hl			;7541	23		#
	ld d,(hl)		;7542	56		V
	ex de,hl		;7543	eb		.
	add hl,bc		;7544	09		.
	ex de,hl		;7545	eb		.
	pop hl			;7546	e1		.
	ld (hl),e		;7547	73		s
	inc hl			;7548	23		#
	ld (hl),d		;7549	72		r
	pop hl			;754a	e1		.
	jr l7534h		;754b	18 e7		. .
	jp 00230h		;754d	c3 30 02	. 0 .
l7550h:
	jp 00282h		;7550	c3 82 02	. . .
	jp 00310h		;7553	c3 10 03	. . .
	jp 00053h		;7556	c3 53 00	. S .
	jp 0031ch		;7559	c3 1c 03	. . .
l755ch:
	jp 003c5h		;755c	c3 c5 03	. . .
l755fh:
	jp 0040ch		;755f	c3 0c 04	. . .
	ld (bc),a		;7562	02		.
	ld d,014h		;7563	16 14		. .
	rra			;7565	1f		.
	nop			;7566	00		.
	nop			;7567	00		.
l7568h:
	call 00330h		;7568	cd 30 03	. 0 .
	jr l755ch		;756b	18 ef		. .
	call 00330h		;756d	cd 30 03	. 0 .
	jr l755fh		;7570	18 ed		. .
l7572h:
	ld a,0c3h		;7572	3e c3		> .
	ld (00000h),a		;7574	32 00 00	2 . .
	ld hl,0003dh		;7577	21 3d 00	! = .
	ld (00001h),hl		;757a	22 01 00	" . .
	ld (0231bh),bc		;757d	ed 43 1b 23	. C . #
	jr l7568h		;7581	18 e5		. .
	ld (iy+052h),0ffh	;7583	fd 36 52 ff	. 6 R .
	set 3,(iy+001h)		;7587	fd cb 01 de	. . . .
	xor a			;758b	af		.
	ld (iy+002h),a		;758c	fd 77 02	. w .
	ld (05c41h),a		;758f	32 41 5c	2 A \
	ld (00281h),a		;7592	32 81 02	2 . .
	ld a,002h		;7595	3e 02		> .
	ld hl,05c88h		;7597	21 88 5c	! . \
	ld (hl),033h		;759a	36 33		6 3
	cp 002h			;759c	fe 02		. .
	jp nz,01601h		;759e	c2 01 16	. . .
	ld hl,00078h		;75a1	21 78 00	! x .
	ld (05c51h),hl		;75a4	22 51 5c	" Q \
	ret			;75a7	c9		.
	defb 0ddh,000h,0c4h ;illegal sequence	;75a8	dd 00 c4	. . .
	dec d			;75ab	15		.
	ld d,e			;75ac	53		S
	rlca			;75ad	07		.
	inc hl			;75ae	23		#
	ld c,c			;75af	49		I
	adc a,c			;75b0	89		.
	ld d,h			;75b1	54		T
	jp nz,06185h		;75b2	c2 85 61	. . a
	ld (hl),b		;75b5	70		p
	rlca			;75b6	07		.
	ld h,d			;75b7	62		b
	ld h,d			;75b8	62		b
	ld h,c			;75b9	61		a
	ld h,d			;75ba	62		b
	ld h,c			;75bb	61		a
	ld h,c			;75bc	61		a
	dec h			;75bd	25		%
	ld d,d			;75be	52		R
	dec c			;75bf	0d		.
	add hl,de		;75c0	19		.
	rlca			;75c1	07		.
	dec c			;75c2	0d		.
	dec c			;75c3	0d		.
	ld h,c			;75c4	61		a
	dec c			;75c5	0d		.
	dec c			;75c6	0d		.
	inc hl			;75c7	23		#
	ld b,e			;75c8	43		C
	ld h,c			;75c9	61		a
	ld h,d			;75ca	62		b
	ld h,c			;75cb	61		a
	ld sp,00d85h		;75cc	31 85 0d	1 . .
	dec c			;75cf	0d		.
	rlca			;75d0	07		.
	add hl,de		;75d1	19		.
	dec c			;75d2	0d		.
	dec c			;75d3	0d		.
	dec d			;75d4	15		.
	dec c			;75d5	0d		.
	ld d,d			;75d6	52		R
	dec c			;75d7	0d		.
	ld sp,08907h		;75d8	31 07 89	1 . .
	dec c			;75db	0d		.
	dec c			;75dc	0d		.
	dec c			;75dd	0d		.
	dec h			;75de	25		%
	add hl,de		;75df	19		.
	dec c			;75e0	0d		.
	inc hl			;75e1	23		#
	dec c			;75e2	0d		.
	adc a,c			;75e3	89		.
	adc a,c			;75e4	89		.
	adc a,c			;75e5	89		.
	add hl,hl		;75e6	29		)
	ld b,e			;75e7	43		C
	ld (hl),b		;75e8	70		p
	add a,e			;75e9	83		.
	rlca			;75ea	07		.
	add a,e			;75eb	83		.
	rlca			;75ec	07		.
	ld c,c			;75ed	49		I
	dec bc			;75ee	0b		.
	ld b,l			;75ef	45		E
	ld b,e			;75f0	43		C
	dec bc			;75f1	0b		.
	dec bc			;75f2	0b		.
	ld h,c			;75f3	61		a
	dec bc			;75f4	0b		.
	dec bc			;75f5	0b		.
	ld b,e			;75f6	43		C
	ld c,c			;75f7	49		I
	ld b,e			;75f8	43		C
	ld h,c			;75f9	61		a
	add a,e			;75fa	83		.
	dec bc			;75fb	0b		.
	dec bc			;75fc	0b		.
	dec bc			;75fd	0b		.
	add hl,de		;75fe	19		.
	ld b,e			;75ff	43		C
	dec bc			;7600	0b		.
	ld b,e			;7601	43		C
	dec bc			;7602	0b		.
	add a,e			;7603	83		.
	add a,e			;7604	83		.
	add a,e			;7605	83		.
	dec bc			;7606	0b		.
	ld b,e			;7607	43		C
	ld d,d			;7608	52		R
	ld sp,0c146h		;7609	31 46 c1	1 F .
	inc e			;760c	1c		.
	cp 020h			;760d	fe 20		.  
	jr nc,l765ah		;760f	30 49		0 I
	ld hl,05c88h		;7611	21 88 5c	! . \
	cp 008h			;7614	fe 08		. .
	jr nz,l7622h		;7616	20 0a		  .
	inc (hl)		;7618	34		4
	ld a,(hl)		;7619	7e		~
	cp 034h			;761a	fe 34		. 4
	ret nz			;761c	c0		.
	ld (hl),001h		;761d	36 01		6 .
	inc hl			;761f	23		#
	inc (hl)		;7620	34		4
	ret			;7621	c9		.
l7622h:
	cp 00dh			;7622	fe 0d		. .
	jp z,001dah		;7624	ca da 01	. . .
	cp 014h			;7627	fe 14		. .
	ld de,00111h		;7629	11 11 01	. . .
	jr z,l7637h		;762c	28 09		( .
	cp 016h			;762e	fe 16		. .
	ret nc			;7630	d0		.
	cp 010h			;7631	fe 10		. .
	ret c			;7633	d8		.
	ld de,0011dh		;7634	11 1d 01	. . .
l7637h:
	ld (05c0eh),a		;7637	32 0e 5c	2 . \
l763ah:
	ld hl,(05c51h)		;763a	2a 51 5c	* Q \
	ld (hl),e		;763d	73		s
	inc hl			;763e	23		#
	ld (hl),d		;763f	72		r
	ret			;7640	c9		.
	ld e,a			;7641	5f		_
	or a			;7642	b7		.
	ld a,028h		;7643	3e 28		> (
	jr z,l7649h		;7645	28 02		( .
	ld a,020h		;7647	3e 20		>  
l7649h:
	ld (00205h),a		;7649	32 05 02	2 . .
	ld a,e			;764c	7b		{
	call 00a8dh		;764d	cd 8d 0a	. . .
	ld de,000ddh		;7650	11 dd 00	. . .
	jr l763ah		;7653	18 e5		. .
	ld de,009f4h		;7655	11 f4 09	. . .
	jr l763ah		;7658	18 e0		. .
l765ah:
	cp 080h			;765a	fe 80		. .
	ret nc			;765c	d0		.
	ld c,a			;765d	4f		O
	ld hl,05c89h		;765e	21 89 5c	! . \
	ld b,(hl)		;7661	46		F
	ld a,019h		;7662	3e 19		> .
	sub b			;7664	90		.
	ld b,a			;7665	47		G
	push af			;7666	f5		.
	ld de,00020h		;7667	11 20 00	.   .
	ld hl,0ffe0h		;766a	21 e0 ff	! . .
l766dh:
	add hl,de		;766d	19		.
	djnz l766dh		;766e	10 fd		. .
	ld a,(05c88h)		;7670	3a 88 5c	: . \
	ld b,a			;7673	47		G
	ld a,034h		;7674	3e 34		> 4
	sub b			;7676	90		.
	ld b,a			;7677	47		G
	ld d,005h		;7678	16 05		. .
	ld a,0fbh		;767a	3e fb		> .
l767ch:
	add a,d			;767c	82		.
	djnz l767ch		;767d	10 fd		. .
	ld e,a			;767f	5f		_
	and 007h		;7680	e6 07		. .
	ld (0022fh),a		;7682	32 2f 02	2 / .
	srl e			;7685	cb 3b		. ;
	srl e			;7687	cb 3b		. ;
	srl e			;7689	cb 3b		. ;
	ld d,040h		;768b	16 40		. @
	add hl,de		;768d	19		.
	ld de,00700h		;768e	11 00 07	. . .
	pop af			;7691	f1		.
	cp 009h			;7692	fe 09		. .
	jr c,l769ch		;7694	38 06		8 .
	add hl,de		;7696	19		.
	cp 011h			;7697	fe 11		. .
	jr c,l769ch		;7699	38 01		8 .
	add hl,de		;769b	19		.
l769ch:
	ld (05c84h),hl		;769c	22 84 5c	" . \
	ld a,c			;769f	79		y
	sub 020h		;76a0	d6 20		.  
	ld l,a			;76a2	6f		o
	ld h,000h		;76a3	26 00		& .
	push hl			;76a5	e5		.
	add hl,hl		;76a6	29		)
	add hl,hl		;76a7	29		)
	add hl,hl		;76a8	29		)
	ld de,03d00h		;76a9	11 00 3d	. . =
	add hl,de		;76ac	19		.
	ld a,c			;76ad	79		y
	ld bc,00008h		;76ae	01 08 00	. . .
	ld de,00227h		;76b1	11 27 02	. ' .
	ldir			;76b4	ed b0		. .
	cp 059h			;76b6	fe 59		. Y
	jr nz,l76bfh		;76b8	20 05		  .
	ld hl,0022ah		;76ba	21 2a 02	! * .
	ld (hl),038h		;76bd	36 38		6 8
l76bfh:
	ld c,a			;76bf	4f		O
	pop hl			;76c0	e1		.
	ld de,0007dh		;76c1	11 7d 00	. } .
	add hl,de		;76c4	19		.
	ld e,(hl)		;76c5	5e		^
	ld hl,(05c84h)		;76c6	2a 84 5c	* . \
	ld a,(0022fh)		;76c9	3a 2f 02	: / .
	or a			;76cc	b7		.
	jr z,l76d8h		;76cd	28 09		( .
	ld b,a			;76cf	47		G
	push de			;76d0	d5		.
	ld de,00100h		;76d1	11 00 01	. . .
	call 001e5h		;76d4	cd e5 01	. . .
	pop de			;76d7	d1		.
l76d8h:
	ld b,a			;76d8	47		G
	add a,005h		;76d9	c6 05		. .
	push af			;76db	f5		.
	cp 009h			;76dc	fe 09		. .
	jr nc,l76e7h		;76de	30 07		0 .
	ld b,005h		;76e0	06 05		. .
	ld c,e			;76e2	4b		K
	ld d,008h		;76e3	16 08		. .
	jr l76f6h		;76e5	18 0f		. .
l76e7h:
	sub 008h		;76e7	d6 08		. .
	ld d,a			;76e9	57		W
	ld a,008h		;76ea	3e 08		> .
	sub b			;76ec	90		.
	ld b,a			;76ed	47		G
	ld c,e			;76ee	4b		K
	call 001f3h		;76ef	cd f3 01	. . .
	inc l			;76f2	2c		,
	ld b,d			;76f3	42		B
	ld d,010h		;76f4	16 10		. .
l76f6h:
	call 001f3h		;76f6	cd f3 01	. . .
	ld a,d			;76f9	7a		z
	pop de			;76fa	d1		.
	sub d			;76fb	92		.
	jr z,l7705h		;76fc	28 07		( .
	ld b,a			;76fe	47		G
	ld de,00100h		;76ff	11 00 01	. . .
	call 001e5h		;7702	cd e5 01	. . .
l7705h:
	ld hl,05c88h		;7705	21 88 5c	! . \
	dec (hl)		;7708	35		5
	ret nz			;7709	c0		.
	ld (hl),033h		;770a	36 33		6 3
	inc hl			;770c	23		#
	dec (hl)		;770d	35		5
	ret nz			;770e	c0		.
	inc (hl)		;770f	34		4
	ld b,017h		;7710	06 17		. .
	jp 00e00h		;7712	c3 00 0e	. . .
l7715h:
	push bc			;7715	c5		.
	ld b,008h		;7716	06 08		. .
	push hl			;7718	e5		.
l7719h:
	rlc (hl)		;7719	cb 06		. .
	add hl,de		;771b	19		.
	djnz l7719h		;771c	10 fb		. .
	pop hl			;771e	e1		.
	pop bc			;771f	c1		.
	djnz l7715h		;7720	10 f3		. .
	ret			;7722	c9		.
	push de			;7723	d5		.
l7724h:
	push bc			;7724	c5		.
l7725h:
	ld de,00227h		;7725	11 27 02	. ' .
	bit 7,c			;7728	cb 79		. y
	push af			;772a	f5		.
	jr nz,l7743h		;772b	20 16		  .
	push hl			;772d	e5		.
	ld b,008h		;772e	06 08		. .
l7730h:
	ld a,(de)		;7730	1a		.
	bit 7,a			;7731	cb 7f		. .
	res 7,(hl)		;7733	cb be		. .
	jr z,l7739h		;7735	28 02		( .
	set 7,(hl)		;7737	cb fe		. .
l7739h:
	inc de			;7739	13		.
	rlc (hl)		;773a	cb 06		. .
	inc h			;773c	24		$
	djnz l7730h		;773d	10 f1		. .
	pop hl			;773f	e1		.
	ld de,00227h		;7740	11 27 02	. ' .
l7743h:
	rlc c			;7743	cb 01		. .
	ld b,008h		;7745	06 08		. .
	ex de,hl		;7747	eb		.
l7748h:
	rlc (hl)		;7748	cb 06		. .
	inc hl			;774a	23		#
	djnz l7748h		;774b	10 fb		. .
	ex de,hl		;774d	eb		.
	pop af			;774e	f1		.
	jr nz,l7725h		;774f	20 d4		  .
	pop de			;7751	d1		.
	ld b,d			;7752	42		B
	djnz l7724h		;7753	10 cf		. .
	pop de			;7755	d1		.
	ret			;7756	c9		.
	nop			;7757	00		.
	nop			;7758	00		.
	nop			;7759	00		.
	nop			;775a	00		.
	nop			;775b	00		.
	nop			;775c	00		.
	nop			;775d	00		.
	nop			;775e	00		.
	nop			;775f	00		.
	push ix			;7760	dd e5		. .
	push bc			;7762	c5		.
	push de			;7763	d5		.
	push af			;7764	f5		.
	push hl			;7765	e5		.
	push af			;7766	f5		.
	ld a,(00281h)		;7767	3a 81 02	: . .
	inc a			;776a	3c		<
	inc a			;776b	3c		<
	call 0006ch		;776c	cd 6c 00	. l .
	pop af			;776f	f1		.
	cp 010h			;7770	fe 10		. .
	jr nz,l7786h		;7772	20 12		  .
	ld a,(00281h)		;7774	3a 81 02	: . .
	xor 001h		;7777	ee 01		. .
	ld (00281h),a		;7779	32 81 02	2 . .
	ld a,002h		;777c	3e 02		> .
	jr z,l7781h		;777e	28 01		( .
	inc a			;7780	3c		<
l7781h:
	call 0006ch		;7781	cd 6c 00	. l .
	jr l77a6h		;7784	18 20		.  
l7786h:
	cp 00ch			;7786	fe 0c		. .
	jr nz,l7799h		;7788	20 0f		  .
	ld hl,04000h		;778a	21 00 40	! . @
	ld (hl),000h		;778d	36 00		6 .
	ld de,04001h		;778f	11 01 40	. . @
	ld bc,01800h		;7792	01 00 18	. . .
	ldir			;7795	ed b0		. .
	jr l77a6h		;7797	18 0d		. .
l7799h:
	exx			;7799	d9		.
	cp 008h			;779a	fe 08		. .
	jr nz,l77a4h		;779c	20 06		  .
	rst 10h			;779e	d7		.
	ld a,020h		;779f	3e 20		>  
	rst 10h			;77a1	d7		.
	ld a,008h		;77a2	3e 08		> .
l77a4h:
	rst 10h			;77a4	d7		.
	exx			;77a5	d9		.
l77a6h:
	ld (iy+052h),0ffh	;77a6	fd 36 52 ff	. 6 R .
	pop hl			;77aa	e1		.
	pop af			;77ab	f1		.
	pop de			;77ac	d1		.
	pop bc			;77ad	c1		.
	pop ix			;77ae	dd e1		. .
	ret			;77b0	c9		.
	nop			;77b1	00		.
	push ix			;77b2	dd e5		. .
	push bc			;77b4	c5		.
	push de			;77b5	d5		.
	push hl			;77b6	e5		.
l77b7h:
	exx			;77b7	d9		.
	push af			;77b8	f5		.
	ld a,014h		;77b9	3e 14		> .
	rst 10h			;77bb	d7		.
	ld a,001h		;77bc	3e 01		> .
	rst 10h			;77be	d7		.
	pop af			;77bf	f1		.
	push af			;77c0	f5		.
	cp 05fh			;77c1	fe 5f		. _
	jr nz,l77cfh		;77c3	20 0a		  .
	bit 3,(iy+030h)		;77c5	fd cb 30 5e	. . 0 ^
	ld a,04ch		;77c9	3e 4c		> L
	jr z,l77cfh		;77cb	28 02		( .
	ld a,043h		;77cd	3e 43		> C
l77cfh:
	rst 10h			;77cf	d7		.
	ld a,014h		;77d0	3e 14		> .
	rst 10h			;77d2	d7		.
	xor a			;77d3	af		.
	rst 10h			;77d4	d7		.
	exx			;77d5	d9		.
	res 5,(iy+001h)		;77d6	fd cb 01 ae	. . . .
l77dah:
	bit 5,(iy+001h)		;77da	fd cb 01 6e	. . . n
	jr z,l77dah		;77de	28 fa		( .
	call 003c0h		;77e0	cd c0 03	. . .
	ld a,(05c08h)		;77e3	3a 08 5c	: . \
	ld hl,0030ch		;77e6	21 0c 03	! . .
	ld b,004h		;77e9	06 04		. .
l77ebh:
	cp (hl)			;77eb	be		.
	jr z,l7820h		;77ec	28 32		( 2
	inc hl			;77ee	23		#
	djnz l77ebh		;77ef	10 fa		. .
	cp 006h			;77f1	fe 06		. .
	jr z,l7819h		;77f3	28 24		( $
	ld b,00bh		;77f5	06 0b		. .
	ld hl,002f6h		;77f7	21 f6 02	! . .
l77fah:
	cp (hl)			;77fa	be		.
	inc hl			;77fb	23		#
	jr z,l7823h		;77fc	28 25		( %
	inc hl			;77fe	23		#
	djnz l77fah		;77ff	10 f9		. .
l7801h:
	pop hl			;7801	e1		.
	push ix			;7802	dd e5		. .
	push af			;7804	f5		.
	ld d,000h		;7805	16 00		. .
	ld e,(iy-001h)		;7807	fd 5e ff	. ^ .
	ld hl,000c8h		;780a	21 c8 00	! . .
	call 003b5h		;780d	cd b5 03	. . .
	pop af			;7810	f1		.
	pop ix			;7811	dd e1		. .
	pop hl			;7813	e1		.
	pop de			;7814	d1		.
	pop bc			;7815	c1		.
	pop ix			;7816	dd e1		. .
	ret			;7818	c9		.
l7819h:
	ld hl,05c6ah		;7819	21 6a 5c	! j \
	ld a,008h		;781c	3e 08		> .
	xor (hl)		;781e	ae		.
	ld (hl),a		;781f	77		w
l7820h:
	pop af			;7820	f1		.
	jr l77b7h		;7821	18 94		. .
l7823h:
	ld a,(hl)		;7823	7e		~
	jr l7801h		;7824	18 db		. .
	ex af,af'		;7826	08		.
	jr l7835h		;7827	18 0c		. .
	ex af,af'		;7829	08		.
	dec bc			;782a	0b		.
	inc c			;782b	0c		.
	rlca			;782c	07		.
	inc bc			;782d	03		.
	add a,05bh		;782e	c6 5b		. [
	push bc			;7830	c5		.
	ld e,l			;7831	5d		]
	jp po,0c37eh		;7832	e2 7e c3	. ~ .
l7835h:
	ld a,h			;7835	7c		|
	call 0cc5ch		;7836	cd 5c cc	. \ .
	ld a,e			;7839	7b		{
	bit 7,l			;783a	cb 7d		. }
	rst 0			;783c	c7		.
	ret z			;783d	c8		.
	ret			;783e	c9		.
	xor h			;783f	ac		.
	ld a,07fh		;7840	3e 7f		> .
	in a,(0feh)		;7842	db fe		. .
	rra			;7844	1f		.
	ccf			;7845	3f		?
	ret			;7846	c9		.
	ld a,05fh		;7847	3e 5f		> _
	jp 00020h		;7849	c3 20 00	.   .
	ld hl,(0033ch)		;784c	2a 3c 03	* < .
	exx			;784f	d9		.
	ld hl,05c88h		;7850	21 88 5c	! . \
	ld (hl),01fh		;7853	36 1f		6 .
	inc hl			;7855	23		#
	ld (hl),015h		;7856	36 15		6 .
	ld a,002h		;7858	3e 02		> .
	call 01601h		;785a	cd 01 16	. . .
	ei			;785d	fb		.
	rst 8			;785e	cf		.
	rst 38h			;785f	ff		.
	exx			;7860	d9		.
	ld (0033ch),hl		;7861	22 3c 03	" < .
	ld hl,05c88h		;7864	21 88 5c	! . \
	ld (hl),033h		;7867	36 33		6 3
	jp 00d6bh		;7869	c3 6b 0d	. k .
	nop			;786c	00		.
	nop			;786d	00		.
	push ix			;786e	dd e5		. .
	push hl			;7870	e5		.
	ld b,005h		;7871	06 05		. .
	ld ix,0038ch		;7873	dd 21 8c 03	. ! . .
l7877h:
	ld e,(ix+000h)		;7877	dd 5e 00	. ^ .
	ld d,(ix+001h)		;787a	dd 56 01	. V .
	or a			;787d	b7		.
	sbc hl,de		;787e	ed 52		. R
	jr nc,l788ah		;7880	30 08		0 .
	add hl,de		;7882	19		.
	inc ix			;7883	dd 23		. #
	inc ix			;7885	dd 23		. #
	djnz l7877h		;7887	10 ee		. .
	inc b			;7889	04		.
l788ah:
	ld l,a			;788a	6f		o
	ld a,b			;788b	78		x
	call 00396h		;788c	cd 96 03	. . .
	pop hl			;788f	e1		.
	ld ix,0038ch		;7890	dd 21 8c 03	. ! . .
	ld bc,00530h		;7894	01 30 05	. 0 .
l7897h:
	ld a,030h		;7897	3e 30		> 0
	ld e,(ix+000h)		;7899	dd 5e 00	. ^ .
	ld d,(ix+001h)		;789c	dd 56 01	. V .
l789fh:
	or a			;789f	b7		.
	sbc hl,de		;78a0	ed 52		. R
	jr c,l78a7h		;78a2	38 03		8 .
	inc a			;78a4	3c		<
	jr l789fh		;78a5	18 f8		. .
l78a7h:
	add hl,de		;78a7	19		.
	cp c			;78a8	b9		.
	jr z,l78afh		;78a9	28 04		( .
	call 0001dh		;78ab	cd 1d 00	. . .
	dec c			;78ae	0d		.
l78afh:
	inc ix			;78af	dd 23		. #
	inc ix			;78b1	dd 23		. #
	djnz l7897h		;78b3	10 e2		. .
	pop ix			;78b5	dd e1		. .
	cp c			;78b7	b9		.
	ret nz			;78b8	c0		.
	jp 0001dh		;78b9	c3 1d 00	. . .
	djnz l78e5h		;78bc	10 27		. '
	ret pe			;78be	e8		.
	inc bc			;78bf	03		.
	ld h,h			;78c0	64		d
	nop			;78c1	00		.
	ld a,(bc)		;78c2	0a		.
	nop			;78c3	00		.
	ld bc,l9500h		;78c4	01 00 95	. . .
	ret nc			;78c7	d0		.
	neg			;78c8	ed 44		. D
	ld b,a			;78ca	47		G
l78cbh:
	call 003bbh		;78cb	cd bb 03	. . .
	djnz l78cbh		;78ce	10 fb		. .
	ret			;78d0	c9		.
	ld b,003h		;78d1	06 03		. .
l78d3h:
	call 003b7h		;78d3	cd b7 03	. . .
	djnz l78d3h		;78d6	10 fb		. .
	ld hl,(022fch)		;78d8	2a fc 22	* . "
	ld a,l			;78db	7d		}
	or h			;78dc	b4		.
	ret z			;78dd	c8		.
l78deh:
	ld a,(hl)		;78de	7e		~
	inc hl			;78df	23		#
	call 0001dh		;78e0	cd 1d 00	. . .
	cp 00dh			;78e3	fe 0d		. .
l78e5h:
	jr nz,l78deh		;78e5	20 f7		  .
	ld a,00dh		;78e7	3e 0d		> .
	jr l78edh		;78e9	18 02		. .
	ld a,020h		;78eb	3e 20		>  
l78edh:
	jp 0001dh		;78ed	c3 1d 00	. . .
	ld a,008h		;78f0	3e 08		> .
	jp 0001dh		;78f2	c3 1d 00	. . .
	ld sp,0264eh		;78f5	31 4e 26	1 N &
	call 00da1h		;78f8	cd a1 0d	. . .
	set 3,(iy+030h)		;78fb	fd cb 30 de	. . 0 .
	ld hl,022ach		;78ff	21 ac 22	! . "
	ld b,028h		;7902	06 28		. (
l7904h:
	ld (hl),00dh		;7904	36 0d		6 .
	inc hl			;7906	23		#
	djnz l7904h		;7907	10 fb		. .
	ld a,0c9h		;7909	3e c9		> .
	ld (022f6h),a		;790b	32 f6 22	2 . "
	ld a,02ch		;790e	3e 2c		> ,
	ld (022e2h),a		;7910	32 e2 22	2 . "
	ld a,00fh		;7913	3e 0f		> .
	ld (022f3h),a		;7915	32 f3 22	2 . "
	ld hl,0000ah		;7918	21 0a 00	! . .
	ld (022d6h),hl		;791b	22 d6 22	" . "
	ld (022d8h),hl		;791e	22 d8 22	" . "
	ld hl,(05c7bh)		;7921	2a 7b 5c	* { \
	ld (0233dh),hl		;7924	22 3d 23	" = #
	ld hl,02b3ah		;7927	21 3a 2b	! : +
	ld (022e8h),hl		;792a	22 e8 22	" . "
	ld (00036h),hl		;792d	22 36 00	" 6 .
	ld (022ech),hl		;7930	22 ec 22	" . "
	ld hl,02555h		;7933	21 55 25	! U %
	call 00455h		;7936	cd 55 04	. U .
	call 00e39h		;7939	cd 39 0e	. 9 .
	ld sp,0264eh		;793c	31 4e 26	1 N &
	call 00026h		;793f	cd 26 00	. & .
	call 003b7h		;7942	cd b7 03	. . .
l7945h:
	ld sp,0264eh		;7945	31 4e 26	1 N &
	call 00da1h		;7948	cd a1 0d	. . .
	ld h,a			;794b	67		g
	ld l,a			;794c	6f		o
	ld (022dah),hl		;794d	22 da 22	" . "
	ld (022dch),hl		;7950	22 dc 22	" . "
	ld (0233fh),hl		;7953	22 3f 23	" ? #
	call 00523h		;7956	cd 23 05	. # .
	jr nz,l797dh		;7959	20 22		  "
	ld hl,00415h		;795b	21 15 04	! . .
	push hl			;795e	e5		.
	ld a,(022d5h)		;795f	3a d5 22	: . "
	ld hl,0213fh		;7962	21 3f 21	! ? !
	ld b,017h		;7965	06 17		. .
l7967h:
	cp (hl)			;7967	be		.
	jr z,l7977h		;7968	28 0d		( .
	sub 020h		;796a	d6 20		.  
	cp (hl)			;796c	be		.
	jr z,l7977h		;796d	28 08		( .
	add a,020h		;796f	c6 20		.  
	inc hl			;7971	23		#
	inc hl			;7972	23		#
	inc hl			;7973	23		#
	djnz l7967h		;7974	10 f1		. .
	ret			;7976	c9		.
l7977h:
	inc hl			;7977	23		#
	ld e,(hl)		;7978	5e		^
	inc hl			;7979	23		#
	ld d,(hl)		;797a	56		V
	ex de,hl		;797b	eb		.
	jp (hl)			;797c	e9		.
l797dh:
	ld hl,02284h		;797d	21 84 22	! . "
	call 00455h		;7980	cd 55 04	. U .
	jr l7945h		;7983	18 c0		. .
l7985h:
	ld a,(hl)		;7985	7e		~
	or a			;7986	b7		.
	ret z			;7987	c8		.
	call 0001dh		;7988	cd 1d 00	. . .
	inc hl			;798b	23		#
	jr l7985h		;798c	18 f7		. .
	ld a,(022e3h)		;798e	3a e3 22	: . "
	ld b,a			;7991	47		G
	ld a,026h		;7992	3e 26		> &
	cp b			;7994	b8		.
	jr c,l79a9h		;7995	38 12		8 .
	ld a,021h		;7997	3e 21		> !
	cp b			;7999	b8		.
	jr c,l79a9h		;799a	38 0d		8 .
	ld a,018h		;799c	3e 18		> .
	cp b			;799e	b8		.
	jr c,l79a9h		;799f	38 08		8 .
	ld a,b			;79a1	78		x
	dec a			;79a2	3d		=
	ld a,033h		;79a3	3e 33		> 3
	jr z,l79a9h		;79a5	28 02		( .
	ld a,001h		;79a7	3e 01		> .
l79a9h:
	ld (0231ah),a		;79a9	32 1a 23	2 . #
	scf			;79ac	37		7
	ret			;79ad	c9		.
	ld d,006h		;79ae	16 06		. .
	ld hl,0258ch		;79b0	21 8c 25	! . %
	ld c,040h		;79b3	0e 40		. @
	ld e,000h		;79b5	1e 00		. .
l79b7h:
	ld a,033h		;79b7	3e 33		> 3
	sub d			;79b9	92		.
	ld b,a			;79ba	47		G
l79bbh:
	bit 1,(ix+000h)		;79bb	dd cb 00 4e	. . . N
	ret nz			;79bf	c0		.
l79c0h:
	bit 4,(ix+000h)		;79c0	dd cb 00 66	. . . f
	jr z,l79d2h		;79c4	28 0c		( .
	ld a,(0231ah)		;79c6	3a 1a 23	: . #
	cp b			;79c9	b8		.
	ld a,020h		;79ca	3e 20		>  
	jr c,l79ddh		;79cc	38 0f		8 .
	res 4,(ix+000h)		;79ce	dd cb 00 a6	. . . .
l79d2h:
	bit 0,(ix+000h)		;79d2	dd cb 00 46	. . . F
	jr z,l79dah		;79d6	28 02		( .
	ld a,(hl)		;79d8	7e		~
	inc hl			;79d9	23		#
l79dah:
	call z,00317h		;79da	cc 17 03	. . .
l79ddh:
	cp 009h			;79dd	fe 09		. .
	jr nz,l79eah		;79df	20 09		  .
	set 4,(ix+000h)		;79e1	dd cb 00 e6	. . . .
	call 00462h		;79e5	cd 62 04	. b .
	jr l79c0h		;79e8	18 d6		. .
l79eah:
	cp 018h			;79ea	fe 18		. .
	jr nz,l79f9h		;79ec	20 0b		  .
	inc e			;79ee	1c		.
	dec e			;79ef	1d		.
	jr z,l79bbh		;79f0	28 c9		( .
l79f2h:
	call 00507h		;79f2	cd 07 05	. . .
	jr nz,l79f2h		;79f5	20 fb		  .
l79f7h:
	jr l79b7h		;79f7	18 be		. .
l79f9h:
	cp 003h			;79f9	fe 03		. .
	jp z,003b7h		;79fb	ca b7 03	. . .
	cp 008h			;79fe	fe 08		. .
	jr nz,l7a09h		;7a00	20 07		  .
l7a02h:
	inc e			;7a02	1c		.
	dec e			;7a03	1d		.
	call nz,00507h		;7a04	c4 07 05	. . .
	jr l79bbh		;7a07	18 b2		. .
l7a09h:
	cp 00dh			;7a09	fe 0d		. .
	jr z,l7a12h		;7a0b	28 05		( .
	cp 020h			;7a0d	fe 20		.  
	jr c,l79bbh		;7a0f	38 aa		8 .
	ccf			;7a11	3f		?
l7a12h:
	bit 0,(ix+000h)		;7a12	dd cb 00 46	. . . F
	jr nz,l7a1ah		;7a16	20 02		  .
	ld (hl),a		;7a18	77		w
	inc hl			;7a19	23		#
l7a1ah:
	push af			;7a1a	f5		.
	call 0001dh		;7a1b	cd 1d 00	. . .
	pop af			;7a1e	f1		.
	ccf			;7a1f	3f		?
	ret c			;7a20	d8		.
	inc e			;7a21	1c		.
	dec b			;7a22	05		.
	dec c			;7a23	0d		.
	jr z,l7a02h		;7a24	28 dc		( .
	inc b			;7a26	04		.
	dec b			;7a27	05		.
	jr nz,l79bbh		;7a28	20 91		  .
	dec (ix+001h)		;7a2a	dd 35 01	. 5 .
	ld b,d			;7a2d	42		B
	inc b			;7a2e	04		.
	dec b			;7a2f	05		.
	jr z,l79f7h		;7a30	28 c5		( .
	call 0039bh		;7a32	cd 9b 03	. . .
	jr l79f7h		;7a35	18 c0		. .
	ld a,033h		;7a37	3e 33		> 3
	sub d			;7a39	92		.
	cp b			;7a3a	b8		.
	jr nz,l7a47h		;7a3b	20 0a		  .
	ld b,d			;7a3d	42		B
	inc b			;7a3e	04		.
	dec b			;7a3f	05		.
	jr z,l7a47h		;7a40	28 05		( .
l7a42h:
	call 003c0h		;7a42	cd c0 03	. . .
	djnz l7a42h		;7a45	10 fb		. .
l7a47h:
	inc b			;7a47	04		.
	call 003c0h		;7a48	cd c0 03	. . .
	res 4,(ix+000h)		;7a4b	dd cb 00 a6	. . . .
	dec hl			;7a4f	2b		+
	inc c			;7a50	0c		.
	dec e			;7a51	1d		.
	ret			;7a52	c9		.
	ld a,03eh		;7a53	3e 3e		> >
	call 0001dh		;7a55	cd 1d 00	. . .
	ld d,001h		;7a58	16 01		. .
	call 00480h		;7a5a	cd 80 04	. . .
	ld hl,0258ch		;7a5d	21 8c 25	! . %
	call 005c7h		;7a60	cd c7 05	. . .
	ld (022d5h),a		;7a63	32 d5 22	2 . "
	ret z			;7a66	c8		.
	call 00602h		;7a67	cd 02 06	. . .
	jr nc,l7a8ah		;7a6a	30 1e		0 .
	call 005deh		;7a6c	cd de 05	. . .
	ret nz			;7a6f	c0		.
	ld (022deh),hl		;7a70	22 de 22	" . "
	inc de			;7a73	13		.
	ld a,(de)		;7a74	1a		.
	cp 00dh			;7a75	fe 0d		. .
	jr nz,l7a84h		;7a77	20 0b		  .
	ld (022dah),hl		;7a79	22 da 22	" . "
	ld (022dch),hl		;7a7c	22 dc 22	" . "
	call 00633h		;7a7f	cd 33 06	. 3 .
	xor a			;7a82	af		.
	ret			;7a83	c9		.
l7a84h:
	inc de			;7a84	13		.
	call 00ca5h		;7a85	cd a5 0c	. . .
	xor a			;7a88	af		.
	ret			;7a89	c9		.
l7a8ah:
	call 005c6h		;7a8a	cd c6 05	. . .
	ret z			;7a8d	c8		.
	cp b			;7a8e	b8		.
	jr z,l7aa0h		;7a8f	28 0f		( .
	call 005deh		;7a91	cd de 05	. . .
	ret nz			;7a94	c0		.
	ld (022d6h),hl		;7a95	22 d6 22	" . "
	ld (022dah),hl		;7a98	22 da 22	" . "
	ex de,hl		;7a9b	eb		.
l7a9ch:
	call 005c6h		;7a9c	cd c6 05	. . .
	ret z			;7a9f	c8		.
l7aa0h:
	cp b			;7aa0	b8		.
	jr nz,l7a9ch		;7aa1	20 f9		  .
	call 005c6h		;7aa3	cd c6 05	. . .
	ret z			;7aa6	c8		.
	cp b			;7aa7	b8		.
	jr z,l7ab9h		;7aa8	28 0f		( .
	call 005deh		;7aaa	cd de 05	. . .
	ret nz			;7aad	c0		.
	ld (022d8h),hl		;7aae	22 d8 22	" . "
	ld (022dch),hl		;7ab1	22 dc 22	" . "
	ex de,hl		;7ab4	eb		.
l7ab5h:
	call 005c6h		;7ab5	cd c6 05	. . .
	ret z			;7ab8	c8		.
l7ab9h:
	cp b			;7ab9	b8		.
	jr nz,l7ab5h		;7aba	20 f9		  .
	ld c,b			;7abc	48		H
	inc hl			;7abd	23		#
	ld a,(hl)		;7abe	7e		~
	cp c			;7abf	b9		.
	jr z,l7ad7h		;7ac0	28 15		( .
	ld de,022ach		;7ac2	11 ac 22	. . "
	ld (0233fh),de		;7ac5	ed 53 3f 23	. S ? #
	call 005abh		;7ac9	cd ab 05	. . .
	jr c,l7ad8h		;7acc	38 0a		8 .
	ret z			;7ace	c8		.
	dec hl			;7acf	2b		+
l7ad0h:
	call 005c6h		;7ad0	cd c6 05	. . .
	ret z			;7ad3	c8		.
	cp b			;7ad4	b8		.
	jr nz,l7ad0h		;7ad5	20 f9		  .
l7ad7h:
	inc hl			;7ad7	23		#
l7ad8h:
	ld de,022c0h		;7ad8	11 c0 22	. . "
	ld b,014h		;7adb	06 14		. .
	ld a,00dh		;7add	3e 0d		> .
	push de			;7adf	d5		.
	push bc			;7ae0	c5		.
l7ae1h:
	ld (de),a		;7ae1	12		.
	inc de			;7ae2	13		.
	djnz l7ae1h		;7ae3	10 fc		. .
	pop bc			;7ae5	c1		.
	pop de			;7ae6	d1		.
l7ae7h:
	ld a,(hl)		;7ae7	7e		~
	inc hl			;7ae8	23		#
	cp c			;7ae9	b9		.
	jr nz,l7aeeh		;7aea	20 02		  .
	scf			;7aec	37		7
	ret			;7aed	c9		.
l7aeeh:
	cp 00dh			;7aee	fe 0d		. .
	ret z			;7af0	c8		.
	ld (de),a		;7af1	12		.
	inc de			;7af2	13		.
	djnz l7ae7h		;7af3	10 f2		. .
	ret			;7af5	c9		.
	inc hl			;7af6	23		#
	ld a,(022e2h)		;7af7	3a e2 22	: . "
	ld b,a			;7afa	47		G
l7afbh:
	ld a,(hl)		;7afb	7e		~
	inc hl			;7afc	23		#
	cp 020h			;7afd	fe 20		.  
	jr z,l7afbh		;7aff	28 fa		( .
	dec hl			;7b01	2b		+
	cp 00dh			;7b02	fe 0d		. .
	ret			;7b04	c9		.
	call 00455h		;7b05	cd 55 04	. U .
	call 00480h		;7b08	cd 80 04	. . .
	ld hl,0258ch		;7b0b	21 8c 25	! . %
	push bc			;7b0e	c5		.
	ex de,hl		;7b0f	eb		.
	ld hl,00000h		;7b10	21 00 00	! . .
l7b13h:
	ld a,(de)		;7b13	1a		.
	call 00602h		;7b14	cd 02 06	. . .
	jr nc,l7b28h		;7b17	30 0f		0 .
	inc de			;7b19	13		.
	ld b,h			;7b1a	44		D
	ld c,l			;7b1b	4d		M
	add hl,hl		;7b1c	29		)
	add hl,hl		;7b1d	29		)
	add hl,bc		;7b1e	09		.
	add hl,hl		;7b1f	29		)
	ld b,000h		;7b20	06 00		. .
	and 00fh		;7b22	e6 0f		. .
	ld c,a			;7b24	4f		O
	add hl,bc		;7b25	09		.
	jr l7b13h		;7b26	18 eb		. .
l7b28h:
	pop bc			;7b28	c1		.
	dec de			;7b29	1b		.
	ld a,h			;7b2a	7c		|
	or l			;7b2b	b5		.
	jr nz,l7b30h		;7b2c	20 02		  .
	inc a			;7b2e	3c		<
	ret			;7b2f	c9		.
l7b30h:
	xor a			;7b30	af		.
	ret			;7b31	c9		.
	cp 030h			;7b32	fe 30		. 0
	ccf			;7b34	3f		?
	ret nc			;7b35	d0		.
	cp 03ah			;7b36	fe 3a		. :
	ret			;7b38	c9		.
	ld hl,(022d6h)		;7b39	2a d6 22	* . "
l7b3ch:
	ld (022deh),hl		;7b3c	22 de 22	" . "
	push hl			;7b3f	e5		.
	call 00d93h		;7b40	cd 93 0d	. . .
	call 0047eh		;7b43	cd 7e 04	. ~ .
	pop hl			;7b46	e1		.
	ret nc			;7b47	d0		.
	push hl			;7b48	e5		.
	call 00ca2h		;7b49	cd a2 0c	. . .
	pop de			;7b4c	d1		.
	ld hl,(022d8h)		;7b4d	2a d8 22	* . "
	add hl,de		;7b50	19		.
	ld a,h			;7b51	7c		|
	rlca			;7b52	07		.
	ret c			;7b53	d8		.
	jr l7b3ch		;7b54	18 e6		. .
	ld bc,(022dah)		;7b56	ed 4b da 22	. K . "
	ld a,c			;7b5a	79		y
	or b			;7b5b	b0		.
	ret z			;7b5c	c8		.
	ld hl,(022dch)		;7b5d	2a dc 22	* . "
	ld a,l			;7b60	7d		}
	or h			;7b61	b4		.
	ret			;7b62	c9		.
	call 00626h		;7b63	cd 26 06	. & .
	ret z			;7b66	c8		.
	push hl			;7b67	e5		.
	call 00d43h		;7b68	cd 43 0d	. C .
	pop bc			;7b6b	c1		.
	ret c			;7b6c	d8		.
	push hl			;7b6d	e5		.
	call 00d43h		;7b6e	cd 43 0d	. C .
	pop de			;7b71	d1		.
	call z,00d5dh		;7b72	cc 5d 0d	. ] .
	or a			;7b75	b7		.
	sbc hl,de		;7b76	ed 52		. R
	add hl,de		;7b78	19		.
	ret z			;7b79	c8		.
	ret c			;7b7a	d8		.
	ld bc,(00036h)		;7b7b	ed 4b 36 00	. K 6 .
	call 00676h		;7b7f	cd 76 06	. v .
	ld (00036h),de		;7b82	ed 53 36 00	. S 6 .
	ret			;7b86	c9		.
	call 022f6h		;7b87	cd f6 22	. . "
	ld iy,05c3ah		;7b8a	fd 21 3a 5c	. ! : \
	ei			;7b8e	fb		.
	ret			;7b8f	c9		.
l7b90h:
	ld hl,0241dh		;7b90	21 1d 24	! . $
	jp 00a51h		;7b93	c3 51 0a	. Q .
	push hl			;7b96	e5		.
	push bc			;7b97	c5		.
	add hl,bc		;7b98	09		.
	jr c,l7b90h		;7b99	38 f5		8 .
	ld bc,(0233dh)		;7b9b	ed 4b 3d 23	. K = #
	sbc hl,bc		;7b9f	ed 42		. B
	jr nc,l7b90h		;7ba1	30 ed		0 .
	pop bc			;7ba3	c1		.
	pop hl			;7ba4	e1		.
	ret			;7ba5	c9		.
	push de			;7ba6	d5		.
	ex de,hl		;7ba7	eb		.
	ld h,b			;7ba8	60		`
	ld l,c			;7ba9	69		i
	or a			;7baa	b7		.
	sbc hl,de		;7bab	ed 52		. R
	ld b,h			;7bad	44		D
	ld c,l			;7bae	4d		M
	pop hl			;7baf	e1		.
	call 00666h		;7bb0	cd 66 06	. f .
	or a			;7bb3	b7		.
	sbc hl,de		;7bb4	ed 52		. R
	add hl,de		;7bb6	19		.
	ex de,hl		;7bb7	eb		.
	jr c,l7bc6h		;7bb8	38 0c		8 .
	jr z,l7bcbh		;7bba	28 0f		( .
	add hl,bc		;7bbc	09		.
	ex de,hl		;7bbd	eb		.
	add hl,bc		;7bbe	09		.
	ex de,hl		;7bbf	eb		.
	inc bc			;7bc0	03		.
	push de			;7bc1	d5		.
	lddr			;7bc2	ed b8		. .
	pop de			;7bc4	d1		.
	ret			;7bc5	c9		.
l7bc6h:
	inc bc			;7bc6	03		.
	ldir			;7bc7	ed b0		. .
	dec de			;7bc9	1b		.
	ret			;7bca	c9		.
l7bcbh:
	add hl,bc		;7bcb	09		.
	ex de,hl		;7bcc	eb		.
	ret			;7bcd	c9		.
	ld a,(022ach)		;7bce	3a ac 22	: . "
	cp 021h			;7bd1	fe 21		. !
	ret c			;7bd3	d8		.
	ld (022e2h),a		;7bd4	32 e2 22	2 . "
	ret			;7bd7	c9		.
	pop hl			;7bd8	e1		.
	call 003b7h		;7bd9	cd b7 03	. . .
	ld bc,(022dah)		;7bdc	ed 4b da 22	. K . "
	call 00d43h		;7be0	cd 43 0d	. C .
	ret nz			;7be3	c0		.
	inc hl			;7be4	23		#
	inc hl			;7be5	23		#
	call 00dadh		;7be6	cd ad 0d	. . .
	call 00d7dh		;7be9	cd 7d 0d	. } .
	call 00d7ah		;7bec	cd 7a 0d	. z .
	res 0,(ix+000h)		;7bef	dd cb 00 86	. . . .
	ld a,05fh		;7bf3	3e 5f		> _
	ld c,(ix+000h)		;7bf5	dd 4e 00	. N .
	bit 3,c			;7bf8	cb 59		. Y
	jr z,l7bfeh		;7bfa	28 02		( .
	ld a,02ah		;7bfc	3e 2a		> *
l7bfeh:
	bit 2,c			;7bfe	cb 51		. Q
	jr z,l7c04h		;7c00	28 02		( .
	ld a,02bh		;7c02	3e 2b		> +
l7c04h:
	call 00020h		;7c04	cd 20 00	.   .
	ld de,006bfh		;7c07	11 bf 06	. . .
	push de			;7c0a	d5		.
	bit 2,c			;7c0b	cb 51		. Q
	jr nz,l7c7dh		;7c0d	20 6e		  n
	bit 3,c			;7c0f	cb 59		. Y
	jr nz,l7c1bh		;7c11	20 08		  .
	ld hl,02184h		;7c13	21 84 21	! . !
	ld b,00eh		;7c16	06 0e		. .
	jp 00437h		;7c18	c3 37 04	. 7 .
l7c1bh:
	cp 00dh			;7c1b	fe 0d		. .
	jr nz,l7c23h		;7c1d	20 04		  .
	res 3,(ix+000h)		;7c1f	dd cb 00 9e	. . . .
l7c23h:
	cp 009h			;7c23	fe 09		. .
	jr nz,l7c30h		;7c25	20 09		  .
	set 7,(ix+000h)		;7c27	dd cb 00 fe	. . . .
	call 0045eh		;7c2b	cd 5e 04	. ^ .
	jr l7c47h		;7c2e	18 17		. .
l7c30h:
	cp 008h			;7c30	fe 08		. .
	jr nz,l7c39h		;7c32	20 05		  .
	call 007b9h		;7c34	cd b9 07	. . .
	jr l7cb2h		;7c37	18 79		. y
l7c39h:
	bit 7,(ix+000h)		;7c39	dd cb 00 7e	. . . ~
	jr z,l7c50h		;7c3d	28 11		( .
l7c3fh:
	ld a,(022e3h)		;7c3f	3a e3 22	: . "
	ld b,a			;7c42	47		G
	ld a,(0231ah)		;7c43	3a 1a 23	: . #
	cp b			;7c46	b8		.
l7c47h:
	ld a,020h		;7c47	3e 20		>  
	jr c,l7c50h		;7c49	38 05		8 .
	res 7,(ix+000h)		;7c4b	dd cb 00 be	. . . .
	ret			;7c4f	c9		.
l7c50h:
	cp 020h			;7c50	fe 20		.  
	ret c			;7c52	d8		.
	ld e,a			;7c53	5f		_
	ld a,(022e4h)		;7c54	3a e4 22	: . "
	or a			;7c57	b7		.
	ret z			;7c58	c8		.
	ld hl,0258ch		;7c59	21 8c 25	! . %
	call 00d0ch		;7c5c	cd 0c 0d	. . .
	cp 050h			;7c5f	fe 50		. P
	ret z			;7c61	c8		.
	ld b,h			;7c62	44		D
	ld c,l			;7c63	4d		M
	dec bc			;7c64	0b		.
	ld hl,(022e6h)		;7c65	2a e6 22	* . "
	push de			;7c68	d5		.
	push hl			;7c69	e5		.
	ld d,h			;7c6a	54		T
	ld e,l			;7c6b	5d		]
	inc de			;7c6c	13		.
	call 00676h		;7c6d	cd 76 06	. v .
	pop hl			;7c70	e1		.
	pop de			;7c71	d1		.
	ld (hl),e		;7c72	73		s
	call 00765h		;7c73	cd 65 07	. e .
	bit 7,(ix+000h)		;7c76	dd cb 00 7e	. . . ~
	ret z			;7c7a	c8		.
	jr l7c3fh		;7c7b	18 c2		. .
l7c7dh:
	cp 008h			;7c7d	fe 08		. .
	jr z,l7ce9h		;7c7f	28 68		( h
	cp 00dh			;7c81	fe 0d		. .
	jr nz,l7c89h		;7c83	20 04		  .
	res 2,(ix+000h)		;7c85	dd cb 00 96	. . . .
l7c89h:
	cp 020h			;7c89	fe 20		.  
	ret c			;7c8b	d8		.
	ld hl,(022e6h)		;7c8c	2a e6 22	* . "
	ld d,a			;7c8f	57		W
	ld a,(hl)		;7c90	7e		~
	cp 00dh			;7c91	fe 0d		. .
	ret z			;7c93	c8		.
	ld (hl),d		;7c94	72		r
	call 007a0h		;7c95	cd a0 07	. . .
	cp 00dh			;7c98	fe 0d		. .
	ret z			;7c9a	c8		.
	call 00490h		;7c9b	cd 90 04	. . .
l7c9eh:
	ld a,e			;7c9e	7b		{
	ex de,hl		;7c9f	eb		.
	ld hl,022e3h		;7ca0	21 e3 22	! . "
	ld (hl),b		;7ca3	70		p
	inc hl			;7ca4	23		#
	ld (hl),c		;7ca5	71		q
	inc hl			;7ca6	23		#
	ld (hl),a		;7ca7	77		w
	inc hl			;7ca8	23		#
	ld (hl),e		;7ca9	73		s
	inc hl			;7caa	23		#
	ld (hl),d		;7cab	72		r
	res 1,(ix+000h)		;7cac	dd cb 00 8e	. . . .
	or a			;7cb0	b7		.
	ret			;7cb1	c9		.
l7cb2h:
	ld hl,(022e6h)		;7cb2	2a e6 22	* . "
	ld a,(hl)		;7cb5	7e		~
	cp 00dh			;7cb6	fe 0d		. .
	ret z			;7cb8	c8		.
	ld bc,025dbh		;7cb9	01 db 25	. . %
	ld d,h			;7cbc	54		T
	ld e,l			;7cbd	5d		]
	inc hl			;7cbe	23		#
	jp 00676h		;7cbf	c3 76 06	. v .
	call 0045eh		;7cc2	cd 5e 04	. ^ .
l7cc5h:
	call 00765h		;7cc5	cd 65 07	. e .
	ret z			;7cc8	c8		.
	ld a,(0231ah)		;7cc9	3a 1a 23	: . #
	cp b			;7ccc	b8		.
	ret nc			;7ccd	d0		.
	jr l7cc5h		;7cce	18 f5		. .
	set 0,(ix+000h)		;7cd0	dd cb 00 c6	. . . .
	set 1,(ix+000h)		;7cd4	dd cb 00 ce	. . . .
	ld hl,022e3h		;7cd8	21 e3 22	! . "
	ld b,(hl)		;7cdb	46		F
	inc hl			;7cdc	23		#
	ld c,(hl)		;7cdd	4e		N
	inc hl			;7cde	23		#
	ld e,(hl)		;7cdf	5e		^
	inc hl			;7ce0	23		#
	ld a,(hl)		;7ce1	7e		~
	inc hl			;7ce2	23		#
	ld h,(hl)		;7ce3	66		f
	ld l,a			;7ce4	6f		o
	ld d,006h		;7ce5	16 06		. .
	ld a,(hl)		;7ce7	7e		~
	ret			;7ce8	c9		.
l7ce9h:
	call 007a0h		;7ce9	cd a0 07	. . .
	ld a,008h		;7cec	3e 08		> .
	call 004d2h		;7cee	cd d2 04	. . .
	jr l7c9eh		;7cf1	18 ab		. .
	set 2,(ix+000h)		;7cf3	dd cb 00 d6	. . . .
	ret			;7cf7	c9		.
l7cf8h:
	call 00765h		;7cf8	cd 65 07	. e .
	jr nz,l7cf8h		;7cfb	20 fb		  .
	set 3,(ix+000h)		;7cfd	dd cb 00 de	. . . .
	ret			;7d01	c9		.
l7d02h:
	ld hl,(022e6h)		;7d02	2a e6 22	* . "
	ld a,(hl)		;7d05	7e		~
	cp 00dh			;7d06	fe 0d		. .
	ret z			;7d08	c8		.
	call 00782h		;7d09	cd 82 07	. . .
	jr l7d02h		;7d0c	18 f4		. .
	pop hl			;7d0e	e1		.
	pop hl			;7d0f	e1		.
	pop hl			;7d10	e1		.
	jr l7d69h		;7d11	18 56		. V
	ld de,(022f1h)		;7d13	ed 5b f1 22	. [ . "
	call 008c3h		;7d17	cd c3 08	. . .
	jr nc,l7d1eh		;7d1a	30 02		0 .
	pop hl			;7d1c	e1		.
	ret			;7d1d	c9		.
l7d1eh:
	ld hl,022ach		;7d1e	21 ac 22	! . "
	call 00d0ch		;7d21	cd 0c 0d	. . .
	dec a			;7d24	3d		=
	ld e,a			;7d25	5f		_
	ld hl,022c0h		;7d26	21 c0 22	! . "
	push hl			;7d29	e5		.
	call 00d0ch		;7d2a	cd 0c 0d	. . .
	dec a			;7d2d	3d		=
	push af			;7d2e	f5		.
	sub e			;7d2f	93		.
	ld b,000h		;7d30	06 00		. .
	ld d,b			;7d32	50		P
	ld c,a			;7d33	4f		O
	jp p,00808h		;7d34	f2 08 08	. . .
	dec b			;7d37	05		.
	ld hl,0258ch		;7d38	21 8c 25	! . %
	call 00d0ch		;7d3b	cd 0c 0d	. . .
	dec hl			;7d3e	2b		+
	push hl			;7d3f	e5		.
	sub 050h		;7d40	d6 50		. P
	add a,c			;7d42	81		.
	jp p,007deh		;7d43	f2 de 07	. . .
	ld hl,(022f1h)		;7d46	2a f1 22	* . "
	add hl,de		;7d49	19		.
	push hl			;7d4a	e5		.
	add hl,bc		;7d4b	09		.
	ex de,hl		;7d4c	eb		.
	pop hl			;7d4d	e1		.
	pop bc			;7d4e	c1		.
	call 00676h		;7d4f	cd 76 06	. v .
	pop bc			;7d52	c1		.
	pop hl			;7d53	e1		.
	ld c,b			;7d54	48		H
	ld b,000h		;7d55	06 00		. .
	inc c			;7d57	0c		.
	dec c			;7d58	0d		.
	ld de,(022f1h)		;7d59	ed 5b f1 22	. [ . "
	jr z,l7d61h		;7d5d	28 02		( .
	ldir			;7d5f	ed b0		. .
l7d61h:
	call 008c0h		;7d61	cd c0 08	. . .
	pop hl			;7d64	e1		.
	jp nc,008d8h		;7d65	d2 d8 08	. . .
	ret			;7d68	c9		.
l7d69h:
	ld de,(022f1h)		;7d69	ed 5b f1 22	. [ . "
	inc de			;7d6d	13		.
	jr l7d61h		;7d6e	18 f1		. .
	ld a,010h		;7d70	3e 10		> .
	call 0001dh		;7d72	cd 1d 00	. . .
	ld bc,(022dah)		;7d75	ed 4b da 22	. K . "
	ld a,b			;7d79	78		x
	or c			;7d7a	b1		.
	jr nz,l7d7eh		;7d7b	20 01		  .
	inc c			;7d7d	0c		.
l7d7eh:
	call 00d43h		;7d7e	cd 43 0d	. C .
	ret c			;7d81	d8		.
	set 0,(ix+000h)		;7d82	dd cb 00 c6	. . . .
l7d86h:
	ld a,(022f3h)		;7d86	3a f3 22	: . "
	ld (ix+001h),a		;7d89	dd 77 01	. w .
l7d8ch:
	ld c,(hl)		;7d8c	4e		N
	inc hl			;7d8d	23		#
	ld b,(hl)		;7d8e	46		F
	ex de,hl		;7d8f	eb		.
	ld hl,(022dch)		;7d90	2a dc 22	* . "
	ld a,h			;7d93	7c		|
	or l			;7d94	b5		.
	jr nz,l7d9ah		;7d95	20 03		  .
	ld hl,07fffh		;7d97	21 ff 7f	! . .
l7d9ah:
	sbc hl,bc		;7d9a	ed 42		. B
	ret c			;7d9c	d8		.
	ld h,b			;7d9d	60		`
	ld l,c			;7d9e	69		i
	push de			;7d9f	d5		.
	call 00d93h		;7da0	cd 93 0d	. . .
	pop hl			;7da3	e1		.
	push hl			;7da4	e5		.
	inc hl			;7da5	23		#
	ld d,006h		;7da6	16 06		. .
	ld c,000h		;7da8	0e 00		. .
	ld a,(00281h)		;7daa	3a 81 02	: . .
	or a			;7dad	b7		.
	jr z,l7dc1h		;7dae	28 11		( .
	call 00dadh		;7db0	cd ad 0d	. . .
	ld hl,0258ch		;7db3	21 8c 25	! . %
l7db6h:
	ld a,(hl)		;7db6	7e		~
	inc hl			;7db7	23		#
	call 0001dh		;7db8	cd 1d 00	. . .
	cp 00dh			;7dbb	fe 0d		. .
	jr nz,l7db6h		;7dbd	20 f7		  .
	jr l7dc4h		;7dbf	18 03		. .
l7dc1h:
	call 00485h		;7dc1	cd 85 04	. . .
l7dc4h:
	pop hl			;7dc4	e1		.
	dec hl			;7dc5	2b		+
	call 00d5dh		;7dc6	cd 5d 0d	. ] .
	ret z			;7dc9	c8		.
	ld a,(00281h)		;7dca	3a 81 02	: . .
	or a			;7dcd	b7		.
	jr z,l7dd6h		;7dce	28 06		( .
	call 00023h		;7dd0	cd 23 00	. # .
	ret c			;7dd3	d8		.
	jr l7d8ch		;7dd4	18 b6		. .
l7dd6h:
	dec (ix+001h)		;7dd6	dd 35 01	. 5 .
	jp p,0085ch		;7dd9	f2 5c 08	. \ .
	call 00317h		;7ddc	cd 17 03	. . .
	cp 003h			;7ddf	fe 03		. .
	ret z			;7de1	c8		.
	jr l7d86h		;7de2	18 a2		. .
	ld a,(022dah)		;7de4	3a da 22	: . "
	or a			;7de7	b7		.
	jr nz,l7dech		;7de8	20 02		  .
	ld a,00fh		;7dea	3e 0f		> .
l7dech:
	ld (022f3h),a		;7dec	32 f3 22	2 . "
	ret			;7def	c9		.
	call 003b7h		;7df0	cd b7 03	. . .
	push de			;7df3	d5		.
	ld hl,(022dah)		;7df4	2a da 22	* . "
	call 00ca2h		;7df7	cd a2 0c	. . .
	push hl			;7dfa	e5		.
	call 00dadh		;7dfb	cd ad 0d	. . .
	pop bc			;7dfe	c1		.
	dec bc			;7dff	0b		.
	dec bc			;7e00	0b		.
	pop de			;7e01	d1		.
	jr l7e45h		;7e02	18 41		. A
	call 008f4h		;7e04	cd f4 08	. . .
	ret c			;7e07	d8		.
	ld de,(022f1h)		;7e08	ed 5b f1 22	. [ . "
	push de			;7e0c	d5		.
	call 00d7dh		;7e0d	cd 7d 0d	. } .
	call 007a0h		;7e10	cd a0 07	. . .
l7e13h:
	pop de			;7e13	d1		.
	or a			;7e14	b7		.
	sbc hl,de		;7e15	ed 52		. R
	add hl,de		;7e17	19		.
	jp z,006bfh		;7e18	ca bf 06	. . .
	inc hl			;7e1b	23		#
	push de			;7e1c	d5		.
	push hl			;7e1d	e5		.
	call 00765h		;7e1e	cd 65 07	. e .
	pop hl			;7e21	e1		.
	jr l7e13h		;7e22	18 ef		. .
	ld bc,(022d6h)		;7e24	ed 4b d6 22	. K . "
	call 00d43h		;7e28	cd 43 0d	. C .
	ret c			;7e2b	d8		.
l7e2ch:
	push hl			;7e2c	e5		.
	ld e,(hl)		;7e2d	5e		^
	inc hl			;7e2e	23		#
	ld d,(hl)		;7e2f	56		V
	inc hl			;7e30	23		#
	push de			;7e31	d5		.
	call 00dadh		;7e32	cd ad 0d	. . .
	pop de			;7e35	d1		.
	pop bc			;7e36	c1		.
	ld hl,(022d8h)		;7e37	2a d8 22	* . "
	or a			;7e3a	b7		.
	sbc hl,de		;7e3b	ed 52		. R
	ret c			;7e3d	d8		.
	ld (022dah),de		;7e3e	ed 53 da 22	. S . "
	ld de,0258ch		;7e42	11 8c 25	. . %
l7e45h:
	ld (022f1h),de		;7e45	ed 53 f1 22	. S . "
	ld hl,022ach		;7e49	21 ac 22	! . "
	ld a,(hl)		;7e4c	7e		~
	cp 00dh			;7e4d	fe 0d		. .
	scf			;7e4f	37		7
	ret z			;7e50	c8		.
l7e51h:
	ld a,(hl)		;7e51	7e		~
	cp 00dh			;7e52	fe 0d		. .
	ret z			;7e54	c8		.
	ld a,(de)		;7e55	1a		.
	cp 00dh			;7e56	fe 0d		. .
	jr z,l7e61h		;7e58	28 07		( .
	cp (hl)			;7e5a	be		.
	inc hl			;7e5b	23		#
	inc de			;7e5c	13		.
	jr z,l7e51h		;7e5d	28 f2		( .
	jr l7e45h		;7e5f	18 e4		. .
l7e61h:
	ld h,b			;7e61	60		`
	ld l,c			;7e62	69		i
	call 00d5dh		;7e63	cd 5d 0d	. ] .
	jr nz,l7e2ch		;7e66	20 c4		  .
	scf			;7e68	37		7
	ret			;7e69	c9		.
	ld hl,(0230ch)		;7e6a	2a 0c 23	* . #
	ld de,(0230eh)		;7e6d	ed 5b 0e 23	. [ . #
	call 009f6h		;7e71	cd f6 09	. . .
	ret z			;7e74	c8		.
	ret c			;7e75	d8		.
	cp 03ah			;7e76	fe 3a		. :
	jp nz,009c0h		;7e78	c2 c0 09	. . .
	push iy			;7e7b	fd e5		. .
	push ix			;7e7d	dd e5		. .
	call 00b83h		;7e7f	cd 83 0b	. . .
	ld (05ce8h),hl		;7e82	22 e8 5c	" . \
	ex de,hl		;7e85	eb		.
	ld (05ce6h),hl		;7e86	22 e6 5c	" . \
	ld c,00bh		;7e89	0e 0b		. .
	call 03d13h		;7e8b	cd 13 3d	. . =
	pop ix			;7e8e	dd e1		. .
	pop iy			;7e90	fd e1		. .
	ret			;7e92	c9		.
	ld a,d			;7e93	7a		z
	rst 10h			;7e94	d7		.
	ld b,004h		;7e95	06 04		. .
l7e97h:
	ld a,0ffh		;7e97	3e ff		> .
	rst 10h			;7e99	d7		.
	djnz l7e97h		;7e9a	10 fb		. .
l7e9ch:
	ld a,h			;7e9c	7c		|
	or l			;7e9d	b5		.
	jr z,l7ee9h		;7e9e	28 49		( I
	ld a,(de)		;7ea0	1a		.
	rst 10h			;7ea1	d7		.
	inc de			;7ea2	13		.
	dec hl			;7ea3	2b		+
	jr l7e9ch		;7ea4	18 f6		. .
	call 009dbh		;7ea6	cd db 09	. . .
	ret z			;7ea9	c8		.
	ret c			;7eaa	d8		.
	cp 03ah			;7eab	fe 3a		. :
	jr nz,l7ef0h		;7ead	20 41		  A
	push iy			;7eaf	fd e5		. .
	push ix			;7eb1	dd e5		. .
	call 00b83h		;7eb3	cd 83 0b	. . .
	ld (05ce8h),hl		;7eb6	22 e8 5c	" . \
	ex de,hl		;7eb9	eb		.
l7ebah:
	ld (05ce6h),hl		;7eba	22 e6 5c	" . \
	ld c,00bh		;7ebd	0e 0b		. .
	call 03d13h		;7ebf	cd 13 3d	. . =
	pop ix			;7ec2	dd e1		. .
	pop iy			;7ec4	fd e1		. .
	ret			;7ec6	c9		.
	dec bc			;7ec7	0b		.
	ld (ix+00ch),b		;7ec8	dd 70 0c	. p .
	push hl			;7ecb	e5		.
	push de			;7ecc	d5		.
	push ix			;7ecd	dd e5		. .
	pop de			;7ecf	d1		.
	ld hl,00052h		;7ed0	21 52 00	! R .
	add hl,de		;7ed3	19		.
	pop de			;7ed4	d1		.
	ex de,hl		;7ed5	eb		.
	ld a,b			;7ed6	78		x
	or c			;7ed7	b1		.
	ld a,b			;7ed8	78		x
	jr z,l7ee8h		;7ed9	28 0d		( .
	ldir			;7edb	ed b0		. .
	cp 002h			;7edd	fe 02		. .
	jr nz,l7ee8h		;7edf	20 07		  .
	push hl			;7ee1	e5		.
	rst 8			;7ee2	cf		.
	ld h,0d1h		;7ee3	26 d1		& .
	pop hl			;7ee5	e1		.
	jr l7ebah		;7ee6	18 d2		. .
l7ee8h:
	pop hl			;7ee8	e1		.
l7ee9h:
	rst 8			;7ee9	cf		.
	inc hl			;7eea	23		#
	pop ix			;7eeb	dd e1		. .
	pop iy			;7eed	fd e1		. .
	ret			;7eef	c9		.
l7ef0h:
	ld (0265dh),hl		;7ef0	22 5d 26	" ] &
	ex de,hl		;7ef3	eb		.
	ld (0265fh),hl		;7ef4	22 5f 26	" _ &
	push ix			;7ef7	dd e5		. .
	push hl			;7ef9	e5		.
	push de			;7efa	d5		.
	ld hl,02a3ah		;7efb	21 3a 2a	! : *
	ld (hl),003h		;7efe	36 03		6 .
	call 00a04h		;7f00	cd 04 0a	. . .
	pop de			;7f03	d1		.
	pop ix			;7f04	dd e1		. .
	ld a,0ffh		;7f06	3e ff		> .
	jp 00a1dh		;7f08	c3 1d 0a	. . .
	ld bc,(022d6h)		;7f0b	ed 4b d6 22	. K . "
	call 00d43h		;7f0f	cd 43 0d	. C .
	ret c			;7f12	d8		.
	push hl			;7f13	e5		.
	ld bc,(022d8h)		;7f14	ed 4b d8 22	. K . "
	call 00d43h		;7f18	cd 43 0d	. C .
	pop de			;7f1b	d1		.
	jr z,l7f23h		;7f1c	28 05		( .
	jr nc,l7f23h		;7f1e	30 03		0 .
	ld hl,(00036h)		;7f20	2a 36 00	* 6 .
l7f23h:
	call z,00d5dh		;7f23	cc 5d 0d	. ] .
	or a			;7f26	b7		.
	sbc hl,de		;7f27	ed 52		. R
	ld a,(022adh)		;7f29	3a ad 22	: . "
	ret			;7f2c	c9		.
l7f2dh:
	ld (hl),020h		;7f2d	36 20		6  
	inc hl			;7f2f	23		#
	djnz l7f2dh		;7f30	10 fb		. .
	jr l7f43h		;7f32	18 0f		. .
	ld b,00ah		;7f34	06 0a		. .
	ld de,022ach		;7f36	11 ac 22	. . "
l7f39h:
	inc hl			;7f39	23		#
	ld a,(de)		;7f3a	1a		.
	cp 00dh			;7f3b	fe 0d		. .
	jr z,l7f2dh		;7f3d	28 ee		( .
	ld (hl),a		;7f3f	77		w
	inc de			;7f40	13		.
	djnz l7f39h		;7f41	10 f6		. .
l7f43h:
	ld de,00011h		;7f43	11 11 00	. . .
	xor a			;7f46	af		.
	push ix			;7f47	dd e5		. .
	ld ix,02a3ah		;7f49	dd 21 3a 2a	. ! : *
	call 004c2h		;7f4d	cd c2 04	. . .
	pop ix			;7f50	dd e1		. .
	ld hl,02a3ah		;7f52	21 3a 2a	! : *
	ld de,00022h		;7f55	11 22 00	. " .
l7f58h:
	ld (hl),000h		;7f58	36 00		6 .
	inc hl			;7f5a	23		#
	dec de			;7f5b	1b		.
	ld a,d			;7f5c	7a		z
	or e			;7f5d	b3		.
	jr nz,l7f58h		;7f5e	20 f8		  .
	ret			;7f60	c9		.
	push ix			;7f61	dd e5		. .
	ld ix,02a3ah		;7f63	dd 21 3a 2a	. ! : *
	call 00a3dh		;7f67	cd 3d 0a	. = .
	pop ix			;7f6a	dd e1		. .
	ret			;7f6c	c9		.
l7f6dh:
	scf			;7f6d	37		7
	push af			;7f6e	f5		.
	push ix			;7f6f	dd e5		. .
	call 00556h		;7f71	cd 56 05	. V .
	pop ix			;7f74	dd e1		. .
	jr nc,l7f7ah		;7f76	30 02		0 .
	pop af			;7f78	f1		.
	ret			;7f79	c9		.
l7f7ah:
	pop af			;7f7a	f1		.
	or a			;7f7b	b7		.
	jr z,l7f6dh		;7f7c	28 ef		( .
	ld hl,02407h		;7f7e	21 07 24	! . $
	call 00455h		;7f81	cd 55 04	. U .
	jp 0002fh		;7f84	c3 2f 00	. / .
	push de			;7f87	d5		.
	ld hl,0237ch		;7f88	21 7c 23	! | #
	call 00455h		;7f8b	cd 55 04	. U .
l7f8eh:
	ld de,00011h		;7f8e	11 11 00	. . .
	xor a			;7f91	af		.
	push bc			;7f92	c5		.
	call 00a31h		;7f93	cd 31 0a	. 1 .
	pop bc			;7f96	c1		.
	ld a,c			;7f97	79		y
	ld hl,02a3ah		;7f98	21 3a 2a	! : *
	cp (hl)			;7f9b	be		.
	jr nz,l7f8eh		;7f9c	20 f0		  .
	pop de			;7f9e	d1		.
	push de			;7f9f	d5		.
	ld b,00ah		;7fa0	06 0a		. .
	ld (hl),b		;7fa2	70		p
	inc hl			;7fa3	23		#
	ld a,(de)		;7fa4	1a		.
	cp 00dh			;7fa5	fe 0d		. .
	jr z,l7fbah		;7fa7	28 11		( .
l7fa9h:
	ld a,(de)		;7fa9	1a		.
	cp 00dh			;7faa	fe 0d		. .
	jr nz,l7fb3h		;7fac	20 05		  .
	ld a,020h		;7fae	3e 20		>  
	cp (hl)			;7fb0	be		.
	jr z,l7fbah		;7fb1	28 07		( .
l7fb3h:
	cp (hl)			;7fb3	be		.
	inc hl			;7fb4	23		#
	inc de			;7fb5	13		.
	jr nz,l7fbah		;7fb6	20 02		  .
	djnz l7fa9h		;7fb8	10 ef		. .
l7fbah:
	push af			;7fba	f5		.
	ld hl,02415h		;7fbb	21 15 24	! . $
	jr z,l7fc3h		;7fbe	28 03		( .
	ld hl,0242dh		;7fc0	21 2d 24	! - $
l7fc3h:
	call 00455h		;7fc3	cd 55 04	. U .
	ld hl,02653h		;7fc6	21 53 26	! S &
	ld b,00ah		;7fc9	06 0a		. .
l7fcbh:
	ld a,(hl)		;7fcb	7e		~
	inc hl			;7fcc	23		#
	call 0001dh		;7fcd	cd 1d 00	. . .
	djnz l7fcbh		;7fd0	10 f9		. .
	call 003b7h		;7fd2	cd b7 03	. . .
	pop af			;7fd5	f1		.
	jr nz,l7f8eh		;7fd6	20 b6		  .
	pop de			;7fd8	d1		.
	ld e,(hl)		;7fd9	5e		^
	inc hl			;7fda	23		#
	ld d,(hl)		;7fdb	56		V
	ret			;7fdc	c9		.
	push ix			;7fdd	dd e5		. .
	call 02658h		;7fdf	cd 58 26	. X &
	pop ix			;7fe2	dd e1		. .
	ret			;7fe4	c9		.
	pop ix			;7fe5	dd e1		. .
	ret			;7fe7	c9		.
	push ix			;7fe8	dd e5		. .
	call 0265eh		;7fea	cd 5e 26	. ^ &
	pop ix			;7fed	dd e1		. .
	ret			;7fef	c9		.
	ld hl,022ach		;7ff0	21 ac 22	! . "
	ld a,(hl)		;7ff3	7e		~
	sub 031h		;7ff4	d6 31		. 1
	jr c,l802bh		;7ff6	38 33		8 3
	cp 004h			;7ff8	fe 04		. .
	jr nc,l802bh		;7ffa	30 2f		0 /
	push hl			;7ffc	e5		.
	push af			;7ffd	f5		.
	call 02663h		;7ffe	cd 63 26	. c &
	pop af			;8001	f1		.
	ld c,018h		;8002	0e 18		. .
	call 03d13h		;8004	cd 13 3d	. . =
	pop hl			;8007	e1		.
	inc hl			;8008	23		#
	inc hl			;8009	23		#
	ld c,013h		;800a	0e 13		. .
	call 03d13h		;800c	cd 13 3d	. . =
	ld b,008h		;800f	06 08		. .
	ld hl,05cddh		;8011	21 dd 5c	! . \
	ld a,020h		;8014	3e 20		>  
l8016h:
	cp (hl)			;8016	be		.
	jr nc,l801eh		;8017	30 05		0 .
	inc hl			;8019	23		#
	djnz l8016h		;801a	10 fa		. .
	jr l8022h		;801c	18 04		. .
l801eh:
	ld (hl),a		;801e	77		w
	inc hl			;801f	23		#
	djnz l801eh		;8020	10 fc		. .
l8022h:
	ld (hl),043h		;8022	36 43		6 C
	ld c,00ah		;8024	0e 0a		. .
	call 03d13h		;8026	cd 13 3d	. . =
	ret			;8029	c9		.
	ret			;802a	c9		.
l802bh:
	ld hl,02467h		;802b	21 67 24	! g $
	jp 00a51h		;802e	c3 51 0a	. Q .
	call 009dbh		;8031	cd db 09	. . .
	ret z			;8034	c8		.
	ret c			;8035	d8		.
	ex de,hl		;8036	eb		.
	push hl			;8037	e5		.
	push de			;8038	d5		.
	pop bc			;8039	c1		.
	push bc			;803a	c5		.
	ld de,00001h		;803b	11 01 00	. . .
	ld a,00dh		;803e	3e 0d		> .
	cpi			;8040	ed a1		. .
	cpi			;8042	ed a1		. .
	inc de			;8044	13		.
	cpir			;8045	ed b1		. .
	jp pe,00b10h		;8047	ea 10 0b	. . .
	ld (0265dh),de		;804a	ed 53 5d 26	. S ] &
	ld hl,02a3ah		;804e	21 3a 2a	! : *
	ld (hl),004h		;8051	36 04		6 .
	call 00a04h		;8053	cd 04 0a	. . .
	pop bc			;8056	c1		.
	pop hl			;8057	e1		.
l8058h:
	ld de,02a3ah		;8058	11 3a 2a	. : *
	call 00b4eh		;805b	cd 4e 0b	. N .
	push af			;805e	f5		.
	push hl			;805f	e5		.
	push bc			;8060	c5		.
	call 01166h		;8061	cd 66 11	. f .
	push de			;8064	d5		.
	ld a,0ffh		;8065	3e ff		> .
	call 00a17h		;8067	cd 17 0a	. . .
	pop bc			;806a	c1		.
	inc b			;806b	04		.
l806ch:
	push bc			;806c	c5		.
	ld bc,06000h		;806d	01 00 60	. . `
l8070h:
	dec bc			;8070	0b		.
	ld a,b			;8071	78		x
	or c			;8072	b1		.
	jr nz,l8070h		;8073	20 fb		  .
	pop bc			;8075	c1		.
	djnz l806ch		;8076	10 f4		. .
	pop bc			;8078	c1		.
	pop hl			;8079	e1		.
	pop af			;807a	f1		.
	ret c			;807b	d8		.
	jr l8058h		;807c	18 da		. .
l807eh:
	ld a,b			;807e	78		x
	or c			;807f	b1		.
	scf			;8080	37		7
	ret z			;8081	c8		.
	push bc			;8082	c5		.
	ld bc,00000h		;8083	01 00 00	. . .
	push hl			;8086	e5		.
	inc hl			;8087	23		#
	inc hl			;8088	23		#
	ld a,00dh		;8089	3e 0d		> .
	cpir			;808b	ed b1		. .
	ld a,c			;808d	79		y
	neg			;808e	ed 44		. D
	add a,002h		;8090	c6 02		. .
	ld l,a			;8092	6f		o
	xor a			;8093	af		.
	ld h,a			;8094	67		g
	add hl,de		;8095	19		.
	ld bc,(022e8h)		;8096	ed 4b e8 22	. K . "
	dec bc			;809a	0b		.
	dec bc			;809b	0b		.
	dec bc			;809c	0b		.
	sbc hl,bc		;809d	ed 42		. B
	pop hl			;809f	e1		.
	pop bc			;80a0	c1		.
	ld (de),a		;80a1	12		.
	inc de			;80a2	13		.
	ld (de),a		;80a3	12		.
	dec de			;80a4	1b		.
	ret nc			;80a5	d0		.
	ldi			;80a6	ed a0		. .
l80a8h:
	ldi			;80a8	ed a0		. .
	ld a,(hl)		;80aa	7e		~
	cp 00dh			;80ab	fe 0d		. .
	jr nz,l80a8h		;80ad	20 f9		  .
	ldi			;80af	ed a0		. .
	jr l807eh		;80b1	18 cb		. .
	ld iy,05c3ah		;80b3	fd 21 3a 5c	. ! : \
	push hl			;80b7	e5		.
	push de			;80b8	d5		.
l80b9h:
	call 00ac0h		;80b9	cd c0 0a	. . .
	ld a,c			;80bc	79		y
	bit 7,a			;80bd	cb 7f		. .
	nop			;80bf	00		.
	jr nz,l80dfh		;80c0	20 1d		  .
	nop			;80c2	00		.
	nop			;80c3	00		.
	ld hl,02440h		;80c4	21 40 24	! @ $
	call 00455h		;80c7	cd 55 04	. U .
	call 00317h		;80ca	cd 17 03	. . .
	and 05fh		;80cd	e6 5f		. _
	cp 059h			;80cf	fe 59		. Y
	jp nz,0002fh		;80d1	c2 2f 00	. / .
	ld hl,0cd12h		;80d4	21 12 cd	! . .
	ld c,012h		;80d7	0e 12		. .
	call 03d13h		;80d9	cd 13 3d	. . =
	jr l80b9h		;80dc	18 db		. .
	nop			;80de	00		.
l80dfh:
	pop de			;80df	d1		.
	pop hl			;80e0	e1		.
	ret			;80e1	c9		.
	push hl			;80e2	e5		.
	rst 8			;80e3	cf		.
	inc hl			;80e4	23		#
	pop hl			;80e5	e1		.
l80e6h:
	jp 00a51h		;80e6	c3 51 0a	. Q .
	ld bc,00052h		;80e9	01 52 00	. R .
	push ix			;80ec	dd e5		. .
	pop hl			;80ee	e1		.
	add hl,bc		;80ef	09		.
	ld c,(ix+045h)		;80f0	dd 4e 45	. N E
	ld b,(ix+046h)		;80f3	dd 46 46	. F F
	ld a,b			;80f6	78		x
	or c			;80f7	b1		.
	ret z			;80f8	c8		.
	ldir			;80f9	ed b0		. .
	ret			;80fb	c9		.
	call 00ac0h		;80fc	cd c0 0a	. . .
l80ffh:
	ld a,c			;80ff	79		y
	bit 7,a			;8100	cb 7f		. .
	nop			;8102	00		.
	ret z			;8103	c8		.
	nop			;8104	00		.
	nop			;8105	00		.
	nop			;8106	00		.
	nop			;8107	00		.
	nop			;8108	00		.
	nop			;8109	00		.
l810ah:
	ld hl,0245ah		;810a	21 5a 24	! Z $
	jr l80e6h		;810d	18 d7		. .
	ld a,(022adh)		;810f	3a ad 22	: . "
	cp 03ah			;8112	fe 3a		. :
	jr nz,l814ch		;8114	20 36		  6
	push iy			;8116	fd e5		. .
	ld iy,05c3ah		;8118	fd 21 3a 5c	. ! : \
	push ix			;811c	dd e5		. .
	call 00bcch		;811e	cd cc 0b	. . .
	or a			;8121	b7		.
	ld hl,(00036h)		;8122	2a 36 00	* 6 .
	ld de,(022e8h)		;8125	ed 5b e8 22	. [ . "
	sbc hl,de		;8129	ed 52		. R
	add hl,de		;812b	19		.
	push af			;812c	f5		.
	nop			;812d	00		.
	push hl			;812e	e5		.
	xor a			;812f	af		.
	ld (05cf9h),a		;8130	32 f9 5c	2 . \
	inc a			;8133	3c		<
	ld c,00eh		;8134	0e 0e		. .
	call 03d13h		;8136	cd 13 3d	. . =
	pop hl			;8139	e1		.
	ld bc,(05ce8h)		;813a	ed 4b e8 5c	. K . \
	ex de,hl		;813e	eb		.
	ld (hl),008h		;813f	36 08		6 .
	nop			;8141	00		.
	nop			;8142	00		.
	nop			;8143	00		.
	pop af			;8144	f1		.
	pop ix			;8145	dd e1		. .
	pop iy			;8147	fd e1		. .
	push af			;8149	f5		.
	jr l8179h		;814a	18 2d		. -
l814ch:
	ld de,022ach		;814c	11 ac 22	. . "
	ld c,003h		;814f	0e 03		. .
	call 00a57h		;8151	cd 57 0a	. W .
	push de			;8154	d5		.
	push ix			;8155	dd e5		. .
	ld ix,(00036h)		;8157	dd 2a 36 00	. * 6 .
	ld a,0ffh		;815b	3e ff		> .
	call 00a3dh		;815d	cd 3d 0a	. = .
	ld hl,(00036h)		;8160	2a 36 00	* 6 .
	ld de,(022e8h)		;8163	ed 5b e8 22	. [ . "
	or a			;8167	b7		.
	sbc hl,de		;8168	ed 52		. R
	add hl,de		;816a	19		.
	pop ix			;816b	dd e1		. .
	pop de			;816d	d1		.
	push af			;816e	f5		.
	add hl,de		;816f	19		.
	ld bc,00000h		;8170	01 00 00	. . .
	ld a,00dh		;8173	3e 0d		> .
	cpdr			;8175	ed b9		. .
	inc hl			;8177	23		#
	inc hl			;8178	23		#
l8179h:
	ld (00036h),hl		;8179	22 36 00	" 6 .
	pop af			;817c	f1		.
	ret z			;817d	c8		.
	ld hl,00001h		;817e	21 01 00	! . .
	ld (022dah),hl		;8181	22 da 22	" . "
	ld (022dch),hl		;8184	22 dc 22	" . "
	call 00626h		;8187	cd 26 06	. & .
	ret z			;818a	c8		.
	call 00c63h		;818b	cd 63 0c	. c .
	ret m			;818e	f8		.
	set 5,(ix+000h)		;818f	dd cb 00 ee	. . . .
	ld hl,(022dah)		;8193	2a da 22	* . "
	push hl			;8196	e5		.
	ld hl,(022e8h)		;8197	2a e8 22	* . "
	call 00d67h		;819a	cd 67 0d	. g .
l819dh:
	pop de			;819d	d1		.
	ret z			;819e	c8		.
	bit 5,(ix+000h)		;819f	dd cb 00 6e	. . . n
	jr z,l81a9h		;81a3	28 04		( .
	ld (hl),e		;81a5	73		s
	inc hl			;81a6	23		#
	ld (hl),d		;81a7	72		r
	dec hl			;81a8	2b		+
l81a9h:
	ex de,hl		;81a9	eb		.
	ld bc,(022dch)		;81aa	ed 4b dc 22	. K . "
	or a			;81ae	b7		.
	adc hl,bc		;81af	ed 4a		. J
	ret m			;81b1	f8		.
	push hl			;81b2	e5		.
	ex de,hl		;81b3	eb		.
	call 00d5dh		;81b4	cd 5d 0d	. ] .
	jr l819dh		;81b7	18 e4		. .
	ld bc,(022d6h)		;81b9	ed 4b d6 22	. K . "
	call 00d43h		;81bd	cd 43 0d	. C .
	ret nz			;81c0	c0		.
	inc hl			;81c1	23		#
	inc hl			;81c2	23		#
	call 00dadh		;81c3	cd ad 0d	. . .
	ld hl,(022d8h)		;81c6	2a d8 22	* . "
	jr l81d2h		;81c9	18 07		. .
	call 00d72h		;81cb	cd 72 0d	. r .
	ld hl,(022dah)		;81ce	2a da 22	* . "
	pop bc			;81d1	c1		.
l81d2h:
	ld de,0258ch		;81d2	11 8c 25	. . %
	ex de,hl		;81d5	eb		.
	push hl			;81d6	e5		.
	push de			;81d7	d5		.
	push hl			;81d8	e5		.
	ld a,(hl)		;81d9	7e		~
	cp 03bh			;81da	fe 3b		. ;
	jr z,l81f4h		;81dc	28 16		( .
	cp 02ah			;81de	fe 2a		. *
	jr z,l81f4h		;81e0	28 12		( .
	call 00d19h		;81e2	cd 19 0d	. . .
	jr nz,l81f4h		;81e5	20 0d		  .
	jr c,l81f4h		;81e7	38 0b		8 .
	call 00d24h		;81e9	cd 24 0d	. $ .
	call 00d19h		;81ec	cd 19 0d	. . .
	jr c,l81f4h		;81ef	38 03		8 .
	call z,00d24h		;81f1	cc 24 0d	. $ .
l81f4h:
	pop hl			;81f4	e1		.
	call 00d0ch		;81f5	cd 0c 0d	. . .
	ld d,000h		;81f8	16 00		. .
	ld e,a			;81fa	5f		_
	pop hl			;81fb	e1		.
	push hl			;81fc	e5		.
	push de			;81fd	d5		.
	ld b,h			;81fe	44		D
	ld c,l			;81ff	4d		M
	call 00d43h		;8200	cd 43 0d	. C .
	pop de			;8203	d1		.
	jr z,l8226h		;8204	28 20		(  
	ex de,hl		;8206	eb		.
	push hl			;8207	e5		.
	add hl,de		;8208	19		.
sub_8209h:
	inc hl			;8209	23		#
	inc hl			;820a	23		#
	ex de,hl		;820b	eb		.
	push hl			;820c	e5		.
l820dh:
	ld bc,(00036h)		;820d	ed 4b 36 00	. K 6 .
	call 00676h		;8211	cd 76 06	. v .
	ld (00036h),de		;8214	ed 53 36 00	. S 6 .
	pop hl			;8218	e1		.
	pop bc			;8219	c1		.
	pop de			;821a	d1		.
	ld (hl),e		;821b	73		s
	inc hl			;821c	23		#
	ld (hl),d		;821d	72		r
	inc hl			;821e	23		#
	ex de,hl		;821f	eb		.
	pop hl			;8220	e1		.
	push de			;8221	d5		.
	ldir			;8222	ed b0		. .
	pop hl			;8224	e1		.
	ret			;8225	c9		.
l8226h:
	push de			;8226	d5		.
	push hl			;8227	e5		.
	inc hl			;8228	23		#
	inc hl			;8229	23		#
	call 00d0ch		;822a	cd 0c 0d	. . .
	push hl			;822d	e5		.
	ex de,hl		;822e	eb		.
	ld e,a			;822f	5f		_
	ld d,000h		;8230	16 00		. .
	or a			;8232	b7		.
	sbc hl,de		;8233	ed 52		. R
	ex de,hl		;8235	eb		.
	pop hl			;8236	e1		.
	ex de,hl		;8237	eb		.
	add hl,de		;8238	19		.
	ex de,hl		;8239	eb		.
	jr l820dh		;823a	18 d1		. .
	ld a,00dh		;823c	3e 0d		> .
	push bc			;823e	c5		.
	ld bc,00000h		;823f	01 00 00	. . .
	cpir			;8242	ed b1		. .
	ld a,c			;8244	79		y
	neg			;8245	ed 44		. D
	pop bc			;8247	c1		.
	ret			;8248	c9		.
l8249h:
	ld a,(hl)		;8249	7e		~
	cp 020h			;824a	fe 20		.  
	ret z			;824c	c8		.
	cp 00dh			;824d	fe 0d		. .
	scf			;824f	37		7
	ret z			;8250	c8		.
	inc hl			;8251	23		#
	jr l8249h		;8252	18 f5		. .
	push hl			;8254	e5		.
l8255h:
	inc hl			;8255	23		#
	ld a,(hl)		;8256	7e		~
	cp 020h			;8257	fe 20		.  
	jr z,l8255h		;8259	28 fa		( .
	pop de			;825b	d1		.
	cp 00dh			;825c	fe 0d		. .
	ex de,hl		;825e	eb		.
	jr z,l8264h		;825f	28 03		( .
	ld (hl),009h		;8261	36 09		6 .
	inc hl			;8263	23		#
l8264h:
	push hl			;8264	e5		.
	ld hl,025dch		;8265	21 dc 25	! . %
	sbc hl,de		;8268	ed 52		. R
	ld b,h			;826a	44		D
	ld c,l			;826b	4d		M
	pop hl			;826c	e1		.
	push hl			;826d	e5		.
	ex de,hl		;826e	eb		.
	ldir			;826f	ed b0		. .
	pop hl			;8271	e1		.
	ret			;8272	c9		.
	ld hl,(022e8h)		;8273	2a e8 22	* . "
	call 00d67h		;8276	cd 67 0d	. g .
l8279h:
	jr nz,l827eh		;8279	20 03		  .
	sub 001h		;827b	d6 01		. .
	ret			;827d	c9		.
l827eh:
	ld e,(hl)		;827e	5e		^
	inc hl			;827f	23		#
	ld d,(hl)		;8280	56		V
	or a			;8281	b7		.
	ex de,hl		;8282	eb		.
	sbc hl,bc		;8283	ed 42		. B
	ex de,hl		;8285	eb		.
	dec hl			;8286	2b		+
	ret nc			;8287	d0		.
	call 00d5dh		;8288	cd 5d 0d	. ] .
	jr l8279h		;828b	18 ec		. .
	inc hl			;828d	23		#
	inc hl			;828e	23		#
	ld a,00dh		;828f	3e 0d		> .
	push bc			;8291	c5		.
	ld c,000h		;8292	0e 00		. .
	cpir			;8294	ed b1		. .
	pop bc			;8296	c1		.
	push de			;8297	d5		.
	ld de,(00036h)		;8298	ed 5b 36 00	. [ 6 .
	xor a			;829c	af		.
	sbc hl,de		;829d	ed 52		. R
	add hl,de		;829f	19		.
	pop de			;82a0	d1		.
	ret			;82a1	c9		.
l82a2h:
	call 00765h		;82a2	cd 65 07	. e .
	jr nz,l82a2h		;82a5	20 fb		  .
	jp 003b7h		;82a7	c3 b7 03	. . .
	call 00d72h		;82aa	cd 72 0d	. r .
	ld de,0258ch		;82ad	11 8c 25	. . %
	ld hl,022e3h		;82b0	21 e3 22	! . "
	ld a,02dh		;82b3	3e 2d		> -
	ld (hl),a		;82b5	77		w
	inc hl			;82b6	23		#
	ld (hl),050h		;82b7	36 50		6 P
	inc hl			;82b9	23		#
	ld (hl),000h		;82ba	36 00		6 .
	inc hl			;82bc	23		#
	ld (hl),e		;82bd	73		s
	inc hl			;82be	23		#
	ld (hl),d		;82bf	72		r
	ld hl,(022dah)		;82c0	2a da 22	* . "
	push de			;82c3	d5		.
	ld a,005h		;82c4	3e 05		> .
	call 0033eh		;82c6	cd 3e 03	. > .
	pop de			;82c9	d1		.
	jp 003bbh		;82ca	c3 bb 03	. . .
	pop hl			;82cd	e1		.
	jp 003b7h		;82ce	c3 b7 03	. . .
	call 00026h		;82d1	cd 26 00	. & .
	ld ix,022e0h		;82d4	dd 21 e0 22	. ! . "
	xor a			;82d8	af		.
	ld (ix+000h),a		;82d9	dd 77 00	. w .
	ret			;82dc	c9		.
	ld de,0258ch		;82dd	11 8c 25	. . %
	ld bc,0002dh		;82e0	01 2d 00	. - .
l82e3h:
	ld a,(hl)		;82e3	7e		~
	cp 009h			;82e4	fe 09		. .
	jr nz,l82fbh		;82e6	20 13		  .
	inc hl			;82e8	23		#
	ld b,c			;82e9	41		A
	call 00462h		;82ea	cd 62 04	. b .
l82edh:
	ld b,c			;82ed	41		A
	ld a,(0231ah)		;82ee	3a 1a 23	: . #
	cp b			;82f1	b8		.
	jr nc,l82fbh		;82f2	30 07		0 .
	ld a,020h		;82f4	3e 20		>  
	ld (de),a		;82f6	12		.
	inc de			;82f7	13		.
	dec c			;82f8	0d		.
	jr l82edh		;82f9	18 f2		. .
l82fbh:
	ld a,c			;82fb	79		y
	or a			;82fc	b7		.
	jr nz,l8301h		;82fd	20 02		  .
	ld c,01dh		;82ff	0e 1d		. .
l8301h:
	ld a,(hl)		;8301	7e		~
	ldi			;8302	ed a0		. .
	cp 00dh			;8304	fe 0d		. .
	jr nz,l82e3h		;8306	20 db		  .
	ret			;8308	c9		.
	ld hl,(022dah)		;8309	2a da 22	* . "
	ld a,h			;830c	7c		|
	or l			;830d	b5		.
	jr z,l8314h		;830e	28 04		( .
	ld (0233dh),hl		;8310	22 3d 23	" = #
	ret			;8313	c9		.
l8314h:
	ld hl,(0233dh)		;8314	2a 3d 23	* = #
	call 00d93h		;8317	cd 93 0d	. . .
	jp 003b7h		;831a	c3 b7 03	. . .
	ld hl,0247ch		;831d	21 7c 24	! | $
	ld d,00fh		;8320	16 0f		. .
	call 005d5h		;8322	cd d5 05	. . .
	jr nz,l833ah		;8325	20 13		  .
	ld de,00100h		;8327	11 00 01	. . .
	or a			;832a	b7		.
	sbc hl,de		;832b	ed 52		. R
	add hl,de		;832d	19		.
	jr nc,l8331h		;832e	30 01		0 .
	ex de,hl		;8330	eb		.
l8331h:
	ex de,hl		;8331	eb		.
	ld hl,02a3ah		;8332	21 3a 2a	! : *
	add hl,de		;8335	19		.
	ret c			;8336	d8		.
	ld (022ech),hl		;8337	22 ec 22	" . "
l833ah:
	ld hl,0248dh		;833a	21 8d 24	! . $
	ld d,00dh		;833d	16 0d		. .
	call 005d5h		;833f	cd d5 05	. . .
	ex de,hl		;8342	eb		.
	ld hl,(022ech)		;8343	2a ec 22	* . "
	add hl,de		;8346	19		.
	jp c,00660h		;8347	da 60 06	. ` .
	ex de,hl		;834a	eb		.
	push de			;834b	d5		.
	ld bc,(00036h)		;834c	ed 4b 36 00	. K 6 .
	ld hl,(022e8h)		;8350	2a e8 22	* . "
	sbc hl,bc		;8353	ed 42		. B
	jr z,l8364h		;8355	28 0d		( .
	add hl,bc		;8357	09		.
	call 00676h		;8358	cd 76 06	. v .
	pop hl			;835b	e1		.
l835ch:
	ld (022e8h),hl		;835c	22 e8 22	" . "
	ld (00036h),de		;835f	ed 53 36 00	. S 6 .
	ret			;8363	c9		.
l8364h:
	pop de			;8364	d1		.
	ld h,d			;8365	62		b
	ld l,e			;8366	6b		k
	jr l835ch		;8367	18 f3		. .
	ld hl,021aeh		;8369	21 ae 21	! . !
l836ch:
	ld a,(hl)		;836c	7e		~
	or a			;836d	b7		.
	ret z			;836e	c8		.
	cp 00dh			;836f	fe 0d		. .
	jr nz,l8375h		;8371	20 02		  .
	ld c,000h		;8373	0e 00		. .
l8375h:
	cp 006h			;8375	fe 06		. .
	jr nz,l8383h		;8377	20 0a		  .
l8379h:
	bit 4,c			;8379	cb 61		. a
	jr nz,l8386h		;837b	20 09		  .
	call 003bbh		;837d	cd bb 03	. . .
	inc c			;8380	0c		.
	jr l8379h		;8381	18 f6		. .
l8383h:
	call 0001dh		;8383	cd 1d 00	. . .
l8386h:
	inc hl			;8386	23		#
	inc c			;8387	0c		.
	jr l836ch		;8388	18 e2		. .
	ld hl,0228dh		;838a	21 8d 22	! . "
	call 00455h		;838d	cd 55 04	. U .
	call 00317h		;8390	cd 17 03	. . .
	and 0dfh		;8393	e6 df		. .
	cp 059h			;8395	fe 59		. Y
	jr nz,l839fh		;8397	20 06		  .
	ld hl,(022e8h)		;8399	2a e8 22	* . "
	ld (00036h),hl		;839c	22 36 00	" 6 .
l839fh:
	jp 003b7h		;839f	c3 b7 03	. . .
	call 00da1h		;83a2	cd a1 0d	. . .
	ld ix,0264eh		;83a5	dd 21 4e 26	. ! N &
	ld sp,ix		;83a9	dd f9		. .
	ld hl,022f4h		;83ab	21 f4 22	! . "
	xor a			;83ae	af		.
	ld (022eeh),a		;83af	32 ee 22	2 . "
	ld (ix+002h),a		;83b2	dd 77 02	. w .
	ld (ix+003h),a		;83b5	dd 77 03	. w .
	ld b,00ah		;83b8	06 0a		. .
l83bah:
	ld (hl),a		;83ba	77		w
	inc hl			;83bb	23		#
	djnz l83bah		;83bc	10 fc		. .
	ld hl,(022e8h)		;83be	2a e8 22	* . "
	ld (02302h),hl		;83c1	22 02 23	" . #
	ld hl,(022ech)		;83c4	2a ec 22	* . "
	ld (022eah),hl		;83c7	22 ea 22	" . "
	push ix			;83ca	dd e5		. .
	call 00da1h		;83cc	cd a1 0d	. . .
	ld hl,(022dch)		;83cf	2a dc 22	* . "
	ld a,h			;83d2	7c		|
	or l			;83d3	b5		.
	ld de,(00036h)		;83d4	ed 5b 36 00	. [ 6 .
	inc de			;83d8	13		.
	inc de			;83d9	13		.
	ld (02300h),de		;83da	ed 53 00 23	. S . #
	jr nz,l83f5h		;83de	20 15		  .
	ex de,hl		;83e0	eb		.
	push hl			;83e1	e5		.
	ld de,(022e8h)		;83e2	ed 5b e8 22	. [ . "
	sbc hl,de		;83e6	ed 52		. R
	ld b,003h		;83e8	06 03		. .
l83eah:
	srl h			;83ea	cb 3c		. <
	rr l			;83ec	cb 1d		. .
	djnz l83eah		;83ee	10 fa		. .
	ld de,00064h		;83f0	11 64 00	. d .
	add hl,de		;83f3	19		.
	pop de			;83f4	d1		.
l83f5h:
	add hl,de		;83f5	19		.
sub_83f6h:
	jp c,019d0h		;83f6	da d0 19	. . .
	ld de,(0233dh)		;83f9	ed 5b 3d 23	. [ = #
	sbc hl,de		;83fd	ed 52		. R
	add hl,de		;83ff	19		.
	jp nc,019d0h		;8400	d2 d0 19	. . .
	ld (022feh),hl		;8403	22 fe 22	" . "
l8406h:
	ld (02321h),hl		;8406	22 21 23	" ! #
	ld (02341h),hl		;8409	22 41 23	" A #
	ld (0231dh),hl		;840c	22 1d 23	" . #
	ld hl,(022dah)		;840f	2a da 22	* . "
	pop ix			;8412	dd e1		. .
	ld l,004h		;8414	2e 04		. .
	ld a,(022dah)		;8416	3a da 22	: . "
	or a			;8419	b7		.
	jr z,l841fh		;841a	28 03		( .
	xor 004h		;841c	ee 04		. .
	ld l,a			;841e	6f		o
l841fh:
	ld a,l			;841f	7d		}
	pop hl			;8420	e1		.
	bit 0,a			;8421	cb 47		. G
	jr z,l8429h		;8423	28 04		( .
	set 2,(ix+002h)		;8425	dd cb 02 d6	. . . .
l8429h:
	and 03eh		;8429	e6 3e		. >
	or 080h			;842b	f6 80		. .
	ld h,a			;842d	67		g
	ld l,00bh		;842e	2e 0b		. .
	push hl			;8430	e5		.
	ld a,010h		;8431	3e 10		> .
	bit 3,(ix+001h)		;8433	dd cb 01 5e	. . . ^
	call nz,0001dh		;8437	c4 1d 00	. . .
	ld hl,024d3h		;843a	21 d3 24	! . $
	ld de,(02300h)		;843d	ed 5b 00 23	. [ . #
	ld bc,0000dh		;8441	01 0d 00	. . .
	ldir			;8444	ed b0		. .
	ex de,hl		;8446	eb		.
	ld (02323h),hl		;8447	22 23 23	" # #
	ld hl,02517h		;844a	21 17 25	! . %
	call 00455h		;844d	cd 55 04	. U .
	ld hl,(022feh)		;8450	2a fe 22	* . "
	ld (0230ch),hl		;8453	22 0c 23	" . #
	ld (0230eh),hl		;8456	22 0e 23	" . #
	push hl			;8459	e5		.
	call 011f9h		;845a	cd f9 11	. . .
	ld hl,(02321h)		;845d	2a 21 23	* ! #
	ld de,(02341h)		;8460	ed 5b 41 23	. [ A #
	or a			;8464	b7		.
	sbc hl,de		;8465	ed 52		. R
	ld (022f6h),hl		;8467	22 f6 22	" . "
	pop hl			;846a	e1		.
	ld (02321h),hl		;846b	22 21 23	" ! #
	pop hl			;846e	e1		.
	ld a,h			;846f	7c		|
	and 00dh		;8470	e6 0d		. .
	ld l,a			;8472	6f		o
	res 7,h			;8473	cb bc		. .
	res 0,h			;8475	cb 84		. .
	push hl			;8477	e5		.
	ld hl,024b2h		;8478	21 b2 24	! . $
	call 00455h		;847b	cd 55 04	. U .
	ld hl,024c0h		;847e	21 c0 24	! . $
	call 01773h		;8481	cd 73 17	. s .
	call 003b7h		;8484	cd b7 03	. . .
	ld a,(ix+002h)		;8487	dd 7e 02	. ~ .
	and 004h		;848a	e6 04		. .
	ld (ix+002h),a		;848c	dd 77 02	. w .
	xor a			;848f	af		.
	ld (ix+003h),a		;8490	dd 77 03	. w .
	or b			;8493	b0		.
	jr z,l849eh		;8494	28 08		( .
	ld hl,00000h		;8496	21 00 00	! . .
	ld (0233fh),hl		;8499	22 3f 23	" ? #
	jr l8505h		;849c	18 67		. g
l849eh:
	ld hl,(022feh)		;849e	2a fe 22	* . "
	ld (0230ch),hl		;84a1	22 0c 23	" . #
	ld (0230eh),hl		;84a4	22 0e 23	" . #
	ld bc,(0233fh)		;84a7	ed 4b 3f 23	. K ? #
	ld a,b			;84ab	78		x
	or c			;84ac	b1		.
	jr z,l84ech		;84ad	28 3d		( =
	ld a,004h		;84af	3e 04		> .
	ld hl,00000h		;84b1	21 00 00	! . .
	push ix			;84b4	dd e5		. .
	call 00b83h		;84b6	cd 83 0b	. . .
	ld hl,(02331h)		;84b9	2a 31 23	* 1 #
	ld (02335h),hl		;84bc	22 35 23	" 5 #
	ld hl,(0232fh)		;84bf	2a 2f 23	* / #
	ld (02333h),hl		;84c2	22 33 23	" 3 #
	ld hl,(022f6h)		;84c5	2a f6 22	* . "
	ld (05ce8h),hl		;84c8	22 e8 5c	" . \
	ld hl,(02341h)		;84cb	2a 41 23	* A #
	ld (05ce6h),hl		;84ce	22 e6 5c	" . \
	call 0265bh		;84d1	cd 5b 26	. [ &
	ld (0232dh),ix		;84d4	dd 22 2d 23	. " - #
	ld (05c51h),ix		;84d8	dd 22 51 5c	. " Q \
	nop			;84dc	00		.
	nop			;84dd	00		.
	nop			;84de	00		.
	nop			;84df	00		.
	nop			;84e0	00		.
	nop			;84e1	00		.
	nop			;84e2	00		.
	nop			;84e3	00		.
	nop			;84e4	00		.
	nop			;84e5	00		.
	nop			;84e6	00		.
	nop			;84e7	00		.
	nop			;84e8	00		.
	nop			;84e9	00		.
	pop ix			;84ea	dd e1		. .
l84ech:
	xor a			;84ec	af		.
	ld h,a			;84ed	67		g
	ld l,a			;84ee	6f		o
	ld a,0c9h		;84ef	3e c9		> .
	ld (022f6h),a		;84f1	32 f6 22	2 . "
	push hl			;84f4	e5		.
	ld hl,(022e8h)		;84f5	2a e8 22	* . "
	ld (02302h),hl		;84f8	22 02 23	" . #
	call 011f9h		;84fb	cd f9 11	. . .
	ld hl,024b9h		;84fe	21 b9 24	! . $
	call 01773h		;8501	cd 73 17	. s .
	pop hl			;8504	e1		.
l8505h:
	res 1,(ix+002h)		;8505	dd cb 02 8e	. . . .
	call 003b7h		;8509	cd b7 03	. . .
	ld hl,(02300h)		;850c	2a 00 23	* . #
	ld de,0000dh		;850f	11 0d 00	. . .
	add hl,de		;8512	19		.
l8513h:
	ld de,(02323h)		;8513	ed 5b 23 23	. [ # #
	or a			;8517	b7		.
	sbc hl,de		;8518	ed 52		. R
	add hl,de		;851a	19		.
	jr nc,l8563h		;851b	30 46		0 F
	ld de,00004h		;851d	11 04 00	. . .
	add hl,de		;8520	19		.
	push hl			;8521	e5		.
	bit 0,(hl)		;8522	cb 46		. F
	jr nz,l8550h		;8524	20 2a		  *
	push hl			;8526	e5		.
	ld hl,024a7h		;8527	21 a7 24	! . $
	ld de,0258ch		;852a	11 8c 25	. . %
	call 0102bh		;852d	cd 2b 10	. + .
	ex de,hl		;8530	eb		.
	ex (sp),hl		;8531	e3		.
l8532h:
	inc hl			;8532	23		#
	ld a,(hl)		;8533	7e		~
	res 7,a			;8534	cb bf		. .
	ex (sp),hl		;8536	e3		.
	ld (hl),a		;8537	77		w
	inc hl			;8538	23		#
	ex (sp),hl		;8539	e3		.
	bit 7,(hl)		;853a	cb 7e		. ~
	jr z,l8532h		;853c	28 f4		( .
	ex (sp),hl		;853e	e3		.
	ld (hl),020h		;853f	36 20		6  
	inc hl			;8541	23		#
	ex de,hl		;8542	eb		.
	ld hl,024cch		;8543	21 cc 24	! . $
	call 0102bh		;8546	cd 2b 10	. + .
	ex de,hl		;8549	eb		.
	ld (hl),0a0h		;854a	36 a0		6 .
	call 0173eh		;854c	cd 3e 17	. > .
	pop hl			;854f	e1		.
l8550h:
	pop hl			;8550	e1		.
l8551h:
	inc hl			;8551	23		#
	bit 7,(hl)		;8552	cb 7e		. ~
	jr z,l8551h		;8554	28 fb		( .
	inc hl			;8556	23		#
	inc hl			;8557	23		#
	inc hl			;8558	23		#
	jr l8513h		;8559	18 b8		. .
l855bh:
	ld a,(hl)		;855b	7e		~
	or a			;855c	b7		.
	ret z			;855d	c8		.
	ld (de),a		;855e	12		.
	inc hl			;855f	23		#
	inc de			;8560	13		.
	jr l855bh		;8561	18 f8		. .
l8563h:
	ld de,0258ch		;8563	11 8c 25	. . %
	bit 2,(ix+002h)		;8566	dd cb 02 56	. . . V
	jp z,01114h		;856a	ca 14 11	. . .
	ld hl,0258ch		;856d	21 8c 25	! . %
	ld (hl),0a0h		;8570	36 a0		6 .
	ld hl,(02300h)		;8572	2a 00 23	* . #
	push hl			;8575	e5		.
	ld de,0000dh		;8576	11 0d 00	. . .
	add hl,de		;8579	19		.
	ld de,(02323h)		;857a	ed 5b 23 23	. [ # #
	or a			;857e	b7		.
	sbc hl,de		;857f	ed 52		. R
	pop hl			;8581	e1		.
	ld de,0258ch		;8582	11 8c 25	. . %
	jp nc,0110eh		;8585	d2 0e 11	. . .
	ld a,(00032h)		;8588	3a 32 00	: 2 .
	push af			;858b	f5		.
l858ch:
	push de			;858c	d5		.
	ld e,(hl)		;858d	5e		^
	inc hl			;858e	23		#
	ld d,(hl)		;858f	56		V
	ld a,e			;8590	7b		{
	or d			;8591	b2		.
	jr z,l859bh		;8592	28 07		( .
	ld hl,0fffbh		;8594	21 fb ff	! . .
	add hl,de		;8597	19		.
	pop de			;8598	d1		.
	jr l858ch		;8599	18 f1		. .
l859bh:
	pop de			;859b	d1		.
	inc hl			;859c	23		#
	push hl			;859d	e5		.
	inc hl			;859e	23		#
	inc hl			;859f	23		#
	ld b,007h		;85a0	06 07		. .
l85a2h:
	inc hl			;85a2	23		#
	dec b			;85a3	05		.
	ld a,(hl)		;85a4	7e		~
	res 7,a			;85a5	cb bf		. .
	ld (de),a		;85a7	12		.
	inc de			;85a8	13		.
	bit 7,(hl)		;85a9	cb 7e		. ~
	jr z,l85a2h		;85ab	28 f5		( .
	ld a,020h		;85ad	3e 20		>  
l85afh:
	ld (de),a		;85af	12		.
	inc de			;85b0	13		.
	djnz l85afh		;85b1	10 fc		. .
	inc hl			;85b3	23		#
	push de			;85b4	d5		.
	ld e,(hl)		;85b5	5e		^
	inc hl			;85b6	23		#
	ld d,(hl)		;85b7	56		V
	pop hl			;85b8	e1		.
	call 01e0bh		;85b9	cd 0b 1e	. . .
	ex de,hl		;85bc	eb		.
	ld a,020h		;85bd	3e 20		>  
	ld b,004h		;85bf	06 04		. .
l85c1h:
	ld (de),a		;85c1	12		.
	inc de			;85c2	13		.
	djnz l85c1h		;85c3	10 fc		. .
	pop hl			;85c5	e1		.
	pop af			;85c6	f1		.
	dec a			;85c7	3d		=
	push af			;85c8	f5		.
	jr nz,l85d3h		;85c9	20 08		  .
	call 0171ch		;85cb	cd 1c 17	. . .
	pop af			;85ce	f1		.
	ld a,(00032h)		;85cf	3a 32 00	: 2 .
	push af			;85d2	f5		.
l85d3h:
	push de			;85d3	d5		.
	ld e,(hl)		;85d4	5e		^
	inc hl			;85d5	23		#
	ld d,(hl)		;85d6	56		V
	ld a,d			;85d7	7a		z
	or e			;85d8	b3		.
	jr z,l85e9h		;85d9	28 0e		( .
l85dbh:
	ld hl,0fffbh		;85db	21 fb ff	! . .
	add hl,de		;85de	19		.
	ld e,(hl)		;85df	5e		^
	inc hl			;85e0	23		#
	ld d,(hl)		;85e1	56		V
	ld a,d			;85e2	7a		z
	or e			;85e3	b3		.
	jp z,0106bh		;85e4	ca 6b 10	. k .
	jr l85dbh		;85e7	18 f2		. .
l85e9h:
	inc hl			;85e9	23		#
	inc hl			;85ea	23		#
	push hl			;85eb	e5		.
	ld hl,022fah		;85ec	21 fa 22	! . "
	res 1,(hl)		;85ef	cb 8e		. .
l85f1h:
	ld hl,(02300h)		;85f1	2a 00 23	* . #
l85f4h:
	ld de,(02323h)		;85f4	ed 5b 23 23	. [ # #
	or a			;85f8	b7		.
	sbc hl,de		;85f9	ed 52		. R
	add hl,de		;85fb	19		.
	ld a,(022fah)		;85fc	3a fa 22	: . "
	jr nc,l8623h		;85ff	30 22		0 "
	bit 1,a			;8601	cb 4f		. O
	jr z,l8607h		;8603	28 02		( .
	inc hl			;8605	23		#
	inc hl			;8606	23		#
l8607h:
	ld e,(hl)		;8607	5e		^
	inc hl			;8608	23		#
	ld d,(hl)		;8609	56		V
	ex (sp),hl		;860a	e3		.
	or a			;860b	b7		.
	sbc hl,de		;860c	ed 52		. R
	add hl,de		;860e	19		.
	ex (sp),hl		;860f	e3		.
	jr z,l862ah		;8610	28 18		( .
	bit 1,a			;8612	cb 4f		. O
	jr nz,l8618h		;8614	20 02		  .
	inc hl			;8616	23		#
	inc hl			;8617	23		#
l8618h:
	inc hl			;8618	23		#
l8619h:
	inc hl			;8619	23		#
	bit 7,(hl)		;861a	cb 7e		. ~
	jr z,l8619h		;861c	28 fb		( .
	inc hl			;861e	23		#
	inc hl			;861f	23		#
	inc hl			;8620	23		#
	jr l85f4h		;8621	18 d1		. .
l8623h:
	set 1,a			;8623	cb cf		. .
	ld (022fah),a		;8625	32 fa 22	2 . "
	jr l85f1h		;8628	18 c7		. .
l862ah:
	bit 1,a			;862a	cb 4f		. O
	pop de			;862c	d1		.
	jr nz,l85e9h		;862d	20 ba		  .
	push hl			;862f	e5		.
	dec hl			;8630	2b		+
	ld de,(02300h)		;8631	ed 5b 00 23	. [ . #
	or a			;8635	b7		.
	sbc hl,de		;8636	ed 52		. R
	pop hl			;8638	e1		.
	jp nz,0106bh		;8639	c2 6b 10	. k .
	pop de			;863c	d1		.
	pop af			;863d	f1		.
	call 0171ch		;863e	cd 1c 17	. . .
	call 003b7h		;8641	cd b7 03	. . .
	ld hl,(0233fh)		;8644	2a 3f 23	* ? #
	ld a,h			;8647	7c		|
	or l			;8648	b5		.
	ld hl,(0230ch)		;8649	2a 0c 23	* . #
	jr z,l865bh		;864c	28 0d		( .
	call 01d8eh		;864e	cd 8e 1d	. . .
	ld hl,(02333h)		;8651	2a 33 23	* 3 #
	ld bc,(02335h)		;8654	ed 4b 35 23	. K 5 #
	call 00ab8h		;8658	cd b8 0a	. . .
l865bh:
	ld hl,024e0h		;865b	21 e0 24	! . $
	call 00455h		;865e	cd 55 04	. U .
	ld hl,(02323h)		;8661	2a 23 23	* # #
	ld de,(02300h)		;8664	ed 5b 00 23	. [ . #
	push de			;8668	d5		.
	sbc hl,de		;8669	ed 52		. R
	call 00d93h		;866b	cd 93 0d	. . .
	ld hl,024edh		;866e	21 ed 24	! . $
	call 00455h		;8671	cd 55 04	. U .
	ld hl,(0231dh)		;8674	2a 1d 23	* . #
	pop de			;8677	d1		.
	sbc hl,de		;8678	ed 52		. R
	call 00d93h		;867a	cd 93 0d	. . .
	ld a,(022f6h)		;867d	3a f6 22	: . "
	cp 0c3h			;8680	fe c3		. .
	jr nz,l8690h		;8682	20 0c		  .
	ld hl,024f4h		;8684	21 f4 24	! . $
	call 00455h		;8687	cd 55 04	. U .
	ld hl,(022f7h)		;868a	2a f7 22	* . "
	call 00d93h		;868d	cd 93 0d	. . .
l8690h:
	call 003b7h		;8690	cd b7 03	. . .
	jp 0002fh		;8693	c3 2f 00	. / .
	ld hl,(022e8h)		;8696	2a e8 22	* . "
	ld de,02a3ah		;8699	11 3a 2a	. : *
	or a			;869c	b7		.
	sbc hl,de		;869d	ed 52		. R
	ex de,hl		;869f	eb		.
	ret			;86a0	c9		.
	call 01166h		;86a1	cd 66 11	. f .
	push hl			;86a4	e5		.
	ld a,0ffh		;86a5	3e ff		> .
	call 00a31h		;86a7	cd 31 0a	. 1 .
	pop hl			;86aa	e1		.
	ld (02302h),hl		;86ab	22 02 23	" . #
	ret			;86ae	c9		.
	ei			;86af	fb		.
	ld l,(ix+01ah)		;86b0	dd 6e 1a	. n .
	ld h,(ix+01bh)		;86b3	dd 66 1b	. f .
	ld (02316h),hl		;86b6	22 16 23	" . #
	ld c,l			;86b9	4d		M
	ld b,h			;86ba	44		D
	push ix			;86bb	dd e5		. .
	pop hl			;86bd	e1		.
	ld de,0001ch		;86be	11 1c 00	. . .
	add hl,de		;86c1	19		.
	ld (02314h),hl		;86c2	22 14 23	" . #
	ret			;86c5	c9		.
	ld a,b			;86c6	78		x
	or c			;86c7	b1		.
	ret nz			;86c8	c0		.
	ld ix,(0232bh)		;86c9	dd 2a 2b 23	. * + #
	or (ix+00fh)		;86cd	dd b6 0f	. . .
	jr z,l86e3h		;86d0	28 11		( .
	ld a,(ix+00dh)		;86d2	dd 7e 0d	. ~ .
	or (ix+00eh)		;86d5	dd b6 0e	. . .
	jr z,l86e3h		;86d8	28 09		( .
	push de			;86da	d5		.
	call 02655h		;86db	cd 55 26	. U &
	call 0117fh		;86de	cd 7f 11	. . .
	pop de			;86e1	d1		.
	ret			;86e2	c9		.
l86e3h:
	pop hl			;86e3	e1		.
	jr l8723h		;86e4	18 3d		. =
	ld de,02a3ah		;86e6	11 3a 2a	. : *
	ld (02302h),de		;86e9	ed 53 02 23	. S . #
	ld bc,(02316h)		;86ed	ed 4b 16 23	. K . #
	ld hl,(02314h)		;86f1	2a 14 23	* . #
l86f4h:
	call 01196h		;86f4	cd 96 11	. . .
	ldi			;86f7	ed a0		. .
	call 01196h		;86f9	cd 96 11	. . .
l86fch:
	ldi			;86fc	ed a0		. .
	call 01196h		;86fe	cd 96 11	. . .
	ld a,(hl)		;8701	7e		~
	cp 00dh			;8702	fe 0d		. .
	jr nz,l86fch		;8704	20 f6		  .
	ldi			;8706	ed a0		. .
	push hl			;8708	e5		.
	push de			;8709	d5		.
	ld hl,(022ech)		;870a	2a ec 22	* . "
	ld de,0005ah		;870d	11 5a 00	. Z .
	or a			;8710	b7		.
	sbc hl,de		;8711	ed 52		. R
	pop de			;8713	d1		.
	sbc hl,de		;8714	ed 52		. R
	pop hl			;8716	e1		.
	jr nc,l86f4h		;8717	30 db		0 .
	ld (02314h),hl		;8719	22 14 23	" . #
	ld (02316h),bc		;871c	ed 43 16 23	. C . #
	xor a			;8720	af		.
	jr l8725h		;8721	18 02		. .
l8723h:
	ld a,0ffh		;8723	3e ff		> .
l8725h:
	ld (de),a		;8725	12		.
	inc de			;8726	13		.
	ld (de),a		;8727	12		.
	ret			;8728	c9		.
	call 019f4h		;8729	cd f4 19	. . .
	ret nz			;872c	c0		.
	ld (02308h),hl		;872d	22 08 23	" . #
	push hl			;8730	e5		.
	call 01698h		;8731	cd 98 16	. . .
	ex (sp),hl		;8734	e3		.
	push de			;8735	d5		.
	ld a,003h		;8736	3e 03		> .
	jp nz,01dbbh		;8738	c2 bb 1d	. . .
	ld a,d			;873b	7a		z
	cp 00dh			;873c	fe 0d		. .
	jr z,l8748h		;873e	28 08		( .
	cp 02ah			;8740	fe 2a		. *
	jp nz,01311h		;8742	c2 11 13	. . .
	call 01222h		;8745	cd 22 12	. " .
l8748h:
	bit 0,(ix+000h)		;8748	dd cb 00 46	. . . F
	call z,0172eh		;874c	cc 2e 17	. . .
	jp 0163ah		;874f	c3 3a 16	. : .
	bit 0,(ix+002h)		;8752	dd cb 02 46	. . . F
	ret nz			;8756	c0		.
	ld a,(ix+001h)		;8757	dd 7e 01	. ~ .
	rrca			;875a	0f		.
	ret c			;875b	d8		.
	ld b,a			;875c	47		G
	inc hl			;875d	23		#
	ld a,(hl)		;875e	7e		~
	cp 046h			;875f	fe 46		. F
	ld a,b			;8761	78		x
	jp nz,012a4h		;8762	c2 a4 12	. . .
	inc hl			;8765	23		#
	ld a,00dh		;8766	3e 0d		> .
	cp (hl)			;8768	be		.
	jr z,l876ch		;8769	28 01		( .
	inc hl			;876b	23		#
l876ch:
	ld d,h			;876c	54		T
	ld e,l			;876d	5d		]
	ld bc,00000h		;876e	01 00 00	. . .
	cpir			;8771	ed b1		. .
	ld (02312h),hl		;8773	22 12 23	" . #
	ld a,(de)		;8776	1a		.
	cp 00dh			;8777	fe 0d		. .
	jr z,l87ceh		;8779	28 53		( S
	inc de			;877b	13		.
	ld a,(de)		;877c	1a		.
	dec de			;877d	1b		.
	cp 03ah			;877e	fe 3a		. :
	jr nz,l87b4h		;8780	20 32		  2
	ex de,hl		;8782	eb		.
	bit 3,(ix+002h)		;8783	dd cb 02 5e	. . . ^
	ret nz			;8787	c0		.
	set 3,(ix+002h)		;8788	dd cb 02 de	. . . .
	push iy			;878c	fd e5		. .
	push ix			;878e	dd e5		. .
	ld iy,05c3ah		;8790	fd 21 3a 5c	. ! : \
	call 00ac3h		;8794	cd c3 0a	. . .
	bit 7,c			;8797	cb 79		. y
	jp nz,00bd4h		;8799	c2 d4 0b	. . .
	call 02652h		;879c	cd 52 26	. R &
	ld (0232bh),ix		;879f	dd 22 2b 23	. " + #
	nop			;87a3	00		.
	nop			;87a4	00		.
	nop			;87a5	00		.
	nop			;87a6	00		.
	nop			;87a7	00		.
	call 0117fh		;87a8	cd 7f 11	. . .
	call 011b6h		;87ab	cd b6 11	. . .
	pop ix			;87ae	dd e1		. .
	pop iy			;87b0	fd e1		. .
	jr l87ceh		;87b2	18 1a		. .
l87b4h:
	set 0,(ix+002h)		;87b4	dd cb 02 c6	. . . .
	ld a,(ix+001h)		;87b8	dd 7e 01	. ~ .
	ld (02319h),a		;87bb	32 19 23	2 . #
	set 2,(ix+001h)		;87be	dd cb 01 d6	. . . .
	ld c,004h		;87c2	0e 04		. .
	call 00a57h		;87c4	cd 57 0a	. W .
	ex de,hl		;87c7	eb		.
	ld (02310h),hl		;87c8	22 10 23	" . #
	call 01171h		;87cb	cd 71 11	. q .
l87ceh:
	pop bc			;87ce	c1		.
	pop bc			;87cf	c1		.
	pop bc			;87d0	c1		.
	jp 011f9h		;87d1	c3 f9 11	. . .
	rlca			;87d4	07		.
	rlca			;87d5	07		.
	ret c			;87d6	d8		.
	ld a,(hl)		;87d7	7e		~
	cp 043h			;87d8	fe 43		. C
	jr nz,l87eah		;87da	20 0e		  .
	inc hl			;87dc	23		#
	ld a,(hl)		;87dd	7e		~
	res 1,(ix+002h)		;87de	dd cb 02 8e	. . . .
	cp 02dh			;87e2	fe 2d		. -
	ret nz			;87e4	c0		.
	set 1,(ix+002h)		;87e5	dd cb 02 ce	. . . .
	ret			;87e9	c9		.
l87eah:
	cp 044h			;87ea	fe 44		. D
	jr nz,l87fch		;87ec	20 0e		  .
	inc hl			;87ee	23		#
	ld a,(hl)		;87ef	7e		~
	res 7,(ix+000h)		;87f0	dd cb 00 be	. . . .
	cp 02dh			;87f4	fe 2d		. -
	ret z			;87f6	c8		.
	set 7,(ix+000h)		;87f7	dd cb 00 fe	. . . .
	ret			;87fb	c9		.
l87fch:
	cp 04ch			;87fc	fe 4c		. L
	jr nz,l880eh		;87fe	20 0e		  .
	inc hl			;8800	23		#
	ld a,(hl)		;8801	7e		~
	res 2,(ix+001h)		;8802	dd cb 01 96	. . . .
	cp 02dh			;8806	fe 2d		. -
	ret nz			;8808	c0		.
	set 2,(ix+001h)		;8809	dd cb 01 d6	. . . .
	ret			;880d	c9		.
l880eh:
	cp 04dh			;880e	fe 4d		. M
	jr nz,l8820h		;8810	20 0e		  .
	inc hl			;8812	23		#
	ld a,(hl)		;8813	7e		~
	res 4,(ix+002h)		;8814	dd cb 02 a6	. . . .
	cp 02dh			;8818	fe 2d		. -
	ret z			;881a	c8		.
	set 4,(ix+002h)		;881b	dd cb 02 e6	. . . .
	ret			;881f	c9		.
l8820h:
	cp 048h			;8820	fe 48		. H
	jr nz,l882ah		;8822	20 06		  .
	inc hl			;8824	23		#
	ld (022fch),hl		;8825	22 fc 22	" . "
	jr l882eh		;8828	18 04		. .
l882ah:
	cp 045h			;882a	fe 45		. E
	jr nz,l8836h		;882c	20 08		  .
l882eh:
	bit 2,(ix+001h)		;882e	dd cb 01 56	. . . V
	call z,003a1h		;8832	cc a1 03	. . .
	ret			;8835	c9		.
l8836h:
	cp 053h			;8836	fe 53		. S
	ret nz			;8838	c0		.
	bit 2,(ix+001h)		;8839	dd cb 01 56	. . . V
	ret nz			;883d	c0		.
	jp 00317h		;883e	c3 17 03	. . .
	pop af			;8841	f1		.
	push af			;8842	f5		.
	ld hl,00000h		;8843	21 00 00	! . .
	push hl			;8846	e5		.
	push hl			;8847	e5		.
	push hl			;8848	e5		.
	jr nc,l8862h		;8849	30 17		0 .
	ld b,l			;884b	45		E
	ld de,025a0h		;884c	11 a0 25	. . %
	ld a,(de)		;884f	1a		.
	bit 0,(ix+001h)		;8850	dd cb 01 46	. . . F
	jr nz,l8862h		;8854	20 0c		  .
	set 6,(ix+001h)		;8856	dd cb 01 f6	. . . .
	call 01869h		;885a	cd 69 18	. i .
	ld a,007h		;885d	3e 07		> .
l885fh:
	jp c,01dbbh		;885f	da bb 1d	. . .
l8862h:
	cp 03bh			;8862	fe 3b		. ;
	jp z,0151bh		;8864	ca 1b 15	. . .
	call 01abbh		;8867	cd bb 1a	. . .
	ld hl,(02321h)		;886a	2a 21 23	* ! #
	call 01701h		;886d	cd 01 17	. . .
	ld hl,01ed2h		;8870	21 d2 1e	! . .
	push hl			;8873	e5		.
	ld d,000h		;8874	16 00		. .
	push de			;8876	d5		.
	ld hl,025a6h		;8877	21 a6 25	! . %
	call 01b07h		;887a	cd 07 1b	. . .
	bit 3,c			;887d	cb 59		. Y
	jp nz,015e5h		;887f	c2 e5 15	. . .
	ld (02306h),hl		;8882	22 06 23	" . #
	jp 013e0h		;8885	c3 e0 13	. . .
	call 01794h		;8888	cd 94 17	. . .
	jp nc,013ddh		;888b	d2 dd 13	. . .
	bit 0,(ix+001h)		;888e	dd cb 01 46	. . . F
	jr nz,l890dh		;8892	20 79		  y
	ld a,018h		;8894	3e 18		> .
	bit 6,(ix+002h)		;8896	dd cb 02 76	. . . v
	jr nz,l88c2h		;889a	20 26		  &
	bit 5,(ix+002h)		;889c	dd cb 02 6e	. . . n
	jr nz,l88c2h		;88a0	20 20		   
	ld de,(02306h)		;88a2	ed 5b 06 23	. [ . #
	set 7,(ix+002h)		;88a6	dd cb 02 fe	. . . .
	call 01869h		;88aa	cd 69 18	. i .
	res 7,(ix+002h)		;88ad	dd cb 02 be	. . . .
	ld a,007h		;88b1	3e 07		> .
	jr c,l885fh		;88b3	38 aa		8 .
	push hl			;88b5	e5		.
	ld hl,(022f4h)		;88b6	2a f4 22	* . "
	ld a,(hl)		;88b9	7e		~
	srl a			;88ba	cb 3f		. ?
	srl a			;88bc	cb 3f		. ?
	jr c,l88c5h		;88be	38 05		8 .
	ld a,002h		;88c0	3e 02		> .
l88c2h:
	jp 01dbbh		;88c2	c3 bb 1d	. . .
l88c5h:
	ld hl,(022ech)		;88c5	2a ec 22	* . "
	jr z,l88d4h		;88c8	28 0a		( .
	ld b,a			;88ca	47		G
l88cbh:
	xor a			;88cb	af		.
	push bc			;88cc	c5		.
	ld b,a			;88cd	47		G
	ld c,a			;88ce	4f		O
	cpir			;88cf	ed b1		. .
	pop bc			;88d1	c1		.
	djnz l88cbh		;88d2	10 f7		. .
l88d4h:
	set 6,(ix+002h)		;88d4	dd cb 02 f6	. . . .
	push hl			;88d8	e5		.
	ld hl,02343h		;88d9	21 43 23	! C #
	ld (022efh),hl		;88dc	22 ef 22	" . "
	ld hl,(02302h)		;88df	2a 02 23	* . #
	ld (02304h),hl		;88e2	22 04 23	" . #
	pop hl			;88e5	e1		.
	ld (02302h),hl		;88e6	22 02 23	" . #
	pop hl			;88e9	e1		.
	call 01b7dh		;88ea	cd 7d 1b	. } .
	bit 3,c			;88ed	cb 59		. Y
	jp nz,015e5h		;88ef	c2 e5 15	. . .
l88f2h:
	ld b,004h		;88f2	06 04		. .
	call 01b8ah		;88f4	cd 8a 1b	. . .
	push hl			;88f7	e5		.
	ld hl,(022efh)		;88f8	2a ef 22	* . "
	ld (hl),e		;88fb	73		s
	inc hl			;88fc	23		#
	ld (hl),d		;88fd	72		r
	inc hl			;88fe	23		#
	ld (022efh),hl		;88ff	22 ef 22	" . "
	pop hl			;8902	e1		.
	jp nc,015e5h		;8903	d2 e5 15	. . .
	jr l88f2h		;8906	18 ea		. .
l8908h:
	ld a,002h		;8908	3e 02		> .
	jp c,0132fh		;890a	da 2f 13	. / .
l890dh:
	call 01b07h		;890d	cd 07 1b	. . .
	jp c,01358h		;8910	da 58 13	. X .
	call 01b80h		;8913	cd 80 1b	. . .
	bit 7,(ix-012h)		;8916	dd cb ee 7e	. . . ~
	jp z,01571h		;891a	ca 71 15	. q .
	push af			;891d	f5		.
	pop bc			;891e	c1		.
	call 01830h		;891f	cd 30 18	. 0 .
	set 2,(ix-00ah)		;8922	dd cb f6 d6	. . . .
	jr c,l8908h		;8926	38 e0		8 .
	push ix			;8928	dd e5		. .
	ex (sp),hl		;892a	e3		.
	inc hl			;892b	23		#
	bit 5,(ix+002h)		;892c	dd cb 02 6e	. . . n
	jr z,l893fh		;8930	28 0d		( .
	cp 00bh			;8932	fe 0b		. .
	jp z,014a0h		;8934	ca a0 14	. . .
	cp 00ah			;8937	fe 0a		. .
	ld a,016h		;8939	3e 16		> .
	jr z,l899dh		;893b	28 60		( `
	jr l8951h		;893d	18 12		. .
l893fh:
	cp 009h			;893f	fe 09		. .
	jr nc,l8955h		;8941	30 12		0 .
	cp 007h			;8943	fe 07		. .
	jr z,l894fh		;8945	28 08		( .
	jr c,l8955h		;8947	38 0c		8 .
	ld a,001h		;8949	3e 01		> .
	xor (hl)		;894b	ae		.
	ld (hl),a		;894c	77		w
	jr l8951h		;894d	18 02		. .
l894fh:
	res 0,(hl)		;894f	cb 86		. .
l8951h:
	pop hl			;8951	e1		.
	jp 015e5h		;8952	c3 e5 15	. . .
l8955h:
	bit 0,(hl)		;8955	cb 46		. F
	jr nz,l8951h		;8957	20 f8		  .
	pop hl			;8959	e1		.
	cp 00ah			;895a	fe 0a		. .
	jr z,l89ach		;895c	28 4e		( N
	bit 0,c			;895e	cb 41		. A
	jr nz,l8967h		;8960	20 05		  .
	or a			;8962	b7		.
l8963h:
	ld a,012h		;8963	3e 12		> .
	jr nc,l899dh		;8965	30 36		0 6
l8967h:
	push af			;8967	f5		.
	cp 009h			;8968	fe 09		. .
	jr z,l8974h		;896a	28 08		( .
	cp 004h			;896c	fe 04		. .
	jp z,0151eh		;896e	ca 1e 15	. . .
	jp nc,0155ch		;8971	d2 5c 15	. \ .
l8974h:
	push af			;8974	f5		.
	ld b,005h		;8975	06 05		. .
	call 01b8ah		;8977	cd 8a 1b	. . .
	ccf			;897a	3f		?
	jr nc,l8963h		;897b	30 e6		0 .
	ex de,hl		;897d	eb		.
	pop af			;897e	f1		.
	push af			;897f	f5		.
	cp 002h			;8980	fe 02		. .
	jr z,l898ch		;8982	28 08		( .
	call 01abbh		;8984	cd bb 1a	. . .
	push hl			;8987	e5		.
	call 01701h		;8988	cd 01 17	. . .
	pop hl			;898b	e1		.
l898ch:
	pop af			;898c	f1		.
	cp 003h			;898d	fe 03		. .
	jr nz,l89e6h		;898f	20 55		  U
	bit 0,(ix+001h)		;8991	dd cb 01 46	. . . F
	ld a,019h		;8995	3e 19		> .
	jr z,l89a0h		;8997	28 07		( .
	res 0,(ix+001h)		;8999	dd cb 01 86	. . . .
l899dh:
	jp 01dbbh		;899d	c3 bb 1d	. . .
l89a0h:
	ld a,h			;89a0	7c		|
	or l			;89a1	b5		.
	jp nz,0151bh		;89a2	c2 1b 15	. . .
	set 0,(ix+001h)		;89a5	dd cb 01 c6	. . . .
	jp 01504h		;89a9	c3 04 15	. . .
l89ach:
	set 5,(ix+002h)		;89ac	dd cb 02 ee	. . . .
	bit 7,(ix+001h)		;89b0	dd cb 01 7e	. . . ~
	jr z,l89cdh		;89b4	28 17		( .
	ld hl,(022f4h)		;89b6	2a f4 22	* . "
	ld a,h			;89b9	7c		|
	or l			;89ba	b5		.
	ld a,017h		;89bb	3e 17		> .
	jr z,l899dh		;89bd	28 de		( .
	ld a,(022eeh)		;89bf	3a ee 22	: . "
	inc a			;89c2	3c		<
	ld (022eeh),a		;89c3	32 ee 22	2 . "
	dec a			;89c6	3d		=
	set 7,a			;89c7	cb ff		. .
	rlca			;89c9	07		.
	rlca			;89ca	07		.
	or (hl)			;89cb	b6		.
	ld (hl),a		;89cc	77		w
l89cdh:
	jp 0161ah		;89cd	c3 1a 16	. . .
	res 5,(ix+002h)		;89d0	dd cb 02 ae	. . . .
	bit 7,(ix+001h)		;89d4	dd cb 01 7e	. . . ~
	jr z,l89e3h		;89d8	28 09		( .
	ld hl,(022eah)		;89da	2a ea 22	* . "
	ld (hl),000h		;89dd	36 00		6 .
	inc hl			;89df	23		#
	ld (022eah),hl		;89e0	22 ea 22	" . "
l89e3h:
	jp 01421h		;89e3	c3 21 14	. ! .
l89e6h:
	cp 009h			;89e6	fe 09		. .
	jr nz,l89fdh		;89e8	20 13		  .
	ld a,(ix+001h)		;89ea	dd 7e 01	. ~ .
	and 092h		;89ed	e6 92		. .
	jr nz,l8a1fh		;89ef	20 2e		  .
	ex de,hl		;89f1	eb		.
	ld hl,022f6h		;89f2	21 f6 22	! . "
	ld (hl),0c3h		;89f5	36 c3		6 .
	inc hl			;89f7	23		#
	ld (hl),e		;89f8	73		s
	inc hl			;89f9	23		#
	ld (hl),d		;89fa	72		r
	jr l8a4bh		;89fb	18 4e		. N
l89fdh:
	cp 001h			;89fd	fe 01		. .
	jp c,01504h		;89ff	da 04 15	. . .
	push hl			;8a02	e5		.
	jr z,l8a0ch		;8a03	28 07		( .
	ld de,(02321h)		;8a05	ed 5b 21 23	. [ ! #
	add hl,de		;8a09	19		.
	jr l8a21h		;8a0a	18 15		. .
l8a0ch:
	bit 4,(ix+001h)		;8a0c	dd cb 01 66	. . . f
	jr nz,l8a18h		;8a10	20 06		  .
	ld (0230ch),hl		;8a12	22 0c 23	" . #
	ld (0230eh),hl		;8a15	22 0e 23	" . #
l8a18h:
	ld (02321h),hl		;8a18	22 21 23	" ! #
	ld (02341h),hl		;8a1b	22 41 23	" A #
	pop de			;8a1e	d1		.
l8a1fh:
	jr l8a4bh		;8a1f	18 2a		. *
l8a21h:
	ex (sp),hl		;8a21	e3		.
	set 3,(ix-00ah)		;8a22	dd cb f6 de	. . . .
l8a26h:
	ld a,h			;8a26	7c		|
	or l			;8a27	b5		.
	ld a,002h		;8a28	3e 02		> .
	jr z,l8a33h		;8a2a	28 07		( .
	xor a			;8a2c	af		.
	call 01d1ah		;8a2d	cd 1a 1d	. . .
	dec hl			;8a30	2b		+
	jr l8a26h		;8a31	18 f3		. .
l8a33h:
	pop hl			;8a33	e1		.
	bit 0,(ix-008h)		;8a34	dd cb f8 46	. . . F
	jr z,l8a4bh		;8a38	28 11		( .
	cp 002h			;8a3a	fe 02		. .
	jr nc,l8a4bh		;8a3c	30 0d		0 .
	bit 7,(ix+001h)		;8a3e	dd cb 01 7e	. . . ~
	jr z,l8a4bh		;8a42	28 07		( .
	ex de,hl		;8a44	eb		.
	ld hl,(02325h)		;8a45	2a 25 23	* % #
	ld (hl),e		;8a48	73		s
	inc hl			;8a49	23		#
	ld (hl),d		;8a4a	72		r
l8a4bh:
	jp 015e5h		;8a4b	c3 e5 15	. . .
	ld b,(hl)		;8a4e	46		F
	ld e,004h		;8a4f	1e 04		. .
l8a51h:
	call 01b07h		;8a51	cd 07 1b	. . .
	cp b			;8a54	b8		.
	jr z,l8a4bh		;8a55	28 f4		( .
	cp 0a0h			;8a57	fe a0		. .
	jr z,l8a4bh		;8a59	28 f0		( .
	call 01d1ah		;8a5b	cd 1a 1d	. . .
	dec e			;8a5e	1d		.
	jr nz,l8a51h		;8a5f	20 f0		  .
	set 3,(ix-00ah)		;8a61	dd cb f6 de	. . . .
	jr l8a51h		;8a65	18 ea		. .
l8a67h:
	call 0183eh		;8a67	cd 3e 18	. > .
l8a6ah:
	ex af,af'		;8a6a	08		.
	jr nc,l8a4bh		;8a6b	30 de		0 .
	dec b			;8a6d	05		.
	jr nz,l8a74h		;8a6e	20 04		  .
	set 3,(ix-00ah)		;8a70	dd cb f6 de	. . . .
l8a74h:
	push bc			;8a74	c5		.
	ld b,004h		;8a75	06 04		. .
	call 01b8ah		;8a77	cd 8a 1b	. . .
	pop bc			;8a7a	c1		.
	ex af,af'		;8a7b	08		.
	ld a,e			;8a7c	7b		{
	call 01d1ah		;8a7d	cd 1a 1d	. . .
	bit 0,(ix-00fh)		;8a80	dd cb f1 46	. . . F
	jr z,l8a67h		;8a84	28 e1		( .
	ld a,d			;8a86	7a		z
	call 01d1ah		;8a87	cd 1a 1d	. . .
	jr l8a6ah		;8a8a	18 de		. .
	ld b,004h		;8a8c	06 04		. .
	cp 005h			;8a8e	fe 05		. .
	jr nz,l8a74h		;8a90	20 e2		  .
	ld b,002h		;8a92	06 02		. .
	jr l8a74h		;8a94	18 de		. .
l8a96h:
	call 01b88h		;8a96	cd 88 1b	. . .
	ld a,b			;8a99	78		x
	call 01794h		;8a9a	cd 94 17	. . .
l8a9dh:
	ld a,001h		;8a9d	3e 01		> .
	jr c,l8af9h		;8a9f	38 58		8 X
	bit 0,(ix+001h)		;8aa1	dd cb 01 46	. . . F
	jr nz,l8b15h		;8aa5	20 6e		  n
	bit 5,(ix+002h)		;8aa7	dd cb 02 6e	. . . n
	jr nz,l8b15h		;8aab	20 68		  h
	bit 3,c			;8aad	cb 59		. Y
	jr z,l8a96h		;8aaf	28 e5		( .
	call 01830h		;8ab1	cd 30 18	. 0 .
	jr c,l8a9dh		;8ab4	38 e7		8 .
	pop bc			;8ab6	c1		.
	pop hl			;8ab7	e1		.
	bit 2,c			;8ab8	cb 51		. Q
	jr z,l8ac6h		;8aba	28 0a		( .
	dec hl			;8abc	2b		+
	dec hl			;8abd	2b		+
	push de			;8abe	d5		.
	ld de,(02321h)		;8abf	ed 5b 21 23	. [ ! #
	sbc hl,de		;8ac3	ed 52		. R
	pop de			;8ac5	d1		.
l8ac6h:
	ex de,hl		;8ac6	eb		.
	ld a,e			;8ac7	7b		{
	bit 2,c			;8ac8	cb 51		. Q
	call nz,01842h		;8aca	c4 42 18	. B .
	bit 1,c			;8acd	cb 49		. I
	call nz,0183eh		;8acf	c4 3e 18	. > .
	ld a,c			;8ad2	79		y
	or a			;8ad3	b7		.
	ld a,e			;8ad4	7b		{
	ex af,af'		;8ad5	08		.
	ld a,d			;8ad6	7a		z
	bit 0,c			;8ad7	cb 41		. A
	ex de,hl		;8ad9	eb		.
	pop hl			;8ada	e1		.
	push hl			;8adb	e5		.
	scf			;8adc	37		7
	push af			;8add	f5		.
	ccf			;8ade	3f		?
	push af			;8adf	f5		.
	ex af,af'		;8ae0	08		.
	push af			;8ae1	f5		.
	ld a,d			;8ae2	7a		z
	or b			;8ae3	b0		.
	inc c			;8ae4	0c		.
	push af			;8ae5	f5		.
	ld a,0cbh		;8ae6	3e cb		> .
	bit 3,e			;8ae8	cb 5b		. [
	jr z,l8aedh		;8aea	28 01		( .
	push af			;8aec	f5		.
l8aedh:
	bit 4,l			;8aed	cb 65		. e
	jr z,l8b0bh		;8aef	28 1a		( .
	ld a,h			;8af1	7c		|
	bit 6,e			;8af2	cb 73		. s
	jr z,l8afeh		;8af4	28 08		( .
	or a			;8af6	b7		.
l8af7h:
	ld a,011h		;8af7	3e 11		> .
l8af9h:
	jp nz,01dbbh		;8af9	c2 bb 1d	. . .
	jr l8b03h		;8afc	18 05		. .
l8afeh:
	pop bc			;8afe	c1		.
	bit 0,l			;8aff	cb 45		. E
	push af			;8b01	f5		.
	push bc			;8b02	c5		.
l8b03h:
	ld a,0ddh		;8b03	3e dd		> .
	or l			;8b05	b5		.
	push af			;8b06	f5		.
	bit 4,e			;8b07	cb 63		. c
	jr nz,l8af7h		;8b09	20 ec		  .
l8b0bh:
	ld a,0edh		;8b0b	3e ed		> .
	bit 4,e			;8b0d	cb 63		. c
l8b0fh:
	call nz,01d1ah		;8b0f	c4 1a 1d	. . .
	pop af			;8b12	f1		.
	jr nc,l8b0fh		;8b13	30 fa		0 .
l8b15h:
	bit 5,(ix+002h)		;8b15	dd cb 02 6e	. . . n
	jr z,l8b4ah		;8b19	28 2f		( /
	bit 7,(ix+001h)		;8b1b	dd cb 01 7e	. . . ~
	jr z,l8b4ah		;8b1f	28 29		( )
	ld hl,(02308h)		;8b21	2a 08 23	* . #
	ld de,(022eah)		;8b24	ed 5b ea 22	. [ . "
	ld bc,00000h		;8b28	01 00 00	. . .
l8b2bh:
	push bc			;8b2b	c5		.
	ld bc,(022e8h)		;8b2c	ed 4b e8 22	. K . "
	ex de,hl		;8b30	eb		.
	or a			;8b31	b7		.
	sbc hl,bc		;8b32	ed 42		. B
	add hl,bc		;8b34	09		.
	ex de,hl		;8b35	eb		.
	pop bc			;8b36	c1		.
	jr c,l8b3fh		;8b37	38 06		8 .
	ld hl,0238bh		;8b39	21 8b 23	! . #
	jp 019d3h		;8b3c	c3 d3 19	. . .
l8b3fh:
	ld a,(hl)		;8b3f	7e		~
	ldi			;8b40	ed a0		. .
	cp 00dh			;8b42	fe 0d		. .
	jr nz,l8b2bh		;8b44	20 e5		  .
	ld (022eah),de		;8b46	ed 53 ea 22	. S . "
l8b4ah:
	bit 0,(ix+000h)		;8b4a	dd cb 00 46	. . . F
	call z,0172eh		;8b4e	cc 2e 17	. . .
	bit 6,(ix+002h)		;8b51	dd cb 02 76	. . . v
	jr z,l8b5bh		;8b55	28 04		( .
	set 0,(ix+003h)		;8b57	dd cb 03 c6	. . . .
l8b5bh:
	bit 6,(ix-00ah)		;8b5b	dd cb f6 76	. . . v
	jr z,l8b6ah		;8b5f	28 09		( .
	ld a,010h		;8b61	3e 10		> .
	bit 2,(ix+001h)		;8b63	dd cb 01 56	. . . V
	call 01dc8h		;8b67	cd c8 1d	. . .
l8b6ah:
	ld sp,02648h		;8b6a	31 48 26	1 H &
	ld hl,011f9h		;8b6d	21 f9 11	! . .
	ex (sp),hl		;8b70	e3		.
	bit 2,(ix+001h)		;8b71	dd cb 01 56	. . . V
	ret nz			;8b75	c0		.
	call 00023h		;8b76	cd 23 00	. # .
	ret nc			;8b79	d0		.
	call 00317h		;8b7a	cd 17 03	. . .
	and 0dfh		;8b7d	e6 df		. .
	cp 045h			;8b7f	fe 45		. E
	ret nz			;8b81	c0		.
	bit 7,(ix+001h)		;8b82	dd cb 01 7e	. . . ~
	jr nz,l8b90h		;8b86	20 08		  .
	ld hl,(0233fh)		;8b88	2a 3f 23	* ? #
	ld a,h			;8b8b	7c		|
	or l			;8b8c	b5		.
	call nz,00ab8h		;8b8d	c4 b8 0a	. . .
l8b90h:
	bit 3,(ix+002h)		;8b90	dd cb 02 5e	. . . ^
	call nz,00aadh		;8b94	c4 ad 0a	. . .
	jp 0002fh		;8b97	c3 2f 00	. / .
	cp 025h			;8b9a	fe 25		. %
	ld a,000h		;8b9c	3e 00		> .
	ld h,a			;8b9e	67		g
	ld l,a			;8b9f	6f		o
	jr nz,l8bafh		;8ba0	20 0d		  .
l8ba2h:
	ld a,(de)		;8ba2	1a		.
	sub 030h		;8ba3	d6 30		. 0
	ret c			;8ba5	d8		.
	cp 002h			;8ba6	fe 02		. .
	ret nc			;8ba8	d0		.
	rrca			;8ba9	0f		.
	adc hl,hl		;8baa	ed 6a		. j
	inc de			;8bac	13		.
	jr l8ba2h		;8bad	18 f3		. .
l8bafh:
	ld a,(de)		;8baf	1a		.
	sub 030h		;8bb0	d6 30		. 0
	ret m			;8bb2	f8		.
	cp 00ah			;8bb3	fe 0a		. .
	jr c,l8bbfh		;8bb5	38 08		8 .
	sub 007h		;8bb7	d6 07		. .
	cp 00ah			;8bb9	fe 0a		. .
	ret m			;8bbb	f8		.
	cp 010h			;8bbc	fe 10		. .
	ret p			;8bbe	f0		.
l8bbfh:
	inc de			;8bbf	13		.
	add hl,hl		;8bc0	29		)
	add hl,hl		;8bc1	29		)
	add hl,hl		;8bc2	29		)
	add hl,hl		;8bc3	29		)
	or l			;8bc4	b5		.
	ld l,a			;8bc5	6f		o
	jr l8bafh		;8bc6	18 e7		. .
	push af			;8bc8	f5		.
	cp 03bh			;8bc9	fe 3b		. ;
	jr z,l8c18h		;8bcb	28 4b		( K
	cp 02ah			;8bcd	fe 2a		. *
	jr z,l8c18h		;8bcf	28 47		( G
	cp 00dh			;8bd1	fe 0d		. .
	jr z,l8c18h		;8bd3	28 43		( C
	call 01b44h		;8bd5	cd 44 1b	. D .
l8bd8h:
	bit 3,c			;8bd8	cb 59		. Y
	jr nz,l8bf6h		;8bda	20 1a		  .
	call 01b07h		;8bdc	cd 07 1b	. . .
	bit 0,c			;8bdf	cb 41		. A
	jr nz,l8bd8h		;8be1	20 f5		  .
	call 01b5ah		;8be3	cd 5a 1b	. Z .
	call 01b08h		;8be6	cd 08 1b	. . .
	cp 00dh			;8be9	fe 0d		. .
	jr z,l8c18h		;8beb	28 2b		( +
	inc hl			;8bed	23		#
	cp 009h			;8bee	fe 09		. .
	jr z,l8c18h		;8bf0	28 26		( &
	cp 020h			;8bf2	fe 20		.  
	jr l8c18h		;8bf4	18 22		. "
l8bf6h:
	ld a,0a0h		;8bf6	3e a0		> .
	ld (de),a		;8bf8	12		.
	bit 0,(ix+001h)		;8bf9	dd cb 01 46	. . . F
	jr nz,l8c0eh		;8bfd	20 0f		  .
	ld de,025a0h		;8bff	11 a0 25	. . %
	set 6,(ix+001h)		;8c02	dd cb 01 f6	. . . .
	call 01869h		;8c06	cd 69 18	. i .
	ld a,007h		;8c09	3e 07		> .
	jp c,01dbbh		;8c0b	da bb 1d	. . .
l8c0eh:
	bit 0,(ix+000h)		;8c0e	dd cb 00 46	. . . F
	call z,0172eh		;8c12	cc 2e 17	. . .
	jp 0163ah		;8c15	c3 3a 16	. : .
l8c18h:
	push af			;8c18	f5		.
	ld b,05dh		;8c19	06 5d		. ]
l8c1bh:
	inc hl			;8c1b	23		#
	cp 00dh			;8c1c	fe 0d		. .
	jr z,l8c2bh		;8c1e	28 0b		( .
	dec hl			;8c20	2b		+
	call 01afeh		;8c21	cd fe 1a	. . .
	djnz l8c1bh		;8c24	10 f5		. .
	ld c,b			;8c26	48		H
	ld a,00dh		;8c27	3e 0d		> .
	cpir			;8c29	ed b1		. .
l8c2bh:
	ld a,0a0h		;8c2b	3e a0		> .
	ld (de),a		;8c2d	12		.
	pop af			;8c2e	f1		.
	pop de			;8c2f	d1		.
	ret			;8c30	c9		.
	ex de,hl		;8c31	eb		.
	ld hl,(02327h)		;8c32	2a 27 23	* ' #
	bit 7,(ix+000h)		;8c35	dd cb 00 7e	. . . ~
	jr z,l8c42h		;8c39	28 07		( .
	ex de,hl		;8c3b	eb		.
	call 01ad2h		;8c3c	cd d2 1a	. . .
	ex de,hl		;8c3f	eb		.
	jr l8c48h		;8c40	18 06		. .
l8c42h:
	call 01e0bh		;8c42	cd 0b 1e	. . .
	ld (hl),020h		;8c45	36 20		6  
	inc hl			;8c47	23		#
l8c48h:
	ld (02327h),hl		;8c48	22 27 23	" ' #
	ret			;8c4b	c9		.
	ex de,hl		;8c4c	eb		.
	ld (hl),0a0h		;8c4d	36 a0		6 .
	ex de,hl		;8c4f	eb		.
	push hl			;8c50	e5		.
	ld hl,0258ch		;8c51	21 8c 25	! . %
	ld b,000h		;8c54	06 00		. .
	call 01758h		;8c56	cd 58 17	. X .
	pop hl			;8c59	e1		.
	ld de,0258ch		;8c5a	11 8c 25	. . %
	ret			;8c5d	c9		.
	bit 2,(ix+001h)		;8c5e	dd cb 01 56	. . . V
	ret nz			;8c62	c0		.
	bit 0,(ix+003h)		;8c63	dd cb 03 46	. . . F
	jr z,l8c6eh		;8c67	28 05		( .
	bit 4,(ix+002h)		;8c69	dd cb 02 66	. . . f
	ret z			;8c6d	c8		.
l8c6eh:
	ld hl,0258ch		;8c6e	21 8c 25	! . %
	ld b,005h		;8c71	06 05		. .
l8c73h:
	ld a,(hl)		;8c73	7e		~
	call 0001dh		;8c74	cd 1d 00	. . .
	inc hl			;8c77	23		#
	djnz l8c73h		;8c78	10 f9		. .
	xor a			;8c7a	af		.
	ld b,a			;8c7b	47		G
	bit 1,(ix+002h)		;8c7c	dd cb 02 4e	. . . N
	jr z,l8c88h		;8c80	28 06		( .
	ld b,000h		;8c82	06 00		. .
	ld de,00009h		;8c84	11 09 00	. . .
	add hl,de		;8c87	19		.
l8c88h:
	ld a,(hl)		;8c88	7e		~
	cp 0a0h			;8c89	fe a0		. .
	jp z,003b7h		;8c8b	ca b7 03	. . .
	call 0001dh		;8c8e	cd 1d 00	. . .
	inc hl			;8c91	23		#
	djnz l8c88h		;8c92	10 f4		. .
	call 003b7h		;8c94	cd b7 03	. . .
	ld a,(00034h)		;8c97	3a 34 00	: 4 .
	call 0039ah		;8c9a	cd 9a 03	. . .
	ld a,(00035h)		;8c9d	3a 35 00	: 5 .
	ld b,a			;8ca0	47		G
	jr l8c88h		;8ca1	18 e5		. .
	ld a,(022f9h)		;8ca3	3a f9 22	: . "
	ld b,a			;8ca6	47		G
	push hl			;8ca7	e5		.
	call 01788h		;8ca8	cd 88 17	. . .
	pop hl			;8cab	e1		.
	call 00455h		;8cac	cd 55 04	. U .
	dec hl			;8caf	2b		+
	ld (hl),000h		;8cb0	36 00		6 .
	dec hl			;8cb2	2b		+
	ld (hl),000h		;8cb3	36 00		6 .
	jp 003b7h		;8cb5	c3 b7 03	. . .
	push af			;8cb8	f5		.
	xor a			;8cb9	af		.
l8cbah:
	cp (hl)			;8cba	be		.
	jr z,l8cc0h		;8cbb	28 03		( .
	inc hl			;8cbd	23		#
	jr l8cbah		;8cbe	18 fa		. .
l8cc0h:
	pop af			;8cc0	f1		.
	jp 01e10h		;8cc1	c3 10 1e	. . .
	exx			;8cc4	d9		.
	pop hl			;8cc5	e1		.
	pop de			;8cc6	d1		.
	ex (sp),hl		;8cc7	e3		.
	ld b,a			;8cc8	47		G
	inc d			;8cc9	14		.
	bit 7,(hl)		;8cca	cb 7e		. ~
	jr nz,l8d47h		;8ccc	20 79		  y
	inc hl			;8cce	23		#
	ld a,(hl)		;8ccf	7e		~
	and 07fh		;8cd0	e6 7f		. .
	cp 070h			;8cd2	fe 70		. p
	jr c,l8d08h		;8cd4	38 32		8 2
	ld c,a			;8cd6	4f		O
	xor b			;8cd7	a8		.
	rrca			;8cd8	0f		.
	cp 008h			;8cd9	fe 08		. .
	jr nc,l8d40h		;8cdb	30 63		0 c
	cp 005h			;8cdd	fe 05		. .
	jr c,l8d57h		;8cdf	38 76		8 v
	ex af,af'		;8ce1	08		.
	ld a,(ix-00bh)		;8ce2	dd 7e f5	. ~ .
	or a			;8ce5	b7		.
	jr nz,l8d03h		;8ce6	20 1b		  .
	ex af,af'		;8ce8	08		.
	cp 006h			;8ce9	fe 06		. .
	ld a,(ix-00ch)		;8ceb	dd 7e f4	. ~ .
	jr c,l8cfbh		;8cee	38 0b		8 .
	jr z,l8cfeh		;8cf0	28 0c		( .
	cp 003h			;8cf2	fe 03		. .
	dec a			;8cf4	3d		=
	jr nc,l8d03h		;8cf5	30 0c		0 .
	inc a			;8cf7	3c		<
	jr z,l8cfbh		;8cf8	28 01		( .
	inc a			;8cfa	3c		<
l8cfbh:
	rlca			;8cfb	07		.
	rlca			;8cfc	07		.
	rlca			;8cfd	07		.
l8cfeh:
	ld (ix-00dh),a		;8cfe	dd 77 f3	. w .
	and 0c7h		;8d01	e6 c7		. .
l8d03h:
	call nz,01846h		;8d03	c4 46 18	. F .
	jr l8d5ah		;8d06	18 52		. R
l8d08h:
	cp 020h			;8d08	fe 20		.  
	jr nc,l8d3dh		;8d0a	30 31		0 1
	push hl			;8d0c	e5		.
	ld hl,0212dh		;8d0d	21 2d 21	! - !
	add a,l			;8d10	85		.
	ld l,a			;8d11	6f		o
	ld a,h			;8d12	7c		|
	adc a,000h		;8d13	ce 00		. .
	ld h,a			;8d15	67		g
	ld a,b			;8d16	78		x
	ex af,af'		;8d17	08		.
	ld a,(hl)		;8d18	7e		~
	and 00fh		;8d19	e6 0f		. .
	ld c,a			;8d1b	4f		O
	ld a,(hl)		;8d1c	7e		~
	ex af,af'		;8d1d	08		.
	inc hl			;8d1e	23		#
	ld b,(hl)		;8d1f	46		F
	inc hl			;8d20	23		#
	ld h,(hl)		;8d21	66		f
	ld l,b			;8d22	68		h
	ld b,000h		;8d23	06 00		. .
	cpir			;8d25	ed b1		. .
	pop hl			;8d27	e1		.
	ld b,a			;8d28	47		G
	jr nz,l8d40h		;8d29	20 15		  .
	ex af,af'		;8d2b	08		.
l8d2ch:
	sla c			;8d2c	cb 21		. !
	sub 010h		;8d2e	d6 10		. .
	jr nc,l8d2ch		;8d30	30 fa		0 .
	srl c			;8d32	cb 39		. 9
	ld a,(ix-00dh)		;8d34	dd 7e f3	. ~ .
	or c			;8d37	b1		.
	ld (ix-00dh),a		;8d38	dd 77 f3	. w .
	jr l8d5ah		;8d3b	18 1d		. .
l8d3dh:
	cp b			;8d3d	b8		.
	jr z,l8d5ah		;8d3e	28 1a		( .
l8d40h:
	bit 7,(hl)		;8d40	cb 7e		. ~
	inc hl			;8d42	23		#
	jp z,01810h		;8d43	ca 10 18	. . .
	dec hl			;8d46	2b		+
l8d47h:
	inc hl			;8d47	23		#
	inc hl			;8d48	23		#
	ld a,(hl)		;8d49	7e		~
	ld e,a			;8d4a	5f		_
	and 007h		;8d4b	e6 07		. .
	cp d			;8d4d	ba		.
	jp z,0179eh		;8d4e	ca 9e 17	. . .
	inc hl			;8d51	23		#
	jp nc,01810h		;8d52	d2 10 18	. . .
	exx			;8d55	d9		.
	ret			;8d56	c9		.
l8d57h:
	ld (ix-00eh),a		;8d57	dd 77 f2	. w .
l8d5ah:
	ex (sp),hl		;8d5a	e3		.
	push de			;8d5b	d5		.
	push hl			;8d5c	e5		.
	exx			;8d5d	d9		.
	or a			;8d5e	b7		.
	ret			;8d5f	c9		.
	exx			;8d60	d9		.
	pop hl			;8d61	e1		.
	pop de			;8d62	d1		.
	ex (sp),hl		;8d63	e3		.
	ld a,(hl)		;8d64	7e		~
	cp 080h			;8d65	fe 80		. .
	inc hl			;8d67	23		#
	ld d,(hl)		;8d68	56		V
	push de			;8d69	d5		.
	exx			;8d6a	d9		.
	pop de			;8d6b	d1		.
	ld a,d			;8d6c	7a		z
	ret			;8d6d	c9		.
	ld a,d			;8d6e	7a		z
	or 07fh			;8d6f	f6 7f		. .
	and e			;8d71	a3		.
	rlca			;8d72	07		.
	sbc a,a			;8d73	9f		.
	sub d			;8d74	92		.
	ret z			;8d75	c8		.
	bit 1,(ix+000h)		;8d76	dd cb 00 4e	. . . N
	ret nz			;8d7a	c0		.
	set 6,(ix-00ah)		;8d7b	dd cb f6 f6	. . . .
	ret			;8d7f	c9		.
	ld hl,00000h		;8d80	21 00 00	! . .
l8d83h:
	ld a,(de)		;8d83	1a		.
	sub 030h		;8d84	d6 30		. 0
	ret c			;8d86	d8		.
	cp 00ah			;8d87	fe 0a		. .
	ret nc			;8d89	d0		.
	push de			;8d8a	d5		.
	ld d,h			;8d8b	54		T
	ld e,l			;8d8c	5d		]
	add hl,hl		;8d8d	29		)
	add hl,hl		;8d8e	29		)
	add hl,de		;8d8f	19		.
	add hl,hl		;8d90	29		)
	ld d,000h		;8d91	16 00		. .
	ld e,a			;8d93	5f		_
	add hl,de		;8d94	19		.
	pop de			;8d95	d1		.
	inc de			;8d96	13		.
	jr l8d83h		;8d97	18 ea		. .
	ex de,hl		;8d99	eb		.
	ld de,020c8h		;8d9a	11 c8 20	. .  
	ld (0230ah),hl		;8d9d	22 0a 23	" . #
	dec hl			;8da0	2b		+
	push hl			;8da1	e5		.
	push bc			;8da2	c5		.
l8da3h:
	call 01b07h		;8da3	cd 07 1b	. . .
	bit 6,c			;8da6	cb 71		. q
	jr nz,l8da3h		;8da8	20 f9		  .
	pop bc			;8daa	c1		.
	ex (sp),hl		;8dab	e3		.
	push hl			;8dac	e5		.
	push bc			;8dad	c5		.
	push de			;8dae	d5		.
	ld d,000h		;8daf	16 00		. .
	push de			;8db1	d5		.
l8db2h:
	call 01b07h		;8db2	cd 07 1b	. . .
	bit 6,c			;8db5	cb 71		. q
	jp z,019e1h		;8db7	ca e1 19	. . .
	call 01794h		;8dba	cd 94 17	. . .
	jr nc,l8db2h		;8dbd	30 f3		0 .
	pop bc			;8dbf	c1		.
	pop de			;8dc0	d1		.
	inc de			;8dc1	13		.
	ld hl,(02300h)		;8dc2	2a 00 23	* . #
	inc hl			;8dc5	23		#
	inc hl			;8dc6	23		#
	inc hl			;8dc7	23		#
	inc hl			;8dc8	23		#
	push hl			;8dc9	e5		.
	push de			;8dca	d5		.
	push bc			;8dcb	c5		.
	ld b,006h		;8dcc	06 06		. .
	inc hl			;8dce	23		#
l8dcfh:
	ex de,hl		;8dcf	eb		.
	ld a,(de)		;8dd0	1a		.
	and 07fh		;8dd1	e6 7f		. .
	push bc			;8dd3	c5		.
	push af			;8dd4	f5		.
	call 01b08h		;8dd5	cd 08 1b	. . .
	pop af			;8dd8	f1		.
	bit 6,c			;8dd9	cb 71		. q
	pop bc			;8ddb	c1		.
	jr nz,l8de1h		;8ddc	20 03		  .
	or a			;8dde	b7		.
	jr l8de2h		;8ddf	18 01		. .
l8de1h:
	cp (hl)			;8de1	be		.
l8de2h:
	ex de,hl		;8de2	eb		.
	jr z,l8de9h		;8de3	28 04		( .
l8de5h:
	pop bc			;8de5	c1		.
	pop de			;8de6	d1		.
	jr l8e07h		;8de7	18 1e		. .
l8de9h:
	bit 7,(hl)		;8de9	cb 7e		. ~
	inc hl			;8deb	23		#
	inc de			;8dec	13		.
	jr nz,l8df4h		;8ded	20 05		  .
	djnz l8dcfh		;8def	10 de		. .
	jp 018d4h		;8df1	c3 d4 18	. . .
l8df4h:
	dec b			;8df4	05		.
	jr z,l8e04h		;8df5	28 0d		( .
	ex de,hl		;8df7	eb		.
	call 01b08h		;8df8	cd 08 1b	. . .
	ex de,hl		;8dfb	eb		.
	bit 6,c			;8dfc	cb 71		. q
	jr z,l8e04h		;8dfe	28 04		( .
	or a			;8e00	b7		.
	scf			;8e01	37		7
	jr l8de5h		;8e02	18 e1		. .
l8e04h:
	xor a			;8e04	af		.
	jr l8de5h		;8e05	18 de		. .
l8e07h:
	pop hl			;8e07	e1		.
	jr z,l8e21h		;8e08	28 17		( .
	push de			;8e0a	d5		.
	push hl			;8e0b	e5		.
	dec hl			;8e0c	2b		+
	dec hl			;8e0d	2b		+
	jp c,018e3h		;8e0e	da e3 18	. . .
	dec hl			;8e11	2b		+
	dec hl			;8e12	2b		+
	ld e,(hl)		;8e13	5e		^
	inc hl			;8e14	23		#
	ld d,(hl)		;8e15	56		V
	ld a,d			;8e16	7a		z
	or e			;8e17	b3		.
	jr z,l8e66h		;8e18	28 4c		( L
	dec de			;8e1a	1b		.
	pop hl			;8e1b	e1		.
	pop hl			;8e1c	e1		.
	ex de,hl		;8e1d	eb		.
	jp 01899h		;8e1e	c3 99 18	. . .
l8e21h:
	bit 0,b			;8e21	cb 40		. @
	jr z,l8e2ch		;8e23	28 07		( .
	bit 0,(hl)		;8e25	cb 46		. F
	ld a,013h		;8e27	3e 13		> .
	jp z,01dbbh		;8e29	ca bb 1d	. . .
l8e2ch:
	ld (022f4h),hl		;8e2c	22 f4 22	" . "
	bit 6,(ix+001h)		;8e2f	dd cb 01 76	. . . v
	jr z,l8e59h		;8e33	28 24		( $
	res 6,(ix+001h)		;8e35	dd cb 01 b6	. . . .
	bit 7,(ix+001h)		;8e39	dd cb 01 7e	. . . ~
	jr z,l8e64h		;8e3d	28 25		( %
	bit 0,(hl)		;8e3f	cb 46		. F
	ld a,004h		;8e41	3e 04		> .
	jr nz,l8e96h		;8e43	20 51		  Q
	set 0,(hl)		;8e45	cb c6		. .
l8e47h:
	inc hl			;8e47	23		#
	bit 7,(hl)		;8e48	cb 7e		. ~
	jr z,l8e47h		;8e4a	28 fb		( .
	inc hl			;8e4c	23		#
	ld (02325h),hl		;8e4d	22 25 23	" % #
	ld de,(02321h)		;8e50	ed 5b 21 23	. [ ! #
	ld (hl),e		;8e54	73		s
	inc hl			;8e55	23		#
	ld (hl),d		;8e56	72		r
	pop hl			;8e57	e1		.
	ret			;8e58	c9		.
l8e59h:
	inc hl			;8e59	23		#
	bit 7,(hl)		;8e5a	cb 7e		. ~
	jr z,l8e59h		;8e5c	28 fb		( .
	inc hl			;8e5e	23		#
	ld e,(hl)		;8e5f	5e		^
	inc hl			;8e60	23		#
	ld d,(hl)		;8e61	56		V
l8e62h:
	xor a			;8e62	af		.
	inc a			;8e63	3c		<
l8e64h:
	pop hl			;8e64	e1		.
	ret			;8e65	c9		.
l8e66h:
	bit 7,(ix+002h)		;8e66	dd cb 02 7e	. . . ~
	ld a,002h		;8e6a	3e 02		> .
	jr nz,l8e96h		;8e6c	20 28		  (
	bit 6,(ix+002h)		;8e6e	dd cb 02 76	. . . v
	push hl			;8e72	e5		.
	jr z,l8e91h		;8e73	28 1c		( .
	ld hl,(0230ah)		;8e75	2a 0a 23	* . #
	ld a,(hl)		;8e78	7e		~
	cp 03dh			;8e79	fe 3d		. =
	jr nz,l8e91h		;8e7b	20 14		  .
	inc hl			;8e7d	23		#
	ld a,(hl)		;8e7e	7e		~
	and 00fh		;8e7f	e6 0f		. .
	ld hl,02343h		;8e81	21 43 23	! C #
	ld d,000h		;8e84	16 00		. .
	ld e,a			;8e86	5f		_
	add hl,de		;8e87	19		.
	add hl,de		;8e88	19		.
	ld e,(hl)		;8e89	5e		^
	inc hl			;8e8a	23		#
	ld d,(hl)		;8e8b	56		V
	pop hl			;8e8c	e1		.
	pop hl			;8e8d	e1		.
	pop hl			;8e8e	e1		.
	jr l8e62h		;8e8f	18 d1		. .
l8e91h:
	pop hl			;8e91	e1		.
	bit 0,b			;8e92	cb 40		. @
	ld a,013h		;8e94	3e 13		> .
l8e96h:
	jp nz,01dbbh		;8e96	c2 bb 1d	. . .
	ex (sp),hl		;8e99	e3		.
	pop hl			;8e9a	e1		.
	push hl			;8e9b	e5		.
	ld hl,(02323h)		;8e9c	2a 23 23	* # #
	push bc			;8e9f	c5		.
	ld b,004h		;8ea0	06 04		. .
l8ea2h:
	ld (hl),000h		;8ea2	36 00		6 .
	inc hl			;8ea4	23		#
	djnz l8ea2h		;8ea5	10 fb		. .
	ld (hl),001h		;8ea7	36 01		6 .
	ld (022f4h),hl		;8ea9	22 f4 22	" . "
	bit 6,(ix+001h)		;8eac	dd cb 01 76	. . . v
	res 6,(ix+001h)		;8eb0	dd cb 01 b6	. . . .
	jr nz,l8eb8h		;8eb4	20 02		  .
	res 0,(hl)		;8eb6	cb 86		. .
l8eb8h:
	inc hl			;8eb8	23		#
	pop bc			;8eb9	c1		.
	ex (sp),hl		;8eba	e3		.
	pop de			;8ebb	d1		.
	ld (hl),d		;8ebc	72		r
	dec hl			;8ebd	2b		+
	ld (hl),e		;8ebe	73		s
	pop hl			;8ebf	e1		.
	ex de,hl		;8ec0	eb		.
	push hl			;8ec1	e5		.
	push de			;8ec2	d5		.
	push bc			;8ec3	c5		.
	ld b,006h		;8ec4	06 06		. .
l8ec6h:
	ld a,(de)		;8ec6	1a		.
	res 7,a			;8ec7	cb bf		. .
	ex de,hl		;8ec9	eb		.
	push bc			;8eca	c5		.
	call 01b09h		;8ecb	cd 09 1b	. . .
	bit 6,c			;8ece	cb 71		. q
	pop bc			;8ed0	c1		.
	ex de,hl		;8ed1	eb		.
	jr z,l8ed9h		;8ed2	28 05		( .
	ld (hl),a		;8ed4	77		w
	inc hl			;8ed5	23		#
	inc de			;8ed6	13		.
	djnz l8ec6h		;8ed7	10 ed		. .
l8ed9h:
	pop bc			;8ed9	c1		.
	dec hl			;8eda	2b		+
	set 7,(hl)		;8edb	cb fe		. .
	inc hl			;8edd	23		#
	pop af			;8ede	f1		.
	ex (sp),hl		;8edf	e3		.
	pop de			;8ee0	d1		.
	push de			;8ee1	d5		.
	push hl			;8ee2	e5		.
	push af			;8ee3	f5		.
	ex de,hl		;8ee4	eb		.
	ld (02325h),hl		;8ee5	22 25 23	" % #
	ld de,(02321h)		;8ee8	ed 5b 21 23	. [ ! #
	ld (hl),e		;8eec	73		s
	inc hl			;8eed	23		#
	ld (hl),d		;8eee	72		r
	inc hl			;8eef	23		#
	ld (02323h),hl		;8ef0	22 23 23	" # #
	ld de,0000eh		;8ef3	11 0e 00	. . .
	add hl,de		;8ef6	19		.
	ex de,hl		;8ef7	eb		.
	ld hl,(0231dh)		;8ef8	2a 1d 23	* . #
	or a			;8efb	b7		.
	sbc hl,de		;8efc	ed 52		. R
	jr nc,l8f09h		;8efe	30 09		0 .
	ld hl,02500h		;8f00	21 00 25	! . %
	call 00455h		;8f03	cd 55 04	. U .
	jp 01652h		;8f06	c3 52 16	. R .
l8f09h:
	pop hl			;8f09	e1		.
	pop de			;8f0a	d1		.
	ex de,hl		;8f0b	eb		.
	pop de			;8f0c	d1		.
	xor a			;8f0d	af		.
	inc a			;8f0e	3c		<
	pop hl			;8f0f	e1		.
	ret			;8f10	c9		.
	call 01830h		;8f11	cd 30 18	. 0 .
	jp c,0188fh		;8f14	da 8f 18	. . .
	bit 7,e			;8f17	cb 7b		. {
	jr z,l8f1fh		;8f19	28 04		( .
	ld de,(02321h)		;8f1b	ed 5b 21 23	. [ ! #
l8f1fh:
	pop bc			;8f1f	c1		.
	pop hl			;8f20	e1		.
	pop hl			;8f21	e1		.
	scf			;8f22	37		7
	ret			;8f23	c9		.
	ld de,0258ch		;8f24	11 8c 25	. . %
	ld a,020h		;8f27	3e 20		>  
	ld b,05eh		;8f29	06 5e		. ^
l8f2bh:
	ld (de),a		;8f2b	12		.
	inc de			;8f2c	13		.
	djnz l8f2bh		;8f2d	10 fc		. .
	ld de,0259ah		;8f2f	11 9a 25	. . %
	ld (02327h),de		;8f32	ed 53 27 23	. S ' #
	ld hl,(02302h)		;8f36	2a 02 23	* . #
	bit 6,(ix+002h)		;8f39	dd cb 02 76	. . . v
	jr z,l8f51h		;8f3d	28 12		( .
	ld a,(hl)		;8f3f	7e		~
	or a			;8f40	b7		.
	jr nz,l8fc2h		;8f41	20 7f		  .
	ld hl,(02304h)		;8f43	2a 04 23	* . #
	ld (02302h),hl		;8f46	22 02 23	" . #
	res 6,(ix+002h)		;8f49	dd cb 02 b6	. . . .
	res 0,(ix+003h)		;8f4d	dd cb 03 86	. . . .
l8f51h:
	ld a,(ix+002h)		;8f51	dd 7e 02	. ~ .
	bit 0,a			;8f54	cb 47		. G
	jr nz,l8f7fh		;8f56	20 27		  '
	bit 3,a			;8f58	cb 5f		. _
	jr z,l8fb1h		;8f5a	28 55		( U
l8f5ch:
	ld e,(hl)		;8f5c	5e		^
	inc hl			;8f5d	23		#
	ld d,(hl)		;8f5e	56		V
	dec hl			;8f5f	2b		+
	ld a,e			;8f60	7b		{
	inc a			;8f61	3c		<
	inc d			;8f62	14		.
	or d			;8f63	b2		.
	jr z,l8fa4h		;8f64	28 3e		( >
	dec d			;8f66	15		.
	ld a,e			;8f67	7b		{
	or d			;8f68	b2		.
	jr nz,l8fbeh		;8f69	20 53		  S
	push iy			;8f6b	fd e5		. .
	push ix			;8f6d	dd e5		. .
	ld iy,05c3ah		;8f6f	fd 21 3a 5c	. ! : \
	call 011b6h		;8f73	cd b6 11	. . .
	pop ix			;8f76	dd e1		. .
	pop iy			;8f78	fd e1		. .
	ld hl,02a3ah		;8f7a	21 3a 2a	! : *
	jr l8f5ch		;8f7d	18 dd		. .
l8f7fh:
	ld de,(02310h)		;8f7f	ed 5b 10 23	. [ . #
	dec de			;8f83	1b		.
	ld (02310h),de		;8f84	ed 53 10 23	. S . #
	ld a,d			;8f88	7a		z
	or e			;8f89	b3		.
	jr z,l8f98h		;8f8a	28 0c		( .
l8f8ch:
	ld e,(hl)		;8f8c	5e		^
	inc hl			;8f8d	23		#
	ld a,(hl)		;8f8e	7e		~
	dec hl			;8f8f	2b		+
	or e			;8f90	b3		.
	jr nz,l8fbeh		;8f91	20 2b		  +
	call 01171h		;8f93	cd 71 11	. q .
	jr l8f8ch		;8f96	18 f4		. .
l8f98h:
	ld a,(02319h)		;8f98	3a 19 23	: . #
	ld (ix+001h),a		;8f9b	dd 77 01	. w .
	res 0,(ix+002h)		;8f9e	dd cb 02 86	. . . .
	jr l8fabh		;8fa2	18 07		. .
l8fa4h:
	res 3,(ix+002h)		;8fa4	dd cb 02 9e	. . . .
	call 00aadh		;8fa8	cd ad 0a	. . .
l8fabh:
	ld hl,(02312h)		;8fab	2a 12 23	* . #
	ld (02302h),hl		;8fae	22 02 23	" . #
l8fb1h:
	ld de,(00036h)		;8fb1	ed 5b 36 00	. [ 6 .
	xor a			;8fb5	af		.
	sbc hl,de		;8fb6	ed 52		. R
	add hl,de		;8fb8	19		.
	jr nz,l8fbeh		;8fb9	20 03		  .
	inc a			;8fbb	3c		<
	scf			;8fbc	37		7
	ret			;8fbd	c9		.
l8fbeh:
	ld e,(hl)		;8fbe	5e		^
	inc hl			;8fbf	23		#
	ld d,(hl)		;8fc0	56		V
	inc hl			;8fc1	23		#
l8fc2h:
	push hl			;8fc2	e5		.
	ld a,00dh		;8fc3	3e 0d		> .
	ld bc,00000h		;8fc5	01 00 00	. . .
	ld (022f4h),bc		;8fc8	ed 43 f4 22	. C . "
	cpir			;8fcc	ed b1		. .
	ld (02302h),hl		;8fce	22 02 23	" . #
	ex de,hl		;8fd1	eb		.
	ld de,(02327h)		;8fd2	ed 5b 27 23	. [ ' #
	bit 6,(ix+002h)		;8fd6	dd cb 02 76	. . . v
	call z,01ad2h		;8fda	cc d2 1a	. . .
	ld de,025a0h		;8fdd	11 a0 25	. . %
	ld (02327h),de		;8fe0	ed 53 27 23	. S ' #
	pop hl			;8fe4	e1		.
	dec hl			;8fe5	2b		+
	call 01b07h		;8fe6	cd 07 1b	. . .
	bit 7,a			;8fe9	cb 7f		. .
	push hl			;8feb	e5		.
	ld hl,(02327h)		;8fec	2a 27 23	* ' #
	ld (hl),020h		;8fef	36 20		6  
	ex de,hl		;8ff1	eb		.
	ld hl,0258ch		;8ff2	21 8c 25	! . %
	ld (02327h),hl		;8ff5	22 27 23	" ' #
	pop hl			;8ff8	e1		.
	ret			;8ff9	c9		.
	or a			;8ffa	b7		.
	sbc hl,de		;8ffb	ed 52		. R
	add hl,de		;8ffd	19		.
	ret z			;8ffe	c8		.
	ld a,(hl)		;8fff	7e		~
	inc hl			;9000	23		#
	ret			;9001	c9		.
	push ix			;9002	dd e5		. .
	ld ix,0038ch		;9004	dd 21 8c 03	. ! . .
	ld bc,00530h		;9008	01 30 05	. 0 .
l900bh:
	push de			;900b	d5		.
	ld e,(ix+000h)		;900c	dd 5e 00	. ^ .
	ld d,(ix+001h)		;900f	dd 56 01	. V .
	ld a,02fh		;9012	3e 2f		> /
	or a			;9014	b7		.
l9015h:
	inc a			;9015	3c		<
	sbc hl,de		;9016	ed 52		. R
	jr nc,l9015h		;9018	30 fb		0 .
	add hl,de		;901a	19		.
	pop de			;901b	d1		.
	cp c			;901c	b9		.
	jr nz,l9022h		;901d	20 03		  .
	ld a,020h		;901f	3e 20		>  
	inc c			;9021	0c		.
l9022h:
	ld (de),a		;9022	12		.
	dec c			;9023	0d		.
	inc de			;9024	13		.
	inc ix			;9025	dd 23		. #
	inc ix			;9027	dd 23		. #
	djnz l900bh		;9029	10 e0		. .
	pop ix			;902b	dd e1		. .
	ret			;902d	c9		.
	ld a,(hl)		;902e	7e		~
	cp 009h			;902f	fe 09		. .
	jr nz,l9035h		;9031	20 02		  .
	ld a,020h		;9033	3e 20		>  
l9035h:
	ld (de),a		;9035	12		.
	inc de			;9036	13		.
	inc hl			;9037	23		#
	ld a,(hl)		;9038	7e		~
	cp 030h			;9039	fe 30		. 0
	jr c,l9050h		;903b	38 13		8 .
	cp 041h			;903d	fe 41		. A
	jr c,l904bh		;903f	38 0a		8 .
	ld c,0c0h		;9041	0e c0		. .
	cp 07bh			;9043	fe 7b		. {
	ret c			;9045	d8		.
	ld c,098h		;9046	0e 98		. .
	cp 0a0h			;9048	fe a0		. .
	ret z			;904a	c8		.
l904bh:
	ld c,0e0h		;904b	0e e0		. .
	cp 03ah			;904d	fe 3a		. :
	ret c			;904f	d8		.
l9050h:
	push hl			;9050	e5		.
	ld hl,01eb8h		;9051	21 b8 1e	! . .
l9054h:
	ld c,(hl)		;9054	4e		N
	inc c			;9055	0c		.
	jr nz,l905ch		;9056	20 04		  .
	ld c,080h		;9058	0e 80		. .
	jr l9064h		;905a	18 08		. .
l905ch:
	inc hl			;905c	23		#
	bit 7,(hl)		;905d	cb 7e		. ~
	jr nz,l9054h		;905f	20 f3		  .
	cp (hl)			;9061	be		.
	jr nz,l905ch		;9062	20 f8		  .
l9064h:
	pop hl			;9064	e1		.
	bit 4,c			;9065	cb 61		. a
	ret nz			;9067	c0		.
	scf			;9068	37		7
	ret			;9069	c9		.
l906ah:
	bit 4,c			;906a	cb 61		. a
	ret nz			;906c	c0		.
	call 01afeh		;906d	cd fe 1a	. . .
	djnz l906ah		;9070	10 f8		. .
	cp (hl)			;9072	be		.
	ret			;9073	c9		.
	ld b,006h		;9074	06 06		. .
	call 01b3ah		;9076	cd 3a 1b	. : .
	jr z,l907eh		;9079	28 03		( .
l907bh:
	inc de			;907b	13		.
	djnz l907bh		;907c	10 fd		. .
l907eh:
	inc de			;907e	13		.
l907fh:
	bit 0,c			;907f	cb 41		. A
	ret nz			;9081	c0		.
	bit 3,c			;9082	cb 59		. Y
	ret nz			;9084	c0		.
	call 01b07h		;9085	cd 07 1b	. . .
	jr l907fh		;9088	18 f5		. .
	bit 3,c			;908a	cb 59		. Y
	jr z,l9090h		;908c	28 02		( .
	xor a			;908e	af		.
	ret			;908f	c9		.
l9090h:
	ld b,005h		;9090	06 05		. .
	call 01b3ah		;9092	cd 3a 1b	. : .
	jr z,l909ah		;9095	28 03		( .
l9097h:
	inc de			;9097	13		.
	djnz l9097h		;9098	10 fd		. .
l909ah:
	bit 0,c			;909a	cb 41		. A
	ret			;909c	c9		.
l909dh:
	ld a,e			;909d	7b		{
	call 01842h		;909e	cd 42 18	. B .
	ld (ix-009h),e		;90a1	dd 73 f7	. s .
l90a4h:
	ld b,l			;90a4	45		E
	pop hl			;90a5	e1		.
	call 01b7dh		;90a6	cd 7d 1b	. } .
	bit 2,c			;90a9	cb 51		. Q
	ret z			;90ab	c8		.
	inc hl			;90ac	23		#
	call 01b08h		;90ad	cd 08 1b	. . .
l90b0h:
	bit 0,c			;90b0	cb 41		. A
	ret z			;90b2	c8		.
	call 01b07h		;90b3	cd 07 1b	. . .
	jr l90b0h		;90b6	18 f8		. .
	ld b,000h		;90b8	06 00		. .
	push hl			;90ba	e5		.
	ld a,02bh		;90bb	3e 2b		> +
	ld (022fbh),a		;90bd	32 fb 22	2 . "
	ld h,(ix-00ah)		;90c0	dd 66 f6	. f .
	ld l,070h		;90c3	2e 70		. p
	ex (sp),hl		;90c5	e3		.
	ld de,00000h		;90c6	11 00 00	. . .
	call 01b7dh		;90c9	cd 7d 1b	. } .
	cp 028h			;90cc	fe 28		. (
	jr nz,l90f0h		;90ce	20 20		   
	ex (sp),hl		;90d0	e3		.
	set 2,h			;90d1	cb d4		. .
	inc l			;90d3	2c		,
	res 3,b			;90d4	cb 98		. .
	ex (sp),hl		;90d6	e3		.
	call 01b83h		;90d7	cd 83 1b	. . .
	jr c,l90f0h		;90da	38 14		8 .
	ex (sp),hl		;90dc	e3		.
	ld (ix-00ah),h		;90dd	dd 74 f6	. t .
	bit 4,b			;90e0	cb 60		. `
	jr nz,l909dh		;90e2	20 b9		  .
	bit 1,b			;90e4	cb 48		. H
	jr nz,l90a4h		;90e6	20 bc		  .
	ld (ix-00ch),e		;90e8	dd 73 f4	. s .
	ld (ix-00bh),d		;90eb	dd 72 f5	. r .
	jr l90a4h		;90ee	18 b4		. .
l90f0h:
	ld (0231fh),de		;90f0	ed 53 1f 23	. S . #
	bit 5,c			;90f4	cb 69		. i
	jr z,l9123h		;90f6	28 2b		( +
	cp 022h			;90f8	fe 22		. "
	jr nz,l910ch		;90fa	20 10		  .
	call 01b07h		;90fc	cd 07 1b	. . .
	ld d,000h		;90ff	16 00		. .
	ld e,a			;9101	5f		_
	inc hl			;9102	23		#
	cp 0a0h			;9103	fe a0		. .
	jr nz,l9120h		;9105	20 19		  .
	ld e,000h		;9107	1e 00		. .
	dec hl			;9109	2b		+
	jr l911fh		;910a	18 13		. .
l910ch:
	ex de,hl		;910c	eb		.
	cp 023h			;910d	fe 23		. #
	jr z,l911ah		;910f	28 09		( .
	cp 025h			;9111	fe 25		. %
	jr z,l911ah		;9113	28 05		( .
	call 01850h		;9115	cd 50 18	. P .
	jr l911eh		;9118	18 04		. .
l911ah:
	inc de			;911a	13		.
	call 0166ah		;911b	cd 6a 16	. j .
l911eh:
	ex de,hl		;911e	eb		.
l911fh:
	dec hl			;911f	2b		+
l9120h:
	ex (sp),hl		;9120	e3		.
	jr l9181h		;9121	18 5e		. ^
l9123h:
	bit 6,c			;9123	cb 71		. q
	jr z,l916dh		;9125	28 46		( F
	ex de,hl		;9127	eb		.
	call 01869h		;9128	cd 69 18	. i .
	dec hl			;912b	2b		+
	ex (sp),hl		;912c	e3		.
	jr nz,l9181h		;912d	20 52		  R
	bit 1,b			;912f	cb 48		. H
	ld a,009h		;9131	3e 09		> .
	jr nz,l917eh		;9133	20 49		  I
	bit 2,b			;9135	cb 50		. P
	jr nz,l917eh		;9137	20 45		  E
	set 1,b			;9139	cb c8		. .
	ld a,d			;913b	7a		z
	cp 062h			;913c	fe 62		. b
	jr nz,l9161h		;913e	20 21		  !
	ld a,e			;9140	7b		{
	and 030h		;9141	e6 30		. 0
	push af			;9143	f5		.
	or b			;9144	b0		.
	ld b,a			;9145	47		G
	rrca			;9146	0f		.
	rrca			;9147	0f		.
	rrca			;9148	0f		.
	rrca			;9149	0f		.
	and l			;914a	a5		.
	and 001h		;914b	e6 01		. .
	or h			;914d	b4		.
	ld h,a			;914e	67		g
	pop af			;914f	f1		.
	bit 7,h			;9150	cb 7c		. |
	jr nz,l915ah		;9152	20 06		  .
	set 7,h			;9154	cb fc		. .
	or h			;9156	b4		.
	ld h,a			;9157	67		g
	jr l9161h		;9158	18 07		. .
l915ah:
	xor h			;915a	ac		.
	and 030h		;915b	e6 30		. 0
	ld a,008h		;915d	3e 08		> .
	jr nz,l917eh		;915f	20 1d		  .
l9161h:
	ld a,l			;9161	7d		}
	and 001h		;9162	e6 01		. .
	or d			;9164	b2		.
	ld l,a			;9165	6f		o
	ld de,(0231fh)		;9166	ed 5b 1f 23	. [ . #
l916ah:
	jp 01ba4h		;916a	c3 a4 1b	. . .
l916dh:
	set 3,b			;916d	cb d8		. .
	ex (sp),hl		;916f	e3		.
	ld (022fbh),a		;9170	32 fb 22	2 . "
	cp 02dh			;9173	fe 2d		. -
	jp z,01ba6h		;9175	ca a6 1b	. . .
	bit 1,c			;9178	cb 49		. I
	jr nz,l916ah		;917a	20 ee		  .
	ld a,005h		;917c	3e 05		> .
l917eh:
	jp 01dbbh		;917e	c3 bb 1d	. . .
l9181h:
	push hl			;9181	e5		.
	bit 3,b			;9182	cb 58		. X
	ld hl,(0231fh)		;9184	2a 1f 23	* . #
	jp nz,01d04h		;9187	c2 04 1d	. . .
	ld a,(022fbh)		;918a	3a fb 22	: . "
	cp 026h			;918d	fe 26		. &
	jr z,l91e3h		;918f	28 52		( R
	cp 040h			;9191	fe 40		. @
	jr z,l91ebh		;9193	28 56		( V
	cp 021h			;9195	fe 21		. !
	jr z,l91f2h		;9197	28 59		( Y
	cp 02ah			;9199	fe 2a		. *
	jr z,l91afh		;919b	28 12		( .
	cp 02fh			;919d	fe 2f		. /
	jr z,l91f9h		;919f	28 58		( X
	cp 03fh			;91a1	fe 3f		. ?
	jr z,l91f9h		;91a3	28 54		( T
	ld a,h			;91a5	7c		|
	or d			;91a6	b2		.
	add hl,de		;91a7	19		.
	jp 01d07h		;91a8	c3 07 1d	. . .
l91abh:
	ld a,015h		;91ab	3e 15		> .
l91adh:
	jr l917eh		;91ad	18 cf		. .
l91afh:
	ld a,d			;91af	7a		z
	xor h			;91b0	ac		.
	ld c,a			;91b1	4f		O
	call 01cf9h		;91b2	cd f9 1c	. . .
	ex de,hl		;91b5	eb		.
	call 01cf9h		;91b6	cd f9 1c	. . .
	xor a			;91b9	af		.
	sbc hl,de		;91ba	ed 52		. R
	add hl,de		;91bc	19		.
	jr nc,l91c0h		;91bd	30 01		0 .
	ex de,hl		;91bf	eb		.
l91c0h:
	or d			;91c0	b2		.
	jr nz,l91abh		;91c1	20 e8		  .
	or e			;91c3	b3		.
	ld e,d			;91c4	5a		Z
	jr nz,l91d0h		;91c5	20 09		  .
	ex de,hl		;91c7	eb		.
	jr l9237h		;91c8	18 6d		. m
l91cah:
	ex de,hl		;91ca	eb		.
	add hl,de		;91cb	19		.
	ex de,hl		;91cc	eb		.
l91cdh:
	add hl,hl		;91cd	29		)
	jr c,l91abh		;91ce	38 db		8 .
l91d0h:
	rra			;91d0	1f		.
	jr nc,l91cdh		;91d1	30 fa		0 .
	or a			;91d3	b7		.
	jr nz,l91cah		;91d4	20 f4		  .
	adc hl,de		;91d6	ed 5a		. Z
	jr c,l91abh		;91d8	38 d1		8 .
	jp m,01c7bh		;91da	fa 7b 1c	. { .
	or c			;91dd	b1		.
	call m,01cfch		;91de	fc fc 1c	. . .
	jr l91e9h		;91e1	18 06		. .
l91e3h:
	ld a,l			;91e3	7d		}
	and e			;91e4	a3		.
	ld l,a			;91e5	6f		o
	ld a,h			;91e6	7c		|
	and d			;91e7	a2		.
l91e8h:
	ld h,a			;91e8	67		g
l91e9h:
	jr l9237h		;91e9	18 4c		. L
l91ebh:
	ld a,l			;91eb	7d		}
	or e			;91ec	b3		.
	ld l,a			;91ed	6f		o
	ld a,h			;91ee	7c		|
	or d			;91ef	b2		.
	jr l91e8h		;91f0	18 f6		. .
l91f2h:
	ld a,l			;91f2	7d		}
	xor e			;91f3	ab		.
	ld l,a			;91f4	6f		o
	ld a,h			;91f5	7c		|
	xor d			;91f6	aa		.
	jr l91e8h		;91f7	18 ef		. .
l91f9h:
	push bc			;91f9	c5		.
	ld b,a			;91fa	47		G
	ld a,d			;91fb	7a		z
	or e			;91fc	b3		.
	ld a,014h		;91fd	3e 14		> .
l91ffh:
	jr z,l91adh		;91ff	28 ac		( .
	ld a,d			;9201	7a		z
	xor h			;9202	ac		.
	ld a,h			;9203	7c		|
	push af			;9204	f5		.
	call 01cf9h		;9205	cd f9 1c	. . .
	ex de,hl		;9208	eb		.
	call 01cf9h		;9209	cd f9 1c	. . .
	ex de,hl		;920c	eb		.
	ld a,b			;920d	78		x
	ld bc,00000h		;920e	01 00 00	. . .
l9211h:
	sbc hl,de		;9211	ed 52		. R
	inc bc			;9213	03		.
	jr nc,l9211h		;9214	30 fb		0 .
	dec bc			;9216	0b		.
	add hl,de		;9217	19		.
	cp 03fh			;9218	fe 3f		. ?
	jr z,l9225h		;921a	28 09		( .
	ld h,b			;921c	60		`
	ld l,c			;921d	69		i
	pop af			;921e	f1		.
l921fh:
	call m,01cfch		;921f	fc fc 1c	. . .
	pop bc			;9222	c1		.
	jr l9237h		;9223	18 12		. .
l9225h:
	pop af			;9225	f1		.
	or a			;9226	b7		.
	jr l921fh		;9227	18 f6		. .
	ld a,h			;9229	7c		|
	or a			;922a	b7		.
	ret p			;922b	f0		.
	xor a			;922c	af		.
	sub l			;922d	95		.
	ld l,a			;922e	6f		o
	ld a,000h		;922f	3e 00		> .
	sbc a,h			;9231	9c		.
	ld h,a			;9232	67		g
	ret			;9233	c9		.
	or a			;9234	b7		.
	sbc hl,de		;9235	ed 52		. R
l9237h:
	ex de,hl		;9237	eb		.
	pop hl			;9238	e1		.
	bit 1,b			;9239	cb 48		. H
	jr z,l9247h		;923b	28 0a		( .
	bit 4,b			;923d	cb 60		. `
	ld a,006h		;923f	3e 06		> .
	jr z,l91ffh		;9241	28 bc		( .
	bit 0,l			;9243	cb 45		. E
	jr z,l91ffh		;9245	28 b8		( .
l9247h:
	jp 01c3ah		;9247	c3 3a 1c	. : .
	exx			;924a	d9		.
	ld hl,(02321h)		;924b	2a 21 23	* ! #
	bit 1,(ix+000h)		;924e	dd cb 00 4e	. . . N
	jr nz,l92a9h		;9252	20 55		  U
	bit 1,(ix+001h)		;9254	dd cb 01 4e	. . . N
	jr nz,l9298h		;9258	20 3e		  >
	push hl			;925a	e5		.
	ld hl,(0230ch)		;925b	2a 0c 23	* . #
	bit 5,(ix+001h)		;925e	dd cb 01 6e	. . . n
	jr nz,l9284h		;9262	20 20		   
	ld de,(0231dh)		;9264	ed 5b 1d 23	. [ . #
	or a			;9268	b7		.
	sbc hl,de		;9269	ed 52		. R
	add hl,de		;926b	19		.
	jr nc,l9284h		;926c	30 16		0 .
	ld de,(0231bh)		;926e	ed 5b 1b 23	. [ . #
	or a			;9272	b7		.
	sbc hl,de		;9273	ed 52		. R
	add hl,de		;9275	19		.
	jr c,l9284h		;9276	38 0c		8 .
l9278h:
	exx			;9278	d9		.
	ld hl,022f6h		;9279	21 f6 22	! . "
	ld (hl),0c9h		;927c	36 c9		6 .
	ld hl,0239ah		;927e	21 9a 23	! . #
	jp 019d3h		;9281	c3 d3 19	. . .
l9284h:
	ld de,(0233dh)		;9284	ed 5b 3d 23	. [ = #
	or a			;9288	b7		.
	sbc hl,de		;9289	ed 52		. R
	add hl,de		;928b	19		.
	ccf			;928c	3f		?
	call c,01d7fh		;928d	dc 7f 1d	. . .
	jr c,l9278h		;9290	38 e6		8 .
	ld (hl),a		;9292	77		w
	inc hl			;9293	23		#
	ld (0230ch),hl		;9294	22 0c 23	" . #
	pop hl			;9297	e1		.
l9298h:
	bit 3,(ix-00ah)		;9298	dd cb f6 5e	. . . ^
	jr nz,l92a9h		;929c	20 0b		  .
	push hl			;929e	e5		.
	ld hl,(02327h)		;929f	2a 27 23	* ' #
	call 01e10h		;92a2	cd 10 1e	. . .
	ld (02327h),hl		;92a5	22 27 23	" ' #
	pop hl			;92a8	e1		.
l92a9h:
	inc hl			;92a9	23		#
	ld (02321h),hl		;92aa	22 21 23	" ! #
	exx			;92ad	d9		.
	ret			;92ae	c9		.
	ld c,a			;92af	4f		O
	ld a,0c9h		;92b0	3e c9		> .
	ld (022f6h),a		;92b2	32 f6 22	2 . "
	ld de,(0233fh)		;92b5	ed 5b 3f 23	. [ ? #
	ld a,d			;92b9	7a		z
	or e			;92ba	b3		.
	scf			;92bb	37		7
	ret z			;92bc	c8		.
	ld a,c			;92bd	79		y
	push af			;92be	f5		.
	push ix			;92bf	dd e5		. .
	ld ix,(0232dh)		;92c1	dd 2a 2d 23	. * - #
	ld (02329h),ix		;92c5	dd 22 29 23	. " ) #
	ld (05c51h),ix		;92c9	dd 22 51 5c	. " Q \
	ex de,hl		;92cd	eb		.
	ld hl,(0230eh)		;92ce	2a 0e 23	* . #
l92d1h:
	or a			;92d1	b7		.
	sbc hl,de		;92d2	ed 52		. R
	add hl,de		;92d4	19		.
	jr z,l92dch		;92d5	28 05		( .
	ld a,(hl)		;92d7	7e		~
	rst 10h			;92d8	d7		.
	inc hl			;92d9	23		#
	jr l92d1h		;92da	18 f5		. .
l92dch:
	ld hl,(022feh)		;92dc	2a fe 22	* . "
	ld (0230eh),hl		;92df	22 0e 23	" . #
	ld (0230ch),hl		;92e2	22 0c 23	" . #
	pop ix			;92e5	dd e1		. .
	pop af			;92e7	f1		.
	scf			;92e8	37		7
	ccf			;92e9	3f		?
	ret			;92ea	c9		.
	ld hl,0163ah		;92eb	21 3a 16	! : .
	push hl			;92ee	e5		.
	bit 0,(ix+001h)		;92ef	dd cb 01 46	. . . F
	ret nz			;92f3	c0		.
	bit 7,(ix+001h)		;92f4	dd cb 01 7e	. . . ~
	ld c,a			;92f8	4f		O
	ld b,(ix+002h)		;92f9	dd 46 02	. F .
	push bc			;92fc	c5		.
	set 1,(ix+002h)		;92fd	dd cb 02 ce	. . . .
	call nz,0173eh		;9301	c4 3e 17	. > .
	pop bc			;9304	c1		.
	ld a,(022f9h)		;9305	3a f9 22	: . "
	add a,001h		;9308	c6 01		. .
	daa			;930a	27		'
	ld (022f9h),a		;930b	32 f9 22	2 . "
	ld a,c			;930e	79		y
	push bc			;930f	c5		.
	ld hl,0249ch		;9310	21 9c 24	! . $
	ld de,0258ch		;9313	11 8c 25	. . %
	ld bc,0000bh		;9316	01 0b 00	. . .
	ldir			;9319	ed b0		. .
	ld hl,0258ch		;931b	21 8c 25	! . %
	call 01788h		;931e	cd 88 17	. . .
	ld (hl),0a0h		;9321	36 a0		6 .
	res 1,(ix+002h)		;9323	dd cb 02 8e	. . . .
	call 0173eh		;9327	cd 3e 17	. > .
	call 003b7h		;932a	cd b7 03	. . .
	pop bc			;932d	c1		.
	ld a,00fh		;932e	3e 0f		> .
	and b			;9330	a0		.
	ld (ix+002h),a		;9331	dd 77 02	. w .
	res 0,(ix+003h)		;9334	dd cb 03 86	. . . .
	jp 0164ah		;9338	c3 4a 16	. J .
	ld a,d			;933b	7a		z
	call 01e10h		;933c	cd 10 1e	. . .
	ld a,e			;933f	7b		{
	push af			;9340	f5		.
	rrca			;9341	0f		.
	rrca			;9342	0f		.
	rrca			;9343	0f		.
	rrca			;9344	0f		.
	call 01e19h		;9345	cd 19 1e	. . .
	pop af			;9348	f1		.
	and 00fh		;9349	e6 0f		. .
	add a,090h		;934b	c6 90		. .
	daa			;934d	27		'
	adc a,040h		;934e	ce 40		. @
	daa			;9350	27		'
	ld (hl),a		;9351	77		w
	inc hl			;9352	23		#
	ret			;9353	c9		.
	call 00d93h		;9354	cd 93 0d	. . .
	ex de,hl		;9357	eb		.
	call 00455h		;9358	cd 55 04	. U .
	inc hl			;935b	23		#
	ex de,hl		;935c	eb		.
	ret			;935d	c9		.
	ld de,023a3h		;935e	11 a3 23	. . #
	call 01e27h		;9361	cd 27 1e	. ' .
	ld a,(022e2h)		;9364	3a e2 22	: . "
	call 0001dh		;9367	cd 1d 00	. . .
	call 01e27h		;936a	cd 27 1e	. ' .
	ld hl,(022d6h)		;936d	2a d6 22	* . "
	call 01e24h		;9370	cd 24 1e	. $ .
	ld hl,(022d8h)		;9373	2a d8 22	* . "
	call 01e24h		;9376	cd 24 1e	. $ .
	ld hl,(0233dh)		;9379	2a 3d 23	* = #
	call 01e24h		;937c	cd 24 1e	. $ .
	ld hl,(022e8h)		;937f	2a e8 22	* . "
	call 01e24h		;9382	cd 24 1e	. $ .
	ld hl,(00036h)		;9385	2a 36 00	* 6 .
	call 01e24h		;9388	cd 24 1e	. $ .
	ld hl,022ach		;938b	21 ac 22	! . "
l938eh:
	ld a,(hl)		;938e	7e		~
	or a			;938f	b7		.
	jp z,003b7h		;9390	ca b7 03	. . .
	cp 00dh			;9393	fe 0d		. .
	jr z,l939dh		;9395	28 06		( .
	call 0001dh		;9397	cd 1d 00	. . .
	inc hl			;939a	23		#
	jr l938eh		;939b	18 f1		. .
l939dh:
	inc hl			;939d	23		#
	ld a,(hl)		;939e	7e		~
	cp 00dh			;939f	fe 0d		. .
	jr z,l939dh		;93a1	28 fa		( .
	call 003bbh		;93a3	cd bb 03	. . .
	jr l938eh		;93a6	18 e6		. .
	call 00d6bh		;93a8	cd 6b 0d	. k .
	ld a,002h		;93ab	3e 02		> .
	call 01601h		;93ad	cd 01 16	. . .
	ld (iy+052h),0ffh	;93b0	fd 36 52 ff	. 6 R .
	ld hl,05cf6h		;93b4	21 f6 5c	! . \
	ld bc,(022dah)		;93b7	ed 4b da 22	. K . "
	ld a,b			;93bb	78		x
	or c			;93bc	b1		.
	jr z,l93c0h		;93bd	28 01		( .
	dec c			;93bf	0d		.
l93c0h:
	ld (hl),c		;93c0	71		q
	ld a,c			;93c1	79		y
	ld c,001h		;93c2	0e 01		. .
	call 02666h		;93c4	cd 66 26	. f &
	ld c,007h		;93c7	0e 07		. .
	ld hl,01eafh		;93c9	21 af 1e	! . .
	ld a,002h		;93cc	3e 02		> .
	call 03d13h		;93ce	cd 13 3d	. . =
	ld a,002h		;93d1	3e 02		> .
	call 0006ch		;93d3	cd 6c 00	. l .
	ld a,016h		;93d6	3e 16		> .
	rst 10h			;93d8	d7		.
	ld a,016h		;93d9	3e 16		> .
	rst 10h			;93db	d7		.
	xor a			;93dc	af		.
	rst 10h			;93dd	d7		.
	ret			;93de	c9		.
	ld hl,(004b0h)		;93df	2a b0 04	* . .
	inc hl			;93e2	23		#
	ld e,(hl)		;93e3	5e		^
	inc hl			;93e4	23		#
	ld d,(hl)		;93e5	56		V
	ex de,hl		;93e6	eb		.
	jp (hl)			;93e7	e9		.
	sub b			;93e8	90		.
	jr nz,l93f4h		;93e9	20 09		  .
	sub e			;93eb	93		.
	inc l			;93ec	2c		,
	sub a			;93ed	97		.
	dec sp			;93ee	3b		;
	dec c			;93ef	0d		.
	add a,c			;93f0	81		.
	dec hl			;93f1	2b		+
	ld h,040h		;93f2	26 40		& @
l93f4h:
	ld hl,(02f21h)		;93f4	2a 21 2f	* ! /
	ccf			;93f7	3f		?
	add hl,hl		;93f8	29		)
	daa			;93f9	27		'
	cp a			;93fa	bf		.
	inc h			;93fb	24		$
	dec a			;93fc	3d		=
	sbc a,a			;93fd	9f		.
	dec h			;93fe	25		%
	inc hl			;93ff	23		#
	ld (l80ffh),hl		;9400	22 ff 80	" . .
	nop			;9403	00		.
	ld bc,0444ch		;9404	01 4c 44	. L D
	ld h,b			;9407	60		`
	add a,e			;9408	83		.
	ld a,b			;9409	78		x
	inc b			;940a	04		.
	call p,0043eh		;940b	f4 3e 04	. > .
	or c			;940e	b1		.
	ld a,(bc)		;940f	0a		.
	inc b			;9410	04		.
	push hl			;9411	e5		.
	ld a,(de)		;9412	1a		.
	inc b			;9413	04		.
	di			;9414	f3		.
	ld a,(0ec14h)		;9415	3a 14 ec	: . .
	ld d,a			;9418	57		W
	inc d			;9419	14		.
	xor 05fh		;941a	ee 5f		. _
	inc bc			;941c	03		.
	nop			;941d	00		.
	add a,e			;941e	83		.
	ld b,b			;941f	40		@
	inc b			;9420	04		.
	call p,00306h		;9421	f4 06 03	. . .
	ld h,d			;9424	62		b
	jp p,00421h		;9425	f2 21 04	. ! .
	di			;9428	f3		.
	ld hl,(06a03h)		;9429	2a 03 6a	* . j
	jp p,00431h		;942c	f2 31 04	. 1 .
	jp po,014f9h		;942f	e2 f9 14	. . .
	di			;9432	f3		.
	ld a,e			;9433	7b		{
	inc bc			;9434	03		.
	inc c			;9435	0c		.
	jp p,01401h		;9436	f2 01 14	. . .
	di			;9439	f3		.
	ld c,e			;943a	4b		K
	inc bc			;943b	03		.
	ld (hl),e		;943c	73		s
	jp po,01422h		;943d	e2 22 14	. " .
	adc a,h			;9440	8c		.
	ld b,e			;9441	43		C
	inc b			;9442	04		.
	ret po			;9443	e0		.
	ld (03103h),a		;9444	32 03 31	2 . 1
	ret po			;9447	e0		.
	ld (bc),a		;9448	02		.
	inc bc			;9449	03		.
	ld h,l			;944a	65		e
	ret po			;944b	e0		.
	ld (de),a		;944c	12		.
	inc de			;944d	13		.
	ret			;944e	c9		.
	and b			;944f	a0		.
	inc d			;9450	14		.
	jp nc,013b0h		;9451	d2 b0 13	. . .
	call nz,014a8h		;9454	c4 a8 14	. . .
	jp nc,013b8h		;9457	d2 b8 13	. . .
	ld l,h			;945a	6c		l
	ret po			;945b	e0		.
	ld b,a			;945c	47		G
	inc de			;945d	13		.
	ld l,(hl)		;945e	6e		n
	ret po			;945f	e0		.
	ld c,a			;9460	4f		O
	ld bc,0524ah		;9461	01 4a 52	. J R
	ret m			;9464	f8		.
	jr $+5			;9465	18 03		. .
	add hl,bc		;9467	09		.
	ret m			;9468	f8		.
	jr nz,$+4		;9469	20 02		  .
	ld d,b			;946b	50		P
	jp p,003c3h		;946c	f2 c3 03	. . .
	ld b,0f2h		;946f	06 f2		. .
	jp nz,0e343h		;9471	c2 43 e3	. C .
	jp (hl)			;9474	e9		.
	ld bc,05550h		;9475	01 50 55	. P U
	ld d,e			;9478	53		S
	ld c,b			;9479	48		H
	adc a,a			;947a	8f		.
	push bc			;947b	c5		.
	ld (bc),a		;947c	02		.
	ld c,a			;947d	4f		O
	ld d,b			;947e	50		P
	adc a,a			;947f	8f		.
	pop bc			;9480	c1		.
	ld bc,04143h		;9481	01 43 41	. C A
	ld c,h			;9484	4c		L
	ld c,h			;9485	4c		L
	jp p,005cdh		;9486	f2 cd 05	. . .
	ld b,0f2h		;9489	06 f2		. .
	call nz,05002h		;948b	c4 02 50	. . P
	add a,e			;948e	83		.
	cp b			;948f	b8		.
	inc bc			;9490	03		.
	call p,003feh		;9491	f4 fe 03	. . .
	call z,0132fh		;9494	cc 2f 13	. / .
	ret			;9497	c9		.
	and c			;9498	a1		.
	inc d			;9499	14		.
	jp nc,013b1h		;949a	d2 b1 13	. . .
	call nz,014a9h		;949d	c4 a9 14	. . .
	jp nc,002b9h		;94a0	d2 b9 02	. . .
	ld b,e			;94a3	43		C
	add a,03fh		;94a4	c6 3f		. ?
	add a,c			;94a6	81		.
	ld c,l			;94a7	4d		M
	ld b,c			;94a8	41		A
	jp l810ah		;94a9	c3 0a 81	. . .
	ld b,l			;94ac	45		E
	ld d,c			;94ad	51		Q
	push de			;94ae	d5		.
	nop			;94af	00		.
	add a,d			;94b0	82		.
	ld c,(hl)		;94b1	4e		N
	call nz,l8406h+1	;94b2	c4 07 84	. . .
	call 0830bh		;94b5	cd 0b 83	. . .
	call nc,sub_8209h	;94b8	d4 09 82	. . .
	ld c,h			;94bb	4c		L
	ld d,e			;94bc	53		S
	push bc			;94bd	c5		.
	ex af,af'		;94be	08		.
	ld (bc),a		;94bf	02		.
	ld e,b			;94c0	58		X
	ld l,b			;94c1	68		h
	ret pe			;94c2	e8		.
	ex af,af'		;94c3	08		.
	inc bc			;94c4	03		.
	ld h,h			;94c5	64		d
	jp po,003ebh		;94c6	e2 eb 03	. . .
	ld l,e			;94c9	6b		k
	jp po,003e3h		;94ca	e2 e3 03	. . .
	ret c			;94cd	d8		.
	exx			;94ce	d9		.
	ld (bc),a		;94cf	02		.
	ret			;94d0	c9		.
	ei			;94d1	fb		.
	ld bc,04e49h		;94d2	01 49 4e	. I N
	ld b,e			;94d5	43		C
	add a,b			;94d6	80		.
	inc b			;94d7	04		.
	inc b			;94d8	04		.
	adc a,h			;94d9	8c		.
	inc bc			;94da	03		.
	inc bc			;94db	03		.
	ld h,b			;94dc	60		`
	push af			;94dd	f5		.
	in a,(014h)		;94de	db 14		. .
	rst 20h			;94e0	e7		.
	ld a,b			;94e1	78		x
	inc de			;94e2	13		.
	nop			;94e3	00		.
	rst 20h			;94e4	e7		.
	ld b,b			;94e5	40		@
	inc de			;94e6	13		.
	ret			;94e7	c9		.
	and d			;94e8	a2		.
	inc d			;94e9	14		.
	jp nc,013b2h		;94ea	d2 b2 13	. . .
	call nz,014aah		;94ed	c4 aa 14	. . .
	jp nc,012bah		;94f0	d2 ba 12	. . .
	ld c,l			;94f3	4d		M
	cp 046h			;94f4	fe 46		. F
	add a,d			;94f6	82		.
	add a,003h		;94f7	c6 03		. .
	add a,c			;94f9	81		.
	ld b,h			;94fa	44		D
	ld b,l			;94fb	45		E
	ld b,(hl)		;94fc	46		F
	jp nz,l8406h		;94fd	c2 06 84	. . .
l9500h:
	rst 10h			;9500	d7		.
	dec b			;9501	05		.
	add a,h			;9502	84		.
	call 08404h		;9503	cd 04 84	. . .
	out (002h),a		;9506	d3 02		. .
	inc bc			;9508	03		.
	ld b,e			;9509	43		C
	add a,b			;950a	80		.
	dec b			;950b	05		.
	inc b			;950c	04		.
	adc a,h			;950d	8c		.
	dec bc			;950e	0b		.
	ld (bc),a		;950f	02		.
	ld c,d			;9510	4a		J
	ld c,(hl)		;9511	4e		N
	ld e,d			;9512	5a		Z
	ret m			;9513	f8		.
	djnz l9518h		;9514	10 02		. .
	ld b,c			;9516	41		A
	pop bc			;9517	c1		.
l9518h:
	daa			;9518	27		'
	ld (bc),a		;9519	02		.
	ret			;951a	c9		.
	di			;951b	f3		.
	ld de,04253h		;951c	11 53 42	. S B
	ld b,e			;951f	43		C
	ld h,d			;9520	62		b
	adc a,h			;9521	8c		.
	ld b,d			;9522	42		B
	inc b			;9523	04		.
	ld h,b			;9524	60		`
	add a,e			;9525	83		.
	sbc a,b			;9526	98		.
	dec b			;9527	05		.
	call p,002deh		;9528	f4 de 02	. . .
	ld d,l			;952b	55		U
	ld b,d			;952c	42		B
	add a,e			;952d	83		.
	sub b			;952e	90		.
	inc b			;952f	04		.
	call p,00ad6h		;9530	f4 d6 0a	. . .
	ld c,h			;9533	4c		L
	ld b,c			;9534	41		A
	add a,e			;9535	83		.
	jr nz,l9542h		;9536	20 0a		  .
	ld d,d			;9538	52		R
	ld b,c			;9539	41		A
	add a,e			;953a	83		.
	jr z,l9548h		;953b	28 0b		( .
	ld c,h			;953d	4c		L
	add a,e			;953e	83		.
	jr c,$+12		;953f	38 0a		8 .
	ld b,l			;9541	45		E
l9542h:
	ld d,h			;9542	54		T
	ld a,d			;9543	7a		z
	add a,e			;9544	83		.
	ret nz			;9545	c0		.
	ld (bc),a		;9546	02		.
	ld b,e			;9547	43		C
l9548h:
	add a,037h		;9548	c6 37		. 7
	ld bc,04552h		;954a	01 52 45	. R E
	call nc,004c9h		;954d	d4 c9 04	. . .
	add a,(hl)		;9550	86		.
	ret nz			;9551	c0		.
	inc d			;9552	14		.
	ret			;9553	c9		.
	ld c,l			;9554	4d		M
	inc d			;9555	14		.
	adc a,045h		;9556	ce 45		. E
	dec bc			;9558	0b		.
	ld d,e			;9559	53		S
	ld a,d			;955a	7a		z
	add a,e			;955b	83		.
	add a,b			;955c	80		.
	ld a,(bc)		;955d	0a		.
	ld c,h			;955e	4c		L
	ld b,e			;955f	43		C
	add a,e			;9560	83		.
	nop			;9561	00		.
	inc b			;9562	04		.
	pop bc			;9563	c1		.
	rlca			;9564	07		.
	dec bc			;9565	0b		.
	add a,e			;9566	83		.
	djnz l956ch		;9567	10 03		. .
	pop bc			;9569	c1		.
	rla			;956a	17		.
	inc de			;956b	13		.
l956ch:
	call nz,00a6fh		;956c	c4 6f 0a	. o .
	ld d,d			;956f	52		R
	ld b,e			;9570	43		C
	add a,e			;9571	83		.
	ex af,af'		;9572	08		.
	inc b			;9573	04		.
	pop bc			;9574	c1		.
	rrca			;9575	0f		.
	dec bc			;9576	0b		.
	add a,e			;9577	83		.
	jr l957dh		;9578	18 03		. .
	pop bc			;957a	c1		.
	rra			;957b	1f		.
	inc de			;957c	13		.
l957dh:
	call nz,00267h		;957d	c4 67 02	. g .
	ld d,e			;9580	53		S
	ld d,h			;9581	54		T
	call m,002c7h		;9582	fc c7 02	. . .
	ld b,e			;9585	43		C
	ld b,c			;9586	41		A
	ld c,h			;9587	4c		L
	ret m			;9588	f8		.
	rst 28h			;9589	ef		.
	dec b			;958a	05		.
	add hl,bc		;958b	09		.
	ret m			;958c	f8		.
	rst 28h			;958d	ef		.
	ld bc,0524fh		;958e	01 4f 52	. O R
	add a,e			;9591	83		.
	or b			;9592	b0		.
	inc bc			;9593	03		.
	call p,sub_83f6h	;9594	f4 f6 83	. . .
	rst 0			;9597	c7		.
	ld bc,05502h		;9598	01 02 55	. . U
	ld d,h			;959b	54		T
	ld (hl),l		;959c	75		u
	ret po			;959d	e0		.
	out (014h),a		;959e	d3 14		. .
	ld h,a			;95a0	67		g
	add a,b			;95a1	80		.
	ld b,c			;95a2	41		A
	inc d			;95a3	14		.
	ret			;95a4	c9		.
	and e			;95a5	a3		.
	inc d			;95a6	14		.
	call nz,012abh		;95a7	c4 ab 12	. . .
	ld d,h			;95aa	54		T
	ld c,c			;95ab	49		I
	jp nc,013b3h		;95ac	d2 b3 13	. . .
	ld b,h			;95af	44		D
	jp nc,001bbh		;95b0	d2 bb 01	. . .
	ld b,c			;95b3	41		A
	ld b,h			;95b4	44		D
	ld b,h			;95b5	44		D
	ld h,b			;95b6	60		`
	add a,e			;95b7	83		.
	add a,b			;95b8	80		.
	dec b			;95b9	05		.
	call p,004c6h		;95ba	f4 c6 04	. . .
	ld h,d			;95bd	62		b
	adc a,h			;95be	8c		.
	add hl,bc		;95bf	09		.
	inc bc			;95c0	03		.
	ld b,e			;95c1	43		C
	ld h,b			;95c2	60		`
	add a,e			;95c3	83		.
	adc a,b			;95c4	88		.
	dec b			;95c5	05		.
	call p,014ceh		;95c6	f4 ce 14	. . .
	ld h,d			;95c9	62		b
	adc a,h			;95ca	8c		.
	ld c,d			;95cb	4a		J
	ld (bc),a		;95cc	02		.
	ld c,(hl)		;95cd	4e		N
	ld b,h			;95ce	44		D
	add a,e			;95cf	83		.
	and b			;95d0	a0		.
	inc b			;95d1	04		.
	call p,001e6h		;95d2	f4 e6 01	. . .
	ld e,b			;95d5	58		X
	ld c,a			;95d6	4f		O
	ld d,d			;95d7	52		R
	add a,e			;95d8	83		.
	xor b			;95d9	a8		.
	inc b			;95da	04		.
	call p,009eeh		;95db	f4 ee 09	. . .
	ld b,d			;95de	42		B
	ld c,c			;95df	49		I
	ld d,h			;95e0	54		T
	ld a,d			;95e1	7a		z
	add a,e			;95e2	83		.
	ld b,b			;95e3	40		@
	ld (bc),a		;95e4	02		.
	ld d,d			;95e5	52		R
	set 7,a			;95e6	cb ff		. .
	ld de,0454eh		;95e8	11 4e 45	. N E
	rst 0			;95eb	c7		.
	ld b,h			;95ec	44		D
	ld (bc),a		;95ed	02		.
	ld c,a			;95ee	4f		O
	ret nc			;95ef	d0		.
	nop			;95f0	00		.
	ld bc,04148h		;95f1	01 48 41	. H A
	ld c,h			;95f4	4c		L
	call nc,00076h		;95f5	d4 76 00	. v .
	add a,b			;95f8	80		.
	nop			;95f9	00		.
	ld bc,026c8h		;95fa	01 c8 26	. . &
	ld (bc),a		;95fd	02		.
	call z,00162h		;95fe	cc 62 01	. b .
	pop bc			;9601	c1		.
	ld h,b			;9602	60		`
	ld (bc),a		;9603	02		.
	add a,068h		;9604	c6 68		. h
	ld bc,022c4h		;9606	01 c4 22	. . "
	ld (bc),a		;9609	02		.
	push bc			;960a	c5		.
	ld h,h			;960b	64		d
	ld bc,020c2h		;960c	01 c2 20	. .  
	ld (bc),a		;960f	02		.
	jp 00130h		;9610	c3 30 01	. 0 .
	jp c,0012ch		;9613	da 2c 01	. , .
	jp 00166h		;9616	c3 66 01	. f .
	ld c,(hl)		;9619	4e		N
	jp c,0022ah		;961a	da 2a 02	. * .
	jp 0012eh		;961d	c3 2e 01	. . .
	ld d,e			;9620	53		S
	ret nc			;9621	d0		.
	ld l,d			;9622	6a		j
	add a,c			;9623	81		.
	and h			;9624	a4		.
	nop			;9625	00		.
	ld bc,024c5h		;9626	01 c5 24	. . $
	ld bc,028cch		;9629	01 cc 28	. . (
	ld bc,06cc9h		;962c	01 c9 6c	. . l
	ld (de),a		;962f	12		.
	ret c			;9630	d8		.
	ld h,d			;9631	62		b
	ld (062d9h),a		;9632	32 d9 62	2 . b
	ld bc,038cdh		;9635	01 cd 38	. . 8
	ld bc,036d0h		;9638	01 d0 36	. . 6
	ld (bc),a		;963b	02		.
	push bc			;963c	c5		.
	inc (hl)		;963d	34		4
	ld (bc),a		;963e	02		.
	rst 8			;963f	cf		.
	ld (0d201h),a		;9640	32 01 d2	2 . .
	ld l,(hl)		;9643	6e		n
	nop			;9644	00		.
	ld h,b			;9645	60		`
	ld h,e			;9646	63		c
	jr z,$+40		;9647	28 26		( &
	inc h			;9649	24		$
	ld (02066h),hl		;964a	22 66 20	" f  
	jr c,l9685h		;964d	38 36		8 6
	inc (hl)		;964f	34		4
	ld (02e66h),a		;9650	32 66 2e	2 f .
	inc l			;9653	2c		,
	ld hl,(0626ah)		;9654	2a 6a 62	* j b
	ld h,h			;9657	64		d
	jr nc,l96c2h		;9658	30 68		0 h
	ld h,d			;965a	62		b
	ld h,h			;965b	64		d
	jr nc,l9696h		;965c	30 38		0 8
	dec d			;965e	15		.
	ld hl,01508h		;965f	21 08 15	! . .
	ld hl,01d38h		;9662	21 38 1d	! 8 .
	ld hl,02134h		;9665	21 34 21	! 4 !
	ld hl,02544h		;9668	21 44 25	! D %
	ld hl,02944h		;966b	21 44 29	! D )
	ld hl,07241h		;966e	21 41 72	! A r
	ld c,042h		;9671	0e 42		. B
	add hl,hl		;9673	29		)
	nop			;9674	00		.
	ld b,e			;9675	43		C
	defb 0edh ;next byte illegal after ed	;9676	ed		.
	dec c			;9677	0d		.
	ld b,h			;9678	44		D
	inc sp			;9679	33		3
	ld b,045h		;967a	06 45		. E
	xor h			;967c	ac		.
	ld b,046h		;967d	06 46		. F
	call nc,04708h		;967f	d4 08 47	. . G
	rst 18h			;9682	df		.
	dec bc			;9683	0b		.
	ld c,b			;9684	48		H
l9685h:
	add hl,sp		;9685	39		9
	ld c,049h		;9686	0e 49		. I
	add hl,bc		;9688	09		.
	ld b,04ch		;9689	06 4c		. L
	ld b,l			;968b	45		E
	ex af,af'		;968c	08		.
	ld c,e			;968d	4b		K
	or h			;968e	b4		.
	ex af,af'		;968f	08		.
	ld c,l			;9690	4d		M
	adc a,c			;9691	89		.
	inc c			;9692	0c		.
	ld c,(hl)		;9693	4e		N
	ld d,a			;9694	57		W
	inc c			;9695	0c		.
l9696h:
	ld c,a			;9696	4f		O
	ld a,(05009h)		;9697	3a 09 50	: . P
	halt			;969a	76		v
	add hl,bc		;969b	09		.
	ld d,h			;969c	54		T
	ld bc,0550bh		;969d	01 0b 55	. . U
	exx			;96a0	d9		.
	dec c			;96a1	0d		.
	ld d,d			;96a2	52		R
	ld d,a			;96a3	57		W
	ld b,053h		;96a4	06 53		. S
	sbc a,(hl)		;96a6	9e		.
	ld b,056h		;96a7	06 56		. V
	ld l,01eh		;96a9	2e 1e		. .
	ld d,a			;96ab	57		W
	ld b,b			;96ac	40		@
	ex af,af'		;96ad	08		.
	ld e,b			;96ae	58		X
	ld a,b			;96af	78		x
	ld e,05ah		;96b0	1e 5a		. Z
	ld e,d			;96b2	5a		Z
	ld c,009h		;96b3	0e 09		. .
	sub d			;96b5	92		.
	rlca			;96b6	07		.
	jr nz,l971eh		;96b7	20 65		  e
	rlca			;96b9	07		.
	ex af,af'		;96ba	08		.
	cp c			;96bb	b9		.
	rlca			;96bc	07		.
	ld b,e			;96bd	43		C
	jp 00d07h		;96be	c3 07 0d	. . .
	sbc a,e			;96c1	9b		.
l96c2h:
	inc c			;96c2	0c		.
	ld b,(hl)		;96c3	46		F
	add hl,sp		;96c4	39		9
	ex af,af'		;96c5	08		.
	ld c,c			;96c6	49		I
	call 04b07h		;96c7	cd 07 4b	. . K
	add a,d			;96ca	82		.
	rlca			;96cb	07		.
	ld c,h			;96cc	4c		L
	ld a,d			;96cd	7a		z
	dec c			;96ce	0d		.
	ld d,c			;96cf	51		Q
	sbc a,l			;96d0	9d		.
	dec c			;96d1	0d		.
	ld d,e			;96d2	53		S
	ex (sp),hl		;96d3	e3		.
	rlca			;96d4	07		.
	ld d,d			;96d5	52		R
	xor b			;96d6	a8		.
	ld b,058h		;96d7	06 58		. X
	ret z			;96d9	c8		.
	rlca			;96da	07		.
	ld e,d			;96db	5a		Z
	jp nc,00d07h		;96dc	d2 07 0d	. . .
	ld b,c			;96df	41		A
	ld (hl),e		;96e0	73		s
	ld (hl),e		;96e1	73		s
	ld h,l			;96e2	65		e
	ld l,l			;96e3	6d		m
	ld h,d			;96e4	62		b
	ld l,h			;96e5	6c		l
	ld h,l			;96e6	65		e
	ld b,042h		;96e7	06 42		. B
	ld a,c			;96e9	79		y
	ld h,l			;96ea	65		e
	dec c			;96eb	0d		.
	ld b,e			;96ec	43		C
	ld l,b			;96ed	68		h
	ld h,c			;96ee	61		a
	ld l,(hl)		;96ef	6e		n
	ld h,a			;96f0	67		g
	ld h,l			;96f1	65		e
	jr nz,l9756h		;96f2	20 62		  b
	ld (hl),l		;96f4	75		u
	ld h,(hl)		;96f5	66		f
	ld h,(hl)		;96f6	66		f
	ld h,l			;96f7	65		e
	ld (hl),d		;96f8	72		r
	ld b,044h		;96f9	06 44		. D
	ld h,l			;96fb	65		e
	ld l,h			;96fc	6c		l
	ld h,l			;96fd	65		e
	ld (hl),h		;96fe	74		t
	ld h,l			;96ff	65		e
	dec c			;9700	0d		.
	ld b,l			;9701	45		E
	ld h,h			;9702	64		d
	ld l,c			;9703	69		i
	ld (hl),h		;9704	74		t
	ld b,046h		;9705	06 46		. F
	ld l,c			;9707	69		i
	ld l,(hl)		;9708	6e		n
	ld h,h			;9709	64		d
	dec c			;970a	0d		.
	ld b,a			;970b	47		G
	ld h,l			;970c	65		e
	ld (hl),h		;970d	74		t
	jr nz,l9784h		;970e	20 74		  t
	ld h,l			;9710	65		e
	ld a,b			;9711	78		x
	ld (hl),h		;9712	74		t
	ld b,048h		;9713	06 48		. H
	ld h,l			;9715	65		e
	ld l,h			;9716	6c		l
	ld (hl),b		;9717	70		p
	dec c			;9718	0d		.
	ld c,c			;9719	49		I
	ld l,(hl)		;971a	6e		n
	ld (hl),e		;971b	73		s
	ld h,l			;971c	65		e
	ld (hl),d		;971d	72		r
l971eh:
	ld (hl),h		;971e	74		t
	ld b,04bh		;971f	06 4b		. K
	ld l,a			;9721	6f		o
	ld l,(hl)		;9722	6e		n
	ld h,(hl)		;9723	66		f
	ld l,c			;9724	69		i
	ld h,a			;9725	67		g
	ld (hl),l		;9726	75		u
	ld (hl),d		;9727	72		r
	ld h,l			;9728	65		e
	jr nz,l9797h		;9729	20 6c		  l
	ld l,c			;972b	69		i
	ld (hl),e		;972c	73		s
	ld (hl),h		;972d	74		t
	dec c			;972e	0d		.
	ld c,h			;972f	4c		L
	ld l,c			;9730	69		i
	ld (hl),e		;9731	73		s
	ld (hl),h		;9732	74		t
	ld b,04dh		;9733	06 4d		. M
	ld l,a			;9735	6f		o
	halt			;9736	76		v
	ld h,l			;9737	65		e
	dec c			;9738	0d		.
	ld (hl),d		;9739	72		r
	ld h,l			;973a	65		e
	ld c,(hl)		;973b	4e		N
	ld (hl),l		;973c	75		u
	ld l,l			;973d	6d		m
	ld h,d			;973e	62		b
	ld h,l			;973f	65		e
	ld (hl),d		;9740	72		r
	ld b,04fh		;9741	06 4f		. O
	ld h,d			;9743	62		b
	ld l,d			;9744	6a		j
	ld h,l			;9745	65		e
	ld h,e			;9746	63		c
	ld (hl),h		;9747	74		t
	jr nz,l97bdh		;9748	20 73		  s
	ld h,c			;974a	61		a
	halt			;974b	76		v
	ld h,l			;974c	65		e
	dec c			;974d	0d		.
	ld d,b			;974e	50		P
	ld (hl),l		;974f	75		u
	ld (hl),h		;9750	74		t
	jr nz,l97c7h		;9751	20 74		  t
	ld h,l			;9753	65		e
	ld a,b			;9754	78		x
	ld (hl),h		;9755	74		t
l9756h:
	ld b,052h		;9756	06 52		. R
	ld (hl),l		;9758	75		u
	ld l,(hl)		;9759	6e		n
	dec c			;975a	0d		.
	ld d,e			;975b	53		S
	ld h,l			;975c	65		e
	ld (hl),b		;975d	70		p
	ld h,c			;975e	61		a
	ld (hl),d		;975f	72		r
	ld h,c			;9760	61		a
	ld (hl),h		;9761	74		t
	ld l,a			;9762	6f		o
	ld (hl),d		;9763	72		r
	ld b,054h		;9764	06 54		. T
	ld h,c			;9766	61		a
	ld (hl),b		;9767	70		p
	ld h,l			;9768	65		e
	jr nz,l97d1h		;9769	20 66		  f
	ld l,a			;976b	6f		o
	ld (hl),d		;976c	72		r
	jr nz,l97d8h		;976d	20 69		  i
	ld l,(hl)		;976f	6e		n
	ld h,e			;9770	63		c
	ld l,h			;9771	6c		l
	ld (hl),l		;9772	75		u
	ld h,h			;9773	64		d
	ld h,l			;9774	65		e
	dec c			;9775	0d		.
	ld h,e			;9776	63		c
	ld (hl),l		;9777	75		u
	ld (hl),d		;9778	72		r
	ld (hl),d		;9779	72		r
	ld h,l			;977a	65		e
	ld l,(hl)		;977b	6e		n
	ld (hl),h		;977c	74		t
	jr nz,l97d5h		;977d	20 56		  V
	ld h,c			;977f	61		a
	ld l,h			;9780	6c		l
	ld (hl),l		;9781	75		u
	ld h,l			;9782	65		e
	ld (hl),e		;9783	73		s
l9784h:
	ld b,075h		;9784	06 75		. u
	ld (hl),e		;9786	73		s
	ld h,l			;9787	65		e
	jr nz,l97dch		;9788	20 52		  R
	ld b,c			;978a	41		A
	ld c,l			;978b	4d		M
	jr nz,l97e3h		;978c	20 55		  U
	ld l,(hl)		;978e	6e		n
	ld (hl),h		;978f	74		t
	ld l,c			;9790	69		i
	ld l,h			;9791	6c		l
	dec c			;9792	0d		.
	ld d,a			;9793	57		W
	ld (hl),d		;9794	72		r
	ld l,c			;9795	69		i
	ld (hl),h		;9796	74		t
l9797h:
	ld h,l			;9797	65		e
	jr nz,l980eh		;9798	20 74		  t
	ld h,l			;979a	65		e
	ld a,b			;979b	78		x
	ld (hl),h		;979c	74		t
	ld b,058h		;979d	06 58		. X
	jr nz,l9804h		;979f	20 63		  c
	ld h,c			;97a1	61		a
	ld (hl),h		;97a2	74		t
	ld h,c			;97a3	61		a
	ld l,h			;97a4	6c		l
	ld l,a			;97a5	6f		o
	ld h,a			;97a6	67		g
	ld (hl),l		;97a7	75		u
	ld h,l			;97a8	65		e
	dec c			;97a9	0d		.
	ld e,d			;97aa	5a		Z
	ld h,c			;97ab	61		a
	ld (hl),b		;97ac	70		p
	jr nz,l9823h		;97ad	20 74		  t
	ld h,l			;97af	65		e
	ld a,b			;97b0	78		x
	ld (hl),h		;97b1	74		t
	dec c			;97b2	0d		.
	nop			;97b3	00		.
	ld d,b			;97b4	50		P
	ld h,c			;97b5	61		a
	ld (hl),d		;97b6	72		r
	ld h,h			;97b7	64		d
	ld l,a			;97b8	6f		o
	ld l,(hl)		;97b9	6e		n
	ccf			;97ba	3f		?
	dec c			;97bb	0d		.
	nop			;97bc	00		.
l97bdh:
	dec c			;97bd	0d		.
	ld b,h			;97be	44		D
	ld h,l			;97bf	65		e
	ld l,h			;97c0	6c		l
	ld h,l			;97c1	65		e
	ld (hl),h		;97c2	74		t
	ld h,l			;97c3	65		e
	jr nz,l983ah		;97c4	20 74		  t
	ld h,l			;97c6	65		e
l97c7h:
	ld a,b			;97c7	78		x
	ld (hl),h		;97c8	74		t
	jr z,l9824h		;97c9	28 59		( Y
	cpl			;97cb	2f		/
	ld c,(hl)		;97cc	4e		N
	add hl,hl		;97cd	29		)
	nop			;97ce	00		.
	dec c			;97cf	0d		.
	nop			;97d0	00		.
l97d1h:
	nop			;97d1	00		.
	nop			;97d2	00		.
	nop			;97d3	00		.
	nop			;97d4	00		.
l97d5h:
	jr nz,l97d7h		;97d5	20 00		  .
l97d7h:
	nop			;97d7	00		.
l97d8h:
	nop			;97d8	00		.
	nop			;97d9	00		.
	nop			;97da	00		.
	dec c			;97db	0d		.
l97dch:
	nop			;97dc	00		.
	nop			;97dd	00		.
	nop			;97de	00		.
	nop			;97df	00		.
	nop			;97e0	00		.
	nop			;97e1	00		.
	nop			;97e2	00		.
l97e3h:
	nop			;97e3	00		.
	nop			;97e4	00		.
	nop			;97e5	00		.
	nop			;97e6	00		.
	nop			;97e7	00		.
	nop			;97e8	00		.
	nop			;97e9	00		.
	nop			;97ea	00		.
	nop			;97eb	00		.
	nop			;97ec	00		.
	nop			;97ed	00		.
	nop			;97ee	00		.
	nop			;97ef	00		.
	nop			;97f0	00		.
	nop			;97f1	00		.
	nop			;97f2	00		.
	nop			;97f3	00		.
	nop			;97f4	00		.
	nop			;97f5	00		.
	nop			;97f6	00		.
	nop			;97f7	00		.
	nop			;97f8	00		.
	nop			;97f9	00		.
	nop			;97fa	00		.
	nop			;97fb	00		.
	nop			;97fc	00		.
	nop			;97fd	00		.
	nop			;97fe	00		.
	nop			;97ff	00		.
	nop			;9800	00		.
	nop			;9801	00		.
	nop			;9802	00		.
	nop			;9803	00		.
l9804h:
	nop			;9804	00		.
	nop			;9805	00		.
	nop			;9806	00		.
	nop			;9807	00		.
	nop			;9808	00		.
	nop			;9809	00		.
	nop			;980a	00		.
	nop			;980b	00		.
	nop			;980c	00		.
	nop			;980d	00		.
l980eh:
	nop			;980e	00		.
	nop			;980f	00		.
l9810h:
	nop			;9810	00		.
	nop			;9811	00		.
	nop			;9812	00		.
	nop			;9813	00		.
	nop			;9814	00		.
	nop			;9815	00		.
	nop			;9816	00		.
	nop			;9817	00		.
	nop			;9818	00		.
	nop			;9819	00		.
	nop			;981a	00		.
	nop			;981b	00		.
	nop			;981c	00		.
	nop			;981d	00		.
	nop			;981e	00		.
	nop			;981f	00		.
	nop			;9820	00		.
	nop			;9821	00		.
	nop			;9822	00		.
l9823h:
	nop			;9823	00		.
l9824h:
	nop			;9824	00		.
	nop			;9825	00		.
	nop			;9826	00		.
	nop			;9827	00		.
	nop			;9828	00		.
	nop			;9829	00		.
	nop			;982a	00		.
	nop			;982b	00		.
	nop			;982c	00		.
	nop			;982d	00		.
	nop			;982e	00		.
	nop			;982f	00		.
	nop			;9830	00		.
	nop			;9831	00		.
	nop			;9832	00		.
	nop			;9833	00		.
	nop			;9834	00		.
	nop			;9835	00		.
	nop			;9836	00		.
	nop			;9837	00		.
	nop			;9838	00		.
	nop			;9839	00		.
l983ah:
	nop			;983a	00		.
	nop			;983b	00		.
	nop			;983c	00		.
	nop			;983d	00		.
	nop			;983e	00		.
	nop			;983f	00		.
	nop			;9840	00		.
	nop			;9841	00		.
	nop			;9842	00		.
	nop			;9843	00		.
	nop			;9844	00		.
	nop			;9845	00		.
	nop			;9846	00		.
	nop			;9847	00		.
	nop			;9848	00		.
	nop			;9849	00		.
	nop			;984a	00		.
	nop			;984b	00		.
	nop			;984c	00		.
	nop			;984d	00		.
	nop			;984e	00		.
	nop			;984f	00		.
	nop			;9850	00		.
	nop			;9851	00		.
	nop			;9852	00		.
	nop			;9853	00		.
	nop			;9854	00		.
	nop			;9855	00		.
	nop			;9856	00		.
	nop			;9857	00		.
	nop			;9858	00		.
	nop			;9859	00		.
	nop			;985a	00		.
	nop			;985b	00		.
	nop			;985c	00		.
	nop			;985d	00		.
	nop			;985e	00		.
	nop			;985f	00		.
	nop			;9860	00		.
	nop			;9861	00		.
	nop			;9862	00		.
	nop			;9863	00		.
	nop			;9864	00		.
	nop			;9865	00		.
	nop			;9866	00		.
	nop			;9867	00		.
	nop			;9868	00		.
	nop			;9869	00		.
	nop			;986a	00		.
	nop			;986b	00		.
	nop			;986c	00		.
	nop			;986d	00		.
	nop			;986e	00		.
	nop			;986f	00		.
	nop			;9870	00		.
	nop			;9871	00		.
	nop			;9872	00		.
	nop			;9873	00		.
	nop			;9874	00		.
	nop			;9875	00		.
	nop			;9876	00		.
	nop			;9877	00		.
	nop			;9878	00		.
	nop			;9879	00		.
	nop			;987a	00		.
	nop			;987b	00		.
	nop			;987c	00		.
	nop			;987d	00		.
	nop			;987e	00		.
	nop			;987f	00		.
	nop			;9880	00		.
	nop			;9881	00		.
	nop			;9882	00		.
	nop			;9883	00		.
	nop			;9884	00		.
	nop			;9885	00		.
	nop			;9886	00		.
	nop			;9887	00		.
	nop			;9888	00		.
	nop			;9889	00		.
	nop			;988a	00		.
	nop			;988b	00		.
	nop			;988c	00		.
	nop			;988d	00		.
	nop			;988e	00		.
	nop			;988f	00		.
	nop			;9890	00		.
	nop			;9891	00		.
	nop			;9892	00		.
	dec c			;9893	0d		.
	ld c,c			;9894	49		I
	ld l,(hl)		;9895	6e		n
	ld h,e			;9896	63		c
	ld l,h			;9897	6c		l
	ld (hl),l		;9898	75		u
	ld h,h			;9899	64		d
	ld h,l			;989a	65		e
	jr nz,l9903h		;989b	20 66		  f
	ld l,c			;989d	69		i
	ld l,h			;989e	6c		l
	ld h,l			;989f	65		e
	jr nz,l990bh		;98a0	20 69		  i
	ld (hl),e		;98a2	73		s
	jr nz,$+103		;98a3	20 65		  e
	ld l,l			;98a5	6d		m
	ld (hl),b		;98a6	70		p
	ld (hl),h		;98a7	74		t
	ld a,c			;98a8	79		y
	ld hl,0000dh		;98a9	21 0d 00	! . .
	dec c			;98ac	0d		.
	ld d,e			;98ad	53		S
	ld (hl),h		;98ae	74		t
	ld h,c			;98af	61		a
	ld (hl),d		;98b0	72		r
	ld (hl),h		;98b1	74		t
	jr nz,$+118		;98b2	20 74		  t
	ld h,c			;98b4	61		a
	ld (hl),b		;98b5	70		p
	ld h,l			;98b6	65		e
	ld l,02eh		;98b7	2e 2e		. .
	dec c			;98b9	0d		.
	nop			;98ba	00		.
	ld c,(hl)		;98bb	4e		N
	ld l,a			;98bc	6f		o
	jr nz,l990ch		;98bd	20 4d		  M
	ld h,c			;98bf	61		a
	ld h,e			;98c0	63		c
	ld (hl),d		;98c1	72		r
	ld l,a			;98c2	6f		o
	jr nz,l9918h		;98c3	20 53		  S
	ld (hl),b		;98c5	70		p
	ld h,c			;98c6	61		a
	ld h,e			;98c7	63		c
	ld h,l			;98c8	65		e
	nop			;98c9	00		.
	ld b,d			;98ca	42		B
	ld h,c			;98cb	61		a
	ld h,h			;98cc	64		d
	jr nz,l991eh		;98cd	20 4f		  O
	ld d,d			;98cf	52		R
	ld b,a			;98d0	47		G
	ld hl,00d00h		;98d1	21 00 0d	! . .
	ld b,h			;98d4	44		D
	ld h,l			;98d5	65		e
	ld l,h			;98d6	6c		l
	ld l,c			;98d7	69		i
	ld l,l			;98d8	6d		m
	ld l,c			;98d9	69		i
	ld (hl),h		;98da	74		t
	ld h,l			;98db	65		e
	ld (hl),d		;98dc	72		r
	ld a,(00006h)		;98dd	3a 06 00	: . .
	dec c			;98e0	0d		.
	ld sp,07473h		;98e1	31 73 74	1 s t
	jr nz,l9956h		;98e4	20 70		  p
	ld h,c			;98e6	61		a
	ld (hl),d		;98e7	72		r
	ld h,c			;98e8	61		a
	ld l,l			;98e9	6d		m
	ld h,l			;98ea	65		e
	ld (hl),h		;98eb	74		t
	ld h,l			;98ec	65		e
	ld (hl),d		;98ed	72		r
	ld a,(00006h)		;98ee	3a 06 00	: . .
	dec c			;98f1	0d		.
	ld (0646eh),a		;98f2	32 6e 64	2 n d
	jr nz,l9967h		;98f5	20 70		  p
	ld h,c			;98f7	61		a
	ld (hl),d		;98f8	72		r
	ld h,c			;98f9	61		a
	ld l,l			;98fa	6d		m
	ld h,l			;98fb	65		e
	ld (hl),h		;98fc	74		t
	ld h,l			;98fd	65		e
	ld (hl),d		;98fe	72		r
	ld a,(00006h)		;98ff	3a 06 00	: . .
	dec c			;9902	0d		.
l9903h:
	ld d,h			;9903	54		T
	ld l,a			;9904	6f		o
	ld (hl),b		;9905	70		p
	jr nz,l9977h		;9906	20 6f		  o
	ld h,(hl)		;9908	66		f
	jr nz,l9978h		;9909	20 6d		  m
l990bh:
	ld h,l			;990b	65		e
l990ch:
	ld l,l			;990c	6d		m
	ld l,a			;990d	6f		o
	ld (hl),d		;990e	72		r
	ld a,c			;990f	79		y
	ld a,(00006h)		;9910	3a 06 00	: . .
	dec c			;9913	0d		.
	ld d,h			;9914	54		T
	ld h,l			;9915	65		e
	ld a,b			;9916	78		x
	ld (hl),h		;9917	74		t
l9918h:
	jr nz,l998dh		;9918	20 73		  s
	ld (hl),h		;991a	74		t
	ld h,c			;991b	61		a
	ld (hl),d		;991c	72		r
	ld (hl),h		;991d	74		t
l991eh:
	ld (hl),e		;991e	73		s
	ld a,(00006h)		;991f	3a 06 00	: . .
	dec c			;9922	0d		.
	ld d,h			;9923	54		T
	ld h,l			;9924	65		e
	ld a,b			;9925	78		x
	ld (hl),h		;9926	74		t
	jr nz,l998eh		;9927	20 65		  e
	ld l,(hl)		;9929	6e		n
	ld h,h			;992a	64		d
	ld (hl),e		;992b	73		s
	ld a,(00006h)		;992c	3a 06 00	: . .
	dec c			;992f	0d		.
	ld c,(hl)		;9930	4e		N
	ld h,c			;9931	61		a
	ld l,l			;9932	6d		m
	ld h,l			;9933	65		e
	ld a,(00006h)		;9934	3a 06 00	: . .
	dec c			;9937	0d		.
	ld d,h			;9938	54		T
	ld h,c			;9939	61		a
	ld (hl),b		;993a	70		p
	ld h,l			;993b	65		e
	jr nz,l99a3h		;993c	20 65		  e
	ld (hl),d		;993e	72		r
	ld (hl),d		;993f	72		r
	ld l,a			;9940	6f		o
	ld (hl),d		;9941	72		r
	ld hl,0000dh		;9942	21 0d 00	! . .
	dec c			;9945	0d		.
	ld d,l			;9946	55		U
	ld (hl),e		;9947	73		s
	ld l,c			;9948	69		i
	ld l,(hl)		;9949	6e		n
	ld h,a			;994a	67		g
	jr nz,l994dh		;994b	20 00		  .
l994dh:
	dec c			;994d	0d		.
	ld c,a			;994e	4f		O
	ld (hl),l		;994f	75		u
	ld (hl),h		;9950	74		t
	jr nz,l99c2h		;9951	20 6f		  o
	ld h,(hl)		;9953	66		f
	jr nz,$+111		;9954	20 6d		  m
l9956h:
	ld h,l			;9956	65		e
	ld l,l			;9957	6d		m
	ld l,a			;9958	6f		o
	ld (hl),d		;9959	72		r
	ld a,c			;995a	79		y
	ld hl,00d00h		;995b	21 00 0d	! . .
	ld b,(hl)		;995e	46		F
	ld l,a			;995f	6f		o
	ld (hl),l		;9960	75		u
	ld l,(hl)		;9961	6e		n
	ld h,h			;9962	64		d
	jr nz,l9965h		;9963	20 00		  .
l9965h:
	ld d,d			;9965	52		R
	ld h,l			;9966	65		e
l9967h:
	ld h,e			;9967	63		c
	ld l,a			;9968	6f		o
	ld (hl),d		;9969	72		r
	ld h,h			;996a	64		d
	jr nz,l999dh		;996b	20 30		  0
	jr nc,l997ch		;996d	30 0d		0 .
	nop			;996f	00		.
	dec c			;9970	0d		.
	ld b,(hl)		;9971	46		F
	ld l,c			;9972	69		i
	ld l,h			;9973	6c		l
	ld h,l			;9974	65		e
	jr nz,l99dch		;9975	20 65		  e
l9977h:
	ld a,b			;9977	78		x
l9978h:
	ld l,c			;9978	69		i
	ld (hl),e		;9979	73		s
	ld (hl),h		;997a	74		t
	ld (hl),e		;997b	73		s
l997ch:
	jr nz,l99c2h		;997c	20 44		  D
	ld h,l			;997e	65		e
	ld l,h			;997f	6c		l
	ld h,l			;9980	65		e
	ld (hl),h		;9981	74		t
	ld h,l			;9982	65		e
	jr z,l99deh		;9983	28 59		( Y
	cpl			;9985	2f		/
	ld c,(hl)		;9986	4e		N
	add hl,hl		;9987	29		)
	ccf			;9988	3f		?
	nop			;9989	00		.
	dec c			;998a	0d		.
	ld b,(hl)		;998b	46		F
	ld l,c			;998c	69		i
l998dh:
	ld l,h			;998d	6c		l
l998eh:
	ld h,l			;998e	65		e
	jr nz,l99d2h		;998f	20 41		  A
	ld h,d			;9991	62		b
	ld (hl),e		;9992	73		s
	ld h,l			;9993	65		e
	ld l,(hl)		;9994	6e		n
	ld (hl),h		;9995	74		t
	nop			;9996	00		.
	dec c			;9997	0d		.
	ld b,d			;9998	42		B
	ld h,c			;9999	61		a
	ld h,h			;999a	64		d
	jr nz,l99ebh		;999b	20 4e		  N
l999dh:
	ld h,c			;999d	61		a
	ld l,l			;999e	6d		m
	ld h,l			;999f	65		e
	nop			;99a0	00		.
	dec c			;99a1	0d		.
	ld d,b			;99a2	50		P
l99a3h:
	ld (hl),d		;99a3	72		r
	ld l,a			;99a4	6f		o
	ld (hl),h		;99a5	74		t
	ld h,l			;99a6	65		e
	ld h,e			;99a7	63		c
	ld (hl),h		;99a8	74		t
	ld h,l			;99a9	65		e
	ld h,h			;99aa	64		d
	nop			;99ab	00		.
	ld c,c			;99ac	49		I
	ld l,(hl)		;99ad	6e		n
	ld h,e			;99ae	63		c
	ld l,h			;99af	6c		l
	ld (hl),l		;99b0	75		u
	ld h,h			;99b1	64		d
	ld h,l			;99b2	65		e
	jr nz,l9a17h		;99b3	20 62		  b
	ld (hl),l		;99b5	75		u
	ld h,(hl)		;99b6	66		f
	ld h,(hl)		;99b7	66		f
	ld h,l			;99b8	65		e
	ld (hl),d		;99b9	72		r
	ccf			;99ba	3f		?
	jr nz,l99bdh		;99bb	20 00		  .
l99bdh:
	ld c,l			;99bd	4d		M
	ld h,c			;99be	61		a
	ld h,e			;99bf	63		c
	ld (hl),d		;99c0	72		r
	ld l,a			;99c1	6f		o
l99c2h:
	jr nz,l9a26h		;99c2	20 62		  b
	ld (hl),l		;99c4	75		u
	ld h,(hl)		;99c5	66		f
	ld h,(hl)		;99c6	66		f
	ld h,l			;99c7	65		e
	ld (hl),d		;99c8	72		r
	ccf			;99c9	3f		?
	jr nz,l99cch		;99ca	20 00		  .
l99cch:
	ld hl,(05245h)		;99cc	2a 45 52	* E R
	ld d,d			;99cf	52		R
	ld c,a			;99d0	4f		O
	ld d,d			;99d1	52		R
l99d2h:
	ld hl,(00020h)		;99d2	2a 20 00	*   .
	nop			;99d5	00		.
	nop			;99d6	00		.
	ld hl,(04157h)		;99d7	2a 57 41	* W A
	ld d,d			;99da	52		R
	ld c,(hl)		;99db	4e		N
l99dch:
	ld c,c			;99dc	49		I
	ld c,(hl)		;99dd	4e		N
l99deh:
	ld b,a			;99de	47		G
	ld hl,(00020h)		;99df	2a 20 00	*   .
	ld d,b			;99e2	50		P
	ld h,c			;99e3	61		a
	ld (hl),e		;99e4	73		s
	ld (hl),e		;99e5	73		s
	jr nz,l9a19h		;99e6	20 31		  1
	nop			;99e8	00		.
	dec c			;99e9	0d		.
	ld d,b			;99ea	50		P
l99ebh:
	ld h,c			;99eb	61		a
	ld (hl),e		;99ec	73		s
	ld (hl),e		;99ed	73		s
	jr nz,l9a22h		;99ee	20 32		  2
	jr nz,l9a57h		;99f0	20 65		  e
	ld (hl),d		;99f2	72		r
	ld (hl),d		;99f3	72		r
	ld l,a			;99f4	6f		o
	ld (hl),d		;99f5	72		r
	ld (hl),e		;99f6	73		s
	ld a,(00020h)		;99f7	3a 20 00	:   .
	nop			;99fa	00		.
	nop			;99fb	00		.
	ld h,c			;99fc	61		a
	ld h,d			;99fd	62		b
	ld (hl),e		;99fe	73		s
	ld h,l			;99ff	65		e
	ld l,(hl)		;9a00	6e		n
	ld (hl),h		;9a01	74		t
	nop			;9a02	00		.
	nop			;9a03	00		.
	nop			;9a04	00		.
	nop			;9a05	00		.
	nop			;9a06	00		.
	nop			;9a07	00		.
	ld a,a			;9a08	7f		.
	ld a,a			;9a09	7f		.
	ld a,a			;9a0a	7f		.
	ld a,a			;9a0b	7f		.
	ld a,a			;9a0c	7f		.
	rst 38h			;9a0d	ff		.
	nop			;9a0e	00		.
	nop			;9a0f	00		.
	ld d,h			;9a10	54		T
	ld h,c			;9a11	61		a
	ld h,d			;9a12	62		b
	ld l,h			;9a13	6c		l
	ld h,l			;9a14	65		e
	jr nz,l9a8ch		;9a15	20 75		  u
l9a17h:
	ld (hl),e		;9a17	73		s
	ld h,l			;9a18	65		e
l9a19h:
	ld h,h			;9a19	64		d
	ld a,(00020h)		;9a1a	3a 20 00	:   .
	jr nz,l9a85h		;9a1d	20 66		  f
	ld (hl),d		;9a1f	72		r
	ld l,a			;9a20	6f		o
	ld l,l			;9a21	6d		m
l9a22h:
	jr nz,l9a24h		;9a22	20 00		  .
l9a24h:
	dec c			;9a24	0d		.
	ld b,l			;9a25	45		E
l9a26h:
	ld a,b			;9a26	78		x
	ld h,l			;9a27	65		e
	ld h,e			;9a28	63		c
	ld (hl),l		;9a29	75		u
	ld (hl),h		;9a2a	74		t
	ld h,l			;9a2b	65		e
	ld (hl),e		;9a2c	73		s
	ld a,(00020h)		;9a2d	3a 20 00	:   .
	ld c,(hl)		;9a30	4e		N
	ld l,a			;9a31	6f		o
	jr nz,l9a87h		;9a32	20 53		  S
	ld a,c			;9a34	79		y
	ld l,l			;9a35	6d		m
	ld h,d			;9a36	62		b
	ld l,a			;9a37	6f		o
	ld l,h			;9a38	6c		l
	jr nz,$+86		;9a39	20 54		  T
	ld h,c			;9a3b	61		a
	ld h,d			;9a3c	62		b
	ld l,h			;9a3d	6c		l
	ld h,l			;9a3e	65		e
	jr nz,l9ab4h		;9a3f	20 73		  s
	ld (hl),b		;9a41	70		p
	ld h,c			;9a42	61		a
	ld h,e			;9a43	63		c
	ld h,l			;9a44	65		e
	ld hl,00d00h		;9a45	21 00 0d	! . .
	ld c,b			;9a48	48		H
	ld l,c			;9a49	69		i
	ld d,e			;9a4a	53		S
	ld l,a			;9a4b	6f		o
	ld h,(hl)		;9a4c	66		f
	ld (hl),h		;9a4d	74		t
	jr nz,l9a97h		;9a4e	20 47		  G
	ld b,l			;9a50	45		E
	ld c,(hl)		;9a51	4e		N
	jr nz,l9a95h		;9a52	20 41		  A
	ld (hl),e		;9a54	73		s
	ld (hl),e		;9a55	73		s
	ld h,l			;9a56	65		e
l9a57h:
	ld l,l			;9a57	6d		m
	ld h,d			;9a58	62		b
	ld l,h			;9a59	6c		l
	ld h,l			;9a5a	65		e
	ld (hl),d		;9a5b	72		r
	jr nz,l9ab8h		;9a5c	20 5a		  Z
	ld e,b			;9a5e	58		X
	jr nz,l9ab4h		;9a5f	20 53		  S
	ld (hl),b		;9a61	70		p
	ld h,l			;9a62	65		e
	ld h,e			;9a63	63		c
	ld (hl),h		;9a64	74		t
	ld (hl),d		;9a65	72		r
	ld (hl),l		;9a66	75		u
	ld l,l			;9a67	6d		m
	dec c			;9a68	0d		.
	ld a,a			;9a69	7f		.
	jr nz,$+51		;9a6a	20 31		  1
	add hl,sp		;9a6c	39		9
	add hl,sp		;9a6d	39		9
	jr nc,l9a90h		;9a6e	30 20		0  
	ld c,l			;9a70	4d		M
	ld c,a			;9a71	4f		O
	ld b,c			;9a72	41		A
	jr nz,l9ab7h		;9a73	20 42		  B
	dec l			;9a75	2d		-
	ld b,h			;9a76	44		D
	ld l,c			;9a77	69		i
	ld (hl),e		;9a78	73		s
	ld l,e			;9a79	6b		k
	jr nz,l9af2h		;9a7a	20 76		  v
	ld h,l			;9a7c	65		e
	ld (hl),d		;9a7d	72		r
	ld (hl),e		;9a7e	73		s
	ld l,c			;9a7f	69		i
	ld l,a			;9a80	6f		o
	ld l,(hl)		;9a81	6e		n
	ld l,00dh		;9a82	2e 0d		. .
	dec c			;9a84	0d		.
l9a85h:
	ld b,e			;9a85	43		C
	ld l,a			;9a86	6f		o
l9a87h:
	ld (hl),b		;9a87	70		p
	ld a,c			;9a88	79		y
	ld (hl),d		;9a89	72		r
	ld l,c			;9a8a	69		i
	ld h,a			;9a8b	67		g
l9a8ch:
	ld l,b			;9a8c	68		h
	ld (hl),h		;9a8d	74		t
	jr nz,l9ab8h		;9a8e	20 28		  (
l9a90h:
	ld b,e			;9a90	43		C
	add hl,hl		;9a91	29		)
	jr nz,l9adch		;9a92	20 48		  H
	ld l,c			;9a94	69		i
l9a95h:
	ld d,e			;9a95	53		S
	ld l,a			;9a96	6f		o
l9a97h:
	ld h,(hl)		;9a97	66		f
	ld (hl),h		;9a98	74		t
	jr nz,$+51		;9a99	20 31		  1
	add hl,sp		;9a9b	39		9
	jr c,l9ad5h		;9a9c	38 37		8 7
	dec c			;9a9e	0d		.
	ld d,(hl)		;9a9f	56		V
	inc (hl)		;9aa0	34		4
	ld l,031h		;9aa1	2e 31		. 1
	jr nz,$+34		;9aa3	20 20		   
	ld b,c			;9aa5	41		A
	ld l,h			;9aa6	6c		l
	ld l,h			;9aa7	6c		l
	jr nz,l9b1ch		;9aa8	20 72		  r
	ld l,c			;9aaa	69		i
	ld h,a			;9aab	67		g
	ld l,b			;9aac	68		h
	ld (hl),h		;9aad	74		t
	ld (hl),e		;9aae	73		s
	jr nz,l9b23h		;9aaf	20 72		  r
	ld h,l			;9ab1	65		e
	ld (hl),e		;9ab2	73		s
	ld h,l			;9ab3	65		e
l9ab4h:
	ld (hl),d		;9ab4	72		r
	halt			;9ab5	76		v
	ld h,l			;9ab6	65		e
l9ab7h:
	ld h,h			;9ab7	64		d
l9ab8h:
	dec c			;9ab8	0d		.
	nop			;9ab9	00		.
	nop			;9aba	00		.
	ld e,042h		;9abb	1e 42		. B
	dec c			;9abd	0d		.
	nop			;9abe	00		.
	inc h			;9abf	24		$
	nop			;9ac0	00		.
	daa			;9ac1	27		'
	nop			;9ac2	00		.
	ld hl,(02d00h)		;9ac3	2a 00 2d	* . -
	nop			;9ac6	00		.
	jr nc,l9ac9h		;9ac7	30 00		0 .
l9ac9h:
	add hl,sp		;9ac9	39		9
	nop			;9aca	00		.
	ld a,000h		;9acb	3e 00		> .
	ld b,l			;9acd	45		E
	nop			;9ace	00		.
	ld c,b			;9acf	48		H
	nop			;9ad0	00		.
	ld c,e			;9ad1	4b		K
	nop			;9ad2	00		.
	ld c,a			;9ad3	4f		O
	nop			;9ad4	00		.
l9ad5h:
	ld h,e			;9ad5	63		c
	nop			;9ad6	00		.
	ld (hl),d		;9ad7	72		r
	nop			;9ad8	00		.
	ld a,b			;9ad9	78		x
	nop			;9ada	00		.
	push af			;9adb	f5		.
l9adch:
	nop			;9adc	00		.
	jp m,00500h		;9add	fa 00 05	. . .
	ld bc,0011ah		;9ae0	01 1a 01	. . .
	ld hl,05301h		;9ae3	21 01 53	! . S
	ld bc,00182h		;9ae6	01 82 01	. . .
	adc a,e			;9ae9	8b		.
	ld bc,00192h		;9aea	01 92 01	. . .
	sbc a,d			;9aed	9a		.
	ld bc,001a5h		;9aee	01 a5 01	. . .
	ret nz			;9af1	c0		.
l9af2h:
	ld bc,001c7h		;9af2	01 c7 01	. . .
	out (001h),a		;9af5	d3 01		. .
	or 001h			;9af7	f6 01		. .
	ld de,03802h		;9af9	11 02 38	. . 8
	ld (bc),a		;9afc	02		.
	dec a			;9afd	3d		=
	ld (bc),a		;9afe	02		.
	ld b,l			;9aff	45		E
	ld (bc),a		;9b00	02		.
	ld c,d			;9b01	4a		J
	ld (bc),a		;9b02	02		.
	ld d,d			;9b03	52		R
	ld (bc),a		;9b04	02		.
	or c			;9b05	b1		.
	ld (bc),a		;9b06	02		.
	or a			;9b07	b7		.
	ld (bc),a		;9b08	02		.
	ret z			;9b09	c8		.
	ld (bc),a		;9b0a	02		.
	ld a,(de)		;9b0b	1a		.
	inc bc			;9b0c	03		.
	dec e			;9b0d	1d		.
	inc bc			;9b0e	03		.
	ld (04503h),a		;9b0f	32 03 45	2 . E
	inc bc			;9b12	03		.
	ld e,l			;9b13	5d		]
	inc bc			;9b14	03		.
	ld h,d			;9b15	62		b
	inc bc			;9b16	03		.
	ld a,h			;9b17	7c		|
	inc bc			;9b18	03		.
	adc a,d			;9b19	8a		.
	inc bc			;9b1a	03		.
	sbc a,h			;9b1b	9c		.
l9b1ch:
	inc bc			;9b1c	03		.
	and h			;9b1d	a4		.
	inc bc			;9b1e	03		.
	xor c			;9b1f	a9		.
	inc bc			;9b20	03		.
	or c			;9b21	b1		.
	inc bc			;9b22	03		.
l9b23h:
	cp (hl)			;9b23	be		.
	inc bc			;9b24	03		.
	jp 0c603h		;9b25	c3 03 c6	. . .
	inc bc			;9b28	03		.
	ret			;9b29	c9		.
	inc bc			;9b2a	03		.
	ret nc			;9b2b	d0		.
	inc bc			;9b2c	03		.
	call c,0e103h		;9b2d	dc 03 e1	. . .
	inc bc			;9b30	03		.
	and 003h		;9b31	e6 03		. .
	call pe,0ef03h		;9b33	ec 03 ef	. . .
	inc bc			;9b36	03		.
	push af			;9b37	f5		.
	inc bc			;9b38	03		.
	ret m			;9b39	f8		.
	inc bc			;9b3a	03		.
	ei			;9b3b	fb		.
	inc bc			;9b3c	03		.
	cp 003h			;9b3d	fe 03		. .
	ld bc,00404h		;9b3f	01 04 04	. . .
	inc b			;9b42	04		.
	rlca			;9b43	07		.
	inc b			;9b44	04		.
	ld a,(bc)		;9b45	0a		.
	inc b			;9b46	04		.
	dec c			;9b47	0d		.
	inc b			;9b48	04		.
	djnz l9b4fh		;9b49	10 04		. .
	inc de			;9b4b	13		.
	inc b			;9b4c	04		.
	ld d,004h		;9b4d	16 04		. .
l9b4fh:
	add hl,de		;9b4f	19		.
	ld bc,l7550h		;9b50	01 50 75	. P u
	rlca			;9b53	07		.
	ld (hl),b		;9b54	70		p
	ld bc,05001h		;9b55	01 01 50	. . P
	ret po			;9b58	e0		.
	ld bc,l7705h		;9b59	01 05 77	. . w
	cp 015h			;9b5c	fe 15		. .
	xor b			;9b5e	a8		.
	ld (hl),l		;9b5f	75		u
	and l			;9b60	a5		.
	ld (hl),a		;9b61	77		w
	cp l			;9b62	bd		.
	sbc a,d			;9b63	9a		.
	jr l9b6eh		;9b64	18 08		. .
	cp 015h			;9b66	fe 15		. .
	xor b			;9b68	a8		.
	ld (hl),l		;9b69	75		u
	and l			;9b6a	a5		.
	ld (hl),a		;9b6b	77		w
	cp (hl)			;9b6c	be		.
	sbc a,d			;9b6d	9a		.
l9b6eh:
	ld e,h			;9b6e	5c		\
	dec c			;9b6f	0d		.
	ld bc,03f01h		;9b70	01 01 3f	. . ?
	ld sp,l9810h		;9b73	31 10 98	1 . .
	ld e,07ah		;9b76	1e 7a		. z
	daa			;9b78	27		'
	ld d,05fh		;9b79	16 5f		. _
	ld a,b			;9b7b	78		x
	ld b,l			;9b7c	45		E
	ld a,c			;9b7d	79		y
	dec b			;9b7e	05		.
	ld h,005h		;9b7f	26 05		& .
	dec hl			;9b81	2b		+
	jp 02669h		;9b82	c3 69 26	. i &
	jp 0269dh		;9b85	c3 9d 26	. . &
	jp 026f0h		;9b88	c3 f0 26	. . &
	jp 02700h		;9b8b	c3 00 27	. . '
	jp 027f4h		;9b8e	c3 f4 27	. . '
	sbc a,h			;9b91	9c		.
	daa			;9b92	27		'
	jp 0288eh		;9b93	c3 8e 28	. . (
	jp 02893h		;9b96	c3 93 28	. . (
	ld a,c			;9b99	79		y
	ld c,008h		;9b9a	0e 08		. .
	call 03d13h		;9b9c	cd 13 3d	. . =
	ld hl,(05c4fh)		;9b9f	2a 4f 5c	* O \
	dec hl			;9ba2	2b		+
	ld bc,00122h		;9ba3	01 22 01	. " .
	call 01655h		;9ba6	cd 55 16	. U .
	inc hl			;9ba9	23		#
	inc hl			;9baa	23		#
	push hl			;9bab	e5		.
	pop ix			;9bac	dd e1		. .
	ld c,014h		;9bae	0e 14		. .
	inc hl			;9bb0	23		#
	inc hl			;9bb1	23		#
	call 03d13h		;9bb2	cd 13 3d	. . =
	ld a,(05cf6h)		;9bb5	3a f6 5c	: . \
	ld (ix+012h),a		;9bb8	dd 77 12	. w .
	ld a,(05d16h)		;9bbb	3a 16 5d	: . ]
	ld (ix+013h),a		;9bbe	dd 77 13	. w .
	ld a,(ix+010h)		;9bc1	dd 7e 10	. ~ .
	ld (ix+015h),a		;9bc4	dd 77 15	. w .
	ld a,(ix+011h)		;9bc7	dd 7e 11	. ~ .
	ld (ix+016h),a		;9bca	dd 77 16	. w .
	ld a,(ix+012h)		;9bcd	dd 7e 12	. ~ .
	ld (05cf6h),a		;9bd0	32 f6 5c	2 . \
	ld a,(ix+013h)		;9bd3	dd 7e 13	. ~ .
	ld (05d16h),a		;9bd6	32 16 5d	2 . ]
	ld l,(ix+00dh)		;9bd9	dd 6e 0d	. n .
	ld h,(ix+00eh)		;9bdc	dd 66 0e	. f .
	ld bc,00100h		;9bdf	01 00 01	. . .
	xor a			;9be2	af		.
	sbc hl,bc		;9be3	ed 42		. B
	jr nc,l9bech		;9be5	30 05		0 .
	add hl,bc		;9be7	09		.
	ld c,l			;9be8	4d		M
	ld b,h			;9be9	44		D
	ld l,a			;9bea	6f		o
	ld h,a			;9beb	67		g
l9bech:
	ld (ix+00dh),l		;9bec	dd 75 0d	. u .
	ld (ix+00eh),h		;9bef	dd 74 0e	. t .
	ld (ix+01ah),c		;9bf2	dd 71 1a	. q .
	ld (ix+01bh),b		;9bf5	dd 70 1b	. p .
	push ix			;9bf8	dd e5		. .
	pop hl			;9bfa	e1		.
	ld bc,0001ch		;9bfb	01 1c 00	. . .
	add hl,bc		;9bfe	09		.
	ld bc,00105h		;9bff	01 05 01	. . .
	ld e,(ix+015h)		;9c02	dd 5e 15	. ^ .
	ld d,(ix+016h)		;9c05	dd 56 16	. V .
	call 03d13h		;9c08	cd 13 3d	. . =
	dec (ix+00fh)		;9c0b	dd 35 0f	. 5 .
	ld a,(ix+015h)		;9c0e	dd 7e 15	. ~ .
	inc a			;9c11	3c		<
	ld e,a			;9c12	5f		_
	and 00fh		;9c13	e6 0f		. .
	ld (ix+015h),a		;9c15	dd 77 15	. w .
	ld a,e			;9c18	7b		{
	and 0f0h		;9c19	e6 f0		. .
	ret z			;9c1b	c8		.
	inc (ix+016h)		;9c1c	dd 34 16	. 4 .
	ret			;9c1f	c9		.
	ld ix,(0232bh)		;9c20	dd 2a 2b 23	. * + #
	push ix			;9c24	dd e5		. .
	pop hl			;9c26	e1		.
	dec hl			;9c27	2b		+
	dec hl			;9c28	2b		+
	ld bc,00122h		;9c29	01 22 01	. " .
	call 019e8h		;9c2c	cd e8 19	. . .
	ret			;9c2f	c9		.
	ld hl,(05c4fh)		;9c30	2a 4f 5c	* O \
	dec hl			;9c33	2b		+
	ld bc,00122h		;9c34	01 22 01	. " .
	call 01655h		;9c37	cd 55 16	. U .
	inc hl			;9c3a	23		#
	inc hl			;9c3b	23		#
	push hl			;9c3c	e5		.
	pop ix			;9c3d	dd e1		. .
	ld c,014h		;9c3f	0e 14		. .
	inc hl			;9c41	23		#
	inc hl			;9c42	23		#
	call 03d13h		;9c43	cd 13 3d	. . =
	xor a			;9c46	af		.
	ld (ix+01ah),a		;9c47	dd 77 1a	. w .
	ld (ix+01bh),a		;9c4a	dd 77 1b	. w .
	inc a			;9c4d	3c		<
	ld (ix+00fh),a		;9c4e	dd 77 0f	. w .
	ld a,(05cf6h)		;9c51	3a f6 5c	: . \
	ld (ix+012h),a		;9c54	dd 77 12	. w .
	ld a,(05d16h)		;9c57	3a 16 5d	: . ]
	ld (ix+013h),a		;9c5a	dd 77 13	. w .
	ld hl,(05c4fh)		;9c5d	2a 4f 5c	* O \
	dec hl			;9c60	2b		+
	ld bc,00102h		;9c61	01 02 01	. . .
	call 01655h		;9c64	cd 55 16	. U .
	inc hl			;9c67	23		#
	inc hl			;9c68	23		#
	push hl			;9c69	e5		.
	ld bc,00105h		;9c6a	01 05 01	. . .
	ld de,00008h		;9c6d	11 08 00	. . .
	call 03d13h		;9c70	cd 13 3d	. . =
	di			;9c73	f3		.
	pop iy			;9c74	fd e1		. .
	push iy			;9c76	fd e5		. .
	ld bc,000e1h		;9c78	01 e1 00	. . .
	add iy,bc		;9c7b	fd 09		. .
	ld a,(iy+000h)		;9c7d	fd 7e 00	. ~ .
	ld (ix+010h),a		;9c80	dd 77 10	. w .
	ld (ix+015h),a		;9c83	dd 77 15	. w .
	ld a,(iy+001h)		;9c86	fd 7e 01	. ~ .
	ld (ix+011h),a		;9c89	dd 77 11	. w .
	ld (ix+016h),a		;9c8c	dd 77 16	. w .
	ld a,(iy+004h)		;9c8f	fd 7e 04	. ~ .
	ld (ix+018h),a		;9c92	dd 77 18	. w .
	ld a,(iy+005h)		;9c95	fd 7e 05	. ~ .
	ld (ix+019h),a		;9c98	dd 77 19	. w .
	ld a,(iy+003h)		;9c9b	fd 7e 03	. ~ .
	inc a			;9c9e	3c		<
	ld (ix+017h),a		;9c9f	dd 77 17	. w .
	ld iy,05c3ah		;9ca2	fd 21 3a 5c	. ! : \
	ei			;9ca6	fb		.
	pop hl			;9ca7	e1		.
	push af			;9ca8	f5		.
	dec hl			;9ca9	2b		+
	dec hl			;9caa	2b		+
	ld bc,00102h		;9cab	01 02 01	. . .
	call 019e8h		;9cae	cd e8 19	. . .
	pop af			;9cb1	f1		.
	bit 7,a			;9cb2	cb 7f		. .
	scf			;9cb4	37		7
	jp nz,0287ah		;9cb5	c2 7a 28	. z (
	ld a,(ix+018h)		;9cb8	dd 7e 18	. ~ .
	or (ix+019h)		;9cbb	dd b6 19	. . .
	scf			;9cbe	37		7
	jp z,0287ah		;9cbf	ca 7a 28	. z (
	ld hl,(02661h)		;9cc2	2a 61 26	* a &
	ld (ix+000h),l		;9cc5	dd 75 00	. u .
	ld (ix+001h),h		;9cc8	dd 74 01	. t .
	ret			;9ccb	c9		.
	push hl			;9ccc	e5		.
	push de			;9ccd	d5		.
	push bc			;9cce	c5		.
	ld e,(ix+01ah)		;9ccf	dd 5e 1a	. ^ .
	ld d,(ix+01bh)		;9cd2	dd 56 1b	. V .
	ld bc,0001ch		;9cd5	01 1c 00	. . .
	push ix			;9cd8	dd e5		. .
	pop hl			;9cda	e1		.
	add hl,bc		;9cdb	09		.
	add hl,de		;9cdc	19		.
	ld (hl),a		;9cdd	77		w
	inc de			;9cde	13		.
	xor a			;9cdf	af		.
	or d			;9ce0	b2		.
	ld (ix+01ah),e		;9ce1	dd 73 1a	. s .
	call nz,027bbh		;9ce4	c4 bb 27	. . '
	pop bc			;9ce7	c1		.
	pop de			;9ce8	d1		.
	pop hl			;9ce9	e1		.
	ret			;9cea	c9		.
	ld a,(ix+012h)		;9ceb	dd 7e 12	. ~ .
	ld (05cf6h),a		;9cee	32 f6 5c	2 . \
	ld a,(ix+013h)		;9cf1	dd 7e 13	. ~ .
	ld (05d16h),a		;9cf4	32 16 5d	2 . ]
	ld bc,00106h		;9cf7	01 06 01	. . .
	push ix			;9cfa	dd e5		. .
	pop hl			;9cfc	e1		.
	ld de,0001ch		;9cfd	11 1c 00	. . .
	add hl,de		;9d00	19		.
	ld e,(ix+015h)		;9d01	dd 5e 15	. ^ .
	ld d,(ix+016h)		;9d04	dd 56 16	. V .
	call 03d13h		;9d07	cd 13 3d	. . =
	call 026deh		;9d0a	cd de 26	. . &
	inc (ix+00fh)		;9d0d	dd 34 0f	. 4 .
	ld l,(ix+018h)		;9d10	dd 6e 18	. n .
	ld h,(ix+019h)		;9d13	dd 66 19	. f .
	dec hl			;9d16	2b		+
	ld a,l			;9d17	7d		}
	or h			;9d18	b4		.
	scf			;9d19	37		7
	jp z,0287ah		;9d1a	ca 7a 28	. z (
	ld (ix+019h),h		;9d1d	dd 74 19	. t .
	ld (ix+018h),l		;9d20	dd 75 18	. u .
	ret			;9d23	c9		.
	ld ix,(0232dh)		;9d24	dd 2a 2d 23	. * - #
	ld a,(ix+01ah)		;9d28	dd 7e 1a	. ~ .
	or (ix+01bh)		;9d2b	dd b6 1b	. . .
	call nz,027bbh		;9d2e	c4 bb 27	. . '
	dec (ix+00fh)		;9d31	dd 35 0f	. 5 .
	jr z,l9da8h		;9d34	28 72		( r
	ld a,(ix+012h)		;9d36	dd 7e 12	. ~ .
	ld (05cf6h),a		;9d39	32 f6 5c	2 . \
	ld a,(ix+013h)		;9d3c	dd 7e 13	. ~ .
	ld (05d16h),a		;9d3f	32 16 5d	2 . ]
	ld hl,(05c4fh)		;9d42	2a 4f 5c	* O \
	dec hl			;9d45	2b		+
	ld bc,00102h		;9d46	01 02 01	. . .
	push bc			;9d49	c5		.
	call 01655h		;9d4a	cd 55 16	. U .
	inc hl			;9d4d	23		#
	inc hl			;9d4e	23		#
	push hl			;9d4f	e5		.
	ld bc,00105h		;9d50	01 05 01	. . .
	ld de,00008h		;9d53	11 08 00	. . .
	call 03d13h		;9d56	cd 13 3d	. . =
	di			;9d59	f3		.
	pop iy			;9d5a	fd e1		. .
	push iy			;9d5c	fd e5		. .
	ld bc,000e1h		;9d5e	01 e1 00	. . .
	add iy,bc		;9d61	fd 09		. .
	ld a,(ix+015h)		;9d63	dd 7e 15	. ~ .
	ld (iy+000h),a		;9d66	fd 77 00	. w .
	ld a,(ix+016h)		;9d69	dd 7e 16	. ~ .
	ld (iy+001h),a		;9d6c	fd 77 01	. w .
	inc (iy+003h)		;9d6f	fd 34 03	. 4 .
	ld a,(ix+018h)		;9d72	dd 7e 18	. ~ .
	ld (iy+004h),a		;9d75	fd 77 04	. w .
	ld a,(ix+019h)		;9d78	dd 7e 19	. ~ .
	ld (iy+005h),a		;9d7b	fd 77 05	. w .
	ld iy,05c3ah		;9d7e	fd 21 3a 5c	. ! : \
	ei			;9d82	fb		.
	ld c,013h		;9d83	0e 13		. .
	push ix			;9d85	dd e5		. .
	pop hl			;9d87	e1		.
	inc hl			;9d88	23		#
	inc hl			;9d89	23		#
	call 03d13h		;9d8a	cd 13 3d	. . =
	ld c,009h		;9d8d	0e 09		. .
	ld a,(ix+017h)		;9d8f	dd 7e 17	. ~ .
	dec a			;9d92	3d		=
	call 03d13h		;9d93	cd 13 3d	. . =
	pop hl			;9d96	e1		.
	push hl			;9d97	e5		.
	ld de,00008h		;9d98	11 08 00	. . .
	ld bc,00106h		;9d9b	01 06 01	. . .
	call 03d13h		;9d9e	cd 13 3d	. . =
	pop hl			;9da1	e1		.
	dec hl			;9da2	2b		+
	dec hl			;9da3	2b		+
	pop bc			;9da4	c1		.
	call 019e8h		;9da5	cd e8 19	. . .
l9da8h:
	scf			;9da8	37		7
	ccf			;9da9	3f		?
	push af			;9daa	f5		.
	push ix			;9dab	dd e5		. .
	pop hl			;9dad	e1		.
	dec hl			;9dae	2b		+
	dec hl			;9daf	2b		+
	ld bc,00122h		;9db0	01 22 01	. " .
	call 019e8h		;9db3	cd e8 19	. . .
	pop af			;9db6	f1		.
	ret nc			;9db7	d0		.
	xor a			;9db8	af		.
	call 01601h		;9db9	cd 01 16	. . .
	rst 8			;9dbc	cf		.
	rlca			;9dbd	07		.
	ld c,001h		;9dbe	0e 01		. .
	jp 03d13h		;9dc0	c3 13 3d	. . =
	push af			;9dc3	f5		.
	call 03d13h		;9dc4	cd 13 3d	. . =
	pop af			;9dc7	f1		.
	ld (05cf8h),a		;9dc8	32 f8 5c	2 . \
	ld (05cf9h),a		;9dcb	32 f9 5c	2 . \
	ret			;9dce	c9		.
	sbc a,l			;9dcf	9d		.
	ld e,000h		;9dd0	1e 00		. .
	ld hl,02400h		;9dd2	21 00 24	! . $
	nop			;9dd5	00		.
	daa			;9dd6	27		'
	nop			;9dd7	00		.
	ld hl,(02d00h)		;9dd8	2a 00 2d	* . -
	nop			;9ddb	00		.
	jr nc,l9ddeh		;9ddc	30 00		0 .
l9ddeh:
	add hl,sp		;9dde	39		9
	nop			;9ddf	00		.
	ld a,000h		;9de0	3e 00		> .
	ld b,l			;9de2	45		E
	nop			;9de3	00		.
	ld c,b			;9de4	48		H
	nop			;9de5	00		.
	ld c,e			;9de6	4b		K
	nop			;9de7	00		.
	ld c,a			;9de8	4f		O
	nop			;9de9	00		.
	ld h,e			;9dea	63		c
	nop			;9deb	00		.
	ld (hl),d		;9dec	72		r
	nop			;9ded	00		.
	ld a,b			;9dee	78		x
	nop			;9def	00		.
	push af			;9df0	f5		.
	nop			;9df1	00		.
	jp m,00500h		;9df2	fa 00 05	. . .
	ld bc,0011ah		;9df5	01 1a 01	. . .
	ld hl,05301h		;9df8	21 01 53	! . S
	ld bc,00182h		;9dfb	01 82 01	. . .
	adc a,e			;9dfe	8b		.
	ld bc,00192h		;9dff	01 92 01	. . .
	sbc a,d			;9e02	9a		.
	ld bc,001a5h		;9e03	01 a5 01	. . .
	ret nz			;9e06	c0		.
	ld bc,001c7h		;9e07	01 c7 01	. . .
	out (001h),a		;9e0a	d3 01		. .
	or 001h			;9e0c	f6 01		. .
	ld de,03802h		;9e0e	11 02 38	. . 8
	ld (bc),a		;9e11	02		.
	dec a			;9e12	3d		=
	ld (bc),a		;9e13	02		.
	ld b,l			;9e14	45		E
	ld (bc),a		;9e15	02		.
	ld c,d			;9e16	4a		J
	ld (bc),a		;9e17	02		.
	ld d,d			;9e18	52		R
	ld (bc),a		;9e19	02		.
	or c			;9e1a	b1		.
	ld (bc),a		;9e1b	02		.
	or a			;9e1c	b7		.
	ld (bc),a		;9e1d	02		.
	ret z			;9e1e	c8		.
	ld (bc),a		;9e1f	02		.
	ld a,(de)		;9e20	1a		.
	inc bc			;9e21	03		.
	dec e			;9e22	1d		.
	inc bc			;9e23	03		.
	ld (04503h),a		;9e24	32 03 45	2 . E
	inc bc			;9e27	03		.
	ld e,l			;9e28	5d		]
	inc bc			;9e29	03		.
	ld h,d			;9e2a	62		b
	inc bc			;9e2b	03		.
	ld a,h			;9e2c	7c		|
	inc bc			;9e2d	03		.
	adc a,d			;9e2e	8a		.
	inc bc			;9e2f	03		.
	sbc a,h			;9e30	9c		.
	inc bc			;9e31	03		.
	and h			;9e32	a4		.
	inc bc			;9e33	03		.
	xor c			;9e34	a9		.
	inc bc			;9e35	03		.
	or c			;9e36	b1		.
	inc bc			;9e37	03		.
	cp (hl)			;9e38	be		.
	inc bc			;9e39	03		.
	jp 0c603h		;9e3a	c3 03 c6	. . .
	inc bc			;9e3d	03		.
	ret			;9e3e	c9		.
	inc bc			;9e3f	03		.
	ret nc			;9e40	d0		.
	inc bc			;9e41	03		.
	call c,0e103h		;9e42	dc 03 e1	. . .
	inc bc			;9e45	03		.
	and 003h		;9e46	e6 03		. .
	call pe,0ef03h		;9e48	ec 03 ef	. . .
	inc bc			;9e4b	03		.
	push af			;9e4c	f5		.
	inc bc			;9e4d	03		.
	ret m			;9e4e	f8		.
	inc bc			;9e4f	03		.
	ei			;9e50	fb		.
	inc bc			;9e51	03		.
	cp 003h			;9e52	fe 03		. .
	ld bc,00404h		;9e54	01 04 04	. . .
	inc b			;9e57	04		.
	rlca			;9e58	07		.
	inc b			;9e59	04		.
	ld a,(bc)		;9e5a	0a		.
	inc b			;9e5b	04		.
	dec c			;9e5c	0d		.
	inc b			;9e5d	04		.
	djnz l9e64h		;9e5e	10 04		. .
	inc de			;9e60	13		.
	inc b			;9e61	04		.
	ld d,004h		;9e62	16 04		. .
l9e64h:
	add hl,de		;9e64	19		.
	inc b			;9e65	04		.
	ld e,004h		;9e66	1e 04		. .
	ld hl,02404h		;9e68	21 04 24	! . $
	inc b			;9e6b	04		.
	daa			;9e6c	27		'
	inc b			;9e6d	04		.
	inc l			;9e6e	2c		,
	inc b			;9e6f	04		.
	jr nc,l9e76h		;9e70	30 04		0 .
	inc sp			;9e72	33		3
	inc b			;9e73	04		.
	ld c,(hl)		;9e74	4e		N
	inc b			;9e75	04		.
l9e76h:
	ld d,c			;9e76	51		Q
	inc b			;9e77	04		.
	ld e,c			;9e78	59		Y
	inc b			;9e79	04		.
	ld e,a			;9e7a	5f		_
	inc b			;9e7b	04		.
	ld a,d			;9e7c	7a		z
	inc b			;9e7d	04		.
	add a,c			;9e7e	81		.
	inc b			;9e7f	04		.
	sub a			;9e80	97		.
	inc b			;9e81	04		.
	xor e			;9e82	ab		.
	inc b			;9e83	04		.
	or (hl)			;9e84	b6		.
	inc b			;9e85	04		.
	jp 0cc04h		;9e86	c3 04 cc	. . .
	inc b			;9e89	04		.
	push de			;9e8a	d5		.
	inc b			;9e8b	04		.
	call pe,00304h		;9e8c	ec 04 03	. . .
	dec b			;9e8f	05		.
	inc de			;9e90	13		.
	dec b			;9e91	05		.
	add hl,de		;9e92	19		.
	dec b			;9e93	05		.
	ld h,005h		;9e94	26 05		& .
	dec hl			;9e96	2b		+
	dec b			;9e97	05		.
	ld l,005h		;9e98	2e 05		. .
	ld sp,03405h		;9e9a	31 05 34	1 . 4
	dec b			;9e9d	05		.
	jr c,l9ea5h		;9e9e	38 05		8 .
	dec a			;9ea0	3d		=
	dec b			;9ea1	05		.
	ld b,c			;9ea2	41		A
	dec b			;9ea3	05		.
	ld c,d			;9ea4	4a		J
l9ea5h:
	dec b			;9ea5	05		.
	ld c,l			;9ea6	4d		M
	dec b			;9ea7	05		.
	ld d,b			;9ea8	50		P
	dec b			;9ea9	05		.
	ld d,(hl)		;9eaa	56		V
	dec b			;9eab	05		.
	ld e,e			;9eac	5b		[
	dec b			;9ead	05		.
	ld h,d			;9eae	62		b
	dec b			;9eaf	05		.
	ld h,(hl)		;9eb0	66		f
	dec b			;9eb1	05		.
	ld l,c			;9eb2	69		i
	dec b			;9eb3	05		.
	ld l,l			;9eb4	6d		m
	dec b			;9eb5	05		.
	ld (hl),h		;9eb6	74		t
	dec b			;9eb7	05		.
	ld a,e			;9eb8	7b		{
	dec b			;9eb9	05		.
	ld a,a			;9eba	7f		.
	dec b			;9ebb	05		.
	add a,d			;9ebc	82		.
	dec b			;9ebd	05		.
	add a,(hl)		;9ebe	86		.
	dec b			;9ebf	05		.
	sub e			;9ec0	93		.
	dec b			;9ec1	05		.
	sub a			;9ec2	97		.
	dec b			;9ec3	05		.
	sbc a,d			;9ec4	9a		.
	dec b			;9ec5	05		.
	and c			;9ec6	a1		.
	dec b			;9ec7	05		.
	xor c			;9ec8	a9		.
	dec b			;9ec9	05		.
	ret z			;9eca	c8		.
	dec b			;9ecb	05		.
	sub 005h		;9ecc	d6 05		. .
	exx			;9ece	d9		.
	dec b			;9ecf	05		.
	call c,0e505h		;9ed0	dc 05 e5	. . .
	dec b			;9ed3	05		.
	ld a,(bc)		;9ed4	0a		.
	ld b,00dh		;9ed5	06 0d		. .
	ld b,011h		;9ed7	06 11		. .
	ld b,014h		;9ed9	06 14		. .
	ld b,01ah		;9edb	06 1a		. .
	ld b,01eh		;9edd	06 1e		. .
	ld b,028h		;9edf	06 28		. (
	ld b,02eh		;9ee1	06 2e		. .
	ld b,034h		;9ee3	06 34		. 4
	ld b,039h		;9ee5	06 39		. 9
	ld b,03fh		;9ee7	06 3f		. ?
	ld b,043h		;9ee9	06 43		. C
	ld b,04dh		;9eeb	06 4d		. M
	ld b,050h		;9eed	06 50		. P
	ld b,054h		;9eef	06 54		. T
	ld b,058h		;9ef1	06 58		. X
	ld b,061h		;9ef3	06 61		. a
	ld b,064h		;9ef5	06 64		. d
	ld b,06dh		;9ef7	06 6d		. m
	ld b,081h		;9ef9	06 81		. .
	ld b,09fh		;9efb	06 9f		. .
	ld b,0a5h		;9efd	06 a5		. .
	ld b,0aah		;9eff	06 aa		. .
	ld b,0aeh		;9f01	06 ae		. .
	ld b,0b1h		;9f03	06 b1		. .
	ld b,0b7h		;9f05	06 b7		. .
	ld b,0bah		;9f07	06 ba		. .
	ld b,0bdh		;9f09	06 bd		. .
	ld b,0d5h		;9f0b	06 d5		. .
	ld b,0d8h		;9f0d	06 d8		. .
	ld b,0e4h		;9f0f	06 e4		. .
	ld b,0e9h		;9f11	06 e9		. .
	ld b,0fch		;9f13	06 fc		. .
	ld b,005h		;9f15	06 05		. .
	rlca			;9f17	07		.
	djnz l9f21h		;9f18	10 07		. .
	inc d			;9f1a	14		.
	rlca			;9f1b	07		.
	dec h			;9f1c	25		%
	rlca			;9f1d	07		.
	ld hl,(02d07h)		;9f1e	2a 07 2d	* . -
l9f21h:
	rlca			;9f21	07		.
	ld (hl),007h		;9f22	36 07		6 .
	ld a,007h		;9f24	3e 07		> .
	ld b,h			;9f26	44		D
	rlca			;9f27	07		.
	ld e,l			;9f28	5d		]
	rlca			;9f29	07		.
	ld h,(hl)		;9f2a	66		f
	rlca			;9f2b	07		.
	ld l,h			;9f2c	6c		l
	rlca			;9f2d	07		.
	ld (hl),c		;9f2e	71		q
	rlca			;9f2f	07		.
	add a,e			;9f30	83		.
	rlca			;9f31	07		.
	adc a,d			;9f32	8a		.
	rlca			;9f33	07		.
	sub b			;9f34	90		.
	rlca			;9f35	07		.
	sub e			;9f36	93		.
	rlca			;9f37	07		.
	sub (hl)		;9f38	96		.
	rlca			;9f39	07		.
	sbc a,d			;9f3a	9a		.
	rlca			;9f3b	07		.
	xor c			;9f3c	a9		.
	rlca			;9f3d	07		.
	cp d			;9f3e	ba		.
	rlca			;9f3f	07		.
	cp a			;9f40	bf		.
	rlca			;9f41	07		.
	ret			;9f42	c9		.
	rlca			;9f43	07		.
	out (007h),a		;9f44	d3 07		. .
	jp c,0e507h		;9f46	da 07 e5	. . .
	rlca			;9f49	07		.
	ret pe			;9f4a	e8		.
	rlca			;9f4b	07		.
	rst 28h			;9f4c	ef		.
	rlca			;9f4d	07		.
	jp p,0f707h		;9f4e	f2 07 f7	. . .
	rlca			;9f51	07		.
	ei			;9f52	fb		.
	rlca			;9f53	07		.
	dec b			;9f54	05		.
	ex af,af'		;9f55	08		.
	add hl,bc		;9f56	09		.
	ex af,af'		;9f57	08		.
	inc c			;9f58	0c		.
	ex af,af'		;9f59	08		.
	inc d			;9f5a	14		.
	ex af,af'		;9f5b	08		.
	rla			;9f5c	17		.
	ex af,af'		;9f5d	08		.
	jr nz,l9f68h		;9f5e	20 08		  .
	dec hl			;9f60	2b		+
	ex af,af'		;9f61	08		.
	ld (03608h),a		;9f62	32 08 36	2 . 6
	ex af,af'		;9f65	08		.
	dec sp			;9f66	3b		;
	ex af,af'		;9f67	08		.
l9f68h:
	ld b,e			;9f68	43		C
	ex af,af'		;9f69	08		.
	ld b,a			;9f6a	47		G
	ex af,af'		;9f6b	08		.
	ld c,a			;9f6c	4f		O
	ex af,af'		;9f6d	08		.
	ld d,a			;9f6e	57		W
	ex af,af'		;9f6f	08		.
	ld h,c			;9f70	61		a
	ex af,af'		;9f71	08		.
	ld (hl),c		;9f72	71		q
	ex af,af'		;9f73	08		.
	ld a,e			;9f74	7b		{
	ex af,af'		;9f75	08		.
	add a,c			;9f76	81		.
	ex af,af'		;9f77	08		.
	add a,h			;9f78	84		.
	ex af,af'		;9f79	08		.
	adc a,c			;9f7a	89		.
	ex af,af'		;9f7b	08		.
	sub d			;9f7c	92		.
	ex af,af'		;9f7d	08		.
	sub a			;9f7e	97		.
	ex af,af'		;9f7f	08		.
	sbc a,e			;9f80	9b		.
	ex af,af'		;9f81	08		.
	and c			;9f82	a1		.
	ex af,af'		;9f83	08		.
	xor d			;9f84	aa		.
	ex af,af'		;9f85	08		.
	xor l			;9f86	ad		.
	ex af,af'		;9f87	08		.
	or l			;9f88	b5		.
	ex af,af'		;9f89	08		.
	cp l			;9f8a	bd		.
	ex af,af'		;9f8b	08		.
	pop bc			;9f8c	c1		.
	ex af,af'		;9f8d	08		.
	push bc			;9f8e	c5		.
	ex af,af'		;9f8f	08		.
	ret z			;9f90	c8		.
	ex af,af'		;9f91	08		.
	call z,0d508h		;9f92	cc 08 d5	. . .
	ex af,af'		;9f95	08		.
	jp c,0de08h		;9f96	da 08 de	. . .
	ex af,af'		;9f99	08		.
	pop hl			;9f9a	e1		.
	ex af,af'		;9f9b	08		.
	jp (hl)			;9f9c	e9		.
	ex af,af'		;9f9d	08		.
	rst 28h			;9f9e	ef		.
	ex af,af'		;9f9f	08		.
	or 008h			;9fa0	f6 08		. .
	ld sp,hl		;9fa2	f9		.
	ex af,af'		;9fa3	08		.
	inc bc			;9fa4	03		.
	add hl,bc		;9fa5	09		.
	ex af,af'		;9fa6	08		.
	add hl,bc		;9fa7	09		.
	djnz l9fb3h		;9fa8	10 09		. .
	inc de			;9faa	13		.
	add hl,bc		;9fab	09		.
	rla			;9fac	17		.
	add hl,bc		;9fad	09		.
	ld a,(de)		;9fae	1a		.
	add hl,bc		;9faf	09		.
	inc (hl)		;9fb0	34		4
	add hl,bc		;9fb1	09		.
	dec sp			;9fb2	3b		;
l9fb3h:
	add hl,bc		;9fb3	09		.
	ccf			;9fb4	3f		?
	add hl,bc		;9fb5	09		.
	ld b,d			;9fb6	42		B
	add hl,bc		;9fb7	09		.
	ld c,c			;9fb8	49		I
	add hl,bc		;9fb9	09		.
	ld d,b			;9fba	50		P
	add hl,bc		;9fbb	09		.
	ld (hl),a		;9fbc	77		w
	add hl,bc		;9fbd	09		.
	add a,h			;9fbe	84		.
	add hl,bc		;9fbf	09		.
	pop bc			;9fc0	c1		.
	add hl,bc		;9fc1	09		.
	push bc			;9fc2	c5		.
	add hl,bc		;9fc3	09		.
	call z,0d109h		;9fc4	cc 09 d1	. . .
	add hl,bc		;9fc7	09		.
	exx			;9fc8	d9		.
	add hl,bc		;9fc9	09		.
	add ix,bc		;9fca	dd 09		. .
	ret po			;9fcc	e0		.
	add hl,bc		;9fcd	09		.
	and 009h		;9fce	e6 09		. .
	jp (hl)			;9fd0	e9		.
	add hl,bc		;9fd1	09		.
	pop af			;9fd2	f1		.
	add hl,bc		;9fd3	09		.
	call p,0fa09h		;9fd4	f4 09 fa	. . .
	add hl,bc		;9fd7	09		.
	rlca			;9fd8	07		.
	ld a,(bc)		;9fd9	0a		.
	dec de			;9fda	1b		.
	ld a,(bc)		;9fdb	0a		.
	inc hl			;9fdc	23		#
	ld a,(bc)		;9fdd	0a		.
	dec (hl)		;9fde	35		5
	ld a,(bc)		;9fdf	0a		.
	jr c,l9fech		;9fe0	38 0a		8 .
	ld c,a			;9fe2	4f		O
	ld a,(bc)		;9fe3	0a		.
	ld d,d			;9fe4	52		R
	ld a,(bc)		;9fe5	0a		.
	ld d,l			;9fe6	55		U
	ld a,(bc)		;9fe7	0a		.
	ld e,c			;9fe8	59		Y
	ld a,(bc)		;9fe9	0a		.
	ld e,h			;9fea	5c		\
	ld a,(bc)		;9feb	0a		.
l9fech:
	ld h,h			;9fec	64		d
	ld a,(bc)		;9fed	0a		.
	ld l,c			;9fee	69		i
	ld a,(bc)		;9fef	0a		.
	adc a,h			;9ff0	8c		.
	ld a,(bc)		;9ff1	0a		.
	sub c			;9ff2	91		.
	ld a,(bc)		;9ff3	0a		.
	sub h			;9ff4	94		.
	ld a,(bc)		;9ff5	0a		.
	sub a			;9ff6	97		.
	ld a,(bc)		;9ff7	0a		.
	sbc a,(hl)		;9ff8	9e		.
	ld a,(bc)		;9ff9	0a		.
	and e			;9ffa	a3		.
	ld a,(bc)		;9ffb	0a		.
	or b			;9ffc	b0		.
	ld a,(bc)		;9ffd	0a		.
	cp e			;9ffe	bb		.
	ld a,(bc)		;9fff	0a		.
	pop bc			;a000	c1		.
	ld a,(bc)		;a001	0a		.
	call m,0ff0ah		;a002	fc 0a ff	. . .
	ld a,(bc)		;a005	0a		.
	ld (bc),a		;a006	02		.
	dec bc			;a007	0b		.
	jr $+13			;a008	18 0b		. .
	inc e			;a00a	1c		.
	dec bc			;a00b	0b		.
	rra			;a00c	1f		.
	dec bc			;a00d	0b		.
	inc h			;a00e	24		$
	dec bc			;a00f	0b		.
	add hl,hl		;a010	29		)
	dec bc			;a011	0b		.
	inc l			;a012	2c		,
	dec bc			;a013	0b		.
	ld (0380bh),a		;a014	32 0b 38	2 . 8
	dec bc			;a017	0b		.
	ld l,b			;a018	68		h
	dec bc			;a019	0b		.
	adc a,d			;a01a	8a		.
	dec bc			;a01b	0b		.
	sub l			;a01c	95		.
	dec bc			;a01d	0b		.
	sbc a,b			;a01e	98		.
	dec bc			;a01f	0b		.
	sbc a,e			;a020	9b		.
	dec bc			;a021	0b		.
	and d			;a022	a2		.
	dec bc			;a023	0b		.
	and l			;a024	a5		.
	dec bc			;a025	0b		.
	or a			;a026	b7		.
	dec bc			;a027	0b		.
	call 0db0bh		;a028	cd 0b db	. . .
	dec bc			;a02b	0b		.
	ret po			;a02c	e0		.
	dec bc			;a02d	0b		.
	rst 28h			;a02e	ef		.
	dec bc			;a02f	0b		.
	di			;a030	f3		.
	dec bc			;a031	0b		.
	rst 30h			;a032	f7		.
	dec bc			;a033	0b		.
	inc c			;a034	0c		.
	inc c			;a035	0c		.
	dec e			;a036	1d		.
	inc c			;a037	0c		.
	ld (0290ch),hl		;a038	22 0c 29	" . )
	inc c			;a03b	0c		.
	ld l,00ch		;a03c	2e 0c		. .
	ld sp,0350ch		;a03e	31 0c 35	1 . 5
	inc c			;a041	0c		.
	ld c,d			;a042	4a		J
	inc c			;a043	0c		.
	ld d,d			;a044	52		R
	inc c			;a045	0c		.
	ld d,l			;a046	55		U
	inc c			;a047	0c		.
	ld e,b			;a048	58		X
	inc c			;a049	0c		.
	ld e,h			;a04a	5c		\
	inc c			;a04b	0c		.
	ld h,h			;a04c	64		d
	inc c			;a04d	0c		.
	ld l,b			;a04e	68		h
	inc c			;a04f	0c		.
	ld l,e			;a050	6b		k
	inc c			;a051	0c		.
	ld a,h			;a052	7c		|
	inc c			;a053	0c		.
	add a,l			;a054	85		.
	inc c			;a055	0c		.
	adc a,e			;a056	8b		.
	inc c			;a057	0c		.
	adc a,(hl)		;a058	8e		.
	inc c			;a059	0c		.
	sub h			;a05a	94		.
	inc c			;a05b	0c		.
	sub a			;a05c	97		.
	inc c			;a05d	0c		.
	sbc a,h			;a05e	9c		.
	inc c			;a05f	0c		.
	sbc a,a			;a060	9f		.
	inc c			;a061	0c		.
	and e			;a062	a3		.
	inc c			;a063	0c		.
	or e			;a064	b3		.
	inc c			;a065	0c		.
	cp d			;a066	ba		.
	inc c			;a067	0c		.
	cp l			;a068	bd		.
	inc c			;a069	0c		.
	jp nz,0c60ch		;a06a	c2 0c c6	. . .
	inc c			;a06d	0c		.
	pop de			;a06e	d1		.
	inc c			;a06f	0c		.
	rst 18h			;a070	df		.
	inc c			;a071	0c		.
	jp po,0e60ch		;a072	e2 0c e6	. . .
	inc c			;a075	0c		.
	ei			;a076	fb		.
	inc c			;a077	0c		.
	ld (hl),00dh		;a078	36 0d		6 .
	ld b,h			;a07a	44		D
	dec c			;a07b	0d		.
	ld b,a			;a07c	47		G
	dec c			;a07d	0d		.
	ld e,c			;a07e	59		Y
	dec c			;a07f	0d		.
	ld l,d			;a080	6a		j
	dec c			;a081	0d		.
	ld (hl),e		;a082	73		s
	dec c			;a083	0d		.
	ld a,b			;a084	78		x
	dec c			;a085	0d		.
	ld a,e			;a086	7b		{
	dec c			;a087	0d		.
	ld a,(hl)		;a088	7e		~
	dec c			;a089	0d		.
	add a,c			;a08a	81		.
	dec c			;a08b	0d		.
	sub c			;a08c	91		.
	dec c			;a08d	0d		.
	sub a			;a08e	97		.
	dec c			;a08f	0d		.
	sbc a,e			;a090	9b		.
	dec c			;a091	0d		.
	sbc a,a			;a092	9f		.
	dec c			;a093	0d		.
	and d			;a094	a2		.
	dec c			;a095	0d		.
	and (hl)		;a096	a6		.
	dec c			;a097	0d		.
	xor (hl)		;a098	ae		.
	dec c			;a099	0d		.
	cp e			;a09a	bb		.
	dec c			;a09b	0d		.
	cp a			;a09c	bf		.
	dec c			;a09d	0d		.
	jp c,0e10dh		;a09e	da 0d e1	. . .
	dec c			;a0a1	0d		.
	push hl			;a0a2	e5		.
	dec c			;a0a3	0d		.
	ret pe			;a0a4	e8		.
	dec c			;a0a5	0d		.
	ex de,hl		;a0a6	eb		.
	dec c			;a0a7	0d		.
	xor 00dh		;a0a8	ee 0d		. .
	di			;a0aa	f3		.
	dec c			;a0ab	0d		.
	inc bc			;a0ac	03		.
	ld c,008h		;a0ad	0e 08		. .
	ld c,00bh		;a0af	0e 0b		. .
	ld c,010h		;a0b1	0e 10		. .
	ld c,014h		;a0b3	0e 14		. .
	ld c,018h		;a0b5	0e 18		. .
	ld c,01eh		;a0b7	0e 1e		. .
	ld c,021h		;a0b9	0e 21		. !
	ld c,029h		;a0bb	0e 29		. )
	ld c,02dh		;a0bd	0e 2d		. -
	ld c,031h		;a0bf	0e 31		. 1
	ld c,03ah		;a0c1	0e 3a		. :
	ld c,04eh		;a0c3	0e 4e		. N
	ld c,054h		;a0c5	0e 54		. T
	ld c,05bh		;a0c7	0e 5b		. [
	ld c,05eh		;a0c9	0e 5e		. ^
	ld c,061h		;a0cb	0e 61		. a
	ld c,06ah		;a0cd	0e 6a		. j
	ld c,06dh		;a0cf	0e 6d		. m
	ld c,070h		;a0d1	0e 70		. p
	ld c,073h		;a0d3	0e 73		. s
	ld c,077h		;a0d5	0e 77		. w
	ld c,07ch		;a0d7	0e 7c		. |
	ld c,080h		;a0d9	0e 80		. .
	ld c,08fh		;a0db	0e 8f		. .
	ld c,092h		;a0dd	0e 92		. .
	ld c,095h		;a0df	0e 95		. .
	ld c,098h		;a0e1	0e 98		. .
	ld c,09dh		;a0e3	0e 9d		. .
	ld c,0a0h		;a0e5	0e a0		. .
	ld c,0a6h		;a0e7	0e a6		. .
	ld c,0ach		;a0e9	0e ac		. .
	ld c,0b4h		;a0eb	0e b4		. .
	ld c,0c7h		;a0ed	0e c7		. .
	ld c,0cbh		;a0ef	0e cb		. .
	ld c,0d1h		;a0f1	0e d1		. .
	ld c,0d4h		;a0f3	0e d4		. .
	ld c,0d7h		;a0f5	0e d7		. .
	ld c,0dah		;a0f7	0e da		. .
	ld c,0ddh		;a0f9	0e dd		. .
	ld c,0e0h		;a0fb	0e e0		. .
la0fdh:
	ld c,0e7h		;a0fd	0e e7		. .
	ld c,008h		;a0ff	0e 08		. .
	rrca			;a101	0f		.
	dec bc			;a102	0b		.
	rrca			;a103	0f		.
	rrca			;a104	0f		.
	rrca			;a105	0f		.
	jr $+17			;a106	18 0f		. .
	dec de			;a108	1b		.
	rrca			;a109	0f		.
	ld e,00fh		;a10a	1e 0f		. .
	ld hl,0240fh		;a10c	21 0f 24	! . $
	rrca			;a10f	0f		.
	daa			;a110	27		'
la111h:
	rrca			;a111	0f		.
	dec hl			;a112	2b		+
	rrca			;a113	0f		.
	ld l,00fh		;a114	2e 0f		. .
	ld (0380fh),a		;a116	32 0f 38	2 . 8
	rrca			;a119	0f		.
	inc a			;a11a	3c		<
	rrca			;a11b	0f		.
	ld c,c			;a11c	49		I
	rrca			;a11d	0f		.
	ld c,h			;a11e	4c		L
	rrca			;a11f	0f		.
	ld c,a			;a120	4f		O
	rrca			;a121	0f		.
	ld d,d			;a122	52		R
	rrca			;a123	0f		.
	ld d,l			;a124	55		U
	rrca			;a125	0f		.
	ld l,d			;a126	6a		j
	rrca			;a127	0f		.
	ld l,a			;a128	6f		o
	rrca			;a129	0f		.
	ld (hl),d		;a12a	72		r
	rrca			;a12b	0f		.
	ld (hl),l		;a12c	75		u
	rrca			;a12d	0f		.
la12eh:
	ld a,c			;a12e	79		y
	rrca			;a12f	0f		.
	add a,a			;a130	87		.
	rrca			;a131	0f		.
	adc a,d			;a132	8a		.
	rrca			;a133	0f		.
	adc a,l			;a134	8d		.
	rrca			;a135	0f		.
	sub b			;a136	90		.
	rrca			;a137	0f		.
la138h:
	sub e			;a138	93		.
	rrca			;a139	0f		.
	sub (hl)		;a13a	96		.
	rrca			;a13b	0f		.
	sbc a,h			;a13c	9c		.
	rrca			;a13d	0f		.
	and d			;a13e	a2		.
la13fh:
	rrca			;a13f	0f		.
	and (hl)		;a140	a6		.
	rrca			;a141	0f		.
	ld (hl),c		;a142	71		q
	ld (de),a		;a143	12		.
	jp nz,0c60fh		;a144	c2 0f c6	. . .
	rrca			;a147	0f		.
	ret			;a148	c9		.
	rrca			;a149	0f		.
	call z,0cf0fh		;a14a	cc 0f cf	. . .
	rrca			;a14d	0f		.
la14eh:
	jp nc,0da0fh		;a14e	d2 0f da	. . .
	rrca			;a151	0f		.
	defb 0ddh,00fh,0e5h ;illegal sequence	;a152	dd 0f e5	. . .
	rrca			;a155	0f		.
	ret m			;a156	f8		.
	rrca			;a157	0f		.
	ei			;a158	fb		.
	rrca			;a159	0f		.
	cp 00fh			;a15a	fe 0f		. .
	inc d			;a15c	14		.
	djnz $+25		;a15d	10 17		. .
	djnz $+31		;a15f	10 1d		. .
	djnz la197h		;a161	10 34		. 4
	djnz la1a0h		;a163	10 3b		. ;
	djnz la1a5h		;a165	10 3e		. >
	djnz la1ach		;a167	10 43		. C
	djnz $+78		;a169	10 4c		. L
	djnz la1c0h		;a16b	10 53		. S
	djnz la1c5h		;a16d	10 56		. V
	djnz $+91		;a16f	10 59		. Y
	djnz la0fdh		;a171	10 8a		. .
	djnz la111h		;a173	10 9c		. .
	djnz $-94		;a175	10 a0		. .
	djnz la12eh		;a177	10 b5		. .
la179h:
	djnz la138h		;a179	10 bd		. .
	djnz la13fh		;a17b	10 c2		. .
	djnz $-56		;a17d	10 c6		. .
	djnz la14eh		;a17f	10 cd		. .
	djnz la179h		;a181	10 f6		. .
	djnz la188h		;a183	10 03		. .
	ld de,0110ah		;a185	11 0a 11	. . .
la188h:
	rrca			;a188	0f		.
	ld de,01112h		;a189	11 12 11	. . .
	dec d			;a18c	15		.
	ld de,0111ah		;a18d	11 1a 11	. . .
	rra			;a190	1f		.
	ld de,01122h		;a191	11 22 11	. " .
	ld h,011h		;a194	26 11		& .
	add hl,hl		;a196	29		)
la197h:
	ld de,0112ch		;a197	11 2c 11	. , .
	cpl			;a19a	2f		/
	ld de,01132h		;a19b	11 32 11	. 2 .
	ld (hl),011h		;a19e	36 11		6 .
la1a0h:
	inc a			;a1a0	3c		<
	ld de,0113fh		;a1a1	11 3f 11	. ? .
	ld b,d			;a1a4	42		B
la1a5h:
	ld de,01145h		;a1a5	11 45 11	. E .
	ld c,e			;a1a8	4b		K
	ld de,0114eh		;a1a9	11 4e 11	. N .
la1ach:
	ld d,l			;a1ac	55		U
	ld de,01158h		;a1ad	11 58 11	. X .
	ld e,e			;a1b0	5b		[
	ld de,0115eh		;a1b1	11 5e 11	. ^ .
	ld h,c			;a1b4	61		a
	ld de,01164h		;a1b5	11 64 11	. d .
	ld h,a			;a1b8	67		g
	ld de,0116ah		;a1b9	11 6a 11	. j .
	ld (hl),d		;a1bc	72		r
	ld de,01178h		;a1bd	11 78 11	. x .
la1c0h:
	ld a,h			;a1c0	7c		|
	ld de,01187h		;a1c1	11 87 11	. . .
	sub e			;a1c4	93		.
la1c5h:
	ld de,0119bh		;a1c5	11 9b 11	. . .
	xor a			;a1c8	af		.
	ld de,011b7h		;a1c9	11 b7 11	. . .
	cp e			;a1cc	bb		.
	ld de,011bfh		;a1cd	11 bf 11	. . .
	jp nz,0c511h		;a1d0	c2 11 c5	. . .
	ld de,011cah		;a1d3	11 ca 11	. . .
	rst 8			;a1d6	cf		.
	ld de,011dbh		;a1d7	11 db 11	. . .
	jp pe,0ee11h		;a1da	ea 11 ee	. . .
	ld de,011fah		;a1dd	11 fa 11	. . .
	cp 011h			;a1e0	fe 11		. .
	ld (bc),a		;a1e2	02		.
	ld (de),a		;a1e3	12		.
	add hl,bc		;a1e4	09		.
	ld (de),a		;a1e5	12		.
	inc de			;a1e6	13		.
	ld (de),a		;a1e7	12		.
	ld d,012h		;a1e8	16 12		. .
	dec e			;a1ea	1d		.
	ld (de),a		;a1eb	12		.
	jr nz,la200h		;a1ec	20 12		  .
	inc sp			;a1ee	33		3
	ld (de),a		;a1ef	12		.
	ld b,h			;a1f0	44		D
	ld (de),a		;a1f1	12		.
	ld h,l			;a1f2	65		e
	ld (de),a		;a1f3	12		.
	ld l,d			;a1f4	6a		j
	ld (de),a		;a1f5	12		.
	ld l,l			;a1f6	6d		m
	ld (de),a		;a1f7	12		.
	ld a,c			;a1f8	79		y
	ld (de),a		;a1f9	12		.
	ld a,h			;a1fa	7c		|
	ld (de),a		;a1fb	12		.
	adc a,h			;a1fc	8c		.
	ld (de),a		;a1fd	12		.
	sub l			;a1fe	95		.
	ld (de),a		;a1ff	12		.
la200h:
	sbc a,c			;a200	99		.
	ld (de),a		;a201	12		.
	sbc a,h			;a202	9c		.
	ld (de),a		;a203	12		.
	and d			;a204	a2		.
	ld (de),a		;a205	12		.
	or 012h			;a206	f6 12		. .
	inc bc			;a208	03		.
	inc de			;a209	13		.
	rrca			;a20a	0f		.
	inc de			;a20b	13		.
	dec e			;a20c	1d		.
	inc de			;a20d	13		.
	dec hl			;a20e	2b		+
	inc de			;a20f	13		.
	jr nc,la225h		;a210	30 13		0 .
	dec (hl)		;a212	35		5
	inc de			;a213	13		.
	jr c,la229h		;a214	38 13		8 .
	dec sp			;a216	3b		;
	inc de			;a217	13		.
	ld a,013h		;a218	3e 13		> .
	ld b,c			;a21a	41		A
	inc de			;a21b	13		.
	ld c,b			;a21c	48		H
	inc de			;a21d	13		.
	ld c,e			;a21e	4b		K
	inc de			;a21f	13		.
	ld d,b			;a220	50		P
	inc de			;a221	13		.
	ld d,e			;a222	53		S
	inc de			;a223	13		.
	ld d,(hl)		;a224	56		V
la225h:
	inc de			;a225	13		.
	ld e,c			;a226	59		Y
	inc de			;a227	13		.
	ld e,h			;a228	5c		\
la229h:
	inc de			;a229	13		.
	ld (hl),h		;a22a	74		t
	inc de			;a22b	13		.
	ld a,e			;a22c	7b		{
	inc de			;a22d	13		.
	add a,a			;a22e	87		.
	inc de			;a22f	13		.
	sub e			;a230	93		.
	inc de			;a231	13		.
	sub (hl)		;a232	96		.
	inc de			;a233	13		.
	xor d			;a234	aa		.
	inc de			;a235	13		.
	xor l			;a236	ad		.
	inc de			;a237	13		.
	or b			;a238	b0		.
	inc de			;a239	13		.
	or e			;a23a	b3		.
	inc de			;a23b	13		.
	or a			;a23c	b7		.
	inc de			;a23d	13		.
	cp e			;a23e	bb		.
	inc de			;a23f	13		.
	ret nz			;a240	c0		.
	inc de			;a241	13		.
	push bc			;a242	c5		.
	inc de			;a243	13		.
	ret			;a244	c9		.
	inc de			;a245	13		.
	ret nc			;a246	d0		.
	inc de			;a247	13		.
	call nc,0db13h		;a248	d4 13 db	. . .
	inc de			;a24b	13		.
	sbc a,013h		;a24c	de 13		. .
	pop hl			;a24e	e1		.
	inc de			;a24f	13		.
	call po,0eb13h		;a250	e4 13 eb	. . .
	inc de			;a253	13		.
	ret p			;a254	f0		.
	inc de			;a255	13		.
	dec b			;a256	05		.
	inc d			;a257	14		.
	inc hl			;a258	23		#
	inc d			;a259	14		.
	ccf			;a25a	3f		?
	inc d			;a25b	14		.
	ld b,d			;a25c	42		B
	inc d			;a25d	14		.
	ld c,b			;a25e	48		H
	inc d			;a25f	14		.
	ld d,l			;a260	55		U
	inc d			;a261	14		.
	ld e,c			;a262	59		Y
	inc d			;a263	14		.
	ld l,(hl)		;a264	6e		n
	inc d			;a265	14		.
	ld (hl),e		;a266	73		s
	inc d			;a267	14		.
	ld a,d			;a268	7a		z
	inc d			;a269	14		.
	add a,a			;a26a	87		.
	inc d			;a26b	14		.
	sub b			;a26c	90		.
	inc d			;a26d	14		.
	sub h			;a26e	94		.
	inc d			;a26f	14		.
	sbc a,(hl)		;a270	9e		.
	inc d			;a271	14		.
	xor e			;a272	ab		.
	inc d			;a273	14		.
	or c			;a274	b1		.
	inc d			;a275	14		.
	or h			;a276	b4		.
	inc d			;a277	14		.
	jp 0d014h		;a278	c3 14 d0	. . .
	inc d			;a27b	14		.
	rst 10h			;a27c	d7		.
	inc d			;a27d	14		.
	ex (sp),hl		;a27e	e3		.
	inc d			;a27f	14		.
	and 014h		;a280	e6 14		. .
	jp (hl)			;a282	e9		.
	inc d			;a283	14		.
	call pe,0fe14h		;a284	ec 14 fe	. . .
	inc d			;a287	14		.
	ld d,015h		;a288	16 15		. .
	inc e			;a28a	1c		.
	dec d			;a28b	15		.
	ld (02c15h),hl		;a28c	22 15 2c	" . ,
	dec d			;a28f	15		.
	jr c,la2a7h		;a290	38 15		8 .
	ld c,b			;a292	48		H
	dec d			;a293	15		.
	ld c,(hl)		;a294	4e		N
	dec d			;a295	15		.
	ld e,b			;a296	58		X
	dec d			;a297	15		.
la298h:
	ld h,a			;a298	67		g
	dec d			;a299	15		.
	ld l,e			;a29a	6b		k
	dec d			;a29b	15		.
	add a,d			;a29c	82		.
	dec d			;a29d	15		.
	sub c			;a29e	91		.
la29fh:
	dec d			;a29f	15		.
	sbc a,e			;a2a0	9b		.
	dec d			;a2a1	15		.
	and b			;a2a2	a0		.
	dec d			;a2a3	15		.
la2a4h:
	jp z,0e015h		;a2a4	ca 15 e0	. . .
la2a7h:
	dec d			;a2a7	15		.
	jp p,0f615h		;a2a8	f2 15 f6	. . .
	dec d			;a2ab	15		.
	cp 015h			;a2ac	fe 15		. .
la2aeh:
	ld a,(bc)		;a2ae	0a		.
	ld d,00dh		;a2af	16 0d		. .
	ld d,018h		;a2b1	16 18		. .
	ld d,01fh		;a2b3	16 1f		. .
	ld d,038h		;a2b5	16 38		. 8
	ld d,03bh		;a2b7	16 3b		. ;
	ld d,03eh		;a2b9	16 3e		. >
	ld d,047h		;a2bb	16 47		. G
	ld d,04bh		;a2bd	16 4b		. K
	ld d,059h		;a2bf	16 59		. Y
	ld d,05eh		;a2c1	16 5e		. ^
la2c3h:
	ld d,065h		;a2c3	16 65		. e
	ld d,068h		;a2c5	16 68		. h
	ld d,0a6h		;a2c7	16 a6		. .
	ld d,0adh		;a2c9	16 ad		. .
	ld d,0b4h		;a2cb	16 b4		. .
	ld d,0b7h		;a2cd	16 b7		. .
	ld d,0d0h		;a2cf	16 d0		. .
	ld d,0d7h		;a2d1	16 d7		. .
	ld d,0dch		;a2d3	16 dc		. .
	ld d,0e3h		;a2d5	16 e3		. .
	ld d,0e6h		;a2d7	16 e6		. .
	ld d,0f2h		;a2d9	16 f2		. .
	ld d,003h		;a2db	16 03		. .
	rla			;a2dd	17		.
	dec c			;a2de	0d		.
	rla			;a2df	17		.
	inc de			;a2e0	13		.
la2e1h:
	rla			;a2e1	17		.
	add hl,de		;a2e2	19		.
	rla			;a2e3	17		.
	ld (02717h),hl		;a2e4	22 17 27	" . '
	rla			;a2e7	17		.
	dec hl			;a2e8	2b		+
	rla			;a2e9	17		.
la2eah:
	ccf			;a2ea	3f		?
	rla			;a2eb	17		.
	ld b,l			;a2ec	45		E
	rla			;a2ed	17		.
	ld e,h			;a2ee	5c		\
	rla			;a2ef	17		.
	ld e,a			;a2f0	5f		_
	rla			;a2f1	17		.
	ld h,l			;a2f2	65		e
	rla			;a2f3	17		.
	ld l,b			;a2f4	68		h
	rla			;a2f5	17		.
	ld l,e			;a2f6	6b		k
	rla			;a2f7	17		.
	ld l,(hl)		;a2f8	6e		n
	rla			;a2f9	17		.
	ld (hl),h		;a2fa	74		t
	rla			;a2fb	17		.
	ld a,c			;a2fc	79		y
	rla			;a2fd	17		.
	ld a,l			;a2fe	7d		}
	rla			;a2ff	17		.
	add a,(hl)		;a300	86		.
	rla			;a301	17		.
la302h:
	sub d			;a302	92		.
	rla			;a303	17		.
	call nc,0de17h		;a304	d4 17 de	. . .
	rla			;a307	17		.
	inc d			;a308	14		.
	jr $+33			;a309	18 1f		. .
	jr la330h		;a30b	18 23		. #
	jr la37ah		;a30d	18 6b		. k
	jr $+112		;a30f	18 6e		. n
	jr la387h		;a311	18 74		. t
	jr la298h		;a313	18 83		. .
	jr la29fh		;a315	18 88		. .
	jr la2a4h		;a317	18 8b		. .
	jr la2aeh		;a319	18 93		. .
	jr la2c3h		;a31b	18 a6		. .
	jr la2e1h		;a31d	18 c2		. .
	jr la2eah		;a31f	18 c9		. .
la321h:
	jr la302h		;a321	18 df		. .
	jr $-15			;a323	18 ef		. .
	jr la321h		;a325	18 fa		. .
	jr $-1			;a327	18 fd		. .
	jr $+32			;a329	18 1e		. .
	add hl,de		;a32b	19		.
	ld (04619h),hl		;a32c	22 19 46	" . F
	add hl,de		;a32f	19		.
la330h:
	ld d,d			;a330	52		R
	add hl,de		;a331	19		.
	ld h,a			;a332	67		g
	add hl,de		;a333	19		.
	ld l,l			;a334	6d		m
	add hl,de		;a335	19		.
	ld a,d			;a336	7a		z
	add hl,de		;a337	19		.
	sbc a,h			;a338	9c		.
	add hl,de		;a339	19		.
	or (hl)			;a33a	b6		.
	add hl,de		;a33b	19		.
	cp d			;a33c	ba		.
	add hl,de		;a33d	19		.
	pop bc			;a33e	c1		.
	add hl,de		;a33f	19		.
	ret			;a340	c9		.
	add hl,de		;a341	19		.
	pop de			;a342	d1		.
	add hl,de		;a343	19		.
	call nc,0d719h		;a344	d4 19 d7	. . .
	add hl,de		;a347	19		.
	jp po,0e519h		;a348	e2 19 e5	. . .
	add hl,de		;a34b	19		.
	defb 0edh ;next byte illegal after ed	;a34c	ed		.
	add hl,de		;a34d	19		.
	push af			;a34e	f5		.
	add hl,de		;a34f	19		.
	nop			;a350	00		.
	ld a,(de)		;a351	1a		.
	inc b			;a352	04		.
	ld a,(de)		;a353	1a		.
	rlca			;a354	07		.
	ld a,(de)		;a355	1a		.
	inc d			;a356	14		.
	ld a,(de)		;a357	1a		.
	rla			;a358	17		.
	ld a,(de)		;a359	1a		.
	ld b,h			;a35a	44		D
	ld a,(de)		;a35b	1a		.
	ld c,e			;a35c	4b		K
	ld a,(de)		;a35d	1a		.
	ld d,c			;a35e	51		Q
	ld a,(de)		;a35f	1a		.
	ld d,(hl)		;a360	56		V
	ld a,(de)		;a361	1a		.
	ld h,h			;a362	64		d
	ld a,(de)		;a363	1a		.
	ld l,c			;a364	69		i
	ld a,(de)		;a365	1a		.
	ld a,c			;a366	79		y
	ld a,(de)		;a367	1a		.
	ld a,h			;a368	7c		|
	ld a,(de)		;a369	1a		.
	ld a,a			;a36a	7f		.
	ld a,(de)		;a36b	1a		.
	add a,e			;a36c	83		.
	ld a,(de)		;a36d	1a		.
	sbc a,d			;a36e	9a		.
	ld a,(de)		;a36f	1a		.
	sbc a,a			;a370	9f		.
	ld a,(de)		;a371	1a		.
	and h			;a372	a4		.
	ld a,(de)		;a373	1a		.
	xor e			;a374	ab		.
	ld a,(de)		;a375	1a		.
	xor (hl)		;a376	ae		.
	ld a,(de)		;a377	1a		.
	or d			;a378	b2		.
	ld a,(de)		;a379	1a		.
la37ah:
	or a			;a37a	b7		.
	ld a,(de)		;a37b	1a		.
	cp l			;a37c	bd		.
	ld a,(de)		;a37d	1a		.
	jp 0c61ah		;a37e	c3 1a c6	. . .
	ld a,(de)		;a381	1a		.
	sub 01ah		;a382	d6 1a		. .
	ld (03e1bh),hl		;a384	22 1b 3e	" . >
la387h:
	dec de			;a387	1b		.
	ld b,a			;a388	47		G
	dec de			;a389	1b		.
	ld d,(hl)		;a38a	56		V
	dec de			;a38b	1b		.
	ld h,e			;a38c	63		c
	dec de			;a38d	1b		.
	ld l,a			;a38e	6f		o
	dec de			;a38f	1b		.
	ld (hl),a		;a390	77		w
	dec de			;a391	1b		.
	ld a,(hl)		;a392	7e		~
	dec de			;a393	1b		.
	add a,h			;a394	84		.
	dec de			;a395	1b		.
	adc a,(hl)		;a396	8e		.
	dec de			;a397	1b		.
	sbc a,d			;a398	9a		.
	dec de			;a399	1b		.
	xor b			;a39a	a8		.
	dec de			;a39b	1b		.
	jp nz,0cd1bh		;a39c	c2 1b cd	. . .
	dec de			;a39f	1b		.
	and 01bh		;a3a0	e6 1b		. .
	call pe,0f91bh		;a3a2	ec 1b f9	. . .
	dec de			;a3a5	1b		.
	jr c,$+30		;a3a6	38 1c		8 .
	dec sp			;a3a8	3b		;
	inc e			;a3a9	1c		.
	ld b,c			;a3aa	41		A
	inc e			;a3ab	1c		.
	ld b,(hl)		;a3ac	46		F
	inc e			;a3ad	1c		.
	ld c,a			;a3ae	4f		O
	inc e			;a3af	1c		.
	ld d,l			;a3b0	55		U
	inc e			;a3b1	1c		.
	ld e,b			;a3b2	58		X
	inc e			;a3b3	1c		.
	ld e,e			;a3b4	5b		[
	inc e			;a3b5	1c		.
	ld a,c			;a3b6	79		y
	inc e			;a3b7	1c		.
	add a,e			;a3b8	83		.
	inc e			;a3b9	1c		.
	add a,a			;a3ba	87		.
	inc e			;a3bb	1c		.
	xor e			;a3bc	ab		.
	inc e			;a3bd	1c		.
	xor a			;a3be	af		.
	inc e			;a3bf	1c		.
	sub 01ch		;a3c0	d6 1c		. .
	jp c,0f01ch		;a3c2	da 1c f0	. . .
	inc e			;a3c5	1c		.
	jr la3e5h		;a3c6	18 1d		. .
	inc e			;a3c8	1c		.
	dec e			;a3c9	1d		.
	inc l			;a3ca	2c		,
	dec e			;a3cb	1d		.
	ld (hl),01dh		;a3cc	36 1d		6 .
	ld b,b			;a3ce	40		@
	dec e			;a3cf	1d		.
	ld c,d			;a3d0	4a		J
	dec e			;a3d1	1d		.
	ld c,a			;a3d2	4f		O
	dec e			;a3d3	1d		.
	ld d,d			;a3d4	52		R
	dec e			;a3d5	1d		.
	ld d,(hl)		;a3d6	56		V
	dec e			;a3d7	1d		.
	ld e,(hl)		;a3d8	5e		^
	dec e			;a3d9	1d		.
	ld h,l			;a3da	65		e
	dec e			;a3db	1d		.
	ld (hl),b		;a3dc	70		p
	dec e			;a3dd	1d		.
	ld (hl),e		;a3de	73		s
	dec e			;a3df	1d		.
	halt			;a3e0	76		v
	dec e			;a3e1	1d		.
	ld a,e			;a3e2	7b		{
	dec e			;a3e3	1d		.
	add a,e			;a3e4	83		.
la3e5h:
	dec e			;a3e5	1d		.
	add a,a			;a3e6	87		.
	dec e			;a3e7	1d		.
	sub e			;a3e8	93		.
	dec e			;a3e9	1d		.
	sub a			;a3ea	97		.
	dec e			;a3eb	1d		.
	sbc a,a			;a3ec	9f		.
	dec e			;a3ed	1d		.
	xor l			;a3ee	ad		.
	dec e			;a3ef	1d		.
	or b			;a3f0	b0		.
	dec e			;a3f1	1d		.
	or e			;a3f2	b3		.
	dec e			;a3f3	1d		.
	cp h			;a3f4	bc		.
	dec e			;a3f5	1d		.
	jp nc,0d61dh		;a3f6	d2 1d d6	. . .
	dec e			;a3f9	1d		.
	call c,0e11dh		;a3fa	dc 1d e1	. . .
	dec e			;a3fd	1d		.
	call po,0ec1dh		;a3fe	e4 1d ec	. . .
	dec e			;a401	1d		.
	rst 28h			;a402	ef		.
	dec e			;a403	1d		.
	ret m			;a404	f8		.
	dec e			;a405	1d		.
	ei			;a406	fb		.
	dec e			;a407	1d		.
	add hl,bc		;a408	09		.
	ld e,00dh		;a409	1e 0d		. .
	ld e,016h		;a40b	1e 16		. .
	ld e,025h		;a40d	1e 25		. %
	ld e,029h		;a40f	1e 29		. )
	ld e,02fh		;a411	1e 2f		. /
	ld e,032h		;a413	1e 32		. 2
	ld e,035h		;a415	1e 35		. 5
	ld e,038h		;a417	1e 38		. 8
	ld e,03bh		;a419	1e 3b		. ;
	ld e,03eh		;a41b	1e 3e		. >
	ld e,041h		;a41d	1e 41		. A
	ld e,044h		;a41f	1e 44		. D
	ld e,047h		;a421	1e 47		. G
	ld e,04ah		;a423	1e 4a		. J
	ld e,04dh		;a425	1e 4d		. M
	ld e,050h		;a427	1e 50		. P
	ld e,053h		;a429	1e 53		. S
	ld e,056h		;a42b	1e 56		. V
	ld e,059h		;a42d	1e 59		. Y
	ld e,05ch		;a42f	1e 5c		. \
	ld e,061h		;a431	1e 61		. a
	ld e,068h		;a433	1e 68		. h
	ld e,074h		;a435	1e 74		. t
	ld e,089h		;a437	1e 89		. .
	ld e,09ah		;a439	1e 9a		. .
	ld e,0a4h		;a43b	1e a4		. .
	ld e,02eh		;a43d	1e 2e		. .
	ld hl,02131h		;a43f	21 31 21	! 1 !
	inc (hl)		;a442	34		4
	ld hl,02137h		;a443	21 37 21	! 7 !
	ld a,(03d21h)		;a446	3a 21 3d	: ! =
	ld hl,02140h		;a449	21 40 21	! @ !
	ld b,e			;a44c	43		C
	ld hl,02146h		;a44d	21 46 21	! F !
	ld c,c			;a450	49		I
	ld hl,0214ch		;a451	21 4c 21	! L !
	ld c,a			;a454	4f		O
	ld hl,02152h		;a455	21 52 21	! R !
	ld d,l			;a458	55		U
	ld hl,02158h		;a459	21 58 21	! X !
	ld e,e			;a45c	5b		[
	ld hl,0215eh		;a45d	21 5e 21	! ^ !
	ld h,c			;a460	61		a
	ld hl,02164h		;a461	21 64 21	! d !
	ld h,a			;a464	67		g
	ld hl,0216ah		;a465	21 6a 21	! j !
	ld l,l			;a468	6d		m
	ld hl,02170h		;a469	21 70 21	! p !
	ld (hl),e		;a46c	73		s
	ld hl,02176h		;a46d	21 76 21	! v !
	ld a,c			;a470	79		y
	ld hl,0217ch		;a471	21 7c 21	! | !
	ld a,a			;a474	7f		.
	ld hl,02182h		;a475	21 82 21	! . !
	add a,l			;a478	85		.
	ld hl,02188h		;a479	21 88 21	! . !
	adc a,e			;a47c	8b		.
	ld hl,0218eh		;a47d	21 8e 21	! . !
	sub c			;a480	91		.
	ld hl,02194h		;a481	21 94 21	! . !
	sub a			;a484	97		.
	ld hl,0219ah		;a485	21 9a 21	! . !
	sbc a,l			;a488	9d		.
	ld hl,021a0h		;a489	21 a0 21	! . !
	and e			;a48c	a3		.
	ld hl,021a6h		;a48d	21 a6 21	! . !
	xor c			;a490	a9		.
	ld hl,021ach		;a491	21 ac 21	! . !
	xor h			;a494	ac		.
	ld de,00acfh		;a495	11 cf 0a	. . .
	sub l			;a498	95		.
	ld e,053h		;a499	1e 53		. S
	ld h,056h		;a49b	26 56		& V
	ld h,059h		;a49d	26 59		& Y
	ld h,05ch		;a49f	26 5c		& \
	ld h,05fh		;a4a1	26 5f		& _
	ld h,061h		;a4a3	26 61		& a
	ld h,064h		;a4a5	26 64		& d
	ld h,067h		;a4a7	26 67		& g
	ld h,0f2h		;a4a9	26 f2		& .
	ld h,086h		;a4ab	26 86		& .
	daa			;a4ad	27		'
	sub b			;a4ae	90		.
	daa			;a4af	27		'
	sub e			;a4b0	93		.
	daa			;a4b1	27		'
	or l			;a4b2	b5		.
	daa			;a4b3	27		'
	in a,(027h)		;a4b4	db 27		. '
	ex de,hl		;a4b6	eb		.
	daa			;a4b7	27		'
	or 027h			;a4b8	f6 27		. '
	rst 38h			;a4ba	ff		.
	daa			;a4bb	27		'
	nop			;a4bc	00		.
	nop			;a4bd	00		.
