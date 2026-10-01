;===========================================================================
; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)
; SETUP: the boot program and the setup menu, 13 893 bytes, run at #8000
; Source bytes: ROM page 0 #115F-#3209, packed with Hrust 1.x;
; unpacked by scripts/extract.py (scripts/hrust.py) exactly as the
; SETUP stub at page 0 #1000 does it.
;===========================================================================
;
; What SETUP does (details and the step-by-step boot: rom/README.md):
;   keyboard and interrupts on, CMOS settings read (defaults when the
;   checksum is bad), memory cleared, the boot screen (logo, version,
;   memory sizes, CMOS clock), floppy and IDE detection, DEL = setup menu,
;   then the boot from the CMOS boot device: LBA 1 of that device must
;   start with "Starting..." + #00; it is copied to #8000 and run at #800C.
;
; Addresses #8000-#B644 are RAM (window 2). JP #800C and the use of
; #7C00/#7E00 refer to the loaded boot sector, not to this image.
;
; Labels: hand names (dict_setup.py) and names carried by byte pattern
; from BIOS-PP 1273243 DSETUP.ASM and its includes, shown as
; "; = NAME (src: FILE:LINE)", "~" = short-window match (less certain).
;===========================================================================

	org 08000h

lb645h:	equ 0xb645
;---------------------------------------------------------------------------
; Entry from the depacker (it RETs to #8000): the stub pushed AF and the
; return address; SP = #80F0 (stack in #8040-#80EF), then SetupStart.
;---------------------------------------------------------------------------

SetupEntry:
	pop af			;8000	f1		.
	pop hl			;8001	e1		.
	ld sp,l80f0h		;8002	31 f0 80	1 . .
	push hl			;8005	e5		.
	jp SetupStart		;8006	c3 be 81	. . .

; BLOCK 'CopyrightText' (start 0x8009 end 0x8022)
CopyrightText:
	defb 028h		;8009	28		(
	defb 043h		;800a	43		C
	defb 029h		;800b	29		)
l800ch:
	defb 020h		;800c	20		 
	defb 032h		;800d	32		2
	defb 030h		;800e	30		0
	defb 030h		;800f	30		0
	defb 032h		;8010	32		2
	defb 020h		;8011	20		 
	defb 050h		;8012	50		P
	defb 045h		;8013	45		E
	defb 054h		;8014	54		T
	defb 045h		;8015	45		E
	defb 052h		;8016	52		R
	defb 053h		;8017	53		S
	defb 020h		;8018	20		 
	defb 050h		;8019	50		P
	defb 04ch		;801a	4c		L
	defb 055h		;801b	55		U
	defb 053h		;801c	53		S
	defb 020h		;801d	20		 
	defb 04ch		;801e	4c		L
	defb 054h		;801f	54		T
	defb 044h		;8020	44		D
	defb 020h		;8021	20		 
INT_OFF:
				; = INT_OFF (src: DSETUP.ASM:139, BIOS-PP 1273243)
	di			;8022	f3		.
	ld a,03fh		;8023	3e 3f		> ?
	ld i,a			;8025	ed 47		. G
	im 1			;8027	ed 56		. V
	ret			;8029	c9		.
INT_ON:
				; = INT_ON (src: DSETUP.ASM:145, BIOS-PP 1273243)
	di			;802a	f3		.
	ld a,080h		;802b	3e 80		> .
	ld i,a			;802d	ed 47		. G
	ld hl,SetupIntHandler	;802f	21 01 81	! . .
	ld (l80ffh),hl		;8032	22 ff 80	" . .
	im 2			;8035	ed 5e		. ^
	ei			;8037	fb		.
	ret			;8038	c9		.

; BLOCK 'Zero8039' (start 0x8039 end 0x8040)
Zero8039:
	defb 000h		;8039	00		.
	defb 000h		;803a	00		.
	defb 000h		;803b	00		.
	defb 000h		;803c	00		.
	defb 000h		;803d	00		.
	defb 000h		;803e	00		.
	defb 000h		;803f	00		.
l8040h:

; BLOCK 'StackArea' (start 0x8040 end 0x8101)
StackArea:
	defb 000h		;8040	00		.
	defb 000h		;8041	00		.
	defb 000h		;8042	00		.
	defb 000h		;8043	00		.
	defb 000h		;8044	00		.
	defb 000h		;8045	00		.
	defb 000h		;8046	00		.
	defb 000h		;8047	00		.
	defb 000h		;8048	00		.
	defb 000h		;8049	00		.
	defb 000h		;804a	00		.
	defb 000h		;804b	00		.
	defb 000h		;804c	00		.
	defb 000h		;804d	00		.
	defb 000h		;804e	00		.
	defb 000h		;804f	00		.
	defb 000h		;8050	00		.
	defb 000h		;8051	00		.
	defb 000h		;8052	00		.
	defb 000h		;8053	00		.
	defb 000h		;8054	00		.
	defb 000h		;8055	00		.
	defb 000h		;8056	00		.
	defb 000h		;8057	00		.
	defb 000h		;8058	00		.
	defb 000h		;8059	00		.
	defb 000h		;805a	00		.
	defb 000h		;805b	00		.
	defb 000h		;805c	00		.
	defb 000h		;805d	00		.
	defb 000h		;805e	00		.
	defb 000h		;805f	00		.
	defb 000h		;8060	00		.
	defb 000h		;8061	00		.
	defb 000h		;8062	00		.
	defb 000h		;8063	00		.
	defb 000h		;8064	00		.
	defb 000h		;8065	00		.
	defb 000h		;8066	00		.
	defb 000h		;8067	00		.
	defb 000h		;8068	00		.
	defb 000h		;8069	00		.
	defb 000h		;806a	00		.
	defb 000h		;806b	00		.
	defb 000h		;806c	00		.
	defb 000h		;806d	00		.
	defb 000h		;806e	00		.
	defb 000h		;806f	00		.
	defb 000h		;8070	00		.
	defb 000h		;8071	00		.
	defb 000h		;8072	00		.
	defb 000h		;8073	00		.
	defb 000h		;8074	00		.
	defb 000h		;8075	00		.
	defb 000h		;8076	00		.
	defb 000h		;8077	00		.
	defb 000h		;8078	00		.
	defb 000h		;8079	00		.
	defb 000h		;807a	00		.
	defb 000h		;807b	00		.
	defb 000h		;807c	00		.
	defb 000h		;807d	00		.
	defb 000h		;807e	00		.
	defb 000h		;807f	00		.
l8080h:
	defb 000h		;8080	00		.
	defb 000h		;8081	00		.
	defb 000h		;8082	00		.
	defb 000h		;8083	00		.
	defb 000h		;8084	00		.
	defb 000h		;8085	00		.
	defb 000h		;8086	00		.
	defb 000h		;8087	00		.
	defb 000h		;8088	00		.
	defb 000h		;8089	00		.
	defb 000h		;808a	00		.
	defb 000h		;808b	00		.
	defb 000h		;808c	00		.
	defb 000h		;808d	00		.
	defb 000h		;808e	00		.
	defb 000h		;808f	00		.
	defb 000h		;8090	00		.
	defb 000h		;8091	00		.
	defb 000h		;8092	00		.
	defb 000h		;8093	00		.
	defb 000h		;8094	00		.
	defb 000h		;8095	00		.
	defb 000h		;8096	00		.
	defb 000h		;8097	00		.
	defb 000h		;8098	00		.
	defb 000h		;8099	00		.
	defb 000h		;809a	00		.
	defb 000h		;809b	00		.
	defb 000h		;809c	00		.
	defb 000h		;809d	00		.
	defb 000h		;809e	00		.
	defb 000h		;809f	00		.
	defb 000h		;80a0	00		.
	defb 000h		;80a1	00		.
	defb 000h		;80a2	00		.
	defb 000h		;80a3	00		.
	defb 000h		;80a4	00		.
	defb 000h		;80a5	00		.
	defb 000h		;80a6	00		.
	defb 000h		;80a7	00		.
	defb 000h		;80a8	00		.
	defb 000h		;80a9	00		.
	defb 000h		;80aa	00		.
	defb 000h		;80ab	00		.
	defb 000h		;80ac	00		.
	defb 000h		;80ad	00		.
	defb 000h		;80ae	00		.
	defb 000h		;80af	00		.
	defb 000h		;80b0	00		.
	defb 000h		;80b1	00		.
	defb 000h		;80b2	00		.
	defb 000h		;80b3	00		.
	defb 000h		;80b4	00		.
	defb 000h		;80b5	00		.
	defb 000h		;80b6	00		.
	defb 000h		;80b7	00		.
	defb 000h		;80b8	00		.
	defb 000h		;80b9	00		.
	defb 000h		;80ba	00		.
	defb 000h		;80bb	00		.
	defb 000h		;80bc	00		.
	defb 000h		;80bd	00		.
	defb 000h		;80be	00		.
	defb 000h		;80bf	00		.
	defb 000h		;80c0	00		.
	defb 000h		;80c1	00		.
	defb 000h		;80c2	00		.
	defb 000h		;80c3	00		.
	defb 000h		;80c4	00		.
	defb 000h		;80c5	00		.
	defb 000h		;80c6	00		.
	defb 000h		;80c7	00		.
	defb 000h		;80c8	00		.
	defb 000h		;80c9	00		.
	defb 000h		;80ca	00		.
	defb 000h		;80cb	00		.
	defb 000h		;80cc	00		.
	defb 000h		;80cd	00		.
	defb 000h		;80ce	00		.
	defb 000h		;80cf	00		.
	defb 000h		;80d0	00		.
	defb 000h		;80d1	00		.
	defb 000h		;80d2	00		.
	defb 000h		;80d3	00		.
	defb 000h		;80d4	00		.
	defb 000h		;80d5	00		.
	defb 000h		;80d6	00		.
	defb 000h		;80d7	00		.
	defb 000h		;80d8	00		.
	defb 000h		;80d9	00		.
	defb 000h		;80da	00		.
	defb 000h		;80db	00		.
	defb 000h		;80dc	00		.
	defb 000h		;80dd	00		.
	defb 000h		;80de	00		.
	defb 000h		;80df	00		.
	defb 000h		;80e0	00		.
	defb 000h		;80e1	00		.
	defb 000h		;80e2	00		.
	defb 000h		;80e3	00		.
	defb 000h		;80e4	00		.
	defb 000h		;80e5	00		.
	defb 000h		;80e6	00		.
	defb 000h		;80e7	00		.
	defb 000h		;80e8	00		.
	defb 000h		;80e9	00		.
	defb 000h		;80ea	00		.
	defb 000h		;80eb	00		.
	defb 000h		;80ec	00		.
	defb 000h		;80ed	00		.
	defb 000h		;80ee	00		.
	defb 000h		;80ef	00		.
l80f0h:
	defb 000h		;80f0	00		.
	defb 000h		;80f1	00		.
	defb 000h		;80f2	00		.
	defb 000h		;80f3	00		.
	defb 000h		;80f4	00		.
	defb 000h		;80f5	00		.
	defb 000h		;80f6	00		.
	defb 000h		;80f7	00		.
	defb 000h		;80f8	00		.
	defb 000h		;80f9	00		.
	defb 000h		;80fa	00		.
	defb 000h		;80fb	00		.
	defb 000h		;80fc	00		.
	defb 000h		;80fd	00		.
	defb 000h		;80fe	00		.
l80ffh:
	defb 000h		;80ff	00		.
	defb 000h		;8100	00		.
;---------------------------------------------------------------------------
; IM 2 interrupt handler (I = #80, vector at #80FF): saves all registers
; and runs KeyboardInterrupt, which drains the keyboard bytes from the
; Z84C15 SIO channel A into the key buffer (#9E00).
;---------------------------------------------------------------------------
SetupIntHandler:
				; = INT_ (src: DSETUP.ASM:160, BIOS-PP 1273243)
	push af			;8101	f5		.
	ex af,af'		;8102	08		.
	push af			;8103	f5		.
	push bc			;8104	c5		.
	push de			;8105	d5		.
	push hl			;8106	e5		.
	exx			;8107	d9		.
	push bc			;8108	c5		.
	push de			;8109	d5		.
	push hl			;810a	e5		.
	push ix			;810b	dd e5		. .
	push iy			;810d	fd e5		. .
	call KeyboardInterrupt	;810f	cd f0 9e	. . .
	pop iy			;8112	fd e1		. .
	pop ix			;8114	dd e1		. .
	pop hl			;8116	e1		.
	pop de			;8117	d1		.
	pop bc			;8118	c1		.
	exx			;8119	d9		.
	pop hl			;811a	e1		.
	pop de			;811b	d1		.
	pop bc			;811c	c1		.
	pop af			;811d	f1		.
	ex af,af'		;811e	08		.
	pop af			;811f	f1		.
	ei			;8120	fb		.
	reti			;8121	ed 4d		. M
EXSETUP:
				; = EXSETUP (src: DSETUP.ASM:188, BIOS-PP 1273243)
	call INT_OFF	;8123	cd 22 80	. " .
XFLEX:
				; = XFLEX (src: DSETUP.ASM:196, BIOS-PP 1273243)
	pop hl			;8126	e1		.
	ld (JMPHL+1),hl		;8127	22 2b 81	" + .
JMPHL:
				; = JMPHL (src: DSETUP.ASM:200, BIOS-PP 1273243)
	jp 00000h		;812a	c3 00 00	. . .
INSTALL:
				; = INSTALL (src: DSETUP.ASM:202, BIOS-PP 1273243)
	ld hl,LOGO		;812d	21 50 8d	! P .
	ld de,0d900h		;8130	11 00 d9	. . .
	ld bc,008d3h		;8133	01 d3 08	. . .
	ldir			;8136	ed b0		. .
	call 0d900h		;8138	cd 00 d9	. . .
	call INT_ON		;813b	cd 2a 80	. * .
	call SET_CGA	;813e	cd 80 8c	. . .
	xor a			;8141	af		.
	out (0feh),a		;8142	d3 fe		. .
	ld ix,TAB1		;8144	dd 21 7f 8b	. ! . .
	ld hl,00000h		;8148	21 00 00	! . .
	ld e,001h		;814b	1e 01		. .
	ld c,0b0h		;814d	0e b0		. .
	call 00018h		;814f	cd 18 00	. . .
	ld de,00000h		;8152	11 00 00	. . .
	ld hl,02050h		;8155	21 50 20	! P  
	ld bc,00789h		;8158	01 89 07	. . .
	call 00018h		;815b	cd 18 00	. . .
	ld de,00000h		;815e	11 00 00	. . .
	ld hl,00820h		;8161	21 20 08	!   .
	ld bc,00089h		;8164	01 89 00	. . .
	call 00018h		;8167	cd 18 00	. . .
	call SETLAND		;816a	cd 04 b6	. . .
	call GET_ID		;816d	cd 09 88	. . .
	ld de,00028h		;8170	11 28 00	. ( .
	call LOCAT		;8173	cd ae 89	. . .
	ld hl,IDBUFF		;8176	21 5f 86	! _ .
	ld a,00bh		;8179	3e 0b		> .
	call CPRINTZ		;817b	cd 18 8a	. . .
	ld hl,BUILD		;817e	21 b1 81	! . .
	ld a,00bh		;8181	3e 0b		> .
	call CPRINTZ		;8183	cd 18 8a	. . .
	ld de,00128h		;8186	11 28 01	. ( .
	call LOCAT		;8189	cd ae 89	. . .
	ld a,001h		;818c	3e 01		> .
	ld e,00ah		;818e	1e 0a		. .
	call POSTMSC		;8190	cd ef b5	. . .
	ld de,00228h		;8193	11 28 02	. ( .
	call LOCAT		;8196	cd ae 89	. . .
	ld a,002h		;8199	3e 02		> .
	ld e,002h		;819b	1e 02		. .
	call POSTMSC		;819d	cd ef b5	. . .
	in a,(0e2h)		;81a0	db e2		. .
	push af			;81a2	f5		.
	ld a,0feh		;81a3	3e fe		> .
	out (0e2h),a		;81a5	d3 e2		. .
	ld hl,0f010h		;81a7	21 10 f0	! . .
	call GETTIME		;81aa	cd 10 88	. . .
	pop af			;81ad	f1		.
	out (0e2h),a		;81ae	d3 e2		. .
	ret			;81b0	c9		.
BUILD:

; BLOCK 'Text81B1' (start 0x81b1 end 0x81be)
				; = BUILD (src: DSETUP.ASM:262, BIOS-PP 1273243)
	defb 02eh		;81b1	2e		.
	defb 032h		;81b2	32		2
	defb 035h		;81b3	35		5
	defb 033h		;81b4	33		3
	defb 000h		;81b5	00		.
RSTID:
				; = RSTID (src: DSETUP.ASM:269, BIOS-PP 1273243)
	defb 052h		;81b6	52		R
	defb 045h		;81b7	45		E
	defb 053h		;81b8	53		S
	defb 054h		;81b9	54		T
	defb 041h		;81ba	41		A
	defb 052h		;81bb	52		R
	defb 054h		;81bc	54		T
	defb 000h		;81bd	00		.
;---------------------------------------------------------------------------
; SetupStart, the boot sequence:
;   1. KeyboardInit (SIO A), clear the text screen, read the CMOS
;      settings (READING); a bad checksum loads the defaults (SETDEFX);
;   2. clear the user interrupt hook (#C124-#C127, system page #FE);
;   3. TR-DOS quick-start check, floppy drive table (FINSTAL), clear
;      memory (CLEARM);
;   4. screen position from CMOS #1F (ApplyScreenPosition);
;   5. warm start? (signature RSTID at #F000 of the system page): skip
;      the screen, else draw the boot screen: logo, BIOS id, memory
;      size (function #C0), CMOS clock or "no CMOS";
;   6. TSETUP: DEL enters the setup menu, ESC leaves to Spectrum mode;
;   7. IDE auto-detect, then BootSysDevice; on failure BootAltDevice;
;      if both fail, "press a key" and start over.
;---------------------------------------------------------------------------
SetupStart:
				; = START (src: DSETUP.ASM:271, BIOS-PP 1273243)
	di			;81be	f3		.
	push af			;81bf	f5		.
	xor a			;81c0	af		.
	ld (ERRSUM+1),a		;81c1	32 71 82	2 q .
	call KeyboardInit	;81c4	cd 73 a3	. s .
	call ZXCLS		;81c7	cd fc 87	. . .
	call READING		;81ca	cd 5e 9b	. ^ .
	call TCHEKSM		;81cd	cd 45 9b	. E .
	call nz,SETDEFX		;81d0	c4 8b 9b	. . .
	ld c,0e2h		;81d3	0e e2		. .
	in b,(c)		;81d5	ed 40		. @
	push bc			;81d7	c5		.
	ld a,0feh		;81d8	3e fe		> .
	out (c),a		;81da	ed 79		. y
	ld hl,0c124h		;81dc	21 24 c1	! $ .
	xor a			;81df	af		.
	ld (hl),a		;81e0	77		w
	inc l			;81e1	2c		,
	ld (hl),a		;81e2	77		w
	inc l			;81e3	2c		,
	ld (hl),a		;81e4	77		w
	inc l			;81e5	2c		,
	ld (hl),a		;81e6	77		w
	pop bc			;81e7	c1		.
	out (c),b		;81e8	ed 41		. A
	call TRQUICK		;81ea	cd da 88	. . .
	call FINSTAL	;81ed	cd cd 84	. . .
	call CLEARM		;81f0	cd 6a 88	. j .
	ld a,01fh		;81f3	3e 1f		> .
	call READCMS		;81f5	cd 27 9b	. ' .
	push af			;81f8	f5		.
	in a,(0e2h)		;81f9	db e2		. .
	ld e,a			;81fb	5f		_
	ld a,040h		;81fc	3e 40		> @
	out (0e2h),a		;81fe	d3 e2		. .
	ld a,(0c400h)		;8200	3a 00 c4	: . .
	ld d,a			;8203	57		W
	ld a,0cbh		;8204	3e cb		> .
	ld (0c400h),a		;8206	32 00 c4	2 . .
	pop af			;8209	f1		.
	ld bc,00000h		;820a	01 00 00	. . .
	out (c),a		;820d	ed 79		. y
	ld a,d			;820f	7a		z
	ld (0c400h),a		;8210	32 00 c4	2 . .
	ld a,e			;8213	7b		{
	out (0e2h),a		;8214	d3 e2		. .
	ld bc,0010eh		;8216	01 0e 01	. . .
	call G_VALUE		;8219	cd 19 9b	. . .
	pop bc			;821c	c1		.
	inc b			;821d	04		.
	dec b			;821e	05		.
	jr nz,QIGNORE		;821f	20 39		  9
	push af			;8221	f5		.
	ld c,0e2h		;8222	0e e2		. .
	in b,(c)		;8224	ed 40		. @
	push bc			;8226	c5		.
	ld a,0feh		;8227	3e fe		> .
	out (c),a		;8229	ed 79		. y
	ld hl,0f000h		;822b	21 00 f0	! . .
	ld de,RSTID		;822e	11 b6 81	. . .
	ld b,007h		;8231	06 07		. .
	call COMPARE		;8233	cd 6a 83	. j .
	call nz,SETRSTS		;8236	c4 41 82	. A .
	pop bc			;8239	c1		.
	out (c),b		;823a	ed 41		. A
	jr z,HOTST		;823c	28 17		( .
	pop af			;823e	f1		.
	jr QIGNORE		;823f	18 19		. .
SETRSTS:
				; = SETRSTS (src: DSETUP.ASM:346, BIOS-PP 1273243)
	push af			;8241	f5		.
	ld hl,RSTID		;8242	21 b6 81	! . .
	ld de,0f000h		;8245	11 00 f0	. . .
	ld bc,00008h		;8248	01 08 00	. . .
	ldir			;824b	ed b0		. .
	ld hl,0f008h		;824d	21 08 f0	! . .
	call GETTIME		;8250	cd 10 88	. . .
	pop af			;8253	f1		.
	ret			;8254	c9		.
HOTST:
				; = HOTST (src: DSETUP.ASM:356, BIOS-PP 1273243)
	pop af			;8255	f1		.
	or a			;8256	b7		.
	jp nz,EXSETUP		;8257	c2 23 81	. # .
QIGNORE:
				; = QIGNORE (src: DSETUP.ASM:359, BIOS-PP 1273243)
	call INSTALL		;825a	cd 2d 81	. - .
	ld de,00528h		;825d	11 28 05	. ( .
	call LOCAT		;8260	cd ae 89	. . .
	ld a,00ah		;8263	3e 0a		> .
	ld e,08fh		;8265	1e 8f		. .
	call POSTMSC		;8267	cd ef b5	. . .
	ld de,00800h		;826a	11 00 08	. . .
	call LOCAT		;826d	cd ae 89	. . .
ERRSUM:
				; = ERRSUM (src: DSETUP.ASM:369, BIOS-PP 1273243)
	ld a,000h		;8270	3e 00		> .
	or a			;8272	b7		.
	jr z,CHEKOK		;8273	28 0b		( .
	ld a,00dh		;8275	3e 0d		> .
	call POSTMSG		;8277	cd db b5	. . .
	call CRLF		;827a	cd b3 89	. . .
	call CRLF		;827d	cd b3 89	. . .
CHEKOK:
				; = CHEKOK (src: DSETUP.ASM:376, BIOS-PP 1273243)
	call GET_CUR	;8280	cd a4 89	. . .
	push de			;8283	d5		.
	call LOGOTYP		;8284	cd fe 8a	. . .
	ld de,00000h		;8287	11 00 00	. . .
	call LOCAT		;828a	cd ae 89	. . .
	ld a,016h		;828d	3e 16		> .
	call POSTMSG		;828f	cd db b5	. . .
	call PIDNUM	;8292	cd 5f 87	. _ .
	call CRLF		;8295	cd b3 89	. . .
	ld a,01dh		;8298	3e 1d		> .
	call POSTMSG		;829a	cd db b5	. . .
	call sub_876fh		;829d	cd 6f 87	. o .
	call CRLF		;82a0	cd b3 89	. . .
	ld a,017h		;82a3	3e 17		> .
	call POSTMSG		;82a5	cd db b5	. . .
	ld c,0c0h		;82a8	0e c0		. .
	call 00018h		;82aa	cd 18 00	. . .
	push bc			;82ad	c5		.
	call PMEMORY		;82ae	cd f0 87	. . .
	call CRLF		;82b1	cd b3 89	. . .
	ld a,01ah		;82b4	3e 1a		> .
	call POSTMSG		;82b6	cd db b5	. . .
	pop hl			;82b9	e1		.
	call PMEMORY		;82ba	cd f0 87	. . .
	call CRLF		;82bd	cd b3 89	. . .
	call TSTCMOS		;82c0	cd 4f 9b	. O .
	ld a,019h		;82c3	3e 19		> .
	jr c,NOCMOS		;82c5	38 1d		8 .
	call sub_8396h		;82c7	cd 96 83	. . .
	ld a,018h		;82ca	3e 18		> .
	call POSTMSG		;82cc	cd db b5	. . .
	ld a,02ch		;82cf	3e 2c		> ,
	call PRINT		;82d1	cd f8 89	. . .
	ld a,020h		;82d4	3e 20		>  
	call PRINT		;82d6	cd f8 89	. . .
	ld hl,0f010h		;82d9	21 10 f0	! . .
	call PRNTIME		;82dc	cd cd 87	. . .
	call CRLF		;82df	cd b3 89	. . .
	jr NOCMOS2		;82e2	18 03		. .
NOCMOS:
				; = NOCMOS (src: DSETUP.ASM:423, BIOS-PP 1273243)
	call POSTMSG		;82e4	cd db b5	. . .
NOCMOS2:
				; = NOCMOS2 (src: DSETUP.ASM:424, BIOS-PP 1273243)
	ld de,00528h		;82e7	11 28 05	. ( .
	call LOCAT		;82ea	cd ae 89	. . .
	ld a,00bh		;82ed	3e 0b		> .
	ld e,00fh		;82ef	1e 0f		. .
	call POSTMSC		;82f1	cd ef b5	. . .
	pop de			;82f4	d1		.
	call LOCAT		;82f5	cd ae 89	. . .
	call TSETUP		;82f8	cd 4a 83	. J .
	call AUTODET		;82fb	cd 2a 85	. * .
	call CTRLKEY		;82fe	cd 64 9e	. d .
	ld a,b			;8301	78		x
	and 010h		;8302	e6 10		. .
	jr nz,ABOOT		;8304	20 0e		  .
	call TSETUP		;8306	cd 4a 83	. J .
	call BootSysDevice	;8309	cd c9 83	. . .
	ld a,023h		;830c	3e 23		> #
	call c,POSTMSG		;830e	dc db b5	. . .
	call CRLF		;8311	cd b3 89	. . .
ABOOT:
				; = ABOOT (src: DSETUP.ASM:447, BIOS-PP 1273243)
	ld a,022h		;8314	3e 22		> "
	call POSTMSG		;8316	cd db b5	. . .
	call BootAltDevice	;8319	cd c4 83	. . .
	ld a,023h		;831c	3e 23		> #
NOSKIP1:
				; = ~NOSKIP1 (src: DSETUP.ASM:764, BIOS-PP 1273243)
	call c,POSTMSG		;831e	dc db b5	. . .
	call CRLF		;8321	cd b3 89	. . .
NEXTIDE:
				; = ~NEXTIDE (src: DSETUP.ASM:767, BIOS-PP 1273243)
	ld bc,0021dh		;8324	01 1d 02	. . .
	call G_VALUE		;8327	cd 19 9b	. . .
	or a			;832a	b7		.
	jp z,EXSETUP		;832b	ca 23 81	. # .
	call CRLF		;832e	cd b3 89	. . .
	ld a,025h		;8331	3e 25		> %
	ld e,00fh		;8333	1e 0f		. .
	call POSTMSC		;8335	cd ef b5	. . .
	ei			;8338	fb		.
AGAKEY:
				; = AGAKEY (src: DSETUP.ASM:466, BIOS-PP 1273243)
	call WAITKEY	;8339	cd 48 9e	. H .
	cp 01bh			;833c	fe 1b		. .
	jp z,EXSETUP		;833e	ca 23 81	. # .
	cp 00dh			;8341	fe 0d		. .
	jp nz,AGAKEY		;8343	c2 39 83	. 9 .
	xor a			;8346	af		.
	jp SetupStart		;8347	c3 be 81	. . .
TSETUP:
				; = TSETUP (src: DSETUP.ASM:474, BIOS-PP 1273243)
	call SCANKEY		;834a	cd 57 9e	. W .
	ret z			;834d	c8		.
	ld hl,04f00h		;834e	21 00 4f	! . O
	and a			;8351	a7		.
	sbc hl,de		;8352	ed 52		. R
	jr z,CSETUP		;8354	28 0c		( .
	ld hl,0011bh		;8356	21 1b 01	! . .
	and a			;8359	a7		.
	sbc hl,de		;835a	ed 52		. R
	jr nz,TSETUP		;835c	20 ec		  .
	pop hl			;835e	e1		.
	jp EXSETUP		;835f	c3 23 81	. # .
CSETUP:
				; = CSETUP (src: DSETUP.ASM:486, BIOS-PP 1273243)
	pop hl			;8362	e1		.
	call U_SETUP	;8363	cd 07 98	. . .
	xor a			;8366	af		.
	jp SetupStart		;8367	c3 be 81	. . .
COMPARE:
				; = COMPARE (src: DSETUP.ASM:491, BIOS-PP 1273243)
	ld a,(de)		;836a	1a		.
	cp (hl)			;836b	be		.
	ret nz			;836c	c0		.
	inc hl			;836d	23		#
	inc de			;836e	13		.
	djnz COMPARE		;836f	10 f9		. .
	ret			;8371	c9		.
;---------------------------------------------------------------------------
; ApplyScreenPosition: a trick to reach a PLD register that no port maps:
; temporarily store code #CB (HOLD, the screen position counters) at
; page #40 offset #0400 (map 0, write, DOS off, all address bits 0), do
; OUT (C),A with BC = #0000 (A = CMOS #1F), then restore the table byte.
;---------------------------------------------------------------------------
ApplyScreenPosition:
	ld a,01fh		;8372	3e 1f		> .
	call READCMS		;8374	cd 27 9b	. ' .
	push af			;8377	f5		.
	in a,(0e2h)		;8378	db e2		. .
	ld e,a			;837a	5f		_
	ld a,040h		;837b	3e 40		> @
	out (0e2h),a		;837d	d3 e2		. .
	ld a,(0c400h)		;837f	3a 00 c4	: . .
	ld d,a			;8382	57		W
	ld a,0cbh		;8383	3e cb		> .
	ld (0c400h),a		;8385	32 00 c4	2 . .
	pop af			;8388	f1		.
	ld bc,00000h		;8389	01 00 00	. . .
	out (c),a		;838c	ed 79		. y
	ld a,d			;838e	7a		z
	ld (0c400h),a		;838f	32 00 c4	2 . .
	ld a,e			;8392	7b		{
	out (0e2h),a		;8393	d3 e2		. .
	ret			;8395	c9		.
sub_8396h:
	ld d,00ah		;8396	16 0a		. .
	ld c,0f6h		;8398	0e f6		. .
	rst 18h			;839a	df		.
	cp 026h			;839b	fe 26		. &
	jr nz,l83a7h		;839d	20 08		  .
	ld d,00ch		;839f	16 0c		. .
	ld c,0f6h		;83a1	0e f6		. .
	rst 18h			;83a3	df		.
	cp 050h			;83a4	fe 50		. P
	ret z			;83a6	c8		.
l83a7h:
	ld d,00ah		;83a7	16 0a		. .
	ld a,026h		;83a9	3e 26		> &
	ld c,0f7h		;83ab	0e f7		. .
	rst 18h			;83ad	df		.
	ld d,00bh		;83ae	16 0b		. .
	ld a,002h		;83b0	3e 02		> .
	ld c,0f7h		;83b2	0e f7		. .
	rst 18h			;83b4	df		.
	ld d,00ch		;83b5	16 0c		. .
	ld a,050h		;83b7	3e 50		> P
	ld c,0f7h		;83b9	0e f7		. .
	rst 18h			;83bb	df		.
	ld d,00dh		;83bc	16 0d		. .
	ld a,080h		;83be	3e 80		> .
	ld c,0f7h		;83c0	0e f7		. .
	rst 18h			;83c2	df		.
	ret			;83c3	c9		.
;---------------------------------------------------------------------------
; Boot device from CMOS register #10: low nibble = system disk, high
; nibble = alternative disk (G_VALUE with B = mask, C = register).
; Value -> device code in B (the BIOS disk API number):
;   0 -> #00 floppy A, 1 -> #01 floppy B, 2 -> #80 IDE master,
;   3 -> #81 IDE slave, 4 -> #6E RAM disk; anything else fails.
; A CD-ROM unit found at the IDE position prints a message and fails
; (no CD boot in 3.04).
;---------------------------------------------------------------------------
BootAltDevice:
				; = BOOTALT (src: DSETUP.ASM:505, BIOS-PP 1273243)
	ld bc,07010h		;83c4	01 10 70	. . p
	jr BootFromCmosNibble	;83c7	18 03		. .
BootSysDevice:
				; = BOOTSYS (src: DSETUP.ASM:507, BIOS-PP 1273243)
	ld bc,00710h		;83c9	01 10 07	. . .
BootFromCmosNibble:
				; = BOOT000 (src: DSETUP.ASM:508, BIOS-PP 1273243)
	call G_VALUE		;83cc	cd 19 9b	. . .
	or a			;83cf	b7		.
	ld b,000h		;83d0	06 00		. .
	jp z,BootFloppy		;83d2	ca ef 83	. . .
	dec a			;83d5	3d		=
	ld b,001h		;83d6	06 01		. .
	jp z,BootFloppy		;83d8	ca ef 83	. . .
	dec a			;83db	3d		=
	ld b,080h		;83dc	06 80		. .
	jp z,BootIde		;83de	ca 16 84	. . .
	dec a			;83e1	3d		=
	ld b,081h		;83e2	06 81		. .
	jp z,BootIde		;83e4	ca 16 84	. . .
	dec a			;83e7	3d		=
	ld b,06eh		;83e8	06 6e		. n
	jp z,BootRamDisk	;83ea	ca 0c 84	. . .
	scf			;83ed	37		7
	ret			;83ee	c9		.
;---------------------------------------------------------------------------
; Floppy: function #51 (DRV_RESET) with A = drive first; it runs the
; density probe (ROM page 0 FddProbeDensity), so a 1.44 MB disk is read
; at 500 kbit/s from here on.
;---------------------------------------------------------------------------
BootFloppy:
				; = FDSTART (src: DSETUP.ASM:527, BIOS-PP 1273243)
	push bc			;83ef	c5		.
	push bc			;83f0	c5		.
	ld a,01eh		;83f1	3e 1e		> .
	call POSTMSG		;83f3	cd db b5	. . .
	pop af			;83f6	f1		.
	ld c,051h		;83f7	0e 51		. Q
	call 00018h		;83f9	cd 18 00	. . .
	pop bc			;83fc	c1		.
	ret c			;83fd	d8		.
	jp LoadBootSector	;83fe	c3 51 84	. Q .
BootCdRom:
				; = CDSTART (src: DSETUP.ASM:538, BIOS-PP 1273243)
	push bc			;8401	c5		.
	ld a,020h		;8402	3e 20		>  
	call POSTMSG		;8404	cd db b5	. . .
	pop bc			;8407	c1		.
	set 6,b			;8408	cb f0		. .
	scf			;840a	37		7
	ret			;840b	c9		.
BootRamDisk:
				; = RDSTART (src: DSETUP.ASM:546, BIOS-PP 1273243)
	push bc			;840c	c5		.
	ld a,021h		;840d	3e 21		> !
	call POSTMSG		;840f	cd db b5	. . .
	pop bc			;8412	c1		.
	jp LoadBootSector	;8413	c3 51 84	. Q .
BootIde:
				; = HDSTART (src: DSETUP.ASM:552, BIOS-PP 1273243)
	in a,(0e2h)		;8416	db e2		. .
	ex af,af'		;8418	08		.
	ld a,0feh		;8419	3e fe		> .
	out (0e2h),a		;841b	d3 e2		. .
	ld a,b			;841d	78		x
	and 003h		;841e	e6 03		. .
	ld iy,0c1c0h		;8420	fd 21 c0 c1	. ! . .
	jr z,HDSL1		;8424	28 12		( .
	ld iy,0c1c8h		;8426	fd 21 c8 c1	. ! . .
	dec a			;842a	3d		=
	jr z,HDSL1		;842b	28 0b		( .
	ld iy,0c1d0h		;842d	fd 21 d0 c1	. ! . .
	dec a			;8431	3d		=
	jr z,HDSL1		;8432	28 04		( .
	ld iy,0c1d8h		;8434	fd 21 d8 c1	. ! . .
HDSL1:
				; = HDSL1 (src: DSETUP.ASM:561, BIOS-PP 1273243)
	ld a,(iy+007h)		;8438	fd 7e 07	. ~ .
	ex af,af'		;843b	08		.
	out (0e2h),a		;843c	d3 e2		. .
	ex af,af'		;843e	08		.
	cp 002h			;843f	fe 02		. .
	jp z,BootCdRom		;8441	ca 01 84	. . .
	push af			;8444	f5		.
	push bc			;8445	c5		.
	ld a,01fh		;8446	3e 1f		> .
	call POSTMSG		;8448	cd db b5	. . .
	pop bc			;844b	c1		.
	pop af			;844c	f1		.
	cp 0ffh			;844d	fe ff		. .
	scf			;844f	37		7
	ret z			;8450	c8		.
;---------------------------------------------------------------------------
; LoadBootSector: function #55 (DRV_READ), A = device, HL:IX = #0000:#0001
; (LBA 1), B = 1 sector, DE = #7E00. The 512 bytes must start with
; BootSignature, "Starting..." + #00 (12 bytes); otherwise Carry = 1 and
; the next device is tried.
; Worked example (DSS 1.62 floppy, testdata/machines/sprinter/): LBA 1 =
; cylinder 0, side 0, sector 2; its first 12 bytes are "Starting...",0.
;---------------------------------------------------------------------------
LoadBootSector:
				; = OS_LOAD (src: DSETUP.ASM:576, BIOS-PP 1273243)
	ld hl,00000h		;8451	21 00 00	! . .
	ld ix,00001h		;8454	dd 21 01 00	. ! . .
	ld de,07e00h		;8458	11 00 7e	. . ~
	ld a,b			;845b	78		x
	ld bc,00155h		;845c	01 55 01	. U .
	push af			;845f	f5		.
	call 00018h		;8460	cd 18 00	. . .
	pop bc			;8463	c1		.
	ret c			;8464	d8		.
	ld a,b			;8465	78		x
	ex af,af'		;8466	08		.
	ld hl,07e00h		;8467	21 00 7e	! . ~
	ld de,BootSignature	;846a	11 86 84	. . .
	ld b,00ch		;846d	06 0c		. .
SYSLOP1:
				; = SYSLOP1 (src: DSETUP.ASM:590, BIOS-PP 1273243)
	ld a,(de)		;846f	1a		.
	cp (hl)			;8470	be		.
	scf			;8471	37		7
	ret nz			;8472	c0		.
	inc hl			;8473	23		#
	inc de			;8474	13		.
	djnz SYSLOP1		;8475	10 f8		. .
	ex af,af'		;8477	08		.
	push af			;8478	f5		.
	ld a,024h		;8479	3e 24		> $
	call POSTMSG		;847b	cd db b5	. . .
	call CRLF		;847e	cd b3 89	. . .
	pop af			;8481	f1		.
	pop hl			;8482	e1		.
	jp RunBootSector		;8483	c3 92 84	. . .
BootSignature:

; BLOCK 'Text8486' (start 0x8486 end 0x8492)
				; = SYSID (src: DSETUP.ASM:606, BIOS-PP 1273243)
	defb 053h		;8486	53		S
	defb 074h		;8487	74		t
	defb 061h		;8488	61		a
	defb 072h		;8489	72		r
	defb 074h		;848a	74		t
	defb 069h		;848b	69		i
	defb 06eh		;848c	6e		n
	defb 067h		;848d	67		g
	defb 02eh		;848e	2e		.
	defb 02eh		;848f	2e		.
	defb 02eh		;8490	2e		.
	defb 000h		;8491	00		.
;---------------------------------------------------------------------------
; RunBootSector: copy BootMover7C00 (26 bytes) to #7C00 and jump there. It
; sets SP = #7FFF, clears #8000, copies the sector from #7E00 to #8000 and
; jumps to #800C (the code after the 12-byte signature) with A = the
; device code (from AF'). The DSS loader then loads LBA 2-3 and
; SYSTEM.DOS (hardware-reference.md 14).
;---------------------------------------------------------------------------
RunBootSector:
				; = MOVE0 (src: DSETUP.ASM:609, BIOS-PP 1273243)
	di			;8492	f3		.
	im 1			;8493	ed 56		. V
	ld hl,BootMover7C00	;8495	21 a3 84	! . .
	ld de,07c00h		;8498	11 00 7c	. . |
	ld bc,0001ah		;849b	01 1a 00	. . .
	ldir			;849e	ed b0		. .
	jp 07c00h		;84a0	c3 00 7c	. . |
BootMover7C00:
				; = MOVE1 (src: DSETUP.ASM:616, BIOS-PP 1273243)
	ld sp,07fffh		;84a3	31 ff 7f	1 . .
	ld hl,SetupEntry	;84a6	21 00 80	! . .
	ld d,h			;84a9	54		T
	ld b,h			;84aa	44		D
	ld e,l			;84ab	5d		]
	ld c,l			;84ac	4d		M
	inc e			;84ad	1c		.
	ld (hl),l		;84ae	75		u
	ld hl,07e00h		;84af	21 00 7e	! . ~
	ld de,SetupEntry	;84b2	11 00 80	. . .
	ld bc,00200h		;84b5	01 00 02	. . .
	ldir			;84b8	ed b0		. .
	jp l800ch		;84ba	c3 0c 80	. . .
MOVE2:

; BLOCK 'Data84BD' (start 0x84bd end 0x84cd)
				; = MOVE2 (src: DSETUP.ASM:629, BIOS-PP 1273243)
				; = FD144A (src: DSETUP.ASM:631, BIOS-PP 1273243)
	defb 080h		;84bd	80		.
	defb 012h		;84be	12		.
	defb 002h		;84bf	02		.
	defb 050h		;84c0	50		P
	defb 000h		;84c1	00		.
	defb 000h		;84c2	00		.
	defb 002h		;84c3	02		.
	defb 003h		;84c4	03		.
FD720A:
				; = FD720A (src: DSETUP.ASM:632, BIOS-PP 1273243)
	defb 000h		;84c5	00		.
	defb 009h		;84c6	09		.
	defb 002h		;84c7	02		.
	defb 050h		;84c8	50		P
	defb 000h		;84c9	00		.
	defb 000h		;84ca	00		.
	defb 002h		;84cb	02		.
	defb 003h		;84cc	03		.
FINSTAL:
				; = FINSTAL (src: DSETUP.ASM:634, BIOS-PP 1273243)
	in a,(0e2h)		;84cd	db e2		. .
	ex af,af'		;84cf	08		.
	ld a,0feh		;84d0	3e fe		> .
	out (0e2h),a		;84d2	d3 e2		. .
	ld hl,0c1e0h		;84d4	21 e0 c1	! . .
	ld bc,020ffh		;84d7	01 ff 20	. .  
FILLFDD:
				; = FILLFDD (src: DSETUP.ASM:640, BIOS-PP 1273243)
	ld (hl),c		;84da	71		q
	inc hl			;84db	23		#
	djnz FILLFDD		;84dc	10 fc		. .
	ex af,af'		;84de	08		.
	out (0e2h),a		;84df	d3 e2		. .
	ld bc,00311h		;84e1	01 11 03	. . .
	call G_VALUE		;84e4	cd 19 9b	. . .
	ld hl,FD720A		;84e7	21 c5 84	! . .
	or a			;84ea	b7		.
	jr z,SETFD0		;84eb	28 06		( .
	ld hl,MOVE2		;84ed	21 bd 84	! . .
	dec a			;84f0	3d		=
	jr nz,NOFDD0		;84f1	20 12		  .
SETFD0:
				; = SETFD0 (src: DSETUP.ASM:653, BIOS-PP 1273243)
	in a,(0e2h)		;84f3	db e2		. .
	ex af,af'		;84f5	08		.
	ld a,0feh		;84f6	3e fe		> .
	out (0e2h),a		;84f8	d3 e2		. .
	ld de,0c1e0h		;84fa	11 e0 c1	. . .
	ld bc,00008h		;84fd	01 08 00	. . .
	ldir			;8500	ed b0		. .
	ex af,af'		;8502	08		.
	out (0e2h),a		;8503	d3 e2		. .
NOFDD0:
				; = NOFDD0 (src: DSETUP.ASM:662, BIOS-PP 1273243)
	ld bc,00c11h		;8505	01 11 0c	. . .
	call G_VALUE		;8508	cd 19 9b	. . .
	ld hl,FD720A		;850b	21 c5 84	! . .
	or a			;850e	b7		.
	jr z,SETFD1		;850f	28 06		( .
	ld hl,MOVE2		;8511	21 bd 84	! . .
	dec a			;8514	3d		=
	jr nz,NOFDD1		;8515	20 12		  .
SETFD1:
				; = SETFD1 (src: DSETUP.ASM:670, BIOS-PP 1273243)
	in a,(0e2h)		;8517	db e2		. .
	ex af,af'		;8519	08		.
	ld a,0feh		;851a	3e fe		> .
	out (0e2h),a		;851c	d3 e2		. .
	ld de,0c1e8h		;851e	11 e8 c1	. . .
	ld bc,00008h		;8521	01 08 00	. . .
	ldir			;8524	ed b0		. .
	ex af,af'		;8526	08		.
	out (0e2h),a		;8527	d3 e2		. .
NOFDD1:
				; = NOFDD1 (src: DSETUP.ASM:679, BIOS-PP 1273243)
	ret			;8529	c9		.
AUTODET:
				; = AUTODET (src: DSETUP.ASM:685, BIOS-PP 1273243)
	in a,(0e2h)		;852a	db e2		. .
	ex af,af'		;852c	08		.
	ld a,0feh		;852d	3e fe		> .
	out (0e2h),a		;852f	d3 e2		. .
	ld hl,0c1c0h		;8531	21 c0 c1	! . .
	ld bc,020ffh		;8534	01 ff 20	. .  
FILLIDE:
				; = FILLIDE (src: DSETUP.ASM:691, BIOS-PP 1273243)
	ld (hl),c		;8537	71		q
	inc hl			;8538	23		#
	djnz FILLIDE		;8539	10 fc		. .
	ex af,af'		;853b	08		.
	out (0e2h),a		;853c	d3 e2		. .
	call sub_8552h		;853e	cd 52 85	. R .
	ld a,000h		;8541	3e 00		> .
	call sub_8584h		;8543	cd 84 85	. . .
	call sub_8552h		;8546	cd 52 85	. R .
	ld a,001h		;8549	3e 01		> .
	call sub_8584h		;854b	cd 84 85	. . .
	call CRLF		;854e	cd b3 89	. . .
	ret			;8551	c9		.
sub_8552h:
	ld c,0e2h		;8552	0e e2		. .
	in b,(c)		;8554	ed 40		. @
	ld a,0feh		;8556	3e fe		> .
	out (c),a		;8558	ed 79		. y
	call sub_8560h		;855a	cd 60 85	. ` .
	out (c),b		;855d	ed 41		. A
	ret			;855f	c9		.
sub_8560h:
	ld iy,0c1c0h		;8560	fd 21 c0 c1	. ! . .
	ld a,(iy+007h)		;8564	fd 7e 07	. ~ .
	inc a			;8567	3c		<
	ret z			;8568	c8		.
	ld iy,0c1c8h		;8569	fd 21 c8 c1	. ! . .
	ld a,(iy+007h)		;856d	fd 7e 07	. ~ .
	inc a			;8570	3c		<
	ret z			;8571	c8		.
	ld iy,0c1d0h		;8572	fd 21 d0 c1	. ! . .
	ld a,(iy+007h)		;8576	fd 7e 07	. ~ .
	inc a			;8579	3c		<
	ret z			;857a	c8		.
	ld iy,0c1d8h		;857b	fd 21 d8 c1	. ! . .
	ld a,(iy+007h)		;857f	fd 7e 07	. ~ .
	inc a			;8582	3c		<
	ret			;8583	c9		.
sub_8584h:
	and a			;8584	a7		.
	ld h,a			;8585	67		g
	ld bc,03011h		;8586	01 11 30	. . 0
	ld l,00eh		;8589	2e 0e		. .
	jr z,l85a7h		;858b	28 1a		( .
	dec a			;858d	3d		=
	ld bc,0c011h		;858e	01 11 c0	. . .
	ld l,00fh		;8591	2e 0f		. .
	jr z,l85a7h		;8593	28 12		( .
	dec a			;8595	3d		=
	ld bc,03011h		;8596	01 11 30	. . 0
	ld l,010h		;8599	2e 10		. .
	jr z,l85a7h		;859b	28 0a		( .
	dec a			;859d	3d		=
	ld bc,0c011h		;859e	01 11 c0	. . .
	ld l,011h		;85a1	2e 11		. .
	jr z,l85a7h		;85a3	28 02		( .
	scf			;85a5	37		7
	ret			;85a6	c9		.
l85a7h:
	call G_VALUE		;85a7	cd 19 9b	. . .
	or a			;85aa	b7		.
	jp z,l85b7h		;85ab	ca b7 85	. . .
	dec a			;85ae	3d		=
	jp z,l85fah	;85af	ca fa 85	. . .
	dec a			;85b2	3d		=
	jp z,l85d9h		;85b3	ca d9 85	. . .
	ret			;85b6	c9		.
l85b7h:
	push hl			;85b7	e5		.
	ld a,l			;85b8	7d		}
	call POSTMSG		;85b9	cd db b5	. . .
	call SUBNAME		;85bc	cd a9 89	. . .
	ei			;85bf	fb		.
	pop hl			;85c0	e1		.
	ld a,h			;85c1	7c		|
	call sub_9649h		;85c2	cd 49 96	. I .
l85c5h:
	call nc,MODEL		;85c5	d4 bc 89	. . .
	ld a,(SKIP)		;85c8	3a 04 98	: . .
	inc a			;85cb	3c		<
	ld a,013h		;85cc	3e 13		> .
	jr z,l85d2h		;85ce	28 02		( .
	ld a,014h		;85d0	3e 14		> .
l85d2h:
	call c,POSTMSG		;85d2	dc db b5	. . .
	call CRLF		;85d5	cd b3 89	. . .
	ret			;85d8	c9		.
l85d9h:
	push hl			;85d9	e5		.
	ld a,l			;85da	7d		}
	call POSTMSG		;85db	cd db b5	. . .
	call SUBNAME		;85de	cd a9 89	. . .
	ei			;85e1	fb		.
	pop hl			;85e2	e1		.
	ld a,h			;85e3	7c		|
	call l9623h	;85e4	cd 23 96	. # .
	jp l85c5h		;85e7	c3 c5 85	. . .

; BLOCK 'Text85EA' (start 0x85ea end 0x85fa)
Text85EA:
	defb 037h		;85ea	37		7
	defb 038h		;85eb	38		8
	defb 039h		;85ec	39		9
	defb 03ah		;85ed	3a		:
l85eeh:
	defb 03bh		;85ee	3b		;
	defb 03ch		;85ef	3c		<
	defb 03dh		;85f0	3d		=
	defb 03eh		;85f1	3e		>
l85f2h:
	defb 02fh		;85f2	2f		/
	defb 030h		;85f3	30		0
	defb 031h		;85f4	31		1
	defb 032h		;85f5	32		2
l85f6h:
	defb 033h		;85f6	33		3
	defb 034h		;85f7	34		4
	defb 035h		;85f8	35		5
	defb 036h		;85f9	36		6
l85fah:
	ld a,h			;85fa	7c		|
	ld ix,Text85EA	;85fb	dd 21 ea 85	. ! . .
	cp 000h			;85ff	fe 00		. .
	jr z,l8617h		;8601	28 14		( .
	ld ix,l85eeh		;8603	dd 21 ee 85	. ! . .
	cp 001h			;8607	fe 01		. .
	jr z,l8617h		;8609	28 0c		( .
	ld ix,l85f2h		;860b	dd 21 f2 85	. ! . .
	cp 002h			;860f	fe 02		. .
	jr z,l8617h		;8611	28 04		( .
	ld ix,l85f6h		;8613	dd 21 f6 85	. ! . .
l8617h:
	call sub_9630h		;8617	cd 30 96	. 0 .
DTSLS:
				; = ~DTSLS (src: DSETUP.ASM:802, BIOS-PP 1273243)
	ld hl,07e00h		;861a	21 00 7e	! . ~
	ld de,07e01h		;861d	11 01 7e	. . ~
	ld bc,001ffh		;8620	01 ff 01	. . .
	ld (hl),000h		;8623	36 00		6 .
	ldir			;8625	ed b0		. .
	call WAITHDD		;8627	cd bc 97	. . .
	ret c			;862a	d8		.
	ld a,(ix+002h)		;862b	dd 7e 02	. ~ .
	call READCMS		;862e	cd 27 9b	. ' .
	ld (07e06h),a		;8631	32 06 7e	2 . ~
	ld a,(ix+001h)		;8634	dd 7e 01	. ~ .
	call READCMS		;8637	cd 27 9b	. ' .
	push af			;863a	f5		.
	ld a,(ix+000h)		;863b	dd 7e 00	. ~ .
	call READCMS		;863e	cd 27 9b	. ' .
	pop hl			;8641	e1		.
	ld l,a			;8642	6f		o
	ld (07e02h),hl		;8643	22 02 7e	" . ~
	ld a,(ix+003h)		;8646	dd 7e 03	. ~ .
	call READCMS		;8649	cd 27 9b	. ' .
	ld (07e0ch),a		;864c	32 0c 7e	2 . ~
	ld a,0a0h		;864f	3e a0		> .
	ld bc,04152h		;8651	01 52 41	. R A
	out (c),a		;8654	ed 79		. y
	ld a,001h		;8656	3e 01		> .
	ld (IDEDEV),a		;8658	32 05 98	2 . .
NXT_IDE:
				; = NXT_IDE (src: DSETUP.ASM:864, BIOS-PP 1273243)
	call IDESPEC		;865b	cd 1c 97	. . .
	ret			;865e	c9		.
IDBUFF:

; BLOCK 'Zero865F' (start 0x865f end 0x875f)
				; = IDBUFF (src: DSETUP.ASM:867, BIOS-PP 1273243)
	defb 000h		;865f	00		.
	defb 000h		;8660	00		.
	defb 000h		;8661	00		.
	defb 000h		;8662	00		.
	defb 000h		;8663	00		.
	defb 000h		;8664	00		.
	defb 000h		;8665	00		.
	defb 000h		;8666	00		.
	defb 000h		;8667	00		.
	defb 000h		;8668	00		.
	defb 000h		;8669	00		.
	defb 000h		;866a	00		.
	defb 000h		;866b	00		.
	defb 000h		;866c	00		.
	defb 000h		;866d	00		.
	defb 000h		;866e	00		.
	defb 000h		;866f	00		.
	defb 000h		;8670	00		.
	defb 000h		;8671	00		.
	defb 000h		;8672	00		.
	defb 000h		;8673	00		.
	defb 000h		;8674	00		.
	defb 000h		;8675	00		.
	defb 000h		;8676	00		.
	defb 000h		;8677	00		.
	defb 000h		;8678	00		.
	defb 000h		;8679	00		.
	defb 000h		;867a	00		.
	defb 000h		;867b	00		.
	defb 000h		;867c	00		.
	defb 000h		;867d	00		.
	defb 000h		;867e	00		.
	defb 000h		;867f	00		.
	defb 000h		;8680	00		.
	defb 000h		;8681	00		.
	defb 000h		;8682	00		.
	defb 000h		;8683	00		.
	defb 000h		;8684	00		.
	defb 000h		;8685	00		.
	defb 000h		;8686	00		.
	defb 000h		;8687	00		.
	defb 000h		;8688	00		.
	defb 000h		;8689	00		.
	defb 000h		;868a	00		.
	defb 000h		;868b	00		.
	defb 000h		;868c	00		.
	defb 000h		;868d	00		.
	defb 000h		;868e	00		.
	defb 000h		;868f	00		.
	defb 000h		;8690	00		.
	defb 000h		;8691	00		.
	defb 000h		;8692	00		.
	defb 000h		;8693	00		.
	defb 000h		;8694	00		.
	defb 000h		;8695	00		.
	defb 000h		;8696	00		.
	defb 000h		;8697	00		.
	defb 000h		;8698	00		.
	defb 000h		;8699	00		.
	defb 000h		;869a	00		.
	defb 000h		;869b	00		.
	defb 000h		;869c	00		.
	defb 000h		;869d	00		.
	defb 000h		;869e	00		.
	defb 000h		;869f	00		.
	defb 000h		;86a0	00		.
	defb 000h		;86a1	00		.
	defb 000h		;86a2	00		.
	defb 000h		;86a3	00		.
	defb 000h		;86a4	00		.
	defb 000h		;86a5	00		.
	defb 000h		;86a6	00		.
	defb 000h		;86a7	00		.
	defb 000h		;86a8	00		.
	defb 000h		;86a9	00		.
	defb 000h		;86aa	00		.
	defb 000h		;86ab	00		.
	defb 000h		;86ac	00		.
	defb 000h		;86ad	00		.
	defb 000h		;86ae	00		.
	defb 000h		;86af	00		.
	defb 000h		;86b0	00		.
	defb 000h		;86b1	00		.
	defb 000h		;86b2	00		.
	defb 000h		;86b3	00		.
	defb 000h		;86b4	00		.
	defb 000h		;86b5	00		.
	defb 000h		;86b6	00		.
	defb 000h		;86b7	00		.
	defb 000h		;86b8	00		.
	defb 000h		;86b9	00		.
	defb 000h		;86ba	00		.
	defb 000h		;86bb	00		.
	defb 000h		;86bc	00		.
	defb 000h		;86bd	00		.
	defb 000h		;86be	00		.
	defb 000h		;86bf	00		.
	defb 000h		;86c0	00		.
	defb 000h		;86c1	00		.
	defb 000h		;86c2	00		.
	defb 000h		;86c3	00		.
	defb 000h		;86c4	00		.
	defb 000h		;86c5	00		.
	defb 000h		;86c6	00		.
	defb 000h		;86c7	00		.
	defb 000h		;86c8	00		.
	defb 000h		;86c9	00		.
	defb 000h		;86ca	00		.
	defb 000h		;86cb	00		.
	defb 000h		;86cc	00		.
	defb 000h		;86cd	00		.
	defb 000h		;86ce	00		.
	defb 000h		;86cf	00		.
	defb 000h		;86d0	00		.
	defb 000h		;86d1	00		.
	defb 000h		;86d2	00		.
	defb 000h		;86d3	00		.
	defb 000h		;86d4	00		.
	defb 000h		;86d5	00		.
	defb 000h		;86d6	00		.
	defb 000h		;86d7	00		.
	defb 000h		;86d8	00		.
	defb 000h		;86d9	00		.
	defb 000h		;86da	00		.
	defb 000h		;86db	00		.
	defb 000h		;86dc	00		.
	defb 000h		;86dd	00		.
	defb 000h		;86de	00		.
	defb 000h		;86df	00		.
	defb 000h		;86e0	00		.
	defb 000h		;86e1	00		.
	defb 000h		;86e2	00		.
	defb 000h		;86e3	00		.
	defb 000h		;86e4	00		.
	defb 000h		;86e5	00		.
	defb 000h		;86e6	00		.
	defb 000h		;86e7	00		.
	defb 000h		;86e8	00		.
	defb 000h		;86e9	00		.
	defb 000h		;86ea	00		.
	defb 000h		;86eb	00		.
	defb 000h		;86ec	00		.
	defb 000h		;86ed	00		.
	defb 000h		;86ee	00		.
	defb 000h		;86ef	00		.
	defb 000h		;86f0	00		.
	defb 000h		;86f1	00		.
	defb 000h		;86f2	00		.
	defb 000h		;86f3	00		.
	defb 000h		;86f4	00		.
	defb 000h		;86f5	00		.
	defb 000h		;86f6	00		.
	defb 000h		;86f7	00		.
	defb 000h		;86f8	00		.
	defb 000h		;86f9	00		.
	defb 000h		;86fa	00		.
	defb 000h		;86fb	00		.
	defb 000h		;86fc	00		.
	defb 000h		;86fd	00		.
	defb 000h		;86fe	00		.
	defb 000h		;86ff	00		.
	defb 000h		;8700	00		.
	defb 000h		;8701	00		.
	defb 000h		;8702	00		.
	defb 000h		;8703	00		.
	defb 000h		;8704	00		.
	defb 000h		;8705	00		.
	defb 000h		;8706	00		.
	defb 000h		;8707	00		.
	defb 000h		;8708	00		.
	defb 000h		;8709	00		.
	defb 000h		;870a	00		.
	defb 000h		;870b	00		.
	defb 000h		;870c	00		.
	defb 000h		;870d	00		.
	defb 000h		;870e	00		.
	defb 000h		;870f	00		.
	defb 000h		;8710	00		.
	defb 000h		;8711	00		.
	defb 000h		;8712	00		.
	defb 000h		;8713	00		.
	defb 000h		;8714	00		.
	defb 000h		;8715	00		.
	defb 000h		;8716	00		.
	defb 000h		;8717	00		.
	defb 000h		;8718	00		.
	defb 000h		;8719	00		.
	defb 000h		;871a	00		.
	defb 000h		;871b	00		.
	defb 000h		;871c	00		.
	defb 000h		;871d	00		.
	defb 000h		;871e	00		.
	defb 000h		;871f	00		.
	defb 000h		;8720	00		.
	defb 000h		;8721	00		.
	defb 000h		;8722	00		.
	defb 000h		;8723	00		.
	defb 000h		;8724	00		.
	defb 000h		;8725	00		.
	defb 000h		;8726	00		.
	defb 000h		;8727	00		.
	defb 000h		;8728	00		.
	defb 000h		;8729	00		.
	defb 000h		;872a	00		.
	defb 000h		;872b	00		.
	defb 000h		;872c	00		.
	defb 000h		;872d	00		.
	defb 000h		;872e	00		.
	defb 000h		;872f	00		.
	defb 000h		;8730	00		.
	defb 000h		;8731	00		.
	defb 000h		;8732	00		.
	defb 000h		;8733	00		.
	defb 000h		;8734	00		.
	defb 000h		;8735	00		.
	defb 000h		;8736	00		.
	defb 000h		;8737	00		.
	defb 000h		;8738	00		.
	defb 000h		;8739	00		.
	defb 000h		;873a	00		.
	defb 000h		;873b	00		.
	defb 000h		;873c	00		.
	defb 000h		;873d	00		.
	defb 000h		;873e	00		.
	defb 000h		;873f	00		.
	defb 000h		;8740	00		.
	defb 000h		;8741	00		.
	defb 000h		;8742	00		.
	defb 000h		;8743	00		.
	defb 000h		;8744	00		.
	defb 000h		;8745	00		.
	defb 000h		;8746	00		.
	defb 000h		;8747	00		.
	defb 000h		;8748	00		.
	defb 000h		;8749	00		.
	defb 000h		;874a	00		.
	defb 000h		;874b	00		.
	defb 000h		;874c	00		.
	defb 000h		;874d	00		.
	defb 000h		;874e	00		.
	defb 000h		;874f	00		.
	defb 000h		;8750	00		.
	defb 000h		;8751	00		.
	defb 000h		;8752	00		.
	defb 000h		;8753	00		.
	defb 000h		;8754	00		.
	defb 000h		;8755	00		.
	defb 000h		;8756	00		.
	defb 000h		;8757	00		.
	defb 000h		;8758	00		.
	defb 000h		;8759	00		.
	defb 000h		;875a	00		.
	defb 000h		;875b	00		.
	defb 000h		;875c	00		.
	defb 000h		;875d	00		.
	defb 000h		;875e	00		.
PIDNUM:
				; = PIDNUM (src: DSETUP.ASM:869, BIOS-PP 1273243)
	ld hl,IDBUFF		;875f	21 5f 86	! _ .
	ld bc,000ffh		;8762	01 ff 00	. . .
	xor a			;8765	af		.
	cpir			;8766	ed b1		. .
	ld a,(hl)		;8768	7e		~
	or a			;8769	b7		.
	ret z			;876a	c8		.
	call sub_8a08h		;876b	cd 08 8a	. . .
	ret			;876e	c9		.
sub_876fh:
	ld bc,001edh		;876f	01 ed 01	. . .
	rst 18h			;8772	df		.
	push de			;8773	d5		.
	push hl			;8774	e5		.
	push bc			;8775	c5		.
	ld a,b			;8776	78		x
	call sub_8798h		;8777	cd 98 87	. . .
	ld a,02dh		;877a	3e 2d		> -
	call PRINT		;877c	cd f8 89	. . .
	pop bc			;877f	c1		.
	ld a,c			;8780	79		y
	call sub_8798h		;8781	cd 98 87	. . .
	ld a,02dh		;8784	3e 2d		> -
	call PRINT		;8786	cd f8 89	. . .
	pop hl			;8789	e1		.
	call sub_8a20h		;878a	cd 20 8a	.   .
	pop de			;878d	d1		.
	push de			;878e	d5		.
	ld a,d			;878f	7a		z
	call sub_8798h		;8790	cd 98 87	. . .
	pop de			;8793	d1		.
	ld a,e			;8794	7b		{
	jp sub_8798h		;8795	c3 98 87	. . .
sub_8798h:
	ld d,a			;8798	57		W
	rrca			;8799	0f		.
	rrca			;879a	0f		.
	rrca			;879b	0f		.
	rrca			;879c	0f		.
	and 00fh		;879d	e6 0f		. .
	add a,030h		;879f	c6 30		. 0
	cp 03ah			;87a1	fe 3a		. :
	jr c,l87a7h		;87a3	38 02		8 .
	add a,007h		;87a5	c6 07		. .
l87a7h:
	call PRINT		;87a7	cd f8 89	. . .
	ld a,d			;87aa	7a		z
	and 00fh		;87ab	e6 0f		. .
	add a,030h		;87ad	c6 30		. 0
	cp 03ah			;87af	fe 3a		. :
	jp c,PRINT		;87b1	da f8 89	. . .
	add a,007h		;87b4	c6 07		. .
	jp PRINT		;87b6	c3 f8 89	. . .
PHEX:
				; = PHEX (src: DSETUP.ASM:909, BIOS-PP 1273243)
	ld d,a			;87b9	57		W
	rrca			;87ba	0f		.
	rrca			;87bb	0f		.
	rrca			;87bc	0f		.
	rrca			;87bd	0f		.
	and 00fh		;87be	e6 0f		. .
	add a,030h		;87c0	c6 30		. 0
	call PRINT		;87c2	cd f8 89	. . .
	ld a,d			;87c5	7a		z
	and 00fh		;87c6	e6 0f		. .
	add a,030h		;87c8	c6 30		. 0
	jp PRINT		;87ca	c3 f8 89	. . .
PRNTIME:
				; = PRNTIME (src: DSETUP.ASM:922, BIOS-PP 1273243)
	in a,(0e2h)		;87cd	db e2		. .
	push af			;87cf	f5		.
	ld a,0feh		;87d0	3e fe		> .
	out (0e2h),a		;87d2	d3 e2		. .
	ld a,(hl)		;87d4	7e		~
	call PHEX		;87d5	cd b9 87	. . .
	ld a,03ah		;87d8	3e 3a		> :
	call PRINT		;87da	cd f8 89	. . .
	inc l			;87dd	2c		,
	ld a,(hl)		;87de	7e		~
	call PHEX		;87df	cd b9 87	. . .
	ld a,03ah		;87e2	3e 3a		> :
	call PRINT		;87e4	cd f8 89	. . .
	inc l			;87e7	2c		,
	ld a,(hl)		;87e8	7e		~
	call PHEX		;87e9	cd b9 87	. . .
	pop af			;87ec	f1		.
	out (0e2h),a		;87ed	d3 e2		. .
	ret			;87ef	c9		.
PMEMORY:
				; = PMEMORY (src: DSETUP.ASM:942, BIOS-PP 1273243)
	add hl,hl		;87f0	29		)
	add hl,hl		;87f1	29		)
	add hl,hl		;87f2	29		)
	add hl,hl		;87f3	29		)
	call IPRINT		;87f4	cd 24 8a	. $ .
	ld a,04bh		;87f7	3e 4b		> K
	jp PRINT		;87f9	c3 f8 89	. . .
ZXCLS:
				; = ZXCLS (src: DSETUP.ASM:950, BIOS-PP 1273243)
	ld hl,04000h		;87fc	21 00 40	! . @
	ld de,04001h		;87ff	11 01 40	. . @
	ld bc,01affh		;8802	01 ff 1a	. . .
	ld (hl),l		;8805	75		u
	ldir			;8806	ed b0		. .
	ret			;8808	c9		.
GET_ID:
				; = GET_ID (src: DSETUP.ASM:957, BIOS-PP 1273243)
	ld hl,IDBUFF		;8809	21 5f 86	! _ .
	ld c,0efh		;880c	0e ef		. .
	rst 18h			;880e	df		.
	ret			;880f	c9		.
GETTIME:
				; = GETTIME (src: DSETUP.ASM:983, BIOS-PP 1273243)
	ld d,004h		;8810	16 04		. .
	ld c,0f6h		;8812	0e f6		. .
	call 00018h		;8814	cd 18 00	. . .
	ld (hl),a		;8817	77		w
	inc hl			;8818	23		#
	ld d,002h		;8819	16 02		. .
	ld c,0f6h		;881b	0e f6		. .
	call 00018h		;881d	cd 18 00	. . .
	ld (hl),a		;8820	77		w
	inc hl			;8821	23		#
	ld d,000h		;8822	16 00		. .
	ld c,0f6h		;8824	0e f6		. .
	call 00018h		;8826	cd 18 00	. . .
	ld (hl),a		;8829	77		w
	inc hl			;882a	23		#
	ld d,007h		;882b	16 07		. .
	ld c,0f6h		;882d	0e f6		. .
	call 00018h		;882f	cd 18 00	. . .
	ld (hl),a		;8832	77		w
	inc hl			;8833	23		#
	ld d,008h		;8834	16 08		. .
	ld c,0f6h		;8836	0e f6		. .
	call 00018h		;8838	cd 18 00	. . .
	ld (hl),a		;883b	77		w
	inc hl			;883c	23		#
	ld d,032h		;883d	16 32		. 2
	ld c,0f6h		;883f	0e f6		. .
	call 00018h		;8841	cd 18 00	. . .
	ld (hl),a		;8844	77		w
	inc hl			;8845	23		#
	ld d,009h		;8846	16 09		. .
	ld c,0f6h		;8848	0e f6		. .
	call 00018h		;884a	cd 18 00	. . .
	ld (hl),a		;884d	77		w
	dec hl			;884e	2b		+
	ld c,a			;884f	4f		O
	ld a,019h		;8850	3e 19		> .
	cp (hl)			;8852	be		.
	ret z			;8853	c8		.
	ld a,020h		;8854	3e 20		>  
	cp (hl)			;8856	be		.
	ret z			;8857	c8		.
	ld a,080h		;8858	3e 80		> .
	cp c			;885a	b9		.
	ld a,020h		;885b	3e 20		>  
	jr c,WCU		;885d	38 02		8 .
	ld a,019h		;885f	3e 19		> .
WCU:
				; = WCU (src: DSETUP.ASM:1030, BIOS-PP 1273243)
	ld (hl),a		;8861	77		w
	ld d,032h		;8862	16 32		. 2
	ld c,0f7h		;8864	0e f7		. .
	call 00018h		;8866	cd 18 00	. . .
	ret			;8869	c9		.
CLEARM:
				; = CLEARM (src: DSETUP.ASM:1036, BIOS-PP 1273243)
	in a,(0e2h)		;886a	db e2		. .
	push af			;886c	f5		.
	ld a,0feh		;886d	3e fe		> .
	out (0e2h),a		;886f	d3 e2		. .
	ld hl,0c180h		;8871	21 80 c1	! . .
	ld de,07de0h		;8874	11 e0 7d	. . }
	ld bc,00010h		;8877	01 10 00	. . .
	ldir			;887a	ed b0		. .
	ld hl,07e00h		;887c	21 00 7e	! . ~
	ld de,07e01h		;887f	11 01 7e	. . ~
	ld bc,000ffh		;8882	01 ff 00	. . .
	ld (hl),000h		;8885	36 00		6 .
	ldir			;8887	ed b0		. .
	ld ix,0c180h		;8889	dd 21 80 c1	. ! . .
	ld h,0c2h		;888d	26 c2		& .
	ld d,07eh		;888f	16 7e		. ~
	ld bc,010ffh		;8891	01 ff 10	. . .
MEMLOOP:
				; = MEMLOOP (src: DSETUP.ASM:1053, BIOS-PP 1273243)
	ld a,(ix+000h)		;8894	dd 7e 00	. ~ .
	inc ix			;8897	dd 23		. #
	or a			;8899	b7		.
	ld l,a			;889a	6f		o
	call nz,RCHAIN		;889b	c4 b3 88	. . .
	djnz MEMLOOP		;889e	10 f4		. .
	ld c,0c1h		;88a0	0e c1		. .
	call 00018h		;88a2	cd 18 00	. . .
	ld bc,0400eh		;88a5	01 0e 40	. . @
	call G_VALUE		;88a8	cd 19 9b	. . .
	or a			;88ab	b7		.
	call nz,CCHAIN		;88ac	c4 bd 88	. . .
	pop af			;88af	f1		.
	out (0e2h),a		;88b0	d3 e2		. .
	ret			;88b2	c9		.
RCHAIN:
				; = RCHAIN (src: DSETUP.ASM:1069, BIOS-PP 1273243)
	ld e,l			;88b3	5d		]
	ldi			;88b4	ed a0		. .
	dec l			;88b6	2d		-
	ld l,(hl)		;88b7	6e		n
	inc l			;88b8	2c		,
	ret z			;88b9	c8		.
	dec l			;88ba	2d		-
	jr RCHAIN		;88bb	18 f6		. .
CCHAIN:
				; = CCHAIN (src: DSETUP.ASM:1078, BIOS-PP 1273243)
	ld hl,07e00h		;88bd	21 00 7e	! . ~
	ld de,0c200h		;88c0	11 00 c2	. . .
	xor a			;88c3	af		.
CCHAINC:
				; = CCHAINC (src: DSETUP.ASM:1081, BIOS-PP 1273243)
	cp (hl)			;88c4	be		.
	jr z,NOCOPYC		;88c5	28 04		( .
	ld e,l			;88c7	5d		]
	ldi			;88c8	ed a0		. .
	dec l			;88ca	2d		-
NOCOPYC:
				; = NOCOPYC (src: DSETUP.ASM:1086, BIOS-PP 1273243)
	inc l			;88cb	2c		,
	jr nz,CCHAINC		;88cc	20 f6		  .
	ld hl,07de0h		;88ce	21 e0 7d	! . }
	ld de,0c180h		;88d1	11 80 c1	. . .
	ld bc,00010h		;88d4	01 10 00	. . .
	ldir			;88d7	ed b0		. .
	ret			;88d9	c9		.
TRQUICK:
				; = TRQUICK (src: DSETUP.ASM:1094, BIOS-PP 1273243)
	ld bc,0031eh		;88da	01 1e 03	. . .
	ld a,000h		;88dd	3e 00		> .
	call TRDOSX		;88df	cd f7 88	. . .
	ld bc,00c1eh		;88e2	01 1e 0c	. . .
	ld a,001h		;88e5	3e 01		> .
	call TRDOSX		;88e7	cd f7 88	. . .
	ld bc,0301eh		;88ea	01 1e 30	. . 0
	ld a,002h		;88ed	3e 02		> .
	call TRDOSX		;88ef	cd f7 88	. . .
	ld bc,0c01eh		;88f2	01 1e c0	. . .
	ld a,003h		;88f5	3e 03		> .
TRDOSX:
				; = TRDOSX (src: DSETUP.ASM:1112, BIOS-PP 1273243)
	push af			;88f7	f5		.
	call G_VALUE		;88f8	cd 19 9b	. . .
	pop bc			;88fb	c1		.
	or a			;88fc	b7		.
	ret z			;88fd	c8		.
	ld c,0cch		;88fe	0e cc		. .
	dec a			;8900	3d		=
	jp z,00018h		;8901	ca 18 00	. . .
	ld c,0cdh		;8904	0e cd		. .
	dec a			;8906	3d		=
	jp z,00018h		;8907	ca 18 00	. . .
	scf			;890a	37		7
	ret			;890b	c9		.
OPENDOS:
				; = OPENDOS (src: DSETUP.ASM:1126, BIOS-PP 1273243)
	di			;890c	f3		.
	in a,(0e2h)		;890d	db e2		. .
	ex af,af'		;890f	08		.
	ld a,040h		;8910	3e 40		> @
	out (0e2h),a		;8912	d3 e2		. .
	ld hl,0c000h		;8914	21 00 c0	! . .
	ld de,0f000h		;8917	11 00 f0	. . .
	ld bc,00400h		;891a	01 00 04	. . .
	ldir			;891d	ed b0		. .
	ld a,040h		;891f	3e 40		> @
	ld (0f26eh),a		;8921	32 6e f2	2 n .
	ld (0f27eh),a		;8924	32 7e f2	2 ~ .
	ld (0f2eeh),a		;8927	32 ee f2	2 . .
	ld (0f2feh),a		;892a	32 fe f2	2 . .
	ld (0f36eh),a		;892d	32 6e f3	2 n .
	ld (0f37eh),a		;8930	32 7e f3	2 ~ .
	ld (0f3eeh),a		;8933	32 ee f3	2 . .
	ld (0f3feh),a		;8936	32 fe f3	2 . .
	ld hl,0f000h		;8939	21 00 f0	! . .
	ld de,0f400h		;893c	11 00 f4	. . .
	ld bc,00c00h		;893f	01 00 0c	. . .
	ldir			;8942	ed b0		. .
	ex af,af'		;8944	08		.
	out (0e2h),a		;8945	d3 e2		. .
	ret			;8947	c9		.
DOUBLE:
				; = DOUBLE (src: DSETUP.ASM:1152, BIOS-PP 1273243)
	ld hl,SDOUBLE		;8948	21 68 89	! h .
	jr SETELEM		;894b	18 12		. .
SINGLE:
				; = SINGLE (src: DSETUP.ASM:1155, BIOS-PP 1273243)
	ld hl,SSINGLE		;894d	21 72 89	! r .
	jr SETELEM		;8950	18 0d		. .
HIGH:
				; = HIGH (src: DSETUP.ASM:1158, BIOS-PP 1273243)
	ld hl,SSIN_DW		;8952	21 7c 89	! | .
	jr SETELEM		;8955	18 08		. .

; BLOCK 'Data8957' (start 0x8957 end 0x895c)
Data8957:
	defb 021h		;8957	21		!
	defb 086h		;8958	86		.
	defb 089h		;8959	89		.
	defb 018h		;895a	18		.
	defb 003h		;895b	03		.
MEDIUM:
				; = MEDIUM (src: DSETUP.ASM:1164, BIOS-PP 1273243)
	ld hl,SSIN_AL		;895c	21 90 89	! . .
SETELEM:
				; = SETELEM (src: DSETUP.ASM:1165, BIOS-PP 1273243)
	ld de,SELEM		;895f	11 9a 89	. . .
	ld bc,0000ah		;8962	01 0a 00	. . .
	ldir			;8965	ed b0		. .
	ret			;8967	c9		.
SDOUBLE:

; BLOCK 'Data8968' (start 0x8968 end 0x89a4)
				; = SDOUBLE (src: DSETUP.ASM:1170, BIOS-PP 1273243)
	defb 0c9h		;8968	c9		.
	defb 0bbh		;8969	bb		.
	defb 0cdh		;896a	cd		.
	defb 0bah		;896b	ba		.
	defb 0c8h		;896c	c8		.
	defb 0bch		;896d	bc		.
	defb 0cch		;896e	cc		.
	defb 0b9h		;896f	b9		.
	defb 0cbh		;8970	cb		.
	defb 0cah		;8971	ca		.
SSINGLE:
				; = SSINGLE (src: DSETUP.ASM:1183, BIOS-PP 1273243)
	defb 0dah		;8972	da		.
	defb 0bfh		;8973	bf		.
	defb 0c4h		;8974	c4		.
	defb 0b3h		;8975	b3		.
	defb 0c0h		;8976	c0		.
	defb 0d9h		;8977	d9		.
	defb 0c3h		;8978	c3		.
	defb 0b4h		;8979	b4		.
	defb 0c2h		;897a	c2		.
	defb 0c1h		;897b	c1		.
SSIN_DW:
				; = SSIN_DW (src: DSETUP.ASM:1196, BIOS-PP 1273243)
	defb 0c9h		;897c	c9		.
	defb 0bbh		;897d	bb		.
	defb 0c4h		;897e	c4		.
	defb 0bah		;897f	ba		.
	defb 0c7h		;8980	c7		.
	defb 0b6h		;8981	b6		.
	defb 0c7h		;8982	c7		.
	defb 0b6h		;8983	b6		.
	defb 0d1h		;8984	d1		.
	defb 0c1h		;8985	c1		.
SSIN_UP:
				; = SSIN_UP (src: DSETUP.ASM:1209, BIOS-PP 1273243)
	defb 0c7h		;8986	c7		.
	defb 0b6h		;8987	b6		.
	defb 0c4h		;8988	c4		.
	defb 0bah		;8989	ba		.
	defb 0c8h		;898a	c8		.
	defb 0bch		;898b	bc		.
	defb 0c7h		;898c	c7		.
	defb 0b6h		;898d	b6		.
	defb 0c2h		;898e	c2		.
	defb 0cfh		;898f	cf		.
SSIN_AL:
				; = SSIN_AL (src: DSETUP.ASM:1222, BIOS-PP 1273243)
	defb 0c7h		;8990	c7		.
	defb 0b6h		;8991	b6		.
	defb 0c4h		;8992	c4		.
	defb 0bah		;8993	ba		.
	defb 0c7h		;8994	c7		.
	defb 0b6h		;8995	b6		.
	defb 0c7h		;8996	c7		.
	defb 0b6h		;8997	b6		.
	defb 0c2h		;8998	c2		.
	defb 0c1h		;8999	c1		.
SELEM:
				; = SELEM (src: DSETUP.ASM:1235, BIOS-PP 1273243)
				; = UL (src: DSETUP.ASM:1236, BIOS-PP 1273243)
	defb 0dah		;899a	da		.
UR:
				; = UR (src: DSETUP.ASM:1237, BIOS-PP 1273243)
	defb 0bfh		;899b	bf		.
ZL:
				; = ZL (src: DSETUP.ASM:1238, BIOS-PP 1273243)
	defb 0c4h		;899c	c4		.
VL:
				; = VL (src: DSETUP.ASM:1239, BIOS-PP 1273243)
	defb 0b3h		;899d	b3		.
LL:
				; = LL (src: DSETUP.ASM:1240, BIOS-PP 1273243)
	defb 0c0h		;899e	c0		.
LR:
				; = LR (src: DSETUP.ASM:1241, BIOS-PP 1273243)
	defb 0d9h		;899f	d9		.
LC:
				; = LC (src: DSETUP.ASM:1243, BIOS-PP 1273243)
	defb 0c3h		;89a0	c3		.
RC:
				; = RC (src: DSETUP.ASM:1244, BIOS-PP 1273243)
	defb 0b4h		;89a1	b4		.
UC:
				; = UC (src: DSETUP.ASM:1245, BIOS-PP 1273243)
	defb 0c2h		;89a2	c2		.
DC:
				; = DC (src: DSETUP.ASM:1246, BIOS-PP 1273243)
	defb 0c1h		;89a3	c1		.
GET_CUR:
				; = GET_CUR (src: VIDEO_IO.ASM:19, BIOS-PP 1273243)
	ld c,08eh		;89a4	0e 8e		. .
	jp 00018h		;89a6	c3 18 00	. . .
SUBNAME:
				; = SUBNAME (src: VIDEO_IO.ASM:22, BIOS-PP 1273243)
	call GET_CUR	;89a9	cd a4 89	. . .
	ld e,024h		;89ac	1e 24		. $
LOCAT:
				; = LOCAT (src: VIDEO_IO.ASM:25, BIOS-PP 1273243)
	ld c,084h		;89ae	0e 84		. .
	jp 00018h		;89b0	c3 18 00	. . .
CRLF:
				; = CRLF (src: VIDEO_IO.ASM:28, BIOS-PP 1273243)
	call GET_CUR	;89b3	cd a4 89	. . .
	inc d			;89b6	14		.
	ld e,000h		;89b7	1e 00		. .
	jp LOCAT		;89b9	c3 ae 89	. . .
MODEL:
				; = MODEL (src: VIDEO_IO.ASM:33, BIOS-PP 1273243)
	ld hl,07e36h		;89bc	21 36 7e	! 6 ~
	ld a,(hl)		;89bf	7e		~
	or a			;89c0	b7		.
	jr z,DL1		;89c1	28 07		( .
	ld b,014h		;89c3	06 14		. .
	call DWPRINT		;89c5	cd d1 89	. . .
	and a			;89c8	a7		.
	ret			;89c9	c9		.
DL1:
				; = DL1 (src: VIDEO_IO.ASM:42, BIOS-PP 1273243)
	ld a,010h		;89ca	3e 10		> .
	call POSTMSG		;89cc	cd db b5	. . .
	and a			;89cf	a7		.
	ret			;89d0	c9		.
DWPRINT:
				; = DWPRINT (src: VIDEO_IO.ASM:47, BIOS-PP 1273243)
	ld a,(hl)		;89d1	7e		~
	cp 020h			;89d2	fe 20		.  
	jr nz,PRINTDW		;89d4	20 0c		  .
	inc hl			;89d6	23		#
	ld a,(hl)		;89d7	7e		~
	dec hl			;89d8	2b		+
	cp 020h			;89d9	fe 20		.  
	jr nz,PRINTDW		;89db	20 05		  .
	inc hl			;89dd	23		#
	inc hl			;89de	23		#
	djnz DWPRINT		;89df	10 f0		. .
	ret			;89e1	c9		.
PRINTDW:
				; = PRINTDW (src: VIDEO_IO.ASM:60, BIOS-PP 1273243)
	push bc			;89e2	c5		.
	ld e,(hl)		;89e3	5e		^
	inc hl			;89e4	23		#
	ld a,(hl)		;89e5	7e		~
	inc hl			;89e6	23		#
	call PRINT		;89e7	cd f8 89	. . .
	ld a,e			;89ea	7b		{
	call PRINT		;89eb	cd f8 89	. . .
	pop bc			;89ee	c1		.
	djnz PRINTDW		;89ef	10 f1		. .
	ret			;89f1	c9		.
PRINTA:
				; = PRINTA (src: VIDEO_IO.ASM:72, BIOS-PP 1273243)
	ld c,083h		;89f2	0e 83		. .
	ld e,a			;89f4	5f		_
	jp 00018h		;89f5	c3 18 00	. . .
PRINT:
				; = PRINT (src: VIDEO_IO.ASM:76, BIOS-PP 1273243)
	ld bc,00182h		;89f8	01 82 01	. . .
	jp 00018h		;89fb	c3 18 00	. . .
TPRINTZ:
				; = TPRINTZ (src: VIDEO_IO.ASM:79, BIOS-PP 1273243)
	call LOCAT		;89fe	cd ae 89	. . .
	ld bc,0008ch		;8a01	01 8c 00	. . .
	ld d,b			;8a04	50		P
	jp 00018h		;8a05	c3 18 00	. . .
sub_8a08h:
	ld bc,0008ch		;8a08	01 8c 00	. . .
	ld d,b			;8a0b	50		P
	jp 00018h		;8a0c	c3 18 00	. . .
PRINTZ:
				; = PRINTZ (src: VIDEO_IO.ASM:84, BIOS-PP 1273243)
	ld bc,0008bh		;8a0f	01 8b 00	. . .
	ld d,b			;8a12	50		P
	ld e,00fh		;8a13	1e 0f		. .
	jp 00018h		;8a15	c3 18 00	. . .
CPRINTZ:
				; = CPRINTZ (src: VIDEO_IO.ASM:93, BIOS-PP 1273243)
	ld bc,0008bh		;8a18	01 8b 00	. . .
	ld d,b			;8a1b	50		P
	ld e,a			;8a1c	5f		_
	jp 00018h		;8a1d	c3 18 00	. . .
sub_8a20h:
	ld d,001h		;8a20	16 01		. .
	jr l8a26h		;8a22	18 02		. .
IPRINT:
				; = IPRINT (src: VIDEO_IO.ASM:100, BIOS-PP 1273243)
	ld d,000h		;8a24	16 00		. .
l8a26h:
	ld bc,02710h		;8a26	01 10 27	. . '
	call PRINTDG		;8a29	cd 44 8a	. D .
	ld bc,003e8h		;8a2c	01 e8 03	. . .
	call PRINTDG		;8a2f	cd 44 8a	. D .
	ld bc,00064h		;8a32	01 64 00	. d .
	call PRINTDG		;8a35	cd 44 8a	. D .
	ld bc,0000ah		;8a38	01 0a 00	. . .
	call PRINTDG		;8a3b	cd 44 8a	. D .
	ld a,l			;8a3e	7d		}
	add a,030h		;8a3f	c6 30		. 0
	jp PRINT		;8a41	c3 f8 89	. . .
PRINTDG:
				; = PRINTDG (src: VIDEO_IO.ASM:113, BIOS-PP 1273243)
	ld a,02fh		;8a44	3e 2f		> /
PDG1:
				; = PDG1 (src: VIDEO_IO.ASM:114, BIOS-PP 1273243)
	inc a			;8a46	3c		<
	sbc hl,bc		;8a47	ed 42		. B
	jr nc,PDG1		;8a49	30 fb		0 .
	add hl,bc		;8a4b	09		.
	bit 0,d			;8a4c	cb 42		. B
	jr nz,PDG2		;8a4e	20 05		  .
	cp 030h			;8a50	fe 30		. 0
	ret z			;8a52	c8		.
	set 0,d			;8a53	cb c2		. .
PDG2:
				; = PDG2 (src: VIDEO_IO.ASM:123, BIOS-PP 1273243)
	push bc			;8a55	c5		.
	call PRINT		;8a56	cd f8 89	. . .
	pop bc			;8a59	c1		.
	ret			;8a5a	c9		.
TLINEV:
				; = TLINEV (src: VIDEO_IO.ASM:132, BIOS-PP 1273243)
	call LOCAT		;8a5b	cd ae 89	. . .
	ld a,(UC)		;8a5e	3a a2 89	: . .
	call PRSYM		;8a61	cd f3 8a	. . .
	dec h			;8a64	25		%
	dec h			;8a65	25		%
TLINEV1:
				; = TLINEV1 (src: VIDEO_IO.ASM:137, BIOS-PP 1273243)
	inc d			;8a66	14		.
	call LOCAT		;8a67	cd ae 89	. . .
	ld a,(VL)		;8a6a	3a 9d 89	: . .
	call PRSYM		;8a6d	cd f3 8a	. . .
	dec h			;8a70	25		%
	jr nz,TLINEV1		;8a71	20 f3		  .
	inc d			;8a73	14		.
	call LOCAT		;8a74	cd ae 89	. . .
	ld a,(DC)		;8a77	3a a3 89	: . .
	jp PRSYM		;8a7a	c3 f3 8a	. . .
TLINEH:
				; = TLINEH (src: VIDEO_IO.ASM:152, BIOS-PP 1273243)
	call LOCAT		;8a7d	cd ae 89	. . .
	ld a,(LC)		;8a80	3a a0 89	: . .
	call PRSYM		;8a83	cd f3 8a	. . .
	dec l			;8a86	2d		-
	dec l			;8a87	2d		-
	ld a,(ZL)		;8a88	3a 9c 89	: . .
	ld b,l			;8a8b	45		E
	call PRSYMB		;8a8c	cd f9 8a	. . .
	ld a,(RC)		;8a8f	3a a1 89	: . .
	jp PRSYM		;8a92	c3 f3 8a	. . .
PBORDER:
				; = PBORDER (src: VIDEO_IO.ASM:168, BIOS-PP 1273243)
	push bc			;8a95	c5		.
	ld a,c			;8a96	79		y
	dec a			;8a97	3d		=
	ld (BSHI+1),a		;8a98	32 cb 8a	2 . .
	dec a			;8a9b	3d		=
	ld (BHOR+1),a		;8a9c	32 b0 8a	2 . .
	ld (BHOR2+1),a		;8a9f	32 e8 8a	2 . .
	call LOCAT		;8aa2	cd ae 89	. . .
	ld a,(SELEM)		;8aa5	3a 9a 89	: . .
	ld h,e			;8aa8	63		c
	call PRSYM		;8aa9	cd f3 8a	. . .
	ld a,(ZL)		;8aac	3a 9c 89	: . .
BHOR:
				; = BHOR (src: VIDEO_IO.ASM:180, BIOS-PP 1273243)
	ld b,001h		;8aaf	06 01		. .
	call PRSYMB		;8ab1	cd f9 8a	. . .
	ld a,(UR)		;8ab4	3a 9b 89	: . .
	call PRSYM		;8ab7	cd f3 8a	. . .
	pop bc			;8aba	c1		.
	dec b			;8abb	05		.
	dec b			;8abc	05		.
	inc d			;8abd	14		.
	ld e,h			;8abe	5c		\
BHH:
				; = BHH (src: VIDEO_IO.ASM:189, BIOS-PP 1273243)
	push bc			;8abf	c5		.
	call LOCAT		;8ac0	cd ae 89	. . .
	ld a,(VL)		;8ac3	3a 9d 89	: . .
	call PRSYM		;8ac6	cd f3 8a	. . .
	ld a,h			;8ac9	7c		|
BSHI:
				; = BSHI (src: VIDEO_IO.ASM:194, BIOS-PP 1273243)
	add a,000h		;8aca	c6 00		. .
	ld e,a			;8acc	5f		_
	call LOCAT		;8acd	cd ae 89	. . .
	ld a,(VL)		;8ad0	3a 9d 89	: . .
	call PRSYM		;8ad3	cd f3 8a	. . .
	pop bc			;8ad6	c1		.
	ld e,h			;8ad7	5c		\
	inc d			;8ad8	14		.
	djnz BHH		;8ad9	10 e4		. .
	call LOCAT		;8adb	cd ae 89	. . .
	ld a,(LL)		;8ade	3a 9e 89	: . .
	call PRSYM		;8ae1	cd f3 8a	. . .
	ld a,(ZL)		;8ae4	3a 9c 89	: . .
BHOR2:
				; = BHOR2 (src: VIDEO_IO.ASM:207, BIOS-PP 1273243)
	ld b,001h		;8ae7	06 01		. .
	call PRSYMB		;8ae9	cd f9 8a	. . .
	ld a,(LR)		;8aec	3a 9f 89	: . .
	call PRSYM		;8aef	cd f3 8a	. . .
	ret			;8af2	c9		.
PRSYM:
				; = PRSYM (src: VIDEO_IO.ASM:213, BIOS-PP 1273243)
	ld bc,00182h		;8af3	01 82 01	. . .
	jp 00018h		;8af6	c3 18 00	. . .
PRSYMB:
				; = PRSYMB (src: VIDEO_IO.ASM:217, BIOS-PP 1273243)
	ld c,082h		;8af9	0e 82		. .
	jp 00018h		;8afb	c3 18 00	. . .
LOGOTYP:
				; = LOGOTYP (src: VIDEO_IO.ASM:221, BIOS-PP 1273243)
	call LOGOX	;8afe	cd 4b 8b	. K .
	ld bc,0180eh		;8b01	01 0e 18	. . .
	call G_VALUE		;8b04	cd 19 9b	. . .
	or a			;8b07	b7		.
	jr z,EASYDLY		;8b08	28 08		( .
	dec a			;8b0a	3d		=
	jr z,l8b21h	;8b0b	28 14		( .
	dec a			;8b0d	3d		=
	jr z,l8b1bh	;8b0e	28 0b		( .
	jr l8b1bh		;8b10	18 09		. .
EASYDLY:
				; = EASYDLY (src: VIDEO_IO.ASM:231, BIOS-PP 1273243)
	ld b,019h		;8b12	06 19		. .
	ei			;8b14	fb		.
	halt			;8b15	76		v

; BLOCK 'Data8B16' (start 0x8b16 end 0x8b1b)
Data8B16:
	defb 010h		;8b16	10		.
	defb 0fch		;8b17	fc		.
	defb 0f3h		;8b18	f3		.
	defb 018h		;8b19	18		.
	defb 012h		;8b1a	12		.
l8b1bh:
	ld b,064h		;8b1b	06 64		. d
	ei			;8b1d	fb		.
	halt			;8b1e	76		v

; BLOCK 'Data8B1F' (start 0x8b1f end 0x8b21)
Data8B1F:
	defb 010h		;8b1f	10		.
	defb 0fch		;8b20	fc		.
l8b21h:
	ld b,082h		;8b21	06 82		. .
	push bc			;8b23	c5		.
	ei			;8b24	fb		.
	halt			;8b25	76		v

; BLOCK 'Data8B26' (start 0x8b26 end 0x8b4b)
Data8B26:
	defb 0f3h		;8b26	f3		.
	defb 0cdh		;8b27	cd		.
	defb 08fh		;8b28	8f		.
	defb 08bh		;8b29	8b		.
	defb 0c1h		;8b2a	c1		.
	defb 010h		;8b2b	10		.
	defb 0f6h		;8b2c	f6		.
	defb 0ddh		;8b2d	dd		.
	defb 021h		;8b2e	21		!
	defb 07fh		;8b2f	7f		.
	defb 08bh		;8b30	8b		.
	defb 021h		;8b31	21		!
	defb 000h		;8b32	00		.
	defb 000h		;8b33	00		.
	defb 01eh		;8b34	1e		.
	defb 001h		;8b35	01		.
	defb 00eh		;8b36	0e		.
	defb 0b0h		;8b37	b0		.
	defb 0fbh		;8b38	fb		.
	defb 076h		;8b39	76		v
	defb 0f3h		;8b3a	f3		.
	defb 0cdh		;8b3b	cd		.
	defb 018h		;8b3c	18		.
	defb 000h		;8b3d	00		.
	defb 011h		;8b3e	11		.
	defb 000h		;8b3f	00		.
	defb 000h		;8b40	00		.
	defb 021h		;8b41	21		!
	defb 020h		;8b42	20		 
	defb 008h		;8b43	08		.
	defb 001h		;8b44	01		.
	defb 089h		;8b45	89		.
	defb 007h		;8b46	07		.
	defb 0cdh		;8b47	cd		.
	defb 018h		;8b48	18		.
	defb 000h		;8b49	00		.
	defb 0c9h		;8b4a	c9		.
LOGOX:
				; = LOGOX (src: VIDEO_IO.ASM:263, BIOS-PP 1273243)
	ld ix,TAB2		;8b4b	dd 21 87 8b	. ! . .
	ld hl,00000h		;8b4f	21 00 00	! . .
	ld e,001h		;8b52	1e 01		. .
	ld c,0b0h		;8b54	0e b0		. .
	call 00018h		;8b56	cd 18 00	. . .
	ld a,0c0h		;8b59	3e c0		> .
	out (089h),a		;8b5b	d3 89		. .
	ld hl,LOGPAL		;8b5d	21 00 8c	! . .
	ld de,PALCOL		;8b60	11 40 8c	. @ .
	ld bc,00040h		;8b63	01 40 00	. @ .
	ldir			;8b66	ed b0		. .
	ld hl,PALCOL		;8b68	21 40 8c	! @ .
	ld de,01000h		;8b6b	11 00 10	. . .
	ld bc,0ffa4h		;8b6e	01 a4 ff	. . .
	xor a			;8b71	af		.
	call 00018h		;8b72	cd 18 00	. . .
	ld a,0c0h		;8b75	3e c0		> .
	out (089h),a		;8b77	d3 89		. .
	ld hl,LOGO		;8b79	21 50 8d	! P .
	jp DECODE		;8b7c	c3 d5 8b	. . .
TAB1:

; BLOCK 'Data8B7F' (start 0x8b7f end 0x8b8f)
				; = TAB1 (src: VIDEO_IO.ASM:286, BIOS-PP 1273243)
	defb 028h		;8b7f	28		(
	defb 020h		;8b80	20		 
	defb 000h		;8b81	00		.
	defb 000h		;8b82	00		.
	defb 01bh		;8b83	1b		.
	defb 000h		;8b84	00		.
	defb 000h		;8b85	00		.
	defb 000h		;8b86	00		.
TAB2:
				; = TAB2 (src: VIDEO_IO.ASM:288, BIOS-PP 1273243)
	defb 010h		;8b87	10		.
	defb 008h		;8b88	08		.
	defb 000h		;8b89	00		.
	defb 000h		;8b8a	00		.
	defb 000h		;8b8b	00		.
	defb 000h		;8b8c	00		.
	defb 008h		;8b8d	08		.
	defb 000h		;8b8e	00		.
FADE:
				; = FADE (src: VIDEO_IO.ASM:290, BIOS-PP 1273243)
	ld hl,PALCOL		;8b8f	21 40 8c	! @ .
	ld b,010h		;8b92	06 10		. .
l8b94h:
	ld a,(hl)		;8b94	7e		~
	or a			;8b95	b7		.
	jr z,l8b99h		;8b96	28 01		( .
	dec (hl)		;8b98	35		5
l8b99h:
	inc hl			;8b99	23		#
	ld a,(hl)		;8b9a	7e		~
	or a			;8b9b	b7		.
	jr z,l8b9fh		;8b9c	28 01		( .
	dec (hl)		;8b9e	35		5
l8b9fh:
	inc hl			;8b9f	23		#
	ld a,(hl)		;8ba0	7e		~
	or a			;8ba1	b7		.
	jr z,l8ba5h		;8ba2	28 01		( .
	dec (hl)		;8ba4	35		5
l8ba5h:
	inc hl			;8ba5	23		#
	inc hl			;8ba6	23		#
	djnz l8b94h		;8ba7	10 eb		. .
	ld hl,PALCOL		;8ba9	21 40 8c	! @ .
	ld b,010h		;8bac	06 10		. .
l8baeh:
	ld a,(hl)		;8bae	7e		~
	or a			;8baf	b7		.
	jr z,l8bb3h		;8bb0	28 01		( .
	dec (hl)		;8bb2	35		5
l8bb3h:
	inc hl			;8bb3	23		#
	ld a,(hl)		;8bb4	7e		~
	or a			;8bb5	b7		.
	jr z,l8bb9h		;8bb6	28 01		( .
	dec (hl)		;8bb8	35		5
l8bb9h:
	inc hl			;8bb9	23		#
	ld a,(hl)		;8bba	7e		~
	or a			;8bbb	b7		.
	jr z,l8bbfh		;8bbc	28 01		( .
	dec (hl)		;8bbe	35		5
l8bbfh:
	inc hl			;8bbf	23		#
	inc hl			;8bc0	23		#
	djnz l8baeh		;8bc1	10 eb		. .
	ld hl,PALCOL		;8bc3	21 40 8c	! @ .
	ld de,01000h		;8bc6	11 00 10	. . .
	ld bc,0ffa4h		;8bc9	01 a4 ff	. . .
	xor a			;8bcc	af		.
	call 00018h		;8bcd	cd 18 00	. . .
	ld a,0c0h		;8bd0	3e c0		> .
	out (089h),a		;8bd2	d3 89		. .
	ret			;8bd4	c9		.
DECODE:
				; = DECODE (src: VIDEO_IO.ASM:337, BIOS-PP 1273243)
	push hl			;8bd5	e5		.
	in a,(0a2h)		;8bd6	db a2		. .
	ld h,a			;8bd8	67		g
	in a,(089h)		;8bd9	db 89		. .
	ld l,a			;8bdb	6f		o
	ex (sp),hl		;8bdc	e3		.
	ld a,050h		;8bdd	3e 50		> P
	out (0a2h),a		;8bdf	d3 a2		. .
	ld hl,0d900h		;8be1	21 00 d9	! . .
	ld de,04040h		;8be4	11 40 40	. @ @
	ld a,048h		;8be7	3e 48		> H
LOGO0:
				; = LOGO0 (src: VIDEO_IO.ASM:348, BIOS-PP 1273243)
	push de			;8be9	d5		.
	dec a			;8bea	3d		=
	out (089h),a		;8beb	d3 89		. .
	ld bc,00080h		;8bed	01 80 00	. . .
	ldir			;8bf0	ed b0		. .
	pop de			;8bf2	d1		.
	or a			;8bf3	b7		.
	jp nz,LOGO0		;8bf4	c2 e9 8b	. . .
	pop bc			;8bf7	c1		.
	ld a,b			;8bf8	78		x
	out (0a2h),a		;8bf9	d3 a2		. .
	ld a,c			;8bfb	79		y
	out (089h),a		;8bfc	d3 89		. .
	xor a			;8bfe	af		.
	ret			;8bff	c9		.
LOGPAL:

; BLOCK 'Data8C00' (start 0x8c00 end 0x8c80)
				; = LOGPAL (src: VIDEO_IO.ASM:364, BIOS-PP 1273243)
	defb 0ffh		;8c00	ff		.
	defb 0ffh		;8c01	ff		.
	defb 0ffh		;8c02	ff		.
	defb 000h		;8c03	00		.
	defb 08ch		;8c04	8c		.
	defb 0a5h		;8c05	a5		.
	defb 0a5h		;8c06	a5		.
	defb 000h		;8c07	00		.
	defb 042h		;8c08	42		B
	defb 0efh		;8c09	ef		.
	defb 0efh		;8c0a	ef		.
	defb 000h		;8c0b	00		.
	defb 0deh		;8c0c	de		.
	defb 0ceh		;8c0d	ce		.
	defb 0c6h		;8c0e	c6		.
	defb 000h		;8c0f	00		.
	defb 084h		;8c10	84		.
	defb 039h		;8c11	39		9
	defb 039h		;8c12	39		9
	defb 000h		;8c13	00		.
	defb 0ceh		;8c14	ce		.
	defb 08ch		;8c15	8c		.
	defb 084h		;8c16	84		.
	defb 000h		;8c17	00		.
	defb 0a5h		;8c18	a5		.
	defb 039h		;8c19	39		9
	defb 031h		;8c1a	31		1
	defb 000h		;8c1b	00		.
	defb 084h		;8c1c	84		.
	defb 07bh		;8c1d	7b		{
	defb 07bh		;8c1e	7b		{
	defb 000h		;8c1f	00		.
	defb 063h		;8c20	63		c
	defb 05ah		;8c21	5a		Z
	defb 05ah		;8c22	5a		Z
	defb 000h		;8c23	00		.
	defb 0bdh		;8c24	bd		.
	defb 039h		;8c25	39		9
	defb 039h		;8c26	39		9
	defb 000h		;8c27	00		.
	defb 084h		;8c28	84		.
	defb 008h		;8c29	08		.
	defb 008h		;8c2a	08		.
	defb 000h		;8c2b	00		.
	defb 0adh		;8c2c	ad		.
	defb 008h		;8c2d	08		.
	defb 008h		;8c2e	08		.
	defb 000h		;8c2f	00		.
	defb 018h		;8c30	18		.
	defb 000h		;8c31	00		.
	defb 000h		;8c32	00		.
	defb 000h		;8c33	00		.
	defb 063h		;8c34	63		c
	defb 008h		;8c35	08		.
	defb 010h		;8c36	10		.
	defb 000h		;8c37	00		.
	defb 094h		;8c38	94		.
	defb 008h		;8c39	08		.
	defb 018h		;8c3a	18		.
	defb 000h		;8c3b	00		.
	defb 000h		;8c3c	00		.
	defb 000h		;8c3d	00		.
	defb 000h		;8c3e	00		.
	defb 000h		;8c3f	00		.
PALCOL:
				; = PALCOL (src: VIDEO_IO.ASM:382, BIOS-PP 1273243)
	defb 0ffh		;8c40	ff		.
	defb 0ffh		;8c41	ff		.
	defb 0ffh		;8c42	ff		.
	defb 000h		;8c43	00		.
	defb 08ch		;8c44	8c		.
	defb 0bdh		;8c45	bd		.
	defb 0bdh		;8c46	bd		.
	defb 080h		;8c47	80		.
	defb 0bdh		;8c48	bd		.
	defb 0ceh		;8c49	ce		.
	defb 0bdh		;8c4a	bd		.
	defb 000h		;8c4b	00		.
	defb 0efh		;8c4c	ef		.
	defb 0ceh		;8c4d	ce		.
	defb 0bdh		;8c4e	bd		.
	defb 000h		;8c4f	00		.
	defb 0bch		;8c50	bc		.
	defb 073h		;8c51	73		s
	defb 073h		;8c52	73		s
	defb 000h		;8c53	00		.
	defb 09ch		;8c54	9c		.
	defb 063h		;8c55	63		c
	defb 063h		;8c56	63		c
	defb 000h		;8c57	00		.
	defb 0deh		;8c58	de		.
	defb 08ch		;8c59	8c		.
	defb 08ch		;8c5a	8c		.
	defb 000h		;8c5b	00		.
	defb 08ch		;8c5c	8c		.
	defb 052h		;8c5d	52		R
	defb 052h		;8c5e	52		R
	defb 000h		;8c5f	00		.
	defb 0bdh		;8c60	bd		.
	defb 063h		;8c61	63		c
	defb 063h		;8c62	63		c
	defb 000h		;8c63	00		.
	defb 052h		;8c64	52		R
	defb 010h		;8c65	10		.
	defb 010h		;8c66	10		.
	defb 000h		;8c67	00		.
	defb 021h		;8c68	21		!
	defb 000h		;8c69	00		.
	defb 000h		;8c6a	00		.
	defb 000h		;8c6b	00		.
	defb 08ch		;8c6c	8c		.
	defb 000h		;8c6d	00		.
	defb 000h		;8c6e	00		.
	defb 000h		;8c6f	00		.
	defb 0adh		;8c70	ad		.
	defb 000h		;8c71	00		.
	defb 000h		;8c72	00		.
	defb 000h		;8c73	00		.
	defb 0adh		;8c74	ad		.
	defb 010h		;8c75	10		.
	defb 021h		;8c76	21		!
	defb 000h		;8c77	00		.
	defb 08ch		;8c78	8c		.
	defb 000h		;8c79	00		.
	defb 010h		;8c7a	10		.
	defb 000h		;8c7b	00		.
	defb 000h		;8c7c	00		.
	defb 000h		;8c7d	00		.
	defb 000h		;8c7e	00		.
	defb 000h		;8c7f	00		.
SET_CGA:
				; = SET_CGA (src: VIDEO_IO.ASM:400, BIOS-PP 1273243)
	call SETPAL4		;8c80	cd cc 8c	. . .
	ld hl,lb645h	;8c83	21 45 b6	! E .
	ld de,00000h		;8c86	11 00 00	. . .
	ld bc,0ffa4h		;8c89	01 a4 ff	. . .
	ld a,004h		;8c8c	3e 04		> .
	call 00018h		;8c8e	cd 18 00	. . .
	ld hl,lb645h	;8c91	21 45 b6	! E .
	ld de,00000h		;8c94	11 00 00	. . .
	ld bc,0ffa4h		;8c97	01 a4 ff	. . .
	ld a,006h		;8c9a	3e 06		> .
	call 00018h		;8c9c	cd 18 00	. . .
	ld hl,0b845h		;8c9f	21 45 b8	! E .
	ld de,l8080h		;8ca2	11 80 80	. . .
	ld bc,0ffa4h		;8ca5	01 a4 ff	. . .
	ld a,007h		;8ca8	3e 07		> .
	call 00018h		;8caa	cd 18 00	. . .
	call SETPAL5		;8cad	cd f4 8c	. . .
	ld hl,lb645h	;8cb0	21 45 b6	! E .
	ld de,00000h		;8cb3	11 00 00	. . .
	ld bc,0ffa4h		;8cb6	01 a4 ff	. . .
	ld a,005h		;8cb9	3e 05		> .
	call 00018h		;8cbb	cd 18 00	. . .
	ld hl,lb645h	;8cbe	21 45 b6	! E .
	ld de,SetupEntry	;8cc1	11 00 80	. . .
	ld bc,0ffa4h		;8cc4	01 a4 ff	. . .
	ld a,007h		;8cc7	3e 07		> .
	jp 00018h		;8cc9	c3 18 00	. . .
SETPAL4:
				; = SETPAL4 (src: VIDEO_IO.ASM:428, BIOS-PP 1273243)
	ld hl,COLORS		;8ccc	21 10 8d	! . .
	ld de,lb645h	;8ccf	11 45 b6	. E .
	ld c,008h		;8cd2	0e 08		. .
DCR0:
				; = DCR0 (src: VIDEO_IO.ASM:431, BIOS-PP 1273243)
	ld b,010h		;8cd4	06 10		. .
DCR1:
				; = DCR1 (src: VIDEO_IO.ASM:432, BIOS-PP 1273243)
	push bc			;8cd6	c5		.
	push hl			;8cd7	e5		.
	ldi			;8cd8	ed a0		. .
	ldi			;8cda	ed a0		. .
	ldi			;8cdc	ed a0		. .
	ldi			;8cde	ed a0		. .
	pop hl			;8ce0	e1		.
	pop bc			;8ce1	c1		.
	djnz DCR1		;8ce2	10 f2		. .
	inc hl			;8ce4	23		#
	inc hl			;8ce5	23		#
	inc hl			;8ce6	23		#
	inc hl			;8ce7	23		#
	dec c			;8ce8	0d		.
	jr nz,DCR0		;8ce9	20 e9		  .
	ld hl,lb645h	;8ceb	21 45 b6	! E .
	ld bc,00200h		;8cee	01 00 02	. . .
	ldir			;8cf1	ed b0		. .
	ret			;8cf3	c9		.
SETPAL5:
				; = SETPAL5 (src: VIDEO_IO.ASM:452, BIOS-PP 1273243)
	ld hl,COLORS		;8cf4	21 10 8d	! . .
	ld de,lb645h	;8cf7	11 45 b6	. E .
	ld b,008h		;8cfa	06 08		. .
DCR01:
				; = DCR01 (src: VIDEO_IO.ASM:455, BIOS-PP 1273243)
	push bc			;8cfc	c5		.
	push hl			;8cfd	e5		.
	ld bc,00040h		;8cfe	01 40 00	. @ .
	ldir			;8d01	ed b0		. .
	pop hl			;8d03	e1		.
	pop bc			;8d04	c1		.
	djnz DCR01		;8d05	10 f5		. .
	ld hl,lb645h	;8d07	21 45 b6	! E .
	ld bc,00200h		;8d0a	01 00 02	. . .
	ldir			;8d0d	ed b0		. .
	ret			;8d0f	c9		.
COLORS:

; BLOCK 'Data8D10' (start 0x8d10 end 0x9623)
				; = COLORS (src: VIDEO_IO.ASM:467, BIOS-PP 1273243)
	defb 000h		;8d10	00		.
	defb 000h		;8d11	00		.
	defb 000h		;8d12	00		.
	defb 000h		;8d13	00		.
	defb 0a8h		;8d14	a8		.
	defb 000h		;8d15	00		.
	defb 000h		;8d16	00		.
	defb 000h		;8d17	00		.
	defb 000h		;8d18	00		.
	defb 0a8h		;8d19	a8		.
	defb 000h		;8d1a	00		.
	defb 000h		;8d1b	00		.
	defb 0a8h		;8d1c	a8		.
	defb 0a8h		;8d1d	a8		.
	defb 000h		;8d1e	00		.
	defb 000h		;8d1f	00		.
	defb 000h		;8d20	00		.
	defb 000h		;8d21	00		.
	defb 0a8h		;8d22	a8		.
	defb 000h		;8d23	00		.
	defb 0a8h		;8d24	a8		.
	defb 000h		;8d25	00		.
	defb 0a8h		;8d26	a8		.
	defb 000h		;8d27	00		.
	defb 000h		;8d28	00		.
	defb 054h		;8d29	54		T
	defb 0a8h		;8d2a	a8		.
	defb 000h		;8d2b	00		.
	defb 0a8h		;8d2c	a8		.
	defb 0a8h		;8d2d	a8		.
	defb 0a8h		;8d2e	a8		.
	defb 000h		;8d2f	00		.
	defb 054h		;8d30	54		T
	defb 054h		;8d31	54		T
	defb 054h		;8d32	54		T
	defb 000h		;8d33	00		.
	defb 0fch		;8d34	fc		.
	defb 054h		;8d35	54		T
	defb 054h		;8d36	54		T
	defb 000h		;8d37	00		.
	defb 054h		;8d38	54		T
	defb 0fch		;8d39	fc		.
	defb 054h		;8d3a	54		T
	defb 000h		;8d3b	00		.
	defb 0fch		;8d3c	fc		.
	defb 0fch		;8d3d	fc		.
	defb 054h		;8d3e	54		T
	defb 000h		;8d3f	00		.
	defb 054h		;8d40	54		T
	defb 054h		;8d41	54		T
	defb 0fch		;8d42	fc		.
	defb 000h		;8d43	00		.
	defb 0fch		;8d44	fc		.
	defb 054h		;8d45	54		T
	defb 0fch		;8d46	fc		.
	defb 000h		;8d47	00		.
	defb 054h		;8d48	54		T
	defb 0fch		;8d49	fc		.
	defb 0fch		;8d4a	fc		.
	defb 000h		;8d4b	00		.
	defb 0fch		;8d4c	fc		.
	defb 0fch		;8d4d	fc		.
	defb 0fch		;8d4e	fc		.
	defb 000h		;8d4f	00		.
LOGO:
				; = LOGO (src: VIDEO_IO.ASM:485, BIOS-PP 1273243)
	defb 0cdh		;8d50	cd		.
	defb 0d2h		;8d51	d2		.
	defb 0e1h		;8d52	e1		.
	defb 0f3h		;8d53	f3		.
	defb 0edh		;8d54	ed		.
	defb 073h		;8d55	73		s
	defb 073h		;8d56	73		s
	defb 0d9h		;8d57	d9		.
	defb 021h		;8d58	21		!
	defb 01eh		;8d59	1e		.
	defb 0d9h		;8d5a	d9		.
	defb 011h		;8d5b	11		.
	defb 000h		;8d5c	00		.
	defb 0d8h		;8d5d	d8		.
	defb 001h		;8d5e	01		.
	defb 064h		;8d5f	64		d
	defb 000h		;8d60	00		.
	defb 0d5h		;8d61	d5		.
	defb 0edh		;8d62	ed		.
	defb 0b0h		;8d63	b0		.
	defb 021h		;8d64	21		!
	defb 0d1h		;8d65	d1		.
	defb 0e1h		;8d66	e1		.
	defb 011h		;8d67	11		.
	defb 0ffh		;8d68	ff		.
	defb 0ffh		;8d69	ff		.
	defb 001h		;8d6a	01		.
	defb 050h		;8d6b	50		P
	defb 008h		;8d6c	08		.
	defb 0c9h		;8d6d	c9		.
	defb 0edh		;8d6e	ed		.
	defb 0b8h		;8d6f	b8		.
	defb 021h		;8d70	21		!
	defb 0b0h		;8d71	b0		.
	defb 0f7h		;8d72	f7		.
	defb 011h		;8d73	11		.
	defb 000h		;8d74	00		.
	defb 0d9h		;8d75	d9		.
	defb 006h		;8d76	06		.
	defb 000h		;8d77	00		.
	defb 07eh		;8d78	7e		~
	defb 0cbh		;8d79	cb		.
	defb 07fh		;8d7a	7f		.
	defb 020h		;8d7b	20		 
	defb 01dh		;8d7c	1d		.
	defb 0e6h		;8d7d	e6		.
	defb 00fh		;8d7e	0f		.
	defb 047h		;8d7f	47		G
	defb 0edh		;8d80	ed		.
	defb 06fh		;8d81	6f		o
	defb 0c6h		;8d82	c6		.
	defb 003h		;8d83	03		.
	defb 04fh		;8d84	4f		O
	defb 023h		;8d85	23		#
	defb 07bh		;8d86	7b		{
	defb 096h		;8d87	96		.
	defb 023h		;8d88	23		#
	defb 0f9h		;8d89	f9		.
	defb 066h		;8d8a	66		f
	defb 06fh		;8d8b	6f		o
	defb 07ah		;8d8c	7a		z
	defb 098h		;8d8d	98		.
	defb 044h		;8d8e	44		D
	defb 067h		;8d8f	67		g
	defb 078h		;8d90	78		x
	defb 006h		;8d91	06		.
	defb 000h		;8d92	00		.
	defb 0edh		;8d93	ed		.
	defb 0b0h		;8d94	b0		.
	defb 060h		;8d95	60		`
	defb 069h		;8d96	69		i
	defb 039h		;8d97	39		9
	defb 018h		;8d98	18		.
	defb 0dfh		;8d99	df		.
	defb 0e6h		;8d9a	e6		.
	defb 07fh		;8d9b	7f		.
	defb 028h		;8d9c	28		(
	defb 019h		;8d9d	19		.
	defb 023h		;8d9e	23		#
	defb 0cbh		;8d9f	cb		.
	defb 077h		;8da0	77		w
	defb 020h		;8da1	20		 
	defb 005h		;8da2	05		.
	defb 04fh		;8da3	4f		O
	defb 0edh		;8da4	ed		.
	defb 0b0h		;8da5	b0		.
	defb 018h		;8da6	18		.
	defb 0d0h		;8da7	d0		.
	defb 0e6h		;8da8	e6		.
	defb 03fh		;8da9	3f		?
	defb 0c6h		;8daa	c6		.
	defb 003h		;8dab	03		.
	defb 047h		;8dac	47		G
	defb 07eh		;8dad	7e		~
	defb 023h		;8dae	23		#
	defb 04eh		;8daf	4e		N
	defb 012h		;8db0	12		.
	defb 013h		;8db1	13		.
	defb 010h		;8db2	10		.
	defb 0fch		;8db3	fc		.
	defb 079h		;8db4	79		y
	defb 018h		;8db5	18		.
	defb 0c2h		;8db6	c2		.
	defb 031h		;8db7	31		1
	defb 05bh		;8db8	5b		[
	defb 0d8h		;8db9	d8		.
	defb 006h		;8dba	06		.
	defb 003h		;8dbb	03		.
	defb 0e1h		;8dbc	e1		.
	defb 03bh		;8dbd	3b		;
	defb 0f1h		;8dbe	f1		.
	defb 077h		;8dbf	77		w
	defb 010h		;8dc0	10		.
	defb 0fah		;8dc1	fa		.
	defb 031h		;8dc2	31		1
	defb 000h		;8dc3	00		.
	defb 000h		;8dc4	00		.
	defb 0f3h		;8dc5	f3		.
	defb 0c9h		;8dc6	c9		.
	defb 000h		;8dc7	00		.
	defb 000h		;8dc8	00		.
	defb 000h		;8dc9	00		.
	defb 000h		;8dca	00		.
	defb 000h		;8dcb	00		.
	defb 000h		;8dcc	00		.
	defb 000h		;8dcd	00		.
	defb 000h		;8dce	00		.
	defb 000h		;8dcf	00		.
	defb 000h		;8dd0	00		.
	defb 000h		;8dd1	00		.
	defb 0ffh		;8dd2	ff		.
	defb 0ffh		;8dd3	ff		.
	defb 0ffh		;8dd4	ff		.
	defb 0ffh		;8dd5	ff		.
	defb 0ffh		;8dd6	ff		.
	defb 0ffh		;8dd7	ff		.
	defb 0ffh		;8dd8	ff		.
	defb 0ffh		;8dd9	ff		.
	defb 0ffh		;8dda	ff		.
	defb 0ffh		;8ddb	ff		.
	defb 0ffh		;8ddc	ff		.
	defb 0ffh		;8ddd	ff		.
	defb 0ffh		;8dde	ff		.
	defb 0ffh		;8ddf	ff		.
	defb 0ffh		;8de0	ff		.
	defb 0ffh		;8de1	ff		.
	defb 0ffh		;8de2	ff		.
	defb 0ffh		;8de3	ff		.
	defb 0ffh		;8de4	ff		.
	defb 0ffh		;8de5	ff		.
	defb 0ffh		;8de6	ff		.
	defb 0ffh		;8de7	ff		.
	defb 0ffh		;8de8	ff		.
	defb 0ffh		;8de9	ff		.
	defb 0d9h		;8dea	d9		.
	defb 0ffh		;8deb	ff		.
	defb 081h		;8dec	81		.
	defb 0fch		;8ded	fc		.
	defb 0ddh		;8dee	dd		.
	defb 0cch		;8def	cc		.
	defb 0ffh		;8df0	ff		.
	defb 0ffh		;8df1	ff		.
	defb 0d6h		;8df2	d6		.
	defb 0ffh		;8df3	ff		.
	defb 0e7h		;8df4	e7		.
	defb 0cch		;8df5	cc		.
	defb 0ffh		;8df6	ff		.
	defb 0ffh		;8df7	ff		.
	defb 0cbh		;8df8	cb		.
	defb 0ffh		;8df9	ff		.
	defb 070h		;8dfa	70		p
	defb 0f6h		;8dfb	f6		.
	defb 081h		;8dfc	81		.
	defb 0cdh		;8dfd	cd		.
	defb 0cah		;8dfe	ca		.
	defb 0ddh		;8dff	dd		.
	defb 081h		;8e00	81		.
	defb 0dah		;8e01	da		.
	defb 0c1h		;8e02	c1		.
	defb 0aah		;8e03	aa		.
	defb 0cbh		;8e04	cb		.
	defb 0ddh		;8e05	dd		.
	defb 081h		;8e06	81		.
	defb 0dch		;8e07	dc		.
	defb 071h		;8e08	71		q
	defb 00ah		;8e09	0a		.
	defb 0ffh		;8e0a	ff		.
	defb 0ffh		;8e0b	ff		.
	defb 070h		;8e0c	70		p
	defb 0f7h		;8e0d	f7		.
	defb 011h		;8e0e	11		.
	defb 077h		;8e0f	77		w
	defb 030h		;8e10	30		0
	defb 07bh		;8e11	7b		{
	defb 000h		;8e12	00		.
	defb 073h		;8e13	73		s
	defb 081h		;8e14	81		.
	defb 0aeh		;8e15	ae		.
	defb 0d2h		;8e16	d2		.
	defb 0eeh		;8e17	ee		.
	defb 081h		;8e18	81		.
	defb 0eah		;8e19	ea		.
	defb 070h		;8e1a	70		p
	defb 08dh		;8e1b	8d		.
	defb 051h		;8e1c	51		Q
	defb 0a5h		;8e1d	a5		.
	defb 081h		;8e1e	81		.
	defb 0cfh		;8e1f	cf		.
	defb 0fdh		;8e20	fd		.
	defb 0ffh		;8e21	ff		.
	defb 041h		;8e22	41		A
	defb 0efh		;8e23	ef		.
	defb 020h		;8e24	20		 
	defb 0f6h		;8e25	f6		.
	defb 082h		;8e26	82		.
	defb 0aah		;8e27	aa		.
	defb 0aah		;8e28	aa		.
	defb 030h		;8e29	30		0
	defb 077h		;8e2a	77		w
	defb 070h		;8e2b	70		p
	defb 0f4h		;8e2c	f4		.
	defb 010h		;8e2d	10		.
	defb 0efh		;8e2e	ef		.
	defb 071h		;8e2f	71		q
	defb 00ch		;8e30	0c		.
	defb 070h		;8e31	70		p
	defb 099h		;8e32	99		.
	defb 031h		;8e33	31		1
	defb 014h		;8e34	14		.
	defb 072h		;8e35	72		r
	defb 011h		;8e36	11		.
	defb 0f5h		;8e37	f5		.
	defb 0ffh		;8e38	ff		.
	defb 070h		;8e39	70		p
	defb 0f7h		;8e3a	f7		.
	defb 081h		;8e3b	81		.
	defb 0dah		;8e3c	da		.
	defb 040h		;8e3d	40		@
	defb 07ch		;8e3e	7c		|
	defb 081h		;8e3f	81		.
	defb 0adh		;8e40	ad		.
	defb 070h		;8e41	70		p
	defb 0ddh		;8e42	dd		.
	defb 0d1h		;8e43	d1		.
	defb 0cch		;8e44	cc		.
	defb 011h		;8e45	11		.
	defb 09eh		;8e46	9e		.
	defb 040h		;8e47	40		@
	defb 02bh		;8e48	2b		+
	defb 070h		;8e49	70		p
	defb 083h		;8e4a	83		.
	defb 072h		;8e4b	72		r
	defb 093h		;8e4c	93		.
	defb 0eah		;8e4d	ea		.
	defb 0ffh		;8e4e	ff		.
	defb 061h		;8e4f	61		a
	defb 0efh		;8e50	ef		.
	defb 050h		;8e51	50		P
	defb 050h		;8e52	50		P
	defb 051h		;8e53	51		Q
	defb 0dah		;8e54	da		.
	defb 0d8h		;8e55	d8		.
	defb 0aah		;8e56	aa		.
	defb 081h		;8e57	81		.
	defb 0ach		;8e58	ac		.
	defb 062h		;8e59	62		b
	defb 025h		;8e5a	25		%
	defb 021h		;8e5b	21		!
	defb 02ch		;8e5c	2c		,
	defb 081h		;8e5d	81		.
	defb 0aah		;8e5e	aa		.
	defb 000h		;8e5f	00		.
	defb 0afh		;8e60	af		.
	defb 032h		;8e61	32		2
	defb 010h		;8e62	10		.
	defb 0eeh		;8e63	ee		.
	defb 0ffh		;8e64	ff		.
	defb 023h		;8e65	23		#
	defb 068h		;8e66	68		h
	defb 002h		;8e67	02		.
	defb 06dh		;8e68	6d		m
	defb 081h		;8e69	81		.
	defb 0dah		;8e6a	da		.
	defb 011h		;8e6b	11		.
	defb 0ech		;8e6c	ec		.
	defb 070h		;8e6d	70		p
	defb 0f6h		;8e6e	f6		.
	defb 0e1h		;8e6f	e1		.
	defb 0aah		;8e70	aa		.
	defb 052h		;8e71	52		R
	defb 02dh		;8e72	2d		-
	defb 020h		;8e73	20		 
	defb 03bh		;8e74	3b		;
	defb 012h		;8e75	12		.
	defb 09eh		;8e76	9e		.
	defb 073h		;8e77	73		s
	defb 098h		;8e78	98		.
	defb 0e5h		;8e79	e5		.
	defb 0ffh		;8e7a	ff		.
	defb 070h		;8e7b	70		p
	defb 0c5h		;8e7c	c5		.
	defb 060h		;8e7d	60		`
	defb 0f8h		;8e7e	f8		.
	defb 081h		;8e7f	81		.
	defb 0cah		;8e80	ca		.
	defb 0ebh		;8e81	eb		.
	defb 0aah		;8e82	aa		.
	defb 021h		;8e83	21		!
	defb 00ah		;8e84	0a		.
	defb 081h		;8e85	81		.
	defb 0cdh		;8e86	cd		.
	defb 020h		;8e87	20		 
	defb 0bfh		;8e88	bf		.
	defb 050h		;8e89	50		P
	defb 083h		;8e8a	83		.
	defb 072h		;8e8b	72		r
	defb 08dh		;8e8c	8d		.
	defb 0dch		;8e8d	dc		.
	defb 0ffh		;8e8e	ff		.
	defb 024h		;8e8f	24		$
	defb 064h		;8e90	64		d
	defb 031h		;8e91	31		1
	defb 078h		;8e92	78		x
	defb 081h		;8e93	81		.
	defb 0adh		;8e94	ad		.
	defb 022h		;8e95	22		"
	defb 0cbh		;8e96	cb		.
	defb 070h		;8e97	70		p
	defb 07ch		;8e98	7c		|
	defb 0e9h		;8e99	e9		.
	defb 0aah		;8e9a	aa		.
	defb 081h		;8e9b	81		.
	defb 0adh		;8e9c	ad		.
	defb 023h		;8e9d	23		#
	defb 034h		;8e9e	34		4
	defb 020h		;8e9f	20		 
	defb 082h		;8ea0	82		.
	defb 002h		;8ea1	02		.
	defb 037h		;8ea2	37		7
	defb 074h		;8ea3	74		t
	defb 09ch		;8ea4	9c		.
	defb 0dch		;8ea5	dc		.
	defb 0ffh		;8ea6	ff		.
	defb 014h		;8ea7	14		.
	defb 0e2h		;8ea8	e2		.
	defb 000h		;8ea9	00		.
	defb 0b9h		;8eaa	b9		.
	defb 003h		;8eab	03		.
	defb 050h		;8eac	50		P
	defb 070h		;8ead	70		p
	defb 07dh		;8eae	7d		}
	defb 0f6h		;8eaf	f6		.
	defb 0aah		;8eb0	aa		.
	defb 014h		;8eb1	14		.
	defb 00eh		;8eb2	0e		.
	defb 082h		;8eb3	82		.
	defb 0ddh		;8eb4	dd		.
	defb 0ddh		;8eb5	dd		.
	defb 003h		;8eb6	03		.
	defb 0b0h		;8eb7	b0		.
	defb 081h		;8eb8	81		.
	defb 0eah		;8eb9	ea		.
	defb 034h		;8eba	34		4
	defb 01ah		;8ebb	1a		.
	defb 0dfh		;8ebc	df		.
	defb 0ffh		;8ebd	ff		.
	defb 033h		;8ebe	33		3
	defb 0e9h		;8ebf	e9		.
	defb 021h		;8ec0	21		!
	defb 037h		;8ec1	37		7
	defb 072h		;8ec2	72		r
	defb 06dh		;8ec3	6d		m
	defb 0f8h		;8ec4	f8		.
	defb 0aah		;8ec5	aa		.
	defb 070h		;8ec6	70		p
	defb 050h		;8ec7	50		P
	defb 024h		;8ec8	24		$
	defb 017h		;8ec9	17		.
	defb 074h		;8eca	74		t
	defb 012h		;8ecb	12		.
	defb 0d2h		;8ecc	d2		.
	defb 0ffh		;8ecd	ff		.
	defb 050h		;8ece	50		P
	defb 07eh		;8ecf	7e		~
	defb 070h		;8ed0	70		p
	defb 07dh		;8ed1	7d		}
	defb 032h		;8ed2	32		2
	defb 0eeh		;8ed3	ee		.
	defb 0f9h		;8ed4	f9		.
	defb 000h		;8ed5	00		.
	defb 082h		;8ed6	82		.
	defb 034h		;8ed7	34		4
	defb 04dh		;8ed8	4d		M
	defb 005h		;8ed9	05		.
	defb 014h		;8eda	14		.
	defb 081h		;8edb	81		.
	defb 0cdh		;8edc	cd		.
	defb 002h		;8edd	02		.
	defb 0c9h		;8ede	c9		.
	defb 001h		;8edf	01		.
	defb 004h		;8ee0	04		.
	defb 074h		;8ee1	74		t
	defb 094h		;8ee2	94		.
	defb 0d2h		;8ee3	d2		.
	defb 0ffh		;8ee4	ff		.
	defb 006h		;8ee5	06		.
	defb 05eh		;8ee6	5e		^
	defb 011h		;8ee7	11		.
	defb 07bh		;8ee8	7b		{
	defb 082h		;8ee9	82		.
	defb 0eah		;8eea	ea		.
	defb 0adh		;8eeb	ad		.
	defb 015h		;8eec	15		.
	defb 046h		;8eed	46		F
	defb 0c9h		;8eee	c9		.
	defb 0aah		;8eef	aa		.
	defb 083h		;8ef0	83		.
	defb 071h		;8ef1	71		q
	defb 033h		;8ef2	33		3
	defb 033h		;8ef3	33		3
	defb 0f2h		;8ef4	f2		.
	defb 0aah		;8ef5	aa		.
	defb 082h		;8ef6	82		.
	defb 0aeh		;8ef7	ae		.
	defb 065h		;8ef8	65		e
	defb 010h		;8ef9	10		.
	defb 0bah		;8efa	ba		.
	defb 082h		;8efb	82		.
	defb 06bh		;8efc	6b		k
	defb 0eah		;8efd	ea		.
	defb 015h		;8efe	15		.
	defb 03eh		;8eff	3e		>
	defb 050h		;8f00	50		P
	defb 081h		;8f01	81		.
	defb 0d7h		;8f02	d7		.
	defb 0ffh		;8f03	ff		.
	defb 032h		;8f04	32		2
	defb 030h		;8f05	30		0
	defb 020h		;8f06	20		 
	defb 07eh		;8f07	7e		~
	defb 072h		;8f08	72		r
	defb 0efh		;8f09	ef		.
	defb 023h		;8f0a	23		#
	defb 0efh		;8f0b	ef		.
	defb 084h		;8f0c	84		.
	defb 0a7h		;8f0d	a7		.
	defb 010h		;8f0e	10		.
	defb 000h		;8f0f	00		.
	defb 05dh		;8f10	5d		]
	defb 075h		;8f11	75		u
	defb 079h		;8f12	79		y
	defb 055h		;8f13	55		U
	defb 07fh		;8f14	7f		.
	defb 0e0h		;8f15	e0		.
	defb 0bbh		;8f16	bb		.
	defb 089h		;8f17	89		.
	defb 0ebh		;8f18	eb		.
	defb 0beh		;8f19	be		.
	defb 0adh		;8f1a	ad		.
	defb 010h		;8f1b	10		.
	defb 000h		;8f1c	00		.
	defb 000h		;8f1d	00		.
	defb 09bh		;8f1e	9b		.
	defb 0bbh		;8f1f	bb		.
	defb 0bbh		;8f20	bb		.
	defb 006h		;8f21	06		.
	defb 018h		;8f22	18		.
	defb 021h		;8f23	21		!
	defb 058h		;8f24	58		X
	defb 072h		;8f25	72		r
	defb 006h		;8f26	06		.
	defb 0ceh		;8f27	ce		.
	defb 0ffh		;8f28	ff		.
	defb 022h		;8f29	22		"
	defb 0afh		;8f2a	af		.
	defb 082h		;8f2b	82		.
	defb 0eeh		;8f2c	ee		.
	defb 0eeh		;8f2d	ee		.
	defb 015h		;8f2e	15		.
	defb 0beh		;8f2f	be		.
	defb 0ceh		;8f30	ce		.
	defb 0aah		;8f31	aa		.
	defb 087h		;8f32	87		.
	defb 080h		;8f33	80		.
	defb 000h		;8f34	00		.
	defb 003h		;8f35	03		.
	defb 088h		;8f36	88		.
	defb 011h		;8f37	11		.
	defb 011h		;8f38	11		.
	defb 017h		;8f39	17		.
	defb 0d3h		;8f3a	d3		.
	defb 077h		;8f3b	77		w
	defb 081h		;8f3c	81		.
	defb 071h		;8f3d	71		q
	defb 0cch		;8f3e	cc		.
	defb 011h		;8f3f	11		.
	defb 082h		;8f40	82		.
	defb 019h		;8f41	19		.
	defb 064h		;8f42	64		d
	defb 0c9h		;8f43	c9		.
	defb 0bbh		;8f44	bb		.
	defb 085h		;8f45	85		.
	defb 0aeh		;8f46	ae		.
	defb 063h		;8f47	63		c
	defb 000h		;8f48	00		.
	defb 003h		;8f49	03		.
	defb 0ebh		;8f4a	eb		.
	defb 000h		;8f4b	00		.
	defb 0abh		;8f4c	ab		.
	defb 020h		;8f4d	20		 
	defb 05eh		;8f4e	5e		^
	defb 071h		;8f4f	71		q
	defb 003h		;8f50	03		.
	defb 0ceh		;8f51	ce		.
	defb 0ffh		;8f52	ff		.
	defb 016h		;8f53	16		.
	defb 0ddh		;8f54	dd		.
	defb 082h		;8f55	82		.
	defb 0aeh		;8f56	ae		.
	defb 0eeh		;8f57	ee		.
	defb 016h		;8f58	16		.
	defb 0c1h		;8f59	c1		.
	defb 0d1h		;8f5a	d1		.
	defb 0aah		;8f5b	aa		.
	defb 0ebh		;8f5c	eb		.
	defb 000h		;8f5d	00		.
	defb 081h		;8f5e	81		.
	defb 009h		;8f5f	09		.
	defb 0c9h		;8f60	c9		.
	defb 0bbh		;8f61	bb		.
	defb 084h		;8f62	84		.
	defb 0eah		;8f63	ea		.
	defb 030h		;8f64	30		0
	defb 000h		;8f65	00		.
	defb 04eh		;8f66	4e		N
	defb 021h		;8f67	21		!
	defb 02bh		;8f68	2b		+
	defb 071h		;8f69	71		q
	defb 003h		;8f6a	03		.
	defb 0d0h		;8f6b	d0		.
	defb 0ffh		;8f6c	ff		.
	defb 016h		;8f6d	16		.
	defb 0e1h		;8f6e	e1		.
	defb 082h		;8f6f	82		.
	defb 0aah		;8f70	aa		.
	defb 0eeh		;8f71	ee		.
	defb 013h		;8f72	13		.
	defb 0b5h		;8f73	b5		.
	defb 0ffh		;8f74	ff		.
	defb 0aah		;8f75	aa		.
	defb 083h		;8f76	83		.
	defb 0a5h		;8f77	a5		.
	defb 000h		;8f78	00		.
	defb 00bh		;8f79	0b		.
	defb 0c9h		;8f7a	c9		.
	defb 0bbh		;8f7b	bb		.
	defb 081h		;8f7c	81		.
	defb 060h		;8f7d	60		`
	defb 050h		;8f7e	50		P
	defb 00fh		;8f7f	0f		.
	defb 022h		;8f80	22		"
	defb 006h		;8f81	06		.
	defb 024h		;8f82	24		$
	defb 054h		;8f83	54		T
	defb 0ceh		;8f84	ce		.
	defb 0ffh		;8f85	ff		.
	defb 040h		;8f86	40		@
	defb 0feh		;8f87	fe		.
	defb 074h		;8f88	74		t
	defb 0e9h		;8f89	e9		.
	defb 0fah		;8f8a	fa		.
	defb 0aah		;8f8b	aa		.
	defb 082h		;8f8c	82		.
	defb 000h		;8f8d	00		.
	defb 006h		;8f8e	06		.
	defb 0c9h		;8f8f	c9		.
	defb 0bbh		;8f90	bb		.
	defb 081h		;8f91	81		.
	defb 0d8h		;8f92	d8		.
	defb 060h		;8f93	60		`
	defb 08fh		;8f94	8f		.
	defb 081h		;8f95	81		.
	defb 0bdh		;8f96	bd		.
	defb 003h		;8f97	03		.
	defb 009h		;8f98	09		.
	defb 004h		;8f99	04		.
	defb 00ch		;8f9a	0c		.
	defb 071h		;8f9b	71		q
	defb 002h		;8f9c	02		.
	defb 079h		;8f9d	79		y
	defb 058h		;8f9e	58		X
	defb 040h		;8f9f	40		@
	defb 0feh		;8fa0	fe		.
	defb 075h		;8fa1	75		u
	defb 068h		;8fa2	68		h
	defb 0d9h		;8fa3	d9		.
	defb 0aah		;8fa4	aa		.
	defb 081h		;8fa5	81		.
	defb 096h		;8fa6	96		.
	defb 0dbh		;8fa7	db		.
	defb 066h		;8fa8	66		f
	defb 081h		;8fa9	81		.
	defb 070h		;8faa	70		p
	defb 071h		;8fab	71		q
	defb 000h		;8fac	00		.
	defb 071h		;8fad	71		q
	defb 000h		;8fae	00		.
	defb 050h		;8faf	50		P
	defb 081h		;8fb0	81		.
	defb 031h		;8fb1	31		1
	defb 002h		;8fb2	02		.
	defb 078h		;8fb3	78		x
	defb 01bh		;8fb4	1b		.
	defb 050h		;8fb5	50		P
	defb 07fh		;8fb6	7f		.
	defb 070h		;8fb7	70		p
	defb 0feh		;8fb8	fe		.
	defb 0d8h		;8fb9	d8		.
	defb 0aah		;8fba	aa		.
	defb 081h		;8fbb	81		.
	defb 0a5h		;8fbc	a5		.
	defb 0e3h		;8fbd	e3		.
	defb 000h		;8fbe	00		.
	defb 081h		;8fbf	81		.
	defb 036h		;8fc0	36		6
	defb 0c9h		;8fc1	c9		.
	defb 0bbh		;8fc2	bb		.
	defb 082h		;8fc3	82		.
	defb 0b6h		;8fc4	b6		.
	defb 000h		;8fc5	00		.
	defb 042h		;8fc6	42		B
	defb 000h		;8fc7	00		.
	defb 032h		;8fc8	32		2
	defb 005h		;8fc9	05		.
	defb 021h		;8fca	21		!
	defb 002h		;8fcb	02		.
	defb 078h		;8fcc	78		x
	defb 09ch		;8fcd	9c		.
	defb 049h		;8fce	49		I
	defb 0dbh		;8fcf	db		.
	defb 005h		;8fd0	05		.
	defb 072h		;8fd1	72		r
	defb 025h		;8fd2	25		%
	defb 0b1h		;8fd3	b1		.
	defb 0dbh		;8fd4	db		.
	defb 0aah		;8fd5	aa		.
	defb 020h		;8fd6	20		 
	defb 07fh		;8fd7	7f		.
	defb 083h		;8fd8	83		.
	defb 018h		;8fd9	18		.
	defb 044h		;8fda	44		D
	defb 044h		;8fdb	44		D
	defb 0dch		;8fdc	dc		.
	defb 0aah		;8fdd	aa		.
	defb 081h		;8fde	81		.
	defb 0abh		;8fdf	ab		.
	defb 0cah		;8fe0	ca		.
	defb 0bbh		;8fe1	bb		.
	defb 083h		;8fe2	83		.
	defb 043h		;8fe3	43		C
	defb 000h		;8fe4	00		.
	defb 000h		;8fe5	00		.
	defb 013h		;8fe6	13		.
	defb 000h		;8fe7	00		.
	defb 070h		;8fe8	70		p
	defb 081h		;8fe9	81		.
	defb 002h		;8fea	02		.
	defb 0e6h		;8feb	e6		.
	defb 009h		;8fec	09		.
	defb 023h		;8fed	23		#
	defb 0c8h		;8fee	c8		.
	defb 0ffh		;8fef	ff		.
	defb 014h		;8ff0	14		.
	defb 0a0h		;8ff1	a0		.
	defb 002h		;8ff2	02		.
	defb 07bh		;8ff3	7b		{
	defb 085h		;8ff4	85		.
	defb 0cfh		;8ff5	cf		.
	defb 070h		;8ff6	70		p
	defb 000h		;8ff7	00		.
	defb 000h		;8ff8	00		.
	defb 0ach		;8ff9	ac		.
	defb 0dah		;8ffa	da		.
	defb 0aah		;8ffb	aa		.
	defb 084h		;8ffc	84		.
	defb 000h		;8ffd	00		.
	defb 000h		;8ffe	00		.
	defb 003h		;8fff	03		.
	defb 056h		;9000	56		V
	defb 0ech		;9001	ec		.
	defb 0bbh		;9002	bb		.
	defb 081h		;9003	81		.
	defb 066h		;9004	66		f
	defb 005h		;9005	05		.
	defb 03ch		;9006	3c		<
	defb 081h		;9007	81		.
	defb 05bh		;9008	5b		[
	defb 0cah		;9009	ca		.
	defb 0bbh		;900a	bb		.
	defb 005h		;900b	05		.
	defb 00dh		;900c	0d		.
	defb 014h		;900d	14		.
	defb 0e2h		;900e	e2		.
	defb 079h		;900f	79		y
	defb 09dh		;9010	9d		.
	defb 031h		;9011	31		1
	defb 07eh		;9012	7e		~
	defb 087h		;9013	87		.
	defb 0dch		;9014	dc		.
	defb 0cch		;9015	cc		.
	defb 0ffh		;9016	ff		.
	defb 077h		;9017	77		w
	defb 078h		;9018	78		x
	defb 000h		;9019	00		.
	defb 00ah		;901a	0a		.
	defb 0d9h		;901b	d9		.
	defb 0aah		;901c	aa		.
	defb 084h		;901d	84		.
	defb 0a1h		;901e	a1		.
	defb 000h		;901f	00		.
	defb 000h		;9020	00		.
	defb 06eh		;9021	6e		n
	defb 0ebh		;9022	eb		.
	defb 0bbh		;9023	bb		.
	defb 082h		;9024	82		.
	defb 0b9h		;9025	b9		.
	defb 053h		;9026	53		S
	defb 070h		;9027	70		p
	defb 07fh		;9028	7f		.
	defb 072h		;9029	72		r
	defb 004h		;902a	04		.
	defb 081h		;902b	81		.
	defb 0ddh		;902c	dd		.
	defb 021h		;902d	21		!
	defb 082h		;902e	82		.
	defb 07bh		;902f	7b		{
	defb 059h		;9030	59		Y
	defb 014h		;9031	14		.
	defb 078h		;9032	78		x
	defb 087h		;9033	87		.
	defb 0ddh		;9034	dd		.
	defb 0cch		;9035	cc		.
	defb 0cbh		;9036	cb		.
	defb 0cfh		;9037	cf		.
	defb 007h		;9038	07		.
	defb 0aah		;9039	aa		.
	defb 0d4h		;903a	d4		.
	defb 070h		;903b	70		p
	defb 080h		;903c	80		.
	defb 0d0h		;903d	d0		.
	defb 0aah		;903e	aa		.
	defb 083h		;903f	83		.
	defb 000h		;9040	00		.
	defb 000h		;9041	00		.
	defb 005h		;9042	05		.
	defb 064h		;9043	64		d
	defb 0f4h		;9044	f4		.
	defb 083h		;9045	83		.
	defb 0beh		;9046	be		.
	defb 0eeh		;9047	ee		.
	defb 0eeh		;9048	ee		.
	defb 0e2h		;9049	e2		.
	defb 000h		;904a	00		.
	defb 082h		;904b	82		.
	defb 033h		;904c	33		3
	defb 04bh		;904d	4b		K
	defb 0ceh		;904e	ce		.
	defb 0bbh		;904f	bb		.
	defb 082h		;9050	82		.
	defb 0dch		;9051	dc		.
	defb 0cch		;9052	cc		.
	defb 001h		;9053	01		.
	defb 0edh		;9054	ed		.
	defb 000h		;9055	00		.
	defb 0efh		;9056	ef		.
	defb 07bh		;9057	7b		{
	defb 0d9h		;9058	d9		.
	defb 082h		;9059	82		.
	defb 0ddh		;905a	dd		.
	defb 0aeh		;905b	ae		.
	defb 005h		;905c	05		.
	defb 0f3h		;905d	f3		.
	defb 084h		;905e	84		.
	defb 0abh		;905f	ab		.
	defb 0cch		;9060	cc		.
	defb 000h		;9061	00		.
	defb 0bah		;9062	ba		.
	defb 070h		;9063	70		p
	defb 080h		;9064	80		.
	defb 0d1h		;9065	d1		.
	defb 0aah		;9066	aa		.
	defb 083h		;9067	83		.
	defb 000h		;9068	00		.
	defb 000h		;9069	00		.
	defb 01eh		;906a	1e		.
	defb 0c8h		;906b	c8		.
	defb 0bbh		;906c	bb		.
	defb 084h		;906d	84		.
	defb 0b4h		;906e	b4		.
	defb 000h		;906f	00		.
	defb 00eh		;9070	0e		.
	defb 04dh		;9071	4d		M
	defb 0deh		;9072	de		.
	defb 0ddh		;9073	dd		.
	defb 081h		;9074	81		.
	defb 0dbh		;9075	db		.
	defb 0d1h		;9076	d1		.
	defb 0bbh		;9077	bb		.
	defb 003h		;9078	03		.
	defb 005h		;9079	05		.
	defb 002h		;907a	02		.
	defb 083h		;907b	83		.
	defb 07ch		;907c	7c		|
	defb 0d4h		;907d	d4		.
	defb 003h		;907e	03		.
	defb 095h		;907f	95		.
	defb 000h		;9080	00		.
	defb 094h		;9081	94		.
	defb 040h		;9082	40		@
	defb 080h		;9083	80		.
	defb 082h		;9084	82		.
	defb 0a8h		;9085	a8		.
	defb 050h		;9086	50		P
	defb 0d3h		;9087	d3		.
	defb 033h		;9088	33		3
	defb 017h		;9089	17		.
	defb 007h		;908a	07		.
	defb 081h		;908b	81		.
	defb 005h		;908c	05		.
	defb 072h		;908d	72		r
	defb 049h		;908e	49		I
	defb 085h		;908f	85		.
	defb 0bbh		;9090	bb		.
	defb 0bbh		;9091	bb		.
	defb 0e0h		;9092	e0		.
	defb 000h		;9093	00		.
	defb 096h		;9094	96		.
	defb 0f4h		;9095	f4		.
	defb 0bbh		;9096	bb		.
	defb 081h		;9097	81		.
	defb 0bah		;9098	ba		.
	defb 001h		;9099	01		.
	defb 082h		;909a	82		.
	defb 082h		;909b	82		.
	defb 0adh		;909c	ad		.
	defb 0dch		;909d	dc		.
	defb 07ch		;909e	7c		|
	defb 0d8h		;909f	d8		.
	defb 082h		;90a0	82		.
	defb 0ddh		;90a1	dd		.
	defb 0aeh		;90a2	ae		.
	defb 006h		;90a3	06		.
	defb 0a2h		;90a4	a2		.
	defb 084h		;90a5	84		.
	defb 0aah		;90a6	aa		.
	defb 0abh		;90a7	ab		.
	defb 0cch		;90a8	cc		.
	defb 033h		;90a9	33		3
	defb 031h		;90aa	31		1
	defb 000h		;90ab	00		.
	defb 081h		;90ac	81		.
	defb 011h		;90ad	11		.
	defb 0d1h		;90ae	d1		.
	defb 088h		;90af	88		.
	defb 087h		;90b0	87		.
	defb 053h		;90b1	53		S
	defb 05ah		;90b2	5a		Z
	defb 0aah		;90b3	aa		.
	defb 0aah		;90b4	aa		.
	defb 010h		;90b5	10		.
	defb 000h		;90b6	00		.
	defb 09eh		;90b7	9e		.
	defb 076h		;90b8	76		v
	defb 074h		;90b9	74		t
	defb 081h		;90ba	81		.
	defb 0beh		;90bb	be		.
	defb 071h		;90bc	71		q
	defb 080h		;90bd	80		.
	defb 0e2h		;90be	e2		.
	defb 000h		;90bf	00		.
	defb 082h		;90c0	82		.
	defb 003h		;90c1	03		.
	defb 05eh		;90c2	5e		^
	defb 076h		;90c3	76		v
	defb 0b0h		;90c4	b0		.
	defb 011h		;90c5	11		.
	defb 001h		;90c6	01		.
	defb 050h		;90c7	50		P
	defb 080h		;90c8	80		.
	defb 000h		;90c9	00		.
	defb 0ffh		;90ca	ff		.
	defb 081h		;90cb	81		.
	defb 0ddh		;90cc	dd		.
	defb 006h		;90cd	06		.
	defb 075h		;90ce	75		u
	defb 081h		;90cf	81		.
	defb 0cah		;90d0	ca		.
	defb 030h		;90d1	30		0
	defb 080h		;90d2	80		.
	defb 081h		;90d3	81		.
	defb 03ah		;90d4	3a		:
	defb 00ch		;90d5	0c		.
	defb 0d3h		;90d6	d3		.
	defb 083h		;90d7	83		.
	defb 055h		;90d8	55		U
	defb 06eh		;90d9	6e		n
	defb 0bbh		;90da	bb		.
	defb 05ch		;90db	5c		\
	defb 06bh		;90dc	6b		k
	defb 05ah		;90dd	5a		Z
	defb 0edh		;90de	ed		.
	defb 087h		;90df	87		.
	defb 0ddh		;90e0	dd		.
	defb 0cah		;90e1	ca		.
	defb 0a9h		;90e2	a9		.
	defb 035h		;90e3	35		5
	defb 0aah		;90e4	aa		.
	defb 0aah		;90e5	aa		.
	defb 0a8h		;90e6	a8		.
	defb 071h		;90e7	71		q
	defb 001h		;90e8	01		.
	defb 012h		;90e9	12		.
	defb 002h		;90ea	02		.
	defb 082h		;90eb	82		.
	defb 0e6h		;90ec	e6		.
	defb 068h		;90ed	68		h
	defb 0e7h		;90ee	e7		.
	defb 077h		;90ef	77		w
	defb 084h		;90f0	84		.
	defb 071h		;90f1	71		q
	defb 013h		;90f2	13		.
	defb 000h		;90f3	00		.
	defb 000h		;90f4	00		.
	defb 071h		;90f5	71		q
	defb 08ch		;90f6	8c		.
	defb 085h		;90f7	85		.
	defb 0bch		;90f8	bc		.
	defb 0cch		;90f9	cc		.
	defb 0deh		;90fa	de		.
	defb 0eeh		;90fb	ee		.
	defb 0ddh		;90fc	dd		.
	defb 071h		;90fd	71		q
	defb 07fh		;90fe	7f		.
	defb 082h		;90ff	82		.
	defb 0dah		;9100	da		.
	defb 0eah		;9101	ea		.
	defb 002h		;9102	02		.
	defb 013h		;9103	13		.
	defb 070h		;9104	70		p
	defb 080h		;9105	80		.
	defb 083h		;9106	83		.
	defb 071h		;9107	71		q
	defb 056h		;9108	56		V
	defb 0e5h		;9109	e5		.
	defb 0ceh		;910a	ce		.
	defb 000h		;910b	00		.
	defb 084h		;910c	84		.
	defb 005h		;910d	05		.
	defb 0a4h		;910e	a4		.
	defb 033h		;910f	33		3
	defb 039h		;9110	39		9
	defb 018h		;9111	18		.
	defb 088h		;9112	88		.
	defb 081h		;9113	81		.
	defb 05eh		;9114	5e		^
	defb 077h		;9115	77		w
	defb 076h		;9116	76		v
	defb 0ebh		;9117	eb		.
	defb 0bbh		;9118	bb		.
	defb 084h		;9119	84		.
	defb 0bah		;911a	ba		.
	defb 000h		;911b	00		.
	defb 000h		;911c	00		.
	defb 03eh		;911d	3e		>
	defb 071h		;911e	71		q
	defb 081h		;911f	81		.
	defb 002h		;9120	02		.
	defb 082h		;9121	82		.
	defb 050h		;9122	50		P
	defb 080h		;9123	80		.
	defb 01ah		;9124	1a		.
	defb 025h		;9125	25		%
	defb 083h		;9126	83		.
	defb 0edh		;9127	ed		.
	defb 0dch		;9128	dc		.
	defb 0cdh		;9129	cd		.
	defb 004h		;912a	04		.
	defb 036h		;912b	36		6
	defb 051h		;912c	51		Q
	defb 000h		;912d	00		.
	defb 083h		;912e	83		.
	defb 0a7h		;912f	a7		.
	defb 010h		;9130	10		.
	defb 06ah		;9131	6a		j
	defb 074h		;9132	74		t
	defb 0e8h		;9133	e8		.
	defb 058h		;9134	58		X
	defb 0fbh		;9135	fb		.
	defb 084h		;9136	84		.
	defb 035h		;9137	35		5
	defb 065h		;9138	65		e
	defb 033h		;9139	33		3
	defb 04ah		;913a	4a		J
	defb 001h		;913b	01		.
	defb 001h		;913c	01		.
	defb 086h		;913d	86		.
	defb 000h		;913e	00		.
	defb 000h		;913f	00		.
	defb 07eh		;9140	7e		~
	defb 0bbh		;9141	bb		.
	defb 0aah		;9142	aa		.
	defb 0aeh		;9143	ae		.
	defb 0f0h		;9144	f0		.
	defb 0bbh		;9145	bb		.
	defb 081h		;9146	81		.
	defb 0beh		;9147	be		.
	defb 004h		;9148	04		.
	defb 087h		;9149	87		.
	defb 081h		;914a	81		.
	defb 06dh		;914b	6d		m
	defb 075h		;914c	75		u
	defb 006h		;914d	06		.
	defb 002h		;914e	02		.
	defb 001h		;914f	01		.
	defb 074h		;9150	74		t
	defb 0fch		;9151	fc		.
	defb 083h		;9152	83		.
	defb 0dah		;9153	da		.
	defb 0adh		;9154	ad		.
	defb 0cch		;9155	cc		.
	defb 070h		;9156	70		p
	defb 080h		;9157	80		.
	defb 00eh		;9158	0e		.
	defb 054h		;9159	54		T
	defb 083h		;915a	83		.
	defb 081h		;915b	81		.
	defb 074h		;915c	74		t
	defb 0adh		;915d	ad		.
	defb 0ceh		;915e	ce		.
	defb 000h		;915f	00		.
	defb 084h		;9160	84		.
	defb 003h		;9161	03		.
	defb 0a6h		;9162	a6		.
	defb 053h		;9163	53		S
	defb 096h		;9164	96		.
	defb 00dh		;9165	0d		.
	defb 0fbh		;9166	fb		.
	defb 019h		;9167	19		.
	defb 08ah		;9168	8a		.
	defb 081h		;9169	81		.
	defb 037h		;916a	37		7
	defb 008h		;916b	08		.
	defb 00bh		;916c	0b		.
	defb 0efh		;916d	ef		.
	defb 011h		;916e	11		.
	defb 081h		;916f	81		.
	defb 030h		;9170	30		0
	defb 071h		;9171	71		q
	defb 001h		;9172	01		.
	defb 001h		;9173	01		.
	defb 081h		;9174	81		.
	defb 005h		;9175	05		.
	defb 06fh		;9176	6f		o
	defb 075h		;9177	75		u
	defb 07ch		;9178	7c		|
	defb 00ah		;9179	0a		.
	defb 06ch		;917a	6c		l
	defb 00eh		;917b	0e		.
	defb 0cbh		;917c	cb		.
	defb 062h		;917d	62		b
	defb 080h		;917e	80		.
	defb 085h		;917f	85		.
	defb 0aah		;9180	aa		.
	defb 0aah		;9181	aa		.
	defb 030h		;9182	30		0
	defb 06dh		;9183	6d		m
	defb 0a3h		;9184	a3		.
	defb 0ceh		;9185	ce		.
	defb 000h		;9186	00		.
	defb 089h		;9187	89		.
	defb 05ah		;9188	5a		Z
	defb 046h		;9189	46		F
	defb 035h		;918a	35		5
	defb 06ah		;918b	6a		j
	defb 0bbh		;918c	bb		.
	defb 0beh		;918d	be		.
	defb 0edh		;918e	ed		.
	defb 0dah		;918f	da		.
	defb 073h		;9190	73		s
	defb 0f7h		;9191	f7		.
	defb 000h		;9192	00		.
	defb 081h		;9193	81		.
	defb 008h		;9194	08		.
	defb 063h		;9195	63		c
	defb 08eh		;9196	8e		.
	defb 070h		;9197	70		p
	defb 080h		;9198	80		.
	defb 070h		;9199	70		p
	defb 080h		;919a	80		.
	defb 070h		;919b	70		p
	defb 080h		;919c	80		.
	defb 084h		;919d	84		.
	defb 0aah		;919e	aa		.
	defb 013h		;919f	13		.
	defb 016h		;91a0	16		.
	defb 0adh		;91a1	ad		.
	defb 000h		;91a2	00		.
	defb 0afh		;91a3	af		.
	defb 0cbh		;91a4	cb		.
	defb 000h		;91a5	00		.
	defb 083h		;91a6	83		.
	defb 003h		;91a7	03		.
	defb 0aeh		;91a8	ae		.
	defb 053h		;91a9	53		S
	defb 015h		;91aa	15		.
	defb 07ch		;91ab	7c		|
	defb 004h		;91ac	04		.
	defb 078h		;91ad	78		x
	defb 084h		;91ae	84		.
	defb 0e6h		;91af	e6		.
	defb 066h		;91b0	66		f
	defb 066h		;91b1	66		f
	defb 065h		;91b2	65		e
	defb 0d3h		;91b3	d3		.
	defb 055h		;91b4	55		U
	defb 0dbh		;91b5	db		.
	defb 099h		;91b6	99		.
	defb 082h		;91b7	82		.
	defb 05bh		;91b8	5b		[
	defb 074h		;91b9	74		t
	defb 069h		;91ba	69		i
	defb 0b2h		;91bb	b2		.
	defb 081h		;91bc	81		.
	defb 0adh		;91bd	ad		.
	defb 071h		;91be	71		q
	defb 000h		;91bf	00		.
	defb 071h		;91c0	71		q
	defb 080h		;91c1	80		.
	defb 061h		;91c2	61		a
	defb 080h		;91c3	80		.
	defb 072h		;91c4	72		r
	defb 002h		;91c5	02		.
	defb 0c9h		;91c6	c9		.
	defb 000h		;91c7	00		.
	defb 084h		;91c8	84		.
	defb 034h		;91c9	34		4
	defb 061h		;91ca	61		a
	defb 033h		;91cb	33		3
	defb 06eh		;91cc	6e		n
	defb 069h		;91cd	69		i
	defb 0fah		;91ce	fa		.
	defb 026h		;91cf	26		&
	defb 052h		;91d0	52		R
	defb 005h		;91d1	05		.
	defb 004h		;91d2	04		.
	defb 085h		;91d3	85		.
	defb 0ebh		;91d4	eb		.
	defb 0ebh		;91d5	eb		.
	defb 0eeh		;91d6	ee		.
	defb 0ebh		;91d7	eb		.
	defb 0bbh		;91d8	bb		.
	defb 07ah		;91d9	7a		z
	defb 00ah		;91da	0a		.
	defb 0e5h		;91db	e5		.
	defb 0bbh		;91dc	bb		.
	defb 083h		;91dd	83		.
	defb 0bdh		;91de	bd		.
	defb 0aah		;91df	aa		.
	defb 0beh		;91e0	be		.
	defb 072h		;91e1	72		r
	defb 000h		;91e2	00		.
	defb 012h		;91e3	12		.
	defb 080h		;91e4	80		.
	defb 00eh		;91e5	0e		.
	defb 057h		;91e6	57		W
	defb 072h		;91e7	72		r
	defb 000h		;91e8	00		.
	defb 085h		;91e9	85		.
	defb 0aah		;91ea	aa		.
	defb 0aah		;91eb	aa		.
	defb 075h		;91ec	75		u
	defb 074h		;91ed	74		t
	defb 0aah		;91ee	aa		.
	defb 071h		;91ef	71		q
	defb 001h		;91f0	01		.
	defb 053h		;91f1	53		S
	defb 0c1h		;91f2	c1		.
	defb 081h		;91f3	81		.
	defb 0ebh		;91f4	eb		.
	defb 0e3h		;91f5	e3		.
	defb 033h		;91f6	33		3
	defb 082h		;91f7	82		.
	defb 099h		;91f8	99		.
	defb 09ah		;91f9	9a		.
	defb 0dfh		;91fa	df		.
	defb 0bbh		;91fb	bb		.
	defb 019h		;91fc	19		.
	defb 008h		;91fd	08		.
	defb 077h		;91fe	77		w
	defb 07ch		;91ff	7c		|
	defb 015h		;9200	15		.
	defb 0feh		;9201	fe		.
	defb 073h		;9202	73		s
	defb 080h		;9203	80		.
	defb 02eh		;9204	2e		.
	defb 0e1h		;9205	e1		.
	defb 082h		;9206	82		.
	defb 050h		;9207	50		P
	defb 09eh		;9208	9e		.
	defb 072h		;9209	72		r
	defb 002h		;920a	02		.
	defb 05bh		;920b	5b		[
	defb 0feh		;920c	fe		.
	defb 082h		;920d	82		.
	defb 05eh		;920e	5e		^
	defb 0edh		;920f	ed		.
	defb 0e0h		;9210	e0		.
	defb 0ddh		;9211	dd		.
	defb 086h		;9212	86		.
	defb 0d4h		;9213	d4		.
	defb 066h		;9214	66		f
	defb 067h		;9215	67		g
	defb 033h		;9216	33		3
	defb 033h		;9217	33		3
	defb 035h		;9218	35		5
	defb 075h		;9219	75		u
	defb 01ah		;921a	1a		.
	defb 0d2h		;921b	d2		.
	defb 0bbh		;921c	bb		.
	defb 083h		;921d	83		.
	defb 0bah		;921e	ba		.
	defb 0ddh		;921f	dd		.
	defb 0ebh		;9220	eb		.
	defb 054h		;9221	54		T
	defb 000h		;9222	00		.
	defb 03ah		;9223	3a		:
	defb 0f6h		;9224	f6		.
	defb 081h		;9225	81		.
	defb 0adh		;9226	ad		.
	defb 074h		;9227	74		t
	defb 080h		;9228	80		.
	defb 03fh		;9229	3f		?
	defb 060h		;922a	60		`
	defb 082h		;922b	82		.
	defb 055h		;922c	55		U
	defb 056h		;922d	56		V
	defb 071h		;922e	71		q
	defb 001h		;922f	01		.
	defb 064h		;9230	64		d
	defb 004h		;9231	04		.
	defb 0e0h		;9232	e0		.
	defb 055h		;9233	55		U
	defb 088h		;9234	88		.
	defb 053h		;9235	53		S
	defb 031h		;9236	31		1
	defb 0cbh		;9237	cb		.
	defb 0bbh		;9238	bb		.
	defb 0eeh		;9239	ee		.
	defb 068h		;923a	68		h
	defb 033h		;923b	33		3
	defb 033h		;923c	33		3
	defb 00bh		;923d	0b		.
	defb 073h		;923e	73		s
	defb 0d8h		;923f	d8		.
	defb 0bbh		;9240	bb		.
	defb 083h		;9241	83		.
	defb 0bdh		;9242	bd		.
	defb 0dah		;9243	da		.
	defb 0bbh		;9244	bb		.
	defb 014h		;9245	14		.
	defb 080h		;9246	80		.
	defb 04fh		;9247	4f		O
	defb 0d2h		;9248	d2		.
	defb 01dh		;9249	1d		.
	defb 0eeh		;924a	ee		.
	defb 055h		;924b	55		U
	defb 080h		;924c	80		.
	defb 051h		;924d	51		Q
	defb 000h		;924e	00		.
	defb 084h		;924f	84		.
	defb 0aah		;9250	aa		.
	defb 050h		;9251	50		P
	defb 04ah		;9252	4a		J
	defb 0d6h		;9253	d6		.
	defb 0f5h		;9254	f5		.
	defb 000h		;9255	00		.
	defb 086h		;9256	86		.
	defb 005h		;9257	05		.
	defb 055h		;9258	55		U
	defb 0bdh		;9259	bd		.
	defb 0e9h		;925a	e9		.
	defb 033h		;925b	33		3
	defb 033h		;925c	33		3
	defb 078h		;925d	78		x
	defb 074h		;925e	74		t
	defb 0cfh		;925f	cf		.
	defb 0bbh		;9260	bb		.
	defb 083h		;9261	83		.
	defb 0aah		;9262	aa		.
	defb 0abh		;9263	ab		.
	defb 0beh		;9264	be		.
	defb 005h		;9265	05		.
	defb 080h		;9266	80		.
	defb 04fh		;9267	4f		O
	defb 0a9h		;9268	a9		.
	defb 00fh		;9269	0f		.
	defb 0e5h		;926a	e5		.
	defb 001h		;926b	01		.
	defb 091h		;926c	91		.
	defb 089h		;926d	89		.
	defb 0cch		;926e	cc		.
	defb 0cah		;926f	ca		.
	defb 0abh		;9270	ab		.
	defb 0c3h		;9271	c3		.
	defb 033h		;9272	33		3
	defb 0bah		;9273	ba		.
	defb 0dah		;9274	da		.
	defb 03ah		;9275	3a		:
	defb 0fah		;9276	fa		.
	defb 04fh		;9277	4f		O
	defb 0e4h		;9278	e4		.
	defb 003h		;9279	03		.
	defb 0eeh		;927a	ee		.
	defb 0f7h		;927b	f7		.
	defb 000h		;927c	00		.
	defb 085h		;927d	85		.
	defb 055h		;927e	55		U
	defb 0cbh		;927f	cb		.
	defb 0b4h		;9280	b4		.
	defb 033h		;9281	33		3
	defb 039h		;9282	39		9
	defb 079h		;9283	79		y
	defb 006h		;9284	06		.
	defb 0ceh		;9285	ce		.
	defb 0bbh		;9286	bb		.
	defb 082h		;9287	82		.
	defb 0aah		;9288	aa		.
	defb 0bbh		;9289	bb		.
	defb 039h		;928a	39		9
	defb 06eh		;928b	6e		n
	defb 000h		;928c	00		.
	defb 07ch		;928d	7c		|
	defb 01fh		;928e	1f		.
	defb 0e5h		;928f	e5		.
	defb 017h		;9290	17		.
	defb 0cfh		;9291	cf		.
	defb 088h		;9292	88		.
	defb 0dch		;9293	dc		.
	defb 0cch		;9294	cc		.
	defb 0dch		;9295	dc		.
	defb 053h		;9296	53		S
	defb 03fh		;9297	3f		?
	defb 0aeh		;9298	ae		.
	defb 0aah		;9299	aa		.
	defb 033h		;929a	33		3
	defb 060h		;929b	60		`
	defb 081h		;929c	81		.
	defb 083h		;929d	83		.
	defb 030h		;929e	30		0
	defb 06dh		;929f	6d		m
	defb 0a5h		;92a0	a5		.
	defb 0e6h		;92a1	e6		.
	defb 055h		;92a2	55		U
	defb 081h		;92a3	81		.
	defb 080h		;92a4	80		.
	defb 0ceh		;92a5	ce		.
	defb 000h		;92a6	00		.
	defb 084h		;92a7	84		.
	defb 05bh		;92a8	5b		[
	defb 0bbh		;92a9	bb		.
	defb 033h		;92aa	33		3
	defb 059h		;92ab	59		Y
	defb 0d6h		;92ac	d6		.
	defb 0bbh		;92ad	bb		.
	defb 084h		;92ae	84		.
	defb 0aah		;92af	aa		.
	defb 0bbh		;92b0	bb		.
	defb 0bbh		;92b1	bb		.
	defb 0edh		;92b2	ed		.
	defb 01fh		;92b3	1f		.
	defb 0dah		;92b4	da		.
	defb 030h		;92b5	30		0
	defb 07eh		;92b6	7e		~
	defb 00dh		;92b7	0d		.
	defb 049h		;92b8	49		I
	defb 08dh		;92b9	8d		.
	defb 0b9h		;92ba	b9		.
	defb 099h		;92bb	99		.
	defb 0beh		;92bc	be		.
	defb 0adh		;92bd	ad		.
	defb 0cch		;92be	cc		.
	defb 0c7h		;92bf	c7		.
	defb 053h		;92c0	53		S
	defb 0cah		;92c1	ca		.
	defb 0aeh		;92c2	ae		.
	defb 0aah		;92c3	aa		.
	defb 0b2h		;92c4	b2		.
	defb 033h		;92c5	33		3
	defb 07ah		;92c6	7a		z
	defb 050h		;92c7	50		P
	defb 080h		;92c8	80		.
	defb 081h		;92c9	81		.
	defb 039h		;92ca	39		9
	defb 07dh		;92cb	7d		}
	defb 06fh		;92cc	6f		o
	defb 07dh		;92cd	7d		}
	defb 070h		;92ce	70		p
	defb 0d3h		;92cf	d3		.
	defb 0bbh		;92d0	bb		.
	defb 082h		;92d1	82		.
	defb 0bdh		;92d2	bd		.
	defb 055h		;92d3	55		U
	defb 0cdh		;92d4	cd		.
	defb 000h		;92d5	00		.
	defb 084h		;92d6	84		.
	defb 005h		;92d7	05		.
	defb 07bh		;92d8	7b		{
	defb 0b3h		;92d9	b3		.
	defb 035h		;92da	35		5
	defb 0d5h		;92db	d5		.
	defb 0bbh		;92dc	bb		.
	defb 084h		;92dd	84		.
	defb 0bah		;92de	ba		.
	defb 0ebh		;92df	eb		.
	defb 0bbh		;92e0	bb		.
	defb 0beh		;92e1	be		.
	defb 01fh		;92e2	1f		.
	defb 08fh		;92e3	8f		.
	defb 083h		;92e4	83		.
	defb 0ddh		;92e5	dd		.
	defb 0ddh		;92e6	dd		.
	defb 0adh		;92e7	ad		.
	defb 078h		;92e8	78		x
	defb 040h		;92e9	40		@
	defb 083h		;92ea	83		.
	defb 0dch		;92eb	dc		.
	defb 0cch		;92ec	cc		.
	defb 048h		;92ed	48		H
	defb 00fh		;92ee	0f		.
	defb 0eah		;92ef	ea		.
	defb 083h		;92f0	83		.
	defb 0ebh		;92f1	eb		.
	defb 0aah		;92f2	aa		.
	defb 0deh		;92f3	de		.
	defb 067h		;92f4	67		g
	defb 0feh		;92f5	fe		.
	defb 082h		;92f6	82		.
	defb 000h		;92f7	00		.
	defb 033h		;92f8	33		3
	defb 0e6h		;92f9	e6		.
	defb 000h		;92fa	00		.
	defb 083h		;92fb	83		.
	defb 07bh		;92fc	7b		{
	defb 0bbh		;92fd	bb		.
	defb 050h		;92fe	50		P
	defb 0cdh		;92ff	cd		.
	defb 000h		;9300	00		.
	defb 084h		;9301	84		.
	defb 005h		;9302	05		.
	defb 0bbh		;9303	bb		.
	defb 003h		;9304	03		.
	defb 06bh		;9305	6b		k
	defb 0d4h		;9306	d4		.
	defb 0bbh		;9307	bb		.
	defb 083h		;9308	83		.
	defb 0eeh		;9309	ee		.
	defb 0bbh		;930a	bb		.
	defb 0beh		;930b	be		.
	defb 01bh		;930c	1b		.
	defb 002h		;930d	02		.
	defb 00eh		;930e	0e		.
	defb 06dh		;930f	6d		m
	defb 02fh		;9310	2f		/
	defb 0e4h		;9311	e4		.
	defb 0c5h		;9312	c5		.
	defb 0cch		;9313	cc		.
	defb 081h		;9314	81		.
	defb 0bah		;9315	ba		.
	defb 01eh		;9316	1e		.
	defb 061h		;9317	61		a
	defb 000h		;9318	00		.
	defb 005h		;9319	05		.
	defb 05eh		;931a	5e		^
	defb 0bch		;931b	bc		.
	defb 0cch		;931c	cc		.
	defb 0eeh		;931d	ee		.
	defb 0d7h		;931e	d7		.
	defb 0bbh		;931f	bb		.
	defb 084h		;9320	84		.
	defb 0abh		;9321	ab		.
	defb 040h		;9322	40		@
	defb 000h		;9323	00		.
	defb 0bdh		;9324	bd		.
	defb 070h		;9325	70		p
	defb 080h		;9326	80		.
	defb 05fh		;9327	5f		_
	defb 0ach		;9328	ac		.
	defb 083h		;9329	83		.
	defb 0bdh		;932a	bd		.
	defb 083h		;932b	83		.
	defb 08bh		;932c	8b		.
	defb 0d6h		;932d	d6		.
	defb 0bbh		;932e	bb		.
	defb 02bh		;932f	2b		+
	defb 081h		;9330	81		.
	defb 00fh		;9331	0f		.
	defb 0eah		;9332	ea		.
	defb 083h		;9333	83		.
	defb 0dch		;9334	dc		.
	defb 0cfh		;9335	cf		.
	defb 0aah		;9336	aa		.
	defb 06eh		;9337	6e		n
	defb 0c6h		;9338	c6		.
	defb 01fh		;9339	1f		.
	defb 0eeh		;933a	ee		.
	defb 07eh		;933b	7e		~
	defb 0e0h		;933c	e0		.
	defb 0eah		;933d	ea		.
	defb 0bbh		;933e	bb		.
	defb 00ch		;933f	0c		.
	defb 070h		;9340	70		p
	defb 071h		;9341	71		q
	defb 000h		;9342	00		.
	defb 060h		;9343	60		`
	defb 080h		;9344	80		.
	defb 081h		;9345	81		.
	defb 0c3h		;9346	c3		.
	defb 070h		;9347	70		p
	defb 080h		;9348	80		.
	defb 0c9h		;9349	c9		.
	defb 0bbh		;934a	bb		.
	defb 007h		;934b	07		.
	defb 03dh		;934c	3d		=
	defb 03bh		;934d	3b		;
	defb 080h		;934e	80		.
	defb 012h		;934f	12		.
	defb 0fbh		;9350	fb		.
	defb 081h		;9351	81		.
	defb 0d4h		;9352	d4		.
	defb 078h		;9353	78		x
	defb 0ebh		;9354	eb		.
	defb 082h		;9355	82		.
	defb 078h		;9356	78		x
	defb 0dah		;9357	da		.
	defb 03fh		;9358	3f		?
	defb 060h		;9359	60		`
	defb 079h		;935a	79		y
	defb 07eh		;935b	7e		~
	defb 082h		;935c	82		.
	defb 030h		;935d	30		0
	defb 000h		;935e	00		.
	defb 001h		;935f	01		.
	defb 085h		;9360	85		.
	defb 0e2h		;9361	e2		.
	defb 000h		;9362	00		.
	defb 082h		;9363	82		.
	defb 008h		;9364	08		.
	defb 0bbh		;9365	bb		.
	defb 076h		;9366	76		v
	defb 02fh		;9367	2f		/
	defb 05eh		;9368	5e		^
	defb 0aah		;9369	aa		.
	defb 082h		;936a	82		.
	defb 0bah		;936b	ba		.
	defb 053h		;936c	53		S
	defb 07ah		;936d	7a		z
	defb 07ah		;936e	7a		z
	defb 0c8h		;936f	c8		.
	defb 0bbh		;9370	bb		.
	defb 081h		;9371	81		.
	defb 0beh		;9372	be		.
	defb 001h		;9373	01		.
	defb 080h		;9374	80		.
	defb 03dh		;9375	3d		=
	defb 001h		;9376	01		.
	defb 00eh		;9377	0e		.
	defb 0f0h		;9378	f0		.
	defb 083h		;9379	83		.
	defb 0d4h		;937a	d4		.
	defb 044h		;937b	44		D
	defb 088h		;937c	88		.
	defb 05fh		;937d	5f		_
	defb 053h		;937e	53		S
	defb 08ah		;937f	8a		.
	defb 078h		;9380	78		x
	defb 088h		;9381	88		.
	defb 084h		;9382	84		.
	defb 088h		;9383	88		.
	defb 0ach		;9384	ac		.
	defb 0eeh		;9385	ee		.
	defb 0eeh		;9386	ee		.
	defb 0e7h		;9387	e7		.
	defb 055h		;9388	55		U
	defb 058h		;9389	58		X
	defb 04fh		;938a	4f		O
	defb 0fbh		;938b	fb		.
	defb 088h		;938c	88		.
	defb 0eeh		;938d	ee		.
	defb 0eeh		;938e	ee		.
	defb 0e8h		;938f	e8		.
	defb 053h		;9390	53		S
	defb 06eh		;9391	6e		n
	defb 0eeh		;9392	ee		.
	defb 0beh		;9393	be		.
	defb 0ebh		;9394	eb		.
	defb 01fh		;9395	1f		.
	defb 0f4h		;9396	f4		.
	defb 07ch		;9397	7c		|
	defb 040h		;9398	40		@
	defb 0d3h		;9399	d3		.
	defb 0bbh		;939a	bb		.
	defb 082h		;939b	82		.
	defb 0b9h		;939c	b9		.
	defb 090h		;939d	90		.
	defb 0ceh		;939e	ce		.
	defb 000h		;939f	00		.
	defb 082h		;93a0	82		.
	defb 05bh		;93a1	5b		[
	defb 073h		;93a2	73		s
	defb 072h		;93a3	72		r
	defb 080h		;93a4	80		.
	defb 0cbh		;93a5	cb		.
	defb 0bbh		;93a6	bb		.
	defb 05ch		;93a7	5c		\
	defb 07eh		;93a8	7e		~
	defb 01fh		;93a9	1f		.
	defb 071h		;93aa	71		q
	defb 083h		;93ab	83		.
	defb 0dah		;93ac	da		.
	defb 04eh		;93ad	4e		N
	defb 0e4h		;93ae	e4		.
	defb 016h		;93af	16		.
	defb 053h		;93b0	53		S
	defb 01dh		;93b1	1d		.
	defb 0cfh		;93b2	cf		.
	defb 088h		;93b3	88		.
	defb 064h		;93b4	64		d
	defb 044h		;93b5	44		D
	defb 0cch		;93b6	cc		.
	defb 088h		;93b7	88		.
	defb 0eeh		;93b8	ee		.
	defb 0eeh		;93b9	ee		.
	defb 075h		;93ba	75		u
	defb 055h		;93bb	55		U
	defb 062h		;93bc	62		b
	defb 0eah		;93bd	ea		.
	defb 087h		;93be	87		.
	defb 0beh		;93bf	be		.
	defb 0dah		;93c0	da		.
	defb 050h		;93c1	50		P
	defb 056h		;93c2	56		V
	defb 0bbh		;93c3	bb		.
	defb 049h		;93c4	49		I
	defb 096h		;93c5	96		.
	defb 056h		;93c6	56		V
	defb 074h		;93c7	74		t
	defb 006h		;93c8	06		.
	defb 0f6h		;93c9	f6		.
	defb 0d5h		;93ca	d5		.
	defb 066h		;93cb	66		f
	defb 081h		;93cc	81		.
	defb 060h		;93cd	60		`
	defb 0cfh		;93ce	cf		.
	defb 000h		;93cf	00		.
	defb 084h		;93d0	84		.
	defb 005h		;93d1	05		.
	defb 0bdh		;93d2	bd		.
	defb 053h		;93d3	53		S
	defb 054h		;93d4	54		T
	defb 0d3h		;93d5	d3		.
	defb 0bbh		;93d6	bb		.
	defb 00fh		;93d7	0f		.
	defb 001h		;93d8	01		.
	defb 03fh		;93d9	3f		?
	defb 082h		;93da	82		.
	defb 00eh		;93db	0e		.
	defb 074h		;93dc	74		t
	defb 00fh		;93dd	0f		.
	defb 0f3h		;93de	f3		.
	defb 04bh		;93df	4b		K
	defb 042h		;93e0	42		B
	defb 08ah		;93e1	8a		.
	defb 0dah		;93e2	da		.
	defb 0aeh		;93e3	ae		.
	defb 066h		;93e4	66		f
	defb 06eh		;93e5	6e		n
	defb 0dch		;93e6	dc		.
	defb 0cch		;93e7	cc		.
	defb 0ceh		;93e8	ce		.
	defb 0bah		;93e9	ba		.
	defb 055h		;93ea	55		U
	defb 058h		;93eb	58		X
	defb 00dh		;93ec	0d		.
	defb 08ah		;93ed	8a		.
	defb 05fh		;93ee	5f		_
	defb 0b9h		;93ef	b9		.
	defb 083h		;93f0	83		.
	defb 0a4h		;93f1	a4		.
	defb 000h		;93f2	00		.
	defb 09bh		;93f3	9b		.
	defb 01ch		;93f4	1c		.
	defb 040h		;93f5	40		@
	defb 0efh		;93f6	ef		.
	defb 000h		;93f7	00		.
	defb 084h		;93f8	84		.
	defb 003h		;93f9	03		.
	defb 09bh		;93fa	9b		.
	defb 095h		;93fb	95		.
	defb 055h		;93fc	55		U
	defb 070h		;93fd	70		p
	defb 07fh		;93fe	7f		.
	defb 0c9h		;93ff	c9		.
	defb 0bbh		;9400	bb		.
	defb 081h		;9401	81		.
	defb 0b6h		;9402	b6		.
	defb 02dh		;9403	2d		-
	defb 07ch		;9404	7c		|
	defb 03eh		;9405	3e		>
	defb 07eh		;9406	7e		~
	defb 010h		;9407	10		.
	defb 081h		;9408	81		.
	defb 045h		;9409	45		E
	defb 083h		;940a	83		.
	defb 000h		;940b	00		.
	defb 088h		;940c	88		.
	defb 08bh		;940d	8b		.
	defb 0eeh		;940e	ee		.
	defb 0b6h		;940f	b6		.
	defb 04dh		;9410	4d		M
	defb 0cch		;9411	cc		.
	defb 0ceh		;9412	ce		.
	defb 0aah		;9413	aa		.
	defb 053h		;9414	53		S
	defb 057h		;9415	57		W
	defb 0ddh		;9416	dd		.
	defb 0cch		;9417	cc		.
	defb 0deh		;9418	de		.
	defb 001h		;9419	01		.
	defb 0a4h		;941a	a4		.
	defb 023h		;941b	23		#
	defb 028h		;941c	28		(
	defb 084h		;941d	84		.
	defb 0aeh		;941e	ae		.
	defb 050h		;941f	50		P
	defb 006h		;9420	06		.
	defb 0aeh		;9421	ae		.
	defb 073h		;9422	73		s
	defb 059h		;9423	59		Y
	defb 0e6h		;9424	e6		.
	defb 000h		;9425	00		.
	defb 086h		;9426	86		.
	defb 003h		;9427	03		.
	defb 039h		;9428	39		9
	defb 0b6h		;9429	b6		.
	defb 047h		;942a	47		G
	defb 055h		;942b	55		U
	defb 055h		;942c	55		U
	defb 0d2h		;942d	d2		.
	defb 0bbh		;942e	bb		.
	defb 082h		;942f	82		.
	defb 0b6h		;9430	b6		.
	defb 04ah		;9431	4a		J
	defb 01dh		;9432	1d		.
	defb 0fbh		;9433	fb		.
	defb 07fh		;9434	7f		.
	defb 07eh		;9435	7e		~
	defb 073h		;9436	73		s
	defb 080h		;9437	80		.
	defb 08ah		;9438	8a		.
	defb 0ddh		;9439	dd		.
	defb 0dah		;943a	da		.
	defb 0e6h		;943b	e6		.
	defb 044h		;943c	44		D
	defb 0dch		;943d	dc		.
	defb 0cbh		;943e	cb		.
	defb 0aah		;943f	aa		.
	defb 013h		;9440	13		.
	defb 035h		;9441	35		5
	defb 096h		;9442	96		.
	defb 004h		;9443	04		.
	defb 09bh		;9444	9b		.
	defb 040h		;9445	40		@
	defb 082h		;9446	82		.
	defb 087h		;9447	87		.
	defb 0beh		;9448	be		.
	defb 067h		;9449	67		g
	defb 005h		;944a	05		.
	defb 06eh		;944b	6e		n
	defb 0aah		;944c	aa		.
	defb 04eh		;944d	4e		N
	defb 0eah		;944e	ea		.
	defb 073h		;944f	73		s
	defb 08fh		;9450	8f		.
	defb 0d2h		;9451	d2		.
	defb 0eeh		;9452	ee		.
	defb 081h		;9453	81		.
	defb 069h		;9454	69		i
	defb 0cah		;9455	ca		.
	defb 033h		;9456	33		3
	defb 00eh		;9457	0e		.
	defb 0fbh		;9458	fb		.
	defb 085h		;9459	85		.
	defb 096h		;945a	96		.
	defb 067h		;945b	67		g
	defb 055h		;945c	55		U
	defb 055h		;945d	55		U
	defb 069h		;945e	69		i
	defb 0d2h		;945f	d2		.
	defb 0bbh		;9460	bb		.
	defb 081h		;9461	81		.
	defb 0e4h		;9462	e4		.
	defb 00ch		;9463	0c		.
	defb 0b1h		;9464	b1		.
	defb 07fh		;9465	7f		.
	defb 0fch		;9466	fc		.
	defb 07fh		;9467	7f		.
	defb 083h		;9468	83		.
	defb 00fh		;9469	0f		.
	defb 080h		;946a	80		.
	defb 081h		;946b	81		.
	defb 0cdh		;946c	cd		.
	defb 030h		;946d	30		0
	defb 080h		;946e	80		.
	defb 088h		;946f	88		.
	defb 053h		;9470	53		S
	defb 035h		;9471	35		5
	defb 099h		;9472	99		.
	defb 096h		;9473	96		.
	defb 04ah		;9474	4a		J
	defb 0ddh		;9475	dd		.
	defb 0cdh		;9476	cd		.
	defb 0ddh		;9477	dd		.
	defb 005h		;9478	05		.
	defb 023h		;9479	23		#
	defb 00fh		;947a	0f		.
	defb 0c3h		;947b	c3		.
	defb 083h		;947c	83		.
	defb 0a6h		;947d	a6		.
	defb 070h		;947e	70		p
	defb 099h		;947f	99		.
	defb 074h		;9480	74		t
	defb 045h		;9481	45		E
	defb 0e3h		;9482	e3		.
	defb 0bbh		;9483	bb		.
	defb 081h		;9484	81		.
	defb 0b6h		;9485	b6		.
	defb 019h		;9486	19		.
	defb 01ah		;9487	1a		.
	defb 081h		;9488	81		.
	defb 058h		;9489	58		X
	defb 070h		;948a	70		p
	defb 07fh		;948b	7f		.
	defb 0c8h		;948c	c8		.
	defb 0bbh		;948d	bb		.
	defb 083h		;948e	83		.
	defb 066h		;948f	66		f
	defb 064h		;9490	64		d
	defb 04dh		;9491	4d		M
	defb 014h		;9492	14		.
	defb 0f7h		;9493	f7		.
	defb 07fh		;9494	7f		.
	defb 0fbh		;9495	fb		.
	defb 0c8h		;9496	c8		.
	defb 0ffh		;9497	ff		.
	defb 01ch		;9498	1c		.
	defb 005h		;9499	05		.
	defb 001h		;949a	01		.
	defb 000h		;949b	00		.
	defb 081h		;949c	81		.
	defb 0adh		;949d	ad		.
	defb 000h		;949e	00		.
	defb 080h		;949f	80		.
	defb 083h		;94a0	83		.
	defb 099h		;94a1	99		.
	defb 066h		;94a2	66		f
	defb 066h		;94a3	66		f
	defb 001h		;94a4	01		.
	defb 026h		;94a5	26		&
	defb 082h		;94a6	82		.
	defb 0ddh		;94a7	dd		.
	defb 0dah		;94a8	da		.
	defb 00fh		;94a9	0f		.
	defb 035h		;94aa	35		5
	defb 082h		;94ab	82		.
	defb 0edh		;94ac	ed		.
	defb 044h		;94ad	44		D
	defb 0efh		;94ae	ef		.
	defb 033h		;94af	33		3
	defb 081h		;94b0	81		.
	defb 099h		;94b1	99		.
	defb 07dh		;94b2	7d		}
	defb 016h		;94b3	16		.
	defb 0c9h		;94b4	c9		.
	defb 0bbh		;94b5	bb		.
	defb 002h		;94b6	02		.
	defb 0e1h		;94b7	e1		.
	defb 020h		;94b8	20		 
	defb 0fdh		;94b9	fd		.
	defb 07fh		;94ba	7f		.
	defb 078h		;94bb	78		x
	defb 0cah		;94bc	ca		.
	defb 0ffh		;94bd	ff		.
	defb 01fh		;94be	1f		.
	defb 003h		;94bf	03		.
	defb 040h		;94c0	40		@
	defb 080h		;94c1	80		.
	defb 013h		;94c2	13		.
	defb 00dh		;94c3	0d		.
	defb 081h		;94c4	81		.
	defb 044h		;94c5	44		D
	defb 015h		;94c6	15		.
	defb 09ah		;94c7	9a		.
	defb 025h		;94c8	25		%
	defb 02ch		;94c9	2c		,
	defb 07fh		;94ca	7f		.
	defb 0bbh		;94cb	bb		.
	defb 0fah		;94cc	fa		.
	defb 0bbh		;94cd	bb		.
	defb 000h		;94ce	00		.
	defb 053h		;94cf	53		S
	defb 021h		;94d0	21		!
	defb 07ch		;94d1	7c		|
	defb 07fh		;94d2	7f		.
	defb 076h		;94d3	76		v
	defb 0cbh		;94d4	cb		.
	defb 0ffh		;94d5	ff		.
	defb 00fh		;94d6	0f		.
	defb 083h		;94d7	83		.
	defb 081h		;94d8	81		.
	defb 0a4h		;94d9	a4		.
	defb 031h		;94da	31		1
	defb 000h		;94db	00		.
	defb 084h		;94dc	84		.
	defb 096h		;94dd	96		.
	defb 044h		;94de	44		D
	defb 044h		;94df	44		D
	defb 0ddh		;94e0	dd		.
	defb 0c2h		;94e1	c2		.
	defb 044h		;94e2	44		D
	defb 02fh		;94e3	2f		/
	defb 0feh		;94e4	fe		.
	defb 0ffh		;94e5	ff		.
	defb 0bbh		;94e6	bb		.
	defb 030h		;94e7	30		0
	defb 0fch		;94e8	fc		.
	defb 030h		;94e9	30		0
	defb 07eh		;94ea	7e		~
	defb 07fh		;94eb	7f		.
	defb 074h		;94ec	74		t
	defb 0cdh		;94ed	cd		.
	defb 0ffh		;94ee	ff		.
	defb 070h		;94ef	70		p
	defb 080h		;94f0	80		.
	defb 082h		;94f1	82		.
	defb 064h		;94f2	64		d
	defb 044h		;94f3	44		D
	defb 01eh		;94f4	1e		.
	defb 0d5h		;94f5	d5		.
	defb 010h		;94f6	10		.
	defb 082h		;94f7	82		.
	defb 082h		;94f8	82		.
	defb 0eeh		;94f9	ee		.
	defb 04ah		;94fa	4a		J
	defb 01dh		;94fb	1d		.
	defb 083h		;94fc	83		.
	defb 077h		;94fd	77		w
	defb 0c7h		;94fe	c7		.
	defb 0f3h		;94ff	f3		.
	defb 0bbh		;9500	bb		.
	defb 081h		;9501	81		.
	defb 0b6h		;9502	b6		.
	defb 014h		;9503	14		.
	defb 05bh		;9504	5b		[
	defb 000h		;9505	00		.
	defb 054h		;9506	54		T
	defb 009h		;9507	09		.
	defb 069h		;9508	69		i
	defb 071h		;9509	71		q
	defb 0fah		;950a	fa		.
	defb 0ceh		;950b	ce		.
	defb 0ffh		;950c	ff		.
	defb 00fh		;950d	0f		.
	defb 084h		;950e	84		.
	defb 081h		;950f	81		.
	defb 0d4h		;9510	d4		.
	defb 003h		;9511	03		.
	defb 000h		;9512	00		.
	defb 084h		;9513	84		.
	defb 0bah		;9514	ba		.
	defb 033h		;9515	33		3
	defb 035h		;9516	35		5
	defb 064h		;9517	64		d
	defb 023h		;9518	23		#
	defb 022h		;9519	22		"
	defb 040h		;951a	40		@
	defb 083h		;951b	83		.
	defb 081h		;951c	81		.
	defb 04eh		;951d	4e		N
	defb 00dh		;951e	0d		.
	defb 0fbh		;951f	fb		.
	defb 081h		;9520	81		.
	defb 0aah		;9521	aa		.
	defb 042h		;9522	42		B
	defb 08dh		;9523	8d		.
	defb 0f1h		;9524	f1		.
	defb 0bbh		;9525	bb		.
	defb 050h		;9526	50		P
	defb 07dh		;9527	7d		}
	defb 070h		;9528	70		p
	defb 07eh		;9529	7e		~
	defb 0d4h		;952a	d4		.
	defb 0ffh		;952b	ff		.
	defb 01dh		;952c	1d		.
	defb 086h		;952d	86		.
	defb 087h		;952e	87		.
	defb 044h		;952f	44		D
	defb 04ch		;9530	4c		L
	defb 087h		;9531	87		.
	defb 071h		;9532	71		q
	defb 033h		;9533	33		3
	defb 035h		;9534	35		5
	defb 044h		;9535	44		D
	defb 047h		;9536	47		G
	defb 015h		;9537	15		.
	defb 041h		;9538	41		A
	defb 005h		;9539	05		.
	defb 081h		;953a	81		.
	defb 044h		;953b	44		D
	defb 022h		;953c	22		"
	defb 00bh		;953d	0b		.
	defb 081h		;953e	81		.
	defb 0aeh		;953f	ae		.
	defb 07bh		;9540	7b		{
	defb 064h		;9541	64		d
	defb 0e8h		;9542	e8		.
	defb 0bbh		;9543	bb		.
	defb 012h		;9544	12		.
	defb 049h		;9545	49		I
	defb 081h		;9546	81		.
	defb 044h		;9547	44		D
	defb 041h		;9548	41		A
	defb 07ah		;9549	7a		z
	defb 073h		;954a	73		s
	defb 0f4h		;954b	f4		.
	defb 0d2h		;954c	d2		.
	defb 0ffh		;954d	ff		.
	defb 01fh		;954e	1f		.
	defb 006h		;954f	06		.
	defb 086h		;9550	86		.
	defb 044h		;9551	44		D
	defb 044h		;9552	44		D
	defb 087h		;9553	87		.
	defb 077h		;9554	77		w
	defb 055h		;9555	55		U
	defb 086h		;9556	86		.
	defb 065h		;9557	65		e
	defb 09eh		;9558	9e		.
	defb 029h		;9559	29		)
	defb 09dh		;955a	9d		.
	defb 0c4h		;955b	c4		.
	defb 044h		;955c	44		D
	defb 085h		;955d	85		.
	defb 0aah		;955e	aa		.
	defb 0aah		;955f	aa		.
	defb 0adh		;9560	ad		.
	defb 0ddh		;9561	dd		.
	defb 0dah		;9562	da		.
	defb 013h		;9563	13		.
	defb 012h		;9564	12		.
	defb 0e5h		;9565	e5		.
	defb 0bbh		;9566	bb		.
	defb 001h		;9567	01		.
	defb 076h		;9568	76		v
	defb 010h		;9569	10		.
	defb 07ch		;956a	7c		|
	defb 001h		;956b	01		.
	defb 0cch		;956c	cc		.
	defb 008h		;956d	08		.
	defb 069h		;956e	69		i
	defb 074h		;956f	74		t
	defb 071h		;9570	71		q
	defb 0d7h		;9571	d7		.
	defb 0ffh		;9572	ff		.
	defb 030h		;9573	30		0
	defb 06dh		;9574	6d		m
	defb 012h		;9575	12		.
	defb 0a6h		;9576	a6		.
	defb 054h		;9577	54		T
	defb 09fh		;9578	9f		.
	defb 025h		;9579	25		%
	defb 01fh		;957a	1f		.
	defb 00bh		;957b	0b		.
	defb 051h		;957c	51		Q
	defb 002h		;957d	02		.
	defb 08dh		;957e	8d		.
	defb 083h		;957f	83		.
	defb 064h		;9580	64		d
	defb 046h		;9581	46		F
	defb 066h		;9582	66		f
	defb 032h		;9583	32		2
	defb 08fh		;9584	8f		.
	defb 028h		;9585	28		(
	defb 018h		;9586	18		.
	defb 07fh		;9587	7f		.
	defb 084h		;9588	84		.
	defb 0d3h		;9589	d3		.
	defb 0bbh		;958a	bb		.
	defb 026h		;958b	26		&
	defb 02bh		;958c	2b		+
	defb 001h		;958d	01		.
	defb 0f4h		;958e	f4		.
	defb 070h		;958f	70		p
	defb 07eh		;9590	7e		~
	defb 0e1h		;9591	e1		.
	defb 0ffh		;9592	ff		.
	defb 056h		;9593	56		V
	defb 00dh		;9594	0d		.
	defb 075h		;9595	75		u
	defb 01eh		;9596	1e		.
	defb 045h		;9597	45		E
	defb 020h		;9598	20		 
	defb 01bh		;9599	1b		.
	defb 0d5h		;959a	d5		.
	defb 031h		;959b	31		1
	defb 08ah		;959c	8a		.
	defb 006h		;959d	06		.
	defb 0a7h		;959e	a7		.
	defb 02dh		;959f	2d		-
	defb 001h		;95a0	01		.
	defb 005h		;95a1	05		.
	defb 012h		;95a2	12		.
	defb 01dh		;95a3	1d		.
	defb 009h		;95a4	09		.
	defb 038h		;95a5	38		8
	defb 030h		;95a6	30		0
	defb 07fh		;95a7	7f		.
	defb 090h		;95a8	90		.
	defb 081h		;95a9	81		.
	defb 0beh		;95aa	be		.
	defb 01fh		;95ab	1f		.
	defb 09ch		;95ac	9c		.
	defb 081h		;95ad	81		.
	defb 0b6h		;95ae	b6		.
	defb 030h		;95af	30		0
	defb 07ah		;95b0	7a		z
	defb 004h		;95b1	04		.
	defb 0e9h		;95b2	e9		.
	defb 030h		;95b3	30		0
	defb 0fah		;95b4	fa		.
	defb 075h		;95b5	75		u
	defb 06ch		;95b6	6c		l
	defb 0dch		;95b7	dc		.
	defb 0ffh		;95b8	ff		.
	defb 066h		;95b9	66		f
	defb 00eh		;95ba	0e		.
	defb 074h		;95bb	74		t
	defb 0a0h		;95bc	a0		.
	defb 055h		;95bd	55		U
	defb 0a2h		;95be	a2		.
	defb 026h		;95bf	26		&
	defb 0a6h		;95c0	a6		.
	defb 032h		;95c1	32		2
	defb 00dh		;95c2	0d		.
	defb 081h		;95c3	81		.
	defb 04eh		;95c4	4e		N
	defb 07ah		;95c5	7a		z
	defb 00dh		;95c6	0d		.
	defb 07ah		;95c7	7a		z
	defb 012h		;95c8	12		.
	defb 019h		;95c9	19		.
	defb 054h		;95ca	54		T
	defb 01dh		;95cb	1d		.
	defb 087h		;95cc	87		.
	defb 083h		;95cd	83		.
	defb 0eeh		;95ce	ee		.
	defb 0eeh		;95cf	ee		.
	defb 06eh		;95d0	6e		n
	defb 031h		;95d1	31		1
	defb 074h		;95d2	74		t
	defb 030h		;95d3	30		0
	defb 0c8h		;95d4	c8		.
	defb 072h		;95d5	72		r
	defb 0f2h		;95d6	f2		.
	defb 0e3h		;95d7	e3		.
	defb 0ffh		;95d8	ff		.
	defb 026h		;95d9	26		&
	defb 090h		;95da	90		.
	defb 075h		;95db	75		u
	defb 01eh		;95dc	1e		.
	defb 076h		;95dd	76		v
	defb 025h		;95de	25		%
	defb 056h		;95df	56		V
	defb 0a3h		;95e0	a3		.
	defb 06ch		;95e1	6c		l
	defb 0ffh		;95e2	ff		.
	defb 079h		;95e3	79		y
	defb 02ch		;95e4	2c		,
	defb 03eh		;95e5	3e		>
	defb 011h		;95e6	11		.
	defb 01fh		;95e7	1f		.
	defb 0b5h		;95e8	b5		.
	defb 07ah		;95e9	7a		z
	defb 04ch		;95ea	4c		L
	defb 030h		;95eb	30		0
	defb 0f8h		;95ec	f8		.
	defb 075h		;95ed	75		u
	defb 068h		;95ee	68		h
	defb 0ffh		;95ef	ff		.
	defb 0ffh		;95f0	ff		.
	defb 076h		;95f1	76		v
	defb 0a8h		;95f2	a8		.
	defb 0ddh		;95f3	dd		.
	defb 0ddh		;95f4	dd		.
	defb 05ah		;95f5	5a		Z
	defb 051h		;95f6	51		Q
	defb 0ffh		;95f7	ff		.
	defb 0ffh		;95f8	ff		.
	defb 0d0h		;95f9	d0		.
	defb 0ffh		;95fa	ff		.
	defb 077h		;95fb	77		w
	defb 0b1h		;95fc	b1		.
	defb 0d8h		;95fd	d8		.
	defb 0cch		;95fe	cc		.
	defb 0ffh		;95ff	ff		.
	defb 0ffh		;9600	ff		.
	defb 0ddh		;9601	dd		.
	defb 0ffh		;9602	ff		.
	defb 0d5h		;9603	d5		.
	defb 0cch		;9604	cc		.
	defb 076h		;9605	76		v
	defb 0d9h		;9606	d9		.
	defb 0ffh		;9607	ff		.
	defb 0ffh		;9608	ff		.
	defb 0ffh		;9609	ff		.
	defb 0ffh		;960a	ff		.
	defb 0ffh		;960b	ff		.
	defb 0ffh		;960c	ff		.
	defb 0ffh		;960d	ff		.
	defb 0ffh		;960e	ff		.
	defb 0ffh		;960f	ff		.
	defb 0ffh		;9610	ff		.
	defb 0ffh		;9611	ff		.
	defb 0ffh		;9612	ff		.
	defb 0ffh		;9613	ff		.
	defb 0ffh		;9614	ff		.
	defb 0ffh		;9615	ff		.
	defb 0ffh		;9616	ff		.
	defb 0ffh		;9617	ff		.
	defb 0ffh		;9618	ff		.
	defb 0ffh		;9619	ff		.
	defb 0ffh		;961a	ff		.
	defb 0ffh		;961b	ff		.
	defb 0ffh		;961c	ff		.
	defb 0ffh		;961d	ff		.
	defb 0ffh		;961e	ff		.
	defb 0cah		;961f	ca		.
	defb 0ffh		;9620	ff		.
	defb 080h		;9621	80		.
	defb 0c9h		;9622	c9		.
l9623h:
	call sub_9630h		;9623	cd 30 96	. 0 .
MASTERC:
				; = MASTERC (src: AUTOIDE.ASM:42, BIOS-PP 1273243)
	ld a,002h		;9626	3e 02		> .
	ld (IDEDEV),a		;9628	32 05 98	2 . .
	ld a,0ffh		;962b	3e ff		> .
	jp CDMASTR		;962d	c3 51 96	. Q .
sub_9630h:
	and a			;9630	a7		.
	ld d,0a0h		;9631	16 a0		. .
	jr z,l963ah		;9633	28 05		( .
	dec a			;9635	3d		=
	ld d,0b0h		;9636	16 b0		. .
	jr z,l963ah		;9638	28 00		( .
l963ah:
	ld a,021h		;963a	3e 21		> !
	out (0bch),a		;963c	d3 bc		. .
	ld a,000h		;963e	3e 00		> .
	ld (l9806h),a		;9640	32 06 98	2 . .
	ld bc,04152h		;9643	01 52 41	. R A
	out (c),d		;9646	ed 51		. Q
	ret			;9648	c9		.
sub_9649h:
	call sub_9630h		;9649	cd 30 96	. 0 .
MASTER:
				; = MASTER (src: AUTOIDE.ASM:51, BIOS-PP 1273243)
	ld a,0ffh		;964c	3e ff		> .
	ld (IDEDEV),a		;964e	32 05 98	2 . .
CDMASTR:
				; = CDMASTR (src: AUTOIDE.ASM:53, BIOS-PP 1273243)
	ld (SKIP),a		;9651	32 04 98	2 . .
	ld bc,04152h		;9654	01 52 41	. R A
	out (c),d		;9657	ed 51		. Q
	ld bc,04053h		;9659	01 53 40	. S @
	in a,(c)		;965c	ed 78		. x
	and 080h		;965e	e6 80		. .
	ld hl,00118h		;9660	21 18 01	! . .
	jr z,NO_BUSY	;9663	28 1a		( .
	ld hl,0060eh		;9665	21 0e 06	! . .
	ei			;9668	fb		.
CLRBUSY:
				; = CLRBUSY (src: AUTOIDE.ASM:64, BIOS-PP 1273243)
	halt			;9669	76		v

; BLOCK 'Data966A' (start 0x966a end 0x967f)
Data966A:
	defb 02bh		;966a	2b		+
	defb 07ch		;966b	7c		|
	defb 0b5h		;966c	b5		.
	defb 0cah		;966d	ca		.
	defb 01ah		;966e	1a		.
	defb 097h		;966f	97		.
	defb 0cdh		;9670	cd		.
	defb 0e6h		;9671	e6		.
	defb 097h		;9672	97		.
	defb 0dah		;9673	da		.
	defb 01ah		;9674	1a		.
	defb 097h		;9675	97		.
	defb 001h		;9676	01		.
	defb 053h		;9677	53		S
	defb 040h		;9678	40		@
	defb 0edh		;9679	ed		.
	defb 078h		;967a	78		x
	defb 0e6h		;967b	e6		.
	defb 080h		;967c	80		.
	defb 020h		;967d	20		 
	defb 0eah		;967e	ea		.
NO_BUSY:
				; = NO_BUSY (src: AUTOIDE.ASM:76, BIOS-PP 1273243)
	ld e,005h		;967f	1e 05		. .
	ld bc,00152h		;9681	01 52 01	. R .
	out (c),e		;9684	ed 59		. Y
	ld bc,00010h		;9686	01 10 00	. . .
l9689h:
	djnz l9689h		;9689	10 fe		. .
	dec c			;968b	0d		.
	jr nz,l9689h		;968c	20 fb		  .
	ld bc,00052h		;968e	01 52 00	. R .
	in a,(c)		;9691	ed 78		. x
	cp e			;9693	bb		.
	jp nz,ABSENT		;9694	c2 1a 97	. . .
	ld a,(IDEDEV)		;9697	3a 05 98	: . .
	cp 002h			;969a	fe 02		. .
	jp z,NOHDD		;969c	ca fb 96	. . .
	ld e,000h		;969f	1e 00		. .
	ld bc,04153h		;96a1	01 53 41	. S A
	out (c),e		;96a4	ed 59		. Y
WXREADY:
				; = WXREADY (src: AUTOIDE.ASM:96, BIOS-PP 1273243)
	halt			;96a6	76		v

; BLOCK 'Data96A7' (start 0x96a7 end 0x96dd)
Data96A7:
	defb 02bh		;96a7	2b		+
	defb 07ch		;96a8	7c		|
	defb 0b5h		;96a9	b5		.
	defb 0cah		;96aa	ca		.
	defb 01ah		;96ab	1a		.
	defb 097h		;96ac	97		.
	defb 0cdh		;96ad	cd		.
	defb 0e6h		;96ae	e6		.
	defb 097h		;96af	97		.
	defb 0dah		;96b0	da		.
	defb 01ah		;96b1	1a		.
	defb 097h		;96b2	97		.
	defb 001h		;96b3	01		.
	defb 053h		;96b4	53		S
	defb 040h		;96b5	40		@
	defb 0edh		;96b6	ed		.
	defb 078h		;96b7	78		x
	defb 0e6h		;96b8	e6		.
	defb 0c0h		;96b9	c0		.
	defb 0feh		;96ba	fe		.
	defb 040h		;96bb	40		@
	defb 020h		;96bc	20		 
	defb 0e8h		;96bd	e8		.
	defb 03eh		;96be	3e		>
	defb 001h		;96bf	01		.
	defb 032h		;96c0	32		2
	defb 005h		;96c1	05		.
	defb 098h		;96c2	98		.
	defb 01eh		;96c3	1e		.
	defb 0ech		;96c4	ec		.
	defb 001h		;96c5	01		.
	defb 053h		;96c6	53		S
	defb 041h		;96c7	41		A
	defb 0edh		;96c8	ed		.
	defb 059h		;96c9	59		Y
	defb 006h		;96ca	06		.
	defb 000h		;96cb	00		.
	defb 010h		;96cc	10		.
	defb 0feh		;96cd	fe		.
	defb 02ah		;96ce	2a		*
	defb 002h		;96cf	02		.
	defb 098h		;96d0	98		.
	defb 011h		;96d1	11		.
	defb 001h		;96d2	01		.
	defb 001h		;96d3	01		.
	defb 001h		;96d4	01		.
	defb 053h		;96d5	53		S
	defb 040h		;96d6	40		@
	defb 0cdh		;96d7	cd		.
	defb 0d2h		;96d8	d2		.
	defb 097h		;96d9	97		.
	defb 0d2h		;96da	d2		.
	defb 0fbh		;96db	fb		.
	defb 096h		;96dc	96		.
GETPARM:
				; = GETPARM (src: AUTOIDE.ASM:125, BIOS-PP 1273243)
	ld hl,(WAITIDE)		;96dd	2a 00 98	* . .
	ld de,00808h		;96e0	11 08 08	. . .
	ld bc,04053h		;96e3	01 53 40	. S @
	call WAITPRT	;96e6	cd d2 97	. . .
	jp c,ABSENT		;96e9	da 1a 97	. . .
	ld bc,00050h		;96ec	01 50 00	. P .
	ld hl,07e00h		;96ef	21 00 7e	! . ~
	inir			;96f2	ed b2		. .
	inir			;96f4	ed b2		. .
	call IDESPEC		;96f6	cd 1c 97	. . .
	and a			;96f9	a7		.
	ret			;96fa	c9		.
NOHDD:
				; = NOHDD (src: AUTOIDE.ASM:138, BIOS-PP 1273243)
	ld a,002h		;96fb	3e 02		> .
	ld (IDEDEV),a		;96fd	32 05 98	2 . .
	ld e,0a1h		;9700	1e a1		. .
	ld bc,04153h		;9702	01 53 41	. S A
	out (c),e		;9705	ed 59		. Y
	ld b,000h		;9707	06 00		. .
l9709h:
	djnz l9709h		;9709	10 fe		. .
	ld hl,(WAITSML)		;970b	2a 02 98	* . .
	ld de,00101h		;970e	11 01 01	. . .
	ld bc,04053h		;9711	01 53 40	. S @
	call WAITPRT	;9714	cd d2 97	. . .
	jp c,GETPARM	;9717	da dd 96	. . .
ABSENT:
				; = ABSENT (src: AUTOIDE.ASM:150, BIOS-PP 1273243)
	scf			;971a	37		7
	ret			;971b	c9		.
IDESPEC:
				; = IDESPEC (src: AUTOIDE.ASM:153, BIOS-PP 1273243)
	in a,(0e2h)		;971c	db e2		. .
	ex af,af'		;971e	08		.
	ld a,0feh		;971f	3e fe		> .
	out (0e2h),a		;9721	d3 e2		. .
	ld a,(IDEDEV)		;9723	3a 05 98	: . .
	ld (iy+007h),a		;9726	fd 77 07	. w .
	cp 002h			;9729	fe 02		. .
	jp z,l978ch		;972b	ca 8c 97	. . .
	ld bc,04052h		;972e	01 52 40	. R @
	in a,(c)		;9731	ed 78		. x
	and 0f0h		;9733	e6 f0		. .
	ld b,a			;9735	47		G
	ld a,(07e06h)		;9736	3a 06 7e	: . ~
	ld (iy+002h),a		;9739	fd 77 02	. w .
	dec a			;973c	3d		=
	and 00fh		;973d	e6 0f		. .
	or b			;973f	b0		.
	ld b,a			;9740	47		G
	ld a,(07e63h)		;9741	3a 63 7e	: c ~
	bit 1,a			;9744	cb 4f		. O
	jr z,NONLBA		;9746	28 02		( .
	set 6,b			;9748	cb f0		. .
NONLBA:
				; = NONLBA (src: AUTOIDE.ASM:175, BIOS-PP 1273243)
	ld a,b			;974a	78		x
	ld bc,04152h		;974b	01 52 41	. R A
	out (c),a		;974e	ed 79		. y
	and 0f0h		;9750	e6 f0		. .
	ld hl,l9806h		;9752	21 06 98	! . .
	or (hl)			;9755	b6		.
	ld (iy+000h),a		;9756	fd 77 00	. w .
	ld hl,(07e02h)		;9759	2a 02 7e	* . ~
	ld (iy+003h),l		;975c	fd 75 03	. u .
	ld (iy+004h),h		;975f	fd 74 04	. t .
	ld a,(07e0ch)		;9762	3a 0c 7e	: . ~
	ld (iy+001h),a		;9765	fd 77 01	. w .
	ld bc,00152h		;9768	01 52 01	. R .
	out (c),a		;976b	ed 79		. y
	ld a,091h		;976d	3e 91		> .
	call IDE_CMD		;976f	cd 9c 97	. . .
	ld c,(iy+001h)		;9772	fd 4e 01	. N .
	ld b,000h		;9775	06 00		. .
	ld a,(iy+002h)		;9777	fd 7e 02	. ~ .
	ld hl,00000h		;977a	21 00 00	! . .
HDDINI3:
				; = HDDINI3 (src: AUTOIDE.ASM:194, BIOS-PP 1273243)
	add hl,bc		;977d	09		.
	dec a			;977e	3d		=
	jr nz,HDDINI3		;977f	20 fc		  .
	ld (iy+005h),l		;9781	fd 75 05	. u .
	ld (iy+006h),h		;9784	fd 74 06	. t .
NOSPEC:
				; = NOSPEC (src: AUTOIDE.ASM:199, BIOS-PP 1273243)
	ex af,af'		;9787	08		.
	out (0e2h),a		;9788	d3 e2		. .
	and a			;978a	a7		.
	ret			;978b	c9		.
l978ch:
	ld bc,04052h		;978c	01 52 40	. R @
	in a,(c)		;978f	ed 78		. x
	and 0f0h		;9791	e6 f0		. .
	ld hl,l9806h		;9793	21 06 98	! . .
	or (hl)			;9796	b6		.
	ld (iy+000h),a		;9797	fd 77 00	. w .
	jr NOSPEC		;979a	18 eb		. .
IDE_CMD:
				; = IDE_CMD (src: AUTOIDE.ASM:204, BIOS-PP 1273243)
	push af			;979c	f5		.
	ld hl,(WAITIDE)		;979d	2a 00 98	* . .
	ld de,0c040h		;97a0	11 40 c0	. @ .
	ld bc,04053h		;97a3	01 53 40	. S @
	call WAITPRT	;97a6	cd d2 97	. . .
	pop de			;97a9	d1		.
	ret c			;97aa	d8		.
	ld bc,04153h		;97ab	01 53 41	. S A
	out (c),d		;97ae	ed 51		. Q
	ld hl,(WAITIDE)		;97b0	2a 00 98	* . .
	ld de,0c040h		;97b3	11 40 c0	. @ .
	ld bc,04053h		;97b6	01 53 40	. S @
	jp WAITPRT		;97b9	c3 d2 97	. . .
WAITHDD:
				; = WAITHDD (src: AUTOIDE.ASM:218, BIOS-PP 1273243)
	ei			;97bc	fb		.
	ld hl,005fdh		;97bd	21 fd 05	! . .
WTREADY:
				; = WTREADY (src: AUTOIDE.ASM:220, BIOS-PP 1273243)
	halt			;97c0	76		v

; BLOCK 'Data97C1' (start 0x97c1 end 0x97d2)
Data97C1:
	defb 001h		;97c1	01		.
	defb 053h		;97c2	53		S
	defb 040h		;97c3	40		@
	defb 0edh		;97c4	ed		.
	defb 078h		;97c5	78		x
	defb 0e6h		;97c6	e6		.
	defb 0c0h		;97c7	c0		.
	defb 0feh		;97c8	fe		.
	defb 040h		;97c9	40		@
	defb 0c8h		;97ca	c8		.
	defb 02bh		;97cb	2b		+
	defb 07ch		;97cc	7c		|
	defb 0b5h		;97cd	b5		.
	defb 020h		;97ce	20		 
	defb 0f0h		;97cf	f0		.
	defb 037h		;97d0	37		7
	defb 0c9h		;97d1	c9		.
WAITPRT:
				; = WAITPRT (src: AUTOIDE.ASM:257, BIOS-PP 1273243)
	in a,(c)		;97d2	ed 78		. x
	and d			;97d4	a2		.
	cp e			;97d5	bb		.
	jr nz,WAITP2		;97d6	20 02		  .
	and a			;97d8	a7		.
	ret			;97d9	c9		.
WAITP2:
				; = WAITP2 (src: AUTOIDE.ASM:266, BIOS-PP 1273243)
	dec hl			;97da	2b		+
	call SKIPKEY		;97db	cd e6 97	. . .
	ret c			;97de	d8		.
	ld a,l			;97df	7d		}
	or h			;97e0	b4		.
	jp nz,WAITPRT	;97e1	c2 d2 97	. . .
WAITP1:
				; = WAITP1 (src: AUTOIDE.ASM:272, BIOS-PP 1273243)
	scf			;97e4	37		7
	ret			;97e5	c9		.
SKIPKEY:
				; = SKIPKEY (src: AUTOIDE.ASM:275, BIOS-PP 1273243)
	exx			;97e6	d9		.
	call SCANKEY		;97e7	cd 57 9e	. W .
	exx			;97ea	d9		.
	scf			;97eb	37		7
	ccf			;97ec	3f		?
	ret z			;97ed	c8		.
	exx			;97ee	d9		.
	ld hl,03e00h		;97ef	21 00 3e	! . >
	and a			;97f2	a7		.
	sbc hl,de		;97f3	ed 52		. R
	exx			;97f5	d9		.
	scf			;97f6	37		7
	ccf			;97f7	3f		?
	ret nz			;97f8	c0		.
	ld a,000h		;97f9	3e 00		> .
	ld (SKIP),a		;97fb	32 04 98	2 . .
	scf			;97fe	37		7
	ret			;97ff	c9		.
WAITIDE:

; BLOCK 'Data9800' (start 0x9800 end 0x9807)
				; = WAITIDE (src: AUTOIDE.ASM:294, BIOS-PP 1273243)
	defb 000h		;9800	00		.
	defb 000h		;9801	00		.
WAITSML:
				; = WAITSML (src: AUTOIDE.ASM:296, BIOS-PP 1273243)
	defb 000h		;9802	00		.
	defb 004h		;9803	04		.
SKIP:
				; = SKIP (src: AUTOIDE.ASM:298, BIOS-PP 1273243)
	defb 0ffh		;9804	ff		.
IDEDEV:
				; = IDEDEV (src: AUTOIDE.ASM:300, BIOS-PP 1273243)
	defb 0ffh		;9805	ff		.
l9806h:
	defb 000h		;9806	00		.
U_SETUP:
				; = U_SETUP (src: SETUP.ASM:2, BIOS-PP 1273243)
	ld a,01ah		;9807	3e 1a		> .
	call READCMS		;9809	cd 27 9b	. ' .
	and 00fh		;980c	e6 0f		. .
	ld l,a			;980e	6f		o
	call CSET		;980f	cd a3 99	. . .
	ld de,00000h		;9812	11 00 00	. . .
	ld hl,02050h		;9815	21 50 20	! P  
	ld a,(NORCLR)		;9818	3a 6c 9a	: l .
	ld b,a			;981b	47		G
	ld c,089h		;981c	0e 89		. .
	call 00018h		;981e	cd 18 00	. . .
	ld a,003h		;9821	3e 03		> .
	ld de,00100h		;9823	11 00 01	. . .
	call POSTLEN		;9826	cd b7 b5	. . .
	ld a,003h		;9829	3e 03		> .
	call POSTMSG		;982b	cd db b5	. . .
	ld a,004h		;982e	3e 04		> .
	ld de,00200h		;9830	11 00 02	. . .
	call POSTLEN		;9833	cd b7 b5	. . .
	ld a,004h		;9836	3e 04		> .
	call POSTMSG		;9838	cd db b5	. . .
	ld a,005h		;983b	3e 05		> .
	ld de,00500h		;983d	11 00 05	. . .
	call POSTLEN		;9840	cd b7 b5	. . .
	ld a,005h		;9843	3e 05		> .
	call POSTMSG		;9845	cd db b5	. . .
	call DOUBLE		;9848	cd 48 89	. H .
	ld de,00402h		;984b	11 02 04	. . .
	ld bc,01a4ch		;984e	01 4c 1a	. L .
	call PBORDER		;9851	cd 95 8a	. . .
	call MEDIUM	;9854	cd 5c 89	. \ .
	ld de,00602h		;9857	11 02 06	. . .
	ld bc,0134ch		;985a	01 4c 13	. L .
	call PBORDER		;985d	cd 95 8a	. . .
	call SINGLE		;9860	cd 4d 89	. M .
	ld de,0062ah		;9863	11 2a 06	. * .
	ld h,013h		;9866	26 13		& .
	call TLINEV		;9868	cd 5b 8a	. [ .
	ld de,01906h		;986b	11 06 19	. . .
	call LOCAT		;986e	cd ae 89	. . .
	ld a,006h		;9871	3e 06		> .
	call POSTMSG		;9873	cd db b5	. . .
	ld de,01a06h		;9876	11 06 1a	. . .
	call LOCAT		;9879	cd ae 89	. . .
	ld a,007h		;987c	3e 07		> .
	call POSTMSG		;987e	cd db b5	. . .
	ld de,01b06h		;9881	11 06 1b	. . .
	call LOCAT		;9884	cd ae 89	. . .
	ld a,008h		;9887	3e 08		> .
	call POSTMSG		;9889	cd db b5	. . .
	ld de,01c06h		;988c	11 06 1c	. . .
	call LOCAT		;988f	cd ae 89	. . .
	ld a,009h		;9892	3e 09		> .
	call POSTMSG		;9894	cd db b5	. . .
	ld bc,01600h		;9897	01 00 16	. . .
STT1:
				; = STT1 (src: SETUP.ASM:67, BIOS-PP 1273243)
	ld a,c			;989a	79		y
	ld (ITEM),a		;989b	32 6b 9a	2 k .
	push bc			;989e	c5		.
	call PTEXT		;989f	cd d4 99	. . .
	pop bc			;98a2	c1		.
	inc c			;98a3	0c		.
	djnz STT1		;98a4	10 f4		. .
	xor a			;98a6	af		.
	ld (ITEM),a		;98a7	32 6b 9a	2 k .
	call PCURSOR		;98aa	cd e6 99	. . .
AGAIN:
				; = AGAIN (src: SETUP.ASM:77, BIOS-PP 1273243)
	ld hl,AGAIN		;98ad	21 ad 98	! . .
	push hl			;98b0	e5		.
	call EMSGR	;98b1	cd b3 b5	. . .
	ld hl,05200h		;98b4	21 00 52	! . R
	and a			;98b7	a7		.
	sbc hl,de		;98b8	ed 52		. R
	jp z,INCITM		;98ba	ca 70 99	. p .
	ld hl,05800h		;98bd	21 00 58	! . X
	and a			;98c0	a7		.
	sbc hl,de		;98c1	ed 52		. R
	jp z,DECITM		;98c3	ca 82 99	. . .
	ld hl,05600h		;98c6	21 00 56	! . V
	and a			;98c9	a7		.
	sbc hl,de		;98ca	ed 52		. R
	jp z,ADDITM		;98cc	ca 5b 99	. [ .
	ld hl,05400h		;98cf	21 00 54	! . T
	and a			;98d2	a7		.
	sbc hl,de		;98d3	ed 52		. R
	jp z,SUBITM		;98d5	ca 4a 99	. J .
	ld hl,05300h		;98d8	21 00 53	! . S
	and a			;98db	a7		.
	sbc hl,de		;98dc	ed 52		. R
	jp z,INCVAL	;98de	ca 70 9a	. p .
	ld hl,05900h		;98e1	21 00 59	! . Y
	and a			;98e4	a7		.
	sbc hl,de		;98e5	ed 52		. R
	jp z,DECVAL		;98e7	ca a9 9a	. . .
	ld hl,la400h		;98ea	21 00 a4	! . .
	and a			;98ed	a7		.
	sbc hl,de		;98ee	ed 52		. R
	jp z,INCVAL	;98f0	ca 70 9a	. p .
	cp 02bh			;98f3	fe 2b		. +
	jp z,INCVAL	;98f5	ca 70 9a	. p .
	cp 02dh			;98f8	fe 2d		. -
	jp z,DECVAL		;98fa	ca a9 9a	. . .
	ld hl,03c00h		;98fd	21 00 3c	! . <
	and a			;9900	a7		.
	sbc hl,de		;9901	ed 52		. R
	jp z,SAVEV		;9903	ca 46 99	. F .
	ld hl,03d00h		;9906	21 00 3d	! . =
	and a			;9909	a7		.
	sbc hl,de		;990a	ed 52		. R
	jp z,CCHANGE		;990c	ca 94 99	. . .
	ld hl,03f00h		;990f	21 00 3f	! . ?
	and a			;9912	a7		.
	sbc hl,de		;9913	ed 52		. R
	jp z,OLD_VAL		;9915	ca 33 99	. 3 .
	ld hl,04100h		;9918	21 00 41	! . A
	and a			;991b	a7		.
	sbc hl,de		;991c	ed 52		. R
	jp z,DEF_VAL		;991e	ca 3a 99	. : .
	ld hl,04400h		;9921	21 00 44	! . D
	and a			;9924	a7		.
	sbc hl,de		;9925	ed 52		. R
	jp z,SAVEXIT		;9927	ca 41 99	. A .
	ld hl,0011bh		;992a	21 1b 01	! . .
	and a			;992d	a7		.
	sbc hl,de		;992e	ed 52		. R
	ret nz			;9930	c0		.
	pop hl			;9931	e1		.
	ret			;9932	c9		.
OLD_VAL:
				; = OLD_VAL (src: SETUP.ASM:139, BIOS-PP 1273243)
	call READING		;9933	cd 5e 9b	. ^ .
	call REFRESH		;9936	cd b9 99	. . .
	ret			;9939	c9		.
DEF_VAL:
				; = DEF_VAL (src: SETUP.ASM:143, BIOS-PP 1273243)
	call SETDEF		;993a	cd 96 9b	. . .
	call REFRESH		;993d	cd b9 99	. . .
	ret			;9940	c9		.
SAVEXIT:
				; = SAVEXIT (src: SETUP.ASM:147, BIOS-PP 1273243)
	call WRITING		;9941	cd 70 9b	. p .
	pop hl			;9944	e1		.
	ret			;9945	c9		.
SAVEV:
				; = SAVEV (src: SETUP.ASM:151, BIOS-PP 1273243)
	call WRITING		;9946	cd 70 9b	. p .
	ret			;9949	c9		.
SUBITM:
				; = SUBITM (src: SETUP.ASM:154, BIOS-PP 1273243)
	call RCURSOR		;994a	cd 1a 9a	. . .
	ld a,(ITEM)		;994d	3a 6b 9a	: k .
	sub 011h		;9950	d6 11		. .
	jr nc,GODITM2		;9952	30 01		0 .
	xor a			;9954	af		.
GODITM2:
				; = GODITM2 (src: SETUP.ASM:159, BIOS-PP 1273243)
	ld (ITEM),a		;9955	32 6b 9a	2 k .
	jp PCURSOR		;9958	c3 e6 99	. . .
ADDITM:
				; = ADDITM (src: SETUP.ASM:162, BIOS-PP 1273243)
	call RCURSOR		;995b	cd 1a 9a	. . .
	ld a,(ITEM)		;995e	3a 6b 9a	: k .
	add a,011h		;9961	c6 11		. .
	cp 016h			;9963	fe 16		. .
	jr c,GODITM		;9965	38 03		8 .
	ld a,016h		;9967	3e 16		> .
	dec a			;9969	3d		=
GODITM:
				; = GODITM (src: SETUP.ASM:169, BIOS-PP 1273243)
	ld (ITEM),a		;996a	32 6b 9a	2 k .
	jp PCURSOR		;996d	c3 e6 99	. . .
INCITM:
				; = INCITM (src: SETUP.ASM:172, BIOS-PP 1273243)
	call RCURSOR		;9970	cd 1a 9a	. . .
	ld a,(ITEM)		;9973	3a 6b 9a	: k .
	inc a			;9976	3c		<
	cp 016h			;9977	fe 16		. .
	jr nz,l997ch		;9979	20 01		  .
	xor a			;997b	af		.
l997ch:
	ld (ITEM),a		;997c	32 6b 9a	2 k .
	jp PCURSOR		;997f	c3 e6 99	. . .
DECITM:
				; = DECITM (src: SETUP.ASM:181, BIOS-PP 1273243)
	call RCURSOR		;9982	cd 1a 9a	. . .
	ld a,(ITEM)		;9985	3a 6b 9a	: k .
	or a			;9988	b7		.
	jr nz,l998dh		;9989	20 02		  .
	ld a,016h		;998b	3e 16		> .
l998dh:
	dec a			;998d	3d		=
	ld (ITEM),a		;998e	32 6b 9a	2 k .
	jp PCURSOR		;9991	c3 e6 99	. . .
CCHANGE:
				; = CCHANGE (src: SETUP.ASM:190, BIOS-PP 1273243)
	ld a,01ah		;9994	3e 1a		> .
	call READCMS		;9996	cd 27 9b	. ' .
	inc a			;9999	3c		<
	and 00fh		;999a	e6 0f		. .
	ld l,a			;999c	6f		o
	ld b,a			;999d	47		G
	ld a,01ah		;999e	3e 1a		> .
	call sub_9b2ch		;99a0	cd 2c 9b	. , .
CSET:
				; = CSET (src: SETUP.ASM:198, BIOS-PP 1273243)
	ld h,000h		;99a3	26 00		& .
	ld de,STYLES		;99a5	11 c0 9b	. . .
	add hl,hl		;99a8	29		)
	add hl,hl		;99a9	29		)
	add hl,de		;99aa	19		.
	ld de,NORCLR		;99ab	11 6c 9a	. l .
	ldi			;99ae	ed a0		. .
	ldi			;99b0	ed a0		. .
	ldi			;99b2	ed a0		. .
	ldi			;99b4	ed a0		. .
	call FSCREEN		;99b6	cd 4e 9a	. N .
REFRESH:
				; = REFRESH (src: SETUP.ASM:209, BIOS-PP 1273243)
	ld bc,01600h		;99b9	01 00 16	. . .
	ld a,(ITEM)		;99bc	3a 6b 9a	: k .
	push af			;99bf	f5		.
STT2:
				; = STT2 (src: SETUP.ASM:212, BIOS-PP 1273243)
	ld a,c			;99c0	79		y
	ld (ITEM),a		;99c1	32 6b 9a	2 k .
	push bc			;99c4	c5		.
	call PTEXT		;99c5	cd d4 99	. . .
	pop bc			;99c8	c1		.
	inc c			;99c9	0c		.
	djnz STT2		;99ca	10 f4		. .
	pop af			;99cc	f1		.
	ld (ITEM),a		;99cd	32 6b 9a	2 k .
	call PCURSOR		;99d0	cd e6 99	. . .
	ret			;99d3	c9		.
PTEXT:
				; = PTEXT (src: SETUP.ASM:224, BIOS-PP 1273243)
	ld a,(ITEM)		;99d4	3a 6b 9a	: k .
	ld l,a			;99d7	6f		o
	ld h,000h		;99d8	26 00		& .
	ld de,0ba00h		;99da	11 00 ba	. . .
	add hl,hl		;99dd	29		)
	add hl,de		;99de	19		.
	ld e,(hl)		;99df	5e		^
	inc hl			;99e0	23		#
	ld d,(hl)		;99e1	56		V
	ex de,hl		;99e2	eb		.
	jp PITEM		;99e3	c3 f0 9a	. . .
PCURSOR:
				; = PCURSOR (src: SETUP.ASM:236, BIOS-PP 1273243)
	ld a,(ITEM)		;99e6	3a 6b 9a	: k .
	ld l,a			;99e9	6f		o
	ld h,000h		;99ea	26 00		& .
	ld de,0ba00h		;99ec	11 00 ba	. . .
	add hl,hl		;99ef	29		)
	add hl,de		;99f0	19		.
	ld a,(hl)		;99f1	7e		~
	inc hl			;99f2	23		#
	ld h,(hl)		;99f3	66		f
	ld l,a			;99f4	6f		o
	ld e,(hl)		;99f5	5e		^
	inc hl			;99f6	23		#
	ld d,(hl)		;99f7	56		V
	inc hl			;99f8	23		#
	ld bc,00100h		;99f9	01 00 01	. . .
	xor a			;99fc	af		.
	cpir			;99fd	ed b1		. .
	ld a,0ffh		;99ff	3e ff		> .
	sub c			;9a01	91		.
	add a,e			;9a02	83		.
	ld e,a			;9a03	5f		_
	call LOCAT		;9a04	cd ae 89	. . .
	inc hl			;9a07	23		#
	inc hl			;9a08	23		#
	inc hl			;9a09	23		#
	ld bc,00100h		;9a0a	01 00 01	. . .
	xor a			;9a0d	af		.
	cpir			;9a0e	ed b1		. .
	ld a,0ffh		;9a10	3e ff		> .
	sub c			;9a12	91		.
	ld b,a			;9a13	47		G
	ld a,(CURCLR)		;9a14	3a 6e 9a	: n .
	jp PRINTA		;9a17	c3 f2 89	. . .
RCURSOR:
				; = RCURSOR (src: SETUP.ASM:271, BIOS-PP 1273243)
	ld a,(ITEM)		;9a1a	3a 6b 9a	: k .
	ld l,a			;9a1d	6f		o
	ld h,000h		;9a1e	26 00		& .
	ld de,0ba00h		;9a20	11 00 ba	. . .
	add hl,hl		;9a23	29		)
	add hl,de		;9a24	19		.
	ld a,(hl)		;9a25	7e		~
	inc hl			;9a26	23		#
	ld h,(hl)		;9a27	66		f
	ld l,a			;9a28	6f		o
	ld e,(hl)		;9a29	5e		^
	inc hl			;9a2a	23		#
	ld d,(hl)		;9a2b	56		V
	inc hl			;9a2c	23		#
	ld bc,00100h		;9a2d	01 00 01	. . .
	xor a			;9a30	af		.
	cpir			;9a31	ed b1		. .
	ld a,0ffh		;9a33	3e ff		> .
	sub c			;9a35	91		.
	add a,e			;9a36	83		.
	ld e,a			;9a37	5f		_
	call LOCAT		;9a38	cd ae 89	. . .
	inc hl			;9a3b	23		#
	inc hl			;9a3c	23		#
	inc hl			;9a3d	23		#
	ld bc,00100h		;9a3e	01 00 01	. . .
	xor a			;9a41	af		.
	cpir			;9a42	ed b1		. .
	ld a,0ffh		;9a44	3e ff		> .
	sub c			;9a46	91		.
	ld b,a			;9a47	47		G
	ld a,(NORCLR)		;9a48	3a 6c 9a	: l .
	jp PRINTA		;9a4b	c3 f2 89	. . .
FSCREEN:
				; = FSCREEN (src: SETUP.ASM:306, BIOS-PP 1273243)
	ld de,00000h		;9a4e	11 00 00	. . .
	ei			;9a51	fb		.
	halt			;9a52	76		v
FSC1:
				; = FSC1 (src: SETUP.ASM:309, BIOS-PP 1273243)
	push de			;9a53	d5		.
	ld c,084h		;9a54	0e 84		. .
	call 00018h		;9a56	cd 18 00	. . .
	ld a,(NORCLR)		;9a59	3a 6c 9a	: l .
	ld e,a			;9a5c	5f		_
	ld bc,05083h		;9a5d	01 83 50	. . P
	call 00018h		;9a60	cd 18 00	. . .
	pop de			;9a63	d1		.
	ld a,020h		;9a64	3e 20		>  
	inc d			;9a66	14		.
	cp d			;9a67	ba		.
	jr nz,FSC1		;9a68	20 e9		  .
	ret			;9a6a	c9		.
ITEM:

; BLOCK 'Data9A6B' (start 0x9a6b end 0x9a70)
				; = ITEM (src: SETUP.ASM:323, BIOS-PP 1273243)
	defb 000h		;9a6b	00		.
NORCLR:
				; = NORCLR (src: SETUP.ASM:325, BIOS-PP 1273243)
	defb 01fh		;9a6c	1f		.
HLTCLR:
				; = HLTCLR (src: SETUP.ASM:326, BIOS-PP 1273243)
	defb 01eh		;9a6d	1e		.
CURCLR:
				; = CURCLR (src: SETUP.ASM:327, BIOS-PP 1273243)
	defb 04fh		;9a6e	4f		O
WRMCLR:
				; = WRMCLR (src: SETUP.ASM:328, BIOS-PP 1273243)
	defb 01fh		;9a6f	1f		.
INCVAL:
				; = INCVAL (src: SETUP.ASM:330, BIOS-PP 1273243)
	ld a,(ITEM)		;9a70	3a 6b 9a	: k .
	ld l,a			;9a73	6f		o
	ld h,000h		;9a74	26 00		& .
	ld de,0ba00h		;9a76	11 00 ba	. . .
	add hl,hl		;9a79	29		)
	add hl,de		;9a7a	19		.
	ld a,(hl)		;9a7b	7e		~
	inc hl			;9a7c	23		#
	ld h,(hl)		;9a7d	66		f
	ld l,a			;9a7e	6f		o
	inc hl			;9a7f	23		#
	inc hl			;9a80	23		#
	xor a			;9a81	af		.
	ld bc,000ffh		;9a82	01 ff 00	. . .
	cpir			;9a85	ed b1		. .
	ld a,(hl)		;9a87	7e		~
	inc hl			;9a88	23		#
	push af			;9a89	f5		.
	call READCMS		;9a8a	cd 27 9b	. ' .
	ld c,a			;9a8d	4f		O
	and (hl)		;9a8e	a6		.
	inc hl			;9a8f	23		#
	cp (hl)			;9a90	be		.
	ld a,c			;9a91	79		y
	jr z,OVERI		;9a92	28 08		( .
	dec hl			;9a94	2b		+
	ld b,(hl)		;9a95	46		F
	call ADDVAL		;9a96	cd e1 9a	. . .
	add a,b			;9a99	80		.
	jr l9a9dh		;9a9a	18 01		. .
OVERI:
				; = OVERI (src: SETUP.ASM:360, BIOS-PP 1273243)
	xor (hl)		;9a9c	ae		.
l9a9dh:
	ld b,a			;9a9d	47		G
	pop af			;9a9e	f1		.
	call sub_9b2ch		;9a9f	cd 2c 9b	. , .
	call PTEXT		;9aa2	cd d4 99	. . .
	call ApplyScreenPosition	;9aa5	cd 72 83	. r .
	ret			;9aa8	c9		.
DECVAL:
				; = DECVAL (src: SETUP.ASM:367, BIOS-PP 1273243)
	ld a,(ITEM)		;9aa9	3a 6b 9a	: k .
	ld l,a			;9aac	6f		o
	ld h,000h		;9aad	26 00		& .
	ld de,0ba00h		;9aaf	11 00 ba	. . .
	add hl,hl		;9ab2	29		)
	add hl,de		;9ab3	19		.
	ld a,(hl)		;9ab4	7e		~
	inc hl			;9ab5	23		#
	ld h,(hl)		;9ab6	66		f
	ld l,a			;9ab7	6f		o
	inc hl			;9ab8	23		#
	inc hl			;9ab9	23		#
	xor a			;9aba	af		.
	ld bc,000ffh		;9abb	01 ff 00	. . .
	cpir			;9abe	ed b1		. .
	ld a,(hl)		;9ac0	7e		~
	inc hl			;9ac1	23		#
	push af			;9ac2	f5		.
	call READCMS		;9ac3	cd 27 9b	. ' .
	ld c,a			;9ac6	4f		O
	and (hl)		;9ac7	a6		.
	ld a,c			;9ac8	79		y
	inc hl			;9ac9	23		#
	jr z,OVERD		;9aca	28 08		( .
	dec hl			;9acc	2b		+
	ld b,(hl)		;9acd	46		F
	call ADDVAL		;9ace	cd e1 9a	. . .
	sub b			;9ad1	90		.
	jr l9ad5h		;9ad2	18 01		. .
OVERD:
				; = OVERD (src: SETUP.ASM:396, BIOS-PP 1273243)
	or (hl)			;9ad4	b6		.
l9ad5h:
	ld b,a			;9ad5	47		G
	pop af			;9ad6	f1		.
	call sub_9b2ch		;9ad7	cd 2c 9b	. , .
	call PTEXT		;9ada	cd d4 99	. . .
	call ApplyScreenPosition	;9add	cd 72 83	. r .
	ret			;9ae0	c9		.
ADDVAL:
				; = ADDVAL (src: SETUP.ASM:405, BIOS-PP 1273243)
	ld c,000h		;9ae1	0e 00		. .
ADDV1:
				; = ADDV1 (src: SETUP.ASM:406, BIOS-PP 1273243)
	inc c			;9ae3	0c		.
	rrc b			;9ae4	cb 08		. .
	jr nc,ADDV1		;9ae6	30 fb		0 .
	ld b,080h		;9ae8	06 80		. .
ADDV2:
				; = ADDV2 (src: SETUP.ASM:410, BIOS-PP 1273243)
	rlc b			;9aea	cb 00		. .
	dec c			;9aec	0d		.
	jr nz,ADDV2		;9aed	20 fb		  .
	ret			;9aef	c9		.
PITEM:
				; = PITEM (src: SETUP.ASM:415, BIOS-PP 1273243)
	ld e,(hl)		;9af0	5e		^
	inc hl			;9af1	23		#
	ld d,(hl)		;9af2	56		V
	inc hl			;9af3	23		#
	call LOCAT		;9af4	cd ae 89	. . .
	call sub_8a08h		;9af7	cd 08 8a	. . .
	ld a,(hl)		;9afa	7e		~
	call READCMS		;9afb	cd 27 9b	. ' .
	inc hl			;9afe	23		#
	ld b,(hl)		;9aff	46		F
	inc hl			;9b00	23		#
	inc hl			;9b01	23		#
	and b			;9b02	a0		.
RRLP:
				; = RRLP (src: SETUP.ASM:428, BIOS-PP 1273243)
	rrca			;9b03	0f		.
	rrc b			;9b04	cb 08		. .
	jr nc,RRLP		;9b06	30 fb		0 .
	rlca			;9b08	07		.
	or a			;9b09	b7		.
	jp z,sub_8a08h		;9b0a	ca 08 8a	. . .
	ld b,a			;9b0d	47		G
	xor a			;9b0e	af		.
NIT:
				; = NIT (src: SETUP.ASM:436, BIOS-PP 1273243)
	ld c,0ffh		;9b0f	0e ff		. .
	cpir			;9b11	ed b1		. .
	ret nz			;9b13	c0		.
	djnz NIT		;9b14	10 f9		. .
	jp sub_8a08h		;9b16	c3 08 8a	. . .
G_VALUE:
				; = G_VALUE (src: SETUP.ASM:442, BIOS-PP 1273243)
	push bc			;9b19	c5		.
	ld a,c			;9b1a	79		y
	call READCMS		;9b1b	cd 27 9b	. ' .
	pop bc			;9b1e	c1		.
	and b			;9b1f	a0		.
RRLPX:
				; = RRLPX (src: SETUP.ASM:447, BIOS-PP 1273243)
	rrca			;9b20	0f		.
	rrc b			;9b21	cb 08		. .
	jr nc,RRLPX		;9b23	30 fb		0 .
	rlca			;9b25	07		.
	ret			;9b26	c9		.
READCMS:
				; = READCMS (src: SETUP.ASM:453, BIOS-PP 1273243)
	ld d,09dh		;9b27	16 9d		. .
	ld e,a			;9b29	5f		_
	ld a,(de)		;9b2a	1a		.
	ret			;9b2b	c9		.
sub_9b2ch:
	ld d,09dh		;9b2c	16 9d		. .
	ld e,a			;9b2e	5f		_
	ld a,b			;9b2f	78		x
	ld (de),a		;9b30	12		.
	ret			;9b31	c9		.
CHEKSUM:
				; = CHEKSUM (src: SETUP.ASM:464, BIOS-PP 1273243)
	ld bc,0120eh		;9b32	01 0e 12	. . .
	ld h,0deh		;9b35	26 de		& .
CHSUM1:
				; = CHSUM1 (src: SETUP.ASM:466, BIOS-PP 1273243)
	ld a,c			;9b37	79		y
	call READCMS		;9b38	cd 27 9b	. ' .
	ld l,a			;9b3b	6f		o
	ld a,h			;9b3c	7c		|
	sub l			;9b3d	95		.
	rlca			;9b3e	07		.
	sub l			;9b3f	95		.
	ld h,a			;9b40	67		g
	inc c			;9b41	0c		.
	djnz CHSUM1		;9b42	10 f3		. .
	ret			;9b44	c9		.
TCHEKSM:
				; = TCHEKSM (src: SETUP.ASM:478, BIOS-PP 1273243)
	call CHEKSUM		;9b45	cd 32 9b	. 2 .
	ld a,03fh		;9b48	3e 3f		> ?
	call READCMS		;9b4a	cd 27 9b	. ' .
	cp h			;9b4d	bc		.
	ret			;9b4e	c9		.
TSTCMOS:
				; = TSTCMOS (src: SETUP.ASM:484, BIOS-PP 1273243)
	ld c,0f5h		;9b4f	0e f5		. .
	jp 00018h		;9b51	c3 18 00	. . .
RDCMOS:
				; = RDCMOS (src: SETUP.ASM:516, BIOS-PP 1273243)
	ld c,0f6h		;9b54	0e f6		. .
	jp 00018h		;9b56	c3 18 00	. . .
WRCMOS:
				; = WRCMOS (src: SETUP.ASM:519, BIOS-PP 1273243)
	ld c,0f7h		;9b59	0e f7		. .
	jp 00018h		;9b5b	c3 18 00	. . .
READING:
				; = READING (src: SETUP.ASM:522, BIOS-PP 1273243)
	ld d,00eh		;9b5e	16 0e		. .
READI:
				; = READI (src: SETUP.ASM:523, BIOS-PP 1273243)
	push de			;9b60	d5		.
	call RDCMOS		;9b61	cd 54 9b	. T .
	pop de			;9b64	d1		.
	ld h,09dh		;9b65	26 9d		& .
	ld l,d			;9b67	6a		j
	ld (hl),a		;9b68	77		w
	ld a,040h		;9b69	3e 40		> @
	inc d			;9b6b	14		.
	cp d			;9b6c	ba		.
	jr nz,READI		;9b6d	20 f1		  .
	ret			;9b6f	c9		.
WRITING:
				; = WRITING (src: SETUP.ASM:535, BIOS-PP 1273243)
	call CHEKSUM		;9b70	cd 32 9b	. 2 .
	ld b,h			;9b73	44		D
	ld a,03fh		;9b74	3e 3f		> ?
	call sub_9b2ch		;9b76	cd 2c 9b	. , .
	ld d,00eh		;9b79	16 0e		. .
WRITI:
				; = WRITI (src: SETUP.ASM:540, BIOS-PP 1273243)
	ld h,09dh		;9b7b	26 9d		& .
	ld l,d			;9b7d	6a		j
	ld a,(hl)		;9b7e	7e		~
	push de			;9b7f	d5		.
	call WRCMOS		;9b80	cd 59 9b	. Y .
	pop de			;9b83	d1		.
	ld a,040h		;9b84	3e 40		> @
	inc d			;9b86	14		.
	cp d			;9b87	ba		.
	jr nz,WRITI		;9b88	20 f1		  .
	ret			;9b8a	c9		.
SETDEFX:
				; = SETDEFX (src: SETUP.ASM:552, BIOS-PP 1273243)
	xor a			;9b8b	af		.
	dec a			;9b8c	3d		=
	ld (ERRSUM+1),a		;9b8d	32 71 82	2 q .
	call SETDEF		;9b90	cd 96 9b	. . .
	jp WRITING		;9b93	c3 70 9b	. p .
SETDEF:
				; = SETDEF (src: SETUP.ASM:558, BIOS-PP 1273243)
	ld hl,DEFVAL		;9b96	21 00 9c	! . .
	ld c,012h		;9b99	0e 12		. .
	ld a,00eh		;9b9b	3e 0e		> .
SETDF1:
				; = SETDF1 (src: SETUP.ASM:561, BIOS-PP 1273243)
	ld b,(hl)		;9b9d	46		F
	inc hl			;9b9e	23		#
	push af			;9b9f	f5		.
	call sub_9b2ch		;9ba0	cd 2c 9b	. , .
	pop af			;9ba3	f1		.
	inc a			;9ba4	3c		<
	dec c			;9ba5	0d		.
	jr nz,SETDF1		;9ba6	20 f5		  .
	ld a,035h		;9ba8	3e 35		> 5
	ld b,000h		;9baa	06 00		. .
	call sub_9b2ch		;9bac	cd 2c 9b	. , .
	ld a,036h		;9baf	3e 36		> 6
	ld b,000h		;9bb1	06 00		. .
	call sub_9b2ch		;9bb3	cd 2c 9b	. , .
	call CHEKSUM		;9bb6	cd 32 9b	. 2 .
	ld b,h			;9bb9	44		D
	ld a,03fh		;9bba	3e 3f		> ?
	call sub_9b2ch		;9bbc	cd 2c 9b	. , .
	ret			;9bbf	c9		.
STYLES:

; BLOCK 'Data9BC0' (start 0x9bc0 end 0x9e48)
				; = STYLES (src: SETUP.ASM:643, BIOS-PP 1273243)
	defb 01fh		;9bc0	1f		.
	defb 01eh		;9bc1	1e		.
	defb 04fh		;9bc2	4f		O
	defb 01fh		;9bc3	1f		.
	defb 01ah		;9bc4	1a		.
	defb 01dh		;9bc5	1d		.
	defb 05fh		;9bc6	5f		_
	defb 01fh		;9bc7	1f		.
	defb 02bh		;9bc8	2b		+
	defb 02eh		;9bc9	2e		.
	defb 030h		;9bca	30		0
	defb 020h		;9bcb	20		 
	defb 030h		;9bcc	30		0
	defb 03eh		;9bcd	3e		>
	defb 020h		;9bce	20		 
	defb 03ah		;9bcf	3a		:
	defb 03fh		;9bd0	3f		?
	defb 030h		;9bd1	30		0
	defb 071h		;9bd2	71		q
	defb 03eh		;9bd3	3e		>
	defb 030h		;9bd4	30		0
	defb 036h		;9bd5	36		6
	defb 067h		;9bd6	67		g
	defb 03fh		;9bd7	3f		?
	defb 04eh		;9bd8	4e		N
	defb 04fh		;9bd9	4f		O
	defb 002h		;9bda	02		.
	defb 042h		;9bdb	42		B
	defb 047h		;9bdc	47		G
	defb 04fh		;9bdd	4f		O
	defb 01fh		;9bde	1f		.
	defb 04fh		;9bdf	4f		O
	defb 05bh		;9be0	5b		[
	defb 05ah		;9be1	5a		Z
	defb 070h		;9be2	70		p
	defb 05fh		;9be3	5f		_
	defb 05fh		;9be4	5f		_
	defb 05eh		;9be5	5e		^
	defb 021h		;9be6	21		!
	defb 05ch		;9be7	5c		\
	defb 070h		;9be8	70		p
	defb 074h		;9be9	74		t
	defb 03fh		;9bea	3f		?
	defb 07fh		;9beb	7f		.
	defb 071h		;9bec	71		q
	defb 07eh		;9bed	7e		~
	defb 05fh		;9bee	5f		_
	defb 07fh		;9bef	7f		.
	defb 07ah		;9bf0	7a		z
	defb 07bh		;9bf1	7b		{
	defb 02fh		;9bf2	2f		/
	defb 07fh		;9bf3	7f		.
	defb 00ah		;9bf4	0a		.
	defb 00bh		;9bf5	0b		.
	defb 03eh		;9bf6	3e		>
	defb 00eh		;9bf7	0e		.
	defb 007h		;9bf8	07		.
	defb 00fh		;9bf9	0f		.
	defb 070h		;9bfa	70		p
	defb 007h		;9bfb	07		.
	defb 00dh		;9bfc	0d		.
	defb 00ah		;9bfd	0a		.
	defb 074h		;9bfe	74		t
	defb 004h		;9bff	04		.
DEFVAL:
				; = DEFVAL (src: SETUP.ASM:725, BIOS-PP 1273243)
	defb 068h		;9c00	68		h
	defb 007h		;9c01	07		.
	defb 012h		;9c02	12		.
	defb 000h		;9c03	00		.
	defb 000h		;9c04	00		.
	defb 000h		;9c05	00		.
	defb 000h		;9c06	00		.
	defb 000h		;9c07	00		.
	defb 000h		;9c08	00		.
	defb 000h		;9c09	00		.
	defb 000h		;9c0a	00		.
	defb 000h		;9c0b	00		.
	defb 000h		;9c0c	00		.
	defb 007h		;9c0d	07		.
	defb 000h		;9c0e	00		.
	defb 002h		;9c0f	02		.
	defb 000h		;9c10	00		.
	defb 077h		;9c11	77		w
	defb 000h		;9c12	00		.
	defb 000h		;9c13	00		.
	defb 000h		;9c14	00		.
	defb 000h		;9c15	00		.
	defb 000h		;9c16	00		.
	defb 000h		;9c17	00		.
	defb 000h		;9c18	00		.
	defb 000h		;9c19	00		.
	defb 000h		;9c1a	00		.
	defb 000h		;9c1b	00		.
	defb 000h		;9c1c	00		.
	defb 000h		;9c1d	00		.
	defb 000h		;9c1e	00		.
	defb 000h		;9c1f	00		.
	defb 000h		;9c20	00		.
	defb 000h		;9c21	00		.
	defb 000h		;9c22	00		.
	defb 000h		;9c23	00		.
	defb 000h		;9c24	00		.
	defb 000h		;9c25	00		.
	defb 000h		;9c26	00		.
	defb 000h		;9c27	00		.
	defb 000h		;9c28	00		.
	defb 000h		;9c29	00		.
	defb 000h		;9c2a	00		.
	defb 000h		;9c2b	00		.
	defb 000h		;9c2c	00		.
	defb 000h		;9c2d	00		.
	defb 000h		;9c2e	00		.
	defb 000h		;9c2f	00		.
	defb 000h		;9c30	00		.
	defb 000h		;9c31	00		.
	defb 000h		;9c32	00		.
	defb 000h		;9c33	00		.
	defb 000h		;9c34	00		.
	defb 000h		;9c35	00		.
	defb 000h		;9c36	00		.
	defb 000h		;9c37	00		.
	defb 000h		;9c38	00		.
	defb 000h		;9c39	00		.
	defb 000h		;9c3a	00		.
	defb 000h		;9c3b	00		.
	defb 000h		;9c3c	00		.
	defb 000h		;9c3d	00		.
	defb 000h		;9c3e	00		.
	defb 000h		;9c3f	00		.
	defb 000h		;9c40	00		.
	defb 000h		;9c41	00		.
	defb 000h		;9c42	00		.
	defb 000h		;9c43	00		.
	defb 000h		;9c44	00		.
	defb 000h		;9c45	00		.
	defb 000h		;9c46	00		.
	defb 000h		;9c47	00		.
	defb 000h		;9c48	00		.
	defb 000h		;9c49	00		.
	defb 000h		;9c4a	00		.
	defb 000h		;9c4b	00		.
	defb 000h		;9c4c	00		.
	defb 000h		;9c4d	00		.
	defb 000h		;9c4e	00		.
	defb 000h		;9c4f	00		.
	defb 000h		;9c50	00		.
	defb 000h		;9c51	00		.
	defb 000h		;9c52	00		.
	defb 000h		;9c53	00		.
	defb 000h		;9c54	00		.
	defb 000h		;9c55	00		.
	defb 000h		;9c56	00		.
	defb 000h		;9c57	00		.
	defb 000h		;9c58	00		.
	defb 000h		;9c59	00		.
	defb 000h		;9c5a	00		.
	defb 000h		;9c5b	00		.
	defb 000h		;9c5c	00		.
	defb 000h		;9c5d	00		.
	defb 000h		;9c5e	00		.
	defb 000h		;9c5f	00		.
	defb 000h		;9c60	00		.
	defb 000h		;9c61	00		.
	defb 000h		;9c62	00		.
	defb 000h		;9c63	00		.
	defb 000h		;9c64	00		.
	defb 000h		;9c65	00		.
	defb 000h		;9c66	00		.
	defb 000h		;9c67	00		.
	defb 000h		;9c68	00		.
	defb 000h		;9c69	00		.
	defb 000h		;9c6a	00		.
	defb 000h		;9c6b	00		.
	defb 000h		;9c6c	00		.
	defb 000h		;9c6d	00		.
	defb 000h		;9c6e	00		.
	defb 000h		;9c6f	00		.
	defb 000h		;9c70	00		.
	defb 000h		;9c71	00		.
	defb 000h		;9c72	00		.
	defb 000h		;9c73	00		.
	defb 000h		;9c74	00		.
	defb 000h		;9c75	00		.
	defb 000h		;9c76	00		.
	defb 000h		;9c77	00		.
	defb 000h		;9c78	00		.
	defb 000h		;9c79	00		.
	defb 000h		;9c7a	00		.
	defb 000h		;9c7b	00		.
	defb 000h		;9c7c	00		.
	defb 000h		;9c7d	00		.
	defb 000h		;9c7e	00		.
	defb 000h		;9c7f	00		.
	defb 000h		;9c80	00		.
	defb 000h		;9c81	00		.
	defb 000h		;9c82	00		.
	defb 000h		;9c83	00		.
	defb 000h		;9c84	00		.
	defb 000h		;9c85	00		.
	defb 000h		;9c86	00		.
	defb 000h		;9c87	00		.
	defb 000h		;9c88	00		.
	defb 000h		;9c89	00		.
	defb 000h		;9c8a	00		.
	defb 000h		;9c8b	00		.
	defb 000h		;9c8c	00		.
	defb 000h		;9c8d	00		.
	defb 000h		;9c8e	00		.
	defb 000h		;9c8f	00		.
	defb 000h		;9c90	00		.
	defb 000h		;9c91	00		.
	defb 000h		;9c92	00		.
	defb 000h		;9c93	00		.
	defb 000h		;9c94	00		.
	defb 000h		;9c95	00		.
	defb 000h		;9c96	00		.
	defb 000h		;9c97	00		.
	defb 000h		;9c98	00		.
	defb 000h		;9c99	00		.
	defb 000h		;9c9a	00		.
	defb 000h		;9c9b	00		.
	defb 000h		;9c9c	00		.
	defb 000h		;9c9d	00		.
	defb 000h		;9c9e	00		.
	defb 000h		;9c9f	00		.
	defb 000h		;9ca0	00		.
	defb 000h		;9ca1	00		.
	defb 000h		;9ca2	00		.
	defb 000h		;9ca3	00		.
	defb 000h		;9ca4	00		.
	defb 000h		;9ca5	00		.
	defb 000h		;9ca6	00		.
	defb 000h		;9ca7	00		.
	defb 000h		;9ca8	00		.
	defb 000h		;9ca9	00		.
	defb 000h		;9caa	00		.
	defb 000h		;9cab	00		.
	defb 000h		;9cac	00		.
	defb 000h		;9cad	00		.
	defb 000h		;9cae	00		.
	defb 000h		;9caf	00		.
	defb 000h		;9cb0	00		.
	defb 000h		;9cb1	00		.
	defb 000h		;9cb2	00		.
	defb 000h		;9cb3	00		.
	defb 000h		;9cb4	00		.
	defb 000h		;9cb5	00		.
	defb 000h		;9cb6	00		.
	defb 000h		;9cb7	00		.
	defb 000h		;9cb8	00		.
	defb 000h		;9cb9	00		.
	defb 000h		;9cba	00		.
	defb 000h		;9cbb	00		.
	defb 000h		;9cbc	00		.
	defb 000h		;9cbd	00		.
	defb 000h		;9cbe	00		.
	defb 000h		;9cbf	00		.
	defb 000h		;9cc0	00		.
	defb 000h		;9cc1	00		.
	defb 000h		;9cc2	00		.
	defb 000h		;9cc3	00		.
	defb 000h		;9cc4	00		.
	defb 000h		;9cc5	00		.
	defb 000h		;9cc6	00		.
	defb 000h		;9cc7	00		.
	defb 000h		;9cc8	00		.
	defb 000h		;9cc9	00		.
	defb 000h		;9cca	00		.
	defb 000h		;9ccb	00		.
	defb 000h		;9ccc	00		.
	defb 000h		;9ccd	00		.
	defb 000h		;9cce	00		.
	defb 000h		;9ccf	00		.
	defb 000h		;9cd0	00		.
	defb 000h		;9cd1	00		.
	defb 000h		;9cd2	00		.
	defb 000h		;9cd3	00		.
	defb 000h		;9cd4	00		.
	defb 000h		;9cd5	00		.
	defb 000h		;9cd6	00		.
	defb 000h		;9cd7	00		.
	defb 000h		;9cd8	00		.
	defb 000h		;9cd9	00		.
	defb 000h		;9cda	00		.
	defb 000h		;9cdb	00		.
	defb 000h		;9cdc	00		.
	defb 000h		;9cdd	00		.
	defb 000h		;9cde	00		.
	defb 000h		;9cdf	00		.
	defb 000h		;9ce0	00		.
	defb 000h		;9ce1	00		.
	defb 000h		;9ce2	00		.
	defb 000h		;9ce3	00		.
	defb 000h		;9ce4	00		.
	defb 000h		;9ce5	00		.
	defb 000h		;9ce6	00		.
	defb 000h		;9ce7	00		.
	defb 000h		;9ce8	00		.
	defb 000h		;9ce9	00		.
	defb 000h		;9cea	00		.
	defb 000h		;9ceb	00		.
	defb 000h		;9cec	00		.
	defb 000h		;9ced	00		.
	defb 000h		;9cee	00		.
	defb 000h		;9cef	00		.
	defb 000h		;9cf0	00		.
	defb 000h		;9cf1	00		.
	defb 000h		;9cf2	00		.
	defb 000h		;9cf3	00		.
	defb 000h		;9cf4	00		.
	defb 000h		;9cf5	00		.
	defb 000h		;9cf6	00		.
	defb 000h		;9cf7	00		.
	defb 000h		;9cf8	00		.
	defb 000h		;9cf9	00		.
	defb 000h		;9cfa	00		.
	defb 000h		;9cfb	00		.
	defb 000h		;9cfc	00		.
	defb 000h		;9cfd	00		.
	defb 000h		;9cfe	00		.
	defb 000h		;9cff	00		.
CMOSARE:
				; = CMOSARE (src: SETUP.ASM:811, BIOS-PP 1273243)
	defb 000h		;9d00	00		.
	defb 000h		;9d01	00		.
	defb 000h		;9d02	00		.
	defb 000h		;9d03	00		.
	defb 000h		;9d04	00		.
	defb 000h		;9d05	00		.
	defb 000h		;9d06	00		.
	defb 000h		;9d07	00		.
	defb 000h		;9d08	00		.
	defb 000h		;9d09	00		.
	defb 000h		;9d0a	00		.
	defb 000h		;9d0b	00		.
	defb 000h		;9d0c	00		.
	defb 000h		;9d0d	00		.
	defb 060h		;9d0e	60		`
	defb 007h		;9d0f	07		.
	defb 012h		;9d10	12		.
	defb 000h		;9d11	00		.
	defb 000h		;9d12	00		.
	defb 000h		;9d13	00		.
	defb 000h		;9d14	00		.
	defb 000h		;9d15	00		.
	defb 000h		;9d16	00		.
	defb 000h		;9d17	00		.
	defb 000h		;9d18	00		.
	defb 000h		;9d19	00		.
	defb 000h		;9d1a	00		.
	defb 007h		;9d1b	07		.
	defb 000h		;9d1c	00		.
	defb 002h		;9d1d	02		.
	defb 000h		;9d1e	00		.
	defb 077h		;9d1f	77		w
	defb 000h		;9d20	00		.
	defb 000h		;9d21	00		.
	defb 000h		;9d22	00		.
	defb 000h		;9d23	00		.
	defb 000h		;9d24	00		.
	defb 000h		;9d25	00		.
	defb 000h		;9d26	00		.
	defb 000h		;9d27	00		.
	defb 000h		;9d28	00		.
	defb 000h		;9d29	00		.
	defb 000h		;9d2a	00		.
	defb 000h		;9d2b	00		.
	defb 000h		;9d2c	00		.
	defb 000h		;9d2d	00		.
	defb 000h		;9d2e	00		.
	defb 000h		;9d2f	00		.
	defb 000h		;9d30	00		.
	defb 000h		;9d31	00		.
	defb 019h		;9d32	19		.
	defb 000h		;9d33	00		.
	defb 000h		;9d34	00		.
	defb 000h		;9d35	00		.
	defb 000h		;9d36	00		.
	defb 000h		;9d37	00		.
	defb 000h		;9d38	00		.
	defb 000h		;9d39	00		.
	defb 000h		;9d3a	00		.
	defb 000h		;9d3b	00		.
	defb 000h		;9d3c	00		.
	defb 000h		;9d3d	00		.
	defb 000h		;9d3e	00		.
	defb 000h		;9d3f	00		.
	defb 000h		;9d40	00		.
	defb 000h		;9d41	00		.
	defb 000h		;9d42	00		.
	defb 000h		;9d43	00		.
	defb 000h		;9d44	00		.
	defb 000h		;9d45	00		.
	defb 000h		;9d46	00		.
	defb 000h		;9d47	00		.
	defb 000h		;9d48	00		.
	defb 000h		;9d49	00		.
	defb 000h		;9d4a	00		.
	defb 000h		;9d4b	00		.
	defb 000h		;9d4c	00		.
	defb 000h		;9d4d	00		.
	defb 000h		;9d4e	00		.
	defb 000h		;9d4f	00		.
	defb 000h		;9d50	00		.
	defb 000h		;9d51	00		.
	defb 000h		;9d52	00		.
	defb 000h		;9d53	00		.
	defb 000h		;9d54	00		.
	defb 000h		;9d55	00		.
	defb 000h		;9d56	00		.
	defb 000h		;9d57	00		.
	defb 000h		;9d58	00		.
	defb 000h		;9d59	00		.
	defb 000h		;9d5a	00		.
	defb 000h		;9d5b	00		.
	defb 000h		;9d5c	00		.
	defb 000h		;9d5d	00		.
	defb 000h		;9d5e	00		.
	defb 000h		;9d5f	00		.
	defb 000h		;9d60	00		.
	defb 000h		;9d61	00		.
	defb 000h		;9d62	00		.
	defb 000h		;9d63	00		.
	defb 000h		;9d64	00		.
	defb 000h		;9d65	00		.
	defb 000h		;9d66	00		.
	defb 000h		;9d67	00		.
	defb 000h		;9d68	00		.
	defb 000h		;9d69	00		.
	defb 000h		;9d6a	00		.
	defb 000h		;9d6b	00		.
	defb 000h		;9d6c	00		.
	defb 000h		;9d6d	00		.
	defb 000h		;9d6e	00		.
	defb 000h		;9d6f	00		.
	defb 000h		;9d70	00		.
	defb 000h		;9d71	00		.
	defb 000h		;9d72	00		.
	defb 000h		;9d73	00		.
	defb 000h		;9d74	00		.
	defb 000h		;9d75	00		.
	defb 000h		;9d76	00		.
	defb 000h		;9d77	00		.
	defb 000h		;9d78	00		.
	defb 000h		;9d79	00		.
	defb 000h		;9d7a	00		.
	defb 000h		;9d7b	00		.
	defb 000h		;9d7c	00		.
	defb 000h		;9d7d	00		.
	defb 000h		;9d7e	00		.
	defb 000h		;9d7f	00		.
	defb 000h		;9d80	00		.
	defb 000h		;9d81	00		.
	defb 000h		;9d82	00		.
	defb 000h		;9d83	00		.
	defb 000h		;9d84	00		.
	defb 000h		;9d85	00		.
	defb 000h		;9d86	00		.
	defb 000h		;9d87	00		.
	defb 000h		;9d88	00		.
	defb 000h		;9d89	00		.
	defb 000h		;9d8a	00		.
	defb 000h		;9d8b	00		.
	defb 000h		;9d8c	00		.
	defb 000h		;9d8d	00		.
	defb 000h		;9d8e	00		.
	defb 000h		;9d8f	00		.
	defb 000h		;9d90	00		.
	defb 000h		;9d91	00		.
	defb 000h		;9d92	00		.
	defb 000h		;9d93	00		.
	defb 000h		;9d94	00		.
	defb 000h		;9d95	00		.
	defb 000h		;9d96	00		.
	defb 000h		;9d97	00		.
	defb 000h		;9d98	00		.
	defb 000h		;9d99	00		.
	defb 000h		;9d9a	00		.
	defb 000h		;9d9b	00		.
	defb 000h		;9d9c	00		.
	defb 000h		;9d9d	00		.
	defb 000h		;9d9e	00		.
	defb 000h		;9d9f	00		.
	defb 000h		;9da0	00		.
	defb 000h		;9da1	00		.
	defb 000h		;9da2	00		.
	defb 000h		;9da3	00		.
	defb 000h		;9da4	00		.
	defb 000h		;9da5	00		.
	defb 000h		;9da6	00		.
	defb 000h		;9da7	00		.
	defb 000h		;9da8	00		.
	defb 000h		;9da9	00		.
	defb 000h		;9daa	00		.
	defb 000h		;9dab	00		.
	defb 000h		;9dac	00		.
	defb 000h		;9dad	00		.
	defb 000h		;9dae	00		.
	defb 000h		;9daf	00		.
	defb 000h		;9db0	00		.
	defb 000h		;9db1	00		.
	defb 000h		;9db2	00		.
	defb 000h		;9db3	00		.
	defb 000h		;9db4	00		.
	defb 000h		;9db5	00		.
	defb 000h		;9db6	00		.
	defb 000h		;9db7	00		.
	defb 000h		;9db8	00		.
	defb 000h		;9db9	00		.
	defb 000h		;9dba	00		.
	defb 000h		;9dbb	00		.
	defb 000h		;9dbc	00		.
	defb 000h		;9dbd	00		.
	defb 000h		;9dbe	00		.
	defb 000h		;9dbf	00		.
	defb 000h		;9dc0	00		.
	defb 000h		;9dc1	00		.
	defb 000h		;9dc2	00		.
	defb 000h		;9dc3	00		.
	defb 000h		;9dc4	00		.
	defb 000h		;9dc5	00		.
	defb 000h		;9dc6	00		.
	defb 000h		;9dc7	00		.
	defb 000h		;9dc8	00		.
	defb 000h		;9dc9	00		.
	defb 000h		;9dca	00		.
	defb 000h		;9dcb	00		.
	defb 000h		;9dcc	00		.
	defb 000h		;9dcd	00		.
	defb 000h		;9dce	00		.
	defb 000h		;9dcf	00		.
	defb 000h		;9dd0	00		.
	defb 000h		;9dd1	00		.
	defb 000h		;9dd2	00		.
	defb 000h		;9dd3	00		.
	defb 000h		;9dd4	00		.
	defb 000h		;9dd5	00		.
	defb 000h		;9dd6	00		.
	defb 000h		;9dd7	00		.
	defb 000h		;9dd8	00		.
	defb 000h		;9dd9	00		.
	defb 000h		;9dda	00		.
	defb 000h		;9ddb	00		.
	defb 000h		;9ddc	00		.
	defb 000h		;9ddd	00		.
	defb 000h		;9dde	00		.
	defb 000h		;9ddf	00		.
	defb 000h		;9de0	00		.
	defb 000h		;9de1	00		.
	defb 000h		;9de2	00		.
	defb 000h		;9de3	00		.
	defb 000h		;9de4	00		.
	defb 000h		;9de5	00		.
	defb 000h		;9de6	00		.
	defb 000h		;9de7	00		.
	defb 000h		;9de8	00		.
	defb 000h		;9de9	00		.
	defb 000h		;9dea	00		.
	defb 000h		;9deb	00		.
	defb 000h		;9dec	00		.
	defb 000h		;9ded	00		.
	defb 000h		;9dee	00		.
	defb 000h		;9def	00		.
	defb 000h		;9df0	00		.
	defb 000h		;9df1	00		.
	defb 000h		;9df2	00		.
	defb 000h		;9df3	00		.
	defb 000h		;9df4	00		.
	defb 000h		;9df5	00		.
	defb 000h		;9df6	00		.
	defb 000h		;9df7	00		.
	defb 000h		;9df8	00		.
	defb 000h		;9df9	00		.
	defb 000h		;9dfa	00		.
	defb 000h		;9dfb	00		.
	defb 000h		;9dfc	00		.
	defb 000h		;9dfd	00		.
	defb 000h		;9dfe	00		.
	defb 000h		;9dff	00		.
SBUF:
				; = SBUF (src: KEY.ASM:2, BIOS-PP 1273243)
	defb 000h		;9e00	00		.
	defb 000h		;9e01	00		.
	defb 000h		;9e02	00		.
	defb 000h		;9e03	00		.
	defb 000h		;9e04	00		.
	defb 000h		;9e05	00		.
	defb 000h		;9e06	00		.
	defb 000h		;9e07	00		.
	defb 000h		;9e08	00		.
	defb 000h		;9e09	00		.
	defb 000h		;9e0a	00		.
	defb 000h		;9e0b	00		.
	defb 000h		;9e0c	00		.
	defb 000h		;9e0d	00		.
	defb 000h		;9e0e	00		.
	defb 000h		;9e0f	00		.
	defb 000h		;9e10	00		.
	defb 000h		;9e11	00		.
	defb 000h		;9e12	00		.
	defb 000h		;9e13	00		.
	defb 000h		;9e14	00		.
	defb 000h		;9e15	00		.
	defb 000h		;9e16	00		.
	defb 000h		;9e17	00		.
	defb 000h		;9e18	00		.
	defb 000h		;9e19	00		.
	defb 000h		;9e1a	00		.
	defb 000h		;9e1b	00		.
	defb 000h		;9e1c	00		.
	defb 000h		;9e1d	00		.
	defb 000h		;9e1e	00		.
	defb 000h		;9e1f	00		.
	defb 000h		;9e20	00		.
	defb 000h		;9e21	00		.
	defb 000h		;9e22	00		.
	defb 000h		;9e23	00		.
	defb 000h		;9e24	00		.
	defb 000h		;9e25	00		.
	defb 000h		;9e26	00		.
	defb 000h		;9e27	00		.
	defb 000h		;9e28	00		.
	defb 000h		;9e29	00		.
	defb 000h		;9e2a	00		.
	defb 000h		;9e2b	00		.
	defb 000h		;9e2c	00		.
	defb 000h		;9e2d	00		.
	defb 000h		;9e2e	00		.
	defb 000h		;9e2f	00		.
	defb 000h		;9e30	00		.
	defb 000h		;9e31	00		.
	defb 000h		;9e32	00		.
	defb 000h		;9e33	00		.
	defb 000h		;9e34	00		.
	defb 000h		;9e35	00		.
	defb 000h		;9e36	00		.
	defb 000h		;9e37	00		.
	defb 000h		;9e38	00		.
	defb 000h		;9e39	00		.
	defb 000h		;9e3a	00		.
	defb 000h		;9e3b	00		.
	defb 000h		;9e3c	00		.
	defb 000h		;9e3d	00		.
	defb 000h		;9e3e	00		.
	defb 000h		;9e3f	00		.
EBUF:
				; = EBUF (src: KEY.ASM:6, BIOS-PP 1273243)
				; = HEAD (src: KEY.ASM:8, BIOS-PP 1273243)
	defb 000h		;9e40	00		.
HOST:
				; = HOST (src: KEY.ASM:9, BIOS-PP 1273243)
	defb 000h		;9e41	00		.
KEYFLAG:
				; = KEYFLAG (src: KEY.ASM:20, BIOS-PP 1273243)
	defb 002h		;9e42	02		.
KEYCTRL:
				; = KEYCTRL (src: KEY.ASM:31, BIOS-PP 1273243)
	defb 000h		;9e43	00		.
	defb 000h		;9e44	00		.
	defb 003h		;9e45	03		.
UNCODE:
				; = UNCODE (src: KEY.ASM:83, BIOS-PP 1273243)
	defb 000h		;9e46	00		.
	defb 000h		;9e47	00		.
WAITKEY:
				; = WAITKEY (src: KEY.ASM:85, BIOS-PP 1273243)
	ld hl,HOST		;9e48	21 41 9e	! A .
	ld a,(EBUF)		;9e4b	3a 40 9e	: @ .
	cp (hl)			;9e4e	be		.
	jr z,WAITKEY	;9e4f	28 f7		( .
	call GETSYM		;9e51	cd c2 9e	. . .
	ld a,e			;9e54	7b		{
	and a			;9e55	a7		.
	ret			;9e56	c9		.
SCANKEY:
				; = SCANKEY (src: KEY.ASM:94, BIOS-PP 1273243)
	ld hl,HOST		;9e57	21 41 9e	! A .
	ld a,(EBUF)		;9e5a	3a 40 9e	: @ .
	cp (hl)			;9e5d	be		.
	ret z			;9e5e	c8		.
	call GETSYM		;9e5f	cd c2 9e	. . .
	ld a,e			;9e62	7b		{
	ret			;9e63	c9		.
CTRLKEY:
				; = CTRLKEY (src: KEY.ASM:113, BIOS-PP 1273243)
	ld hl,HOST		;9e64	21 41 9e	! A .
	ld a,(EBUF)		;9e67	3a 40 9e	: @ .
	cp (hl)			;9e6a	be		.
	ld bc,(KEYFLAG)		;9e6b	ed 4b 42 9e	. K B .
	ld a,000h		;9e6f	3e 00		> .
	ret z			;9e71	c8		.
	dec a			;9e72	3d		=
	ret			;9e73	c9		.
TESTKEY:
				; = TESTKEY (src: KEY.ASM:122, BIOS-PP 1273243)
	ld hl,HOST		;9e74	21 41 9e	! A .
	ld a,(EBUF)		;9e77	3a 40 9e	: @ .
	cp (hl)			;9e7a	be		.
	ret z			;9e7b	c8		.
	ld l,(hl)		;9e7c	6e		n
	ld h,09eh		;9e7d	26 9e		& .
	ld e,(hl)		;9e7f	5e		^
	inc l			;9e80	2c		,
	ld d,(hl)		;9e81	56		V
	inc l			;9e82	2c		,
	ld b,(hl)		;9e83	46		F
	inc l			;9e84	2c		,
	ld c,(hl)		;9e85	4e		N
	ld a,e			;9e86	7b		{
	ret			;9e87	c9		.
K_CLEAR:
				; = K_CLEAR (src: KEY.ASM:138, BIOS-PP 1273243)
	ld a,(HOST)		;9e88	3a 41 9e	: A .
	ld (EBUF),a		;9e8b	32 40 9e	2 @ .
	ld a,02fh		;9e8e	3e 2f		> /
	cp b			;9e90	b8		.
	jr c,K_C2		;9e91	38 04		8 .
	ld a,001h		;9e93	3e 01		> .
	scf			;9e95	37		7
	ret			;9e96	c9		.
K_C2:
				; = K_C2 (src: KEY.ASM:147, BIOS-PP 1273243)
	ld a,035h		;9e97	3e 35		> 5
	cp b			;9e99	b8		.
	jr nc,K_C3		;9e9a	30 04		0 .
	ld a,001h		;9e9c	3e 01		> .
	scf			;9e9e	37		7
	ret			;9e9f	c9		.
K_C3:
				; = K_C3 (src: KEY.ASM:154, BIOS-PP 1273243)
	ld c,b			;9ea0	48		H
	rst 10h			;9ea1	d7		.
	ret			;9ea2	c9		.
PUTSYM:
				; = PUTSYM (src: KEY.ASM:158, BIOS-PP 1273243)
	ld hl,EBUF		;9ea3	21 40 9e	! @ .
	ld a,(HOST)		;9ea6	3a 41 9e	: A .
	sub 004h		;9ea9	d6 04		. .
	and 03fh		;9eab	e6 3f		. ?
	cp (hl)			;9ead	be		.
	jr z,FULL_BF		;9eae	28 2c		( ,
	ld a,(hl)		;9eb0	7e		~
	inc (hl)		;9eb1	34		4
	inc (hl)		;9eb2	34		4
	inc (hl)		;9eb3	34		4
	inc (hl)		;9eb4	34		4
	res 6,(hl)		;9eb5	cb b6		. .
	ld l,a			;9eb7	6f		o
	ld h,09eh		;9eb8	26 9e		& .
	ld (hl),e		;9eba	73		s
	inc l			;9ebb	2c		,
	ld (hl),d		;9ebc	72		r
	inc l			;9ebd	2c		,
	ld (hl),b		;9ebe	70		p
	inc l			;9ebf	2c		,
	ld (hl),c		;9ec0	71		q
	ret			;9ec1	c9		.
GETSYM:
				; = GETSYM (src: KEY.ASM:181, BIOS-PP 1273243)
	ld hl,HOST		;9ec2	21 41 9e	! A .
	ld a,(EBUF)		;9ec5	3a 40 9e	: @ .
	cp (hl)			;9ec8	be		.
	ret z			;9ec9	c8		.
	ld a,(hl)		;9eca	7e		~
	inc (hl)		;9ecb	34		4
	inc (hl)		;9ecc	34		4
	inc (hl)		;9ecd	34		4
	inc (hl)		;9ece	34		4
	res 6,(hl)		;9ecf	cb b6		. .
	ld l,a			;9ed1	6f		o
	ld h,09eh		;9ed2	26 9e		& .
	ld e,(hl)		;9ed4	5e		^
	inc l			;9ed5	2c		,
	ld d,(hl)		;9ed6	56		V
	inc l			;9ed7	2c		,
	ld b,(hl)		;9ed8	46		F
	inc l			;9ed9	2c		,
	ld c,(hl)		;9eda	4e		N
	ret			;9edb	c9		.
FULL_BF:
				; = FULL_BF (src: KEY.ASM:202, BIOS-PP 1273243)
	ex af,af'		;9edc	08		.
	bit 0,(ix+003h)		;9edd	dd cb 03 46	. . . F
	jr z,l9eeeh		;9ee1	28 0b		( .
	exx			;9ee3	d9		.
	ld de,000e6h		;9ee4	11 e6 00	. . .
	ld hl,00032h		;9ee7	21 32 00	! 2 .
	call BEEP	;9eea	cd 57 a3	. W .
	exx			;9eed	d9		.
l9eeeh:
	ex af,af'		;9eee	08		.
	ret			;9eef	c9		.
;---------------------------------------------------------------------------
; KeyboardInterrupt: while SIO A (#19) reports a received byte, read it
; from #18 (AT scan code set 2 from the keyboard controller in the PLD /
; keyboard), handle the #E0 / #F0 prefixes and put the key into the
; ring buffer (HEAD/HOST at #9E40/#9E41).
;---------------------------------------------------------------------------
KeyboardInterrupt:
	ld ix,KEYFLAG		;9ef0	dd 21 42 9e	. ! B .
RESCANN:
				; = RESCANN (src: KEY.ASM:215, BIOS-PP 1273243)
	in a,(019h)		;9ef4	db 19		. .
	bit 0,a			;9ef6	cb 47		. G
	ret z			;9ef8	c8		.
	in a,(018h)		;9ef9	db 18		. .
	cp 0f0h			;9efb	fe f0		. .
	jr z,l9f64h		;9efd	28 65		( e
	cp 0e0h			;9eff	fe e0		. .
	jr z,l9f5eh		;9f01	28 5b		( [
	cp 0e1h			;9f03	fe e1		. .
	jr z,l9f6ah		;9f05	28 63		( c
	bit 6,(ix+002h)		;9f07	dd cb 02 76	. . . v
	jr nz,l9f70h		;9f0b	20 63		  c
	ld l,a			;9f0d	6f		o
	call XLAT	;9f0e	cd 90 a1	. . .
	call SHIFTS		;9f11	cd 31 a0	. 1 .
	res 7,(ix+002h)		;9f14	dd cb 02 be	. . . .
	res 5,(ix+002h)		;9f18	dd cb 02 ae	. . . .
	ret z			;9f1c	c8		.
	call INPCODE		;9f1d	cd b4 a1	. . .
	ld hl,01c00h		;9f20	21 00 1c	! . .
	and a			;9f23	a7		.
	sbc hl,de		;9f24	ed 52		. R
	call z,CAPS_X		;9f26	cc 85 9f	. . .
	ld hl,05000h		;9f29	21 00 50	! . P
	and a			;9f2c	a7		.
	sbc hl,de		;9f2d	ed 52		. R
	call z,INS_X		;9f2f	cc 9e 9f	. . .
	ld hl,04900h		;9f32	21 00 49	! . I
	and a			;9f35	a7		.
	sbc hl,de		;9f36	ed 52		. R
	call z,NUM_X		;9f38	cc a7 9f	. . .
	ld hl,0c900h		;9f3b	21 00 c9	! . .
	and a			;9f3e	a7		.
	sbc hl,de		;9f3f	ed 52		. R
	call z,PAUSE_X		;9f41	cc b0 9f	. . .
	ld hl,04800h		;9f44	21 00 48	! . H
	and a			;9f47	a7		.
	sbc hl,de		;9f48	ed 52		. R
	call z,SCL_X	;9f4a	cc cd 9f	. . .
	ld hl,0cf00h		;9f4d	21 00 cf	! . .
	and a			;9f50	a7		.
	sbc hl,de		;9f51	ed 52		. R
	call z,RST_X		;9f53	cc 8e 9f	. . .
	ld bc,(KEYFLAG)		;9f56	ed 4b 42 9e	. K B .
	call PUTSYM		;9f5a	cd a3 9e	. . .
	ret			;9f5d	c9		.
l9f5eh:
	set 7,(ix+002h)		;9f5e	dd cb 02 fe	. . . .
	jr RESCANN		;9f62	18 90		. .
l9f64h:
	set 6,(ix+002h)		;9f64	dd cb 02 f6	. . . .
	jr RESCANN		;9f68	18 8a		. .
l9f6ah:
	set 5,(ix+002h)		;9f6a	dd cb 02 ee	. . . .
	jr RESCANN		;9f6e	18 84		. .
l9f70h:
	res 6,(ix+002h)		;9f70	dd cb 02 b6	. . . .
	ld l,a			;9f74	6f		o
	call XLAT	;9f75	cd 90 a1	. . .
	call UNSHIFT		;9f78	cd d6 9f	. . .
	res 7,(ix+002h)		;9f7b	dd cb 02 be	. . . .
	ld h,000h		;9f7f	26 00		& .
	ld (UNCODE),hl		;9f81	22 46 9e	" F .
	ret			;9f84	c9		.
CAPS_X:
				; = CAPS_X (src: KEY.ASM:287, BIOS-PP 1273243)
	ld a,(ix+000h)		;9f85	dd 7e 00	. ~ .
	xor 001h		;9f88	ee 01		. .
	ld (ix+000h),a		;9f8a	dd 77 00	. w .
	ret			;9f8d	c9		.
RST_X:
				; = RST_X (src: KEY.ASM:306, BIOS-PP 1273243)
	bit 5,(ix+001h)		;9f8e	dd cb 01 6e	. . . n
	ret z			;9f92	c8		.
	bit 4,(ix+001h)		;9f93	dd cb 01 66	. . . f
	ret z			;9f97	c8		.
	xor a			;9f98	af		.
	ld bc,002fdh		;9f99	01 fd 02	. . .
	rst 18h			;9f9c	df		.
	ret			;9f9d	c9		.
INS_X:
				; = INS_X (src: KEY.ASM:315, BIOS-PP 1273243)
	ld a,(ix+000h)		;9f9e	dd 7e 00	. ~ .
	xor 002h		;9fa1	ee 02		. .
	ld (ix+000h),a		;9fa3	dd 77 00	. w .
	ret			;9fa6	c9		.
NUM_X:
				; = NUM_X (src: KEY.ASM:320, BIOS-PP 1273243)
	ld a,(ix+000h)		;9fa7	dd 7e 00	. ~ .
	xor 008h		;9faa	ee 08		. .
	ld (ix+000h),a		;9fac	dd 77 00	. w .
	ret			;9faf	c9		.
PAUSE_X:
				; = PAUSE_X (src: KEY.ASM:325, BIOS-PP 1273243)
	bit 5,(ix+001h)		;9fb0	dd cb 01 6e	. . . n
	ret z			;9fb4	c8		.
	pop hl			;9fb5	e1		.
	ld a,(ix+000h)		;9fb6	dd 7e 00	. ~ .
	xor 040h		;9fb9	ee 40		. @
	ld (ix+000h),a		;9fbb	dd 77 00	. w .
	bit 6,(ix+000h)		;9fbe	dd cb 00 76	. . . v
	ret z			;9fc2	c8		.
	ei			;9fc3	fb		.
PAUSE_:
				; = PAUSE_ (src: KEY.ASM:334, BIOS-PP 1273243)
	halt			;9fc4	76		v

; BLOCK 'Data9FC5' (start 0x9fc5 end 0x9fcd)
Data9FC5:
	defb 0ddh		;9fc5	dd		.
	defb 0cbh		;9fc6	cb		.
	defb 000h		;9fc7	00		.
	defb 076h		;9fc8	76		v
	defb 020h		;9fc9	20		 
	defb 0f9h		;9fca	f9		.
	defb 0f3h		;9fcb	f3		.
	defb 0c9h		;9fcc	c9		.
SCL_X:
				; = SCL_X (src: KEY.ASM:340, BIOS-PP 1273243)
	ld a,(ix+000h)		;9fcd	dd 7e 00	. ~ .
	xor 004h		;9fd0	ee 04		. .
	ld (ix+000h),a		;9fd2	dd 77 00	. w .
	ret			;9fd5	c9		.
UNSHIFT:
				; = UNSHIFT (src: KEY.ASM:345, BIOS-PP 1273243)
	ld a,l			;9fd6	7d		}
	cp 037h			;9fd7	fe 37		. 7
	jr nz,USH1		;9fd9	20 0e		  .
	res 2,(ix+001h)		;9fdb	dd cb 01 96	. . . .
	bit 0,(ix+001h)		;9fdf	dd cb 01 46	. . . F
	ret nz			;9fe3	c0		.
	res 4,(ix+001h)		;9fe4	dd cb 01 a6	. . . .
	ret			;9fe8	c9		.
USH1:
				; = USH1 (src: KEY.ASM:353, BIOS-PP 1273243)
	cp 039h			;9fe9	fe 39		. 9
	jr nz,USH2		;9feb	20 0e		  .
	res 0,(ix+001h)		;9fed	dd cb 01 86	. . . .
	bit 2,(ix+001h)		;9ff1	dd cb 01 56	. . . V
	ret nz			;9ff5	c0		.
	res 4,(ix+001h)		;9ff6	dd cb 01 a6	. . . .
	ret			;9ffa	c9		.
USH2:
				; = USH2 (src: KEY.ASM:360, BIOS-PP 1273243)
	cp 036h			;9ffb	fe 36		. 6
	jr nz,USH3		;9ffd	20 0e		  .
	res 3,(ix+001h)		;9fff	dd cb 01 9e	. . . .
	bit 1,(ix+001h)		;a003	dd cb 01 4e	. . . N
	ret nz			;a007	c0		.
	res 5,(ix+001h)		;a008	dd cb 01 ae	. . . .
	ret			;a00c	c9		.
USH3:
				; = USH3 (src: KEY.ASM:367, BIOS-PP 1273243)
	cp 03ah			;a00d	fe 3a		. :
	jr nz,USH4		;a00f	20 0e		  .
	res 1,(ix+001h)		;a011	dd cb 01 8e	. . . .
	bit 3,(ix+001h)		;a015	dd cb 01 5e	. . . ^
	ret nz			;a019	c0		.
	res 5,(ix+001h)		;a01a	dd cb 01 ae	. . . .
	ret			;a01e	c9		.
USH4:
				; = USH4 (src: KEY.ASM:374, BIOS-PP 1273243)
	cp 029h			;a01f	fe 29		. )
	jr nz,USH5		;a021	20 05		  .
	res 7,(ix+001h)		;a023	dd cb 01 be	. . . .
	ret			;a027	c9		.
USH5:
				; = USH5 (src: KEY.ASM:378, BIOS-PP 1273243)
	cp 034h			;a028	fe 34		. 4
	jr nz,USH6		;a02a	20 04		  .
	res 6,(ix+001h)		;a02c	dd cb 01 b6	. . . .
USH6:
				; = USH6 (src: KEY.ASM:381, BIOS-PP 1273243)
	ret			;a030	c9		.
SHIFTS:
				; = SHIFTS (src: KEY.ASM:383, BIOS-PP 1273243)
	ld a,l			;a031	7d		}
	cp 037h			;a032	fe 37		. 7
	jr nz,NSH1		;a034	20 09		  .
	set 2,(ix+001h)		;a036	dd cb 01 d6	. . . .
	set 4,(ix+001h)		;a03a	dd cb 01 e6	. . . .
	ret			;a03e	c9		.
NSH1:
				; = NSH1 (src: KEY.ASM:389, BIOS-PP 1273243)
	cp 039h			;a03f	fe 39		. 9
	jr nz,NSH2		;a041	20 09		  .
	set 0,(ix+001h)		;a043	dd cb 01 c6	. . . .
	set 4,(ix+001h)		;a047	dd cb 01 e6	. . . .
	ret			;a04b	c9		.
NSH2:
				; = NSH2 (src: KEY.ASM:394, BIOS-PP 1273243)
	cp 036h			;a04c	fe 36		. 6
	jr nz,NSH3		;a04e	20 09		  .
	set 3,(ix+001h)		;a050	dd cb 01 de	. . . .
	set 5,(ix+001h)		;a054	dd cb 01 ee	. . . .
	ret			;a058	c9		.
NSH3:
				; = NSH3 (src: KEY.ASM:399, BIOS-PP 1273243)
	cp 03ah			;a059	fe 3a		. :
	jr nz,NSH4		;a05b	20 09		  .
	set 1,(ix+001h)		;a05d	dd cb 01 ce	. . . .
	set 5,(ix+001h)		;a061	dd cb 01 ee	. . . .
	ret			;a065	c9		.
NSH4:
				; = NSH4 (src: KEY.ASM:404, BIOS-PP 1273243)
	cp 029h			;a066	fe 29		. )
	jr nz,NSH5		;a068	20 05		  .
	set 7,(ix+001h)		;a06a	dd cb 01 fe	. . . .
	ret			;a06e	c9		.
NSH5:
				; = NSH5 (src: KEY.ASM:408, BIOS-PP 1273243)
	cp 034h			;a06f	fe 34		. 4
	jr nz,NSH6		;a071	20 04		  .
	set 6,(ix+001h)		;a073	dd cb 01 f6	. . . .
NSH6:
				; = NSH6 (src: KEY.ASM:411, BIOS-PP 1273243)
	ret			;a077	c9		.

; BLOCK 'DataA078' (start 0xa078 end 0xa190)
DataA078:
	defb 000h		;a078	00		.
	defb 000h		;a079	00		.
	defb 000h		;a07a	00		.
	defb 000h		;a07b	00		.
	defb 000h		;a07c	00		.
	defb 000h		;a07d	00		.
	defb 000h		;a07e	00		.
	defb 000h		;a07f	00		.
	defb 000h		;a080	00		.
	defb 000h		;a081	00		.
	defb 000h		;a082	00		.
	defb 000h		;a083	00		.
	defb 000h		;a084	00		.
	defb 000h		;a085	00		.
	defb 000h		;a086	00		.
	defb 000h		;a087	00		.
	defb 000h		;a088	00		.
	defb 000h		;a089	00		.
	defb 000h		;a08a	00		.
	defb 000h		;a08b	00		.
	defb 000h		;a08c	00		.
	defb 000h		;a08d	00		.
	defb 000h		;a08e	00		.
	defb 000h		;a08f	00		.
	defb 000h		;a090	00		.
	defb 000h		;a091	00		.
	defb 000h		;a092	00		.
	defb 000h		;a093	00		.
	defb 000h		;a094	00		.
	defb 000h		;a095	00		.
	defb 000h		;a096	00		.
	defb 000h		;a097	00		.
	defb 000h		;a098	00		.
	defb 000h		;a099	00		.
	defb 000h		;a09a	00		.
	defb 000h		;a09b	00		.
	defb 000h		;a09c	00		.
	defb 000h		;a09d	00		.
	defb 000h		;a09e	00		.
	defb 000h		;a09f	00		.
	defb 000h		;a0a0	00		.
	defb 000h		;a0a1	00		.
	defb 000h		;a0a2	00		.
	defb 000h		;a0a3	00		.
	defb 000h		;a0a4	00		.
	defb 000h		;a0a5	00		.
	defb 000h		;a0a6	00		.
	defb 000h		;a0a7	00		.
	defb 000h		;a0a8	00		.
	defb 000h		;a0a9	00		.
	defb 000h		;a0aa	00		.
	defb 000h		;a0ab	00		.
	defb 000h		;a0ac	00		.
	defb 000h		;a0ad	00		.
	defb 000h		;a0ae	00		.
	defb 000h		;a0af	00		.
	defb 000h		;a0b0	00		.
	defb 000h		;a0b1	00		.
	defb 000h		;a0b2	00		.
	defb 000h		;a0b3	00		.
	defb 000h		;a0b4	00		.
	defb 000h		;a0b5	00		.
	defb 000h		;a0b6	00		.
	defb 000h		;a0b7	00		.
	defb 000h		;a0b8	00		.
	defb 000h		;a0b9	00		.
	defb 000h		;a0ba	00		.
	defb 000h		;a0bb	00		.
	defb 000h		;a0bc	00		.
	defb 000h		;a0bd	00		.
	defb 000h		;a0be	00		.
	defb 000h		;a0bf	00		.
	defb 000h		;a0c0	00		.
	defb 000h		;a0c1	00		.
	defb 000h		;a0c2	00		.
	defb 000h		;a0c3	00		.
	defb 000h		;a0c4	00		.
	defb 000h		;a0c5	00		.
	defb 000h		;a0c6	00		.
	defb 000h		;a0c7	00		.
	defb 000h		;a0c8	00		.
	defb 000h		;a0c9	00		.
	defb 000h		;a0ca	00		.
	defb 000h		;a0cb	00		.
	defb 000h		;a0cc	00		.
	defb 000h		;a0cd	00		.
	defb 000h		;a0ce	00		.
	defb 000h		;a0cf	00		.
	defb 000h		;a0d0	00		.
	defb 000h		;a0d1	00		.
	defb 000h		;a0d2	00		.
	defb 000h		;a0d3	00		.
	defb 000h		;a0d4	00		.
	defb 000h		;a0d5	00		.
	defb 000h		;a0d6	00		.
	defb 000h		;a0d7	00		.
	defb 000h		;a0d8	00		.
	defb 000h		;a0d9	00		.
	defb 000h		;a0da	00		.
	defb 000h		;a0db	00		.
	defb 000h		;a0dc	00		.
	defb 000h		;a0dd	00		.
	defb 000h		;a0de	00		.
	defb 000h		;a0df	00		.
	defb 000h		;a0e0	00		.
	defb 000h		;a0e1	00		.
	defb 000h		;a0e2	00		.
	defb 000h		;a0e3	00		.
	defb 000h		;a0e4	00		.
	defb 000h		;a0e5	00		.
	defb 000h		;a0e6	00		.
	defb 000h		;a0e7	00		.
	defb 000h		;a0e8	00		.
	defb 000h		;a0e9	00		.
	defb 000h		;a0ea	00		.
	defb 000h		;a0eb	00		.
	defb 000h		;a0ec	00		.
	defb 000h		;a0ed	00		.
	defb 000h		;a0ee	00		.
	defb 000h		;a0ef	00		.
	defb 000h		;a0f0	00		.
	defb 000h		;a0f1	00		.
	defb 000h		;a0f2	00		.
	defb 000h		;a0f3	00		.
	defb 000h		;a0f4	00		.
	defb 000h		;a0f5	00		.
	defb 000h		;a0f6	00		.
	defb 000h		;a0f7	00		.
	defb 000h		;a0f8	00		.
	defb 000h		;a0f9	00		.
	defb 000h		;a0fa	00		.
	defb 000h		;a0fb	00		.
	defb 000h		;a0fc	00		.
	defb 000h		;a0fd	00		.
	defb 000h		;a0fe	00		.
	defb 000h		;a0ff	00		.
XLAT_T:
				; = XLAT_T (src: KEY.ASM:419, BIOS-PP 1273243)
	defb 000h		;a100	00		.
	defb 043h		;a101	43		C
	defb 000h		;a102	00		.
	defb 03fh		;a103	3f		?
	defb 03dh		;a104	3d		=
	defb 03bh		;a105	3b		;
	defb 03ch		;a106	3c		<
	defb 046h		;a107	46		F
	defb 000h		;a108	00		.
	defb 044h		;a109	44		D
	defb 042h		;a10a	42		B
	defb 040h		;a10b	40		@
	defb 03eh		;a10c	3e		>
	defb 00fh		;a10d	0f		.
	defb 000h		;a10e	00		.
	defb 000h		;a10f	00		.
	defb 000h		;a110	00		.
	defb 037h		;a111	37		7
	defb 029h		;a112	29		)
	defb 000h		;a113	00		.
	defb 036h		;a114	36		6
	defb 010h		;a115	10		.
	defb 002h		;a116	02		.
	defb 000h		;a117	00		.
	defb 000h		;a118	00		.
	defb 000h		;a119	00		.
	defb 02ah		;a11a	2a		*
	defb 01eh		;a11b	1e		.
	defb 01dh		;a11c	1d		.
	defb 011h		;a11d	11		.
	defb 003h		;a11e	03		.
	defb 000h		;a11f	00		.
	defb 000h		;a120	00		.
	defb 02ch		;a121	2c		,
	defb 02bh		;a122	2b		+
	defb 01fh		;a123	1f		.
	defb 012h		;a124	12		.
	defb 005h		;a125	05		.
	defb 004h		;a126	04		.
	defb 000h		;a127	00		.
	defb 000h		;a128	00		.
	defb 038h		;a129	38		8
	defb 02dh		;a12a	2d		-
	defb 020h		;a12b	20		 
	defb 014h		;a12c	14		.
	defb 013h		;a12d	13		.
	defb 006h		;a12e	06		.
	defb 000h		;a12f	00		.
	defb 000h		;a130	00		.
	defb 02fh		;a131	2f		/
	defb 02eh		;a132	2e		.
	defb 022h		;a133	22		"
	defb 021h		;a134	21		!
	defb 015h		;a135	15		.
	defb 007h		;a136	07		.
	defb 000h		;a137	00		.
	defb 000h		;a138	00		.
	defb 000h		;a139	00		.
	defb 030h		;a13a	30		0
	defb 023h		;a13b	23		#
	defb 016h		;a13c	16		.
	defb 008h		;a13d	08		.
	defb 009h		;a13e	09		.
	defb 000h		;a13f	00		.
	defb 000h		;a140	00		.
	defb 031h		;a141	31		1
	defb 024h		;a142	24		$
	defb 017h		;a143	17		.
	defb 018h		;a144	18		.
	defb 00bh		;a145	0b		.
	defb 00ah		;a146	0a		.
	defb 000h		;a147	00		.
	defb 000h		;a148	00		.
	defb 032h		;a149	32		2
	defb 033h		;a14a	33		3
	defb 025h		;a14b	25		%
	defb 026h		;a14c	26		&
	defb 019h		;a14d	19		.
	defb 00ch		;a14e	0c		.
	defb 000h		;a14f	00		.
	defb 000h		;a150	00		.
	defb 000h		;a151	00		.
	defb 027h		;a152	27		'
	defb 000h		;a153	00		.
	defb 01ah		;a154	1a		.
	defb 00dh		;a155	0d		.
	defb 000h		;a156	00		.
	defb 000h		;a157	00		.
	defb 01ch		;a158	1c		.
	defb 034h		;a159	34		4
	defb 028h		;a15a	28		(
	defb 01bh		;a15b	1b		.
	defb 000h		;a15c	00		.
	defb 035h		;a15d	35		5
	defb 000h		;a15e	00		.
	defb 000h		;a15f	00		.
	defb 000h		;a160	00		.
	defb 000h		;a161	00		.
	defb 000h		;a162	00		.
	defb 000h		;a163	00		.
	defb 000h		;a164	00		.
	defb 000h		;a165	00		.
	defb 00eh		;a166	0e		.
	defb 000h		;a167	00		.
	defb 000h		;a168	00		.
	defb 051h		;a169	51		Q
	defb 000h		;a16a	00		.
	defb 054h		;a16b	54		T
	defb 057h		;a16c	57		W
	defb 000h		;a16d	00		.
	defb 000h		;a16e	00		.
	defb 000h		;a16f	00		.
	defb 050h		;a170	50		P
	defb 04fh		;a171	4f		O
	defb 052h		;a172	52		R
	defb 055h		;a173	55		U
	defb 056h		;a174	56		V
	defb 058h		;a175	58		X
	defb 001h		;a176	01		.
	defb 049h		;a177	49		I
	defb 045h		;a178	45		E
	defb 04dh		;a179	4d		M
	defb 053h		;a17a	53		S
	defb 04ch		;a17b	4c		L
	defb 04bh		;a17c	4b		K
	defb 059h		;a17d	59		Y
	defb 048h		;a17e	48		H
	defb 000h		;a17f	00		.
	defb 000h		;a180	00		.
	defb 000h		;a181	00		.
	defb 000h		;a182	00		.
	defb 041h		;a183	41		A
	defb 000h		;a184	00		.
	defb 000h		;a185	00		.
	defb 000h		;a186	00		.
	defb 000h		;a187	00		.
	defb 000h		;a188	00		.
	defb 000h		;a189	00		.
	defb 000h		;a18a	00		.
	defb 000h		;a18b	00		.
	defb 000h		;a18c	00		.
	defb 000h		;a18d	00		.
	defb 000h		;a18e	00		.
	defb 000h		;a18f	00		.
XLAT:
				; = XLAT (src: KEY.ASM:430, BIOS-PP 1273243)
	bit 7,(ix+002h)		;a190	dd cb 02 7e	. . . ~
	jr z,W_O_E0		;a194	28 1a		( .
	cp 011h			;a196	fe 11		. .
	ld l,039h		;a198	2e 39		. 9
	ret z			;a19a	c8		.
	cp 014h			;a19b	fe 14		. .
	ld l,03ah		;a19d	2e 3a		. :
	ret z			;a19f	c8		.
	cp 05ah			;a1a0	fe 5a		. Z
	ld l,04eh		;a1a2	2e 4e		. N
	ret z			;a1a4	c8		.
	cp 04ah			;a1a5	fe 4a		. J
	ld l,04ah		;a1a7	2e 4a		. J
	ret z			;a1a9	c8		.
	cp 07ch			;a1aa	fe 7c		. |
	ld l,047h		;a1ac	2e 47		. G
	ret z			;a1ae	c8		.
	ld l,a			;a1af	6f		o
W_O_E0:
				; = W_O_E0 (src: KEY.ASM:448, BIOS-PP 1273243)
	ld h,0a1h		;a1b0	26 a1		& .
	ld l,(hl)		;a1b2	6e		n
	ret			;a1b3	c9		.
INPCODE:
				; = INPCODE (src: KEY.ASM:456, BIOS-PP 1273243)
	ld d,l			;a1b4	55		U
	ld e,000h		;a1b5	1e 00		. .
	ld a,(ix+001h)		;a1b7	dd 7e 01	. ~ .
	and 0c0h		;a1ba	e6 c0		. .
	jr nz,la1dch		;a1bc	20 1e		  .
	set 7,d			;a1be	cb fa		. .
	bit 4,(ix+001h)		;a1c0	dd cb 01 66	. . . f
	ret nz			;a1c4	c0		.
	bit 5,(ix+001h)		;a1c5	dd cb 01 6e	. . . n
	ret nz			;a1c9	c0		.
	ld d,l			;a1ca	55		U
	bit 0,(ix+000h)		;a1cb	dd cb 00 46	. . . F
	ld bc,CAPSTAB		;a1cf	01 a3 a2	. . .
	jr nz,la1d7h		;a1d2	20 03		  .
	ld bc,NORMTAB		;a1d4	01 ef a1	. . .
la1d7h:
	ld h,000h		;a1d7	26 00		& .
	add hl,bc		;a1d9	09		.
	ld e,(hl)		;a1da	5e		^
	ret			;a1db	c9		.
la1dch:
	ld bc,ENDNORM		;a1dc	01 49 a2	. I .
	bit 0,(ix+000h)		;a1df	dd cb 00 46	. . . F
	jr z,CONVER5		;a1e3	28 03		( .
	ld bc,SHF2TAB		;a1e5	01 fd a2	. . .
CONVER5:
				; = CONVER5 (src: KEY.ASM:482, BIOS-PP 1273243)
	ld h,000h		;a1e8	26 00		& .
	add hl,bc		;a1ea	09		.
	ld e,(hl)		;a1eb	5e		^
	set 7,d			;a1ec	cb fa		. .
	ret			;a1ee	c9		.
NORMTAB:

; BLOCK 'DataA1EF' (start 0xa1ef end 0xa357)
				; = NORMTAB (src: KEY.ASM:561, BIOS-PP 1273243)
	defb 060h		;a1ef	60		`
	defb 01bh		;a1f0	1b		.
	defb 031h		;a1f1	31		1
	defb 032h		;a1f2	32		2
	defb 033h		;a1f3	33		3
	defb 034h		;a1f4	34		4
	defb 035h		;a1f5	35		5
	defb 036h		;a1f6	36		6
	defb 037h		;a1f7	37		7
	defb 038h		;a1f8	38		8
	defb 039h		;a1f9	39		9
	defb 030h		;a1fa	30		0
	defb 02dh		;a1fb	2d		-
	defb 03dh		;a1fc	3d		=
	defb 008h		;a1fd	08		.
	defb 009h		;a1fe	09		.
	defb 071h		;a1ff	71		q
	defb 077h		;a200	77		w
	defb 065h		;a201	65		e
	defb 072h		;a202	72		r
	defb 074h		;a203	74		t
	defb 079h		;a204	79		y
	defb 075h		;a205	75		u
	defb 069h		;a206	69		i
	defb 06fh		;a207	6f		o
	defb 070h		;a208	70		p
	defb 05bh		;a209	5b		[
	defb 05dh		;a20a	5d		]
	defb 000h		;a20b	00		.
	defb 061h		;a20c	61		a
	defb 073h		;a20d	73		s
	defb 064h		;a20e	64		d
	defb 066h		;a20f	66		f
	defb 067h		;a210	67		g
	defb 068h		;a211	68		h
	defb 06ah		;a212	6a		j
	defb 06bh		;a213	6b		k
	defb 06ch		;a214	6c		l
	defb 03bh		;a215	3b		;
	defb 027h		;a216	27		'
	defb 00dh		;a217	0d		.
	defb 000h		;a218	00		.
	defb 07ah		;a219	7a		z
	defb 078h		;a21a	78		x
	defb 063h		;a21b	63		c
	defb 076h		;a21c	76		v
	defb 062h		;a21d	62		b
	defb 06eh		;a21e	6e		n
	defb 06dh		;a21f	6d		m
	defb 02ch		;a220	2c		,
	defb 02eh		;a221	2e		.
	defb 02fh		;a222	2f		/
	defb 000h		;a223	00		.
	defb 05ch		;a224	5c		\
	defb 000h		;a225	00		.
	defb 000h		;a226	00		.
	defb 020h		;a227	20		 
	defb 000h		;a228	00		.
	defb 000h		;a229	00		.
	defb 000h		;a22a	00		.
	defb 000h		;a22b	00		.
	defb 000h		;a22c	00		.
	defb 000h		;a22d	00		.
	defb 000h		;a22e	00		.
	defb 000h		;a22f	00		.
	defb 000h		;a230	00		.
	defb 000h		;a231	00		.
	defb 000h		;a232	00		.
	defb 000h		;a233	00		.
	defb 000h		;a234	00		.
	defb 000h		;a235	00		.
	defb 000h		;a236	00		.
	defb 000h		;a237	00		.
	defb 000h		;a238	00		.
	defb 02fh		;a239	2f		/
	defb 02ah		;a23a	2a		*
	defb 02dh		;a23b	2d		-
	defb 02bh		;a23c	2b		+
	defb 00dh		;a23d	0d		.
	defb 000h		;a23e	00		.
	defb 000h		;a23f	00		.
	defb 000h		;a240	00		.
	defb 000h		;a241	00		.
	defb 000h		;a242	00		.
	defb 000h		;a243	00		.
	defb 000h		;a244	00		.
	defb 000h		;a245	00		.
	defb 000h		;a246	00		.
	defb 000h		;a247	00		.
	defb 000h		;a248	00		.
ENDNORM:
				; = ENDNORM (src: KEY.ASM:569, BIOS-PP 1273243)
				; = SHIFTAB (src: KEY.ASM:571, BIOS-PP 1273243)
	defb 07eh		;a249	7e		~
	defb 01bh		;a24a	1b		.
	defb 021h		;a24b	21		!
	defb 040h		;a24c	40		@
	defb 023h		;a24d	23		#
	defb 024h		;a24e	24		$
	defb 025h		;a24f	25		%
	defb 05eh		;a250	5e		^
	defb 026h		;a251	26		&
	defb 02ah		;a252	2a		*
	defb 028h		;a253	28		(
	defb 029h		;a254	29		)
	defb 05fh		;a255	5f		_
	defb 02bh		;a256	2b		+
	defb 008h		;a257	08		.
	defb 009h		;a258	09		.
	defb 051h		;a259	51		Q
	defb 057h		;a25a	57		W
	defb 045h		;a25b	45		E
	defb 052h		;a25c	52		R
	defb 054h		;a25d	54		T
	defb 059h		;a25e	59		Y
	defb 055h		;a25f	55		U
	defb 049h		;a260	49		I
	defb 04fh		;a261	4f		O
	defb 050h		;a262	50		P
	defb 07bh		;a263	7b		{
	defb 07dh		;a264	7d		}
	defb 000h		;a265	00		.
	defb 041h		;a266	41		A
	defb 053h		;a267	53		S
	defb 044h		;a268	44		D
	defb 046h		;a269	46		F
	defb 047h		;a26a	47		G
	defb 048h		;a26b	48		H
	defb 04ah		;a26c	4a		J
	defb 04bh		;a26d	4b		K
	defb 04ch		;a26e	4c		L
	defb 03ah		;a26f	3a		:
	defb 022h		;a270	22		"
	defb 00dh		;a271	0d		.
	defb 000h		;a272	00		.
	defb 05ah		;a273	5a		Z
	defb 058h		;a274	58		X
	defb 043h		;a275	43		C
	defb 056h		;a276	56		V
	defb 042h		;a277	42		B
	defb 04eh		;a278	4e		N
	defb 04dh		;a279	4d		M
	defb 03ch		;a27a	3c		<
	defb 03eh		;a27b	3e		>
	defb 03fh		;a27c	3f		?
	defb 000h		;a27d	00		.
	defb 07ch		;a27e	7c		|
	defb 000h		;a27f	00		.
	defb 000h		;a280	00		.
	defb 020h		;a281	20		 
	defb 000h		;a282	00		.
	defb 000h		;a283	00		.
	defb 000h		;a284	00		.
	defb 000h		;a285	00		.
	defb 000h		;a286	00		.
	defb 000h		;a287	00		.
	defb 000h		;a288	00		.
	defb 000h		;a289	00		.
	defb 000h		;a28a	00		.
	defb 000h		;a28b	00		.
	defb 000h		;a28c	00		.
	defb 000h		;a28d	00		.
	defb 000h		;a28e	00		.
	defb 000h		;a28f	00		.
	defb 000h		;a290	00		.
	defb 000h		;a291	00		.
	defb 000h		;a292	00		.
	defb 02fh		;a293	2f		/
	defb 02ah		;a294	2a		*
	defb 02dh		;a295	2d		-
	defb 02bh		;a296	2b		+
	defb 00dh		;a297	0d		.
	defb 000h		;a298	00		.
	defb 000h		;a299	00		.
	defb 000h		;a29a	00		.
	defb 000h		;a29b	00		.
	defb 000h		;a29c	00		.
	defb 000h		;a29d	00		.
	defb 000h		;a29e	00		.
	defb 000h		;a29f	00		.
	defb 000h		;a2a0	00		.
	defb 000h		;a2a1	00		.
	defb 000h		;a2a2	00		.
CAPSTAB:
				; = CAPSTAB (src: KEY.ASM:580, BIOS-PP 1273243)
	defb 060h		;a2a3	60		`
	defb 01bh		;a2a4	1b		.
	defb 031h		;a2a5	31		1
	defb 032h		;a2a6	32		2
	defb 033h		;a2a7	33		3
	defb 034h		;a2a8	34		4
	defb 035h		;a2a9	35		5
	defb 036h		;a2aa	36		6
	defb 037h		;a2ab	37		7
	defb 038h		;a2ac	38		8
	defb 039h		;a2ad	39		9
	defb 030h		;a2ae	30		0
	defb 02dh		;a2af	2d		-
	defb 03dh		;a2b0	3d		=
	defb 008h		;a2b1	08		.
	defb 009h		;a2b2	09		.
	defb 051h		;a2b3	51		Q
	defb 057h		;a2b4	57		W
	defb 045h		;a2b5	45		E
	defb 052h		;a2b6	52		R
	defb 054h		;a2b7	54		T
	defb 059h		;a2b8	59		Y
	defb 055h		;a2b9	55		U
	defb 049h		;a2ba	49		I
	defb 04fh		;a2bb	4f		O
	defb 050h		;a2bc	50		P
	defb 05bh		;a2bd	5b		[
	defb 05dh		;a2be	5d		]
	defb 000h		;a2bf	00		.
	defb 041h		;a2c0	41		A
	defb 053h		;a2c1	53		S
	defb 044h		;a2c2	44		D
	defb 046h		;a2c3	46		F
	defb 047h		;a2c4	47		G
	defb 048h		;a2c5	48		H
	defb 04ah		;a2c6	4a		J
	defb 04bh		;a2c7	4b		K
	defb 04ch		;a2c8	4c		L
	defb 03bh		;a2c9	3b		;
	defb 027h		;a2ca	27		'
	defb 00dh		;a2cb	0d		.
	defb 000h		;a2cc	00		.
	defb 05ah		;a2cd	5a		Z
	defb 058h		;a2ce	58		X
	defb 043h		;a2cf	43		C
	defb 056h		;a2d0	56		V
	defb 042h		;a2d1	42		B
	defb 04eh		;a2d2	4e		N
	defb 04dh		;a2d3	4d		M
	defb 02ch		;a2d4	2c		,
	defb 02eh		;a2d5	2e		.
	defb 02fh		;a2d6	2f		/
	defb 000h		;a2d7	00		.
	defb 05ch		;a2d8	5c		\
	defb 000h		;a2d9	00		.
	defb 000h		;a2da	00		.
	defb 020h		;a2db	20		 
	defb 000h		;a2dc	00		.
	defb 000h		;a2dd	00		.
	defb 000h		;a2de	00		.
	defb 000h		;a2df	00		.
	defb 000h		;a2e0	00		.
	defb 000h		;a2e1	00		.
	defb 000h		;a2e2	00		.
	defb 000h		;a2e3	00		.
	defb 000h		;a2e4	00		.
	defb 000h		;a2e5	00		.
	defb 000h		;a2e6	00		.
	defb 000h		;a2e7	00		.
	defb 000h		;a2e8	00		.
	defb 000h		;a2e9	00		.
	defb 000h		;a2ea	00		.
	defb 000h		;a2eb	00		.
	defb 000h		;a2ec	00		.
	defb 02fh		;a2ed	2f		/
	defb 02ah		;a2ee	2a		*
	defb 02dh		;a2ef	2d		-
	defb 02bh		;a2f0	2b		+
	defb 00dh		;a2f1	0d		.
	defb 000h		;a2f2	00		.
	defb 000h		;a2f3	00		.
	defb 000h		;a2f4	00		.
	defb 000h		;a2f5	00		.
	defb 000h		;a2f6	00		.
	defb 000h		;a2f7	00		.
	defb 000h		;a2f8	00		.
	defb 000h		;a2f9	00		.
	defb 000h		;a2fa	00		.
	defb 000h		;a2fb	00		.
	defb 000h		;a2fc	00		.
SHF2TAB:
				; = SHF2TAB (src: KEY.ASM:589, BIOS-PP 1273243)
	defb 07eh		;a2fd	7e		~
	defb 01bh		;a2fe	1b		.
	defb 021h		;a2ff	21		!
	defb 040h		;a300	40		@
	defb 023h		;a301	23		#
	defb 024h		;a302	24		$
	defb 025h		;a303	25		%
	defb 05eh		;a304	5e		^
	defb 026h		;a305	26		&
	defb 02ah		;a306	2a		*
	defb 028h		;a307	28		(
	defb 029h		;a308	29		)
	defb 05fh		;a309	5f		_
	defb 02bh		;a30a	2b		+
	defb 008h		;a30b	08		.
	defb 009h		;a30c	09		.
	defb 071h		;a30d	71		q
	defb 077h		;a30e	77		w
	defb 065h		;a30f	65		e
	defb 072h		;a310	72		r
	defb 074h		;a311	74		t
	defb 079h		;a312	79		y
	defb 075h		;a313	75		u
	defb 069h		;a314	69		i
	defb 06fh		;a315	6f		o
	defb 070h		;a316	70		p
	defb 07bh		;a317	7b		{
	defb 07dh		;a318	7d		}
	defb 000h		;a319	00		.
	defb 061h		;a31a	61		a
	defb 073h		;a31b	73		s
	defb 064h		;a31c	64		d
	defb 066h		;a31d	66		f
	defb 067h		;a31e	67		g
	defb 068h		;a31f	68		h
	defb 06ah		;a320	6a		j
	defb 06bh		;a321	6b		k
	defb 06ch		;a322	6c		l
	defb 03ah		;a323	3a		:
	defb 022h		;a324	22		"
	defb 00dh		;a325	0d		.
	defb 000h		;a326	00		.
	defb 07ah		;a327	7a		z
	defb 078h		;a328	78		x
	defb 063h		;a329	63		c
	defb 076h		;a32a	76		v
	defb 062h		;a32b	62		b
	defb 06eh		;a32c	6e		n
	defb 06dh		;a32d	6d		m
	defb 03ch		;a32e	3c		<
	defb 03eh		;a32f	3e		>
	defb 03fh		;a330	3f		?
	defb 000h		;a331	00		.
	defb 07ch		;a332	7c		|
	defb 000h		;a333	00		.
	defb 000h		;a334	00		.
	defb 020h		;a335	20		 
	defb 000h		;a336	00		.
	defb 000h		;a337	00		.
	defb 000h		;a338	00		.
	defb 000h		;a339	00		.
	defb 000h		;a33a	00		.
	defb 000h		;a33b	00		.
	defb 000h		;a33c	00		.
	defb 000h		;a33d	00		.
	defb 000h		;a33e	00		.
	defb 000h		;a33f	00		.
	defb 000h		;a340	00		.
	defb 000h		;a341	00		.
	defb 000h		;a342	00		.
	defb 000h		;a343	00		.
	defb 000h		;a344	00		.
	defb 000h		;a345	00		.
	defb 000h		;a346	00		.
	defb 02fh		;a347	2f		/
	defb 02ah		;a348	2a		*
	defb 02dh		;a349	2d		-
	defb 02bh		;a34a	2b		+
	defb 00dh		;a34b	0d		.
	defb 000h		;a34c	00		.
	defb 000h		;a34d	00		.
	defb 000h		;a34e	00		.
	defb 000h		;a34f	00		.
	defb 000h		;a350	00		.
	defb 000h		;a351	00		.
	defb 000h		;a352	00		.
	defb 000h		;a353	00		.
	defb 000h		;a354	00		.
	defb 000h		;a355	00		.
	defb 000h		;a356	00		.
BEEP:
				; = BEEP (src: KEY.ASM:637, BIOS-PP 1273243)
	ld a,010h		;a357	3e 10		> .
	out (0feh),a		;a359	d3 fe		. .
	ld b,d			;a35b	42		B
	ld c,e			;a35c	4b		K
BPP:
				; = BPP (src: KEY.ASM:641, BIOS-PP 1273243)
	dec bc			;a35d	0b		.
	ld a,b			;a35e	78		x
	or c			;a35f	b1		.
	jr nz,BPP		;a360	20 fb		  .
	ld a,000h		;a362	3e 00		> .
	out (0feh),a		;a364	d3 fe		. .
	ld b,d			;a366	42		B
	ld c,e			;a367	4b		K
BPP2:
				; = BPP2 (src: KEY.ASM:649, BIOS-PP 1273243)
	dec bc			;a368	0b		.
	ld a,b			;a369	78		x
	or c			;a36a	b1		.
	jr nz,BPP2		;a36b	20 fb		  .
	dec hl			;a36d	2b		+
	ld a,h			;a36e	7c		|
	or l			;a36f	b5		.
	jr nz,BEEP	;a370	20 e5		  .
	ret			;a372	c9		.
;---------------------------------------------------------------------------
; KeyboardInit: program SIO channel A (#19) for the keyboard (register
; sequence of MAN 9.1) and reset the key buffer.
;---------------------------------------------------------------------------
KeyboardInit:
				; = KINIT (src: KEY.ASM:662, BIOS-PP 1273243)
	ld a,000h		;a373	3e 00		> .
	out (019h),a		;a375	d3 19		. .
	ld a,001h		;a377	3e 01		> .
	out (019h),a		;a379	d3 19		. .
	ld a,000h		;a37b	3e 00		> .
	out (019h),a		;a37d	d3 19		. .
	ld a,003h		;a37f	3e 03		> .
	out (019h),a		;a381	d3 19		. .
	ld a,0c1h		;a383	3e c1		> .
	out (019h),a		;a385	d3 19		. .
	ld a,004h		;a387	3e 04		> .
	out (019h),a		;a389	d3 19		. .
	ld a,007h		;a38b	3e 07		> .
	out (019h),a		;a38d	d3 19		. .
	ld a,005h		;a38f	3e 05		> .
	out (019h),a		;a391	d3 19		. .
	ld a,062h		;a393	3e 62		> b
	out (019h),a		;a395	d3 19		. .
	ret			;a397	c9		.

; BLOCK 'DataA398' (start 0xa398 end 0xb5b3)
DataA398:
	defb 02ch		;a398	2c		,
	defb 0bah		;a399	ba		.
	defb 061h		;a39a	61		a
	defb 0bah		;a39b	ba		.
	defb 096h		;a39c	96		.
	defb 0bah		;a39d	ba		.
	defb 0cbh		;a39e	cb		.
	defb 0bah		;a39f	ba		.
	defb 000h		;a3a0	00		.
	defb 0bbh		;a3a1	bb		.
	defb 03eh		;a3a2	3e		>
	defb 0bbh		;a3a3	bb		.
	defb 079h		;a3a4	79		y
	defb 0bbh		;a3a5	bb		.
	defb 0b0h		;a3a6	b0		.
	defb 0bbh		;a3a7	bb		.
	defb 0e5h		;a3a8	e5		.
	defb 0bbh		;a3a9	bb		.
	defb 035h		;a3aa	35		5
	defb 0bch		;a3ab	bc		.
	defb 085h		;a3ac	85		.
	defb 0bch		;a3ad	bc		.
	defb 0aeh		;a3ae	ae		.
	defb 0bch		;a3af	bc		.
	defb 0d7h		;a3b0	d7		.
	defb 0bch		;a3b1	bc		.
	defb 016h		;a3b2	16		.
	defb 0bdh		;a3b3	bd		.
	defb 055h		;a3b4	55		U
	defb 0bdh		;a3b5	bd		.
	defb 08ah		;a3b6	8a		.
	defb 0bdh		;a3b7	bd		.
	defb 0dah		;a3b8	da		.
	defb 0bdh		;a3b9	bd		.
	defb 02ah		;a3ba	2a		*
	defb 0beh		;a3bb	be		.
	defb 057h		;a3bc	57		W
	defb 0beh		;a3bd	be		.
	defb 08ah		;a3be	8a		.
	defb 0beh		;a3bf	be		.
	defb 0bdh		;a3c0	bd		.
	defb 0beh		;a3c1	be		.
	defb 0f0h		;a3c2	f0		.
	defb 0beh		;a3c3	be		.
	defb 003h		;a3c4	03		.
	defb 007h		;a3c5	07		.
	defb 04ch		;a3c6	4c		L
	defb 061h		;a3c7	61		a
	defb 06eh		;a3c8	6e		n
	defb 067h		;a3c9	67		g
	defb 075h		;a3ca	75		u
	defb 061h		;a3cb	61		a
	defb 067h		;a3cc	67		g
	defb 065h		;a3cd	65		e
	defb 020h		;a3ce	20		 
	defb 028h		;a3cf	28		(
	defb 09fh		;a3d0	9f		.
	defb 0a7h		;a3d1	a7		.
	defb 0ebh		;a3d2	eb		.
	defb 0aah		;a3d3	aa		.
	defb 029h		;a3d4	29		)
	defb 020h		;a3d5	20		 
	defb 020h		;a3d6	20		 
	defb 020h		;a3d7	20		 
	defb 020h		;a3d8	20		 
	defb 020h		;a3d9	20		 
	defb 020h		;a3da	20		 
	defb 020h		;a3db	20		 
	defb 020h		;a3dc	20		 
	defb 020h		;a3dd	20		 
	defb 020h		;a3de	20		 
	defb 020h		;a3df	20		 
	defb 020h		;a3e0	20		 
	defb 03ah		;a3e1	3a		:
	defb 020h		;a3e2	20		 
	defb 000h		;a3e3	00		.
	defb 00eh		;a3e4	0e		.
	defb 004h		;a3e5	04		.
	defb 004h		;a3e6	04		.
	defb 045h		;a3e7	45		E
	defb 06eh		;a3e8	6e		n
	defb 067h		;a3e9	67		g
	defb 06ch		;a3ea	6c		l
	defb 069h		;a3eb	69		i
	defb 073h		;a3ec	73		s
	defb 068h		;a3ed	68		h
	defb 020h		;a3ee	20		 
	defb 000h		;a3ef	00		.
	defb 052h		;a3f0	52		R
	defb 075h		;a3f1	75		u
	defb 073h		;a3f2	73		s
	defb 073h		;a3f3	73		s
	defb 069h		;a3f4	69		i
	defb 061h		;a3f5	61		a
	defb 06eh		;a3f6	6e		n
	defb 020h		;a3f7	20		 
	defb 000h		;a3f8	00		.
	defb 003h		;a3f9	03		.
	defb 008h		;a3fa	08		.
	defb 04dh		;a3fb	4d		M
	defb 065h		;a3fc	65		e
	defb 06dh		;a3fd	6d		m
	defb 06fh		;a3fe	6f		o
	defb 072h		;a3ff	72		r
la400h:
	defb 079h		;a400	79		y
	defb 020h		;a401	20		 
	defb 054h		;a402	54		T
	defb 065h		;a403	65		e
	defb 073h		;a404	73		s
	defb 074h		;a405	74		t
	defb 020h		;a406	20		 
	defb 020h		;a407	20		 
	defb 020h		;a408	20		 
	defb 020h		;a409	20		 
	defb 020h		;a40a	20		 
	defb 020h		;a40b	20		 
	defb 020h		;a40c	20		 
	defb 020h		;a40d	20		 
	defb 020h		;a40e	20		 
	defb 020h		;a40f	20		 
	defb 020h		;a410	20		 
	defb 020h		;a411	20		 
	defb 020h		;a412	20		 
	defb 020h		;a413	20		 
	defb 020h		;a414	20		 
	defb 020h		;a415	20		 
	defb 03ah		;a416	3a		:
	defb 020h		;a417	20		 
	defb 000h		;a418	00		.
	defb 00eh		;a419	0e		.
	defb 080h		;a41a	80		.
	defb 080h		;a41b	80		.
	defb 044h		;a41c	44		D
	defb 069h		;a41d	69		i
	defb 073h		;a41e	73		s
	defb 061h		;a41f	61		a
	defb 062h		;a420	62		b
	defb 06ch		;a421	6c		l
	defb 065h		;a422	65		e
	defb 064h		;a423	64		d
	defb 000h		;a424	00		.
	defb 045h		;a425	45		E
	defb 06eh		;a426	6e		n
	defb 061h		;a427	61		a
	defb 062h		;a428	62		b
	defb 06ch		;a429	6c		l
	defb 065h		;a42a	65		e
	defb 064h		;a42b	64		d
	defb 020h		;a42c	20		 
	defb 000h		;a42d	00		.
	defb 003h		;a42e	03		.
	defb 009h		;a42f	09		.
	defb 053h		;a430	53		S
	defb 061h		;a431	61		a
	defb 066h		;a432	66		f
	defb 065h		;a433	65		e
	defb 020h		;a434	20		 
	defb 052h		;a435	52		R
	defb 041h		;a436	41		A
	defb 04dh		;a437	4d		M
	defb 02dh		;a438	2d		-
	defb 064h		;a439	64		d
	defb 069h		;a43a	69		i
	defb 073h		;a43b	73		s
	defb 06bh		;a43c	6b		k
	defb 073h		;a43d	73		s
	defb 020h		;a43e	20		 
	defb 020h		;a43f	20		 
	defb 020h		;a440	20		 
	defb 020h		;a441	20		 
	defb 020h		;a442	20		 
	defb 020h		;a443	20		 
	defb 020h		;a444	20		 
	defb 020h		;a445	20		 
	defb 020h		;a446	20		 
	defb 020h		;a447	20		 
	defb 020h		;a448	20		 
	defb 020h		;a449	20		 
	defb 020h		;a44a	20		 
	defb 03ah		;a44b	3a		:
	defb 020h		;a44c	20		 
	defb 000h		;a44d	00		.
	defb 00eh		;a44e	0e		.
	defb 040h		;a44f	40		@
	defb 040h		;a450	40		@
	defb 044h		;a451	44		D
	defb 069h		;a452	69		i
	defb 073h		;a453	73		s
	defb 061h		;a454	61		a
	defb 062h		;a455	62		b
	defb 06ch		;a456	6c		l
	defb 065h		;a457	65		e
	defb 064h		;a458	64		d
	defb 000h		;a459	00		.
	defb 045h		;a45a	45		E
	defb 06eh		;a45b	6e		n
	defb 061h		;a45c	61		a
	defb 062h		;a45d	62		b
	defb 06ch		;a45e	6c		l
	defb 065h		;a45f	65		e
	defb 064h		;a460	64		d
	defb 020h		;a461	20		 
	defb 000h		;a462	00		.
	defb 003h		;a463	03		.
	defb 00ah		;a464	0a		.
	defb 055h		;a465	55		U
	defb 070h		;a466	70		p
	defb 064h		;a467	64		d
	defb 061h		;a468	61		a
	defb 074h		;a469	74		t
	defb 065h		;a46a	65		e
	defb 020h		;a46b	20		 
	defb 042h		;a46c	42		B
	defb 049h		;a46d	49		I
	defb 04fh		;a46e	4f		O
	defb 053h		;a46f	53		S
	defb 020h		;a470	20		 
	defb 020h		;a471	20		 
	defb 020h		;a472	20		 
	defb 020h		;a473	20		 
	defb 020h		;a474	20		 
	defb 020h		;a475	20		 
	defb 020h		;a476	20		 
	defb 020h		;a477	20		 
	defb 020h		;a478	20		 
	defb 020h		;a479	20		 
	defb 020h		;a47a	20		 
	defb 020h		;a47b	20		 
	defb 020h		;a47c	20		 
	defb 020h		;a47d	20		 
	defb 020h		;a47e	20		 
	defb 020h		;a47f	20		 
	defb 03ah		;a480	3a		:
	defb 020h		;a481	20		 
	defb 000h		;a482	00		.
	defb 00eh		;a483	0e		.
	defb 020h		;a484	20		 
	defb 020h		;a485	20		 
	defb 044h		;a486	44		D
	defb 069h		;a487	69		i
	defb 073h		;a488	73		s
	defb 061h		;a489	61		a
	defb 062h		;a48a	62		b
	defb 06ch		;a48b	6c		l
	defb 065h		;a48c	65		e
	defb 064h		;a48d	64		d
	defb 000h		;a48e	00		.
	defb 045h		;a48f	45		E
	defb 06eh		;a490	6e		n
	defb 061h		;a491	61		a
	defb 062h		;a492	62		b
	defb 06ch		;a493	6c		l
	defb 065h		;a494	65		e
	defb 064h		;a495	64		d
	defb 020h		;a496	20		 
	defb 000h		;a497	00		.
	defb 003h		;a498	03		.
	defb 00bh		;a499	0b		.
	defb 053h		;a49a	53		S
	defb 074h		;a49b	74		t
	defb 061h		;a49c	61		a
	defb 072h		;a49d	72		r
	defb 074h		;a49e	74		t
	defb 020h		;a49f	20		 
	defb 044h		;a4a0	44		D
	defb 065h		;a4a1	65		e
	defb 06ch		;a4a2	6c		l
	defb 061h		;a4a3	61		a
	defb 079h		;a4a4	79		y
	defb 020h		;a4a5	20		 
	defb 020h		;a4a6	20		 
	defb 020h		;a4a7	20		 
	defb 020h		;a4a8	20		 
	defb 020h		;a4a9	20		 
	defb 020h		;a4aa	20		 
	defb 020h		;a4ab	20		 
	defb 020h		;a4ac	20		 
	defb 020h		;a4ad	20		 
	defb 020h		;a4ae	20		 
	defb 020h		;a4af	20		 
	defb 020h		;a4b0	20		 
	defb 020h		;a4b1	20		 
	defb 020h		;a4b2	20		 
	defb 020h		;a4b3	20		 
	defb 020h		;a4b4	20		 
	defb 03ah		;a4b5	3a		:
	defb 020h		;a4b6	20		 
	defb 000h		;a4b7	00		.
	defb 00eh		;a4b8	0e		.
	defb 018h		;a4b9	18		.
	defb 010h		;a4ba	10		.
	defb 044h		;a4bb	44		D
	defb 069h		;a4bc	69		i
	defb 073h		;a4bd	73		s
	defb 061h		;a4be	61		a
	defb 062h		;a4bf	62		b
	defb 06ch		;a4c0	6c		l
	defb 065h		;a4c1	65		e
	defb 064h		;a4c2	64		d
	defb 000h		;a4c3	00		.
	defb 04eh		;a4c4	4e		N
	defb 06fh		;a4c5	6f		o
	defb 072h		;a4c6	72		r
	defb 06dh		;a4c7	6d		m
	defb 061h		;a4c8	61		a
	defb 06ch		;a4c9	6c		l
	defb 020h		;a4ca	20		 
	defb 020h		;a4cb	20		 
	defb 000h		;a4cc	00		.
	defb 045h		;a4cd	45		E
	defb 06eh		;a4ce	6e		n
	defb 061h		;a4cf	61		a
	defb 062h		;a4d0	62		b
	defb 06ch		;a4d1	6c		l
	defb 065h		;a4d2	65		e
	defb 064h		;a4d3	64		d
	defb 020h		;a4d4	20		 
	defb 000h		;a4d5	00		.
	defb 003h		;a4d6	03		.
	defb 00ch		;a4d7	0c		.
	defb 054h		;a4d8	54		T
	defb 079h		;a4d9	79		y
	defb 070h		;a4da	70		p
	defb 065h		;a4db	65		e
	defb 06dh		;a4dc	6d		m
	defb 061h		;a4dd	61		a
	defb 074h		;a4de	74		t
	defb 069h		;a4df	69		i
	defb 063h		;a4e0	63		c
	defb 020h		;a4e1	20		 
	defb 052h		;a4e2	52		R
	defb 061h		;a4e3	61		a
	defb 074h		;a4e4	74		t
	defb 065h		;a4e5	65		e
	defb 020h		;a4e6	20		 
	defb 028h		;a4e7	28		(
	defb 043h		;a4e8	43		C
	defb 068h		;a4e9	68		h
	defb 061h		;a4ea	61		a
	defb 072h		;a4eb	72		r
	defb 073h		;a4ec	73		s
	defb 02fh		;a4ed	2f		/
	defb 053h		;a4ee	53		S
	defb 065h		;a4ef	65		e
	defb 063h		;a4f0	63		c
	defb 029h		;a4f1	29		)
	defb 020h		;a4f2	20		 
	defb 03ah		;a4f3	3a		:
	defb 020h		;a4f4	20		 
	defb 000h		;a4f5	00		.
	defb 00fh		;a4f6	0f		.
	defb 007h		;a4f7	07		.
	defb 007h		;a4f8	07		.
	defb 036h		;a4f9	36		6
	defb 020h		;a4fa	20		 
	defb 000h		;a4fb	00		.
	defb 038h		;a4fc	38		8
	defb 020h		;a4fd	20		 
	defb 000h		;a4fe	00		.
	defb 031h		;a4ff	31		1
	defb 030h		;a500	30		0
	defb 000h		;a501	00		.
	defb 031h		;a502	31		1
	defb 032h		;a503	32		2
	defb 000h		;a504	00		.
	defb 031h		;a505	31		1
	defb 035h		;a506	35		5
	defb 000h		;a507	00		.
	defb 032h		;a508	32		2
	defb 030h		;a509	30		0
	defb 000h		;a50a	00		.
	defb 032h		;a50b	32		2
	defb 034h		;a50c	34		4
	defb 000h		;a50d	00		.
	defb 033h		;a50e	33		3
	defb 030h		;a50f	30		0
	defb 000h		;a510	00		.
	defb 003h		;a511	03		.
	defb 00dh		;a512	0d		.
	defb 054h		;a513	54		T
	defb 079h		;a514	79		y
	defb 070h		;a515	70		p
	defb 065h		;a516	65		e
	defb 06dh		;a517	6d		m
	defb 061h		;a518	61		a
	defb 074h		;a519	74		t
	defb 069h		;a51a	69		i
	defb 063h		;a51b	63		c
	defb 020h		;a51c	20		 
	defb 044h		;a51d	44		D
	defb 065h		;a51e	65		e
	defb 06ch		;a51f	6c		l
	defb 061h		;a520	61		a
	defb 079h		;a521	79		y
	defb 020h		;a522	20		 
	defb 028h		;a523	28		(
	defb 04dh		;a524	4d		M
	defb 073h		;a525	73		s
	defb 065h		;a526	65		e
	defb 063h		;a527	63		c
	defb 029h		;a528	29		)
	defb 020h		;a529	20		 
	defb 020h		;a52a	20		 
	defb 020h		;a52b	20		 
	defb 020h		;a52c	20		 
	defb 020h		;a52d	20		 
	defb 03ah		;a52e	3a		:
	defb 020h		;a52f	20		 
	defb 000h		;a530	00		.
	defb 00fh		;a531	0f		.
	defb 060h		;a532	60		`
	defb 060h		;a533	60		`
	defb 032h		;a534	32		2
	defb 035h		;a535	35		5
	defb 030h		;a536	30		0
	defb 020h		;a537	20		 
	defb 000h		;a538	00		.
	defb 035h		;a539	35		5
	defb 030h		;a53a	30		0
	defb 030h		;a53b	30		0
	defb 020h		;a53c	20		 
	defb 000h		;a53d	00		.
	defb 037h		;a53e	37		7
	defb 035h		;a53f	35		5
	defb 030h		;a540	30		0
	defb 020h		;a541	20		 
	defb 000h		;a542	00		.
	defb 031h		;a543	31		1
	defb 030h		;a544	30		0
	defb 030h		;a545	30		0
	defb 030h		;a546	30		0
	defb 000h		;a547	00		.
	defb 003h		;a548	03		.
	defb 00eh		;a549	0e		.
	defb 052h		;a54a	52		R
	defb 065h		;a54b	65		e
	defb 062h		;a54c	62		b
	defb 06fh		;a54d	6f		o
	defb 06fh		;a54e	6f		o
	defb 074h		;a54f	74		t
	defb 020h		;a550	20		 
	defb 06dh		;a551	6d		m
	defb 065h		;a552	65		e
	defb 073h		;a553	73		s
	defb 073h		;a554	73		s
	defb 061h		;a555	61		a
	defb 067h		;a556	67		g
	defb 065h		;a557	65		e
	defb 020h		;a558	20		 
	defb 020h		;a559	20		 
	defb 020h		;a55a	20		 
	defb 020h		;a55b	20		 
	defb 020h		;a55c	20		 
	defb 020h		;a55d	20		 
	defb 020h		;a55e	20		 
	defb 020h		;a55f	20		 
	defb 020h		;a560	20		 
	defb 020h		;a561	20		 
	defb 020h		;a562	20		 
	defb 020h		;a563	20		 
	defb 020h		;a564	20		 
	defb 03ah		;a565	3a		:
	defb 020h		;a566	20		 
	defb 000h		;a567	00		.
	defb 01dh		;a568	1d		.
	defb 002h		;a569	02		.
	defb 002h		;a56a	02		.
	defb 044h		;a56b	44		D
	defb 069h		;a56c	69		i
	defb 073h		;a56d	73		s
	defb 061h		;a56e	61		a
	defb 062h		;a56f	62		b
	defb 06ch		;a570	6c		l
	defb 065h		;a571	65		e
	defb 064h		;a572	64		d
	defb 000h		;a573	00		.
	defb 045h		;a574	45		E
	defb 06eh		;a575	6e		n
	defb 061h		;a576	61		a
	defb 062h		;a577	62		b
	defb 06ch		;a578	6c		l
	defb 065h		;a579	65		e
	defb 064h		;a57a	64		d
	defb 020h		;a57b	20		 
	defb 000h		;a57c	00		.
	defb 003h		;a57d	03		.
	defb 00fh		;a57e	0f		.
	defb 053h		;a57f	53		S
	defb 079h		;a580	79		y
	defb 073h		;a581	73		s
	defb 074h		;a582	74		t
	defb 065h		;a583	65		e
	defb 06dh		;a584	6d		m
	defb 020h		;a585	20		 
	defb 044h		;a586	44		D
	defb 069h		;a587	69		i
	defb 073h		;a588	73		s
	defb 06bh		;a589	6b		k
	defb 020h		;a58a	20		 
	defb 020h		;a58b	20		 
	defb 020h		;a58c	20		 
	defb 020h		;a58d	20		 
	defb 020h		;a58e	20		 
	defb 020h		;a58f	20		 
	defb 020h		;a590	20		 
	defb 020h		;a591	20		 
	defb 020h		;a592	20		 
	defb 020h		;a593	20		 
	defb 020h		;a594	20		 
	defb 020h		;a595	20		 
	defb 020h		;a596	20		 
	defb 020h		;a597	20		 
	defb 020h		;a598	20		 
	defb 020h		;a599	20		 
	defb 03ah		;a59a	3a		:
	defb 020h		;a59b	20		 
	defb 000h		;a59c	00		.
	defb 010h		;a59d	10		.
	defb 007h		;a59e	07		.
	defb 004h		;a59f	04		.
	defb 031h		;a5a0	31		1
	defb 02dh		;a5a1	2d		-
	defb 073h		;a5a2	73		s
	defb 074h		;a5a3	74		t
	defb 020h		;a5a4	20		 
	defb 046h		;a5a5	46		F
	defb 044h		;a5a6	44		D
	defb 044h		;a5a7	44		D
	defb 000h		;a5a8	00		.
	defb 032h		;a5a9	32		2
	defb 02dh		;a5aa	2d		-
	defb 06eh		;a5ab	6e		n
	defb 064h		;a5ac	64		d
	defb 020h		;a5ad	20		 
	defb 046h		;a5ae	46		F
	defb 044h		;a5af	44		D
	defb 044h		;a5b0	44		D
	defb 000h		;a5b1	00		.
	defb 031h		;a5b2	31		1
	defb 02dh		;a5b3	2d		-
	defb 073h		;a5b4	73		s
	defb 074h		;a5b5	74		t
	defb 020h		;a5b6	20		 
	defb 049h		;a5b7	49		I
	defb 044h		;a5b8	44		D
	defb 045h		;a5b9	45		E
	defb 000h		;a5ba	00		.
	defb 032h		;a5bb	32		2
	defb 02dh		;a5bc	2d		-
	defb 06eh		;a5bd	6e		n
	defb 064h		;a5be	64		d
	defb 020h		;a5bf	20		 
	defb 049h		;a5c0	49		I
	defb 044h		;a5c1	44		D
	defb 045h		;a5c2	45		E
	defb 000h		;a5c3	00		.
	defb 052h		;a5c4	52		R
	defb 041h		;a5c5	41		A
	defb 04dh		;a5c6	4d		M
	defb 02dh		;a5c7	2d		-
	defb 044h		;a5c8	44		D
	defb 049h		;a5c9	49		I
	defb 053h		;a5ca	53		S
	defb 04bh		;a5cb	4b		K
	defb 000h		;a5cc	00		.
	defb 003h		;a5cd	03		.
	defb 010h		;a5ce	10		.
	defb 041h		;a5cf	41		A
	defb 06ch		;a5d0	6c		l
	defb 074h		;a5d1	74		t
	defb 02eh		;a5d2	2e		.
	defb 020h		;a5d3	20		 
	defb 053h		;a5d4	53		S
	defb 079h		;a5d5	79		y
	defb 073h		;a5d6	73		s
	defb 074h		;a5d7	74		t
	defb 065h		;a5d8	65		e
	defb 06dh		;a5d9	6d		m
	defb 020h		;a5da	20		 
	defb 044h		;a5db	44		D
	defb 069h		;a5dc	69		i
	defb 073h		;a5dd	73		s
	defb 06bh		;a5de	6b		k
	defb 020h		;a5df	20		 
	defb 020h		;a5e0	20		 
	defb 020h		;a5e1	20		 
	defb 020h		;a5e2	20		 
	defb 020h		;a5e3	20		 
	defb 020h		;a5e4	20		 
	defb 020h		;a5e5	20		 
	defb 020h		;a5e6	20		 
	defb 020h		;a5e7	20		 
	defb 020h		;a5e8	20		 
	defb 020h		;a5e9	20		 
	defb 03ah		;a5ea	3a		:
	defb 020h		;a5eb	20		 
	defb 000h		;a5ec	00		.
	defb 010h		;a5ed	10		.
	defb 070h		;a5ee	70		p
	defb 040h		;a5ef	40		@
	defb 031h		;a5f0	31		1
	defb 02dh		;a5f1	2d		-
	defb 073h		;a5f2	73		s
	defb 074h		;a5f3	74		t
	defb 020h		;a5f4	20		 
	defb 046h		;a5f5	46		F
	defb 044h		;a5f6	44		D
	defb 044h		;a5f7	44		D
	defb 000h		;a5f8	00		.
	defb 032h		;a5f9	32		2
	defb 02dh		;a5fa	2d		-
	defb 06eh		;a5fb	6e		n
	defb 064h		;a5fc	64		d
	defb 020h		;a5fd	20		 
	defb 046h		;a5fe	46		F
	defb 044h		;a5ff	44		D
	defb 044h		;a600	44		D
	defb 000h		;a601	00		.
	defb 031h		;a602	31		1
	defb 02dh		;a603	2d		-
	defb 073h		;a604	73		s
	defb 074h		;a605	74		t
	defb 020h		;a606	20		 
	defb 049h		;a607	49		I
	defb 044h		;a608	44		D
	defb 045h		;a609	45		E
	defb 000h		;a60a	00		.
	defb 032h		;a60b	32		2
	defb 02dh		;a60c	2d		-
	defb 06eh		;a60d	6e		n
	defb 064h		;a60e	64		d
	defb 020h		;a60f	20		 
	defb 049h		;a610	49		I
	defb 044h		;a611	44		D
	defb 045h		;a612	45		E
	defb 000h		;a613	00		.
	defb 052h		;a614	52		R
	defb 041h		;a615	41		A
	defb 04dh		;a616	4d		M
	defb 02dh		;a617	2d		-
	defb 044h		;a618	44		D
	defb 049h		;a619	49		I
	defb 053h		;a61a	53		S
	defb 04bh		;a61b	4b		K
	defb 000h		;a61c	00		.
	defb 003h		;a61d	03		.
	defb 011h		;a61e	11		.
	defb 046h		;a61f	46		F
	defb 044h		;a620	44		D
	defb 044h		;a621	44		D
	defb 020h		;a622	20		 
	defb 046h		;a623	46		F
	defb 069h		;a624	69		i
	defb 072h		;a625	72		r
	defb 073h		;a626	73		s
	defb 074h		;a627	74		t
	defb 020h		;a628	20		 
	defb 020h		;a629	20		 
	defb 020h		;a62a	20		 
	defb 020h		;a62b	20		 
	defb 020h		;a62c	20		 
	defb 020h		;a62d	20		 
	defb 020h		;a62e	20		 
	defb 020h		;a62f	20		 
	defb 020h		;a630	20		 
	defb 020h		;a631	20		 
	defb 020h		;a632	20		 
	defb 020h		;a633	20		 
	defb 020h		;a634	20		 
	defb 020h		;a635	20		 
	defb 020h		;a636	20		 
	defb 020h		;a637	20		 
	defb 020h		;a638	20		 
	defb 020h		;a639	20		 
	defb 03ah		;a63a	3a		:
	defb 020h		;a63b	20		 
	defb 000h		;a63c	00		.
	defb 011h		;a63d	11		.
	defb 003h		;a63e	03		.
	defb 000h		;a63f	00		.
	defb 041h		;a640	41		A
	defb 075h		;a641	75		u
	defb 074h		;a642	74		t
	defb 06fh		;a643	6f		o
	defb 020h		;a644	20		 
	defb 000h		;a645	00		.
	defb 003h		;a646	03		.
	defb 012h		;a647	12		.
	defb 046h		;a648	46		F
	defb 044h		;a649	44		D
	defb 044h		;a64a	44		D
	defb 020h		;a64b	20		 
	defb 053h		;a64c	53		S
	defb 065h		;a64d	65		e
	defb 063h		;a64e	63		c
	defb 06fh		;a64f	6f		o
	defb 06eh		;a650	6e		n
	defb 064h		;a651	64		d
	defb 020h		;a652	20		 
	defb 020h		;a653	20		 
	defb 020h		;a654	20		 
	defb 020h		;a655	20		 
	defb 020h		;a656	20		 
	defb 020h		;a657	20		 
	defb 020h		;a658	20		 
	defb 020h		;a659	20		 
	defb 020h		;a65a	20		 
	defb 020h		;a65b	20		 
	defb 020h		;a65c	20		 
	defb 020h		;a65d	20		 
	defb 020h		;a65e	20		 
	defb 020h		;a65f	20		 
	defb 020h		;a660	20		 
	defb 020h		;a661	20		 
	defb 020h		;a662	20		 
	defb 03ah		;a663	3a		:
	defb 020h		;a664	20		 
	defb 000h		;a665	00		.
	defb 011h		;a666	11		.
	defb 00ch		;a667	0c		.
	defb 000h		;a668	00		.
	defb 041h		;a669	41		A
	defb 075h		;a66a	75		u
	defb 074h		;a66b	74		t
	defb 06fh		;a66c	6f		o
	defb 020h		;a66d	20		 
	defb 000h		;a66e	00		.
	defb 003h		;a66f	03		.
	defb 013h		;a670	13		.
	defb 049h		;a671	49		I
	defb 044h		;a672	44		D
	defb 045h		;a673	45		E
	defb 020h		;a674	20		 
	defb 04dh		;a675	4d		M
	defb 061h		;a676	61		a
	defb 073h		;a677	73		s
	defb 074h		;a678	74		t
	defb 065h		;a679	65		e
	defb 072h		;a67a	72		r
	defb 020h		;a67b	20		 
	defb 020h		;a67c	20		 
	defb 020h		;a67d	20		 
	defb 020h		;a67e	20		 
	defb 020h		;a67f	20		 
	defb 020h		;a680	20		 
	defb 020h		;a681	20		 
	defb 020h		;a682	20		 
	defb 020h		;a683	20		 
	defb 020h		;a684	20		 
	defb 020h		;a685	20		 
	defb 020h		;a686	20		 
	defb 020h		;a687	20		 
	defb 020h		;a688	20		 
	defb 020h		;a689	20		 
	defb 020h		;a68a	20		 
	defb 020h		;a68b	20		 
	defb 03ah		;a68c	3a		:
	defb 020h		;a68d	20		 
	defb 000h		;a68e	00		.
	defb 011h		;a68f	11		.
	defb 030h		;a690	30		0
	defb 030h		;a691	30		0
	defb 041h		;a692	41		A
	defb 075h		;a693	75		u
	defb 074h		;a694	74		t
	defb 06fh		;a695	6f		o
	defb 020h		;a696	20		 
	defb 020h		;a697	20		 
	defb 000h		;a698	00		.
	defb 053h		;a699	53		S
	defb 065h		;a69a	65		e
	defb 074h		;a69b	74		t
	defb 075h		;a69c	75		u
	defb 070h		;a69d	70		p
	defb 020h		;a69e	20		 
	defb 000h		;a69f	00		.
	defb 043h		;a6a0	43		C
	defb 044h		;a6a1	44		D
	defb 02dh		;a6a2	2d		-
	defb 052h		;a6a3	52		R
	defb 04fh		;a6a4	4f		O
	defb 04dh		;a6a5	4d		M
	defb 000h		;a6a6	00		.
	defb 02dh		;a6a7	2d		-
	defb 02dh		;a6a8	2d		-
	defb 02dh		;a6a9	2d		-
	defb 02dh		;a6aa	2d		-
	defb 02dh		;a6ab	2d		-
	defb 02dh		;a6ac	2d		-
	defb 000h		;a6ad	00		.
	defb 003h		;a6ae	03		.
	defb 014h		;a6af	14		.
	defb 049h		;a6b0	49		I
	defb 044h		;a6b1	44		D
	defb 045h		;a6b2	45		E
	defb 020h		;a6b3	20		 
	defb 053h		;a6b4	53		S
	defb 06ch		;a6b5	6c		l
	defb 061h		;a6b6	61		a
	defb 076h		;a6b7	76		v
	defb 065h		;a6b8	65		e
	defb 020h		;a6b9	20		 
	defb 020h		;a6ba	20		 
	defb 020h		;a6bb	20		 
	defb 020h		;a6bc	20		 
	defb 020h		;a6bd	20		 
	defb 020h		;a6be	20		 
	defb 020h		;a6bf	20		 
	defb 020h		;a6c0	20		 
	defb 020h		;a6c1	20		 
	defb 020h		;a6c2	20		 
	defb 020h		;a6c3	20		 
	defb 020h		;a6c4	20		 
	defb 020h		;a6c5	20		 
	defb 020h		;a6c6	20		 
	defb 020h		;a6c7	20		 
	defb 020h		;a6c8	20		 
	defb 020h		;a6c9	20		 
	defb 020h		;a6ca	20		 
	defb 03ah		;a6cb	3a		:
	defb 020h		;a6cc	20		 
	defb 000h		;a6cd	00		.
	defb 011h		;a6ce	11		.
	defb 0c0h		;a6cf	c0		.
	defb 0c0h		;a6d0	c0		.
	defb 041h		;a6d1	41		A
	defb 075h		;a6d2	75		u
	defb 074h		;a6d3	74		t
	defb 06fh		;a6d4	6f		o
	defb 020h		;a6d5	20		 
	defb 020h		;a6d6	20		 
	defb 000h		;a6d7	00		.
	defb 053h		;a6d8	53		S
	defb 065h		;a6d9	65		e
	defb 074h		;a6da	74		t
	defb 075h		;a6db	75		u
	defb 070h		;a6dc	70		p
	defb 020h		;a6dd	20		 
	defb 000h		;a6de	00		.
	defb 043h		;a6df	43		C
	defb 044h		;a6e0	44		D
	defb 02dh		;a6e1	2d		-
	defb 052h		;a6e2	52		R
	defb 04fh		;a6e3	4f		O
	defb 04dh		;a6e4	4d		M
	defb 000h		;a6e5	00		.
	defb 02dh		;a6e6	2d		-
	defb 02dh		;a6e7	2d		-
	defb 02dh		;a6e8	2d		-
	defb 02dh		;a6e9	2d		-
	defb 02dh		;a6ea	2d		-
	defb 02dh		;a6eb	2d		-
	defb 000h		;a6ec	00		.
	defb 003h		;a6ed	03		.
	defb 015h		;a6ee	15		.
	defb 048h		;a6ef	48		H
	defb 044h		;a6f0	44		D
	defb 044h		;a6f1	44		D
	defb 020h		;a6f2	20		 
	defb 057h		;a6f3	57		W
	defb 072h		;a6f4	72		r
	defb 069h		;a6f5	69		i
	defb 074h		;a6f6	74		t
	defb 065h		;a6f7	65		e
	defb 020h		;a6f8	20		 
	defb 070h		;a6f9	70		p
	defb 072h		;a6fa	72		r
	defb 06fh		;a6fb	6f		o
	defb 074h		;a6fc	74		t
	defb 065h		;a6fd	65		e
	defb 063h		;a6fe	63		c
	defb 074h		;a6ff	74		t
	defb 020h		;a700	20		 
	defb 020h		;a701	20		 
	defb 020h		;a702	20		 
	defb 020h		;a703	20		 
	defb 020h		;a704	20		 
	defb 020h		;a705	20		 
	defb 020h		;a706	20		 
	defb 020h		;a707	20		 
	defb 020h		;a708	20		 
	defb 020h		;a709	20		 
	defb 03ah		;a70a	3a		:
	defb 020h		;a70b	20		 
	defb 000h		;a70c	00		.
	defb 01dh		;a70d	1d		.
	defb 001h		;a70e	01		.
	defb 001h		;a70f	01		.
	defb 044h		;a710	44		D
	defb 069h		;a711	69		i
	defb 073h		;a712	73		s
	defb 061h		;a713	61		a
	defb 062h		;a714	62		b
	defb 06ch		;a715	6c		l
	defb 065h		;a716	65		e
	defb 064h		;a717	64		d
	defb 000h		;a718	00		.
	defb 045h		;a719	45		E
	defb 06eh		;a71a	6e		n
	defb 061h		;a71b	61		a
	defb 062h		;a71c	62		b
	defb 06ch		;a71d	6c		l
	defb 065h		;a71e	65		e
	defb 064h		;a71f	64		d
	defb 020h		;a720	20		 
	defb 000h		;a721	00		.
	defb 003h		;a722	03		.
	defb 016h		;a723	16		.
	defb 059h		;a724	59		Y
	defb 02dh		;a725	2d		-
	defb 053h		;a726	53		S
	defb 063h		;a727	63		c
	defb 072h		;a728	72		r
	defb 065h		;a729	65		e
	defb 065h		;a72a	65		e
	defb 06eh		;a72b	6e		n
	defb 020h		;a72c	20		 
	defb 070h		;a72d	70		p
	defb 06fh		;a72e	6f		o
	defb 073h		;a72f	73		s
	defb 069h		;a730	69		i
	defb 074h		;a731	74		t
	defb 069h		;a732	69		i
	defb 06fh		;a733	6f		o
	defb 06eh		;a734	6e		n
	defb 020h		;a735	20		 
	defb 020h		;a736	20		 
	defb 020h		;a737	20		 
	defb 020h		;a738	20		 
	defb 020h		;a739	20		 
	defb 020h		;a73a	20		 
	defb 020h		;a73b	20		 
	defb 020h		;a73c	20		 
	defb 020h		;a73d	20		 
	defb 020h		;a73e	20		 
	defb 03ah		;a73f	3a		:
	defb 020h		;a740	20		 
	defb 000h		;a741	00		.
	defb 01fh		;a742	1f		.
	defb 0f0h		;a743	f0		.
	defb 0e0h		;a744	e0		.
	defb 02dh		;a745	2d		-
	defb 037h		;a746	37		7
	defb 000h		;a747	00		.
	defb 02dh		;a748	2d		-
	defb 036h		;a749	36		6
	defb 000h		;a74a	00		.
	defb 02dh		;a74b	2d		-
	defb 035h		;a74c	35		5
	defb 000h		;a74d	00		.
	defb 02dh		;a74e	2d		-
	defb 034h		;a74f	34		4
	defb 000h		;a750	00		.
	defb 02dh		;a751	2d		-
	defb 033h		;a752	33		3
	defb 000h		;a753	00		.
	defb 02dh		;a754	2d		-
	defb 032h		;a755	32		2
	defb 000h		;a756	00		.
	defb 02dh		;a757	2d		-
	defb 031h		;a758	31		1
	defb 000h		;a759	00		.
	defb 020h		;a75a	20		 
	defb 030h		;a75b	30		0
	defb 000h		;a75c	00		.
	defb 02bh		;a75d	2b		+
	defb 031h		;a75e	31		1
	defb 000h		;a75f	00		.
	defb 02bh		;a760	2b		+
	defb 032h		;a761	32		2
	defb 000h		;a762	00		.
	defb 02bh		;a763	2b		+
	defb 033h		;a764	33		3
	defb 000h		;a765	00		.
	defb 02bh		;a766	2b		+
	defb 034h		;a767	34		4
	defb 000h		;a768	00		.
	defb 02bh		;a769	2b		+
	defb 035h		;a76a	35		5
	defb 000h		;a76b	00		.
	defb 02bh		;a76c	2b		+
	defb 036h		;a76d	36		6
	defb 000h		;a76e	00		.
	defb 02bh		;a76f	2b		+
	defb 037h		;a770	37		7
	defb 000h		;a771	00		.
	defb 003h		;a772	03		.
	defb 017h		;a773	17		.
	defb 058h		;a774	58		X
	defb 02dh		;a775	2d		-
	defb 053h		;a776	53		S
	defb 063h		;a777	63		c
	defb 072h		;a778	72		r
	defb 065h		;a779	65		e
	defb 065h		;a77a	65		e
	defb 06eh		;a77b	6e		n
	defb 020h		;a77c	20		 
	defb 070h		;a77d	70		p
	defb 06fh		;a77e	6f		o
	defb 073h		;a77f	73		s
	defb 069h		;a780	69		i
	defb 074h		;a781	74		t
	defb 069h		;a782	69		i
	defb 06fh		;a783	6f		o
	defb 06eh		;a784	6e		n
	defb 020h		;a785	20		 
	defb 020h		;a786	20		 
	defb 020h		;a787	20		 
	defb 020h		;a788	20		 
	defb 020h		;a789	20		 
	defb 020h		;a78a	20		 
	defb 020h		;a78b	20		 
	defb 020h		;a78c	20		 
	defb 020h		;a78d	20		 
	defb 020h		;a78e	20		 
	defb 03ah		;a78f	3a		:
	defb 020h		;a790	20		 
	defb 000h		;a791	00		.
	defb 01fh		;a792	1f		.
	defb 00fh		;a793	0f		.
	defb 00eh		;a794	0e		.
	defb 02bh		;a795	2b		+
	defb 037h		;a796	37		7
	defb 000h		;a797	00		.
	defb 02bh		;a798	2b		+
	defb 036h		;a799	36		6
	defb 000h		;a79a	00		.
	defb 02bh		;a79b	2b		+
	defb 035h		;a79c	35		5
	defb 000h		;a79d	00		.
	defb 02bh		;a79e	2b		+
	defb 034h		;a79f	34		4
	defb 000h		;a7a0	00		.
	defb 02bh		;a7a1	2b		+
	defb 033h		;a7a2	33		3
	defb 000h		;a7a3	00		.
	defb 02bh		;a7a4	2b		+
	defb 032h		;a7a5	32		2
	defb 000h		;a7a6	00		.
	defb 02bh		;a7a7	2b		+
	defb 031h		;a7a8	31		1
	defb 000h		;a7a9	00		.
	defb 020h		;a7aa	20		 
	defb 030h		;a7ab	30		0
	defb 000h		;a7ac	00		.
	defb 02dh		;a7ad	2d		-
	defb 031h		;a7ae	31		1
	defb 000h		;a7af	00		.
	defb 02dh		;a7b0	2d		-
	defb 032h		;a7b1	32		2
	defb 000h		;a7b2	00		.
	defb 02dh		;a7b3	2d		-
	defb 033h		;a7b4	33		3
	defb 000h		;a7b5	00		.
	defb 02dh		;a7b6	2d		-
	defb 034h		;a7b7	34		4
	defb 000h		;a7b8	00		.
	defb 02dh		;a7b9	2d		-
	defb 035h		;a7ba	35		5
	defb 000h		;a7bb	00		.
	defb 02dh		;a7bc	2d		-
	defb 036h		;a7bd	36		6
	defb 000h		;a7be	00		.
	defb 02dh		;a7bf	2d		-
	defb 037h		;a7c0	37		7
	defb 000h		;a7c1	00		.
	defb 02bh		;a7c2	2b		+
	defb 007h		;a7c3	07		.
	defb 051h		;a7c4	51		Q
	defb 075h		;a7c5	75		u
	defb 069h		;a7c6	69		i
	defb 063h		;a7c7	63		c
	defb 06bh		;a7c8	6b		k
	defb 020h		;a7c9	20		 
	defb 052h		;a7ca	52		R
	defb 04fh		;a7cb	4f		O
	defb 04dh		;a7cc	4d		M
	defb 020h		;a7cd	20		 
	defb 053h		;a7ce	53		S
	defb 074h		;a7cf	74		t
	defb 061h		;a7d0	61		a
	defb 072h		;a7d1	72		r
	defb 074h		;a7d2	74		t
	defb 020h		;a7d3	20		 
	defb 020h		;a7d4	20		 
	defb 020h		;a7d5	20		 
	defb 020h		;a7d6	20		 
	defb 03ah		;a7d7	3a		:
	defb 020h		;a7d8	20		 
	defb 000h		;a7d9	00		.
	defb 00eh		;a7da	0e		.
	defb 001h		;a7db	01		.
	defb 001h		;a7dc	01		.
	defb 044h		;a7dd	44		D
	defb 069h		;a7de	69		i
	defb 073h		;a7df	73		s
	defb 061h		;a7e0	61		a
	defb 062h		;a7e1	62		b
	defb 06ch		;a7e2	6c		l
	defb 065h		;a7e3	65		e
	defb 064h		;a7e4	64		d
	defb 000h		;a7e5	00		.
	defb 045h		;a7e6	45		E
	defb 06eh		;a7e7	6e		n
	defb 061h		;a7e8	61		a
	defb 062h		;a7e9	62		b
	defb 06ch		;a7ea	6c		l
	defb 065h		;a7eb	65		e
	defb 064h		;a7ec	64		d
	defb 020h		;a7ed	20		 
	defb 000h		;a7ee	00		.
	defb 02bh		;a7ef	2b		+
	defb 008h		;a7f0	08		.
	defb 054h		;a7f1	54		T
	defb 052h		;a7f2	52		R
	defb 020h		;a7f3	20		 
	defb 044h		;a7f4	44		D
	defb 04fh		;a7f5	4f		O
	defb 053h		;a7f6	53		S
	defb 020h		;a7f7	20		 
	defb 041h		;a7f8	41		A
	defb 03ah		;a7f9	3a		:
	defb 03eh		;a7fa	3e		>
	defb 020h		;a7fb	20		 
	defb 020h		;a7fc	20		 
	defb 020h		;a7fd	20		 
	defb 020h		;a7fe	20		 
	defb 020h		;a7ff	20		 
	defb 020h		;a800	20		 
	defb 020h		;a801	20		 
	defb 020h		;a802	20		 
	defb 020h		;a803	20		 
	defb 03ah		;a804	3a		:
	defb 020h		;a805	20		 
	defb 000h		;a806	00		.
	defb 01eh		;a807	1e		.
	defb 003h		;a808	03		.
	defb 002h		;a809	02		.
	defb 044h		;a80a	44		D
	defb 065h		;a80b	65		e
	defb 066h		;a80c	66		f
	defb 061h		;a80d	61		a
	defb 075h		;a80e	75		u
	defb 06ch		;a80f	6c		l
	defb 074h		;a810	74		t
	defb 000h		;a811	00		.
	defb 046h		;a812	46		F
	defb 044h		;a813	44		D
	defb 044h		;a814	44		D
	defb 020h		;a815	20		 
	defb 020h		;a816	20		 
	defb 020h		;a817	20		 
	defb 020h		;a818	20		 
	defb 000h		;a819	00		.
	defb 048h		;a81a	48		H
	defb 044h		;a81b	44		D
	defb 044h		;a81c	44		D
	defb 020h		;a81d	20		 
	defb 020h		;a81e	20		 
	defb 020h		;a81f	20		 
	defb 020h		;a820	20		 
	defb 000h		;a821	00		.
	defb 02bh		;a822	2b		+
	defb 009h		;a823	09		.
	defb 054h		;a824	54		T
	defb 052h		;a825	52		R
	defb 020h		;a826	20		 
	defb 044h		;a827	44		D
	defb 04fh		;a828	4f		O
	defb 053h		;a829	53		S
	defb 020h		;a82a	20		 
	defb 042h		;a82b	42		B
	defb 03ah		;a82c	3a		:
	defb 03eh		;a82d	3e		>
	defb 020h		;a82e	20		 
	defb 020h		;a82f	20		 
	defb 020h		;a830	20		 
	defb 020h		;a831	20		 
	defb 020h		;a832	20		 
	defb 020h		;a833	20		 
	defb 020h		;a834	20		 
	defb 020h		;a835	20		 
	defb 020h		;a836	20		 
	defb 03ah		;a837	3a		:
	defb 020h		;a838	20		 
	defb 000h		;a839	00		.
	defb 01eh		;a83a	1e		.
	defb 00ch		;a83b	0c		.
	defb 008h		;a83c	08		.
	defb 044h		;a83d	44		D
	defb 065h		;a83e	65		e
	defb 066h		;a83f	66		f
	defb 061h		;a840	61		a
	defb 075h		;a841	75		u
	defb 06ch		;a842	6c		l
	defb 074h		;a843	74		t
	defb 000h		;a844	00		.
	defb 046h		;a845	46		F
	defb 044h		;a846	44		D
	defb 044h		;a847	44		D
	defb 020h		;a848	20		 
	defb 020h		;a849	20		 
	defb 020h		;a84a	20		 
	defb 020h		;a84b	20		 
	defb 000h		;a84c	00		.
	defb 048h		;a84d	48		H
	defb 044h		;a84e	44		D
	defb 044h		;a84f	44		D
	defb 020h		;a850	20		 
	defb 020h		;a851	20		 
	defb 020h		;a852	20		 
	defb 020h		;a853	20		 
	defb 000h		;a854	00		.
	defb 02bh		;a855	2b		+
	defb 00ah		;a856	0a		.
	defb 054h		;a857	54		T
	defb 052h		;a858	52		R
	defb 020h		;a859	20		 
	defb 044h		;a85a	44		D
	defb 04fh		;a85b	4f		O
	defb 053h		;a85c	53		S
	defb 020h		;a85d	20		 
	defb 043h		;a85e	43		C
	defb 03ah		;a85f	3a		:
	defb 03eh		;a860	3e		>
	defb 020h		;a861	20		 
	defb 020h		;a862	20		 
	defb 020h		;a863	20		 
	defb 020h		;a864	20		 
	defb 020h		;a865	20		 
	defb 020h		;a866	20		 
	defb 020h		;a867	20		 
	defb 020h		;a868	20		 
	defb 020h		;a869	20		 
	defb 03ah		;a86a	3a		:
	defb 020h		;a86b	20		 
	defb 000h		;a86c	00		.
	defb 01eh		;a86d	1e		.
	defb 030h		;a86e	30		0
	defb 020h		;a86f	20		 
	defb 044h		;a870	44		D
	defb 065h		;a871	65		e
	defb 066h		;a872	66		f
	defb 061h		;a873	61		a
	defb 075h		;a874	75		u
	defb 06ch		;a875	6c		l
	defb 074h		;a876	74		t
	defb 000h		;a877	00		.
	defb 046h		;a878	46		F
	defb 044h		;a879	44		D
	defb 044h		;a87a	44		D
	defb 020h		;a87b	20		 
	defb 020h		;a87c	20		 
	defb 020h		;a87d	20		 
	defb 020h		;a87e	20		 
	defb 000h		;a87f	00		.
	defb 048h		;a880	48		H
	defb 044h		;a881	44		D
	defb 044h		;a882	44		D
	defb 020h		;a883	20		 
	defb 020h		;a884	20		 
	defb 020h		;a885	20		 
	defb 020h		;a886	20		 
	defb 000h		;a887	00		.
	defb 02bh		;a888	2b		+
	defb 00bh		;a889	0b		.
	defb 054h		;a88a	54		T
	defb 052h		;a88b	52		R
	defb 020h		;a88c	20		 
	defb 044h		;a88d	44		D
	defb 04fh		;a88e	4f		O
	defb 053h		;a88f	53		S
	defb 020h		;a890	20		 
	defb 044h		;a891	44		D
	defb 03ah		;a892	3a		:
	defb 03eh		;a893	3e		>
	defb 020h		;a894	20		 
	defb 020h		;a895	20		 
	defb 020h		;a896	20		 
	defb 020h		;a897	20		 
	defb 020h		;a898	20		 
	defb 020h		;a899	20		 
	defb 020h		;a89a	20		 
	defb 020h		;a89b	20		 
	defb 020h		;a89c	20		 
	defb 03ah		;a89d	3a		:
	defb 020h		;a89e	20		 
	defb 000h		;a89f	00		.
	defb 01eh		;a8a0	1e		.
	defb 0c0h		;a8a1	c0		.
	defb 080h		;a8a2	80		.
	defb 044h		;a8a3	44		D
	defb 065h		;a8a4	65		e
	defb 066h		;a8a5	66		f
	defb 061h		;a8a6	61		a
	defb 075h		;a8a7	75		u
	defb 06ch		;a8a8	6c		l
	defb 074h		;a8a9	74		t
	defb 000h		;a8aa	00		.
	defb 046h		;a8ab	46		F
	defb 044h		;a8ac	44		D
	defb 044h		;a8ad	44		D
	defb 020h		;a8ae	20		 
	defb 020h		;a8af	20		 
	defb 020h		;a8b0	20		 
	defb 020h		;a8b1	20		 
	defb 000h		;a8b2	00		.
	defb 048h		;a8b3	48		H
	defb 044h		;a8b4	44		D
	defb 044h		;a8b5	44		D
	defb 020h		;a8b6	20		 
	defb 020h		;a8b7	20		 
	defb 020h		;a8b8	20		 
	defb 020h		;a8b9	20		 
	defb 000h		;a8ba	00		.
MSGENG:
				; = MSGENG (src: DSETUP.ASM:1447, BIOS-PP 1273243)
	defb 000h		;a8bb	00		.
	defb 000h		;a8bc	00		.
	defb 043h		;a8bd	43		C
	defb 06fh		;a8be	6f		o
	defb 070h		;a8bf	70		p
	defb 079h		;a8c0	79		y
	defb 072h		;a8c1	72		r
	defb 069h		;a8c2	69		i
	defb 067h		;a8c3	67		g
	defb 068h		;a8c4	68		h
	defb 074h		;a8c5	74		t
	defb 020h		;a8c6	20		 
	defb 028h		;a8c7	28		(
	defb 063h		;a8c8	63		c
	defb 029h		;a8c9	29		)
	defb 020h		;a8ca	20		 
	defb 032h		;a8cb	32		2
	defb 030h		;a8cc	30		0
	defb 030h		;a8cd	30		0
	defb 032h		;a8ce	32		2
	defb 020h		;a8cf	20		 
	defb 050h		;a8d0	50		P
	defb 045h		;a8d1	45		E
	defb 054h		;a8d2	54		T
	defb 045h		;a8d3	45		E
	defb 052h		;a8d4	52		R
	defb 053h		;a8d5	53		S
	defb 020h		;a8d6	20		 
	defb 050h		;a8d7	50		P
	defb 04ch		;a8d8	4c		L
	defb 055h		;a8d9	55		U
	defb 053h		;a8da	53		S
	defb 020h		;a8db	20		 
	defb 04ch		;a8dc	4c		L
	defb 054h		;a8dd	54		T
	defb 044h		;a8de	44		D
	defb 000h		;a8df	00		.
	defb 041h		;a8e0	41		A
	defb 06ch		;a8e1	6c		l
	defb 06ch		;a8e2	6c		l
	defb 020h		;a8e3	20		 
	defb 052h		;a8e4	52		R
	defb 069h		;a8e5	69		i
	defb 067h		;a8e6	67		g
	defb 068h		;a8e7	68		h
	defb 074h		;a8e8	74		t
	defb 073h		;a8e9	73		s
	defb 020h		;a8ea	20		 
	defb 052h		;a8eb	52		R
	defb 065h		;a8ec	65		e
	defb 073h		;a8ed	73		s
	defb 065h		;a8ee	65		e
	defb 072h		;a8ef	72		r
	defb 076h		;a8f0	76		v
	defb 065h		;a8f1	65		e
	defb 064h		;a8f2	64		d
	defb 000h		;a8f3	00		.
	defb 053h		;a8f4	53		S
	defb 050h		;a8f5	50		P
	defb 052h		;a8f6	52		R
	defb 049h		;a8f7	49		I
	defb 04eh		;a8f8	4e		N
	defb 054h		;a8f9	54		T
	defb 045h		;a8fa	45		E
	defb 052h		;a8fb	52		R
	defb 020h		;a8fc	20		 
	defb 053h		;a8fd	53		S
	defb 045h		;a8fe	45		E
	defb 054h		;a8ff	54		T
	defb 055h		;a900	55		U
	defb 050h		;a901	50		P
	defb 020h		;a902	20		 
	defb 055h		;a903	55		U
	defb 054h		;a904	54		T
	defb 049h		;a905	49		I
	defb 04ch		;a906	4c		L
	defb 049h		;a907	49		I
	defb 054h		;a908	54		T
	defb 059h		;a909	59		Y
	defb 020h		;a90a	20		 
	defb 056h		;a90b	56		V
	defb 065h		;a90c	65		e
	defb 072h		;a90d	72		r
	defb 073h		;a90e	73		s
	defb 069h		;a90f	69		i
	defb 06fh		;a910	6f		o
	defb 06eh		;a911	6e		n
	defb 020h		;a912	20		 
	defb 031h		;a913	31		1
	defb 02eh		;a914	2e		.
	defb 035h		;a915	35		5
	defb 038h		;a916	38		8
	defb 000h		;a917	00		.
	defb 028h		;a918	28		(
	defb 043h		;a919	43		C
	defb 029h		;a91a	29		)
	defb 020h		;a91b	20		 
	defb 032h		;a91c	32		2
	defb 030h		;a91d	30		0
	defb 030h		;a91e	30		0
	defb 032h		;a91f	32		2
	defb 020h		;a920	20		 
	defb 050h		;a921	50		P
	defb 045h		;a922	45		E
	defb 054h		;a923	54		T
	defb 045h		;a924	45		E
	defb 052h		;a925	52		R
	defb 053h		;a926	53		S
	defb 020h		;a927	20		 
	defb 050h		;a928	50		P
	defb 04ch		;a929	4c		L
	defb 055h		;a92a	55		U
	defb 053h		;a92b	53		S
	defb 020h		;a92c	20		 
	defb 04ch		;a92d	4c		L
	defb 054h		;a92e	54		T
	defb 044h		;a92f	44		D
	defb 02eh		;a930	2e		.
	defb 000h		;a931	00		.
	defb 053h		;a932	53		S
	defb 054h		;a933	54		T
	defb 041h		;a934	41		A
	defb 052h		;a935	52		R
	defb 054h		;a936	54		T
	defb 020h		;a937	20		 
	defb 046h		;a938	46		F
	defb 045h		;a939	45		E
	defb 041h		;a93a	41		A
	defb 054h		;a93b	54		T
	defb 055h		;a93c	55		U
	defb 052h		;a93d	52		R
	defb 045h		;a93e	45		E
	defb 053h		;a93f	53		S
	defb 020h		;a940	20		 
	defb 053h		;a941	53		S
	defb 045h		;a942	45		E
	defb 054h		;a943	54		T
	defb 055h		;a944	55		U
	defb 050h		;a945	50		P
	defb 000h		;a946	00		.
	defb 045h		;a947	45		E
	defb 053h		;a948	53		S
	defb 043h		;a949	43		C
	defb 020h		;a94a	20		 
	defb 03ah		;a94b	3a		:
	defb 020h		;a94c	20		 
	defb 051h		;a94d	51		Q
	defb 075h		;a94e	75		u
	defb 069h		;a94f	69		i
	defb 074h		;a950	74		t
	defb 020h		;a951	20		 
	defb 020h		;a952	20		 
	defb 020h		;a953	20		 
	defb 020h		;a954	20		 
	defb 020h		;a955	20		 
	defb 020h		;a956	20		 
	defb 020h		;a957	20		 
	defb 020h		;a958	20		 
	defb 020h		;a959	20		 
	defb 020h		;a95a	20		 
	defb 020h		;a95b	20		 
	defb 020h		;a95c	20		 
	defb 020h		;a95d	20		 
	defb 020h		;a95e	20		 
	defb 020h		;a95f	20		 
	defb 020h		;a960	20		 
	defb 020h		;a961	20		 
	defb 020h		;a962	20		 
	defb 020h		;a963	20		 
	defb 020h		;a964	20		 
	defb 020h		;a965	20		 
	defb 020h		;a966	20		 
	defb 020h		;a967	20		 
	defb 020h		;a968	20		 
	defb 020h		;a969	20		 
	defb 020h		;a96a	20		 
	defb 020h		;a96b	20		 
	defb 020h		;a96c	20		 
	defb 020h		;a96d	20		 
	defb 020h		;a96e	20		 
	defb 020h		;a96f	20		 
	defb 046h		;a970	46		F
	defb 031h		;a971	31		1
	defb 030h		;a972	30		0
	defb 020h		;a973	20		 
	defb 03ah		;a974	3a		:
	defb 020h		;a975	20		 
	defb 053h		;a976	53		S
	defb 061h		;a977	61		a
	defb 076h		;a978	76		v
	defb 065h		;a979	65		e
	defb 020h		;a97a	20		 
	defb 026h		;a97b	26		&
	defb 020h		;a97c	20		 
	defb 045h		;a97d	45		E
	defb 078h		;a97e	78		x
	defb 069h		;a97f	69		i
	defb 074h		;a980	74		t
	defb 020h		;a981	20		 
	defb 053h		;a982	53		S
	defb 065h		;a983	65		e
	defb 074h		;a984	74		t
	defb 075h		;a985	75		u
	defb 070h		;a986	70		p
	defb 000h		;a987	00		.
	defb 046h		;a988	46		F
	defb 032h		;a989	32		2
	defb 020h		;a98a	20		 
	defb 020h		;a98b	20		 
	defb 03ah		;a98c	3a		:
	defb 020h		;a98d	20		 
	defb 053h		;a98e	53		S
	defb 061h		;a98f	61		a
	defb 076h		;a990	76		v
	defb 065h		;a991	65		e
	defb 020h		;a992	20		 
	defb 056h		;a993	56		V
	defb 061h		;a994	61		a
	defb 06ch		;a995	6c		l
	defb 075h		;a996	75		u
	defb 065h		;a997	65		e
	defb 073h		;a998	73		s
	defb 020h		;a999	20		 
	defb 020h		;a99a	20		 
	defb 020h		;a99b	20		 
	defb 020h		;a99c	20		 
	defb 020h		;a99d	20		 
	defb 020h		;a99e	20		 
	defb 020h		;a99f	20		 
	defb 020h		;a9a0	20		 
	defb 020h		;a9a1	20		 
	defb 020h		;a9a2	20		 
	defb 020h		;a9a3	20		 
	defb 020h		;a9a4	20		 
	defb 020h		;a9a5	20		 
	defb 020h		;a9a6	20		 
	defb 020h		;a9a7	20		 
	defb 020h		;a9a8	20		 
	defb 020h		;a9a9	20		 
	defb 020h		;a9aa	20		 
	defb 020h		;a9ab	20		 
	defb 020h		;a9ac	20		 
	defb 018h		;a9ad	18		.
	defb 020h		;a9ae	20		 
	defb 019h		;a9af	19		.
	defb 020h		;a9b0	20		 
	defb 01ah		;a9b1	1a		.
	defb 020h		;a9b2	20		 
	defb 01bh		;a9b3	1b		.
	defb 020h		;a9b4	20		 
	defb 03ah		;a9b5	3a		:
	defb 020h		;a9b6	20		 
	defb 053h		;a9b7	53		S
	defb 065h		;a9b8	65		e
	defb 06ch		;a9b9	6c		l
	defb 065h		;a9ba	65		e
	defb 063h		;a9bb	63		c
	defb 074h		;a9bc	74		t
	defb 020h		;a9bd	20		 
	defb 049h		;a9be	49		I
	defb 074h		;a9bf	74		t
	defb 065h		;a9c0	65		e
	defb 06dh		;a9c1	6d		m
	defb 000h		;a9c2	00		.
	defb 046h		;a9c3	46		F
	defb 035h		;a9c4	35		5
	defb 020h		;a9c5	20		 
	defb 020h		;a9c6	20		 
	defb 03ah		;a9c7	3a		:
	defb 020h		;a9c8	20		 
	defb 04fh		;a9c9	4f		O
	defb 06ch		;a9ca	6c		l
	defb 064h		;a9cb	64		d
	defb 020h		;a9cc	20		 
	defb 056h		;a9cd	56		V
	defb 061h		;a9ce	61		a
	defb 06ch		;a9cf	6c		l
	defb 075h		;a9d0	75		u
	defb 065h		;a9d1	65		e
	defb 073h		;a9d2	73		s
	defb 020h		;a9d3	20		 
	defb 020h		;a9d4	20		 
	defb 020h		;a9d5	20		 
	defb 020h		;a9d6	20		 
	defb 020h		;a9d7	20		 
	defb 020h		;a9d8	20		 
	defb 020h		;a9d9	20		 
	defb 020h		;a9da	20		 
	defb 020h		;a9db	20		 
	defb 020h		;a9dc	20		 
	defb 020h		;a9dd	20		 
	defb 020h		;a9de	20		 
	defb 020h		;a9df	20		 
	defb 020h		;a9e0	20		 
	defb 020h		;a9e1	20		 
	defb 020h		;a9e2	20		 
	defb 020h		;a9e3	20		 
	defb 020h		;a9e4	20		 
	defb 020h		;a9e5	20		 
	defb 050h		;a9e6	50		P
	defb 055h		;a9e7	55		U
	defb 02fh		;a9e8	2f		/
	defb 050h		;a9e9	50		P
	defb 044h		;a9ea	44		D
	defb 02fh		;a9eb	2f		/
	defb 02bh		;a9ec	2b		+
	defb 02fh		;a9ed	2f		/
	defb 02dh		;a9ee	2d		-
	defb 020h		;a9ef	20		 
	defb 03ah		;a9f0	3a		:
	defb 020h		;a9f1	20		 
	defb 04dh		;a9f2	4d		M
	defb 06fh		;a9f3	6f		o
	defb 064h		;a9f4	64		d
	defb 069h		;a9f5	69		i
	defb 066h		;a9f6	66		f
	defb 079h		;a9f7	79		y
	defb 000h		;a9f8	00		.
	defb 046h		;a9f9	46		F
	defb 037h		;a9fa	37		7
	defb 020h		;a9fb	20		 
	defb 020h		;a9fc	20		 
	defb 03ah		;a9fd	3a		:
	defb 020h		;a9fe	20		 
	defb 044h		;a9ff	44		D
	defb 065h		;aa00	65		e
	defb 066h		;aa01	66		f
	defb 061h		;aa02	61		a
	defb 075h		;aa03	75		u
	defb 06ch		;aa04	6c		l
	defb 074h		;aa05	74		t
	defb 020h		;aa06	20		 
	defb 056h		;aa07	56		V
	defb 061h		;aa08	61		a
	defb 06ch		;aa09	6c		l
	defb 075h		;aa0a	75		u
	defb 065h		;aa0b	65		e
	defb 073h		;aa0c	73		s
	defb 020h		;aa0d	20		 
	defb 020h		;aa0e	20		 
	defb 020h		;aa0f	20		 
	defb 020h		;aa10	20		 
	defb 020h		;aa11	20		 
	defb 020h		;aa12	20		 
	defb 020h		;aa13	20		 
	defb 020h		;aa14	20		 
	defb 020h		;aa15	20		 
	defb 020h		;aa16	20		 
	defb 020h		;aa17	20		 
	defb 020h		;aa18	20		 
	defb 020h		;aa19	20		 
	defb 020h		;aa1a	20		 
	defb 020h		;aa1b	20		 
	defb 020h		;aa1c	20		 
	defb 020h		;aa1d	20		 
	defb 020h		;aa1e	20		 
	defb 020h		;aa1f	20		 
	defb 020h		;aa20	20		 
	defb 020h		;aa21	20		 
	defb 020h		;aa22	20		 
	defb 046h		;aa23	46		F
	defb 033h		;aa24	33		3
	defb 020h		;aa25	20		 
	defb 03ah		;aa26	3a		:
	defb 020h		;aa27	20		 
	defb 043h		;aa28	43		C
	defb 06fh		;aa29	6f		o
	defb 06ch		;aa2a	6c		l
	defb 06fh		;aa2b	6f		o
	defb 072h		;aa2c	72		r
	defb 020h		;aa2d	20		 
	defb 000h		;aa2e	00		.
	defb 050h		;aa2f	50		P
	defb 072h		;aa30	72		r
	defb 065h		;aa31	65		e
	defb 073h		;aa32	73		s
	defb 073h		;aa33	73		s
	defb 020h		;aa34	20		 
	defb 03ch		;aa35	3c		<
	defb 044h		;aa36	44		D
	defb 045h		;aa37	45		E
	defb 04ch		;aa38	4c		L
	defb 03eh		;aa39	3e		>
	defb 020h		;aa3a	20		 
	defb 074h		;aa3b	74		t
	defb 06fh		;aa3c	6f		o
	defb 020h		;aa3d	20		 
	defb 065h		;aa3e	65		e
	defb 06eh		;aa3f	6e		n
	defb 074h		;aa40	74		t
	defb 065h		;aa41	65		e
	defb 072h		;aa42	72		r
	defb 020h		;aa43	20		 
	defb 053h		;aa44	53		S
	defb 045h		;aa45	45		E
	defb 054h		;aa46	54		T
	defb 055h		;aa47	55		U
	defb 050h		;aa48	50		P
	defb 000h		;aa49	00		.
	defb 050h		;aa4a	50		P
	defb 072h		;aa4b	72		r
	defb 065h		;aa4c	65		e
	defb 073h		;aa4d	73		s
	defb 073h		;aa4e	73		s
	defb 020h		;aa4f	20		 
	defb 03ch		;aa50	3c		<
	defb 041h		;aa51	41		A
	defb 04ch		;aa52	4c		L
	defb 054h		;aa53	54		T
	defb 03eh		;aa54	3e		>
	defb 020h		;aa55	20		 
	defb 066h		;aa56	66		f
	defb 06fh		;aa57	6f		o
	defb 072h		;aa58	72		r
	defb 020h		;aa59	20		 
	defb 041h		;aa5a	41		A
	defb 06ch		;aa5b	6c		l
	defb 074h		;aa5c	74		t
	defb 02eh		;aa5d	2e		.
	defb 020h		;aa5e	20		 
	defb 053h		;aa5f	53		S
	defb 079h		;aa60	79		y
	defb 073h		;aa61	73		s
	defb 074h		;aa62	74		t
	defb 065h		;aa63	65		e
	defb 06dh		;aa64	6d		m
	defb 020h		;aa65	20		 
	defb 044h		;aa66	44		D
	defb 069h		;aa67	69		i
	defb 073h		;aa68	73		s
	defb 06bh		;aa69	6b		k
	defb 000h		;aa6a	00		.
	defb 031h		;aa6b	31		1
	defb 032h		;aa6c	32		2
	defb 033h		;aa6d	33		3
	defb 034h		;aa6e	34		4
	defb 035h		;aa6f	35		5
	defb 036h		;aa70	36		6
	defb 037h		;aa71	37		7
	defb 038h		;aa72	38		8
	defb 000h		;aa73	00		.
	defb 057h		;aa74	57		W
	defb 041h		;aa75	41		A
	defb 052h		;aa76	52		R
	defb 04eh		;aa77	4e		N
	defb 049h		;aa78	49		I
	defb 04eh		;aa79	4e		N
	defb 047h		;aa7a	47		G
	defb 021h		;aa7b	21		!
	defb 020h		;aa7c	20		 
	defb 043h		;aa7d	43		C
	defb 04dh		;aa7e	4d		M
	defb 04fh		;aa7f	4f		O
	defb 053h		;aa80	53		S
	defb 020h		;aa81	20		 
	defb 043h		;aa82	43		C
	defb 048h		;aa83	48		H
	defb 045h		;aa84	45		E
	defb 043h		;aa85	43		C
	defb 04bh		;aa86	4b		K
	defb 053h		;aa87	53		S
	defb 055h		;aa88	55		U
	defb 04dh		;aa89	4d		M
	defb 020h		;aa8a	20		 
	defb 045h		;aa8b	45		E
	defb 052h		;aa8c	52		R
	defb 052h		;aa8d	52		R
	defb 04fh		;aa8e	4f		O
	defb 052h		;aa8f	52		R
	defb 02ch		;aa90	2c		,
	defb 020h		;aa91	20		 
	defb 049h		;aa92	49		I
	defb 04eh		;aa93	4e		N
	defb 053h		;aa94	53		S
	defb 054h		;aa95	54		T
	defb 041h		;aa96	41		A
	defb 04ch		;aa97	4c		L
	defb 04ch		;aa98	4c		L
	defb 020h		;aa99	20		 
	defb 044h		;aa9a	44		D
	defb 045h		;aa9b	45		E
	defb 046h		;aa9c	46		F
	defb 041h		;aa9d	41		A
	defb 055h		;aa9e	55		U
	defb 04ch		;aa9f	4c		L
	defb 054h		;aaa0	54		T
	defb 020h		;aaa1	20		 
	defb 056h		;aaa2	56		V
	defb 041h		;aaa3	41		A
	defb 04ch		;aaa4	4c		L
	defb 055h		;aaa5	55		U
	defb 045h		;aaa6	45		E
	defb 053h		;aaa7	53		S
	defb 021h		;aaa8	21		!
	defb 000h		;aaa9	00		.
	defb 020h		;aaaa	20		 
	defb 044h		;aaab	44		D
	defb 065h		;aaac	65		e
	defb 074h		;aaad	74		t
	defb 065h		;aaae	65		e
	defb 063h		;aaaf	63		c
	defb 074h		;aab0	74		t
	defb 069h		;aab1	69		i
	defb 06eh		;aab2	6e		n
	defb 067h		;aab3	67		g
	defb 020h		;aab4	20		 
	defb 049h		;aab5	49		I
	defb 044h		;aab6	44		D
	defb 045h		;aab7	45		E
	defb 020h		;aab8	20		 
	defb 050h		;aab9	50		P
	defb 072h		;aaba	72		r
	defb 069h		;aabb	69		i
	defb 06dh		;aabc	6d		m
	defb 061h		;aabd	61		a
	defb 072h		;aabe	72		r
	defb 079h		;aabf	79		y
	defb 020h		;aac0	20		 
	defb 04dh		;aac1	4d		M
	defb 061h		;aac2	61		a
	defb 073h		;aac3	73		s
	defb 074h		;aac4	74		t
	defb 065h		;aac5	65		e
	defb 072h		;aac6	72		r
	defb 020h		;aac7	20		 
	defb 020h		;aac8	20		 
	defb 020h		;aac9	20		 
	defb 02eh		;aaca	2e		.
	defb 02eh		;aacb	2e		.
	defb 02eh		;aacc	2e		.
	defb 020h		;aacd	20		 
	defb 05bh		;aace	5b		[
	defb 050h		;aacf	50		P
	defb 072h		;aad0	72		r
	defb 065h		;aad1	65		e
	defb 073h		;aad2	73		s
	defb 073h		;aad3	73		s
	defb 020h		;aad4	20		 
	defb 046h		;aad5	46		F
	defb 034h		;aad6	34		4
	defb 020h		;aad7	20		 
	defb 074h		;aad8	74		t
	defb 06fh		;aad9	6f		o
	defb 020h		;aada	20		 
	defb 073h		;aadb	73		s
	defb 06bh		;aadc	6b		k
	defb 069h		;aadd	69		i
	defb 070h		;aade	70		p
	defb 05dh		;aadf	5d		]
	defb 000h		;aae0	00		.
	defb 020h		;aae1	20		 
	defb 044h		;aae2	44		D
	defb 065h		;aae3	65		e
	defb 074h		;aae4	74		t
	defb 065h		;aae5	65		e
	defb 063h		;aae6	63		c
	defb 074h		;aae7	74		t
	defb 069h		;aae8	69		i
	defb 06eh		;aae9	6e		n
	defb 067h		;aaea	67		g
	defb 020h		;aaeb	20		 
	defb 049h		;aaec	49		I
	defb 044h		;aaed	44		D
	defb 045h		;aaee	45		E
	defb 020h		;aaef	20		 
	defb 050h		;aaf0	50		P
	defb 072h		;aaf1	72		r
	defb 069h		;aaf2	69		i
	defb 06dh		;aaf3	6d		m
	defb 061h		;aaf4	61		a
	defb 072h		;aaf5	72		r
	defb 079h		;aaf6	79		y
	defb 020h		;aaf7	20		 
	defb 053h		;aaf8	53		S
	defb 06ch		;aaf9	6c		l
	defb 061h		;aafa	61		a
	defb 076h		;aafb	76		v
	defb 065h		;aafc	65		e
	defb 020h		;aafd	20		 
	defb 020h		;aafe	20		 
	defb 020h		;aaff	20		 
	defb 020h		;ab00	20		 
	defb 02eh		;ab01	2e		.
	defb 02eh		;ab02	2e		.
	defb 02eh		;ab03	2e		.
	defb 020h		;ab04	20		 
	defb 05bh		;ab05	5b		[
	defb 050h		;ab06	50		P
	defb 072h		;ab07	72		r
	defb 065h		;ab08	65		e
	defb 073h		;ab09	73		s
	defb 073h		;ab0a	73		s
	defb 020h		;ab0b	20		 
	defb 046h		;ab0c	46		F
	defb 034h		;ab0d	34		4
	defb 020h		;ab0e	20		 
	defb 074h		;ab0f	74		t
	defb 06fh		;ab10	6f		o
	defb 020h		;ab11	20		 
	defb 073h		;ab12	73		s
	defb 06bh		;ab13	6b		k
	defb 069h		;ab14	69		i
	defb 070h		;ab15	70		p
	defb 05dh		;ab16	5d		]
	defb 000h		;ab17	00		.
	defb 000h		;ab18	00		.
	defb 000h		;ab19	00		.
	defb 055h		;ab1a	55		U
	defb 06eh		;ab1b	6e		n
	defb 06bh		;ab1c	6b		k
	defb 06eh		;ab1d	6e		n
	defb 06fh		;ab1e	6f		o
	defb 077h		;ab1f	77		w
	defb 06eh		;ab20	6e		n
	defb 020h		;ab21	20		 
	defb 020h		;ab22	20		 
	defb 020h		;ab23	20		 
	defb 020h		;ab24	20		 
	defb 020h		;ab25	20		 
	defb 020h		;ab26	20		 
	defb 020h		;ab27	20		 
	defb 020h		;ab28	20		 
	defb 020h		;ab29	20		 
	defb 020h		;ab2a	20		 
	defb 020h		;ab2b	20		 
	defb 000h		;ab2c	00		.
	defb 04eh		;ab2d	4e		N
	defb 06fh		;ab2e	6f		o
	defb 06eh		;ab2f	6e		n
	defb 065h		;ab30	65		e
	defb 020h		;ab31	20		 
	defb 020h		;ab32	20		 
	defb 020h		;ab33	20		 
	defb 020h		;ab34	20		 
	defb 020h		;ab35	20		 
	defb 020h		;ab36	20		 
	defb 020h		;ab37	20		 
	defb 020h		;ab38	20		 
	defb 020h		;ab39	20		 
	defb 020h		;ab3a	20		 
	defb 020h		;ab3b	20		 
	defb 020h		;ab3c	20		 
	defb 020h		;ab3d	20		 
	defb 020h		;ab3e	20		 
	defb 000h		;ab3f	00		.
	defb 053h		;ab40	53		S
	defb 06bh		;ab41	6b		k
	defb 069h		;ab42	69		i
	defb 070h		;ab43	70		p
	defb 020h		;ab44	20		 
	defb 020h		;ab45	20		 
	defb 020h		;ab46	20		 
	defb 020h		;ab47	20		 
	defb 020h		;ab48	20		 
	defb 020h		;ab49	20		 
	defb 020h		;ab4a	20		 
	defb 020h		;ab4b	20		 
	defb 020h		;ab4c	20		 
	defb 020h		;ab4d	20		 
	defb 020h		;ab4e	20		 
	defb 020h		;ab4f	20		 
	defb 020h		;ab50	20		 
	defb 020h		;ab51	20		 
	defb 000h		;ab52	00		.
	defb 046h		;ab53	46		F
	defb 061h		;ab54	61		a
	defb 069h		;ab55	69		i
	defb 06ch		;ab56	6c		l
	defb 020h		;ab57	20		 
	defb 020h		;ab58	20		 
	defb 020h		;ab59	20		 
	defb 020h		;ab5a	20		 
	defb 020h		;ab5b	20		 
	defb 020h		;ab5c	20		 
	defb 020h		;ab5d	20		 
	defb 020h		;ab5e	20		 
	defb 020h		;ab5f	20		 
	defb 020h		;ab60	20		 
	defb 020h		;ab61	20		 
	defb 020h		;ab62	20		 
	defb 020h		;ab63	20		 
	defb 020h		;ab64	20		 
	defb 000h		;ab65	00		.
	defb 04dh		;ab66	4d		M
	defb 06fh		;ab67	6f		o
	defb 064h		;ab68	64		d
	defb 065h		;ab69	65		e
	defb 06ch		;ab6a	6c		l
	defb 020h		;ab6b	20		 
	defb 06eh		;ab6c	6e		n
	defb 061h		;ab6d	61		a
	defb 06dh		;ab6e	6d		m
	defb 065h		;ab6f	65		e
	defb 03ah		;ab70	3a		:
	defb 020h		;ab71	20		 
	defb 000h		;ab72	00		.
	defb 04dh		;ab73	4d		M
	defb 065h		;ab74	65		e
	defb 06dh		;ab75	6d		m
	defb 06fh		;ab76	6f		o
	defb 072h		;ab77	72		r
	defb 079h		;ab78	79		y
	defb 020h		;ab79	20		 
	defb 020h		;ab7a	20		 
	defb 020h		;ab7b	20		 
	defb 020h		;ab7c	20		 
	defb 03ah		;ab7d	3a		:
	defb 020h		;ab7e	20		 
	defb 000h		;ab7f	00		.
	defb 043h		;ab80	43		C
	defb 04dh		;ab81	4d		M
	defb 04fh		;ab82	4f		O
	defb 053h		;ab83	53		S
	defb 020h		;ab84	20		 
	defb 020h		;ab85	20		 
	defb 020h		;ab86	20		 
	defb 020h		;ab87	20		 
	defb 020h		;ab88	20		 
	defb 020h		;ab89	20		 
	defb 03ah		;ab8a	3a		:
	defb 020h		;ab8b	20		 
	defb 046h		;ab8c	46		F
	defb 06fh		;ab8d	6f		o
	defb 075h		;ab8e	75		u
	defb 06eh		;ab8f	6e		n
	defb 064h		;ab90	64		d
	defb 000h		;ab91	00		.
	defb 043h		;ab92	43		C
	defb 04dh		;ab93	4d		M
	defb 04fh		;ab94	4f		O
	defb 053h		;ab95	53		S
	defb 020h		;ab96	20		 
	defb 020h		;ab97	20		 
	defb 020h		;ab98	20		 
	defb 020h		;ab99	20		 
	defb 020h		;ab9a	20		 
	defb 020h		;ab9b	20		 
	defb 03ah		;ab9c	3a		:
	defb 020h		;ab9d	20		 
	defb 04eh		;ab9e	4e		N
	defb 06fh		;ab9f	6f		o
	defb 06eh		;aba0	6e		n
	defb 065h		;aba1	65		e
	defb 000h		;aba2	00		.
	defb 041h		;aba3	41		A
	defb 076h		;aba4	76		v
	defb 061h		;aba5	61		a
	defb 069h		;aba6	69		i
	defb 06ch		;aba7	6c		l
	defb 061h		;aba8	61		a
	defb 062h		;aba9	62		b
	defb 06ch		;abaa	6c		l
	defb 065h		;abab	65		e
	defb 020h		;abac	20		 
	defb 03ah		;abad	3a		:
	defb 020h		;abae	20		 
	defb 000h		;abaf	00		.
	defb 037h		;abb0	37		7
	defb 000h		;abb1	00		.
	defb 038h		;abb2	38		8
	defb 000h		;abb3	00		.
	defb 042h		;abb4	42		B
	defb 06fh		;abb5	6f		o
	defb 061h		;abb6	61		a
	defb 072h		;abb7	72		r
	defb 064h		;abb8	64		d
	defb 020h		;abb9	20		 
	defb 049h		;abba	49		I
	defb 044h		;abbb	44		D
	defb 020h		;abbc	20		 
	defb 020h		;abbd	20		 
	defb 03ah		;abbe	3a		:
	defb 020h		;abbf	20		 
	defb 000h		;abc0	00		.
	defb 053h		;abc1	53		S
	defb 074h		;abc2	74		t
	defb 061h		;abc3	61		a
	defb 072h		;abc4	72		r
	defb 074h		;abc5	74		t
	defb 020h		;abc6	20		 
	defb 066h		;abc7	66		f
	defb 072h		;abc8	72		r
	defb 06fh		;abc9	6f		o
	defb 06dh		;abca	6d		m
	defb 020h		;abcb	20		 
	defb 044h		;abcc	44		D
	defb 069h		;abcd	69		i
	defb 073h		;abce	73		s
	defb 06bh		;abcf	6b		k
	defb 065h		;abd0	65		e
	defb 074h		;abd1	74		t
	defb 074h		;abd2	74		t
	defb 065h		;abd3	65		e
	defb 02eh		;abd4	2e		.
	defb 02eh		;abd5	2e		.
	defb 02eh		;abd6	2e		.
	defb 000h		;abd7	00		.
	defb 053h		;abd8	53		S
	defb 074h		;abd9	74		t
	defb 061h		;abda	61		a
	defb 072h		;abdb	72		r
	defb 074h		;abdc	74		t
	defb 020h		;abdd	20		 
	defb 066h		;abde	66		f
	defb 072h		;abdf	72		r
	defb 06fh		;abe0	6f		o
	defb 06dh		;abe1	6d		m
	defb 020h		;abe2	20		 
	defb 048h		;abe3	48		H
	defb 061h		;abe4	61		a
	defb 072h		;abe5	72		r
	defb 064h		;abe6	64		d
	defb 020h		;abe7	20		 
	defb 064h		;abe8	64		d
	defb 069h		;abe9	69		i
	defb 073h		;abea	73		s
	defb 06bh		;abeb	6b		k
	defb 02eh		;abec	2e		.
	defb 02eh		;abed	2e		.
	defb 02eh		;abee	2e		.
	defb 000h		;abef	00		.
	defb 053h		;abf0	53		S
	defb 074h		;abf1	74		t
	defb 061h		;abf2	61		a
	defb 072h		;abf3	72		r
	defb 074h		;abf4	74		t
	defb 020h		;abf5	20		 
	defb 066h		;abf6	66		f
	defb 072h		;abf7	72		r
	defb 06fh		;abf8	6f		o
	defb 06dh		;abf9	6d		m
	defb 020h		;abfa	20		 
	defb 043h		;abfb	43		C
	defb 044h		;abfc	44		D
	defb 02dh		;abfd	2d		-
	defb 052h		;abfe	52		R
	defb 04fh		;abff	4f		O
	defb 04dh		;ac00	4d		M
	defb 02eh		;ac01	2e		.
	defb 02eh		;ac02	2e		.
	defb 02eh		;ac03	2e		.
	defb 000h		;ac04	00		.
	defb 053h		;ac05	53		S
	defb 074h		;ac06	74		t
	defb 061h		;ac07	61		a
	defb 072h		;ac08	72		r
	defb 074h		;ac09	74		t
	defb 020h		;ac0a	20		 
	defb 066h		;ac0b	66		f
	defb 072h		;ac0c	72		r
	defb 06fh		;ac0d	6f		o
	defb 06dh		;ac0e	6d		m
	defb 020h		;ac0f	20		 
	defb 052h		;ac10	52		R
	defb 041h		;ac11	41		A
	defb 04dh		;ac12	4d		M
	defb 020h		;ac13	20		 
	defb 064h		;ac14	64		d
	defb 069h		;ac15	69		i
	defb 073h		;ac16	73		s
	defb 06bh		;ac17	6b		k
	defb 02eh		;ac18	2e		.
	defb 02eh		;ac19	2e		.
	defb 02eh		;ac1a	2e		.
	defb 000h		;ac1b	00		.
	defb 041h		;ac1c	41		A
	defb 06ch		;ac1d	6c		l
	defb 074h		;ac1e	74		t
	defb 065h		;ac1f	65		e
	defb 072h		;ac20	72		r
	defb 06eh		;ac21	6e		n
	defb 061h		;ac22	61		a
	defb 074h		;ac23	74		t
	defb 069h		;ac24	69		i
	defb 076h		;ac25	76		v
	defb 065h		;ac26	65		e
	defb 020h		;ac27	20		 
	defb 000h		;ac28	00		.
	defb 066h		;ac29	66		f
	defb 061h		;ac2a	61		a
	defb 069h		;ac2b	69		i
	defb 06ch		;ac2c	6c		l
	defb 000h		;ac2d	00		.
	defb 04fh		;ac2e	4f		O
	defb 06bh		;ac2f	6b		k
	defb 000h		;ac30	00		.
	defb 050h		;ac31	50		P
	defb 052h		;ac32	52		R
	defb 045h		;ac33	45		E
	defb 053h		;ac34	53		S
	defb 053h		;ac35	53		S
	defb 020h		;ac36	20		 
	defb 03ch		;ac37	3c		<
	defb 045h		;ac38	45		E
	defb 04eh		;ac39	4e		N
	defb 054h		;ac3a	54		T
	defb 045h		;ac3b	45		E
	defb 052h		;ac3c	52		R
	defb 03eh		;ac3d	3e		>
	defb 020h		;ac3e	20		 
	defb 054h		;ac3f	54		T
	defb 04fh		;ac40	4f		O
	defb 020h		;ac41	20		 
	defb 052h		;ac42	52		R
	defb 045h		;ac43	45		E
	defb 042h		;ac44	42		B
	defb 04fh		;ac45	4f		O
	defb 04fh		;ac46	4f		O
	defb 054h		;ac47	54		T
	defb 02ch		;ac48	2c		,
	defb 020h		;ac49	20		 
	defb 03ch		;ac4a	3c		<
	defb 045h		;ac4b	45		E
	defb 053h		;ac4c	53		S
	defb 043h		;ac4d	43		C
	defb 03eh		;ac4e	3e		>
	defb 020h		;ac4f	20		 
	defb 054h		;ac50	54		T
	defb 04fh		;ac51	4f		O
	defb 020h		;ac52	20		 
	defb 043h		;ac53	43		C
	defb 041h		;ac54	41		A
	defb 04eh		;ac55	4e		N
	defb 043h		;ac56	43		C
	defb 045h		;ac57	45		E
	defb 04ch		;ac58	4c		L
	defb 020h		;ac59	20		 
	defb 02eh		;ac5a	2e		.
	defb 020h		;ac5b	20		 
	defb 02eh		;ac5c	2e		.
	defb 020h		;ac5d	20		 
	defb 02eh		;ac5e	2e		.
	defb 000h		;ac5f	00		.
lac60h:
	defb 02ch		;ac60	2c		,
	defb 0bah		;ac61	ba		.
	defb 065h		;ac62	65		e
	defb 0bah		;ac63	ba		.
	defb 09ch		;ac64	9c		.
	defb 0bah		;ac65	ba		.
	defb 0d3h		;ac66	d3		.
	defb 0bah		;ac67	ba		.
	defb 00ah		;ac68	0a		.
	defb 0bbh		;ac69	bb		.
	defb 04bh		;ac6a	4b		K
	defb 0bbh		;ac6b	bb		.
	defb 086h		;ac6c	86		.
	defb 0bbh		;ac6d	bb		.
	defb 0bdh		;ac6e	bd		.
	defb 0bbh		;ac6f	bb		.
	defb 0f4h		;ac70	f4		.
	defb 0bbh		;ac71	bb		.
	defb 044h		;ac72	44		D
	defb 0bch		;ac73	bc		.
	defb 094h		;ac74	94		.
	defb 0bch		;ac75	bc		.
	defb 0bdh		;ac76	bd		.
	defb 0bch		;ac77	bc		.
	defb 0e6h		;ac78	e6		.
	defb 0bch		;ac79	bc		.
	defb 025h		;ac7a	25		%
	defb 0bdh		;ac7b	bd		.
	defb 064h		;ac7c	64		d
	defb 0bdh		;ac7d	bd		.
	defb 09bh		;ac7e	9b		.
	defb 0bdh		;ac7f	bd		.
	defb 0ebh		;ac80	eb		.
	defb 0bdh		;ac81	bd		.
	defb 03bh		;ac82	3b		;
	defb 0beh		;ac83	be		.
	defb 06ah		;ac84	6a		j
	defb 0beh		;ac85	be		.
	defb 0ach		;ac86	ac		.
	defb 0beh		;ac87	be		.
	defb 0eeh		;ac88	ee		.
	defb 0beh		;ac89	be		.
	defb 030h		;ac8a	30		0
	defb 0bfh		;ac8b	bf		.
	defb 003h		;ac8c	03		.
	defb 007h		;ac8d	07		.
	defb 09fh		;ac8e	9f		.
	defb 0a7h		;ac8f	a7		.
	defb 0ebh		;ac90	eb		.
	defb 0aah		;ac91	aa		.
	defb 020h		;ac92	20		 
	defb 028h		;ac93	28		(
	defb 04ch		;ac94	4c		L
	defb 061h		;ac95	61		a
	defb 06eh		;ac96	6e		n
	defb 067h		;ac97	67		g
	defb 075h		;ac98	75		u
	defb 061h		;ac99	61		a
	defb 067h		;ac9a	67		g
	defb 065h		;ac9b	65		e
	defb 029h		;ac9c	29		)
	defb 020h		;ac9d	20		 
	defb 020h		;ac9e	20		 
	defb 020h		;ac9f	20		 
	defb 020h		;aca0	20		 
	defb 020h		;aca1	20		 
	defb 020h		;aca2	20		 
	defb 020h		;aca3	20		 
	defb 020h		;aca4	20		 
	defb 020h		;aca5	20		 
	defb 020h		;aca6	20		 
	defb 020h		;aca7	20		 
	defb 020h		;aca8	20		 
	defb 03ah		;aca9	3a		:
	defb 020h		;acaa	20		 
	defb 000h		;acab	00		.
	defb 00eh		;acac	0e		.
	defb 004h		;acad	04		.
	defb 004h		;acae	04		.
	defb 080h		;acaf	80		.
	defb 0adh		;acb0	ad		.
	defb 0a3h		;acb1	a3		.
	defb 0abh		;acb2	ab		.
	defb 0a8h		;acb3	a8		.
	defb 0a9h		;acb4	a9		.
	defb 0e1h		;acb5	e1		.
	defb 0aah		;acb6	aa		.
	defb 0a8h		;acb7	a8		.
	defb 0a9h		;acb8	a9		.
	defb 000h		;acb9	00		.
	defb 090h		;acba	90		.
	defb 0e3h		;acbb	e3		.
	defb 0e1h		;acbc	e1		.
	defb 0e1h		;acbd	e1		.
	defb 0aah		;acbe	aa		.
	defb 0a8h		;acbf	a8		.
	defb 0a9h		;acc0	a9		.
	defb 020h		;acc1	20		 
	defb 020h		;acc2	20		 
	defb 020h		;acc3	20		 
	defb 000h		;acc4	00		.
	defb 003h		;acc5	03		.
	defb 008h		;acc6	08		.
	defb 092h		;acc7	92		.
	defb 0a5h		;acc8	a5		.
	defb 0e1h		;acc9	e1		.
	defb 0e2h		;acca	e2		.
	defb 0a8h		;accb	a8		.
	defb 0e0h		;accc	e0		.
	defb 0aeh		;accd	ae		.
	defb 0a2h		;acce	a2		.
	defb 0a0h		;accf	a0		.
	defb 0adh		;acd0	ad		.
	defb 0a8h		;acd1	a8		.
	defb 0a5h		;acd2	a5		.
	defb 020h		;acd3	20		 
	defb 0afh		;acd4	af		.
	defb 0a0h		;acd5	a0		.
	defb 0ach		;acd6	ac		.
	defb 0efh		;acd7	ef		.
	defb 0e2h		;acd8	e2		.
	defb 0a8h		;acd9	a8		.
	defb 020h		;acda	20		 
	defb 020h		;acdb	20		 
	defb 020h		;acdc	20		 
	defb 020h		;acdd	20		 
	defb 020h		;acde	20		 
	defb 020h		;acdf	20		 
	defb 020h		;ace0	20		 
	defb 020h		;ace1	20		 
	defb 03ah		;ace2	3a		:
	defb 020h		;ace3	20		 
	defb 000h		;ace4	00		.
	defb 00eh		;ace5	0e		.
	defb 080h		;ace6	80		.
	defb 080h		;ace7	80		.
	defb 08eh		;ace8	8e		.
	defb 0e2h		;ace9	e2		.
	defb 0aah		;acea	aa		.
	defb 0abh		;aceb	ab		.
	defb 0eeh		;acec	ee		.
	defb 0e7h		;aced	e7		.
	defb 0a5h		;acee	a5		.
	defb 0adh		;acef	ad		.
	defb 0aeh		;acf0	ae		.
	defb 000h		;acf1	00		.
	defb 082h		;acf2	82		.
	defb 0aah		;acf3	aa		.
	defb 0abh		;acf4	ab		.
	defb 0eeh		;acf5	ee		.
	defb 0e7h		;acf6	e7		.
	defb 0a5h		;acf7	a5		.
	defb 0adh		;acf8	ad		.
	defb 0aeh		;acf9	ae		.
	defb 020h		;acfa	20		 
	defb 000h		;acfb	00		.
	defb 003h		;acfc	03		.
	defb 009h		;acfd	09		.
	defb 091h		;acfe	91		.
	defb 0aeh		;acff	ae		.
	defb 0e5h		;ad00	e5		.
	defb 0e0h		;ad01	e0		.
	defb 0a0h		;ad02	a0		.
	defb 0adh		;ad03	ad		.
	defb 0a5h		;ad04	a5		.
	defb 0adh		;ad05	ad		.
	defb 0a8h		;ad06	a8		.
	defb 0a5h		;ad07	a5		.
	defb 020h		;ad08	20		 
	defb 052h		;ad09	52		R
	defb 041h		;ad0a	41		A
	defb 04dh		;ad0b	4d		M
	defb 02dh		;ad0c	2d		-
	defb 0a4h		;ad0d	a4		.
	defb 0a8h		;ad0e	a8		.
	defb 0e1h		;ad0f	e1		.
	defb 0aah		;ad10	aa		.
	defb 0aeh		;ad11	ae		.
	defb 0a2h		;ad12	a2		.
	defb 020h		;ad13	20		 
	defb 020h		;ad14	20		 
	defb 020h		;ad15	20		 
	defb 020h		;ad16	20		 
	defb 020h		;ad17	20		 
	defb 020h		;ad18	20		 
	defb 03ah		;ad19	3a		:
	defb 020h		;ad1a	20		 
	defb 000h		;ad1b	00		.
	defb 00eh		;ad1c	0e		.
	defb 040h		;ad1d	40		@
	defb 040h		;ad1e	40		@
	defb 08eh		;ad1f	8e		.
	defb 0e2h		;ad20	e2		.
	defb 0aah		;ad21	aa		.
	defb 0abh		;ad22	ab		.
	defb 0eeh		;ad23	ee		.
	defb 0e7h		;ad24	e7		.
	defb 0a5h		;ad25	a5		.
	defb 0adh		;ad26	ad		.
	defb 0aeh		;ad27	ae		.
	defb 000h		;ad28	00		.
	defb 082h		;ad29	82		.
	defb 0aah		;ad2a	aa		.
	defb 0abh		;ad2b	ab		.
	defb 0eeh		;ad2c	ee		.
	defb 0e7h		;ad2d	e7		.
	defb 0a5h		;ad2e	a5		.
	defb 0adh		;ad2f	ad		.
	defb 0aeh		;ad30	ae		.
	defb 020h		;ad31	20		 
	defb 000h		;ad32	00		.
	defb 003h		;ad33	03		.
	defb 00ah		;ad34	0a		.
	defb 08eh		;ad35	8e		.
	defb 0a1h		;ad36	a1		.
	defb 0adh		;ad37	ad		.
	defb 0aeh		;ad38	ae		.
	defb 0a2h		;ad39	a2		.
	defb 0abh		;ad3a	ab		.
	defb 0a5h		;ad3b	a5		.
	defb 0adh		;ad3c	ad		.
	defb 0a8h		;ad3d	a8		.
	defb 0a5h		;ad3e	a5		.
	defb 020h		;ad3f	20		 
	defb 042h		;ad40	42		B
	defb 049h		;ad41	49		I
	defb 04fh		;ad42	4f		O
	defb 053h		;ad43	53		S
	defb 020h		;ad44	20		 
	defb 020h		;ad45	20		 
	defb 020h		;ad46	20		 
	defb 020h		;ad47	20		 
	defb 020h		;ad48	20		 
	defb 020h		;ad49	20		 
	defb 020h		;ad4a	20		 
	defb 020h		;ad4b	20		 
	defb 020h		;ad4c	20		 
	defb 020h		;ad4d	20		 
	defb 020h		;ad4e	20		 
	defb 020h		;ad4f	20		 
	defb 03ah		;ad50	3a		:
	defb 020h		;ad51	20		 
	defb 000h		;ad52	00		.
	defb 00eh		;ad53	0e		.
	defb 020h		;ad54	20		 
	defb 020h		;ad55	20		 
	defb 08eh		;ad56	8e		.
	defb 0e2h		;ad57	e2		.
	defb 0aah		;ad58	aa		.
	defb 0abh		;ad59	ab		.
	defb 0eeh		;ad5a	ee		.
	defb 0e7h		;ad5b	e7		.
	defb 0a5h		;ad5c	a5		.
	defb 0adh		;ad5d	ad		.
	defb 0aeh		;ad5e	ae		.
	defb 000h		;ad5f	00		.
	defb 082h		;ad60	82		.
	defb 0aah		;ad61	aa		.
	defb 0abh		;ad62	ab		.
	defb 0eeh		;ad63	ee		.
	defb 0e7h		;ad64	e7		.
	defb 0a5h		;ad65	a5		.
	defb 0adh		;ad66	ad		.
	defb 0aeh		;ad67	ae		.
	defb 020h		;ad68	20		 
	defb 000h		;ad69	00		.
	defb 003h		;ad6a	03		.
	defb 00bh		;ad6b	0b		.
	defb 08dh		;ad6c	8d		.
	defb 0a0h		;ad6d	a0		.
	defb 0e7h		;ad6e	e7		.
	defb 0a0h		;ad6f	a0		.
	defb 0abh		;ad70	ab		.
	defb 0ech		;ad71	ec		.
	defb 0adh		;ad72	ad		.
	defb 0aeh		;ad73	ae		.
	defb 0a5h		;ad74	a5		.
	defb 020h		;ad75	20		 
	defb 0aeh		;ad76	ae		.
	defb 0a6h		;ad77	a6		.
	defb 0a8h		;ad78	a8		.
	defb 0a4h		;ad79	a4		.
	defb 0a0h		;ad7a	a0		.
	defb 0adh		;ad7b	ad		.
	defb 0a8h		;ad7c	a8		.
	defb 0a5h		;ad7d	a5		.
	defb 020h		;ad7e	20		 
	defb 020h		;ad7f	20		 
	defb 020h		;ad80	20		 
	defb 020h		;ad81	20		 
	defb 020h		;ad82	20		 
	defb 020h		;ad83	20		 
	defb 020h		;ad84	20		 
	defb 020h		;ad85	20		 
	defb 020h		;ad86	20		 
	defb 03ah		;ad87	3a		:
	defb 020h		;ad88	20		 
	defb 000h		;ad89	00		.
	defb 00eh		;ad8a	0e		.
	defb 018h		;ad8b	18		.
	defb 010h		;ad8c	10		.
	defb 08eh		;ad8d	8e		.
	defb 0e2h		;ad8e	e2		.
	defb 0aah		;ad8f	aa		.
	defb 0abh		;ad90	ab		.
	defb 0eeh		;ad91	ee		.
	defb 0e7h		;ad92	e7		.
	defb 0a5h		;ad93	a5		.
	defb 0adh		;ad94	ad		.
	defb 0aeh		;ad95	ae		.
	defb 000h		;ad96	00		.
	defb 08eh		;ad97	8e		.
	defb 0a1h		;ad98	a1		.
	defb 0ebh		;ad99	eb		.
	defb 0e7h		;ad9a	e7		.
	defb 0adh		;ad9b	ad		.
	defb 0aeh		;ad9c	ae		.
	defb 0a5h		;ad9d	a5		.
	defb 020h		;ad9e	20		 
	defb 020h		;ad9f	20		 
	defb 000h		;ada0	00		.
	defb 082h		;ada1	82		.
	defb 0aah		;ada2	aa		.
	defb 0abh		;ada3	ab		.
	defb 0eeh		;ada4	ee		.
	defb 0e7h		;ada5	e7		.
	defb 0a5h		;ada6	a5		.
	defb 0adh		;ada7	ad		.
	defb 0aeh		;ada8	ae		.
	defb 020h		;ada9	20		 
	defb 000h		;adaa	00		.
	defb 003h		;adab	03		.
	defb 00ch		;adac	0c		.
	defb 091h		;adad	91		.
	defb 0aah		;adae	aa		.
	defb 0aeh		;adaf	ae		.
	defb 0e0h		;adb0	e0		.
	defb 0aeh		;adb1	ae		.
	defb 0e1h		;adb2	e1		.
	defb 0e2h		;adb3	e2		.
	defb 0ech		;adb4	ec		.
	defb 020h		;adb5	20		 
	defb 0a0h		;adb6	a0		.
	defb 0a2h		;adb7	a2		.
	defb 0e2h		;adb8	e2		.
	defb 0aeh		;adb9	ae		.
	defb 0afh		;adba	af		.
	defb 0aeh		;adbb	ae		.
	defb 0a2h		;adbc	a2		.
	defb 0e2h		;adbd	e2		.
	defb 0aeh		;adbe	ae		.
	defb 0e0h		;adbf	e0		.
	defb 0a0h		;adc0	a0		.
	defb 020h		;adc1	20		 
	defb 020h		;adc2	20		 
	defb 020h		;adc3	20		 
	defb 020h		;adc4	20		 
	defb 020h		;adc5	20		 
	defb 020h		;adc6	20		 
	defb 020h		;adc7	20		 
	defb 03ah		;adc8	3a		:
	defb 020h		;adc9	20		 
	defb 000h		;adca	00		.
	defb 00fh		;adcb	0f		.
	defb 007h		;adcc	07		.
	defb 007h		;adcd	07		.
	defb 036h		;adce	36		6
	defb 020h		;adcf	20		 
	defb 000h		;add0	00		.
	defb 038h		;add1	38		8
	defb 020h		;add2	20		 
	defb 000h		;add3	00		.
	defb 031h		;add4	31		1
	defb 030h		;add5	30		0
	defb 000h		;add6	00		.
	defb 031h		;add7	31		1
	defb 032h		;add8	32		2
	defb 000h		;add9	00		.
	defb 031h		;adda	31		1
	defb 035h		;addb	35		5
	defb 000h		;addc	00		.
	defb 032h		;addd	32		2
	defb 030h		;adde	30		0
	defb 000h		;addf	00		.
	defb 032h		;ade0	32		2
	defb 034h		;ade1	34		4
	defb 000h		;ade2	00		.
	defb 033h		;ade3	33		3
	defb 030h		;ade4	30		0
	defb 000h		;ade5	00		.
	defb 003h		;ade6	03		.
	defb 00dh		;ade7	0d		.
	defb 087h		;ade8	87		.
	defb 0a0h		;ade9	a0		.
	defb 0a4h		;adea	a4		.
	defb 0a5h		;adeb	a5		.
	defb 0e0h		;adec	e0		.
	defb 0a6h		;aded	a6		.
	defb 0aah		;adee	aa		.
	defb 0a0h		;adef	a0		.
	defb 020h		;adf0	20		 
	defb 0a0h		;adf1	a0		.
	defb 0a2h		;adf2	a2		.
	defb 0e2h		;adf3	e2		.
	defb 0aeh		;adf4	ae		.
	defb 0afh		;adf5	af		.
	defb 0aeh		;adf6	ae		.
	defb 0a2h		;adf7	a2		.
	defb 0e2h		;adf8	e2		.
	defb 0aeh		;adf9	ae		.
	defb 0e0h		;adfa	e0		.
	defb 0a0h		;adfb	a0		.
	defb 020h		;adfc	20		 
	defb 028h		;adfd	28		(
	defb 08ch		;adfe	8c		.
	defb 0e1h		;adff	e1		.
	defb 0a5h		;ae00	a5		.
	defb 0aah		;ae01	aa		.
	defb 029h		;ae02	29		)
	defb 03ah		;ae03	3a		:
	defb 020h		;ae04	20		 
	defb 000h		;ae05	00		.
	defb 00fh		;ae06	0f		.
	defb 060h		;ae07	60		`
	defb 060h		;ae08	60		`
	defb 032h		;ae09	32		2
	defb 035h		;ae0a	35		5
	defb 030h		;ae0b	30		0
	defb 020h		;ae0c	20		 
	defb 000h		;ae0d	00		.
	defb 035h		;ae0e	35		5
	defb 030h		;ae0f	30		0
	defb 030h		;ae10	30		0
	defb 020h		;ae11	20		 
	defb 000h		;ae12	00		.
	defb 037h		;ae13	37		7
	defb 035h		;ae14	35		5
	defb 030h		;ae15	30		0
	defb 020h		;ae16	20		 
	defb 000h		;ae17	00		.
	defb 031h		;ae18	31		1
	defb 030h		;ae19	30		0
	defb 030h		;ae1a	30		0
	defb 030h		;ae1b	30		0
	defb 000h		;ae1c	00		.
	defb 003h		;ae1d	03		.
	defb 00eh		;ae1e	0e		.
	defb 091h		;ae1f	91		.
	defb 0aeh		;ae20	ae		.
	defb 0aeh		;ae21	ae		.
	defb 0a1h		;ae22	a1		.
	defb 0e9h		;ae23	e9		.
	defb 0a5h		;ae24	a5		.
	defb 0adh		;ae25	ad		.
	defb 0a8h		;ae26	a8		.
	defb 0a5h		;ae27	a5		.
	defb 020h		;ae28	20		 
	defb 0aeh		;ae29	ae		.
	defb 020h		;ae2a	20		 
	defb 0afh		;ae2b	af		.
	defb 0a5h		;ae2c	a5		.
	defb 0e0h		;ae2d	e0		.
	defb 0a5h		;ae2e	a5		.
	defb 0a7h		;ae2f	a7		.
	defb 0a0h		;ae30	a0		.
	defb 0a3h		;ae31	a3		.
	defb 0e0h		;ae32	e0		.
	defb 0e3h		;ae33	e3		.
	defb 0a7h		;ae34	a7		.
	defb 0aah		;ae35	aa		.
	defb 0a5h		;ae36	a5		.
	defb 020h		;ae37	20		 
	defb 020h		;ae38	20		 
	defb 020h		;ae39	20		 
	defb 03ah		;ae3a	3a		:
	defb 020h		;ae3b	20		 
	defb 000h		;ae3c	00		.
	defb 01dh		;ae3d	1d		.
	defb 002h		;ae3e	02		.
	defb 002h		;ae3f	02		.
	defb 08eh		;ae40	8e		.
	defb 0e2h		;ae41	e2		.
	defb 0aah		;ae42	aa		.
	defb 0abh		;ae43	ab		.
	defb 0eeh		;ae44	ee		.
	defb 0e7h		;ae45	e7		.
	defb 0a5h		;ae46	a5		.
	defb 0adh		;ae47	ad		.
	defb 0aeh		;ae48	ae		.
	defb 000h		;ae49	00		.
	defb 082h		;ae4a	82		.
	defb 0aah		;ae4b	aa		.
	defb 0abh		;ae4c	ab		.
	defb 0eeh		;ae4d	ee		.
	defb 0e7h		;ae4e	e7		.
	defb 0a5h		;ae4f	a5		.
	defb 0adh		;ae50	ad		.
	defb 0aeh		;ae51	ae		.
	defb 020h		;ae52	20		 
	defb 000h		;ae53	00		.
	defb 003h		;ae54	03		.
	defb 00fh		;ae55	0f		.
	defb 091h		;ae56	91		.
	defb 0a8h		;ae57	a8		.
	defb 0e1h		;ae58	e1		.
	defb 0e2h		;ae59	e2		.
	defb 0a5h		;ae5a	a5		.
	defb 0ach		;ae5b	ac		.
	defb 0adh		;ae5c	ad		.
	defb 0ebh		;ae5d	eb		.
	defb 0a9h		;ae5e	a9		.
	defb 020h		;ae5f	20		 
	defb 0a4h		;ae60	a4		.
	defb 0a8h		;ae61	a8		.
	defb 0e1h		;ae62	e1		.
	defb 0aah		;ae63	aa		.
	defb 020h		;ae64	20		 
	defb 020h		;ae65	20		 
	defb 020h		;ae66	20		 
	defb 020h		;ae67	20		 
	defb 020h		;ae68	20		 
	defb 020h		;ae69	20		 
	defb 020h		;ae6a	20		 
	defb 020h		;ae6b	20		 
	defb 020h		;ae6c	20		 
	defb 020h		;ae6d	20		 
	defb 020h		;ae6e	20		 
	defb 020h		;ae6f	20		 
	defb 020h		;ae70	20		 
	defb 03ah		;ae71	3a		:
	defb 020h		;ae72	20		 
	defb 000h		;ae73	00		.
	defb 010h		;ae74	10		.
	defb 007h		;ae75	07		.
	defb 004h		;ae76	04		.
	defb 031h		;ae77	31		1
	defb 02dh		;ae78	2d		-
	defb 073h		;ae79	73		s
	defb 074h		;ae7a	74		t
	defb 020h		;ae7b	20		 
	defb 046h		;ae7c	46		F
	defb 044h		;ae7d	44		D
	defb 044h		;ae7e	44		D
	defb 000h		;ae7f	00		.
	defb 032h		;ae80	32		2
	defb 02dh		;ae81	2d		-
	defb 06eh		;ae82	6e		n
	defb 064h		;ae83	64		d
	defb 020h		;ae84	20		 
	defb 046h		;ae85	46		F
	defb 044h		;ae86	44		D
	defb 044h		;ae87	44		D
	defb 000h		;ae88	00		.
	defb 031h		;ae89	31		1
	defb 02dh		;ae8a	2d		-
	defb 073h		;ae8b	73		s
	defb 074h		;ae8c	74		t
	defb 020h		;ae8d	20		 
	defb 049h		;ae8e	49		I
	defb 044h		;ae8f	44		D
	defb 045h		;ae90	45		E
	defb 000h		;ae91	00		.
	defb 032h		;ae92	32		2
	defb 02dh		;ae93	2d		-
	defb 06eh		;ae94	6e		n
	defb 064h		;ae95	64		d
	defb 020h		;ae96	20		 
	defb 049h		;ae97	49		I
	defb 044h		;ae98	44		D
	defb 045h		;ae99	45		E
	defb 000h		;ae9a	00		.
	defb 052h		;ae9b	52		R
	defb 041h		;ae9c	41		A
	defb 04dh		;ae9d	4d		M
	defb 02dh		;ae9e	2d		-
	defb 044h		;ae9f	44		D
	defb 049h		;aea0	49		I
	defb 053h		;aea1	53		S
	defb 04bh		;aea2	4b		K
	defb 000h		;aea3	00		.
	defb 003h		;aea4	03		.
	defb 010h		;aea5	10		.
	defb 080h		;aea6	80		.
	defb 0abh		;aea7	ab		.
	defb 0e2h		;aea8	e2		.
	defb 02eh		;aea9	2e		.
	defb 020h		;aeaa	20		 
	defb 091h		;aeab	91		.
	defb 0a8h		;aeac	a8		.
	defb 0e1h		;aead	e1		.
	defb 0e2h		;aeae	e2		.
	defb 0a5h		;aeaf	a5		.
	defb 0ach		;aeb0	ac		.
	defb 0adh		;aeb1	ad		.
	defb 0ebh		;aeb2	eb		.
	defb 0a9h		;aeb3	a9		.
	defb 020h		;aeb4	20		 
	defb 0a4h		;aeb5	a4		.
	defb 0a8h		;aeb6	a8		.
	defb 0e1h		;aeb7	e1		.
	defb 0aah		;aeb8	aa		.
	defb 020h		;aeb9	20		 
	defb 020h		;aeba	20		 
	defb 020h		;aebb	20		 
	defb 020h		;aebc	20		 
	defb 020h		;aebd	20		 
	defb 020h		;aebe	20		 
	defb 020h		;aebf	20		 
	defb 020h		;aec0	20		 
	defb 03ah		;aec1	3a		:
	defb 020h		;aec2	20		 
	defb 000h		;aec3	00		.
	defb 010h		;aec4	10		.
	defb 070h		;aec5	70		p
	defb 040h		;aec6	40		@
	defb 031h		;aec7	31		1
	defb 02dh		;aec8	2d		-
	defb 073h		;aec9	73		s
	defb 074h		;aeca	74		t
	defb 020h		;aecb	20		 
	defb 046h		;aecc	46		F
	defb 044h		;aecd	44		D
	defb 044h		;aece	44		D
	defb 000h		;aecf	00		.
	defb 032h		;aed0	32		2
	defb 02dh		;aed1	2d		-
	defb 06eh		;aed2	6e		n
	defb 064h		;aed3	64		d
	defb 020h		;aed4	20		 
	defb 046h		;aed5	46		F
	defb 044h		;aed6	44		D
	defb 044h		;aed7	44		D
	defb 000h		;aed8	00		.
	defb 031h		;aed9	31		1
	defb 02dh		;aeda	2d		-
	defb 073h		;aedb	73		s
	defb 074h		;aedc	74		t
	defb 020h		;aedd	20		 
	defb 049h		;aede	49		I
	defb 044h		;aedf	44		D
	defb 045h		;aee0	45		E
	defb 000h		;aee1	00		.
	defb 032h		;aee2	32		2
	defb 02dh		;aee3	2d		-
	defb 06eh		;aee4	6e		n
	defb 064h		;aee5	64		d
	defb 020h		;aee6	20		 
	defb 049h		;aee7	49		I
	defb 044h		;aee8	44		D
	defb 045h		;aee9	45		E
	defb 000h		;aeea	00		.
	defb 052h		;aeeb	52		R
	defb 041h		;aeec	41		A
	defb 04dh		;aeed	4d		M
	defb 02dh		;aeee	2d		-
	defb 044h		;aeef	44		D
	defb 049h		;aef0	49		I
	defb 053h		;aef1	53		S
	defb 04bh		;aef2	4b		K
	defb 000h		;aef3	00		.
	defb 003h		;aef4	03		.
	defb 011h		;aef5	11		.
	defb 046h		;aef6	46		F
	defb 044h		;aef7	44		D
	defb 044h		;aef8	44		D
	defb 020h		;aef9	20		 
	defb 0afh		;aefa	af		.
	defb 0a5h		;aefb	a5		.
	defb 0e0h		;aefc	e0		.
	defb 0a2h		;aefd	a2		.
	defb 0ebh		;aefe	eb		.
	defb 0a9h		;aeff	a9		.
	defb 020h		;af00	20		 
	defb 020h		;af01	20		 
	defb 020h		;af02	20		 
	defb 020h		;af03	20		 
	defb 020h		;af04	20		 
	defb 020h		;af05	20		 
	defb 020h		;af06	20		 
	defb 020h		;af07	20		 
	defb 020h		;af08	20		 
	defb 020h		;af09	20		 
	defb 020h		;af0a	20		 
	defb 020h		;af0b	20		 
	defb 020h		;af0c	20		 
	defb 020h		;af0d	20		 
	defb 020h		;af0e	20		 
	defb 020h		;af0f	20		 
	defb 020h		;af10	20		 
	defb 03ah		;af11	3a		:
	defb 020h		;af12	20		 
	defb 000h		;af13	00		.
	defb 011h		;af14	11		.
	defb 003h		;af15	03		.
	defb 000h		;af16	00		.
	defb 080h		;af17	80		.
	defb 0a2h		;af18	a2		.
	defb 0e2h		;af19	e2		.
	defb 0aeh		;af1a	ae		.
	defb 020h		;af1b	20		 
	defb 000h		;af1c	00		.
	defb 003h		;af1d	03		.
	defb 012h		;af1e	12		.
	defb 046h		;af1f	46		F
	defb 044h		;af20	44		D
	defb 044h		;af21	44		D
	defb 020h		;af22	20		 
	defb 0a2h		;af23	a2		.
	defb 0e2h		;af24	e2		.
	defb 0aeh		;af25	ae		.
	defb 0e0h		;af26	e0		.
	defb 0aeh		;af27	ae		.
	defb 0a9h		;af28	a9		.
	defb 020h		;af29	20		 
	defb 020h		;af2a	20		 
	defb 020h		;af2b	20		 
	defb 020h		;af2c	20		 
	defb 020h		;af2d	20		 
	defb 020h		;af2e	20		 
	defb 020h		;af2f	20		 
	defb 020h		;af30	20		 
	defb 020h		;af31	20		 
	defb 020h		;af32	20		 
	defb 020h		;af33	20		 
	defb 020h		;af34	20		 
	defb 020h		;af35	20		 
	defb 020h		;af36	20		 
	defb 020h		;af37	20		 
	defb 020h		;af38	20		 
	defb 020h		;af39	20		 
	defb 03ah		;af3a	3a		:
	defb 020h		;af3b	20		 
	defb 000h		;af3c	00		.
	defb 011h		;af3d	11		.
	defb 00ch		;af3e	0c		.
	defb 000h		;af3f	00		.
	defb 080h		;af40	80		.
	defb 0a2h		;af41	a2		.
	defb 0e2h		;af42	e2		.
	defb 0aeh		;af43	ae		.
	defb 020h		;af44	20		 
	defb 000h		;af45	00		.
	defb 003h		;af46	03		.
	defb 013h		;af47	13		.
	defb 049h		;af48	49		I
	defb 044h		;af49	44		D
	defb 045h		;af4a	45		E
	defb 020h		;af4b	20		 
	defb 04dh		;af4c	4d		M
	defb 061h		;af4d	61		a
	defb 073h		;af4e	73		s
	defb 074h		;af4f	74		t
	defb 065h		;af50	65		e
	defb 072h		;af51	72		r
	defb 020h		;af52	20		 
	defb 020h		;af53	20		 
	defb 020h		;af54	20		 
	defb 020h		;af55	20		 
	defb 020h		;af56	20		 
	defb 020h		;af57	20		 
	defb 020h		;af58	20		 
	defb 020h		;af59	20		 
	defb 020h		;af5a	20		 
	defb 020h		;af5b	20		 
	defb 020h		;af5c	20		 
	defb 020h		;af5d	20		 
	defb 020h		;af5e	20		 
	defb 020h		;af5f	20		 
	defb 020h		;af60	20		 
	defb 020h		;af61	20		 
	defb 020h		;af62	20		 
	defb 03ah		;af63	3a		:
	defb 020h		;af64	20		 
	defb 000h		;af65	00		.
	defb 011h		;af66	11		.
	defb 030h		;af67	30		0
	defb 030h		;af68	30		0
	defb 080h		;af69	80		.
	defb 0a2h		;af6a	a2		.
	defb 0e2h		;af6b	e2		.
	defb 0aeh		;af6c	ae		.
	defb 020h		;af6d	20		 
	defb 020h		;af6e	20		 
	defb 000h		;af6f	00		.
	defb 053h		;af70	53		S
	defb 065h		;af71	65		e
	defb 074h		;af72	74		t
	defb 075h		;af73	75		u
	defb 070h		;af74	70		p
	defb 020h		;af75	20		 
	defb 000h		;af76	00		.
	defb 043h		;af77	43		C
	defb 044h		;af78	44		D
	defb 02dh		;af79	2d		-
	defb 052h		;af7a	52		R
	defb 04fh		;af7b	4f		O
	defb 04dh		;af7c	4d		M
	defb 000h		;af7d	00		.
	defb 02dh		;af7e	2d		-
	defb 02dh		;af7f	2d		-
	defb 02dh		;af80	2d		-
	defb 02dh		;af81	2d		-
	defb 02dh		;af82	2d		-
	defb 02dh		;af83	2d		-
	defb 000h		;af84	00		.
	defb 003h		;af85	03		.
	defb 014h		;af86	14		.
	defb 049h		;af87	49		I
	defb 044h		;af88	44		D
	defb 045h		;af89	45		E
	defb 020h		;af8a	20		 
	defb 053h		;af8b	53		S
	defb 06ch		;af8c	6c		l
	defb 061h		;af8d	61		a
	defb 076h		;af8e	76		v
	defb 065h		;af8f	65		e
	defb 020h		;af90	20		 
	defb 020h		;af91	20		 
	defb 020h		;af92	20		 
	defb 020h		;af93	20		 
	defb 020h		;af94	20		 
	defb 020h		;af95	20		 
	defb 020h		;af96	20		 
	defb 020h		;af97	20		 
	defb 020h		;af98	20		 
	defb 020h		;af99	20		 
	defb 020h		;af9a	20		 
	defb 020h		;af9b	20		 
	defb 020h		;af9c	20		 
	defb 020h		;af9d	20		 
	defb 020h		;af9e	20		 
	defb 020h		;af9f	20		 
	defb 020h		;afa0	20		 
	defb 020h		;afa1	20		 
	defb 03ah		;afa2	3a		:
	defb 020h		;afa3	20		 
	defb 000h		;afa4	00		.
	defb 011h		;afa5	11		.
	defb 0c0h		;afa6	c0		.
	defb 0c0h		;afa7	c0		.
	defb 080h		;afa8	80		.
	defb 0a2h		;afa9	a2		.
	defb 0e2h		;afaa	e2		.
	defb 0aeh		;afab	ae		.
	defb 020h		;afac	20		 
	defb 020h		;afad	20		 
	defb 000h		;afae	00		.
	defb 053h		;afaf	53		S
	defb 065h		;afb0	65		e
	defb 074h		;afb1	74		t
	defb 075h		;afb2	75		u
	defb 070h		;afb3	70		p
	defb 020h		;afb4	20		 
	defb 000h		;afb5	00		.
	defb 043h		;afb6	43		C
	defb 044h		;afb7	44		D
	defb 02dh		;afb8	2d		-
	defb 052h		;afb9	52		R
	defb 04fh		;afba	4f		O
	defb 04dh		;afbb	4d		M
	defb 000h		;afbc	00		.
	defb 02dh		;afbd	2d		-
	defb 02dh		;afbe	2d		-
	defb 02dh		;afbf	2d		-
	defb 02dh		;afc0	2d		-
	defb 02dh		;afc1	2d		-
	defb 02dh		;afc2	2d		-
	defb 000h		;afc3	00		.
	defb 003h		;afc4	03		.
	defb 015h		;afc5	15		.
	defb 087h		;afc6	87		.
	defb 0a0h		;afc7	a0		.
	defb 0e9h		;afc8	e9		.
	defb 0a8h		;afc9	a8		.
	defb 0e2h		;afca	e2		.
	defb 0a0h		;afcb	a0		.
	defb 020h		;afcc	20		 
	defb 0a7h		;afcd	a7		.
	defb 0a0h		;afce	a0		.
	defb 0afh		;afcf	af		.
	defb 0a8h		;afd0	a8		.
	defb 0e1h		;afd1	e1		.
	defb 0a8h		;afd2	a8		.
	defb 020h		;afd3	20		 
	defb 0adh		;afd4	ad		.
	defb 0a0h		;afd5	a0		.
	defb 020h		;afd6	20		 
	defb 048h		;afd7	48		H
	defb 044h		;afd8	44		D
	defb 044h		;afd9	44		D
	defb 020h		;afda	20		 
	defb 020h		;afdb	20		 
	defb 020h		;afdc	20		 
	defb 020h		;afdd	20		 
	defb 020h		;afde	20		 
	defb 020h		;afdf	20		 
	defb 020h		;afe0	20		 
	defb 03ah		;afe1	3a		:
	defb 020h		;afe2	20		 
	defb 000h		;afe3	00		.
	defb 01dh		;afe4	1d		.
	defb 001h		;afe5	01		.
	defb 001h		;afe6	01		.
	defb 08eh		;afe7	8e		.
	defb 0e2h		;afe8	e2		.
	defb 0aah		;afe9	aa		.
	defb 0abh		;afea	ab		.
	defb 0eeh		;afeb	ee		.
	defb 0e7h		;afec	e7		.
	defb 0a5h		;afed	a5		.
	defb 0adh		;afee	ad		.
	defb 0aeh		;afef	ae		.
	defb 000h		;aff0	00		.
	defb 082h		;aff1	82		.
	defb 0aah		;aff2	aa		.
	defb 0abh		;aff3	ab		.
	defb 0eeh		;aff4	ee		.
	defb 0e7h		;aff5	e7		.
	defb 0a5h		;aff6	a5		.
	defb 0adh		;aff7	ad		.
	defb 0aeh		;aff8	ae		.
	defb 020h		;aff9	20		 
	defb 000h		;affa	00		.
	defb 003h		;affb	03		.
	defb 016h		;affc	16		.
	defb 08fh		;affd	8f		.
	defb 0aeh		;affe	ae		.
	defb 0abh		;afff	ab		.
	defb 0aeh		;b000	ae		.
	defb 0a6h		;b001	a6		.
	defb 0a5h		;b002	a5		.
	defb 0adh		;b003	ad		.
	defb 0a8h		;b004	a8		.
	defb 0a5h		;b005	a5		.
	defb 020h		;b006	20		 
	defb 0edh		;b007	ed		.
	defb 0aah		;b008	aa		.
	defb 0e0h		;b009	e0		.
	defb 0a0h		;b00a	a0		.
	defb 0adh		;b00b	ad		.
	defb 0a0h		;b00c	a0		.
	defb 020h		;b00d	20		 
	defb 0afh		;b00e	af		.
	defb 0aeh		;b00f	ae		.
	defb 020h		;b010	20		 
	defb 059h		;b011	59		Y
	defb 020h		;b012	20		 
	defb 020h		;b013	20		 
	defb 020h		;b014	20		 
	defb 020h		;b015	20		 
	defb 020h		;b016	20		 
	defb 020h		;b017	20		 
	defb 03ah		;b018	3a		:
	defb 020h		;b019	20		 
	defb 000h		;b01a	00		.
	defb 01fh		;b01b	1f		.
	defb 0f0h		;b01c	f0		.
	defb 0e0h		;b01d	e0		.
	defb 02dh		;b01e	2d		-
	defb 037h		;b01f	37		7
	defb 000h		;b020	00		.
	defb 02dh		;b021	2d		-
	defb 036h		;b022	36		6
	defb 000h		;b023	00		.
	defb 02dh		;b024	2d		-
	defb 035h		;b025	35		5
	defb 000h		;b026	00		.
	defb 02dh		;b027	2d		-
	defb 034h		;b028	34		4
	defb 000h		;b029	00		.
	defb 02dh		;b02a	2d		-
	defb 033h		;b02b	33		3
	defb 000h		;b02c	00		.
	defb 02dh		;b02d	2d		-
	defb 032h		;b02e	32		2
	defb 000h		;b02f	00		.
	defb 02dh		;b030	2d		-
	defb 031h		;b031	31		1
	defb 000h		;b032	00		.
	defb 020h		;b033	20		 
	defb 030h		;b034	30		0
	defb 000h		;b035	00		.
	defb 02bh		;b036	2b		+
	defb 031h		;b037	31		1
	defb 000h		;b038	00		.
	defb 02bh		;b039	2b		+
	defb 032h		;b03a	32		2
	defb 000h		;b03b	00		.
	defb 02bh		;b03c	2b		+
	defb 033h		;b03d	33		3
	defb 000h		;b03e	00		.
	defb 02bh		;b03f	2b		+
	defb 034h		;b040	34		4
	defb 000h		;b041	00		.
	defb 02bh		;b042	2b		+
	defb 035h		;b043	35		5
	defb 000h		;b044	00		.
	defb 02bh		;b045	2b		+
	defb 036h		;b046	36		6
	defb 000h		;b047	00		.
	defb 02bh		;b048	2b		+
	defb 037h		;b049	37		7
	defb 000h		;b04a	00		.
	defb 003h		;b04b	03		.
	defb 017h		;b04c	17		.
	defb 08fh		;b04d	8f		.
	defb 0aeh		;b04e	ae		.
	defb 0abh		;b04f	ab		.
	defb 0aeh		;b050	ae		.
	defb 0a6h		;b051	a6		.
	defb 0a5h		;b052	a5		.
	defb 0adh		;b053	ad		.
	defb 0a8h		;b054	a8		.
	defb 0a5h		;b055	a5		.
	defb 020h		;b056	20		 
	defb 0edh		;b057	ed		.
	defb 0aah		;b058	aa		.
	defb 0e0h		;b059	e0		.
	defb 0a0h		;b05a	a0		.
	defb 0adh		;b05b	ad		.
	defb 0a0h		;b05c	a0		.
	defb 020h		;b05d	20		 
	defb 0afh		;b05e	af		.
	defb 0aeh		;b05f	ae		.
	defb 020h		;b060	20		 
	defb 058h		;b061	58		X
	defb 020h		;b062	20		 
	defb 020h		;b063	20		 
	defb 020h		;b064	20		 
	defb 020h		;b065	20		 
	defb 020h		;b066	20		 
	defb 020h		;b067	20		 
	defb 03ah		;b068	3a		:
	defb 020h		;b069	20		 
	defb 000h		;b06a	00		.
	defb 01fh		;b06b	1f		.
	defb 00fh		;b06c	0f		.
	defb 00eh		;b06d	0e		.
	defb 02bh		;b06e	2b		+
	defb 037h		;b06f	37		7
	defb 000h		;b070	00		.
	defb 02bh		;b071	2b		+
	defb 036h		;b072	36		6
	defb 000h		;b073	00		.
	defb 02bh		;b074	2b		+
	defb 035h		;b075	35		5
	defb 000h		;b076	00		.
	defb 02bh		;b077	2b		+
	defb 034h		;b078	34		4
	defb 000h		;b079	00		.
	defb 02bh		;b07a	2b		+
	defb 033h		;b07b	33		3
	defb 000h		;b07c	00		.
	defb 02bh		;b07d	2b		+
	defb 032h		;b07e	32		2
	defb 000h		;b07f	00		.
	defb 02bh		;b080	2b		+
	defb 031h		;b081	31		1
	defb 000h		;b082	00		.
	defb 020h		;b083	20		 
	defb 030h		;b084	30		0
	defb 000h		;b085	00		.
	defb 02dh		;b086	2d		-
	defb 031h		;b087	31		1
	defb 000h		;b088	00		.
	defb 02dh		;b089	2d		-
	defb 032h		;b08a	32		2
	defb 000h		;b08b	00		.
	defb 02dh		;b08c	2d		-
	defb 033h		;b08d	33		3
	defb 000h		;b08e	00		.
	defb 02dh		;b08f	2d		-
	defb 034h		;b090	34		4
	defb 000h		;b091	00		.
	defb 02dh		;b092	2d		-
	defb 035h		;b093	35		5
	defb 000h		;b094	00		.
	defb 02dh		;b095	2d		-
	defb 036h		;b096	36		6
	defb 000h		;b097	00		.
	defb 02dh		;b098	2d		-
	defb 037h		;b099	37		7
	defb 000h		;b09a	00		.
	defb 02bh		;b09b	2b		+
	defb 007h		;b09c	07		.
	defb 081h		;b09d	81		.
	defb 0ebh		;b09e	eb		.
	defb 0e1h		;b09f	e1		.
	defb 0e2h		;b0a0	e2		.
	defb 0e0h		;b0a1	e0		.
	defb 0ebh		;b0a2	eb		.
	defb 0a9h		;b0a3	a9		.
	defb 020h		;b0a4	20		 
	defb 0a7h		;b0a5	a7		.
	defb 0a0h		;b0a6	a0		.
	defb 0afh		;b0a7	af		.
	defb 0e3h		;b0a8	e3		.
	defb 0e1h		;b0a9	e1		.
	defb 0aah		;b0aa	aa		.
	defb 020h		;b0ab	20		 
	defb 08fh		;b0ac	8f		.
	defb 087h		;b0ad	87		.
	defb 093h		;b0ae	93		.
	defb 020h		;b0af	20		 
	defb 03ah		;b0b0	3a		:
	defb 020h		;b0b1	20		 
	defb 000h		;b0b2	00		.
	defb 00eh		;b0b3	0e		.
	defb 001h		;b0b4	01		.
	defb 001h		;b0b5	01		.
	defb 08eh		;b0b6	8e		.
	defb 0e2h		;b0b7	e2		.
	defb 0aah		;b0b8	aa		.
	defb 0abh		;b0b9	ab		.
	defb 0eeh		;b0ba	ee		.
	defb 0e7h		;b0bb	e7		.
	defb 0a5h		;b0bc	a5		.
	defb 0adh		;b0bd	ad		.
	defb 0aeh		;b0be	ae		.
	defb 000h		;b0bf	00		.
	defb 082h		;b0c0	82		.
	defb 0aah		;b0c1	aa		.
	defb 0abh		;b0c2	ab		.
	defb 0eeh		;b0c3	ee		.
	defb 0e7h		;b0c4	e7		.
	defb 0a5h		;b0c5	a5		.
	defb 0adh		;b0c6	ad		.
	defb 0aeh		;b0c7	ae		.
	defb 020h		;b0c8	20		 
	defb 000h		;b0c9	00		.
	defb 02bh		;b0ca	2b		+
	defb 008h		;b0cb	08		.
	defb 054h		;b0cc	54		T
	defb 052h		;b0cd	52		R
	defb 020h		;b0ce	20		 
	defb 044h		;b0cf	44		D
	defb 04fh		;b0d0	4f		O
	defb 053h		;b0d1	53		S
	defb 020h		;b0d2	20		 
	defb 041h		;b0d3	41		A
	defb 03ah		;b0d4	3a		:
	defb 03eh		;b0d5	3e		>
	defb 020h		;b0d6	20		 
	defb 020h		;b0d7	20		 
	defb 020h		;b0d8	20		 
	defb 020h		;b0d9	20		 
	defb 020h		;b0da	20		 
	defb 020h		;b0db	20		 
	defb 020h		;b0dc	20		 
	defb 020h		;b0dd	20		 
	defb 020h		;b0de	20		 
	defb 03ah		;b0df	3a		:
	defb 020h		;b0e0	20		 
	defb 000h		;b0e1	00		.
	defb 01eh		;b0e2	1e		.
	defb 003h		;b0e3	03		.
	defb 002h		;b0e4	02		.
	defb 08fh		;b0e5	8f		.
	defb 0aeh		;b0e6	ae		.
	defb 020h		;b0e7	20		 
	defb 0e3h		;b0e8	e3		.
	defb 0ach		;b0e9	ac		.
	defb 0aeh		;b0ea	ae		.
	defb 0abh		;b0eb	ab		.
	defb 0e7h		;b0ec	e7		.
	defb 0a0h		;b0ed	a0		.
	defb 0adh		;b0ee	ad		.
	defb 0a8h		;b0ef	a8		.
	defb 0eeh		;b0f0	ee		.
	defb 000h		;b0f1	00		.
	defb 046h		;b0f2	46		F
	defb 044h		;b0f3	44		D
	defb 044h		;b0f4	44		D
	defb 020h		;b0f5	20		 
	defb 020h		;b0f6	20		 
	defb 020h		;b0f7	20		 
	defb 020h		;b0f8	20		 
	defb 020h		;b0f9	20		 
	defb 020h		;b0fa	20		 
	defb 020h		;b0fb	20		 
	defb 020h		;b0fc	20		 
	defb 020h		;b0fd	20		 
	defb 000h		;b0fe	00		.
	defb 048h		;b0ff	48		H
	defb 044h		;b100	44		D
	defb 044h		;b101	44		D
	defb 020h		;b102	20		 
	defb 020h		;b103	20		 
	defb 020h		;b104	20		 
	defb 020h		;b105	20		 
	defb 020h		;b106	20		 
	defb 020h		;b107	20		 
	defb 020h		;b108	20		 
	defb 020h		;b109	20		 
	defb 020h		;b10a	20		 
	defb 000h		;b10b	00		.
	defb 02bh		;b10c	2b		+
	defb 009h		;b10d	09		.
	defb 054h		;b10e	54		T
	defb 052h		;b10f	52		R
	defb 020h		;b110	20		 
	defb 044h		;b111	44		D
	defb 04fh		;b112	4f		O
	defb 053h		;b113	53		S
	defb 020h		;b114	20		 
	defb 042h		;b115	42		B
	defb 03ah		;b116	3a		:
	defb 03eh		;b117	3e		>
	defb 020h		;b118	20		 
	defb 020h		;b119	20		 
	defb 020h		;b11a	20		 
	defb 020h		;b11b	20		 
	defb 020h		;b11c	20		 
	defb 020h		;b11d	20		 
	defb 020h		;b11e	20		 
	defb 020h		;b11f	20		 
	defb 020h		;b120	20		 
	defb 03ah		;b121	3a		:
	defb 020h		;b122	20		 
	defb 000h		;b123	00		.
	defb 01eh		;b124	1e		.
	defb 00ch		;b125	0c		.
	defb 008h		;b126	08		.
	defb 08fh		;b127	8f		.
	defb 0aeh		;b128	ae		.
	defb 020h		;b129	20		 
	defb 0e3h		;b12a	e3		.
	defb 0ach		;b12b	ac		.
	defb 0aeh		;b12c	ae		.
	defb 0abh		;b12d	ab		.
	defb 0e7h		;b12e	e7		.
	defb 0a0h		;b12f	a0		.
	defb 0adh		;b130	ad		.
	defb 0a8h		;b131	a8		.
	defb 0eeh		;b132	ee		.
	defb 000h		;b133	00		.
	defb 046h		;b134	46		F
	defb 044h		;b135	44		D
	defb 044h		;b136	44		D
	defb 020h		;b137	20		 
	defb 020h		;b138	20		 
	defb 020h		;b139	20		 
	defb 020h		;b13a	20		 
	defb 020h		;b13b	20		 
	defb 020h		;b13c	20		 
	defb 020h		;b13d	20		 
	defb 020h		;b13e	20		 
	defb 020h		;b13f	20		 
	defb 000h		;b140	00		.
	defb 048h		;b141	48		H
	defb 044h		;b142	44		D
	defb 044h		;b143	44		D
	defb 020h		;b144	20		 
	defb 020h		;b145	20		 
	defb 020h		;b146	20		 
	defb 020h		;b147	20		 
	defb 020h		;b148	20		 
	defb 020h		;b149	20		 
	defb 020h		;b14a	20		 
	defb 020h		;b14b	20		 
	defb 020h		;b14c	20		 
	defb 000h		;b14d	00		.
	defb 02bh		;b14e	2b		+
	defb 00ah		;b14f	0a		.
	defb 054h		;b150	54		T
	defb 052h		;b151	52		R
	defb 020h		;b152	20		 
	defb 044h		;b153	44		D
	defb 04fh		;b154	4f		O
	defb 053h		;b155	53		S
	defb 020h		;b156	20		 
	defb 043h		;b157	43		C
	defb 03ah		;b158	3a		:
	defb 03eh		;b159	3e		>
	defb 020h		;b15a	20		 
	defb 020h		;b15b	20		 
	defb 020h		;b15c	20		 
	defb 020h		;b15d	20		 
	defb 020h		;b15e	20		 
	defb 020h		;b15f	20		 
	defb 020h		;b160	20		 
	defb 020h		;b161	20		 
	defb 020h		;b162	20		 
	defb 03ah		;b163	3a		:
	defb 020h		;b164	20		 
	defb 000h		;b165	00		.
	defb 01eh		;b166	1e		.
	defb 030h		;b167	30		0
	defb 020h		;b168	20		 
	defb 08fh		;b169	8f		.
	defb 0aeh		;b16a	ae		.
	defb 020h		;b16b	20		 
	defb 0e3h		;b16c	e3		.
	defb 0ach		;b16d	ac		.
	defb 0aeh		;b16e	ae		.
	defb 0abh		;b16f	ab		.
	defb 0e7h		;b170	e7		.
	defb 0a0h		;b171	a0		.
	defb 0adh		;b172	ad		.
	defb 0a8h		;b173	a8		.
	defb 0eeh		;b174	ee		.
	defb 000h		;b175	00		.
	defb 046h		;b176	46		F
	defb 044h		;b177	44		D
	defb 044h		;b178	44		D
	defb 020h		;b179	20		 
	defb 020h		;b17a	20		 
	defb 020h		;b17b	20		 
	defb 020h		;b17c	20		 
	defb 020h		;b17d	20		 
	defb 020h		;b17e	20		 
	defb 020h		;b17f	20		 
	defb 020h		;b180	20		 
	defb 020h		;b181	20		 
	defb 000h		;b182	00		.
	defb 048h		;b183	48		H
	defb 044h		;b184	44		D
	defb 044h		;b185	44		D
	defb 020h		;b186	20		 
	defb 020h		;b187	20		 
	defb 020h		;b188	20		 
	defb 020h		;b189	20		 
	defb 020h		;b18a	20		 
	defb 020h		;b18b	20		 
	defb 020h		;b18c	20		 
	defb 020h		;b18d	20		 
	defb 020h		;b18e	20		 
	defb 000h		;b18f	00		.
	defb 02bh		;b190	2b		+
	defb 00bh		;b191	0b		.
	defb 054h		;b192	54		T
	defb 052h		;b193	52		R
	defb 020h		;b194	20		 
	defb 044h		;b195	44		D
	defb 04fh		;b196	4f		O
	defb 053h		;b197	53		S
	defb 020h		;b198	20		 
	defb 044h		;b199	44		D
	defb 03ah		;b19a	3a		:
	defb 03eh		;b19b	3e		>
	defb 020h		;b19c	20		 
	defb 020h		;b19d	20		 
	defb 020h		;b19e	20		 
	defb 020h		;b19f	20		 
	defb 020h		;b1a0	20		 
	defb 020h		;b1a1	20		 
	defb 020h		;b1a2	20		 
	defb 020h		;b1a3	20		 
	defb 020h		;b1a4	20		 
	defb 03ah		;b1a5	3a		:
	defb 020h		;b1a6	20		 
	defb 000h		;b1a7	00		.
	defb 01eh		;b1a8	1e		.
	defb 0c0h		;b1a9	c0		.
	defb 080h		;b1aa	80		.
	defb 08fh		;b1ab	8f		.
	defb 0aeh		;b1ac	ae		.
	defb 020h		;b1ad	20		 
	defb 0e3h		;b1ae	e3		.
	defb 0ach		;b1af	ac		.
	defb 0aeh		;b1b0	ae		.
	defb 0abh		;b1b1	ab		.
	defb 0e7h		;b1b2	e7		.
	defb 0a0h		;b1b3	a0		.
	defb 0adh		;b1b4	ad		.
	defb 0a8h		;b1b5	a8		.
	defb 0eeh		;b1b6	ee		.
	defb 000h		;b1b7	00		.
	defb 046h		;b1b8	46		F
	defb 044h		;b1b9	44		D
	defb 044h		;b1ba	44		D
	defb 020h		;b1bb	20		 
	defb 020h		;b1bc	20		 
	defb 020h		;b1bd	20		 
	defb 020h		;b1be	20		 
	defb 020h		;b1bf	20		 
	defb 020h		;b1c0	20		 
	defb 020h		;b1c1	20		 
	defb 020h		;b1c2	20		 
	defb 020h		;b1c3	20		 
	defb 000h		;b1c4	00		.
	defb 048h		;b1c5	48		H
	defb 044h		;b1c6	44		D
	defb 044h		;b1c7	44		D
	defb 020h		;b1c8	20		 
	defb 020h		;b1c9	20		 
	defb 020h		;b1ca	20		 
	defb 020h		;b1cb	20		 
	defb 020h		;b1cc	20		 
	defb 020h		;b1cd	20		 
	defb 020h		;b1ce	20		 
	defb 020h		;b1cf	20		 
	defb 020h		;b1d0	20		 
	defb 000h		;b1d1	00		.
EITMR:
				; = EITMR (src: DSETUP.ASM:1667, BIOS-PP 1273243)
				; = MSGRUS (src: DSETUP.ASM:1669, BIOS-PP 1273243)
	defb 000h		;b1d2	00		.
	defb 000h		;b1d3	00		.
	defb 043h		;b1d4	43		C
	defb 06fh		;b1d5	6f		o
	defb 070h		;b1d6	70		p
	defb 079h		;b1d7	79		y
	defb 072h		;b1d8	72		r
	defb 069h		;b1d9	69		i
	defb 067h		;b1da	67		g
	defb 068h		;b1db	68		h
	defb 074h		;b1dc	74		t
	defb 020h		;b1dd	20		 
	defb 028h		;b1de	28		(
	defb 063h		;b1df	63		c
	defb 029h		;b1e0	29		)
	defb 020h		;b1e1	20		 
	defb 032h		;b1e2	32		2
	defb 030h		;b1e3	30		0
	defb 030h		;b1e4	30		0
	defb 032h		;b1e5	32		2
	defb 020h		;b1e6	20		 
	defb 08fh		;b1e7	8f		.
	defb 085h		;b1e8	85		.
	defb 092h		;b1e9	92		.
	defb 085h		;b1ea	85		.
	defb 090h		;b1eb	90		.
	defb 091h		;b1ec	91		.
	defb 020h		;b1ed	20		 
	defb 08fh		;b1ee	8f		.
	defb 0abh		;b1ef	ab		.
	defb 0eeh		;b1f0	ee		.
	defb 0e1h		;b1f1	e1		.
	defb 02eh		;b1f2	2e		.
	defb 000h		;b1f3	00		.
	defb 082h		;b1f4	82		.
	defb 0e1h		;b1f5	e1		.
	defb 0a5h		;b1f6	a5		.
	defb 020h		;b1f7	20		 
	defb 0afh		;b1f8	af		.
	defb 0e0h		;b1f9	e0		.
	defb 0a0h		;b1fa	a0		.
	defb 0a2h		;b1fb	a2		.
	defb 0a0h		;b1fc	a0		.
	defb 020h		;b1fd	20		 
	defb 0e1h		;b1fe	e1		.
	defb 0aeh		;b1ff	ae		.
	defb 0e5h		;b200	e5		.
	defb 0e0h		;b201	e0		.
	defb 0a0h		;b202	a0		.
	defb 0adh		;b203	ad		.
	defb 0efh		;b204	ef		.
	defb 0eeh		;b205	ee		.
	defb 0e2h		;b206	e2		.
	defb 0e1h		;b207	e1		.
	defb 0efh		;b208	ef		.
	defb 000h		;b209	00		.
	defb 053h		;b20a	53		S
	defb 050h		;b20b	50		P
	defb 052h		;b20c	52		R
	defb 049h		;b20d	49		I
	defb 04eh		;b20e	4e		N
	defb 054h		;b20f	54		T
	defb 045h		;b210	45		E
	defb 052h		;b211	52		R
	defb 020h		;b212	20		 
	defb 053h		;b213	53		S
	defb 045h		;b214	45		E
	defb 054h		;b215	54		T
	defb 055h		;b216	55		U
	defb 050h		;b217	50		P
	defb 020h		;b218	20		 
	defb 055h		;b219	55		U
	defb 054h		;b21a	54		T
	defb 049h		;b21b	49		I
	defb 04ch		;b21c	4c		L
	defb 049h		;b21d	49		I
	defb 054h		;b21e	54		T
	defb 059h		;b21f	59		Y
	defb 020h		;b220	20		 
	defb 056h		;b221	56		V
	defb 065h		;b222	65		e
	defb 072h		;b223	72		r
	defb 073h		;b224	73		s
	defb 069h		;b225	69		i
	defb 06fh		;b226	6f		o
	defb 06eh		;b227	6e		n
	defb 020h		;b228	20		 
	defb 031h		;b229	31		1
	defb 02eh		;b22a	2e		.
	defb 035h		;b22b	35		5
	defb 038h		;b22c	38		8
	defb 000h		;b22d	00		.
	defb 028h		;b22e	28		(
	defb 043h		;b22f	43		C
	defb 029h		;b230	29		)
	defb 020h		;b231	20		 
	defb 032h		;b232	32		2
	defb 030h		;b233	30		0
	defb 030h		;b234	30		0
	defb 032h		;b235	32		2
	defb 020h		;b236	20		 
	defb 08fh		;b237	8f		.
	defb 085h		;b238	85		.
	defb 092h		;b239	92		.
	defb 085h		;b23a	85		.
	defb 090h		;b23b	90		.
	defb 091h		;b23c	91		.
	defb 020h		;b23d	20		 
	defb 08fh		;b23e	8f		.
	defb 0abh		;b23f	ab		.
	defb 0eeh		;b240	ee		.
	defb 0e1h		;b241	e1		.
	defb 02eh		;b242	2e		.
	defb 000h		;b243	00		.
	defb 093h		;b244	93		.
	defb 091h		;b245	91		.
	defb 092h		;b246	92		.
	defb 080h		;b247	80		.
	defb 08dh		;b248	8d		.
	defb 08eh		;b249	8e		.
	defb 082h		;b24a	82		.
	defb 08ah		;b24b	8a		.
	defb 088h		;b24c	88		.
	defb 020h		;b24d	20		 
	defb 08eh		;b24e	8e		.
	defb 091h		;b24f	91		.
	defb 08eh		;b250	8e		.
	defb 081h		;b251	81		.
	defb 085h		;b252	85		.
	defb 08dh		;b253	8d		.
	defb 08dh		;b254	8d		.
	defb 08eh		;b255	8e		.
	defb 091h		;b256	91		.
	defb 092h		;b257	92		.
	defb 085h		;b258	85		.
	defb 089h		;b259	89		.
	defb 020h		;b25a	20		 
	defb 082h		;b25b	82		.
	defb 08ah		;b25c	8a		.
	defb 08bh		;b25d	8b		.
	defb 09eh		;b25e	9e		.
	defb 097h		;b25f	97		.
	defb 085h		;b260	85		.
	defb 08dh		;b261	8d		.
	defb 088h		;b262	88		.
	defb 09fh		;b263	9f		.
	defb 000h		;b264	00		.
	defb 045h		;b265	45		E
	defb 053h		;b266	53		S
	defb 043h		;b267	43		C
	defb 020h		;b268	20		 
	defb 03ah		;b269	3a		:
	defb 020h		;b26a	20		 
	defb 082h		;b26b	82		.
	defb 0ebh		;b26c	eb		.
	defb 0a9h		;b26d	a9		.
	defb 0e2h		;b26e	e2		.
	defb 0a8h		;b26f	a8		.
	defb 020h		;b270	20		 
	defb 020h		;b271	20		 
	defb 020h		;b272	20		 
	defb 020h		;b273	20		 
	defb 020h		;b274	20		 
	defb 020h		;b275	20		 
	defb 020h		;b276	20		 
	defb 020h		;b277	20		 
	defb 020h		;b278	20		 
	defb 020h		;b279	20		 
	defb 020h		;b27a	20		 
	defb 020h		;b27b	20		 
	defb 020h		;b27c	20		 
	defb 020h		;b27d	20		 
	defb 020h		;b27e	20		 
	defb 020h		;b27f	20		 
	defb 020h		;b280	20		 
	defb 020h		;b281	20		 
	defb 020h		;b282	20		 
	defb 020h		;b283	20		 
	defb 020h		;b284	20		 
	defb 020h		;b285	20		 
	defb 020h		;b286	20		 
	defb 020h		;b287	20		 
	defb 020h		;b288	20		 
	defb 020h		;b289	20		 
	defb 020h		;b28a	20		 
	defb 020h		;b28b	20		 
	defb 020h		;b28c	20		 
	defb 020h		;b28d	20		 
	defb 046h		;b28e	46		F
	defb 031h		;b28f	31		1
	defb 030h		;b290	30		0
	defb 020h		;b291	20		 
	defb 03ah		;b292	3a		:
	defb 020h		;b293	20		 
	defb 091h		;b294	91		.
	defb 0aeh		;b295	ae		.
	defb 0e5h		;b296	e5		.
	defb 0e0h		;b297	e0		.
	defb 0a0h		;b298	a0		.
	defb 0adh		;b299	ad		.
	defb 0a8h		;b29a	a8		.
	defb 0e2h		;b29b	e2		.
	defb 0ech		;b29c	ec		.
	defb 020h		;b29d	20		 
	defb 0a8h		;b29e	a8		.
	defb 020h		;b29f	20		 
	defb 0a2h		;b2a0	a2		.
	defb 0ebh		;b2a1	eb		.
	defb 0a9h		;b2a2	a9		.
	defb 0e2h		;b2a3	e2		.
	defb 0a8h		;b2a4	a8		.
	defb 000h		;b2a5	00		.
	defb 046h		;b2a6	46		F
	defb 032h		;b2a7	32		2
	defb 020h		;b2a8	20		 
	defb 020h		;b2a9	20		 
	defb 03ah		;b2aa	3a		:
	defb 020h		;b2ab	20		 
	defb 091h		;b2ac	91		.
	defb 0aeh		;b2ad	ae		.
	defb 0e5h		;b2ae	e5		.
	defb 0e0h		;b2af	e0		.
	defb 0a0h		;b2b0	a0		.
	defb 0adh		;b2b1	ad		.
	defb 0a8h		;b2b2	a8		.
	defb 0e2h		;b2b3	e2		.
	defb 0ech		;b2b4	ec		.
	defb 020h		;b2b5	20		 
	defb 0a7h		;b2b6	a7		.
	defb 0adh		;b2b7	ad		.
	defb 0a0h		;b2b8	a0		.
	defb 0e7h		;b2b9	e7		.
	defb 0a5h		;b2ba	a5		.
	defb 0adh		;b2bb	ad		.
	defb 0a8h		;b2bc	a8		.
	defb 0efh		;b2bd	ef		.
	defb 020h		;b2be	20		 
	defb 020h		;b2bf	20		 
	defb 020h		;b2c0	20		 
	defb 020h		;b2c1	20		 
	defb 020h		;b2c2	20		 
	defb 020h		;b2c3	20		 
	defb 020h		;b2c4	20		 
	defb 020h		;b2c5	20		 
	defb 020h		;b2c6	20		 
	defb 020h		;b2c7	20		 
	defb 020h		;b2c8	20		 
	defb 020h		;b2c9	20		 
	defb 020h		;b2ca	20		 
	defb 018h		;b2cb	18		.
	defb 020h		;b2cc	20		 
	defb 019h		;b2cd	19		.
	defb 020h		;b2ce	20		 
	defb 01ah		;b2cf	1a		.
	defb 020h		;b2d0	20		 
	defb 01bh		;b2d1	1b		.
	defb 020h		;b2d2	20		 
	defb 03ah		;b2d3	3a		:
	defb 020h		;b2d4	20		 
	defb 082h		;b2d5	82		.
	defb 0ebh		;b2d6	eb		.
	defb 0a1h		;b2d7	a1		.
	defb 0aeh		;b2d8	ae		.
	defb 0e0h		;b2d9	e0		.
	defb 020h		;b2da	20		 
	defb 0afh		;b2db	af		.
	defb 0e3h		;b2dc	e3		.
	defb 0adh		;b2dd	ad		.
	defb 0aah		;b2de	aa		.
	defb 0e2h		;b2df	e2		.
	defb 0a0h		;b2e0	a0		.
	defb 000h		;b2e1	00		.
	defb 046h		;b2e2	46		F
	defb 035h		;b2e3	35		5
	defb 020h		;b2e4	20		 
	defb 020h		;b2e5	20		 
	defb 03ah		;b2e6	3a		:
	defb 020h		;b2e7	20		 
	defb 091h		;b2e8	91		.
	defb 0e2h		;b2e9	e2		.
	defb 0a0h		;b2ea	a0		.
	defb 0e0h		;b2eb	e0		.
	defb 0ebh		;b2ec	eb		.
	defb 0a5h		;b2ed	a5		.
	defb 020h		;b2ee	20		 
	defb 0a7h		;b2ef	a7		.
	defb 0adh		;b2f0	ad		.
	defb 0a0h		;b2f1	a0		.
	defb 0e7h		;b2f2	e7		.
	defb 0a5h		;b2f3	a5		.
	defb 0adh		;b2f4	ad		.
	defb 0a8h		;b2f5	a8		.
	defb 0efh		;b2f6	ef		.
	defb 020h		;b2f7	20		 
	defb 020h		;b2f8	20		 
	defb 020h		;b2f9	20		 
	defb 020h		;b2fa	20		 
	defb 020h		;b2fb	20		 
	defb 020h		;b2fc	20		 
	defb 020h		;b2fd	20		 
	defb 020h		;b2fe	20		 
	defb 020h		;b2ff	20		 
	defb 020h		;b300	20		 
	defb 020h		;b301	20		 
	defb 020h		;b302	20		 
	defb 020h		;b303	20		 
	defb 020h		;b304	20		 
	defb 050h		;b305	50		P
	defb 055h		;b306	55		U
	defb 02fh		;b307	2f		/
	defb 050h		;b308	50		P
	defb 044h		;b309	44		D
	defb 02fh		;b30a	2f		/
	defb 02bh		;b30b	2b		+
	defb 02fh		;b30c	2f		/
	defb 02dh		;b30d	2d		-
	defb 020h		;b30e	20		 
	defb 03ah		;b30f	3a		:
	defb 020h		;b310	20		 
	defb 088h		;b311	88		.
	defb 0a7h		;b312	a7		.
	defb 0ach		;b313	ac		.
	defb 0a5h		;b314	a5		.
	defb 0adh		;b315	ad		.
	defb 0a5h		;b316	a5		.
	defb 0adh		;b317	ad		.
	defb 0a8h		;b318	a8		.
	defb 0a5h		;b319	a5		.
	defb 000h		;b31a	00		.
	defb 046h		;b31b	46		F
	defb 037h		;b31c	37		7
	defb 020h		;b31d	20		 
	defb 020h		;b31e	20		 
	defb 03ah		;b31f	3a		:
	defb 020h		;b320	20		 
	defb 087h		;b321	87		.
	defb 0adh		;b322	ad		.
	defb 0a0h		;b323	a0		.
	defb 0e7h		;b324	e7		.
	defb 0a5h		;b325	a5		.
	defb 0adh		;b326	ad		.
	defb 0a8h		;b327	a8		.
	defb 0efh		;b328	ef		.
	defb 020h		;b329	20		 
	defb 0afh		;b32a	af		.
	defb 0aeh		;b32b	ae		.
	defb 020h		;b32c	20		 
	defb 0e3h		;b32d	e3		.
	defb 0ach		;b32e	ac		.
	defb 0aeh		;b32f	ae		.
	defb 0abh		;b330	ab		.
	defb 0e7h		;b331	e7		.
	defb 0a0h		;b332	a0		.
	defb 0adh		;b333	ad		.
	defb 0a8h		;b334	a8		.
	defb 0eeh		;b335	ee		.
	defb 020h		;b336	20		 
	defb 020h		;b337	20		 
	defb 020h		;b338	20		 
	defb 020h		;b339	20		 
	defb 020h		;b33a	20		 
	defb 020h		;b33b	20		 
	defb 020h		;b33c	20		 
	defb 020h		;b33d	20		 
	defb 020h		;b33e	20		 
	defb 020h		;b33f	20		 
	defb 020h		;b340	20		 
	defb 020h		;b341	20		 
	defb 020h		;b342	20		 
	defb 020h		;b343	20		 
	defb 020h		;b344	20		 
	defb 046h		;b345	46		F
	defb 033h		;b346	33		3
	defb 020h		;b347	20		 
	defb 03ah		;b348	3a		:
	defb 020h		;b349	20		 
	defb 096h		;b34a	96		.
	defb 0a2h		;b34b	a2		.
	defb 0a5h		;b34c	a5		.
	defb 0e2h		;b34d	e2		.
	defb 0a0h		;b34e	a0		.
	defb 020h		;b34f	20		 
	defb 000h		;b350	00		.
	defb 08dh		;b351	8d		.
	defb 0a0h		;b352	a0		.
	defb 0a6h		;b353	a6		.
	defb 0ach		;b354	ac		.
	defb 0a8h		;b355	a8		.
	defb 0e2h		;b356	e2		.
	defb 0a5h		;b357	a5		.
	defb 020h		;b358	20		 
	defb 03ch		;b359	3c		<
	defb 044h		;b35a	44		D
	defb 045h		;b35b	45		E
	defb 04ch		;b35c	4c		L
	defb 03eh		;b35d	3e		>
	defb 020h		;b35e	20		 
	defb 0a4h		;b35f	a4		.
	defb 0abh		;b360	ab		.
	defb 0efh		;b361	ef		.
	defb 020h		;b362	20		 
	defb 0a2h		;b363	a2		.
	defb 0e5h		;b364	e5		.
	defb 0aeh		;b365	ae		.
	defb 0a4h		;b366	a4		.
	defb 0a0h		;b367	a0		.
	defb 020h		;b368	20		 
	defb 0a2h		;b369	a2		.
	defb 020h		;b36a	20		 
	defb 053h		;b36b	53		S
	defb 045h		;b36c	45		E
	defb 054h		;b36d	54		T
	defb 055h		;b36e	55		U
	defb 050h		;b36f	50		P
	defb 000h		;b370	00		.
	defb 08dh		;b371	8d		.
	defb 0a0h		;b372	a0		.
	defb 0a6h		;b373	a6		.
	defb 0ach		;b374	ac		.
	defb 0a8h		;b375	a8		.
	defb 0e2h		;b376	e2		.
	defb 0a5h		;b377	a5		.
	defb 020h		;b378	20		 
	defb 03ch		;b379	3c		<
	defb 041h		;b37a	41		A
	defb 04ch		;b37b	4c		L
	defb 054h		;b37c	54		T
	defb 03eh		;b37d	3e		>
	defb 020h		;b37e	20		 
	defb 0a4h		;b37f	a4		.
	defb 0abh		;b380	ab		.
	defb 0efh		;b381	ef		.
	defb 020h		;b382	20		 
	defb 080h		;b383	80		.
	defb 0abh		;b384	ab		.
	defb 0e2h		;b385	e2		.
	defb 02eh		;b386	2e		.
	defb 020h		;b387	20		 
	defb 091h		;b388	91		.
	defb 0a8h		;b389	a8		.
	defb 0e1h		;b38a	e1		.
	defb 0e2h		;b38b	e2		.
	defb 0a5h		;b38c	a5		.
	defb 0ach		;b38d	ac		.
	defb 0adh		;b38e	ad		.
	defb 0aeh		;b38f	ae		.
	defb 0a3h		;b390	a3		.
	defb 0aeh		;b391	ae		.
	defb 020h		;b392	20		 
	defb 0a4h		;b393	a4		.
	defb 0a8h		;b394	a8		.
	defb 0e1h		;b395	e1		.
	defb 0aah		;b396	aa		.
	defb 0a0h		;b397	a0		.
	defb 000h		;b398	00		.
	defb 031h		;b399	31		1
	defb 032h		;b39a	32		2
	defb 033h		;b39b	33		3
	defb 034h		;b39c	34		4
	defb 035h		;b39d	35		5
	defb 036h		;b39e	36		6
	defb 037h		;b39f	37		7
	defb 038h		;b3a0	38		8
	defb 000h		;b3a1	00		.
	defb 082h		;b3a2	82		.
	defb 08dh		;b3a3	8d		.
	defb 088h		;b3a4	88		.
	defb 08ch		;b3a5	8c		.
	defb 080h		;b3a6	80		.
	defb 08dh		;b3a7	8d		.
	defb 088h		;b3a8	88		.
	defb 085h		;b3a9	85		.
	defb 021h		;b3aa	21		!
	defb 020h		;b3ab	20		 
	defb 08eh		;b3ac	8e		.
	defb 098h		;b3ad	98		.
	defb 088h		;b3ae	88		.
	defb 081h		;b3af	81		.
	defb 08ah		;b3b0	8a		.
	defb 080h		;b3b1	80		.
	defb 020h		;b3b2	20		 
	defb 08ah		;b3b3	8a		.
	defb 08eh		;b3b4	8e		.
	defb 08dh		;b3b5	8d		.
	defb 092h		;b3b6	92		.
	defb 090h		;b3b7	90		.
	defb 08eh		;b3b8	8e		.
	defb 08bh		;b3b9	8b		.
	defb 09ch		;b3ba	9c		.
	defb 08dh		;b3bb	8d		.
	defb 08eh		;b3bc	8e		.
	defb 089h		;b3bd	89		.
	defb 020h		;b3be	20		 
	defb 091h		;b3bf	91		.
	defb 093h		;b3c0	93		.
	defb 08ch		;b3c1	8c		.
	defb 08ch		;b3c2	8c		.
	defb 09bh		;b3c3	9b		.
	defb 020h		;b3c4	20		 
	defb 043h		;b3c5	43		C
	defb 04dh		;b3c6	4d		M
	defb 04fh		;b3c7	4f		O
	defb 053h		;b3c8	53		S
	defb 02ch		;b3c9	2c		,
	defb 020h		;b3ca	20		 
	defb 093h		;b3cb	93		.
	defb 091h		;b3cc	91		.
	defb 092h		;b3cd	92		.
	defb 080h		;b3ce	80		.
	defb 08dh		;b3cf	8d		.
	defb 08eh		;b3d0	8e		.
	defb 082h		;b3d1	82		.
	defb 08bh		;b3d2	8b		.
	defb 085h		;b3d3	85		.
	defb 08dh		;b3d4	8d		.
	defb 09bh		;b3d5	9b		.
	defb 020h		;b3d6	20		 
	defb 087h		;b3d7	87		.
	defb 08dh		;b3d8	8d		.
	defb 080h		;b3d9	80		.
	defb 097h		;b3da	97		.
	defb 085h		;b3db	85		.
	defb 08dh		;b3dc	8d		.
	defb 088h		;b3dd	88		.
	defb 09fh		;b3de	9f		.
	defb 020h		;b3df	20		 
	defb 08fh		;b3e0	8f		.
	defb 08eh		;b3e1	8e		.
	defb 020h		;b3e2	20		 
	defb 093h		;b3e3	93		.
	defb 08ch		;b3e4	8c		.
	defb 08eh		;b3e5	8e		.
	defb 08bh		;b3e6	8b		.
	defb 097h		;b3e7	97		.
	defb 080h		;b3e8	80		.
	defb 08dh		;b3e9	8d		.
	defb 088h		;b3ea	88		.
	defb 09eh		;b3eb	9e		.
	defb 000h		;b3ec	00		.
	defb 08eh		;b3ed	8e		.
	defb 0afh		;b3ee	af		.
	defb 0e0h		;b3ef	e0		.
	defb 0a5h		;b3f0	a5		.
	defb 0a4h		;b3f1	a4		.
	defb 0a5h		;b3f2	a5		.
	defb 0abh		;b3f3	ab		.
	defb 0a5h		;b3f4	a5		.
	defb 0adh		;b3f5	ad		.
	defb 0a8h		;b3f6	a8		.
	defb 0a5h		;b3f7	a5		.
	defb 020h		;b3f8	20		 
	defb 08fh		;b3f9	8f		.
	defb 0a5h		;b3fa	a5		.
	defb 0e0h		;b3fb	e0		.
	defb 0a2h		;b3fc	a2		.
	defb 0a8h		;b3fd	a8		.
	defb 0e7h		;b3fe	e7		.
	defb 0adh		;b3ff	ad		.
	defb 0aeh		;b400	ae		.
	defb 0a3h		;b401	a3		.
	defb 0aeh		;b402	ae		.
	defb 020h		;b403	20		 
	defb 049h		;b404	49		I
	defb 044h		;b405	44		D
	defb 045h		;b406	45		E
	defb 020h		;b407	20		 
	defb 04dh		;b408	4d		M
	defb 061h		;b409	61		a
	defb 073h		;b40a	73		s
	defb 074h		;b40b	74		t
	defb 065h		;b40c	65		e
	defb 072h		;b40d	72		r
	defb 02eh		;b40e	2e		.
	defb 02eh		;b40f	2e		.
	defb 02eh		;b410	2e		.
	defb 05bh		;b411	5b		[
	defb 046h		;b412	46		F
	defb 034h		;b413	34		4
	defb 020h		;b414	20		 
	defb 0a4h		;b415	a4		.
	defb 0abh		;b416	ab		.
	defb 0efh		;b417	ef		.
	defb 020h		;b418	20		 
	defb 0afh		;b419	af		.
	defb 0e0h		;b41a	e0		.
	defb 0aeh		;b41b	ae		.
	defb 0afh		;b41c	af		.
	defb 0e3h		;b41d	e3		.
	defb 0e1h		;b41e	e1		.
	defb 0aah		;b41f	aa		.
	defb 0a0h		;b420	a0		.
	defb 05dh		;b421	5d		]
	defb 020h		;b422	20		 
	defb 000h		;b423	00		.
	defb 08eh		;b424	8e		.
	defb 0afh		;b425	af		.
	defb 0e0h		;b426	e0		.
	defb 0a5h		;b427	a5		.
	defb 0a4h		;b428	a4		.
	defb 0a5h		;b429	a5		.
	defb 0abh		;b42a	ab		.
	defb 0a5h		;b42b	a5		.
	defb 0adh		;b42c	ad		.
	defb 0a8h		;b42d	a8		.
	defb 0a5h		;b42e	a5		.
	defb 020h		;b42f	20		 
	defb 08fh		;b430	8f		.
	defb 0a5h		;b431	a5		.
	defb 0e0h		;b432	e0		.
	defb 0a2h		;b433	a2		.
	defb 0a8h		;b434	a8		.
	defb 0e7h		;b435	e7		.
	defb 0adh		;b436	ad		.
	defb 0aeh		;b437	ae		.
	defb 0a3h		;b438	a3		.
	defb 0aeh		;b439	ae		.
	defb 020h		;b43a	20		 
	defb 049h		;b43b	49		I
	defb 044h		;b43c	44		D
	defb 045h		;b43d	45		E
	defb 020h		;b43e	20		 
	defb 053h		;b43f	53		S
	defb 06ch		;b440	6c		l
	defb 061h		;b441	61		a
	defb 076h		;b442	76		v
	defb 065h		;b443	65		e
	defb 020h		;b444	20		 
	defb 02eh		;b445	2e		.
	defb 02eh		;b446	2e		.
	defb 02eh		;b447	2e		.
	defb 05bh		;b448	5b		[
	defb 046h		;b449	46		F
	defb 034h		;b44a	34		4
	defb 020h		;b44b	20		 
	defb 0a4h		;b44c	a4		.
	defb 0abh		;b44d	ab		.
	defb 0efh		;b44e	ef		.
	defb 020h		;b44f	20		 
	defb 0afh		;b450	af		.
	defb 0e0h		;b451	e0		.
	defb 0aeh		;b452	ae		.
	defb 0afh		;b453	af		.
	defb 0e3h		;b454	e3		.
	defb 0e1h		;b455	e1		.
	defb 0aah		;b456	aa		.
	defb 0a0h		;b457	a0		.
	defb 05dh		;b458	5d		]
	defb 020h		;b459	20		 
	defb 000h		;b45a	00		.
	defb 000h		;b45b	00		.
	defb 000h		;b45c	00		.
	defb 08dh		;b45d	8d		.
	defb 0a5h		;b45e	a5		.
	defb 0a8h		;b45f	a8		.
	defb 0a7h		;b460	a7		.
	defb 0a2h		;b461	a2		.
	defb 0a5h		;b462	a5		.
	defb 0e1h		;b463	e1		.
	defb 0e2h		;b464	e2		.
	defb 0adh		;b465	ad		.
	defb 0ebh		;b466	eb		.
	defb 0a9h		;b467	a9		.
	defb 020h		;b468	20		 
	defb 020h		;b469	20		 
	defb 020h		;b46a	20		 
	defb 020h		;b46b	20		 
	defb 020h		;b46c	20		 
	defb 020h		;b46d	20		 
	defb 020h		;b46e	20		 
	defb 000h		;b46f	00		.
	defb 08dh		;b470	8d		.
	defb 0a5h		;b471	a5		.
	defb 0e2h		;b472	e2		.
	defb 020h		;b473	20		 
	defb 020h		;b474	20		 
	defb 020h		;b475	20		 
	defb 020h		;b476	20		 
	defb 020h		;b477	20		 
	defb 020h		;b478	20		 
	defb 020h		;b479	20		 
	defb 020h		;b47a	20		 
	defb 020h		;b47b	20		 
	defb 020h		;b47c	20		 
	defb 020h		;b47d	20		 
	defb 020h		;b47e	20		 
	defb 020h		;b47f	20		 
	defb 020h		;b480	20		 
	defb 020h		;b481	20		 
	defb 000h		;b482	00		.
	defb 08fh		;b483	8f		.
	defb 0e0h		;b484	e0		.
	defb 0aeh		;b485	ae		.
	defb 0afh		;b486	af		.
	defb 0e3h		;b487	e3		.
	defb 0e9h		;b488	e9		.
	defb 0a5h		;b489	a5		.
	defb 0adh		;b48a	ad		.
	defb 020h		;b48b	20		 
	defb 020h		;b48c	20		 
	defb 020h		;b48d	20		 
	defb 020h		;b48e	20		 
	defb 020h		;b48f	20		 
	defb 020h		;b490	20		 
	defb 020h		;b491	20		 
	defb 020h		;b492	20		 
	defb 020h		;b493	20		 
	defb 020h		;b494	20		 
	defb 000h		;b495	00		.
	defb 08dh		;b496	8d		.
	defb 0a5h		;b497	a5		.
	defb 020h		;b498	20		 
	defb 0aeh		;b499	ae		.
	defb 0afh		;b49a	af		.
	defb 0e0h		;b49b	e0		.
	defb 0a5h		;b49c	a5		.
	defb 0a4h		;b49d	a4		.
	defb 0a5h		;b49e	a5		.
	defb 0abh		;b49f	ab		.
	defb 0f1h		;b4a0	f1		.
	defb 0adh		;b4a1	ad		.
	defb 020h		;b4a2	20		 
	defb 020h		;b4a3	20		 
	defb 020h		;b4a4	20		 
	defb 020h		;b4a5	20		 
	defb 020h		;b4a6	20		 
	defb 020h		;b4a7	20		 
	defb 000h		;b4a8	00		.
	defb 08ch		;b4a9	8c		.
	defb 0aeh		;b4aa	ae		.
	defb 0a4h		;b4ab	a4		.
	defb 0a5h		;b4ac	a5		.
	defb 0abh		;b4ad	ab		.
	defb 0ech		;b4ae	ec		.
	defb 020h		;b4af	20		 
	defb 020h		;b4b0	20		 
	defb 020h		;b4b1	20		 
	defb 020h		;b4b2	20		 
	defb 03ah		;b4b3	3a		:
	defb 020h		;b4b4	20		 
	defb 000h		;b4b5	00		.
	defb 08fh		;b4b6	8f		.
	defb 0a0h		;b4b7	a0		.
	defb 0ach		;b4b8	ac		.
	defb 0efh		;b4b9	ef		.
	defb 0e2h		;b4ba	e2		.
	defb 0ech		;b4bb	ec		.
	defb 020h		;b4bc	20		 
	defb 020h		;b4bd	20		 
	defb 020h		;b4be	20		 
	defb 020h		;b4bf	20		 
	defb 03ah		;b4c0	3a		:
	defb 020h		;b4c1	20		 
	defb 000h		;b4c2	00		.
	defb 043h		;b4c3	43		C
	defb 04dh		;b4c4	4d		M
	defb 04fh		;b4c5	4f		O
	defb 053h		;b4c6	53		S
	defb 020h		;b4c7	20		 
	defb 020h		;b4c8	20		 
	defb 020h		;b4c9	20		 
	defb 020h		;b4ca	20		 
	defb 020h		;b4cb	20		 
	defb 020h		;b4cc	20		 
	defb 03ah		;b4cd	3a		:
	defb 020h		;b4ce	20		 
	defb 08dh		;b4cf	8d		.
	defb 0a0h		;b4d0	a0		.
	defb 0a9h		;b4d1	a9		.
	defb 0a4h		;b4d2	a4		.
	defb 0a5h		;b4d3	a5		.
	defb 0adh		;b4d4	ad		.
	defb 000h		;b4d5	00		.
	defb 043h		;b4d6	43		C
	defb 04dh		;b4d7	4d		M
	defb 04fh		;b4d8	4f		O
	defb 053h		;b4d9	53		S
	defb 020h		;b4da	20		 
	defb 020h		;b4db	20		 
	defb 020h		;b4dc	20		 
	defb 020h		;b4dd	20		 
	defb 020h		;b4de	20		 
	defb 020h		;b4df	20		 
	defb 03ah		;b4e0	3a		:
	defb 020h		;b4e1	20		 
	defb 08dh		;b4e2	8d		.
	defb 0a5h		;b4e3	a5		.
	defb 0e2h		;b4e4	e2		.
	defb 000h		;b4e5	00		.
	defb 084h		;b4e6	84		.
	defb 0aeh		;b4e7	ae		.
	defb 0e1h		;b4e8	e1		.
	defb 0e2h		;b4e9	e2		.
	defb 0e3h		;b4ea	e3		.
	defb 0afh		;b4eb	af		.
	defb 0adh		;b4ec	ad		.
	defb 0aeh		;b4ed	ae		.
	defb 020h		;b4ee	20		 
	defb 020h		;b4ef	20		 
	defb 03ah		;b4f0	3a		:
	defb 020h		;b4f1	20		 
	defb 000h		;b4f2	00		.
	defb 037h		;b4f3	37		7
	defb 000h		;b4f4	00		.
	defb 038h		;b4f5	38		8
	defb 000h		;b4f6	00		.
	defb 049h		;b4f7	49		I
	defb 044h		;b4f8	44		D
	defb 020h		;b4f9	20		 
	defb 0afh		;b4fa	af		.
	defb 0abh		;b4fb	ab		.
	defb 0a0h		;b4fc	a0		.
	defb 0e2h		;b4fd	e2		.
	defb 0ebh		;b4fe	eb		.
	defb 020h		;b4ff	20		 
	defb 020h		;b500	20		 
	defb 03ah		;b501	3a		:
	defb 020h		;b502	20		 
	defb 000h		;b503	00		.
	defb 087h		;b504	87		.
	defb 0a0h		;b505	a0		.
	defb 0afh		;b506	af		.
	defb 0e3h		;b507	e3		.
	defb 0e1h		;b508	e1		.
	defb 0aah		;b509	aa		.
	defb 020h		;b50a	20		 
	defb 0e1h		;b50b	e1		.
	defb 020h		;b50c	20		 
	defb 0a4h		;b50d	a4		.
	defb 0a8h		;b50e	a8		.
	defb 0e1h		;b50f	e1		.
	defb 0aah		;b510	aa		.
	defb 0a5h		;b511	a5		.
	defb 0e2h		;b512	e2		.
	defb 0ebh		;b513	eb		.
	defb 02eh		;b514	2e		.
	defb 02eh		;b515	2e		.
	defb 02eh		;b516	2e		.
	defb 000h		;b517	00		.
	defb 087h		;b518	87		.
	defb 0a0h		;b519	a0		.
	defb 0afh		;b51a	af		.
	defb 0e3h		;b51b	e3		.
	defb 0e1h		;b51c	e1		.
	defb 0aah		;b51d	aa		.
	defb 020h		;b51e	20		 
	defb 0e1h		;b51f	e1		.
	defb 020h		;b520	20		 
	defb 0a6h		;b521	a6		.
	defb 0a5h		;b522	a5		.
	defb 0e1h		;b523	e1		.
	defb 0e2h		;b524	e2		.
	defb 0aah		;b525	aa		.
	defb 0aeh		;b526	ae		.
	defb 0a3h		;b527	a3		.
	defb 0aeh		;b528	ae		.
	defb 020h		;b529	20		 
	defb 0a4h		;b52a	a4		.
	defb 0a8h		;b52b	a8		.
	defb 0e1h		;b52c	e1		.
	defb 0aah		;b52d	aa		.
	defb 0a0h		;b52e	a0		.
	defb 02eh		;b52f	2e		.
	defb 02eh		;b530	2e		.
	defb 02eh		;b531	2e		.
	defb 000h		;b532	00		.
	defb 087h		;b533	87		.
	defb 0a0h		;b534	a0		.
	defb 0afh		;b535	af		.
	defb 0e3h		;b536	e3		.
	defb 0e1h		;b537	e1		.
	defb 0aah		;b538	aa		.
	defb 020h		;b539	20		 
	defb 0e1h		;b53a	e1		.
	defb 020h		;b53b	20		 
	defb 043h		;b53c	43		C
	defb 044h		;b53d	44		D
	defb 02dh		;b53e	2d		-
	defb 052h		;b53f	52		R
	defb 04fh		;b540	4f		O
	defb 04dh		;b541	4d		M
	defb 02eh		;b542	2e		.
	defb 02eh		;b543	2e		.
	defb 02eh		;b544	2e		.
	defb 000h		;b545	00		.
	defb 087h		;b546	87		.
	defb 0a0h		;b547	a0		.
	defb 0afh		;b548	af		.
	defb 0e3h		;b549	e3		.
	defb 0e1h		;b54a	e1		.
	defb 0aah		;b54b	aa		.
	defb 020h		;b54c	20		 
	defb 0e1h		;b54d	e1		.
	defb 020h		;b54e	20		 
	defb 052h		;b54f	52		R
	defb 041h		;b550	41		A
	defb 04dh		;b551	4d		M
	defb 020h		;b552	20		 
	defb 0a4h		;b553	a4		.
	defb 0a8h		;b554	a8		.
	defb 0e1h		;b555	e1		.
	defb 0aah		;b556	aa		.
	defb 0a0h		;b557	a0		.
	defb 02eh		;b558	2e		.
	defb 02eh		;b559	2e		.
	defb 02eh		;b55a	2e		.
	defb 000h		;b55b	00		.
	defb 080h		;b55c	80		.
	defb 0abh		;b55d	ab		.
	defb 0ech		;b55e	ec		.
	defb 0e2h		;b55f	e2		.
	defb 0a5h		;b560	a5		.
	defb 0e0h		;b561	e0		.
	defb 0adh		;b562	ad		.
	defb 0a0h		;b563	a0		.
	defb 0e2h		;b564	e2		.
	defb 0a8h		;b565	a8		.
	defb 0a2h		;b566	a2		.
	defb 0adh		;b567	ad		.
	defb 0ebh		;b568	eb		.
	defb 0a9h		;b569	a9		.
	defb 020h		;b56a	20		 
	defb 000h		;b56b	00		.
	defb 0adh		;b56c	ad		.
	defb 0a5h		;b56d	a5		.
	defb 0a2h		;b56e	a2		.
	defb 0aeh		;b56f	ae		.
	defb 0a7h		;b570	a7		.
	defb 0ach		;b571	ac		.
	defb 0aeh		;b572	ae		.
	defb 0a6h		;b573	a6		.
	defb 0a5h		;b574	a5		.
	defb 0adh		;b575	ad		.
	defb 000h		;b576	00		.
	defb 04fh		;b577	4f		O
	defb 06bh		;b578	6b		k
	defb 000h		;b579	00		.
	defb 08dh		;b57a	8d		.
	defb 080h		;b57b	80		.
	defb 086h		;b57c	86		.
	defb 08ch		;b57d	8c		.
	defb 088h		;b57e	88		.
	defb 092h		;b57f	92		.
	defb 085h		;b580	85		.
	defb 020h		;b581	20		 
	defb 03ch		;b582	3c		<
	defb 045h		;b583	45		E
	defb 04eh		;b584	4e		N
	defb 054h		;b585	54		T
	defb 045h		;b586	45		E
	defb 052h		;b587	52		R
	defb 03eh		;b588	3e		>
	defb 020h		;b589	20		 
	defb 084h		;b58a	84		.
	defb 08bh		;b58b	8b		.
	defb 09fh		;b58c	9f		.
	defb 020h		;b58d	20		 
	defb 08fh		;b58e	8f		.
	defb 085h		;b58f	85		.
	defb 090h		;b590	90		.
	defb 085h		;b591	85		.
	defb 087h		;b592	87		.
	defb 080h		;b593	80		.
	defb 083h		;b594	83		.
	defb 090h		;b595	90		.
	defb 093h		;b596	93		.
	defb 087h		;b597	87		.
	defb 08ah		;b598	8a		.
	defb 088h		;b599	88		.
	defb 02ch		;b59a	2c		,
	defb 020h		;b59b	20		 
	defb 03ch		;b59c	3c		<
	defb 045h		;b59d	45		E
	defb 053h		;b59e	53		S
	defb 043h		;b59f	43		C
	defb 03eh		;b5a0	3e		>
	defb 020h		;b5a1	20		 
	defb 084h		;b5a2	84		.
	defb 08bh		;b5a3	8b		.
	defb 09fh		;b5a4	9f		.
	defb 020h		;b5a5	20		 
	defb 08eh		;b5a6	8e		.
	defb 092h		;b5a7	92		.
	defb 08ch		;b5a8	8c		.
	defb 085h		;b5a9	85		.
	defb 08dh		;b5aa	8d		.
	defb 09bh		;b5ab	9b		.
	defb 020h		;b5ac	20		 
	defb 02eh		;b5ad	2e		.
	defb 020h		;b5ae	20		 
	defb 02eh		;b5af	2e		.
	defb 020h		;b5b0	20		 
	defb 02eh		;b5b1	2e		.
	defb 000h		;b5b2	00		.
EMSGR:
				; = EMSGR (src: DSETUP.ASM:1722, BIOS-PP 1273243)
				; = KEY (src: DSETUP.ASM:1724, BIOS-PP 1273243)
	ei			;b5b3	fb		.
	jp WAITKEY		;b5b4	c3 48 9e	. H .
POSTLEN:
				; = POSTLEN (src: DSETUP.ASM:1727, BIOS-PP 1273243)
	inc a			;b5b7	3c		<
	ld hl,lb645h	;b5b8	21 45 b6	! E .
	ld bc,(SIZS)		;b5bb	ed 4b 43 b6	. K C .
LCPIR2:
				; = LCPIR2 (src: DSETUP.ASM:1730, BIOS-PP 1273243)
	ex af,af'		;b5bf	08		.
	xor a			;b5c0	af		.
	cpir			;b5c1	ed b1		. .
	ret nz			;b5c3	c0		.
	ex af,af'		;b5c4	08		.
	dec a			;b5c5	3d		=
	jr nz,LCPIR2		;b5c6	20 f7		  .
	ld bc,00100h		;b5c8	01 00 01	. . .
	xor a			;b5cb	af		.
	cpir			;b5cc	ed b1		. .
	ld a,0ffh		;b5ce	3e ff		> .
	sub c			;b5d0	91		.
	srl a			;b5d1	cb 3f		. ?
	ld c,a			;b5d3	4f		O
	ld a,028h		;b5d4	3e 28		> (
	sub c			;b5d6	91		.
	ld e,a			;b5d7	5f		_
	jp LOCAT		;b5d8	c3 ae 89	. . .
POSTMSG:
				; = POSTMSG (src: DSETUP.ASM:1749, BIOS-PP 1273243)
	inc a			;b5db	3c		<
	ld hl,lb645h	;b5dc	21 45 b6	! E .
	ld bc,(SIZS)		;b5df	ed 4b 43 b6	. K C .
LCPIR:
				; = LCPIR (src: DSETUP.ASM:1752, BIOS-PP 1273243)
	ex af,af'		;b5e3	08		.
	xor a			;b5e4	af		.
	cpir			;b5e5	ed b1		. .
	ret nz			;b5e7	c0		.
	ex af,af'		;b5e8	08		.
	dec a			;b5e9	3d		=
	jr nz,LCPIR		;b5ea	20 f7		  .
	jp sub_8a08h		;b5ec	c3 08 8a	. . .
POSTMSC:
				; = POSTMSC (src: DSETUP.ASM:1761, BIOS-PP 1273243)
	inc a			;b5ef	3c		<
	ld hl,lb645h	;b5f0	21 45 b6	! E .
	ld bc,(SIZS)		;b5f3	ed 4b 43 b6	. K C .
LCPIR3:
				; = LCPIR3 (src: DSETUP.ASM:1764, BIOS-PP 1273243)
	ex af,af'		;b5f7	08		.
	xor a			;b5f8	af		.
	cpir			;b5f9	ed b1		. .
	ret nz			;b5fb	c0		.
	ex af,af'		;b5fc	08		.
	dec a			;b5fd	3d		=
	jr nz,LCPIR3		;b5fe	20 f7		  .
	ld a,e			;b600	7b		{
	jp CPRINTZ		;b601	c3 18 8a	. . .
SETLAND:
				; = SETLAND (src: DSETUP.ASM:1774, BIOS-PP 1273243)
	ld a,00eh		;b604	3e 0e		> .
	call READCMS		;b606	cd 27 9b	. ' .
	and 004h		;b609	e6 04		. .
	jr nz,RUSXX		;b60b	20 1b		  .
SETENG:
				; = SETENG (src: DSETUP.ASM:1778, BIOS-PP 1273243)
	ld hl,MSGENG		;b60d	21 bb a8	! . .
	ld de,lb645h	;b610	11 45 b6	. E .
	ld bc,003a5h		;b613	01 a5 03	. . .
	ld (SIZS),bc		;b616	ed 43 43 b6	. C C .
	ldir			;b61a	ed b0		. .
	ld hl,DataA398	;b61c	21 98 a3	! . .
	ld de,0ba00h		;b61f	11 00 ba	. . .
	ld bc,00523h		;b622	01 23 05	. # .
	ldir			;b625	ed b0		. .
	ret			;b627	c9		.
RUSXX:
				; = RUSXX (src: DSETUP.ASM:1789, BIOS-PP 1273243)
	ld hl,EITMR		;b628	21 d2 b1	! . .
	ld de,lb645h	;b62b	11 45 b6	. E .
	ld bc,003e1h		;b62e	01 e1 03	. . .
	ld (SIZS),bc		;b631	ed 43 43 b6	. C C .
	ldir			;b635	ed b0		. .
	ld hl,lac60h		;b637	21 60 ac	! ` .
	ld de,0ba00h		;b63a	11 00 ba	. . .
	ld bc,00572h		;b63d	01 72 05	. r .
	ldir			;b640	ed b0		. .
	ret			;b642	c9		.
SIZS:

; BLOCK 'ZeroB643' (start 0xb643 end 0xb645)
				; = SIZS (src: DSETUP.ASM:1800, BIOS-PP 1273243)
	defb 000h		;b643	00		.
	defb 000h		;b644	00		.
lb645h:
