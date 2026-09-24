; z80dasm 1.2.0
; command line: z80dasm -a -t -g 0x5D40 -o scratch/voron1-helpers.asm scratch/voron1-ram/helpers-5D40.bin

	org 05d40h

	ld h,c			;5d40	61		a
	ld l,(hl)		;5d41	6e		n
	ld h,c			;5d42	61		a
	ld h,a			;5d43	67		g
	ld h,l			;5d44	65		e
	ld (hl),d		;5d45	72		r
	jr nz,$+34		;5d46	20 20		   
	halt			;5d48	76		v
	ld sp,03130h		;5d49	31 30 31	1 0 1
	ld h,d			;5d4c	62		b
	push de			;5d4d	d5		.
	ld de,05d48h		;5d4e	11 48 5d	. H ]
	ld bc,00004h		;5d51	01 04 00	. . .
	ldir			;5d54	ed b0		. .
	pop de			;5d56	d1		.
	ld a,(hl)		;5d57	7e		~
	srl a			;5d58	cb 3f		. ?
	jr nc,$+34		;5d5a	30 20		0  
	call 05dceh		;5d5c	cd ce 5d	. . ]
	rra			;5d5f	1f		.
	rl b			;5d60	cb 10		. .
	and 007h		;5d62	e6 07		. .
	jr nz,$+4		;5d64	20 02		  .
	ld a,(hl)		;5d66	7e		~
	inc hl			;5d67	23		#
	ld c,(hl)		;5d68	4e		N
	inc hl			;5d69	23		#
	push hl			;5d6a	e5		.
	ld h,d			;5d6b	62		b
	ld l,e			;5d6c	6b		k
	sbc hl,bc		;5d6d	ed 42		. B
	ld b,000h		;5d6f	06 00		. .
	ld c,a			;5d71	4f		O
	inc bc			;5d72	03		.
	inc bc			;5d73	03		.
	ldir			;5d74	ed b0		. .
	pop hl			;5d76	e1		.
	ex af,af'		;5d77	08		.
	jr z,$-33		;5d78	28 dd		( .
	jr nz,$+11		;5d7a	20 09		  .
	rra			;5d7c	1f		.
	jr c,$+41		;5d7d	38 27		8 '
	rra			;5d7f	1f		.
	jr c,$+15		;5d80	38 0d		8 .
	jr z,$+68		;5d82	28 42		( B
	inc hl			;5d84	23		#
	ld b,a			;5d85	47		G
	ld a,(hl)		;5d86	7e		~
	inc hl			;5d87	23		#
	xor (hl)		;5d88	ae		.
	ld (de),a		;5d89	12		.
	inc de			;5d8a	13		.
	djnz $-5		;5d8b	10 f9		. .
	jr $-54			;5d8d	18 c8		. .
	srl a			;5d8f	cb 3f		. ?
	jr c,$+12		;5d91	38 0a		8 .
	ld c,a			;5d93	4f		O
	xor a			;5d94	af		.
	ex af,af'		;5d95	08		.
	inc hl			;5d96	23		#
	push hl			;5d97	e5		.
	ld h,d			;5d98	62		b
	ld l,e			;5d99	6b		k
	dec hl			;5d9a	2b		+
	jr $-41			;5d9b	18 d5		. .
	call 05dceh		;5d9d	cd ce 5d	. . ]
	rra			;5da0	1f		.
	rl b			;5da1	cb 10		. .
	ld c,(hl)		;5da3	4e		N
	jr $-14			;5da4	18 f0		. .
	srl a			;5da6	cb 3f		. ?
	jr nc,$+15		;5da8	30 0d		0 .
	ld c,a			;5daa	4f		O
	inc hl			;5dab	23		#
	ld a,(hl)		;5dac	7e		~
	and 01fh		;5dad	e6 1f		. .
	ld b,a			;5daf	47		G
	ld a,c			;5db0	79		y
	call 05dceh		;5db1	cd ce 5d	. . ]
	or a			;5db4	b7		.
	jr $-81			;5db5	18 ad		. .
	inc a			;5db7	3c		<
	ld c,a			;5db8	4f		O
	inc hl			;5db9	23		#
	push hl			;5dba	e5		.
	ld h,d			;5dbb	62		b
	ld l,e			;5dbc	6b		k
	sbc hl,bc		;5dbd	ed 42		. B
	ld c,002h		;5dbf	0e 02		. .
	ldir			;5dc1	ed b0		. .
	pop hl			;5dc3	e1		.
	jr $-109		;5dc4	18 91		. .
	ld hl,05d48h		;5dc6	21 48 5d	! H ]
	ld c,004h		;5dc9	0e 04		. .
	ldir			;5dcb	ed b0		. .
	ret			;5dcd	c9		.
	ex af,af'		;5dce	08		.
	ld a,(hl)		;5dcf	7e		~
	rlca			;5dd0	07		.
	rlca			;5dd1	07		.
	rlca			;5dd2	07		.
	and 007h		;5dd3	e6 07		. .
	ex af,af'		;5dd5	08		.
	inc hl			;5dd6	23		#
	ret			;5dd7	c9		.
	ld de,0fffeh		;5dd8	11 fe ff	. . .
	push hl			;5ddb	e5		.
	ld c,(hl)		;5ddc	4e		N
	inc hl			;5ddd	23		#
	ld b,(hl)		;5dde	46		F
	add hl,bc		;5ddf	09		.
	lddr			;5de0	ed b8		. .
	ex de,hl		;5de2	eb		.
	inc hl			;5de3	23		#
	pop de			;5de4	d1		.
	jp 05d4dh		;5de5	c3 4d 5d	. M ]
	xor a			;5de8	af		.
	jr $+4			;5de9	18 02		. .
	ld a,007h		;5deb	3e 07		> .
	or 010h			;5ded	f6 10		. .
	push bc			;5def	c5		.
	ld bc,07ffdh		;5df0	01 fd 7f	. . .
	out (c),a		;5df3	ed 79		. y
	pop bc			;5df5	c1		.
	ret			;5df6	c9		.
	xor a			;5df7	af		.
	ld de,05afeh		;5df8	11 fe 5a	. . Z
	ld hl,05affh		;5dfb	21 ff 5a	! . Z
	ld bc,00300h		;5dfe	01 00 03	. . .
	ld (hl),a		;5e01	77		w
	lddr			;5e02	ed b8		. .
	ret			;5e04	c9		.
	call 05debh		;5e05	cd eb 5d	. . ]
	ld de,0dafeh		;5e08	11 fe da	. . .
	ld hl,0daffh		;5e0b	21 ff da	! . .
	ld bc,00300h		;5e0e	01 00 03	. . .
	ld (hl),000h		;5e11	36 00		6 .
	lddr			;5e13	ed b8		. .
	ret			;5e15	c9		.
	ld a,010h		;5e16	3e 10		> .
	jr $+4			;5e18	18 02		. .
	ld a,018h		;5e1a	3e 18		> .
	ld (05deeh),a		;5e1c	32 ee 5d	2 . ]
	ret			;5e1f	c9		.
	ld hl,(0237ch)		;5e20	2a 7c 23	* | #
	xor (hl)		;5e23	ae		.
	call 05f10h		;5e24	cd 10 5f	. . _
	djnz $+97		;5e27	10 5f		. _
	djnz $+97		;5e29	10 5f		. _
	djnz $+97		;5e2b	10 5f		. _
	djnz $+97		;5e2d	10 5f		. _
	djnz $+97		;5e2f	10 5f		. _
	djnz $+97		;5e31	10 5f		. _
	djnz $+97		;5e33	10 5f		. _
	djnz $+97		;5e35	10 5f		. _
	djnz $+97		;5e37	10 5f		. _
	djnz $+97		;5e39	10 5f		. _
	djnz $+97		;5e3b	10 5f		. _
	djnz $+97		;5e3d	10 5f		. _
	djnz $+97		;5e3f	10 5f		. _
	di			;5e41	f3		.
	ld ix,02f5fh		;5e42	dd 21 5f 2f	. ! _ /
	call 06085h		;5e46	cd 85 60	. . `
	ld ix,02f65h		;5e49	dd 21 65 2f	. ! e /
	jp 06085h		;5e4d	c3 85 60	. . `
	ret nz			;5e50	c0		.
	nop			;5e51	00		.
	djnz $-62		;5e52	10 c0		. .
	ld (bc),a		;5e54	02		.
	add hl,bc		;5e55	09		.
	call nz,00a03h		;5e56	c4 03 0a	. . .
	call nz,00805h		;5e59	c4 05 08	. . .
	jp nz,02807h		;5e5c	c2 07 28	. . (
	pop bc			;5e5f	c1		.
	rrca			;5e60	0f		.
	djnz $-62		;5e61	10 c0		. .
	add a,c			;5e63	81		.
	djnz $-58		;5e64	10 c4		. .
	ld de,0c00bh		;5e66	11 0b c0	. . .
	inc d			;5e69	14		.
	inc c			;5e6a	0c		.
	jp nz,00916h		;5e6b	c2 16 09	. . .
	pop bc			;5e6e	c1		.
	jr $+10			;5e6f	18 08		. .
	call nz,00819h		;5e71	c4 19 08	. . .
	jp nz,0011bh		;5e74	c2 1b 01	. . .
	ret nz			;5e77	c0		.
	add a,l			;5e78	85		.
	djnz $-60		;5e79	10 c2		. .
	ld hl,(0c210h)		;5e7b	2a 10 c2	* . .
	dec e			;5e7e	1d		.
	djnz $-59		;5e7f	10 c3		. .
	jr nz,$+7		;5e81	20 05		  .
	jp 00521h		;5e83	c3 21 05	. ! .
	jp 00a22h		;5e86	c3 22 0a	. " .
	jp 00924h		;5e89	c3 24 09	. $ .
	jp nz,00526h		;5e8c	c2 26 05	. & .
	jp nz,00427h		;5e8f	c2 27 04	. ' .
	pop bc			;5e92	c1		.
	jr z,$+12		;5e93	28 0a		( .
	pop bc			;5e95	c1		.
	ld hl,(0c20bh)		;5e96	2a 0b c2	* . .
	inc l			;5e99	2c		,
	dec bc			;5e9a	0b		.
	jp 00b2eh		;5e9b	c3 2e 0b	. . .
	call nz,00b30h		;5e9e	c4 30 0b	. 0 .
	ret nz			;5ea1	c0		.
	inc sp			;5ea2	33		3
	inc c			;5ea3	0c		.
	jp nz,00d35h		;5ea4	c2 35 0d	. 5 .
	ret nz			;5ea7	c0		.
	jr c,$+14		;5ea8	38 0c		8 .
	jp nz,00a3ah		;5eaa	c2 3a 0a	. : .
	jp nz,0093ch		;5ead	c2 3c 09	. < .
	pop bc			;5eb0	c1		.
	ld a,00bh		;5eb1	3e 0b		> .
	jp nz,00940h		;5eb3	c2 40 09	. @ .
	pop bc			;5eb6	c1		.
	ld b,d			;5eb7	42		B
	ld a,(bc)		;5eb8	0a		.
	pop bc			;5eb9	c1		.
	ld b,h			;5eba	44		D
	dec bc			;5ebb	0b		.
	jp nz,00d46h		;5ebc	c2 46 0d	. F .
	ret nz			;5ebf	c0		.
	ld c,c			;5ec0	49		I
	inc c			;5ec1	0c		.
	jp nz,00b4bh		;5ec2	c2 4b 0b	. K .
	jp 00c4dh		;5ec5	c3 4d 0c	. M .
	ret nz			;5ec8	c0		.
	ld d,b			;5ec9	50		P
	inc c			;5eca	0c		.
	jp nz,00b52h		;5ecb	c2 52 0b	. R .
	jp 00b54h		;5ece	c3 54 0b	. T .
	call nz,00c56h		;5ed1	c4 56 0c	. V .
	pop bc			;5ed4	c1		.
	ld e,c			;5ed5	59		Y
	add hl,bc		;5ed6	09		.
	ret nz			;5ed7	c0		.
	ld e,e			;5ed8	5b		[
	ld b,0c1h		;5ed9	06 c1		. .
	ld e,h			;5edb	5c		\
	ex af,af'		;5edc	08		.
	call nz,0085dh		;5edd	c4 5d 08	. ] .
	jp nz,00a5fh		;5ee0	c2 5f 0a	. _ .
	jp nz,00a61h		;5ee3	c2 61 0a	. a .
	jp nz,00a63h		;5ee6	c2 63 0a	. c .
	jp nz,00a65h		;5ee9	c2 65 0a	. e .
	jp nz,00b67h		;5eec	c2 67 0b	. g .
	jp 00c69h		;5eef	c3 69 0c	. i .
	ret nz			;5ef2	c0		.
	ld l,h			;5ef3	6c		l
	ex af,af'		;5ef4	08		.
	jp 0096dh		;5ef5	c3 6d 09	. m .
	jp nz,00a6fh		;5ef8	c2 6f 0a	. o .
	jp nz,00971h		;5efb	c2 71 09	. q .
	pop bc			;5efe	c1		.
	ld (hl),e		;5eff	73		s
	ld a,(bc)		;5f00	0a		.
	pop bc			;5f01	c1		.
	ld (hl),l		;5f02	75		u
	dec bc			;5f03	0b		.
	jp nz,00b77h		;5f04	c2 77 0b	. w .
	jp 00b79h		;5f07	c3 79 0b	. y .
	call nz,00a7bh		;5f0a	c4 7b 0a	. { .
	call nz,00a7dh		;5f0d	c4 7d 0a	. } .
