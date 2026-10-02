; CDPLAYER.FLX (Flex Navigator plugin, A. Shabarshin 2002, "OSHAOS CD-Player v1.0 beta1"):
; the IDE / ATAPI routines and the packet table, C1BCh-C35Dh. z80dasm 1.x output, origin BFF0h
; (file offset 0 = BFF0h: the code calls C1BCh at file offset 1CCh). Source file: C:\FN\FLX\cdplayer.flx
; of the MAME-pack sp_hdd_sys image (12 732 bytes, SHA-1 144400e4a1d53702c3a18e13c5092f5aeb35bcdd).
; Notes in README.md. Ports (the BIOS port table): #0050 / #xx50 data (A8 = 0: low byte / latch,
; A8 = 1: word), #0154 / #0155 byte count low / high (write), #4152 device, #4052 / #4053 read
; device / status, #4153 command.
lc1bch:
	ld bc,04053h		;c1bc
	in a,(c)		;c1bf
	rlca			;c1c1
	ret nc			;c1c2
	jr lc1bch		;c1c3
lc1c5h:
	ld bc,04053h		;c1c5
	in a,(c)		;c1c8
	bit 3,a			;c1ca
	ret nz			;c1cc
	jr lc1c5h		;c1cd
sub_c1cfh:
	ld bc,04053h		;c1cf
	in a,(c)		;c1d2
	rrca			;c1d4
	ret			;c1d5
sub_c1d6h:
	ld a,0b0h		;c1d6
	jr lc1deh		;c1d8
	ld a,0a0h		;c1da
	jr lc1deh		;c1dc
lc1deh:
	ld bc,04152h		;c1de
	out (c),a		;c1e1
	ld bc,04052h		;c1e3
	in a,(c)		;c1e6
	rlca			;c1e8
	ret			;c1e9
sub_c1eah:
	ld bc,00154h		;c1ea
	out (c),l		;c1ed
	ld bc,00155h		;c1ef
	out (c),h		;c1f2
	ret			;c1f4
sub_c1f5h:
	ld bc,00154h		;c1f5
	in l,(c)		;c1f8
	ld bc,00155h		;c1fa
	in h,(c)		;c1fd
	ret			;c1ff
sub_c200h:
	call lc1bch		;c200
	call lc1c5h		;c203
	ld hl,lc35eh		;c206
	ld bc,00050h		;c209
	ld d,000h		;c20c
lc20eh:
	ini			;c20e
	ini			;c210
	ini			;c212
	ini			;c214
	ini			;c216
	ini			;c218
	ini			;c21a
	ini			;c21c
	dec d			;c21e
	jr nz,lc20eh		;c21f
	ld hl,lc35eh		;c221
	ld bc,00400h		;c224
lc227h:
	ld d,(hl)		;c227
	inc hl			;c228
	ld e,(hl)		;c229
	dec hl			;c22a
	ld (hl),e		;c22b
	inc hl			;c22c
	ld (hl),d		;c22d
	inc hl			;c22e
	dec bc			;c22f
	ld a,b			;c230
	or c			;c231
	jr nz,lc227h		;c232
	ret			;c234
sub_c235h:
	ld de,lc35eh		;c235
	di			;c238
	ld d,d			;c239
	ld a,00ch		;c23a
	ld l,l			;c23c
	ld a,(hl)		;c23d
	ld (de),a		;c23e
	ld b,b			;c23f
	ei			;c240
lc241h:
	call lc1bch		;c241
	call sub_c1d6h		;c244
	call lc1bch		;c247
lc24ah:
	ld bc,04053h		;c24a
	in a,(c)		;c24d
	and 040h		;c24f
	jr z,lc24ah		;c251
	ld a,0a0h		;c253
	ld bc,04153h		;c255
	out (c),a		;c258
	call lc1bch		;c25a
	call sub_c1cfh		;c25d
	jp c,lc241h		;c260
	call lc1c5h		;c263
	ld hl,lc35eh		;c266
	ld bc,00150h		;c269
	outi			;c26c
	outi			;c26e
	outi			;c270
	outi			;c272
	outi			;c274
	outi			;c276
	outi			;c278
	outi			;c27a
	outi			;c27c
	outi			;c27e
	outi			;c280
	outi			;c282
	ret			;c284
