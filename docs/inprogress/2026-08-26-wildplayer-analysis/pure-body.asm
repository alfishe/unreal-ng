; ==========================================================================
; WildPlayer v0.333 - PURE PLAYER BODY (Pentagon live-memory dump)
; binary: wildplayer_body_org5D3B.bin  org 0x5D3B  len 0x4B88
; 
; WHAT THIS IS
;   Everything between the boot.B BASIC line (0x5D3B) and the loaded
;   PT3 module (0xA8C3): loader stub, unpacked player code, setup UI,
;   file browser, format detectors, TR-DOS disk driver, interrupt
;   handler with the TurboSound switch block, and the installed VTII
;   PT3 engine instance at 0xA000.
; 
; READING ORDER
;   1. BASIC_LINE256 / LOADER_ENTRY  - how TR-DOS `run` reaches the code
;   2. BANK_SWITCH (0x5FE0)          - every bank touch goes through it
;   3. TS_PROBE_OVERLAY (0x6090)     - the TurboSound detection story
;   4. TS_GATE_A / TS_MARKER_CHECK   - how the probe result arms TS mode
;   5. DUAL_CHIP_INIT / INT_HANDLER / PER_FRAME_PLAY - the dual-chip play
;   6. VTII_INSTANCE (0xA000)        - the per-chip music engine
; 
; TURBOSOUND SEMANTICS (classic TS rev.C / 2xAY)
;   OUT #FFFD,#FE  select FIRST chip   OUT #FFFD,#FF  select SECOND chip
;   OUT #FFFD,r    select register r   OUT #BFFD,v    write register data
;   IN  #FFFD      read current register of the SELECTED chip
;   (aliased mirrors #C000/#8000/#BEFD etc. decode to the same devices)
; ==========================================================================

	org 0x5D3B

; --------------------------------------------------------------------------
; symbol table - named anchors (auto labels appear inline at use sites)
; --------------------------------------------------------------------------
BASIC_LINE256           : equ 0x5D3B
LOADER_ENTRY            : equ 0x5D53
STAGE2                  : equ 0x5DB3
BANK_SWITCH             : equ 0x5FE0
TS_PROBE_OVERLAY        : equ 0x6090
TS_GATE_A               : equ 0x6A3A
TS_MARKER_CHECK         : equ 0x6A5D
TS_MODE_FLAG            : equ 0x6B02
TS_STATE                : equ 0x6B03
TS_STATE2               : equ 0x6B05
BANK_SWITCH_TAIL        : equ 0x847F
DUAL_CHIP_INIT          : equ 0x86EF
ANALYZER_POLL_SET       : equ 0x87F6
ANALYZER_POLL           : equ 0x87FA
MUTE_BOTH               : equ 0x8A61
MUTE_SEL_FE             : equ 0x8A66
MUTE_SEL_FF             : equ 0x8A6F
TS_PRESENT_FLAG         : equ 0x8EBF
TS_SETUP_DATA           : equ 0x8EC0
INT_HANDLER             : equ 0x9060
PER_FRAME_PLAY          : equ 0x907D
SEL_FE_PLAY             : equ 0x9091
SEL_FF_PLAY             : equ 0x909B
FDC_POLL                : equ 0x9B04
VTII_INSTANCE           : equ 0xA000
VTII_SIG                : equ 0xA00D
MODULE_BASE             : equ 0xA8C3

; boot.B BASIC line 256 header:  00 01 (line 256) EC 00 (len 236) then
; FD(CLEAR) '0' 0E 00 00 B3 5F 00  /  F9(C0) RANDOMIZE-USR '0' 0E 00 00 53 5D 00
; / 3A(:) EA(REM) <machine code to EOL>.
; TR-DOS hide-the-numbers trick: the LISTED digits are placeholder '0's;
; each is followed by CH-14 (0x0E) + a 5-byte float in ZX small-int form
; (exponent 00, value in mantissa bytes 2-3): 0x5FB3=24499 and 0x5D53=23891.
; Execution uses the binary copy, so the line really means
; CLEAR 24499 : RANDOMIZE USR 23891 : REM <code>  -  RAMTOP=0x5FB3 keeps
; the stack below the code, USR jumps to 0x5D53 right after the REM token.
; TR-DOS `run` loads the whole B-file at PROG=0x5D3B; the interpreter then
; executes this line - USR lands in the loader with the code as 'comment'.
; --------------------------------------------------------------------------
; DATA 0x5D3B-0x5D52  BASIC line-256 header (00 01 EC 00 FD 30 ...)
; --------------------------------------------------------------------------
DATA_5D3B:
	defb 00 01 EC 00 FD 30 0E 00  ;5D3B  .....0..
	defb 00 B3 5F 00 3A F9 C0 30  ;5D43  .._.:..0
	defb 0E 00 00 53 5D 00 3A EA  ;5D4B  ...S].:.


; ..........................................................................
; code segment restart 0x5D53 - clean decode boundary after data above
; ..........................................................................
;unpack/bootstrap stage 2 (JR target from loader)

;Loader entry: DI; XOR A; OUT (#FE),A (border black); JR STAGE2.
;The packed Wild Player image follows as REM data; this stub is the
;only part that runs 'as loaded' - it unpacks the rest over RAM.
LOADER_ENTRY:
	di			;5d53	f3		.
	xor a			;5d54	af		.
	out (0feh),a		;5d55	d3 fe		. .
	jr STAGE2		;5d57	18 5a		. Z
; --------------------------------------------------------------------------
; DATA 0x5D59-0x5DB2  WP SETUP menu text
; --------------------------------------------------------------------------
DATA_5D59:
	defb 00 00 57 50 20 53 45 54  ;5D59  ..WP SET
	defb 55 50 2D 2D 2D 2D 2D 2D  ;5D61  UP------
	defb 2D 2D 48 44 44 64 72 76  ;5D69  --HDDdrv
	defb 3A 31 43 44 20 64 72 76  ;5D71  :1CD drv
	defb 3A 31 47 53 6F 75 6E 64  ;5D79  :1GSound
	defb 3A 31 53 53 70 65 65 64  ;5D81  :1SSpeed
	defb 3A 37 2D 3D 33 44 31 33  ;5D89  :7-=3D13
	defb 3D 2D 53 44 72 69 76 65  ;5D91  =-SDrive
	defb 3A 58 2D 2D 2D 2D 2D 2D  ;5D99  :X------
	defb 2D 2D 57 50 76 30 2E 33  ;5DA1  --WPv0.3
	defb 33 33 3D 3D 3D 3D 3D 3D  ;5DA9  33======
	defb 3D 3D                    ;5DB1  ==


; ..........................................................................
; code segment restart 0x5DB3 - clean decode boundary after data above
; ..........................................................................
;CALL 86CC; LD A,(TS_MODE_FLAG); OR A; JR Z,skip;
;dual-chip init: OUT #FFFD,#FE / OUT #FFFD,#FF (+ register defaults)
;- installs and initializes BOTH chip engines when TS was detected.
;Mute both chips: OUT #FFFD,#FE + R8/R9/R10:=0, then OUT #FFFD,#FF +
;R8/R9/R10:=0. In the captured run this is the ONLY code that ever
;touched chip 2 after the failed probe (9 writes total).
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).

;unpack/bootstrap stage 2 (JR target from loader)
STAGE2:
	ld sp,D_5FF0		;5db3	31 f0 5f	1 . _
	ld a,007h		;5db6	3e 07		> .
	ld (05c8dh),a		;5db8	32 8d 5c	2 . \
	ld (05c48h),a		;5dbb	32 48 5c	2 H \
	ld bc,000bbh		;5dbe	01 bb 00	. . .
	ld a,0f3h		;5dc1	3e f3		> .
	out (c),a		;5dc3	ed 79		. y
	ld hl,D_5D6B		;5dc5	21 6b 5d	! k ]
	ld de,05b00h		;5dc8	11 00 5b	. . [
	ld bc,00040h		;5dcb	01 40 00	. @ .
	ldir			;5dce	ed b0		. .
	ld a,010h		;5dd0	3e 10		> .
	call R_5E24		;5dd2	cd 24 5e	. $ ^
	ld hl,0c000h		;5dd5	21 00 c0	! . .
	ld b,040h		;5dd8	06 40		. @
	call R_5E18		;5dda	cd 18 5e	. . ^
	ld a,014h		;5ddd	3e 14		> .
	call R_5E24		;5ddf	cd 24 5e	. $ ^
	ld hl,0c000h		;5de2	21 00 c0	! . .
	ld b,040h		;5de5	06 40		. @
	call R_5E18		;5de7	cd 18 5e	. . ^
	ld a,017h		;5dea	3e 17		> .
	call R_5E24		;5dec	cd 24 5e	. $ ^
	ld hl,0d800h		;5def	21 00 d8	! . .
	ld b,008h		;5df2	06 08		. .
	call R_5E18		;5df4	cd 18 5e	. . ^
	ld a,016h		;5df7	3e 16		> .
	call R_5E24		;5df9	cd 24 5e	. $ ^
	ld hl,0c000h		;5dfc	21 00 c0	! . .
	ld b,00ah		;5dff	06 0a		. .
	call R_5E18		;5e01	cd 18 5e	. . ^
	ld a,011h		;5e04	3e 11		> .
	call R_5E24		;5e06	cd 24 5e	. $ ^
	ld hl,0e000h		;5e09	21 00 e0	! . .
	ld b,01eh		;5e0c	06 1e		. .
	call R_5E18		;5e0e	cd 18 5e	. . ^
	ld hl,00000h		;5e11	21 00 00	! . .
	push hl			;5e14	e5		.
	jp 0e000h		;5e15	c3 00 e0	. . .
R_5E18:
	ld de,(05cf4h)		;5e18	ed 5b f4 5c	. [ . \
	ld c,005h		;5e1c	0e 05		. .
	di			;5e1e	f3		.
	call 03d13h		;5e1f	cd 13 3d	. . =
	di			;5e22	f3		.
	ret			;5e23	c9		.
R_5E24:
	ld bc,D_7FFD		;5e24	01 fd 7f	. . .
	out (c),a		;5e27	ed 79		. y
	ret			;5e29	c9		.
	dec c			;5e2a	0d		.
	add a,b			;5e2b	80		.
	dec c			;5e2c	0d		.
	add a,b			;5e2d	80		.
	nop			;5e2e	00		.
	nop			;5e2f	00		.
	ld d,e			;5e30	53		S
	ld e,l			;5e31	5d		]
	nop			;5e32	00		.
	dec c			;5e33	0d		.
	add a,b			;5e34	80		.
	dec c			;5e35	0d		.
	nop			;5e36	00		.
	nop			;5e37	00		.
	nop			;5e38	00		.
	nop			;5e39	00		.
	nop			;5e3a	00		.
	nop			;5e3b	00		.
	nop			;5e3c	00		.
	nop			;5e3d	00		.
	nop			;5e3e	00		.
	nop			;5e3f	00		.
	nop			;5e40	00		.
	nop			;5e41	00		.
	nop			;5e42	00		.
	nop			;5e43	00		.
	nop			;5e44	00		.
	nop			;5e45	00		.
	nop			;5e46	00		.
	nop			;5e47	00		.
	nop			;5e48	00		.
	nop			;5e49	00		.
	nop			;5e4a	00		.
	nop			;5e4b	00		.
	nop			;5e4c	00		.
	nop			;5e4d	00		.
	nop			;5e4e	00		.
	nop			;5e4f	00		.
	nop			;5e50	00		.
	nop			;5e51	00		.
	nop			;5e52	00		.
	nop			;5e53	00		.
	nop			;5e54	00		.
	nop			;5e55	00		.
	nop			;5e56	00		.
	nop			;5e57	00		.
	nop			;5e58	00		.
	nop			;5e59	00		.
	nop			;5e5a	00		.
	nop			;5e5b	00		.
	nop			;5e5c	00		.
	nop			;5e5d	00		.
	nop			;5e5e	00		.
	nop			;5e5f	00		.
	nop			;5e60	00		.
	nop			;5e61	00		.
	nop			;5e62	00		.
	nop			;5e63	00		.
	nop			;5e64	00		.
	nop			;5e65	00		.
	nop			;5e66	00		.
	nop			;5e67	00		.
	nop			;5e68	00		.
	nop			;5e69	00		.
	ld b,l			;5e6a	45		E
	add hl,sp		;5e6b	39		9
	and e			;5e6c	a3		.
	add hl,sp		;5e6d	39		9
	in a,(002h)		;5e6e	db 02		. .
	ld a,h			;5e70	7c		|
	jr c,l5eebh		;5e71	38 78		8 x
	ld e,(hl)		;5e73	5e		^
	ld c,l			;5e74	4d		M
	nop			;5e75	00		.
	ld b,b			;5e76	40		@
	ret nz			;5e77	c0		.
	rlca			;5e78	07		.
	rlca			;5e79	07		.
	rlca			;5e7a	07		.
	rlca			;5e7b	07		.
	inc bc			;5e7c	03		.
	inc bc			;5e7d	03		.
	or b			;5e7e	b0		.
	ld e,(hl)		;5e7f	5e		^
	ld bc,D_7FFD		;5e80	01 fd 7f	. . .
	ld a,017h		;5e83	3e 17		> .
	out (c),a		;5e85	ed 79		. y
	ld hl,0d800h		;5e87	21 00 d8	! . .
	ld de,0c000h		;5e8a	11 00 c0	. . .
	call R_5EB9		;5e8d	cd b9 5e	. . ^
	ld bc,D_7FFD		;5e90	01 fd 7f	. . .
	ld a,010h		;5e93	3e 10		> .
	out (c),a		;5e95	ed 79		. y
	call R_5EB3		;5e97	cd b3 5e	. . ^
	ld b,064h		;5e9a	06 64		. d
	ei			;5e9c	fb		.
l5e9dh:
	halt			;5e9d	76		v
	djnz l5e9dh		;5e9e	10 fd		. .
	ld bc,D_7FFD		;5ea0	01 fd 7f	. . .
	ld a,011h		;5ea3	3e 11		> .
	out (c),a		;5ea5	ed 79		. y
	ld hl,0f51ah		;5ea7	21 1a f5	! . .
	ld de,04000h		;5eaa	11 00 40	. . @
	call 0fcc8h		;5ead	cd c8 fc	. . .
	jp 0c000h		;5eb0	c3 00 c0	. . .
R_5EB3:
	ld hl,0c000h		;5eb3	21 00 c0	! . .
	ld de,D_6000		;5eb6	11 00 60	. . `
R_5EB9:
	di			;5eb9	f3		.
	ld ix,0fff4h		;5eba	dd 21 f4 ff	. ! . .
	add ix,sp		;5ebe	dd 39		. 9
	push de			;5ec0	d5		.
	ld sp,hl		;5ec1	f9		.
	pop bc			;5ec2	c1		.
	ex de,hl		;5ec3	eb		.
	pop bc			;5ec4	c1		.
	dec bc			;5ec5	0b		.
	add hl,bc		;5ec6	09		.
	ex de,hl		;5ec7	eb		.
	pop bc			;5ec8	c1		.
	dec bc			;5ec9	0b		.
	add hl,bc		;5eca	09		.
	sbc hl,de		;5ecb	ed 52		. R
	add hl,de		;5ecd	19		.
	jr c,l5ed2h		;5ece	38 02		8 .
	ld d,h			;5ed0	54		T
	ld e,l			;5ed1	5d		]
l5ed2h:
	lddr			;5ed2	ed b8		. .
	ex de,hl		;5ed4	eb		.
	ld d,(ix+00bh)		;5ed5	dd 56 0b	. V .
	ld e,(ix+00ah)		;5ed8	dd 5e 0a	. ^ .
	ld sp,hl		;5edb	f9		.
	pop hl			;5edc	e1		.
	pop hl			;5edd	e1		.
	pop hl			;5ede	e1		.
	ld b,006h		;5edf	06 06		. .
l5ee1h:
	dec sp			;5ee1	3b		;
	pop af			;5ee2	f1		.
	ld (ix+006h),a		;5ee3	dd 77 06	. w .
	inc ix			;5ee6	dd 23		. #
	djnz l5ee1h		;5ee8	10 f7		. .
	exx			;5eea	d9		.
l5eebh:
	ld d,0bfh		;5eeb	16 bf		. .
	ld bc,01010h		;5eed	01 10 10	. . .
	pop hl			;5ef0	e1		.
l5ef1h:
	dec sp			;5ef1	3b		;
	pop af			;5ef2	f1		.
	exx			;5ef3	d9		.
l5ef4h:
	ld (de),a		;5ef4	12		.
	inc de			;5ef5	13		.
l5ef6h:
	exx			;5ef6	d9		.
l5ef7h:
	add hl,hl		;5ef7	29		)
	djnz l5efch		;5ef8	10 02		. .
	pop hl			;5efa	e1		.
	ld b,c			;5efb	41		A
l5efch:
	jr c,l5ef1h		;5efc	38 f3		8 .
	ld e,001h		;5efe	1e 01		. .
l5f00h:
	ld a,080h		;5f00	3e 80		> .
l5f02h:
	add hl,hl		;5f02	29		)
	djnz l5f07h		;5f03	10 02		. .
	pop hl			;5f05	e1		.
	ld b,c			;5f06	41		A
l5f07h:
	rla			;5f07	17		.
	jr c,l5f02h		;5f08	38 f8		8 .
	cp 003h			;5f0a	fe 03		. .
	jr c,l5f13h		;5f0c	38 05		8 .
	add a,e			;5f0e	83		.
	ld e,a			;5f0f	5f		_
	xor c			;5f10	a9		.
	jr nz,l5f00h		;5f11	20 ed		  .
l5f13h:
	add a,e			;5f13	83		.
	cp 004h			;5f14	fe 04		. .
	jr z,l5f72h		;5f16	28 5a		( Z
	adc a,0ffh		;5f18	ce ff		. .
	cp 002h			;5f1a	fe 02		. .
	exx			;5f1c	d9		.
l5f1dh:
	ld c,a			;5f1d	4f		O
l5f1eh:
	exx			;5f1e	d9		.
	ld a,0bfh		;5f1f	3e bf		> .
	jr c,l5f37h		;5f21	38 14		8 .
l5f23h:
	add hl,hl		;5f23	29		)
	djnz l5f28h		;5f24	10 02		. .
	pop hl			;5f26	e1		.
	ld b,c			;5f27	41		A
l5f28h:
	rla			;5f28	17		.
	jr c,l5f23h		;5f29	38 f8		8 .
	jr z,l5f32h		;5f2b	28 05		( .
	inc a			;5f2d	3c		<
	add a,d			;5f2e	82		.
	jr nc,l5f39h		;5f2f	30 08		0 .
	sub d			;5f31	92		.
l5f32h:
	inc a			;5f32	3c		<
	jr nz,l5f41h		;5f33	20 0c		  .
	ld a,0efh		;5f35	3e ef		> .
l5f37h:
	rrca			;5f37	0f		.
	cp a			;5f38	bf		.
l5f39h:
	add hl,hl		;5f39	29		)
	djnz l5f3eh		;5f3a	10 02		. .
	pop hl			;5f3c	e1		.
	ld b,c			;5f3d	41		A
l5f3eh:
	rla			;5f3e	17		.
	jr c,l5f39h		;5f3f	38 f8		8 .
l5f41h:
	exx			;5f41	d9		.
	ld h,0ffh		;5f42	26 ff		& .
	jr z,l5f4ch		;5f44	28 06		( .
	ld h,a			;5f46	67		g
	dec sp			;5f47	3b		;
	inc a			;5f48	3c		<
	jr z,l5f57h		;5f49	28 0c		( .
	pop af			;5f4b	f1		.
l5f4ch:
	ld l,a			;5f4c	6f		o
	add hl,de		;5f4d	19		.
	ldir			;5f4e	ed b0		. .
	jr l5ef6h		;5f50	18 a4		. .
l5f52h:
	exx			;5f52	d9		.
	rrc d			;5f53	cb 0a		. .
	jr l5ef7h		;5f55	18 a0		. .
l5f57h:
	pop af			;5f57	f1		.
	cp 0e0h			;5f58	fe e0		. .
	jr c,l5f4ch		;5f5a	38 f0		8 .
	rlca			;5f5c	07		.
	xor c			;5f5d	a9		.
	inc a			;5f5e	3c		<
	jr z,l5f52h		;5f5f	28 f1		( .
	sub 010h		;5f61	d6 10		. .
l5f63h:
	ld l,a			;5f63	6f		o
	ld c,a			;5f64	4f		O
	ld h,0ffh		;5f65	26 ff		& .
	add hl,de		;5f67	19		.
	ldi			;5f68	ed a0		. .
	dec sp			;5f6a	3b		;
	pop af			;5f6b	f1		.
	ld (de),a		;5f6c	12		.
	inc hl			;5f6d	23		#
	inc de			;5f6e	13		.
	ld a,(hl)		;5f6f	7e		~
	jr l5ef4h		;5f70	18 82		. .
l5f72h:
	ld a,080h		;5f72	3e 80		> .
l5f74h:
	add hl,hl		;5f74	29		)
	djnz l5f79h		;5f75	10 02		. .
	pop hl			;5f77	e1		.
	ld b,c			;5f78	41		A
l5f79h:
	adc a,a			;5f79	8f		.
	jr nz,l5f95h		;5f7a	20 19		  .
	jr c,l5f74h		;5f7c	38 f6		8 .
	ld a,0fch		;5f7e	3e fc		> .
	jr l5f98h		;5f80	18 16		. .
l5f82h:
	dec sp			;5f82	3b		;
	pop bc			;5f83	c1		.
	ld c,b			;5f84	48		H
	ld b,a			;5f85	47		G
	ccf			;5f86	3f		?
	jr l5f1eh		;5f87	18 95		. .
l5f89h:
	cp 00fh			;5f89	fe 0f		. .
	jr c,l5f82h		;5f8b	38 f5		8 .
	jr nz,l5f1dh		;5f8d	20 8e		  .
	add a,0f4h		;5f8f	c6 f4		. .
	ld sp,ix		;5f91	dd f9		. .
	jr l5fa9h		;5f93	18 14		. .
l5f95h:
	sbc a,a			;5f95	9f		.
	ld a,0efh		;5f96	3e ef		> .
l5f98h:
	add hl,hl		;5f98	29		)
	djnz l5f9dh		;5f99	10 02		. .
	pop hl			;5f9b	e1		.
	ld b,c			;5f9c	41		A
l5f9dh:
	rla			;5f9d	17		.
	jr c,l5f98h		;5f9e	38 f8		8 .
	exx			;5fa0	d9		.
	jr nz,l5f63h		;5fa1	20 c0		  .
	bit 7,a			;5fa3	cb 7f		. .
	jr z,l5f89h		;5fa5	28 e2		( .
	sub 0eah		;5fa7	d6 ea		. .
l5fa9h:
	ex de,hl		;5fa9	eb		.
l5faah:
	pop de			;5faa	d1		.
	ld (hl),e		;5fab	73		s
	inc hl			;5fac	23		#
	ld (hl),d		;5fad	72		r
	inc hl			;5fae	23		#
	dec a			;5faf	3d		=
	jr nz,l5faah		;5fb0	20 f8		  .
	jp nz,0005ch		;5fb2	c2 5c 00	. \ .
	nop			;5fb5	00		.
	nop			;5fb6	00		.
	nop			;5fb7	00		.
	defb 0fdh,0ffh,0afh ;illegal sequence	;5fb8	fd ff af	. . .
	sub b			;5fbb	90		.
	ld e,b			;5fbc	58		X
	ld e,b			;5fbd	58		X
	ld d,c			;5fbe	51		Q
	and (hl)		;5fbf	a6		.
	jr nz,l5fc2h		;5fc0	20 00		  .
l5fc2h:
	inc b			;5fc2	04		.
	rla			;5fc3	17		.
	rrca			;5fc4	0f		.
	ld (bc),a		;5fc5	02		.
	defb 0fdh,0ffh,0afh ;illegal sequence	;5fc6	fd ff af	. . .
	sub b			;5fc9	90		.
	ld b,l			;5fca	45		E
	adc a,b			;5fcb	88		.
	inc b			;5fcc	04		.
	nop			;5fcd	00		.
	defb 0fdh,0ffh,00ch ;illegal sequence	;5fce	fd ff 0c	. . .
	inc c			;5fd1	0c		.
D_5FD2:
	xor (hl)		;5fd2	ae		.
	rst 0			;5fd3	c7		.
	djnz l6021h		;5fd4	10 4b		. K
	pop af			;5fd6	f1		.
	ld b,a			;5fd7	47		G
	jr nc,l5fdah		;5fd8	30 00		0 .
l5fdah:
	ld a,(bc)		;5fda	0a		.
	jr nc,$-121		;5fdb	30 85		0 .
	ld h,d			;5fdd	62		b
	nop			;5fde	00		.
	nop			;5fdf	00		.
;PUSH BC / LD A,#? / AND 7 / OR #10 / LD BC,#7FFD / OUT (C),A / POP BC / RET
;Pages RAM bank (A & 7) into the 0xC000 window, keeping bit 4 (screen)
;and bit 3 (48K ROM) intact. Used by every bank-aware player routine.
BANK_SWITCH:
	push bc			;5fe0	c5		.
	and 007h		;5fe1	e6 07		. .
	or 010h			;5fe3	f6 10		. .
	ld bc,D_7FFD		;5fe5	01 fd 7f	. . .
	out (c),a		;5fe8	ed 79		. y
	pop bc			;5fea	c1		.
	ret			;5feb	c9		.
	ld a,(05b17h)		;5fec	3a 17 5b	: . [
	cp 031h			;5fef	fe 31		. 1
	jr nz,l6037h		;5ff1	20 44		  D
	in a,(0b3h)		;5ff3	db b3		. .
	ld b,000h		;5ff5	06 00		. .
	ld c,a			;5ff7	4f		O
l5ff8h:
	in a,(0b3h)		;5ff8	db b3		. .
	cp c			;5ffa	b9		.
	jr nz,l6037h		;5ffb	20 3a		  :
	djnz l5ff8h		;5ffd	10 f9		. .
	ld a,0c3h		;5fff	3e c3		> .
D_6001:
	add a,d			;6001	82		.
	add a,h			;6002	84		.
	jp R_8486		;6003	c3 86 84	. . .
	jp R_848A		;6006	c3 8a 84	. . .
	jp R_848E		;6009	c3 8e 84	. . .
	jp R_84B2		;600c	c3 b2 84	. . .
	jp R_9316		;600f	c3 16 93	. . .
	jp R_766A		;6012	c3 6a 76	. j v
	jp R_7690		;6015	c3 90 76	. . v
	jp R_85F4		;6018	c3 f4 85	. . .
	jp R_72A0		;601b	c3 a0 72	. . r
	jp 00000h		;601e	c3 00 00	. . .
l6021h:
	jp 00000h		;6021	c3 00 00	. . .
	jp R_9A9B		;6024	c3 9b 9a	. . .
	jp R_769E		;6027	c3 9e 76	. . v
	ret			;602a	c9		.
	nop			;602b	00		.
	nop			;602c	00		.
	jp R_7F80		;602d	c3 80 7f	. . .
	ret			;6030	c9		.
	nop			;6031	00		.
	nop			;6032	00		.
	nop			;6033	00		.
	nop			;6034	00		.
	ld c,b			;6035	48		H
	sub c			;6036	91		.
l6037h:
	ld b,e			;6037	43		C
	sub e			;6038	93		.
M_6039:
	ld sp,00031h		;6039	31 31 00	1 1 .
	nop			;603c	00		.
	nop			;603d	00		.
	nop			;603e	00		.
	nop			;603f	00		.
D_6040:
	cp 0f7h			;6040	fe f7		. .
	ei			;6042	fb		.
	defb 0fdh,0efh,0dfh ;illegal sequence	;6043	fd ef df	. . .
	cp a			;6046	bf		.
	ld a,a			;6047	7f		.
	nop			;6048	00		.
D_6049:
	ld bc,0fe00h		;6049	01 00 fe	. . .
	ld e,d			;604c	5a		Z
	nop			;604d	00		.
	cp 058h			;604e	fe 58		. X
	nop			;6050	00		.
	cp 043h			;6051	fe 43		. C
	nop			;6053	00		.
	cp 056h			;6054	fe 56		. V
	nop			;6056	00		.
	cp 031h			;6057	fe 31		. 1
	nop			;6059	00		.
	cp 032h			;605a	fe 32		. 2
	nop			;605c	00		.
	cp 033h			;605d	fe 33		. 3
	nop			;605f	00		.
	cp 034h			;6060	fe 34		. 4
	nop			;6062	00		.
	cp 035h			;6063	fe 35		. 5
	nop			;6065	00		.
	cp 051h			;6066	fe 51		. Q
	nop			;6068	00		.
	cp 057h			;6069	fe 57		. W
	nop			;606b	00		.
	cp 045h			;606c	fe 45		. E
	nop			;606e	00		.
	cp 052h			;606f	fe 52		. R
	nop			;6071	00		.
	cp 054h			;6072	fe 54		. T
	nop			;6074	00		.
	cp 041h			;6075	fe 41		. A
	nop			;6077	00		.
	cp 053h			;6078	fe 53		. S
	nop			;607a	00		.
	cp 044h			;607b	fe 44		. D
	nop			;607d	00		.
	cp 046h			;607e	fe 46		. F
	nop			;6080	00		.
	cp 047h			;6081	fe 47		. G
	nop			;6083	00		.
	cp 030h			;6084	fe 30		. 0
	nop			;6086	00		.
	cp 039h			;6087	fe 39		. 9
	nop			;6089	00		.
	cp 038h			;608a	fe 38		. 8
	nop			;608c	00		.
	cp 037h			;608d	fe 37		. 7
	nop			;608f	00		.
;TurboSound presence probe RAN HERE at intro time (code since replaced):
;  OUT #FFFD,#F8 / OUT #FFFD,#00 / OUT #BFFD,#BF / IN A,(#FFFD)
;  JP P,+4 -> bit7 of readback == 0 means 'TS present' -> (8EBF)=1
;On a plain 2xAY TurboSound the #BF marker survives -> readback #BF ->
;bit7=1 -> 'no TS' -> 8EBF=0 -> whole dual-chip machinery disabled.
TS_PROBE_OVERLAY:
	cp 036h			;6090	fe 36		. 6
	nop			;6092	00		.
	cp 050h			;6093	fe 50		. P
	nop			;6095	00		.
	cp 04fh			;6096	fe 4f		. O
	nop			;6098	00		.
	cp 049h			;6099	fe 49		. I
	nop			;609b	00		.
	cp 055h			;609c	fe 55		. U
	nop			;609e	00		.
	cp 059h			;609f	fe 59		. Y
	nop			;60a1	00		.
	cp 002h			;60a2	fe 02		. .
	nop			;60a4	00		.
	cp 04ch			;60a5	fe 4c		. L
	nop			;60a7	00		.
	cp 04bh			;60a8	fe 4b		. K
	nop			;60aa	00		.
	cp 04ah			;60ab	fe 4a		. J
	nop			;60ad	00		.
	cp 048h			;60ae	fe 48		. H
	nop			;60b0	00		.
	cp 020h			;60b1	fe 20		.  
	nop			;60b3	00		.
	cp 000h			;60b4	fe 00		. .
D_60B6:
	nop			;60b6	00		.
	cp 04dh			;60b7	fe 4d		. M
	nop			;60b9	00		.
	cp 04eh			;60ba	fe 4e		. N
	nop			;60bc	00		.
	cp 042h			;60bd	fe 42		. B
	nop			;60bf	00		.
	cp 092h			;60c0	fe 92		. .
	im 2			;60c2	ed 5e		. ^
	call R_809E		;60c4	cd 9e 80	. . .
	ld a,(05b2fh)		;60c7	3a 2f 5b	: / [
	cp 041h			;60ca	fe 41		. A
	jr z,l60d2h		;60cc	28 04		( .
	cp 042h			;60ce	fe 42		. B
	jr nz,l60d7h		;60d0	20 05		  .
l60d2h:
	sub 041h		;60d2	d6 41		. A
	ld (05cf6h),a		;60d4	32 f6 5c	2 . \
l60d7h:
	ld a,(05cf6h)		;60d7	3a f6 5c	: . \
	and 003h		;60da	e6 03		. .
	ld (M_9341),a		;60dc	32 41 93	2 A .
	add a,041h		;60df	c6 41		. A
	ld (M_8E75),a		;60e1	32 75 8e	2 u .
	cp 041h			;60e4	fe 41		. A
	call z,R_80DC		;60e6	cc dc 80	. . .
	cp 042h			;60e9	fe 42		. B
	call z,R_80D4		;60eb	cc d4 80	. . .
	cp 043h			;60ee	fe 43		. C
	call z,R_80CA		;60f0	cc ca 80	. . .
	cp 044h			;60f3	fe 44		. D
	call z,R_80C0		;60f5	cc c0 80	. . .
	call R_9129		;60f8	cd 29 91	. ) .
	ld a,(05b07h)		;60fb	3a 07 5b	: . [
	ld (M_9794),a		;60fe	32 94 97	2 . .
	ld a,(05b0fh)		;6101	3a 0f 5b	: . [
	ld (M_9795),a		;6104	32 95 97	2 . .
	ld hl,05b0fh		;6107	21 0f 5b	! . [
	ld a,020h		;610a	3e 20		>  
	ld b,080h		;610c	06 80		. .
	call R_74B6		;610e	cd b6 74	. . t
	ld hl,0d802h		;6111	21 02 d8	! . .
	ld (M_85B2),hl		;6114	22 b2 85	" . .
	call R_8190		;6117	cd 90 81	. . .
	call R_848E		;611a	cd 8e 84	. . .
	call R_735F		;611d	cd 5f 73	. _ s
	call R_851B		;6120	cd 1b 85	. . .
	call R_73E3		;6123	cd e3 73	. . s
	call R_8047		;6126	cd 47 80	. G .
	ld de,D_8E22		;6129	11 22 8e	. " .
	call R_8DD0		;612c	cd d0 8d	. . .
	call R_8083		;612f	cd 83 80	. . .
	ld hl,D_975B		;6132	21 5b 97	! [ .
	ld de,D_6000		;6135	11 00 60	. . `
	ld bc,000c1h		;6138	01 c1 00	. . .
	ldir			;613b	ed b0		. .
	ei			;613d	fb		.
R_613E:
	xor a			;613e	af		.
	ld (M_7670),a		;613f	32 70 76	2 p v
	ld (M_769A),a		;6142	32 9a 76	2 . v
	ld hl,D_7D1A		;6145	21 1a 7d	! . }
	ld (M_61ED),hl		;6148	22 ed 61	" . a
	ld hl,D_755D		;614b	21 5d 75	! ] u
	ld (M_6246),hl		;614e	22 46 62	" F b
	ld a,(M_6039)		;6151	3a 39 60	: 9 `
	cp 031h			;6154	fe 31		. 1
	jp nz,07d04h		;6156	c2 04 7d	. . }
	call R_9A80		;6159	cd 80 9a	. . .
	call R_9A8F		;615c	cd 8f 9a	. . .
	ld a,d			;615f	7a		z
	or e			;6160	b3		.
	jp nz,07d04h		;6161	c2 04 7d	. . }
	ld a,001h		;6164	3e 01		> .
R_6166:
	ld (M_8F17),a		;6166	32 17 8f	2 . .
	ld a,001h		;6169	3e 01		> .
	ld (M_8F16),a		;616b	32 16 8f	2 . .
	ld a,(M_603A)		;616e	3a 3a 60	: : `
	cp 031h			;6171	fe 31		. 1
	jr nz,l6182h		;6173	20 0d		  .
	call R_9A7D		;6175	cd 7d 9a	. } .
	ld a,008h		;6178	3e 08		> .
	call R_9A4A		;617a	cd 4a 9a	. J .
	call R_9A8C		;617d	cd 8c 9a	. . .
	jr z,l618dh		;6180	28 0b		( .
l6182h:
	ld a,0c9h		;6182	3e c9		> .
	ld (M_7670),a		;6184	32 70 76	2 p v
	xor a			;6187	af		.
	ld (M_8F16),a		;6188	32 16 8f	2 . .
	jr l61b4h		;618b	18 27		. '
l618dh:
	ld a,(05b1fh)		;618d	3a 1f 5b	: . [
	ld hl,D_6001		;6190	21 01 60	! . `
	cp 032h			;6193	fe 32		. 2
	jr z,l61a5h		;6195	28 0e		( .
	ld hl,0c002h		;6197	21 02 c0	! . .
	cp 034h			;619a	fe 34		. 4
	jr z,l61a5h		;619c	28 07		( .
	ld hl,D_8005		;619e	21 05 80	! . .
	cp 038h			;61a1	fe 38		. 8
	jr nz,l61b4h		;61a3	20 0f		  .
l61a5h:
	ld (M_9A10),hl		;61a5	22 10 9a	" . .
	call R_9A32		;61a8	cd 32 9a	. 2 .
	call R_9A3E		;61ab	cd 3e 9a	. > .
	ld hl,D_6001		;61ae	21 01 60	! . `
	ld (M_9A10),hl		;61b1	22 10 9a	" . .
l61b4h:
	ld hl,(M_8F16)		;61b4	2a 16 8f	* . .
	ld a,h			;61b7	7c		|
	or l			;61b8	b5		.
	jr nz,R_61D8		;61b9	20 1d		  .
	ld a,(M_8F15)		;61bb	3a 15 8f	: . .
	or a			;61be	b7		.
	jr nz,l61d2h		;61bf	20 11		  .
	inc a			;61c1	3c		<
	ld (M_8F15),a		;61c2	32 15 8f	2 . .
	ld a,0f0h		;61c5	3e f0		> .
	ld (M_9A06),a		;61c7	32 06 9a	2 . .
	ld a,0a0h		;61ca	3e a0		> .
	ld (M_9A07),a		;61cc	32 07 9a	2 . .
	jp R_613E		;61cf	c3 3e 61	. > a
l61d2h:
	call R_7335		;61d2	cd 35 73	. 5 s
	jp R_6271		;61d5	c3 71 62	. q b
R_61D8:
	ei			;61d8	fb		.
	halt			;61d9	76		v
	ld sp,(08efch)		;61da	ed 7b fc 8e	. { . .
	ld hl,00000h		;61de	21 00 00	! . .
	ld (M_7B9A),hl		;61e1	22 9a 7b	" . {
	ld (M_7B9C),hl		;61e4	22 9c 7b	" . {
	ld a,001h		;61e7	3e 01		> .
	ld (M_9A00),a		;61e9	32 00 9a	2 . .
	call R_7C08		;61ec	cd 08 7c	. . |
	xor a			;61ef	af		.
	ld (M_9A00),a		;61f0	32 00 9a	2 . .
	ld de,D_8E22		;61f3	11 22 8e	. " .
	call R_8DD0		;61f6	cd d0 8d	. . .
	call R_8482		;61f9	cd 82 84	. . .
	ld hl,0e100h		;61fc	21 00 e1	! . .
	ld (M_8EFE),hl		;61ff	22 fe 8e	" . .
	ld (hl),000h		;6202	36 00		6 .
	inc hl			;6204	23		#
	ld (hl),000h		;6205	36 00		6 .
	inc hl			;6207	23		#
	ld (hl),002h		;6208	36 02		6 .
	ld bc,00008h		;620a	01 08 00	. . .
	inc hl			;620d	23		#
	ex de,hl		;620e	eb		.
	ld hl,D_8EE2		;620f	21 e2 8e	! . .
	push hl			;6212	e5		.
	ldir			;6213	ed b0		. .
	pop hl			;6215	e1		.
	ld de,D_8EEA		;6216	11 ea 8e	. . .
	ld c,008h		;6219	0e 08		. .
	ldir			;621b	ed b0		. .
R_621D:
	ld a,002h		;621d	3e 02		> .
	ld (M_8ECE),a		;621f	32 ce 8e	2 . .
	ld hl,00000h		;6222	21 00 00	! . .
	ld (M_8F00),hl		;6225	22 00 8f	" . .
	ld hl,0c009h		;6228	21 09 c0	! . .
R_622B:
	ld (M_85AE),hl		;622b	22 ae 85	" . .
	ld hl,00000h		;622e	21 00 00	! . .
	ld (M_831D),hl		;6231	22 1d 83	" . .
	ld hl,0c001h		;6234	21 01 c0	! . .
	ld (M_8ED8),hl		;6237	22 d8 8e	" . .
M_623A:
	ld hl,D_8EEA		;623a	21 ea 8e	! . .
	ld de,04020h		;623d	11 20 40	.   @
	ld b,006h		;6240	06 06		. .
	call R_85BC		;6242	cd bc 85	. . .
	call R_7C7B		;6245	cd 7b 7c	. { |
	ld b,020h		;6248	06 20		.  
	ld hl,(M_8ED8)		;624a	2a d8 8e	* . .
	dec hl			;624d	2b		+
	call R_74B5		;624e	cd b5 74	. . t
	ld hl,0bff4h		;6251	21 f4 bf	! . .
	ld de,00015h		;6254	11 15 00	. . .
	ld bc,0fffeh		;6257	01 fe ff	. . .
	xor a			;625a	af		.
l625bh:
	add hl,de		;625b	19		.
	inc bc			;625c	03		.
	cp (hl)			;625d	be		.
	jr nz,l625bh		;625e	20 fb		  .
	ld h,b			;6260	60		`
	ld l,c			;6261	69		i
	ld de,D_8E2B		;6262	11 2b 8e	. + .
	call R_92E8		;6265	cd e8 92	. . .
	call R_8C1D		;6268	cd 1d 8c	. . .
R_626B:
	call R_852C		;626b	cd 2c 85	. , .
R_626E:
	call R_8BBB		;626e	cd bb 8b	. . .
R_6271:
	ld a,038h		;6271	3e 38		> 8
	call R_80ED		;6273	cd ed 80	. . .
	call R_8BF9		;6276	cd f9 8b	. . .
	xor a			;6279	af		.
	ld (M_8F19),a		;627a	32 19 8f	2 . .
	call R_8655		;627d	cd 55 86	. U .
	call DUAL_CHIP_INIT	;6280	cd ef 86	. . .
	ei			;6283	fb		.
	halt			;6284	76		v
	ld a,(M_8110)		;6285	3a 10 81	: . .
	call R_80ED		;6288	cd ed 80	. . .
	call R_8282		;628b	cd 82 82	. . .
	call R_8BB6		;628e	cd b6 8b	. . .
	call R_89EB		;6291	cd eb 89	. . .
	call nz,08f2dh		;6294	c4 2d 8f	. - .
	ld a,(M_8ED5)		;6297	3a d5 8e	: . .
	or a			;629a	b7		.
	jp z,l6491h		;629b	ca 91 64	. . d
	ld hl,(M_8F16)		;629e	2a 16 8f	* . .
	ld a,h			;62a1	7c		|
	or l			;62a2	b5		.
	jr z,l62afh		;62a3	28 0a		( .
	call R_89DF		;62a5	cd df 89	. . .
	jr z,l62afh		;62a8	28 05		( .
	call R_73B9		;62aa	cd b9 73	. . s
	jr R_6271		;62ad	18 c2		. .
l62afh:
	call R_89D3		;62af	cd d3 89	. . .
	call nz,R_7307		;62b2	c4 07 73	. . s
	call R_89D7		;62b5	cd d7 89	. . .
	call nz,R_7320		;62b8	c4 20 73	.   s
	call R_8A07		;62bb	cd 07 8a	. . .
	call nz,07411h		;62be	c4 11 74	. . t
	call R_8A0B		;62c1	cd 0b 8a	. . .
	call nz,0741bh		;62c4	c4 1b 74	. . t
	call R_8A0F		;62c7	cd 0f 8a	. . .
	call nz,07425h		;62ca	c4 25 74	. % t
	call R_8A13		;62cd	cd 13 8a	. . .
	call nz,0742fh		;62d0	c4 2f 74	. / t
	call R_89EF		;62d3	cd ef 89	. . .
	call nz,08af8h		;62d6	c4 f8 8a	. . .
	call R_8A39		;62d9	cd 39 8a	. 9 .
	jp z,R_72BE		;62dc	ca be 72	. . r
	ld a,001h		;62df	3e 01		> .
	ld (M_8EC2),a		;62e1	32 c2 8e	2 . .
	call MUTE_BOTH		;62e4	cd 61 8a	. a .
	call R_8600		;62e7	cd 00 86	. . .
	call R_8482		;62ea	cd 82 84	. . .
	ld de,0d802h		;62ed	11 02 d8	. . .
	add hl,de		;62f0	19		.
	ld de,05b02h		;62f1	11 02 5b	. . [
	ld bc,00008h		;62f4	01 08 00	. . .
	ldir			;62f7	ed b0		. .
	ld a,02eh		;62f9	3e 2e		> .
	ld (de),a		;62fb	12		.
	inc e			;62fc	1c		.
	ld a,(hl)		;62fd	7e		~
	ld c,003h		;62fe	0e 03		. .
	ldir			;6300	ed b0		. .
	cp 042h			;6302	fe 42		. B
	jp nz,l6307h		;6304	c2 07 63	. . c
l6307h:
	ld a,(hl)		;6307	7e		~
	dec a			;6308	3d		=
	jp z,R_6B5E		;6309	ca 5e 6b	. ^ k
	jp m,R_6B5E		;630c	fa 5e 6b	. ^ k
	cp 060h			;630f	fe 60		. `
	jp nc,R_6B5E		;6311	d2 5e 6b	. ^ k
	ld (M_6328),a		;6314	32 28 63	2 ( c
	ld de,VTII_INSTANCE	;6317	11 00 a0	. . .
	ld a,00eh		;631a	3e 0e		> .
	call R_9241		;631c	cd 41 92	. A .
	ld a,(M_9345)		;631f	3a 45 93	: E .
	or a			;6322	b7		.
	jp nz,R_6B5E		;6323	c2 5e 6b	. ^ k
	ld bc,05301h		;6326	01 01 53	. . S
	ld a,0a1h		;6329	3e a1		> .
	add a,b			;632b	80		.
	ld h,a			;632c	67		g
	ld l,c			;632d	69		i
l632eh:
	ld (hl),000h		;632e	36 00		6 .
	inc hl			;6330	23		#
	ld a,h			;6331	7c		|
	or l			;6332	b5		.
	jr nz,l632eh		;6333	20 f9		  .
	jp R_69B9		;6335	c3 b9 69	. . i
l6338h:
	ld a,0d2h		;6338	3e d2		> .
	call R_6456		;633a	cd 56 64	. V d
	xor a			;633d	af		.
	ld (M_8EC9),a		;633e	32 c9 8e	2 . .
	ld (M_8EC8),a		;6341	32 c8 8e	2 . .
	ld (M_8ECA),a		;6344	32 ca 8e	2 . .
	jp R_6271		;6347	c3 71 62	. q b
l634ah:
	jp R_6B5E		;634a	c3 5e 6b	. ^ k
	call R_8BBB		;634d	cd bb 8b	. . .
	xor a			;6350	af		.
	ld (M_8EC1),a		;6351	32 c1 8e	2 . .
	ld (M_8EC7),a		;6354	32 c7 8e	2 . .
	ld (M_8ECA),a		;6357	32 ca 8e	2 . .
	inc a			;635a	3c		<
	ld (M_8EC8),a		;635b	32 c8 8e	2 . .
	ld (M_8EC9),a		;635e	32 c9 8e	2 . .
	call MUTE_BOTH		;6361	cd 61 8a	. a .
	call R_75A6		;6364	cd a6 75	. . u
	xor a			;6367	af		.
	call R_6456		;6368	cd 56 64	. V d
	ld a,0f3h		;636b	3e f3		> .
	call R_6456		;636d	cd 56 64	. V d
	ld a,023h		;6370	3e 23		> #
	call R_6456		;6372	cd 56 64	. V d
	call R_646C		;6375	cd 6c 64	. l d
	ex af,af'		;6378	08		.
	ld a,030h		;6379	3e 30		> 0
	call R_6456		;637b	cd 56 64	. V d
	ld a,0d1h		;637e	3e d1		> .
	call R_6456		;6380	cd 56 64	. V d
	ex af,af'		;6383	08		.
	ld de,00006h		;6384	11 06 00	. . .
	ld h,d			;6387	62		b
	ld l,a			;6388	6f		o
	add hl,hl		;6389	29		)
	add hl,hl		;638a	29		)
	add hl,hl		;638b	29		)
	add hl,hl		;638c	29		)
	add hl,de		;638d	19		.
	ld a,(D_803E)		;638e	3a 3e 80	: > .
	cp h			;6391	bc		.
	jr nz,l639ch		;6392	20 08		  .
	ld b,a			;6394	47		G
	ld a,(M_803D)		;6395	3a 3d 80	: = .
	cp l			;6398	bd		.
	ld a,b			;6399	78		x
	jr z,l639eh		;639a	28 02		( .
l639ch:
	jr nc,l6338h		;639c	30 9a		0 .
l639eh:
	or a			;639e	b7		.
	jr z,l63abh		;639f	28 0a		( .
l63a1h:
	push af			;63a1	f5		.
	ld b,000h		;63a2	06 00		. .
	call R_63EF		;63a4	cd ef 63	. . c
	pop af			;63a7	f1		.
	dec a			;63a8	3d		=
	jr nz,l63a1h		;63a9	20 f6		  .
l63abh:
	ld a,(M_803D)		;63ab	3a 3d 80	: = .
	ld b,a			;63ae	47		G
	call R_63EF		;63af	cd ef 63	. . c
	call R_75A3		;63b2	cd a3 75	. . u
	ld a,0d2h		;63b5	3e d2		> .
	call R_6456		;63b7	cd 56 64	. V d
	in a,(0b3h)		;63ba	db b3		. .
	out (0b3h),a		;63bc	d3 b3		. .
	ld a,031h		;63be	3e 31		> 1
	call R_6456		;63c0	cd 56 64	. V d
	ld hl,(M_8F0B)		;63c3	2a 0b 8f	* . .
	ld de,M_7B9A		;63c6	11 9a 7b	. . {
	ld bc,00004h		;63c9	01 04 00	. . .
	ldir			;63cc	ed b0		. .
	ld hl,(M_8F0B)		;63ce	2a 0b 8f	* . .
	call R_9A9B		;63d1	cd 9b 9a	. . .
	ld hl,VTII_INSTANCE	;63d4	21 00 a0	! . .
	ld b,001h		;63d7	06 01		. .
	call R_769E		;63d9	cd 9e 76	. . v
	call R_8053		;63dc	cd 53 80	. S .
	ld de,0400ch		;63df	11 0c 40	. . @
	ld hl,VTII_INSTANCE	;63e2	21 00 a0	! . .
	ld b,00ch		;63e5	06 0c		. .
	ld a,011h		;63e7	3e 11		> .
	call R_85B6		;63e9	cd b6 85	. . .
	jp R_6B5E		;63ec	c3 5e 6b	. ^ k
R_63EF:
	di			;63ef	f3		.
	xor a			;63f0	af		.
	ld (M_7B59),a		;63f1	32 59 7b	2 Y {
l63f4h:
	push bc			;63f4	c5		.
	ld hl,VTII_INSTANCE	;63f5	21 00 a0	! . .
	ld b,001h		;63f8	06 01		. .
	call R_769E		;63fa	cd 9e 76	. . v
	call R_7589		;63fd	cd 89 75	. . u
	ld hl,VTII_INSTANCE	;6400	21 00 a0	! . .
	ld bc,000bbh		;6403	01 bb 00	. . .
	ld e,002h		;6406	1e 02		. .
l6408h:
	in a,(c)		;6408	ed 78		. x
	jp p,l6412h		;640a	f2 12 64	. . d
	in a,(c)		;640d	ed 78		. x
	jp m,l6408h		;640f	fa 08 64	. . d
l6412h:
	ld a,(hl)		;6412	7e		~
	out (0b3h),a		;6413	d3 b3		. .
	inc l			;6415	2c		,
l6416h:
	in a,(c)		;6416	ed 78		. x
	jp p,l6420h		;6418	f2 20 64	.   d
	in a,(c)		;641b	ed 78		. x
	jp m,l6416h		;641d	fa 16 64	. . d
l6420h:
	ld a,(hl)		;6420	7e		~
	out (0b3h),a		;6421	d3 b3		. .
	inc hl			;6423	23		#
l6424h:
	in a,(c)		;6424	ed 78		. x
	jp p,l642eh		;6426	f2 2e 64	. . d
	in a,(c)		;6429	ed 78		. x
	jp m,l6424h		;642b	fa 24 64	. $ d
l642eh:
	ld a,(hl)		;642e	7e		~
	out (0b3h),a		;642f	d3 b3		. .
	inc hl			;6431	23		#
l6432h:
	in a,(c)		;6432	ed 78		. x
	jp p,l643ch		;6434	f2 3c 64	. < d
	in a,(c)		;6437	ed 78		. x
	jp m,l6432h		;6439	fa 32 64	. 2 d
l643ch:
	ld a,(hl)		;643c	7e		~
	out (0b3h),a		;643d	d3 b3		. .
	inc hl			;643f	23		#
	djnz l6408h		;6440	10 c6		. .
	dec e			;6442	1d		.
	jr nz,l6408h		;6443	20 c3		  .
	ld a,0fdh		;6445	3e fd		> .
	in a,(0feh)		;6447	db fe		. .
	rrca			;6449	0f		.
	jr c,l6451h		;644a	38 05		8 .
	ld a,001h		;644c	3e 01		> .
	ld (M_7B59),a		;644e	32 59 7b	2 Y {
l6451h:
	pop bc			;6451	c1		.
	djnz l63f4h		;6452	10 a0		. .
	ei			;6454	fb		.
	ret			;6455	c9		.
R_6456:
	ret			;6456	c9		.
	out (0bbh),a		;6457	d3 bb		. .
l6459h:
	in a,(0bbh)		;6459	db bb		. .
	rrca			;645b	0f		.
	jr c,l6459h		;645c	38 fb		8 .
	ret			;645e	c9		.
R_645F:
	in a,(0bbh)		;645f	db bb		. .
	rrca			;6461	0f		.
	ret			;6462	c9		.
R_6463:
	ret			;6463	c9		.
	out (0b3h),a		;6464	d3 b3		. .
l6466h:
	in a,(0bbh)		;6466	db bb		. .
	rlca			;6468	07		.
	jr c,l6466h		;6469	38 fb		8 .
	ret			;646b	c9		.
R_646C:
	in a,(0bbh)		;646c	db bb		. .
	rlca			;646e	07		.
	jr nc,R_646C		;646f	30 fb		0 .
	in a,(0b3h)		;6471	db b3		. .
	ret			;6473	c9		.
l6474h:
	ld a,(M_9A02)		;6474	3a 02 9a	: . .
	or a			;6477	b7		.
	jp nz,R_6271		;6478	c2 71 62	. q b
	call R_9A35		;647b	cd 35 9a	. 5 .
	jp R_61D8		;647e	c3 d8 61	. . a
l6481h:
	call R_7307		;6481	cd 07 73	. . s
	jr l6489h		;6484	18 03		. .
l6486h:
	call R_7320		;6486	cd 20 73	.   s
l6489h:
	ld a,001h		;6489	3e 01		> .
	ld (M_8C2A),a		;648b	32 2a 8c	2 * .
	jp R_6271		;648e	c3 71 62	. q b
l6491h:
	call R_89DB		;6491	cd db 89	. . .
	jr z,l649ch		;6494	28 06		( .
	call R_7335		;6496	cd 35 73	. 5 s
	jp R_6271		;6499	c3 71 62	. q b
l649ch:
	ld a,(M_8ED6)		;649c	3a d6 8e	: . .
	or a			;649f	b7		.
	jp nz,093f6h		;64a0	c2 f6 93	. . .
	call R_8A39		;64a3	cd 39 8a	. 9 .
	jp nz,R_654B		;64a6	c2 4b 65	. K e
	call R_8931		;64a9	cd 31 89	. 1 .
	jp z,l6508h		;64ac	ca 08 65	. . e
	call R_8959		;64af	cd 59 89	. Y .
	jr z,l64d8h		;64b2	28 24		( $
	call R_89B4		;64b4	cd b4 89	. . .
	jr nz,l64d8h		;64b7	20 1f		  .
	call R_8A0F		;64b9	cd 0f 8a	. . .
	call nz,08a8fh		;64bc	c4 8f 8a	. . .
	call R_8A13		;64bf	cd 13 8a	. . .
	call nz,08ad0h		;64c2	c4 d0 8a	. . .
	call R_89DF		;64c5	cd df 89	. . .
	call nz,08abah		;64c8	c4 ba 8a	. . .
	call R_89D3		;64cb	cd d3 89	. . .
	jr nz,l6481h		;64ce	20 b1		  .
	call R_89D7		;64d0	cd d7 89	. . .
	jr nz,l6486h		;64d3	20 b1		  .
	jp R_72BE		;64d5	c3 be 72	. . r
l64d8h:
	call R_89B4		;64d8	cd b4 89	. . .
	jp nz,08b61h		;64db	c2 61 8b	. a .
	call R_8A4D		;64de	cd 4d 8a	. M .
	call nz,07f50h		;64e1	c4 50 7f	. P .
	call R_8A51		;64e4	cd 51 8a	. Q .
	call nz,07f5ah		;64e7	c4 5a 7f	. Z .
	call R_89F7		;64ea	cd f7 89	. . .
	jp nz,l6542h		;64ed	c2 42 65	. B e
	call R_89FB		;64f0	cd fb 89	. . .
	jp nz,l6474h		;64f3	c2 74 64	. t d
	call R_8A07		;64f6	cd 07 8a	. . .
	call nz,R_766A		;64f9	c4 6a 76	. j v
	call R_8A0B		;64fc	cd 0b 8a	. . .
	call nz,R_7690		;64ff	c4 90 76	. . v
	call R_8A0F		;6502	cd 0f 8a	. . .
	call nz,0768ch		;6505	c4 8c 76	. . v
l6508h:
	ld a,(M_8EC8)		;6508	3a c8 8e	: . .
	or a			;650b	b7		.
	jp z,R_72BE		;650c	ca be 72	. . r
	ld a,(M_8EC9)		;650f	3a c9 8e	: . .
	or a			;6512	b7		.
	jp z,R_72BE		;6513	ca be 72	. . r
	call R_645F		;6516	cd 5f 64	. _ d
	jp c,R_72BE		;6519	da be 72	. . r
	ld a,(M_8ECA)		;651c	3a ca 8e	: . .
	or a			;651f	b7		.
	jr nz,l652ch		;6520	20 0a		  .
	ld a,060h		;6522	3e 60		> `
	out (0bbh),a		;6524	d3 bb		. .
	ld (M_8ECA),a		;6526	32 ca 8e	2 . .
	jp R_72BE		;6529	c3 be 72	. . r
l652ch:
	xor a			;652c	af		.
	ld (M_8ECA),a		;652d	32 ca 8e	2 . .
	in a,(0b3h)		;6530	db b3		. .
	ld hl,M_8EC7		;6532	21 c7 8e	! . .
	cp (hl)			;6535	be		.
	ld (hl),a		;6536	77		w
	jp nc,R_72BE		;6537	d2 be 72	. . r
l653ah:
	call R_7320		;653a	cd 20 73	.   s
	call R_8282		;653d	cd 82 82	. . .
	jr R_654B		;6540	18 09		. .
l6542h:
	ld a,(M_8EC8)		;6542	3a c8 8e	: . .
	or a			;6545	b7		.
	jp z,R_72BE		;6546	ca be 72	. . r
	jr l653ah		;6549	18 ef		. .
R_654B:
	call R_85F4		;654b	cd f4 85	. . .
	jp z,07217h		;654e	ca 17 72	. . r
	ld (M_8F0B),hl		;6551	22 0b 8f	" . .
	call R_9A9B		;6554	cd 9b 9a	. . .
	call R_860E		;6557	cd 0e 86	. . .
	ld de,0c011h		;655a	11 11 c0	. . .
	add hl,de		;655d	19		.
	ld bc,00008h		;655e	01 08 00	. . .
	ld a,02eh		;6561	3e 2e		> .
	cpdr			;6563	ed b9		. .
	jp nz,R_698F		;6565	c2 8f 69	. . i
	inc hl			;6568	23		#
	inc hl			;6569	23		#
	ld de,D_6979		;656a	11 79 69	. y i
	ld c,000h		;656d	0e 00		. .
l656fh:
	push hl			;656f	e5		.
	ld b,003h		;6570	06 03		. .
l6572h:
	ld a,(de)		;6572	1a		.
	cp (hl)			;6573	be		.
	jr nz,l6595h		;6574	20 1f		  .
	inc hl			;6576	23		#
	inc de			;6577	13		.
	djnz l6572h		;6578	10 f8		. .
	pop hl			;657a	e1		.
	ld a,c			;657b	79		y
	or a			;657c	b7		.
	jp z,l634ah		;657d	ca 4a 63	. J c
	dec a			;6580	3d		=
	jr z,l65a5h		;6581	28 22		( "
	dec a			;6583	3d		=
	jr z,l65a5h		;6584	28 1f		( .
	dec a			;6586	3d		=
	jp z,06888h		;6587	ca 88 68	. . h
	dec a			;658a	3d		=
	jp z,0669ch		;658b	ca 9c 66	. . f
	dec a			;658e	3d		=
	jp z,0669ch		;658f	ca 9c 66	. . f
	jp R_698F		;6592	c3 8f 69	. . i
l6595h:
	ex de,hl		;6595	eb		.
	ld d,000h		;6596	16 00		. .
	ld e,b			;6598	58		X
	add hl,de		;6599	19		.
	ex de,hl		;659a	eb		.
	pop hl			;659b	e1		.
	inc c			;659c	0c		.
	ld a,c			;659d	79		y
	cp 006h			;659e	fe 06		. .
	jp nc,R_698F		;65a0	d2 8f 69	. . i
	jr l656fh		;65a3	18 ca		. .
l65a5h:
	ld a,(R_6456)		;65a5	3a 56 64	: V d
	or a			;65a8	b7		.
	jp nz,R_6B5E		;65a9	c2 5e 6b	. ^ k
	call R_8482		;65ac	cd 82 84	. . .
	ld bc,0fefeh		;65af	01 fe fe	. . .
	in a,(c)		;65b2	ed 78		. x
	and 007h		;65b4	e6 07		. .
	cp 007h			;65b6	fe 07		. .
	jr z,l65d3h		;65b8	28 19		( .
	ld de,00102h		;65ba	11 02 01	. . .
	rrca			;65bd	0f		.
	jr nc,l65c3h		;65be	30 03		0 .
	ld de,00403h		;65c0	11 03 04	. . .
l65c3h:
	ld hl,D_7E23		;65c3	21 23 7e	! # ~
	rrca			;65c6	0f		.
	jr nc,l65cch		;65c7	30 03		0 .
	ld hl,00000h		;65c9	21 00 00	! . .
l65cch:
	ld (0c903h),de		;65cc	ed 53 03 c9	. S . .
	ld (0c905h),hl		;65d0	22 05 c9	" . .
l65d3h:
	ld a,0f3h		;65d3	3e f3		> .
	call R_6456		;65d5	cd 56 64	. V d
	ld a,000h		;65d8	3e 00		> .
	out (0b3h),a		;65da	d3 b3		. .
	ld a,014h		;65dc	3e 14		> .
	call R_6456		;65de	cd 56 64	. V d
	ld a,002h		;65e1	3e 02		> .
	call R_6463		;65e3	cd 63 64	. c d
	ld a,000h		;65e6	3e 00		> .
	call R_6463		;65e8	cd 63 64	. c d
	ld a,040h		;65eb	3e 40		> @
	call R_6463		;65ed	cd 63 64	. c d
	ld hl,0c900h		;65f0	21 00 c9	! . .
	ld bc,00200h		;65f3	01 00 02	. . .
l65f6h:
	push bc			;65f6	c5		.
	ld a,(hl)		;65f7	7e		~
	call R_6463		;65f8	cd 63 64	. c d
	inc hl			;65fb	23		#
	pop bc			;65fc	c1		.
	dec bc			;65fd	0b		.
	ld a,b			;65fe	78		x
	or c			;65ff	b1		.
	jr nz,l65f6h		;6600	20 f4		  .
	xor a			;6602	af		.
	out (0b3h),a		;6603	d3 b3		. .
	ld a,013h		;6605	3e 13		> .
	call R_6456		;6607	cd 56 64	. V d
	ld a,040h		;660a	3e 40		> @
	call R_6463		;660c	cd 63 64	. c d
	xor a			;660f	af		.
	out (0bbh),a		;6610	d3 bb		. .
	in a,(0b3h)		;6612	db b3		. .
	xor a			;6614	af		.
	ld (M_6978),a		;6615	32 78 69	2 x i
	ld a,(D_803E)		;6618	3a 3e 80	: > .
	or a			;661b	b7		.
	jr z,l6635h		;661c	28 17		( .
	ld b,a			;661e	47		G
l661fh:
	ld c,008h		;661f	0e 08		. .
l6621h:
	push bc			;6621	c5		.
	ld b,020h		;6622	06 20		.  
	call R_63EF		;6624	cd ef 63	. . c
	pop bc			;6627	c1		.
	ld a,(M_7B59)		;6628	3a 59 7b	: Y {
	or a			;662b	b7		.
	ld a,0feh		;662c	3e fe		> .
	jr nz,l6646h		;662e	20 16		  .
	dec c			;6630	0d		.
	jr nz,l6621h		;6631	20 ee		  .
	djnz l661fh		;6633	10 ea		. .
l6635h:
	ld a,(M_803D)		;6635	3a 3d 80	: = .
	or a			;6638	b7		.
	jr z,l6644h		;6639	28 09		( .
	ld b,a			;663b	47		G
	call R_63EF		;663c	cd ef 63	. . c
	ld a,001h		;663f	3e 01		> .
	ld (M_6978),a		;6641	32 78 69	2 x i
l6644h:
	ld a,0ffh		;6644	3e ff		> .
l6646h:
	out (0bbh),a		;6646	d3 bb		. .
l6648h:
	in a,(0bbh)		;6648	db bb		. .
	rrca			;664a	0f		.
	jr c,l6648h		;664b	38 fb		8 .
	ld a,(M_6978)		;664d	3a 78 69	: x i
	or a			;6650	b7		.
	jp z,R_6B5E		;6651	ca 5e 6b	. ^ k
R_6654:
	call R_7320		;6654	cd 20 73	.   s
	call R_8282		;6657	cd 82 82	. . .
	call R_8BBB		;665a	cd bb 8b	. . .
	call R_8C07		;665d	cd 07 8c	. . .
	jp R_654B		;6660	c3 4b 65	. K e
R_6663:
	ld a,(hl)		;6663	7e		~
	inc hl			;6664	23		#
	or a			;6665	b7		.
	ret z			;6666	c8		.
	cp 00dh			;6667	fe 0d		. .
	ret z			;6669	c8		.
	djnz R_6663		;666a	10 f7		. .
	or a			;666c	b7		.
	ret			;666d	c9		.
R_666E:
	ld de,D_6688		;666e	11 88 66	. . f
l6671h:
	ld a,(hl)		;6671	7e		~
	or a			;6672	b7		.
	ret z			;6673	c8		.
	ex de,hl		;6674	eb		.
	cp (hl)			;6675	be		.
	ex de,hl		;6676	eb		.
	inc hl			;6677	23		#
	jr z,l667eh		;6678	28 04		( .
	djnz R_666E		;667a	10 f2		. .
	xor a			;667c	af		.
	ret			;667d	c9		.
l667eh:
	inc de			;667e	13		.
	ld a,(de)		;667f	1a		.
	or a			;6680	b7		.
	jr nz,l6671h		;6681	20 ee		  .
	dec hl			;6683	2b		+
	dec hl			;6684	2b		+
	dec hl			;6685	2b		+
	dec a			;6686	3d		=
	ret			;6687	c9		.
D_6688:
; --------------------------------------------------------------------------
; DATA 0x6688-0x66A5  'ZX Spectrum Sound C...' info string
; --------------------------------------------------------------------------
DATA_6688:
	defb 5A 58 20 53 70 65 63 74  ;6688  ZX Spect
	defb 72 75 6D 20 53 6F 75 6E  ;6690  rum Soun
	defb 64 20 43 00 CD B2 84 21  ;6698  d C....!
	defb 00 A1 E5 06 02 CD        ;66A0  ......


; ..........................................................................
; code segment restart 0x66A6 - clean decode boundary after data above
; ..........................................................................
;Mute both chips: OUT #FFFD,#FE + R8/R9/R10:=0, then OUT #FFFD,#FF +
;R8/R9/R10:=0. In the captured run this is the ONLY code that ever
;touched chip 2 after the failed probe (9 writes total).
;TS-present flag written by the intro probe (1=present).
;0 => every TS setup path (6888 / 6D09 pattern: OR A / JP Z) is
;skipped and TS_MODE_FLAG stays 0 forever.
;TS engine install parameters filled in when 8EBF=1
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).

	sbc a,(hl)		;66a6	9e		.
	halt			;66a7	76		v
	pop hl			;66a8	e1		.
	ld b,001h		;66a9	06 01		. .
	call R_666E		;66ab	cd 6e 66	. n f
	ld hl,D_A100		;66ae	21 00 a1	! . .
	jr nz,l66b6h		;66b1	20 03		  .
	or a			;66b3	b7		.
	jr z,l66c4h		;66b4	28 0e		( .
l66b6h:
	ld b,000h		;66b6	06 00		. .
	call R_6663		;66b8	cd 63 66	. c f
	jp nz,l675eh		;66bb	c2 5e 67	. ^ g
	ld a,(hl)		;66be	7e		~
	cp 00ah			;66bf	fe 0a		. .
	jr nz,l66c4h		;66c1	20 01		  .
	inc hl			;66c3	23		#
l66c4h:
	ld (M_6876),hl		;66c4	22 76 68	" v h
	ld b,000h		;66c7	06 00		. .
	call R_6663		;66c9	cd 63 66	. c f
	jp nz,l675eh		;66cc	c2 5e 67	. ^ g
	or a			;66cf	b7		.
	jr z,l66e3h		;66d0	28 11		( .
	ld a,(hl)		;66d2	7e		~
	cp 00ah			;66d3	fe 0a		. .
	jr nz,l66d8h		;66d5	20 01		  .
	inc hl			;66d7	23		#
l66d8h:
	ld a,(hl)		;66d8	7e		~
	cp 00dh			;66d9	fe 0d		. .
	jr z,l66f1h		;66db	28 14		( .
	or a			;66dd	b7		.
	jr z,l66f1h		;66de	28 11		( .
	jp R_8F24		;66e0	c3 24 8f	. $ .
l66e3h:
	dec hl			;66e3	2b		+
	dec hl			;66e4	2b		+
	ld a,(hl)		;66e5	7e		~
	cp 00ah			;66e6	fe 0a		. .
	jr z,l66f1h		;66e8	28 07		( .
	cp 00dh			;66ea	fe 0d		. .
	jr z,l66f1h		;66ec	28 03		( .
	inc hl			;66ee	23		#
	ld (hl),00dh		;66ef	36 0d		6 .
l66f1h:
	ld hl,(M_7B51)		;66f1	2a 51 7b	* Q {
	ld de,(M_7B53)		;66f4	ed 5b 53 7b	. [ S {
R_66F8:
	ld (M_6878),hl		;66f8	22 78 68	" x h
	ld (l687ah),de		;66fb	ed 53 7a 68	. S z h
	ld (M_7B9A),hl		;66ff	22 9a 7b	" . {
	ld (M_7B9C),de		;6702	ed 53 9c 7b	. S . {
	ld hl,M_7B51		;6706	21 51 7b	! Q {
	call R_9A9B		;6709	cd 9b 9a	. . .
	xor a			;670c	af		.
	ld (0afe0h),a		;670d	32 e0 af	2 . .
	ld (M_687C),a		;6710	32 7c 68	2 | h
l6713h:
	ld hl,0b000h		;6713	21 00 b0	! . .
	push hl			;6716	e5		.
	ld b,001h		;6717	06 01		. .
	call R_769E		;6719	cd 9e 76	. . v
	pop hl			;671c	e1		.
	ld a,(M_687C)		;671d	3a 7c 68	: | h
	or a			;6720	b7		.
	jr z,l6725h		;6721	28 02		( .
	ld h,0afh		;6723	26 af		& .
l6725h:
	ld de,(M_6876)		;6725	ed 5b 76 68	. [ v h
	ld bc,0001fh		;6729	01 1f 00	. . .
l672ch:
	ld a,(hl)		;672c	7e		~
	or a			;672d	b7		.
	jr z,l675eh		;672e	28 2e		( .
	cp 0e5h			;6730	fe e5		. .
	jr z,l6761h		;6732	28 2d		( -
	ld a,(de)		;6734	1a		.
	cp 061h			;6735	fe 61		. a
	jr c,l673bh		;6737	38 02		8 .
	sub 020h		;6739	d6 20		.  
l673bh:
	cp (hl)			;673b	be		.
	inc hl			;673c	23		#
	jr z,l676dh		;673d	28 2e		( .
l673fh:
	ld a,(de)		;673f	1a		.
	cp (hl)			;6740	be		.
	jr z,l67a2h		;6741	28 5f		( _
R_6743:
	add hl,bc		;6743	09		.
	ld a,h			;6744	7c		|
	cp 0b7h			;6745	fe b7		. .
	jr c,l672ch		;6747	38 e3		8 .
	ld hl,0b000h		;6749	21 00 b0	! . .
	ld de,D_A800		;674c	11 00 a8	. . .
	ld bc,00800h		;674f	01 00 08	. . .
	ldir			;6752	ed b0		. .
	ld (M_687C),a		;6754	32 7c 68	2 | h
	ld a,(M_9A04)		;6757	3a 04 9a	: . .
	cp 00fh			;675a	fe 0f		. .
	jr nz,l6713h		;675c	20 b5		  .
l675eh:
	jp R_6B5E		;675e	c3 5e 6b	. ^ k
l6761h:
	inc hl			;6761	23		#
	jr R_6743		;6762	18 df		. .
l6764h:
	inc de			;6764	13		.
l6765h:
	inc hl			;6765	23		#
	ld a,(hl)		;6766	7e		~
	cp 020h			;6767	fe 20		.  
	jr nz,l6794h		;6769	20 29		  )
	jr l6765h		;676b	18 f8		. .
l676dh:
	ld (M_67DF),hl		;676d	22 df 67	" . g
	dec hl			;6770	2b		+
	ld bc,00c2eh		;6771	01 2e 0c	. . .
l6774h:
	ld a,(de)		;6774	1a		.
	inc de			;6775	13		.
	cp 05ch			;6776	fe 5c		. \
	jr z,l67ebh		;6778	28 71		( q
	cp 00eh			;677a	fe 0e		. .
	jp c,l6813h		;677c	da 13 68	. . h
	cp 061h			;677f	fe 61		. a
	jr c,l6789h		;6781	38 06		8 .
	cp 07bh			;6783	fe 7b		. {
	jr nc,l6789h		;6785	30 02		0 .
	sub 020h		;6787	d6 20		.  
l6789h:
	cp (hl)			;6789	be		.
	jr nz,l6796h		;678a	20 0a		  .
	cp c			;678c	b9		.
	jr z,l6793h		;678d	28 04		( .
	ld a,(de)		;678f	1a		.
	cp c			;6790	b9		.
	jr z,l6764h		;6791	28 d1		( .
l6793h:
	inc hl			;6793	23		#
l6794h:
	djnz l6774h		;6794	10 de		. .
l6796h:
	ld hl,(M_67DF)		;6796	2a df 67	* . g
	ld bc,0001fh		;6799	01 1f 00	. . .
	ld de,(M_6876)		;679c	ed 5b 76 68	. [ v h
	jr l673fh		;67a0	18 9d		. .
l67a2h:
	ld (M_67DF),hl		;67a2	22 df 67	" . g
	ld c,018h		;67a5	0e 18		. .
l67a7h:
	ld b,005h		;67a7	06 05		. .
l67a9h:
	ld a,(de)		;67a9	1a		.
	inc de			;67aa	13		.
	cp 05ch			;67ab	fe 5c		. \
	jr z,l6807h		;67ad	28 58		( X
	cp 00eh			;67af	fe 0e		. .
	jr c,l6818h		;67b1	38 65		8 e
	cp (hl)			;67b3	be		.
	jr nz,l67deh		;67b4	20 28		  (
	inc hl			;67b6	23		#
	xor a			;67b7	af		.
	cp (hl)			;67b8	be		.
	jr nz,l67deh		;67b9	20 23		  #
	inc hl			;67bb	23		#
	djnz l67a9h		;67bc	10 eb		. .
	dec c			;67be	0d		.
	jr z,l67deh		;67bf	28 1d		( .
	ld a,c			;67c1	79		y
	and 003h		;67c2	e6 03		. .
	jr z,l67d3h		;67c4	28 0d		( .
	ld b,002h		;67c6	06 02		. .
	dec a			;67c8	3d		=
	jr z,l67cfh		;67c9	28 04		( .
	ld b,006h		;67cb	06 06		. .
D_67CD:
	dec c			;67cd	0d		.
	inc hl			;67ce	23		#
l67cfh:
	inc hl			;67cf	23		#
	inc hl			;67d0	23		#
	jr l67a9h		;67d1	18 d6		. .
l67d3h:
	push de			;67d3	d5		.
	ld de,0ffc0h		;67d4	11 c0 ff	. . .
	add hl,de		;67d7	19		.
	pop de			;67d8	d1		.
	xor a			;67d9	af		.
	cp (hl)			;67da	be		.
	inc hl			;67db	23		#
	jr nz,l67a7h		;67dc	20 c9		  .
l67deh:
	ld hl,00000h		;67de	21 00 00	! . .
	ld bc,0001fh		;67e1	01 1f 00	. . .
	ld de,(M_6876)		;67e4	ed 5b 76 68	. [ v h
	jp R_6743		;67e8	c3 43 67	. C g
l67ebh:
	ld hl,(M_67DF)		;67eb	2a df 67	* . g
	ld bc,0000ah		;67ee	01 0a 00	. . .
	add hl,bc		;67f1	09		.
	ld a,(hl)		;67f2	7e		~
	and 010h		;67f3	e6 10		. .
	jr z,l6796h		;67f5	28 9f		( .
	ld a,(hl)		;67f7	7e		~
	and 00fh		;67f8	e6 0f		. .
	cp 00fh			;67fa	fe 0f		. .
	jr z,l6796h		;67fc	28 98		( .
	ld (M_6876),de		;67fe	ed 53 76 68	. S v h
	call R_686E		;6802	cd 6e 68	. n h
	jr l6810h		;6805	18 09		. .
l6807h:
	ld (M_6876),de		;6807	ed 53 76 68	. S v h
	call R_6842		;680b	cd 42 68	. B h
	jr nz,l67deh		;680e	20 ce		  .
l6810h:
	jp R_66F8		;6810	c3 f8 66	. . f
l6813h:
	call R_686E		;6813	cd 6e 68	. n h
	jr l681dh		;6816	18 05		. .
l6818h:
	call R_6842		;6818	cd 42 68	. B h
	jr nz,l67deh		;681b	20 c1		  .
l681dh:
	ld (M_7B9A),hl		;681d	22 9a 7b	" . {
	ld (M_7B9C),de		;6820	ed 53 9c 7b	. S . {
	call R_9A9B		;6824	cd 9b 9a	. . .
	ld h,b			;6827	60		`
	ld l,c			;6828	69		i
	ld c,(hl)		;6829	4e		N
	inc hl			;682a	23		#
	ld b,(hl)		;682b	46		F
	inc hl			;682c	23		#
	ld e,(hl)		;682d	5e		^
	inc hl			;682e	23		#
	ld d,(hl)		;682f	56		V
	ld h,b			;6830	60		`
	ld l,c			;6831	69		i
	ld bc,00800h		;6832	01 00 08	. . .
	ld (M_803F),hl		;6835	22 3f 80	" ? .
	ld (08041h),de		;6838	ed 53 41 80	. S A .
	call R_7FF0		;683c	cd f0 7f	. . .
	jp R_698F		;683f	c3 8f 69	. . i
R_6842:
	ld hl,(M_67DF)		;6842	2a df 67	* . g
	ld de,0000ch		;6845	11 0c 00	. . .
	add hl,de		;6848	19		.
	ld c,(hl)		;6849	4e		N
	ld de,00013h		;684a	11 13 00	. . .
	add hl,de		;684d	19		.
	xor a			;684e	af		.
	ld b,00bh		;684f	06 0b		. .
l6851h:
	rrca			;6851	0f		.
	add a,(hl)		;6852	86		.
	inc hl			;6853	23		#
	djnz l6851h		;6854	10 fb		. .
	cp c			;6856	b9		.
	ret nz			;6857	c0		.
	ld de,00009h		;6858	11 09 00	. . .
l685bh:
	add hl,de		;685b	19		.
	ld c,(hl)		;685c	4e		N
	inc hl			;685d	23		#
	ld b,(hl)		;685e	46		F
	ld de,00005h		;685f	11 05 00	. . .
	add hl,de		;6862	19		.
	ld e,(hl)		;6863	5e		^
	inc hl			;6864	23		#
	ld d,(hl)		;6865	56		V
	inc hl			;6866	23		#
	push hl			;6867	e5		.
	ld h,b			;6868	60		`
	ld l,c			;6869	69		i
	ex de,hl		;686a	eb		.
	pop bc			;686b	c1		.
	xor a			;686c	af		.
	ret			;686d	c9		.
R_686E:
	ld hl,(M_67DF)		;686e	2a df 67	* . g
	ld de,00013h		;6871	11 13 00	. . .
	jr l685bh		;6874	18 e5		. .
M_6876:
	nop			;6876	00		.
	nop			;6877	00		.
M_6878:
	nop			;6878	00		.
	nop			;6879	00		.
l687ah:
	nop			;687a	00		.
	nop			;687b	00		.
M_687C:
	nop			;687c	00		.
R_687D:
	ld a,(hl)		;687d	7e		~
	or a			;687e	b7		.
	jr z,l6882h		;687f	28 01		( .
	inc hl			;6881	23		#
l6882h:
	ld (de),a		;6882	12		.
	inc de			;6883	13		.
	djnz R_687D		;6884	10 f7		. .
	inc hl			;6886	23		#
	ret			;6887	c9		.
	ld a,(TS_PRESENT_FLAG)	;6888	3a bf 8e	: . .
	or a			;688b	b7		.
	jp z,R_6B5E		;688c	ca 5e 6b	. ^ k
	call R_72A0		;688f	cd a0 72	. . r
	ld hl,0c016h		;6892	21 16 c0	! . .
	ld bc,00200h		;6895	01 00 02	. . .
	call R_71C1		;6898	cd c1 71	. . q
	ld hl,0c200h		;689b	21 00 c2	! . .
	ld b,004h		;689e	06 04		. .
	call R_769E		;68a0	cd 9e 76	. . v
	ld hl,0c204h		;68a3	21 04 c2	! . .
	ld de,05b4fh		;68a6	11 4f 5b	. O [
	ld b,020h		;68a9	06 20		.  
	call R_687D		;68ab	cd 7d 68	. } h
	ld b,020h		;68ae	06 20		.  
	call R_687D		;68b0	cd 7d 68	. } h
	ld hl,05b4fh		;68b3	21 4f 5b	! O [
	ld (M_8EC3),hl		;68b6	22 c3 8e	" . .
	ld hl,05b6fh		;68b9	21 6f 5b	! o [
	ld (M_8EC5),hl		;68bc	22 c5 8e	" . .
	call R_6B2C		;68bf	cd 2c 6b	. , k
	call R_8921		;68c2	cd 21 89	. ! .
	call 0c000h		;68c5	cd 00 c0	. . .
	ld (l6974h),bc		;68c8	ed 43 74 69	. C t i
	ld (l6976h),de		;68cc	ed 53 76 69	. S v i
	di			;68d0	f3		.
	ld hl,D_6941		;68d1	21 41 69	! A i
	ld (05bffh),hl		;68d4	22 ff 5b	" . [
	ei			;68d7	fb		.
	ld hl,D_8E9E		;68d8	21 9e 8e	! . .
	ld de,040ebh		;68db	11 eb 40	. . @
	ld b,00ah		;68de	06 0a		. .
	ld a,001h		;68e0	3e 01		> .
	call R_85B6		;68e2	cd b6 85	. . .
	ld hl,D_8EAE		;68e5	21 ae 8e	! . .
	ld de,0480bh		;68e8	11 0b 48	. . H
	ld b,00ah		;68eb	06 0a		. .
	ld a,001h		;68ed	3e 01		> .
	call R_85B6		;68ef	cd b6 85	. . .
l68f2h:
	ei			;68f2	fb		.
	halt			;68f3	76		v
	ld a,(0c00dh)		;68f4	3a 0d c0	: . .
	or a			;68f7	b7		.
	jr z,l692fh		;68f8	28 35		( 5
	ld hl,(0c009h)		;68fa	2a 09 c0	* . .
	inc hl			;68fd	23		#
	inc hl			;68fe	23		#
	ld a,(hl)		;68ff	7e		~
	cp 0d2h			;6900	fe d2		. .
	jr c,l691ah		;6902	38 16		8 .
	sub 010h		;6904	d6 10		. .
	ld (hl),a		;6906	77		w
	ld hl,0d200h		;6907	21 00 d2	! . .
	ld de,0c200h		;690a	11 00 c2	. . .
	ld bc,01000h		;690d	01 00 10	. . .
	ldir			;6910	ed b0		. .
	ld hl,0d200h		;6912	21 00 d2	! . .
	ld b,002h		;6915	06 02		. .
	call R_769E		;6917	cd 9e 76	. . v
l691ah:
	call R_89C9		;691a	cd c9 89	. . .
	jr nc,l692fh		;691d	30 10		0 .
	ld a,0fdh		;691f	3e fd		> .
R_6921:
	in a,(0feh)		;6921	db fe		. .
	rra			;6923	1f		.
	jr c,l68f2h		;6924	38 cc		8 .
	call R_6935		;6926	cd 35 69	. 5 i
	call R_72A0		;6929	cd a0 72	. . r
	jp R_6B5E		;692c	c3 5e 6b	. ^ k
l692fh:
	call R_6935		;692f	cd 35 69	. 5 i
	jp R_6654		;6932	c3 54 66	. T f
R_6935:
	di			;6935	f3		.
	ld hl,D_900E		;6936	21 0e 90	! . .
	ld (05bffh),hl		;6939	22 ff 5b	" . [
	ei			;693c	fb		.
	call 0c006h		;693d	cd 06 c0	. . .
	ret			;6940	c9		.
D_6941:
	push hl			;6941	e5		.
	push bc			;6942	c5		.
	push de			;6943	d5		.
	push af			;6944	f5		.
	ex af,af'		;6945	08		.
	push af			;6946	f5		.
	ld a,(0c00dh)		;6947	3a 0d c0	: . .
	or a			;694a	b7		.
	jr z,l6968h		;694b	28 1b		( .
	ld bc,(l6974h)		;694d	ed 4b 74 69	. K t i
	ld de,(l6976h)		;6951	ed 5b 76 69	. [ v i
	ld a,(M_6973)		;6955	3a 73 69	: s i
	ex af,af'		;6958	08		.
	call 0c003h		;6959	cd 03 c0	. . .
	ex af,af'		;695c	08		.
	ld (M_6973),a		;695d	32 73 69	2 s i
	ld (l6976h),de		;6960	ed 53 76 69	. S v i
	ld (l6974h),bc		;6964	ed 43 74 69	. C t i
l6968h:
	call R_86CC		;6968	cd cc 86	. . .
	pop af			;696b	f1		.
	ex af,af'		;696c	08		.
	pop af			;696d	f1		.
	pop de			;696e	d1		.
	pop bc			;696f	c1		.
	pop hl			;6970	e1		.
	ei			;6971	fb		.
	ret			;6972	c9		.
M_6973:
	nop			;6973	00		.
l6974h:
	nop			;6974	00		.
	nop			;6975	00		.
l6976h:
	nop			;6976	00		.
	nop			;6977	00		.
M_6978:
	nop			;6978	00		.
D_6979:
	ld c,l			;6979	4d		M
	ld c,a			;697a	4f		O
	ld b,h			;697b	44		D
	ld d,d			;697c	52		R
	ld b,c			;697d	41		A
	ld d,a			;697e	57		W
	ld d,a			;697f	57		W
	ld b,c			;6980	41		A
	ld d,(hl)		;6981	56		V
	ld d,h			;6982	54		T
	ld b,(hl)		;6983	46		F
	ld b,h			;6984	44		D
	ld c,l			;6985	4d		M
	inc sp			;6986	33		3
	ld d,l			;6987	55		U
	ld b,c			;6988	41		A
	ld e,c			;6989	59		Y
	ld c,h			;698a	4c		L
D_698B:
	ld l,b			;698b	68		h
	ld (hl),d		;698c	72		r
	ld (03a00h),a		;698d	32 00 3a	2 . :
	ld b,b			;6990	40		@
	add a,b			;6991	80		.
	or a			;6992	b7		.
	jp z,M_6B24		;6993	ca 24 6b	. $ k
	cp 060h			;6996	fe 60		. `
	jp nc,M_6B24		;6998	d2 24 6b	. $ k
	ld de,(M_803D)		;699b	ed 5b 3d 80	. [ = .
	ld a,d			;699f	7a		z
	or a			;69a0	b7		.
	jp nz,M_6B24		;69a1	c2 24 6b	. $ k
	ld a,e			;69a4	7b		{
	cp 00dh			;69a5	fe 0d		. .
	jp nc,M_6B24		;69a7	d2 24 6b	. $ k
	call R_84B2		;69aa	cd b2 84	. . .
	ld hl,VTII_INSTANCE	;69ad	21 00 a0	! . .
	ld b,00ch		;69b0	06 0c		. .
	call R_769E		;69b2	cd 9e 76	. . v
	ld bc,(M_803F)		;69b5	ed 4b 3f 80	. K ? .
R_69B9:
	ld (M_8F13),bc		;69b9	ed 43 13 8f	. C . .
	ld a,001h		;69bd	3e 01		> .
	ld (M_8EC2),a		;69bf	32 c2 8e	2 . .
	call MUTE_BOTH		;69c2	cd 61 8a	. a .
	ld hl,05b4fh		;69c5	21 4f 5b	! O [
	ld b,040h		;69c8	06 40		. @
	ld a,020h		;69ca	3e 20		>  
	call R_74B6		;69cc	cd b6 74	. . t
	xor a			;69cf	af		.
	ld (M_6B01),a		;69d0	32 01 6b	2 . k
	ld hl,VTII_INSTANCE	;69d3	21 00 a0	! . .
	ld de,D_698B		;69d6	11 8b 69	. . i
	call R_71B1		;69d9	cd b1 71	. . q
	jr nz,l6a13h		;69dc	20 35		  5
	ld a,(hl)		;69de	7e		~
	and 07fh		;69df	e6 7f		. .
	cp 031h			;69e1	fe 31		. 1
	jr nz,l6a13h		;69e3	20 2e		  .
	ld hl,(M_A004)		;69e5	2a 04 a0	* . .
	ld a,h			;69e8	7c		|
	cp 050h			;69e9	fe 50		. P
	jp nc,R_6B5E		;69eb	d2 5e 6b	. ^ k
	ld (M_8F13),hl		;69ee	22 13 8f	" . .
	call R_8482		;69f1	cd 82 84	. . .
	ld hl,0c800h		;69f4	21 00 c8	! . .
	ld de,D_9E00		;69f7	11 00 9e	. . .
	ld bc,00100h		;69fa	01 00 01	. . .
	push de			;69fd	d5		.
	push bc			;69fe	c5		.
	ldir			;69ff	ed b0		. .
	call R_84B2		;6a01	cd b2 84	. . .
	pop bc			;6a04	c1		.
	pop hl			;6a05	e1		.
	ld de,0ff00h		;6a06	11 00 ff	. . .
	ldir			;6a09	ed b0		. .
	ld hl,VTII_INSTANCE	;6a0b	21 00 a0	! . .
	ld d,h			;6a0e	54		T
	ld e,l			;6a0f	5d		]
	call 0ff00h		;6a10	cd 00 ff	. . .
l6a13h:
	ld hl,VTII_INSTANCE	;6a13	21 00 a0	! . .
	ld de,D_6BFD		;6a16	11 fd 6b	. . k
	ld a,001h		;6a19	3e 01		> .
	ld (M_8F20),a		;6a1b	32 20 8f	2   .
	call R_6BCE		;6a1e	cd ce 6b	. . k
	jp nz,06c29h		;6a21	c2 29 6c	. ) l
	xor a			;6a24	af		.
	ld (TS_STATE2),a	;6a25	32 05 6b	2 . k
	ld de,D_6BFD		;6a28	11 fd 6b	. . k
	jp R_6C3D		;6a2b	c3 3d 6c	. = l
R_6A2E:
	ld hl,D_A001		;6a2e	21 01 a0	! . .
	dec hl			;6a31	2b		+
	ld de,VTII_INSTANCE	;6a32	11 00 a0	. . .
	ld bc,0341fh		;6a35	01 1f 34	. . 4
	ldir			;6a38	ed b0		. .
;LD A,(6B05); OR A; JR Z,TS_MARKER_CHECK - first gate of the TS setup:
;only runs for player types that support TurboSound at all.
TS_GATE_A:
	ld a,(TS_STATE2)	;6a3a	3a 05 6b	: . k
	or a			;6a3d	b7		.
	jr z,TS_MARKER_CHECK	;6a3e	28 1d		( .
	ld de,0c86eh		;6a40	11 6e c8	. n .
	call R_71DF		;6a43	cd df 71	. . q
	ld hl,0c018h		;6a46	21 18 c0	! . .
	ld bc,00651h		;6a49	01 51 06	. Q .
	call R_71C1		;6a4c	cd c1 71	. . q
	ld hl,0c88ch		;6a4f	21 8c c8	! . .
	ld (M_8EC3),hl		;6a52	22 c3 8e	" . .
	ld hl,0c8b0h		;6a55	21 b0 c8	! . .
	ld (M_8EC5),hl		;6a58	22 c5 8e	" . .
	jr l6a87h		;6a5b	18 2a		. *
;LD A,(0xA062); CP #20; JR Z,skip; LD A,2; LD (6B04),A; LD (6B02),A
;Converts the probed marker state into TS_MODE_FLAG=2 (dual engine).
TS_MARKER_CHECK:
	ld a,(M_A062)		;6a5d	3a 62 a0	: b .
	cp 020h			;6a60	fe 20		.  
	jr z,l6a6ch		;6a62	28 08		( .
	ld a,002h		;6a64	3e 02		> .
	ld (M_6B04),a		;6a66	32 04 6b	2 . k
	ld (TS_MODE_FLAG),a	;6a69	32 02 6b	2 . k
l6a6ch:
	ld de,0cbe1h		;6a6c	11 e1 cb	. . .
	call R_71DF		;6a6f	cd df 71	. . q
	ld hl,0c006h		;6a72	21 06 c0	! . .
	ld bc,00be1h		;6a75	01 e1 0b	. . .
	call R_71C1		;6a78	cd c1 71	. . q
	ld hl,0cbffh		;6a7b	21 ff cb	! . .
	ld (M_8EC3),hl		;6a7e	22 c3 8e	" . .
	ld hl,0cc23h		;6a81	21 23 cc	! # .
	ld (M_8EC5),hl		;6a84	22 c5 8e	" . .
l6a87h:
	ld a,020h		;6a87	3e 20		>  
	ld (0c00ah),a		;6a89	32 0a c0	2 . .
	ld a,005h		;6a8c	3e 05		> .
	ld (M_907B),a		;6a8e	32 7b 90	2 { .
R_6A91:
	call 0c000h		;6a91	cd 00 c0	. . .
	ld a,001h		;6a94	3e 01		> .
	ld (M_8EBE),a		;6a96	32 be 8e	2 . .
	ld hl,00000h		;6a99	21 00 00	! . .
	ld (M_90DC),hl		;6a9c	22 dc 90	" . .
	ld hl,TS_SETUP_DATA	;6a9f	21 c0 8e	! . .
	ld a,(hl)		;6aa2	7e		~
	ld (hl),000h		;6aa3	36 00		6 .
	or a			;6aa5	b7		.
	jr nz,$+96		;6aa6	20 5e		  ^
	call R_8A5D		;6aa8	cd 5d 8a	. ] .
	jr z,$+91		;6aab	28 59		( Y
	ld hl,0c000h		;6aad	21 00 c0	! . .
	ld de,VTII_INSTANCE	;6ab0	11 00 a0	. . .
	ld bc,02000h		;6ab3	01 00 20	. .  
	ldir			;6ab6	ed b0		. .
	ld a,008h		;6ab8	3e 08		> .
	call R_847C		;6aba	cd 7c 84	. | .
	ld hl,VTII_INSTANCE	;6abd	21 00 a0	! . .
	ld de,0c000h		;6ac0	11 00 c0	. . .
	ld bc,02000h		;6ac3	01 00 20	. .  
	ldir			;6ac6	ed b0		. .
	ld a,001h		;6ac8	3e 01		> .
	call R_847C		;6aca	cd 7c 84	. | .
	ld hl,0e000h		;6acd	21 00 e0	! . .
	ld de,VTII_INSTANCE	;6ad0	11 00 a0	. . .
	ld bc,02000h		;6ad3	01 00 20	. .  
	ldir			;6ad6	ed b0		. .
	ld a,008h		;6ad8	3e 08		> .
	call R_847C		;6ada	cd 7c 84	. | .
	ld hl,VTII_INSTANCE	;6add	21 00 a0	! . .
	ld de,0e000h		;6ae0	11 00 e0	. . .
	ld bc,02000h		;6ae3	01 00 20	. .  
	ldir			;6ae6	ed b0		. .
	ld a,001h		;6ae8	3e 01		> .
	ld (TS_MODE_FLAG),a	;6aea	32 02 6b	2 . k
	ld (TS_STATE),a		;6aed	32 03 6b	2 . k
	ld hl,00000h		;6af0	21 00 00	! . .
	ld (M_8EC1),hl		;6af3	22 c1 8e	" . .
	ld hl,(M_907B)		;6af6	2a 7b 90	* { .
	ld (M_9094),hl		;6af9	22 94 90	" . .
	call R_6B64		;6afc	cd 64 6b	. d k
	jr M_6B24		;6aff	18 23		. #
M_6B01:
	nop			;6b01	00		.
;TurboSound mode: 0 = single chip, 1/2 = dual chip.
;Gates the dual-chip init (86EF) and the per-frame switch (907D).
;Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above.
TS_MODE_FLAG:
	nop			;6b02	00		.
;player-type / TS state bytes 6B03..6B05 (6B05=1 here)
TS_STATE:
	nop			;6b03	00		.
M_6B04:
	nop			;6b04	00		.
;TS-capable player-type flag checked at TS_GATE_A
TS_STATE2:
	ld bc,0043ah		;6b05	01 3a 04	. : .
	ld l,e			;6b08	6b		k
	or a			;6b09	b7		.
	jr nz,l6b14h		;6b0a	20 08		  .
	ld hl,(TS_STATE)	;6b0c	2a 03 6b	* . k
	ld h,000h		;6b0f	26 00		& .
	ld (TS_MODE_FLAG),hl	;6b11	22 02 6b	" . k
l6b14h:
	xor a			;6b14	af		.
	ld (M_6B04),a		;6b15	32 04 6b	2 . k
	ld hl,00001h		;6b18	21 01 00	! . .
	ld (M_8EC1),hl		;6b1b	22 c1 8e	" . .
	call R_6B64		;6b1e	cd 64 6b	. d k
	call R_6B2C		;6b21	cd 2c 6b	. , k
M_6B24:
	nop			;6b24	00		.
	call R_73E7		;6b25	cd e7 73	. . s
	ei			;6b28	fb		.
	jp R_6271		;6b29	c3 71 62	. q b
R_6B2C:
	ld hl,(M_8EC3)		;6b2c	2a c3 8e	* . .
	ld de,05bbfh		;6b2f	11 bf 5b	. . [
	ld bc,00020h		;6b32	01 20 00	.   .
	ldir			;6b35	ed b0		. .
	ld hl,(M_8EC5)		;6b37	2a c5 8e	* . .
	ld de,05bdfh		;6b3a	11 df 5b	. . [
	ld c,020h		;6b3d	0e 20		.  
	ldir			;6b3f	ed b0		. .
	ld de,0400ch		;6b41	11 0c 40	. . @
	ld hl,05bbfh		;6b44	21 bf 5b	! . [
	ld b,014h		;6b47	06 14		. .
	ld a,007h		;6b49	3e 07		> .
	call R_85B6		;6b4b	cd b6 85	. . .
	ld de,0402ch		;6b4e	11 2c 40	. , @
	ld hl,05bdfh		;6b51	21 df 5b	! . [
	ld b,014h		;6b54	06 14		. .
	ld a,007h		;6b56	3e 07		> .
	call R_85B6		;6b58	cd b6 85	. . .
	jp R_8486		;6b5b	c3 86 84	. . .
R_6B5E:
	xor a			;6b5e	af		.
	ld (M_8EC2),a		;6b5f	32 c2 8e	2 . .
	jr M_6B24		;6b62	18 c0		. .
R_6B64:
	ld a,(M_8ECD)		;6b64	3a cd 8e	: . .
	cp 018h			;6b67	fe 18		. .
	jr nz,l6b88h		;6b69	20 1d		  .
	ld hl,D_8E49		;6b6b	21 49 8e	! I .
	ld de,D_8E4A		;6b6e	11 4a 8e	. J .
	ld bc,00007h		;6b71	01 07 00	. . .
	ldir			;6b74	ed b0		. .
	ld a,(M_8E75)		;6b76	3a 75 8e	: u .
	ld (de),a		;6b79	12		.
	inc de			;6b7a	13		.
	ld a,03ah		;6b7b	3e 3a		> :
	ld (de),a		;6b7d	12		.
	inc de			;6b7e	13		.
	ld hl,05b02h		;6b7f	21 02 5b	! . [
	ld c,00ch		;6b82	0e 0c		. .
	ldir			;6b84	ed b0		. .
	jr l6bb2h		;6b86	18 2a		. *
l6b88h:
	ld hl,D_8EEA		;6b88	21 ea 8e	! . .
	ld de,D_8E4A		;6b8b	11 4a 8e	. J .
	ld bc,00009h		;6b8e	01 09 00	. . .
	ldir			;6b91	ed b0		. .
	ld hl,D_8E32		;6b93	21 32 8e	! 2 .
	ld c,00ch		;6b96	0e 0c		. .
	ldir			;6b98	ed b0		. .
	ld a,0ffh		;6b9a	3e ff		> .
	ld (D_8E49),a		;6b9c	32 49 8e	2 I .
l6b9fh:
	ld hl,D_8E50		;6b9f	21 50 8e	! P .
	ld de,D_8E51		;6ba2	11 51 8e	. Q .
	ld c,008h		;6ba5	0e 08		. .
	ld a,(D_8E51)		;6ba7	3a 51 8e	: Q .
	cp 020h			;6baa	fe 20		.  
	jr nz,l6bb2h		;6bac	20 04		  .
	lddr			;6bae	ed b8		. .
	jr l6b9fh		;6bb0	18 ed		. .
l6bb2h:
	ld a,001h		;6bb2	3e 01		> .
	ld (M_8C28),a		;6bb4	32 28 8c	2 ( .
	xor a			;6bb7	af		.
	ld (M_8C2A),a		;6bb8	32 2a 8c	2 * .
	ld a,(M_6B24)		;6bbb	3a 24 6b	: $ k
	or a			;6bbe	b7		.
	ret nz			;6bbf	c0		.
	ld hl,D_8E49		;6bc0	21 49 8e	! I .
	ld b,00eh		;6bc3	06 0e		. .
	ld de,04869h		;6bc5	11 69 48	. i H
	ld a,(M_933C)		;6bc8	3a 3c 93	: < .
	jp R_85B6		;6bcb	c3 b6 85	. . .
R_6BCE:
	ld (l6bdeh+1),de	;6bce	ed 53 df 6b	. S . k
	push hl			;6bd2	e5		.
	ld bc,(M_8F13)		;6bd3	ed 4b 13 8f	. K . .
	ld hl,VTII_INSTANCE	;6bd7	21 00 a0	! . .
	add hl,bc		;6bda	09		.
	ld b,h			;6bdb	44		D
	ld c,l			;6bdc	4d		M
	pop hl			;6bdd	e1		.
l6bdeh:
	ld de,D_6C0B		;6bde	11 0b 6c	. . l
l6be1h:
	ld a,h			;6be1	7c		|
	cp b			;6be2	b8		.
	jr nc,$+22		;6be3	30 14		0 .
	ld a,(de)		;6be5	1a		.
	cp (hl)			;6be6	be		.
	inc hl			;6be7	23		#
	jp nz,l6be1h		;6be8	c2 e1 6b	. . k
	ld (M_6A2F),hl		;6beb	22 2f 6a	" / j
R_6BEE:
	inc de			;6bee	13		.
	ld a,(de)		;6bef	1a		.
	or a			;6bf0	b7		.
	ret z			;6bf1	c8		.
	cp (hl)			;6bf2	be		.
	jr nz,l6bdeh		;6bf3	20 e9		  .
	inc hl			;6bf5	23		#
	defb 0c3h,0eeh		;6bf6	c3 ee		. .
; --------------------------------------------------------------------------
; DATA 0x6BF8-0x6C3F  module-format detector strings (ProTracker 3. / Vortex Tracker II 1. / TFMcom1.)
; --------------------------------------------------------------------------
DATA_6BF8:
	defb 6B 3E 01 B7 C9 50 72 6F  ;6BF8  k>...Pro
	defb 54 72 61 63 6B 65 72 20  ;6C00  Tracker 
	defb 33 2E 00 56 6F 72 74 65  ;6C08  3..Vorte
	defb 78 20 54 72 61 63 6B 65  ;6C10  x Tracke
	defb 72 20 49 49 20 31 2E 00  ;6C18  r II 1..
	defb 54 46 4D 63 6F 6D 31 2E  ;6C20  TFMcom1.
	defb 00 21 00 A0 11 0B 6C CD  ;6C28  .!....l.
	defb CE 6B C2 C3 6C 3E 01 32  ;6C30  .k..l>.2
	defb 05 6B 11 0B 6C 2A 2F 6A  ;6C38  .k..l*/j


; ..........................................................................
; code segment restart 0x6C40 - clean decode boundary after data above
; ..........................................................................
;TurboSound mode: 0 = single chip, 1/2 = dual chip.
;Gates the dual-chip init (86EF) and the per-frame switch (907D).
;Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above.
;player-type / TS state bytes 6B03..6B05 (6B05=1 here)
;TS-present flag written by the intro probe (1=present).
;0 => every TS setup path (6888 / 6D09 pattern: OR A / JP Z) is
;skipped and TS_MODE_FLAG stays 0 forever.
;TS engine install parameters filled in when 8EBF=1
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).

	push hl			;6c40	e5		.
	ld bc,00034h		;6c41	01 34 00	. 4 .
	add hl,bc		;6c44	09		.
	call R_6BCE		;6c45	cd ce 6b	. . k
	ld de,(M_6A2F)		;6c48	ed 5b 2f 6a	. [ / j
	pop hl			;6c4c	e1		.
	ld (M_6A2F),hl		;6c4d	22 2f 6a	" / j
	jp R_6A2E		;6c50	c3 2e 6a	. . j
	ex de,hl		;6c53	eb		.
	dec hl			;6c54	2b		+
	push hl			;6c55	e5		.
	call R_84B6		;6c56	cd b6 84	. . .
	ld hl,VTII_INSTANCE	;6c59	21 00 a0	! . .
	ld de,0f000h		;6c5c	11 00 f0	. . .
	ld bc,01000h		;6c5f	01 00 10	. . .
	ldir			;6c62	ed b0		. .
	ld hl,R_84B6		;6c64	21 b6 84	! . .
	ld (M_71D5),hl		;6c67	22 d5 71	" . q
	ld hl,0c018h		;6c6a	21 18 c0	! . .
	ld bc,00651h		;6c6d	01 51 06	. Q .
	call R_71C1		;6c70	cd c1 71	. . q
	ld hl,R_8486		;6c73	21 86 84	! . .
	ld (M_71D5),hl		;6c76	22 d5 71	" . q
	ld hl,0f000h		;6c79	21 00 f0	! . .
	ld de,VTII_INSTANCE	;6c7c	11 00 a0	. . .
	ld bc,01000h		;6c7f	01 00 10	. . .
	ldir			;6c82	ed b0		. .
	pop hl			;6c84	e1		.
	exx			;6c85	d9		.
	ld de,0c86eh		;6c86	11 6e c8	. n .
	exx			;6c89	d9		.
	call R_6CA4		;6c8a	cd a4 6c	. . l
	call 0c000h		;6c8d	cd 00 c0	. . .
	ld a,001h		;6c90	3e 01		> .
	ld (TS_MODE_FLAG),a	;6c92	32 02 6b	2 . k
	ld (TS_STATE),a		;6c95	32 03 6b	2 . k
	ld hl,0c005h		;6c98	21 05 c0	! . .
	ld (M_9094),hl		;6c9b	22 94 90	" . .
	call R_84B2		;6c9e	cd b2 84	. . .
	jp R_6A2E		;6ca1	c3 2e 6a	. . j
R_6CA4:
	ld b,018h		;6ca4	06 18		. .
l6ca6h:
	push bc			;6ca6	c5		.
	call R_84B2		;6ca7	cd b2 84	. . .
	ld de,D_9E00		;6caa	11 00 9e	. . .
	ld bc,00200h		;6cad	01 00 02	. . .
	ldir			;6cb0	ed b0		. .
	call R_84B6		;6cb2	cd b6 84	. . .
	exx			;6cb5	d9		.
	ld hl,D_9E00		;6cb6	21 00 9e	! . .
	ld bc,00200h		;6cb9	01 00 02	. . .
	ldir			;6cbc	ed b0		. .
	exx			;6cbe	d9		.
	pop bc			;6cbf	c1		.
	djnz l6ca6h		;6cc0	10 e4		. .
	ret			;6cc2	c9		.
	ld hl,VTII_INSTANCE	;6cc3	21 00 a0	! . .
	ld de,D_6C20		;6cc6	11 20 6c	.   l
	call R_6BCE		;6cc9	cd ce 6b	. . k
	jp nz,l6d50h		;6ccc	c2 50 6d	. P m
	ld a,002h		;6ccf	3e 02		> .
	ld (M_8F20),a		;6cd1	32 20 8f	2   .
	push hl			;6cd4	e5		.
	ld hl,(M_8F13)		;6cd5	2a 13 8f	* . .
	ld de,VTII_INSTANCE	;6cd8	11 00 a0	. . .
	add hl,de		;6cdb	19		.
	pop de			;6cdc	d1		.
	or a			;6cdd	b7		.
	sbc hl,de		;6cde	ed 52		. R
	ex de,hl		;6ce0	eb		.
	ld a,d			;6ce1	7a		z
	cp 038h			;6ce2	fe 38		. 8
	jp nc,R_6B5E		;6ce4	d2 5e 6b	. ^ k
	ld b,006h		;6ce7	06 06		. .
	ld a,(hl)		;6ce9	7e		~
	inc hl			;6cea	23		#
	cp 030h			;6ceb	fe 30		. 0
	jp c,R_6B5E		;6ced	da 5e 6b	. ^ k
	ld a,(hl)		;6cf0	7e		~
	cp 032h			;6cf1	fe 32		. 2
	jr z,l6cfah		;6cf3	28 05		( .
	cp 03ch			;6cf5	fe 3c		. <
	jp nz,R_6B5E		;6cf7	c2 5e 6b	. ^ k
l6cfah:
	inc hl			;6cfa	23		#
	inc hl			;6cfb	23		#
	ld a,(hl)		;6cfc	7e		~
	cp 038h			;6cfd	fe 38		. 8
	jp nc,R_6B5E		;6cff	d2 5e 6b	. ^ k
	djnz l6cfah		;6d02	10 f6		. .
	ld a,003h		;6d04	3e 03		> .
	ld (M_8F20),a		;6d06	32 20 8f	2   .
	ld a,(TS_PRESENT_FLAG)	;6d09	3a bf 8e	: . .
	or a			;6d0c	b7		.
	jp z,R_6B5E		;6d0d	ca 5e 6b	. ^ k
	ld (TS_SETUP_DATA),a	;6d10	32 c0 8e	2 . .
	ld hl,(M_6A2F)		;6d13	2a 2f 6a	* / j
	dec hl			;6d16	2b		+
	ld de,VTII_INSTANCE	;6d17	11 00 a0	. . .
	ld bc,038bdh		;6d1a	01 bd 38	. . 8
	ldir			;6d1d	ed b0		. .
	ld de,0c73dh		;6d1f	11 3d c7	. = .
	call R_71DF		;6d22	cd df 71	. . q
	ld hl,0c014h		;6d25	21 14 c0	! . .
	ld bc,0073dh		;6d28	01 3d 07	. = .
	call R_71C1		;6d2b	cd c1 71	. . q
	ld hl,0c75fh		;6d2e	21 5f c7	! _ .
	ld a,(0c745h)		;6d31	3a 45 c7	: E .
	cp 031h			;6d34	fe 31		. 1
	jr z,l6d3bh		;6d36	28 03		( .
	ld hl,0c76fh		;6d38	21 6f c7	! o .
l6d3bh:
	ld de,05b4fh		;6d3b	11 4f 5b	. O [
	ld b,020h		;6d3e	06 20		.  
	call R_687D		;6d40	cd 7d 68	. } h
	ld b,020h		;6d43	06 20		.  
	call R_687D		;6d45	cd 7d 68	. } h
	ld a,005h		;6d48	3e 05		> .
	ld (M_6B01),a		;6d4a	32 01 6b	2 . k
	jp R_6E7D		;6d4d	c3 7d 6e	. } n
l6d50h:
	ld ix,VTII_INSTANCE	;6d50	dd 21 00 a0	. ! . .
	ld a,(ix+000h)		;6d54	dd 7e 00	. ~ .
	cp 0cdh			;6d57	fe cd		. .
	jr nz,l6d6eh		;6d59	20 13		  .
	ld a,(ix+001h)		;6d5b	dd 7e 01	. ~ .
	cp 052h			;6d5e	fe 52		. R
	jr nz,l6d6eh		;6d60	20 0c		  .
	ld a,(ix+002h)		;6d62	dd 7e 02	. ~ .
	cp 000h			;6d65	fe 00		. .
	jr nz,l6d6eh		;6d67	20 05		  .
	ld de,006e9h		;6d69	11 e9 06	. . .
	jr l6d8dh		;6d6c	18 1f		. .
l6d6eh:
	ld a,(ix+000h)		;6d6e	dd 7e 00	. ~ .
	cp 021h			;6d71	fe 21		. !
	jr nz,l6d91h		;6d73	20 1c		  .
	ld a,(ix+003h)		;6d75	dd 7e 03	. ~ .
	cp 0c3h			;6d78	fe c3		. .
	jr nz,l6d91h		;6d7a	20 15		  .
	ld a,(ix+006h)		;6d7c	dd 7e 06	. ~ .
	cp 0c3h			;6d7f	fe c3		. .
	jr nz,l6d91h		;6d81	20 0e		  .
	ld a,(ix+00ah)		;6d83	dd 7e 0a	. ~ .
	cp 07eh			;6d86	fe 7e		. ~
	jr nz,l6d91h		;6d88	20 07		  .
	ld de,0043ch		;6d8a	11 3c 04	. < .
l6d8dh:
	push ix			;6d8d	dd e5		. .
	add ix,de		;6d8f	dd 19		. .
l6d91h:
	ld a,(ix+000h)		;6d91	dd 7e 00	. ~ .
	or a			;6d94	b7		.
	jp z,l6e5ch		;6d95	ca 5c 6e	. \ n
	cp 018h			;6d98	fe 18		. .
	jp nc,l6e5ch		;6d9a	d2 5c 6e	. \ n
	ld a,(ix+006h)		;6d9d	dd 7e 06	. ~ .
	cp 028h			;6da0	fe 28		. (
	jp nc,l6e5ch		;6da2	d2 5c 6e	. \ n
	ld a,(ix+004h)		;6da5	dd 7e 04	. ~ .
	cp 020h			;6da8	fe 20		.  
	jp nc,l6e5ch		;6daa	d2 5c 6e	. \ n
	ld a,(ix+002h)		;6dad	dd 7e 02	. ~ .
	cp 010h			;6db0	fe 10		. .
	jp nc,l6e5ch		;6db2	d2 5c 6e	. \ n
	push ix			;6db5	dd e5		. .
	pop hl			;6db7	e1		.
	ld de,00007h		;6db8	11 07 00	. . .
	add hl,de		;6dbb	19		.
	ld b,012h		;6dbc	06 12		. .
l6dbeh:
	ld a,(hl)		;6dbe	7e		~
	cp 018h			;6dbf	fe 18		. .
	inc hl			;6dc1	23		#
	jr c,l6dceh		;6dc2	38 0a		8 .
	cp 020h			;6dc4	fe 20		.  
	jp c,l6e5ch		;6dc6	da 5c 6e	. \ n
	cp 07bh			;6dc9	fe 7b		. {
	jp nc,l6e5ch		;6dcb	d2 5c 6e	. \ n
l6dceh:
	djnz l6dbeh		;6dce	10 ee		. .
	push ix			;6dd0	dd e5		. .
	pop hl			;6dd2	e1		.
	ld e,01bh		;6dd3	1e 1b		. .
	add hl,de		;6dd5	19		.
	ld a,(hl)		;6dd6	7e		~
	or a			;6dd7	b7		.
	jp z,l6e5ch		;6dd8	ca 5c 6e	. \ n
	cp 012h			;6ddb	fe 12		. .
	jp nc,l6e5ch		;6ddd	d2 5c 6e	. \ n
	call R_6E0C		;6de0	cd 0c 6e	. . n
	jr z,l6e5ch		;6de3	28 77		( w
	push ix			;6de5	dd e5		. .
	pop hl			;6de7	e1		.
	ld de,VTII_INSTANCE	;6de8	11 00 a0	. . .
	ld bc,03bc4h		;6deb	01 c4 3b	. . ;
	ldir			;6dee	ed b0		. .
	ld de,0c43ch		;6df0	11 3c c4	. < .
	call R_71DF		;6df3	cd df 71	. . q
	ld hl,0c002h		;6df6	21 02 c0	! . .
	ld bc,0043ch		;6df9	01 3c 04	. < .
	call R_71C1		;6dfc	cd c1 71	. . q
	ld hl,0c443h		;6dff	21 43 c4	! C .
	ld de,05b4fh		;6e02	11 4f 5b	. O [
	ld c,012h		;6e05	0e 12		. .
	ldir			;6e07	ed b0		. .
	jp R_6E7B		;6e09	c3 7b 6e	. { n
R_6E0C:
	ld a,(M_A083)		;6e0c	3a 83 a0	: . .
	inc a			;6e0f	3c		<
	jr z,l6e58h		;6e10	28 46		( F
	ld hl,(M_A003)		;6e12	2a 03 a0	* . .
	ld a,h			;6e15	7c		|
	or l			;6e16	b5		.
	jr nz,l6e58h		;6e17	20 3f		  ?
	ld hl,M_A003		;6e19	21 03 a0	! . .
	ld b,020h		;6e1c	06 20		.  
l6e1eh:
	ld a,(hl)		;6e1e	7e		~
	inc hl			;6e1f	23		#
	or (hl)			;6e20	b6		.
	jr nz,l6e28h		;6e21	20 05		  .
	inc hl			;6e23	23		#
	djnz l6e1eh		;6e24	10 f8		. .
	jr l6e58h		;6e26	18 30		. 0
l6e28h:
	ld d,(hl)		;6e28	56		V
	dec hl			;6e29	2b		+
	ld e,(hl)		;6e2a	5e		^
	ld hl,(M_A043)		;6e2b	2a 43 a0	* C .
	ld bc,(0a063h)		;6e2e	ed 4b 63 a0	. K c .
	or a			;6e32	b7		.
	sbc hl,de		;6e33	ed 52		. R
	jr c,l6e58h		;6e35	38 21		8 !
	ex de,hl		;6e37	eb		.
	xor a			;6e38	af		.
	sbc hl,bc		;6e39	ed 42		. B
	jr c,l6e58h		;6e3b	38 1b		8 .
	ld hl,D_A064		;6e3d	21 64 a0	! d .
	ld b,01dh		;6e40	06 1d		. .
l6e42h:
	inc hl			;6e42	23		#
	cp (hl)			;6e43	be		.
	jr nz,l6e4ah		;6e44	20 04		  .
	djnz l6e42h		;6e46	10 fa		. .
	xor a			;6e48	af		.
	ret			;6e49	c9		.
l6e4ah:
	ld hl,D_A064		;6e4a	21 64 a0	! d .
	ld a,01eh		;6e4d	3e 1e		> .
	ld b,a			;6e4f	47		G
l6e50h:
	inc hl			;6e50	23		#
	cp (hl)			;6e51	be		.
	jr nc,l6e58h		;6e52	30 04		0 .
	djnz l6e50h		;6e54	10 fa		. .
	xor a			;6e56	af		.
	ret			;6e57	c9		.
l6e58h:
	ld a,001h		;6e58	3e 01		> .
	or a			;6e5a	b7		.
	ret			;6e5b	c9		.
l6e5ch:
	call R_6E0C		;6e5c	cd 0c 6e	. . n
	jp nz,l6eb4h		;6e5f	c2 b4 6e	. . n
	ld de,0ca0ch		;6e62	11 0c ca	. . .
	call R_71DF		;6e65	cd df 71	. . q
	ld hl,0c004h		;6e68	21 04 c0	! . .
	ld bc,0099bh		;6e6b	01 9b 09	. . .
	call R_71C1		;6e6e	cd c1 71	. . q
	ld hl,0ca71h		;6e71	21 71 ca	! q .
	ld de,05b4fh		;6e74	11 4f 5b	. O [
	ld c,01eh		;6e77	0e 1e		. .
	ldir			;6e79	ed b0		. .
R_6E7B:
	ld a,006h		;6e7b	3e 06		> .
R_6E7D:
	ld hl,05b4fh		;6e7d	21 4f 5b	! O [
	ld (M_8EC3),hl		;6e80	22 c3 8e	" . .
	ld hl,05b6fh		;6e83	21 6f 5b	! o [
	ld (M_8EC5),hl		;6e86	22 c5 8e	" . .
	ld (M_907B),a		;6e89	32 7b 90	2 { .
	jp R_6A91		;6e8c	c3 91 6a	. . j
R_6E8F:
	ld b,h			;6e8f	44		D
	ld c,l			;6e90	4d		M
	ld de,0cbabh		;6e91	11 ab cb	. . .
	add hl,de		;6e94	19		.
	add ix,de		;6e95	dd 19		. .
	ex de,hl		;6e97	eb		.
	dec ix			;6e98	dd 2b		. +
	dec ix			;6e9a	dd 2b		. +
l6e9ch:
	ld a,(de)		;6e9c	1a		.
	ld l,a			;6e9d	6f		o
	inc de			;6e9e	13		.
	ld a,(de)		;6e9f	1a		.
	ld h,a			;6ea0	67		g
	dec de			;6ea1	1b		.
	or a			;6ea2	b7		.
	sbc hl,bc		;6ea3	ed 42		. B
	ld a,l			;6ea5	7d		}
	ld (de),a		;6ea6	12		.
	inc de			;6ea7	13		.
	ld a,h			;6ea8	7c		|
	ld (de),a		;6ea9	12		.
	inc de			;6eaa	13		.
	push ix			;6eab	dd e5		. .
	pop hl			;6ead	e1		.
	or a			;6eae	b7		.
	sbc hl,de		;6eaf	ed 52		. R
	jr nc,l6e9ch		;6eb1	30 e9		0 .
	ret			;6eb3	c9		.
l6eb4h:
	ld hl,VTII_INSTANCE	;6eb4	21 00 a0	! . .
	ld de,D_6F00		;6eb7	11 00 6f	. . o
	call R_71B1		;6eba	cd b1 71	. . q
	jr nz,$+76		;6ebd	20 4a		  J
	ld de,0cbabh		;6ebf	11 ab cb	. . .
	call R_71DF		;6ec2	cd df 71	. . q
	ld a,(0cbb3h)		;6ec5	3a b3 cb	: . .
	cp 034h			;6ec8	fe 34		. 4
	jr nc,l6ee0h		;6eca	30 14		0 .
	ld hl,0004ch		;6ecc	21 4c 00	! L .
	ld ix,(0cbf5h)		;6ecf	dd 2a f5 cb	. * . .
	call R_6E8F		;6ed3	cd 8f 6e	. . n
	ld hl,(0cbf5h)		;6ed6	2a f5 cb	* . .
	ld ix,(0cbf0h)		;6ed9	dd 2a f0 cb	. * . .
R_6EDD:
	call R_6E8F		;6edd	cd 8f 6e	. . n
l6ee0h:
	ld hl,0c008h		;6ee0	21 08 c0	! . .
	ld bc,00babh		;6ee3	01 ab 0b	. . .
	call R_71C1		;6ee6	cd c1 71	. . q
	ld hl,0cbc4h		;6ee9	21 c4 cb	! . .
	ld de,05b4fh		;6eec	11 4f 5b	. O [
	ld c,014h		;6eef	0e 14		. .
	ldir			;6ef1	ed b0		. .
	ld c,004h		;6ef3	0e 04		. .
	add hl,bc		;6ef5	09		.
	defb 011h,06fh		;6ef6	11 6f		. o
; --------------------------------------------------------------------------
; DATA 0x6EF8-0x710F  PSC/Pro Sound Creator detector strings + tables
; --------------------------------------------------------------------------
DATA_6EF8:
	defb 5B 0E 14 ED B0 C3 7B 6E  ;6EF8  [.....{n
	defb 50 53 43 20 56 31 2E 30  ;6F00  PSC V1.0
	defb 00 21 00 A0 11 3B 6F CD  ;6F08  .!...;o.
	defb B1 71 C2 56 6F 21 32 A0  ;6F10  .q.Vo!2.
	defb 13 CD B1 71 C2 56 6F 11  ;6F18  ...q.Vo.
	defb 65 CB CD DF 71 21 0A C0  ;6F20  e...q!..
	defb 01 65 0B CD C1 71 21 6D  ;6F28  .e...q!m
	defb CB 11 4F 5B 0E 2A ED B0  ;6F30  ..O[.*..
	defb C3 7B 6E 4D 6F 64 75 6C  ;6F38  .{nModul
	defb 65 3A 20 00 3B 46 61 73  ;6F40  e: .;Fas
	defb 74 20 54 72 61 63 6B 65  ;6F48  t Tracke
	defb 72 20 76 31 2E 00 21 0A  ;6F50  r v1..!.
	defb A0 11 84 6F CD B1 71 C2  ;6F58  ...o..q.
	defb A0 6F 3A 09 A0 B7 28 54  ;6F60  .o:...(T
	defb 11 68 C7 CD DF 71 21 0C  ;6F68  .h...q!.
	defb C0 01 68 07 CD C1 71 21  ;6F70  ..h...q!
	defb 8E C7 11 4F 5B 0E 19 ED  ;6F78  ...O[...
	defb B0 C3 7B 6E 4B 53 41 20  ;6F80  ..{nKSA 
	defb 53 4F 46 54 57 41 52 45  ;6F88  SOFTWARE
	defb 20 43 4F 4D 50 49 4C 41  ;6F90   COMPILA
	defb 54 49 4F 4E 20 4F 46 00  ;6F98  TION OF.
	defb 21 00 A0 7E B7 28 15 FE  ;6FA0  !..~.(..
	defb 18 30 11 06 04 23 23 7E  ;6FA8  .0...##~
	defb B7 28 09 FE 20 30 05 10  ;6FB0  .(.. 0..
	defb F4 C3 62 6F 21 01 A0 11  ;6FB8  ..bo!...
	defb E4 6F CD B1 71 C2 E8 6F  ;6FC0  .o..q..o
	defb 11 9A C6 CD DF 71 21 0E  ;6FC8  .....q!.
	defb C0 01 9A 06 CD C1 71 21  ;6FD0  ......q!
	defb A1 C6 11 4F 5B 0E 20 ED  ;6FD8  ...O[. .
	defb B0 C3 7B 6E 47 54 52 00  ;6FE0  ..{nGTR.
	defb 21 00 A0 7E B7 CA 81 70  ;6FE8  !..~...p
	defb 3D FE 40 D2 81 70 23 7E  ;6FF0  =.@..p#~
	defb FE 60 D2 81 70 23 01 20  ;6FF8  .`..p#. 
	defb 03 23 7E B9 D2 81 70 79  ;7000  .#~...py
	defb C6 10 4F 23 10 F3 7E FE  ;7008  ..O#..~.
	defb 80 D2 81 70 2A 02 A0 11  ;7010  ...p*...
	defb 00 A0 19 4E 23 46 23 5E  ;7018  ...N#F#^
	defb 23 56 EB B7 ED 42 DA 81  ;7020  #V...B..
	defb 70 11 B2 C5 CD DF 71 21  ;7028  p.....q!
	defb 10 C0 01 B2 05 CD C1 71  ;7030  .......q
	defb 21 B2 C5 01 80 00 78 B1  ;7038  !.....x.
	defb 28 13 11 6D 70 1A ED B1  ;7040  (..mp...
	defb 20 0B 13 1A B7 28 09 BE  ;7048   ....(..
	defb 20 EC 23 18 F5 21 0F 5B  ;7050   .#..!.[
	defb 11 4F 5B 01 14 00 ED B0  ;7058  .O[.....
	defb 0E 04 09 11 6F 5B 0E 14  ;7060  ....o[..
	defb ED B0 C3 7B 6E 41 53 4D  ;7068  ...{nASM
	defb 20 43 4F 4D 50 49 4C 41  ;7070   COMPILA
	defb 54 49 4F 4E 20 4F 46 20  ;7078  TION OF 
	defb 00 21 01 A0 06 40 7E 23  ;7080  .!...@~#
	defb 23 FE 60 38 02 10 F7 78  ;7088  #.`8...x
	defb FE 3C 30 22 7E FE 60 38  ;7090  .<0"~.`8
	defb 1D 2A 00 A0 ED 5B 02 A0  ;7098  .*...[..
	defb ED 52 38 12 11 E6 C5 CD  ;70A0  .R8.....
	defb DF 71 21 1A C0 01 E6 05  ;70A8  .q!.....
	defb CD C1 71 C3 7B 6E 21 00  ;70B0  ..q.{n!.
	defb A0 11 D9 70 CD B1 71 20  ;70B8  ...p..q 
	defb 1D 23 7E FE 60 38 17 C3  ;70C0  .#~.`8..
	defb 5E 6B CD DF 71 21 02 C0  ;70C8  ^k..q!..
	defb 01 11 05 CD BA 71 C3 7B  ;70D0  .....q.{
	defb 6E 46 58 53 4D 00 21 00  ;70D8  nFXSM.!.
	defb A0 7E FE 18 30 31 23 23  ;70E0  .~..01##
	defb 23 7E 23 B6 20 29 21 45  ;70E8  #~#. )!E
	defb A0 06 1D 7E 23 FE 20 38  ;70F0  ...~#. 8
	defb 1E 10 F8 C3 5E 6B CD DF  ;70F8  ....^k..
	defb 71 21 04 C0 01 C6 06 CD  ;7100  q!......
	defb BA 71 21 0B C7 11 4F 5B  ;7108  .q!...O[


; ..........................................................................
; code segment restart 0x7110 - clean decode boundary after data above
; ..........................................................................
;PUSH BC / LD A,#? / AND 7 / OR #10 / LD BC,#7FFD / OUT (C),A / POP BC / RET
;Pages RAM bank (A & 7) into the 0xC000 window, keeping bit 4 (screen)
;and bit 3 (48K ROM) intact. Used by every bank-aware player routine.
;TurboSound mode: 0 = single chip, 1/2 = dual chip.
;Gates the dual-chip init (86EF) and the per-frame switch (907D).
;Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above.
;player-type / TS state bytes 6B03..6B05 (6B05=1 here)
;Mute both chips: OUT #FFFD,#FE + R8/R9/R10:=0, then OUT #FFFD,#FF +
;R8/R9/R10:=0. In the captured run this is the ONLY code that ever
;touched chip 2 after the failed probe (9 writes total).
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).
;signature "=VTII PT3 Player r.7="

	ld c,01eh		;7110	0e 1e		. .
	ldir			;7112	ed b0		. .
	jp R_6E7B		;7114	c3 7b 6e	. { n
	call R_84B2		;7117	cd b2 84	. . .
	call R_7BAD		;711a	cd ad 7b	. . {
	jp nz,R_6B5E		;711d	c2 5e 6b	. ^ k
	ld hl,(M_8F13)		;7120	2a 13 8f	* . .
	ld de,(VTII_INSTANCE)	;7123	ed 5b 00 a0	. [ . .
	ld (07154h),de		;7127	ed 53 54 71	. S T q
	sbc hl,de		;712b	ed 52		. R
	jr c,l7197h		;712d	38 68		8 h
	ld a,h			;712f	7c		|
	cp 003h			;7130	fe 03		. .
	jr l7197h		;7132	18 63		. c
	call R_84B6		;7134	cd b6 84	. . .
	ld hl,VTII_INSTANCE	;7137	21 00 a0	! . .
	ld de,0e000h		;713a	11 00 e0	. . .
	ld bc,02000h		;713d	01 00 20	. .  
	push hl			;7140	e5		.
	push de			;7141	d5		.
	push bc			;7142	c5		.
	ldir			;7143	ed b0		. .
	call R_71A2		;7145	cd a2 71	. . q
	call R_84B6		;7148	cd b6 84	. . .
	pop bc			;714b	c1		.
	pop hl			;714c	e1		.
	pop de			;714d	d1		.
	ldir			;714e	ed b0		. .
	call R_84B2		;7150	cd b2 84	. . .
	ld hl,00000h		;7153	21 00 00	! . .
	ld de,VTII_INSTANCE	;7156	11 00 a0	. . .
	add hl,de		;7159	19		.
	ld bc,03abch		;715a	01 bc 3a	. . :
	ldir			;715d	ed b0		. .
	call R_7BAD		;715f	cd ad 7b	. . {
	jr nz,l719ah		;7162	20 36		  6
	ld hl,VTII_INSTANCE	;7164	21 00 a0	! . .
	exx			;7167	d9		.
	ld de,0c544h		;7168	11 44 c5	. D .
	exx			;716b	d9		.
	call R_6CA4		;716c	cd a4 6c	. . l
	ld hl,R_84B6		;716f	21 b6 84	! . .
	ld (M_71D5),hl		;7172	22 d5 71	" . q
	ld hl,0c012h		;7175	21 12 c0	! . .
	ld bc,00544h		;7178	01 44 05	. D .
	call R_71C1		;717b	cd c1 71	. . q
	ld hl,R_8486		;717e	21 86 84	! . .
	ld (M_71D5),hl		;7181	22 d5 71	" . q
	call 0c000h		;7184	cd 00 c0	. . .
	ld a,001h		;7187	3e 01		> .
	ld (TS_MODE_FLAG),a	;7189	32 02 6b	2 . k
	ld (TS_STATE),a		;718c	32 03 6b	2 . k
	ld hl,0c064h		;718f	21 64 c0	! d .
	ld (M_9094),hl		;7192	22 94 90	" . .
	jr l719ah		;7195	18 03		. .
l7197h:
	call R_71A2		;7197	cd a2 71	. . q
l719ah:
	call R_8486		;719a	cd 86 84	. . .
	ld a,064h		;719d	3e 64		> d
	jp R_6E7D		;719f	c3 7d 6e	. } n
R_71A2:
	ld de,0c544h		;71a2	11 44 c5	. D .
	call R_71DF		;71a5	cd df 71	. . q
	ld hl,0c012h		;71a8	21 12 c0	! . .
	ld bc,00544h		;71ab	01 44 05	. D .
	jp R_71C1		;71ae	c3 c1 71	. . q
R_71B1:
	ld a,(de)		;71b1	1a		.
	or a			;71b2	b7		.
	ret z			;71b3	c8		.
	cp (hl)			;71b4	be		.
	ret nz			;71b5	c0		.
	inc hl			;71b6	23		#
	inc de			;71b7	13		.
	jr R_71B1		;71b8	18 f7		. .
R_71BA:
	exx			;71ba	d9		.
	call R_8492		;71bb	cd 92 84	. . .
	exx			;71be	d9		.
	jr l71c6h		;71bf	18 05		. .
R_71C1:
	exx			;71c1	d9		.
	call R_848E		;71c2	cd 8e 84	. . .
	exx			;71c5	d9		.
l71c6h:
	ld a,(hl)		;71c6	7e		~
	inc hl			;71c7	23		#
	ld h,(hl)		;71c8	66		f
	ld l,a			;71c9	6f		o
	ld de,VTII_INSTANCE	;71ca	11 00 a0	. . .
	push de			;71cd	d5		.
	push bc			;71ce	c5		.
	di			;71cf	f3		.
	call 0c000h		;71d0	cd 00 c0	. . .
	ei			;71d3	fb		.
	call R_8486		;71d4	cd 86 84	. . .
	pop bc			;71d7	c1		.
	pop hl			;71d8	e1		.
	ld de,0c000h		;71d9	11 00 c0	. . .
	ldir			;71dc	ed b0		. .
	ret			;71de	c9		.
R_71DF:
	call R_8486		;71df	cd 86 84	. . .
	ld hl,VTII_INSTANCE	;71e2	21 00 a0	! . .
	ld bc,02000h		;71e5	01 00 20	. .  
	push hl			;71e8	e5		.
	ldir			;71e9	ed b0		. .
	ld (M_8F11),de		;71eb	ed 53 11 8f	. S . .
	call R_84B2		;71ef	cd b2 84	. . .
	pop de			;71f2	d1		.
	ld hl,0c000h		;71f3	21 00 c0	! . .
	ld bc,01c00h		;71f6	01 00 1c	. . .
	push bc			;71f9	c5		.
	ldir			;71fa	ed b0		. .
	call R_8486		;71fc	cd 86 84	. . .
	pop bc			;71ff	c1		.
	ld hl,VTII_INSTANCE	;7200	21 00 a0	! . .
	ld de,(M_8F11)		;7203	ed 5b 11 8f	. [ . .
	ldir			;7207	ed b0		. .
	ret			;7209	c9		.
D_720A:
	ld hl,(M_8F1A)		;720a	2a 1a 8f	* . .
l720dh:
	ld de,M_7B51		;720d	11 51 7b	. Q {
	ld bc,00004h		;7210	01 04 00	. . .
	ldir			;7213	ed b0		. .
	jr l7225h		;7215	18 0e		. .
	ld a,(M_9A02)		;7217	3a 02 9a	: . .
	or a			;721a	b7		.
	jp nz,l7809h		;721b	c2 09 78	. . x
	push hl			;721e	e5		.
	call R_9A9B		;721f	cd 9b 9a	. . .
	pop hl			;7222	e1		.
	jr l720dh		;7223	18 e8		. .
l7225h:
	ld bc,00005h		;7225	01 05 00	. . .
	add hl,bc		;7228	09		.
	push hl			;7229	e5		.
	ld hl,D_8EEA		;722a	21 ea 8e	! . .
	ld de,D_8EF4		;722d	11 f4 8e	. . .
	ld c,008h		;7230	0e 08		. .
	ldir			;7232	ed b0		. .
	pop hl			;7234	e1		.
	ld a,(hl)		;7235	7e		~
	ex af,af'		;7236	08		.
	ld de,D_8EEA		;7237	11 ea 8e	. . .
	ld c,008h		;723a	0e 08		. .
	ldir			;723c	ed b0		. .
	call R_8482		;723e	cd 82 84	. . .
	ex af,af'		;7241	08		.
	cp 02eh			;7242	fe 2e		. .
	jr nz,l7280h		;7244	20 3a		  :
	ld hl,(M_8F02)		;7246	2a 02 8f	* . .
	ld a,h			;7249	7c		|
	or l			;724a	b5		.
	jr nz,l7280h		;724b	20 33		  3
	ld hl,(M_8EFE)		;724d	2a fe 8e	* . .
	ld a,l			;7250	7d		}
	or a			;7251	b7		.
	jr nz,l725dh		;7252	20 09		  .
	ld a,h			;7254	7c		|
	cp 0e1h			;7255	fe e1		. .
	jr nz,l725dh		;7257	20 04		  .
	ld bc,0000bh		;7259	01 0b 00	. . .
	add hl,bc		;725c	09		.
l725dh:
	dec hl			;725d	2b		+
	ld de,D_8EF1		;725e	11 f1 8e	. . .
	ld bc,00008h		;7261	01 08 00	. . .
	lddr			;7264	ed b8		. .
	ld a,(hl)		;7266	7e		~
	dec hl			;7267	2b		+
	ld d,(hl)		;7268	56		V
	dec hl			;7269	2b		+
	ld e,(hl)		;726a	5e		^
	ld (M_8F00),de		;726b	ed 53 00 8f	. S . .
	ld (M_8ECE),a		;726f	32 ce 8e	2 . .
	ld (M_8EFE),hl		;7272	22 fe 8e	" . .
	xor a			;7275	af		.
	call R_8616		;7276	cd 16 86	. . .
	ld de,0c009h		;7279	11 09 c0	. . .
	add hl,de		;727c	19		.
	jp R_622B		;727d	c3 2b 62	. + b
l7280h:
	ld hl,(M_8EFE)		;7280	2a fe 8e	* . .
	ld de,(M_8F00)		;7283	ed 5b 00 8f	. [ . .
	ld a,(M_8ECE)		;7287	3a ce 8e	: . .
R_728A:
	ld (hl),e		;728a	73		s
	inc hl			;728b	23		#
	ld (hl),d		;728c	72		r
	inc hl			;728d	23		#
	ld (hl),a		;728e	77		w
	inc hl			;728f	23		#
	ld de,D_8EF4		;7290	11 f4 8e	. . .
	ld bc,00008h		;7293	01 08 00	. . .
	ex de,hl		;7296	eb		.
	ldir			;7297	ed b0		. .
	ld (M_8EFE),de		;7299	ed 53 fe 8e	. S . .
	jp R_621D		;729d	c3 1d 62	. . b
R_72A0:
	xor a			;72a0	af		.
	ld (M_8EC8),a		;72a1	32 c8 8e	2 . .
	ld (M_8EC9),a		;72a4	32 c9 8e	2 . .
	ld h,a			;72a7	67		g
	ld l,a			;72a8	6f		o
	ld (M_8EC1),hl		;72a9	22 c1 8e	" . .
	ld (TS_MODE_FLAG),hl	;72ac	22 02 6b	" . k
	call MUTE_BOTH		;72af	cd 61 8a	. a .
	call R_8053		;72b2	cd 53 80	. S .
	xor a			;72b5	af		.
	call R_6456		;72b6	cd 56 64	. V d
	ld a,0f3h		;72b9	3e f3		> .
	jp R_6456		;72bb	c3 56 64	. V d
R_72BE:
	call R_89B4		;72be	cd b4 89	. . .
	jp nz,R_6271		;72c1	c2 71 62	. q b
	call R_89F3		;72c4	cd f3 89	. . .
	call nz,R_72A0		;72c7	c4 a0 72	. . r
R_72CA:
	call R_8A03		;72ca	cd 03 8a	. . .
	jr nz,l72ech		;72cd	20 1d		  .
	call R_89FF		;72cf	cd ff 89	. . .
	jp z,R_6271		;72d2	ca 71 62	. q b
	call R_72F6		;72d5	cd f6 72	. . r
	call R_8496		;72d8	cd 96 84	. . .
	ld bc,D_7FFD		;72db	01 fd 7f	. . .
	xor a			;72de	af		.
	out (c),a		;72df	ed 79		. y
	di			;72e1	f3		.
	ld a,03fh		;72e2	3e 3f		> ?
	ld i,a			;72e4	ed 47		. G
	im 1			;72e6	ed 56		. V
	ei			;72e8	fb		.
	jp 00000h		;72e9	c3 00 00	. . .
l72ech:
	call R_72F6		;72ec	cd f6 72	. . r
	ld hl,00000h		;72ef	21 00 00	! . .
	push hl			;72f2	e5		.
	jp 03d2fh		;72f3	c3 2f 3d	. / =
R_72F6:
	di			;72f6	f3		.
	call MUTE_BOTH		;72f7	cd 61 8a	. a .
	ld a,(M_8ED6)		;72fa	3a d6 8e	: . .
	or a			;72fd	b7		.
	ret nz			;72fe	c0		.
	ld a,(M_8F16)		;72ff	3a 16 8f	: . .
	or a			;7302	b7		.
	call nz,09a44h		;7303	c4 44 9a	. D .
	ret			;7306	c9		.
R_7307:
	xor a			;7307	af		.
	ld (M_8EC9),a		;7308	32 c9 8e	2 . .
	ld hl,M_8ECE		;730b	21 ce 8e	! . .
	ld a,(hl)		;730e	7e		~
	cp 003h			;730f	fe 03		. .
	jr c,l731ah		;7311	38 07		8 .
	dec (hl)		;7313	35		5
l7314h:
	ld a,001h		;7314	3e 01		> .
	ld (M_8C29),a		;7316	32 29 8c	2 ) .
	ret			;7319	c9		.
l731ah:
	ld a,001h		;731a	3e 01		> .
	ld (M_8ED4),a		;731c	32 d4 8e	2 . .
	ret			;731f	c9		.
R_7320:
	xor a			;7320	af		.
	ld (M_8EC9),a		;7321	32 c9 8e	2 . .
	ld hl,M_8ECE		;7324	21 ce 8e	! . .
	ld a,(hl)		;7327	7e		~
	cp 014h			;7328	fe 14		. .
	jr nc,l732fh		;732a	30 03		0 .
	inc (hl)		;732c	34		4
	jr l7314h		;732d	18 e5		. .
l732fh:
	ld a,002h		;732f	3e 02		> .
	ld (M_8ED4),a		;7331	32 d4 8e	2 . .
	ret			;7334	c9		.
R_7335:
	call R_8389		;7335	cd 89 83	. . .
	ld hl,05949h		;7338	21 49 59	! I Y
	ld b,00eh		;733b	06 0e		. .
	ld a,001h		;733d	3e 01		> .
l733fh:
	ld (hl),a		;733f	77		w
	inc hl			;7340	23		#
	djnz l733fh		;7341	10 fc		. .
	ld hl,D_81FD		;7343	21 fd 81	! . .
	ld (M_830F),hl		;7346	22 0f 83	" . .
	ld hl,R_8482		;7349	21 82 84	! . .
	ld (M_82DA),hl		;734c	22 da 82	" . .
	ld hl,0d802h		;734f	21 02 d8	! . .
	ld (M_829B),hl		;7352	22 9b 82	" . .
	ld a,0f0h		;7355	3e f0		> .
	ld (M_82B2),a		;7357	32 b2 82	2 . .
	ld a,010h		;735a	3e 10		> .
	ld (M_82F8),a		;735c	32 f8 82	2 . .
R_735F:
	ld a,002h		;735f	3e 02		> .
R_7361:
	ld (M_8ED5),a		;7361	32 d5 8e	2 . .
	ld a,(M_7329)		;7364	3a 29 73	: ) s
	ld l,a			;7367	6f		o
	ld a,(M_8321)		;7368	3a 21 83	: ! .
	ld (M_7329),a		;736b	32 29 73	2 ) s
	ld a,l			;736e	7d		}
	ld (M_8321),a		;736f	32 21 83	2 ! .
	ld hl,(M_8ECD)		;7372	2a cd 8e	* . .
	ld de,(M_8ECF)		;7375	ed 5b cf 8e	. [ . .
	ld (M_8ECD),de		;7379	ed 53 cd 8e	. S . .
	ld (M_8ECF),hl		;737d	22 cf 8e	" . .
	ld hl,(M_8F00)		;7380	2a 00 8f	* . .
	ld de,(M_8F04)		;7383	ed 5b 04 8f	. [ . .
	ld (M_8F00),de		;7387	ed 53 00 8f	. S . .
	ld (M_8F04),hl		;738b	22 04 8f	" . .
	ld hl,(M_85AE)		;738e	2a ae 85	* . .
	ld de,(M_85B2)		;7391	ed 5b b2 85	. [ . .
	ld (M_85AE),de		;7395	ed 53 ae 85	. S . .
	ld (M_85B2),hl		;7399	22 b2 85	" . .
	ld hl,(M_85B0)		;739c	2a b0 85	* . .
	ld de,(M_85B4)		;739f	ed 5b b4 85	. [ . .
	ld (M_85B0),de		;73a3	ed 53 b0 85	. S . .
	ld (M_85B4),hl		;73a7	22 b4 85	" . .
	ld hl,(M_831D)		;73aa	2a 1d 83	* . .
	ld de,(M_831F)		;73ad	ed 5b 1f 83	. [ . .
	ld (M_831F),hl		;73b1	22 1f 83	" . .
	ld (M_831D),de		;73b4	ed 53 1d 83	. S . .
	ret			;73b8	c9		.
R_73B9:
	call R_8376		;73b9	cd 76 83	. v .
	ld hl,05949h		;73bc	21 49 59	! I Y
	ld b,00eh		;73bf	06 0e		. .
	ld a,006h		;73c1	3e 06		> .
l73c3h:
	ld (hl),a		;73c3	77		w
	inc hl			;73c4	23		#
	djnz l73c3h		;73c5	10 fc		. .
	ld hl,D_822C		;73c7	21 2c 82	! , .
	ld (M_830F),hl		;73ca	22 0f 83	" . .
	ld hl,R_848A		;73cd	21 8a 84	! . .
	ld (M_82DA),hl		;73d0	22 da 82	" . .
	ld hl,0c009h		;73d3	21 09 c0	! . .
	ld (M_829B),hl		;73d6	22 9b 82	" . .
	ld a,0ebh		;73d9	3e eb		> .
	ld (M_82B2),a		;73db	32 b2 82	2 . .
	ld a,015h		;73de	3e 15		> .
	ld (M_82F8),a		;73e0	32 f8 82	2 . .
R_73E3:
	xor a			;73e3	af		.
	jp R_7361		;73e4	c3 61 73	. a s
R_73E7:
	ld a,(M_8ED5)		;73e7	3a d5 8e	: . .
	or a			;73ea	b7		.
	jp z,R_8376		;73eb	ca 76 83	. v .
	jp R_8389		;73ee	c3 89 83	. . .
	ld hl,0c000h		;73f1	21 00 c0	! . .
	ld b,004h		;73f4	06 04		. .
	call R_769E		;73f6	cd 9e 76	. . v
	ld hl,0c000h		;73f9	21 00 c0	! . .
	ret			;73fc	c9		.
	ld hl,04000h		;73fd	21 00 40	! . @
	ld de,VTII_INSTANCE	;7400	11 00 a0	. . .
	jr l740bh		;7403	18 06		. .
	ld hl,VTII_INSTANCE	;7405	21 00 a0	! . .
	ld de,04000h		;7408	11 00 40	. . @
l740bh:
	ld bc,01b00h		;740b	01 00 1b	. . .
	ldir			;740e	ed b0		. .
	ret			;7410	c9		.
l7411h:
	call R_809E		;7411	cd 9e 80	. . .
	call R_80DC		;7414	cd dc 80	. . .
	ld a,000h		;7417	3e 00		> .
	jr l7437h		;7419	18 1c		. .
l741bh:
	call R_809E		;741b	cd 9e 80	. . .
	call R_80D4		;741e	cd d4 80	. . .
	ld a,001h		;7421	3e 01		> .
	jr l7437h		;7423	18 12		. .
l7425h:
	call R_809E		;7425	cd 9e 80	. . .
	call R_80CA		;7428	cd ca 80	. . .
	ld a,002h		;742b	3e 02		> .
	jr l7437h		;742d	18 08		. .
l742fh:
	call R_809E		;742f	cd 9e 80	. . .
	call R_80C0		;7432	cd c0 80	. . .
	ld a,003h		;7435	3e 03		> .
l7437h:
	ld bc,(M_9341)		;7437	ed 4b 41 93	. K A .
	ld (M_9341),a		;743b	32 41 93	2 A .
	push bc			;743e	c5		.
	ld a,001h		;743f	3e 01		> .
	ld (M_8EC2),a		;7441	32 c2 8e	2 . .
	call MUTE_BOTH		;7444	cd 61 8a	. a .
	call R_9125		;7447	cd 25 91	. % .
	pop bc			;744a	c1		.
	jr z,l745bh		;744b	28 0e		( .
	ld a,c			;744d	79		y
	and 003h		;744e	e6 03		. .
	or a			;7450	b7		.
	jr z,l7411h		;7451	28 be		( .
	dec a			;7453	3d		=
	jr z,l741bh		;7454	28 c5		( .
	dec a			;7456	3d		=
	jr z,l7425h		;7457	28 cc		( .
	jr l742fh		;7459	18 d4		. .
l745bh:
	ld a,(M_9341)		;745b	3a 41 93	: A .
	add a,041h		;745e	c6 41		. A
	ld (M_8E75),a		;7460	32 75 8e	2 u .
	call R_9129		;7463	cd 29 91	. ) .
	ld hl,00000h		;7466	21 00 00	! . .
	ld (M_8F00),hl		;7469	22 00 8f	" . .
	ld hl,00218h		;746c	21 18 02	! . .
	ld (M_8ECD),hl		;746f	22 cd 8e	" . .
	ld hl,0d802h		;7472	21 02 d8	! . .
	ld (M_85AE),hl		;7475	22 ae 85	" . .
	call R_8190		;7478	cd 90 81	. . .
	xor a			;747b	af		.
	ld (M_8EC2),a		;747c	32 c2 8e	2 . .
	ld de,D_8E71		;747f	11 71 8e	. q .
	ld hl,D_9960		;7482	21 60 99	! ` .
	call R_8DD3		;7485	cd d3 8d	. . .
	jp R_851B		;7488	c3 1b 85	. . .
R_748B:
	or a			;748b	b7		.
	ld b,000h		;748c	06 00		. .
	ld c,a			;748e	4f		O
	ld a,00dh		;748f	3e 0d		> .
	jr z,l74adh		;7491	28 1a		( .
l7493h:
	dec a			;7493	3d		=
	ldi			;7494	ed a0		. .
	jp pe,l7493h		;7496	ea 93 74	. . t
	ld b,a			;7499	47		G
	dec hl			;749a	2b		+
	ld a,(hl)		;749b	7e		~
	cp 031h			;749c	fe 31		. 1
	jr nz,l74aah		;749e	20 0a		  .
	dec hl			;74a0	2b		+
	ld a,(hl)		;74a1	7e		~
	cp 03bh			;74a2	fe 3b		. ;
	jr nz,l74aah		;74a4	20 04		  .
	inc b			;74a6	04		.
	inc b			;74a7	04		.
	dec de			;74a8	1b		.
	dec de			;74a9	1b		.
l74aah:
	ld a,b			;74aa	78		x
	or a			;74ab	b7		.
	ret z			;74ac	c8		.
l74adh:
	ld b,a			;74ad	47		G
	ld a,020h		;74ae	3e 20		>  
l74b0h:
	ld (de),a		;74b0	12		.
	inc de			;74b1	13		.
	djnz l74b0h		;74b2	10 fc		. .
	ret			;74b4	c9		.
R_74B5:
	xor a			;74b5	af		.
R_74B6:
	ld (hl),a		;74b6	77		w
	inc hl			;74b7	23		#
	djnz R_74B6		;74b8	10 fc		. .
	ret			;74ba	c9		.
l74bbh:
	ex de,hl		;74bb	eb		.
	ld hl,(M_803F)		;74bc	2a 3f 80	* ? .
	ld bc,0e800h		;74bf	01 00 e8	. . .
	add hl,bc		;74c2	09		.
	ld (M_803F),hl		;74c3	22 3f 80	" ? .
	ex de,hl		;74c6	eb		.
	xor a			;74c7	af		.
	ret			;74c8	c9		.
l74c9h:
	ld b,020h		;74c9	06 20		.  
	inc hl			;74cb	23		#
l74cch:
	ld a,(hl)		;74cc	7e		~
D_74CD:
	inc hl			;74cd	23		#
	cp (hl)			;74ce	be		.
	jr nz,l74d7h		;74cf	20 06		  .
	inc hl			;74d1	23		#
	djnz l74cch		;74d2	10 f8		. .
	ld a,001h		;74d4	3e 01		> .
	ret			;74d6	c9		.
l74d7h:
	dec hl			;74d7	2b		+
R_74D8:
	ld a,h			;74d8	7c		|
	cp 0a0h			;74d9	fe a0		. .
	jr z,l74eeh		;74db	28 11		( .
D_74DD:
	push hl			;74dd	e5		.
	ld de,(M_803F)		;74de	ed 5b 3f 80	. [ ? .
	or a			;74e2	b7		.
	sbc hl,de		;74e3	ed 52		. R
	ld a,h			;74e5	7c		|
	cp 0a0h			;74e6	fe a0		. .
	pop hl			;74e8	e1		.
	jr c,l74eeh		;74e9	38 03		8 .
	ld a,016h		;74eb	3e 16		> .
	ret			;74ed	c9		.
l74eeh:
	ld a,(M_8F0F)		;74ee	3a 0f 8f	: . .
	or a			;74f1	b7		.
	jr z,l74f9h		;74f2	28 05		( .
	ld a,h			;74f4	7c		|
	cp 0bdh			;74f5	fe bd		. .
	jr nc,l74bbh		;74f7	30 c2		0 .
l74f9h:
	ld a,(hl)		;74f9	7e		~
	ld c,a			;74fa	4f		O
	inc hl			;74fb	23		#
	or (hl)			;74fc	b6		.
	jr z,l74c9h		;74fd	28 ca		( .
	ld b,(hl)		;74ff	46		F
	push hl			;7500	e5		.
	push bc			;7501	c5		.
	ld de,00018h		;7502	11 18 00	. . .
	add hl,de		;7505	19		.
	ld a,(hl)		;7506	7e		~
	cp 002h			;7507	fe 02		. .
	jr nz,l752bh		;7509	20 20		   
	ld e,008h		;750b	1e 08		. .
	add hl,de		;750d	19		.
	ld a,(hl)		;750e	7e		~
	cp 001h			;750f	fe 01		. .
	jr z,l7521h		;7511	28 0e		( .
	or a			;7513	b7		.
	jr nz,l751ch		;7514	20 06		  .
	pop bc			;7516	c1		.
	pop hl			;7517	e1		.
	add hl,bc		;7518	09		.
	dec hl			;7519	2b		+
	jr R_74D8		;751a	18 bc		. .
l751ch:
	or a			;751c	b7		.
	sbc hl,de		;751d	ed 52		. R
	jr l752bh		;751f	18 0a		. .
l7521h:
	dec hl			;7521	2b		+
	ld a,(hl)		;7522	7e		~
	inc hl			;7523	23		#
	or a			;7524	b7		.
	jr z,l7529h		;7525	28 02		( .
	ld (hl),02eh		;7527	36 2e		6 .
l7529h:
	sbc hl,de		;7529	ed 52		. R
l752bh:
	ld de,0ffeeh		;752b	11 ee ff	. . .
	add hl,de		;752e	19		.
	ld de,(M_8ED8)		;752f	ed 5b d8 8e	. [ . .
	call R_9AA4		;7533	cd a4 9a	. . .
	ld bc,00004h		;7536	01 04 00	. . .
	add hl,bc		;7539	09		.
	call R_9AA1		;753a	cd a1 9a	. . .
	inc hl			;753d	23		#
	inc hl			;753e	23		#
	inc hl			;753f	23		#
	add hl,bc		;7540	09		.
	ldi			;7541	ed a0		. .
	inc hl			;7543	23		#
	inc hl			;7544	23		#
	inc c			;7545	0c		.
	add hl,bc		;7546	09		.
	ld a,(hl)		;7547	7e		~
	cp 00fh			;7548	fe 0f		. .
	jr c,l754eh		;754a	38 02		8 .
	ld a,00eh		;754c	3e 0e		> .
l754eh:
	inc hl			;754e	23		#
	call R_748B		;754f	cd 8b 74	. . t
	ld (M_8ED8),de		;7552	ed 53 d8 8e	. S . .
	pop bc			;7556	c1		.
	pop hl			;7557	e1		.
	add hl,bc		;7558	09		.
	dec hl			;7559	2b		+
	jp R_74D8		;755a	c3 d8 74	. . t
D_755D:
	call R_848A		;755d	cd 8a 84	. . .
	call R_9A9B		;7560	cd 9b 9a	. . .
	call R_75A6		;7563	cd a6 75	. . u
l7566h:
	ld hl,VTII_INSTANCE	;7566	21 00 a0	! . .
	ld a,001h		;7569	3e 01		> .
	ld (M_8F18),a		;756b	32 18 8f	2 . .
	ld b,001h		;756e	06 01		. .
	call R_769E		;7570	cd 9e 76	. . v
	ld (hl),000h		;7573	36 00		6 .
	call R_75C1		;7575	cd c1 75	. . u
	ld a,h			;7578	7c		|
	cp 0a8h			;7579	fe a8		. .
	jr nz,R_75AB		;757b	20 2e		  .
	ld a,(M_9A04)		;757d	3a 04 9a	: . .
	cp 00fh			;7580	fe 0f		. .
	jr z,R_75AB		;7582	28 27		( '
	call R_7589		;7584	cd 89 75	. . u
	jr l7566h		;7587	18 dd		. .
R_7589:
	ld hl,D_75C0		;7589	21 c0 75	! . u
	ld a,(hl)		;758c	7e		~
	inc (hl)		;758d	34		4
	cp 00ah			;758e	fe 0a		. .
	jr c,l759bh		;7590	38 09		8 .
	cp 014h			;7592	fe 14		. .
	jr nc,R_75A3		;7594	30 0d		0 .
	ld hl,05901h		;7596	21 01 59	! . Y
	jr l759eh		;7599	18 03		. .
l759bh:
	ld hl,058ebh		;759b	21 eb 58	! . X
l759eh:
	add a,l			;759e	85		.
	ld l,a			;759f	6f		o
	ld (hl),038h		;75a0	36 38		6 8
	ret			;75a2	c9		.
R_75A3:
	call R_75AB		;75a3	cd ab 75	. . u
R_75A6:
	xor a			;75a6	af		.
	ld (D_75C0),a		;75a7	32 c0 75	2 . u
	ret			;75aa	c9		.
R_75AB:
	ld hl,058ebh		;75ab	21 eb 58	! . X
	ld b,00ah		;75ae	06 0a		. .
	ld a,007h		;75b0	3e 07		> .
l75b2h:
	ld (hl),a		;75b2	77		w
	inc hl			;75b3	23		#
	djnz l75b2h		;75b4	10 fc		. .
	ld hl,0590bh		;75b6	21 0b 59	! . Y
	ld b,00ah		;75b9	06 0a		. .
l75bbh:
	ld (hl),a		;75bb	77		w
	inc hl			;75bc	23		#
	djnz l75bbh		;75bd	10 fc		. .
	ret			;75bf	c9		.
D_75C0:
	nop			;75c0	00		.
R_75C1:
	ld hl,M_8F18		;75c1	21 18 8f	! . .
	ld a,(hl)		;75c4	7e		~
	or a			;75c5	b7		.
	ret z			;75c6	c8		.
	ld b,000h		;75c7	06 00		. .
	ld (hl),b		;75c9	70		p
	ld hl,VTII_INSTANCE	;75ca	21 00 a0	! . .
R_75CD:
	ld a,(hl)		;75cd	7e		~
	or a			;75ce	b7		.
	ret z			;75cf	c8		.
	cp 02eh			;75d0	fe 2e		. .
	jp nz,l75ddh		;75d2	c2 dd 75	. . u
	inc hl			;75d5	23		#
	ld a,(hl)		;75d6	7e		~
	cp 02eh			;75d7	fe 2e		. .
	jp nz,l7663h		;75d9	c2 63 76	. c v
	dec hl			;75dc	2b		+
l75ddh:
	cp 0e5h			;75dd	fe e5		. .
	jp z,l7664h		;75df	ca 64 76	. d v
	cp 020h			;75e2	fe 20		.  
	jp c,l7664h		;75e4	da 64 76	. d v
	inc hl			;75e7	23		#
	ld a,(hl)		;75e8	7e		~
	cp 020h			;75e9	fe 20		.  
	jr c,l7663h		;75eb	38 76		8 v
	inc hl			;75ed	23		#
	ld a,(hl)		;75ee	7e		~
	cp 020h			;75ef	fe 20		.  
	jr c,l7662h		;75f1	38 6f		8 o
	dec hl			;75f3	2b		+
	dec hl			;75f4	2b		+
	ex de,hl		;75f5	eb		.
	ld hl,(M_8ED8)		;75f6	2a d8 8e	* . .
	ld a,h			;75f9	7c		|
	cp 0ffh			;75fa	fe ff		. .
	jp nz,l7603h		;75fc	c2 03 76	. . v
	ld a,l			;75ff	7d		}
	cp 0e7h			;7600	fe e7		. .
	ret nc			;7602	d0		.
l7603h:
	ld c,008h		;7603	0e 08		. .
	add hl,bc		;7605	09		.
	ex de,hl		;7606	eb		.
	ld c,008h		;7607	0e 08		. .
	ldir			;7609	ed b0		. .
	ld a,02eh		;760b	3e 2e		> .
	ld (de),a		;760d	12		.
	inc de			;760e	13		.
	ld c,003h		;760f	0e 03		. .
	ldir			;7611	ed b0		. .
	ld a,(hl)		;7613	7e		~
	and 010h		;7614	e6 10		. .
	jr z,l7622h		;7616	28 0a		( .
	ld c,004h		;7618	0e 04		. .
	ex de,hl		;761a	eb		.
	or a			;761b	b7		.
	sbc hl,bc		;761c	ed 42		. B
	ld (hl),020h		;761e	36 20		6  
	add hl,bc		;7620	09		.
	ex de,hl		;7621	eb		.
l7622h:
	ex de,hl		;7622	eb		.
	ld c,00dh		;7623	0e 0d		. .
	or a			;7625	b7		.
	sbc hl,bc		;7626	ed 42		. B
	ld (hl),a		;7628	77		w
	ex de,hl		;7629	eb		.
	inc hl			;762a	23		#
	inc hl			;762b	23		#
	inc hl			;762c	23		#
	inc hl			;762d	23		#
	inc de			;762e	13		.
	inc hl			;762f	23		#
	inc de			;7630	13		.
	ld c,00ah		;7631	0e 0a		. .
	add hl,bc		;7633	09		.
	ex de,hl		;7634	eb		.
	or a			;7635	b7		.
	sbc hl,bc		;7636	ed 42		. B
	ex de,hl		;7638	eb		.
	ldi			;7639	ed a0		. .
	ldi			;763b	ed a0		. .
	or a			;763d	b7		.
	sbc hl,bc		;763e	ed 42		. B
	ldi			;7640	ed a0		. .
	ldi			;7642	ed a0		. .
	add hl,bc		;7644	09		.
	inc de			;7645	13		.
	inc de			;7646	13		.
	inc de			;7647	13		.
	ld a,(hl)		;7648	7e		~
	ld (de),a		;7649	12		.
	inc hl			;764a	23		#
	dec de			;764b	1b		.
	ld a,(hl)		;764c	7e		~
	ld (de),a		;764d	12		.
	inc hl			;764e	23		#
	dec de			;764f	1b		.
	ld a,(hl)		;7650	7e		~
	ld (de),a		;7651	12		.
	inc hl			;7652	23		#
	dec de			;7653	1b		.
	ld a,(hl)		;7654	7e		~
	ld (de),a		;7655	12		.
	inc hl			;7656	23		#
	ex de,hl		;7657	eb		.
	ld c,012h		;7658	0e 12		. .
	add hl,bc		;765a	09		.
	ld (M_8ED8),hl		;765b	22 d8 8e	" . .
	ex de,hl		;765e	eb		.
	jp R_75CD		;765f	c3 cd 75	. . u
l7662h:
	dec hl			;7662	2b		+
l7663h:
	dec hl			;7663	2b		+
l7664h:
	ld c,020h		;7664	0e 20		.  
	add hl,bc		;7666	09		.
	jp R_75CD		;7667	c3 cd 75	. . u
R_766A:
	ld hl,R_7C08		;766a	21 08 7c	! . |
	ld de,R_7C7B		;766d	11 7b 7c	. { |
M_7670:
	ret			;7670	c9		.
	call R_9A7D		;7671	cd 7d 9a	. } .
	ld a,008h		;7674	3e 08		> .
	call R_9A4A		;7676	cd 4a 9a	. J .
l7679h:
	ld (M_61ED),hl		;7679	22 ed 61	" . a
	ld (M_6246),de		;767c	ed 53 46 62	. S F b
	ld hl,05b0fh		;7680	21 0f 5b	! . [
	ld b,020h		;7683	06 20		.  
	ld a,b			;7685	78		x
	call R_74B6		;7686	cd b6 74	. . t
	jp R_61D8		;7689	c3 d8 61	. . a
	ld a,001h		;768c	3e 01		> .
	jr l7691h		;768e	18 01		. .
R_7690:
	xor a			;7690	af		.
l7691h:
	ld (M_769D),a		;7691	32 9d 76	2 . v
	ld hl,D_7D1A		;7694	21 1a 7d	! . }
	ld de,D_755D		;7697	11 5d 75	. ] u
M_769A:
	ret			;769a	c9		.
	jr l7679h		;769b	18 dc		. .
M_769D:
	nop			;769d	00		.
R_769E:
	xor a			;769e	af		.
	ld (M_7B59),a		;769f	32 59 7b	2 Y {
	jp R_76A5		;76a2	c3 a5 76	. . v
R_76A5:
	push bc			;76a5	c5		.
	push hl			;76a6	e5		.
	call R_9A92		;76a7	cd 92 9a	. . .
	ld hl,D_9A1F		;76aa	21 1f 9a	! . .
	call R_7F9D		;76ad	cd 9d 7f	. . .
	pop hl			;76b0	e1		.
	pop bc			;76b1	c1		.
	ld de,00800h		;76b2	11 00 08	. . .
	add hl,de		;76b5	19		.
	djnz R_76A5		;76b6	10 ed		. .
	ret			;76b8	c9		.
R_76B9:
	call R_7709		;76b9	cd 09 77	. . w
l76bch:
	push bc			;76bc	c5		.
	ld hl,(M_7BA2)		;76bd	2a a2 7b	* . {
	ld a,(M_7B86)		;76c0	3a 86 7b	: . {
	cp 004h			;76c3	fe 04		. .
	jr nc,l76e6h		;76c5	30 1f		0 .
	call R_771C		;76c7	cd 1c 77	. . w
	ld hl,(M_7BA2)		;76ca	2a a2 7b	* . {
	ld a,(M_7B86)		;76cd	3a 86 7b	: . {
	cp 002h			;76d0	fe 02		. .
	jr nc,l76e8h		;76d2	30 14		0 .
	call R_771C		;76d4	cd 1c 77	. . w
	ld hl,(M_7BA2)		;76d7	2a a2 7b	* . {
	ld a,001h		;76da	3e 01		> .
	call R_771C		;76dc	cd 1c 77	. . w
	ld hl,(M_7BA2)		;76df	2a a2 7b	* . {
	ld a,001h		;76e2	3e 01		> .
	jr l76e8h		;76e4	18 02		. .
l76e6h:
	ld a,004h		;76e6	3e 04		> .
l76e8h:
	call R_771C		;76e8	cd 1c 77	. . w
	pop bc			;76eb	c1		.
	djnz l76bch		;76ec	10 ce		. .
R_76EE:
	ld hl,(M_7BA2)		;76ee	2a a2 7b	* . {
	ret			;76f1	c9		.
R_76F2:
	call R_7709		;76f2	cd 09 77	. . w
l76f5h:
	push bc			;76f5	c5		.
	ld hl,(M_7BA2)		;76f6	2a a2 7b	* . {
	ld a,001h		;76f9	3e 01		> .
	ld (M_7738),a		;76fb	32 38 77	2 8 w
	call R_771C		;76fe	cd 1c 77	. . w
	pop bc			;7701	c1		.
	djnz l76f5h		;7702	10 f1		. .
	ret			;7704	c9		.
D_7705:
	nop			;7705	00		.
	nop			;7706	00		.
l7707h:
	nop			;7707	00		.
	nop			;7708	00		.
R_7709:
	ld (M_7BA2),hl		;7709	22 a2 7b	" . {
	ld hl,D_9A03		;770c	21 03 9a	! . .
	ld a,(hl)		;770f	7e		~
	inc hl			;7710	23		#
	or (hl)			;7711	b6		.
	ret nz			;7712	c0		.
	push bc			;7713	c5		.
	ld hl,M_7B9A		;7714	21 9a 7b	! . {
	call R_7813		;7717	cd 13 78	. . x
	pop bc			;771a	c1		.
	ret			;771b	c9		.
R_771C:
	ld (M_774A),a		;771c	32 4a 77	2 J w
	call R_9A59		;771f	cd 59 9a	. Y .
	ld (M_7BA2),hl		;7722	22 a2 7b	" . {
	ld hl,D_9A0A		;7725	21 0a 9a	! . .
	ld de,D_7705		;7728	11 05 77	. . w
	ld bc,00004h		;772b	01 04 00	. . .
	ldir			;772e	ed b0		. .
	ld hl,(D_9A0A)		;7730	2a 0a 9a	* . .
	ld de,(09a0ch)		;7733	ed 5b 0c 9a	. [ . .
	ld c,004h		;7737	0e 04		. .
	add hl,bc		;7739	09		.
	jr nc,l773dh		;773a	30 01		0 .
	inc de			;773c	13		.
l773dh:
	call R_9A4D		;773d	cd 4d 9a	. M .
	ld a,004h		;7740	3e 04		> .
	ld (M_7738),a		;7742	32 38 77	2 8 w
	ld hl,D_9A03		;7745	21 03 9a	! . .
	ld a,(hl)		;7748	7e		~
	add a,004h		;7749	c6 04		. .
	ld (hl),a		;774b	77		w
	ld bc,(M_7B86)		;774c	ed 4b 86 7b	. K . {
	cp c			;7750	b9		.
	ret c			;7751	d8		.
	ld (hl),000h		;7752	36 00		6 .
	inc hl			;7754	23		#
	ld (hl),001h		;7755	36 01		6 .
	ld a,0cbh		;7757	3e cb		> .
	ld (M_7858),a		;7759	32 58 78	2 X x
	ld hl,(M_7B9A)		;775c	2a 9a 7b	* . {
	ld de,(M_7B9C)		;775f	ed 5b 9c 7b	. [ . {
	call R_77DC		;7763	cd dc 77	. . w
	call R_7813		;7766	cd 13 78	. . x
	ld a,(M_9A04)		;7769	3a 04 9a	: . .
	cp 00fh			;776c	fe 0f		. .
	jr z,l7775h		;776e	28 05		( .
	xor a			;7770	af		.
	ld (M_7858),a		;7771	32 58 78	2 X x
	ret			;7774	c9		.
l7775h:
	pop bc			;7775	c1		.
	pop bc			;7776	c1		.
	xor a			;7777	af		.
	ld (M_7858),a		;7778	32 58 78	2 X x
	jp R_76EE		;777b	c3 ee 76	. . v
R_777E:
	ld hl,(M_77D8)		;777e	2a d8 77	* . w
	ld de,(l77dah)		;7781	ed 5b da 77	. [ . w
	call R_77DC		;7785	cd dc 77	. . w
R_7788:
	ld a,(hl)		;7788	7e		~
	inc hl			;7789	23		#
	or (hl)			;778a	b6		.
	inc hl			;778b	23		#
	or (hl)			;778c	b6		.
	inc hl			;778d	23		#
	or (hl)			;778e	b6		.
	inc hl			;778f	23		#
	jr z,l77c7h		;7790	28 35		( 5
	ex de,hl		;7792	eb		.
	ld hl,M_77D8		;7793	21 d8 77	! . w
	inc (hl)		;7796	34		4
	jr nz,l77a3h		;7797	20 0a		  .
	inc hl			;7799	23		#
	inc (hl)		;779a	34		4
	jr nz,l77a3h		;779b	20 06		  .
	inc hl			;779d	23		#
	inc (hl)		;779e	34		4
	jr nz,l77a3h		;779f	20 02		  .
	inc hl			;77a1	23		#
	inc (hl)		;77a2	34		4
l77a3h:
	ex de,hl		;77a3	eb		.
	ld a,h			;77a4	7c		|
	cp 0a0h			;77a5	fe a0		. .
	jr c,R_7788		;77a7	38 df		8 .
	ld hl,(D_9A0A)		;77a9	2a 0a 9a	* . .
	ld de,(09a0ch)		;77ac	ed 5b 0c 9a	. [ . .
	ld bc,00001h		;77b0	01 01 00	. . .
	add hl,bc		;77b3	09		.
	jr nc,l77b7h		;77b4	30 01		0 .
	inc de			;77b6	13		.
l77b7h:
	call R_9A4D		;77b7	cd 4d 9a	. M .
	ld hl,D_9E00		;77ba	21 00 9e	! . .
	ld a,001h		;77bd	3e 01		> .
	call R_9A59		;77bf	cd 59 9a	. Y .
	ld hl,D_9E00		;77c2	21 00 9e	! . .
	jr R_7788		;77c5	18 c1		. .
l77c7h:
	exx			;77c7	d9		.
	ld hl,(M_77D8)		;77c8	2a d8 77	* . w
	ld de,(l77dah)		;77cb	ed 5b da 77	. [ . w
	push hl			;77cf	e5		.
	ld hl,M_77D8		;77d0	21 d8 77	! . w
	call R_7FA5		;77d3	cd a5 7f	. . .
	pop hl			;77d6	e1		.
	ret			;77d7	c9		.
M_77D8:
	nop			;77d8	00		.
	nop			;77d9	00		.
l77dah:
	nop			;77da	00		.
	nop			;77db	00		.
R_77DC:
	ld bc,00080h		;77dc	01 80 00	. . .
	call R_7B23		;77df	cd 23 7b	. # {
	add hl,hl		;77e2	29		)
	add hl,hl		;77e3	29		)
	push hl			;77e4	e5		.
	ex de,hl		;77e5	eb		.
	ld de,00000h		;77e6	11 00 00	. . .
	ld (M_7807),hl		;77e9	22 07 78	" . x
	ld bc,(M_7B94)		;77ec	ed 4b 94 7b	. K . {
	call R_7FAD		;77f0	cd ad 7f	. . .
	call R_7877		;77f3	cd 77 78	. w x
	call R_9A4D		;77f6	cd 4d 9a	. M .
	ld hl,D_9E00		;77f9	21 00 9e	! . .
	ld a,001h		;77fc	3e 01		> .
	call R_9A59		;77fe	cd 59 9a	. Y .
	pop bc			;7801	c1		.
	ld hl,D_9E00		;7802	21 00 9e	! . .
	add hl,bc		;7805	09		.
	ret			;7806	c9		.
M_7807:
	nop			;7807	00		.
	nop			;7808	00		.
l7809h:
	ld (M_8F1A),hl		;7809	22 1a 8f	" . .
	call R_848A		;780c	cd 8a 84	. . .
	ld de,D_720A		;780f	11 0a 72	. . r
	push de			;7812	d5		.
R_7813:
	ld e,(hl)		;7813	5e		^
	inc hl			;7814	23		#
	ld d,(hl)		;7815	56		V
	inc hl			;7816	23		#
	ld a,(hl)		;7817	7e		~
	inc hl			;7818	23		#
	ld h,(hl)		;7819	66		f
	ld l,a			;781a	6f		o
	or h			;781b	b4		.
	or e			;781c	b3		.
	or d			;781d	b2		.
	jr z,l785bh		;781e	28 3b		( ;
	ld a,h			;7820	7c		|
	cp 00fh			;7821	fe 0f		. .
	jr z,l7855h		;7823	28 30		( 0
	ex de,hl		;7825	eb		.
l7826h:
	ld (M_7B9A),hl		;7826	22 9a 7b	" . {
	ld (M_7B9C),de		;7829	ed 53 9c 7b	. S . {
	ld bc,00002h		;782d	01 02 00	. . .
	or a			;7830	b7		.
	sbc hl,bc		;7831	ed 42		. B
	jr nc,l7836h		;7833	30 01		0 .
	dec de			;7835	1b		.
l7836h:
	ld bc,(M_7B86)		;7836	ed 4b 86 7b	. K . {
	call R_7FF8		;783a	cd f8 7f	. . .
	ld bc,(M_7B96)		;783d	ed 4b 96 7b	. K . {
	call R_7FAD		;7841	cd ad 7f	. . .
	ex de,hl		;7844	eb		.
	ld bc,(l7b98h)		;7845	ed 4b 98 7b	. K . {
	add hl,bc		;7849	09		.
	ex de,hl		;784a	eb		.
	xor a			;784b	af		.
	ld (M_9A04),a		;784c	32 04 9a	2 . .
	call R_7877		;784f	cd 77 78	. w x
	jp R_9A4D		;7852	c3 4d 9a	. M .
l7855h:
	ld (M_9A04),a		;7855	32 04 9a	2 . .
M_7858:
	nop			;7858	00		.
	pop de			;7859	d1		.
	ret			;785a	c9		.
l785bh:
	ld hl,(M_7B88)		;785b	2a 88 7b	* . {
	ld de,(M_7B8A)		;785e	ed 5b 8a 7b	. [ . {
	jr l7826h		;7862	18 c2		. .
R_7864:
	ld bc,(M_7B90)		;7864	ed 4b 90 7b	. K . {
	ld (M_7FC8),bc		;7868	ed 43 c8 7f	. C . .
	ld bc,(l7b92h)		;786c	ed 4b 92 7b	. K . {
	ld (l7fcah),bc		;7870	ed 43 ca 7f	. C . .
	jp R_7FB1		;7874	c3 b1 7f	. . .
R_7877:
	call R_7864		;7877	cd 64 78	. d x
	ld a,l			;787a	7d		}
	ld (M_788D),a		;787b	32 8d 78	2 . x
	ld a,d			;787e	7a		z
	ld (M_788E),a		;787f	32 8e 78	2 . x
	push de			;7882	d5		.
	ld d,e			;7883	53		S
	ld e,h			;7884	5c		\
	ld (l788bh),de		;7885	ed 53 8b 78	. S . x
	pop de			;7889	d1		.
	ret			;788a	c9		.
l788bh:
	nop			;788b	00		.
	nop			;788c	00		.
M_788D:
	nop			;788d	00		.
M_788E:
	nop			;788e	00		.
	xor a			;788f	af		.
	ld (M_7B59),a		;7890	32 59 7b	2 Y {
	ld de,D_9A5C		;7893	11 5c 9a	. \ .
	ld (07720h),de		;7896	ed 53 20 77	. S   w
	call R_76B9		;789a	cd b9 76	. . v
	ld de,R_9A59		;789d	11 59 9a	. Y .
	ld (07720h),de		;78a0	ed 53 20 77	. S   w
	ret			;78a4	c9		.
	ld hl,D_7B5A		;78a5	21 5a 7b	! Z {
	ld b,00bh		;78a8	06 0b		. .
l78aah:
	ld a,(hl)		;78aa	7e		~
	cp 024h			;78ab	fe 24		. $
	jr z,l78cah		;78ad	28 1b		( .
	cp 030h			;78af	fe 30		. 0
	jr c,l78bbh		;78b1	38 08		8 .
	cp 03ah			;78b3	fe 3a		. :
	jr c,l78bfh		;78b5	38 08		8 .
	cp 041h			;78b7	fe 41		. A
	jr nc,l78bfh		;78b9	30 04		0 .
l78bbh:
	ld (hl),05fh		;78bb	36 5f		6 _
	jr l78cah		;78bd	18 0b		. .
l78bfh:
	cp 061h			;78bf	fe 61		. a
	jr c,l78cah		;78c1	38 07		8 .
	cp 07bh			;78c3	fe 7b		. {
	jr nc,l78bbh		;78c5	30 f4		0 .
	sub 020h		;78c7	d6 20		.  
	ld (hl),a		;78c9	77		w
l78cah:
	inc hl			;78ca	23		#
	djnz l78aah		;78cb	10 dd		. .
	ld hl,D_7B61		;78cd	21 61 7b	! a {
	ld b,006h		;78d0	06 06		. .
	call R_79D0		;78d2	cd d0 79	. . y
	ld hl,D_7B64		;78d5	21 64 7b	! d {
	ld b,003h		;78d8	06 03		. .
	call R_79D0		;78da	cd d0 79	. . y
	ld hl,(M_7B51)		;78dd	2a 51 7b	* Q {
	ld de,(M_7B53)		;78e0	ed 5b 53 7b	. [ S {
	ld (M_7B9A),hl		;78e4	22 9a 7b	" . {
	ld (M_7B9C),de		;78e7	ed 53 9c 7b	. S . {
	call R_9A9B		;78eb	cd 9b 9a	. . .
R_78EE:
	ld hl,VTII_INSTANCE	;78ee	21 00 a0	! . .
	ld b,001h		;78f1	06 01		. .
	call R_76F2		;78f3	cd f2 76	. . v
	ld hl,VTII_INSTANCE	;78f6	21 00 a0	! . .
	ld de,00020h		;78f9	11 20 00	.   .
	ld b,010h		;78fc	06 10		. .
	xor a			;78fe	af		.
l78ffh:
	cp (hl)			;78ff	be		.
	jr z,l796ch		;7900	28 6a		( j
	add hl,de		;7902	19		.
	djnz l78ffh		;7903	10 fa		. .
	ld a,(M_9A04)		;7905	3a 04 9a	: . .
	cp 00fh			;7908	fe 0f		. .
	jr nz,R_78EE		;790a	20 e2		  .
	ld hl,D_7B8C		;790c	21 8c 7b	! . {
	ld de,M_77D8		;790f	11 d8 77	. . w
	ld bc,00004h		;7912	01 04 00	. . .
	ldir			;7915	ed b0		. .
	call R_777E		;7917	cd 7e 77	. ~ w
	ld (M_7B4D),hl		;791a	22 4d 7b	" M {
	ld (l7b4fh),de		;791d	ed 53 4f 7b	. S O {
	exx			;7921	d9		.
	ld a,0ffh		;7922	3e ff		> .
	dec hl			;7924	2b		+
	ld (hl),00fh		;7925	36 0f		6 .
	dec hl			;7927	2b		+
	ld (hl),a		;7928	77		w
	dec hl			;7929	2b		+
	ld (hl),a		;792a	77		w
	dec hl			;792b	2b		+
	ld (hl),a		;792c	77		w
	ld hl,D_9E00		;792d	21 00 9e	! . .
	ld a,001h		;7930	3e 01		> .
	call D_9A5C		;7932	cd 5c 9a	. \ .
	ld hl,(M_7B9A)		;7935	2a 9a 7b	* . {
	ld de,(M_7B9C)		;7938	ed 5b 9c 7b	. [ . {
	call R_77DC		;793c	cd dc 77	. . w
	ex de,hl		;793f	eb		.
	ld hl,M_7B4D		;7940	21 4d 7b	! M {
	ld bc,00004h		;7943	01 04 00	. . .
	ldir			;7946	ed b0		. .
	ld hl,D_9E00		;7948	21 00 9e	! . .
	ld a,001h		;794b	3e 01		> .
	call D_9A5C		;794d	cd 5c 9a	. \ .
	ld hl,M_7B4D		;7950	21 4d 7b	! M {
	call R_7813		;7953	cd 13 78	. . x
	ld hl,D_9E00		;7956	21 00 9e	! . .
	ld b,000h		;7959	06 00		. .
	call R_74B5		;795b	cd b5 74	. . t
	call R_74B5		;795e	cd b5 74	. . t
	ld hl,D_9E00		;7961	21 00 9e	! . .
	ld a,001h		;7964	3e 01		> .
	call D_9A5C		;7966	cd 5c 9a	. \ .
	jp R_78EE		;7969	c3 ee 78	. . x
l796ch:
	ex de,hl		;796c	eb		.
	ld hl,(M_7B49)		;796d	2a 49 7b	* I {
	ld (M_7B74),hl		;7970	22 74 7b	" t {
	ld hl,(M_7B4B)		;7973	2a 4b 7b	* K {
	ld (M_7B6E),hl		;7976	22 6e 7b	" n {
	ld hl,D_7B5A		;7979	21 5a 7b	! Z {
	ld bc,00021h		;797c	01 21 00	. ! .
	ldir			;797f	ed b0		. .
	ld a,d			;7981	7a		z
	ld (M_7B40),a		;7982	32 40 7b	2 @ {
	ld hl,(D_7705)		;7985	2a 05 77	* . w
	ld de,(l7707h)		;7988	ed 5b 07 77	. [ . w
	call R_9A50		;798c	cd 50 9a	. P .
	ld hl,VTII_INSTANCE	;798f	21 00 a0	! . .
	ld a,001h		;7992	3e 01		> .
	call D_9A5C		;7994	cd 5c 9a	. \ .
	ld a,(M_9A04)		;7997	3a 04 9a	: . .
	cp 00fh			;799a	fe 0f		. .
	ret z			;799c	c8		.
	ld hl,(D_9A0A)		;799d	2a 0a 9a	* . .
	ld de,(09a0ch)		;79a0	ed 5b 0c 9a	. [ . .
	call R_9A50		;79a4	cd 50 9a	. P .
	ld hl,VTII_INSTANCE	;79a7	21 00 a0	! . .
	ld b,001h		;79aa	06 01		. .
	call R_76F2		;79ac	cd f2 76	. . v
	ld a,(M_7B40)		;79af	3a 40 7b	: @ {
	cp 0a2h			;79b2	fe a2		. .
	ret c			;79b4	d8		.
	ld hl,(D_7705)		;79b5	2a 05 77	* . w
	ld de,(l7707h)		;79b8	ed 5b 07 77	. [ . w
	call R_9A50		;79bc	cd 50 9a	. P .
	ld hl,VTII_INSTANCE	;79bf	21 00 a0	! . .
	ld b,000h		;79c2	06 00		. .
	call R_74B5		;79c4	cd b5 74	. . t
	ld hl,VTII_INSTANCE	;79c7	21 00 a0	! . .
	ld a,001h		;79ca	3e 01		> .
	call D_9A5C		;79cc	cd 5c 9a	. \ .
	ret			;79cf	c9		.
R_79D0:
	ld a,(hl)		;79d0	7e		~
	cp 05fh			;79d1	fe 5f		. _
	ret nz			;79d3	c0		.
	ld (hl),020h		;79d4	36 20		6  
	dec hl			;79d6	2b		+
	djnz R_79D0		;79d7	10 f7		. .
	ret			;79d9	c9		.
	ld (M_7B47),hl		;79da	22 47 7b	" G {
	call R_84B2		;79dd	cd b2 84	. . .
	ld hl,0c000h		;79e0	21 00 c0	! . .
	ld (M_7B45),hl		;79e3	22 45 7b	" E {
	ld hl,D_7B8C		;79e6	21 8c 7b	! . {
	ld de,M_77D8		;79e9	11 d8 77	. . w
	ld bc,00004h		;79ec	01 04 00	. . .
	ldir			;79ef	ed b0		. .
	call R_777E		;79f1	cd 7e 77	. ~ w
	ld (M_7B49),hl		;79f4	22 49 7b	" I {
	ld (M_7B4B),de		;79f7	ed 53 4b 7b	. S K {
	ld (D_7B8C),hl		;79fb	22 8c 7b	" . {
	ld (l7b8eh),de		;79fe	ed 53 8e 7b	. S . {
	call R_7A8E		;7a02	cd 8e 7a	. . z
l7a05h:
	ld hl,(M_7B47)		;7a05	2a 47 7b	* G {
	dec hl			;7a08	2b		+
	ld (M_7B47),hl		;7a09	22 47 7b	" G {
	ld a,h			;7a0c	7c		|
	or l			;7a0d	b5		.
	jr z,l7a2ah		;7a0e	28 1a		( .
	exx			;7a10	d9		.
	ld a,h			;7a11	7c		|
	cp 0a0h			;7a12	fe a0		. .
	jr c,l7a1bh		;7a14	38 05		8 .
	call R_777E		;7a16	cd 7e 77	. ~ w
	jr l7a1eh		;7a19	18 03		. .
l7a1bh:
	call R_7788		;7a1b	cd 88 77	. . w
l7a1eh:
	ld (D_7B8C),hl		;7a1e	22 8c 7b	" . {
	ld (l7b8eh),de		;7a21	ed 53 8e 7b	. S . {
	call R_7A8E		;7a25	cd 8e 7a	. . z
	jr l7a05h		;7a28	18 db		. .
l7a2ah:
	ld de,00fffh		;7a2a	11 ff 0f	. . .
	ld h,e			;7a2d	63		c
	ld l,e			;7a2e	6b		k
	call R_7A8E		;7a2f	cd 8e 7a	. . z
	ld hl,0c000h		;7a32	21 00 c0	! . .
l7a35h:
	call R_7A9F		;7a35	cd 9f 7a	. . z
	push hl			;7a38	e5		.
	ld h,b			;7a39	60		`
	ld l,c			;7a3a	69		i
	call R_77DC		;7a3b	cd dc 77	. . w
	ex de,hl		;7a3e	eb		.
	pop hl			;7a3f	e1		.
l7a40h:
	ld (M_7B41),hl		;7a40	22 41 7b	" A {
	ld bc,00004h		;7a43	01 04 00	. . .
	ldir			;7a46	ed b0		. .
	dec hl			;7a48	2b		+
	ld a,(hl)		;7a49	7e		~
	cp 00fh			;7a4a	fe 0f		. .
	jr z,l7a85h		;7a4c	28 37		( 7
	inc hl			;7a4e	23		#
	ld hl,(M_7B41)		;7a4f	2a 41 7b	* A {
	call R_7A9F		;7a52	cd 9f 7a	. . z
	ld (M_7B43),hl		;7a55	22 43 7b	" C {
	ld h,b			;7a58	60		`
	ld l,c			;7a59	69		i
	ld bc,00080h		;7a5a	01 80 00	. . .
	call R_7B23		;7a5d	cd 23 7b	. # {
	ld b,h			;7a60	44		D
	ld c,l			;7a61	4d		M
	ld hl,(M_7807)		;7a62	2a 07 78	* . x
	or a			;7a65	b7		.
	sbc hl,de		;7a66	ed 52		. R
	jr z,l7a77h		;7a68	28 0d		( .
	ld hl,D_9E00		;7a6a	21 00 9e	! . .
	ld a,001h		;7a6d	3e 01		> .
	call D_9A5C		;7a6f	cd 5c 9a	. \ .
	ld hl,(M_7B41)		;7a72	2a 41 7b	* A {
	jr l7a35h		;7a75	18 be		. .
l7a77h:
	ld h,b			;7a77	60		`
	ld l,c			;7a78	69		i
	add hl,hl		;7a79	29		)
	add hl,hl		;7a7a	29		)
	ld bc,D_9E00		;7a7b	01 00 9e	. . .
	add hl,bc		;7a7e	09		.
	ex de,hl		;7a7f	eb		.
	ld hl,(M_7B43)		;7a80	2a 43 7b	* C {
	jr l7a40h		;7a83	18 bb		. .
l7a85h:
	ld hl,D_9E00		;7a85	21 00 9e	! . .
	ld a,001h		;7a88	3e 01		> .
	call D_9A5C		;7a8a	cd 5c 9a	. \ .
	ret			;7a8d	c9		.
R_7A8E:
	ld a,h			;7a8e	7c		|
	ld c,l			;7a8f	4d		M
	ld hl,(M_7B45)		;7a90	2a 45 7b	* E {
	ld (hl),c		;7a93	71		q
	inc hl			;7a94	23		#
	ld (hl),a		;7a95	77		w
	inc hl			;7a96	23		#
	ld (hl),e		;7a97	73		s
	inc hl			;7a98	23		#
	ld (hl),d		;7a99	72		r
	inc hl			;7a9a	23		#
	ld (M_7B45),hl		;7a9b	22 45 7b	" E {
	ret			;7a9e	c9		.
R_7A9F:
	ld c,(hl)		;7a9f	4e		N
	inc hl			;7aa0	23		#
	ld b,(hl)		;7aa1	46		F
	inc hl			;7aa2	23		#
	ld e,(hl)		;7aa3	5e		^
	inc hl			;7aa4	23		#
	ld d,(hl)		;7aa5	56		V
	inc hl			;7aa6	23		#
	ret			;7aa7	c9		.
	ld hl,M_7B51		;7aa8	21 51 7b	! Q {
	ld de,M_7B9A		;7aab	11 9a 7b	. . {
	ld bc,00004h		;7aae	01 04 00	. . .
	ldir			;7ab1	ed b0		. .
	call R_9A9B		;7ab3	cd 9b 9a	. . .
l7ab6h:
	ld hl,VTII_INSTANCE	;7ab6	21 00 a0	! . .
	ld b,001h		;7ab9	06 01		. .
	call R_76F2		;7abb	cd f2 76	. . v
	ld hl,VTII_INSTANCE	;7abe	21 00 a0	! . .
l7ac1h:
	ld a,(hl)		;7ac1	7e		~
	or a			;7ac2	b7		.
	ret z			;7ac3	c8		.
	ld de,0001ah		;7ac4	11 1a 00	. . .
	add hl,de		;7ac7	19		.
	ld e,(hl)		;7ac8	5e		^
	inc hl			;7ac9	23		#
	ld d,(hl)		;7aca	56		V
	ex de,hl		;7acb	eb		.
	ld bc,(l7b3ch)		;7acc	ed 4b 3c 7b	. K < {
	or a			;7ad0	b7		.
	sbc hl,bc		;7ad1	ed 42		. B
	jr z,l7ae6h		;7ad3	28 11		( .
	ld hl,00005h		;7ad5	21 05 00	! . .
l7ad8h:
	add hl,de		;7ad8	19		.
	ld a,h			;7ad9	7c		|
	cp 0a2h			;7ada	fe a2		. .
	jr nc,l7ab6h		;7adc	30 d8		0 .
	jr l7ac1h		;7ade	18 e1		. .
l7ae0h:
	ld de,00020h		;7ae0	11 20 00	.   .
	pop hl			;7ae3	e1		.
	jr l7ad8h		;7ae4	18 f2		. .
l7ae6h:
	ld hl,0fff9h		;7ae6	21 f9 ff	! . .
	add hl,de		;7ae9	19		.
	ld e,(hl)		;7aea	5e		^
	inc hl			;7aeb	23		#
	ld d,(hl)		;7aec	56		V
	ex de,hl		;7aed	eb		.
	ld bc,(l7b3ch+2)	;7aee	ed 4b 3e 7b	. K > {
	or a			;7af2	b7		.
	sbc hl,bc		;7af3	ed 42		. B
	ld hl,0000bh		;7af5	21 0b 00	! . .
	jr nz,l7ad8h		;7af8	20 de		  .
	ld hl,0ffebh		;7afa	21 eb ff	! . .
	add hl,de		;7afd	19		.
	push hl			;7afe	e5		.
	ld de,D_7B5A		;7aff	11 5a 7b	. Z {
	ld b,008h		;7b02	06 08		. .
l7b04h:
	ld a,(de)		;7b04	1a		.
	cp (hl)			;7b05	be		.
	jr nz,l7ae0h		;7b06	20 d8		  .
	inc hl			;7b08	23		#
	inc de			;7b09	13		.
	djnz l7b04h		;7b0a	10 f8		. .
	pop de			;7b0c	d1		.
	ld a,001h		;7b0d	3e 01		> .
	or a			;7b0f	b7		.
	ret			;7b10	c9		.
	ld hl,(D_7705)		;7b11	2a 05 77	* . w
	ld de,(l7707h)		;7b14	ed 5b 07 77	. [ . w
	call R_9A50		;7b18	cd 50 9a	. P .
	ld hl,VTII_INSTANCE	;7b1b	21 00 a0	! . .
	ld a,001h		;7b1e	3e 01		> .
	jp D_9A5C		;7b20	c3 5c 9a	. \ .
R_7B23:
	ld a,l			;7b23	7d		}
	ex af,af'		;7b24	08		.
	ld a,l			;7b25	7d		}
	ld l,h			;7b26	6c		l
	ld h,e			;7b27	63		c
	rla			;7b28	17		.
	rl l			;7b29	cb 15		. .
	rl h			;7b2b	cb 14		. .
	ex de,hl		;7b2d	eb		.
	ex af,af'		;7b2e	08		.
	and 07fh		;7b2f	e6 7f		. .
	ld h,000h		;7b31	26 00		& .
	ld l,a			;7b33	6f		o
	ld (M_8043),hl		;7b34	22 43 80	" C .
	ld (M_803D),de		;7b37	ed 53 3d 80	. S = .
	ret			;7b3b	c9		.
l7b3ch:
	ld bc,00001h		;7b3c	01 01 00	. . .
	nop			;7b3f	00		.
M_7B40:
	nop			;7b40	00		.
M_7B41:
	nop			;7b41	00		.
	nop			;7b42	00		.
M_7B43:
	nop			;7b43	00		.
	nop			;7b44	00		.
M_7B45:
	nop			;7b45	00		.
	nop			;7b46	00		.
M_7B47:
	nop			;7b47	00		.
	nop			;7b48	00		.
M_7B49:
	nop			;7b49	00		.
	nop			;7b4a	00		.
M_7B4B:
	nop			;7b4b	00		.
	nop			;7b4c	00		.
M_7B4D:
	nop			;7b4d	00		.
	nop			;7b4e	00		.
l7b4fh:
	nop			;7b4f	00		.
	nop			;7b50	00		.
M_7B51:
	nop			;7b51	00		.
	nop			;7b52	00		.
M_7B53:
	nop			;7b53	00		.
	nop			;7b54	00		.
	nop			;7b55	00		.
	nop			;7b56	00		.
	nop			;7b57	00		.
	nop			;7b58	00		.
M_7B59:
	nop			;7b59	00		.
D_7B5A:
	nop			;7b5a	00		.
	nop			;7b5b	00		.
	nop			;7b5c	00		.
	nop			;7b5d	00		.
	nop			;7b5e	00		.
	nop			;7b5f	00		.
	nop			;7b60	00		.
D_7B61:
	nop			;7b61	00		.
	nop			;7b62	00		.
	nop			;7b63	00		.
D_7B64:
	nop			;7b64	00		.
	nop			;7b65	00		.
	nop			;7b66	00		.
	nop			;7b67	00		.
	nop			;7b68	00		.
	ld (hl),b		;7b69	70		p
	djnz M_7BA5		;7b6a	10 39		. 9
	djnz M_7BA7		;7b6c	10 39		. 9
M_7B6E:
	nop			;7b6e	00		.
	nop			;7b6f	00		.
	nop			;7b70	00		.
	ld (hl),b		;7b71	70		p
	djnz R_7BAD		;7b72	10 39		. 9
M_7B74:
	nop			;7b74	00		.
	nop			;7b75	00		.
	nop			;7b76	00		.
	nop			;7b77	00		.
	nop			;7b78	00		.
	nop			;7b79	00		.
	nop			;7b7a	00		.
M_7B7B:
	nop			;7b7b	00		.
	nop			;7b7c	00		.
M_7B7D:
	nop			;7b7d	00		.
	nop			;7b7e	00		.
l7b7fh:
	nop			;7b7f	00		.
	nop			;7b80	00		.
M_7B81:
	nop			;7b81	00		.
M_7B82:
	nop			;7b82	00		.
	nop			;7b83	00		.
M_7B84:
	nop			;7b84	00		.
	nop			;7b85	00		.
M_7B86:
	nop			;7b86	00		.
	nop			;7b87	00		.
M_7B88:
	nop			;7b88	00		.
	nop			;7b89	00		.
M_7B8A:
	nop			;7b8a	00		.
	nop			;7b8b	00		.
D_7B8C:
	ld (bc),a		;7b8c	02		.
	nop			;7b8d	00		.
l7b8eh:
	nop			;7b8e	00		.
	nop			;7b8f	00		.
M_7B90:
	nop			;7b90	00		.
	nop			;7b91	00		.
l7b92h:
	nop			;7b92	00		.
	nop			;7b93	00		.
M_7B94:
	nop			;7b94	00		.
	nop			;7b95	00		.
M_7B96:
	nop			;7b96	00		.
	nop			;7b97	00		.
l7b98h:
	nop			;7b98	00		.
	nop			;7b99	00		.
M_7B9A:
	nop			;7b9a	00		.
	nop			;7b9b	00		.
M_7B9C:
	nop			;7b9c	00		.
	nop			;7b9d	00		.
	nop			;7b9e	00		.
	nop			;7b9f	00		.
	nop			;7ba0	00		.
	nop			;7ba1	00		.
M_7BA2:
	nop			;7ba2	00		.
	nop			;7ba3	00		.
M_7BA4:
	nop			;7ba4	00		.
M_7BA5:
	nop			;7ba5	00		.
	nop			;7ba6	00		.
M_7BA7:
	nop			;7ba7	00		.
	nop			;7ba8	00		.
l7ba9h:
	ld a,001h		;7ba9	3e 01		> .
	or a			;7bab	b7		.
	ret			;7bac	c9		.
R_7BAD:
	ld ix,VTII_INSTANCE	;7bad	dd 21 00 a0	. ! . .
	ld a,(ix+000h)		;7bb1	dd 7e 00	. ~ .
	cp 03eh			;7bb4	fe 3e		. >
	jr nz,l7bdfh		;7bb6	20 27		  '
	ld a,(ix+002h)		;7bb8	dd 7e 02	. ~ .
	cp 032h			;7bbb	fe 32		. 2
	jr nz,l7bdfh		;7bbd	20 20		   
	ld a,(ix+005h)		;7bbf	dd 7e 05	. ~ .
	cp 032h			;7bc2	fe 32		. 2
	jr nz,l7bdfh		;7bc4	20 19		  .
	ld a,(ix+008h)		;7bc6	dd 7e 08	. ~ .
	cp 032h			;7bc9	fe 32		. 2
	jr nz,l7bdfh		;7bcb	20 12		  .
	ld a,(ix+00bh)		;7bcd	dd 7e 0b	. ~ .
	cp 001h			;7bd0	fe 01		. .
	jr nz,l7bdfh		;7bd2	20 0b		  .
	ld hl,D_A544		;7bd4	21 44 a5	! D .
	ld de,VTII_INSTANCE	;7bd7	11 00 a0	. . .
	ld bc,03abch		;7bda	01 bc 3a	. . :
	ldir			;7bdd	ed b0		. .
l7bdfh:
	ld de,(VTII_INSTANCE)	;7bdf	ed 5b 00 a0	. [ . .
	ld a,d			;7be3	7a		z
	cp 040h			;7be4	fe 40		. @
	jr nc,l7ba9h		;7be6	30 c1		0 .
	or e			;7be8	b3		.
	jr z,l7ba9h		;7be9	28 be		( .
	ld hl,D_9FFF		;7beb	21 ff 9f	! . .
	add hl,de		;7bee	19		.
	ld a,(hl)		;7bef	7e		~
	cp 009h			;7bf0	fe 09		. .
	jr nc,l7ba9h		;7bf2	30 b5		0 .
	dec hl			;7bf4	2b		+
	ld b,006h		;7bf5	06 06		. .
l7bf7h:
	ld a,(hl)		;7bf7	7e		~
	or a			;7bf8	b7		.
	jr nz,l7ba9h		;7bf9	20 ae		  .
	dec hl			;7bfb	2b		+
	djnz l7bf7h		;7bfc	10 f9		. .
	ld a,(hl)		;7bfe	7e		~
	or a			;7bff	b7		.
	jr z,l7ba9h		;7c00	28 a7		( .
	cp 021h			;7c02	fe 21		. !
	jr nc,l7ba9h		;7c04	30 a3		0 .
	xor a			;7c06	af		.
	ret			;7c07	c9		.
R_7C08:
	xor a			;7c08	af		.
	ld (M_9A02),a		;7c09	32 02 9a	2 . .
	ld (M_9A08),a		;7c0c	32 08 9a	2 . .
	ld hl,R_76A5		;7c0f	21 a5 76	! . v
	ld (M_76A3),hl		;7c12	22 a3 76	" . v
	call R_80AF		;7c15	cd af 80	. . .
	ld hl,05848h		;7c18	21 48 58	! H X
	ld de,05868h		;7c1b	11 68 58	. h X
	call R_80E2		;7c1e	cd e2 80	. . .
	ld hl,D_8E1D		;7c21	21 1d 8e	! . .
	ld de,D_8E60		;7c24	11 60 8e	. ` .
	ld bc,00003h		;7c27	01 03 00	. . .
	ldir			;7c2a	ed b0		. .
	call R_9A32		;7c2c	cd 32 9a	. 2 .
	ld a,(M_934E)		;7c2f	3a 4e 93	: N .
	or a			;7c32	b7		.
	jr z,l7c3ah		;7c33	28 05		( .
	ld a,004h		;7c35	3e 04		> .
	call R_966A		;7c37	cd 6a 96	. j .
l7c3ah:
	call R_90DE		;7c3a	cd de 90	. . .
	jp nz,0934fh		;7c3d	c2 4f 93	. O .
	xor a			;7c40	af		.
	ld (M_8ED6),a		;7c41	32 d6 8e	2 . .
	call R_9A32		;7c44	cd 32 9a	. 2 .
	call R_8053		;7c47	cd 53 80	. S .
	ld hl,00010h		;7c4a	21 10 00	! . .
	call R_9A98		;7c4d	cd 98 9a	. . .
	ld hl,VTII_INSTANCE	;7c50	21 00 a0	! . .
	call R_9A92		;7c53	cd 92 9a	. . .
	ld hl,D_A028		;7c56	21 28 a0	! ( .
	ld de,D_8E64		;7c59	11 64 8e	. d .
	ld b,00bh		;7c5c	06 0b		. .
	call R_850F		;7c5e	cd 0f 85	. . .
	ld hl,D_A0A2		;7c61	21 a2 a0	! . .
	call R_9A9E		;7c64	cd 9e 9a	. . .
	ld de,(0a0a8h)		;7c67	ed 5b a8 a0	. [ . .
	ld hl,(M_A0A6)		;7c6b	2a a6 a0	* . .
	ld bc,00800h		;7c6e	01 00 08	. . .
	ld (M_803F),hl		;7c71	22 3f 80	" ? .
	ld (l8041h),de		;7c74	ed 53 41 80	. S A .
	jp R_7FF0		;7c78	c3 f0 7f	. . .
R_7C7B:
	ld a,008h		;7c7b	3e 08		> .
	ld (M_7CCA),a		;7c7d	32 ca 7c	2 . |
	ld a,004h		;7c80	3e 04		> .
	ld (M_7CB3),a		;7c82	32 b3 7c	2 . |
	ld de,VTII_INSTANCE	;7c85	11 00 a0	. . .
	ld (M_7CDA),de		;7c88	ed 53 da 7c	. S . |
	ld (07c98h),de		;7c8c	ed 53 98 7c	. S . |
	xor a			;7c90	af		.
	ld (M_8F0F),a		;7c91	32 0f 8f	2 . .
l7c94h:
	ld a,(M_803D)		;7c94	3a 3d 80	: = .
	ld hl,VTII_INSTANCE	;7c97	21 00 a0	! . .
	cp 005h			;7c9a	fe 05		. .
	jr nc,l7cb2h		;7c9c	30 14		0 .
	ld de,(M_8F0F)		;7c9e	ed 5b 0f 8f	. [ . .
	dec e			;7ca2	1d		.
	inc e			;7ca3	1c		.
	jr z,l7cd2h		;7ca4	28 2c		( ,
	cp 004h			;7ca6	fe 04		. .
	jr nc,l7cb2h		;7ca8	30 08		0 .
	ld e,000h		;7caa	1e 00		. .
	ld (M_8F0F),de		;7cac	ed 53 0f 8f	. S . .
	jr l7cd2h		;7cb0	18 20		.  
l7cb2h:
	sub 004h		;7cb2	d6 04		. .
	ld (M_803D),a		;7cb4	32 3d 80	2 = .
	ld a,(M_7CB3)		;7cb7	3a b3 7c	: . |
	ld (M_7CD1),a		;7cba	32 d1 7c	2 . |
	ld a,003h		;7cbd	3e 03		> .
	ld (M_7CB3),a		;7cbf	32 b3 7c	2 . |
	ld (M_8F0F),a		;7cc2	32 0f 8f	2 . .
	ld de,D_7C99		;7cc5	11 99 7c	. . |
	ld a,(de)		;7cc8	1a		.
	add a,008h		;7cc9	c6 08		. .
	ld (de),a		;7ccb	12		.
	xor a			;7ccc	af		.
D_7CCD:
	ld (M_7CCA),a		;7ccd	32 ca 7c	2 . |
	ld a,004h		;7cd0	3e 04		> .
l7cd2h:
	ld b,a			;7cd2	47		G
	call R_769E		;7cd3	cd 9e 76	. . v
	call R_848A		;7cd6	cd 8a 84	. . .
	ld hl,VTII_INSTANCE	;7cd9	21 00 a0	! . .
	call R_74D8		;7cdc	cd d8 74	. . t
	or a			;7cdf	b7		.
	ret nz			;7ce0	c0		.
	ld de,0e800h		;7ce1	11 00 e8	. . .
	add hl,de		;7ce4	19		.
	ld (M_7CDA),hl		;7ce5	22 da 7c	" . |
	ld hl,0bd00h		;7ce8	21 00 bd	! . .
	ld de,D_A500		;7ceb	11 00 a5	. . .
	ld bc,00300h		;7cee	01 00 03	. . .
	ldir			;7cf1	ed b0		. .
	jr l7c94h		;7cf3	18 9f		. .
l7cf5h:
	ld hl,(M_7BA5)		;7cf5	2a a5 7b	* . {
	ld de,(M_7BA7)		;7cf8	ed 5b a7 7b	. [ . {
	xor a			;7cfc	af		.
	ld (M_7F09),a		;7cfd	32 09 7f	2 . .
	jp R_7E10		;7d00	c3 10 7e	. . ~
l7d03h:
	pop hl			;7d03	e1		.
	ld a,0c9h		;7d04	3e c9		> .
	ld (M_769A),a		;7d06	32 9a 76	2 . v
	ld hl,R_7C08		;7d09	21 08 7c	! . |
	ld de,R_7C7B		;7d0c	11 7b 7c	. { |
	ld (M_61ED),hl		;7d0f	22 ed 61	" . a
	ld (M_6246),de		;7d12	ed 53 46 62	. S F b
	xor a			;7d16	af		.
	jp R_6166		;7d17	c3 66 61	. f a
D_7D1A:
	ld a,001h		;7d1a	3e 01		> .
	ld (M_9A02),a		;7d1c	32 02 9a	2 . .
	ld (M_8F18),a		;7d1f	32 18 8f	2 . .
	ld hl,00000h		;7d22	21 00 00	! . .
	ld (M_7BA5),hl		;7d25	22 a5 7b	" . {
	ld (M_7BA7),hl		;7d28	22 a7 7b	" . {
	xor a			;7d2b	af		.
	ld (M_8ED6),a		;7d2c	32 d6 8e	2 . .
	ld a,0c9h		;7d2f	3e c9		> .
	ld (M_9A08),a		;7d31	32 08 9a	2 . .
	ld hl,R_76B9		;7d34	21 b9 76	! . v
	ld (M_76A3),hl		;7d37	22 a3 76	" . v
	call R_80AF		;7d3a	cd af 80	. . .
	ld hl,05888h		;7d3d	21 88 58	! . X
	ld de,058a8h		;7d40	11 a8 58	. . X
	call R_80E2		;7d43	cd e2 80	. . .
	ld hl,D_8E1A		;7d46	21 1a 8e	! . .
	ld de,D_8E60		;7d49	11 60 8e	. ` .
	ld bc,00003h		;7d4c	01 03 00	. . .
	ldir			;7d4f	ed b0		. .
	call R_9A80		;7d51	cd 80 9a	. . .
	call R_9A74		;7d54	cd 74 9a	. t .
	ld de,00000h		;7d57	11 00 00	. . .
	ld hl,00000h		;7d5a	21 00 00	! . .
	ld (M_7B9A),hl		;7d5d	22 9a 7b	" . {
	ld (M_7B9C),de		;7d60	ed 53 9c 7b	. S . {
	ld (M_8F11),hl		;7d64	22 11 8f	" . .
	ld (M_8F13),de		;7d67	ed 53 13 8f	. S . .
	call R_9A4D		;7d6b	cd 4d 9a	. M .
	ld hl,VTII_INSTANCE	;7d6e	21 00 a0	! . .
	ld a,001h		;7d71	3e 01		> .
	call R_9A59		;7d73	cd 59 9a	. Y .
	ld a,005h		;7d76	3e 05		> .
	ld (M_7BA4),a		;7d78	32 a4 7b	2 . {
	ld (M_7F09),a		;7d7b	32 09 7f	2 . .
	ld hl,D_A1C2		;7d7e	21 c2 a1	! . .
	ld de,00010h		;7d81	11 10 00	. . .
	ld b,004h		;7d84	06 04		. .
l7d86h:
	ld a,(hl)		;7d86	7e		~
	cp 005h			;7d87	fe 05		. .
	jr z,l7e03h		;7d89	28 78		( x
	cp 00bh			;7d8b	fe 0b		. .
	jr z,l7e03h		;7d8d	28 74		( t
	cp 00ch			;7d8f	fe 0c		. .
	jr z,l7e03h		;7d91	28 70		( p
	cp 00fh			;7d93	fe 0f		. .
	jr z,l7e03h		;7d95	28 6c		( l
	add hl,de		;7d97	19		.
	djnz l7d86h		;7d98	10 ec		. .
l7d9ah:
	ld a,(M_7F09)		;7d9a	3a 09 7f	: . .
	or a			;7d9d	b7		.
	jp z,l7d03h		;7d9e	ca 03 7d	. . }
	ld de,(M_8F13)		;7da1	ed 5b 13 8f	. [ . .
	ld hl,(M_8F11)		;7da5	2a 11 8f	* . .
	call R_9A4D		;7da8	cd 4d 9a	. M .
	ld hl,VTII_INSTANCE	;7dab	21 00 a0	! . .
	ld a,001h		;7dae	3e 01		> .
	call R_9A59		;7db0	cd 59 9a	. Y .
	ld hl,M_7BA4		;7db3	21 a4 7b	! . {
	dec (hl)		;7db6	35		5
	jp z,l7cf5h		;7db7	ca f5 7c	. . |
	ld a,(D_A1C2)		;7dba	3a c2 a1	: . .
	cp 010h			;7dbd	fe 10		. .
	jp nc,l7cf5h		;7dbf	d2 f5 7c	. . |
	ld hl,(M_A1D6)		;7dc2	2a d6 a1	* . .
	ld de,(0a1d8h)		;7dc5	ed 5b d8 a1	. [ . .
	ld (M_7FC8),hl		;7dc9	22 c8 7f	" . .
	ld (l7fcah),de		;7dcc	ed 53 ca 7f	. S . .
	push hl			;7dd0	e5		.
	push de			;7dd1	d5		.
	ld hl,(M_8F11)		;7dd2	2a 11 8f	* . .
	ld de,(M_8F13)		;7dd5	ed 5b 13 8f	. [ . .
	call R_7FB1		;7dd9	cd b1 7f	. . .
	ld (M_8F13),de		;7ddc	ed 53 13 8f	. S . .
	ld (M_8F11),hl		;7de0	22 11 8f	" . .
	pop de			;7de3	d1		.
	pop hl			;7de4	e1		.
	ld (M_7FC8),hl		;7de5	22 c8 7f	" . .
	ld (l7fcah),de		;7de8	ed 53 ca 7f	. S . .
	call R_9A4D		;7dec	cd 4d 9a	. M .
	ld hl,VTII_INSTANCE	;7def	21 00 a0	! . .
	ld a,001h		;7df2	3e 01		> .
	call R_9A59		;7df4	cd 59 9a	. Y .
	ld hl,(M_A1C6)		;7df7	2a c6 a1	* . .
	ld de,(0a1c8h)		;7dfa	ed 5b c8 a1	. [ . .
	call R_7FB1		;7dfe	cd b1 7f	. . .
	jr R_7E10		;7e01	18 0d		. .
l7e03h:
	inc hl			;7e03	23		#
	inc hl			;7e04	23		#
D_7E05:
	inc hl			;7e05	23		#
	inc hl			;7e06	23		#
	ld e,(hl)		;7e07	5e		^
	inc hl			;7e08	23		#
	ld d,(hl)		;7e09	56		V
	inc hl			;7e0a	23		#
	ld a,(hl)		;7e0b	7e		~
	inc hl			;7e0c	23		#
	ld h,(hl)		;7e0d	66		f
	ld l,a			;7e0e	6f		o
	ex de,hl		;7e0f	eb		.
R_7E10:
	ld (M_7B90),hl		;7e10	22 90 7b	" . {
	ld (l7b92h),de		;7e13	ed 53 92 7b	. S . {
	call R_9A4D		;7e17	cd 4d 9a	. M .
	ld hl,VTII_INSTANCE	;7e1a	21 00 a0	! . .
	ld a,001h		;7e1d	3e 01		> .
	call R_9A59		;7e1f	cd 59 9a	. Y .
	ld hl,M_A003		;7e22	21 03 a0	! . .
	ld b,006h		;7e25	06 06		. .
	ld a,01dh		;7e27	3e 1d		> .
l7e29h:
	cp (hl)			;7e29	be		.
	jp nc,l7d9ah		;7e2a	d2 9a 7d	. . }
	inc hl			;7e2d	23		#
	djnz l7e29h		;7e2e	10 f9		. .
	ld hl,(M_A00B)		;7e30	2a 0b a0	* . .
	ld a,h			;7e33	7c		|
	dec a			;7e34	3d		=
	dec a			;7e35	3d		=
	or l			;7e36	b5		.
	jp nz,l7d9ah		;7e37	c2 9a 7d	. . }
	ld a,(VTII_SIG)		;7e3a	3a 0d a0	: . .
	or a			;7e3d	b7		.
	jp z,l7d9ah		;7e3e	ca 9a 7d	. . }
	ld a,(M_A010)		;7e41	3a 10 a0	: . .
	or a			;7e44	b7		.
	jp z,l7d9ah		;7e45	ca 9a 7d	. . }
	ld hl,(M_A016)		;7e48	2a 16 a0	* . .
	ld a,h			;7e4b	7c		|
	or l			;7e4c	b5		.
	jp nz,l7d9ah		;7e4d	c2 9a 7d	. . }
	ld hl,(M_A024)		;7e50	2a 24 a0	* $ .
	ld a,h			;7e53	7c		|
	or l			;7e54	b5		.
	ld hl,(M_A026)		;7e55	2a 26 a0	* & .
	or h			;7e58	b4		.
	or l			;7e59	b5		.
	jp z,l7d9ah		;7e5a	ca 9a 7d	. . }
	ld hl,M_7B90		;7e5d	21 90 7b	! . {
	ld de,M_7BA5		;7e60	11 a5 7b	. . {
	ld bc,00004h		;7e63	01 04 00	. . .
	ldir			;7e66	ed b0		. .
	ld hl,M_769D		;7e68	21 9d 76	! . v
	ld a,(hl)		;7e6b	7e		~
	dec (hl)		;7e6c	35		5
	or a			;7e6d	b7		.
	jp nz,l7d9ah		;7e6e	c2 9a 7d	. . }
	ld (hl),000h		;7e71	36 00		6 .
	ld a,(VTII_SIG)		;7e73	3a 0d a0	: . .
	ld (M_7B86),a		;7e76	32 86 7b	2 . {
	ld hl,(M_A00E)		;7e79	2a 0e a0	* . .
	ld (M_7B7B),hl		;7e7c	22 7b 7b	" { {
	ld hl,(M_A030)		;7e7f	2a 30 a0	* 0 .
	ld de,00000h		;7e82	11 00 00	. . .
	call R_7864		;7e85	cd 64 78	. d x
	ld (M_7B7D),hl		;7e88	22 7d 7b	" } {
	ld (l7b7fh),de		;7e8b	ed 53 7f 7b	. S . {
	ld a,(M_A010)		;7e8f	3a 10 a0	: . .
	ld (M_7B81),a		;7e92	32 81 7b	2 . {
	ld hl,(M_A024)		;7e95	2a 24 a0	* $ .
	ld (M_7B82),hl		;7e98	22 82 7b	" . {
	ld hl,(M_A026)		;7e9b	2a 26 a0	* & .
	ld (M_7B84),hl		;7e9e	22 84 7b	" . {
	ld hl,(M_A02C)		;7ea1	2a 2c a0	* , .
	ld (M_7B88),hl		;7ea4	22 88 7b	" . {
	ld hl,(M_A02E)		;7ea7	2a 2e a0	* . .
	ld (M_7B8A),hl		;7eaa	22 8a 7b	" . {
	ld hl,(M_7B82)		;7ead	2a 82 7b	* . {
	ld de,(M_7B84)		;7eb0	ed 5b 84 7b	. [ . {
	ld bc,(M_7B81)		;7eb4	ed 4b 81 7b	. K . {
	ld b,000h		;7eb8	06 00		. .
	call R_7FF8		;7eba	cd f8 7f	. . .
	push hl			;7ebd	e5		.
	push de			;7ebe	d5		.
	ld hl,(M_7B7B)		;7ebf	2a 7b 7b	* { {
	ld (M_7B94),hl		;7ec2	22 94 7b	" . {
	pop de			;7ec5	d1		.
	pop bc			;7ec6	c1		.
	call R_7FAD		;7ec7	cd ad 7f	. . .
	ld (M_7B96),hl		;7eca	22 96 7b	" . {
	ld (l7b98h),de		;7ecd	ed 53 98 7b	. S . {
	ld hl,00000h		;7ed1	21 00 00	! . .
	ld (M_7B9A),hl		;7ed4	22 9a 7b	" . {
	ld (M_7B9C),hl		;7ed7	22 9c 7b	" . {
	ld (M_7B51),hl		;7eda	22 51 7b	" Q {
D_7EDD:
	ld (M_7B53),hl		;7edd	22 53 7b	" S {
	ld hl,(M_7B7D)		;7ee0	2a 7d 7b	* } {
	ld de,(l7b7fh)		;7ee3	ed 5b 7f 7b	. [ . {
	call R_9A4D		;7ee7	cd 4d 9a	. M .
	ld hl,D_A200		;7eea	21 00 a2	! . .
	ld a,001h		;7eed	3e 01		> .
	call R_9A59		;7eef	cd 59 9a	. Y .
	ld hl,D_A3EC		;7ef2	21 ec a3	! . .
	ld de,D_7B8C		;7ef5	11 8c 7b	. . {
	ld bc,00004h		;7ef8	01 04 00	. . .
	ldir			;7efb	ed b0		. .
	ld hl,D_A047		;7efd	21 47 a0	! G .
	ld de,D_8E64		;7f00	11 64 8e	. d .
	ld b,00bh		;7f03	06 0b		. .
	call R_850F		;7f05	cd 0f 85	. . .
	ret			;7f08	c9		.
M_7F09:
	nop			;7f09	00		.
	ld hl,(M_8ED1)		;7f0a	2a d1 8e	* . .
	ld b,l			;7f0d	45		E
	inc b			;7f0e	04		.
	ld c,005h		;7f0f	0e 05		. .
	xor a			;7f11	af		.
	sub c			;7f12	91		.
l7f13h:
	add a,c			;7f13	81		.
	djnz l7f13h		;7f14	10 fd		. .
	ld e,a			;7f16	5f		_
	call R_8482		;7f17	cd 82 84	. . .
	ld a,h			;7f1a	7c		|
	add a,a			;7f1b	87		.
	ld l,a			;7f1c	6f		o
	ld h,0ffh		;7f1d	26 ff		& .
	ld a,(hl)		;7f1f	7e		~
	inc l			;7f20	2c		,
	ld h,(hl)		;7f21	66		f
	ld d,e			;7f22	53		S
	srl e			;7f23	cb 3b		. ;
	srl e			;7f25	cb 3b		. ;
	srl e			;7f27	cb 3b		. ;
	add a,e			;7f29	83		.
	ld l,a			;7f2a	6f		o
	ld a,(M_8ED3)		;7f2b	3a d3 8e	: . .
	add a,l			;7f2e	85		.
	ld l,a			;7f2f	6f		o
	ld a,d			;7f30	7a		z
	ld de,0f800h		;7f31	11 00 f8	. . .
	and 007h		;7f34	e6 07		. .
	jr z,l7f42h		;7f36	28 0a		( .
	add a,a			;7f38	87		.
	add a,0efh		;7f39	c6 ef		. .
	push hl			;7f3b	e5		.
	ld l,d			;7f3c	6a		j
	ld h,a			;7f3d	67		g
	ld d,(hl)		;7f3e	56		V
	inc h			;7f3f	24		$
	ld e,(hl)		;7f40	5e		^
	pop hl			;7f41	e1		.
l7f42h:
	ld b,008h		;7f42	06 08		. .
l7f44h:
	ld a,d			;7f44	7a		z
	xor (hl)		;7f45	ae		.
	ld (hl),a		;7f46	77		w
	inc l			;7f47	2c		,
	ld a,e			;7f48	7b		{
	xor (hl)		;7f49	ae		.
	ld (hl),a		;7f4a	77		w
	inc h			;7f4b	24		$
	dec l			;7f4c	2d		-
	djnz l7f44h		;7f4d	10 f5		. .
	ret			;7f4f	c9		.
	ld a,(M_9A02)		;7f50	3a 02 9a	: . .
	dec a			;7f53	3d		=
	jp nz,09a41h		;7f54	c2 41 9a	. A .
	jp R_9A62		;7f57	c3 62 9a	. b .
	ld a,(M_9A02)		;7f5a	3a 02 9a	: . .
	dec a			;7f5d	3d		=
	ret nz			;7f5e	c0		.
	jp R_9A65		;7f5f	c3 65 9a	. e .
	ld hl,00000h		;7f62	21 00 00	! . .
	ld de,00000h		;7f65	11 00 00	. . .
	exx			;7f68	d9		.
	ld hl,VTII_INSTANCE	;7f69	21 00 a0	! . .
	ld de,0000dh		;7f6c	11 0d 00	. . .
	ld b,080h		;7f6f	06 80		. .
l7f71h:
	ld a,(hl)		;7f71	7e		~
	or a			;7f72	b7		.
	ret z			;7f73	c8		.
	add hl,de		;7f74	19		.
	ld a,(hl)		;7f75	7e		~
	exx			;7f76	d9		.
	ld e,a			;7f77	5f		_
	add hl,de		;7f78	19		.
	exx			;7f79	d9		.
	inc hl			;7f7a	23		#
	inc hl			;7f7b	23		#
	inc hl			;7f7c	23		#
	djnz l7f71h		;7f7d	10 f2		. .
	ret			;7f7f	c9		.
R_7F80:
	ld a,001h		;7f80	3e 01		> .
	ld (M_8EC2),a		;7f82	32 c2 8e	2 . .
	call MUTE_BOTH		;7f85	cd 61 8a	. a .
	jp R_9129		;7f88	c3 29 91	. ) .
R_7F8B:
	ld d,h			;7f8b	54		T
	ld e,l			;7f8c	5d		]
	ld a,b			;7f8d	78		x
	ld b,c			;7f8e	41		A
	ld c,a			;7f8f	4f		O
	inc c			;7f90	0c		.
	xor a			;7f91	af		.
	dec b			;7f92	05		.
	jr z,l7f98h		;7f93	28 03		( .
l7f95h:
	add hl,de		;7f95	19		.
	djnz l7f95h		;7f96	10 fd		. .
l7f98h:
	ld b,a			;7f98	47		G
	dec c			;7f99	0d		.
	jr nz,l7f95h		;7f9a	20 f9		  .
	ret			;7f9c	c9		.
R_7F9D:
	ld b,004h		;7f9d	06 04		. .
l7f9fh:
	inc (hl)		;7f9f	34		4
	ret nz			;7fa0	c0		.
	dec hl			;7fa1	2b		+
	djnz l7f9fh		;7fa2	10 fb		. .
	ret			;7fa4	c9		.
R_7FA5:
	ld b,004h		;7fa5	06 04		. .
l7fa7h:
	inc (hl)		;7fa7	34		4
	ret nz			;7fa8	c0		.
	inc hl			;7fa9	23		#
	djnz l7fa7h		;7faa	10 fb		. .
	ret			;7fac	c9		.
R_7FAD:
	add hl,bc		;7fad	09		.
	ret nc			;7fae	d0		.
	inc de			;7faf	13		.
	ret			;7fb0	c9		.
R_7FB1:
	ex de,hl		;7fb1	eb		.
	ld bc,(l7fcah)		;7fb2	ed 4b ca 7f	. K . .
	add hl,bc		;7fb6	09		.
	ex de,hl		;7fb7	eb		.
	ld bc,(M_7FC8)		;7fb8	ed 4b c8 7f	. K . .
	add hl,bc		;7fbc	09		.
	jr nc,l7fc0h		;7fbd	30 01		0 .
	inc de			;7fbf	13		.
l7fc0h:
	ld (M_7FC8),hl		;7fc0	22 c8 7f	" . .
	ld (l7fcah),de		;7fc3	ed 53 ca 7f	. S . .
	ret			;7fc7	c9		.
M_7FC8:
	nop			;7fc8	00		.
	nop			;7fc9	00		.
l7fcah:
	nop			;7fca	00		.
	nop			;7fcb	00		.
R_7FCC:
	call R_7FD1		;7fcc	cd d1 7f	. . .
	ex de,hl		;7fcf	eb		.
	ret			;7fd0	c9		.
R_7FD1:
	ld ix,0ffffh		;7fd1	dd 21 ff ff	. ! . .
	inc de			;7fd5	13		.
l7fd6h:
	dec de			;7fd6	1b		.
	or a			;7fd7	b7		.
l7fd8h:
	sbc hl,bc		;7fd8	ed 42		. B
	inc ix			;7fda	dd 23		. #
	jp nc,l7fd8h		;7fdc	d2 d8 7f	. . .
	ld a,d			;7fdf	7a		z
	or e			;7fe0	b3		.
	jp nz,l7fd6h		;7fe1	c2 d6 7f	. . .
	push ix			;7fe4	dd e5		. .
	pop de			;7fe6	d1		.
	add hl,bc		;7fe7	09		.
	ld (M_8043),hl		;7fe8	22 43 80	" C .
	ld (M_803D),de		;7feb	ed 53 3d 80	. S = .
	ret			;7fef	c9		.
R_7FF0:
	call R_7FCC		;7ff0	cd cc 7f	. . .
	inc hl			;7ff3	23		#
	ld (M_803D),hl		;7ff4	22 3d 80	" = .
	ret			;7ff7	c9		.
R_7FF8:
	ld a,b			;7ff8	78		x
	ld b,c			;7ff9	41		A
	ld c,a			;7ffa	4f		O
	inc c			;7ffb	0c		.
	or a			;7ffc	b7		.
D_7FFD:
	jr nz,l8003h		;7ffd	20 04		  .
	dec b			;7fff	05		.
	jr z,l802eh		;8000	28 2c		( ,
	inc b			;8002	04		.
l8003h:
	xor a			;8003	af		.
	cp b			;8004	b8		.
D_8005:
	jr nz,l8008h		;8005	20 01		  .
	dec c			;8007	0d		.
l8008h:
	dec b			;8008	05		.
	push hl			;8009	e5		.
	push bc			;800a	c5		.
	ld h,d			;800b	62		b
	ld l,e			;800c	6b		k
	cp b			;800d	b8		.
	jr z,l8014h		;800e	28 04		( .
l8010h:
	add hl,de		;8010	19		.
	djnz l8010h		;8011	10 fd		. .
	ld b,a			;8013	47		G
l8014h:
	dec c			;8014	0d		.
	jr nz,l8010h		;8015	20 f9		  .
	ld (M_803A),hl		;8017	22 3a 80	" : .
	pop bc			;801a	c1		.
	pop hl			;801b	e1		.
	ld d,h			;801c	54		T
	ld e,l			;801d	5d		]
	cp b			;801e	b8		.
	jr z,l8027h		;801f	28 06		( .
l8021h:
	add hl,de		;8021	19		.
	jr c,l802fh		;8022	38 0b		8 .
l8024h:
	djnz l8021h		;8024	10 fb		. .
	ld b,a			;8026	47		G
l8027h:
	dec c			;8027	0d		.
	jr nz,l8021h		;8028	20 f7		  .
	ld de,(M_803A)		;802a	ed 5b 3a 80	. [ : .
l802eh:
	ret			;802e	c9		.
l802fh:
	exx			;802f	d9		.
	ld hl,(M_803A)		;8030	2a 3a 80	* : .
	inc hl			;8033	23		#
	ld (M_803A),hl		;8034	22 3a 80	" : .
	exx			;8037	d9		.
	jr l8024h		;8038	18 ea		. .
M_803A:
	nop			;803a	00		.
	nop			;803b	00		.
	nop			;803c	00		.
M_803D:
	nop			;803d	00		.
D_803E:
	nop			;803e	00		.
M_803F:
	nop			;803f	00		.
M_8040:
	nop			;8040	00		.
l8041h:
	nop			;8041	00		.
	nop			;8042	00		.
M_8043:
	nop			;8043	00		.
	nop			;8044	00		.
	nop			;8045	00		.
	nop			;8046	00		.
R_8047:
	ld hl,05820h		;8047	21 20 58	!   X
	ld b,008h		;804a	06 08		. .
	ld a,005h		;804c	3e 05		> .
l804eh:
	ld (hl),a		;804e	77		w
	inc hl			;804f	23		#
	djnz l804eh		;8050	10 fc		. .
	ret			;8052	c9		.
R_8053:
	ld a,(M_6B24)		;8053	3a 24 6b	: $ k
	or a			;8056	b7		.
	ret nz			;8057	c0		.
	ld hl,D_9880		;8058	21 80 98	! . .
	ld bc,01006h		;805b	01 06 10	. . .
	ld a,01ah		;805e	3e 1a		> .
	ld (M_807A),a		;8060	32 7a 80	2 z .
	call R_8071		;8063	cd 71 80	. q .
	ld hl,D_9930		;8066	21 30 99	! 0 .
	ld bc,00809h		;8069	01 09 08	. . .
	ld a,00eh		;806c	3e 0e		> .
	ld (M_807A),a		;806e	32 7a 80	2 z .
R_8071:
	push bc			;8071	c5		.
	ld a,(hl)		;8072	7e		~
	inc hl			;8073	23		#
	ld d,(hl)		;8074	56		V
	inc hl			;8075	23		#
	add a,c			;8076	81		.
	ld e,a			;8077	5f		_
	ex de,hl		;8078	eb		.
	ld b,01ah		;8079	06 1a		. .
	call R_74B5		;807b	cd b5 74	. . t
	ex de,hl		;807e	eb		.
	pop bc			;807f	c1		.
	djnz R_8071		;8080	10 ef		. .
	ret			;8082	c9		.
R_8083:
	call R_8482		;8083	cd 82 84	. . .
	ld hl,D_809C		;8086	21 9c 80	! . .
	ld b,002h		;8089	06 02		. .
	ld de,040e9h		;808b	11 e9 40	. . @
	call R_85BC		;808e	cd bc 85	. . .
	ld hl,D_809C		;8091	21 9c 80	! . .
	ld b,002h		;8094	06 02		. .
	ld de,040f5h		;8096	11 f5 40	. . @
	jp R_85BC		;8099	c3 bc 85	. . .
D_809C:
	ld d,h			;809c	54		T
	ld d,e			;809d	53		S
R_809E:
	ld hl,05856h		;809e	21 56 58	! V X
	ld a,006h		;80a1	3e 06		> .
	ld de,0001fh		;80a3	11 1f 00	. . .
	ld b,004h		;80a6	06 04		. .
l80a8h:
	ld (hl),a		;80a8	77		w
	inc hl			;80a9	23		#
	ld (hl),a		;80aa	77		w
	add hl,de		;80ab	19		.
	djnz l80a8h		;80ac	10 fa		. .
	ret			;80ae	c9		.
R_80AF:
	ld hl,05848h		;80af	21 48 58	! H X
	ld a,006h		;80b2	3e 06		> .
	ld de,0001fh		;80b4	11 1f 00	. . .
	ld b,004h		;80b7	06 04		. .
l80b9h:
	ld (hl),a		;80b9	77		w
	inc hl			;80ba	23		#
	ld (hl),a		;80bb	77		w
	add hl,de		;80bc	19		.
	djnz l80b9h		;80bd	10 fa		. .
	ret			;80bf	c9		.
R_80C0:
	jr R_80D4		;80c0	18 12		. .
	ld hl,05916h		;80c2	21 16 59	! . Y
	ld de,05936h		;80c5	11 36 59	. 6 Y
	jr R_80E2		;80c8	18 18		. .
R_80CA:
	jr R_80DC		;80ca	18 10		. .
	ld hl,058d6h		;80cc	21 d6 58	! . X
	ld de,058f6h		;80cf	11 f6 58	. . X
	jr R_80E2		;80d2	18 0e		. .
R_80D4:
	ld hl,05896h		;80d4	21 96 58	! . X
	ld de,058b6h		;80d7	11 b6 58	. . X
	jr R_80E2		;80da	18 06		. .
R_80DC:
	ld hl,05856h		;80dc	21 56 58	! V X
	ld de,05876h		;80df	11 76 58	. v X
R_80E2:
	ld a,046h		;80e2	3e 46		> F
	ld (hl),a		;80e4	77		w
	inc hl			;80e5	23		#
	ld (hl),a		;80e6	77		w
	ex de,hl		;80e7	eb		.
	ld (hl),a		;80e8	77		w
	inc hl			;80e9	23		#
	ld (hl),a		;80ea	77		w
	xor a			;80eb	af		.
	ret			;80ec	c9		.
R_80ED:
	ld de,(M_8ECE)		;80ed	ed 5b ce 8e	. [ . .
	ld b,008h		;80f1	06 08		. .
	push de			;80f3	d5		.
	push hl			;80f4	e5		.
	ld hl,05b8fh		;80f5	21 8f 5b	! . [
	ld d,000h		;80f8	16 00		. .
	add hl,de		;80fa	19		.
	add hl,de		;80fb	19		.
	ld d,(hl)		;80fc	56		V
	inc hl			;80fd	23		#
	ex af,af'		;80fe	08		.
	ld a,(M_8ECD)		;80ff	3a cd 8e	: . .
	add a,(hl)		;8102	86		.
	ld e,a			;8103	5f		_
	ld a,(de)		;8104	1a		.
	ld (M_8110),a		;8105	32 10 81	2 . .
	ex af,af'		;8108	08		.
l8109h:
	ld (de),a		;8109	12		.
	inc de			;810a	13		.
	djnz l8109h		;810b	10 fc		. .
	pop hl			;810d	e1		.
	pop de			;810e	d1		.
	ret			;810f	c9		.
M_8110:
	ld b,l			;8110	45		E
R_8111:
	ld (M_8139),a		;8111	32 39 81	2 9 .
	push de			;8114	d5		.
	exx			;8115	d9		.
	pop de			;8116	d1		.
	ld hl,D_9880		;8117	21 80 98	! . .
	ld d,000h		;811a	16 00		. .
	add hl,de		;811c	19		.
	add hl,de		;811d	19		.
	add a,(hl)		;811e	86		.
	inc l			;811f	2c		,
	exx			;8120	d9		.
	ld e,a			;8121	5f		_
	exx			;8122	d9		.
	ld a,(hl)		;8123	7e		~
	inc hl			;8124	23		#
	exx			;8125	d9		.
	ld d,a			;8126	57		W
	ld a,c			;8127	79		y
	ld (M_812E),a		;8128	32 2e 81	2 . .
l812bh:
	ld a,b			;812b	78		x
	ld bc,001ffh		;812c	01 ff 01	. . .
l812fh:
	ldi			;812f	ed a0		. .
	djnz l812fh		;8131	10 fc		. .
	ld b,a			;8133	47		G
	exx			;8134	d9		.
	ld a,(hl)		;8135	7e		~
	inc l			;8136	2c		,
	exx			;8137	d9		.
	add a,000h		;8138	c6 00		. .
	ld e,a			;813a	5f		_
	exx			;813b	d9		.
	ld a,(hl)		;813c	7e		~
	inc hl			;813d	23		#
	exx			;813e	d9		.
	ld d,a			;813f	57		W
	djnz l812bh		;8140	10 e9		. .
	ret			;8142	c9		.
R_8143:
	ei			;8143	fb		.
	halt			;8144	76		v
	djnz R_8143		;8145	10 fc		. .
	ret			;8147	c9		.
R_8148:
	push bc			;8148	c5		.
	push de			;8149	d5		.
	call R_8C5D		;814a	cd 5d 8c	. ] .
	pop de			;814d	d1		.
	pop bc			;814e	c1		.
	ret nz			;814f	c0		.
	inc hl			;8150	23		#
	inc e			;8151	1c		.
	djnz R_8148		;8152	10 f4		. .
	ret			;8154	c9		.
D_8155:
	call R_8482		;8155	cd 82 84	. . .
	xor a			;8158	af		.
	ld (M_84BB),a		;8159	32 bb 84	2 . .
	ld bc,05b4fh		;815c	01 4f 5b	. O [
	call R_84BC		;815f	cd bc 84	. . .
	ld hl,05b4fh		;8162	21 4f 5b	! O [
R_8165:
	exx			;8165	d9		.
	push bc			;8166	c5		.
	push de			;8167	d5		.
	push hl			;8168	e5		.
	ld a,016h		;8169	3e 16		> .
	sub b			;816b	90		.
	ld hl,00000h		;816c	21 00 00	! . .
	ld de,00020h		;816f	11 20 00	.   .
	ld b,a			;8172	47		G
l8173h:
	add hl,de		;8173	19		.
	djnz l8173h		;8174	10 fd		. .
	ld de,05820h		;8176	11 20 58	.   X
	ld a,018h		;8179	3e 18		> .
	add a,e			;817b	83		.
	ld e,a			;817c	5f		_
	add hl,de		;817d	19		.
	ld a,(M_84EE)		;817e	3a ee 84	: . .
	ld b,008h		;8181	06 08		. .
l8183h:
	ld (hl),a		;8183	77		w
	inc hl			;8184	23		#
	djnz l8183h		;8185	10 fc		. .
	pop hl			;8187	e1		.
	pop de			;8188	d1		.
	pop bc			;8189	c1		.
	exx			;818a	d9		.
	ld b,008h		;818b	06 08		. .
	jp R_8148		;818d	c3 48 81	. H .
R_8190:
	ld a,(0a8e4h)		;8190	3a e4 a8	: . .
	ld de,D_8E89		;8193	11 89 8e	. . .
	call R_92FE		;8196	cd fe 92	. . .
	ld a,(0a8f4h)		;8199	3a f4 a8	: . .
	ld de,D_8E8D		;819c	11 8d 8e	. . .
	call R_92FE		;819f	cd fe 92	. . .
	ld hl,(0a8e5h)		;81a2	2a e5 a8	* . .
	ld de,D_8E97		;81a5	11 97 8e	. . .
	call R_92E0		;81a8	cd e0 92	. . .
	ld hl,0a8f5h		;81ab	21 f5 a8	! . .
	ld de,D_8E79		;81ae	11 79 8e	. y .
	ld b,00bh		;81b1	06 0b		. .
	call R_850F		;81b3	cd 0f 85	. . .
	call R_8482		;81b6	cd 82 84	. . .
	ld hl,VTII_INSTANCE	;81b9	21 00 a0	! . .
	ld de,0d800h		;81bc	11 00 d8	. . .
R_81BF:
	xor a			;81bf	af		.
	ld (de),a		;81c0	12		.
	inc de			;81c1	13		.
	ld (de),a		;81c2	12		.
	inc de			;81c3	13		.
	ld a,(hl)		;81c4	7e		~
	or a			;81c5	b7		.
	jr z,l81f7h		;81c6	28 2f		( /
	ld bc,00009h		;81c8	01 09 00	. . .
	ldir			;81cb	ed b0		. .
	ld a,(hl)		;81cd	7e		~
	inc hl			;81ce	23		#
	cp 041h			;81cf	fe 41		. A
	jr c,l81e0h		;81d1	38 0d		8 .
	cp 07bh			;81d3	fe 7b		. {
	jr nc,l81e0h		;81d5	30 09		0 .
	ld a,(hl)		;81d7	7e		~
	cp 041h			;81d8	fe 41		. A
	jr c,l81e0h		;81da	38 04		8 .
	cp 07bh			;81dc	fe 7b		. {
	jr c,l81e9h		;81de	38 09		8 .
l81e0h:
	ld a,020h		;81e0	3e 20		>  
	ld (de),a		;81e2	12		.
	inc de			;81e3	13		.
	ld (de),a		;81e4	12		.
	inc de			;81e5	13		.
	inc hl			;81e6	23		#
	jr l81eeh		;81e7	18 05		. .
l81e9h:
	dec hl			;81e9	2b		+
	ld c,002h		;81ea	0e 02		. .
	ldir			;81ec	ed b0		. .
l81eeh:
	inc hl			;81ee	23		#
	inc hl			;81ef	23		#
	ld c,003h		;81f0	0e 03		. .
	ldir			;81f2	ed b0		. .
	jp R_81BF		;81f4	c3 bf 81	. . .
l81f7h:
	ld (de),a		;81f7	12		.
	inc de			;81f8	13		.
	ld (de),a		;81f9	12		.
	inc de			;81fa	13		.
	ld (de),a		;81fb	12		.
	ret			;81fc	c9		.
D_81FD:
	call R_8482		;81fd	cd 82 84	. . .
	xor a			;8200	af		.
	ld (M_84BB),a		;8201	32 bb 84	2 . .
	ld bc,05b4fh		;8204	01 4f 5b	. O [
	call R_84BC		;8207	cd bc 84	. . .
	ld a,d			;820a	7a		z
	cp 050h			;820b	fe 50		. P
	jr nz,l8214h		;820d	20 05		  .
	ld hl,05ad8h		;820f	21 d8 5a	! . Z
	jr l8217h		;8212	18 03		. .
l8214h:
	ld hl,05858h		;8214	21 58 58	! X X
l8217h:
	ld a,(M_84EE)		;8217	3a ee 84	: . .
	ld b,008h		;821a	06 08		. .
l821ch:
	ld (hl),a		;821c	77		w
	inc hl			;821d	23		#
	djnz l821ch		;821e	10 fc		. .
	ld hl,05b4fh		;8220	21 4f 5b	! O [
	ld b,008h		;8223	06 08		. .
	ld a,018h		;8225	3e 18		> .
	add a,e			;8227	83		.
	ld e,a			;8228	5f		_
	jp R_8148		;8229	c3 48 81	. H .
D_822C:
	call R_848A		;822c	cd 8a 84	. . .
	ld a,003h		;822f	3e 03		> .
	ld (M_84BB),a		;8231	32 bb 84	2 . .
	call R_8252		;8234	cd 52 82	. R .
	push hl			;8237	e5		.
	ld a,d			;8238	7a		z
	cp 050h			;8239	fe 50		. P
	jr nz,l8242h		;823b	20 05		  .
	ld hl,05ac0h		;823d	21 c0 5a	! . Z
	jr l8245h		;8240	18 03		. .
l8242h:
	ld hl,05840h		;8242	21 40 58	! @ X
l8245h:
	ld a,c			;8245	79		y
	ld b,008h		;8246	06 08		. .
l8248h:
	ld (hl),a		;8248	77		w
	inc hl			;8249	23		#
	djnz l8248h		;824a	10 fc		. .
	pop hl			;824c	e1		.
	ld b,008h		;824d	06 08		. .
	jp R_8148		;824f	c3 48 81	. H .
R_8252:
	push de			;8252	d5		.
	dec hl			;8253	2b		+
	ld a,(hl)		;8254	7e		~
	inc hl			;8255	23		#
	cp 002h			;8256	fe 02		. .
	jr z,l8264h		;8258	28 0a		( .
	and 010h		;825a	e6 10		. .
	cp 010h			;825c	fe 10		. .
	jr z,l8264h		;825e	28 04		( .
	ld c,007h		;8260	0e 07		. .
	jr l8266h		;8262	18 02		. .
l8264h:
	ld c,047h		;8264	0e 47		. G
l8266h:
	ld a,c			;8266	79		y
	ld (M_84EE),a		;8267	32 ee 84	2 . .
	push bc			;826a	c5		.
	ld de,05b4fh		;826b	11 4f 5b	. O [
	ld bc,0000ch		;826e	01 0c 00	. . .
	ldir			;8271	ed b0		. .
	ld a,020h		;8273	3e 20		>  
	ld (de),a		;8275	12		.
	ld hl,05b4fh		;8276	21 4f 5b	! O [
	pop bc			;8279	c1		.
	pop de			;827a	d1		.
	ret			;827b	c9		.
D_827C:
	call R_8252		;827c	cd 52 82	. R .
	jp R_8165		;827f	c3 65 81	. e .
R_8282:
	ld a,(M_8ED4)		;8282	3a d4 8e	: . .
	or a			;8285	b7		.
	jp z,l8316h		;8286	ca 16 83	. . .
	dec a			;8289	3d		=
	jr z,l8290h		;828a	28 04		( .
	dec a			;828c	3d		=
	jp z,l82d9h		;828d	ca d9 82	. . .
l8290h:
	ld a,(M_831D)		;8290	3a 1d 83	: . .
	or a			;8293	b7		.
	jp nz,l8316h		;8294	c2 16 83	. . .
	ld hl,(M_85AE)		;8297	2a ae 85	* . .
	ld de,0d802h		;829a	11 02 d8	. . .
	xor a			;829d	af		.
	sbc hl,de		;829e	ed 52		. R
	jp z,l8316h		;82a0	ca 16 83	. . .
	call R_8453		;82a3	cd 53 84	. S .
	ei			;82a6	fb		.
	ld hl,(M_8F00)		;82a7	2a 00 8f	* . .
	dec hl			;82aa	2b		+
	ld (M_8F00),hl		;82ab	22 00 8f	" . .
	ld hl,(M_85AE)		;82ae	2a ae 85	* . .
	ld de,0fff0h		;82b1	11 f0 ff	. . .
	add hl,de		;82b4	19		.
	ld (M_85AE),hl		;82b5	22 ae 85	" . .
	push hl			;82b8	e5		.
	xor a			;82b9	af		.
	ld (M_831E),a		;82ba	32 1e 83	2 . .
	ld hl,M_7329		;82bd	21 29 73	! ) s
	ld a,(hl)		;82c0	7e		~
	cp 016h			;82c1	fe 16		. .
	jr nc,l82cdh		;82c3	30 08		0 .
	inc (hl)		;82c5	34		4
	ld a,001h		;82c6	3e 01		> .
	ld (M_831E),a		;82c8	32 1e 83	2 . .
	jr l82d4h		;82cb	18 07		. .
l82cdh:
	ld hl,(M_85B0)		;82cd	2a b0 85	* . .
	add hl,de		;82d0	19		.
	ld (M_85B0),hl		;82d1	22 b0 85	" . .
l82d4h:
	ld de,04040h		;82d4	11 40 40	. @ @
	jr l8309h		;82d7	18 30		. 0
l82d9h:
	call R_8482		;82d9	cd 82 84	. . .
	ld a,(M_831E)		;82dc	3a 1e 83	: . .
	or a			;82df	b7		.
	jr nz,l8316h		;82e0	20 34		  4
	ld hl,(M_85B0)		;82e2	2a b0 85	* . .
	ld a,(hl)		;82e5	7e		~
	or a			;82e6	b7		.
	jr z,l8316h		;82e7	28 2d		( -
	call R_8448		;82e9	cd 48 84	. H .
	ei			;82ec	fb		.
	ld hl,(M_8F00)		;82ed	2a 00 8f	* . .
	inc hl			;82f0	23		#
	ld (M_8F00),hl		;82f1	22 00 8f	" . .
	ld hl,(M_85AE)		;82f4	2a ae 85	* . .
	ld de,00010h		;82f7	11 10 00	. . .
	add hl,de		;82fa	19		.
	ld (M_85AE),hl		;82fb	22 ae 85	" . .
	ld hl,(M_85B0)		;82fe	2a b0 85	* . .
	push hl			;8301	e5		.
	add hl,de		;8302	19		.
	ld (M_85B0),hl		;8303	22 b0 85	" . .
	ld de,050c0h		;8306	11 c0 50	. . P
l8309h:
	pop hl			;8309	e1		.
	xor a			;830a	af		.
	ld (M_8DCB),a		;830b	32 cb 8d	2 . .
	call D_81FD		;830e	cd fd 81	. . .
l8311h:
	xor a			;8311	af		.
	ld (M_8ED4),a		;8312	32 d4 8e	2 . .
	ret			;8315	c9		.
l8316h:
	ld a,001h		;8316	3e 01		> .
	ld (M_8C29),a		;8318	32 29 8c	2 ) .
	jr l8311h		;831b	18 f4		. .
M_831D:
	nop			;831d	00		.
M_831E:
	nop			;831e	00		.
M_831F:
	nop			;831f	00		.
	nop			;8320	00		.
M_8321:
	ld d,022h		;8321	16 22		. "
	ld h,(hl)		;8323	66		f
	add a,e			;8324	83		.
	inc l			;8325	2c		,
	inc l			;8326	2c		,
	ld (M_836A),hl		;8327	22 6a 83	" j .
	inc l			;832a	2c		,
	inc l			;832b	2c		,
	ld (M_836E),hl		;832c	22 6e 83	" n .
	inc l			;832f	2c		,
	inc l			;8330	2c		,
	ld (M_8372),hl		;8331	22 72 83	" r .
	ld hl,D_8361		;8334	21 61 83	! a .
	ld bc,00014h		;8337	01 14 00	. . .
	ldir			;833a	ed b0		. .
	ret			;833c	c9		.
R_833D:
	inc hl			;833d	23		#
	ld b,(hl)		;833e	46		F
	inc hl			;833f	23		#
	push hl			;8340	e5		.
	ld (M_8366),bc		;8341	ed 43 66 83	. C f .
	inc c			;8345	0c		.
	inc c			;8346	0c		.
	ld (M_836A),bc		;8347	ed 43 6a 83	. C j .
	inc c			;834b	0c		.
	inc c			;834c	0c		.
	ld (M_836E),bc		;834d	ed 43 6e 83	. C n .
	inc c			;8351	0c		.
	inc c			;8352	0c		.
	ld (M_8372),bc		;8353	ed 43 72 83	. C r .
	ld hl,D_8361		;8357	21 61 83	! a .
	ld bc,00013h		;835a	01 13 00	. . .
	ldir			;835d	ed b0		. .
	pop hl			;835f	e1		.
	ret			;8360	c9		.
D_8361:
	ld sp,04038h		;8361	31 38 40	1 8 @
	pop hl			;8364	e1		.
	ld (04058h),hl		;8365	22 58 40	" X @
	pop hl			;8368	e1		.
	ld (0405ah),hl		;8369	22 5a 40	" Z @
	pop hl			;836c	e1		.
	ld (0405ch),hl		;836d	22 5c 40	" \ @
	pop hl			;8370	e1		.
	ld (0405eh),hl		;8371	22 5e 40	" ^ @
	nop			;8374	00		.
	nop			;8375	00		.
R_8376:
	xor a			;8376	af		.
	ld (M_83CF),a		;8377	32 cf 83	2 . .
	ld (M_83DA),a		;837a	32 da 83	2 . .
	ld (M_842A),a		;837d	32 2a 84	2 * .
	ld (M_8435),a		;8380	32 35 84	2 5 .
	exx			;8383	d9		.
	ld hl,05860h		;8384	21 60 58	! ` X
	jr l839bh		;8387	18 12		. .
R_8389:
	ld a,018h		;8389	3e 18		> .
	ld (M_83CF),a		;838b	32 cf 83	2 . .
	ld (M_83DA),a		;838e	32 da 83	2 . .
	ld (M_842A),a		;8391	32 2a 84	2 * .
	ld (M_8435),a		;8394	32 35 84	2 5 .
	exx			;8397	d9		.
	ld hl,05878h		;8398	21 78 58	! x X
l839bh:
	ld bc,00020h		;839b	01 20 00	.   .
	exx			;839e	d9		.
	call R_84B2		;839f	cd b2 84	. . .
	ld hl,D_98A0		;83a2	21 a0 98	! . .
	ld b,0a1h		;83a5	06 a1		. .
	ld c,0ffh		;83a7	0e ff		. .
	ld de,0c000h		;83a9	11 00 c0	. . .
l83ach:
	inc c			;83ac	0c		.
	ld a,c			;83ad	79		y
	cp 008h			;83ae	fe 08		. .
	jr c,l83c7h		;83b0	38 15		8 .
	push hl			;83b2	e5		.
	push bc			;83b3	c5		.
	exx			;83b4	d9		.
	push hl			;83b5	e5		.
	add hl,bc		;83b6	09		.
	exx			;83b7	d9		.
	pop hl			;83b8	e1		.
	ld (M_8362),hl		;83b9	22 62 83	" b .
	ld bc,0ffe0h		;83bc	01 e0 ff	. . .
	add hl,bc		;83bf	09		.
	call R_8322		;83c0	cd 22 83	. " .
	pop bc			;83c3	c1		.
	pop hl			;83c4	e1		.
	ld c,000h		;83c5	0e 00		. .
l83c7h:
	push bc			;83c7	c5		.
	push hl			;83c8	e5		.
	ld bc,00010h		;83c9	01 10 00	. . .
	add hl,bc		;83cc	09		.
	ld a,(hl)		;83cd	7e		~
	add a,018h		;83ce	c6 18		. .
	ld c,a			;83d0	4f		O
	inc hl			;83d1	23		#
	ld b,(hl)		;83d2	46		F
	ld (M_8362),bc		;83d3	ed 43 62 83	. C b .
	pop hl			;83d7	e1		.
	ld a,(hl)		;83d8	7e		~
	add a,018h		;83d9	c6 18		. .
	ld c,a			;83db	4f		O
	call R_833D		;83dc	cd 3d 83	. = .
	pop bc			;83df	c1		.
	dec b			;83e0	05		.
	jp nz,l83ach		;83e1	c2 ac 83	. . .
	ld hl,D_845E		;83e4	21 5e 84	! ^ .
	ld bc,00005h		;83e7	01 05 00	. . .
	ldir			;83ea	ed b0		. .
	exx			;83ec	d9		.
	or a			;83ed	b7		.
	sbc hl,bc		;83ee	ed 42		. B
	exx			;83f0	d9		.
	ld de,0d100h		;83f1	11 00 d1	. . .
	ld hl,D_99E0		;83f4	21 e0 99	! . .
	ld b,0a1h		;83f7	06 a1		. .
	ld c,0ffh		;83f9	0e ff		. .
l83fbh:
	inc c			;83fb	0c		.
	ld a,c			;83fc	79		y
	cp 008h			;83fd	fe 08		. .
	jr c,l8420h		;83ff	38 1f		8 .
	push hl			;8401	e5		.
	push bc			;8402	c5		.
	exx			;8403	d9		.
	or a			;8404	b7		.
	sbc hl,bc		;8405	ed 42		. B
	push hl			;8407	e5		.
	exx			;8408	d9		.
	pop hl			;8409	e1		.
	ld (M_8362),hl		;840a	22 62 83	" b .
	ld bc,00020h		;840d	01 20 00	.   .
	add hl,bc		;8410	09		.
	call R_8322		;8411	cd 22 83	. " .
	pop bc			;8414	c1		.
	pop hl			;8415	e1		.
	ld a,b			;8416	78		x
	ld bc,00020h		;8417	01 20 00	.   .
	or a			;841a	b7		.
	sbc hl,bc		;841b	ed 42		. B
	ld c,000h		;841d	0e 00		. .
	ld b,a			;841f	47		G
l8420h:
	push bc			;8420	c5		.
	push hl			;8421	e5		.
	ld bc,00010h		;8422	01 10 00	. . .
	or a			;8425	b7		.
	sbc hl,bc		;8426	ed 42		. B
	ld a,(hl)		;8428	7e		~
	add a,018h		;8429	c6 18		. .
	ld c,a			;842b	4f		O
	inc hl			;842c	23		#
	ld b,(hl)		;842d	46		F
	ld (M_8362),bc		;842e	ed 43 62 83	. C b .
	pop hl			;8432	e1		.
	ld a,(hl)		;8433	7e		~
	add a,018h		;8434	c6 18		. .
	ld c,a			;8436	4f		O
	call R_833D		;8437	cd 3d 83	. = .
	pop bc			;843a	c1		.
	dec b			;843b	05		.
	jp nz,l83fbh		;843c	c2 fb 83	. . .
	ld hl,D_845E		;843f	21 5e 84	! ^ .
	ld bc,00005h		;8442	01 05 00	. . .
	ldir			;8445	ed b0		. .
	ret			;8447	c9		.
R_8448:
	call R_84B2		;8448	cd b2 84	. . .
	di			;844b	f3		.
	ld (l8463h),sp		;844c	ed 73 63 84	. s c .
	jp 0c000h		;8450	c3 00 c0	. . .
R_8453:
	call R_84B2		;8453	cd b2 84	. . .
	di			;8456	f3		.
	ld (l8463h),sp		;8457	ed 73 63 84	. s c .
	jp 0d100h		;845b	c3 00 d1	. . .
D_845E:
	ld sp,(l8463h)		;845e	ed 7b 63 84	. { c .
	ret			;8462	c9		.
l8463h:
	nop			;8463	00		.
	nop			;8464	00		.
	ld (M_84BB),a		;8465	32 bb 84	2 . .
	jr R_847C		;8468	18 12		. .
	ld a,(M_84BB)		;846a	3a bb 84	: . .
	jr R_847C		;846d	18 0d		. .
R_846F:
	ld a,(M_84BA)		;846f	3a ba 84	: . .
	jr R_847C		;8472	18 08		. .
R_8474:
	push af			;8474	f5		.
	ld a,(M_933C)		;8475	3a 3c 93	: < .
	ld (M_84BA),a		;8478	32 ba 84	2 . .
	pop af			;847b	f1		.
R_847C:
	ld (M_933C),a		;847c	32 3c 93	2 < .
;JP BANK_SWITCH - bank helper tail-jump thunk
BANK_SWITCH_TAIL:
	jp BANK_SWITCH		;847f	c3 e0 5f	. . _
R_8482:
	ld a,000h		;8482	3e 00		> .
	jr R_847C		;8484	18 f6		. .
R_8486:
	ld a,001h		;8486	3e 01		> .
	jr R_847C		;8488	18 f2		. .
R_848A:
	ld a,003h		;848a	3e 03		> .
	jr R_847C		;848c	18 ee		. .
R_848E:
	ld a,004h		;848e	3e 04		> .
	jr R_847C		;8490	18 ea		. .
R_8492:
	ld a,006h		;8492	3e 06		> .
	jr R_847C		;8494	18 e6		. .
R_8496:
	ld a,007h		;8496	3e 07		> .
	jr R_847C		;8498	18 e2		. .
	ld a,008h		;849a	3e 08		> .
	jr R_847C		;849c	18 de		. .
R_849E:
	ld a,009h		;849e	3e 09		> .
	jr R_847C		;84a0	18 da		. .
	ld a,00ah		;84a2	3e 0a		> .
	jr R_847C		;84a4	18 d6		. .
	ld a,00bh		;84a6	3e 0b		> .
	jr R_847C		;84a8	18 d2		. .
	ld a,00ch		;84aa	3e 0c		> .
	jr R_847C		;84ac	18 ce		. .
	ld a,00dh		;84ae	3e 0d		> .
	jr R_847C		;84b0	18 ca		. .
R_84B2:
	ld a,00eh		;84b2	3e 0e		> .
	jr R_847C		;84b4	18 c6		. .
R_84B6:
	ld a,00fh		;84b6	3e 0f		> .
	jr R_847C		;84b8	18 c2		. .
M_84BA:
	rlca			;84ba	07		.
M_84BB:
	rlca			;84bb	07		.
R_84BC:
	push de			;84bc	d5		.
	push hl			;84bd	e5		.
	ld d,b			;84be	50		P
	ld e,c			;84bf	59		Y
	ld bc,00008h		;84c0	01 08 00	. . .
	ld a,(hl)		;84c3	7e		~
	ldir			;84c4	ed b0		. .
	cp 001h			;84c6	fe 01		. .
	jr z,l84cbh		;84c8	28 01		( .
	ld a,(hl)		;84ca	7e		~
l84cbh:
	ex de,hl		;84cb	eb		.
	ld (hl),02eh		;84cc	36 2e		6 .
	inc hl			;84ce	23		#
	ex de,hl		;84cf	eb		.
	ld c,003h		;84d0	0e 03		. .
	ldir			;84d2	ed b0		. .
	push hl			;84d4	e5		.
	ld hl,D_84EF		;84d5	21 ef 84	! . .
	ld c,010h		;84d8	0e 10		. .
	cpir			;84da	ed b1		. .
	ld hl,D_850E		;84dc	21 0e 85	! . .
	or a			;84df	b7		.
	sbc hl,bc		;84e0	ed 42		. B
	ld a,(hl)		;84e2	7e		~
	ld (M_84EE),a		;84e3	32 ee 84	2 . .
	pop hl			;84e6	e1		.
	pop bc			;84e7	c1		.
; --------------------------------------------------------------------------
; DATA 0x84E8-0x84FF  setup key table 'WwTtSsMmBCcZz-'
; --------------------------------------------------------------------------
DATA_84E8:
	defb 0B 0B 0A 12 D1 C9 05 57  ;84E8  .......W
	defb 77 54 74 53 73 4D 6D 42  ;84F0  wTtSsMmB
	defb 43 63 5A 7A 2D 01 00 44  ;84F8  CcZz-..D


; ..........................................................................
; code segment restart 0x8500 - clean decode boundary after data above
; ..........................................................................
;PUSH BC / LD A,#? / AND 7 / OR #10 / LD BC,#7FFD / OUT (C),A / POP BC / RET
;Pages RAM bank (A & 7) into the 0xC000 window, keeping bit 4 (screen)
;and bit 3 (48K ROM) intact. Used by every bank-aware player routine.
;TurboSound mode: 0 = single chip, 1/2 = dual chip.
;Gates the dual-chip init (86EF) and the per-frame switch (907D).
;Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above.

	ld b,h			;8500	44		D
	ld b,h			;8501	44		D
	ld b,h			;8502	44		D
	inc b			;8503	04		.
	inc b			;8504	04		.
	ld b,l			;8505	45		E
	ld b,l			;8506	45		E
	ld b,a			;8507	47		G
	rlca			;8508	07		.
	rlca			;8509	07		.
	ld b,d			;850a	42		B
	ld b,d			;850b	42		B
	ld b,001h		;850c	06 01		. .
D_850E:
	dec b			;850e	05		.
R_850F:
	ld a,(hl)		;850f	7e		~
	or a			;8510	b7		.
	jr nz,l8515h		;8511	20 02		  .
	ld a,020h		;8513	3e 20		>  
l8515h:
	ld (de),a		;8515	12		.
	inc hl			;8516	23		#
	inc de			;8517	13		.
	djnz R_850F		;8518	10 f5		. .
	ret			;851a	c9		.
R_851B:
	call R_8482		;851b	cd 82 84	. . .
	ld hl,D_8155		;851e	21 55 81	! U .
	ld (M_8576),hl		;8521	22 76 85	" v .
	ld de,01808h		;8524	11 08 18	. . .
	ld bc,01810h		;8527	01 10 18	. . .
	jr l853dh		;852a	18 11		. .
R_852C:
	call R_848A		;852c	cd 8a 84	. . .
	ld hl,D_827C		;852f	21 7c 82	! | .
	ld (M_8576),hl		;8532	22 76 85	" v .
	ld de,00008h		;8535	11 08 00	. . .
	ld bc,00015h		;8538	01 15 00	. . .
	ld a,003h		;853b	3e 03		> .
l853dh:
	ld (M_84BB),a		;853d	32 bb 84	2 . .
	ld a,d			;8540	7a		z
	ld (M_8565),a		;8541	32 65 85	2 e .
	ld a,e			;8544	7b		{
	ld (M_818C),a		;8545	32 8c 81	2 . .
	ld a,c			;8548	79		y
	ld (M_857A),a		;8549	32 7a 85	2 z .
	ld a,b			;854c	78		x
	ld (M_817A),a		;854d	32 7a 81	2 z .
	ld hl,(M_85AE)		;8550	2a ae 85	* . .
	exx			;8553	d9		.
	ld b,015h		;8554	06 15		. .
	ld hl,D_98A0		;8556	21 a0 98	! . .
	ld de,0000fh		;8559	11 0f 00	. . .
l855ch:
	xor a			;855c	af		.
	ld (M_8DCB),a		;855d	32 cb 8d	2 . .
	ld a,(hl)		;8560	7e		~
	inc hl			;8561	23		#
	exx			;8562	d9		.
	ld e,a			;8563	5f		_
	ld a,018h		;8564	3e 18		> .
	ld (M_8597),a		;8566	32 97 85	2 . .
	add a,e			;8569	83		.
	ld e,a			;856a	5f		_
	exx			;856b	d9		.
	ld a,(hl)		;856c	7e		~
	add hl,de		;856d	19		.
	exx			;856e	d9		.
	ld d,a			;856f	57		W
	ld a,(hl)		;8570	7e		~
	or a			;8571	b7		.
	jr z,l8581h		;8572	28 0d		( .
	push hl			;8574	e5		.
	call D_8155		;8575	cd 55 81	. U .
	pop hl			;8578	e1		.
	ld bc,00010h		;8579	01 10 00	. . .
	add hl,bc		;857c	09		.
	exx			;857d	d9		.
	djnz l855ch		;857e	10 dc		. .
	exx			;8580	d9		.
l8581h:
	exx			;8581	d9		.
	ld a,016h		;8582	3e 16		> .
	sub b			;8584	90		.
	ld (M_7329),a		;8585	32 29 73	2 ) s
	exx			;8588	d9		.
	ld (M_85B0),hl		;8589	22 b0 85	" . .
	exx			;858c	d9		.
	ld a,b			;858d	78		x
	or a			;858e	b7		.
	ret z			;858f	c8		.
	sbc hl,de		;8590	ed 52		. R
	dec hl			;8592	2b		+
l8593h:
	ld a,(hl)		;8593	7e		~
	inc hl			;8594	23		#
	exx			;8595	d9		.
	add a,018h		;8596	c6 18		. .
	ld e,a			;8598	5f		_
	exx			;8599	d9		.
	ld a,(hl)		;859a	7e		~
	add hl,de		;859b	19		.
	exx			;859c	d9		.
	ld d,a			;859d	57		W
	ld b,008h		;859e	06 08		. .
	ld hl,05b0fh		;85a0	21 0f 5b	! . [
	call R_8148		;85a3	cd 48 81	. H .
	xor a			;85a6	af		.
	ld (M_8DCB),a		;85a7	32 cb 8d	2 . .
	exx			;85aa	d9		.
	djnz l8593h		;85ab	10 e6		. .
	ret			;85ad	c9		.
M_85AE:
	ld (bc),a		;85ae	02		.
	ret c			;85af	d8		.
M_85B0:
	ld (000d9h),a		;85b0	32 d9 00	2 . .
	nop			;85b3	00		.
M_85B4:
	nop			;85b4	00		.
	nop			;85b5	00		.
R_85B6:
	ld (M_84BB),a		;85b6	32 bb 84	2 . .
	call R_847C		;85b9	cd 7c 84	. | .
R_85BC:
	xor a			;85bc	af		.
R_85BD:
	ld (M_8DCB),a		;85bd	32 cb 8d	2 . .
	jp R_8148		;85c0	c3 48 81	. H .
l85c3h:
	call R_860E		;85c3	cd 0e 86	. . .
	ld de,0c004h		;85c6	11 04 c0	. . .
	add hl,de		;85c9	19		.
l85cah:
	ld d,(hl)		;85ca	56		V
	inc hl			;85cb	23		#
	ld e,(hl)		;85cc	5e		^
	inc hl			;85cd	23		#
	ld a,(hl)		;85ce	7e		~
	inc hl			;85cf	23		#
	push hl			;85d0	e5		.
	ld l,(hl)		;85d1	6e		n
	ld h,a			;85d2	67		g
	ld bc,00800h		;85d3	01 00 08	. . .
	ld (M_803F),hl		;85d6	22 3f 80	" ? .
	ld (08041h),de		;85d9	ed 53 41 80	. S A .
	call R_7FF0		;85dd	cd f0 7f	. . .
	pop hl			;85e0	e1		.
	inc hl			;85e1	23		#
	ld a,(hl)		;85e2	7e		~
	ld de,0fff8h		;85e3	11 f8 ff	. . .
	add hl,de		;85e6	19		.
	push hl			;85e7	e5		.
	ld de,M_7B9A		;85e8	11 9a 7b	. . {
	ld bc,00004h		;85eb	01 04 00	. . .
	ldir			;85ee	ed b0		. .
	pop hl			;85f0	e1		.
	cp 010h			;85f1	fe 10		. .
	ret			;85f3	c9		.
R_85F4:
	ld a,(M_9A02)		;85f4	3a 02 9a	: . .
	or a			;85f7	b7		.
	jr nz,l85c3h		;85f8	20 c9		  .
	call R_860E		;85fa	cd 0e 86	. . .
	jp R_8631		;85fd	c3 31 86	. 1 .
R_8600:
	ld a,010h		;8600	3e 10		> .
	ld (M_8624),a		;8602	32 24 86	2 $ .
	call R_860E		;8605	cd 0e 86	. . .
	ld a,015h		;8608	3e 15		> .
	ld (M_8624),a		;860a	32 24 86	2 $ .
	ret			;860d	c9		.
R_860E:
	call R_848A		;860e	cd 8a 84	. . .
	ld a,(M_8ECE)		;8611	3a ce 8e	: . .
	sub 002h		;8614	d6 02		. .
R_8616:
	ld hl,(M_8F00)		;8616	2a 00 8f	* . .
	ld b,000h		;8619	06 00		. .
	ld c,a			;861b	4f		O
	add hl,bc		;861c	09		.
	ld (M_8F02),hl		;861d	22 02 8f	" . .
	ld a,h			;8620	7c		|
	or l			;8621	b5		.
	ret z			;8622	c8		.
	ld bc,00015h		;8623	01 15 00	. . .
	jp R_7F8B		;8626	c3 8b 7f	. . .
	ld a,(M_9A02)		;8629	3a 02 9a	: . .
	or a			;862c	b7		.
	jr nz,l85cah		;862d	20 9b		  .
	jr l8635h		;862f	18 04		. .
R_8631:
	ld de,0c004h		;8631	11 04 c0	. . .
	add hl,de		;8634	19		.
l8635h:
	ld d,(hl)		;8635	56		V
	inc hl			;8636	23		#
	ld e,(hl)		;8637	5e		^
	inc hl			;8638	23		#
	ld a,(hl)		;8639	7e		~
	inc hl			;863a	23		#
	push hl			;863b	e5		.
D_863C:
	ld l,(hl)		;863c	6e		n
	ld h,a			;863d	67		g
	ld bc,00800h		;863e	01 00 08	. . .
	ld (M_803F),hl		;8641	22 3f 80	" ? .
	ld (08041h),de		;8644	ed 53 41 80	. S A .
	call R_7FF0		;8648	cd f0 7f	. . .
	pop hl			;864b	e1		.
	inc hl			;864c	23		#
	ld a,(hl)		;864d	7e		~
	ld de,0fff8h		;864e	11 f8 ff	. . .
	add hl,de		;8651	19		.
	cp 002h			;8652	fe 02		. .
	ret			;8654	c9		.
R_8655:
	ld hl,04e0bh		;8655	21 0b 4e	! . N
	ld de,04f0bh		;8658	11 0b 4f	. . O
	ld c,0a0h		;865b	0e a0		. .
R_865D:
	ld b,003h		;865d	06 03		. .
l865fh:
	ldi			;865f	ed a0		. .
	ldi			;8661	ed a0		. .
	ldi			;8663	ed a0		. .
	ldi			;8665	ed a0		. .
	ldi			;8667	ed a0		. .
	ldi			;8669	ed a0		. .
	ldi			;866b	ed a0		. .
	ldi			;866d	ed a0		. .
	ldi			;866f	ed a0		. .
	ldi			;8671	ed a0		. .
	dec d			;8673	15		.
	dec h			;8674	25		%
	dec l			;8675	2d		-
	dec e			;8676	1d		.
	ldd			;8677	ed a8		. .
	ldd			;8679	ed a8		. .
	ldd			;867b	ed a8		. .
	ldd			;867d	ed a8		. .
	ldd			;867f	ed a8		. .
	ldd			;8681	ed a8		. .
	ldd			;8683	ed a8		. .
	ldd			;8685	ed a8		. .
	ldd			;8687	ed a8		. .
	ldd			;8689	ed a8		. .
	dec d			;868b	15		.
	dec h			;868c	25		%
	inc l			;868d	2c		,
R_868E:
	inc e			;868e	1c		.
	djnz l865fh		;868f	10 ce		. .
	ldi			;8691	ed a0		. .
	ldi			;8693	ed a0		. .
	ldi			;8695	ed a0		. .
	ldi			;8697	ed a0		. .
	ldi			;8699	ed a0		. .
	ldi			;869b	ed a0		. .
	ldi			;869d	ed a0		. .
	ldi			;869f	ed a0		. .
	ldi			;86a1	ed a0		. .
	ldi			;86a3	ed a0		. .
	ld a,c			;86a5	79		y
	cp 05ah			;86a6	fe 5a		. Z
	ret c			;86a8	d8		.
	ld hl,047ebh		;86a9	21 eb 47	! . G
	ld de,0480bh		;86ac	11 0b 48	. . H
	ldi			;86af	ed a0		. .
	ldi			;86b1	ed a0		. .
	ldi			;86b3	ed a0		. .
	ldi			;86b5	ed a0		. .
	ldi			;86b7	ed a0		. .
	ldi			;86b9	ed a0		. .
	ldi			;86bb	ed a0		. .
	ldi			;86bd	ed a0		. .
	ldi			;86bf	ed a0		. .
	ldi			;86c1	ed a0		. .
	ld hl,046ebh		;86c3	21 eb 46	! . F
	ld de,047ebh		;86c6	11 eb 47	. . G
	jp R_865D		;86c9	c3 5d 86	. ] .
R_86CC:
	ld a,r			;86cc	ed 5f		. _
	and 007h		;86ce	e6 07		. .
	ld b,002h		;86d0	06 02		. .
	ld hl,058ebh		;86d2	21 eb 58	! . X
l86d5h:
	ld (hl),a		;86d5	77		w
	inc l			;86d6	2c		,
	ld (hl),a		;86d7	77		w
	inc l			;86d8	2c		,
	ld (hl),a		;86d9	77		w
	inc l			;86da	2c		,
	ld (hl),a		;86db	77		w
	inc l			;86dc	2c		,
	ld (hl),a		;86dd	77		w
	inc l			;86de	2c		,
	ld (hl),a		;86df	77		w
	inc l			;86e0	2c		,
	ld (hl),a		;86e1	77		w
	inc l			;86e2	2c		,
	ld (hl),a		;86e3	77		w
	inc l			;86e4	2c		,
	ld (hl),a		;86e5	77		w
	inc l			;86e6	2c		,
	ld (hl),a		;86e7	77		w
	inc l			;86e8	2c		,
	ld hl,0590bh		;86e9	21 0b 59	! . Y
	djnz l86d5h		;86ec	10 e7		. .
	ret			;86ee	c9		.
;CALL 86CC; LD A,(TS_MODE_FLAG); OR A; JR Z,skip;
;dual-chip init: OUT #FFFD,#FE / OUT #FFFD,#FF (+ register defaults)
;- installs and initializes BOTH chip engines when TS was detected.
DUAL_CHIP_INIT:
	call R_86CC		;86ef	cd cc 86	. . .
	ld a,(TS_MODE_FLAG)	;86f2	3a 02 6b	: . k
	or a			;86f5	b7		.
	jr z,R_8709		;86f6	28 11		( .
	ld bc,0fffdh		;86f8	01 fd ff	. . .
	ld a,0feh		;86fb	3e fe		> .
	out (c),a		;86fd	ed 79		. y
	call R_8709		;86ff	cd 09 87	. . .
	ld bc,0fffdh		;8702	01 fd ff	. . .
	ld a,0ffh		;8705	3e ff		> .
	out (c),a		;8707	ed 79		. y
R_8709:
	ld a,001h		;8709	3e 01		> .
	call R_87F7		;870b	cd f7 87	. . .
	ld (M_87A2),hl		;870e	22 a2 87	" . .
	ld a,008h		;8711	3e 08		> .
	ld (M_87A1),a		;8713	32 a1 87	2 . .
	ld a,003h		;8716	3e 03		> .
	call R_87F7		;8718	cd f7 87	. . .
	ld (M_87A5),hl		;871b	22 a5 87	" . .
	ld a,009h		;871e	3e 09		> .
	ld (M_87A4),a		;8720	32 a4 87	2 . .
	ld a,005h		;8723	3e 05		> .
	call R_87F7		;8725	cd f7 87	. . .
	ld (M_87A8),hl		;8728	22 a8 87	" . .
	ld a,00ah		;872b	3e 0a		> .
	ld (M_87A7),a		;872d	32 a7 87	2 . .
l8730h:
	ld de,M_87A2		;8730	11 a2 87	. . .
	ld a,003h		;8733	3e 03		> .
l8735h:
	dec a			;8735	3d		=
	jr z,l8755h		;8736	28 1d		( .
	ex de,hl		;8738	eb		.
	ld (M_879D),hl		;8739	22 9d 87	" . .
	ld e,(hl)		;873c	5e		^
	inc hl			;873d	23		#
	ld d,(hl)		;873e	56		V
	inc hl			;873f	23		#
	inc hl			;8740	23		#
	ld (M_879F),hl		;8741	22 9f 87	" . .
	ld c,(hl)		;8744	4e		N
	inc hl			;8745	23		#
	ld b,(hl)		;8746	46		F
D_8747:
	dec hl			;8747	2b		+
	ex de,hl		;8748	eb		.
	or a			;8749	b7		.
	sbc hl,bc		;874a	ed 42		. B
	jr c,l8735h		;874c	38 e7		8 .
	jr z,l8735h		;874e	28 e5		( .
	call R_8787		;8750	cd 87 87	. . .
	jr l8730h		;8753	18 db		. .
l8755h:
	ld hl,D_884A		;8755	21 4a 88	! J .
	ld (M_879D),hl		;8758	22 9d 87	" . .
	ld a,001h		;875b	3e 01		> .
	ld (M_879C),a		;875d	32 9c 87	2 . .
	ld bc,(M_87A2)		;8760	ed 4b a2 87	. K . .
	ld a,(M_87A1)		;8764	3a a1 87	: . .
	ld (ANALYZER_POLL_SET),a	;8767	32 f6 87	2 . .
	call R_87B2		;876a	cd b2 87	. . .
	ld bc,(M_87A5)		;876d	ed 4b a5 87	. K . .
	ld a,(M_87A4)		;8771	3a a4 87	: . .
	ld (ANALYZER_POLL_SET),a	;8774	32 f6 87	2 . .
	call R_87B2		;8777	cd b2 87	. . .
	ld bc,(M_87A8)		;877a	ed 4b a8 87	. K . .
	ld a,(M_87A7)		;877e	3a a7 87	: . .
	ld (ANALYZER_POLL_SET),a	;8781	32 f6 87	2 . .
	jp R_87B2		;8784	c3 b2 87	. . .
R_8787:
	ld hl,(M_879D)		;8787	2a 9d 87	* . .
	ld de,(M_879F)		;878a	ed 5b 9f 87	. [ . .
	dec hl			;878e	2b		+
	dec de			;878f	1b		.
	ld b,003h		;8790	06 03		. .
l8792h:
	ld c,(hl)		;8792	4e		N
	ld a,(de)		;8793	1a		.
	ld (hl),a		;8794	77		w
	ld a,c			;8795	79		y
	ld (de),a		;8796	12		.
	inc hl			;8797	23		#
	inc de			;8798	13		.
	djnz l8792h		;8799	10 f7		. .
	ret			;879b	c9		.
M_879C:
	add hl,de		;879c	19		.
M_879D:
	add a,b			;879d	80		.
	adc a,b			;879e	88		.
M_879F:
	xor b			;879f	a8		.
	add a,a			;87a0	87		.
M_87A1:
	add hl,bc		;87a1	09		.
M_87A2:
	or b			;87a2	b0		.
	nop			;87a3	00		.
M_87A4:
	ex af,af'		;87a4	08		.
M_87A5:
	pop af			;87a5	f1		.
	ld bc,00f0ah		;87a6	01 0a 0f	. . .
	ld (bc),a		;87a9	02		.
	ld b,d			;87aa	42		B
	ld d,l			;87ab	55		U
	ld b,h			;87ac	44		D
	ld b,h			;87ad	44		D
	ld b,l			;87ae	45		E
	ld d,d			;87af	52		R
	ld d,l			;87b0	55		U
	ld e,d			;87b1	5a		Z
R_87B2:
	ld a,b			;87b2	78		x
	or c			;87b3	b1		.
	jr z,l87b7h		;87b4	28 01		( .
	dec bc			;87b6	0b		.
l87b7h:
	ld hl,(M_879D)		;87b7	2a 9d 87	* . .
	ld a,(M_879C)		;87ba	3a 9c 87	: . .
	dec a			;87bd	3d		=
l87beh:
	inc a			;87be	3c		<
	ld e,(hl)		;87bf	5e		^
	inc hl			;87c0	23		#
	ld d,(hl)		;87c1	56		V
	inc hl			;87c2	23		#
	ex de,hl		;87c3	eb		.
	sbc hl,bc		;87c4	ed 42		. B
	ex de,hl		;87c6	eb		.
	jp c,l87beh		;87c7	da be 87	. . .
	ld (M_879D),hl		;87ca	22 9d 87	" . .
	ld (M_879C),a		;87cd	32 9c 87	2 . .
	add a,a			;87d0	87		.
	exx			;87d1	d9		.
	ld l,a			;87d2	6f		o
	exx			;87d3	d9		.
	srl a			;87d4	cb 3f		. ?
	srl a			;87d6	cb 3f		. ?
	srl a			;87d8	cb 3f		. ?
	add a,00bh		;87da	c6 0b		. .
	ld hl,D_98FE		;87dc	21 fe 98	! . .
	push af			;87df	f5		.
	add a,(hl)		;87e0	86		.
	inc hl			;87e1	23		#
	ld d,(hl)		;87e2	56		V
	ld e,a			;87e3	5f		_
	ld bc,0000fh		;87e4	01 0f 00	. . .
	add hl,bc		;87e7	09		.
	pop af			;87e8	f1		.
	add a,(hl)		;87e9	86		.
	inc hl			;87ea	23		#
	ld h,(hl)		;87eb	66		f
	ld l,a			;87ec	6f		o
	exx			;87ed	d9		.
	ld a,l			;87ee	7d		}
	ex af,af'		;87ef	08		.
	ld a,(ANALYZER_POLL_SET)	;87f0	3a f6 87	: . .
	jp R_880C		;87f3	c3 0c 88	. . .
;analyzer polling preamble (register select)
ANALYZER_POLL_SET:
	ld a,(bc)		;87f6	0a		.
R_87F7:
	ld bc,0fffdh		;87f7	01 fd ff	. . .
;LD BC,#FFFD / OUT (C),reg / IN A,(C) - AY register probe used by the
;setup screen to show current chip state. High-traffic in port traces
;because the UI polls it every frame (6054 hits/PC in the capture).
ANALYZER_POLL:
	out (c),a		;87fa	ed 79		. y
	ex af,af'		;87fc	08		.
	in a,(c)		;87fd	ed 78		. x
	and 00fh		;87ff	e6 0f		. .
	ld h,a			;8801	67		g
	ex af,af'		;8802	08		.
	dec a			;8803	3d		=
	out (c),a		;8804	ed 79		. y
	ex af,af'		;8806	08		.
	in a,(c)		;8807	ed 78		. x
	ld l,a			;8809	6f		o
	ex af,af'		;880a	08		.
	ret			;880b	c9		.
R_880C:
	ld bc,0fffdh		;880c	01 fd ff	. . .
	out (c),a		;880f	ed 79		. y
	in a,(c)		;8811	ed 78		. x
	and 00fh		;8813	e6 0f		. .
	ret z			;8815	c8		.
	ex af,af'		;8816	08		.
	and 007h		;8817	e6 07		. .
	ld hl,D_8841		;8819	21 41 88	! A .
	ld e,a			;881c	5f		_
	ld d,000h		;881d	16 00		. .
	add hl,de		;881f	19		.
	ld a,(hl)		;8820	7e		~
	srl a			;8821	cb 3f		. ?
	or (hl)			;8823	b6		.
	exx			;8824	d9		.
	ld c,a			;8825	4f		O
	ex af,af'		;8826	08		.
	ld b,a			;8827	47		G
	push hl			;8828	e5		.
	cp 009h			;8829	fe 09		. .
	jr c,l8839h		;882b	38 0c		8 .
	sub 008h		;882d	d6 08		. .
	ld b,a			;882f	47		G
	ex de,hl		;8830	eb		.
l8831h:
	ld a,c			;8831	79		y
	or (hl)			;8832	b6		.
	ld (hl),a		;8833	77		w
	dec h			;8834	25		%
	djnz l8831h		;8835	10 fa		. .
	ld b,008h		;8837	06 08		. .
l8839h:
	pop hl			;8839	e1		.
l883ah:
	ld a,c			;883a	79		y
	or (hl)			;883b	b6		.
	ld (hl),a		;883c	77		w
	dec h			;883d	25		%
	djnz l883ah		;883e	10 fa		. .
	ret			;8840	c9		.
D_8841:
	add a,b			;8841	80		.
	ld b,b			;8842	40		@
	jr nz,l8855h		;8843	20 10		  .
	ex af,af'		;8845	08		.
	inc b			;8846	04		.
	ld (bc),a		;8847	02		.
	ld bc,01400h		;8848	01 00 14	. . .
	nop			;884b	00		.
	add hl,de		;884c	19		.
	nop			;884d	00		.
	inc e			;884e	1c		.
	nop			;884f	00		.
	ld hl,02800h		;8850	21 00 28	! . (
	nop			;8853	00		.
	dec l			;8854	2d		-
l8855h:
	nop			;8855	00		.
	ld (03800h),a		;8856	32 00 38	2 . 8
	nop			;8859	00		.
	ld b,e			;885a	43		C
	nop			;885b	00		.
	ld c,a			;885c	4f		O
	nop			;885d	00		.
	ld e,c			;885e	59		Y
	nop			;885f	00		.
	ld h,h			;8860	64		d
	nop			;8861	00		.
	ld (hl),b		;8862	70		p
	nop			;8863	00		.
	ld a,(hl)		;8864	7e		~
	nop			;8865	00		.
	adc a,l			;8866	8d		.
	nop			;8867	00		.
	sbc a,a			;8868	9f		.
	nop			;8869	00		.
	or d			;886a	b2		.
	nop			;886b	00		.
	ret z			;886c	c8		.
	nop			;886d	00		.
	ret po			;886e	e0		.
	nop			;886f	00		.
	call m,01b00h		;8870	fc 00 1b	. . .
	ld bc,0013dh		;8873	01 3d 01	. = .
	ld h,h			;8876	64		d
	ld bc,00190h		;8877	01 90 01	. . .
	pop bc			;887a	c1		.
	ld bc,001f8h		;887b	01 f8 01	. . .
	ld (hl),002h		;887e	36 02		6 .
	and c			;8880	a1		.
	ld (bc),a		;8881	02		.
	jr nz,l8887h		;8882	20 03		  .
	or a			;8884	b7		.
	inc bc			;8885	03		.
	ld l,e			;8886	6b		k
l8887h:
	inc b			;8887	04		.
	ld b,c			;8888	41		A
	dec b			;8889	05		.
	ld b,b			;888a	40		@
	ld b,003h		;888b	06 03		. .
	rlca			;888d	07		.
	ld d,a			;888e	57		W
	ex af,af'		;888f	08		.
	ex de,hl		;8890	eb		.
	add hl,bc		;8891	09		.
	ld (0ff0bh),hl		;8892	22 0b ff	" . .
	rst 38h			;8895	ff		.
	xor a			;8896	af		.
l8897h:
	ld l,a			;8897	6f		o
	ld h,0f1h		;8898	26 f1		& .
	ld bc,00700h		;889a	01 00 07	. . .
	push af			;889d	f5		.
l889eh:
	srl a			;889e	cb 3f		. ?
	rr c			;88a0	cb 19		. .
	ld (hl),a		;88a2	77		w
	inc h			;88a3	24		$
	ld (hl),c		;88a4	71		q
	inc h			;88a5	24		$
	djnz l889eh		;88a6	10 f6		. .
	pop af			;88a8	f1		.
	inc a			;88a9	3c		<
	jr nz,l8897h		;88aa	20 eb		  .
R_88AC:
	ld hl,05800h		;88ac	21 00 58	! . X
	ld de,05b8fh		;88af	11 8f 5b	. . [
	ld b,018h		;88b2	06 18		. .
l88b4h:
	push bc			;88b4	c5		.
	ld a,h			;88b5	7c		|
	ld (de),a		;88b6	12		.
	inc de			;88b7	13		.
	ld a,l			;88b8	7d		}
	ld (de),a		;88b9	12		.
	inc de			;88ba	13		.
	ld bc,0001fh		;88bb	01 1f 00	. . .
	add hl,bc		;88be	09		.
	inc hl			;88bf	23		#
	pop bc			;88c0	c1		.
	djnz l88b4h		;88c1	10 f1		. .
	ld hl,D_9880		;88c3	21 80 98	! . .
	ld de,04000h		;88c6	11 00 40	. . @
	ld b,0c0h		;88c9	06 c0		. .
l88cbh:
	ld (hl),e		;88cb	73		s
	inc hl			;88cc	23		#
	ld (hl),d		;88cd	72		r
	inc hl			;88ce	23		#
	inc d			;88cf	14		.
	ld a,d			;88d0	7a		z
	and 007h		;88d1	e6 07		. .
	jp nz,l88e0h		;88d3	c2 e0 88	. . .
	ld a,e			;88d6	7b		{
	add a,020h		;88d7	c6 20		.  
	ld e,a			;88d9	5f		_
	jr c,l88e0h		;88da	38 04		8 .
	ld a,d			;88dc	7a		z
	sub 008h		;88dd	d6 08		. .
	ld d,a			;88df	57		W
l88e0h:
	djnz l88cbh		;88e0	10 e9		. .
	ld hl,D_9880		;88e2	21 80 98	! . .
	ld de,0ff00h		;88e5	11 00 ff	. . .
	ld b,018h		;88e8	06 18		. .
l88eah:
	ld a,(hl)		;88ea	7e		~
	inc hl			;88eb	23		#
	ld (de),a		;88ec	12		.
	inc e			;88ed	1c		.
	ld a,(hl)		;88ee	7e		~
	ld (de),a		;88ef	12		.
	inc e			;88f0	1c		.
	ld a,b			;88f1	78		x
	ld bc,0000fh		;88f2	01 0f 00	. . .
	add hl,bc		;88f5	09		.
	ld b,a			;88f6	47		G
	djnz l88eah		;88f7	10 f1		. .
	ret			;88f9	c9		.
R_88FA:
	ld de,D_6049		;88fa	11 49 60	. I `
	ld hl,D_604A		;88fd	21 4a 60	! J `
	ld b,028h		;8900	06 28		. (
l8902h:
	ld a,(hl)		;8902	7e		~
	inc hl			;8903	23		#
	or a			;8904	b7		.
	jr nz,l8919h		;8905	20 12		  .
	ld a,(hl)		;8907	7e		~
	cp 002h			;8908	fe 02		. .
	jr c,l8915h		;890a	38 09		8 .
l890ch:
	inc hl			;890c	23		#
	inc hl			;890d	23		#
	inc de			;890e	13		.
	inc de			;890f	13		.
	inc de			;8910	13		.
	djnz l8902h		;8911	10 ef		. .
	xor a			;8913	af		.
	ret			;8914	c9		.
l8915h:
	inc a			;8915	3c		<
	ld (M_8EDB),a		;8916	32 db 8e	2 . .
l8919h:
	ld a,(de)		;8919	1a		.
	or a			;891a	b7		.
	jr z,l890ch		;891b	28 ef		( .
	ld a,0f7h		;891d	3e f7		> .
	ex de,hl		;891f	eb		.
	ret			;8920	c9		.
R_8921:
	ei			;8921	fb		.
	halt			;8922	76		v
	call R_8931		;8923	cd 31 89	. 1 .
	jr nz,R_8921		;8926	20 f9		  .
	ret			;8928	c9		.
l8929h:
	ei			;8929	fb		.
	halt			;892a	76		v
	call R_8931		;892b	cd 31 89	. 1 .
	jr z,l8929h		;892e	28 f9		( .
	ret			;8930	c9		.
R_8931:
	xor a			;8931	af		.
	in a,(0feh)		;8932	db fe		. .
	and 01fh		;8934	e6 1f		. .
	cp 01fh			;8936	fe 1f		. .
	ret			;8938	c9		.
	ld bc,D_7FFE		;8939	01 fe 7f	. . .
	in a,(c)		;893c	ed 78		. x
	bit 0,a			;893e	cb 47		. G
	ret			;8940	c9		.
	ld bc,0bffeh		;8941	01 fe bf	. . .
	in a,(c)		;8944	ed 78		. x
	bit 0,a			;8946	cb 47		. G
	ret			;8948	c9		.
	ld bc,0fdfeh		;8949	01 fe fd	. . .
	in a,(c)		;894c	ed 78		. x
	bit 0,a			;894e	cb 47		. G
	ret			;8950	c9		.
	ld bc,0fefeh		;8951	01 fe fe	. . .
	in a,(c)		;8954	ed 78		. x
	bit 0,a			;8956	cb 47		. G
	ret			;8958	c9		.
R_8959:
	push hl			;8959	e5		.
	ld hl,D_604A		;895a	21 4a 60	! J `
	call R_8977		;895d	cd 77 89	. w .
	pop hl			;8960	e1		.
	ret			;8961	c9		.
	ld a,(D_604A)		;8962	3a 4a 60	: J `
	cp 004h			;8965	fe 04		. .
	ret nc			;8967	d0		.
	xor a			;8968	af		.
	ret			;8969	c9		.
	call R_8959		;896a	cd 59 89	. Y .
	jr nz,l8971h		;896d	20 02		  .
	xor a			;896f	af		.
	ret			;8970	c9		.
l8971h:
	call R_89B4		;8971	cd b4 89	. . .
	ret nz			;8974	c0		.
	xor a			;8975	af		.
	ret			;8976	c9		.
R_8977:
	ld a,(hl)		;8977	7e		~
	or a			;8978	b7		.
	jr nz,l8983h		;8979	20 08		  .
	inc hl			;897b	23		#
	ld a,(hl)		;897c	7e		~
	cp 004h			;897d	fe 04		. .
	jr c,l8983h		;897f	38 02		8 .
	xor a			;8981	af		.
	ret			;8982	c9		.
l8983h:
	ld a,00bh		;8983	3e 0b		> .
	or a			;8985	b7		.
	ret			;8986	c9		.
R_8987:
	ld a,(hl)		;8987	7e		~
	ld d,000h		;8988	16 00		. .
	cp 005h			;898a	fe 05		. .
	jr nc,l8995h		;898c	30 07		0 .
	or a			;898e	b7		.
	jr nz,l8993h		;898f	20 02		  .
	xor a			;8991	af		.
	ret			;8992	c9		.
l8993h:
	ld d,0ffh		;8993	16 ff		. .
l8995h:
	inc hl			;8995	23		#
	ld a,(hl)		;8996	7e		~
	inc a			;8997	3c		<
	jr z,l899dh		;8998	28 03		( .
	ld (hl),d		;899a	72		r
	or a			;899b	b7		.
	ret			;899c	c9		.
l899dh:
	ld (hl),a		;899d	77		w
	ld a,038h		;899e	3e 38		> 8
	call R_89B0		;89a0	cd b0 89	. . .
	ld b,005h		;89a3	06 05		. .
	call R_8143		;89a5	cd 43 81	. C .
	ld a,(M_8110)		;89a8	3a 10 81	: . .
	call R_89B0		;89ab	cd b0 89	. . .
	xor a			;89ae	af		.
	ret			;89af	c9		.
R_89B0:
	ld hl,(M_8ECB)		;89b0	2a cb 8e	* . .
	jp (hl)			;89b3	e9		.
R_89B4:
	push hl			;89b4	e5		.
	ld hl,D_60B6		;89b5	21 b6 60	! . `
	call R_8977		;89b8	cd 77 89	. w .
	pop hl			;89bb	e1		.
	ret			;89bc	c9		.
	ld a,(D_60B6)		;89bd	3a b6 60	: . `
	or a			;89c0	b7		.
	ret			;89c1	c9		.
	ld a,07fh		;89c2	3e 7f		> .
	in a,(0feh)		;89c4	db fe		. .
	bit 1,a			;89c6	cb 4f		. O
	ret			;89c8	c9		.
R_89C9:
	ld a,0bfh		;89c9	3e bf		> .
l89cbh:
	in a,(0feh)		;89cb	db fe		. .
	rra			;89cd	1f		.
	ret			;89ce	c9		.
	ld a,07fh		;89cf	3e 7f		> .
	jr l89cbh		;89d1	18 f8		. .
R_89D3:
	ld e,045h		;89d3	1e 45		. E
	jr l8a2dh		;89d5	18 56		. V
R_89D7:
	ld e,048h		;89d7	1e 48		. H
	jr l8a2dh		;89d9	18 52		. R
R_89DB:
	ld e,042h		;89db	1e 42		. B
	jr l8a2dh		;89dd	18 4e		. N
R_89DF:
	ld e,01bh		;89df	1e 1b		. .
	jr l8a2dh		;89e1	18 4a		. J
	ld e,018h		;89e3	1e 18		. .
	jr l8a2dh		;89e5	18 46		. F
	ld e,015h		;89e7	1e 15		. .
	jr l8a2dh		;89e9	18 42		. B
R_89EB:
	ld e,03ch		;89eb	1e 3c		. <
	jr l8a2dh		;89ed	18 3e		. >
R_89EF:
	ld e,069h		;89ef	1e 69		. i
	jr l8a2dh		;89f1	18 3a		. :
R_89F3:
	ld e,06fh		;89f3	1e 6f		. o
	jr l8a2dh		;89f5	18 36		. 6
R_89F7:
	ld e,072h		;89f7	1e 72		. r
	jr l8a2dh		;89f9	18 32		. 2
R_89FB:
	ld e,024h		;89fb	1e 24		. $
	jr l8a2dh		;89fd	18 2e		. .
R_89FF:
	ld e,01eh		;89ff	1e 1e		. .
	jr l8a2dh		;8a01	18 2a		. *
R_8A03:
	ld e,021h		;8a03	1e 21		. !
	jr l8a2dh		;8a05	18 26		. &
R_8A07:
	ld e,00fh		;8a07	1e 0f		. .
	jr l8a2dh		;8a09	18 22		. "
R_8A0B:
	ld e,012h		;8a0b	1e 12		. .
	jr l8a2dh		;8a0d	18 1e		. .
R_8A0F:
	ld e,015h		;8a0f	1e 15		. .
	jr l8a2dh		;8a11	18 1a		. .
R_8A13:
	ld e,018h		;8a13	1e 18		. .
	jr l8a2dh		;8a15	18 16		. .
	ld e,02ah		;8a17	1e 2a		. *
	jr l8a2dh		;8a19	18 12		. .
R_8A1B:
	ld e,075h		;8a1b	1e 75		. u
	jr l8a2dh		;8a1d	18 0e		. .
R_8A1F:
	ld e,006h		;8a1f	1e 06		. .
	jr l8a2dh		;8a21	18 0a		. .
R_8A23:
	ld e,009h		;8a23	1e 09		. .
	jr l8a2dh		;8a25	18 06		. .
R_8A27:
	ld e,00ch		;8a27	1e 0c		. .
	jr l8a2dh		;8a29	18 02		. .
	ld e,030h		;8a2b	1e 30		. 0
l8a2dh:
	push hl			;8a2d	e5		.
	ld hl,D_604A		;8a2e	21 4a 60	! J `
	ld d,000h		;8a31	16 00		. .
	add hl,de		;8a33	19		.
	call R_8987		;8a34	cd 87 89	. . .
	pop hl			;8a37	e1		.
	ret			;8a38	c9		.
R_8A39:
	ld e,05ah		;8a39	1e 5a		. Z
	jr l8a2dh		;8a3b	18 f0		. .
	ld e,04eh		;8a3d	1e 4e		. N
	jr l8a2dh		;8a3f	18 ec		. .
	ld e,04bh		;8a41	1e 4b		. K
	jr l8a2dh		;8a43	18 e8		. .
	ld e,036h		;8a45	1e 36		. 6
	jr l8a2dh		;8a47	18 e4		. .
	ld e,066h		;8a49	1e 66		. f
	jr l8a2dh		;8a4b	18 e0		. .
R_8A4D:
	ld e,063h		;8a4d	1e 63		. c
	jr l8a2dh		;8a4f	18 dc		. .
R_8A51:
	ld e,060h		;8a51	1e 60		. `
	jr l8a2dh		;8a53	18 d8		. .
	ld e,033h		;8a55	1e 33		. 3
	jr l8a2dh		;8a57	18 d4		. .
	ld e,027h		;8a59	1e 27		. '
	jr l8a2dh		;8a5b	18 d0		. .
R_8A5D:
	ld e,003h		;8a5d	1e 03		. .
	jr l8a2dh		;8a5f	18 cc		. .
;Mute both chips: OUT #FFFD,#FE + R8/R9/R10:=0, then OUT #FFFD,#FF +
;R8/R9/R10:=0. In the captured run this is the ONLY code that ever
;touched chip 2 after the failed probe (9 writes total).
MUTE_BOTH:
	ld bc,0fffdh		;8a61	01 fd ff	. . .
	ld a,0feh		;8a64	3e fe		> .
MUTE_SEL_FE:
	out (c),a		;8a66	ed 79		. y
	push bc			;8a68	c5		.
	call R_8A71		;8a69	cd 71 8a	. q .
	pop bc			;8a6c	c1		.
	ld a,0ffh		;8a6d	3e ff		> .
MUTE_SEL_FF:
	out (c),a		;8a6f	ed 79		. y
R_8A71:
	ld bc,0fffdh		;8a71	01 fd ff	. . .
	ld e,b			;8a74	58		X
	ld d,008h		;8a75	16 08		. .
	xor a			;8a77	af		.
	out (c),d		;8a78	ed 51		. Q
	ld b,0bfh		;8a7a	06 bf		. .
	out (c),a		;8a7c	ed 79		. y
	ld b,e			;8a7e	43		C
	inc d			;8a7f	14		.
	out (c),d		;8a80	ed 51		. Q
	ld b,0bfh		;8a82	06 bf		. .
	out (c),a		;8a84	ed 79		. y
	ld b,e			;8a86	43		C
	inc d			;8a87	14		.
	out (c),d		;8a88	ed 51		. Q
	ld b,0bfh		;8a8a	06 bf		. .
	out (c),a		;8a8c	ed 79		. y
	ret			;8a8e	c9		.
	call R_848A		;8a8f	cd 8a 84	. . .
	ld hl,(M_85AE)		;8a92	2a ae 85	* . .
	ld de,0c009h		;8a95	11 09 c0	. . .
	xor a			;8a98	af		.
	sbc hl,de		;8a99	ed 52		. R
	jr z,l8abah		;8a9b	28 1d		( .
	ld (M_831E),a		;8a9d	32 1e 83	2 . .
	ld hl,(M_8F00)		;8aa0	2a 00 8f	* . .
	ld a,h			;8aa3	7c		|
	or a			;8aa4	b7		.
	jr nz,l8aach		;8aa5	20 05		  .
	ld a,l			;8aa7	7d		}
	cp 016h			;8aa8	fe 16		. .
	jr c,l8abah		;8aaa	38 0e		8 .
l8aach:
	ld de,0ffebh		;8aac	11 eb ff	. . .
	add hl,de		;8aaf	19		.
	ex de,hl		;8ab0	eb		.
	ld hl,(M_85AE)		;8ab1	2a ae 85	* . .
	ld bc,0fe47h		;8ab4	01 47 fe	. G .
	add hl,bc		;8ab7	09		.
	jr l8ac5h		;8ab8	18 0b		. .
l8abah:
	ld a,002h		;8aba	3e 02		> .
	ld (M_8ECE),a		;8abc	32 ce 8e	2 . .
	ld de,00000h		;8abf	11 00 00	. . .
	ld hl,0c009h		;8ac2	21 09 c0	! . .
l8ac5h:
	ld (M_85AE),hl		;8ac5	22 ae 85	" . .
	ld (M_8F00),de		;8ac8	ed 53 00 8f	. S . .
	pop hl			;8acc	e1		.
D_8ACD:
	jp R_626B		;8acd	c3 6b 62	. k b
	call R_848A		;8ad0	cd 8a 84	. . .
	ld hl,(M_85B0)		;8ad3	2a b0 85	* . .
	ld a,(hl)		;8ad6	7e		~
	or a			;8ad7	b7		.
	jr z,l8b3bh		;8ad8	28 61		( a
	ld (M_85AE),hl		;8ada	22 ae 85	" . .
	call R_852C		;8add	cd 2c 85	. , .
	ld hl,(M_8F00)		;8ae0	2a 00 8f	* . .
	ld de,00015h		;8ae3	11 15 00	. . .
	add hl,de		;8ae6	19		.
	ld (M_8F00),hl		;8ae7	22 00 8f	" . .
	ld a,(M_7329)		;8aea	3a 29 73	: ) s
	cp 016h			;8aed	fe 16		. .
	jr z,l8af4h		;8aef	28 03		( .
	ld (M_8ECE),a		;8af1	32 ce 8e	2 . .
l8af4h:
	pop hl			;8af4	e1		.
	jp R_626E		;8af5	c3 6e 62	. n b
	call R_8600		;8af8	cd 00 86	. . .
	call R_8482		;8afb	cd 82 84	. . .
	ld de,0d800h		;8afe	11 00 d8	. . .
	add hl,de		;8b01	19		.
	ld a,0b3h		;8b02	3e b3		> .
	cp (hl)			;8b04	be		.
	jr nz,l8b09h		;8b05	20 02		  .
	ld a,020h		;8b07	3e 20		>  
l8b09h:
	ld (hl),a		;8b09	77		w
	inc hl			;8b0a	23		#
	inc hl			;8b0b	23		#
	ld bc,05b4fh		;8b0c	01 4f 5b	. O [
	call R_84BC		;8b0f	cd bc 84	. . .
	call R_8496		;8b12	cd 96 84	. . .
	ld a,(M_8ECE)		;8b15	3a ce 8e	: . .
	add a,a			;8b18	87		.
	ld h,0ffh		;8b19	26 ff		& .
	ld l,a			;8b1b	6f		o
	ld a,(hl)		;8b1c	7e		~
	inc hl			;8b1d	23		#
	ld d,(hl)		;8b1e	56		V
	add a,01dh		;8b1f	c6 1d		. .
	ld e,a			;8b21	5f		_
	ld hl,05b57h		;8b22	21 57 5b	! W [
	ld b,003h		;8b25	06 03		. .
	xor a			;8b27	af		.
	call R_85B6		;8b28	cd b6 85	. . .
	jp R_7320		;8b2b	c3 20 73	.   s
l8b2eh:
	call R_8A0F		;8b2e	cd 0f 8a	. . .
	jp nz,l8abah		;8b31	c2 ba 8a	. . .
	call R_8A13		;8b34	cd 13 8a	. . .
	ret z			;8b37	c8		.
	call R_848A		;8b38	cd 8a 84	. . .
l8b3bh:
	ld hl,0c01eh		;8b3b	21 1e c0	! . .
	ld de,00015h		;8b3e	11 15 00	. . .
	ld bc,0ffffh		;8b41	01 ff ff	. . .
l8b44h:
	inc bc			;8b44	03		.
	ld a,(hl)		;8b45	7e		~
	add hl,de		;8b46	19		.
	or a			;8b47	b7		.
	jr nz,l8b44h		;8b48	20 fa		  .
	ld hl,0ffech		;8b4a	21 ec ff	! . .
	add hl,bc		;8b4d	09		.
	ld a,h			;8b4e	7c		|
	cp 0ffh			;8b4f	fe ff		. .
	jr z,l8b59h		;8b51	28 06		( .
	ld b,h			;8b53	44		D
	ld c,l			;8b54	4d		M
	ld a,016h		;8b55	3e 16		> .
	jr l8b91h		;8b57	18 38		. 8
l8b59h:
	ld bc,00000h		;8b59	01 00 00	. . .
	ld a,016h		;8b5c	3e 16		> .
	add a,l			;8b5e	85		.
	jr l8b91h		;8b5f	18 30		. 0
	ld hl,R_6271		;8b61	21 71 62	! q b
	push hl			;8b64	e5		.
	call R_8959		;8b65	cd 59 89	. Y .
	jr nz,l8b2eh		;8b68	20 c4		  .
	call R_88FA		;8b6a	cd fa 88	. . .
	cp 0f7h			;8b6d	fe f7		. .
	ret nz			;8b6f	c0		.
	ld a,(hl)		;8b70	7e		~
	cp 004h			;8b71	fe 04		. .
	ret c			;8b73	d8		.
	ld (M_8B8B),a		;8b74	32 8b 8b	2 . .
	call R_860E		;8b77	cd 0e 86	. . .
	ld de,0c01eh		;8b7a	11 1e c0	. . .
	add hl,de		;8b7d	19		.
	ld de,00015h		;8b7e	11 15 00	. . .
	ld bc,(M_8F02)		;8b81	ed 4b 02 8f	. K . .
l8b85h:
	ld a,(hl)		;8b85	7e		~
	or a			;8b86	b7		.
	ret z			;8b87	c8		.
	add hl,de		;8b88	19		.
	inc bc			;8b89	03		.
	cp 000h			;8b8a	fe 00		. .
	jp nz,l8b85h		;8b8c	c2 85 8b	. . .
	ld a,002h		;8b8f	3e 02		> .
l8b91h:
	ld (M_8ECE),a		;8b91	32 ce 8e	2 . .
	ld (M_8F00),bc		;8b94	ed 43 00 8f	. C . .
	xor a			;8b98	af		.
	call R_8616		;8b99	cd 16 86	. . .
	ld de,0c009h		;8b9c	11 09 c0	. . .
	add hl,de		;8b9f	19		.
	ld (M_85AE),hl		;8ba0	22 ae 85	" . .
	call R_852C		;8ba3	cd 2c 85	. , .
	call R_8BBB		;8ba6	cd bb 8b	. . .
l8ba9h:
	call R_88FA		;8ba9	cd fa 88	. . .
	ld a,(M_8B8B)		;8bac	3a 8b 8b	: . .
	cp (hl)			;8baf	be		.
	jr z,l8ba9h		;8bb0	28 f7		( .
	pop hl			;8bb2	e1		.
	jp R_6271		;8bb3	c3 71 62	. q b
R_8BB6:
	ld a,(M_8C2A)		;8bb6	3a 2a 8c	: * .
	or a			;8bb9	b7		.
	ret z			;8bba	c8		.
R_8BBB:
	call R_860E		;8bbb	cd 0e 86	. . .
	ld de,0c005h		;8bbe	11 05 c0	. . .
	add hl,de		;8bc1	19		.
	ld b,(hl)		;8bc2	46		F
	inc hl			;8bc3	23		#
	ld c,(hl)		;8bc4	4e		N
	inc hl			;8bc5	23		#
	ld a,(hl)		;8bc6	7e		~
	or a			;8bc7	b7		.
	jr z,l8bcbh		;8bc8	28 01		( .
	inc bc			;8bca	03		.
l8bcbh:
	push hl			;8bcb	e5		.
	ld de,D_8E43		;8bcc	11 43 8e	. C .
	call R_92D6		;8bcf	cd d6 92	. . .
	pop hl			;8bd2	e1		.
	inc hl			;8bd3	23		#
	inc hl			;8bd4	23		#
	ld de,D_8E32		;8bd5	11 32 8e	. 2 .
	ld b,00ch		;8bd8	06 0c		. .
	call R_850F		;8bda	cd 0f 85	. . .
	ld hl,(M_8F00)		;8bdd	2a 00 8f	* . .
	ld bc,(M_8ECE)		;8be0	ed 4b ce 8e	. K . .
	ld b,000h		;8be4	06 00		. .
	dec c			;8be6	0d		.
	dec c			;8be7	0d		.
	add hl,bc		;8be8	09		.
	ld de,D_8E27		;8be9	11 27 8e	. ' .
	call R_92E8		;8bec	cd e8 92	. . .
	ld a,001h		;8bef	3e 01		> .
	ld (M_8C28),a		;8bf1	32 28 8c	2 ( .
	xor a			;8bf4	af		.
	ld (M_8C2A),a		;8bf5	32 2a 8c	2 * .
	ret			;8bf8	c9		.
R_8BF9:
	ld hl,M_8C29		;8bf9	21 29 8c	! ) .
	ld a,(hl)		;8bfc	7e		~
	or a			;8bfd	b7		.
	ret z			;8bfe	c8		.
	dec hl			;8bff	2b		+
D_8C00:
	ld a,(hl)		;8c00	7e		~
	or a			;8c01	b7		.
	ret z			;8c02	c8		.
	xor a			;8c03	af		.
	ld (hl),a		;8c04	77		w
	inc hl			;8c05	23		#
	ld (hl),a		;8c06	77		w
R_8C07:
	ld hl,D_8E32		;8c07	21 32 8e	! 2 .
	ld b,008h		;8c0a	06 08		. .
	ld de,04849h		;8c0c	11 49 48	. I H
	call R_85BD		;8c0f	cd bd 85	. . .
	ld hl,D_8E42		;8c12	21 42 8e	! B .
	ld b,004h		;8c15	06 04		. .
	ld de,04853h		;8c17	11 53 48	. S H
	call R_85BC		;8c1a	cd bc 85	. . .
R_8C1D:
	ld hl,D_8E26		;8c1d	21 26 8e	! & .
	ld b,005h		;8c20	06 05		. .
	ld de,040d2h		;8c22	11 d2 40	. . @
	jp R_85BC		;8c25	c3 bc 85	. . .
M_8C28:
	nop			;8c28	00		.
M_8C29:
	ld bc,03a00h		;8c29	01 00 3a	. . :
	djnz $-111		;8c2c	10 8f		. .
	or a			;8c2e	b7		.
	ret z			;8c2f	c8		.
	pop de			;8c30	d1		.
	pop bc			;8c31	c1		.
	pop hl			;8c32	e1		.
	ld de,D_8CCD		;8c33	11 cd 8c	. . .
	push de			;8c36	d5		.
	jr l8c3fh		;8c37	18 06		. .
sub_8c39h:
	ld a,(M_8F10)		;8c39	3a 10 8f	: . .
	or a			;8c3c	b7		.
	ld a,(hl)		;8c3d	7e		~
	ret z			;8c3e	c8		.
l8c3fh:
	ld a,(M_84BB)		;8c3f	3a bb 84	: . .
	ld (M_933C),a		;8c42	32 3c 93	2 < .
	call BANK_SWITCH	;8c45	cd e0 5f	. . _
	pop de			;8c48	d1		.
	ld (08f0dh),de		;8c49	ed 53 0d 8f	. S . .
	ld a,001h		;8c4d	3e 01		> .
	or a			;8c4f	b7		.
	ret			;8c50	c9		.
R_8C51:
	ld a,(M_84BB)		;8c51	3a bb 84	: . .
	ld (M_933C),a		;8c54	32 3c 93	2 < .
	call BANK_SWITCH	;8c57	cd e0 5f	. . _
	pop hl			;8c5a	e1		.
	xor a			;8c5b	af		.
	ret			;8c5c	c9		.
R_8C5D:
	ex af,af'		;8c5d	08		.
	ld a,(hl)		;8c5e	7e		~
	cp 00dh			;8c5f	fe 0d		. .
	call z,sub_8c39h	;8c61	cc 39 8c	. 9 .
	or a			;8c64	b7		.
	call z,sub_8c39h	;8c65	cc 39 8c	. 9 .
	ld (M_8DCD+1),bc	;8c68	ed 43 ce 8d	. C . .
	ex af,af'		;8c6c	08		.
	push hl			;8c6d	e5		.
	ld hl,M_8DCB		;8c6e	21 cb 8d	! . .
	ld a,(hl)		;8c71	7e		~
	inc (hl)		;8c72	34		4
	cp 001h			;8c73	fe 01		. .
	jr c,l8c9bh		;8c75	38 24		8 $
	jr z,l8cbfh		;8c77	28 46		( F
	cp 002h			;8c79	fe 02		. .
	jr z,l8c86h		;8c7b	28 09		( .
	cp 003h			;8c7d	fe 03		. .
	jr z,l8cc4h		;8c7f	28 43		( C
	ld (hl),000h		;8c81	36 00		6 .
	dec a			;8c83	3d		=
	jr R_8C87		;8c84	18 01		. .
l8c86h:
	add a,a			;8c86	87		.
R_8C87:
	add a,a			;8c87	87		.
	add a,0efh		;8c88	c6 ef		. .
	ld h,a			;8c8a	67		g
	ld a,007h		;8c8b	3e 07		> .
	ld (M_933C),a		;8c8d	32 3c 93	2 < .
	call BANK_SWITCH	;8c90	cd e0 5f	. . _
	push bc			;8c93	c5		.
	ex af,af'		;8c94	08		.
	ld c,a			;8c95	4f		O
	ld b,0e8h		;8c96	06 e8		. .
	jp R_8CFB		;8c98	c3 fb 8c	. . .
l8c9bh:
	pop hl			;8c9b	e1		.
	inc hl			;8c9c	23		#
	ld a,(hl)		;8c9d	7e		~
	cp 00dh			;8c9e	fe 0d		. .
	call z,sub_8c39h	;8ca0	cc 39 8c	. 9 .
	or a			;8ca3	b7		.
	call z,sub_8c39h	;8ca4	cc 39 8c	. 9 .
	push hl			;8ca7	e5		.
	ld c,a			;8ca8	4f		O
	ex af,af'		;8ca9	08		.
	ld h,0e8h		;8caa	26 e8		& .
	ld l,a			;8cac	6f		o
	ld a,007h		;8cad	3e 07		> .
	ld (M_933C),a		;8caf	32 3c 93	2 < .
	call BANK_SWITCH	;8cb2	cd e0 5f	. . _
	push de			;8cb5	d5		.
	call R_8DAC		;8cb6	cd ac 8d	. . .
	pop de			;8cb9	d1		.
	ld a,005h		;8cba	3e 05		> .
	jp R_8C87		;8cbc	c3 87 8c	. . .
l8cbfh:
	ld hl,0fdf3h		;8cbf	21 f3 fd	! . .
	jr l8cc7h		;8cc2	18 03		. .
l8cc4h:
	ld hl,0fbf1h		;8cc4	21 f1 fb	! . .
l8cc7h:
	ld (M_8D0D),hl		;8cc7	22 0d 8d	" . .
	pop hl			;8cca	e1		.
	inc hl			;8ccb	23		#
	ld a,(hl)		;8ccc	7e		~
D_8CCD:
	cp 00dh			;8ccd	fe 0d		. .
	call z,sub_8c39h	;8ccf	cc 39 8c	. 9 .
	ld (M_8DCD),a		;8cd2	32 cd 8d	2 . .
	push hl			;8cd5	e5		.
	push bc			;8cd6	c5		.
	ex af,af'		;8cd7	08		.
	ld b,0e8h		;8cd8	06 e8		. .
	ld c,a			;8cda	4f		O
	ld a,007h		;8cdb	3e 07		> .
	ld (M_933C),a		;8cdd	32 3c 93	2 < .
	call BANK_SWITCH	;8ce0	cd e0 5f	. . _
	ld a,(M_8D0D)		;8ce3	3a 0d 8d	: . .
	ld h,a			;8ce6	67		g
	push de			;8ce7	d5		.
	call R_8D0F		;8ce8	cd 0f 8d	. . .
	pop de			;8ceb	d1		.
	ld a,(M_8DCD)		;8cec	3a cd 8d	: . .
	or a			;8cef	b7		.
	call z,M_8C29+2		;8cf0	cc 2b 8c	. + .
	ex af,af'		;8cf3	08		.
	ld c,a			;8cf4	4f		O
	ld b,0e8h		;8cf5	06 e8		. .
	ld a,(M_8D0E)		;8cf7	3a 0e 8d	: . .
	ld h,a			;8cfa	67		g
R_8CFB:
	ld a,(M_8DCF)		;8cfb	3a cf 8d	: . .
	dec a			;8cfe	3d		=
	jr nz,l8d06h		;8cff	20 05		  .
	call R_8D0F		;8d01	cd 0f 8d	. . .
	jr l8d09h		;8d04	18 03		. .
l8d06h:
	call R_8D46		;8d06	cd 46 8d	. F .
l8d09h:
	pop bc			;8d09	c1		.
	jp R_8C51		;8d0a	c3 51 8c	. Q .
M_8D0D:
	pop af			;8d0d	f1		.
M_8D0E:
	ei			;8d0e	fb		.
R_8D0F:
	ld a,(bc)		;8d0f	0a		.
	ld l,a			;8d10	6f		o
	ld a,(de)		;8d11	1a		.
	or (hl)			;8d12	b6		.
	ld (de),a		;8d13	12		.
	inc b			;8d14	04		.
	inc d			;8d15	14		.
	ld a,(bc)		;8d16	0a		.
	ld l,a			;8d17	6f		o
	ld a,(de)		;8d18	1a		.
	or (hl)			;8d19	b6		.
	ld (de),a		;8d1a	12		.
	inc b			;8d1b	04		.
	inc d			;8d1c	14		.
	ld a,(bc)		;8d1d	0a		.
	ld l,a			;8d1e	6f		o
	ld a,(de)		;8d1f	1a		.
	or (hl)			;8d20	b6		.
	ld (de),a		;8d21	12		.
	inc b			;8d22	04		.
	inc d			;8d23	14		.
	ld a,(bc)		;8d24	0a		.
	ld l,a			;8d25	6f		o
	ld a,(de)		;8d26	1a		.
	or (hl)			;8d27	b6		.
	ld (de),a		;8d28	12		.
	inc b			;8d29	04		.
	inc d			;8d2a	14		.
	ld a,(bc)		;8d2b	0a		.
	ld l,a			;8d2c	6f		o
	ld a,(de)		;8d2d	1a		.
	or (hl)			;8d2e	b6		.
	ld (de),a		;8d2f	12		.
	inc b			;8d30	04		.
	inc d			;8d31	14		.
	ld a,(bc)		;8d32	0a		.
	ld l,a			;8d33	6f		o
	ld a,(de)		;8d34	1a		.
	or (hl)			;8d35	b6		.
	ld (de),a		;8d36	12		.
	inc b			;8d37	04		.
	inc d			;8d38	14		.
	ld a,(bc)		;8d39	0a		.
	ld l,a			;8d3a	6f		o
	ld a,(de)		;8d3b	1a		.
	or (hl)			;8d3c	b6		.
	ld (de),a		;8d3d	12		.
	inc b			;8d3e	04		.
	inc d			;8d3f	14		.
	ld a,(bc)		;8d40	0a		.
	ld l,a			;8d41	6f		o
	ld a,(de)		;8d42	1a		.
	or (hl)			;8d43	b6		.
	ld (de),a		;8d44	12		.
	ret			;8d45	c9		.
R_8D46:
	ld a,(bc)		;8d46	0a		.
	ld l,a			;8d47	6f		o
	ld a,(de)		;8d48	1a		.
	or (hl)			;8d49	b6		.
	ld (de),a		;8d4a	12		.
	inc e			;8d4b	1c		.
	inc h			;8d4c	24		$
	ld a,(hl)		;8d4d	7e		~
	ld (de),a		;8d4e	12		.
	dec e			;8d4f	1d		.
	dec h			;8d50	25		%
	inc b			;8d51	04		.
	inc d			;8d52	14		.
	ld a,(bc)		;8d53	0a		.
	ld l,a			;8d54	6f		o
	ld a,(de)		;8d55	1a		.
	or (hl)			;8d56	b6		.
	ld (de),a		;8d57	12		.
	inc e			;8d58	1c		.
	inc h			;8d59	24		$
	ld a,(hl)		;8d5a	7e		~
	ld (de),a		;8d5b	12		.
	dec e			;8d5c	1d		.
	dec h			;8d5d	25		%
	inc b			;8d5e	04		.
	inc d			;8d5f	14		.
	ld a,(bc)		;8d60	0a		.
	ld l,a			;8d61	6f		o
	ld a,(de)		;8d62	1a		.
	or (hl)			;8d63	b6		.
	ld (de),a		;8d64	12		.
	inc e			;8d65	1c		.
	inc h			;8d66	24		$
	ld a,(hl)		;8d67	7e		~
	ld (de),a		;8d68	12		.
	dec e			;8d69	1d		.
	dec h			;8d6a	25		%
	inc b			;8d6b	04		.
	inc d			;8d6c	14		.
	ld a,(bc)		;8d6d	0a		.
	ld l,a			;8d6e	6f		o
	ld a,(de)		;8d6f	1a		.
	or (hl)			;8d70	b6		.
	ld (de),a		;8d71	12		.
	inc e			;8d72	1c		.
	inc h			;8d73	24		$
	ld a,(hl)		;8d74	7e		~
	ld (de),a		;8d75	12		.
	dec e			;8d76	1d		.
	dec h			;8d77	25		%
	inc b			;8d78	04		.
	inc d			;8d79	14		.
	ld a,(bc)		;8d7a	0a		.
	ld l,a			;8d7b	6f		o
	ld a,(de)		;8d7c	1a		.
	or (hl)			;8d7d	b6		.
	ld (de),a		;8d7e	12		.
	inc e			;8d7f	1c		.
	inc h			;8d80	24		$
	ld a,(hl)		;8d81	7e		~
	ld (de),a		;8d82	12		.
	dec e			;8d83	1d		.
	dec h			;8d84	25		%
	inc b			;8d85	04		.
	inc d			;8d86	14		.
	ld a,(bc)		;8d87	0a		.
	ld l,a			;8d88	6f		o
	ld a,(de)		;8d89	1a		.
	or (hl)			;8d8a	b6		.
	ld (de),a		;8d8b	12		.
	inc e			;8d8c	1c		.
	inc h			;8d8d	24		$
	ld a,(hl)		;8d8e	7e		~
	ld (de),a		;8d8f	12		.
	dec e			;8d90	1d		.
	dec h			;8d91	25		%
	inc b			;8d92	04		.
	inc d			;8d93	14		.
	ld a,(bc)		;8d94	0a		.
	ld l,a			;8d95	6f		o
	ld a,(de)		;8d96	1a		.
	or (hl)			;8d97	b6		.
	ld (de),a		;8d98	12		.
	inc e			;8d99	1c		.
	inc h			;8d9a	24		$
	ld a,(hl)		;8d9b	7e		~
	ld (de),a		;8d9c	12		.
	dec e			;8d9d	1d		.
	dec h			;8d9e	25		%
	inc b			;8d9f	04		.
	inc d			;8da0	14		.
	ld a,(bc)		;8da1	0a		.
	ld l,a			;8da2	6f		o
	ld a,(de)		;8da3	1a		.
	or (hl)			;8da4	b6		.
	ld (de),a		;8da5	12		.
	inc e			;8da6	1c		.
	inc h			;8da7	24		$
	ld a,(hl)		;8da8	7e		~
	ld (de),a		;8da9	12		.
	ret			;8daa	c9		.
	nop			;8dab	00		.
R_8DAC:
	ld a,(hl)		;8dac	7e		~
	ld (de),a		;8dad	12		.
	inc h			;8dae	24		$
	inc d			;8daf	14		.
	ld a,(hl)		;8db0	7e		~
	ld (de),a		;8db1	12		.
	inc h			;8db2	24		$
	inc d			;8db3	14		.
	ld a,(hl)		;8db4	7e		~
	ld (de),a		;8db5	12		.
	inc h			;8db6	24		$
	inc d			;8db7	14		.
	ld a,(hl)		;8db8	7e		~
	ld (de),a		;8db9	12		.
	inc h			;8dba	24		$
	inc d			;8dbb	14		.
	ld a,(hl)		;8dbc	7e		~
	ld (de),a		;8dbd	12		.
	inc h			;8dbe	24		$
	inc d			;8dbf	14		.
	ld a,(hl)		;8dc0	7e		~
	ld (de),a		;8dc1	12		.
	inc h			;8dc2	24		$
	inc d			;8dc3	14		.
	ld a,(hl)		;8dc4	7e		~
	ld (de),a		;8dc5	12		.
	inc h			;8dc6	24		$
	inc d			;8dc7	14		.
	ld a,(hl)		;8dc8	7e		~
	ld (de),a		;8dc9	12		.
	ret			;8dca	c9		.
M_8DCB:
	nop			;8dcb	00		.
	inc b			;8dcc	04		.
M_8DCD:
	jr nc,M_8DCF		;8dcd	30 00		0 .
M_8DCF:
	ld bc,0b021h		;8dcf	01 21 b0	. ! .
	sbc a,b			;8dd2	98		.
R_8DD3:
	ld (M_8E20),de		;8dd3	ed 53 20 8e	. S   .
	ld a,001h		;8dd7	3e 01		> .
	ld (M_8F10),a		;8dd9	32 10 8f	2 . .
l8ddch:
	ld a,(hl)		;8ddc	7e		~
	inc hl			;8ddd	23		#
	ld d,(hl)		;8dde	56		V
	add a,009h		;8ddf	c6 09		. .
	ld e,a			;8de1	5f		_
	push hl			;8de2	e5		.
	ld hl,(M_8E20)		;8de3	2a 20 8e	*   .
	ld a,(hl)		;8de6	7e		~
	cp 00ah			;8de7	fe 0a		. .
	jr nc,l8df8h		;8de9	30 0d		0 .
	or a			;8deb	b7		.
	jr z,l8dfdh		;8dec	28 0f		( .
	ld b,a			;8dee	47		G
	add a,e			;8def	83		.
	ld e,a			;8df0	5f		_
	ld a,00eh		;8df1	3e 0e		> .
	sub b			;8df3	90		.
	ld b,a			;8df4	47		G
	inc hl			;8df5	23		#
	jr l8dfah		;8df6	18 02		. .
l8df8h:
	ld b,00eh		;8df8	06 0e		. .
l8dfah:
	call R_85BC		;8dfa	cd bc 85	. . .
l8dfdh:
	ld hl,(M_8E20)		;8dfd	2a 20 8e	*   .
	ld bc,00017h		;8e00	01 17 00	. . .
	xor a			;8e03	af		.
	cpir			;8e04	ed b1		. .
	ld (M_8E20),hl		;8e06	22 20 8e	"   .
	ld a,(hl)		;8e09	7e		~
	pop hl			;8e0a	e1		.
	cp 00dh			;8e0b	fe 0d		. .
	jr z,l8e15h		;8e0d	28 06		( .
	ld de,0000fh		;8e0f	11 0f 00	. . .
	add hl,de		;8e12	19		.
	jr l8ddch		;8e13	18 c7		. .
l8e15h:
	xor a			;8e15	af		.
	ld (M_8F10),a		;8e16	32 10 8f	2 . .
	ret			;8e19	c9		.
D_8E1A:
; --------------------------------------------------------------------------
; DATA 0x8E1A-0x8F03  file browser UI text (HDD/CD, Len:, Drv:, Fls/Free, Root Dir)
; --------------------------------------------------------------------------
DATA_8E1A:
	defb 48 44 44 43 44 20 9D 8E  ;8E1A  HDDCD ..
	defb 00 00 00 09 20 30 30 30  ;8E22  .... 000
	defb 2F 30 30 30 00 00 00 00  ;8E2A  /000....
	defb 2E 2E 2E 2E 2E 2E 2E 2E  ;8E32  ........
	defb 2E 2E 2E 2E 20 4C 65 6E  ;8E3A  .... Len
	defb 3A 30 30 30 30 30 00 20  ;8E42  :00000. 
	defb 20 20 20 20 20 20 20 41  ;8E4A         A
	defb 3A 62 61 64 61 70 6C 20  ;8E52  :badapl 
	defb 20 2E 6D 20 20 00 58 44  ;8E5A   .m  .XD
	defb 44 3A 20 20 20 20 20 20  ;8E62  D:      
	defb 20 20 20 20 20 00 00 44  ;8E6A       ..D
	defb 72 76 3A 41 20 20 20 5A  ;8E72  rv:A   Z
	defb 58 4D 41 4B 20 20 20 20  ;8E7A  XMAK    
	defb 20 20 00 46 6C 73 20 30  ;8E82    .Fls 0
	defb 31 39 2F 30 30 30 20 20  ;8E8A  19/000  
	defb 46 72 65 65 20 31 35 38  ;8E92  Free 158
	defb 30 20 00 0D 20 20 20 20  ;8E9A  0 ..    
	defb 41 20 2D 20 52 45 54 55  ;8EA2  A - RETU
	defb 52 4E 20 20 45 4E 54 45  ;8EAA  RN  ENTE
	defb 52 20 2D 20 4E 45 58 54  ;8EB2  R - NEXT
	defb 20 20 20 20 01 01 00 01  ;8EBA      ....
	defb 00 8C C8 B0 C8 00 00 00  ;8EC2  ........
	defb 00 ED 80 18 0E 00 02 00  ;8ECA  ........
	defb 00 00 00 02 00 10 01 C0  ;8ED2  ........
	defb 00 00 00 00 01 00 00 00  ;8EDA  ........
	defb 52 6F 6F 74 20 44 69 72  ;8EE2  Root Dir
	defb 20 20 20 20 20 20 20 20  ;8EEA          
	defb 5C 20 20 20 20 20 20 20  ;8EF2  \       
	defb 20 20 DE 5F 00 E1 00 00  ;8EFA    ._....
	defb 0C 00                    ;8F02  ..


; ..........................................................................
; code segment restart 0x8F04 - clean decode boundary after data above
; ..........................................................................
;TurboSound mode: 0 = single chip, 1/2 = dual chip.
;Gates the dual-chip init (86EF) and the per-frame switch (907D).
;Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above.
;player-type / TS state bytes 6B03..6B05 (6B05=1 here)
;JP BANK_SWITCH - bank helper tail-jump thunk
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).

M_8F04:
	nop			;8f04	00		.
	nop			;8f05	00		.
	nop			;8f06	00		.
	nop			;8f07	00		.
	nop			;8f08	00		.
	nop			;8f09	00		.
	nop			;8f0a	00		.
M_8F0B:
	nop			;8f0b	00		.
	nop			;8f0c	00		.
	call 0008ch		;8f0d	cd 8c 00	. . .
M_8F10:
	nop			;8f10	00		.
M_8F11:
	ld l,(hl)		;8f11	6e		n
	ret pe			;8f12	e8		.
M_8F13:
	ld bc,00153h		;8f13	01 53 01	. S .
M_8F16:
	nop			;8f16	00		.
M_8F17:
	nop			;8f17	00		.
M_8F18:
	nop			;8f18	00		.
M_8F19:
	nop			;8f19	00		.
M_8F1A:
	nop			;8f1a	00		.
	nop			;8f1b	00		.
	nop			;8f1c	00		.
	nop			;8f1d	00		.
	nop			;8f1e	00		.
	nop			;8f1f	00		.
M_8F20:
	ld bc,024c3h		;8f20	01 c3 24	. . $
	ld l,e			;8f23	6b		k
R_8F24:
	ld hl,D_8F21		;8f24	21 21 8f	! ! .
	push hl			;8f27	e5		.
	ld hl,041d4h		;8f28	21 d4 41	! . A
	jr l8f3bh		;8f2b	18 0e		. .
	ld a,(M_8ED5)		;8f2d	3a d5 8e	: . .
	or a			;8f30	b7		.
	jr z,l8f38h		;8f31	28 05		( .
	ld hl,040d3h		;8f33	21 d3 40	! . @
	jr l8f3bh		;8f36	18 03		. .
l8f38h:
	ld hl,04021h		;8f38	21 21 40	! ! @
l8f3bh:
	ret			;8f3b	c9		.
	sub c			;8f3c	91		.
	adc a,a			;8f3d	8f		.
	call R_849E		;8f3e	cd 9e 84	. . .
	ld hl,04000h		;8f41	21 00 40	! . @
	ld de,0db00h		;8f44	11 00 db	. . .
	ld bc,01b00h		;8f47	01 00 1b	. . .
	ldir			;8f4a	ed b0		. .
	ld a,018h		;8f4c	3e 18		> .
	ld (M_5FE4),a		;8f4e	32 e4 5f	2 . _
	ld hl,0c000h		;8f51	21 00 c0	! . .
	ld de,04000h		;8f54	11 00 40	. . @
	ld bc,01800h		;8f57	01 00 18	. . .
	ldir			;8f5a	ed b0		. .
	call R_8496		;8f5c	cd 96 84	. . .
	ld hl,0d060h		;8f5f	21 60 d0	! ` .
	ld (M_6B42),hl		;8f62	22 42 6b	" B k
	ld l,080h		;8f65	2e 80		. .
	ld (M_6B4F),hl		;8f67	22 4f 6b	" O k
	ld a,015h		;8f6a	3e 15		> .
	ld (M_968F),a		;8f6c	32 8f 96	2 . .
	ld hl,(M_8FEA)		;8f6f	2a ea 8f	* . .
	ld de,(M_8ECD)		;8f72	ed 5b cd 8e	. [ . .
	ld h,000h		;8f76	26 00		& .
	ld (M_8ECD),hl		;8f78	22 cd 8e	" . .
	ld (M_8FEA),de		;8f7b	ed 53 ea 8f	. S . .
	ld a,0c9h		;8f7f	3e c9		> .
	ld (M_6B24),a		;8f81	32 24 6b	2 $ k
	ld (M_96C0),a		;8f84	32 c0 96	2 . .
	ld hl,049d8h		;8f87	21 d8 49	! . I
	ld (M_96A8),hl		;8f8a	22 a8 96	" . .
	ld (M_96BA),hl		;8f8d	22 ba 96	" . .
	call 04251h		;8f90	cd 51 42	. Q B
	ld hl,D_95C5		;8f93	21 c5 95	! . .
	ld (M_96A8),hl		;8f96	22 a8 96	" . .
	ld (M_96BA),hl		;8f99	22 ba 96	" . .
	xor a			;8f9c	af		.
	ld (M_6B24),a		;8f9d	32 24 6b	2 $ k
	ld (M_96C0),a		;8fa0	32 c0 96	2 . .
	ld hl,(M_8ECD)		;8fa3	2a cd 8e	* . .
	ld de,(M_8FEA)		;8fa6	ed 5b ea 8f	. [ . .
	ld (M_8FEA),hl		;8faa	22 ea 8f	" . .
	ld (M_8ECD),de		;8fad	ed 53 cd 8e	. S . .
	xor a			;8fb1	af		.
	ld (M_8ED6),a		;8fb2	32 d6 8e	2 . .
	ld a,006h		;8fb5	3e 06		> .
	ld (M_968F),a		;8fb7	32 8f 96	2 . .
	ld hl,0400ch		;8fba	21 0c 40	! . @
	ld (M_6B42),hl		;8fbd	22 42 6b	" B k
	ld l,02ch		;8fc0	2e 2c		. ,
	ld (M_6B4F),hl		;8fc2	22 4f 6b	" O k
	call R_849E		;8fc5	cd 9e 84	. . .
	ld hl,0db00h		;8fc8	21 00 db	! . .
	ld de,04000h		;8fcb	11 00 40	. . @
	ld bc,01b00h		;8fce	01 00 1b	. . .
	ldir			;8fd1	ed b0		. .
	ld hl,(M_8F4F)		;8fd3	2a 4f 8f	* O .
	ld a,010h		;8fd6	3e 10		> .
	ld (hl),a		;8fd8	77		w
	ei			;8fd9	fb		.
	halt			;8fda	76		v
	di			;8fdb	f3		.
	call R_8496		;8fdc	cd 96 84	. . .
	call R_88AC		;8fdf	cd ac 88	. . .
	ei			;8fe2	fb		.
	call R_8053		;8fe3	cd 53 80	. S .
	jp R_73E7		;8fe6	c3 e7 73	. . s
	nop			;8fe9	00		.
M_8FEA:
	jr l8fech		;8fea	18 00		. .
l8fech:
	ld (hl),000h		;8fec	36 00		6 .
	inc hl			;8fee	23		#
	ld (hl),001h		;8fef	36 01		6 .
	jp R_903A		;8ff1	c3 3a 90	. : .
l8ff4h:
	inc hl			;8ff4	23		#
	ld a,(hl)		;8ff5	7e		~
	or a			;8ff6	b7		.
	jr z,l9003h		;8ff7	28 0a		( .
	inc a			;8ff9	3c		<
	jr z,l9003h		;8ffa	28 07		( .
	xor a			;8ffc	af		.
	ld (hl),a		;8ffd	77		w
	dec hl			;8ffe	2b		+
	inc a			;8fff	3c		<
D_9000:
	ld (hl),a		;9000	77		w
	jr l900ah		;9001	18 07		. .
l9003h:
	dec hl			;9003	2b		+
	ld a,(hl)		;9004	7e		~
	cp 0feh			;9005	fe fe		. .
	jr nc,l900ah		;9007	30 01		0 .
	inc (hl)		;9009	34		4
l900ah:
	inc hl			;900a	23		#
	jp R_903A		;900b	c3 3a 90	. : .
D_900E:
	push af			;900e	f5		.
	push bc			;900f	c5		.
	push de			;9010	d5		.
	push hl			;9011	e5		.
	push ix			;9012	dd e5		. .
	exx			;9014	d9		.
	ex af,af'		;9015	08		.
	push af			;9016	f5		.
	push bc			;9017	c5		.
	push de			;9018	d5		.
	push hl			;9019	e5		.
	ld hl,D_9A01		;901a	21 01 9a	! . .
	inc (hl)		;901d	34		4
	ld hl,D_604A		;901e	21 4a 60	! J `
	ld de,D_6040		;9021	11 40 60	. @ `
l9024h:
	ld b,005h		;9024	06 05		. .
	ld a,(de)		;9026	1a		.
	in a,(0feh)		;9027	db fe		. .
	ld c,a			;9029	4f		O
l902ah:
	rr c			;902a	cb 19		. .
	jr nc,l8ff4h		;902c	30 c6		0 .
	ld a,(hl)		;902e	7e		~
	or a			;902f	b7		.
	jr nz,l8fech		;9030	20 ba		  .
	inc hl			;9032	23		#
	ld a,(hl)		;9033	7e		~
	cp 0feh			;9034	fe fe		. .
	jp nc,R_903A		;9036	d2 3a 90	. : .
	inc (hl)		;9039	34		4
R_903A:
	inc hl			;903a	23		#
	inc hl			;903b	23		#
	djnz l902ah		;903c	10 ec		. .
	inc de			;903e	13		.
	ld a,(de)		;903f	1a		.
	or a			;9040	b7		.
	jp nz,l9024h		;9041	c2 24 90	. $ .
	ld hl,058e9h		;9044	21 e9 58	! . X
	ld (hl),a		;9047	77		w
	inc l			;9048	2c		,
	ld (hl),a		;9049	77		w
	ld l,0f5h		;904a	2e f5		. .
	ld (hl),a		;904c	77		w
	inc l			;904d	2c		,
	ld (hl),a		;904e	77		w
	ld a,(TS_STATE)		;904f	3a 03 6b	: . k
	or a			;9052	b7		.
	jr nz,l90b3h		;9053	20 5e		  ^
	ld bc,(M_8EC1)		;9055	ed 4b c1 8e	. K . .
	ld a,(M_8F19)		;9059	3a 19 8f	: . .
	or c			;905c	b1		.
	jr z,l90b3h		;905d	28 54		( T
	ld a,c			;905f	79		y
;Interrupt entry. Runs the TS switch block every frame when
;TS_MODE_FLAG != 0: bank-switch + OUT #FFFD,#FE + CALL engine1 +
;OUT #FFFD,#FF + CALL engine2 (engine copies live at 0xC005 inside
;their RAM banks - only reachable while the right bank is paged).
INT_HANDLER:
	or a			;9060	b7		.
	jr z,l90cch		;9061	28 69		( i
	ld a,b			;9063	78		x
	or a			;9064	b7		.
	jr nz,l90b3h		;9065	20 4c		  L
	ld hl,(M_90DA)		;9067	2a da 90	* . .
	inc hl			;906a	23		#
	ld (M_90DA),hl		;906b	22 da 90	" . .
	ld hl,(M_90DC)		;906e	2a dc 90	* . .
	inc hl			;9071	23		#
	ld (M_90DC),hl		;9072	22 dc 90	" . .
	ld a,001h		;9075	3e 01		> .
	call R_8474		;9077	cd 74 84	. t .
	call 0c005h		;907a	cd 05 c0	. . .
;LD A,(TS_MODE_FLAG); OR A; JR Z,single - the per-frame play gate.
;Dual path: #FE select at 9091, #FF select at 909B (per port trace).
PER_FRAME_PLAY:
	ld a,(TS_MODE_FLAG)	;907d	3a 02 6b	: . k
	or a			;9080	b7		.
	jr z,l90ach		;9081	28 29		( )
	cp 001h			;9083	fe 01		. .
	jr nz,l909dh		;9085	20 16		  .
	ld a,00fh		;9087	3e 0f		> .
	call BANK_SWITCH_TAIL	;9089	cd 7f 84	. . .
	ld bc,0fffdh		;908c	01 fd ff	. . .
	ld a,0feh		;908f	3e fe		> .
SEL_FE_PLAY:
	out (c),a		;9091	ed 79		. y
	call 0c005h		;9093	cd 05 c0	. . .
	ld bc,0fffdh		;9096	01 fd ff	. . .
	ld a,0ffh		;9099	3e ff		> .
SEL_FF_PLAY:
	out (c),a		;909b	ed 79		. y
l909dh:
	ld a,r			;909d	ed 5f		. _
	and 047h		;909f	e6 47		. G
	ld hl,058e9h		;90a1	21 e9 58	! . X
	ld (hl),a		;90a4	77		w
	inc l			;90a5	2c		,
	ld (hl),a		;90a6	77		w
	ld l,0f5h		;90a7	2e f5		. .
	ld (hl),a		;90a9	77		w
	inc l			;90aa	2c		,
	ld (hl),a		;90ab	77		w
l90ach:
	call R_846F		;90ac	cd 6f 84	. o .
	ld a,r			;90af	ed 5f		. _
	and 007h		;90b1	e6 07		. .
l90b3h:
	ld b,00eh		;90b3	06 0e		. .
	ld hl,05969h		;90b5	21 69 59	! i Y
l90b8h:
	ld (hl),a		;90b8	77		w
	inc l			;90b9	2c		,
	djnz l90b8h		;90ba	10 fc		. .
	ld a,(M_933C)		;90bc	3a 3c 93	: < .
	and 007h		;90bf	e6 07		. .
	cp 007h			;90c1	fe 07		. .
	jr nz,l90cch		;90c3	20 07		  .
	ld a,(M_8ED6)		;90c5	3a d6 8e	: . .
	or a			;90c8	b7		.
	call nz,sub_9684h	;90c9	c4 84 96	. . .
l90cch:
	pop hl			;90cc	e1		.
	pop de			;90cd	d1		.
	pop bc			;90ce	c1		.
	pop af			;90cf	f1		.
	exx			;90d0	d9		.
	ex af,af'		;90d1	08		.
	pop ix			;90d2	dd e1		. .
	pop hl			;90d4	e1		.
	pop de			;90d5	d1		.
	pop bc			;90d6	c1		.
	pop af			;90d7	f1		.
	ei			;90d8	fb		.
	ret			;90d9	c9		.
M_90DA:
	call m,0fc05h		;90da	fc 05 fc	. . .
	dec b			;90dd	05		.
R_90DE:
	ld a,006h		;90de	3e 06		> .
	out (0feh),a		;90e0	d3 fe		. .
	call R_8A07		;90e2	cd 07 8a	. . .
	call nz,R_766A		;90e5	c4 6a 76	. j v
	call R_8A0B		;90e8	cd 0b 8a	. . .
	call nz,R_7690		;90eb	c4 90 76	. . v
	ld hl,D_972B		;90ee	21 2b 97	! + .
	call R_9A6E		;90f1	cd 6e 9a	. n .
	ld hl,00000h		;90f4	21 00 00	! . .
	ld (M_96EC),hl		;90f7	22 ec 96	" . .
	xor a			;90fa	af		.
	out (0feh),a		;90fb	d3 fe		. .
	call R_9A74		;90fd	cd 74 9a	. t .
	call R_9A7A		;9100	cd 7a 9a	. z .
	jr c,R_90DE		;9103	38 d9		8 .
	ld hl,VTII_INSTANCE	;9105	21 00 a0	! . .
	ld bc,00320h		;9108	01 20 03	.   .
	call R_9A83		;910b	cd 83 9a	. . .
	call R_9A74		;910e	cd 74 9a	. t .
	call R_9A7A		;9111	cd 7a 9a	. z .
	jr c,R_90DE		;9114	38 c8		8 .
	ld a,(M_A005)		;9116	3a 05 a0	: . .
	and 004h		;9119	e6 04		. .
	cp 004h			;911b	fe 04		. .
	ret z			;911d	c8		.
	ld a,001h		;911e	3e 01		> .
	ld (M_8ED6),a		;9120	32 d6 8e	2 . .
	or a			;9123	b7		.
	ret			;9124	c9		.
R_9125:
	xor a			;9125	af		.
	ret			;9126	c9		.
M_9127:
	nop			;9127	00		.
M_9128:
	nop			;9128	00		.
R_9129:
	xor a			;9129	af		.
	ld (M_9343),a		;912a	32 43 93	2 C .
	ld (M_9128),a		;912d	32 28 91	2 ( .
	ld a,0feh		;9130	3e fe		> .
	ld (M_9127),a		;9132	32 27 91	2 ' .
	ld a,(M_9341)		;9135	3a 41 93	: A .
	and 003h		;9138	e6 03		. .
	ld c,001h		;913a	0e 01		. .
	call R_9168		;913c	cd 68 91	. h .
	ld a,(M_9128)		;913f	3a 28 91	: ( .
	ld (M_9127),a		;9142	32 27 91	2 ' .
	or a			;9145	b7		.
	jr nz,l9157h		;9146	20 0f		  .
	ld hl,VTII_INSTANCE	;9148	21 00 a0	! . .
	ld bc,00900h		;914b	01 00 09	. . .
	call R_929B		;914e	cd 9b 92	. . .
l9151h:
	ld a,(M_9345)		;9151	3a 45 93	: E .
	ld c,a			;9154	4f		O
	or a			;9155	b7		.
	ret			;9156	c9		.
l9157h:
	ld (M_9345),a		;9157	32 45 93	2 E .
	ld hl,VTII_INSTANCE	;915a	21 00 a0	! . .
	ld de,D_A001		;915d	11 01 a0	. . .
	ld bc,000ffh		;9160	01 ff 00	. . .
	ld (hl),b		;9163	70		p
	ldir			;9164	ed b0		. .
	jr l9151h		;9166	18 e9		. .
R_9168:
	ex af,af'		;9168	08		.
	di			;9169	f3		.
	ld a,03fh		;916a	3e 3f		> ?
	ld i,a			;916c	ed 47		. G
	im 1			;916e	ed 56		. V
	ex af,af'		;9170	08		.
	ld (M_9239),hl		;9171	22 39 92	" 9 .
	ld (l923bh),de		;9174	ed 53 3b 92	. S ; .
	ld (l923dh),bc		;9178	ed 43 3d 92	. C = .
	ld (M_923F),a		;917c	32 3f 92	2 ? .
	call R_9192		;917f	cd 92 91	. . .
	ld hl,D_900E		;9182	21 0e 90	! . .
	ld (05bffh),hl		;9185	22 ff 5b	" . [
	di			;9188	f3		.
	ld a,(M_9240)		;9189	3a 40 92	: @ .
	ld i,a			;918c	ed 47		. G
	im 2			;918e	ed 5e		. ^
	ei			;9190	fb		.
	ret			;9191	c9		.
R_9192:
	ld (0919dh),sp		;9192	ed 73 9d 91	. s . .
	call R_91C9		;9196	cd c9 91	. . .
	call 03d13h		;9199	cd 13 3d	. . =
	ld sp,D_5FD2		;919c	31 d2 5f	1 . _
	ld a,(05d0fh)		;919f	3a 0f 5d	: . ]
	or a			;91a2	b7		.
	ret z			;91a3	c8		.
	cp 007h			;91a4	fe 07		. .
	jr nz,l91ach		;91a6	20 04		  .
	ld (M_9345),a		;91a8	32 45 93	2 E .
	ret			;91ab	c9		.
l91ach:
	cp 006h			;91ac	fe 06		. .
	jr nz,l91b9h		;91ae	20 09		  .
	ld (M_9128),a		;91b0	32 28 91	2 ( .
	ld a,(M_9127)		;91b3	3a 27 91	: ' .
	cp 0feh			;91b6	fe fe		. .
	ret z			;91b8	c8		.
l91b9h:
	ld hl,(M_9239)		;91b9	2a 39 92	* 9 .
	ld de,(l923bh)		;91bc	ed 5b 3b 92	. [ ; .
	ld bc,(l923dh)		;91c0	ed 4b 3d 92	. K = .
	ld a,(M_923F)		;91c4	3a 3f 92	: ? .
	jr R_9192		;91c7	18 c9		. .
R_91C9:
	di			;91c9	f3		.
	ld (05c3dh),sp		;91ca	ed 73 3d 5c	. s = \
	ex af,af'		;91ce	08		.
	xor a			;91cf	af		.
	ld (05d0fh),a		;91d0	32 0f 5d	2 . ]
	ld (05d10h),a		;91d3	32 10 5d	2 . ]
	push hl			;91d6	e5		.
	ld a,0c3h		;91d7	3e c3		> .
	ld (05cc2h),a		;91d9	32 c2 5c	2 . \
	ld hl,D_91E5		;91dc	21 e5 91	! . .
	ld (05cc3h),hl		;91df	22 c3 5c	" . \
	pop hl			;91e2	e1		.
	ex af,af'		;91e3	08		.
	ret			;91e4	c9		.
D_91E5:
	di			;91e5	f3		.
	pop de			;91e6	d1		.
	ld hl,00d6bh		;91e7	21 6b 0d	! k .
	or a			;91ea	b7		.
	sbc hl,de		;91eb	ed 52		. R
	ret z			;91ed	c8		.
	ld hl,00010h		;91ee	21 10 00	! . .
	or a			;91f1	b7		.
	sbc hl,de		;91f2	ed 52		. R
	ret z			;91f4	c8		.
	ld hl,01a1bh		;91f5	21 1b 1a	! . .
	or a			;91f8	b7		.
	sbc hl,de		;91f9	ed 52		. R
	ret z			;91fb	c8		.
	ld hl,0028eh		;91fc	21 8e 02	! . .
	or a			;91ff	b7		.
	sbc hl,de		;9200	ed 52		. R
	jr z,l9225h		;9202	28 21		( !
	ld hl,0031eh		;9204	21 1e 03	! . .
	or a			;9207	b7		.
	sbc hl,de		;9208	ed 52		. R
	jr z,l9225h		;920a	28 19		( .
	ld hl,00333h		;920c	21 33 03	! 3 .
	or a			;920f	b7		.
	sbc hl,de		;9210	ed 52		. R
	jr z,l9236h		;9212	28 22		( "
	ld hl,01b7bh		;9214	21 7b 1b	! { .
	or a			;9217	b7		.
	sbc hl,de		;9218	ed 52		. R
	jr z,l922ah		;921a	28 0e		( .
	push de			;921c	d5		.
	ld hl,(05d02h)		;921d	2a 02 5d	* . ]
	ld de,(05d04h)		;9220	ed 5b 04 5d	. [ . ]
	ret			;9224	c9		.
l9225h:
	ld a,001h		;9225	3e 01		> .
	add a,0ffh		;9227	c6 ff		. .
	ret			;9229	c9		.
l922ah:
	ld sp,(05d13h)		;922a	ed 7b 13 5d	. { . ]
	ld bc,00014h		;922e	01 14 00	. . .
	ld a,c			;9231	79		y
	ld (05d0fh),a		;9232	32 0f 5d	2 . ]
	ret			;9235	c9		.
l9236h:
	ld a,041h		;9236	3e 41		> A
	ret			;9238	c9		.
M_9239:
	nop			;9239	00		.
	and b			;923a	a0		.
l923bh:
	rlca			;923b	07		.
	daa			;923c	27		'
l923dh:
	dec b			;923d	05		.
	ld d,h			;923e	54		T
M_923F:
	ld d,h			;923f	54		T
M_9240:
	ld e,e			;9240	5b		[
R_9241:
	di			;9241	f3		.
	push af			;9242	f5		.
	xor a			;9243	af		.
	ld (M_9345),a		;9244	32 45 93	2 E .
	ld a,(M_9343)		;9247	3a 43 93	: C .
	ld (M_9344),a		;924a	32 44 93	2 D .
	ld b,(hl)		;924d	46		F
	inc hl			;924e	23		#
	ld a,(hl)		;924f	7e		~
	inc hl			;9250	23		#
	and 00fh		;9251	e6 0f		. .
	ld c,a			;9253	4f		O
	ld a,(hl)		;9254	7e		~
	ld (M_9343),a		;9255	32 43 93	2 C .
	ex de,hl		;9258	eb		.
	pop de			;9259	d1		.
	push af			;925a	f5		.
	ld a,d			;925b	7a		z
	call R_847C		;925c	cd 7c 84	. | .
	pop af			;925f	f1		.
	ld d,a			;9260	57		W
	ld e,c			;9261	59		Y
	ld a,b			;9262	78		x
	cp 081h			;9263	fe 81		. .
	jr c,l9269h		;9265	38 02		8 .
	ld b,080h		;9267	06 80		. .
l9269h:
	ld c,005h		;9269	0e 05		. .
	push bc			;926b	c5		.
	push hl			;926c	e5		.
	push de			;926d	d5		.
	call R_9168		;926e	cd 68 91	. h .
	pop de			;9271	d1		.
	pop hl			;9272	e1		.
	pop bc			;9273	c1		.
	ld c,000h		;9274	0e 00		. .
	add hl,bc		;9276	09		.
	ld c,e			;9277	4b		K
	ld a,(05cf4h)		;9278	3a f4 5c	: . \
	ld (M_928A),a		;927b	32 8a 92	2 . .
	ld a,(05cf5h)		;927e	3a f5 5c	: . \
	ld d,a			;9281	57		W
	ld a,(M_9344)		;9282	3a 44 93	: D .
	ld (M_9343),a		;9285	32 43 93	2 C .
	xor a			;9288	af		.
	ret			;9289	c9		.
M_928A:
	dec bc			;928a	0b		.
	ex af,af'		;928b	08		.
	ld a,006h		;928c	3e 06		> .
	ld (M_926A),a		;928e	32 6a 92	2 j .
	ex af,af'		;9291	08		.
	call R_9241		;9292	cd 41 92	. A .
	ld a,005h		;9295	3e 05		> .
	ld (M_926A),a		;9297	32 6a 92	2 j .
	ret			;929a	c9		.
R_929B:
	xor a			;929b	af		.
	ld (M_9345),a		;929c	32 45 93	2 E .
	call R_92A4		;929f	cd a4 92	. . .
	ei			;92a2	fb		.
	ret			;92a3	c9		.
R_92A4:
	ld a,(M_9343)		;92a4	3a 43 93	: C .
	ld d,a			;92a7	57		W
	ld e,c			;92a8	59		Y
	ld c,005h		;92a9	0e 05		. .
	push hl			;92ab	e5		.
	push bc			;92ac	c5		.
	call R_9168		;92ad	cd 68 91	. h .
	pop hl			;92b0	e1		.
	pop de			;92b1	d1		.
	ld l,000h		;92b2	2e 00		. .
	add hl,de		;92b4	19		.
	ret			;92b5	c9		.
	ld a,(M_9343)		;92b6	3a 43 93	: C .
	ld d,a			;92b9	57		W
	ld e,c			;92ba	59		Y
	ld c,006h		;92bb	0e 06		. .
	push hl			;92bd	e5		.
	push bc			;92be	c5		.
	call R_9168		;92bf	cd 68 91	. h .
	pop hl			;92c2	e1		.
	pop de			;92c3	d1		.
	ld l,000h		;92c4	2e 00		. .
	add hl,de		;92c6	19		.
	ret			;92c7	c9		.
	ld hl,05b0fh		;92c8	21 0f 5b	! . [
	ld de,050e0h		;92cb	11 e0 50	. . P
	ld b,00dh		;92ce	06 0d		. .
	ld a,(M_933C)		;92d0	3a 3c 93	: < .
	jp R_85B6		;92d3	c3 b6 85	. . .
R_92D6:
	ld h,b			;92d6	60		`
	ld l,c			;92d7	69		i
	ld bc,02710h		;92d8	01 10 27	. . '
	call R_9309		;92db	cd 09 93	. . .
	ld (de),a		;92de	12		.
	inc de			;92df	13		.
R_92E0:
	ld bc,003e8h		;92e0	01 e8 03	. . .
	call R_9309		;92e3	cd 09 93	. . .
	ld (de),a		;92e6	12		.
	inc de			;92e7	13		.
R_92E8:
	ld bc,00064h		;92e8	01 64 00	. d .
	call R_9309		;92eb	cd 09 93	. . .
	ld (de),a		;92ee	12		.
	inc de			;92ef	13		.
l92f0h:
	ld c,00ah		;92f0	0e 0a		. .
	call R_9309		;92f2	cd 09 93	. . .
	ld (de),a		;92f5	12		.
	inc de			;92f6	13		.
	ld c,001h		;92f7	0e 01		. .
	call R_9309		;92f9	cd 09 93	. . .
	ld (de),a		;92fc	12		.
	ret			;92fd	c9		.
R_92FE:
	ld h,000h		;92fe	26 00		& .
	ld l,a			;9300	6f		o
	jr R_92E8		;9301	18 e5		. .
R_9303:
	ld b,000h		;9303	06 00		. .
	ld h,b			;9305	60		`
	ld l,a			;9306	6f		o
	jr l92f0h		;9307	18 e7		. .
R_9309:
	ld a,0ffh		;9309	3e ff		> .
	or a			;930b	b7		.
l930ch:
	inc a			;930c	3c		<
	sbc hl,bc		;930d	ed 42		. B
	jp nc,l930ch		;930f	d2 0c 93	. . .
	add hl,bc		;9312	09		.
	add a,030h		;9313	c6 30		. 0
	ret			;9315	c9		.
R_9316:
	ld de,0ffffh		;9316	11 ff ff	. . .
	or a			;9319	b7		.
l931ah:
	inc de			;931a	13		.
	sbc hl,bc		;931b	ed 42		. B
	jp nc,l931ah		;931d	d2 1a 93	. . .
	add hl,bc		;9320	09		.
	ret			;9321	c9		.
	nop			;9322	00		.
	nop			;9323	00		.
	nop			;9324	00		.
	nop			;9325	00		.
	nop			;9326	00		.
	nop			;9327	00		.
	nop			;9328	00		.
	nop			;9329	00		.
	nop			;932a	00		.
	nop			;932b	00		.
	nop			;932c	00		.
	nop			;932d	00		.
	nop			;932e	00		.
	nop			;932f	00		.
	nop			;9330	00		.
	nop			;9331	00		.
	nop			;9332	00		.
	nop			;9333	00		.
	nop			;9334	00		.
	nop			;9335	00		.
	nop			;9336	00		.
	nop			;9337	00		.
	nop			;9338	00		.
	nop			;9339	00		.
	nop			;933a	00		.
	nop			;933b	00		.
M_933C:
	rlca			;933c	07		.
	nop			;933d	00		.
	nop			;933e	00		.
	ld bc,00001h		;933f	01 01 00	. . .
	nop			;9342	00		.
M_9343:
	nop			;9343	00		.
M_9344:
	nop			;9344	00		.
M_9345:
	nop			;9345	00		.
l9346h:
	ld a,001h		;9346	3e 01		> .
	ld (M_934E),a		;9348	32 4e 93	2 N .
	jp R_766A		;934b	c3 6a 76	. j v
M_934E:
	nop			;934e	00		.
	pop hl			;934f	e1		.
l9350h:
	call R_8496		;9350	cd 96 84	. . .
	ei			;9353	fb		.
	halt			;9354	76		v
	di			;9355	f3		.
	ld hl,D_972B		;9356	21 2b 97	! + .
	call R_9A6E		;9359	cd 6e 9a	. n .
	call R_9A74		;935c	cd 74 9a	. t .
	call R_9A7A		;935f	cd 7a 9a	. z .
	jr nc,l9371h		;9362	30 0d		0 .
	ld a,004h		;9364	3e 04		> .
	call R_966A		;9366	cd 6a 96	. j .
	ld hl,00000h		;9369	21 00 00	! . .
	ld (M_96EC),hl		;936c	22 ec 96	" . .
	jr l9350h		;936f	18 df		. .
l9371h:
	call R_9A77		;9371	cd 77 9a	. w .
	ld hl,D_9E00		;9374	21 00 9e	! . .
	ld bc,00320h		;9377	01 20 03	.   .
	call R_9A83		;937a	cd 83 9a	. . .
	call R_9A71		;937d	cd 71 9a	. q .
	ld a,(M_96F9)		;9380	3a f9 96	: . .
	cp 003h			;9383	fe 03		. .
	jr z,l938ch		;9385	28 05		( .
	ld a,(M_9E02)		;9387	3a 02 9e	: . .
	jr l9393h		;938a	18 07		. .
l938ch:
	ld a,(M_9E03)		;938c	3a 03 9e	: . .
	ld b,a			;938f	47		G
	call R_9628		;9390	cd 28 96	. ( .
l9393h:
	ld (M_93D5),a		;9393	32 d5 93	2 . .
R_9396:
	ld a,001h		;9396	3e 01		> .
	ld (M_96F5),a		;9398	32 f5 96	2 . .
	call R_964D		;939b	cd 4d 96	. M .
	ld hl,0e600h		;939e	21 00 e6	! . .
	ld bc,00002h		;93a1	01 02 00	. . .
	call D_95C5		;93a4	cd c5 95	. . .
	ld hl,0e610h		;93a7	21 10 e6	! . .
	ld bc,00005h		;93aa	01 05 00	. . .
	call D_95C5		;93ad	cd c5 95	. . .
	ld a,(M_93D5)		;93b0	3a d5 93	: . .
	ld de,D_96F2		;93b3	11 f2 96	. . .
	call R_9303		;93b6	cd 03 93	. . .
	ld a,(D_96F2)		;93b9	3a f2 96	: . .
	call R_95CE		;93bc	cd ce 95	. . .
	ld bc,0001eh		;93bf	01 1e 00	. . .
	call D_95C5		;93c2	cd c5 95	. . .
	ld a,(M_96F3)		;93c5	3a f3 96	: . .
	call R_95CE		;93c8	cd ce 95	. . .
	ld bc,0001fh		;93cb	01 1f 00	. . .
	call D_95C5		;93ce	cd c5 95	. . .
	call R_9A71		;93d1	cd 71 9a	. q .
	ld b,001h		;93d4	06 01		. .
	call R_9567		;93d6	cd 67 95	. g .
	ld hl,D_96FE		;93d9	21 fe 96	! . .
	call R_9A6E		;93dc	cd 6e 9a	. n .
	ld a,001h		;93df	3e 01		> .
	ld (M_96F5),a		;93e1	32 f5 96	2 . .
	xor a			;93e4	af		.
	ld (M_96F6),a		;93e5	32 f6 96	2 . .
	ld (M_96F7),a		;93e8	32 f7 96	2 . .
	ld hl,D_9704		;93eb	21 04 97	! . .
	ld de,D_9710		;93ee	11 10 97	. . .
	ld bc,00003h		;93f1	01 03 00	. . .
	ldir			;93f4	ed b0		. .
	ei			;93f6	fb		.
	halt			;93f7	76		v
	di			;93f8	f3		.
	ld hl,D_974F		;93f9	21 4f 97	! O .
	call R_9A6E		;93fc	cd 6e 9a	. n .
	ei			;93ff	fb		.
	halt			;9400	76		v
	di			;9401	f3		.
	ld hl,05b0fh		;9402	21 0f 5b	! . [
	ld bc,00012h		;9405	01 12 00	. . .
	call R_9A83		;9408	cd 83 9a	. . .
	call R_9A71		;940b	cd 71 9a	. q .
	call R_9A7A		;940e	cd 7a 9a	. z .
	jp c,l9346h		;9411	da 46 93	. F .
	ld hl,05b1ch		;9414	21 1c 5b	! . [
	ld de,M_96EC		;9417	11 ec 96	. . .
	ld bc,00003h		;941a	01 03 00	. . .
	ldir			;941d	ed b0		. .
	ld a,(05b10h)		;941f	3a 10 5b	: . [
	cp 013h			;9422	fe 13		. .
	jr nz,l942dh		;9424	20 07		  .
	ld a,016h		;9426	3e 16		> .
	ld (M_96F7),a		;9428	32 f7 96	2 . .
	jr l9432h		;942b	18 05		. .
l942dh:
	cp 014h			;942d	fe 14		. .
	jp z,l9346h		;942f	ca 46 93	. F .
l9432h:
	call R_89F7		;9432	cd f7 89	. . .
	jp z,l9479h		;9435	ca 79 94	. y .
	ld hl,D_96EE		;9438	21 ee 96	! . .
	ld de,D_96FD		;943b	11 fd 96	. . .
	ld bc,D_96EE		;943e	01 ee 96	. . .
	call R_95DC		;9441	cd dc 95	. . .
R_9444:
	ld hl,D_9703		;9444	21 03 97	! . .
	ld de,D_96EE		;9447	11 ee 96	. . .
	ld bc,D_970F		;944a	01 0f 97	. . .
	call R_95DC		;944d	cd dc 95	. . .
	ei			;9450	fb		.
	halt			;9451	76		v
	di			;9452	f3		.
	xor a			;9453	af		.
	ld (D_970F),a		;9454	32 0f 97	2 . .
	ld (D_96EE),a		;9457	32 ee 96	2 . .
	ld hl,D_9704		;945a	21 04 97	! . .
	ld de,D_9710		;945d	11 10 97	. . .
	ld bc,00003h		;9460	01 03 00	. . .
	ldir			;9463	ed b0		. .
	ei			;9465	fb		.
	halt			;9466	76		v
	di			;9467	f3		.
	ld hl,D_970A		;9468	21 0a 97	! . .
	call R_9A6E		;946b	cd 6e 9a	. n .
	call R_9A7A		;946e	cd 7a 9a	. z .
	jr nc,l948dh		;9471	30 1a		0 .
	ei			;9473	fb		.
	halt			;9474	76		v
	di			;9475	f3		.
	jp R_9507		;9476	c3 07 95	. . .
l9479h:
	call R_8A1B		;9479	cd 1b 8a	. . .
	jr z,l948dh		;947c	28 0f		( .
	ld de,D_96EE		;947e	11 ee 96	. . .
	ld hl,D_96FD		;9481	21 fd 96	! . .
	ld bc,D_96EE		;9484	01 ee 96	. . .
	call R_95FE		;9487	cd fe 95	. . .
	jp R_9444		;948a	c3 44 94	. D .
l948dh:
	ld hl,M_93D5		;948d	21 d5 93	! . .
	call R_89D3		;9490	cd d3 89	. . .
	jr z,l949bh		;9493	28 06		( .
	call R_963D		;9495	cd 3d 96	. = .
	jp R_9396		;9498	c3 96 93	. . .
l949bh:
	call R_89D7		;949b	cd d7 89	. . .
	jr z,l94a6h		;949e	28 06		( .
	call R_9646		;94a0	cd 46 96	. F .
	jp R_9396		;94a3	c3 96 93	. . .
l94a6h:
	call R_8A5D		;94a6	cd 5d 8a	. ] .
	jr nz,R_9507		;94a9	20 5c		  \
	call R_89EF		;94ab	cd ef 89	. . .
	call nz,sub_9558h	;94ae	c4 58 95	. X .
	call R_8A27		;94b1	cd 27 8a	. ' .
	jr z,l94b9h		;94b4	28 03		( .
	jp R_9396		;94b6	c3 96 93	. . .
l94b9h:
	call R_8A23		;94b9	cd 23 8a	. # .
	jr z,l94d4h		;94bc	28 16		( .
	ei			;94be	fb		.
	halt			;94bf	76		v
	di			;94c0	f3		.
	ld a,(M_96F7)		;94c1	3a f7 96	: . .
	or a			;94c4	b7		.
	jr nz,l94d4h		;94c5	20 0d		  .
	ld hl,D_9737		;94c7	21 37 97	! 7 .
	call R_9A6E		;94ca	cd 6e 9a	. n .
	ld hl,D_973F		;94cd	21 3f 97	! ? .
	ld a,001h		;94d0	3e 01		> .
	xor (hl)		;94d2	ae		.
	ld (hl),a		;94d3	77		w
l94d4h:
	call R_8A1F		;94d4	cd 1f 8a	. . .
	jr z,l94eeh		;94d7	28 15		( .
R_94D9:
	ld a,(M_96F7)		;94d9	3a f7 96	: . .
	or a			;94dc	b7		.
	jr nz,l94eeh		;94dd	20 0f		  .
	ld hl,D_9743		;94df	21 43 97	! C .
	call R_9A6E		;94e2	cd 6e 9a	. n .
	xor a			;94e5	af		.
	ld hl,M_96EC		;94e6	21 ec 96	! . .
	ld (hl),a		;94e9	77		w
	inc hl			;94ea	23		#
	ld (hl),a		;94eb	77		w
	inc hl			;94ec	23		#
	ld (hl),a		;94ed	77		w
l94eeh:
	call R_89F3		;94ee	cd f3 89	. . .
	jr z,l9501h		;94f1	28 0e		( .
	ld hl,M_96F9		;94f3	21 f9 96	! . .
	ld a,(hl)		;94f6	7e		~
	inc a			;94f7	3c		<
	cp 004h			;94f8	fe 04		. .
	jr c,l94fdh		;94fa	38 01		8 .
	xor a			;94fc	af		.
l94fdh:
	ld (hl),a		;94fd	77		w
	call R_964D		;94fe	cd 4d 96	. M .
l9501h:
	ld a,(M_96F7)		;9501	3a f7 96	: . .
	or a			;9504	b7		.
	jr z,l9548h		;9505	28 41		( A
R_9507:
	ei			;9507	fb		.
	halt			;9508	76		v
	di			;9509	f3		.
	ld a,(M_96F9)		;950a	3a f9 96	: . .
	or a			;950d	b7		.
	jr z,l951ch		;950e	28 0c		( .
	cp 001h			;9510	fe 01		. .
	jr z,l952bh		;9512	28 17		( .
	cp 002h			;9514	fe 02		. .
	jr z,l9539h		;9516	28 21		( !
	cp 003h			;9518	fe 03		. .
	jr z,l953bh		;951a	28 1f		( .
l951ch:
	ld hl,M_93D5		;951c	21 d5 93	! . .
	ld b,(hl)		;951f	46		F
	call R_963D		;9520	cd 3d 96	. = .
	ld a,(hl)		;9523	7e		~
	cp b			;9524	b8		.
	jp nz,l9545h		;9525	c2 45 95	. E .
	jp R_94D9		;9528	c3 d9 94	. . .
l952bh:
	ld hl,M_93D5		;952b	21 d5 93	! . .
	ld a,(hl)		;952e	7e		~
	call R_963D		;952f	cd 3d 96	. = .
	cp (hl)			;9532	be		.
	jr nz,l9545h		;9533	20 10		  .
	ld a,(M_9E02)		;9535	3a 02 9e	: . .
	ld (hl),a		;9538	77		w
l9539h:
	jr l9545h		;9539	18 0a		. .
l953bh:
	ld a,(M_9E03)		;953b	3a 03 9e	: . .
	ld b,a			;953e	47		G
	call R_9628		;953f	cd 28 96	. ( .
	ld (M_93D5),a		;9542	32 d5 93	2 . .
l9545h:
	jp R_9396		;9545	c3 96 93	. . .
l9548h:
	ei			;9548	fb		.
	call R_8A07		;9549	cd 07 8a	. . .
	call nz,R_766A		;954c	c4 6a 76	. j v
	call R_8A0B		;954f	cd 0b 8a	. . .
	call nz,R_7690		;9552	c4 90 76	. . v
	jp R_72CA		;9555	c3 ca 72	. . r
sub_9558h:
	ld hl,D_96F8		;9558	21 f8 96	! . .
	ld a,(hl)		;955b	7e		~
	ld (hl),001h		;955c	36 01		6 .
	or a			;955e	b7		.
	jp z,R_964D		;955f	ca 4d 96	. M .
	ld (hl),000h		;9562	36 00		6 .
	jp R_964D		;9564	c3 4d 96	. M .
R_9567:
	xor a			;9567	af		.
	or b			;9568	b0		.
	jr nz,l956ch		;9569	20 01		  .
	inc b			;956b	04		.
l956ch:
	ld hl,D_9DFE		;956c	21 fe 9d	! . .
	ld a,b			;956f	78		x
	ld de,00008h		;9570	11 08 00	. . .
l9573h:
	add hl,de		;9573	19		.
	djnz l9573h		;9574	10 fd		. .
	cp (hl)			;9576	be		.
	jr z,l957ch		;9577	28 03		( .
	ld hl,D_9E06		;9579	21 06 9e	! . .
l957ch:
	inc hl			;957c	23		#
	inc hl			;957d	23		#
	inc hl			;957e	23		#
	ld bc,00003h		;957f	01 03 00	. . .
	ld de,D_9701		;9582	11 01 97	. . .
	push hl			;9585	e5		.
	ldir			;9586	ed b0		. .
	ld a,(D_96F8)		;9588	3a f8 96	: . .
	or a			;958b	b7		.
	jr z,l959dh		;958c	28 0f		( .
	inc hl			;958e	23		#
	inc hl			;958f	23		#
	ld b,040h		;9590	06 40		. @
	ld de,00008h		;9592	11 08 00	. . .
l9595h:
	ld a,(hl)		;9595	7e		~
	cp 041h			;9596	fe 41		. A
	jr nc,l95aah		;9598	30 10		0 .
	add hl,de		;959a	19		.
	djnz l9595h		;959b	10 f8		. .
l959dh:
	pop hl			;959d	e1		.
l959eh:
	ld bc,00008h		;959e	01 08 00	. . .
	ld de,D_9704		;95a1	11 04 97	. . .
	add hl,bc		;95a4	09		.
	ld c,003h		;95a5	0e 03		. .
	ldir			;95a7	ed b0		. .
	ret			;95a9	c9		.
l95aah:
	pop de			;95aa	d1		.
	dec hl			;95ab	2b		+
	dec hl			;95ac	2b		+
	dec hl			;95ad	2b		+
	dec hl			;95ae	2b		+
	dec hl			;95af	2b		+
	jr l959eh		;95b0	18 ec		. .
	ld a,(de)		;95b2	1a		.
	cp (hl)			;95b3	be		.
	ret c			;95b4	d8		.
	inc hl			;95b5	23		#
	inc de			;95b6	13		.
	ld a,(de)		;95b7	1a		.
	cp (hl)			;95b8	be		.
	ret c			;95b9	d8		.
	inc hl			;95ba	23		#
	inc de			;95bb	13		.
	ld a,(de)		;95bc	1a		.
	cp (hl)			;95bd	be		.
	ret c			;95be	d8		.
	ld a,016h		;95bf	3e 16		> .
	ld (M_96F7),a		;95c1	32 f7 96	2 . .
	ret			;95c4	c9		.
D_95C5:
	ld a,c			;95c5	79		y
	ld e,b			;95c6	58		X
	ld b,010h		;95c7	06 10		. .
	ld c,001h		;95c9	0e 01		. .
	jp R_8111		;95cb	c3 11 81	. . .
R_95CE:
	ld hl,0e560h		;95ce	21 60 e5	! ` .
	sub 030h		;95d1	d6 30		. 0
	ret z			;95d3	c8		.
	ld b,a			;95d4	47		G
	ld de,00010h		;95d5	11 10 00	. . .
l95d8h:
	add hl,de		;95d8	19		.
	djnz l95d8h		;95d9	10 fd		. .
	ret			;95db	c9		.
R_95DC:
	ld a,(de)		;95dc	1a		.
	add a,(hl)		;95dd	86		.
	cp 04ah			;95de	fe 4a		. J
	jr c,l95e4h		;95e0	38 02		8 .
	sub 04ah		;95e2	d6 4a		. J
l95e4h:
	ld (bc),a		;95e4	02		.
	dec bc			;95e5	0b		.
	dec hl			;95e6	2b		+
	dec de			;95e7	1b		.
	ld a,(de)		;95e8	1a		.
	jr c,l95ech		;95e9	38 01		8 .
	inc a			;95eb	3c		<
l95ech:
	add a,(hl)		;95ec	86		.
	cp 03ch			;95ed	fe 3c		. <
	jr c,l95f3h		;95ef	38 02		8 .
	sub 03ch		;95f1	d6 3c		. <
l95f3h:
	ld (bc),a		;95f3	02		.
	dec bc			;95f4	0b		.
	dec hl			;95f5	2b		+
	dec de			;95f6	1b		.
	ld a,(de)		;95f7	1a		.
	jr c,l95fbh		;95f8	38 01		8 .
	inc a			;95fa	3c		<
l95fbh:
	add a,(hl)		;95fb	86		.
	ld (bc),a		;95fc	02		.
	ret			;95fd	c9		.
R_95FE:
	ld a,(de)		;95fe	1a		.
	sub (hl)		;95ff	96		.
	jr nc,l9604h		;9600	30 02		0 .
	add a,04ah		;9602	c6 4a		. J
l9604h:
	ld (bc),a		;9604	02		.
	dec hl			;9605	2b		+
	dec de			;9606	1b		.
	dec bc			;9607	0b		.
	ld a,(de)		;9608	1a		.
	jr nc,l960ch		;9609	30 01		0 .
	dec a			;960b	3d		=
l960ch:
	sub (hl)		;960c	96		.
	jr nc,l9611h		;960d	30 02		0 .
	add a,03ch		;960f	c6 3c		. <
l9611h:
	ld (bc),a		;9611	02		.
	dec hl			;9612	2b		+
	dec de			;9613	1b		.
	dec bc			;9614	0b		.
	ld a,(de)		;9615	1a		.
	jr nc,l961ch		;9616	30 04		0 .
	dec a			;9618	3d		=
	jp m,l9621h		;9619	fa 21 96	. ! .
l961ch:
	sub (hl)		;961c	96		.
	jr c,l9621h		;961d	38 02		8 .
	ld (bc),a		;961f	02		.
	ret			;9620	c9		.
l9621h:
	xor a			;9621	af		.
	ld (bc),a		;9622	02		.
	inc bc			;9623	03		.
	ld (bc),a		;9624	02		.
	inc bc			;9625	03		.
	ld (bc),a		;9626	02		.
	ret			;9627	c9		.
R_9628:
	ld a,(M_93D5)		;9628	3a d5 93	: . .
	ld c,a			;962b	4f		O
	ld a,r			;962c	ed 5f		. _
	xor c			;962e	a9		.
	add a,e			;962f	83		.
	add a,(hl)		;9630	86		.
l9631h:
	push bc			;9631	c5		.
l9632h:
	dec a			;9632	3d		=
	jr z,l963ah		;9633	28 05		( .
	djnz l9632h		;9635	10 fb		. .
	pop bc			;9637	c1		.
	jr l9631h		;9638	18 f7		. .
l963ah:
	ld a,b			;963a	78		x
	pop bc			;963b	c1		.
	ret			;963c	c9		.
R_963D:
	ld a,(M_9E03)		;963d	3a 03 9e	: . .
	ld c,a			;9640	4f		O
	ld a,(hl)		;9641	7e		~
	cp c			;9642	b9		.
	ret nc			;9643	d0		.
	inc (hl)		;9644	34		4
	ret			;9645	c9		.
R_9646:
	ld a,(M_9E02)		;9646	3a 02 9e	: . .
	cp (hl)			;9649	be		.
	ret nc			;964a	d0		.
	dec (hl)		;964b	35		5
	ret			;964c	c9		.
R_964D:
	ld a,(D_96F8)		;964d	3a f8 96	: . .
	sla a			;9650	cb 27		. '
	sla a			;9652	cb 27		. '
	or a			;9654	b7		.
	jr nz,l9659h		;9655	20 02		  .
	ld a,007h		;9657	3e 07		> .
l9659h:
	ld hl,0580bh		;9659	21 0b 58	! . X
	ld b,002h		;965c	06 02		. .
l965eh:
	ld (hl),a		;965e	77		w
	inc l			;965f	2c		,
	ld (hl),a		;9660	77		w
	inc l			;9661	2c		,
	ld (hl),a		;9662	77		w
	ld l,02bh		;9663	2e 2b		. +
	djnz l965eh		;9665	10 f7		. .
	ld a,(M_96F9)		;9667	3a f9 96	: . .
R_966A:
	ld hl,0e620h		;966a	21 20 e6	!   .
	or a			;966d	b7		.
	jr z,l9677h		;966e	28 07		( .
	ld b,a			;9670	47		G
	ld de,00030h		;9671	11 30 00	. 0 .
l9674h:
	add hl,de		;9674	19		.
	djnz l9674h		;9675	10 fd		. .
l9677h:
	call R_8496		;9677	cd 96 84	. . .
	ld bc,00f03h		;967a	01 03 0f	. . .
	ld e,000h		;967d	1e 00		. .
	ld a,00bh		;967f	3e 0b		> .
	jp R_8111		;9681	c3 11 81	. . .
sub_9684h:
	ld a,(M_96F6)		;9684	3a f6 96	: . .
	or a			;9687	b7		.
	ret nz			;9688	c0		.
	ld hl,D_96EE		;9689	21 ee 96	! . .
	ld b,003h		;968c	06 03		. .
	ld a,006h		;968e	3e 06		> .
	ex af,af'		;9690	08		.
l9691h:
	ld a,(hl)		;9691	7e		~
	dec hl			;9692	2b		+
	push bc			;9693	c5		.
	push hl			;9694	e5		.
	ld de,D_96F2		;9695	11 f2 96	. . .
	call R_9303		;9698	cd 03 93	. . .
	ld a,(D_96F2)		;969b	3a f2 96	: . .
	call R_95CE		;969e	cd ce 95	. . .
	ex af,af'		;96a1	08		.
	ld c,a			;96a2	4f		O
	inc a			;96a3	3c		<
	ex af,af'		;96a4	08		.
	ld b,000h		;96a5	06 00		. .
	call D_95C5		;96a7	cd c5 95	. . .
	ld a,(M_96F3)		;96aa	3a f3 96	: . .
	call R_95CE		;96ad	cd ce 95	. . .
	ex af,af'		;96b0	08		.
	ld c,a			;96b1	4f		O
	sub 004h		;96b2	d6 04		. .
	ex af,af'		;96b4	08		.
	ld a,(M_96A6)		;96b5	3a a6 96	: . .
	ld b,a			;96b8	47		G
	call D_95C5		;96b9	cd c5 95	. . .
	pop hl			;96bc	e1		.
	pop bc			;96bd	c1		.
	djnz l9691h		;96be	10 d1		. .
M_96C0:
	nop			;96c0	00		.
	ld hl,D_96EE		;96c1	21 ee 96	! . .
	ld a,(hl)		;96c4	7e		~
	add a,002h		;96c5	c6 02		. .
	cp 064h			;96c7	fe 64		. d
	jr c,l96d9h		;96c9	38 0e		8 .
	ld (hl),000h		;96cb	36 00		6 .
	dec hl			;96cd	2b		+
	ld a,(hl)		;96ce	7e		~
	inc a			;96cf	3c		<
	cp 03ch			;96d0	fe 3c		. <
	jr c,l96d9h		;96d2	38 05		8 .
	ld (hl),000h		;96d4	36 00		6 .
	dec hl			;96d6	2b		+
	ld a,(hl)		;96d7	7e		~
	inc a			;96d8	3c		<
l96d9h:
	ld (hl),a		;96d9	77		w
	ld a,(M_96F5)		;96da	3a f5 96	: . .
	or a			;96dd	b7		.
	ret z			;96de	c8		.
	xor a			;96df	af		.
	ld (M_96F5),a		;96e0	32 f5 96	2 . .
	ld hl,M_96EC		;96e3	21 ec 96	! . .
	ld (hl),a		;96e6	77		w
	inc hl			;96e7	23		#
	ld (hl),a		;96e8	77		w
	inc hl			;96e9	23		#
	ld (hl),a		;96ea	77		w
	ret			;96eb	c9		.
M_96EC:
	nop			;96ec	00		.
	nop			;96ed	00		.
D_96EE:
	nop			;96ee	00		.
	nop			;96ef	00		.
	nop			;96f0	00		.
	nop			;96f1	00		.
D_96F2:
	jr nc,l9724h		;96f2	30 30		0 0
	jr nc,M_96F6		;96f4	30 00		0 .
M_96F6:
	nop			;96f6	00		.
M_96F7:
	nop			;96f7	00		.
D_96F8:
	nop			;96f8	00		.
M_96F9:
	nop			;96f9	00		.
	ld bc,00500h		;96fa	01 00 05	. . .
D_96FD:
	nop			;96fd	00		.
D_96FE:
	ld b,a			;96fe	47		G
	nop			;96ff	00		.
	nop			;9700	00		.
D_9701:
	nop			;9701	00		.
	nop			;9702	00		.
D_9703:
	nop			;9703	00		.
D_9704:
	nop			;9704	00		.
	ld bc,00000h		;9705	01 00 00	. . .
	nop			;9708	00		.
	nop			;9709	00		.
D_970A:
	ld b,a			;970a	47		G
	nop			;970b	00		.
	nop			;970c	00		.
	nop			;970d	00		.
	nop			;970e	00		.
D_970F:
	nop			;970f	00		.
D_9710:
	nop			;9710	00		.
	nop			;9711	00		.
	nop			;9712	00		.
	nop			;9713	00		.
	nop			;9714	00		.
	nop			;9715	00		.
	ld b,a			;9716	47		G
	nop			;9717	00		.
	nop			;9718	00		.
	rst 38h			;9719	ff		.
	rst 38h			;971a	ff		.
	rst 38h			;971b	ff		.
	rst 38h			;971c	ff		.
	rst 38h			;971d	ff		.
	rst 38h			;971e	ff		.
	nop			;971f	00		.
	nop			;9720	00		.
	nop			;9721	00		.
	ld b,l			;9722	45		E
	nop			;9723	00		.
l9724h:
	ld bc,01400h		;9724	01 00 14	. . .
	nop			;9727	00		.
	nop			;9728	00		.
	nop			;9729	00		.
	nop			;972a	00		.
D_972B:
	ld b,e			;972b	43		C
	ld (bc),a		;972c	02		.
	nop			;972d	00		.
	nop			;972e	00		.
	nop			;972f	00		.
	nop			;9730	00		.
	ld bc,02003h		;9731	01 03 20	. .  
	nop			;9734	00		.
	nop			;9735	00		.
	nop			;9736	00		.
D_9737:
	ld c,e			;9737	4b		K
	nop			;9738	00		.
	nop			;9739	00		.
	nop			;973a	00		.
	nop			;973b	00		.
	nop			;973c	00		.
	nop			;973d	00		.
	nop			;973e	00		.
D_973F:
	nop			;973f	00		.
	nop			;9740	00		.
	nop			;9741	00		.
	nop			;9742	00		.
D_9743:
	ld c,(hl)		;9743	4e		N
	nop			;9744	00		.
	nop			;9745	00		.
	nop			;9746	00		.
	nop			;9747	00		.
	nop			;9748	00		.
	nop			;9749	00		.
	nop			;974a	00		.
	nop			;974b	00		.
	nop			;974c	00		.
	nop			;974d	00		.
	nop			;974e	00		.
D_974F:
	ld b,d			;974f	42		B
	ld (bc),a		;9750	02		.
	ld b,b			;9751	40		@
	ld bc,00000h		;9752	01 00 00	. . .
	nop			;9755	00		.
	nop			;9756	00		.
	ld (de),a		;9757	12		.
	nop			;9758	00		.
	nop			;9759	00		.
	nop			;975a	00		.
D_975B:
	jp R_8482		;975b	c3 82 84	. . .
	jp R_8486		;975e	c3 86 84	. . .
	jp R_848A		;9761	c3 8a 84	. . .
	jp R_848E		;9764	c3 8e 84	. . .
	jp R_84B2		;9767	c3 b2 84	. . .
	jp R_9316		;976a	c3 16 93	. . .
	jp R_766A		;976d	c3 6a 76	. j v
	jp R_7690		;9770	c3 90 76	. . v
	jp R_85F4		;9773	c3 f4 85	. . .
	jp R_72A0		;9776	c3 a0 72	. . r
	jp 00000h		;9779	c3 00 00	. . .
	jp 00000h		;977c	c3 00 00	. . .
	jp R_9A9B		;977f	c3 9b 9a	. . .
	jp R_769E		;9782	c3 9e 76	. . v
	ret			;9785	c9		.
	nop			;9786	00		.
	nop			;9787	00		.
	jp R_7F80		;9788	c3 80 7f	. . .
	ret			;978b	c9		.
	nop			;978c	00		.
	nop			;978d	00		.
	nop			;978e	00		.
	nop			;978f	00		.
	ld c,b			;9790	48		H
	sub c			;9791	91		.
	ld b,e			;9792	43		C
	sub e			;9793	93		.
M_9794:
	ld sp,00031h		;9794	31 31 00	1 1 .
	nop			;9797	00		.
	nop			;9798	00		.
	nop			;9799	00		.
	nop			;979a	00		.
	cp 0f7h			;979b	fe f7		. .
	ei			;979d	fb		.
	defb 0fdh,0efh		;979e	fd ef		. .
; --------------------------------------------------------------------------
; DATA 0x97A0-0x9E49  virtual keyboard tables / note tables
; --------------------------------------------------------------------------
DATA_97A0:
	defb DF BF 7F 00 01 01 01 5A  ;97A0  .......Z
	defb 20 20 58 20 20 43 20 20  ;97A8    X  C  
	defb 56 20 20 31 20 20 32 20  ;97B0  V  1  2 
	defb 20 33 20 20 34 20 20 35  ;97B8   3  4  5
	defb 20 20 51 20 20 57 20 20  ;97C0    Q  W  
	defb 45 20 20 52 20 20 54 20  ;97C8  E  R  T 
	defb 20 41 20 20 53 20 20 44  ;97D0   A  S  D
	defb 20 20 46 20 20 47 20 20  ;97D8    F  G  
	defb 30 20 20 39 20 20 38 20  ;97E0  0  9  8 
	defb 20 37 20 20 36 20 20 50  ;97E8   7  6  P
	defb 20 20 4F 20 20 49 20 20  ;97F0    O  I  
	defb 55 20 20 59 20 20 02 01  ;97F8  U  Y  ..
	defb 01 4C 20 20 4B 20 20 4A  ;9800  .L  K  J
	defb 20 20 48 20 20 20 01 01  ;9808    H   ..
	defb 00 01 01 4D 20 20 4E 20  ;9810  ...M  N 
	defb 20 42 20 20 00 00 00 00  ;9818   B  ....
	defb 00 00 00 00 00 00 F8 38  ;9820  .......8
	defb 00 38 78 20 00 00 20 00  ;9828  .8x .. .
	defb 00 00 00 20 00 00 00 00  ;9830  ... ....
	defb 00 00 00 00 00 00 00 00  ;9838  ........
	defb 00 00 00 00 00 00 00 00  ;9840  ........
	defb 00 00 00 00 00 00 00 00  ;9848  ........
	defb 00 00 00 00 00 00 00 00  ;9850  ........
	defb 00 00 00 00 00 00 00 00  ;9858  ........
	defb 00 00 00 00 00 00 00 00  ;9860  ........
	defb 00 00 00 00 00 00 00 00  ;9868  ........
	defb 00 00 00 00 00 00 00 00  ;9870  ........
	defb 00 00 00 00 00 00 00 00  ;9878  ........
	defb 00 40 00 41 00 42 00 43  ;9880  .@.A.B.C
	defb 00 44 00 45 00 46 00 47  ;9888  .D.E.F.G
	defb 20 40 20 41 20 42 20 43  ;9890   @ A B C
	defb 20 44 20 45 20 46 20 47  ;9898   D E F G
	defb 40 40 40 41 40 42 40 43  ;98A0  @@@A@B@C
	defb 40 44 40 45 40 46 40 47  ;98A8  @D@E@F@G
	defb 60 40 60 41 60 42 60 43  ;98B0  `@`A`B`C
	defb 60 44 60 45 60 46 60 47  ;98B8  `D`E`F`G
	defb 80 40 80 41 80 42 80 43  ;98C0  .@.A.B.C
	defb 80 44 80 45 80 46 80 47  ;98C8  .D.E.F.G
	defb A0 40 A0 41 A0 42 A0 43  ;98D0  .@.A.B.C
	defb A0 44 A0 45 A0 46 A0 47  ;98D8  .D.E.F.G
	defb C0 40 C0 41 C0 42 C0 43  ;98E0  .@.A.B.C
	defb C0 44 C0 45 C0 46 C0 47  ;98E8  .D.E.F.G
	defb E0 40 E0 41 E0 42 E0 43  ;98F0  .@.A.B.C
	defb E0 44 E0 45 E0 46 E0 47  ;98F8  .D.E.F.G
	defb 00 48 00 49 00 4A 00 4B  ;9900  .H.I.J.K
	defb 00 4C 00 4D 00 4E 00 4F  ;9908  .L.M.N.O
	defb 20 48 20 49 20 4A 20 4B  ;9910   H I J K
	defb 20 4C 20 4D 20 4E 20 4F  ;9918   L M N O
	defb 40 48 40 49 40 4A 40 4B  ;9920  @H@I@J@K
	defb 40 4C 40 4D 40 4E 40 4F  ;9928  @L@M@N@O
	defb 60 48 60 49 60 4A 60 4B  ;9930  `H`I`J`K
	defb 60 4C 60 4D 60 4E 60 4F  ;9938  `L`M`N`O
	defb 80 48 80 49 80 4A 80 4B  ;9940  .H.I.J.K
	defb 80 4C 80 4D 80 4E 80 4F  ;9948  .L.M.N.O
	defb A0 48 A0 49 A0 4A A0 4B  ;9950  .H.I.J.K
	defb A0 4C A0 4D A0 4E A0 4F  ;9958  .L.M.N.O
	defb C0 48 C0 49 C0 4A C0 4B  ;9960  .H.I.J.K
	defb C0 4C C0 4D C0 4E C0 4F  ;9968  .L.M.N.O
	defb E0 48 E0 49 E0 4A E0 4B  ;9970  .H.I.J.K
	defb E0 4C E0 4D E0 4E E0 4F  ;9978  .L.M.N.O
	defb 00 50 00 51 00 52 00 53  ;9980  .P.Q.R.S
	defb 00 54 00 55 00 56 00 57  ;9988  .T.U.V.W
	defb 20 50 20 51 20 52 20 53  ;9990   P Q R S
	defb 20 54 20 55 20 56 20 57  ;9998   T U V W
	defb 40 50 40 51 40 52 40 53  ;99A0  @P@Q@R@S
	defb 40 54 40 55 40 56 40 57  ;99A8  @T@U@V@W
	defb 60 50 60 51 60 52 60 53  ;99B0  `P`Q`R`S
	defb 60 54 60 55 60 56 60 57  ;99B8  `T`U`V`W
	defb 80 50 80 51 80 52 80 53  ;99C0  .P.Q.R.S
	defb 80 54 80 55 80 56 80 57  ;99C8  .T.U.V.W
	defb A0 50 A0 51 A0 52 A0 53  ;99D0  .P.Q.R.S
	defb A0 54 A0 55 A0 56 A0 57  ;99D8  .T.U.V.W
	defb C0 50 C0 51 C0 52 C0 53  ;99E0  .P.Q.R.S
	defb C0 54 C0 55 C0 56 C0 57  ;99E8  .T.U.V.W
	defb E0 50 E0 51 E0 52 E0 53  ;99F0  .P.Q.R.S
	defb E0 54 E0 55 E0 56 E0 57  ;99F8  .T.U.V.W
	defb 00 90 00 00 00 00 F0 A0  ;9A00  ........
	defb 00 00 00 00 00 00 BB 00  ;9A08  ........
	defb 01 60 00 00 00 00 00 00  ;9A10  .`......
	defb 00 00 28 00 00 00 00 10  ;9A18  ..(.....
	defb 00 00 01 10 00 00 43 00  ;9A20  ......C.
	defb 00 00 00 00 01 03 20 00  ;9A28  ...... .
	defb 00 00 C3 08 9B C3 14 9B  ;9A30  ........
	defb C3 1D 9B C3 22 9B C3 27  ;9A38  ...."..'
	defb 9B C3 39 9B C3 3F 9B C3  ;9A40  ..9..?..
	defb 45 9B C3 4E 9B C3 58 9B  ;9A48  E..N..X.
	defb C3 5F 9B C3 63 9B C3 84  ;9A50  ._..c...
	defb 9B C3 A0 9B C3 BB 9B C3  ;9A58  ........
	defb A0 9B C3 D6 9B C3 E1 9B  ;9A60  ........
	defb C3 F4 9B C3 00 00 C3 1B  ;9A68  ........
	defb 9C C3 5B 9C C3 69 9C C3  ;9A70  ..[..i..
	defb 89 9C C3 91 9C C3 96 9C  ;9A78  ........
	defb C3 A7 9C C3 AE 9C C3 DE  ;9A80  ........
	defb 9C C3 ED 9C C3 FC 9C C3  ;9A88  ........
	defb FF 9C C3 1D 9D C3 00 00  ;9A90  ........
	defb C3 99 9D C3 A7 9D C3 BB  ;9A98  ........
	defb 9D C3 BE 9D C3 C2 9D C0  ;9AA0  ........
	defb 21 F0 3F 11 DB 9A 01 06  ;9AA8  !.?.....
	defb 00 DD 21 0D 18 CD F9 9A  ;9AB0  ..!.....
	defb 21 DB 9A 11 E1 9A 01 C9  ;9AB8  !.......
	defb 06 1A BE 20 06 23 13 10  ;9AC0  ... .#..
	defb F8 0E F3 79 32 FE 9A 32  ;9AC8  ...y2..2
	defb F4 9A 01 BA FF 3E 77 CD  ;9AD0  .....>w.
	defb FE 9A C9 FF FF FF FF FF  ;9AD8  ........
	defb FF ED 79 C9 ED 78 C9 3E  ;9AE0  ..y..x.>
	defb F7 DB FE 0F C9 3E F7 DB  ;9AE8  .....>..
	defb FE 0F 0F C9 C9 DD 21 F3  ;9AF0  ......!.
	defb 3F DD E5 C3 2F 3D C9 DD  ;9AF8  ?.../=..
	defb 21 F0 3F DD E5 C3 2F 3D  ;9B00  !.?.../=
	defb 3A 08 9A B7 C0 01 BE FF  ;9B08  :.......
	defb 3E EC 18 EA 21 DB 9D CD  ;9B10  >...!...
	defb 1B 9C C3 5B 9C 21 E7 9D  ;9B18  ...[.!..
	defb 18 F5 3A 02 9A B7 C0 21  ;9B20  ..:....!
	defb 10 00 CD 99 9D 21 0E 9A  ;9B28  .....!..
	defb CD 17 9B 21 00 00 C3 1D  ;9B30  ...!....
	defb 9D CD 3F 9B C3 5B 9C 21  ;9B38  ..?..[.!
	defb F3 9D C3 1B 9C 01 BE FF  ;9B40  ........
	defb CD FE 9A C3 69 9C 01 BE  ;9B48  ....i...
	defb FF 18 AB 01 BE FF 18 9C  ;9B50  ........
	defb 22 0A 9A ED 53 0C 9A 7C  ;9B58  "...S..|
	defb 62 53 5F 7C E6 0F 67 3A  ;9B60  bS_|..g:
	defb 16 9C B4 01 BE FE CD FE  ;9B68  ........
	defb 9A 7D 06 FB CD FE 9A 7A  ;9B70  .}.....z
	defb 06 FD CD FE 9A 7B 06 FC  ;9B78  .....{..
	defb CD FE 9A C9 01 BE FE CD  ;9B80  ........
	defb F4 9A E6 0F 67 06 FB CD  ;9B88  ....g...
	defb F4 9A 6F 06 FD CD F4 9A  ;9B90  ..o.....
	defb 57 06 FC CD F4 9A 5F C9  ;9B98  W....._.
	defb F5 01 BE FA CD FE 9A 3E  ;9BA0  .......>
	defb 20 CD 45 9B C1 C5 CD 6B  ;9BA8   .E....k
	defb 9D 22 17 9C CD 69 9C C1  ;9BB0  ."...i..
	defb 10 F3 C9 F5 01 BE FA CD  ;9BB8  ........
	defb FE 9A 3E 30 CD 45 9B C1  ;9BC0  ..>0.E..
	defb C5 CD 81 9D 22 17 9C CD  ;9BC8  ...."...
	defb 69 9C C1 10 F3 C9 CD F4  ;9BD0  i.......
	defb 9B 3E E0 CD 4E 9B C3 69  ;9BD8  .>..N..i
	defb 9C CD F4 9B 3E FC 01 BE  ;9BE0  ....>...
	defb FA CD FE 9A 3E E2 CD 4E  ;9BE8  ....>..N
	defb 9B C3 69 9C CD A7 9C CD  ;9BF0  ..i.....
	defb 69 9C 11 00 00 21 02 00  ;9BF8  i....!..
	defb CD 58 9B 3E 01 CD A0 9B  ;9C00  .X.>....
	defb 11 00 00 21 04 00 CD 58  ;9C08  ...!...X
	defb 9B 3E 01 C3 A0 9B A0 00  ;9C10  .>......
	defb 00 00 00 E5 CD 69 9C CD  ;9C18  .....i..
	defb 96 9C 21 00 08 CD DE 9C  ;9C20  ..!.....
	defb 3E A0 CD 4E 9B E1 01 0C  ;9C28  >..N....
	defb 00 C5 CD 5B 9C CD 89 9C  ;9C30  ...[....
	defb C1 B7 CB 18 CB 19 30 01  ;9C38  ......0.
	defb 03 C5 23 7E 01 BE D8 CD  ;9C40  ..#~....
	defb FE 9A 2B 7E 01 BE F8 CD  ;9C48  ..+~....
	defb FE 9A 23 23 C1 0B 78 B1  ;9C50  ..##..x.
	defb 20 E7 C9 AF D3 FE CD 53  ;9C58   ......S
	defb 9B 07 D0 3E 04 D3 FE 18  ;9C60  ...>....
	defb F2 AF 32 01 9A CD 53 9B  ;9C68  ..2...S.
	defb 07 30 12 3A 01 9A FE FE  ;9C70  .0.:....
	defb 38 F3 3E 01 D3 FE CD 08  ;9C78  8.>.....
	defb 9B FB 76 18 E4 AF D3 FE  ;9C80  ..v.....
	defb C9 CD 53 9B CB 5F C0 18  ;9C88  ..S.._..
	defb F8 CD 53 9B 0F C9 3A 07  ;9C90  ..S...:.
	defb 9A 32 16 9C 01 BE FE CD  ;9C98  .2......
	defb FE 9A CD 53 9B 07 C9 3A  ;9CA0  ...S...:
	defb 06 9A 18 ED C1 C9 C5 CD  ;9CA8  ........
	defb 96 9C CD 5B 9C CD 91 9C  ;9CB0  ...[....
	defb 38 F2 CD 89 9C C1 B7 CB  ;9CB8  8.......
	defb 18 CB 19 30 01 03 C5 01  ;9CC0  ...0....
	defb BE F8 CD F4 9A 77 23 01  ;9CC8  .....w#.
	defb BE D8 CD F4 9A 77 23 C1  ;9CD0  .....w#.
	defb 0B 78 B1 20 E9 C9 01 BE  ;9CD8  .x. ....
	defb FC 7D CD FE 9A 01 BE FD  ;9CE0  .}......
	defb 7C CD FE 9A C9 01 BE FC  ;9CE8  |.......
	defb CD F4 9A 6F 01 BE FD CD  ;9CF0  ...o....
	defb F4 9A 67 C9 CD 96 9C 11  ;9CF8  ..g.....
	defb 00 00 62 6B CD 58 9B CD  ;9D00  ..bk.X..
	defb 0D 9B FB 76 76 76 76 CD  ;9D08  ...vvvv.
	defb 84 9B 21 14 EB B7 ED 52  ;9D10  ..!....R
	defb C8 3E 01 B7 C9 22 69 9D  ;9D18  .>..."i.
	defb AF D3 FE 3A 00 9A B7 28  ;9D20  ...:...(
	defb 0C CD E7 9A D4 12 60 CD  ;9D28  ......`.
	defb ED 9A D4 15 60 21 1A 9A  ;9D30  ....`!..
	defb CD 1B 9C 3E 05 D3 FE CD  ;9D38  ...>....
	defb 69 9C CD 91 9C 38 D9 16  ;9D40  i....8..
	defb 00 CD 53 9B CB 5F 20 05  ;9D48  ..S.._ .
	defb 15 28 CD 18 F4 2A 69 9D  ;9D50  .(...*i.
	defb CD 6B 9D CD 6B 9D CD 6B  ;9D58  .k..k..k
	defb 9D CD 6B 9D AF D3 FE C9  ;9D60  ..k.....
	defb 00 00 00 0E BE 16 00 06  ;9D68  ........
	defb F8 CD F4 9A 77 23 06 D8  ;9D70  ....w#..
	defb CD F4 9A 77 23 15 20 EF  ;9D78  ...w#. .
	defb C9 0E BE 16 00 06 D8 23  ;9D80  .......#
	defb 7E CD FE 9A 06 F8 2B 7E  ;9D88  ~.....+~
	defb CD FE 9A 23 23 15 20 ED  ;9D90  ...##. .
	defb C9 11 00 00 ED 53 1C 9A  ;9D98  .....S..
	defb 7C 65 6F 22 1E 9A C9 AF  ;9DA0  |eo"....
	defb 32 03 9A 32 05 9A 32 04  ;9DA8  2..2..2.
	defb 9A 3A 02 9A B7 C0 11 1C  ;9DB0  .:......
	defb 9A 18 04 11 1C 9A 7E 12  ;9DB8  ......~.
	defb 23 13 7E 12 23 13 7E 12  ;9DC0  #.~.#.~.
	defb 23 13 7E 12 23 13 C9 12  ;9DC8  #.~.#...
	defb 00 00 00 24 00 00 00 00  ;9DD0  ...$....
	defb 00 00 00 1B 00 00 00 02  ;9DD8  ........
	defb 00 00 00 00 00 00 00 1B  ;9DE0  ........
	defb 00 00 00 03 00 00 00 00  ;9DE8  ........
	defb 00 00 00 1B 00 00 00 00  ;9DF0  ........
	defb 00 00 00 00 00 00 00 00  ;9DF8  ........
	defb A8 58 58 18 A8 A8 68 58  ;9E00  .XX...hX
	defb 68 18 68 58 60 60 00 00  ;9E08  h.hX``..
	defb 00 00 00 00 00 00 00 00  ;9E10  ........
	defb 00 00 00 00 00 00 00 00  ;9E18  ........
	defb 00 00 00 00 00 00 F8 60  ;9E20  .......`
	defb 70 E0 D8 A8 C0 30 70 50  ;9E28  p....0pP
	defb 28 10 F0 20 20 70 20 20  ;9E30  (..  p  
	defb F8 50 00 00 00 30 00 50  ;9E38  .P...0.P
	defb 78 58 34 00 18 60 50 20  ;9E40  xX4..`P 
	defb 30 00                    ;9E48  0.

; --------------------------------------------------------------------------
; DATA 0x9E4A-0x9FFF  8x8 font bitmaps
; --------------------------------------------------------------------------
DATA_9E4A:
	defb 30 60 30 78 78 70 18 70  ;9E4A  0`0xxp.p
	defb 30 60 30 30 30 30 18 00  ;9E52  0`0000..
	defb 60 30 38 58 70 30 70 78  ;9E5A  `08Xp0px
	defb 60 30 68 78 30 68 78 58  ;9E62  `0hx0hxX
	defb 68 30 60 30 68 30 30 30  ;9E6A  h0`0h000
	defb 10 48 58 30 78 38 18 70  ;9E72  .HX0x8.p
	defb 20 00 78 38 70 38 38 38  ;9E7A   .x8p888
	defb 60 18 68 78 58 68 18 58  ;9E82  `.hxXh.X
	defb 68 30 60 18 60 70 18 30  ;9E8A  h0`.`p.0
	defb 10 48 58 18 78 30 30 30  ;9E92  .HX.x000
	defb 00 88 58 70 70 60 78 78  ;9E9A  ..Xpp`xx
	defb A8 70 38 38 68 58 58 68  ;9EA2  .p88hXXh
	defb 30 58 60 30 30 30 20 58  ;9EAA  0X`000 X
	defb 70 18 F8 F0 70 68 70 70  ;9EB2  p...phpp
	defb 50 58 38 70 70 38 38 38  ;9EBA  PX8pp888
	defb A8 70 38 38 68 58 68 68  ;9EC2  .p88hXhh
	defb 30 58 50 A8 50 60 60 60  ;9ECA  0XP.P```
	defb 50 50 60 50 50 50 00 00  ;9ED2  PP`PPP..
	defb 00 60 00 00 60 60 00 60  ;9EDA  .`..``.`
	defb 60 50 00 50 00 50 50 00  ;9EE2  `P.P.PP.
	defb 50 00 00 60 50 00 00 60  ;9EEA  P..`P..`
	defb 50 50 60 00 60 F8 F8 C0  ;9EF2  PP`.`...
	defb 18 00 60 30 30 18 70 58  ;9EFA  ..`00.pX
	defb 70 18 F8 F0 70 68 70 70  ;9F02  p...phpp
	defb 50 58 78 38 00 00 00 00  ;9F0A  PXx8....
	defb 00 00 00 00 00 00 00 00  ;9F12  ........
	defb 00 00 00 00 00 00 00 00  ;9F1A  ........
	defb 00 00 00 00 F8 00 20 60  ;9F22  ...... `
	defb C0 20 80 10 20 00 28 E0  ;9F2A  . .. .(.
	defb 00 F8 20 20 00 00 00 00  ;9F32  ..  ....
	defb 00 00 00 00 00 00 30 00  ;9F3A  ......0.
	defb 00 00 00 00 00 00 10 00  ;9F42  ........
	defb 00 00 00 00 00 00 00 00  ;9F4A  ........
	defb 00 00 00 00 00 10 00 00  ;9F52  ........
	defb 00 00 00 00 00 00 00 00  ;9F5A  ........
	defb 00 00 00 00 00 00 00 00  ;9F62  ........
	defb 00 00 00 08 00 00 00 00  ;9F6A  ........
	defb 00 00 00 00 00 00 00 00  ;9F72  ........
	defb 00 F8 00 00 00 00 00 00  ;9F7A  ........
	defb 00 30 00 00 30 00 00 00  ;9F82  .0..0...
	defb 00 00 60 18 00 00 00 00  ;9F8A  ..`.....
	defb 00 00 00 30 00 18 30 60  ;9F92  ...0..0`
	defb 00 70 00 00 00 00 00 00  ;9F9A  .p......
	defb 00 00 00 00 00 00 00 00  ;9FA2  ........
	defb 00 00 00 00 00 00 00 00  ;9FAA  ........
	defb 08 00 00 08 00 00 00 00  ;9FB2  ........
	defb 00 00 00 00 00 00 00 00  ;9FBA  ........
	defb 00 00 00 00 00 00 00 00  ;9FC2  ........
	defb 00 00 00 50 F8 60 60 60  ;9FCA  ...P.```
	defb 50 50 60 50 50 50 00 00  ;9FD2  PP`PPP..
	defb 00 60 00 00 60 60 00 60  ;9FDA  .`..``.`
	defb 60 50 00 50 00 50 50 00  ;9FE2  `P.P.PP.
	defb 50 00 00 60 50 00 00 60  ;9FEA  P..`P..`
	defb 50 50 60 00 60 F8 F8 C0  ;9FF2  PP`.`...
	defb 18 00 60 00 00 30        ;9FFA  ..`..0


; ..........................................................................
; code segment restart 0xA000 - clean decode boundary after data above
; ..........................................................................
;Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player
;into fixed RAM (the master copy lives in RAM bank 1 @0xC000).
;Entry points follow the VTII standard: +0 INIT (HL=module ptr),
;+5 PLAY (call every interrupt). The engine's register-write loop at
;+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data
;stream seen in the port trace (13 OUTs/frame).
VTII_INSTANCE:
	ld hl,0c86eh		;a000	21 6e c8	! n .
M_A003:
	jr $+60			;a003	18 3a		. :
M_A005:
	jp 0c4b9h		;a005	c3 b9 c4	. . .
	jr $+43			;a008	18 29		. )
D_A00A:
	nop			;a00a	00		.
M_A00B:
	nop			;a00b	00		.
	nop			;a00c	00		.
;signature "=VTII PT3 Player r.7="
VTII_SIG:
; signature "=VTII PT3 Player r.7="
; --------------------------------------------------------------------------
; DATA 0xA00D-0xA022  engine signature '=VTII PT3 Player r.7='
; --------------------------------------------------------------------------
DATA_A00D:
	defb 3D 56 54 49 49 20 50 54  ;A00D  =VTII PT
	defb 33 20 50 6C 61 79 65 72  ;A015  3 Player
	defb 20 72 2E 37 3D 21        ;A01D   r.7=!


; ..........................................................................
; code segment restart 0xA023 - clean decode boundary after data above
; ..........................................................................
	ld a,(bc)		;a023	0a		.
M_A024:
	ret nz			;a024	c0		.
	set 7,(hl)		;a025	cb fe		. .
	bit 0,(hl)		;a027	cb 46		. F
	ret z			;a029	c8		.
	pop hl			;a02a	e1		.
	ld hl,0c6a8h		;a02b	21 a8 c6	! . .
M_A02E:
	inc (hl)		;a02e	34		4
	ld hl,0c66ch		;a02f	21 6c c6	! l .
D_A032:
	inc (hl)		;a032	34		4
	xor a			;a033	af		.
	ld h,a			;a034	67		g
	ld l,a			;a035	6f		o
	ld (0c6b6h),a		;a036	32 b6 c6	2 . .
	ld (0c6b7h),hl		;a039	22 b7 c6	" . .
	jp 0c5abh		;a03c	c3 ab c5	. . .
	ld (0c1a8h),hl		;a03f	22 a8 c1	" . .
	ld (0c33eh),hl		;a042	22 3e c3	" > .
D_A045:
	push hl			;a045	e5		.
	ld de,00064h		;a046	11 64 00	. d .
	add hl,de		;a049	19		.
	ld a,(hl)		;a04a	7e		~
	ld (0c545h),a		;a04b	32 45 c5	2 E .
	push hl			;a04e	e5		.
	pop ix			;a04f	dd e1		. .
	add hl,de		;a051	19		.
	ld (0c00bh),hl		;a052	22 0b c0	" . .
	ld e,(ix+002h)		;a055	dd 5e 02	. ^ .
	add hl,de		;a058	19		.
	inc hl			;a059	23		#
	ld (0c4e7h),hl		;a05a	22 e7 c4	" . .
	pop de			;a05d	d1		.
	ld l,(ix+003h)		;a05e	dd 6e 03	. n .
	ld h,(ix+004h)		;a061	dd 66 04	. f .
D_A064:
	add hl,de		;a064	19		.
	ld (0c4f4h),hl		;a065	22 f4 c4	" . .
	ld hl,000a9h		;a068	21 a9 00	! . .
	add hl,de		;a06b	19		.
	ld (0c337h),hl		;a06c	22 37 c3	" 7 .
	ld hl,00069h		;a06f	21 69 00	! i .
	add hl,de		;a072	19		.
	ld (0c1a1h),hl		;a073	22 a1 c1	" . .
	ld hl,0c00ah		;a076	21 0a c0	! . .
	res 7,(hl)		;a079	cb be		. .
	ld de,0c61ch		;a07b	11 1c c6	. . .
	ld bc,0c71fh		;a07e	01 1f c7	. . .
la081h:
	ld a,(de)		;a081	1a		.
	inc de			;a082	13		.
M_A083:
	cp 01eh			;a083	fe 1e		. .
	jr nc,la08dh		;a085	30 06		0 .
	ld h,a			;a087	67		g
	ld a,(de)		;a088	1a		.
	ld l,a			;a089	6f		o
	inc de			;a08a	13		.
	jr la094h		;a08b	18 07		. .
la08dh:
	push de			;a08d	d5		.
	ld d,000h		;a08e	16 00		. .
	ld e,a			;a090	5f		_
	add hl,de		;a091	19		.
	add hl,de		;a092	19		.
	pop de			;a093	d1		.
la094h:
	ld a,h			;a094	7c		|
	ld (bc),a		;a095	02		.
	dec bc			;a096	0b		.
	ld a,l			;a097	7d		}
	ld (bc),a		;a098	02		.
	dec bc			;a099	0b		.
	sub 0f0h		;a09a	d6 f0		. .
	jr nz,la081h		;a09c	20 e3		  .
	ld hl,0c651h		;a09e	21 51 c6	! Q .
	ld (hl),a		;a0a1	77		w
D_A0A2:
	ld de,0c652h		;a0a2	11 52 c6	. R .
	ld bc,0006ch		;a0a5	01 6c 00	. l .
	ldir			;a0a8	ed b0		. .
	inc a			;a0aa	3c		<
	ld (0c6a8h),a		;a0ab	32 a8 c6	2 . .
	ld hl,0f001h		;a0ae	21 01 f0	! . .
	ld (0c66ch),hl		;a0b1	22 6c c6	" l .
	ld (0c689h),hl		;a0b4	22 89 c6	" . .
	ld (0c6a6h),hl		;a0b7	22 a6 c6	" . .
	ld hl,0c618h		;a0ba	21 18 c6	! . .
	ld (0c4d1h),hl		;a0bd	22 d1 c4	" . .
	ld (0c65eh),hl		;a0c0	22 5e c6	" ^ .
D_A0C3:
	ld (0c67bh),hl		;a0c3	22 7b c6	" { .
	ld (0c698h),hl		;a0c6	22 98 c6	" . .
	ld (0c660h),hl		;a0c9	22 60 c6	" ` .
	ld (0c67dh),hl		;a0cc	22 7d c6	" } .
	ld (0c69ah),hl		;a0cf	22 9a c6	" . .
	ld a,(ix-057h)		;a0d2	dd 7e a9	. ~ .
	sub 030h		;a0d5	d6 30		. 0
	jr c,la0ddh		;a0d7	38 04		8 .
	cp 00ah			;a0d9	fe 0a		. .
	jr c,la0dfh		;a0db	38 02		8 .
la0ddh:
	ld a,006h		;a0dd	3e 06		> .
la0dfh:
	ld (0c28dh),a		;a0df	32 8d c2	2 . .
	push af			;a0e2	f5		.
	cp 004h			;a0e3	fe 04		. .
	ld a,(ix-001h)		;a0e5	dd 7e ff	. ~ .
	rla			;a0e8	17		.
	and 007h		;a0e9	e6 07		. .
	ld hl,0c5c8h		;a0eb	21 c8 c5	! . .
	push de			;a0ee	d5		.
	ld d,b			;a0ef	50		P
	add a,a			;a0f0	87		.
	ld e,a			;a0f1	5f		_
	add hl,de		;a0f2	19		.
	ld e,(hl)		;a0f3	5e		^
	inc hl			;a0f4	23		#
	srl e			;a0f5	cb 3b		. ;
	sbc a,a			;a0f7	9f		.
	and 0a7h		;a0f8	e6 a7		. .
	ld (0c120h),a		;a0fa	32 20 c1	2   .
	ex de,hl		;a0fd	eb		.
	pop bc			;a0fe	c1		.
	add hl,bc		;a0ff	09		.
D_A100:
	ld a,(de)		;a100	1a		.
	add a,0d8h		;a101	c6 d8		. .
	ld c,a			;a103	4f		O
	adc a,0c5h		;a104	ce c5		. .
	sub c			;a106	91		.
	ld b,a			;a107	47		G
	push bc			;a108	c5		.
	ld de,0c7aeh		;a109	11 ae c7	. . .
	push de			;a10c	d5		.
	ld b,00ch		;a10d	06 0c		. .
la10fh:
	push bc			;a10f	c5		.
	ld c,(hl)		;a110	4e		N
	inc hl			;a111	23		#
	push hl			;a112	e5		.
	ld b,(hl)		;a113	46		F
	push de			;a114	d5		.
	ex de,hl		;a115	eb		.
	ld de,00017h		;a116	11 17 00	. . .
	defb 0ddh,026h,008h ;ld ixh,008h	;a119	dd 26 08	. & .
la11ch:
	srl b			;a11c	cb 38		. 8
	rr c			;a11e	cb 19		. .
	add hl,de		;a120	19		.
	ld a,c			;a121	79		y
	adc a,d			;a122	8a		.
	ld (hl),a		;a123	77		w
	inc hl			;a124	23		#
	ld a,b			;a125	78		x
	adc a,d			;a126	8a		.
	ld (hl),a		;a127	77		w
	add hl,de		;a128	19		.
	defb 0ddh,025h ;dec ixh	;a129	dd 25		. %
	jr nz,la11ch		;a12b	20 ef		  .
	pop de			;a12d	d1		.
	inc de			;a12e	13		.
	inc de			;a12f	13		.
	pop hl			;a130	e1		.
	inc hl			;a131	23		#
	pop bc			;a132	c1		.
	djnz la10fh		;a133	10 da		. .
	pop hl			;a135	e1		.
	pop de			;a136	d1		.
	ld a,e			;a137	7b		{
	cp 0e4h			;a138	fe e4		. .
	jr nz,la141h		;a13a	20 05		  .
	ld a,0fdh		;a13c	3e fd		> .
	ld (0c7dch),a		;a13e	32 dc c7	2 . .
la141h:
	ld a,(de)		;a141	1a		.
	and a			;a142	a7		.
	jr z,la156h		;a143	28 11		( .
	rra			;a145	1f		.
	push af			;a146	f5		.
	add a,a			;a147	87		.
	ld c,a			;a148	4f		O
	add hl,bc		;a149	09		.
	pop af			;a14a	f1		.
	jr nc,la14fh		;a14b	30 02		0 .
	dec (hl)		;a14d	35		5
	dec (hl)		;a14e	35		5
la14fh:
	inc (hl)		;a14f	34		4
	and a			;a150	a7		.
	sbc hl,bc		;a151	ed 42		. B
	inc de			;a153	13		.
	jr la141h		;a154	18 eb		. .
la156h:
	pop af			;a156	f1		.
	cp 005h			;a157	fe 05		. .
	ld hl,00011h		;a159	21 11 00	! . .
	ld d,h			;a15c	54		T
	ld e,h			;a15d	5c		\
	ld a,017h		;a15e	3e 17		> .
	jr nc,la165h		;a160	30 03		0 .
	dec l			;a162	2d		-
	ld e,l			;a163	5d		]
	xor a			;a164	af		.
la165h:
	ld (0c174h),a		;a165	32 74 c1	2 t .
	ld ix,0c6beh		;a168	dd 21 be c6	. ! . .
	ld c,010h		;a16c	0e 10		. .
la16eh:
	push hl			;a16e	e5		.
	add hl,de		;a16f	19		.
	ex de,hl		;a170	eb		.
	sbc hl,hl		;a171	ed 62		. b
la173h:
	ld a,l			;a173	7d		}
	ld a,l			;a174	7d		}
	ld a,h			;a175	7c		|
	adc a,000h		;a176	ce 00		. .
	ld (ix+000h),a		;a178	dd 77 00	. w .
	inc ix			;a17b	dd 23		. #
	add hl,de		;a17d	19		.
	inc c			;a17e	0c		.
	ld a,c			;a17f	79		y
	and 00fh		;a180	e6 0f		. .
	jr nz,la173h		;a182	20 ef		  .
	pop hl			;a184	e1		.
	ld a,e			;a185	7b		{
	cp 077h			;a186	fe 77		. w
	jr nz,la18bh		;a188	20 01		  .
	inc e			;a18a	1c		.
la18bh:
	ld a,c			;a18b	79		y
	and a			;a18c	a7		.
	jr nz,la16eh		;a18d	20 df		  .
	jp 0c5abh		;a18f	c3 ab c5	. . .
la192h:
	ld (ix+008h),000h	;a192	dd 36 08 00	. 6 . .
	call 0c32fh		;a196	cd 2f c3	. / .
	ld a,(bc)		;a199	0a		.
	inc bc			;a19a	03		.
	rrca			;a19b	0f		.
la19ch:
	add a,a			;a19c	87		.
la19dh:
	ld e,a			;a19d	5f		_
	ld d,000h		;a19e	16 00		. .
	ld hl,02121h		;a1a0	21 21 21	! ! !
	add hl,de		;a1a3	19		.
	ld e,(hl)		;a1a4	5e		^
	inc hl			;a1a5	23		#
	ld d,(hl)		;a1a6	56		V
	ld hl,02121h		;a1a7	21 21 21	! ! !
	add hl,de		;a1aa	19		.
	ld (ix+003h),l		;a1ab	dd 75 03	. u .
	ld (ix+004h),h		;a1ae	dd 74 04	. t .
	jr la1f4h		;a1b1	18 41		. A
la1b3h:
	rlca			;a1b3	07		.
	rlca			;a1b4	07		.
	rlca			;a1b5	07		.
	rlca			;a1b6	07		.
	ld (ix+010h),a		;a1b7	dd 77 10	. w .
	jr la1f7h		;a1ba	18 3b		. ;
la1bch:
	ld (ix+008h),a		;a1bc	dd 77 08	. w .
	ld (ix-00ch),a		;a1bf	dd 77 f4	. w .
D_A1C2:
	jr la1f7h		;a1c2	18 33		. 3
la1c4h:
	dec a			;a1c4	3d		=
	jr nz,la1ceh		;a1c5	20 07		  .
	ld a,(bc)		;a1c7	0a		.
	inc bc			;a1c8	03		.
	ld (ix+005h),a		;a1c9	dd 77 05	. w .
	jr la1f7h		;a1cc	18 29		. )
la1ceh:
	call 0c313h		;a1ce	cd 13 c3	. . .
	jr la1f7h		;a1d1	18 24		. $
la1d3h:
	call 0c32fh		;a1d3	cd 2f c3	. / .
M_A1D6:
	jr la1f4h		;a1d6	18 1c		. .
la1d8h:
	ld (ix+008h),a		;a1d8	dd 77 08	. w .
	ld (ix-00ch),a		;a1db	dd 77 f4	. w .
	call nz,0c313h		;a1de	c4 13 c3	. . .
	ld a,(bc)		;a1e1	0a		.
	inc bc			;a1e2	03		.
	jr la19dh		;a1e3	18 b8		. .
	ld a,(ix+006h)		;a1e5	dd 7e 06	. ~ .
	ld (0c271h),a		;a1e8	32 71 c2	2 q .
	ld l,(ix-006h)		;a1eb	dd 6e fa	. n .
	ld h,(ix-005h)		;a1ee	dd 66 fb	. f .
	ld (0c293h),hl		;a1f1	22 93 c2	" . .
la1f4h:
	ld de,02010h		;a1f4	11 10 20	. .  
la1f7h:
	ld a,(bc)		;a1f7	0a		.
	inc bc			;a1f8	03		.
	add a,e			;a1f9	83		.
	jr c,la192h		;a1fa	38 96		8 .
	add a,d			;a1fc	82		.
	jr z,la248h		;a1fd	28 49		( I
	jr c,la19ch		;a1ff	38 9b		8 .
	add a,e			;a201	83		.
	jr z,la229h		;a202	28 25		( %
	jr c,la1b3h		;a204	38 ad		8 .
	add a,e			;a206	83		.
	jr z,la1bch		;a207	28 b3		( .
	jr c,la1c4h		;a209	38 b9		8 .
	add a,060h		;a20b	c6 60		. `
	jr c,la22fh		;a20d	38 20		8  
	add a,e			;a20f	83		.
	jr c,la1d3h		;a210	38 c1		8 .
	add a,d			;a212	82		.
	jr c,la224h		;a213	38 0f		8 .
	add a,e			;a215	83		.
	jr c,la1d8h		;a216	38 c0		8 .
	add a,a			;a218	87		.
	ld e,a			;a219	5f		_
	ld hl,D_A268		;a21a	21 68 a2	! h .
	add hl,de		;a21d	19		.
	ld e,(hl)		;a21e	5e		^
	inc hl			;a21f	23		#
	ld d,(hl)		;a220	56		V
	push de			;a221	d5		.
	jr la1f4h		;a222	18 d0		. .
la224h:
	ld (0c6ach),a		;a224	32 ac c6	2 . .
	jr la1f7h		;a227	18 ce		. .
la229h:
	res 0,(ix+009h)		;a229	dd cb 09 86	. . . .
	jr la237h		;a22d	18 08		. .
la22fh:
	ld (ix+006h),a		;a22f	dd 77 06	. w .
	set 0,(ix+009h)		;a232	dd cb 09 c6	. . . .
	xor a			;a236	af		.
la237h:
	ld (0c246h),sp		;a237	ed 73 46 c2	. s F .
	ld sp,ix		;a23b	dd f9		. .
	ld h,a			;a23d	67		g
	ld l,a			;a23e	6f		o
	push hl			;a23f	e5		.
	push hl			;a240	e5		.
	push hl			;a241	e5		.
	push hl			;a242	e5		.
	push hl			;a243	e5		.
	push hl			;a244	e5		.
	ld sp,03131h		;a245	31 31 31	1 1 1
la248h:
	ld a,(ix+005h)		;a248	dd 7e 05	. ~ .
	ld (ix+00fh),a		;a24b	dd 77 0f	. w .
	ret			;a24e	c9		.
	res 2,(ix+009h)		;a24f	dd cb 09 96	. . . .
	ld a,(bc)		;a253	0a		.
	inc bc			;a254	03		.
	inc bc			;a255	03		.
	inc bc			;a256	03		.
	ld (ix+00ah),a		;a257	dd 77 0a	. w .
	ld (ix-007h),a		;a25a	dd 77 f9	. w .
	ld de,0c7aeh		;a25d	11 ae c7	. . .
	ld a,(ix+006h)		;a260	dd 7e 06	. ~ .
	ld (ix+007h),a		;a263	dd 77 07	. w .
	add a,a			;a266	87		.
	ld l,a			;a267	6f		o
D_A268:
	ld h,000h		;a268	26 00		& .
	add hl,de		;a26a	19		.
	ld a,(hl)		;a26b	7e		~
	inc hl			;a26c	23		#
	ld h,(hl)		;a26d	66		f
	ld l,a			;a26e	6f		o
	push hl			;a26f	e5		.
	ld a,03eh		;a270	3e 3e		> >
	ld (ix+006h),a		;a272	dd 77 06	. w .
	add a,a			;a275	87		.
	ld l,a			;a276	6f		o
	ld h,000h		;a277	26 00		& .
	add hl,de		;a279	19		.
	ld e,(hl)		;a27a	5e		^
	inc hl			;a27b	23		#
	ld d,(hl)		;a27c	56		V
	pop hl			;a27d	e1		.
	sbc hl,de		;a27e	ed 52		. R
	ld (ix+00dh),l		;a280	dd 75 0d	. u .
	ld (ix+00eh),h		;a283	dd 74 0e	. t .
	ld e,(ix-006h)		;a286	dd 5e fa	. ^ .
	ld d,(ix-005h)		;a289	dd 56 fb	. V .
	ld a,03eh		;a28c	3e 3e		> >
	cp 006h			;a28e	fe 06		. .
	jr c,la29bh		;a290	38 09		8 .
	ld de,01111h		;a292	11 11 11	. . .
	ld (ix-006h),e		;a295	dd 73 fa	. s .
	ld (ix-005h),d		;a298	dd 72 fb	. r .
la29bh:
	ld a,(bc)		;a29b	0a		.
	inc bc			;a29c	03		.
	ex af,af'		;a29d	08		.
	ld a,(bc)		;a29e	0a		.
	inc bc			;a29f	03		.
	and a			;a2a0	a7		.
	jr z,la2a4h		;a2a1	28 01		( .
	ex de,hl		;a2a3	eb		.
la2a4h:
	sbc hl,de		;a2a4	ed 52		. R
	jp p,0c2aeh		;a2a6	f2 ae c2	. . .
	cpl			;a2a9	2f		/
	ex af,af'		;a2aa	08		.
	neg			;a2ab	ed 44		. D
	ex af,af'		;a2ad	08		.
la2aeh:
	ld (ix+00ch),a		;a2ae	dd 77 0c	. w .
	ex af,af'		;a2b1	08		.
	ld (ix+00bh),a		;a2b2	dd 77 0b	. w .
	ld (ix-002h),000h	;a2b5	dd 36 fe 00	. 6 . .
	ret			;a2b9	c9		.
	set 2,(ix+009h)		;a2ba	dd cb 09 d6	. . . .
	ld a,(bc)		;a2be	0a		.
	inc bc			;a2bf	03		.
	ld (ix+00ah),a		;a2c0	dd 77 0a	. w .
	and a			;a2c3	a7		.
	jr nz,la2cdh		;a2c4	20 07		  .
	ld a,(0c28dh)		;a2c6	3a 8d c2	: . .
	cp 007h			;a2c9	fe 07		. .
	sbc a,a			;a2cb	9f		.
	inc a			;a2cc	3c		<
la2cdh:
	ld (ix-007h),a		;a2cd	dd 77 f9	. w .
	ld a,(bc)		;a2d0	0a		.
	inc bc			;a2d1	03		.
	ex af,af'		;a2d2	08		.
	ld a,(bc)		;a2d3	0a		.
	inc bc			;a2d4	03		.
	jr la2aeh		;a2d5	18 d7		. .
	ld a,(bc)		;a2d7	0a		.
	inc bc			;a2d8	03		.
	ld (ix-00bh),a		;a2d9	dd 77 f5	. w .
	ret			;a2dc	c9		.
	ld a,(bc)		;a2dd	0a		.
	inc bc			;a2de	03		.
	ld (ix-00ch),a		;a2df	dd 77 f4	. w .
	ret			;a2e2	c9		.
	ld a,(bc)		;a2e3	0a		.
	inc bc			;a2e4	03		.
	ld (ix-001h),a		;a2e5	dd 77 ff	. w .
	ld (ix-002h),a		;a2e8	dd 77 fe	. w .
	ld a,(bc)		;a2eb	0a		.
	inc bc			;a2ec	03		.
	ld (ix+000h),a		;a2ed	dd 77 00	. w .
	xor a			;a2f0	af		.
	ld (ix-007h),a		;a2f1	dd 77 f9	. w .
	ld (ix-006h),a		;a2f4	dd 77 fa	. w .
	ld (ix-005h),a		;a2f7	dd 77 fb	. w .
	ret			;a2fa	c9		.
	ld a,(bc)		;a2fb	0a		.
	inc bc			;a2fc	03		.
	ld (0c5a1h),a		;a2fd	32 a1 c5	2 . .
	ld (0c6abh),a		;a300	32 ab c6	2 . .
	ld a,(bc)		;a303	0a		.
	inc bc			;a304	03		.
	ld l,a			;a305	6f		o
	ld a,(bc)		;a306	0a		.
	inc bc			;a307	03		.
	ld h,a			;a308	67		g
	ld (0c5a4h),hl		;a309	22 a4 c5	" . .
	ret			;a30c	c9		.
	ld a,(bc)		;a30d	0a		.
	inc bc			;a30e	03		.
	ld (0c545h),a		;a30f	32 45 c5	2 E .
	ret			;a312	c9		.
	ld (ix+008h),e		;a313	dd 73 08	. s .
	ld (0c6bbh),a		;a316	32 bb c6	2 . .
	ld a,(bc)		;a319	0a		.
	inc bc			;a31a	03		.
	ld h,a			;a31b	67		g
	ld a,(bc)		;a31c	0a		.
	inc bc			;a31d	03		.
	ld l,a			;a31e	6f		o
	ld (0c6bch),hl		;a31f	22 bc c6	" . .
	xor a			;a322	af		.
	ld (ix-00ch),a		;a323	dd 77 f4	. w .
	ld (0c6abh),a		;a326	32 ab c6	2 . .
	ld h,a			;a329	67		g
	ld l,a			;a32a	6f		o
	ld (0c6a9h),hl		;a32b	22 a9 c6	" . .
	ret			;a32e	c9		.
	add a,a			;a32f	87		.
D_A330:
	ld e,a			;a330	5f		_
	ld d,000h		;a331	16 00		. .
	ld (ix-00ch),d		;a333	dd 72 f4	. r .
	ld hl,02121h		;a336	21 21 21	! ! !
	add hl,de		;a339	19		.
	ld e,(hl)		;a33a	5e		^
	inc hl			;a33b	23		#
	ld d,(hl)		;a33c	56		V
	ld hl,02121h		;a33d	21 21 21	! ! !
	add hl,de		;a340	19		.
	ld (ix+001h),l		;a341	dd 75 01	. u .
	ld (ix+002h),h		;a344	dd 74 02	. t .
	ret			;a347	c9		.
	ld l,0c3h		;a348	2e c3		. .
	cp d			;a34a	ba		.
	jp nz,0c24fh		;a34b	c2 4f c2	. O .
	rst 10h			;a34e	d7		.
	jp nz,0c2ddh		;a34f	c2 dd c2	. . .
	ex (sp),hl		;a352	e3		.
	jp nz,0c32eh		;a353	c2 2e c3	. . .
	ld l,0c3h		;a356	2e c3		. .
	ei			;a358	fb		.
	jp nz,0c30dh		;a359	c2 0d c3	. . .
	ld l,0c3h		;a35c	2e c3		. .
	ld l,0c3h		;a35e	2e c3		. .
	ld l,0c3h		;a360	2e c3		. .
	ld l,0c3h		;a362	2e c3		. .
	ld l,0c3h		;a364	2e c3		. .
	ld l,0c3h		;a366	2e c3		. .
	xor a			;a368	af		.
	ld (0c6b8h),a		;a369	32 b8 c6	2 . .
	bit 0,(ix+015h)		;a36c	dd cb 15 46	. . . F
	push hl			;a370	e5		.
	jp z,0c496h		;a371	ca 96 c4	. . .
	ld (0c3e1h),sp		;a374	ed 73 e1 c3	. s . .
	ld l,(ix+00dh)		;a378	dd 6e 0d	. n .
	ld h,(ix+00eh)		;a37b	dd 66 0e	. f .
	ld sp,hl		;a37e	f9		.
	pop de			;a37f	d1		.
	ld h,a			;a380	67		g
	ld a,(ix+000h)		;a381	dd 7e 00	. ~ .
	ld l,a			;a384	6f		o
	add hl,sp		;a385	39		9
	inc a			;a386	3c		<
	cp d			;a387	ba		.
	jr c,la38bh		;a388	38 01		8 .
	ld a,e			;a38a	7b		{
la38bh:
	ld (ix+000h),a		;a38b	dd 77 00	. w .
	ld a,(ix+012h)		;a38e	dd 7e 12	. ~ .
	add a,(hl)		;a391	86		.
	jp p,0c396h		;a392	f2 96 c3	. . .
	xor a			;a395	af		.
	cp 060h			;a396	fe 60		. `
	jr c,la39ch		;a398	38 02		8 .
	ld a,05fh		;a39a	3e 5f		> _
la39ch:
	add a,a			;a39c	87		.
	ex af,af'		;a39d	08		.
	ld l,(ix+00fh)		;a39e	dd 6e 0f	. n .
	ld h,(ix+010h)		;a3a1	dd 66 10	. f .
	ld sp,hl		;a3a4	f9		.
	pop de			;a3a5	d1		.
	ld h,000h		;a3a6	26 00		& .
	ld a,(ix+001h)		;a3a8	dd 7e 01	. ~ .
	ld b,a			;a3ab	47		G
	add a,a			;a3ac	87		.
	add a,a			;a3ad	87		.
	ld l,a			;a3ae	6f		o
	add hl,sp		;a3af	39		9
	ld sp,hl		;a3b0	f9		.
	ld a,b			;a3b1	78		x
	inc a			;a3b2	3c		<
	cp d			;a3b3	ba		.
	jr c,la3b7h		;a3b4	38 01		8 .
	ld a,e			;a3b6	7b		{
la3b7h:
	ld (ix+001h),a		;a3b7	dd 77 01	. w .
	pop bc			;a3ba	c1		.
	pop hl			;a3bb	e1		.
	ld e,(ix+008h)		;a3bc	dd 5e 08	. ^ .
	ld d,(ix+009h)		;a3bf	dd 56 09	. V .
	add hl,de		;a3c2	19		.
	bit 6,b			;a3c3	cb 70		. p
	jr z,la3cdh		;a3c5	28 06		( .
	ld (ix+008h),l		;a3c7	dd 75 08	. u .
	ld (ix+009h),h		;a3ca	dd 74 09	. t .
la3cdh:
	ex de,hl		;a3cd	eb		.
	ex af,af'		;a3ce	08		.
	ld l,a			;a3cf	6f		o
	ld h,000h		;a3d0	26 00		& .
	ld sp,0c7aeh		;a3d2	31 ae c7	1 . .
	add hl,sp		;a3d5	39		9
	ld sp,hl		;a3d6	f9		.
	pop hl			;a3d7	e1		.
	add hl,de		;a3d8	19		.
	ld e,(ix+006h)		;a3d9	dd 5e 06	. ^ .
	ld d,(ix+007h)		;a3dc	dd 56 07	. V .
	add hl,de		;a3df	19		.
	ld sp,03131h		;a3e0	31 31 31	1 1 1
	ex (sp),hl		;a3e3	e3		.
	xor a			;a3e4	af		.
	or (ix+005h)		;a3e5	dd b6 05	. . .
	jr z,la428h		;a3e8	28 3e		( >
	dec (ix+005h)		;a3ea	dd 35 05	. 5 .
	jr nz,la428h		;a3ed	20 39		  9
	ld a,(ix+016h)		;a3ef	dd 7e 16	. ~ .
	ld (ix+005h),a		;a3f2	dd 77 05	. w .
	ld l,(ix+017h)		;a3f5	dd 6e 17	. n .
	ld h,(ix+018h)		;a3f8	dd 66 18	. f .
	ld a,h			;a3fb	7c		|
	add hl,de		;a3fc	19		.
	ld (ix+006h),l		;a3fd	dd 75 06	. u .
	ld (ix+007h),h		;a400	dd 74 07	. t .
	bit 2,(ix+015h)		;a403	dd cb 15 56	. . . V
	jr nz,la428h		;a407	20 1f		  .
	ld e,(ix+019h)		;a409	dd 5e 19	. ^ .
	ld d,(ix+01ah)		;a40c	dd 56 1a	. V .
	and a			;a40f	a7		.
	jr z,la413h		;a410	28 01		( .
	ex de,hl		;a412	eb		.
la413h:
	sbc hl,de		;a413	ed 52		. R
	jp m,0c428h		;a415	fa 28 c4	. ( .
	ld a,(ix+013h)		;a418	dd 7e 13	. ~ .
	ld (ix+012h),a		;a41b	dd 77 12	. w .
	xor a			;a41e	af		.
	ld (ix+005h),a		;a41f	dd 77 05	. w .
	ld (ix+006h),a		;a422	dd 77 06	. w .
	ld (ix+007h),a		;a425	dd 77 07	. w .
la428h:
	ld a,(ix+002h)		;a428	dd 7e 02	. ~ .
	bit 7,c			;a42b	cb 79		. y
	jr z,la442h		;a42d	28 13		( .
	bit 6,c			;a42f	cb 71		. q
	jr z,la43ah		;a431	28 07		( .
	cp 00fh			;a433	fe 0f		. .
	jr z,la442h		;a435	28 0b		( .
	inc a			;a437	3c		<
	jr la43fh		;a438	18 05		. .
la43ah:
	cp 0f1h			;a43a	fe f1		. .
	jr z,la442h		;a43c	28 04		( .
	dec a			;a43e	3d		=
la43fh:
	ld (ix+002h),a		;a43f	dd 77 02	. w .
la442h:
	ld l,a			;a442	6f		o
	ld a,b			;a443	78		x
	and 00fh		;a444	e6 0f		. .
	add a,l			;a446	85		.
	jp p,0c44bh		;a447	f2 4b c4	. K .
	xor a			;a44a	af		.
	cp 010h			;a44b	fe 10		. .
	jr c,la451h		;a44d	38 02		8 .
	ld a,00fh		;a44f	3e 0f		> .
la451h:
	or (ix+01ch)		;a451	dd b6 1c	. . .
	ld l,a			;a454	6f		o
	ld h,000h		;a455	26 00		& .
	ld de,0c6aeh		;a457	11 ae c6	. . .
	add hl,de		;a45a	19		.
	ld a,(hl)		;a45b	7e		~
	bit 0,c			;a45c	cb 41		. A
	jr nz,la463h		;a45e	20 03		  .
	or (ix+014h)		;a460	dd b6 14	. . .
la463h:
	ld (0c6b8h),a		;a463	32 b8 c6	2 . .
	bit 7,b			;a466	cb 78		. x
	ld a,c			;a468	79		y
	jr z,la484h		;a469	28 19		( .
	rla			;a46b	17		.
	rla			;a46c	17		.
	sra a			;a46d	cb 2f		. /
	sra a			;a46f	cb 2f		. /
	sra a			;a471	cb 2f		. /
	add a,(ix+004h)		;a473	dd 86 04	. . .
	bit 5,b			;a476	cb 68		. h
	jr z,la47dh		;a478	28 03		( .
	ld (ix+004h),a		;a47a	dd 77 04	. w .
la47dh:
	ld hl,0c585h		;a47d	21 85 c5	! . .
	add a,(hl)		;a480	86		.
	ld (hl),a		;a481	77		w
	jr la492h		;a482	18 0e		. .
la484h:
	rra			;a484	1f		.
	add a,(ix+003h)		;a485	dd 86 03	. . .
	ld (0c6adh),a		;a488	32 ad c6	2 . .
	bit 5,b			;a48b	cb 68		. h
	jr z,la492h		;a48d	28 03		( .
	ld (ix+003h),a		;a48f	dd 77 03	. w .
la492h:
	ld a,b			;a492	78		x
	rra			;a493	1f		.
	and 048h		;a494	e6 48		. H
	ld hl,0c6b5h		;a496	21 b5 c6	! . .
	or (hl)			;a499	b6		.
	rrca			;a49a	0f		.
	ld (hl),a		;a49b	77		w
	pop hl			;a49c	e1		.
	xor a			;a49d	af		.
	or (ix+00ah)		;a49e	dd b6 0a	. . .
	ret z			;a4a1	c8		.
	dec (ix+00ah)		;a4a2	dd 35 0a	. 5 .
	ret nz			;a4a5	c0		.
	xor (ix+015h)		;a4a6	dd ae 15	. . .
	ld (ix+015h),a		;a4a9	dd 77 15	. w .
	rra			;a4ac	1f		.
	ld a,(ix+00bh)		;a4ad	dd 7e 0b	. ~ .
	jr c,la4b5h		;a4b0	38 03		8 .
	ld a,(ix+00ch)		;a4b2	dd 7e 0c	. ~ .
la4b5h:
	ld (ix+00ah),a		;a4b5	dd 77 0a	. w .
	ret			;a4b8	c9		.
	xor a			;a4b9	af		.
	ld (0c585h),a		;a4ba	32 85 c5	2 . .
	ld (0c6b5h),a		;a4bd	32 b5 c6	2 . .
	dec a			;a4c0	3d		=
	ld (0c6bbh),a		;a4c1	32 bb c6	2 . .
	ld hl,0c6a8h		;a4c4	21 a8 c6	! . .
	dec (hl)		;a4c7	35		5
	jr nz,la549h		;a4c8	20 7f		  .
	ld hl,0c66ch		;a4ca	21 6c c6	! l .
	dec (hl)		;a4cd	35		5
	jr nz,la51ch		;a4ce	20 4c		  L
	ld bc,00101h		;a4d0	01 01 01	. . .
	ld a,(bc)		;a4d3	0a		.
	and a			;a4d4	a7		.
	jr nz,la511h		;a4d5	20 3a		  :
	ld d,a			;a4d7	57		W
	ld (0c6ach),a		;a4d8	32 ac c6	2 . .
	ld hl,(0c00bh)		;a4db	2a 0b c0	* . .
	inc hl			;a4de	23		#
	ld a,(hl)		;a4df	7e		~
	inc a			;a4e0	3c		<
	jr nz,la4ebh		;a4e1	20 08		  .
	call 0c022h		;a4e3	cd 22 c0	. " .
	ld hl,02121h		;a4e6	21 21 21	! ! !
	ld a,(hl)		;a4e9	7e		~
	inc a			;a4ea	3c		<
la4ebh:
	ld (0c00bh),hl		;a4eb	22 0b c0	" . .
	dec a			;a4ee	3d		=
	add a,a			;a4ef	87		.
	ld e,a			;a4f0	5f		_
	rl d			;a4f1	cb 12		. .
	ld hl,02121h		;a4f3	21 21 21	! ! !
	add hl,de		;a4f6	19		.
	ld de,(0c1a8h)		;a4f7	ed 5b a8 c1	. [ . .
	ld (0c50fh),sp		;a4fb	ed 73 0f c5	. s . .
	ld sp,hl		;a4ff	f9		.
D_A500:
	pop hl			;a500	e1		.
	add hl,de		;a501	19		.
	ld b,h			;a502	44		D
	ld c,l			;a503	4d		M
	pop hl			;a504	e1		.
	add hl,de		;a505	19		.
	ld (0c527h),hl		;a506	22 27 c5	" ' .
	pop hl			;a509	e1		.
	add hl,de		;a50a	19		.
	ld (0c53bh),hl		;a50b	22 3b c5	" ; .
	ld sp,03131h		;a50e	31 31 31	1 1 1
la511h:
	ld ix,0c65dh		;a511	dd 21 5d c6	. ! ] .
	call 0c1e5h		;a515	cd e5 c1	. . .
	ld (0c4d1h),bc		;a518	ed 43 d1 c4	. C . .
la51ch:
	ld hl,0c689h		;a51c	21 89 c6	! . .
	dec (hl)		;a51f	35		5
	jr nz,la530h		;a520	20 0e		  .
	ld ix,0c67ah		;a522	dd 21 7a c6	. ! z .
	ld bc,00101h		;a526	01 01 01	. . .
	call 0c1e5h		;a529	cd e5 c1	. . .
	ld (0c527h),bc		;a52c	ed 43 27 c5	. C ' .
la530h:
	ld hl,0c6a6h		;a530	21 a6 c6	! . .
	dec (hl)		;a533	35		5
	jr nz,D_A544		;a534	20 0e		  .
	ld ix,0c697h		;a536	dd 21 97 c6	. ! . .
	ld bc,00101h		;a53a	01 01 01	. . .
	call 0c1e5h		;a53d	cd e5 c1	. . .
	ld (0c53bh),bc		;a540	ed 43 3b c5	. C ; .
D_A544:
	ld a,03eh		;a544	3e 3e		> >
	ld (0c6a8h),a		;a546	32 a8 c6	2 . .
la549h:
	ld ix,0c651h		;a549	dd 21 51 c6	. ! Q .
	ld hl,(0c6aeh)		;a54d	2a ae c6	* . .
	call 0c368h		;a550	cd 68 c3	. h .
	ld (0c6aeh),hl		;a553	22 ae c6	" . .
	ld a,(0c6b8h)		;a556	3a b8 c6	: . .
	ld (0c6b6h),a		;a559	32 b6 c6	2 . .
	ld ix,0c66eh		;a55c	dd 21 6e c6	. ! n .
	ld hl,(0c6b0h)		;a560	2a b0 c6	* . .
	call 0c368h		;a563	cd 68 c3	. h .
	ld (0c6b0h),hl		;a566	22 b0 c6	" . .
	ld a,(0c6b8h)		;a569	3a b8 c6	: . .
	ld (0c6b7h),a		;a56c	32 b7 c6	2 . .
	ld ix,0c68bh		;a56f	dd 21 8b c6	. ! . .
	ld hl,(0c6b2h)		;a573	2a b2 c6	* . .
	call 0c368h		;a576	cd 68 c3	. h .
	ld (0c6b2h),hl		;a579	22 b2 c6	" . .
	ld hl,(0c6ach)		;a57c	2a ac c6	* . .
	ld a,h			;a57f	7c		|
	add a,l			;a580	85		.
	ld (0c6b4h),a		;a581	32 b4 c6	2 . .
	ld a,03eh		;a584	3e 3e		> >
	ld e,a			;a586	5f		_
	add a,a			;a587	87		.
	sbc a,a			;a588	9f		.
	ld d,a			;a589	57		W
	ld hl,(0c6bch)		;a58a	2a bc c6	* . .
	add hl,de		;a58d	19		.
	ld de,(0c6a9h)		;a58e	ed 5b a9 c6	. [ . .
	add hl,de		;a592	19		.
	ld (0c6b9h),hl		;a593	22 b9 c6	" . .
	xor a			;a596	af		.
	ld hl,0c6abh		;a597	21 ab c6	! . .
	or (hl)			;a59a	b6		.
	jr z,$+16		;a59b	28 0e		( .
	dec (hl)		;a59d	35		5
	jr nz,$+12		;a59e	20 0a		  .
; --------------------------------------------------------------------------
; DATA 0xA5A0-0xA8C2  VTII engine tail tables (note/volume delay tables)
; --------------------------------------------------------------------------
DATA_A5A0:
	defb 3E 3E 77 21 21 21 19 22  ;A5A0  >>w!!!."
	defb A9 C6 AF 11 BF FF 01 FD  ;A5A8  ........
	defb FF 21 AE C6 ED 79 43 ED  ;A5B0  .!...yC.
	defb A3 42 3C FE 0D 20 F5 ED  ;A5B8  .B<.. ..
	defb 79 7E A7 F8 43 ED 79 C9  ;A5C0  y~..C.y.
	defb 64 2A 65 00 01 0C 01 0C  ;A5C8  d*e.....
	defb 94 35 30 0E 60 20 60 21  ;A5D0  .50.` `!
	defb 01 05 09 0B 0D 0F 13 15  ;A5D8  ........
	defb 19 25 3D 00 5D 00 31 37  ;A5E0  .%=.].17
	defb 4D 53 5F 71 82 8C 9C 9E  ;A5E8  MS_q....
	defb A0 A6 A8 AA AC AE AE 00  ;A5F0  ........
	defb 57 1F 23 25 29 2D 2F 33  ;A5F8  W.#%)-/3
	defb BF 00 1D 21 23 27 2B 2D  ;A600  ...!#'+-
	defb 31 55 BD BF 00 1B 21 25  ;A608  1U....!%
	defb 29 2B 3B 4D 5F BB BD BF  ;A610  )+;M_...
	defb 00 01 00 90 0D D8 69 70  ;A618  ......ip
	defb 76 7D 85 8D 95 9D A8 B1  ;A620  v}......
	defb BB 0C DA 62 68 6D 75 7B  ;A628  ...bhmu{
	defb 83 8A 92 9C A4 AF B8 0E  ;A630  ........
	defb 08 6A 72 78 7E 86 90 96  ;A638  .jrx~...
	defb A0 AA B4 BE 0F C0 78 88  ;A640  ......x.
	defb 80 90 98 A0 B0 A8 E0 B0  ;A648  ........
	defb E8 09 D0 02 09 54 03 09  ;A650  .....T..
	defb D0 02 27 09 D0 03 22 B1  ;A658  ..'...".
	defb 02 D0 09 B1 01 D0 02 09  ;A660  ........
	defb 54 03 09 D0 02 09 D0 03  ;A668  T.......
	defb 09 D0 02 27 09 54 03 22  ;A670  ...'.T."
	defb B1 02 D0 09 B1 01 D0 02  ;A678  ........
	defb 09 D0 03 09 D0 02 09 54  ;A680  .......T
	defb 03 09 D0 02 27 09 D0 03  ;A688  ....'...
	defb 22 B1 02 D0 09 B1 01 D0  ;A690  ".......
	defb 02 09 54 03 09 D0 02 09  ;A698  ..T.....
	defb D0 03 09 D0 02 27 09 54  ;A6A0  .....'.T
	defb 03 22 B1 02 D0 09 B1 01  ;A6A8  ."......
	defb D0 02 09 D0 03 09 D0 02  ;A6B0  ........
	defb 09 54 03 09 D0 02 27 09  ;A6B8  .T....'.
	defb D0 03 22 B1 02 D0 09 B1  ;A6C0  ..".....
	defb 01 D0 02 09 54 03 09 D0  ;A6C8  ....T...
	defb 02 09 D0 03 09 D0 02 27  ;A6D0  .......'
	defb 09 54 03 22 B1 02 D0 09  ;A6D8  .T."....
	defb B1 01 D0 02 09 D0 03 09  ;A6E0  ........
	defb D0 02 09 54 03 09 D0 02  ;A6E8  ...T....
	defb 20 09 B1 03 D0 03 09 B1  ;A6F0   .......
	defb 01 D0 02 09 54 03 09 D0  ;A6F8  ....T...
	defb 02 09 D0 03 09 D0 02 00  ;A700  ........
	defb F0 10 CF B1 08 71 CB 71  ;A708  .....q.q
	defb C9 71 C7 71 C5 71 C3 71  ;A710  .q.q.q.q
	defb C1 71 C0 00 B1 40 D0 00  ;A718  .q...@..
	defb CE 09 B1 03 54 03 09 B1  ;A720  ....T...
	defb 01 D0 02 09 D0 03 09 D0  ;A728  ........
	defb 02 09 54 03 09 D0 02 CD  ;A730  ..T.....
	defb 09 B1 03 D0 03 09 B1 01  ;A738  ........
	defb D0 02 09 54 03 09 D0 02  ;A740  ...T....
	defb 09 D0 03 09 D0 02 CC 09  ;A748  ........
	defb B1 03 54 03 09 B1 01 D0  ;A750  ..T.....
	defb 02 09 D0 03 09 D0 02 09  ;A758  ........
	defb 54 03 09 D0 02 CB 09 B1  ;A760  T.......
	defb 03 D0 03 09 B1 01 D0 02  ;A768  ........
	defb 09 54 03 09 D0 02 09 D0  ;A770  .T......
	defb 03 09 D0 02 CA 09 B1 03  ;A778  ........
	defb 54 03 09 B1 01 D0 02 09  ;A780  T.......
	defb D0 03 09 D0 02 09 54 03  ;A788  ......T.
	defb 09 D0 02 C9 09 B1 03 D0  ;A790  ........
	defb 03 09 B1 01 D0 02 09 54  ;A798  .......T
	defb 03 09 D0 02 09 D0 03 09  ;A7A0  ........
	defb D0 02 C8 09 B1 03 54 03  ;A7A8  ......T.
	defb 09 B1 01 D0 02 09 D0 03  ;A7B0  ........
	defb 09 D0 02 D1 09 54 03 09  ;A7B8  .....T..
	defb D0 02 C7 09 D0 03 01 D0  ;A7C0  ........
	defb 01 10 00 C6 D0 C5 09 D0  ;A7C8  ........
	defb 02 C4 09 D0 03 C3 09 D0  ;A7D0  ........
	defb 02 C2 09 D0 03 C1 09 D0  ;A7D8  ........
	defb 02 00 01 02 00 0F 00 00  ;A7E0  ........
	defb 00 8F 00 00 00 01 81 0F  ;A7E8  ........
	defb 00 00 01 03 01 0F 00 00  ;A7F0  ........
	defb 01 8F 00 00 81 8F 00 00  ;A7F8  ........
	defb 02 03 01 1D 00 00 00 1D  ;A800  ........
	defb 00 00 80 86 00 00 00 06  ;A808  ........
	defb 01 8F 01 00 01 8F 02 00  ;A810  ........
	defb 01 8F 01 00 01 8F FF FF  ;A818  ........
	defb 01 8F FE FF 01 8F FF FF  ;A820  ........
	defb 04 0A 0F 0F 80 00 0D 0F  ;A828  ........
	defb C0 00 09 0E 00 01 05 0E  ;A830  ........
	defb 40 01 01 1D 00 00 01 1D  ;A838  @.......
	defb 00 00 01 1D 00 00 01 1D  ;A840  ........
	defb 00 00 01 1D 00 00 81 1D  ;A848  ........
	defb 00 00 00 0B 01 10 00 00  ;A850  ........
	defb 01 10 00 00 01 10 00 00  ;A858  ........
	defb 01 10 00 00 01 10 00 00  ;A860  ........
	defb 01 10 00 00 01 10 00 00  ;A868  ........
	defb 01 10 00 00 01 10 00 00  ;A870  ........
	defb 01 10 00 00 C1 10 00 00  ;A878  ........
	defb 00 0A 01 0F 01 00 01 0F  ;A880  ........
	defb 02 00 01 0F 03 00 01 0F  ;A888  ........
	defb 04 00 01 0F 03 00 01 0F  ;A890  ........
	defb FF FF 01 0F FE FF 01 0F  ;A898  ........
	defb FD FF 01 0F FC FF 01 0F  ;A8A0  ........
	defb FD FF 00 01 00 03 04 13  ;A8A8  ........
	defb 00 0C 00 00 03 F8 FD 00  ;A8B0  ........
	defb 03 04 13 00 13 00 02 03  ;A8B8  ........
	defb 00 0C 00                 ;A8C0  ...

