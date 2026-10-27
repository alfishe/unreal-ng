; prosiak.exe (Wild Sound XM player, Sprinter). Rebuilt by tools/make_listings.py from the
; file of the MAME pack's system disk (DEMOS/WILDSND); z80dasm 1.2.0 with -a -l -t.
; Stage 2: the loader VM, #0000-#0606 of its own page (window 0)
; z80dasm 1.2.0

	org 00000h

l0000h:
	nop			;0000	00		.
l0001h:
	nop			;0001	00		.
	nop			;0002	00		.
	nop			;0003	00		.
	nop			;0004	00		.
	nop			;0005	00		.
	nop			;0006	00		.
	nop			;0007	00		.

; BIOS calls: through the trampoline at #3DDF (window 0 back to the DSS page).
Rst08:
	jp 03ddfh		;0008	c3 df 3d	. . =
	nop			;000b	00		.
	nop			;000c	00		.
	nop			;000d	00		.
	nop			;000e	00		.
	nop			;000f	00		.

; DSS calls: through the trampoline at #3DE5.
Rst10:
	jp 03de5h		;0010	c3 e5 3d	. . =

; BLOCK 'd0013' (start 0x0013 end 0x0038)
d0013_start:
	defb 000h		;0013	00		.
	defb 000h		;0014	00		.
l0015h:
	defb 000h		;0015	00		.
	defb 000h		;0016	00		.
	defb 000h		;0017	00		.
	defb 000h		;0018	00		.
	defb 000h		;0019	00		.
	defb 000h		;001a	00		.
	defb 000h		;001b	00		.
	defb 000h		;001c	00		.
	defb 000h		;001d	00		.
	defb 000h		;001e	00		.
	defb 000h		;001f	00		.
l0020h:
	defb 000h		;0020	00		.
	defb 000h		;0021	00		.
	defb 000h		;0022	00		.
	defb 000h		;0023	00		.
	defb 000h		;0024	00		.
	defb 000h		;0025	00		.
	defb 000h		;0026	00		.
	defb 000h		;0027	00		.
	defb 000h		;0028	00		.
	defb 000h		;0029	00		.
	defb 000h		;002a	00		.
	defb 000h		;002b	00		.
	defb 000h		;002c	00		.
	defb 000h		;002d	00		.
	defb 000h		;002e	00		.
	defb 000h		;002f	00		.
	defb 000h		;0030	00		.
	defb 000h		;0031	00		.
	defb 000h		;0032	00		.
	defb 000h		;0033	00		.
	defb 000h		;0034	00		.
	defb 000h		;0035	00		.
	defb 000h		;0036	00		.
	defb 000h		;0037	00		.
	call sub_003ch		;0038	cd 3c 00	. < .
	ei			;003b	fb		.
sub_003ch:
	ret			;003c	c9		.

; BLOCK 'd003d' (start 0x003d end 0x0064)
d003d_start:
	defb 000h		;003d	00		.
	defb 000h		;003e	00		.
	defb 000h		;003f	00		.
l0040h:
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
	defb 000h		;004c	00		.
	defb 000h		;004d	00		.
	defb 000h		;004e	00		.
	defb 000h		;004f	00		.
	defb 000h		;0050	00		.
	defb 000h		;0051	00		.
	defb 000h		;0052	00		.
	defb 000h		;0053	00		.
	defb 000h		;0054	00		.
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
	retn			;0064	ed 45		. E
	nop			;0066	00		.
	nop			;0067	00		.
	nop			;0068	00		.
	defb 0c3h,064h		;0069	c3 64		. d

; BLOCK 'd006b' (start 0x006b end 0x0100)
d006b_start:
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
	defb 000h		;009d	00		.
	defb 000h		;009e	00		.
	defb 000h		;009f	00		.
	defb 000h		;00a0	00		.
	defb 000h		;00a1	00		.
	defb 000h		;00a2	00		.
	defb 000h		;00a3	00		.
	defb 000h		;00a4	00		.
	defb 000h		;00a5	00		.
	defb 000h		;00a6	00		.
	defb 000h		;00a7	00		.
	defb 000h		;00a8	00		.
	defb 000h		;00a9	00		.
	defb 000h		;00aa	00		.
	defb 000h		;00ab	00		.
	defb 000h		;00ac	00		.
	defb 000h		;00ad	00		.
	defb 000h		;00ae	00		.
	defb 000h		;00af	00		.
	defb 000h		;00b0	00		.
	defb 000h		;00b1	00		.
	defb 000h		;00b2	00		.
	defb 000h		;00b3	00		.
	defb 000h		;00b4	00		.
	defb 000h		;00b5	00		.
	defb 000h		;00b6	00		.
	defb 000h		;00b7	00		.
	defb 000h		;00b8	00		.
	defb 000h		;00b9	00		.
	defb 000h		;00ba	00		.
	defb 000h		;00bb	00		.
	defb 000h		;00bc	00		.
	defb 000h		;00bd	00		.
	defb 000h		;00be	00		.
	defb 000h		;00bf	00		.
	defb 000h		;00c0	00		.
	defb 000h		;00c1	00		.
	defb 000h		;00c2	00		.
	defb 000h		;00c3	00		.
	defb 000h		;00c4	00		.
	defb 000h		;00c5	00		.
	defb 000h		;00c6	00		.
	defb 000h		;00c7	00		.
	defb 000h		;00c8	00		.
	defb 000h		;00c9	00		.
	defb 000h		;00ca	00		.
	defb 000h		;00cb	00		.
	defb 000h		;00cc	00		.
	defb 000h		;00cd	00		.
	defb 000h		;00ce	00		.
	defb 000h		;00cf	00		.
	defb 000h		;00d0	00		.
	defb 000h		;00d1	00		.
	defb 000h		;00d2	00		.
	defb 000h		;00d3	00		.
	defb 000h		;00d4	00		.
	defb 000h		;00d5	00		.
	defb 000h		;00d6	00		.
	defb 000h		;00d7	00		.
	defb 000h		;00d8	00		.
	defb 000h		;00d9	00		.
	defb 000h		;00da	00		.
	defb 000h		;00db	00		.
	defb 000h		;00dc	00		.
	defb 000h		;00dd	00		.
	defb 000h		;00de	00		.
	defb 000h		;00df	00		.
	defb 000h		;00e0	00		.
	defb 000h		;00e1	00		.
	defb 000h		;00e2	00		.
	defb 000h		;00e3	00		.
	defb 000h		;00e4	00		.
	defb 000h		;00e5	00		.
	defb 000h		;00e6	00		.
	defb 000h		;00e7	00		.
	defb 000h		;00e8	00		.
	defb 000h		;00e9	00		.
	defb 000h		;00ea	00		.
	defb 000h		;00eb	00		.
	defb 000h		;00ec	00		.
	defb 000h		;00ed	00		.
	defb 000h		;00ee	00		.
	defb 000h		;00ef	00		.
	defb 000h		;00f0	00		.
	defb 000h		;00f1	00		.
	defb 000h		;00f2	00		.
	defb 000h		;00f3	00		.
	defb 000h		;00f4	00		.
	defb 000h		;00f5	00		.
	defb 000h		;00f6	00		.
	defb 000h		;00f7	00		.
	defb 000h		;00f8	00		.
	defb 000h		;00f9	00		.
	defb 000h		;00fa	00		.
	defb 000h		;00fb	00		.
	defb 000h		;00fc	00		.
	defb 000h		;00fd	00		.
	defb 000h		;00fe	00		.
	defb 000h		;00ff	00		.