sub_c285h:
	ld de,0ffffh		;c285
	call sub_c1d6h		;c288
	ld bc,04053h		;c28b
	in a,(c)		;c28e
	rlca			;c290
	call c,sub_c2c5h	;c291
	ld hl,00000h		;c294
	call sub_c1eah		;c297
	ld a,0ech		;c29a
	ld bc,04153h		;c29c
	out (c),a		;c29f
	call lc1bch		;c2a1
	call sub_c1f5h		;c2a4
	push hl			;c2a7
	pop de			;c2a8
	ld bc,leb14h		;c2a9
	or a			;c2ac
	sbc hl,bc		;c2ad
	ld a,0a1h		;c2af
	ld bc,04153h		;c2b1
	out (c),a		;c2b4
	call sub_c200h		;c2b6
	ld hl,lc35eh		;c2b9
	ld a,(hl)		;c2bc
	and 01fh		;c2bd
	cp 005h			;c2bf
	jr nz,sub_c2c5h		;c2c1
	scf			;c2c3
	ret			;c2c4
sub_c2c5h:
	pop hl			;c2c5
	xor a			;c2c6
	ret			;c2c7
sub_c2c8h:
	push hl			;c2c8
	ld hl,lc2e7h		;c2c9
	xor a			;c2cc
	ld c,00ah		;c2cd
	rst 10h			;c2cf
	ld (lc2f2h),a		;c2d0
	ld a,(lc2f2h)		;c2d3
	ld de,00800h		;c2d6
	ld hl,lc35eh		;c2d9
	ld c,014h		;c2dc
	rst 10h			;c2de
	ld a,(lc2f2h)		;c2df
	ld c,012h		;c2e2
	rst 10h			;c2e4
	pop hl			;c2e5
	ret			;c2e6
lc2e7h:
	ld h,e			;c2e7
	ld h,h			;c2e8
	ld e,a			;c2e9
	ld h,d			;c2ea
	ld (hl),l		;c2eb
	ld h,(hl)		;c2ec
	ld l,064h		;c2ed
	ld h,c			;c2ef
	ld (hl),h		;c2f0
	nop			;c2f1
lc2f2h:
	nop			;c2f2
	ld hl,lc2feh		;c2f3
	call sub_c235h		;c2f6
	ret			;c2f9
sub_c2fah:
	call sub_c235h		;c2fa
	ret			;c2fd
lc2feh:
	nop			;c2fe
	nop			;c2ff
	nop			;c300
	nop			;c301
	nop			;c302
	nop			;c303
	nop			;c304
	nop			;c305
	nop			;c306
	nop			;c307
	nop			;c308
	nop			;c309
	ld bc,00000h		;c30a
	nop			;c30d
	nop			;c30e
	nop			;c30f
	nop			;c310
	nop			;c311
	nop			;c312
	nop			;c313
	nop			;c314
	nop			;c315
	dec de			;c316
	nop			;c317
	nop			;c318
	nop			;c319
	ld bc,00000h		;c31a
	nop			;c31d
	nop			;c31e
	nop			;c31f
	nop			;c320
	nop			;c321
lc322h:
	dec de			;c322
	nop			;c323
	nop			;c324
	nop			;c325
	ld (bc),a		;c326
	nop			;c327
	nop			;c328
	nop			;c329
	nop			;c32a
	nop			;c32b
	nop			;c32c
	nop			;c32d
	dec de			;c32e
	nop			;c32f
	nop			;c330
	nop			;c331
	inc bc			;c332
	nop			;c333
	nop			;c334
	nop			;c335
	nop			;c336
	nop			;c337
	nop			;c338
	nop			;c339
	ld c,e			;c33a
	nop			;c33b
	nop			;c33c
	nop			;c33d
	nop			;c33e
	nop			;c33f
	nop			;c340
	nop			;c341
	nop			;c342
	nop			;c343
	nop			;c344
	nop			;c345
	ld c,e			;c346
	nop			;c347
	nop			;c348
	nop			;c349
	nop			;c34a
	nop			;c34b
	nop			;c34c
	nop			;c34d
	ld bc,00000h		;c34e
	nop			;c351
lc352h:
	ld b,a			;c352
	nop			;c353
	nop			;c354
	nop			;c355
	ld (bc),a		;c356
	nop			;c357
	ld d,b			;c358
	nop			;c359
	ld c,d			;c35a
	nop			;c35b
	nop			;c35c
	nop			;c35d
lc35eh:
