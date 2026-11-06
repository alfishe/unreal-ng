; prosiak.exe (Wild Sound XM player, Sprinter). Rebuilt by tools/make_listings.py from the
; file of the MAME pack's system disk (DEMOS/WILDSND); z80dasm 1.2.0 with -a -l -t.
; Stage 3: block 0, the Wild Sound player, #4500-#4DD4
; z80dasm 1.2.0

	org 04500h


; Script op `24 00 45`. Prints "XM Init ... OK", probes ISA slot 0, then slot 1.
; Neither: AEN on (BusIdle) and VmExit, so the loader prints "Done ...".
PlayerInit:
	ld hl,l4819h		;4500	21 19 48	! . H
	call 00121h		;4503	cd 21 01	. ! .
	xor a			;4506	af		.
	call ProbeSlot		;4507	cd 19 45	. . E
	jr z,l4518h		;450a	28 0c		( .
	inc a			;450c	3c		<
	call ProbeSlot		;450d	cd 19 45	. . E
	jr z,l4518h		;4510	28 06		( .
	call BusIdle		;4512	cd 1e 4a	. . J
	jp 0011ch		;4515	c3 1c 01	. . .
l4518h:
	ret			;4518	c9		.

; A = slot: prints "Wild Sound ... ISA #n ", selects the slot, Identify; prints the
; card's string or "Not Found". Z = found.
ProbeSlot:
	push hl			;4519	e5		.
	push af			;451a	f5		.
	push af			;451b	f5		.
	ld hl,d47fa_start	;451c	21 fa 47	! . G
	call 00121h		;451f	cd 21 01	. ! .
	pop af			;4522	f1		.
	push af			;4523	f5		.
	ld l,a			;4524	6f		o
	ld h,000h		;4525	26 00		& .
	call 00158h		;4527	cd 58 01	. X .
	ld a,020h		;452a	3e 20		>  
	call 0012ch		;452c	cd 2c 01	. , .
	pop af			;452f	f1		.
	call SetSlot		;4530	cd dc 49	. . I
	call SelectIsaIo	;4533	cd f2 49	. . I
	call d4c47_end		;4536	cd 47 4d	. G M
	push af			;4539	f5		.
	ld hl,d4d55_start	;453a	21 55 4d	! U M
	jr z,l4542h		;453d	28 03		( .
	ld hl,l480fh		;453f	21 0f 48	! . H
l4542h:
	call 00121h		;4542	cd 21 01	. ! .
	call 001c2h		;4545	cd c2 01	. . .
	pop af			;4548	f1		.
	pop hl			;4549	e1		.
	ld a,h			;454a	7c		|
	pop hl			;454b	e1		.
	ret			;454c	c9		.

; BLOCK 'd454d' (start 0x454d end 0x455d)
d454d_start:
	defb 000h		;454d	00		.
l454eh:
	defb 000h		;454e	00		.
	defb 000h		;454f	00		.
	defb 000h		;4550	00		.
	defb 000h		;4551	00		.
	defb 000h		;4552	00		.
	defb 000h		;4553	00		.
	defb 000h		;4554	00		.
	defb 000h		;4555	00		.
	defb 000h		;4556	00		.
	defb 000h		;4557	00		.
	defb 000h		;4558	00		.
	defb 000h		;4559	00		.
	defb 000h		;455a	00		.
	defb 000h		;455b	00		.
	defb 000h		;455c	00		.

; DE to ISA port 1 (data).
WriteWord:
	push hl			;455d	e5		.
	push de			;455e	d5		.
	push bc			;455f	c5		.
	ld hl,d454d_start	;4560	21 4d 45	! M E
	ld (d454d_start),de	;4563	ed 53 4d 45	. S M E
	ld de,00001h		;4567	11 01 00	. . .
	ld bc,00002h		;456a	01 02 00	. . .
	call IsaOutBlock	;456d	cd 6b 4a	. k J
	pop bc			;4570	c1		.
	pop de			;4571	d1		.
	pop hl			;4572	e1		.
	ret			;4573	c9		.
	push hl			;4574	e5		.
	push de			;4575	d5		.
	push bc			;4576	c5		.
	ld hl,d454d_start	;4577	21 4d 45	! M E
	ld (hl),a		;457a	77		w
	ld (l454eh),de		;457b	ed 53 4e 45	. S N E
	ld de,00001h		;457f	11 01 00	. . .
	ld bc,00003h		;4582	01 03 00	. . .
	call IsaOutBlock	;4585	cd 6b 4a	. k J
	pop bc			;4588	c1		.
	pop de			;4589	d1		.
	pop hl			;458a	e1		.
	ret			;458b	c9		.

; B bytes from ISA port 1 to HL.
ReadData:
	push de			;458c	d5		.
	push bc			;458d	c5		.
	ld c,b			;458e	48		H
	ld b,000h		;458f	06 00		. .
	ld de,00001h		;4591	11 01 00	. . .
	call IsaInBlock		;4594	cd 7d 4b	. } K
	pop bc			;4597	c1		.
	pop de			;4598	d1		.
	ret			;4599	c9		.

; B bytes from HL to ISA port 1.
WriteData:
	push de			;459a	d5		.
	push bc			;459b	c5		.
	ld c,b			;459c	48		H
	ld b,000h		;459d	06 00		. .
	ld de,00001h		;459f	11 01 00	. . .
	call IsaOutBlock	;45a2	cd 6b 4a	. k J
	pop bc			;45a5	c1		.
	pop de			;45a6	d1		.
	ret			;45a7	c9		.
sub_45a8h:
	push de			;45a8	d5		.
	push bc			;45a9	c5		.
	ld c,a			;45aa	4f		O
	ld b,e			;45ab	43		C
	ld de,00000h		;45ac	11 00 00	. . .
	call IsaOutWord		;45af	cd 46 4a	. F J
	pop bc			;45b2	c1		.
	pop de			;45b3	d1		.
	ret			;45b4	c9		.

; Command #F1 to port 0, then the word A (D = 0) to port 1: select a function.
CmdF1:
	push de			;45b5	d5		.
	push af			;45b6	f5		.
	ld a,0f1h		;45b7	3e f1		> .
	call OutCmd		;45b9	cd c3 4d	. . M
	pop de			;45bc	d1		.
	ld e,d			;45bd	5a		Z
	ld d,000h		;45be	16 00		. .
	call d454d_end		;45c0	cd 5d 45	. ] E
	pop de			;45c3	d1		.
	ret			;45c4	c9		.

; Command #F2, count B, then B data bytes: one data packet.
CmdF2:
	ld e,b			;45c5	58		X
	ld a,0f2h		;45c6	3e f2		> .
	call sub_45a8h		;45c8	cd a8 45	. . E
	inc b			;45cb	04		.
	dec b			;45cc	05		.
	ret z			;45cd	c8		.
	call WriteData		;45ce	cd 9a 45	. . E
	ret			;45d1	c9		.
sub_45d2h:
	push bc			;45d2	c5		.
	ld b,000h		;45d3	06 00		. .
	call CmdF2		;45d5	cd c5 45	. . E
	pop bc			;45d8	c1		.
	call sub_467fh		;45d9	cd 7f 46	. . F
	ret			;45dc	c9		.

; Rolling add / xor / sub over the status frame.
Checksum:
	ld de,00000h		;45dd	11 00 00	. . .
l45e0h:
	ld a,(hl)		;45e0	7e		~
	inc hl			;45e1	23		#
	bit 0,e			;45e2	cb 43		. C
	jr nz,l45f3h		;45e4	20 0d		  .
	add a,e			;45e6	83		.
	bit 1,e			;45e7	cb 4b		. K
	jr nz,l45eeh		;45e9	20 03		  .
	xor d			;45eb	aa		.
	jr l45fch		;45ec	18 0e		. .
l45eeh:
	ld c,a			;45ee	4f		O
	ld a,d			;45ef	7a		z
	sub c			;45f0	91		.
	jr l45fch		;45f1	18 09		. .
l45f3h:
	sub e			;45f3	93		.
	bit 1,e			;45f4	cb 4b		. K
	jr nz,l45fbh		;45f6	20 03		  .
	xor d			;45f8	aa		.
	jr l45fch		;45f9	18 01		. .
l45fbh:
	add a,d			;45fb	82		.
l45fch:
	ld d,a			;45fc	57		W
	inc e			;45fd	1c		.
	djnz l45e0h		;45fe	10 e0		. .
	ret			;4600	c9		.

; Command #F3, then 32 bytes from port 1 to #4DD5; valid when bytes 29 / 30 are
; #FA / #3D and the checksum matches. The player waits here for the card.
ReadStatus:
	push de			;4601	d5		.
	push hl			;4602	e5		.
	push bc			;4603	c5		.
	ld a,0f3h		;4604	3e f3		> .
	call OutCmd		;4606	cd c3 4d	. . M
	ld b,001h		;4609	06 01		. .
l460bh:
	djnz l460bh		;460b	10 fe		. .
	ld b,020h		;460d	06 20		.  
	ld hl,04dd5h		;460f	21 d5 4d	! . M
	push hl			;4612	e5		.
	call ReadData		;4613	cd 8c 45	. . E
	dec hl			;4616	2b		+
	dec hl			;4617	2b		+
	ld a,(hl)		;4618	7e		~
	xor 03dh		;4619	ee 3d		. =
	ld c,a			;461b	4f		O
	dec hl			;461c	2b		+
	ld a,(hl)		;461d	7e		~
	xor 0fah		;461e	ee fa		. .
	or c			;4620	b1		.
	pop hl			;4621	e1		.
	jr nz,l4629h		;4622	20 05		  .
	dec b			;4624	05		.
	call Checksum		;4625	cd dd 45	. . E
	xor (hl)		;4628	ae		.
l4629h:
	pop bc			;4629	c1		.
	pop hl			;462a	e1		.
	pop de			;462b	d1		.
	ret nz			;462c	c0		.
	ld a,(04dd6h)		;462d	3a d6 4d	: . M
	rlca			;4630	07		.
	ret			;4631	c9		.
sub_4632h:
	push hl			;4632	e5		.
	ld hl,02710h		;4633	21 10 27	! . '
	call sub_4645h		;4636	cd 45 46	. E F
	pop hl			;4639	e1		.
	ret			;463a	c9		.

; Border colour (port #FE): LoadInsHeaders 5, LoadSamples 4, Play 0 (6 at #4784, not called).
SetBorder:
	ld (04640h),a		;463b	32 40 46	2 @ F
l463eh:
	push af			;463e	f5		.
	ld a,000h		;463f	3e 00		> .
	out (0feh),a		;4641	d3 fe		. .
	pop af			;4643	f1		.
	ret			;4644	c9		.
sub_4645h:
	ld (0464eh),hl		;4645	22 4e 46	" N F
	ret			;4648	c9		.
sub_4649h:
	push hl			;4649	e5		.
	ld (l4658h+1),a		;464a	32 59 46	2 Y F
	ld hl,00000h		;464d	21 00 00	! . .
	ld a,h			;4650	7c		|
	or l			;4651	b5		.
	jr z,l4658h		;4652	28 04		( .
	dec hl			;4654	2b		+
	ld (0464eh),hl		;4655	22 4e 46	" N F
l4658h:
	ld a,000h		;4658	3e 00		> .
	pop hl			;465a	e1		.
	ret			;465b	c9		.

; Loops on ReadStatus, flashing the border.
WaitReady:
	call ReadStatus		;465c	cd 01 46	. . F
	jp z,l463eh		;465f	ca 3e 46	. > F
l4662h:
	call sub_4632h		;4662	cd 32 46	. 2 F
l4665h:
	call ReadStatus		;4665	cd 01 46	. . F
	jr z,WaitReady		;4668	28 f2		( .
	call sub_4649h		;466a	cd 49 46	. I F
	jr nz,l4665h		;466d	20 f6		  .
	ld a,001h		;466f	3e 01		> .
	xor 001h		;4671	ee 01		. .
	ld (04670h),a		;4673	32 70 46	2 p F
	ld a,002h		;4676	3e 02		> .
	out (0feh),a		;4678	d3 fe		. .
	call z,l463eh		;467a	cc 3e 46	. > F
	jr l4662h		;467d	18 e3		. .
sub_467fh:
	call WaitReady		;467f	cd 5c 46	. \ F
	jr c,sub_467fh		;4682	38 fb		8 .
	ret			;4684	c9		.
sub_4685h:
	xor a			;4685	af		.

; Sends DE bytes as #F2 packets, each no longer than the card's buffer size from
; the status frame (#4DDF).
UploadBlock:
	push bc			;4686	c5		.
	push af			;4687	f5		.
	call sub_467fh		;4688	cd 7f 46	. . F
l468bh:
	ld a,(04ddfh)		;468b	3a df 4d	: . M
	push de			;468e	d5		.
	inc d			;468f	14		.
	dec d			;4690	15		.
	jr nz,l4697h		;4691	20 04		  .
	cp e			;4693	bb		.
	jr c,l4697h		;4694	38 01		8 .
	ld a,e			;4696	7b		{
l4697h:
	ld b,a			;4697	47		G
	push bc			;4698	c5		.
	call CmdF2		;4699	cd c5 45	. . E
	call sub_467fh		;469c	cd 7f 46	. . F
	pop bc			;469f	c1		.
	pop de			;46a0	d1		.
	ld c,b			;46a1	48		H
	ld b,000h		;46a2	06 00		. .
	ex de,hl		;46a4	eb		.
	and a			;46a5	a7		.
	sbc hl,bc		;46a6	ed 42		. B
	ex de,hl		;46a8	eb		.
	jr nz,l468bh		;46a9	20 e0		  .
	pop af			;46ab	f1		.
	or a			;46ac	b7		.
	call z,sub_45d2h	;46ad	cc d2 45	. . E
	pop bc			;46b0	c1		.
	ret			;46b1	c9		.
sub_46b2h:
	call CmdF1		;46b2	cd b5 45	. . E
	ld hl,00000h		;46b5	21 00 00	! . .
	ld de,00000h		;46b8	11 00 00	. . .
	call d454d_end		;46bb	cd 5d 45	. ] E
	push de			;46be	d5		.
	ld de,00000h		;46bf	11 00 00	. . .
	call d454d_end		;46c2	cd 5d 45	. ] E
	pop de			;46c5	d1		.
	ret			;46c6	c9		.
sub_46c7h:
	call sub_46b2h		;46c7	cd b2 46	. . F
	call sub_4685h		;46ca	cd 85 46	. . F
	ret			;46cd	c9		.
sub_46ceh:
	ld (046b6h),hl		;46ce	22 b6 46	" . F
	ld (046b9h),bc		;46d1	ed 43 b9 46	. C . F
	ret			;46d5	c9		.
sub_46d6h:
	ld a,000h		;46d6	3e 00		> .
	call CmdF1		;46d8	cd b5 45	. . E
	call sub_45d2h		;46db	cd d2 45	. . E
	ld a,001h		;46de	3e 01		> .
	call CmdF1		;46e0	cd b5 45	. . E
	call sub_45d2h		;46e3	cd d2 45	. . E
	ret			;46e6	c9		.
sub_46e7h:
	call sub_46d6h		;46e7	cd d6 46	. . F
	ld a,002h		;46ea	3e 02		> .
	call CmdF1		;46ec	cd b5 45	. . E
	call sub_45d2h		;46ef	cd d2 45	. . E
	ret			;46f2	c9		.
l46f3h:
	nop			;46f3	00		.
	nop			;46f4	00		.
l46f5h:
	nop			;46f5	00		.
	nop			;46f6	00		.
sub_46f7h:
	push hl			;46f7	e5		.
	push bc			;46f8	c5		.
	ld hl,(l46f3h)		;46f9	2a f3 46	* . F
	sbc hl,bc		;46fc	ed 42		. B
	ld (l46f3h),hl		;46fe	22 f3 46	" . F
	ld bc,00000h		;4701	01 00 00	. . .
	ld hl,(l46f5h)		;4704	2a f5 46	* . F
	sbc hl,bc		;4707	ed 42		. B
	ld (l46f5h),hl		;4709	22 f5 46	" . F
	pop bc			;470c	c1		.
	pop hl			;470d	e1		.
	ret			;470e	c9		.
sub_470fh:
	push hl			;470f	e5		.
	ld (0471fh),a		;4710	32 1f 47	2 . G
	ld hl,(l46f3h)		;4713	2a f3 46	* . F
	ld a,l			;4716	7d		}
	or h			;4717	b4		.
	ld hl,(l46f5h)		;4718	2a f5 46	* . F
	or l			;471b	b5		.
	or h			;471c	b4		.
	pop hl			;471d	e1		.
	ld a,000h		;471e	3e 00		> .
	ret			;4720	c9		.
sub_4721h:
	ld hl,(046b6h)		;4721	2a b6 46	* . F
	ld e,(hl)		;4724	5e		^
	inc hl			;4725	23		#
	ld d,(hl)		;4726	56		V
	inc hl			;4727	23		#
	ld c,(hl)		;4728	4e		N
	inc hl			;4729	23		#
	ld b,(hl)		;472a	46		F
	inc hl			;472b	23		#
	ld (046b6h),hl		;472c	22 b6 46	" . F
	ld hl,(046b9h)		;472f	2a b9 46	* . F
	dec hl			;4732	2b		+
	dec hl			;4733	2b		+
	dec hl			;4734	2b		+
	dec hl			;4735	2b		+
	ld (046b9h),hl		;4736	22 b9 46	" . F
	ld (l46f3h),de		;4739	ed 53 f3 46	. S . F
	ld (l46f5h),bc		;473d	ed 43 f5 46	. C . F
	push bc			;4741	c5		.
	push de			;4742	d5		.
	call sub_46e7h		;4743	cd e7 46	. . F
	ld a,010h		;4746	3e 10		> .
	call CmdF1		;4748	cd b5 45	. . E
	pop de			;474b	d1		.
	call d454d_end		;474c	cd 5d 45	. ] E
	pop de			;474f	d1		.
	call d454d_end		;4750	cd 5d 45	. ] E
	ret			;4753	c9		.
l4754h:
	nop			;4754	00		.

; Script op `24 55 47` after block 1: first call #F1 #00, #01, #02, then #F1 #10
; and the data; "XM TRK Data".
LoadTrack:
	di			;4755	f3		.
	call sub_46ceh		;4756	cd ce 46	. . F
	ld a,(l4754h)		;4759	3a 54 47	: T G
	or a			;475c	b7		.
	call z,sub_4721h	;475d	cc 21 47	. ! G
	ld hl,(046b6h)		;4760	2a b6 46	* . F
	ld de,(046b9h)		;4763	ed 5b b9 46	. [ . F
	ld a,001h		;4767	3e 01		> .
	call UploadBlock	;4769	cd 86 46	. . F
	ld bc,(046b9h)		;476c	ed 4b b9 46	. K . F
	call sub_46f7h		;4770	cd f7 46	. . F
	call sub_470fh		;4773	cd 0f 47	. . G
	call z,sub_45d2h	;4776	cc d2 45	. . E
	ld hl,l4754h		;4779	21 54 47	! T G
	inc (hl)		;477c	34		4
	ld hl,l482ah		;477d	21 2a 48	! * H
	call 00121h		;4780	cd 21 01	. ! .
	ret			;4783	c9		.
	di			;4784	f3		.
	call sub_46ceh		;4785	cd ce 46	. . F
	ld a,006h		;4788	3e 06		> .
	call SetBorder		;478a	cd 3b 46	. ; F
	call sub_46e7h		;478d	cd e7 46	. . F
	ld a,010h		;4790	3e 10		> .
	call sub_46c7h		;4792	cd c7 46	. . F
	ret			;4795	c9		.

; Script op `24 96 47` after block 2: #F1 #20 and the data; "XM INS HDR".
LoadInsHeaders:
	di			;4796	f3		.
	call sub_46ceh		;4797	cd ce 46	. . F
	ld a,005h		;479a	3e 05		> .
	call SetBorder		;479c	cd 3b 46	. ; F
	ld a,020h		;479f	3e 20		>  
	call sub_46c7h		;47a1	cd c7 46	. . F
	ld hl,l4841h		;47a4	21 41 48	! A H
	call 00121h		;47a7	cd 21 01	. ! .
	ret			;47aa	c9		.

; Script op `24 AB 47` after blocks 3-7: #F1 #21 and the data; "XM INS".
LoadSamples:
	di			;47ab	f3		.
	call sub_46ceh		;47ac	cd ce 46	. . F
	ld a,004h		;47af	3e 04		> .
	call SetBorder		;47b1	cd 3b 46	. ; F
	ld a,021h		;47b4	3e 21		> !
	call sub_46c7h		;47b6	cd c7 46	. . F
	ld hl,l4838h		;47b9	21 38 48	! 8 H
	call 00121h		;47bc	cd 21 01	. ! .
	ret			;47bf	c9		.

; Script op `24 C0 47`: #F1 #30, "XM PLAY", wait for a key, #F1 #00 and #01,
; AEN on. The card plays; the Z80 only waits.
Play:
	di			;47c0	f3		.
	ld a,000h		;47c1	3e 00		> .
	call SetBorder		;47c3	cd 3b 46	. ; F
	ld a,030h		;47c6	3e 30		> 0
	call CmdF1		;47c8	cd b5 45	. . E
	call sub_45d2h		;47cb	cd d2 45	. . E
	ld hl,l484eh		;47ce	21 4e 48	! N H
	call 00121h		;47d1	cd 21 01	. ! .
	call sub_47deh		;47d4	cd de 47	. . G
	call sub_46d6h		;47d7	cd d6 46	. . F
	call BusIdle		;47da	cd 1e 4a	. . J
	ret			;47dd	c9		.
sub_47deh:
	call sub_4934h		;47de	cd 34 49	. 4 I
l47e1h:
	halt			;47e1	76		v
	call sub_488ah		;47e2	cd 8a 48	. . H
	ld a,(l4889h)		;47e5	3a 89 48	: . H
	or a			;47e8	b7		.
	jr z,l47e1h		;47e9	28 f6		( .
	call sub_4946h		;47eb	cd 46 49	. F I
	ret			;47ee	c9		.
sub_47efh:
	ld a,000h		;47ef	3e 00		> .
	inc a			;47f1	3c		<
	ld (sub_47efh+1),a	;47f2	32 f0 47	2 . G
	and 007h		;47f5	e6 07		. .
	out (0feh),a		;47f7	d3 fe		. .
	ret			;47f9	c9		.

; BLOCK 'd47fa' (start 0x47fa end 0x4858)
d47fa_start:
	defb 057h		;47fa	57		W
	defb 069h		;47fb	69		i
	defb 06ch		;47fc	6c		l
	defb 064h		;47fd	64		d
	defb 020h		;47fe	20		 
	defb 053h		;47ff	53		S
	defb 06fh		;4800	6f		o
	defb 075h		;4801	75		u
	defb 06eh		;4802	6e		n
	defb 064h		;4803	64		d
	defb 020h		;4804	20		 
	defb 02eh		;4805	2e		.
	defb 02eh		;4806	2e		.
	defb 02eh		;4807	2e		.
	defb 020h		;4808	20		 
	defb 049h		;4809	49		I
	defb 053h		;480a	53		S
	defb 041h		;480b	41		A
	defb 020h		;480c	20		 
	defb 023h		;480d	23		#
	defb 000h		;480e	00		.
l480fh:
	defb 04eh		;480f	4e		N
	defb 06fh		;4810	6f		o
	defb 074h		;4811	74		t
	defb 020h		;4812	20		 
	defb 046h		;4813	46		F
	defb 06fh		;4814	6f		o
	defb 075h		;4815	75		u
	defb 06eh		;4816	6e		n
	defb 064h		;4817	64		d
	defb 000h		;4818	00		.
l4819h:
	defb 058h		;4819	58		X
	defb 04dh		;481a	4d		M
	defb 020h		;481b	20		 
	defb 049h		;481c	49		I
	defb 06eh		;481d	6e		n
	defb 069h		;481e	69		i
	defb 074h		;481f	74		t
	defb 020h		;4820	20		 
	defb 02eh		;4821	2e		.
	defb 02eh		;4822	2e		.
	defb 02eh		;4823	2e		.
	defb 020h		;4824	20		 
	defb 04fh		;4825	4f		O
	defb 04bh		;4826	4b		K
	defb 00dh		;4827	0d		.
	defb 00ah		;4828	0a		.
	defb 000h		;4829	00		.
l482ah:
	defb 058h		;482a	58		X
	defb 04dh		;482b	4d		M
	defb 020h		;482c	20		 
	defb 054h		;482d	54		T
	defb 052h		;482e	52		R
	defb 04bh		;482f	4b		K
	defb 020h		;4830	20		 
	defb 044h		;4831	44		D
	defb 061h		;4832	61		a
	defb 074h		;4833	74		t
	defb 061h		;4834	61		a
	defb 00dh		;4835	0d		.
	defb 00ah		;4836	0a		.
	defb 000h		;4837	00		.
l4838h:
	defb 058h		;4838	58		X
	defb 04dh		;4839	4d		M
	defb 020h		;483a	20		 
	defb 049h		;483b	49		I
	defb 04eh		;483c	4e		N
	defb 053h		;483d	53		S
	defb 00dh		;483e	0d		.
	defb 00ah		;483f	0a		.
	defb 000h		;4840	00		.
l4841h:
	defb 058h		;4841	58		X
	defb 04dh		;4842	4d		M
	defb 020h		;4843	20		 
	defb 049h		;4844	49		I
	defb 04eh		;4845	4e		N
	defb 053h		;4846	53		S
	defb 020h		;4847	20		 
	defb 048h		;4848	48		H
	defb 044h		;4849	44		D
	defb 052h		;484a	52		R
	defb 00dh		;484b	0d		.
	defb 00ah		;484c	0a		.
	defb 000h		;484d	00		.
l484eh:
	defb 058h		;484e	58		X
	defb 04dh		;484f	4d		M
	defb 020h		;4850	20		 
	defb 050h		;4851	50		P
	defb 04ch		;4852	4c		L
	defb 041h		;4853	41		A
	defb 059h		;4854	59		Y
	defb 00dh		;4855	0d		.
	defb 00ah		;4856	0a		.
	defb 000h		;4857	00		.
	ld a,030h		;4858	3e 30		> 0
	out (019h),a		;485a	d3 19		. .
	ld a,018h		;485c	3e 18		> .
	out (019h),a		;485e	d3 19		. .
	ld a,000h		;4860	3e 00		> .
	out (019h),a		;4862	d3 19		. .
	ld a,038h		;4864	3e 38		> 8
	out (019h),a		;4866	d3 19		. .
	ld a,001h		;4868	3e 01		> .
	out (019h),a		;486a	d3 19		. .
	ld a,000h		;486c	3e 00		> .
	out (019h),a		;486e	d3 19		. .
	ld a,003h		;4870	3e 03		> .
	out (019h),a		;4872	d3 19		. .
	ld a,0c1h		;4874	3e c1		> .
	out (019h),a		;4876	d3 19		. .
	ld a,004h		;4878	3e 04		> .
	out (019h),a		;487a	d3 19		. .
	ld a,007h		;487c	3e 07		> .
	out (019h),a		;487e	d3 19		. .
	ld a,005h		;4880	3e 05		> .
	out (019h),a		;4882	d3 19		. .
	ld a,060h		;4884	3e 60		> `
	out (019h),a		;4886	d3 19		. .
	ret			;4888	c9		.
l4889h:
	nop			;4889	00		.
sub_488ah:
	call sub_488eh		;488a	cd 8e 48	. . H
	ret			;488d	c9		.
sub_488eh:
	push hl			;488e	e5		.
l488fh:
	ld hl,l4c43h		;488f	21 43 4c	! C L
	call sub_494bh		;4892	cd 4b 49	. K I
	jr z,l48a6h		;4895	28 0f		( .
	call sub_4978h		;4897	cd 78 49	. x I
	cp 076h			;489a	fe 76		. v
	jr nz,l48a1h		;489c	20 03		  .
	ld (l4889h),a		;489e	32 89 48	2 . H
l48a1h:
	call 00145h		;48a1	cd 45 01	. E .
	jr l488fh		;48a4	18 e9		. .
l48a6h:
	pop hl			;48a6	e1		.
	ret			;48a7	c9		.
l48a8h:
	push af			;48a8	f5		.
l48a9h:
	push hl			;48a9	e5		.
	ld hl,l4c43h		;48aa	21 43 4c	! C L
	in a,(018h)		;48ad	db 18		. .
	call sub_4957h		;48af	cd 57 49	. W I
	pop hl			;48b2	e1		.
	pop af			;48b3	f1		.
	ei			;48b4	fb		.
	ret			;48b5	c9		.

; BLOCK 'd48b6' (start 0x48b6 end 0x48ff)
d48b6_start:
	defb 000h		;48b6	00		.
	defb 000h		;48b7	00		.
	defb 000h		;48b8	00		.
	defb 000h		;48b9	00		.
	defb 000h		;48ba	00		.
	defb 000h		;48bb	00		.
	defb 000h		;48bc	00		.
	defb 000h		;48bd	00		.
	defb 000h		;48be	00		.
	defb 000h		;48bf	00		.
	defb 000h		;48c0	00		.
	defb 000h		;48c1	00		.
	defb 000h		;48c2	00		.
	defb 000h		;48c3	00		.
	defb 000h		;48c4	00		.
	defb 000h		;48c5	00		.
	defb 000h		;48c6	00		.
	defb 000h		;48c7	00		.
	defb 000h		;48c8	00		.
	defb 000h		;48c9	00		.
	defb 000h		;48ca	00		.
	defb 000h		;48cb	00		.
	defb 000h		;48cc	00		.
	defb 000h		;48cd	00		.
	defb 000h		;48ce	00		.
	defb 000h		;48cf	00		.
	defb 000h		;48d0	00		.
	defb 000h		;48d1	00		.
	defb 000h		;48d2	00		.
	defb 000h		;48d3	00		.
	defb 000h		;48d4	00		.
	defb 000h		;48d5	00		.
	defb 000h		;48d6	00		.
	defb 000h		;48d7	00		.
	defb 000h		;48d8	00		.
	defb 000h		;48d9	00		.
	defb 000h		;48da	00		.
	defb 000h		;48db	00		.
	defb 000h		;48dc	00		.
	defb 000h		;48dd	00		.
	defb 000h		;48de	00		.
	defb 000h		;48df	00		.
	defb 000h		;48e0	00		.
	defb 000h		;48e1	00		.
	defb 000h		;48e2	00		.
	defb 000h		;48e3	00		.
	defb 000h		;48e4	00		.
	defb 000h		;48e5	00		.
	defb 000h		;48e6	00		.
	defb 000h		;48e7	00		.
	defb 000h		;48e8	00		.
	defb 000h		;48e9	00		.
	defb 000h		;48ea	00		.
	defb 000h		;48eb	00		.
	defb 000h		;48ec	00		.
	defb 000h		;48ed	00		.
	defb 000h		;48ee	00		.
	defb 000h		;48ef	00		.
	defb 000h		;48f0	00		.
	defb 000h		;48f1	00		.
	defb 000h		;48f2	00		.
	defb 000h		;48f3	00		.
	defb 000h		;48f4	00		.
	defb 000h		;48f5	00		.
	defb 000h		;48f6	00		.
	defb 000h		;48f7	00		.
	defb 000h		;48f8	00		.
	defb 000h		;48f9	00		.
	defb 000h		;48fa	00		.
	defb 000h		;48fb	00		.
	defb 000h		;48fc	00		.
	defb 000h		;48fd	00		.
	defb 000h		;48fe	00		.
	ld bc,0f549h		;48ff	01 49 f5	. I .
	in a,(019h)		;4902	db 19		. .
	rrca			;4904	0f		.
	jp c,l48a9h		;4905	da a9 48	. . H
	push hl			;4908	e5		.
	push de			;4909	d5		.
	push bc			;490a	c5		.
	ld hl,l48a8h		;490b	21 a8 48	! . H
	ld (d48b6_end),hl	;490e	22 ff 48	" . H
	ei			;4911	fb		.
	exx			;4912	d9		.
	ex af,af'		;4913	08		.
	push af			;4914	f5		.
	push hl			;4915	e5		.
	push de			;4916	d5		.
	push bc			;4917	c5		.
	push ix			;4918	dd e5		. .
	push iy			;491a	fd e5		. .
	call sub_47efh		;491c	cd ef 47	. . G
	ld hl,d48b6_end+2	;491f	21 01 49	! . I
	ld (d48b6_end),hl	;4922	22 ff 48	" . H
	pop iy			;4925	fd e1		. .
	pop ix			;4927	dd e1		. .
	pop bc			;4929	c1		.
	pop de			;492a	d1		.
	pop hl			;492b	e1		.
	pop af			;492c	f1		.
	exx			;492d	d9		.
	ex af,af'		;492e	08		.
	pop bc			;492f	c1		.
	pop de			;4930	d1		.
	pop hl			;4931	e1		.
	pop af			;4932	f1		.
	ret			;4933	c9		.
sub_4934h:
	di			;4934	f3		.
	call d47fa_end		;4935	cd 58 48	. X H
	ld hl,d48b6_end+2	;4938	21 01 49	! . I
	ld (d48b6_end),hl	;493b	22 ff 48	" . H
	ld a,048h		;493e	3e 48		> H
	ld i,a			;4940	ed 47		. G
	im 2			;4942	ed 5e		. ^
	ei			;4944	fb		.
	ret			;4945	c9		.
sub_4946h:
	di			;4946	f3		.
	im 1			;4947	ed 56		. V
	ei			;4949	fb		.
	ret			;494a	c9		.
sub_494bh:
	ld a,(hl)		;494b	7e		~
	inc hl			;494c	23		#
	sub (hl)		;494d	96		.
	dec hl			;494e	2b		+
	ret			;494f	c9		.
	ld a,(hl)		;4950	7e		~
	inc hl			;4951	23		#
	sub (hl)		;4952	96		.
	dec hl			;4953	2b		+
	cpl			;4954	2f		/
	or a			;4955	b7		.
	ret			;4956	c9		.
sub_4957h:
	push de			;4957	d5		.
	ld d,a			;4958	57		W
	ld a,(hl)		;4959	7e		~
	inc hl			;495a	23		#
	sub (hl)		;495b	96		.
	dec hl			;495c	2b		+
	inc a			;495d	3c		<
	jr z,l4974h		;495e	28 14		( .
	ld a,(hl)		;4960	7e		~
	inc (hl)		;4961	34		4
	push hl			;4962	e5		.
	inc hl			;4963	23		#
	inc hl			;4964	23		#
	add a,(hl)		;4965	86		.
	ld e,a			;4966	5f		_
	inc hl			;4967	23		#
	ld a,000h		;4968	3e 00		> .
	adc a,(hl)		;496a	8e		.
	ld h,a			;496b	67		g
	ld l,e			;496c	6b		k
	ld a,d			;496d	7a		z
	ld (hl),a		;496e	77		w
	pop hl			;496f	e1		.
	pop de			;4970	d1		.
	dec hl			;4971	2b		+
	scf			;4972	37		7
	ret			;4973	c9		.
l4974h:
	ld a,d			;4974	7a		z
	pop de			;4975	d1		.
l4976h:
	and a			;4976	a7		.
	ret			;4977	c9		.
sub_4978h:
	ld a,(hl)		;4978	7e		~
	inc hl			;4979	23		#
	sub (hl)		;497a	96		.
	jr z,l4976h		;497b	28 f9		( .
	push de			;497d	d5		.
	ld a,(hl)		;497e	7e		~
	inc hl			;497f	23		#
	add a,(hl)		;4980	86		.
	ld e,a			;4981	5f		_
	inc hl			;4982	23		#
	ld a,000h		;4983	3e 00		> .
	adc a,(hl)		;4985	8e		.
	ld d,a			;4986	57		W
	ld a,(de)		;4987	1a		.
	pop de			;4988	d1		.
	dec hl			;4989	2b		+
	dec hl			;498a	2b		+
	inc (hl)		;498b	34		4
	dec hl			;498c	2b		+
	scf			;498d	37		7
	ret			;498e	c9		.
l498fh:
	nop			;498f	00		.

; #1FFD = #11 (extended pages), #9FBD = A (A19-A14 = bits 5-0, AEN = bit 6,
; RESET = bit 7; core/src/emulator/io/sprinter/isa/sprinterisabus.h), #1FFD = #01.
SetIsaLatch:
	push bc			;4990	c5		.
	push de			;4991	d5		.
	ld bc,01ffdh		;4992	01 fd 1f	. . .
	ld de,00111h		;4995	11 11 01	. . .
	out (c),e		;4998	ed 59		. Y
	ld bc,09fbdh		;499a	01 bd 9f	. . .
	out (c),a		;499d	ed 79		. y
	ld (l498fh),a		;499f	32 8f 49	2 . I
	ld bc,01ffdh		;49a2	01 fd 1f	. . .
	out (c),d		;49a5	ed 51		. Q
	pop de			;49a7	d1		.
	pop bc			;49a8	c1		.
	ret			;49a9	c9		.
	push af			;49aa	f5		.
	ld a,(l498fh)		;49ab	3a 8f 49	: . I
	rla			;49ae	17		.
	rrca			;49af	0f		.
	call SetIsaLatch	;49b0	cd 90 49	. . I
	pop af			;49b3	f1		.
	ret			;49b4	c9		.
	push af			;49b5	f5		.
	ld a,(l498fh)		;49b6	3a 8f 49	: . I
	res 6,a			;49b9	cb b7		. .
	jr nc,l49bfh		;49bb	30 02		0 .
	set 6,a			;49bd	cb f7		. .
l49bfh:
	call SetIsaLatch	;49bf	cd 90 49	. . I
	pop af			;49c2	f1		.
	ret			;49c3	c9		.
	push af			;49c4	f5		.
	push bc			;49c5	c5		.
	and 03fh		;49c6	e6 3f		. ?
	ld c,a			;49c8	4f		O
	ld a,(l498fh)		;49c9	3a 8f 49	: . I
	and 0c0h		;49cc	e6 c0		. .
	or c			;49ce	b1		.
	call SetIsaLatch	;49cf	cd 90 49	. . I
	pop bc			;49d2	c1		.
	pop af			;49d3	f1		.
	ret			;49d4	c9		.
	ld a,(049f4h)		;49d5	3a f4 49	: . I
	rrca			;49d8	0f		.
	and 001h		;49d9	e6 01		. .
	ret			;49db	c9		.

; A = slot: patches the operand of SelectIsaIo to 4 | slot*2.
SetSlot:
	push af			;49dc	f5		.
	rlca			;49dd	07		.
	and 002h		;49de	e6 02		. .
	or 004h			;49e0	f6 04		. .
	ld (049f4h),a		;49e2	32 f4 49	2 . I
	pop af			;49e5	f1		.
	ret			;49e6	c9		.
	push af			;49e7	f5		.
	ld a,(049f4h)		;49e8	3a f4 49	: . I
	xor 002h		;49eb	ee 02		. .
	ld (049f4h),a		;49ed	32 f4 49	2 . I
	pop af			;49f0	f1		.
	ret			;49f1	c9		.

; ISA I/O page #D0 | (A & 6): #D4 = slot 0, #D6 = slot 1, patched into every
; access routine below; #9FBD = 0.
SelectIsaIo:
	push af			;49f2	f5		.
	ld a,004h		;49f3	3e 04		> .
	call sub_49fah		;49f5	cd fa 49	. . I
	pop af			;49f8	f1		.
	ret			;49f9	c9		.
sub_49fah:
	push af			;49fa	f5		.
	and 006h		;49fb	e6 06		. .
	or 0d0h			;49fd	f6 d0		. .
	ld (04a37h),a		;49ff	32 37 4a	2 7 J
	ld (04a56h),a		;4a02	32 56 4a	2 V J
	ld (04a81h),a		;4a05	32 81 4a	2 . J
	ld (04afah),a		;4a08	32 fa 4a	2 . J
	ld (04b4bh),a		;4a0b	32 4b 4b	2 K K
	ld (04b69h),a		;4a0e	32 69 4b	2 i K
	ld (04b94h),a		;4a11	32 94 4b	2 . K
	ld (04c01h),a		;4a14	32 01 4c	2 . L
	ld a,000h		;4a17	3e 00		> .
	call SetIsaLatch	;4a19	cd 90 49	. . I
	pop af			;4a1c	f1		.
	ret			;4a1d	c9		.

; #9FBD = #40: AEN on, no cycles reach the cards.
BusIdle:
	push af			;4a1e	f5		.
	ld a,040h		;4a1f	3e 40		> @
	call SetIsaLatch	;4a21	cd 90 49	. . I
	pop af			;4a24	f1		.
	ret			;4a25	c9		.

; Write A to ISA I/O port DE: window 3 = the slot's I/O page, (#C000 | DE) = A.
IsaOut:
	push bc			;4a26	c5		.
	push hl			;4a27	e5		.
	push de			;4a28	d5		.
	set 6,d			;4a29	cb f2		. .
	set 7,d			;4a2b	cb fa		. .
	ld bc,01ffdh		;4a2d	01 fd 1f	. . .
	ld l,011h		;4a30	2e 11		. .
	out (c),l		;4a32	ed 69		. i
	ld c,0e2h		;4a34	0e e2		. .
	ld l,0d4h		;4a36	2e d4		. .
	out (c),l		;4a38	ed 69		. i
	ld (de),a		;4a3a	12		.
	ld bc,01ffdh		;4a3b	01 fd 1f	. . .
	ld l,001h		;4a3e	2e 01		. .
	out (c),l		;4a40	ed 69		. i
	pop de			;4a42	d1		.
	pop hl			;4a43	e1		.
	pop bc			;4a44	c1		.
	ret			;4a45	c9		.
IsaOutWord:
	push af			;4a46	f5		.
	push de			;4a47	d5		.
	push hl			;4a48	e5		.
	push bc			;4a49	c5		.
	set 6,d			;4a4a	cb f2		. .
	set 7,d			;4a4c	cb fa		. .
	ld bc,01ffdh		;4a4e	01 fd 1f	. . .
	ld l,011h		;4a51	2e 11		. .
	out (c),l		;4a53	ed 69		. i
	ld a,0d4h		;4a55	3e d4		> .
	out (0e2h),a		;4a57	d3 e2		. .
	ex de,hl		;4a59	eb		.
	pop bc			;4a5a	c1		.
	push bc			;4a5b	c5		.
	ld (hl),c		;4a5c	71		q
	inc hl			;4a5d	23		#
	ld (hl),b		;4a5e	70		p
	ld bc,01ffdh		;4a5f	01 fd 1f	. . .
	ld l,001h		;4a62	2e 01		. .
	out (c),l		;4a64	ed 69		. i
	pop bc			;4a66	c1		.
	pop hl			;4a67	e1		.
	pop de			;4a68	d1		.
	pop af			;4a69	f1		.
	ret			;4a6a	c9		.

; BC bytes from HL to the one port DE.
IsaOutBlock:
	push af			;4a6b	f5		.
	ex af,af'		;4a6c	08		.
	push af			;4a6d	f5		.
	push de			;4a6e	d5		.
	push bc			;4a6f	c5		.
	set 6,d			;4a70	cb f2		. .
	set 7,d			;4a72	cb fa		. .
	in a,(0e2h)		;4a74	db e2		. .
	ex af,af'		;4a76	08		.
	push bc			;4a77	c5		.
	ld bc,01ffdh		;4a78	01 fd 1f	. . .
	ld a,011h		;4a7b	3e 11		> .
	out (c),a		;4a7d	ed 79		. y
	pop bc			;4a7f	c1		.
	ld a,0d4h		;4a80	3e d4		> .
	out (0e2h),a		;4a82	d3 e2		. .
l4a84h:
	bit 6,h			;4a84	cb 74		. t
	jr z,l4aa0h		;4a86	28 18		( .
	bit 7,h			;4a88	cb 7c		. |
	jr z,l4aa0h		;4a8a	28 14		( .
	in a,(0c2h)		;4a8c	db c2		. .
	ex af,af'		;4a8e	08		.
	out (0c2h),a		;4a8f	d3 c2		. .
	res 6,h			;4a91	cb b4		. .
l4a93h:
	ldi			;4a93	ed a0		. .
	dec de			;4a95	1b		.
	jp pe,l4a93h		;4a96	ea 93 4a	. . J
	ex af,af'		;4a99	08		.
	out (0c2h),a		;4a9a	d3 c2		. .
	set 6,h			;4a9c	cb f4		. .
	jr l4aa6h		;4a9e	18 06		. .
l4aa0h:
	ldi			;4aa0	ed a0		. .
	dec de			;4aa2	1b		.
	jp pe,l4a84h		;4aa3	ea 84 4a	. . J
l4aa6h:
	ld bc,01ffdh		;4aa6	01 fd 1f	. . .
	ld a,001h		;4aa9	3e 01		> .
	out (c),a		;4aab	ed 79		. y
	pop bc			;4aad	c1		.
	pop de			;4aae	d1		.
	pop af			;4aaf	f1		.
	ex af,af'		;4ab0	08		.
	pop af			;4ab1	f1		.
	ret			;4ab2	c9		.
	push bc			;4ab3	c5		.
	ld b,000h		;4ab4	06 00		. .
	inc c			;4ab6	0c		.
	dec c			;4ab7	0d		.
	jr nz,l4abbh		;4ab8	20 01		  .
	inc b			;4aba	04		.
l4abbh:
	call IsaOutBlock	;4abb	cd 6b 4a	. k J
	pop bc			;4abe	c1		.
	ret			;4abf	c9		.
	push af			;4ac0	f5		.
	ld (04afeh),a		;4ac1	32 fe 4a	2 . J
	ex af,af'		;4ac4	08		.
	push af			;4ac5	f5		.
	push de			;4ac6	d5		.
	push bc			;4ac7	c5		.
	ld a,b			;4ac8	78		x
	ld (04b14h),a		;4ac9	32 14 4b	2 . K
	ld (04b26h),a		;4acc	32 26 4b	2 & K
	ld a,d			;4acf	7a		z
	rlca			;4ad0	07		.
	rlca			;4ad1	07		.
	and 003h		;4ad2	e6 03		. .
	set 6,d			;4ad4	cb f2		. .
	set 7,d			;4ad6	cb fa		. .
	ld (l4b22h+1),de	;4ad8	ed 53 23 4b	. S # K
	ld (l4b10h+1),de	;4adc	ed 53 11 4b	. S . K
	ld b,000h		;4ae0	06 00		. .
	inc c			;4ae2	0c		.
	dec c			;4ae3	0d		.
	jr nz,l4ae7h		;4ae4	20 01		  .
	inc b			;4ae6	04		.
l4ae7h:
	add a,e			;4ae7	83		.
	ld e,a			;4ae8	5f		_
	ld a,d			;4ae9	7a		z
	adc a,000h		;4aea	ce 00		. .
	ld d,a			;4aec	57		W
	in a,(0e2h)		;4aed	db e2		. .
	ex af,af'		;4aef	08		.
	push bc			;4af0	c5		.
	ld bc,01ffdh		;4af1	01 fd 1f	. . .
	ld a,011h		;4af4	3e 11		> .
	out (c),a		;4af6	ed 79		. y
	pop bc			;4af8	c1		.
	ld a,0d4h		;4af9	3e d4		> .
	out (0e2h),a		;4afb	d3 e2		. .
	ld a,000h		;4afd	3e 00		> .
l4affh:
	bit 6,h			;4aff	cb 74		. t
	jr z,l4b22h		;4b01	28 1f		( .
	bit 7,h			;4b03	cb 7c		. |
	jr z,l4b22h		;4b05	28 1b		( .
	push af			;4b07	f5		.
	in a,(0c2h)		;4b08	db c2		. .
	ex af,af'		;4b0a	08		.
	out (0c2h),a		;4b0b	d3 c2		. .
	pop af			;4b0d	f1		.
	res 6,h			;4b0e	cb b4		. .
l4b10h:
	ld (00000h),a		;4b10	32 00 00	2 . .
	add a,000h		;4b13	c6 00		. .
	ldi			;4b15	ed a0		. .
	dec de			;4b17	1b		.
	jp pe,l4b10h		;4b18	ea 10 4b	. . K
	ex af,af'		;4b1b	08		.
	out (0c2h),a		;4b1c	d3 c2		. .
	set 6,h			;4b1e	cb f4		. .
	jr l4b2dh		;4b20	18 0b		. .
l4b22h:
	ld (00000h),a		;4b22	32 00 00	2 . .
	add a,000h		;4b25	c6 00		. .
	ldi			;4b27	ed a0		. .
	dec de			;4b29	1b		.
	jp pe,l4affh		;4b2a	ea ff 4a	. . J
l4b2dh:
	ld bc,01ffdh		;4b2d	01 fd 1f	. . .
	ld a,001h		;4b30	3e 01		> .
	out (c),a		;4b32	ed 79		. y
	pop bc			;4b34	c1		.
	pop de			;4b35	d1		.
	pop af			;4b36	f1		.
	ex af,af'		;4b37	08		.
	pop af			;4b38	f1		.
	ret			;4b39	c9		.

; Read ISA I/O port DE into A.
IsaIn:
	push bc			;4b3a	c5		.
	push hl			;4b3b	e5		.
	push de			;4b3c	d5		.
	set 6,d			;4b3d	cb f2		. .
	set 7,d			;4b3f	cb fa		. .
	ld bc,01ffdh		;4b41	01 fd 1f	. . .
	ld l,011h		;4b44	2e 11		. .
	out (c),l		;4b46	ed 69		. i
	ld c,0e2h		;4b48	0e e2		. .
	ld l,0d4h		;4b4a	2e d4		. .
	out (c),l		;4b4c	ed 69		. i
	ld a,(de)		;4b4e	1a		.
	ld bc,01ffdh		;4b4f	01 fd 1f	. . .
	ld l,001h		;4b52	2e 01		. .
	out (c),l		;4b54	ed 69		. i
	pop de			;4b56	d1		.
	pop hl			;4b57	e1		.
	pop bc			;4b58	c1		.
	ret			;4b59	c9		.
	push de			;4b5a	d5		.
	push hl			;4b5b	e5		.
	push bc			;4b5c	c5		.
	set 6,d			;4b5d	cb f2		. .
	set 7,d			;4b5f	cb fa		. .
	ld bc,01ffdh		;4b61	01 fd 1f	. . .
	ld l,011h		;4b64	2e 11		. .
	out (c),l		;4b66	ed 69		. i
	ld a,0d4h		;4b68	3e d4		> .
	out (0e2h),a		;4b6a	d3 e2		. .
	ex de,hl		;4b6c	eb		.
	pop bc			;4b6d	c1		.
	push bc			;4b6e	c5		.
	ld (hl),c		;4b6f	71		q
	inc hl			;4b70	23		#
	ld a,(hl)		;4b71	7e		~
	ld bc,01ffdh		;4b72	01 fd 1f	. . .
	ld l,001h		;4b75	2e 01		. .
	out (c),l		;4b77	ed 69		. i
	pop bc			;4b79	c1		.
	pop hl			;4b7a	e1		.
	pop de			;4b7b	d1		.
	ret			;4b7c	c9		.

; BC bytes from the one port HL to DE.
IsaInBlock:
	push af			;4b7d	f5		.
	ex af,af'		;4b7e	08		.
	push af			;4b7f	f5		.
	push de			;4b80	d5		.
	push bc			;4b81	c5		.
	set 6,d			;4b82	cb f2		. .
	set 7,d			;4b84	cb fa		. .
	in a,(0e2h)		;4b86	db e2		. .
	ex af,af'		;4b88	08		.
	push bc			;4b89	c5		.
	ld bc,01ffdh		;4b8a	01 fd 1f	. . .
	ld a,011h		;4b8d	3e 11		> .
	out (c),a		;4b8f	ed 79		. y
	pop bc			;4b91	c1		.
	ex de,hl		;4b92	eb		.
	ld a,0d4h		;4b93	3e d4		> .
	out (0e2h),a		;4b95	d3 e2		. .
l4b97h:
	bit 6,d			;4b97	cb 72		. r
	jr z,l4bb3h		;4b99	28 18		( .
	bit 7,d			;4b9b	cb 7a		. z
	jr z,l4bb3h		;4b9d	28 14		( .
	in a,(0c2h)		;4b9f	db c2		. .
	ex af,af'		;4ba1	08		.
	out (0c2h),a		;4ba2	d3 c2		. .
	res 6,d			;4ba4	cb b2		. .
l4ba6h:
	ldi			;4ba6	ed a0		. .
	dec hl			;4ba8	2b		+
	jp pe,l4ba6h		;4ba9	ea a6 4b	. . K
	ex af,af'		;4bac	08		.
	out (0c2h),a		;4bad	d3 c2		. .
	set 6,d			;4baf	cb f2		. .
	jr l4bb9h		;4bb1	18 06		. .
l4bb3h:
	ldi			;4bb3	ed a0		. .
	dec hl			;4bb5	2b		+
	jp pe,l4b97h		;4bb6	ea 97 4b	. . K
l4bb9h:
	ld bc,01ffdh		;4bb9	01 fd 1f	. . .
	ld a,001h		;4bbc	3e 01		> .
	out (c),a		;4bbe	ed 79		. y
	ex de,hl		;4bc0	eb		.
	pop bc			;4bc1	c1		.
	pop de			;4bc2	d1		.
	pop af			;4bc3	f1		.
	ex af,af'		;4bc4	08		.
	pop af			;4bc5	f1		.
	ret			;4bc6	c9		.
	push af			;4bc7	f5		.
	ld (04c06h),a		;4bc8	32 06 4c	2 . L
	ex af,af'		;4bcb	08		.
	push af			;4bcc	f5		.
	push de			;4bcd	d5		.
	push bc			;4bce	c5		.
	ld a,b			;4bcf	78		x
	ld (04c1ch),a		;4bd0	32 1c 4c	2 . L
	ld (04c2eh),a		;4bd3	32 2e 4c	2 . L
	ld a,d			;4bd6	7a		z
	rlca			;4bd7	07		.
	rlca			;4bd8	07		.
	and 003h		;4bd9	e6 03		. .
	set 6,d			;4bdb	cb f2		. .
	set 7,d			;4bdd	cb fa		. .
	ld (l4c2ah+1),de	;4bdf	ed 53 2b 4c	. S + L
	ld (l4c18h+1),de	;4be3	ed 53 19 4c	. S . L
	ld b,000h		;4be7	06 00		. .
	inc c			;4be9	0c		.
	dec c			;4bea	0d		.
	jr nz,l4beeh		;4beb	20 01		  .
	inc b			;4bed	04		.
l4beeh:
	add a,e			;4bee	83		.
	ld e,a			;4bef	5f		_
	ld a,d			;4bf0	7a		z
	adc a,000h		;4bf1	ce 00		. .
	ld d,a			;4bf3	57		W
	in a,(0e2h)		;4bf4	db e2		. .
	ex af,af'		;4bf6	08		.
	push bc			;4bf7	c5		.
	ld bc,01ffdh		;4bf8	01 fd 1f	. . .
	ld a,011h		;4bfb	3e 11		> .
	out (c),a		;4bfd	ed 79		. y
	pop bc			;4bff	c1		.
	ld a,0d4h		;4c00	3e d4		> .
	out (0e2h),a		;4c02	d3 e2		. .
	ex de,hl		;4c04	eb		.
	ld a,000h		;4c05	3e 00		> .
l4c07h:
	bit 6,d			;4c07	cb 72		. r
	jr z,l4c2ah		;4c09	28 1f		( .
	bit 7,d			;4c0b	cb 7a		. z
	jr z,l4c2ah		;4c0d	28 1b		( .
	push af			;4c0f	f5		.
	in a,(0c2h)		;4c10	db c2		. .
	ex af,af'		;4c12	08		.
	out (0c2h),a		;4c13	d3 c2		. .
	pop af			;4c15	f1		.
	res 6,d			;4c16	cb b2		. .
l4c18h:
	ld (00000h),a		;4c18	32 00 00	2 . .
	add a,000h		;4c1b	c6 00		. .
	ldi			;4c1d	ed a0		. .
	dec hl			;4c1f	2b		+
	jp pe,l4c18h		;4c20	ea 18 4c	. . L
	ex af,af'		;4c23	08		.
	out (0c2h),a		;4c24	d3 c2		. .
	set 6,d			;4c26	cb f2		. .
	jr l4c35h		;4c28	18 0b		. .
l4c2ah:
	ld (00000h),a		;4c2a	32 00 00	2 . .
	add a,000h		;4c2d	c6 00		. .
	ldi			;4c2f	ed a0		. .
	dec hl			;4c31	2b		+
	jp pe,l4c07h		;4c32	ea 07 4c	. . L
l4c35h:
	ld bc,01ffdh		;4c35	01 fd 1f	. . .
	ld a,001h		;4c38	3e 01		> .
	out (c),a		;4c3a	ed 79		. y
	ex de,hl		;4c3c	eb		.
	pop bc			;4c3d	c1		.
	pop de			;4c3e	d1		.
	pop af			;4c3f	f1		.
	ex af,af'		;4c40	08		.
	pop af			;4c41	f1		.
	ret			;4c42	c9		.
l4c43h:
	nop			;4c43	00		.
	nop			;4c44	00		.
	ld b,a			;4c45	47		G
	ld c,h			;4c46	4c		L

; BLOCK 'd4c47' (start 0x4c47 end 0x4d47)
d4c47_start:
	defb 000h		;4c47	00		.
	defb 000h		;4c48	00		.
	defb 000h		;4c49	00		.
	defb 000h		;4c4a	00		.
	defb 000h		;4c4b	00		.
	defb 000h		;4c4c	00		.
	defb 000h		;4c4d	00		.
	defb 000h		;4c4e	00		.
	defb 000h		;4c4f	00		.
	defb 000h		;4c50	00		.
	defb 000h		;4c51	00		.
	defb 000h		;4c52	00		.
	defb 000h		;4c53	00		.
	defb 000h		;4c54	00		.
	defb 000h		;4c55	00		.
	defb 000h		;4c56	00		.
	defb 000h		;4c57	00		.
	defb 000h		;4c58	00		.
	defb 000h		;4c59	00		.
	defb 000h		;4c5a	00		.
	defb 000h		;4c5b	00		.
	defb 000h		;4c5c	00		.
	defb 000h		;4c5d	00		.
	defb 000h		;4c5e	00		.
	defb 000h		;4c5f	00		.
	defb 000h		;4c60	00		.
	defb 000h		;4c61	00		.
	defb 000h		;4c62	00		.
	defb 000h		;4c63	00		.
	defb 000h		;4c64	00		.
	defb 000h		;4c65	00		.
	defb 000h		;4c66	00		.
	defb 000h		;4c67	00		.
	defb 000h		;4c68	00		.
	defb 000h		;4c69	00		.
	defb 000h		;4c6a	00		.
	defb 000h		;4c6b	00		.
	defb 000h		;4c6c	00		.
	defb 000h		;4c6d	00		.
	defb 000h		;4c6e	00		.
	defb 000h		;4c6f	00		.
	defb 000h		;4c70	00		.
	defb 000h		;4c71	00		.
	defb 000h		;4c72	00		.
	defb 000h		;4c73	00		.
	defb 000h		;4c74	00		.
	defb 000h		;4c75	00		.
	defb 000h		;4c76	00		.
	defb 000h		;4c77	00		.
	defb 000h		;4c78	00		.
	defb 000h		;4c79	00		.
	defb 000h		;4c7a	00		.
	defb 000h		;4c7b	00		.
	defb 000h		;4c7c	00		.
	defb 000h		;4c7d	00		.
	defb 000h		;4c7e	00		.
	defb 000h		;4c7f	00		.
	defb 000h		;4c80	00		.
	defb 000h		;4c81	00		.
	defb 000h		;4c82	00		.
	defb 000h		;4c83	00		.
	defb 000h		;4c84	00		.
	defb 000h		;4c85	00		.
	defb 000h		;4c86	00		.
	defb 000h		;4c87	00		.
	defb 000h		;4c88	00		.
	defb 000h		;4c89	00		.
	defb 000h		;4c8a	00		.
	defb 000h		;4c8b	00		.
	defb 000h		;4c8c	00		.
	defb 000h		;4c8d	00		.
	defb 000h		;4c8e	00		.
	defb 000h		;4c8f	00		.
	defb 000h		;4c90	00		.
	defb 000h		;4c91	00		.
	defb 000h		;4c92	00		.
	defb 000h		;4c93	00		.
	defb 000h		;4c94	00		.
	defb 000h		;4c95	00		.
	defb 000h		;4c96	00		.
	defb 000h		;4c97	00		.
	defb 000h		;4c98	00		.
	defb 000h		;4c99	00		.
	defb 000h		;4c9a	00		.
	defb 000h		;4c9b	00		.
	defb 000h		;4c9c	00		.
	defb 000h		;4c9d	00		.
	defb 000h		;4c9e	00		.
	defb 000h		;4c9f	00		.
	defb 000h		;4ca0	00		.
	defb 000h		;4ca1	00		.
	defb 000h		;4ca2	00		.
	defb 000h		;4ca3	00		.
	defb 000h		;4ca4	00		.
	defb 000h		;4ca5	00		.
	defb 000h		;4ca6	00		.
	defb 000h		;4ca7	00		.
	defb 000h		;4ca8	00		.
	defb 000h		;4ca9	00		.
	defb 000h		;4caa	00		.
	defb 000h		;4cab	00		.
	defb 000h		;4cac	00		.
	defb 000h		;4cad	00		.
	defb 000h		;4cae	00		.
	defb 000h		;4caf	00		.
	defb 000h		;4cb0	00		.
	defb 000h		;4cb1	00		.
	defb 000h		;4cb2	00		.
	defb 000h		;4cb3	00		.
	defb 000h		;4cb4	00		.
	defb 000h		;4cb5	00		.
	defb 000h		;4cb6	00		.
	defb 000h		;4cb7	00		.
	defb 000h		;4cb8	00		.
	defb 000h		;4cb9	00		.
	defb 000h		;4cba	00		.
	defb 000h		;4cbb	00		.
	defb 000h		;4cbc	00		.
	defb 000h		;4cbd	00		.
	defb 000h		;4cbe	00		.
	defb 000h		;4cbf	00		.
	defb 000h		;4cc0	00		.
	defb 000h		;4cc1	00		.
	defb 000h		;4cc2	00		.
	defb 000h		;4cc3	00		.
	defb 000h		;4cc4	00		.
	defb 000h		;4cc5	00		.
	defb 000h		;4cc6	00		.
	defb 000h		;4cc7	00		.
	defb 000h		;4cc8	00		.
	defb 000h		;4cc9	00		.
	defb 000h		;4cca	00		.
	defb 000h		;4ccb	00		.
	defb 000h		;4ccc	00		.
	defb 000h		;4ccd	00		.
	defb 000h		;4cce	00		.
	defb 000h		;4ccf	00		.
	defb 000h		;4cd0	00		.
	defb 000h		;4cd1	00		.
	defb 000h		;4cd2	00		.
	defb 000h		;4cd3	00		.
	defb 000h		;4cd4	00		.
	defb 000h		;4cd5	00		.
	defb 000h		;4cd6	00		.
	defb 000h		;4cd7	00		.
	defb 000h		;4cd8	00		.
	defb 000h		;4cd9	00		.
	defb 000h		;4cda	00		.
	defb 000h		;4cdb	00		.
	defb 000h		;4cdc	00		.
	defb 000h		;4cdd	00		.
	defb 000h		;4cde	00		.
	defb 000h		;4cdf	00		.
	defb 000h		;4ce0	00		.
	defb 000h		;4ce1	00		.
	defb 000h		;4ce2	00		.
	defb 000h		;4ce3	00		.
	defb 000h		;4ce4	00		.
	defb 000h		;4ce5	00		.
	defb 000h		;4ce6	00		.
	defb 000h		;4ce7	00		.
	defb 000h		;4ce8	00		.
	defb 000h		;4ce9	00		.
	defb 000h		;4cea	00		.
	defb 000h		;4ceb	00		.
	defb 000h		;4cec	00		.
	defb 000h		;4ced	00		.
	defb 000h		;4cee	00		.
	defb 000h		;4cef	00		.
	defb 000h		;4cf0	00		.
	defb 000h		;4cf1	00		.
	defb 000h		;4cf2	00		.
	defb 000h		;4cf3	00		.
	defb 000h		;4cf4	00		.
	defb 000h		;4cf5	00		.
	defb 000h		;4cf6	00		.
	defb 000h		;4cf7	00		.
	defb 000h		;4cf8	00		.
	defb 000h		;4cf9	00		.
	defb 000h		;4cfa	00		.
	defb 000h		;4cfb	00		.
	defb 000h		;4cfc	00		.
	defb 000h		;4cfd	00		.
	defb 000h		;4cfe	00		.
	defb 000h		;4cff	00		.
	defb 000h		;4d00	00		.
	defb 000h		;4d01	00		.
	defb 000h		;4d02	00		.
	defb 000h		;4d03	00		.
	defb 000h		;4d04	00		.
	defb 000h		;4d05	00		.
	defb 000h		;4d06	00		.
	defb 000h		;4d07	00		.
	defb 000h		;4d08	00		.
	defb 000h		;4d09	00		.
	defb 000h		;4d0a	00		.
	defb 000h		;4d0b	00		.
	defb 000h		;4d0c	00		.
	defb 000h		;4d0d	00		.
	defb 000h		;4d0e	00		.
	defb 000h		;4d0f	00		.
	defb 000h		;4d10	00		.
	defb 000h		;4d11	00		.
	defb 000h		;4d12	00		.
	defb 000h		;4d13	00		.
	defb 000h		;4d14	00		.
	defb 000h		;4d15	00		.
	defb 000h		;4d16	00		.
	defb 000h		;4d17	00		.
	defb 000h		;4d18	00		.
	defb 000h		;4d19	00		.
	defb 000h		;4d1a	00		.
	defb 000h		;4d1b	00		.
	defb 000h		;4d1c	00		.
	defb 000h		;4d1d	00		.
	defb 000h		;4d1e	00		.
	defb 000h		;4d1f	00		.
	defb 000h		;4d20	00		.
	defb 000h		;4d21	00		.
	defb 000h		;4d22	00		.
	defb 000h		;4d23	00		.
	defb 000h		;4d24	00		.
	defb 000h		;4d25	00		.
	defb 000h		;4d26	00		.
	defb 000h		;4d27	00		.
	defb 000h		;4d28	00		.
	defb 000h		;4d29	00		.
	defb 000h		;4d2a	00		.
	defb 000h		;4d2b	00		.
	defb 000h		;4d2c	00		.
	defb 000h		;4d2d	00		.
	defb 000h		;4d2e	00		.
	defb 000h		;4d2f	00		.
	defb 000h		;4d30	00		.
	defb 000h		;4d31	00		.
	defb 000h		;4d32	00		.
	defb 000h		;4d33	00		.
	defb 000h		;4d34	00		.
	defb 000h		;4d35	00		.
	defb 000h		;4d36	00		.
	defb 000h		;4d37	00		.
	defb 000h		;4d38	00		.
	defb 000h		;4d39	00		.
	defb 000h		;4d3a	00		.
	defb 000h		;4d3b	00		.
	defb 000h		;4d3c	00		.
	defb 000h		;4d3d	00		.
	defb 000h		;4d3e	00		.
	defb 000h		;4d3f	00		.
	defb 000h		;4d40	00		.
	defb 000h		;4d41	00		.
	defb 000h		;4d42	00		.
	defb 000h		;4d43	00		.
	defb 000h		;4d44	00		.
	defb 000h		;4d45	00		.
	defb 000h		;4d46	00		.

; SendEnable with A = 0; found when the card answers #DF; then up to 32 bytes of
; its identification string go to #4D55.
Identify:
	ld a,000h		;4d47	3e 00		> .
	call SendEnable		;4d49	cd 82 4d	. . M
	ret nz			;4d4c	c0		.
	ld hl,d4d55_start	;4d4d	21 55 4d	! U M
	call d4d55_end		;4d50	cd 76 4d	. v M
	xor a			;4d53	af		.
	ret			;4d54	c9		.

; BLOCK 'd4d55' (start 0x4d55 end 0x4d76)
d4d55_start:
	defb 020h		;4d55	20		 
	defb 020h		;4d56	20		 
	defb 020h		;4d57	20		 
	defb 020h		;4d58	20		 
	defb 020h		;4d59	20		 
	defb 020h		;4d5a	20		 
	defb 020h		;4d5b	20		 
	defb 020h		;4d5c	20		 
	defb 020h		;4d5d	20		 
	defb 020h		;4d5e	20		 
	defb 020h		;4d5f	20		 
	defb 020h		;4d60	20		 
	defb 020h		;4d61	20		 
	defb 020h		;4d62	20		 
	defb 020h		;4d63	20		 
	defb 020h		;4d64	20		 
	defb 020h		;4d65	20		 
	defb 020h		;4d66	20		 
	defb 020h		;4d67	20		 
	defb 020h		;4d68	20		 
	defb 020h		;4d69	20		 
	defb 020h		;4d6a	20		 
	defb 020h		;4d6b	20		 
	defb 020h		;4d6c	20		 
	defb 020h		;4d6d	20		 
	defb 020h		;4d6e	20		 
	defb 020h		;4d6f	20		 
	defb 020h		;4d70	20		 
	defb 020h		;4d71	20		 
	defb 020h		;4d72	20		 
	defb 020h		;4d73	20		 
	defb 020h		;4d74	20		 
	defb 000h		;4d75	00		.
	ld b,020h		;4d76	06 20		.  
l4d78h:
	call d4da7_end		;4d78	cd ba 4d	. . M
	ld (hl),a		;4d7b	77		w
	inc hl			;4d7c	23		#
	or a			;4d7d	b7		.
	ret z			;4d7e	c8		.
	djnz l4d78h		;4d7f	10 f7		. .
	ret			;4d81	c9		.

; Port 0 <- #F0, port 1 <- "Wild Sound Enable ", #00 (A), #00; A = port 1.
SendEnable:
	ld hl,d4da7_start	;4d82	21 a7 4d	! . M
	call sub_4d8bh		;4d85	cd 8b 4d	. . M
	cp 0dfh			;4d88	fe df		. .
	ret			;4d8a	c9		.
sub_4d8bh:
	push af			;4d8b	f5		.
	ld a,0f0h		;4d8c	3e f0		> .
	call OutCmd		;4d8e	cd c3 4d	. . M
l4d91h:
	ld a,(hl)		;4d91	7e		~
	or a			;4d92	b7		.
	jr z,l4d9bh		;4d93	28 06		( .
	inc hl			;4d95	23		#
	call OutData		;4d96	cd cc 4d	. . M
	jr l4d91h		;4d99	18 f6		. .
l4d9bh:
	pop af			;4d9b	f1		.
	call OutData		;4d9c	cd cc 4d	. . M
	xor a			;4d9f	af		.
	call OutData		;4da0	cd cc 4d	. . M
	call d4da7_end		;4da3	cd ba 4d	. . M
	ret			;4da6	c9		.

; BLOCK 'd4da7' (start 0x4da7 end 0x4dba)
d4da7_start:
	defb 057h		;4da7	57		W
	defb 069h		;4da8	69		i
	defb 06ch		;4da9	6c		l
	defb 064h		;4daa	64		d
	defb 020h		;4dab	20		 
	defb 053h		;4dac	53		S
	defb 06fh		;4dad	6f		o
	defb 075h		;4dae	75		u
	defb 06eh		;4daf	6e		n
	defb 064h		;4db0	64		d
	defb 020h		;4db1	20		 
	defb 045h		;4db2	45		E
	defb 06eh		;4db3	6e		n
	defb 061h		;4db4	61		a
	defb 062h		;4db5	62		b
	defb 06ch		;4db6	6c		l
	defb 065h		;4db7	65		e
	defb 020h		;4db8	20		 
	defb 000h		;4db9	00		.

; A = ISA port 1.
InData:
	push de			;4dba	d5		.
	ld de,00001h		;4dbb	11 01 00	. . .
	call IsaIn		;4dbe	cd 3a 4b	. : K
	pop de			;4dc1	d1		.
	ret			;4dc2	c9		.

; ISA port 0 <- A.
OutCmd:
	push de			;4dc3	d5		.
	ld de,00000h		;4dc4	11 00 00	. . .
	call IsaOut		;4dc7	cd 26 4a	. & J
	pop de			;4dca	d1		.
	ret			;4dcb	c9		.

; ISA port 1 <- A.
OutData:
	push de			;4dcc	d5		.
	ld de,00001h		;4dcd	11 01 00	. . .
	call IsaOut		;4dd0	cd 26 4a	. & J
	pop de			;4dd3	d1		.
	ret			;4dd4	c9		.