; Prints two lines, keeps SP for VmExit, sets the key word #05C4 = #FFFF and runs
; the VM script from the file's second sector (file offset #100).
Stage2Start:
	ld hl,d01cd_start	;0100	21 cd 01	! . .
	call PrintZ		;0103	cd 21 01	. ! .
	ld hl,044d4h		;0106	21 d4 44	! . D
	ld c,05ch		;0109	0e 5c		. \
	rst 10h			;010b	d7		.
	ld (VmExit+1),sp	;010c	ed 73 1d 01	. s . .
	ld hl,0ffffh		;0110	21 ff ff	! . .
	ld (KeyWord),hl		;0113	22 c4 05	" . .
	ld hl,l0000h		;0116	21 00 00	! . .
	jp VmSeek		;0119	c3 44 02	. D .

; Back to the loader (#43C7): SP as at the start, DI, RET. Script op `24 1C 01`.
VmExit:
	ld sp,l0000h		;011c	31 00 00	1 . .
	di			;011f	f3		.
	ret			;0120	c9		.

; Print the zero-terminated string at HL (DSS PutChar #5B).
PrintZ:
	ld a,(hl)		;0121	7e		~
	inc hl			;0122	23		#
	or a			;0123	b7		.
	ret z			;0124	c8		.
	push hl			;0125	e5		.
	call PutChar		;0126	cd 2c 01	. , .
	pop hl			;0129	e1		.
	jr PrintZ		;012a	18 f5		. .
PutChar:
	push bc			;012c	c5		.
	ld c,05bh		;012d	0e 5b		. [
	rst 10h			;012f	d7		.
	pop bc			;0130	c1		.
	ret			;0131	c9		.
sub_0132h:
	and 00fh		;0132	e6 0f		. .
	cp 00ah			;0134	fe 0a		. .
	jr nc,l013bh		;0136	30 03		0 .
	add a,030h		;0138	c6 30		. 0
	ret			;013a	c9		.
l013bh:
	add a,037h		;013b	c6 37		. 7
	ret			;013d	c9		.
PrintHex16:
	ld a,l			;013e	7d		}
	push af			;013f	f5		.
	ld a,h			;0140	7c		|
	call PrintHex8		;0141	cd 45 01	. E .
	pop af			;0144	f1		.
PrintHex8:
	push af			;0145	f5		.
	rrca			;0146	0f		.
	rrca			;0147	0f		.
	rrca			;0148	0f		.
	rrca			;0149	0f		.
	call sub_0132h		;014a	cd 32 01	. 2 .
	ld c,05bh		;014d	0e 5b		. [
	rst 10h			;014f	d7		.
	pop af			;0150	f1		.
	call sub_0132h		;0151	cd 32 01	. 2 .
	ld c,05bh		;0154	0e 5b		. [
	rst 10h			;0156	d7		.
	ret			;0157	c9		.
PrintDec:
	ld c,0ffh		;0158	0e ff		. .
	ld de,02710h		;015a	11 10 27	. . '
	call sub_0175h		;015d	cd 75 01	. u .
	ld de,l03e8h		;0160	11 e8 03	. . .
	call sub_0175h		;0163	cd 75 01	. u .
	ld de,d003d_end		;0166	11 64 00	. d .
	call sub_0175h		;0169	cd 75 01	. u .
	ld de,Rst08+2		;016c	11 0a 00	. . .
	call sub_0175h		;016f	cd 75 01	. u .
	ld a,l			;0172	7d		}
	jr l0183h		;0173	18 0e		. .
sub_0175h:
	xor a			;0175	af		.
	dec a			;0176	3d		=
l0177h:
	inc a			;0177	3c		<
	sbc hl,de		;0178	ed 52		. R
	jr nc,l0177h		;017a	30 fb		0 .
	add hl,de		;017c	19		.
	inc c			;017d	0c		.
	jr nz,l0183h		;017e	20 03		  .
	dec c			;0180	0d		.
	or a			;0181	b7		.
	ret z			;0182	c8		.
l0183h:
	inc c			;0183	0c		.
	add a,030h		;0184	c6 30		. 0
	push hl			;0186	e5		.
	push de			;0187	d5		.
	push bc			;0188	c5		.
	call PutChar		;0189	cd 2c 01	. , .
	pop bc			;018c	c1		.
	pop de			;018d	d1		.
	pop hl			;018e	e1		.
	ret			;018f	c9		.

; Debug dump: BC bytes from HL (not called by this file's script).
HexDump:
	ld d,0ffh		;0190	16 ff		. .
l0192h:
	ld a,b			;0192	78		x
	or c			;0193	b1		.
	jr z,l01c2h		;0194	28 2c		( ,
	push bc			;0196	c5		.
	push hl			;0197	e5		.
	dec d			;0198	15		.
	jp m,l01a3h		;0199	fa a3 01	. . .
	jr nz,l01b4h		;019c	20 16		  .
	ld a,00ah		;019e	3e 0a		> .
	ld c,05bh		;01a0	0e 5b		. [
	rst 10h			;01a2	d7		.
l01a3h:
	ld a,00dh		;01a3	3e 0d		> .
	ld c,05bh		;01a5	0e 5b		. [
	rst 10h			;01a7	d7		.
	pop hl			;01a8	e1		.
	push hl			;01a9	e5		.
	call PrintHex16		;01aa	cd 3e 01	. > .
	ld a,020h		;01ad	3e 20		>  
	ld c,05bh		;01af	0e 5b		. [
	rst 10h			;01b1	d7		.
	ld d,020h		;01b2	16 20		.  
l01b4h:
	pop hl			;01b4	e1		.
	ld a,(hl)		;01b5	7e		~
	inc hl			;01b6	23		#
	push hl			;01b7	e5		.
	push de			;01b8	d5		.
	call PrintHex8		;01b9	cd 45 01	. E .
	pop de			;01bc	d1		.
	pop hl			;01bd	e1		.
	pop bc			;01be	c1		.
	dec bc			;01bf	0b		.
	jr l0192h		;01c0	18 d0		. .
l01c2h:
	ld a,00ah		;01c2	3e 0a		> .
	ld c,05bh		;01c4	0e 5b		. [
	rst 10h			;01c6	d7		.
	ld a,00dh		;01c7	3e 0d		> .
	ld c,05bh		;01c9	0e 5b		. [
	rst 10h			;01cb	d7		.
	ret			;01cc	c9		.

; BLOCK 'd01cd' (start 0x01cd end 0x01ee)
d01cd_start:
	defb 054h		;01cd	54		T
	defb 068h		;01ce	68		h
	defb 069h		;01cf	69		i
	defb 073h		;01d0	73		s
	defb 020h		;01d1	20		 
	defb 054h		;01d2	54		T
	defb 065h		;01d3	65		e
	defb 078h		;01d4	78		x
	defb 074h		;01d5	74		t
	defb 020h		;01d6	20		 
	defb 046h		;01d7	46		F
	defb 072h		;01d8	72		r
	defb 06fh		;01d9	6f		o
	defb 06dh		;01da	6d		m
	defb 020h		;01db	20		 
	defb 053h		;01dc	53		S
	defb 068h		;01dd	68		h
	defb 061h		;01de	61		a
	defb 064h		;01df	64		d
	defb 06fh		;01e0	6f		o
	defb 077h		;01e1	77		w
	defb 020h		;01e2	20		 
	defb 050h		;01e3	50		P
	defb 061h		;01e4	61		a
	defb 067h		;01e5	67		g
	defb 065h		;01e6	65		e
	defb 020h		;01e7	20		 
	defb 02eh		;01e8	2e		.
	defb 02eh		;01e9	2e		.
	defb 02eh		;01ea	2e		.
	defb 00dh		;01eb	0d		.
	defb 00ah		;01ec	0a		.
	defb 000h		;01ed	00		.

; Ops #E8 (if ... gosub) / #EC (if ... goto): two operand words, each the low
; byte itself when the high byte is 0, else the byte at that address (C = first,
; B = second); the next script byte is the condition: #C0 C < B, #C6 C <= B,
; #CE ==, #D4 C >= B, #DC C > B, #E2 !=; then the 2-byte target, skipped when the
; condition is false. Read from the code; this file's script has none.
OpCompare:
	ex af,af'		;01ee	08		.
	ld e,(hl)		;01ef	5e		^
	inc hl			;01f0	23		#
	ld d,(hl)		;01f1	56		V
	inc hl			;01f2	23		#
	ld a,e			;01f3	7b		{
	inc d			;01f4	14		.
	dec d			;01f5	15		.
	jr z,l01f9h		;01f6	28 01		( .
	ld a,(de)		;01f8	1a		.
l01f9h:
	ld c,a			;01f9	4f		O
	ld e,(hl)		;01fa	5e		^
	inc hl			;01fb	23		#
	ld d,(hl)		;01fc	56		V
	inc hl			;01fd	23		#
	ld a,e			;01fe	7b		{
	inc d			;01ff	14		.
	dec d			;0200	15		.
	jr z,l0204h		;0201	28 01		( .
	ld a,(de)		;0203	1a		.
l0204h:
	ld b,a			;0204	47		G
	ld (0024bh),hl		;0205	22 4b 02	" K .
	jr VmFetch		;0208	18 43		. C
l020ah:
	inc hl			;020a	23		#
	inc hl			;020b	23		#
	ld (0024bh),hl		;020c	22 4b 02	" K .
	jr VmLoop		;020f	18 36		. 6
l0211h:
	ex af,af'		;0211	08		.
	jr l024fh		;0212	18 3b		. ;
	ld a,c			;0214	79		y
	sub b			;0215	90		.
	jr c,l0211h		;0216	38 f9		8 .
	jr l020ah		;0218	18 f0		. .
	ld a,c			;021a	79		y
	sub b			;021b	90		.
	jr c,l0211h		;021c	38 f3		8 .
	jr z,l0211h		;021e	28 f1		( .
	jr l020ah		;0220	18 e8		. .
	ld a,c			;0222	79		y
	xor b			;0223	a8		.
	jr z,l0211h		;0224	28 eb		( .
	jr l020ah		;0226	18 e2		. .
	ld a,b			;0228	78		x
	sub c			;0229	91		.
	jr c,l0211h		;022a	38 e5		8 .
	jr z,l0211h		;022c	28 e3		( .
	jr l020ah		;022e	18 da		. .
	ld a,b			;0230	78		x
	sub c			;0231	91		.
	jr c,l0211h		;0232	38 dd		8 .
	jr l020ah		;0234	18 d4		. .
	ld a,b			;0236	78		x
	xor c			;0237	a9		.
	jr nz,l0211h		;0238	20 d7		  .
	jr l020ah		;023a	18 ce		. .
	ld a,040h		;023c	3e 40		> @
	jr d01cd_end		;023e	18 ae		. .
	ld a,047h		;0240	3e 47		> G
	jr d01cd_end		;0242	18 aa		. .

; Read #40 script bytes from the VM position into #4106.
VmSeek:
	call sub_035ch		;0244	cd 5c 03	. \ .

; Re-read when the pointer reaches #4136 (16 bytes before the buffer's end).
VmLoop:
	call sub_0354h		;0247	cd 54 03	. T .
	ld hl,04106h		;024a	21 06 41	! . A

; Opcode = the displacement of the JR at #0252 (self-modifying dispatch):
; target = #0254 + op. Traced with TTD: #024D runs once per op.
VmFetch:
	ld a,(hl)		;024d	7e		~
	inc hl			;024e	23		#
l024fh:
	ld (l0252h+1),a		;024f	32 53 02	2 S .
l0252h:
	jr l0252h		;0252	18 fe		. .

; Op #00: load a record (5 bytes: A, DE, HL, BC) without unpacking.
OpLoad:
	call LoadRecord		;0254	cd 69 04	. i .
	jr VmLoop		;0257	18 ee		. .

; Op #05 `05 A DE HL BC`: read BC bytes from file offset #100*(DE+1)+A to HL,
; then UnpackInPlace.
OpLoadUnpack:
	call LoadRecord		;0259	cd 69 04	. i .
	call Nop		;025c	cd 52 03	. R .
	ld hl,(l03bdh)		;025f	2a bd 03	* . .
	ld bc,(l03bfh)		;0262	ed 4b bf 03	. K . .
	call UnpackInPlace	;0266	cd eb 02	. . .
	call Nop		;0269	cd 52 03	. R .
	jr VmLoop		;026c	18 d9		. .

; Op #1A: write a record to the #8000 window (read from the code; unused here).
OpPut:
	call sub_0420h		;026e	cd 20 04	.   .
	jr VmLoop		;0271	18 d4		. .

; Op #1F: as #1A with Carry set (unused here).
OpPutC:
	call sub_0419h		;0273	cd 19 04	. . .
	jr VmLoop		;0276	18 cf		. .

; Op #24 `24 lo hi`: CALL lo hi with HL / BC = the last block's address / length.
OpCall:
	ld e,(hl)		;0278	5e		^
	inc hl			;0279	23		#
	ld d,(hl)		;027a	56		V
	inc hl			;027b	23		#
	ld (0024bh),hl		;027c	22 4b 02	" K .
	ld (0028dh),de		;027f	ed 53 8d 02	. S . .
	call Nop		;0283	cd 52 03	. R .
	ld hl,l0000h		;0286	21 00 00	! . .
	ld bc,l0000h		;0289	01 00 00	. . .
	call l0000h		;028c	cd 00 00	. . .
	call Nop		;028f	cd 52 03	. R .
	jr VmLoop		;0292	18 b3		. .

; Op #40 `40 lo hi`: push the return position (Z80 stack), continue at lo hi.
; Op #47 enters at #029B: goto. Op #63 (#02B7): return. Unused here.
OpGosub:
	call sub_02dah		;0294	cd da 02	. . .
	inc de			;0297	13		.
	inc de			;0298	13		.
	inc de			;0299	13		.
	push de			;029a	d5		.
	ld e,(hl)		;029b	5e		^
	inc hl			;029c	23		#
	ld d,(hl)		;029d	56		V
	inc hl			;029e	23		#
l029fh:
	ld (l02d8h),de		;029f	ed 53 d8 02	. S . .
	ld hl,04106h		;02a3	21 06 41	! . A
	ld (0024bh),hl		;02a6	22 4b 02	" K .
	jr VmSeek		;02a9	18 99		. .

; Op #57 `57 fn arg`: page service (#0375).
OpPages:
	ld a,(hl)		;02ab	7e		~
	inc hl			;02ac	23		#
	ld c,(hl)		;02ad	4e		N
	inc hl			;02ae	23		#
	ld (0024bh),hl		;02af	22 4b 02	" K .
	call PageService	;02b2	cd 75 03	. u .
	jr VmLoop		;02b5	18 90		. .
	pop de			;02b7	d1		.
	jr l029fh		;02b8	18 e5		. .

; Op #66: word XOR (#05C4) == 0: the XOR key at #05C6 becomes active, else jump.
; Unused here: #05C4 stays #FFFF and the key is empty.
OpKeyCheck:
	ld a,(KeyWord)		;02ba	3a c4 05	: . .
	xor (hl)		;02bd	ae		.
	ld e,a			;02be	5f		_
	inc hl			;02bf	23		#
	ld a,(l05c5h)		;02c0	3a c5 05	: . .
	xor (hl)		;02c3	ae		.
	or e			;02c4	b3		.
	inc hl			;02c5	23		#
	ld e,(hl)		;02c6	5e		^
	inc hl			;02c7	23		#
	ld d,(hl)		;02c8	56		V
	inc hl			;02c9	23		#
	ld (0024bh),hl		;02ca	22 4b 02	" K .
	jr nz,l029fh		;02cd	20 d0		  .
	ld hl,XorKey		;02cf	21 c6 05	! . .
	ld (00507h),hl		;02d2	22 07 05	" . .
	jp VmLoop		;02d5	c3 47 02	. G .
l02d8h:
	nop			;02d8	00		.
	nop			;02d9	00		.
sub_02dah:
	push hl			;02da	e5		.
	ld hl,(0024bh)		;02db	2a 4b 02	* K .
	ld de,0befah		;02de	11 fa be	. . .
	add hl,de		;02e1	19		.
	ld de,(l02d8h)		;02e2	ed 5b d8 02	. [ . .
	add hl,de		;02e6	19		.
	ld e,h			;02e7	5c		\
	ex de,hl		;02e8	eb		.
	pop hl			;02e9	e1		.
	ret			;02ea	c9		.

; HL = record start: a word = the unpacked length, then the LZSA2 stream; BC =
; record length. The output ends where the record ends; the 32 bytes behind
; the record are saved at #05A4 during the unpacking and put back after it.
UnpackInPlace:
	ld e,(hl)		;02eb	5e		^
	inc hl			;02ec	23		#
	ld d,(hl)		;02ed	56		V
	ld (0028ah),de		;02ee	ed 53 8a 02	. S . .
	ld (00333h),hl		;02f2	22 33 03	" 3 .
	dec bc			;02f5	0b		.
	dec bc			;02f6	0b		.
	add hl,bc		;02f7	09		.
	ld (00313h),bc		;02f8	ed 43 13 03	. C . .
	push hl			;02fc	e5		.
	inc hl			;02fd	23		#
	sbc hl,de		;02fe	ed 52		. R
	ld (00287h),hl		;0300	22 87 02	" . .
	ld (0031ah),hl		;0303	22 1a 03	" . .
	pop hl			;0306	e1		.
	ld bc,l0020h		;0307	01 20 00	.   .
	add hl,bc		;030a	09		.
	jr c,l0328h		;030b	38 1b		8 .
	push hl			;030d	e5		.
	call sub_034dh		;030e	cd 4d 03	. M .
	pop de			;0311	d1		.
	ld bc,l0000h		;0312	01 00 00	. . .
	lddr			;0315	ed b8		. .
	ex de,hl		;0317	eb		.
	inc hl			;0318	23		#
	ld de,l0000h		;0319	11 00 00	. . .
	call 03d56h		;031c	cd 56 3d	. V =
	ld hl,SaveArea		;031f	21 a4 05	! . .
	ld bc,l0020h		;0322	01 20 00	.   .
	ldir			;0325	ed b0		. .
	ret			;0327	c9		.
l0328h:
	push de			;0328	d5		.
	ld hl,(0031ah)		;0329	2a 1a 03	* . .
	dec hl			;032c	2b		+
	call sub_034dh		;032d	cd 4d 03	. M .
	ex de,hl		;0330	eb		.
	inc de			;0331	13		.
	ld hl,l0000h		;0332	21 00 00	! . .
	inc hl			;0335	23		#
	call 03d56h		;0336	cd 56 3d	. V =
	dec de			;0339	1b		.
	ld l,e			;033a	6b		k
	ld h,d			;033b	62		b
	ld bc,l0020h		;033c	01 20 00	.   .
	add hl,bc		;033f	09		.
	ex de,hl		;0340	eb		.
	pop bc			;0341	c1		.
	lddr			;0342	ed b8		. .
	ld hl,l05c3h		;0344	21 c3 05	! . .
l0347h:
	ld bc,l0020h		;0347	01 20 00	.   .
	lddr			;034a	ed b8		. .
	ret			;034c	c9		.
sub_034dh:
	ld de,l05c3h		;034d	11 c3 05	. . .
	jr l0347h		;0350	18 f5		. .

; DI; RET (around every call out of the VM).
Nop:
	di			;0352	f3		.
	ret			;0353	c9		.
sub_0354h:
	ld hl,(0024bh)		;0354	2a 4b 02	* K .
	ld de,0becah		;0357	11 ca be	. . .
	add hl,de		;035a	19		.
	ret nc			;035b	d0		.
sub_035ch:
	call sub_02dah		;035c	cd da 02	. . .
	ld (l02d8h),de		;035f	ed 53 d8 02	. S . .
	ld a,e			;0363	7b		{
	ld e,d			;0364	5a		Z
	ld d,000h		;0365	16 00		. .
	ld hl,04106h		;0367	21 06 41	! . A
	ld (0024bh),hl		;036a	22 4b 02	" K .
	ld bc,l0040h		;036d	01 40 00	. @ .
	call ReadFile		;0370	cd 73 04	. s .
	ret			;0373	c9		.
l0374h:
	nop			;0374	00		.

; A = 2: map the remembered tag's page into window 2 (#C2); 3: into window 3
; (#E2); 4: restore both; 5: remember tag C; 6 (C != 0): AllocPage for it.
PageService:
	dec a			;0375	3d		=
	dec a			;0376	3d		=
	jr z,l0386h		;0377	28 0d		( .
	dec a			;0379	3d		=
	jr z,l0391h		;037a	28 15		( .
	dec a			;037c	3d		=
	jr z,l039ch		;037d	28 1d		( .
	dec a			;037f	3d		=
	jr z,l03a7h		;0380	28 25		( %
	dec a			;0382	3d		=
	jr z,l03ach		;0383	28 27		( '
	ret			;0385	c9		.
l0386h:
	ld a,(l0374h)		;0386	3a 74 03	: t .
	ld c,a			;0389	4f		O
	call 043a2h		;038a	cd a2 43	. . C
	ret c			;038d	d8		.
	out (0c2h),a		;038e	d3 c2		. .
	ret			;0390	c9		.
l0391h:
	ld a,(l0374h)		;0391	3a 74 03	: t .
	ld c,a			;0394	4f		O
	call 043a2h		;0395	cd a2 43	. . C
	ret c			;0398	d8		.
	out (0e2h),a		;0399	d3 e2		. .
	ret			;039b	c9		.
l039ch:
	ld a,(04103h)		;039c	3a 03 41	: . A
	out (0c2h),a		;039f	d3 c2		. .
	ld a,(04104h)		;03a1	3a 04 41	: . A
	out (0e2h),a		;03a4	d3 e2		. .
	ret			;03a6	c9		.
l03a7h:
	ld a,c			;03a7	79		y
	ld (l0374h),a		;03a8	32 74 03	2 t .
	ret			;03ab	c9		.
l03ach:
	ld a,c			;03ac	79		y
	or a			;03ad	b7		.
	ret z			;03ae	c8		.
	ld bc,(l0374h)		;03af	ed 4b 74 03	. K t .
	call 04375h		;03b3	cd 75 43	. u C
	ret			;03b6	c9		.
sub_03b7h:
	push bc			;03b7	c5		.
	ld bc,07ffdh		;03b8	01 fd 7f	. . .
	pop bc			;03bb	c1		.
	ret			;03bc	c9		.
l03bdh:
	nop			;03bd	00		.
	nop			;03be	00		.
l03bfh:
	nop			;03bf	00		.
	nop			;03c0	00		.

; A, DE, HL, BC from the script.
ReadRecord:
	ld a,(hl)		;03c1	7e		~
	inc hl			;03c2	23		#
	ld e,(hl)		;03c3	5e		^
	inc hl			;03c4	23		#
	ld d,(hl)		;03c5	56		V
	inc hl			;03c6	23		#
	push de			;03c7	d5		.
	ld e,(hl)		;03c8	5e		^
	inc hl			;03c9	23		#
	ld d,(hl)		;03ca	56		V
	inc hl			;03cb	23		#
	ld c,(hl)		;03cc	4e		N
	inc hl			;03cd	23		#
	ld b,(hl)		;03ce	46		F
	inc hl			;03cf	23		#
	ld (0024bh),hl		;03d0	22 4b 02	" K .
	ex de,hl		;03d3	eb		.
	pop de			;03d4	d1		.
	ld (l03bdh),hl		;03d5	22 bd 03	" . .
	ld (l03bfh),bc		;03d8	ed 43 bf 03	. C . .
	ret			;03dc	c9		.
sub_03ddh:
	ld hl,08000h		;03dd	21 00 80	! . .
	push af			;03e0	f5		.
	ld a,010h		;03e1	3e 10		> .
	call sub_03b7h		;03e3	cd b7 03	. . .
	pop af			;03e6	f1		.
	ret			;03e7	c9		.
l03e8h:
	push hl			;03e8	e5		.
	push af			;03e9	f5		.
	ld hl,(sub_03ddh+1)	;03ea	2a de 03	* . .
	ld a,(003e2h)		;03ed	3a e2 03	: . .
	add hl,bc		;03f0	09		.
	jr nc,l0401h		;03f1	30 0e		0 .
	set 7,h			;03f3	cb fc		. .
	set 6,h			;03f5	cb f4		. .
	inc a			;03f7	3c		<
	cp 012h			;03f8	fe 12		. .
	jr z,l0400h		;03fa	28 04		( .
	cp 015h			;03fc	fe 15		. .
	jr nz,l0401h		;03fe	20 01		  .
l0400h:
	inc a			;0400	3c		<
l0401h:
	ld (sub_03ddh+1),hl	;0401	22 de 03	" . .
	ld (003e2h),a		;0404	32 e2 03	2 . .
	pop af			;0407	f1		.
	pop hl			;0408	e1		.
	ret			;0409	c9		.
sub_040ah:
	push hl			;040a	e5		.
	call sub_03ddh		;040b	cd dd 03	. . .
	ld (hl),a		;040e	77		w
	pop hl			;040f	e1		.
	push bc			;0410	c5		.
	ld bc,l0001h		;0411	01 01 00	. . .
	call l03e8h		;0414	cd e8 03	. . .
	pop bc			;0417	c1		.
	ret			;0418	c9		.
sub_0419h:
	call ReadRecord		;0419	cd c1 03	. . .
	push af			;041c	f5		.
	scf			;041d	37		7
	jr l0425h		;041e	18 05		. .
sub_0420h:
	call ReadRecord		;0420	cd c1 03	. . .
	push af			;0423	f5		.
	and a			;0424	a7		.
l0425h:
	push af			;0425	f5		.
	ld a,l			;0426	7d		}
	call sub_040ah		;0427	cd 0a 04	. . .
	ld a,h			;042a	7c		|
	call sub_040ah		;042b	cd 0a 04	. . .
	ld a,c			;042e	79		y
	call sub_040ah		;042f	cd 0a 04	. . .
	pop af			;0432	f1		.
	ld a,b			;0433	78		x
	bit 7,a			;0434	cb 7f		. .
	jr nz,l0440h		;0436	20 08		  .
	jr nc,l044eh		;0438	30 14		0 .
	set 7,a			;043a	cb ff		. .
	bit 6,a			;043c	cb 77		. w
	jr z,l044eh		;043e	28 0e		( .
l0440h:
	push af			;0440	f5		.
	or 0c0h			;0441	f6 c0		. .
	call sub_040ah		;0443	cd 0a 04	. . .
	pop af			;0446	f1		.
	ld a,b			;0447	78		x
	res 0,a			;0448	cb 87		. .
	jr nc,l044eh		;044a	30 02		0 .
	set 0,a			;044c	cb c7		. .
l044eh:
	call sub_040ah		;044e	cd 0a 04	. . .
l0451h:
	ld a,b			;0451	78		x
	or c			;0452	b1		.
	jr nz,l0457h		;0453	20 02		  .
	pop af			;0455	f1		.
	ret			;0456	c9		.
l0457h:
	pop af			;0457	f1		.
	call sub_03ddh		;0458	cd dd 03	. . .
	call ReadFile		;045b	cd 73 04	. s .
	push bc			;045e	c5		.
	ld bc,l0000h		;045f	01 00 00	. . .
	call l03e8h		;0462	cd e8 03	. . .
	pop bc			;0465	c1		.
	push af			;0466	f5		.
	jr l0451h		;0467	18 e8		. .

; ReadRecord, then ReadFile.
LoadRecord:
	call ReadRecord		;0469	cd c1 03	. . .
	ld (00287h),hl		;046c	22 87 02	" . .
	ld (0028ah),bc		;046f	ed 43 8a 02	. C . .

; Read BC bytes from the record's file position to HL. A part that would land on
; #3DFF-#3E1F (the depacker and trampolines) goes through the copy at #4246.
ReadFile:
	push hl			;0473	e5		.
	add hl,bc		;0474	09		.
	ld (00488h),hl		;0475	22 88 04	" . .
	pop hl			;0478	e1		.
	jr nc,l048bh		;0479	30 10		0 .
	push af			;047b	f5		.
	ld a,l			;047c	7d		}
	cpl			;047d	2f		/
	ld c,a			;047e	4f		O
	ld a,h			;047f	7c		|
	cpl			;0480	2f		/
	ld b,a			;0481	47		G
	inc bc			;0482	03		.
	pop af			;0483	f1		.
	call l048bh		;0484	cd 8b 04	. . .
	ld bc,l0000h		;0487	01 00 00	. . .
	ret			;048a	c9		.
l048bh:
	ld (00460h),bc		;048b	ed 43 60 04	. C ` .
l048fh:
	ld (ReadChunk+1),a	;048f	32 ec 04	2 . .
	ld (004eeh),de		;0492	ed 53 ee 04	. S . .
	ex de,hl		;0496	eb		.
	ld hl,03dffh		;0497	21 ff 3d	! . =
	and a			;049a	a7		.
	sbc hl,de		;049b	ed 52		. R
	jr c,l04adh		;049d	38 0e		8 .
	inc hl			;049f	23		#
	ld (sub_04e8h+1),hl	;04a0	22 e9 04	" . .
	sbc hl,bc		;04a3	ed 42		. B
	ex de,hl		;04a5	eb		.
	jr nc,ReadChunk		;04a6	30 43		0 C
	call sub_04d7h		;04a8	cd d7 04	. . .
	jr l048fh		;04ab	18 e2		. .
l04adh:
	ld hl,03e1fh		;04ad	21 1f 3e	! . >
	and a			;04b0	a7		.
	sbc hl,de		;04b1	ed 52		. R
	jr c,l04d4h		;04b3	38 1f		8 .
	inc hl			;04b5	23		#
	ld (sub_04e8h+1),hl	;04b6	22 e9 04	" . .
	sbc hl,bc		;04b9	ed 42		. B
	push af			;04bb	f5		.
	push de			;04bc	d5		.
	ex de,hl		;04bd	eb		.
	ld de,03e00h		;04be	11 00 3e	. . >
	and a			;04c1	a7		.
	sbc hl,de		;04c2	ed 52		. R
	ld de,04246h		;04c4	11 46 42	. F B
	add hl,de		;04c7	19		.
	pop de			;04c8	d1		.
	pop af			;04c9	f1		.
	jr nc,ReadChunk		;04ca	30 1f		0 .
	call sub_04d7h		;04cc	cd d7 04	. . .
	ld hl,03e20h		;04cf	21 20 3e	!   >
	jr l048fh		;04d2	18 bb		. .
l04d4h:
	ex de,hl		;04d4	eb		.
	jr ReadChunk		;04d5	18 14		. .
sub_04d7h:
	ex de,hl		;04d7	eb		.
	ld h,b			;04d8	60		`
	ld l,c			;04d9	69		i
	ld bc,(sub_04e8h+1)	;04da	ed 4b e9 04	. K . .
	and a			;04de	a7		.
	sbc hl,bc		;04df	ed 42		. B
	push hl			;04e1	e5		.
	ex de,hl		;04e2	eb		.
	call sub_04e8h		;04e3	cd e8 04	. . .
	pop bc			;04e6	c1		.
	ret			;04e7	c9		.
sub_04e8h:
	ld bc,l0000h		;04e8	01 00 00	. . .

; Bit 7 of D = this chunk is XORed with the key at #05C6 (cycled to its NUL)
; after the read. No record of this file sets it.
ReadChunk:
	ld a,000h		;04eb	3e 00		> .
	ld de,l0000h		;04ed	11 00 00	. . .
	bit 7,d			;04f0	cb 7a		. z
	jr z,ReadSectors	;04f2	28 37		( 7
	res 7,d			;04f4	cb ba		. .
	ld (0050ah),hl		;04f6	22 0a 05	" . .
	ld (0050dh),bc		;04f9	ed 43 0d 05	. C . .
	call ReadSectors	;04fd	cd 2b 05	. + .
	set 7,d			;0500	cb fa		. .
	push hl			;0502	e5		.
	push de			;0503	d5		.
	push bc			;0504	c5		.
	push af			;0505	f5		.
	ld de,XorKey		;0506	11 c6 05	. . .
	ld hl,l0000h		;0509	21 00 00	! . .
	ld bc,l0000h		;050c	01 00 00	. . .
l050fh:
	ld a,b			;050f	78		x
	or c			;0510	b1		.
	jr z,l0522h		;0511	28 0f		( .
	ld a,(de)		;0513	1a		.
	or a			;0514	b7		.
	jr nz,l051bh		;0515	20 04		  .
	ld de,XorKey		;0517	11 c6 05	. . .
	ld a,(de)		;051a	1a		.
l051bh:
	xor (hl)		;051b	ae		.
	ld (hl),a		;051c	77		w
	inc de			;051d	13		.
	inc hl			;051e	23		#
	dec bc			;051f	0b		.
	jr l050fh		;0520	18 ed		. .
l0522h:
	ld (00507h),de		;0522	ed 53 07 05	. S . .
	pop af			;0526	f1		.
	pop bc			;0527	c1		.
	pop de			;0528	d1		.
	pop hl			;0529	e1		.
	ret			;052a	c9		.

; A = byte offset in the first sector, DE = sector - 1; partial first and last
; sectors go through the 256-byte buffer #4146.
ReadSectors:
	or a			;052b	b7		.
	jr z,l054fh		;052c	28 21		( !
	push de			;052e	d5		.
	push bc			;052f	c5		.
	push hl			;0530	e5		.
	push af			;0531	f5		.
	ld hl,04146h		;0532	21 46 41	! F A
	ld b,001h		;0535	06 01		. .
	call DssReadSectors	;0537	cd 76 05	. v .
	pop af			;053a	f1		.
	ld l,a			;053b	6f		o
	ld h,000h		;053c	26 00		& .
	ld de,04146h		;053e	11 46 41	. F A
	add hl,de		;0541	19		.
	pop de			;0542	d1		.
	pop bc			;0543	c1		.
l0544h:
	ldi			;0544	ed a0		. .
	jp po,l056fh		;0546	e2 6f 05	. o .
	inc a			;0549	3c		<
	jr nz,l0544h		;054a	20 f8		  .
	ex de,hl		;054c	eb		.
	pop de			;054d	d1		.
	inc de			;054e	13		.
l054fh:
	push bc			;054f	c5		.
	call DssReadSectors	;0550	cd 76 05	. v .
	pop bc			;0553	c1		.
	ld b,000h		;0554	06 00		. .
	ld a,c			;0556	79		y
	or a			;0557	b7		.
	ret z			;0558	c8		.
	push de			;0559	d5		.
	push hl			;055a	e5		.
	push bc			;055b	c5		.
	ld hl,04146h		;055c	21 46 41	! F A
	ld b,001h		;055f	06 01		. .
	call DssReadSectors	;0561	cd 76 05	. v .
	pop bc			;0564	c1		.
	pop de			;0565	d1		.
	ld a,c			;0566	79		y
	ld hl,04146h		;0567	21 46 41	! F A
	ldir			;056a	ed b0		. .
	ex de,hl		;056c	eb		.
	pop de			;056d	d1		.
	ret			;056e	c9		.
l056fh:
	ex de,hl		;056f	eb		.
	pop de			;0570	d1		.
	inc de			;0571	13		.
	inc a			;0572	3c		<
	ret z			;0573	c8		.
	dec de			;0574	1b		.
	ret			;0575	c9		.

; DSS Move_FP (#15) to (DE+1)*256, then DSS Read (#13) of B sectors.
DssReadSectors:
	inc b			;0576	04		.
	dec b			;0577	05		.
	ret z			;0578	c8		.
	push de			;0579	d5		.
	push hl			;057a	e5		.
	push bc			;057b	c5		.
	push bc			;057c	c5		.
	push hl			;057d	e5		.
	inc de			;057e	13		.
	defb 0ddh,02eh,000h ;ld ixl,000h	;057f	dd 2e 00	. . .
	defb 0ddh,063h ;ld ixh,e	;0582	dd 63		. c
	ld l,d			;0584	6a		j
	ld h,000h		;0585	26 00		& .
	ld bc,l0015h		;0587	01 15 00	. . .
	ld a,(04105h)		;058a	3a 05 41	: . A
	rst 10h			;058d	d7		.
	pop hl			;058e	e1		.
	pop de			;058f	d1		.
	ld e,000h		;0590	1e 00		. .
	ld c,013h		;0592	0e 13		. .
	ld a,(04105h)		;0594	3a 05 41	: . A
	rst 10h			;0597	d7		.
	pop bc			;0598	c1		.
	pop hl			;0599	e1		.
	ld c,000h		;059a	0e 00		. .
	add hl,bc		;059c	09		.
	ld a,b			;059d	78		x
	pop de			;059e	d1		.
	add a,e			;059f	83		.
	ld e,a			;05a0	5f		_
	ret nc			;05a1	d0		.
	inc d			;05a2	14		.
	ret			;05a3	c9		.

; 32 bytes kept by UnpackInPlace.
SaveArea:

; BLOCK 'd05a4' (start 0x05a4 end 0x05e6)
d05a4_start:
	defb 000h		;05a4	00		.
	defb 000h		;05a5	00		.
	defb 000h		;05a6	00		.
	defb 000h		;05a7	00		.
	defb 000h		;05a8	00		.
	defb 000h		;05a9	00		.
	defb 000h		;05aa	00		.
	defb 000h		;05ab	00		.
	defb 000h		;05ac	00		.
	defb 000h		;05ad	00		.
	defb 000h		;05ae	00		.
	defb 000h		;05af	00		.
	defb 000h		;05b0	00		.
	defb 000h		;05b1	00		.
	defb 000h		;05b2	00		.
	defb 000h		;05b3	00		.
	defb 000h		;05b4	00		.
	defb 000h		;05b5	00		.
	defb 000h		;05b6	00		.
	defb 000h		;05b7	00		.
	defb 000h		;05b8	00		.
	defb 000h		;05b9	00		.
	defb 000h		;05ba	00		.
	defb 000h		;05bb	00		.
	defb 000h		;05bc	00		.
	defb 000h		;05bd	00		.
	defb 000h		;05be	00		.
	defb 000h		;05bf	00		.
	defb 000h		;05c0	00		.
	defb 000h		;05c1	00		.
	defb 000h		;05c2	00		.
l05c3h:
	defb 000h		;05c3	00		.

; #FFFF: OpKeyCheck compares against it.
KeyWord:
	defb 0ffh		;05c4	ff		.
l05c5h:
	defb 0ffh		;05c5	ff		.

; The XOR key (zero-terminated; empty in this file).
XorKey:
	defb 000h		;05c6	00		.
	defb 000h		;05c7	00		.
	defb 000h		;05c8	00		.
	defb 000h		;05c9	00		.
	defb 000h		;05ca	00		.
	defb 000h		;05cb	00		.
	defb 000h		;05cc	00		.
	defb 000h		;05cd	00		.
	defb 000h		;05ce	00		.
	defb 000h		;05cf	00		.
	defb 000h		;05d0	00		.
	defb 000h		;05d1	00		.
	defb 000h		;05d2	00		.
	defb 000h		;05d3	00		.
	defb 000h		;05d4	00		.
	defb 000h		;05d5	00		.
	defb 000h		;05d6	00		.
	defb 000h		;05d7	00		.
	defb 000h		;05d8	00		.
	defb 000h		;05d9	00		.
	defb 000h		;05da	00		.
	defb 000h		;05db	00		.
	defb 000h		;05dc	00		.
	defb 000h		;05dd	00		.
	defb 000h		;05de	00		.
	defb 000h		;05df	00		.
	defb 000h		;05e0	00		.
	defb 000h		;05e1	00		.
	defb 000h		;05e2	00		.
	defb 000h		;05e3	00		.
	defb 000h		;05e4	00		.
	defb 000h		;05e5	00		.

; Copied to #3DDF: map the DSS page into window 0, run its RST, map stage 2 back.
Trampolines:
	push hl			;05e6	e5		.
	ld hl,Rst08		;05e7	21 08 00	! . .
	jr l05f0h		;05ea	18 04		. .
	push hl			;05ec	e5		.
	ld hl,Rst10		;05ed	21 10 00	! . .
l05f0h:
	push af			;05f0	f5		.
	ld a,000h		;05f1	3e 00		> .
	out (082h),a		;05f3	d3 82		. .
	pop af			;05f5	f1		.
	ld (03df7h),hl		;05f6	22 f7 3d	" . =
	ld hl,03df9h		;05f9	21 f9 3d	! . =
	ex (sp),hl		;05fc	e3		.
	jp l0000h		;05fd	c3 00 00	. . .
	push af			;0600	f5		.
	ld a,000h		;0601	3e 00		> .
	out (082h),a		;0603	d3 82		. .
	pop af			;0605	f1		.
	ret			;0606	c9		.
