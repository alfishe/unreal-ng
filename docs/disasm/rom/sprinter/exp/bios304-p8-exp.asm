;===========================================================================
; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)
; ROM page 8: the BIOS proper ("EXP"), seen by the CPU at #0000-#3FFF
; Image: data/rom/sprinter/sp2k-3.04.rom (CRC32 1729cb5c), file offset #20000
;===========================================================================
;
; What this page does (details: ../README.md and exp/README.md):
;   * cold start after the PLD loader: power-on self test (POST) with
;     progress codes on the PIO, the port table (DCP) written into RAM
;     page #40, ports and devices set up, then SETUP copied to #8000 and
;     started;
;   * RST #18 (and RST #08 from RAM): the BIOS function call, function
;     number in C. #80-#FF through the table at #3000, #40-#48 (old HDD
;     API) here, #50-#5F (disk API) in ROM page 0 via the stub at #3FE8;
;   * the screen/text/window functions, the memory manager, CMOS, the
;     8x8 font (#2800) and the packed port table (#1400).
;
; Window 0 shows ROM page 8 while OUT (#7C) bit 0 = 0 and ROM page 0
; when it is 1. Both pages carry stubs at the same addresses (#3FD0-#3FFF)
; so that code switching the page continues in the other page.
;
; No public source of 3.04 exists. Labels come from three places:
;   * hand names (this file, dict_exp.py),
;   * "FnNN_NAME": the handler of BIOS function NN, read from the
;     dispatch table; NAME from Shared_Includes BIOS_equ.inc,
;   * names carried by byte pattern from the closest source, Tolik-Trek
;     Sprinter-BIOS 0271ac3 (2023) bios/exp/*.asm. Each is shown as
;     "; = NAME (src: FILE:LINE)"; "~" = matched by a short unique window
;     only (less certain). Unnamed lXXXXh code has no counterpart there.
;
; Regenerate and verify: see scripts/README.md (gen.py, checkcov.py).
;===========================================================================

	org 00000h

TABLE_X_v3:	equ 0x00a3
TABLE_X_v4:	equ 0x00a4
;---------------------------------------------------------------------------
; Reset vector. The PLD loader (ROM page #C) ends with a CPU reset once the
; PLD is configured; the CPU then starts here with ROM page 8 in window 0.
;---------------------------------------------------------------------------

Reset:
	jp ColdStart		;0000	c3 00 01	. . .
;---------------------------------------------------------------------------
; RomNumber: 3 bytes read by programs that identify the board. Byte #0005
; (BoardId) is #07 in this image and #24 in the ZXMAK2 copy SP_304.BIN -
; the only difference in this page between the two known 3.04 dumps.
;---------------------------------------------------------------------------
RomNumber:

; BLOCK 'Data0003' (start 0x0003 end 0x0008)
	defb 000h		;0003	00		.
l0004h:
	defb 000h		;0004	00		.
BoardId:
	defb 007h		;0005	07		.
	defb 000h		;0006	00		.
l0007h:
	defb 000h		;0007	00		.
;---------------------------------------------------------------------------
; RST #08 from RAM, ROM half. A program running with RAM in window 0
; (DSS) has its own stub at #0008: PUSH AF / LD A,0 / OUT (#7C),A. That OUT
; maps this ROM page into window 0, so the CPU fetches #000D from here:
; POP AF, JR Rst18Return -> CALL BiosDispatch -> JR #0008. Now this
; copy runs: OUT (#3C),0 switches window 0 back to RAM and the program's
; own #000D (POP AF / RET) returns to the caller. Same page-switch trick
; as the stubs at #3FD0.
;---------------------------------------------------------------------------
Rst08FromRam:
				; = EXP_FNS_2_RET (src: EXP.asm:31, BIOS-TT 0271ac3)
	push af			;0008	f5		.
	ld a,000h		;0009	3e 00		> .
	out (03ch),a		;000b	d3 3c		. <
	pop af			;000d	f1		.
l000eh:
	jr Rst18Return		;000e	18 0b		. .
RST_10:

; BLOCK 'Zero0010' (start 0x0010 end 0x0018)
				; = RST_10 (src: EXP.asm:41, BIOS-TT 0271ac3)
	defb 000h		;0010	00		.
	defb 000h		;0011	00		.
	defb 000h		;0012	00		.
	defb 000h		;0013	00		.
	defb 000h		;0014	00		.
	defb 000h		;0015	00		.
	defb 000h		;0016	00		.
	defb 000h		;0017	00		.
;---------------------------------------------------------------------------
; RST #18: the BIOS call from ROM code (SETUP, page 0). C = function number.
; Example: LD D,#10 : LD C,#F6 : RST #18 reads CMOS register #10 into A.
;---------------------------------------------------------------------------
Rst18:
	jp BiosDispatch		;0018	c3 0a 31	. . 1
Rst18Return:
				; = RST_18_1 (src: EXP.asm:49, BIOS-TT 0271ac3)
	call BiosDispatch	;001b	cd 0a 31	. . 1
	jr Rst08FromRam		;001e	18 e8		. .
RST_20:

; BLOCK 'Zero0020' (start 0x0020 end 0x0038)
				; = RST_20 (src: EXP.asm:57, BIOS-TT 0271ac3)
	defb 000h		;0020	00		.
	defb 000h		;0021	00		.
	defb 000h		;0022	00		.
	defb 000h		;0023	00		.
	defb 000h		;0024	00		.
	defb 000h		;0025	00		.
	defb 000h		;0026	00		.
	defb 000h		;0027	00		.
RST_28:
				; = RST_28 (src: EXP.asm:62, BIOS-TT 0271ac3)
	defb 000h		;0028	00		.
	defb 000h		;0029	00		.
	defb 000h		;002a	00		.
l002bh:
	defb 000h		;002b	00		.
	defb 000h		;002c	00		.
	defb 000h		;002d	00		.
	defb 000h		;002e	00		.
	defb 000h		;002f	00		.
RST_30:
				; = RST_30 (src: EXP.asm:67, BIOS-TT 0271ac3)
	defb 000h		;0030	00		.
	defb 000h		;0031	00		.
	defb 000h		;0032	00		.
	defb 000h		;0033	00		.
	defb 000h		;0034	00		.
	defb 000h		;0035	00		.
	defb 000h		;0036	00		.
	defb 000h		;0037	00		.
IntVector:
				; = RST38 (src: EXP.asm:73, BIOS-TT 0271ac3)
	ei			;0038	fb		.
	reti			;0039	ed 4d		. M

; BLOCK 'Zero003B' (start 0x003b end 0x0066)
Zero003B:
	defb 000h		;003b	00		.
	defb 000h		;003c	00		.
	defb 000h		;003d	00		.
	defb 000h		;003e	00		.
	defb 000h		;003f	00		.
	defb 000h		;0040	00		.
	defb 000h		;0041	00		.
	defb 000h		;0042	00		.
	defb 000h		;0043	00		.
	defb 000h		;0044	00		.
	defb 000h		;0045	00		.
	defb 000h		;0046	00		.
	defb 000h		;0047	00		.
	defb 000h		;0048	00		.
	defb 000h		;0049	00		.
	defb 000h		;004a	00		.
	defb 000h		;004b	00		.
l004ch:
	defb 000h		;004c	00		.
	defb 000h		;004d	00		.
l004eh:
	defb 000h		;004e	00		.
l004fh:
	defb 000h		;004f	00		.
l0050h:
	defb 000h		;0050	00		.
l0051h:
	defb 000h		;0051	00		.
	defb 000h		;0052	00		.
l0053h:
	defb 000h		;0053	00		.
l0054h:
	defb 000h		;0054	00		.
l0055h:
	defb 000h		;0055	00		.
	defb 000h		;0056	00		.
	defb 000h		;0057	00		.
	defb 000h		;0058	00		.
	defb 000h		;0059	00		.
	defb 000h		;005a	00		.
	defb 000h		;005b	00		.
	defb 000h		;005c	00		.
	defb 000h		;005d	00		.
	defb 000h		;005e	00		.
	defb 000h		;005f	00		.
	defb 000h		;0060	00		.
	defb 000h		;0061	00		.
	defb 000h		;0062	00		.
	defb 000h		;0063	00		.
	defb 000h		;0064	00		.
	defb 000h		;0065	00		.
NmiVector:
	retn			;0066	ed 45		. E

; BLOCK 'Data0068' (start 0x0068 end 0x00a1)
Data0068:
	defb 000h		;0068	00		.
	defb 000h		;0069	00		.
	defb 000h		;006a	00		.
	defb 000h		;006b	00		.
	defb 000h		;006c	00		.
	defb 000h		;006d	00		.
	defb 000h		;006e	00		.
	defb 000h		;006f	00		.
	defb 000h		;0070	00		.
	defb 000h		;0071	00		.
	defb 000h		;0072	00		.
	defb 000h		;0073	00		.
	defb 000h		;0074	00		.
	defb 000h		;0075	00		.
	defb 000h		;0076	00		.
	defb 000h		;0077	00		.
	defb 000h		;0078	00		.
	defb 000h		;0079	00		.
	defb 000h		;007a	00		.
	defb 000h		;007b	00		.
	defb 000h		;007c	00		.
	defb 000h		;007d	00		.
	defb 000h		;007e	00		.
	defb 000h		;007f	00		.
	defb 000h		;0080	00		.
	defb 000h		;0081	00		.
	defb 000h		;0082	00		.
	defb 000h		;0083	00		.
	defb 000h		;0084	00		.
	defb 000h		;0085	00		.
	defb 000h		;0086	00		.
	defb 000h		;0087	00		.
	defb 000h		;0088	00		.
	defb 000h		;0089	00		.
	defb 000h		;008a	00		.
	defb 000h		;008b	00		.
	defb 000h		;008c	00		.
	defb 000h		;008d	00		.
	defb 000h		;008e	00		.
	defb 000h		;008f	00		.
	defb 000h		;0090	00		.
	defb 000h		;0091	00		.
	defb 000h		;0092	00		.
	defb 000h		;0093	00		.
	defb 000h		;0094	00		.
	defb 000h		;0095	00		.
	defb 000h		;0096	00		.
	defb 000h		;0097	00		.
	defb 000h		;0098	00		.
	defb 000h		;0099	00		.
	defb 000h		;009a	00		.
	defb 000h		;009b	00		.
	defb 000h		;009c	00		.
l009dh:
	defb 000h		;009d	00		.
	defb 000h		;009e	00		.
	defb 000h		;009f	00		.
PostCodeTable:
				; = TABLE_X (src: EXP.asm:87, BIOS-TT 0271ac3)
				; = TABLE_X.v0 (src: EXP.asm:88, BIOS-TT 0271ac3)
	defb 028h		;00a0	28		(
TABLE_X_v1:
				; = TABLE_X.v1 (src: EXP.asm:89, BIOS-TT 0271ac3)
	cp l			;00a1	bd		.
TABLE_X_v2:
				; = TABLE_X.v2 (src: EXP.asm:90, BIOS-TT 0271ac3)
	ld (0a534h),a		;00a2	32 34 a5	2 4 .
TABLE_X_v5:
				; = TABLE_X.v5 (src: EXP.asm:93, BIOS-TT 0271ac3)
	ld h,h			;00a5	64		d
TABLE_X_v6:
				; = TABLE_X.v6 (src: EXP.asm:94, BIOS-TT 0271ac3)
	ld h,b			;00a6	60		`
TABLE_X_v7:
				; = TABLE_X.v7 (src: EXP.asm:95, BIOS-TT 0271ac3)
	dec a			;00a7	3d		=
TABLE_X_v8:
				; = TABLE_X.v8 (src: EXP.asm:96, BIOS-TT 0271ac3)
	jr nz,l00ceh		;00a8	20 24		  $
	ld hl,06ae0h		;00aa	21 e0 6a	! . j
	or b			;00ad	b0		.
	ld h,d			;00ae	62		b
	ld h,e			;00af	63		c
	nop			;00b0	00		.
	nop			;00b1	00		.
	nop			;00b2	00		.
	nop			;00b3	00		.
	nop			;00b4	00		.
	nop			;00b5	00		.
	nop			;00b6	00		.
	nop			;00b7	00		.
	nop			;00b8	00		.
	nop			;00b9	00		.
	nop			;00ba	00		.
	nop			;00bb	00		.
	nop			;00bc	00		.
	nop			;00bd	00		.
	nop			;00be	00		.
	nop			;00bf	00		.
BiosVersionText:
	inc b			;00c0	04		.
	inc bc			;00c1	03		.
l00c2h:
	ld (07053h),hl		;00c2	22 53 70	" S p
	ld (hl),d		;00c5	72		r
	ld l,c			;00c6	69		i
	ld l,(hl)		;00c7	6e		n
	ld (hl),h		;00c8	74		t
	ld h,l			;00c9	65		e
	ld (hl),d		;00ca	72		r
	jr nz,l010fh		;00cb	20 42		  B
	ld c,c			;00cd	49		I
l00ceh:
	ld c,a			;00ce	4f		O
	ld d,e			;00cf	53		S
	ld a,(07620h)		;00d0	3a 20 76	:   v
	ld h,l			;00d3	65		e
	ld (hl),d		;00d4	72		r
	jr nz,$+53		;00d5	20 33		  3
	ld l,030h		;00d7	2e 30		. 0
	inc (hl)		;00d9	34		4
	nop			;00da	00		.
	ld d,e			;00db	53		S
	ld (hl),b		;00dc	70		p
	ld (hl),d		;00dd	72		r
	ld l,c			;00de	69		i
	ld l,(hl)		;00df	6e		n
	ld (hl),h		;00e0	74		t
	ld h,l			;00e1	65		e
	ld (hl),d		;00e2	72		r
	nop			;00e3	00		.
	nop			;00e4	00		.
	nop			;00e5	00		.
	nop			;00e6	00		.
	nop			;00e7	00		.
	nop			;00e8	00		.
	nop			;00e9	00		.
	nop			;00ea	00		.
	nop			;00eb	00		.
	nop			;00ec	00		.
	nop			;00ed	00		.
	nop			;00ee	00		.
	nop			;00ef	00		.
	nop			;00f0	00		.
	nop			;00f1	00		.
	nop			;00f2	00		.
	nop			;00f3	00		.
	nop			;00f4	00		.
	nop			;00f5	00		.
	nop			;00f6	00		.
	nop			;00f7	00		.
	nop			;00f8	00		.
	nop			;00f9	00		.
	nop			;00fa	00		.
	nop			;00fb	00		.
	nop			;00fc	00		.
	nop			;00fd	00		.
	nop			;00fe	00		.
l00ffh:
	nop			;00ff	00		.
;---------------------------------------------------------------------------
; Cold start. If the 12 bytes at #FFE0 equal RestartSignature, a program
; asked for a "restart through RAM": jump there (it wipes itself and falls
; through to a reset). Otherwise continue at InitCpuPorts.
;---------------------------------------------------------------------------
ColdStart:
	di			;0100	f3		.
	im 1			;0101	ed 56		. V
	ld hl,0ffe0h		;0103	21 e0 ff	! . .
	ld de,RestartSignature	;0106	11 16 01	. . .
	ld b,00ch		;0109	06 0c		. .
l010bh:
	ld a,(de)		;010b	1a		.
	cp (hl)			;010c	be		.
	jr nz,InitCpuPorts	;010d	20 42		  B
l010fh:
	inc hl			;010f	23		#
	inc de			;0110	13		.
	djnz l010bh		;0111	10 f8		. .
	jp 0ffe0h		;0113	c3 e0 ff	. . .
RestartSignature:
				; = ~RESTARTS_PROG (src: EXP.asm:1435, BIOS-TT 0271ac3)
	ld hl,0ffe0h		;0116	21 e0 ff	! . .
	ld b,010h		;0119	06 10		. .
l011bh:
	ld (hl),000h		;011b	36 00		6 .
	inc hl			;011d	23		#
	djnz l011bh		;011e	10 fb		. .
	nop			;0120	00		.
	nop			;0121	00		.
	ld a,001h		;0122	3e 01		> .
	out (0e2h),a		;0124	d3 e2		. .
;---------------------------------------------------------------------------
; Copied to #C000 and called (RunSetup). OUT (#7C),5 = ROM page 0 in window 0
; (bit 0) with port map 0 enabled (bit 2); LDIR copies page 0 #1000-#3FFF to
; RAM #8000; OUT (#7C),4 = back to page 8; JP #8005 enters the SETUP stub
; (the "SETUP (C) 2001 PETERS PLUS" header, then the Hrust depacker that
; unpacks SETUP to #8000). A' = 1 when SPACE was held (port #7FFE bit 0).
;---------------------------------------------------------------------------
StartSetupInRam:
	ld a,005h		;0126	3e 05		> .
	out (07ch),a		;0128	d3 7c		. |
	ld hl,EMM_GetMem_noRAM	;012a	21 00 10	! . .
	ld de,08000h		;012d	11 00 80	. . .
	ld bc,FnTable80	;0130	01 00 30	. . 0
	ldir			;0133	ed b0		. .
	ld a,004h		;0135	3e 04		> .
	out (07ch),a		;0137	d3 7c		. |
	ld hl,08005h		;0139	21 05 80	! . .
	ld a,0feh		;013c	3e fe		> .
	ld bc,l204eh		;013e	01 4e 20	. N  
	out (c),a		;0141	ed 79		. y
	ld a,07fh		;0143	3e 7f		> .
	in a,(0feh)		;0145	db fe		. .
	cpl			;0147	2f		/
	and 001h		;0148	e6 01		. .
	ex af,af'		;014a	08		.
	ld a,0ffh		;014b	3e ff		> .
	out (c),a		;014d	ed 79		. y
	ex af,af'		;014f	08		.
l0150h:
	jp (hl)			;0150	e9		.
;---------------------------------------------------------------------------
; Z84C15 on-chip devices: SIO A (#19) and PIO (#1C/#1D) set up, system
; control registers #EE/#EF (wait states, chip selects), border black.
; From here on the PIO port A (#1C) shows POST progress codes from
; PostCodeTable (#00A0: a 7-segment-style pattern per step).
;---------------------------------------------------------------------------
InitCpuPorts:
	ld sp,0bffeh		;0151	31 fe bf	1 . .
l0154h:
	ld a,005h		;0154	3e 05		> .
	out (019h),a		;0156	d3 19		. .
	ld a,062h		;0158	3e 62		> b
	out (019h),a		;015a	d3 19		. .
	ld a,0cfh		;015c	3e cf		> .
	out (01dh),a		;015e	d3 1d		. .
	xor a			;0160	af		.
	out (01dh),a		;0161	d3 1d		. .
	out (01ch),a		;0163	d3 1c		. .
	ld bc,0ffeeh		;0165	01 ee ff	. . .
	xor a			;0168	af		.
	out (c),a		;0169	ed 79		. y
	inc c			;016b	0c		.
	out (c),a		;016c	ed 79		. y
	dec c			;016e	0d		.
	ld a,003h		;016f	3e 03		> .
	out (c),a		;0171	ed 79		. y
	inc c			;0173	0c		.
	xor a			;0174	af		.
	ld a,001h		;0175	3e 01		> .
	out (c),a		;0177	ed 79		. y
	xor a			;0179	af		.
	out (0feh),a		;017a	d3 fe		. .
RET_FROM_BIOS_TO_BASIC48:
				; = RET_FROM_BIOS_TO_BASIC48 (src: EXP.asm:262, BIOS-TT 0271ac3)
				; = START (src: EXP.asm:272, BIOS-TT 0271ac3)
	ld a,(PostCodeTable)	;017c	3a a0 00	: . .
	out (01ch),a		;017f	d3 1c		. .
POST_1_RAM_BUS:
				; = POST_1_RAM_BUS (src: EXP.asm:282, BIOS-TT 0271ac3)
	ld bc,Reset		;0181	01 00 00	. . .
	ld hl,0c000h		;0184	21 00 c0	! . .
	ld de,l0055h		;0187	11 55 00	. U .
POST_1_RAM_BUS_loop:
				; = POST_1_RAM_BUS.loop (src: EXP.asm:287, BIOS-TT 0271ac3)
	ld (hl),e		;018a	73		s
	inc l			;018b	2c		,
	ld (hl),d		;018c	72		r
	dec l			;018d	2d		-
	ld a,(hl)		;018e	7e		~
	xor e			;018f	ab		.
	or c			;0190	b1		.
	ld c,a			;0191	4f		O
	inc l			;0192	2c		,
	ld a,(hl)		;0193	7e		~
	xor d			;0194	aa		.
	or b			;0195	b0		.
	ld b,a			;0196	47		G
	dec l			;0197	2d		-
	dec e			;0198	1d		.
	inc d			;0199	14		.
	jr nz,POST_1_RAM_BUS_loop	;019a	20 ee		  .
	ld a,e			;019c	7b		{
	cpl			;019d	2f		/
	ld e,a			;019e	5f		_
	inc l			;019f	2c		,
	inc l			;01a0	2c		,
	bit 4,l			;01a1	cb 65		. e
	jr z,POST_1_RAM_BUS_loop	;01a3	28 e5		( .
	ld a,b			;01a5	78		x
	or c			;01a6	b1		.
	jr z,POST_1_OK		;01a7	28 21		( !
POST_1_RAM_BUS_error:
				; = POST_1_RAM_BUS.error (src: EXP.asm:319, BIOS-TT 0271ac3)
	ld hl,PostCodeTable	;01a9	21 a0 00	! . .
POST_1_RAM_BUS_ERB_2:
				; = POST_1_RAM_BUS.ERB_2 (src: EXP.asm:321, BIOS-TT 0271ac3)
	bit 0,c			;01ac	cb 41		. A
	jr z,POST_1_RAM_BUS_ERB_1	;01ae	28 0d		( .
	ld a,(hl)		;01b0	7e		~
	and 0dfh		;01b1	e6 df		. .
	out (01ch),a		;01b3	d3 1c		. .
	ld de,Reset		;01b5	11 00 00	. . .
POST_1_RAM_BUS_pause:
				; = POST_1_RAM_BUS.pause (src: EXP.asm:332, BIOS-TT 0271ac3)
	dec de			;01b8	1b		.
	ld a,d			;01b9	7a		z
	or e			;01ba	b3		.
	jr nz,POST_1_RAM_BUS_pause	;01bb	20 fb		  .
POST_1_RAM_BUS_ERB_1:
				; = POST_1_RAM_BUS.ERB_1 (src: EXP.asm:339, BIOS-TT 0271ac3)
	ld a,c			;01bd	79		y
	rra			;01be	1f		.
	rr b			;01bf	cb 18		. .
	rr c			;01c1	cb 19		. .
	inc l			;01c3	2c		,
	ld a,l			;01c4	7d		}
	and 0afh		;01c5	e6 af		. .
	ld l,a			;01c7	6f		o
	jr POST_1_RAM_BUS_ERB_2	;01c8	18 e2		. .
POST_1_OK:
				; = POST_1_OK (src: EXP.asm:355, BIOS-TT 0271ac3)
	ld a,(TABLE_X_v1)	;01ca	3a a1 00	: . .
	out (01ch),a		;01cd	d3 1c		. .
POST_2_ADRESS_BUS:
				; = POST_2_ADRESS_BUS (src: EXP.asm:361, BIOS-TT 0271ac3)
	ld hl,0c000h		;01cf	21 00 c0	! . .
	ld de,Reset		;01d2	11 00 00	. . .
POST_2_ADRESS_BUS_fill_mem:
				; = POST_2_ADRESS_BUS.fill_mem (src: EXP.asm:364, BIOS-TT 0271ac3)
	ld (hl),e		;01d5	73		s
	inc l			;01d6	2c		,
	ld (hl),d		;01d7	72		r
	inc hl			;01d8	23		#
	inc de			;01d9	13		.
	inc de			;01da	13		.
	bit 7,h			;01db	cb 7c		. |
	jr nz,POST_2_ADRESS_BUS_fill_mem	;01dd	20 f6		  .
	dec hl			;01df	2b		+
	dec de			;01e0	1b		.
	dec de			;01e1	1b		.
POST_2_ADRESS_BUS_check_mem:
				; = POST_2_ADRESS_BUS.check_mem (src: EXP.asm:377, BIOS-TT 0271ac3)
	ld a,(hl)		;01e2	7e		~
	cp d			;01e3	ba		.
	jr nz,POST_2_ADRESS_BUS_error	;01e4	20 0f		  .
	dec hl			;01e6	2b		+
	ld a,(hl)		;01e7	7e		~
	cp e			;01e8	bb		.
	jr nz,POST_2_ADRESS_BUS_error	;01e9	20 0a		  .
	dec hl			;01eb	2b		+
	dec de			;01ec	1b		.
	dec de			;01ed	1b		.
	ld a,h			;01ee	7c		|
	cp 0bfh			;01ef	fe bf		. .
	jr nz,POST_2_ADRESS_BUS_check_mem	;01f1	20 ef		  .
	jr POST_2_OK		;01f3	18 53		. S
POST_2_ADRESS_BUS_error:
				; = POST_2_ADRESS_BUS.error (src: EXP.asm:394, BIOS-TT 0271ac3)
				; = POST_2_ADRESS_BUS.TSAB_4 (src: EXP.asm:395, BIOS-TT 0271ac3)
	ld c,d			;01f5	4a		J
	ld b,0dfh		;01f6	06 df		. .
	ld ix,POST_2_ADRESS_BUS_TSAB_3	;01f8	dd 21 fe 01	. ! . .
	jr OUT_C_BYTE		;01fc	18 09		. .
POST_2_ADRESS_BUS_TSAB_3:
				; = POST_2_ADRESS_BUS.TSAB_3 (src: EXP.asm:400, BIOS-TT 0271ac3)
	ld c,e			;01fe	4b		K
	ld b,0ffh		;01ff	06 ff		. .
	ld ix,POST_2_ADRESS_BUS_error	;0201	dd 21 f5 01	. ! . .
	jr OUT_C_BYTE		;0205	18 00		. .
OUT_C_BYTE:
				; = OUT_C_BYTE (src: EXP.asm:408, BIOS-TT 0271ac3)
	ld a,c			;0207	79		y
	rrca			;0208	0f		.
	rrca			;0209	0f		.
	rrca			;020a	0f		.
	rrca			;020b	0f		.
	and 00fh		;020c	e6 0f		. .
	or 0a0h			;020e	f6 a0		. .
	ld l,a			;0210	6f		o
	ld h,000h		;0211	26 00		& .
	ld a,(hl)		;0213	7e		~
	and b			;0214	a0		.
	out (01ch),a		;0215	d3 1c		. .
	exx			;0217	d9		.
	ld de,Reset		;0218	11 00 00	. . .
OUT_C_BYTE_LOOP_WTT2:
				; = OUT_C_BYTE.LOOP_WTT2 (src: EXP.asm:426, BIOS-TT 0271ac3)
	dec de			;021b	1b		.
	ld a,d			;021c	7a		z
	or e			;021d	b3		.
	jr nz,OUT_C_BYTE_LOOP_WTT2	;021e	20 fb		  .
	ld a,0ffh		;0220	3e ff		> .
	out (01ch),a		;0222	d3 1c		. .
OUT_C_BYTE_LOOP_WTT21:
				; = OUT_C_BYTE.LOOP_WTT21 (src: EXP.asm:433, BIOS-TT 0271ac3)
	dec de			;0224	1b		.
	ld a,d			;0225	7a		z
	or e			;0226	b3		.
	jr nz,OUT_C_BYTE_LOOP_WTT21	;0227	20 fb		  .
	exx			;0229	d9		.
	ld a,c			;022a	79		y
	and 00fh		;022b	e6 0f		. .
	or 0a0h			;022d	f6 a0		. .
	ld l,a			;022f	6f		o
	ld a,(hl)		;0230	7e		~
	out (01ch),a		;0231	d3 1c		. .
	exx			;0233	d9		.
	ld de,Reset		;0234	11 00 00	. . .
OUT_C_BYTE_LOOP_WTT3:
				; = OUT_C_BYTE.LOOP_WTT3 (src: EXP.asm:449, BIOS-TT 0271ac3)
	dec de			;0237	1b		.
	ld a,d			;0238	7a		z
	or e			;0239	b3		.
	jr nz,OUT_C_BYTE_LOOP_WTT3	;023a	20 fb		  .
	ld a,0ffh		;023c	3e ff		> .
	out (01ch),a		;023e	d3 1c		. .
OUT_C_BYTE_LOOP_WTT31:
				; = OUT_C_BYTE.LOOP_WTT31 (src: EXP.asm:456, BIOS-TT 0271ac3)
	dec de			;0240	1b		.
	ld a,d			;0241	7a		z
	or e			;0242	b3		.
	jr nz,OUT_C_BYTE_LOOP_WTT31	;0243	20 fb		  .
	exx			;0245	d9		.
	jp (ix)			;0246	dd e9		. .
POST_2_OK:
				; = POST_2_OK (src: EXP.asm:469, BIOS-TT 0271ac3)
	ld a,(TABLE_X_v2)	;0248	3a a2 00	: . .
	out (01ch),a		;024b	d3 1c		. .
;---------------------------------------------------------------------------
; POST step 3: write the port table (DCP) into RAM page #40 (DcpInit), then
; IN A,(#E2) - the first port READ after reset "opens" the decoder: until
; then the PLD ignores port writes (hardware-reference section 4.2).
;---------------------------------------------------------------------------
PostDcpInit:
				; = POST_3_INIT_DCP (src: EXP.asm:474, BIOS-TT 0271ac3)
	ld hl,PostDcpOpened	;024d	21 53 02	! S .
	jp DcpInit		;0250	c3 a1 0c	. . .
PostDcpOpened:
				; = POST_3_OK (src: EXP.asm:483, BIOS-TT 0271ac3)
	ld a,(TABLE_X_v3)	;0253	3a a3 00	: . .
	out (01ch),a		;0256	d3 1c		. .
	in a,(0e2h)		;0258	db e2		. .
	in a,(0e2h)		;025a	db e2		. .
	ex af,af'		;025c	08		.
POST_4_PAGES:
				; = POST_4_PAGES (src: EXP.asm:490, BIOS-TT 0271ac3)
	ld b,000h		;025d	06 00		. .
POST_4_PAGES_loop:
				; = POST_4_PAGES.loop (src: EXP.asm:492, BIOS-TT 0271ac3)
	ld a,0ffh		;025f	3e ff		> .
	ld i,a			;0261	ed 47		. G
	ld a,b			;0263	78		x
	out (0e2h),a		;0264	d3 e2		. .
	ld a,000h		;0266	3e 00		> .
	ld i,a			;0268	ed 47		. G
	in a,(0e2h)		;026a	db e2		. .
	cp b			;026c	b8		.
	jr nz,l0276h		;026d	20 07		  .
	djnz POST_4_PAGES_loop	;026f	10 ee		. .
	ex af,af'		;0271	08		.
	out (0e2h),a		;0272	d3 e2		. .
	jr l0280h		;0274	18 0a		. .
l0276h:
	ld c,b			;0276	48		H
	ld ix,l027bh		;0277	dd 21 7b 02	. ! { .
l027bh:
	ld b,0dfh		;027b	06 df		. .
	jp OUT_C_BYTE		;027d	c3 07 02	. . .
l0280h:
	ld a,(TABLE_X_v4)	;0280	3a a4 00	: . .
	out (01ch),a		;0283	d3 1c		. .
POST_5_DATA_BUS:
				; = POST_5_DATA_BUS (src: EXP.asm:524, BIOS-TT 0271ac3)
	ld b,000h		;0285	06 00		. .
POST_5_DATA_BUS_loop:
				; = POST_5_DATA_BUS.loop (src: EXP.asm:526, BIOS-TT 0271ac3)
	in a,(000h)		;0287	db 00		. .
	cp 0ffh			;0289	fe ff		. .
	djnz POST_5_DATA_BUS_loop	;028b	10 fa		. .
	jr l02a7h		;028d	18 18		. .

; BLOCK 'Data028F' (start 0x028f end 0x0290)
Data028F:
	defb 04fh		;028f	4f		O
l0290h:
	ld ix,POST_5_DATA_BUS_erbr1	;0290	dd 21 99 02	. ! . .
	ld b,0dfh		;0294	06 df		. .
	jp OUT_C_BYTE		;0296	c3 07 02	. . .
POST_5_DATA_BUS_erbr1:
				; = POST_5_DATA_BUS.erbr1 (src: EXP.asm:539, BIOS-TT 0271ac3)
	ld a,0dfh		;0299	3e df		> .
	out (01ch),a		;029b	d3 1c		. .
	ld de,Reset		;029d	11 00 00	. . .
POST_5_DATA_BUS_LOOP_WTT4:
				; = POST_5_DATA_BUS.LOOP_WTT4 (src: EXP.asm:544, BIOS-TT 0271ac3)
	dec de			;02a0	1b		.
	ld a,d			;02a1	7a		z
	or e			;02a2	b3		.
	jr nz,POST_5_DATA_BUS_LOOP_WTT4	;02a3	20 fb		  .
	jr l0290h		;02a5	18 e9		. .
l02a7h:
	ld a,(TABLE_X_v5)	;02a7	3a a5 00	: . .
	out (01ch),a		;02aa	d3 1c		. .
;---------------------------------------------------------------------------
; The PLD loader leaves IY = #0107 and IX = #FFFD (loader 3.04 #0000-#0099).
; If IY is #0107 the IX value is kept, otherwise IX = #FFFD; it is stored
; at #C13E of the system page (#FE). Then RAM pages for windows 0-3 and
; PortsInit.
;---------------------------------------------------------------------------
PostLoaderHandOver:
	in a,(0e2h)		;02ac	db e2		. .
	ex af,af'		;02ae	08		.
	ld a,0feh		;02af	3e fe		> .
	out (0e2h),a		;02b1	d3 e2		. .
	defb 0fdh,07dh ;ld a,iyl	;02b3	fd 7d		. }
	ld l,a			;02b5	6f		o
	defb 0fdh,07ch ;ld a,iyh	;02b6	fd 7c		. |
	ld h,a			;02b8	67		g
	ld bc,00107h		;02b9	01 07 01	. . .
	and a			;02bc	a7		.
	sbc hl,bc		;02bd	ed 42		. B
	jr z,l02c5h		;02bf	28 04		( .
	ld ix,0fffdh		;02c1	dd 21 fd ff	. ! . .
l02c5h:
	ld (0c13eh),ix		;02c5	dd 22 3e c1	. " > .
	ld hl,(0c13eh)		;02c9	2a 3e c1	* > .
	ex af,af'		;02cc	08		.
	out (0e2h),a		;02cd	d3 e2		. .
	xor a			;02cf	af		.
	out (089h),a		;02d0	d3 89		. .
	out (0c9h),a		;02d2	d3 c9		. .
	out (0e2h),a		;02d4	d3 e2		. .
	out (082h),a		;02d6	d3 82		. .
	ld a,005h		;02d8	3e 05		> .
	out (0a2h),a		;02da	d3 a2		. .
	ld a,002h		;02dc	3e 02		> .
	out (0c2h),a		;02de	d3 c2		. .
	ld sp,0bffeh		;02e0	31 fe bf	1 . .
	push hl			;02e3	e5		.
	call PortsInit		;02e4	cd 6e 03	. n .
	call Fn97_CheckInit	;02e7	cd 31 12	. 1 .
	pop hl			;02ea	e1		.
	jr l02edh		;02eb	18 00		. .
l02edh:
	ld a,0eeh		;02ed	3e ee		> .
	ld b,000h		;02ef	06 00		. .
	ld c,0f8h		;02f1	0e f8		. .
	call ToBios3D13	;02f3	cd 13 3d	. . =
	ld a,b			;02f6	78		x
	and a			;02f7	a7		.
	jr z,CheckSystemPage	;02f8	28 16		( .
	di			;02fa	f3		.
	out (0e2h),a		;02fb	d3 e2		. .
	ld a,(0fff1h)		;02fd	3a f1 ff	: . .
	out (0a2h),a		;0300	d3 a2		. .
	ld a,(0fff2h)		;0302	3a f2 ff	: . .
	out (0c2h),a		;0305	d3 c2		. .
	ld a,(0fff3h)		;0307	3a f3 ff	: . .
	out (0e2h),a		;030a	d3 e2		. .
	ld hl,(0fff4h)		;030c	2a f4 ff	* . .
	jp (hl)			;030f	e9		.
CheckSystemPage:
	in a,(0e2h)		;0310	db e2		. .
	ex af,af'		;0312	08		.
	ld a,041h		;0313	3e 41		> A
	out (0e2h),a		;0315	d3 e2		. .
	ld a,(0fffeh)		;0317	3a fe ff	: . .
	cp 05ah			;031a	fe 5a		. Z
	jr nz,l0325h		;031c	20 07		  .
	ld a,(0ffffh)		;031e	3a ff ff	: . .
	cp 058h			;0321	fe 58		. X
	jr z,l0347h		;0323	28 22		( "
l0325h:
	ld hl,0c000h		;0325	21 00 c0	! . .
	ld de,0c001h		;0328	11 01 c0	. . .
	ld bc,03fffh		;032b	01 ff 3f	. . ?
	ld (hl),c		;032e	71		q
	ldir			;032f	ed b0		. .
	ld hl,l04d4h		;0331	21 d4 04	! . .
	ld de,0c038h		;0334	11 38 c0	. 8 .
	ld bc,l009dh		;0337	01 9d 00	. . .
	ldir			;033a	ed b0		. .
	ld hl,l04cch		;033c	21 cc 04	! . .
	ld de,0c008h		;033f	11 08 c0	. . .
	ld bc,Rst08FromRam	;0342	01 08 00	. . .
	ldir			;0345	ed b0		. .
l0347h:
	ex af,af'		;0347	08		.
	out (0e2h),a		;0348	d3 e2		. .
	ld a,0ffh		;034a	3e ff		> .
	ld bc,l204eh		;034c	01 4e 20	. N  
	out (c),a		;034f	ed 79		. y
	ld hl,SCREEN_TABLES_PENTAGON	;0351	21 62 0c	! b .
	call FN_SYNC_SetDefLines	;0354	cd 72 0b	. r .
	ld hl,SCREEN_TABLES_PENTAGON	;0357	21 62 0c	! b .
	call FN_SYNC_SetDefLines	;035a	cd 72 0b	. r .
;---------------------------------------------------------------------------
; RunSetup: copy StartSetupInRam (43 bytes) to #C000 and call it; SETUP
; normally never returns. If it does, enter Spectrum mode.
;---------------------------------------------------------------------------
RunSetup:
	ld hl,StartSetupInRam	;035d	21 26 01	! & .
	ld de,0c000h		;0360	11 00 c0	. . .
	ld bc,l002bh		;0363	01 2b 00	. + .
	ldir			;0366	ed b0		. .
	call 0c000h		;0368	cd 00 c0	. . .
	jp SpectrumMode		;036b	c3 24 04	. $ .
;---------------------------------------------------------------------------
; PortsInit: ISA reset pulse (#9FBD), SIO A and B (keyboard and mouse,
; MAN 9.1/9.4), CTC channel 0, PIO, Covox-Blaster muted (#4F x 256),
; port map 3 with the Beta system register (#FF), then:
;   OUT (#BC),#21  -> address #21BC, code #2B: primary IDE channel
;   OUT (#7FFD),#10 and OUT (#1FFD),#01: Spectrum paging registers
;---------------------------------------------------------------------------
PortsInit:
				; = PORTS_INIT (src: EXP.asm:776, BIOS-TT 0271ac3)
	ld a,007h		;036e	3e 07		> .
	out (07ch),a		;0370	d3 7c		. |
	ld bc,09fbdh		;0372	01 bd 9f	. . .
	ld a,0ffh		;0375	3e ff		> .
	out (c),a		;0377	ed 79		. y
PORTS_INIT_isa_reset:
				; = PORTS_INIT.isa_reset (src: EXP.asm:785, BIOS-TT 0271ac3)
	dec a			;0379	3d		=
	jr nz,PORTS_INIT_isa_reset	;037a	20 fd		  .
	out (c),a		;037c	ed 79		. y
	ld a,000h		;037e	3e 00		> .
	out (019h),a		;0380	d3 19		. .
	ld a,001h		;0382	3e 01		> .
	out (019h),a		;0384	d3 19		. .
	ld a,000h		;0386	3e 00		> .
	out (019h),a		;0388	d3 19		. .
	ld a,003h		;038a	3e 03		> .
	out (019h),a		;038c	d3 19		. .
	ld a,0c1h		;038e	3e c1		> .
	out (019h),a		;0390	d3 19		. .
	ld a,004h		;0392	3e 04		> .
	out (019h),a		;0394	d3 19		. .
	ld a,005h		;0396	3e 05		> .
	out (019h),a		;0398	d3 19		. .
	ld a,005h		;039a	3e 05		> .
	out (019h),a		;039c	d3 19		. .
	ld a,062h		;039e	3e 62		> b
	out (019h),a		;03a0	d3 19		. .
	ld a,000h		;03a2	3e 00		> .
	out (01bh),a		;03a4	d3 1b		. .
	ld a,001h		;03a6	3e 01		> .
	out (01bh),a		;03a8	d3 1b		. .
	ld a,000h		;03aa	3e 00		> .
	out (01bh),a		;03ac	d3 1b		. .
	ld a,003h		;03ae	3e 03		> .
	out (01bh),a		;03b0	d3 1b		. .
	ld a,041h		;03b2	3e 41		> A
	out (01bh),a		;03b4	d3 1b		. .
	ld a,004h		;03b6	3e 04		> .
	out (01bh),a		;03b8	d3 1b		. .
	ld a,044h		;03ba	3e 44		> D
	out (01bh),a		;03bc	d3 1b		. .
	ld a,005h		;03be	3e 05		> .
	out (01bh),a		;03c0	d3 1b		. .
	ld a,0e0h		;03c2	3e e0		> .
	out (01bh),a		;03c4	d3 1b		. .
	ld a,055h		;03c6	3e 55		> U
	out (010h),a		;03c8	d3 10		. .
	ld a,02dh		;03ca	3e 2d		> -
	out (010h),a		;03cc	d3 10		. .
	ld a,00fh		;03ce	3e 0f		> .
	out (01dh),a		;03d0	d3 1d		. .
	out (01dh),a		;03d2	d3 1d		. .
	ld bc,0001fh		;03d4	01 1f 00	. . .
	ld a,0cfh		;03d7	3e cf		> .
	out (c),a		;03d9	ed 79		. y
	ld a,03fh		;03db	3e 3f		> ?
	out (c),a		;03dd	ed 79		. y
	ld a,0c0h		;03df	3e c0		> .
	out (01eh),a		;03e1	d3 1e		. .
	xor a			;03e3	af		.
	out (04eh),a		;03e4	d3 4e		. N
	ld bc,l004fh		;03e6	01 4f 00	. O .
	ld a,080h		;03e9	3e 80		> .
PORTS_INIT_CBL_MUTE:
				; = PORTS_INIT.CBL_MUTE (src: EXP.asm:884, BIOS-TT 0271ac3)
	out (c),a		;03eb	ed 79		. y
	djnz PORTS_INIT_CBL_MUTE	;03ed	10 fc		. .
	ld a,01ch		;03ef	3e 1c		> .
	out (07ch),a		;03f1	d3 7c		. |
	out (0ffh),a		;03f3	d3 ff		. .
	push hl			;03f5	e5		.
	pop hl			;03f6	e1		.
	ld a,03ch		;03f7	3e 3c		> <
	out (0ffh),a		;03f9	d3 ff		. .
	push hl			;03fb	e5		.
	pop hl			;03fc	e1		.
	xor a			;03fd	af		.
	out (00fh),a		;03fe	d3 0f		. .
l0400h:
	ld a,004h		;0400	3e 04		> .
	out (07ch),a		;0402	d3 7c		. |
	ld a,021h		;0404	3e 21		> !
	out (0bch),a		;0406	d3 bc		. .
	ld bc,07ffdh		;0408	01 fd 7f	. . .
	ld a,010h		;040b	3e 10		> .
	out (c),a		;040d	ed 79		. y
	ld b,01fh		;040f	06 1f		. .
	ld a,001h		;0411	3e 01		> .
	out (c),a		;0413	ed 79		. y
	ret			;0415	c9		.
FnFB_GOTO_SPECTRUM:
				; = ~init_zx_roms.length (src: EXP.asm:1053, BIOS-TT 0271ac3)
				; = ~GOTO_SPEC (src: EXP.asm:1059, BIOS-TT 0271ac3)
	in a,(0e2h)		;0416	db e2		. .
	ex af,af'		;0418	08		.
	ld a,0feh		;0419	3e fe		> .
	out (0e2h),a		;041b	d3 e2		. .
	ld a,b			;041d	78		x
	ld (0c13bh),a		;041e	32 3b c1	2 ; .
	ex af,af'		;0421	08		.
	out (0e2h),a		;0422	d3 e2		. .
SpectrumMode:
				; = ~ZX_SPECTRUM_MODE (src: EXP.asm:1074, BIOS-TT 0271ac3)
	ld a,0feh		;0424	3e fe		> .
	ld bc,l204eh		;0426	01 4e 20	. N  
	out (c),a		;0429	ed 79		. y
	ld sp,0bfffh		;042b	31 ff bf	1 . .
	ld hl,SCREEN_TABLES_PENTAGON	;042e	21 62 0c	! b .
	call FN_SYNC_SetDefLines	;0431	cd 72 0b	. r .
	call SET_PAL_ZX		;0434	cd f4 09	. . .
	ld hl,04104h		;0437	21 04 41	! . A
	ld e,000h		;043a	1e 00		. .
	ld b,004h		;043c	06 04		. .
	call Fn80_LP_OPEN_S	;043e	cd b5 37	. . 7
	ld hl,05104h		;0441	21 04 51	! . Q
	ld e,000h		;0444	1e 00		. .
	ld b,004h		;0446	06 04		. .
	call Fn80_LP_OPEN_S	;0448	cd b5 37	. . 7
	ld d,035h		;044b	16 35		. 5
	call FnF6_CMOS_RD	;044d	cd ba 32	. . 2
	bit 0,a			;0450	cb 47		. G
SPECTRUM_0:
				; = ~SPECTRUM_0 (src: EXP.asm:1107, BIOS-TT 0271ac3)
	xor a			;0452	af		.
SPECTRUM_TASK:
				; = ~SPECTRUM_TASK (src: EXP.asm:1109, BIOS-TT 0271ac3)
	ld ix,StartSpectrumRom	;0453	dd 21 5a 04	. ! Z .
	jp REINIT_MD_NO_INC_H		;0457	c3 91 09	. . .
StartSpectrumRom:
	ld sp,0bfffh		;045a	31 ff bf	1 . .
	ld de,05b00h		;045d	11 00 5b	. . [
	ld hl,SpectrumRomStub5B00	;0460	21 6c 04	! l .
	ld bc,l0055h		;0463	01 55 00	. U .
	ldir			;0466	ed b0		. .
	di			;0468	f3		.
	jp 05b00h		;0469	c3 00 5b	. . [
SpectrumRomStub5B00:
	ld a,000h		;046c	3e 00		> .
	out (03ch),a		;046e	d3 3c		. <
	ld c,0e2h		;0470	0e e2		. .
	in b,(c)		;0472	ed 40		. @
	ld a,0feh		;0474	3e fe		> .
	out (0e2h),a		;0476	d3 e2		. .
	ld de,(0c13ah)		;0478	ed 5b 3a c1	. [ : .
	xor a			;047c	af		.
	ld (0c13bh),a		;047d	32 3b c1	2 ; .
	out (c),b		;0480	ed 41		. A
	ld bc,l1ffdh		;0482	01 fd 1f	. . .
	out (c),a		;0485	ed 79		. y
	ld b,07fh		;0487	06 7f		. .
	out (c),a		;0489	ed 79		. y
	ld a,e			;048b	7b		{
	out (03ch),a		;048c	d3 3c		. <
	inc d			;048e	14		.
	dec d			;048f	15		.
	jp z,Reset		;0490	ca 00 00	. . .
	ld a,010h		;0493	3e 10		> .
	out (c),a		;0495	ed 79		. y
	dec d			;0497	15		.
	jp z,Reset		;0498	ca 00 00	. . .
	ld hl,Reset		;049b	21 00 00	! . .
	push hl			;049e	e5		.
	dec d			;049f	15		.
	jp z,l3d29h	;04a0	ca 29 3d	. ) =
	xor a			;04a3	af		.
	out (c),a		;04a4	ed 79		. y
	ld a,002h		;04a6	3e 02		> .
	ld b,01fh		;04a8	06 1f		. .
	out (c),a		;04aa	ed 79		. y
	dec d			;04ac	15		.
	jp z,Reset		;04ad	ca 00 00	. . .
	ld a,000h		;04b0	3e 00		> .
	out (c),a		;04b2	ed 79		. y
	ld a,030h		;04b4	3e 30		> 0
	ld b,07fh		;04b6	06 7f		. .
	out (c),a		;04b8	ed 79		. y
	dec d			;04ba	15		.
	jp z,l3d29h	;04bb	ca 29 3d	. ) =
	jp Reset		;04be	c3 00 00	. . .
MsgNoSpectrumRom:

; BLOCK 'Text04C1' (start 0x04c1 end 0x0523)
	defb 03eh		;04c1	3e		>
	defb 002h		;04c2	02		.
	defb 0d3h		;04c3	d3		.
	defb 07ch		;04c4	7c		|
	defb 0c9h		;04c5	c9		.
	defb 03eh		;04c6	3e		>
	defb 003h		;04c7	03		.
	defb 0d3h		;04c8	d3		.
	defb 07ch		;04c9	7c		|
	defb 0c9h		;04ca	c9		.
	defb 0e9h		;04cb	e9		.
l04cch:
	defb 0f5h		;04cc	f5		.
	defb 03eh		;04cd	3e		>
	defb 000h		;04ce	00		.
	defb 0d3h		;04cf	d3		.
	defb 07ch		;04d0	7c		|
	defb 0f1h		;04d1	f1		.
	defb 0c9h		;04d2	c9		.
	defb 000h		;04d3	00		.
l04d4h:
	defb 0f3h		;04d4	f3		.
	defb 018h		;04d5	18		.
	defb 04ch		;04d6	4c		L
	defb 020h		;04d7	20		 
	defb 053h		;04d8	53		S
	defb 070h		;04d9	70		p
	defb 065h		;04da	65		e
	defb 063h		;04db	63		c
	defb 074h		;04dc	74		t
	defb 072h		;04dd	72		r
	defb 075h		;04de	75		u
	defb 06dh		;04df	6d		m
	defb 020h		;04e0	20		 
	defb 052h		;04e1	52		R
	defb 04fh		;04e2	4f		O
	defb 04dh		;04e3	4d		M
	defb 020h		;04e4	20		 
	defb 06eh		;04e5	6e		n
	defb 06fh		;04e6	6f		o
	defb 074h		;04e7	74		t
	defb 020h		;04e8	20		 
	defb 069h		;04e9	69		i
	defb 06eh		;04ea	6e		n
	defb 073h		;04eb	73		s
	defb 074h		;04ec	74		t
	defb 061h		;04ed	61		a
	defb 06ch		;04ee	6c		l
	defb 06ch		;04ef	6c		l
	defb 065h		;04f0	65		e
	defb 064h		;04f1	64		d
	defb 02eh		;04f2	2e		.
	defb 020h		;04f3	20		 
	defb 020h		;04f4	20		 
	defb 055h		;04f5	55		U
	defb 073h		;04f6	73		s
	defb 065h		;04f7	65		e
	defb 020h		;04f8	20		 
	defb 073h		;04f9	73		s
	defb 070h		;04fa	70		p
	defb 065h		;04fb	65		e
	defb 063h		;04fc	63		c
	defb 074h		;04fd	74		t
	defb 072h		;04fe	72		r
	defb 075h		;04ff	75		u
	defb 06dh		;0500	6d		m
	defb 02eh		;0501	2e		.
	defb 065h		;0502	65		e
	defb 078h		;0503	78		x
	defb 065h		;0504	65		e
	defb 020h		;0505	20		 
	defb 020h		;0506	20		 
	defb 050h		;0507	50		P
	defb 072h		;0508	72		r
	defb 065h		;0509	65		e
	defb 073h		;050a	73		s
	defb 073h		;050b	73		s
	defb 020h		;050c	20		 
	defb 043h		;050d	43		C
	defb 074h		;050e	74		t
	defb 072h		;050f	72		r
	defb 06ch		;0510	6c		l
	defb 02bh		;0511	2b		+
	defb 041h		;0512	41		A
	defb 06ch		;0513	6c		l
	defb 074h		;0514	74		t
	defb 02bh		;0515	2b		+
	defb 044h		;0516	44		D
	defb 065h		;0517	65		e
	defb 06ch		;0518	6c		l
	defb 020h		;0519	20		 
	defb 06fh		;051a	6f		o
	defb 072h		;051b	72		r
	defb 020h		;051c	20		 
	defb 052h		;051d	52		R
	defb 045h		;051e	45		E
	defb 053h		;051f	53		S
	defb 045h		;0520	45		E
	defb 054h		;0521	54		T
	defb 000h		;0522	00		.
l0523h:
	ld a,0ffh		;0523	3e ff		> .
	out (0e2h),a		;0525	d3 e2		. .
	out (0c2h),a		;0527	d3 c2		. .
	out (0a2h),a		;0529	d3 a2		. .
	ld sp,0bf00h		;052b	31 00 bf	1 . .
	ld c,080h		;052e	0e 80		. .
	ld b,003h		;0530	06 03		. .
	ld e,000h		;0532	1e 00		. .
	rst 8			;0534	cf		.
	ld c,089h		;0535	0e 89		. .
	ld de,Reset		;0537	11 00 00	. . .
	ld hl,l2050h		;053a	21 50 20	! P  
	rst 8			;053d	cf		.
	ld a,001h		;053e	3e 01		> .
	out (0c9h),a		;0540	d3 c9		. .
	ld hl,Zero003B	;0542	21 3b 00	! ; .
	ld de,0a000h		;0545	11 00 a0	. . .
	ld bc,l004ch		;0548	01 4c 00	. L .
	ld a,c			;054b	79		y
	ldir			;054c	ed b0		. .
	ld hl,0a000h		;054e	21 00 a0	! . .
	ld b,a			;0551	47		G
	ld c,087h		;0552	0e 87		. .
	ld e,0c3h		;0554	1e c3		. .
	ld d,000h		;0556	16 00		. .
	rst 8			;0558	cf		.
	di			;0559	f3		.
	halt			;055a	76		v

; BLOCK 'Data055B' (start 0x055b end 0x0571)
Data055B:
	defb 03eh		;055b	3e		>
	defb 010h		;055c	10		.
	defb 001h		;055d	01		.
	defb 0fdh		;055e	fd		.
	defb 01fh		;055f	1f		.
	defb 0edh		;0560	ed		.
	defb 079h		;0561	79		y
	defb 03eh		;0562	3e		>
	defb 0a0h		;0563	a0		.
	defb 0d3h		;0564	d3		.
	defb 0e2h		;0565	e2		.
	defb 03eh		;0566	3e		>
	defb 002h		;0567	02		.
	defb 0d3h		;0568	d3		.
	defb 074h		;0569	74		t
	defb 032h		;056a	32		2
	defb 000h		;056b	00		.
	defb 0c0h		;056c	c0		.
	defb 018h		;056d	18		.
	defb 0fbh		;056e	fb		.
	defb 0f3h		;056f	f3		.
	defb 076h		;0570	76		v
;---------------------------------------------------------------------------
; Functions #40-#4F (C already had bit 6 cleared, so C = 0..#0F here) are
; the old HDD API and are served in this page; #50-#7F go to ROM page 0
; through ToPage0 (#3FE8).
;---------------------------------------------------------------------------
HddFnDispatch:
	push af			;0571	f5		.
	ld a,c			;0572	79		y
	cp 010h			;0573	fe 10		. .
	jr c,l057bh		;0575	38 04		8 .
	pop af			;0577	f1		.
	jp ToPage0		;0578	c3 e8 3f	. . ?
l057bh:
	ld a,c			;057b	79		y
	and a			;057c	a7		.
	jp z,Fn40_HDD_INIT	;057d	ca c1 07	. . .
	dec a			;0580	3d		=
	jp z,Fn41_HDD_RECAL	;0581	ca 66 07	. f .
	dec a			;0584	3d		=
	jp z,Fn42_HDD_TEST_IDE	;0585	ca 81 07	. . .
	dec a			;0588	3d		=
	jp z,Fn43_HDD_PREPARE	;0589	ca 22 06	. " .
	dec a			;058c	3d		=
	jp z,Fn44_HDD_READ_BPB	;058d	ca 43 06	. C .
	dec a			;0590	3d		=
	jp z,Fn45_HDD_READ	;0591	ca 5c 06	. \ .
	dec a			;0594	3d		=
	jp z,Fn46_HDD_WRITE	;0595	ca fc 06	. . .
	dec a			;0598	3d		=
	jp z,Fn47_HDD_PART	;0599	ca dd 08	. . .
	dec a			;059c	3d		=
	jp z,Fn48_HDD_READ_NEXT	;059d	ca 4e 06	. N .
	pop af			;05a0	f1		.
	scf			;05a1	37		7
	ret			;05a2	c9		.
PORTS_INIT_loop:
				; = PORTS_INIT.loop (src: FUNC_4x.ASM:3, BIOS-TT 0271ac3)
				; = HD_BPB_PREP (src: FUNC_4x.ASM:11, BIOS-TT 0271ac3)
	ld d,a			;05a3	57		W
	in a,(0e2h)		;05a4	db e2		. .
	ex af,af'		;05a6	08		.
	ld a,0feh		;05a7	3e fe		> .
	out (0e2h),a		;05a9	d3 e2		. .
	ld a,(0c60ch)		;05ab	3a 0c c6	: . .
	ld e,a			;05ae	5f		_
	ex af,af'		;05af	08		.
	out (0e2h),a		;05b0	d3 e2		. .
	ld a,d			;05b2	7a		z
	ld d,000h		;05b3	16 00		. .
	ld ix,Reset		;05b5	dd 21 00 00	. ! . .
	ld b,001h		;05b9	06 01		. .
HD_PREPARE:
				; = HD_PREPARE (src: FUNC_4x.ASM:25, BIOS-TT 0271ac3)
	push af			;05bb	f5		.
	push hl			;05bc	e5		.
	call HD_CALC_SECS	;05bd	cd 8c 08	. . .
	jr nc,HD_PREPARE_L1	;05c0	30 04		0 .
	pop hl			;05c2	e1		.
	pop af			;05c3	f1		.
	scf			;05c4	37		7
	ret			;05c5	c9		.
HD_PREPARE_L1:
				; = HD_PREPARE.L1 (src: FUNC_4x.ASM:35, BIOS-TT 0271ac3)
	ld a,b			;05c6	78		x
	ld bc,InitCpuPorts+1	;05c7	01 52 01	. R .
	out (c),a		;05ca	ed 79		. y
	ld bc,InitCpuPorts+2	;05cc	01 53 01	. S .
	out (c),l		;05cf	ed 69		. i
	ld bc,l0154h		;05d1	01 54 01	. T .
	out (c),e		;05d4	ed 59		. Y
	ld bc,l0154h+1		;05d6	01 55 01	. U .
	out (c),d		;05d9	ed 51		. Q
	ld bc,04152h		;05db	01 52 41	. R A
	dec b			;05de	05		.
	in a,(c)		;05df	ed 78		. x
	and 0f0h		;05e1	e6 f0		. .
	or h			;05e3	b4		.
	inc b			;05e4	04		.
	out (c),a		;05e5	ed 79		. y
	pop hl			;05e7	e1		.
	pop af			;05e8	f1		.
	and a			;05e9	a7		.
	ret			;05ea	c9		.
NEXT_ADD_SEC:
				; = NEXT_ADD_SEC (src: FUNC_4x.ASM:69, BIOS-TT 0271ac3)
	push af			;05eb	f5		.
	ld a,b			;05ec	78		x
	ld bc,InitCpuPorts+1	;05ed	01 52 01	. R .
	out (c),a		;05f0	ed 79		. y
	ld bc,l0053h		;05f2	01 53 00	. S .
	in a,(c)		;05f5	ed 78		. x
	adc a,e			;05f7	8b		.
	inc b			;05f8	04		.
	out (c),a		;05f9	ed 79		. y
	ld bc,l0054h		;05fb	01 54 00	. T .
	in a,(c)		;05fe	ed 78		. x
	adc a,d			;0600	8a		.
	inc b			;0601	04		.
	out (c),a		;0602	ed 79		. y
	ld bc,l0055h		;0604	01 55 00	. U .
	in a,(c)		;0607	ed 78		. x
	adc a,000h		;0609	ce 00		. .
	inc b			;060b	04		.
	out (c),a		;060c	ed 79		. y
	ld bc,04052h		;060e	01 52 40	. R @
	in a,(c)		;0611	ed 78		. x
	ld d,a			;0613	57		W
	adc a,000h		;0614	ce 00		. .
	and 00fh		;0616	e6 0f		. .
	ld e,a			;0618	5f		_
	ld a,d			;0619	7a		z
	and 0f0h		;061a	e6 f0		. .
	or e			;061c	b3		.
	inc b			;061d	04		.
	out (c),a		;061e	ed 79		. y
	pop af			;0620	f1		.
	ret			;0621	c9		.
Fn43_HDD_PREPARE:
	pop af			;0622	f1		.
FN_HDD_PREPARE:
				; = FN_HDD_PREPARE (src: FUNC_4x.ASM:143, BIOS-TT 0271ac3)
	and a			;0623	a7		.
	inc b			;0624	04		.
	dec b			;0625	05		.
	ret z			;0626	c8		.
	call HD_WAIT		;0627	cd 63 08	. c .
	ret c			;062a	d8		.
	call HD_PREPARE		;062b	cd bb 05	. . .
	ret c			;062e	d8		.
	exx			;062f	d9		.
	ld c,0e2h		;0630	0e e2		. .
	in b,(c)		;0632	ed 40		. @
	exx			;0634	d9		.
	out (0e2h),a		;0635	d3 e2		. .
	ex af,af'		;0637	08		.
	ld a,0c0h		;0638	3e c0		> .
	out (089h),a		;063a	d3 89		. .
	ld bc,04153h		;063c	01 53 41	. S A
	ld a,020h		;063f	3e 20		>  
	and a			;0641	a7		.
	ret			;0642	c9		.
Fn44_HDD_READ_BPB:
	pop af			;0643	f1		.
FN_HDD_READ_BPB:
				; = FN_HDD_READ_BPB (src: FUNC_4x.ASM:177, BIOS-TT 0271ac3)
	call HD_WAIT		;0644	cd 63 08	. c .
	ret c			;0647	d8		.
	call PORTS_INIT_loop	;0648	cd a3 05	. . .
	ret c			;064b	d8		.
	jr HD_RD_L1		;064c	18 1b		. .
Fn48_HDD_READ_NEXT:
	pop af			;064e	f1		.
FN_HDD_READ_NEXT:
				; = FN_HDD_READ_NEXT (src: FUNC_4x.ASM:190, BIOS-TT 0271ac3)
	and a			;064f	a7		.
	inc b			;0650	04		.
	dec b			;0651	05		.
	ret z			;0652	c8		.
	call HD_WAIT		;0653	cd 63 08	. c .
	ret c			;0656	d8		.
	call NEXT_ADD_SEC	;0657	cd eb 05	. . .
	jr HD_RD_L1		;065a	18 0d		. .
Fn45_HDD_READ:
	pop af			;065c	f1		.
FN_HDD_READ:
				; = FN_HDD_READ (src: FUNC_4x.ASM:202, BIOS-TT 0271ac3)
	and a			;065d	a7		.
	inc b			;065e	04		.
	dec b			;065f	05		.
	ret z			;0660	c8		.
	call HD_WAIT		;0661	cd 63 08	. c .
	ret c			;0664	d8		.
	call HD_PREPARE		;0665	cd bb 05	. . .
	ret c			;0668	d8		.
HD_RD_L1:
				; = HD_RD_L1 (src: FUNC_4x.ASM:212, BIOS-TT 0271ac3)
	exx			;0669	d9		.
	ld c,0e2h		;066a	0e e2		. .
	in b,(c)		;066c	ed 40		. @
	exx			;066e	d9		.
	out (0e2h),a		;066f	d3 e2		. .
	ex af,af'		;0671	08		.
	ld a,0c0h		;0672	3e c0		> .
	out (089h),a		;0674	d3 89		. .
	ld bc,04153h		;0676	01 53 41	. S A
	ld a,020h		;0679	3e 20		>  
	out (c),a		;067b	ed 79		. y
l067dh:
	ld bc,04053h		;067d	01 53 40	. S @
	in a,(c)		;0680	ed 78		. x
	bit 7,a			;0682	cb 7f		. .
	jr nz,l067dh		;0684	20 f7		  .
	bit 3,a			;0686	cb 5f		. _
	jr nz,HD_READ_CONT	;0688	20 13		  .
	ld a,000h		;068a	3e 00		> .
	out (089h),a		;068c	d3 89		. .
HD_RET:
				; = HD_RET (src: FUNC_4x.ASM:233, BIOS-TT 0271ac3)
	exx			;068e	d9		.
	out (c),b		;068f	ed 41		. A
	exx			;0691	d9		.
	ld bc,l0051h		;0692	01 51 00	. Q .
	in a,(c)		;0695	ed 78		. x
	and a			;0697	a7		.
	scf			;0698	37		7
	ret nz			;0699	c0		.
	ex af,af'		;069a	08		.
	and a			;069b	a7		.
	ret			;069c	c9		.
HD_READ_CONT:
				; = HD_READ_CONT (src: FUNC_4x.ASM:247, BIOS-TT 0271ac3)
	ld bc,l0050h		;069d	01 50 00	. P .
l06a0h:
	ini			;06a0	ed a2		. .
	ini			;06a2	ed a2		. .
	ini			;06a4	ed a2		. .
	ini			;06a6	ed a2		. .
	ini			;06a8	ed a2		. .
	ini			;06aa	ed a2		. .
	ini			;06ac	ed a2		. .
	ini			;06ae	ed a2		. .
	ini			;06b0	ed a2		. .
	ini			;06b2	ed a2		. .
	ini			;06b4	ed a2		. .
	ini			;06b6	ed a2		. .
	ini			;06b8	ed a2		. .
	ini			;06ba	ed a2		. .
	ini			;06bc	ed a2		. .
	ini			;06be	ed a2		. .
	jp nz,l06a0h		;06c0	c2 a0 06	. . .
l06c3h:
	ini			;06c3	ed a2		. .
	ini			;06c5	ed a2		. .
	ini			;06c7	ed a2		. .
	ini			;06c9	ed a2		. .
	ini			;06cb	ed a2		. .
	ini			;06cd	ed a2		. .
	ini			;06cf	ed a2		. .
	ini			;06d1	ed a2		. .
	ini			;06d3	ed a2		. .
	ini			;06d5	ed a2		. .
	ini			;06d7	ed a2		. .
	ini			;06d9	ed a2		. .
	ini			;06db	ed a2		. .
	ini			;06dd	ed a2		. .
	ini			;06df	ed a2		. .
	ini			;06e1	ed a2		. .
	jp nz,l06c3h		;06e3	c2 c3 06	. . .
	ld a,h			;06e6	7c		|
	or l			;06e7	b5		.
	jr nz,l067dh		;06e8	20 93		  .
	ld a,0feh		;06ea	3e fe		> .
	out (0e2h),a		;06ec	d3 e2		. .
	ex af,af'		;06ee	08		.
	ld hl,0c200h		;06ef	21 00 c2	! . .
	ld l,a			;06f2	6f		o
	ld a,(hl)		;06f3	7e		~
	out (0e2h),a		;06f4	d3 e2		. .
	ex af,af'		;06f6	08		.
	ld hl,0c000h		;06f7	21 00 c0	! . .
	jr l067dh		;06fa	18 81		. .
Fn46_HDD_WRITE:
	pop af			;06fc	f1		.
FN_HDD_WRITE:
				; = FN_HDD_WRITE (src: FUNC_4x.ASM:277, BIOS-TT 0271ac3)
	and a			;06fd	a7		.
	inc b			;06fe	04		.
	dec b			;06ff	05		.
	ret z			;0700	c8		.
	call HD_WAIT		;0701	cd 63 08	. c .
	ret c			;0704	d8		.
	call HD_PREPARE		;0705	cd bb 05	. . .
	ret c			;0708	d8		.
	exx			;0709	d9		.
	ld c,0e2h		;070a	0e e2		. .
	in b,(c)		;070c	ed 40		. @
	exx			;070e	d9		.
	out (0e2h),a		;070f	d3 e2		. .
	ex af,af'		;0711	08		.
	ld bc,04153h		;0712	01 53 41	. S A
	ld a,030h		;0715	3e 30		> 0
	out (c),a		;0717	ed 79		. y
l0719h:
	ld bc,04053h		;0719	01 53 40	. S @
	in a,(c)		;071c	ed 78		. x
	bit 7,a			;071e	cb 7f		. .
	jr nz,l0719h		;0720	20 f7		  .
	bit 3,a			;0722	cb 5f		. _
	jp z,HD_RET		;0724	ca 8e 06	. . .
	ld bc,l0150h		;0727	01 50 01	. P .
	ld d,020h		;072a	16 20		.  
l072ch:
	outi			;072c	ed a3		. .
	outi			;072e	ed a3		. .
	outi			;0730	ed a3		. .
	outi			;0732	ed a3		. .
	outi			;0734	ed a3		. .
	outi			;0736	ed a3		. .
	outi			;0738	ed a3		. .
	outi			;073a	ed a3		. .
	outi			;073c	ed a3		. .
	outi			;073e	ed a3		. .
	outi			;0740	ed a3		. .
	outi			;0742	ed a3		. .
	outi			;0744	ed a3		. .
	outi			;0746	ed a3		. .
	outi			;0748	ed a3		. .
	outi			;074a	ed a3		. .
	dec d			;074c	15		.
	jp nz,l072ch		;074d	c2 2c 07	. , .
	ld a,h			;0750	7c		|
	or l			;0751	b5		.
	jr nz,l0719h		;0752	20 c5		  .
	ld a,0feh		;0754	3e fe		> .
	out (0e2h),a		;0756	d3 e2		. .
	ex af,af'		;0758	08		.
	ld hl,0c200h		;0759	21 00 c2	! . .
	ld l,a			;075c	6f		o
	ld a,(hl)		;075d	7e		~
	out (0e2h),a		;075e	d3 e2		. .
	ex af,af'		;0760	08		.
	ld hl,0c000h		;0761	21 00 c0	! . .
	jr l0719h		;0764	18 b3		. .
Fn41_HDD_RECAL:
	pop af			;0766	f1		.
FN_HDD_RECAL:
				; = FN_HDD_RECAL (src: FUNC_4x.ASM:330, BIOS-TT 0271ac3)
	ld a,0a0h		;0767	3e a0		> .
	ld bc,04152h		;0769	01 52 41	. R A
	out (c),a		;076c	ed 79		. y
	ld a,090h		;076e	3e 90		> .
	call HD_CMD_EXE		;0770	cd 5a 08	. Z .
	and a			;0773	a7		.
	bit 0,a			;0774	cb 47		. G
	ret z			;0776	c8		.
	ld bc,l0051h		;0777	01 51 00	. Q .
	in a,(c)		;077a	ed 78		. x
	cp 001h			;077c	fe 01		. .
	ret z			;077e	c8		.
	scf			;077f	37		7
	ret			;0780	c9		.
Fn42_HDD_TEST_IDE:
	pop af			;0781	f1		.
FN_HDD_TEST_IDE:
				; = FN_HDD_TEST_IDE (src: FUNC_4x.ASM:354, BIOS-TT 0271ac3)
	ld e,000h		;0782	1e 00		. .
	ld bc,04152h		;0784	01 52 41	. R A
	ld a,0a0h		;0787	3e a0		> .
	out (c),a		;0789	ed 79		. y
	call TEST_HDD_DRV	;078b	cd a6 07	. . .
	jr nz,NO_HDD1		;078e	20 02		  .
	set 0,e			;0790	cb c3		. .
NO_HDD1:
				; = NO_HDD1 (src: FUNC_4x.ASM:364, BIOS-TT 0271ac3)
	ld bc,04152h		;0792	01 52 41	. R A
	ld a,0b0h		;0795	3e b0		> .
	out (c),a		;0797	ed 79		. y
	call TEST_HDD_DRV	;0799	cd a6 07	. . .
	jr nz,NO_HDD2		;079c	20 02		  .
	set 1,e			;079e	cb cb		. .
NO_HDD2:
				; = NO_HDD2 (src: FUNC_4x.ASM:373, BIOS-TT 0271ac3)
	ld a,e			;07a0	7b		{
	and a			;07a1	a7		.
	scf			;07a2	37		7
	ret z			;07a3	c8		.
	and a			;07a4	a7		.
	ret			;07a5	c9		.
TEST_HDD_DRV:
				; = TEST_HDD_DRV (src: FUNC_4x.ASM:384, BIOS-TT 0271ac3)
	ld hl,POST_2_ADRESS_BUS_TSAB_3	;07a6	21 fe 01	! . .
	ld bc,InitCpuPorts+1	;07a9	01 52 01	. R .
	out (c),l		;07ac	ed 69		. i
	ld bc,InitCpuPorts+2	;07ae	01 53 01	. S .
	out (c),h		;07b1	ed 61		. a
	ld bc,00252h		;07b3	01 52 02	. R .
	in a,(c)		;07b6	ed 78		. x
	cp l			;07b8	bd		.
	ret nz			;07b9	c0		.
	ld bc,PostDcpOpened	;07ba	01 53 02	. S .
	in a,(c)		;07bd	ed 78		. x
	cp h			;07bf	bc		.
	ret			;07c0	c9		.
Fn40_HDD_INIT:
	pop af			;07c1	f1		.
TEST_HDD_DRV_07C2:
				; = TEST_HDD_DRV (src: FUNC_4x.ASM:412, BIOS-TT 0271ac3)
				; = FN_HDD_INIT (src: FUNC_4x.ASM:444, BIOS-TT 0271ac3)
	ld bc,04152h		;07c2	01 52 41	. R A
	ld a,0a0h		;07c5	3e a0		> .
	out (c),a		;07c7	ed 79		. y
	call TEST_HDD_DRV	;07c9	cd a6 07	. . .
	jr nz,HD_ABSENT		;07cc	20 1f		  .
l07ceh:
	ld bc,04053h		;07ce	01 53 40	. S @
	in a,(c)		;07d1	ed 78		. x
	bit 7,a			;07d3	cb 7f		. .
	jr nz,l07ceh		;07d5	20 f7		  .
	ld bc,04153h		;07d7	01 53 41	. S A
	ld a,0ech		;07da	3e ec		> .
	out (c),a		;07dc	ed 79		. y
l07deh:
	ld bc,04053h		;07de	01 53 40	. S @
	in a,(c)		;07e1	ed 78		. x
	bit 7,a			;07e3	cb 7f		. .
	jr nz,l07deh		;07e5	20 f7		  .
	bit 3,a			;07e7	cb 5f		. _
	jr nz,HD_C0_L2		;07e9	20 10		  .
	scf			;07eb	37		7
	ret			;07ec	c9		.
HD_ABSENT:
				; = HD_ABSENT (src: FUNC_4x.ASM:463, BIOS-TT 0271ac3)
	ld bc,04152h		;07ed	01 52 41	. R A
	ld a,0b0h		;07f0	3e b0		> .
	out (c),a		;07f2	ed 79		. y
	call TEST_HDD_DRV	;07f4	cd a6 07	. . .
	jr z,l07ceh		;07f7	28 d5		( .
	scf			;07f9	37		7
	ret			;07fa	c9		.
HD_C0_L2:
				; = HD_C0_L2 (src: FUNC_4x.ASM:472, BIOS-TT 0271ac3)
	ld bc,l0050h		;07fb	01 50 00	. P .
	ld hl,0c600h		;07fe	21 00 c6	! . .
	in a,(0e2h)		;0801	db e2		. .
	ld d,a			;0803	57		W
	ld a,0feh		;0804	3e fe		> .
	out (0e2h),a		;0806	d3 e2		. .
	inir			;0808	ed b2		. .
	inir			;080a	ed b2		. .
	ld a,(0c60ch)		;080c	3a 0c c6	: . .
	ld c,a			;080f	4f		O
	ld b,000h		;0810	06 00		. .
	ld hl,Reset		;0812	21 00 00	! . .
	ld a,(0c606h)		;0815	3a 06 c6	: . .
HD_C0_L2_loop:
				; = HD_C0_L2.loop (src: FUNC_4x.ASM:486, BIOS-TT 0271ac3)
	add hl,bc		;0818	09		.
	dec a			;0819	3d		=
	jr nz,HD_C0_L2_loop	;081a	20 fc		  .
	ld (0c604h),hl		;081c	22 04 c6	" . .
l081fh:
	ld bc,04053h		;081f	01 53 40	. S @
	in a,(c)		;0822	ed 78		. x
	bit 7,a			;0824	cb 7f		. .
	jr nz,l081fh		;0826	20 f7		  .
	ld bc,04152h		;0828	01 52 41	. R A
	dec b			;082b	05		.
	in a,(c)		;082c	ed 78		. x
	and 010h		;082e	e6 10		. .
	ld b,a			;0830	47		G
	ld a,(0c606h)		;0831	3a 06 c6	: . .
	dec a			;0834	3d		=
	and 00fh		;0835	e6 0f		. .
	or 0a0h			;0837	f6 a0		. .
	or b			;0839	b0		.
	ld h,a			;083a	67		g
	ld a,(0c663h)		;083b	3a 63 c6	: c .
	bit 1,a			;083e	cb 4f		. O
	jr z,HD_C0_NO_LBA	;0840	28 02		( .
	set 6,h			;0842	cb f4		. .
HD_C0_NO_LBA:
				; = HD_C0_NO_LBA (src: FUNC_4x.ASM:511, BIOS-TT 0271ac3)
	ld bc,04152h		;0844	01 52 41	. R A
	out (c),h		;0847	ed 61		. a
	ld a,(0c60ch)		;0849	3a 0c c6	: . .
	ld bc,InitCpuPorts+1	;084c	01 52 01	. R .
	out (c),a		;084f	ed 79		. y
	ld a,d			;0851	7a		z
	out (0e2h),a		;0852	d3 e2		. .
	ld a,091h		;0854	3e 91		> .
	call HD_CMD_EXE		;0856	cd 5a 08	. Z .
	ret			;0859	c9		.
HD_CMD_EXE:
				; = HD_CMD_EXE (src: FUNC_4x.ASM:526, BIOS-TT 0271ac3)
	call HD_WAIT		;085a	cd 63 08	. c .
	ret c			;085d	d8		.
	ld bc,04153h		;085e	01 53 41	. S A
	out (c),a		;0861	ed 79		. y
HD_WAIT:
				; = HD_WAIT (src: FUNC_4x.ASM:531, BIOS-TT 0271ac3)
	push de			;0863	d5		.
	push bc			;0864	c5		.
	push af			;0865	f5		.
	ld de,Reset		;0866	11 00 00	. . .
HD_WAIT1:
				; = HD_WAIT1 (src: FUNC_4x.ASM:536, BIOS-TT 0271ac3)
	ld bc,04053h		;0869	01 53 40	. S @
	in a,(c)		;086c	ed 78		. x
	bit 7,a			;086e	cb 7f		. .
	jr z,HD_W_EXIT		;0870	28 0a		( .
	dec de			;0872	1b		.
	ld a,d			;0873	7a		z
	or e			;0874	b3		.
	jr nz,HD_WAIT1		;0875	20 f2		  .
	pop af			;0877	f1		.
	pop bc			;0878	c1		.
	pop de			;0879	d1		.
	scf			;087a	37		7
	ret			;087b	c9		.
HD_W_EXIT:
				; = HD_W_EXIT (src: FUNC_4x.ASM:550, BIOS-TT 0271ac3)
	pop af			;087c	f1		.
	pop bc			;087d	c1		.
	pop de			;087e	d1		.
	and a			;087f	a7		.
	ret			;0880	c9		.
HDD_LBA:
				; = HDD_LBA (src: FUNC_4x.ASM:560, BIOS-TT 0271ac3)
				; = HD_CALC_SECS (src: FUNC_4x.ASM:569, BIOS-TT 0271ac3)
	pop bc			;0881	c1		.
	ld l,e			;0882	6b		k
	ld e,d			;0883	5a		Z
	defb 0ddh,055h ;ld d,ixl	;0884	dd 55		. U
	defb 0ddh,07ch ;ld a,ixh	;0886	dd 7c		. |
	and 00fh		;0888	e6 0f		. .
	ld h,a			;088a	67		g
	ret			;088b	c9		.
HD_CALC_SECS:
				; = HD_CALC_SECS (src: FUNC_4x.ASM:595, BIOS-TT 0271ac3)
	push bc			;088c	c5		.
	ld bc,04152h		;088d	01 52 41	. R A
	dec b			;0890	05		.
	in a,(c)		;0891	ed 78		. x
	bit 6,a			;0893	cb 77		. w
	jr nz,HDD_LBA		;0895	20 ea		  .
	pop bc			;0897	c1		.
	defb 0ddh,07ch ;ld a,ixh	;0898	dd 7c		. |
	and a			;089a	a7		.
	scf			;089b	37		7
	ret nz			;089c	c0		.
	push ix			;089d	dd e5		. .
	pop hl			;089f	e1		.
	in a,(0e2h)		;08a0	db e2		. .
	ld c,a			;08a2	4f		O
	ld a,0feh		;08a3	3e fe		> .
	out (0e2h),a		;08a5	d3 e2		. .
	ld a,c			;08a7	79		y
	ld (0c107h),a		;08a8	32 07 c1	2 . .
	ld bc,(0c604h)		;08ab	ed 4b 04 c6	. K . .
	ld a,010h		;08af	3e 10		> .
	scf			;08b1	37		7
DIV_LOOP:
				; = DIV_LOOP (src: FUNC_4x.ASM:624, BIOS-TT 0271ac3)
	ex de,hl		;08b2	eb		.
	add hl,hl		;08b3	29		)
	ex de,hl		;08b4	eb		.
	adc hl,hl		;08b5	ed 6a		. j
	sbc hl,bc		;08b7	ed 42		. B
	jr nc,NO_ADD		;08b9	30 06		0 .
	add hl,bc		;08bb	09		.
	dec a			;08bc	3d		=
	jr nz,DIV_LOOP		;08bd	20 f3		  .
	jr DIV_END		;08bf	18 04		. .
NO_ADD:
				; = NO_ADD (src: FUNC_4x.ASM:636, BIOS-TT 0271ac3)
	inc de			;08c1	13		.
	dec a			;08c2	3d		=
	jr nz,DIV_LOOP		;08c3	20 ed		  .
DIV_END:
				; = DIV_END (src: FUNC_4x.ASM:640, BIOS-TT 0271ac3)
	ld a,(0c60ch)		;08c5	3a 0c c6	: . .
	ld b,000h		;08c8	06 00		. .
	ld c,a			;08ca	4f		O
	xor a			;08cb	af		.
HD_CALC_LOOP2:
				; = HD_CALC_LOOP2 (src: FUNC_4x.ASM:650, BIOS-TT 0271ac3)
	sbc hl,bc		;08cc	ed 42		. B
	inc a			;08ce	3c		<
	jr nc,HD_CALC_LOOP2	;08cf	30 fb		0 .
	dec a			;08d1	3d		=
	add hl,bc		;08d2	09		.
	inc l			;08d3	2c		,
	ld h,a			;08d4	67		g
	ld a,(0c107h)		;08d5	3a 07 c1	: . .
	out (0e2h),a		;08d8	d3 e2		. .
	pop bc			;08da	c1		.
	and a			;08db	a7		.
	ret			;08dc	c9		.
Fn47_HDD_PART:
	pop af			;08dd	f1		.
FN_HDD_PART:
				; = FN_HDD_PART (src: FUNC_4x.ASM:667, BIOS-TT 0271ac3)
	bit 0,a			;08de	cb 47		. G
	ld a,021h		;08e0	3e 21		> !
	jr z,FN_HDD_PART_SET_CH	;08e2	28 02		( .
	ld a,001h		;08e4	3e 01		> .
FN_HDD_PART_SET_CH:
				; = FN_HDD_PART.SET_CH (src: FUNC_4x.ASM:672, BIOS-TT 0271ac3)
	out (0bch),a		;08e6	d3 bc		. .
	ret			;08e8	c9		.
FnEE_RST_CONF_AY8910:
				; = RST_CONF (src: FUNC_SERVICE.asm:13, BIOS-TT 0271ac3)
				; = RST_CONF.AY8910 (src: FUNC_SERVICE.asm:15, BIOS-TT 0271ac3)
	ld d,035h		;08e9	16 35		. 5
	call FnF6_CMOS_RD	;08eb	cd ba 32	. . 2
	or 001h			;08ee	f6 01		. .
	call FnF7_CMOS_WR	;08f0	cd 99 32	. . 2
	ld bc,l204eh		;08f3	01 4e 20	. N  
	in a,(c)		;08f6	ed 78		. x
	and 0feh		;08f8	e6 fe		. .
	out (c),a		;08fa	ed 79		. y
	ld a,0eah		;08fc	3e ea		> .
	ld de,0fffch		;08fe	11 fc ff	. . .
	jr RST_CONF_INT_PLD	;0901	18 53		. S
FnF0_RST_CONF_SP97_1:
				; = RST_CONF.SP97_1 (src: FUNC_SERVICE.asm:33, BIOS-TT 0271ac3)
	ld d,035h		;0903	16 35		. 5
	call FnF6_CMOS_RD	;0905	cd ba 32	. . 2
	and 0feh		;0908	e6 fe		. .
	call FnF7_CMOS_WR	;090a	cd 99 32	. . 2
	ld bc,l204eh		;090d	01 4e 20	. N  
	in a,(c)		;0910	ed 78		. x
	and 0feh		;0912	e6 fe		. .
	out (c),a		;0914	ed 79		. y
	ld a,0ech		;0916	3e ec		> .
	ld de,0fffeh		;0918	11 fe ff	. . .
	jr RST_CONF_INT_PLD	;091b	18 39		. 9
FnF1_RST_CONF_SP97_2:
				; = RST_CONF.SP97_2 (src: FUNC_SERVICE.asm:50, BIOS-TT 0271ac3)
	ld bc,l204eh		;091d	01 4e 20	. N  
	in a,(c)		;0920	ed 78		. x
	or 001h			;0922	f6 01		. .
	out (c),a		;0924	ed 79		. y
	ld a,0eeh		;0926	3e ee		> .
	ld de,0fffdh		;0928	11 fd ff	. . .
	jr RST_CONF_INT_PLD	;092b	18 29		. )
FnF3_RST_CONF_CUSTOM:
				; = RST_CONF.CUSTOM (src: FUNC_SERVICE.asm:61, BIOS-TT 0271ac3)
	cp 080h			;092d	fe 80		. .
	jr nc,l093fh		;092f	30 0e		0 .
	ld c,0e2h		;0931	0e e2		. .
	in b,(c)		;0933	ed 40		. @
	out (c),a		;0935	ed 79		. y
	ld de,(0c090h)		;0937	ed 5b 90 c0	. [ . .
	out (c),b		;093b	ed 41		. A
	jr RST_CONF_INT_PLD	;093d	18 17		. .
l093fh:
	cp 0ech			;093f	fe ec		. .
	ld de,0fffeh		;0941	11 fe ff	. . .
	jr z,RST_CONF_INT_PLD	;0944	28 10		( .
	cp 0eeh			;0946	fe ee		. .
	ld de,0fffdh		;0948	11 fd ff	. . .
	jr z,RST_CONF_INT_PLD	;094b	28 09		( .
	cp 0eah			;094d	fe ea		. .
	ld de,0fffch		;094f	11 fc ff	. . .
	jr z,RST_CONF_INT_PLD	;0952	28 02		( .
	scf			;0954	37		7
	ret			;0955	c9		.
RST_CONF_INT_PLD:
				; = RST_CONF.INT_PLD (src: FUNC_SERVICE.asm:71, BIOS-TT 0271ac3)
	ld c,0e2h		;0956	0e e2		. .
	in b,(c)		;0958	ed 40		. @
	ld a,0feh		;095a	3e fe		> .
	out (c),a		;095c	ed 79		. y
	ld (0c13eh),de		;095e	ed 53 3e c1	. S > .
	out (c),b		;0962	ed 41		. A
	ld a,e			;0964	7b		{
	cp 0ffh			;0965	fe ff		. .
	ld a,080h		;0967	3e 80		> .
	jr z,RST_CONF_YES_CBL	;0969	28 01		( .
	xor a			;096b	af		.
RST_CONF_YES_CBL:
				; = RST_CONF.YES_CBL (src: FUNC_SERVICE.asm:84, BIOS-TT 0271ac3)
	ld bc,l004eh		;096c	01 4e 00	. N .
	out (c),a		;096f	ed 79		. y
	ld a,e			;0971	7b		{
	or 0feh			;0972	f6 fe		. .
	ld bc,l204eh		;0974	01 4e 20	. N  
	out (c),a		;0977	ed 79		. y
	xor a			;0979	af		.
	ret			;097a	c9		.
l097bh:
	ld a,010h		;097b	3e 10		> .
	ld bc,l1ffdh		;097d	01 fd 1f	. . .
	out (c),a		;0980	ed 79		. y
	ld a,0a0h		;0982	3e a0		> .
	out (0e2h),a		;0984	d3 e2		. .
	ld a,002h		;0986	3e 02		> .
	out (07ch),a		;0988	d3 7c		. |
REINIT_loop2:
				; = REINIT.loop2 (src: FUNC_SERVICE.asm:234, BIOS-TT 0271ac3)
	ld (0c000h),a		;098a	32 00 c0	2 . .
	jr REINIT_loop2		;098d	18 fb		. .

; BLOCK 'Data098F' (start 0x098f end 0x0991)
Data098F:
	defb 0f3h		;098f	f3		.
	defb 076h		;0990	76		v
REINIT_MD_NO_INC_H:
				; = REINIT.MD_NO_INC_H (src: FLEX.asm:22, BIOS-TT 0271ac3)
				; = INIT_PAGES (src: FLEX.asm:40, BIOS-TT 0271ac3)
	and a			;0991	a7		.
	ld e,a			;0992	5f		_
	ex af,af'		;0993	08		.
	ld a,e			;0994	7b		{
SCORPION_256_MODE:
				; = SCORPION_256_MODE (src: FLEX.asm:75, BIOS-TT 0271ac3)
	ld d,010h		;0995	16 10		. .
	and 030h		;0997	e6 30		. 0
ALL_MODE:
				; = ALL_MODE (src: FLEX.asm:79, BIOS-TT 0271ac3)
	out (082h),a		;0999	d3 82		. .
	xor 005h		;099b	ee 05		. .
	out (0a2h),a		;099d	d3 a2		. .
	xor 007h		;099f	ee 07		. .
	out (0c2h),a		;09a1	d3 c2		. .
	or 00fh			;09a3	f6 0f		. .
	ld e,a			;09a5	5f		_
ALL_MODE_loop:
				; = ALL_MODE.loop (src: FLEX.asm:88, BIOS-TT 0271ac3)
	dec d			;09a6	15		.
	ld a,d			;09a7	7a		z
	rlca			;09a8	07		.
	and 010h		;09a9	e6 10		. .
	ld bc,l1ffdh		;09ab	01 fd 1f	. . .
	out (c),a		;09ae	ed 79		. y
	ld a,d			;09b0	7a		z
	and 007h		;09b1	e6 07		. .
	or 040h			;09b3	f6 40		. @
	ld b,07fh		;09b5	06 7f		. .
	out (c),a		;09b7	ed 79		. y
	ld a,e			;09b9	7b		{
	out (0e2h),a		;09ba	d3 e2		. .
	dec e			;09bc	1d		.
	dec d			;09bd	15		.
	inc d			;09be	14		.
	jr nz,ALL_MODE_loop	;09bf	20 e5		  .
INIT_VIDEO_REG:
				; = INIT_VIDEO_REG (src: FLEX.asm:110, BIOS-TT 0271ac3)
	xor a			;09c1	af		.
	out (089h),a		;09c2	d3 89		. .
	out (0c9h),a		;09c4	d3 c9		. .
	ld hl,04000h		;09c6	21 00 40	! . @
	ld de,04000h		;09c9	11 00 40	. . @
	ld bc,l1b00h		;09cc	01 00 1b	. . .
	ex af,af'		;09cf	08		.
	bit 6,a			;09d0	cb 77		. w
	jr nz,NO_SCREEN_ALT	;09d2	20 18		  .
	ex af,af'		;09d4	08		.
	ld a,007h		;09d5	3e 07		> .
	ld bc,07ffdh		;09d7	01 fd 7f	. . .
	out (c),a		;09da	ed 79		. y
	ld hl,0c000h		;09dc	21 00 c0	! . .
	ld de,0c000h		;09df	11 00 c0	. . .
	ld bc,l1b00h		;09e2	01 00 1b	. . .
	xor a			;09e5	af		.
	ld bc,07ffdh		;09e6	01 fd 7f	. . .
	out (c),a		;09e9	ed 79		. y
	ex af,af'		;09eb	08		.
NO_SCREEN_ALT:
				; = NO_SCREEN_ALT (src: FLEX.asm:145, BIOS-TT 0271ac3)
	and a			;09ec	a7		.
	jp (ix)			;09ed	dd e9		. .
SET_PAL_IBM:
				; = SET_PAL_IBM (src: FLEX.asm:163, BIOS-TT 0271ac3)
	ld de,08000h		;09ef	11 00 80	. . .
	jr SET_PAL_		;09f2	18 03		. .
SET_PAL_ZX:
				; = SET_PAL_ZX (src: FLEX.asm:166, BIOS-TT 0271ac3)
	ld de,Reset		;09f4	11 00 00	. . .
SET_PAL_:
				; = SET_PAL_ (src: FLEX.asm:168, BIOS-TT 0271ac3)
	push ix			;09f7	dd e5		. .
	in a,(089h)		;09f9	db 89		. .
	push af			;09fb	f5		.
	in a,(0e2h)		;09fc	db e2		. .
	ex af,af'		;09fe	08		.
	ld a,050h		;09ff	3e 50		> P
	out (0e2h),a		;0a01	d3 e2		. .
	ld ix,0c3f0h		;0a03	dd 21 f0 c3	. ! . .
SET_PAL_ZX1:
				; = SET_PAL_ZX1 (src: FLEX.asm:178, BIOS-TT 0271ac3)
	call GENERATE_PAL1	;0a07	cd 30 0a	. 0 .
	ld a,e			;0a0a	7b		{
	out (089h),a		;0a0b	d3 89		. .
	ld (ix+000h),l		;0a0d	dd 75 00	. u .
	ld (ix+001h),b		;0a10	dd 70 01	. p .
	ld (ix+002h),c		;0a13	dd 71 02	. q .
	ld (ix+003h),h		;0a16	dd 74 03	. t .
	inc e			;0a19	1c		.
	jr nz,SET_PAL_ZX1	;0a1a	20 eb		  .
	ld bc,l0004h		;0a1c	01 04 00	. . .
	add ix,bc		;0a1f	dd 09		. .
	inc d			;0a21	14		.
	ld a,d			;0a22	7a		z
	and 003h		;0a23	e6 03		. .
	jr nz,SET_PAL_ZX1	;0a25	20 e0		  .
	ex af,af'		;0a27	08		.
	out (0e2h),a		;0a28	d3 e2		. .
	pop af			;0a2a	f1		.
	out (089h),a		;0a2b	d3 89		. .
	pop ix			;0a2d	dd e1		. .
	ret			;0a2f	c9		.
GENERATE_PAL1:
				; = GENERATE_PAL1 (src: FLEX.asm:205, BIOS-TT 0271ac3)
	xor a			;0a30	af		.
	ld c,a			;0a31	4f		O
	ld b,a			;0a32	47		G
	ld l,a			;0a33	6f		o
	bit 7,d			;0a34	cb 7a		. z
	jr nz,GENERATE_IBM	;0a36	20 49		  I
	bit 1,d			;0a38	cb 4a		. J
	jr nz,GEM_PAL_FLH	;0a3a	20 1d		  .
GEM_PAL_NOF:
				; = GEM_PAL_NOF (src: FLEX.asm:216, BIOS-TT 0271ac3)
	bit 0,d			;0a3c	cb 42		. B
	jr z,GEN_PAL_PAP	;0a3e	28 21		( !
GEN_PAL_INK:
				; = GEN_PAL_INK (src: FLEX.asm:220, BIOS-TT 0271ac3)
	ld a,0c8h		;0a40	3e c8		> .
	bit 6,e			;0a42	cb 73		. s
	jr z,GEN_PAL_NOI1	;0a44	28 02		( .
	ld a,0f0h		;0a46	3e f0		> .
GEN_PAL_NOI1:
				; = GEN_PAL_NOI1 (src: FLEX.asm:225, BIOS-TT 0271ac3)
	bit 0,e			;0a48	cb 43		. C
	jr z,GEN_PP_NO1		;0a4a	28 01		( .
	ld c,a			;0a4c	4f		O
GEN_PP_NO1:
				; = GEN_PP_NO1 (src: FLEX.asm:229, BIOS-TT 0271ac3)
	bit 1,e			;0a4d	cb 4b		. K
	jr z,GEN_PP_NO2		;0a4f	28 01		( .
	ld l,a			;0a51	6f		o
GEN_PP_NO2:
				; = GEN_PP_NO2 (src: FLEX.asm:233, BIOS-TT 0271ac3)
	bit 2,e			;0a52	cb 53		. S
	jr z,GEN_PP_NO3		;0a54	28 01		( .
	ld b,a			;0a56	47		G
GEN_PP_NO3:
				; = GEN_PP_NO3 (src: FLEX.asm:237, BIOS-TT 0271ac3)
	jr GEN_PP_NO6		;0a57	18 1f		. .
GEM_PAL_FLH:
				; = GEM_PAL_FLH (src: FLEX.asm:241, BIOS-TT 0271ac3)
	bit 7,e			;0a59	cb 7b		. {
	jr z,GEM_PAL_NOF	;0a5b	28 df		( .
	bit 0,d			;0a5d	cb 42		. B
	jr z,GEN_PAL_INK	;0a5f	28 df		( .
GEN_PAL_PAP:
				; = GEN_PAL_PAP (src: FLEX.asm:247, BIOS-TT 0271ac3)
	ld a,0c8h		;0a61	3e c8		> .
	bit 6,e			;0a63	cb 73		. s
	jr z,GEN_PAL_NOI2	;0a65	28 02		( .
	ld a,0f0h		;0a67	3e f0		> .
GEN_PAL_NOI2:
				; = GEN_PAL_NOI2 (src: FLEX.asm:252, BIOS-TT 0271ac3)
	bit 3,e			;0a69	cb 5b		. [
	jr z,GEN_PP_NO4		;0a6b	28 01		( .
	ld c,a			;0a6d	4f		O
GEN_PP_NO4:
				; = GEN_PP_NO4 (src: FLEX.asm:256, BIOS-TT 0271ac3)
	bit 4,e			;0a6e	cb 63		. c
	jr z,GEN_PP_NO5		;0a70	28 01		( .
	ld l,a			;0a72	6f		o
GEN_PP_NO5:
				; = GEN_PP_NO5 (src: FLEX.asm:260, BIOS-TT 0271ac3)
	bit 5,e			;0a73	cb 6b		. k
	jr z,GEN_PP_NO6		;0a75	28 01		( .
	ld b,a			;0a77	47		G
GEN_PP_NO6:
				; = GEN_PP_NO6 (src: FLEX.asm:264, BIOS-TT 0271ac3)
	ld a,c			;0a78	79		y
	and a			;0a79	a7		.
	rra			;0a7a	1f		.
	add a,l			;0a7b	85		.
	rra			;0a7c	1f		.
	add a,b			;0a7d	80		.
	rra			;0a7e	1f		.
	ld h,a			;0a7f	67		g
	ret			;0a80	c9		.
GENERATE_IBM:
				; = GENERATE_IBM (src: FLEX.asm:279, BIOS-TT 0271ac3)
	bit 7,e			;0a81	cb 7b		. {
	jr z,GEN_IBM_NO_FLH	;0a83	28 04		( .
	bit 1,d			;0a85	cb 4a		. J
	jr z,GEN_IBM_PAPER	;0a87	28 30		( 0
GEN_IBM_NO_FLH:
				; = GEN_IBM_NO_FLH (src: FLEX.asm:285, BIOS-TT 0271ac3)
	bit 0,d			;0a89	cb 42		. B
	jr z,GEN_IBM_PAPER	;0a8b	28 2c		( ,
GEN_IBM_INK:
				; = GEN_IBM_INK (src: FLEX.asm:289, BIOS-TT 0271ac3)
				; = GEN_NO_INTENS (src: FLEX.asm:291, BIOS-TT 0271ac3)
	ld a,0a8h		;0a8d	3e a8		> .
GEN_INTENS:
				; = GEN_INTENS (src: FLEX.asm:293, BIOS-TT 0271ac3)
	bit 0,e			;0a8f	cb 43		. C
	jr z,GEN_PPI_NO4	;0a91	28 01		( .
	ld c,a			;0a93	4f		O
GEN_PPI_NO4:
				; = GEN_PPI_NO4 (src: FLEX.asm:297, BIOS-TT 0271ac3)
	bit 2,e			;0a94	cb 53		. S
	jr z,GEN_PPI_NO5	;0a96	28 01		( .
	ld l,a			;0a98	6f		o
GEN_PPI_NO5:
				; = GEN_PPI_NO5 (src: FLEX.asm:301, BIOS-TT 0271ac3)
	bit 1,e			;0a99	cb 4b		. K
	jr z,GEN_PPI_NO6	;0a9b	28 01		( .
	ld b,a			;0a9d	47		G
GEN_PPI_NO6:
				; = GEN_PPI_NO6 (src: FLEX.asm:305, BIOS-TT 0271ac3)
	ld a,e			;0a9e	7b		{
	and 00fh		;0a9f	e6 0f		. .
	cp 006h			;0aa1	fe 06		. .
	jr nz,no_correct	;0aa3	20 02		  .
	ld b,054h		;0aa5	06 54		. T
no_correct:
				; = no_correct (src: FLEX.asm:312, BIOS-TT 0271ac3)
	bit 3,e			;0aa7	cb 5b		. [
	jr z,GEN_PP_NO6		;0aa9	28 cd		( .
	ld a,054h		;0aab	3e 54		> T
	add a,c			;0aad	81		.
	ld c,a			;0aae	4f		O
	ld a,054h		;0aaf	3e 54		> T
	add a,b			;0ab1	80		.
	ld b,a			;0ab2	47		G
	ld a,054h		;0ab3	3e 54		> T
	add a,l			;0ab5	85		.
	ld l,a			;0ab6	6f		o
	jr GEN_PP_NO6		;0ab7	18 bf		. .
GEN_IBM_PAPER:
				; = GEN_IBM_PAPER (src: FLEX.asm:328, BIOS-TT 0271ac3)
	ld a,0a8h		;0ab9	3e a8		> .
	bit 4,e			;0abb	cb 63		. c
	jr z,GEN_PPI_NO4X	;0abd	28 01		( .
	ld c,a			;0abf	4f		O
GEN_PPI_NO4X:
				; = GEN_PPI_NO4X (src: FLEX.asm:333, BIOS-TT 0271ac3)
	bit 6,e			;0ac0	cb 73		. s
	jr z,GEN_PPI_NO5X	;0ac2	28 01		( .
	ld l,a			;0ac4	6f		o
GEN_PPI_NO5X:
				; = GEN_PPI_NO5X (src: FLEX.asm:337, BIOS-TT 0271ac3)
	bit 5,e			;0ac5	cb 6b		. k
	jr z,GEN_PPI_NO6X	;0ac7	28 01		( .
	ld b,a			;0ac9	47		G
GEN_PPI_NO6X:
				; = GEN_PPI_NO6X (src: FLEX.asm:341, BIOS-TT 0271ac3)
	ld a,e			;0aca	7b		{
	and 070h		;0acb	e6 70		. p
	cp 060h			;0acd	fe 60		. `
	jr nz,no_correct2	;0acf	20 02		  .
	ld b,054h		;0ad1	06 54		. T
no_correct2:
				; = no_correct2 (src: FLEX.asm:348, BIOS-TT 0271ac3)
	jr GEN_PP_NO6		;0ad3	18 a3		. .
SET_PAL_GRAF:
				; = SET_PAL_GRAF (src: FLEX.asm:356, BIOS-TT 0271ac3)
	push ix			;0ad5	dd e5		. .
	in a,(089h)		;0ad7	db 89		. .
	push af			;0ad9	f5		.
	in a,(0e2h)		;0ada	db e2		. .
	ex af,af'		;0adc	08		.
	ld a,050h		;0add	3e 50		> P
	out (0e2h),a		;0adf	d3 e2		. .
	ld ix,0c3e0h		;0ae1	dd 21 e0 c3	. ! . .
	ld a,d			;0ae5	7a		z
	and 003h		;0ae6	e6 03		. .
	add a,a			;0ae8	87		.
	add a,a			;0ae9	87		.
	ld e,a			;0aea	5f		_
	ld d,000h		;0aeb	16 00		. .
	add ix,de		;0aed	dd 19		. .
	ld e,000h		;0aef	1e 00		. .
	xor a			;0af1	af		.
	ld b,a			;0af2	47		G
	ld c,a			;0af3	4f		O
	ld l,a			;0af4	6f		o
	ld h,a			;0af5	67		g
SET_PAL_GR1:
				; = SET_PAL_GR1 (src: FLEX.asm:384, BIOS-TT 0271ac3)
	ld a,e			;0af6	7b		{
	out (089h),a		;0af7	d3 89		. .
	ld (ix+000h),l		;0af9	dd 75 00	. u .
	ld (ix+001h),b		;0afc	dd 70 01	. p .
	ld (ix+002h),c		;0aff	dd 71 02	. q .
	ld (ix+003h),h		;0b02	dd 74 03	. t .
	call GENERATE_PAL3	;0b05	cd 31 0b	. 1 .
	inc e			;0b08	1c		.
	ld a,e			;0b09	7b		{
	cp 028h			;0b0a	fe 28		. (
	jr nz,SET_PAL_GR1	;0b0c	20 e8		  .
	xor a			;0b0e	af		.
	ld b,a			;0b0f	47		G
	ld c,a			;0b10	4f		O
	ld l,a			;0b11	6f		o
	ld h,a			;0b12	67		g
SET_PAL_GR2:
				; = SET_PAL_GR2 (src: FLEX.asm:406, BIOS-TT 0271ac3)
	ld a,e			;0b13	7b		{
	out (089h),a		;0b14	d3 89		. .
	ld (ix+000h),l		;0b16	dd 75 00	. u .
	ld (ix+001h),b		;0b19	dd 70 01	. p .
	ld (ix+002h),c		;0b1c	dd 71 02	. q .
	ld (ix+003h),h		;0b1f	dd 74 03	. t .
	call GENERATE_PAL2	;0b22	cd 40 0b	. @ .
	inc e			;0b25	1c		.
	jr nz,SET_PAL_GR2	;0b26	20 eb		  .
	ex af,af'		;0b28	08		.
	out (0e2h),a		;0b29	d3 e2		. .
	pop af			;0b2b	f1		.
	out (089h),a		;0b2c	d3 89		. .
	pop ix			;0b2e	dd e1		. .
	ret			;0b30	c9		.
GENERATE_PAL3:
				; = GENERATE_PAL3 (src: FLEX.asm:430, BIOS-TT 0271ac3)
	ld a,b			;0b31	78		x
	add a,006h		;0b32	c6 06		. .
	ld b,a			;0b34	47		G
	ld c,a			;0b35	4f		O
	ld l,a			;0b36	6f		o
	ld a,c			;0b37	79		y
	and a			;0b38	a7		.
	rra			;0b39	1f		.
	add a,l			;0b3a	85		.
	rra			;0b3b	1f		.
	add a,b			;0b3c	80		.
	rra			;0b3d	1f		.
	ld h,a			;0b3e	67		g
	ret			;0b3f	c9		.
GENERATE_PAL2:
				; = GENERATE_PAL2 (src: FLEX.asm:448, BIOS-TT 0271ac3)
	ld a,c			;0b40	79		y
	add a,032h		;0b41	c6 32		. 2
	ld c,a			;0b43	4f		O
	jr nc,GEN_PAL2_L1	;0b44	30 12		0 .
	ld c,000h		;0b46	0e 00		. .
	ld a,l			;0b48	7d		}
	add a,032h		;0b49	c6 32		. 2
	ld l,a			;0b4b	6f		o
	jr nc,GEN_PAL2_L1	;0b4c	30 0a		0 .
	ld l,000h		;0b4e	2e 00		. .
	ld a,b			;0b50	78		x
	add a,032h		;0b51	c6 32		. 2
	ld b,a			;0b53	47		G
	jr nc,GEN_PAL2_L1	;0b54	30 02		0 .
	ld b,000h		;0b56	06 00		. .
GEN_PAL2_L1:
				; = GEN_PAL2_L1 (src: FLEX.asm:465, BIOS-TT 0271ac3)
	ld a,c			;0b58	79		y
	and a			;0b59	a7		.
	rr a			;0b5a	cb 1f		. .
	add a,l			;0b5c	85		.
	rr a			;0b5d	cb 1f		. .
	add a,b			;0b5f	80		.
	rr a			;0b60	cb 1f		. .
	ld h,a			;0b62	67		g
	ret			;0b63	c9		.
FnF2_FN_SYNC:
				; = ~FN_SYNC.old_mode (src: FLEX.asm:562, BIOS-TT 0271ac3)
	and a			;0b64	a7		.
	jr z,FN_SYNC_SetDefLines	;0b65	28 0b		( .
	dec a			;0b67	3d		=
	jr z,FN_SYNC_INT_SCORP	;0b68	28 2c		( ,
	dec a			;0b6a	3d		=
	jr z,FN_SYNC_INT_PENT	;0b6b	28 2f		( /
	dec a			;0b6d	3d		=
	jr z,FN_SYNC_INT_ORIG	;0b6e	28 20		(  
FN_SYNC_error:
				; = FN_SYNC.error (src: FLEX.asm:573, BIOS-TT 0271ac3)
	scf			;0b70	37		7
	ret			;0b71	c9		.
FN_SYNC_SetDefLines:
				; = FN_SYNC.SetDefLines (src: FLEX.asm:576, BIOS-TT 0271ac3)
	ld c,0e2h		;0b72	0e e2		. .
	in b,(c)		;0b74	ed 40		. @
	ld a,0feh		;0b76	3e fe		> .
	out (c),a		;0b78	ed 79		. y
	ld de,(0c138h)		;0b7a	ed 5b 38 c1	. [ 8 .
	out (c),b		;0b7e	ed 41		. A
	ld hl,SCREEN_TABLES_PENTAGON	;0b80	21 62 0c	! b .
	and a			;0b83	a7		.
	sbc hl,de		;0b84	ed 52		. R
	jr z,FN_SYNC_INT_PENT	;0b86	28 14		( .
	ld hl,l0c75h		;0b88	21 75 0c	! u .
	and a			;0b8b	a7		.
	sbc hl,de		;0b8c	ed 52		. R
	jr z,FN_SYNC_INT_SCORP	;0b8e	28 06		( .
FN_SYNC_INT_ORIG:
				; = FN_SYNC.INT_ORIG (src: FLEX.asm:625, BIOS-TT 0271ac3)
	ld ix,l0c8bh		;0b90	dd 21 8b 0c	. ! . .
	jr FN_SYNC_PROG_SCR	;0b94	18 0c		. .
FN_SYNC_INT_SCORP:
				; = FN_SYNC.INT_SCORP (src: FLEX.asm:628, BIOS-TT 0271ac3)
	ld ix,l0c75h		;0b96	dd 21 75 0c	. ! u .
	jr FN_SYNC_PROG_SCR	;0b9a	18 06		. .
FN_SYNC_INT_PENT:
				; = FN_SYNC.INT_PENT (src: FLEX.asm:631, BIOS-TT 0271ac3)
	ld ix,SCREEN_TABLES_PENTAGON	;0b9c	dd 21 62 0c	. ! b .
	jr FN_SYNC_PROG_SCR	;0ba0	18 00		. .
FN_SYNC_PROG_SCR:
				; = FN_SYNC.PROG_SCR (src: FLEX.asm:635, BIOS-TT 0271ac3)
	in a,(0e2h)		;0ba2	db e2		. .
	ex af,af'		;0ba4	08		.
	ld a,0feh		;0ba5	3e fe		> .
	out (0e2h),a		;0ba7	d3 e2		. .
	ex af,af'		;0ba9	08		.
	ld (0c107h),a		;0baa	32 07 c1	2 . .
	in a,(089h)		;0bad	db 89		. .
	ld (0c11dh),a		;0baf	32 1d c1	2 . .
	ld (0c138h),ix		;0bb2	dd 22 38 c1	. " 8 .
	ld a,000h		;0bb6	3e 00		> .
FN_SYNC_loop_1:
				; = FN_SYNC.loop_1 (src: FLEX.asm:649, BIOS-TT 0271ac3)
	out (089h),a		;0bb8	d3 89		. .
	ex af,af'		;0bba	08		.
	ld a,050h		;0bbb	3e 50		> P
	out (0e2h),a		;0bbd	d3 e2		. .
	ld hl,0c300h		;0bbf	21 00 c3	! . .
FN_SYNC_loop_2:
				; = FN_SYNC.loop_2 (src: FLEX.asm:659, BIOS-TT 0271ac3)
	ld c,(ix+000h)		;0bc2	dd 4e 00	. N .
FN_SYNC_loop_3:
				; = FN_SYNC.loop_3 (src: FLEX.asm:662, BIOS-TT 0271ac3)
	ld e,(ix+001h)		;0bc5	dd 5e 01	. ^ .
	ld d,(ix+002h)		;0bc8	dd 56 02	. V .
FN_SYNC_loop_4:
				; = FN_SYNC.loop_4 (src: FLEX.asm:667, BIOS-TT 0271ac3)
	ld a,(de)		;0bcb	1a		.
	inc de			;0bcc	13		.
	and a			;0bcd	a7		.
	jr z,FN_SYNC_loop_4_exit	;0bce	28 1f		( .
	ld b,a			;0bd0	47		G
	ld a,(de)		;0bd1	1a		.
	inc de			;0bd2	13		.
FN_SYNC_loop_5:
				; = FN_SYNC.loop_5 (src: FLEX.asm:676, BIOS-TT 0271ac3)
	ld (hl),a		;0bd3	77		w
	inc l			;0bd4	2c		,
	ld (hl),000h		;0bd5	36 00		6 .
	inc l			;0bd7	2c		,
	ld (hl),000h		;0bd8	36 00		6 .
	ex af,af'		;0bda	08		.
	inc a			;0bdb	3c		<
	out (089h),a		;0bdc	d3 89		. .
	ex af,af'		;0bde	08		.
	ld (hl),000h		;0bdf	36 00		6 .
	dec l			;0be1	2d		-
	ld (hl),000h		;0be2	36 00		6 .
	dec l			;0be4	2d		-
	ld (hl),a		;0be5	77		w
	ex af,af'		;0be6	08		.
	inc a			;0be7	3c		<
	out (089h),a		;0be8	d3 89		. .
	ex af,af'		;0bea	08		.
	djnz FN_SYNC_loop_5	;0beb	10 e6		. .
	jr FN_SYNC_loop_4	;0bed	18 dc		. .
FN_SYNC_loop_4_exit:
				; = FN_SYNC.loop_4_exit (src: FLEX.asm:703, BIOS-TT 0271ac3)
	inc hl			;0bef	23		#
	inc hl			;0bf0	23		#
	inc hl			;0bf1	23		#
	inc hl			;0bf2	23		#
	in a,(089h)		;0bf3	db 89		. .
	and 080h		;0bf5	e6 80		. .
	out (089h),a		;0bf7	d3 89		. .
	dec c			;0bf9	0d		.
	jr nz,FN_SYNC_loop_3	;0bfa	20 c9		  .
	inc ix			;0bfc	dd 23		. #
	inc ix			;0bfe	dd 23		. #
l0c00h:
	inc ix			;0c00	dd 23		. #
	ld a,(ix+000h)		;0c02	dd 7e 00	. ~ .
	and a			;0c05	a7		.
	jr nz,FN_SYNC_loop_2	;0c06	20 ba		  .
	ld a,0feh		;0c08	3e fe		> .
	out (0e2h),a		;0c0a	d3 e2		. .
	ld ix,(0c138h)		;0c0c	dd 2a 38 c1	. * 8 .
	ex af,af'		;0c10	08		.
	add a,080h		;0c11	c6 80		. .
	jr nc,FN_SYNC_loop_1	;0c13	30 a3		0 .
	ld a,(0c11dh)		;0c15	3a 1d c1	: . .
	out (089h),a		;0c18	d3 89		. .
	ld a,(0c107h)		;0c1a	3a 07 c1	: . .
	out (0e2h),a		;0c1d	d3 e2		. .
	ret			;0c1f	c9		.
SCREEN_TABLES:

; BLOCK 'Data0C20' (start 0x0c20 end 0x0ca1)
				; = SCREEN_TABLES (src: FLEX.asm:762, BIOS-TT 0271ac3)
				; = SCREEN_TABLES.SCR (src: FLEX.asm:767, BIOS-TT 0271ac3)
	defb 029h		;0c20	29		)
	defb 0f8h		;0c21	f8		.
	defb 003h		;0c22	03		.
	defb 0fch		;0c23	fc		.
	defb 004h		;0c24	04		.
	defb 0fch		;0c25	fc		.
	defb 007h		;0c26	07		.
	defb 0fch		;0c27	fc		.
	defb 009h		;0c28	09		.
	defb 0f8h		;0c29	f8		.
	defb 000h		;0c2a	00		.
SCREEN_TABLES_INT:
				; = SCREEN_TABLES.INT (src: FLEX.asm:775, BIOS-TT 0271ac3)
	defb 028h		;0c2b	28		(
	defb 0fch		;0c2c	fc		.
	defb 002h		;0c2d	02		.
	defb 0fdh		;0c2e	fd		.
	defb 006h		;0c2f	06		.
	defb 0fch		;0c30	fc		.
	defb 007h		;0c31	07		.
	defb 0fch		;0c32	fc		.
	defb 009h		;0c33	09		.
	defb 0fch		;0c34	fc		.
	defb 000h		;0c35	00		.
SCREEN_TABLES_INT_SC:
				; = SCREEN_TABLES.INT_SC (src: FLEX.asm:776, BIOS-TT 0271ac3)
	defb 029h		;0c36	29		)
	defb 0f8h		;0c37	f8		.
	defb 001h		;0c38	01		.
	defb 0fdh		;0c39	fd		.
	defb 006h		;0c3a	06		.
	defb 0fch		;0c3b	fc		.
	defb 007h		;0c3c	07		.
	defb 0fch		;0c3d	fc		.
	defb 009h		;0c3e	09		.
	defb 0f8h		;0c3f	f8		.
	defb 000h		;0c40	00		.
SCREEN_TABLES_BLN:
				; = SCREEN_TABLES.BLN (src: FLEX.asm:777, BIOS-TT 0271ac3)
	defb 029h		;0c41	29		)
	defb 0fch		;0c42	fc		.
	defb 003h		;0c43	03		.
	defb 0fch		;0c44	fc		.
	defb 004h		;0c45	04		.
	defb 0fch		;0c46	fc		.
	defb 007h		;0c47	07		.
	defb 0fch		;0c48	fc		.
	defb 009h		;0c49	09		.
	defb 0fch		;0c4a	fc		.
	defb 000h		;0c4b	00		.
SCREEN_TABLES_SNC:
				; = SCREEN_TABLES.SNC (src: FLEX.asm:778, BIOS-TT 0271ac3)
	defb 029h		;0c4c	29		)
	defb 0fch		;0c4d	fc		.
	defb 003h		;0c4e	03		.
	defb 0fch		;0c4f	fc		.
	defb 004h		;0c50	04		.
	defb 0fch		;0c51	fc		.
	defb 007h		;0c52	07		.
	defb 0fch		;0c53	fc		.
	defb 009h		;0c54	09		.
	defb 0fch		;0c55	fc		.
	defb 000h		;0c56	00		.
SCREEN_TABLES_RES:
				; = SCREEN_TABLES.RES (src: FLEX.asm:779, BIOS-TT 0271ac3)
	defb 029h		;0c57	29		)
	defb 0f8h		;0c58	f8		.
	defb 003h		;0c59	03		.
	defb 0feh		;0c5a	fe		.
	defb 004h		;0c5b	04		.
	defb 0feh		;0c5c	fe		.
	defb 007h		;0c5d	07		.
	defb 0feh		;0c5e	fe		.
	defb 009h		;0c5f	09		.
	defb 0f8h		;0c60	f8		.
	defb 000h		;0c61	00		.
SCREEN_TABLES_PENTAGON:
				; = SCREEN_TABLES.PENTAGON (src: FLEX.asm:787, BIOS-TT 0271ac3)
	defb 021h		;0c62	21		!
	defb 020h		;0c63	20		 
	defb 00ch		;0c64	0c		.
	defb 001h		;0c65	01		.
	defb 02bh		;0c66	2b		+
	defb 00ch		;0c67	0c		.
	defb 003h		;0c68	03		.
	defb 04ch		;0c69	4c		L
	defb 00ch		;0c6a	0c		.
	defb 001h		;0c6b	01		.
	defb 041h		;0c6c	41		A
	defb 00ch		;0c6d	0c		.
	defb 001h		;0c6e	01		.
	defb 020h		;0c6f	20		 
	defb 00ch		;0c70	0c		.
	defb 001h		;0c71	01		.
	defb 057h		;0c72	57		W
	defb 00ch		;0c73	0c		.
	defb 000h		;0c74	00		.
l0c75h:
	defb 01fh		;0c75	1f		.
	defb 020h		;0c76	20		 
	defb 00ch		;0c77	0c		.
	defb 001h		;0c78	01		.
	defb 036h		;0c79	36		6
	defb 00ch		;0c7a	0c		.
	defb 001h		;0c7b	01		.
	defb 020h		;0c7c	20		 
	defb 00ch		;0c7d	0c		.
	defb 001h		;0c7e	01		.
	defb 04ch		;0c7f	4c		L
	defb 00ch		;0c80	0c		.
	defb 003h		;0c81	03		.
	defb 04ch		;0c82	4c		L
	defb 00ch		;0c83	0c		.
	defb 001h		;0c84	01		.
	defb 041h		;0c85	41		A
	defb 00ch		;0c86	0c		.
	defb 002h		;0c87	02		.
	defb 057h		;0c88	57		W
	defb 00ch		;0c89	0c		.
	defb 000h		;0c8a	00		.
l0c8bh:
	defb 021h		;0c8b	21		!
	defb 020h		;0c8c	20		 
	defb 00ch		;0c8d	0c		.
	defb 001h		;0c8e	01		.
	defb 04ch		;0c8f	4c		L
	defb 00ch		;0c90	0c		.
	defb 001h		;0c91	01		.
	defb 02bh		;0c92	2b		+
	defb 00ch		;0c93	0c		.
	defb 002h		;0c94	02		.
	defb 04ch		;0c95	4c		L
	defb 00ch		;0c96	0c		.
	defb 001h		;0c97	01		.
	defb 041h		;0c98	41		A
	defb 00ch		;0c99	0c		.
	defb 001h		;0c9a	01		.
	defb 020h		;0c9b	20		 
	defb 00ch		;0c9c	0c		.
	defb 001h		;0c9d	01		.
	defb 057h		;0c9e	57		W
	defb 00ch		;0c9f	0c		.
	defb 000h		;0ca0	00		.
;---------------------------------------------------------------------------
; DcpInit: unpack the port table into RAM page #40 (seen at #C000-#FFFF).
; Stream at #1400: a flag byte, then for each of its 8 bits (MSB first)
; either a literal byte (bit = 1) or a zero (bit = 0). 16 384 bytes out.
; Worked example: flags #60 = 0110 0000 -> 0, literal, literal, 0, 0, 0,
; 0, 0 (two literal bytes follow the flag byte).
; Then map 3 (#F000-#FFFF) = 4 copies of map 0's first 1 KB, which is the
; "write/read, DOS on, PN5 = 0" quarter: map 3 decodes the floppy ports
; whatever the DOS signal says. Returns with JP (HL') after IN A,(#E2).
; tools/sprinter/dcp-table.py does the same unpacking and prints the table.
;---------------------------------------------------------------------------
DcpInit:
	exx			;0ca1	d9		.
	ld hl,DcpTablePacked	;0ca2	21 00 14	! . .
	ld de,0c000h		;0ca5	11 00 c0	. . .
l0ca8h:
	ld b,008h		;0ca8	06 08		. .
	ld c,(hl)		;0caa	4e		N
	inc hl			;0cab	23		#
l0cach:
	rlc c			;0cac	cb 01		. .
	jr c,l0cb8h		;0cae	38 08		8 .
	ld a,000h		;0cb0	3e 00		> .
	ld (de),a		;0cb2	12		.
	inc de			;0cb3	13		.
	djnz l0cach		;0cb4	10 f6		. .
	jr l0cbeh		;0cb6	18 06		. .
l0cb8h:
	ld a,(hl)		;0cb8	7e		~
	inc hl			;0cb9	23		#
	ld (de),a		;0cba	12		.
	inc de			;0cbb	13		.
	djnz l0cach		;0cbc	10 ee		. .
l0cbeh:
	inc d			;0cbe	14		.
	dec d			;0cbf	15		.
	jr nz,l0ca8h		;0cc0	20 e6		  .
	ld hl,0c000h		;0cc2	21 00 c0	! . .
	ld de,0f000h		;0cc5	11 00 f0	. . .
	ld bc,l0400h		;0cc8	01 00 04	. . .
	ldir			;0ccb	ed b0		. .
	ld hl,0f000h		;0ccd	21 00 f0	! . .
	ld de,0f400h		;0cd0	11 00 f4	. . .
	ld bc,l0c00h		;0cd3	01 00 0c	. . .
	ldir			;0cd6	ed b0		. .
	in a,(0e2h)		;0cd8	db e2		. .
	exx			;0cda	d9		.
	jp (hl)			;0cdb	e9		.
DCP_CONFIG_PARSE_TABLE:
				; = ~DCP_CONFIG.PARSE_TABLE (src: DCP.ASM:536, BIOS-TT 0271ac3)
	ld a,l			;0cdc	7d		}
	and e			;0cdd	a3		.
	ld l,a			;0cde	6f		o
	ld a,h			;0cdf	7c		|
	and d			;0ce0	a2		.
	or 0c0h			;0ce1	f6 c0		. .
	ld h,a			;0ce3	67		g
	ld a,d			;0ce4	7a		z
	or 0c0h			;0ce5	f6 c0		. .
	ld d,a			;0ce7	57		W
DCP_CONFIG_loop:
				; = ~DCP_CONFIG.loop (src: DCP.ASM:550, BIOS-TT 0271ac3)
	ld (hl),b		;0ce8	70		p
	ld a,l			;0ce9	7d		}
	or e			;0cea	b3		.
	inc a			;0ceb	3c		<
	jr z,DCP_CONFIG_carry	;0cec	28 09		( .
	or e			;0cee	b3		.
	xor e			;0cef	ab		.
	ld c,a			;0cf0	4f		O
	ld a,l			;0cf1	7d		}
	and e			;0cf2	a3		.
	or c			;0cf3	b1		.
	ld l,a			;0cf4	6f		o
	jr DCP_CONFIG_loop	;0cf5	18 f1		. .
DCP_CONFIG_carry:
				; = ~DCP_CONFIG.carry (src: DCP.ASM:568, BIOS-TT 0271ac3)
	ld a,l			;0cf7	7d		}
	and e			;0cf8	a3		.
	ld l,a			;0cf9	6f		o
	ld a,h			;0cfa	7c		|
	or d			;0cfb	b2		.
	inc a			;0cfc	3c		<
	jr z,l0d08h		;0cfd	28 09		( .
	or d			;0cff	b2		.
	xor d			;0d00	aa		.
	ld c,a			;0d01	4f		O
	ld a,h			;0d02	7c		|
	and d			;0d03	a2		.
	or c			;0d04	b1		.
	ld h,a			;0d05	67		g
	jr DCP_CONFIG_loop	;0d06	18 e0		. .
l0d08h:
	jp (ix)			;0d08	dd e9		. .
FnA1_PIC_POINT:
				; = FLEX_END (src: FLEX.asm:822, BIOS-TT 0271ac3)
				; = PIC_FN1 (src: FUNC_PIC.ASM:5, BIOS-TT 0271ac3)
	and a			;0d0a	a7		.
	scf			;0d0b	37		7
	ret nz			;0d0c	c0		.
	in a,(089h)		;0d0d	db 89		. .
	push af			;0d0f	f5		.
	in a,(0e2h)		;0d10	db e2		. .
	ex af,af'		;0d12	08		.
	ld a,0feh		;0d13	3e fe		> .
	out (0e2h),a		;0d15	d3 e2		. .
	ld a,(0e01ah)		;0d17	3a 1a e0	: . .
	add a,e			;0d1a	83		.
	out (089h),a		;0d1b	d3 89		. .
	ld a,b			;0d1d	78		x
	ld bc,(0e018h)		;0d1e	ed 4b 18 e0	. K . .
	add hl,bc		;0d22	09		.
	ld b,a			;0d23	47		G
	ld a,050h		;0d24	3e 50		> P
	out (0e2h),a		;0d26	d3 e2		. .
	ld (hl),a		;0d28	77		w
	ex af,af'		;0d29	08		.
	out (0e2h),a		;0d2a	d3 e2		. .
	pop af			;0d2c	f1		.
	out (089h),a		;0d2d	d3 89		. .
	ret			;0d2f	c9		.
FnA2_PIC_FN2:
				; = PIC_FN2 (src: FUNC_PIC.ASM:39, BIOS-TT 0271ac3)
	ld d,a			;0d30	57		W
	in a,(0a2h)		;0d31	db a2		. .
	ex af,af'		;0d33	08		.
	ld a,b			;0d34	78		x
	add a,050h		;0d35	c6 50		. P
	out (0a2h),a		;0d37	d3 a2		. .
	bit 0,b			;0d39	cb 40		. @
	ld bc,04040h		;0d3b	01 40 40	. @ @
	jr z,PIC_FN2_NO_2ND	;0d3e	28 03		( .
	ld bc,04180h		;0d40	01 80 41	. . A
PIC_FN2_NO_2ND:
				; = PIC_FN2_NO_2ND (src: FUNC_PIC.ASM:52, BIOS-TT 0271ac3)
	add hl,bc		;0d43	09		.
	ld a,e			;0d44	7b		{
	out (089h),a		;0d45	d3 89		. .
	defb 0ddh,07ch ;ld a,ixh	;0d47	dd 7c		. |
	and a			;0d49	a7		.
	jr z,PIC_FN2_NO256	;0d4a	28 0f		( .
PIC_FN2_256L:
				; = PIC_FN2_256L (src: FUNC_PIC.ASM:61, BIOS-TT 0271ac3)
	ld b,040h		;0d4c	06 40		. @
PIC_FN2_256:
				; = PIC_FN2_256 (src: FUNC_PIC.ASM:63, BIOS-TT 0271ac3)
	ld (hl),d		;0d4e	72		r
	inc hl			;0d4f	23		#
	ld (hl),d		;0d50	72		r
	inc hl			;0d51	23		#
	ld (hl),d		;0d52	72		r
	inc hl			;0d53	23		#
	ld (hl),d		;0d54	72		r
	inc hl			;0d55	23		#
	djnz PIC_FN2_256	;0d56	10 f6		. .
	dec a			;0d58	3d		=
	jr nz,PIC_FN2_256L	;0d59	20 f1		  .
PIC_FN2_NO256:
	defb 0ddh,045h ;ld b,ixl	;0d5b	dd 45		. E
	and a			;0d5d	a7		.
	rr b			;0d5e	cb 18		. .
	jr nc,PIC_FN2_NO1	;0d60	30 03		0 .
	ld (hl),d		;0d62	72		r
	inc hl			;0d63	23		#
	and a			;0d64	a7		.
PIC_FN2_NO1:
				; = PIC_FN2_NO1 (src: FUNC_PIC.ASM:84, BIOS-TT 0271ac3)
	rr b			;0d65	cb 18		. .
	jr nc,PIC_FN2_NO2	;0d67	30 05		0 .
	ld (hl),d		;0d69	72		r
	inc hl			;0d6a	23		#
	ld (hl),d		;0d6b	72		r
	inc hl			;0d6c	23		#
	and a			;0d6d	a7		.
PIC_FN2_NO2:
				; = PIC_FN2_NO2 (src: FUNC_PIC.ASM:92, BIOS-TT 0271ac3)
	xor a			;0d6e	af		.
	cp b			;0d6f	b8		.
	jr z,PIC_FN2_NO4	;0d70	28 0a		( .
PIC_FN2_4:
				; = PIC_FN2_4 (src: FUNC_PIC.ASM:96, BIOS-TT 0271ac3)
	ld (hl),d		;0d72	72		r
	inc hl			;0d73	23		#
	ld (hl),d		;0d74	72		r
	inc hl			;0d75	23		#
	ld (hl),d		;0d76	72		r
	inc hl			;0d77	23		#
	ld (hl),d		;0d78	72		r
	inc hl			;0d79	23		#
	djnz PIC_FN2_4		;0d7a	10 f6		. .
PIC_FN2_NO4:
				; = PIC_FN2_NO4 (src: FUNC_PIC.ASM:106, BIOS-TT 0271ac3)
	ex af,af'		;0d7c	08		.
	out (0a2h),a		;0d7d	d3 a2		. .
	xor a			;0d7f	af		.
	out (089h),a		;0d80	d3 89		. .
	ret			;0d82	c9		.
FnA3_PIC_FN3:
				; = PIC_FN3 (src: FUNC_PIC.ASM:119, BIOS-TT 0271ac3)
	in a,(0e2h)		;0d83	db e2		. .
	ld c,a			;0d85	4f		O
	ld a,0feh		;0d86	3e fe		> .
	out (0e2h),a		;0d88	d3 e2		. .
	ld a,c			;0d8a	79		y
	ld (0c107h),a		;0d8b	32 07 c1	2 . .
	in a,(0c2h)		;0d8e	db c2		. .
	ld (0c106h),a		;0d90	32 06 c1	2 . .
	in a,(0a2h)		;0d93	db a2		. .
	ld (0c105h),a		;0d95	32 05 c1	2 . .
	ld a,b			;0d98	78		x
	add a,050h		;0d99	c6 50		. P
	out (0a2h),a		;0d9b	d3 a2		. .
	bit 0,b			;0d9d	cb 40		. @
	ld bc,04040h		;0d9f	01 40 40	. @ @
	jr z,PIC_FN3_NO_2ND	;0da2	28 03		( .
	ld bc,04180h		;0da4	01 80 41	. . A
PIC_FN3_NO_2ND:
				; = PIC_FN3_NO_2ND (src: FUNC_PIC.ASM:139, BIOS-TT 0271ac3)
	add hl,bc		;0da7	09		.
	ld a,e			;0da8	7b		{
	out (089h),a		;0da9	d3 89		. .
	ld a,h			;0dab	7c		|
	exx			;0dac	d9		.
	ld d,a			;0dad	57		W
	exx			;0dae	d9		.
	ld a,l			;0daf	7d		}
	exx			;0db0	d9		.
	ld e,a			;0db1	5f		_
	exx			;0db2	d9		.
	ex af,af'		;0db3	08		.
	out (0c2h),a		;0db4	d3 c2		. .
	ex af,af'		;0db6	08		.
	ld hl,0c200h		;0db7	21 00 c2	! . .
	ld l,a			;0dba	6f		o
	ld a,0feh		;0dbb	3e fe		> .
	out (0e2h),a		;0dbd	d3 e2		. .
	ld a,(hl)		;0dbf	7e		~
	out (0e2h),a		;0dc0	d3 e2		. .
	exx			;0dc2	d9		.
	ldir			;0dc3	ed b0		. .
	bit 6,h			;0dc5	cb 74		. t
	jr z,PIC_FN3_NO		;0dc7	28 03		( .
	res 6,h			;0dc9	cb b4		. .
	ex af,af'		;0dcb	08		.
PIC_FN3_NO:
				; = PIC_FN3_NO (src: FUNC_PIC.ASM:169, BIOS-TT 0271ac3)
	exx			;0dcc	d9		.
	ld a,0feh		;0dcd	3e fe		> .
	out (0e2h),a		;0dcf	d3 e2		. .
	ld a,(0c105h)		;0dd1	3a 05 c1	: . .
	out (0a2h),a		;0dd4	d3 a2		. .
	ld a,(0c106h)		;0dd6	3a 06 c1	: . .
	out (0c2h),a		;0dd9	d3 c2		. .
	ld a,(0c107h)		;0ddb	3a 07 c1	: . .
	out (0e2h),a		;0dde	d3 e2		. .
	xor a			;0de0	af		.
	out (089h),a		;0de1	d3 89		. .
	ret			;0de3	c9		.
FnA4_PIC_SET_PAL:
				; = PIC_SET_PAL (src: FUNC_PIC.ASM:189, BIOS-TT 0271ac3)
	push ix			;0de4	dd e5		. .
	ex af,af'		;0de6	08		.
	in a,(089h)		;0de7	db 89		. .
	push af			;0de9	f5		.
	ld a,e			;0dea	7b		{
	out (089h),a		;0deb	d3 89		. .
	ld a,d			;0ded	7a		z
	ex af,af'		;0dee	08		.
	bit 7,h			;0def	cb 7c		. |
	ld c,0e2h		;0df1	0e e2		. .
	ld d,0c3h		;0df3	16 c3		. .
	jr z,PIC_FN4_NO_PAGE1	;0df5	28 04		( .
	ld d,043h		;0df7	16 43		. C
	ld c,0a2h		;0df9	0e a2		. .
PIC_FN4_NO_PAGE1:
				; = PIC_FN4_NO_PAGE1 (src: FUNC_PIC.ASM:206, BIOS-TT 0271ac3)
	add a,a			;0dfb	87		.
	jr c,PIC_PAL_READ	;0dfc	38 3a		8 :
	add a,a			;0dfe	87		.
	and 01ch		;0dff	e6 1c		. .
	xor 0e0h		;0e01	ee e0		. .
	ld e,a			;0e03	5f		_
	push de			;0e04	d5		.
	pop ix			;0e05	dd e1		. .
	ld d,b			;0e07	50		P
	ex af,af'		;0e08	08		.
	ld b,a			;0e09	47		G
	in e,(c)		;0e0a	ed 58		. X
	ld a,050h		;0e0c	3e 50		> P
	out (c),a		;0e0e	ed 79		. y
PIC_FN4_L1:
				; = PIC_FN4_L1 (src: FUNC_PIC.ASM:226, BIOS-TT 0271ac3)
	ld a,(hl)		;0e10	7e		~
	and d			;0e11	a2		.
	ld (ix+002h),a		;0e12	dd 77 02	. w .
	inc hl			;0e15	23		#
	ld a,(hl)		;0e16	7e		~
	and d			;0e17	a2		.
	ld (ix+001h),a		;0e18	dd 77 01	. w .
	inc hl			;0e1b	23		#
	ld a,(hl)		;0e1c	7e		~
	and d			;0e1d	a2		.
	ld (ix+000h),a		;0e1e	dd 77 00	. w .
	inc hl			;0e21	23		#
	ld a,(hl)		;0e22	7e		~
	and d			;0e23	a2		.
	ld (ix+003h),a		;0e24	dd 77 03	. w .
	inc hl			;0e27	23		#
	in a,(089h)		;0e28	db 89		. .
	inc a			;0e2a	3c		<
	out (089h),a		;0e2b	d3 89		. .
	djnz PIC_FN4_L1		;0e2d	10 e1		. .
	out (c),e		;0e2f	ed 59		. Y
	pop af			;0e31	f1		.
	out (089h),a		;0e32	d3 89		. .
	pop ix			;0e34	dd e1		. .
	and a			;0e36	a7		.
	ret			;0e37	c9		.
PIC_PAL_READ:
				; = PIC_PAL_READ (src: FUNC_PIC.ASM:262, BIOS-TT 0271ac3)
	add a,a			;0e38	87		.
	and 01ch		;0e39	e6 1c		. .
	xor 0e0h		;0e3b	ee e0		. .
	ld e,a			;0e3d	5f		_
	push de			;0e3e	d5		.
	pop ix			;0e3f	dd e1		. .
	ld d,b			;0e41	50		P
	ex af,af'		;0e42	08		.
	ld b,a			;0e43	47		G
	in e,(c)		;0e44	ed 58		. X
	ld a,050h		;0e46	3e 50		> P
	out (c),a		;0e48	ed 79		. y
PIC_FN4_L2:
				; = PIC_FN4_L2 (src: FUNC_PIC.ASM:279, BIOS-TT 0271ac3)
	ld a,(ix+002h)		;0e4a	dd 7e 02	. ~ .
	ld (hl),a		;0e4d	77		w
	inc hl			;0e4e	23		#
	ld a,(ix+001h)		;0e4f	dd 7e 01	. ~ .
	ld (hl),a		;0e52	77		w
	inc hl			;0e53	23		#
	ld a,(ix+000h)		;0e54	dd 7e 00	. ~ .
	ld (hl),a		;0e57	77		w
	inc hl			;0e58	23		#
	ld a,(ix+003h)		;0e59	dd 7e 03	. ~ .
	ld (hl),a		;0e5c	77		w
	inc hl			;0e5d	23		#
	in a,(089h)		;0e5e	db 89		. .
	inc a			;0e60	3c		<
	out (089h),a		;0e61	d3 89		. .
	djnz PIC_FN4_L2		;0e63	10 e5		. .
	out (c),e		;0e65	ed 59		. Y
	pop af			;0e67	f1		.
	out (089h),a		;0e68	d3 89		. .
	pop ix			;0e6a	dd e1		. .
	and a			;0e6c	a7		.
	ret			;0e6d	c9		.
FnA5_PIC_FN5:
				; = PIC_FN5 (src: FUNC_PIC.ASM:313, BIOS-TT 0271ac3)
	ld a,e			;0e6e	7b		{
	and 001h		;0e6f	e6 01		. .
	out (0c9h),a		;0e71	d3 c9		. .
	ret			;0e73	c9		.
FnA6_SET_PAL_INIT:
				; = PIC_FN6 (src: FUNC_PIC.ASM:320, BIOS-TT 0271ac3)
	ld d,a			;0e74	57		W
	dec b			;0e75	05		.
	jp z,SET_PAL_GRAF	;0e76	ca d5 0a	. . .
	dec b			;0e79	05		.
	jp z,SET_PAL_ZX		;0e7a	ca f4 09	. . .
	dec b			;0e7d	05		.
	jp z,SET_PAL_IBM	;0e7e	ca ef 09	. . .
	scf			;0e81	37		7
	ret			;0e82	c9		.
FnA7_PIC_FN7:
				; = PIC_FN7 (src: FUNC_PIC.ASM:367, BIOS-TT 0271ac3)
	ld d,a			;0e83	57		W
	in a,(0a2h)		;0e84	db a2		. .
	push af			;0e86	f5		.
	ld a,b			;0e87	78		x
	add a,050h		;0e88	c6 50		. P
	out (0a2h),a		;0e8a	d3 a2		. .
	bit 0,b			;0e8c	cb 40		. @
	ld bc,04040h		;0e8e	01 40 40	. @ @
	jr z,PIC_FN7_no_2nd	;0e91	28 03		( .
	ld bc,04180h		;0e93	01 80 41	. . A
PIC_FN7_no_2nd:
				; = PIC_FN7.no_2nd (src: FUNC_PIC.ASM:380, BIOS-TT 0271ac3)
	add hl,bc		;0e96	09		.
	ld a,e			;0e97	7b		{
PIC_FN7_loop:
				; = PIC_FN7.loop (src: FUNC_PIC.ASM:384, BIOS-TT 0271ac3)
	out (089h),a		;0e98	d3 89		. .
	ld (hl),d		;0e9a	72		r
	exx			;0e9b	d9		.
	ex af,af'		;0e9c	08		.
	dec h			;0e9d	25		%
	jr z,PIC_FN7_exit	;0e9e	28 11		( .
	ld a,c			;0ea0	79		y
	add a,b			;0ea1	80		.
	ld c,a			;0ea2	4f		O
	ld a,e			;0ea3	7b		{
	exx			;0ea4	d9		.
	adc a,l			;0ea5	8d		.
	ld l,a			;0ea6	6f		o
	exx			;0ea7	d9		.
	ld a,d			;0ea8	7a		z
	exx			;0ea9	d9		.
	adc a,h			;0eaa	8c		.
	ld h,a			;0eab	67		g
	inc e			;0eac	1c		.
	ex af,af'		;0ead	08		.
	inc a			;0eae	3c		<
	jr PIC_FN7_loop		;0eaf	18 e7		. .
PIC_FN7_exit:
				; = PIC_FN7.exit (src: FUNC_PIC.ASM:413, BIOS-TT 0271ac3)
	exx			;0eb1	d9		.
	pop af			;0eb2	f1		.
	out (0a2h),a		;0eb3	d3 a2		. .
	xor a			;0eb5	af		.
	out (089h),a		;0eb6	d3 89		. .
	ret			;0eb8	c9		.
FnA8_PIC_FN8:
				; = PIC_FN8 (src: FUNC_PIC.ASM:425, BIOS-TT 0271ac3)
	in a,(0a2h)		;0eb9	db a2		. .
	push af			;0ebb	f5		.
	ld a,b			;0ebc	78		x
	add a,050h		;0ebd	c6 50		. P
	out (0a2h),a		;0ebf	d3 a2		. .
	bit 0,b			;0ec1	cb 40		. @
	ld bc,04040h		;0ec3	01 40 40	. @ @
	jr z,PIC_FN8_no_2nd	;0ec6	28 03		( .
	ld bc,04180h		;0ec8	01 80 41	. . A
PIC_FN8_no_2nd:
				; = PIC_FN8.no_2nd (src: FUNC_PIC.ASM:437, BIOS-TT 0271ac3)
	add hl,bc		;0ecb	09		.
	ld a,e			;0ecc	7b		{
PIC_FN8_loop:
				; = PIC_FN8.loop (src: FUNC_PIC.ASM:441, BIOS-TT 0271ac3)
	out (089h),a		;0ecd	d3 89		. .
	ld d,(ix+000h)		;0ecf	dd 56 00	. V .
	inc ix			;0ed2	dd 23		. #
	ld (hl),d		;0ed4	72		r
	exx			;0ed5	d9		.
	ex af,af'		;0ed6	08		.
	dec h			;0ed7	25		%
	jr z,PIC_FN8_exit	;0ed8	28 11		( .
	ld a,c			;0eda	79		y
	add a,b			;0edb	80		.
	ld c,a			;0edc	4f		O
	ld a,e			;0edd	7b		{
	exx			;0ede	d9		.
	adc a,l			;0edf	8d		.
	ld l,a			;0ee0	6f		o
	exx			;0ee1	d9		.
	ld a,d			;0ee2	7a		z
	exx			;0ee3	d9		.
	adc a,h			;0ee4	8c		.
	ld h,a			;0ee5	67		g
	inc e			;0ee6	1c		.
	ex af,af'		;0ee7	08		.
	inc a			;0ee8	3c		<
	jr PIC_FN8_loop		;0ee9	18 e2		. .
PIC_FN8_exit:
				; = PIC_FN8.exit (src: FUNC_PIC.ASM:474, BIOS-TT 0271ac3)
	exx			;0eeb	d9		.
	pop af			;0eec	f1		.
	out (0a2h),a		;0eed	d3 a2		. .
	xor a			;0eef	af		.
	out (089h),a		;0ef0	d3 89		. .
	ret			;0ef2	c9		.
FnA9_PIC_FN9:
				; = PIC_FN9 (src: FUNC_PIC.ASM:485, BIOS-TT 0271ac3)
				; = PIC_FN10 (src: FUNC_PIC.ASM:490, BIOS-TT 0271ac3)
				; = PIC_FN11 (src: FUNC_PIC.ASM:491, BIOS-TT 0271ac3)
				; = PIC_FN12 (src: FUNC_PIC.ASM:492, BIOS-TT 0271ac3)
				; = PIC_FN13 (src: FUNC_PIC.ASM:493, BIOS-TT 0271ac3)
				; = PIC_FN14 (src: FUNC_PIC.ASM:494, BIOS-TT 0271ac3)
				; = PIC_FN15 (src: FUNC_PIC.ASM:495, BIOS-TT 0271ac3)
	scf			;0ef3	37		7
	ret			;0ef4	c9		.
LP_SCR_80:

; BLOCK 'Data0EF5' (start 0x0ef5 end 0x0f55)
				; = LP_SCR_80 (src: FUNC_PIC.ASM:504, BIOS-TT 0271ac3)
	defb 028h		;0ef5	28		(
	defb 020h		;0ef6	20		 
	defb 000h		;0ef7	00		.
	defb 000h		;0ef8	00		.
	defb 01bh		;0ef9	1b		.
	defb 000h		;0efa	00		.
	defb 000h		;0efb	00		.
	defb 000h		;0efc	00		.
	defb 000h		;0efd	00		.
	defb 000h		;0efe	00		.
	defb 000h		;0eff	00		.
	defb 000h		;0f00	00		.
	defb 000h		;0f01	00		.
	defb 000h		;0f02	00		.
	defb 000h		;0f03	00		.
	defb 000h		;0f04	00		.
LP_SCR_40:
				; = LP_SCR_40 (src: FUNC_PIC.ASM:509, BIOS-TT 0271ac3)
	defb 028h		;0f05	28		(
	defb 020h		;0f06	20		 
	defb 000h		;0f07	00		.
	defb 000h		;0f08	00		.
	defb 07bh		;0f09	7b		{
	defb 000h		;0f0a	00		.
	defb 000h		;0f0b	00		.
	defb 000h		;0f0c	00		.
	defb 000h		;0f0d	00		.
	defb 000h		;0f0e	00		.
	defb 000h		;0f0f	00		.
	defb 000h		;0f10	00		.
	defb 000h		;0f11	00		.
	defb 000h		;0f12	00		.
	defb 000h		;0f13	00		.
	defb 000h		;0f14	00		.
LP_SCR_32:
				; = LP_SCR_32 (src: FUNC_PIC.ASM:514, BIOS-TT 0271ac3)
	defb 020h		;0f15	20		 
	defb 018h		;0f16	18		.
	defb 004h		;0f17	04		.
	defb 004h		;0f18	04		.
	defb 030h		;0f19	30		0
	defb 001h		;0f1a	01		.
	defb 000h		;0f1b	00		.
	defb 000h		;0f1c	00		.
	defb 000h		;0f1d	00		.
	defb 000h		;0f1e	00		.
	defb 000h		;0f1f	00		.
	defb 000h		;0f20	00		.
	defb 000h		;0f21	00		.
	defb 000h		;0f22	00		.
	defb 000h		;0f23	00		.
	defb 000h		;0f24	00		.
LP_SCR_64:
				; = LP_SCR_64 (src: FUNC_PIC.ASM:519, BIOS-TT 0271ac3)
	defb 020h		;0f25	20		 
	defb 018h		;0f26	18		.
	defb 004h		;0f27	04		.
	defb 004h		;0f28	04		.
	defb 09bh		;0f29	9b		.
	defb 000h		;0f2a	00		.
	defb 000h		;0f2b	00		.
	defb 000h		;0f2c	00		.
	defb 000h		;0f2d	00		.
	defb 000h		;0f2e	00		.
	defb 000h		;0f2f	00		.
	defb 000h		;0f30	00		.
	defb 000h		;0f31	00		.
	defb 000h		;0f32	00		.
	defb 000h		;0f33	00		.
	defb 000h		;0f34	00		.
PIC_320X256_1:
				; = PIC_320X256_1 (src: FUNC_PIC.ASM:524, BIOS-TT 0271ac3)
	defb 028h		;0f35	28		(
	defb 020h		;0f36	20		 
	defb 000h		;0f37	00		.
	defb 000h		;0f38	00		.
	defb 020h		;0f39	20		 
	defb 000h		;0f3a	00		.
	defb 008h		;0f3b	08		.
	defb 000h		;0f3c	00		.
	defb 000h		;0f3d	00		.
	defb 000h		;0f3e	00		.
	defb 000h		;0f3f	00		.
	defb 000h		;0f40	00		.
	defb 000h		;0f41	00		.
	defb 000h		;0f42	00		.
	defb 000h		;0f43	00		.
	defb 000h		;0f44	00		.
PIC_320X256_2:
				; = PIC_320X256_2 (src: FUNC_PIC.ASM:529, BIOS-TT 0271ac3)
	defb 028h		;0f45	28		(
	defb 020h		;0f46	20		 
	defb 000h		;0f47	00		.
	defb 000h		;0f48	00		.
	defb 060h		;0f49	60		`
	defb 000h		;0f4a	00		.
	defb 030h		;0f4b	30		0
	defb 000h		;0f4c	00		.
	defb 000h		;0f4d	00		.
	defb 000h		;0f4e	00		.
	defb 000h		;0f4f	00		.
	defb 000h		;0f50	00		.
	defb 000h		;0f51	00		.
	defb 000h		;0f52	00		.
	defb 000h		;0f53	00		.
	defb 000h		;0f54	00		.
Fn90_GetMemSize_old:
				; = EMM.GetMemSize (src: FUNC_RAM_ROM_DRV.ASM:7, BIOS-TT 0271ac3)
	in a,(0c2h)		;0f55	db c2		. .
	ld b,a			;0f57	47		G
	ld a,0feh		;0f58	3e fe		> .
	out (0c2h),a		;0f5a	d3 c2		. .
	ld hl,08200h		;0f5c	21 00 82	! . .
	ld c,000h		;0f5f	0e 00		. .
EMM_GetMemSize_loop:
				; = EMM.GetMemSize.loop (src: FUNC_RAM_ROM_DRV.ASM:14, BIOS-TT 0271ac3)
	ld a,(hl)		;0f61	7e		~
	inc l			;0f62	2c		,
	jr z,EMM_GetMemSize_exit	;0f63	28 06		( .
	and a			;0f65	a7		.
	jr nz,EMM_GetMemSize_loop	;0f66	20 f9		  .
	inc c			;0f68	0c		.
	jr EMM_GetMemSize_loop	;0f69	18 f6		. .
EMM_GetMemSize_exit:
				; = EMM.GetMemSize.exit (src: FUNC_RAM_ROM_DRV.ASM:21, BIOS-TT 0271ac3)
	ld hl,ColdStart		;0f6b	21 00 01	! . .
	ld a,b			;0f6e	78		x
	ld b,000h		;0f6f	06 00		. .
	out (0c2h),a		;0f71	d3 c2		. .
	ret			;0f73	c9		.
Fn91_InitMem_old:
				; = EMM.InitMem (src: FUNC_RAM_ROM_DRV.ASM:34, BIOS-TT 0271ac3)
	push bc			;0f74	c5		.
	push hl			;0f75	e5		.
	push de			;0f76	d5		.
	in a,(0c2h)		;0f77	db c2		. .
	ld c,a			;0f79	4f		O
	ld a,0feh		;0f7a	3e fe		> .
	out (0c2h),a		;0f7c	d3 c2		. .
	ld hl,08200h		;0f7e	21 00 82	! . .
EMM_InitMem_loopFree:
				; = EMM.InitMem.loopFree (src: FUNC_RAM_ROM_DRV.ASM:45, BIOS-TT 0271ac3)
	ld (hl),000h		;0f81	36 00		6 .
	inc l			;0f83	2c		,
	jr nz,EMM_InitMem_loopFree	;0f84	20 fb		  .
	ld de,Data0FA7	;0f86	11 a7 0f	. . .
EMM_InitMem_loop:
				; = EMM.InitMem.loop (src: FUNC_RAM_ROM_DRV.ASM:52, BIOS-TT 0271ac3)
	ld a,(de)		;0f89	1a		.
	cp 0ffh			;0f8a	fe ff		. .
	jr z,l0f94h		;0f8c	28 06		( .
EMM_InitMem_loopBlk:
				; = EMM.InitMem.loopBlk (src: FUNC_RAM_ROM_DRV.ASM:55, BIOS-TT 0271ac3)
	inc de			;0f8e	13		.
	ld l,a			;0f8f	6f		o
	ld a,(de)		;0f90	1a		.
	ld (hl),a		;0f91	77		w
	jr EMM_InitMem_loop	;0f92	18 f5		. .
l0f94h:
	ld l,a			;0f94	6f		o
	ld (hl),a		;0f95	77		w
	ld hl,08180h		;0f96	21 80 81	! . .
	ld b,010h		;0f99	06 10		. .
EMM_InitMem_loop2:
				; = EMM.InitMem.loop2 (src: FUNC_RAM_ROM_DRV.ASM:70, BIOS-TT 0271ac3)
	ld (hl),000h		;0f9b	36 00		6 .
	inc l			;0f9d	2c		,
	djnz EMM_InitMem_loop2	;0f9e	10 fb		. .
	ld a,c			;0fa0	79		y
	out (0c2h),a		;0fa1	d3 c2		. .
	pop de			;0fa3	d1		.
	pop hl			;0fa4	e1		.
	pop bc			;0fa5	c1		.
	ret			;0fa6	c9		.

; BLOCK 'Data0FA7' (start 0x0fa7 end 0x0fd3)
Data0FA7:
	defb 000h		;0fa7	00		.
RESERVED_PAGES:
				; = RESERVED_PAGES (src: FUNC_RAM_ROM_DRV.ASM:81, BIOS-TT 0271ac3)
	defb 001h		;0fa8	01		.
	defb 002h		;0fa9	02		.
	defb 003h		;0faa	03		.
	defb 004h		;0fab	04		.
	defb 005h		;0fac	05		.
	defb 006h		;0fad	06		.
	defb 007h		;0fae	07		.
	defb 008h		;0faf	08		.
	defb 009h		;0fb0	09		.
	defb 00ah		;0fb1	0a		.
	defb 00bh		;0fb2	0b		.
	defb 00ch		;0fb3	0c		.
	defb 00dh		;0fb4	0d		.
	defb 00eh		;0fb5	0e		.
	defb 00fh		;0fb6	0f		.
	defb 040h		;0fb7	40		@
	defb 041h		;0fb8	41		A
	defb 042h		;0fb9	42		B
	defb 043h		;0fba	43		C
	defb 044h		;0fbb	44		D
	defb 045h		;0fbc	45		E
	defb 046h		;0fbd	46		F
	defb 047h		;0fbe	47		G
	defb 050h		;0fbf	50		P
	defb 051h		;0fc0	51		Q
	defb 052h		;0fc1	52		R
	defb 053h		;0fc2	53		S
	defb 054h		;0fc3	54		T
	defb 055h		;0fc4	55		U
	defb 056h		;0fc5	56		V
	defb 057h		;0fc6	57		W
	defb 058h		;0fc7	58		X
	defb 059h		;0fc8	59		Y
	defb 05ah		;0fc9	5a		Z
	defb 05bh		;0fca	5b		[
	defb 05ch		;0fcb	5c		\
	defb 05dh		;0fcc	5d		]
	defb 05eh		;0fcd	5e		^
	defb 05fh		;0fce	5f		_
	defb 0fch		;0fcf	fc		.
	defb 0fdh		;0fd0	fd		.
	defb 0feh		;0fd1	fe		.
	defb 0ffh		;0fd2	ff		.
FnC2_GetMem:
	push de			;0fd3	d5		.
RESERVED_PAGES_Blocks:
				; = RESERVED_PAGES.Blocks (src: FUNC_RAM_ROM_DRV.ASM:104, BIOS-TT 0271ac3)
				; = EMM.GetMem (src: FUNC_RAM_ROM_DRV.ASM:117, BIOS-TT 0271ac3)
	push bc			;0fd4	c5		.
	in a,(0c2h)		;0fd5	db c2		. .
	ex af,af'		;0fd7	08		.
	ld a,0feh		;0fd8	3e fe		> .
	out (0c2h),a		;0fda	d3 c2		. .
	ld c,b			;0fdc	48		H
	ld hl,08200h		;0fdd	21 00 82	! . .
EMM_GetMem_loop:
				; = EMM.GetMem.loop (src: FUNC_RAM_ROM_DRV.ASM:127, BIOS-TT 0271ac3)
	dec l			;0fe0	2d		-
	jr z,EMM_GetMem_noRAM	;0fe1	28 1d		( .
	ld a,(hl)		;0fe3	7e		~
	and a			;0fe4	a7		.
	jr nz,EMM_GetMem_loop	;0fe5	20 f9		  .
	djnz EMM_GetMem_loop	;0fe7	10 f7		. .
	ld b,c			;0fe9	41		A
	ld c,0ffh		;0fea	0e ff		. .
	ld hl,08200h		;0fec	21 00 82	! . .
EMM_GetMem_loop2:
				; = EMM.GetMem.loop2 (src: FUNC_RAM_ROM_DRV.ASM:140, BIOS-TT 0271ac3)
	dec l			;0fef	2d		-
	ld a,(hl)		;0ff0	7e		~
	and a			;0ff1	a7		.
	jr nz,EMM_GetMem_loop2	;0ff2	20 fb		  .
	ld (hl),c		;0ff4	71		q
	ld c,l			;0ff5	4d		M
	djnz EMM_GetMem_loop2	;0ff6	10 f7		. .
	ex af,af'		;0ff8	08		.
	out (0c2h),a		;0ff9	d3 c2		. .
	ld a,l			;0ffb	7d		}
	and a			;0ffc	a7		.
	pop bc			;0ffd	c1		.
	pop de			;0ffe	d1		.
	ret			;0fff	c9		.
EMM_GetMem_noRAM:
				; = EMM.GetMem.noRAM (src: FUNC_RAM_ROM_DRV.ASM:157, BIOS-TT 0271ac3)
	ld l,001h		;1000	2e 01		. .
	ex af,af'		;1002	08		.
	out (0c2h),a		;1003	d3 c2		. .
	ld a,l			;1005	7d		}
	scf			;1006	37		7
	pop bc			;1007	c1		.
	pop de			;1008	d1		.
	ret			;1009	c9		.
Fn92_GetMemRMD:
				; = EMM.GetMemRMD (src: FUNC_RAM_ROM_DRV.ASM:177, BIOS-TT 0271ac3)
	push af			;100a	f5		.
	call FnC2_GetMem	;100b	cd d3 0f	. . .
	jr c,EMM_GetMemRMD_error1	;100e	38 09		8 .
	ld b,a			;1010	47		G
	pop af			;1011	f1		.
	call FnC9_BLK_TO_RAMD	;1012	cd ba 31	. . 1
	ret nc			;1015	d0		.
EMM_GetMemRMD_error2:
				; = EMM.GetMemRMD.error2 (src: FUNC_RAM_ROM_DRV.ASM:186, BIOS-TT 0271ac3)
	ld l,002h		;1016	2e 02		. .
	ret			;1018	c9		.
EMM_GetMemRMD_error1:
				; = EMM.GetMemRMD.error1 (src: FUNC_RAM_ROM_DRV.ASM:189, BIOS-TT 0271ac3)
	pop af			;1019	f1		.
	ld l,001h		;101a	2e 01		. .
	scf			;101c	37		7
	ret			;101d	c9		.
FnC3_FreeMem:
				; = EMM.FreeMem (src: FUNC_RAM_ROM_DRV.ASM:221, BIOS-TT 0271ac3)
	and a			;101e	a7		.
	scf			;101f	37		7
	ret z			;1020	c8		.
	ld l,a			;1021	6f		o
	in a,(0c2h)		;1022	db c2		. .
	ex af,af'		;1024	08		.
	ld a,0feh		;1025	3e fe		> .
	out (0c2h),a		;1027	d3 c2		. .
	ld h,082h		;1029	26 82		& .
	ld a,l			;102b	7d		}
EMM_F3M_L1:
				; = EMM_F3M_L1 (src: FUNC_RAM_ROM_DRV.ASM:234, BIOS-TT 0271ac3)
	ld l,a			;102c	6f		o
	ld a,(hl)		;102d	7e		~
	and a			;102e	a7		.
	jr z,EMM_FN3M_ERR	;102f	28 0d		( .
	ld (hl),000h		;1031	36 00		6 .
	cp 0ffh			;1033	fe ff		. .
	jr nz,EMM_F3M_L1	;1035	20 f5		  .
	ex af,af'		;1037	08		.
	out (0c2h),a		;1038	d3 c2		. .
	ld a,000h		;103a	3e 00		> .
	and a			;103c	a7		.
	ret			;103d	c9		.
EMM_FN3M_ERR:
				; = EMM_FN3M_ERR (src: FUNC_RAM_ROM_DRV.ASM:249, BIOS-TT 0271ac3)
	ex af,af'		;103e	08		.
	out (0c2h),a		;103f	d3 c2		. .
	ld a,002h		;1041	3e 02		> .
	scf			;1043	37		7
	ret			;1044	c9		.
Fn93_FreeMemRMD:
				; = ~EMM.FreeMemRMD (src: FUNC_RAM_ROM_DRV.ASM:202, BIOS-TT 0271ac3)
				; = EMM.GetMemPageRMD (src: FUNC_RAM_ROM_DRV.ASM:265, BIOS-TT 0271ac3)
	call FnCE_GET_RAMD_ST	;1045	cd 56 32	. V 2
	ret c			;1048	d8		.
	scf			;1049	37		7
	ret z			;104a	c8		.
	ld c,a			;104b	4f		O
	in a,(0c2h)		;104c	db c2		. .
	ld b,a			;104e	47		G
	ld a,0feh		;104f	3e fe		> .
	out (0c2h),a		;1051	d3 c2		. .
	ld (hl),000h		;1053	36 00		6 .
	ld a,b			;1055	78		x
	out (0c2h),a		;1056	d3 c2		. .
	ld a,c			;1058	79		y
	jr FnC3_FreeMem		;1059	18 c3		. .
FnC4_GetMemPage:
				; = EMM.GetMemPage (src: FUNC_RAM_ROM_DRV.ASM:276, BIOS-TT 0271ac3)
	ld l,a			;105b	6f		o
	in a,(0c2h)		;105c	db c2		. .
	ex af,af'		;105e	08		.
	ld a,0feh		;105f	3e fe		> .
	out (0c2h),a		;1061	d3 c2		. .
	inc b			;1063	04		.
	ld h,082h		;1064	26 82		& .
EMM_F4M_L1:
				; = EMM_F4M_L1 (src: FUNC_RAM_ROM_DRV.ASM:285, BIOS-TT 0271ac3)
	ld a,(hl)		;1066	7e		~
	and a			;1067	a7		.
	jr z,EMM_F4M_ERR	;1068	28 08		( .
	dec b			;106a	05		.
	jr z,EMM_F4M_END	;106b	28 0c		( .
	ld l,a			;106d	6f		o
	cp 0ffh			;106e	fe ff		. .
	jr nz,EMM_F4M_L1	;1070	20 f4		  .
EMM_F4M_ERR:
				; = EMM_F4M_ERR (src: FUNC_RAM_ROM_DRV.ASM:294, BIOS-TT 0271ac3)
	ld l,a			;1072	6f		o
	ex af,af'		;1073	08		.
	out (0c2h),a		;1074	d3 c2		. .
	ld a,l			;1076	7d		}
	scf			;1077	37		7
	ret			;1078	c9		.
EMM_F4M_END:
				; = EMM_F4M_END (src: FUNC_RAM_ROM_DRV.ASM:302, BIOS-TT 0271ac3)
	ex af,af'		;1079	08		.
	out (0c2h),a		;107a	d3 c2		. .
	ld a,l			;107c	7d		}
	and a			;107d	a7		.
	ret			;107e	c9		.
Fn94_GetMemPageRMD:
	call FnCE_GET_RAMD_ST	;107f	cd 56 32	. V 2
	ret c			;1082	d8		.
	scf			;1083	37		7
	ret z			;1084	c8		.
	jr FnC4_GetMemPage	;1085	18 d4		. .
Fn95_Unnamed:
				; = EMM.GetMemPageNext (src: FUNC_RAM_ROM_DRV.ASM:319, BIOS-TT 0271ac3)
	ld l,a			;1087	6f		o
	and a			;1088	a7		.
	scf			;1089	37		7
	ret z			;108a	c8		.
	in a,(0c2h)		;108b	db c2		. .
	ld h,a			;108d	67		g
	ld a,0feh		;108e	3e fe		> .
	out (0c2h),a		;1090	d3 c2		. .
	ld a,h			;1092	7c		|
	ld h,082h		;1093	26 82		& .
	ld l,(hl)		;1095	6e		n
	out (0c2h),a		;1096	d3 c2		. .
	ld a,l			;1098	7d		}
	and a			;1099	a7		.
	scf			;109a	37		7
	ret z			;109b	c8		.
	and a			;109c	a7		.
	ret			;109d	c9		.
FnC5_GetMemBlkPages:
				; = EMM.GetMemBlkPages (src: FUNC_RAM_ROM_DRV.ASM:349, BIOS-TT 0271ac3)
	push de			;109e	d5		.
	push hl			;109f	e5		.
	ex de,hl		;10a0	eb		.
	ld b,000h		;10a1	06 00		. .
	ld l,a			;10a3	6f		o
EMM_GetMemBlkPages_loop:
				; = EMM.GetMemBlkPages.loop (src: FUNC_RAM_ROM_DRV.ASM:356, BIOS-TT 0271ac3)
	ld a,l			;10a4	7d		}
	ld (de),a		;10a5	12		.
	inc de			;10a6	13		.
	and a			;10a7	a7		.
	jr z,EMM_GetMemBlkPages_error	;10a8	28 14		( .
	cp 0ffh			;10aa	fe ff		. .
	jr z,EMM_GetMemBlkPages_end	;10ac	28 14		( .
	in a,(0c2h)		;10ae	db c2		. .
	ld c,a			;10b0	4f		O
	ld a,0feh		;10b1	3e fe		> .
	out (0c2h),a		;10b3	d3 c2		. .
	ld h,082h		;10b5	26 82		& .
	ld l,(hl)		;10b7	6e		n
	ld a,c			;10b8	79		y
	out (0c2h),a		;10b9	d3 c2		. .
	inc b			;10bb	04		.
	jr nz,EMM_GetMemBlkPages_loop	;10bc	20 e6		  .
EMM_GetMemBlkPages_error:
				; = EMM.GetMemBlkPages.error (src: FUNC_RAM_ROM_DRV.ASM:376, BIOS-TT 0271ac3)
	scf			;10be	37		7
	pop hl			;10bf	e1		.
	pop de			;10c0	d1		.
	ret			;10c1	c9		.
EMM_GetMemBlkPages_end:
				; = EMM.GetMemBlkPages.end (src: FUNC_RAM_ROM_DRV.ASM:381, BIOS-TT 0271ac3)
	pop hl			;10c2	e1		.
	pop de			;10c3	d1		.
	and a			;10c4	a7		.
	ret			;10c5	c9		.
Fn96_Unnamed:
	ld c,082h		;10c6	0e 82		. .
	in b,(c)		;10c8	ed 40		. @
	and a			;10ca	a7		.
	ret z			;10cb	c8		.
	ld c,0a2h		;10cc	0e a2		. .
	in b,(c)		;10ce	ed 40		. @
	dec a			;10d0	3d		=
	ret z			;10d1	c8		.
	ld c,0c2h		;10d2	0e c2		. .
	in b,(c)		;10d4	ed 40		. @
	dec a			;10d6	3d		=
	ret z			;10d7	c8		.
	ld c,0e2h		;10d8	0e e2		. .
	in b,(c)		;10da	ed 40		. @
	dec a			;10dc	3d		=
	ret z			;10dd	c8		.
	scf			;10de	37		7
	ret			;10df	c9		.
FnC8_BLK_RD_WR:
	and a			;10e0	a7		.
	scf			;10e1	37		7
	ret z			;10e2	c8		.
BLK_RD_WR_start:
				; = BLK_RD_WR.start (src: FUNC_RAM_ROM_DRV.ASM:427, BIOS-TT 0271ac3)
	ex af,af'		;10e3	08		.
	and a			;10e4	a7		.
	jr z,l10ffh		;10e5	28 18		( .
	cp 0ffh			;10e7	fe ff		. .
	jr z,l10ffh		;10e9	28 14		( .
	cp 005h			;10eb	fe 05		. .
	jr z,l10fbh		;10ed	28 0c		( .
	cp 006h			;10ef	fe 06		. .
	jr z,l10fbh		;10f1	28 08		( .
	cp 046h			;10f3	fe 46		. F
	jp z,l3f10h		;10f5	ca 10 3f	. . ?
	ex af,af'		;10f8	08		.
	scf			;10f9	37		7
	ret			;10fa	c9		.
l10fbh:
	cp 006h			;10fb	fe 06		. .
	jr l1100h		;10fd	18 01		. .
l10ffh:
	and a			;10ff	a7		.
l1100h:
	ex af,af'		;1100	08		.
	push hl			;1101	e5		.
	push bc			;1102	c5		.
	ld c,0c2h		;1103	0e c2		. .
	in b,(c)		;1105	ed 40		. @
	ld a,0feh		;1107	3e fe		> .
	out (c),a		;1109	ed 79		. y
	ld h,082h		;110b	26 82		& .
	ld l,a			;110d	6f		o
	inc d			;110e	14		.
RAMD_LOOP_D:
				; = RAMD_LOOP_D (src: FUNC_RAM_ROM_DRV.ASM:464, BIOS-TT 0271ac3)
	dec d			;110f	15		.
	jr z,NOT_FOUR_BLK	;1110	28 06		( .
	ld l,(hl)		;1112	6e		n
	ld l,(hl)		;1113	6e		n
	ld l,(hl)		;1114	6e		n
	ld l,(hl)		;1115	6e		n
	jr RAMD_LOOP_D		;1116	18 f7		. .
NOT_FOUR_BLK:
				; = NOT_FOUR_BLK (src: FUNC_RAM_ROM_DRV.ASM:473, BIOS-TT 0271ac3)
	ld a,e			;1118	7b		{
NOT_FOUR_BLK_loop:
				; = NOT_FOUR_BLK.loop (src: FUNC_RAM_ROM_DRV.ASM:475, BIOS-TT 0271ac3)
	sub 040h		;1119	d6 40		. @
	jr c,NOT_ONE_BLK	;111b	38 03		8 .
	ld l,(hl)		;111d	6e		n
	jr NOT_FOUR_BLK_loop	;111e	18 f9		. .
NOT_ONE_BLK:
				; = NOT_ONE_BLK (src: FUNC_RAM_ROM_DRV.ASM:480, BIOS-TT 0271ac3)
	and 03fh		;1120	e6 3f		. ?
	ld d,a			;1122	57		W
	ld e,000h		;1123	1e 00		. .
	ld a,l			;1125	7d		}
	out (c),b		;1126	ed 41		. A
	pop bc			;1128	c1		.
	pop hl			;1129	e1		.
	bit 7,h			;112a	cb 7c		. |
	jr nz,BLK_PAGE1		;112c	20 0c		  .
BLK_PAGE3:
				; = BLK_PAGE3 (src: FUNC_RAM_ROM_DRV.ASM:495, BIOS-TT 0271ac3)
	ld c,0e2h		;112e	0e e2		. .
	in c,(c)		;1130	ed 48		. H
	out (0e2h),a		;1132	d3 e2		. .
	set 7,d			;1134	cb fa		. .
	set 6,d			;1136	cb f2		. .
	jr BLK_CONT1		;1138	18 0a		. .
BLK_PAGE1:
				; = BLK_PAGE1 (src: FUNC_RAM_ROM_DRV.ASM:503, BIOS-TT 0271ac3)
	ld c,0a2h		;113a	0e a2		. .
	in c,(c)		;113c	ed 48		. H
	out (0a2h),a		;113e	d3 a2		. .
	res 7,d			;1140	cb ba		. .
	set 6,d			;1142	cb f2		. .
BLK_CONT1:
				; = BLK_CONT1 (src: FUNC_RAM_ROM_DRV.ASM:510, BIOS-TT 0271ac3)
	ex af,af'		;1144	08		.
	jr z,NO_EX_RW1		;1145	28 01		( .
	ex de,hl		;1147	eb		.
NO_EX_RW1:
				; = NO_EX_RW1 (src: FUNC_RAM_ROM_DRV.ASM:514, BIOS-TT 0271ac3)
	ex af,af'		;1148	08		.
	ld a,010h		;1149	3e 10		> .
l114bh:
	ldi			;114b	ed a0		. .
	ldi			;114d	ed a0		. .
	ldi			;114f	ed a0		. .
	ldi			;1151	ed a0		. .
	ldi			;1153	ed a0		. .
	ldi			;1155	ed a0		. .
	ldi			;1157	ed a0		. .
	ldi			;1159	ed a0		. .
	ldi			;115b	ed a0		. .
	ldi			;115d	ed a0		. .
	ldi			;115f	ed a0		. .
	ldi			;1161	ed a0		. .
	ldi			;1163	ed a0		. .
	ldi			;1165	ed a0		. .
	ldi			;1167	ed a0		. .
	ldi			;1169	ed a0		. .
	dec a			;116b	3d		=
	jr nz,l114bh		;116c	20 dd		  .
	ex af,af'		;116e	08		.
	jr z,NO_EX_RW2		;116f	28 01		( .
	ex de,hl		;1171	eb		.
NO_EX_RW2:
				; = NO_EX_RW2 (src: FUNC_RAM_ROM_DRV.ASM:529, BIOS-TT 0271ac3)
	ex af,af'		;1172	08		.
	inc b			;1173	04		.
	dec b			;1174	05		.
	jp z,BLK_EXIT_1		;1175	ca b7 11	. . .
	bit 6,d			;1178	cb 72		. r
	jp nz,BLK_CONT1		;117a	c2 44 11	. D .
	bit 7,d			;117d	cb 7a		. z
	jr z,BLK_PAGE3_X	;117f	28 12		( .
	in a,(0a2h)		;1181	db a2		. .
	ld e,a			;1183	5f		_
	ld d,042h		;1184	16 42		. B
	ld a,0feh		;1186	3e fe		> .
	out (0a2h),a		;1188	d3 a2		. .
	ld a,(de)		;118a	1a		.
	out (0a2h),a		;118b	d3 a2		. .
	ld de,04000h		;118d	11 00 40	. . @
	jp BLK_CONT1		;1190	c3 44 11	. D .
BLK_PAGE3_X:
				; = BLK_PAGE3_X (src: FUNC_RAM_ROM_DRV.ASM:551, BIOS-TT 0271ac3)
	in a,(0e2h)		;1193	db e2		. .
	ld e,a			;1195	5f		_
	ld d,0c2h		;1196	16 c2		. .
	ld a,0feh		;1198	3e fe		> .
	out (0e2h),a		;119a	d3 e2		. .
	ld a,(de)		;119c	1a		.
	out (0e2h),a		;119d	d3 e2		. .
	ld de,0c000h		;119f	11 00 c0	. . .
	bit 7,h			;11a2	cb 7c		. |
	jp z,BLK_CONT1		;11a4	ca 44 11	. D .
	ld e,a			;11a7	5f		_
	ld a,c			;11a8	79		y
	out (0e2h),a		;11a9	d3 e2		. .
	in a,(0a2h)		;11ab	db a2		. .
	ld c,a			;11ad	4f		O
	ld a,e			;11ae	7b		{
	out (0a2h),a		;11af	d3 a2		. .
	ld de,04000h		;11b1	11 00 40	. . @
	jp BLK_CONT1		;11b4	c3 44 11	. D .
BLK_EXIT_1:
				; = BLK_EXIT_1 (src: FUNC_RAM_ROM_DRV.ASM:574, BIOS-TT 0271ac3)
	ld a,d			;11b7	7a		z
	cp 0a0h			;11b8	fe a0		. .
	jr nc,l11c1h		;11ba	30 05		0 .
	ld a,c			;11bc	79		y
	out (0a2h),a		;11bd	d3 a2		. .
	and a			;11bf	a7		.
	ret			;11c0	c9		.
l11c1h:
	ld a,c			;11c1	79		y
	out (0e2h),a		;11c2	d3 e2		. .
	and a			;11c4	a7		.
	ret			;11c5	c9		.
Fn9A_GET_DISK_REDIR:
	push hl			;11c6	e5		.
	ld hl,08100h		;11c7	21 00 81	! . .
	in a,(0c2h)		;11ca	db c2		. .
	ex af,af'		;11cc	08		.
	ld a,0feh		;11cd	3e fe		> .
	out (0c2h),a		;11cf	d3 c2		. .
	ld a,(05cf6h)		;11d1	3a f6 5c	: . \
	and 003h		;11d4	e6 03		. .
	add a,l			;11d6	85		.
	ld l,a			;11d7	6f		o
	ld l,(hl)		;11d8	6e		n
	ex af,af'		;11d9	08		.
	out (0c2h),a		;11da	d3 c2		. .
	ld a,l			;11dc	7d		}
	pop hl			;11dd	e1		.
	ret			;11de	c9		.
Fn99_SET_DISK_REDIR:
	push hl			;11df	e5		.
	ld hl,08100h		;11e0	21 00 81	! . .
	in a,(0c2h)		;11e3	db c2		. .
	ex af,af'		;11e5	08		.
	ld a,0feh		;11e6	3e fe		> .
	out (0c2h),a		;11e8	d3 c2		. .
	ld a,(05cf6h)		;11ea	3a f6 5c	: . \
	and 003h		;11ed	e6 03		. .
	add a,l			;11ef	85		.
	ld l,a			;11f0	6f		o
	ld (hl),e		;11f1	73		s
	ld l,(hl)		;11f2	6e		n
	ex af,af'		;11f3	08		.
	out (0c2h),a		;11f4	d3 c2		. .
	ld a,l			;11f6	7d		}
	pop hl			;11f7	e1		.
	ret			;11f8	c9		.
Fn98_RAMD_CALC_PAGE:
				; = ROM_DISK.stackDepth (src: FUNC_RAM_ROM_DRV.ASM:753, BIOS-TT 0271ac3)
				; = ROM_DISK.readProcedure.size (src: FUNC_RAM_ROM_DRV.ASM:754, BIOS-TT 0271ac3)
				; = RAMD_CALC_PAGE (src: FUNC_RAM_ROM_DRV.ASM:764, BIOS-TT 0271ac3)
	cp 010h			;11f9	fe 10		. .
	ccf			;11fb	3f		?
	ret c			;11fc	d8		.
	push af			;11fd	f5		.
	ld h,d			;11fe	62		b
	ld l,e			;11ff	6b		k
	add hl,hl		;1200	29		)
	add hl,hl		;1201	29		)
	ld b,h			;1202	44		D
	ld a,e			;1203	7b		{
	or 0c0h			;1204	f6 c0		. .
	ld c,a			;1206	4f		O
	pop af			;1207	f1		.
	call Fn94_GetMemPageRMD	;1208	cd 7f 10	. . .
	ld l,000h		;120b	2e 00		. .
	ld h,c			;120d	61		a
	ret			;120e	c9		.
FnEF_FN_VERSION:
				; = ~FN_VERSION (src: FUNC_SYS.ASM:52, BIOS-TT 0271ac3)
	push hl			;120f	e5		.
	ex de,hl		;1210	eb		.
	ld hl,l00c2h		;1211	21 c2 00	! . .
	ld c,(hl)		;1214	4e		N
	inc hl			;1215	23		#
	ld b,000h		;1216	06 00		. .
	ldir			;1218	ed b0		. .
	pop hl			;121a	e1		.
	ld de,(BiosVersionText)	;121b	ed 5b c0 00	. [ . .
	in a,(0c2h)		;121f	db c2		. .
	ex af,af'		;1221	08		.
	ld a,0feh		;1222	3e fe		> .
	out (0c2h),a		;1224	d3 c2		. .
	ld bc,(0813eh)		;1226	ed 4b 3e 81	. K > .
	ex af,af'		;122a	08		.
	out (0c2h),a		;122b	d3 c2		. .
	ld a,002h		;122d	3e 02		> .
	and a			;122f	a7		.
	ret			;1230	c9		.
Fn97_CheckInit:
				; = ~EMM.CheckColdInit (src: FUNC_SYS.ASM:94, BIOS-TT 0271ac3)
	in a,(0c2h)		;1231	db c2		. .
	ex af,af'		;1233	08		.
	ld a,0feh		;1234	3e fe		> .
	out (0c2h),a		;1236	d3 c2		. .
	ld hl,08000h		;1238	21 00 80	! . .
	ld de,l00c2h		;123b	11 c2 00	. . .
	ld a,(de)		;123e	1a		.
	inc de			;123f	13		.
	ld b,a			;1240	47		G
EMM_CheckColdInit_loop:
				; = ~EMM.CheckColdInit.loop (src: FUNC_SYS.ASM:105, BIOS-TT 0271ac3)
	ld a,(de)		;1241	1a		.
	cp (hl)			;1242	be		.
	jr nz,l1255h		;1243	20 10		  .
	inc hl			;1245	23		#
	inc de			;1246	13		.
	djnz EMM_CheckColdInit_loop	;1247	10 f8		. .
	ex af,af'		;1249	08		.
	out (0c2h),a		;124a	d3 c2		. .
	ret			;124c	c9		.
Fn9F_FullInit:
	di			;124d	f3		.
	in a,(0c2h)		;124e	db c2		. .
	ex af,af'		;1250	08		.
	ld a,0feh		;1251	3e fe		> .
	out (0c2h),a		;1253	d3 c2		. .
l1255h:
	ld de,08000h		;1255	11 00 80	. . .
	ld hl,l00c2h		;1258	21 c2 00	! . .
	ld c,(hl)		;125b	4e		N
	inc hl			;125c	23		#
	ld b,000h		;125d	06 00		. .
	ldir			;125f	ed b0		. .
	ld hl,08100h		;1261	21 00 81	! . .
	ld (hl),000h		;1264	36 00		6 .
	inc hl			;1266	23		#
	ld (hl),001h		;1267	36 01		6 .
	inc hl			;1269	23		#
	ld (hl),040h		;126a	36 40		6 @
	inc hl			;126c	23		#
	ld (hl),003h		;126d	36 03		6 .
	inc hl			;126f	23		#
	ld (hl),000h		;1270	36 00		6 .
	inc hl			;1272	23		#
	ld (hl),005h		;1273	36 05		6 .
	inc hl			;1275	23		#
	ld (hl),002h		;1276	36 02		6 .
	inc hl			;1278	23		#
	ld (hl),000h		;1279	36 00		6 .
	ld a,009h		;127b	3e 09		> .
	ld (0811fh),a		;127d	32 1f 81	2 . .
	ld a,000h		;1280	3e 00		> .
	ld (08128h),a		;1282	32 28 81	2 ( .
	ld hl,08180h		;1285	21 80 81	! . .
	ld de,08181h		;1288	11 81 81	. . .
	ld bc,l000eh+1		;128b	01 0f 00	. . .
	ld (hl),000h		;128e	36 00		6 .
	ldir			;1290	ed b0		. .
	ld de,Font8x8	;1292	11 00 28	. . (
	ld (0814ah),de		;1295	ed 53 4a 81	. S J .
INIT_CONFIG_ALL_setDefaultINT:
				; = ~INIT_CONFIG_ALL.setDefaultINT (src: FUNC_SYS.ASM:199, BIOS-TT 0271ac3)
	ld hl,l0c75h		;1299	21 75 0c	! u .
INIT_CONFIG_ALL_setINT:
				; = ~INIT_CONFIG_ALL.setINT (src: FUNC_SYS.ASM:201, BIOS-TT 0271ac3)
	ld (08138h),hl		;129c	22 38 81	" 8 .
	ld de,l0004h		;129f	11 04 00	. . .
	ld (0813ah),de		;12a2	ed 53 3a 81	. S : .
	ld hl,0ac00h		;12a6	21 00 ac	! . .
	ld de,0ac01h		;12a9	11 01 ac	. . .
	ld bc,l00ffh		;12ac	01 ff 00	. . .
	ld (hl),000h		;12af	36 00		6 .
	ldir			;12b1	ed b0		. .
	ex af,af'		;12b3	08		.
	out (0c2h),a		;12b4	d3 c2		. .
	call Fn91_InitMem_old	;12b6	cd 74 0f	. t .
	in a,(0e2h)		;12b9	db e2		. .
	push af			;12bb	f5		.
	ld a,0feh		;12bc	3e fe		> .
	out (0e2h),a		;12be	d3 e2		. .
	pop af			;12c0	f1		.
	out (0e2h),a		;12c1	d3 e2		. .
	ret			;12c3	c9		.

; BLOCK 'Fill12C4' (start 0x12c4 end 0x1400)
Fill12C4:
	defb 0ffh		;12c4	ff		.
	defb 0ffh		;12c5	ff		.
	defb 0ffh		;12c6	ff		.
	defb 0ffh		;12c7	ff		.
	defb 0ffh		;12c8	ff		.
	defb 0ffh		;12c9	ff		.
	defb 0ffh		;12ca	ff		.
	defb 0ffh		;12cb	ff		.
	defb 0ffh		;12cc	ff		.
	defb 0ffh		;12cd	ff		.
	defb 0ffh		;12ce	ff		.
	defb 0ffh		;12cf	ff		.
	defb 0ffh		;12d0	ff		.
	defb 0ffh		;12d1	ff		.
	defb 0ffh		;12d2	ff		.
	defb 0ffh		;12d3	ff		.
	defb 0ffh		;12d4	ff		.
	defb 0ffh		;12d5	ff		.
	defb 0ffh		;12d6	ff		.
	defb 0ffh		;12d7	ff		.
	defb 0ffh		;12d8	ff		.
	defb 0ffh		;12d9	ff		.
	defb 0ffh		;12da	ff		.
	defb 0ffh		;12db	ff		.
	defb 0ffh		;12dc	ff		.
	defb 0ffh		;12dd	ff		.
	defb 0ffh		;12de	ff		.
	defb 0ffh		;12df	ff		.
	defb 0ffh		;12e0	ff		.
	defb 0ffh		;12e1	ff		.
	defb 0ffh		;12e2	ff		.
	defb 0ffh		;12e3	ff		.
	defb 0ffh		;12e4	ff		.
	defb 0ffh		;12e5	ff		.
	defb 0ffh		;12e6	ff		.
	defb 0ffh		;12e7	ff		.
	defb 0ffh		;12e8	ff		.
	defb 0ffh		;12e9	ff		.
	defb 0ffh		;12ea	ff		.
	defb 0ffh		;12eb	ff		.
	defb 0ffh		;12ec	ff		.
	defb 0ffh		;12ed	ff		.
	defb 0ffh		;12ee	ff		.
	defb 0ffh		;12ef	ff		.
	defb 0ffh		;12f0	ff		.
	defb 0ffh		;12f1	ff		.
	defb 0ffh		;12f2	ff		.
	defb 0ffh		;12f3	ff		.
	defb 0ffh		;12f4	ff		.
	defb 0ffh		;12f5	ff		.
	defb 0ffh		;12f6	ff		.
	defb 0ffh		;12f7	ff		.
	defb 0ffh		;12f8	ff		.
	defb 0ffh		;12f9	ff		.
	defb 0ffh		;12fa	ff		.
	defb 0ffh		;12fb	ff		.
	defb 0ffh		;12fc	ff		.
	defb 0ffh		;12fd	ff		.
	defb 0ffh		;12fe	ff		.
	defb 0ffh		;12ff	ff		.
	defb 0ffh		;1300	ff		.
	defb 0ffh		;1301	ff		.
	defb 0ffh		;1302	ff		.
	defb 0ffh		;1303	ff		.
	defb 0ffh		;1304	ff		.
	defb 0ffh		;1305	ff		.
	defb 0ffh		;1306	ff		.
	defb 0ffh		;1307	ff		.
	defb 0ffh		;1308	ff		.
	defb 0ffh		;1309	ff		.
	defb 0ffh		;130a	ff		.
	defb 0ffh		;130b	ff		.
	defb 0ffh		;130c	ff		.
	defb 0ffh		;130d	ff		.
	defb 0ffh		;130e	ff		.
	defb 0ffh		;130f	ff		.
	defb 0ffh		;1310	ff		.
	defb 0ffh		;1311	ff		.
	defb 0ffh		;1312	ff		.
	defb 0ffh		;1313	ff		.
	defb 0ffh		;1314	ff		.
	defb 0ffh		;1315	ff		.
	defb 0ffh		;1316	ff		.
	defb 0ffh		;1317	ff		.
	defb 0ffh		;1318	ff		.
	defb 0ffh		;1319	ff		.
	defb 0ffh		;131a	ff		.
	defb 0ffh		;131b	ff		.
	defb 0ffh		;131c	ff		.
	defb 0ffh		;131d	ff		.
	defb 0ffh		;131e	ff		.
	defb 0ffh		;131f	ff		.
	defb 0ffh		;1320	ff		.
	defb 0ffh		;1321	ff		.
	defb 0ffh		;1322	ff		.
	defb 0ffh		;1323	ff		.
	defb 0ffh		;1324	ff		.
	defb 0ffh		;1325	ff		.
	defb 0ffh		;1326	ff		.
	defb 0ffh		;1327	ff		.
	defb 0ffh		;1328	ff		.
	defb 0ffh		;1329	ff		.
	defb 0ffh		;132a	ff		.
	defb 0ffh		;132b	ff		.
	defb 0ffh		;132c	ff		.
	defb 0ffh		;132d	ff		.
	defb 0ffh		;132e	ff		.
	defb 0ffh		;132f	ff		.
	defb 0ffh		;1330	ff		.
	defb 0ffh		;1331	ff		.
	defb 0ffh		;1332	ff		.
	defb 0ffh		;1333	ff		.
	defb 0ffh		;1334	ff		.
	defb 0ffh		;1335	ff		.
	defb 0ffh		;1336	ff		.
	defb 0ffh		;1337	ff		.
	defb 0ffh		;1338	ff		.
	defb 0ffh		;1339	ff		.
	defb 0ffh		;133a	ff		.
	defb 0ffh		;133b	ff		.
	defb 0ffh		;133c	ff		.
	defb 0ffh		;133d	ff		.
	defb 0ffh		;133e	ff		.
	defb 0ffh		;133f	ff		.
	defb 0ffh		;1340	ff		.
	defb 0ffh		;1341	ff		.
	defb 0ffh		;1342	ff		.
	defb 0ffh		;1343	ff		.
	defb 0ffh		;1344	ff		.
	defb 0ffh		;1345	ff		.
	defb 0ffh		;1346	ff		.
	defb 0ffh		;1347	ff		.
	defb 0ffh		;1348	ff		.
	defb 0ffh		;1349	ff		.
	defb 0ffh		;134a	ff		.
	defb 0ffh		;134b	ff		.
	defb 0ffh		;134c	ff		.
	defb 0ffh		;134d	ff		.
	defb 0ffh		;134e	ff		.
	defb 0ffh		;134f	ff		.
	defb 0ffh		;1350	ff		.
	defb 0ffh		;1351	ff		.
	defb 0ffh		;1352	ff		.
	defb 0ffh		;1353	ff		.
	defb 0ffh		;1354	ff		.
	defb 0ffh		;1355	ff		.
	defb 0ffh		;1356	ff		.
	defb 0ffh		;1357	ff		.
	defb 0ffh		;1358	ff		.
	defb 0ffh		;1359	ff		.
	defb 0ffh		;135a	ff		.
	defb 0ffh		;135b	ff		.
	defb 0ffh		;135c	ff		.
	defb 0ffh		;135d	ff		.
	defb 0ffh		;135e	ff		.
	defb 0ffh		;135f	ff		.
	defb 0ffh		;1360	ff		.
	defb 0ffh		;1361	ff		.
	defb 0ffh		;1362	ff		.
	defb 0ffh		;1363	ff		.
	defb 0ffh		;1364	ff		.
	defb 0ffh		;1365	ff		.
	defb 0ffh		;1366	ff		.
	defb 0ffh		;1367	ff		.
	defb 0ffh		;1368	ff		.
	defb 0ffh		;1369	ff		.
	defb 0ffh		;136a	ff		.
	defb 0ffh		;136b	ff		.
	defb 0ffh		;136c	ff		.
	defb 0ffh		;136d	ff		.
	defb 0ffh		;136e	ff		.
	defb 0ffh		;136f	ff		.
	defb 0ffh		;1370	ff		.
	defb 0ffh		;1371	ff		.
	defb 0ffh		;1372	ff		.
	defb 0ffh		;1373	ff		.
	defb 0ffh		;1374	ff		.
	defb 0ffh		;1375	ff		.
	defb 0ffh		;1376	ff		.
	defb 0ffh		;1377	ff		.
	defb 0ffh		;1378	ff		.
	defb 0ffh		;1379	ff		.
	defb 0ffh		;137a	ff		.
	defb 0ffh		;137b	ff		.
	defb 0ffh		;137c	ff		.
	defb 0ffh		;137d	ff		.
	defb 0ffh		;137e	ff		.
	defb 0ffh		;137f	ff		.
	defb 0ffh		;1380	ff		.
	defb 0ffh		;1381	ff		.
	defb 0ffh		;1382	ff		.
	defb 0ffh		;1383	ff		.
	defb 0ffh		;1384	ff		.
	defb 0ffh		;1385	ff		.
	defb 0ffh		;1386	ff		.
	defb 0ffh		;1387	ff		.
	defb 0ffh		;1388	ff		.
	defb 0ffh		;1389	ff		.
	defb 0ffh		;138a	ff		.
	defb 0ffh		;138b	ff		.
	defb 0ffh		;138c	ff		.
	defb 0ffh		;138d	ff		.
	defb 0ffh		;138e	ff		.
	defb 0ffh		;138f	ff		.
	defb 0ffh		;1390	ff		.
	defb 0ffh		;1391	ff		.
	defb 0ffh		;1392	ff		.
	defb 0ffh		;1393	ff		.
	defb 0ffh		;1394	ff		.
	defb 0ffh		;1395	ff		.
	defb 0ffh		;1396	ff		.
	defb 0ffh		;1397	ff		.
	defb 0ffh		;1398	ff		.
	defb 0ffh		;1399	ff		.
	defb 0ffh		;139a	ff		.
	defb 0ffh		;139b	ff		.
	defb 0ffh		;139c	ff		.
	defb 0ffh		;139d	ff		.
	defb 0ffh		;139e	ff		.
	defb 0ffh		;139f	ff		.
	defb 0ffh		;13a0	ff		.
	defb 0ffh		;13a1	ff		.
	defb 0ffh		;13a2	ff		.
	defb 0ffh		;13a3	ff		.
	defb 0ffh		;13a4	ff		.
	defb 0ffh		;13a5	ff		.
	defb 0ffh		;13a6	ff		.
	defb 0ffh		;13a7	ff		.
	defb 0ffh		;13a8	ff		.
	defb 0ffh		;13a9	ff		.
	defb 0ffh		;13aa	ff		.
	defb 0ffh		;13ab	ff		.
	defb 0ffh		;13ac	ff		.
	defb 0ffh		;13ad	ff		.
	defb 0ffh		;13ae	ff		.
	defb 0ffh		;13af	ff		.
	defb 0ffh		;13b0	ff		.
	defb 0ffh		;13b1	ff		.
	defb 0ffh		;13b2	ff		.
	defb 0ffh		;13b3	ff		.
	defb 0ffh		;13b4	ff		.
	defb 0ffh		;13b5	ff		.
	defb 0ffh		;13b6	ff		.
	defb 0ffh		;13b7	ff		.
	defb 0ffh		;13b8	ff		.
	defb 0ffh		;13b9	ff		.
	defb 0ffh		;13ba	ff		.
	defb 0ffh		;13bb	ff		.
	defb 0ffh		;13bc	ff		.
	defb 0ffh		;13bd	ff		.
	defb 0ffh		;13be	ff		.
	defb 0ffh		;13bf	ff		.
	defb 0ffh		;13c0	ff		.
	defb 0ffh		;13c1	ff		.
	defb 0ffh		;13c2	ff		.
	defb 0ffh		;13c3	ff		.
	defb 0ffh		;13c4	ff		.
	defb 0ffh		;13c5	ff		.
	defb 0ffh		;13c6	ff		.
	defb 0ffh		;13c7	ff		.
	defb 0ffh		;13c8	ff		.
	defb 0ffh		;13c9	ff		.
	defb 0ffh		;13ca	ff		.
	defb 0ffh		;13cb	ff		.
	defb 0ffh		;13cc	ff		.
	defb 0ffh		;13cd	ff		.
	defb 0ffh		;13ce	ff		.
	defb 0ffh		;13cf	ff		.
	defb 0ffh		;13d0	ff		.
	defb 0ffh		;13d1	ff		.
	defb 0ffh		;13d2	ff		.
	defb 0ffh		;13d3	ff		.
	defb 0ffh		;13d4	ff		.
	defb 0ffh		;13d5	ff		.
	defb 0ffh		;13d6	ff		.
	defb 0ffh		;13d7	ff		.
	defb 0ffh		;13d8	ff		.
	defb 0ffh		;13d9	ff		.
	defb 0ffh		;13da	ff		.
	defb 0ffh		;13db	ff		.
	defb 0ffh		;13dc	ff		.
	defb 0ffh		;13dd	ff		.
	defb 0ffh		;13de	ff		.
	defb 0ffh		;13df	ff		.
	defb 0ffh		;13e0	ff		.
	defb 0ffh		;13e1	ff		.
	defb 0ffh		;13e2	ff		.
	defb 0ffh		;13e3	ff		.
	defb 0ffh		;13e4	ff		.
	defb 0ffh		;13e5	ff		.
	defb 0ffh		;13e6	ff		.
	defb 0ffh		;13e7	ff		.
	defb 0ffh		;13e8	ff		.
	defb 0ffh		;13e9	ff		.
	defb 0ffh		;13ea	ff		.
	defb 0ffh		;13eb	ff		.
	defb 0ffh		;13ec	ff		.
	defb 0ffh		;13ed	ff		.
	defb 0ffh		;13ee	ff		.
	defb 0ffh		;13ef	ff		.
	defb 0ffh		;13f0	ff		.
	defb 0ffh		;13f1	ff		.
	defb 0ffh		;13f2	ff		.
	defb 0ffh		;13f3	ff		.
	defb 0ffh		;13f4	ff		.
	defb 0ffh		;13f5	ff		.
	defb 0ffh		;13f6	ff		.
	defb 0ffh		;13f7	ff		.
	defb 0ffh		;13f8	ff		.
	defb 0ffh		;13f9	ff		.
	defb 0ffh		;13fa	ff		.
	defb 0ffh		;13fb	ff		.
	defb 0ffh		;13fc	ff		.
	defb 0ffh		;13fd	ff		.
	defb 0ffh		;13fe	ff		.
	defb 0ffh		;13ff	ff		.
;---------------------------------------------------------------------------
; DcpTablePacked: the port table in the format DcpInit reads (#1400-#27F3).
; Unpacked it is byte for byte the page dump old_files/DCP_PAGE.bin of
; BIOS-TT 0271ac3 (see hardware-reference.md 4.4 for the decoded table).
;---------------------------------------------------------------------------
DcpTablePacked:

; BLOCK 'DcpTablePacked' (start 0x1400 end 0x27f4)
	defb 001h		;1400	01		.
	defb 010h		;1401	10		.
	defb 060h		;1402	60		`
	defb 0c4h		;1403	c4		.
	defb 0e8h		;1404	e8		.
	defb 001h		;1405	01		.
	defb 010h		;1406	10		.
	defb 060h		;1407	60		`
	defb 0c4h		;1408	c4		.
	defb 0e8h		;1409	e8		.
	defb 009h		;140a	09		.
	defb 0c6h		;140b	c6		.
	defb 011h		;140c	11		.
	defb 02ch		;140d	2c		,
	defb 0e9h		;140e	e9		.
	defb 02ah		;140f	2a		*
	defb 016h		;1410	16		.
	defb 009h		;1411	09		.
	defb 0c6h		;1412	c6		.
	defb 011h		;1413	11		.
	defb 02ch		;1414	2c		,
	defb 0e9h		;1415	e9		.
	defb 02bh		;1416	2b		+
	defb 017h		;1417	17		.
	defb 0ffh		;1418	ff		.
	defb 020h		;1419	20		 
	defb 021h		;141a	21		!
	defb 022h		;141b	22		"
	defb 023h		;141c	23		#
	defb 024h		;141d	24		$
	defb 025h		;141e	25		%
	defb 089h		;141f	89		.
	defb 012h		;1420	12		.
	defb 060h		;1421	60		`
	defb 0c5h		;1422	c5		.
	defb 0eah		;1423	ea		.
	defb 0ffh		;1424	ff		.
	defb 020h		;1425	20		 
	defb 021h		;1426	21		!
	defb 022h		;1427	22		"
	defb 023h		;1428	23		#
	defb 024h		;1429	24		$
	defb 025h		;142a	25		%
	defb 0c3h		;142b	c3		.
	defb 012h		;142c	12		.
	defb 060h		;142d	60		`
	defb 0c5h		;142e	c5		.
	defb 0eah		;142f	ea		.
	defb 009h		;1430	09		.
	defb 0c6h		;1431	c6		.
	defb 013h		;1432	13		.
	defb 02fh		;1433	2f		/
	defb 0f0h		;1434	f0		.
	defb 0c7h		;1435	c7		.
	defb 0c0h		;1436	c0		.
	defb 0c2h		;1437	c2		.
	defb 014h		;1438	14		.
	defb 009h		;1439	09		.
	defb 0c6h		;143a	c6		.
	defb 013h		;143b	13		.
	defb 02bh		;143c	2b		+
	defb 0f0h		;143d	f0		.
	defb 0c7h		;143e	c7		.
	defb 0c2h		;143f	c2		.
	defb 014h		;1440	14		.
	defb 001h		;1441	01		.
	defb 010h		;1442	10		.
	defb 060h		;1443	60		`
	defb 0c4h		;1444	c4		.
	defb 0e8h		;1445	e8		.
	defb 001h		;1446	01		.
	defb 010h		;1447	10		.
	defb 060h		;1448	60		`
	defb 0c4h		;1449	c4		.
	defb 0e8h		;144a	e8		.
	defb 009h		;144b	09		.
	defb 0c6h		;144c	c6		.
	defb 011h		;144d	11		.
	defb 02ch		;144e	2c		,
	defb 0e9h		;144f	e9		.
	defb 02eh		;1450	2e		.
	defb 02ch		;1451	2c		,
	defb 009h		;1452	09		.
	defb 0c6h		;1453	c6		.
	defb 011h		;1454	11		.
	defb 02ch		;1455	2c		,
	defb 0e9h		;1456	e9		.
	defb 02fh		;1457	2f		/
	defb 02dh		;1458	2d		-
	defb 0bdh		;1459	bd		.
	defb 020h		;145a	20		 
	defb 026h		;145b	26		&
	defb 027h		;145c	27		'
	defb 028h		;145d	28		(
	defb 029h		;145e	29		)
	defb 012h		;145f	12		.
	defb 060h		;1460	60		`
	defb 0c5h		;1461	c5		.
	defb 0eah		;1462	ea		.
	defb 0bdh		;1463	bd		.
	defb 020h		;1464	20		 
	defb 026h		;1465	26		&
	defb 027h		;1466	27		'
	defb 028h		;1467	28		(
	defb 029h		;1468	29		)
	defb 012h		;1469	12		.
	defb 060h		;146a	60		`
	defb 0c5h		;146b	c5		.
	defb 0eah		;146c	ea		.
	defb 009h		;146d	09		.
	defb 0c6h		;146e	c6		.
	defb 013h		;146f	13		.
	defb 02fh		;1470	2f		/
	defb 0f0h		;1471	f0		.
	defb 0c7h		;1472	c7		.
	defb 0c1h		;1473	c1		.
	defb 0c2h		;1474	c2		.
	defb 014h		;1475	14		.
	defb 009h		;1476	09		.
	defb 0c6h		;1477	c6		.
	defb 013h		;1478	13		.
	defb 02fh		;1479	2f		/
	defb 0f0h		;147a	f0		.
	defb 0c7h		;147b	c7		.
	defb 0c1h		;147c	c1		.
	defb 0c2h		;147d	c2		.
	defb 014h		;147e	14		.
	defb 001h		;147f	01		.
	defb 010h		;1480	10		.
	defb 060h		;1481	60		`
	defb 0c4h		;1482	c4		.
	defb 0e8h		;1483	e8		.
	defb 001h		;1484	01		.
	defb 010h		;1485	10		.
	defb 060h		;1486	60		`
	defb 0c4h		;1487	c4		.
	defb 0e8h		;1488	e8		.
	defb 009h		;1489	09		.
	defb 0c6h		;148a	c6		.
	defb 011h		;148b	11		.
	defb 024h		;148c	24		$
	defb 0e9h		;148d	e9		.
	defb 01bh		;148e	1b		.
	defb 009h		;148f	09		.
	defb 0c6h		;1490	c6		.
	defb 011h		;1491	11		.
	defb 024h		;1492	24		$
	defb 0e9h		;1493	e9		.
	defb 01eh		;1494	1e		.
	defb 081h		;1495	81		.
	defb 020h		;1496	20		 
	defb 012h		;1497	12		.
	defb 060h		;1498	60		`
	defb 0c5h		;1499	c5		.
	defb 0eah		;149a	ea		.
	defb 081h		;149b	81		.
	defb 020h		;149c	20		 
	defb 012h		;149d	12		.
	defb 060h		;149e	60		`
	defb 0c5h		;149f	c5		.
	defb 0eah		;14a0	ea		.
	defb 009h		;14a1	09		.
	defb 0c6h		;14a2	c6		.
	defb 013h		;14a3	13		.
	defb 02bh		;14a4	2b		+
	defb 0f0h		;14a5	f0		.
	defb 0c7h		;14a6	c7		.
	defb 0c2h		;14a7	c2		.
	defb 014h		;14a8	14		.
	defb 009h		;14a9	09		.
	defb 0c6h		;14aa	c6		.
	defb 013h		;14ab	13		.
	defb 02fh		;14ac	2f		/
	defb 0f0h		;14ad	f0		.
	defb 0c7h		;14ae	c7		.
	defb 091h		;14af	91		.
	defb 0c2h		;14b0	c2		.
	defb 014h		;14b1	14		.
	defb 001h		;14b2	01		.
	defb 010h		;14b3	10		.
	defb 060h		;14b4	60		`
	defb 0c4h		;14b5	c4		.
	defb 0e8h		;14b6	e8		.
	defb 001h		;14b7	01		.
	defb 010h		;14b8	10		.
	defb 060h		;14b9	60		`
	defb 0c4h		;14ba	c4		.
	defb 0e8h		;14bb	e8		.
	defb 009h		;14bc	09		.
	defb 0c6h		;14bd	c6		.
	defb 011h		;14be	11		.
	defb 024h		;14bf	24		$
	defb 0e9h		;14c0	e9		.
	defb 01dh		;14c1	1d		.
	defb 009h		;14c2	09		.
	defb 0c6h		;14c3	c6		.
	defb 011h		;14c4	11		.
	defb 024h		;14c5	24		$
	defb 0e9h		;14c6	e9		.
	defb 01eh		;14c7	1e		.
	defb 081h		;14c8	81		.
	defb 020h		;14c9	20		 
	defb 012h		;14ca	12		.
	defb 060h		;14cb	60		`
	defb 0c5h		;14cc	c5		.
	defb 0eah		;14cd	ea		.
	defb 081h		;14ce	81		.
	defb 020h		;14cf	20		 
	defb 012h		;14d0	12		.
	defb 060h		;14d1	60		`
	defb 0c5h		;14d2	c5		.
	defb 0eah		;14d3	ea		.
	defb 009h		;14d4	09		.
	defb 0c6h		;14d5	c6		.
	defb 013h		;14d6	13		.
	defb 02bh		;14d7	2b		+
	defb 0f0h		;14d8	f0		.
	defb 0c7h		;14d9	c7		.
	defb 0c2h		;14da	c2		.
	defb 014h		;14db	14		.
	defb 009h		;14dc	09		.
	defb 0c6h		;14dd	c6		.
	defb 013h		;14de	13		.
	defb 02fh		;14df	2f		/
	defb 0f0h		;14e0	f0		.
	defb 0c7h		;14e1	c7		.
	defb 090h		;14e2	90		.
	defb 0c2h		;14e3	c2		.
	defb 014h		;14e4	14		.
	defb 001h		;14e5	01		.
	defb 010h		;14e6	10		.
	defb 060h		;14e7	60		`
	defb 0c4h		;14e8	c4		.
	defb 0e8h		;14e9	e8		.
	defb 001h		;14ea	01		.
	defb 010h		;14eb	10		.
	defb 060h		;14ec	60		`
	defb 0c4h		;14ed	c4		.
	defb 0e8h		;14ee	e8		.
	defb 009h		;14ef	09		.
	defb 0c6h		;14f0	c6		.
	defb 011h		;14f1	11		.
	defb 020h		;14f2	20		 
	defb 0e9h		;14f3	e9		.
	defb 009h		;14f4	09		.
	defb 0c6h		;14f5	c6		.
	defb 011h		;14f6	11		.
	defb 020h		;14f7	20		 
	defb 0e9h		;14f8	e9		.
	defb 0fdh		;14f9	fd		.
	defb 020h		;14fa	20		 
	defb 021h		;14fb	21		!
	defb 022h		;14fc	22		"
	defb 023h		;14fd	23		#
	defb 024h		;14fe	24		$
	defb 025h		;14ff	25		%
	defb 012h		;1500	12		.
	defb 060h		;1501	60		`
	defb 0c5h		;1502	c5		.
	defb 0eah		;1503	ea		.
	defb 0fdh		;1504	fd		.
	defb 020h		;1505	20		 
	defb 021h		;1506	21		!
	defb 022h		;1507	22		"
	defb 023h		;1508	23		#
	defb 024h		;1509	24		$
	defb 025h		;150a	25		%
	defb 012h		;150b	12		.
	defb 060h		;150c	60		`
	defb 0c5h		;150d	c5		.
	defb 0eah		;150e	ea		.
	defb 009h		;150f	09		.
	defb 0c6h		;1510	c6		.
	defb 013h		;1511	13		.
	defb 025h		;1512	25		%
	defb 0f0h		;1513	f0		.
	defb 0c0h		;1514	c0		.
	defb 015h		;1515	15		.
	defb 009h		;1516	09		.
	defb 0c6h		;1517	c6		.
	defb 013h		;1518	13		.
	defb 021h		;1519	21		!
	defb 0f0h		;151a	f0		.
	defb 015h		;151b	15		.
	defb 001h		;151c	01		.
	defb 010h		;151d	10		.
	defb 060h		;151e	60		`
	defb 0c4h		;151f	c4		.
	defb 0e8h		;1520	e8		.
	defb 001h		;1521	01		.
	defb 010h		;1522	10		.
	defb 060h		;1523	60		`
	defb 0c4h		;1524	c4		.
	defb 0e8h		;1525	e8		.
	defb 009h		;1526	09		.
	defb 0c6h		;1527	c6		.
	defb 011h		;1528	11		.
	defb 020h		;1529	20		 
	defb 0e9h		;152a	e9		.
	defb 009h		;152b	09		.
	defb 0c6h		;152c	c6		.
	defb 011h		;152d	11		.
	defb 020h		;152e	20		 
	defb 0e9h		;152f	e9		.
	defb 0bdh		;1530	bd		.
	defb 020h		;1531	20		 
	defb 026h		;1532	26		&
	defb 027h		;1533	27		'
	defb 028h		;1534	28		(
	defb 029h		;1535	29		)
	defb 012h		;1536	12		.
	defb 060h		;1537	60		`
	defb 0c5h		;1538	c5		.
	defb 0eah		;1539	ea		.
	defb 0bdh		;153a	bd		.
	defb 020h		;153b	20		 
	defb 026h		;153c	26		&
	defb 027h		;153d	27		'
	defb 028h		;153e	28		(
	defb 029h		;153f	29		)
	defb 012h		;1540	12		.
	defb 060h		;1541	60		`
	defb 0c5h		;1542	c5		.
	defb 0eah		;1543	ea		.
	defb 009h		;1544	09		.
	defb 0c6h		;1545	c6		.
	defb 013h		;1546	13		.
	defb 025h		;1547	25		%
	defb 0f0h		;1548	f0		.
	defb 0c1h		;1549	c1		.
	defb 015h		;154a	15		.
	defb 009h		;154b	09		.
	defb 0c6h		;154c	c6		.
	defb 013h		;154d	13		.
	defb 025h		;154e	25		%
	defb 0f0h		;154f	f0		.
	defb 0c1h		;1550	c1		.
	defb 015h		;1551	15		.
	defb 001h		;1552	01		.
	defb 010h		;1553	10		.
	defb 060h		;1554	60		`
	defb 0c4h		;1555	c4		.
	defb 0e8h		;1556	e8		.
	defb 001h		;1557	01		.
	defb 010h		;1558	10		.
	defb 060h		;1559	60		`
	defb 0c4h		;155a	c4		.
	defb 0e8h		;155b	e8		.
	defb 009h		;155c	09		.
	defb 0c6h		;155d	c6		.
	defb 011h		;155e	11		.
	defb 020h		;155f	20		 
	defb 0e9h		;1560	e9		.
	defb 009h		;1561	09		.
	defb 0c6h		;1562	c6		.
	defb 011h		;1563	11		.
	defb 024h		;1564	24		$
	defb 0e9h		;1565	e9		.
	defb 01ch		;1566	1c		.
	defb 081h		;1567	81		.
	defb 020h		;1568	20		 
	defb 012h		;1569	12		.
	defb 060h		;156a	60		`
	defb 0c5h		;156b	c5		.
	defb 0eah		;156c	ea		.
	defb 081h		;156d	81		.
	defb 020h		;156e	20		 
	defb 012h		;156f	12		.
	defb 060h		;1570	60		`
	defb 0c5h		;1571	c5		.
	defb 0eah		;1572	ea		.
	defb 009h		;1573	09		.
	defb 0c6h		;1574	c6		.
	defb 013h		;1575	13		.
	defb 021h		;1576	21		!
	defb 0f0h		;1577	f0		.
	defb 015h		;1578	15		.
	defb 009h		;1579	09		.
	defb 0c6h		;157a	c6		.
	defb 013h		;157b	13		.
	defb 021h		;157c	21		!
	defb 0f0h		;157d	f0		.
	defb 015h		;157e	15		.
	defb 001h		;157f	01		.
	defb 010h		;1580	10		.
	defb 060h		;1581	60		`
	defb 0c4h		;1582	c4		.
	defb 0e8h		;1583	e8		.
	defb 001h		;1584	01		.
	defb 010h		;1585	10		.
	defb 060h		;1586	60		`
	defb 0c4h		;1587	c4		.
	defb 0e8h		;1588	e8		.
	defb 009h		;1589	09		.
	defb 0c6h		;158a	c6		.
	defb 011h		;158b	11		.
	defb 020h		;158c	20		 
	defb 0e9h		;158d	e9		.
	defb 009h		;158e	09		.
	defb 0c6h		;158f	c6		.
	defb 011h		;1590	11		.
	defb 024h		;1591	24		$
	defb 0e9h		;1592	e9		.
	defb 01ch		;1593	1c		.
	defb 081h		;1594	81		.
	defb 020h		;1595	20		 
	defb 012h		;1596	12		.
	defb 060h		;1597	60		`
	defb 0c5h		;1598	c5		.
	defb 0eah		;1599	ea		.
	defb 081h		;159a	81		.
	defb 020h		;159b	20		 
	defb 012h		;159c	12		.
	defb 061h		;159d	61		a
	defb 0c5h		;159e	c5		.
	defb 0eah		;159f	ea		.
	defb 058h		;15a0	58		X
	defb 009h		;15a1	09		.
	defb 0c6h		;15a2	c6		.
	defb 013h		;15a3	13		.
	defb 021h		;15a4	21		!
	defb 0f0h		;15a5	f0		.
	defb 015h		;15a6	15		.
	defb 009h		;15a7	09		.
	defb 0c6h		;15a8	c6		.
	defb 013h		;15a9	13		.
	defb 025h		;15aa	25		%
	defb 0f0h		;15ab	f0		.
	defb 052h		;15ac	52		R
	defb 015h		;15ad	15		.
	defb 001h		;15ae	01		.
	defb 088h		;15af	88		.
	defb 060h		;15b0	60		`
	defb 0c4h		;15b1	c4		.
	defb 0e8h		;15b2	e8		.
	defb 001h		;15b3	01		.
	defb 088h		;15b4	88		.
	defb 060h		;15b5	60		`
	defb 0c4h		;15b6	c4		.
	defb 0e8h		;15b7	e8		.
	defb 009h		;15b8	09		.
	defb 0c6h		;15b9	c6		.
	defb 088h		;15ba	88		.
	defb 039h		;15bb	39		9
	defb 0e9h		;15bc	e9		.
	defb 032h		;15bd	32		2
	defb 02ah		;15be	2a		*
	defb 032h		;15bf	32		2
	defb 009h		;15c0	09		.
	defb 0c6h		;15c1	c6		.
	defb 088h		;15c2	88		.
	defb 039h		;15c3	39		9
	defb 0e9h		;15c4	e9		.
	defb 032h		;15c5	32		2
	defb 02bh		;15c6	2b		+
	defb 032h		;15c7	32		2
	defb 0ffh		;15c8	ff		.
	defb 020h		;15c9	20		 
	defb 021h		;15ca	21		!
	defb 022h		;15cb	22		"
	defb 023h		;15cc	23		#
	defb 024h		;15cd	24		$
	defb 025h		;15ce	25		%
	defb 089h		;15cf	89		.
	defb 088h		;15d0	88		.
	defb 060h		;15d1	60		`
	defb 0c5h		;15d2	c5		.
	defb 0eah		;15d3	ea		.
	defb 0ffh		;15d4	ff		.
	defb 020h		;15d5	20		 
	defb 021h		;15d6	21		!
	defb 022h		;15d7	22		"
	defb 023h		;15d8	23		#
	defb 024h		;15d9	24		$
	defb 025h		;15da	25		%
	defb 0c3h		;15db	c3		.
	defb 088h		;15dc	88		.
	defb 060h		;15dd	60		`
	defb 0c5h		;15de	c5		.
	defb 0eah		;15df	ea		.
	defb 009h		;15e0	09		.
	defb 0c6h		;15e1	c6		.
	defb 088h		;15e2	88		.
	defb 03eh		;15e3	3e		>
	defb 0f0h		;15e4	f0		.
	defb 088h		;15e5	88		.
	defb 0c7h		;15e6	c7		.
	defb 0c0h		;15e7	c0		.
	defb 0c2h		;15e8	c2		.
	defb 009h		;15e9	09		.
	defb 0c6h		;15ea	c6		.
	defb 088h		;15eb	88		.
	defb 03ah		;15ec	3a		:
	defb 0f0h		;15ed	f0		.
	defb 088h		;15ee	88		.
	defb 0c7h		;15ef	c7		.
	defb 0c2h		;15f0	c2		.
	defb 001h		;15f1	01		.
	defb 088h		;15f2	88		.
	defb 060h		;15f3	60		`
	defb 0c4h		;15f4	c4		.
	defb 0e8h		;15f5	e8		.
	defb 001h		;15f6	01		.
	defb 088h		;15f7	88		.
	defb 060h		;15f8	60		`
	defb 0c4h		;15f9	c4		.
	defb 0e8h		;15fa	e8		.
	defb 009h		;15fb	09		.
	defb 0c6h		;15fc	c6		.
	defb 088h		;15fd	88		.
	defb 03dh		;15fe	3d		=
	defb 0e9h		;15ff	e9		.
	defb 032h		;1600	32		2
	defb 02eh		;1601	2e		.
	defb 02ch		;1602	2c		,
	defb 032h		;1603	32		2
	defb 009h		;1604	09		.
	defb 0c6h		;1605	c6		.
	defb 088h		;1606	88		.
	defb 03dh		;1607	3d		=
	defb 0e9h		;1608	e9		.
	defb 032h		;1609	32		2
	defb 02fh		;160a	2f		/
	defb 02dh		;160b	2d		-
	defb 032h		;160c	32		2
	defb 0bdh		;160d	bd		.
	defb 020h		;160e	20		 
	defb 026h		;160f	26		&
	defb 027h		;1610	27		'
	defb 028h		;1611	28		(
	defb 029h		;1612	29		)
	defb 088h		;1613	88		.
	defb 060h		;1614	60		`
	defb 0c5h		;1615	c5		.
	defb 0eah		;1616	ea		.
	defb 0bdh		;1617	bd		.
	defb 020h		;1618	20		 
	defb 026h		;1619	26		&
	defb 027h		;161a	27		'
	defb 028h		;161b	28		(
	defb 029h		;161c	29		)
	defb 088h		;161d	88		.
	defb 060h		;161e	60		`
	defb 0c5h		;161f	c5		.
	defb 0eah		;1620	ea		.
	defb 009h		;1621	09		.
	defb 0c6h		;1622	c6		.
	defb 088h		;1623	88		.
	defb 03eh		;1624	3e		>
	defb 0f0h		;1625	f0		.
	defb 088h		;1626	88		.
	defb 0c7h		;1627	c7		.
	defb 0c1h		;1628	c1		.
	defb 0c2h		;1629	c2		.
	defb 009h		;162a	09		.
	defb 0c6h		;162b	c6		.
	defb 088h		;162c	88		.
	defb 03eh		;162d	3e		>
	defb 0f0h		;162e	f0		.
	defb 088h		;162f	88		.
	defb 0c7h		;1630	c7		.
	defb 0c1h		;1631	c1		.
	defb 0c2h		;1632	c2		.
	defb 001h		;1633	01		.
	defb 088h		;1634	88		.
	defb 060h		;1635	60		`
	defb 0c4h		;1636	c4		.
	defb 0e8h		;1637	e8		.
	defb 001h		;1638	01		.
	defb 088h		;1639	88		.
	defb 060h		;163a	60		`
	defb 0c4h		;163b	c4		.
	defb 0e8h		;163c	e8		.
	defb 009h		;163d	09		.
	defb 0c6h		;163e	c6		.
	defb 088h		;163f	88		.
	defb 035h		;1640	35		5
	defb 0e9h		;1641	e9		.
	defb 032h		;1642	32		2
	defb 01bh		;1643	1b		.
	defb 032h		;1644	32		2
	defb 009h		;1645	09		.
	defb 0c6h		;1646	c6		.
	defb 088h		;1647	88		.
	defb 035h		;1648	35		5
	defb 0e9h		;1649	e9		.
	defb 032h		;164a	32		2
	defb 01eh		;164b	1e		.
	defb 032h		;164c	32		2
	defb 081h		;164d	81		.
	defb 020h		;164e	20		 
	defb 088h		;164f	88		.
	defb 060h		;1650	60		`
	defb 0c5h		;1651	c5		.
	defb 0eah		;1652	ea		.
	defb 081h		;1653	81		.
	defb 020h		;1654	20		 
	defb 088h		;1655	88		.
	defb 060h		;1656	60		`
	defb 0c5h		;1657	c5		.
	defb 0eah		;1658	ea		.
	defb 009h		;1659	09		.
	defb 0c6h		;165a	c6		.
	defb 088h		;165b	88		.
	defb 03ah		;165c	3a		:
	defb 0f0h		;165d	f0		.
	defb 088h		;165e	88		.
	defb 0c7h		;165f	c7		.
	defb 0c2h		;1660	c2		.
	defb 009h		;1661	09		.
	defb 0c6h		;1662	c6		.
	defb 088h		;1663	88		.
	defb 03eh		;1664	3e		>
	defb 0f0h		;1665	f0		.
	defb 088h		;1666	88		.
	defb 0c7h		;1667	c7		.
	defb 091h		;1668	91		.
	defb 0c2h		;1669	c2		.
	defb 001h		;166a	01		.
	defb 088h		;166b	88		.
	defb 060h		;166c	60		`
	defb 0c4h		;166d	c4		.
	defb 0e8h		;166e	e8		.
	defb 001h		;166f	01		.
	defb 088h		;1670	88		.
	defb 060h		;1671	60		`
	defb 0c4h		;1672	c4		.
	defb 0e8h		;1673	e8		.
	defb 009h		;1674	09		.
	defb 0c6h		;1675	c6		.
	defb 088h		;1676	88		.
	defb 035h		;1677	35		5
	defb 0e9h		;1678	e9		.
	defb 032h		;1679	32		2
	defb 01dh		;167a	1d		.
	defb 032h		;167b	32		2
	defb 009h		;167c	09		.
	defb 0c6h		;167d	c6		.
	defb 088h		;167e	88		.
	defb 035h		;167f	35		5
	defb 0e9h		;1680	e9		.
	defb 032h		;1681	32		2
	defb 01eh		;1682	1e		.
	defb 032h		;1683	32		2
	defb 081h		;1684	81		.
	defb 020h		;1685	20		 
	defb 088h		;1686	88		.
	defb 060h		;1687	60		`
	defb 0c5h		;1688	c5		.
	defb 0eah		;1689	ea		.
	defb 081h		;168a	81		.
	defb 020h		;168b	20		 
	defb 088h		;168c	88		.
	defb 060h		;168d	60		`
	defb 0c5h		;168e	c5		.
	defb 0eah		;168f	ea		.
	defb 009h		;1690	09		.
	defb 0c6h		;1691	c6		.
	defb 088h		;1692	88		.
	defb 03ah		;1693	3a		:
	defb 0f0h		;1694	f0		.
	defb 088h		;1695	88		.
	defb 0c7h		;1696	c7		.
	defb 0c2h		;1697	c2		.
	defb 009h		;1698	09		.
	defb 0c6h		;1699	c6		.
	defb 088h		;169a	88		.
	defb 03eh		;169b	3e		>
	defb 0f0h		;169c	f0		.
	defb 088h		;169d	88		.
	defb 0c7h		;169e	c7		.
	defb 090h		;169f	90		.
	defb 0c2h		;16a0	c2		.
	defb 001h		;16a1	01		.
	defb 015h		;16a2	15		.
	defb 0e0h		;16a3	e0		.
	defb 088h		;16a4	88		.
	defb 0c4h		;16a5	c4		.
	defb 0e8h		;16a6	e8		.
	defb 001h		;16a7	01		.
	defb 015h		;16a8	15		.
	defb 0e0h		;16a9	e0		.
	defb 088h		;16aa	88		.
	defb 0c4h		;16ab	c4		.
	defb 0e8h		;16ac	e8		.
	defb 008h		;16ad	08		.
	defb 0c6h		;16ae	c6		.
	defb 031h		;16af	31		1
	defb 0e9h		;16b0	e9		.
	defb 032h		;16b1	32		2
	defb 032h		;16b2	32		2
	defb 008h		;16b3	08		.
	defb 0c6h		;16b4	c6		.
	defb 031h		;16b5	31		1
	defb 0e9h		;16b6	e9		.
	defb 032h		;16b7	32		2
	defb 032h		;16b8	32		2
	defb 0fch		;16b9	fc		.
	defb 020h		;16ba	20		 
	defb 021h		;16bb	21		!
	defb 022h		;16bc	22		"
	defb 023h		;16bd	23		#
	defb 024h		;16be	24		$
	defb 025h		;16bf	25		%
	defb 060h		;16c0	60		`
	defb 0c5h		;16c1	c5		.
	defb 0eah		;16c2	ea		.
	defb 0fch		;16c3	fc		.
	defb 020h		;16c4	20		 
	defb 021h		;16c5	21		!
	defb 022h		;16c6	22		"
	defb 023h		;16c7	23		#
	defb 024h		;16c8	24		$
	defb 025h		;16c9	25		%
	defb 060h		;16ca	60		`
	defb 0c5h		;16cb	c5		.
	defb 0eah		;16cc	ea		.
	defb 018h		;16cd	18		.
	defb 088h		;16ce	88		.
	defb 0c6h		;16cf	c6		.
	defb 036h		;16d0	36		6
	defb 0f0h		;16d1	f0		.
	defb 088h		;16d2	88		.
	defb 0c0h		;16d3	c0		.
	defb 040h		;16d4	40		@
	defb 018h		;16d5	18		.
	defb 088h		;16d6	88		.
	defb 0c6h		;16d7	c6		.
	defb 032h		;16d8	32		2
	defb 0f0h		;16d9	f0		.
	defb 088h		;16da	88		.
	defb 040h		;16db	40		@
	defb 001h		;16dc	01		.
	defb 015h		;16dd	15		.
	defb 0e0h		;16de	e0		.
	defb 088h		;16df	88		.
	defb 0c4h		;16e0	c4		.
	defb 0e8h		;16e1	e8		.
	defb 001h		;16e2	01		.
	defb 015h		;16e3	15		.
	defb 0e0h		;16e4	e0		.
	defb 088h		;16e5	88		.
	defb 0c4h		;16e6	c4		.
	defb 0e8h		;16e7	e8		.
	defb 008h		;16e8	08		.
	defb 0c6h		;16e9	c6		.
	defb 031h		;16ea	31		1
	defb 0e9h		;16eb	e9		.
	defb 032h		;16ec	32		2
	defb 032h		;16ed	32		2
	defb 008h		;16ee	08		.
	defb 0c6h		;16ef	c6		.
	defb 031h		;16f0	31		1
	defb 0e9h		;16f1	e9		.
	defb 032h		;16f2	32		2
	defb 032h		;16f3	32		2
	defb 0bch		;16f4	bc		.
	defb 020h		;16f5	20		 
	defb 026h		;16f6	26		&
	defb 027h		;16f7	27		'
	defb 028h		;16f8	28		(
	defb 029h		;16f9	29		)
	defb 060h		;16fa	60		`
	defb 0c5h		;16fb	c5		.
	defb 0eah		;16fc	ea		.
	defb 0bch		;16fd	bc		.
	defb 020h		;16fe	20		 
	defb 026h		;16ff	26		&
	defb 027h		;1700	27		'
	defb 028h		;1701	28		(
	defb 029h		;1702	29		)
	defb 060h		;1703	60		`
	defb 0c5h		;1704	c5		.
	defb 0eah		;1705	ea		.
	defb 018h		;1706	18		.
	defb 088h		;1707	88		.
	defb 0c6h		;1708	c6		.
	defb 036h		;1709	36		6
	defb 0f0h		;170a	f0		.
	defb 088h		;170b	88		.
	defb 0c1h		;170c	c1		.
	defb 040h		;170d	40		@
	defb 018h		;170e	18		.
	defb 088h		;170f	88		.
	defb 0c6h		;1710	c6		.
	defb 036h		;1711	36		6
	defb 0f0h		;1712	f0		.
	defb 088h		;1713	88		.
	defb 0c1h		;1714	c1		.
	defb 040h		;1715	40		@
	defb 001h		;1716	01		.
	defb 015h		;1717	15		.
	defb 0e0h		;1718	e0		.
	defb 088h		;1719	88		.
	defb 0c4h		;171a	c4		.
	defb 0e8h		;171b	e8		.
	defb 001h		;171c	01		.
	defb 015h		;171d	15		.
	defb 0e0h		;171e	e0		.
	defb 088h		;171f	88		.
	defb 0c4h		;1720	c4		.
	defb 0e8h		;1721	e8		.
	defb 008h		;1722	08		.
	defb 0c6h		;1723	c6		.
	defb 031h		;1724	31		1
	defb 0e9h		;1725	e9		.
	defb 032h		;1726	32		2
	defb 032h		;1727	32		2
	defb 008h		;1728	08		.
	defb 0c6h		;1729	c6		.
	defb 035h		;172a	35		5
	defb 0e9h		;172b	e9		.
	defb 032h		;172c	32		2
	defb 01ch		;172d	1c		.
	defb 032h		;172e	32		2
	defb 080h		;172f	80		.
	defb 020h		;1730	20		 
	defb 060h		;1731	60		`
	defb 0c5h		;1732	c5		.
	defb 0eah		;1733	ea		.
	defb 080h		;1734	80		.
	defb 020h		;1735	20		 
	defb 060h		;1736	60		`
	defb 0c5h		;1737	c5		.
	defb 0eah		;1738	ea		.
	defb 018h		;1739	18		.
	defb 088h		;173a	88		.
	defb 0c6h		;173b	c6		.
	defb 032h		;173c	32		2
	defb 0f0h		;173d	f0		.
	defb 088h		;173e	88		.
	defb 040h		;173f	40		@
	defb 018h		;1740	18		.
	defb 088h		;1741	88		.
	defb 0c6h		;1742	c6		.
	defb 032h		;1743	32		2
	defb 0f0h		;1744	f0		.
	defb 088h		;1745	88		.
	defb 040h		;1746	40		@
	defb 001h		;1747	01		.
	defb 015h		;1748	15		.
	defb 0e0h		;1749	e0		.
	defb 088h		;174a	88		.
	defb 0c4h		;174b	c4		.
	defb 0e8h		;174c	e8		.
	defb 001h		;174d	01		.
	defb 015h		;174e	15		.
	defb 0e0h		;174f	e0		.
	defb 088h		;1750	88		.
	defb 0c4h		;1751	c4		.
	defb 0e8h		;1752	e8		.
	defb 008h		;1753	08		.
	defb 0c6h		;1754	c6		.
	defb 031h		;1755	31		1
	defb 0e9h		;1756	e9		.
	defb 032h		;1757	32		2
	defb 032h		;1758	32		2
	defb 008h		;1759	08		.
	defb 0c6h		;175a	c6		.
	defb 035h		;175b	35		5
	defb 0e9h		;175c	e9		.
	defb 032h		;175d	32		2
	defb 01ch		;175e	1c		.
	defb 032h		;175f	32		2
	defb 080h		;1760	80		.
	defb 020h		;1761	20		 
	defb 060h		;1762	60		`
	defb 0c5h		;1763	c5		.
	defb 0eah		;1764	ea		.
	defb 080h		;1765	80		.
	defb 020h		;1766	20		 
	defb 061h		;1767	61		a
	defb 0c5h		;1768	c5		.
	defb 0eah		;1769	ea		.
	defb 058h		;176a	58		X
	defb 018h		;176b	18		.
	defb 088h		;176c	88		.
	defb 0c6h		;176d	c6		.
	defb 032h		;176e	32		2
	defb 0f0h		;176f	f0		.
	defb 088h		;1770	88		.
	defb 040h		;1771	40		@
	defb 018h		;1772	18		.
	defb 088h		;1773	88		.
	defb 0c6h		;1774	c6		.
	defb 036h		;1775	36		6
	defb 0f0h		;1776	f0		.
	defb 088h		;1777	88		.
	defb 052h		;1778	52		R
	defb 040h		;1779	40		@
	defb 001h		;177a	01		.
	defb 010h		;177b	10		.
	defb 060h		;177c	60		`
	defb 0c4h		;177d	c4		.
	defb 0e8h		;177e	e8		.
	defb 001h		;177f	01		.
	defb 010h		;1780	10		.
	defb 060h		;1781	60		`
	defb 0c4h		;1782	c4		.
	defb 0e8h		;1783	e8		.
	defb 009h		;1784	09		.
	defb 0c6h		;1785	c6		.
	defb 011h		;1786	11		.
	defb 02ch		;1787	2c		,
	defb 0e9h		;1788	e9		.
	defb 02ah		;1789	2a		*
	defb 016h		;178a	16		.
	defb 009h		;178b	09		.
	defb 0c6h		;178c	c6		.
	defb 011h		;178d	11		.
	defb 02ch		;178e	2c		,
	defb 0e9h		;178f	e9		.
	defb 02bh		;1790	2b		+
	defb 017h		;1791	17		.
	defb 0ffh		;1792	ff		.
	defb 020h		;1793	20		 
	defb 021h		;1794	21		!
	defb 022h		;1795	22		"
	defb 023h		;1796	23		#
	defb 024h		;1797	24		$
	defb 025h		;1798	25		%
	defb 089h		;1799	89		.
	defb 012h		;179a	12		.
	defb 060h		;179b	60		`
	defb 0c5h		;179c	c5		.
	defb 0eah		;179d	ea		.
	defb 0ffh		;179e	ff		.
	defb 020h		;179f	20		 
	defb 021h		;17a0	21		!
	defb 022h		;17a1	22		"
	defb 023h		;17a2	23		#
	defb 024h		;17a3	24		$
	defb 025h		;17a4	25		%
	defb 0c3h		;17a5	c3		.
	defb 012h		;17a6	12		.
	defb 060h		;17a7	60		`
	defb 0c5h		;17a8	c5		.
	defb 0eah		;17a9	ea		.
	defb 009h		;17aa	09		.
	defb 0c6h		;17ab	c6		.
	defb 013h		;17ac	13		.
	defb 02fh		;17ad	2f		/
	defb 0f0h		;17ae	f0		.
	defb 0c7h		;17af	c7		.
	defb 0c0h		;17b0	c0		.
	defb 0c2h		;17b1	c2		.
	defb 014h		;17b2	14		.
	defb 009h		;17b3	09		.
	defb 0c6h		;17b4	c6		.
	defb 013h		;17b5	13		.
	defb 02bh		;17b6	2b		+
	defb 0f0h		;17b7	f0		.
	defb 0c7h		;17b8	c7		.
	defb 0c2h		;17b9	c2		.
	defb 014h		;17ba	14		.
	defb 001h		;17bb	01		.
	defb 010h		;17bc	10		.
	defb 060h		;17bd	60		`
	defb 0c4h		;17be	c4		.
	defb 0e8h		;17bf	e8		.
	defb 001h		;17c0	01		.
	defb 010h		;17c1	10		.
	defb 060h		;17c2	60		`
	defb 0c4h		;17c3	c4		.
	defb 0e8h		;17c4	e8		.
	defb 009h		;17c5	09		.
	defb 0c6h		;17c6	c6		.
	defb 011h		;17c7	11		.
	defb 02ch		;17c8	2c		,
	defb 0e9h		;17c9	e9		.
	defb 02eh		;17ca	2e		.
	defb 02ch		;17cb	2c		,
	defb 009h		;17cc	09		.
	defb 0c6h		;17cd	c6		.
	defb 011h		;17ce	11		.
	defb 02ch		;17cf	2c		,
	defb 0e9h		;17d0	e9		.
	defb 02fh		;17d1	2f		/
	defb 02dh		;17d2	2d		-
	defb 0bdh		;17d3	bd		.
	defb 020h		;17d4	20		 
	defb 026h		;17d5	26		&
	defb 027h		;17d6	27		'
	defb 028h		;17d7	28		(
	defb 029h		;17d8	29		)
	defb 012h		;17d9	12		.
	defb 060h		;17da	60		`
	defb 0c5h		;17db	c5		.
	defb 0eah		;17dc	ea		.
	defb 0bdh		;17dd	bd		.
	defb 020h		;17de	20		 
	defb 026h		;17df	26		&
	defb 027h		;17e0	27		'
	defb 028h		;17e1	28		(
	defb 029h		;17e2	29		)
	defb 012h		;17e3	12		.
	defb 060h		;17e4	60		`
	defb 0c5h		;17e5	c5		.
	defb 0eah		;17e6	ea		.
	defb 009h		;17e7	09		.
	defb 0c6h		;17e8	c6		.
	defb 013h		;17e9	13		.
	defb 02fh		;17ea	2f		/
	defb 0f0h		;17eb	f0		.
	defb 0c7h		;17ec	c7		.
	defb 0c1h		;17ed	c1		.
	defb 0c2h		;17ee	c2		.
	defb 014h		;17ef	14		.
	defb 009h		;17f0	09		.
	defb 0c6h		;17f1	c6		.
	defb 013h		;17f2	13		.
	defb 02fh		;17f3	2f		/
	defb 0f0h		;17f4	f0		.
	defb 0c7h		;17f5	c7		.
	defb 0c1h		;17f6	c1		.
	defb 0c2h		;17f7	c2		.
	defb 014h		;17f8	14		.
	defb 001h		;17f9	01		.
	defb 010h		;17fa	10		.
	defb 060h		;17fb	60		`
	defb 0c4h		;17fc	c4		.
	defb 0e8h		;17fd	e8		.
	defb 001h		;17fe	01		.
	defb 010h		;17ff	10		.
	defb 060h		;1800	60		`
	defb 0c4h		;1801	c4		.
	defb 0e8h		;1802	e8		.
	defb 009h		;1803	09		.
	defb 0c6h		;1804	c6		.
	defb 011h		;1805	11		.
	defb 024h		;1806	24		$
	defb 0e9h		;1807	e9		.
	defb 01bh		;1808	1b		.
	defb 009h		;1809	09		.
	defb 0c6h		;180a	c6		.
	defb 011h		;180b	11		.
	defb 024h		;180c	24		$
	defb 0e9h		;180d	e9		.
	defb 01eh		;180e	1e		.
	defb 081h		;180f	81		.
	defb 020h		;1810	20		 
	defb 012h		;1811	12		.
	defb 060h		;1812	60		`
	defb 0c5h		;1813	c5		.
	defb 0eah		;1814	ea		.
	defb 081h		;1815	81		.
	defb 020h		;1816	20		 
	defb 012h		;1817	12		.
	defb 060h		;1818	60		`
	defb 0c5h		;1819	c5		.
	defb 0eah		;181a	ea		.
	defb 009h		;181b	09		.
	defb 0c6h		;181c	c6		.
	defb 013h		;181d	13		.
	defb 02bh		;181e	2b		+
	defb 0f0h		;181f	f0		.
	defb 0c7h		;1820	c7		.
	defb 0c2h		;1821	c2		.
	defb 014h		;1822	14		.
	defb 009h		;1823	09		.
	defb 0c6h		;1824	c6		.
	defb 013h		;1825	13		.
	defb 02fh		;1826	2f		/
	defb 0f0h		;1827	f0		.
	defb 0c7h		;1828	c7		.
	defb 091h		;1829	91		.
	defb 0c2h		;182a	c2		.
	defb 014h		;182b	14		.
	defb 001h		;182c	01		.
	defb 010h		;182d	10		.
	defb 060h		;182e	60		`
	defb 0c4h		;182f	c4		.
	defb 0e8h		;1830	e8		.
	defb 001h		;1831	01		.
	defb 010h		;1832	10		.
	defb 060h		;1833	60		`
	defb 0c4h		;1834	c4		.
	defb 0e8h		;1835	e8		.
	defb 009h		;1836	09		.
	defb 0c6h		;1837	c6		.
	defb 011h		;1838	11		.
	defb 024h		;1839	24		$
	defb 0e9h		;183a	e9		.
	defb 01dh		;183b	1d		.
	defb 009h		;183c	09		.
	defb 0c6h		;183d	c6		.
	defb 011h		;183e	11		.
	defb 024h		;183f	24		$
	defb 0e9h		;1840	e9		.
	defb 01eh		;1841	1e		.
	defb 081h		;1842	81		.
	defb 020h		;1843	20		 
	defb 012h		;1844	12		.
	defb 060h		;1845	60		`
	defb 0c5h		;1846	c5		.
	defb 0eah		;1847	ea		.
	defb 081h		;1848	81		.
	defb 020h		;1849	20		 
	defb 012h		;184a	12		.
	defb 060h		;184b	60		`
	defb 0c5h		;184c	c5		.
	defb 0eah		;184d	ea		.
	defb 009h		;184e	09		.
	defb 0c6h		;184f	c6		.
	defb 013h		;1850	13		.
	defb 02bh		;1851	2b		+
	defb 0f0h		;1852	f0		.
	defb 0c7h		;1853	c7		.
	defb 0c2h		;1854	c2		.
	defb 014h		;1855	14		.
	defb 009h		;1856	09		.
	defb 0c6h		;1857	c6		.
	defb 013h		;1858	13		.
	defb 02fh		;1859	2f		/
	defb 0f0h		;185a	f0		.
	defb 0c7h		;185b	c7		.
	defb 090h		;185c	90		.
	defb 0c2h		;185d	c2		.
	defb 014h		;185e	14		.
	defb 001h		;185f	01		.
	defb 010h		;1860	10		.
	defb 060h		;1861	60		`
	defb 0c4h		;1862	c4		.
	defb 0e8h		;1863	e8		.
	defb 001h		;1864	01		.
	defb 010h		;1865	10		.
	defb 060h		;1866	60		`
	defb 0c4h		;1867	c4		.
	defb 0e8h		;1868	e8		.
	defb 009h		;1869	09		.
	defb 0c6h		;186a	c6		.
	defb 011h		;186b	11		.
	defb 020h		;186c	20		 
	defb 0e9h		;186d	e9		.
	defb 009h		;186e	09		.
	defb 0c6h		;186f	c6		.
	defb 011h		;1870	11		.
	defb 020h		;1871	20		 
	defb 0e9h		;1872	e9		.
	defb 0fdh		;1873	fd		.
	defb 020h		;1874	20		 
	defb 021h		;1875	21		!
	defb 022h		;1876	22		"
	defb 023h		;1877	23		#
	defb 024h		;1878	24		$
	defb 025h		;1879	25		%
	defb 012h		;187a	12		.
	defb 060h		;187b	60		`
	defb 0c5h		;187c	c5		.
	defb 0eah		;187d	ea		.
	defb 0fdh		;187e	fd		.
	defb 020h		;187f	20		 
	defb 021h		;1880	21		!
	defb 022h		;1881	22		"
	defb 023h		;1882	23		#
	defb 024h		;1883	24		$
	defb 025h		;1884	25		%
	defb 012h		;1885	12		.
	defb 060h		;1886	60		`
	defb 0c5h		;1887	c5		.
	defb 0eah		;1888	ea		.
	defb 009h		;1889	09		.
	defb 0c6h		;188a	c6		.
	defb 013h		;188b	13		.
	defb 025h		;188c	25		%
	defb 0f0h		;188d	f0		.
	defb 0c0h		;188e	c0		.
	defb 015h		;188f	15		.
	defb 009h		;1890	09		.
	defb 0c6h		;1891	c6		.
	defb 013h		;1892	13		.
	defb 021h		;1893	21		!
	defb 0f0h		;1894	f0		.
	defb 015h		;1895	15		.
	defb 001h		;1896	01		.
	defb 010h		;1897	10		.
	defb 060h		;1898	60		`
	defb 0c4h		;1899	c4		.
	defb 0e8h		;189a	e8		.
	defb 001h		;189b	01		.
	defb 010h		;189c	10		.
	defb 060h		;189d	60		`
	defb 0c4h		;189e	c4		.
	defb 0e8h		;189f	e8		.
	defb 009h		;18a0	09		.
	defb 0c6h		;18a1	c6		.
	defb 011h		;18a2	11		.
	defb 020h		;18a3	20		 
	defb 0e9h		;18a4	e9		.
	defb 009h		;18a5	09		.
	defb 0c6h		;18a6	c6		.
	defb 011h		;18a7	11		.
	defb 020h		;18a8	20		 
	defb 0e9h		;18a9	e9		.
	defb 0bdh		;18aa	bd		.
	defb 020h		;18ab	20		 
	defb 026h		;18ac	26		&
	defb 027h		;18ad	27		'
	defb 028h		;18ae	28		(
	defb 029h		;18af	29		)
	defb 012h		;18b0	12		.
	defb 060h		;18b1	60		`
	defb 0c5h		;18b2	c5		.
	defb 0eah		;18b3	ea		.
	defb 0bdh		;18b4	bd		.
	defb 020h		;18b5	20		 
	defb 026h		;18b6	26		&
	defb 027h		;18b7	27		'
	defb 028h		;18b8	28		(
	defb 029h		;18b9	29		)
	defb 012h		;18ba	12		.
	defb 060h		;18bb	60		`
	defb 0c5h		;18bc	c5		.
	defb 0eah		;18bd	ea		.
	defb 009h		;18be	09		.
	defb 0c6h		;18bf	c6		.
	defb 013h		;18c0	13		.
	defb 025h		;18c1	25		%
	defb 0f0h		;18c2	f0		.
	defb 0c1h		;18c3	c1		.
	defb 015h		;18c4	15		.
	defb 009h		;18c5	09		.
	defb 0c6h		;18c6	c6		.
	defb 013h		;18c7	13		.
	defb 025h		;18c8	25		%
	defb 0f0h		;18c9	f0		.
	defb 0c1h		;18ca	c1		.
	defb 015h		;18cb	15		.
	defb 001h		;18cc	01		.
	defb 010h		;18cd	10		.
	defb 060h		;18ce	60		`
	defb 0c4h		;18cf	c4		.
	defb 0e8h		;18d0	e8		.
	defb 001h		;18d1	01		.
	defb 010h		;18d2	10		.
	defb 060h		;18d3	60		`
	defb 0c4h		;18d4	c4		.
	defb 0e8h		;18d5	e8		.
	defb 009h		;18d6	09		.
	defb 0c6h		;18d7	c6		.
	defb 011h		;18d8	11		.
	defb 020h		;18d9	20		 
	defb 0e9h		;18da	e9		.
	defb 009h		;18db	09		.
	defb 0c6h		;18dc	c6		.
	defb 011h		;18dd	11		.
	defb 024h		;18de	24		$
	defb 0e9h		;18df	e9		.
	defb 01ch		;18e0	1c		.
	defb 081h		;18e1	81		.
	defb 020h		;18e2	20		 
	defb 012h		;18e3	12		.
	defb 060h		;18e4	60		`
	defb 0c5h		;18e5	c5		.
	defb 0eah		;18e6	ea		.
	defb 081h		;18e7	81		.
	defb 020h		;18e8	20		 
	defb 012h		;18e9	12		.
	defb 060h		;18ea	60		`
	defb 0c5h		;18eb	c5		.
	defb 0eah		;18ec	ea		.
	defb 009h		;18ed	09		.
	defb 0c6h		;18ee	c6		.
	defb 013h		;18ef	13		.
	defb 021h		;18f0	21		!
	defb 0f0h		;18f1	f0		.
	defb 015h		;18f2	15		.
	defb 009h		;18f3	09		.
	defb 0c6h		;18f4	c6		.
	defb 013h		;18f5	13		.
	defb 021h		;18f6	21		!
	defb 0f0h		;18f7	f0		.
	defb 015h		;18f8	15		.
	defb 001h		;18f9	01		.
	defb 010h		;18fa	10		.
	defb 060h		;18fb	60		`
	defb 0c4h		;18fc	c4		.
	defb 0e8h		;18fd	e8		.
	defb 001h		;18fe	01		.
	defb 010h		;18ff	10		.
	defb 060h		;1900	60		`
	defb 0c4h		;1901	c4		.
	defb 0e8h		;1902	e8		.
	defb 009h		;1903	09		.
	defb 0c6h		;1904	c6		.
	defb 011h		;1905	11		.
	defb 020h		;1906	20		 
	defb 0e9h		;1907	e9		.
	defb 009h		;1908	09		.
	defb 0c6h		;1909	c6		.
	defb 011h		;190a	11		.
	defb 024h		;190b	24		$
	defb 0e9h		;190c	e9		.
	defb 01ch		;190d	1c		.
	defb 081h		;190e	81		.
	defb 020h		;190f	20		 
	defb 012h		;1910	12		.
	defb 060h		;1911	60		`
	defb 0c5h		;1912	c5		.
	defb 0eah		;1913	ea		.
	defb 081h		;1914	81		.
	defb 020h		;1915	20		 
	defb 012h		;1916	12		.
	defb 061h		;1917	61		a
	defb 0c5h		;1918	c5		.
	defb 0eah		;1919	ea		.
	defb 058h		;191a	58		X
	defb 009h		;191b	09		.
	defb 0c6h		;191c	c6		.
	defb 013h		;191d	13		.
	defb 021h		;191e	21		!
	defb 0f0h		;191f	f0		.
	defb 015h		;1920	15		.
	defb 009h		;1921	09		.
	defb 0c6h		;1922	c6		.
	defb 013h		;1923	13		.
	defb 025h		;1924	25		%
	defb 0f0h		;1925	f0		.
	defb 052h		;1926	52		R
	defb 015h		;1927	15		.
	defb 001h		;1928	01		.
	defb 088h		;1929	88		.
	defb 060h		;192a	60		`
	defb 0c4h		;192b	c4		.
	defb 0e8h		;192c	e8		.
	defb 001h		;192d	01		.
	defb 088h		;192e	88		.
	defb 060h		;192f	60		`
	defb 0c4h		;1930	c4		.
	defb 0e8h		;1931	e8		.
	defb 009h		;1932	09		.
	defb 0c6h		;1933	c6		.
	defb 088h		;1934	88		.
	defb 039h		;1935	39		9
	defb 0e9h		;1936	e9		.
	defb 032h		;1937	32		2
	defb 02ah		;1938	2a		*
	defb 032h		;1939	32		2
	defb 009h		;193a	09		.
	defb 0c6h		;193b	c6		.
	defb 088h		;193c	88		.
	defb 039h		;193d	39		9
	defb 0e9h		;193e	e9		.
	defb 032h		;193f	32		2
	defb 02bh		;1940	2b		+
	defb 032h		;1941	32		2
	defb 0ffh		;1942	ff		.
	defb 020h		;1943	20		 
	defb 021h		;1944	21		!
	defb 022h		;1945	22		"
	defb 023h		;1946	23		#
	defb 024h		;1947	24		$
	defb 025h		;1948	25		%
	defb 089h		;1949	89		.
	defb 088h		;194a	88		.
	defb 060h		;194b	60		`
	defb 0c5h		;194c	c5		.
	defb 0eah		;194d	ea		.
	defb 0ffh		;194e	ff		.
	defb 020h		;194f	20		 
	defb 021h		;1950	21		!
	defb 022h		;1951	22		"
	defb 023h		;1952	23		#
	defb 024h		;1953	24		$
	defb 025h		;1954	25		%
	defb 0c3h		;1955	c3		.
	defb 088h		;1956	88		.
	defb 060h		;1957	60		`
	defb 0c5h		;1958	c5		.
	defb 0eah		;1959	ea		.
	defb 009h		;195a	09		.
	defb 0c6h		;195b	c6		.
	defb 088h		;195c	88		.
	defb 03eh		;195d	3e		>
	defb 0f0h		;195e	f0		.
	defb 088h		;195f	88		.
	defb 0c7h		;1960	c7		.
	defb 0c0h		;1961	c0		.
	defb 0c2h		;1962	c2		.
	defb 009h		;1963	09		.
	defb 0c6h		;1964	c6		.
	defb 088h		;1965	88		.
	defb 03ah		;1966	3a		:
	defb 0f0h		;1967	f0		.
	defb 088h		;1968	88		.
	defb 0c7h		;1969	c7		.
	defb 0c2h		;196a	c2		.
	defb 001h		;196b	01		.
	defb 088h		;196c	88		.
	defb 060h		;196d	60		`
	defb 0c4h		;196e	c4		.
	defb 0e8h		;196f	e8		.
	defb 001h		;1970	01		.
	defb 088h		;1971	88		.
	defb 060h		;1972	60		`
	defb 0c4h		;1973	c4		.
	defb 0e8h		;1974	e8		.
	defb 009h		;1975	09		.
	defb 0c6h		;1976	c6		.
	defb 088h		;1977	88		.
	defb 03dh		;1978	3d		=
	defb 0e9h		;1979	e9		.
	defb 032h		;197a	32		2
	defb 02eh		;197b	2e		.
	defb 02ch		;197c	2c		,
	defb 032h		;197d	32		2
	defb 009h		;197e	09		.
	defb 0c6h		;197f	c6		.
	defb 088h		;1980	88		.
	defb 03dh		;1981	3d		=
	defb 0e9h		;1982	e9		.
	defb 032h		;1983	32		2
	defb 02fh		;1984	2f		/
	defb 02dh		;1985	2d		-
	defb 032h		;1986	32		2
	defb 0bdh		;1987	bd		.
	defb 020h		;1988	20		 
	defb 026h		;1989	26		&
	defb 027h		;198a	27		'
	defb 028h		;198b	28		(
	defb 029h		;198c	29		)
	defb 088h		;198d	88		.
	defb 060h		;198e	60		`
	defb 0c5h		;198f	c5		.
	defb 0eah		;1990	ea		.
	defb 0bdh		;1991	bd		.
	defb 020h		;1992	20		 
	defb 026h		;1993	26		&
	defb 027h		;1994	27		'
	defb 028h		;1995	28		(
	defb 029h		;1996	29		)
	defb 088h		;1997	88		.
	defb 060h		;1998	60		`
	defb 0c5h		;1999	c5		.
	defb 0eah		;199a	ea		.
	defb 009h		;199b	09		.
	defb 0c6h		;199c	c6		.
	defb 088h		;199d	88		.
	defb 03ah		;199e	3a		:
	defb 0f0h		;199f	f0		.
	defb 088h		;19a0	88		.
	defb 0c7h		;19a1	c7		.
	defb 0c2h		;19a2	c2		.
	defb 009h		;19a3	09		.
	defb 0c6h		;19a4	c6		.
	defb 088h		;19a5	88		.
	defb 03ah		;19a6	3a		:
	defb 0f0h		;19a7	f0		.
	defb 088h		;19a8	88		.
	defb 0c7h		;19a9	c7		.
	defb 0c2h		;19aa	c2		.
	defb 001h		;19ab	01		.
	defb 088h		;19ac	88		.
	defb 060h		;19ad	60		`
	defb 0c4h		;19ae	c4		.
	defb 0e8h		;19af	e8		.
	defb 001h		;19b0	01		.
	defb 088h		;19b1	88		.
	defb 060h		;19b2	60		`
	defb 0c4h		;19b3	c4		.
	defb 0e8h		;19b4	e8		.
	defb 009h		;19b5	09		.
	defb 0c6h		;19b6	c6		.
	defb 088h		;19b7	88		.
	defb 035h		;19b8	35		5
	defb 0e9h		;19b9	e9		.
	defb 032h		;19ba	32		2
	defb 01bh		;19bb	1b		.
	defb 032h		;19bc	32		2
	defb 009h		;19bd	09		.
	defb 0c6h		;19be	c6		.
	defb 088h		;19bf	88		.
	defb 035h		;19c0	35		5
	defb 0e9h		;19c1	e9		.
	defb 032h		;19c2	32		2
	defb 01eh		;19c3	1e		.
	defb 032h		;19c4	32		2
	defb 081h		;19c5	81		.
	defb 020h		;19c6	20		 
	defb 088h		;19c7	88		.
	defb 060h		;19c8	60		`
	defb 0c5h		;19c9	c5		.
	defb 0eah		;19ca	ea		.
	defb 081h		;19cb	81		.
	defb 020h		;19cc	20		 
	defb 088h		;19cd	88		.
	defb 060h		;19ce	60		`
	defb 0c5h		;19cf	c5		.
	defb 0eah		;19d0	ea		.
	defb 009h		;19d1	09		.
	defb 0c6h		;19d2	c6		.
	defb 088h		;19d3	88		.
	defb 03ah		;19d4	3a		:
	defb 0f0h		;19d5	f0		.
	defb 088h		;19d6	88		.
	defb 0c7h		;19d7	c7		.
	defb 0c2h		;19d8	c2		.
	defb 009h		;19d9	09		.
	defb 0c6h		;19da	c6		.
	defb 088h		;19db	88		.
	defb 03eh		;19dc	3e		>
	defb 0f0h		;19dd	f0		.
	defb 088h		;19de	88		.
	defb 0c7h		;19df	c7		.
	defb 091h		;19e0	91		.
	defb 0c2h		;19e1	c2		.
	defb 001h		;19e2	01		.
	defb 088h		;19e3	88		.
	defb 060h		;19e4	60		`
	defb 0c4h		;19e5	c4		.
	defb 0e8h		;19e6	e8		.
	defb 001h		;19e7	01		.
	defb 088h		;19e8	88		.
	defb 060h		;19e9	60		`
	defb 0c4h		;19ea	c4		.
	defb 0e8h		;19eb	e8		.
	defb 009h		;19ec	09		.
	defb 0c6h		;19ed	c6		.
	defb 088h		;19ee	88		.
	defb 035h		;19ef	35		5
	defb 0e9h		;19f0	e9		.
	defb 032h		;19f1	32		2
	defb 01dh		;19f2	1d		.
	defb 032h		;19f3	32		2
	defb 009h		;19f4	09		.
	defb 0c6h		;19f5	c6		.
	defb 088h		;19f6	88		.
	defb 035h		;19f7	35		5
	defb 0e9h		;19f8	e9		.
	defb 032h		;19f9	32		2
	defb 01eh		;19fa	1e		.
	defb 032h		;19fb	32		2
	defb 081h		;19fc	81		.
	defb 020h		;19fd	20		 
	defb 088h		;19fe	88		.
	defb 060h		;19ff	60		`
	defb 0c5h		;1a00	c5		.
	defb 0eah		;1a01	ea		.
	defb 081h		;1a02	81		.
	defb 020h		;1a03	20		 
	defb 088h		;1a04	88		.
	defb 060h		;1a05	60		`
	defb 0c5h		;1a06	c5		.
	defb 0eah		;1a07	ea		.
	defb 009h		;1a08	09		.
	defb 0c6h		;1a09	c6		.
	defb 088h		;1a0a	88		.
	defb 03ah		;1a0b	3a		:
	defb 0f0h		;1a0c	f0		.
	defb 088h		;1a0d	88		.
	defb 0c7h		;1a0e	c7		.
	defb 0c2h		;1a0f	c2		.
	defb 009h		;1a10	09		.
	defb 0c6h		;1a11	c6		.
	defb 088h		;1a12	88		.
	defb 03eh		;1a13	3e		>
	defb 0f0h		;1a14	f0		.
	defb 088h		;1a15	88		.
	defb 0c7h		;1a16	c7		.
	defb 090h		;1a17	90		.
	defb 0c2h		;1a18	c2		.
	defb 001h		;1a19	01		.
	defb 015h		;1a1a	15		.
	defb 0e0h		;1a1b	e0		.
	defb 088h		;1a1c	88		.
	defb 0c4h		;1a1d	c4		.
	defb 0e8h		;1a1e	e8		.
	defb 001h		;1a1f	01		.
	defb 015h		;1a20	15		.
	defb 0e0h		;1a21	e0		.
	defb 088h		;1a22	88		.
	defb 0c4h		;1a23	c4		.
	defb 0e8h		;1a24	e8		.
	defb 008h		;1a25	08		.
	defb 0c6h		;1a26	c6		.
	defb 031h		;1a27	31		1
	defb 0e9h		;1a28	e9		.
	defb 032h		;1a29	32		2
	defb 032h		;1a2a	32		2
	defb 008h		;1a2b	08		.
	defb 0c6h		;1a2c	c6		.
	defb 031h		;1a2d	31		1
	defb 0e9h		;1a2e	e9		.
	defb 032h		;1a2f	32		2
	defb 032h		;1a30	32		2
	defb 0fch		;1a31	fc		.
	defb 020h		;1a32	20		 
	defb 021h		;1a33	21		!
	defb 022h		;1a34	22		"
	defb 023h		;1a35	23		#
	defb 024h		;1a36	24		$
	defb 025h		;1a37	25		%
	defb 060h		;1a38	60		`
	defb 0c5h		;1a39	c5		.
	defb 0eah		;1a3a	ea		.
	defb 0fch		;1a3b	fc		.
	defb 020h		;1a3c	20		 
	defb 021h		;1a3d	21		!
	defb 022h		;1a3e	22		"
	defb 023h		;1a3f	23		#
	defb 024h		;1a40	24		$
	defb 025h		;1a41	25		%
	defb 060h		;1a42	60		`
	defb 0c5h		;1a43	c5		.
	defb 0eah		;1a44	ea		.
	defb 018h		;1a45	18		.
	defb 088h		;1a46	88		.
	defb 0c6h		;1a47	c6		.
	defb 036h		;1a48	36		6
	defb 0f0h		;1a49	f0		.
	defb 088h		;1a4a	88		.
	defb 0c0h		;1a4b	c0		.
	defb 040h		;1a4c	40		@
	defb 018h		;1a4d	18		.
	defb 088h		;1a4e	88		.
	defb 0c6h		;1a4f	c6		.
	defb 032h		;1a50	32		2
	defb 0f0h		;1a51	f0		.
	defb 088h		;1a52	88		.
	defb 040h		;1a53	40		@
	defb 001h		;1a54	01		.
	defb 015h		;1a55	15		.
	defb 0e0h		;1a56	e0		.
	defb 088h		;1a57	88		.
	defb 0c4h		;1a58	c4		.
	defb 0e8h		;1a59	e8		.
	defb 001h		;1a5a	01		.
	defb 015h		;1a5b	15		.
	defb 0e0h		;1a5c	e0		.
	defb 088h		;1a5d	88		.
	defb 0c4h		;1a5e	c4		.
	defb 0e8h		;1a5f	e8		.
	defb 008h		;1a60	08		.
	defb 0c6h		;1a61	c6		.
	defb 031h		;1a62	31		1
	defb 0e9h		;1a63	e9		.
	defb 032h		;1a64	32		2
	defb 032h		;1a65	32		2
	defb 008h		;1a66	08		.
	defb 0c6h		;1a67	c6		.
	defb 031h		;1a68	31		1
	defb 0e9h		;1a69	e9		.
	defb 032h		;1a6a	32		2
	defb 032h		;1a6b	32		2
	defb 0bch		;1a6c	bc		.
	defb 020h		;1a6d	20		 
	defb 026h		;1a6e	26		&
	defb 027h		;1a6f	27		'
	defb 028h		;1a70	28		(
	defb 029h		;1a71	29		)
	defb 060h		;1a72	60		`
	defb 0c5h		;1a73	c5		.
	defb 0eah		;1a74	ea		.
	defb 0bch		;1a75	bc		.
	defb 020h		;1a76	20		 
	defb 026h		;1a77	26		&
	defb 027h		;1a78	27		'
	defb 028h		;1a79	28		(
	defb 029h		;1a7a	29		)
	defb 060h		;1a7b	60		`
	defb 0c5h		;1a7c	c5		.
	defb 0eah		;1a7d	ea		.
	defb 018h		;1a7e	18		.
	defb 088h		;1a7f	88		.
	defb 0c6h		;1a80	c6		.
	defb 032h		;1a81	32		2
	defb 0f0h		;1a82	f0		.
	defb 088h		;1a83	88		.
	defb 040h		;1a84	40		@
	defb 018h		;1a85	18		.
	defb 088h		;1a86	88		.
	defb 0c6h		;1a87	c6		.
	defb 032h		;1a88	32		2
	defb 0f0h		;1a89	f0		.
	defb 088h		;1a8a	88		.
	defb 040h		;1a8b	40		@
	defb 001h		;1a8c	01		.
	defb 015h		;1a8d	15		.
	defb 0e0h		;1a8e	e0		.
	defb 088h		;1a8f	88		.
	defb 0c4h		;1a90	c4		.
	defb 0e8h		;1a91	e8		.
	defb 001h		;1a92	01		.
	defb 015h		;1a93	15		.
	defb 0e0h		;1a94	e0		.
	defb 088h		;1a95	88		.
	defb 0c4h		;1a96	c4		.
	defb 0e8h		;1a97	e8		.
	defb 008h		;1a98	08		.
	defb 0c6h		;1a99	c6		.
	defb 031h		;1a9a	31		1
	defb 0e9h		;1a9b	e9		.
	defb 032h		;1a9c	32		2
	defb 032h		;1a9d	32		2
	defb 008h		;1a9e	08		.
	defb 0c6h		;1a9f	c6		.
	defb 035h		;1aa0	35		5
	defb 0e9h		;1aa1	e9		.
	defb 032h		;1aa2	32		2
	defb 01ch		;1aa3	1c		.
	defb 032h		;1aa4	32		2
	defb 080h		;1aa5	80		.
	defb 020h		;1aa6	20		 
	defb 060h		;1aa7	60		`
	defb 0c5h		;1aa8	c5		.
	defb 0eah		;1aa9	ea		.
	defb 080h		;1aaa	80		.
	defb 020h		;1aab	20		 
	defb 060h		;1aac	60		`
	defb 0c5h		;1aad	c5		.
	defb 0eah		;1aae	ea		.
	defb 018h		;1aaf	18		.
	defb 088h		;1ab0	88		.
	defb 0c6h		;1ab1	c6		.
	defb 032h		;1ab2	32		2
	defb 0f0h		;1ab3	f0		.
	defb 088h		;1ab4	88		.
	defb 040h		;1ab5	40		@
	defb 018h		;1ab6	18		.
	defb 088h		;1ab7	88		.
	defb 0c6h		;1ab8	c6		.
	defb 032h		;1ab9	32		2
	defb 0f0h		;1aba	f0		.
	defb 088h		;1abb	88		.
	defb 040h		;1abc	40		@
	defb 001h		;1abd	01		.
	defb 015h		;1abe	15		.
	defb 0e0h		;1abf	e0		.
	defb 088h		;1ac0	88		.
	defb 0c4h		;1ac1	c4		.
	defb 0e8h		;1ac2	e8		.
	defb 001h		;1ac3	01		.
	defb 015h		;1ac4	15		.
	defb 0e0h		;1ac5	e0		.
	defb 088h		;1ac6	88		.
	defb 0c4h		;1ac7	c4		.
	defb 0e8h		;1ac8	e8		.
	defb 008h		;1ac9	08		.
	defb 0c6h		;1aca	c6		.
	defb 031h		;1acb	31		1
	defb 0e9h		;1acc	e9		.
	defb 032h		;1acd	32		2
	defb 032h		;1ace	32		2
	defb 008h		;1acf	08		.
	defb 0c6h		;1ad0	c6		.
	defb 035h		;1ad1	35		5
	defb 0e9h		;1ad2	e9		.
	defb 032h		;1ad3	32		2
	defb 01ch		;1ad4	1c		.
	defb 032h		;1ad5	32		2
	defb 080h		;1ad6	80		.
	defb 020h		;1ad7	20		 
	defb 060h		;1ad8	60		`
	defb 0c5h		;1ad9	c5		.
	defb 0eah		;1ada	ea		.
	defb 080h		;1adb	80		.
	defb 020h		;1adc	20		 
	defb 061h		;1add	61		a
	defb 0c5h		;1ade	c5		.
	defb 0eah		;1adf	ea		.
	defb 058h		;1ae0	58		X
	defb 018h		;1ae1	18		.
	defb 088h		;1ae2	88		.
	defb 0c6h		;1ae3	c6		.
	defb 032h		;1ae4	32		2
	defb 0f0h		;1ae5	f0		.
	defb 088h		;1ae6	88		.
	defb 040h		;1ae7	40		@
	defb 018h		;1ae8	18		.
	defb 088h		;1ae9	88		.
	defb 0c6h		;1aea	c6		.
	defb 036h		;1aeb	36		6
	defb 0f0h		;1aec	f0		.
	defb 088h		;1aed	88		.
	defb 052h		;1aee	52		R
	defb 040h		;1aef	40		@
	defb 001h		;1af0	01		.
	defb 010h		;1af1	10		.
	defb 060h		;1af2	60		`
	defb 0c4h		;1af3	c4		.
	defb 0e8h		;1af4	e8		.
	defb 001h		;1af5	01		.
	defb 010h		;1af6	10		.
	defb 060h		;1af7	60		`
	defb 0c4h		;1af8	c4		.
	defb 0e8h		;1af9	e8		.
	defb 009h		;1afa	09		.
	defb 0c6h		;1afb	c6		.
	defb 011h		;1afc	11		.
	defb 020h		;1afd	20		 
	defb 0e9h		;1afe	e9		.
	defb 009h		;1aff	09		.
l1b00h:
	defb 0c6h		;1b00	c6		.
	defb 011h		;1b01	11		.
	defb 020h		;1b02	20		 
	defb 0e9h		;1b03	e9		.
	defb 0fdh		;1b04	fd		.
	defb 020h		;1b05	20		 
	defb 021h		;1b06	21		!
	defb 022h		;1b07	22		"
	defb 023h		;1b08	23		#
	defb 024h		;1b09	24		$
	defb 025h		;1b0a	25		%
	defb 012h		;1b0b	12		.
	defb 060h		;1b0c	60		`
	defb 0c5h		;1b0d	c5		.
	defb 0eah		;1b0e	ea		.
	defb 0fdh		;1b0f	fd		.
	defb 020h		;1b10	20		 
	defb 021h		;1b11	21		!
	defb 022h		;1b12	22		"
	defb 023h		;1b13	23		#
	defb 024h		;1b14	24		$
	defb 025h		;1b15	25		%
	defb 012h		;1b16	12		.
	defb 060h		;1b17	60		`
	defb 0c5h		;1b18	c5		.
	defb 0eah		;1b19	ea		.
	defb 009h		;1b1a	09		.
	defb 0c6h		;1b1b	c6		.
	defb 013h		;1b1c	13		.
	defb 027h		;1b1d	27		'
	defb 0f0h		;1b1e	f0		.
	defb 0c0h		;1b1f	c0		.
	defb 0c2h		;1b20	c2		.
	defb 014h		;1b21	14		.
	defb 009h		;1b22	09		.
	defb 0c6h		;1b23	c6		.
	defb 013h		;1b24	13		.
	defb 023h		;1b25	23		#
	defb 0f0h		;1b26	f0		.
	defb 0c2h		;1b27	c2		.
	defb 014h		;1b28	14		.
	defb 001h		;1b29	01		.
	defb 010h		;1b2a	10		.
	defb 060h		;1b2b	60		`
	defb 0c4h		;1b2c	c4		.
	defb 0e8h		;1b2d	e8		.
	defb 001h		;1b2e	01		.
	defb 010h		;1b2f	10		.
	defb 060h		;1b30	60		`
	defb 0c4h		;1b31	c4		.
	defb 0e8h		;1b32	e8		.
	defb 009h		;1b33	09		.
	defb 0c6h		;1b34	c6		.
	defb 011h		;1b35	11		.
	defb 020h		;1b36	20		 
	defb 0e9h		;1b37	e9		.
	defb 009h		;1b38	09		.
	defb 0c6h		;1b39	c6		.
	defb 011h		;1b3a	11		.
	defb 020h		;1b3b	20		 
	defb 0e9h		;1b3c	e9		.
	defb 0bdh		;1b3d	bd		.
	defb 020h		;1b3e	20		 
	defb 026h		;1b3f	26		&
	defb 027h		;1b40	27		'
	defb 028h		;1b41	28		(
	defb 029h		;1b42	29		)
	defb 012h		;1b43	12		.
	defb 060h		;1b44	60		`
	defb 0c5h		;1b45	c5		.
	defb 0eah		;1b46	ea		.
	defb 0bdh		;1b47	bd		.
	defb 020h		;1b48	20		 
	defb 026h		;1b49	26		&
	defb 027h		;1b4a	27		'
	defb 028h		;1b4b	28		(
	defb 029h		;1b4c	29		)
	defb 012h		;1b4d	12		.
	defb 060h		;1b4e	60		`
	defb 0c5h		;1b4f	c5		.
	defb 0eah		;1b50	ea		.
	defb 009h		;1b51	09		.
	defb 0c6h		;1b52	c6		.
	defb 013h		;1b53	13		.
	defb 027h		;1b54	27		'
	defb 0f0h		;1b55	f0		.
	defb 0c1h		;1b56	c1		.
	defb 0c2h		;1b57	c2		.
	defb 014h		;1b58	14		.
	defb 009h		;1b59	09		.
	defb 0c6h		;1b5a	c6		.
	defb 013h		;1b5b	13		.
	defb 027h		;1b5c	27		'
	defb 0f0h		;1b5d	f0		.
	defb 0c1h		;1b5e	c1		.
	defb 0c2h		;1b5f	c2		.
	defb 014h		;1b60	14		.
	defb 001h		;1b61	01		.
	defb 010h		;1b62	10		.
	defb 060h		;1b63	60		`
	defb 0c4h		;1b64	c4		.
	defb 0e8h		;1b65	e8		.
	defb 001h		;1b66	01		.
	defb 010h		;1b67	10		.
	defb 060h		;1b68	60		`
	defb 0c4h		;1b69	c4		.
	defb 0e8h		;1b6a	e8		.
	defb 009h		;1b6b	09		.
	defb 0c6h		;1b6c	c6		.
	defb 011h		;1b6d	11		.
	defb 020h		;1b6e	20		 
	defb 0e9h		;1b6f	e9		.
	defb 009h		;1b70	09		.
	defb 0c6h		;1b71	c6		.
	defb 011h		;1b72	11		.
	defb 020h		;1b73	20		 
	defb 0e9h		;1b74	e9		.
	defb 081h		;1b75	81		.
	defb 020h		;1b76	20		 
	defb 012h		;1b77	12		.
	defb 060h		;1b78	60		`
	defb 0c5h		;1b79	c5		.
	defb 0eah		;1b7a	ea		.
	defb 081h		;1b7b	81		.
	defb 020h		;1b7c	20		 
	defb 012h		;1b7d	12		.
	defb 060h		;1b7e	60		`
	defb 0c5h		;1b7f	c5		.
	defb 0eah		;1b80	ea		.
	defb 009h		;1b81	09		.
	defb 0c6h		;1b82	c6		.
	defb 013h		;1b83	13		.
	defb 023h		;1b84	23		#
	defb 0f0h		;1b85	f0		.
	defb 0c2h		;1b86	c2		.
	defb 014h		;1b87	14		.
	defb 009h		;1b88	09		.
	defb 0c6h		;1b89	c6		.
	defb 013h		;1b8a	13		.
	defb 027h		;1b8b	27		'
	defb 0f0h		;1b8c	f0		.
	defb 091h		;1b8d	91		.
	defb 0c2h		;1b8e	c2		.
	defb 014h		;1b8f	14		.
	defb 001h		;1b90	01		.
	defb 010h		;1b91	10		.
	defb 060h		;1b92	60		`
	defb 0c4h		;1b93	c4		.
	defb 0e8h		;1b94	e8		.
	defb 001h		;1b95	01		.
	defb 010h		;1b96	10		.
	defb 060h		;1b97	60		`
	defb 0c4h		;1b98	c4		.
	defb 0e8h		;1b99	e8		.
	defb 009h		;1b9a	09		.
	defb 0c6h		;1b9b	c6		.
	defb 011h		;1b9c	11		.
	defb 020h		;1b9d	20		 
	defb 0e9h		;1b9e	e9		.
	defb 009h		;1b9f	09		.
	defb 0c6h		;1ba0	c6		.
	defb 011h		;1ba1	11		.
	defb 020h		;1ba2	20		 
	defb 0e9h		;1ba3	e9		.
	defb 081h		;1ba4	81		.
	defb 020h		;1ba5	20		 
	defb 012h		;1ba6	12		.
	defb 060h		;1ba7	60		`
	defb 0c5h		;1ba8	c5		.
	defb 0eah		;1ba9	ea		.
	defb 081h		;1baa	81		.
	defb 020h		;1bab	20		 
	defb 012h		;1bac	12		.
	defb 060h		;1bad	60		`
	defb 0c5h		;1bae	c5		.
	defb 0eah		;1baf	ea		.
	defb 009h		;1bb0	09		.
	defb 0c6h		;1bb1	c6		.
	defb 013h		;1bb2	13		.
	defb 023h		;1bb3	23		#
	defb 0f0h		;1bb4	f0		.
	defb 0c2h		;1bb5	c2		.
	defb 014h		;1bb6	14		.
	defb 009h		;1bb7	09		.
	defb 0c6h		;1bb8	c6		.
	defb 013h		;1bb9	13		.
	defb 027h		;1bba	27		'
	defb 0f0h		;1bbb	f0		.
	defb 090h		;1bbc	90		.
	defb 0c2h		;1bbd	c2		.
	defb 014h		;1bbe	14		.
	defb 001h		;1bbf	01		.
	defb 010h		;1bc0	10		.
	defb 060h		;1bc1	60		`
	defb 0c4h		;1bc2	c4		.
	defb 0e8h		;1bc3	e8		.
	defb 001h		;1bc4	01		.
	defb 010h		;1bc5	10		.
	defb 060h		;1bc6	60		`
	defb 0c4h		;1bc7	c4		.
	defb 0e8h		;1bc8	e8		.
	defb 009h		;1bc9	09		.
	defb 0c6h		;1bca	c6		.
	defb 011h		;1bcb	11		.
	defb 020h		;1bcc	20		 
	defb 0e9h		;1bcd	e9		.
	defb 009h		;1bce	09		.
	defb 0c6h		;1bcf	c6		.
	defb 011h		;1bd0	11		.
	defb 020h		;1bd1	20		 
	defb 0e9h		;1bd2	e9		.
	defb 0fdh		;1bd3	fd		.
	defb 020h		;1bd4	20		 
	defb 021h		;1bd5	21		!
	defb 022h		;1bd6	22		"
	defb 023h		;1bd7	23		#
	defb 024h		;1bd8	24		$
	defb 025h		;1bd9	25		%
	defb 012h		;1bda	12		.
	defb 060h		;1bdb	60		`
	defb 0c5h		;1bdc	c5		.
	defb 0eah		;1bdd	ea		.
	defb 0fdh		;1bde	fd		.
	defb 020h		;1bdf	20		 
	defb 021h		;1be0	21		!
	defb 022h		;1be1	22		"
	defb 023h		;1be2	23		#
	defb 024h		;1be3	24		$
	defb 025h		;1be4	25		%
	defb 012h		;1be5	12		.
	defb 060h		;1be6	60		`
	defb 0c5h		;1be7	c5		.
	defb 0eah		;1be8	ea		.
	defb 009h		;1be9	09		.
	defb 0c6h		;1bea	c6		.
	defb 013h		;1beb	13		.
	defb 025h		;1bec	25		%
	defb 0f0h		;1bed	f0		.
	defb 0c0h		;1bee	c0		.
	defb 015h		;1bef	15		.
	defb 009h		;1bf0	09		.
	defb 0c6h		;1bf1	c6		.
	defb 013h		;1bf2	13		.
	defb 021h		;1bf3	21		!
	defb 0f0h		;1bf4	f0		.
	defb 015h		;1bf5	15		.
	defb 001h		;1bf6	01		.
	defb 010h		;1bf7	10		.
	defb 060h		;1bf8	60		`
	defb 0c4h		;1bf9	c4		.
	defb 0e8h		;1bfa	e8		.
	defb 001h		;1bfb	01		.
	defb 010h		;1bfc	10		.
	defb 060h		;1bfd	60		`
	defb 0c4h		;1bfe	c4		.
	defb 0e8h		;1bff	e8		.
	defb 009h		;1c00	09		.
	defb 0c6h		;1c01	c6		.
	defb 011h		;1c02	11		.
	defb 020h		;1c03	20		 
	defb 0e9h		;1c04	e9		.
	defb 009h		;1c05	09		.
	defb 0c6h		;1c06	c6		.
	defb 011h		;1c07	11		.
	defb 020h		;1c08	20		 
	defb 0e9h		;1c09	e9		.
	defb 0bdh		;1c0a	bd		.
	defb 020h		;1c0b	20		 
	defb 026h		;1c0c	26		&
	defb 027h		;1c0d	27		'
	defb 028h		;1c0e	28		(
	defb 029h		;1c0f	29		)
	defb 012h		;1c10	12		.
	defb 060h		;1c11	60		`
	defb 0c5h		;1c12	c5		.
	defb 0eah		;1c13	ea		.
	defb 0bdh		;1c14	bd		.
	defb 020h		;1c15	20		 
	defb 026h		;1c16	26		&
	defb 027h		;1c17	27		'
	defb 028h		;1c18	28		(
	defb 029h		;1c19	29		)
	defb 012h		;1c1a	12		.
	defb 060h		;1c1b	60		`
	defb 0c5h		;1c1c	c5		.
	defb 0eah		;1c1d	ea		.
	defb 009h		;1c1e	09		.
	defb 0c6h		;1c1f	c6		.
	defb 013h		;1c20	13		.
	defb 025h		;1c21	25		%
	defb 0f0h		;1c22	f0		.
	defb 0c1h		;1c23	c1		.
	defb 015h		;1c24	15		.
	defb 009h		;1c25	09		.
	defb 0c6h		;1c26	c6		.
	defb 013h		;1c27	13		.
	defb 025h		;1c28	25		%
	defb 0f0h		;1c29	f0		.
	defb 0c1h		;1c2a	c1		.
	defb 015h		;1c2b	15		.
	defb 001h		;1c2c	01		.
	defb 010h		;1c2d	10		.
	defb 060h		;1c2e	60		`
	defb 0c4h		;1c2f	c4		.
	defb 0e8h		;1c30	e8		.
	defb 001h		;1c31	01		.
	defb 010h		;1c32	10		.
	defb 060h		;1c33	60		`
	defb 0c4h		;1c34	c4		.
	defb 0e8h		;1c35	e8		.
	defb 009h		;1c36	09		.
	defb 0c6h		;1c37	c6		.
	defb 011h		;1c38	11		.
	defb 020h		;1c39	20		 
	defb 0e9h		;1c3a	e9		.
	defb 009h		;1c3b	09		.
	defb 0c6h		;1c3c	c6		.
	defb 011h		;1c3d	11		.
	defb 020h		;1c3e	20		 
	defb 0e9h		;1c3f	e9		.
	defb 081h		;1c40	81		.
	defb 020h		;1c41	20		 
	defb 012h		;1c42	12		.
	defb 060h		;1c43	60		`
	defb 0c5h		;1c44	c5		.
	defb 0eah		;1c45	ea		.
	defb 081h		;1c46	81		.
	defb 020h		;1c47	20		 
	defb 012h		;1c48	12		.
	defb 060h		;1c49	60		`
	defb 0c5h		;1c4a	c5		.
	defb 0eah		;1c4b	ea		.
	defb 009h		;1c4c	09		.
	defb 0c6h		;1c4d	c6		.
	defb 013h		;1c4e	13		.
	defb 021h		;1c4f	21		!
	defb 0f0h		;1c50	f0		.
	defb 015h		;1c51	15		.
	defb 009h		;1c52	09		.
	defb 0c6h		;1c53	c6		.
	defb 013h		;1c54	13		.
	defb 021h		;1c55	21		!
	defb 0f0h		;1c56	f0		.
	defb 015h		;1c57	15		.
	defb 001h		;1c58	01		.
	defb 010h		;1c59	10		.
	defb 060h		;1c5a	60		`
	defb 0c4h		;1c5b	c4		.
	defb 0e8h		;1c5c	e8		.
	defb 001h		;1c5d	01		.
	defb 010h		;1c5e	10		.
	defb 060h		;1c5f	60		`
	defb 0c4h		;1c60	c4		.
	defb 0e8h		;1c61	e8		.
	defb 009h		;1c62	09		.
	defb 0c6h		;1c63	c6		.
	defb 011h		;1c64	11		.
	defb 020h		;1c65	20		 
	defb 0e9h		;1c66	e9		.
	defb 009h		;1c67	09		.
	defb 0c6h		;1c68	c6		.
	defb 011h		;1c69	11		.
	defb 020h		;1c6a	20		 
	defb 0e9h		;1c6b	e9		.
	defb 081h		;1c6c	81		.
	defb 020h		;1c6d	20		 
	defb 012h		;1c6e	12		.
	defb 060h		;1c6f	60		`
	defb 0c5h		;1c70	c5		.
	defb 0eah		;1c71	ea		.
	defb 081h		;1c72	81		.
	defb 020h		;1c73	20		 
	defb 012h		;1c74	12		.
	defb 060h		;1c75	60		`
	defb 0c5h		;1c76	c5		.
	defb 0eah		;1c77	ea		.
	defb 009h		;1c78	09		.
	defb 0c6h		;1c79	c6		.
	defb 013h		;1c7a	13		.
	defb 021h		;1c7b	21		!
	defb 0f0h		;1c7c	f0		.
	defb 015h		;1c7d	15		.
	defb 009h		;1c7e	09		.
	defb 0c6h		;1c7f	c6		.
	defb 013h		;1c80	13		.
	defb 025h		;1c81	25		%
	defb 0f0h		;1c82	f0		.
	defb 052h		;1c83	52		R
	defb 015h		;1c84	15		.
	defb 000h		;1c85	00		.
	defb 000h		;1c86	00		.
	defb 000h		;1c87	00		.
	defb 000h		;1c88	00		.
	defb 008h		;1c89	08		.
	defb 0c6h		;1c8a	c6		.
	defb 000h		;1c8b	00		.
	defb 008h		;1c8c	08		.
	defb 0c6h		;1c8d	c6		.
	defb 000h		;1c8e	00		.
	defb 000h		;1c8f	00		.
	defb 000h		;1c90	00		.
	defb 000h		;1c91	00		.
	defb 000h		;1c92	00		.
	defb 018h		;1c93	18		.
	defb 088h		;1c94	88		.
	defb 0c6h		;1c95	c6		.
	defb 016h		;1c96	16		.
	defb 088h		;1c97	88		.
	defb 0c0h		;1c98	c0		.
	defb 0c2h		;1c99	c2		.
	defb 018h		;1c9a	18		.
	defb 088h		;1c9b	88		.
	defb 0c6h		;1c9c	c6		.
	defb 012h		;1c9d	12		.
	defb 088h		;1c9e	88		.
	defb 0c2h		;1c9f	c2		.
	defb 000h		;1ca0	00		.
	defb 000h		;1ca1	00		.
	defb 000h		;1ca2	00		.
	defb 000h		;1ca3	00		.
	defb 008h		;1ca4	08		.
	defb 0c6h		;1ca5	c6		.
	defb 000h		;1ca6	00		.
	defb 008h		;1ca7	08		.
	defb 0c6h		;1ca8	c6		.
	defb 000h		;1ca9	00		.
	defb 000h		;1caa	00		.
	defb 000h		;1cab	00		.
	defb 000h		;1cac	00		.
	defb 000h		;1cad	00		.
	defb 018h		;1cae	18		.
	defb 088h		;1caf	88		.
	defb 0c6h		;1cb0	c6		.
	defb 016h		;1cb1	16		.
	defb 088h		;1cb2	88		.
	defb 0c1h		;1cb3	c1		.
	defb 0c2h		;1cb4	c2		.
	defb 018h		;1cb5	18		.
	defb 088h		;1cb6	88		.
	defb 0c6h		;1cb7	c6		.
	defb 016h		;1cb8	16		.
	defb 088h		;1cb9	88		.
	defb 0c1h		;1cba	c1		.
	defb 0c2h		;1cbb	c2		.
	defb 000h		;1cbc	00		.
	defb 000h		;1cbd	00		.
	defb 000h		;1cbe	00		.
	defb 000h		;1cbf	00		.
	defb 008h		;1cc0	08		.
	defb 0c6h		;1cc1	c6		.
	defb 000h		;1cc2	00		.
	defb 008h		;1cc3	08		.
	defb 0c6h		;1cc4	c6		.
	defb 000h		;1cc5	00		.
	defb 000h		;1cc6	00		.
	defb 000h		;1cc7	00		.
	defb 000h		;1cc8	00		.
	defb 000h		;1cc9	00		.
	defb 018h		;1cca	18		.
	defb 088h		;1ccb	88		.
	defb 0c6h		;1ccc	c6		.
	defb 012h		;1ccd	12		.
	defb 088h		;1cce	88		.
	defb 0c2h		;1ccf	c2		.
	defb 018h		;1cd0	18		.
	defb 088h		;1cd1	88		.
	defb 0c6h		;1cd2	c6		.
	defb 016h		;1cd3	16		.
	defb 088h		;1cd4	88		.
	defb 091h		;1cd5	91		.
	defb 0c2h		;1cd6	c2		.
	defb 000h		;1cd7	00		.
	defb 000h		;1cd8	00		.
	defb 000h		;1cd9	00		.
	defb 000h		;1cda	00		.
	defb 008h		;1cdb	08		.
	defb 0c6h		;1cdc	c6		.
	defb 000h		;1cdd	00		.
	defb 008h		;1cde	08		.
	defb 0c6h		;1cdf	c6		.
	defb 000h		;1ce0	00		.
	defb 000h		;1ce1	00		.
	defb 000h		;1ce2	00		.
	defb 000h		;1ce3	00		.
	defb 000h		;1ce4	00		.
	defb 018h		;1ce5	18		.
	defb 088h		;1ce6	88		.
	defb 0c6h		;1ce7	c6		.
	defb 012h		;1ce8	12		.
	defb 088h		;1ce9	88		.
	defb 0c2h		;1cea	c2		.
	defb 018h		;1ceb	18		.
	defb 088h		;1cec	88		.
	defb 0c6h		;1ced	c6		.
	defb 016h		;1cee	16		.
	defb 088h		;1cef	88		.
	defb 090h		;1cf0	90		.
	defb 0c2h		;1cf1	c2		.
	defb 001h		;1cf2	01		.
	defb 015h		;1cf3	15		.
	defb 000h		;1cf4	00		.
	defb 001h		;1cf5	01		.
	defb 015h		;1cf6	15		.
	defb 000h		;1cf7	00		.
	defb 008h		;1cf8	08		.
	defb 0c6h		;1cf9	c6		.
	defb 000h		;1cfa	00		.
	defb 008h		;1cfb	08		.
	defb 0c6h		;1cfc	c6		.
	defb 000h		;1cfd	00		.
	defb 000h		;1cfe	00		.
	defb 000h		;1cff	00		.
	defb 000h		;1d00	00		.
	defb 000h		;1d01	00		.
	defb 018h		;1d02	18		.
	defb 088h		;1d03	88		.
	defb 0c6h		;1d04	c6		.
	defb 016h		;1d05	16		.
	defb 088h		;1d06	88		.
	defb 0c0h		;1d07	c0		.
	defb 040h		;1d08	40		@
	defb 018h		;1d09	18		.
	defb 088h		;1d0a	88		.
	defb 0c6h		;1d0b	c6		.
	defb 012h		;1d0c	12		.
	defb 088h		;1d0d	88		.
	defb 040h		;1d0e	40		@
	defb 001h		;1d0f	01		.
	defb 015h		;1d10	15		.
	defb 000h		;1d11	00		.
	defb 001h		;1d12	01		.
	defb 015h		;1d13	15		.
	defb 000h		;1d14	00		.
	defb 008h		;1d15	08		.
	defb 0c6h		;1d16	c6		.
	defb 000h		;1d17	00		.
	defb 008h		;1d18	08		.
	defb 0c6h		;1d19	c6		.
	defb 000h		;1d1a	00		.
	defb 000h		;1d1b	00		.
	defb 000h		;1d1c	00		.
	defb 000h		;1d1d	00		.
	defb 000h		;1d1e	00		.
	defb 018h		;1d1f	18		.
	defb 088h		;1d20	88		.
	defb 0c6h		;1d21	c6		.
	defb 016h		;1d22	16		.
	defb 088h		;1d23	88		.
	defb 0c1h		;1d24	c1		.
	defb 040h		;1d25	40		@
	defb 018h		;1d26	18		.
	defb 088h		;1d27	88		.
	defb 0c6h		;1d28	c6		.
	defb 016h		;1d29	16		.
	defb 088h		;1d2a	88		.
	defb 0c1h		;1d2b	c1		.
	defb 040h		;1d2c	40		@
	defb 001h		;1d2d	01		.
	defb 015h		;1d2e	15		.
	defb 000h		;1d2f	00		.
	defb 001h		;1d30	01		.
	defb 015h		;1d31	15		.
	defb 000h		;1d32	00		.
	defb 008h		;1d33	08		.
	defb 0c6h		;1d34	c6		.
	defb 000h		;1d35	00		.
	defb 008h		;1d36	08		.
	defb 0c6h		;1d37	c6		.
	defb 000h		;1d38	00		.
	defb 000h		;1d39	00		.
	defb 000h		;1d3a	00		.
	defb 000h		;1d3b	00		.
	defb 000h		;1d3c	00		.
	defb 018h		;1d3d	18		.
	defb 088h		;1d3e	88		.
	defb 0c6h		;1d3f	c6		.
	defb 012h		;1d40	12		.
	defb 088h		;1d41	88		.
	defb 040h		;1d42	40		@
	defb 018h		;1d43	18		.
	defb 088h		;1d44	88		.
	defb 0c6h		;1d45	c6		.
	defb 012h		;1d46	12		.
	defb 088h		;1d47	88		.
	defb 040h		;1d48	40		@
	defb 001h		;1d49	01		.
	defb 015h		;1d4a	15		.
	defb 000h		;1d4b	00		.
	defb 001h		;1d4c	01		.
	defb 015h		;1d4d	15		.
	defb 000h		;1d4e	00		.
	defb 008h		;1d4f	08		.
	defb 0c6h		;1d50	c6		.
	defb 000h		;1d51	00		.
	defb 008h		;1d52	08		.
	defb 0c6h		;1d53	c6		.
	defb 000h		;1d54	00		.
	defb 000h		;1d55	00		.
	defb 000h		;1d56	00		.
	defb 000h		;1d57	00		.
	defb 000h		;1d58	00		.
	defb 018h		;1d59	18		.
	defb 088h		;1d5a	88		.
	defb 0c6h		;1d5b	c6		.
	defb 012h		;1d5c	12		.
	defb 088h		;1d5d	88		.
	defb 040h		;1d5e	40		@
	defb 018h		;1d5f	18		.
	defb 088h		;1d60	88		.
	defb 0c6h		;1d61	c6		.
	defb 016h		;1d62	16		.
	defb 088h		;1d63	88		.
	defb 052h		;1d64	52		R
	defb 040h		;1d65	40		@
	defb 001h		;1d66	01		.
	defb 010h		;1d67	10		.
	defb 060h		;1d68	60		`
	defb 0c4h		;1d69	c4		.
	defb 0e8h		;1d6a	e8		.
	defb 001h		;1d6b	01		.
	defb 010h		;1d6c	10		.
	defb 060h		;1d6d	60		`
	defb 0c4h		;1d6e	c4		.
	defb 0e8h		;1d6f	e8		.
	defb 009h		;1d70	09		.
	defb 0c6h		;1d71	c6		.
	defb 011h		;1d72	11		.
	defb 020h		;1d73	20		 
	defb 0e9h		;1d74	e9		.
	defb 009h		;1d75	09		.
	defb 0c6h		;1d76	c6		.
	defb 011h		;1d77	11		.
	defb 020h		;1d78	20		 
	defb 0e9h		;1d79	e9		.
	defb 0fdh		;1d7a	fd		.
	defb 020h		;1d7b	20		 
	defb 021h		;1d7c	21		!
	defb 022h		;1d7d	22		"
	defb 023h		;1d7e	23		#
	defb 024h		;1d7f	24		$
	defb 025h		;1d80	25		%
	defb 012h		;1d81	12		.
	defb 060h		;1d82	60		`
	defb 0c5h		;1d83	c5		.
	defb 0eah		;1d84	ea		.
	defb 0fdh		;1d85	fd		.
	defb 020h		;1d86	20		 
	defb 021h		;1d87	21		!
	defb 022h		;1d88	22		"
	defb 023h		;1d89	23		#
	defb 024h		;1d8a	24		$
	defb 025h		;1d8b	25		%
	defb 012h		;1d8c	12		.
	defb 060h		;1d8d	60		`
	defb 0c5h		;1d8e	c5		.
	defb 0eah		;1d8f	ea		.
	defb 009h		;1d90	09		.
	defb 0c6h		;1d91	c6		.
	defb 013h		;1d92	13		.
	defb 027h		;1d93	27		'
	defb 0f0h		;1d94	f0		.
	defb 0c0h		;1d95	c0		.
	defb 0c2h		;1d96	c2		.
	defb 014h		;1d97	14		.
	defb 009h		;1d98	09		.
	defb 0c6h		;1d99	c6		.
	defb 013h		;1d9a	13		.
	defb 023h		;1d9b	23		#
	defb 0f0h		;1d9c	f0		.
	defb 0c2h		;1d9d	c2		.
	defb 014h		;1d9e	14		.
	defb 001h		;1d9f	01		.
	defb 010h		;1da0	10		.
	defb 060h		;1da1	60		`
	defb 0c4h		;1da2	c4		.
	defb 0e8h		;1da3	e8		.
	defb 001h		;1da4	01		.
	defb 010h		;1da5	10		.
	defb 060h		;1da6	60		`
	defb 0c4h		;1da7	c4		.
	defb 0e8h		;1da8	e8		.
	defb 009h		;1da9	09		.
	defb 0c6h		;1daa	c6		.
	defb 011h		;1dab	11		.
	defb 020h		;1dac	20		 
	defb 0e9h		;1dad	e9		.
	defb 009h		;1dae	09		.
	defb 0c6h		;1daf	c6		.
	defb 011h		;1db0	11		.
	defb 020h		;1db1	20		 
	defb 0e9h		;1db2	e9		.
	defb 0bdh		;1db3	bd		.
	defb 020h		;1db4	20		 
	defb 026h		;1db5	26		&
	defb 027h		;1db6	27		'
	defb 028h		;1db7	28		(
	defb 029h		;1db8	29		)
	defb 012h		;1db9	12		.
	defb 060h		;1dba	60		`
	defb 0c5h		;1dbb	c5		.
	defb 0eah		;1dbc	ea		.
	defb 0bdh		;1dbd	bd		.
	defb 020h		;1dbe	20		 
	defb 026h		;1dbf	26		&
	defb 027h		;1dc0	27		'
	defb 028h		;1dc1	28		(
	defb 029h		;1dc2	29		)
	defb 012h		;1dc3	12		.
	defb 060h		;1dc4	60		`
	defb 0c5h		;1dc5	c5		.
	defb 0eah		;1dc6	ea		.
	defb 009h		;1dc7	09		.
	defb 0c6h		;1dc8	c6		.
	defb 013h		;1dc9	13		.
	defb 023h		;1dca	23		#
	defb 0f0h		;1dcb	f0		.
	defb 0c2h		;1dcc	c2		.
	defb 014h		;1dcd	14		.
	defb 009h		;1dce	09		.
	defb 0c6h		;1dcf	c6		.
	defb 013h		;1dd0	13		.
	defb 023h		;1dd1	23		#
	defb 0f0h		;1dd2	f0		.
	defb 0c2h		;1dd3	c2		.
	defb 014h		;1dd4	14		.
	defb 001h		;1dd5	01		.
	defb 010h		;1dd6	10		.
	defb 060h		;1dd7	60		`
	defb 0c4h		;1dd8	c4		.
	defb 0e8h		;1dd9	e8		.
	defb 001h		;1dda	01		.
	defb 010h		;1ddb	10		.
	defb 060h		;1ddc	60		`
	defb 0c4h		;1ddd	c4		.
	defb 0e8h		;1dde	e8		.
	defb 009h		;1ddf	09		.
	defb 0c6h		;1de0	c6		.
	defb 011h		;1de1	11		.
	defb 020h		;1de2	20		 
	defb 0e9h		;1de3	e9		.
	defb 009h		;1de4	09		.
	defb 0c6h		;1de5	c6		.
	defb 011h		;1de6	11		.
	defb 020h		;1de7	20		 
	defb 0e9h		;1de8	e9		.
	defb 081h		;1de9	81		.
	defb 020h		;1dea	20		 
	defb 012h		;1deb	12		.
	defb 060h		;1dec	60		`
	defb 0c5h		;1ded	c5		.
	defb 0eah		;1dee	ea		.
	defb 081h		;1def	81		.
	defb 020h		;1df0	20		 
	defb 012h		;1df1	12		.
	defb 060h		;1df2	60		`
	defb 0c5h		;1df3	c5		.
	defb 0eah		;1df4	ea		.
	defb 009h		;1df5	09		.
	defb 0c6h		;1df6	c6		.
	defb 013h		;1df7	13		.
	defb 023h		;1df8	23		#
	defb 0f0h		;1df9	f0		.
	defb 0c2h		;1dfa	c2		.
	defb 014h		;1dfb	14		.
	defb 009h		;1dfc	09		.
	defb 0c6h		;1dfd	c6		.
	defb 013h		;1dfe	13		.
	defb 027h		;1dff	27		'
	defb 0f0h		;1e00	f0		.
	defb 091h		;1e01	91		.
	defb 0c2h		;1e02	c2		.
	defb 014h		;1e03	14		.
	defb 001h		;1e04	01		.
	defb 010h		;1e05	10		.
	defb 060h		;1e06	60		`
	defb 0c4h		;1e07	c4		.
	defb 0e8h		;1e08	e8		.
	defb 001h		;1e09	01		.
	defb 010h		;1e0a	10		.
	defb 060h		;1e0b	60		`
	defb 0c4h		;1e0c	c4		.
	defb 0e8h		;1e0d	e8		.
	defb 009h		;1e0e	09		.
	defb 0c6h		;1e0f	c6		.
	defb 011h		;1e10	11		.
	defb 020h		;1e11	20		 
	defb 0e9h		;1e12	e9		.
	defb 009h		;1e13	09		.
	defb 0c6h		;1e14	c6		.
	defb 011h		;1e15	11		.
	defb 020h		;1e16	20		 
	defb 0e9h		;1e17	e9		.
	defb 081h		;1e18	81		.
	defb 020h		;1e19	20		 
	defb 012h		;1e1a	12		.
	defb 060h		;1e1b	60		`
	defb 0c5h		;1e1c	c5		.
	defb 0eah		;1e1d	ea		.
	defb 081h		;1e1e	81		.
	defb 020h		;1e1f	20		 
	defb 012h		;1e20	12		.
	defb 060h		;1e21	60		`
	defb 0c5h		;1e22	c5		.
	defb 0eah		;1e23	ea		.
	defb 009h		;1e24	09		.
	defb 0c6h		;1e25	c6		.
	defb 013h		;1e26	13		.
	defb 023h		;1e27	23		#
	defb 0f0h		;1e28	f0		.
	defb 0c2h		;1e29	c2		.
	defb 014h		;1e2a	14		.
	defb 009h		;1e2b	09		.
	defb 0c6h		;1e2c	c6		.
	defb 013h		;1e2d	13		.
	defb 027h		;1e2e	27		'
	defb 0f0h		;1e2f	f0		.
	defb 090h		;1e30	90		.
	defb 0c2h		;1e31	c2		.
	defb 014h		;1e32	14		.
	defb 001h		;1e33	01		.
	defb 010h		;1e34	10		.
	defb 060h		;1e35	60		`
	defb 0c4h		;1e36	c4		.
	defb 0e8h		;1e37	e8		.
	defb 001h		;1e38	01		.
	defb 010h		;1e39	10		.
	defb 060h		;1e3a	60		`
	defb 0c4h		;1e3b	c4		.
	defb 0e8h		;1e3c	e8		.
	defb 009h		;1e3d	09		.
	defb 0c6h		;1e3e	c6		.
	defb 011h		;1e3f	11		.
	defb 020h		;1e40	20		 
	defb 0e9h		;1e41	e9		.
	defb 009h		;1e42	09		.
	defb 0c6h		;1e43	c6		.
	defb 011h		;1e44	11		.
	defb 020h		;1e45	20		 
	defb 0e9h		;1e46	e9		.
	defb 0fdh		;1e47	fd		.
	defb 020h		;1e48	20		 
	defb 021h		;1e49	21		!
	defb 022h		;1e4a	22		"
	defb 023h		;1e4b	23		#
	defb 024h		;1e4c	24		$
	defb 025h		;1e4d	25		%
	defb 012h		;1e4e	12		.
	defb 060h		;1e4f	60		`
	defb 0c5h		;1e50	c5		.
	defb 0eah		;1e51	ea		.
	defb 0fdh		;1e52	fd		.
	defb 020h		;1e53	20		 
	defb 021h		;1e54	21		!
	defb 022h		;1e55	22		"
	defb 023h		;1e56	23		#
	defb 024h		;1e57	24		$
	defb 025h		;1e58	25		%
	defb 012h		;1e59	12		.
	defb 060h		;1e5a	60		`
	defb 0c5h		;1e5b	c5		.
	defb 0eah		;1e5c	ea		.
	defb 009h		;1e5d	09		.
	defb 0c6h		;1e5e	c6		.
	defb 013h		;1e5f	13		.
	defb 025h		;1e60	25		%
	defb 0f0h		;1e61	f0		.
	defb 0c0h		;1e62	c0		.
	defb 015h		;1e63	15		.
	defb 009h		;1e64	09		.
	defb 0c6h		;1e65	c6		.
	defb 013h		;1e66	13		.
	defb 021h		;1e67	21		!
	defb 0f0h		;1e68	f0		.
	defb 015h		;1e69	15		.
	defb 001h		;1e6a	01		.
	defb 010h		;1e6b	10		.
	defb 060h		;1e6c	60		`
	defb 0c4h		;1e6d	c4		.
	defb 0e8h		;1e6e	e8		.
	defb 001h		;1e6f	01		.
	defb 010h		;1e70	10		.
	defb 060h		;1e71	60		`
	defb 0c4h		;1e72	c4		.
	defb 0e8h		;1e73	e8		.
	defb 009h		;1e74	09		.
	defb 0c6h		;1e75	c6		.
	defb 011h		;1e76	11		.
	defb 020h		;1e77	20		 
	defb 0e9h		;1e78	e9		.
	defb 009h		;1e79	09		.
	defb 0c6h		;1e7a	c6		.
	defb 011h		;1e7b	11		.
	defb 020h		;1e7c	20		 
	defb 0e9h		;1e7d	e9		.
	defb 0bdh		;1e7e	bd		.
	defb 020h		;1e7f	20		 
	defb 026h		;1e80	26		&
	defb 027h		;1e81	27		'
	defb 028h		;1e82	28		(
	defb 029h		;1e83	29		)
	defb 012h		;1e84	12		.
	defb 060h		;1e85	60		`
	defb 0c5h		;1e86	c5		.
	defb 0eah		;1e87	ea		.
	defb 0bdh		;1e88	bd		.
	defb 020h		;1e89	20		 
	defb 026h		;1e8a	26		&
	defb 027h		;1e8b	27		'
	defb 028h		;1e8c	28		(
	defb 029h		;1e8d	29		)
	defb 012h		;1e8e	12		.
	defb 060h		;1e8f	60		`
	defb 0c5h		;1e90	c5		.
	defb 0eah		;1e91	ea		.
	defb 009h		;1e92	09		.
	defb 0c6h		;1e93	c6		.
	defb 013h		;1e94	13		.
	defb 021h		;1e95	21		!
	defb 0f0h		;1e96	f0		.
	defb 015h		;1e97	15		.
	defb 009h		;1e98	09		.
	defb 0c6h		;1e99	c6		.
	defb 013h		;1e9a	13		.
	defb 021h		;1e9b	21		!
	defb 0f0h		;1e9c	f0		.
	defb 015h		;1e9d	15		.
	defb 001h		;1e9e	01		.
	defb 010h		;1e9f	10		.
	defb 060h		;1ea0	60		`
	defb 0c4h		;1ea1	c4		.
	defb 0e8h		;1ea2	e8		.
	defb 001h		;1ea3	01		.
	defb 010h		;1ea4	10		.
	defb 060h		;1ea5	60		`
	defb 0c4h		;1ea6	c4		.
	defb 0e8h		;1ea7	e8		.
	defb 009h		;1ea8	09		.
	defb 0c6h		;1ea9	c6		.
	defb 011h		;1eaa	11		.
	defb 020h		;1eab	20		 
	defb 0e9h		;1eac	e9		.
	defb 009h		;1ead	09		.
	defb 0c6h		;1eae	c6		.
	defb 011h		;1eaf	11		.
	defb 020h		;1eb0	20		 
	defb 0e9h		;1eb1	e9		.
	defb 081h		;1eb2	81		.
	defb 020h		;1eb3	20		 
	defb 012h		;1eb4	12		.
	defb 060h		;1eb5	60		`
	defb 0c5h		;1eb6	c5		.
	defb 0eah		;1eb7	ea		.
	defb 081h		;1eb8	81		.
	defb 020h		;1eb9	20		 
	defb 012h		;1eba	12		.
	defb 060h		;1ebb	60		`
	defb 0c5h		;1ebc	c5		.
	defb 0eah		;1ebd	ea		.
	defb 009h		;1ebe	09		.
	defb 0c6h		;1ebf	c6		.
	defb 013h		;1ec0	13		.
	defb 021h		;1ec1	21		!
	defb 0f0h		;1ec2	f0		.
	defb 015h		;1ec3	15		.
	defb 009h		;1ec4	09		.
	defb 0c6h		;1ec5	c6		.
	defb 013h		;1ec6	13		.
	defb 021h		;1ec7	21		!
	defb 0f0h		;1ec8	f0		.
	defb 015h		;1ec9	15		.
	defb 001h		;1eca	01		.
	defb 010h		;1ecb	10		.
	defb 060h		;1ecc	60		`
	defb 0c4h		;1ecd	c4		.
	defb 0e8h		;1ece	e8		.
	defb 001h		;1ecf	01		.
	defb 010h		;1ed0	10		.
	defb 060h		;1ed1	60		`
	defb 0c4h		;1ed2	c4		.
	defb 0e8h		;1ed3	e8		.
	defb 009h		;1ed4	09		.
	defb 0c6h		;1ed5	c6		.
	defb 011h		;1ed6	11		.
	defb 020h		;1ed7	20		 
	defb 0e9h		;1ed8	e9		.
	defb 009h		;1ed9	09		.
	defb 0c6h		;1eda	c6		.
	defb 011h		;1edb	11		.
	defb 020h		;1edc	20		 
	defb 0e9h		;1edd	e9		.
	defb 081h		;1ede	81		.
	defb 020h		;1edf	20		 
	defb 012h		;1ee0	12		.
	defb 060h		;1ee1	60		`
	defb 0c5h		;1ee2	c5		.
	defb 0eah		;1ee3	ea		.
	defb 081h		;1ee4	81		.
	defb 020h		;1ee5	20		 
	defb 012h		;1ee6	12		.
	defb 060h		;1ee7	60		`
	defb 0c5h		;1ee8	c5		.
	defb 0eah		;1ee9	ea		.
	defb 009h		;1eea	09		.
	defb 0c6h		;1eeb	c6		.
	defb 013h		;1eec	13		.
	defb 021h		;1eed	21		!
	defb 0f0h		;1eee	f0		.
	defb 015h		;1eef	15		.
	defb 009h		;1ef0	09		.
	defb 0c6h		;1ef1	c6		.
	defb 013h		;1ef2	13		.
	defb 025h		;1ef3	25		%
	defb 0f0h		;1ef4	f0		.
	defb 052h		;1ef5	52		R
	defb 015h		;1ef6	15		.
	defb 000h		;1ef7	00		.
	defb 000h		;1ef8	00		.
	defb 000h		;1ef9	00		.
	defb 000h		;1efa	00		.
	defb 008h		;1efb	08		.
	defb 0c6h		;1efc	c6		.
	defb 000h		;1efd	00		.
	defb 008h		;1efe	08		.
	defb 0c6h		;1eff	c6		.
	defb 000h		;1f00	00		.
	defb 000h		;1f01	00		.
	defb 000h		;1f02	00		.
	defb 000h		;1f03	00		.
	defb 000h		;1f04	00		.
	defb 018h		;1f05	18		.
	defb 088h		;1f06	88		.
	defb 0c6h		;1f07	c6		.
	defb 016h		;1f08	16		.
	defb 088h		;1f09	88		.
	defb 0c0h		;1f0a	c0		.
	defb 0c2h		;1f0b	c2		.
	defb 018h		;1f0c	18		.
	defb 088h		;1f0d	88		.
	defb 0c6h		;1f0e	c6		.
	defb 012h		;1f0f	12		.
	defb 088h		;1f10	88		.
	defb 0c2h		;1f11	c2		.
	defb 000h		;1f12	00		.
	defb 000h		;1f13	00		.
	defb 000h		;1f14	00		.
	defb 000h		;1f15	00		.
	defb 008h		;1f16	08		.
	defb 0c6h		;1f17	c6		.
	defb 000h		;1f18	00		.
	defb 008h		;1f19	08		.
	defb 0c6h		;1f1a	c6		.
	defb 000h		;1f1b	00		.
	defb 000h		;1f1c	00		.
	defb 000h		;1f1d	00		.
	defb 000h		;1f1e	00		.
	defb 000h		;1f1f	00		.
	defb 018h		;1f20	18		.
	defb 088h		;1f21	88		.
	defb 0c6h		;1f22	c6		.
	defb 012h		;1f23	12		.
	defb 088h		;1f24	88		.
	defb 0c2h		;1f25	c2		.
	defb 018h		;1f26	18		.
	defb 088h		;1f27	88		.
	defb 0c6h		;1f28	c6		.
	defb 012h		;1f29	12		.
	defb 088h		;1f2a	88		.
	defb 0c2h		;1f2b	c2		.
	defb 000h		;1f2c	00		.
	defb 000h		;1f2d	00		.
	defb 000h		;1f2e	00		.
	defb 000h		;1f2f	00		.
	defb 008h		;1f30	08		.
	defb 0c6h		;1f31	c6		.
	defb 000h		;1f32	00		.
	defb 008h		;1f33	08		.
	defb 0c6h		;1f34	c6		.
	defb 000h		;1f35	00		.
	defb 000h		;1f36	00		.
	defb 000h		;1f37	00		.
	defb 000h		;1f38	00		.
	defb 000h		;1f39	00		.
	defb 018h		;1f3a	18		.
	defb 088h		;1f3b	88		.
	defb 0c6h		;1f3c	c6		.
	defb 012h		;1f3d	12		.
	defb 088h		;1f3e	88		.
	defb 0c2h		;1f3f	c2		.
	defb 018h		;1f40	18		.
	defb 088h		;1f41	88		.
	defb 0c6h		;1f42	c6		.
	defb 016h		;1f43	16		.
	defb 088h		;1f44	88		.
	defb 091h		;1f45	91		.
	defb 0c2h		;1f46	c2		.
	defb 000h		;1f47	00		.
	defb 000h		;1f48	00		.
	defb 000h		;1f49	00		.
	defb 000h		;1f4a	00		.
	defb 008h		;1f4b	08		.
	defb 0c6h		;1f4c	c6		.
	defb 000h		;1f4d	00		.
	defb 008h		;1f4e	08		.
	defb 0c6h		;1f4f	c6		.
	defb 000h		;1f50	00		.
	defb 000h		;1f51	00		.
	defb 000h		;1f52	00		.
	defb 000h		;1f53	00		.
	defb 000h		;1f54	00		.
	defb 018h		;1f55	18		.
	defb 088h		;1f56	88		.
	defb 0c6h		;1f57	c6		.
	defb 012h		;1f58	12		.
	defb 088h		;1f59	88		.
	defb 0c2h		;1f5a	c2		.
	defb 018h		;1f5b	18		.
	defb 088h		;1f5c	88		.
	defb 0c6h		;1f5d	c6		.
	defb 016h		;1f5e	16		.
	defb 088h		;1f5f	88		.
	defb 090h		;1f60	90		.
	defb 0c2h		;1f61	c2		.
	defb 001h		;1f62	01		.
	defb 015h		;1f63	15		.
	defb 000h		;1f64	00		.
	defb 001h		;1f65	01		.
	defb 015h		;1f66	15		.
	defb 000h		;1f67	00		.
	defb 008h		;1f68	08		.
	defb 0c6h		;1f69	c6		.
	defb 000h		;1f6a	00		.
	defb 008h		;1f6b	08		.
	defb 0c6h		;1f6c	c6		.
	defb 000h		;1f6d	00		.
	defb 000h		;1f6e	00		.
	defb 000h		;1f6f	00		.
	defb 000h		;1f70	00		.
	defb 000h		;1f71	00		.
	defb 018h		;1f72	18		.
	defb 088h		;1f73	88		.
	defb 0c6h		;1f74	c6		.
	defb 016h		;1f75	16		.
	defb 088h		;1f76	88		.
	defb 0c0h		;1f77	c0		.
	defb 040h		;1f78	40		@
	defb 018h		;1f79	18		.
	defb 088h		;1f7a	88		.
	defb 0c6h		;1f7b	c6		.
	defb 012h		;1f7c	12		.
	defb 088h		;1f7d	88		.
	defb 040h		;1f7e	40		@
	defb 001h		;1f7f	01		.
	defb 015h		;1f80	15		.
	defb 000h		;1f81	00		.
	defb 001h		;1f82	01		.
	defb 015h		;1f83	15		.
	defb 000h		;1f84	00		.
	defb 008h		;1f85	08		.
	defb 0c6h		;1f86	c6		.
	defb 000h		;1f87	00		.
	defb 008h		;1f88	08		.
	defb 0c6h		;1f89	c6		.
	defb 000h		;1f8a	00		.
	defb 000h		;1f8b	00		.
	defb 000h		;1f8c	00		.
	defb 000h		;1f8d	00		.
	defb 000h		;1f8e	00		.
	defb 018h		;1f8f	18		.
	defb 088h		;1f90	88		.
	defb 0c6h		;1f91	c6		.
	defb 012h		;1f92	12		.
	defb 088h		;1f93	88		.
	defb 040h		;1f94	40		@
	defb 018h		;1f95	18		.
	defb 088h		;1f96	88		.
	defb 0c6h		;1f97	c6		.
	defb 012h		;1f98	12		.
	defb 088h		;1f99	88		.
	defb 040h		;1f9a	40		@
	defb 001h		;1f9b	01		.
	defb 015h		;1f9c	15		.
	defb 000h		;1f9d	00		.
	defb 001h		;1f9e	01		.
	defb 015h		;1f9f	15		.
	defb 000h		;1fa0	00		.
	defb 008h		;1fa1	08		.
	defb 0c6h		;1fa2	c6		.
	defb 000h		;1fa3	00		.
	defb 008h		;1fa4	08		.
	defb 0c6h		;1fa5	c6		.
	defb 000h		;1fa6	00		.
	defb 000h		;1fa7	00		.
	defb 000h		;1fa8	00		.
	defb 000h		;1fa9	00		.
	defb 000h		;1faa	00		.
	defb 018h		;1fab	18		.
	defb 088h		;1fac	88		.
	defb 0c6h		;1fad	c6		.
	defb 012h		;1fae	12		.
	defb 088h		;1faf	88		.
	defb 040h		;1fb0	40		@
	defb 018h		;1fb1	18		.
	defb 088h		;1fb2	88		.
	defb 0c6h		;1fb3	c6		.
	defb 012h		;1fb4	12		.
	defb 088h		;1fb5	88		.
	defb 040h		;1fb6	40		@
	defb 001h		;1fb7	01		.
	defb 015h		;1fb8	15		.
	defb 000h		;1fb9	00		.
	defb 001h		;1fba	01		.
	defb 015h		;1fbb	15		.
	defb 000h		;1fbc	00		.
	defb 008h		;1fbd	08		.
	defb 0c6h		;1fbe	c6		.
	defb 000h		;1fbf	00		.
	defb 008h		;1fc0	08		.
	defb 0c6h		;1fc1	c6		.
	defb 000h		;1fc2	00		.
	defb 000h		;1fc3	00		.
	defb 000h		;1fc4	00		.
	defb 000h		;1fc5	00		.
	defb 000h		;1fc6	00		.
	defb 018h		;1fc7	18		.
	defb 088h		;1fc8	88		.
	defb 0c6h		;1fc9	c6		.
	defb 012h		;1fca	12		.
	defb 088h		;1fcb	88		.
	defb 040h		;1fcc	40		@
	defb 018h		;1fcd	18		.
	defb 088h		;1fce	88		.
	defb 0c6h		;1fcf	c6		.
	defb 016h		;1fd0	16		.
	defb 088h		;1fd1	88		.
	defb 052h		;1fd2	52		R
	defb 040h		;1fd3	40		@
	defb 001h		;1fd4	01		.
	defb 010h		;1fd5	10		.
	defb 060h		;1fd6	60		`
	defb 0c4h		;1fd7	c4		.
	defb 0e8h		;1fd8	e8		.
	defb 001h		;1fd9	01		.
	defb 010h		;1fda	10		.
	defb 060h		;1fdb	60		`
	defb 0c4h		;1fdc	c4		.
	defb 0e8h		;1fdd	e8		.
	defb 009h		;1fde	09		.
	defb 0c6h		;1fdf	c6		.
	defb 011h		;1fe0	11		.
	defb 020h		;1fe1	20		 
	defb 0e9h		;1fe2	e9		.
	defb 009h		;1fe3	09		.
	defb 0c6h		;1fe4	c6		.
	defb 011h		;1fe5	11		.
	defb 020h		;1fe6	20		 
	defb 0e9h		;1fe7	e9		.
	defb 0fdh		;1fe8	fd		.
	defb 020h		;1fe9	20		 
	defb 021h		;1fea	21		!
	defb 022h		;1feb	22		"
	defb 023h		;1fec	23		#
	defb 024h		;1fed	24		$
	defb 025h		;1fee	25		%
	defb 012h		;1fef	12		.
	defb 060h		;1ff0	60		`
	defb 0c5h		;1ff1	c5		.
	defb 0eah		;1ff2	ea		.
	defb 0fdh		;1ff3	fd		.
	defb 020h		;1ff4	20		 
	defb 021h		;1ff5	21		!
	defb 022h		;1ff6	22		"
	defb 023h		;1ff7	23		#
	defb 024h		;1ff8	24		$
	defb 025h		;1ff9	25		%
	defb 012h		;1ffa	12		.
	defb 060h		;1ffb	60		`
	defb 0c5h		;1ffc	c5		.
l1ffdh:
	defb 0eah		;1ffd	ea		.
	defb 009h		;1ffe	09		.
	defb 0c6h		;1fff	c6		.
	defb 013h		;2000	13		.
	defb 027h		;2001	27		'
	defb 0f0h		;2002	f0		.
	defb 0c1h		;2003	c1		.
	defb 0c2h		;2004	c2		.
	defb 014h		;2005	14		.
	defb 009h		;2006	09		.
	defb 0c6h		;2007	c6		.
	defb 013h		;2008	13		.
	defb 027h		;2009	27		'
	defb 0f0h		;200a	f0		.
	defb 0c1h		;200b	c1		.
	defb 0c2h		;200c	c2		.
	defb 014h		;200d	14		.
	defb 001h		;200e	01		.
	defb 010h		;200f	10		.
	defb 060h		;2010	60		`
	defb 0c4h		;2011	c4		.
	defb 0e8h		;2012	e8		.
	defb 001h		;2013	01		.
	defb 010h		;2014	10		.
	defb 060h		;2015	60		`
	defb 0c4h		;2016	c4		.
	defb 0e8h		;2017	e8		.
	defb 009h		;2018	09		.
	defb 0c6h		;2019	c6		.
	defb 011h		;201a	11		.
	defb 020h		;201b	20		 
	defb 0e9h		;201c	e9		.
	defb 009h		;201d	09		.
	defb 0c6h		;201e	c6		.
	defb 011h		;201f	11		.
	defb 020h		;2020	20		 
	defb 0e9h		;2021	e9		.
	defb 0bdh		;2022	bd		.
	defb 020h		;2023	20		 
	defb 026h		;2024	26		&
	defb 027h		;2025	27		'
	defb 028h		;2026	28		(
	defb 029h		;2027	29		)
	defb 012h		;2028	12		.
	defb 060h		;2029	60		`
	defb 0c5h		;202a	c5		.
	defb 0eah		;202b	ea		.
	defb 0bdh		;202c	bd		.
	defb 020h		;202d	20		 
	defb 026h		;202e	26		&
	defb 027h		;202f	27		'
	defb 028h		;2030	28		(
	defb 029h		;2031	29		)
	defb 012h		;2032	12		.
	defb 060h		;2033	60		`
	defb 0c5h		;2034	c5		.
	defb 0eah		;2035	ea		.
	defb 009h		;2036	09		.
	defb 0c6h		;2037	c6		.
	defb 013h		;2038	13		.
	defb 027h		;2039	27		'
	defb 0f0h		;203a	f0		.
	defb 0c1h		;203b	c1		.
	defb 0c2h		;203c	c2		.
	defb 014h		;203d	14		.
	defb 009h		;203e	09		.
	defb 0c6h		;203f	c6		.
	defb 013h		;2040	13		.
	defb 027h		;2041	27		'
	defb 0f0h		;2042	f0		.
	defb 0c1h		;2043	c1		.
	defb 0c2h		;2044	c2		.
	defb 014h		;2045	14		.
	defb 001h		;2046	01		.
	defb 010h		;2047	10		.
	defb 060h		;2048	60		`
	defb 0c4h		;2049	c4		.
	defb 0e8h		;204a	e8		.
	defb 001h		;204b	01		.
	defb 010h		;204c	10		.
	defb 060h		;204d	60		`
l204eh:
	defb 0c4h		;204e	c4		.
	defb 0e8h		;204f	e8		.
l2050h:
	defb 009h		;2050	09		.
	defb 0c6h		;2051	c6		.
	defb 011h		;2052	11		.
	defb 024h		;2053	24		$
	defb 0e9h		;2054	e9		.
	defb 018h		;2055	18		.
	defb 009h		;2056	09		.
	defb 0c6h		;2057	c6		.
	defb 011h		;2058	11		.
	defb 024h		;2059	24		$
	defb 0e9h		;205a	e9		.
	defb 019h		;205b	19		.
	defb 081h		;205c	81		.
	defb 020h		;205d	20		 
	defb 012h		;205e	12		.
	defb 060h		;205f	60		`
	defb 0c5h		;2060	c5		.
	defb 0eah		;2061	ea		.
	defb 081h		;2062	81		.
	defb 020h		;2063	20		 
	defb 012h		;2064	12		.
	defb 060h		;2065	60		`
	defb 0c5h		;2066	c5		.
	defb 0eah		;2067	ea		.
	defb 009h		;2068	09		.
	defb 0c6h		;2069	c6		.
	defb 013h		;206a	13		.
	defb 023h		;206b	23		#
	defb 0f0h		;206c	f0		.
	defb 0c2h		;206d	c2		.
	defb 014h		;206e	14		.
	defb 009h		;206f	09		.
	defb 0c6h		;2070	c6		.
	defb 013h		;2071	13		.
	defb 027h		;2072	27		'
	defb 0f0h		;2073	f0		.
	defb 091h		;2074	91		.
	defb 0c2h		;2075	c2		.
	defb 014h		;2076	14		.
	defb 001h		;2077	01		.
	defb 010h		;2078	10		.
	defb 060h		;2079	60		`
	defb 0c4h		;207a	c4		.
	defb 0e8h		;207b	e8		.
	defb 001h		;207c	01		.
	defb 010h		;207d	10		.
	defb 060h		;207e	60		`
	defb 0c4h		;207f	c4		.
	defb 0e8h		;2080	e8		.
	defb 009h		;2081	09		.
	defb 0c6h		;2082	c6		.
	defb 011h		;2083	11		.
	defb 024h		;2084	24		$
	defb 0e9h		;2085	e9		.
	defb 01ah		;2086	1a		.
	defb 009h		;2087	09		.
	defb 0c6h		;2088	c6		.
	defb 011h		;2089	11		.
	defb 024h		;208a	24		$
	defb 0e9h		;208b	e9		.
	defb 01bh		;208c	1b		.
	defb 081h		;208d	81		.
	defb 020h		;208e	20		 
	defb 012h		;208f	12		.
	defb 060h		;2090	60		`
	defb 0c5h		;2091	c5		.
	defb 0eah		;2092	ea		.
	defb 081h		;2093	81		.
	defb 020h		;2094	20		 
	defb 012h		;2095	12		.
	defb 060h		;2096	60		`
	defb 0c5h		;2097	c5		.
	defb 0eah		;2098	ea		.
	defb 009h		;2099	09		.
	defb 0c6h		;209a	c6		.
	defb 013h		;209b	13		.
	defb 023h		;209c	23		#
	defb 0f0h		;209d	f0		.
	defb 0c2h		;209e	c2		.
	defb 014h		;209f	14		.
	defb 009h		;20a0	09		.
	defb 0c6h		;20a1	c6		.
	defb 013h		;20a2	13		.
	defb 027h		;20a3	27		'
	defb 0f0h		;20a4	f0		.
	defb 090h		;20a5	90		.
	defb 0c2h		;20a6	c2		.
	defb 014h		;20a7	14		.
	defb 001h		;20a8	01		.
	defb 010h		;20a9	10		.
	defb 060h		;20aa	60		`
	defb 0c4h		;20ab	c4		.
	defb 0e8h		;20ac	e8		.
	defb 001h		;20ad	01		.
	defb 010h		;20ae	10		.
	defb 060h		;20af	60		`
	defb 0c4h		;20b0	c4		.
	defb 0e8h		;20b1	e8		.
	defb 009h		;20b2	09		.
	defb 0c6h		;20b3	c6		.
	defb 011h		;20b4	11		.
	defb 020h		;20b5	20		 
	defb 0e9h		;20b6	e9		.
	defb 009h		;20b7	09		.
	defb 0c6h		;20b8	c6		.
	defb 011h		;20b9	11		.
	defb 020h		;20ba	20		 
	defb 0e9h		;20bb	e9		.
	defb 0fdh		;20bc	fd		.
	defb 020h		;20bd	20		 
	defb 021h		;20be	21		!
	defb 022h		;20bf	22		"
	defb 023h		;20c0	23		#
	defb 024h		;20c1	24		$
	defb 025h		;20c2	25		%
	defb 012h		;20c3	12		.
	defb 060h		;20c4	60		`
	defb 0c5h		;20c5	c5		.
	defb 0eah		;20c6	ea		.
	defb 0fdh		;20c7	fd		.
	defb 020h		;20c8	20		 
	defb 021h		;20c9	21		!
	defb 022h		;20ca	22		"
	defb 023h		;20cb	23		#
	defb 024h		;20cc	24		$
	defb 025h		;20cd	25		%
	defb 012h		;20ce	12		.
	defb 060h		;20cf	60		`
	defb 0c5h		;20d0	c5		.
	defb 0eah		;20d1	ea		.
	defb 009h		;20d2	09		.
	defb 0c6h		;20d3	c6		.
	defb 013h		;20d4	13		.
	defb 025h		;20d5	25		%
	defb 0f0h		;20d6	f0		.
	defb 0c1h		;20d7	c1		.
	defb 015h		;20d8	15		.
	defb 009h		;20d9	09		.
	defb 0c6h		;20da	c6		.
	defb 013h		;20db	13		.
	defb 025h		;20dc	25		%
	defb 0f0h		;20dd	f0		.
	defb 0c1h		;20de	c1		.
	defb 015h		;20df	15		.
	defb 001h		;20e0	01		.
	defb 010h		;20e1	10		.
	defb 060h		;20e2	60		`
	defb 0c4h		;20e3	c4		.
	defb 0e8h		;20e4	e8		.
	defb 001h		;20e5	01		.
	defb 010h		;20e6	10		.
	defb 060h		;20e7	60		`
	defb 0c4h		;20e8	c4		.
	defb 0e8h		;20e9	e8		.
	defb 009h		;20ea	09		.
	defb 0c6h		;20eb	c6		.
	defb 011h		;20ec	11		.
	defb 020h		;20ed	20		 
	defb 0e9h		;20ee	e9		.
	defb 009h		;20ef	09		.
	defb 0c6h		;20f0	c6		.
	defb 011h		;20f1	11		.
	defb 020h		;20f2	20		 
	defb 0e9h		;20f3	e9		.
	defb 0bdh		;20f4	bd		.
	defb 020h		;20f5	20		 
	defb 026h		;20f6	26		&
	defb 027h		;20f7	27		'
	defb 028h		;20f8	28		(
	defb 029h		;20f9	29		)
	defb 012h		;20fa	12		.
	defb 060h		;20fb	60		`
	defb 0c5h		;20fc	c5		.
	defb 0eah		;20fd	ea		.
	defb 0bdh		;20fe	bd		.
	defb 020h		;20ff	20		 
	defb 026h		;2100	26		&
	defb 027h		;2101	27		'
	defb 028h		;2102	28		(
	defb 029h		;2103	29		)
	defb 012h		;2104	12		.
	defb 060h		;2105	60		`
	defb 0c5h		;2106	c5		.
	defb 0eah		;2107	ea		.
	defb 009h		;2108	09		.
	defb 0c6h		;2109	c6		.
	defb 013h		;210a	13		.
	defb 025h		;210b	25		%
	defb 0f0h		;210c	f0		.
	defb 0c1h		;210d	c1		.
	defb 015h		;210e	15		.
	defb 009h		;210f	09		.
	defb 0c6h		;2110	c6		.
	defb 013h		;2111	13		.
	defb 025h		;2112	25		%
	defb 0f0h		;2113	f0		.
	defb 0c1h		;2114	c1		.
	defb 015h		;2115	15		.
	defb 001h		;2116	01		.
	defb 010h		;2117	10		.
	defb 060h		;2118	60		`
	defb 0c4h		;2119	c4		.
	defb 0e8h		;211a	e8		.
	defb 001h		;211b	01		.
	defb 010h		;211c	10		.
	defb 060h		;211d	60		`
	defb 0c4h		;211e	c4		.
	defb 0e8h		;211f	e8		.
	defb 009h		;2120	09		.
	defb 0c6h		;2121	c6		.
	defb 011h		;2122	11		.
	defb 024h		;2123	24		$
	defb 0e9h		;2124	e9		.
	defb 018h		;2125	18		.
	defb 009h		;2126	09		.
	defb 0c6h		;2127	c6		.
	defb 011h		;2128	11		.
	defb 024h		;2129	24		$
	defb 0e9h		;212a	e9		.
	defb 019h		;212b	19		.
	defb 081h		;212c	81		.
	defb 020h		;212d	20		 
	defb 012h		;212e	12		.
	defb 060h		;212f	60		`
	defb 0c5h		;2130	c5		.
	defb 0eah		;2131	ea		.
	defb 081h		;2132	81		.
	defb 020h		;2133	20		 
	defb 012h		;2134	12		.
	defb 060h		;2135	60		`
	defb 0c5h		;2136	c5		.
	defb 0eah		;2137	ea		.
	defb 009h		;2138	09		.
	defb 0c6h		;2139	c6		.
	defb 013h		;213a	13		.
	defb 021h		;213b	21		!
	defb 0f0h		;213c	f0		.
	defb 015h		;213d	15		.
	defb 009h		;213e	09		.
	defb 0c6h		;213f	c6		.
	defb 013h		;2140	13		.
	defb 021h		;2141	21		!
	defb 0f0h		;2142	f0		.
	defb 015h		;2143	15		.
	defb 001h		;2144	01		.
	defb 010h		;2145	10		.
	defb 060h		;2146	60		`
	defb 0c4h		;2147	c4		.
	defb 0e8h		;2148	e8		.
	defb 001h		;2149	01		.
	defb 010h		;214a	10		.
	defb 060h		;214b	60		`
	defb 0c4h		;214c	c4		.
	defb 0e8h		;214d	e8		.
	defb 009h		;214e	09		.
	defb 0c6h		;214f	c6		.
	defb 011h		;2150	11		.
	defb 024h		;2151	24		$
	defb 0e9h		;2152	e9		.
	defb 01ah		;2153	1a		.
	defb 009h		;2154	09		.
	defb 0c6h		;2155	c6		.
	defb 011h		;2156	11		.
	defb 024h		;2157	24		$
	defb 0e9h		;2158	e9		.
	defb 01bh		;2159	1b		.
	defb 081h		;215a	81		.
	defb 020h		;215b	20		 
	defb 012h		;215c	12		.
	defb 060h		;215d	60		`
	defb 0c5h		;215e	c5		.
	defb 0eah		;215f	ea		.
	defb 081h		;2160	81		.
	defb 020h		;2161	20		 
	defb 012h		;2162	12		.
	defb 060h		;2163	60		`
	defb 0c5h		;2164	c5		.
	defb 0eah		;2165	ea		.
	defb 009h		;2166	09		.
	defb 0c6h		;2167	c6		.
	defb 013h		;2168	13		.
	defb 021h		;2169	21		!
	defb 0f0h		;216a	f0		.
	defb 015h		;216b	15		.
	defb 009h		;216c	09		.
	defb 0c6h		;216d	c6		.
	defb 013h		;216e	13		.
	defb 025h		;216f	25		%
	defb 0f0h		;2170	f0		.
	defb 052h		;2171	52		R
	defb 015h		;2172	15		.
	defb 000h		;2173	00		.
	defb 000h		;2174	00		.
	defb 000h		;2175	00		.
	defb 000h		;2176	00		.
	defb 008h		;2177	08		.
	defb 0c6h		;2178	c6		.
	defb 000h		;2179	00		.
	defb 008h		;217a	08		.
	defb 0c6h		;217b	c6		.
	defb 000h		;217c	00		.
	defb 000h		;217d	00		.
	defb 000h		;217e	00		.
	defb 000h		;217f	00		.
	defb 000h		;2180	00		.
	defb 018h		;2181	18		.
	defb 088h		;2182	88		.
	defb 0c6h		;2183	c6		.
	defb 016h		;2184	16		.
	defb 088h		;2185	88		.
	defb 0c1h		;2186	c1		.
	defb 0c2h		;2187	c2		.
	defb 018h		;2188	18		.
	defb 088h		;2189	88		.
	defb 0c6h		;218a	c6		.
	defb 016h		;218b	16		.
	defb 088h		;218c	88		.
	defb 0c1h		;218d	c1		.
	defb 0c2h		;218e	c2		.
	defb 000h		;218f	00		.
	defb 000h		;2190	00		.
	defb 000h		;2191	00		.
	defb 000h		;2192	00		.
	defb 008h		;2193	08		.
	defb 0c6h		;2194	c6		.
	defb 000h		;2195	00		.
	defb 008h		;2196	08		.
	defb 0c6h		;2197	c6		.
	defb 000h		;2198	00		.
	defb 000h		;2199	00		.
	defb 000h		;219a	00		.
	defb 000h		;219b	00		.
	defb 000h		;219c	00		.
	defb 018h		;219d	18		.
	defb 088h		;219e	88		.
	defb 0c6h		;219f	c6		.
	defb 016h		;21a0	16		.
	defb 088h		;21a1	88		.
	defb 0c1h		;21a2	c1		.
	defb 0c2h		;21a3	c2		.
	defb 018h		;21a4	18		.
	defb 088h		;21a5	88		.
	defb 0c6h		;21a6	c6		.
	defb 016h		;21a7	16		.
	defb 088h		;21a8	88		.
	defb 0c1h		;21a9	c1		.
	defb 0c2h		;21aa	c2		.
	defb 000h		;21ab	00		.
	defb 000h		;21ac	00		.
	defb 000h		;21ad	00		.
	defb 000h		;21ae	00		.
	defb 008h		;21af	08		.
	defb 0c6h		;21b0	c6		.
	defb 000h		;21b1	00		.
	defb 008h		;21b2	08		.
	defb 0c6h		;21b3	c6		.
	defb 000h		;21b4	00		.
	defb 000h		;21b5	00		.
	defb 000h		;21b6	00		.
	defb 000h		;21b7	00		.
	defb 000h		;21b8	00		.
	defb 018h		;21b9	18		.
	defb 088h		;21ba	88		.
	defb 0c6h		;21bb	c6		.
	defb 012h		;21bc	12		.
	defb 088h		;21bd	88		.
	defb 0c2h		;21be	c2		.
	defb 018h		;21bf	18		.
	defb 088h		;21c0	88		.
	defb 0c6h		;21c1	c6		.
	defb 016h		;21c2	16		.
	defb 088h		;21c3	88		.
	defb 091h		;21c4	91		.
	defb 0c2h		;21c5	c2		.
	defb 000h		;21c6	00		.
	defb 000h		;21c7	00		.
	defb 000h		;21c8	00		.
	defb 000h		;21c9	00		.
	defb 008h		;21ca	08		.
	defb 0c6h		;21cb	c6		.
	defb 000h		;21cc	00		.
	defb 008h		;21cd	08		.
	defb 0c6h		;21ce	c6		.
	defb 000h		;21cf	00		.
	defb 000h		;21d0	00		.
	defb 000h		;21d1	00		.
	defb 000h		;21d2	00		.
	defb 000h		;21d3	00		.
	defb 018h		;21d4	18		.
	defb 088h		;21d5	88		.
	defb 0c6h		;21d6	c6		.
	defb 012h		;21d7	12		.
	defb 088h		;21d8	88		.
	defb 0c2h		;21d9	c2		.
	defb 018h		;21da	18		.
	defb 088h		;21db	88		.
	defb 0c6h		;21dc	c6		.
	defb 016h		;21dd	16		.
	defb 088h		;21de	88		.
	defb 090h		;21df	90		.
	defb 0c2h		;21e0	c2		.
	defb 001h		;21e1	01		.
	defb 015h		;21e2	15		.
	defb 000h		;21e3	00		.
	defb 001h		;21e4	01		.
	defb 015h		;21e5	15		.
	defb 000h		;21e6	00		.
	defb 008h		;21e7	08		.
	defb 0c6h		;21e8	c6		.
	defb 000h		;21e9	00		.
	defb 008h		;21ea	08		.
	defb 0c6h		;21eb	c6		.
	defb 000h		;21ec	00		.
	defb 000h		;21ed	00		.
	defb 000h		;21ee	00		.
	defb 000h		;21ef	00		.
	defb 000h		;21f0	00		.
	defb 018h		;21f1	18		.
	defb 088h		;21f2	88		.
	defb 0c6h		;21f3	c6		.
	defb 016h		;21f4	16		.
	defb 088h		;21f5	88		.
	defb 0c1h		;21f6	c1		.
	defb 040h		;21f7	40		@
	defb 018h		;21f8	18		.
	defb 088h		;21f9	88		.
	defb 0c6h		;21fa	c6		.
	defb 016h		;21fb	16		.
	defb 088h		;21fc	88		.
	defb 0c1h		;21fd	c1		.
	defb 040h		;21fe	40		@
	defb 001h		;21ff	01		.
	defb 015h		;2200	15		.
	defb 000h		;2201	00		.
	defb 001h		;2202	01		.
	defb 015h		;2203	15		.
	defb 000h		;2204	00		.
	defb 008h		;2205	08		.
	defb 0c6h		;2206	c6		.
	defb 000h		;2207	00		.
	defb 008h		;2208	08		.
	defb 0c6h		;2209	c6		.
	defb 000h		;220a	00		.
	defb 000h		;220b	00		.
	defb 000h		;220c	00		.
	defb 000h		;220d	00		.
	defb 000h		;220e	00		.
	defb 018h		;220f	18		.
	defb 088h		;2210	88		.
	defb 0c6h		;2211	c6		.
	defb 016h		;2212	16		.
	defb 088h		;2213	88		.
	defb 0c1h		;2214	c1		.
	defb 040h		;2215	40		@
	defb 018h		;2216	18		.
	defb 088h		;2217	88		.
	defb 0c6h		;2218	c6		.
	defb 016h		;2219	16		.
	defb 088h		;221a	88		.
	defb 0c1h		;221b	c1		.
	defb 040h		;221c	40		@
	defb 001h		;221d	01		.
	defb 015h		;221e	15		.
	defb 000h		;221f	00		.
	defb 001h		;2220	01		.
	defb 015h		;2221	15		.
	defb 000h		;2222	00		.
	defb 008h		;2223	08		.
	defb 0c6h		;2224	c6		.
	defb 000h		;2225	00		.
	defb 008h		;2226	08		.
	defb 0c6h		;2227	c6		.
	defb 000h		;2228	00		.
	defb 000h		;2229	00		.
	defb 000h		;222a	00		.
	defb 000h		;222b	00		.
	defb 000h		;222c	00		.
	defb 018h		;222d	18		.
	defb 088h		;222e	88		.
	defb 0c6h		;222f	c6		.
	defb 012h		;2230	12		.
	defb 088h		;2231	88		.
	defb 040h		;2232	40		@
	defb 018h		;2233	18		.
	defb 088h		;2234	88		.
	defb 0c6h		;2235	c6		.
	defb 012h		;2236	12		.
	defb 088h		;2237	88		.
	defb 040h		;2238	40		@
	defb 001h		;2239	01		.
	defb 015h		;223a	15		.
	defb 000h		;223b	00		.
	defb 001h		;223c	01		.
	defb 015h		;223d	15		.
	defb 000h		;223e	00		.
	defb 008h		;223f	08		.
	defb 0c6h		;2240	c6		.
	defb 000h		;2241	00		.
	defb 008h		;2242	08		.
	defb 0c6h		;2243	c6		.
	defb 000h		;2244	00		.
	defb 000h		;2245	00		.
	defb 000h		;2246	00		.
	defb 000h		;2247	00		.
	defb 000h		;2248	00		.
	defb 018h		;2249	18		.
	defb 088h		;224a	88		.
	defb 0c6h		;224b	c6		.
	defb 012h		;224c	12		.
	defb 088h		;224d	88		.
	defb 040h		;224e	40		@
	defb 018h		;224f	18		.
	defb 088h		;2250	88		.
	defb 0c6h		;2251	c6		.
	defb 016h		;2252	16		.
	defb 088h		;2253	88		.
	defb 052h		;2254	52		R
	defb 040h		;2255	40		@
	defb 001h		;2256	01		.
	defb 010h		;2257	10		.
	defb 060h		;2258	60		`
	defb 0c4h		;2259	c4		.
	defb 0e8h		;225a	e8		.
	defb 001h		;225b	01		.
	defb 010h		;225c	10		.
	defb 060h		;225d	60		`
	defb 0c4h		;225e	c4		.
	defb 0e8h		;225f	e8		.
	defb 009h		;2260	09		.
	defb 0c6h		;2261	c6		.
	defb 011h		;2262	11		.
	defb 020h		;2263	20		 
	defb 0e9h		;2264	e9		.
	defb 009h		;2265	09		.
	defb 0c6h		;2266	c6		.
	defb 011h		;2267	11		.
	defb 020h		;2268	20		 
	defb 0e9h		;2269	e9		.
	defb 0fdh		;226a	fd		.
	defb 020h		;226b	20		 
	defb 021h		;226c	21		!
	defb 022h		;226d	22		"
	defb 023h		;226e	23		#
	defb 024h		;226f	24		$
	defb 025h		;2270	25		%
	defb 012h		;2271	12		.
	defb 060h		;2272	60		`
	defb 0c5h		;2273	c5		.
	defb 0eah		;2274	ea		.
	defb 0fdh		;2275	fd		.
	defb 020h		;2276	20		 
	defb 021h		;2277	21		!
	defb 022h		;2278	22		"
	defb 023h		;2279	23		#
	defb 024h		;227a	24		$
	defb 025h		;227b	25		%
	defb 012h		;227c	12		.
	defb 060h		;227d	60		`
	defb 0c5h		;227e	c5		.
	defb 0eah		;227f	ea		.
	defb 009h		;2280	09		.
	defb 0c6h		;2281	c6		.
	defb 013h		;2282	13		.
	defb 023h		;2283	23		#
	defb 0f0h		;2284	f0		.
	defb 0c2h		;2285	c2		.
	defb 014h		;2286	14		.
	defb 009h		;2287	09		.
	defb 0c6h		;2288	c6		.
	defb 013h		;2289	13		.
	defb 023h		;228a	23		#
	defb 0f0h		;228b	f0		.
	defb 0c2h		;228c	c2		.
	defb 014h		;228d	14		.
	defb 001h		;228e	01		.
	defb 010h		;228f	10		.
	defb 060h		;2290	60		`
	defb 0c4h		;2291	c4		.
	defb 0e8h		;2292	e8		.
	defb 001h		;2293	01		.
	defb 010h		;2294	10		.
	defb 060h		;2295	60		`
	defb 0c4h		;2296	c4		.
	defb 0e8h		;2297	e8		.
	defb 009h		;2298	09		.
	defb 0c6h		;2299	c6		.
	defb 011h		;229a	11		.
	defb 020h		;229b	20		 
	defb 0e9h		;229c	e9		.
	defb 009h		;229d	09		.
	defb 0c6h		;229e	c6		.
	defb 011h		;229f	11		.
	defb 020h		;22a0	20		 
	defb 0e9h		;22a1	e9		.
	defb 0bdh		;22a2	bd		.
	defb 020h		;22a3	20		 
	defb 026h		;22a4	26		&
	defb 027h		;22a5	27		'
	defb 028h		;22a6	28		(
	defb 029h		;22a7	29		)
	defb 012h		;22a8	12		.
	defb 060h		;22a9	60		`
	defb 0c5h		;22aa	c5		.
	defb 0eah		;22ab	ea		.
	defb 0bdh		;22ac	bd		.
	defb 020h		;22ad	20		 
	defb 026h		;22ae	26		&
	defb 027h		;22af	27		'
	defb 028h		;22b0	28		(
	defb 029h		;22b1	29		)
	defb 012h		;22b2	12		.
	defb 060h		;22b3	60		`
	defb 0c5h		;22b4	c5		.
	defb 0eah		;22b5	ea		.
	defb 009h		;22b6	09		.
	defb 0c6h		;22b7	c6		.
	defb 013h		;22b8	13		.
	defb 023h		;22b9	23		#
	defb 0f0h		;22ba	f0		.
	defb 0c2h		;22bb	c2		.
	defb 014h		;22bc	14		.
	defb 009h		;22bd	09		.
	defb 0c6h		;22be	c6		.
	defb 013h		;22bf	13		.
	defb 023h		;22c0	23		#
	defb 0f0h		;22c1	f0		.
	defb 0c2h		;22c2	c2		.
	defb 014h		;22c3	14		.
	defb 001h		;22c4	01		.
	defb 010h		;22c5	10		.
	defb 060h		;22c6	60		`
	defb 0c4h		;22c7	c4		.
	defb 0e8h		;22c8	e8		.
	defb 001h		;22c9	01		.
	defb 010h		;22ca	10		.
	defb 060h		;22cb	60		`
	defb 0c4h		;22cc	c4		.
	defb 0e8h		;22cd	e8		.
	defb 009h		;22ce	09		.
	defb 0c6h		;22cf	c6		.
	defb 011h		;22d0	11		.
	defb 024h		;22d1	24		$
	defb 0e9h		;22d2	e9		.
	defb 018h		;22d3	18		.
	defb 009h		;22d4	09		.
	defb 0c6h		;22d5	c6		.
	defb 011h		;22d6	11		.
	defb 024h		;22d7	24		$
	defb 0e9h		;22d8	e9		.
	defb 019h		;22d9	19		.
	defb 081h		;22da	81		.
	defb 020h		;22db	20		 
	defb 012h		;22dc	12		.
	defb 060h		;22dd	60		`
	defb 0c5h		;22de	c5		.
	defb 0eah		;22df	ea		.
	defb 081h		;22e0	81		.
	defb 020h		;22e1	20		 
	defb 012h		;22e2	12		.
	defb 060h		;22e3	60		`
	defb 0c5h		;22e4	c5		.
	defb 0eah		;22e5	ea		.
	defb 009h		;22e6	09		.
	defb 0c6h		;22e7	c6		.
	defb 013h		;22e8	13		.
	defb 023h		;22e9	23		#
	defb 0f0h		;22ea	f0		.
	defb 0c2h		;22eb	c2		.
	defb 014h		;22ec	14		.
	defb 009h		;22ed	09		.
	defb 0c6h		;22ee	c6		.
	defb 013h		;22ef	13		.
	defb 027h		;22f0	27		'
	defb 0f0h		;22f1	f0		.
	defb 091h		;22f2	91		.
	defb 0c2h		;22f3	c2		.
	defb 014h		;22f4	14		.
	defb 001h		;22f5	01		.
	defb 010h		;22f6	10		.
	defb 060h		;22f7	60		`
	defb 0c4h		;22f8	c4		.
	defb 0e8h		;22f9	e8		.
	defb 001h		;22fa	01		.
	defb 010h		;22fb	10		.
	defb 060h		;22fc	60		`
	defb 0c4h		;22fd	c4		.
	defb 0e8h		;22fe	e8		.
	defb 009h		;22ff	09		.
	defb 0c6h		;2300	c6		.
	defb 011h		;2301	11		.
	defb 024h		;2302	24		$
	defb 0e9h		;2303	e9		.
	defb 01ah		;2304	1a		.
	defb 009h		;2305	09		.
	defb 0c6h		;2306	c6		.
	defb 011h		;2307	11		.
	defb 024h		;2308	24		$
	defb 0e9h		;2309	e9		.
	defb 01bh		;230a	1b		.
	defb 081h		;230b	81		.
	defb 020h		;230c	20		 
	defb 012h		;230d	12		.
	defb 060h		;230e	60		`
	defb 0c5h		;230f	c5		.
	defb 0eah		;2310	ea		.
	defb 081h		;2311	81		.
	defb 020h		;2312	20		 
	defb 012h		;2313	12		.
	defb 060h		;2314	60		`
	defb 0c5h		;2315	c5		.
	defb 0eah		;2316	ea		.
	defb 009h		;2317	09		.
	defb 0c6h		;2318	c6		.
	defb 013h		;2319	13		.
	defb 023h		;231a	23		#
	defb 0f0h		;231b	f0		.
	defb 0c2h		;231c	c2		.
	defb 014h		;231d	14		.
	defb 009h		;231e	09		.
	defb 0c6h		;231f	c6		.
	defb 013h		;2320	13		.
	defb 027h		;2321	27		'
	defb 0f0h		;2322	f0		.
	defb 090h		;2323	90		.
	defb 0c2h		;2324	c2		.
	defb 014h		;2325	14		.
	defb 001h		;2326	01		.
	defb 010h		;2327	10		.
	defb 060h		;2328	60		`
	defb 0c4h		;2329	c4		.
	defb 0e8h		;232a	e8		.
	defb 001h		;232b	01		.
	defb 010h		;232c	10		.
	defb 060h		;232d	60		`
	defb 0c4h		;232e	c4		.
	defb 0e8h		;232f	e8		.
	defb 009h		;2330	09		.
	defb 0c6h		;2331	c6		.
	defb 011h		;2332	11		.
	defb 020h		;2333	20		 
	defb 0e9h		;2334	e9		.
	defb 009h		;2335	09		.
	defb 0c6h		;2336	c6		.
	defb 011h		;2337	11		.
	defb 020h		;2338	20		 
	defb 0e9h		;2339	e9		.
	defb 0fdh		;233a	fd		.
	defb 020h		;233b	20		 
	defb 021h		;233c	21		!
	defb 022h		;233d	22		"
	defb 023h		;233e	23		#
	defb 024h		;233f	24		$
	defb 025h		;2340	25		%
	defb 012h		;2341	12		.
	defb 060h		;2342	60		`
	defb 0c5h		;2343	c5		.
	defb 0eah		;2344	ea		.
	defb 0fdh		;2345	fd		.
	defb 020h		;2346	20		 
	defb 021h		;2347	21		!
	defb 022h		;2348	22		"
	defb 023h		;2349	23		#
	defb 024h		;234a	24		$
	defb 025h		;234b	25		%
	defb 012h		;234c	12		.
	defb 060h		;234d	60		`
	defb 0c5h		;234e	c5		.
	defb 0eah		;234f	ea		.
	defb 009h		;2350	09		.
	defb 0c6h		;2351	c6		.
	defb 013h		;2352	13		.
	defb 021h		;2353	21		!
	defb 0f0h		;2354	f0		.
	defb 015h		;2355	15		.
	defb 009h		;2356	09		.
	defb 0c6h		;2357	c6		.
	defb 013h		;2358	13		.
	defb 021h		;2359	21		!
	defb 0f0h		;235a	f0		.
	defb 015h		;235b	15		.
	defb 001h		;235c	01		.
	defb 010h		;235d	10		.
	defb 060h		;235e	60		`
	defb 0c4h		;235f	c4		.
	defb 0e8h		;2360	e8		.
	defb 001h		;2361	01		.
	defb 010h		;2362	10		.
	defb 060h		;2363	60		`
	defb 0c4h		;2364	c4		.
	defb 0e8h		;2365	e8		.
	defb 009h		;2366	09		.
	defb 0c6h		;2367	c6		.
	defb 011h		;2368	11		.
	defb 020h		;2369	20		 
	defb 0e9h		;236a	e9		.
	defb 009h		;236b	09		.
	defb 0c6h		;236c	c6		.
	defb 011h		;236d	11		.
	defb 020h		;236e	20		 
	defb 0e9h		;236f	e9		.
	defb 0bdh		;2370	bd		.
	defb 020h		;2371	20		 
	defb 026h		;2372	26		&
	defb 027h		;2373	27		'
	defb 028h		;2374	28		(
	defb 029h		;2375	29		)
	defb 012h		;2376	12		.
	defb 060h		;2377	60		`
	defb 0c5h		;2378	c5		.
	defb 0eah		;2379	ea		.
	defb 0bdh		;237a	bd		.
	defb 020h		;237b	20		 
	defb 026h		;237c	26		&
	defb 027h		;237d	27		'
	defb 028h		;237e	28		(
	defb 029h		;237f	29		)
	defb 012h		;2380	12		.
	defb 060h		;2381	60		`
	defb 0c5h		;2382	c5		.
	defb 0eah		;2383	ea		.
	defb 009h		;2384	09		.
	defb 0c6h		;2385	c6		.
	defb 013h		;2386	13		.
	defb 021h		;2387	21		!
	defb 0f0h		;2388	f0		.
	defb 015h		;2389	15		.
	defb 009h		;238a	09		.
	defb 0c6h		;238b	c6		.
	defb 013h		;238c	13		.
	defb 021h		;238d	21		!
	defb 0f0h		;238e	f0		.
	defb 015h		;238f	15		.
	defb 001h		;2390	01		.
	defb 010h		;2391	10		.
	defb 060h		;2392	60		`
	defb 0c4h		;2393	c4		.
	defb 0e8h		;2394	e8		.
	defb 001h		;2395	01		.
	defb 010h		;2396	10		.
	defb 060h		;2397	60		`
	defb 0c4h		;2398	c4		.
	defb 0e8h		;2399	e8		.
	defb 009h		;239a	09		.
	defb 0c6h		;239b	c6		.
	defb 011h		;239c	11		.
	defb 024h		;239d	24		$
	defb 0e9h		;239e	e9		.
	defb 018h		;239f	18		.
	defb 009h		;23a0	09		.
	defb 0c6h		;23a1	c6		.
	defb 011h		;23a2	11		.
	defb 024h		;23a3	24		$
	defb 0e9h		;23a4	e9		.
	defb 019h		;23a5	19		.
	defb 081h		;23a6	81		.
	defb 020h		;23a7	20		 
	defb 012h		;23a8	12		.
	defb 060h		;23a9	60		`
	defb 0c5h		;23aa	c5		.
	defb 0eah		;23ab	ea		.
	defb 081h		;23ac	81		.
	defb 020h		;23ad	20		 
	defb 012h		;23ae	12		.
	defb 060h		;23af	60		`
	defb 0c5h		;23b0	c5		.
	defb 0eah		;23b1	ea		.
	defb 009h		;23b2	09		.
	defb 0c6h		;23b3	c6		.
	defb 013h		;23b4	13		.
	defb 021h		;23b5	21		!
	defb 0f0h		;23b6	f0		.
	defb 015h		;23b7	15		.
	defb 009h		;23b8	09		.
	defb 0c6h		;23b9	c6		.
	defb 013h		;23ba	13		.
	defb 021h		;23bb	21		!
	defb 0f0h		;23bc	f0		.
	defb 015h		;23bd	15		.
	defb 001h		;23be	01		.
	defb 010h		;23bf	10		.
	defb 060h		;23c0	60		`
	defb 0c4h		;23c1	c4		.
	defb 0e8h		;23c2	e8		.
	defb 001h		;23c3	01		.
	defb 010h		;23c4	10		.
	defb 060h		;23c5	60		`
	defb 0c4h		;23c6	c4		.
	defb 0e8h		;23c7	e8		.
	defb 009h		;23c8	09		.
	defb 0c6h		;23c9	c6		.
	defb 011h		;23ca	11		.
	defb 024h		;23cb	24		$
	defb 0e9h		;23cc	e9		.
	defb 01ah		;23cd	1a		.
	defb 009h		;23ce	09		.
	defb 0c6h		;23cf	c6		.
	defb 011h		;23d0	11		.
	defb 024h		;23d1	24		$
	defb 0e9h		;23d2	e9		.
	defb 01bh		;23d3	1b		.
	defb 081h		;23d4	81		.
	defb 020h		;23d5	20		 
	defb 012h		;23d6	12		.
	defb 060h		;23d7	60		`
	defb 0c5h		;23d8	c5		.
	defb 0eah		;23d9	ea		.
	defb 081h		;23da	81		.
	defb 020h		;23db	20		 
	defb 012h		;23dc	12		.
	defb 060h		;23dd	60		`
	defb 0c5h		;23de	c5		.
	defb 0eah		;23df	ea		.
	defb 009h		;23e0	09		.
	defb 0c6h		;23e1	c6		.
	defb 013h		;23e2	13		.
	defb 021h		;23e3	21		!
	defb 0f0h		;23e4	f0		.
	defb 015h		;23e5	15		.
	defb 009h		;23e6	09		.
	defb 0c6h		;23e7	c6		.
	defb 013h		;23e8	13		.
	defb 025h		;23e9	25		%
	defb 0f0h		;23ea	f0		.
	defb 052h		;23eb	52		R
	defb 015h		;23ec	15		.
	defb 000h		;23ed	00		.
	defb 000h		;23ee	00		.
	defb 000h		;23ef	00		.
	defb 000h		;23f0	00		.
	defb 008h		;23f1	08		.
	defb 0c6h		;23f2	c6		.
	defb 000h		;23f3	00		.
	defb 008h		;23f4	08		.
	defb 0c6h		;23f5	c6		.
	defb 000h		;23f6	00		.
	defb 000h		;23f7	00		.
	defb 000h		;23f8	00		.
	defb 000h		;23f9	00		.
	defb 000h		;23fa	00		.
	defb 018h		;23fb	18		.
	defb 088h		;23fc	88		.
	defb 0c6h		;23fd	c6		.
	defb 012h		;23fe	12		.
	defb 088h		;23ff	88		.
	defb 0c2h		;2400	c2		.
	defb 018h		;2401	18		.
	defb 088h		;2402	88		.
	defb 0c6h		;2403	c6		.
	defb 012h		;2404	12		.
	defb 088h		;2405	88		.
	defb 0c2h		;2406	c2		.
	defb 000h		;2407	00		.
	defb 000h		;2408	00		.
	defb 000h		;2409	00		.
	defb 000h		;240a	00		.
	defb 008h		;240b	08		.
	defb 0c6h		;240c	c6		.
	defb 000h		;240d	00		.
	defb 008h		;240e	08		.
	defb 0c6h		;240f	c6		.
	defb 000h		;2410	00		.
	defb 000h		;2411	00		.
	defb 000h		;2412	00		.
	defb 000h		;2413	00		.
	defb 000h		;2414	00		.
	defb 018h		;2415	18		.
	defb 088h		;2416	88		.
	defb 0c6h		;2417	c6		.
	defb 012h		;2418	12		.
	defb 088h		;2419	88		.
	defb 0c2h		;241a	c2		.
	defb 018h		;241b	18		.
	defb 088h		;241c	88		.
	defb 0c6h		;241d	c6		.
	defb 012h		;241e	12		.
	defb 088h		;241f	88		.
	defb 0c2h		;2420	c2		.
	defb 000h		;2421	00		.
	defb 000h		;2422	00		.
	defb 000h		;2423	00		.
	defb 000h		;2424	00		.
	defb 008h		;2425	08		.
	defb 0c6h		;2426	c6		.
	defb 000h		;2427	00		.
	defb 008h		;2428	08		.
	defb 0c6h		;2429	c6		.
	defb 000h		;242a	00		.
	defb 000h		;242b	00		.
	defb 000h		;242c	00		.
	defb 000h		;242d	00		.
	defb 000h		;242e	00		.
	defb 018h		;242f	18		.
	defb 088h		;2430	88		.
	defb 0c6h		;2431	c6		.
	defb 012h		;2432	12		.
	defb 088h		;2433	88		.
	defb 0c2h		;2434	c2		.
	defb 018h		;2435	18		.
	defb 088h		;2436	88		.
	defb 0c6h		;2437	c6		.
	defb 016h		;2438	16		.
	defb 088h		;2439	88		.
	defb 091h		;243a	91		.
	defb 0c2h		;243b	c2		.
	defb 000h		;243c	00		.
	defb 000h		;243d	00		.
	defb 000h		;243e	00		.
	defb 000h		;243f	00		.
	defb 008h		;2440	08		.
	defb 0c6h		;2441	c6		.
	defb 000h		;2442	00		.
	defb 008h		;2443	08		.
	defb 0c6h		;2444	c6		.
	defb 000h		;2445	00		.
	defb 000h		;2446	00		.
	defb 000h		;2447	00		.
	defb 000h		;2448	00		.
	defb 000h		;2449	00		.
	defb 018h		;244a	18		.
	defb 088h		;244b	88		.
	defb 0c6h		;244c	c6		.
	defb 012h		;244d	12		.
	defb 088h		;244e	88		.
	defb 0c2h		;244f	c2		.
	defb 018h		;2450	18		.
	defb 088h		;2451	88		.
	defb 0c6h		;2452	c6		.
	defb 016h		;2453	16		.
	defb 088h		;2454	88		.
	defb 090h		;2455	90		.
	defb 0c2h		;2456	c2		.
	defb 001h		;2457	01		.
	defb 015h		;2458	15		.
	defb 000h		;2459	00		.
	defb 001h		;245a	01		.
	defb 015h		;245b	15		.
	defb 000h		;245c	00		.
	defb 008h		;245d	08		.
	defb 0c6h		;245e	c6		.
	defb 000h		;245f	00		.
	defb 008h		;2460	08		.
	defb 0c6h		;2461	c6		.
	defb 000h		;2462	00		.
	defb 000h		;2463	00		.
	defb 000h		;2464	00		.
	defb 000h		;2465	00		.
	defb 000h		;2466	00		.
	defb 018h		;2467	18		.
	defb 088h		;2468	88		.
	defb 0c6h		;2469	c6		.
	defb 012h		;246a	12		.
	defb 088h		;246b	88		.
	defb 040h		;246c	40		@
	defb 018h		;246d	18		.
	defb 088h		;246e	88		.
	defb 0c6h		;246f	c6		.
	defb 012h		;2470	12		.
	defb 088h		;2471	88		.
	defb 040h		;2472	40		@
	defb 001h		;2473	01		.
	defb 015h		;2474	15		.
	defb 000h		;2475	00		.
	defb 001h		;2476	01		.
	defb 015h		;2477	15		.
	defb 000h		;2478	00		.
	defb 008h		;2479	08		.
	defb 0c6h		;247a	c6		.
	defb 000h		;247b	00		.
	defb 008h		;247c	08		.
	defb 0c6h		;247d	c6		.
	defb 000h		;247e	00		.
	defb 000h		;247f	00		.
	defb 000h		;2480	00		.
	defb 000h		;2481	00		.
	defb 000h		;2482	00		.
	defb 018h		;2483	18		.
	defb 088h		;2484	88		.
	defb 0c6h		;2485	c6		.
	defb 012h		;2486	12		.
	defb 088h		;2487	88		.
	defb 040h		;2488	40		@
	defb 018h		;2489	18		.
	defb 088h		;248a	88		.
	defb 0c6h		;248b	c6		.
	defb 012h		;248c	12		.
	defb 088h		;248d	88		.
	defb 040h		;248e	40		@
	defb 001h		;248f	01		.
	defb 015h		;2490	15		.
	defb 000h		;2491	00		.
	defb 001h		;2492	01		.
	defb 015h		;2493	15		.
	defb 000h		;2494	00		.
	defb 008h		;2495	08		.
	defb 0c6h		;2496	c6		.
	defb 000h		;2497	00		.
	defb 008h		;2498	08		.
	defb 0c6h		;2499	c6		.
	defb 000h		;249a	00		.
	defb 000h		;249b	00		.
	defb 000h		;249c	00		.
	defb 000h		;249d	00		.
	defb 000h		;249e	00		.
	defb 018h		;249f	18		.
	defb 088h		;24a0	88		.
	defb 0c6h		;24a1	c6		.
	defb 012h		;24a2	12		.
	defb 088h		;24a3	88		.
	defb 040h		;24a4	40		@
	defb 018h		;24a5	18		.
	defb 088h		;24a6	88		.
	defb 0c6h		;24a7	c6		.
	defb 012h		;24a8	12		.
	defb 088h		;24a9	88		.
	defb 040h		;24aa	40		@
	defb 001h		;24ab	01		.
	defb 015h		;24ac	15		.
	defb 000h		;24ad	00		.
	defb 001h		;24ae	01		.
	defb 015h		;24af	15		.
	defb 000h		;24b0	00		.
	defb 008h		;24b1	08		.
	defb 0c6h		;24b2	c6		.
	defb 000h		;24b3	00		.
	defb 008h		;24b4	08		.
	defb 0c6h		;24b5	c6		.
	defb 000h		;24b6	00		.
	defb 000h		;24b7	00		.
	defb 000h		;24b8	00		.
	defb 000h		;24b9	00		.
	defb 000h		;24ba	00		.
	defb 018h		;24bb	18		.
	defb 088h		;24bc	88		.
	defb 0c6h		;24bd	c6		.
	defb 012h		;24be	12		.
	defb 088h		;24bf	88		.
	defb 040h		;24c0	40		@
	defb 018h		;24c1	18		.
	defb 088h		;24c2	88		.
	defb 0c6h		;24c3	c6		.
	defb 016h		;24c4	16		.
	defb 088h		;24c5	88		.
	defb 052h		;24c6	52		R
	defb 040h		;24c7	40		@
	defb 005h		;24c8	05		.
	defb 0c1h		;24c9	c1		.
	defb 010h		;24ca	10		.
	defb 004h		;24cb	04		.
	defb 0c1h		;24cc	c1		.
	defb 005h		;24cd	05		.
	defb 0c1h		;24ce	c1		.
	defb 010h		;24cf	10		.
	defb 004h		;24d0	04		.
	defb 0c1h		;24d1	c1		.
	defb 005h		;24d2	05		.
	defb 0c1h		;24d3	c1		.
	defb 011h		;24d4	11		.
	defb 004h		;24d5	04		.
	defb 0c1h		;24d6	c1		.
	defb 005h		;24d7	05		.
	defb 0c1h		;24d8	c1		.
	defb 011h		;24d9	11		.
	defb 004h		;24da	04		.
	defb 0c1h		;24db	c1		.
	defb 005h		;24dc	05		.
	defb 0c1h		;24dd	c1		.
	defb 012h		;24de	12		.
	defb 004h		;24df	04		.
	defb 0c1h		;24e0	c1		.
	defb 005h		;24e1	05		.
	defb 0c1h		;24e2	c1		.
	defb 012h		;24e3	12		.
	defb 004h		;24e4	04		.
	defb 0c1h		;24e5	c1		.
	defb 005h		;24e6	05		.
	defb 0c1h		;24e7	c1		.
	defb 013h		;24e8	13		.
	defb 007h		;24e9	07		.
	defb 0c1h		;24ea	c1		.
	defb 0c2h		;24eb	c2		.
	defb 014h		;24ec	14		.
	defb 005h		;24ed	05		.
	defb 0c1h		;24ee	c1		.
	defb 013h		;24ef	13		.
	defb 007h		;24f0	07		.
	defb 0c1h		;24f1	c1		.
	defb 0c2h		;24f2	c2		.
	defb 014h		;24f3	14		.
	defb 005h		;24f4	05		.
	defb 0c1h		;24f5	c1		.
	defb 010h		;24f6	10		.
	defb 004h		;24f7	04		.
	defb 0c1h		;24f8	c1		.
	defb 005h		;24f9	05		.
	defb 0c1h		;24fa	c1		.
	defb 010h		;24fb	10		.
	defb 004h		;24fc	04		.
	defb 0c1h		;24fd	c1		.
	defb 005h		;24fe	05		.
	defb 0c1h		;24ff	c1		.
	defb 011h		;2500	11		.
	defb 004h		;2501	04		.
	defb 0c1h		;2502	c1		.
	defb 005h		;2503	05		.
	defb 0c1h		;2504	c1		.
	defb 011h		;2505	11		.
	defb 004h		;2506	04		.
	defb 0c1h		;2507	c1		.
	defb 005h		;2508	05		.
	defb 0c1h		;2509	c1		.
	defb 012h		;250a	12		.
	defb 004h		;250b	04		.
	defb 0c1h		;250c	c1		.
	defb 005h		;250d	05		.
	defb 0c1h		;250e	c1		.
	defb 012h		;250f	12		.
	defb 004h		;2510	04		.
	defb 0c1h		;2511	c1		.
	defb 005h		;2512	05		.
	defb 0c1h		;2513	c1		.
	defb 013h		;2514	13		.
	defb 007h		;2515	07		.
	defb 0c1h		;2516	c1		.
	defb 0c2h		;2517	c2		.
	defb 014h		;2518	14		.
	defb 005h		;2519	05		.
	defb 0c1h		;251a	c1		.
	defb 013h		;251b	13		.
	defb 007h		;251c	07		.
	defb 0c1h		;251d	c1		.
	defb 0c2h		;251e	c2		.
	defb 014h		;251f	14		.
	defb 001h		;2520	01		.
	defb 010h		;2521	10		.
	defb 000h		;2522	00		.
	defb 001h		;2523	01		.
	defb 010h		;2524	10		.
	defb 000h		;2525	00		.
	defb 001h		;2526	01		.
	defb 011h		;2527	11		.
	defb 000h		;2528	00		.
	defb 001h		;2529	01		.
	defb 011h		;252a	11		.
	defb 000h		;252b	00		.
	defb 001h		;252c	01		.
	defb 012h		;252d	12		.
	defb 000h		;252e	00		.
	defb 001h		;252f	01		.
	defb 012h		;2530	12		.
	defb 000h		;2531	00		.
	defb 001h		;2532	01		.
	defb 013h		;2533	13		.
	defb 003h		;2534	03		.
	defb 0c2h		;2535	c2		.
	defb 014h		;2536	14		.
	defb 001h		;2537	01		.
	defb 013h		;2538	13		.
	defb 007h		;2539	07		.
	defb 091h		;253a	91		.
	defb 0c2h		;253b	c2		.
	defb 014h		;253c	14		.
	defb 001h		;253d	01		.
	defb 010h		;253e	10		.
	defb 000h		;253f	00		.
	defb 001h		;2540	01		.
	defb 010h		;2541	10		.
	defb 000h		;2542	00		.
	defb 001h		;2543	01		.
	defb 011h		;2544	11		.
	defb 000h		;2545	00		.
	defb 001h		;2546	01		.
	defb 011h		;2547	11		.
	defb 000h		;2548	00		.
	defb 001h		;2549	01		.
	defb 012h		;254a	12		.
	defb 000h		;254b	00		.
	defb 001h		;254c	01		.
	defb 012h		;254d	12		.
	defb 000h		;254e	00		.
	defb 001h		;254f	01		.
	defb 013h		;2550	13		.
	defb 003h		;2551	03		.
	defb 0c2h		;2552	c2		.
	defb 014h		;2553	14		.
	defb 001h		;2554	01		.
	defb 013h		;2555	13		.
	defb 007h		;2556	07		.
	defb 090h		;2557	90		.
	defb 0c2h		;2558	c2		.
	defb 014h		;2559	14		.
	defb 001h		;255a	01		.
	defb 010h		;255b	10		.
	defb 000h		;255c	00		.
	defb 001h		;255d	01		.
	defb 010h		;255e	10		.
	defb 000h		;255f	00		.
	defb 001h		;2560	01		.
	defb 011h		;2561	11		.
	defb 000h		;2562	00		.
	defb 001h		;2563	01		.
	defb 011h		;2564	11		.
	defb 000h		;2565	00		.
	defb 001h		;2566	01		.
	defb 012h		;2567	12		.
	defb 000h		;2568	00		.
	defb 001h		;2569	01		.
	defb 012h		;256a	12		.
	defb 000h		;256b	00		.
	defb 001h		;256c	01		.
	defb 013h		;256d	13		.
	defb 001h		;256e	01		.
	defb 015h		;256f	15		.
	defb 001h		;2570	01		.
	defb 013h		;2571	13		.
	defb 001h		;2572	01		.
	defb 015h		;2573	15		.
	defb 001h		;2574	01		.
	defb 010h		;2575	10		.
	defb 000h		;2576	00		.
	defb 001h		;2577	01		.
	defb 010h		;2578	10		.
	defb 000h		;2579	00		.
	defb 001h		;257a	01		.
	defb 011h		;257b	11		.
	defb 000h		;257c	00		.
	defb 001h		;257d	01		.
	defb 011h		;257e	11		.
	defb 000h		;257f	00		.
	defb 001h		;2580	01		.
	defb 012h		;2581	12		.
	defb 000h		;2582	00		.
	defb 001h		;2583	01		.
	defb 012h		;2584	12		.
	defb 000h		;2585	00		.
	defb 001h		;2586	01		.
	defb 013h		;2587	13		.
	defb 001h		;2588	01		.
	defb 015h		;2589	15		.
	defb 001h		;258a	01		.
	defb 013h		;258b	13		.
	defb 001h		;258c	01		.
	defb 015h		;258d	15		.
	defb 001h		;258e	01		.
	defb 010h		;258f	10		.
	defb 000h		;2590	00		.
	defb 001h		;2591	01		.
	defb 010h		;2592	10		.
	defb 000h		;2593	00		.
	defb 001h		;2594	01		.
	defb 011h		;2595	11		.
	defb 000h		;2596	00		.
	defb 001h		;2597	01		.
	defb 011h		;2598	11		.
	defb 000h		;2599	00		.
	defb 001h		;259a	01		.
	defb 012h		;259b	12		.
	defb 000h		;259c	00		.
	defb 001h		;259d	01		.
	defb 012h		;259e	12		.
	defb 000h		;259f	00		.
	defb 001h		;25a0	01		.
	defb 013h		;25a1	13		.
	defb 001h		;25a2	01		.
	defb 015h		;25a3	15		.
	defb 001h		;25a4	01		.
	defb 013h		;25a5	13		.
	defb 001h		;25a6	01		.
	defb 015h		;25a7	15		.
	defb 001h		;25a8	01		.
	defb 010h		;25a9	10		.
	defb 000h		;25aa	00		.
	defb 001h		;25ab	01		.
	defb 010h		;25ac	10		.
	defb 000h		;25ad	00		.
	defb 001h		;25ae	01		.
	defb 011h		;25af	11		.
	defb 000h		;25b0	00		.
	defb 001h		;25b1	01		.
	defb 011h		;25b2	11		.
	defb 000h		;25b3	00		.
	defb 001h		;25b4	01		.
	defb 012h		;25b5	12		.
	defb 000h		;25b6	00		.
	defb 001h		;25b7	01		.
	defb 012h		;25b8	12		.
	defb 000h		;25b9	00		.
	defb 001h		;25ba	01		.
	defb 013h		;25bb	13		.
	defb 001h		;25bc	01		.
	defb 015h		;25bd	15		.
	defb 001h		;25be	01		.
	defb 013h		;25bf	13		.
	defb 005h		;25c0	05		.
	defb 052h		;25c1	52		R
	defb 015h		;25c2	15		.
	defb 004h		;25c3	04		.
	defb 0c1h		;25c4	c1		.
	defb 004h		;25c5	04		.
	defb 0c1h		;25c6	c1		.
	defb 004h		;25c7	04		.
	defb 0c1h		;25c8	c1		.
	defb 004h		;25c9	04		.
	defb 0c1h		;25ca	c1		.
	defb 004h		;25cb	04		.
	defb 0c1h		;25cc	c1		.
	defb 004h		;25cd	04		.
	defb 0c1h		;25ce	c1		.
	defb 004h		;25cf	04		.
	defb 0c1h		;25d0	c1		.
	defb 004h		;25d1	04		.
	defb 0c1h		;25d2	c1		.
	defb 004h		;25d3	04		.
	defb 0c1h		;25d4	c1		.
	defb 004h		;25d5	04		.
	defb 0c1h		;25d6	c1		.
	defb 004h		;25d7	04		.
	defb 0c1h		;25d8	c1		.
	defb 004h		;25d9	04		.
	defb 0c1h		;25da	c1		.
	defb 004h		;25db	04		.
	defb 0c1h		;25dc	c1		.
	defb 006h		;25dd	06		.
	defb 0c1h		;25de	c1		.
	defb 0c2h		;25df	c2		.
	defb 004h		;25e0	04		.
	defb 0c1h		;25e1	c1		.
	defb 006h		;25e2	06		.
	defb 0c1h		;25e3	c1		.
	defb 0c2h		;25e4	c2		.
	defb 004h		;25e5	04		.
	defb 0c1h		;25e6	c1		.
	defb 004h		;25e7	04		.
	defb 0c1h		;25e8	c1		.
	defb 004h		;25e9	04		.
	defb 0c1h		;25ea	c1		.
	defb 004h		;25eb	04		.
	defb 0c1h		;25ec	c1		.
	defb 004h		;25ed	04		.
	defb 0c1h		;25ee	c1		.
	defb 004h		;25ef	04		.
	defb 0c1h		;25f0	c1		.
	defb 004h		;25f1	04		.
	defb 0c1h		;25f2	c1		.
	defb 004h		;25f3	04		.
	defb 0c1h		;25f4	c1		.
	defb 004h		;25f5	04		.
	defb 0c1h		;25f6	c1		.
	defb 004h		;25f7	04		.
	defb 0c1h		;25f8	c1		.
	defb 004h		;25f9	04		.
	defb 0c1h		;25fa	c1		.
	defb 004h		;25fb	04		.
	defb 0c1h		;25fc	c1		.
	defb 004h		;25fd	04		.
	defb 0c1h		;25fe	c1		.
	defb 006h		;25ff	06		.
	defb 0c1h		;2600	c1		.
	defb 0c2h		;2601	c2		.
	defb 004h		;2602	04		.
	defb 0c1h		;2603	c1		.
	defb 006h		;2604	06		.
	defb 0c1h		;2605	c1		.
	defb 0c2h		;2606	c2		.
	defb 000h		;2607	00		.
	defb 000h		;2608	00		.
	defb 000h		;2609	00		.
	defb 000h		;260a	00		.
	defb 000h		;260b	00		.
	defb 000h		;260c	00		.
	defb 000h		;260d	00		.
	defb 000h		;260e	00		.
	defb 000h		;260f	00		.
	defb 000h		;2610	00		.
	defb 000h		;2611	00		.
	defb 000h		;2612	00		.
	defb 000h		;2613	00		.
	defb 002h		;2614	02		.
	defb 0c2h		;2615	c2		.
	defb 000h		;2616	00		.
	defb 006h		;2617	06		.
	defb 091h		;2618	91		.
	defb 0c2h		;2619	c2		.
	defb 000h		;261a	00		.
	defb 000h		;261b	00		.
	defb 000h		;261c	00		.
	defb 000h		;261d	00		.
	defb 000h		;261e	00		.
	defb 000h		;261f	00		.
	defb 000h		;2620	00		.
	defb 000h		;2621	00		.
	defb 000h		;2622	00		.
	defb 000h		;2623	00		.
	defb 000h		;2624	00		.
	defb 000h		;2625	00		.
	defb 000h		;2626	00		.
	defb 002h		;2627	02		.
	defb 0c2h		;2628	c2		.
	defb 000h		;2629	00		.
	defb 006h		;262a	06		.
	defb 090h		;262b	90		.
	defb 0c2h		;262c	c2		.
	defb 001h		;262d	01		.
	defb 015h		;262e	15		.
	defb 000h		;262f	00		.
	defb 001h		;2630	01		.
	defb 015h		;2631	15		.
	defb 000h		;2632	00		.
	defb 000h		;2633	00		.
	defb 000h		;2634	00		.
	defb 000h		;2635	00		.
	defb 000h		;2636	00		.
	defb 000h		;2637	00		.
	defb 000h		;2638	00		.
	defb 000h		;2639	00		.
	defb 000h		;263a	00		.
	defb 000h		;263b	00		.
	defb 002h		;263c	02		.
	defb 040h		;263d	40		@
	defb 000h		;263e	00		.
	defb 002h		;263f	02		.
	defb 040h		;2640	40		@
	defb 001h		;2641	01		.
	defb 015h		;2642	15		.
	defb 000h		;2643	00		.
	defb 001h		;2644	01		.
	defb 015h		;2645	15		.
	defb 000h		;2646	00		.
	defb 000h		;2647	00		.
	defb 000h		;2648	00		.
	defb 000h		;2649	00		.
	defb 000h		;264a	00		.
	defb 000h		;264b	00		.
	defb 000h		;264c	00		.
	defb 000h		;264d	00		.
	defb 000h		;264e	00		.
	defb 000h		;264f	00		.
	defb 002h		;2650	02		.
	defb 040h		;2651	40		@
	defb 000h		;2652	00		.
	defb 002h		;2653	02		.
	defb 040h		;2654	40		@
	defb 001h		;2655	01		.
	defb 015h		;2656	15		.
	defb 000h		;2657	00		.
	defb 001h		;2658	01		.
	defb 015h		;2659	15		.
	defb 000h		;265a	00		.
	defb 000h		;265b	00		.
	defb 000h		;265c	00		.
	defb 000h		;265d	00		.
	defb 000h		;265e	00		.
	defb 000h		;265f	00		.
	defb 000h		;2660	00		.
	defb 000h		;2661	00		.
	defb 000h		;2662	00		.
	defb 000h		;2663	00		.
	defb 002h		;2664	02		.
	defb 040h		;2665	40		@
	defb 000h		;2666	00		.
	defb 002h		;2667	02		.
	defb 040h		;2668	40		@
	defb 001h		;2669	01		.
	defb 015h		;266a	15		.
	defb 000h		;266b	00		.
	defb 001h		;266c	01		.
	defb 015h		;266d	15		.
	defb 000h		;266e	00		.
	defb 000h		;266f	00		.
	defb 000h		;2670	00		.
	defb 000h		;2671	00		.
	defb 000h		;2672	00		.
	defb 000h		;2673	00		.
	defb 000h		;2674	00		.
	defb 000h		;2675	00		.
	defb 000h		;2676	00		.
	defb 000h		;2677	00		.
	defb 002h		;2678	02		.
	defb 040h		;2679	40		@
	defb 000h		;267a	00		.
	defb 006h		;267b	06		.
	defb 052h		;267c	52		R
	defb 040h		;267d	40		@
	defb 001h		;267e	01		.
	defb 010h		;267f	10		.
	defb 000h		;2680	00		.
	defb 001h		;2681	01		.
	defb 010h		;2682	10		.
	defb 000h		;2683	00		.
	defb 001h		;2684	01		.
	defb 011h		;2685	11		.
	defb 000h		;2686	00		.
	defb 001h		;2687	01		.
	defb 011h		;2688	11		.
	defb 000h		;2689	00		.
	defb 001h		;268a	01		.
	defb 012h		;268b	12		.
	defb 000h		;268c	00		.
	defb 001h		;268d	01		.
	defb 012h		;268e	12		.
	defb 000h		;268f	00		.
	defb 001h		;2690	01		.
	defb 013h		;2691	13		.
	defb 003h		;2692	03		.
	defb 0c2h		;2693	c2		.
	defb 014h		;2694	14		.
	defb 001h		;2695	01		.
	defb 013h		;2696	13		.
	defb 003h		;2697	03		.
	defb 0c2h		;2698	c2		.
	defb 014h		;2699	14		.
	defb 001h		;269a	01		.
	defb 010h		;269b	10		.
	defb 000h		;269c	00		.
	defb 001h		;269d	01		.
	defb 010h		;269e	10		.
	defb 000h		;269f	00		.
	defb 001h		;26a0	01		.
	defb 011h		;26a1	11		.
	defb 000h		;26a2	00		.
	defb 001h		;26a3	01		.
	defb 011h		;26a4	11		.
	defb 000h		;26a5	00		.
	defb 001h		;26a6	01		.
	defb 012h		;26a7	12		.
	defb 000h		;26a8	00		.
	defb 001h		;26a9	01		.
	defb 012h		;26aa	12		.
	defb 000h		;26ab	00		.
	defb 001h		;26ac	01		.
	defb 013h		;26ad	13		.
	defb 003h		;26ae	03		.
	defb 0c2h		;26af	c2		.
	defb 014h		;26b0	14		.
	defb 001h		;26b1	01		.
	defb 013h		;26b2	13		.
	defb 003h		;26b3	03		.
	defb 0c2h		;26b4	c2		.
	defb 014h		;26b5	14		.
	defb 001h		;26b6	01		.
	defb 010h		;26b7	10		.
	defb 000h		;26b8	00		.
	defb 001h		;26b9	01		.
	defb 010h		;26ba	10		.
	defb 000h		;26bb	00		.
	defb 001h		;26bc	01		.
	defb 011h		;26bd	11		.
	defb 000h		;26be	00		.
	defb 001h		;26bf	01		.
	defb 011h		;26c0	11		.
	defb 000h		;26c1	00		.
	defb 001h		;26c2	01		.
	defb 012h		;26c3	12		.
	defb 000h		;26c4	00		.
	defb 001h		;26c5	01		.
	defb 012h		;26c6	12		.
	defb 000h		;26c7	00		.
	defb 001h		;26c8	01		.
	defb 013h		;26c9	13		.
	defb 003h		;26ca	03		.
	defb 0c2h		;26cb	c2		.
	defb 014h		;26cc	14		.
	defb 001h		;26cd	01		.
	defb 013h		;26ce	13		.
	defb 007h		;26cf	07		.
	defb 091h		;26d0	91		.
	defb 0c2h		;26d1	c2		.
	defb 014h		;26d2	14		.
	defb 001h		;26d3	01		.
	defb 010h		;26d4	10		.
	defb 000h		;26d5	00		.
	defb 001h		;26d6	01		.
	defb 010h		;26d7	10		.
	defb 000h		;26d8	00		.
	defb 001h		;26d9	01		.
	defb 011h		;26da	11		.
	defb 000h		;26db	00		.
	defb 001h		;26dc	01		.
	defb 011h		;26dd	11		.
	defb 000h		;26de	00		.
	defb 001h		;26df	01		.
	defb 012h		;26e0	12		.
	defb 000h		;26e1	00		.
	defb 001h		;26e2	01		.
	defb 012h		;26e3	12		.
	defb 000h		;26e4	00		.
	defb 001h		;26e5	01		.
	defb 013h		;26e6	13		.
	defb 003h		;26e7	03		.
	defb 0c2h		;26e8	c2		.
	defb 014h		;26e9	14		.
	defb 001h		;26ea	01		.
	defb 013h		;26eb	13		.
	defb 007h		;26ec	07		.
	defb 090h		;26ed	90		.
	defb 0c2h		;26ee	c2		.
	defb 014h		;26ef	14		.
	defb 001h		;26f0	01		.
	defb 010h		;26f1	10		.
	defb 000h		;26f2	00		.
	defb 001h		;26f3	01		.
	defb 010h		;26f4	10		.
	defb 000h		;26f5	00		.
	defb 001h		;26f6	01		.
	defb 011h		;26f7	11		.
	defb 000h		;26f8	00		.
	defb 001h		;26f9	01		.
	defb 011h		;26fa	11		.
	defb 000h		;26fb	00		.
	defb 001h		;26fc	01		.
	defb 012h		;26fd	12		.
	defb 000h		;26fe	00		.
	defb 001h		;26ff	01		.
	defb 012h		;2700	12		.
	defb 000h		;2701	00		.
	defb 001h		;2702	01		.
	defb 013h		;2703	13		.
	defb 001h		;2704	01		.
	defb 015h		;2705	15		.
	defb 001h		;2706	01		.
	defb 013h		;2707	13		.
	defb 001h		;2708	01		.
	defb 015h		;2709	15		.
	defb 001h		;270a	01		.
	defb 010h		;270b	10		.
	defb 000h		;270c	00		.
	defb 001h		;270d	01		.
	defb 010h		;270e	10		.
	defb 000h		;270f	00		.
	defb 001h		;2710	01		.
	defb 011h		;2711	11		.
	defb 000h		;2712	00		.
	defb 001h		;2713	01		.
	defb 011h		;2714	11		.
	defb 000h		;2715	00		.
	defb 001h		;2716	01		.
	defb 012h		;2717	12		.
	defb 000h		;2718	00		.
	defb 001h		;2719	01		.
	defb 012h		;271a	12		.
	defb 000h		;271b	00		.
	defb 001h		;271c	01		.
	defb 013h		;271d	13		.
	defb 001h		;271e	01		.
	defb 015h		;271f	15		.
	defb 001h		;2720	01		.
	defb 013h		;2721	13		.
	defb 001h		;2722	01		.
	defb 015h		;2723	15		.
	defb 001h		;2724	01		.
	defb 010h		;2725	10		.
	defb 000h		;2726	00		.
	defb 001h		;2727	01		.
	defb 010h		;2728	10		.
	defb 000h		;2729	00		.
	defb 001h		;272a	01		.
	defb 011h		;272b	11		.
	defb 000h		;272c	00		.
	defb 001h		;272d	01		.
	defb 011h		;272e	11		.
	defb 000h		;272f	00		.
	defb 001h		;2730	01		.
	defb 012h		;2731	12		.
	defb 000h		;2732	00		.
	defb 001h		;2733	01		.
	defb 012h		;2734	12		.
	defb 000h		;2735	00		.
	defb 001h		;2736	01		.
	defb 013h		;2737	13		.
	defb 001h		;2738	01		.
	defb 015h		;2739	15		.
	defb 001h		;273a	01		.
	defb 013h		;273b	13		.
	defb 001h		;273c	01		.
	defb 015h		;273d	15		.
	defb 001h		;273e	01		.
	defb 010h		;273f	10		.
	defb 000h		;2740	00		.
	defb 001h		;2741	01		.
	defb 010h		;2742	10		.
	defb 000h		;2743	00		.
	defb 001h		;2744	01		.
	defb 011h		;2745	11		.
	defb 000h		;2746	00		.
	defb 001h		;2747	01		.
	defb 011h		;2748	11		.
	defb 000h		;2749	00		.
	defb 001h		;274a	01		.
	defb 012h		;274b	12		.
	defb 000h		;274c	00		.
	defb 001h		;274d	01		.
	defb 012h		;274e	12		.
	defb 000h		;274f	00		.
	defb 001h		;2750	01		.
	defb 013h		;2751	13		.
	defb 001h		;2752	01		.
	defb 015h		;2753	15		.
	defb 001h		;2754	01		.
	defb 013h		;2755	13		.
	defb 005h		;2756	05		.
	defb 052h		;2757	52		R
	defb 015h		;2758	15		.
	defb 000h		;2759	00		.
	defb 000h		;275a	00		.
	defb 000h		;275b	00		.
	defb 000h		;275c	00		.
	defb 000h		;275d	00		.
	defb 000h		;275e	00		.
	defb 000h		;275f	00		.
	defb 000h		;2760	00		.
	defb 000h		;2761	00		.
	defb 000h		;2762	00		.
	defb 000h		;2763	00		.
	defb 000h		;2764	00		.
	defb 000h		;2765	00		.
	defb 002h		;2766	02		.
	defb 0c2h		;2767	c2		.
	defb 000h		;2768	00		.
	defb 002h		;2769	02		.
	defb 0c2h		;276a	c2		.
	defb 000h		;276b	00		.
	defb 000h		;276c	00		.
	defb 000h		;276d	00		.
	defb 000h		;276e	00		.
	defb 000h		;276f	00		.
	defb 000h		;2770	00		.
	defb 000h		;2771	00		.
	defb 000h		;2772	00		.
	defb 000h		;2773	00		.
	defb 000h		;2774	00		.
	defb 000h		;2775	00		.
	defb 000h		;2776	00		.
	defb 000h		;2777	00		.
	defb 002h		;2778	02		.
	defb 0c2h		;2779	c2		.
	defb 000h		;277a	00		.
	defb 002h		;277b	02		.
	defb 0c2h		;277c	c2		.
	defb 000h		;277d	00		.
	defb 000h		;277e	00		.
	defb 000h		;277f	00		.
	defb 000h		;2780	00		.
	defb 000h		;2781	00		.
	defb 000h		;2782	00		.
	defb 000h		;2783	00		.
	defb 000h		;2784	00		.
	defb 000h		;2785	00		.
	defb 000h		;2786	00		.
	defb 000h		;2787	00		.
	defb 000h		;2788	00		.
	defb 000h		;2789	00		.
	defb 002h		;278a	02		.
	defb 0c2h		;278b	c2		.
	defb 000h		;278c	00		.
	defb 006h		;278d	06		.
	defb 091h		;278e	91		.
	defb 0c2h		;278f	c2		.
	defb 000h		;2790	00		.
	defb 000h		;2791	00		.
	defb 000h		;2792	00		.
	defb 000h		;2793	00		.
	defb 000h		;2794	00		.
	defb 000h		;2795	00		.
	defb 000h		;2796	00		.
	defb 000h		;2797	00		.
	defb 000h		;2798	00		.
	defb 000h		;2799	00		.
	defb 000h		;279a	00		.
	defb 000h		;279b	00		.
	defb 000h		;279c	00		.
	defb 002h		;279d	02		.
	defb 0c2h		;279e	c2		.
	defb 000h		;279f	00		.
	defb 006h		;27a0	06		.
	defb 090h		;27a1	90		.
	defb 0c2h		;27a2	c2		.
	defb 001h		;27a3	01		.
	defb 015h		;27a4	15		.
	defb 000h		;27a5	00		.
	defb 001h		;27a6	01		.
	defb 015h		;27a7	15		.
	defb 000h		;27a8	00		.
	defb 000h		;27a9	00		.
	defb 000h		;27aa	00		.
	defb 000h		;27ab	00		.
	defb 000h		;27ac	00		.
	defb 000h		;27ad	00		.
	defb 000h		;27ae	00		.
	defb 000h		;27af	00		.
	defb 000h		;27b0	00		.
	defb 000h		;27b1	00		.
	defb 002h		;27b2	02		.
	defb 040h		;27b3	40		@
	defb 000h		;27b4	00		.
	defb 002h		;27b5	02		.
	defb 040h		;27b6	40		@
	defb 001h		;27b7	01		.
	defb 015h		;27b8	15		.
	defb 000h		;27b9	00		.
	defb 001h		;27ba	01		.
	defb 015h		;27bb	15		.
	defb 000h		;27bc	00		.
	defb 000h		;27bd	00		.
	defb 000h		;27be	00		.
	defb 000h		;27bf	00		.
	defb 000h		;27c0	00		.
	defb 000h		;27c1	00		.
	defb 000h		;27c2	00		.
	defb 000h		;27c3	00		.
	defb 000h		;27c4	00		.
	defb 000h		;27c5	00		.
	defb 002h		;27c6	02		.
	defb 040h		;27c7	40		@
	defb 000h		;27c8	00		.
	defb 002h		;27c9	02		.
	defb 040h		;27ca	40		@
	defb 001h		;27cb	01		.
	defb 015h		;27cc	15		.
	defb 000h		;27cd	00		.
	defb 001h		;27ce	01		.
	defb 015h		;27cf	15		.
	defb 000h		;27d0	00		.
	defb 000h		;27d1	00		.
	defb 000h		;27d2	00		.
	defb 000h		;27d3	00		.
	defb 000h		;27d4	00		.
	defb 000h		;27d5	00		.
	defb 000h		;27d6	00		.
	defb 000h		;27d7	00		.
	defb 000h		;27d8	00		.
	defb 000h		;27d9	00		.
	defb 002h		;27da	02		.
	defb 040h		;27db	40		@
	defb 000h		;27dc	00		.
	defb 002h		;27dd	02		.
	defb 040h		;27de	40		@
	defb 001h		;27df	01		.
	defb 015h		;27e0	15		.
	defb 000h		;27e1	00		.
	defb 001h		;27e2	01		.
	defb 015h		;27e3	15		.
	defb 000h		;27e4	00		.
	defb 000h		;27e5	00		.
	defb 000h		;27e6	00		.
	defb 000h		;27e7	00		.
	defb 000h		;27e8	00		.
	defb 000h		;27e9	00		.
	defb 000h		;27ea	00		.
	defb 000h		;27eb	00		.
	defb 000h		;27ec	00		.
	defb 000h		;27ed	00		.
	defb 002h		;27ee	02		.
	defb 040h		;27ef	40		@
	defb 000h		;27f0	00		.
	defb 006h		;27f1	06		.
	defb 052h		;27f2	52		R
	defb 040h		;27f3	40		@
l27f4h:

; BLOCK 'Data27F4' (start 0x27f4 end 0x2800)
Data27F4:
	defb 0ffh		;27f4	ff		.
	defb 0ffh		;27f5	ff		.
	defb 0ffh		;27f6	ff		.
	defb 0ffh		;27f7	ff		.
	defb 0ffh		;27f8	ff		.
	defb 0ffh		;27f9	ff		.
	defb 0ffh		;27fa	ff		.
	defb 0ffh		;27fb	ff		.
	defb 0ffh		;27fc	ff		.
	defb 0ffh		;27fd	ff		.
	defb 0ffh		;27fe	ff		.
	defb 0ffh		;27ff	ff		.
;---------------------------------------------------------------------------
; Font8x8: 256 characters x 8 bytes (same as BIOS-TT 0271ac3 FONT).
;---------------------------------------------------------------------------
Font8x8:

; BLOCK 'Font8x8' (start 0x2800 end 0x3000)
				; = ~ZG_ADRESS (src: EXP.asm:960, BIOS-TT 0271ac3)
	defb 000h		;2800	00		.
	defb 07eh		;2801	7e		~
	defb 07eh		;2802	7e		~
	defb 06ch		;2803	6c		l
	defb 010h		;2804	10		.
	defb 038h		;2805	38		8
	defb 010h		;2806	10		.
	defb 000h		;2807	00		.
	defb 0ffh		;2808	ff		.
	defb 000h		;2809	00		.
	defb 0ffh		;280a	ff		.
	defb 00fh		;280b	0f		.
	defb 03ch		;280c	3c		<
	defb 03fh		;280d	3f		?
	defb 07fh		;280e	7f		.
	defb 018h		;280f	18		.
	defb 080h		;2810	80		.
	defb 002h		;2811	02		.
	defb 018h		;2812	18		.
	defb 066h		;2813	66		f
	defb 07fh		;2814	7f		.
	defb 03eh		;2815	3e		>
	defb 000h		;2816	00		.
	defb 018h		;2817	18		.
	defb 018h		;2818	18		.
	defb 018h		;2819	18		.
	defb 000h		;281a	00		.
	defb 000h		;281b	00		.
	defb 000h		;281c	00		.
	defb 000h		;281d	00		.
	defb 000h		;281e	00		.
	defb 000h		;281f	00		.
	defb 000h		;2820	00		.
	defb 030h		;2821	30		0
	defb 06ch		;2822	6c		l
	defb 06ch		;2823	6c		l
	defb 030h		;2824	30		0
	defb 000h		;2825	00		.
	defb 038h		;2826	38		8
	defb 060h		;2827	60		`
	defb 018h		;2828	18		.
	defb 060h		;2829	60		`
	defb 000h		;282a	00		.
	defb 000h		;282b	00		.
	defb 000h		;282c	00		.
	defb 000h		;282d	00		.
	defb 000h		;282e	00		.
	defb 006h		;282f	06		.
	defb 07ch		;2830	7c		|
	defb 030h		;2831	30		0
	defb 078h		;2832	78		x
	defb 078h		;2833	78		x
	defb 01ch		;2834	1c		.
	defb 0fch		;2835	fc		.
	defb 038h		;2836	38		8
	defb 0fch		;2837	fc		.
	defb 078h		;2838	78		x
	defb 078h		;2839	78		x
	defb 000h		;283a	00		.
	defb 000h		;283b	00		.
	defb 018h		;283c	18		.
	defb 000h		;283d	00		.
	defb 060h		;283e	60		`
	defb 078h		;283f	78		x
	defb 07ch		;2840	7c		|
	defb 030h		;2841	30		0
	defb 0fch		;2842	fc		.
	defb 03ch		;2843	3c		<
	defb 0f8h		;2844	f8		.
	defb 0feh		;2845	fe		.
	defb 0feh		;2846	fe		.
	defb 03ch		;2847	3c		<
	defb 0cch		;2848	cc		.
	defb 078h		;2849	78		x
	defb 01eh		;284a	1e		.
	defb 0e6h		;284b	e6		.
	defb 0f0h		;284c	f0		.
	defb 0c6h		;284d	c6		.
	defb 0c6h		;284e	c6		.
	defb 038h		;284f	38		8
	defb 0fch		;2850	fc		.
	defb 078h		;2851	78		x
	defb 0fch		;2852	fc		.
	defb 078h		;2853	78		x
	defb 0fch		;2854	fc		.
	defb 0cch		;2855	cc		.
	defb 0cch		;2856	cc		.
	defb 0c6h		;2857	c6		.
	defb 0c6h		;2858	c6		.
	defb 0cch		;2859	cc		.
	defb 0feh		;285a	fe		.
	defb 078h		;285b	78		x
	defb 0c0h		;285c	c0		.
	defb 078h		;285d	78		x
	defb 010h		;285e	10		.
	defb 000h		;285f	00		.
	defb 030h		;2860	30		0
	defb 000h		;2861	00		.
	defb 0e0h		;2862	e0		.
	defb 000h		;2863	00		.
	defb 01ch		;2864	1c		.
	defb 000h		;2865	00		.
	defb 038h		;2866	38		8
	defb 000h		;2867	00		.
	defb 0e0h		;2868	e0		.
	defb 030h		;2869	30		0
	defb 00ch		;286a	0c		.
	defb 0e0h		;286b	e0		.
	defb 070h		;286c	70		p
	defb 000h		;286d	00		.
	defb 000h		;286e	00		.
	defb 000h		;286f	00		.
	defb 000h		;2870	00		.
	defb 000h		;2871	00		.
	defb 000h		;2872	00		.
	defb 000h		;2873	00		.
	defb 010h		;2874	10		.
	defb 000h		;2875	00		.
	defb 000h		;2876	00		.
	defb 000h		;2877	00		.
	defb 000h		;2878	00		.
	defb 000h		;2879	00		.
	defb 000h		;287a	00		.
	defb 01ch		;287b	1c		.
	defb 018h		;287c	18		.
	defb 0e0h		;287d	e0		.
	defb 076h		;287e	76		v
	defb 000h		;287f	00		.
	defb 00eh		;2880	0e		.
	defb 0fch		;2881	fc		.
	defb 0f8h		;2882	f8		.
	defb 0fch		;2883	fc		.
	defb 07eh		;2884	7e		~
	defb 0fch		;2885	fc		.
	defb 0dbh		;2886	db		.
	defb 03ch		;2887	3c		<
	defb 0c6h		;2888	c6		.
	defb 0d6h		;2889	d6		.
	defb 0c6h		;288a	c6		.
	defb 006h		;288b	06		.
	defb 0c6h		;288c	c6		.
	defb 0c6h		;288d	c6		.
	defb 07ch		;288e	7c		|
	defb 0feh		;288f	fe		.
	defb 0fch		;2890	fc		.
	defb 07ch		;2891	7c		|
	defb 0fch		;2892	fc		.
	defb 0c6h		;2893	c6		.
	defb 018h		;2894	18		.
	defb 0c3h		;2895	c3		.
	defb 0cch		;2896	cc		.
	defb 0c6h		;2897	c6		.
	defb 0d6h		;2898	d6		.
	defb 0d6h		;2899	d6		.
	defb 0f0h		;289a	f0		.
	defb 0c2h		;289b	c2		.
	defb 0c0h		;289c	c0		.
	defb 07ch		;289d	7c		|
	defb 0ceh		;289e	ce		.
	defb 07eh		;289f	7e		~
	defb 000h		;28a0	00		.
	defb 004h		;28a1	04		.
	defb 000h		;28a2	00		.
	defb 000h		;28a3	00		.
	defb 000h		;28a4	00		.
	defb 000h		;28a5	00		.
	defb 000h		;28a6	00		.
	defb 000h		;28a7	00		.
	defb 000h		;28a8	00		.
	defb 030h		;28a9	30		0
	defb 000h		;28aa	00		.
	defb 000h		;28ab	00		.
	defb 000h		;28ac	00		.
	defb 000h		;28ad	00		.
	defb 000h		;28ae	00		.
	defb 000h		;28af	00		.
	defb 022h		;28b0	22		"
	defb 055h		;28b1	55		U
	defb 0dbh		;28b2	db		.
	defb 010h		;28b3	10		.
	defb 010h		;28b4	10		.
	defb 010h		;28b5	10		.
	defb 014h		;28b6	14		.
	defb 000h		;28b7	00		.
	defb 000h		;28b8	00		.
	defb 014h		;28b9	14		.
	defb 014h		;28ba	14		.
	defb 000h		;28bb	00		.
	defb 014h		;28bc	14		.
	defb 014h		;28bd	14		.
	defb 010h		;28be	10		.
	defb 000h		;28bf	00		.
	defb 010h		;28c0	10		.
	defb 010h		;28c1	10		.
	defb 000h		;28c2	00		.
	defb 010h		;28c3	10		.
	defb 000h		;28c4	00		.
	defb 010h		;28c5	10		.
	defb 010h		;28c6	10		.
	defb 014h		;28c7	14		.
	defb 014h		;28c8	14		.
	defb 000h		;28c9	00		.
	defb 014h		;28ca	14		.
	defb 000h		;28cb	00		.
	defb 014h		;28cc	14		.
	defb 000h		;28cd	00		.
	defb 014h		;28ce	14		.
	defb 010h		;28cf	10		.
	defb 014h		;28d0	14		.
	defb 000h		;28d1	00		.
	defb 000h		;28d2	00		.
	defb 014h		;28d3	14		.
	defb 010h		;28d4	10		.
	defb 000h		;28d5	00		.
	defb 000h		;28d6	00		.
	defb 014h		;28d7	14		.
	defb 010h		;28d8	10		.
	defb 010h		;28d9	10		.
	defb 000h		;28da	00		.
	defb 0ffh		;28db	ff		.
	defb 000h		;28dc	00		.
	defb 0f0h		;28dd	f0		.
	defb 00fh		;28de	0f		.
	defb 0ffh		;28df	ff		.
	defb 000h		;28e0	00		.
	defb 000h		;28e1	00		.
	defb 000h		;28e2	00		.
	defb 000h		;28e3	00		.
	defb 000h		;28e4	00		.
	defb 000h		;28e5	00		.
	defb 000h		;28e6	00		.
	defb 000h		;28e7	00		.
	defb 000h		;28e8	00		.
	defb 000h		;28e9	00		.
	defb 000h		;28ea	00		.
	defb 000h		;28eb	00		.
	defb 000h		;28ec	00		.
	defb 000h		;28ed	00		.
	defb 000h		;28ee	00		.
	defb 000h		;28ef	00		.
	defb 048h		;28f0	48		H
	defb 048h		;28f1	48		H
	defb 060h		;28f2	60		`
	defb 018h		;28f3	18		.
	defb 008h		;28f4	08		.
	defb 010h		;28f5	10		.
	defb 030h		;28f6	30		0
	defb 000h		;28f7	00		.
	defb 060h		;28f8	60		`
	defb 000h		;28f9	00		.
	defb 000h		;28fa	00		.
	defb 01fh		;28fb	1f		.
	defb 0a0h		;28fc	a0		.
	defb 060h		;28fd	60		`
	defb 000h		;28fe	00		.
	defb 000h		;28ff	00		.
	defb 000h		;2900	00		.
	defb 081h		;2901	81		.
	defb 0ffh		;2902	ff		.
	defb 0feh		;2903	fe		.
	defb 038h		;2904	38		8
	defb 07ch		;2905	7c		|
	defb 010h		;2906	10		.
	defb 000h		;2907	00		.
	defb 0ffh		;2908	ff		.
	defb 03ch		;2909	3c		<
	defb 0c3h		;290a	c3		.
	defb 007h		;290b	07		.
	defb 066h		;290c	66		f
	defb 033h		;290d	33		3
	defb 063h		;290e	63		c
	defb 0dbh		;290f	db		.
	defb 0e0h		;2910	e0		.
	defb 00eh		;2911	0e		.
	defb 03ch		;2912	3c		<
	defb 066h		;2913	66		f
	defb 0dbh		;2914	db		.
	defb 063h		;2915	63		c
	defb 000h		;2916	00		.
	defb 03ch		;2917	3c		<
	defb 03ch		;2918	3c		<
	defb 018h		;2919	18		.
	defb 018h		;291a	18		.
	defb 030h		;291b	30		0
	defb 000h		;291c	00		.
	defb 024h		;291d	24		$
	defb 018h		;291e	18		.
	defb 0ffh		;291f	ff		.
	defb 000h		;2920	00		.
	defb 078h		;2921	78		x
	defb 06ch		;2922	6c		l
	defb 06ch		;2923	6c		l
	defb 07ch		;2924	7c		|
	defb 0c6h		;2925	c6		.
	defb 06ch		;2926	6c		l
	defb 060h		;2927	60		`
	defb 030h		;2928	30		0
	defb 030h		;2929	30		0
	defb 066h		;292a	66		f
	defb 030h		;292b	30		0
	defb 000h		;292c	00		.
	defb 000h		;292d	00		.
	defb 000h		;292e	00		.
	defb 00ch		;292f	0c		.
	defb 0c6h		;2930	c6		.
	defb 070h		;2931	70		p
	defb 0cch		;2932	cc		.
	defb 0cch		;2933	cc		.
	defb 03ch		;2934	3c		<
	defb 0c0h		;2935	c0		.
	defb 060h		;2936	60		`
	defb 0cch		;2937	cc		.
	defb 0cch		;2938	cc		.
	defb 0cch		;2939	cc		.
	defb 030h		;293a	30		0
	defb 030h		;293b	30		0
	defb 030h		;293c	30		0
	defb 000h		;293d	00		.
	defb 030h		;293e	30		0
	defb 0cch		;293f	cc		.
	defb 0c6h		;2940	c6		.
	defb 078h		;2941	78		x
	defb 066h		;2942	66		f
	defb 066h		;2943	66		f
	defb 06ch		;2944	6c		l
	defb 062h		;2945	62		b
	defb 062h		;2946	62		b
	defb 066h		;2947	66		f
	defb 0cch		;2948	cc		.
	defb 030h		;2949	30		0
	defb 00ch		;294a	0c		.
	defb 066h		;294b	66		f
	defb 060h		;294c	60		`
	defb 0eeh		;294d	ee		.
	defb 0e6h		;294e	e6		.
	defb 06ch		;294f	6c		l
	defb 066h		;2950	66		f
	defb 0cch		;2951	cc		.
	defb 066h		;2952	66		f
	defb 0cch		;2953	cc		.
	defb 0b4h		;2954	b4		.
	defb 0cch		;2955	cc		.
	defb 0cch		;2956	cc		.
	defb 0c6h		;2957	c6		.
	defb 0c6h		;2958	c6		.
	defb 0cch		;2959	cc		.
	defb 0c6h		;295a	c6		.
	defb 060h		;295b	60		`
	defb 060h		;295c	60		`
	defb 018h		;295d	18		.
	defb 038h		;295e	38		8
	defb 000h		;295f	00		.
	defb 030h		;2960	30		0
	defb 000h		;2961	00		.
	defb 060h		;2962	60		`
	defb 000h		;2963	00		.
	defb 00ch		;2964	0c		.
	defb 000h		;2965	00		.
	defb 06ch		;2966	6c		l
	defb 000h		;2967	00		.
	defb 060h		;2968	60		`
	defb 000h		;2969	00		.
	defb 000h		;296a	00		.
	defb 060h		;296b	60		`
	defb 030h		;296c	30		0
	defb 000h		;296d	00		.
	defb 000h		;296e	00		.
	defb 000h		;296f	00		.
	defb 000h		;2970	00		.
	defb 000h		;2971	00		.
	defb 000h		;2972	00		.
	defb 000h		;2973	00		.
	defb 030h		;2974	30		0
	defb 000h		;2975	00		.
	defb 000h		;2976	00		.
	defb 000h		;2977	00		.
	defb 000h		;2978	00		.
	defb 000h		;2979	00		.
	defb 000h		;297a	00		.
	defb 030h		;297b	30		0
	defb 018h		;297c	18		.
	defb 030h		;297d	30		0
	defb 0dch		;297e	dc		.
	defb 010h		;297f	10		.
	defb 01eh		;2980	1e		.
	defb 0c0h		;2981	c0		.
	defb 0cch		;2982	cc		.
	defb 0c0h		;2983	c0		.
	defb 066h		;2984	66		f
	defb 0c0h		;2985	c0		.
	defb 0dbh		;2986	db		.
	defb 066h		;2987	66		f
	defb 0c6h		;2988	c6		.
	defb 0c6h		;2989	c6		.
	defb 0cch		;298a	cc		.
	defb 00eh		;298b	0e		.
	defb 0eeh		;298c	ee		.
	defb 0c6h		;298d	c6		.
	defb 0c6h		;298e	c6		.
	defb 0c6h		;298f	c6		.
	defb 0c6h		;2990	c6		.
	defb 0c6h		;2991	c6		.
	defb 030h		;2992	30		0
	defb 0c6h		;2993	c6		.
	defb 07eh		;2994	7e		~
	defb 066h		;2995	66		f
	defb 0cch		;2996	cc		.
	defb 0c6h		;2997	c6		.
	defb 0d6h		;2998	d6		.
	defb 0d6h		;2999	d6		.
	defb 030h		;299a	30		0
	defb 0c2h		;299b	c2		.
	defb 0c0h		;299c	c0		.
	defb 0c6h		;299d	c6		.
	defb 0dbh		;299e	db		.
	defb 0c6h		;299f	c6		.
	defb 000h		;29a0	00		.
	defb 078h		;29a1	78		x
	defb 000h		;29a2	00		.
	defb 000h		;29a3	00		.
	defb 000h		;29a4	00		.
	defb 000h		;29a5	00		.
	defb 000h		;29a6	00		.
	defb 000h		;29a7	00		.
	defb 000h		;29a8	00		.
	defb 000h		;29a9	00		.
	defb 000h		;29aa	00		.
	defb 000h		;29ab	00		.
	defb 000h		;29ac	00		.
	defb 000h		;29ad	00		.
	defb 000h		;29ae	00		.
	defb 000h		;29af	00		.
	defb 088h		;29b0	88		.
	defb 0aah		;29b1	aa		.
	defb 077h		;29b2	77		w
	defb 010h		;29b3	10		.
	defb 010h		;29b4	10		.
	defb 010h		;29b5	10		.
	defb 014h		;29b6	14		.
	defb 000h		;29b7	00		.
	defb 000h		;29b8	00		.
	defb 014h		;29b9	14		.
	defb 014h		;29ba	14		.
	defb 000h		;29bb	00		.
	defb 014h		;29bc	14		.
	defb 014h		;29bd	14		.
	defb 010h		;29be	10		.
	defb 000h		;29bf	00		.
	defb 010h		;29c0	10		.
	defb 010h		;29c1	10		.
	defb 000h		;29c2	00		.
	defb 010h		;29c3	10		.
	defb 000h		;29c4	00		.
	defb 010h		;29c5	10		.
	defb 010h		;29c6	10		.
	defb 014h		;29c7	14		.
	defb 014h		;29c8	14		.
	defb 000h		;29c9	00		.
	defb 014h		;29ca	14		.
	defb 000h		;29cb	00		.
	defb 014h		;29cc	14		.
	defb 000h		;29cd	00		.
	defb 014h		;29ce	14		.
	defb 010h		;29cf	10		.
	defb 014h		;29d0	14		.
	defb 000h		;29d1	00		.
	defb 000h		;29d2	00		.
	defb 014h		;29d3	14		.
	defb 010h		;29d4	10		.
	defb 000h		;29d5	00		.
	defb 000h		;29d6	00		.
	defb 014h		;29d7	14		.
	defb 010h		;29d8	10		.
	defb 010h		;29d9	10		.
	defb 000h		;29da	00		.
	defb 0ffh		;29db	ff		.
	defb 000h		;29dc	00		.
	defb 0f0h		;29dd	f0		.
	defb 00fh		;29de	0f		.
	defb 0ffh		;29df	ff		.
	defb 000h		;29e0	00		.
	defb 000h		;29e1	00		.
	defb 000h		;29e2	00		.
	defb 000h		;29e3	00		.
	defb 018h		;29e4	18		.
	defb 000h		;29e5	00		.
	defb 000h		;29e6	00		.
	defb 000h		;29e7	00		.
	defb 000h		;29e8	00		.
	defb 000h		;29e9	00		.
	defb 000h		;29ea	00		.
	defb 000h		;29eb	00		.
	defb 000h		;29ec	00		.
	defb 000h		;29ed	00		.
	defb 000h		;29ee	00		.
	defb 000h		;29ef	00		.
	defb 0fch		;29f0	fc		.
	defb 000h		;29f1	00		.
	defb 030h		;29f2	30		0
	defb 030h		;29f3	30		0
	defb 014h		;29f4	14		.
	defb 010h		;29f5	10		.
	defb 030h		;29f6	30		0
	defb 000h		;29f7	00		.
	defb 090h		;29f8	90		.
	defb 000h		;29f9	00		.
	defb 000h		;29fa	00		.
	defb 010h		;29fb	10		.
	defb 0d0h		;29fc	d0		.
	defb 090h		;29fd	90		.
	defb 000h		;29fe	00		.
	defb 000h		;29ff	00		.
	defb 000h		;2a00	00		.
	defb 0a5h		;2a01	a5		.
	defb 0dbh		;2a02	db		.
	defb 0feh		;2a03	fe		.
	defb 07ch		;2a04	7c		|
	defb 038h		;2a05	38		8
	defb 038h		;2a06	38		8
	defb 018h		;2a07	18		.
	defb 0e7h		;2a08	e7		.
	defb 066h		;2a09	66		f
	defb 099h		;2a0a	99		.
	defb 00fh		;2a0b	0f		.
	defb 066h		;2a0c	66		f
	defb 03fh		;2a0d	3f		?
	defb 07fh		;2a0e	7f		.
	defb 03ch		;2a0f	3c		<
	defb 0f8h		;2a10	f8		.
	defb 03eh		;2a11	3e		>
	defb 07eh		;2a12	7e		~
	defb 066h		;2a13	66		f
	defb 0dbh		;2a14	db		.
	defb 038h		;2a15	38		8
	defb 000h		;2a16	00		.
	defb 07eh		;2a17	7e		~
	defb 07eh		;2a18	7e		~
	defb 018h		;2a19	18		.
	defb 00ch		;2a1a	0c		.
	defb 060h		;2a1b	60		`
	defb 0c0h		;2a1c	c0		.
	defb 066h		;2a1d	66		f
	defb 03ch		;2a1e	3c		<
	defb 0ffh		;2a1f	ff		.
	defb 000h		;2a20	00		.
	defb 078h		;2a21	78		x
	defb 06ch		;2a22	6c		l
	defb 0feh		;2a23	fe		.
	defb 0c0h		;2a24	c0		.
	defb 0cch		;2a25	cc		.
	defb 038h		;2a26	38		8
	defb 0c0h		;2a27	c0		.
	defb 060h		;2a28	60		`
	defb 018h		;2a29	18		.
	defb 03ch		;2a2a	3c		<
	defb 030h		;2a2b	30		0
	defb 000h		;2a2c	00		.
	defb 000h		;2a2d	00		.
	defb 000h		;2a2e	00		.
	defb 018h		;2a2f	18		.
	defb 0ceh		;2a30	ce		.
	defb 030h		;2a31	30		0
	defb 00ch		;2a32	0c		.
	defb 00ch		;2a33	0c		.
	defb 06ch		;2a34	6c		l
	defb 0f8h		;2a35	f8		.
	defb 0c0h		;2a36	c0		.
	defb 00ch		;2a37	0c		.
	defb 0cch		;2a38	cc		.
	defb 0cch		;2a39	cc		.
	defb 030h		;2a3a	30		0
	defb 030h		;2a3b	30		0
	defb 060h		;2a3c	60		`
	defb 0fch		;2a3d	fc		.
	defb 018h		;2a3e	18		.
	defb 00ch		;2a3f	0c		.
	defb 0deh		;2a40	de		.
	defb 0cch		;2a41	cc		.
	defb 066h		;2a42	66		f
	defb 0c0h		;2a43	c0		.
	defb 066h		;2a44	66		f
	defb 068h		;2a45	68		h
	defb 068h		;2a46	68		h
	defb 0c0h		;2a47	c0		.
	defb 0cch		;2a48	cc		.
	defb 030h		;2a49	30		0
	defb 00ch		;2a4a	0c		.
	defb 06ch		;2a4b	6c		l
	defb 060h		;2a4c	60		`
	defb 0feh		;2a4d	fe		.
	defb 0f6h		;2a4e	f6		.
	defb 0c6h		;2a4f	c6		.
	defb 066h		;2a50	66		f
	defb 0cch		;2a51	cc		.
	defb 066h		;2a52	66		f
	defb 060h		;2a53	60		`
	defb 030h		;2a54	30		0
	defb 0cch		;2a55	cc		.
	defb 0cch		;2a56	cc		.
	defb 0c6h		;2a57	c6		.
	defb 06ch		;2a58	6c		l
	defb 0cch		;2a59	cc		.
	defb 08ch		;2a5a	8c		.
	defb 060h		;2a5b	60		`
	defb 030h		;2a5c	30		0
	defb 018h		;2a5d	18		.
	defb 06ch		;2a5e	6c		l
	defb 000h		;2a5f	00		.
	defb 018h		;2a60	18		.
	defb 078h		;2a61	78		x
	defb 060h		;2a62	60		`
	defb 078h		;2a63	78		x
	defb 00ch		;2a64	0c		.
	defb 078h		;2a65	78		x
	defb 060h		;2a66	60		`
	defb 076h		;2a67	76		v
	defb 06ch		;2a68	6c		l
	defb 070h		;2a69	70		p
	defb 00ch		;2a6a	0c		.
	defb 066h		;2a6b	66		f
	defb 030h		;2a6c	30		0
	defb 0cch		;2a6d	cc		.
	defb 0f8h		;2a6e	f8		.
	defb 078h		;2a6f	78		x
	defb 0dch		;2a70	dc		.
	defb 076h		;2a71	76		v
	defb 0dch		;2a72	dc		.
	defb 07ch		;2a73	7c		|
	defb 07ch		;2a74	7c		|
	defb 0cch		;2a75	cc		.
	defb 0cch		;2a76	cc		.
	defb 0c6h		;2a77	c6		.
	defb 0c6h		;2a78	c6		.
	defb 0cch		;2a79	cc		.
	defb 0fch		;2a7a	fc		.
	defb 030h		;2a7b	30		0
	defb 018h		;2a7c	18		.
	defb 030h		;2a7d	30		0
	defb 000h		;2a7e	00		.
	defb 038h		;2a7f	38		8
	defb 036h		;2a80	36		6
	defb 0c0h		;2a81	c0		.
	defb 0cch		;2a82	cc		.
	defb 0c0h		;2a83	c0		.
	defb 066h		;2a84	66		f
	defb 0c0h		;2a85	c0		.
	defb 07eh		;2a86	7e		~
	defb 006h		;2a87	06		.
	defb 0ceh		;2a88	ce		.
	defb 0ceh		;2a89	ce		.
	defb 0d8h		;2a8a	d8		.
	defb 01eh		;2a8b	1e		.
	defb 0feh		;2a8c	fe		.
	defb 0c6h		;2a8d	c6		.
	defb 0c6h		;2a8e	c6		.
	defb 0c6h		;2a8f	c6		.
	defb 0c6h		;2a90	c6		.
	defb 0c0h		;2a91	c0		.
	defb 030h		;2a92	30		0
	defb 0c6h		;2a93	c6		.
	defb 0dbh		;2a94	db		.
	defb 03ch		;2a95	3c		<
	defb 0cch		;2a96	cc		.
	defb 0c6h		;2a97	c6		.
	defb 0d6h		;2a98	d6		.
	defb 0d6h		;2a99	d6		.
	defb 030h		;2a9a	30		0
	defb 0c2h		;2a9b	c2		.
	defb 0c0h		;2a9c	c0		.
	defb 006h		;2a9d	06		.
	defb 0dbh		;2a9e	db		.
	defb 0c6h		;2a9f	c6		.
	defb 078h		;2aa0	78		x
	defb 0c0h		;2aa1	c0		.
	defb 0f8h		;2aa2	f8		.
	defb 0fch		;2aa3	fc		.
	defb 07eh		;2aa4	7e		~
	defb 078h		;2aa5	78		x
	defb 0dbh		;2aa6	db		.
	defb 078h		;2aa7	78		x
	defb 0cch		;2aa8	cc		.
	defb 0cch		;2aa9	cc		.
	defb 0cch		;2aaa	cc		.
	defb 00eh		;2aab	0e		.
	defb 0c6h		;2aac	c6		.
	defb 0cch		;2aad	cc		.
	defb 078h		;2aae	78		x
	defb 0fch		;2aaf	fc		.
	defb 022h		;2ab0	22		"
	defb 055h		;2ab1	55		U
	defb 0dbh		;2ab2	db		.
	defb 010h		;2ab3	10		.
	defb 010h		;2ab4	10		.
	defb 0f0h		;2ab5	f0		.
	defb 014h		;2ab6	14		.
	defb 000h		;2ab7	00		.
	defb 0f0h		;2ab8	f0		.
	defb 0f4h		;2ab9	f4		.
	defb 014h		;2aba	14		.
	defb 0fch		;2abb	fc		.
	defb 0f4h		;2abc	f4		.
	defb 014h		;2abd	14		.
	defb 0f0h		;2abe	f0		.
	defb 000h		;2abf	00		.
	defb 010h		;2ac0	10		.
	defb 010h		;2ac1	10		.
	defb 000h		;2ac2	00		.
	defb 010h		;2ac3	10		.
	defb 000h		;2ac4	00		.
	defb 010h		;2ac5	10		.
	defb 01fh		;2ac6	1f		.
	defb 014h		;2ac7	14		.
	defb 017h		;2ac8	17		.
	defb 01fh		;2ac9	1f		.
	defb 0f7h		;2aca	f7		.
	defb 0ffh		;2acb	ff		.
	defb 017h		;2acc	17		.
	defb 0ffh		;2acd	ff		.
	defb 0f7h		;2ace	f7		.
	defb 0ffh		;2acf	ff		.
	defb 014h		;2ad0	14		.
	defb 0ffh		;2ad1	ff		.
	defb 000h		;2ad2	00		.
	defb 014h		;2ad3	14		.
	defb 01fh		;2ad4	1f		.
	defb 01fh		;2ad5	1f		.
	defb 000h		;2ad6	00		.
	defb 014h		;2ad7	14		.
	defb 0ffh		;2ad8	ff		.
	defb 010h		;2ad9	10		.
	defb 000h		;2ada	00		.
	defb 0ffh		;2adb	ff		.
	defb 000h		;2adc	00		.
	defb 0f0h		;2add	f0		.
	defb 00fh		;2ade	0f		.
	defb 0ffh		;2adf	ff		.
	defb 0f8h		;2ae0	f8		.
	defb 078h		;2ae1	78		x
	defb 0fch		;2ae2	fc		.
	defb 0cch		;2ae3	cc		.
	defb 07eh		;2ae4	7e		~
	defb 0c6h		;2ae5	c6		.
	defb 0cch		;2ae6	cc		.
	defb 0cch		;2ae7	cc		.
	defb 0d6h		;2ae8	d6		.
	defb 0d6h		;2ae9	d6		.
	defb 0f0h		;2aea	f0		.
	defb 0c2h		;2aeb	c2		.
	defb 0c0h		;2aec	c0		.
	defb 07ch		;2aed	7c		|
	defb 0ceh		;2aee	ce		.
	defb 07ch		;2aef	7c		|
	defb 0c0h		;2af0	c0		.
	defb 078h		;2af1	78		x
	defb 018h		;2af2	18		.
	defb 060h		;2af3	60		`
	defb 010h		;2af4	10		.
	defb 010h		;2af5	10		.
	defb 000h		;2af6	00		.
	defb 064h		;2af7	64		d
	defb 090h		;2af8	90		.
	defb 000h		;2af9	00		.
	defb 000h		;2afa	00		.
	defb 010h		;2afb	10		.
	defb 090h		;2afc	90		.
	defb 020h		;2afd	20		 
	defb 03ch		;2afe	3c		<
	defb 000h		;2aff	00		.
	defb 000h		;2b00	00		.
	defb 081h		;2b01	81		.
	defb 0ffh		;2b02	ff		.
	defb 0feh		;2b03	fe		.
	defb 0feh		;2b04	fe		.
	defb 0feh		;2b05	fe		.
	defb 07ch		;2b06	7c		|
	defb 03ch		;2b07	3c		<
	defb 0c3h		;2b08	c3		.
	defb 042h		;2b09	42		B
	defb 0bdh		;2b0a	bd		.
	defb 07dh		;2b0b	7d		}
	defb 066h		;2b0c	66		f
	defb 030h		;2b0d	30		0
	defb 063h		;2b0e	63		c
	defb 0e7h		;2b0f	e7		.
	defb 0feh		;2b10	fe		.
	defb 0feh		;2b11	fe		.
	defb 018h		;2b12	18		.
	defb 066h		;2b13	66		f
	defb 07bh		;2b14	7b		{
	defb 06ch		;2b15	6c		l
	defb 000h		;2b16	00		.
	defb 018h		;2b17	18		.
	defb 018h		;2b18	18		.
	defb 018h		;2b19	18		.
	defb 0feh		;2b1a	fe		.
	defb 0feh		;2b1b	fe		.
	defb 0c0h		;2b1c	c0		.
	defb 0ffh		;2b1d	ff		.
	defb 07eh		;2b1e	7e		~
	defb 07eh		;2b1f	7e		~
	defb 000h		;2b20	00		.
	defb 030h		;2b21	30		0
	defb 000h		;2b22	00		.
	defb 06ch		;2b23	6c		l
	defb 078h		;2b24	78		x
	defb 018h		;2b25	18		.
	defb 076h		;2b26	76		v
	defb 000h		;2b27	00		.
	defb 060h		;2b28	60		`
	defb 018h		;2b29	18		.
	defb 0ffh		;2b2a	ff		.
	defb 0fch		;2b2b	fc		.
	defb 000h		;2b2c	00		.
	defb 0fch		;2b2d	fc		.
	defb 000h		;2b2e	00		.
	defb 030h		;2b2f	30		0
	defb 0deh		;2b30	de		.
	defb 030h		;2b31	30		0
	defb 038h		;2b32	38		8
	defb 038h		;2b33	38		8
	defb 0cch		;2b34	cc		.
	defb 00ch		;2b35	0c		.
	defb 0f8h		;2b36	f8		.
	defb 018h		;2b37	18		.
	defb 078h		;2b38	78		x
	defb 07ch		;2b39	7c		|
	defb 000h		;2b3a	00		.
	defb 000h		;2b3b	00		.
	defb 0c0h		;2b3c	c0		.
	defb 000h		;2b3d	00		.
	defb 00ch		;2b3e	0c		.
	defb 018h		;2b3f	18		.
	defb 0deh		;2b40	de		.
	defb 0cch		;2b41	cc		.
	defb 07ch		;2b42	7c		|
	defb 0c0h		;2b43	c0		.
	defb 066h		;2b44	66		f
	defb 078h		;2b45	78		x
	defb 078h		;2b46	78		x
	defb 0c0h		;2b47	c0		.
	defb 0fch		;2b48	fc		.
	defb 030h		;2b49	30		0
	defb 00ch		;2b4a	0c		.
	defb 078h		;2b4b	78		x
	defb 060h		;2b4c	60		`
	defb 0feh		;2b4d	fe		.
	defb 0deh		;2b4e	de		.
	defb 0c6h		;2b4f	c6		.
	defb 07ch		;2b50	7c		|
	defb 0cch		;2b51	cc		.
	defb 07ch		;2b52	7c		|
	defb 030h		;2b53	30		0
	defb 030h		;2b54	30		0
	defb 0cch		;2b55	cc		.
	defb 0cch		;2b56	cc		.
	defb 0d6h		;2b57	d6		.
	defb 038h		;2b58	38		8
	defb 078h		;2b59	78		x
	defb 018h		;2b5a	18		.
	defb 060h		;2b5b	60		`
	defb 018h		;2b5c	18		.
	defb 018h		;2b5d	18		.
	defb 0c6h		;2b5e	c6		.
	defb 000h		;2b5f	00		.
	defb 000h		;2b60	00		.
	defb 00ch		;2b61	0c		.
	defb 07ch		;2b62	7c		|
	defb 0cch		;2b63	cc		.
	defb 07ch		;2b64	7c		|
	defb 0cch		;2b65	cc		.
	defb 0f0h		;2b66	f0		.
	defb 0cch		;2b67	cc		.
	defb 076h		;2b68	76		v
	defb 030h		;2b69	30		0
	defb 00ch		;2b6a	0c		.
	defb 06ch		;2b6b	6c		l
	defb 030h		;2b6c	30		0
	defb 0feh		;2b6d	fe		.
	defb 0cch		;2b6e	cc		.
	defb 0cch		;2b6f	cc		.
	defb 066h		;2b70	66		f
	defb 0cch		;2b71	cc		.
	defb 076h		;2b72	76		v
	defb 0c0h		;2b73	c0		.
	defb 030h		;2b74	30		0
	defb 0cch		;2b75	cc		.
	defb 0cch		;2b76	cc		.
	defb 0d6h		;2b77	d6		.
	defb 06ch		;2b78	6c		l
	defb 0cch		;2b79	cc		.
	defb 098h		;2b7a	98		.
	defb 0e0h		;2b7b	e0		.
	defb 000h		;2b7c	00		.
	defb 01ch		;2b7d	1c		.
	defb 000h		;2b7e	00		.
	defb 06ch		;2b7f	6c		l
	defb 066h		;2b80	66		f
	defb 0fch		;2b81	fc		.
	defb 0fch		;2b82	fc		.
	defb 0c0h		;2b83	c0		.
	defb 066h		;2b84	66		f
	defb 0f8h		;2b85	f8		.
	defb 018h		;2b86	18		.
	defb 03ch		;2b87	3c		<
	defb 0deh		;2b88	de		.
	defb 0deh		;2b89	de		.
	defb 0f8h		;2b8a	f8		.
	defb 036h		;2b8b	36		6
	defb 0d6h		;2b8c	d6		.
	defb 0feh		;2b8d	fe		.
	defb 0c6h		;2b8e	c6		.
	defb 0c6h		;2b8f	c6		.
	defb 0fch		;2b90	fc		.
	defb 0c0h		;2b91	c0		.
	defb 030h		;2b92	30		0
	defb 07eh		;2b93	7e		~
	defb 0dbh		;2b94	db		.
	defb 018h		;2b95	18		.
	defb 0cch		;2b96	cc		.
	defb 07eh		;2b97	7e		~
	defb 0d6h		;2b98	d6		.
	defb 0d6h		;2b99	d6		.
	defb 03eh		;2b9a	3e		>
	defb 0f2h		;2b9b	f2		.
	defb 0fch		;2b9c	fc		.
	defb 01eh		;2b9d	1e		.
	defb 0fbh		;2b9e	fb		.
	defb 07eh		;2b9f	7e		~
	defb 00ch		;2ba0	0c		.
	defb 0f8h		;2ba1	f8		.
	defb 0cch		;2ba2	cc		.
	defb 0c0h		;2ba3	c0		.
	defb 066h		;2ba4	66		f
	defb 0cch		;2ba5	cc		.
	defb 07eh		;2ba6	7e		~
	defb 0cch		;2ba7	cc		.
	defb 0cch		;2ba8	cc		.
	defb 0cch		;2ba9	cc		.
	defb 0d8h		;2baa	d8		.
	defb 01eh		;2bab	1e		.
	defb 0eeh		;2bac	ee		.
	defb 0cch		;2bad	cc		.
	defb 0cch		;2bae	cc		.
	defb 0cch		;2baf	cc		.
	defb 088h		;2bb0	88		.
	defb 0aah		;2bb1	aa		.
	defb 0eeh		;2bb2	ee		.
	defb 010h		;2bb3	10		.
	defb 010h		;2bb4	10		.
	defb 010h		;2bb5	10		.
	defb 014h		;2bb6	14		.
	defb 000h		;2bb7	00		.
	defb 010h		;2bb8	10		.
	defb 004h		;2bb9	04		.
	defb 014h		;2bba	14		.
	defb 004h		;2bbb	04		.
	defb 004h		;2bbc	04		.
	defb 014h		;2bbd	14		.
	defb 010h		;2bbe	10		.
	defb 000h		;2bbf	00		.
	defb 010h		;2bc0	10		.
	defb 010h		;2bc1	10		.
	defb 000h		;2bc2	00		.
	defb 010h		;2bc3	10		.
	defb 000h		;2bc4	00		.
	defb 010h		;2bc5	10		.
	defb 010h		;2bc6	10		.
	defb 014h		;2bc7	14		.
	defb 010h		;2bc8	10		.
	defb 010h		;2bc9	10		.
	defb 000h		;2bca	00		.
	defb 000h		;2bcb	00		.
	defb 010h		;2bcc	10		.
	defb 000h		;2bcd	00		.
	defb 000h		;2bce	00		.
	defb 000h		;2bcf	00		.
	defb 014h		;2bd0	14		.
	defb 000h		;2bd1	00		.
	defb 000h		;2bd2	00		.
	defb 014h		;2bd3	14		.
	defb 010h		;2bd4	10		.
	defb 010h		;2bd5	10		.
	defb 000h		;2bd6	00		.
	defb 014h		;2bd7	14		.
	defb 010h		;2bd8	10		.
	defb 010h		;2bd9	10		.
	defb 000h		;2bda	00		.
	defb 0ffh		;2bdb	ff		.
	defb 000h		;2bdc	00		.
	defb 0f0h		;2bdd	f0		.
	defb 00fh		;2bde	0f		.
	defb 0ffh		;2bdf	ff		.
	defb 0cch		;2be0	cc		.
	defb 0cch		;2be1	cc		.
	defb 030h		;2be2	30		0
	defb 0cch		;2be3	cc		.
	defb 0dbh		;2be4	db		.
	defb 06ch		;2be5	6c		l
	defb 0cch		;2be6	cc		.
	defb 0cch		;2be7	cc		.
	defb 0d6h		;2be8	d6		.
	defb 0d6h		;2be9	d6		.
	defb 030h		;2bea	30		0
	defb 0c2h		;2beb	c2		.
	defb 0c0h		;2bec	c0		.
	defb 0c6h		;2bed	c6		.
	defb 0dbh		;2bee	db		.
	defb 0cch		;2bef	cc		.
	defb 0f8h		;2bf0	f8		.
	defb 0cch		;2bf1	cc		.
	defb 030h		;2bf2	30		0
	defb 030h		;2bf3	30		0
	defb 010h		;2bf4	10		.
	defb 010h		;2bf5	10		.
	defb 0fch		;2bf6	fc		.
	defb 098h		;2bf7	98		.
	defb 060h		;2bf8	60		`
	defb 030h		;2bf9	30		0
	defb 000h		;2bfa	00		.
	defb 090h		;2bfb	90		.
	defb 090h		;2bfc	90		.
	defb 040h		;2bfd	40		@
	defb 03ch		;2bfe	3c		<
	defb 000h		;2bff	00		.
	defb 000h		;2c00	00		.
	defb 0bdh		;2c01	bd		.
	defb 0c3h		;2c02	c3		.
	defb 07ch		;2c03	7c		|
	defb 07ch		;2c04	7c		|
	defb 0feh		;2c05	fe		.
	defb 0feh		;2c06	fe		.
	defb 03ch		;2c07	3c		<
	defb 0c3h		;2c08	c3		.
	defb 042h		;2c09	42		B
	defb 0bdh		;2c0a	bd		.
	defb 0cch		;2c0b	cc		.
	defb 03ch		;2c0c	3c		<
	defb 030h		;2c0d	30		0
	defb 063h		;2c0e	63		c
	defb 0e7h		;2c0f	e7		.
	defb 0f8h		;2c10	f8		.
	defb 03eh		;2c11	3e		>
	defb 018h		;2c12	18		.
	defb 066h		;2c13	66		f
	defb 01bh		;2c14	1b		.
	defb 06ch		;2c15	6c		l
	defb 07eh		;2c16	7e		~
	defb 07eh		;2c17	7e		~
	defb 018h		;2c18	18		.
	defb 07eh		;2c19	7e		~
	defb 00ch		;2c1a	0c		.
	defb 060h		;2c1b	60		`
	defb 0c0h		;2c1c	c0		.
	defb 066h		;2c1d	66		f
	defb 0ffh		;2c1e	ff		.
	defb 03ch		;2c1f	3c		<
	defb 000h		;2c20	00		.
	defb 030h		;2c21	30		0
	defb 000h		;2c22	00		.
	defb 0feh		;2c23	fe		.
	defb 00ch		;2c24	0c		.
	defb 030h		;2c25	30		0
	defb 0dch		;2c26	dc		.
	defb 000h		;2c27	00		.
	defb 060h		;2c28	60		`
	defb 018h		;2c29	18		.
	defb 03ch		;2c2a	3c		<
	defb 030h		;2c2b	30		0
	defb 000h		;2c2c	00		.
	defb 000h		;2c2d	00		.
	defb 000h		;2c2e	00		.
	defb 060h		;2c2f	60		`
	defb 0f6h		;2c30	f6		.
	defb 030h		;2c31	30		0
	defb 060h		;2c32	60		`
	defb 00ch		;2c33	0c		.
	defb 0feh		;2c34	fe		.
	defb 00ch		;2c35	0c		.
	defb 0cch		;2c36	cc		.
	defb 030h		;2c37	30		0
	defb 0cch		;2c38	cc		.
	defb 00ch		;2c39	0c		.
	defb 000h		;2c3a	00		.
	defb 000h		;2c3b	00		.
	defb 060h		;2c3c	60		`
	defb 000h		;2c3d	00		.
	defb 018h		;2c3e	18		.
	defb 030h		;2c3f	30		0
	defb 0deh		;2c40	de		.
	defb 0fch		;2c41	fc		.
	defb 066h		;2c42	66		f
	defb 0c0h		;2c43	c0		.
	defb 066h		;2c44	66		f
	defb 068h		;2c45	68		h
	defb 068h		;2c46	68		h
	defb 0ceh		;2c47	ce		.
	defb 0cch		;2c48	cc		.
	defb 030h		;2c49	30		0
	defb 0cch		;2c4a	cc		.
	defb 06ch		;2c4b	6c		l
	defb 062h		;2c4c	62		b
	defb 0d6h		;2c4d	d6		.
	defb 0ceh		;2c4e	ce		.
	defb 0c6h		;2c4f	c6		.
	defb 060h		;2c50	60		`
	defb 0dch		;2c51	dc		.
	defb 06ch		;2c52	6c		l
	defb 018h		;2c53	18		.
	defb 030h		;2c54	30		0
	defb 0cch		;2c55	cc		.
	defb 0cch		;2c56	cc		.
	defb 0feh		;2c57	fe		.
	defb 038h		;2c58	38		8
	defb 030h		;2c59	30		0
	defb 032h		;2c5a	32		2
	defb 060h		;2c5b	60		`
	defb 00ch		;2c5c	0c		.
	defb 018h		;2c5d	18		.
	defb 000h		;2c5e	00		.
	defb 000h		;2c5f	00		.
	defb 000h		;2c60	00		.
	defb 07ch		;2c61	7c		|
	defb 066h		;2c62	66		f
	defb 0c0h		;2c63	c0		.
	defb 0cch		;2c64	cc		.
	defb 0fch		;2c65	fc		.
	defb 060h		;2c66	60		`
	defb 0cch		;2c67	cc		.
	defb 066h		;2c68	66		f
	defb 030h		;2c69	30		0
	defb 00ch		;2c6a	0c		.
	defb 078h		;2c6b	78		x
	defb 030h		;2c6c	30		0
	defb 0feh		;2c6d	fe		.
	defb 0cch		;2c6e	cc		.
	defb 0cch		;2c6f	cc		.
	defb 066h		;2c70	66		f
	defb 0cch		;2c71	cc		.
	defb 066h		;2c72	66		f
	defb 078h		;2c73	78		x
	defb 030h		;2c74	30		0
	defb 0cch		;2c75	cc		.
	defb 0cch		;2c76	cc		.
	defb 0feh		;2c77	fe		.
	defb 038h		;2c78	38		8
	defb 0cch		;2c79	cc		.
	defb 030h		;2c7a	30		0
	defb 030h		;2c7b	30		0
	defb 018h		;2c7c	18		.
	defb 030h		;2c7d	30		0
	defb 000h		;2c7e	00		.
	defb 0c6h		;2c7f	c6		.
	defb 0feh		;2c80	fe		.
	defb 0c6h		;2c81	c6		.
	defb 0c6h		;2c82	c6		.
	defb 0c0h		;2c83	c0		.
	defb 066h		;2c84	66		f
	defb 0c0h		;2c85	c0		.
	defb 07eh		;2c86	7e		~
	defb 006h		;2c87	06		.
	defb 0f6h		;2c88	f6		.
	defb 0f6h		;2c89	f6		.
	defb 0cch		;2c8a	cc		.
	defb 066h		;2c8b	66		f
	defb 0c6h		;2c8c	c6		.
	defb 0c6h		;2c8d	c6		.
	defb 0c6h		;2c8e	c6		.
	defb 0c6h		;2c8f	c6		.
	defb 0c0h		;2c90	c0		.
	defb 0c0h		;2c91	c0		.
	defb 030h		;2c92	30		0
	defb 006h		;2c93	06		.
	defb 0dbh		;2c94	db		.
	defb 03ch		;2c95	3c		<
	defb 0cch		;2c96	cc		.
	defb 006h		;2c97	06		.
	defb 0d6h		;2c98	d6		.
	defb 0d6h		;2c99	d6		.
	defb 033h		;2c9a	33		3
	defb 0dah		;2c9b	da		.
	defb 0c6h		;2c9c	c6		.
	defb 006h		;2c9d	06		.
	defb 0dbh		;2c9e	db		.
	defb 036h		;2c9f	36		6
	defb 07ch		;2ca0	7c		|
	defb 0cch		;2ca1	cc		.
	defb 0f8h		;2ca2	f8		.
	defb 0c0h		;2ca3	c0		.
	defb 066h		;2ca4	66		f
	defb 0fch		;2ca5	fc		.
	defb 018h		;2ca6	18		.
	defb 018h		;2ca7	18		.
	defb 0dch		;2ca8	dc		.
	defb 0dch		;2ca9	dc		.
	defb 0f0h		;2caa	f0		.
	defb 036h		;2cab	36		6
	defb 0d6h		;2cac	d6		.
	defb 0fch		;2cad	fc		.
	defb 0cch		;2cae	cc		.
	defb 0cch		;2caf	cc		.
	defb 022h		;2cb0	22		"
	defb 055h		;2cb1	55		U
	defb 0dbh		;2cb2	db		.
	defb 010h		;2cb3	10		.
	defb 0f0h		;2cb4	f0		.
	defb 0f0h		;2cb5	f0		.
	defb 0f4h		;2cb6	f4		.
	defb 0fch		;2cb7	fc		.
	defb 0f0h		;2cb8	f0		.
	defb 0f4h		;2cb9	f4		.
	defb 014h		;2cba	14		.
	defb 0f4h		;2cbb	f4		.
	defb 0fch		;2cbc	fc		.
	defb 0fch		;2cbd	fc		.
	defb 0f0h		;2cbe	f0		.
	defb 0f0h		;2cbf	f0		.
	defb 01fh		;2cc0	1f		.
	defb 0ffh		;2cc1	ff		.
	defb 0ffh		;2cc2	ff		.
	defb 01fh		;2cc3	1f		.
	defb 0ffh		;2cc4	ff		.
	defb 0ffh		;2cc5	ff		.
	defb 01fh		;2cc6	1f		.
	defb 017h		;2cc7	17		.
	defb 01fh		;2cc8	1f		.
	defb 017h		;2cc9	17		.
	defb 0ffh		;2cca	ff		.
	defb 0f7h		;2ccb	f7		.
	defb 017h		;2ccc	17		.
	defb 0ffh		;2ccd	ff		.
	defb 0f7h		;2cce	f7		.
	defb 0ffh		;2ccf	ff		.
	defb 0ffh		;2cd0	ff		.
	defb 0ffh		;2cd1	ff		.
	defb 0ffh		;2cd2	ff		.
	defb 01fh		;2cd3	1f		.
	defb 01fh		;2cd4	1f		.
	defb 01fh		;2cd5	1f		.
	defb 01fh		;2cd6	1f		.
	defb 0ffh		;2cd7	ff		.
	defb 0ffh		;2cd8	ff		.
	defb 0f0h		;2cd9	f0		.
	defb 01fh		;2cda	1f		.
	defb 0ffh		;2cdb	ff		.
	defb 0ffh		;2cdc	ff		.
	defb 0f0h		;2cdd	f0		.
	defb 00fh		;2cde	0f		.
	defb 000h		;2cdf	00		.
	defb 0cch		;2ce0	cc		.
	defb 0c0h		;2ce1	c0		.
	defb 030h		;2ce2	30		0
	defb 07ch		;2ce3	7c		|
	defb 0dbh		;2ce4	db		.
	defb 038h		;2ce5	38		8
	defb 0cch		;2ce6	cc		.
	defb 07ch		;2ce7	7c		|
	defb 0d6h		;2ce8	d6		.
	defb 0d6h		;2ce9	d6		.
	defb 03eh		;2cea	3e		>
	defb 0f2h		;2ceb	f2		.
	defb 0f8h		;2cec	f8		.
	defb 01eh		;2ced	1e		.
	defb 0fbh		;2cee	fb		.
	defb 07ch		;2cef	7c		|
	defb 0c0h		;2cf0	c0		.
	defb 0fch		;2cf1	fc		.
	defb 060h		;2cf2	60		`
	defb 018h		;2cf3	18		.
	defb 010h		;2cf4	10		.
	defb 010h		;2cf5	10		.
	defb 000h		;2cf6	00		.
	defb 000h		;2cf7	00		.
	defb 000h		;2cf8	00		.
	defb 030h		;2cf9	30		0
	defb 030h		;2cfa	30		0
	defb 050h		;2cfb	50		P
	defb 090h		;2cfc	90		.
	defb 0f0h		;2cfd	f0		.
	defb 03ch		;2cfe	3c		<
	defb 000h		;2cff	00		.
	defb 000h		;2d00	00		.
	defb 099h		;2d01	99		.
	defb 0e7h		;2d02	e7		.
	defb 038h		;2d03	38		8
	defb 038h		;2d04	38		8
	defb 0d6h		;2d05	d6		.
	defb 07ch		;2d06	7c		|
	defb 018h		;2d07	18		.
	defb 0e7h		;2d08	e7		.
	defb 066h		;2d09	66		f
	defb 099h		;2d0a	99		.
	defb 0cch		;2d0b	cc		.
	defb 018h		;2d0c	18		.
	defb 070h		;2d0d	70		p
	defb 067h		;2d0e	67		g
	defb 03ch		;2d0f	3c		<
	defb 0e0h		;2d10	e0		.
	defb 00eh		;2d11	0e		.
	defb 07eh		;2d12	7e		~
	defb 000h		;2d13	00		.
	defb 01bh		;2d14	1b		.
	defb 038h		;2d15	38		8
	defb 07eh		;2d16	7e		~
	defb 03ch		;2d17	3c		<
	defb 018h		;2d18	18		.
	defb 03ch		;2d19	3c		<
	defb 018h		;2d1a	18		.
	defb 030h		;2d1b	30		0
	defb 0feh		;2d1c	fe		.
	defb 024h		;2d1d	24		$
	defb 0ffh		;2d1e	ff		.
	defb 018h		;2d1f	18		.
	defb 000h		;2d20	00		.
	defb 000h		;2d21	00		.
	defb 000h		;2d22	00		.
	defb 06ch		;2d23	6c		l
	defb 0f8h		;2d24	f8		.
	defb 066h		;2d25	66		f
	defb 0cch		;2d26	cc		.
	defb 000h		;2d27	00		.
	defb 030h		;2d28	30		0
	defb 030h		;2d29	30		0
	defb 066h		;2d2a	66		f
	defb 030h		;2d2b	30		0
	defb 030h		;2d2c	30		0
	defb 000h		;2d2d	00		.
	defb 030h		;2d2e	30		0
	defb 0c0h		;2d2f	c0		.
	defb 0e6h		;2d30	e6		.
	defb 030h		;2d31	30		0
	defb 0cch		;2d32	cc		.
	defb 0cch		;2d33	cc		.
	defb 00ch		;2d34	0c		.
	defb 0cch		;2d35	cc		.
	defb 0cch		;2d36	cc		.
	defb 030h		;2d37	30		0
	defb 0cch		;2d38	cc		.
	defb 018h		;2d39	18		.
	defb 030h		;2d3a	30		0
	defb 030h		;2d3b	30		0
	defb 030h		;2d3c	30		0
	defb 0fch		;2d3d	fc		.
	defb 030h		;2d3e	30		0
	defb 000h		;2d3f	00		.
	defb 0c0h		;2d40	c0		.
	defb 0cch		;2d41	cc		.
	defb 066h		;2d42	66		f
	defb 066h		;2d43	66		f
	defb 06ch		;2d44	6c		l
	defb 062h		;2d45	62		b
	defb 060h		;2d46	60		`
	defb 066h		;2d47	66		f
	defb 0cch		;2d48	cc		.
	defb 030h		;2d49	30		0
	defb 0cch		;2d4a	cc		.
	defb 066h		;2d4b	66		f
	defb 066h		;2d4c	66		f
	defb 0c6h		;2d4d	c6		.
	defb 0c6h		;2d4e	c6		.
	defb 06ch		;2d4f	6c		l
	defb 060h		;2d50	60		`
	defb 078h		;2d51	78		x
	defb 066h		;2d52	66		f
	defb 0cch		;2d53	cc		.
	defb 030h		;2d54	30		0
	defb 0cch		;2d55	cc		.
	defb 078h		;2d56	78		x
	defb 0eeh		;2d57	ee		.
	defb 06ch		;2d58	6c		l
	defb 030h		;2d59	30		0
	defb 066h		;2d5a	66		f
	defb 060h		;2d5b	60		`
	defb 006h		;2d5c	06		.
	defb 018h		;2d5d	18		.
	defb 000h		;2d5e	00		.
	defb 000h		;2d5f	00		.
	defb 000h		;2d60	00		.
	defb 0cch		;2d61	cc		.
	defb 066h		;2d62	66		f
	defb 0cch		;2d63	cc		.
	defb 0cch		;2d64	cc		.
	defb 0c0h		;2d65	c0		.
	defb 060h		;2d66	60		`
	defb 07ch		;2d67	7c		|
	defb 066h		;2d68	66		f
	defb 030h		;2d69	30		0
	defb 0cch		;2d6a	cc		.
	defb 06ch		;2d6b	6c		l
	defb 030h		;2d6c	30		0
	defb 0d6h		;2d6d	d6		.
	defb 0cch		;2d6e	cc		.
	defb 0cch		;2d6f	cc		.
	defb 07ch		;2d70	7c		|
	defb 07ch		;2d71	7c		|
	defb 060h		;2d72	60		`
	defb 00ch		;2d73	0c		.
	defb 034h		;2d74	34		4
	defb 0cch		;2d75	cc		.
	defb 078h		;2d76	78		x
	defb 0feh		;2d77	fe		.
	defb 06ch		;2d78	6c		l
	defb 07ch		;2d79	7c		|
	defb 064h		;2d7a	64		d
	defb 030h		;2d7b	30		0
	defb 018h		;2d7c	18		.
	defb 030h		;2d7d	30		0
	defb 000h		;2d7e	00		.
	defb 0c6h		;2d7f	c6		.
	defb 0c6h		;2d80	c6		.
	defb 0c6h		;2d81	c6		.
	defb 0c6h		;2d82	c6		.
	defb 0c0h		;2d83	c0		.
	defb 066h		;2d84	66		f
	defb 0c0h		;2d85	c0		.
	defb 0dbh		;2d86	db		.
	defb 0c6h		;2d87	c6		.
	defb 0e6h		;2d88	e6		.
	defb 0e6h		;2d89	e6		.
	defb 0c6h		;2d8a	c6		.
	defb 0c6h		;2d8b	c6		.
	defb 0c6h		;2d8c	c6		.
	defb 0c6h		;2d8d	c6		.
	defb 0c6h		;2d8e	c6		.
	defb 0c6h		;2d8f	c6		.
	defb 0c0h		;2d90	c0		.
	defb 0c6h		;2d91	c6		.
	defb 030h		;2d92	30		0
	defb 0c6h		;2d93	c6		.
	defb 07eh		;2d94	7e		~
	defb 066h		;2d95	66		f
	defb 0cch		;2d96	cc		.
	defb 006h		;2d97	06		.
	defb 0d6h		;2d98	d6		.
	defb 0d6h		;2d99	d6		.
	defb 033h		;2d9a	33		3
	defb 0dah		;2d9b	da		.
	defb 0c6h		;2d9c	c6		.
	defb 0c6h		;2d9d	c6		.
	defb 0dbh		;2d9e	db		.
	defb 066h		;2d9f	66		f
	defb 0cch		;2da0	cc		.
	defb 0cch		;2da1	cc		.
	defb 0c6h		;2da2	c6		.
	defb 0c0h		;2da3	c0		.
	defb 066h		;2da4	66		f
	defb 0c0h		;2da5	c0		.
	defb 07eh		;2da6	7e		~
	defb 0cch		;2da7	cc		.
	defb 0ech		;2da8	ec		.
	defb 0ech		;2da9	ec		.
	defb 0cch		;2daa	cc		.
	defb 066h		;2dab	66		f
	defb 0c6h		;2dac	c6		.
	defb 0cch		;2dad	cc		.
	defb 0cch		;2dae	cc		.
	defb 0cch		;2daf	cc		.
	defb 088h		;2db0	88		.
	defb 0aah		;2db1	aa		.
	defb 077h		;2db2	77		w
	defb 010h		;2db3	10		.
	defb 010h		;2db4	10		.
	defb 010h		;2db5	10		.
	defb 014h		;2db6	14		.
	defb 014h		;2db7	14		.
	defb 010h		;2db8	10		.
	defb 014h		;2db9	14		.
	defb 014h		;2dba	14		.
	defb 014h		;2dbb	14		.
	defb 000h		;2dbc	00		.
	defb 000h		;2dbd	00		.
	defb 000h		;2dbe	00		.
	defb 010h		;2dbf	10		.
	defb 000h		;2dc0	00		.
	defb 000h		;2dc1	00		.
	defb 010h		;2dc2	10		.
	defb 010h		;2dc3	10		.
	defb 000h		;2dc4	00		.
	defb 010h		;2dc5	10		.
	defb 010h		;2dc6	10		.
	defb 014h		;2dc7	14		.
	defb 000h		;2dc8	00		.
	defb 014h		;2dc9	14		.
	defb 000h		;2dca	00		.
	defb 014h		;2dcb	14		.
	defb 014h		;2dcc	14		.
	defb 000h		;2dcd	00		.
	defb 014h		;2dce	14		.
	defb 000h		;2dcf	00		.
	defb 000h		;2dd0	00		.
	defb 010h		;2dd1	10		.
	defb 014h		;2dd2	14		.
	defb 000h		;2dd3	00		.
	defb 000h		;2dd4	00		.
	defb 010h		;2dd5	10		.
	defb 014h		;2dd6	14		.
	defb 014h		;2dd7	14		.
	defb 010h		;2dd8	10		.
	defb 000h		;2dd9	00		.
	defb 010h		;2dda	10		.
	defb 0ffh		;2ddb	ff		.
	defb 0ffh		;2ddc	ff		.
	defb 0f0h		;2ddd	f0		.
	defb 00fh		;2dde	0f		.
	defb 000h		;2ddf	00		.
	defb 0f8h		;2de0	f8		.
	defb 0cch		;2de1	cc		.
	defb 030h		;2de2	30		0
	defb 00ch		;2de3	0c		.
	defb 07eh		;2de4	7e		~
	defb 06ch		;2de5	6c		l
	defb 0cch		;2de6	cc		.
	defb 00ch		;2de7	0c		.
	defb 0d6h		;2de8	d6		.
	defb 0d6h		;2de9	d6		.
	defb 033h		;2dea	33		3
	defb 0dah		;2deb	da		.
	defb 0cch		;2dec	cc		.
	defb 0c6h		;2ded	c6		.
	defb 0dbh		;2dee	db		.
	defb 06ch		;2def	6c		l
	defb 0c0h		;2df0	c0		.
	defb 0c0h		;2df1	c0		.
	defb 000h		;2df2	00		.
	defb 000h		;2df3	00		.
	defb 010h		;2df4	10		.
	defb 050h		;2df5	50		P
	defb 030h		;2df6	30		0
	defb 064h		;2df7	64		d
	defb 000h		;2df8	00		.
	defb 000h		;2df9	00		.
	defb 000h		;2dfa	00		.
	defb 030h		;2dfb	30		0
	defb 000h		;2dfc	00		.
	defb 000h		;2dfd	00		.
	defb 03ch		;2dfe	3c		<
	defb 000h		;2dff	00		.
	defb 000h		;2e00	00		.
	defb 081h		;2e01	81		.
	defb 0ffh		;2e02	ff		.
	defb 010h		;2e03	10		.
	defb 010h		;2e04	10		.
	defb 010h		;2e05	10		.
	defb 010h		;2e06	10		.
	defb 000h		;2e07	00		.
	defb 0ffh		;2e08	ff		.
	defb 03ch		;2e09	3c		<
	defb 0c3h		;2e0a	c3		.
	defb 0cch		;2e0b	cc		.
	defb 07eh		;2e0c	7e		~
	defb 0f0h		;2e0d	f0		.
	defb 0e6h		;2e0e	e6		.
	defb 0dbh		;2e0f	db		.
	defb 080h		;2e10	80		.
	defb 002h		;2e11	02		.
	defb 03ch		;2e12	3c		<
	defb 066h		;2e13	66		f
	defb 01bh		;2e14	1b		.
	defb 0cch		;2e15	cc		.
	defb 07eh		;2e16	7e		~
	defb 018h		;2e17	18		.
	defb 018h		;2e18	18		.
	defb 018h		;2e19	18		.
	defb 000h		;2e1a	00		.
	defb 000h		;2e1b	00		.
	defb 000h		;2e1c	00		.
	defb 000h		;2e1d	00		.
	defb 000h		;2e1e	00		.
	defb 000h		;2e1f	00		.
	defb 000h		;2e20	00		.
	defb 030h		;2e21	30		0
	defb 000h		;2e22	00		.
	defb 06ch		;2e23	6c		l
	defb 030h		;2e24	30		0
	defb 0c6h		;2e25	c6		.
	defb 076h		;2e26	76		v
	defb 000h		;2e27	00		.
	defb 018h		;2e28	18		.
	defb 060h		;2e29	60		`
	defb 000h		;2e2a	00		.
	defb 000h		;2e2b	00		.
	defb 030h		;2e2c	30		0
	defb 000h		;2e2d	00		.
	defb 030h		;2e2e	30		0
	defb 080h		;2e2f	80		.
	defb 07ch		;2e30	7c		|
	defb 0fch		;2e31	fc		.
	defb 0fch		;2e32	fc		.
	defb 078h		;2e33	78		x
	defb 01eh		;2e34	1e		.
	defb 078h		;2e35	78		x
	defb 078h		;2e36	78		x
	defb 030h		;2e37	30		0
	defb 078h		;2e38	78		x
	defb 070h		;2e39	70		p
	defb 030h		;2e3a	30		0
	defb 030h		;2e3b	30		0
	defb 018h		;2e3c	18		.
	defb 000h		;2e3d	00		.
	defb 060h		;2e3e	60		`
	defb 030h		;2e3f	30		0
	defb 078h		;2e40	78		x
	defb 0cch		;2e41	cc		.
	defb 0fch		;2e42	fc		.
	defb 03ch		;2e43	3c		<
	defb 0f8h		;2e44	f8		.
	defb 0feh		;2e45	fe		.
	defb 0f0h		;2e46	f0		.
	defb 03eh		;2e47	3e		>
	defb 0cch		;2e48	cc		.
	defb 078h		;2e49	78		x
	defb 078h		;2e4a	78		x
	defb 0e6h		;2e4b	e6		.
	defb 0feh		;2e4c	fe		.
	defb 0c6h		;2e4d	c6		.
	defb 0c6h		;2e4e	c6		.
	defb 038h		;2e4f	38		8
	defb 0f0h		;2e50	f0		.
	defb 01ch		;2e51	1c		.
	defb 0e6h		;2e52	e6		.
	defb 078h		;2e53	78		x
	defb 078h		;2e54	78		x
	defb 0fch		;2e55	fc		.
	defb 030h		;2e56	30		0
	defb 0c6h		;2e57	c6		.
	defb 0c6h		;2e58	c6		.
	defb 078h		;2e59	78		x
	defb 0feh		;2e5a	fe		.
	defb 078h		;2e5b	78		x
	defb 002h		;2e5c	02		.
	defb 078h		;2e5d	78		x
	defb 000h		;2e5e	00		.
	defb 000h		;2e5f	00		.
	defb 000h		;2e60	00		.
	defb 076h		;2e61	76		v
	defb 0dch		;2e62	dc		.
	defb 078h		;2e63	78		x
	defb 076h		;2e64	76		v
	defb 078h		;2e65	78		x
	defb 0f0h		;2e66	f0		.
	defb 00ch		;2e67	0c		.
	defb 0e6h		;2e68	e6		.
	defb 078h		;2e69	78		x
	defb 0cch		;2e6a	cc		.
	defb 0e6h		;2e6b	e6		.
	defb 078h		;2e6c	78		x
	defb 0c6h		;2e6d	c6		.
	defb 0cch		;2e6e	cc		.
	defb 078h		;2e6f	78		x
	defb 060h		;2e70	60		`
	defb 00ch		;2e71	0c		.
	defb 0f0h		;2e72	f0		.
	defb 0f8h		;2e73	f8		.
	defb 018h		;2e74	18		.
	defb 076h		;2e75	76		v
	defb 030h		;2e76	30		0
	defb 06ch		;2e77	6c		l
	defb 0c6h		;2e78	c6		.
	defb 00ch		;2e79	0c		.
	defb 0fch		;2e7a	fc		.
	defb 01ch		;2e7b	1c		.
	defb 018h		;2e7c	18		.
	defb 0e0h		;2e7d	e0		.
	defb 000h		;2e7e	00		.
	defb 0feh		;2e7f	fe		.
	defb 0c6h		;2e80	c6		.
	defb 0fch		;2e81	fc		.
	defb 0fch		;2e82	fc		.
	defb 0c0h		;2e83	c0		.
	defb 0ffh		;2e84	ff		.
	defb 0feh		;2e85	fe		.
	defb 0dbh		;2e86	db		.
	defb 07ch		;2e87	7c		|
	defb 0c6h		;2e88	c6		.
	defb 0c6h		;2e89	c6		.
	defb 0c6h		;2e8a	c6		.
	defb 0c6h		;2e8b	c6		.
	defb 0c6h		;2e8c	c6		.
	defb 0c6h		;2e8d	c6		.
	defb 07ch		;2e8e	7c		|
	defb 0c6h		;2e8f	c6		.
	defb 0c0h		;2e90	c0		.
	defb 07ch		;2e91	7c		|
	defb 030h		;2e92	30		0
	defb 07ch		;2e93	7c		|
	defb 018h		;2e94	18		.
	defb 0c3h		;2e95	c3		.
	defb 0feh		;2e96	fe		.
	defb 006h		;2e97	06		.
	defb 0feh		;2e98	fe		.
	defb 0ffh		;2e99	ff		.
	defb 03eh		;2e9a	3e		>
	defb 0f2h		;2e9b	f2		.
	defb 0fch		;2e9c	fc		.
	defb 07ch		;2e9d	7c		|
	defb 0ceh		;2e9e	ce		.
	defb 0c6h		;2e9f	c6		.
	defb 07eh		;2ea0	7e		~
	defb 078h		;2ea1	78		x
	defb 0fch		;2ea2	fc		.
	defb 0c0h		;2ea3	c0		.
	defb 0ffh		;2ea4	ff		.
	defb 07ch		;2ea5	7c		|
	defb 0dbh		;2ea6	db		.
	defb 078h		;2ea7	78		x
	defb 0cch		;2ea8	cc		.
	defb 0cch		;2ea9	cc		.
	defb 0cch		;2eaa	cc		.
	defb 0c6h		;2eab	c6		.
	defb 0c6h		;2eac	c6		.
	defb 0cch		;2ead	cc		.
	defb 078h		;2eae	78		x
	defb 0cch		;2eaf	cc		.
	defb 022h		;2eb0	22		"
	defb 055h		;2eb1	55		U
	defb 0dbh		;2eb2	db		.
	defb 010h		;2eb3	10		.
	defb 010h		;2eb4	10		.
	defb 010h		;2eb5	10		.
	defb 014h		;2eb6	14		.
	defb 014h		;2eb7	14		.
	defb 010h		;2eb8	10		.
	defb 014h		;2eb9	14		.
	defb 014h		;2eba	14		.
	defb 014h		;2ebb	14		.
	defb 000h		;2ebc	00		.
	defb 000h		;2ebd	00		.
	defb 000h		;2ebe	00		.
	defb 010h		;2ebf	10		.
	defb 000h		;2ec0	00		.
	defb 000h		;2ec1	00		.
	defb 010h		;2ec2	10		.
	defb 010h		;2ec3	10		.
	defb 000h		;2ec4	00		.
	defb 010h		;2ec5	10		.
	defb 010h		;2ec6	10		.
	defb 014h		;2ec7	14		.
	defb 000h		;2ec8	00		.
	defb 014h		;2ec9	14		.
	defb 000h		;2eca	00		.
	defb 014h		;2ecb	14		.
	defb 014h		;2ecc	14		.
	defb 000h		;2ecd	00		.
	defb 014h		;2ece	14		.
	defb 000h		;2ecf	00		.
	defb 000h		;2ed0	00		.
	defb 010h		;2ed1	10		.
	defb 014h		;2ed2	14		.
	defb 000h		;2ed3	00		.
	defb 000h		;2ed4	00		.
	defb 010h		;2ed5	10		.
	defb 014h		;2ed6	14		.
	defb 014h		;2ed7	14		.
	defb 010h		;2ed8	10		.
	defb 000h		;2ed9	00		.
	defb 010h		;2eda	10		.
	defb 0ffh		;2edb	ff		.
	defb 0ffh		;2edc	ff		.
	defb 0f0h		;2edd	f0		.
	defb 00fh		;2ede	0f		.
	defb 000h		;2edf	00		.
	defb 0c0h		;2ee0	c0		.
	defb 078h		;2ee1	78		x
	defb 030h		;2ee2	30		0
	defb 0cch		;2ee3	cc		.
	defb 018h		;2ee4	18		.
	defb 0c6h		;2ee5	c6		.
	defb 0feh		;2ee6	fe		.
	defb 00ch		;2ee7	0c		.
	defb 0feh		;2ee8	fe		.
	defb 0ffh		;2ee9	ff		.
	defb 03eh		;2eea	3e		>
	defb 0f2h		;2eeb	f2		.
	defb 0f8h		;2eec	f8		.
	defb 07ch		;2eed	7c		|
	defb 0ceh		;2eee	ce		.
	defb 0cch		;2eef	cc		.
	defb 0feh		;2ef0	fe		.
	defb 07ch		;2ef1	7c		|
	defb 078h		;2ef2	78		x
	defb 078h		;2ef3	78		x
	defb 010h		;2ef4	10		.
	defb 020h		;2ef5	20		 
	defb 030h		;2ef6	30		0
	defb 098h		;2ef7	98		.
	defb 000h		;2ef8	00		.
	defb 000h		;2ef9	00		.
	defb 000h		;2efa	00		.
	defb 010h		;2efb	10		.
	defb 000h		;2efc	00		.
	defb 000h		;2efd	00		.
	defb 000h		;2efe	00		.
	defb 000h		;2eff	00		.
	defb 000h		;2f00	00		.
	defb 07eh		;2f01	7e		~
	defb 07eh		;2f02	7e		~
	defb 000h		;2f03	00		.
	defb 000h		;2f04	00		.
	defb 038h		;2f05	38		8
	defb 038h		;2f06	38		8
	defb 000h		;2f07	00		.
	defb 0ffh		;2f08	ff		.
	defb 000h		;2f09	00		.
	defb 0ffh		;2f0a	ff		.
	defb 078h		;2f0b	78		x
	defb 018h		;2f0c	18		.
	defb 0e0h		;2f0d	e0		.
	defb 0c0h		;2f0e	c0		.
	defb 018h		;2f0f	18		.
	defb 000h		;2f10	00		.
	defb 000h		;2f11	00		.
	defb 018h		;2f12	18		.
	defb 000h		;2f13	00		.
	defb 000h		;2f14	00		.
	defb 078h		;2f15	78		x
	defb 000h		;2f16	00		.
	defb 0ffh		;2f17	ff		.
	defb 000h		;2f18	00		.
	defb 000h		;2f19	00		.
	defb 000h		;2f1a	00		.
	defb 000h		;2f1b	00		.
	defb 000h		;2f1c	00		.
	defb 000h		;2f1d	00		.
	defb 000h		;2f1e	00		.
	defb 000h		;2f1f	00		.
	defb 000h		;2f20	00		.
	defb 000h		;2f21	00		.
	defb 000h		;2f22	00		.
	defb 000h		;2f23	00		.
	defb 000h		;2f24	00		.
	defb 000h		;2f25	00		.
	defb 000h		;2f26	00		.
	defb 000h		;2f27	00		.
	defb 000h		;2f28	00		.
	defb 000h		;2f29	00		.
	defb 000h		;2f2a	00		.
	defb 000h		;2f2b	00		.
	defb 060h		;2f2c	60		`
	defb 000h		;2f2d	00		.
	defb 000h		;2f2e	00		.
	defb 000h		;2f2f	00		.
	defb 000h		;2f30	00		.
	defb 000h		;2f31	00		.
	defb 000h		;2f32	00		.
	defb 000h		;2f33	00		.
	defb 000h		;2f34	00		.
	defb 000h		;2f35	00		.
	defb 000h		;2f36	00		.
	defb 000h		;2f37	00		.
	defb 000h		;2f38	00		.
	defb 000h		;2f39	00		.
	defb 000h		;2f3a	00		.
	defb 060h		;2f3b	60		`
	defb 000h		;2f3c	00		.
	defb 000h		;2f3d	00		.
	defb 000h		;2f3e	00		.
	defb 000h		;2f3f	00		.
	defb 000h		;2f40	00		.
	defb 000h		;2f41	00		.
	defb 000h		;2f42	00		.
	defb 000h		;2f43	00		.
	defb 000h		;2f44	00		.
	defb 000h		;2f45	00		.
	defb 000h		;2f46	00		.
	defb 000h		;2f47	00		.
	defb 000h		;2f48	00		.
	defb 000h		;2f49	00		.
	defb 000h		;2f4a	00		.
	defb 000h		;2f4b	00		.
	defb 000h		;2f4c	00		.
	defb 000h		;2f4d	00		.
	defb 000h		;2f4e	00		.
	defb 000h		;2f4f	00		.
	defb 000h		;2f50	00		.
	defb 000h		;2f51	00		.
	defb 000h		;2f52	00		.
	defb 000h		;2f53	00		.
	defb 000h		;2f54	00		.
	defb 000h		;2f55	00		.
	defb 000h		;2f56	00		.
	defb 000h		;2f57	00		.
	defb 000h		;2f58	00		.
	defb 000h		;2f59	00		.
	defb 000h		;2f5a	00		.
	defb 000h		;2f5b	00		.
	defb 000h		;2f5c	00		.
	defb 000h		;2f5d	00		.
	defb 000h		;2f5e	00		.
	defb 0ffh		;2f5f	ff		.
	defb 000h		;2f60	00		.
	defb 000h		;2f61	00		.
	defb 000h		;2f62	00		.
	defb 000h		;2f63	00		.
	defb 000h		;2f64	00		.
	defb 000h		;2f65	00		.
	defb 000h		;2f66	00		.
	defb 0f8h		;2f67	f8		.
	defb 000h		;2f68	00		.
	defb 000h		;2f69	00		.
	defb 078h		;2f6a	78		x
	defb 000h		;2f6b	00		.
	defb 000h		;2f6c	00		.
	defb 000h		;2f6d	00		.
	defb 000h		;2f6e	00		.
	defb 000h		;2f6f	00		.
	defb 0f0h		;2f70	f0		.
	defb 01eh		;2f71	1e		.
	defb 000h		;2f72	00		.
	defb 000h		;2f73	00		.
	defb 000h		;2f74	00		.
	defb 000h		;2f75	00		.
	defb 000h		;2f76	00		.
	defb 000h		;2f77	00		.
	defb 000h		;2f78	00		.
	defb 0f8h		;2f79	f8		.
	defb 000h		;2f7a	00		.
	defb 000h		;2f7b	00		.
	defb 000h		;2f7c	00		.
	defb 000h		;2f7d	00		.
	defb 000h		;2f7e	00		.
	defb 000h		;2f7f	00		.
	defb 000h		;2f80	00		.
	defb 000h		;2f81	00		.
	defb 000h		;2f82	00		.
	defb 000h		;2f83	00		.
	defb 0c3h		;2f84	c3		.
	defb 000h		;2f85	00		.
	defb 000h		;2f86	00		.
	defb 000h		;2f87	00		.
	defb 000h		;2f88	00		.
	defb 000h		;2f89	00		.
	defb 000h		;2f8a	00		.
	defb 000h		;2f8b	00		.
	defb 000h		;2f8c	00		.
	defb 000h		;2f8d	00		.
	defb 000h		;2f8e	00		.
	defb 000h		;2f8f	00		.
	defb 000h		;2f90	00		.
	defb 000h		;2f91	00		.
	defb 000h		;2f92	00		.
	defb 000h		;2f93	00		.
	defb 000h		;2f94	00		.
	defb 000h		;2f95	00		.
	defb 006h		;2f96	06		.
	defb 000h		;2f97	00		.
	defb 000h		;2f98	00		.
	defb 003h		;2f99	03		.
	defb 000h		;2f9a	00		.
	defb 000h		;2f9b	00		.
	defb 000h		;2f9c	00		.
	defb 000h		;2f9d	00		.
	defb 000h		;2f9e	00		.
	defb 000h		;2f9f	00		.
	defb 000h		;2fa0	00		.
	defb 000h		;2fa1	00		.
	defb 000h		;2fa2	00		.
	defb 000h		;2fa3	00		.
	defb 0c3h		;2fa4	c3		.
	defb 000h		;2fa5	00		.
	defb 000h		;2fa6	00		.
	defb 000h		;2fa7	00		.
	defb 000h		;2fa8	00		.
	defb 000h		;2fa9	00		.
	defb 000h		;2faa	00		.
	defb 000h		;2fab	00		.
	defb 000h		;2fac	00		.
	defb 000h		;2fad	00		.
	defb 000h		;2fae	00		.
	defb 000h		;2faf	00		.
	defb 088h		;2fb0	88		.
	defb 0aah		;2fb1	aa		.
	defb 0eeh		;2fb2	ee		.
	defb 010h		;2fb3	10		.
	defb 010h		;2fb4	10		.
	defb 010h		;2fb5	10		.
	defb 014h		;2fb6	14		.
	defb 014h		;2fb7	14		.
	defb 010h		;2fb8	10		.
	defb 014h		;2fb9	14		.
	defb 014h		;2fba	14		.
	defb 014h		;2fbb	14		.
	defb 000h		;2fbc	00		.
	defb 000h		;2fbd	00		.
	defb 000h		;2fbe	00		.
	defb 010h		;2fbf	10		.
	defb 000h		;2fc0	00		.
	defb 000h		;2fc1	00		.
	defb 010h		;2fc2	10		.
	defb 010h		;2fc3	10		.
	defb 000h		;2fc4	00		.
	defb 010h		;2fc5	10		.
	defb 010h		;2fc6	10		.
	defb 014h		;2fc7	14		.
	defb 000h		;2fc8	00		.
	defb 014h		;2fc9	14		.
	defb 000h		;2fca	00		.
	defb 014h		;2fcb	14		.
	defb 014h		;2fcc	14		.
	defb 000h		;2fcd	00		.
	defb 014h		;2fce	14		.
	defb 000h		;2fcf	00		.
	defb 000h		;2fd0	00		.
	defb 010h		;2fd1	10		.
	defb 014h		;2fd2	14		.
	defb 000h		;2fd3	00		.
	defb 000h		;2fd4	00		.
	defb 010h		;2fd5	10		.
	defb 014h		;2fd6	14		.
	defb 014h		;2fd7	14		.
	defb 010h		;2fd8	10		.
	defb 000h		;2fd9	00		.
	defb 010h		;2fda	10		.
	defb 0ffh		;2fdb	ff		.
	defb 0ffh		;2fdc	ff		.
	defb 0f0h		;2fdd	f0		.
	defb 00fh		;2fde	0f		.
	defb 000h		;2fdf	00		.
	defb 0c0h		;2fe0	c0		.
	defb 000h		;2fe1	00		.
	defb 000h		;2fe2	00		.
	defb 078h		;2fe3	78		x
	defb 018h		;2fe4	18		.
	defb 000h		;2fe5	00		.
	defb 006h		;2fe6	06		.
	defb 000h		;2fe7	00		.
	defb 000h		;2fe8	00		.
	defb 003h		;2fe9	03		.
	defb 000h		;2fea	00		.
	defb 000h		;2feb	00		.
	defb 000h		;2fec	00		.
	defb 000h		;2fed	00		.
	defb 000h		;2fee	00		.
	defb 000h		;2fef	00		.
	defb 000h		;2ff0	00		.
	defb 000h		;2ff1	00		.
	defb 000h		;2ff2	00		.
	defb 000h		;2ff3	00		.
	defb 010h		;2ff4	10		.
	defb 000h		;2ff5	00		.
	defb 000h		;2ff6	00		.
	defb 000h		;2ff7	00		.
	defb 000h		;2ff8	00		.
	defb 000h		;2ff9	00		.
	defb 000h		;2ffa	00		.
	defb 000h		;2ffb	00		.
	defb 000h		;2ffc	00		.
	defb 000h		;2ffd	00		.
	defb 000h		;2ffe	00		.
	defb 000h		;2fff	00		.
;---------------------------------------------------------------------------
; FnTable80: handler addresses of BIOS functions #80-#FF, entry at
; #3000 + 2 * (C - #80). Functions served by FnNotImplemented (#315D,
; SCF : RET) in 3.04: #9B, #9C, #B9, #BA, #BB, #BC, #BD, #BE, #BF, #E1, #E2, #E3, 
;   #E4, #E5, #E6, #E7, #EB, #EC, #F9, #FA, #FC, #FE
;   
;---------------------------------------------------------------------------
FnTable80:

; BLOCK 'FnTable80' (start 0x3000 end 0x3100)
	defb 0b5h		;3000	b5		.
	defb 037h		;3001	37		7
	defb 09fh		;3002	9f		.
	defb 033h		;3003	33		3
	defb 0c5h		;3004	c5		.
	defb 033h		;3005	33		3
	defb 0e5h		;3006	e5		.
	defb 033h		;3007	33		3
	defb 0a8h		;3008	a8		.
	defb 035h		;3009	35		5
	defb 008h		;300a	08		.
	defb 034h		;300b	34		4
	defb 032h		;300c	32		2
	defb 034h		;300d	34		4
	defb 054h		;300e	54		T
	defb 034h		;300f	34		4
	defb 083h		;3010	83		.
	defb 034h		;3011	34		4
	defb 0d3h		;3012	d3		.
	defb 035h		;3013	35		5
	defb 064h		;3014	64		d
	defb 03ah		;3015	3a		:
	defb 0ach		;3016	ac		.
	defb 034h		;3017	34		4
	defb 0d8h		;3018	d8		.
	defb 034h		;3019	34		4
	defb 0d7h		;301a	d7		.
	defb 035h		;301b	35		5
	defb 0b5h		;301c	b5		.
	defb 035h		;301d	35		5
	defb 0f0h		;301e	f0		.
	defb 032h		;301f	32		2
	defb 055h		;3020	55		U
	defb 00fh		;3021	0f		.
	defb 074h		;3022	74		t
	defb 00fh		;3023	0f		.
	defb 00ah		;3024	0a		.
	defb 010h		;3025	10		.
	defb 045h		;3026	45		E
	defb 010h		;3027	10		.
	defb 07fh		;3028	7f		.
	defb 010h		;3029	10		.
	defb 087h		;302a	87		.
	defb 010h		;302b	10		.
	defb 0c6h		;302c	c6		.
	defb 010h		;302d	10		.
	defb 031h		;302e	31		1
	defb 012h		;302f	12		.
	defb 0f9h		;3030	f9		.
	defb 011h		;3031	11		.
	defb 0dfh		;3032	df		.
	defb 011h		;3033	11		.
	defb 0c6h		;3034	c6		.
	defb 011h		;3035	11		.
	defb 05dh		;3036	5d		]
	defb 031h		;3037	31		1
	defb 05dh		;3038	5d		]
	defb 031h		;3039	31		1
	defb 02ah		;303a	2a		*
	defb 033h		;303b	33		3
	defb 049h		;303c	49		I
	defb 033h		;303d	33		3
	defb 04dh		;303e	4d		M
	defb 012h		;303f	12		.
	defb 041h		;3040	41		A
	defb 038h		;3041	38		8
	defb 00ah		;3042	0a		.
	defb 00dh		;3043	0d		.
	defb 030h		;3044	30		0
	defb 00dh		;3045	0d		.
	defb 083h		;3046	83		.
	defb 00dh		;3047	0d		.
	defb 0e4h		;3048	e4		.
	defb 00dh		;3049	0d		.
	defb 06eh		;304a	6e		n
	defb 00eh		;304b	0e		.
	defb 074h		;304c	74		t
	defb 00eh		;304d	0e		.
	defb 083h		;304e	83		.
	defb 00eh		;304f	0e		.
	defb 0b9h		;3050	b9		.
	defb 00eh		;3051	0e		.
	defb 0f3h		;3052	f3		.
	defb 00eh		;3053	0e		.
	defb 0f3h		;3054	f3		.
	defb 00eh		;3055	0e		.
	defb 0f3h		;3056	f3		.
	defb 00eh		;3057	0e		.
	defb 0f3h		;3058	f3		.
	defb 00eh		;3059	0e		.
	defb 0f3h		;305a	f3		.
	defb 00eh		;305b	0e		.
	defb 0f3h		;305c	f3		.
	defb 00eh		;305d	0e		.
	defb 0f3h		;305e	f3		.
	defb 00eh		;305f	0e		.
	defb 00fh		;3060	0f		.
	defb 038h		;3061	38		8
	defb 013h		;3062	13		.
	defb 039h		;3063	39		9
	defb 0afh		;3064	af		.
	defb 03bh		;3065	3b		;
	defb 000h		;3066	00		.
	defb 03ch		;3067	3c		<
	defb 039h		;3068	39		9
	defb 03bh		;3069	3b		;
	defb 066h		;306a	66		f
	defb 03bh		;306b	3b		;
	defb 0c9h		;306c	c9		.
	defb 036h		;306d	36		6
	defb 093h		;306e	93		.
	defb 03bh		;306f	3b		;
	defb 0bfh		;3070	bf		.
	defb 036h		;3071	36		6
	defb 05dh		;3072	5d		]
	defb 031h		;3073	31		1
	defb 05dh		;3074	5d		]
	defb 031h		;3075	31		1
	defb 05dh		;3076	5d		]
	defb 031h		;3077	31		1
	defb 05dh		;3078	5d		]
	defb 031h		;3079	31		1
	defb 05dh		;307a	5d		]
	defb 031h		;307b	31		1
	defb 05dh		;307c	5d		]
	defb 031h		;307d	31		1
	defb 05dh		;307e	5d		]
	defb 031h		;307f	31		1
	defb 055h		;3080	55		U
	defb 00fh		;3081	0f		.
	defb 074h		;3082	74		t
	defb 00fh		;3083	0f		.
	defb 0d3h		;3084	d3		.
	defb 00fh		;3085	0f		.
	defb 01eh		;3086	1e		.
	defb 010h		;3087	10		.
	defb 05bh		;3088	5b		[
	defb 010h		;3089	10		.
	defb 09eh		;308a	9e		.
	defb 010h		;308b	10		.
	defb 0c6h		;308c	c6		.
	defb 010h		;308d	10		.
	defb 087h		;308e	87		.
	defb 010h		;308f	10		.
	defb 0e0h		;3090	e0		.
	defb 010h		;3091	10		.
	defb 0bah		;3092	ba		.
	defb 031h		;3093	31		1
	defb 0e1h		;3094	e1		.
	defb 031h		;3095	31		1
	defb 001h		;3096	01		.
	defb 032h		;3097	32		2
	defb 01fh		;3098	1f		.
	defb 032h		;3099	32		2
	defb 03ah		;309a	3a		:
	defb 032h		;309b	32		2
	defb 056h		;309c	56		V
	defb 032h		;309d	32		2
	defb 06fh		;309e	6f		o
	defb 032h		;309f	32		2
	defb 020h		;30a0	20		 
	defb 03eh		;30a1	3e		>
	defb 020h		;30a2	20		 
	defb 03eh		;30a3	3e		>
	defb 020h		;30a4	20		 
	defb 03eh		;30a5	3e		>
	defb 020h		;30a6	20		 
	defb 03eh		;30a7	3e		>
	defb 020h		;30a8	20		 
	defb 03eh		;30a9	3e		>
	defb 020h		;30aa	20		 
	defb 03eh		;30ab	3e		>
	defb 020h		;30ac	20		 
	defb 03eh		;30ad	3e		>
	defb 020h		;30ae	20		 
	defb 03eh		;30af	3e		>
	defb 020h		;30b0	20		 
	defb 03eh		;30b1	3e		>
	defb 020h		;30b2	20		 
	defb 03eh		;30b3	3e		>
	defb 020h		;30b4	20		 
	defb 03eh		;30b5	3e		>
	defb 020h		;30b6	20		 
	defb 03eh		;30b7	3e		>
	defb 020h		;30b8	20		 
	defb 03eh		;30b9	3e		>
	defb 020h		;30ba	20		 
	defb 03eh		;30bb	3e		>
	defb 020h		;30bc	20		 
	defb 03eh		;30bd	3e		>
	defb 020h		;30be	20		 
	defb 03eh		;30bf	3e		>
	defb 0feh		;30c0	fe		.
	defb 034h		;30c1	34		4
	defb 05dh		;30c2	5d		]
	defb 031h		;30c3	31		1
	defb 05dh		;30c4	5d		]
	defb 031h		;30c5	31		1
	defb 05dh		;30c6	5d		]
	defb 031h		;30c7	31		1
	defb 05dh		;30c8	5d		]
	defb 031h		;30c9	31		1
	defb 05dh		;30ca	5d		]
	defb 031h		;30cb	31		1
	defb 05dh		;30cc	5d		]
	defb 031h		;30cd	31		1
	defb 05dh		;30ce	5d		]
	defb 031h		;30cf	31		1
	defb 047h		;30d0	47		G
	defb 03ch		;30d1	3c		<
	defb 08ah		;30d2	8a		.
	defb 03ch		;30d3	3c		<
	defb 04eh		;30d4	4e		N
	defb 03eh		;30d5	3e		>
	defb 05dh		;30d6	5d		]
	defb 031h		;30d7	31		1
	defb 05dh		;30d8	5d		]
	defb 031h		;30d9	31		1
	defb 01eh		;30da	1e		.
	defb 031h		;30db	31		1
	defb 0e9h		;30dc	e9		.
	defb 008h		;30dd	08		.
	defb 00fh		;30de	0f		.
	defb 012h		;30df	12		.
	defb 003h		;30e0	03		.
	defb 009h		;30e1	09		.
	defb 01dh		;30e2	1d		.
	defb 009h		;30e3	09		.
	defb 064h		;30e4	64		d
	defb 00bh		;30e5	0b		.
	defb 02dh		;30e6	2d		-
	defb 009h		;30e7	09		.
	defb 09fh		;30e8	9f		.
	defb 031h		;30e9	31		1
	defb 0cah		;30ea	ca		.
	defb 032h		;30eb	32		2
	defb 0bah		;30ec	ba		.
	defb 032h		;30ed	32		2
	defb 099h		;30ee	99		.
	defb 032h		;30ef	32		2
	defb 05fh		;30f0	5f		_
	defb 031h		;30f1	31		1
	defb 05dh		;30f2	5d		]
	defb 031h		;30f3	31		1
	defb 05dh		;30f4	5d		]
	defb 031h		;30f5	31		1
	defb 016h		;30f6	16		.
	defb 004h		;30f7	04		.
	defb 05dh		;30f8	5d		]
	defb 031h		;30f9	31		1
	defb 072h		;30fa	72		r
	defb 033h		;30fb	33		3
	defb 05dh		;30fc	5d		]
	defb 031h		;30fd	31		1
	defb 00fh		;30fe	0f		.
	defb 012h		;30ff	12		.
BiosCallFromPage0:
	pop af			;3100	f1		.
	call CallFnTable	;3101	cd 4f 31	. O 1
	call DOS_ON	;3104	cd 00 3d	. . =
	jp Page0CallEntry		;3107	c3 f8 3f	. . ?
;---------------------------------------------------------------------------
; BiosDispatch, C = function number:
;   #80-#FF  -> CallFnTable (table at #3000)
;   #40-#7F  -> bit 6 cleared, HddFnDispatch (#0571)
;   #00-#3F  -> Carry set: not a BIOS function
;---------------------------------------------------------------------------
BiosDispatch:
	bit 7,c			;310a	cb 79		. y
	jr z,l3112h		;310c	28 04		( .
	call CallFnTable	;310e	cd 4f 31	. O 1
	ret			;3111	c9		.
l3112h:
	bit 6,c			;3112	cb 71		. q
	res 6,c			;3114	cb b1		. .
	jr nz,l311ah		;3116	20 02		  .
	scf			;3118	37		7
	ret			;3119	c9		.
l311ah:
	call HddFnDispatch	;311a	cd 71 05	. q .
	ret			;311d	c9		.
FnED_FN_CRIPT:
	dec b			;311e	05		.
	scf			;311f	37		7
	ret nz			;3120	c0		.
	ld hl,(BoardId)		;3121	2a 05 00	* . .
	ld a,(l0007h)		;3124	3a 07 00	: . .
	ld bc,05282h		;3127	01 82 52	. . R
	ld de,047e8h		;312a	11 e8 47	. . G
	and a			;312d	a7		.
	ret			;312e	c9		.
	push af			;312f	f5		.
	ld a,r			;3130	ed 5f		. _
	jp pe,l3137h		;3132	ea 37 31	. 7 1
	ld a,r			;3135	ed 5f		. _
l3137h:
	ld a,080h		;3137	3e 80		> .
	jp pe,l313dh		;3139	ea 3d 31	. = 1
	xor a			;313c	af		.
l313dh:
	ld r,a			;313d	ed 4f		. O
	di			;313f	f3		.
	pop af			;3140	f1		.
	ret			;3141	c9		.
	push af			;3142	f5		.
	ld a,r			;3143	ed 5f		. _
	bit 7,a			;3145	cb 7f		. .
	jr z,l314ch		;3147	28 03		( .
	ei			;3149	fb		.
	pop af			;314a	f1		.
	ret			;314b	c9		.
l314ch:
	di			;314c	f3		.
	pop af			;314d	f1		.
	ret			;314e	c9		.
CallFnTable:
	push hl			;314f	e5		.
	push af			;3150	f5		.
	ld l,c			;3151	69		i
	sla l			;3152	cb 25		. %
	ld h,030h		;3154	26 30		& 0
	ld a,(hl)		;3156	7e		~
	inc l			;3157	2c		,
	ld h,(hl)		;3158	66		f
	ld l,a			;3159	6f		o
	pop af			;315a	f1		.
	ex (sp),hl		;315b	e3		.
	ret			;315c	c9		.
;---------------------------------------------------------------------------
; FnNotImplemented: returns Carry = 1 (error).
;---------------------------------------------------------------------------
FnNotImplemented:
	scf			;315d	37		7
	ret			;315e	c9		.
FnF8_SET_PORTS:
				; = ~SET_PORTS (src: DCP.ASM:596, BIOS-TT 0271ac3)
	ex af,af'		;315f	08		.
	ld a,004h		;3160	3e 04		> .
	out (07ch),a		;3162	d3 7c		. |
	ld c,0c2h		;3164	0e c2		. .
	in d,(c)		;3166	ed 50		. P
	ld a,040h		;3168	3e 40		> @
	out (c),a		;316a	ed 79		. y
	ld a,(08000h)		;316c	3a 00 80	: . .
	ld l,a			;316f	6f		o
	ld a,(08200h)		;3170	3a 00 82	: . .
	ld h,a			;3173	67		g
	ex af,af'		;3174	08		.
	ld (08000h),a		;3175	32 00 80	2 . .
	ld (08200h),a		;3178	32 00 82	2 . .
	ex af,af'		;317b	08		.
	ld a,b			;317c	78		x
	ld bc,Reset		;317d	01 00 00	. . .
	ex af,af'		;3180	08		.
	in a,(c)		;3181	ed 78		. x
	ex af,af'		;3183	08		.
	out (c),a		;3184	ed 79		. y
	ex af,af'		;3186	08		.
	ld b,a			;3187	47		G
	ld a,l			;3188	7d		}
	ld (08000h),a		;3189	32 00 80	2 . .
	ld a,h			;318c	7c		|
	ld (08200h),a		;318d	32 00 82	2 . .
	ld c,0c2h		;3190	0e c2		. .
	ld a,0feh		;3192	3e fe		> .
	out (c),a		;3194	ed 79		. y
	ld a,(0813ah)		;3196	3a 3a 81	: : .
	out (c),d		;3199	ed 51		. Q
	out (07ch),a		;319b	d3 7c		. |
	and a			;319d	a7		.
	ret			;319e	c9		.
FnF4_DCP_CONFIG:
	and a			;319f	a7		.
	jp z,PortsInit		;31a0	ca 6e 03	. n .
	push ix			;31a3	dd e5		. .
	ld ix,Data31B3	;31a5	dd 21 b3 31	. ! . 1
	in a,(0e2h)		;31a9	db e2		. .
	ex af,af'		;31ab	08		.
	ld a,040h		;31ac	3e 40		> @
	out (0e2h),a		;31ae	d3 e2		. .
	jp DCP_CONFIG_PARSE_TABLE	;31b0	c3 dc 0c	. . .

; BLOCK 'Data31B3' (start 0x31b3 end 0x31ba)
Data31B3:
	defb 008h		;31b3	08		.
	defb 0d3h		;31b4	d3		.
	defb 0e2h		;31b5	e2		.
	defb 0a7h		;31b6	a7		.
	defb 0ddh		;31b7	dd		.
	defb 0e1h		;31b8	e1		.
	defb 0c9h		;31b9	c9		.
FnC9_BLK_TO_RAMD:
	cp 010h			;31ba	fe 10		. .
	ccf			;31bc	3f		?
	ret c			;31bd	d8		.
	push hl			;31be	e5		.
	push bc			;31bf	c5		.
	ld l,a			;31c0	6f		o
	in a,(0c2h)		;31c1	db c2		. .
	ld c,a			;31c3	4f		O
	ld a,0feh		;31c4	3e fe		> .
	out (0c2h),a		;31c6	d3 c2		. .
	ld a,l			;31c8	7d		}
	ld hl,08180h		;31c9	21 80 81	! . .
	add a,l			;31cc	85		.
	ld l,a			;31cd	6f		o
	ld a,(hl)		;31ce	7e		~
	and a			;31cf	a7		.
	jr nz,BLK_BUSY		;31d0	20 09		  .
	ld (hl),b		;31d2	70		p
	ld a,c			;31d3	79		y
	out (0c2h),a		;31d4	d3 c2		. .
	ld a,b			;31d6	78		x
	and a			;31d7	a7		.
	pop bc			;31d8	c1		.
	pop hl			;31d9	e1		.
	ret			;31da	c9		.
BLK_BUSY:
				; = BLK_BUSY (src: FUNC_RAM_ROM_DRV.ASM:885, BIOS-TT 0271ac3)
	ld a,c			;31db	79		y
	out (0c2h),a		;31dc	d3 c2		. .
	scf			;31de	37		7
	pop hl			;31df	e1		.
	ret			;31e0	c9		.
FnCA_RAMD_CLEAR:
				; = ~BLK_TO_RAMD (src: FUNC_RAM_ROM_DRV.ASM:856, BIOS-TT 0271ac3)
				; = RAMD_CLEAR (src: FUNC_RAM_ROM_DRV.ASM:900, BIOS-TT 0271ac3)
	cp 010h			;31e1	fe 10		. .
	ccf			;31e3	3f		?
	ret c			;31e4	d8		.
	push hl			;31e5	e5		.
	ld l,a			;31e6	6f		o
	in a,(0c2h)		;31e7	db c2		. .
	ld c,a			;31e9	4f		O
	ld a,0feh		;31ea	3e fe		> .
	out (0c2h),a		;31ec	d3 c2		. .
	ld a,l			;31ee	7d		}
	ld hl,08180h		;31ef	21 80 81	! . .
	add a,l			;31f2	85		.
	ld l,a			;31f3	6f		o
	ld b,a			;31f4	47		G
	ld a,(hl)		;31f5	7e		~
	and a			;31f6	a7		.
	jr z,BLK_BUSY		;31f7	28 e2		( .
	ld (hl),000h		;31f9	36 00		6 .
	ld a,c			;31fb	79		y
	out (0c2h),a		;31fc	d3 c2		. .
	and a			;31fe	a7		.
	pop hl			;31ff	e1		.
	ret			;3200	c9		.
FnCB_RAMD_TO_DRV:
				; = ~RAMD_TO_DRV (src: FUNK_FOR_TRDOS.ASM:71, BIOS-TT 0271ac3)
	cp 010h			;3201	fe 10		. .
	ccf			;3203	3f		?
	ret c			;3204	d8		.
	ld c,a			;3205	4f		O
	ld a,b			;3206	78		x
	cp 004h			;3207	fe 04		. .
	ccf			;3209	3f		?
	ret c			;320a	d8		.
	ld hl,08100h		;320b	21 00 81	! . .
	ld l,b			;320e	68		h
	in a,(0c2h)		;320f	db c2		. .
	ld b,a			;3211	47		G
	ld a,0feh		;3212	3e fe		> .
	out (0c2h),a		;3214	d3 c2		. .
	ld a,c			;3216	79		y
	add a,004h		;3217	c6 04		. .
	ld (hl),a		;3219	77		w
	ld a,b			;321a	78		x
	out (0c2h),a		;321b	d3 c2		. .
	and a			;321d	a7		.
	ret			;321e	c9		.
FnCC_FDD_TO_DRV:
				; = ~FDD_TO_DRV (src: FUNK_FOR_TRDOS.ASM:43, BIOS-TT 0271ac3)
	cp 004h			;321f	fe 04		. .
	ccf			;3221	3f		?
	ret c			;3222	d8		.
	ld c,a			;3223	4f		O
	ld a,b			;3224	78		x
	cp 004h			;3225	fe 04		. .
	ccf			;3227	3f		?
	ret c			;3228	d8		.
	ld hl,08100h		;3229	21 00 81	! . .
	ld l,b			;322c	68		h
	in a,(0c2h)		;322d	db c2		. .
	ld b,a			;322f	47		G
	ld a,0feh		;3230	3e fe		> .
	out (0c2h),a		;3232	d3 c2		. .
	ld (hl),c		;3234	71		q
	ld a,b			;3235	78		x
	out (0c2h),a		;3236	d3 c2		. .
	and a			;3238	a7		.
	ret			;3239	c9		.
FnCD_HDD_TO_DRV:
				; = ~HDD_TO_DRV (src: FUNK_FOR_TRDOS.ASM:99, BIOS-TT 0271ac3)
	and 00fh		;323a	e6 0f		. .
	ld c,a			;323c	4f		O
	ld a,b			;323d	78		x
	cp 004h			;323e	fe 04		. .
	ccf			;3240	3f		?
	ret c			;3241	d8		.
	ld hl,08100h		;3242	21 00 81	! . .
	ld l,b			;3245	68		h
	in a,(0c2h)		;3246	db c2		. .
	ld b,a			;3248	47		G
	ld a,0feh		;3249	3e fe		> .
	out (0c2h),a		;324b	d3 c2		. .
	ld a,c			;324d	79		y
	add a,040h		;324e	c6 40		. @
	ld (hl),a		;3250	77		w
	ld a,b			;3251	78		x
	out (0c2h),a		;3252	d3 c2		. .
	and a			;3254	a7		.
	ret			;3255	c9		.
FnCE_GET_RAMD_ST:
				; = ~GET_RAMD_ST (src: FUNC_RAM_ROM_DRV.ASM:826, BIOS-TT 0271ac3)
	cp 010h			;3256	fe 10		. .
	ccf			;3258	3f		?
	ret c			;3259	d8		.
	push bc			;325a	c5		.
	ld hl,08180h		;325b	21 80 81	! . .
	add a,l			;325e	85		.
	ld l,a			;325f	6f		o
	in a,(0c2h)		;3260	db c2		. .
	ld b,a			;3262	47		G
	ld a,0feh		;3263	3e fe		> .
	out (0c2h),a		;3265	d3 c2		. .
	ld c,(hl)		;3267	4e		N
	ld a,b			;3268	78		x
	out (0c2h),a		;3269	d3 c2		. .
	ld a,c			;326b	79		y
	pop bc			;326c	c1		.
	and a			;326d	a7		.
	ret			;326e	c9		.
FnCF_GET_DRV_ST:
				; = ~GET_DRV_ST (src: FUNK_FOR_TRDOS.ASM:125, BIOS-TT 0271ac3)
	cp 004h			;326f	fe 04		. .
	ccf			;3271	3f		?
	ret c			;3272	d8		.
	push bc			;3273	c5		.
	ld hl,08100h		;3274	21 00 81	! . .
	add a,l			;3277	85		.
	ld l,a			;3278	6f		o
	in a,(0c2h)		;3279	db c2		. .
	ld b,a			;327b	47		G
	ld a,0feh		;327c	3e fe		> .
	out (0c2h),a		;327e	d3 c2		. .
	ld c,(hl)		;3280	4e		N
	ld a,b			;3281	78		x
	out (0c2h),a		;3282	d3 c2		. .
	ld a,c			;3284	79		y
	pop bc			;3285	c1		.
	and a			;3286	a7		.
	ret			;3287	c9		.
CmosEmulatedWrite:
				; = ~CMOS_EMU_WR (src: FUNC_CMOS.ASM:5, BIOS-TT 0271ac3)
	push de			;3288	d5		.
	ld c,0e2h		;3289	0e e2		. .
	in b,(c)		;328b	ed 40		. @
	ld e,0feh		;328d	1e fe		. .
	out (c),e		;328f	ed 59		. Y
	ld e,d			;3291	5a		Z
	ld d,0ffh		;3292	16 ff		. .
	ld (de),a		;3294	12		.
	out (c),b		;3295	ed 41		. A
	pop de			;3297	d1		.
	ret			;3298	c9		.
;---------------------------------------------------------------------------
; CMOS write (function #F7): D = register, A = value. If no DS12887 answers
; (CmosTest fails) the value goes to an emulated CMOS in the system page
; (#FE, at #FF00 + D in window 3). Real chip: address to port #DFBD,
; data to #BFBD (codes #1D / #1E). Read (#F6): data from #FFBD (code #1C).
;---------------------------------------------------------------------------
FnF7_CMOS_WR:
				; = ~CMOS_WR (src: FUNC_CMOS.ASM:21, BIOS-TT 0271ac3)
	call FnF5_CMOS_TEST	;3299	cd ca 32	. . 2
	jr c,CmosEmulatedWrite	;329c	38 ea		8 .
CmosWriteRegister:
				; = ~XWR_CMOS (src: FUNC_CMOS.ASM:24, BIOS-TT 0271ac3)
	ld bc,0dfbdh		;329e	01 bd df	. . .
	out (c),d		;32a1	ed 51		. Q
	ld bc,0bfbdh		;32a3	01 bd bf	. . .
	out (c),a		;32a6	ed 79		. y
	ret			;32a8	c9		.
CmosEmulatedRead:
				; = ~CMOS_EMU_RD (src: FUNC_CMOS.ASM:31, BIOS-TT 0271ac3)
	push de			;32a9	d5		.
	ld c,0e2h		;32aa	0e e2		. .
	in b,(c)		;32ac	ed 40		. @
	ld e,0feh		;32ae	1e fe		. .
	out (c),e		;32b0	ed 59		. Y
	ld e,d			;32b2	5a		Z
	ld d,0ffh		;32b3	16 ff		. .
	ld a,(de)		;32b5	1a		.
	out (c),b		;32b6	ed 41		. A
	pop de			;32b8	d1		.
	ret			;32b9	c9		.
FnF6_CMOS_RD:
				; = ~CMOS_RD (src: FUNC_CMOS.ASM:47, BIOS-TT 0271ac3)
	call FnF5_CMOS_TEST	;32ba	cd ca 32	. . 2
	jr c,CmosEmulatedRead	;32bd	38 ea		8 .
CmosReadRegister:
				; = ~XRD_CMOS (src: FUNC_CMOS.ASM:50, BIOS-TT 0271ac3)
	ld bc,0dfbdh		;32bf	01 bd df	. . .
	out (c),d		;32c2	ed 51		. Q
	ld bc,0ffbdh		;32c4	01 bd ff	. . .
	in a,(c)		;32c7	ed 78		. x
	ret			;32c9	c9		.
;---------------------------------------------------------------------------
; CMOS presence test (function #F5): invert register #3F, read it back,
; restore it. Carry = 1 when no chip answers.
;---------------------------------------------------------------------------
FnF5_CMOS_TEST:
				; = ~CMOS_TEST (src: FUNC_CMOS.ASM:57, BIOS-TT 0271ac3)
	push de			;32ca	d5		.
	push bc			;32cb	c5		.
	push af			;32cc	f5		.
	ld d,03fh		;32cd	16 3f		. ?
	call CmosReadRegister	;32cf	cd bf 32	. . 2
	ld e,a			;32d2	5f		_
	cpl			;32d3	2f		/
	call CmosWriteRegister	;32d4	cd 9e 32	. . 2
	call CmosReadRegister	;32d7	cd bf 32	. . 2
	cpl			;32da	2f		/
	cp e			;32db	bb		.
	jr nz,l32e7h		;32dc	20 09		  .
	ld a,e			;32de	7b		{
	call CmosWriteRegister	;32df	cd 9e 32	. . 2
	pop af			;32e2	f1		.
	pop bc			;32e3	c1		.
	pop de			;32e4	d1		.
	and a			;32e5	a7		.
	ret			;32e6	c9		.
l32e7h:
	ld a,e			;32e7	7b		{
	call CmosWriteRegister	;32e8	cd 9e 32	. . 2
	pop af			;32eb	f1		.
	pop bc			;32ec	c1		.
	pop de			;32ed	d1		.
	scf			;32ee	37		7
	ret			;32ef	c9		.
;---------------------------------------------------------------------------
; Function #8F (FN_TURBO): A = 2 / 3 turbo off / on (through #7C), A = #12 /
; #13 floppy density: OUT (#BD) with A = #01 (address #01BD, code #16,
; 720 KB, 250 kbit/s) or #21 (#21BD, code #17, 1.44 MB, 500 kbit/s).
;---------------------------------------------------------------------------
Fn8F_FN_TURBO:
				; = ~FN_TURBO (src: FUNC_SYS.ASM:234, BIOS-TT 0271ac3)
	cp 002h			;32f0	fe 02		. .
	jr z,l330eh		;32f2	28 1a		( .
	cp 003h			;32f4	fe 03		. .
	jr z,l330eh		;32f6	28 16		( .
	cp 012h			;32f8	fe 12		. .
	jr z,FddSet720		;32fa	28 06		( .
	cp 013h			;32fc	fe 13		. .
	jr z,FddSet1440		;32fe	28 08		( .
	scf			;3300	37		7
	ret			;3301	c9		.
FddSet720:
				; = ~FN_TURBO.SET_FDD_720 (src: FUNC_SYS.ASM:247, BIOS-TT 0271ac3)
	ld a,001h		;3302	3e 01		> .
	out (0bdh),a		;3304	d3 bd		. .
	and a			;3306	a7		.
	ret			;3307	c9		.
FddSet1440:
				; = ~FN_TURBO.SET_FDD_1440 (src: FUNC_SYS.ASM:253, BIOS-TT 0271ac3)
	ld a,021h		;3308	3e 21		> !
	out (0bdh),a		;330a	d3 bd		. .
	and a			;330c	a7		.
	ret			;330d	c9		.
l330eh:
	ld c,a			;330e	4f		O
	in a,(0e2h)		;330f	db e2		. .
	ld b,a			;3311	47		G
	ld a,0feh		;3312	3e fe		> .
	out (0e2h),a		;3314	d3 e2		. .
	ld de,(0c13ah)		;3316	ed 5b 3a c1	. [ : .
	ld a,e			;331a	7b		{
	and 0fch		;331b	e6 fc		. .
	or c			;331d	b1		.
	ld e,a			;331e	5f		_
	out (07ch),a		;331f	d3 7c		. |
	ld (0c13ah),de		;3321	ed 53 3a c1	. S : .
	ld a,b			;3325	78		x
	out (0e2h),a		;3326	d3 e2		. .
	and a			;3328	a7		.
	ret			;3329	c9		.
Fn9D_DivMemBlocks:
				; = EMM.DivMemBlocks (src: FUNC_RAM_ROM_DRV.ASM:937, BIOS-TT 0271ac3)
	inc b			;332a	04		.
	dec b			;332b	05		.
	scf			;332c	37		7
	ret z			;332d	c8		.
	dec b			;332e	05		.
	ld e,a			;332f	5f		_
	call FnC4_GetMemPage	;3330	cd 5b 10	. [ .
	ret c			;3333	d8		.
	ld d,a			;3334	57		W
	in a,(0c2h)		;3335	db c2		. .
	ex af,af'		;3337	08		.
	ld a,0feh		;3338	3e fe		> .
	out (0c2h),a		;333a	d3 c2		. .
	ld h,082h		;333c	26 82		& .
	ld l,d			;333e	6a		j
	ld a,(hl)		;333f	7e		~
	ld (hl),0ffh		;3340	36 ff		6 .
	ld b,a			;3342	47		G
	ex af,af'		;3343	08		.
	out (0c2h),a		;3344	d3 c2		. .
	ld a,e			;3346	7b		{
	and a			;3347	a7		.
	ret			;3348	c9		.
Fn9E_MergeMemBlocks:
				; = EMM.MergeMemBlocks (src: FUNC_RAM_ROM_DRV.ASM:975, BIOS-TT 0271ac3)
	ld e,a			;3349	5f		_
	in a,(0c2h)		;334a	db c2		. .
	ex af,af'		;334c	08		.
	ld a,0feh		;334d	3e fe		> .
	out (0c2h),a		;334f	d3 c2		. .
	ld h,082h		;3351	26 82		& .
	ld l,e			;3353	6b		k
	ld c,b			;3354	48		H
	ld b,000h		;3355	06 00		. .
EMM_ADD_L:
				; = EMM_ADD_L (src: FUNC_RAM_ROM_DRV.ASM:985, BIOS-TT 0271ac3)
	ld a,(hl)		;3357	7e		~
	and a			;3358	a7		.
	jr z,EMM_ADD_ERR	;3359	28 07		( .
	cp 0ffh			;335b	fe ff		. .
	jr z,EMM_ADD_NEXT	;335d	28 08		( .
	ld l,a			;335f	6f		o
	djnz EMM_ADD_L		;3360	10 f5		. .
EMM_ADD_ERR:
				; = EMM_ADD_ERR (src: FUNC_RAM_ROM_DRV.ASM:993, BIOS-TT 0271ac3)
	ex af,af'		;3362	08		.
	out (0c2h),a		;3363	d3 c2		. .
	scf			;3365	37		7
	ret			;3366	c9		.
EMM_ADD_NEXT:
				; = EMM_ADD_NEXT (src: FUNC_RAM_ROM_DRV.ASM:999, BIOS-TT 0271ac3)
	ld a,c			;3367	79		y
	and a			;3368	a7		.
	jr z,EMM_ADD_ERR	;3369	28 f7		( .
	ld (hl),a		;336b	77		w
	ex af,af'		;336c	08		.
	out (0c2h),a		;336d	d3 c2		. .
	and a			;336f	a7		.
	ld a,e			;3370	7b		{
	ret			;3371	c9		.
FnFD_REINIT:
	dec b			;3372	05		.
	jr z,REINIT_Restart	;3373	28 04		( .
	dec b			;3375	05		.
	scf			;3376	37		7
	ret nz			;3377	c0		.
	inc b			;3378	04		.
REINIT_Restart:
				; = ~REINIT.Restart (src: FUNC_SERVICE.asm:203, BIOS-TT 0271ac3)
	di			;3379	f3		.
	ld a,000h		;337a	3e 00		> .
	ld bc,l1ffdh		;337c	01 fd 1f	. . .
	out (c),a		;337f	ed 79		. y
	ld b,07fh		;3381	06 7f		. .
	out (c),a		;3383	ed 79		. y
	ld a,040h		;3385	3e 40		> @
	out (0e2h),a		;3387	d3 e2		. .
	ld a,005h		;3389	3e 05		> .
	out (0a2h),a		;338b	d3 a2		. .
	ld a,002h		;338d	3e 02		> .
	out (0e2h),a		;338f	d3 e2		. .
	ld a,000h		;3391	3e 00		> .
	out (082h),a		;3393	d3 82		. .
	out (089h),a		;3395	d3 89		. .
	out (0c9h),a		;3397	d3 c9		. .
	jp z,Reset		;3399	ca 00 00	. . .
	jp l097bh		;339c	c3 7b 09	. { .
Fn81_LP_PRINT_ALL:
				; = CMDREAD.LLL (src: FUNC_LOW_PRINT.ASM:77, BIOS-TT 0271ac3)
				; = LP_PRINT_ALL (src: FUNC_LOW_PRINT.ASM:149, BIOS-TT 0271ac3)
	call LP_BEG_P		;339f	cd 9a 36	. . 6
	exx			;33a2	d9		.
	ld c,a			;33a3	4f		O
	ld a,050h		;33a4	3e 50		> P
	out (0e2h),a		;33a6	d3 e2		. .
	exx			;33a8	d9		.
LP_PRINT_AL1:
				; = LP_PRINT_AL1 (src: FUNC_LOW_PRINT.ASM:157, BIOS-TT 0271ac3)
	exx			;33a9	d9		.
	ld a,d			;33aa	7a		z
	out (089h),a		;33ab	d3 89		. .
	inc d			;33ad	14		.
	ld (hl),c		;33ae	71		q
	exx			;33af	d9		.
	ld a,e			;33b0	7b		{
	exx			;33b1	d9		.
	inc l			;33b2	2c		,
	ld (hl),a		;33b3	77		w
	dec l			;33b4	2d		-
	djnz l33bah		;33b5	10 03		. .
	call LP_NEXT_HL		;33b7	cd 78 36	. x 6
l33bah:
	exx			;33ba	d9		.
	djnz LP_PRINT_AL1	;33bb	10 ec		. .
	ld a,0feh		;33bd	3e fe		> .
	out (0e2h),a		;33bf	d3 e2		. .
	call LP_END_P		;33c1	cd 89 37	. . 7
	ret			;33c4	c9		.
Fn82_LP_PRINT_SYM:
				; = LP_PRINT_SYM (src: FUNC_LOW_PRINT.ASM:186, BIOS-TT 0271ac3)
	call LP_BEG_P		;33c5	cd 9a 36	. . 6
	exx			;33c8	d9		.
	ld c,a			;33c9	4f		O
	ld a,050h		;33ca	3e 50		> P
	out (0e2h),a		;33cc	d3 e2		. .
	exx			;33ce	d9		.
LP_PRINT_SY1:
				; = LP_PRINT_SY1 (src: FUNC_LOW_PRINT.ASM:194, BIOS-TT 0271ac3)
	exx			;33cf	d9		.
	ld a,d			;33d0	7a		z
	out (089h),a		;33d1	d3 89		. .
	ld (hl),c		;33d3	71		q
	inc d			;33d4	14		.
	djnz l33dah		;33d5	10 03		. .
	call LP_NEXT_HL		;33d7	cd 78 36	. x 6
l33dah:
	exx			;33da	d9		.
	djnz LP_PRINT_SY1	;33db	10 f2		. .
	ld a,0feh		;33dd	3e fe		> .
	out (0e2h),a		;33df	d3 e2		. .
	call LP_END_P		;33e1	cd 89 37	. . 7
	ret			;33e4	c9		.
Fn83_LP_PRINT_ATR:
				; = LP_PRINT_ATR (src: FUNC_LOW_PRINT.ASM:214, BIOS-TT 0271ac3)
	call LP_BEG_P		;33e5	cd 9a 36	. . 6
	ld a,e			;33e8	7b		{
	exx			;33e9	d9		.
	ld c,a			;33ea	4f		O
	ld a,050h		;33eb	3e 50		> P
	out (0e2h),a		;33ed	d3 e2		. .
	exx			;33ef	d9		.
LP_PRINT_AT1:
				; = LP_PRINT_AT1 (src: FUNC_LOW_PRINT.ASM:223, BIOS-TT 0271ac3)
	exx			;33f0	d9		.
	ld a,d			;33f1	7a		z
	out (089h),a		;33f2	d3 89		. .
	inc d			;33f4	14		.
	inc l			;33f5	2c		,
	ld (hl),c		;33f6	71		q
	dec l			;33f7	2d		-
	djnz l33fdh		;33f8	10 03		. .
	call LP_NEXT_HL		;33fa	cd 78 36	. x 6
l33fdh:
	exx			;33fd	d9		.
	djnz LP_PRINT_AT1	;33fe	10 f0		. .
	ld a,0feh		;3400	3e fe		> .
	out (0e2h),a		;3402	d3 e2		. .
	call LP_END_P		;3404	cd 89 37	. . 7
	ret			;3407	c9		.
Fn85_LP_PRINT_LINE:
				; = LP_PRINT_LINE (src: FUNC_LOW_PRINT.ASM:248, BIOS-TT 0271ac3)
	call LP_BEG_P		;3408	cd 9a 36	. . 6
	exx			;340b	d9		.
	ld a,050h		;340c	3e 50		> P
	out (0e2h),a		;340e	d3 e2		. .
	ld c,089h		;3410	0e 89		. .
	exx			;3412	d9		.
LP_PRINT_LN1:
				; = LP_PRINT_LN1 (src: FUNC_LOW_PRINT.ASM:257, BIOS-TT 0271ac3)
	exx			;3413	d9		.
	out (c),d		;3414	ed 51		. Q
	inc d			;3416	14		.
	exx			;3417	d9		.
	ld a,(hl)		;3418	7e		~
	inc hl			;3419	23		#
	exx			;341a	d9		.
	ld (hl),a		;341b	77		w
	exx			;341c	d9		.
	ld a,e			;341d	7b		{
	exx			;341e	d9		.
	inc l			;341f	2c		,
	ld (hl),a		;3420	77		w
	dec l			;3421	2d		-
	djnz l3427h		;3422	10 03		. .
	call LP_NEXT_HL		;3424	cd 78 36	. x 6
l3427h:
	exx			;3427	d9		.
	djnz LP_PRINT_LN1	;3428	10 e9		. .
	ld a,0feh		;342a	3e fe		> .
	out (0e2h),a		;342c	d3 e2		. .
	call LP_END_P		;342e	cd 89 37	. . 7
	ret			;3431	c9		.
Fn86_LP_PRINT_LINE2:
				; = LP_PRINT_LINE2 (src: FUNC_LOW_PRINT.ASM:289, BIOS-TT 0271ac3)
	call LP_BEG_P		;3432	cd 9a 36	. . 6
	ld a,050h		;3435	3e 50		> P
	out (0e2h),a		;3437	d3 e2		. .
	exx			;3439	d9		.
	ld c,089h		;343a	0e 89		. .
	exx			;343c	d9		.
LP_PRINT_LN2:
				; = LP_PRINT_LN2 (src: FUNC_LOW_PRINT.ASM:298, BIOS-TT 0271ac3)
	ld a,(hl)		;343d	7e		~
	inc hl			;343e	23		#
	exx			;343f	d9		.
	out (c),d		;3440	ed 51		. Q
	inc d			;3442	14		.
	ld (hl),a		;3443	77		w
	djnz l3449h		;3444	10 03		. .
	call LP_NEXT_HL		;3446	cd 78 36	. x 6
l3449h:
	exx			;3449	d9		.
	djnz LP_PRINT_LN2	;344a	10 f1		. .
	ld a,0feh		;344c	3e fe		> .
	out (0e2h),a		;344e	d3 e2		. .
	call LP_END_P		;3450	cd 89 37	. . 7
	ret			;3453	c9		.
Fn87_LP_PRINT_LINE3:
				; = LP_PRINT_LINE3 (src: FUNC_LOW_PRINT.ASM:319, BIOS-TT 0271ac3)
	call LP_BEG_P		;3454	cd 9a 36	. . 6
	exx			;3457	d9		.
	ld a,050h		;3458	3e 50		> P
	out (0e2h),a		;345a	d3 e2		. .
	exx			;345c	d9		.
LP_PRINT_LN3:
				; = LP_PRINT_LN3 (src: FUNC_LOW_PRINT.ASM:327, BIOS-TT 0271ac3)
	exx			;345d	d9		.
	ld a,d			;345e	7a		z
	out (089h),a		;345f	d3 89		. .
	inc d			;3461	14		.
	exx			;3462	d9		.
	ld a,(hl)		;3463	7e		~
	inc hl			;3464	23		#
	cp d			;3465	ba		.
	jr nz,LP_PR_L31		;3466	20 03		  .
	dec hl			;3468	2b		+
	ld a,020h		;3469	3e 20		>  
LP_PR_L31:
				; = LP_PR_L31 (src: FUNC_LOW_PRINT.ASM:340, BIOS-TT 0271ac3)
	exx			;346b	d9		.
	ld (hl),a		;346c	77		w
	exx			;346d	d9		.
	ld a,e			;346e	7b		{
	exx			;346f	d9		.
	inc l			;3470	2c		,
	ld (hl),a		;3471	77		w
	dec l			;3472	2d		-
	djnz l3478h		;3473	10 03		. .
	call LP_NEXT_HL		;3475	cd 78 36	. x 6
l3478h:
	exx			;3478	d9		.
	djnz LP_PRINT_LN3	;3479	10 e2		. .
	ld a,0feh		;347b	3e fe		> .
	out (0e2h),a		;347d	d3 e2		. .
	call LP_END_P		;347f	cd 89 37	. . 7
	ret			;3482	c9		.
Fn88_LP_PRINT_LINE4:
				; = LP_PRINT_LINE4 (src: FUNC_LOW_PRINT.ASM:367, BIOS-TT 0271ac3)
	call LP_BEG_P		;3483	cd 9a 36	. . 6
	exx			;3486	d9		.
	ld a,050h		;3487	3e 50		> P
	out (0e2h),a		;3489	d3 e2		. .
	exx			;348b	d9		.
LP_PRINT_LN4:
				; = LP_PRINT_LN4 (src: FUNC_LOW_PRINT.ASM:375, BIOS-TT 0271ac3)
	exx			;348c	d9		.
	ld a,d			;348d	7a		z
	out (089h),a		;348e	d3 89		. .
	exx			;3490	d9		.
	ld a,(hl)		;3491	7e		~
	inc hl			;3492	23		#
	cp d			;3493	ba		.
	jr nz,LP_PR_L41		;3494	20 03		  .
	dec hl			;3496	2b		+
	ld a,020h		;3497	3e 20		>  
LP_PR_L41:
				; = LP_PR_L41 (src: FUNC_LOW_PRINT.ASM:387, BIOS-TT 0271ac3)
	exx			;3499	d9		.
	ld (hl),a		;349a	77		w
	inc d			;349b	14		.
	djnz l34a1h		;349c	10 03		. .
	call LP_NEXT_HL		;349e	cd 78 36	. x 6
l34a1h:
	exx			;34a1	d9		.
	djnz LP_PRINT_LN4	;34a2	10 e8		. .
	ld a,0feh		;34a4	3e fe		> .
	out (0e2h),a		;34a6	d3 e2		. .
	call LP_END_P		;34a8	cd 89 37	. . 7
	ret			;34ab	c9		.
Fn8B_LP_PRINT_LINE5:
				; = LP_PRINT_LINE5 (src: FUNC_LOW_PRINT.ASM:410, BIOS-TT 0271ac3)
	call LP_BEG_P		;34ac	cd 9a 36	. . 6
	exx			;34af	d9		.
	ld a,050h		;34b0	3e 50		> P
	out (0e2h),a		;34b2	d3 e2		. .
	exx			;34b4	d9		.
LP_PRINT_LN5:
				; = LP_PRINT_LN5 (src: FUNC_LOW_PRINT.ASM:418, BIOS-TT 0271ac3)
	exx			;34b5	d9		.
	ld a,d			;34b6	7a		z
	out (089h),a		;34b7	d3 89		. .
	exx			;34b9	d9		.
	ld a,(hl)		;34ba	7e		~
	inc hl			;34bb	23		#
	cp d			;34bc	ba		.
	jr z,LP_PR_L51		;34bd	28 11		( .
	exx			;34bf	d9		.
	ld (hl),a		;34c0	77		w
	exx			;34c1	d9		.
	ld a,e			;34c2	7b		{
	exx			;34c3	d9		.
	inc l			;34c4	2c		,
	ld (hl),a		;34c5	77		w
	dec l			;34c6	2d		-
	inc d			;34c7	14		.
	djnz l34cdh		;34c8	10 03		. .
	call LP_NEXT_HL		;34ca	cd 78 36	. x 6
l34cdh:
	exx			;34cd	d9		.
	djnz LP_PRINT_LN5	;34ce	10 e5		. .
LP_PR_L51:
				; = LP_PR_L51 (src: FUNC_LOW_PRINT.ASM:450, BIOS-TT 0271ac3)
	ld a,0feh		;34d0	3e fe		> .
	out (0e2h),a		;34d2	d3 e2		. .
	call LP_END_P		;34d4	cd 89 37	. . 7
	ret			;34d7	c9		.
Fn8C_LP_PRINT_LINE6:
				; = LP_PRINT_LINE6 (src: FUNC_LOW_PRINT.ASM:456, BIOS-TT 0271ac3)
	call LP_BEG_P		;34d8	cd 9a 36	. . 6
	exx			;34db	d9		.
	ld a,050h		;34dc	3e 50		> P
	out (0e2h),a		;34de	d3 e2		. .
	exx			;34e0	d9		.
LP_PRINT_LN6:
				; = LP_PRINT_LN6 (src: FUNC_LOW_PRINT.ASM:464, BIOS-TT 0271ac3)
	exx			;34e1	d9		.
	ld a,d			;34e2	7a		z
	out (089h),a		;34e3	d3 89		. .
	exx			;34e5	d9		.
	ld a,(hl)		;34e6	7e		~
	inc hl			;34e7	23		#
	cp d			;34e8	ba		.
	jr z,LP_PR_L61		;34e9	28 0b		( .
	exx			;34eb	d9		.
	ld (hl),a		;34ec	77		w
	inc d			;34ed	14		.
	djnz l34f3h		;34ee	10 03		. .
	call LP_NEXT_HL		;34f0	cd 78 36	. x 6
l34f3h:
	exx			;34f3	d9		.
	djnz LP_PRINT_LN6	;34f4	10 eb		. .
LP_PR_L61:
				; = LP_PR_L61 (src: FUNC_LOW_PRINT.ASM:489, BIOS-TT 0271ac3)
	ld a,0feh		;34f6	3e fe		> .
	out (0e2h),a		;34f8	d3 e2		. .
	call LP_END_P		;34fa	cd 89 37	. . 7
	ret			;34fd	c9		.
FnE0_LP_PR_LINE_DIR:
				; = LP_PRINT_LINE_DIR (src: FUNC_LOW_PRINT.ASM:495, BIOS-TT 0271ac3)
	call LP_BEG_P		;34fe	cd 9a 36	. . 6
	exx			;3501	d9		.
	ld a,050h		;3502	3e 50		> P
	out (0e2h),a		;3504	d3 e2		. .
	exx			;3506	d9		.
LP_PRINT_LN_D:
				; = LP_PRINT_LN_D (src: FUNC_LOW_PRINT.ASM:504, BIOS-TT 0271ac3)
	exx			;3507	d9		.
	ld a,d			;3508	7a		z
	out (089h),a		;3509	d3 89		. .
	exx			;350b	d9		.
	ld a,(hl)		;350c	7e		~
	inc hl			;350d	23		#
	cp b			;350e	b8		.
	jr z,LP_LN_DD1		;350f	28 29		( )
	exx			;3511	d9		.
	cp 00eh			;3512	fe 0e		. .
	jr nc,LP_XX		;3514	30 1a		0 .
	cp 007h			;3516	fe 07		. .
	jr c,LP_XX		;3518	38 16		8 .
	sub 007h		;351a	d6 07		. .
	jr z,LP_BEEP		;351c	28 2a		( *
	dec a			;351e	3d		=
	jr z,LP_BACK		;351f	28 29		( )
	dec a			;3521	3d		=
	jr z,LP_TAB		;3522	28 34		( 4
	dec a			;3524	3d		=
	jr z,LP_LF		;3525	28 5d		( ]
	dec a			;3527	3d		=
	jr z,LP_XX		;3528	28 06		( .
	dec a			;352a	3d		=
	jr z,LP_CLS		;352b	28 6b		( k
	dec a			;352d	3d		=
	jr z,LP_CR		;352e	28 6a		( j
LP_XX:
				; = LP_XX (src: FUNC_LOW_PRINT.ASM:535, BIOS-TT 0271ac3)
				; = LP_PRINT_LN_DD (src: FUNC_LOW_PRINT.ASM:537, BIOS-TT 0271ac3)
	ld (hl),a		;3530	77		w
	inc d			;3531	14		.
l3532h:
	djnz l3537h		;3532	10 03		. .
	call LP_NEXT_HL		;3534	cd 78 36	. x 6
l3537h:
	exx			;3537	d9		.
	jr LP_PRINT_LN_D	;3538	18 cd		. .
LP_LN_DD1:
				; = LP_LN_DD1 (src: FUNC_LOW_PRINT.ASM:547, BIOS-TT 0271ac3)
	ld a,0feh		;353a	3e fe		> .
	out (0e2h),a		;353c	d3 e2		. .
	call LP_END_P		;353e	cd 89 37	. . 7
	ret			;3541	c9		.
LP_PRINT_LN_D11:
				; = LP_PRINT_LN_D11 (src: FUNC_LOW_PRINT.ASM:553, BIOS-TT 0271ac3)
	ld a,050h		;3542	3e 50		> P
	out (0e2h),a		;3544	d3 e2		. .
	jr l3532h		;3546	18 ea		. .
LP_BEEP:
				; = LP_BEEP (src: FUNC_LOW_PRINT.ASM:558, BIOS-TT 0271ac3)
	jr l3532h		;3548	18 e8		. .
LP_BACK:
				; = LP_BACK (src: FUNC_LOW_PRINT.ASM:561, BIOS-TT 0271ac3)
	ld a,0feh		;354a	3e fe		> .
	out (0e2h),a		;354c	d3 e2		. .
	ld a,(0e010h)		;354e	3a 10 e0	: . .
	cp d			;3551	ba		.
	jr z,LP_PRINT_LN_D11	;3552	28 ee		( .
	inc b			;3554	04		.
	dec d			;3555	15		.
	jr LP_PRINT_LN_D11	;3556	18 ea		. .
LP_TAB:
				; = LP_TAB (src: FUNC_LOW_PRINT.ASM:572, BIOS-TT 0271ac3)
	ld a,0feh		;3558	3e fe		> .
	out (0e2h),a		;355a	d3 e2		. .
	ld a,(0e010h)		;355c	3a 10 e0	: . .
	sub d			;355f	92		.
	neg			;3560	ed 44		. D
	and 007h		;3562	e6 07		. .
	neg			;3564	ed 44		. D
	add a,008h		;3566	c6 08		. .
	ld c,a			;3568	4f		O
	ld a,050h		;3569	3e 50		> P
	out (0e2h),a		;356b	d3 e2		. .
LP_TAB_L:
				; = LP_TAB_L (src: FUNC_LOW_PRINT.ASM:587, BIOS-TT 0271ac3)
	ld (hl),020h		;356d	36 20		6  
	inc d			;356f	14		.
	dec b			;3570	05		.
	jr z,LP_TAB_L1		;3571	28 05		( .
	dec c			;3573	0d		.
	jr nz,LP_TAB_L		;3574	20 f7		  .
	jr l3532h		;3576	18 ba		. .
LP_TAB_L1:
				; = LP_TAB_L1 (src: FUNC_LOW_PRINT.ASM:596, BIOS-TT 0271ac3)
	ld a,0feh		;3578	3e fe		> .
	out (0e2h),a		;357a	d3 e2		. .
	ld a,(0e010h)		;357c	3a 10 e0	: . .
	ld d,a			;357f	57		W
	ld a,(0e012h)		;3580	3a 12 e0	: . .
	ld b,a			;3583	47		G
LP_LF:
				; = LP_LF (src: FUNC_LOW_PRINT.ASM:604, BIOS-TT 0271ac3)
	ld a,0feh		;3584	3e fe		> .
	out (0e2h),a		;3586	d3 e2		. .
	inc l			;3588	2c		,
	inc l			;3589	2c		,
	inc l			;358a	2c		,
	inc l			;358b	2c		,
	ld a,(0e00fh)		;358c	3a 0f e0	: . .
	cp l			;358f	bd		.
	jr nc,LP_PRINT_LN_D11	;3590	30 b0		0 .
	ld a,(0e00eh)		;3592	3a 0e e0	: . .
	ld l,a			;3595	6f		o
	jr LP_PRINT_LN_D11	;3596	18 aa		. .
LP_CLS:
				; = LP_CLS (src: FUNC_LOW_PRINT.ASM:624, BIOS-TT 0271ac3)
	jr l3532h		;3598	18 98		. .
LP_CR:
				; = LP_CR (src: FUNC_LOW_PRINT.ASM:628, BIOS-TT 0271ac3)
	ld a,0feh		;359a	3e fe		> .
	out (0e2h),a		;359c	d3 e2		. .
	ld a,(0e010h)		;359e	3a 10 e0	: . .
	ld d,a			;35a1	57		W
	ld a,(0e012h)		;35a2	3a 12 e0	: . .
	ld b,a			;35a5	47		G
	jr LP_PRINT_LN_D11	;35a6	18 9a		. .
Fn84_LP_SET_PLACE:
				; = LP_SET_PLACE (src: FUNC_LOW_PRINT.ASM:641, BIOS-TT 0271ac3)
	call LP_BEG_P		;35a8	cd 9a 36	. . 6
	call LP_AT_D		;35ab	cd 3a 36	. : 6
	call LP_TAB_E		;35ae	cd 53 36	. S 6
	call LP_END_P		;35b1	cd 89 37	. . 7
	ret			;35b4	c9		.
Fn8E_LP_GET_PLACE:
				; = LP_GET_PLACE (src: FUNC_LOW_PRINT.ASM:648, BIOS-TT 0271ac3)
	call LP_BEG_P		;35b5	cd 9a 36	. . 6
	ld a,(0e010h)		;35b8	3a 10 e0	: . .
	neg			;35bb	ed 44		. D
	exx			;35bd	d9		.
	add a,d			;35be	82		.
	exx			;35bf	d9		.
	ld e,a			;35c0	5f		_
	ld a,(0e00eh)		;35c1	3a 0e e0	: . .
	neg			;35c4	ed 44		. D
	exx			;35c6	d9		.
	add a,l			;35c7	85		.
	dec a			;35c8	3d		=
	exx			;35c9	d9		.
	rrca			;35ca	0f		.
	rrca			;35cb	0f		.
	and 03fh		;35cc	e6 3f		. ?
	ld d,a			;35ce	57		W
	call LP_END_P		;35cf	cd 89 37	. . 7
	ret			;35d2	c9		.
Fn89_LP_CLS_WIN:
				; = LP_CLS_WIN (src: FUNC_LOW_PRINT.ASM:677, BIOS-TT 0271ac3)
	ld c,020h		;35d3	0e 20		.  
	jr LP_CLS_WIN_3		;35d5	18 01		. .
Fn8D_LP_CLS_WIN2:
				; = LP_CLS_WIN2 (src: FUNC_LOW_PRINT.ASM:680, BIOS-TT 0271ac3)
	ld c,a			;35d7	4f		O
LP_CLS_WIN_3:
				; = LP_CLS_WIN_3 (src: FUNC_LOW_PRINT.ASM:682, BIOS-TT 0271ac3)
	call LP_BEG_P		;35d8	cd 9a 36	. . 6
	ld (0c150h),bc		;35db	ed 43 50 c1	. C P .
	push de			;35df	d5		.
LP_CLS_L2:
				; = LP_CLS_L2 (src: FUNC_LOW_PRINT.ASM:686, BIOS-TT 0271ac3)
	call LP_AT_D		;35e0	cd 3a 36	. : 6
	call LP_TAB_E		;35e3	cd 53 36	. S 6
	push de			;35e6	d5		.
	exx			;35e7	d9		.
	ld bc,(0c150h)		;35e8	ed 4b 50 c1	. K P .
	exx			;35ec	d9		.
	ld b,l			;35ed	45		E
	ld a,050h		;35ee	3e 50		> P
	out (0e2h),a		;35f0	d3 e2		. .
LP_CLS_L1:
				; = LP_CLS_L1 (src: FUNC_LOW_PRINT.ASM:699, BIOS-TT 0271ac3)
	exx			;35f2	d9		.
	ld a,d			;35f3	7a		z
	out (089h),a		;35f4	d3 89		. .
	ld (hl),c		;35f6	71		q
	inc l			;35f7	2c		,
	ld (hl),b		;35f8	70		p
	dec l			;35f9	2d		-
	inc d			;35fa	14		.
	exx			;35fb	d9		.
	djnz LP_CLS_L1		;35fc	10 f4		. .
	ld a,0feh		;35fe	3e fe		> .
	out (0e2h),a		;3600	d3 e2		. .
	pop de			;3602	d1		.
	inc d			;3603	14		.
	dec h			;3604	25		%
	jr nz,LP_CLS_L2		;3605	20 d9		  .
	pop de			;3607	d1		.
	call LP_AT_D		;3608	cd 3a 36	. : 6
	call LP_TAB_E		;360b	cd 53 36	. S 6
	call LP_END_P		;360e	cd 89 37	. . 7
	ret			;3611	c9		.
sub_3612h:
	in a,(0e2h)		;3612	db e2		. .
	ld c,a			;3614	4f		O
	ld a,0feh		;3615	3e fe		> .
	out (0e2h),a		;3617	d3 e2		. .
	ld a,c			;3619	79		y
	ld (0c107h),a		;361a	32 07 c1	2 . .
	in a,(089h)		;361d	db 89		. .
	ld (0c11dh),a		;361f	32 1d c1	2 . .
	ld de,(0e000h)		;3622	ed 5b 00 e0	. [ . .
	ld a,(0e004h)		;3626	3a 04 e0	: . .
	bit 5,a			;3629	cb 6f		. o
	ld a,(0c11dh)		;362b	3a 1d c1	: . .
	out (089h),a		;362e	d3 89		. .
	ld a,(0c107h)		;3630	3a 07 c1	: . .
	out (0e2h),a		;3633	d3 e2		. .
	ret nz			;3635	c0		.
	ld a,e			;3636	7b		{
	add a,a			;3637	87		.
	ld e,a			;3638	5f		_
	ret			;3639	c9		.
LP_AT_D:
				; = LP_AT_D (src: FUNC_LOW_PRINT.ASM:752, BIOS-TT 0271ac3)
	ld a,(0e001h)		;363a	3a 01 e0	: . .
	exx			;363d	d9		.
	ld l,a			;363e	6f		o
	exx			;363f	d9		.
	ld a,d			;3640	7a		z
	exx			;3641	d9		.
LP_AT_DX:
				; = LP_AT_DX (src: FUNC_LOW_PRINT.ASM:759, BIOS-TT 0271ac3)
	sub l			;3642	95		.
	jr nc,LP_AT_DX		;3643	30 fd		0 .
	add a,l			;3645	85		.
	add a,a			;3646	87		.
	add a,a			;3647	87		.
	ld l,a			;3648	6f		o
	ld a,(0e00eh)		;3649	3a 0e e0	: . .
	add a,l			;364c	85		.
	ld l,a			;364d	6f		o
	inc l			;364e	2c		,
	ld h,0c3h		;364f	26 c3		& .
	exx			;3651	d9		.
	ret			;3652	c9		.
LP_TAB_E:
				; = LP_TAB_E (src: FUNC_LOW_PRINT.ASM:777, BIOS-TT 0271ac3)
	ld a,(0e012h)		;3653	3a 12 e0	: . .
	exx			;3656	d9		.
	ld d,a			;3657	57		W
	exx			;3658	d9		.
	ld a,e			;3659	7b		{
	exx			;365a	d9		.
LP_TAB_EX:
				; = LP_TAB_EX (src: FUNC_LOW_PRINT.ASM:784, BIOS-TT 0271ac3)
	sub d			;365b	92		.
	jr nc,LP_TAB_EX		;365c	30 fd		0 .
	jr z,LP_TAB_EX		;365e	28 fb		( .
	neg			;3660	ed 44		. D
	ld b,a			;3662	47		G
	neg			;3663	ed 44		. D
	add a,d			;3665	82		.
	ld d,a			;3666	57		W
	ld a,(0e004h)		;3667	3a 04 e0	: . .
	bit 5,a			;366a	cb 6f		. o
	jr z,LP_NO_ADD_A	;366c	28 03		( .
	ld a,d			;366e	7a		z
	add a,a			;366f	87		.
	ld d,a			;3670	57		W
LP_NO_ADD_A:
				; = LP_NO_ADD_A (src: FUNC_LOW_PRINT.ASM:801, BIOS-TT 0271ac3)
	ld a,(0e010h)		;3671	3a 10 e0	: . .
	add a,d			;3674	82		.
	ld d,a			;3675	57		W
	exx			;3676	d9		.
	ret			;3677	c9		.
LP_NEXT_HL:
				; = LP_NEXT_HL (src: FUNC_LOW_PRINT.ASM:812, BIOS-TT 0271ac3)
	ld a,0feh		;3678	3e fe		> .
	out (0e2h),a		;367a	d3 e2		. .
	inc l			;367c	2c		,
	inc l			;367d	2c		,
	inc l			;367e	2c		,
	inc l			;367f	2c		,
	ld a,(0e00fh)		;3680	3a 0f e0	: . .
	cp l			;3683	bd		.
	jr nc,LP_NEXT_HL1	;3684	30 07		0 .
	ld a,(0e00eh)		;3686	3a 0e e0	: . .
	ld l,a			;3689	6f		o
	inc l			;368a	2c		,
	ld h,0c3h		;368b	26 c3		& .
LP_NEXT_HL1:
				; = LP_NEXT_HL1 (src: FUNC_LOW_PRINT.ASM:830, BIOS-TT 0271ac3)
	ld a,(0e010h)		;368d	3a 10 e0	: . .
	ld d,a			;3690	57		W
	ld a,(0e012h)		;3691	3a 12 e0	: . .
	ld b,a			;3694	47		G
	ld a,050h		;3695	3e 50		> P
	out (0e2h),a		;3697	d3 e2		. .
	ret			;3699	c9		.
LP_BEG_P:
				; = LP_BEG_P (src: FUNC_LOW_PRINT.ASM:844, BIOS-TT 0271ac3)
	ex af,af'		;369a	08		.
	exx			;369b	d9		.
	in a,(0e2h)		;369c	db e2		. .
	ld c,a			;369e	4f		O
	ld a,0feh		;369f	3e fe		> .
	out (0e2h),a		;36a1	d3 e2		. .
	ld a,c			;36a3	79		y
	ld (0c107h),a		;36a4	32 07 c1	2 . .
	in a,(089h)		;36a7	db 89		. .
	ld (0c11dh),a		;36a9	32 1d c1	2 . .
	ld hl,(0e008h)		;36ac	2a 08 e0	* . .
	ld de,(0e00ch)		;36af	ed 5b 0c e0	. [ . .
	ld bc,(0e00ah)		;36b3	ed 4b 0a e0	. K . .
	ld a,e			;36b7	7b		{
	and a			;36b8	a7		.
	rra			;36b9	1f		.
	out (089h),a		;36ba	d3 89		. .
	exx			;36bc	d9		.
	ex af,af'		;36bd	08		.
	ret			;36be	c9		.
FnB8_WIN_GET_ZG:
				; = WIN_GET_ZG (src: FUNC_LOW_PRINT.ASM:862, BIOS-TT 0271ac3)
	ld hl,Font8x8	;36bf	21 00 28	! . (
	ld bc,00800h		;36c2	01 00 08	. . .
	ldir			;36c5	ed b0		. .
	and a			;36c7	a7		.
	ret			;36c8	c9		.
FnB6_WIN_SET_ZG:
				; = WIN_SET_ZG (src: FUNC_LOW_PRINT.ASM:869, BIOS-TT 0271ac3)
				; = LP_SET_ZG (src: FUNC_LOW_PRINT.ASM:870, BIOS-TT 0271ac3)
	ex af,af'		;36c9	08		.
	exx			;36ca	d9		.
	in a,(0e2h)		;36cb	db e2		. .
	ld c,a			;36cd	4f		O
	ld a,0feh		;36ce	3e fe		> .
	out (0e2h),a		;36d0	d3 e2		. .
	ld a,c			;36d2	79		y
	ld (0c107h),a		;36d3	32 07 c1	2 . .
	in a,(089h)		;36d6	db 89		. .
	ld (0c11dh),a		;36d8	32 1d c1	2 . .
	call LP_SET_ZG1		;36db	cd ec 36	. . 6
	ld a,(0c11dh)		;36de	3a 1d c1	: . .
	out (089h),a		;36e1	d3 89		. .
	ld a,(0c107h)		;36e3	3a 07 c1	: . .
	out (0e2h),a		;36e6	d3 e2		. .
	exx			;36e8	d9		.
	ex af,af'		;36e9	08		.
	and a			;36ea	a7		.
	ret			;36eb	c9		.
LP_SET_ZG1:
				; = LP_SET_ZG1 (src: FUNC_LOW_PRINT.ASM:884, BIOS-TT 0271ac3)
	in a,(0a2h)		;36ec	db a2		. .
	ld (0c105h),a		;36ee	32 05 c1	2 . .
	ld a,0ffh		;36f1	3e ff		> .
	out (0a2h),a		;36f3	d3 a2		. .
	exx			;36f5	d9		.
	ld bc,l204eh		;36f6	01 4e 20	. N  
	in a,(c)		;36f9	ed 78		. x
	ld (0c150h),a		;36fb	32 50 c1	2 P .
	and 0feh		;36fe	e6 fe		. .
	out (c),a		;3700	ed 79		. y
	ex af,af'		;3702	08		.
	ld b,a			;3703	47		G
	and 00fh		;3704	e6 0f		. .
	add a,a			;3706	87		.
	out (089h),a		;3707	d3 89		. .
	ld a,b			;3709	78		x
	rrca			;370a	0f		.
	rrca			;370b	0f		.
	rrca			;370c	0f		.
	and 018h		;370d	e6 18		. .
	or 040h			;370f	f6 40		. @
	ld h,a			;3711	67		g
	ld l,000h		;3712	2e 00		. .
	ld bc,00800h		;3714	01 00 08	. . .
	ex de,hl		;3717	eb		.
	ldir			;3718	ed b0		. .
	ex de,hl		;371a	eb		.
	ld a,h			;371b	7c		|
	rrca			;371c	0f		.
	rrca			;371d	0f		.
	rrca			;371e	0f		.
	dec a			;371f	3d		=
	and 003h		;3720	e6 03		. .
	add a,058h		;3722	c6 58		. X
	ld h,a			;3724	67		g
LP_INI_L1:
				; = LP_INI_L1 (src: FUNC_LOW_PRINT.ASM:925, BIOS-TT 0271ac3)
	ld (hl),l		;3725	75		u
	inc l			;3726	2c		,
	jr nz,LP_INI_L1		;3727	20 fc		  .
	ld a,(0c150h)		;3729	3a 50 c1	: P .
	ld bc,l204eh		;372c	01 4e 20	. N  
	out (c),a		;372f	ed 79		. y
	exx			;3731	d9		.
	ex af,af'		;3732	08		.
	ld a,(0c105h)		;3733	3a 05 c1	: . .
	out (0a2h),a		;3736	d3 a2		. .
	ret			;3738	c9		.
LP_INI_P:
				; = LP_INI_P (src: FUNC_LOW_PRINT.ASM:942, BIOS-TT 0271ac3)
	ex af,af'		;3739	08		.
	exx			;373a	d9		.
	in a,(0e2h)		;373b	db e2		. .
	ld c,a			;373d	4f		O
	ld a,0feh		;373e	3e fe		> .
	out (0e2h),a		;3740	d3 e2		. .
	ld a,c			;3742	79		y
	ld (0c107h),a		;3743	32 07 c1	2 . .
	in a,(089h)		;3746	db 89		. .
	ld (0c11dh),a		;3748	32 1d c1	2 . .
	ld a,(0e004h)		;374b	3a 04 e0	: . .
	cp 0c0h			;374e	fe c0		. .
	jr nc,LP_INI_NO_ZG	;3750	30 19		0 .
	ld a,(0e005h)		;3752	3a 05 e0	: . .
	bit 0,a			;3755	cb 47		. G
	jr nz,LP_INI_NO_ZG	;3757	20 12		  .
	ld a,(0e004h)		;3759	3a 04 e0	: . .
	bit 4,a			;375c	cb 67		. g
	jr z,LP_INI_NO_ZG	;375e	28 0b		( .
	ld de,(0c14ah)		;3760	ed 5b 4a c1	. [ J .
	exx			;3764	d9		.
	ex af,af'		;3765	08		.
	call LP_SET_ZG1		;3766	cd ec 36	. . 6
	exx			;3769	d9		.
	ex af,af'		;376a	08		.
LP_INI_NO_ZG:
				; = LP_INI_NO_ZG (src: FUNC_LOW_PRINT.ASM:967, BIOS-TT 0271ac3)
	ld a,(0e010h)		;376b	3a 10 e0	: . .
	ld d,a			;376e	57		W
	ld a,(0e00eh)		;376f	3a 0e e0	: . .
	ld l,a			;3772	6f		o
	inc l			;3773	2c		,
	ld h,0c3h		;3774	26 c3		& .
	and a			;3776	a7		.
	ld a,(0e004h)		;3777	3a 04 e0	: . .
	bit 5,a			;377a	cb 6f		. o
	ld a,(0e000h)		;377c	3a 00 e0	: . .
	jr nz,LP_INI_40		;377f	20 02		  .
	add a,a			;3781	87		.
	scf			;3782	37		7
LP_INI_40:
				; = LP_INI_40 (src: FUNC_LOW_PRINT.ASM:982, BIOS-TT 0271ac3)
	ld b,a			;3783	47		G
	ld (0e012h),a		;3784	32 12 e0	2 . .
	ex af,af'		;3787	08		.
	exx			;3788	d9		.
LP_END_P:
				; = LP_END_P (src: FUNC_LOW_PRINT.ASM:997, BIOS-TT 0271ac3)
	ex af,af'		;3789	08		.
	exx			;378a	d9		.
	rla			;378b	17		.
	ld e,a			;378c	5f		_
	ld (0e008h),hl		;378d	22 08 e0	" . .
	ld (0e00ch),de		;3790	ed 53 0c e0	. S . .
	ld (0e00ah),bc		;3794	ed 43 0a e0	. C . .
	ld a,(0c11dh)		;3798	3a 1d c1	: . .
	out (089h),a		;379b	d3 89		. .
	ld a,(0c107h)		;379d	3a 07 c1	: . .
	out (0e2h),a		;37a0	d3 e2		. .
	exx			;37a2	d9		.
	ex af,af'		;37a3	08		.
	and a			;37a4	a7		.
	ret			;37a5	c9		.
LP_END_P2:
				; = LP_END_P2 (src: FUNC_LOW_PRINT.ASM:1012, BIOS-TT 0271ac3)
	ex af,af'		;37a6	08		.
	exx			;37a7	d9		.
	ld a,(0c11dh)		;37a8	3a 1d c1	: . .
	out (089h),a		;37ab	d3 89		. .
	ld a,(0c107h)		;37ad	3a 07 c1	: . .
	out (0e2h),a		;37b0	d3 e2		. .
	exx			;37b2	d9		.
	ex af,af'		;37b3	08		.
	ret			;37b4	c9		.
Fn80_LP_OPEN_S:
				; = LP_OPEN_S (src: FUNC_LOW_PRINT.ASM:1023, BIOS-TT 0271ac3)
	ld a,b			;37b5	78		x
	add a,a			;37b6	87		.
	cp 014h			;37b7	fe 14		. .
	ccf			;37b9	3f		?
	ret c			;37ba	d8		.
	push hl			;37bb	e5		.
	ld hl,Table37CB	;37bc	21 cb 37	! . 7
	add a,l			;37bf	85		.
	ld l,a			;37c0	6f		o
	ld a,h			;37c1	7c		|
	adc a,000h		;37c2	ce 00		. .
	ld h,a			;37c4	67		g
	ld a,(hl)		;37c5	7e		~
	inc hl			;37c6	23		#
	ld h,(hl)		;37c7	66		f
	ld l,a			;37c8	6f		o
	ex (sp),hl		;37c9	e3		.
	ret			;37ca	c9		.

; BLOCK 'Table37CB' (start 0x37cb end 0x37df)
Table37CB:
	defb 0dfh		;37cb	df		.
	defb 037h		;37cc	37		7
	defb 0e8h		;37cd	e8		.
	defb 037h		;37ce	37		7
	defb 0f1h		;37cf	f1		.
	defb 037h		;37d0	37		7
	defb 0fah		;37d1	fa		.
	defb 037h		;37d2	37		7
	defb 0e2h		;37d3	e2		.
	defb 037h		;37d4	37		7
	defb 0ebh		;37d5	eb		.
	defb 037h		;37d6	37		7
	defb 0f4h		;37d7	f4		.
	defb 037h		;37d8	37		7
	defb 0fdh		;37d9	fd		.
	defb 037h		;37da	37		7
	defb 003h		;37db	03		.
	defb 038h		;37dc	38		8
	defb 009h		;37dd	09		.
	defb 038h		;37de	38		8
LP_SET_32:
				; = LP_SET_32 (src: FUNC_LOW_PRINT.ASM:1065, BIOS-TT 0271ac3)
	ld hl,04104h		;37df	21 04 41	! . A
LP_SET_32X:
				; = LP_SET_32X (src: FUNC_LOW_PRINT.ASM:1067, BIOS-TT 0271ac3)
	ld ix,LP_SCR_32		;37e2	dd 21 15 0f	. ! . .
	jr FnA0_PIC_FN0		;37e6	18 59		. Y
LP_SET_64:
				; = LP_SET_64 (src: FUNC_LOW_PRINT.ASM:1071, BIOS-TT 0271ac3)
	ld hl,04104h		;37e8	21 04 41	! . A
LP_SET_64X:
				; = LP_SET_64X (src: FUNC_LOW_PRINT.ASM:1073, BIOS-TT 0271ac3)
	ld ix,LP_SCR_64		;37eb	dd 21 25 0f	. ! % .
	jr FnA0_PIC_FN0		;37ef	18 50		. P
LP_SET_40:
				; = LP_SET_40 (src: FUNC_LOW_PRINT.ASM:1077, BIOS-TT 0271ac3)
	ld hl,04000h		;37f1	21 00 40	! . @
LP_SET_40X:
				; = LP_SET_40X (src: FUNC_LOW_PRINT.ASM:1079, BIOS-TT 0271ac3)
	ld ix,LP_SCR_40		;37f4	dd 21 05 0f	. ! . .
	jr FnA0_PIC_FN0		;37f8	18 47		. G
LP_SET_80:
				; = LP_SET_80 (src: FUNC_LOW_PRINT.ASM:1083, BIOS-TT 0271ac3)
	ld hl,04000h		;37fa	21 00 40	! . @
LP_SET_80X:
				; = LP_SET_80X (src: FUNC_LOW_PRINT.ASM:1085, BIOS-TT 0271ac3)
	ld ix,LP_SCR_80		;37fd	dd 21 f5 0e	. ! . .
	jr FnA0_PIC_FN0		;3801	18 3e		. >
PIC_SET_S1:
				; = PIC_SET_S1 (src: FUNC_LOW_PRINT.ASM:1089, BIOS-TT 0271ac3)
	ld ix,PIC_320X256_1	;3803	dd 21 35 0f	. ! 5 .
	jr FnA0_PIC_FN0		;3807	18 38		. 8
PIC_SET_S2:
				; = PIC_SET_S2 (src: FUNC_LOW_PRINT.ASM:1093, BIOS-TT 0271ac3)
	ld ix,PIC_320X256_2	;3809	dd 21 45 0f	. ! E .
	jr FnA0_PIC_FN0		;380d	18 32		. 2
FnB0_WIN_OPEN:
	in a,(0e2h)		;380f	db e2		. .
	ld c,a			;3811	4f		O
	ld a,0feh		;3812	3e fe		> .
	out (0e2h),a		;3814	d3 e2		. .
	ld a,c			;3816	79		y
	ld (0c107h),a		;3817	32 07 c1	2 . .
	in a,(089h)		;381a	db 89		. .
	ld (0c11dh),a		;381c	32 1d c1	2 . .
	ld (0c140h),ix		;381f	dd 22 40 c1	. " @ .
	push hl			;3823	e5		.
	push de			;3824	d5		.
	ld hl,(0c140h)		;3825	2a 40 c1	* @ .
	ld de,0e000h		;3828	11 00 e0	. . .
	ld bc,RST_20		;382b	01 20 00	.   .
	ldir			;382e	ed b0		. .
	ld ix,0e000h		;3830	dd 21 00 e0	. ! . .
	pop de			;3834	d1		.
	pop hl			;3835	e1		.
	ld l,(ix+002h)		;3836	dd 6e 02	. n .
	ld h,(ix+003h)		;3839	dd 66 03	. f .
	ld (ix+013h),e		;383c	dd 73 13	. s .
	jr WIN_OPEN_W1		;383f	18 3f		. ?
FnA0_PIC_FN0:
				; = PIC_FN0 (src: FUNC_LOW_PRINT.ASM:1122, BIOS-TT 0271ac3)
				; = LP_SET_MODE (src: FUNC_LOW_PRINT.ASM:1123, BIOS-TT 0271ac3)
	ld a,h			;3841	7c		|
	and 010h		;3842	e6 10		. .
	xor e			;3844	ab		.
	ld e,a			;3845	5f		_
	ld a,l			;3846	7d		}
	and 03fh		;3847	e6 3f		. ?
	add hl,hl		;3849	29		)
	add hl,hl		;384a	29		)
	ld l,a			;384b	6f		o
	res 7,h			;384c	cb bc		. .
	res 6,h			;384e	cb b4		. .
	in a,(0e2h)		;3850	db e2		. .
	ld c,a			;3852	4f		O
	ld a,0feh		;3853	3e fe		> .
	out (0e2h),a		;3855	d3 e2		. .
	ld a,c			;3857	79		y
	ld (0c107h),a		;3858	32 07 c1	2 . .
	in a,(089h)		;385b	db 89		. .
	ld (0c11dh),a		;385d	32 1d c1	2 . .
	ld (0c140h),ix		;3860	dd 22 40 c1	. " @ .
	push hl			;3864	e5		.
	push de			;3865	d5		.
	ld hl,(0c140h)		;3866	2a 40 c1	* @ .
	ld de,0e000h		;3869	11 00 e0	. . .
	ld bc,RST_20		;386c	01 20 00	.   .
	ldir			;386f	ed b0		. .
	ld ix,0e000h		;3871	dd 21 00 e0	. ! . .
	pop de			;3875	d1		.
	pop hl			;3876	e1		.
	ld (ix+002h),l		;3877	dd 75 02	. u .
	ld (ix+003h),h		;387a	dd 74 03	. t .
	ld (ix+013h),e		;387d	dd 73 13	. s .
WIN_OPEN_W1:
				; = WIN_OPEN_W1 (src: FUNC_LOW_PRINT.ASM:1157, BIOS-TT 0271ac3)
	ld a,l			;3880	7d		}
	add a,a			;3881	87		.
	inc a			;3882	3c		<
	bit 4,e			;3883	cb 63		. c
	jr nz,LP_SET_NO_OR	;3885	20 02		  .
	or 080h			;3887	f6 80		. .
LP_SET_NO_OR:
				; = LP_SET_NO_OR (src: FUNC_LOW_PRINT.ASM:1164, BIOS-TT 0271ac3)
	ld (ix+010h),a		;3889	dd 77 10	. w .
	ld d,a			;388c	57		W
	ld a,(ix+000h)		;388d	dd 7e 00	. ~ .
	add a,a			;3890	87		.
	add a,d			;3891	82		.
	ld (ix+011h),a		;3892	dd 77 11	. w .
	ld (0c15eh),de		;3895	ed 53 5e c1	. S ^ .
	ld a,h			;3899	7c		|
	and 03fh		;389a	e6 3f		. ?
	add a,a			;389c	87		.
	add a,a			;389d	87		.
	ld l,a			;389e	6f		o
	ld h,0c3h		;389f	26 c3		& .
	ld (0c15ch),hl		;38a1	22 5c c1	" \ .
	ld (ix+00eh),a		;38a4	dd 77 0e	. w .
	ld a,(ix+001h)		;38a7	dd 7e 01	. ~ .
	add a,a			;38aa	87		.
	add a,a			;38ab	87		.
	add a,l			;38ac	85		.
	ld (ix+00fh),a		;38ad	dd 77 0f	. w .
	ld l,(ix+00eh)		;38b0	dd 6e 0e	. n .
	ld h,0c3h		;38b3	26 c3		& .
	ld b,(ix+001h)		;38b5	dd 46 01	. F .
	ld (ix+014h),000h	;38b8	dd 36 14 00	. 6 . .
	ld a,(ix+004h)		;38bc	dd 7e 04	. ~ .
	ld (ix+015h),a		;38bf	dd 77 15	. w .
	bit 4,a			;38c2	cb 67		. g
	jr nz,LP_SET_LOOP	;38c4	20 20		   
	and 0f0h		;38c6	e6 f0		. .
	ld c,a			;38c8	4f		O
	ld a,(ix+006h)		;38c9	dd 7e 06	. ~ .
	rrca			;38cc	0f		.
	rrca			;38cd	0f		.
	rrca			;38ce	0f		.
	ld d,a			;38cf	57		W
	and 00fh		;38d0	e6 0f		. .
	or c			;38d2	b1		.
	ld (ix+015h),a		;38d3	dd 77 15	. w .
	ld a,d			;38d6	7a		z
	and 0e0h		;38d7	e6 e0		. .
	ld c,a			;38d9	4f		O
	ld a,(ix+007h)		;38da	dd 7e 07	. ~ .
	and 01fh		;38dd	e6 1f		. .
	or c			;38df	b1		.
	rlca			;38e0	07		.
	rlca			;38e1	07		.
	rlca			;38e2	07		.
	ld (ix+014h),a		;38e3	dd 77 14	. w .
LP_SET_LOOP:
				; = LP_SET_LOOP (src: FUNC_LOW_PRINT.ASM:1221, BIOS-TT 0271ac3)
	ld d,(ix+010h)		;38e6	dd 56 10	. V .
	ld c,(ix+000h)		;38e9	dd 4e 00	. N .
	ld a,(ix+004h)		;38ec	dd 7e 04	. ~ .
	push bc			;38ef	c5		.
	call LP_MODE_LINE	;38f0	cd 15 39	. . 9
	pop bc			;38f3	c1		.
	inc l			;38f4	2c		,
	inc l			;38f5	2c		,
	inc l			;38f6	2c		,
	inc l			;38f7	2c		,
	djnz LP_SET_LOOP	;38f8	10 ec		. .
	ld a,(ix+013h)		;38fa	dd 7e 13	. ~ .
	and 001h		;38fd	e6 01		. .
	out (0c9h),a		;38ff	d3 c9		. .
	ld a,(0c11dh)		;3901	3a 1d c1	: . .
	out (089h),a		;3904	d3 89		. .
	ld a,(0c107h)		;3906	3a 07 c1	: . .
	out (0e2h),a		;3909	d3 e2		. .
	call LP_INI_P		;390b	cd 39 37	. 9 7
	call sub_3612h		;390e	cd 12 36	. . 6
	xor a			;3911	af		.
	ret			;3912	c9		.
FnB1_WIN_CLOSE:
				; = WIN_CLOSE (src: FUNC_LOW_PRINT.ASM:1255, BIOS-TT 0271ac3)
	scf			;3913	37		7
	ret			;3914	c9		.
LP_MODE_LINE:
				; = LP_MODE_LINE (src: FUNC_LOW_PRINT.ASM:1260, BIOS-TT 0271ac3)
	bit 0,(ix+005h)		;3915	dd cb 05 46	. . . F
	jp nz,LP_MODE_LINE2	;3919	c2 69 39	. i 9
	bit 4,a			;391c	cb 67		. g
	jp z,LP_MODE_LINE3	;391e	ca d3 39	. . 9
	dec d			;3921	15		.
	ex af,af'		;3922	08		.
	ld a,050h		;3923	3e 50		> P
	out (0e2h),a		;3925	d3 e2		. .
	ld a,l			;3927	7d		}
	cp 080h			;3928	fe 80		. .
	jr nc,LP_EXIT_MODE	;392a	30 37		0 7
LP_MODE_RECURSE:
				; = LP_MODE_RECURSE (src: FUNC_LOW_PRINT.ASM:1276, BIOS-TT 0271ac3)
	ld a,d			;392c	7a		z
	and 07fh		;392d	e6 7f		. .
	sub 050h		;392f	d6 50		. P
	jr c,LP_MODE_LL		;3931	38 13		8 .
	sub 030h		;3933	d6 30		. 0
	neg			;3935	ed 44		. D
	ld e,a			;3937	5f		_
	add a,d			;3938	82		.
	ld d,a			;3939	57		W
	ld a,e			;393a	7b		{
	rra			;393b	1f		.
	and 03fh		;393c	e6 3f		. ?
	sub c			;393e	91		.
	jr nc,LP_EXIT_MODE	;393f	30 22		0 "
	neg			;3941	ed 44		. D
	ld c,a			;3943	4f		O
	jr LP_MODE_RECURSE	;3944	18 e6		. .
LP_MODE_LL:
				; = LP_MODE_LL (src: FUNC_LOW_PRINT.ASM:1297, BIOS-TT 0271ac3)
	neg			;3946	ed 44		. D
	rra			;3948	1f		.
	and 03fh		;3949	e6 3f		. ?
	cp c			;394b	b9		.
	jr nc,LP_MODE_LR	;394c	30 01		0 .
	ld c,a			;394e	4f		O
LP_MODE_LR:
				; = LP_MODE_LR (src: FUNC_LOW_PRINT.ASM:1304, BIOS-TT 0271ac3)
	ex af,af'		;394f	08		.
	inc d			;3950	14		.
LP_MD_LL1:
				; = LP_MD_LL1 (src: FUNC_LOW_PRINT.ASM:1308, BIOS-TT 0271ac3)
	ex af,af'		;3951	08		.
	ld a,d			;3952	7a		z
	out (089h),a		;3953	d3 89		. .
	ex af,af'		;3955	08		.
	ld (hl),a		;3956	77		w
	inc d			;3957	14		.
	ex af,af'		;3958	08		.
	ld a,d			;3959	7a		z
	out (089h),a		;395a	d3 89		. .
	ex af,af'		;395c	08		.
	ld (hl),a		;395d	77		w
	inc d			;395e	14		.
	dec c			;395f	0d		.
	jr nz,LP_MD_LL1		;3960	20 ef		  .
	ex af,af'		;3962	08		.
LP_EXIT_MODE:
				; = LP_EXIT_MODE (src: FUNC_LOW_PRINT.ASM:1325, BIOS-TT 0271ac3)
	ld a,0feh		;3963	3e fe		> .
	out (0e2h),a		;3965	d3 e2		. .
	ex af,af'		;3967	08		.
	ret			;3968	c9		.
LP_MODE_LINE2:
				; = LP_MODE_LINE2 (src: FUNC_LOW_PRINT.ASM:1333, BIOS-TT 0271ac3)
	ld a,(ix+015h)		;3969	dd 7e 15	. ~ .
	ld b,(ix+014h)		;396c	dd 46 14	. F .
	dec d			;396f	15		.
	ex af,af'		;3970	08		.
	ld a,050h		;3971	3e 50		> P
	out (0e2h),a		;3973	d3 e2		. .
	ld a,l			;3975	7d		}
	cp 080h			;3976	fe 80		. .
	jr nc,LP_EXIT_MODE2	;3978	30 4d		0 M
LP_MODE_RECURSE2:
				; = LP_MODE_RECURSE2 (src: FUNC_LOW_PRINT.ASM:1349, BIOS-TT 0271ac3)
	ld a,d			;397a	7a		z
	and 07fh		;397b	e6 7f		. .
	sub 050h		;397d	d6 50		. P
	jr c,LP_MODE_LL2	;397f	38 13		8 .
	sub 030h		;3981	d6 30		. 0
	neg			;3983	ed 44		. D
	ld e,a			;3985	5f		_
	add a,d			;3986	82		.
	ld d,a			;3987	57		W
	ld a,e			;3988	7b		{
	rra			;3989	1f		.
	and 03fh		;398a	e6 3f		. ?
	sub c			;398c	91		.
	jr nc,LP_EXIT_MODE2	;398d	30 38		0 8
	neg			;398f	ed 44		. D
	ld c,a			;3991	4f		O
	jr LP_MODE_RECURSE2	;3992	18 e6		. .
LP_MODE_LL2:
				; = LP_MODE_LL2 (src: FUNC_LOW_PRINT.ASM:1370, BIOS-TT 0271ac3)
	neg			;3994	ed 44		. D
	rra			;3996	1f		.
	and 03fh		;3997	e6 3f		. ?
	cp c			;3999	b9		.
	jr nc,LP_MODE_LR2	;399a	30 01		0 .
	ld c,a			;399c	4f		O
LP_MODE_LR2:
				; = LP_MODE_LR2 (src: FUNC_LOW_PRINT.ASM:1377, BIOS-TT 0271ac3)
	ex af,af'		;399d	08		.
	inc d			;399e	14		.
LP_MD_LL2:
				; = LP_MD_LL2 (src: FUNC_LOW_PRINT.ASM:1381, BIOS-TT 0271ac3)
	ex af,af'		;399f	08		.
	ld a,d			;39a0	7a		z
	out (089h),a		;39a1	d3 89		. .
	ex af,af'		;39a3	08		.
	ld (hl),a		;39a4	77		w
	inc l			;39a5	2c		,
	ld (hl),b		;39a6	70		p
	inc l			;39a7	2c		,
	ld (hl),b		;39a8	70		p
	dec l			;39a9	2d		-
	dec l			;39aa	2d		-
	inc d			;39ab	14		.
	bit 4,a			;39ac	cb 67		. g
	jr nz,lp_md_ll3x	;39ae	20 01		  .
	inc b			;39b0	04		.
lp_md_ll3x:
				; = lp_md_ll3x (src: FUNC_LOW_PRINT.ASM:1398, BIOS-TT 0271ac3)
	ex af,af'		;39b1	08		.
	ld a,d			;39b2	7a		z
	out (089h),a		;39b3	d3 89		. .
	ex af,af'		;39b5	08		.
	ld (hl),a		;39b6	77		w
	inc l			;39b7	2c		,
	ld (hl),b		;39b8	70		p
	inc l			;39b9	2c		,
	ld (hl),b		;39ba	70		p
	dec l			;39bb	2d		-
	dec l			;39bc	2d		-
	inc d			;39bd	14		.
	inc b			;39be	04		.
	jr nz,LP_NO_ADD_40	;39bf	20 02		  .
	add a,040h		;39c1	c6 40		. @
LP_NO_ADD_40:
				; = LP_NO_ADD_40 (src: FUNC_LOW_PRINT.ASM:1416, BIOS-TT 0271ac3)
	dec c			;39c3	0d		.
	jr nz,LP_MD_LL2		;39c4	20 d9		  .
	ex af,af'		;39c6	08		.
LP_EXIT_MODE2:
				; = LP_EXIT_MODE2 (src: FUNC_LOW_PRINT.ASM:1422, BIOS-TT 0271ac3)
	ld a,0feh		;39c7	3e fe		> .
	out (0e2h),a		;39c9	d3 e2		. .
	ex af,af'		;39cb	08		.
	ld (ix+014h),b		;39cc	dd 70 14	. p .
	ld (ix+015h),a		;39cf	dd 77 15	. w .
	ret			;39d2	c9		.
LP_MODE_LINE3:
				; = LP_MODE_LINE3 (src: FUNC_LOW_PRINT.ASM:1433, BIOS-TT 0271ac3)
	ld a,(ix+014h)		;39d3	dd 7e 14	. ~ .
	ld b,(ix+015h)		;39d6	dd 46 15	. F .
	dec d			;39d9	15		.
	ex af,af'		;39da	08		.
	ld a,050h		;39db	3e 50		> P
	out (0e2h),a		;39dd	d3 e2		. .
	ld a,l			;39df	7d		}
	cp 080h			;39e0	fe 80		. .
	jr nc,LP_EXIT_MODE3	;39e2	30 48		0 H
LP_MODE_RECURSE3:
				; = LP_MODE_RECURSE3 (src: FUNC_LOW_PRINT.ASM:1449, BIOS-TT 0271ac3)
	ld a,d			;39e4	7a		z
	and 07fh		;39e5	e6 7f		. .
	sub 050h		;39e7	d6 50		. P
	jr c,LP_MODE_LL3	;39e9	38 13		8 .
	sub 030h		;39eb	d6 30		. 0
	neg			;39ed	ed 44		. D
	ld e,a			;39ef	5f		_
	add a,d			;39f0	82		.
	ld d,a			;39f1	57		W
	ld a,e			;39f2	7b		{
	rra			;39f3	1f		.
	and 03fh		;39f4	e6 3f		. ?
	sub c			;39f6	91		.
	jr nc,LP_EXIT_MODE3	;39f7	30 33		0 3
	neg			;39f9	ed 44		. D
	ld c,a			;39fb	4f		O
	jr LP_MODE_RECURSE3	;39fc	18 e6		. .
LP_MODE_LL3:
				; = LP_MODE_LL3 (src: FUNC_LOW_PRINT.ASM:1470, BIOS-TT 0271ac3)
	neg			;39fe	ed 44		. D
	rra			;3a00	1f		.
	and 03fh		;3a01	e6 3f		. ?
	cp c			;3a03	b9		.
	jr nc,LP_MODE_LR3	;3a04	30 01		0 .
	ld c,a			;3a06	4f		O
LP_MODE_LR3:
				; = LP_MODE_LR3 (src: FUNC_LOW_PRINT.ASM:1477, BIOS-TT 0271ac3)
	ex af,af'		;3a07	08		.
	inc d			;3a08	14		.
	bit 5,b			;3a09	cb 68		. h
	jr z,LP_GR_640		;3a0b	28 2d		( -
LP_MD_LL3:
				; = LP_MD_LL3 (src: FUNC_LOW_PRINT.ASM:1485, BIOS-TT 0271ac3)
	ex af,af'		;3a0d	08		.
	ld a,d			;3a0e	7a		z
	out (089h),a		;3a0f	d3 89		. .
	ex af,af'		;3a11	08		.
	ld (hl),b		;3a12	70		p
	inc l			;3a13	2c		,
	ld (hl),a		;3a14	77		w
	inc l			;3a15	2c		,
	ld (hl),000h		;3a16	36 00		6 .
	dec l			;3a18	2d		-
	dec l			;3a19	2d		-
	inc d			;3a1a	14		.
	inc d			;3a1b	14		.
	inc a			;3a1c	3c		<
	ld e,a			;3a1d	5f		_
	and 007h		;3a1e	e6 07		. .
	jr nz,LP_NO_INC_B	;3a20	20 05		  .
	ld a,e			;3a22	7b		{
	sub 008h		;3a23	d6 08		. .
	ld e,a			;3a25	5f		_
	inc b			;3a26	04		.
LP_NO_INC_B:
				; = LP_NO_INC_B (src: FUNC_LOW_PRINT.ASM:1520, BIOS-TT 0271ac3)
	ld a,e			;3a27	7b		{
	dec c			;3a28	0d		.
	jr nz,LP_MD_LL3		;3a29	20 e2		  .
LP_640_RET:
				; = LP_640_RET (src: FUNC_LOW_PRINT.ASM:1526, BIOS-TT 0271ac3)
	ex af,af'		;3a2b	08		.
LP_EXIT_MODE3:
				; = LP_EXIT_MODE3 (src: FUNC_LOW_PRINT.ASM:1528, BIOS-TT 0271ac3)
	ld a,0feh		;3a2c	3e fe		> .
	out (0e2h),a		;3a2e	d3 e2		. .
	ex af,af'		;3a30	08		.
	ld a,(ix+014h)		;3a31	dd 7e 14	. ~ .
	add a,008h		;3a34	c6 08		. .
	ld (ix+014h),a		;3a36	dd 77 14	. w .
	ret			;3a39	c9		.
LP_GR_640:
				; = LP_GR_640 (src: FUNC_LOW_PRINT.ASM:1539, BIOS-TT 0271ac3)
	ex af,af'		;3a3a	08		.
	ld a,d			;3a3b	7a		z
	out (089h),a		;3a3c	d3 89		. .
	ex af,af'		;3a3e	08		.
	ld (hl),b		;3a3f	70		p
	inc l			;3a40	2c		,
	ld (hl),a		;3a41	77		w
	inc l			;3a42	2c		,
	ld (hl),000h		;3a43	36 00		6 .
	inc d			;3a45	14		.
	ex af,af'		;3a46	08		.
	ld a,d			;3a47	7a		z
	out (089h),a		;3a48	d3 89		. .
	ex af,af'		;3a4a	08		.
	ld (hl),000h		;3a4b	36 00		6 .
	dec l			;3a4d	2d		-
	ld (hl),a		;3a4e	77		w
	dec l			;3a4f	2d		-
	ld (hl),b		;3a50	70		p
	inc d			;3a51	14		.
	inc a			;3a52	3c		<
	ld e,a			;3a53	5f		_
	and 007h		;3a54	e6 07		. .
	jr nz,LP_NO_INC_B6	;3a56	20 05		  .
	ld a,e			;3a58	7b		{
	sub 008h		;3a59	d6 08		. .
	ld e,a			;3a5b	5f		_
	inc b			;3a5c	04		.
LP_NO_INC_B6:
				; = LP_NO_INC_B6 (src: FUNC_LOW_PRINT.ASM:1573, BIOS-TT 0271ac3)
	ld a,e			;3a5d	7b		{
	dec c			;3a5e	0d		.
	jr nz,LP_GR_640		;3a5f	20 d9		  .
	jp LP_640_RET		;3a61	c3 2b 3a	. + :
Fn8A_LP_SCROLL_UD:
				; = LP_SCROLL_UD (src: FUNC_LOW_PRINT.ASM:1584, BIOS-TT 0271ac3)
	dec b			;3a64	05		.
	jr z,LP_SCROLL_UP	;3a65	28 05		( .
	dec b			;3a67	05		.
	jr z,LP_SCROLL_DN	;3a68	28 66		( f
	scf			;3a6a	37		7
	ret			;3a6b	c9		.
LP_SCROLL_UP:
				; = LP_SCROLL_UP (src: FUNC_LOW_PRINT.ASM:1599, BIOS-TT 0271ac3)
	dec e			;3a6c	1d		.
	ret z			;3a6d	c8		.
	in a,(0e2h)		;3a6e	db e2		. .
	ld c,a			;3a70	4f		O
	ld a,0feh		;3a71	3e fe		> .
	out (0e2h),a		;3a73	d3 e2		. .
	ld a,c			;3a75	79		y
	ld (0c107h),a		;3a76	32 07 c1	2 . .
	in a,(089h)		;3a79	db 89		. .
	ld (0c11dh),a		;3a7b	32 1d c1	2 . .
	ld b,e			;3a7e	43		C
	ld e,000h		;3a7f	1e 00		. .
	push bc			;3a81	c5		.
	call LP_AT_D		;3a82	cd 3a 36	. : 6
	call LP_TAB_E		;3a85	cd 53 36	. S 6
	pop bc			;3a88	c1		.
LP_SCROLL_L2:
				; = LP_SCROLL_L2 (src: FUNC_LOW_PRINT.ASM:1611, BIOS-TT 0271ac3)
	exx			;3a89	d9		.
	push hl			;3a8a	e5		.
	exx			;3a8b	d9		.
	pop hl			;3a8c	e1		.
	ld e,l			;3a8d	5d		]
	ld d,h			;3a8e	54		T
	inc l			;3a8f	2c		,
	inc l			;3a90	2c		,
	inc l			;3a91	2c		,
	inc l			;3a92	2c		,
	ld a,(0e00fh)		;3a93	3a 0f e0	: . .
	cp l			;3a96	bd		.
	jr c,l3ac5h		;3a97	38 2c		8 ,
	push bc			;3a99	c5		.
	ld a,l			;3a9a	7d		}
	exx			;3a9b	d9		.
	ld l,a			;3a9c	6f		o
	exx			;3a9d	d9		.
	ld a,(0e000h)		;3a9e	3a 00 e0	: . .
	add a,a			;3aa1	87		.
	add a,a			;3aa2	87		.
	ld c,a			;3aa3	4f		O
	ld b,000h		;3aa4	06 00		. .
	ld a,050h		;3aa6	3e 50		> P
	out (0e2h),a		;3aa8	d3 e2		. .
	exx			;3aaa	d9		.
	ld a,d			;3aab	7a		z
	exx			;3aac	d9		.
LP_SCROLL_L1:
				; = LP_SCROLL_L1 (src: FUNC_LOW_PRINT.ASM:1650, BIOS-TT 0271ac3)
	out (089h),a		;3aad	d3 89		. .
	inc a			;3aaf	3c		<
	ldi			;3ab0	ed a0		. .
	ldd			;3ab2	ed a8		. .
	out (089h),a		;3ab4	d3 89		. .
	inc a			;3ab6	3c		<
	ldi			;3ab7	ed a0		. .
	ldd			;3ab9	ed a8		. .
	jp pe,LP_SCROLL_L1	;3abb	ea ad 3a	. . :
	ld a,0feh		;3abe	3e fe		> .
	out (0e2h),a		;3ac0	d3 e2		. .
	pop bc			;3ac2	c1		.
	djnz LP_SCROLL_L2	;3ac3	10 c4		. .
l3ac5h:
	ld a,(0c11dh)		;3ac5	3a 1d c1	: . .
	out (089h),a		;3ac8	d3 89		. .
	ld a,(0c107h)		;3aca	3a 07 c1	: . .
	out (0e2h),a		;3acd	d3 e2		. .
	ret			;3acf	c9		.
LP_SCROLL_DN:
				; = LP_SCROLL_DN (src: FUNC_LOW_PRINT.ASM:1676, BIOS-TT 0271ac3)
	dec e			;3ad0	1d		.
	ret z			;3ad1	c8		.
	in a,(0e2h)		;3ad2	db e2		. .
	ld c,a			;3ad4	4f		O
	ld a,0feh		;3ad5	3e fe		> .
	out (0e2h),a		;3ad7	d3 e2		. .
	ld a,c			;3ad9	79		y
	ld (0c107h),a		;3ada	32 07 c1	2 . .
	in a,(089h)		;3add	db 89		. .
	ld (0c11dh),a		;3adf	32 1d c1	2 . .
	ld b,e			;3ae2	43		C
	ld e,000h		;3ae3	1e 00		. .
	ld a,d			;3ae5	7a		z
	add a,b			;3ae6	80		.
	ld d,a			;3ae7	57		W
	push bc			;3ae8	c5		.
	call LP_AT_D		;3ae9	cd 3a 36	. : 6
	call LP_TAB_E		;3aec	cd 53 36	. S 6
	pop bc			;3aef	c1		.
LP_SCROLL_D2:
				; = LP_SCROLL_D2 (src: FUNC_LOW_PRINT.ASM:1693, BIOS-TT 0271ac3)
	exx			;3af0	d9		.
	push hl			;3af1	e5		.
	exx			;3af2	d9		.
	pop hl			;3af3	e1		.
	ld e,l			;3af4	5d		]
	ld d,h			;3af5	54		T
	dec l			;3af6	2d		-
	dec l			;3af7	2d		-
	dec l			;3af8	2d		-
	dec l			;3af9	2d		-
	ld a,(0e00eh)		;3afa	3a 0e e0	: . .
	cp l			;3afd	bd		.
	jr z,LP_SCROLL_U_CONT	;3afe	28 02		( .
	jr nc,l3ac5h		;3b00	30 c3		0 .
LP_SCROLL_U_CONT:
				; = LP_SCROLL_U_CONT (src: FUNC_LOW_PRINT.ASM:1713, BIOS-TT 0271ac3)
	push bc			;3b02	c5		.
	ld a,l			;3b03	7d		}
	exx			;3b04	d9		.
	ld l,a			;3b05	6f		o
	exx			;3b06	d9		.
	ld a,(0e000h)		;3b07	3a 00 e0	: . .
	add a,a			;3b0a	87		.
	add a,a			;3b0b	87		.
	ld c,a			;3b0c	4f		O
	ld b,000h		;3b0d	06 00		. .
	ld a,050h		;3b0f	3e 50		> P
	out (0e2h),a		;3b11	d3 e2		. .
	exx			;3b13	d9		.
	ld a,d			;3b14	7a		z
	exx			;3b15	d9		.
LP_SCROLL_D1:
				; = LP_SCROLL_D1 (src: FUNC_LOW_PRINT.ASM:1735, BIOS-TT 0271ac3)
	out (089h),a		;3b16	d3 89		. .
	inc a			;3b18	3c		<
	ldi			;3b19	ed a0		. .
	ldd			;3b1b	ed a8		. .
	out (089h),a		;3b1d	d3 89		. .
	inc a			;3b1f	3c		<
	ldi			;3b20	ed a0		. .
	ldd			;3b22	ed a8		. .
	jp pe,LP_SCROLL_D1	;3b24	ea 16 3b	. . ;
	ld a,0feh		;3b27	3e fe		> .
	out (0e2h),a		;3b29	d3 e2		. .
	pop bc			;3b2b	c1		.
	djnz LP_SCROLL_D2	;3b2c	10 c2		. .
	ld a,(0c11dh)		;3b2e	3a 1d c1	: . .
	out (089h),a		;3b31	d3 89		. .
	ld a,(0c107h)		;3b33	3a 07 c1	: . .
	out (0e2h),a		;3b36	d3 e2		. .
	ret			;3b38	c9		.
FnB4_WIN_GET_SYM:
				; = WIN_GET_SYM (src: FUNC_LOW_PRINT.ASM:1761, BIOS-TT 0271ac3)
	and a			;3b39	a7		.
	scf			;3b3a	37		7
	ret nz			;3b3b	c0		.
	call LP_BEG_P		;3b3c	cd 9a 36	. . 6
	call LP_AT_D		;3b3f	cd 3a 36	. : 6
	call LP_TAB_E		;3b42	cd 53 36	. S 6
	ld a,050h		;3b45	3e 50		> P
	out (0e2h),a		;3b47	d3 e2		. .
	exx			;3b49	d9		.
	ld a,d			;3b4a	7a		z
	out (089h),a		;3b4b	d3 89		. .
	ld a,(hl)		;3b4d	7e		~
	exx			;3b4e	d9		.
	ld l,a			;3b4f	6f		o
	exx			;3b50	d9		.
	inc l			;3b51	2c		,
	ld a,(hl)		;3b52	7e		~
	exx			;3b53	d9		.
	ld h,a			;3b54	67		g
	exx			;3b55	d9		.
	dec l			;3b56	2d		-
	dec l			;3b57	2d		-
	ld a,(hl)		;3b58	7e		~
	exx			;3b59	d9		.
	ld b,a			;3b5a	47		G
	exx			;3b5b	d9		.
	inc l			;3b5c	2c		,
	exx			;3b5d	d9		.
	ld a,0feh		;3b5e	3e fe		> .
	out (0e2h),a		;3b60	d3 e2		. .
	call LP_END_P		;3b62	cd 89 37	. . 7
	ret			;3b65	c9		.
FnB5_WIN_PUT_SYM:
				; = WIN_PUT_SYM (src: FUNC_LOW_PRINT.ASM:1807, BIOS-TT 0271ac3)
	and a			;3b66	a7		.
	scf			;3b67	37		7
	ret nz			;3b68	c0		.
	call LP_BEG_P		;3b69	cd 9a 36	. . 6
	call LP_AT_D		;3b6c	cd 3a 36	. : 6
	call LP_TAB_E		;3b6f	cd 53 36	. S 6
	ld a,050h		;3b72	3e 50		> P
	out (0e2h),a		;3b74	d3 e2		. .
	exx			;3b76	d9		.
	ld a,d			;3b77	7a		z
	out (089h),a		;3b78	d3 89		. .
	exx			;3b7a	d9		.
	ld a,l			;3b7b	7d		}
	exx			;3b7c	d9		.
	ld (hl),a		;3b7d	77		w
	inc l			;3b7e	2c		,
	exx			;3b7f	d9		.
	ld a,h			;3b80	7c		|
	exx			;3b81	d9		.
	ld (hl),a		;3b82	77		w
	dec l			;3b83	2d		-
	dec l			;3b84	2d		-
	exx			;3b85	d9		.
	ld a,b			;3b86	78		x
	exx			;3b87	d9		.
	ld (hl),a		;3b88	77		w
	inc l			;3b89	2c		,
	exx			;3b8a	d9		.
	ld a,0feh		;3b8b	3e fe		> .
	out (0e2h),a		;3b8d	d3 e2		. .
	call LP_END_P		;3b8f	cd 89 37	. . 7
	ret			;3b92	c9		.
FnB7_WIN_MOVE:
				; = WIN_MOVE (src: FUNC_LOW_PRINT.ASM:1859, BIOS-TT 0271ac3)
	and a			;3b93	a7		.
	scf			;3b94	37		7
	ret nz			;3b95	c0		.
	push ix			;3b96	dd e5		. .
	push hl			;3b98	e5		.
	ld b,0ffh		;3b99	06 ff		. .
	ld ix,0c000h		;3b9b	dd 21 00 c0	. ! . .
	call WIN_COPY_WIN1	;3b9f	cd b2 3b	. . ;
	pop hl			;3ba2	e1		.
	pop de			;3ba3	d1		.
	ld b,0ffh		;3ba4	06 ff		. .
	ld ix,0c000h		;3ba6	dd 21 00 c0	. ! . .
	call WIN_REST_WIN1	;3baa	cd 03 3c	. . <
	and a			;3bad	a7		.
	ret			;3bae	c9		.
FnB2_WIN_COPY:
				; = WIN_COPY (src: FUNC_LOW_PRINT.ASM:1881, BIOS-TT 0271ac3)
	and a			;3baf	a7		.
	scf			;3bb0	37		7
	ret nz			;3bb1	c0		.
WIN_COPY_WIN1:
				; = WIN_COPY_WIN1 (src: FUNC_LOW_PRINT.ASM:1885, BIOS-TT 0271ac3)
	call LP_BEG_P		;3bb2	cd 9a 36	. . 6
	in a,(0a2h)		;3bb5	db a2		. .
	push af			;3bb7	f5		.
	call LP_AT_D		;3bb8	cd 3a 36	. : 6
	call LP_TAB_E		;3bbb	cd 53 36	. S 6
	ld (0c150h),sp		;3bbe	ed 73 50 c1	. s P .
	ld a,b			;3bc2	78		x
	out (0e2h),a		;3bc3	d3 e2		. .
	ld a,050h		;3bc5	3e 50		> P
	out (0a2h),a		;3bc7	d3 a2		. .
	exx			;3bc9	d9		.
	res 7,h			;3bca	cb bc		. .
	ld c,d			;3bcc	4a		J
	exx			;3bcd	d9		.
LP_COPY_L2:
				; = LP_COPY_L2 (src: FUNC_LOW_PRINT.ASM:1906, BIOS-TT 0271ac3)
	ld a,l			;3bce	7d		}
	exx			;3bcf	d9		.
	ld b,a			;3bd0	47		G
	add a,a			;3bd1	87		.
	defb 0ddh,085h ;add a,ixl	;3bd2	dd 85		. .
	defb 0ddh,06fh ;ld ixl,a	;3bd4	dd 6f		. o
	jr nc,LP_NO_INC_IX1	;3bd6	30 02		0 .
	defb 0ddh,024h ;inc ixh	;3bd8	dd 24		. $
LP_NO_INC_IX1:
				; = LP_NO_INC_IX1 (src: FUNC_LOW_PRINT.ASM:1916, BIOS-TT 0271ac3)
	ld sp,ix		;3bda	dd f9		. .
	ld a,c			;3bdc	79		y
	add a,b			;3bdd	80		.
LP_COPY_L1:
				; = LP_COPY_L1 (src: FUNC_LOW_PRINT.ASM:1923, BIOS-TT 0271ac3)
	dec a			;3bde	3d		=
	out (089h),a		;3bdf	d3 89		. .
	ld e,(hl)		;3be1	5e		^
	inc l			;3be2	2c		,
	ld d,(hl)		;3be3	56		V
	dec l			;3be4	2d		-
	push de			;3be5	d5		.
	djnz LP_COPY_L1		;3be6	10 f6		. .
	inc hl			;3be8	23		#
	inc hl			;3be9	23		#
	inc hl			;3bea	23		#
	inc hl			;3beb	23		#
	exx			;3bec	d9		.
	dec h			;3bed	25		%
	jr nz,LP_COPY_L2	;3bee	20 de		  .
	ld a,0feh		;3bf0	3e fe		> .
	out (0e2h),a		;3bf2	d3 e2		. .
	ld sp,(0c150h)		;3bf4	ed 7b 50 c1	. { P .
	pop af			;3bf8	f1		.
	out (0a2h),a		;3bf9	d3 a2		. .
	call LP_END_P2		;3bfb	cd a6 37	. . 7
	and a			;3bfe	a7		.
	ret			;3bff	c9		.
FnB3_WIN_RESTORE:
				; = WIN_RESTORE (src: FUNC_LOW_PRINT.ASM:1958, BIOS-TT 0271ac3)
	and a			;3c00	a7		.
	scf			;3c01	37		7
	ret nz			;3c02	c0		.
WIN_REST_WIN1:
				; = WIN_REST_WIN1 (src: FUNC_LOW_PRINT.ASM:1962, BIOS-TT 0271ac3)
	call LP_BEG_P		;3c03	cd 9a 36	. . 6
	in a,(0a2h)		;3c06	db a2		. .
	push af			;3c08	f5		.
	call LP_AT_D		;3c09	cd 3a 36	. : 6
	call LP_TAB_E		;3c0c	cd 53 36	. S 6
	ld (0c150h),sp		;3c0f	ed 73 50 c1	. s P .
	ld sp,ix		;3c13	dd f9		. .
	ld a,b			;3c15	78		x
	out (0e2h),a		;3c16	d3 e2		. .
	ld a,050h		;3c18	3e 50		> P
	out (0a2h),a		;3c1a	d3 a2		. .
	exx			;3c1c	d9		.
	res 7,h			;3c1d	cb bc		. .
	ld c,d			;3c1f	4a		J
	exx			;3c20	d9		.
LP_REST_L2:
				; = LP_REST_L2 (src: FUNC_LOW_PRINT.ASM:1984, BIOS-TT 0271ac3)
	ld a,l			;3c21	7d		}
	exx			;3c22	d9		.
	ld b,a			;3c23	47		G
	ld a,c			;3c24	79		y
LP_REST_L1:
				; = LP_REST_L1 (src: FUNC_LOW_PRINT.ASM:1992, BIOS-TT 0271ac3)
	out (089h),a		;3c25	d3 89		. .
	inc a			;3c27	3c		<
	pop de			;3c28	d1		.
	ld (hl),e		;3c29	73		s
	inc l			;3c2a	2c		,
	ld (hl),d		;3c2b	72		r
	dec l			;3c2c	2d		-
	djnz LP_REST_L1		;3c2d	10 f6		. .
	inc hl			;3c2f	23		#
	inc hl			;3c30	23		#
	inc hl			;3c31	23		#
	inc hl			;3c32	23		#
	exx			;3c33	d9		.
	dec h			;3c34	25		%
	jr nz,LP_REST_L2	;3c35	20 ea		  .
	ld a,0feh		;3c37	3e fe		> .
	out (0e2h),a		;3c39	d3 e2		. .
	ld sp,(0c150h)		;3c3b	ed 7b 50 c1	. { P .
	pop af			;3c3f	f1		.
	out (0a2h),a		;3c40	d3 a2		. .
	call LP_END_P2		;3c42	cd a6 37	. . 7
	and a			;3c45	a7		.
	ret			;3c46	c9		.
FnE8_FN_SEND_BYTE:
				; = ~FN_SEND_BYTE (src: FUNC_SYS.ASM:301, BIOS-TT 0271ac3)
	ld e,a			;3c47	5f		_
	call SEND_HALF_BYTE	;3c48	cd 52 3c	. R <
	ret c			;3c4b	d8		.
	ld a,e			;3c4c	7b		{
	rrca			;3c4d	0f		.
	rrca			;3c4e	0f		.
	rrca			;3c4f	0f		.
	rrca			;3c50	0f		.
	ld e,a			;3c51	5f		_
SEND_HALF_BYTE:
				; = ~SEND_HALF_BYTE (src: FUNC_SYS.ASM:311, BIOS-TT 0271ac3)
	ld a,e			;3c52	7b		{
	or 0f0h			;3c53	f6 f0		. .
	out (01ch),a		;3c55	d3 1c		. .
	ld bc,Reset		;3c57	01 00 00	. . .
WAIT_SENT_1:
				; = ~WAIT_SENT_1 (src: FUNC_SYS.ASM:316, BIOS-TT 0271ac3)
	in a,(0ffh)		;3c5a	db ff		. .
	bit 4,a			;3c5c	cb 67		. g
	jr nz,CONTINUE_SENT	;3c5e	20 08		  .
	dec bc			;3c60	0b		.
	ld a,b			;3c61	78		x
	or c			;3c62	b1		.
	jr nz,WAIT_SENT_1	;3c63	20 f5		  .
	xor a			;3c65	af		.
	scf			;3c66	37		7
	ret			;3c67	c9		.
CONTINUE_SENT:
				; = ~CONTINUE_SENT (src: FUNC_SYS.ASM:327, BIOS-TT 0271ac3)
	ld a,e			;3c68	7b		{
	and 00fh		;3c69	e6 0f		. .
	out (01ch),a		;3c6b	d3 1c		. .
	ld bc,Reset		;3c6d	01 00 00	. . .
WAIT_SENT_2:
				; = ~WAIT_SENT_2 (src: FUNC_SYS.ASM:334, BIOS-TT 0271ac3)
	in a,(0ffh)		;3c70	db ff		. .
	bit 4,a			;3c72	cb 67		. g
	jr z,CONTINUE_SENT2	;3c74	28 0d		( .
	dec bc			;3c76	0b		.
	ld a,b			;3c77	78		x
	or c			;3c78	b1		.
	jr nz,WAIT_SENT_2	;3c79	20 f5		  .
	ld a,e			;3c7b	7b		{
	or 0f0h			;3c7c	f6 f0		. .
	out (01ch),a		;3c7e	d3 1c		. .
	xor a			;3c80	af		.
	scf			;3c81	37		7
	ret			;3c82	c9		.
CONTINUE_SENT2:
				; = ~CONTINUE_SENT2 (src: FUNC_SYS.ASM:348, BIOS-TT 0271ac3)
	ld a,e			;3c83	7b		{
	or 0f0h			;3c84	f6 f0		. .
	out (01ch),a		;3c86	d3 1c		. .
	xor a			;3c88	af		.
	ret			;3c89	c9		.
FnE9_FN_RESEIVE_B:
				; = ~FN_RESEIVE_B (src: FUNC_SYS.ASM:357, BIOS-TT 0271ac3)
	call RESEIVE_POLU_BYTE	;3c8a	cd 9d 3c	. . <
	ret c			;3c8d	d8		.
	rlca			;3c8e	07		.
	rlca			;3c8f	07		.
	rlca			;3c90	07		.
	rlca			;3c91	07		.
	and 0f0h		;3c92	e6 f0		. .
	ld e,a			;3c94	5f		_
	call RESEIVE_POLU_BYTE	;3c95	cd 9d 3c	. . <
	ret c			;3c98	d8		.
	and 00fh		;3c99	e6 0f		. .
	or e			;3c9b	b3		.
	ret			;3c9c	c9		.
RESEIVE_POLU_BYTE:
				; = ~RESEIVE_POLU_BYTE (src: FUNC_SYS.ASM:371, BIOS-TT 0271ac3)
	ld a,0f0h		;3c9d	3e f0		> .
	out (01ch),a		;3c9f	d3 1c		. .
	ld bc,Reset		;3ca1	01 00 00	. . .
WAIT_RES_1:
				; = ~WAIT_RES_1 (src: FUNC_SYS.ASM:375, BIOS-TT 0271ac3)
	in a,(0ffh)		;3ca4	db ff		. .
	bit 4,a			;3ca6	cb 67		. g
	jr nz,CONTINUE_RES	;3ca8	20 08		  .
	dec bc			;3caa	0b		.
	ld a,b			;3cab	78		x
	or c			;3cac	b1		.
	jr nz,WAIT_RES_1	;3cad	20 f5		  .
	xor a			;3caf	af		.
	scf			;3cb0	37		7
	ret			;3cb1	c9		.
CONTINUE_RES:
				; = ~CONTINUE_RES (src: FUNC_SYS.ASM:386, BIOS-TT 0271ac3)
	xor a			;3cb2	af		.
	out (01ch),a		;3cb3	d3 1c		. .
	ld bc,Reset		;3cb5	01 00 00	. . .
WAIT_RES_2:
				; = ~WAIT_RES_2 (src: FUNC_SYS.ASM:392, BIOS-TT 0271ac3)
	in a,(0ffh)		;3cb8	db ff		. .
	bit 4,a			;3cba	cb 67		. g
	jr z,CONTINUE_RES2	;3cbc	28 0b		( .
	dec bc			;3cbe	0b		.
	ld a,b			;3cbf	78		x
	or c			;3cc0	b1		.
	jr nz,WAIT_RES_2	;3cc1	20 f5		  .
	ld a,0f0h		;3cc3	3e f0		> .
	out (01ch),a		;3cc5	d3 1c		. .
	scf			;3cc7	37		7
	ret			;3cc8	c9		.
CONTINUE_RES2:
				; = ~CONTINUE_RES2 (src: FUNC_SYS.ASM:404, BIOS-TT 0271ac3)
				; = ~LOOP_EQ (src: FUNC_SYS.ASM:405, BIOS-TT 0271ac3)
	and 00fh		;3cc9	e6 0f		. .
	ld b,a			;3ccb	47		G
	in a,(0ffh)		;3ccc	db ff		. .
	and 00fh		;3cce	e6 0f		. .
	cp b			;3cd0	b8		.
	jr nz,CONTINUE_RES2	;3cd1	20 f6		  .
	or 0f0h			;3cd3	f6 f0		. .
	out (01ch),a		;3cd5	d3 1c		. .
	ret			;3cd7	c9		.

; BLOCK 'Fill3CD8' (start 0x3cd8 end 0x3d00)
Fill3CD8:
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
DOS_ON:
				; = DOS_ON (src: EXP.asm:1632, BIOS-TT 0271ac3)
	nop			;3d00	00		.
	ret			;3d01	c9		.

; BLOCK 'Fill3D02' (start 0x3d02 end 0x3d13)
Fill3D02:
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
ToBios3D13:
	nop			;3d13	00		.
	jp Rst18		;3d14	c3 18 00	. . .

; BLOCK 'Fill3D17' (start 0x3d17 end 0x3d29)
Fill3D17:
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
l3d29h:
	rst 38h			;3d29	ff		.
	rst 38h			;3d2a	ff		.
	rst 38h			;3d2b	ff		.
	rst 38h			;3d2c	ff		.
	rst 38h			;3d2d	ff		.
	rst 38h			;3d2e	ff		.
	rst 38h			;3d2f	ff		.
	rst 38h			;3d30	ff		.
	rst 38h			;3d31	ff		.
	rst 38h			;3d32	ff		.
	rst 38h			;3d33	ff		.
	rst 38h			;3d34	ff		.
	rst 38h			;3d35	ff		.
	rst 38h			;3d36	ff		.
	rst 38h			;3d37	ff		.
	rst 38h			;3d38	ff		.
	rst 38h			;3d39	ff		.
	rst 38h			;3d3a	ff		.
	rst 38h			;3d3b	ff		.
	rst 38h			;3d3c	ff		.
	rst 38h			;3d3d	ff		.
	rst 38h			;3d3e	ff		.
	rst 38h			;3d3f	ff		.
	rst 38h			;3d40	ff		.
	rst 38h			;3d41	ff		.
	rst 38h			;3d42	ff		.
	rst 38h			;3d43	ff		.
	rst 38h			;3d44	ff		.
	rst 38h			;3d45	ff		.
	rst 38h			;3d46	ff		.
	rst 38h			;3d47	ff		.
	rst 38h			;3d48	ff		.
	rst 38h			;3d49	ff		.
	rst 38h			;3d4a	ff		.
	rst 38h			;3d4b	ff		.
	rst 38h			;3d4c	ff		.
	rst 38h			;3d4d	ff		.
	rst 38h			;3d4e	ff		.
	rst 38h			;3d4f	ff		.
	rst 38h			;3d50	ff		.
	rst 38h			;3d51	ff		.
	rst 38h			;3d52	ff		.
	rst 38h			;3d53	ff		.
	rst 38h			;3d54	ff		.
	rst 38h			;3d55	ff		.
	rst 38h			;3d56	ff		.
	rst 38h			;3d57	ff		.
	rst 38h			;3d58	ff		.
	rst 38h			;3d59	ff		.
	rst 38h			;3d5a	ff		.
	rst 38h			;3d5b	ff		.
	rst 38h			;3d5c	ff		.
	rst 38h			;3d5d	ff		.
	rst 38h			;3d5e	ff		.
	rst 38h			;3d5f	ff		.
	rst 38h			;3d60	ff		.
	rst 38h			;3d61	ff		.
	rst 38h			;3d62	ff		.
	rst 38h			;3d63	ff		.
	rst 38h			;3d64	ff		.
	rst 38h			;3d65	ff		.
	rst 38h			;3d66	ff		.
	rst 38h			;3d67	ff		.
	rst 38h			;3d68	ff		.
	rst 38h			;3d69	ff		.
	rst 38h			;3d6a	ff		.
	rst 38h			;3d6b	ff		.
	rst 38h			;3d6c	ff		.
	rst 38h			;3d6d	ff		.
	rst 38h			;3d6e	ff		.
	rst 38h			;3d6f	ff		.
	rst 38h			;3d70	ff		.
	rst 38h			;3d71	ff		.
	rst 38h			;3d72	ff		.
	rst 38h			;3d73	ff		.
	rst 38h			;3d74	ff		.
	rst 38h			;3d75	ff		.
	rst 38h			;3d76	ff		.
	rst 38h			;3d77	ff		.
	rst 38h			;3d78	ff		.
	rst 38h			;3d79	ff		.
	rst 38h			;3d7a	ff		.
	rst 38h			;3d7b	ff		.
	rst 38h			;3d7c	ff		.
	rst 38h			;3d7d	ff		.
	rst 38h			;3d7e	ff		.
	rst 38h			;3d7f	ff		.
	rst 38h			;3d80	ff		.
	rst 38h			;3d81	ff		.
	rst 38h			;3d82	ff		.
	rst 38h			;3d83	ff		.
	rst 38h			;3d84	ff		.
	rst 38h			;3d85	ff		.
	rst 38h			;3d86	ff		.
	rst 38h			;3d87	ff		.
	rst 38h			;3d88	ff		.
	rst 38h			;3d89	ff		.
	rst 38h			;3d8a	ff		.
	rst 38h			;3d8b	ff		.
	rst 38h			;3d8c	ff		.
	rst 38h			;3d8d	ff		.
	rst 38h			;3d8e	ff		.
	rst 38h			;3d8f	ff		.
	rst 38h			;3d90	ff		.
	rst 38h			;3d91	ff		.
	rst 38h			;3d92	ff		.
	rst 38h			;3d93	ff		.
	rst 38h			;3d94	ff		.
	rst 38h			;3d95	ff		.
	rst 38h			;3d96	ff		.
	rst 38h			;3d97	ff		.
	rst 38h			;3d98	ff		.
	rst 38h			;3d99	ff		.
	rst 38h			;3d9a	ff		.
	rst 38h			;3d9b	ff		.
	rst 38h			;3d9c	ff		.
	rst 38h			;3d9d	ff		.
	rst 38h			;3d9e	ff		.
	rst 38h			;3d9f	ff		.
	rst 38h			;3da0	ff		.
	rst 38h			;3da1	ff		.
	rst 38h			;3da2	ff		.
	rst 38h			;3da3	ff		.
	rst 38h			;3da4	ff		.
	rst 38h			;3da5	ff		.
	rst 38h			;3da6	ff		.
	rst 38h			;3da7	ff		.
	rst 38h			;3da8	ff		.
	rst 38h			;3da9	ff		.
	rst 38h			;3daa	ff		.
	rst 38h			;3dab	ff		.
	rst 38h			;3dac	ff		.
	rst 38h			;3dad	ff		.
	rst 38h			;3dae	ff		.
	rst 38h			;3daf	ff		.
	rst 38h			;3db0	ff		.
	rst 38h			;3db1	ff		.
	rst 38h			;3db2	ff		.
	rst 38h			;3db3	ff		.
	rst 38h			;3db4	ff		.
	rst 38h			;3db5	ff		.
	rst 38h			;3db6	ff		.
	rst 38h			;3db7	ff		.
	rst 38h			;3db8	ff		.
	rst 38h			;3db9	ff		.
	rst 38h			;3dba	ff		.
	rst 38h			;3dbb	ff		.
	rst 38h			;3dbc	ff		.
	rst 38h			;3dbd	ff		.
	rst 38h			;3dbe	ff		.
	rst 38h			;3dbf	ff		.
	rst 38h			;3dc0	ff		.
	rst 38h			;3dc1	ff		.
	rst 38h			;3dc2	ff		.
	rst 38h			;3dc3	ff		.
	rst 38h			;3dc4	ff		.
	rst 38h			;3dc5	ff		.
	rst 38h			;3dc6	ff		.
	rst 38h			;3dc7	ff		.
	rst 38h			;3dc8	ff		.
	rst 38h			;3dc9	ff		.
	rst 38h			;3dca	ff		.
	rst 38h			;3dcb	ff		.
	rst 38h			;3dcc	ff		.
	rst 38h			;3dcd	ff		.
	rst 38h			;3dce	ff		.
	rst 38h			;3dcf	ff		.
	rst 38h			;3dd0	ff		.
	rst 38h			;3dd1	ff		.
	rst 38h			;3dd2	ff		.
	rst 38h			;3dd3	ff		.
	rst 38h			;3dd4	ff		.
	rst 38h			;3dd5	ff		.
	rst 38h			;3dd6	ff		.
	rst 38h			;3dd7	ff		.
	rst 38h			;3dd8	ff		.
	rst 38h			;3dd9	ff		.
	rst 38h			;3dda	ff		.
	rst 38h			;3ddb	ff		.
	rst 38h			;3ddc	ff		.
	rst 38h			;3ddd	ff		.
	rst 38h			;3dde	ff		.
	rst 38h			;3ddf	ff		.
	rst 38h			;3de0	ff		.
	rst 38h			;3de1	ff		.
	rst 38h			;3de2	ff		.
	rst 38h			;3de3	ff		.
	rst 38h			;3de4	ff		.
	rst 38h			;3de5	ff		.
	rst 38h			;3de6	ff		.
	rst 38h			;3de7	ff		.
	rst 38h			;3de8	ff		.
	rst 38h			;3de9	ff		.
	rst 38h			;3dea	ff		.
	rst 38h			;3deb	ff		.
	rst 38h			;3dec	ff		.
	rst 38h			;3ded	ff		.
	rst 38h			;3dee	ff		.
	rst 38h			;3def	ff		.
	rst 38h			;3df0	ff		.
	rst 38h			;3df1	ff		.
	rst 38h			;3df2	ff		.
	rst 38h			;3df3	ff		.
	rst 38h			;3df4	ff		.
	rst 38h			;3df5	ff		.
	rst 38h			;3df6	ff		.
	rst 38h			;3df7	ff		.
	rst 38h			;3df8	ff		.
	rst 38h			;3df9	ff		.
	rst 38h			;3dfa	ff		.
	rst 38h			;3dfb	ff		.
	rst 38h			;3dfc	ff		.
	rst 38h			;3dfd	ff		.
	rst 38h			;3dfe	ff		.
	rst 38h			;3dff	ff		.
DOS_OFF:
				; = DOS_OFF (src: EXP.asm:1657, BIOS-TT 0271ac3)
	di			;3e00	f3		.
	push af			;3e01	f5		.
	push bc			;3e02	c5		.
	ld bc,(05bffh)		;3e03	ed 4b ff 5b	. K . [
	ld a,0c9h		;3e07	3e c9		> .
	ld (05bffh),a		;3e09	32 ff 5b	2 . [
	call 05bffh		;3e0c	cd ff 5b	. . [
	ld (05bffh),bc		;3e0f	ed 43 ff 5b	. C . [
	pop bc			;3e13	c1		.
	pop af			;3e14	f1		.
	ret			;3e15	c9		.

; BLOCK 'Fill3E16' (start 0x3e16 end 0x3e20)
Fill3E16:
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
FnD0_Unnamed:
	scf			;3e20	37		7
	ret			;3e21	c9		.
	in a,(0e2h)		;3e22	db e2		. .
	ld b,a			;3e24	47		G
	ld a,0feh		;3e25	3e fe		> .
	out (0e2h),a		;3e27	d3 e2		. .
	push hl			;3e29	e5		.
	ld l,c			;3e2a	69		i
	ld h,0c1h		;3e2b	26 c1		& .
	ld a,(hl)		;3e2d	7e		~
	pop hl			;3e2e	e1		.
	and a			;3e2f	a7		.
	scf			;3e30	37		7
	jr z,l3e49h		;3e31	28 16		( .
	out (0e2h),a		;3e33	d3 e2		. .
	ld (0c0feh),sp		;3e35	ed 73 fe c0	. s . .
	ld sp,0c0f0h		;3e39	31 f0 c0	1 . .
	push bc			;3e3c	c5		.
	call 0c100h		;3e3d	cd 00 c1	. . .
	pop bc			;3e40	c1		.
	ld sp,(0c0feh)		;3e41	ed 7b fe c0	. { . .
	ld a,b			;3e45	78		x
	out (0e2h),a		;3e46	d3 e2		. .
	ret			;3e48	c9		.
l3e49h:
	ld a,b			;3e49	78		x
	out (0e2h),a		;3e4a	d3 e2		. .
FN_LIB:
				; = FN_LIB (src: EXP.asm:1683, BIOS-TT 0271ac3)
	scf			;3e4c	37		7
	ret			;3e4d	c9		.
FnEA_FN_KBD_OUT:
				; = FN_KBD_OUT (src: EXP.asm:1726, BIOS-TT 0271ac3)
	and a			;3e4e	a7		.
	ld e,a			;3e4f	5f		_
	ld d,0ffh		;3e50	16 ff		. .
	jp pe,kbd_parity	;3e52	ea 57 3e	. W >
	ld d,0feh		;3e55	16 fe		. .
kbd_parity:
				; = kbd_parity (src: EXP.asm:1732, BIOS-TT 0271ac3)
	and a			;3e57	a7		.
	rl e			;3e58	cb 13		. .
	rl d			;3e5a	cb 12		. .
	ld c,00bh		;3e5c	0e 0b		. .
kbd_loop:
				; = kbd_loop (src: EXP.asm:1738, BIOS-TT 0271ac3)
	ld a,005h		;3e5e	3e 05		> .
	out (019h),a		;3e60	d3 19		. .
	ld a,060h		;3e62	3e 60		> `
	bit 0,e			;3e64	cb 43		. C
	jr nz,no_inv		;3e66	20 02		  .
	xor 002h		;3e68	ee 02		. .
no_inv:
				; = no_inv (src: EXP.asm:1745, BIOS-TT 0271ac3)
	ld l,a			;3e6a	6f		o
	out (019h),a		;3e6b	d3 19		. .
	ld a,005h		;3e6d	3e 05		> .
	out (019h),a		;3e6f	d3 19		. .
	ld a,l			;3e71	7d		}
	or 080h			;3e72	f6 80		. .
	out (019h),a		;3e74	d3 19		. .
	ld a,b			;3e76	78		x
kbd_loop1:
				; = kbd_loop1 (src: EXP.asm:1755, BIOS-TT 0271ac3)
	push hl			;3e77	e5		.
	pop hl			;3e78	e1		.
	dec a			;3e79	3d		=
	jr nz,kbd_loop1		;3e7a	20 fb		  .
	ld a,005h		;3e7c	3e 05		> .
	out (019h),a		;3e7e	d3 19		. .
	ld a,l			;3e80	7d		}
	out (019h),a		;3e81	d3 19		. .
	ld a,b			;3e83	78		x
kbd_loop2:
				; = kbd_loop2 (src: EXP.asm:1766, BIOS-TT 0271ac3)
	push hl			;3e84	e5		.
	pop hl			;3e85	e1		.
	dec a			;3e86	3d		=
	jr nz,kbd_loop2		;3e87	20 fb		  .
	rr d			;3e89	cb 1a		. .
	rr e			;3e8b	cb 1b		. .
	dec c			;3e8d	0d		.
	jr nz,kbd_loop		;3e8e	20 ce		  .
	ld a,005h		;3e90	3e 05		> .
	out (019h),a		;3e92	d3 19		. .
	ld a,060h		;3e94	3e 60		> `
	out (019h),a		;3e96	d3 19		. .
	and a			;3e98	a7		.
	ret			;3e99	c9		.

; BLOCK 'Data3E9A' (start 0x3e9a end 0x3f06)
Data3E9A:
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
l3f00h:
	defb 00ch		;3f00	0c		.
	defb 011h		;3f01	11		.
	defb 012h		;3f02	12		.
	defb 013h		;3f03	13		.
	defb 014h		;3f04	14		.
	defb 015h		;3f05	15		.
ROM_DISK_Pages:
				; = ROM_DISK.Pages (src: EXP.asm:1799, BIOS-TT 0271ac3)
	ld d,017h		;3f06	16 17		. .
	add hl,de		;3f08	19		.
	ld a,(de)		;3f09	1a		.
	dec de			;3f0a	1b		.
	rst 38h			;3f0b	ff		.
	rst 38h			;3f0c	ff		.
	rst 38h			;3f0d	ff		.
	rst 38h			;3f0e	ff		.
	rst 38h			;3f0f	ff		.
l3f10h:
	ex af,af'		;3f10	08		.
	dec a			;3f11	3d		=
	jr z,l3f3bh		;3f12	28 27		( '
	dec a			;3f14	3d		=
	scf			;3f15	37		7
	ret z			;3f16	c8		.
	ex de,hl		;3f17	eb		.
	add hl,hl		;3f18	29		)
	ex de,hl		;3f19	eb		.
	ld a,b			;3f1a	78		x
	add a,a			;3f1b	87		.
	ld b,a			;3f1c	47		G
	ret c			;3f1d	d8		.
	call l3f3bh		;3f1e	cd 3b 3f	. ; ?
	ret c			;3f21	d8		.
	and a			;3f22	a7		.
	rr d			;3f23	cb 1a		. .
	rr e			;3f25	cb 1b		. .
	xor a			;3f27	af		.
	ret			;3f28	c9		.

; BLOCK 'Data3F29' (start 0x3f29 end 0x3f2f)
Data3F29:
	defb 011h		;3f29	11		.
	defb 000h		;3f2a	00		.
	defb 000h		;3f2b	00		.
	defb 001h		;3f2c	01		.
	defb 000h		;3f2d	00		.
	defb 001h		;3f2e	01		.
l3f2fh:
	push de			;3f2f	d5		.
	push bc			;3f30	c5		.
	ld hl,l3f00h		;3f31	21 00 3f	! . ?
	ld de,0ff00h		;3f34	11 00 ff	. . .
	ld a,01fh		;3f37	3e 1f		> .
	jr l3f54h		;3f39	18 19		. .
l3f3bh:
	ld c,000h		;3f3b	0e 00		. .
ROM_DISK_loop:
				; = ~ROM_DISK.loop (src: FUNC_RAM_ROM_DRV.ASM:662, BIOS-TT 0271ac3)
	push de			;3f3d	d5		.
	push bc			;3f3e	c5		.
	ld a,e			;3f3f	7b		{
	and 03fh		;3f40	e6 3f		. ?
	push af			;3f42	f5		.
	ex de,hl		;3f43	eb		.
	add hl,hl		;3f44	29		)
	add hl,hl		;3f45	29		)
	ld a,(l3f00h)		;3f46	3a 00 3f	: . ?
	inc h			;3f49	24		$
	cp h			;3f4a	bc		.
	ld l,h			;3f4b	6c		l
	ld h,03fh		;3f4c	26 3f		& ?
	ld a,(hl)		;3f4e	7e		~
	pop hl			;3f4f	e1		.
	ld l,000h		;3f50	2e 00		. .
	jr c,l3f90h	;3f52	38 3c		8 <
l3f54h:
	di			;3f54	f3		.
ROM_DISK_loopRead:
				; = ~ROM_DISK.loopRead (src: FUNC_RAM_ROM_DRV.ASM:687, BIOS-TT 0271ac3)
	push hl			;3f55	e5		.
	push de			;3f56	d5		.
	ld hl,0ffeah		;3f57	21 ea ff	! . .
	add hl,sp		;3f5a	39		9
	push hl			;3f5b	e5		.
	ld de,Data3F82	;3f5c	11 82 3f	. . ?
	ex de,hl		;3f5f	eb		.
	ld bc,l000eh		;3f60	01 0e 00	. . .
	ldir			;3f63	ed b0		. .
	ld bc,ColdStart		;3f65	01 00 01	. . .
	ret			;3f68	c9		.
ROM_DISK_readNext:
				; = ~ROM_DISK.readNext (src: FUNC_RAM_ROM_DRV.ASM:704, BIOS-TT 0271ac3)
	pop bc			;3f69	c1		.
	inc c			;3f6a	0c		.
	dec b			;3f6b	05		.
	jr z,l3f7dh		;3f6c	28 0f		( .
	bit 6,h			;3f6e	cb 74		. t
	push bc			;3f70	c5		.
	jr z,l3f54h		;3f71	28 e1		( .
	pop hl			;3f73	e1		.
	ld a,b			;3f74	78		x
	ld b,000h		;3f75	06 00		. .
	add hl,bc		;3f77	09		.
	ld b,a			;3f78	47		G
	ex de,hl		;3f79	eb		.
	jp l3f3bh		;3f7a	c3 3b 3f	. ; ?
l3f7dh:
	pop hl			;3f7d	e1		.
	add hl,bc		;3f7e	09		.
	ex de,hl		;3f7f	eb		.
	and a			;3f80	a7		.
	ret			;3f81	c9		.

; BLOCK 'Data3F82' (start 0x3f82 end 0x3f90)
Data3F82:
	defb 0d1h		;3f82	d1		.
	defb 0e1h		;3f83	e1		.
	defb 0d3h		;3f84	d3		.
	defb 05ch		;3f85	5c		\
	defb 0edh		;3f86	ed		.
	defb 0b0h		;3f87	b0		.
	defb 0afh		;3f88	af		.
	defb 0d3h		;3f89	d3		.
	defb 05ch		;3f8a	5c		\
	defb 0d3h		;3f8b	d3		.
	defb 07ch		;3f8c	7c		|
	defb 0c3h		;3f8d	c3		.
	defb 069h		;3f8e	69		i
	defb 03fh		;3f8f	3f		?
l3f90h:
	pop bc			;3f90	c1		.
	pop de			;3f91	d1		.
	scf			;3f92	37		7
	ret			;3f93	c9		.

; BLOCK 'Fill3F94' (start 0x3f94 end 0x3fd0)
Fill3F94:
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
; Page stubs. ROM pages 0 and 8 have code at #3FD0-#3FFF that is meant to
; run across a page switch: OUT (#7C),A changes the page under the running
; code, and the next instruction is fetched from the other page at the
; next address. Worked example, ToPage0 (#3FE8): PUSH AF / LD A,1 /
; OUT (#7C),A switches to page 0; the CPU continues at page 0 #3FED, which
; holds JP #0100 (page 0 dispatcher). Page 0 returns through its own
; #3FE8 (LD A,0 / OUT (#7C),A), landing here at #3FED: POP AF / RET.
;---------------------------------------------------------------------------
BackFromPage0:
				; = FN1_RET (src: EXP.asm:1812, BIOS-TT 0271ac3)
	push af			;3fd0	f5		.
	ld a,001h		;3fd1	3e 01		> .
	out (07ch),a		;3fd3	d3 7c		. |
	pop af			;3fd5	f1		.
	rst 18h			;3fd6	df		.
	jr BackFromPage0		;3fd7	18 f7		. .

; BLOCK 'Fill3FD9' (start 0x3fd9 end 0x3fe8)
Fill3FD9:
	defb 0ffh		;3fd9	ff		.
	defb 0ffh		;3fda	ff		.
	defb 0ffh		;3fdb	ff		.
	defb 0ffh		;3fdc	ff		.
	defb 0ffh		;3fdd	ff		.
	defb 0ffh		;3fde	ff		.
	defb 0ffh		;3fdf	ff		.
	defb 0ffh		;3fe0	ff		.
	defb 0ffh		;3fe1	ff		.
	defb 0ffh		;3fe2	ff		.
	defb 0ffh		;3fe3	ff		.
	defb 0ffh		;3fe4	ff		.
	defb 0ffh		;3fe5	ff		.
	defb 0ffh		;3fe6	ff		.
	defb 0ffh		;3fe7	ff		.
ToPage0:
				; = EXP_HDD (src: EXP.asm:1840, BIOS-TT 0271ac3)
	push af			;3fe8	f5		.
	ld a,001h		;3fe9	3e 01		> .
	out (07ch),a		;3feb	d3 7c		. |
	pop af			;3fed	f1		.
	ret			;3fee	c9		.

; BLOCK 'Fill3FEF' (start 0x3fef end 0x3ff8)
Fill3FEF:
	defb 0ffh		;3fef	ff		.
	defb 0ffh		;3ff0	ff		.
	defb 0ffh		;3ff1	ff		.
	defb 0ffh		;3ff2	ff		.
	defb 0ffh		;3ff3	ff		.
	defb 0ffh		;3ff4	ff		.
	defb 0ffh		;3ff5	ff		.
	defb 0ffh		;3ff6	ff		.
	defb 0ffh		;3ff7	ff		.
Page0CallEntry:
				; = EXP_FNS_RET (src: EXP.asm:1862, BIOS-TT 0271ac3)
	push af			;3ff8	f5		.
	ld a,000h		;3ff9	3e 00		> .
	out (03ch),a		;3ffb	d3 3c		. <
	jp BiosCallFromPage0	;3ffd	c3 00 31	. . 1
