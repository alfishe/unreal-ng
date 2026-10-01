;===========================================================================
; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)
; ROM page 0: disk drivers ("EXTENDED"), the SETUP stub and packed SETUP
; Image: data/rom/sprinter/sp2k-3.04.rom (CRC32 1729cb5c), file offset #00000
;===========================================================================
;
; Layout of the page (details: ../README.md and rom/README.md):
;   #0000-#0EBB  disk drivers, entered from ROM page 8 through the page
;                stubs (BIOS functions #50-#5F, C = number - #40 here):
;                floppy (WD1793), IDE hard disk, ATAPI CD-ROM, RAM disk
;   #1000-#115E  SETUP stub: "SETUP (C) 2001 PETERS PLUS LTD", then code
;                that copies the Hrust depacker to #D000 and unpacks SETUP
;                to #8000. This block RUNS AT #8000: ROM page 8 copies
;                #1000-#3FFF of this page to RAM #8000 first, so its jump
;                and load addresses are #80xx/#81xx (listing addresses
;                here are the ROM offsets #10xx/#11xx)
;   #115F-#3209  SETUP packed with Hrust 1.x (13 893 bytes unpacked;
;                listing: rom/bios304-setup.asm)
;   #3FD0-#3FFF  page stubs matching those of ROM page 8
;   bytes 4-7    the ROM checksum (#3B #0D #05 #80; ZXMAK2 copy: #58 #7E
;                #83 #D4)
;
; Labels: hand names (dict_rom.py); "FnNN_NAME" handlers of the disk
; function dispatch; names carried by byte pattern from BIOS-PP 1273243
; (EXTENDED.ASM, FDRIVER2.ASM, HDRIVER6.ASM) and BIOS-TT 0271ac3, shown as
; "; = NAME (src: FILE:LINE)", "~" = short-window match (less certain).
;===========================================================================

	org 00000h

;---------------------------------------------------------------------------
; DI : HALT - this page is never entered at #0000 (the CPU starts in the
; PLD loader, ROM page #C, and then in ROM page 8). Bytes 4-7 hold the ROM
; checksum; they and page 8 byte 5 are the 5 bytes that differ from the
; ZXMAK2 copy of 3.04.
;---------------------------------------------------------------------------

RomStart:
	di			;0000	f3		.
	halt			;0001	76		v

; BLOCK 'Data0002' (start 0x0002 end 0x0010)
Data0002:
	defb 0ffh		;0002	ff		.
	defb 0ffh		;0003	ff		.
RomChecksum:
	defb 03bh		;0004	3b		;
	defb 00dh		;0005	0d		.
	defb 005h		;0006	05		.
	defb 080h		;0007	80		.
	defb 0ffh		;0008	ff		.
	defb 0ffh		;0009	ff		.
	defb 0ffh		;000a	ff		.
	defb 0ffh		;000b	ff		.
l000ch:
	defb 0ffh		;000c	ff		.
	defb 0ffh		;000d	ff		.
	defb 0ffh		;000e	ff		.
	defb 0ffh		;000f	ff		.
l0010h:
	ret			;0010	c9		.

; BLOCK 'Fill0011' (start 0x0011 end 0x0038)
Fill0011:
	defb 0ffh		;0011	ff		.
	defb 0ffh		;0012	ff		.
	defb 0ffh		;0013	ff		.
	defb 0ffh		;0014	ff		.
	defb 0ffh		;0015	ff		.
	defb 0ffh		;0016	ff		.
	defb 0ffh		;0017	ff		.
	defb 0ffh		;0018	ff		.
	defb 0ffh		;0019	ff		.
	defb 0ffh		;001a	ff		.
	defb 0ffh		;001b	ff		.
	defb 0ffh		;001c	ff		.
	defb 0ffh		;001d	ff		.
	defb 0ffh		;001e	ff		.
	defb 0ffh		;001f	ff		.
	defb 0ffh		;0020	ff		.
	defb 0ffh		;0021	ff		.
	defb 0ffh		;0022	ff		.
	defb 0ffh		;0023	ff		.
	defb 0ffh		;0024	ff		.
	defb 0ffh		;0025	ff		.
	defb 0ffh		;0026	ff		.
	defb 0ffh		;0027	ff		.
	defb 0ffh		;0028	ff		.
	defb 0ffh		;0029	ff		.
	defb 0ffh		;002a	ff		.
	defb 0ffh		;002b	ff		.
	defb 0ffh		;002c	ff		.
	defb 0ffh		;002d	ff		.
	defb 0ffh		;002e	ff		.
	defb 0ffh		;002f	ff		.
	defb 0ffh		;0030	ff		.
	defb 0ffh		;0031	ff		.
	defb 0ffh		;0032	ff		.
	defb 0ffh		;0033	ff		.
	defb 0ffh		;0034	ff		.
	defb 0ffh		;0035	ff		.
	defb 0ffh		;0036	ff		.
	defb 0ffh		;0037	ff		.
;---------------------------------------------------------------------------
; Interrupt handler while this page is in window 0: if the system page
; (#FE, mapped at #C000 here) holds #AA at #C127, call the user handler
; whose address is at #C124 and RAM page at #C126 (CallUserInt maps the
; page into the window the address points to).
;---------------------------------------------------------------------------
IntHandler:
				; = INT (src: EXTENDED.ASM:46, BIOS-PP 1273243)
	push hl			;0038	e5		.
	push bc			;0039	c5		.
	push af			;003a	f5		.
FN_SYNC_SetDefLines:
				; = ~FN_SYNC.SetDefLines (src: FLEX.asm:576, BIOS-TT 0271ac3)
	ld c,0e2h		;003b	0e e2		. .
	in b,(c)		;003d	ed 40		. @
	ld a,0feh		;003f	3e fe		> .
	out (c),a		;0041	ed 79		. y
	ld a,(0c127h)		;0043	3a 27 c1	: ' .
	cp 0aah			;0046	fe aa		. .
	jr z,YESINT		;0048	28 04		( .
	out (c),b		;004a	ed 41		. A
	jr NOINT		;004c	18 0d		. .
YESINT:
				; = YESINT (src: EXTENDED.ASM:59, BIOS-PP 1273243)
	ld hl,(0c124h)		;004e	2a 24 c1	* $ .
l0051h:
	ld a,h			;0051	7c		|
l0052h:
	or l			;0052	b5		.
	ld a,(0c126h)		;0053	3a 26 c1	: & .
	out (c),b		;0056	ed 41		. A
	call nz,CallUserInt	;0058	c4 68 00	. h .
NOINT:
				; = NOINT (src: EXTENDED.ASM:65, BIOS-PP 1273243)
	pop af			;005b	f1		.
	pop bc			;005c	c1		.
	pop hl			;005d	e1		.
	ei			;005e	fb		.
	reti			;005f	ed 4d		. M

; BLOCK 'Fill0061' (start 0x0061 end 0x0066)
Fill0061:
	defb 0ffh		;0061	ff		.
	defb 0ffh		;0062	ff		.
	defb 0ffh		;0063	ff		.
	defb 0ffh		;0064	ff		.
	defb 0ffh		;0065	ff		.
l0066h:
	retn			;0066	ed 45		. E
CallUserInt:
				; = EXTINT (src: EXTENDED.ASM:73, BIOS-PP 1273243)
	or a			;0068	b7		.
	ret z			;0069	c8		.
	ld c,0a2h		;006a	0e a2		. .
	bit 7,h			;006c	cb 7c		. |
	jr z,L1			;006e	28 08		( .
	ld c,0c2h		;0070	0e c2		. .
	bit 6,h			;0072	cb 74		. t
	jr z,L1			;0074	28 02		( .
	ld c,0e2h		;0076	0e e2		. .
L1:
				; = L1 (src: EXTENDED.ASM:82, BIOS-PP 1273243)
	in b,(c)		;0078	ed 40		. @
	push bc			;007a	c5		.
	out (c),a		;007b	ed 79		. y
	call JPHL		;007d	cd 84 00	. . .
	pop bc			;0080	c1		.
	out (c),b		;0081	ed 41		. A
	ret			;0083	c9		.
JPHL:
				; = JPHL (src: EXTENDED.ASM:89, BIOS-PP 1273243)
	jp (hl)			;0084	e9		.

; BLOCK 'Fill0085' (start 0x0085 end 0x0100)
Fill0085:
	defb 0ffh		;0085	ff		.
	defb 0ffh		;0086	ff		.
	defb 0ffh		;0087	ff		.
	defb 0ffh		;0088	ff		.
	defb 0ffh		;0089	ff		.
	defb 0ffh		;008a	ff		.
	defb 0ffh		;008b	ff		.
	defb 0ffh		;008c	ff		.
	defb 0ffh		;008d	ff		.
	defb 0ffh		;008e	ff		.
	defb 0ffh		;008f	ff		.
	defb 0ffh		;0090	ff		.
	defb 0ffh		;0091	ff		.
	defb 0ffh		;0092	ff		.
	defb 0ffh		;0093	ff		.
	defb 0ffh		;0094	ff		.
	defb 0ffh		;0095	ff		.
	defb 0ffh		;0096	ff		.
	defb 0ffh		;0097	ff		.
	defb 0ffh		;0098	ff		.
	defb 0ffh		;0099	ff		.
	defb 0ffh		;009a	ff		.
	defb 0ffh		;009b	ff		.
	defb 0ffh		;009c	ff		.
	defb 0ffh		;009d	ff		.
	defb 0ffh		;009e	ff		.
	defb 0ffh		;009f	ff		.
	defb 0ffh		;00a0	ff		.
	defb 0ffh		;00a1	ff		.
	defb 0ffh		;00a2	ff		.
	defb 0ffh		;00a3	ff		.
	defb 0ffh		;00a4	ff		.
	defb 0ffh		;00a5	ff		.
	defb 0ffh		;00a6	ff		.
	defb 0ffh		;00a7	ff		.
	defb 0ffh		;00a8	ff		.
	defb 0ffh		;00a9	ff		.
	defb 0ffh		;00aa	ff		.
	defb 0ffh		;00ab	ff		.
	defb 0ffh		;00ac	ff		.
	defb 0ffh		;00ad	ff		.
	defb 0ffh		;00ae	ff		.
	defb 0ffh		;00af	ff		.
	defb 0ffh		;00b0	ff		.
	defb 0ffh		;00b1	ff		.
	defb 0ffh		;00b2	ff		.
	defb 0ffh		;00b3	ff		.
	defb 0ffh		;00b4	ff		.
	defb 0ffh		;00b5	ff		.
	defb 0ffh		;00b6	ff		.
	defb 0ffh		;00b7	ff		.
	defb 0ffh		;00b8	ff		.
	defb 0ffh		;00b9	ff		.
	defb 0ffh		;00ba	ff		.
	defb 0ffh		;00bb	ff		.
	defb 0ffh		;00bc	ff		.
	defb 0ffh		;00bd	ff		.
	defb 0ffh		;00be	ff		.
	defb 0ffh		;00bf	ff		.
	defb 0ffh		;00c0	ff		.
	defb 0ffh		;00c1	ff		.
	defb 0ffh		;00c2	ff		.
	defb 0ffh		;00c3	ff		.
	defb 0ffh		;00c4	ff		.
	defb 0ffh		;00c5	ff		.
	defb 0ffh		;00c6	ff		.
	defb 0ffh		;00c7	ff		.
	defb 0ffh		;00c8	ff		.
	defb 0ffh		;00c9	ff		.
	defb 0ffh		;00ca	ff		.
	defb 0ffh		;00cb	ff		.
	defb 0ffh		;00cc	ff		.
	defb 0ffh		;00cd	ff		.
	defb 0ffh		;00ce	ff		.
	defb 0ffh		;00cf	ff		.
	defb 0ffh		;00d0	ff		.
	defb 0ffh		;00d1	ff		.
	defb 0ffh		;00d2	ff		.
	defb 0ffh		;00d3	ff		.
	defb 0ffh		;00d4	ff		.
	defb 0ffh		;00d5	ff		.
	defb 0ffh		;00d6	ff		.
	defb 0ffh		;00d7	ff		.
	defb 0ffh		;00d8	ff		.
	defb 0ffh		;00d9	ff		.
	defb 0ffh		;00da	ff		.
	defb 0ffh		;00db	ff		.
	defb 0ffh		;00dc	ff		.
	defb 0ffh		;00dd	ff		.
	defb 0ffh		;00de	ff		.
	defb 0ffh		;00df	ff		.
	defb 0ffh		;00e0	ff		.
	defb 0ffh		;00e1	ff		.
	defb 0ffh		;00e2	ff		.
	defb 0ffh		;00e3	ff		.
	defb 0ffh		;00e4	ff		.
	defb 0ffh		;00e5	ff		.
	defb 0ffh		;00e6	ff		.
	defb 0ffh		;00e7	ff		.
	defb 0ffh		;00e8	ff		.
	defb 0ffh		;00e9	ff		.
	defb 0ffh		;00ea	ff		.
	defb 0ffh		;00eb	ff		.
	defb 0ffh		;00ec	ff		.
	defb 0ffh		;00ed	ff		.
	defb 0ffh		;00ee	ff		.
	defb 0ffh		;00ef	ff		.
	defb 0ffh		;00f0	ff		.
	defb 0ffh		;00f1	ff		.
	defb 0ffh		;00f2	ff		.
	defb 0ffh		;00f3	ff		.
	defb 0ffh		;00f4	ff		.
	defb 0ffh		;00f5	ff		.
	defb 0ffh		;00f6	ff		.
	defb 0ffh		;00f7	ff		.
	defb 0ffh		;00f8	ff		.
	defb 0ffh		;00f9	ff		.
	defb 0ffh		;00fa	ff		.
	defb 0ffh		;00fb	ff		.
	defb 0ffh		;00fc	ff		.
	defb 0ffh		;00fd	ff		.
	defb 0ffh		;00fe	ff		.
l00ffh:
	defb 0ffh		;00ff	ff		.
;---------------------------------------------------------------------------
; Entry from ROM page 8 (its ToPage0 stub at #3FE8 lands at #3FED = JP #0100
; in this page). AF was pushed by the stub. DiskFnDispatch serves the
; call; BackToPage8 (#3FE8) switches back and page 8 returns to the caller.
;---------------------------------------------------------------------------
FromPage8Call:
				; = L0100 (src: EXTENDED.ASM:100, BIOS-PP 1273243)
	pop af			;0100	f1		.
	call DiskFnDispatch	;0101	cd 20 04	.   .
	jp BackToPage8		;0104	c3 e8 3f	. . ?
FromRamCall:
				; = L0107 (src: EXTENDED.ASM:103, BIOS-PP 1273243)
	pop af			;0107	f1		.
	call DiskFnDispatch	;0108	cd 20 04	.   .
	jp l3ff0h		;010b	c3 f0 3f	. . ?
OldHddDispatch:
				; = L010E (src: EXTENDED.ASM:106, BIOS-PP 1273243)
	push af			;010e	f5		.
	ld a,c			;010f	79		y
	and a			;0110	a7		.
	jp z,Fn40_HDD_INIT_old	;0111	ca 08 03	. . .
	dec a			;0114	3d		=
	jp z,Fn41_HDD_RECAL_old	;0115	ca ad 02	. . .
	dec a			;0118	3d		=
	jp z,Fn42_HDD_TEST_IDE_old	;0119	ca c8 02	. . .
	dec a			;011c	3d		=
	jp z,Fn43_HDD_PREPARE_old	;011d	ca 77 01	. w .
	dec a			;0120	3d		=
	jp z,Fn44_HDD_READ_BPB_old	;0121	ca 98 01	. . .
	dec a			;0124	3d		=
	jp z,Fn45_HDD_READ_old	;0125	ca a3 01	. . .
	dec a			;0128	3d		=
	jp z,Fn46_HDD_WRITE_old	;0129	ca 43 02	. C .
	pop af			;012c	f1		.
	scf			;012d	37		7
	ret			;012e	c9		.
L012F:
				; = L012F (src: EXTENDED.ASM:125, BIOS-PP 1273243)
				; = PORTS_INIT.loop (src: FUNC_4x.ASM:3, BIOS-TT 0271ac3)
				; = HD_BPB_PREP (src: FUNC_4x.ASM:11, BIOS-TT 0271ac3)
	ld d,a			;012f	57		W
	in a,(0e2h)		;0130	db e2		. .
	ex af,af'		;0132	08		.
	ld a,0feh		;0133	3e fe		> .
	out (0e2h),a		;0135	d3 e2		. .
	ld a,(0c60ch)		;0137	3a 0c c6	: . .
	ld e,a			;013a	5f		_
	ex af,af'		;013b	08		.
	out (0e2h),a		;013c	d3 e2		. .
	ld a,d			;013e	7a		z
	ld d,000h		;013f	16 00		. .
	ld ix,RomStart		;0141	dd 21 00 00	. ! . .
	ld b,001h		;0145	06 01		. .
L0147:
				; = L0147 (src: EXTENDED.ASM:138, BIOS-PP 1273243)
				; = HD_PREPARE (src: FUNC_4x.ASM:25, BIOS-TT 0271ac3)
	push af			;0147	f5		.
	push hl			;0148	e5		.
	call L03D0		;0149	cd d0 03	. . .
	jr nc,L0152		;014c	30 04		0 .
	pop hl			;014e	e1		.
	pop af			;014f	f1		.
l0150h:
	scf			;0150	37		7
l0151h:
	ret			;0151	c9		.
L0152:
				; = L0152 (src: EXTENDED.ASM:146, BIOS-PP 1273243)
				; = HD_PREPARE.L1 (src: FUNC_4x.ASM:35, BIOS-TT 0271ac3)
	ld a,b			;0152	78		x
l0153h:
	ld bc,L0152		;0153	01 52 01	. R .
	out (c),a		;0156	ed 79		. y
	ld bc,l0153h		;0158	01 53 01	. S .
	out (c),l		;015b	ed 69		. i
	ld bc,l0153h+1		;015d	01 54 01	. T .
	out (c),e		;0160	ed 59		. Y
	ld bc,l0153h+2		;0162	01 55 01	. U .
	out (c),d		;0165	ed 51		. Q
	ld bc,04152h		;0167	01 52 41	. R A
	dec b			;016a	05		.
	in a,(c)		;016b	ed 78		. x
	and 0f0h		;016d	e6 f0		. .
	or h			;016f	b4		.
	inc b			;0170	04		.
	out (c),a		;0171	ed 79		. y
	pop hl			;0173	e1		.
	pop af			;0174	f1		.
	and a			;0175	a7		.
	ret			;0176	c9		.
Fn43_HDD_PREPARE_old:
				; = L0177 (src: EXTENDED.ASM:167, BIOS-PP 1273243)
	pop af			;0177	f1		.
FN_HDD_PREPARE:
				; = FN_HDD_PREPARE (src: FUNC_4x.ASM:143, BIOS-TT 0271ac3)
	and a			;0178	a7		.
	inc b			;0179	04		.
	dec b			;017a	05		.
	ret z			;017b	c8		.
	call L03AA		;017c	cd aa 03	. . .
	ret c			;017f	d8		.
	call L0147		;0180	cd 47 01	. G .
	ret c			;0183	d8		.
	exx			;0184	d9		.
	ld c,0e2h		;0185	0e e2		. .
	in b,(c)		;0187	ed 40		. @
	exx			;0189	d9		.
	out (0e2h),a		;018a	d3 e2		. .
	ex af,af'		;018c	08		.
	ld a,0c0h		;018d	3e c0		> .
	out (089h),a		;018f	d3 89		. .
	ld bc,04153h		;0191	01 53 41	. S A
	ld a,020h		;0194	3e 20		>  
	and a			;0196	a7		.
	ret			;0197	c9		.
Fn44_HDD_READ_BPB_old:
				; = L0198 (src: EXTENDED.ASM:189, BIOS-PP 1273243)
	pop af			;0198	f1		.
FN_HDD_READ_BPB:
				; = FN_HDD_READ_BPB (src: FUNC_4x.ASM:177, BIOS-TT 0271ac3)
	call L03AA		;0199	cd aa 03	. . .
	ret c			;019c	d8		.
	call L012F		;019d	cd 2f 01	. / .
	ret c			;01a0	d8		.
	jr L01B0		;01a1	18 0d		. .
Fn45_HDD_READ_old:
				; = L01A3 (src: EXTENDED.ASM:195, BIOS-PP 1273243)
	pop af			;01a3	f1		.
FN_HDD_READ:
				; = FN_HDD_READ (src: FUNC_4x.ASM:202, BIOS-TT 0271ac3)
	and a			;01a4	a7		.
	inc b			;01a5	04		.
	dec b			;01a6	05		.
	ret z			;01a7	c8		.
	call L03AA		;01a8	cd aa 03	. . .
	ret c			;01ab	d8		.
	call L0147		;01ac	cd 47 01	. G .
	ret c			;01af	d8		.
L01B0:
				; = L01B0 (src: EXTENDED.ASM:204, BIOS-PP 1273243)
				; = HD_RD_L1 (src: FUNC_4x.ASM:212, BIOS-TT 0271ac3)
	exx			;01b0	d9		.
	ld c,0e2h		;01b1	0e e2		. .
	in b,(c)		;01b3	ed 40		. @
	exx			;01b5	d9		.
	out (0e2h),a		;01b6	d3 e2		. .
	ex af,af'		;01b8	08		.
	ld a,0c0h		;01b9	3e c0		> .
	out (089h),a		;01bb	d3 89		. .
	ld bc,04153h		;01bd	01 53 41	. S A
	ld a,020h		;01c0	3e 20		>  
	out (c),a		;01c2	ed 79		. y
L01C4:
				; = L01C4 (src: EXTENDED.ASM:215, BIOS-PP 1273243)
	ld bc,04053h		;01c4	01 53 40	. S @
	in a,(c)		;01c7	ed 78		. x
	bit 7,a			;01c9	cb 7f		. .
	jr nz,L01C4		;01cb	20 f7		  .
	bit 3,a			;01cd	cb 5f		. _
	jr nz,L01E4		;01cf	20 13		  .
	ld a,000h		;01d1	3e 00		> .
	out (089h),a		;01d3	d3 89		. .
L01D5:
				; = L01D5 (src: EXTENDED.ASM:223, BIOS-PP 1273243)
				; = HD_RET (src: FUNC_4x.ASM:233, BIOS-TT 0271ac3)
	exx			;01d5	d9		.
	out (c),b		;01d6	ed 41		. A
	exx			;01d8	d9		.
	ld bc,l0051h		;01d9	01 51 00	. Q .
	in a,(c)		;01dc	ed 78		. x
	and a			;01de	a7		.
	scf			;01df	37		7
	ret nz			;01e0	c0		.
	ex af,af'		;01e1	08		.
	and a			;01e2	a7		.
	ret			;01e3	c9		.
L01E4:
				; = L01E4 (src: EXTENDED.ASM:234, BIOS-PP 1273243)
				; = HD_READ_CONT (src: FUNC_4x.ASM:247, BIOS-TT 0271ac3)
	ld bc,YESINT+2		;01e4	01 50 00	. P .
L01E7:
				; = L01E7 (src: EXTENDED.ASM:235, BIOS-PP 1273243)
	ini			;01e7	ed a2		. .
	ini			;01e9	ed a2		. .
	ini			;01eb	ed a2		. .
	ini			;01ed	ed a2		. .
	ini			;01ef	ed a2		. .
	ini			;01f1	ed a2		. .
	ini			;01f3	ed a2		. .
	ini			;01f5	ed a2		. .
	ini			;01f7	ed a2		. .
	ini			;01f9	ed a2		. .
	ini			;01fb	ed a2		. .
	ini			;01fd	ed a2		. .
	ini			;01ff	ed a2		. .
	ini			;0201	ed a2		. .
	ini			;0203	ed a2		. .
	ini			;0205	ed a2		. .
	jp nz,L01E7		;0207	c2 e7 01	. . .
L020A:
				; = L020A (src: EXTENDED.ASM:252, BIOS-PP 1273243)
	ini			;020a	ed a2		. .
	ini			;020c	ed a2		. .
	ini			;020e	ed a2		. .
	ini			;0210	ed a2		. .
	ini			;0212	ed a2		. .
	ini			;0214	ed a2		. .
	ini			;0216	ed a2		. .
	ini			;0218	ed a2		. .
	ini			;021a	ed a2		. .
	ini			;021c	ed a2		. .
	ini			;021e	ed a2		. .
	ini			;0220	ed a2		. .
	ini			;0222	ed a2		. .
	ini			;0224	ed a2		. .
	ini			;0226	ed a2		. .
	ini			;0228	ed a2		. .
	jp nz,L020A		;022a	c2 0a 02	. . .
	ld a,h			;022d	7c		|
	or l			;022e	b5		.
	jr nz,L01C4		;022f	20 93		  .
	ld a,0feh		;0231	3e fe		> .
	out (0e2h),a		;0233	d3 e2		. .
l0235h:
	ex af,af'		;0235	08		.
	ld hl,0c200h		;0236	21 00 c2	! . .
	ld l,a			;0239	6f		o
	ld a,(hl)		;023a	7e		~
	out (0e2h),a		;023b	d3 e2		. .
	ex af,af'		;023d	08		.
	ld hl,0c000h		;023e	21 00 c0	! . .
	jr L01C4		;0241	18 81		. .
Fn46_HDD_WRITE_old:
				; = L0243 (src: EXTENDED.ASM:283, BIOS-PP 1273243)
	pop af			;0243	f1		.
FN_HDD_WRITE:
				; = FN_HDD_WRITE (src: FUNC_4x.ASM:277, BIOS-TT 0271ac3)
	and a			;0244	a7		.
	inc b			;0245	04		.
	dec b			;0246	05		.
	ret z			;0247	c8		.
	call L03AA		;0248	cd aa 03	. . .
	ret c			;024b	d8		.
	call L0147		;024c	cd 47 01	. G .
	ret c			;024f	d8		.
	exx			;0250	d9		.
	ld c,0e2h		;0251	0e e2		. .
	in b,(c)		;0253	ed 40		. @
l0255h:
	exx			;0255	d9		.
	out (0e2h),a		;0256	d3 e2		. .
	ex af,af'		;0258	08		.
	ld bc,04153h		;0259	01 53 41	. S A
	ld a,030h		;025c	3e 30		> 0
	out (c),a		;025e	ed 79		. y
L0260:
				; = L0260 (src: EXTENDED.ASM:301, BIOS-PP 1273243)
	ld bc,04053h		;0260	01 53 40	. S @
	in a,(c)		;0263	ed 78		. x
	bit 7,a			;0265	cb 7f		. .
	jr nz,L0260		;0267	20 f7		  .
	bit 3,a			;0269	cb 5f		. _
	jp z,L01D5		;026b	ca d5 01	. . .
	ld bc,l0150h		;026e	01 50 01	. P .
	ld d,020h		;0271	16 20		.  
L0273:
				; = L0273 (src: EXTENDED.ASM:309, BIOS-PP 1273243)
	outi			;0273	ed a3		. .
	outi			;0275	ed a3		. .
	outi			;0277	ed a3		. .
	outi			;0279	ed a3		. .
	outi			;027b	ed a3		. .
	outi			;027d	ed a3		. .
	outi			;027f	ed a3		. .
	outi			;0281	ed a3		. .
	outi			;0283	ed a3		. .
	outi			;0285	ed a3		. .
	outi			;0287	ed a3		. .
	outi			;0289	ed a3		. .
	outi			;028b	ed a3		. .
	outi			;028d	ed a3		. .
	outi			;028f	ed a3		. .
	outi			;0291	ed a3		. .
	dec d			;0293	15		.
	jp nz,L0273		;0294	c2 73 02	. s .
	ld a,h			;0297	7c		|
	or l			;0298	b5		.
	jr nz,L0260		;0299	20 c5		  .
	ld a,0feh		;029b	3e fe		> .
	out (0e2h),a		;029d	d3 e2		. .
	ex af,af'		;029f	08		.
	ld hl,0c200h		;02a0	21 00 c2	! . .
	ld l,a			;02a3	6f		o
	ld a,(hl)		;02a4	7e		~
	out (0e2h),a		;02a5	d3 e2		. .
	ex af,af'		;02a7	08		.
	ld hl,0c000h		;02a8	21 00 c0	! . .
	jr L0260		;02ab	18 b3		. .
Fn41_HDD_RECAL_old:
				; = L02AD (src: EXTENDED.ASM:341, BIOS-PP 1273243)
	pop af			;02ad	f1		.
FN_HDD_RECAL:
				; = FN_HDD_RECAL (src: FUNC_4x.ASM:330, BIOS-TT 0271ac3)
	ld a,0a0h		;02ae	3e a0		> .
	ld bc,04152h		;02b0	01 52 41	. R A
	out (c),a		;02b3	ed 79		. y
	ld a,090h		;02b5	3e 90		> .
	call L03A1		;02b7	cd a1 03	. . .
	and a			;02ba	a7		.
	bit 0,a			;02bb	cb 47		. G
	ret z			;02bd	c8		.
	ld bc,l0051h		;02be	01 51 00	. Q .
	in a,(c)		;02c1	ed 78		. x
	cp 001h			;02c3	fe 01		. .
	ret z			;02c5	c8		.
	scf			;02c6	37		7
	ret			;02c7	c9		.
Fn42_HDD_TEST_IDE_old:
				; = L02C8 (src: EXTENDED.ASM:357, BIOS-PP 1273243)
	pop af			;02c8	f1		.
FN_HDD_TEST_IDE:
				; = FN_HDD_TEST_IDE (src: FUNC_4x.ASM:354, BIOS-TT 0271ac3)
	ld e,000h		;02c9	1e 00		. .
	ld bc,04152h		;02cb	01 52 41	. R A
	ld a,0a0h		;02ce	3e a0		> .
	out (c),a		;02d0	ed 79		. y
	call L02ED		;02d2	cd ed 02	. . .
	jr nz,L02D9		;02d5	20 02		  .
	set 0,e			;02d7	cb c3		. .
L02D9:
				; = L02D9 (src: EXTENDED.ASM:365, BIOS-PP 1273243)
				; = NO_HDD1 (src: FUNC_4x.ASM:364, BIOS-TT 0271ac3)
	ld bc,04152h		;02d9	01 52 41	. R A
	ld a,0b0h		;02dc	3e b0		> .
	out (c),a		;02de	ed 79		. y
	call L02ED		;02e0	cd ed 02	. . .
	jr nz,L02E7		;02e3	20 02		  .
	set 1,e			;02e5	cb cb		. .
L02E7:
				; = L02E7 (src: EXTENDED.ASM:371, BIOS-PP 1273243)
				; = NO_HDD2 (src: FUNC_4x.ASM:373, BIOS-TT 0271ac3)
	ld a,e			;02e7	7b		{
	and a			;02e8	a7		.
	scf			;02e9	37		7
	ret z			;02ea	c8		.
	and a			;02eb	a7		.
	ret			;02ec	c9		.
L02ED:
				; = L02ED (src: EXTENDED.ASM:378, BIOS-PP 1273243)
				; = TEST_HDD_DRV (src: FUNC_4x.ASM:384, BIOS-TT 0271ac3)
	ld hl,l00ffh		;02ed	21 ff 00	! . .
	ld bc,l0153h+1		;02f0	01 54 01	. T .
	out (c),l		;02f3	ed 69		. i
	ld bc,l0153h+2		;02f5	01 55 01	. U .
	out (c),h		;02f8	ed 61		. a
	ld bc,00254h		;02fa	01 54 02	. T .
	in a,(c)		;02fd	ed 78		. x
	cp l			;02ff	bd		.
	ret nz			;0300	c0		.
	ld bc,l0255h		;0301	01 55 02	. U .
	in a,(c)		;0304	ed 78		. x
	cp h			;0306	bc		.
	ret			;0307	c9		.
Fn40_HDD_INIT_old:
				; = L0308 (src: EXTENDED.ASM:391, BIOS-PP 1273243)
	pop af			;0308	f1		.
TEST_HDD_DRV:
				; = TEST_HDD_DRV (src: FUNC_4x.ASM:412, BIOS-TT 0271ac3)
				; = FN_HDD_INIT (src: FUNC_4x.ASM:444, BIOS-TT 0271ac3)
	ld bc,04152h		;0309	01 52 41	. R A
	ld a,0a0h		;030c	3e a0		> .
	out (c),a		;030e	ed 79		. y
	call L02ED		;0310	cd ed 02	. . .
	jr nz,L0334		;0313	20 1f		  .
L0315:
				; = L0315 (src: EXTENDED.ASM:397, BIOS-PP 1273243)
	ld bc,04053h		;0315	01 53 40	. S @
	in a,(c)		;0318	ed 78		. x
	bit 7,a			;031a	cb 7f		. .
	jr nz,L0315		;031c	20 f7		  .
	ld bc,04153h		;031e	01 53 41	. S A
	ld a,0ech		;0321	3e ec		> .
	out (c),a		;0323	ed 79		. y
L0325:
				; = L0325 (src: EXTENDED.ASM:404, BIOS-PP 1273243)
	ld bc,04053h		;0325	01 53 40	. S @
	in a,(c)		;0328	ed 78		. x
	bit 7,a			;032a	cb 7f		. .
	jr nz,L0325		;032c	20 f7		  .
	bit 3,a			;032e	cb 5f		. _
	jr nz,L0342		;0330	20 10		  .
	scf			;0332	37		7
	ret			;0333	c9		.
L0334:
				; = L0334 (src: EXTENDED.ASM:413, BIOS-PP 1273243)
				; = HD_ABSENT (src: FUNC_4x.ASM:463, BIOS-TT 0271ac3)
	ld bc,04152h		;0334	01 52 41	. R A
	ld a,0b0h		;0337	3e b0		> .
	out (c),a		;0339	ed 79		. y
	call L02ED		;033b	cd ed 02	. . .
	jr z,L0315		;033e	28 d5		( .
	scf			;0340	37		7
	ret			;0341	c9		.
L0342:
				; = L0342 (src: EXTENDED.ASM:421, BIOS-PP 1273243)
				; = HD_C0_L2 (src: FUNC_4x.ASM:472, BIOS-TT 0271ac3)
	ld bc,YESINT+2		;0342	01 50 00	. P .
	ld hl,0c600h		;0345	21 00 c6	! . .
	in a,(0e2h)		;0348	db e2		. .
	ld d,a			;034a	57		W
	ld a,0feh		;034b	3e fe		> .
	out (0e2h),a		;034d	d3 e2		. .
	inir			;034f	ed b2		. .
	inir			;0351	ed b2		. .
	ld a,(0c60ch)		;0353	3a 0c c6	: . .
	ld c,a			;0356	4f		O
	ld b,000h		;0357	06 00		. .
	ld hl,RomStart		;0359	21 00 00	! . .
	ld a,(0c606h)		;035c	3a 06 c6	: . .
L035F:
				; = L035F (src: EXTENDED.ASM:434, BIOS-PP 1273243)
				; = HD_C0_L2.loop (src: FUNC_4x.ASM:486, BIOS-TT 0271ac3)
	add hl,bc		;035f	09		.
	dec a			;0360	3d		=
	jr nz,L035F		;0361	20 fc		  .
	ld (0c604h),hl		;0363	22 04 c6	" . .
L0366:
				; = L0366 (src: EXTENDED.ASM:438, BIOS-PP 1273243)
	ld bc,04053h		;0366	01 53 40	. S @
	in a,(c)		;0369	ed 78		. x
	bit 7,a			;036b	cb 7f		. .
	jr nz,L0366		;036d	20 f7		  .
	ld bc,04152h		;036f	01 52 41	. R A
	dec b			;0372	05		.
	in a,(c)		;0373	ed 78		. x
	and 010h		;0375	e6 10		. .
	ld b,a			;0377	47		G
	ld a,(0c606h)		;0378	3a 06 c6	: . .
	dec a			;037b	3d		=
	and 00fh		;037c	e6 0f		. .
	or 0a0h			;037e	f6 a0		. .
	or b			;0380	b0		.
	ld h,a			;0381	67		g
	ld a,(0c663h)		;0382	3a 63 c6	: c .
	bit 1,a			;0385	cb 4f		. O
	jr z,L038B		;0387	28 02		( .
	set 6,h			;0389	cb f4		. .
L038B:
				; = L038B (src: EXTENDED.ASM:457, BIOS-PP 1273243)
				; = HD_C0_NO_LBA (src: FUNC_4x.ASM:511, BIOS-TT 0271ac3)
	ld bc,04152h		;038b	01 52 41	. R A
	out (c),h		;038e	ed 61		. a
	ld a,(0c60ch)		;0390	3a 0c c6	: . .
	ld bc,L0152		;0393	01 52 01	. R .
	out (c),a		;0396	ed 79		. y
	ld a,d			;0398	7a		z
	out (0e2h),a		;0399	d3 e2		. .
	ld a,091h		;039b	3e 91		> .
	call L03A1		;039d	cd a1 03	. . .
	ret			;03a0	c9		.
L03A1:
				; = L03A1 (src: EXTENDED.ASM:468, BIOS-PP 1273243)
				; = HD_CMD_EXE (src: FUNC_4x.ASM:526, BIOS-TT 0271ac3)
	call L03AA		;03a1	cd aa 03	. . .
	ret c			;03a4	d8		.
	ld bc,04153h		;03a5	01 53 41	. S A
	out (c),a		;03a8	ed 79		. y
L03AA:
				; = L03AA (src: EXTENDED.ASM:472, BIOS-PP 1273243)
				; = HD_WAIT (src: FUNC_4x.ASM:531, BIOS-TT 0271ac3)
	push de			;03aa	d5		.
	push bc			;03ab	c5		.
	push af			;03ac	f5		.
	ld de,RomStart		;03ad	11 00 00	. . .
L03B0:
				; = L03B0 (src: EXTENDED.ASM:476, BIOS-PP 1273243)
				; = HD_WAIT1 (src: FUNC_4x.ASM:536, BIOS-TT 0271ac3)
	ld bc,04053h		;03b0	01 53 40	. S @
	in a,(c)		;03b3	ed 78		. x
	bit 7,a			;03b5	cb 7f		. .
	jr z,L03C3		;03b7	28 0a		( .
	dec de			;03b9	1b		.
	ld a,d			;03ba	7a		z
	or e			;03bb	b3		.
	jr nz,L03B0		;03bc	20 f2		  .
	pop af			;03be	f1		.
	pop bc			;03bf	c1		.
	pop de			;03c0	d1		.
	scf			;03c1	37		7
	ret			;03c2	c9		.
L03C3:
				; = L03C3 (src: EXTENDED.ASM:490, BIOS-PP 1273243)
				; = HD_W_EXIT (src: FUNC_4x.ASM:550, BIOS-TT 0271ac3)
	pop af			;03c3	f1		.
	pop bc			;03c4	c1		.
	pop de			;03c5	d1		.
	and a			;03c6	a7		.
	ret			;03c7	c9		.
L03C8:
				; = L03C8 (src: EXTENDED.ASM:496, BIOS-PP 1273243)
				; = HDD_LBA (src: FUNC_4x.ASM:560, BIOS-TT 0271ac3)
				; = HD_CALC_SECS (src: FUNC_4x.ASM:569, BIOS-TT 0271ac3)
	pop bc			;03c8	c1		.
	ld l,e			;03c9	6b		k
	ld e,d			;03ca	5a		Z
	defb 0ddh,055h ;ld d,ixl	;03cb	dd 55		. U
	xor a			;03cd	af		.
	ld h,a			;03ce	67		g
	ret			;03cf	c9		.
L03D0:
	defb 0ddh,07ch ;ld a,ixh	;03d0	dd 7c		. |
	and a			;03d2	a7		.
	scf			;03d3	37		7
	ret nz			;03d4	c0		.
	push bc			;03d5	c5		.
	ld bc,04152h		;03d6	01 52 41	. R A
	dec b			;03d9	05		.
	in a,(c)		;03da	ed 78		. x
	bit 6,a			;03dc	cb 77		. w
	jr nz,L03C8		;03de	20 e8		  .
	push ix			;03e0	dd e5		. .
	pop hl			;03e2	e1		.
PIC_FN3:
				; = ~PIC_FN3 (src: FUNC_PIC.ASM:119, BIOS-TT 0271ac3)
	in a,(0e2h)		;03e3	db e2		. .
	ld c,a			;03e5	4f		O
	ld a,0feh		;03e6	3e fe		> .
	out (0e2h),a		;03e8	d3 e2		. .
	ld a,c			;03ea	79		y
	ld (0c107h),a		;03eb	32 07 c1	2 . .
	ld bc,(0c604h)		;03ee	ed 4b 04 c6	. K . .
	ld a,010h		;03f2	3e 10		> .
	scf			;03f4	37		7
L03F5:
				; = L03F5 (src: EXTENDED.ASM:525, BIOS-PP 1273243)
				; = DIV_LOOP (src: FUNC_4x.ASM:624, BIOS-TT 0271ac3)
	ex de,hl		;03f5	eb		.
	add hl,hl		;03f6	29		)
	ex de,hl		;03f7	eb		.
	adc hl,hl		;03f8	ed 6a		. j
	sbc hl,bc		;03fa	ed 42		. B
	jr nc,L0404		;03fc	30 06		0 .
	add hl,bc		;03fe	09		.
	dec a			;03ff	3d		=
	jr nz,L03F5		;0400	20 f3		  .
	jr L0408		;0402	18 04		. .
L0404:
				; = L0404 (src: EXTENDED.ASM:535, BIOS-PP 1273243)
				; = NO_ADD (src: FUNC_4x.ASM:636, BIOS-TT 0271ac3)
	inc de			;0404	13		.
	dec a			;0405	3d		=
	jr nz,L03F5		;0406	20 ed		  .
L0408:
				; = L0408 (src: EXTENDED.ASM:538, BIOS-PP 1273243)
				; = DIV_END (src: FUNC_4x.ASM:640, BIOS-TT 0271ac3)
	ld a,(0c60ch)		;0408	3a 0c c6	: . .
	ld b,000h		;040b	06 00		. .
	ld c,a			;040d	4f		O
	xor a			;040e	af		.
L040F:
				; = L040F (src: EXTENDED.ASM:542, BIOS-PP 1273243)
				; = HD_CALC_LOOP2 (src: FUNC_4x.ASM:650, BIOS-TT 0271ac3)
	sbc hl,bc		;040f	ed 42		. B
	inc a			;0411	3c		<
	jr nc,L040F		;0412	30 fb		0 .
	dec a			;0414	3d		=
	add hl,bc		;0415	09		.
	inc l			;0416	2c		,
	ld h,a			;0417	67		g
	ld a,(0c107h)		;0418	3a 07 c1	: . .
	out (0e2h),a		;041b	d3 e2		. .
	pop bc			;041d	c1		.
	and a			;041e	a7		.
	ret			;041f	c9		.
;---------------------------------------------------------------------------
; DiskFnDispatch. C = function number - #40 (page 8 cleared bit 6):
;   bit 7, 6 or 5 set -> Carry (not a disk function)
;   bit 4 = 0 -> OldHddDispatch (#40-#47, the 2.x HDD API kept for old
;                programs; page 8 serves #40-#48 itself in 3.04)
;   bit 4 = 1 -> #50-#5F, the disk API (A = device: #00-#0F floppy,
;                #80-#8F IDE, #C0-#CF CD-ROM; HL:IX = sector number):
;      #50 -, #51 reset, #52 long read, #53 long write, #54 verify,
;      #55 read, #56 write, #57 detect, #58 / #59 get / set media
;      parameters, #5A version (DE = #0235: disk subsystem version 2,
;      modification #35 = 53, i.e. 2.53 - the "253" of the build name;
;      the 2.17 sources return 2.41), #5F device list (DRV_LIST)
; 3.04 adds a CD-ROM (ATAPI) branch to each device switch
; (cp #C0 / cp #D0); the 2.17 sources (BIOS-PP) have it commented out.
;---------------------------------------------------------------------------
DiskFnDispatch:
				; = L0420 (src: EXTENDED.ASM:554, BIOS-PP 1273243)
	scf			;0420	37		7
	bit 7,c			;0421	cb 79		. y
	ret nz			;0423	c0		.
	bit 6,c			;0424	cb 71		. q
	ret nz			;0426	c0		.
	bit 5,c			;0427	cb 69		. i
	ret nz			;0429	c0		.
	bit 4,c			;042a	cb 61		. a
	jp z,OldHddDispatch	;042c	ca 0e 01	. . .
	res 4,c			;042f	cb a1		. .
	inc c			;0431	0c		.
	dec c			;0432	0d		.
	jp z,Fn50_DRV_GET_NAME	;0433	ca 72 04	. r .
	dec c			;0436	0d		.
	jp z,Fn51_DRV_RESET	;0437	ca fd 04	. . .
	dec c			;043a	0d		.
	jp z,Fn52_DRV_READ_LONG	;043b	ca 1a 05	. . .
	dec c			;043e	0d		.
	jp z,Fn53_DRV_WRITE_LONG	;043f	ca 37 05	. 7 .
	dec c			;0442	0d		.
	jp z,Fn54_DRV_VERIFY	;0443	ca 7a 05	. z .
	dec c			;0446	0d		.
	jp z,Fn55_DRV_READ	;0447	ca 4a 05	. J .
	dec c			;044a	0d		.
	jp z,Fn56_DRV_WRITE	;044b	ca 67 05	. g .
	dec c			;044e	0d		.
	jp z,Fn57_DRV_DETECT	;044f	ca 88 05	. . .
	dec c			;0452	0d		.
	jp z,Fn58_DRV_GET_PAR	;0453	ca 9b 05	. . .
	dec c			;0456	0d		.
	jp z,Fn59_DRV_SET_PAR	;0457	ca ae 05	. . .
	dec c			;045a	0d		.
	jp z,Fn5A_DRV_VERSION	;045b	ca 76 04	. v .
	dec c			;045e	0d		.
	jp z,Fn50_DRV_GET_NAME	;045f	ca 72 04	. r .
	dec c			;0462	0d		.
	jp z,Fn50_DRV_GET_NAME	;0463	ca 72 04	. r .
	dec c			;0466	0d		.
	jp z,Fn50_DRV_GET_NAME	;0467	ca 72 04	. r .
	dec c			;046a	0d		.
	jp z,Fn50_DRV_GET_NAME	;046b	ca 72 04	. r .
	dec c			;046e	0d		.
	jp z,Fn5F_DRV_LIST	;046f	ca 81 04	. . .
Fn50_DRV_GET_NAME:
				; = NOPCOMM (src: EXTENDED.ASM:611, BIOS-PP 1273243)
	ld a,001h		;0472	3e 01		> .
	scf			;0474	37		7
	ret			;0475	c9		.
Fn5A_DRV_VERSION:
				; = CVERSI (src: EXTENDED.ASM:615, BIOS-PP 1273243)
				; = DRV_VERSION (src: FUNC_5x.asm:19, BIOS-TT 0271ac3)
	ld hl,RomStart		;0476	21 00 00	! . .
	ld bc,RomStart		;0479	01 00 00	. . .
	ld de,l0235h		;047c	11 35 02	. 5 .
	and a			;047f	a7		.
	ret			;0480	c9		.
;---------------------------------------------------------------------------
; Function #5F (device list): counts the floppy drives (descriptors at
; #C1E0 and #C1E8 of the system page) and the IDE units of ONE channel
; (descriptors #C1C0 = master, #C1C8 = slave; type byte +7: #01 hard disk,
; #02 CD-ROM, #FF none). Result: 4 bytes at (IX): #04, floppies, hard
; disks, CD-ROMs.
;---------------------------------------------------------------------------
Fn5F_DRV_LIST:
				; = CCONFIG (src: EXTENDED.ASM:621, BIOS-PP 1273243)
				; = DRV_CONFIG (src: FUNC_5x.asm:26, BIOS-TT 0271ac3)
	in a,(0e2h)		;0481	db e2		. .
	push af			;0483	f5		.
	push iy			;0484	fd e5		. .
	ld a,0feh		;0486	3e fe		> .
	out (0e2h),a		;0488	d3 e2		. .
	ld (ix+000h),004h	;048a	dd 36 00 04	. 6 . .
	ld (ix+001h),000h	;048e	dd 36 01 00	. 6 . .
	ld (ix+002h),000h	;0492	dd 36 02 00	. 6 . .
	ld (ix+003h),000h	;0496	dd 36 03 00	. 6 . .
	ld (ix+004h),000h	;049a	dd 36 04 00	. 6 . .
	ld hl,0c1e0h		;049e	21 e0 c1	! . .
	inc (ix+001h)		;04a1	dd 34 01	. 4 .
	ld b,008h		;04a4	06 08		. .
	ld a,0ffh		;04a6	3e ff		> .
TFD0:
				; = TFD0 (src: EXTENDED.ASM:636, BIOS-PP 1273243)
				; = DRV_CONFIG.TFD0 (src: FUNC_5x.asm:42, BIOS-TT 0271ac3)
	cp (hl)			;04a8	be		.
	inc hl			;04a9	23		#
	jr nz,YYYFD0		;04aa	20 05		  .
	djnz TFD0		;04ac	10 fa		. .
	dec (ix+001h)		;04ae	dd 35 01	. 5 .
YYYFD0:
				; = YYYFD0 (src: EXTENDED.ASM:641, BIOS-PP 1273243)
				; = DRV_CONFIG.YYYFD0 (src: FUNC_5x.asm:48, BIOS-TT 0271ac3)
	ld hl,0c1e8h		;04b1	21 e8 c1	! . .
	inc (ix+001h)		;04b4	dd 34 01	. 4 .
	ld b,008h		;04b7	06 08		. .
	ld a,0ffh		;04b9	3e ff		> .
TFD1:
				; = TFD1 (src: EXTENDED.ASM:645, BIOS-PP 1273243)
				; = DRV_CONFIG.TFD1 (src: FUNC_5x.asm:53, BIOS-TT 0271ac3)
	cp (hl)			;04bb	be		.
	inc hl			;04bc	23		#
	jr nz,YYYFD1		;04bd	20 05		  .
	djnz TFD1		;04bf	10 fa		. .
	dec (ix+001h)		;04c1	dd 35 01	. 5 .
YYYFD1:
				; = YYYFD1 (src: EXTENDED.ASM:650, BIOS-PP 1273243)
				; = DRV_CONFIG.YYYFD1 (src: FUNC_5x.asm:59, BIOS-TT 0271ac3)
	ld iy,0c1c0h		;04c4	fd 21 c0 c1	. ! . .
	ld a,(iy+007h)		;04c8	fd 7e 07	. ~ .
	cp 0ffh			;04cb	fe ff		. .
	jr z,NOT_CD0		;04cd	28 0e		( .
	cp 001h			;04cf	fe 01		. .
	jr nz,NOT_HD0		;04d1	20 03		  .
	inc (ix+002h)		;04d3	dd 34 02	. 4 .
NOT_HD0:
				; = NOT_HD0 (src: EXTENDED.ASM:659, BIOS-PP 1273243)
				; = DRV_CONFIG.NOT_HD0 (src: FUNC_5x.asm:68, BIOS-TT 0271ac3)
				; = ~DRV_CONFIG.NOT_HD2 (src: FUNC_5x.asm:94, BIOS-TT 0271ac3)
	cp 002h			;04d6	fe 02		. .
	jr nz,NOT_CD0		;04d8	20 03		  .
	inc (ix+003h)		;04da	dd 34 03	. 4 .
NOT_CD0:
				; = NOT_CD0 (src: EXTENDED.ASM:662, BIOS-PP 1273243)
				; = ABSIDE0 (src: EXTENDED.ASM:663, BIOS-PP 1273243)
				; = DRV_CONFIG.NOT_CD0 (src: FUNC_5x.asm:72, BIOS-TT 0271ac3)
				; = DRV_CONFIG.ABSIDE0 (src: FUNC_5x.asm:73, BIOS-TT 0271ac3)
	ld iy,0c1c8h		;04dd	fd 21 c8 c1	. ! . .
	ld a,(iy+007h)		;04e1	fd 7e 07	. ~ .
	cp 0ffh			;04e4	fe ff		. .
	jr z,NOT_CD1		;04e6	28 0e		( .
	cp 001h			;04e8	fe 01		. .
	jr nz,NOT_HD1		;04ea	20 03		  .
	inc (ix+002h)		;04ec	dd 34 02	. 4 .
NOT_HD1:
				; = NOT_HD1 (src: EXTENDED.ASM:670, BIOS-PP 1273243)
				; = DRV_CONFIG.NOT_HD1 (src: FUNC_5x.asm:81, BIOS-TT 0271ac3)
				; = ~DRV_CONFIG.NOT_HD3 (src: FUNC_5x.asm:107, BIOS-TT 0271ac3)
	cp 002h			;04ef	fe 02		. .
	jr nz,NOT_CD1		;04f1	20 03		  .
	inc (ix+003h)		;04f3	dd 34 03	. 4 .
NOT_CD1:
				; = NOT_CD1 (src: EXTENDED.ASM:673, BIOS-PP 1273243)
				; = ABSIDE1 (src: EXTENDED.ASM:674, BIOS-PP 1273243)
				; = DRV_CONFIG.NOT_CD1 (src: FUNC_5x.asm:85, BIOS-TT 0271ac3)
				; = DRV_CONFIG.ABSIDE1 (src: FUNC_5x.asm:86, BIOS-TT 0271ac3)
	pop iy			;04f6	fd e1		. .
	pop af			;04f8	f1		.
	out (0e2h),a		;04f9	d3 e2		. .
	xor a			;04fb	af		.
	ret			;04fc	c9		.
Fn51_DRV_RESET:
				; = CRESET (src: EXTENDED.ASM:681, BIOS-PP 1273243)
	cp 010h			;04fd	fe 10		. .
	jp c,RESETD		;04ff	da 09 06	. . .
	cp 080h			;0502	fe 80		. .
	jp c,l0516h		;0504	da 16 05	. . .
	cp 090h			;0507	fe 90		. .
	jp c,RESETH		;0509	da 81 09	. . .
	cp 0c0h			;050c	fe c0		. .
	jp c,l0516h		;050e	da 16 05	. . .
	cp 0d0h			;0511	fe d0		. .
	jp c,CD_5x_RESET	;0513	da d0 0c	. . .
l0516h:
	ld a,0aah		;0516	3e aa		> .
	scf			;0518	37		7
	ret			;0519	c9		.
Fn52_DRV_READ_LONG:
	cp 010h			;051a	fe 10		. .
	jp c,LREADD		;051c	da b9 06	. . .
	cp 080h			;051f	fe 80		. .
	jp c,RESETU		;0521	da 33 05	. 3 .
	cp 090h			;0524	fe 90		. .
	jp c,LREADH		;0526	da fa 09	. . .
	cp 0c0h			;0529	fe c0		. .
	jp c,RESETU		;052b	da 33 05	. 3 .
	cp 0d0h			;052e	fe d0		. .
	jp c,CD_5x_LONG_READ	;0530	da df 0c	. . .
RESETU:
				; = RESETU (src: EXTENDED.ASM:696, BIOS-PP 1273243)
	ld a,0aah		;0533	3e aa		> .
	scf			;0535	37		7
	ret			;0536	c9		.
Fn53_DRV_WRITE_LONG:
	cp 010h			;0537	fe 10		. .
	jp c,LWRITED		;0539	da 68 07	. h .
	cp 080h			;053c	fe 80		. .
	jp c,LREADU		;053e	da 46 05	. F .
	cp 090h			;0541	fe 90		. .
	jp c,LWRITEH		;0543	da eb 0a	. . .
LREADU:
				; = LREADU (src: EXTENDED.ASM:715, BIOS-PP 1273243)
	ld a,0aah		;0546	3e aa		> .
	scf			;0548	37		7
	ret			;0549	c9		.
Fn55_DRV_READ:
	cp 010h			;054a	fe 10		. .
	jp c,READD		;054c	da b5 06	. . .
	cp 080h			;054f	fe 80		. .
	jp c,READU		;0551	da 63 05	. c .
	cp 090h			;0554	fe 90		. .
	jp c,READH		;0556	da f6 09	. . .
	cp 0c0h			;0559	fe c0		. .
	jp c,READU		;055b	da 63 05	. c .
	cp 0d0h			;055e	fe d0		. .
	jp c,CD_5x_LONG_READ	;0560	da df 0c	. . .
READU:
				; = READU (src: EXTENDED.ASM:754, BIOS-PP 1273243)
	ld a,0aah		;0563	3e aa		> .
	scf			;0565	37		7
	ret			;0566	c9		.
Fn56_DRV_WRITE:
	cp 010h			;0567	fe 10		. .
	jp c,WRITED		;0569	da 64 07	. d .
	cp 080h			;056c	fe 80		. .
	jp c,WRITEU		;056e	da 76 05	. v .
	cp 090h			;0571	fe 90		. .
	jp c,WRITEH		;0573	da e7 0a	. . .
WRITEU:
				; = WRITEU (src: EXTENDED.ASM:773, BIOS-PP 1273243)
	ld a,0aah		;0576	3e aa		> .
	scf			;0578	37		7
	ret			;0579	c9		.
Fn54_DRV_VERIFY:
				; = CVERIFY (src: EXTENDED.ASM:778, BIOS-PP 1273243)
	cp 080h			;057a	fe 80		. .
	jp c,VERIFYU		;057c	da 84 05	. . .
	cp 090h			;057f	fe 90		. .
	jp c,VERIFYH		;0581	da ef 0b	. . .
VERIFYU:
				; = VERIFYU (src: EXTENDED.ASM:793, BIOS-PP 1273243)
	ld a,0aah		;0584	3e aa		> .
	scf			;0586	37		7
	ret			;0587	c9		.
Fn57_DRV_DETECT:
				; = CDETECT (src: EXTENDED.ASM:798, BIOS-PP 1273243)
	cp 010h			;0588	fe 10		. .
	jp c,DETECTD		;058a	da fd 05	. . .
	cp 0c0h			;058d	fe c0		. .
	jp c,DETECTU		;058f	da 97 05	. . .
	cp 0d0h			;0592	fe d0		. .
	jp c,CD_CLOSE		;0594	da f8 0c	. . .
DETECTU:
				; = DETECTU (src: EXTENDED.ASM:813, BIOS-PP 1273243)
	ld a,0aah		;0597	3e aa		> .
	scf			;0599	37		7
	ret			;059a	c9		.
Fn58_DRV_GET_PAR:
				; = CGETMED (src: EXTENDED.ASM:818, BIOS-PP 1273243)
	cp 010h			;059b	fe 10		. .
	jp c,GETMEDD		;059d	da c5 05	. . .
	cp 080h			;05a0	fe 80		. .
	jp c,GETMEDU		;05a2	da aa 05	. . .
	cp 090h			;05a5	fe 90		. .
	jp c,GETMEDH		;05a7	da b2 09	. . .
GETMEDU:
				; = GETMEDU (src: EXTENDED.ASM:825, BIOS-PP 1273243)
	ld a,0aah		;05aa	3e aa		> .
	scf			;05ac	37		7
	ret			;05ad	c9		.
Fn59_DRV_SET_PAR:
				; = CSETMED (src: EXTENDED.ASM:830, BIOS-PP 1273243)
	cp 010h			;05ae	fe 10		. .
	jp c,SETMEDD		;05b0	da e1 05	. . .
	cp 080h			;05b3	fe 80		. .
	jp c,SETMEDU		;05b5	da bd 05	. . .
	cp 090h			;05b8	fe 90		. .
	jp c,SETMEDH		;05ba	da d6 09	. . .
SETMEDU:
				; = SETMEDU (src: EXTENDED.ASM:837, BIOS-PP 1273243)
	ld a,0aah		;05bd	3e aa		> .
	scf			;05bf	37		7
	ret			;05c0	c9		.
CHANGED:
				; = CHANGED (src: FDRIVER2.ASM:27, BIOS-PP 1273243)
	ld a,001h		;05c1	3e 01		> .
	and a			;05c3	a7		.
	ret			;05c4	c9		.
GETMEDD:
				; = GETMEDD (src: FDRIVER2.ASM:42, BIOS-PP 1273243)
				; = FDD_5x.GETMED (src: FDD_DRIVER_2.asm:26, BIOS-TT 0271ac3)
	in a,(0e2h)		;05c5	db e2		. .
	ex af,af'		;05c7	08		.
	ld a,0feh		;05c8	3e fe		> .
	out (0e2h),a		;05ca	d3 e2		. .
	ld hl,(0c1e1h)		;05cc	2a e1 c1	* . .
	ld de,(0c1e3h)		;05cf	ed 5b e3 c1	. [ . .
	ld ix,(0c1e5h)		;05d3	dd 2a e5 c1	. * . .
	ld a,(0c1e0h)		;05d7	3a e0 c1	: . .
	ld b,a			;05da	47		G
	ex af,af'		;05db	08		.
	out (0e2h),a		;05dc	d3 e2		. .
	ex af,af'		;05de	08		.
	and a			;05df	a7		.
	ret			;05e0	c9		.
SETMEDD:
				; = SETMEDD (src: FDRIVER2.ASM:68, BIOS-PP 1273243)
				; = FDD_5x.SETMED (src: FDD_DRIVER_2.asm:53, BIOS-TT 0271ac3)
	in a,(0e2h)		;05e1	db e2		. .
	ex af,af'		;05e3	08		.
	ld a,0feh		;05e4	3e fe		> .
	out (0e2h),a		;05e6	d3 e2		. .
	ld a,b			;05e8	78		x
	ld (0c1e1h),hl		;05e9	22 e1 c1	" . .
	ld (0c1e3h),de		;05ec	ed 53 e3 c1	. S . .
	ld (0c1e5h),ix		;05f0	dd 22 e5 c1	. " . .
	ld (0c1e0h),a		;05f4	32 e0 c1	2 . .
	ex af,af'		;05f7	08		.
	out (0e2h),a		;05f8	d3 e2		. .
	ex af,af'		;05fa	08		.
	and a			;05fb	a7		.
	ret			;05fc	c9		.
DETECTD:
				; = DETECTD (src: FDRIVER2.ASM:89, BIOS-PP 1273243)
				; = FDD_5x.DETECT (src: FDD_DRIVER_2.asm:75, BIOS-TT 0271ac3)
	call DOS_ON		;05fd	cd 69 09	. i .
	call FddProbeDensity	;0600	cd 69 06	. i .
	push af			;0603	f5		.
	call DOS_OFF		;0604	cd 70 09	. p .
	pop af			;0607	f1		.
	ret			;0608	c9		.
RESETD:
				; = RESETD (src: FDRIVER2.ASM:101, BIOS-PP 1273243)
				; = FDD_5x.RESET (src: FDD_DRIVER_2.asm:88, BIOS-TT 0271ac3)
	call DOS_ON		;0609	cd 69 09	. i .
	call S_FDD		;060c	cd ed 07	. . .
	call FddProbeDensity	;060f	cd 69 06	. i .
	ld a,004h		;0612	3e 04		> .
	jp c,MOTORFF		;0614	da 26 06	. & .
	call RESWG		;0617	cd 2a 09	. * .
	xor a			;061a	af		.
	out (03fh),a		;061b	d3 3f		. ?
	in a,(00fh)		;061d	db 0f		. .
	ld c,a			;061f	4f		O
	call DOS_OFF		;0620	cd 70 09	. p .
	ld a,c			;0623	79		y
	and a			;0624	a7		.
	ret			;0625	c9		.
MOTORFF:
				; = MOTORFF (src: FDRIVER2.ASM:116, BIOS-PP 1273243)
				; = ~MOTOR_OFF (src: FDD_DRIVER_2.asm:579, BIOS-TT 0271ac3)
	push af			;0626	f5		.
	ld a,0d0h		;0627	3e d0		> .
	out (00fh),a		;0629	d3 0f		. .
	ld a,000h		;062b	3e 00		> .
	out (0ffh),a		;062d	d3 ff		. .
	ld a,03ch		;062f	3e 3c		> <
	out (0ffh),a		;0631	d3 ff		. .
	call DOS_OFF		;0633	cd 70 09	. p .
	pop af			;0636	f1		.
	ret			;0637	c9		.
;---------------------------------------------------------------------------
; Floppy density. The current density is bit 7 of #C1E0 in the system
; page (#FE): 1 = HD (1.44 MB, 500 kbit/s), 0 = DD (720 KB, 250 kbit/s).
; FddFlipDensity toggles it, FddApplyDensity writes it to the hardware
; (FddSetDensityDD / FddSetDensityHD below).
;---------------------------------------------------------------------------
FddFlipDensity:
				; = TURNSPD (src: FDRIVER2.ASM:127, BIOS-PP 1273243)
	in a,(0e2h)		;0638	db e2		. .
	ex af,af'		;063a	08		.
	ld a,0feh		;063b	3e fe		> .
	out (0e2h),a		;063d	d3 e2		. .
	ld a,(0c1e0h)		;063f	3a e0 c1	: . .
	xor 080h		;0642	ee 80		. .
	ld (0c1e0h),a		;0644	32 e0 c1	2 . .
	and 080h		;0647	e6 80		. .
	ex af,af'		;0649	08		.
	out (0e2h),a		;064a	d3 e2		. .
	ex af,af'		;064c	08		.
	jp z,FddSetDensityDD	;064d	ca 77 09	. w .
	jp FddSetDensityHD	;0650	c3 7c 09	. | .
FddApplyDensity:
				; = SPEED (src: FDRIVER2.ASM:141, BIOS-PP 1273243)
	in a,(0e2h)		;0653	db e2		. .
	ex af,af'		;0655	08		.
	ld a,0feh		;0656	3e fe		> .
	out (0e2h),a		;0658	d3 e2		. .
	ld a,(0c1e0h)		;065a	3a e0 c1	: . .
	and 080h		;065d	e6 80		. .
	ex af,af'		;065f	08		.
	out (0e2h),a		;0660	d3 e2		. .
	ex af,af'		;0662	08		.
	jp z,FddSetDensityDD	;0663	ca 77 09	. w .
	jp FddSetDensityHD	;0666	c3 7c 09	. | .
;---------------------------------------------------------------------------
; FddProbeDensity ("DISK_ID" in the 2.17 sources): find the density of the
; disk in the drive.
;   1. apply the current density, seek (command #18);
;   2. READ ADDRESS (#C0) to port #0F (= WD1793 command register through
;      the port table; the code runs from ROM so the #1F operand rewrite
;      does not apply) and poll port #FF (bit 7 INTRQ, bit 6 DRQ) for at
;      most #F000 loops;
;   3. on a time-out flip the density and try again: 4 tries in all, so
;      each density gets two chances.
; Worked example: a 1.44 MB disk while the latch says DD: try 1 times out
; (no ID field is readable at 250 kbit/s), flip to HD, try 2 reads an ID.
; Returns A = #80 (HD) or #00 (DD), Carry = 1 when all 4 tries failed.
;---------------------------------------------------------------------------
FddProbeDensity:
				; = DISK_ID (src: FDRIVER2.ASM:153, BIOS-PP 1273243)
	di			;0669	f3		.
	exx			;066a	d9		.
	call FddApplyDensity	;066b	cd 53 06	. S .
	in a,(03fh)		;066e	db 3f		. ?
	out (07fh),a		;0670	d3 7f		. .
	ld a,018h		;0672	3e 18		> .
	call EXECOM		;0674	cd 2c 09	. , .
	ld c,004h		;0677	0e 04		. .
ID_LP0:
				; = ID_LP0 (src: FDRIVER2.ASM:161, BIOS-PP 1273243)
				; = ~DISK_ID.ID_LP0 (src: FDD_DRIVER_2.asm:633, BIOS-TT 0271ac3)
	ld a,0c0h		;0679	3e c0		> .
	out (00fh),a		;067b	d3 0f		. .
	ld hl,0f000h		;067d	21 00 f0	! . .
ID_LP1:
				; = ID_LP1 (src: FDRIVER2.ASM:164, BIOS-PP 1273243)
				; = ~DISK_ID.ID_LP1 (src: FDD_DRIVER_2.asm:637, BIOS-TT 0271ac3)
	in a,(0ffh)		;0680	db ff		. .
	and 0c0h		;0682	e6 c0		. .
	jr z,ID_LP4		;0684	28 1e		( .
ID_LP2:
				; = ID_LP2 (src: FDRIVER2.ASM:167, BIOS-PP 1273243)
				; = ~DISK_ID.ID_LP2 (src: FDD_DRIVER_2.asm:641, BIOS-TT 0271ac3)
	in a,(07fh)		;0686	db 7f		. .
ID_LP3:
				; = ID_LP3 (src: FDRIVER2.ASM:168, BIOS-PP 1273243)
				; = ~DISK_ID.ID_LP3 (src: FDD_DRIVER_2.asm:643, BIOS-TT 0271ac3)
	in a,(0ffh)		;0688	db ff		. .
	and 0c0h		;068a	e6 c0		. .
	jr z,ID_LP3		;068c	28 fa		( .
	jp p,ID_LP2		;068e	f2 86 06	. . .
	exx			;0691	d9		.
	in a,(0e2h)		;0692	db e2		. .
	ex af,af'		;0694	08		.
	ld a,0feh		;0695	3e fe		> .
	out (0e2h),a		;0697	d3 e2		. .
	ld a,(0c1e0h)		;0699	3a e0 c1	: . .
	ex af,af'		;069c	08		.
	out (0e2h),a		;069d	d3 e2		. .
	ex af,af'		;069f	08		.
	and 080h		;06a0	e6 80		. .
	ei			;06a2	fb		.
	ret			;06a3	c9		.
ID_LP4:
				; = ID_LP4 (src: FDRIVER2.ASM:185, BIOS-PP 1273243)
				; = ~DISK_ID.ID_LP4 (src: FDD_DRIVER_2.asm:660, BIOS-TT 0271ac3)
	dec hl			;06a4	2b		+
	ld a,h			;06a5	7c		|
	or l			;06a6	b5		.
	jp nz,ID_LP1		;06a7	c2 80 06	. . .
	call FddFlipDensity	;06aa	cd 38 06	. 8 .
	dec c			;06ad	0d		.
	jp nz,ID_LP0		;06ae	c2 79 06	. y .
	exx			;06b1	d9		.
	scf			;06b2	37		7
	ei			;06b3	fb		.
	ret			;06b4	c9		.
READD:
				; = READD (src: FDRIVER2.ASM:207, BIOS-PP 1273243)
				; = FDD_5x.READ (src: FDD_DRIVER_2.asm:113, BIOS-TT 0271ac3)
	ex af,af'		;06b5	08		.
	in a,(0e2h)		;06b6	db e2		. .
	ex af,af'		;06b8	08		.
LREADD:
				; = LREADD (src: FDRIVER2.ASM:221, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ (src: FDD_DRIVER_2.asm:128, BIOS-TT 0271ac3)
	push iy			;06b9	fd e5		. .
	push bc			;06bb	c5		.
	push hl			;06bc	e5		.
	push ix			;06bd	dd e5		. .
	ex af,af'		;06bf	08		.
	ld c,a			;06c0	4f		O
	ex af,af'		;06c1	08		.
	push bc			;06c2	c5		.
	call DOS_ON		;06c3	cd 69 09	. i .
	call S_FDD		;06c6	cd ed 07	. . .
	call FddApplyDensity	;06c9	cd 53 06	. S .
	call NTRACK		;06cc	cd 3e 09	. > .
	pop bc			;06cf	c1		.
	ex de,hl		;06d0	eb		.
	in a,(0e2h)		;06d1	db e2		. .
	ex af,af'		;06d3	08		.
	ld a,0feh		;06d4	3e fe		> .
	out (0e2h),a		;06d6	d3 e2		. .
	ld iy,(0c1e5h)		;06d8	fd 2a e5 c1	. * . .
	defb 0ddh,061h ;ld ixh,c	;06dc	dd 61		. a
	ld a,(0c1e1h)		;06de	3a e1 c1	: . .
	ld c,a			;06e1	4f		O
	ex af,af'		;06e2	08		.
	out (0e2h),a		;06e3	d3 e2		. .
DSK_LP:
				; = DSK_LP (src: FDRIVER2.ASM:248, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ.DSK_LP (src: FDD_DRIVER_2.asm:156, BIOS-TT 0271ac3)
	ld a,d			;06e5	7a		z
	exx			;06e6	d9		.
	call SEEK		;06e7	cd e2 08	. . .
	exx			;06ea	d9		.
	push de			;06eb	d5		.
	push bc			;06ec	c5		.
	push hl			;06ed	e5		.
	call RD_SEC		;06ee	cd 0a 08	. . .
	jp c,ERRDOS		;06f1	da 3e 07	. > .
	defb 0fdh,054h ;ld d,iyh	;06f4	fd 54		. T
	defb 0fdh,05dh ;ld e,iyl	;06f6	fd 5d		. ]
	pop hl			;06f8	e1		.
	pop bc			;06f9	c1		.
	add hl,de		;06fa	19		.
	jr nc,THISRD		;06fb	30 15		0 .
	in a,(0e2h)		;06fd	db e2		. .
	ex af,af'		;06ff	08		.
	ld a,0feh		;0700	3e fe		> .
	out (0e2h),a		;0702	d3 e2		. .
	ld d,0c2h		;0704	16 c2		. .
	defb 0ddh,05ch ;ld e,ixh	;0706	dd 5c		. \
	ld a,(de)		;0708	1a		.
	defb 0ddh,067h ;ld ixh,a	;0709	dd 67		. g
	ex af,af'		;070b	08		.
	out (0e2h),a		;070c	d3 e2		. .
	set 7,h			;070e	cb fc		. .
	set 6,h			;0710	cb f4		. .
THISRD:
				; = THISRD (src: FDRIVER2.ASM:275, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ.THISRD (src: FDD_DRIVER_2.asm:184, BIOS-TT 0271ac3)
	pop de			;0712	d1		.
	ld a,c			;0713	79		y
	inc e			;0714	1c		.
	cp e			;0715	bb		.
	jp nz,NINC_T		;0716	c2 1c 07	. . .
	ld e,000h		;0719	1e 00		. .
	inc d			;071b	14		.
NINC_T:
				; = NINC_T (src: FDRIVER2.ASM:282, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ.NINC_T (src: FDD_DRIVER_2.asm:192, BIOS-TT 0271ac3)
	djnz DSK_LP		;071c	10 c7		. .
RETDOS:
				; = RETDOS (src: FDRIVER2.ASM:283, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ.RETDOS (src: FDD_DRIVER_2.asm:194, BIOS-TT 0271ac3)
	call DOS_OFF		;071e	cd 70 09	. p .
	defb 0ddh,07ch ;ld a,ixh	;0721	dd 7c		. |
	ex af,af'		;0723	08		.
	ex de,hl		;0724	eb		.
	pop ix			;0725	dd e1		. .
	pop hl			;0727	e1		.
	pop bc			;0728	c1		.
	pop iy			;0729	fd e1		. .
	ld a,b			;072b	78		x
	ld c,b			;072c	48		H
	inc b			;072d	04		.
	dec b			;072e	05		.
	ld b,000h		;072f	06 00		. .
	jr nz,ADD8BIT		;0731	20 01		  .
	inc b			;0733	04		.
ADD8BIT:
				; = ADD8BIT (src: FDRIVER2.ASM:298, BIOS-PP 1273243)
				; = FDD_5x.LONG_READ.ADD8BIT (src: FDD_DRIVER_2.asm:210, BIOS-TT 0271ac3)
	add ix,bc		;0734	dd 09		. .
	ld bc,RomStart		;0736	01 00 00	. . .
	adc hl,bc		;0739	ed 4a		. J
	ld b,a			;073b	47		G
	xor a			;073c	af		.
	ret			;073d	c9		.
ERRDOS:
				; = ERRDOS (src: FDRIVER2.ASM:305, BIOS-PP 1273243)
	pop hl			;073e	e1		.
	pop bc			;073f	c1		.
	pop de			;0740	d1		.
	call DOS_OFF		;0741	cd 70 09	. p .
	ex de,hl		;0744	eb		.
	ex af,af'		;0745	08		.
	exx			;0746	d9		.
	defb 0ddh,04ch ;ld c,ixh	;0747	dd 4c		. L
	exx			;0749	d9		.
	ld a,b			;074a	78		x
	pop ix			;074b	dd e1		. .
	pop hl			;074d	e1		.
	pop bc			;074e	c1		.
	pop iy			;074f	fd e1		. .
	push af			;0751	f5		.
	ld c,a			;0752	4f		O
	ld a,b			;0753	78		x
	sub c			;0754	91		.
	ld c,a			;0755	4f		O
	ld b,000h		;0756	06 00		. .
	add ix,bc		;0758	dd 09		. .
	ld c,b			;075a	48		H
	adc hl,bc		;075b	ed 4a		. J
	pop bc			;075d	c1		.
	exx			;075e	d9		.
	ld a,c			;075f	79		y
	exx			;0760	d9		.
	ex af,af'		;0761	08		.
	scf			;0762	37		7
	ret			;0763	c9		.
WRITED:
				; = WRITED (src: FDRIVER2.ASM:346, BIOS-PP 1273243)
				; = FDD_5x.WRITE (src: FDD_DRIVER_2.asm:260, BIOS-TT 0271ac3)
	ex af,af'		;0764	08		.
	in a,(0e2h)		;0765	db e2		. .
	ex af,af'		;0767	08		.
LWRITED:
				; = LWRITED (src: FDRIVER2.ASM:360, BIOS-PP 1273243)
				; = FDD_5x.LONG_WRITE (src: FDD_DRIVER_2.asm:275, BIOS-TT 0271ac3)
	push iy			;0768	fd e5		. .
	push bc			;076a	c5		.
	push hl			;076b	e5		.
	push ix			;076c	dd e5		. .
	ex af,af'		;076e	08		.
	ld c,a			;076f	4f		O
	ex af,af'		;0770	08		.
	push bc			;0771	c5		.
	call DOS_ON		;0772	cd 69 09	. i .
	call S_FDD		;0775	cd ed 07	. . .
	call FddApplyDensity	;0778	cd 53 06	. S .
	call NTRACK		;077b	cd 3e 09	. > .
	pop bc			;077e	c1		.
	ex de,hl		;077f	eb		.
	in a,(0e2h)		;0780	db e2		. .
	ex af,af'		;0782	08		.
	ld a,0feh		;0783	3e fe		> .
	out (0e2h),a		;0785	d3 e2		. .
	ld iy,(0c1e5h)		;0787	fd 2a e5 c1	. * . .
	defb 0ddh,061h ;ld ixh,c	;078b	dd 61		. a
	ld a,(0c1e1h)		;078d	3a e1 c1	: . .
	ld c,a			;0790	4f		O
	ex af,af'		;0791	08		.
	out (0e2h),a		;0792	d3 e2		. .
DSK_LP2:
				; = DSK_LP2 (src: FDRIVER2.ASM:387, BIOS-PP 1273243)
				; = FDD_5x.LONG_WRITE.DSK_LP2 (src: FDD_DRIVER_2.asm:303, BIOS-TT 0271ac3)
	ld a,d			;0794	7a		z
	exx			;0795	d9		.
	call SEEK		;0796	cd e2 08	. . .
	exx			;0799	d9		.
	push de			;079a	d5		.
	push bc			;079b	c5		.
	push hl			;079c	e5		.
	call WR_SEC		;079d	cd 73 08	. s .
	jp c,ERRDOS		;07a0	da 3e 07	. > .
	defb 0fdh,054h ;ld d,iyh	;07a3	fd 54		. T
	defb 0fdh,05dh ;ld e,iyl	;07a5	fd 5d		. ]
	pop hl			;07a7	e1		.
	pop bc			;07a8	c1		.
	add hl,de		;07a9	19		.
	jr nc,THISWR		;07aa	30 15		0 .
	in a,(0e2h)		;07ac	db e2		. .
	ex af,af'		;07ae	08		.
	ld a,0feh		;07af	3e fe		> .
	out (0e2h),a		;07b1	d3 e2		. .
	ld d,0c2h		;07b3	16 c2		. .
	defb 0ddh,05ch ;ld e,ixh	;07b5	dd 5c		. \
	ld a,(de)		;07b7	1a		.
	defb 0ddh,067h ;ld ixh,a	;07b8	dd 67		. g
	ex af,af'		;07ba	08		.
	out (0e2h),a		;07bb	d3 e2		. .
	set 7,h			;07bd	cb fc		. .
	set 6,h			;07bf	cb f4		. .
THISWR:
				; = THISWR (src: FDRIVER2.ASM:414, BIOS-PP 1273243)
				; = FDD_5x.LONG_WRITE.THISWR (src: FDD_DRIVER_2.asm:331, BIOS-TT 0271ac3)
	pop de			;07c1	d1		.
	ld a,c			;07c2	79		y
	inc e			;07c3	1c		.
	cp e			;07c4	bb		.
	jp nz,NINC_T2		;07c5	c2 cb 07	. . .
	ld e,000h		;07c8	1e 00		. .
	inc d			;07ca	14		.
NINC_T2:
				; = NINC_T2 (src: FDRIVER2.ASM:421, BIOS-PP 1273243)
				; = FDD_5x.LONG_WRITE.NINC_T2 (src: FDD_DRIVER_2.asm:339, BIOS-TT 0271ac3)
	djnz DSK_LP2		;07cb	10 c7		. .
	call DOS_OFF		;07cd	cd 70 09	. p .
	defb 0ddh,07ch ;ld a,ixh	;07d0	dd 7c		. |
	ex af,af'		;07d2	08		.
	ex de,hl		;07d3	eb		.
	pop ix			;07d4	dd e1		. .
	pop hl			;07d6	e1		.
	pop bc			;07d7	c1		.
	pop iy			;07d8	fd e1		. .
	ld a,b			;07da	78		x
	ld c,b			;07db	48		H
	inc b			;07dc	04		.
	dec b			;07dd	05		.
	ld b,000h		;07de	06 00		. .
	jr nz,ADW8BIT		;07e0	20 01		  .
	inc b			;07e2	04		.
ADW8BIT:
				; = ADW8BIT (src: FDRIVER2.ASM:437, BIOS-PP 1273243)
				; = FDD_5x.LONG_WRITE.ADW8BIT (src: FDD_DRIVER_2.asm:356, BIOS-TT 0271ac3)
	add ix,bc		;07e3	dd 09		. .
	ld bc,RomStart		;07e5	01 00 00	. . .
	adc hl,bc		;07e8	ed 4a		. J
	ld b,a			;07ea	47		G
	xor a			;07eb	af		.
	ret			;07ec	c9		.
S_FDD:
				; = S_FDD (src: FDRIVER2.ASM:444, BIOS-PP 1273243)
	push bc			;07ed	c5		.
	and 001h		;07ee	e6 01		. .
	ld b,a			;07f0	47		G
	or 03ch			;07f1	f6 3c		. <
	out (0ffh),a		;07f3	d3 ff		. .
	in a,(0e2h)		;07f5	db e2		. .
	ex af,af'		;07f7	08		.
	ld a,0feh		;07f8	3e fe		> .
	out (0e2h),a		;07fa	d3 e2		. .
	ld a,(0c1e0h)		;07fc	3a e0 c1	: . .
	and 0feh		;07ff	e6 fe		. .
	or b			;0801	b0		.
	ld (0c1e0h),a		;0802	32 e0 c1	2 . .
	ex af,af'		;0805	08		.
	out (0e2h),a		;0806	d3 e2		. .
	pop bc			;0808	c1		.
	ret			;0809	c9		.
RD_SEC:
				; = RD_SEC (src: FDRIVER2.ASM:462, BIOS-PP 1273243)
	ld d,005h		;080a	16 05		. .
RRETRY:
				; = RRETRY (src: FDRIVER2.ASM:463, BIOS-PP 1273243)
				; = RD_SEC.RRETRY (src: FDD_DRIVER_2.asm:369, BIOS-TT 0271ac3)
	di			;080c	f3		.
	push de			;080d	d5		.
	ld a,e			;080e	7b		{
	inc a			;080f	3c		<
	out (05fh),a		;0810	d3 5f		. _
FDREAD:
				; = FDREAD (src: FDRIVER2.ASM:471, BIOS-PP 1273243)
				; = RD_SEC.FDREAD (src: FDD_DRIVER_2.asm:375, BIOS-TT 0271ac3)
	in a,(0e2h)		;0812	db e2		. .
	ex af,af'		;0814	08		.
	defb 0ddh,07ch ;ld a,ixh	;0815	dd 7c		. |
	out (0e2h),a		;0817	d3 e2		. .
	ld b,004h		;0819	06 04		. .
	ld c,07fh		;081b	0e 7f		. .
	ld a,080h		;081d	3e 80		> .
	out (00fh),a		;081f	d3 0f		. .
FDR001:
				; = FDR001 (src: FDRIVER2.ASM:479, BIOS-PP 1273243)
				; = RD_SEC.FDR001 (src: FDD_DRIVER_2.asm:384, BIOS-TT 0271ac3)
	in a,(0ffh)		;0821	db ff		. .
	and 0c0h		;0823	e6 c0		. .
	jr nz,FDR004		;0825	20 0a		  .
	inc de			;0827	13		.
	ld a,e			;0828	7b		{
	or d			;0829	b2		.
	jr nz,FDR001		;082a	20 f5		  .
	djnz FDR001		;082c	10 f3		. .
	scf			;082e	37		7
	jr FDR005		;082f	18 0b		. .
FDR004:
				; = FDR004 (src: FDRIVER2.ASM:489, BIOS-PP 1273243)
				; = RD_SEC.FDR004 (src: FDD_DRIVER_2.asm:395, BIOS-TT 0271ac3)
	ini			;0831	ed a2		. .
FDR002:
				; = FDR002 (src: FDRIVER2.ASM:490, BIOS-PP 1273243)
				; = RD_SEC.FDR002 (src: FDD_DRIVER_2.asm:397, BIOS-TT 0271ac3)
	in a,(0ffh)		;0833	db ff		. .
	and 0c0h		;0835	e6 c0		. .
	jr z,FDR002		;0837	28 fa		( .
	jp p,FDR004		;0839	f2 31 08	. 1 .
FDR005:
				; = FDR005 (src: FDRIVER2.ASM:494, BIOS-PP 1273243)
				; = RD_SEC.FDR005 (src: FDD_DRIVER_2.asm:402, BIOS-TT 0271ac3)
	ex af,af'		;083c	08		.
	out (0e2h),a		;083d	d3 e2		. .
	ex af,af'		;083f	08		.
	pop de			;0840	d1		.
	ei			;0841	fb		.
	in a,(00fh)		;0842	db 0f		. .
	ld c,a			;0844	4f		O
	jp c,ERR_XRD		;0845	da 5e 08	. ^ .
	and 07fh		;0848	e6 7f		. .
	ret z			;084a	c8		.
	bit 2,c			;084b	cb 51		. Q
	jr nz,ERDATA		;084d	20 12		  .
	ld a,005h		;084f	3e 05		> .
	dec d			;0851	15		.
	jp z,RSTOP		;0852	ca 66 08	. f .
	push de			;0855	d5		.
	call RESWG		;0856	cd 2a 09	. * .
	defb 0ddh,07dh ;ld a,ixl	;0859	dd 7d		. }
	call SEEK		;085b	cd e2 08	. . .
ERR_XRD:
				; = ERR_XRD (src: FDRIVER2.ASM:516, BIOS-PP 1273243)
				; = RD_SEC.ERR_XRD (src: FDD_DRIVER_2.asm:425, BIOS-TT 0271ac3)
	pop de			;085e	d1		.
	jr RRETRY		;085f	18 ab		. .
ERDATA:
				; = ERDATA (src: FDRIVER2.ASM:518, BIOS-PP 1273243)
				; = RD_SEC.ERDATA (src: FDD_DRIVER_2.asm:428, BIOS-TT 0271ac3)
	dec d			;0861	15		.
	jr nz,RRETRY		;0862	20 a8		  .
ERRRD:
				; = ERRRD (src: FDRIVER2.ASM:520, BIOS-PP 1273243)
				; = RD_SEC.ERRRD (src: FDD_DRIVER_2.asm:431, BIOS-TT 0271ac3)
	ld a,009h		;0864	3e 09		> .
RSTOP:
				; = RSTOP (src: FDRIVER2.ASM:521, BIOS-PP 1273243)
				; = RD_SEC.RSTOP (src: FDD_DRIVER_2.asm:432, BIOS-TT 0271ac3)
	ex af,af'		;0866	08		.
	ld a,0d0h		;0867	3e d0		> .
	out (00fh),a		;0869	d3 0f		. .
	ex af,af'		;086b	08		.
	bit 0,c			;086c	cb 41		. A
	scf			;086e	37		7
	ret z			;086f	c8		.
	ld a,006h		;0870	3e 06		> .
	ret			;0872	c9		.
WR_SEC:
				; = WR_SEC (src: FDRIVER2.ASM:531, BIOS-PP 1273243)
	ld d,005h		;0873	16 05		. .
WRETRY:
				; = WRETRY (src: FDRIVER2.ASM:532, BIOS-PP 1273243)
				; = WR_SEC.WRETRY (src: FDD_DRIVER_2.asm:448, BIOS-TT 0271ac3)
	di			;0875	f3		.
	push de			;0876	d5		.
	ld a,e			;0877	7b		{
	inc a			;0878	3c		<
	out (05fh),a		;0879	d3 5f		. _
FDWRITE:
				; = FDWRITE (src: FDRIVER2.ASM:540, BIOS-PP 1273243)
				; = WR_SEC.FDWRITE (src: FDD_DRIVER_2.asm:454, BIOS-TT 0271ac3)
	in a,(0e2h)		;087b	db e2		. .
	ex af,af'		;087d	08		.
	defb 0ddh,07ch ;ld a,ixh	;087e	dd 7c		. |
	out (0e2h),a		;0880	d3 e2		. .
	ld b,004h		;0882	06 04		. .
	ld c,07fh		;0884	0e 7f		. .
	ld a,0a0h		;0886	3e a0		> .
	out (00fh),a		;0888	d3 0f		. .
FDW001:
				; = FDW001 (src: FDRIVER2.ASM:548, BIOS-PP 1273243)
				; = WR_SEC.FDW001 (src: FDD_DRIVER_2.asm:463, BIOS-TT 0271ac3)
	in a,(0ffh)		;088a	db ff		. .
	and 0c0h		;088c	e6 c0		. .
	jr nz,FDW004		;088e	20 0a		  .
	inc de			;0890	13		.
	ld a,e			;0891	7b		{
	or d			;0892	b2		.
	jr nz,FDW001		;0893	20 f5		  .
	djnz FDW001		;0895	10 f3		. .
	scf			;0897	37		7
	jr FDW005		;0898	18 0b		. .
FDW004:
				; = FDW004 (src: FDRIVER2.ASM:558, BIOS-PP 1273243)
				; = WR_SEC.FDW004 (src: FDD_DRIVER_2.asm:474, BIOS-TT 0271ac3)
	outi			;089a	ed a3		. .
FDW002:
				; = FDW002 (src: FDRIVER2.ASM:559, BIOS-PP 1273243)
				; = WR_SEC.FDW002 (src: FDD_DRIVER_2.asm:476, BIOS-TT 0271ac3)
	in a,(0ffh)		;089c	db ff		. .
	and 0c0h		;089e	e6 c0		. .
	jr z,FDW002		;08a0	28 fa		( .
	jp p,FDW004		;08a2	f2 9a 08	. . .
FDW005:
				; = FDW005 (src: FDRIVER2.ASM:563, BIOS-PP 1273243)
				; = WR_SEC.FDW005 (src: FDD_DRIVER_2.asm:481, BIOS-TT 0271ac3)
	ex af,af'		;08a5	08		.
	out (0e2h),a		;08a6	d3 e2		. .
	ex af,af'		;08a8	08		.
	pop de			;08a9	d1		.
	ei			;08aa	fb		.
	in a,(00fh)		;08ab	db 0f		. .
	ld c,a			;08ad	4f		O
	jp c,ERR_XWR		;08ae	da c4 08	. . .
	and 07fh		;08b1	e6 7f		. .
	ret z			;08b3	c8		.
	bit 6,c			;08b4	cb 71		. q
	ld a,008h		;08b6	3e 08		> .
	jr nz,WSTOP		;08b8	20 1b		  .
	bit 2,c			;08ba	cb 51		. Q
	jr nz,EWDATA		;08bc	20 12		  .
	ld a,005h		;08be	3e 05		> .
	dec d			;08c0	15		.
	jp z,WSTOP		;08c1	ca d5 08	. . .
ERR_XWR:
				; = ERR_XWR (src: FDRIVER2.ASM:584, BIOS-PP 1273243)
				; = WR_SEC.ERR_XWR (src: FDD_DRIVER_2.asm:503, BIOS-TT 0271ac3)
	push de			;08c4	d5		.
	call RESWG		;08c5	cd 2a 09	. * .
	defb 0ddh,07dh ;ld a,ixl	;08c8	dd 7d		. }
	call SEEK		;08ca	cd e2 08	. . .
	pop de			;08cd	d1		.
	jr WRETRY		;08ce	18 a5		. .
EWDATA:
				; = EWDATA (src: FDRIVER2.ASM:590, BIOS-PP 1273243)
				; = WR_SEC.EWDATA (src: FDD_DRIVER_2.asm:510, BIOS-TT 0271ac3)
	dec d			;08d0	15		.
	jr nz,WRETRY		;08d1	20 a2		  .
ERRWR:
				; = ERRWR (src: FDRIVER2.ASM:592, BIOS-PP 1273243)
				; = WR_SEC.ERRWR (src: FDD_DRIVER_2.asm:513, BIOS-TT 0271ac3)
	ld a,00ah		;08d3	3e 0a		> .
WSTOP:
				; = WSTOP (src: FDRIVER2.ASM:593, BIOS-PP 1273243)
				; = WR_SEC.WSTOP (src: FDD_DRIVER_2.asm:514, BIOS-TT 0271ac3)
	ex af,af'		;08d5	08		.
	ld a,0d0h		;08d6	3e d0		> .
	out (00fh),a		;08d8	d3 0f		. .
	ex af,af'		;08da	08		.
	bit 0,c			;08db	cb 41		. A
	scf			;08dd	37		7
	ret z			;08de	c8		.
	ld a,006h		;08df	3e 06		> .
	ret			;08e1	c9		.
SEEK:
	defb 0ddh,06fh ;ld ixl,a	;08e2	dd 6f		. o
	ld c,a			;08e4	4f		O
	in a,(0e2h)		;08e5	db e2		. .
	ex af,af'		;08e7	08		.
	ld a,0feh		;08e8	3e fe		> .
	out (0e2h),a		;08ea	d3 e2		. .
	ld a,(0c1e0h)		;08ec	3a e0 c1	: . .
	and 001h		;08ef	e6 01		. .
	ex af,af'		;08f1	08		.
	out (0e2h),a		;08f2	d3 e2		. .
	ex af,af'		;08f4	08		.
	srl c			;08f5	cb 39		. 9
	jr c,GT001		;08f7	38 02		8 .
	or 03ch			;08f9	f6 3c		. <
GT001:
				; = GT001 (src: FDRIVER2.ASM:617, BIOS-PP 1273243)
				; = SEEK.GT001 (src: FDD_DRIVER_2.asm:689, BIOS-TT 0271ac3)
	or 02ch			;08fb	f6 2c		. ,
	out (0ffh),a		;08fd	d3 ff		. .
	in a,(03fh)		;08ff	db 3f		. ?
	cp c			;0901	b9		.
	push bc			;0902	c5		.
	call nz,P50ms		;0903	c4 1f 09	. . .
	pop bc			;0906	c1		.
	ld a,c			;0907	79		y
l0908h:
	out (07fh),a		;0908	d3 7f		. .
	in a,(03fh)		;090a	db 3f		. ?
	cp c			;090c	b9		.
	ex af,af'		;090d	08		.
	ld a,018h		;090e	3e 18		> .
	call EXECOM		;0910	cd 2c 09	. , .
	ret c			;0913	d8		.
	ex af,af'		;0914	08		.
	ld a,c			;0915	79		y
	out (03fh),a		;0916	d3 3f		. ?
	ret z			;0918	c8		.
STOL:
				; = STOL (src: FDRIVER2.ASM:636, BIOS-PP 1273243)
				; = SEEK.STOL (src: FDD_DRIVER_2.asm:708, BIOS-TT 0271ac3)
	push bc			;0919	c5		.
	call P50ms		;091a	cd 1f 09	. . .
	pop bc			;091d	c1		.
	ret			;091e	c9		.
P50ms:
				; = P50ms (src: FDRIVER2.ASM:647, BIOS-PP 1273243)
	ld a,00ch		;091f	3e 0c		> .
P1ms:
				; = P1ms (src: FDRIVER2.ASM:648, BIOS-PP 1273243)
				; = P50ms.P1ms (src: FDD_DRIVER_2.asm:719, BIOS-TT 0271ac3)
	ld c,0ffh		;0921	0e ff		. .
PMS:
				; = PMS (src: FDRIVER2.ASM:649, BIOS-PP 1273243)
				; = P50ms.PMS (src: FDD_DRIVER_2.asm:720, BIOS-TT 0271ac3)
	dec c			;0923	0d		.
	jr nz,PMS		;0924	20 fd		  .
	dec a			;0926	3d		=
	jr nz,P1ms		;0927	20 f8		  .
	ret			;0929	c9		.
RESWG:
				; = RESWG (src: FDRIVER2.ASM:655, BIOS-PP 1273243)
	ld a,008h		;092a	3e 08		> .
EXECOM:
				; = EXECOM (src: FDRIVER2.ASM:656, BIOS-PP 1273243)
	out (00fh),a		;092c	d3 0f		. .
	ld hl,RomStart		;092e	21 00 00	! . .
WREST:
				; = WREST (src: FDRIVER2.ASM:658, BIOS-PP 1273243)
				; = EXECOM.WREST (src: FDD_DRIVER_2.asm:731, BIOS-TT 0271ac3)
	dec hl			;0931	2b		+
	ld a,h			;0932	7c		|
	or l			;0933	b5		.
	scf			;0934	37		7
	ret z			;0935	c8		.
	in a,(0ffh)		;0936	db ff		. .
	and 080h		;0938	e6 80		. .
	jr z,WREST		;093a	28 f5		( .
	and a			;093c	a7		.
	ret			;093d	c9		.
NTRACK:
				; = NTRACK (src: FDRIVER2.ASM:677, BIOS-PP 1273243)
	push hl			;093e	e5		.
	ex (sp),ix		;093f	dd e3		. .
	pop hl			;0941	e1		.
	in a,(0e2h)		;0942	db e2		. .
	ex af,af'		;0944	08		.
	ld a,0feh		;0945	3e fe		> .
	out (0e2h),a		;0947	d3 e2		. .
	ld a,(0c1e1h)		;0949	3a e1 c1	: . .
	ld c,a			;094c	4f		O
	ld b,000h		;094d	06 00		. .
	ex af,af'		;094f	08		.
	out (0e2h),a		;0950	d3 e2		. .
	xor a			;0952	af		.
NTRK:
				; = NTRK (src: FDRIVER2.ASM:691, BIOS-PP 1273243)
				; = NTRACK.NTRK (src: FDD_DRIVER_2.asm:762, BIOS-TT 0271ac3)
	inc a			;0953	3c		<
	sbc hl,bc		;0954	ed 42		. B
	jr nc,NTRK		;0956	30 fb		0 .
	ex af,af'		;0958	08		.
	defb 0ddh,07dh ;ld a,ixl	;0959	dd 7d		. }
	defb 0ddh,0b4h ;or ixh	;095b	dd b4		. .
	jr z,NTRK3		;095d	28 05		( .
	ex af,af'		;095f	08		.
	dec ix			;0960	dd 2b		. +
	jr NTRK			;0962	18 ef		. .
NTRK3:
				; = NTRK3 (src: FDRIVER2.ASM:701, BIOS-PP 1273243)
				; = NTRACK.NTRK3 (src: FDD_DRIVER_2.asm:772, BIOS-TT 0271ac3)
	ex af,af'		;0964	08		.
	add hl,bc		;0965	09		.
	dec a			;0966	3d		=
	ld h,a			;0967	67		g
	ret			;0968	c9		.
DOS_ON:
				; = DOS_ON (src: FDRIVER2.ASM:707, BIOS-PP 1273243)
	ex af,af'		;0969	08		.
	ld a,01dh		;096a	3e 1d		> .
	out (07ch),a		;096c	d3 7c		. |
	ex af,af'		;096e	08		.
	ret			;096f	c9		.
DOS_OFF:
				; = DOS_OFF (src: FDRIVER2.ASM:713, BIOS-PP 1273243)
	ex af,af'		;0970	08		.
	ld a,005h		;0971	3e 05		> .
	out (07ch),a		;0973	d3 7c		. |
	ex af,af'		;0975	08		.
	ret			;0976	c9		.
;---------------------------------------------------------------------------
; The density latch: OUT (#BD),A puts A on address lines A15-A8, so
; A = #01 -> port #01BD -> code #16 (720 KB) and A = #21 -> #21BD ->
; code #17 (1.44 MB) in the port table (hardware-reference.md 4.4).
;---------------------------------------------------------------------------
FddSetDensityDD:
				; = ~FDD.SET720 (src: FDD_DRIVER_2.asm:545, BIOS-TT 0271ac3)
	ld a,001h		;0977	3e 01		> .
	out (0bdh),a		;0979	d3 bd		. .
	ret			;097b	c9		.
FddSetDensityHD:
	ld a,021h		;097c	3e 21		> !
	out (0bdh),a		;097e	d3 bd		. .
	ret			;0980	c9		.
RESETH:
				; = RESETH (src: HDRIVER6.ASM:64, BIOS-PP 1273243)
	xor a			;0981	af		.
	ret			;0982	c9		.
SELECTH:
				; = SELECTH (src: HDRIVER6.ASM:67, BIOS-PP 1273243)
	and 00fh		;0983	e6 0f		. .
	ld iy,0c1c0h		;0985	fd 21 c0 c1	. ! . .
	jr z,SELHH		;0989	28 07		( .
	dec a			;098b	3d		=
	ld iy,0c1c8h		;098c	fd 21 c8 c1	. ! . .
	jr nz,NODRIVE		;0990	20 1c		  .
SELHH:
				; = SELHH (src: HDRIVER6.ASM:73, BIOS-PP 1273243)
	exx			;0992	d9		.
	ld c,0e2h		;0993	0e e2		. .
	in b,(c)		;0995	ed 40		. @
	ld a,0feh		;0997	3e fe		> .
	out (c),a		;0999	ed 79		. y
	ld a,(iy+007h)		;099b	fd 7e 07	. ~ .
	cp 001h			;099e	fe 01		. .
	ld a,(iy+000h)		;09a0	fd 7e 00	. ~ .
	out (c),b		;09a3	ed 41		. A
	ld bc,04152h		;09a5	01 52 41	. R A
	res 0,a			;09a8	cb 87		. .
	out (c),a		;09aa	ed 79		. y
	exx			;09ac	d9		.
	ret z			;09ad	c8		.
NODRIVE:
				; = NODRIVE (src: HDRIVER6.ASM:86, BIOS-PP 1273243)
	ld a,002h		;09ae	3e 02		> .
	scf			;09b0	37		7
	ret			;09b1	c9		.
GETMEDH:
				; = GETMEDH (src: HDRIVER6.ASM:100, BIOS-PP 1273243)
				; = HDD_5x.GETMED (src: HDD_DRIVER_6.asm:140, BIOS-TT 0271ac3)
	call SELECTH		;09b2	cd 83 09	. . .
	ret c			;09b5	d8		.
	in a,(0e2h)		;09b6	db e2		. .
	ex af,af'		;09b8	08		.
	ld a,0feh		;09b9	3e fe		> .
	out (0e2h),a		;09bb	d3 e2		. .
	ld l,(iy+001h)		;09bd	fd 6e 01	. n .
	ld h,(iy+002h)		;09c0	fd 66 02	. f .
	ld e,(iy+003h)		;09c3	fd 5e 03	. ^ .
	ld d,(iy+004h)		;09c6	fd 56 04	. V .
	ld b,(iy+000h)		;09c9	fd 46 00	. F .
	ld ix,00200h		;09cc	dd 21 00 02	. ! . .
	ex af,af'		;09d0	08		.
	out (0e2h),a		;09d1	d3 e2		. .
	ex af,af'		;09d3	08		.
	and a			;09d4	a7		.
	ret			;09d5	c9		.
SETMEDH:
				; = SETMEDH (src: HDRIVER6.ASM:128, BIOS-PP 1273243)
				; = HDD_5x.SETMED (src: HDD_DRIVER_6.asm:169, BIOS-TT 0271ac3)
	call SELECTH		;09d6	cd 83 09	. . .
	ret c			;09d9	d8		.
	in a,(0e2h)		;09da	db e2		. .
	ex af,af'		;09dc	08		.
	ld a,0feh		;09dd	3e fe		> .
	out (0e2h),a		;09df	d3 e2		. .
	ld (iy+001h),l		;09e1	fd 75 01	. u .
	ld (iy+002h),h		;09e4	fd 74 02	. t .
	ld (iy+003h),e		;09e7	fd 73 03	. s .
	ld (iy+004h),d		;09ea	fd 72 04	. r .
	ld (iy+000h),b		;09ed	fd 70 00	. p .
	ex af,af'		;09f0	08		.
	out (0e2h),a		;09f1	d3 e2		. .
	ex af,af'		;09f3	08		.
	and a			;09f4	a7		.
	ret			;09f5	c9		.
READH:
				; = READH (src: HDRIVER6.ASM:156, BIOS-PP 1273243)
				; = HDD_5x.READ (src: HDD_DRIVER_6.asm:198, BIOS-TT 0271ac3)
	ex af,af'		;09f6	08		.
	in a,(0e2h)		;09f7	db e2		. .
	ex af,af'		;09f9	08		.
LREADH:
				; = LREADH (src: HDRIVER6.ASM:171, BIOS-PP 1273243)
				; = HDD_5x.LONG_READ (src: HDD_DRIVER_6.asm:214, BIOS-TT 0271ac3)
	push iy			;09fa	fd e5		. .
	exx			;09fc	d9		.
	ld c,089h		;09fd	0e 89		. .
	in b,(c)		;09ff	ed 40		. @
	push bc			;0a01	c5		.
	ld e,0c0h		;0a02	1e c0		. .
	out (c),e		;0a04	ed 59		. Y
	exx			;0a06	d9		.
	push bc			;0a07	c5		.
	push ix			;0a08	dd e5		. .
	push hl			;0a0a	e5		.
	call RDS000		;0a0b	cd 52 0a	. R .
	ex de,hl		;0a0e	eb		.
	jp c,HERRRD0		;0a0f	da 30 0a	. 0 .
	defb 0ddh,07ch ;ld a,ixh	;0a12	dd 7c		. |
	ex af,af'		;0a14	08		.
	pop hl			;0a15	e1		.
	pop ix			;0a16	dd e1		. .
	pop bc			;0a18	c1		.
	xor a			;0a19	af		.
	cp b			;0a1a	b8		.
	ld c,b			;0a1b	48		H
	ld b,a			;0a1c	47		G
	jr nz,RNOT256		;0a1d	20 09		  .
	inc b			;0a1f	04		.
	add ix,bc		;0a20	dd 09		. .
	ld b,c			;0a22	41		A
	adc hl,bc		;0a23	ed 4a		. J
	ex af,af'		;0a25	08		.
	jr RST8RDR		;0a26	18 21		. !
RNOT256:
				; = RNOT256 (src: HDRIVER6.ASM:202, BIOS-PP 1273243)
	add ix,bc		;0a28	dd 09		. .
	ld c,b			;0a2a	48		H
	adc hl,bc		;0a2b	ed 4a		. J
	ex af,af'		;0a2d	08		.
	jr RST8RDR		;0a2e	18 19		. .
HERRRD0:
				; = HERRRD0 (src: HDRIVER6.ASM:208, BIOS-PP 1273243)
	ld b,a			;0a30	47		G
	defb 0ddh,04dh ;ld c,ixl	;0a31	dd 4d		. M
	defb 0ddh,07ch ;ld a,ixh	;0a33	dd 7c		. |
	ex af,af'		;0a35	08		.
	pop hl			;0a36	e1		.
	pop ix			;0a37	dd e1		. .
	push bc			;0a39	c5		.
	ld b,000h		;0a3a	06 00		. .
	add ix,bc		;0a3c	dd 09		. .
	ld c,b			;0a3e	48		H
	adc hl,bc		;0a3f	ed 4a		. J
	pop bc			;0a41	c1		.
	pop af			;0a42	f1		.
	sub c			;0a43	91		.
	ld c,a			;0a44	4f		O
	ld a,b			;0a45	78		x
	ld b,c			;0a46	41		A
	scf			;0a47	37		7
	ex af,af'		;0a48	08		.
RST8RDR:
				; = RST8RDR (src: HDRIVER6.ASM:225, BIOS-PP 1273243)
	exx			;0a49	d9		.
	pop bc			;0a4a	c1		.
	out (c),b		;0a4b	ed 41		. A
	exx			;0a4d	d9		.
	pop iy			;0a4e	fd e1		. .
	ex af,af'		;0a50	08		.
	ret			;0a51	c9		.
RDS000:
				; = RDS000 (src: HDRIVER6.ASM:234, BIOS-PP 1273243)
	call SELECTH		;0a52	cd 83 09	. . .
	ret c			;0a55	d8		.
	exx			;0a56	d9		.
	ld de,0c140h		;0a57	11 40 c1	. @ .
	ld bc,04053h		;0a5a	01 53 40	. S @
	call WAITPRT		;0a5d	cd bb 0c	. . .
	exx			;0a60	d9		.
	ret c			;0a61	d8		.
	ex af,af'		;0a62	08		.
	push af			;0a63	f5		.
	push de			;0a64	d5		.
	call PRESET		;0a65	cd 40 0c	. @ .
	pop hl			;0a68	e1		.
	pop af			;0a69	f1		.
	defb 0ddh,02eh,000h ;ld ixl,000h	;0a6a	dd 2e 00	. . .
	defb 0ddh,067h ;ld ixh,a	;0a6d	dd 67		. g
	ld bc,04153h		;0a6f	01 53 41	. S A
	ld a,020h		;0a72	3e 20		>  
	out (c),a		;0a74	ed 79		. y
RDS002:
				; = RDS002 (src: HDRIVER6.ASM:254, BIOS-PP 1273243)
	exx			;0a76	d9		.
	ld de,08908h		;0a77	11 08 89	. . .
	ld bc,04053h		;0a7a	01 53 40	. S @
	call WAITPRT		;0a7d	cd bb 0c	. . .
	exx			;0a80	d9		.
	ret c			;0a81	d8		.
	nop			;0a82	00		.
	in a,(0e2h)		;0a83	db e2		. .
	ex af,af'		;0a85	08		.
	defb 0ddh,07ch ;ld a,ixh	;0a86	dd 7c		. |
	out (0e2h),a		;0a88	d3 e2		. .
	ld bc,YESINT+2		;0a8a	01 50 00	. P .
	ld d,020h		;0a8d	16 20		.  
RDS003:
				; = RDS003 (src: HDRIVER6.ASM:267, BIOS-PP 1273243)
	ini			;0a8f	ed a2		. .
	ini			;0a91	ed a2		. .
	ini			;0a93	ed a2		. .
	ini			;0a95	ed a2		. .
	ini			;0a97	ed a2		. .
	ini			;0a99	ed a2		. .
	ini			;0a9b	ed a2		. .
	ini			;0a9d	ed a2		. .
	ini			;0a9f	ed a2		. .
	ini			;0aa1	ed a2		. .
	ini			;0aa3	ed a2		. .
	ini			;0aa5	ed a2		. .
	ini			;0aa7	ed a2		. .
	ini			;0aa9	ed a2		. .
	ini			;0aab	ed a2		. .
	ini			;0aad	ed a2		. .
	dec d			;0aaf	15		.
	jr nz,RDS003		;0ab0	20 dd		  .
	ex af,af'		;0ab2	08		.
	out (0e2h),a		;0ab3	d3 e2		. .
	ei			;0ab5	fb		.
	ld a,h			;0ab6	7c		|
	or l			;0ab7	b5		.
	jr nz,W44		;0ab8	20 14		  .
	ld hl,0c000h		;0aba	21 00 c0	! . .
	in a,(0e2h)		;0abd	db e2		. .
	ex af,af'		;0abf	08		.
	ld a,0feh		;0ac0	3e fe		> .
	out (0e2h),a		;0ac2	d3 e2		. .
	ld d,0c2h		;0ac4	16 c2		. .
	defb 0ddh,05ch ;ld e,ixh	;0ac6	dd 5c		. \
	ld a,(de)		;0ac8	1a		.
	defb 0ddh,067h ;ld ixh,a	;0ac9	dd 67		. g
	ex af,af'		;0acb	08		.
	out (0e2h),a		;0acc	d3 e2		. .
W44:
	defb 0ddh,02ch ;inc ixl	;0ace	dd 2c		. ,
	exx			;0ad0	d9		.
	ld de,0c140h		;0ad1	11 40 c1	. @ .
	ld bc,04053h		;0ad4	01 53 40	. S @
	call WAITPRT		;0ad7	cd bb 0c	. . .
	exx			;0ada	d9		.
	ret c			;0adb	d8		.
	ld bc,04053h		;0adc	01 53 40	. S @
	in a,(c)		;0adf	ed 78		. x
	bit 3,a			;0ae1	cb 5f		. _
	jr nz,RDS002		;0ae3	20 91		  .
	xor a			;0ae5	af		.
	ret			;0ae6	c9		.
WRITEH:
				; = WRITEH (src: HDRIVER6.ASM:330, BIOS-PP 1273243)
				; = HDD_5x.WRITE (src: HDD_DRIVER_6.asm:359, BIOS-TT 0271ac3)
	ex af,af'		;0ae7	08		.
	in a,(0e2h)		;0ae8	db e2		. .
	ex af,af'		;0aea	08		.
LWRITEH:
				; = LWRITEH (src: HDRIVER6.ASM:345, BIOS-PP 1273243)
				; = HDD_5x.LONG_WRITE (src: HDD_DRIVER_6.asm:375, BIOS-TT 0271ac3)
	push iy			;0aeb	fd e5		. .
	exx			;0aed	d9		.
	ld c,089h		;0aee	0e 89		. .
	in b,(c)		;0af0	ed 40		. @
	push bc			;0af2	c5		.
	ld e,0c0h		;0af3	1e c0		. .
	out (c),e		;0af5	ed 59		. Y
	exx			;0af7	d9		.
	push ix			;0af8	dd e5		. .
	push hl			;0afa	e5		.
	push bc			;0afb	c5		.
	call WRS000		;0afc	cd 43 0b	. C .
	ex de,hl		;0aff	eb		.
	jp c,HERRWR0		;0b00	da 21 0b	. ! .
	defb 0ddh,07ch ;ld a,ixh	;0b03	dd 7c		. |
	ex af,af'		;0b05	08		.
	pop hl			;0b06	e1		.
	pop ix			;0b07	dd e1		. .
	pop bc			;0b09	c1		.
	xor a			;0b0a	af		.
	cp b			;0b0b	b8		.
	ld c,b			;0b0c	48		H
	ld b,a			;0b0d	47		G
	jr nz,WNOT256		;0b0e	20 09		  .
	inc b			;0b10	04		.
	add ix,bc		;0b11	dd 09		. .
	ld b,c			;0b13	41		A
	adc hl,bc		;0b14	ed 4a		. J
	ex af,af'		;0b16	08		.
	jr RST8WRR		;0b17	18 21		. !
WNOT256:
				; = WNOT256 (src: HDRIVER6.ASM:377, BIOS-PP 1273243)
	add ix,bc		;0b19	dd 09		. .
	ld c,b			;0b1b	48		H
	adc hl,bc		;0b1c	ed 4a		. J
	ex af,af'		;0b1e	08		.
	jr RST8WRR		;0b1f	18 19		. .
HERRWR0:
				; = HERRWR0 (src: HDRIVER6.ASM:383, BIOS-PP 1273243)
	ld b,a			;0b21	47		G
	defb 0ddh,04dh ;ld c,ixl	;0b22	dd 4d		. M
	defb 0ddh,07ch ;ld a,ixh	;0b24	dd 7c		. |
	ex af,af'		;0b26	08		.
	pop hl			;0b27	e1		.
	pop ix			;0b28	dd e1		. .
	push bc			;0b2a	c5		.
	ld b,000h		;0b2b	06 00		. .
	add ix,bc		;0b2d	dd 09		. .
	ld c,b			;0b2f	48		H
	adc hl,bc		;0b30	ed 4a		. J
	pop bc			;0b32	c1		.
	pop af			;0b33	f1		.
	sub c			;0b34	91		.
	ld c,a			;0b35	4f		O
	ld a,b			;0b36	78		x
	ld b,c			;0b37	41		A
	scf			;0b38	37		7
	ex af,af'		;0b39	08		.
RST8WRR:
				; = RST8WRR (src: HDRIVER6.ASM:400, BIOS-PP 1273243)
	exx			;0b3a	d9		.
	pop bc			;0b3b	c1		.
	out (c),b		;0b3c	ed 41		. A
	exx			;0b3e	d9		.
	pop iy			;0b3f	fd e1		. .
	ex af,af'		;0b41	08		.
	ret			;0b42	c9		.
WRS000:
				; = WRS000 (src: HDRIVER6.ASM:409, BIOS-PP 1273243)
	call SELECTH		;0b43	cd 83 09	. . .
	ret c			;0b46	d8		.
	exx			;0b47	d9		.
	ld de,0c140h		;0b48	11 40 c1	. @ .
	ld bc,04053h		;0b4b	01 53 40	. S @
	call WAITPRT		;0b4e	cd bb 0c	. . .
	exx			;0b51	d9		.
	ret c			;0b52	d8		.
	ex af,af'		;0b53	08		.
	push af			;0b54	f5		.
	push de			;0b55	d5		.
	push bc			;0b56	c5		.
	ld d,01dh		;0b57	16 1d		. .
	ld c,0f6h		;0b59	0e f6		. .
	call ToPage8Call	;0b5b	cd d0 3f	. . ?
	pop bc			;0b5e	c1		.
	and 001h		;0b5f	e6 01		. .
	jr z,NOWP		;0b61	28 0a		( .
	pop hl			;0b63	e1		.
	pop af			;0b64	f1		.
	ex af,af'		;0b65	08		.
	defb 0ddh,02eh,000h ;ld ixl,000h	;0b66	dd 2e 00	. . .
	ld a,008h		;0b69	3e 08		> .
	scf			;0b6b	37		7
	ret			;0b6c	c9		.
NOWP:
				; = NOWP (src: HDRIVER6.ASM:434, BIOS-PP 1273243)
	call PRESET		;0b6d	cd 40 0c	. @ .
	pop hl			;0b70	e1		.
	pop af			;0b71	f1		.
	defb 0ddh,02eh,000h ;ld ixl,000h	;0b72	dd 2e 00	. . .
	defb 0ddh,067h ;ld ixh,a	;0b75	dd 67		. g
	ld bc,04153h		;0b77	01 53 41	. S A
	ld a,030h		;0b7a	3e 30		> 0
	out (c),a		;0b7c	ed 79		. y
WRS002:
				; = WRS002 (src: HDRIVER6.ASM:443, BIOS-PP 1273243)
	exx			;0b7e	d9		.
	ld de,08908h		;0b7f	11 08 89	. . .
	ld bc,04053h		;0b82	01 53 40	. S @
	call WAITPRT		;0b85	cd bb 0c	. . .
	exx			;0b88	d9		.
	ret c			;0b89	d8		.
	di			;0b8a	f3		.
	in a,(0e2h)		;0b8b	db e2		. .
	ex af,af'		;0b8d	08		.
	defb 0ddh,07ch ;ld a,ixh	;0b8e	dd 7c		. |
	out (0e2h),a		;0b90	d3 e2		. .
	ld bc,l0150h		;0b92	01 50 01	. P .
	ld d,020h		;0b95	16 20		.  
WRS003:
				; = WRS003 (src: HDRIVER6.ASM:456, BIOS-PP 1273243)
	outi			;0b97	ed a3		. .
	outi			;0b99	ed a3		. .
	outi			;0b9b	ed a3		. .
	outi			;0b9d	ed a3		. .
	outi			;0b9f	ed a3		. .
	outi			;0ba1	ed a3		. .
	outi			;0ba3	ed a3		. .
	outi			;0ba5	ed a3		. .
	outi			;0ba7	ed a3		. .
	outi			;0ba9	ed a3		. .
	outi			;0bab	ed a3		. .
	outi			;0bad	ed a3		. .
	outi			;0baf	ed a3		. .
	outi			;0bb1	ed a3		. .
	outi			;0bb3	ed a3		. .
	outi			;0bb5	ed a3		. .
	dec d			;0bb7	15		.
	jr nz,WRS003		;0bb8	20 dd		  .
	ex af,af'		;0bba	08		.
	out (0e2h),a		;0bbb	d3 e2		. .
	ei			;0bbd	fb		.
	ld a,h			;0bbe	7c		|
	or l			;0bbf	b5		.
	jr nz,W33		;0bc0	20 14		  .
	ld hl,0c000h		;0bc2	21 00 c0	! . .
	in a,(0e2h)		;0bc5	db e2		. .
	ex af,af'		;0bc7	08		.
	ld a,0feh		;0bc8	3e fe		> .
	out (0e2h),a		;0bca	d3 e2		. .
	ld d,0c2h		;0bcc	16 c2		. .
	defb 0ddh,05ch ;ld e,ixh	;0bce	dd 5c		. \
	ld a,(de)		;0bd0	1a		.
	defb 0ddh,067h ;ld ixh,a	;0bd1	dd 67		. g
	ex af,af'		;0bd3	08		.
	out (0e2h),a		;0bd4	d3 e2		. .
W33:
	defb 0ddh,02ch ;inc ixl	;0bd6	dd 2c		. ,
	exx			;0bd8	d9		.
	ld de,0c140h		;0bd9	11 40 c1	. @ .
	ld bc,04053h		;0bdc	01 53 40	. S @
	call WAITPRT		;0bdf	cd bb 0c	. . .
	exx			;0be2	d9		.
	ret c			;0be3	d8		.
	ld bc,04053h		;0be4	01 53 40	. S @
	in a,(c)		;0be7	ed 78		. x
	bit 3,a			;0be9	cb 5f		. _
	jr nz,WRS002		;0beb	20 91		  .
	xor a			;0bed	af		.
	ret			;0bee	c9		.
VERIFYH:
				; = VERIFYH (src: HDRIVER6.ASM:516, BIOS-PP 1273243)
				; = HDD_5x.VERIFY (src: HDD_DRIVER_6.asm:532, BIOS-TT 0271ac3)
	push iy			;0bef	fd e5		. .
	exx			;0bf1	d9		.
	ld c,089h		;0bf2	0e 89		. .
	in b,(c)		;0bf4	ed 40		. @
	push bc			;0bf6	c5		.
	ld e,0c0h		;0bf7	1e c0		. .
	out (c),e		;0bf9	ed 59		. Y
	exx			;0bfb	d9		.
	push ix			;0bfc	dd e5		. .
	push hl			;0bfe	e5		.
	call VRS000		;0bff	cd 0d 0c	. . .
	pop hl			;0c02	e1		.
	pop ix			;0c03	dd e1		. .
	exx			;0c05	d9		.
	pop bc			;0c06	c1		.
	out (c),b		;0c07	ed 41		. A
	exx			;0c09	d9		.
	pop iy			;0c0a	fd e1		. .
	ret			;0c0c	c9		.
VRS000:
				; = VRS000 (src: HDRIVER6.ASM:538, BIOS-PP 1273243)
	call SELECTH		;0c0d	cd 83 09	. . .
	ret c			;0c10	d8		.
	exx			;0c11	d9		.
	ld de,0c140h		;0c12	11 40 c1	. @ .
	ld bc,04053h		;0c15	01 53 40	. S @
	call WAITPRT		;0c18	cd bb 0c	. . .
	exx			;0c1b	d9		.
	ret c			;0c1c	d8		.
	push de			;0c1d	d5		.
	call PRESET		;0c1e	cd 40 0c	. @ .
	pop hl			;0c21	e1		.
	ld bc,04153h		;0c22	01 53 41	. S A
	ld a,040h		;0c25	3e 40		> @
	out (c),a		;0c27	ed 79		. y
VRS002:
				; = VRS002 (src: HDRIVER6.ASM:552, BIOS-PP 1273243)
	ld bc,04053h		;0c29	01 53 40	. S @
	in a,(c)		;0c2c	ed 78		. x
	bit 0,a			;0c2e	cb 47		. G
	jr z,VRS003		;0c30	28 02		( .
	scf			;0c32	37		7
	ret			;0c33	c9		.
VRS003:
				; = VRS003 (src: HDRIVER6.ASM:558, BIOS-PP 1273243)
	ld de,0c140h		;0c34	11 40 c1	. @ .
	ld bc,04053h		;0c37	01 53 40	. S @
	call WAITPRT		;0c3a	cd bb 0c	. . .
	ret c			;0c3d	d8		.
	xor a			;0c3e	af		.
	ret			;0c3f	c9		.
PRESET:
				; = PRESET (src: HDRIVER6.ASM:568, BIOS-PP 1273243)
	ld a,b			;0c40	78		x
	ld bc,L0152		;0c41	01 52 01	. R .
	out (c),a		;0c44	ed 79		. y
	in a,(0e2h)		;0c46	db e2		. .
	ex af,af'		;0c48	08		.
	ld a,0feh		;0c49	3e fe		> .
	out (0e2h),a		;0c4b	d3 e2		. .
	ld a,(iy+000h)		;0c4d	fd 7e 00	. ~ .
	ld bc,04152h		;0c50	01 52 41	. R A
	out (c),a		;0c53	ed 79		. y
	bit 6,a			;0c55	cb 77		. w
	defb 0ddh,05dh ;ld e,ixl	;0c57	dd 5d		. ]
	defb 0ddh,054h ;ld d,ixh	;0c59	dd 54		. T
	call z,LBA_CHS		;0c5b	cc 7e 0c	. ~ .
	ld bc,l0153h		;0c5e	01 53 01	. S .
	out (c),e		;0c61	ed 59		. Y
	ld bc,l0153h+1		;0c63	01 54 01	. T .
	out (c),d		;0c66	ed 51		. Q
	ld bc,l0153h+2		;0c68	01 55 01	. U .
	out (c),l		;0c6b	ed 69		. i
	ld bc,04152h		;0c6d	01 52 41	. R A
	dec b			;0c70	05		.
	in a,(c)		;0c71	ed 78		. x
	and 0f0h		;0c73	e6 f0		. .
	or h			;0c75	b4		.
	inc b			;0c76	04		.
	out (c),a		;0c77	ed 79		. y
	ex af,af'		;0c79	08		.
	out (0e2h),a		;0c7a	d3 e2		. .
	and a			;0c7c	a7		.
	ret			;0c7d	c9		.
LBA_CHS:
				; = LBA_CHS (src: HDRIVER6.ASM:602, BIOS-PP 1273243)
	ld c,(iy+005h)		;0c7e	fd 4e 05	. N .
	ld b,(iy+006h)		;0c81	fd 46 06	. F .
DIV32X:
	defb 0ddh,062h ;ld ixh,d	;0c84	dd 62		. b
	defb 0ddh,06bh ;ld ixl,e	;0c86	dd 6b		. k
	ex de,hl		;0c88	eb		.
	ld hl,RomStart		;0c89	21 00 00	! . .
	ld a,020h		;0c8c	3e 20		>  
DIV011:
				; = DIV011 (src: HDRIVER6.ASM:611, BIOS-PP 1273243)
	add ix,ix		;0c8e	dd 29		. )
	ex de,hl		;0c90	eb		.
	adc hl,hl		;0c91	ed 6a		. j
	ex de,hl		;0c93	eb		.
	adc hl,hl		;0c94	ed 6a		. j
	sbc hl,bc		;0c96	ed 42		. B
	jr nc,DIV012		;0c98	30 06		0 .
	add hl,bc		;0c9a	09		.
	dec a			;0c9b	3d		=
	jr nz,DIV011		;0c9c	20 f0		  .
	jr DIV014		;0c9e	18 05		. .
DIV012:
				; = DIV012 (src: HDRIVER6.ASM:622, BIOS-PP 1273243)
	inc ix			;0ca0	dd 23		. #
	dec a			;0ca2	3d		=
	jr nz,DIV011		;0ca3	20 e9		  .
DIV014:
				; = DIV014 (src: HDRIVER6.ASM:625, BIOS-PP 1273243)
	ld e,(iy+001h)		;0ca5	fd 5e 01	. ^ .
	ld d,000h		;0ca8	16 00		. .
	xor a			;0caa	af		.
CHS005:
				; = CHS005 (src: HDRIVER6.ASM:628, BIOS-PP 1273243)
	inc a			;0cab	3c		<
	sbc hl,de		;0cac	ed 52		. R
	jr nc,CHS005		;0cae	30 fb		0 .
	add hl,de		;0cb0	19		.
	dec a			;0cb1	3d		=
	ld h,a			;0cb2	67		g
	ld e,l			;0cb3	5d		]
	inc e			;0cb4	1c		.
	defb 0ddh,055h ;ld d,ixl	;0cb5	dd 55		. U
	defb 0ddh,07ch ;ld a,ixh	;0cb7	dd 7c		. |
	ld l,a			;0cb9	6f		o
	ret			;0cba	c9		.
WAITPRT:
				; = WAITPRT (src: HDRIVER6.ASM:645, BIOS-PP 1273243)
	ld hl,RomStart		;0cbb	21 00 00	! . .
WAITP0:
				; = WAITP0 (src: HDRIVER6.ASM:647, BIOS-PP 1273243)
				; = WAITPRT.P0 (src: HDD_DRIVER_6.asm:658, BIOS-TT 0271ac3)
	in a,(c)		;0cbe	ed 78		. x
	and d			;0cc0	a2		.
	cp e			;0cc1	bb		.
	jr nz,WAITP2		;0cc2	20 02		  .
	and a			;0cc4	a7		.
	ret			;0cc5	c9		.
WAITP2:
				; = WAITP2 (src: HDRIVER6.ASM:656, BIOS-PP 1273243)
				; = WAITPRT.P2 (src: HDD_DRIVER_6.asm:664, BIOS-TT 0271ac3)
	dec hl			;0cc6	2b		+
	ld a,l			;0cc7	7d		}
	or h			;0cc8	b4		.
	jp nz,WAITP0		;0cc9	c2 be 0c	. . .
WAITP1:
				; = WAITP1 (src: HDRIVER6.ASM:660, BIOS-PP 1273243)
				; = WAITPRT.error (src: HDD_DRIVER_6.asm:668, BIOS-TT 0271ac3)
	ld a,004h		;0ccc	3e 04		> .
	scf			;0cce	37		7
	ret			;0ccf	c9		.
CD_5x_RESET:
				; = CD_5x.RESET (src: CD_DRIVER_0.asm:36, BIOS-TT 0271ac3)
	ld b,032h		;0cd0	06 32		. 2
CD_5x_RESET_loop:
				; = CD_5x.RESET.loop (src: CD_DRIVER_0.asm:38, BIOS-TT 0271ac3)
	push bc			;0cd2	c5		.
	ld a,001h		;0cd3	3e 01		> .
	call CD_OPEN_0CEE	;0cd5	cd ee 0c	. . .
	pop bc			;0cd8	c1		.
	ret nc			;0cd9	d0		.
	ei			;0cda	fb		.
	halt			;0cdb	76		v

; BLOCK 'Data0CDC' (start 0x0cdc end 0x0cdf)
Data0CDC:
	defb 010h		;0cdc	10		.
	defb 0f4h		;0cdd	f4		.
	defb 0c9h		;0cde	c9		.
CD_5x_LONG_READ:
				; = CD_5x.LONG_READ (src: CD_DRIVER_0.asm:52, BIOS-TT 0271ac3)
				; = CD_5x.READ (src: CD_DRIVER_0.asm:55, BIOS-TT 0271ac3)
	ld a,001h		;0cdf	3e 01		> .
	jp CD_READ		;0ce1	c3 0c 0d	. . .
CD_OPEN:
				; = CD.OPEN (src: CD_DRIVER_0.asm:62, BIOS-TT 0271ac3)
	ld a,001h		;0ce4	3e 01		> .
	jp CD_CLOSE		;0ce6	c3 f8 0c	. . .

; BLOCK 'Data0CE9' (start 0x0ce9 end 0x0cee)
Data0CE9:
	defb 03eh		;0ce9	3e		>
	defb 001h		;0cea	01		.
	defb 0c3h		;0ceb	c3		.
	defb 002h		;0cec	02		.
	defb 00dh		;0ced	0d		.
CD_OPEN_0CEE:
				; = CD_OPEN (src: CD_DRIVER_0.asm:90, BIOS-TT 0271ac3)
	ld hl,CMDNOPP		;0cee	21 8c 0e	! . .
	ld de,RomStart		;0cf1	11 00 00	. . .
	call AP_COM		;0cf4	cd 44 0d	. D .
	ret			;0cf7	c9		.
CD_CLOSE:
				; = CD_CLOSE (src: CD_DRIVER_0.asm:97, BIOS-TT 0271ac3)
	ld hl,CMDOPEN		;0cf8	21 98 0e	! . .
	ld de,RomStart		;0cfb	11 00 00	. . .
	call AP_COM		;0cfe	cd 44 0d	. D .
	ret			;0d01	c9		.

; BLOCK 'Data0D02' (start 0x0d02 end 0x0d0c)
Data0D02:
	defb 021h		;0d02	21		!
	defb 0a4h		;0d03	a4		.
	defb 00eh		;0d04	0e		.
	defb 011h		;0d05	11		.
	defb 000h		;0d06	00		.
	defb 000h		;0d07	00		.
	defb 0cdh		;0d08	cd		.
	defb 044h		;0d09	44		D
	defb 00dh		;0d0a	0d		.
	defb 0c9h		;0d0b	c9		.
CD_READ:
				; = CD_READ (src: CD_DRIVER_0.asm:108, BIOS-TT 0271ac3)
	ld c,a			;0d0c	4f		O
	exx			;0d0d	d9		.
	ld c,0e2h		;0d0e	0e e2		. .
	in a,(c)		;0d10	ed 78		. x
	push af			;0d12	f5		.
	ld a,0feh		;0d13	3e fe		> .
	out (c),a		;0d15	ed 79		. y
	ld hl,CMDREAD		;0d17	21 b0 0e	! . .
	ld de,0fef0h		;0d1a	11 f0 fe	. . .
	ld bc,l000ch		;0d1d	01 0c 00	. . .
	ldir			;0d20	ed b0		. .
	exx			;0d22	d9		.
	ld a,h			;0d23	7c		|
	ld (0fef2h),a		;0d24	32 f2 fe	2 . .
	ld a,l			;0d27	7d		}
	ld (0fef3h),a		;0d28	32 f3 fe	2 . .
	defb 0ddh,07ch ;ld a,ixh	;0d2b	dd 7c		. |
	ld (0fef4h),a		;0d2d	32 f4 fe	2 . .
	defb 0ddh,07dh ;ld a,ixl	;0d30	dd 7d		. }
	ld (0fef5h),a		;0d32	32 f5 fe	2 . .
	ld a,b			;0d35	78		x
	ld (0fef8h),a		;0d36	32 f8 fe	2 . .
	pop af			;0d39	f1		.
	out (0e2h),a		;0d3a	d3 e2		. .
	ld hl,0fef0h		;0d3c	21 f0 fe	! . .
	ld a,c			;0d3f	79		y
	call AP_COM		;0d40	cd 44 0d	. D .
	ret			;0d43	c9		.
AP_COM:
				; = AP_COM (src: CD_DRIVER_0.asm:151, BIOS-TT 0271ac3)
	and 001h		;0d44	e6 01		. .
	ld a,0a0h		;0d46	3e a0		> .
	jr z,AP_COM_APCOM1	;0d48	28 02		( .
	ld a,0b0h		;0d4a	3e b0		> .
AP_COM_APCOM1:
				; = AP_COM.APCOM1 (src: CD_DRIVER_0.asm:155, BIOS-TT 0271ac3)
	ld bc,04152h		;0d4c	01 52 41	. R A
	out (c),a		;0d4f	ed 79		. y
	exx			;0d51	d9		.
	ld de,08000h		;0d52	11 00 80	. . .
	ld bc,04053h		;0d55	01 53 40	. S @
	call CWAITPRT	;0d58	cd 6d 0e	. m .
	exx			;0d5b	d9		.
	jr nc,AP_COM_CDREADY	;0d5c	30 17		0 .
	ld bc,04153h		;0d5e	01 53 41	. S A
	ld a,008h		;0d61	3e 08		> .
	out (c),a		;0d63	ed 79		. y
	ld b,000h		;0d65	06 00		. .
l0d67h:
	djnz l0d67h		;0d67	10 fe		. .
	exx			;0d69	d9		.
	ld de,08000h		;0d6a	11 00 80	. . .
	ld bc,04053h		;0d6d	01 53 40	. S @
	call CWAITPRT	;0d70	cd 6d 0e	. m .
	exx			;0d73	d9		.
	ret c			;0d74	d8		.
AP_COM_CDREADY:
				; = AP_COM.CDREADY (src: CD_DRIVER_0.asm:174, BIOS-TT 0271ac3)
	ld c,0e2h		;0d75	0e e2		. .
	in b,(c)		;0d77	ed 40		. @
	push de			;0d79	d5		.
	push bc			;0d7a	c5		.
	ld a,0feh		;0d7b	3e fe		> .
	out (c),a		;0d7d	ed 79		. y
	ld a,b			;0d7f	78		x
	ld de,0fee0h		;0d80	11 e0 fe	. . .
	ld bc,l000ch		;0d83	01 0c 00	. . .
	ldir			;0d86	ed b0		. .
	pop bc			;0d88	c1		.
	pop de			;0d89	d1		.
	out (c),b		;0d8a	ed 41		. A
	xor a			;0d8c	af		.
	exx			;0d8d	d9		.
	out (c),a		;0d8e	ed 79		. y
	xor a			;0d90	af		.
	ld bc,l0151h		;0d91	01 51 01	. Q .
	out (c),a		;0d94	ed 79		. y
	ld de,00800h		;0d96	11 00 08	. . .
	ld bc,l0153h+1		;0d99	01 54 01	. T .
	out (c),e		;0d9c	ed 59		. Y
	ld bc,l0153h+2		;0d9e	01 55 01	. U .
	out (c),d		;0da1	ed 51		. Q
	ld bc,04153h		;0da3	01 53 41	. S A
	ld a,0a0h		;0da6	3e a0		> .
	out (c),a		;0da8	ed 79		. y
	ld de,08000h		;0daa	11 00 80	. . .
	ld bc,04053h		;0dad	01 53 40	. S @
	call CWAITPRT	;0db0	cd 6d 0e	. m .
	exx			;0db3	d9		.
	ret c			;0db4	d8		.
	exx			;0db5	d9		.
	ld de,l0908h		;0db6	11 08 09	. . .
	ld bc,04053h		;0db9	01 53 40	. S @
	call CWAITPRT	;0dbc	cd 6d 0e	. m .
	exx			;0dbf	d9		.
	bit 0,a			;0dc0	cb 47		. G
	jr nz,AP_COM_CDERROR	;0dc2	20 3b		  ;
	jr nc,AP_COM_YEP_DRQ	;0dc4	30 03		0 .
	ld a,080h		;0dc6	3e 80		> .
	ret			;0dc8	c9		.
AP_COM_YEP_DRQ:
				; = AP_COM.YEP_DRQ (src: CD_DRIVER_0.asm:221, BIOS-TT 0271ac3)
	ld c,0e2h		;0dc9	0e e2		. .
	in b,(c)		;0dcb	ed 40		. @
	push bc			;0dcd	c5		.
	ld a,0feh		;0dce	3e fe		> .
	out (0e2h),a		;0dd0	d3 e2		. .
	ld hl,0fee0h		;0dd2	21 e0 fe	! . .
	ld bc,l0150h		;0dd5	01 50 01	. P .
	ld a,00ch		;0dd8	3e 0c		> .
	srl a			;0dda	cb 3f		. ?
AP_COM_OUTPKT:
				; = AP_COM.OUTPKT (src: CD_DRIVER_0.asm:231, BIOS-TT 0271ac3)
	outi			;0ddc	ed a3		. .
	outi			;0dde	ed a3		. .
	dec a			;0de0	3d		=
	jr nz,AP_COM_OUTPKT	;0de1	20 f9		  .
	pop bc			;0de3	c1		.
	out (c),b		;0de4	ed 41		. A
	ld b,080h		;0de6	06 80		. .
l0de8h:
	djnz l0de8h		;0de8	10 fe		. .
AP_COM_AP_LOOP:
				; = AP_COM.AP_LOOP (src: CD_DRIVER_0.asm:240, BIOS-TT 0271ac3)
	exx			;0dea	d9		.
	ld de,08000h		;0deb	11 00 80	. . .
	ld bc,04053h		;0dee	01 53 40	. S @
	call CWAITPRT	;0df1	cd 6d 0e	. m .
	exx			;0df4	d9		.
	ret c			;0df5	d8		.
	ld bc,04053h		;0df6	01 53 40	. S @
	in a,(c)		;0df9	ed 78		. x
	bit 0,a			;0dfb	cb 47		. G
	jr z,AP_COM_NO_ERR	;0dfd	28 0d		( .
AP_COM_CDERROR:
				; = AP_COM.CDERROR (src: CD_DRIVER_0.asm:251, BIOS-TT 0271ac3)
	ld bc,l0051h		;0dff	01 51 00	. Q .
	in a,(c)		;0e02	ed 78		. x
	rrca			;0e04	0f		.
	rrca			;0e05	0f		.
	rrca			;0e06	0f		.
	rrca			;0e07	0f		.
	and 00fh		;0e08	e6 0f		. .
	scf			;0e0a	37		7
	ret			;0e0b	c9		.
AP_COM_NO_ERR:
				; = AP_COM.NO_ERR (src: CD_DRIVER_0.asm:261, BIOS-TT 0271ac3)
	bit 3,a			;0e0c	cb 5f		. _
	ld a,000h		;0e0e	3e 00		> .
	ret z			;0e10	c8		.
	ex de,hl		;0e11	eb		.
	ld bc,00054h		;0e12	01 54 00	. T .
	in e,(c)		;0e15	ed 58		. X
	ld bc,00055h		;0e17	01 55 00	. U .
	in d,(c)		;0e1a	ed 50		. P
	ld a,d			;0e1c	7a		z
	or e			;0e1d	b3		.
	ret z			;0e1e	c8		.
	ld bc,l0052h		;0e1f	01 52 00	. R .
	in a,(c)		;0e22	ed 78		. x
	and 002h		;0e24	e6 02		. .
	cp 002h			;0e26	fe 02		. .
	jp z,AP_COM_FROM_CD	;0e28	ca 3b 0e	. ; .
	ld bc,YESINT+2		;0e2b	01 50 00	. P .
AP_COM_WR_T_CD:
				; = AP_COM.WR_T_CD (src: CD_DRIVER_0.asm:280, BIOS-TT 0271ac3)
	outi			;0e2e	ed a3		. .
	outi			;0e30	ed a3		. .
	dec de			;0e32	1b		.
	dec de			;0e33	1b		.
	ld a,d			;0e34	7a		z
	or e			;0e35	b3		.
	jr nz,AP_COM_WR_T_CD	;0e36	20 f6		  .
	ex de,hl		;0e38	eb		.
	jr AP_COM_AP_LOOP	;0e39	18 af		. .
AP_COM_FROM_CD:
				; = AP_COM.FROM_CD (src: CD_DRIVER_0.asm:290, BIOS-TT 0271ac3)
	ld a,h			;0e3b	7c		|
	or l			;0e3c	b5		.
	jr z,AP_COM_NULL	;0e3d	28 10		( .
	ld bc,YESINT+2		;0e3f	01 50 00	. P .
AP_COM_RD_F_CD:
				; = AP_COM.RD_F_CD (src: CD_DRIVER_0.asm:295, BIOS-TT 0271ac3)
	ini			;0e42	ed a2		. .
	ini			;0e44	ed a2		. .
	dec de			;0e46	1b		.
	dec de			;0e47	1b		.
	ld a,d			;0e48	7a		z
	or e			;0e49	b3		.
	jr nz,AP_COM_RD_F_CD	;0e4a	20 f6		  .
	ex de,hl		;0e4c	eb		.
	jr AP_COM_AP_LOOP	;0e4d	18 9b		. .
AP_COM_NULL:
				; = AP_COM.NULL (src: CD_DRIVER_0.asm:305, BIOS-TT 0271ac3)
	ld bc,YESINT+2		;0e4f	01 50 00	. P .
AP_COM_RD_N_CD:
				; = AP_COM.RD_N_CD (src: CD_DRIVER_0.asm:306, BIOS-TT 0271ac3)
	in a,(c)		;0e52	ed 78		. x
	dec b			;0e54	05		.
	in a,(c)		;0e55	ed 78		. x
	dec b			;0e57	05		.
	dec de			;0e58	1b		.
	dec de			;0e59	1b		.
	ld a,d			;0e5a	7a		z
	or e			;0e5b	b3		.
	jr nz,AP_COM_RD_N_CD	;0e5c	20 f4		  .
	jr AP_COM_AP_LOOP	;0e5e	18 8a		. .

; BLOCK 'Data0E60' (start 0x0e60 end 0x0e6d)
Data0E60:
	defb 021h		;0e60	21		!
	defb 000h		;0e61	00		.
	defb 000h		;0e62	00		.
	defb 02dh		;0e63	2d		-
	defb 020h		;0e64	20		 
	defb 0fdh		;0e65	fd		.
	defb 025h		;0e66	25		%
	defb 020h		;0e67	20		 
	defb 0fah		;0e68	fa		.
	defb 01dh		;0e69	1d		.
	defb 020h		;0e6a	20		 
	defb 0f7h		;0e6b	f7		.
	defb 0c9h		;0e6c	c9		.
CWAITPRT:
				; = CWAITPRT (src: CD_DRIVER_0.asm:335, BIOS-TT 0271ac3)
	ld a,064h		;0e6d	3e 64		> d
	ld hl,RomStart		;0e6f	21 00 00	! . .
CWAITPRT_CWAITPX:
				; = CWAITPRT.CWAITPX (src: CD_DRIVER_0.asm:338, BIOS-TT 0271ac3)
	ex af,af'		;0e72	08		.
CWAITPRT_CWAITP0:
				; = CWAITPRT.CWAITP0 (src: CD_DRIVER_0.asm:340, BIOS-TT 0271ac3)
	in a,(c)		;0e73	ed 78		. x
	cp 0ffh			;0e75	fe ff		. .
	jr z,CWAITPRT_CWAITP1	;0e77	28 11		( .
	and d			;0e79	a2		.
	cp e			;0e7a	bb		.
	jr nz,CWAITPRT_CWAITP2	;0e7b	20 02		  .
	and a			;0e7d	a7		.
	ret			;0e7e	c9		.
CWAITPRT_CWAITP2:
				; = CWAITPRT.CWAITP2 (src: CD_DRIVER_0.asm:349, BIOS-TT 0271ac3)
	dec l			;0e7f	2d		-
	jr nz,CWAITPRT_CWAITP0	;0e80	20 f1		  .
	dec h			;0e82	25		%
	jr nz,CWAITPRT_CWAITP0	;0e83	20 ee		  .
	ex af,af'		;0e85	08		.
	dec a			;0e86	3d		=
	jr nz,CWAITPRT_CWAITPX	;0e87	20 e9		  .
	ex af,af'		;0e89	08		.
CWAITPRT_CWAITP1:
				; = CWAITPRT.CWAITP1 (src: CD_DRIVER_0.asm:358, BIOS-TT 0271ac3)
	scf			;0e8a	37		7
	ret			;0e8b	c9		.
CMDNOPP:

; BLOCK 'Data0E8C' (start 0x0e8c end 0x0ebc)
				; = CMDNOPP (src: CD_DRIVER_0.asm:362, BIOS-TT 0271ac3)
	defb 000h		;0e8c	00		.
	defb 000h		;0e8d	00		.
	defb 000h		;0e8e	00		.
	defb 000h		;0e8f	00		.
	defb 000h		;0e90	00		.
	defb 000h		;0e91	00		.
	defb 000h		;0e92	00		.
	defb 000h		;0e93	00		.
	defb 000h		;0e94	00		.
	defb 000h		;0e95	00		.
	defb 000h		;0e96	00		.
	defb 000h		;0e97	00		.
CMDOPEN:
				; = CMDOPEN (src: CD_DRIVER_0.asm:369, BIOS-TT 0271ac3)
	defb 01bh		;0e98	1b		.
	defb 000h		;0e99	00		.
	defb 000h		;0e9a	00		.
	defb 000h		;0e9b	00		.
	defb 002h		;0e9c	02		.
	defb 000h		;0e9d	00		.
	defb 000h		;0e9e	00		.
	defb 000h		;0e9f	00		.
	defb 000h		;0ea0	00		.
	defb 000h		;0ea1	00		.
	defb 000h		;0ea2	00		.
	defb 000h		;0ea3	00		.
CMDCLOS:
				; = CMDCLOS (src: CD_DRIVER_0.asm:374, BIOS-TT 0271ac3)
	defb 01bh		;0ea4	1b		.
	defb 000h		;0ea5	00		.
	defb 000h		;0ea6	00		.
	defb 000h		;0ea7	00		.
	defb 003h		;0ea8	03		.
	defb 000h		;0ea9	00		.
	defb 000h		;0eaa	00		.
	defb 000h		;0eab	00		.
	defb 000h		;0eac	00		.
	defb 000h		;0ead	00		.
	defb 000h		;0eae	00		.
	defb 000h		;0eaf	00		.
CMDREAD:
				; = CMDREAD (src: CD_DRIVER_0.asm:379, BIOS-TT 0271ac3)
	defb 028h		;0eb0	28		(
	defb 000h		;0eb1	00		.
	defb 000h		;0eb2	00		.
	defb 000h		;0eb3	00		.
	defb 000h		;0eb4	00		.
	defb 000h		;0eb5	00		.
	defb 000h		;0eb6	00		.
	defb 000h		;0eb7	00		.
	defb 001h		;0eb8	01		.
	defb 000h		;0eb9	00		.
	defb 000h		;0eba	00		.
	defb 000h		;0ebb	00		.
l0ebch:

; BLOCK 'Fill0EBC' (start 0x0ebc end 0x1000)
Fill0EBC:
	defb 0ffh		;0ebc	ff		.
	defb 0ffh		;0ebd	ff		.
	defb 0ffh		;0ebe	ff		.
	defb 0ffh		;0ebf	ff		.
	defb 0ffh		;0ec0	ff		.
	defb 0ffh		;0ec1	ff		.
	defb 0ffh		;0ec2	ff		.
	defb 0ffh		;0ec3	ff		.
	defb 0ffh		;0ec4	ff		.
	defb 0ffh		;0ec5	ff		.
	defb 0ffh		;0ec6	ff		.
	defb 0ffh		;0ec7	ff		.
	defb 0ffh		;0ec8	ff		.
	defb 0ffh		;0ec9	ff		.
	defb 0ffh		;0eca	ff		.
	defb 0ffh		;0ecb	ff		.
	defb 0ffh		;0ecc	ff		.
	defb 0ffh		;0ecd	ff		.
	defb 0ffh		;0ece	ff		.
	defb 0ffh		;0ecf	ff		.
	defb 0ffh		;0ed0	ff		.
	defb 0ffh		;0ed1	ff		.
	defb 0ffh		;0ed2	ff		.
	defb 0ffh		;0ed3	ff		.
	defb 0ffh		;0ed4	ff		.
	defb 0ffh		;0ed5	ff		.
	defb 0ffh		;0ed6	ff		.
	defb 0ffh		;0ed7	ff		.
	defb 0ffh		;0ed8	ff		.
	defb 0ffh		;0ed9	ff		.
	defb 0ffh		;0eda	ff		.
	defb 0ffh		;0edb	ff		.
	defb 0ffh		;0edc	ff		.
	defb 0ffh		;0edd	ff		.
	defb 0ffh		;0ede	ff		.
	defb 0ffh		;0edf	ff		.
	defb 0ffh		;0ee0	ff		.
	defb 0ffh		;0ee1	ff		.
	defb 0ffh		;0ee2	ff		.
	defb 0ffh		;0ee3	ff		.
	defb 0ffh		;0ee4	ff		.
	defb 0ffh		;0ee5	ff		.
	defb 0ffh		;0ee6	ff		.
	defb 0ffh		;0ee7	ff		.
	defb 0ffh		;0ee8	ff		.
	defb 0ffh		;0ee9	ff		.
	defb 0ffh		;0eea	ff		.
	defb 0ffh		;0eeb	ff		.
	defb 0ffh		;0eec	ff		.
	defb 0ffh		;0eed	ff		.
	defb 0ffh		;0eee	ff		.
	defb 0ffh		;0eef	ff		.
	defb 0ffh		;0ef0	ff		.
	defb 0ffh		;0ef1	ff		.
	defb 0ffh		;0ef2	ff		.
	defb 0ffh		;0ef3	ff		.
	defb 0ffh		;0ef4	ff		.
	defb 0ffh		;0ef5	ff		.
	defb 0ffh		;0ef6	ff		.
	defb 0ffh		;0ef7	ff		.
	defb 0ffh		;0ef8	ff		.
	defb 0ffh		;0ef9	ff		.
	defb 0ffh		;0efa	ff		.
	defb 0ffh		;0efb	ff		.
	defb 0ffh		;0efc	ff		.
	defb 0ffh		;0efd	ff		.
	defb 0ffh		;0efe	ff		.
	defb 0ffh		;0eff	ff		.
	defb 0ffh		;0f00	ff		.
	defb 0ffh		;0f01	ff		.
	defb 0ffh		;0f02	ff		.
	defb 0ffh		;0f03	ff		.
	defb 0ffh		;0f04	ff		.
	defb 0ffh		;0f05	ff		.
	defb 0ffh		;0f06	ff		.
	defb 0ffh		;0f07	ff		.
	defb 0ffh		;0f08	ff		.
	defb 0ffh		;0f09	ff		.
	defb 0ffh		;0f0a	ff		.
	defb 0ffh		;0f0b	ff		.
	defb 0ffh		;0f0c	ff		.
	defb 0ffh		;0f0d	ff		.
	defb 0ffh		;0f0e	ff		.
	defb 0ffh		;0f0f	ff		.
	defb 0ffh		;0f10	ff		.
	defb 0ffh		;0f11	ff		.
	defb 0ffh		;0f12	ff		.
	defb 0ffh		;0f13	ff		.
	defb 0ffh		;0f14	ff		.
	defb 0ffh		;0f15	ff		.
	defb 0ffh		;0f16	ff		.
	defb 0ffh		;0f17	ff		.
	defb 0ffh		;0f18	ff		.
	defb 0ffh		;0f19	ff		.
	defb 0ffh		;0f1a	ff		.
	defb 0ffh		;0f1b	ff		.
	defb 0ffh		;0f1c	ff		.
	defb 0ffh		;0f1d	ff		.
	defb 0ffh		;0f1e	ff		.
	defb 0ffh		;0f1f	ff		.
	defb 0ffh		;0f20	ff		.
	defb 0ffh		;0f21	ff		.
	defb 0ffh		;0f22	ff		.
	defb 0ffh		;0f23	ff		.
	defb 0ffh		;0f24	ff		.
	defb 0ffh		;0f25	ff		.
	defb 0ffh		;0f26	ff		.
	defb 0ffh		;0f27	ff		.
	defb 0ffh		;0f28	ff		.
	defb 0ffh		;0f29	ff		.
	defb 0ffh		;0f2a	ff		.
	defb 0ffh		;0f2b	ff		.
	defb 0ffh		;0f2c	ff		.
	defb 0ffh		;0f2d	ff		.
	defb 0ffh		;0f2e	ff		.
	defb 0ffh		;0f2f	ff		.
	defb 0ffh		;0f30	ff		.
	defb 0ffh		;0f31	ff		.
	defb 0ffh		;0f32	ff		.
	defb 0ffh		;0f33	ff		.
	defb 0ffh		;0f34	ff		.
	defb 0ffh		;0f35	ff		.
	defb 0ffh		;0f36	ff		.
	defb 0ffh		;0f37	ff		.
	defb 0ffh		;0f38	ff		.
	defb 0ffh		;0f39	ff		.
	defb 0ffh		;0f3a	ff		.
	defb 0ffh		;0f3b	ff		.
	defb 0ffh		;0f3c	ff		.
	defb 0ffh		;0f3d	ff		.
	defb 0ffh		;0f3e	ff		.
	defb 0ffh		;0f3f	ff		.
	defb 0ffh		;0f40	ff		.
	defb 0ffh		;0f41	ff		.
	defb 0ffh		;0f42	ff		.
	defb 0ffh		;0f43	ff		.
	defb 0ffh		;0f44	ff		.
	defb 0ffh		;0f45	ff		.
	defb 0ffh		;0f46	ff		.
	defb 0ffh		;0f47	ff		.
	defb 0ffh		;0f48	ff		.
	defb 0ffh		;0f49	ff		.
	defb 0ffh		;0f4a	ff		.
	defb 0ffh		;0f4b	ff		.
	defb 0ffh		;0f4c	ff		.
	defb 0ffh		;0f4d	ff		.
	defb 0ffh		;0f4e	ff		.
	defb 0ffh		;0f4f	ff		.
	defb 0ffh		;0f50	ff		.
	defb 0ffh		;0f51	ff		.
	defb 0ffh		;0f52	ff		.
	defb 0ffh		;0f53	ff		.
	defb 0ffh		;0f54	ff		.
	defb 0ffh		;0f55	ff		.
	defb 0ffh		;0f56	ff		.
	defb 0ffh		;0f57	ff		.
	defb 0ffh		;0f58	ff		.
	defb 0ffh		;0f59	ff		.
	defb 0ffh		;0f5a	ff		.
	defb 0ffh		;0f5b	ff		.
	defb 0ffh		;0f5c	ff		.
	defb 0ffh		;0f5d	ff		.
	defb 0ffh		;0f5e	ff		.
	defb 0ffh		;0f5f	ff		.
	defb 0ffh		;0f60	ff		.
	defb 0ffh		;0f61	ff		.
	defb 0ffh		;0f62	ff		.
	defb 0ffh		;0f63	ff		.
	defb 0ffh		;0f64	ff		.
	defb 0ffh		;0f65	ff		.
	defb 0ffh		;0f66	ff		.
	defb 0ffh		;0f67	ff		.
	defb 0ffh		;0f68	ff		.
	defb 0ffh		;0f69	ff		.
	defb 0ffh		;0f6a	ff		.
	defb 0ffh		;0f6b	ff		.
	defb 0ffh		;0f6c	ff		.
	defb 0ffh		;0f6d	ff		.
	defb 0ffh		;0f6e	ff		.
	defb 0ffh		;0f6f	ff		.
	defb 0ffh		;0f70	ff		.
	defb 0ffh		;0f71	ff		.
	defb 0ffh		;0f72	ff		.
	defb 0ffh		;0f73	ff		.
	defb 0ffh		;0f74	ff		.
	defb 0ffh		;0f75	ff		.
	defb 0ffh		;0f76	ff		.
	defb 0ffh		;0f77	ff		.
	defb 0ffh		;0f78	ff		.
	defb 0ffh		;0f79	ff		.
	defb 0ffh		;0f7a	ff		.
	defb 0ffh		;0f7b	ff		.
	defb 0ffh		;0f7c	ff		.
	defb 0ffh		;0f7d	ff		.
	defb 0ffh		;0f7e	ff		.
	defb 0ffh		;0f7f	ff		.
	defb 0ffh		;0f80	ff		.
	defb 0ffh		;0f81	ff		.
	defb 0ffh		;0f82	ff		.
	defb 0ffh		;0f83	ff		.
	defb 0ffh		;0f84	ff		.
	defb 0ffh		;0f85	ff		.
	defb 0ffh		;0f86	ff		.
	defb 0ffh		;0f87	ff		.
	defb 0ffh		;0f88	ff		.
	defb 0ffh		;0f89	ff		.
	defb 0ffh		;0f8a	ff		.
	defb 0ffh		;0f8b	ff		.
	defb 0ffh		;0f8c	ff		.
	defb 0ffh		;0f8d	ff		.
	defb 0ffh		;0f8e	ff		.
	defb 0ffh		;0f8f	ff		.
	defb 0ffh		;0f90	ff		.
	defb 0ffh		;0f91	ff		.
	defb 0ffh		;0f92	ff		.
	defb 0ffh		;0f93	ff		.
	defb 0ffh		;0f94	ff		.
	defb 0ffh		;0f95	ff		.
	defb 0ffh		;0f96	ff		.
	defb 0ffh		;0f97	ff		.
	defb 0ffh		;0f98	ff		.
	defb 0ffh		;0f99	ff		.
	defb 0ffh		;0f9a	ff		.
	defb 0ffh		;0f9b	ff		.
	defb 0ffh		;0f9c	ff		.
	defb 0ffh		;0f9d	ff		.
	defb 0ffh		;0f9e	ff		.
	defb 0ffh		;0f9f	ff		.
	defb 0ffh		;0fa0	ff		.
	defb 0ffh		;0fa1	ff		.
	defb 0ffh		;0fa2	ff		.
	defb 0ffh		;0fa3	ff		.
	defb 0ffh		;0fa4	ff		.
	defb 0ffh		;0fa5	ff		.
	defb 0ffh		;0fa6	ff		.
	defb 0ffh		;0fa7	ff		.
	defb 0ffh		;0fa8	ff		.
	defb 0ffh		;0fa9	ff		.
	defb 0ffh		;0faa	ff		.
	defb 0ffh		;0fab	ff		.
	defb 0ffh		;0fac	ff		.
	defb 0ffh		;0fad	ff		.
	defb 0ffh		;0fae	ff		.
	defb 0ffh		;0faf	ff		.
	defb 0ffh		;0fb0	ff		.
	defb 0ffh		;0fb1	ff		.
	defb 0ffh		;0fb2	ff		.
	defb 0ffh		;0fb3	ff		.
	defb 0ffh		;0fb4	ff		.
	defb 0ffh		;0fb5	ff		.
	defb 0ffh		;0fb6	ff		.
	defb 0ffh		;0fb7	ff		.
	defb 0ffh		;0fb8	ff		.
	defb 0ffh		;0fb9	ff		.
	defb 0ffh		;0fba	ff		.
	defb 0ffh		;0fbb	ff		.
	defb 0ffh		;0fbc	ff		.
	defb 0ffh		;0fbd	ff		.
	defb 0ffh		;0fbe	ff		.
	defb 0ffh		;0fbf	ff		.
	defb 0ffh		;0fc0	ff		.
	defb 0ffh		;0fc1	ff		.
	defb 0ffh		;0fc2	ff		.
	defb 0ffh		;0fc3	ff		.
	defb 0ffh		;0fc4	ff		.
	defb 0ffh		;0fc5	ff		.
	defb 0ffh		;0fc6	ff		.
	defb 0ffh		;0fc7	ff		.
	defb 0ffh		;0fc8	ff		.
	defb 0ffh		;0fc9	ff		.
	defb 0ffh		;0fca	ff		.
	defb 0ffh		;0fcb	ff		.
	defb 0ffh		;0fcc	ff		.
	defb 0ffh		;0fcd	ff		.
	defb 0ffh		;0fce	ff		.
	defb 0ffh		;0fcf	ff		.
	defb 0ffh		;0fd0	ff		.
	defb 0ffh		;0fd1	ff		.
	defb 0ffh		;0fd2	ff		.
	defb 0ffh		;0fd3	ff		.
	defb 0ffh		;0fd4	ff		.
	defb 0ffh		;0fd5	ff		.
	defb 0ffh		;0fd6	ff		.
	defb 0ffh		;0fd7	ff		.
	defb 0ffh		;0fd8	ff		.
	defb 0ffh		;0fd9	ff		.
	defb 0ffh		;0fda	ff		.
	defb 0ffh		;0fdb	ff		.
	defb 0ffh		;0fdc	ff		.
	defb 0ffh		;0fdd	ff		.
	defb 0ffh		;0fde	ff		.
	defb 0ffh		;0fdf	ff		.
	defb 0ffh		;0fe0	ff		.
	defb 0ffh		;0fe1	ff		.
	defb 0ffh		;0fe2	ff		.
	defb 0ffh		;0fe3	ff		.
	defb 0ffh		;0fe4	ff		.
	defb 0ffh		;0fe5	ff		.
	defb 0ffh		;0fe6	ff		.
	defb 0ffh		;0fe7	ff		.
	defb 0ffh		;0fe8	ff		.
	defb 0ffh		;0fe9	ff		.
	defb 0ffh		;0fea	ff		.
	defb 0ffh		;0feb	ff		.
	defb 0ffh		;0fec	ff		.
	defb 0ffh		;0fed	ff		.
	defb 0ffh		;0fee	ff		.
	defb 0ffh		;0fef	ff		.
	defb 0ffh		;0ff0	ff		.
	defb 0ffh		;0ff1	ff		.
	defb 0ffh		;0ff2	ff		.
	defb 0ffh		;0ff3	ff		.
	defb 0ffh		;0ff4	ff		.
	defb 0ffh		;0ff5	ff		.
	defb 0ffh		;0ff6	ff		.
	defb 0ffh		;0ff7	ff		.
	defb 0ffh		;0ff8	ff		.
	defb 0ffh		;0ff9	ff		.
	defb 0ffh		;0ffa	ff		.
	defb 0ffh		;0ffb	ff		.
	defb 0ffh		;0ffc	ff		.
	defb 0ffh		;0ffd	ff		.
	defb 0ffh		;0ffe	ff		.
	defb 0ffh		;0fff	ff		.
;---------------------------------------------------------------------------
; SetupStub, assembled for #8000 (it runs from the RAM copy that page 8
; makes): "SETUP (C) 2001 PETERS PLUS LTD", JR over the text, then: SP =
; #7FFF, push the return address #8000, copy the depacker to #D000 and
; jump to it with HL = packed data (#815F in RAM = #115F here), DE =
; #8000. The depacker returns to #8000, the start of unpacked SETUP.
;---------------------------------------------------------------------------
SetupStub:

; BLOCK 'SetupStub' (start 0x1000 end 0x115f)
	defb 053h		;1000	53		S
	defb 045h		;1001	45		E
	defb 054h		;1002	54		T
	defb 055h		;1003	55		U
	defb 050h		;1004	50		P
	defb 018h		;1005	18		.
	defb 019h		;1006	19		.
	defb 028h		;1007	28		(
	defb 043h		;1008	43		C
	defb 029h		;1009	29		)
	defb 020h		;100a	20		 
	defb 032h		;100b	32		2
	defb 030h		;100c	30		0
	defb 030h		;100d	30		0
	defb 031h		;100e	31		1
	defb 020h		;100f	20		 
	defb 050h		;1010	50		P
	defb 045h		;1011	45		E
	defb 054h		;1012	54		T
	defb 045h		;1013	45		E
	defb 052h		;1014	52		R
	defb 053h		;1015	53		S
	defb 020h		;1016	20		 
	defb 050h		;1017	50		P
	defb 04ch		;1018	4c		L
	defb 055h		;1019	55		U
	defb 053h		;101a	53		S
	defb 020h		;101b	20		 
	defb 04ch		;101c	4c		L
	defb 054h		;101d	54		T
	defb 044h		;101e	44		D
	defb 020h		;101f	20		 
	defb 0f3h		;1020	f3		.
	defb 0e1h		;1021	e1		.
	defb 031h		;1022	31		1
	defb 0ffh		;1023	ff		.
	defb 07fh		;1024	7f		.
	defb 0e5h		;1025	e5		.
	defb 0f5h		;1026	f5		.
	defb 021h		;1027	21		!
	defb 000h		;1028	00		.
	defb 080h		;1029	80		.
	defb 0e5h		;102a	e5		.
	defb 011h		;102b	11		.
	defb 000h		;102c	00		.
	defb 0d0h		;102d	d0		.
	defb 0d5h		;102e	d5		.
	defb 021h		;102f	21		!
	defb 03eh		;1030	3e		>
	defb 080h		;1031	80		.
	defb 001h		;1032	01		.
	defb 0cch		;1033	cc		.
	defb 021h		;1034	21		!
	defb 0edh		;1035	ed		.
	defb 0b0h		;1036	b0		.
	defb 021h		;1037	21		!
	defb 05fh		;1038	5f		_
	defb 081h		;1039	81		.
	defb 011h		;103a	11		.
	defb 000h		;103b	00		.
	defb 080h		;103c	80		.
	defb 0c9h		;103d	c9		.
	defb 0d5h		;103e	d5		.
	defb 0e5h		;103f	e5		.
	defb 023h		;1040	23		#
	defb 023h		;1041	23		#
	defb 04eh		;1042	4e		N
	defb 023h		;1043	23		#
	defb 046h		;1044	46		F
	defb 023h		;1045	23		#
	defb 00bh		;1046	0b		.
	defb 0ebh		;1047	eb		.
	defb 009h		;1048	09		.
	defb 0ebh		;1049	eb		.
	defb 04eh		;104a	4e		N
	defb 023h		;104b	23		#
	defb 046h		;104c	46		F
	defb 00bh		;104d	0b		.
	defb 0e1h		;104e	e1		.
	defb 009h		;104f	09		.
	defb 0edh		;1050	ed		.
	defb 052h		;1051	52		R
	defb 019h		;1052	19		.
	defb 038h		;1053	38		8
	defb 002h		;1054	02		.
	defb 054h		;1055	54		T
	defb 05dh		;1056	5d		]
	defb 0edh		;1057	ed		.
	defb 0b8h		;1058	b8		.
	defb 0ebh		;1059	eb		.
	defb 0d1h		;105a	d1		.
	defb 00eh		;105b	0e		.
	defb 00ch		;105c	0c		.
	defb 009h		;105d	09		.
	defb 0e5h		;105e	e5		.
	defb 0ddh		;105f	dd		.
	defb 0e1h		;1060	e1		.
	defb 03eh		;1061	3e		>
	defb 003h		;1062	03		.
	defb 02bh		;1063	2b		+
	defb 046h		;1064	46		F
	defb 02bh		;1065	2b		+
	defb 04eh		;1066	4e		N
	defb 0c5h		;1067	c5		.
	defb 03dh		;1068	3d		=
	defb 020h		;1069	20		 
	defb 0f8h		;106a	f8		.
	defb 047h		;106b	47		G
	defb 0d9h		;106c	d9		.
	defb 016h		;106d	16		.
	defb 0bfh		;106e	bf		.
	defb 00eh		;106f	0e		.
	defb 010h		;1070	10		.
	defb 0cdh		;1071	cd		.
	defb 015h		;1072	15		.
	defb 0d1h		;1073	d1		.
	defb 0ddh		;1074	dd		.
	defb 07eh		;1075	7e		~
	defb 000h		;1076	00		.
	defb 0ddh		;1077	dd		.
	defb 023h		;1078	23		#
	defb 0d9h		;1079	d9		.
	defb 012h		;107a	12		.
	defb 013h		;107b	13		.
	defb 0d9h		;107c	d9		.
	defb 029h		;107d	29		)
	defb 010h		;107e	10		.
	defb 003h		;107f	03		.
	defb 0cdh		;1080	cd		.
	defb 015h		;1081	15		.
	defb 0d1h		;1082	d1		.
	defb 038h		;1083	38		8
	defb 0efh		;1084	ef		.
	defb 01eh		;1085	1e		.
	defb 001h		;1086	01		.
	defb 03eh		;1087	3e		>
	defb 080h		;1088	80		.
	defb 029h		;1089	29		)
	defb 010h		;108a	10		.
	defb 003h		;108b	03		.
	defb 0cdh		;108c	cd		.
	defb 015h		;108d	15		.
	defb 0d1h		;108e	d1		.
	defb 017h		;108f	17		.
	defb 038h		;1090	38		8
	defb 0f7h		;1091	f7		.
	defb 0feh		;1092	fe		.
	defb 003h		;1093	03		.
	defb 038h		;1094	38		8
	defb 005h		;1095	05		.
	defb 083h		;1096	83		.
	defb 05fh		;1097	5f		_
	defb 0a9h		;1098	a9		.
	defb 020h		;1099	20		 
	defb 0ech		;109a	ec		.
	defb 083h		;109b	83		.
	defb 0feh		;109c	fe		.
	defb 004h		;109d	04		.
	defb 028h		;109e	28		(
	defb 062h		;109f	62		b
	defb 0ceh		;10a0	ce		.
	defb 0ffh		;10a1	ff		.
	defb 0feh		;10a2	fe		.
	defb 002h		;10a3	02		.
	defb 0d9h		;10a4	d9		.
	defb 04fh		;10a5	4f		O
	defb 0d9h		;10a6	d9		.
	defb 03eh		;10a7	3e		>
	defb 0bfh		;10a8	bf		.
	defb 038h		;10a9	38		8
	defb 015h		;10aa	15		.
	defb 029h		;10ab	29		)
	defb 010h		;10ac	10		.
	defb 003h		;10ad	03		.
	defb 0cdh		;10ae	cd		.
	defb 015h		;10af	15		.
	defb 0d1h		;10b0	d1		.
	defb 017h		;10b1	17		.
	defb 038h		;10b2	38		8
	defb 0f7h		;10b3	f7		.
	defb 028h		;10b4	28		(
	defb 005h		;10b5	05		.
	defb 03ch		;10b6	3c		<
	defb 082h		;10b7	82		.
	defb 030h		;10b8	30		0
	defb 008h		;10b9	08		.
	defb 092h		;10ba	92		.
	defb 03ch		;10bb	3c		<
	defb 020h		;10bc	20		 
	defb 00dh		;10bd	0d		.
	defb 03eh		;10be	3e		>
	defb 0efh		;10bf	ef		.
	defb 00fh		;10c0	0f		.
	defb 0bfh		;10c1	bf		.
	defb 029h		;10c2	29		)
	defb 010h		;10c3	10		.
	defb 003h		;10c4	03		.
	defb 0cdh		;10c5	cd		.
	defb 015h		;10c6	15		.
	defb 0d1h		;10c7	d1		.
	defb 017h		;10c8	17		.
	defb 038h		;10c9	38		8
	defb 0f7h		;10ca	f7		.
	defb 0d9h		;10cb	d9		.
	defb 026h		;10cc	26		&
	defb 0ffh		;10cd	ff		.
	defb 028h		;10ce	28		(
	defb 009h		;10cf	09		.
	defb 067h		;10d0	67		g
	defb 03ch		;10d1	3c		<
	defb 0ddh		;10d2	dd		.
	defb 07eh		;10d3	7e		~
	defb 000h		;10d4	00		.
	defb 0ddh		;10d5	dd		.
	defb 023h		;10d6	23		#
	defb 028h		;10d7	28		(
	defb 00bh		;10d8	0b		.
	defb 06fh		;10d9	6f		o
	defb 019h		;10da	19		.
	defb 0edh		;10db	ed		.
	defb 0b0h		;10dc	b0		.
	defb 018h		;10dd	18		.
	defb 09dh		;10de	9d		.
	defb 0d9h		;10df	d9		.
	defb 0cbh		;10e0	cb		.
	defb 00ah		;10e1	0a		.
	defb 018h		;10e2	18		.
	defb 099h		;10e3	99		.
	defb 0feh		;10e4	fe		.
	defb 0e0h		;10e5	e0		.
	defb 038h		;10e6	38		8
	defb 0f1h		;10e7	f1		.
	defb 007h		;10e8	07		.
	defb 0a9h		;10e9	a9		.
	defb 03ch		;10ea	3c		<
	defb 028h		;10eb	28		(
	defb 0f2h		;10ec	f2		.
	defb 0d6h		;10ed	d6		.
	defb 010h		;10ee	10		.
	defb 06fh		;10ef	6f		o
	defb 04fh		;10f0	4f		O
	defb 026h		;10f1	26		&
	defb 0ffh		;10f2	ff		.
	defb 019h		;10f3	19		.
	defb 0edh		;10f4	ed		.
	defb 0a0h		;10f5	a0		.
	defb 0ddh		;10f6	dd		.
	defb 07eh		;10f7	7e		~
	defb 000h		;10f8	00		.
	defb 0ddh		;10f9	dd		.
	defb 023h		;10fa	23		#
	defb 012h		;10fb	12		.
	defb 023h		;10fc	23		#
	defb 013h		;10fd	13		.
	defb 07eh		;10fe	7e		~
	defb 0c3h		;10ff	c3		.
	defb 03ch		;1100	3c		<
	defb 0d0h		;1101	d0		.
	defb 03eh		;1102	3e		>
	defb 080h		;1103	80		.
	defb 029h		;1104	29		)
	defb 010h		;1105	10		.
	defb 003h		;1106	03		.
	defb 0cdh		;1107	cd		.
	defb 015h		;1108	15		.
	defb 0d1h		;1109	d1		.
	defb 08fh		;110a	8f		.
	defb 020h		;110b	20		 
	defb 024h		;110c	24		$
	defb 038h		;110d	38		8
	defb 0f5h		;110e	f5		.
	defb 03eh		;110f	3e		>
	defb 0fch		;1110	fc		.
	defb 018h		;1111	18		.
	defb 021h		;1112	21		!
	defb 047h		;1113	47		G
	defb 0ddh		;1114	dd		.
	defb 04eh		;1115	4e		N
	defb 000h		;1116	00		.
	defb 0ddh		;1117	dd		.
	defb 023h		;1118	23		#
	defb 03fh		;1119	3f		?
	defb 018h		;111a	18		.
	defb 08ah		;111b	8a		.
	defb 0feh		;111c	fe		.
	defb 00fh		;111d	0f		.
	defb 038h		;111e	38		8
	defb 0f3h		;111f	f3		.
	defb 020h		;1120	20		 
	defb 083h		;1121	83		.
	defb 006h		;1122	06		.
	defb 003h		;1123	03		.
	defb 0ebh		;1124	eb		.
	defb 0d1h		;1125	d1		.
	defb 073h		;1126	73		s
	defb 023h		;1127	23		#
	defb 072h		;1128	72		r
	defb 023h		;1129	23		#
	defb 010h		;112a	10		.
	defb 0f9h		;112b	f9		.
	defb 021h		;112c	21		!
	defb 058h		;112d	58		X
	defb 027h		;112e	27		'
	defb 0d9h		;112f	d9		.
	defb 0c9h		;1130	c9		.
	defb 09fh		;1131	9f		.
	defb 03eh		;1132	3e		>
	defb 0efh		;1133	ef		.
	defb 029h		;1134	29		)
	defb 010h		;1135	10		.
	defb 003h		;1136	03		.
	defb 0cdh		;1137	cd		.
	defb 015h		;1138	15		.
	defb 0d1h		;1139	d1		.
	defb 017h		;113a	17		.
	defb 038h		;113b	38		8
	defb 0f7h		;113c	f7		.
	defb 0d9h		;113d	d9		.
	defb 020h		;113e	20		 
	defb 0afh		;113f	af		.
	defb 0cbh		;1140	cb		.
	defb 07fh		;1141	7f		.
	defb 028h		;1142	28		(
	defb 0d8h		;1143	d8		.
	defb 0d6h		;1144	d6		.
	defb 0eah		;1145	ea		.
	defb 087h		;1146	87		.
	defb 047h		;1147	47		G
	defb 0ddh		;1148	dd		.
	defb 07eh		;1149	7e		~
	defb 000h		;114a	00		.
	defb 0ddh		;114b	dd		.
	defb 023h		;114c	23		#
	defb 012h		;114d	12		.
	defb 013h		;114e	13		.
	defb 010h		;114f	10		.
	defb 0f7h		;1150	f7		.
	defb 018h		;1151	18		.
	defb 08ah		;1152	8a		.
	defb 041h		;1153	41		A
	defb 0ddh		;1154	dd		.
	defb 06eh		;1155	6e		n
	defb 000h		;1156	00		.
	defb 0ddh		;1157	dd		.
	defb 023h		;1158	23		#
	defb 0ddh		;1159	dd		.
	defb 066h		;115a	66		f
	defb 000h		;115b	00		.
	defb 0ddh		;115c	dd		.
	defb 023h		;115d	23		#
	defb 0c9h		;115e	c9		.
;---------------------------------------------------------------------------
; SetupPacked: Hrust 1.x stream with a ZX header ("HR", unpacked length
; #3645 = 13 893, packed length, the last 6 bytes raw). scripts/hrust.py
; unpacks it; result: rom/bios304-setup.asm.
;---------------------------------------------------------------------------
SetupPacked:

; BLOCK 'SetupPacked' (start 0x115f end 0x320a)
	defb 048h		;115f	48		H
	defb 052h		;1160	52		R
	defb 045h		;1161	45		E
	defb 036h		;1162	36		6
	defb 0abh		;1163	ab		.
	defb 020h		;1164	20		 
	defb 005h		;1165	05		.
	defb 0edh		;1166	ed		.
	defb 0b0h		;1167	b0		.
	defb 0c9h		;1168	c9		.
	defb 000h		;1169	00		.
	defb 000h		;116a	00		.
	defb 023h		;116b	23		#
	defb 062h		;116c	62		b
	defb 0f1h		;116d	f1		.
	defb 0e1h		;116e	e1		.
	defb 031h		;116f	31		1
	defb 0f0h		;1170	f0		.
	defb 080h		;1171	80		.
	defb 0e5h		;1172	e5		.
	defb 0c3h		;1173	c3		.
	defb 0beh		;1174	be		.
	defb 081h		;1175	81		.
	defb 028h		;1176	28		(
	defb 043h		;1177	43		C
	defb 029h		;1178	29		)
	defb 020h		;1179	20		 
	defb 032h		;117a	32		2
	defb 030h		;117b	30		0
	defb 01fh		;117c	1f		.
	defb 08ah		;117d	8a		.
	defb 050h		;117e	50		P
	defb 045h		;117f	45		E
	defb 03eh		;1180	3e		>
	defb 01bh		;1181	1b		.
	defb 054h		;1182	54		T
	defb 052h		;1183	52		R
	defb 053h		;1184	53		S
	defb 0ech		;1185	ec		.
	defb 073h		;1186	73		s
	defb 04ch		;1187	4c		L
	defb 055h		;1188	55		U
	defb 04fh		;1189	4f		O
	defb 04ch		;118a	4c		L
	defb 054h		;118b	54		T
	defb 044h		;118c	44		D
	defb 0f3h		;118d	f3		.
	defb 03eh		;118e	3e		>
	defb 03fh		;118f	3f		?
	defb 09fh		;1190	9f		.
	defb 08dh		;1191	8d		.
	defb 0edh		;1192	ed		.
	defb 047h		;1193	47		G
	defb 056h		;1194	56		V
	defb 0c9h		;1195	c9		.
	defb 0e3h		;1196	e3		.
	defb 013h		;1197	13		.
	defb 080h		;1198	80		.
	defb 021h		;1199	21		!
	defb 0c7h		;119a	c7		.
	defb 0e7h		;119b	e7		.
	defb 001h		;119c	01		.
	defb 081h		;119d	81		.
	defb 022h		;119e	22		"
	defb 0ffh		;119f	ff		.
	defb 05eh		;11a0	5e		^
	defb 0fbh		;11a1	fb		.
	defb 001h		;11a2	01		.
	defb 0b0h		;11a3	b0		.
	defb 0c9h		;11a4	c9		.
	defb 000h		;11a5	00		.
	defb 0c7h		;11a6	c7		.
	defb 01bh		;11a7	1b		.
	defb 07fh		;11a8	7f		.
	defb 0f5h		;11a9	f5		.
	defb 008h		;11aa	08		.
	defb 0c5h		;11ab	c5		.
	defb 0cbh		;11ac	cb		.
	defb 0d5h		;11ad	d5		.
	defb 0d5h		;11ae	d5		.
	defb 0e5h		;11af	e5		.
	defb 0d9h		;11b0	d9		.
	defb 0ddh		;11b1	dd		.
	defb 0c3h		;11b2	c3		.
	defb 03dh		;11b3	3d		=
	defb 0fdh		;11b4	fd		.
	defb 0cdh		;11b5	cd		.
	defb 0f0h		;11b6	f0		.
	defb 09eh		;11b7	9e		.
	defb 01fh		;11b8	1f		.
	defb 0c6h		;11b9	c6		.
	defb 0e1h		;11ba	e1		.
	defb 0ddh		;11bb	dd		.
	defb 0d1h		;11bc	d1		.
	defb 098h		;11bd	98		.
	defb 0abh		;11be	ab		.
	defb 0c1h		;11bf	c1		.
	defb 0d9h		;11c0	d9		.
	defb 0f1h		;11c1	f1		.
	defb 008h		;11c2	08		.
	defb 0c5h		;11c3	c5		.
	defb 0dfh		;11c4	df		.
	defb 0fbh		;11c5	fb		.
	defb 0edh		;11c6	ed		.
	defb 04dh		;11c7	4d		M
	defb 0cdh		;11c8	cd		.
	defb 022h		;11c9	22		"
	defb 080h		;11ca	80		.
	defb 0e1h		;11cb	e1		.
	defb 0f0h		;11cc	f0		.
	defb 0e6h		;11cd	e6		.
	defb 02bh		;11ce	2b		+
	defb 081h		;11cf	81		.
	defb 0c3h		;11d0	c3		.
	defb 0d4h		;11d1	d4		.
	defb 021h		;11d2	21		!
	defb 050h		;11d3	50		P
	defb 08dh		;11d4	8d		.
	defb 011h		;11d5	11		.
	defb 027h		;11d6	27		'
	defb 0fdh		;11d7	fd		.
	defb 0d9h		;11d8	d9		.
	defb 001h		;11d9	01		.
	defb 0d3h		;11da	d3		.
	defb 008h		;11db	08		.
	defb 0fch		;11dc	fc		.
	defb 0b0h		;11dd	b0		.
	defb 085h		;11de	85		.
	defb 0c1h		;11df	c1		.
	defb 0fah		;11e0	fa		.
	defb 02ah		;11e1	2a		*
	defb 0fch		;11e2	fc		.
	defb 01bh		;11e3	1b		.
	defb 08ch		;11e4	8c		.
	defb 0afh		;11e5	af		.
	defb 0d3h		;11e6	d3		.
	defb 0feh		;11e7	fe		.
	defb 0ddh		;11e8	dd		.
	defb 021h		;11e9	21		!
	defb 07fh		;11ea	7f		.
	defb 08bh		;11eb	8b		.
	defb 08bh		;11ec	8b		.
	defb 053h		;11ed	53		S
	defb 01eh		;11ee	1e		.
	defb 030h		;11ef	30		0
	defb 09dh		;11f0	9d		.
	defb 001h		;11f1	01		.
	defb 00eh		;11f2	0e		.
	defb 018h		;11f3	18		.
	defb 09fh		;11f4	9f		.
	defb 04ch		;11f5	4c		L
	defb 0deh		;11f6	de		.
	defb 0d8h		;11f7	d8		.
	defb 020h		;11f8	20		 
	defb 001h		;11f9	01		.
	defb 089h		;11fa	89		.
	defb 053h		;11fb	53		S
	defb 07bh		;11fc	7b		{
	defb 007h		;11fd	07		.
	defb 020h		;11fe	20		 
	defb 002h		;11ff	02		.
	defb 03dh		;1200	3d		=
	defb 008h		;1201	08		.
	defb 005h		;1202	05		.
	defb 055h		;1203	55		U
	defb 0cch		;1204	cc		.
	defb 0c5h		;1205	c5		.
	defb 004h		;1206	04		.
	defb 0b6h		;1207	b6		.
	defb 009h		;1208	09		.
	defb 088h		;1209	88		.
	defb 0ffh		;120a	ff		.
	defb 028h		;120b	28		(
	defb 0f3h		;120c	f3		.
	defb 017h		;120d	17		.
	defb 0aeh		;120e	ae		.
	defb 089h		;120f	89		.
	defb 021h		;1210	21		!
	defb 05fh		;1211	5f		_
	defb 086h		;1212	86		.
	defb 03eh		;1213	3e		>
	defb 00bh		;1214	0b		.
	defb 006h		;1215	06		.
	defb 0b2h		;1216	b2		.
	defb 08ah		;1217	8a		.
	defb 0b1h		;1218	b1		.
	defb 081h		;1219	81		.
	defb 007h		;121a	07		.
	defb 0ebh		;121b	eb		.
	defb 04ah		;121c	4a		J
	defb 055h		;121d	55		U
	defb 001h		;121e	01		.
	defb 083h		;121f	83		.
	defb 087h		;1220	87		.
	defb 03eh		;1221	3e		>
	defb 01eh		;1222	1e		.
	defb 00ah		;1223	0a		.
	defb 076h		;1224	76		v
	defb 09eh		;1225	9e		.
	defb 0efh		;1226	ef		.
	defb 0b5h		;1227	b5		.
	defb 002h		;1228	02		.
	defb 00dh		;1229	0d		.
	defb 0d3h		;122a	d3		.
	defb 03ch		;122b	3c		<
	defb 093h		;122c	93		.
	defb 002h		;122d	02		.
	defb 00fh		;122e	0f		.
	defb 0ffh		;122f	ff		.
	defb 0dbh		;1230	db		.
	defb 0e2h		;1231	e2		.
	defb 0f5h		;1232	f5		.
	defb 03eh		;1233	3e		>
	defb 0feh		;1234	fe		.
	defb 0d3h		;1235	d3		.
	defb 021h		;1236	21		!
	defb 07bh		;1237	7b		{
	defb 0a6h		;1238	a6		.
	defb 010h		;1239	10		.
	defb 0f0h		;123a	f0		.
	defb 0e8h		;123b	e8		.
	defb 010h		;123c	10		.
	defb 0f1h		;123d	f1		.
	defb 0f0h		;123e	f0		.
	defb 0ffh		;123f	ff		.
	defb 0c9h		;1240	c9		.
	defb 02eh		;1241	2e		.
	defb 032h		;1242	32		2
	defb 035h		;1243	35		5
	defb 033h		;1244	33		3
	defb 000h		;1245	00		.
	defb 052h		;1246	52		R
	defb 045h		;1247	45		E
	defb 053h		;1248	53		S
	defb 054h		;1249	54		T
	defb 041h		;124a	41		A
	defb 003h		;124b	03		.
	defb 0c5h		;124c	c5		.
	defb 0f3h		;124d	f3		.
	defb 02eh		;124e	2e		.
	defb 0feh		;124f	fe		.
	defb 0f5h		;1250	f5		.
	defb 0afh		;1251	af		.
	defb 032h		;1252	32		2
	defb 071h		;1253	71		q
	defb 082h		;1254	82		.
	defb 0cdh		;1255	cd		.
	defb 073h		;1256	73		s
	defb 0a3h		;1257	a3		.
	defb 0fch		;1258	fc		.
	defb 087h		;1259	87		.
	defb 0ceh		;125a	ce		.
	defb 02eh		;125b	2e		.
	defb 05eh		;125c	5e		^
	defb 09bh		;125d	9b		.
	defb 0fch		;125e	fc		.
	defb 0e2h		;125f	e2		.
	defb 045h		;1260	45		E
	defb 0c4h		;1261	c4		.
	defb 08bh		;1262	8b		.
	defb 00eh		;1263	0e		.
	defb 0e2h		;1264	e2		.
	defb 0edh		;1265	ed		.
	defb 040h		;1266	40		@
	defb 0c5h		;1267	c5		.
	defb 0ffh		;1268	ff		.
	defb 0c1h		;1269	c1		.
	defb 0cbh		;126a	cb		.
	defb 079h		;126b	79		y
	defb 021h		;126c	21		!
	defb 024h		;126d	24		$
	defb 0c1h		;126e	c1		.
	defb 0afh		;126f	af		.
	defb 077h		;1270	77		w
	defb 0eeh		;1271	ee		.
	defb 075h		;1272	75		u
	defb 02ch		;1273	2c		,
	defb 0c1h		;1274	c1		.
	defb 0edh		;1275	ed		.
	defb 041h		;1276	41		A
	defb 047h		;1277	47		G
	defb 061h		;1278	61		a
	defb 0e6h		;1279	e6		.
	defb 0dah		;127a	da		.
	defb 059h		;127b	59		Y
	defb 0b3h		;127c	b3		.
	defb 084h		;127d	84		.
	defb 06ah		;127e	6a		j
	defb 03eh		;127f	3e		>
	defb 01fh		;1280	1f		.
	defb 0e6h		;1281	e6		.
	defb 0a6h		;1282	a6		.
	defb 0f2h		;1283	f2		.
	defb 027h		;1284	27		'
	defb 0f5h		;1285	f5		.
	defb 0a7h		;1286	a7		.
	defb 05fh		;1287	5f		_
	defb 03eh		;1288	3e		>
	defb 040h		;1289	40		@
	defb 033h		;128a	33		3
	defb 0f0h		;128b	f0		.
	defb 0b0h		;128c	b0		.
	defb 03ah		;128d	3a		:
	defb 000h		;128e	00		.
	defb 0c4h		;128f	c4		.
	defb 057h		;1290	57		W
	defb 0cbh		;1291	cb		.
	defb 032h		;1292	32		2
	defb 031h		;1293	31		1
	defb 0ebh		;1294	eb		.
	defb 0f1h		;1295	f1		.
	defb 001h		;1296	01		.
	defb 054h		;1297	54		T
	defb 0b4h		;1298	b4		.
	defb 0aah		;1299	aa		.
	defb 0cdh		;129a	cd		.
	defb 07ah		;129b	7a		z
	defb 07bh		;129c	7b		{
	defb 031h		;129d	31		1
	defb 0eah		;129e	ea		.
	defb 036h		;129f	36		6
	defb 0a5h		;12a0	a5		.
	defb 0bfh		;12a1	bf		.
	defb 070h		;12a2	70		p
	defb 019h		;12a3	19		.
	defb 09bh		;12a4	9b		.
	defb 0c1h		;12a5	c1		.
	defb 004h		;12a6	04		.
	defb 005h		;12a7	05		.
	defb 020h		;12a8	20		 
	defb 039h		;12a9	39		9
	defb 0f2h		;12aa	f2		.
	defb 00eh		;12ab	0e		.
	defb 0f9h		;12ac	f9		.
	defb 0f3h		;12ad	f3		.
	defb 0b1h		;12ae	b1		.
	defb 000h		;12af	00		.
	defb 0f0h		;12b0	f0		.
	defb 011h		;12b1	11		.
	defb 0b6h		;12b2	b6		.
	defb 081h		;12b3	81		.
	defb 006h		;12b4	06		.
	defb 09fh		;12b5	9f		.
	defb 0beh		;12b6	be		.
	defb 028h		;12b7	28		(
	defb 06ah		;12b8	6a		j
	defb 083h		;12b9	83		.
	defb 0c4h		;12ba	c4		.
	defb 041h		;12bb	41		A
	defb 082h		;12bc	82		.
	defb 0aeh		;12bd	ae		.
	defb 028h		;12be	28		(
	defb 017h		;12bf	17		.
	defb 0f1h		;12c0	f1		.
	defb 061h		;12c1	61		a
	defb 0e7h		;12c2	e7		.
	defb 018h		;12c3	18		.
	defb 019h		;12c4	19		.
	defb 0f5h		;12c5	f5		.
	defb 021h		;12c6	21		!
	defb 029h		;12c7	29		)
	defb 0a6h		;12c8	a6		.
	defb 019h		;12c9	19		.
	defb 0f0h		;12ca	f0		.
	defb 0e9h		;12cb	e9		.
	defb 008h		;12cc	08		.
	defb 0ebh		;12cd	eb		.
	defb 0c6h		;12ce	c6		.
	defb 09ah		;12cf	9a		.
	defb 0f7h		;12d0	f7		.
	defb 008h		;12d1	08		.
	defb 05ah		;12d2	5a		Z
	defb 0c9h		;12d3	c9		.
	defb 0e8h		;12d4	e8		.
	defb 0fch		;12d5	fc		.
	defb 0b7h		;12d6	b7		.
	defb 0c2h		;12d7	c2		.
	defb 023h		;12d8	23		#
	defb 081h		;12d9	81		.
	defb 0cdh		;12da	cd		.
	defb 02dh		;12db	2d		-
	defb 0f6h		;12dc	f6		.
	defb 0dah		;12dd	da		.
	defb 028h		;12de	28		(
	defb 005h		;12df	05		.
	defb 036h		;12e0	36		6
	defb 00ah		;12e1	0a		.
	defb 01eh		;12e2	1e		.
	defb 08fh		;12e3	8f		.
	defb 069h		;12e4	69		i
	defb 0bbh		;12e5	bb		.
	defb 029h		;12e6	29		)
	defb 000h		;12e7	00		.
	defb 008h		;12e8	08		.
	defb 0c3h		;12e9	c3		.
	defb 085h		;12ea	85		.
	defb 0b7h		;12eb	b7		.
	defb 028h		;12ec	28		(
	defb 00bh		;12ed	0b		.
	defb 00ch		;12ee	0c		.
	defb 0b2h		;12ef	b2		.
	defb 00dh		;12f0	0d		.
	defb 0dbh		;12f1	db		.
	defb 06eh		;12f2	6e		n
	defb 09bh		;12f3	9b		.
	defb 0b3h		;12f4	b3		.
	defb 089h		;12f5	89		.
	defb 0c5h		;12f6	c5		.
	defb 0a4h		;12f7	a4		.
	defb 0d5h		;12f8	d5		.
	defb 09bh		;12f9	9b		.
	defb 029h		;12fa	29		)
	defb 0feh		;12fb	fe		.
	defb 0ffh		;12fc	ff		.
	defb 083h		;12fd	83		.
	defb 0d9h		;12fe	d9		.
	defb 00eh		;12ff	0e		.
	defb 016h		;1300	16		.
	defb 01dh		;1301	1d		.
	defb 013h		;1302	13		.
	defb 05fh		;1303	5f		_
	defb 035h		;1304	35		5
	defb 0aah		;1305	aa		.
	defb 00ch		;1306	0c		.
	defb 01dh		;1307	1d		.
	defb 075h		;1308	75		u
	defb 0abh		;1309	ab		.
	defb 06fh		;130a	6f		o
	defb 0abh		;130b	ab		.
	defb 05ah		;130c	5a		Z
	defb 017h		;130d	17		.
	defb 057h		;130e	57		W
	defb 0a2h		;130f	a2		.
	defb 00eh		;1310	0e		.
	defb 0c0h		;1311	c0		.
	defb 0bdh		;1312	bd		.
	defb 0c5h		;1313	c5		.
	defb 061h		;1314	61		a
	defb 0a9h		;1315	a9		.
	defb 04fh		;1316	4f		O
	defb 01ah		;1317	1a		.
	defb 095h		;1318	95		.
	defb 0f7h		;1319	f7		.
	defb 0e1h		;131a	e1		.
	defb 0f9h		;131b	f9		.
	defb 005h		;131c	05		.
	defb 04fh		;131d	4f		O
	defb 09bh		;131e	9b		.
	defb 03eh		;131f	3e		>
	defb 019h		;1320	19		.
	defb 038h		;1321	38		8
	defb 01dh		;1322	1d		.
	defb 0b3h		;1323	b3		.
	defb 0d3h		;1324	d3		.
	defb 096h		;1325	96		.
	defb 083h		;1326	83		.
	defb 0fdh		;1327	fd		.
	defb 018h		;1328	18		.
	defb 0dch		;1329	dc		.
	defb 04ch		;132a	4c		L
	defb 02ch		;132b	2c		,
	defb 0f8h		;132c	f8		.
	defb 0abh		;132d	ab		.
	defb 0e0h		;132e	e0		.
	defb 020h		;132f	20		 
	defb 007h		;1330	07		.
	defb 06dh		;1331	6d		m
	defb 0ceh		;1332	ce		.
	defb 0a9h		;1333	a9		.
	defb 06bh		;1334	6b		k
	defb 0deh		;1335	de		.
	defb 018h		;1336	18		.
	defb 003h		;1337	03		.
	defb 05eh		;1338	5e		^
	defb 00fh		;1339	0f		.
	defb 076h		;133a	76		v
	defb 00bh		;133b	0b		.
	defb 01eh		;133c	1e		.
	defb 00fh		;133d	0f		.
	defb 0aah		;133e	aa		.
	defb 09ah		;133f	9a		.
	defb 076h		;1340	76		v
	defb 0d1h		;1341	d1		.
	defb 0c5h		;1342	c5		.
	defb 092h		;1343	92		.
	defb 0eeh		;1344	ee		.
	defb 04ah		;1345	4a		J
	defb 040h		;1346	40		@
	defb 085h		;1347	85		.
	defb 0dah		;1348	da		.
	defb 0feh		;1349	fe		.
	defb 064h		;134a	64		d
	defb 09eh		;134b	9e		.
	defb 078h		;134c	78		x
	defb 0e6h		;134d	e6		.
	defb 010h		;134e	10		.
	defb 020h		;134f	20		 
	defb 00eh		;1350	0e		.
	defb 06eh		;1351	6e		n
	defb 053h		;1352	53		S
	defb 0c9h		;1353	c9		.
	defb 0beh		;1354	be		.
	defb 023h		;1355	23		#
	defb 0dch		;1356	dc		.
	defb 0ach		;1357	ac		.
	defb 04ch		;1358	4c		L
	defb 069h		;1359	69		i
	defb 0e6h		;135a	e6		.
	defb 022h		;135b	22		"
	defb 094h		;135c	94		.
	defb 05fh		;135d	5f		_
	defb 0c4h		;135e	c4		.
	defb 097h		;135f	97		.
	defb 032h		;1360	32		2
	defb 001h		;1361	01		.
	defb 01dh		;1362	1d		.
	defb 076h		;1363	76		v
	defb 0f2h		;1364	f2		.
	defb 0b7h		;1365	b7		.
	defb 087h		;1366	87		.
	defb 04ah		;1367	4a		J
	defb 0cah		;1368	ca		.
	defb 02ch		;1369	2c		,
	defb 04eh		;136a	4e		N
	defb 073h		;136b	73		s
	defb 025h		;136c	25		%
	defb 0bch		;136d	bc		.
	defb 0fbh		;136e	fb		.
	defb 0ebh		;136f	eb		.
	defb 048h		;1370	48		H
	defb 0feh		;1371	fe		.
	defb 01bh		;1372	1b		.
	defb 087h		;1373	87		.
	defb 0a6h		;1374	a6		.
	defb 089h		;1375	89		.
	defb 0f5h		;1376	f5		.
	defb 00dh		;1377	0d		.
	defb 0c2h		;1378	c2		.
	defb 039h		;1379	39		9
	defb 083h		;137a	83		.
	defb 0afh		;137b	af		.
	defb 0bfh		;137c	bf		.
	defb 0f0h		;137d	f0		.
	defb 097h		;137e	97		.
	defb 0feh		;137f	fe		.
	defb 057h		;1380	57		W
	defb 0c8h		;1381	c8		.
	defb 0ddh		;1382	dd		.
	defb 04fh		;1383	4f		O
	defb 0a7h		;1384	a7		.
	defb 0edh		;1385	ed		.
	defb 052h		;1386	52		R
	defb 028h		;1387	28		(
	defb 00ch		;1388	0c		.
	defb 063h		;1389	63		c
	defb 035h		;138a	35		5
	defb 01bh		;138b	1b		.
	defb 001h		;138c	01		.
	defb 020h		;138d	20		 
	defb 0dbh		;138e	db		.
	defb 098h		;138f	98		.
	defb 0ech		;1390	ec		.
	defb 0e1h		;1391	e1		.
	defb 0fah		;1392	fa		.
	defb 023h		;1393	23		#
	defb 057h		;1394	57		W
	defb 007h		;1395	07		.
	defb 098h		;1396	98		.
	defb 045h		;1397	45		E
	defb 060h		;1398	60		`
	defb 01ah		;1399	1a		.
	defb 082h		;139a	82		.
	defb 0fdh		;139b	fd		.
	defb 0c0h		;139c	c0		.
	defb 023h		;139d	23		#
	defb 013h		;139e	13		.
	defb 010h		;139f	10		.
	defb 0f9h		;13a0	f9		.
	defb 0c9h		;13a1	c9		.
	defb 0fch		;13a2	fc		.
	defb 033h		;13a3	33		3
	defb 081h		;13a4	81		.
	defb 0c9h		;13a5	c9		.
	defb 016h		;13a6	16		.
	defb 00ah		;13a7	0a		.
	defb 00eh		;13a8	0e		.
	defb 0f6h		;13a9	f6		.
	defb 0dfh		;13aa	df		.
	defb 0feh		;13ab	fe		.
	defb 026h		;13ac	26		&
	defb 0bah		;13ad	ba		.
	defb 08ch		;13ae	8c		.
	defb 0c5h		;13af	c5		.
	defb 00ch		;13b0	0c		.
	defb 09dh		;13b1	9d		.
	defb 0afh		;13b2	af		.
	defb 050h		;13b3	50		P
	defb 0c8h		;13b4	c8		.
	defb 02ch		;13b5	2c		,
	defb 0fbh		;13b6	fb		.
	defb 03eh		;13b7	3e		>
	defb 026h		;13b8	26		&
	defb 0f7h		;13b9	f7		.
	defb 06dh		;13ba	6d		m
	defb 009h		;13bb	09		.
	defb 0c5h		;13bc	c5		.
	defb 002h		;13bd	02		.
	defb 066h		;13be	66		f
	defb 0b3h		;13bf	b3		.
	defb 00ch		;13c0	0c		.
	defb 09bh		;13c1	9b		.
	defb 055h		;13c2	55		U
	defb 050h		;13c3	50		P
	defb 00dh		;13c4	0d		.
	defb 07ch		;13c5	7c		|
	defb 032h		;13c6	32		2
	defb 080h		;13c7	80		.
	defb 08fh		;13c8	8f		.
	defb 0f9h		;13c9	f9		.
	defb 0c9h		;13ca	c9		.
	defb 001h		;13cb	01		.
	defb 010h		;13cc	10		.
	defb 070h		;13cd	70		p
	defb 01bh		;13ce	1b		.
	defb 0a7h		;13cf	a7		.
	defb 0b2h		;13d0	b2		.
	defb 067h		;13d1	67		g
	defb 05bh		;13d2	5b		[
	defb 006h		;13d3	06		.
	defb 069h		;13d4	69		i
	defb 0f6h		;13d5	f6		.
	defb 000h		;13d6	00		.
	defb 0cah		;13d7	ca		.
	defb 0efh		;13d8	ef		.
	defb 083h		;13d9	83		.
	defb 03dh		;13da	3d		=
	defb 001h		;13db	01		.
	defb 0a1h		;13dc	a1		.
	defb 0b6h		;13dd	b6		.
	defb 080h		;13de	80		.
	defb 0d5h		;13df	d5		.
	defb 067h		;13e0	67		g
	defb 016h		;13e1	16		.
	defb 084h		;13e2	84		.
	defb 081h		;13e3	81		.
	defb 0ach		;13e4	ac		.
	defb 0d6h		;13e5	d6		.
	defb 06eh		;13e6	6e		n
	defb 02dh		;13e7	2d		-
	defb 0d7h		;13e8	d7		.
	defb 00ch		;13e9	0c		.
	defb 037h		;13ea	37		7
	defb 0c9h		;13eb	c9		.
	defb 0c5h		;13ec	c5		.
	defb 036h		;13ed	36		6
	defb 01eh		;13ee	1e		.
	defb 01ch		;13ef	1c		.
	defb 03dh		;13f0	3d		=
	defb 023h		;13f1	23		#
	defb 0f1h		;13f2	f1		.
	defb 00eh		;13f3	0e		.
	defb 051h		;13f4	51		Q
	defb 0b1h		;13f5	b1		.
	defb 0c1h		;13f6	c1		.
	defb 0d8h		;13f7	d8		.
	defb 0c3h		;13f8	c3		.
	defb 0deh		;13f9	de		.
	defb 019h		;13fa	19		.
	defb 084h		;13fb	84		.
	defb 0bfh		;13fc	bf		.
	defb 053h		;13fd	53		S
	defb 0d2h		;13fe	d2		.
	defb 0c1h		;13ff	c1		.
	defb 06ch		;1400	6c		l
	defb 0a8h		;1401	a8		.
	defb 0cbh		;1402	cb		.
	defb 0f0h		;1403	f0		.
	defb 0aah		;1404	aa		.
	defb 0aah		;1405	aa		.
	defb 021h		;1406	21		!
	defb 09ah		;1407	9a		.
	defb 0a5h		;1408	a5		.
	defb 062h		;1409	62		b
	defb 008h		;140a	08		.
	defb 02fh		;140b	2f		/
	defb 0ddh		;140c	dd		.
	defb 08ah		;140d	8a		.
	defb 0e4h		;140e	e4		.
	defb 003h		;140f	03		.
	defb 0fdh		;1410	fd		.
	defb 0eah		;1411	ea		.
	defb 0f3h		;1412	f3		.
	defb 021h		;1413	21		!
	defb 0c0h		;1414	c0		.
	defb 0c1h		;1415	c1		.
	defb 028h		;1416	28		(
	defb 012h		;1417	12		.
	defb 0c8h		;1418	c8		.
	defb 0a7h		;1419	a7		.
	defb 014h		;141a	14		.
	defb 03dh		;141b	3d		=
	defb 048h		;141c	48		H
	defb 059h		;141d	59		Y
	defb 0cdh		;141e	cd		.
	defb 0d0h		;141f	d0		.
	defb 030h		;1420	30		0
	defb 09fh		;1421	9f		.
	defb 004h		;1422	04		.
	defb 0d8h		;1423	d8		.
	defb 0e6h		;1424	e6		.
	defb 044h		;1425	44		D
	defb 07eh		;1426	7e		~
	defb 007h		;1427	07		.
	defb 008h		;1428	08		.
	defb 0f3h		;1429	f3		.
	defb 017h		;142a	17		.
	defb 0dfh		;142b	df		.
	defb 0feh		;142c	fe		.
	defb 002h		;142d	02		.
	defb 0cah		;142e	ca		.
	defb 001h		;142f	01		.
	defb 084h		;1430	84		.
	defb 0f5h		;1431	f5		.
	defb 03eh		;1432	3e		>
	defb 019h		;1433	19		.
	defb 0c7h		;1434	c7		.
	defb 02ch		;1435	2c		,
	defb 0c7h		;1436	c7		.
	defb 0f1h		;1437	f1		.
	defb 0feh		;1438	fe		.
	defb 0ffh		;1439	ff		.
	defb 037h		;143a	37		7
	defb 0e2h		;143b	e2		.
	defb 081h		;143c	81		.
	defb 0fdh		;143d	fd		.
	defb 0ddh		;143e	dd		.
	defb 0b8h		;143f	b8		.
	defb 018h		;1440	18		.
	defb 033h		;1441	33		3
	defb 02fh		;1442	2f		/
	defb 07eh		;1443	7e		~
	defb 078h		;1444	78		x
	defb 0aeh		;1445	ae		.
	defb 051h		;1446	51		Q
	defb 055h		;1447	55		U
	defb 0f5h		;1448	f5		.
	defb 0abh		;1449	ab		.
	defb 073h		;144a	73		s
	defb 099h		;144b	99		.
	defb 078h		;144c	78		x
	defb 008h		;144d	08		.
	defb 07eh		;144e	7e		~
	defb 0dah		;144f	da		.
	defb 0f2h		;1450	f2		.
	defb 011h		;1451	11		.
	defb 086h		;1452	86		.
	defb 084h		;1453	84		.
	defb 006h		;1454	06		.
	defb 00ch		;1455	0c		.
	defb 0fbh		;1456	fb		.
	defb 037h		;1457	37		7
	defb 0bch		;1458	bc		.
	defb 064h		;1459	64		d
	defb 0fah		;145a	fa		.
	defb 0f8h		;145b	f8		.
	defb 008h		;145c	08		.
	defb 02ah		;145d	2a		*
	defb 024h		;145e	24		$
	defb 07fh		;145f	7f		.
	defb 0d9h		;1460	d9		.
	defb 0fch		;1461	fc		.
	defb 0f1h		;1462	f1		.
	defb 0dch		;1463	dc		.
	defb 092h		;1464	92		.
	defb 084h		;1465	84		.
	defb 053h		;1466	53		S
	defb 074h		;1467	74		t
	defb 061h		;1468	61		a
	defb 0cfh		;1469	cf		.
	defb 017h		;146a	17		.
	defb 072h		;146b	72		r
	defb 069h		;146c	69		i
	defb 06eh		;146d	6e		n
	defb 067h		;146e	67		g
	defb 02eh		;146f	2e		.
	defb 07ch		;1470	7c		|
	defb 0f2h		;1471	f2		.
	defb 02ch		;1472	2c		,
	defb 0edh		;1473	ed		.
	defb 056h		;1474	56		V
	defb 021h		;1475	21		!
	defb 0a3h		;1476	a3		.
	defb 084h		;1477	84		.
	defb 06ch		;1478	6c		l
	defb 0ddh		;1479	dd		.
	defb 0c0h		;147a	c0		.
	defb 07ch		;147b	7c		|
	defb 001h		;147c	01		.
	defb 01ah		;147d	1a		.
	defb 0adh		;147e	ad		.
	defb 0c3h		;147f	c3		.
	defb 0e6h		;1480	e6		.
	defb 0f8h		;1481	f8		.
	defb 031h		;1482	31		1
	defb 0ffh		;1483	ff		.
	defb 07fh		;1484	7f		.
	defb 0d4h		;1485	d4		.
	defb 0feh		;1486	fe		.
	defb 0c1h		;1487	c1		.
	defb 080h		;1488	80		.
	defb 054h		;1489	54		T
	defb 044h		;148a	44		D
	defb 05dh		;148b	5d		]
	defb 04dh		;148c	4d		M
	defb 01ch		;148d	1c		.
	defb 075h		;148e	75		u
	defb 0b8h		;148f	b8		.
	defb 035h		;1490	35		5
	defb 0f4h		;1491	f4		.
	defb 0a1h		;1492	a1		.
	defb 002h		;1493	02		.
	defb 080h		;1494	80		.
	defb 046h		;1495	46		F
	defb 00ch		;1496	0c		.
	defb 009h		;1497	09		.
	defb 03ch		;1498	3c		<
	defb 012h		;1499	12		.
	defb 050h		;149a	50		P
	defb 0cbh		;149b	cb		.
	defb 099h		;149c	99		.
	defb 091h		;149d	91		.
	defb 003h		;149e	03		.
	defb 009h		;149f	09		.
	defb 00fh		;14a0	0f		.
	defb 0cbh		;14a1	cb		.
	defb 0fdh		;14a2	fd		.
	defb 05fh		;14a3	5f		_
	defb 049h		;14a4	49		I
	defb 021h		;14a5	21		!
	defb 0e0h		;14a6	e0		.
	defb 0c1h		;14a7	c1		.
	defb 001h		;14a8	01		.
	defb 0ffh		;14a9	ff		.
	defb 020h		;14aa	20		 
	defb 071h		;14ab	71		q
	defb 023h		;14ac	23		#
	defb 010h		;14ad	10		.
	defb 0fch		;14ae	fc		.
	defb 04bh		;14af	4b		K
	defb 039h		;14b0	39		9
	defb 05dh		;14b1	5d		]
	defb 001h		;14b2	01		.
	defb 011h		;14b3	11		.
	defb 000h		;14b4	00		.
	defb 0e8h		;14b5	e8		.
	defb 066h		;14b6	66		f
	defb 0c9h		;14b7	c9		.
	defb 021h		;14b8	21		!
	defb 0c5h		;14b9	c5		.
	defb 084h		;14ba	84		.
	defb 088h		;14bb	88		.
	defb 006h		;14bc	06		.
	defb 0d6h		;14bd	d6		.
	defb 0bbh		;14be	bb		.
	defb 0bdh		;14bf	bd		.
	defb 03dh		;14c0	3d		=
	defb 020h		;14c1	20		 
	defb 012h		;14c2	12		.
	defb 0dah		;14c3	da		.
	defb 011h		;14c4	11		.
	defb 0ebh		;14c5	eb		.
	defb 096h		;14c6	96		.
	defb 0dah		;14c7	da		.
	defb 04bh		;14c8	4b		K
	defb 002h		;14c9	02		.
	defb 09bh		;14ca	9b		.
	defb 0dch		;14cb	dc		.
	defb 00ch		;14cc	0c		.
	defb 0f8h		;14cd	f8		.
	defb 0edh		;14ce	ed		.
	defb 0dch		;14cf	dc		.
	defb 0e8h		;14d0	e8		.
	defb 025h		;14d1	25		%
	defb 0dfh		;14d2	df		.
	defb 0dch		;14d3	dc		.
	defb 0c9h		;14d4	c9		.
	defb 0a3h		;14d5	a3		.
	defb 0bch		;14d6	bc		.
	defb 07eh		;14d7	7e		~
	defb 0f0h		;14d8	f0		.
	defb 0a3h		;14d9	a3		.
	defb 0cdh		;14da	cd		.
	defb 052h		;14db	52		R
	defb 085h		;14dc	85		.
	defb 0dbh		;14dd	db		.
	defb 08ch		;14de	8c		.
	defb 02fh		;14df	2f		/
	defb 084h		;14e0	84		.
	defb 05bh		;14e1	5b		[
	defb 06ch		;14e2	6c		l
	defb 001h		;14e3	01		.
	defb 0c6h		;14e4	c6		.
	defb 060h		;14e5	60		`
	defb 030h		;14e6	30		0
	defb 08ch		;14e7	8c		.
	defb 0dbh		;14e8	db		.
	defb 0f2h		;14e9	f2		.
	defb 00eh		;14ea	0e		.
	defb 0edh		;14eb	ed		.
	defb 040h		;14ec	40		@
	defb 0d1h		;14ed	d1		.
	defb 07bh		;14ee	7b		{
	defb 088h		;14ef	88		.
	defb 060h		;14f0	60		`
	defb 041h		;14f1	41		A
	defb 0c9h		;14f2	c9		.
	defb 06dh		;14f3	6d		m
	defb 044h		;14f4	44		D
	defb 0c0h		;14f5	c0		.
	defb 0d4h		;14f6	d4		.
	defb 03ch		;14f7	3c		<
	defb 0c8h		;14f8	c8		.
	defb 0d7h		;14f9	d7		.
	defb 01eh		;14fa	1e		.
	defb 0bdh		;14fb	bd		.
	defb 0dah		;14fc	da		.
	defb 02bh		;14fd	2b		+
	defb 0bbh		;14fe	bb		.
	defb 03ch		;14ff	3c		<
	defb 0eeh		;1500	ee		.
	defb 0b9h		;1501	b9		.
	defb 03ch		;1502	3c		<
	defb 0c9h		;1503	c9		.
	defb 0a7h		;1504	a7		.
	defb 067h		;1505	67		g
	defb 09fh		;1506	9f		.
	defb 0dfh		;1507	df		.
	defb 07fh		;1508	7f		.
	defb 030h		;1509	30		0
	defb 02eh		;150a	2e		.
	defb 00eh		;150b	0e		.
	defb 028h		;150c	28		(
	defb 01ah		;150d	1a		.
	defb 03dh		;150e	3d		=
	defb 062h		;150f	62		b
	defb 016h		;1510	16		.
	defb 0c0h		;1511	c0		.
	defb 00fh		;1512	0f		.
	defb 012h		;1513	12		.
	defb 01eh		;1514	1e		.
	defb 0ach		;1515	ac		.
	defb 061h		;1516	61		a
	defb 016h		;1517	16		.
	defb 010h		;1518	10		.
	defb 00ah		;1519	0a		.
	defb 00ch		;151a	0c		.
	defb 0b4h		;151b	b4		.
	defb 0feh		;151c	fe		.
	defb 057h		;151d	57		W
	defb 0b6h		;151e	b6		.
	defb 0f1h		;151f	f1		.
	defb 065h		;1520	65		e
	defb 06dh		;1521	6d		m
	defb 074h		;1522	74		t
	defb 080h		;1523	80		.
	defb 085h		;1524	85		.
	defb 03dh		;1525	3d		=
	defb 03fh		;1526	3f		?
	defb 09ch		;1527	9c		.
	defb 0fah		;1528	fa		.
	defb 076h		;1529	76		v
	defb 022h		;152a	22		"
	defb 0d9h		;152b	d9		.
	defb 0c9h		;152c	c9		.
	defb 0e5h		;152d	e5		.
	defb 07dh		;152e	7d		}
	defb 00bh		;152f	0b		.
	defb 09fh		;1530	9f		.
	defb 0c2h		;1531	c2		.
	defb 0a9h		;1532	a9		.
	defb 089h		;1533	89		.
	defb 0fbh		;1534	fb		.
	defb 0e1h		;1535	e1		.
	defb 07ch		;1536	7c		|
	defb 049h		;1537	49		I
	defb 098h		;1538	98		.
	defb 0ffh		;1539	ff		.
	defb 096h		;153a	96		.
	defb 0d4h		;153b	d4		.
	defb 0bch		;153c	bc		.
	defb 089h		;153d	89		.
	defb 03ah		;153e	3a		:
	defb 004h		;153f	04		.
	defb 098h		;1540	98		.
	defb 03ch		;1541	3c		<
	defb 03eh		;1542	3e		>
	defb 013h		;1543	13		.
	defb 0d5h		;1544	d5		.
	defb 0ceh		;1545	ce		.
	defb 04bh		;1546	4b		K
	defb 014h		;1547	14		.
	defb 0d5h		;1548	d5		.
	defb 0bfh		;1549	bf		.
	defb 04ch		;154a	4c		L
	defb 0deh		;154b	de		.
	defb 097h		;154c	97		.
	defb 08ah		;154d	8a		.
	defb 023h		;154e	23		#
	defb 096h		;154f	96		.
	defb 0c3h		;1550	c3		.
	defb 0c5h		;1551	c5		.
	defb 085h		;1552	85		.
	defb 037h		;1553	37		7
	defb 038h		;1554	38		8
	defb 039h		;1555	39		9
	defb 03ah		;1556	3a		:
	defb 03bh		;1557	3b		;
	defb 03ch		;1558	3c		<
	defb 03dh		;1559	3d		=
	defb 03eh		;155a	3e		>
	defb 02fh		;155b	2f		/
	defb 030h		;155c	30		0
	defb 031h		;155d	31		1
	defb 032h		;155e	32		2
	defb 033h		;155f	33		3
	defb 034h		;1560	34		4
	defb 035h		;1561	35		5
	defb 036h		;1562	36		6
	defb 07ch		;1563	7c		|
	defb 059h		;1564	59		Y
	defb 0eah		;1565	ea		.
	defb 0e2h		;1566	e2		.
	defb 0f3h		;1567	f3		.
	defb 085h		;1568	85		.
	defb 0feh		;1569	fe		.
	defb 000h		;156a	00		.
	defb 028h		;156b	28		(
	defb 014h		;156c	14		.
	defb 0eeh		;156d	ee		.
	defb 048h		;156e	48		H
	defb 07ch		;156f	7c		|
	defb 001h		;1570	01		.
	defb 04bh		;1571	4b		K
	defb 04fh		;1572	4f		O
	defb 07ch		;1573	7c		|
	defb 0f2h		;1574	f2		.
	defb 04fh		;1575	4f		O
	defb 089h		;1576	89		.
	defb 002h		;1577	02		.
	defb 021h		;1578	21		!
	defb 0b6h		;1579	b6		.
	defb 089h		;157a	89		.
	defb 0f6h		;157b	f6		.
	defb 037h		;157c	37		7
	defb 030h		;157d	30		0
	defb 096h		;157e	96		.
	defb 04ch		;157f	4c		L
	defb 091h		;1580	91		.
	defb 095h		;1581	95		.
	defb 001h		;1582	01		.
	defb 014h		;1583	14		.
	defb 01fh		;1584	1f		.
	defb 035h		;1585	35		5
	defb 036h		;1586	36		6
	defb 0ffh		;1587	ff		.
	defb 0cdh		;1588	cd		.
	defb 0bch		;1589	bc		.
	defb 097h		;158a	97		.
	defb 0d8h		;158b	d8		.
	defb 070h		;158c	70		p
	defb 0d7h		;158d	d7		.
	defb 0ddh		;158e	dd		.
	defb 07eh		;158f	7e		~
	defb 002h		;1590	02		.
	defb 046h		;1591	46		F
	defb 032h		;1592	32		2
	defb 006h		;1593	06		.
	defb 073h		;1594	73		s
	defb 04fh		;1595	4f		O
	defb 09fh		;1596	9f		.
	defb 02eh		;1597	2e		.
	defb 014h		;1598	14		.
	defb 03dh		;1599	3d		=
	defb 03eh		;159a	3e		>
	defb 026h		;159b	26		&
	defb 005h		;159c	05		.
	defb 04eh		;159d	4e		N
	defb 07dh		;159e	7d		}
	defb 0e1h		;159f	e1		.
	defb 06fh		;15a0	6f		o
	defb 022h		;15a1	22		"
	defb 002h		;15a2	02		.
	defb 08bh		;15a3	8b		.
	defb 02ah		;15a4	2a		*
	defb 09bh		;15a5	9b		.
	defb 0e6h		;15a6	e6		.
	defb 007h		;15a7	07		.
	defb 00ch		;15a8	0c		.
	defb 03eh		;15a9	3e		>
	defb 0a0h		;15aa	a0		.
	defb 001h		;15ab	01		.
	defb 052h		;15ac	52		R
	defb 041h		;15ad	41		A
	defb 033h		;15ae	33		3
	defb 02fh		;15af	2f		/
	defb 004h		;15b0	04		.
	defb 0f3h		;15b1	f3		.
	defb 032h		;15b2	32		2
	defb 005h		;15b3	05		.
	defb 098h		;15b4	98		.
	defb 0ech		;15b5	ec		.
	defb 01ch		;15b6	1c		.
	defb 019h		;15b7	19		.
	defb 000h		;15b8	00		.
	defb 0c9h		;15b9	c9		.
	defb 0e2h		;15ba	e2		.
	defb 060h		;15bb	60		`
	defb 0dah		;15bc	da		.
	defb 0d1h		;15bd	d1		.
	defb 0fah		;15be	fa		.
	defb 017h		;15bf	17		.
	defb 062h		;15c0	62		b
	defb 042h		;15c1	42		B
	defb 0beh		;15c2	be		.
	defb 07fh		;15c3	7f		.
	defb 003h		;15c4	03		.
	defb 0afh		;15c5	af		.
	defb 0edh		;15c6	ed		.
	defb 0b1h		;15c7	b1		.
	defb 07eh		;15c8	7e		~
	defb 0b7h		;15c9	b7		.
	defb 0c8h		;15ca	c8		.
	defb 0cdh		;15cb	cd		.
	defb 008h		;15cc	08		.
	defb 08ah		;15cd	8a		.
	defb 0c9h		;15ce	c9		.
	defb 001h		;15cf	01		.
	defb 0edh		;15d0	ed		.
	defb 0dfh		;15d1	df		.
	defb 0d5h		;15d2	d5		.
	defb 0e5h		;15d3	e5		.
	defb 0c5h		;15d4	c5		.
	defb 078h		;15d5	78		x
	defb 0cdh		;15d6	cd		.
	defb 072h		;15d7	72		r
	defb 0ebh		;15d8	eb		.
	defb 098h		;15d9	98		.
	defb 087h		;15da	87		.
	defb 03eh		;15db	3e		>
	defb 02dh		;15dc	2d		-
	defb 05ah		;15dd	5a		Z
	defb 0c1h		;15de	c1		.
	defb 0adh		;15df	ad		.
	defb 03dh		;15e0	3d		=
	defb 0d9h		;15e1	d9		.
	defb 054h		;15e2	54		T
	defb 04fh		;15e3	4f		O
	defb 0e1h		;15e4	e1		.
	defb 0f9h		;15e5	f9		.
	defb 020h		;15e6	20		 
	defb 0d1h		;15e7	d1		.
	defb 0d5h		;15e8	d5		.
	defb 07ah		;15e9	7a		z
	defb 0cfh		;15ea	cf		.
	defb 042h		;15eb	42		B
	defb 07bh		;15ec	7b		{
	defb 0c3h		;15ed	c3		.
	defb 05fh		;15ee	5f		_
	defb 0bdh		;15ef	bd		.
	defb 057h		;15f0	57		W
	defb 00fh		;15f1	0f		.
	defb 0f8h		;15f2	f8		.
	defb 08dh		;15f3	8d		.
	defb 0e6h		;15f4	e6		.
	defb 0c6h		;15f5	c6		.
	defb 030h		;15f6	30		0
	defb 0feh		;15f7	fe		.
	defb 03ah		;15f8	3a		:
	defb 038h		;15f9	38		8
	defb 002h		;15fa	02		.
	defb 0deh		;15fb	de		.
	defb 054h		;15fc	54		T
	defb 007h		;15fd	07		.
	defb 0dfh		;15fe	df		.
	defb 07ah		;15ff	7a		z
	defb 09eh		;1600	9e		.
	defb 052h		;1601	52		R
	defb 0dah		;1602	da		.
	defb 08ch		;1603	8c		.
	defb 0c7h		;1604	c7		.
	defb 0c3h		;1605	c3		.
	defb 07eh		;1606	7e		~
	defb 0fbh		;1607	fb		.
	defb 0a2h		;1608	a2		.
	defb 02fh		;1609	2f		/
	defb 0dfh		;160a	df		.
	defb 08fh		;160b	8f		.
	defb 0a9h		;160c	a9		.
	defb 048h		;160d	48		H
	defb 073h		;160e	73		s
	defb 0d3h		;160f	d3		.
	defb 07eh		;1610	7e		~
	defb 0e4h		;1611	e4		.
	defb 0b9h		;1612	b9		.
	defb 0dfh		;1613	df		.
	defb 009h		;1614	09		.
	defb 0f2h		;1615	f2		.
	defb 0cdh		;1616	cd		.
	defb 089h		;1617	89		.
	defb 02ch		;1618	2c		,
	defb 066h		;1619	66		f
	defb 0f5h		;161a	f5		.
	defb 0afh		;161b	af		.
	defb 0e6h		;161c	e6		.
	defb 0c1h		;161d	c1		.
	defb 029h		;161e	29		)
	defb 025h		;161f	25		%
	defb 0fdh		;1620	fd		.
	defb 0cdh		;1621	cd		.
	defb 024h		;1622	24		$
	defb 08ah		;1623	8a		.
	defb 03eh		;1624	3e		>
	defb 04bh		;1625	4b		K
	defb 0d1h		;1626	d1		.
	defb 053h		;1627	53		S
	defb 094h		;1628	94		.
	defb 01eh		;1629	1e		.
	defb 040h		;162a	40		@
	defb 01eh		;162b	1e		.
	defb 0a7h		;162c	a7		.
	defb 065h		;162d	65		e
	defb 060h		;162e	60		`
	defb 01ah		;162f	1a		.
	defb 075h		;1630	75		u
	defb 01fh		;1631	1f		.
	defb 0c9h		;1632	c9		.
	defb 056h		;1633	56		V
	defb 00eh		;1634	0e		.
	defb 085h		;1635	85		.
	defb 083h		;1636	83		.
	defb 0efh		;1637	ef		.
	defb 0dfh		;1638	df		.
	defb 016h		;1639	16		.
	defb 004h		;163a	04		.
	defb 0d9h		;163b	d9		.
	defb 05ch		;163c	5c		\
	defb 0f6h		;163d	f6		.
	defb 04ch		;163e	4c		L
	defb 077h		;163f	77		w
	defb 023h		;1640	23		#
	defb 0b5h		;1641	b5		.
	defb 077h		;1642	77		w
	defb 002h		;1643	02		.
	defb 07dh		;1644	7d		}
	defb 0c4h		;1645	c4		.
	defb 0fah		;1646	fa		.
	defb 05eh		;1647	5e		^
	defb 007h		;1648	07		.
	defb 0f5h		;1649	f5		.
	defb 0bdh		;164a	bd		.
	defb 008h		;164b	08		.
	defb 0eah		;164c	ea		.
	defb 07bh		;164d	7b		{
	defb 032h		;164e	32		2
	defb 095h		;164f	95		.
	defb 0f7h		;1650	f7		.
	defb 009h		;1651	09		.
	defb 067h		;1652	67		g
	defb 0ffh		;1653	ff		.
	defb 02bh		;1654	2b		+
	defb 04fh		;1655	4f		O
	defb 03eh		;1656	3e		>
	defb 019h		;1657	19		.
	defb 0beh		;1658	be		.
	defb 0c8h		;1659	c8		.
	defb 0cch		;165a	cc		.
	defb 00fh		;165b	0f		.
	defb 020h		;165c	20		 
	defb 080h		;165d	80		.
	defb 0b9h		;165e	b9		.
	defb 031h		;165f	31		1
	defb 0f9h		;1660	f9		.
	defb 046h		;1661	46		F
	defb 04dh		;1662	4d		M
	defb 0e3h		;1663	e3		.
	defb 077h		;1664	77		w
	defb 0dbh		;1665	db		.
	defb 0f7h		;1666	f7		.
	defb 05ch		;1667	5c		\
	defb 044h		;1668	44		D
	defb 09fh		;1669	9f		.
	defb 079h		;166a	79		y
	defb 0c0h		;166b	c0		.
	defb 036h		;166c	36		6
	defb 080h		;166d	80		.
	defb 0c1h		;166e	c1		.
	defb 011h		;166f	11		.
	defb 0cbh		;1670	cb		.
	defb 0edh		;1671	ed		.
	defb 0e0h		;1672	e0		.
	defb 07dh		;1673	7d		}
	defb 001h		;1674	01		.
	defb 010h		;1675	10		.
	defb 0d1h		;1676	d1		.
	defb 082h		;1677	82		.
	defb 0deh		;1678	de		.
	defb 09eh		;1679	9e		.
	defb 090h		;167a	90		.
	defb 0deh		;167b	de		.
	defb 09eh		;167c	9e		.
	defb 08ah		;167d	8a		.
	defb 0eah		;167e	ea		.
	defb 0e7h		;167f	e7		.
	defb 026h		;1680	26		&
	defb 0c2h		;1681	c2		.
	defb 016h		;1682	16		.
	defb 074h		;1683	74		t
	defb 08dh		;1684	8d		.
	defb 010h		;1685	10		.
	defb 0a7h		;1686	a7		.
	defb 0f3h		;1687	f3		.
	defb 05fh		;1688	5f		_
	defb 023h		;1689	23		#
	defb 0b7h		;168a	b7		.
	defb 06fh		;168b	6f		o
	defb 0c4h		;168c	c4		.
	defb 0b3h		;168d	b3		.
	defb 088h		;168e	88		.
	defb 010h		;168f	10		.
	defb 0f4h		;1690	f4		.
	defb 0cdh		;1691	cd		.
	defb 01ah		;1692	1a		.
	defb 0e8h		;1693	e8		.
	defb 0c1h		;1694	c1		.
	defb 0c4h		;1695	c4		.
	defb 001h		;1696	01		.
	defb 025h		;1697	25		%
	defb 02eh		;1698	2e		.
	defb 040h		;1699	40		@
	defb 0ffh		;169a	ff		.
	defb 0feh		;169b	fe		.
	defb 0bdh		;169c	bd		.
	defb 0f1h		;169d	f1		.
	defb 0afh		;169e	af		.
	defb 03dh		;169f	3d		=
	defb 05dh		;16a0	5d		]
	defb 0edh		;16a1	ed		.
	defb 0a0h		;16a2	a0		.
	defb 02dh		;16a3	2d		-
	defb 06eh		;16a4	6e		n
	defb 02ch		;16a5	2c		,
	defb 0c8h		;16a6	c8		.
	defb 06fh		;16a7	6f		o
	defb 037h		;16a8	37		7
	defb 018h		;16a9	18		.
	defb 0f6h		;16aa	f6		.
	defb 0f2h		;16ab	f2		.
	defb 0c2h		;16ac	c2		.
	defb 0b3h		;16ad	b3		.
	defb 091h		;16ae	91		.
	defb 0afh		;16af	af		.
	defb 0beh		;16b0	be		.
	defb 04ch		;16b1	4c		L
	defb 0bch		;16b2	bc		.
	defb 033h		;16b3	33		3
	defb 02ch		;16b4	2c		,
	defb 020h		;16b5	20		 
	defb 039h		;16b6	39		9
	defb 0d3h		;16b7	d3		.
	defb 0a6h		;16b8	a6		.
	defb 011h		;16b9	11		.
	defb 0b9h		;16ba	b9		.
	defb 0b9h		;16bb	b9		.
	defb 02eh		;16bc	2e		.
	defb 0a3h		;16bd	a3		.
	defb 095h		;16be	95		.
	defb 01eh		;16bf	1e		.
	defb 003h		;16c0	03		.
	defb 064h		;16c1	64		d
	defb 015h		;16c2	15		.
	defb 09fh		;16c3	9f		.
	defb 0f7h		;16c4	f7		.
	defb 088h		;16c5	88		.
	defb 00ch		;16c6	0c		.
	defb 0d8h		;16c7	d8		.
	defb 0c6h		;16c8	c6		.
	defb 064h		;16c9	64		d
	defb 046h		;16ca	46		F
	defb 080h		;16cb	80		.
	defb 030h		;16cc	30		0
	defb 03fh		;16cd	3f		?
	defb 081h		;16ce	81		.
	defb 0d8h		;16cf	d8		.
	defb 0c0h		;16d0	c0		.
	defb 025h		;16d1	25		%
	defb 0b7h		;16d2	b7		.
	defb 003h		;16d3	03		.
	defb 0f5h		;16d4	f5		.
	defb 021h		;16d5	21		!
	defb 033h		;16d6	33		3
	defb 0f3h		;16d7	f3		.
	defb 06dh		;16d8	6d		m
	defb 00eh		;16d9	0e		.
	defb 0cch		;16da	cc		.
	defb 03dh		;16db	3d		=
	defb 0cah		;16dc	ca		.
	defb 0a1h		;16dd	a1		.
	defb 0d7h		;16de	d7		.
	defb 04ah		;16df	4a		J
	defb 0cdh		;16e0	cd		.
	defb 037h		;16e1	37		7
	defb 0c9h		;16e2	c9		.
	defb 016h		;16e3	16		.
	defb 06fh		;16e4	6f		o
	defb 0f3h		;16e5	f3		.
	defb 01dh		;16e6	1d		.
	defb 06eh		;16e7	6e		n
	defb 08dh		;16e8	8d		.
	defb 06bh		;16e9	6b		k
	defb 0a9h		;16ea	a9		.
	defb 0c0h		;16eb	c0		.
	defb 098h		;16ec	98		.
	defb 045h		;16ed	45		E
	defb 02eh		;16ee	2e		.
	defb 004h		;16ef	04		.
	defb 0bah		;16f0	ba		.
	defb 0ech		;16f1	ec		.
	defb 0f1h		;16f2	f1		.
	defb 032h		;16f3	32		2
	defb 06eh		;16f4	6e		n
	defb 0f2h		;16f5	f2		.
	defb 03ah		;16f6	3a		:
	defb 0ebh		;16f7	eb		.
	defb 07eh		;16f8	7e		~
	defb 0eeh		;16f9	ee		.
	defb 09eh		;16fa	9e		.
	defb 0ceh		;16fb	ce		.
	defb 0feh		;16fc	fe		.
	defb 0d0h		;16fd	d0		.
	defb 093h		;16fe	93		.
	defb 0f3h		;16ff	f3		.
	defb 065h		;1700	65		e
	defb 0fdh		;1701	fd		.
	defb 0b7h		;1702	b7		.
	defb 022h		;1703	22		"
	defb 0f3h		;1704	f3		.
	defb 0feh		;1705	fe		.
	defb 04bh		;1706	4b		K
	defb 009h		;1707	09		.
	defb 0f2h		;1708	f2		.
	defb 0f4h		;1709	f4		.
	defb 0f4h		;170a	f4		.
	defb 0f4h		;170b	f4		.
	defb 00ch		;170c	0c		.
	defb 0f6h		;170d	f6		.
	defb 0b7h		;170e	b7		.
	defb 0e2h		;170f	e2		.
	defb 021h		;1710	21		!
	defb 068h		;1711	68		h
	defb 089h		;1712	89		.
	defb 018h		;1713	18		.
	defb 012h		;1714	12		.
	defb 09bh		;1715	9b		.
	defb 06dh		;1716	6d		m
	defb 072h		;1717	72		r
	defb 076h		;1718	76		v
	defb 09fh		;1719	9f		.
	defb 00dh		;171a	0d		.
	defb 07ch		;171b	7c		|
	defb 008h		;171c	08		.
	defb 09bh		;171d	9b		.
	defb 06dh		;171e	6d		m
	defb 086h		;171f	86		.
	defb 08bh		;1720	8b		.
	defb 087h		;1721	87		.
	defb 003h		;1722	03		.
	defb 090h		;1723	90		.
	defb 011h		;1724	11		.
	defb 09ah		;1725	9a		.
	defb 01fh		;1726	1f		.
	defb 0b5h		;1727	b5		.
	defb 001h		;1728	01		.
	defb 00ah		;1729	0a		.
	defb 072h		;172a	72		r
	defb 0bbh		;172b	bb		.
	defb 09bh		;172c	9b		.
	defb 0ffh		;172d	ff		.
	defb 0cdh		;172e	cd		.
	defb 0bah		;172f	ba		.
	defb 0c8h		;1730	c8		.
	defb 0bch		;1731	bc		.
	defb 0cch		;1732	cc		.
	defb 0b9h		;1733	b9		.
	defb 0cbh		;1734	cb		.
	defb 0cah		;1735	ca		.
	defb 0dah		;1736	da		.
	defb 0bfh		;1737	bf		.
	defb 027h		;1738	27		'
	defb 0c0h		;1739	c0		.
	defb 0b3h		;173a	b3		.
	defb 0f3h		;173b	f3		.
	defb 0d9h		;173c	d9		.
	defb 0c3h		;173d	c3		.
	defb 0b4h		;173e	b4		.
	defb 0c2h		;173f	c2		.
	defb 0c1h		;1740	c1		.
	defb 0c4h		;1741	c4		.
	defb 0ech		;1742	ec		.
	defb 0cfh		;1743	cf		.
	defb 0bah		;1744	ba		.
	defb 0c7h		;1745	c7		.
	defb 0b6h		;1746	b6		.
	defb 0d1h		;1747	d1		.
	defb 0c1h		;1748	c1		.
	defb 03dh		;1749	3d		=
	defb 0fch		;174a	fc		.
	defb 023h		;174b	23		#
	defb 08eh		;174c	8e		.
	defb 06dh		;174d	6d		m
	defb 0ebh		;174e	eb		.
	defb 0c2h		;174f	c2		.
	defb 0cfh		;1750	cf		.
	defb 0cch		;1751	cc		.
	defb 066h		;1752	66		f
	defb 09fh		;1753	9f		.
	defb 038h		;1754	38		8
	defb 011h		;1755	11		.
	defb 0aeh		;1756	ae		.
	defb 0d8h		;1757	d8		.
	defb 00eh		;1758	0e		.
	defb 08eh		;1759	8e		.
	defb 0feh		;175a	fe		.
	defb 096h		;175b	96		.
	defb 0feh		;175c	fe		.
	defb 0c1h		;175d	c1		.
	defb 0a4h		;175e	a4		.
	defb 089h		;175f	89		.
	defb 01eh		;1760	1e		.
	defb 024h		;1761	24		$
	defb 03ah		;1762	3a		:
	defb 0cbh		;1763	cb		.
	defb 084h		;1764	84		.
	defb 015h		;1765	15		.
	defb 0b6h		;1766	b6		.
	defb 014h		;1767	14		.
	defb 01eh		;1768	1e		.
	defb 0c3h		;1769	c3		.
	defb 093h		;176a	93		.
	defb 06fh		;176b	6f		o
	defb 0bah		;176c	ba		.
	defb 036h		;176d	36		6
	defb 07eh		;176e	7e		~
	defb 0a9h		;176f	a9		.
	defb 028h		;1770	28		(
	defb 0ebh		;1771	eb		.
	defb 0cdh		;1772	cd		.
	defb 007h		;1773	07		.
	defb 006h		;1774	06		.
	defb 014h		;1775	14		.
	defb 0ffh		;1776	ff		.
	defb 0d1h		;1777	d1		.
	defb 0a7h		;1778	a7		.
	defb 0c9h		;1779	c9		.
	defb 03eh		;177a	3e		>
	defb 010h		;177b	10		.
	defb 0e7h		;177c	e7		.
	defb 0c3h		;177d	c3		.
	defb 00fh		;177e	0f		.
	defb 07eh		;177f	7e		~
	defb 085h		;1780	85		.
	defb 08fh		;1781	8f		.
	defb 0feh		;1782	fe		.
	defb 020h		;1783	20		 
	defb 00ch		;1784	0c		.
	defb 023h		;1785	23		#
	defb 060h		;1786	60		`
	defb 056h		;1787	56		V
	defb 02bh		;1788	2b		+
	defb 005h		;1789	05		.
	defb 0f3h		;178a	f3		.
	defb 08fh		;178b	8f		.
	defb 010h		;178c	10		.
	defb 0f0h		;178d	f0		.
	defb 0c9h		;178e	c9		.
	defb 0c5h		;178f	c5		.
	defb 05eh		;1790	5e		^
	defb 065h		;1791	65		e
	defb 0c8h		;1792	c8		.
	defb 0beh		;1793	be		.
	defb 0edh		;1794	ed		.
	defb 0fdh		;1795	fd		.
	defb 07bh		;1796	7b		{
	defb 01dh		;1797	1d		.
	defb 0b2h		;1798	b2		.
	defb 091h		;1799	91		.
	defb 0f1h		;179a	f1		.
	defb 00eh		;179b	0e		.
	defb 083h		;179c	83		.
	defb 05fh		;179d	5f		_
	defb 0cdh		;179e	cd		.
	defb 038h		;179f	38		8
	defb 0bbh		;17a0	bb		.
	defb 001h		;17a1	01		.
	defb 082h		;17a2	82		.
	defb 00ch		;17a3	0c		.
	defb 04ch		;17a4	4c		L
	defb 0b5h		;17a5	b5		.
	defb 0bbh		;17a6	bb		.
	defb 08ch		;17a7	8c		.
	defb 068h		;17a8	68		h
	defb 02bh		;17a9	2b		+
	defb 050h		;17aa	50		P
	defb 0b3h		;17ab	b3		.
	defb 03dh		;17ac	3d		=
	defb 077h		;17ad	77		w
	defb 03eh		;17ae	3e		>
	defb 08bh		;17af	8b		.
	defb 01eh		;17b0	1e		.
	defb 00fh		;17b1	0f		.
	defb 0dah		;17b2	da		.
	defb 0b5h		;17b3	b5		.
	defb 0e1h		;17b4	e1		.
	defb 0ech		;17b5	ec		.
	defb 0d8h		;17b6	d8		.
	defb 016h		;17b7	16		.
	defb 001h		;17b8	01		.
	defb 002h		;17b9	02		.
	defb 0f0h		;17ba	f0		.
	defb 0e5h		;17bb	e5		.
	defb 010h		;17bc	10		.
	defb 027h		;17bd	27		'
	defb 0cdh		;17be	cd		.
	defb 044h		;17bf	44		D
	defb 08ah		;17c0	8a		.
	defb 0dah		;17c1	da		.
	defb 0b6h		;17c2	b6		.
	defb 0e8h		;17c3	e8		.
	defb 003h		;17c4	03		.
	defb 05ah		;17c5	5a		Z
	defb 099h		;17c6	99		.
	defb 064h		;17c7	64		d
	defb 0c9h		;17c8	c9		.
	defb 0b5h		;17c9	b5		.
	defb 032h		;17ca	32		2
	defb 02ah		;17cb	2a		*
	defb 0bfh		;17cc	bf		.
	defb 077h		;17cd	77		w
	defb 07dh		;17ce	7d		}
	defb 089h		;17cf	89		.
	defb 03eh		;17d0	3e		>
	defb 02fh		;17d1	2f		/
	defb 03ch		;17d2	3c		<
	defb 0edh		;17d3	ed		.
	defb 0cdh		;17d4	cd		.
	defb 0f0h		;17d5	f0		.
	defb 042h		;17d6	42		B
	defb 030h		;17d7	30		0
	defb 0fbh		;17d8	fb		.
	defb 009h		;17d9	09		.
	defb 0cbh		;17da	cb		.
	defb 08dh		;17db	8d		.
	defb 00eh		;17dc	0e		.
	defb 002h		;17dd	02		.
	defb 0feh		;17de	fe		.
	defb 0c8h		;17df	c8		.
	defb 0c2h		;17e0	c2		.
	defb 0c5h		;17e1	c5		.
	defb 09ch		;17e2	9c		.
	defb 0d6h		;17e3	d6		.
	defb 095h		;17e4	95		.
	defb 0c9h		;17e5	c9		.
	defb 0a3h		;17e6	a3		.
	defb 03ah		;17e7	3a		:
	defb 0a2h		;17e8	a2		.
	defb 047h		;17e9	47		G
	defb 053h		;17ea	53		S
	defb 0f5h		;17eb	f5		.
	defb 0f3h		;17ec	f3		.
	defb 025h		;17ed	25		%
	defb 0a9h		;17ee	a9		.
	defb 032h		;17ef	32		2
	defb 05eh		;17f0	5e		^
	defb 048h		;17f1	48		H
	defb 075h		;17f2	75		u
	defb 09dh		;17f3	9d		.
	defb 020h		;17f4	20		 
	defb 0a7h		;17f5	a7		.
	defb 08eh		;17f6	8e		.
	defb 0edh		;17f7	ed		.
	defb 064h		;17f8	64		d
	defb 0a3h		;17f9	a3		.
	defb 0c3h		;17fa	c3		.
	defb 08ah		;17fb	8a		.
	defb 06ch		;17fc	6c		l
	defb 0afh		;17fd	af		.
	defb 0a0h		;17fe	a0		.
	defb 0ech		;17ff	ec		.
	defb 0a8h		;1800	a8		.
	defb 02dh		;1801	2d		-
	defb 097h		;1802	97		.
	defb 0c5h		;1803	c5		.
	defb 09ch		;1804	9c		.
	defb 045h		;1805	45		E
	defb 054h		;1806	54		T
	defb 066h		;1807	66		f
	defb 0f9h		;1808	f9		.
	defb 0a1h		;1809	a1		.
	defb 084h		;180a	84		.
	defb 08fh		;180b	8f		.
	defb 0c5h		;180c	c5		.
	defb 079h		;180d	79		y
	defb 03dh		;180e	3d		=
	defb 032h		;180f	32		2
	defb 0cbh		;1810	cb		.
	defb 044h		;1811	44		D
	defb 07eh		;1812	7e		~
	defb 0b0h		;1813	b0		.
	defb 05ah		;1814	5a		Z
	defb 067h		;1815	67		g
	defb 0e8h		;1816	e8		.
	defb 094h		;1817	94		.
	defb 096h		;1818	96		.
	defb 0dbh		;1819	db		.
	defb 0bah		;181a	ba		.
	defb 063h		;181b	63		c
	defb 0dah		;181c	da		.
	defb 09bh		;181d	9b		.
	defb 0cah		;181e	ca		.
	defb 0dch		;181f	dc		.
	defb 006h		;1820	06		.
	defb 036h		;1821	36		6
	defb 0dbh		;1822	db		.
	defb 09bh		;1823	9b		.
	defb 07ch		;1824	7c		|
	defb 05ch		;1825	5c		\
	defb 0cch		;1826	cc		.
	defb 0c1h		;1827	c1		.
	defb 005h		;1828	05		.
	defb 014h		;1829	14		.
	defb 05ch		;182a	5c		\
	defb 09fh		;182b	9f		.
	defb 0cfh		;182c	cf		.
	defb 096h		;182d	96		.
	defb 0a7h		;182e	a7		.
	defb 07ch		;182f	7c		|
	defb 0c6h		;1830	c6		.
	defb 000h		;1831	00		.
	defb 053h		;1832	53		S
	defb 07eh		;1833	7e		~
	defb 05fh		;1834	5f		_
	defb 069h		;1835	69		i
	defb 0fbh		;1836	fb		.
	defb 0c1h		;1837	c1		.
	defb 05ch		;1838	5c		\
	defb 014h		;1839	14		.
	defb 010h		;183a	10		.
	defb 0e4h		;183b	e4		.
	defb 049h		;183c	49		I
	defb 05bh		;183d	5b		[
	defb 09eh		;183e	9e		.
	defb 0dbh		;183f	db		.
	defb 0f8h		;1840	f8		.
	defb 0c8h		;1841	c8		.
	defb 09fh		;1842	9f		.
	defb 08eh		;1843	8e		.
	defb 048h		;1844	48		H
	defb 0e7h		;1845	e7		.
	defb 035h		;1846	35		5
	defb 061h		;1847	61		a
	defb 005h		;1848	05		.
	defb 00eh		;1849	0e		.
	defb 00bh		;184a	0b		.
	defb 0c8h		;184b	c8		.
	defb 000h		;184c	00		.
	defb 04bh		;184d	4b		K
	defb 08bh		;184e	8b		.
	defb 0a4h		;184f	a4		.
	defb 0ech		;1850	ec		.
	defb 07dh		;1851	7d		}
	defb 0a4h		;1852	a4		.
	defb 028h		;1853	28		(
	defb 008h		;1854	08		.
	defb 03dh		;1855	3d		=
	defb 0bfh		;1856	bf		.
	defb 0e8h		;1857	e8		.
	defb 014h		;1858	14		.
	defb 00bh		;1859	0b		.
	defb 018h		;185a	18		.
	defb 009h		;185b	09		.
	defb 006h		;185c	06		.
	defb 059h		;185d	59		Y
	defb 0f9h		;185e	f9		.
	defb 019h		;185f	19		.
	defb 0fbh		;1860	fb		.
	defb 076h		;1861	76		v
	defb 010h		;1862	10		.
	defb 0fch		;1863	fc		.
	defb 0f3h		;1864	f3		.
	defb 032h		;1865	32		2
	defb 05ch		;1866	5c		\
	defb 075h		;1867	75		u
	defb 064h		;1868	64		d
	defb 0f9h		;1869	f9		.
	defb 02ch		;186a	2c		,
	defb 082h		;186b	82		.
	defb 0c5h		;186c	c5		.
	defb 0b7h		;186d	b7		.
	defb 0a4h		;186e	a4		.
	defb 0f3h		;186f	f3		.
	defb 0f2h		;1870	f2		.
	defb 08fh		;1871	8f		.
	defb 0c4h		;1872	c4		.
	defb 0f6h		;1873	f6		.
	defb 0cdh		;1874	cd		.
	defb 0f6h		;1875	f6		.
	defb 017h		;1876	17		.
	defb 0fdh		;1877	fd		.
	defb 098h		;1878	98		.
	defb 0bbh		;1879	bb		.
	defb 0d9h		;187a	d9		.
	defb 020h		;187b	20		 
	defb 02bh		;187c	2b		+
	defb 04eh		;187d	4e		N
	defb 014h		;187e	14		.
	defb 0c9h		;187f	c9		.
	defb 087h		;1880	87		.
	defb 05eh		;1881	5e		^
	defb 0fbh		;1882	fb		.
	defb 0f9h		;1883	f9		.
	defb 03eh		;1884	3e		>
	defb 0c0h		;1885	c0		.
	defb 0d3h		;1886	d3		.
	defb 0ech		;1887	ec		.
	defb 0bch		;1888	bc		.
	defb 09fh		;1889	9f		.
	defb 08ch		;188a	8c		.
	defb 011h		;188b	11		.
	defb 040h		;188c	40		@
	defb 07dh		;188d	7d		}
	defb 0ebh		;188e	eb		.
	defb 001h		;188f	01		.
	defb 00dh		;1890	0d		.
	defb 03eh		;1891	3e		>
	defb 014h		;1892	14		.
	defb 0d3h		;1893	d3		.
	defb 0d1h		;1894	d1		.
	defb 0f7h		;1895	f7		.
	defb 010h		;1896	10		.
	defb 001h		;1897	01		.
	defb 0a4h		;1898	a4		.
	defb 0ffh		;1899	ff		.
	defb 0afh		;189a	af		.
	defb 09ah		;189b	9a		.
	defb 03fh		;189c	3f		?
	defb 050h		;189d	50		P
	defb 08dh		;189e	8d		.
	defb 0c3h		;189f	c3		.
	defb 0d5h		;18a0	d5		.
	defb 08bh		;18a1	8b		.
	defb 028h		;18a2	28		(
	defb 020h		;18a3	20		 
	defb 0cfh		;18a4	cf		.
	defb 01bh		;18a5	1b		.
	defb 0dbh		;18a6	db		.
	defb 0bbh		;18a7	bb		.
	defb 0d8h		;18a8	d8		.
	defb 010h		;18a9	10		.
	defb 008h		;18aa	08		.
	defb 0f6h		;18ab	f6		.
	defb 0d9h		;18ac	d9		.
	defb 0d2h		;18ad	d2		.
	defb 019h		;18ae	19		.
	defb 09dh		;18af	9d		.
	defb 0d9h		;18b0	d9		.
	defb 006h		;18b1	06		.
	defb 010h		;18b2	10		.
	defb 02bh		;18b3	2b		+
	defb 001h		;18b4	01		.
	defb 035h		;18b5	35		5
	defb 0d6h		;18b6	d6		.
	defb 05fh		;18b7	5f		_
	defb 04bh		;18b8	4b		K
	defb 060h		;18b9	60		`
	defb 08bh		;18ba	8b		.
	defb 038h		;18bb	38		8
	defb 0ebh		;18bc	eb		.
	defb 033h		;18bd	33		3
	defb 076h		;18be	76		v
	defb 07eh		;18bf	7e		~
	defb 0feh		;18c0	fe		.
	defb 0a5h		;18c1	a5		.
	defb 0c9h		;18c2	c9		.
	defb 0e5h		;18c3	e5		.
	defb 0dbh		;18c4	db		.
	defb 0a2h		;18c5	a2		.
	defb 067h		;18c6	67		g
	defb 03bh		;18c7	3b		;
	defb 028h		;18c8	28		(
	defb 06fh		;18c9	6f		o
	defb 0e3h		;18ca	e3		.
	defb 034h		;18cb	34		4
	defb 027h		;18cc	27		'
	defb 050h		;18cd	50		P
	defb 0a2h		;18ce	a2		.
	defb 07ch		;18cf	7c		|
	defb 0d9h		;18d0	d9		.
	defb 0f9h		;18d1	f9		.
	defb 0c3h		;18d2	c3		.
	defb 07ch		;18d3	7c		|
	defb 03eh		;18d4	3e		>
	defb 048h		;18d5	48		H
	defb 0d5h		;18d6	d5		.
	defb 03dh		;18d7	3d		=
	defb 0a7h		;18d8	a7		.
	defb 0cfh		;18d9	cf		.
	defb 001h		;18da	01		.
	defb 080h		;18db	80		.
	defb 076h		;18dc	76		v
	defb 0d1h		;18dd	d1		.
	defb 039h		;18de	39		9
	defb 0cdh		;18df	cd		.
	defb 0b7h		;18e0	b7		.
	defb 0c2h		;18e1	c2		.
	defb 0e9h		;18e2	e9		.
	defb 033h		;18e3	33		3
	defb 078h		;18e4	78		x
	defb 07fh		;18e5	7f		.
	defb 0a7h		;18e6	a7		.
	defb 079h		;18e7	79		y
	defb 0afh		;18e8	af		.
	defb 0c9h		;18e9	c9		.
	defb 0cdh		;18ea	cd		.
	defb 03fh		;18eb	3f		?
	defb 0ffh		;18ec	ff		.
	defb 05bh		;18ed	5b		[
	defb 04ch		;18ee	4c		L
	defb 01ch		;18ef	1c		.
	defb 0a5h		;18f0	a5		.
	defb 042h		;18f1	42		B
	defb 0efh		;18f2	ef		.
	defb 032h		;18f3	32		2
	defb 071h		;18f4	71		q
	defb 0deh		;18f5	de		.
	defb 0ceh		;18f6	ce		.
	defb 088h		;18f7	88		.
	defb 0e3h		;18f8	e3		.
	defb 0bch		;18f9	bc		.
	defb 084h		;18fa	84		.
	defb 039h		;18fb	39		9
	defb 026h		;18fc	26		&
	defb 00ch		;18fd	0c		.
	defb 08ch		;18fe	8c		.
	defb 07ah		;18ff	7a		z
	defb 062h		;1900	62		b
	defb 0a5h		;1901	a5		.
	defb 031h		;1902	31		1
	defb 013h		;1903	13		.
	defb 047h		;1904	47		G
	defb 07bh		;1905	7b		{
	defb 063h		;1906	63		c
	defb 094h		;1907	94		.
	defb 01dh		;1908	1d		.
	defb 05ah		;1909	5a		Z
	defb 033h		;190a	33		3
	defb 03bh		;190b	3b		;
	defb 0bdh		;190c	bd		.
	defb 084h		;190d	84		.
	defb 008h		;190e	08		.
	defb 0c3h		;190f	c3		.
	defb 055h		;1910	55		U
	defb 063h		;1911	63		c
	defb 0adh		;1912	ad		.
	defb 0f6h		;1913	f6		.
	defb 01fh		;1914	1f		.
	defb 09eh		;1915	9e		.
	defb 063h		;1916	63		c
	defb 023h		;1917	23		#
	defb 062h		;1918	62		b
	defb 010h		;1919	10		.
	defb 094h		;191a	94		.
	defb 01fh		;191b	1f		.
	defb 06bh		;191c	6b		k
	defb 063h		;191d	63		c
	defb 0eeh		;191e	ee		.
	defb 0c0h		;191f	c0		.
	defb 0bdh		;1920	bd		.
	defb 0e3h		;1921	e3		.
	defb 0d9h		;1922	d9		.
	defb 080h		;1923	80		.
	defb 0ceh		;1924	ce		.
	defb 098h		;1925	98		.
	defb 01fh		;1926	1f		.
	defb 0e6h		;1927	e6		.
	defb 0efh		;1928	ef		.
	defb 0bch		;1929	bc		.
	defb 073h		;192a	73		s
	defb 01ch		;192b	1c		.
	defb 0e9h		;192c	e9		.
	defb 0f6h		;192d	f6		.
	defb 09ch		;192e	9c		.
	defb 01ah		;192f	1a		.
	defb 0c6h		;1930	c6		.
	defb 0b4h		;1931	b4		.
	defb 0e4h		;1932	e4		.
	defb 08ch		;1933	8c		.
	defb 052h		;1934	52		R
	defb 095h		;1935	95		.
	defb 039h		;1936	39		9
	defb 0c4h		;1937	c4		.
	defb 0a6h		;1938	a6		.
	defb 040h		;1939	40		@
	defb 010h		;193a	10		.
	defb 0e0h		;193b	e0		.
	defb 041h		;193c	41		A
	defb 0d0h		;193d	d0		.
	defb 0e7h		;193e	e7		.
	defb 05bh		;193f	5b		[
	defb 0aeh		;1940	ae		.
	defb 0adh		;1941	ad		.
	defb 090h		;1942	90		.
	defb 071h		;1943	71		q
	defb 00eh		;1944	0e		.
	defb 03dh		;1945	3d		=
	defb 021h		;1946	21		!
	defb 0dfh		;1947	df		.
	defb 0c6h		;1948	c6		.
	defb 009h		;1949	09		.
	defb 0fdh		;194a	fd		.
	defb 0cdh		;194b	cd		.
	defb 0cch		;194c	cc		.
	defb 08ch		;194d	8c		.
	defb 021h		;194e	21		!
	defb 045h		;194f	45		E
	defb 0b6h		;1950	b6		.
	defb 0b8h		;1951	b8		.
	defb 0fdh		;1952	fd		.
	defb 0d2h		;1953	d2		.
	defb 040h		;1954	40		@
	defb 03eh		;1955	3e		>
	defb 004h		;1956	04		.
	defb 03fh		;1957	3f		?
	defb 075h		;1958	75		u
	defb 0a5h		;1959	a5		.
	defb 006h		;195a	06		.
	defb 03bh		;195b	3b		;
	defb 02eh		;195c	2e		.
	defb 0b8h		;195d	b8		.
	defb 011h		;195e	11		.
	defb 080h		;195f	80		.
	defb 034h		;1960	34		4
	defb 069h		;1961	69		i
	defb 0f6h		;1962	f6		.
	defb 04bh		;1963	4b		K
	defb 09dh		;1964	9d		.
	defb 0f0h		;1965	f0		.
	defb 0f4h		;1966	f4		.
	defb 0f2h		;1967	f2		.
	defb 00dh		;1968	0d		.
	defb 005h		;1969	05		.
	defb 0d3h		;196a	d3		.
	defb 00ah		;196b	0a		.
	defb 0f2h		;196c	f2		.
	defb 0efh		;196d	ef		.
	defb 03ch		;196e	3c		<
	defb 032h		;196f	32		2
	defb 021h		;1970	21		!
	defb 010h		;1971	10		.
	defb 08dh		;1972	8d		.
	defb 011h		;1973	11		.
	defb 0efh		;1974	ef		.
	defb 0cbh		;1975	cb		.
	defb 00eh		;1976	0e		.
	defb 008h		;1977	08		.
	defb 0d8h		;1978	d8		.
	defb 0c5h		;1979	c5		.
	defb 0e5h		;197a	e5		.
	defb 0edh		;197b	ed		.
	defb 0a0h		;197c	a0		.
	defb 04bh		;197d	4b		K
	defb 02fh		;197e	2f		/
	defb 0e1h		;197f	e1		.
	defb 049h		;1980	49		I
	defb 0ffh		;1981	ff		.
	defb 029h		;1982	29		)
	defb 0f2h		;1983	f2		.
	defb 0dbh		;1984	db		.
	defb 0edh		;1985	ed		.
	defb 0d2h		;1986	d2		.
	defb 00dh		;1987	0d		.
	defb 020h		;1988	20		 
	defb 0e9h		;1989	e9		.
	defb 0d3h		;198a	d3		.
	defb 013h		;198b	13		.
	defb 0efh		;198c	ef		.
	defb 0c7h		;198d	c7		.
	defb 0c9h		;198e	c9		.
	defb 0d8h		;198f	d8		.
	defb 026h		;1990	26		&
	defb 05ch		;1991	5c		\
	defb 0f5h		;1992	f5		.
	defb 008h		;1993	08		.
	defb 0e5h		;1994	e5		.
	defb 065h		;1995	65		e
	defb 0ddh		;1996	dd		.
	defb 0f5h		;1997	f5		.
	defb 088h		;1998	88		.
	defb 0fch		;1999	fc		.
	defb 0f6h		;199a	f6		.
	defb 0d6h		;199b	d6		.
	defb 06ch		;199c	6c		l
	defb 0a8h		;199d	a8		.
	defb 0afh		;199e	af		.
	defb 0d8h		;199f	d8		.
	defb 0b6h		;19a0	b6		.
	defb 06bh		;19a1	6b		k
	defb 030h		;19a2	30		0
	defb 0f3h		;19a3	f3		.
	defb 054h		;19a4	54		T
	defb 01fh		;19a5	1f		.
	defb 0a8h		;19a6	a8		.
	defb 09fh		;19a7	9f		.
	defb 080h		;19a8	80		.
	defb 0e1h		;19a9	e1		.
	defb 0ech		;19aa	ec		.
	defb 0fch		;19ab	fc		.
	defb 031h		;19ac	31		1
	defb 0f8h		;19ad	f8		.
	defb 07ch		;19ae	7c		|
	defb 0f6h		;19af	f6		.
	defb 087h		;19b0	87		.
	defb 02bh		;19b1	2b		+
	defb 06ah		;19b2	6a		j
	defb 0a8h		;19b3	a8		.
	defb 0f8h		;19b4	f8		.
	defb 081h		;19b5	81		.
	defb 0abh		;19b6	ab		.
	defb 07eh		;19b7	7e		~
	defb 0e7h		;19b8	e7		.
	defb 00ah		;19b9	0a		.
	defb 0cdh		;19ba	cd		.
	defb 0d2h		;19bb	d2		.
	defb 0cbh		;19bc	cb		.
	defb 0cbh		;19bd	cb		.
	defb 0e1h		;19be	e1		.
	defb 0f3h		;19bf	f3		.
	defb 0edh		;19c0	ed		.
	defb 0fch		;19c1	fc		.
	defb 0d9h		;19c2	d9		.
	defb 021h		;19c3	21		!
	defb 01eh		;19c4	1e		.
	defb 089h		;19c5	89		.
	defb 028h		;19c6	28		(
	defb 0afh		;19c7	af		.
	defb 000h		;19c8	00		.
	defb 0d8h		;19c9	d8		.
	defb 0d4h		;19ca	d4		.
	defb 0d5h		;19cb	d5		.
	defb 0f6h		;19cc	f6		.
	defb 0e5h		;19cd	e5		.
	defb 004h		;19ce	04		.
	defb 0d1h		;19cf	d1		.
	defb 0e1h		;19d0	e1		.
	defb 011h		;19d1	11		.
	defb 0d9h		;19d2	d9		.
	defb 001h		;19d3	01		.
	defb 050h		;19d4	50		P
	defb 008h		;19d5	08		.
	defb 0c9h		;19d6	c9		.
	defb 059h		;19d7	59		Y
	defb 053h		;19d8	53		S
	defb 0b8h		;19d9	b8		.
	defb 0b0h		;19da	b0		.
	defb 0f7h		;19db	f7		.
	defb 062h		;19dc	62		b
	defb 0c5h		;19dd	c5		.
	defb 0bdh		;19de	bd		.
	defb 006h		;19df	06		.
	defb 0ffh		;19e0	ff		.
	defb 042h		;19e1	42		B
	defb 07eh		;19e2	7e		~
	defb 0cbh		;19e3	cb		.
	defb 07fh		;19e4	7f		.
	defb 020h		;19e5	20		 
	defb 01dh		;19e6	1d		.
	defb 0e6h		;19e7	e6		.
	defb 00fh		;19e8	0f		.
	defb 047h		;19e9	47		G
	defb 0edh		;19ea	ed		.
	defb 06fh		;19eb	6f		o
	defb 0c6h		;19ec	c6		.
	defb 003h		;19ed	03		.
	defb 04fh		;19ee	4f		O
	defb 023h		;19ef	23		#
	defb 07bh		;19f0	7b		{
	defb 096h		;19f1	96		.
	defb 0f9h		;19f2	f9		.
	defb 066h		;19f3	66		f
	defb 06fh		;19f4	6f		o
	defb 07ah		;19f5	7a		z
	defb 098h		;19f6	98		.
	defb 044h		;19f7	44		D
	defb 0a6h		;19f8	a6		.
	defb 09ch		;19f9	9c		.
	defb 067h		;19fa	67		g
	defb 078h		;19fb	78		x
	defb 0cdh		;19fc	cd		.
	defb 0ffh		;19fd	ff		.
	defb 0cfh		;19fe	cf		.
	defb 060h		;19ff	60		`
	defb 069h		;1a00	69		i
	defb 039h		;1a01	39		9
	defb 018h		;1a02	18		.
	defb 0dfh		;1a03	df		.
	defb 0e6h		;1a04	e6		.
	defb 07fh		;1a05	7f		.
	defb 028h		;1a06	28		(
	defb 019h		;1a07	19		.
	defb 023h		;1a08	23		#
	defb 0f5h		;1a09	f5		.
	defb 077h		;1a0a	77		w
	defb 0ech		;1a0b	ec		.
	defb 09dh		;1a0c	9d		.
	defb 005h		;1a0d	05		.
	defb 04fh		;1a0e	4f		O
	defb 0dfh		;1a0f	df		.
	defb 094h		;1a10	94		.
	defb 0d0h		;1a11	d0		.
	defb 03fh		;1a12	3f		?
	defb 0d8h		;1a13	d8		.
	defb 047h		;1a14	47		G
	defb 07eh		;1a15	7e		~
	defb 023h		;1a16	23		#
	defb 04eh		;1a17	4e		N
	defb 088h		;1a18	88		.
	defb 091h		;1a19	91		.
	defb 012h		;1a1a	12		.
	defb 013h		;1a1b	13		.
	defb 06dh		;1a1c	6d		m
	defb 07bh		;1a1d	7b		{
	defb 094h		;1a1e	94		.
	defb 079h		;1a1f	79		y
	defb 018h		;1a20	18		.
	defb 0c2h		;1a21	c2		.
	defb 031h		;1a22	31		1
	defb 05bh		;1a23	5b		[
	defb 0d8h		;1a24	d8		.
	defb 006h		;1a25	06		.
	defb 003h		;1a26	03		.
	defb 0e1h		;1a27	e1		.
	defb 03bh		;1a28	3b		;
	defb 0f1h		;1a29	f1		.
	defb 077h		;1a2a	77		w
	defb 010h		;1a2b	10		.
	defb 0fah		;1a2c	fa		.
	defb 058h		;1a2d	58		X
	defb 0f3h		;1a2e	f3		.
	defb 0e1h		;1a2f	e1		.
	defb 0fch		;1a30	fc		.
	defb 098h		;1a31	98		.
	defb 00ah		;1a32	0a		.
	defb 00ch		;1a33	0c		.
	defb 06eh		;1a34	6e		n
	defb 08dh		;1a35	8d		.
	defb 0dfh		;1a36	df		.
	defb 0d9h		;1a37	d9		.
	defb 0c4h		;1a38	c4		.
	defb 0e7h		;1a39	e7		.
	defb 081h		;1a3a	81		.
	defb 0fch		;1a3b	fc		.
	defb 0ddh		;1a3c	dd		.
	defb 0cch		;1a3d	cc		.
	defb 0d6h		;1a3e	d6		.
	defb 0b5h		;1a3f	b5		.
	defb 06ah		;1a40	6a		j
	defb 0e7h		;1a41	e7		.
	defb 0f1h		;1a42	f1		.
	defb 01bh		;1a43	1b		.
	defb 0cbh		;1a44	cb		.
	defb 070h		;1a45	70		p
	defb 0f6h		;1a46	f6		.
	defb 081h		;1a47	81		.
	defb 0cdh		;1a48	cd		.
	defb 0cah		;1a49	ca		.
	defb 0ddh		;1a4a	dd		.
	defb 0fah		;1a4b	fa		.
	defb 03ch		;1a4c	3c		<
	defb 0dah		;1a4d	da		.
	defb 0c1h		;1a4e	c1		.
	defb 0aah		;1a4f	aa		.
	defb 0cbh		;1a50	cb		.
	defb 064h		;1a51	64		d
	defb 0e7h		;1a52	e7		.
	defb 0dch		;1a53	dc		.
	defb 071h		;1a54	71		q
	defb 00ah		;1a55	0a		.
	defb 070h		;1a56	70		p
	defb 07eh		;1a57	7e		~
	defb 0d9h		;1a58	d9		.
	defb 065h		;1a59	65		e
	defb 077h		;1a5a	77		w
	defb 030h		;1a5b	30		0
	defb 00dh		;1a5c	0d		.
	defb 073h		;1a5d	73		s
	defb 081h		;1a5e	81		.
	defb 0aeh		;1a5f	ae		.
	defb 0d2h		;1a60	d2		.
	defb 0eeh		;1a61	ee		.
	defb 0c2h		;1a62	c2		.
	defb 027h		;1a63	27		'
	defb 0eah		;1a64	ea		.
	defb 070h		;1a65	70		p
	defb 08dh		;1a66	8d		.
	defb 051h		;1a67	51		Q
	defb 0a5h		;1a68	a5		.
	defb 08fh		;1a69	8f		.
	defb 0ffh		;1a6a	ff		.
	defb 0cfh		;1a6b	cf		.
	defb 0fdh		;1a6c	fd		.
	defb 0ffh		;1a6d	ff		.
	defb 041h		;1a6e	41		A
	defb 0efh		;1a6f	ef		.
	defb 020h		;1a70	20		 
	defb 0f6h		;1a71	f6		.
	defb 082h		;1a72	82		.
	defb 0aah		;1a73	aa		.
	defb 017h		;1a74	17		.
	defb 0feh		;1a75	fe		.
	defb 030h		;1a76	30		0
	defb 077h		;1a77	77		w
	defb 070h		;1a78	70		p
	defb 0f4h		;1a79	f4		.
	defb 010h		;1a7a	10		.
	defb 0efh		;1a7b	ef		.
	defb 071h		;1a7c	71		q
	defb 00ch		;1a7d	0c		.
	defb 099h		;1a7e	99		.
	defb 031h		;1a7f	31		1
	defb 036h		;1a80	36		6
	defb 0e9h		;1a81	e9		.
	defb 014h		;1a82	14		.
	defb 072h		;1a83	72		r
	defb 011h		;1a84	11		.
	defb 0f5h		;1a85	f5		.
	defb 0d3h		;1a86	d3		.
	defb 0c5h		;1a87	c5		.
	defb 040h		;1a88	40		@
	defb 07ch		;1a89	7c		|
	defb 0e0h		;1a8a	e0		.
	defb 09fh		;1a8b	9f		.
	defb 0f5h		;1a8c	f5		.
	defb 0adh		;1a8d	ad		.
	defb 0ddh		;1a8e	dd		.
	defb 0d1h		;1a8f	d1		.
	defb 0cch		;1a90	cc		.
	defb 011h		;1a91	11		.
	defb 09eh		;1a92	9e		.
	defb 040h		;1a93	40		@
	defb 02bh		;1a94	2b		+
	defb 063h		;1a95	63		c
	defb 07ah		;1a96	7a		z
	defb 083h		;1a97	83		.
	defb 072h		;1a98	72		r
	defb 093h		;1a99	93		.
	defb 0eah		;1a9a	ea		.
	defb 0f0h		;1a9b	f0		.
	defb 061h		;1a9c	61		a
	defb 050h		;1a9d	50		P
	defb 0e1h		;1a9e	e1		.
	defb 0ffh		;1a9f	ff		.
	defb 051h		;1aa0	51		Q
	defb 0dah		;1aa1	da		.
	defb 0d8h		;1aa2	d8		.
	defb 0aah		;1aa3	aa		.
	defb 081h		;1aa4	81		.
	defb 0ach		;1aa5	ac		.
	defb 062h		;1aa6	62		b
	defb 025h		;1aa7	25		%
	defb 021h		;1aa8	21		!
	defb 02ch		;1aa9	2c		,
	defb 0ffh		;1aaa	ff		.
	defb 001h		;1aab	01		.
	defb 000h		;1aac	00		.
	defb 0afh		;1aad	af		.
	defb 032h		;1aae	32		2
	defb 010h		;1aaf	10		.
	defb 0eeh		;1ab0	ee		.
	defb 0ffh		;1ab1	ff		.
	defb 023h		;1ab2	23		#
	defb 068h		;1ab3	68		h
	defb 037h		;1ab4	37		7
	defb 09bh		;1ab5	9b		.
	defb 002h		;1ab6	02		.
	defb 06dh		;1ab7	6d		m
	defb 0d2h		;1ab8	d2		.
	defb 011h		;1ab9	11		.
	defb 0ech		;1aba	ec		.
	defb 08dh		;1abb	8d		.
	defb 0e1h		;1abc	e1		.
	defb 0aah		;1abd	aa		.
	defb 037h		;1abe	37		7
	defb 0ffh		;1abf	ff		.
	defb 052h		;1ac0	52		R
	defb 02dh		;1ac1	2d		-
	defb 020h		;1ac2	20		 
	defb 03bh		;1ac3	3b		;
	defb 012h		;1ac4	12		.
	defb 09eh		;1ac5	9e		.
	defb 073h		;1ac6	73		s
	defb 098h		;1ac7	98		.
	defb 0e5h		;1ac8	e5		.
	defb 0beh		;1ac9	be		.
	defb 0c5h		;1aca	c5		.
	defb 060h		;1acb	60		`
	defb 079h		;1acc	79		y
	defb 0f3h		;1acd	f3		.
	defb 0f8h		;1ace	f8		.
	defb 081h		;1acf	81		.
	defb 0cah		;1ad0	ca		.
	defb 0ebh		;1ad1	eb		.
	defb 0aah		;1ad2	aa		.
	defb 0f2h		;1ad3	f2		.
	defb 00ah		;1ad4	0a		.
	defb 0cdh		;1ad5	cd		.
	defb 020h		;1ad6	20		 
	defb 0bfh		;1ad7	bf		.
	defb 050h		;1ad8	50		P
	defb 09bh		;1ad9	9b		.
	defb 0bfh		;1ada	bf		.
	defb 0c0h		;1adb	c0		.
	defb 08dh		;1adc	8d		.
	defb 0dch		;1add	dc		.
	defb 0ffh		;1ade	ff		.
	defb 024h		;1adf	24		$
	defb 064h		;1ae0	64		d
	defb 031h		;1ae1	31		1
	defb 078h		;1ae2	78		x
	defb 0ach		;1ae3	ac		.
	defb 022h		;1ae4	22		"
	defb 003h		;1ae5	03		.
	defb 0e6h		;1ae6	e6		.
	defb 0cbh		;1ae7	cb		.
	defb 070h		;1ae8	70		p
	defb 07ch		;1ae9	7c		|
	defb 0e9h		;1aea	e9		.
	defb 0bch		;1aeb	bc		.
	defb 023h		;1aec	23		#
	defb 0e8h		;1aed	e8		.
	defb 0fch		;1aee	fc		.
	defb 034h		;1aef	34		4
	defb 020h		;1af0	20		 
	defb 082h		;1af1	82		.
	defb 002h		;1af2	02		.
	defb 037h		;1af3	37		7
	defb 074h		;1af4	74		t
	defb 09ch		;1af5	9c		.
	defb 023h		;1af6	23		#
	defb 062h		;1af7	62		b
	defb 014h		;1af8	14		.
	defb 0e2h		;1af9	e2		.
	defb 000h		;1afa	00		.
	defb 0b9h		;1afb	b9		.
	defb 003h		;1afc	03		.
	defb 050h		;1afd	50		P
	defb 070h		;1afe	70		p
	defb 07dh		;1aff	7d		}
	defb 0f6h		;1b00	f6		.
	defb 0aah		;1b01	aa		.
	defb 014h		;1b02	14		.
	defb 00eh		;1b03	0e		.
	defb 082h		;1b04	82		.
	defb 0ddh		;1b05	dd		.
	defb 0f9h		;1b06	f9		.
	defb 0e6h		;1b07	e6		.
	defb 003h		;1b08	03		.
	defb 0b0h		;1b09	b0		.
	defb 060h		;1b0a	60		`
	defb 034h		;1b0b	34		4
	defb 01ah		;1b0c	1a		.
	defb 0dfh		;1b0d	df		.
	defb 0ffh		;1b0e	ff		.
	defb 033h		;1b0f	33		3
	defb 084h		;1b10	84		.
	defb 058h		;1b11	58		X
	defb 02bh		;1b12	2b		+
	defb 037h		;1b13	37		7
	defb 072h		;1b14	72		r
	defb 06dh		;1b15	6d		m
	defb 0f8h		;1b16	f8		.
	defb 0aah		;1b17	aa		.
	defb 070h		;1b18	70		p
	defb 050h		;1b19	50		P
	defb 024h		;1b1a	24		$
	defb 017h		;1b1b	17		.
	defb 074h		;1b1c	74		t
	defb 012h		;1b1d	12		.
	defb 0d2h		;1b1e	d2		.
	defb 0ffh		;1b1f	ff		.
	defb 0bfh		;1b20	bf		.
	defb 019h		;1b21	19		.
	defb 07eh		;1b22	7e		~
	defb 0ddh		;1b23	dd		.
	defb 032h		;1b24	32		2
	defb 0eeh		;1b25	ee		.
	defb 0f9h		;1b26	f9		.
	defb 000h		;1b27	00		.
	defb 082h		;1b28	82		.
	defb 0f4h		;1b29	f4		.
	defb 0e6h		;1b2a	e6		.
	defb 034h		;1b2b	34		4
	defb 04dh		;1b2c	4d		M
	defb 005h		;1b2d	05		.
	defb 014h		;1b2e	14		.
	defb 0aah		;1b2f	aa		.
	defb 002h		;1b30	02		.
	defb 0c9h		;1b31	c9		.
	defb 001h		;1b32	01		.
	defb 004h		;1b33	04		.
	defb 018h		;1b34	18		.
	defb 0b1h		;1b35	b1		.
	defb 0fdh		;1b36	fd		.
	defb 094h		;1b37	94		.
	defb 0ffh		;1b38	ff		.
	defb 006h		;1b39	06		.
	defb 05eh		;1b3a	5e		^
	defb 011h		;1b3b	11		.
	defb 07bh		;1b3c	7b		{
	defb 082h		;1b3d	82		.
	defb 0eah		;1b3e	ea		.
	defb 0adh		;1b3f	ad		.
	defb 015h		;1b40	15		.
	defb 046h		;1b41	46		F
	defb 0c9h		;1b42	c9		.
	defb 0aah		;1b43	aa		.
	defb 083h		;1b44	83		.
	defb 071h		;1b45	71		q
	defb 033h		;1b46	33		3
	defb 0beh		;1b47	be		.
	defb 0f0h		;1b48	f0		.
	defb 0f2h		;1b49	f2		.
	defb 082h		;1b4a	82		.
	defb 0aeh		;1b4b	ae		.
	defb 065h		;1b4c	65		e
	defb 010h		;1b4d	10		.
	defb 0bah		;1b4e	ba		.
	defb 011h		;1b4f	11		.
	defb 01bh		;1b50	1b		.
	defb 093h		;1b51	93		.
	defb 08dh		;1b52	8d		.
	defb 06bh		;1b53	6b		k
	defb 0eah		;1b54	ea		.
	defb 015h		;1b55	15		.
	defb 03eh		;1b56	3e		>
	defb 050h		;1b57	50		P
	defb 081h		;1b58	81		.
	defb 0d7h		;1b59	d7		.
	defb 0ffh		;1b5a	ff		.
	defb 032h		;1b5b	32		2
	defb 030h		;1b5c	30		0
	defb 020h		;1b5d	20		 
	defb 07eh		;1b5e	7e		~
	defb 072h		;1b5f	72		r
	defb 0efh		;1b60	ef		.
	defb 023h		;1b61	23		#
	defb 084h		;1b62	84		.
	defb 0a7h		;1b63	a7		.
	defb 06ch		;1b64	6c		l
	defb 05dh		;1b65	5d		]
	defb 021h		;1b66	21		!
	defb 0ffh		;1b67	ff		.
	defb 075h		;1b68	75		u
	defb 079h		;1b69	79		y
	defb 055h		;1b6a	55		U
	defb 07fh		;1b6b	7f		.
	defb 0e0h		;1b6c	e0		.
	defb 0bbh		;1b6d	bb		.
	defb 089h		;1b6e	89		.
	defb 0ebh		;1b6f	eb		.
	defb 0beh		;1b70	be		.
	defb 05ah		;1b71	5a		Z
	defb 0feh		;1b72	fe		.
	defb 071h		;1b73	71		q
	defb 0b4h		;1b74	b4		.
	defb 09bh		;1b75	9b		.
	defb 0bbh		;1b76	bb		.
	defb 006h		;1b77	06		.
	defb 018h		;1b78	18		.
	defb 021h		;1b79	21		!
	defb 058h		;1b7a	58		X
	defb 072h		;1b7b	72		r
	defb 0e3h		;1b7c	e3		.
	defb 01fh		;1b7d	1f		.
	defb 0ceh		;1b7e	ce		.
	defb 0ffh		;1b7f	ff		.
	defb 022h		;1b80	22		"
	defb 0afh		;1b81	af		.
	defb 082h		;1b82	82		.
	defb 0eeh		;1b83	ee		.
	defb 0e3h		;1b84	e3		.
	defb 0ffh		;1b85	ff		.
	defb 015h		;1b86	15		.
	defb 0beh		;1b87	be		.
	defb 0ceh		;1b88	ce		.
	defb 0aah		;1b89	aa		.
	defb 087h		;1b8a	87		.
	defb 080h		;1b8b	80		.
	defb 000h		;1b8c	00		.
	defb 003h		;1b8d	03		.
	defb 088h		;1b8e	88		.
	defb 011h		;1b8f	11		.
	defb 0dfh		;1b90	df		.
	defb 0fch		;1b91	fc		.
	defb 017h		;1b92	17		.
	defb 0d3h		;1b93	d3		.
	defb 077h		;1b94	77		w
	defb 081h		;1b95	81		.
	defb 071h		;1b96	71		q
	defb 006h		;1b97	06		.
	defb 082h		;1b98	82		.
	defb 019h		;1b99	19		.
	defb 064h		;1b9a	64		d
	defb 0c9h		;1b9b	c9		.
	defb 08bh		;1b9c	8b		.
	defb 0c9h		;1b9d	c9		.
	defb 0bbh		;1b9e	bb		.
	defb 085h		;1b9f	85		.
	defb 0aeh		;1ba0	ae		.
	defb 01bh		;1ba1	1b		.
	defb 003h		;1ba2	03		.
	defb 0ebh		;1ba3	eb		.
	defb 09bh		;1ba4	9b		.
	defb 0e0h		;1ba5	e0		.
	defb 0abh		;1ba6	ab		.
	defb 020h		;1ba7	20		 
	defb 05eh		;1ba8	5e		^
	defb 071h		;1ba9	71		q
	defb 0d6h		;1baa	d6		.
	defb 016h		;1bab	16		.
	defb 03eh		;1bac	3e		>
	defb 034h		;1bad	34		4
	defb 0ddh		;1bae	dd		.
	defb 0a1h		;1baf	a1		.
	defb 0eeh		;1bb0	ee		.
	defb 0c1h		;1bb1	c1		.
	defb 0d1h		;1bb2	d1		.
	defb 0aah		;1bb3	aa		.
	defb 067h		;1bb4	67		g
	defb 077h		;1bb5	77		w
	defb 081h		;1bb6	81		.
	defb 009h		;1bb7	09		.
	defb 003h		;1bb8	03		.
	defb 01fh		;1bb9	1f		.
	defb 084h		;1bba	84		.
	defb 0eah		;1bbb	ea		.
	defb 030h		;1bbc	30		0
	defb 04eh		;1bbd	4e		N
	defb 0d3h		;1bbe	d3		.
	defb 09ch		;1bbf	9c		.
	defb 021h		;1bc0	21		!
	defb 02bh		;1bc1	2b		+
	defb 0d0h		;1bc2	d0		.
	defb 05eh		;1bc3	5e		^
	defb 09ah		;1bc4	9a		.
	defb 0e1h		;1bc5	e1		.
	defb 0b7h		;1bc6	b7		.
	defb 0eeh		;1bc7	ee		.
	defb 013h		;1bc8	13		.
	defb 0b5h		;1bc9	b5		.
	defb 0b9h		;1bca	b9		.
	defb 001h		;1bcb	01		.
	defb 07ah		;1bcc	7a		z
	defb 0a5h		;1bcd	a5		.
	defb 000h		;1bce	00		.
	defb 00bh		;1bcf	0b		.
	defb 0feh		;1bd0	fe		.
	defb 0cdh		;1bd1	cd		.
	defb 081h		;1bd2	81		.
	defb 060h		;1bd3	60		`
	defb 050h		;1bd4	50		P
	defb 00fh		;1bd5	0f		.
	defb 022h		;1bd6	22		"
	defb 006h		;1bd7	06		.
	defb 024h		;1bd8	24		$
	defb 054h		;1bd9	54		T
	defb 099h		;1bda	99		.
	defb 06fh		;1bdb	6f		o
	defb 0cdh		;1bdc	cd		.
	defb 040h		;1bdd	40		@
	defb 0feh		;1bde	fe		.
	defb 074h		;1bdf	74		t
	defb 0e9h		;1be0	e9		.
	defb 0fah		;1be1	fa		.
	defb 06ah		;1be2	6a		j
	defb 05fh		;1be3	5f		_
	defb 027h		;1be4	27		'
	defb 0fch		;1be5	fc		.
	defb 006h		;1be6	06		.
	defb 0d8h		;1be7	d8		.
	defb 060h		;1be8	60		`
	defb 0feh		;1be9	fe		.
	defb 013h		;1bea	13		.
	defb 08fh		;1beb	8f		.
	defb 0bdh		;1bec	bd		.
	defb 003h		;1bed	03		.
	defb 009h		;1bee	09		.
	defb 004h		;1bef	04		.
	defb 00ch		;1bf0	0c		.
	defb 071h		;1bf1	71		q
	defb 002h		;1bf2	02		.
	defb 079h		;1bf3	79		y
	defb 058h		;1bf4	58		X
	defb 0f2h		;1bf5	f2		.
	defb 073h		;1bf6	73		s
	defb 075h		;1bf7	75		u
	defb 068h		;1bf8	68		h
	defb 0d9h		;1bf9	d9		.
	defb 039h		;1bfa	39		9
	defb 0f1h		;1bfb	f1		.
	defb 0f6h		;1bfc	f6		.
	defb 096h		;1bfd	96		.
	defb 0dbh		;1bfe	db		.
	defb 066h		;1bff	66		f
	defb 070h		;1c00	70		p
	defb 071h		;1c01	71		q
	defb 000h		;1c02	00		.
	defb 06fh		;1c03	6f		o
	defb 0fch		;1c04	fc		.
	defb 051h		;1c05	51		Q
	defb 031h		;1c06	31		1
	defb 002h		;1c07	02		.
	defb 078h		;1c08	78		x
	defb 0a3h		;1c09	a3		.
	defb 00bh		;1c0a	0b		.
	defb 01bh		;1c0b	1b		.
	defb 07fh		;1c0c	7f		.
	defb 070h		;1c0d	70		p
	defb 0feh		;1c0e	fe		.
	defb 09ch		;1c0f	9c		.
	defb 0a5h		;1c10	a5		.
	defb 0dah		;1c11	da		.
	defb 034h		;1c12	34		4
	defb 0e3h		;1c13	e3		.
	defb 09fh		;1c14	9f		.
	defb 036h		;1c15	36		6
	defb 0ceh		;1c16	ce		.
	defb 082h		;1c17	82		.
	defb 0b6h		;1c18	b6		.
	defb 0cfh		;1c19	cf		.
	defb 079h		;1c1a	79		y
	defb 0fah		;1c1b	fa		.
	defb 042h		;1c1c	42		B
	defb 032h		;1c1d	32		2
	defb 005h		;1c1e	05		.
	defb 021h		;1c1f	21		!
	defb 0e1h		;1c20	e1		.
	defb 0c1h		;1c21	c1		.
	defb 09ch		;1c22	9c		.
	defb 049h		;1c23	49		I
	defb 0dbh		;1c24	db		.
	defb 072h		;1c25	72		r
	defb 025h		;1c26	25		%
	defb 0b1h		;1c27	b1		.
	defb 03ch		;1c28	3c		<
	defb 0feh		;1c29	fe		.
	defb 0aah		;1c2a	aa		.
	defb 020h		;1c2b	20		 
	defb 07fh		;1c2c	7f		.
	defb 083h		;1c2d	83		.
	defb 018h		;1c2e	18		.
	defb 044h		;1c2f	44		D
	defb 0dch		;1c30	dc		.
	defb 035h		;1c31	35		5
	defb 0dfh		;1c32	df		.
	defb 0ddh		;1c33	dd		.
	defb 0abh		;1c34	ab		.
	defb 0cah		;1c35	ca		.
	defb 0bbh		;1c36	bb		.
	defb 083h		;1c37	83		.
	defb 043h		;1c38	43		C
	defb 038h		;1c39	38		8
	defb 013h		;1c3a	13		.
	defb 02ch		;1c3b	2c		,
	defb 03fh		;1c3c	3f		?
	defb 0eah		;1c3d	ea		.
	defb 070h		;1c3e	70		p
	defb 002h		;1c3f	02		.
	defb 0e6h		;1c40	e6		.
	defb 009h		;1c41	09		.
	defb 023h		;1c42	23		#
	defb 0c8h		;1c43	c8		.
	defb 0b7h		;1c44	b7		.
	defb 0a0h		;1c45	a0		.
	defb 06fh		;1c46	6f		o
	defb 00eh		;1c47	0e		.
	defb 07bh		;1c48	7b		{
	defb 085h		;1c49	85		.
	defb 0cfh		;1c4a	cf		.
	defb 0e0h		;1c4b	e0		.
	defb 000h		;1c4c	00		.
	defb 0ach		;1c4d	ac		.
	defb 0dah		;1c4e	da		.
	defb 0aah		;1c4f	aa		.
	defb 0b9h		;1c50	b9		.
	defb 03eh		;1c51	3e		>
	defb 084h		;1c52	84		.
	defb 003h		;1c53	03		.
	defb 056h		;1c54	56		V
	defb 0ech		;1c55	ec		.
	defb 040h		;1c56	40		@
	defb 0bah		;1c57	ba		.
	defb 08eh		;1c58	8e		.
	defb 066h		;1c59	66		f
	defb 005h		;1c5a	05		.
	defb 03ch		;1c5b	3c		<
	defb 0f2h		;1c5c	f2		.
	defb 05bh		;1c5d	5b		[
	defb 05fh		;1c5e	5f		_
	defb 00ah		;1c5f	0a		.
	defb 00dh		;1c60	0d		.
	defb 09ah		;1c61	9a		.
	defb 079h		;1c62	79		y
	defb 09dh		;1c63	9d		.
	defb 031h		;1c64	31		1
	defb 0e6h		;1c65	e6		.
	defb 0c9h		;1c66	c9		.
	defb 07eh		;1c67	7e		~
	defb 087h		;1c68	87		.
	defb 0dch		;1c69	dc		.
	defb 0e0h		;1c6a	e0		.
	defb 077h		;1c6b	77		w
	defb 078h		;1c6c	78		x
	defb 000h		;1c6d	00		.
	defb 00ah		;1c6e	0a		.
	defb 0f9h		;1c6f	f9		.
	defb 048h		;1c70	48		H
	defb 088h		;1c71	88		.
	defb 0f6h		;1c72	f6		.
	defb 0a1h		;1c73	a1		.
	defb 06eh		;1c74	6e		n
	defb 0ebh		;1c75	eb		.
	defb 089h		;1c76	89		.
	defb 098h		;1c77	98		.
	defb 09fh		;1c78	9f		.
	defb 0b9h		;1c79	b9		.
	defb 053h		;1c7a	53		S
	defb 070h		;1c7b	70		p
	defb 07fh		;1c7c	7f		.
	defb 072h		;1c7d	72		r
	defb 004h		;1c7e	04		.
	defb 081h		;1c7f	81		.
	defb 0ddh		;1c80	dd		.
	defb 021h		;1c81	21		!
	defb 082h		;1c82	82		.
	defb 07bh		;1c83	7b		{
	defb 059h		;1c84	59		Y
	defb 014h		;1c85	14		.
	defb 078h		;1c86	78		x
	defb 0cch		;1c87	cc		.
	defb 0bfh		;1c88	bf		.
	defb 0f6h		;1c89	f6		.
	defb 0ddh		;1c8a	dd		.
	defb 0cbh		;1c8b	cb		.
	defb 0cfh		;1c8c	cf		.
	defb 007h		;1c8d	07		.
	defb 0aah		;1c8e	aa		.
	defb 0d4h		;1c8f	d4		.
	defb 070h		;1c90	70		p
	defb 080h		;1c91	80		.
	defb 0d0h		;1c92	d0		.
	defb 037h		;1c93	37		7
	defb 014h		;1c94	14		.
	defb 06eh		;1c95	6e		n
	defb 0dfh		;1c96	df		.
	defb 005h		;1c97	05		.
	defb 064h		;1c98	64		d
	defb 0f4h		;1c99	f4		.
	defb 0beh		;1c9a	be		.
	defb 0f3h		;1c9b	f3		.
	defb 0a5h		;1c9c	a5		.
	defb 0e5h		;1c9d	e5		.
	defb 05fh		;1c9e	5f		_
	defb 082h		;1c9f	82		.
	defb 033h		;1ca0	33		3
	defb 04bh		;1ca1	4b		K
	defb 0ceh		;1ca2	ce		.
	defb 0f2h		;1ca3	f2		.
	defb 01bh		;1ca4	1b		.
	defb 0d4h		;1ca5	d4		.
	defb 0c3h		;1ca6	c3		.
	defb 001h		;1ca7	01		.
	defb 0edh		;1ca8	ed		.
	defb 000h		;1ca9	00		.
	defb 0efh		;1caa	ef		.
	defb 07bh		;1cab	7b		{
	defb 0d9h		;1cac	d9		.
	defb 098h		;1cad	98		.
	defb 0ffh		;1cae	ff		.
	defb 05ah		;1caf	5a		Z
	defb 0aeh		;1cb0	ae		.
	defb 005h		;1cb1	05		.
	defb 0f3h		;1cb2	f3		.
	defb 084h		;1cb3	84		.
	defb 0abh		;1cb4	ab		.
	defb 0cch		;1cb5	cc		.
	defb 000h		;1cb6	00		.
	defb 0bah		;1cb7	ba		.
	defb 0d8h		;1cb8	d8		.
	defb 0cbh		;1cb9	cb		.
	defb 0a9h		;1cba	a9		.
	defb 0f5h		;1cbb	f5		.
	defb 0d8h		;1cbc	d8		.
	defb 01eh		;1cbd	1e		.
	defb 0c8h		;1cbe	c8		.
	defb 0f5h		;1cbf	f5		.
	defb 093h		;1cc0	93		.
	defb 00bh		;1cc1	0b		.
	defb 0b4h		;1cc2	b4		.
	defb 00eh		;1cc3	0e		.
	defb 04dh		;1cc4	4d		M
	defb 0deh		;1cc5	de		.
	defb 092h		;1cc6	92		.
	defb 0dbh		;1cc7	db		.
	defb 013h		;1cc8	13		.
	defb 0feh		;1cc9	fe		.
	defb 0d1h		;1cca	d1		.
	defb 0bbh		;1ccb	bb		.
	defb 003h		;1ccc	03		.
	defb 005h		;1ccd	05		.
	defb 002h		;1cce	02		.
	defb 083h		;1ccf	83		.
	defb 07ch		;1cd0	7c		|
	defb 0d4h		;1cd1	d4		.
	defb 09bh		;1cd2	9b		.
	defb 010h		;1cd3	10		.
	defb 095h		;1cd4	95		.
	defb 000h		;1cd5	00		.
	defb 094h		;1cd6	94		.
	defb 040h		;1cd7	40		@
	defb 080h		;1cd8	80		.
	defb 082h		;1cd9	82		.
	defb 0a8h		;1cda	a8		.
	defb 050h		;1cdb	50		P
	defb 0d3h		;1cdc	d3		.
	defb 033h		;1cdd	33		3
	defb 017h		;1cde	17		.
	defb 007h		;1cdf	07		.
	defb 081h		;1ce0	81		.
	defb 044h		;1ce1	44		D
	defb 049h		;1ce2	49		I
	defb 09bh		;1ce3	9b		.
	defb 02fh		;1ce4	2f		/
	defb 085h		;1ce5	85		.
	defb 08fh		;1ce6	8f		.
	defb 0e0h		;1ce7	e0		.
	defb 000h		;1ce8	00		.
	defb 096h		;1ce9	96		.
	defb 0f4h		;1cea	f4		.
	defb 06ch		;1ceb	6c		l
	defb 0bah		;1cec	ba		.
	defb 0e9h		;1ced	e9		.
	defb 08fh		;1cee	8f		.
	defb 001h		;1cef	01		.
	defb 082h		;1cf0	82		.
	defb 0adh		;1cf1	ad		.
	defb 0dch		;1cf2	dc		.
	defb 07ch		;1cf3	7c		|
	defb 0d8h		;1cf4	d8		.
	defb 048h		;1cf5	48		H
	defb 0f3h		;1cf6	f3		.
	defb 0b9h		;1cf7	b9		.
	defb 006h		;1cf8	06		.
	defb 0a2h		;1cf9	a2		.
	defb 084h		;1cfa	84		.
	defb 0aah		;1cfb	aa		.
	defb 0b8h		;1cfc	b8		.
	defb 033h		;1cfd	33		3
	defb 018h		;1cfe	18		.
	defb 021h		;1cff	21		!
	defb 06fh		;1d00	6f		o
	defb 0eah		;1d01	ea		.
	defb 011h		;1d02	11		.
	defb 088h		;1d03	88		.
	defb 087h		;1d04	87		.
	defb 053h		;1d05	53		S
	defb 05ah		;1d06	5a		Z
	defb 074h		;1d07	74		t
	defb 0a7h		;1d08	a7		.
	defb 07fh		;1d09	7f		.
	defb 066h		;1d0a	66		f
	defb 09eh		;1d0b	9e		.
	defb 076h		;1d0c	76		v
	defb 074h		;1d0d	74		t
	defb 081h		;1d0e	81		.
	defb 0beh		;1d0f	be		.
	defb 071h		;1d10	71		q
	defb 080h		;1d11	80		.
	defb 08bh		;1d12	8b		.
	defb 003h		;1d13	03		.
	defb 049h		;1d14	49		I
	defb 0f9h		;1d15	f9		.
	defb 05eh		;1d16	5e		^
	defb 076h		;1d17	76		v
	defb 0b0h		;1d18	b0		.
	defb 011h		;1d19	11		.
	defb 001h		;1d1a	01		.
	defb 050h		;1d1b	50		P
	defb 06bh		;1d1c	6b		k
	defb 021h		;1d1d	21		!
	defb 089h		;1d1e	89		.
	defb 0c9h		;1d1f	c9		.
	defb 0ddh		;1d20	dd		.
	defb 006h		;1d21	06		.
	defb 075h		;1d22	75		u
	defb 0b0h		;1d23	b0		.
	defb 030h		;1d24	30		0
	defb 080h		;1d25	80		.
	defb 0cdh		;1d26	cd		.
	defb 0ffh		;1d27	ff		.
	defb 03ah		;1d28	3a		:
	defb 00ch		;1d29	0c		.
	defb 0d3h		;1d2a	d3		.
	defb 083h		;1d2b	83		.
	defb 055h		;1d2c	55		U
	defb 06eh		;1d2d	6e		n
	defb 0bbh		;1d2e	bb		.
	defb 05ch		;1d2f	5c		\
	defb 06bh		;1d30	6b		k
	defb 05ah		;1d31	5a		Z
	defb 0edh		;1d32	ed		.
	defb 054h		;1d33	54		T
	defb 0c4h		;1d34	c4		.
	defb 0cch		;1d35	cc		.
	defb 0cah		;1d36	ca		.
	defb 0a9h		;1d37	a9		.
	defb 035h		;1d38	35		5
	defb 0cfh		;1d39	cf		.
	defb 05fh		;1d3a	5f		_
	defb 00ah		;1d3b	0a		.
	defb 0a8h		;1d3c	a8		.
	defb 071h		;1d3d	71		q
	defb 001h		;1d3e	01		.
	defb 012h		;1d3f	12		.
	defb 002h		;1d40	02		.
	defb 082h		;1d41	82		.
	defb 0e6h		;1d42	e6		.
	defb 068h		;1d43	68		h
	defb 0e7h		;1d44	e7		.
	defb 077h		;1d45	77		w
	defb 084h		;1d46	84		.
	defb 071h		;1d47	71		q
	defb 0f4h		;1d48	f4		.
	defb 0b8h		;1d49	b8		.
	defb 08ch		;1d4a	8c		.
	defb 085h		;1d4b	85		.
	defb 0bch		;1d4c	bc		.
	defb 07eh		;1d4d	7e		~
	defb 0e0h		;1d4e	e0		.
	defb 0cch		;1d4f	cc		.
	defb 0deh		;1d50	de		.
	defb 0eeh		;1d51	ee		.
	defb 0ddh		;1d52	dd		.
	defb 07fh		;1d53	7f		.
	defb 082h		;1d54	82		.
	defb 0dah		;1d55	da		.
	defb 0eah		;1d56	ea		.
	defb 002h		;1d57	02		.
	defb 013h		;1d58	13		.
	defb 031h		;1d59	31		1
	defb 062h		;1d5a	62		b
	defb 05fh		;1d5b	5f		_
	defb 0eah		;1d5c	ea		.
	defb 0b3h		;1d5d	b3		.
	defb 019h		;1d5e	19		.
	defb 056h		;1d5f	56		V
	defb 0e5h		;1d60	e5		.
	defb 0ceh		;1d61	ce		.
	defb 000h		;1d62	00		.
	defb 084h		;1d63	84		.
	defb 005h		;1d64	05		.
	defb 0a4h		;1d65	a4		.
	defb 033h		;1d66	33		3
	defb 039h		;1d67	39		9
	defb 018h		;1d68	18		.
	defb 088h		;1d69	88		.
	defb 081h		;1d6a	81		.
	defb 05eh		;1d6b	5e		^
	defb 077h		;1d6c	77		w
	defb 076h		;1d6d	76		v
	defb 00bh		;1d6e	0b		.
	defb 084h		;1d6f	84		.
	defb 0bah		;1d70	ba		.
	defb 09bh		;1d71	9b		.
	defb 065h		;1d72	65		e
	defb 0d8h		;1d73	d8		.
	defb 03eh		;1d74	3e		>
	defb 071h		;1d75	71		q
	defb 0cah		;1d76	ca		.
	defb 082h		;1d77	82		.
	defb 0a5h		;1d78	a5		.
	defb 01ah		;1d79	1a		.
	defb 000h		;1d7a	00		.
	defb 0ffh		;1d7b	ff		.
	defb 025h		;1d7c	25		%
	defb 083h		;1d7d	83		.
	defb 0edh		;1d7e	ed		.
	defb 0dch		;1d7f	dc		.
	defb 0cdh		;1d80	cd		.
	defb 004h		;1d81	04		.
	defb 036h		;1d82	36		6
	defb 051h		;1d83	51		Q
	defb 000h		;1d84	00		.
	defb 0fch		;1d85	fc		.
	defb 09fh		;1d86	9f		.
	defb 0deh		;1d87	de		.
	defb 06ah		;1d88	6a		j
	defb 074h		;1d89	74		t
	defb 0e8h		;1d8a	e8		.
	defb 058h		;1d8b	58		X
	defb 0fbh		;1d8c	fb		.
	defb 084h		;1d8d	84		.
	defb 035h		;1d8e	35		5
	defb 065h		;1d8f	65		e
	defb 033h		;1d90	33		3
	defb 04ah		;1d91	4a		J
	defb 001h		;1d92	01		.
	defb 0beh		;1d93	be		.
	defb 079h		;1d94	79		y
	defb 086h		;1d95	86		.
	defb 0ddh		;1d96	dd		.
	defb 07eh		;1d97	7e		~
	defb 0bbh		;1d98	bb		.
	defb 0aah		;1d99	aa		.
	defb 0aeh		;1d9a	ae		.
	defb 0f0h		;1d9b	f0		.
	defb 027h		;1d9c	27		'
	defb 06eh		;1d9d	6e		n
	defb 051h		;1d9e	51		Q
	defb 0beh		;1d9f	be		.
	defb 004h		;1da0	04		.
	defb 087h		;1da1	87		.
	defb 06dh		;1da2	6d		m
	defb 075h		;1da3	75		u
	defb 036h		;1da4	36		6
	defb 0ffh		;1da5	ff		.
	defb 006h		;1da6	06		.
	defb 002h		;1da7	02		.
	defb 001h		;1da8	01		.
	defb 074h		;1da9	74		t
	defb 0fch		;1daa	fc		.
	defb 083h		;1dab	83		.
	defb 0dah		;1dac	da		.
	defb 0adh		;1dad	ad		.
	defb 0cch		;1dae	cc		.
	defb 0aeh		;1daf	ae		.
	defb 00eh		;1db0	0e		.
	defb 054h		;1db1	54		T
	defb 04bh		;1db2	4b		K
	defb 007h		;1db3	07		.
	defb 081h		;1db4	81		.
	defb 074h		;1db5	74		t
	defb 0adh		;1db6	ad		.
	defb 0ach		;1db7	ac		.
	defb 00ah		;1db8	0a		.
	defb 011h		;1db9	11		.
	defb 003h		;1dba	03		.
	defb 0a6h		;1dbb	a6		.
	defb 053h		;1dbc	53		S
	defb 096h		;1dbd	96		.
	defb 00dh		;1dbe	0d		.
	defb 0fbh		;1dbf	fb		.
	defb 019h		;1dc0	19		.
	defb 08ah		;1dc1	8a		.
	defb 081h		;1dc2	81		.
	defb 037h		;1dc3	37		7
	defb 008h		;1dc4	08		.
	defb 00bh		;1dc5	0b		.
	defb 0efh		;1dc6	ef		.
	defb 011h		;1dc7	11		.
	defb 030h		;1dc8	30		0
	defb 0cdh		;1dc9	cd		.
	defb 061h		;1dca	61		a
	defb 076h		;1dcb	76		v
	defb 017h		;1dcc	17		.
	defb 0afh		;1dcd	af		.
	defb 0ffh		;1dce	ff		.
	defb 06fh		;1dcf	6f		o
	defb 075h		;1dd0	75		u
	defb 07ch		;1dd1	7c		|
	defb 00ah		;1dd2	0a		.
	defb 06ch		;1dd3	6c		l
	defb 00eh		;1dd4	0e		.
	defb 0cbh		;1dd5	cb		.
	defb 062h		;1dd6	62		b
	defb 080h		;1dd7	80		.
	defb 085h		;1dd8	85		.
	defb 07fh		;1dd9	7f		.
	defb 033h		;1dda	33		3
	defb 0a7h		;1ddb	a7		.
	defb 06dh		;1ddc	6d		m
	defb 0a3h		;1ddd	a3		.
	defb 0d9h		;1dde	d9		.
	defb 089h		;1ddf	89		.
	defb 05ah		;1de0	5a		Z
	defb 046h		;1de1	46		F
	defb 035h		;1de2	35		5
	defb 06ah		;1de3	6a		j
	defb 0bbh		;1de4	bb		.
	defb 073h		;1de5	73		s
	defb 0f3h		;1de6	f3		.
	defb 0beh		;1de7	be		.
	defb 0edh		;1de8	ed		.
	defb 0dah		;1de9	da		.
	defb 073h		;1dea	73		s
	defb 0f7h		;1deb	f7		.
	defb 019h		;1dec	19		.
	defb 008h		;1ded	08		.
	defb 063h		;1dee	63		c
	defb 08eh		;1def	8e		.
	defb 0f1h		;1df0	f1		.
	defb 036h		;1df1	36		6
	defb 0bfh		;1df2	bf		.
	defb 031h		;1df3	31		1
	defb 0bfh		;1df4	bf		.
	defb 008h		;1df5	08		.
	defb 013h		;1df6	13		.
	defb 016h		;1df7	16		.
	defb 0adh		;1df8	ad		.
	defb 000h		;1df9	00		.
	defb 0afh		;1dfa	af		.
	defb 0cbh		;1dfb	cb		.
	defb 088h		;1dfc	88		.
	defb 08fh		;1dfd	8f		.
	defb 0bfh		;1dfe	bf		.
	defb 0e5h		;1dff	e5		.
	defb 0aeh		;1e00	ae		.
	defb 015h		;1e01	15		.
	defb 07ch		;1e02	7c		|
	defb 004h		;1e03	04		.
	defb 078h		;1e04	78		x
	defb 084h		;1e05	84		.
	defb 0e6h		;1e06	e6		.
	defb 066h		;1e07	66		f
	defb 0a4h		;1e08	a4		.
	defb 0ffh		;1e09	ff		.
	defb 065h		;1e0a	65		e
	defb 0d3h		;1e0b	d3		.
	defb 055h		;1e0c	55		U
	defb 0dbh		;1e0d	db		.
	defb 099h		;1e0e	99		.
	defb 082h		;1e0f	82		.
	defb 05bh		;1e10	5b		[
	defb 074h		;1e11	74		t
	defb 069h		;1e12	69		i
	defb 0b2h		;1e13	b2		.
	defb 0e0h		;1e14	e0		.
	defb 0adh		;1e15	ad		.
	defb 090h		;1e16	90		.
	defb 0c9h		;1e17	c9		.
	defb 035h		;1e18	35		5
	defb 0f5h		;1e19	f5		.
	defb 061h		;1e1a	61		a
	defb 072h		;1e1b	72		r
	defb 018h		;1e1c	18		.
	defb 03fh		;1e1d	3f		?
	defb 0d0h		;1e1e	d0		.
	defb 098h		;1e1f	98		.
	defb 034h		;1e20	34		4
	defb 033h		;1e21	33		3
	defb 06eh		;1e22	6e		n
	defb 069h		;1e23	69		i
	defb 0fah		;1e24	fa		.
	defb 026h		;1e25	26		&
	defb 0e6h		;1e26	e6		.
	defb 0f1h		;1e27	f1		.
	defb 052h		;1e28	52		R
	defb 005h		;1e29	05		.
	defb 004h		;1e2a	04		.
	defb 085h		;1e2b	85		.
	defb 0ebh		;1e2c	eb		.
	defb 0eeh		;1e2d	ee		.
	defb 0f4h		;1e2e	f4		.
	defb 0e5h		;1e2f	e5		.
	defb 040h		;1e30	40		@
	defb 07ah		;1e31	7a		z
	defb 00ah		;1e32	0a		.
	defb 0e5h		;1e33	e5		.
	defb 005h		;1e34	05		.
	defb 0bdh		;1e35	bd		.
	defb 0aah		;1e36	aa		.
	defb 0beh		;1e37	be		.
	defb 072h		;1e38	72		r
	defb 0d2h		;1e39	d2		.
	defb 0e7h		;1e3a	e7		.
	defb 0f7h		;1e3b	f7		.
	defb 012h		;1e3c	12		.
	defb 00eh		;1e3d	0e		.
	defb 057h		;1e3e	57		W
	defb 04ch		;1e3f	4c		L
	defb 071h		;1e40	71		q
	defb 096h		;1e41	96		.
	defb 075h		;1e42	75		u
	defb 074h		;1e43	74		t
	defb 082h		;1e44	82		.
	defb 009h		;1e45	09		.
	defb 0c5h		;1e46	c5		.
	defb 053h		;1e47	53		S
	defb 0c1h		;1e48	c1		.
	defb 081h		;1e49	81		.
	defb 0ebh		;1e4a	eb		.
	defb 0e3h		;1e4b	e3		.
	defb 033h		;1e4c	33		3
	defb 082h		;1e4d	82		.
	defb 099h		;1e4e	99		.
	defb 09ah		;1e4f	9a		.
	defb 0dfh		;1e50	df		.
	defb 0bbh		;1e51	bb		.
	defb 019h		;1e52	19		.
	defb 008h		;1e53	08		.
	defb 077h		;1e54	77		w
	defb 07ch		;1e55	7c		|
	defb 015h		;1e56	15		.
	defb 0feh		;1e57	fe		.
	defb 073h		;1e58	73		s
	defb 080h		;1e59	80		.
	defb 02eh		;1e5a	2e		.
	defb 069h		;1e5b	69		i
	defb 007h		;1e5c	07		.
	defb 09bh		;1e5d	9b		.
	defb 050h		;1e5e	50		P
	defb 09eh		;1e5f	9e		.
	defb 0bbh		;1e60	bb		.
	defb 05bh		;1e61	5b		[
	defb 0feh		;1e62	fe		.
	defb 05eh		;1e63	5e		^
	defb 03fh		;1e64	3f		?
	defb 0feh		;1e65	fe		.
	defb 0edh		;1e66	ed		.
	defb 0e0h		;1e67	e0		.
	defb 0ddh		;1e68	dd		.
	defb 086h		;1e69	86		.
	defb 0d4h		;1e6a	d4		.
	defb 066h		;1e6b	66		f
	defb 067h		;1e6c	67		g
	defb 033h		;1e6d	33		3
	defb 035h		;1e6e	35		5
	defb 075h		;1e6f	75		u
	defb 0f9h		;1e70	f9		.
	defb 09bh		;1e71	9b		.
	defb 01ah		;1e72	1a		.
	defb 0d2h		;1e73	d2		.
	defb 0c0h		;1e74	c0		.
	defb 0bah		;1e75	ba		.
	defb 0ddh		;1e76	dd		.
	defb 0ebh		;1e77	eb		.
	defb 054h		;1e78	54		T
	defb 000h		;1e79	00		.
	defb 03ah		;1e7a	3a		:
	defb 0f6h		;1e7b	f6		.
	defb 09bh		;1e7c	9b		.
	defb 0bfh		;1e7d	bf		.
	defb 097h		;1e7e	97		.
	defb 074h		;1e7f	74		t
	defb 080h		;1e80	80		.
	defb 03fh		;1e81	3f		?
	defb 060h		;1e82	60		`
	defb 082h		;1e83	82		.
	defb 055h		;1e84	55		U
	defb 056h		;1e85	56		V
	defb 0c1h		;1e86	c1		.
	defb 064h		;1e87	64		d
	defb 0fch		;1e88	fc		.
	defb 083h		;1e89	83		.
	defb 004h		;1e8a	04		.
	defb 0e0h		;1e8b	e0		.
	defb 088h		;1e8c	88		.
	defb 053h		;1e8d	53		S
	defb 031h		;1e8e	31		1
	defb 0cbh		;1e8f	cb		.
	defb 0bbh		;1e90	bb		.
	defb 0eeh		;1e91	ee		.
	defb 068h		;1e92	68		h
	defb 031h		;1e93	31		1
	defb 0ddh		;1e94	dd		.
	defb 0dbh		;1e95	db		.
	defb 00bh		;1e96	0b		.
	defb 073h		;1e97	73		s
	defb 0d8h		;1e98	d8		.
	defb 09ch		;1e99	9c		.
	defb 0dah		;1e9a	da		.
	defb 084h		;1e9b	84		.
	defb 03fh		;1e9c	3f		?
	defb 014h		;1e9d	14		.
	defb 080h		;1e9e	80		.
	defb 04fh		;1e9f	4f		O
	defb 0d2h		;1ea0	d2		.
	defb 01dh		;1ea1	1d		.
	defb 0eeh		;1ea2	ee		.
	defb 055h		;1ea3	55		U
	defb 078h		;1ea4	78		x
	defb 053h		;1ea5	53		S
	defb 0dfh		;1ea6	df		.
	defb 04eh		;1ea7	4e		N
	defb 050h		;1ea8	50		P
	defb 04ah		;1ea9	4a		J
	defb 0d6h		;1eaa	d6		.
	defb 0f5h		;1eab	f5		.
	defb 038h		;1eac	38		8
	defb 03fh		;1ead	3f		?
	defb 086h		;1eae	86		.
	defb 005h		;1eaf	05		.
	defb 055h		;1eb0	55		U
	defb 0bdh		;1eb1	bd		.
	defb 0e9h		;1eb2	e9		.
	defb 0c0h		;1eb3	c0		.
	defb 039h		;1eb4	39		9
	defb 078h		;1eb5	78		x
	defb 074h		;1eb6	74		t
	defb 0cfh		;1eb7	cf		.
	defb 0e0h		;1eb8	e0		.
	defb 05ch		;1eb9	5c		\
	defb 044h		;1eba	44		D
	defb 0beh		;1ebb	be		.
	defb 005h		;1ebc	05		.
	defb 073h		;1ebd	73		s
	defb 062h		;1ebe	62		b
	defb 0a9h		;1ebf	a9		.
	defb 00fh		;1ec0	0f		.
	defb 0e5h		;1ec1	e5		.
	defb 001h		;1ec2	01		.
	defb 091h		;1ec3	91		.
	defb 089h		;1ec4	89		.
	defb 0cch		;1ec5	cc		.
	defb 0cah		;1ec6	ca		.
	defb 0abh		;1ec7	ab		.
	defb 0c3h		;1ec8	c3		.
	defb 033h		;1ec9	33		3
	defb 0bah		;1eca	ba		.
	defb 0dah		;1ecb	da		.
	defb 03ah		;1ecc	3a		:
	defb 0fah		;1ecd	fa		.
	defb 04fh		;1ece	4f		O
	defb 0e4h		;1ecf	e4		.
	defb 003h		;1ed0	03		.
	defb 0eeh		;1ed1	ee		.
	defb 075h		;1ed2	75		u
	defb 079h		;1ed3	79		y
	defb 016h		;1ed4	16		.
	defb 085h		;1ed5	85		.
	defb 055h		;1ed6	55		U
	defb 0cbh		;1ed7	cb		.
	defb 0b4h		;1ed8	b4		.
	defb 08eh		;1ed9	8e		.
	defb 079h		;1eda	79		y
	defb 006h		;1edb	06		.
	defb 050h		;1edc	50		P
	defb 0ech		;1edd	ec		.
	defb 0c9h		;1ede	c9		.
	defb 0aah		;1edf	aa		.
	defb 0c8h		;1ee0	c8		.
	defb 03fh		;1ee1	3f		?
	defb 06eh		;1ee2	6e		n
	defb 000h		;1ee3	00		.
	defb 07ch		;1ee4	7c		|
	defb 01fh		;1ee5	1f		.
	defb 0e5h		;1ee6	e5		.
	defb 017h		;1ee7	17		.
	defb 0cfh		;1ee8	cf		.
	defb 088h		;1ee9	88		.
	defb 0beh		;1eea	be		.
	defb 0f9h		;1eeb	f9		.
	defb 037h		;1eec	37		7
	defb 053h		;1eed	53		S
	defb 03fh		;1eee	3f		?
	defb 0aeh		;1eef	ae		.
	defb 0aah		;1ef0	aa		.
	defb 033h		;1ef1	33		3
	defb 060h		;1ef2	60		`
	defb 081h		;1ef3	81		.
	defb 083h		;1ef4	83		.
	defb 034h		;1ef5	34		4
	defb 078h		;1ef6	78		x
	defb 0e4h		;1ef7	e4		.
	defb 0a5h		;1ef8	a5		.
	defb 0e6h		;1ef9	e6		.
	defb 055h		;1efa	55		U
	defb 080h		;1efb	80		.
	defb 0a6h		;1efc	a6		.
	defb 07ch		;1efd	7c		|
	defb 0b9h		;1efe	b9		.
	defb 05bh		;1eff	5b		[
	defb 0bbh		;1f00	bb		.
	defb 033h		;1f01	33		3
	defb 059h		;1f02	59		Y
	defb 0d6h		;1f03	d6		.
	defb 06bh		;1f04	6b		k
	defb 0ffh		;1f05	ff		.
	defb 01fh		;1f06	1f		.
	defb 0d9h		;1f07	d9		.
	defb 0edh		;1f08	ed		.
	defb 01fh		;1f09	1f		.
	defb 0dah		;1f0a	da		.
	defb 030h		;1f0b	30		0
	defb 07eh		;1f0c	7e		~
	defb 00dh		;1f0d	0d		.
	defb 049h		;1f0e	49		I
	defb 08dh		;1f0f	8d		.
	defb 0b9h		;1f10	b9		.
	defb 06fh		;1f11	6f		o
	defb 096h		;1f12	96		.
	defb 099h		;1f13	99		.
	defb 0beh		;1f14	be		.
	defb 097h		;1f15	97		.
	defb 0c7h		;1f16	c7		.
	defb 0f3h		;1f17	f3		.
	defb 0cah		;1f18	ca		.
	defb 0aah		;1f19	aa		.
	defb 0b2h		;1f1a	b2		.
	defb 033h		;1f1b	33		3
	defb 08dh		;1f1c	8d		.
	defb 02fh		;1f1d	2f		/
	defb 07ah		;1f1e	7a		z
	defb 05bh		;1f1f	5b		[
	defb 081h		;1f20	81		.
	defb 039h		;1f21	39		9
	defb 07dh		;1f22	7d		}
	defb 06fh		;1f23	6f		o
	defb 0a3h		;1f24	a3		.
	defb 09bh		;1f25	9b		.
	defb 070h		;1f26	70		p
	defb 0d3h		;1f27	d3		.
	defb 0b6h		;1f28	b6		.
	defb 0bdh		;1f29	bd		.
	defb 055h		;1f2a	55		U
	defb 0cdh		;1f2b	cd		.
	defb 036h		;1f2c	36		6
	defb 07bh		;1f2d	7b		{
	defb 05fh		;1f2e	5f		_
	defb 0d0h		;1f2f	d0		.
	defb 0b3h		;1f30	b3		.
	defb 035h		;1f31	35		5
	defb 0d5h		;1f32	d5		.
	defb 03ch		;1f33	3c		<
	defb 0f8h		;1f34	f8		.
	defb 0beh		;1f35	be		.
	defb 01fh		;1f36	1f		.
	defb 08fh		;1f37	8f		.
	defb 0c2h		;1f38	c2		.
	defb 08fh		;1f39	8f		.
	defb 083h		;1f3a	83		.
	defb 0ddh		;1f3b	dd		.
	defb 0adh		;1f3c	ad		.
	defb 078h		;1f3d	78		x
	defb 040h		;1f3e	40		@
	defb 00bh		;1f3f	0b		.
	defb 037h		;1f40	37		7
	defb 0a8h		;1f41	a8		.
	defb 048h		;1f42	48		H
	defb 00fh		;1f43	0f		.
	defb 0eah		;1f44	ea		.
	defb 0ebh		;1f45	eb		.
	defb 0cbh		;1f46	cb		.
	defb 0cdh		;1f47	cd		.
	defb 0aah		;1f48	aa		.
	defb 0deh		;1f49	de		.
	defb 067h		;1f4a	67		g
	defb 017h		;1f4b	17		.
	defb 000h		;1f4c	00		.
	defb 033h		;1f4d	33		3
	defb 0e6h		;1f4e	e6		.
	defb 0abh		;1f4f	ab		.
	defb 093h		;1f50	93		.
	defb 0dah		;1f51	da		.
	defb 07bh		;1f52	7b		{
	defb 0bbh		;1f53	bb		.
	defb 050h		;1f54	50		P
	defb 0d5h		;1f55	d5		.
	defb 074h		;1f56	74		t
	defb 06bh		;1f57	6b		k
	defb 0dfh		;1f58	df		.
	defb 034h		;1f59	34		4
	defb 0d4h		;1f5a	d4		.
	defb 059h		;1f5b	59		Y
	defb 0eeh		;1f5c	ee		.
	defb 0d6h		;1f5d	d6		.
	defb 01bh		;1f5e	1b		.
	defb 002h		;1f5f	02		.
	defb 00eh		;1f60	0e		.
	defb 06dh		;1f61	6d		m
	defb 0c9h		;1f62	c9		.
	defb 0e4h		;1f63	e4		.
	defb 02fh		;1f64	2f		/
	defb 0e4h		;1f65	e4		.
	defb 0c5h		;1f66	c5		.
	defb 0cch		;1f67	cc		.
	defb 083h		;1f68	83		.
	defb 01eh		;1f69	1e		.
	defb 061h		;1f6a	61		a
	defb 029h		;1f6b	29		)
	defb 06fh		;1f6c	6f		o
	defb 026h		;1f6d	26		&
	defb 05eh		;1f6e	5e		^
	defb 0ddh		;1f6f	dd		.
	defb 0eeh		;1f70	ee		.
	defb 0d7h		;1f71	d7		.
	defb 0bdh		;1f72	bd		.
	defb 0abh		;1f73	ab		.
	defb 040h		;1f74	40		@
	defb 000h		;1f75	00		.
	defb 061h		;1f76	61		a
	defb 02eh		;1f77	2e		.
	defb 0bdh		;1f78	bd		.
	defb 076h		;1f79	76		v
	defb 05fh		;1f7a	5f		_
	defb 0ach		;1f7b	ac		.
	defb 018h		;1f7c	18		.
	defb 0d2h		;1f7d	d2		.
	defb 0a6h		;1f7e	a6		.
	defb 08bh		;1f7f	8b		.
	defb 07fh		;1f80	7f		.
	defb 02bh		;1f81	2b		+
	defb 081h		;1f82	81		.
	defb 0bdh		;1f83	bd		.
	defb 017h		;1f84	17		.
	defb 0c4h		;1f85	c4		.
	defb 0dch		;1f86	dc		.
	defb 0cfh		;1f87	cf		.
	defb 0aah		;1f88	aa		.
	defb 06eh		;1f89	6e		n
	defb 0c6h		;1f8a	c6		.
	defb 01fh		;1f8b	1f		.
	defb 0eeh		;1f8c	ee		.
	defb 07eh		;1f8d	7e		~
	defb 0e0h		;1f8e	e0		.
	defb 0eah		;1f8f	ea		.
	defb 0bbh		;1f90	bb		.
	defb 00ch		;1f91	0c		.
	defb 04dh		;1f92	4d		M
	defb 093h		;1f93	93		.
	defb 06ah		;1f94	6a		j
	defb 060h		;1f95	60		`
	defb 084h		;1f96	84		.
	defb 0c3h		;1f97	c3		.
	defb 0deh		;1f98	de		.
	defb 0bfh		;1f99	bf		.
	defb 0f0h		;1f9a	f0		.
	defb 0c9h		;1f9b	c9		.
	defb 0bbh		;1f9c	bb		.
	defb 007h		;1f9d	07		.
	defb 03dh		;1f9e	3d		=
	defb 03bh		;1f9f	3b		;
	defb 012h		;1fa0	12		.
	defb 0fbh		;1fa1	fb		.
	defb 081h		;1fa2	81		.
	defb 0d4h		;1fa3	d4		.
	defb 078h		;1fa4	78		x
	defb 02eh		;1fa5	2e		.
	defb 08bh		;1fa6	8b		.
	defb 0ebh		;1fa7	eb		.
	defb 082h		;1fa8	82		.
	defb 0dah		;1fa9	da		.
	defb 0d1h		;1faa	d1		.
	defb 079h		;1fab	79		y
	defb 07eh		;1fac	7e		~
	defb 0afh		;1fad	af		.
	defb 00fh		;1fae	0f		.
	defb 030h		;1faf	30		0
	defb 000h		;1fb0	00		.
	defb 001h		;1fb1	01		.
	defb 085h		;1fb2	85		.
	defb 007h		;1fb3	07		.
	defb 07fh		;1fb4	7f		.
	defb 05dh		;1fb5	5d		]
	defb 008h		;1fb6	08		.
	defb 0bbh		;1fb7	bb		.
	defb 076h		;1fb8	76		v
	defb 02fh		;1fb9	2f		/
	defb 05eh		;1fba	5e		^
	defb 0aah		;1fbb	aa		.
	defb 0bah		;1fbc	ba		.
	defb 05eh		;1fbd	5e		^
	defb 08fh		;1fbe	8f		.
	defb 053h		;1fbf	53		S
	defb 07ah		;1fc0	7a		z
	defb 0c8h		;1fc1	c8		.
	defb 07fh		;1fc2	7f		.
	defb 0f1h		;1fc3	f1		.
	defb 0d5h		;1fc4	d5		.
	defb 001h		;1fc5	01		.
	defb 080h		;1fc6	80		.
	defb 03dh		;1fc7	3d		=
	defb 00eh		;1fc8	0e		.
	defb 0f0h		;1fc9	f0		.
	defb 083h		;1fca	83		.
	defb 0d4h		;1fcb	d4		.
	defb 044h		;1fcc	44		D
	defb 0e3h		;1fcd	e3		.
	defb 0f0h		;1fce	f0		.
	defb 088h		;1fcf	88		.
	defb 05fh		;1fd0	5f		_
	defb 053h		;1fd1	53		S
	defb 08ah		;1fd2	8a		.
	defb 078h		;1fd3	78		x
	defb 084h		;1fd4	84		.
	defb 0fdh		;1fd5	fd		.
	defb 063h		;1fd6	63		c
	defb 0ach		;1fd7	ac		.
	defb 0eeh		;1fd8	ee		.
	defb 0e7h		;1fd9	e7		.
	defb 055h		;1fda	55		U
	defb 058h		;1fdb	58		X
	defb 04fh		;1fdc	4f		O
	defb 0fbh		;1fdd	fb		.
	defb 0e2h		;1fde	e2		.
	defb 097h		;1fdf	97		.
	defb 0eeh		;1fe0	ee		.
	defb 0e8h		;1fe1	e8		.
	defb 053h		;1fe2	53		S
	defb 06eh		;1fe3	6e		n
	defb 09ch		;1fe4	9c		.
	defb 07eh		;1fe5	7e		~
	defb 0beh		;1fe6	be		.
	defb 0ebh		;1fe7	eb		.
	defb 01fh		;1fe8	1f		.
	defb 0f4h		;1fe9	f4		.
	defb 07ch		;1fea	7c		|
	defb 040h		;1feb	40		@
	defb 036h		;1fec	36		6
	defb 0b9h		;1fed	b9		.
	defb 090h		;1fee	90		.
	defb 0e5h		;1fef	e5		.
	defb 0c5h		;1ff0	c5		.
	defb 007h		;1ff1	07		.
	defb 017h		;1ff2	17		.
	defb 073h		;1ff3	73		s
	defb 072h		;1ff4	72		r
	defb 080h		;1ff5	80		.
	defb 07ch		;1ff6	7c		|
	defb 0f2h		;1ff7	f2		.
	defb 092h		;1ff8	92		.
	defb 05ch		;1ff9	5c		\
	defb 07eh		;1ffa	7e		~
	defb 01fh		;1ffb	1f		.
	defb 071h		;1ffc	71		q
	defb 0a7h		;1ffd	a7		.
	defb 04eh		;1ffe	4e		N
	defb 0e4h		;1fff	e4		.
	defb 016h		;2000	16		.
	defb 053h		;2001	53		S
	defb 01dh		;2002	1d		.
	defb 02ch		;2003	2c		,
	defb 0bdh		;2004	bd		.
	defb 0dfh		;2005	df		.
	defb 064h		;2006	64		d
	defb 044h		;2007	44		D
	defb 0cch		;2008	cc		.
	defb 0d5h		;2009	d5		.
	defb 04fh		;200a	4f		O
	defb 040h		;200b	40		@
	defb 075h		;200c	75		u
	defb 055h		;200d	55		U
	defb 062h		;200e	62		b
	defb 0eah		;200f	ea		.
	defb 087h		;2010	87		.
	defb 0beh		;2011	be		.
	defb 0dah		;2012	da		.
	defb 050h		;2013	50		P
	defb 056h		;2014	56		V
	defb 0bbh		;2015	bb		.
	defb 049h		;2016	49		I
	defb 096h		;2017	96		.
	defb 074h		;2018	74		t
	defb 006h		;2019	06		.
	defb 0f6h		;201a	f6		.
	defb 0f3h		;201b	f3		.
	defb 0f4h		;201c	f4		.
	defb 0d5h		;201d	d5		.
	defb 066h		;201e	66		f
	defb 081h		;201f	81		.
	defb 060h		;2020	60		`
	defb 0cfh		;2021	cf		.
	defb 031h		;2022	31		1
	defb 0bdh		;2023	bd		.
	defb 053h		;2024	53		S
	defb 054h		;2025	54		T
	defb 017h		;2026	17		.
	defb 07eh		;2027	7e		~
	defb 0c4h		;2028	c4		.
	defb 00fh		;2029	0f		.
	defb 001h		;202a	01		.
	defb 03fh		;202b	3f		?
	defb 082h		;202c	82		.
	defb 00eh		;202d	0e		.
	defb 074h		;202e	74		t
	defb 0f3h		;202f	f3		.
	defb 04bh		;2030	4b		K
	defb 0b3h		;2031	b3		.
	defb 0f9h		;2032	f9		.
	defb 042h		;2033	42		B
	defb 08ah		;2034	8a		.
	defb 0dah		;2035	da		.
	defb 0aeh		;2036	ae		.
	defb 066h		;2037	66		f
	defb 06eh		;2038	6e		n
	defb 005h		;2039	05		.
	defb 0ceh		;203a	ce		.
	defb 0bah		;203b	ba		.
	defb 0f5h		;203c	f5		.
	defb 07fh		;203d	7f		.
	defb 09eh		;203e	9e		.
	defb 00dh		;203f	0d		.
	defb 08ah		;2040	8a		.
	defb 05fh		;2041	5f		_
	defb 0b9h		;2042	b9		.
	defb 083h		;2043	83		.
	defb 0a4h		;2044	a4		.
	defb 000h		;2045	00		.
	defb 09bh		;2046	9b		.
	defb 01ch		;2047	1c		.
	defb 040h		;2048	40		@
	defb 0efh		;2049	ef		.
	defb 03eh		;204a	3e		>
	defb 0e8h		;204b	e8		.
	defb 068h		;204c	68		h
	defb 095h		;204d	95		.
	defb 055h		;204e	55		U
	defb 070h		;204f	70		p
	defb 07fh		;2050	7f		.
	defb 0fch		;2051	fc		.
	defb 0bbh		;2052	bb		.
	defb 090h		;2053	90		.
	defb 0b6h		;2054	b6		.
	defb 02dh		;2055	2d		-
	defb 07ch		;2056	7c		|
	defb 03eh		;2057	3e		>
	defb 07eh		;2058	7e		~
	defb 010h		;2059	10		.
	defb 0f3h		;205a	f3		.
	defb 01fh		;205b	1f		.
	defb 045h		;205c	45		E
	defb 083h		;205d	83		.
	defb 000h		;205e	00		.
	defb 088h		;205f	88		.
	defb 08bh		;2060	8b		.
	defb 0eeh		;2061	ee		.
	defb 0b6h		;2062	b6		.
	defb 04dh		;2063	4d		M
	defb 05fh		;2064	5f		_
	defb 078h		;2065	78		x
	defb 0d6h		;2066	d6		.
	defb 0aah		;2067	aa		.
	defb 053h		;2068	53		S
	defb 057h		;2069	57		W
	defb 0ddh		;206a	dd		.
	defb 0deh		;206b	de		.
	defb 001h		;206c	01		.
	defb 0a4h		;206d	a4		.
	defb 023h		;206e	23		#
	defb 072h		;206f	72		r
	defb 0f1h		;2070	f1		.
	defb 028h		;2071	28		(
	defb 084h		;2072	84		.
	defb 0aeh		;2073	ae		.
	defb 050h		;2074	50		P
	defb 006h		;2075	06		.
	defb 073h		;2076	73		s
	defb 059h		;2077	59		Y
	defb 039h		;2078	39		9
	defb 0feh		;2079	fe		.
	defb 0d5h		;207a	d5		.
	defb 086h		;207b	86		.
	defb 003h		;207c	03		.
	defb 039h		;207d	39		9
	defb 0b6h		;207e	b6		.
	defb 047h		;207f	47		G
	defb 055h		;2080	55		U
	defb 0fch		;2081	fc		.
	defb 020h		;2082	20		 
	defb 0eeh		;2083	ee		.
	defb 082h		;2084	82		.
	defb 04ah		;2085	4a		J
	defb 01dh		;2086	1d		.
	defb 0fbh		;2087	fb		.
	defb 07fh		;2088	7f		.
	defb 07eh		;2089	7e		~
	defb 0c9h		;208a	c9		.
	defb 09fh		;208b	9f		.
	defb 0cch		;208c	cc		.
	defb 08ah		;208d	8a		.
	defb 0ddh		;208e	dd		.
	defb 0dah		;208f	da		.
	defb 0e6h		;2090	e6		.
	defb 044h		;2091	44		D
	defb 0dch		;2092	dc		.
	defb 0cbh		;2093	cb		.
	defb 05fh		;2094	5f		_
	defb 0bfh		;2095	bf		.
	defb 0f9h		;2096	f9		.
	defb 035h		;2097	35		5
	defb 096h		;2098	96		.
	defb 004h		;2099	04		.
	defb 09bh		;209a	9b		.
	defb 040h		;209b	40		@
	defb 082h		;209c	82		.
	defb 077h		;209d	77		w
	defb 067h		;209e	67		g
	defb 005h		;209f	05		.
	defb 06eh		;20a0	6e		n
	defb 0aah		;20a1	aa		.
	defb 04eh		;20a2	4e		N
	defb 03fh		;20a3	3f		?
	defb 0d7h		;20a4	d7		.
	defb 0eah		;20a5	ea		.
	defb 073h		;20a6	73		s
	defb 08fh		;20a7	8f		.
	defb 0c5h		;20a8	c5		.
	defb 069h		;20a9	69		i
	defb 0cah		;20aa	ca		.
	defb 033h		;20ab	33		3
	defb 00eh		;20ac	0e		.
	defb 09bh		;20ad	9b		.
	defb 0e6h		;20ae	e6		.
	defb 0fbh		;20af	fb		.
	defb 085h		;20b0	85		.
	defb 096h		;20b1	96		.
	defb 067h		;20b2	67		g
	defb 0cfh		;20b3	cf		.
	defb 069h		;20b4	69		i
	defb 0ceh		;20b5	ce		.
	defb 081h		;20b6	81		.
	defb 0b2h		;20b7	b2		.
	defb 0f1h		;20b8	f1		.
	defb 0e4h		;20b9	e4		.
	defb 00ch		;20ba	0c		.
	defb 0b1h		;20bb	b1		.
	defb 07fh		;20bc	7f		.
	defb 0fch		;20bd	fc		.
	defb 083h		;20be	83		.
	defb 00fh		;20bf	0f		.
	defb 013h		;20c0	13		.
	defb 0e2h		;20c1	e2		.
	defb 0dah		;20c2	da		.
	defb 0cdh		;20c3	cd		.
	defb 030h		;20c4	30		0
	defb 0c5h		;20c5	c5		.
	defb 035h		;20c6	35		5
	defb 0bfh		;20c7	bf		.
	defb 0f1h		;20c8	f1		.
	defb 099h		;20c9	99		.
	defb 096h		;20ca	96		.
	defb 04ah		;20cb	4a		J
	defb 0ddh		;20cc	dd		.
	defb 0cdh		;20cd	cd		.
	defb 005h		;20ce	05		.
	defb 023h		;20cf	23		#
	defb 00fh		;20d0	0f		.
	defb 0c3h		;20d1	c3		.
	defb 083h		;20d2	83		.
	defb 071h		;20d3	71		q
	defb 0fah		;20d4	fa		.
	defb 0a6h		;20d5	a6		.
	defb 070h		;20d6	70		p
	defb 099h		;20d7	99		.
	defb 074h		;20d8	74		t
	defb 045h		;20d9	45		E
	defb 0e3h		;20da	e3		.
	defb 07dh		;20db	7d		}
	defb 019h		;20dc	19		.
	defb 01ah		;20dd	1a		.
	defb 02bh		;20de	2b		+
	defb 026h		;20df	26		&
	defb 058h		;20e0	58		X
	defb 073h		;20e1	73		s
	defb 0e3h		;20e2	e3		.
	defb 020h		;20e3	20		 
	defb 011h		;20e4	11		.
	defb 083h		;20e5	83		.
	defb 066h		;20e6	66		f
	defb 064h		;20e7	64		d
	defb 04dh		;20e8	4d		M
	defb 014h		;20e9	14		.
	defb 0f7h		;20ea	f7		.
	defb 07fh		;20eb	7f		.
	defb 0fbh		;20ec	fb		.
	defb 0c8h		;20ed	c8		.
	defb 0ffh		;20ee	ff		.
	defb 01ch		;20ef	1c		.
	defb 005h		;20f0	05		.
	defb 001h		;20f1	01		.
	defb 000h		;20f2	00		.
	defb 089h		;20f3	89		.
	defb 09ch		;20f4	9c		.
	defb 0bch		;20f5	bc		.
	defb 080h		;20f6	80		.
	defb 083h		;20f7	83		.
	defb 099h		;20f8	99		.
	defb 00eh		;20f9	0e		.
	defb 001h		;20fa	01		.
	defb 026h		;20fb	26		&
	defb 082h		;20fc	82		.
	defb 07fh		;20fd	7f		.
	defb 0d8h		;20fe	d8		.
	defb 092h		;20ff	92		.
	defb 00fh		;2100	0f		.
	defb 035h		;2101	35		5
	defb 0edh		;2102	ed		.
	defb 044h		;2103	44		D
	defb 0efh		;2104	ef		.
	defb 033h		;2105	33		3
	defb 062h		;2106	62		b
	defb 0e6h		;2107	e6		.
	defb 081h		;2108	81		.
	defb 099h		;2109	99		.
	defb 07dh		;210a	7d		}
	defb 016h		;210b	16		.
	defb 04bh		;210c	4b		K
	defb 0bch		;210d	bc		.
	defb 0b1h		;210e	b1		.
	defb 002h		;210f	02		.
	defb 0e1h		;2110	e1		.
	defb 020h		;2111	20		 
	defb 0fdh		;2112	fd		.
	defb 07fh		;2113	7f		.
	defb 078h		;2114	78		x
	defb 0cah		;2115	ca		.
	defb 0ffh		;2116	ff		.
	defb 01fh		;2117	1f		.
	defb 003h		;2118	03		.
	defb 040h		;2119	40		@
	defb 080h		;211a	80		.
	defb 013h		;211b	13		.
	defb 00dh		;211c	0d		.
	defb 081h		;211d	81		.
	defb 044h		;211e	44		D
	defb 015h		;211f	15		.
	defb 09ah		;2120	9a		.
	defb 025h		;2121	25		%
	defb 02ch		;2122	2c		,
	defb 07fh		;2123	7f		.
	defb 0bbh		;2124	bb		.
	defb 0fah		;2125	fa		.
	defb 000h		;2126	00		.
	defb 053h		;2127	53		S
	defb 021h		;2128	21		!
	defb 07ch		;2129	7c		|
	defb 0ebh		;212a	eb		.
	defb 00fh		;212b	0f		.
	defb 076h		;212c	76		v
	defb 0cbh		;212d	cb		.
	defb 0ffh		;212e	ff		.
	defb 00fh		;212f	0f		.
	defb 083h		;2130	83		.
	defb 081h		;2131	81		.
	defb 0a4h		;2132	a4		.
	defb 07ch		;2133	7c		|
	defb 07ch		;2134	7c		|
	defb 040h		;2135	40		@
	defb 096h		;2136	96		.
	defb 044h		;2137	44		D
	defb 0ddh		;2138	dd		.
	defb 0c2h		;2139	c2		.
	defb 0c9h		;213a	c9		.
	defb 05fh		;213b	5f		_
	defb 02fh		;213c	2f		/
	defb 0feh		;213d	fe		.
	defb 0ffh		;213e	ff		.
	defb 0bbh		;213f	bb		.
	defb 030h		;2140	30		0
	defb 0fch		;2141	fc		.
	defb 0cch		;2142	cc		.
	defb 037h		;2143	37		7
	defb 0e5h		;2144	e5		.
	defb 07fh		;2145	7f		.
	defb 074h		;2146	74		t
	defb 0cdh		;2147	cd		.
	defb 0ffh		;2148	ff		.
	defb 058h		;2149	58		X
	defb 0e5h		;214a	e5		.
	defb 064h		;214b	64		d
	defb 01eh		;214c	1e		.
	defb 0d5h		;214d	d5		.
	defb 079h		;214e	79		y
	defb 008h		;214f	08		.
	defb 010h		;2150	10		.
	defb 0eeh		;2151	ee		.
	defb 03fh		;2152	3f		?
	defb 0bdh		;2153	bd		.
	defb 037h		;2154	37		7
	defb 083h		;2155	83		.
	defb 077h		;2156	77		w
	defb 0c7h		;2157	c7		.
	defb 0f3h		;2158	f3		.
	defb 083h		;2159	83		.
	defb 014h		;215a	14		.
	defb 05bh		;215b	5b		[
	defb 000h		;215c	00		.
	defb 054h		;215d	54		T
	defb 04bh		;215e	4b		K
	defb 0f3h		;215f	f3		.
	defb 009h		;2160	09		.
	defb 069h		;2161	69		i
	defb 071h		;2162	71		q
	defb 0fah		;2163	fa		.
	defb 0ceh		;2164	ce		.
	defb 0c9h		;2165	c9		.
	defb 084h		;2166	84		.
	defb 042h		;2167	42		B
	defb 09ch		;2168	9c		.
	defb 034h		;2169	34		4
	defb 003h		;216a	03		.
	defb 0c9h		;216b	c9		.
	defb 0bah		;216c	ba		.
	defb 002h		;216d	02		.
	defb 064h		;216e	64		d
	defb 023h		;216f	23		#
	defb 022h		;2170	22		"
	defb 0beh		;2171	be		.
	defb 09ch		;2172	9c		.
	defb 0cfh		;2173	cf		.
	defb 081h		;2174	81		.
	defb 04eh		;2175	4e		N
	defb 00dh		;2176	0d		.
	defb 031h		;2177	31		1
	defb 0aah		;2178	aa		.
	defb 042h		;2179	42		B
	defb 08dh		;217a	8d		.
	defb 0f1h		;217b	f1		.
	defb 07fh		;217c	7f		.
	defb 042h		;217d	42		B
	defb 0d8h		;217e	d8		.
	defb 0a6h		;217f	a6		.
	defb 07eh		;2180	7e		~
	defb 0d4h		;2181	d4		.
	defb 0ffh		;2182	ff		.
	defb 01dh		;2183	1d		.
	defb 086h		;2184	86		.
	defb 087h		;2185	87		.
	defb 038h		;2186	38		8
	defb 08bh		;2187	8b		.
	defb 044h		;2188	44		D
	defb 04ch		;2189	4c		L
	defb 071h		;218a	71		q
	defb 0f3h		;218b	f3		.
	defb 082h		;218c	82		.
	defb 047h		;218d	47		G
	defb 015h		;218e	15		.
	defb 041h		;218f	41		A
	defb 005h		;2190	05		.
	defb 07fh		;2191	7f		.
	defb 062h		;2192	62		b
	defb 08ah		;2193	8a		.
	defb 022h		;2194	22		"
	defb 00bh		;2195	0b		.
	defb 0aeh		;2196	ae		.
	defb 07bh		;2197	7b		{
	defb 064h		;2198	64		d
	defb 0e8h		;2199	e8		.
	defb 0bbh		;219a	bb		.
	defb 012h		;219b	12		.
	defb 03eh		;219c	3e		>
	defb 03dh		;219d	3d		=
	defb 049h		;219e	49		I
	defb 041h		;219f	41		A
	defb 07ah		;21a0	7a		z
	defb 073h		;21a1	73		s
	defb 0f4h		;21a2	f4		.
	defb 0d2h		;21a3	d2		.
	defb 078h		;21a4	78		x
	defb 06ah		;21a5	6a		j
	defb 070h		;21a6	70		p
	defb 006h		;21a7	06		.
	defb 0f7h		;21a8	f7		.
	defb 044h		;21a9	44		D
	defb 087h		;21aa	87		.
	defb 077h		;21ab	77		w
	defb 055h		;21ac	55		U
	defb 0afh		;21ad	af		.
	defb 05fh		;21ae	5f		_
	defb 065h		;21af	65		e
	defb 09eh		;21b0	9e		.
	defb 029h		;21b1	29		)
	defb 09dh		;21b2	9d		.
	defb 0c4h		;21b3	c4		.
	defb 044h		;21b4	44		D
	defb 0ffh		;21b5	ff		.
	defb 026h		;21b6	26		&
	defb 08ch		;21b7	8c		.
	defb 0adh		;21b8	ad		.
	defb 046h		;21b9	46		F
	defb 013h		;21ba	13		.
	defb 012h		;21bb	12		.
	defb 0e5h		;21bc	e5		.
	defb 0bbh		;21bd	bb		.
	defb 001h		;21be	01		.
	defb 076h		;21bf	76		v
	defb 010h		;21c0	10		.
	defb 0f9h		;21c1	f9		.
	defb 013h		;21c2	13		.
	defb 07ch		;21c3	7c		|
	defb 0cch		;21c4	cc		.
	defb 008h		;21c5	08		.
	defb 069h		;21c6	69		i
	defb 074h		;21c7	74		t
	defb 071h		;21c8	71		q
	defb 0d7h		;21c9	d7		.
	defb 0ffh		;21ca	ff		.
	defb 0a1h		;21cb	a1		.
	defb 018h		;21cc	18		.
	defb 02bh		;21cd	2b		+
	defb 012h		;21ce	12		.
	defb 0a6h		;21cf	a6		.
	defb 054h		;21d0	54		T
	defb 09fh		;21d1	9f		.
	defb 025h		;21d2	25		%
	defb 01fh		;21d3	1f		.
	defb 00bh		;21d4	0b		.
	defb 051h		;21d5	51		Q
	defb 002h		;21d6	02		.
	defb 08dh		;21d7	8d		.
	defb 083h		;21d8	83		.
	defb 064h		;21d9	64		d
	defb 046h		;21da	46		F
	defb 066h		;21db	66		f
	defb 032h		;21dc	32		2
	defb 08fh		;21dd	8f		.
	defb 028h		;21de	28		(
	defb 018h		;21df	18		.
	defb 07fh		;21e0	7f		.
	defb 084h		;21e1	84		.
	defb 0cch		;21e2	cc		.
	defb 07ch		;21e3	7c		|
	defb 04ch		;21e4	4c		L
	defb 026h		;21e5	26		&
	defb 02bh		;21e6	2b		+
	defb 001h		;21e7	01		.
	defb 0f4h		;21e8	f4		.
	defb 099h		;21e9	99		.
	defb 099h		;21ea	99		.
	defb 058h		;21eb	58		X
	defb 0e1h		;21ec	e1		.
	defb 0ffh		;21ed	ff		.
	defb 056h		;21ee	56		V
	defb 00dh		;21ef	0d		.
	defb 075h		;21f0	75		u
	defb 01eh		;21f1	1e		.
	defb 045h		;21f2	45		E
	defb 020h		;21f3	20		 
	defb 01bh		;21f4	1b		.
	defb 0d5h		;21f5	d5		.
	defb 031h		;21f6	31		1
	defb 08ah		;21f7	8a		.
	defb 006h		;21f8	06		.
	defb 0a7h		;21f9	a7		.
	defb 02dh		;21fa	2d		-
	defb 001h		;21fb	01		.
	defb 005h		;21fc	05		.
	defb 012h		;21fd	12		.
	defb 01dh		;21fe	1d		.
	defb 009h		;21ff	09		.
	defb 038h		;2200	38		8
	defb 030h		;2201	30		0
	defb 07fh		;2202	7f		.
	defb 090h		;2203	90		.
	defb 0c8h		;2204	c8		.
	defb 01fh		;2205	1f		.
	defb 09ch		;2206	9c		.
	defb 04fh		;2207	4f		O
	defb 0bch		;2208	bc		.
	defb 054h		;2209	54		T
	defb 030h		;220a	30		0
	defb 07ah		;220b	7a		z
	defb 004h		;220c	04		.
	defb 0e9h		;220d	e9		.
	defb 0fah		;220e	fa		.
	defb 075h		;220f	75		u
	defb 06ch		;2210	6c		l
	defb 0f9h		;2211	f9		.
	defb 0cbh		;2212	cb		.
	defb 0dch		;2213	dc		.
	defb 0ffh		;2214	ff		.
	defb 066h		;2215	66		f
	defb 021h		;2216	21		!
	defb 0a0h		;2217	a0		.
	defb 055h		;2218	55		U
	defb 0a2h		;2219	a2		.
	defb 026h		;221a	26		&
	defb 0a6h		;221b	a6		.
	defb 032h		;221c	32		2
	defb 006h		;221d	06		.
	defb 0b1h		;221e	b1		.
	defb 001h		;221f	01		.
	defb 04eh		;2220	4e		N
	defb 07ah		;2221	7a		z
	defb 007h		;2222	07		.
	defb 0f9h		;2223	f9		.
	defb 012h		;2224	12		.
	defb 019h		;2225	19		.
	defb 054h		;2226	54		T
	defb 01dh		;2227	1d		.
	defb 087h		;2228	87		.
	defb 03bh		;2229	3b		;
	defb 0e6h		;222a	e6		.
	defb 0ffh		;222b	ff		.
	defb 06eh		;222c	6e		n
	defb 031h		;222d	31		1
	defb 074h		;222e	74		t
	defb 030h		;222f	30		0
	defb 0c8h		;2230	c8		.
	defb 072h		;2231	72		r
	defb 0f2h		;2232	f2		.
	defb 0e3h		;2233	e3		.
	defb 0ffh		;2234	ff		.
	defb 026h		;2235	26		&
	defb 090h		;2236	90		.
	defb 026h		;2237	26		&
	defb 062h		;2238	62		b
	defb 0bah		;2239	ba		.
	defb 076h		;223a	76		v
	defb 025h		;223b	25		%
	defb 056h		;223c	56		V
	defb 0a3h		;223d	a3		.
	defb 06ch		;223e	6c		l
	defb 0ffh		;223f	ff		.
	defb 079h		;2240	79		y
	defb 02ch		;2241	2c		,
	defb 03eh		;2242	3e		>
	defb 011h		;2243	11		.
	defb 01fh		;2244	1f		.
	defb 0b5h		;2245	b5		.
	defb 07ah		;2246	7a		z
	defb 04ch		;2247	4c		L
	defb 0e3h		;2248	e3		.
	defb 0c7h		;2249	c7		.
	defb 0eah		;224a	ea		.
	defb 0f8h		;224b	f8		.
	defb 068h		;224c	68		h
	defb 0ffh		;224d	ff		.
	defb 076h		;224e	76		v
	defb 0a8h		;224f	a8		.
	defb 0ddh		;2250	dd		.
	defb 0c4h		;2251	c4		.
	defb 0e7h		;2252	e7		.
	defb 05ah		;2253	5a		Z
	defb 051h		;2254	51		Q
	defb 0d0h		;2255	d0		.
	defb 0b7h		;2256	b7		.
	defb 06eh		;2257	6e		n
	defb 077h		;2258	77		w
	defb 0b1h		;2259	b1		.
	defb 0d8h		;225a	d8		.
	defb 00bh		;225b	0b		.
	defb 08dh		;225c	8d		.
	defb 0f7h		;225d	f7		.
	defb 0ddh		;225e	dd		.
	defb 0d5h		;225f	d5		.
	defb 076h		;2260	76		v
	defb 063h		;2261	63		c
	defb 060h		;2262	60		`
	defb 0d9h		;2263	d9		.
	defb 090h		;2264	90		.
	defb 072h		;2265	72		r
	defb 0cbh		;2266	cb		.
	defb 09dh		;2267	9d		.
	defb 027h		;2268	27		'
	defb 07eh		;2269	7e		~
	defb 0cbh		;226a	cb		.
	defb 0feh		;226b	fe		.
	defb 0f4h		;226c	f4		.
	defb 03eh		;226d	3e		>
	defb 002h		;226e	02		.
	defb 007h		;226f	07		.
	defb 0b8h		;2270	b8		.
	defb 030h		;2271	30		0
	defb 0b3h		;2272	b3		.
	defb 0ffh		;2273	ff		.
	defb 0ffh		;2274	ff		.
	defb 0c3h		;2275	c3		.
	defb 051h		;2276	51		Q
	defb 096h		;2277	96		.
	defb 0a7h		;2278	a7		.
	defb 016h		;2279	16		.
	defb 0a0h		;227a	a0		.
	defb 028h		;227b	28		(
	defb 005h		;227c	05		.
	defb 03dh		;227d	3d		=
	defb 010h		;227e	10		.
	defb 07fh		;227f	7f		.
	defb 0b0h		;2280	b0		.
	defb 000h		;2281	00		.
	defb 03eh		;2282	3e		>
	defb 021h		;2283	21		!
	defb 0d3h		;2284	d3		.
	defb 0bch		;2285	bc		.
	defb 037h		;2286	37		7
	defb 023h		;2287	23		#
	defb 0fah		;2288	fa		.
	defb 006h		;2289	06		.
	defb 0cdh		;228a	cd		.
	defb 085h		;228b	85		.
	defb 00eh		;228c	0e		.
	defb 051h		;228d	51		Q
	defb 0dah		;228e	da		.
	defb 0ffh		;228f	ff		.
	defb 0ebh		;2290	eb		.
	defb 02ch		;2291	2c		,
	defb 0dah		;2292	da		.
	defb 004h		;2293	04		.
	defb 087h		;2294	87		.
	defb 0a7h		;2295	a7		.
	defb 0feh		;2296	fe		.
	defb 087h		;2297	87		.
	defb 053h		;2298	53		S
	defb 040h		;2299	40		@
	defb 078h		;229a	78		x
	defb 0e6h		;229b	e6		.
	defb 080h		;229c	80		.
	defb 021h		;229d	21		!
	defb 018h		;229e	18		.
	defb 001h		;229f	01		.
	defb 028h		;22a0	28		(
	defb 01ah		;22a1	1a		.
	defb 010h		;22a2	10		.
	defb 01bh		;22a3	1b		.
	defb 07dh		;22a4	7d		}
	defb 016h		;22a5	16		.
	defb 00eh		;22a6	0e		.
	defb 006h		;22a7	06		.
	defb 0fbh		;22a8	fb		.
	defb 076h		;22a9	76		v
	defb 02bh		;22aa	2b		+
	defb 07ch		;22ab	7c		|
	defb 0b5h		;22ac	b5		.
	defb 0cah		;22ad	ca		.
	defb 01ah		;22ae	1a		.
	defb 097h		;22af	97		.
	defb 0cdh		;22b0	cd		.
	defb 0e6h		;22b1	e6		.
	defb 0dah		;22b2	da		.
	defb 087h		;22b3	87		.
	defb 03dh		;22b4	3d		=
	defb 09ah		;22b5	9a		.
	defb 0cah		;22b6	ca		.
	defb 020h		;22b7	20		 
	defb 0eah		;22b8	ea		.
	defb 01eh		;22b9	1e		.
	defb 019h		;22ba	19		.
	defb 0f0h		;22bb	f0		.
	defb 001h		;22bc	01		.
	defb 059h		;22bd	59		Y
	defb 037h		;22be	37		7
	defb 0e4h		;22bf	e4		.
	defb 04eh		;22c0	4e		N
	defb 0feh		;22c1	fe		.
	defb 00dh		;22c2	0d		.
	defb 060h		;22c3	60		`
	defb 09eh		;22c4	9e		.
	defb 020h		;22c5	20		 
	defb 0fbh		;22c6	fb		.
	defb 019h		;22c7	19		.
	defb 01dh		;22c8	1d		.
	defb 0bbh		;22c9	bb		.
	defb 0c2h		;22ca	c2		.
	defb 05bh		;22cb	5b		[
	defb 0a6h		;22cc	a6		.
	defb 0dfh		;22cd	df		.
	defb 03ah		;22ce	3a		:
	defb 0b7h		;22cf	b7		.
	defb 026h		;22d0	26		&
	defb 073h		;22d1	73		s
	defb 0a5h		;22d2	a5		.
	defb 0fbh		;22d3	fb		.
	defb 096h		;22d4	96		.
	defb 0f6h		;22d5	f6		.
	defb 000h		;22d6	00		.
	defb 0f3h		;22d7	f3		.
	defb 041h		;22d8	41		A
	defb 059h		;22d9	59		Y
	defb 09bh		;22da	9b		.
	defb 0c0h		;22db	c0		.
	defb 0c3h		;22dc	c3		.
	defb 0dbh		;22dd	db		.
	defb 035h		;22de	35		5
	defb 0c0h		;22df	c0		.
	defb 0eeh		;22e0	ee		.
	defb 040h		;22e1	40		@
	defb 0e8h		;22e2	e8		.
	defb 0d6h		;22e3	d6		.
	defb 0cch		;22e4	cc		.
	defb 098h		;22e5	98		.
	defb 0f4h		;22e6	f4		.
	defb 0ech		;22e7	ec		.
	defb 0dch		;22e8	dc		.
	defb 006h		;22e9	06		.
	defb 09fh		;22ea	9f		.
	defb 09fh		;22eb	9f		.
	defb 0bdh		;22ec	bd		.
	defb 02ah		;22ed	2a		*
	defb 002h		;22ee	02		.
	defb 098h		;22ef	98		.
	defb 011h		;22f0	11		.
	defb 001h		;22f1	01		.
	defb 030h		;22f2	30		0
	defb 0e6h		;22f3	e6		.
	defb 0dfh		;22f4	df		.
	defb 0f3h		;22f5	f3		.
	defb 0d2h		;22f6	d2		.
	defb 064h		;22f7	64		d
	defb 0c6h		;22f8	c6		.
	defb 0c2h		;22f9	c2		.
	defb 0deh		;22fa	de		.
	defb 071h		;22fb	71		q
	defb 000h		;22fc	00		.
	defb 011h		;22fd	11		.
	defb 008h		;22fe	08		.
	defb 06bh		;22ff	6b		k
	defb 051h		;2300	51		Q
	defb 0c7h		;2301	c7		.
	defb 079h		;2302	79		y
	defb 05dh		;2303	5d		]
	defb 050h		;2304	50		P
	defb 079h		;2305	79		y
	defb 07eh		;2306	7e		~
	defb 0edh		;2307	ed		.
	defb 0b2h		;2308	b2		.
	defb 0b7h		;2309	b7		.
	defb 0fch		;230a	fc		.
	defb 04dh		;230b	4d		M
	defb 0aeh		;230c	ae		.
	defb 065h		;230d	65		e
	defb 0cfh		;230e	cf		.
	defb 0c0h		;230f	c0		.
	defb 052h		;2310	52		R
	defb 02bh		;2311	2b		+
	defb 0e8h		;2312	e8		.
	defb 0a1h		;2313	a1		.
	defb 0efh		;2314	ef		.
	defb 0a3h		;2315	a3		.
	defb 0c3h		;2316	c3		.
	defb 0dah		;2317	da		.
	defb 0ddh		;2318	dd		.
	defb 096h		;2319	96		.
	defb 037h		;231a	37		7
	defb 027h		;231b	27		'
	defb 0b7h		;231c	b7		.
	defb 00eh		;231d	0e		.
	defb 074h		;231e	74		t
	defb 0fdh		;231f	fd		.
	defb 06bh		;2320	6b		k
	defb 0a6h		;2321	a6		.
	defb 077h		;2322	77		w
	defb 007h		;2323	07		.
	defb 071h		;2324	71		q
	defb 08ch		;2325	8c		.
	defb 0beh		;2326	be		.
	defb 052h		;2327	52		R
	defb 09dh		;2328	9d		.
	defb 05fh		;2329	5f		_
	defb 085h		;232a	85		.
	defb 0f0h		;232b	f0		.
	defb 047h		;232c	47		G
	defb 03ah		;232d	3a		:
	defb 006h		;232e	06		.
	defb 07eh		;232f	7e		~
	defb 03dh		;2330	3d		=
	defb 0bfh		;2331	bf		.
	defb 002h		;2332	02		.
	defb 03dh		;2333	3d		=
	defb 0e6h		;2334	e6		.
	defb 00fh		;2335	0f		.
	defb 0b0h		;2336	b0		.
	defb 013h		;2337	13		.
	defb 07fh		;2338	7f		.
	defb 063h		;2339	63		c
	defb 07eh		;233a	7e		~
	defb 0cbh		;233b	cb		.
	defb 04fh		;233c	4f		O
	defb 028h		;233d	28		(
	defb 002h		;233e	02		.
	defb 0f0h		;233f	f0		.
	defb 0f3h		;2340	f3		.
	defb 076h		;2341	76		v
	defb 078h		;2342	78		x
	defb 006h		;2343	06		.
	defb 059h		;2344	59		Y
	defb 08eh		;2345	8e		.
	defb 021h		;2346	21		!
	defb 0eeh		;2347	ee		.
	defb 0b6h		;2348	b6		.
	defb 031h		;2349	31		1
	defb 0c7h		;234a	c7		.
	defb 000h		;234b	00		.
	defb 0b2h		;234c	b2		.
	defb 074h		;234d	74		t
	defb 0b1h		;234e	b1		.
	defb 0ddh		;234f	dd		.
	defb 075h		;2350	75		u
	defb 003h		;2351	03		.
	defb 074h		;2352	74		t
	defb 004h		;2353	04		.
	defb 026h		;2354	26		&
	defb 09eh		;2355	9e		.
	defb 0f6h		;2356	f6		.
	defb 00ch		;2357	0c		.
	defb 0ech		;2358	ec		.
	defb 04fh		;2359	4f		O
	defb 0a9h		;235a	a9		.
	defb 019h		;235b	19		.
	defb 079h		;235c	79		y
	defb 03eh		;235d	3e		>
	defb 091h		;235e	91		.
	defb 0cdh		;235f	cd		.
	defb 09ch		;2360	9c		.
	defb 097h		;2361	97		.
	defb 08ch		;2362	8c		.
	defb 099h		;2363	99		.
	defb 04eh		;2364	4e		N
	defb 092h		;2365	92		.
	defb 0e9h		;2366	e9		.
	defb 07eh		;2367	7e		~
	defb 079h		;2368	79		y
	defb 0bah		;2369	ba		.
	defb 0eeh		;236a	ee		.
	defb 009h		;236b	09		.
	defb 03dh		;236c	3d		=
	defb 020h		;236d	20		 
	defb 0fch		;236e	fc		.
	defb 0aeh		;236f	ae		.
	defb 0a6h		;2370	a6		.
	defb 0dbh		;2371	db		.
	defb 005h		;2372	05		.
	defb 0dbh		;2373	db		.
	defb 006h		;2374	06		.
	defb 07ah		;2375	7a		z
	defb 026h		;2376	26		&
	defb 0bdh		;2377	bd		.
	defb 06fh		;2378	6f		o
	defb 07bh		;2379	7b		{
	defb 0bdh		;237a	bd		.
	defb 0a2h		;237b	a2		.
	defb 0bfh		;237c	bf		.
	defb 018h		;237d	18		.
	defb 0ebh		;237e	eb		.
	defb 0f5h		;237f	f5		.
	defb 0e3h		;2380	e3		.
	defb 05dh		;2381	5d		]
	defb 040h		;2382	40		@
	defb 040h		;2383	40		@
	defb 0c0h		;2384	c0		.
	defb 06eh		;2385	6e		n
	defb 0bfh		;2386	bf		.
	defb 0b5h		;2387	b5		.
	defb 0d1h		;2388	d1		.
	defb 0d8h		;2389	d8		.
	defb 057h		;238a	57		W
	defb 051h		;238b	51		Q
	defb 0ceh		;238c	ce		.
	defb 026h		;238d	26		&
	defb 0c3h		;238e	c3		.
	defb 0bfh		;238f	bf		.
	defb 0dfh		;2390	df		.
	defb 0fbh		;2391	fb		.
	defb 021h		;2392	21		!
	defb 0fdh		;2393	fd		.
	defb 005h		;2394	05		.
	defb 076h		;2395	76		v
	defb 02bh		;2396	2b		+
	defb 00ah		;2397	0a		.
	defb 0f2h		;2398	f2		.
	defb 0c8h		;2399	c8		.
	defb 0dch		;239a	dc		.
	defb 020h		;239b	20		 
	defb 0e5h		;239c	e5		.
	defb 061h		;239d	61		a
	defb 03ah		;239e	3a		:
	defb 035h		;239f	35		5
	defb 081h		;23a0	81		.
	defb 0a2h		;23a1	a2		.
	defb 0bbh		;23a2	bb		.
	defb 002h		;23a3	02		.
	defb 0b2h		;23a4	b2		.
	defb 02bh		;23a5	2b		+
	defb 063h		;23a6	63		c
	defb 01eh		;23a7	1e		.
	defb 0d2h		;23a8	d2		.
	defb 0d8h		;23a9	d8		.
	defb 07dh		;23aa	7d		}
	defb 0b4h		;23ab	b4		.
	defb 0c2h		;23ac	c2		.
	defb 0d8h		;23ad	d8		.
	defb 0b5h		;23ae	b5		.
	defb 0b2h		;23af	b2		.
	defb 0d9h		;23b0	d9		.
	defb 00eh		;23b1	0e		.
	defb 088h		;23b2	88		.
	defb 063h		;23b3	63		c
	defb 03fh		;23b4	3f		?
	defb 0c8h		;23b5	c8		.
	defb 0abh		;23b6	ab		.
	defb 021h		;23b7	21		!
	defb 08bh		;23b8	8b		.
	defb 03eh		;23b9	3e		>
	defb 0abh		;23ba	ab		.
	defb 05ah		;23bb	5a		Z
	defb 067h		;23bc	67		g
	defb 04eh		;23bd	4e		N
	defb 041h		;23be	41		A
	defb 0c0h		;23bf	c0		.
	defb 045h		;23c0	45		E
	defb 056h		;23c1	56		V
	defb 0d4h		;23c2	d4		.
	defb 065h		;23c3	65		e
	defb 0cfh		;23c4	cf		.
	defb 0d1h		;23c5	d1		.
	defb 015h		;23c6	15		.
	defb 03dh		;23c7	3d		=
	defb 0cdh		;23c8	cd		.
	defb 06ah		;23c9	6a		j
	defb 0adh		;23ca	ad		.
	defb 027h		;23cb	27		'
	defb 09bh		;23cc	9b		.
	defb 031h		;23cd	31		1
	defb 079h		;23ce	79		y
	defb 00bh		;23cf	0b		.
	defb 06fh		;23d0	6f		o
	defb 0a3h		;23d1	a3		.
	defb 099h		;23d2	99		.
	defb 0f6h		;23d3	f6		.
	defb 0a7h		;23d4	a7		.
	defb 040h		;23d5	40		@
	defb 03ah		;23d6	3a		:
	defb 06ch		;23d7	6c		l
	defb 09ah		;23d8	9a		.
	defb 047h		;23d9	47		G
	defb 00eh		;23da	0e		.
	defb 089h		;23db	89		.
	defb 09dh		;23dc	9d		.
	defb 0f3h		;23dd	f3		.
	defb 0afh		;23de	af		.
	defb 003h		;23df	03		.
	defb 033h		;23e0	33		3
	defb 0f0h		;23e1	f0		.
	defb 001h		;23e2	01		.
	defb 0b7h		;23e3	b7		.
	defb 0b5h		;23e4	b5		.
	defb 0bah		;23e5	ba		.
	defb 0e1h		;23e6	e1		.
	defb 09dh		;23e7	9d		.
	defb 0a7h		;23e8	a7		.
	defb 0a1h		;23e9	a1		.
	defb 004h		;23ea	04		.
	defb 002h		;23eb	02		.
	defb 0c0h		;23ec	c0		.
	defb 0b4h		;23ed	b4		.
	defb 039h		;23ee	39		9
	defb 06dh		;23ef	6d		m
	defb 005h		;23f0	05		.
	defb 02bh		;23f1	2b		+
	defb 0e6h		;23f2	e6		.
	defb 09fh		;23f3	9f		.
	defb 069h		;23f4	69		i
	defb 06fh		;23f5	6f		o
	defb 06bh		;23f6	6b		k
	defb 096h		;23f7	96		.
	defb 048h		;23f8	48		H
	defb 089h		;23f9	89		.
	defb 06ch		;23fa	6c		l
	defb 0f3h		;23fb	f3		.
	defb 011h		;23fc	11		.
	defb 002h		;23fd	02		.
	defb 004h		;23fe	04		.
	defb 001h		;23ff	01		.
	defb 04ch		;2400	4c		L
	defb 0b8h		;2401	b8		.
	defb 095h		;2402	95		.
	defb 08ah		;2403	8a		.
	defb 0e9h		;2404	e9		.
	defb 0a1h		;2405	a1		.
	defb 05ch		;2406	5c		\
	defb 02dh		;2407	2d		-
	defb 03dh		;2408	3d		=
	defb 006h		;2409	06		.
	defb 013h		;240a	13		.
	defb 03dh		;240b	3d		=
	defb 0a9h		;240c	a9		.
	defb 04dh		;240d	4d		M
	defb 0ebh		;240e	eb		.
	defb 039h		;240f	39		9
	defb 02ah		;2410	2a		*
	defb 006h		;2411	06		.
	defb 026h		;2412	26		&
	defb 00dh		;2413	0d		.
	defb 080h		;2414	80		.
	defb 05bh		;2415	5b		[
	defb 08ah		;2416	8a		.
	defb 019h		;2417	19		.
	defb 082h		;2418	82		.
	defb 0bah		;2419	ba		.
	defb 07ch		;241a	7c		|
	defb 0a0h		;241b	a0		.
	defb 06eh		;241c	6e		n
	defb 071h		;241d	71		q
	defb 0aah		;241e	aa		.
	defb 0cch		;241f	cc		.
	defb 0d8h		;2420	d8		.
	defb 056h		;2421	56		V
	defb 0ddh		;2422	dd		.
	defb 007h		;2423	07		.
	defb 01bh		;2424	1b		.
	defb 0b7h		;2425	b7		.
	defb 0dah		;2426	da		.
	defb 008h		;2427	08		.
	defb 0b6h		;2428	b6		.
	defb 055h		;2429	55		U
	defb 01ch		;242a	1c		.
	defb 055h		;242b	55		U
	defb 0adh		;242c	ad		.
	defb 009h		;242d	09		.
	defb 04ah		;242e	4a		J
	defb 062h		;242f	62		b
	defb 001h		;2430	01		.
	defb 000h		;2431	00		.
	defb 016h		;2432	16		.
	defb 079h		;2433	79		y
	defb 032h		;2434	32		2
	defb 06bh		;2435	6b		k
	defb 09ah		;2436	9a		.
	defb 0c5h		;2437	c5		.
	defb 0cdh		;2438	cd		.
	defb 0d4h		;2439	d4		.
	defb 099h		;243a	99		.
	defb 0c1h		;243b	c1		.
	defb 00ch		;243c	0c		.
	defb 010h		;243d	10		.
	defb 0f4h		;243e	f4		.
	defb 0afh		;243f	af		.
	defb 0beh		;2440	be		.
	defb 0a1h		;2441	a1		.
	defb 031h		;2442	31		1
	defb 099h		;2443	99		.
	defb 021h		;2444	21		!
	defb 0adh		;2445	ad		.
	defb 098h		;2446	98		.
	defb 0e5h		;2447	e5		.
	defb 053h		;2448	53		S
	defb 093h		;2449	93		.
	defb 0f8h		;244a	f8		.
	defb 0b3h		;244b	b3		.
	defb 03bh		;244c	3b		;
	defb 052h		;244d	52		R
	defb 03bh		;244e	3b		;
	defb 00ch		;244f	0c		.
	defb 09eh		;2450	9e		.
	defb 0cah		;2451	ca		.
	defb 070h		;2452	70		p
	defb 0afh		;2453	af		.
	defb 0bah		;2454	ba		.
	defb 058h		;2455	58		X
	defb 0edh		;2456	ed		.
	defb 055h		;2457	55		U
	defb 082h		;2458	82		.
	defb 056h		;2459	56		V
	defb 055h		;245a	55		U
	defb 0afh		;245b	af		.
	defb 05bh		;245c	5b		[
	defb 0afh		;245d	af		.
	defb 0edh		;245e	ed		.
	defb 054h		;245f	54		T
	defb 0eeh		;2460	ee		.
	defb 055h		;2461	55		U
	defb 04ah		;2462	4a		J
	defb 053h		;2463	53		S
	defb 0bdh		;2464	bd		.
	defb 067h		;2465	67		g
	defb 0dch		;2466	dc		.
	defb 09ah		;2467	9a		.
	defb 059h		;2468	59		Y
	defb 0eah		;2469	ea		.
	defb 0b5h		;246a	b5		.
	defb 0a9h		;246b	a9		.
	defb 0e4h		;246c	e4		.
	defb 0bdh		;246d	bd		.
	defb 0a4h		;246e	a4		.
	defb 0b6h		;246f	b6		.
	defb 0edh		;2470	ed		.
	defb 0feh		;2471	fe		.
	defb 02bh		;2472	2b		+
	defb 09bh		;2473	9b		.
	defb 0eeh		;2474	ee		.
	defb 02dh		;2475	2d		-
	defb 0dah		;2476	da		.
	defb 06ch		;2477	6c		l
	defb 03ch		;2478	3c		<
	defb 046h		;2479	46		F
	defb 0afh		;247a	af		.
	defb 06dh		;247b	6d		m
	defb 0d2h		;247c	d2		.
	defb 03dh		;247d	3d		=
	defb 0edh		;247e	ed		.
	defb 055h		;247f	55		U
	defb 094h		;2480	94		.
	defb 03fh		;2481	3f		?
	defb 055h		;2482	55		U
	defb 0afh		;2483	af		.
	defb 033h		;2484	33		3
	defb 0afh		;2485	af		.
	defb 0edh		;2486	ed		.
	defb 041h		;2487	41		A
	defb 0edh		;2488	ed		.
	defb 055h		;2489	55		U
	defb 03ah		;248a	3a		:
	defb 044h		;248b	44		D
	defb 03dh		;248c	3d		=
	defb 0afh		;248d	af		.
	defb 041h		;248e	41		A
	defb 0abh		;248f	ab		.
	defb 0ddh		;2490	dd		.
	defb 02ch		;2491	2c		,
	defb 0c0h		;2492	c0		.
	defb 0d1h		;2493	d1		.
	defb 022h		;2494	22		"
	defb 0e1h		;2495	e1		.
	defb 016h		;2496	16		.
	defb 097h		;2497	97		.
	defb 037h		;2498	37		7
	defb 09fh		;2499	9f		.
	defb 0b9h		;249a	b9		.
	defb 099h		;249b	99		.
	defb 096h		;249c	96		.
	defb 060h		;249d	60		`
	defb 096h		;249e	96		.
	defb 070h		;249f	70		p
	defb 0a7h		;24a0	a7		.
	defb 0a9h		;24a1	a9		.
	defb 0f9h		;24a2	f9		.
	defb 0d9h		;24a3	d9		.
	defb 0edh		;24a4	ed		.
	defb 0cdh		;24a5	cd		.
	defb 01ah		;24a6	1a		.
	defb 09ah		;24a7	9a		.
	defb 03ah		;24a8	3a		:
	defb 05ah		;24a9	5a		Z
	defb 0d6h		;24aa	d6		.
	defb 011h		;24ab	11		.
	defb 030h		;24ac	30		0
	defb 001h		;24ad	01		.
	defb 079h		;24ae	79		y
	defb 066h		;24af	66		f
	defb 052h		;24b0	52		R
	defb 0c3h		;24b1	c3		.
	defb 052h		;24b2	52		R
	defb 0f8h		;24b3	f8		.
	defb 03fh		;24b4	3f		?
	defb 0c6h		;24b5	c6		.
	defb 011h		;24b6	11		.
	defb 0feh		;24b7	fe		.
	defb 016h		;24b8	16		.
	defb 038h		;24b9	38		8
	defb 003h		;24ba	03		.
	defb 03eh		;24bb	3e		>
	defb 0f9h		;24bc	f9		.
	defb 097h		;24bd	97		.
	defb 03dh		;24be	3d		=
	defb 076h		;24bf	76		v
	defb 02eh		;24c0	2e		.
	defb 03ch		;24c1	3c		<
	defb 0f3h		;24c2	f3		.
	defb 05fh		;24c3	5f		_
	defb 020h		;24c4	20		 
	defb 0d9h		;24c5	d9		.
	defb 0f1h		;24c6	f1		.
	defb 02bh		;24c7	2b		+
	defb 0b7h		;24c8	b7		.
	defb 04dh		;24c9	4d		M
	defb 045h		;24ca	45		E
	defb 071h		;24cb	71		q
	defb 0dch		;24cc	dc		.
	defb 073h		;24cd	73		s
	defb 03ch		;24ce	3c		<
	defb 072h		;24cf	72		r
	defb 047h		;24d0	47		G
	defb 0fch		;24d1	fc		.
	defb 056h		;24d2	56		V
	defb 02ch		;24d3	2c		,
	defb 09bh		;24d4	9b		.
	defb 026h		;24d5	26		&
	defb 000h		;24d6	00		.
	defb 011h		;24d7	11		.
	defb 0c0h		;24d8	c0		.
	defb 0f0h		;24d9	f0		.
	defb 038h		;24da	38		8
	defb 029h		;24db	29		)
	defb 019h		;24dc	19		.
	defb 0fbh		;24dd	fb		.
	defb 08ah		;24de	8a		.
	defb 06dh		;24df	6d		m
	defb 090h		;24e0	90		.
	defb 099h		;24e1	99		.
	defb 02ah		;24e2	2a		*
	defb 0ech		;24e3	ec		.
	defb 04eh		;24e4	4e		N
	defb 0deh		;24e5	de		.
	defb 0fch		;24e6	fc		.
	defb 09bh		;24e7	9b		.
	defb 0c9h		;24e8	c9		.
	defb 0f5h		;24e9	f5		.
	defb 00ah		;24ea	0a		.
	defb 02fh		;24eb	2f		/
	defb 0dah		;24ec	da		.
	defb 0f1h		;24ed	f1		.
	defb 0dah		;24ee	da		.
	defb 0c9h		;24ef	c9		.
	defb 048h		;24f0	48		H
	defb 091h		;24f1	91		.
	defb 06fh		;24f2	6f		o
	defb 0cbh		;24f3	cb		.
	defb 07eh		;24f4	7e		~
	defb 0d3h		;24f5	d3		.
	defb 0bah		;24f6	ba		.
	defb 0cch		;24f7	cc		.
	defb 05eh		;24f8	5e		^
	defb 023h		;24f9	23		#
	defb 056h		;24fa	56		V
	defb 0ebh		;24fb	eb		.
	defb 0c3h		;24fc	c3		.
	defb 0f0h		;24fd	f0		.
	defb 0f4h		;24fe	f4		.
	defb 0d5h		;24ff	d5		.
	defb 09fh		;2500	9f		.
	defb 052h		;2501	52		R
	defb 0efh		;2502	ef		.
	defb 07eh		;2503	7e		~
	defb 023h		;2504	23		#
	defb 066h		;2505	66		f
	defb 06fh		;2506	6f		o
	defb 031h		;2507	31		1
	defb 086h		;2508	86		.
	defb 0c0h		;2509	c0		.
	defb 06fh		;250a	6f		o
	defb 0bfh		;250b	bf		.
	defb 07fh		;250c	7f		.
	defb 0edh		;250d	ed		.
	defb 0b1h		;250e	b1		.
	defb 03eh		;250f	3e		>
	defb 0ffh		;2510	ff		.
	defb 091h		;2511	91		.
	defb 083h		;2512	83		.
	defb 0c9h		;2513	c9		.
	defb 005h		;2514	05		.
	defb 0c9h		;2515	c9		.
	defb 0deh		;2516	de		.
	defb 03ch		;2517	3c		<
	defb 0f9h		;2518	f9		.
	defb 06ch		;2519	6c		l
	defb 093h		;251a	93		.
	defb 02dh		;251b	2d		-
	defb 06eh		;251c	6e		n
	defb 07ah		;251d	7a		z
	defb 0f2h		;251e	f2		.
	defb 089h		;251f	89		.
	defb 0a6h		;2520	a6		.
	defb 017h		;2521	17		.
	defb 0cch		;2522	cc		.
	defb 0fbh		;2523	fb		.
	defb 04ah		;2524	4a		J
	defb 063h		;2525	63		c
	defb 0cch		;2526	cc		.
	defb 0c4h		;2527	c4		.
	defb 0f5h		;2528	f5		.
	defb 0f5h		;2529	f5		.
	defb 0fbh		;252a	fb		.
	defb 076h		;252b	76		v
	defb 0d5h		;252c	d5		.
	defb 00eh		;252d	0e		.
	defb 084h		;252e	84		.
	defb 0c8h		;252f	c8		.
	defb 0cch		;2530	cc		.
	defb 04fh		;2531	4f		O
	defb 05fh		;2532	5f		_
	defb 001h		;2533	01		.
	defb 0f2h		;2534	f2		.
	defb 050h		;2535	50		P
	defb 07ch		;2536	7c		|
	defb 07bh		;2537	7b		{
	defb 0d1h		;2538	d1		.
	defb 03eh		;2539	3e		>
	defb 020h		;253a	20		 
	defb 014h		;253b	14		.
	defb 0bah		;253c	ba		.
	defb 038h		;253d	38		8
	defb 059h		;253e	59		Y
	defb 0e9h		;253f	e9		.
	defb 095h		;2540	95		.
	defb 01fh		;2541	1f		.
	defb 01eh		;2542	1e		.
	defb 04fh		;2543	4f		O
	defb 0fdh		;2544	fd		.
	defb 0afh		;2545	af		.
	defb 06eh		;2546	6e		n
	defb 035h		;2547	35		5
	defb 0aah		;2548	aa		.
	defb 0bdh		;2549	bd		.
	defb 0afh		;254a	af		.
	defb 000h		;254b	00		.
	defb 05bh		;254c	5b		[
	defb 000h		;254d	00		.
	defb 0e1h		;254e	e1		.
	defb 005h		;254f	05		.
	defb 0a7h		;2550	a7		.
	defb 0f5h		;2551	f5		.
	defb 00ch		;2552	0c		.
	defb 04fh		;2553	4f		O
	defb 0a6h		;2554	a6		.
	defb 0d2h		;2555	d2		.
	defb 088h		;2556	88		.
	defb 0beh		;2557	be		.
	defb 079h		;2558	79		y
	defb 028h		;2559	28		(
	defb 008h		;255a	08		.
	defb 02bh		;255b	2b		+
	defb 046h		;255c	46		F
	defb 0cdh		;255d	cd		.
	defb 0e1h		;255e	e1		.
	defb 09ah		;255f	9a		.
	defb 080h		;2560	80		.
	defb 018h		;2561	18		.
	defb 001h		;2562	01		.
	defb 0aeh		;2563	ae		.
	defb 047h		;2564	47		G
	defb 0f1h		;2565	f1		.
	defb 001h		;2566	01		.
	defb 077h		;2567	77		w
	defb 091h		;2568	91		.
	defb 023h		;2569	23		#
	defb 072h		;256a	72		r
	defb 083h		;256b	83		.
	defb 0b0h		;256c	b0		.
	defb 0f8h		;256d	f8		.
	defb 02bh		;256e	2b		+
	defb 0c0h		;256f	c0		.
	defb 028h		;2570	28		(
	defb 0c7h		;2571	c7		.
	defb 079h		;2572	79		y
	defb 0cdh		;2573	cd		.
	defb 07ah		;2574	7a		z
	defb 0c8h		;2575	c8		.
	defb 090h		;2576	90		.
	defb 0c8h		;2577	c8		.
	defb 08fh		;2578	8f		.
	defb 07fh		;2579	7f		.
	defb 0b6h		;257a	b6		.
	defb 0c8h		;257b	c8		.
	defb 00eh		;257c	0e		.
	defb 000h		;257d	00		.
	defb 02ah		;257e	2a		*
	defb 0fch		;257f	fc		.
	defb 00ch		;2580	0c		.
	defb 0cbh		;2581	cb		.
	defb 008h		;2582	08		.
	defb 030h		;2583	30		0
	defb 0fbh		;2584	fb		.
	defb 006h		;2585	06		.
	defb 080h		;2586	80		.
	defb 000h		;2587	00		.
	defb 06ah		;2588	6a		j
	defb 0f7h		;2589	f7		.
	defb 09fh		;258a	9f		.
	defb 0c9h		;258b	c9		.
	defb 039h		;258c	39		9
	defb 01dh		;258d	1d		.
	defb 0ddh		;258e	dd		.
	defb 001h		;258f	01		.
	defb 008h		;2590	08		.
	defb 08ah		;2591	8a		.
	defb 07eh		;2592	7e		~
	defb 098h		;2593	98		.
	defb 02ch		;2594	2c		,
	defb 0c8h		;2595	c8		.
	defb 046h		;2596	46		F
	defb 060h		;2597	60		`
	defb 0fbh		;2598	fb		.
	defb 0a0h		;2599	a0		.
	defb 00fh		;259a	0f		.
	defb 0b7h		;259b	b7		.
	defb 073h		;259c	73		s
	defb 007h		;259d	07		.
	defb 0b7h		;259e	b7		.
	defb 0cah		;259f	ca		.
	defb 047h		;25a0	47		G
	defb 0e7h		;25a1	e7		.
	defb 0cdh		;25a2	cd		.
	defb 0afh		;25a3	af		.
	defb 00eh		;25a4	0e		.
	defb 0ffh		;25a5	ff		.
	defb 0adh		;25a6	ad		.
	defb 0c0h		;25a7	c0		.
	defb 010h		;25a8	10		.
	defb 0f9h		;25a9	f9		.
	defb 0c3h		;25aa	c3		.
	defb 0a0h		;25ab	a0		.
	defb 0a6h		;25ac	a6		.
	defb 0c5h		;25ad	c5		.
	defb 079h		;25ae	79		y
	defb 0c3h		;25af	c3		.
	defb 05eh		;25b0	5e		^
	defb 0c1h		;25b1	c1		.
	defb 06dh		;25b2	6d		m
	defb 0fbh		;25b3	fb		.
	defb 0c9h		;25b4	c9		.
	defb 016h		;25b5	16		.
	defb 09dh		;25b6	9d		.
	defb 05fh		;25b7	5f		_
	defb 01ah		;25b8	1a		.
	defb 062h		;25b9	62		b
	defb 0e1h		;25ba	e1		.
	defb 078h		;25bb	78		x
	defb 012h		;25bc	12		.
	defb 001h		;25bd	01		.
	defb 00eh		;25be	0e		.
	defb 087h		;25bf	87		.
	defb 06dh		;25c0	6d		m
	defb 026h		;25c1	26		&
	defb 0deh		;25c2	de		.
	defb 04bh		;25c3	4b		K
	defb 0e3h		;25c4	e3		.
	defb 06fh		;25c5	6f		o
	defb 07ch		;25c6	7c		|
	defb 095h		;25c7	95		.
	defb 007h		;25c8	07		.
	defb 067h		;25c9	67		g
	defb 088h		;25ca	88		.
	defb 0a9h		;25cb	a9		.
	defb 02fh		;25cc	2f		/
	defb 0f3h		;25cd	f3		.
	defb 005h		;25ce	05		.
	defb 032h		;25cf	32		2
	defb 09bh		;25d0	9b		.
	defb 03eh		;25d1	3e		>
	defb 03fh		;25d2	3f		?
	defb 05bh		;25d3	5b		[
	defb 0d3h		;25d4	d3		.
	defb 0bch		;25d5	bc		.
	defb 092h		;25d6	92		.
	defb 0f5h		;25d7	f5		.
	defb 0dbh		;25d8	db		.
	defb 0beh		;25d9	be		.
	defb 0a5h		;25da	a5		.
	defb 0f6h		;25db	f6		.
	defb 0eeh		;25dc	ee		.
	defb 076h		;25dd	76		v
	defb 0f7h		;25de	f7		.
	defb 09fh		;25df	9f		.
	defb 00ah		;25e0	0a		.
	defb 0c2h		;25e1	c2		.
	defb 0d5h		;25e2	d5		.
	defb 0fdh		;25e3	fd		.
	defb 054h		;25e4	54		T
	defb 0d1h		;25e5	d1		.
	defb 026h		;25e6	26		&
	defb 09dh		;25e7	9d		.
	defb 0b5h		;25e8	b5		.
	defb 0e8h		;25e9	e8		.
	defb 06ah		;25ea	6a		j
	defb 077h		;25eb	77		w
	defb 03eh		;25ec	3e		>
	defb 040h		;25ed	40		@
	defb 0fbh		;25ee	fb		.
	defb 0f1h		;25ef	f1		.
	defb 0c7h		;25f0	c7		.
	defb 0a4h		;25f1	a4		.
	defb 0d5h		;25f2	d5		.
	defb 044h		;25f3	44		D
	defb 0d4h		;25f4	d4		.
	defb 061h		;25f5	61		a
	defb 095h		;25f6	95		.
	defb 02ah		;25f7	2a		*
	defb 067h		;25f8	67		g
	defb 038h		;25f9	38		8
	defb 07eh		;25fa	7e		~
	defb 059h		;25fb	59		Y
	defb 0d8h		;25fc	d8		.
	defb 00bh		;25fd	0b		.
	defb 0ach		;25fe	ac		.
	defb 0b2h		;25ff	b2		.
	defb 0afh		;2600	af		.
	defb 001h		;2601	01		.
	defb 090h		;2602	90		.
	defb 0c4h		;2603	c4		.
	defb 034h		;2604	34		4
	defb 0aah		;2605	aa		.
	defb 0c3h		;2606	c3		.
	defb 0b3h		;2607	b3		.
	defb 045h		;2608	45		E
	defb 093h		;2609	93		.
	defb 08bh		;260a	8b		.
	defb 09ch		;260b	9c		.
	defb 09ah		;260c	9a		.
	defb 03eh		;260d	3e		>
	defb 08dh		;260e	8d		.
	defb 031h		;260f	31		1
	defb 062h		;2610	62		b
	defb 023h		;2611	23		#
	defb 0d6h		;2612	d6		.
	defb 00fh		;2613	0f		.
	defb 098h		;2614	98		.
	defb 0f1h		;2615	f1		.
	defb 03ch		;2616	3c		<
	defb 047h		;2617	47		G
	defb 03eh		;2618	3e		>
	defb 035h		;2619	35		5
	defb 006h		;261a	06		.
	defb 019h		;261b	19		.
	defb 055h		;261c	55		U
	defb 000h		;261d	00		.
	defb 0d9h		;261e	d9		.
	defb 096h		;261f	96		.
	defb 036h		;2620	36		6
	defb 036h		;2621	36		6
	defb 07eh		;2622	7e		~
	defb 0bah		;2623	ba		.
	defb 0c9h		;2624	c9		.
	defb 04eh		;2625	4e		N
	defb 09ch		;2626	9c		.
	defb 0ach		;2627	ac		.
	defb 01ah		;2628	1a		.
	defb 01dh		;2629	1d		.
	defb 05fh		;262a	5f		_
	defb 02bh		;262b	2b		+
	defb 02eh		;262c	2e		.
	defb 030h		;262d	30		0
	defb 070h		;262e	70		p
	defb 091h		;262f	91		.
	defb 0f4h		;2630	f4		.
	defb 030h		;2631	30		0
	defb 03ah		;2632	3a		:
	defb 03fh		;2633	3f		?
	defb 00bh		;2634	0b		.
	defb 0e1h		;2635	e1		.
	defb 071h		;2636	71		q
	defb 0f3h		;2637	f3		.
	defb 083h		;2638	83		.
	defb 036h		;2639	36		6
	defb 067h		;263a	67		g
	defb 04eh		;263b	4e		N
	defb 04fh		;263c	4f		O
	defb 002h		;263d	02		.
	defb 042h		;263e	42		B
	defb 047h		;263f	47		G
	defb 06fh		;2640	6f		o
	defb 094h		;2641	94		.
	defb 05bh		;2642	5b		[
	defb 05ah		;2643	5a		Z
	defb 070h		;2644	70		p
	defb 085h		;2645	85		.
	defb 01fh		;2646	1f		.
	defb 05fh		;2647	5f		_
	defb 05eh		;2648	5e		^
	defb 021h		;2649	21		!
	defb 05ch		;264a	5c		\
	defb 027h		;264b	27		'
	defb 036h		;264c	36		6
	defb 074h		;264d	74		t
	defb 0fbh		;264e	fb		.
	defb 07fh		;264f	7f		.
	defb 07eh		;2650	7e		~
	defb 05fh		;2651	5f		_
	defb 07ah		;2652	7a		z
	defb 07bh		;2653	7b		{
	defb 037h		;2654	37		7
	defb 013h		;2655	13		.
	defb 02fh		;2656	2f		/
	defb 00ah		;2657	0a		.
	defb 00bh		;2658	0b		.
	defb 0a5h		;2659	a5		.
	defb 007h		;265a	07		.
	defb 00fh		;265b	0f		.
	defb 0e1h		;265c	e1		.
	defb 017h		;265d	17		.
	defb 070h		;265e	70		p
	defb 00dh		;265f	0d		.
	defb 00ah		;2660	0a		.
	defb 074h		;2661	74		t
	defb 004h		;2662	04		.
	defb 068h		;2663	68		h
	defb 0bch		;2664	bc		.
	defb 05fh		;2665	5f		_
	defb 012h		;2666	12		.
	defb 0f5h		;2667	f5		.
	defb 06ch		;2668	6c		l
	defb 0c5h		;2669	c5		.
	defb 007h		;266a	07		.
	defb 002h		;266b	02		.
	defb 077h		;266c	77		w
	defb 00dh		;266d	0d		.
	defb 080h		;266e	80		.
	defb 0fch		;266f	fc		.
	defb 008h		;2670	08		.
	defb 056h		;2671	56		V
	defb 051h		;2672	51		Q
	defb 060h		;2673	60		`
	defb 000h		;2674	00		.
	defb 0cbh		;2675	cb		.
	defb 0f2h		;2676	f2		.
	defb 019h		;2677	19		.
	defb 058h		;2678	58		X
	defb 01ah		;2679	1a		.
	defb 0ffh		;267a	ff		.
	defb 02dh		;267b	2d		-
	defb 0beh		;267c	be		.
	defb 010h		;267d	10		.
	defb 074h		;267e	74		t
	defb 056h		;267f	56		V
	defb 0dbh		;2680	db		.
	defb 003h		;2681	03		.
	defb 08bh		;2682	8b		.
	defb 07fh		;2683	7f		.
	defb 021h		;2684	21		!
	defb 041h		;2685	41		A
	defb 09eh		;2686	9e		.
	defb 03ah		;2687	3a		:
	defb 040h		;2688	40		@
	defb 0bbh		;2689	bb		.
	defb 0f0h		;268a	f0		.
	defb 0beh		;268b	be		.
	defb 028h		;268c	28		(
	defb 0f7h		;268d	f7		.
	defb 0cdh		;268e	cd		.
	defb 0c2h		;268f	c2		.
	defb 07bh		;2690	7b		{
	defb 0a7h		;2691	a7		.
	defb 0c9h		;2692	c9		.
	defb 036h		;2693	36		6
	defb 0dah		;2694	da		.
	defb 0c8h		;2695	c8		.
	defb 07dh		;2696	7d		}
	defb 0d2h		;2697	d2		.
	defb 087h		;2698	87		.
	defb 04fh		;2699	4f		O
	defb 0edh		;269a	ed		.
	defb 04bh		;269b	4b		K
	defb 042h		;269c	42		B
	defb 0c8h		;269d	c8		.
	defb 0efh		;269e	ef		.
	defb 03eh		;269f	3e		>
	defb 000h		;26a0	00		.
	defb 0c8h		;26a1	c8		.
	defb 03dh		;26a2	3d		=
	defb 07dh		;26a3	7d		}
	defb 078h		;26a4	78		x
	defb 06eh		;26a5	6e		n
	defb 026h		;26a6	26		&
	defb 05eh		;26a7	5e		^
	defb 02ch		;26a8	2c		,
	defb 056h		;26a9	56		V
	defb 099h		;26aa	99		.
	defb 09eh		;26ab	9e		.
	defb 046h		;26ac	46		F
	defb 04eh		;26ad	4e		N
	defb 0dch		;26ae	dc		.
	defb 0b3h		;26af	b3		.
	defb 033h		;26b0	33		3
	defb 0feh		;26b1	fe		.
	defb 041h		;26b2	41		A
	defb 032h		;26b3	32		2
	defb 03eh		;26b4	3e		>
	defb 0f2h		;26b5	f2		.
	defb 0e1h		;26b6	e1		.
	defb 02fh		;26b7	2f		/
	defb 0b8h		;26b8	b8		.
	defb 038h		;26b9	38		8
	defb 004h		;26ba	04		.
	defb 001h		;26bb	01		.
	defb 037h		;26bc	37		7
	defb 0c9h		;26bd	c9		.
	defb 0edh		;26be	ed		.
	defb 032h		;26bf	32		2
	defb 011h		;26c0	11		.
	defb 030h		;26c1	30		0
	defb 098h		;26c2	98		.
	defb 0afh		;26c3	af		.
	defb 048h		;26c4	48		H
	defb 0d7h		;26c5	d7		.
	defb 0d1h		;26c6	d1		.
	defb 050h		;26c7	50		P
	defb 0e8h		;26c8	e8		.
	defb 0ddh		;26c9	dd		.
	defb 0bch		;26ca	bc		.
	defb 0d6h		;26cb	d6		.
	defb 004h		;26cc	04		.
	defb 0e6h		;26cd	e6		.
	defb 03fh		;26ce	3f		?
	defb 0a1h		;26cf	a1		.
	defb 02ch		;26d0	2c		,
	defb 07eh		;26d1	7e		~
	defb 034h		;26d2	34		4
	defb 0e6h		;26d3	e6		.
	defb 05fh		;26d4	5f		_
	defb 0cbh		;26d5	cb		.
	defb 0b6h		;26d6	b6		.
	defb 06fh		;26d7	6f		o
	defb 035h		;26d8	35		5
	defb 0a6h		;26d9	a6		.
	defb 0c5h		;26da	c5		.
	defb 073h		;26db	73		s
	defb 0eah		;26dc	ea		.
	defb 072h		;26dd	72		r
	defb 070h		;26de	70		p
	defb 071h		;26df	71		q
	defb 0bfh		;26e0	bf		.
	defb 0f8h		;26e1	f8		.
	defb 0b2h		;26e2	b2		.
	defb 03dh		;26e3	3d		=
	defb 063h		;26e4	63		c
	defb 007h		;26e5	07		.
	defb 07eh		;26e6	7e		~
	defb 0abh		;26e7	ab		.
	defb 0c9h		;26e8	c9		.
	defb 008h		;26e9	08		.
	defb 0ddh		;26ea	dd		.
	defb 0cbh		;26eb	cb		.
	defb 003h		;26ec	03		.
	defb 028h		;26ed	28		(
	defb 00bh		;26ee	0b		.
	defb 017h		;26ef	17		.
	defb 0cdh		;26f0	cd		.
	defb 0d9h		;26f1	d9		.
	defb 011h		;26f2	11		.
	defb 0e6h		;26f3	e6		.
	defb 061h		;26f4	61		a
	defb 032h		;26f5	32		2
	defb 0cdh		;26f6	cd		.
	defb 061h		;26f7	61		a
	defb 0ebh		;26f8	eb		.
	defb 057h		;26f9	57		W
	defb 0a3h		;26fa	a3		.
	defb 0d9h		;26fb	d9		.
	defb 008h		;26fc	08		.
	defb 05bh		;26fd	5b		[
	defb 01fh		;26fe	1f		.
	defb 0beh		;26ff	be		.
	defb 07bh		;2700	7b		{
	defb 0dbh		;2701	db		.
	defb 019h		;2702	19		.
	defb 0cbh		;2703	cb		.
	defb 047h		;2704	47		G
	defb 0c8h		;2705	c8		.
	defb 018h		;2706	18		.
	defb 0feh		;2707	fe		.
	defb 0cbh		;2708	cb		.
	defb 0d9h		;2709	d9		.
	defb 0f0h		;270a	f0		.
	defb 028h		;270b	28		(
	defb 065h		;270c	65		e
	defb 0e0h		;270d	e0		.
	defb 05bh		;270e	5b		[
	defb 037h		;270f	37		7
	defb 039h		;2710	39		9
	defb 0e1h		;2711	e1		.
	defb 063h		;2712	63		c
	defb 0d6h		;2713	d6		.
	defb 002h		;2714	02		.
	defb 076h		;2715	76		v
	defb 0c5h		;2716	c5		.
	defb 00bh		;2717	0b		.
	defb 020h		;2718	20		 
	defb 06fh		;2719	6f		o
	defb 0cdh		;271a	cd		.
	defb 090h		;271b	90		.
	defb 0a1h		;271c	a1		.
	defb 03ah		;271d	3a		:
	defb 0d5h		;271e	d5		.
	defb 031h		;271f	31		1
	defb 0a0h		;2720	a0		.
	defb 0beh		;2721	be		.
	defb 037h		;2722	37		7
	defb 0b9h		;2723	b9		.
	defb 0aeh		;2724	ae		.
	defb 042h		;2725	42		B
	defb 0b4h		;2726	b4		.
	defb 0a1h		;2727	a1		.
	defb 0aeh		;2728	ae		.
	defb 0d7h		;2729	d7		.
	defb 021h		;272a	21		!
	defb 000h		;272b	00		.
	defb 01ch		;272c	1c		.
	defb 00ah		;272d	0a		.
	defb 0cch		;272e	cc		.
	defb 085h		;272f	85		.
	defb 09fh		;2730	9f		.
	defb 0dbh		;2731	db		.
	defb 07bh		;2732	7b		{
	defb 050h		;2733	50		P
	defb 0abh		;2734	ab		.
	defb 05eh		;2735	5e		^
	defb 09eh		;2736	9e		.
	defb 05ch		;2737	5c		\
	defb 0dbh		;2738	db		.
	defb 049h		;2739	49		I
	defb 05eh		;273a	5e		^
	defb 045h		;273b	45		E
	defb 0c9h		;273c	c9		.
	defb 0f5h		;273d	f5		.
	defb 0dah		;273e	da		.
	defb 0b0h		;273f	b0		.
	defb 0dah		;2740	da		.
	defb 05eh		;2741	5e		^
	defb 048h		;2742	48		H
	defb 05eh		;2743	5e		^
	defb 0f5h		;2744	f5		.
	defb 0cdh		;2745	cd		.
	defb 0cfh		;2746	cf		.
	defb 0fbh		;2747	fb		.
	defb 0dah		;2748	da		.
	defb 08eh		;2749	8e		.
	defb 09fh		;274a	9f		.
	defb 053h		;274b	53		S
	defb 05ch		;274c	5c		\
	defb 015h		;274d	15		.
	defb 0cdh		;274e	cd		.
	defb 0a3h		;274f	a3		.
	defb 0abh		;2750	ab		.
	defb 01bh		;2751	1b		.
	defb 092h		;2752	92		.
	defb 0bah		;2753	ba		.
	defb 0feh		;2754	fe		.
	defb 018h		;2755	18		.
	defb 090h		;2756	90		.
	defb 068h		;2757	68		h
	defb 056h		;2758	56		V
	defb 0f6h		;2759	f6		.
	defb 08ah		;275a	8a		.
	defb 0b3h		;275b	b3		.
	defb 0fah		;275c	fa		.
	defb 0eeh		;275d	ee		.
	defb 0d1h		;275e	d1		.
	defb 047h		;275f	47		G
	defb 084h		;2760	84		.
	defb 0edh		;2761	ed		.
	defb 09ah		;2762	9a		.
	defb 05dh		;2763	5d		]
	defb 099h		;2764	99		.
	defb 0d6h		;2765	d6		.
	defb 09fh		;2766	9f		.
	defb 030h		;2767	30		0
	defb 07dh		;2768	7d		}
	defb 099h		;2769	99		.
	defb 026h		;276a	26		&
	defb 000h		;276b	00		.
	defb 022h		;276c	22		"
	defb 046h		;276d	46		F
	defb 0d9h		;276e	d9		.
	defb 07eh		;276f	7e		~
	defb 0c8h		;2770	c8		.
	defb 070h		;2771	70		p
	defb 0eeh		;2772	ee		.
	defb 001h		;2773	01		.
	defb 094h		;2774	94		.
	defb 075h		;2775	75		u
	defb 090h		;2776	90		.
	defb 0d0h		;2777	d0		.
	defb 06eh		;2778	6e		n
	defb 0c8h		;2779	c8		.
	defb 01ch		;277a	1c		.
	defb 06eh		;277b	6e		n
	defb 066h		;277c	66		f
	defb 0afh		;277d	af		.
	defb 0e8h		;277e	e8		.
	defb 04eh		;277f	4e		N
	defb 0fdh		;2780	fd		.
	defb 002h		;2781	02		.
	defb 0dfh		;2782	df		.
	defb 0bah		;2783	ba		.
	defb 0e0h		;2784	e0		.
	defb 0aeh		;2785	ae		.
	defb 03ah		;2786	3a		:
	defb 0cdh		;2787	cd		.
	defb 067h		;2788	67		g
	defb 031h		;2789	31		1
	defb 0deh		;278a	de		.
	defb 0e1h		;278b	e1		.
	defb 06ah		;278c	6a		j
	defb 0b4h		;278d	b4		.
	defb 040h		;278e	40		@
	defb 0e4h		;278f	e4		.
	defb 089h		;2790	89		.
	defb 015h		;2791	15		.
	defb 02fh		;2792	2f		/
	defb 076h		;2793	76		v
	defb 0c8h		;2794	c8		.
	defb 0fbh		;2795	fb		.
	defb 07bh		;2796	7b		{
	defb 0b6h		;2797	b6		.
	defb 020h		;2798	20		 
	defb 0f9h		;2799	f9		.
	defb 0f3h		;279a	f3		.
	defb 05fh		;279b	5f		_
	defb 09bh		;279c	9b		.
	defb 0dah		;279d	da		.
	defb 004h		;279e	04		.
	defb 0dah		;279f	da		.
	defb 07dh		;27a0	7d		}
	defb 0feh		;27a1	fe		.
	defb 037h		;27a2	37		7
	defb 0aeh		;27a3	ae		.
	defb 0a6h		;27a4	a6		.
	defb 020h		;27a5	20		 
	defb 00eh		;27a6	0e		.
	defb 0d5h		;27a7	d5		.
	defb 096h		;27a8	96		.
	defb 0deh		;27a9	de		.
	defb 06ah		;27aa	6a		j
	defb 046h		;27ab	46		F
	defb 0c0h		;27ac	c0		.
	defb 0a6h		;27ad	a6		.
	defb 0c9h		;27ae	c9		.
	defb 0ceh		;27af	ce		.
	defb 066h		;27b0	66		f
	defb 0ffh		;27b1	ff		.
	defb 039h		;27b2	39		9
	defb 097h		;27b3	97		.
	defb 0abh		;27b4	ab		.
	defb 086h		;27b5	86		.
	defb 056h		;27b6	56		V
	defb 0aeh		;27b7	ae		.
	defb 0b3h		;27b8	b3		.
	defb 036h		;27b9	36		6
	defb 057h		;27ba	57		W
	defb 09dh		;27bb	9d		.
	defb 09eh		;27bc	9e		.
	defb 09dh		;27bd	9d		.
	defb 02dh		;27be	2d		-
	defb 04eh		;27bf	4e		N
	defb 0aeh		;27c0	ae		.
	defb 03bh		;27c1	3b		;
	defb 0aeh		;27c2	ae		.
	defb 03ah		;27c3	3a		:
	defb 057h		;27c4	57		W
	defb 09dh		;27c5	9d		.
	defb 08eh		;27c6	8e		.
	defb 067h		;27c7	67		g
	defb 02fh		;27c8	2f		/
	defb 05eh		;27c9	5e		^
	defb 07bh		;27ca	7b		{
	defb 04ch		;27cb	4c		L
	defb 029h		;27cc	29		)
	defb 0ffh		;27cd	ff		.
	defb 005h		;27ce	05		.
	defb 07bh		;27cf	7b		{
	defb 0cfh		;27d0	cf		.
	defb 0beh		;27d1	be		.
	defb 034h		;27d2	34		4
	defb 07bh		;27d3	7b		{
	defb 02eh		;27d4	2e		.
	defb 004h		;27d5	04		.
	defb 0d5h		;27d6	d5		.
	defb 0dch		;27d7	dc		.
	defb 0b6h		;27d8	b6		.
	defb 0a5h		;27d9	a5		.
	defb 009h		;27da	09		.
	defb 0b9h		;27db	b9		.
	defb 06ah		;27dc	6a		j
	defb 0d6h		;27dd	d6		.
	defb 0dah		;27de	da		.
	defb 06ah		;27df	6a		j
	defb 0e6h		;27e0	e6		.
	defb 0aah		;27e1	aa		.
	defb 094h		;27e2	94		.
	defb 077h		;27e3	77		w
	defb 0c6h		;27e4	c6		.
	defb 0dah		;27e5	da		.
	defb 0cch		;27e6	cc		.
	defb 0afh		;27e7	af		.
	defb 072h		;27e8	72		r
	defb 075h		;27e9	75		u
	defb 0deh		;27ea	de		.
	defb 0eeh		;27eb	ee		.
	defb 0b4h		;27ec	b4		.
	defb 0d5h		;27ed	d5		.
	defb 0b4h		;27ee	b4		.
	defb 029h		;27ef	29		)
	defb 0efh		;27f0	ef		.
	defb 0ceh		;27f1	ce		.
	defb 041h		;27f2	41		A
	defb 0bch		;27f3	bc		.
	defb 0b9h		;27f4	b9		.
	defb 0d8h		;27f5	d8		.
	defb 07ch		;27f6	7c		|
	defb 0b9h		;27f7	b9		.
	defb 0f6h		;27f8	f6		.
	defb 0cbh		;27f9	cb		.
	defb 000h		;27fa	00		.
	defb 08ah		;27fb	8a		.
	defb 0e7h		;27fc	e7		.
	defb 0e1h		;27fd	e1		.
	defb 01bh		;27fe	1b		.
	defb 043h		;27ff	43		C
	defb 03fh		;2800	3f		?
	defb 03dh		;2801	3d		=
	defb 03bh		;2802	3b		;
	defb 03ch		;2803	3c		<
	defb 046h		;2804	46		F
	defb 050h		;2805	50		P
	defb 07dh		;2806	7d		}
	defb 044h		;2807	44		D
	defb 042h		;2808	42		B
	defb 040h		;2809	40		@
	defb 03eh		;280a	3e		>
	defb 00fh		;280b	0f		.
	defb 0d7h		;280c	d7		.
	defb 0c5h		;280d	c5		.
	defb 037h		;280e	37		7
	defb 029h		;280f	29		)
	defb 036h		;2810	36		6
	defb 010h		;2811	10		.
	defb 0fdh		;2812	fd		.
	defb 0d1h		;2813	d1		.
	defb 02ch		;2814	2c		,
	defb 02ah		;2815	2a		*
	defb 01eh		;2816	1e		.
	defb 01dh		;2817	1d		.
	defb 011h		;2818	11		.
	defb 0fch		;2819	fc		.
	defb 07dh		;281a	7d		}
	defb 027h		;281b	27		'
	defb 02ch		;281c	2c		,
	defb 02bh		;281d	2b		+
	defb 01fh		;281e	1f		.
	defb 012h		;281f	12		.
	defb 005h		;2820	05		.
	defb 004h		;2821	04		.
	defb 0fch		;2822	fc		.
	defb 0f8h		;2823	f8		.
	defb 038h		;2824	38		8
	defb 02dh		;2825	2d		-
	defb 020h		;2826	20		 
	defb 014h		;2827	14		.
	defb 013h		;2828	13		.
	defb 006h		;2829	06		.
	defb 0fdh		;282a	fd		.
	defb 0f8h		;282b	f8		.
	defb 02fh		;282c	2f		/
	defb 02eh		;282d	2e		.
	defb 022h		;282e	22		"
	defb 021h		;282f	21		!
	defb 015h		;2830	15		.
	defb 007h		;2831	07		.
	defb 0ach		;2832	ac		.
	defb 040h		;2833	40		@
	defb 030h		;2834	30		0
	defb 0e7h		;2835	e7		.
	defb 0d3h		;2836	d3		.
	defb 0f8h		;2837	f8		.
	defb 009h		;2838	09		.
	defb 031h		;2839	31		1
	defb 0e3h		;283a	e3		.
	defb 0f3h		;283b	f3		.
	defb 024h		;283c	24		$
	defb 017h		;283d	17		.
	defb 018h		;283e	18		.
	defb 00bh		;283f	0b		.
	defb 00ah		;2840	0a		.
	defb 032h		;2841	32		2
	defb 022h		;2842	22		"
	defb 0f5h		;2843	f5		.
	defb 033h		;2844	33		3
	defb 025h		;2845	25		%
	defb 026h		;2846	26		&
	defb 019h		;2847	19		.
	defb 00ch		;2848	0c		.
	defb 027h		;2849	27		'
	defb 07dh		;284a	7d		}
	defb 036h		;284b	36		6
	defb 01ah		;284c	1a		.
	defb 00dh		;284d	0d		.
	defb 02ah		;284e	2a		*
	defb 07bh		;284f	7b		{
	defb 01ch		;2850	1c		.
	defb 034h		;2851	34		4
	defb 028h		;2852	28		(
	defb 01bh		;2853	1b		.
	defb 035h		;2854	35		5
	defb 0fch		;2855	fc		.
	defb 0f6h		;2856	f6		.
	defb 00eh		;2857	0e		.
	defb 08dh		;2858	8d		.
	defb 0fdh		;2859	fd		.
	defb 051h		;285a	51		Q
	defb 0cch		;285b	cc		.
	defb 0aah		;285c	aa		.
	defb 054h		;285d	54		T
	defb 057h		;285e	57		W
	defb 0d8h		;285f	d8		.
	defb 046h		;2860	46		F
	defb 050h		;2861	50		P
	defb 04fh		;2862	4f		O
	defb 052h		;2863	52		R
	defb 055h		;2864	55		U
	defb 056h		;2865	56		V
	defb 058h		;2866	58		X
	defb 001h		;2867	01		.
	defb 049h		;2868	49		I
	defb 045h		;2869	45		E
	defb 04dh		;286a	4d		M
	defb 053h		;286b	53		S
	defb 04ch		;286c	4c		L
	defb 04bh		;286d	4b		K
	defb 059h		;286e	59		Y
	defb 048h		;286f	48		H
	defb 0f8h		;2870	f8		.
	defb 077h		;2871	77		w
	defb 041h		;2872	41		A
	defb 0bfh		;2873	bf		.
	defb 0afh		;2874	af		.
	defb 071h		;2875	71		q
	defb 0ebh		;2876	eb		.
	defb 07eh		;2877	7e		~
	defb 028h		;2878	28		(
	defb 01ah		;2879	1a		.
	defb 0feh		;287a	fe		.
	defb 0dch		;287b	dc		.
	defb 0ech		;287c	ec		.
	defb 011h		;287d	11		.
	defb 02eh		;287e	2e		.
	defb 039h		;287f	39		9
	defb 0c8h		;2880	c8		.
	defb 014h		;2881	14		.
	defb 03ah		;2882	3a		:
	defb 0b3h		;2883	b3		.
	defb 0fbh		;2884	fb		.
	defb 05ah		;2885	5a		Z
	defb 0d8h		;2886	d8		.
	defb 06ch		;2887	6c		l
	defb 04eh		;2888	4e		N
	defb 04ah		;2889	4a		J
	defb 0dch		;288a	dc		.
	defb 0c7h		;288b	c7		.
	defb 07ch		;288c	7c		|
	defb 013h		;288d	13		.
	defb 032h		;288e	32		2
	defb 04ah		;288f	4a		J
	defb 022h		;2890	22		"
	defb 0a1h		;2891	a1		.
	defb 097h		;2892	97		.
	defb 0e6h		;2893	e6		.
	defb 06eh		;2894	6e		n
	defb 0c9h		;2895	c9		.
	defb 055h		;2896	55		U
	defb 01eh		;2897	1e		.
	defb 0d9h		;2898	d9		.
	defb 07eh		;2899	7e		~
	defb 090h		;289a	90		.
	defb 0c0h		;289b	c0		.
	defb 06fh		;289c	6f		o
	defb 003h		;289d	03		.
	defb 020h		;289e	20		 
	defb 0cbh		;289f	cb		.
	defb 0fah		;28a0	fa		.
	defb 086h		;28a1	86		.
	defb 0d6h		;28a2	d6		.
	defb 0d3h		;28a3	d3		.
	defb 055h		;28a4	55		U
	defb 060h		;28a5	60		`
	defb 05fh		;28a6	5f		_
	defb 0fdh		;28a7	fd		.
	defb 0c0h		;28a8	c0		.
	defb 0fah		;28a9	fa		.
	defb 046h		;28aa	46		F
	defb 079h		;28ab	79		y
	defb 078h		;28ac	78		x
	defb 0a3h		;28ad	a3		.
	defb 0a2h		;28ae	a2		.
	defb 020h		;28af	20		 
	defb 003h		;28b0	03		.
	defb 0efh		;28b1	ef		.
	defb 0a1h		;28b2	a1		.
	defb 0adh		;28b3	ad		.
	defb 039h		;28b4	39		9
	defb 0a8h		;28b5	a8		.
	defb 009h		;28b6	09		.
	defb 05eh		;28b7	5e		^
	defb 0c9h		;28b8	c9		.
	defb 09ah		;28b9	9a		.
	defb 0a2h		;28ba	a2		.
	defb 03bh		;28bb	3b		;
	defb 099h		;28bc	99		.
	defb 028h		;28bd	28		(
	defb 0cfh		;28be	cf		.
	defb 0f6h		;28bf	f6		.
	defb 0fdh		;28c0	fd		.
	defb 0a2h		;28c1	a2		.
	defb 079h		;28c2	79		y
	defb 037h		;28c3	37		7
	defb 0d2h		;28c4	d2		.
	defb 0c9h		;28c5	c9		.
	defb 060h		;28c6	60		`
	defb 01bh		;28c7	1b		.
	defb 063h		;28c8	63		c
	defb 091h		;28c9	91		.
	defb 003h		;28ca	03		.
	defb 062h		;28cb	62		b
	defb 0e6h		;28cc	e6		.
	defb 0f3h		;28cd	f3		.
	defb 030h		;28ce	30		0
	defb 02dh		;28cf	2d		-
	defb 03dh		;28d0	3d		=
	defb 040h		;28d1	40		@
	defb 062h		;28d2	62		b
	defb 0c6h		;28d3	c6		.
	defb 071h		;28d4	71		q
	defb 077h		;28d5	77		w
	defb 065h		;28d6	65		e
	defb 072h		;28d7	72		r
	defb 074h		;28d8	74		t
	defb 079h		;28d9	79		y
	defb 075h		;28da	75		u
	defb 069h		;28db	69		i
	defb 06fh		;28dc	6f		o
	defb 070h		;28dd	70		p
	defb 05bh		;28de	5b		[
	defb 05dh		;28df	5d		]
	defb 000h		;28e0	00		.
	defb 061h		;28e1	61		a
	defb 073h		;28e2	73		s
	defb 064h		;28e3	64		d
	defb 066h		;28e4	66		f
	defb 067h		;28e5	67		g
	defb 068h		;28e6	68		h
	defb 06ah		;28e7	6a		j
	defb 06bh		;28e8	6b		k
	defb 06ch		;28e9	6c		l
	defb 03bh		;28ea	3b		;
	defb 027h		;28eb	27		'
	defb 03eh		;28ec	3e		>
	defb 05ah		;28ed	5a		Z
	defb 006h		;28ee	06		.
	defb 07ah		;28ef	7a		z
	defb 078h		;28f0	78		x
	defb 063h		;28f1	63		c
	defb 076h		;28f2	76		v
	defb 062h		;28f3	62		b
	defb 06eh		;28f4	6e		n
	defb 06dh		;28f5	6d		m
	defb 02ch		;28f6	2c		,
	defb 02eh		;28f7	2e		.
	defb 02fh		;28f8	2f		/
	defb 000h		;28f9	00		.
	defb 05ch		;28fa	5c		\
	defb 069h		;28fb	69		i
	defb 0fdh		;28fc	fd		.
	defb 05fh		;28fd	5f		_
	defb 059h		;28fe	59		Y
	defb 04bh		;28ff	4b		K
	defb 0ffh		;2900	ff		.
	defb 02fh		;2901	2f		/
	defb 02ah		;2902	2a		*
	defb 02dh		;2903	2d		-
	defb 02bh		;2904	2b		+
	defb 018h		;2905	18		.
	defb 0fbh		;2906	fb		.
	defb 0f2h		;2907	f2		.
	defb 033h		;2908	33		3
	defb 011h		;2909	11		.
	defb 07eh		;290a	7e		~
	defb 01bh		;290b	1b		.
	defb 021h		;290c	21		!
	defb 040h		;290d	40		@
	defb 023h		;290e	23		#
	defb 024h		;290f	24		$
	defb 025h		;2910	25		%
	defb 05eh		;2911	5e		^
	defb 026h		;2912	26		&
	defb 02ah		;2913	2a		*
	defb 028h		;2914	28		(
	defb 029h		;2915	29		)
	defb 05fh		;2916	5f		_
	defb 02bh		;2917	2b		+
	defb 0a6h		;2918	a6		.
	defb 037h		;2919	37		7
	defb 010h		;291a	10		.
	defb 051h		;291b	51		Q
	defb 057h		;291c	57		W
	defb 045h		;291d	45		E
	defb 052h		;291e	52		R
	defb 054h		;291f	54		T
	defb 059h		;2920	59		Y
	defb 055h		;2921	55		U
	defb 049h		;2922	49		I
	defb 04fh		;2923	4f		O
	defb 050h		;2924	50		P
	defb 07bh		;2925	7b		{
	defb 07dh		;2926	7d		}
	defb 01dh		;2927	1d		.
	defb 053h		;2928	53		S
	defb 044h		;2929	44		D
	defb 06fh		;292a	6f		o
	defb 0feh		;292b	fe		.
	defb 046h		;292c	46		F
	defb 047h		;292d	47		G
	defb 048h		;292e	48		H
	defb 04ah		;292f	4a		J
	defb 04bh		;2930	4b		K
	defb 04ch		;2931	4c		L
	defb 03ah		;2932	3a		:
	defb 022h		;2933	22		"
	defb 0cch		;2934	cc		.
	defb 05ah		;2935	5a		Z
	defb 058h		;2936	58		X
	defb 043h		;2937	43		C
	defb 02ch		;2938	2c		,
	defb 0fdh		;2939	fd		.
	defb 056h		;293a	56		V
	defb 042h		;293b	42		B
	defb 04eh		;293c	4e		N
	defb 04dh		;293d	4d		M
	defb 03ch		;293e	3c		<
	defb 03eh		;293f	3e		>
	defb 03fh		;2940	3f		?
	defb 0edh		;2941	ed		.
	defb 07ch		;2942	7c		|
	defb 0ach		;2943	ac		.
	defb 011h		;2944	11		.
	defb 0a6h		;2945	a6		.
	defb 02fh		;2946	2f		/
	defb 008h		;2947	08		.
	defb 04ch		;2948	4c		L
	defb 02fh		;2949	2f		/
	defb 0d5h		;294a	d5		.
	defb 0a6h		;294b	a6		.
	defb 04ch		;294c	4c		L
	defb 0abh		;294d	ab		.
	defb 0c5h		;294e	c5		.
	defb 0a6h		;294f	a6		.
	defb 04ch		;2950	4c		L
	defb 083h		;2951	83		.
	defb 0d5h		;2952	d5		.
	defb 0a6h		;2953	a6		.
	defb 0fah		;2954	fa		.
	defb 095h		;2955	95		.
	defb 04ch		;2956	4c		L
	defb 0f8h		;2957	f8		.
	defb 025h		;2958	25		%
	defb 0f2h		;2959	f2		.
	defb 04ch		;295a	4c		L
	defb 07ah		;295b	7a		z
	defb 035h		;295c	35		5
	defb 0f2h		;295d	f2		.
	defb 04ch		;295e	4c		L
	defb 052h		;295f	52		R
	defb 030h		;2960	30		0
	defb 0f2h		;2961	f2		.
	defb 0f6h		;2962	f6		.
	defb 0ffh		;2963	ff		.
	defb 04ch		;2964	4c		L
	defb 03eh		;2965	3e		>
	defb 010h		;2966	10		.
	defb 0d3h		;2967	d3		.
	defb 0feh		;2968	fe		.
	defb 042h		;2969	42		B
	defb 04bh		;296a	4b		K
	defb 00bh		;296b	0b		.
	defb 078h		;296c	78		x
	defb 0b1h		;296d	b1		.
	defb 020h		;296e	20		 
	defb 0fbh		;296f	fb		.
	defb 0f5h		;2970	f5		.
	defb 055h		;2971	55		U
	defb 000h		;2972	00		.
	defb 0f4h		;2973	f4		.
	defb 056h		;2974	56		V
	defb 0fbh		;2975	fb		.
	defb 0d4h		;2976	d4		.
	defb 05eh		;2977	5e		^
	defb 0e5h		;2978	e5		.
	defb 0c9h		;2979	c9		.
	defb 019h		;297a	19		.
	defb 07eh		;297b	7e		~
	defb 038h		;297c	38		8
	defb 001h		;297d	01		.
	defb 0c5h		;297e	c5		.
	defb 036h		;297f	36		6
	defb 003h		;2980	03		.
	defb 0abh		;2981	ab		.
	defb 05ch		;2982	5c		\
	defb 0c1h		;2983	c1		.
	defb 072h		;2984	72		r
	defb 095h		;2985	95		.
	defb 004h		;2986	04		.
	defb 007h		;2987	07		.
	defb 055h		;2988	55		U
	defb 0aeh		;2989	ae		.
	defb 005h		;298a	05		.
	defb 0f9h		;298b	f9		.
	defb 0c9h		;298c	c9		.
	defb 062h		;298d	62		b
	defb 0f4h		;298e	f4		.
	defb 0ech		;298f	ec		.
	defb 0c9h		;2990	c9		.
	defb 02ch		;2991	2c		,
	defb 0bah		;2992	ba		.
	defb 061h		;2993	61		a
	defb 096h		;2994	96		.
	defb 0cbh		;2995	cb		.
	defb 0b3h		;2996	b3		.
	defb 064h		;2997	64		d
	defb 0e3h		;2998	e3		.
	defb 0bbh		;2999	bb		.
	defb 07bh		;299a	7b		{
	defb 0d6h		;299b	d6		.
	defb 079h		;299c	79		y
	defb 0b0h		;299d	b0		.
	defb 0e5h		;299e	e5		.
	defb 035h		;299f	35		5
	defb 0d1h		;29a0	d1		.
	defb 0b3h		;29a1	b3		.
	defb 0bch		;29a2	bc		.
	defb 085h		;29a3	85		.
	defb 0aeh		;29a4	ae		.
	defb 0d7h		;29a5	d7		.
	defb 03dh		;29a6	3d		=
	defb 0bbh		;29a7	bb		.
	defb 016h		;29a8	16		.
	defb 0bdh		;29a9	bd		.
	defb 055h		;29aa	55		U
	defb 08ah		;29ab	8a		.
	defb 08ch		;29ac	8c		.
	defb 01bh		;29ad	1b		.
	defb 0dah		;29ae	da		.
	defb 02ah		;29af	2a		*
	defb 0beh		;29b0	be		.
	defb 057h		;29b1	57		W
	defb 0c0h		;29b2	c0		.
	defb 000h		;29b3	00		.
	defb 0dfh		;29b4	df		.
	defb 0b3h		;29b5	b3		.
	defb 0f0h		;29b6	f0		.
	defb 003h		;29b7	03		.
	defb 007h		;29b8	07		.
	defb 04ch		;29b9	4c		L
	defb 061h		;29ba	61		a
	defb 017h		;29bb	17		.
	defb 0c4h		;29bc	c4		.
	defb 06eh		;29bd	6e		n
	defb 067h		;29be	67		g
	defb 075h		;29bf	75		u
	defb 065h		;29c0	65		e
	defb 017h		;29c1	17		.
	defb 0fch		;29c2	fc		.
	defb 020h		;29c3	20		 
	defb 028h		;29c4	28		(
	defb 09fh		;29c5	9f		.
	defb 0a7h		;29c6	a7		.
	defb 0ebh		;29c7	eb		.
	defb 0aah		;29c8	aa		.
	defb 029h		;29c9	29		)
	defb 0f9h		;29ca	f9		.
	defb 0f5h		;29cb	f5		.
	defb 03ah		;29cc	3a		:
	defb 0e7h		;29cd	e7		.
	defb 0b1h		;29ce	b1		.
	defb 053h		;29cf	53		S
	defb 00eh		;29d0	0e		.
	defb 004h		;29d1	04		.
	defb 045h		;29d2	45		E
	defb 09eh		;29d3	9e		.
	defb 007h		;29d4	07		.
	defb 06ch		;29d5	6c		l
	defb 069h		;29d6	69		i
	defb 073h		;29d7	73		s
	defb 068h		;29d8	68		h
	defb 043h		;29d9	43		C
	defb 098h		;29da	98		.
	defb 052h		;29db	52		R
	defb 075h		;29dc	75		u
	defb 07bh		;29dd	7b		{
	defb 0cch		;29de	cc		.
	defb 069h		;29df	69		i
	defb 0d2h		;29e0	d2		.
	defb 0e0h		;29e1	e0		.
	defb 0ffh		;29e2	ff		.
	defb 003h		;29e3	03		.
	defb 008h		;29e4	08		.
	defb 04dh		;29e5	4d		M
	defb 065h		;29e6	65		e
	defb 06dh		;29e7	6d		m
	defb 06fh		;29e8	6f		o
	defb 072h		;29e9	72		r
	defb 079h		;29ea	79		y
	defb 020h		;29eb	20		 
	defb 054h		;29ec	54		T
	defb 0ffh		;29ed	ff		.
	defb 099h		;29ee	99		.
	defb 0fah		;29ef	fa		.
	defb 074h		;29f0	74		t
	defb 06bh		;29f1	6b		k
	defb 0dfh		;29f2	df		.
	defb 0cbh		;29f3	cb		.
	defb 06fh		;29f4	6f		o
	defb 01eh		;29f5	1e		.
	defb 080h		;29f6	80		.
	defb 044h		;29f7	44		D
	defb 0ceh		;29f8	ce		.
	defb 061h		;29f9	61		a
	defb 062h		;29fa	62		b
	defb 06ch		;29fb	6c		l
	defb 0ebh		;29fc	eb		.
	defb 0cch		;29fd	cc		.
	defb 065h		;29fe	65		e
	defb 064h		;29ff	64		d
	defb 000h		;2a00	00		.
	defb 0c2h		;2a01	c2		.
	defb 0f3h		;2a02	f3		.
	defb 009h		;2a03	09		.
	defb 0cbh		;2a04	cb		.
	defb 009h		;2a05	09		.
	defb 053h		;2a06	53		S
	defb 061h		;2a07	61		a
	defb 066h		;2a08	66		f
	defb 0e3h		;2a09	e3		.
	defb 07ch		;2a0a	7c		|
	defb 09ah		;2a0b	9a		.
	defb 052h		;2a0c	52		R
	defb 041h		;2a0d	41		A
	defb 04dh		;2a0e	4d		M
	defb 02dh		;2a0f	2d		-
	defb 064h		;2a10	64		d
	defb 0c0h		;2a11	c0		.
	defb 08ch		;2a12	8c		.
	defb 06bh		;2a13	6b		k
	defb 01dh		;2a14	1d		.
	defb 08bh		;2a15	8b		.
	defb 0cbh		;2a16	cb		.
	defb 040h		;2a17	40		@
	defb 037h		;2a18	37		7
	defb 081h		;2a19	81		.
	defb 0cbh		;2a1a	cb		.
	defb 00ah		;2a1b	0a		.
	defb 09ch		;2a1c	9c		.
	defb 082h		;2a1d	82		.
	defb 055h		;2a1e	55		U
	defb 070h		;2a1f	70		p
	defb 0edh		;2a20	ed		.
	defb 074h		;2a21	74		t
	defb 020h		;2a22	20		 
	defb 042h		;2a23	42		B
	defb 005h		;2a24	05		.
	defb 0b6h		;2a25	b6		.
	defb 04dh		;2a26	4d		M
	defb 053h		;2a27	53		S
	defb 0e9h		;2a28	e9		.
	defb 013h		;2a29	13		.
	defb 096h		;2a2a	96		.
	defb 037h		;2a2b	37		7
	defb 081h		;2a2c	81		.
	defb 0cbh		;2a2d	cb		.
	defb 00bh		;2a2e	0b		.
	defb 073h		;2a2f	73		s
	defb 0cah		;2a30	ca		.
	defb 053h		;2a31	53		S
	defb 074h		;2a32	74		t
	defb 061h		;2a33	61		a
	defb 073h		;2a34	73		s
	defb 099h		;2a35	99		.
	defb 060h		;2a36	60		`
	defb 065h		;2a37	65		e
	defb 06ch		;2a38	6c		l
	defb 05ch		;2a39	5c		\
	defb 037h		;2a3a	37		7
	defb 081h		;2a3b	81		.
	defb 0cbh		;2a3c	cb		.
	defb 018h		;2a3d	18		.
	defb 033h		;2a3e	33		3
	defb 07eh		;2a3f	7e		~
	defb 010h		;2a40	10		.
	defb 0cbh		;2a41	cb		.
	defb 04eh		;2a42	4e		N
	defb 075h		;2a43	75		u
	defb 052h		;2a44	52		R
	defb 039h		;2a45	39		9
	defb 06dh		;2a46	6d		m
	defb 0f4h		;2a47	f4		.
	defb 06ch		;2a48	6c		l
	defb 05fh		;2a49	5f		_
	defb 0bfh		;2a4a	bf		.
	defb 0c2h		;2a4b	c2		.
	defb 00ch		;2a4c	0c		.
	defb 054h		;2a4d	54		T
	defb 079h		;2a4e	79		y
	defb 0b3h		;2a4f	b3		.
	defb 031h		;2a50	31		1
	defb 070h		;2a51	70		p
	defb 021h		;2a52	21		!
	defb 08bh		;2a53	8b		.
	defb 069h		;2a54	69		i
	defb 063h		;2a55	63		c
	defb 0e6h		;2a56	e6		.
	defb 035h		;2a57	35		5
	defb 053h		;2a58	53		S
	defb 085h		;2a59	85		.
	defb 028h		;2a5a	28		(
	defb 043h		;2a5b	43		C
	defb 068h		;2a5c	68		h
	defb 053h		;2a5d	53		S
	defb 0f9h		;2a5e	f9		.
	defb 0b2h		;2a5f	b2		.
	defb 073h		;2a60	73		s
	defb 02fh		;2a61	2f		/
	defb 053h		;2a62	53		S
	defb 065h		;2a63	65		e
	defb 063h		;2a64	63		c
	defb 0e3h		;2a65	e3		.
	defb 0c2h		;2a66	c2		.
	defb 03eh		;2a67	3e		>
	defb 08fh		;2a68	8f		.
	defb 00fh		;2a69	0f		.
	defb 007h		;2a6a	07		.
	defb 036h		;2a6b	36		6
	defb 0eeh		;2a6c	ee		.
	defb 0a7h		;2a6d	a7		.
	defb 038h		;2a6e	38		8
	defb 031h		;2a6f	31		1
	defb 030h		;2a70	30		0
	defb 0cfh		;2a71	cf		.
	defb 07eh		;2a72	7e		~
	defb 032h		;2a73	32		2
	defb 0a1h		;2a74	a1		.
	defb 0d8h		;2a75	d8		.
	defb 035h		;2a76	35		5
	defb 0ech		;2a77	ec		.
	defb 09eh		;2a78	9e		.
	defb 0fah		;2a79	fa		.
	defb 0ech		;2a7a	ec		.
	defb 034h		;2a7b	34		4
	defb 033h		;2a7c	33		3
	defb 0f1h		;2a7d	f1		.
	defb 04bh		;2a7e	4b		K
	defb 0ebh		;2a7f	eb		.
	defb 00dh		;2a80	0d		.
	defb 0f6h		;2a81	f6		.
	defb 078h		;2a82	78		x
	defb 0c5h		;2a83	c5		.
	defb 083h		;2a84	83		.
	defb 028h		;2a85	28		(
	defb 04dh		;2a86	4d		M
	defb 073h		;2a87	73		s
	defb 05ch		;2a88	5c		\
	defb 0afh		;2a89	af		.
	defb 0c9h		;2a8a	c9		.
	defb 087h		;2a8b	87		.
	defb 00fh		;2a8c	0f		.
	defb 060h		;2a8d	60		`
	defb 07ch		;2a8e	7c		|
	defb 07eh		;2a8f	7e		~
	defb 032h		;2a90	32		2
	defb 035h		;2a91	35		5
	defb 030h		;2a92	30		0
	defb 08ah		;2a93	8a		.
	defb 01fh		;2a94	1f		.
	defb 0b5h		;2a95	b5		.
	defb 0ddh		;2a96	dd		.
	defb 037h		;2a97	37		7
	defb 07fh		;2a98	7f		.
	defb 08ch		;2a99	8c		.
	defb 0bch		;2a9a	bc		.
	defb 0e3h		;2a9b	e3		.
	defb 09bh		;2a9c	9b		.
	defb 0c9h		;2a9d	c9		.
	defb 00eh		;2a9e	0e		.
	defb 052h		;2a9f	52		R
	defb 065h		;2aa0	65		e
	defb 062h		;2aa1	62		b
	defb 06fh		;2aa2	6f		o
	defb 053h		;2aa3	53		S
	defb 09ah		;2aa4	9a		.
	defb 04fh		;2aa5	4f		O
	defb 06dh		;2aa6	6d		m
	defb 0b1h		;2aa7	b1		.
	defb 0ffh		;2aa8	ff		.
	defb 021h		;2aa9	21		!
	defb 069h		;2aaa	69		i
	defb 076h		;2aab	76		v
	defb 0ech		;2aac	ec		.
	defb 0b8h		;2aad	b8		.
	defb 050h		;2aae	50		P
	defb 01dh		;2aaf	1d		.
	defb 002h		;2ab0	02		.
	defb 0bch		;2ab1	bc		.
	defb 009h		;2ab2	09		.
	defb 01bh		;2ab3	1b		.
	defb 00fh		;2ab4	0f		.
	defb 053h		;2ab5	53		S
	defb 079h		;2ab6	79		y
	defb 06bh		;2ab7	6b		k
	defb 0a6h		;2ab8	a6		.
	defb 083h		;2ab9	83		.
	defb 093h		;2aba	93		.
	defb 000h		;2abb	00		.
	defb 04dh		;2abc	4d		M
	defb 060h		;2abd	60		`
	defb 06bh		;2abe	6b		k
	defb 031h		;2abf	31		1
	defb 0fah		;2ac0	fa		.
	defb 01bh		;2ac1	1b		.
	defb 010h		;2ac2	10		.
	defb 007h		;2ac3	07		.
	defb 004h		;2ac4	04		.
	defb 031h		;2ac5	31		1
	defb 02dh		;2ac6	2d		-
	defb 062h		;2ac7	62		b
	defb 046h		;2ac8	46		F
	defb 044h		;2ac9	44		D
	defb 099h		;2aca	99		.
	defb 0cdh		;2acb	cd		.
	defb 062h		;2acc	62		b
	defb 02dh		;2acd	2d		-
	defb 06eh		;2ace	6e		n
	defb 0ceh		;2acf	ce		.
	defb 0ddh		;2ad0	dd		.
	defb 0b5h		;2ad1	b5		.
	defb 0cbh		;2ad2	cb		.
	defb 03ah		;2ad3	3a		:
	defb 049h		;2ad4	49		I
	defb 073h		;2ad5	73		s
	defb 0bah		;2ad6	ba		.
	defb 045h		;2ad7	45		E
	defb 0b4h		;2ad8	b4		.
	defb 06bh		;2ad9	6b		k
	defb 09bh		;2ada	9b		.
	defb 007h		;2adb	07		.
	defb 071h		;2adc	71		q
	defb 049h		;2add	49		I
	defb 053h		;2ade	53		S
	defb 04bh		;2adf	4b		K
	defb 0b0h		;2ae0	b0		.
	defb 010h		;2ae1	10		.
	defb 005h		;2ae2	05		.
	defb 0f6h		;2ae3	f6		.
	defb 041h		;2ae4	41		A
	defb 06ch		;2ae5	6c		l
	defb 074h		;2ae6	74		t
	defb 02eh		;2ae7	2e		.
	defb 020h		;2ae8	20		 
	defb 0bbh		;2ae9	bb		.
	defb 096h		;2aea	96		.
	defb 0abh		;2aeb	ab		.
	defb 0b0h		;2aec	b0		.
	defb 070h		;2aed	70		p
	defb 040h		;2aee	40		@
	defb 0cdh		;2aef	cd		.
	defb 005h		;2af0	05		.
	defb 0b0h		;2af1	b0		.
	defb 011h		;2af2	11		.
	defb 095h		;2af3	95		.
	defb 026h		;2af4	26		&
	defb 0dfh		;2af5	df		.
	defb 0dbh		;2af6	db		.
	defb 069h		;2af7	69		i
	defb 0c6h		;2af8	c6		.
	defb 01fh		;2af9	1f		.
	defb 081h		;2afa	81		.
	defb 057h		;2afb	57		W
	defb 0aeh		;2afc	ae		.
	defb 0deh		;2afd	de		.
	defb 0b0h		;2afe	b0		.
	defb 026h		;2aff	26		&
	defb 0adh		;2b00	ad		.
	defb 0e0h		;2b01	e0		.
	defb 041h		;2b02	41		A
	defb 075h		;2b03	75		u
	defb 0fbh		;2b04	fb		.
	defb 06fh		;2b05	6f		o
	defb 045h		;2b06	45		E
	defb 0b5h		;2b07	b5		.
	defb 0d7h		;2b08	d7		.
	defb 012h		;2b09	12		.
	defb 0d7h		;2b0a	d7		.
	defb 0a2h		;2b0b	a2		.
	defb 06fh		;2b0c	6f		o
	defb 00ah		;2b0d	0a		.
	defb 02ch		;2b0e	2c		,
	defb 0bdh		;2b0f	bd		.
	defb 0cdh		;2b10	cd		.
	defb 037h		;2b11	37		7
	defb 0d7h		;2b12	d7		.
	defb 00ch		;2b13	0c		.
	defb 0d7h		;2b14	d7		.
	defb 013h		;2b15	13		.
	defb 0e9h		;2b16	e9		.
	defb 020h		;2b17	20		 
	defb 09fh		;2b18	9f		.
	defb 04dh		;2b19	4d		M
	defb 061h		;2b1a	61		a
	defb 02ah		;2b1b	2a		*
	defb 0b0h		;2b1c	b0		.
	defb 05fh		;2b1d	5f		_
	defb 072h		;2b1e	72		r
	defb 0c9h		;2b1f	c9		.
	defb 095h		;2b20	95		.
	defb 0d7h		;2b21	d7		.
	defb 0b5h		;2b22	b5		.
	defb 0d7h		;2b23	d7		.
	defb 060h		;2b24	60		`
	defb 0ech		;2b25	ec		.
	defb 0b3h		;2b26	b3		.
	defb 0e7h		;2b27	e7		.
	defb 073h		;2b28	73		s
	defb 075h		;2b29	75		u
	defb 070h		;2b2a	70		p
	defb 043h		;2b2b	43		C
	defb 043h		;2b2c	43		C
	defb 0f0h		;2b2d	f0		.
	defb 044h		;2b2e	44		D
	defb 02dh		;2b2f	2d		-
	defb 052h		;2b30	52		R
	defb 04fh		;2b31	4f		O
	defb 04dh		;2b32	4d		M
	defb 0f3h		;2b33	f3		.
	defb 075h		;2b34	75		u
	defb 0cbh		;2b35	cb		.
	defb 05ah		;2b36	5a		Z
	defb 0c1h		;2b37	c1		.
	defb 014h		;2b38	14		.
	defb 0c1h		;2b39	c1		.
	defb 053h		;2b3a	53		S
	defb 06ah		;2b3b	6a		j
	defb 0c3h		;2b3c	c3		.
	defb 07fh		;2b3d	7f		.
	defb 076h		;2b3e	76		v
	defb 09fh		;2b3f	9f		.
	defb 08eh		;2b40	8e		.
	defb 0f1h		;2b41	f1		.
	defb 0c1h		;2b42	c1		.
	defb 0c0h		;2b43	c0		.
	defb 0ebh		;2b44	eb		.
	defb 0c0h		;2b45	c0		.
	defb 0c1h		;2b46	c1		.
	defb 0afh		;2b47	af		.
	defb 0a7h		;2b48	a7		.
	defb 015h		;2b49	15		.
	defb 048h		;2b4a	48		H
	defb 059h		;2b4b	59		Y
	defb 057h		;2b4c	57		W
	defb 072h		;2b4d	72		r
	defb 069h		;2b4e	69		i
	defb 08ah		;2b4f	8a		.
	defb 0b0h		;2b50	b0		.
	defb 0eeh		;2b51	ee		.
	defb 070h		;2b52	70		p
	defb 053h		;2b53	53		S
	defb 0f2h		;2b54	f2		.
	defb 067h		;2b55	67		g
	defb 050h		;2b56	50		P
	defb 028h		;2b57	28		(
	defb 03bh		;2b58	3b		;
	defb 0d2h		;2b59	d2		.
	defb 05bh		;2b5a	5b		[
	defb 001h		;2b5b	01		.
	defb 067h		;2b5c	67		g
	defb 002h		;2b5d	02		.
	defb 05bh		;2b5e	5b		[
	defb 016h		;2b5f	16		.
	defb 059h		;2b60	59		Y
	defb 053h		;2b61	53		S
	defb 0f6h		;2b62	f6		.
	defb 02dh		;2b63	2d		-
	defb 053h		;2b64	53		S
	defb 063h		;2b65	63		c
	defb 072h		;2b66	72		r
	defb 065h		;2b67	65		e
	defb 06eh		;2b68	6e		n
	defb 070h		;2b69	70		p
	defb 063h		;2b6a	63		c
	defb 098h		;2b6b	98		.
	defb 06fh		;2b6c	6f		o
	defb 073h		;2b6d	73		s
	defb 0c5h		;2b6e	c5		.
	defb 0d7h		;2b6f	d7		.
	defb 03fh		;2b70	3f		?
	defb 01ch		;2b71	1c		.
	defb 0cbh		;2b72	cb		.
	defb 01fh		;2b73	1f		.
	defb 09fh		;2b74	9f		.
	defb 0e6h		;2b75	e6		.
	defb 0f0h		;2b76	f0		.
	defb 0e0h		;2b77	e0		.
	defb 02dh		;2b78	2d		-
	defb 037h		;2b79	37		7
	defb 09eh		;2b7a	9e		.
	defb 036h		;2b7b	36		6
	defb 014h		;2b7c	14		.
	defb 0a4h		;2b7d	a4		.
	defb 0bah		;2b7e	ba		.
	defb 0e8h		;2b7f	e8		.
	defb 08ch		;2b80	8c		.
	defb 0bdh		;2b81	bd		.
	defb 033h		;2b82	33		3
	defb 067h		;2b83	67		g
	defb 0a4h		;2b84	a4		.
	defb 0aeh		;2b85	ae		.
	defb 09fh		;2b86	9f		.
	defb 064h		;2b87	64		d
	defb 031h		;2b88	31		1
	defb 020h		;2b89	20		 
	defb 0ebh		;2b8a	eb		.
	defb 02bh		;2b8b	2b		+
	defb 09eh		;2b8c	9e		.
	defb 042h		;2b8d	42		B
	defb 09dh		;2b8e	9d		.
	defb 082h		;2b8f	82		.
	defb 09dh		;2b90	9d		.
	defb 0c2h		;2b91	c2		.
	defb 09ch		;2b92	9c		.
	defb 002h		;2b93	02		.
	defb 098h		;2b94	98		.
	defb 042h		;2b95	42		B
	defb 0dch		;2b96	dc		.
	defb 076h		;2b97	76		v
	defb 053h		;2b98	53		S
	defb 0d6h		;2b99	d6		.
	defb 003h		;2b9a	03		.
	defb 017h		;2b9b	17		.
	defb 058h		;2b9c	58		X
	defb 09dh		;2b9d	9d		.
	defb 007h		;2b9e	07		.
	defb 0b0h		;2b9f	b0		.
	defb 00fh		;2ba0	0f		.
	defb 00eh		;2ba1	0e		.
	defb 052h		;2ba2	52		R
	defb 02dh		;2ba3	2d		-
	defb 0dah		;2ba4	da		.
	defb 0d4h		;2ba5	d4		.
	defb 0ceh		;2ba6	ce		.
	defb 0a5h		;2ba7	a5		.
	defb 094h		;2ba8	94		.
	defb 0c8h		;2ba9	c8		.
	defb 0c2h		;2baa	c2		.
	defb 0bch		;2bab	bc		.
	defb 05ah		;2bac	5a		Z
	defb 0cah		;2bad	ca		.
	defb 0b0h		;2bae	b0		.
	defb 0aah		;2baf	aa		.
	defb 029h		;2bb0	29		)
	defb 0a5h		;2bb1	a5		.
	defb 0a4h		;2bb2	a4		.
	defb 09eh		;2bb3	9e		.
	defb 098h		;2bb4	98		.
	defb 079h		;2bb5	79		y
	defb 04ah		;2bb6	4a		J
	defb 092h		;2bb7	92		.
	defb 08ch		;2bb8	8c		.
	defb 0d6h		;2bb9	d6		.
	defb 007h		;2bba	07		.
	defb 051h		;2bbb	51		Q
	defb 075h		;2bbc	75		u
	defb 092h		;2bbd	92		.
	defb 00ah		;2bbe	0a		.
	defb 054h		;2bbf	54		T
	defb 016h		;2bc0	16		.
	defb 018h		;2bc1	18		.
	defb 0e3h		;2bc2	e3		.
	defb 0bbh		;2bc3	bb		.
	defb 0e6h		;2bc4	e6		.
	defb 0cch		;2bc5	cc		.
	defb 08ch		;2bc6	8c		.
	defb 0dfh		;2bc7	df		.
	defb 0deh		;2bc8	de		.
	defb 03eh		;2bc9	3e		>
	defb 00ah		;2bca	0a		.
	defb 033h		;2bcb	33		3
	defb 02bh		;2bcc	2b		+
	defb 008h		;2bcd	08		.
	defb 054h		;2bce	54		T
	defb 052h		;2bcf	52		R
	defb 0f3h		;2bd0	f3		.
	defb 045h		;2bd1	45		E
	defb 0e7h		;2bd2	e7		.
	defb 079h		;2bd3	79		y
	defb 041h		;2bd4	41		A
	defb 0c7h		;2bd5	c7		.
	defb 0bfh		;2bd6	bf		.
	defb 03ah		;2bd7	3a		:
	defb 03eh		;2bd8	3e		>
	defb 08bh		;2bd9	8b		.
	defb 01eh		;2bda	1e		.
	defb 09dh		;2bdb	9d		.
	defb 026h		;2bdc	26		&
	defb 003h		;2bdd	03		.
	defb 061h		;2bde	61		a
	defb 065h		;2bdf	65		e
	defb 066h		;2be0	66		f
	defb 0f4h		;2be1	f4		.
	defb 075h		;2be2	75		u
	defb 074h		;2be3	74		t
	defb 000h		;2be4	00		.
	defb 0beh		;2be5	be		.
	defb 0a2h		;2be6	a2		.
	defb 036h		;2be7	36		6
	defb 046h		;2be8	46		F
	defb 003h		;2be9	03		.
	defb 0d5h		;2bea	d5		.
	defb 04bh		;2beb	4b		K
	defb 0d8h		;2bec	d8		.
	defb 0efh		;2bed	ef		.
	defb 009h		;2bee	09		.
	defb 0ffh		;2bef	ff		.
	defb 0c6h		;2bf0	c6		.
	defb 0cdh		;2bf1	cd		.
	defb 042h		;2bf2	42		B
	defb 081h		;2bf3	81		.
	defb 0ddh		;2bf4	dd		.
	defb 0cdh		;2bf5	cd		.
	defb 00ch		;2bf6	0c		.
	defb 008h		;2bf7	08		.
	defb 0f5h		;2bf8	f5		.
	defb 096h		;2bf9	96		.
	defb 0cdh		;2bfa	cd		.
	defb 00ah		;2bfb	0a		.
	defb 0f6h		;2bfc	f6		.
	defb 0bfh		;2bfd	bf		.
	defb 0cdh		;2bfe	cd		.
	defb 043h		;2bff	43		C
	defb 0cdh		;2c00	cd		.
	defb 030h		;2c01	30		0
	defb 030h		;2c02	30		0
	defb 0beh		;2c03	be		.
	defb 0adh		;2c04	ad		.
	defb 0deh		;2c05	de		.
	defb 02eh		;2c06	2e		.
	defb 0cdh		;2c07	cd		.
	defb 00bh		;2c08	0b		.
	defb 03fh		;2c09	3f		?
	defb 0a2h		;2c0a	a2		.
	defb 0cdh		;2c0b	cd		.
	defb 060h		;2c0c	60		`
	defb 0f7h		;2c0d	f7		.
	defb 0cdh		;2c0e	cd		.
	defb 0c0h		;2c0f	c0		.
	defb 080h		;2c10	80		.
	defb 05eh		;2c11	5e		^
	defb 061h		;2c12	61		a
	defb 0cdh		;2c13	cd		.
	defb 073h		;2c14	73		s
	defb 039h		;2c15	39		9
	defb 044h		;2c16	44		D
	defb 06fh		;2c17	6f		o
	defb 070h		;2c18	70		p
	defb 079h		;2c19	79		y
	defb 033h		;2c1a	33		3
	defb 067h		;2c1b	67		g
	defb 068h		;2c1c	68		h
	defb 0c9h		;2c1d	c9		.
	defb 057h		;2c1e	57		W
	defb 00dh		;2c1f	0d		.
	defb 028h		;2c20	28		(
	defb 05fh		;2c21	5f		_
	defb 032h		;2c22	32		2
	defb 00fh		;2c23	0f		.
	defb 005h		;2c24	05		.
	defb 0c4h		;2c25	c4		.
	defb 050h		;2c26	50		P
	defb 030h		;2c27	30		0
	defb 08dh		;2c28	8d		.
	defb 045h		;2c29	45		E
	defb 054h		;2c2a	54		T
	defb 052h		;2c2b	52		R
	defb 0bah		;2c2c	ba		.
	defb 0f6h		;2c2d	f6		.
	defb 039h		;2c2e	39		9
	defb 04ch		;2c2f	4c		L
	defb 055h		;2c30	55		U
	defb 098h		;2c31	98		.
	defb 024h		;2c32	24		$
	defb 054h		;2c33	54		T
	defb 022h		;2c34	22		"
	defb 041h		;2c35	41		A
	defb 06ch		;2c36	6c		l
	defb 06bh		;2c37	6b		k
	defb 0e5h		;2c38	e5		.
	defb 0e6h		;2c39	e6		.
	defb 0ddh		;2c3a	dd		.
	defb 062h		;2c3b	62		b
	defb 03eh		;2c3c	3e		>
	defb 073h		;2c3d	73		s
	defb 065h		;2c3e	65		e
	defb 085h		;2c3f	85		.
	defb 010h		;2c40	10		.
	defb 08bh		;2c41	8b		.
	defb 0c7h		;2c42	c7		.
	defb 04ah		;2c43	4a		J
	defb 0fah		;2c44	fa		.
	defb 0f2h		;2c45	f2		.
	defb 053h		;2c46	53		S
	defb 050h		;2c47	50		P
	defb 052h		;2c48	52		R
	defb 049h		;2c49	49		I
	defb 04eh		;2c4a	4e		N
	defb 0d9h		;2c4b	d9		.
	defb 0d1h		;2c4c	d1		.
	defb 08ah		;2c4d	8a		.
	defb 064h		;2c4e	64		d
	defb 0d3h		;2c4f	d3		.
	defb 0f5h		;2c50	f5		.
	defb 050h		;2c51	50		P
	defb 030h		;2c52	30		0
	defb 01eh		;2c53	1e		.
	defb 049h		;2c54	49		I
	defb 04ch		;2c55	4c		L
	defb 011h		;2c56	11		.
	defb 093h		;2c57	93		.
	defb 059h		;2c58	59		Y
	defb 0f6h		;2c59	f6		.
	defb 056h		;2c5a	56		V
	defb 018h		;2c5b	18		.
	defb 0f6h		;2c5c	f6		.
	defb 0a7h		;2c5d	a7		.
	defb 073h		;2c5e	73		s
	defb 031h		;2c5f	31		1
	defb 02eh		;2c60	2e		.
	defb 035h		;2c61	35		5
	defb 038h		;2c62	38		8
	defb 000h		;2c63	00		.
	defb 028h		;2c64	28		(
	defb 043h		;2c65	43		C
	defb 099h		;2c66	99		.
	defb 005h		;2c67	05		.
	defb 0afh		;2c68	af		.
	defb 02eh		;2c69	2e		.
	defb 001h		;2c6a	01		.
	defb 099h		;2c6b	99		.
	defb 0c2h		;2c6c	c2		.
	defb 041h		;2c6d	41		A
	defb 011h		;2c6e	11		.
	defb 052h		;2c6f	52		R
	defb 0fdh		;2c70	fd		.
	defb 046h		;2c71	46		F
	defb 00ch		;2c72	0c		.
	defb 080h		;2c73	80		.
	defb 0c4h		;2c74	c4		.
	defb 072h		;2c75	72		r
	defb 0ech		;2c76	ec		.
	defb 0bch		;2c77	bc		.
	defb 069h		;2c78	69		i
	defb 050h		;2c79	50		P
	defb 09fh		;2c7a	9f		.
	defb 043h		;2c7b	43		C
	defb 002h		;2c7c	02		.
	defb 043h		;2c7d	43		C
	defb 052h		;2c7e	52		R
	defb 077h		;2c7f	77		w
	defb 01fh		;2c80	1f		.
	defb 07fh		;2c81	7f		.
	defb 0d7h		;2c82	d7		.
	defb 0fch		;2c83	fc		.
	defb 0edh		;2c84	ed		.
	defb 046h		;2c85	46		F
	defb 031h		;2c86	31		1
	defb 0b7h		;2c87	b7		.
	defb 0a6h		;2c88	a6		.
	defb 0fch		;2c89	fc		.
	defb 0d7h		;2c8a	d7		.
	defb 053h		;2c8b	53		S
	defb 06dh		;2c8c	6d		m
	defb 0ech		;2c8d	ec		.
	defb 03fh		;2c8e	3f		?
	defb 026h		;2c8f	26		&
	defb 045h		;2c90	45		E
	defb 078h		;2c91	78		x
	defb 0fah		;2c92	fa		.
	defb 02eh		;2c93	2e		.
	defb 0d0h		;2c94	d0		.
	defb 056h		;2c95	56		V
	defb 03eh		;2c96	3e		>
	defb 063h		;2c97	63		c
	defb 023h		;2c98	23		#
	defb 096h		;2c99	96		.
	defb 00fh		;2c9a	0f		.
	defb 091h		;2c9b	91		.
	defb 056h		;2c9c	56		V
	defb 06ch		;2c9d	6c		l
	defb 002h		;2c9e	02		.
	defb 033h		;2c9f	33		3
	defb 075h		;2ca0	75		u
	defb 055h		;2ca1	55		U
	defb 09eh		;2ca2	9e		.
	defb 08dh		;2ca3	8d		.
	defb 0c3h		;2ca4	c3		.
	defb 018h		;2ca5	18		.
	defb 0c9h		;2ca6	c9		.
	defb 0b3h		;2ca7	b3		.
	defb 019h		;2ca8	19		.
	defb 01ah		;2ca9	1a		.
	defb 01bh		;2caa	1b		.
	defb 07dh		;2cab	7d		}
	defb 095h		;2cac	95		.
	defb 0d7h		;2cad	d7		.
	defb 065h		;2cae	65		e
	defb 031h		;2caf	31		1
	defb 086h		;2cb0	86		.
	defb 0afh		;2cb1	af		.
	defb 043h		;2cb2	43		C
	defb 049h		;2cb3	49		I
	defb 018h		;2cb4	18		.
	defb 0cah		;2cb5	ca		.
	defb 0b5h		;2cb6	b5		.
	defb 0c5h		;2cb7	c5		.
	defb 035h		;2cb8	35		5
	defb 0c5h		;2cb9	c5		.
	defb 04fh		;2cba	4f		O
	defb 06ch		;2cbb	6c		l
	defb 021h		;2cbc	21		!
	defb 0cbh		;2cbd	cb		.
	defb 0c0h		;2cbe	c0		.
	defb 0c6h		;2cbf	c6		.
	defb 0d8h		;2cc0	d8		.
	defb 0d9h		;2cc1	d9		.
	defb 050h		;2cc2	50		P
	defb 055h		;2cc3	55		U
	defb 02fh		;2cc4	2f		/
	defb 044h		;2cc5	44		D
	defb 02bh		;2cc6	2b		+
	defb 0feh		;2cc7	fe		.
	defb 0d4h		;2cc8	d4		.
	defb 02dh		;2cc9	2d		-
	defb 0d7h		;2cca	d7		.
	defb 04dh		;2ccb	4d		M
	defb 06fh		;2ccc	6f		o
	defb 064h		;2ccd	64		d
	defb 069h		;2cce	69		i
	defb 066h		;2ccf	66		f
	defb 079h		;2cd0	79		y
	defb 057h		;2cd1	57		W
	defb 06bh		;2cd2	6b		k
	defb 0cah		;2cd3	ca		.
	defb 037h		;2cd4	37		7
	defb 0cah		;2cd5	ca		.
	defb 003h		;2cd6	03		.
	defb 0a3h		;2cd7	a3		.
	defb 0a4h		;2cd8	a4		.
	defb 069h		;2cd9	69		i
	defb 06ah		;2cda	6a		j
	defb 08ch		;2cdb	8c		.
	defb 04dh		;2cdc	4d		M
	defb 033h		;2cdd	33		3
	defb 069h		;2cde	69		i
	defb 02ch		;2cdf	2c		,
	defb 0d7h		;2ce0	d7		.
	defb 095h		;2ce1	95		.
	defb 06ch		;2ce2	6c		l
	defb 072h		;2ce3	72		r
	defb 037h		;2ce4	37		7
	defb 064h		;2ce5	64		d
	defb 08ch		;2ce6	8c		.
	defb 050h		;2ce7	50		P
	defb 048h		;2ce8	48		H
	defb 0f5h		;2ce9	f5		.
	defb 073h		;2cea	73		s
	defb 03ch		;2ceb	3c		<
	defb 044h		;2cec	44		D
	defb 07ch		;2ced	7c		|
	defb 095h		;2cee	95		.
	defb 045h		;2cef	45		E
	defb 04ch		;2cf0	4c		L
	defb 05ah		;2cf1	5a		Z
	defb 07eh		;2cf2	7e		~
	defb 023h		;2cf3	23		#
	defb 098h		;2cf4	98		.
	defb 03ch		;2cf5	3c		<
	defb 01eh		;2cf6	1e		.
	defb 03ch		;2cf7	3c		<
	defb 038h		;2cf8	38		8
	defb 0fdh		;2cf9	fd		.
	defb 094h		;2cfa	94		.
	defb 0c5h		;2cfb	c5		.
	defb 041h		;2cfc	41		A
	defb 0dbh		;2cfd	db		.
	defb 0a5h		;2cfe	a5		.
	defb 0e5h		;2cff	e5		.
	defb 066h		;2d00	66		f
	defb 0d4h		;2d01	d4		.
	defb 00fh		;2d02	0f		.
	defb 081h		;2d03	81		.
	defb 0d3h		;2d04	d3		.
	defb 06bh		;2d05	6b		k
	defb 075h		;2d06	75		u
	defb 097h		;2d07	97		.
	defb 019h		;2d08	19		.
	defb 0cfh		;2d09	cf		.
	defb 03ah		;2d0a	3a		:
	defb 000h		;2d0b	00		.
	defb 057h		;2d0c	57		W
	defb 0cdh		;2d0d	cd		.
	defb 065h		;2d0e	65		e
	defb 0bfh		;2d0f	bf		.
	defb 04eh		;2d10	4e		N
	defb 07fh		;2d11	7f		.
	defb 047h		;2d12	47		G
	defb 021h		;2d13	21		!
	defb 0abh		;2d14	ab		.
	defb 0dch		;2d15	dc		.
	defb 06fh		;2d16	6f		o
	defb 04dh		;2d17	4d		M
	defb 0dch		;2d18	dc		.
	defb 048h		;2d19	48		H
	defb 045h		;2d1a	45		E
	defb 032h		;2d1b	32		2
	defb 058h		;2d1c	58		X
	defb 04bh		;2d1d	4b		K
	defb 055h		;2d1e	55		U
	defb 083h		;2d1f	83		.
	defb 014h		;2d20	14		.
	defb 043h		;2d21	43		C
	defb 099h		;2d22	99		.
	defb 03dh		;2d23	3d		=
	defb 044h		;2d24	44		D
	defb 04dh		;2d25	4d		M
	defb 02ch		;2d26	2c		,
	defb 02ch		;2d27	2c		,
	defb 04eh		;2d28	4e		N
	defb 09eh		;2d29	9e		.
	defb 04ch		;2d2a	4c		L
	defb 06ch		;2d2b	6c		l
	defb 073h		;2d2c	73		s
	defb 0cch		;2d2d	cc		.
	defb 045h		;2d2e	45		E
	defb 046h		;2d2f	46		F
	defb 067h		;2d30	67		g
	defb 0c9h		;2d31	c9		.
	defb 055h		;2d32	55		U
	defb 096h		;2d33	96		.
	defb 056h		;2d34	56		V
	defb 025h		;2d35	25		%
	defb 098h		;2d36	98		.
	defb 0cdh		;2d37	cd		.
	defb 090h		;2d38	90		.
	defb 0a1h		;2d39	a1		.
	defb 021h		;2d3a	21		!
	defb 000h		;2d3b	00		.
	defb 054h		;2d3c	54		T
	defb 0dbh		;2d3d	db		.
	defb 0f9h		;2d3e	f9		.
	defb 04fh		;2d3f	4f		O
	defb 069h		;2d40	69		i
	defb 06eh		;2d41	6e		n
	defb 067h		;2d42	67		g
	defb 0c5h		;2d43	c5		.
	defb 0ech		;2d44	ec		.
	defb 05bh		;2d45	5b		[
	defb 02dh		;2d46	2d		-
	defb 0fdh		;2d47	fd		.
	defb 092h		;2d48	92		.
	defb 007h		;2d49	07		.
	defb 06dh		;2d4a	6d		m
	defb 013h		;2d4b	13		.
	defb 079h		;2d4c	79		y
	defb 03fh		;2d4d	3f		?
	defb 0f7h		;2d4e	f7		.
	defb 0b4h		;2d4f	b4		.
	defb 02eh		;2d50	2e		.
	defb 0bch		;2d51	bc		.
	defb 0c4h		;2d52	c4		.
	defb 05bh		;2d53	5b		[
	defb 0a6h		;2d54	a6		.
	defb 076h		;2d55	76		v
	defb 07bh		;2d56	7b		{
	defb 046h		;2d57	46		F
	defb 034h		;2d58	34		4
	defb 063h		;2d59	63		c
	defb 00ch		;2d5a	0c		.
	defb 0ech		;2d5b	ec		.
	defb 08dh		;2d5c	8d		.
	defb 069h		;2d5d	69		i
	defb 070h		;2d5e	70		p
	defb 05dh		;2d5f	5d		]
	defb 0cfh		;2d60	cf		.
	defb 02fh		;2d61	2f		/
	defb 0c9h		;2d62	c9		.
	defb 00bh		;2d63	0b		.
	defb 06ch		;2d64	6c		l
	defb 0bch		;2d65	bc		.
	defb 0ffh		;2d66	ff		.
	defb 0a7h		;2d67	a7		.
	defb 0c9h		;2d68	c9		.
	defb 055h		;2d69	55		U
	defb 06eh		;2d6a	6e		n
	defb 07fh		;2d6b	7f		.
	defb 01bh		;2d6c	1b		.
	defb 06bh		;2d6d	6b		k
	defb 06fh		;2d6e	6f		o
	defb 077h		;2d6f	77		w
	defb 034h		;2d70	34		4
	defb 07ch		;2d71	7c		|
	defb 064h		;2d72	64		d
	defb 002h		;2d73	02		.
	defb 04eh		;2d74	4e		N
	defb 0ffh		;2d75	ff		.
	defb 08fh		;2d76	8f		.
	defb 0e2h		;2d77	e2		.
	defb 012h		;2d78	12		.
	defb 0d9h		;2d79	d9		.
	defb 088h		;2d7a	88		.
	defb 0f2h		;2d7b	f2		.
	defb 0d2h		;2d7c	d2		.
	defb 0e6h		;2d7d	e6		.
	defb 0ffh		;2d7e	ff		.
	defb 07fh		;2d7f	7f		.
	defb 0e9h		;2d80	e9		.
	defb 046h		;2d81	46		F
	defb 061h		;2d82	61		a
	defb 0ffh		;2d83	ff		.
	defb 06ch		;2d84	6c		l
	defb 050h		;2d85	50		P
	defb 0d3h		;2d86	d3		.
	defb 08ch		;2d87	8c		.
	defb 078h		;2d88	78		x
	defb 050h		;2d89	50		P
	defb 04fh		;2d8a	4f		O
	defb 06eh		;2d8b	6e		n
	defb 061h		;2d8c	61		a
	defb 06dh		;2d8d	6d		m
	defb 0ebh		;2d8e	eb		.
	defb 04bh		;2d8f	4b		K
	defb 02dh		;2d90	2d		-
	defb 00fh		;2d91	0f		.
	defb 0dfh		;2d92	df		.
	defb 088h		;2d93	88		.
	defb 0b8h		;2d94	b8		.
	defb 03eh		;2d95	3e		>
	defb 020h		;2d96	20		 
	defb 0f7h		;2d97	f7		.
	defb 07bh		;2d98	7b		{
	defb 0fdh		;2d99	fd		.
	defb 013h		;2d9a	13		.
	defb 046h		;2d9b	46		F
	defb 0ffh		;2d9c	ff		.
	defb 0c8h		;2d9d	c8		.
	defb 06fh		;2d9e	6f		o
	defb 075h		;2d9f	75		u
	defb 06eh		;2da0	6e		n
	defb 062h		;2da1	62		b
	defb 035h		;2da2	35		5
	defb 027h		;2da3	27		'
	defb 02dh		;2da4	2d		-
	defb 025h		;2da5	25		%
	defb 08fh		;2da6	8f		.
	defb 03dh		;2da7	3d		=
	defb 076h		;2da8	76		v
	defb 0afh		;2da9	af		.
	defb 0d6h		;2daa	d6		.
	defb 0f8h		;2dab	f8		.
	defb 040h		;2dac	40		@
	defb 0d0h		;2dad	d0		.
	defb 037h		;2dae	37		7
	defb 073h		;2daf	73		s
	defb 031h		;2db0	31		1
	defb 0c0h		;2db1	c0		.
	defb 042h		;2db2	42		B
	defb 06fh		;2db3	6f		o
	defb 0d3h		;2db4	d3		.
	defb 037h		;2db5	37		7
	defb 03eh		;2db6	3e		>
	defb 056h		;2db7	56		V
	defb 09fh		;2db8	9f		.
	defb 097h		;2db9	97		.
	defb 0bfh		;2dba	bf		.
	defb 05eh		;2dbb	5e		^
	defb 022h		;2dbc	22		"
	defb 00dh		;2dbd	0d		.
	defb 066h		;2dbe	66		f
	defb 06fh		;2dbf	6f		o
	defb 032h		;2dc0	32		2
	defb 006h		;2dc1	06		.
	defb 09ah		;2dc2	9a		.
	defb 013h		;2dc3	13		.
	defb 012h		;2dc4	12		.
	defb 0e4h		;2dc5	e4		.
	defb 05fh		;2dc6	5f		_
	defb 02dh		;2dc7	2d		-
	defb 056h		;2dc8	56		V
	defb 09bh		;2dc9	9b		.
	defb 048h		;2dca	48		H
	defb 0d2h		;2dcb	d2		.
	defb 07fh		;2dcc	7f		.
	defb 0f8h		;2dcd	f8		.
	defb 051h		;2dce	51		Q
	defb 01eh		;2dcf	1e		.
	defb 0f2h		;2dd0	f2		.
	defb 07fh		;2dd1	7f		.
	defb 07ah		;2dd2	7a		z
	defb 0e4h		;2dd3	e4		.
	defb 0d7h		;2dd4	d7		.
	defb 0f2h		;2dd5	f2		.
	defb 0e2h		;2dd6	e2		.
	defb 0a7h		;2dd7	a7		.
	defb 004h		;2dd8	04		.
	defb 0d4h		;2dd9	d4		.
	defb 098h		;2dda	98		.
	defb 082h		;2ddb	82		.
	defb 03eh		;2ddc	3e		>
	defb 0a6h		;2ddd	a6		.
	defb 04bh		;2dde	4b		K
	defb 090h		;2ddf	90		.
	defb 0a8h		;2de0	a8		.
	defb 0c4h		;2de1	c4		.
	defb 0d6h		;2de2	d6		.
	defb 000h		;2de3	00		.
	defb 0d8h		;2de4	d8		.
	defb 0cbh		;2de5	cb		.
	defb 0c1h		;2de6	c1		.
	defb 07bh		;2de7	7b		{
	defb 04fh		;2de8	4f		O
	defb 03ah		;2de9	3a		:
	defb 04dh		;2dea	4d		M
	defb 05fh		;2deb	5f		_
	defb 050h		;2dec	50		P
	defb 00bh		;2ded	0b		.
	defb 060h		;2dee	60		`
	defb 0bfh		;2def	bf		.
	defb 00dh		;2df0	0d		.
	defb 03ch		;2df1	3c		<
	defb 01ch		;2df2	1c		.
	defb 00ah		;2df3	0a		.
	defb 0bfh		;2df4	bf		.
	defb 017h		;2df5	17		.
	defb 04fh		;2df6	4f		O
	defb 043h		;2df7	43		C
	defb 0c0h		;2df8	c0		.
	defb 0ceh		;2df9	ce		.
	defb 042h		;2dfa	42		B
	defb 002h		;2dfb	02		.
	defb 01ch		;2dfc	1c		.
	defb 0bbh		;2dfd	bb		.
	defb 09dh		;2dfe	9d		.
	defb 048h		;2dff	48		H
	defb 053h		;2e00	53		S
	defb 043h		;2e01	43		C
	defb 085h		;2e02	85		.
	defb 0a7h		;2e03	a7		.
	defb 04ah		;2e04	4a		J
	defb 031h		;2e05	31		1
	defb 041h		;2e06	41		A
	defb 0f8h		;2e07	f8		.
	defb 043h		;2e08	43		C
	defb 040h		;2e09	40		@
	defb 0a9h		;2e0a	a9		.
	defb 07bh		;2e0b	7b		{
	defb 0afh		;2e0c	af		.
	defb 000h		;2e0d	00		.
	defb 02ch		;2e0e	2c		,
	defb 0bah		;2e0f	ba		.
	defb 065h		;2e10	65		e
	defb 01bh		;2e11	1b		.
	defb 03dh		;2e12	3d		=
	defb 09ch		;2e13	9c		.
	defb 0d3h		;2e14	d3		.
	defb 00ah		;2e15	0a		.
	defb 0d6h		;2e16	d6		.
	defb 0b3h		;2e17	b3		.
	defb 0bbh		;2e18	bb		.
	defb 04bh		;2e19	4b		K
	defb 086h		;2e1a	86		.
	defb 0bdh		;2e1b	bd		.
	defb 08ch		;2e1c	8c		.
	defb 07bh		;2e1d	7b		{
	defb 0f4h		;2e1e	f4		.
	defb 044h		;2e1f	44		D
	defb 0bch		;2e20	bc		.
	defb 094h		;2e21	94		.
	defb 03dh		;2e22	3d		=
	defb 003h		;2e23	03		.
	defb 0e6h		;2e24	e6		.
	defb 0cfh		;2e25	cf		.
	defb 00eh		;2e26	0e		.
	defb 025h		;2e27	25		%
	defb 064h		;2e28	64		d
	defb 0ech		;2e29	ec		.
	defb 046h		;2e2a	46		F
	defb 09bh		;2e2b	9b		.
	defb 0ebh		;2e2c	eb		.
	defb 03bh		;2e2d	3b		;
	defb 0beh		;2e2e	be		.
	defb 06ah		;2e2f	6a		j
	defb 06fh		;2e30	6f		o
	defb 0f4h		;2e31	f4		.
	defb 0ach		;2e32	ac		.
	defb 0eeh		;2e33	ee		.
	defb 030h		;2e34	30		0
	defb 0bfh		;2e35	bf		.
	defb 003h		;2e36	03		.
	defb 07dh		;2e37	7d		}
	defb 06fh		;2e38	6f		o
	defb 007h		;2e39	07		.
	defb 042h		;2e3a	42		B
	defb 020h		;2e3b	20		 
	defb 028h		;2e3c	28		(
	defb 076h		;2e3d	76		v
	defb 0f7h		;2e3e	f7		.
	defb 032h		;2e3f	32		2
	defb 0fbh		;2e40	fb		.
	defb 004h		;2e41	04		.
	defb 09fh		;2e42	9f		.
	defb 0ffh		;2e43	ff		.
	defb 038h		;2e44	38		8
	defb 080h		;2e45	80		.
	defb 0adh		;2e46	ad		.
	defb 0a3h		;2e47	a3		.
	defb 0abh		;2e48	ab		.
	defb 0a8h		;2e49	a8		.
	defb 0a9h		;2e4a	a9		.
	defb 0e1h		;2e4b	e1		.
	defb 0aah		;2e4c	aa		.
	defb 016h		;2e4d	16		.
	defb 09ch		;2e4e	9c		.
	defb 000h		;2e4f	00		.
	defb 090h		;2e50	90		.
	defb 0e3h		;2e51	e3		.
	defb 069h		;2e52	69		i
	defb 0d8h		;2e53	d8		.
	defb 0a1h		;2e54	a1		.
	defb 0a7h		;2e55	a7		.
	defb 0ech		;2e56	ec		.
	defb 003h		;2e57	03		.
	defb 008h		;2e58	08		.
	defb 092h		;2e59	92		.
	defb 0a5h		;2e5a	a5		.
	defb 0e2h		;2e5b	e2		.
	defb 0e0h		;2e5c	e0		.
	defb 0aeh		;2e5d	ae		.
	defb 00bh		;2e5e	0b		.
	defb 0cdh		;2e5f	cd		.
	defb 0a2h		;2e60	a2		.
	defb 0a0h		;2e61	a0		.
	defb 0adh		;2e62	ad		.
	defb 0ffh		;2e63	ff		.
	defb 0a5h		;2e64	a5		.
	defb 0afh		;2e65	af		.
	defb 0ach		;2e66	ac		.
	defb 09fh		;2e67	9f		.
	defb 03ch		;2e68	3c		<
	defb 0efh		;2e69	ef		.
	defb 07fh		;2e6a	7f		.
	defb 0f7h		;2e6b	f7		.
	defb 034h		;2e6c	34		4
	defb 08eh		;2e6d	8e		.
	defb 0e2h		;2e6e	e2		.
	defb 0aah		;2e6f	aa		.
	defb 0f6h		;2e70	f6		.
	defb 0feh		;2e71	fe		.
	defb 0abh		;2e72	ab		.
	defb 0eeh		;2e73	ee		.
	defb 0e7h		;2e74	e7		.
	defb 0a5h		;2e75	a5		.
	defb 0adh		;2e76	ad		.
	defb 0aeh		;2e77	ae		.
	defb 000h		;2e78	00		.
	defb 082h		;2e79	82		.
	defb 07bh		;2e7a	7b		{
	defb 0bbh		;2e7b	bb		.
	defb 0cch		;2e7c	cc		.
	defb 0c2h		;2e7d	c2		.
	defb 032h		;2e7e	32		2
	defb 091h		;2e7f	91		.
	defb 0e5h		;2e80	e5		.
	defb 0e0h		;2e81	e0		.
	defb 0cdh		;2e82	cd		.
	defb 0a5h		;2e83	a5		.
	defb 079h		;2e84	79		y
	defb 0cbh		;2e85	cb		.
	defb 060h		;2e86	60		`
	defb 0beh		;2e87	be		.
	defb 00bh		;2e88	0b		.
	defb 0a4h		;2e89	a4		.
	defb 0cfh		;2e8a	cf		.
	defb 018h		;2e8b	18		.
	defb 0aeh		;2e8c	ae		.
	defb 0bch		;2e8d	bc		.
	defb 0bbh		;2e8e	bb		.
	defb 0f3h		;2e8f	f3		.
	defb 032h		;2e90	32		2
	defb 0afh		;2e91	af		.
	defb 002h		;2e92	02		.
	defb 0c9h		;2e93	c9		.
	defb 00ah		;2e94	0a		.
	defb 08eh		;2e95	8e		.
	defb 037h		;2e96	37		7
	defb 03eh		;2e97	3e		>
	defb 0a1h		;2e98	a1		.
	defb 0a2h		;2e99	a2		.
	defb 0abh		;2e9a	ab		.
	defb 008h		;2e9b	08		.
	defb 02ch		;2e9c	2c		,
	defb 0c9h		;2e9d	c9		.
	defb 079h		;2e9e	79		y
	defb 077h		;2e9f	77		w
	defb 02ch		;2ea0	2c		,
	defb 081h		;2ea1	81		.
	defb 0ddh		;2ea2	dd		.
	defb 030h		;2ea3	30		0
	defb 0c6h		;2ea4	c6		.
	defb 057h		;2ea5	57		W
	defb 0c9h		;2ea6	c9		.
	defb 00bh		;2ea7	0b		.
	defb 08dh		;2ea8	8d		.
	defb 0a0h		;2ea9	a0		.
	defb 0e7h		;2eaa	e7		.
	defb 0c2h		;2eab	c2		.
	defb 034h		;2eac	34		4
	defb 0edh		;2ead	ed		.
	defb 0ech		;2eae	ec		.
	defb 0aeh		;2eaf	ae		.
	defb 0cah		;2eb0	ca		.
	defb 02fh		;2eb1	2f		/
	defb 0f7h		;2eb2	f7		.
	defb 0a6h		;2eb3	a6		.
	defb 0a8h		;2eb4	a8		.
	defb 0a4h		;2eb5	a4		.
	defb 055h		;2eb6	55		U
	defb 0bbh		;2eb7	bb		.
	defb 0fbh		;2eb8	fb		.
	defb 02eh		;2eb9	2e		.
	defb 034h		;2eba	34		4
	defb 0f5h		;2ebb	f5		.
	defb 0c9h		;2ebc	c9		.
	defb 09eh		;2ebd	9e		.
	defb 0ebh		;2ebe	eb		.
	defb 0a7h		;2ebf	a7		.
	defb 006h		;2ec0	06		.
	defb 0d7h		;2ec1	d7		.
	defb 0f9h		;2ec2	f9		.
	defb 04bh		;2ec3	4b		K
	defb 063h		;2ec4	63		c
	defb 0cch		;2ec5	cc		.
	defb 0bfh		;2ec6	bf		.
	defb 00ch		;2ec7	0c		.
	defb 091h		;2ec8	91		.
	defb 062h		;2ec9	62		b
	defb 01ch		;2eca	1c		.
	defb 06ch		;2ecb	6c		l
	defb 078h		;2ecc	78		x
	defb 017h		;2ecd	17		.
	defb 0ech		;2ece	ec		.
	defb 020h		;2ecf	20		 
	defb 0a0h		;2ed0	a0		.
	defb 0a2h		;2ed1	a2		.
	defb 0b6h		;2ed2	b6		.
	defb 0b2h		;2ed3	b2		.
	defb 0afh		;2ed4	af		.
	defb 0eah		;2ed5	ea		.
	defb 067h		;2ed6	67		g
	defb 042h		;2ed7	42		B
	defb 0bfh		;2ed8	bf		.
	defb 0efh		;2ed9	ef		.
	defb 0c0h		;2eda	c0		.
	defb 0e6h		;2edb	e6		.
	defb 07fh		;2edc	7f		.
	defb 02bh		;2edd	2b		+
	defb 087h		;2ede	87		.
	defb 0a0h		;2edf	a0		.
	defb 0a4h		;2ee0	a4		.
	defb 0a5h		;2ee1	a5		.
	defb 0e0h		;2ee2	e0		.
	defb 0a6h		;2ee3	a6		.
	defb 0aah		;2ee4	aa		.
	defb 08fh		;2ee5	8f		.
	defb 07fh		;2ee6	7f		.
	defb 0d1h		;2ee7	d1		.
	defb 0c5h		;2ee8	c5		.
	defb 028h		;2ee9	28		(
	defb 08ch		;2eea	8c		.
	defb 00eh		;2eeb	0e		.
	defb 0ech		;2eec	ec		.
	defb 0e1h		;2eed	e1		.
	defb 0a5h		;2eee	a5		.
	defb 0aah		;2eef	aa		.
	defb 029h		;2ef0	29		)
	defb 028h		;2ef1	28		(
	defb 077h		;2ef2	77		w
	defb 02bh		;2ef3	2b		+
	defb 0dfh		;2ef4	df		.
	defb 093h		;2ef5	93		.
	defb 0fbh		;2ef6	fb		.
	defb 0a1h		;2ef7	a1		.
	defb 0e9h		;2ef8	e9		.
	defb 017h		;2ef9	17		.
	defb 037h		;2efa	37		7
	defb 04ch		;2efb	4c		L
	defb 07fh		;2efc	7f		.
	defb 0afh		;2efd	af		.
	defb 0bfh		;2efe	bf		.
	defb 0a7h		;2eff	a7		.
	defb 0a0h		;2f00	a0		.
	defb 0d2h		;2f01	d2		.
	defb 066h		;2f02	66		f
	defb 0a3h		;2f03	a3		.
	defb 0e3h		;2f04	e3		.
	defb 0e5h		;2f05	e5		.
	defb 0a5h		;2f06	a5		.
	defb 0bbh		;2f07	bb		.
	defb 0fbh		;2f08	fb		.
	defb 02bh		;2f09	2b		+
	defb 0aeh		;2f0a	ae		.
	defb 002h		;2f0b	02		.
	defb 016h		;2f0c	16		.
	defb 00fh		;2f0d	0f		.
	defb 091h		;2f0e	91		.
	defb 095h		;2f0f	95		.
	defb 05fh		;2f10	5f		_
	defb 0b7h		;2f11	b7		.
	defb 0e2h		;2f12	e2		.
	defb 0a5h		;2f13	a5		.
	defb 0ach		;2f14	ac		.
	defb 0adh		;2f15	ad		.
	defb 0ebh		;2f16	eb		.
	defb 062h		;2f17	62		b
	defb 008h		;2f18	08		.
	defb 0a3h		;2f19	a3		.
	defb 0adh		;2f1a	ad		.
	defb 0f9h		;2f1b	f9		.
	defb 05dh		;2f1c	5d		]
	defb 029h		;2f1d	29		)
	defb 080h		;2f1e	80		.
	defb 0abh		;2f1f	ab		.
	defb 0e2h		;2f20	e2		.
	defb 016h		;2f21	16		.
	defb 018h		;2f22	18		.
	defb 0b3h		;2f23	b3		.
	defb 039h		;2f24	39		9
	defb 058h		;2f25	58		X
	defb 0abh		;2f26	ab		.
	defb 09ah		;2f27	9a		.
	defb 0eeh		;2f28	ee		.
	defb 029h		;2f29	29		)
	defb 031h		;2f2a	31		1
	defb 0a2h		;2f2b	a2		.
	defb 016h		;2f2c	16		.
	defb 058h		;2f2d	58		X
	defb 0b4h		;2f2e	b4		.
	defb 043h		;2f2f	43		C
	defb 0efh		;2f30	ef		.
	defb 029h		;2f31	29		)
	defb 080h		;2f32	80		.
	defb 0dfh		;2f33	df		.
	defb 0edh		;2f34	ed		.
	defb 0eeh		;2f35	ee		.
	defb 029h		;2f36	29		)
	defb 060h		;2f37	60		`
	defb 006h		;2f38	06		.
	defb 0d4h		;2f39	d4		.
	defb 0beh		;2f3a	be		.
	defb 059h		;2f3b	59		Y
	defb 0d7h		;2f3c	d7		.
	defb 00ch		;2f3d	0c		.
	defb 022h		;2f3e	22		"
	defb 058h		;2f3f	58		X
	defb 0d7h		;2f40	d7		.
	defb 0e5h		;2f41	e5		.
	defb 0eeh		;2f42	ee		.
	defb 029h		;2f43	29		)
	defb 0d7h		;2f44	d7		.
	defb 0aeh		;2f45	ae		.
	defb 083h		;2f46	83		.
	defb 00eh		;2f47	0e		.
	defb 0ech		;2f48	ec		.
	defb 029h		;2f49	29		)
	defb 0e6h		;2f4a	e6		.
	defb 0b2h		;2f4b	b2		.
	defb 0c1h		;2f4c	c1		.
	defb 015h		;2f4d	15		.
	defb 022h		;2f4e	22		"
	defb 0e9h		;2f4f	e9		.
	defb 0a8h		;2f50	a8		.
	defb 0a4h		;2f51	a4		.
	defb 02ch		;2f52	2c		,
	defb 0f6h		;2f53	f6		.
	defb 0a0h		;2f54	a0		.
	defb 062h		;2f55	62		b
	defb 0afh		;2f56	af		.
	defb 0e6h		;2f57	e6		.
	defb 0cfh		;2f58	cf		.
	defb 09eh		;2f59	9e		.
	defb 007h		;2f5a	07		.
	defb 0adh		;2f5b	ad		.
	defb 03fh		;2f5c	3f		?
	defb 07ch		;2f5d	7c		|
	defb 0dch		;2f5e	dc		.
	defb 0b0h		;2f5f	b0		.
	defb 03bh		;2f60	3b		;
	defb 029h		;2f61	29		)
	defb 061h		;2f62	61		a
	defb 02ah		;2f63	2a		*
	defb 059h		;2f64	59		Y
	defb 016h		;2f65	16		.
	defb 08fh		;2f66	8f		.
	defb 0e3h		;2f67	e3		.
	defb 048h		;2f68	48		H
	defb 0abh		;2f69	ab		.
	defb 076h		;2f6a	76		v
	defb 022h		;2f6b	22		"
	defb 0edh		;2f6c	ed		.
	defb 00ch		;2f6d	0c		.
	defb 05fh		;2f6e	5f		_
	defb 0aah		;2f6f	aa		.
	defb 0f8h		;2f70	f8		.
	defb 0c9h		;2f71	c9		.
	defb 030h		;2f72	30		0
	defb 043h		;2f73	43		C
	defb 0e7h		;2f74	e7		.
	defb 0eeh		;2f75	ee		.
	defb 059h		;2f76	59		Y
	defb 0ddh		;2f77	dd		.
	defb 075h		;2f78	75		u
	defb 027h		;2f79	27		'
	defb 046h		;2f7a	46		F
	defb 081h		;2f7b	81		.
	defb 0b0h		;2f7c	b0		.
	defb 058h		;2f7d	58		X
	defb 0dfh		;2f7e	df		.
	defb 0c1h		;2f7f	c1		.
	defb 0b4h		;2f80	b4		.
	defb 07ch		;2f81	7c		|
	defb 027h		;2f82	27		'
	defb 081h		;2f83	81		.
	defb 0ebh		;2f84	eb		.
	defb 00eh		;2f85	0e		.
	defb 0e0h		;2f86	e0		.
	defb 08eh		;2f87	8e		.
	defb 026h		;2f88	26		&
	defb 05ch		;2f89	5c		\
	defb 028h		;2f8a	28		(
	defb 0e3h		;2f8b	e3		.
	defb 00eh		;2f8c	0e		.
	defb 08fh		;2f8d	8f		.
	defb 087h		;2f8e	87		.
	defb 093h		;2f8f	93		.
	defb 0bbh		;2f90	bb		.
	defb 0f7h		;2f91	f7		.
	defb 027h		;2f92	27		'
	defb 08bh		;2f93	8b		.
	defb 002h		;2f94	02		.
	defb 031h		;2f95	31		1
	defb 07dh		;2f96	7d		}
	defb 003h		;2f97	03		.
	defb 00eh		;2f98	0e		.
	defb 0cch		;2f99	cc		.
	defb 025h		;2f9a	25		%
	defb 068h		;2f9b	68		h
	defb 0e3h		;2f9c	e3		.
	defb 0ach		;2f9d	ac		.
	defb 06fh		;2f9e	6f		o
	defb 063h		;2f9f	63		c
	defb 064h		;2fa0	64		d
	defb 0f2h		;2fa1	f2		.
	defb 0a0h		;2fa2	a0		.
	defb 0a8h		;2fa3	a8		.
	defb 0eeh		;2fa4	ee		.
	defb 0bch		;2fa5	bc		.
	defb 0bbh		;2fa6	bb		.
	defb 0b9h		;2fa7	b9		.
	defb 0fdh		;2fa8	fd		.
	defb 0f4h		;2fa9	f4		.
	defb 067h		;2faa	67		g
	defb 01eh		;2fab	1e		.
	defb 018h		;2fac	18		.
	defb 0d8h		;2fad	d8		.
	defb 0c1h		;2fae	c1		.
	defb 0eeh		;2faf	ee		.
	defb 016h		;2fb0	16		.
	defb 0c0h		;2fb1	c0		.
	defb 042h		;2fb2	42		B
	defb 0beh		;2fb3	be		.
	defb 076h		;2fb4	76		v
	defb 0d7h		;2fb5	d7		.
	defb 007h		;2fb6	07		.
	defb 016h		;2fb7	16		.
	defb 00ah		;2fb8	0a		.
	defb 0beh		;2fb9	be		.
	defb 0bbh		;2fba	bb		.
	defb 006h		;2fbb	06		.
	defb 04eh		;2fbc	4e		N
	defb 030h		;2fbd	30		0
	defb 0f8h		;2fbe	f8		.
	defb 02bh		;2fbf	2b		+
	defb 0b0h		;2fc0	b0		.
	defb 0beh		;2fc1	be		.
	defb 08dh		;2fc2	8d		.
	defb 0dbh		;2fc3	db		.
	defb 0e9h		;2fc4	e9		.
	defb 08fh		;2fc5	8f		.
	defb 085h		;2fc6	85		.
	defb 092h		;2fc7	92		.
	defb 025h		;2fc8	25		%
	defb 09fh		;2fc9	9f		.
	defb 090h		;2fca	90		.
	defb 091h		;2fcb	91		.
	defb 022h		;2fcc	22		"
	defb 0cah		;2fcd	ca		.
	defb 0d3h		;2fce	d3		.
	defb 0e1h		;2fcf	e1		.
	defb 02eh		;2fd0	2e		.
	defb 0cch		;2fd1	cc		.
	defb 059h		;2fd2	59		Y
	defb 0f4h		;2fd3	f4		.
	defb 0dch		;2fd4	dc		.
	defb 060h		;2fd5	60		`
	defb 0a2h		;2fd6	a2		.
	defb 0f7h		;2fd7	f7		.
	defb 06eh		;2fd8	6e		n
	defb 060h		;2fd9	60		`
	defb 0e1h		;2fda	e1		.
	defb 000h		;2fdb	00		.
	defb 0c1h		;2fdc	c1		.
	defb 0cch		;2fdd	cc		.
	defb 0efh		;2fde	ef		.
	defb 0eeh		;2fdf	ee		.
	defb 0e2h		;2fe0	e2		.
	defb 0fdh		;2fe1	fd		.
	defb 0efh		;2fe2	ef		.
	defb 067h		;2fe3	67		g
	defb 06fh		;2fe4	6f		o
	defb 0eah		;2fe5	ea		.
	defb 0ffh		;2fe6	ff		.
	defb 0fah		;2fe7	fa		.
	defb 0b0h		;2fe8	b0		.
	defb 093h		;2fe9	93		.
	defb 091h		;2fea	91		.
	defb 092h		;2feb	92		.
	defb 080h		;2fec	80		.
	defb 08dh		;2fed	8d		.
	defb 08eh		;2fee	8e		.
	defb 0c6h		;2fef	c6		.
	defb 0e1h		;2ff0	e1		.
	defb 082h		;2ff1	82		.
	defb 08ah		;2ff2	8a		.
	defb 088h		;2ff3	88		.
	defb 020h		;2ff4	20		 
	defb 091h		;2ff5	91		.
	defb 0a1h		;2ff6	a1		.
	defb 0e7h		;2ff7	e7		.
	defb 081h		;2ff8	81		.
	defb 085h		;2ff9	85		.
	defb 08dh		;2ffa	8d		.
	defb 016h		;2ffb	16		.
	defb 0deh		;2ffc	de		.
	defb 089h		;2ffd	89		.
	defb 020h		;2ffe	20		 
	defb 0f3h		;2fff	f3		.
	defb 077h		;3000	77		w
	defb 08bh		;3001	8b		.
	defb 09eh		;3002	9e		.
	defb 097h		;3003	97		.
	defb 07bh		;3004	7b		{
	defb 0cbh		;3005	cb		.
	defb 088h		;3006	88		.
	defb 09fh		;3007	9f		.
	defb 057h		;3008	57		W
	defb 0dah		;3009	da		.
	defb 0e2h		;300a	e2		.
	defb 082h		;300b	82		.
	defb 036h		;300c	36		6
	defb 0a6h		;300d	a6		.
	defb 0efh		;300e	ef		.
	defb 06ah		;300f	6a		j
	defb 03bh		;3010	3b		;
	defb 007h		;3011	07		.
	defb 0f4h		;3012	f4		.
	defb 03ch		;3013	3c		<
	defb 0e2h		;3014	e2		.
	defb 06ah		;3015	6a		j
	defb 028h		;3016	28		(
	defb 04bh		;3017	4b		K
	defb 02fh		;3018	2f		/
	defb 0ech		;3019	ec		.
	defb 0a8h		;301a	a8		.
	defb 019h		;301b	19		.
	defb 0beh		;301c	be		.
	defb 05dh		;301d	5d		]
	defb 0cbh		;301e	cb		.
	defb 067h		;301f	67		g
	defb 0efh		;3020	ef		.
	defb 0e2h		;3021	e2		.
	defb 089h		;3022	89		.
	defb 0ech		;3023	ec		.
	defb 0a7h		;3024	a7		.
	defb 096h		;3025	96		.
	defb 010h		;3026	10		.
	defb 0a4h		;3027	a4		.
	defb 00bh		;3028	0b		.
	defb 0e0h		;3029	e0		.
	defb 0efh		;302a	ef		.
	defb 0bbh		;302b	bb		.
	defb 005h		;302c	05		.
	defb 09ah		;302d	9a		.
	defb 01bh		;302e	1b		.
	defb 0e2h		;302f	e2		.
	defb 096h		;3030	96		.
	defb 0a1h		;3031	a1		.
	defb 0aeh		;3032	ae		.
	defb 0e0h		;3033	e0		.
	defb 01dh		;3034	1d		.
	defb 0e3h		;3035	e3		.
	defb 0deh		;3036	de		.
	defb 06bh		;3037	6b		k
	defb 0f0h		;3038	f0		.
	defb 0aah		;3039	aa		.
	defb 0a0h		;303a	a0		.
	defb 0d8h		;303b	d8		.
	defb 0d3h		;303c	d3		.
	defb 0e1h		;303d	e1		.
	defb 091h		;303e	91		.
	defb 060h		;303f	60		`
	defb 086h		;3040	86		.
	defb 0b6h		;3041	b6		.
	defb 009h		;3042	09		.
	defb 07fh		;3043	7f		.
	defb 055h		;3044	55		U
	defb 0c7h		;3045	c7		.
	defb 0b9h		;3046	b9		.
	defb 0bdh		;3047	bd		.
	defb 0e1h		;3048	e1		.
	defb 088h		;3049	88		.
	defb 0a7h		;304a	a7		.
	defb 0ach		;304b	ac		.
	defb 0f5h		;304c	f5		.
	defb 09bh		;304d	9b		.
	defb 0dfh		;304e	df		.
	defb 03ch		;304f	3c		<
	defb 06bh		;3050	6b		k
	defb 0efh		;3051	ef		.
	defb 0deh		;3052	de		.
	defb 087h		;3053	87		.
	defb 07dh		;3054	7d		}
	defb 0e5h		;3055	e5		.
	defb 0ceh		;3056	ce		.
	defb 00ch		;3057	0c		.
	defb 07eh		;3058	7e		~
	defb 034h		;3059	34		4
	defb 081h		;305a	81		.
	defb 076h		;305b	76		v
	defb 00ah		;305c	0a		.
	defb 0e6h		;305d	e6		.
	defb 0ebh		;305e	eb		.
	defb 0deh		;305f	de		.
	defb 096h		;3060	96		.
	defb 0a2h		;3061	a2		.
	defb 0a5h		;3062	a5		.
	defb 07dh		;3063	7d		}
	defb 000h		;3064	00		.
	defb 08dh		;3065	8d		.
	defb 063h		;3066	63		c
	defb 026h		;3067	26		&
	defb 0a6h		;3068	a6		.
	defb 0ach		;3069	ac		.
	defb 05dh		;306a	5d		]
	defb 0edh		;306b	ed		.
	defb 03ch		;306c	3c		<
	defb 096h		;306d	96		.
	defb 0dch		;306e	dc		.
	defb 082h		;306f	82		.
	defb 09bh		;3070	9b		.
	defb 0a4h		;3071	a4		.
	defb 0abh		;3072	ab		.
	defb 0c7h		;3073	c7		.
	defb 0a2h		;3074	a2		.
	defb 0e5h		;3075	e5		.
	defb 0aeh		;3076	ae		.
	defb 084h		;3077	84		.
	defb 073h		;3078	73		s
	defb 0b3h		;3079	b3		.
	defb 0f7h		;307a	f7		.
	defb 0d9h		;307b	d9		.
	defb 003h		;307c	03		.
	defb 0f2h		;307d	f2		.
	defb 036h		;307e	36		6
	defb 0bbh		;307f	bb		.
	defb 0d7h		;3080	d7		.
	defb 0fch		;3081	fc		.
	defb 003h		;3082	03		.
	defb 099h		;3083	99		.
	defb 0f7h		;3084	f7		.
	defb 023h		;3085	23		#
	defb 0aeh		;3086	ae		.
	defb 0a3h		;3087	a3		.
	defb 09ah		;3088	9a		.
	defb 0cch		;3089	cc		.
	defb 0beh		;308a	be		.
	defb 022h		;308b	22		"
	defb 049h		;308c	49		I
	defb 0edh		;308d	ed		.
	defb 0fch		;308e	fc		.
	defb 0d2h		;308f	d2		.
	defb 0a2h		;3090	a2		.
	defb 02ch		;3091	2c		,
	defb 082h		;3092	82		.
	defb 0beh		;3093	be		.
	defb 08ch		;3094	8c		.
	defb 0a1h		;3095	a1		.
	defb 085h		;3096	85		.
	defb 065h		;3097	65		e
	defb 085h		;3098	85		.
	defb 021h		;3099	21		!
	defb 0a2h		;309a	a2		.
	defb 098h		;309b	98		.
	defb 014h		;309c	14		.
	defb 0c1h		;309d	c1		.
	defb 081h		;309e	81		.
	defb 08ah		;309f	8a		.
	defb 080h		;30a0	80		.
	defb 026h		;30a1	26		&
	defb 00eh		;30a2	0e		.
	defb 08dh		;30a3	8d		.
	defb 092h		;30a4	92		.
	defb 090h		;30a5	90		.
	defb 08bh		;30a6	8b		.
	defb 09ch		;30a7	9c		.
	defb 0f1h		;30a8	f1		.
	defb 052h		;30a9	52		R
	defb 099h		;30aa	99		.
	defb 09ch		;30ab	9c		.
	defb 091h		;30ac	91		.
	defb 093h		;30ad	93		.
	defb 08ch		;30ae	8c		.
	defb 0edh		;30af	ed		.
	defb 0eeh		;30b0	ee		.
	defb 09bh		;30b1	9b		.
	defb 0b8h		;30b2	b8		.
	defb 0e8h		;30b3	e8		.
	defb 009h		;30b4	09		.
	defb 02ch		;30b5	2c		,
	defb 079h		;30b6	79		y
	defb 0bah		;30b7	ba		.
	defb 093h		;30b8	93		.
	defb 0fah		;30b9	fa		.
	defb 085h		;30ba	85		.
	defb 087h		;30bb	87		.
	defb 0c1h		;30bc	c1		.
	defb 025h		;30bd	25		%
	defb 080h		;30be	80		.
	defb 085h		;30bf	85		.
	defb 044h		;30c0	44		D
	defb 067h		;30c1	67		g
	defb 05eh		;30c2	5e		^
	defb 08eh		;30c3	8e		.
	defb 08ch		;30c4	8c		.
	defb 09fh		;30c5	9f		.
	defb 0cch		;30c6	cc		.
	defb 0d3h		;30c7	d3		.
	defb 080h		;30c8	80		.
	defb 088h		;30c9	88		.
	defb 09eh		;30ca	9e		.
	defb 0b8h		;30cb	b8		.
	defb 000h		;30cc	00		.
	defb 000h		;30cd	00		.
	defb 00ah		;30ce	0a		.
	defb 0a5h		;30cf	a5		.
	defb 0a4h		;30d0	a4		.
	defb 03ch		;30d1	3c		<
	defb 0cfh		;30d2	cf		.
	defb 0b0h		;30d3	b0		.
	defb 0d7h		;30d4	d7		.
	defb 047h		;30d5	47		G
	defb 08fh		;30d6	8f		.
	defb 001h		;30d7	01		.
	defb 094h		;30d8	94		.
	defb 057h		;30d9	57		W
	defb 09ch		;30da	9c		.
	defb 07dh		;30db	7d		}
	defb 0bfh		;30dc	bf		.
	defb 08fh		;30dd	8f		.
	defb 015h		;30de	15		.
	defb 0afh		;30df	af		.
	defb 044h		;30e0	44		D
	defb 00ah		;30e1	0a		.
	defb 05bh		;30e2	5b		[
	defb 0a9h		;30e3	a9		.
	defb 0d9h		;30e4	d9		.
	defb 0fah		;30e5	fa		.
	defb 06ah		;30e6	6a		j
	defb 0f9h		;30e7	f9		.
	defb 0adh		;30e8	ad		.
	defb 0d5h		;30e9	d5		.
	defb 0aeh		;30ea	ae		.
	defb 08bh		;30eb	8b		.
	defb 081h		;30ec	81		.
	defb 099h		;30ed	99		.
	defb 0a0h		;30ee	a0		.
	defb 05dh		;30ef	5d		]
	defb 02dh		;30f0	2d		-
	defb 0e7h		;30f1	e7		.
	defb 0b5h		;30f2	b5		.
	defb 0c9h		;30f3	c9		.
	defb 005h		;30f4	05		.
	defb 0b6h		;30f5	b6		.
	defb 04ch		;30f6	4c		L
	defb 0ffh		;30f7	ff		.
	defb 093h		;30f8	93		.
	defb 0c9h		;30f9	c9		.
	defb 08dh		;30fa	8d		.
	defb 066h		;30fb	66		f
	defb 0cah		;30fc	ca		.
	defb 0a5h		;30fd	a5		.
	defb 0a8h		;30fe	a8		.
	defb 0a7h		;30ff	a7		.
	defb 0eah		;3100	ea		.
	defb 027h		;3101	27		'
	defb 07bh		;3102	7b		{
	defb 0fah		;3103	fa		.
	defb 04ch		;3104	4c		L
	defb 0edh		;3105	ed		.
	defb 0f4h		;3106	f4		.
	defb 062h		;3107	62		b
	defb 0f3h		;3108	f3		.
	defb 0bfh		;3109	bf		.
	defb 0e2h		;310a	e2		.
	defb 0c3h		;310b	c3		.
	defb 000h		;310c	00		.
	defb 0bch		;310d	bc		.
	defb 06ah		;310e	6a		j
	defb 08fh		;310f	8f		.
	defb 0cdh		;3110	cd		.
	defb 0a6h		;3111	a6		.
	defb 0bfh		;3112	bf		.
	defb 09bh		;3113	9b		.
	defb 043h		;3114	43		C
	defb 098h		;3115	98		.
	defb 0dah		;3116	da		.
	defb 0cdh		;3117	cd		.
	defb 0b1h		;3118	b1		.
	defb 08bh		;3119	8b		.
	defb 0f1h		;311a	f1		.
	defb 09ch		;311b	9c		.
	defb 0ech		;311c	ec		.
	defb 000h		;311d	00		.
	defb 08ch		;311e	8c		.
	defb 091h		;311f	91		.
	defb 0a7h		;3120	a7		.
	defb 0bbh		;3121	bb		.
	defb 07ch		;3122	7c		|
	defb 05eh		;3123	5e		^
	defb 006h		;3124	06		.
	defb 0c3h		;3125	c3		.
	defb 0b7h		;3126	b7		.
	defb 0f2h		;3127	f2		.
	defb 08fh		;3128	8f		.
	defb 01eh		;3129	1e		.
	defb 06fh		;312a	6f		o
	defb 0eah		;312b	ea		.
	defb 0b1h		;312c	b1		.
	defb 0f3h		;312d	f3		.
	defb 0cfh		;312e	cf		.
	defb 0bfh		;312f	bf		.
	defb 066h		;3130	66		f
	defb 0a2h		;3131	a2		.
	defb 0a9h		;3132	a9		.
	defb 0d9h		;3133	d9		.
	defb 0adh		;3134	ad		.
	defb 0a6h		;3135	a6		.
	defb 0e9h		;3136	e9		.
	defb 0b5h		;3137	b5		.
	defb 049h		;3138	49		I
	defb 08eh		;3139	8e		.
	defb 0e8h		;313a	e8		.
	defb 084h		;313b	84		.
	defb 07bh		;313c	7b		{
	defb 0e3h		;313d	e3		.
	defb 0afh		;313e	af		.
	defb 0f7h		;313f	f7		.
	defb 0edh		;3140	ed		.
	defb 0dah		;3141	da		.
	defb 0dbh		;3142	db		.
	defb 065h		;3143	65		e
	defb 0bdh		;3144	bd		.
	defb 0c3h		;3145	c3		.
	defb 0afh		;3146	af		.
	defb 0b3h		;3147	b3		.
	defb 0a5h		;3148	a5		.
	defb 0abh		;3149	ab		.
	defb 0a0h		;314a	a0		.
	defb 0e7h		;314b	e7		.
	defb 0ebh		;314c	eb		.
	defb 03dh		;314d	3d		=
	defb 0efh		;314e	ef		.
	defb 087h		;314f	87		.
	defb 0e0h		;3150	e0		.
	defb 08ah		;3151	8a		.
	defb 0a1h		;3152	a1		.
	defb 086h		;3153	86		.
	defb 0f7h		;3154	f7		.
	defb 0d6h		;3155	d6		.
	defb 0d2h		;3156	d2		.
	defb 0ebh		;3157	eb		.
	defb 04ch		;3158	4c		L
	defb 07eh		;3159	7e		~
	defb 004h		;315a	04		.
	defb 007h		;315b	07		.
	defb 0a4h		;315c	a4		.
	defb 0a6h		;315d	a6		.
	defb 040h		;315e	40		@
	defb 0ffh		;315f	ff		.
	defb 0e0h		;3160	e0		.
	defb 069h		;3161	69		i
	defb 0bfh		;3162	bf		.
	defb 062h		;3163	62		b
	defb 03fh		;3164	3f		?
	defb 07bh		;3165	7b		{
	defb 0bfh		;3166	bf		.
	defb 0b7h		;3167	b7		.
	defb 026h		;3168	26		&
	defb 0f1h		;3169	f1		.
	defb 0b3h		;316a	b3		.
	defb 0c1h		;316b	c1		.
	defb 0b2h		;316c	b2		.
	defb 02ch		;316d	2c		,
	defb 0d7h		;316e	d7		.
	defb 027h		;316f	27		'
	defb 0ech		;3170	ec		.
	defb 02ch		;3171	2c		,
	defb 0e0h		;3172	e0		.
	defb 0d2h		;3173	d2		.
	defb 012h		;3174	12		.
	defb 0c0h		;3175	c0		.
	defb 03fh		;3176	3f		?
	defb 0a2h		;3177	a2		.
	defb 0feh		;3178	fe		.
	defb 000h		;3179	00		.
	defb 024h		;317a	24		$
	defb 040h		;317b	40		@
	defb 0a9h		;317c	a9		.
	defb 0aeh		;317d	ae		.
	defb 0a6h		;317e	a6		.
	defb 06fh		;317f	6f		o
	defb 0a2h		;3180	a2		.
	defb 0deh		;3181	de		.
	defb 02fh		;3182	2f		/
	defb 0f6h		;3183	f6		.
	defb 0b7h		;3184	b7		.
	defb 05eh		;3185	5e		^
	defb 086h		;3186	86		.
	defb 08ch		;3187	8c		.
	defb 09dh		;3188	9d		.
	defb 0dfh		;3189	df		.
	defb 088h		;318a	88		.
	defb 092h		;318b	92		.
	defb 085h		;318c	85		.
	defb 063h		;318d	63		c
	defb 0b4h		;318e	b4		.
	defb 0b5h		;318f	b5		.
	defb 084h		;3190	84		.
	defb 08bh		;3191	8b		.
	defb 052h		;3192	52		R
	defb 085h		;3193	85		.
	defb 090h		;3194	90		.
	defb 06fh		;3195	6f		o
	defb 076h		;3196	76		v
	defb 087h		;3197	87		.
	defb 080h		;3198	80		.
	defb 083h		;3199	83		.
	defb 093h		;319a	93		.
	defb 08ah		;319b	8a		.
	defb 0d9h		;319c	d9		.
	defb 07dh		;319d	7d		}
	defb 088h		;319e	88		.
	defb 0aeh		;319f	ae		.
	defb 03bh		;31a0	3b		;
	defb 0b2h		;31a1	b2		.
	defb 08eh		;31a2	8e		.
	defb 092h		;31a3	92		.
	defb 08ch		;31a4	8c		.
	defb 09dh		;31a5	9d		.
	defb 047h		;31a6	47		G
	defb 02ah		;31a7	2a		*
	defb 0f8h		;31a8	f8		.
	defb 0bfh		;31a9	bf		.
	defb 0adh		;31aa	ad		.
	defb 0fbh		;31ab	fb		.
	defb 0c3h		;31ac	c3		.
	defb 048h		;31ad	48		H
	defb 09eh		;31ae	9e		.
	defb 03ch		;31af	3c		<
	defb 021h		;31b0	21		!
	defb 045h		;31b1	45		E
	defb 0b6h		;31b2	b6		.
	defb 0edh		;31b3	ed		.
	defb 04bh		;31b4	4b		K
	defb 043h		;31b5	43		C
	defb 092h		;31b6	92		.
	defb 095h		;31b7	95		.
	defb 008h		;31b8	08		.
	defb 081h		;31b9	81		.
	defb 0c0h		;31ba	c0		.
	defb 07eh		;31bb	7e		~
	defb 01fh		;31bc	1f		.
	defb 03dh		;31bd	3d		=
	defb 020h		;31be	20		 
	defb 0f7h		;31bf	f7		.
	defb 0ech		;31c0	ec		.
	defb 064h		;31c1	64		d
	defb 076h		;31c2	76		v
	defb 0cbh		;31c3	cb		.
	defb 03fh		;31c4	3f		?
	defb 04fh		;31c5	4f		O
	defb 0b0h		;31c6	b0		.
	defb 0d7h		;31c7	d7		.
	defb 028h		;31c8	28		(
	defb 05fh		;31c9	5f		_
	defb 0c3h		;31ca	c3		.
	defb 0aeh		;31cb	ae		.
	defb 089h		;31cc	89		.
	defb 0ach		;31cd	ac		.
	defb 022h		;31ce	22		"
	defb 0dch		;31cf	dc		.
	defb 008h		;31d0	08		.
	defb 0ach		;31d1	ac		.
	defb 02ah		;31d2	2a		*
	defb 0a7h		;31d3	a7		.
	defb 0cch		;31d4	cc		.
	defb 07bh		;31d5	7b		{
	defb 0fch		;31d6	fc		.
	defb 018h		;31d7	18		.
	defb 03eh		;31d8	3e		>
	defb 02fh		;31d9	2f		/
	defb 06eh		;31da	6e		n
	defb 00eh		;31db	0e		.
	defb 003h		;31dc	03		.
	defb 004h		;31dd	04		.
	defb 020h		;31de	20		 
	defb 01bh		;31df	1b		.
	defb 007h		;31e0	07		.
	defb 0e7h		;31e1	e7		.
	defb 021h		;31e2	21		!
	defb 0bbh		;31e3	bb		.
	defb 0a8h		;31e4	a8		.
	defb 011h		;31e5	11		.
	defb 001h		;31e6	01		.
	defb 0a5h		;31e7	a5		.
	defb 0bdh		;31e8	bd		.
	defb 049h		;31e9	49		I
	defb 003h		;31ea	03		.
	defb 0f7h		;31eb	f7		.
	defb 043h		;31ec	43		C
	defb 0d9h		;31ed	d9		.
	defb 0b0h		;31ee	b0		.
	defb 021h		;31ef	21		!
	defb 098h		;31f0	98		.
	defb 0a3h		;31f1	a3		.
	defb 0e7h		;31f2	e7		.
	defb 064h		;31f3	64		d
	defb 090h		;31f4	90		.
	defb 001h		;31f5	01		.
	defb 023h		;31f6	23		#
	defb 005h		;31f7	05		.
	defb 0b6h		;31f8	b6		.
	defb 0afh		;31f9	af		.
	defb 0c9h		;31fa	c9		.
	defb 021h		;31fb	21		!
	defb 0d2h		;31fc	d2		.
	defb 0b1h		;31fd	b1		.
	defb 0f4h		;31fe	f4		.
	defb 02dh		;31ff	2d		-
	defb 0e1h		;3200	e1		.
	defb 0b1h		;3201	b1		.
	defb 05dh		;3202	5d		]
	defb 060h		;3203	60		`
	defb 0ach		;3204	ac		.
	defb 007h		;3205	07		.
	defb 06ch		;3206	6c		l
	defb 072h		;3207	72		r
	defb 000h		;3208	00		.
	defb 080h		;3209	80		.
l320ah:

; BLOCK 'Fill320A' (start 0x320a end 0x3fd0)
Fill320A:
	defb 0ffh		;320a	ff		.
	defb 0ffh		;320b	ff		.
	defb 0ffh		;320c	ff		.
	defb 0ffh		;320d	ff		.
	defb 0ffh		;320e	ff		.
	defb 0ffh		;320f	ff		.
	defb 0ffh		;3210	ff		.
	defb 0ffh		;3211	ff		.
	defb 0ffh		;3212	ff		.
	defb 0ffh		;3213	ff		.
	defb 0ffh		;3214	ff		.
	defb 0ffh		;3215	ff		.
	defb 0ffh		;3216	ff		.
	defb 0ffh		;3217	ff		.
	defb 0ffh		;3218	ff		.
	defb 0ffh		;3219	ff		.
	defb 0ffh		;321a	ff		.
	defb 0ffh		;321b	ff		.
	defb 0ffh		;321c	ff		.
	defb 0ffh		;321d	ff		.
	defb 0ffh		;321e	ff		.
	defb 0ffh		;321f	ff		.
	defb 0ffh		;3220	ff		.
	defb 0ffh		;3221	ff		.
	defb 0ffh		;3222	ff		.
	defb 0ffh		;3223	ff		.
	defb 0ffh		;3224	ff		.
	defb 0ffh		;3225	ff		.
	defb 0ffh		;3226	ff		.
	defb 0ffh		;3227	ff		.
	defb 0ffh		;3228	ff		.
	defb 0ffh		;3229	ff		.
	defb 0ffh		;322a	ff		.
	defb 0ffh		;322b	ff		.
	defb 0ffh		;322c	ff		.
	defb 0ffh		;322d	ff		.
	defb 0ffh		;322e	ff		.
	defb 0ffh		;322f	ff		.
	defb 0ffh		;3230	ff		.
	defb 0ffh		;3231	ff		.
	defb 0ffh		;3232	ff		.
	defb 0ffh		;3233	ff		.
	defb 0ffh		;3234	ff		.
	defb 0ffh		;3235	ff		.
	defb 0ffh		;3236	ff		.
	defb 0ffh		;3237	ff		.
	defb 0ffh		;3238	ff		.
	defb 0ffh		;3239	ff		.
	defb 0ffh		;323a	ff		.
	defb 0ffh		;323b	ff		.
	defb 0ffh		;323c	ff		.
	defb 0ffh		;323d	ff		.
	defb 0ffh		;323e	ff		.
	defb 0ffh		;323f	ff		.
	defb 0ffh		;3240	ff		.
	defb 0ffh		;3241	ff		.
	defb 0ffh		;3242	ff		.
	defb 0ffh		;3243	ff		.
	defb 0ffh		;3244	ff		.
	defb 0ffh		;3245	ff		.
	defb 0ffh		;3246	ff		.
	defb 0ffh		;3247	ff		.
	defb 0ffh		;3248	ff		.
	defb 0ffh		;3249	ff		.
	defb 0ffh		;324a	ff		.
	defb 0ffh		;324b	ff		.
	defb 0ffh		;324c	ff		.
	defb 0ffh		;324d	ff		.
	defb 0ffh		;324e	ff		.
	defb 0ffh		;324f	ff		.
	defb 0ffh		;3250	ff		.
	defb 0ffh		;3251	ff		.
	defb 0ffh		;3252	ff		.
	defb 0ffh		;3253	ff		.
	defb 0ffh		;3254	ff		.
	defb 0ffh		;3255	ff		.
	defb 0ffh		;3256	ff		.
	defb 0ffh		;3257	ff		.
	defb 0ffh		;3258	ff		.
	defb 0ffh		;3259	ff		.
	defb 0ffh		;325a	ff		.
	defb 0ffh		;325b	ff		.
	defb 0ffh		;325c	ff		.
	defb 0ffh		;325d	ff		.
	defb 0ffh		;325e	ff		.
	defb 0ffh		;325f	ff		.
	defb 0ffh		;3260	ff		.
	defb 0ffh		;3261	ff		.
	defb 0ffh		;3262	ff		.
	defb 0ffh		;3263	ff		.
	defb 0ffh		;3264	ff		.
	defb 0ffh		;3265	ff		.
	defb 0ffh		;3266	ff		.
	defb 0ffh		;3267	ff		.
	defb 0ffh		;3268	ff		.
	defb 0ffh		;3269	ff		.
	defb 0ffh		;326a	ff		.
	defb 0ffh		;326b	ff		.
	defb 0ffh		;326c	ff		.
	defb 0ffh		;326d	ff		.
	defb 0ffh		;326e	ff		.
	defb 0ffh		;326f	ff		.
	defb 0ffh		;3270	ff		.
	defb 0ffh		;3271	ff		.
	defb 0ffh		;3272	ff		.
	defb 0ffh		;3273	ff		.
	defb 0ffh		;3274	ff		.
	defb 0ffh		;3275	ff		.
	defb 0ffh		;3276	ff		.
	defb 0ffh		;3277	ff		.
	defb 0ffh		;3278	ff		.
	defb 0ffh		;3279	ff		.
	defb 0ffh		;327a	ff		.
	defb 0ffh		;327b	ff		.
	defb 0ffh		;327c	ff		.
	defb 0ffh		;327d	ff		.
	defb 0ffh		;327e	ff		.
	defb 0ffh		;327f	ff		.
	defb 0ffh		;3280	ff		.
	defb 0ffh		;3281	ff		.
	defb 0ffh		;3282	ff		.
	defb 0ffh		;3283	ff		.
	defb 0ffh		;3284	ff		.
	defb 0ffh		;3285	ff		.
	defb 0ffh		;3286	ff		.
	defb 0ffh		;3287	ff		.
	defb 0ffh		;3288	ff		.
	defb 0ffh		;3289	ff		.
	defb 0ffh		;328a	ff		.
	defb 0ffh		;328b	ff		.
	defb 0ffh		;328c	ff		.
	defb 0ffh		;328d	ff		.
	defb 0ffh		;328e	ff		.
	defb 0ffh		;328f	ff		.
	defb 0ffh		;3290	ff		.
	defb 0ffh		;3291	ff		.
	defb 0ffh		;3292	ff		.
	defb 0ffh		;3293	ff		.
	defb 0ffh		;3294	ff		.
	defb 0ffh		;3295	ff		.
	defb 0ffh		;3296	ff		.
	defb 0ffh		;3297	ff		.
	defb 0ffh		;3298	ff		.
	defb 0ffh		;3299	ff		.
	defb 0ffh		;329a	ff		.
	defb 0ffh		;329b	ff		.
	defb 0ffh		;329c	ff		.
	defb 0ffh		;329d	ff		.
	defb 0ffh		;329e	ff		.
	defb 0ffh		;329f	ff		.
	defb 0ffh		;32a0	ff		.
	defb 0ffh		;32a1	ff		.
	defb 0ffh		;32a2	ff		.
	defb 0ffh		;32a3	ff		.
	defb 0ffh		;32a4	ff		.
	defb 0ffh		;32a5	ff		.
	defb 0ffh		;32a6	ff		.
	defb 0ffh		;32a7	ff		.
	defb 0ffh		;32a8	ff		.
	defb 0ffh		;32a9	ff		.
	defb 0ffh		;32aa	ff		.
	defb 0ffh		;32ab	ff		.
	defb 0ffh		;32ac	ff		.
	defb 0ffh		;32ad	ff		.
	defb 0ffh		;32ae	ff		.
	defb 0ffh		;32af	ff		.
	defb 0ffh		;32b0	ff		.
	defb 0ffh		;32b1	ff		.
	defb 0ffh		;32b2	ff		.
	defb 0ffh		;32b3	ff		.
	defb 0ffh		;32b4	ff		.
	defb 0ffh		;32b5	ff		.
	defb 0ffh		;32b6	ff		.
	defb 0ffh		;32b7	ff		.
	defb 0ffh		;32b8	ff		.
	defb 0ffh		;32b9	ff		.
	defb 0ffh		;32ba	ff		.
	defb 0ffh		;32bb	ff		.
	defb 0ffh		;32bc	ff		.
	defb 0ffh		;32bd	ff		.
	defb 0ffh		;32be	ff		.
	defb 0ffh		;32bf	ff		.
	defb 0ffh		;32c0	ff		.
	defb 0ffh		;32c1	ff		.
	defb 0ffh		;32c2	ff		.
	defb 0ffh		;32c3	ff		.
	defb 0ffh		;32c4	ff		.
	defb 0ffh		;32c5	ff		.
	defb 0ffh		;32c6	ff		.
	defb 0ffh		;32c7	ff		.
	defb 0ffh		;32c8	ff		.
	defb 0ffh		;32c9	ff		.
	defb 0ffh		;32ca	ff		.
	defb 0ffh		;32cb	ff		.
	defb 0ffh		;32cc	ff		.
	defb 0ffh		;32cd	ff		.
	defb 0ffh		;32ce	ff		.
	defb 0ffh		;32cf	ff		.
	defb 0ffh		;32d0	ff		.
	defb 0ffh		;32d1	ff		.
	defb 0ffh		;32d2	ff		.
	defb 0ffh		;32d3	ff		.
	defb 0ffh		;32d4	ff		.
	defb 0ffh		;32d5	ff		.
	defb 0ffh		;32d6	ff		.
	defb 0ffh		;32d7	ff		.
	defb 0ffh		;32d8	ff		.
	defb 0ffh		;32d9	ff		.
	defb 0ffh		;32da	ff		.
	defb 0ffh		;32db	ff		.
	defb 0ffh		;32dc	ff		.
	defb 0ffh		;32dd	ff		.
	defb 0ffh		;32de	ff		.
	defb 0ffh		;32df	ff		.
	defb 0ffh		;32e0	ff		.
	defb 0ffh		;32e1	ff		.
	defb 0ffh		;32e2	ff		.
	defb 0ffh		;32e3	ff		.
	defb 0ffh		;32e4	ff		.
	defb 0ffh		;32e5	ff		.
	defb 0ffh		;32e6	ff		.
	defb 0ffh		;32e7	ff		.
	defb 0ffh		;32e8	ff		.
	defb 0ffh		;32e9	ff		.
	defb 0ffh		;32ea	ff		.
	defb 0ffh		;32eb	ff		.
	defb 0ffh		;32ec	ff		.
	defb 0ffh		;32ed	ff		.
	defb 0ffh		;32ee	ff		.
	defb 0ffh		;32ef	ff		.
	defb 0ffh		;32f0	ff		.
	defb 0ffh		;32f1	ff		.
	defb 0ffh		;32f2	ff		.
	defb 0ffh		;32f3	ff		.
	defb 0ffh		;32f4	ff		.
	defb 0ffh		;32f5	ff		.
	defb 0ffh		;32f6	ff		.
	defb 0ffh		;32f7	ff		.
	defb 0ffh		;32f8	ff		.
	defb 0ffh		;32f9	ff		.
	defb 0ffh		;32fa	ff		.
	defb 0ffh		;32fb	ff		.
	defb 0ffh		;32fc	ff		.
	defb 0ffh		;32fd	ff		.
	defb 0ffh		;32fe	ff		.
	defb 0ffh		;32ff	ff		.
	defb 0ffh		;3300	ff		.
	defb 0ffh		;3301	ff		.
	defb 0ffh		;3302	ff		.
	defb 0ffh		;3303	ff		.
	defb 0ffh		;3304	ff		.
	defb 0ffh		;3305	ff		.
	defb 0ffh		;3306	ff		.
	defb 0ffh		;3307	ff		.
	defb 0ffh		;3308	ff		.
	defb 0ffh		;3309	ff		.
	defb 0ffh		;330a	ff		.
	defb 0ffh		;330b	ff		.
	defb 0ffh		;330c	ff		.
	defb 0ffh		;330d	ff		.
	defb 0ffh		;330e	ff		.
	defb 0ffh		;330f	ff		.
	defb 0ffh		;3310	ff		.
	defb 0ffh		;3311	ff		.
	defb 0ffh		;3312	ff		.
	defb 0ffh		;3313	ff		.
	defb 0ffh		;3314	ff		.
	defb 0ffh		;3315	ff		.
	defb 0ffh		;3316	ff		.
	defb 0ffh		;3317	ff		.
	defb 0ffh		;3318	ff		.
	defb 0ffh		;3319	ff		.
	defb 0ffh		;331a	ff		.
	defb 0ffh		;331b	ff		.
	defb 0ffh		;331c	ff		.
	defb 0ffh		;331d	ff		.
	defb 0ffh		;331e	ff		.
	defb 0ffh		;331f	ff		.
	defb 0ffh		;3320	ff		.
	defb 0ffh		;3321	ff		.
	defb 0ffh		;3322	ff		.
	defb 0ffh		;3323	ff		.
	defb 0ffh		;3324	ff		.
	defb 0ffh		;3325	ff		.
	defb 0ffh		;3326	ff		.
	defb 0ffh		;3327	ff		.
	defb 0ffh		;3328	ff		.
	defb 0ffh		;3329	ff		.
	defb 0ffh		;332a	ff		.
	defb 0ffh		;332b	ff		.
	defb 0ffh		;332c	ff		.
	defb 0ffh		;332d	ff		.
	defb 0ffh		;332e	ff		.
	defb 0ffh		;332f	ff		.
	defb 0ffh		;3330	ff		.
	defb 0ffh		;3331	ff		.
	defb 0ffh		;3332	ff		.
	defb 0ffh		;3333	ff		.
	defb 0ffh		;3334	ff		.
	defb 0ffh		;3335	ff		.
	defb 0ffh		;3336	ff		.
	defb 0ffh		;3337	ff		.
	defb 0ffh		;3338	ff		.
	defb 0ffh		;3339	ff		.
	defb 0ffh		;333a	ff		.
	defb 0ffh		;333b	ff		.
	defb 0ffh		;333c	ff		.
	defb 0ffh		;333d	ff		.
	defb 0ffh		;333e	ff		.
	defb 0ffh		;333f	ff		.
	defb 0ffh		;3340	ff		.
	defb 0ffh		;3341	ff		.
	defb 0ffh		;3342	ff		.
	defb 0ffh		;3343	ff		.
	defb 0ffh		;3344	ff		.
	defb 0ffh		;3345	ff		.
	defb 0ffh		;3346	ff		.
	defb 0ffh		;3347	ff		.
	defb 0ffh		;3348	ff		.
	defb 0ffh		;3349	ff		.
	defb 0ffh		;334a	ff		.
	defb 0ffh		;334b	ff		.
	defb 0ffh		;334c	ff		.
	defb 0ffh		;334d	ff		.
	defb 0ffh		;334e	ff		.
	defb 0ffh		;334f	ff		.
	defb 0ffh		;3350	ff		.
	defb 0ffh		;3351	ff		.
	defb 0ffh		;3352	ff		.
	defb 0ffh		;3353	ff		.
	defb 0ffh		;3354	ff		.
	defb 0ffh		;3355	ff		.
	defb 0ffh		;3356	ff		.
	defb 0ffh		;3357	ff		.
	defb 0ffh		;3358	ff		.
	defb 0ffh		;3359	ff		.
	defb 0ffh		;335a	ff		.
	defb 0ffh		;335b	ff		.
	defb 0ffh		;335c	ff		.
	defb 0ffh		;335d	ff		.
	defb 0ffh		;335e	ff		.
	defb 0ffh		;335f	ff		.
	defb 0ffh		;3360	ff		.
	defb 0ffh		;3361	ff		.
	defb 0ffh		;3362	ff		.
	defb 0ffh		;3363	ff		.
	defb 0ffh		;3364	ff		.
	defb 0ffh		;3365	ff		.
	defb 0ffh		;3366	ff		.
	defb 0ffh		;3367	ff		.
	defb 0ffh		;3368	ff		.
	defb 0ffh		;3369	ff		.
	defb 0ffh		;336a	ff		.
	defb 0ffh		;336b	ff		.
	defb 0ffh		;336c	ff		.
	defb 0ffh		;336d	ff		.
	defb 0ffh		;336e	ff		.
	defb 0ffh		;336f	ff		.
	defb 0ffh		;3370	ff		.
	defb 0ffh		;3371	ff		.
	defb 0ffh		;3372	ff		.
	defb 0ffh		;3373	ff		.
	defb 0ffh		;3374	ff		.
	defb 0ffh		;3375	ff		.
	defb 0ffh		;3376	ff		.
	defb 0ffh		;3377	ff		.
	defb 0ffh		;3378	ff		.
	defb 0ffh		;3379	ff		.
	defb 0ffh		;337a	ff		.
	defb 0ffh		;337b	ff		.
	defb 0ffh		;337c	ff		.
	defb 0ffh		;337d	ff		.
	defb 0ffh		;337e	ff		.
	defb 0ffh		;337f	ff		.
	defb 0ffh		;3380	ff		.
	defb 0ffh		;3381	ff		.
	defb 0ffh		;3382	ff		.
	defb 0ffh		;3383	ff		.
	defb 0ffh		;3384	ff		.
	defb 0ffh		;3385	ff		.
	defb 0ffh		;3386	ff		.
	defb 0ffh		;3387	ff		.
	defb 0ffh		;3388	ff		.
	defb 0ffh		;3389	ff		.
	defb 0ffh		;338a	ff		.
	defb 0ffh		;338b	ff		.
	defb 0ffh		;338c	ff		.
	defb 0ffh		;338d	ff		.
	defb 0ffh		;338e	ff		.
	defb 0ffh		;338f	ff		.
	defb 0ffh		;3390	ff		.
	defb 0ffh		;3391	ff		.
	defb 0ffh		;3392	ff		.
	defb 0ffh		;3393	ff		.
	defb 0ffh		;3394	ff		.
	defb 0ffh		;3395	ff		.
	defb 0ffh		;3396	ff		.
	defb 0ffh		;3397	ff		.
	defb 0ffh		;3398	ff		.
	defb 0ffh		;3399	ff		.
	defb 0ffh		;339a	ff		.
	defb 0ffh		;339b	ff		.
	defb 0ffh		;339c	ff		.
	defb 0ffh		;339d	ff		.
	defb 0ffh		;339e	ff		.
	defb 0ffh		;339f	ff		.
	defb 0ffh		;33a0	ff		.
	defb 0ffh		;33a1	ff		.
	defb 0ffh		;33a2	ff		.
	defb 0ffh		;33a3	ff		.
	defb 0ffh		;33a4	ff		.
	defb 0ffh		;33a5	ff		.
	defb 0ffh		;33a6	ff		.
	defb 0ffh		;33a7	ff		.
	defb 0ffh		;33a8	ff		.
	defb 0ffh		;33a9	ff		.
	defb 0ffh		;33aa	ff		.
	defb 0ffh		;33ab	ff		.
	defb 0ffh		;33ac	ff		.
	defb 0ffh		;33ad	ff		.
	defb 0ffh		;33ae	ff		.
	defb 0ffh		;33af	ff		.
	defb 0ffh		;33b0	ff		.
	defb 0ffh		;33b1	ff		.
	defb 0ffh		;33b2	ff		.
	defb 0ffh		;33b3	ff		.
	defb 0ffh		;33b4	ff		.
	defb 0ffh		;33b5	ff		.
	defb 0ffh		;33b6	ff		.
	defb 0ffh		;33b7	ff		.
	defb 0ffh		;33b8	ff		.
	defb 0ffh		;33b9	ff		.
	defb 0ffh		;33ba	ff		.
	defb 0ffh		;33bb	ff		.
	defb 0ffh		;33bc	ff		.
	defb 0ffh		;33bd	ff		.
	defb 0ffh		;33be	ff		.
	defb 0ffh		;33bf	ff		.
	defb 0ffh		;33c0	ff		.
	defb 0ffh		;33c1	ff		.
	defb 0ffh		;33c2	ff		.
	defb 0ffh		;33c3	ff		.
	defb 0ffh		;33c4	ff		.
	defb 0ffh		;33c5	ff		.
	defb 0ffh		;33c6	ff		.
	defb 0ffh		;33c7	ff		.
	defb 0ffh		;33c8	ff		.
	defb 0ffh		;33c9	ff		.
	defb 0ffh		;33ca	ff		.
	defb 0ffh		;33cb	ff		.
	defb 0ffh		;33cc	ff		.
	defb 0ffh		;33cd	ff		.
	defb 0ffh		;33ce	ff		.
	defb 0ffh		;33cf	ff		.
	defb 0ffh		;33d0	ff		.
	defb 0ffh		;33d1	ff		.
	defb 0ffh		;33d2	ff		.
	defb 0ffh		;33d3	ff		.
	defb 0ffh		;33d4	ff		.
	defb 0ffh		;33d5	ff		.
	defb 0ffh		;33d6	ff		.
	defb 0ffh		;33d7	ff		.
	defb 0ffh		;33d8	ff		.
	defb 0ffh		;33d9	ff		.
	defb 0ffh		;33da	ff		.
	defb 0ffh		;33db	ff		.
	defb 0ffh		;33dc	ff		.
	defb 0ffh		;33dd	ff		.
	defb 0ffh		;33de	ff		.
	defb 0ffh		;33df	ff		.
	defb 0ffh		;33e0	ff		.
	defb 0ffh		;33e1	ff		.
	defb 0ffh		;33e2	ff		.
	defb 0ffh		;33e3	ff		.
	defb 0ffh		;33e4	ff		.
	defb 0ffh		;33e5	ff		.
	defb 0ffh		;33e6	ff		.
	defb 0ffh		;33e7	ff		.
	defb 0ffh		;33e8	ff		.
	defb 0ffh		;33e9	ff		.
	defb 0ffh		;33ea	ff		.
	defb 0ffh		;33eb	ff		.
	defb 0ffh		;33ec	ff		.
	defb 0ffh		;33ed	ff		.
	defb 0ffh		;33ee	ff		.
	defb 0ffh		;33ef	ff		.
	defb 0ffh		;33f0	ff		.
	defb 0ffh		;33f1	ff		.
	defb 0ffh		;33f2	ff		.
	defb 0ffh		;33f3	ff		.
	defb 0ffh		;33f4	ff		.
	defb 0ffh		;33f5	ff		.
	defb 0ffh		;33f6	ff		.
	defb 0ffh		;33f7	ff		.
	defb 0ffh		;33f8	ff		.
	defb 0ffh		;33f9	ff		.
	defb 0ffh		;33fa	ff		.
	defb 0ffh		;33fb	ff		.
	defb 0ffh		;33fc	ff		.
	defb 0ffh		;33fd	ff		.
	defb 0ffh		;33fe	ff		.
	defb 0ffh		;33ff	ff		.
	defb 0ffh		;3400	ff		.
	defb 0ffh		;3401	ff		.
	defb 0ffh		;3402	ff		.
	defb 0ffh		;3403	ff		.
	defb 0ffh		;3404	ff		.
	defb 0ffh		;3405	ff		.
	defb 0ffh		;3406	ff		.
	defb 0ffh		;3407	ff		.
	defb 0ffh		;3408	ff		.
	defb 0ffh		;3409	ff		.
	defb 0ffh		;340a	ff		.
	defb 0ffh		;340b	ff		.
	defb 0ffh		;340c	ff		.
	defb 0ffh		;340d	ff		.
	defb 0ffh		;340e	ff		.
	defb 0ffh		;340f	ff		.
	defb 0ffh		;3410	ff		.
	defb 0ffh		;3411	ff		.
	defb 0ffh		;3412	ff		.
	defb 0ffh		;3413	ff		.
	defb 0ffh		;3414	ff		.
	defb 0ffh		;3415	ff		.
	defb 0ffh		;3416	ff		.
	defb 0ffh		;3417	ff		.
	defb 0ffh		;3418	ff		.
	defb 0ffh		;3419	ff		.
	defb 0ffh		;341a	ff		.
	defb 0ffh		;341b	ff		.
	defb 0ffh		;341c	ff		.
	defb 0ffh		;341d	ff		.
	defb 0ffh		;341e	ff		.
	defb 0ffh		;341f	ff		.
	defb 0ffh		;3420	ff		.
	defb 0ffh		;3421	ff		.
	defb 0ffh		;3422	ff		.
	defb 0ffh		;3423	ff		.
	defb 0ffh		;3424	ff		.
	defb 0ffh		;3425	ff		.
	defb 0ffh		;3426	ff		.
	defb 0ffh		;3427	ff		.
	defb 0ffh		;3428	ff		.
	defb 0ffh		;3429	ff		.
	defb 0ffh		;342a	ff		.
	defb 0ffh		;342b	ff		.
	defb 0ffh		;342c	ff		.
	defb 0ffh		;342d	ff		.
	defb 0ffh		;342e	ff		.
	defb 0ffh		;342f	ff		.
	defb 0ffh		;3430	ff		.
	defb 0ffh		;3431	ff		.
	defb 0ffh		;3432	ff		.
	defb 0ffh		;3433	ff		.
	defb 0ffh		;3434	ff		.
	defb 0ffh		;3435	ff		.
	defb 0ffh		;3436	ff		.
	defb 0ffh		;3437	ff		.
	defb 0ffh		;3438	ff		.
	defb 0ffh		;3439	ff		.
	defb 0ffh		;343a	ff		.
	defb 0ffh		;343b	ff		.
	defb 0ffh		;343c	ff		.
	defb 0ffh		;343d	ff		.
	defb 0ffh		;343e	ff		.
	defb 0ffh		;343f	ff		.
	defb 0ffh		;3440	ff		.
	defb 0ffh		;3441	ff		.
	defb 0ffh		;3442	ff		.
	defb 0ffh		;3443	ff		.
	defb 0ffh		;3444	ff		.
	defb 0ffh		;3445	ff		.
	defb 0ffh		;3446	ff		.
	defb 0ffh		;3447	ff		.
	defb 0ffh		;3448	ff		.
	defb 0ffh		;3449	ff		.
	defb 0ffh		;344a	ff		.
	defb 0ffh		;344b	ff		.
	defb 0ffh		;344c	ff		.
	defb 0ffh		;344d	ff		.
	defb 0ffh		;344e	ff		.
	defb 0ffh		;344f	ff		.
	defb 0ffh		;3450	ff		.
	defb 0ffh		;3451	ff		.
	defb 0ffh		;3452	ff		.
	defb 0ffh		;3453	ff		.
	defb 0ffh		;3454	ff		.
	defb 0ffh		;3455	ff		.
	defb 0ffh		;3456	ff		.
	defb 0ffh		;3457	ff		.
	defb 0ffh		;3458	ff		.
	defb 0ffh		;3459	ff		.
	defb 0ffh		;345a	ff		.
	defb 0ffh		;345b	ff		.
	defb 0ffh		;345c	ff		.
	defb 0ffh		;345d	ff		.
	defb 0ffh		;345e	ff		.
	defb 0ffh		;345f	ff		.
	defb 0ffh		;3460	ff		.
	defb 0ffh		;3461	ff		.
	defb 0ffh		;3462	ff		.
	defb 0ffh		;3463	ff		.
	defb 0ffh		;3464	ff		.
	defb 0ffh		;3465	ff		.
	defb 0ffh		;3466	ff		.
	defb 0ffh		;3467	ff		.
	defb 0ffh		;3468	ff		.
	defb 0ffh		;3469	ff		.
	defb 0ffh		;346a	ff		.
	defb 0ffh		;346b	ff		.
	defb 0ffh		;346c	ff		.
	defb 0ffh		;346d	ff		.
	defb 0ffh		;346e	ff		.
	defb 0ffh		;346f	ff		.
	defb 0ffh		;3470	ff		.
	defb 0ffh		;3471	ff		.
	defb 0ffh		;3472	ff		.
	defb 0ffh		;3473	ff		.
	defb 0ffh		;3474	ff		.
	defb 0ffh		;3475	ff		.
	defb 0ffh		;3476	ff		.
	defb 0ffh		;3477	ff		.
	defb 0ffh		;3478	ff		.
	defb 0ffh		;3479	ff		.
	defb 0ffh		;347a	ff		.
	defb 0ffh		;347b	ff		.
	defb 0ffh		;347c	ff		.
	defb 0ffh		;347d	ff		.
	defb 0ffh		;347e	ff		.
	defb 0ffh		;347f	ff		.
	defb 0ffh		;3480	ff		.
	defb 0ffh		;3481	ff		.
	defb 0ffh		;3482	ff		.
	defb 0ffh		;3483	ff		.
	defb 0ffh		;3484	ff		.
	defb 0ffh		;3485	ff		.
	defb 0ffh		;3486	ff		.
	defb 0ffh		;3487	ff		.
	defb 0ffh		;3488	ff		.
	defb 0ffh		;3489	ff		.
	defb 0ffh		;348a	ff		.
	defb 0ffh		;348b	ff		.
	defb 0ffh		;348c	ff		.
	defb 0ffh		;348d	ff		.
	defb 0ffh		;348e	ff		.
	defb 0ffh		;348f	ff		.
	defb 0ffh		;3490	ff		.
	defb 0ffh		;3491	ff		.
	defb 0ffh		;3492	ff		.
	defb 0ffh		;3493	ff		.
	defb 0ffh		;3494	ff		.
	defb 0ffh		;3495	ff		.
	defb 0ffh		;3496	ff		.
	defb 0ffh		;3497	ff		.
	defb 0ffh		;3498	ff		.
	defb 0ffh		;3499	ff		.
	defb 0ffh		;349a	ff		.
	defb 0ffh		;349b	ff		.
	defb 0ffh		;349c	ff		.
	defb 0ffh		;349d	ff		.
	defb 0ffh		;349e	ff		.
	defb 0ffh		;349f	ff		.
	defb 0ffh		;34a0	ff		.
	defb 0ffh		;34a1	ff		.
	defb 0ffh		;34a2	ff		.
	defb 0ffh		;34a3	ff		.
	defb 0ffh		;34a4	ff		.
	defb 0ffh		;34a5	ff		.
	defb 0ffh		;34a6	ff		.
	defb 0ffh		;34a7	ff		.
	defb 0ffh		;34a8	ff		.
	defb 0ffh		;34a9	ff		.
	defb 0ffh		;34aa	ff		.
	defb 0ffh		;34ab	ff		.
	defb 0ffh		;34ac	ff		.
	defb 0ffh		;34ad	ff		.
	defb 0ffh		;34ae	ff		.
	defb 0ffh		;34af	ff		.
	defb 0ffh		;34b0	ff		.
	defb 0ffh		;34b1	ff		.
	defb 0ffh		;34b2	ff		.
	defb 0ffh		;34b3	ff		.
	defb 0ffh		;34b4	ff		.
	defb 0ffh		;34b5	ff		.
	defb 0ffh		;34b6	ff		.
	defb 0ffh		;34b7	ff		.
	defb 0ffh		;34b8	ff		.
	defb 0ffh		;34b9	ff		.
	defb 0ffh		;34ba	ff		.
	defb 0ffh		;34bb	ff		.
	defb 0ffh		;34bc	ff		.
	defb 0ffh		;34bd	ff		.
	defb 0ffh		;34be	ff		.
	defb 0ffh		;34bf	ff		.
	defb 0ffh		;34c0	ff		.
	defb 0ffh		;34c1	ff		.
	defb 0ffh		;34c2	ff		.
	defb 0ffh		;34c3	ff		.
	defb 0ffh		;34c4	ff		.
	defb 0ffh		;34c5	ff		.
	defb 0ffh		;34c6	ff		.
	defb 0ffh		;34c7	ff		.
	defb 0ffh		;34c8	ff		.
	defb 0ffh		;34c9	ff		.
	defb 0ffh		;34ca	ff		.
	defb 0ffh		;34cb	ff		.
	defb 0ffh		;34cc	ff		.
	defb 0ffh		;34cd	ff		.
	defb 0ffh		;34ce	ff		.
	defb 0ffh		;34cf	ff		.
	defb 0ffh		;34d0	ff		.
	defb 0ffh		;34d1	ff		.
	defb 0ffh		;34d2	ff		.
	defb 0ffh		;34d3	ff		.
	defb 0ffh		;34d4	ff		.
	defb 0ffh		;34d5	ff		.
	defb 0ffh		;34d6	ff		.
	defb 0ffh		;34d7	ff		.
	defb 0ffh		;34d8	ff		.
	defb 0ffh		;34d9	ff		.
	defb 0ffh		;34da	ff		.
	defb 0ffh		;34db	ff		.
	defb 0ffh		;34dc	ff		.
	defb 0ffh		;34dd	ff		.
	defb 0ffh		;34de	ff		.
	defb 0ffh		;34df	ff		.
	defb 0ffh		;34e0	ff		.
	defb 0ffh		;34e1	ff		.
	defb 0ffh		;34e2	ff		.
	defb 0ffh		;34e3	ff		.
	defb 0ffh		;34e4	ff		.
	defb 0ffh		;34e5	ff		.
	defb 0ffh		;34e6	ff		.
	defb 0ffh		;34e7	ff		.
	defb 0ffh		;34e8	ff		.
	defb 0ffh		;34e9	ff		.
	defb 0ffh		;34ea	ff		.
	defb 0ffh		;34eb	ff		.
	defb 0ffh		;34ec	ff		.
	defb 0ffh		;34ed	ff		.
	defb 0ffh		;34ee	ff		.
	defb 0ffh		;34ef	ff		.
	defb 0ffh		;34f0	ff		.
	defb 0ffh		;34f1	ff		.
	defb 0ffh		;34f2	ff		.
	defb 0ffh		;34f3	ff		.
	defb 0ffh		;34f4	ff		.
	defb 0ffh		;34f5	ff		.
	defb 0ffh		;34f6	ff		.
	defb 0ffh		;34f7	ff		.
	defb 0ffh		;34f8	ff		.
	defb 0ffh		;34f9	ff		.
	defb 0ffh		;34fa	ff		.
	defb 0ffh		;34fb	ff		.
	defb 0ffh		;34fc	ff		.
	defb 0ffh		;34fd	ff		.
	defb 0ffh		;34fe	ff		.
	defb 0ffh		;34ff	ff		.
	defb 0ffh		;3500	ff		.
	defb 0ffh		;3501	ff		.
	defb 0ffh		;3502	ff		.
	defb 0ffh		;3503	ff		.
	defb 0ffh		;3504	ff		.
	defb 0ffh		;3505	ff		.
	defb 0ffh		;3506	ff		.
	defb 0ffh		;3507	ff		.
	defb 0ffh		;3508	ff		.
	defb 0ffh		;3509	ff		.
	defb 0ffh		;350a	ff		.
	defb 0ffh		;350b	ff		.
	defb 0ffh		;350c	ff		.
	defb 0ffh		;350d	ff		.
	defb 0ffh		;350e	ff		.
	defb 0ffh		;350f	ff		.
	defb 0ffh		;3510	ff		.
	defb 0ffh		;3511	ff		.
	defb 0ffh		;3512	ff		.
	defb 0ffh		;3513	ff		.
	defb 0ffh		;3514	ff		.
	defb 0ffh		;3515	ff		.
	defb 0ffh		;3516	ff		.
	defb 0ffh		;3517	ff		.
	defb 0ffh		;3518	ff		.
	defb 0ffh		;3519	ff		.
	defb 0ffh		;351a	ff		.
	defb 0ffh		;351b	ff		.
	defb 0ffh		;351c	ff		.
	defb 0ffh		;351d	ff		.
	defb 0ffh		;351e	ff		.
	defb 0ffh		;351f	ff		.
	defb 0ffh		;3520	ff		.
	defb 0ffh		;3521	ff		.
	defb 0ffh		;3522	ff		.
	defb 0ffh		;3523	ff		.
	defb 0ffh		;3524	ff		.
	defb 0ffh		;3525	ff		.
	defb 0ffh		;3526	ff		.
	defb 0ffh		;3527	ff		.
	defb 0ffh		;3528	ff		.
	defb 0ffh		;3529	ff		.
	defb 0ffh		;352a	ff		.
	defb 0ffh		;352b	ff		.
	defb 0ffh		;352c	ff		.
	defb 0ffh		;352d	ff		.
	defb 0ffh		;352e	ff		.
	defb 0ffh		;352f	ff		.
	defb 0ffh		;3530	ff		.
	defb 0ffh		;3531	ff		.
	defb 0ffh		;3532	ff		.
	defb 0ffh		;3533	ff		.
	defb 0ffh		;3534	ff		.
	defb 0ffh		;3535	ff		.
	defb 0ffh		;3536	ff		.
	defb 0ffh		;3537	ff		.
	defb 0ffh		;3538	ff		.
	defb 0ffh		;3539	ff		.
	defb 0ffh		;353a	ff		.
	defb 0ffh		;353b	ff		.
	defb 0ffh		;353c	ff		.
	defb 0ffh		;353d	ff		.
	defb 0ffh		;353e	ff		.
	defb 0ffh		;353f	ff		.
	defb 0ffh		;3540	ff		.
	defb 0ffh		;3541	ff		.
	defb 0ffh		;3542	ff		.
	defb 0ffh		;3543	ff		.
	defb 0ffh		;3544	ff		.
	defb 0ffh		;3545	ff		.
	defb 0ffh		;3546	ff		.
	defb 0ffh		;3547	ff		.
	defb 0ffh		;3548	ff		.
	defb 0ffh		;3549	ff		.
	defb 0ffh		;354a	ff		.
	defb 0ffh		;354b	ff		.
	defb 0ffh		;354c	ff		.
	defb 0ffh		;354d	ff		.
	defb 0ffh		;354e	ff		.
	defb 0ffh		;354f	ff		.
	defb 0ffh		;3550	ff		.
	defb 0ffh		;3551	ff		.
	defb 0ffh		;3552	ff		.
	defb 0ffh		;3553	ff		.
	defb 0ffh		;3554	ff		.
	defb 0ffh		;3555	ff		.
	defb 0ffh		;3556	ff		.
	defb 0ffh		;3557	ff		.
	defb 0ffh		;3558	ff		.
	defb 0ffh		;3559	ff		.
	defb 0ffh		;355a	ff		.
	defb 0ffh		;355b	ff		.
	defb 0ffh		;355c	ff		.
	defb 0ffh		;355d	ff		.
	defb 0ffh		;355e	ff		.
	defb 0ffh		;355f	ff		.
	defb 0ffh		;3560	ff		.
	defb 0ffh		;3561	ff		.
	defb 0ffh		;3562	ff		.
	defb 0ffh		;3563	ff		.
	defb 0ffh		;3564	ff		.
	defb 0ffh		;3565	ff		.
	defb 0ffh		;3566	ff		.
	defb 0ffh		;3567	ff		.
	defb 0ffh		;3568	ff		.
	defb 0ffh		;3569	ff		.
	defb 0ffh		;356a	ff		.
	defb 0ffh		;356b	ff		.
	defb 0ffh		;356c	ff		.
	defb 0ffh		;356d	ff		.
	defb 0ffh		;356e	ff		.
	defb 0ffh		;356f	ff		.
	defb 0ffh		;3570	ff		.
	defb 0ffh		;3571	ff		.
	defb 0ffh		;3572	ff		.
	defb 0ffh		;3573	ff		.
	defb 0ffh		;3574	ff		.
	defb 0ffh		;3575	ff		.
	defb 0ffh		;3576	ff		.
	defb 0ffh		;3577	ff		.
	defb 0ffh		;3578	ff		.
	defb 0ffh		;3579	ff		.
	defb 0ffh		;357a	ff		.
	defb 0ffh		;357b	ff		.
	defb 0ffh		;357c	ff		.
	defb 0ffh		;357d	ff		.
	defb 0ffh		;357e	ff		.
	defb 0ffh		;357f	ff		.
	defb 0ffh		;3580	ff		.
	defb 0ffh		;3581	ff		.
	defb 0ffh		;3582	ff		.
	defb 0ffh		;3583	ff		.
	defb 0ffh		;3584	ff		.
	defb 0ffh		;3585	ff		.
	defb 0ffh		;3586	ff		.
	defb 0ffh		;3587	ff		.
	defb 0ffh		;3588	ff		.
	defb 0ffh		;3589	ff		.
	defb 0ffh		;358a	ff		.
	defb 0ffh		;358b	ff		.
	defb 0ffh		;358c	ff		.
	defb 0ffh		;358d	ff		.
	defb 0ffh		;358e	ff		.
	defb 0ffh		;358f	ff		.
	defb 0ffh		;3590	ff		.
	defb 0ffh		;3591	ff		.
	defb 0ffh		;3592	ff		.
	defb 0ffh		;3593	ff		.
	defb 0ffh		;3594	ff		.
	defb 0ffh		;3595	ff		.
	defb 0ffh		;3596	ff		.
	defb 0ffh		;3597	ff		.
	defb 0ffh		;3598	ff		.
	defb 0ffh		;3599	ff		.
	defb 0ffh		;359a	ff		.
	defb 0ffh		;359b	ff		.
	defb 0ffh		;359c	ff		.
	defb 0ffh		;359d	ff		.
	defb 0ffh		;359e	ff		.
	defb 0ffh		;359f	ff		.
	defb 0ffh		;35a0	ff		.
	defb 0ffh		;35a1	ff		.
	defb 0ffh		;35a2	ff		.
	defb 0ffh		;35a3	ff		.
	defb 0ffh		;35a4	ff		.
	defb 0ffh		;35a5	ff		.
	defb 0ffh		;35a6	ff		.
	defb 0ffh		;35a7	ff		.
	defb 0ffh		;35a8	ff		.
	defb 0ffh		;35a9	ff		.
	defb 0ffh		;35aa	ff		.
	defb 0ffh		;35ab	ff		.
	defb 0ffh		;35ac	ff		.
	defb 0ffh		;35ad	ff		.
	defb 0ffh		;35ae	ff		.
	defb 0ffh		;35af	ff		.
	defb 0ffh		;35b0	ff		.
	defb 0ffh		;35b1	ff		.
	defb 0ffh		;35b2	ff		.
	defb 0ffh		;35b3	ff		.
	defb 0ffh		;35b4	ff		.
	defb 0ffh		;35b5	ff		.
	defb 0ffh		;35b6	ff		.
	defb 0ffh		;35b7	ff		.
	defb 0ffh		;35b8	ff		.
	defb 0ffh		;35b9	ff		.
	defb 0ffh		;35ba	ff		.
	defb 0ffh		;35bb	ff		.
	defb 0ffh		;35bc	ff		.
	defb 0ffh		;35bd	ff		.
	defb 0ffh		;35be	ff		.
	defb 0ffh		;35bf	ff		.
	defb 0ffh		;35c0	ff		.
	defb 0ffh		;35c1	ff		.
	defb 0ffh		;35c2	ff		.
	defb 0ffh		;35c3	ff		.
	defb 0ffh		;35c4	ff		.
	defb 0ffh		;35c5	ff		.
	defb 0ffh		;35c6	ff		.
	defb 0ffh		;35c7	ff		.
	defb 0ffh		;35c8	ff		.
	defb 0ffh		;35c9	ff		.
	defb 0ffh		;35ca	ff		.
	defb 0ffh		;35cb	ff		.
	defb 0ffh		;35cc	ff		.
	defb 0ffh		;35cd	ff		.
	defb 0ffh		;35ce	ff		.
	defb 0ffh		;35cf	ff		.
	defb 0ffh		;35d0	ff		.
	defb 0ffh		;35d1	ff		.
	defb 0ffh		;35d2	ff		.
	defb 0ffh		;35d3	ff		.
	defb 0ffh		;35d4	ff		.
	defb 0ffh		;35d5	ff		.
	defb 0ffh		;35d6	ff		.
	defb 0ffh		;35d7	ff		.
	defb 0ffh		;35d8	ff		.
	defb 0ffh		;35d9	ff		.
	defb 0ffh		;35da	ff		.
	defb 0ffh		;35db	ff		.
	defb 0ffh		;35dc	ff		.
	defb 0ffh		;35dd	ff		.
	defb 0ffh		;35de	ff		.
	defb 0ffh		;35df	ff		.
	defb 0ffh		;35e0	ff		.
	defb 0ffh		;35e1	ff		.
	defb 0ffh		;35e2	ff		.
	defb 0ffh		;35e3	ff		.
	defb 0ffh		;35e4	ff		.
	defb 0ffh		;35e5	ff		.
	defb 0ffh		;35e6	ff		.
	defb 0ffh		;35e7	ff		.
	defb 0ffh		;35e8	ff		.
	defb 0ffh		;35e9	ff		.
	defb 0ffh		;35ea	ff		.
	defb 0ffh		;35eb	ff		.
	defb 0ffh		;35ec	ff		.
	defb 0ffh		;35ed	ff		.
	defb 0ffh		;35ee	ff		.
	defb 0ffh		;35ef	ff		.
	defb 0ffh		;35f0	ff		.
	defb 0ffh		;35f1	ff		.
	defb 0ffh		;35f2	ff		.
	defb 0ffh		;35f3	ff		.
	defb 0ffh		;35f4	ff		.
	defb 0ffh		;35f5	ff		.
	defb 0ffh		;35f6	ff		.
	defb 0ffh		;35f7	ff		.
	defb 0ffh		;35f8	ff		.
	defb 0ffh		;35f9	ff		.
	defb 0ffh		;35fa	ff		.
	defb 0ffh		;35fb	ff		.
	defb 0ffh		;35fc	ff		.
	defb 0ffh		;35fd	ff		.
	defb 0ffh		;35fe	ff		.
	defb 0ffh		;35ff	ff		.
	defb 0ffh		;3600	ff		.
	defb 0ffh		;3601	ff		.
	defb 0ffh		;3602	ff		.
	defb 0ffh		;3603	ff		.
	defb 0ffh		;3604	ff		.
	defb 0ffh		;3605	ff		.
	defb 0ffh		;3606	ff		.
	defb 0ffh		;3607	ff		.
	defb 0ffh		;3608	ff		.
	defb 0ffh		;3609	ff		.
	defb 0ffh		;360a	ff		.
	defb 0ffh		;360b	ff		.
	defb 0ffh		;360c	ff		.
	defb 0ffh		;360d	ff		.
	defb 0ffh		;360e	ff		.
	defb 0ffh		;360f	ff		.
	defb 0ffh		;3610	ff		.
	defb 0ffh		;3611	ff		.
	defb 0ffh		;3612	ff		.
	defb 0ffh		;3613	ff		.
	defb 0ffh		;3614	ff		.
	defb 0ffh		;3615	ff		.
	defb 0ffh		;3616	ff		.
	defb 0ffh		;3617	ff		.
	defb 0ffh		;3618	ff		.
	defb 0ffh		;3619	ff		.
	defb 0ffh		;361a	ff		.
	defb 0ffh		;361b	ff		.
	defb 0ffh		;361c	ff		.
	defb 0ffh		;361d	ff		.
	defb 0ffh		;361e	ff		.
	defb 0ffh		;361f	ff		.
	defb 0ffh		;3620	ff		.
	defb 0ffh		;3621	ff		.
	defb 0ffh		;3622	ff		.
	defb 0ffh		;3623	ff		.
	defb 0ffh		;3624	ff		.
	defb 0ffh		;3625	ff		.
	defb 0ffh		;3626	ff		.
	defb 0ffh		;3627	ff		.
	defb 0ffh		;3628	ff		.
	defb 0ffh		;3629	ff		.
	defb 0ffh		;362a	ff		.
	defb 0ffh		;362b	ff		.
	defb 0ffh		;362c	ff		.
	defb 0ffh		;362d	ff		.
	defb 0ffh		;362e	ff		.
	defb 0ffh		;362f	ff		.
	defb 0ffh		;3630	ff		.
	defb 0ffh		;3631	ff		.
	defb 0ffh		;3632	ff		.
	defb 0ffh		;3633	ff		.
	defb 0ffh		;3634	ff		.
	defb 0ffh		;3635	ff		.
	defb 0ffh		;3636	ff		.
	defb 0ffh		;3637	ff		.
	defb 0ffh		;3638	ff		.
	defb 0ffh		;3639	ff		.
	defb 0ffh		;363a	ff		.
	defb 0ffh		;363b	ff		.
	defb 0ffh		;363c	ff		.
	defb 0ffh		;363d	ff		.
	defb 0ffh		;363e	ff		.
	defb 0ffh		;363f	ff		.
	defb 0ffh		;3640	ff		.
	defb 0ffh		;3641	ff		.
	defb 0ffh		;3642	ff		.
	defb 0ffh		;3643	ff		.
	defb 0ffh		;3644	ff		.
	defb 0ffh		;3645	ff		.
	defb 0ffh		;3646	ff		.
	defb 0ffh		;3647	ff		.
	defb 0ffh		;3648	ff		.
	defb 0ffh		;3649	ff		.
	defb 0ffh		;364a	ff		.
	defb 0ffh		;364b	ff		.
	defb 0ffh		;364c	ff		.
	defb 0ffh		;364d	ff		.
	defb 0ffh		;364e	ff		.
	defb 0ffh		;364f	ff		.
	defb 0ffh		;3650	ff		.
	defb 0ffh		;3651	ff		.
	defb 0ffh		;3652	ff		.
	defb 0ffh		;3653	ff		.
	defb 0ffh		;3654	ff		.
	defb 0ffh		;3655	ff		.
	defb 0ffh		;3656	ff		.
	defb 0ffh		;3657	ff		.
	defb 0ffh		;3658	ff		.
	defb 0ffh		;3659	ff		.
	defb 0ffh		;365a	ff		.
	defb 0ffh		;365b	ff		.
	defb 0ffh		;365c	ff		.
	defb 0ffh		;365d	ff		.
	defb 0ffh		;365e	ff		.
	defb 0ffh		;365f	ff		.
	defb 0ffh		;3660	ff		.
	defb 0ffh		;3661	ff		.
	defb 0ffh		;3662	ff		.
	defb 0ffh		;3663	ff		.
	defb 0ffh		;3664	ff		.
	defb 0ffh		;3665	ff		.
	defb 0ffh		;3666	ff		.
	defb 0ffh		;3667	ff		.
	defb 0ffh		;3668	ff		.
	defb 0ffh		;3669	ff		.
	defb 0ffh		;366a	ff		.
	defb 0ffh		;366b	ff		.
	defb 0ffh		;366c	ff		.
	defb 0ffh		;366d	ff		.
	defb 0ffh		;366e	ff		.
	defb 0ffh		;366f	ff		.
	defb 0ffh		;3670	ff		.
	defb 0ffh		;3671	ff		.
	defb 0ffh		;3672	ff		.
	defb 0ffh		;3673	ff		.
	defb 0ffh		;3674	ff		.
	defb 0ffh		;3675	ff		.
	defb 0ffh		;3676	ff		.
	defb 0ffh		;3677	ff		.
	defb 0ffh		;3678	ff		.
	defb 0ffh		;3679	ff		.
	defb 0ffh		;367a	ff		.
	defb 0ffh		;367b	ff		.
	defb 0ffh		;367c	ff		.
	defb 0ffh		;367d	ff		.
	defb 0ffh		;367e	ff		.
	defb 0ffh		;367f	ff		.
	defb 0ffh		;3680	ff		.
	defb 0ffh		;3681	ff		.
	defb 0ffh		;3682	ff		.
	defb 0ffh		;3683	ff		.
	defb 0ffh		;3684	ff		.
	defb 0ffh		;3685	ff		.
	defb 0ffh		;3686	ff		.
	defb 0ffh		;3687	ff		.
	defb 0ffh		;3688	ff		.
	defb 0ffh		;3689	ff		.
	defb 0ffh		;368a	ff		.
	defb 0ffh		;368b	ff		.
	defb 0ffh		;368c	ff		.
	defb 0ffh		;368d	ff		.
	defb 0ffh		;368e	ff		.
	defb 0ffh		;368f	ff		.
	defb 0ffh		;3690	ff		.
	defb 0ffh		;3691	ff		.
	defb 0ffh		;3692	ff		.
	defb 0ffh		;3693	ff		.
	defb 0ffh		;3694	ff		.
	defb 0ffh		;3695	ff		.
	defb 0ffh		;3696	ff		.
	defb 0ffh		;3697	ff		.
	defb 0ffh		;3698	ff		.
	defb 0ffh		;3699	ff		.
	defb 0ffh		;369a	ff		.
	defb 0ffh		;369b	ff		.
	defb 0ffh		;369c	ff		.
	defb 0ffh		;369d	ff		.
	defb 0ffh		;369e	ff		.
	defb 0ffh		;369f	ff		.
	defb 0ffh		;36a0	ff		.
	defb 0ffh		;36a1	ff		.
	defb 0ffh		;36a2	ff		.
	defb 0ffh		;36a3	ff		.
	defb 0ffh		;36a4	ff		.
	defb 0ffh		;36a5	ff		.
	defb 0ffh		;36a6	ff		.
	defb 0ffh		;36a7	ff		.
	defb 0ffh		;36a8	ff		.
	defb 0ffh		;36a9	ff		.
	defb 0ffh		;36aa	ff		.
	defb 0ffh		;36ab	ff		.
	defb 0ffh		;36ac	ff		.
	defb 0ffh		;36ad	ff		.
	defb 0ffh		;36ae	ff		.
	defb 0ffh		;36af	ff		.
	defb 0ffh		;36b0	ff		.
	defb 0ffh		;36b1	ff		.
	defb 0ffh		;36b2	ff		.
	defb 0ffh		;36b3	ff		.
	defb 0ffh		;36b4	ff		.
	defb 0ffh		;36b5	ff		.
	defb 0ffh		;36b6	ff		.
	defb 0ffh		;36b7	ff		.
	defb 0ffh		;36b8	ff		.
	defb 0ffh		;36b9	ff		.
	defb 0ffh		;36ba	ff		.
	defb 0ffh		;36bb	ff		.
	defb 0ffh		;36bc	ff		.
	defb 0ffh		;36bd	ff		.
	defb 0ffh		;36be	ff		.
	defb 0ffh		;36bf	ff		.
	defb 0ffh		;36c0	ff		.
	defb 0ffh		;36c1	ff		.
	defb 0ffh		;36c2	ff		.
	defb 0ffh		;36c3	ff		.
	defb 0ffh		;36c4	ff		.
	defb 0ffh		;36c5	ff		.
	defb 0ffh		;36c6	ff		.
	defb 0ffh		;36c7	ff		.
	defb 0ffh		;36c8	ff		.
	defb 0ffh		;36c9	ff		.
	defb 0ffh		;36ca	ff		.
	defb 0ffh		;36cb	ff		.
	defb 0ffh		;36cc	ff		.
	defb 0ffh		;36cd	ff		.
	defb 0ffh		;36ce	ff		.
	defb 0ffh		;36cf	ff		.
	defb 0ffh		;36d0	ff		.
	defb 0ffh		;36d1	ff		.
	defb 0ffh		;36d2	ff		.
	defb 0ffh		;36d3	ff		.
	defb 0ffh		;36d4	ff		.
	defb 0ffh		;36d5	ff		.
	defb 0ffh		;36d6	ff		.
	defb 0ffh		;36d7	ff		.
	defb 0ffh		;36d8	ff		.
	defb 0ffh		;36d9	ff		.
	defb 0ffh		;36da	ff		.
	defb 0ffh		;36db	ff		.
	defb 0ffh		;36dc	ff		.
	defb 0ffh		;36dd	ff		.
	defb 0ffh		;36de	ff		.
	defb 0ffh		;36df	ff		.
	defb 0ffh		;36e0	ff		.
	defb 0ffh		;36e1	ff		.
	defb 0ffh		;36e2	ff		.
	defb 0ffh		;36e3	ff		.
	defb 0ffh		;36e4	ff		.
	defb 0ffh		;36e5	ff		.
	defb 0ffh		;36e6	ff		.
	defb 0ffh		;36e7	ff		.
	defb 0ffh		;36e8	ff		.
	defb 0ffh		;36e9	ff		.
	defb 0ffh		;36ea	ff		.
	defb 0ffh		;36eb	ff		.
	defb 0ffh		;36ec	ff		.
	defb 0ffh		;36ed	ff		.
	defb 0ffh		;36ee	ff		.
	defb 0ffh		;36ef	ff		.
	defb 0ffh		;36f0	ff		.
	defb 0ffh		;36f1	ff		.
	defb 0ffh		;36f2	ff		.
	defb 0ffh		;36f3	ff		.
	defb 0ffh		;36f4	ff		.
	defb 0ffh		;36f5	ff		.
	defb 0ffh		;36f6	ff		.
	defb 0ffh		;36f7	ff		.
	defb 0ffh		;36f8	ff		.
	defb 0ffh		;36f9	ff		.
	defb 0ffh		;36fa	ff		.
	defb 0ffh		;36fb	ff		.
	defb 0ffh		;36fc	ff		.
	defb 0ffh		;36fd	ff		.
	defb 0ffh		;36fe	ff		.
	defb 0ffh		;36ff	ff		.
	defb 0ffh		;3700	ff		.
	defb 0ffh		;3701	ff		.
	defb 0ffh		;3702	ff		.
	defb 0ffh		;3703	ff		.
	defb 0ffh		;3704	ff		.
	defb 0ffh		;3705	ff		.
	defb 0ffh		;3706	ff		.
	defb 0ffh		;3707	ff		.
	defb 0ffh		;3708	ff		.
	defb 0ffh		;3709	ff		.
	defb 0ffh		;370a	ff		.
	defb 0ffh		;370b	ff		.
	defb 0ffh		;370c	ff		.
	defb 0ffh		;370d	ff		.
	defb 0ffh		;370e	ff		.
	defb 0ffh		;370f	ff		.
	defb 0ffh		;3710	ff		.
	defb 0ffh		;3711	ff		.
	defb 0ffh		;3712	ff		.
	defb 0ffh		;3713	ff		.
	defb 0ffh		;3714	ff		.
	defb 0ffh		;3715	ff		.
	defb 0ffh		;3716	ff		.
	defb 0ffh		;3717	ff		.
	defb 0ffh		;3718	ff		.
	defb 0ffh		;3719	ff		.
	defb 0ffh		;371a	ff		.
	defb 0ffh		;371b	ff		.
	defb 0ffh		;371c	ff		.
	defb 0ffh		;371d	ff		.
	defb 0ffh		;371e	ff		.
	defb 0ffh		;371f	ff		.
	defb 0ffh		;3720	ff		.
	defb 0ffh		;3721	ff		.
	defb 0ffh		;3722	ff		.
	defb 0ffh		;3723	ff		.
	defb 0ffh		;3724	ff		.
	defb 0ffh		;3725	ff		.
	defb 0ffh		;3726	ff		.
	defb 0ffh		;3727	ff		.
	defb 0ffh		;3728	ff		.
	defb 0ffh		;3729	ff		.
	defb 0ffh		;372a	ff		.
	defb 0ffh		;372b	ff		.
	defb 0ffh		;372c	ff		.
	defb 0ffh		;372d	ff		.
	defb 0ffh		;372e	ff		.
	defb 0ffh		;372f	ff		.
	defb 0ffh		;3730	ff		.
	defb 0ffh		;3731	ff		.
	defb 0ffh		;3732	ff		.
	defb 0ffh		;3733	ff		.
	defb 0ffh		;3734	ff		.
	defb 0ffh		;3735	ff		.
	defb 0ffh		;3736	ff		.
	defb 0ffh		;3737	ff		.
	defb 0ffh		;3738	ff		.
	defb 0ffh		;3739	ff		.
	defb 0ffh		;373a	ff		.
	defb 0ffh		;373b	ff		.
	defb 0ffh		;373c	ff		.
	defb 0ffh		;373d	ff		.
	defb 0ffh		;373e	ff		.
	defb 0ffh		;373f	ff		.
	defb 0ffh		;3740	ff		.
	defb 0ffh		;3741	ff		.
	defb 0ffh		;3742	ff		.
	defb 0ffh		;3743	ff		.
	defb 0ffh		;3744	ff		.
	defb 0ffh		;3745	ff		.
	defb 0ffh		;3746	ff		.
	defb 0ffh		;3747	ff		.
	defb 0ffh		;3748	ff		.
	defb 0ffh		;3749	ff		.
	defb 0ffh		;374a	ff		.
	defb 0ffh		;374b	ff		.
	defb 0ffh		;374c	ff		.
	defb 0ffh		;374d	ff		.
	defb 0ffh		;374e	ff		.
	defb 0ffh		;374f	ff		.
	defb 0ffh		;3750	ff		.
	defb 0ffh		;3751	ff		.
	defb 0ffh		;3752	ff		.
	defb 0ffh		;3753	ff		.
	defb 0ffh		;3754	ff		.
	defb 0ffh		;3755	ff		.
	defb 0ffh		;3756	ff		.
	defb 0ffh		;3757	ff		.
	defb 0ffh		;3758	ff		.
	defb 0ffh		;3759	ff		.
	defb 0ffh		;375a	ff		.
	defb 0ffh		;375b	ff		.
	defb 0ffh		;375c	ff		.
	defb 0ffh		;375d	ff		.
	defb 0ffh		;375e	ff		.
	defb 0ffh		;375f	ff		.
	defb 0ffh		;3760	ff		.
	defb 0ffh		;3761	ff		.
	defb 0ffh		;3762	ff		.
	defb 0ffh		;3763	ff		.
	defb 0ffh		;3764	ff		.
	defb 0ffh		;3765	ff		.
	defb 0ffh		;3766	ff		.
	defb 0ffh		;3767	ff		.
	defb 0ffh		;3768	ff		.
	defb 0ffh		;3769	ff		.
	defb 0ffh		;376a	ff		.
	defb 0ffh		;376b	ff		.
	defb 0ffh		;376c	ff		.
	defb 0ffh		;376d	ff		.
	defb 0ffh		;376e	ff		.
	defb 0ffh		;376f	ff		.
	defb 0ffh		;3770	ff		.
	defb 0ffh		;3771	ff		.
	defb 0ffh		;3772	ff		.
	defb 0ffh		;3773	ff		.
	defb 0ffh		;3774	ff		.
	defb 0ffh		;3775	ff		.
	defb 0ffh		;3776	ff		.
	defb 0ffh		;3777	ff		.
	defb 0ffh		;3778	ff		.
	defb 0ffh		;3779	ff		.
	defb 0ffh		;377a	ff		.
	defb 0ffh		;377b	ff		.
	defb 0ffh		;377c	ff		.
	defb 0ffh		;377d	ff		.
	defb 0ffh		;377e	ff		.
	defb 0ffh		;377f	ff		.
	defb 0ffh		;3780	ff		.
	defb 0ffh		;3781	ff		.
	defb 0ffh		;3782	ff		.
	defb 0ffh		;3783	ff		.
	defb 0ffh		;3784	ff		.
	defb 0ffh		;3785	ff		.
	defb 0ffh		;3786	ff		.
	defb 0ffh		;3787	ff		.
	defb 0ffh		;3788	ff		.
	defb 0ffh		;3789	ff		.
	defb 0ffh		;378a	ff		.
	defb 0ffh		;378b	ff		.
	defb 0ffh		;378c	ff		.
	defb 0ffh		;378d	ff		.
	defb 0ffh		;378e	ff		.
	defb 0ffh		;378f	ff		.
	defb 0ffh		;3790	ff		.
	defb 0ffh		;3791	ff		.
	defb 0ffh		;3792	ff		.
	defb 0ffh		;3793	ff		.
	defb 0ffh		;3794	ff		.
	defb 0ffh		;3795	ff		.
	defb 0ffh		;3796	ff		.
	defb 0ffh		;3797	ff		.
	defb 0ffh		;3798	ff		.
	defb 0ffh		;3799	ff		.
	defb 0ffh		;379a	ff		.
	defb 0ffh		;379b	ff		.
	defb 0ffh		;379c	ff		.
	defb 0ffh		;379d	ff		.
	defb 0ffh		;379e	ff		.
	defb 0ffh		;379f	ff		.
	defb 0ffh		;37a0	ff		.
	defb 0ffh		;37a1	ff		.
	defb 0ffh		;37a2	ff		.
	defb 0ffh		;37a3	ff		.
	defb 0ffh		;37a4	ff		.
	defb 0ffh		;37a5	ff		.
	defb 0ffh		;37a6	ff		.
	defb 0ffh		;37a7	ff		.
	defb 0ffh		;37a8	ff		.
	defb 0ffh		;37a9	ff		.
	defb 0ffh		;37aa	ff		.
	defb 0ffh		;37ab	ff		.
	defb 0ffh		;37ac	ff		.
	defb 0ffh		;37ad	ff		.
	defb 0ffh		;37ae	ff		.
	defb 0ffh		;37af	ff		.
	defb 0ffh		;37b0	ff		.
	defb 0ffh		;37b1	ff		.
	defb 0ffh		;37b2	ff		.
	defb 0ffh		;37b3	ff		.
	defb 0ffh		;37b4	ff		.
	defb 0ffh		;37b5	ff		.
	defb 0ffh		;37b6	ff		.
	defb 0ffh		;37b7	ff		.
	defb 0ffh		;37b8	ff		.
	defb 0ffh		;37b9	ff		.
	defb 0ffh		;37ba	ff		.
	defb 0ffh		;37bb	ff		.
	defb 0ffh		;37bc	ff		.
	defb 0ffh		;37bd	ff		.
	defb 0ffh		;37be	ff		.
	defb 0ffh		;37bf	ff		.
	defb 0ffh		;37c0	ff		.
	defb 0ffh		;37c1	ff		.
	defb 0ffh		;37c2	ff		.
	defb 0ffh		;37c3	ff		.
	defb 0ffh		;37c4	ff		.
	defb 0ffh		;37c5	ff		.
	defb 0ffh		;37c6	ff		.
	defb 0ffh		;37c7	ff		.
	defb 0ffh		;37c8	ff		.
	defb 0ffh		;37c9	ff		.
	defb 0ffh		;37ca	ff		.
	defb 0ffh		;37cb	ff		.
	defb 0ffh		;37cc	ff		.
	defb 0ffh		;37cd	ff		.
	defb 0ffh		;37ce	ff		.
	defb 0ffh		;37cf	ff		.
	defb 0ffh		;37d0	ff		.
	defb 0ffh		;37d1	ff		.
	defb 0ffh		;37d2	ff		.
	defb 0ffh		;37d3	ff		.
	defb 0ffh		;37d4	ff		.
	defb 0ffh		;37d5	ff		.
	defb 0ffh		;37d6	ff		.
	defb 0ffh		;37d7	ff		.
	defb 0ffh		;37d8	ff		.
	defb 0ffh		;37d9	ff		.
	defb 0ffh		;37da	ff		.
	defb 0ffh		;37db	ff		.
	defb 0ffh		;37dc	ff		.
	defb 0ffh		;37dd	ff		.
	defb 0ffh		;37de	ff		.
	defb 0ffh		;37df	ff		.
	defb 0ffh		;37e0	ff		.
	defb 0ffh		;37e1	ff		.
	defb 0ffh		;37e2	ff		.
	defb 0ffh		;37e3	ff		.
	defb 0ffh		;37e4	ff		.
	defb 0ffh		;37e5	ff		.
	defb 0ffh		;37e6	ff		.
	defb 0ffh		;37e7	ff		.
	defb 0ffh		;37e8	ff		.
	defb 0ffh		;37e9	ff		.
	defb 0ffh		;37ea	ff		.
	defb 0ffh		;37eb	ff		.
	defb 0ffh		;37ec	ff		.
	defb 0ffh		;37ed	ff		.
	defb 0ffh		;37ee	ff		.
	defb 0ffh		;37ef	ff		.
	defb 0ffh		;37f0	ff		.
	defb 0ffh		;37f1	ff		.
	defb 0ffh		;37f2	ff		.
	defb 0ffh		;37f3	ff		.
	defb 0ffh		;37f4	ff		.
	defb 0ffh		;37f5	ff		.
	defb 0ffh		;37f6	ff		.
	defb 0ffh		;37f7	ff		.
	defb 0ffh		;37f8	ff		.
	defb 0ffh		;37f9	ff		.
	defb 0ffh		;37fa	ff		.
	defb 0ffh		;37fb	ff		.
	defb 0ffh		;37fc	ff		.
	defb 0ffh		;37fd	ff		.
	defb 0ffh		;37fe	ff		.
	defb 0ffh		;37ff	ff		.
	defb 0ffh		;3800	ff		.
	defb 0ffh		;3801	ff		.
	defb 0ffh		;3802	ff		.
	defb 0ffh		;3803	ff		.
	defb 0ffh		;3804	ff		.
	defb 0ffh		;3805	ff		.
	defb 0ffh		;3806	ff		.
	defb 0ffh		;3807	ff		.
	defb 0ffh		;3808	ff		.
	defb 0ffh		;3809	ff		.
	defb 0ffh		;380a	ff		.
	defb 0ffh		;380b	ff		.
	defb 0ffh		;380c	ff		.
	defb 0ffh		;380d	ff		.
	defb 0ffh		;380e	ff		.
	defb 0ffh		;380f	ff		.
	defb 0ffh		;3810	ff		.
	defb 0ffh		;3811	ff		.
	defb 0ffh		;3812	ff		.
	defb 0ffh		;3813	ff		.
	defb 0ffh		;3814	ff		.
	defb 0ffh		;3815	ff		.
	defb 0ffh		;3816	ff		.
	defb 0ffh		;3817	ff		.
	defb 0ffh		;3818	ff		.
	defb 0ffh		;3819	ff		.
	defb 0ffh		;381a	ff		.
	defb 0ffh		;381b	ff		.
	defb 0ffh		;381c	ff		.
	defb 0ffh		;381d	ff		.
	defb 0ffh		;381e	ff		.
	defb 0ffh		;381f	ff		.
	defb 0ffh		;3820	ff		.
	defb 0ffh		;3821	ff		.
	defb 0ffh		;3822	ff		.
	defb 0ffh		;3823	ff		.
	defb 0ffh		;3824	ff		.
	defb 0ffh		;3825	ff		.
	defb 0ffh		;3826	ff		.
	defb 0ffh		;3827	ff		.
	defb 0ffh		;3828	ff		.
	defb 0ffh		;3829	ff		.
	defb 0ffh		;382a	ff		.
	defb 0ffh		;382b	ff		.
	defb 0ffh		;382c	ff		.
	defb 0ffh		;382d	ff		.
	defb 0ffh		;382e	ff		.
	defb 0ffh		;382f	ff		.
	defb 0ffh		;3830	ff		.
	defb 0ffh		;3831	ff		.
	defb 0ffh		;3832	ff		.
	defb 0ffh		;3833	ff		.
	defb 0ffh		;3834	ff		.
	defb 0ffh		;3835	ff		.
	defb 0ffh		;3836	ff		.
	defb 0ffh		;3837	ff		.
	defb 0ffh		;3838	ff		.
	defb 0ffh		;3839	ff		.
	defb 0ffh		;383a	ff		.
	defb 0ffh		;383b	ff		.
	defb 0ffh		;383c	ff		.
	defb 0ffh		;383d	ff		.
	defb 0ffh		;383e	ff		.
	defb 0ffh		;383f	ff		.
	defb 0ffh		;3840	ff		.
	defb 0ffh		;3841	ff		.
	defb 0ffh		;3842	ff		.
	defb 0ffh		;3843	ff		.
	defb 0ffh		;3844	ff		.
	defb 0ffh		;3845	ff		.
	defb 0ffh		;3846	ff		.
	defb 0ffh		;3847	ff		.
	defb 0ffh		;3848	ff		.
	defb 0ffh		;3849	ff		.
	defb 0ffh		;384a	ff		.
	defb 0ffh		;384b	ff		.
	defb 0ffh		;384c	ff		.
	defb 0ffh		;384d	ff		.
	defb 0ffh		;384e	ff		.
	defb 0ffh		;384f	ff		.
	defb 0ffh		;3850	ff		.
	defb 0ffh		;3851	ff		.
	defb 0ffh		;3852	ff		.
	defb 0ffh		;3853	ff		.
	defb 0ffh		;3854	ff		.
	defb 0ffh		;3855	ff		.
	defb 0ffh		;3856	ff		.
	defb 0ffh		;3857	ff		.
	defb 0ffh		;3858	ff		.
	defb 0ffh		;3859	ff		.
	defb 0ffh		;385a	ff		.
	defb 0ffh		;385b	ff		.
	defb 0ffh		;385c	ff		.
	defb 0ffh		;385d	ff		.
	defb 0ffh		;385e	ff		.
	defb 0ffh		;385f	ff		.
	defb 0ffh		;3860	ff		.
	defb 0ffh		;3861	ff		.
	defb 0ffh		;3862	ff		.
	defb 0ffh		;3863	ff		.
	defb 0ffh		;3864	ff		.
	defb 0ffh		;3865	ff		.
	defb 0ffh		;3866	ff		.
	defb 0ffh		;3867	ff		.
	defb 0ffh		;3868	ff		.
	defb 0ffh		;3869	ff		.
	defb 0ffh		;386a	ff		.
	defb 0ffh		;386b	ff		.
	defb 0ffh		;386c	ff		.
	defb 0ffh		;386d	ff		.
	defb 0ffh		;386e	ff		.
	defb 0ffh		;386f	ff		.
	defb 0ffh		;3870	ff		.
	defb 0ffh		;3871	ff		.
	defb 0ffh		;3872	ff		.
	defb 0ffh		;3873	ff		.
	defb 0ffh		;3874	ff		.
	defb 0ffh		;3875	ff		.
	defb 0ffh		;3876	ff		.
	defb 0ffh		;3877	ff		.
	defb 0ffh		;3878	ff		.
	defb 0ffh		;3879	ff		.
	defb 0ffh		;387a	ff		.
	defb 0ffh		;387b	ff		.
	defb 0ffh		;387c	ff		.
	defb 0ffh		;387d	ff		.
	defb 0ffh		;387e	ff		.
	defb 0ffh		;387f	ff		.
	defb 0ffh		;3880	ff		.
	defb 0ffh		;3881	ff		.
	defb 0ffh		;3882	ff		.
	defb 0ffh		;3883	ff		.
	defb 0ffh		;3884	ff		.
	defb 0ffh		;3885	ff		.
	defb 0ffh		;3886	ff		.
	defb 0ffh		;3887	ff		.
	defb 0ffh		;3888	ff		.
	defb 0ffh		;3889	ff		.
	defb 0ffh		;388a	ff		.
	defb 0ffh		;388b	ff		.
	defb 0ffh		;388c	ff		.
	defb 0ffh		;388d	ff		.
	defb 0ffh		;388e	ff		.
	defb 0ffh		;388f	ff		.
	defb 0ffh		;3890	ff		.
	defb 0ffh		;3891	ff		.
	defb 0ffh		;3892	ff		.
	defb 0ffh		;3893	ff		.
	defb 0ffh		;3894	ff		.
	defb 0ffh		;3895	ff		.
	defb 0ffh		;3896	ff		.
	defb 0ffh		;3897	ff		.
	defb 0ffh		;3898	ff		.
	defb 0ffh		;3899	ff		.
	defb 0ffh		;389a	ff		.
	defb 0ffh		;389b	ff		.
	defb 0ffh		;389c	ff		.
	defb 0ffh		;389d	ff		.
	defb 0ffh		;389e	ff		.
	defb 0ffh		;389f	ff		.
	defb 0ffh		;38a0	ff		.
	defb 0ffh		;38a1	ff		.
	defb 0ffh		;38a2	ff		.
	defb 0ffh		;38a3	ff		.
	defb 0ffh		;38a4	ff		.
	defb 0ffh		;38a5	ff		.
	defb 0ffh		;38a6	ff		.
	defb 0ffh		;38a7	ff		.
	defb 0ffh		;38a8	ff		.
	defb 0ffh		;38a9	ff		.
	defb 0ffh		;38aa	ff		.
	defb 0ffh		;38ab	ff		.
	defb 0ffh		;38ac	ff		.
	defb 0ffh		;38ad	ff		.
	defb 0ffh		;38ae	ff		.
	defb 0ffh		;38af	ff		.
	defb 0ffh		;38b0	ff		.
	defb 0ffh		;38b1	ff		.
	defb 0ffh		;38b2	ff		.
	defb 0ffh		;38b3	ff		.
	defb 0ffh		;38b4	ff		.
	defb 0ffh		;38b5	ff		.
	defb 0ffh		;38b6	ff		.
	defb 0ffh		;38b7	ff		.
	defb 0ffh		;38b8	ff		.
	defb 0ffh		;38b9	ff		.
	defb 0ffh		;38ba	ff		.
	defb 0ffh		;38bb	ff		.
	defb 0ffh		;38bc	ff		.
	defb 0ffh		;38bd	ff		.
	defb 0ffh		;38be	ff		.
	defb 0ffh		;38bf	ff		.
	defb 0ffh		;38c0	ff		.
	defb 0ffh		;38c1	ff		.
	defb 0ffh		;38c2	ff		.
	defb 0ffh		;38c3	ff		.
	defb 0ffh		;38c4	ff		.
	defb 0ffh		;38c5	ff		.
	defb 0ffh		;38c6	ff		.
	defb 0ffh		;38c7	ff		.
	defb 0ffh		;38c8	ff		.
	defb 0ffh		;38c9	ff		.
	defb 0ffh		;38ca	ff		.
	defb 0ffh		;38cb	ff		.
	defb 0ffh		;38cc	ff		.
	defb 0ffh		;38cd	ff		.
	defb 0ffh		;38ce	ff		.
	defb 0ffh		;38cf	ff		.
	defb 0ffh		;38d0	ff		.
	defb 0ffh		;38d1	ff		.
	defb 0ffh		;38d2	ff		.
	defb 0ffh		;38d3	ff		.
	defb 0ffh		;38d4	ff		.
	defb 0ffh		;38d5	ff		.
	defb 0ffh		;38d6	ff		.
	defb 0ffh		;38d7	ff		.
	defb 0ffh		;38d8	ff		.
	defb 0ffh		;38d9	ff		.
	defb 0ffh		;38da	ff		.
	defb 0ffh		;38db	ff		.
	defb 0ffh		;38dc	ff		.
	defb 0ffh		;38dd	ff		.
	defb 0ffh		;38de	ff		.
	defb 0ffh		;38df	ff		.
	defb 0ffh		;38e0	ff		.
	defb 0ffh		;38e1	ff		.
	defb 0ffh		;38e2	ff		.
	defb 0ffh		;38e3	ff		.
	defb 0ffh		;38e4	ff		.
	defb 0ffh		;38e5	ff		.
	defb 0ffh		;38e6	ff		.
	defb 0ffh		;38e7	ff		.
	defb 0ffh		;38e8	ff		.
	defb 0ffh		;38e9	ff		.
	defb 0ffh		;38ea	ff		.
	defb 0ffh		;38eb	ff		.
	defb 0ffh		;38ec	ff		.
	defb 0ffh		;38ed	ff		.
	defb 0ffh		;38ee	ff		.
	defb 0ffh		;38ef	ff		.
	defb 0ffh		;38f0	ff		.
	defb 0ffh		;38f1	ff		.
	defb 0ffh		;38f2	ff		.
	defb 0ffh		;38f3	ff		.
	defb 0ffh		;38f4	ff		.
	defb 0ffh		;38f5	ff		.
	defb 0ffh		;38f6	ff		.
	defb 0ffh		;38f7	ff		.
	defb 0ffh		;38f8	ff		.
	defb 0ffh		;38f9	ff		.
	defb 0ffh		;38fa	ff		.
	defb 0ffh		;38fb	ff		.
	defb 0ffh		;38fc	ff		.
	defb 0ffh		;38fd	ff		.
	defb 0ffh		;38fe	ff		.
	defb 0ffh		;38ff	ff		.
	defb 0ffh		;3900	ff		.
	defb 0ffh		;3901	ff		.
	defb 0ffh		;3902	ff		.
	defb 0ffh		;3903	ff		.
	defb 0ffh		;3904	ff		.
	defb 0ffh		;3905	ff		.
	defb 0ffh		;3906	ff		.
	defb 0ffh		;3907	ff		.
	defb 0ffh		;3908	ff		.
	defb 0ffh		;3909	ff		.
	defb 0ffh		;390a	ff		.
	defb 0ffh		;390b	ff		.
	defb 0ffh		;390c	ff		.
	defb 0ffh		;390d	ff		.
	defb 0ffh		;390e	ff		.
	defb 0ffh		;390f	ff		.
	defb 0ffh		;3910	ff		.
	defb 0ffh		;3911	ff		.
	defb 0ffh		;3912	ff		.
	defb 0ffh		;3913	ff		.
	defb 0ffh		;3914	ff		.
	defb 0ffh		;3915	ff		.
	defb 0ffh		;3916	ff		.
	defb 0ffh		;3917	ff		.
	defb 0ffh		;3918	ff		.
	defb 0ffh		;3919	ff		.
	defb 0ffh		;391a	ff		.
	defb 0ffh		;391b	ff		.
	defb 0ffh		;391c	ff		.
	defb 0ffh		;391d	ff		.
	defb 0ffh		;391e	ff		.
	defb 0ffh		;391f	ff		.
	defb 0ffh		;3920	ff		.
	defb 0ffh		;3921	ff		.
	defb 0ffh		;3922	ff		.
	defb 0ffh		;3923	ff		.
	defb 0ffh		;3924	ff		.
	defb 0ffh		;3925	ff		.
	defb 0ffh		;3926	ff		.
	defb 0ffh		;3927	ff		.
	defb 0ffh		;3928	ff		.
	defb 0ffh		;3929	ff		.
	defb 0ffh		;392a	ff		.
	defb 0ffh		;392b	ff		.
	defb 0ffh		;392c	ff		.
	defb 0ffh		;392d	ff		.
	defb 0ffh		;392e	ff		.
	defb 0ffh		;392f	ff		.
	defb 0ffh		;3930	ff		.
	defb 0ffh		;3931	ff		.
	defb 0ffh		;3932	ff		.
	defb 0ffh		;3933	ff		.
	defb 0ffh		;3934	ff		.
	defb 0ffh		;3935	ff		.
	defb 0ffh		;3936	ff		.
	defb 0ffh		;3937	ff		.
	defb 0ffh		;3938	ff		.
	defb 0ffh		;3939	ff		.
	defb 0ffh		;393a	ff		.
	defb 0ffh		;393b	ff		.
	defb 0ffh		;393c	ff		.
	defb 0ffh		;393d	ff		.
	defb 0ffh		;393e	ff		.
	defb 0ffh		;393f	ff		.
	defb 0ffh		;3940	ff		.
	defb 0ffh		;3941	ff		.
	defb 0ffh		;3942	ff		.
	defb 0ffh		;3943	ff		.
	defb 0ffh		;3944	ff		.
	defb 0ffh		;3945	ff		.
	defb 0ffh		;3946	ff		.
	defb 0ffh		;3947	ff		.
	defb 0ffh		;3948	ff		.
	defb 0ffh		;3949	ff		.
	defb 0ffh		;394a	ff		.
	defb 0ffh		;394b	ff		.
	defb 0ffh		;394c	ff		.
	defb 0ffh		;394d	ff		.
	defb 0ffh		;394e	ff		.
	defb 0ffh		;394f	ff		.
	defb 0ffh		;3950	ff		.
	defb 0ffh		;3951	ff		.
	defb 0ffh		;3952	ff		.
	defb 0ffh		;3953	ff		.
	defb 0ffh		;3954	ff		.
	defb 0ffh		;3955	ff		.
	defb 0ffh		;3956	ff		.
	defb 0ffh		;3957	ff		.
	defb 0ffh		;3958	ff		.
	defb 0ffh		;3959	ff		.
	defb 0ffh		;395a	ff		.
	defb 0ffh		;395b	ff		.
	defb 0ffh		;395c	ff		.
	defb 0ffh		;395d	ff		.
	defb 0ffh		;395e	ff		.
	defb 0ffh		;395f	ff		.
	defb 0ffh		;3960	ff		.
	defb 0ffh		;3961	ff		.
	defb 0ffh		;3962	ff		.
	defb 0ffh		;3963	ff		.
	defb 0ffh		;3964	ff		.
	defb 0ffh		;3965	ff		.
	defb 0ffh		;3966	ff		.
	defb 0ffh		;3967	ff		.
	defb 0ffh		;3968	ff		.
	defb 0ffh		;3969	ff		.
	defb 0ffh		;396a	ff		.
	defb 0ffh		;396b	ff		.
	defb 0ffh		;396c	ff		.
	defb 0ffh		;396d	ff		.
	defb 0ffh		;396e	ff		.
	defb 0ffh		;396f	ff		.
	defb 0ffh		;3970	ff		.
	defb 0ffh		;3971	ff		.
	defb 0ffh		;3972	ff		.
	defb 0ffh		;3973	ff		.
	defb 0ffh		;3974	ff		.
	defb 0ffh		;3975	ff		.
	defb 0ffh		;3976	ff		.
	defb 0ffh		;3977	ff		.
	defb 0ffh		;3978	ff		.
	defb 0ffh		;3979	ff		.
	defb 0ffh		;397a	ff		.
	defb 0ffh		;397b	ff		.
	defb 0ffh		;397c	ff		.
	defb 0ffh		;397d	ff		.
	defb 0ffh		;397e	ff		.
	defb 0ffh		;397f	ff		.
	defb 0ffh		;3980	ff		.
	defb 0ffh		;3981	ff		.
	defb 0ffh		;3982	ff		.
	defb 0ffh		;3983	ff		.
	defb 0ffh		;3984	ff		.
	defb 0ffh		;3985	ff		.
	defb 0ffh		;3986	ff		.
	defb 0ffh		;3987	ff		.
	defb 0ffh		;3988	ff		.
	defb 0ffh		;3989	ff		.
	defb 0ffh		;398a	ff		.
	defb 0ffh		;398b	ff		.
	defb 0ffh		;398c	ff		.
	defb 0ffh		;398d	ff		.
	defb 0ffh		;398e	ff		.
	defb 0ffh		;398f	ff		.
	defb 0ffh		;3990	ff		.
	defb 0ffh		;3991	ff		.
	defb 0ffh		;3992	ff		.
	defb 0ffh		;3993	ff		.
	defb 0ffh		;3994	ff		.
	defb 0ffh		;3995	ff		.
	defb 0ffh		;3996	ff		.
	defb 0ffh		;3997	ff		.
	defb 0ffh		;3998	ff		.
	defb 0ffh		;3999	ff		.
	defb 0ffh		;399a	ff		.
	defb 0ffh		;399b	ff		.
	defb 0ffh		;399c	ff		.
	defb 0ffh		;399d	ff		.
	defb 0ffh		;399e	ff		.
	defb 0ffh		;399f	ff		.
	defb 0ffh		;39a0	ff		.
	defb 0ffh		;39a1	ff		.
	defb 0ffh		;39a2	ff		.
	defb 0ffh		;39a3	ff		.
	defb 0ffh		;39a4	ff		.
	defb 0ffh		;39a5	ff		.
	defb 0ffh		;39a6	ff		.
	defb 0ffh		;39a7	ff		.
	defb 0ffh		;39a8	ff		.
	defb 0ffh		;39a9	ff		.
	defb 0ffh		;39aa	ff		.
	defb 0ffh		;39ab	ff		.
	defb 0ffh		;39ac	ff		.
	defb 0ffh		;39ad	ff		.
	defb 0ffh		;39ae	ff		.
	defb 0ffh		;39af	ff		.
	defb 0ffh		;39b0	ff		.
	defb 0ffh		;39b1	ff		.
	defb 0ffh		;39b2	ff		.
	defb 0ffh		;39b3	ff		.
	defb 0ffh		;39b4	ff		.
	defb 0ffh		;39b5	ff		.
	defb 0ffh		;39b6	ff		.
	defb 0ffh		;39b7	ff		.
	defb 0ffh		;39b8	ff		.
	defb 0ffh		;39b9	ff		.
	defb 0ffh		;39ba	ff		.
	defb 0ffh		;39bb	ff		.
	defb 0ffh		;39bc	ff		.
	defb 0ffh		;39bd	ff		.
	defb 0ffh		;39be	ff		.
	defb 0ffh		;39bf	ff		.
	defb 0ffh		;39c0	ff		.
	defb 0ffh		;39c1	ff		.
	defb 0ffh		;39c2	ff		.
	defb 0ffh		;39c3	ff		.
	defb 0ffh		;39c4	ff		.
	defb 0ffh		;39c5	ff		.
	defb 0ffh		;39c6	ff		.
	defb 0ffh		;39c7	ff		.
	defb 0ffh		;39c8	ff		.
	defb 0ffh		;39c9	ff		.
	defb 0ffh		;39ca	ff		.
	defb 0ffh		;39cb	ff		.
	defb 0ffh		;39cc	ff		.
	defb 0ffh		;39cd	ff		.
	defb 0ffh		;39ce	ff		.
	defb 0ffh		;39cf	ff		.
	defb 0ffh		;39d0	ff		.
	defb 0ffh		;39d1	ff		.
	defb 0ffh		;39d2	ff		.
	defb 0ffh		;39d3	ff		.
	defb 0ffh		;39d4	ff		.
	defb 0ffh		;39d5	ff		.
	defb 0ffh		;39d6	ff		.
	defb 0ffh		;39d7	ff		.
	defb 0ffh		;39d8	ff		.
	defb 0ffh		;39d9	ff		.
	defb 0ffh		;39da	ff		.
	defb 0ffh		;39db	ff		.
	defb 0ffh		;39dc	ff		.
	defb 0ffh		;39dd	ff		.
	defb 0ffh		;39de	ff		.
	defb 0ffh		;39df	ff		.
	defb 0ffh		;39e0	ff		.
	defb 0ffh		;39e1	ff		.
	defb 0ffh		;39e2	ff		.
	defb 0ffh		;39e3	ff		.
	defb 0ffh		;39e4	ff		.
	defb 0ffh		;39e5	ff		.
	defb 0ffh		;39e6	ff		.
	defb 0ffh		;39e7	ff		.
	defb 0ffh		;39e8	ff		.
	defb 0ffh		;39e9	ff		.
	defb 0ffh		;39ea	ff		.
	defb 0ffh		;39eb	ff		.
	defb 0ffh		;39ec	ff		.
	defb 0ffh		;39ed	ff		.
	defb 0ffh		;39ee	ff		.
	defb 0ffh		;39ef	ff		.
	defb 0ffh		;39f0	ff		.
	defb 0ffh		;39f1	ff		.
	defb 0ffh		;39f2	ff		.
	defb 0ffh		;39f3	ff		.
	defb 0ffh		;39f4	ff		.
	defb 0ffh		;39f5	ff		.
	defb 0ffh		;39f6	ff		.
	defb 0ffh		;39f7	ff		.
	defb 0ffh		;39f8	ff		.
	defb 0ffh		;39f9	ff		.
	defb 0ffh		;39fa	ff		.
	defb 0ffh		;39fb	ff		.
	defb 0ffh		;39fc	ff		.
	defb 0ffh		;39fd	ff		.
	defb 0ffh		;39fe	ff		.
	defb 0ffh		;39ff	ff		.
	defb 0ffh		;3a00	ff		.
	defb 0ffh		;3a01	ff		.
	defb 0ffh		;3a02	ff		.
	defb 0ffh		;3a03	ff		.
	defb 0ffh		;3a04	ff		.
	defb 0ffh		;3a05	ff		.
	defb 0ffh		;3a06	ff		.
	defb 0ffh		;3a07	ff		.
	defb 0ffh		;3a08	ff		.
	defb 0ffh		;3a09	ff		.
	defb 0ffh		;3a0a	ff		.
	defb 0ffh		;3a0b	ff		.
	defb 0ffh		;3a0c	ff		.
	defb 0ffh		;3a0d	ff		.
	defb 0ffh		;3a0e	ff		.
	defb 0ffh		;3a0f	ff		.
	defb 0ffh		;3a10	ff		.
	defb 0ffh		;3a11	ff		.
	defb 0ffh		;3a12	ff		.
	defb 0ffh		;3a13	ff		.
	defb 0ffh		;3a14	ff		.
	defb 0ffh		;3a15	ff		.
	defb 0ffh		;3a16	ff		.
	defb 0ffh		;3a17	ff		.
	defb 0ffh		;3a18	ff		.
	defb 0ffh		;3a19	ff		.
	defb 0ffh		;3a1a	ff		.
	defb 0ffh		;3a1b	ff		.
	defb 0ffh		;3a1c	ff		.
	defb 0ffh		;3a1d	ff		.
	defb 0ffh		;3a1e	ff		.
	defb 0ffh		;3a1f	ff		.
	defb 0ffh		;3a20	ff		.
	defb 0ffh		;3a21	ff		.
	defb 0ffh		;3a22	ff		.
	defb 0ffh		;3a23	ff		.
	defb 0ffh		;3a24	ff		.
	defb 0ffh		;3a25	ff		.
	defb 0ffh		;3a26	ff		.
	defb 0ffh		;3a27	ff		.
	defb 0ffh		;3a28	ff		.
	defb 0ffh		;3a29	ff		.
	defb 0ffh		;3a2a	ff		.
	defb 0ffh		;3a2b	ff		.
	defb 0ffh		;3a2c	ff		.
	defb 0ffh		;3a2d	ff		.
	defb 0ffh		;3a2e	ff		.
	defb 0ffh		;3a2f	ff		.
	defb 0ffh		;3a30	ff		.
	defb 0ffh		;3a31	ff		.
	defb 0ffh		;3a32	ff		.
	defb 0ffh		;3a33	ff		.
	defb 0ffh		;3a34	ff		.
	defb 0ffh		;3a35	ff		.
	defb 0ffh		;3a36	ff		.
	defb 0ffh		;3a37	ff		.
	defb 0ffh		;3a38	ff		.
	defb 0ffh		;3a39	ff		.
	defb 0ffh		;3a3a	ff		.
	defb 0ffh		;3a3b	ff		.
	defb 0ffh		;3a3c	ff		.
	defb 0ffh		;3a3d	ff		.
	defb 0ffh		;3a3e	ff		.
	defb 0ffh		;3a3f	ff		.
	defb 0ffh		;3a40	ff		.
	defb 0ffh		;3a41	ff		.
	defb 0ffh		;3a42	ff		.
	defb 0ffh		;3a43	ff		.
	defb 0ffh		;3a44	ff		.
	defb 0ffh		;3a45	ff		.
	defb 0ffh		;3a46	ff		.
	defb 0ffh		;3a47	ff		.
	defb 0ffh		;3a48	ff		.
	defb 0ffh		;3a49	ff		.
	defb 0ffh		;3a4a	ff		.
	defb 0ffh		;3a4b	ff		.
	defb 0ffh		;3a4c	ff		.
	defb 0ffh		;3a4d	ff		.
	defb 0ffh		;3a4e	ff		.
	defb 0ffh		;3a4f	ff		.
	defb 0ffh		;3a50	ff		.
	defb 0ffh		;3a51	ff		.
	defb 0ffh		;3a52	ff		.
	defb 0ffh		;3a53	ff		.
	defb 0ffh		;3a54	ff		.
	defb 0ffh		;3a55	ff		.
	defb 0ffh		;3a56	ff		.
	defb 0ffh		;3a57	ff		.
	defb 0ffh		;3a58	ff		.
	defb 0ffh		;3a59	ff		.
	defb 0ffh		;3a5a	ff		.
	defb 0ffh		;3a5b	ff		.
	defb 0ffh		;3a5c	ff		.
	defb 0ffh		;3a5d	ff		.
	defb 0ffh		;3a5e	ff		.
	defb 0ffh		;3a5f	ff		.
	defb 0ffh		;3a60	ff		.
	defb 0ffh		;3a61	ff		.
	defb 0ffh		;3a62	ff		.
	defb 0ffh		;3a63	ff		.
	defb 0ffh		;3a64	ff		.
	defb 0ffh		;3a65	ff		.
	defb 0ffh		;3a66	ff		.
	defb 0ffh		;3a67	ff		.
	defb 0ffh		;3a68	ff		.
	defb 0ffh		;3a69	ff		.
	defb 0ffh		;3a6a	ff		.
	defb 0ffh		;3a6b	ff		.
	defb 0ffh		;3a6c	ff		.
	defb 0ffh		;3a6d	ff		.
	defb 0ffh		;3a6e	ff		.
	defb 0ffh		;3a6f	ff		.
	defb 0ffh		;3a70	ff		.
	defb 0ffh		;3a71	ff		.
	defb 0ffh		;3a72	ff		.
	defb 0ffh		;3a73	ff		.
	defb 0ffh		;3a74	ff		.
	defb 0ffh		;3a75	ff		.
	defb 0ffh		;3a76	ff		.
	defb 0ffh		;3a77	ff		.
	defb 0ffh		;3a78	ff		.
	defb 0ffh		;3a79	ff		.
	defb 0ffh		;3a7a	ff		.
	defb 0ffh		;3a7b	ff		.
	defb 0ffh		;3a7c	ff		.
	defb 0ffh		;3a7d	ff		.
	defb 0ffh		;3a7e	ff		.
	defb 0ffh		;3a7f	ff		.
	defb 0ffh		;3a80	ff		.
	defb 0ffh		;3a81	ff		.
	defb 0ffh		;3a82	ff		.
	defb 0ffh		;3a83	ff		.
	defb 0ffh		;3a84	ff		.
	defb 0ffh		;3a85	ff		.
	defb 0ffh		;3a86	ff		.
	defb 0ffh		;3a87	ff		.
	defb 0ffh		;3a88	ff		.
	defb 0ffh		;3a89	ff		.
	defb 0ffh		;3a8a	ff		.
	defb 0ffh		;3a8b	ff		.
	defb 0ffh		;3a8c	ff		.
	defb 0ffh		;3a8d	ff		.
	defb 0ffh		;3a8e	ff		.
	defb 0ffh		;3a8f	ff		.
	defb 0ffh		;3a90	ff		.
	defb 0ffh		;3a91	ff		.
	defb 0ffh		;3a92	ff		.
	defb 0ffh		;3a93	ff		.
	defb 0ffh		;3a94	ff		.
	defb 0ffh		;3a95	ff		.
	defb 0ffh		;3a96	ff		.
	defb 0ffh		;3a97	ff		.
	defb 0ffh		;3a98	ff		.
	defb 0ffh		;3a99	ff		.
	defb 0ffh		;3a9a	ff		.
	defb 0ffh		;3a9b	ff		.
	defb 0ffh		;3a9c	ff		.
	defb 0ffh		;3a9d	ff		.
	defb 0ffh		;3a9e	ff		.
	defb 0ffh		;3a9f	ff		.
	defb 0ffh		;3aa0	ff		.
	defb 0ffh		;3aa1	ff		.
	defb 0ffh		;3aa2	ff		.
	defb 0ffh		;3aa3	ff		.
	defb 0ffh		;3aa4	ff		.
	defb 0ffh		;3aa5	ff		.
	defb 0ffh		;3aa6	ff		.
	defb 0ffh		;3aa7	ff		.
	defb 0ffh		;3aa8	ff		.
	defb 0ffh		;3aa9	ff		.
	defb 0ffh		;3aaa	ff		.
	defb 0ffh		;3aab	ff		.
	defb 0ffh		;3aac	ff		.
	defb 0ffh		;3aad	ff		.
	defb 0ffh		;3aae	ff		.
	defb 0ffh		;3aaf	ff		.
	defb 0ffh		;3ab0	ff		.
	defb 0ffh		;3ab1	ff		.
	defb 0ffh		;3ab2	ff		.
	defb 0ffh		;3ab3	ff		.
	defb 0ffh		;3ab4	ff		.
	defb 0ffh		;3ab5	ff		.
	defb 0ffh		;3ab6	ff		.
	defb 0ffh		;3ab7	ff		.
	defb 0ffh		;3ab8	ff		.
	defb 0ffh		;3ab9	ff		.
	defb 0ffh		;3aba	ff		.
	defb 0ffh		;3abb	ff		.
	defb 0ffh		;3abc	ff		.
	defb 0ffh		;3abd	ff		.
	defb 0ffh		;3abe	ff		.
	defb 0ffh		;3abf	ff		.
	defb 0ffh		;3ac0	ff		.
	defb 0ffh		;3ac1	ff		.
	defb 0ffh		;3ac2	ff		.
	defb 0ffh		;3ac3	ff		.
	defb 0ffh		;3ac4	ff		.
	defb 0ffh		;3ac5	ff		.
	defb 0ffh		;3ac6	ff		.
	defb 0ffh		;3ac7	ff		.
	defb 0ffh		;3ac8	ff		.
	defb 0ffh		;3ac9	ff		.
	defb 0ffh		;3aca	ff		.
	defb 0ffh		;3acb	ff		.
	defb 0ffh		;3acc	ff		.
	defb 0ffh		;3acd	ff		.
	defb 0ffh		;3ace	ff		.
	defb 0ffh		;3acf	ff		.
	defb 0ffh		;3ad0	ff		.
	defb 0ffh		;3ad1	ff		.
	defb 0ffh		;3ad2	ff		.
	defb 0ffh		;3ad3	ff		.
	defb 0ffh		;3ad4	ff		.
	defb 0ffh		;3ad5	ff		.
	defb 0ffh		;3ad6	ff		.
	defb 0ffh		;3ad7	ff		.
	defb 0ffh		;3ad8	ff		.
	defb 0ffh		;3ad9	ff		.
	defb 0ffh		;3ada	ff		.
	defb 0ffh		;3adb	ff		.
	defb 0ffh		;3adc	ff		.
	defb 0ffh		;3add	ff		.
	defb 0ffh		;3ade	ff		.
	defb 0ffh		;3adf	ff		.
	defb 0ffh		;3ae0	ff		.
	defb 0ffh		;3ae1	ff		.
	defb 0ffh		;3ae2	ff		.
	defb 0ffh		;3ae3	ff		.
	defb 0ffh		;3ae4	ff		.
	defb 0ffh		;3ae5	ff		.
	defb 0ffh		;3ae6	ff		.
	defb 0ffh		;3ae7	ff		.
	defb 0ffh		;3ae8	ff		.
	defb 0ffh		;3ae9	ff		.
	defb 0ffh		;3aea	ff		.
	defb 0ffh		;3aeb	ff		.
	defb 0ffh		;3aec	ff		.
	defb 0ffh		;3aed	ff		.
	defb 0ffh		;3aee	ff		.
	defb 0ffh		;3aef	ff		.
	defb 0ffh		;3af0	ff		.
	defb 0ffh		;3af1	ff		.
	defb 0ffh		;3af2	ff		.
	defb 0ffh		;3af3	ff		.
	defb 0ffh		;3af4	ff		.
	defb 0ffh		;3af5	ff		.
	defb 0ffh		;3af6	ff		.
	defb 0ffh		;3af7	ff		.
	defb 0ffh		;3af8	ff		.
	defb 0ffh		;3af9	ff		.
	defb 0ffh		;3afa	ff		.
	defb 0ffh		;3afb	ff		.
	defb 0ffh		;3afc	ff		.
	defb 0ffh		;3afd	ff		.
	defb 0ffh		;3afe	ff		.
	defb 0ffh		;3aff	ff		.
	defb 0ffh		;3b00	ff		.
	defb 0ffh		;3b01	ff		.
	defb 0ffh		;3b02	ff		.
	defb 0ffh		;3b03	ff		.
	defb 0ffh		;3b04	ff		.
	defb 0ffh		;3b05	ff		.
	defb 0ffh		;3b06	ff		.
	defb 0ffh		;3b07	ff		.
	defb 0ffh		;3b08	ff		.
	defb 0ffh		;3b09	ff		.
	defb 0ffh		;3b0a	ff		.
	defb 0ffh		;3b0b	ff		.
	defb 0ffh		;3b0c	ff		.
	defb 0ffh		;3b0d	ff		.
	defb 0ffh		;3b0e	ff		.
	defb 0ffh		;3b0f	ff		.
	defb 0ffh		;3b10	ff		.
	defb 0ffh		;3b11	ff		.
	defb 0ffh		;3b12	ff		.
	defb 0ffh		;3b13	ff		.
	defb 0ffh		;3b14	ff		.
	defb 0ffh		;3b15	ff		.
	defb 0ffh		;3b16	ff		.
	defb 0ffh		;3b17	ff		.
	defb 0ffh		;3b18	ff		.
	defb 0ffh		;3b19	ff		.
	defb 0ffh		;3b1a	ff		.
	defb 0ffh		;3b1b	ff		.
	defb 0ffh		;3b1c	ff		.
	defb 0ffh		;3b1d	ff		.
	defb 0ffh		;3b1e	ff		.
	defb 0ffh		;3b1f	ff		.
	defb 0ffh		;3b20	ff		.
	defb 0ffh		;3b21	ff		.
	defb 0ffh		;3b22	ff		.
	defb 0ffh		;3b23	ff		.
	defb 0ffh		;3b24	ff		.
	defb 0ffh		;3b25	ff		.
	defb 0ffh		;3b26	ff		.
	defb 0ffh		;3b27	ff		.
	defb 0ffh		;3b28	ff		.
	defb 0ffh		;3b29	ff		.
	defb 0ffh		;3b2a	ff		.
	defb 0ffh		;3b2b	ff		.
	defb 0ffh		;3b2c	ff		.
	defb 0ffh		;3b2d	ff		.
	defb 0ffh		;3b2e	ff		.
	defb 0ffh		;3b2f	ff		.
	defb 0ffh		;3b30	ff		.
	defb 0ffh		;3b31	ff		.
	defb 0ffh		;3b32	ff		.
	defb 0ffh		;3b33	ff		.
	defb 0ffh		;3b34	ff		.
	defb 0ffh		;3b35	ff		.
	defb 0ffh		;3b36	ff		.
	defb 0ffh		;3b37	ff		.
	defb 0ffh		;3b38	ff		.
	defb 0ffh		;3b39	ff		.
	defb 0ffh		;3b3a	ff		.
	defb 0ffh		;3b3b	ff		.
	defb 0ffh		;3b3c	ff		.
	defb 0ffh		;3b3d	ff		.
	defb 0ffh		;3b3e	ff		.
	defb 0ffh		;3b3f	ff		.
	defb 0ffh		;3b40	ff		.
	defb 0ffh		;3b41	ff		.
	defb 0ffh		;3b42	ff		.
	defb 0ffh		;3b43	ff		.
	defb 0ffh		;3b44	ff		.
	defb 0ffh		;3b45	ff		.
	defb 0ffh		;3b46	ff		.
	defb 0ffh		;3b47	ff		.
	defb 0ffh		;3b48	ff		.
	defb 0ffh		;3b49	ff		.
	defb 0ffh		;3b4a	ff		.
	defb 0ffh		;3b4b	ff		.
	defb 0ffh		;3b4c	ff		.
	defb 0ffh		;3b4d	ff		.
	defb 0ffh		;3b4e	ff		.
	defb 0ffh		;3b4f	ff		.
	defb 0ffh		;3b50	ff		.
	defb 0ffh		;3b51	ff		.
	defb 0ffh		;3b52	ff		.
	defb 0ffh		;3b53	ff		.
	defb 0ffh		;3b54	ff		.
	defb 0ffh		;3b55	ff		.
	defb 0ffh		;3b56	ff		.
	defb 0ffh		;3b57	ff		.
	defb 0ffh		;3b58	ff		.
	defb 0ffh		;3b59	ff		.
	defb 0ffh		;3b5a	ff		.
	defb 0ffh		;3b5b	ff		.
	defb 0ffh		;3b5c	ff		.
	defb 0ffh		;3b5d	ff		.
	defb 0ffh		;3b5e	ff		.
	defb 0ffh		;3b5f	ff		.
	defb 0ffh		;3b60	ff		.
	defb 0ffh		;3b61	ff		.
	defb 0ffh		;3b62	ff		.
	defb 0ffh		;3b63	ff		.
	defb 0ffh		;3b64	ff		.
	defb 0ffh		;3b65	ff		.
	defb 0ffh		;3b66	ff		.
	defb 0ffh		;3b67	ff		.
	defb 0ffh		;3b68	ff		.
	defb 0ffh		;3b69	ff		.
	defb 0ffh		;3b6a	ff		.
	defb 0ffh		;3b6b	ff		.
	defb 0ffh		;3b6c	ff		.
	defb 0ffh		;3b6d	ff		.
	defb 0ffh		;3b6e	ff		.
	defb 0ffh		;3b6f	ff		.
	defb 0ffh		;3b70	ff		.
	defb 0ffh		;3b71	ff		.
	defb 0ffh		;3b72	ff		.
	defb 0ffh		;3b73	ff		.
	defb 0ffh		;3b74	ff		.
	defb 0ffh		;3b75	ff		.
	defb 0ffh		;3b76	ff		.
	defb 0ffh		;3b77	ff		.
	defb 0ffh		;3b78	ff		.
	defb 0ffh		;3b79	ff		.
	defb 0ffh		;3b7a	ff		.
	defb 0ffh		;3b7b	ff		.
	defb 0ffh		;3b7c	ff		.
	defb 0ffh		;3b7d	ff		.
	defb 0ffh		;3b7e	ff		.
	defb 0ffh		;3b7f	ff		.
	defb 0ffh		;3b80	ff		.
	defb 0ffh		;3b81	ff		.
	defb 0ffh		;3b82	ff		.
	defb 0ffh		;3b83	ff		.
	defb 0ffh		;3b84	ff		.
	defb 0ffh		;3b85	ff		.
	defb 0ffh		;3b86	ff		.
	defb 0ffh		;3b87	ff		.
	defb 0ffh		;3b88	ff		.
	defb 0ffh		;3b89	ff		.
	defb 0ffh		;3b8a	ff		.
	defb 0ffh		;3b8b	ff		.
	defb 0ffh		;3b8c	ff		.
	defb 0ffh		;3b8d	ff		.
	defb 0ffh		;3b8e	ff		.
	defb 0ffh		;3b8f	ff		.
	defb 0ffh		;3b90	ff		.
	defb 0ffh		;3b91	ff		.
	defb 0ffh		;3b92	ff		.
	defb 0ffh		;3b93	ff		.
	defb 0ffh		;3b94	ff		.
	defb 0ffh		;3b95	ff		.
	defb 0ffh		;3b96	ff		.
	defb 0ffh		;3b97	ff		.
	defb 0ffh		;3b98	ff		.
	defb 0ffh		;3b99	ff		.
	defb 0ffh		;3b9a	ff		.
	defb 0ffh		;3b9b	ff		.
	defb 0ffh		;3b9c	ff		.
	defb 0ffh		;3b9d	ff		.
	defb 0ffh		;3b9e	ff		.
	defb 0ffh		;3b9f	ff		.
	defb 0ffh		;3ba0	ff		.
	defb 0ffh		;3ba1	ff		.
	defb 0ffh		;3ba2	ff		.
	defb 0ffh		;3ba3	ff		.
	defb 0ffh		;3ba4	ff		.
	defb 0ffh		;3ba5	ff		.
	defb 0ffh		;3ba6	ff		.
	defb 0ffh		;3ba7	ff		.
	defb 0ffh		;3ba8	ff		.
	defb 0ffh		;3ba9	ff		.
	defb 0ffh		;3baa	ff		.
	defb 0ffh		;3bab	ff		.
	defb 0ffh		;3bac	ff		.
	defb 0ffh		;3bad	ff		.
	defb 0ffh		;3bae	ff		.
	defb 0ffh		;3baf	ff		.
	defb 0ffh		;3bb0	ff		.
	defb 0ffh		;3bb1	ff		.
	defb 0ffh		;3bb2	ff		.
	defb 0ffh		;3bb3	ff		.
	defb 0ffh		;3bb4	ff		.
	defb 0ffh		;3bb5	ff		.
	defb 0ffh		;3bb6	ff		.
	defb 0ffh		;3bb7	ff		.
	defb 0ffh		;3bb8	ff		.
	defb 0ffh		;3bb9	ff		.
	defb 0ffh		;3bba	ff		.
	defb 0ffh		;3bbb	ff		.
	defb 0ffh		;3bbc	ff		.
	defb 0ffh		;3bbd	ff		.
	defb 0ffh		;3bbe	ff		.
	defb 0ffh		;3bbf	ff		.
	defb 0ffh		;3bc0	ff		.
	defb 0ffh		;3bc1	ff		.
	defb 0ffh		;3bc2	ff		.
	defb 0ffh		;3bc3	ff		.
	defb 0ffh		;3bc4	ff		.
	defb 0ffh		;3bc5	ff		.
	defb 0ffh		;3bc6	ff		.
	defb 0ffh		;3bc7	ff		.
	defb 0ffh		;3bc8	ff		.
	defb 0ffh		;3bc9	ff		.
	defb 0ffh		;3bca	ff		.
	defb 0ffh		;3bcb	ff		.
	defb 0ffh		;3bcc	ff		.
	defb 0ffh		;3bcd	ff		.
	defb 0ffh		;3bce	ff		.
	defb 0ffh		;3bcf	ff		.
	defb 0ffh		;3bd0	ff		.
	defb 0ffh		;3bd1	ff		.
	defb 0ffh		;3bd2	ff		.
	defb 0ffh		;3bd3	ff		.
	defb 0ffh		;3bd4	ff		.
	defb 0ffh		;3bd5	ff		.
	defb 0ffh		;3bd6	ff		.
	defb 0ffh		;3bd7	ff		.
	defb 0ffh		;3bd8	ff		.
	defb 0ffh		;3bd9	ff		.
	defb 0ffh		;3bda	ff		.
	defb 0ffh		;3bdb	ff		.
	defb 0ffh		;3bdc	ff		.
	defb 0ffh		;3bdd	ff		.
	defb 0ffh		;3bde	ff		.
	defb 0ffh		;3bdf	ff		.
	defb 0ffh		;3be0	ff		.
	defb 0ffh		;3be1	ff		.
	defb 0ffh		;3be2	ff		.
	defb 0ffh		;3be3	ff		.
	defb 0ffh		;3be4	ff		.
	defb 0ffh		;3be5	ff		.
	defb 0ffh		;3be6	ff		.
	defb 0ffh		;3be7	ff		.
	defb 0ffh		;3be8	ff		.
	defb 0ffh		;3be9	ff		.
	defb 0ffh		;3bea	ff		.
	defb 0ffh		;3beb	ff		.
	defb 0ffh		;3bec	ff		.
	defb 0ffh		;3bed	ff		.
	defb 0ffh		;3bee	ff		.
	defb 0ffh		;3bef	ff		.
	defb 0ffh		;3bf0	ff		.
	defb 0ffh		;3bf1	ff		.
	defb 0ffh		;3bf2	ff		.
	defb 0ffh		;3bf3	ff		.
	defb 0ffh		;3bf4	ff		.
	defb 0ffh		;3bf5	ff		.
	defb 0ffh		;3bf6	ff		.
	defb 0ffh		;3bf7	ff		.
	defb 0ffh		;3bf8	ff		.
	defb 0ffh		;3bf9	ff		.
	defb 0ffh		;3bfa	ff		.
	defb 0ffh		;3bfb	ff		.
	defb 0ffh		;3bfc	ff		.
	defb 0ffh		;3bfd	ff		.
	defb 0ffh		;3bfe	ff		.
	defb 0ffh		;3bff	ff		.
	defb 0ffh		;3c00	ff		.
	defb 0ffh		;3c01	ff		.
	defb 0ffh		;3c02	ff		.
	defb 0ffh		;3c03	ff		.
	defb 0ffh		;3c04	ff		.
	defb 0ffh		;3c05	ff		.
	defb 0ffh		;3c06	ff		.
	defb 0ffh		;3c07	ff		.
	defb 0ffh		;3c08	ff		.
	defb 0ffh		;3c09	ff		.
	defb 0ffh		;3c0a	ff		.
	defb 0ffh		;3c0b	ff		.
	defb 0ffh		;3c0c	ff		.
	defb 0ffh		;3c0d	ff		.
	defb 0ffh		;3c0e	ff		.
	defb 0ffh		;3c0f	ff		.
	defb 0ffh		;3c10	ff		.
	defb 0ffh		;3c11	ff		.
	defb 0ffh		;3c12	ff		.
	defb 0ffh		;3c13	ff		.
	defb 0ffh		;3c14	ff		.
	defb 0ffh		;3c15	ff		.
	defb 0ffh		;3c16	ff		.
	defb 0ffh		;3c17	ff		.
	defb 0ffh		;3c18	ff		.
	defb 0ffh		;3c19	ff		.
	defb 0ffh		;3c1a	ff		.
	defb 0ffh		;3c1b	ff		.
	defb 0ffh		;3c1c	ff		.
	defb 0ffh		;3c1d	ff		.
	defb 0ffh		;3c1e	ff		.
	defb 0ffh		;3c1f	ff		.
	defb 0ffh		;3c20	ff		.
	defb 0ffh		;3c21	ff		.
	defb 0ffh		;3c22	ff		.
	defb 0ffh		;3c23	ff		.
	defb 0ffh		;3c24	ff		.
	defb 0ffh		;3c25	ff		.
	defb 0ffh		;3c26	ff		.
	defb 0ffh		;3c27	ff		.
	defb 0ffh		;3c28	ff		.
	defb 0ffh		;3c29	ff		.
	defb 0ffh		;3c2a	ff		.
	defb 0ffh		;3c2b	ff		.
	defb 0ffh		;3c2c	ff		.
	defb 0ffh		;3c2d	ff		.
	defb 0ffh		;3c2e	ff		.
	defb 0ffh		;3c2f	ff		.
	defb 0ffh		;3c30	ff		.
	defb 0ffh		;3c31	ff		.
	defb 0ffh		;3c32	ff		.
	defb 0ffh		;3c33	ff		.
	defb 0ffh		;3c34	ff		.
	defb 0ffh		;3c35	ff		.
	defb 0ffh		;3c36	ff		.
	defb 0ffh		;3c37	ff		.
	defb 0ffh		;3c38	ff		.
	defb 0ffh		;3c39	ff		.
	defb 0ffh		;3c3a	ff		.
	defb 0ffh		;3c3b	ff		.
	defb 0ffh		;3c3c	ff		.
	defb 0ffh		;3c3d	ff		.
	defb 0ffh		;3c3e	ff		.
	defb 0ffh		;3c3f	ff		.
	defb 0ffh		;3c40	ff		.
	defb 0ffh		;3c41	ff		.
	defb 0ffh		;3c42	ff		.
	defb 0ffh		;3c43	ff		.
	defb 0ffh		;3c44	ff		.
	defb 0ffh		;3c45	ff		.
	defb 0ffh		;3c46	ff		.
	defb 0ffh		;3c47	ff		.
	defb 0ffh		;3c48	ff		.
	defb 0ffh		;3c49	ff		.
	defb 0ffh		;3c4a	ff		.
	defb 0ffh		;3c4b	ff		.
	defb 0ffh		;3c4c	ff		.
	defb 0ffh		;3c4d	ff		.
	defb 0ffh		;3c4e	ff		.
	defb 0ffh		;3c4f	ff		.
	defb 0ffh		;3c50	ff		.
	defb 0ffh		;3c51	ff		.
	defb 0ffh		;3c52	ff		.
	defb 0ffh		;3c53	ff		.
	defb 0ffh		;3c54	ff		.
	defb 0ffh		;3c55	ff		.
	defb 0ffh		;3c56	ff		.
	defb 0ffh		;3c57	ff		.
	defb 0ffh		;3c58	ff		.
	defb 0ffh		;3c59	ff		.
	defb 0ffh		;3c5a	ff		.
	defb 0ffh		;3c5b	ff		.
	defb 0ffh		;3c5c	ff		.
	defb 0ffh		;3c5d	ff		.
	defb 0ffh		;3c5e	ff		.
	defb 0ffh		;3c5f	ff		.
	defb 0ffh		;3c60	ff		.
	defb 0ffh		;3c61	ff		.
	defb 0ffh		;3c62	ff		.
	defb 0ffh		;3c63	ff		.
	defb 0ffh		;3c64	ff		.
	defb 0ffh		;3c65	ff		.
	defb 0ffh		;3c66	ff		.
	defb 0ffh		;3c67	ff		.
	defb 0ffh		;3c68	ff		.
	defb 0ffh		;3c69	ff		.
	defb 0ffh		;3c6a	ff		.
	defb 0ffh		;3c6b	ff		.
	defb 0ffh		;3c6c	ff		.
	defb 0ffh		;3c6d	ff		.
	defb 0ffh		;3c6e	ff		.
	defb 0ffh		;3c6f	ff		.
	defb 0ffh		;3c70	ff		.
	defb 0ffh		;3c71	ff		.
	defb 0ffh		;3c72	ff		.
	defb 0ffh		;3c73	ff		.
	defb 0ffh		;3c74	ff		.
	defb 0ffh		;3c75	ff		.
	defb 0ffh		;3c76	ff		.
	defb 0ffh		;3c77	ff		.
	defb 0ffh		;3c78	ff		.
	defb 0ffh		;3c79	ff		.
	defb 0ffh		;3c7a	ff		.
	defb 0ffh		;3c7b	ff		.
	defb 0ffh		;3c7c	ff		.
	defb 0ffh		;3c7d	ff		.
	defb 0ffh		;3c7e	ff		.
	defb 0ffh		;3c7f	ff		.
	defb 0ffh		;3c80	ff		.
	defb 0ffh		;3c81	ff		.
	defb 0ffh		;3c82	ff		.
	defb 0ffh		;3c83	ff		.
	defb 0ffh		;3c84	ff		.
	defb 0ffh		;3c85	ff		.
	defb 0ffh		;3c86	ff		.
	defb 0ffh		;3c87	ff		.
	defb 0ffh		;3c88	ff		.
	defb 0ffh		;3c89	ff		.
	defb 0ffh		;3c8a	ff		.
	defb 0ffh		;3c8b	ff		.
	defb 0ffh		;3c8c	ff		.
	defb 0ffh		;3c8d	ff		.
	defb 0ffh		;3c8e	ff		.
	defb 0ffh		;3c8f	ff		.
	defb 0ffh		;3c90	ff		.
	defb 0ffh		;3c91	ff		.
	defb 0ffh		;3c92	ff		.
	defb 0ffh		;3c93	ff		.
	defb 0ffh		;3c94	ff		.
	defb 0ffh		;3c95	ff		.
	defb 0ffh		;3c96	ff		.
	defb 0ffh		;3c97	ff		.
	defb 0ffh		;3c98	ff		.
	defb 0ffh		;3c99	ff		.
	defb 0ffh		;3c9a	ff		.
	defb 0ffh		;3c9b	ff		.
	defb 0ffh		;3c9c	ff		.
	defb 0ffh		;3c9d	ff		.
	defb 0ffh		;3c9e	ff		.
	defb 0ffh		;3c9f	ff		.
	defb 0ffh		;3ca0	ff		.
	defb 0ffh		;3ca1	ff		.
	defb 0ffh		;3ca2	ff		.
	defb 0ffh		;3ca3	ff		.
	defb 0ffh		;3ca4	ff		.
	defb 0ffh		;3ca5	ff		.
	defb 0ffh		;3ca6	ff		.
	defb 0ffh		;3ca7	ff		.
	defb 0ffh		;3ca8	ff		.
	defb 0ffh		;3ca9	ff		.
	defb 0ffh		;3caa	ff		.
	defb 0ffh		;3cab	ff		.
	defb 0ffh		;3cac	ff		.
	defb 0ffh		;3cad	ff		.
	defb 0ffh		;3cae	ff		.
	defb 0ffh		;3caf	ff		.
	defb 0ffh		;3cb0	ff		.
	defb 0ffh		;3cb1	ff		.
	defb 0ffh		;3cb2	ff		.
	defb 0ffh		;3cb3	ff		.
	defb 0ffh		;3cb4	ff		.
	defb 0ffh		;3cb5	ff		.
	defb 0ffh		;3cb6	ff		.
	defb 0ffh		;3cb7	ff		.
	defb 0ffh		;3cb8	ff		.
	defb 0ffh		;3cb9	ff		.
	defb 0ffh		;3cba	ff		.
	defb 0ffh		;3cbb	ff		.
	defb 0ffh		;3cbc	ff		.
	defb 0ffh		;3cbd	ff		.
	defb 0ffh		;3cbe	ff		.
	defb 0ffh		;3cbf	ff		.
	defb 0ffh		;3cc0	ff		.
	defb 0ffh		;3cc1	ff		.
	defb 0ffh		;3cc2	ff		.
	defb 0ffh		;3cc3	ff		.
	defb 0ffh		;3cc4	ff		.
	defb 0ffh		;3cc5	ff		.
	defb 0ffh		;3cc6	ff		.
	defb 0ffh		;3cc7	ff		.
	defb 0ffh		;3cc8	ff		.
	defb 0ffh		;3cc9	ff		.
	defb 0ffh		;3cca	ff		.
	defb 0ffh		;3ccb	ff		.
	defb 0ffh		;3ccc	ff		.
	defb 0ffh		;3ccd	ff		.
	defb 0ffh		;3cce	ff		.
	defb 0ffh		;3ccf	ff		.
	defb 0ffh		;3cd0	ff		.
	defb 0ffh		;3cd1	ff		.
	defb 0ffh		;3cd2	ff		.
	defb 0ffh		;3cd3	ff		.
	defb 0ffh		;3cd4	ff		.
	defb 0ffh		;3cd5	ff		.
	defb 0ffh		;3cd6	ff		.
	defb 0ffh		;3cd7	ff		.
	defb 0ffh		;3cd8	ff		.
	defb 0ffh		;3cd9	ff		.
	defb 0ffh		;3cda	ff		.
	defb 0ffh		;3cdb	ff		.
	defb 0ffh		;3cdc	ff		.
	defb 0ffh		;3cdd	ff		.
	defb 0ffh		;3cde	ff		.
	defb 0ffh		;3cdf	ff		.
	defb 0ffh		;3ce0	ff		.
	defb 0ffh		;3ce1	ff		.
	defb 0ffh		;3ce2	ff		.
	defb 0ffh		;3ce3	ff		.
	defb 0ffh		;3ce4	ff		.
	defb 0ffh		;3ce5	ff		.
	defb 0ffh		;3ce6	ff		.
	defb 0ffh		;3ce7	ff		.
	defb 0ffh		;3ce8	ff		.
	defb 0ffh		;3ce9	ff		.
	defb 0ffh		;3cea	ff		.
	defb 0ffh		;3ceb	ff		.
	defb 0ffh		;3cec	ff		.
	defb 0ffh		;3ced	ff		.
	defb 0ffh		;3cee	ff		.
	defb 0ffh		;3cef	ff		.
	defb 0ffh		;3cf0	ff		.
	defb 0ffh		;3cf1	ff		.
	defb 0ffh		;3cf2	ff		.
	defb 0ffh		;3cf3	ff		.
	defb 0ffh		;3cf4	ff		.
	defb 0ffh		;3cf5	ff		.
	defb 0ffh		;3cf6	ff		.
	defb 0ffh		;3cf7	ff		.
	defb 0ffh		;3cf8	ff		.
	defb 0ffh		;3cf9	ff		.
	defb 0ffh		;3cfa	ff		.
	defb 0ffh		;3cfb	ff		.
	defb 0ffh		;3cfc	ff		.
	defb 0ffh		;3cfd	ff		.
	defb 0ffh		;3cfe	ff		.
	defb 0ffh		;3cff	ff		.
	defb 0ffh		;3d00	ff		.
	defb 0ffh		;3d01	ff		.
	defb 0ffh		;3d02	ff		.
	defb 0ffh		;3d03	ff		.
	defb 0ffh		;3d04	ff		.
	defb 0ffh		;3d05	ff		.
	defb 0ffh		;3d06	ff		.
	defb 0ffh		;3d07	ff		.
	defb 0ffh		;3d08	ff		.
	defb 0ffh		;3d09	ff		.
	defb 0ffh		;3d0a	ff		.
	defb 0ffh		;3d0b	ff		.
	defb 0ffh		;3d0c	ff		.
	defb 0ffh		;3d0d	ff		.
	defb 0ffh		;3d0e	ff		.
	defb 0ffh		;3d0f	ff		.
	defb 0ffh		;3d10	ff		.
	defb 0ffh		;3d11	ff		.
	defb 0ffh		;3d12	ff		.
	defb 0ffh		;3d13	ff		.
	defb 0ffh		;3d14	ff		.
	defb 0ffh		;3d15	ff		.
	defb 0ffh		;3d16	ff		.
	defb 0ffh		;3d17	ff		.
	defb 0ffh		;3d18	ff		.
	defb 0ffh		;3d19	ff		.
	defb 0ffh		;3d1a	ff		.
	defb 0ffh		;3d1b	ff		.
	defb 0ffh		;3d1c	ff		.
	defb 0ffh		;3d1d	ff		.
	defb 0ffh		;3d1e	ff		.
	defb 0ffh		;3d1f	ff		.
	defb 0ffh		;3d20	ff		.
	defb 0ffh		;3d21	ff		.
	defb 0ffh		;3d22	ff		.
	defb 0ffh		;3d23	ff		.
	defb 0ffh		;3d24	ff		.
	defb 0ffh		;3d25	ff		.
	defb 0ffh		;3d26	ff		.
	defb 0ffh		;3d27	ff		.
	defb 0ffh		;3d28	ff		.
	defb 0ffh		;3d29	ff		.
	defb 0ffh		;3d2a	ff		.
	defb 0ffh		;3d2b	ff		.
	defb 0ffh		;3d2c	ff		.
	defb 0ffh		;3d2d	ff		.
	defb 0ffh		;3d2e	ff		.
	defb 0ffh		;3d2f	ff		.
	defb 0ffh		;3d30	ff		.
	defb 0ffh		;3d31	ff		.
	defb 0ffh		;3d32	ff		.
	defb 0ffh		;3d33	ff		.
	defb 0ffh		;3d34	ff		.
	defb 0ffh		;3d35	ff		.
	defb 0ffh		;3d36	ff		.
	defb 0ffh		;3d37	ff		.
	defb 0ffh		;3d38	ff		.
	defb 0ffh		;3d39	ff		.
	defb 0ffh		;3d3a	ff		.
	defb 0ffh		;3d3b	ff		.
	defb 0ffh		;3d3c	ff		.
	defb 0ffh		;3d3d	ff		.
	defb 0ffh		;3d3e	ff		.
	defb 0ffh		;3d3f	ff		.
	defb 0ffh		;3d40	ff		.
	defb 0ffh		;3d41	ff		.
	defb 0ffh		;3d42	ff		.
	defb 0ffh		;3d43	ff		.
	defb 0ffh		;3d44	ff		.
	defb 0ffh		;3d45	ff		.
	defb 0ffh		;3d46	ff		.
	defb 0ffh		;3d47	ff		.
	defb 0ffh		;3d48	ff		.
	defb 0ffh		;3d49	ff		.
	defb 0ffh		;3d4a	ff		.
	defb 0ffh		;3d4b	ff		.
	defb 0ffh		;3d4c	ff		.
	defb 0ffh		;3d4d	ff		.
	defb 0ffh		;3d4e	ff		.
	defb 0ffh		;3d4f	ff		.
	defb 0ffh		;3d50	ff		.
	defb 0ffh		;3d51	ff		.
	defb 0ffh		;3d52	ff		.
	defb 0ffh		;3d53	ff		.
	defb 0ffh		;3d54	ff		.
	defb 0ffh		;3d55	ff		.
	defb 0ffh		;3d56	ff		.
	defb 0ffh		;3d57	ff		.
	defb 0ffh		;3d58	ff		.
	defb 0ffh		;3d59	ff		.
	defb 0ffh		;3d5a	ff		.
	defb 0ffh		;3d5b	ff		.
	defb 0ffh		;3d5c	ff		.
	defb 0ffh		;3d5d	ff		.
	defb 0ffh		;3d5e	ff		.
	defb 0ffh		;3d5f	ff		.
	defb 0ffh		;3d60	ff		.
	defb 0ffh		;3d61	ff		.
	defb 0ffh		;3d62	ff		.
	defb 0ffh		;3d63	ff		.
	defb 0ffh		;3d64	ff		.
	defb 0ffh		;3d65	ff		.
	defb 0ffh		;3d66	ff		.
	defb 0ffh		;3d67	ff		.
	defb 0ffh		;3d68	ff		.
	defb 0ffh		;3d69	ff		.
	defb 0ffh		;3d6a	ff		.
	defb 0ffh		;3d6b	ff		.
	defb 0ffh		;3d6c	ff		.
	defb 0ffh		;3d6d	ff		.
	defb 0ffh		;3d6e	ff		.
	defb 0ffh		;3d6f	ff		.
	defb 0ffh		;3d70	ff		.
	defb 0ffh		;3d71	ff		.
	defb 0ffh		;3d72	ff		.
	defb 0ffh		;3d73	ff		.
	defb 0ffh		;3d74	ff		.
	defb 0ffh		;3d75	ff		.
	defb 0ffh		;3d76	ff		.
	defb 0ffh		;3d77	ff		.
	defb 0ffh		;3d78	ff		.
	defb 0ffh		;3d79	ff		.
	defb 0ffh		;3d7a	ff		.
	defb 0ffh		;3d7b	ff		.
	defb 0ffh		;3d7c	ff		.
	defb 0ffh		;3d7d	ff		.
	defb 0ffh		;3d7e	ff		.
	defb 0ffh		;3d7f	ff		.
	defb 0ffh		;3d80	ff		.
	defb 0ffh		;3d81	ff		.
	defb 0ffh		;3d82	ff		.
	defb 0ffh		;3d83	ff		.
	defb 0ffh		;3d84	ff		.
	defb 0ffh		;3d85	ff		.
	defb 0ffh		;3d86	ff		.
	defb 0ffh		;3d87	ff		.
	defb 0ffh		;3d88	ff		.
	defb 0ffh		;3d89	ff		.
	defb 0ffh		;3d8a	ff		.
	defb 0ffh		;3d8b	ff		.
	defb 0ffh		;3d8c	ff		.
	defb 0ffh		;3d8d	ff		.
	defb 0ffh		;3d8e	ff		.
	defb 0ffh		;3d8f	ff		.
	defb 0ffh		;3d90	ff		.
	defb 0ffh		;3d91	ff		.
	defb 0ffh		;3d92	ff		.
	defb 0ffh		;3d93	ff		.
	defb 0ffh		;3d94	ff		.
	defb 0ffh		;3d95	ff		.
	defb 0ffh		;3d96	ff		.
	defb 0ffh		;3d97	ff		.
	defb 0ffh		;3d98	ff		.
	defb 0ffh		;3d99	ff		.
	defb 0ffh		;3d9a	ff		.
	defb 0ffh		;3d9b	ff		.
	defb 0ffh		;3d9c	ff		.
	defb 0ffh		;3d9d	ff		.
	defb 0ffh		;3d9e	ff		.
	defb 0ffh		;3d9f	ff		.
	defb 0ffh		;3da0	ff		.
	defb 0ffh		;3da1	ff		.
	defb 0ffh		;3da2	ff		.
	defb 0ffh		;3da3	ff		.
	defb 0ffh		;3da4	ff		.
	defb 0ffh		;3da5	ff		.
	defb 0ffh		;3da6	ff		.
	defb 0ffh		;3da7	ff		.
	defb 0ffh		;3da8	ff		.
	defb 0ffh		;3da9	ff		.
	defb 0ffh		;3daa	ff		.
	defb 0ffh		;3dab	ff		.
	defb 0ffh		;3dac	ff		.
	defb 0ffh		;3dad	ff		.
	defb 0ffh		;3dae	ff		.
	defb 0ffh		;3daf	ff		.
	defb 0ffh		;3db0	ff		.
	defb 0ffh		;3db1	ff		.
	defb 0ffh		;3db2	ff		.
	defb 0ffh		;3db3	ff		.
	defb 0ffh		;3db4	ff		.
	defb 0ffh		;3db5	ff		.
	defb 0ffh		;3db6	ff		.
	defb 0ffh		;3db7	ff		.
	defb 0ffh		;3db8	ff		.
	defb 0ffh		;3db9	ff		.
	defb 0ffh		;3dba	ff		.
	defb 0ffh		;3dbb	ff		.
	defb 0ffh		;3dbc	ff		.
	defb 0ffh		;3dbd	ff		.
	defb 0ffh		;3dbe	ff		.
	defb 0ffh		;3dbf	ff		.
	defb 0ffh		;3dc0	ff		.
	defb 0ffh		;3dc1	ff		.
	defb 0ffh		;3dc2	ff		.
	defb 0ffh		;3dc3	ff		.
	defb 0ffh		;3dc4	ff		.
	defb 0ffh		;3dc5	ff		.
	defb 0ffh		;3dc6	ff		.
	defb 0ffh		;3dc7	ff		.
	defb 0ffh		;3dc8	ff		.
	defb 0ffh		;3dc9	ff		.
	defb 0ffh		;3dca	ff		.
	defb 0ffh		;3dcb	ff		.
	defb 0ffh		;3dcc	ff		.
	defb 0ffh		;3dcd	ff		.
	defb 0ffh		;3dce	ff		.
	defb 0ffh		;3dcf	ff		.
	defb 0ffh		;3dd0	ff		.
	defb 0ffh		;3dd1	ff		.
	defb 0ffh		;3dd2	ff		.
	defb 0ffh		;3dd3	ff		.
	defb 0ffh		;3dd4	ff		.
	defb 0ffh		;3dd5	ff		.
	defb 0ffh		;3dd6	ff		.
	defb 0ffh		;3dd7	ff		.
	defb 0ffh		;3dd8	ff		.
	defb 0ffh		;3dd9	ff		.
	defb 0ffh		;3dda	ff		.
	defb 0ffh		;3ddb	ff		.
	defb 0ffh		;3ddc	ff		.
	defb 0ffh		;3ddd	ff		.
	defb 0ffh		;3dde	ff		.
	defb 0ffh		;3ddf	ff		.
	defb 0ffh		;3de0	ff		.
	defb 0ffh		;3de1	ff		.
	defb 0ffh		;3de2	ff		.
	defb 0ffh		;3de3	ff		.
	defb 0ffh		;3de4	ff		.
	defb 0ffh		;3de5	ff		.
	defb 0ffh		;3de6	ff		.
	defb 0ffh		;3de7	ff		.
	defb 0ffh		;3de8	ff		.
	defb 0ffh		;3de9	ff		.
	defb 0ffh		;3dea	ff		.
	defb 0ffh		;3deb	ff		.
	defb 0ffh		;3dec	ff		.
	defb 0ffh		;3ded	ff		.
	defb 0ffh		;3dee	ff		.
	defb 0ffh		;3def	ff		.
	defb 0ffh		;3df0	ff		.
	defb 0ffh		;3df1	ff		.
	defb 0ffh		;3df2	ff		.
	defb 0ffh		;3df3	ff		.
	defb 0ffh		;3df4	ff		.
	defb 0ffh		;3df5	ff		.
	defb 0ffh		;3df6	ff		.
	defb 0ffh		;3df7	ff		.
	defb 0ffh		;3df8	ff		.
	defb 0ffh		;3df9	ff		.
	defb 0ffh		;3dfa	ff		.
	defb 0ffh		;3dfb	ff		.
	defb 0ffh		;3dfc	ff		.
	defb 0ffh		;3dfd	ff		.
	defb 0ffh		;3dfe	ff		.
	defb 0ffh		;3dff	ff		.
	defb 0ffh		;3e00	ff		.
	defb 0ffh		;3e01	ff		.
	defb 0ffh		;3e02	ff		.
	defb 0ffh		;3e03	ff		.
	defb 0ffh		;3e04	ff		.
	defb 0ffh		;3e05	ff		.
	defb 0ffh		;3e06	ff		.
	defb 0ffh		;3e07	ff		.
	defb 0ffh		;3e08	ff		.
	defb 0ffh		;3e09	ff		.
	defb 0ffh		;3e0a	ff		.
	defb 0ffh		;3e0b	ff		.
	defb 0ffh		;3e0c	ff		.
	defb 0ffh		;3e0d	ff		.
	defb 0ffh		;3e0e	ff		.
	defb 0ffh		;3e0f	ff		.
	defb 0ffh		;3e10	ff		.
	defb 0ffh		;3e11	ff		.
	defb 0ffh		;3e12	ff		.
	defb 0ffh		;3e13	ff		.
	defb 0ffh		;3e14	ff		.
	defb 0ffh		;3e15	ff		.
	defb 0ffh		;3e16	ff		.
	defb 0ffh		;3e17	ff		.
	defb 0ffh		;3e18	ff		.
	defb 0ffh		;3e19	ff		.
	defb 0ffh		;3e1a	ff		.
	defb 0ffh		;3e1b	ff		.
	defb 0ffh		;3e1c	ff		.
	defb 0ffh		;3e1d	ff		.
	defb 0ffh		;3e1e	ff		.
	defb 0ffh		;3e1f	ff		.
	defb 0ffh		;3e20	ff		.
	defb 0ffh		;3e21	ff		.
	defb 0ffh		;3e22	ff		.
	defb 0ffh		;3e23	ff		.
	defb 0ffh		;3e24	ff		.
	defb 0ffh		;3e25	ff		.
	defb 0ffh		;3e26	ff		.
	defb 0ffh		;3e27	ff		.
	defb 0ffh		;3e28	ff		.
	defb 0ffh		;3e29	ff		.
	defb 0ffh		;3e2a	ff		.
	defb 0ffh		;3e2b	ff		.
	defb 0ffh		;3e2c	ff		.
	defb 0ffh		;3e2d	ff		.
	defb 0ffh		;3e2e	ff		.
	defb 0ffh		;3e2f	ff		.
	defb 0ffh		;3e30	ff		.
	defb 0ffh		;3e31	ff		.
	defb 0ffh		;3e32	ff		.
	defb 0ffh		;3e33	ff		.
	defb 0ffh		;3e34	ff		.
	defb 0ffh		;3e35	ff		.
	defb 0ffh		;3e36	ff		.
	defb 0ffh		;3e37	ff		.
	defb 0ffh		;3e38	ff		.
	defb 0ffh		;3e39	ff		.
	defb 0ffh		;3e3a	ff		.
	defb 0ffh		;3e3b	ff		.
	defb 0ffh		;3e3c	ff		.
	defb 0ffh		;3e3d	ff		.
	defb 0ffh		;3e3e	ff		.
	defb 0ffh		;3e3f	ff		.
	defb 0ffh		;3e40	ff		.
	defb 0ffh		;3e41	ff		.
	defb 0ffh		;3e42	ff		.
	defb 0ffh		;3e43	ff		.
	defb 0ffh		;3e44	ff		.
	defb 0ffh		;3e45	ff		.
	defb 0ffh		;3e46	ff		.
	defb 0ffh		;3e47	ff		.
	defb 0ffh		;3e48	ff		.
	defb 0ffh		;3e49	ff		.
	defb 0ffh		;3e4a	ff		.
	defb 0ffh		;3e4b	ff		.
	defb 0ffh		;3e4c	ff		.
	defb 0ffh		;3e4d	ff		.
	defb 0ffh		;3e4e	ff		.
	defb 0ffh		;3e4f	ff		.
	defb 0ffh		;3e50	ff		.
	defb 0ffh		;3e51	ff		.
	defb 0ffh		;3e52	ff		.
	defb 0ffh		;3e53	ff		.
	defb 0ffh		;3e54	ff		.
	defb 0ffh		;3e55	ff		.
	defb 0ffh		;3e56	ff		.
	defb 0ffh		;3e57	ff		.
	defb 0ffh		;3e58	ff		.
	defb 0ffh		;3e59	ff		.
	defb 0ffh		;3e5a	ff		.
	defb 0ffh		;3e5b	ff		.
	defb 0ffh		;3e5c	ff		.
	defb 0ffh		;3e5d	ff		.
	defb 0ffh		;3e5e	ff		.
	defb 0ffh		;3e5f	ff		.
	defb 0ffh		;3e60	ff		.
	defb 0ffh		;3e61	ff		.
	defb 0ffh		;3e62	ff		.
	defb 0ffh		;3e63	ff		.
	defb 0ffh		;3e64	ff		.
	defb 0ffh		;3e65	ff		.
	defb 0ffh		;3e66	ff		.
	defb 0ffh		;3e67	ff		.
	defb 0ffh		;3e68	ff		.
	defb 0ffh		;3e69	ff		.
	defb 0ffh		;3e6a	ff		.
	defb 0ffh		;3e6b	ff		.
	defb 0ffh		;3e6c	ff		.
	defb 0ffh		;3e6d	ff		.
	defb 0ffh		;3e6e	ff		.
	defb 0ffh		;3e6f	ff		.
	defb 0ffh		;3e70	ff		.
	defb 0ffh		;3e71	ff		.
	defb 0ffh		;3e72	ff		.
	defb 0ffh		;3e73	ff		.
	defb 0ffh		;3e74	ff		.
	defb 0ffh		;3e75	ff		.
	defb 0ffh		;3e76	ff		.
	defb 0ffh		;3e77	ff		.
	defb 0ffh		;3e78	ff		.
	defb 0ffh		;3e79	ff		.
	defb 0ffh		;3e7a	ff		.
	defb 0ffh		;3e7b	ff		.
	defb 0ffh		;3e7c	ff		.
	defb 0ffh		;3e7d	ff		.
	defb 0ffh		;3e7e	ff		.
	defb 0ffh		;3e7f	ff		.
	defb 0ffh		;3e80	ff		.
	defb 0ffh		;3e81	ff		.
	defb 0ffh		;3e82	ff		.
	defb 0ffh		;3e83	ff		.
	defb 0ffh		;3e84	ff		.
	defb 0ffh		;3e85	ff		.
	defb 0ffh		;3e86	ff		.
	defb 0ffh		;3e87	ff		.
	defb 0ffh		;3e88	ff		.
	defb 0ffh		;3e89	ff		.
	defb 0ffh		;3e8a	ff		.
	defb 0ffh		;3e8b	ff		.
	defb 0ffh		;3e8c	ff		.
	defb 0ffh		;3e8d	ff		.
	defb 0ffh		;3e8e	ff		.
	defb 0ffh		;3e8f	ff		.
	defb 0ffh		;3e90	ff		.
	defb 0ffh		;3e91	ff		.
	defb 0ffh		;3e92	ff		.
	defb 0ffh		;3e93	ff		.
	defb 0ffh		;3e94	ff		.
	defb 0ffh		;3e95	ff		.
	defb 0ffh		;3e96	ff		.
	defb 0ffh		;3e97	ff		.
	defb 0ffh		;3e98	ff		.
	defb 0ffh		;3e99	ff		.
	defb 0ffh		;3e9a	ff		.
	defb 0ffh		;3e9b	ff		.
	defb 0ffh		;3e9c	ff		.
	defb 0ffh		;3e9d	ff		.
	defb 0ffh		;3e9e	ff		.
	defb 0ffh		;3e9f	ff		.
	defb 0ffh		;3ea0	ff		.
	defb 0ffh		;3ea1	ff		.
	defb 0ffh		;3ea2	ff		.
	defb 0ffh		;3ea3	ff		.
	defb 0ffh		;3ea4	ff		.
	defb 0ffh		;3ea5	ff		.
	defb 0ffh		;3ea6	ff		.
	defb 0ffh		;3ea7	ff		.
	defb 0ffh		;3ea8	ff		.
	defb 0ffh		;3ea9	ff		.
	defb 0ffh		;3eaa	ff		.
	defb 0ffh		;3eab	ff		.
	defb 0ffh		;3eac	ff		.
	defb 0ffh		;3ead	ff		.
	defb 0ffh		;3eae	ff		.
	defb 0ffh		;3eaf	ff		.
	defb 0ffh		;3eb0	ff		.
	defb 0ffh		;3eb1	ff		.
	defb 0ffh		;3eb2	ff		.
	defb 0ffh		;3eb3	ff		.
	defb 0ffh		;3eb4	ff		.
	defb 0ffh		;3eb5	ff		.
	defb 0ffh		;3eb6	ff		.
	defb 0ffh		;3eb7	ff		.
	defb 0ffh		;3eb8	ff		.
	defb 0ffh		;3eb9	ff		.
	defb 0ffh		;3eba	ff		.
	defb 0ffh		;3ebb	ff		.
	defb 0ffh		;3ebc	ff		.
	defb 0ffh		;3ebd	ff		.
	defb 0ffh		;3ebe	ff		.
	defb 0ffh		;3ebf	ff		.
	defb 0ffh		;3ec0	ff		.
	defb 0ffh		;3ec1	ff		.
	defb 0ffh		;3ec2	ff		.
	defb 0ffh		;3ec3	ff		.
	defb 0ffh		;3ec4	ff		.
	defb 0ffh		;3ec5	ff		.
	defb 0ffh		;3ec6	ff		.
	defb 0ffh		;3ec7	ff		.
	defb 0ffh		;3ec8	ff		.
	defb 0ffh		;3ec9	ff		.
	defb 0ffh		;3eca	ff		.
	defb 0ffh		;3ecb	ff		.
	defb 0ffh		;3ecc	ff		.
	defb 0ffh		;3ecd	ff		.
	defb 0ffh		;3ece	ff		.
	defb 0ffh		;3ecf	ff		.
	defb 0ffh		;3ed0	ff		.
	defb 0ffh		;3ed1	ff		.
	defb 0ffh		;3ed2	ff		.
	defb 0ffh		;3ed3	ff		.
	defb 0ffh		;3ed4	ff		.
	defb 0ffh		;3ed5	ff		.
	defb 0ffh		;3ed6	ff		.
	defb 0ffh		;3ed7	ff		.
	defb 0ffh		;3ed8	ff		.
	defb 0ffh		;3ed9	ff		.
	defb 0ffh		;3eda	ff		.
	defb 0ffh		;3edb	ff		.
	defb 0ffh		;3edc	ff		.
	defb 0ffh		;3edd	ff		.
	defb 0ffh		;3ede	ff		.
	defb 0ffh		;3edf	ff		.
	defb 0ffh		;3ee0	ff		.
	defb 0ffh		;3ee1	ff		.
	defb 0ffh		;3ee2	ff		.
	defb 0ffh		;3ee3	ff		.
	defb 0ffh		;3ee4	ff		.
	defb 0ffh		;3ee5	ff		.
	defb 0ffh		;3ee6	ff		.
	defb 0ffh		;3ee7	ff		.
	defb 0ffh		;3ee8	ff		.
	defb 0ffh		;3ee9	ff		.
	defb 0ffh		;3eea	ff		.
	defb 0ffh		;3eeb	ff		.
	defb 0ffh		;3eec	ff		.
	defb 0ffh		;3eed	ff		.
	defb 0ffh		;3eee	ff		.
	defb 0ffh		;3eef	ff		.
	defb 0ffh		;3ef0	ff		.
	defb 0ffh		;3ef1	ff		.
	defb 0ffh		;3ef2	ff		.
	defb 0ffh		;3ef3	ff		.
	defb 0ffh		;3ef4	ff		.
	defb 0ffh		;3ef5	ff		.
	defb 0ffh		;3ef6	ff		.
	defb 0ffh		;3ef7	ff		.
	defb 0ffh		;3ef8	ff		.
	defb 0ffh		;3ef9	ff		.
	defb 0ffh		;3efa	ff		.
	defb 0ffh		;3efb	ff		.
	defb 0ffh		;3efc	ff		.
	defb 0ffh		;3efd	ff		.
	defb 0ffh		;3efe	ff		.
	defb 0ffh		;3eff	ff		.
	defb 0ffh		;3f00	ff		.
	defb 0ffh		;3f01	ff		.
	defb 0ffh		;3f02	ff		.
	defb 0ffh		;3f03	ff		.
	defb 0ffh		;3f04	ff		.
	defb 0ffh		;3f05	ff		.
	defb 0ffh		;3f06	ff		.
	defb 0ffh		;3f07	ff		.
	defb 0ffh		;3f08	ff		.
	defb 0ffh		;3f09	ff		.
	defb 0ffh		;3f0a	ff		.
	defb 0ffh		;3f0b	ff		.
	defb 0ffh		;3f0c	ff		.
	defb 0ffh		;3f0d	ff		.
	defb 0ffh		;3f0e	ff		.
	defb 0ffh		;3f0f	ff		.
	defb 0ffh		;3f10	ff		.
	defb 0ffh		;3f11	ff		.
	defb 0ffh		;3f12	ff		.
	defb 0ffh		;3f13	ff		.
	defb 0ffh		;3f14	ff		.
	defb 0ffh		;3f15	ff		.
	defb 0ffh		;3f16	ff		.
	defb 0ffh		;3f17	ff		.
	defb 0ffh		;3f18	ff		.
	defb 0ffh		;3f19	ff		.
	defb 0ffh		;3f1a	ff		.
	defb 0ffh		;3f1b	ff		.
	defb 0ffh		;3f1c	ff		.
	defb 0ffh		;3f1d	ff		.
	defb 0ffh		;3f1e	ff		.
	defb 0ffh		;3f1f	ff		.
	defb 0ffh		;3f20	ff		.
	defb 0ffh		;3f21	ff		.
	defb 0ffh		;3f22	ff		.
	defb 0ffh		;3f23	ff		.
	defb 0ffh		;3f24	ff		.
	defb 0ffh		;3f25	ff		.
	defb 0ffh		;3f26	ff		.
	defb 0ffh		;3f27	ff		.
	defb 0ffh		;3f28	ff		.
	defb 0ffh		;3f29	ff		.
	defb 0ffh		;3f2a	ff		.
	defb 0ffh		;3f2b	ff		.
	defb 0ffh		;3f2c	ff		.
	defb 0ffh		;3f2d	ff		.
	defb 0ffh		;3f2e	ff		.
	defb 0ffh		;3f2f	ff		.
	defb 0ffh		;3f30	ff		.
	defb 0ffh		;3f31	ff		.
	defb 0ffh		;3f32	ff		.
	defb 0ffh		;3f33	ff		.
	defb 0ffh		;3f34	ff		.
	defb 0ffh		;3f35	ff		.
	defb 0ffh		;3f36	ff		.
	defb 0ffh		;3f37	ff		.
	defb 0ffh		;3f38	ff		.
	defb 0ffh		;3f39	ff		.
	defb 0ffh		;3f3a	ff		.
	defb 0ffh		;3f3b	ff		.
	defb 0ffh		;3f3c	ff		.
	defb 0ffh		;3f3d	ff		.
	defb 0ffh		;3f3e	ff		.
	defb 0ffh		;3f3f	ff		.
	defb 0ffh		;3f40	ff		.
	defb 0ffh		;3f41	ff		.
	defb 0ffh		;3f42	ff		.
	defb 0ffh		;3f43	ff		.
	defb 0ffh		;3f44	ff		.
	defb 0ffh		;3f45	ff		.
	defb 0ffh		;3f46	ff		.
	defb 0ffh		;3f47	ff		.
	defb 0ffh		;3f48	ff		.
	defb 0ffh		;3f49	ff		.
	defb 0ffh		;3f4a	ff		.
	defb 0ffh		;3f4b	ff		.
	defb 0ffh		;3f4c	ff		.
	defb 0ffh		;3f4d	ff		.
	defb 0ffh		;3f4e	ff		.
	defb 0ffh		;3f4f	ff		.
	defb 0ffh		;3f50	ff		.
	defb 0ffh		;3f51	ff		.
	defb 0ffh		;3f52	ff		.
	defb 0ffh		;3f53	ff		.
	defb 0ffh		;3f54	ff		.
	defb 0ffh		;3f55	ff		.
	defb 0ffh		;3f56	ff		.
	defb 0ffh		;3f57	ff		.
	defb 0ffh		;3f58	ff		.
	defb 0ffh		;3f59	ff		.
	defb 0ffh		;3f5a	ff		.
	defb 0ffh		;3f5b	ff		.
	defb 0ffh		;3f5c	ff		.
	defb 0ffh		;3f5d	ff		.
	defb 0ffh		;3f5e	ff		.
	defb 0ffh		;3f5f	ff		.
	defb 0ffh		;3f60	ff		.
	defb 0ffh		;3f61	ff		.
	defb 0ffh		;3f62	ff		.
	defb 0ffh		;3f63	ff		.
	defb 0ffh		;3f64	ff		.
	defb 0ffh		;3f65	ff		.
	defb 0ffh		;3f66	ff		.
	defb 0ffh		;3f67	ff		.
	defb 0ffh		;3f68	ff		.
	defb 0ffh		;3f69	ff		.
	defb 0ffh		;3f6a	ff		.
	defb 0ffh		;3f6b	ff		.
	defb 0ffh		;3f6c	ff		.
	defb 0ffh		;3f6d	ff		.
	defb 0ffh		;3f6e	ff		.
	defb 0ffh		;3f6f	ff		.
	defb 0ffh		;3f70	ff		.
	defb 0ffh		;3f71	ff		.
	defb 0ffh		;3f72	ff		.
	defb 0ffh		;3f73	ff		.
	defb 0ffh		;3f74	ff		.
	defb 0ffh		;3f75	ff		.
	defb 0ffh		;3f76	ff		.
	defb 0ffh		;3f77	ff		.
	defb 0ffh		;3f78	ff		.
	defb 0ffh		;3f79	ff		.
	defb 0ffh		;3f7a	ff		.
	defb 0ffh		;3f7b	ff		.
	defb 0ffh		;3f7c	ff		.
	defb 0ffh		;3f7d	ff		.
	defb 0ffh		;3f7e	ff		.
	defb 0ffh		;3f7f	ff		.
	defb 0ffh		;3f80	ff		.
	defb 0ffh		;3f81	ff		.
	defb 0ffh		;3f82	ff		.
	defb 0ffh		;3f83	ff		.
	defb 0ffh		;3f84	ff		.
	defb 0ffh		;3f85	ff		.
	defb 0ffh		;3f86	ff		.
	defb 0ffh		;3f87	ff		.
	defb 0ffh		;3f88	ff		.
	defb 0ffh		;3f89	ff		.
	defb 0ffh		;3f8a	ff		.
	defb 0ffh		;3f8b	ff		.
	defb 0ffh		;3f8c	ff		.
	defb 0ffh		;3f8d	ff		.
	defb 0ffh		;3f8e	ff		.
	defb 0ffh		;3f8f	ff		.
	defb 0ffh		;3f90	ff		.
	defb 0ffh		;3f91	ff		.
	defb 0ffh		;3f92	ff		.
	defb 0ffh		;3f93	ff		.
	defb 0ffh		;3f94	ff		.
	defb 0ffh		;3f95	ff		.
	defb 0ffh		;3f96	ff		.
	defb 0ffh		;3f97	ff		.
	defb 0ffh		;3f98	ff		.
	defb 0ffh		;3f99	ff		.
	defb 0ffh		;3f9a	ff		.
	defb 0ffh		;3f9b	ff		.
	defb 0ffh		;3f9c	ff		.
	defb 0ffh		;3f9d	ff		.
	defb 0ffh		;3f9e	ff		.
	defb 0ffh		;3f9f	ff		.
	defb 0ffh		;3fa0	ff		.
	defb 0ffh		;3fa1	ff		.
	defb 0ffh		;3fa2	ff		.
	defb 0ffh		;3fa3	ff		.
	defb 0ffh		;3fa4	ff		.
	defb 0ffh		;3fa5	ff		.
	defb 0ffh		;3fa6	ff		.
	defb 0ffh		;3fa7	ff		.
	defb 0ffh		;3fa8	ff		.
	defb 0ffh		;3fa9	ff		.
	defb 0ffh		;3faa	ff		.
	defb 0ffh		;3fab	ff		.
	defb 0ffh		;3fac	ff		.
	defb 0ffh		;3fad	ff		.
	defb 0ffh		;3fae	ff		.
	defb 0ffh		;3faf	ff		.
	defb 0ffh		;3fb0	ff		.
	defb 0ffh		;3fb1	ff		.
	defb 0ffh		;3fb2	ff		.
	defb 0ffh		;3fb3	ff		.
	defb 0ffh		;3fb4	ff		.
	defb 0ffh		;3fb5	ff		.
	defb 0ffh		;3fb6	ff		.
	defb 0ffh		;3fb7	ff		.
	defb 0ffh		;3fb8	ff		.
	defb 0ffh		;3fb9	ff		.
	defb 0ffh		;3fba	ff		.
	defb 0ffh		;3fbb	ff		.
	defb 0ffh		;3fbc	ff		.
	defb 0ffh		;3fbd	ff		.
	defb 0ffh		;3fbe	ff		.
	defb 0ffh		;3fbf	ff		.
	defb 0ffh		;3fc0	ff		.
	defb 0ffh		;3fc1	ff		.
	defb 0ffh		;3fc2	ff		.
	defb 0ffh		;3fc3	ff		.
	defb 0ffh		;3fc4	ff		.
	defb 0ffh		;3fc5	ff		.
	defb 0ffh		;3fc6	ff		.
	defb 0ffh		;3fc7	ff		.
	defb 0ffh		;3fc8	ff		.
	defb 0ffh		;3fc9	ff		.
	defb 0ffh		;3fca	ff		.
	defb 0ffh		;3fcb	ff		.
	defb 0ffh		;3fcc	ff		.
	defb 0ffh		;3fcd	ff		.
	defb 0ffh		;3fce	ff		.
	defb 0ffh		;3fcf	ff		.
;---------------------------------------------------------------------------
; Page stubs (see the same addresses in ROM page 8). OUT (#7C),0 selects
; ROM page 8 (bit 0 = 0), OUT (#7C),1 page 0; OUT (#3C) maps RAM instead
; of ROM into window 0. The instruction after each OUT is fetched from
; whatever the window shows next.
;---------------------------------------------------------------------------
ToPage8Call:
				; = FN1_RET (src: EXP.asm:1812, BIOS-TT 0271ac3)
	push af			;3fd0	f5		.
	ld a,000h		;3fd1	3e 00		> .
	out (07ch),a		;3fd3	d3 7c		. |
	pop af			;3fd5	f1		.
	ret			;3fd6	c9		.

; BLOCK 'Fill3FD7' (start 0x3fd7 end 0x3fd8)
Fill3FD7:
	defb 0ffh		;3fd7	ff		.
l3fd8h:
	jp FromRamCall		;3fd8	c3 07 01	. . .

; BLOCK 'Fill3FDB' (start 0x3fdb end 0x3fe0)
Fill3FDB:
	defb 0ffh		;3fdb	ff		.
	defb 0ffh		;3fdc	ff		.
	defb 0ffh		;3fdd	ff		.
	defb 0ffh		;3fde	ff		.
	defb 0ffh		;3fdf	ff		.
l3fe0h:
	ld a,000h		;3fe0	3e 00		> .
	out (03ch),a		;3fe2	d3 3c		. <
	jp RomStart		;3fe4	c3 00 00	. . .

; BLOCK 'Fill3FE7' (start 0x3fe7 end 0x3fe8)
Fill3FE7:
	defb 0ffh		;3fe7	ff		.
BackToPage8:
	push af			;3fe8	f5		.
	ld a,000h		;3fe9	3e 00		> .
	out (07ch),a		;3feb	d3 7c		. |
	jp FromPage8Call		;3fed	c3 00 01	. . .
l3ff0h:
	push af			;3ff0	f5		.
	di			;3ff1	f3		.
	ld a,000h		;3ff2	3e 00		> .
	out (03ch),a		;3ff4	d3 3c		. <
	jr l3fd8h		;3ff6	18 e0		. .
EXP_FNS_RET:
				; = EXP_FNS_RET (src: EXP.asm:1862, BIOS-TT 0271ac3)
	push af			;3ff8	f5		.
	ld a,000h		;3ff9	3e 00		> .
	out (03ch),a		;3ffb	d3 3c		. <
	jp RomStart		;3ffd	c3 00 00	. . .
