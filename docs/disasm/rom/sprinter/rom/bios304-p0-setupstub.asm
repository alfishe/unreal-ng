;===========================================================================
; Peters Plus Sprinter Sp2000 - BIOS 3.04: the SETUP stub
; ROM page 0 #1000-#115E, shown at the address it runs from: RAM #8000.
; ROM page 8 (StartSetupInRam, page 8 #0126) copies page 0 #1000-#3FFF to
; RAM #8000-#AFFF and jumps to #8005.
;===========================================================================
;
; #8000  "SETUP" and a JR over the copyright text
; #8020  copy the depacker to #D000, push #8000 as the return address,
;        JP #D000 with HL = #815F (packed SETUP), DE = #8000
; #803E  Hrust 1.x depacker (assembled for #D000: its own jumps and data
;        pointers are #D0xx). The unpacked SETUP overwrites #8000 on, so
;        the depacker must run from its copy at #D000.
; Same code as BIOS-PP 1273243 SETUP/BSETUP.ASM + DEPACK.ASM.
;===========================================================================

	org 08000h

l815fh:	equ 0x815f

SetupName:

; BLOCK 'SetupName' (start 0x8000 end 0x8005)
	defb 053h		;8000	53		S
	defb 045h		;8001	45		E
	defb 054h		;8002	54		T
	defb 055h		;8003	55		U
	defb 050h		;8004	50		P
SetupStubEntry:
	jr SetupStubStart	;8005	18 19		. .
SetupCopyright:

; BLOCK 'SetupCopyright' (start 0x8007 end 0x8020)
	defb 028h		;8007	28		(
	defb 043h		;8008	43		C
	defb 029h		;8009	29		)
	defb 020h		;800a	20		 
	defb 032h		;800b	32		2
	defb 030h		;800c	30		0
	defb 030h		;800d	30		0
	defb 031h		;800e	31		1
	defb 020h		;800f	20		 
	defb 050h		;8010	50		P
	defb 045h		;8011	45		E
	defb 054h		;8012	54		T
	defb 045h		;8013	45		E
	defb 052h		;8014	52		R
	defb 053h		;8015	53		S
	defb 020h		;8016	20		 
	defb 050h		;8017	50		P
	defb 04ch		;8018	4c		L
	defb 055h		;8019	55		U
	defb 053h		;801a	53		S
	defb 020h		;801b	20		 
	defb 04ch		;801c	4c		L
	defb 054h		;801d	54		T
	defb 044h		;801e	44		D
	defb 020h		;801f	20		 
;---------------------------------------------------------------------------
; SP = #7FFF; the caller's return address and AF are kept on the new
; stack. Push #8000 (where the depacker returns to) and #D000 (where the
; RET below goes), LDIR #21CC bytes from #803E to #D000 (the depacker; the
; count also covers the packed data), HL = #815F = the packed data in the
; #8000 copy, DE = #8000, RET = JP #D000. The depacker first moves the
; packed block up with LDDR so the output growing from #8000 never
; overtakes the input.
;---------------------------------------------------------------------------
SetupStubStart:
	di			;8020	f3		.
	pop hl			;8021	e1		.
	ld sp,07fffh		;8022	31 ff 7f	1 . .
	push hl			;8025	e5		.
	push af			;8026	f5		.
	ld hl,SetupName		;8027	21 00 80	! . .
	push hl			;802a	e5		.
	ld de,0d000h		;802b	11 00 d0	. . .
	push de			;802e	d5		.
	ld hl,HrustDepacker	;802f	21 3e 80	! > .
	ld bc,021cch		;8032	01 cc 21	. . !
	ldir			;8035	ed b0		. .
	ld hl,l815fh	;8037	21 5f 81	! _ .
	ld de,SetupName		;803a	11 00 80	. . .
	ret			;803d	c9		.
HrustDepacker:
	push de			;803e	d5		.
	push hl			;803f	e5		.
	inc hl			;8040	23		#
	inc hl			;8041	23		#
	ld c,(hl)		;8042	4e		N
	inc hl			;8043	23		#
	ld b,(hl)		;8044	46		F
	inc hl			;8045	23		#
	dec bc			;8046	0b		.
	ex de,hl		;8047	eb		.
	add hl,bc		;8048	09		.
	ex de,hl		;8049	eb		.
	ld c,(hl)		;804a	4e		N
	inc hl			;804b	23		#
	ld b,(hl)		;804c	46		F
	dec bc			;804d	0b		.
	pop hl			;804e	e1		.
	add hl,bc		;804f	09		.
	sbc hl,de		;8050	ed 52		. R
	add hl,de		;8052	19		.
	jr c,l8057h		;8053	38 02		8 .
	ld d,h			;8055	54		T
	ld e,l			;8056	5d		]
l8057h:
	lddr			;8057	ed b8		. .
	ex de,hl		;8059	eb		.
	pop de			;805a	d1		.
	ld c,00ch		;805b	0e 0c		. .
	add hl,bc		;805d	09		.
	push hl			;805e	e5		.
	pop ix			;805f	dd e1		. .
	ld a,003h		;8061	3e 03		> .
l8063h:
	dec hl			;8063	2b		+
	ld b,(hl)		;8064	46		F
	dec hl			;8065	2b		+
	ld c,(hl)		;8066	4e		N
	push bc			;8067	c5		.
	dec a			;8068	3d		=
	jr nz,l8063h		;8069	20 f8		  .
	ld b,a			;806b	47		G
	exx			;806c	d9		.
	ld d,0bfh		;806d	16 bf		. .
	ld c,010h		;806f	0e 10		. .
	call 0d115h		;8071	cd 15 d1	. . .
l8074h:
	ld a,(ix+000h)		;8074	dd 7e 00	. ~ .
	inc ix			;8077	dd 23		. #
	exx			;8079	d9		.
	ld (de),a		;807a	12		.
	inc de			;807b	13		.
l807ch:
	exx			;807c	d9		.
l807dh:
	add hl,hl		;807d	29		)
	djnz l8083h		;807e	10 03		. .
	call 0d115h		;8080	cd 15 d1	. . .
l8083h:
	jr c,l8074h		;8083	38 ef		8 .
	ld e,001h		;8085	1e 01		. .
l8087h:
	ld a,080h		;8087	3e 80		> .
l8089h:
	add hl,hl		;8089	29		)
	djnz l808fh		;808a	10 03		. .
	call 0d115h		;808c	cd 15 d1	. . .
l808fh:
	rla			;808f	17		.
	jr c,l8089h		;8090	38 f7		8 .
	cp 003h			;8092	fe 03		. .
	jr c,l809bh		;8094	38 05		8 .
	add a,e			;8096	83		.
	ld e,a			;8097	5f		_
	xor c			;8098	a9		.
	jr nz,l8087h		;8099	20 ec		  .
l809bh:
	add a,e			;809b	83		.
	cp 004h			;809c	fe 04		. .
	jr z,l8102h		;809e	28 62		( b
	adc a,0ffh		;80a0	ce ff		. .
	cp 002h			;80a2	fe 02		. .
	exx			;80a4	d9		.
l80a5h:
	ld c,a			;80a5	4f		O
l80a6h:
	exx			;80a6	d9		.
	ld a,0bfh		;80a7	3e bf		> .
	jr c,l80c0h		;80a9	38 15		8 .
l80abh:
	add hl,hl		;80ab	29		)
	djnz l80b1h		;80ac	10 03		. .
	call 0d115h		;80ae	cd 15 d1	. . .
l80b1h:
	rla			;80b1	17		.
	jr c,l80abh		;80b2	38 f7		8 .
	jr z,l80bbh		;80b4	28 05		( .
	inc a			;80b6	3c		<
	add a,d			;80b7	82		.
	jr nc,l80c2h		;80b8	30 08		0 .
	sub d			;80ba	92		.
l80bbh:
	inc a			;80bb	3c		<
	jr nz,l80cbh		;80bc	20 0d		  .
	ld a,0efh		;80be	3e ef		> .
l80c0h:
	rrca			;80c0	0f		.
	cp a			;80c1	bf		.
l80c2h:
	add hl,hl		;80c2	29		)
	djnz l80c8h		;80c3	10 03		. .
	call 0d115h		;80c5	cd 15 d1	. . .
l80c8h:
	rla			;80c8	17		.
	jr c,l80c2h		;80c9	38 f7		8 .
