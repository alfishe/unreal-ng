; z80dasm 1.2.0
; command line: z80dasm -a -t -g 0x5F00 -o scratch/voron1-loader.asm scratch/voron1-ram/loader-5F00.bin

	org 05f00h

	ld a,(bc)		;5f00	0a		.
	pop bc			;5f01	c1		.
	ld (hl),l		;5f02	75		u
	dec bc			;5f03	0b		.
	jp nz,00b77h		;5f04	c2 77 0b	. w .
	jp 00b79h		;5f07	c3 79 0b	. y .
	call nz,00a7bh		;5f0a	c4 7b 0a	. { .
	call nz,00a7dh		;5f0d	c4 7d 0a	. } .
	di			;5f10	f3		.
	im 1			;5f11	ed 56		. V
	ld sp,061feh		;5f13	31 fe 61	1 . a
	call 060c3h		;5f16	cd c3 60	. . `
	call 05de8h		;5f19	cd e8 5d	. . ]
	ld hl,0c000h		;5f1c	21 00 c0	! . .
	ld de,0c001h		;5f1f	11 01 c0	. . .
	ld bc,05000h		;5f22	01 00 50	. . P
	ld (hl),0a4h		;5f25	36 a4		6 .
	ldir			;5f27	ed b0		. .
	call 060c7h		;5f29	cd c7 60	. . `
	ld a,(05cf6h)		;5f2c	3a f6 5c	: . \
	ld (06049h),a		;5f2f	32 49 60	2 I `
	ld a,009h		;5f32	3e 09		> .
	out (0feh),a		;5f34	d3 fe		. .
	call 05df8h		;5f36	cd f8 5d	. . ]
	call 05debh		;5f39	cd eb 5d	. . ]
	ld a,001h		;5f3c	3e 01		> .
	call 06094h		;5f3e	cd 94 60	. . `
	ld de,0c000h		;5f41	11 00 c0	. . .
	ld hl,0db02h		;5f44	21 02 db	! . .
	call 05d4dh		;5f47	cd 4d 5d	. M ]
	call 05e1ah		;5f4a	cd 1a 5e	. . ^
	ld bc,0bffeh		;5f4d	01 fe bf	. . .
	in a,(c)		;5f50	ed 78		. x
	rra			;5f52	1f		.
	jr nc,$+42		;5f53	30 28		0 (
	ld a,003h		;5f55	3e 03		> .
	call 05dedh		;5f57	cd ed 5d	. . ]
	ld a,002h		;5f5a	3e 02		> .
	call 05f74h		;5f5c	cd 74 5f	. t _
	ld a,004h		;5f5f	3e 04		> .
	call 05dedh		;5f61	cd ed 5d	. . ]
	ld a,003h		;5f64	3e 03		> .
	call 05f74h		;5f66	cd 74 5f	. t _
	call 05de8h		;5f69	cd e8 5d	. . ]
	ld a,004h		;5f6c	3e 04		> .
	call 06094h		;5f6e	cd 94 60	. . `
	call 06200h		;5f71	cd 00 62	. . b
	call 06094h		;5f74	cd 94 60	. . `
	ld hl,0c000h		;5f77	21 00 c0	! . .
	jp 05dd8h		;5f7a	c3 d8 5d	. . ]
	di			;5f7d	f3		.
	call 05e1ah		;5f7e	cd 1a 5e	. . ^
	xor a			;5f81	af		.
	call 05dedh		;5f82	cd ed 5d	. . ]
	ld a,006h		;5f85	3e 06		> .
	call 06094h		;5f87	cd 94 60	. . `
	ld a,001h		;5f8a	3e 01		> .
	call 05dedh		;5f8c	cd ed 5d	. . ]
	ld a,007h		;5f8f	3e 07		> .
	call 06094h		;5f91	cd 94 60	. . `
	ld a,002h		;5f94	3e 02		> .
	call 05dedh		;5f96	cd ed 5d	. . ]
	ld a,008h		;5f99	3e 08		> .
	call 06094h		;5f9b	cd 94 60	. . `
	call 05df7h		;5f9e	cd f7 5d	. . ]
	call 05e16h		;5fa1	cd 16 5e	. . ^
	xor a			;5fa4	af		.
	out (0feh),a		;5fa5	d3 fe		. .
	call 05debh		;5fa7	cd eb 5d	. . ]
	ld a,009h		;5faa	3e 09		> .
	call 05f74h		;5fac	cd 74 5f	. t _
	call 05e1ah		;5faf	cd 1a 5e	. . ^
	ld a,003h		;5fb2	3e 03		> .
	call 05dedh		;5fb4	cd ed 5d	. . ]
	ld a,00ah		;5fb7	3e 0a		> .
	call 06094h		;5fb9	cd 94 60	. . `
	ld a,004h		;5fbc	3e 04		> .
	call 05dedh		;5fbe	cd ed 5d	. . ]
	ld a,00bh		;5fc1	3e 0b		> .
	call 06094h		;5fc3	cd 94 60	. . `
	ld a,005h		;5fc6	3e 05		> .
	call 05dedh		;5fc8	cd ed 5d	. . ]
	ld a,00ch		;5fcb	3e 0c		> .
	call 06094h		;5fcd	cd 94 60	. . `
	ld a,006h		;5fd0	3e 06		> .
	call 05dedh		;5fd2	cd ed 5d	. . ]
	ld a,00dh		;5fd5	3e 0d		> .
	call 06094h		;5fd7	cd 94 60	. . `
	di			;5fda	f3		.
	ld b,005h		;5fdb	06 05		. .
	push bc			;5fdd	c5		.
	ld a,b			;5fde	78		x
	nop			;5fdf	00		.
	call 05dedh		;5fe0	cd ed 5d	. . ]
	ld hl,0c000h		;5fe3	21 00 c0	! . .
	call 05dd8h		;5fe6	cd d8 5d	. . ]
	pop bc			;5fe9	c1		.
	djnz $-13		;5fea	10 f1		. .
	ld a,006h		;5fec	3e 06		> .
	call 05dedh		;5fee	cd ed 5d	. . ]
	call 0fe00h		;5ff1	cd 00 fe	. . .
	ld (hl),b		;5ff4	70		p
	in a,(0c0h)		;5ff5	db c0		. .
	ret nz			;5ff7	c0		.
	ld h,d			;5ff8	62		b
	ret nz			;5ff9	c0		.
	ret nz			;5ffa	c0		.
	ret nz			;5ffb	c0		.
	ret nz			;5ffc	c0		.
	ret nz			;5ffd	c0		.
	ret nz			;5ffe	c0		.
	ret nz			;5fff	c0		.
	ret nz			;6000	c0		.
	ret nz			;6001	c0		.
	ret nz			;6002	c0		.
	add a,b			;6003	80		.
	di			;6004	f3		.
	call 0604ah		;6005	cd 4a 60	. J `
	ld a,(05cd6h)		;6008	3a d6 5c	: . \
	ex af,af'		;600b	08		.
	defb 0ddh,02eh,003h ;ld ixl,003h	;600c	dd 2e 03	. . .
	push hl			;600f	e5		.
	push bc			;6010	c5		.
	push ix			;6011	dd e5		. .
	ld c,05fh		;6013	0e 5f		. _
	ld a,e			;6015	7b		{
	call 0607bh		;6016	cd 7b 60	. { `
	call 0606eh		;6019	cd 6e 60	. n `
	di			;601c	f3		.
	ld hl,05cd6h		;601d	21 d6 5c	! . \
	ex af,af'		;6020	08		.
	cp (hl)			;6021	be		.
	pop ix			;6022	dd e1		. .
	jr z,$+13		;6024	28 0b		( .
	ld (hl),a		;6026	77		w
	defb 0ddh,02dh ;dec ixl	;6027	dd 2d		. -
	pop bc			;6029	c1		.
	pop hl			;602a	e1		.
	jp z,0608ah		;602b	ca 8a 60	. . `
	ex af,af'		;602e	08		.
	jr $-32			;602f	18 de		. .
	pop bc			;6031	c1		.
	pop hl			;6032	e1		.
	ex af,af'		;6033	08		.
	inc e			;6034	1c		.
	ld a,e			;6035	7b		{
	cp 0c5h			;6036	fe c5		. .
	jr c,$+8		;6038	38 06		8 .
	ld e,0c0h		;603a	1e c0		. .
	inc d			;603c	14		.
	call 0604ah		;603d	cd 4a 60	. J `
	inc h			;6040	24		$
	inc h			;6041	24		$
	inc h			;6042	24		$
	inc h			;6043	24		$
	djnz $-56		;6044	10 c6		. .
	di			;6046	f3		.
	xor a			;6047	af		.
	ret			;6048	c9		.
	nop			;6049	00		.
	ld a,(06049h)		;604a	3a 49 60	: I `
	add a,03ch		;604d	c6 3c		. <
	bit 0,d			;604f	cb 42		. B
	jr z,$+4		;6051	28 02		( .
	res 4,a			;6053	cb a7		. .
	ld c,0ffh		;6055	0e ff		. .
	call 0607bh		;6057	cd 7b 60	. { `
	ld a,d			;605a	7a		z
	srl a			;605b	cb 3f		. ?
	ld c,07fh		;605d	0e 7f		. .
	call 0607bh		;605f	cd 7b 60	. { `
	ld a,018h		;6062	3e 18		> .
	ld c,01fh		;6064	0e 1f		. .
	call 0607bh		;6066	cd 7b 60	. { `
	call 06081h		;6069	cd 81 60	. . `
	di			;606c	f3		.
	ret			;606d	c9		.
	ld bc,0606eh		;606e	01 6e 60	. n `
	push bc			;6071	c5		.
	ld bc,0017fh		;6072	01 7f 01	. . .
	ld ix,02090h		;6075	dd 21 90 20	. ! .  
	jr $+12			;6079	18 0a		. .
	ld ix,02a53h		;607b	dd 21 53 2a	. ! S *
	jr $+6			;607f	18 04		. .
	ld ix,03ef5h		;6081	dd 21 f5 3e	. ! . >
	push ix			;6085	dd e5		. .
	jp 03d2fh		;6087	c3 2f 3d	. / =
	scf			;608a	37		7
	ret			;608b	c9		.
	rlca			;608c	07		.
	add a,l			;608d	85		.
	ld l,a			;608e	6f		o
	jr nc,$+3		;608f	30 01		0 .
	inc h			;6091	24		$
	ld a,(hl)		;6092	7e		~
	ret			;6093	c9		.
	push af			;6094	f5		.
	ld d,a			;6095	57		W
	add a,a			;6096	87		.
	add a,d			;6097	82		.
	ld hl,05e50h		;6098	21 50 5e	! P ^
	call 0608dh		;609b	cd 8d 60	. . `
	ld e,(hl)		;609e	5e		^
	inc hl			;609f	23		#
	ld d,(hl)		;60a0	56		V
	inc hl			;60a1	23		#
	ld b,(hl)		;60a2	46		F
	pop af			;60a3	f1		.
	ld h,0c0h		;60a4	26 c0		& .
	cp 010h			;60a6	fe 10		. .
	jr nc,$+9		;60a8	30 07		0 .
	ld hl,05ff4h		;60aa	21 f4 5f	! . _
	call 0608dh		;60ad	cd 8d 60	. . `
	ld h,a			;60b0	67		g
	xor a			;60b1	af		.
	ld l,a			;60b2	6f		o
	call 06004h		;60b3	cd 04 60	. . `
	ret nc			;60b6	d0		.
	di			;60b7	f3		.
	call 05e41h		;60b8	cd 41 5e	. A ^
	call 05e41h		;60bb	cd 41 5e	. A ^
	call 05e41h		;60be	cd 41 5e	. A ^
	jr $-14			;60c1	18 f0		. .
	ld a,010h		;60c3	3e 10		> .
	jr $+3			;60c5	18 01		. .
	xor a			;60c7	af		.
	ld bc,01ffdh		;60c8	01 fd 1f	. . .
	out (c),a		;60cb	ed 79		. y
	ret			;60cd	c9		.
	di			;60ce	f3		.
	ld (06049h),a		;60cf	32 49 60	2 I `
	push bc			;60d2	c5		.
	ld d,000h		;60d3	16 00		. .
	call 0604ah		;60d5	cd 4a 60	. J `
	call 05e41h		;60d8	cd 41 5e	. A ^
	call 05e41h		;60db	cd 41 5e	. A ^
	pop af			;60de	f1		.
	push af			;60df	f5		.
	or a			;60e0	b7		.
	ld a,034h		;60e1	3e 34		> 4
	jr z,$+4		;60e3	28 02		( .
	ld a,03ah		;60e5	3e 3a		> :
	ld c,0ffh		;60e7	0e ff		. .
	call 060feh		;60e9	cd fe 60	. . `
	call 060feh		;60ec	cd fe 60	. . `
	call 060feh		;60ef	cd fe 60	. . `
	ld a,00fh		;60f2	3e 0f		> .
	call 06094h		;60f4	cd 94 60	. . `
	pop af			;60f7	f1		.
	ld hl,08000h		;60f8	21 00 80	! . .
	ld (hl),a		;60fb	77		w
	inc hl			;60fc	23		#
	jp (hl)			;60fd	e9		.
	inc c			;60fe	0c		.
	call 06102h		;60ff	cd 02 61	. . a
	push bc			;6102	c5		.
	push af			;6103	f5		.
	ld a,c			;6104	79		y
	call 05dedh		;6105	cd ed 5d	. . ]
	pop af			;6108	f1		.
	push af			;6109	f5		.
	call 06094h		;610a	cd 94 60	. . `
	pop af			;610d	f1		.
	pop bc			;610e	c1		.
	inc c			;610f	0c		.
	inc a			;6110	3c		<
	ret			;6111	c9		.
	ld a,(bc)		;6112	0a		.
	ld c,0e8h		;6113	0e e8		. .
	adc a,(hl)		;6115	8e		.
	call 0ffb8h		;6116	cd b8 ff	. . .
	rst 38h			;6119	ff		.
	rra			;611a	1f		.
	ret			;611b	c9		.
	rrc (hl)		;611c	cb 0e		. .
	ret pe			;611e	e8		.
	ld d,h			;611f	54		T
	rrc d			;6120	cb 0a		. .
	ret nz			;6122	c0		.
	ld (hl),h		;6123	74		t
	rrca			;6124	0f		.
	nop			;6125	00		.
	nop			;6126	00		.
	nop			;6127	00		.
	nop			;6128	00		.
	nop			;6129	00		.
	nop			;612a	00		.
	nop			;612b	00		.
	nop			;612c	00		.
	nop			;612d	00		.
	nop			;612e	00		.
	nop			;612f	00		.
	nop			;6130	00		.
	nop			;6131	00		.
	nop			;6132	00		.
	nop			;6133	00		.
	nop			;6134	00		.
	nop			;6135	00		.
	nop			;6136	00		.
	nop			;6137	00		.
	nop			;6138	00		.
	nop			;6139	00		.
	nop			;613a	00		.
	nop			;613b	00		.
	nop			;613c	00		.
	nop			;613d	00		.
	nop			;613e	00		.
	nop			;613f	00		.
	nop			;6140	00		.
	nop			;6141	00		.
	nop			;6142	00		.
	nop			;6143	00		.
	nop			;6144	00		.
	nop			;6145	00		.
	nop			;6146	00		.
	nop			;6147	00		.
	nop			;6148	00		.
	nop			;6149	00		.
	nop			;614a	00		.
	nop			;614b	00		.
	nop			;614c	00		.
	nop			;614d	00		.
	nop			;614e	00		.
	nop			;614f	00		.
	nop			;6150	00		.
	nop			;6151	00		.
	nop			;6152	00		.
	nop			;6153	00		.
	nop			;6154	00		.
	nop			;6155	00		.
	nop			;6156	00		.
	nop			;6157	00		.
	nop			;6158	00		.
	nop			;6159	00		.
	nop			;615a	00		.
	nop			;615b	00		.
	nop			;615c	00		.
	nop			;615d	00		.
	nop			;615e	00		.
	nop			;615f	00		.
	nop			;6160	00		.
	nop			;6161	00		.
	nop			;6162	00		.
	nop			;6163	00		.
	nop			;6164	00		.
	nop			;6165	00		.
	nop			;6166	00		.
	nop			;6167	00		.
	nop			;6168	00		.
	nop			;6169	00		.
	nop			;616a	00		.
	nop			;616b	00		.
	nop			;616c	00		.
	nop			;616d	00		.
	nop			;616e	00		.
	nop			;616f	00		.
	nop			;6170	00		.
	nop			;6171	00		.
	nop			;6172	00		.
	nop			;6173	00		.
	nop			;6174	00		.
	nop			;6175	00		.
	nop			;6176	00		.
	nop			;6177	00		.
	nop			;6178	00		.
	nop			;6179	00		.
	nop			;617a	00		.
	nop			;617b	00		.
	nop			;617c	00		.
	nop			;617d	00		.
	nop			;617e	00		.
	nop			;617f	00		.
	nop			;6180	00		.
	nop			;6181	00		.
	nop			;6182	00		.
	nop			;6183	00		.
	nop			;6184	00		.
	nop			;6185	00		.
	nop			;6186	00		.
	nop			;6187	00		.
	nop			;6188	00		.
	nop			;6189	00		.
	nop			;618a	00		.
	nop			;618b	00		.
	nop			;618c	00		.
	nop			;618d	00		.
	nop			;618e	00		.
	nop			;618f	00		.
	nop			;6190	00		.
	nop			;6191	00		.
	nop			;6192	00		.
	nop			;6193	00		.
	nop			;6194	00		.
	nop			;6195	00		.
	nop			;6196	00		.
	nop			;6197	00		.
	nop			;6198	00		.
	nop			;6199	00		.
	nop			;619a	00		.
	nop			;619b	00		.
	nop			;619c	00		.
	nop			;619d	00		.
	nop			;619e	00		.
	nop			;619f	00		.
	nop			;61a0	00		.
	nop			;61a1	00		.
	nop			;61a2	00		.
	nop			;61a3	00		.
	nop			;61a4	00		.
	nop			;61a5	00		.
	nop			;61a6	00		.
	nop			;61a7	00		.
	nop			;61a8	00		.
	nop			;61a9	00		.
	nop			;61aa	00		.
	nop			;61ab	00		.
	nop			;61ac	00		.
	nop			;61ad	00		.
	nop			;61ae	00		.
	nop			;61af	00		.
	nop			;61b0	00		.
	nop			;61b1	00		.
	nop			;61b2	00		.
	nop			;61b3	00		.
	nop			;61b4	00		.
	nop			;61b5	00		.
	nop			;61b6	00		.
	nop			;61b7	00		.
	nop			;61b8	00		.
	nop			;61b9	00		.
	nop			;61ba	00		.
	nop			;61bb	00		.
	nop			;61bc	00		.
	nop			;61bd	00		.
	nop			;61be	00		.
	nop			;61bf	00		.
	nop			;61c0	00		.
	nop			;61c1	00		.
	nop			;61c2	00		.
	nop			;61c3	00		.
	nop			;61c4	00		.
	nop			;61c5	00		.
	nop			;61c6	00		.
	nop			;61c7	00		.
	nop			;61c8	00		.
	nop			;61c9	00		.
	nop			;61ca	00		.
	nop			;61cb	00		.
	nop			;61cc	00		.
	nop			;61cd	00		.
	nop			;61ce	00		.
	nop			;61cf	00		.
	nop			;61d0	00		.
	nop			;61d1	00		.
	nop			;61d2	00		.
	nop			;61d3	00		.
	nop			;61d4	00		.
	nop			;61d5	00		.
	nop			;61d6	00		.
	nop			;61d7	00		.
	nop			;61d8	00		.
	nop			;61d9	00		.
	nop			;61da	00		.
	nop			;61db	00		.
	nop			;61dc	00		.
	nop			;61dd	00		.
	nop			;61de	00		.
	nop			;61df	00		.
	nop			;61e0	00		.
	nop			;61e1	00		.
	nop			;61e2	00		.
	nop			;61e3	00		.
	nop			;61e4	00		.
	nop			;61e5	00		.
	nop			;61e6	00		.
	nop			;61e7	00		.
	nop			;61e8	00		.
	nop			;61e9	00		.
	nop			;61ea	00		.
	nop			;61eb	00		.