l80cbh:
	exx			;80cb	d9		.
	ld h,0ffh		;80cc	26 ff		& .
	jr z,l80d9h		;80ce	28 09		( .
	ld h,a			;80d0	67		g
	inc a			;80d1	3c		<
	ld a,(ix+000h)		;80d2	dd 7e 00	. ~ .
	inc ix			;80d5	dd 23		. #
	jr z,l80e4h		;80d7	28 0b		( .
l80d9h:
	ld l,a			;80d9	6f		o
	add hl,de		;80da	19		.
	ldir			;80db	ed b0		. .
l80ddh:
	jr l807ch		;80dd	18 9d		. .
l80dfh:
	exx			;80df	d9		.
	rrc d			;80e0	cb 0a		. .
	jr l807dh		;80e2	18 99		. .
l80e4h:
	cp 0e0h			;80e4	fe e0		. .
	jr c,l80d9h		;80e6	38 f1		8 .
	rlca			;80e8	07		.
	xor c			;80e9	a9		.
	inc a			;80ea	3c		<
	jr z,l80dfh		;80eb	28 f2		( .
	sub 010h		;80ed	d6 10		. .
l80efh:
	ld l,a			;80ef	6f		o
	ld c,a			;80f0	4f		O
	ld h,0ffh		;80f1	26 ff		& .
	add hl,de		;80f3	19		.
	ldi			;80f4	ed a0		. .
	ld a,(ix+000h)		;80f6	dd 7e 00	. ~ .
	inc ix			;80f9	dd 23		. #
	ld (de),a		;80fb	12		.
	inc hl			;80fc	23		#
	inc de			;80fd	13		.
	ld a,(hl)		;80fe	7e		~
	jp 0d03ch		;80ff	c3 3c d0	. < .
l8102h:
	ld a,080h		;8102	3e 80		> .
l8104h:
	add hl,hl		;8104	29		)
	djnz l810ah		;8105	10 03		. .
	call 0d115h		;8107	cd 15 d1	. . .
l810ah:
	adc a,a			;810a	8f		.
	jr nz,l8131h		;810b	20 24		  $
	jr c,l8104h		;810d	38 f5		8 .
	ld a,0fch		;810f	3e fc		> .
	jr l8134h		;8111	18 21		. !
l8113h:
	ld b,a			;8113	47		G
	ld c,(ix+000h)		;8114	dd 4e 00	. N .
	inc ix			;8117	dd 23		. #
	ccf			;8119	3f		?
	jr l80a6h		;811a	18 8a		. .
l811ch:
	cp 00fh			;811c	fe 0f		. .
	jr c,l8113h		;811e	38 f3		8 .
	jr nz,l80a5h		;8120	20 83		  .
	ld b,003h		;8122	06 03		. .
	ex de,hl		;8124	eb		.
l8125h:
	pop de			;8125	d1		.
	ld (hl),e		;8126	73		s
	inc hl			;8127	23		#
	ld (hl),d		;8128	72		r
	inc hl			;8129	23		#
	djnz l8125h		;812a	10 f9		. .
	ld hl,02758h		;812c	21 58 27	! X '
	exx			;812f	d9		.
	ret			;8130	c9		.
l8131h:
	sbc a,a			;8131	9f		.
	ld a,0efh		;8132	3e ef		> .
l8134h:
	add hl,hl		;8134	29		)
	djnz l813ah		;8135	10 03		. .
	call 0d115h		;8137	cd 15 d1	. . .
l813ah:
	rla			;813a	17		.
	jr c,l8134h		;813b	38 f7		8 .
	exx			;813d	d9		.
	jr nz,l80efh		;813e	20 af		  .
	bit 7,a			;8140	cb 7f		. .
	jr z,l811ch		;8142	28 d8		( .
	sub 0eah		;8144	d6 ea		. .
	add a,a			;8146	87		.
	ld b,a			;8147	47		G
l8148h:
	ld a,(ix+000h)		;8148	dd 7e 00	. ~ .
	inc ix			;814b	dd 23		. #
	ld (de),a		;814d	12		.
	inc de			;814e	13		.
	djnz l8148h		;814f	10 f7		. .
	jr l80ddh		;8151	18 8a		. .

; BLOCK 'Data8153' (start 0x8153 end 0x815f)
Data8153:
	defb 041h		;8153	41		A
	defb 0ddh		;8154	dd		.
	defb 06eh		;8155	6e		n
	defb 000h		;8156	00		.
	defb 0ddh		;8157	dd		.
	defb 023h		;8158	23		#
	defb 0ddh		;8159	dd		.
	defb 066h		;815a	66		f
	defb 000h		;815b	00		.
	defb 0ddh		;815c	dd		.
	defb 023h		;815d	23		#
	defb 0c9h		;815e	c9		.
l815fh:
