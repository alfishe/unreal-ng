; ==========================================================================
; UMT v2.3x - universal memory tester for ZX Spectrum clones
; binary: umt-unpacked-6000.bin  org 0x6000  program len 0x24FA
; source: testdata/memory/UMT23X.tap (single MegaLZ-packed block,
;         depacked by the built-in interpreter in this script)
; 
; Machine-generated z80dasm reference with hand-curated symbols.
; The analysis article lives in memory-addressing.md; package
; overview in README.md (same folder).
; 
; Tests RAM of 15 models: Pentagon1024/512/128K, Scorpion1024/256,
; KAY1024/2048, Profi1024/512, ATM4.5/7.1, PentEvo(TSConf),
; Sprinter(4096K), GMX(2048), Spectrum 128K. Four methodologies
; per model: mark (page# uniqueness), pattern (00/55/FF/AA),
; checksum (ROM-seeded chained sums), rotate (RR carry chains).
; 
; READING ORDER
;   1. ENTRY                       init + main loop
;   2. MODEL_TABLE + TABLE_WORD    per-model routine dispatch
;   3. MAP_* (60C6h-61D2h)         the ports/bits per clone
;   4. MARK_/PAT_/SUM_/ROT_        the four test methodologies
;   5. SELECT_MODEL + text VM      UI (no clone auto-detection!)
;   6. DEPACK_ENTRY (0xBF00h)      resident MegaLZ depacker
; ==========================================================================

	org 0x6000

; --------------------------------------------------------------------------
; symbol table - named anchors (auto labels appear inline)
; --------------------------------------------------------------------------
ENTRY               : equ 0x6000
RESET_TEST_STATE    : equ 0x6037
PAGE_TESTABLE       : equ 0x6044
TEST_PAGE_PATTERNS  : equ 0x6052
PAGE_NUM            : equ 0x60A6
SCREEN_PTR          : equ 0x60A7
SWEEP_PTR           : equ 0x60A9
SWEEP_PTR2          : equ 0x60AB
TABLE_WORD          : equ 0x60AD
CALL_W0             : equ 0x60BF
MAP_TSCONF          : equ 0x60C6
MAP_SPRINTER        : equ 0x60CF
MAP_ATM71           : equ 0x60D8
ATM71_MODEIN        : equ 0x60EA
ATM71_MODEOUT       : equ 0x60F6
MAP_KAY             : equ 0x60FE
MAP_SCORPION        : equ 0x6145
MAP_PENTAGON        : equ 0x6169
MAP_PROFI           : equ 0x6186
MAP_GMX             : equ 0x61A2
MAP_ATM45           : equ 0x61BD
VERIFY16            : equ 0x61D2
FILL_BY_PUSH        : equ 0x629E
CALL_W2             : equ 0x62F1
PAT_128K            : equ 0x62F8
PAT_256K            : equ 0x62FC
PAT_512K            : equ 0x6300
PAT_1M              : equ 0x6304
SCREEN_ROW_SET      : equ 0x6309
PAT_2M              : equ 0x6318
PAT_4M              : equ 0x6327
CALL_W1             : equ 0x634D
MARK_4M_TSCONF      : equ 0x6354
MARK_1M             : equ 0x6378
MARK_2M             : equ 0x637C
MARK_256K           : equ 0x6380
MARK_512K           : equ 0x6384
MARK_128K           : equ 0x6388
MARK_4M_SPRINTER    : equ 0x638C
STEP_SPRINTER       : equ 0x63B0
VERIFY_STEP_SPRINTER: equ 0x63C3
STEP_LINEAR         : equ 0x63D5
VERIFY_STEP_LINEAR  : equ 0x63DE
CALL_W3             : equ 0x63FE
SELECT_ROW          : equ 0x6416
SUM_2M              : equ 0x643C
SUM_4M              : equ 0x6440
SUM_256K            : equ 0x6444
SUM_1M              : equ 0x646D
SUM_512K            : equ 0x6471
SUM_128K            : equ 0x6475
CHECKSUM_WRITE      : equ 0x64A3
CHECKSUM_VERIFY     : equ 0x64DC
CALL_W4             : equ 0x6533
ROT_128K            : equ 0x653A
ROT_256K            : equ 0x6583
ROT_512K            : equ 0x6587
ROT_1M              : equ 0x658B
ROT_2M              : equ 0x658F
ROT_4M              : equ 0x6593
ROTATE_PAGE         : equ 0x65A2
SEED_TABLE_ONCE     : equ 0x66D3
INIT_CHECKSUM_TABLE : equ 0x670F
CLEAR_ATTRS         : equ 0x6819
INIT_TEXT           : equ 0x6853
HELP_PAGE           : equ 0x68A0
SELECT_MODEL        : equ 0x68D5
BUILD_SCREEN_ADDR_TABLE: equ 0x6B87
ATTR_CUR            : equ 0x6BD1
ATTR_TMP            : equ 0x6BD2
TEXT_CURSOR         : equ 0x6BD3
TEXT_BASE           : equ 0x6BD5
TEXT_LIST           : equ 0x6BD7
TEXT_STREAM         : equ 0x6BD9
NAME_BUFFER         : equ 0x6F2E
MODEL_INDEX         : equ 0x6F3F
MODEL_TABLE         : equ 0x6F40
VERSION_STRING      : equ 0x70DA
FONT                : equ 0x7920
TEXT_EN             : equ 0x8000
DEPACK_ENTRY        : equ 0xBF00
END_TABLE           : equ 0xBFE0

; --------------------------------------------------------------------------
; DI; LD A,3Fh; LD I,A; IM 1; LD SP,6000h. Init sequence calls
; BUILD_SCREEN_ADDR_TABLE, INIT_CHECKSUM_TABLE, CLEAR_ATTRS, INIT_TEXT;
; the main loop then repeatedly calls RESET_TEST_STATE, CLEAR_ATTRS,
; HELP_PAGE and the key dispatcher at 66F4h. Exit (key 0) restores
; #7FFD <= 10h (plain ROM0/SCR) and reboots via JP 0000.
; --------------------------------------------------------------------------
ENTRY:
	di			;6000	f3		.
	ld a,03fh		;6001	3e 3f		> ?
	ld i,a			;6003	ed 47		. G
	im 1			;6005	ed 56		. V
	xor a			;6007	af		.
	out (0feh),a		;6008	d3 fe		. .
	ld (06531h),sp		;600a	ed 73 31 65	. s 1 e
	ld sp,ENTRY		;600e	31 00 60	1 . `
	call BUILD_SCREEN_ADDR_TABLE	;6011	cd 87 6b	. . k
	call INIT_CHECKSUM_TABLE	;6014	cd 0f 67	. . g
	call CLEAR_ATTRS	;6017	cd 19 68	. . h
	call INIT_TEXT		;601a	cd 53 68	. S h
l601dh:
	call RESET_TEST_STATE	;601d	cd 37 60	. 7 `
	call CLEAR_ATTRS	;6020	cd 19 68	. . h
	call HELP_PAGE		;6023	cd a0 68	. . h
	jr nz,l601dh		;6026	20 f5		  .
	call R_66F4		;6028	cd f4 66	. . f
	jr nz,l601dh		;602b	20 f0		  .
	ld bc,07ffdh		;602d	01 fd 7f	. . .
	ld a,010h		;6030	3e 10		> .
	out (c),a		;6032	ed 79		. y
	jp 00000h		;6034	c3 00 00	. . .
; --------------------------------------------------------------------------
; Clears per-run state before each test pass.
; --------------------------------------------------------------------------
RESET_TEST_STATE:
	xor a			;6037	af		.
	out (0feh),a		;6038	d3 fe		. .
	ld (06530h),a		;603a	32 30 65	2 0 e
	ld (0652fh),a		;603d	32 2f 65	2 / e
	ld (066f1h),a		;6040	32 f1 66	2 . f
	ret			;6043	c9		.
; --------------------------------------------------------------------------
; Feeds the current page number (PAGE_NUM) through the model's w0
; MAP_PAGE routine and probes the page for writability.
; --------------------------------------------------------------------------
PAGE_TESTABLE:
	ld a,(PAGE_NUM)		;6044	3a a6 60	: . `
	cp 040h			;6047	fe 40		. @
	ret z			;6049	c8		.
	cp 050h			;604a	fe 50		. P
	ret c			;604c	d8		.
	cp 05fh			;604d	fe 5f		. _
	ret nc			;604f	d0		.
	xor a			;6050	af		.
	ret			;6051	c9		.
; --------------------------------------------------------------------------
; Pattern test driver for one page: 00/55/FF/AA writes verified by
; VERIFY16, walking SWEEP_PTR/SCREEN_PTR to lay results on screen.
; --------------------------------------------------------------------------
TEST_PAGE_PATTERNS:
	ld a,(MODEL_INDEX)	;6052	3a 3f 6f	: ? o
	cp 008h			;6055	fe 08		. .
	call z,PAGE_TESTABLE	;6057	cc 44 60	. D `
	jr z,l609bh		;605a	28 3f		( ?
	ld a,(PAGE_NUM)		;605c	3a a6 60	: . `
	cp 005h			;605f	fe 05		. .
	jr z,l609bh		;6061	28 38		( 8
	ld hl,(SWEEP_PTR)	;6063	2a a9 60	* . `
	call FILL_BY_PUSH	;6066	cd 9e 62	. . b
	ld a,(SWEEP_PTR)	;6069	3a a9 60	: . `
	call VERIFY16		;606c	cd d2 61	. . a
	ld a,042h		;606f	3e 42		> B
	jr nz,l609fh		;6071	20 2c		  ,
	ld hl,(SWEEP_PTR2)	;6073	2a ab 60	* . `
	call FILL_BY_PUSH	;6076	cd 9e 62	. . b
	ld a,(SWEEP_PTR2)	;6079	3a ab 60	: . `
	call VERIFY16		;607c	cd d2 61	. . a
	ld a,043h		;607f	3e 43		> C
	jr nz,l609fh		;6081	20 1c		  .
	ld a,044h		;6083	3e 44		> D
R_6085:
	ld hl,PAGE_NUM		;6085	21 a6 60	! . `
	inc (hl)		;6088	34		4
	ld hl,(SCREEN_PTR)	;6089	2a a7 60	* . `
	ld (hl),a		;608c	77		w
	inc hl			;608d	23		#
	ld a,l			;608e	7d		}
	ld de,00018h		;608f	11 18 00	. . .
	and 007h		;6092	e6 07		. .
	jr nz,l6097h		;6094	20 01		  .
	add hl,de		;6096	19		.
l6097h:
	ld (SCREEN_PTR),hl	;6097	22 a7 60	" . `
	ret			;609a	c9		.
l609bh:
	ld a,046h		;609b	3e 46		> F
	jr R_6085		;609d	18 e6		. .
l609fh:
	ld hl,06530h		;609f	21 30 65	! 0 e
	ld (hl),001h		;60a2	36 01		6 .
	jr R_6085		;60a4	18 df		. .

; --------------------------------------------------------------------------
; DATA byte - current 16K page number fed to the model's MAP routine.
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x60A6-0x60AC  test driver variables (PAGE_NUM/SCREEN_PTR/SWEEP_PTR)
; --------------------------------------------------------------------------
PAGE_NUM:
	defb 0x00
SCREEN_PTR:
	defw 0x0000
SWEEP_PTR:
	defw 0x0000
SWEEP_PTR2:
	defb 0x00
	defb 0x00

; --------------------------------------------------------------------------
; HL in = MODEL_TABLE + model*27 + slot*2 - 1; skips model entries
; (27 = 0x1B bytes each) and returns the routine word in HL.
; --------------------------------------------------------------------------
TABLE_WORD:
	ld a,(MODEL_INDEX)	;60ad	3a 3f 6f	: ? o
	or a			;60b0	b7		.
	jr z,l60bah		;60b1	28 07		( .
	ld b,a			;60b3	47		G
	ld de,0001bh		;60b4	11 1b 00	. . .
l60b7h:
	add hl,de		;60b7	19		.
	djnz l60b7h		;60b8	10 fd		. .
l60bah:
	ld e,(hl)		;60ba	5e		^
	inc hl			;60bb	23		#
	ld d,(hl)		;60bc	56		V
	ex de,hl		;60bd	eb		.
	ret			;60be	c9		.
; --------------------------------------------------------------------------
; JP (HL) into the selected model's w0 = MAP_PAGE routine:
; map page PAGE_NUM into window 0xC000 using the clone's ports.
; --------------------------------------------------------------------------
CALL_W0:
	ld hl,06f41h		;60bf	21 41 6f	! A o
	call TABLE_WORD		;60c2	cd ad 60	. . `
	jp (hl)			;60c5	e9		.
; --------------------------------------------------------------------------
; Pentagon Evo / TSConf: OUT (#13AF),page - raw page number, 256
; pages of 16K (4 MiB). Window at #C000. No other bits involved.
; --------------------------------------------------------------------------
MAP_TSCONF:
	ld a,(PAGE_NUM)		;60c6	3a a6 60	: . `
	ld bc,013afh		;60c9	01 af 13	. . .
	out (c),a		;60cc	ed 79		. y
	ret			;60ce	c9		.
; --------------------------------------------------------------------------
; Sprinter: OUT (#00E2),page - raw page number (public docs for
; this port are scarce; taken as UMT-observed). 256 pages, 4 MiB.
; --------------------------------------------------------------------------
MAP_SPRINTER:
	ld a,(PAGE_NUM)		;60cf	3a a6 60	: . `
	ld bc,000e2h		;60d2	01 e2 00	. . .
	out (c),a		;60d5	ed 79		. y
	ret			;60d7	c9		.
; --------------------------------------------------------------------------
; ATM turbo 7.1: paging needs the ProfROM service - OUT (#FD77),0ABh
; (service mode), PUSH 2A53h marker, JP 3D2Fh (ProfROM hook, returns
; in service mode); then OUT (#FFF7),((~page)&3Fh)|40h - the page
; number is COMPLEMENTED and bit6 marks a RAM page - and finally
; OUT (#FF77),0ABh to leave service mode. Every page switch on
; ATM-1 pays this firmware round-trip.
; --------------------------------------------------------------------------
MAP_ATM71:
	call ATM71_MODEIN	;60d8	cd ea 60	. . `
	ld a,(PAGE_NUM)		;60db	3a a6 60	: . `
	cpl			;60de	2f		/
	and 03fh		;60df	e6 3f		. ?
	or 040h			;60e1	f6 40		. @
	ld bc,0fff7h		;60e3	01 f7 ff	. . .
	out (c),a		;60e6	ed 79		. y
	jr ATM71_MODEOUT	;60e8	18 0c		. .
; --------------------------------------------------------------------------
; OUT (#FD77),0ABh - enter ProfROM monitor
; --------------------------------------------------------------------------
ATM71_MODEIN:
	ld bc,0fd77h		;60ea	01 77 fd	. w .
	ld a,0abh		;60ed	3e ab		> .
	ld hl,02a53h		;60ef	21 53 2a	! S *
	push hl			;60f2	e5		.
	jp 03d2fh		;60f3	c3 2f 3d	. / =
; --------------------------------------------------------------------------
; OUT (#FF77),0ABh - leave ProfROM monitor
; --------------------------------------------------------------------------
ATM71_MODEOUT:
	ld bc,0ff77h		;60f6	01 77 ff	. w .
	ld a,0abh		;60f9	3e ab		> .
	out (c),a		;60fb	ed 79		. y
	ret			;60fd	c9		.
; --------------------------------------------------------------------------
; KAY (all sizes): #1FFD bit4=page bit3, bit7=page bit4, bit6=page
; bit6 (SMC operand at 6136h toggles bit6 for 1024K models);
; #7FFD = (page AND 7) OR 10h, bit7 = page bit5 (SMC at 6141h).
; Shares the Scorpion-style high bits in #1FFD.
; --------------------------------------------------------------------------
MAP_KAY:
	ld a,(PAGE_NUM)		;60fe	3a a6 60	: . `
	ld d,a			;6101	57		W
	and 007h		;6102	e6 07		. .
	ld e,a			;6104	5f		_
	ld a,d			;6105	7a		z
	srl a			;6106	cb 3f		. ?
	srl a			;6108	cb 3f		. ?
	srl a			;610a	cb 3f		. ?
	ld bc,00000h		;610c	01 00 00	. . .
	srl a			;610f	cb 3f		. ?
	jr nc,l6115h		;6111	30 02		0 .
	ld b,010h		;6113	06 10		. .
l6115h:
	srl a			;6115	cb 3f		. ?
	jr nc,l611bh		;6117	30 02		0 .
	ld c,080h		;6119	0e 80		. .
l611bh:
	srl a			;611b	cb 3f		. ?
	ld d,a			;611d	57		W
	ld a,010h		;611e	3e 10		> .
	jr nc,l6124h		;6120	30 02		0 .
	ld a,090h		;6122	3e 90		> .
l6124h:
	ld (06141h),a		;6124	32 41 61	2 A a
	ld a,d			;6127	7a		z
	srl a			;6128	cb 3f		. ?
	ld a,000h		;612a	3e 00		> .
	jr nc,l6130h		;612c	30 02		0 .
	ld a,040h		;612e	3e 40		> @
l6130h:
	ld (06136h),a		;6130	32 36 61	2 6 a
	ld a,b			;6133	78		x
	or c			;6134	b1		.
	or 000h			;6135	f6 00		. .
	ld bc,01ffdh		;6137	01 fd 1f	. . .
	out (c),a		;613a	ed 79		. y
	ld bc,07ffdh		;613c	01 fd 7f	. . .
	ld a,e			;613f	7b		{
	or 010h			;6140	f6 10		. .
	out (c),a		;6142	ed 79		. y
	ret			;6144	c9		.
; --------------------------------------------------------------------------
; Scorpion 1024/256: #1FFD bit4=page bit3, bit6=page bit4,
; bit7=page bit5; #7FFD = (page AND 7) OR 10h. The unreal-ng
; portdecoder_scorpion256 implements exactly this mapping.
; --------------------------------------------------------------------------
MAP_SCORPION:
	ld a,(PAGE_NUM)		;6145	3a a6 60	: . `
	ld d,a			;6148	57		W
	and 007h		;6149	e6 07		. .
	ld e,a			;614b	5f		_
	ld a,d			;614c	7a		z
	and 038h		;614d	e6 38		. 8
	sla a			;614f	cb 27		. '
	ld d,a			;6151	57		W
	and 060h		;6152	e6 60		. `
	sla a			;6154	cb 27		. '
	ld b,a			;6156	47		G
	ld a,d			;6157	7a		z
	and 010h		;6158	e6 10		. .
	or b			;615a	b0		.
l615bh:
	ld bc,01ffdh		;615b	01 fd 1f	. . .
	out (c),a		;615e	ed 79		. y
	ld bc,07ffdh		;6160	01 fd 7f	. . .
	ld a,e			;6163	7b		{
	or 010h			;6164	f6 10		. .
	out (c),a		;6166	ed 79		. y
	ret			;6168	c9		.
; --------------------------------------------------------------------------
; Pentagon 1024/512/128K: #7FFD bits 0-2 = page bits 0-2, bit4=1
; (screen in bank 7 area convention), bit6=page bit3, bit7=page
; bit4, bit5=page bit5 (dual-use of the 48K lock bit - see
; memory-addressing.md). Pentagon 128K/512 just ignore the high bits.
; --------------------------------------------------------------------------
MAP_PENTAGON:
	ld a,(PAGE_NUM)		;6169	3a a6 60	: . `
	ld d,a			;616c	57		W
	and 007h		;616d	e6 07		. .
	ld e,a			;616f	5f		_
	ld a,d			;6170	7a		z
	and 038h		;6171	e6 38		. 8
	sla a			;6173	cb 27		. '
	sla a			;6175	cb 27		. '
	sla a			;6177	cb 27		. '
	jr nc,l617dh		;6179	30 02		0 .
	set 5,a			;617b	cb ef		. .
l617dh:
	ld bc,07ffdh		;617d	01 fd 7f	. . .
	or e			;6180	b3		.
	or 010h			;6181	f6 10		. .
	out (c),a		;6183	ed 79		. y
	ret			;6185	c9		.
; --------------------------------------------------------------------------
; Profi 1024/512: #DFFD = page >> 3 (top bits), #7FFD low 3 bits
; are set as in MAP_PENTAGON minus the forced bit4/bit5 extras.
; --------------------------------------------------------------------------
MAP_PROFI:
	ld a,(PAGE_NUM)		;6186	3a a6 60	: . `
	ld d,a			;6189	57		W
	and 007h		;618a	e6 07		. .
	ld e,a			;618c	5f		_
	ld a,d			;618d	7a		z
	srl a			;618e	cb 3f		. ?
	srl a			;6190	cb 3f		. ?
	srl a			;6192	cb 3f		. ?
	ld bc,0dffdh		;6194	01 fd df	. . .
	out (c),a		;6197	ed 79		. y
l6199h:
	ld bc,07ffdh		;6199	01 fd 7f	. . .
	ld a,e			;619c	7b		{
	or 010h			;619d	f6 10		. .
	out (c),a		;619f	ed 79		. y
	ret			;61a1	c9		.
; --------------------------------------------------------------------------
; GMX 2048: #DFFD = page >> 4, #1FFD bit4 = page bit3, #7FFD =
; (page AND 7) OR 10h. Extension of the Profi scheme by one bit.
; --------------------------------------------------------------------------
MAP_GMX:
	ld a,(PAGE_NUM)		;61a2	3a a6 60	: . `
	ld d,a			;61a5	57		W
	and 007h		;61a6	e6 07		. .
	ld e,a			;61a8	5f		_
	ld a,d			;61a9	7a		z
	srl a			;61aa	cb 3f		. ?
	srl a			;61ac	cb 3f		. ?
	srl a			;61ae	cb 3f		. ?
	srl a			;61b0	cb 3f		. ?
	ld bc,0dffdh		;61b2	01 fd df	. . .
	out (c),a		;61b5	ed 79		. y
	ld a,d			;61b7	7a		z
	rlca			;61b8	07		.
	and 010h		;61b9	e6 10		. .
	jr l615bh		;61bb	18 9e		. .
; --------------------------------------------------------------------------
; ATM 4.50 (1024/512): #FDFD = page >> 3, #7FFD = (page AND 7) OR 10h.
; Note #FDFD here vs #DFFD on Profi - adjacent clone conventions.
; --------------------------------------------------------------------------
MAP_ATM45:
	ld a,(PAGE_NUM)		;61bd	3a a6 60	: . `
	ld d,a			;61c0	57		W
	and 007h		;61c1	e6 07		. .
	ld e,a			;61c3	5f		_
	ld a,d			;61c4	7a		z
	srl a			;61c5	cb 3f		. ?
	srl a			;61c7	cb 3f		. ?
	srl a			;61c9	cb 3f		. ?
	ld bc,0fdfdh		;61cb	01 fd fd	. . .
	out (c),a		;61ce	ed 79		. y
	jr l6199h		;61d0	18 c7		. .
; --------------------------------------------------------------------------
; Unrolled 16x CP (HL) against A with early-out; reports the first
; mismatching offset in the screen cell.
; --------------------------------------------------------------------------
VERIFY16:
	di			;61d2	f3		.
	ld hl,0c000h		;61d3	21 00 c0	! . .
	ld b,000h		;61d6	06 00		. .
l61d8h:
	cp (hl)			;61d8	be		.
	ret nz			;61d9	c0		.
	inc hl			;61da	23		#
	cp (hl)			;61db	be		.
	ret nz			;61dc	c0		.
	inc hl			;61dd	23		#
	cp (hl)			;61de	be		.
	ret nz			;61df	c0		.
	inc hl			;61e0	23		#
	cp (hl)			;61e1	be		.
	ret nz			;61e2	c0		.
	inc hl			;61e3	23		#
	cp (hl)			;61e4	be		.
	ret nz			;61e5	c0		.
	inc hl			;61e6	23		#
	cp (hl)			;61e7	be		.
	ret nz			;61e8	c0		.
	inc hl			;61e9	23		#
	cp (hl)			;61ea	be		.
	ret nz			;61eb	c0		.
	inc hl			;61ec	23		#
	cp (hl)			;61ed	be		.
	ret nz			;61ee	c0		.
	inc hl			;61ef	23		#
	cp (hl)			;61f0	be		.
	ret nz			;61f1	c0		.
	inc hl			;61f2	23		#
	cp (hl)			;61f3	be		.
	ret nz			;61f4	c0		.
	inc hl			;61f5	23		#
	cp (hl)			;61f6	be		.
	ret nz			;61f7	c0		.
	inc hl			;61f8	23		#
	cp (hl)			;61f9	be		.
	ret nz			;61fa	c0		.
	inc hl			;61fb	23		#
	cp (hl)			;61fc	be		.
	ret nz			;61fd	c0		.
	inc hl			;61fe	23		#
	cp (hl)			;61ff	be		.
	ret nz			;6200	c0		.
	inc hl			;6201	23		#
	cp (hl)			;6202	be		.
	ret nz			;6203	c0		.
	inc hl			;6204	23		#
	cp (hl)			;6205	be		.
	ret nz			;6206	c0		.
	inc hl			;6207	23		#
	cp (hl)			;6208	be		.
	ret nz			;6209	c0		.
	inc hl			;620a	23		#
	cp (hl)			;620b	be		.
	ret nz			;620c	c0		.
	inc hl			;620d	23		#
	cp (hl)			;620e	be		.
	ret nz			;620f	c0		.
	inc hl			;6210	23		#
	cp (hl)			;6211	be		.
	ret nz			;6212	c0		.
	inc hl			;6213	23		#
	cp (hl)			;6214	be		.
	ret nz			;6215	c0		.
	inc hl			;6216	23		#
	cp (hl)			;6217	be		.
	ret nz			;6218	c0		.
	inc hl			;6219	23		#
	cp (hl)			;621a	be		.
	ret nz			;621b	c0		.
	inc hl			;621c	23		#
	cp (hl)			;621d	be		.
	ret nz			;621e	c0		.
	inc hl			;621f	23		#
	cp (hl)			;6220	be		.
	ret nz			;6221	c0		.
	inc hl			;6222	23		#
	cp (hl)			;6223	be		.
	ret nz			;6224	c0		.
	inc hl			;6225	23		#
	cp (hl)			;6226	be		.
	ret nz			;6227	c0		.
	inc hl			;6228	23		#
	cp (hl)			;6229	be		.
	ret nz			;622a	c0		.
	inc hl			;622b	23		#
	cp (hl)			;622c	be		.
	ret nz			;622d	c0		.
	inc hl			;622e	23		#
	cp (hl)			;622f	be		.
	ret nz			;6230	c0		.
	inc hl			;6231	23		#
	cp (hl)			;6232	be		.
	ret nz			;6233	c0		.
	inc hl			;6234	23		#
	cp (hl)			;6235	be		.
	ret nz			;6236	c0		.
	inc hl			;6237	23		#
	cp (hl)			;6238	be		.
	ret nz			;6239	c0		.
	inc hl			;623a	23		#
	cp (hl)			;623b	be		.
	ret nz			;623c	c0		.
	inc hl			;623d	23		#
	cp (hl)			;623e	be		.
	ret nz			;623f	c0		.
	inc hl			;6240	23		#
	cp (hl)			;6241	be		.
	ret nz			;6242	c0		.
	inc hl			;6243	23		#
	cp (hl)			;6244	be		.
	ret nz			;6245	c0		.
	inc hl			;6246	23		#
	cp (hl)			;6247	be		.
	ret nz			;6248	c0		.
	inc hl			;6249	23		#
	cp (hl)			;624a	be		.
	ret nz			;624b	c0		.
	inc hl			;624c	23		#
	cp (hl)			;624d	be		.
	ret nz			;624e	c0		.
	inc hl			;624f	23		#
	cp (hl)			;6250	be		.
	ret nz			;6251	c0		.
	inc hl			;6252	23		#
	cp (hl)			;6253	be		.
	ret nz			;6254	c0		.
	inc hl			;6255	23		#
	cp (hl)			;6256	be		.
	ret nz			;6257	c0		.
	inc hl			;6258	23		#
	cp (hl)			;6259	be		.
	ret nz			;625a	c0		.
	inc hl			;625b	23		#
	cp (hl)			;625c	be		.
	ret nz			;625d	c0		.
	inc hl			;625e	23		#
	cp (hl)			;625f	be		.
	ret nz			;6260	c0		.
	inc hl			;6261	23		#
	cp (hl)			;6262	be		.
	ret nz			;6263	c0		.
	inc hl			;6264	23		#
	cp (hl)			;6265	be		.
	ret nz			;6266	c0		.
	inc hl			;6267	23		#
	cp (hl)			;6268	be		.
	ret nz			;6269	c0		.
	inc hl			;626a	23		#
	cp (hl)			;626b	be		.
	ret nz			;626c	c0		.
	inc hl			;626d	23		#
	cp (hl)			;626e	be		.
	ret nz			;626f	c0		.
	inc hl			;6270	23		#
	cp (hl)			;6271	be		.
	ret nz			;6272	c0		.
	inc hl			;6273	23		#
	cp (hl)			;6274	be		.
	ret nz			;6275	c0		.
	inc hl			;6276	23		#
	cp (hl)			;6277	be		.
	ret nz			;6278	c0		.
	inc hl			;6279	23		#
	cp (hl)			;627a	be		.
	ret nz			;627b	c0		.
	inc hl			;627c	23		#
	cp (hl)			;627d	be		.
	ret nz			;627e	c0		.
	inc hl			;627f	23		#
	cp (hl)			;6280	be		.
	ret nz			;6281	c0		.
	inc hl			;6282	23		#
	cp (hl)			;6283	be		.
	ret nz			;6284	c0		.
	inc hl			;6285	23		#
	cp (hl)			;6286	be		.
	ret nz			;6287	c0		.
	inc hl			;6288	23		#
	cp (hl)			;6289	be		.
	ret nz			;628a	c0		.
	inc hl			;628b	23		#
	cp (hl)			;628c	be		.
	ret nz			;628d	c0		.
	inc hl			;628e	23		#
	cp (hl)			;628f	be		.
	ret nz			;6290	c0		.
	inc hl			;6291	23		#
	cp (hl)			;6292	be		.
	ret nz			;6293	c0		.
	inc hl			;6294	23		#
	cp (hl)			;6295	be		.
	ret nz			;6296	c0		.
	inc hl			;6297	23		#
	dec b			;6298	05		.
	jp nz,l61d8h		;6299	c2 d8 61	. . a
	xor a			;629c	af		.
	ret			;629d	c9		.
; --------------------------------------------------------------------------
; SP=dest trick: fills a 16K page by PUSHing BC pairs - fastest
; possible RAM fill, restores SP afterwards.
; --------------------------------------------------------------------------
FILL_BY_PUSH:
	di			;629e	f3		.
	ld (l62efh),sp		;629f	ed 73 ef 62	. s . b
	ld sp,00000h		;62a3	31 00 00	1 . .
	ld b,080h		;62a6	06 80		. .
l62a8h:
	push hl			;62a8	e5		.
	push hl			;62a9	e5		.
	push hl			;62aa	e5		.
	push hl			;62ab	e5		.
	push hl			;62ac	e5		.
	push hl			;62ad	e5		.
	push hl			;62ae	e5		.
	push hl			;62af	e5		.
	push hl			;62b0	e5		.
	push hl			;62b1	e5		.
	push hl			;62b2	e5		.
	push hl			;62b3	e5		.
	push hl			;62b4	e5		.
	push hl			;62b5	e5		.
	push hl			;62b6	e5		.
	push hl			;62b7	e5		.
	push hl			;62b8	e5		.
	push hl			;62b9	e5		.
	push hl			;62ba	e5		.
	push hl			;62bb	e5		.
	push hl			;62bc	e5		.
	push hl			;62bd	e5		.
	push hl			;62be	e5		.
	push hl			;62bf	e5		.
	push hl			;62c0	e5		.
	push hl			;62c1	e5		.
	push hl			;62c2	e5		.
	push hl			;62c3	e5		.
	push hl			;62c4	e5		.
	push hl			;62c5	e5		.
	push hl			;62c6	e5		.
	push hl			;62c7	e5		.
	push hl			;62c8	e5		.
	push hl			;62c9	e5		.
	push hl			;62ca	e5		.
	push hl			;62cb	e5		.
	push hl			;62cc	e5		.
	push hl			;62cd	e5		.
	push hl			;62ce	e5		.
	push hl			;62cf	e5		.
	push hl			;62d0	e5		.
	push hl			;62d1	e5		.
	push hl			;62d2	e5		.
	push hl			;62d3	e5		.
	push hl			;62d4	e5		.
	push hl			;62d5	e5		.
	push hl			;62d6	e5		.
	push hl			;62d7	e5		.
	push hl			;62d8	e5		.
	push hl			;62d9	e5		.
	push hl			;62da	e5		.
	push hl			;62db	e5		.
	push hl			;62dc	e5		.
	push hl			;62dd	e5		.
	push hl			;62de	e5		.
	push hl			;62df	e5		.
	push hl			;62e0	e5		.
	push hl			;62e1	e5		.
	push hl			;62e2	e5		.
	push hl			;62e3	e5		.
	push hl			;62e4	e5		.
	push hl			;62e5	e5		.
	push hl			;62e6	e5		.
	push hl			;62e7	e5		.
	djnz l62a8h		;62e8	10 be		. .
	ld sp,(l62efh)		;62ea	ed 7b ef 62	. { . b
	ret			;62ee	c9		.
l62efh:
	nop			;62ef	00		.
	nop			;62f0	00		.
; --------------------------------------------------------------------------
; JP (HL) into the model's w2 = pattern test slot
; --------------------------------------------------------------------------
CALL_W2:
	ld hl,06f45h		;62f1	21 45 6f	! E o
	call TABLE_WORD		;62f4	cd ad 60	. . `
	jp (hl)			;62f7	e9		.
; --------------------------------------------------------------------------
; w2 ladder: LD B,8 (8 pages) -> PAGE_SWEEP
; --------------------------------------------------------------------------
PAT_128K:
	ld b,008h		;62f8	06 08		. .
	jr l6306h		;62fa	18 0a		. .
; --------------------------------------------------------------------------
; w2 ladder: LD B,10h (16 pages) -> PAGE_SWEEP
; --------------------------------------------------------------------------
PAT_256K:
	ld b,010h		;62fc	06 10		. .
	jr l6306h		;62fe	18 06		. .
; --------------------------------------------------------------------------
; w2 ladder: LD B,20h (32 pages) -> PAGE_SWEEP
; --------------------------------------------------------------------------
PAT_512K:
	ld b,020h		;6300	06 20		.  
	jr l6306h		;6302	18 02		. .
; --------------------------------------------------------------------------
; w2 ladder: LD B,40h (64 pages) -> PAGE_SWEEP
; --------------------------------------------------------------------------
PAT_1M:
	ld b,040h		;6304	06 40		. @
l6306h:
	ld hl,05880h		;6306	21 80 58	! . X
; --------------------------------------------------------------------------
; LD (SCREEN_PTR),HL then the per-page pattern loop body shared by
; every PAT_* entry; HL selects the screen row for this sweep.
; --------------------------------------------------------------------------
SCREEN_ROW_SET:
	ld (SCREEN_PTR),hl	;6309	22 a7 60	" . `
	di			;630c	f3		.
l630dh:
	push bc			;630d	c5		.
	call CALL_W0		;630e	cd bf 60	. . `
	call TEST_PAGE_PATTERNS	;6311	cd 52 60	. R `
	pop bc			;6314	c1		.
	djnz l630dh		;6315	10 f6		. .
	ret			;6317	c9		.
; --------------------------------------------------------------------------
; w2 for 2048K models: sweeps screen rows 5880h and 5888h, 40h
; pages each (128 pages total).
; --------------------------------------------------------------------------
PAT_2M:
	ld hl,05880h		;6318	21 80 58	! . X
	ld b,040h		;631b	06 40		. @
	call SCREEN_ROW_SET	;631d	cd 09 63	. . c
	ld hl,05888h		;6320	21 88 58	! . X
	ld b,040h		;6323	06 40		. @
	jr SCREEN_ROW_SET	;6325	18 e2		. .
; --------------------------------------------------------------------------
; w2 for TSConf/Sprinter: sweeps rows 5880h/5888h/5890h/5898h,
; 40h pages each (256 pages total, tail-called via JP).
; --------------------------------------------------------------------------
PAT_4M:
	ld hl,05880h		;6327	21 80 58	! . X
	ld b,040h		;632a	06 40		. @
	call SCREEN_ROW_SET	;632c	cd 09 63	. . c
	ld hl,05888h		;632f	21 88 58	! . X
	ld b,040h		;6332	06 40		. @
	call SCREEN_ROW_SET	;6334	cd 09 63	. . c
	ld hl,05890h		;6337	21 90 58	! . X
	ld b,040h		;633a	06 40		. @
	call SCREEN_ROW_SET	;633c	cd 09 63	. . c
	ld hl,05898h		;633f	21 98 58	! . X
	ld b,040h		;6342	06 40		. @
	jp SCREEN_ROW_SET	;6344	c3 09 63	. . c
R_6347:
	ld hl,05d00h		;6347	21 00 5d	! . ]
	ld (l63fbh),hl		;634a	22 fb 63	" . c
; --------------------------------------------------------------------------
; JP (HL) into the model's w1 = mark pages slot
; --------------------------------------------------------------------------
CALL_W1:
	ld hl,06f43h		;634d	21 43 6f	! C o
	call TABLE_WORD		;6350	cd ad 60	. . `
	jp (hl)			;6353	e9		.
; --------------------------------------------------------------------------
; w1 for TSConf: B=0 (256 DJNZ iterations); for every page: map it,
; write the page number at 0xC000, remember it; verify pass reads
; back and collects failures (helpers 63B0/63C3/63D5/63DE).
; --------------------------------------------------------------------------
MARK_4M_TSCONF:
	ld b,000h		;6354	06 00		. .
l6356h:
	xor a			;6356	af		.
	ld (PAGE_NUM),a		;6357	32 a6 60	2 . `
	ld c,b			;635a	48		H
l635bh:
	push bc			;635b	c5		.
	call CALL_W0		;635c	cd bf 60	. . `
	call STEP_LINEAR	;635f	cd d5 63	. . c
	pop bc			;6362	c1		.
	djnz l635bh		;6363	10 f6		. .
	xor a			;6365	af		.
	ld (PAGE_NUM),a		;6366	32 a6 60	2 . `
	ld (l63fdh),a		;6369	32 fd 63	2 . c
	ld b,c			;636c	41		A
l636dh:
	push bc			;636d	c5		.
	call CALL_W0		;636e	cd bf 60	. . `
	call VERIFY_STEP_LINEAR	;6371	cd de 63	. . c
	pop bc			;6374	c1		.
	djnz l636dh		;6375	10 f6		. .
	ret			;6377	c9		.
; --------------------------------------------------------------------------
; w1 ladder: LD B,40h (64 pages) mark sweep
; --------------------------------------------------------------------------
MARK_1M:
	ld b,040h		;6378	06 40		. @
	jr l6356h		;637a	18 da		. .
; --------------------------------------------------------------------------
; w1 ladder: LD B,80h (128 pages) mark sweep
; --------------------------------------------------------------------------
MARK_2M:
	ld b,080h		;637c	06 80		. .
	jr l6356h		;637e	18 d6		. .
; --------------------------------------------------------------------------
; w1 ladder: LD B,10h (16 pages) mark sweep
; --------------------------------------------------------------------------
MARK_256K:
	ld b,010h		;6380	06 10		. .
	jr l6356h		;6382	18 d2		. .
; --------------------------------------------------------------------------
; w1 ladder: LD B,20h (32 pages) mark sweep
; --------------------------------------------------------------------------
MARK_512K:
	ld b,020h		;6384	06 20		.  
	jr l6356h		;6386	18 ce		. .
; --------------------------------------------------------------------------
; w1 ladder: LD B,8 (8 pages) mark sweep
; --------------------------------------------------------------------------
MARK_128K:
	ld b,008h		;6388	06 08		. .
	jr l6356h		;638a	18 ca		. .
; --------------------------------------------------------------------------
; w1 for Sprinter: same 256-page sweep but the step helper 63B0
; skips page 40h exactly and pages 50h-5Fh (ROM/VRAM shadows).
; --------------------------------------------------------------------------
MARK_4M_SPRINTER:
	ld b,000h		;638c	06 00		. .
	xor a			;638e	af		.
	ld (PAGE_NUM),a		;638f	32 a6 60	2 . `
	ld c,b			;6392	48		H
l6393h:
	push bc			;6393	c5		.
	call CALL_W0		;6394	cd bf 60	. . `
	call STEP_SPRINTER	;6397	cd b0 63	. . c
	pop bc			;639a	c1		.
	djnz l6393h		;639b	10 f6		. .
	xor a			;639d	af		.
	ld (PAGE_NUM),a		;639e	32 a6 60	2 . `
	ld (l63fdh),a		;63a1	32 fd 63	2 . c
	ld b,c			;63a4	41		A
l63a5h:
	push bc			;63a5	c5		.
	call CALL_W0		;63a6	cd bf 60	. . `
	call VERIFY_STEP_SPRINTER	;63a9	cd c3 63	. . c
	pop bc			;63ac	c1		.
	djnz l63a5h		;63ad	10 f6		. .
	ret			;63af	c9		.
; --------------------------------------------------------------------------
; Sprinter mark step: page++; page==40h -> skip once; 50h-5Fh ->
; skip (ROM/VRAM); otherwise write page number to 0xC000.
; --------------------------------------------------------------------------
STEP_SPRINTER:
	ld hl,PAGE_NUM		;63b0	21 a6 60	! . `
	ld a,(hl)		;63b3	7e		~
	inc (hl)		;63b4	34		4
	cp 040h			;63b5	fe 40		. @
	ret z			;63b7	c8		.
	cp 050h			;63b8	fe 50		. P
	jr c,l63bfh		;63ba	38 03		8 .
	cp 060h			;63bc	fe 60		. `
	ret c			;63be	d8		.
l63bfh:
	ld (0c000h),a		;63bf	32 00 c0	2 . .
	ret			;63c2	c9		.
; --------------------------------------------------------------------------
; Sprinter verify step with the same 40h/50h-5Fh exclusions.
; --------------------------------------------------------------------------
VERIFY_STEP_SPRINTER:
	ld hl,PAGE_NUM		;63c3	21 a6 60	! . `
	ld a,(hl)		;63c6	7e		~
	cp 040h			;63c7	fe 40		. @
	jr z,l63e7h		;63c9	28 1c		( .
	cp 050h			;63cb	fe 50		. P
	jr c,l63e1h		;63cd	38 12		8 .
	cp 060h			;63cf	fe 60		. `
	jr c,l63e7h		;63d1	38 14		8 .
	jr l63e1h		;63d3	18 0c		. .
; --------------------------------------------------------------------------
; Linear mark step: page++; write page number to 0xC000.
; --------------------------------------------------------------------------
STEP_LINEAR:
	ld hl,PAGE_NUM		;63d5	21 a6 60	! . `
	ld a,(hl)		;63d8	7e		~
	inc (hl)		;63d9	34		4
	ld (0c000h),a		;63da	32 00 c0	2 . .
	ret			;63dd	c9		.
; --------------------------------------------------------------------------
; Linear verify step: read 0xC000, must equal page; failures are
; recorded through the list pointer at 63FBh.
; --------------------------------------------------------------------------
VERIFY_STEP_LINEAR:
	ld hl,PAGE_NUM		;63de	21 a6 60	! . `
l63e1h:
	ld a,(0c000h)		;63e1	3a 00 c0	: . .
	cp (hl)			;63e4	be		.
	jr nz,l63eah		;63e5	20 03		  .
l63e7h:
	inc (hl)		;63e7	34		4
	xor a			;63e8	af		.
	ret			;63e9	c9		.
l63eah:
	ld a,(hl)		;63ea	7e		~
	inc (hl)		;63eb	34		4
	ld hl,(l63fbh)		;63ec	2a fb 63	* . c
	ld (hl),a		;63ef	77		w
	inc hl			;63f0	23		#
	ld (l63fbh),hl		;63f1	22 fb 63	" . c
	ld a,001h		;63f4	3e 01		> .
	ld (l63fdh),a		;63f6	32 fd 63	2 . c
	or a			;63f9	b7		.
	ret			;63fa	c9		.
l63fbh:
	nop			;63fb	00		.
	ld e,l			;63fc	5d		]
l63fdh:
	nop			;63fd	00		.
; --------------------------------------------------------------------------
; JP (HL) into the model's w3 = checksum slot
; --------------------------------------------------------------------------
CALL_W3:
	ld hl,06f47h		;63fe	21 47 6f	! G o
	call TABLE_WORD		;6401	cd ad 60	. . `
	jp (hl)			;6404	e9		.
l6405h:
	ld hl,05890h		;6405	21 90 58	! . X
l6408h:
	ld (SCREEN_PTR),hl	;6408	22 a7 60	" . `
	ret			;640b	c9		.
l640ch:
	ld hl,05888h		;640c	21 88 58	! . X
	jr l6408h		;640f	18 f7		. .
l6411h:
	ld hl,05898h		;6411	21 98 58	! . X
	jr l6408h		;6414	18 f2		. .
; --------------------------------------------------------------------------
; Maps a page number to its screen table row: 40h->5898h,
; 80h->5890h, C0h->5888h, else 5880h (two 64-page screen halves).
; --------------------------------------------------------------------------
SELECT_ROW:
	ld a,(MODEL_INDEX)	;6416	3a 3f 6f	: ? o
	cp 003h			;6419	fe 03		. .
	jr z,l6436h		;641b	28 19		( .
	cp 009h			;641d	fe 09		. .
	jr z,l6436h		;641f	28 15		( .
	cp 007h			;6421	fe 07		. .
	jr z,l6428h		;6423	28 03		( .
	cp 008h			;6425	fe 08		. .
	ret nz			;6427	c0		.
l6428h:
	ld a,b			;6428	78		x
	cp 0c0h			;6429	fe c0		. .
	jr z,l640ch		;642b	28 df		( .
	cp 080h			;642d	fe 80		. .
	jr z,l6405h		;642f	28 d4		( .
	cp 040h			;6431	fe 40		. @
	jr z,l6411h		;6433	28 dc		( .
	ret			;6435	c9		.
l6436h:
	ld a,b			;6436	78		x
	cp 040h			;6437	fe 40		. @
	jr z,l640ch		;6439	28 d1		( .
	ret			;643b	c9		.
; --------------------------------------------------------------------------
; w3 ladder: LD B,80h (128 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_2M:
	ld b,080h		;643c	06 80		. .
	jr l6446h		;643e	18 06		. .
; --------------------------------------------------------------------------
; w3 ladder: LD B,0 (256 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_4M:
	ld b,000h		;6440	06 00		. .
	jr l6446h		;6442	18 02		. .
; --------------------------------------------------------------------------
; w3 ladder: LD B,10h (16 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_256K:
	ld b,010h		;6444	06 10		. .
l6446h:
	push bc			;6446	c5		.
	call R_6490		;6447	cd 90 64	. . d
	pop bc			;644a	c1		.
	ld c,b			;644b	48		H
l644ch:
	push bc			;644c	c5		.
	call SELECT_ROW		;644d	cd 16 64	. . d
	call CALL_W0		;6450	cd bf 60	. . `
	call CHECKSUM_WRITE	;6453	cd a3 64	. . d
	pop bc			;6456	c1		.
	djnz l644ch		;6457	10 f3		. .
	push bc			;6459	c5		.
	call R_6479		;645a	cd 79 64	. y d
	pop bc			;645d	c1		.
	ld b,c			;645e	41		A
l645fh:
	push bc			;645f	c5		.
	call SELECT_ROW		;6460	cd 16 64	. . d
	call CALL_W0		;6463	cd bf 60	. . `
	call CHECKSUM_VERIFY	;6466	cd dc 64	. . d
	pop bc			;6469	c1		.
	djnz l645fh		;646a	10 f3		. .
	ret			;646c	c9		.
; --------------------------------------------------------------------------
; w3 ladder: LD B,40h (64 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_1M:
	ld b,040h		;646d	06 40		. @
	jr l6446h		;646f	18 d5		. .
; --------------------------------------------------------------------------
; w3 ladder: LD B,20h (32 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_512K:
	ld b,020h		;6471	06 20		.  
	jr l6446h		;6473	18 d1		. .
; --------------------------------------------------------------------------
; w3 ladder: LD B,8 (8 pages) checksum sweep
; --------------------------------------------------------------------------
SUM_128K:
	ld b,008h		;6475	06 08		. .
	jr l6446h		;6477	18 cd		. .
R_6479:
	xor a			;6479	af		.
	ld (PAGE_NUM),a		;647a	32 a6 60	2 . `
	ld hl,05c00h		;647d	21 00 5c	! . \
	ld de,07700h		;6480	11 00 77	. . w
	ld bc,00100h		;6483	01 00 01	. . .
	ldir			;6486	ed b0		. .
	ld hl,05880h		;6488	21 80 58	! . X
	ld (SCREEN_PTR),hl	;648b	22 a7 60	" . `
	di			;648e	f3		.
	ret			;648f	c9		.
R_6490:
	ld hl,07700h		;6490	21 00 77	! . w
	ld de,05c00h		;6493	11 00 5c	. . \
	ld bc,00100h		;6496	01 00 01	. . .
	ldir			;6499	ed b0		. .
	ld hl,05880h		;649b	21 80 58	! . X
	ld (SCREEN_PTR),hl	;649e	22 a7 60	" . `
	di			;64a1	f3		.
	ret			;64a2	c9		.
; --------------------------------------------------------------------------
; Checksum pass: A = table[L] + table[L+1Fh] stored to table[L+37h]
; and to (DE) - a chained triangular sum over the seeded table at
; 7700h (seed = ROM[0..0FEh] copied at init; 5C00h area is saved
; and restored around the run). SMC counter at 64BAh.
; --------------------------------------------------------------------------
CHECKSUM_WRITE:
	ld a,(MODEL_INDEX)	;64a3	3a 3f 6f	: ? o
	cp 008h			;64a6	fe 08		. .
	call z,PAGE_TESTABLE	;64a8	cc 44 60	. D `
	jr z,l64d7h		;64ab	28 2a		( *
	ld a,(PAGE_NUM)		;64ad	3a a6 60	: . `
	cp 005h			;64b0	fe 05		. .
	jr z,l64d7h		;64b2	28 23		( #
	ld de,0c000h		;64b4	11 00 c0	. . .
l64b7h:
	ld h,077h		;64b7	26 77		& w
	ld a,000h		;64b9	3e 00		> .
	inc a			;64bb	3c		<
	ld (064bah),a		;64bc	32 ba 64	2 . d
	ld l,a			;64bf	6f		o
	ld b,(hl)		;64c0	46		F
	add a,01fh		;64c1	c6 1f		. .
	ld l,a			;64c3	6f		o
	ld c,(hl)		;64c4	4e		N
	add a,018h		;64c5	c6 18		. .
	ld l,a			;64c7	6f		o
	ld a,b			;64c8	78		x
	add a,c			;64c9	81		.
	ld (hl),a		;64ca	77		w
	ld (de),a		;64cb	12		.
	inc de			;64cc	13		.
	ld a,d			;64cd	7a		z
	or a			;64ce	b7		.
	jp nz,l64b7h		;64cf	c2 b7 64	. . d
	ld a,045h		;64d2	3e 45		> E
	jp R_6085		;64d4	c3 85 60	. . `
l64d7h:
	ld a,046h		;64d7	3e 46		> F
	jp R_6085		;64d9	c3 85 60	. . `
; --------------------------------------------------------------------------
; Recomputes the chained sums and flags the first difference.
; --------------------------------------------------------------------------
CHECKSUM_VERIFY:
	ld a,(MODEL_INDEX)	;64dc	3a 3f 6f	: ? o
	cp 008h			;64df	fe 08		. .
	call z,PAGE_TESTABLE	;64e1	cc 44 60	. D `
	jr z,l651fh		;64e4	28 39		( 9
	ld a,(PAGE_NUM)		;64e6	3a a6 60	: . `
	cp 005h			;64e9	fe 05		. .
	jr z,l651fh		;64eb	28 32		( 2
	xor a			;64ed	af		.
	ld (l652fh),a		;64ee	32 2f 65	2 / e
	ld de,0c000h		;64f1	11 00 c0	. . .
l64f4h:
	ld h,077h		;64f4	26 77		& w
	ld a,000h		;64f6	3e 00		> .
	inc a			;64f8	3c		<
	ld (064f7h),a		;64f9	32 f7 64	2 . d
	ld l,a			;64fc	6f		o
	ld b,(hl)		;64fd	46		F
	add a,01fh		;64fe	c6 1f		. .
	ld l,a			;6500	6f		o
	ld c,(hl)		;6501	4e		N
	add a,018h		;6502	c6 18		. .
	ld l,a			;6504	6f		o
	ld a,b			;6505	78		x
	add a,c			;6506	81		.
	ld (hl),a		;6507	77		w
	ld c,a			;6508	4f		O
	ld a,(de)		;6509	1a		.
	inc de			;650a	13		.
	cp c			;650b	b9		.
	jr nz,l6524h		;650c	20 16		  .
R_650E:
	ld a,d			;650e	7a		z
	or a			;650f	b7		.
	jp nz,l64f4h		;6510	c2 f4 64	. . d
	ld a,(l652fh)		;6513	3a 2f 65	: / e
	or a			;6516	b7		.
	jp nz,R_6085		;6517	c2 85 60	. . `
	ld a,044h		;651a	3e 44		> D
	jp R_6085		;651c	c3 85 60	. . `
l651fh:
	ld a,046h		;651f	3e 46		> F
	jp R_6085		;6521	c3 85 60	. . `
l6524h:
	ld a,042h		;6524	3e 42		> B
	ld (l652fh),a		;6526	32 2f 65	2 / e
	ld (l6530h),a		;6529	32 30 65	2 0 e
	jp R_650E		;652c	c3 0e 65	. . e
l652fh:
	nop			;652f	00		.
l6530h:
	nop			;6530	00		.
	nop			;6531	00		.
	nop			;6532	00		.
; --------------------------------------------------------------------------
; JP (HL) into the model's w4 = rotate test slot
; --------------------------------------------------------------------------
CALL_W4:
	ld hl,06f49h		;6533	21 49 6f	! I o
	call TABLE_WORD		;6536	cd ad 60	. . `
	jp (hl)			;6539	e9		.
; --------------------------------------------------------------------------
; w4 ladder: LD B,8 (8 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_128K:
	ld b,008h		;653a	06 08		. .
l653ch:
	ld hl,05880h		;653c	21 80 58	! . X
	ld (l66f2h),hl		;653f	22 f2 66	" . f
	push bc			;6542	c5		.
	call SEED_TABLE_ONCE	;6543	cd d3 66	. . f
	pop bc			;6546	c1		.
	ld c,b			;6547	48		H
	jr nz,l655ah		;6548	20 10		  .
	call R_6597		;654a	cd 97 65	. . e
l654dh:
	push bc			;654d	c5		.
	call SELECT_ROW		;654e	cd 16 64	. . d
	call CALL_W0		;6551	cd bf 60	. . `
	call R_66AB		;6554	cd ab 66	. . f
	pop bc			;6557	c1		.
	djnz l654dh		;6558	10 f3		. .
l655ah:
	ld b,c			;655a	41		A
	call R_6597		;655b	cd 97 65	. . e
l655eh:
	push bc			;655e	c5		.
	call SELECT_ROW		;655f	cd 16 64	. . d
	call CALL_W0		;6562	cd bf 60	. . `
	call R_664D		;6565	cd 4d 66	. M f
	pop bc			;6568	c1		.
	djnz l655eh		;6569	10 f3		. .
	ld b,c			;656b	41		A
	call R_6597		;656c	cd 97 65	. . e
	call R_6625		;656f	cd 25 66	. % f
l6572h:
	push bc			;6572	c5		.
	push af			;6573	f5		.
	call SELECT_ROW		;6574	cd 16 64	. . d
	call CALL_W0		;6577	cd bf 60	. . `
	pop af			;657a	f1		.
	call ROTATE_PAGE	;657b	cd a2 65	. . e
	ex af,af'		;657e	08		.
	pop bc			;657f	c1		.
	djnz l6572h		;6580	10 f0		. .
	ret			;6582	c9		.
; --------------------------------------------------------------------------
; w4 ladder: LD B,10h (16 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_256K:
	ld b,010h		;6583	06 10		. .
	jr l653ch		;6585	18 b5		. .
; --------------------------------------------------------------------------
; w4 ladder: LD B,20h (32 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_512K:
	ld b,020h		;6587	06 20		.  
	jr l653ch		;6589	18 b1		. .
; --------------------------------------------------------------------------
; w4 ladder: LD B,40h (64 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_1M:
	ld b,040h		;658b	06 40		. @
	jr l653ch		;658d	18 ad		. .
; --------------------------------------------------------------------------
; w4 ladder: LD B,80h (128 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_2M:
	ld b,080h		;658f	06 80		. .
	jr l653ch		;6591	18 a9		. .
; --------------------------------------------------------------------------
; w4 ladder: LD B,0 (256 pages) rotate sweep
; --------------------------------------------------------------------------
ROT_4M:
	ld b,000h		;6593	06 00		. .
	jr l653ch		;6595	18 a5		. .
R_6597:
	ld hl,(l66f2h)		;6597	2a f2 66	* . f
	ld (SCREEN_PTR),hl	;659a	22 a7 60	" . `
	xor a			;659d	af		.
	ld (PAGE_NUM),a		;659e	32 a6 60	2 . `
	ret			;65a1	c9		.
; --------------------------------------------------------------------------
; RR (HL) carry-chains: rotates the whole 16K page one bit right
; three times; a stuck bit or cross-coupled cell breaks the carry
; chain and the shifted-out pattern, which the verify sweep catches.
; --------------------------------------------------------------------------
ROTATE_PAGE:
	ex af,af'		;65a2	08		.
	ld a,(MODEL_INDEX)	;65a3	3a 3f 6f	: ? o
	cp 008h			;65a6	fe 08		. .
	call z,PAGE_TESTABLE	;65a8	cc 44 60	. D `
	jp z,0609bh		;65ab	ca 9b 60	. . `
	ld a,(PAGE_NUM)		;65ae	3a a6 60	: . `
	cp 005h			;65b1	fe 05		. .
	jp z,0609bh		;65b3	ca 9b 60	. . `
	ex af,af'		;65b6	08		.
	ld hl,0c000h		;65b7	21 00 c0	! . .
l65bah:
	rr (hl)			;65ba	cb 1e		. .
	inc l			;65bc	2c		,
	rr (hl)			;65bd	cb 1e		. .
	inc l			;65bf	2c		,
	rr (hl)			;65c0	cb 1e		. .
	inc l			;65c2	2c		,
	rr (hl)			;65c3	cb 1e		. .
	inc l			;65c5	2c		,
	rr (hl)			;65c6	cb 1e		. .
	inc l			;65c8	2c		,
	rr (hl)			;65c9	cb 1e		. .
	inc l			;65cb	2c		,
	rr (hl)			;65cc	cb 1e		. .
	inc l			;65ce	2c		,
	rr (hl)			;65cf	cb 1e		. .
	inc l			;65d1	2c		,
	rr (hl)			;65d2	cb 1e		. .
	inc l			;65d4	2c		,
	rr (hl)			;65d5	cb 1e		. .
	inc l			;65d7	2c		,
	rr (hl)			;65d8	cb 1e		. .
	inc l			;65da	2c		,
	rr (hl)			;65db	cb 1e		. .
	inc l			;65dd	2c		,
	rr (hl)			;65de	cb 1e		. .
	inc l			;65e0	2c		,
	rr (hl)			;65e1	cb 1e		. .
	inc l			;65e3	2c		,
	rr (hl)			;65e4	cb 1e		. .
	inc l			;65e6	2c		,
	rr (hl)			;65e7	cb 1e		. .
	inc l			;65e9	2c		,
	rr (hl)			;65ea	cb 1e		. .
	inc l			;65ec	2c		,
	rr (hl)			;65ed	cb 1e		. .
	inc l			;65ef	2c		,
	rr (hl)			;65f0	cb 1e		. .
	inc l			;65f2	2c		,
	rr (hl)			;65f3	cb 1e		. .
	inc l			;65f5	2c		,
	rr (hl)			;65f6	cb 1e		. .
	inc l			;65f8	2c		,
	rr (hl)			;65f9	cb 1e		. .
	inc l			;65fb	2c		,
	rr (hl)			;65fc	cb 1e		. .
	inc l			;65fe	2c		,
	rr (hl)			;65ff	cb 1e		. .
	inc l			;6601	2c		,
	rr (hl)			;6602	cb 1e		. .
	inc l			;6604	2c		,
	rr (hl)			;6605	cb 1e		. .
	inc l			;6607	2c		,
	rr (hl)			;6608	cb 1e		. .
	inc l			;660a	2c		,
	rr (hl)			;660b	cb 1e		. .
	inc l			;660d	2c		,
	rr (hl)			;660e	cb 1e		. .
	inc l			;6610	2c		,
	rr (hl)			;6611	cb 1e		. .
	inc l			;6613	2c		,
	rr (hl)			;6614	cb 1e		. .
	inc l			;6616	2c		,
	rr (hl)			;6617	cb 1e		. .
	inc hl			;6619	23		#
	inc h			;661a	24		$
	dec h			;661b	25		%
	jp nz,l65bah		;661c	c2 ba 65	. . e
	ex af,af'		;661f	08		.
	ld a,045h		;6620	3e 45		> E
	jp R_6085		;6622	c3 85 60	. . `
R_6625:
	push hl			;6625	e5		.
	push bc			;6626	c5		.
	ld hl,05000h		;6627	21 00 50	! . P
	ld a,(057ffh)		;662a	3a ff 57	: . W
	rra			;662d	1f		.
	ld b,000h		;662e	06 00		. .
l6630h:
	rr (hl)			;6630	cb 1e		. .
	inc hl			;6632	23		#
	rr (hl)			;6633	cb 1e		. .
	inc hl			;6635	23		#
	rr (hl)			;6636	cb 1e		. .
	inc hl			;6638	23		#
	rr (hl)			;6639	cb 1e		. .
	inc hl			;663b	23		#
	rr (hl)			;663c	cb 1e		. .
	inc hl			;663e	23		#
	rr (hl)			;663f	cb 1e		. .
	inc hl			;6641	23		#
	rr (hl)			;6642	cb 1e		. .
	inc hl			;6644	23		#
	rr (hl)			;6645	cb 1e		. .
	inc hl			;6647	23		#
	djnz l6630h		;6648	10 e6		. .
	pop bc			;664a	c1		.
	pop hl			;664b	e1		.
	ret			;664c	c9		.
R_664D:
	ld a,(MODEL_INDEX)	;664d	3a 3f 6f	: ? o
	cp 008h			;6650	fe 08		. .
	call z,PAGE_TESTABLE	;6652	cc 44 60	. D `
	jp z,0609bh		;6655	ca 9b 60	. . `
	ld a,(PAGE_NUM)		;6658	3a a6 60	: . `
	cp 005h			;665b	fe 05		. .
	jp z,0609bh		;665d	ca 9b 60	. . `
	ld hl,0c000h		;6660	21 00 c0	! . .
l6663h:
	ld de,05000h		;6663	11 00 50	. . P
	ld b,000h		;6666	06 00		. .
l6668h:
	ld a,(de)		;6668	1a		.
	cp (hl)			;6669	be		.
	jr nz,l66a3h		;666a	20 37		  7
	inc de			;666c	13		.
	inc hl			;666d	23		#
	ld a,(de)		;666e	1a		.
	cp (hl)			;666f	be		.
	jr nz,l66a3h		;6670	20 31		  1
	inc de			;6672	13		.
	inc hl			;6673	23		#
	ld a,(de)		;6674	1a		.
	cp (hl)			;6675	be		.
	jr nz,l66a3h		;6676	20 2b		  +
	inc de			;6678	13		.
	inc hl			;6679	23		#
	ld a,(de)		;667a	1a		.
	cp (hl)			;667b	be		.
	jr nz,l66a3h		;667c	20 25		  %
	inc de			;667e	13		.
	inc hl			;667f	23		#
	ld a,(de)		;6680	1a		.
	cp (hl)			;6681	be		.
	jr nz,l66a3h		;6682	20 1f		  .
	inc de			;6684	13		.
	inc hl			;6685	23		#
	ld a,(de)		;6686	1a		.
	cp (hl)			;6687	be		.
	jr nz,l66a3h		;6688	20 19		  .
	inc de			;668a	13		.
	inc hl			;668b	23		#
	ld a,(de)		;668c	1a		.
	cp (hl)			;668d	be		.
	jr nz,l66a3h		;668e	20 13		  .
	inc de			;6690	13		.
	inc hl			;6691	23		#
	ld a,(de)		;6692	1a		.
	cp (hl)			;6693	be		.
	jr nz,l66a3h		;6694	20 0d		  .
	inc de			;6696	13		.
	inc hl			;6697	23		#
	djnz l6668h		;6698	10 ce		. .
	ld a,h			;669a	7c		|
	or a			;669b	b7		.
	jr nz,l6663h		;669c	20 c5		  .
	ld a,044h		;669e	3e 44		> D
	jp R_6085		;66a0	c3 85 60	. . `
l66a3h:
	ld a,042h		;66a3	3e 42		> B
	ld (l66f1h),a		;66a5	32 f1 66	2 . f
	jp R_6085		;66a8	c3 85 60	. . `
R_66AB:
	ld a,(MODEL_INDEX)	;66ab	3a 3f 6f	: ? o
	cp 008h			;66ae	fe 08		. .
	call z,PAGE_TESTABLE	;66b0	cc 44 60	. D `
	jp z,0609bh		;66b3	ca 9b 60	. . `
	ld a,(PAGE_NUM)		;66b6	3a a6 60	: . `
	cp 005h			;66b9	fe 05		. .
	jp z,0609bh		;66bb	ca 9b 60	. . `
	ld de,0c000h		;66be	11 00 c0	. . .
	ld a,008h		;66c1	3e 08		> .
l66c3h:
	ld hl,05000h		;66c3	21 00 50	! . P
	ld bc,00800h		;66c6	01 00 08	. . .
	ldir			;66c9	ed b0		. .
	dec a			;66cb	3d		=
	jr nz,l66c3h		;66cc	20 f5		  .
	ld a,041h		;66ce	3e 41		> A
	jp R_6085		;66d0	c3 85 60	. . `
; --------------------------------------------------------------------------
; One-shot (SMC latch) initialization guard for the checksum table.
; --------------------------------------------------------------------------
SEED_TABLE_ONCE:
	ld a,(l66f0h)		;66d3	3a f0 66	: . f
	or a			;66d6	b7		.
	ret nz			;66d7	c0		.
	inc a			;66d8	3c		<
	ld (l66f0h),a		;66d9	32 f0 66	2 . f
	ld de,05000h		;66dc	11 00 50	. . P
	ld bc,00800h		;66df	01 00 08	. . .
l66e2h:
	exx			;66e2	d9		.
	call R_6720		;66e3	cd 20 67	.   g
	exx			;66e6	d9		.
	ld (de),a		;66e7	12		.
	inc de			;66e8	13		.
	dec bc			;66e9	0b		.
	ld a,b			;66ea	78		x
	or c			;66eb	b1		.
	jr nz,l66e2h		;66ec	20 f4		  .
	xor a			;66ee	af		.
	ret			;66ef	c9		.
l66f0h:
	nop			;66f0	00		.
l66f1h:
	nop			;66f1	00		.
l66f2h:
	nop			;66f2	00		.
	nop			;66f3	00		.
R_66F4:
	cp 045h			;66f4	fe 45		. E
	ret z			;66f6	c8		.
	call CLEAR_ATTRS	;66f7	cd 19 68	. . h
	ld de,00000h		;66fa	11 00 00	. . .
	ld a,000h		;66fd	3e 00		> .
	call R_6AB0		;66ff	cd b0 6a	. . j
R_6702:
	call R_680D		;6702	cd 0d 68	. . h
	ld hl,05ae0h		;6705	21 e0 5a	! . Z
	call R_6754		;6708	cd 54 67	. T g
	ld a,001h		;670b	3e 01		> .
	or a			;670d	b7		.
	ret			;670e	c9		.
; --------------------------------------------------------------------------
; LDIR ROM[0000..00FEh] -> table at 7700h (0x100 bytes); the ROM
; image becomes the checksum chain seed.
; --------------------------------------------------------------------------
INIT_CHECKSUM_TABLE:
	ld de,07700h		;670f	11 00 77	. . w
	xor a			;6712	af		.
	ld h,a			;6713	67		g
	ld l,a			;6714	6f		o
	ld bc,000ffh		;6715	01 ff 00	. . .
	ldir			;6718	ed b0		. .
	ex de,hl		;671a	eb		.
	ld (hl),l		;671b	75		u
	ld (064bah),a		;671c	32 ba 64	2 . d
	ret			;671f	c9		.
R_6720:
	ld h,077h		;6720	26 77		& w
	ld a,000h		;6722	3e 00		> .
	inc a			;6724	3c		<
	ld (06723h),a		;6725	32 23 67	2 # g
	ld l,a			;6728	6f		o
	ld b,(hl)		;6729	46		F
	add a,01fh		;672a	c6 1f		. .
	ld l,a			;672c	6f		o
	ld c,(hl)		;672d	4e		N
	add a,018h		;672e	c6 18		. .
	ld l,a			;6730	6f		o
	ld a,b			;6731	78		x
	add a,c			;6732	81		.
	ld (hl),a		;6733	77		w
	ret			;6734	c9		.
R_6735:
	inc l			;6735	2c		,
	ld c,a			;6736	4f		O
	and 00fh		;6737	e6 0f		. .
	cp 00ah			;6739	fe 0a		. .
	jr c,l673fh		;673b	38 02		8 .
	add a,007h		;673d	c6 07		. .
l673fh:
	add a,030h		;673f	c6 30		. 0
	ld (hl),a		;6741	77		w
	dec l			;6742	2d		-
	ld a,c			;6743	79		y
	ld (hl),a		;6744	77		w
	xor a			;6745	af		.
	rld			;6746	ed 6f		. o
	cp 00ah			;6748	fe 0a		. .
	jr c,l674eh		;674a	38 02		8 .
	add a,007h		;674c	c6 07		. .
l674eh:
	add a,030h		;674e	c6 30		. 0
	ld (hl),a		;6750	77		w
	inc l			;6751	2c		,
	inc l			;6752	2c		,
	ret			;6753	c9		.
R_6754:
	push hl			;6754	e5		.
	ld a,r			;6755	ed 5f		. _
	and 007h		;6757	e6 07		. .
	or 040h			;6759	f6 40		. @
	ld b,020h		;675b	06 20		.  
l675dh:
	ld (hl),a		;675d	77		w
	inc l			;675e	2c		,
	djnz l675dh		;675f	10 fc		. .
	pop hl			;6761	e1		.
	call R_6805		;6762	cd 05 68	. . h
	jr z,R_6754		;6765	28 ed		( .
	ret			;6767	c9		.
R_6768:
	ld bc,0f7feh		;6768	01 fe f7	. . .
	in a,(c)		;676b	ed 78		. x
	srl a			;676d	cb 3f		. ?
	jr nc,l67cdh		;676f	30 5c		0 \
	srl a			;6771	cb 3f		. ?
	jr nc,l67d1h		;6773	30 5c		0 \
	srl a			;6775	cb 3f		. ?
	jr nc,l67d5h		;6777	30 5c		0 \
	srl a			;6779	cb 3f		. ?
	jr nc,l67d9h		;677b	30 5c		0 \
	srl a			;677d	cb 3f		. ?
	jr nc,l67ddh		;677f	30 5c		0 \
	ld bc,0effeh		;6781	01 fe ef	. . .
	in a,(c)		;6784	ed 78		. x
	srl a			;6786	cb 3f		. ?
	jr nc,l67c9h		;6788	30 3f		0 ?
	srl a			;678a	cb 3f		. ?
	jr nc,l67edh		;678c	30 5f		0 _
	srl a			;678e	cb 3f		. ?
	jr nc,l67e9h		;6790	30 57		0 W
	srl a			;6792	cb 3f		. ?
	jr nc,l67e5h		;6794	30 4f		0 O
	srl a			;6796	cb 3f		. ?
	jr nc,l67e1h		;6798	30 47		0 G
	ld bc,0bffeh		;679a	01 fe bf	. . .
	in a,(c)		;679d	ed 78		. x
	bit 4,a			;679f	cb 67		. g
	jr z,l67f1h		;67a1	28 4e		( N
	ld bc,07ffeh		;67a3	01 fe 7f	. . .
	in a,(c)		;67a6	ed 78		. x
	bit 2,a			;67a8	cb 57		. W
	jr z,l67f5h		;67aa	28 49		( I
	ld bc,0fbfeh		;67ac	01 fe fb	. . .
	in a,(c)		;67af	ed 78		. x
	bit 2,a			;67b1	cb 57		. W
	jr z,l67f9h		;67b3	28 44		( D
	ld bc,0fdfeh		;67b5	01 fe fd	. . .
	in a,(c)		;67b8	ed 78		. x
	bit 1,a			;67ba	cb 4f		. O
	jr z,l67fdh		;67bc	28 3f		( ?
	ld bc,0fefeh		;67be	01 fe fe	. . .
	in a,(c)		;67c1	ed 78		. x
	bit 2,a			;67c3	cb 57		. W
	jr z,l6801h		;67c5	28 3a		( :
	xor a			;67c7	af		.
	ret			;67c8	c9		.
l67c9h:
	ld a,030h		;67c9	3e 30		> 0
	or a			;67cb	b7		.
	ret			;67cc	c9		.
l67cdh:
	ld a,031h		;67cd	3e 31		> 1
	or a			;67cf	b7		.
	ret			;67d0	c9		.
l67d1h:
	ld a,032h		;67d1	3e 32		> 2
	or a			;67d3	b7		.
	ret			;67d4	c9		.
l67d5h:
	ld a,033h		;67d5	3e 33		> 3
	or a			;67d7	b7		.
	ret			;67d8	c9		.
l67d9h:
	ld a,034h		;67d9	3e 34		> 4
	or a			;67db	b7		.
	ret			;67dc	c9		.
l67ddh:
	ld a,035h		;67dd	3e 35		> 5
	or a			;67df	b7		.
	ret			;67e0	c9		.
l67e1h:
	ld a,036h		;67e1	3e 36		> 6
	or a			;67e3	b7		.
	ret			;67e4	c9		.
l67e5h:
	ld a,037h		;67e5	3e 37		> 7
	or a			;67e7	b7		.
	ret			;67e8	c9		.
l67e9h:
	ld a,038h		;67e9	3e 38		> 8
	or a			;67eb	b7		.
	ret			;67ec	c9		.
l67edh:
	ld a,039h		;67ed	3e 39		> 9
	or a			;67ef	b7		.
	ret			;67f0	c9		.
l67f1h:
	ld a,048h		;67f1	3e 48		> H
	or a			;67f3	b7		.
	ret			;67f4	c9		.
l67f5h:
	ld a,04dh		;67f5	3e 4d		> M
	or a			;67f7	b7		.
	ret			;67f8	c9		.
l67f9h:
	ld a,045h		;67f9	3e 45		> E
	or a			;67fb	b7		.
	ret			;67fc	c9		.
l67fdh:
	ld a,053h		;67fd	3e 53		> S
	or a			;67ff	b7		.
	ret			;6800	c9		.
l6801h:
	ld a,03ah		;6801	3e 3a		> :
	or a			;6803	b7		.
	ret			;6804	c9		.
R_6805:
	xor a			;6805	af		.
	in a,(0feh)		;6806	db fe		. .
	and 01fh		;6808	e6 1f		. .
	cp 01fh			;680a	fe 1f		. .
	ret			;680c	c9		.
R_680D:
	call R_6805		;680d	cd 05 68	. . h
	jr nz,R_680D		;6810	20 fb		  .
	ret			;6812	c9		.
l6813h:
	call R_6805		;6813	cd 05 68	. . h
	jr z,l6813h		;6816	28 fb		( .
	ret			;6818	c9		.
; --------------------------------------------------------------------------
; Clears the attribute area of the results screen.
; --------------------------------------------------------------------------
CLEAR_ATTRS:
	ld hl,05800h		;6819	21 00 58	! . X
	ld d,h			;681c	54		T
	ld e,l			;681d	5d		]
	inc de			;681e	13		.
	ld (hl),l		;681f	75		u
	ld bc,002ffh		;6820	01 ff 02	. . .
	ldir			;6823	ed b0		. .
	ret			;6825	c9		.
R_6826:
	ld b,a			;6826	47		G
	ld a,(06ecdh)		;6827	3a cd 6e	: . n
	cp 003h			;682a	fe 03		. .
	ret z			;682c	c8		.
	ld a,b			;682d	78		x
	ld hl,05880h		;682e	21 80 58	! . X
l6831h:
	ld (hl),000h		;6831	36 00		6 .
	ld d,h			;6833	54		T
	ld e,l			;6834	5d		]
	inc de			;6835	13		.
	ld bc,0001fh		;6836	01 1f 00	. . .
	ldir			;6839	ed b0		. .
	ex de,hl		;683b	eb		.
	dec a			;683c	3d		=
	jr nz,l6831h		;683d	20 f2		  .
	ret			;683f	c9		.
sub_6840h:
	ld a,002h		;6840	3e 02		> .
l6842h:
	ld hl,05a00h		;6842	21 00 5a	! . Z
	ld (hl),a		;6845	77		w
	ld d,h			;6846	54		T
	ld e,l			;6847	5d		]
	inc de			;6848	13		.
	ld bc,000ffh		;6849	01 ff 00	. . .
	ldir			;684c	ed b0		. .
	ret			;684e	c9		.
R_684F:
	ld a,001h		;684f	3e 01		> .
	jr l6842h		;6851	18 ef		. .
; --------------------------------------------------------------------------
; Sets TEXT_BASE (6BD5h) and TEXT_LIST (6BD7h) -> TEXT_STREAM,
; pointing the text VM at the built-in descriptor data.
; --------------------------------------------------------------------------
INIT_TEXT:
	ld hl,TEXT_EN		;6853	21 00 80	! . .
	ld (TEXT_BASE),hl	;6856	22 d5 6b	" . k
	ld hl,TEXT_STREAM	;6859	21 d9 6b	! . k
	ld (TEXT_LIST),hl	;685c	22 d7 6b	" . k
	ld de,00a0ah		;685f	11 0a 0a	. . .
	ld a,000h		;6862	3e 00		> .
	call R_6AB0		;6864	cd b0 6a	. . j
	ld de,00c0ah		;6867	11 0a 0c	. . .
	ld a,001h		;686a	3e 01		> .
	call R_6AB0		;686c	cd b0 6a	. . j
	ld de,01706h		;686f	11 06 17	. . .
	ld a,002h		;6872	3e 02		> .
	call R_6AB0		;6874	cd b0 6a	. . j
	ld de,00004h		;6877	11 04 00	. . .
	ld a,004h		;687a	3e 04		> .
	call R_6AB0		;687c	cd b0 6a	. . j
l687fh:
	call R_6768		;687f	cd 68 67	. h g
	jr z,l687fh		;6882	28 fb		( .
	cp 031h			;6884	fe 31		. 1
	jr z,l6899h		;6886	28 11		( .
	cp 032h			;6888	fe 32		. 2
	jr z,l688eh		;688a	28 02		( .
	jr l687fh		;688c	18 f1		. .
l688eh:
	ld hl,0807ch		;688e	21 7c 80	! | .
	ld de,070e3h		;6891	11 e3 70	. . p
	ld bc,0061dh		;6894	01 1d 06	. . .
	ldir			;6897	ed b0		. .
l6899h:
	ld hl,070e3h		;6899	21 e3 70	! . p
	ld (TEXT_BASE),hl	;689c	22 d5 6b	" . k
	ret			;689f	c9		.
; --------------------------------------------------------------------------
; Draws the help/status page through the text VM.
; --------------------------------------------------------------------------
HELP_PAGE:
	call R_6A92		;68a0	cd 92 6a	. . j
	ld de,00008h		;68a3	11 08 00	. . .
	ld a,000h		;68a6	3e 00		> .
	call R_6AAB		;68a8	cd ab 6a	. . j
	ld de,0010ah		;68ab	11 0a 01	. . .
	ld a,001h		;68ae	3e 01		> .
	call R_6AAB		;68b0	cd ab 6a	. . j
	ld de,01700h		;68b3	11 00 17	. . .
	ld a,001h		;68b6	3e 01		> .
	call R_6AB0		;68b8	cd b0 6a	. . j
	ld de,01300h		;68bb	11 00 13	. . .
	ld a,002h		;68be	3e 02		> .
	call R_6AB0		;68c0	cd b0 6a	. . j
	call R_6A24		;68c3	cd 24 6a	. $ j
	cp 048h			;68c6	fe 48		. H
	ret z			;68c8	c8		.
	cp 045h			;68c9	fe 45		. E
	ret z			;68cb	c8		.
	call SELECT_MODEL	;68cc	cd d5 68	. . h
	ld a,001h		;68cf	3e 01		> .
	or a			;68d1	b7		.
	ret			;68d2	c9		.
l68d3h:
	xor a			;68d3	af		.
	ret			;68d4	c9		.
; --------------------------------------------------------------------------
; Menu key handler: digits 1-9 select models 0-8, X selects 9 (GMX),
; on the second page digits 1-5 select models 10-14 (+10 logic).
; Stores the index at MODEL_INDEX (6F3Fh) - the ONLY place a model
; is chosen. UMT never auto-detects the clone: the only port read
; in the whole program is IN A,(0FEh) for the keyboard.
; --------------------------------------------------------------------------
SELECT_MODEL:
	push af			;68d5	f5		.
	call CLEAR_ATTRS	;68d6	cd 19 68	. . h
	pop af			;68d9	f1		.
	sub 031h		;68da	d6 31		. 1
	jr c,l68d3h		;68dc	38 f5		8 .
	cp 00ah			;68de	fe 0a		. .
	jr nc,l68d3h		;68e0	30 f1		0 .
	ld c,a			;68e2	4f		O
	ld a,(06f15h)		;68e3	3a 15 6f	: . o
	and 001h		;68e6	e6 01		. .
	ld a,00ah		;68e8	3e 0a		> .
	jr nz,l68edh		;68ea	20 01		  .
	xor a			;68ec	af		.
l68edh:
	add a,c			;68ed	81		.
	ld (MODEL_INDEX),a	;68ee	32 3f 6f	2 ? o
	cp 00fh			;68f1	fe 0f		. .
	jr nc,l68d3h		;68f3	30 de		0 .
	ld b,a			;68f5	47		G
	ld de,0001bh		;68f6	11 1b 00	. . .
	inc b			;68f9	04		.
	ld hl,06f30h		;68fa	21 30 6f	! 0 o
l68fdh:
	add hl,de		;68fd	19		.
	djnz l68fdh		;68fe	10 fd		. .
	ld de,NAME_BUFFER	;6900	11 2e 6f	. . o
	ld bc,00010h		;6903	01 10 00	. . .
	ldir			;6906	ed b0		. .
	call R_69DF		;6908	cd df 69	. . i
	jr nz,l6913h		;690b	20 06		  .
	call CLEAR_ATTRS	;690d	cd 19 68	. . h
	call R_6916		;6910	cd 16 69	. . i
l6913h:
	ld a,001h		;6913	3e 01		> .
	ret			;6915	c9		.
R_6916:
	call R_6991		;6916	cd 91 69	. . i
	ld de,00004h		;6919	11 04 00	. . .
	ld a,009h		;691c	3e 09		> .
	call R_6AB0		;691e	cd b0 6a	. . j
	call R_69D7		;6921	cd d7 69	. . i
	call R_6968		;6924	cd 68 69	. h i
	ld b,008h		;6927	06 08		. .
	ld de,00400h		;6929	11 00 04	. . .
l692ch:
	push bc			;692c	c5		.
	ld a,009h		;692d	3e 09		> .
	call R_6AAB		;692f	cd ab 6a	. . j
	pop bc			;6932	c1		.
	djnz l692ch		;6933	10 f7		. .
	call R_69AB		;6935	cd ab 69	. . i
	ld a,b			;6938	78		x
	ld (l6940h+1),a		;6939	32 41 69	2 A i
	xor a			;693c	af		.
	ld (l66f0h),a		;693d	32 f0 66	2 . f
l6940h:
	ld a,001h		;6940	3e 01		> .
	call R_6826		;6942	cd 26 68	. & h
	xor a			;6945	af		.
	ld (PAGE_NUM),a		;6946	32 a6 60	2 . `
	call 00000h		;6949	cd 00 00	. . .
	ld a,(l6530h)		;694c	3a 30 65	: 0 e
	or a			;694f	b7		.
	ld de,01700h		;6950	11 00 17	. . .
	ld a,00eh		;6953	3e 0e		> .
	call nz,R_6AB0		;6955	c4 b0 6a	. . j
	ld a,(l66f1h)		;6958	3a f1 66	: . f
	or a			;695b	b7		.
	call nz,sub_6840h	;695c	c4 40 68	. @ h
	call R_6805		;695f	cd 05 68	. . h
	jr z,l6940h		;6962	28 dc		( .
	call R_680D		;6964	cd 0d 68	. . h
	ret			;6967	c9		.
R_6968:
	ld a,(06ecdh)		;6968	3a cd 6e	: . n
	cp 003h			;696b	fe 03		. .
	jr z,l6987h		;696d	28 18		( .
	cp 002h			;696f	fe 02		. .
	ld hl,CALL_W3		;6971	21 fe 63	! . c
	ld b,006h		;6974	06 06		. .
	jr z,l697dh		;6976	28 05		( .
	ld hl,CALL_W2		;6978	21 f1 62	! . b
	ld b,005h		;697b	06 05		. .
l697dh:
	ld (0694ah),hl		;697d	22 4a 69	" J i
	ld a,b			;6980	78		x
	ld de,00e00h		;6981	11 00 0e	. . .
	jp R_6AB0		;6984	c3 b0 6a	. . j
l6987h:
	call R_684F		;6987	cd 4f 68	. O h
	ld hl,CALL_W4		;698a	21 33 65	! 3 e
	ld b,007h		;698d	06 07		. .
	jr l697dh		;698f	18 ec		. .
R_6991:
	ld a,(06ecdh)		;6991	3a cd 6e	: . n
	ld hl,00000h		;6994	21 00 00	! . .
	ld de,0ffffh		;6997	11 ff ff	. . .
	or a			;699a	b7		.
	jr z,l69a3h		;699b	28 06		( .
	ld hl,05555h		;699d	21 55 55	! U U
	ld de,0aaaah		;69a0	11 aa aa	. . .
l69a3h:
	ld (SWEEP_PTR),hl	;69a3	22 a9 60	" . `
	ld (SWEEP_PTR2),de	;69a6	ed 53 ab 60	. S . `
	ret			;69aa	c9		.
R_69AB:
	ld a,(MODEL_INDEX)	;69ab	3a 3f 6f	: ? o
	ld b,a			;69ae	47		G
	inc b			;69af	04		.
	ld hl,06f25h		;69b0	21 25 6f	! % o
	ld de,0001bh		;69b3	11 1b 00	. . .
l69b6h:
	add hl,de		;69b6	19		.
	djnz l69b6h		;69b7	10 fd		. .
	ld a,(hl)		;69b9	7e		~
	push af			;69ba	f5		.
	ld de,00300h		;69bb	11 00 03	. . .
	call R_6AAB		;69be	cd ab 6a	. . j
	pop af			;69c1	f1		.
	ld b,008h		;69c2	06 08		. .
	cp 00ah			;69c4	fe 0a		. .
	jr nz,l69cah		;69c6	20 02		  .
	ld b,001h		;69c8	06 01		. .
l69cah:
	cp 004h			;69ca	fe 04		. .
	jr nz,l69d0h		;69cc	20 02		  .
	ld b,002h		;69ce	06 02		. .
l69d0h:
	cp 005h			;69d0	fe 05		. .
	jr nz,l69d6h		;69d2	20 02		  .
	ld b,004h		;69d4	06 04		. .
l69d6h:
	ret			;69d6	c9		.
R_69D7:
	ld de,00100h		;69d7	11 00 01	. . .
	ld a,00ah		;69da	3e 0a		> .
	jp R_6AB0		;69dc	c3 b0 6a	. . j
R_69DF:
	call R_69D7		;69df	cd d7 69	. . i
	call R_6347		;69e2	cd 47 63	. G c
	ld a,(l63fdh)		;69e5	3a fd 63	: . c
	or a			;69e8	b7		.
	ret z			;69e9	c8		.
	ld a,(05d00h)		;69ea	3a 00 5d	: . ]
	ld hl,070d7h		;69ed	21 d7 70	! . p
	call R_6735		;69f0	cd 35 67	. 5 g
	ld de,00a04h		;69f3	11 04 0a	. . .
	ld a,00bh		;69f6	3e 0b		> .
	call R_6AB0		;69f8	cd b0 6a	. . j
	ld de,01700h		;69fb	11 00 17	. . .
	ld a,00dh		;69fe	3e 0d		> .
	call R_6AB0		;6a00	cd b0 6a	. . j
	ld de,00c00h		;6a03	11 00 0c	. . .
	ld a,00fh		;6a06	3e 0f		> .
	call R_6AB0		;6a08	cd b0 6a	. . j
	ld a,(MODEL_INDEX)	;6a0b	3a 3f 6f	: ? o
	or a			;6a0e	b7		.
	ld de,01000h		;6a0f	11 00 10	. . .
	ld a,00ch		;6a12	3e 0c		> .
	call z,R_6AB0		;6a14	cc b0 6a	. . j
	call R_6702		;6a17	cd 02 67	. . g
	call R_6768		;6a1a	cd 68 67	. h g
	cp 053h			;6a1d	fe 53		. S
	ret z			;6a1f	c8		.
	ld a,001h		;6a20	3e 01		> .
	or a			;6a22	b7		.
	ret			;6a23	c9		.
R_6A24:
	ld de,00400h		;6a24	11 00 04	. . .
	ld a,003h		;6a27	3e 03		> .
	call R_6AB0		;6a29	cd b0 6a	. . j
	ld de,00500h		;6a2c	11 00 05	. . .
	ld a,004h		;6a2f	3e 04		> .
	call R_6AB0		;6a31	cd b0 6a	. . j
	ld a,(06f15h)		;6a34	3a 15 6f	: . o
	and 001h		;6a37	e6 01		. .
	add a,002h		;6a39	c6 02		. .
	ld de,00705h		;6a3b	11 05 07	. . .
	call R_6AAB		;6a3e	cd ab 6a	. . j
	call R_680D		;6a41	cd 0d 68	. . h
l6a44h:
	call R_6768		;6a44	cd 68 67	. h g
	jr z,l6a44h		;6a47	28 fb		( .
	cp 048h			;6a49	fe 48		. H
	ret z			;6a4b	c8		.
	cp 045h			;6a4c	fe 45		. E
	ret z			;6a4e	c8		.
	cp 030h			;6a4f	fe 30		. 0
	jr z,l6a60h		;6a51	28 0d		( .
	cp 04dh			;6a53	fe 4d		. M
	jr z,l6a6bh		;6a55	28 14		( .
	cp 031h			;6a57	fe 31		. 1
	jr c,R_6A24		;6a59	38 c9		8 .
	cp 03bh			;6a5b	fe 3b		. ;
	jr nc,R_6A24		;6a5d	30 c5		0 .
	ret			;6a5f	c9		.
l6a60h:
	ld hl,06ecdh		;6a60	21 cd 6e	! . n
	ld de,06ebeh		;6a63	11 be 6e	. . n
	call R_6A76		;6a66	cd 76 6a	. v j
	jr R_6A24		;6a69	18 b9		. .
l6a6bh:
	ld hl,06f15h		;6a6b	21 15 6f	! . o
	ld de,06f0ah		;6a6e	11 0a 6f	. . o
	call R_6A76		;6a71	cd 76 6a	. v j
	jr R_6A24		;6a74	18 ae		. .
R_6A76:
	ld a,(hl)		;6a76	7e		~
	push hl			;6a77	e5		.
	inc hl			;6a78	23		#
	ld c,(hl)		;6a79	4e		N
	inc hl			;6a7a	23		#
	ld b,(hl)		;6a7b	46		F
	inc hl			;6a7c	23		#
	inc a			;6a7d	3c		<
	cp b			;6a7e	b8		.
	jr c,l6a82h		;6a7f	38 01		8 .
	xor a			;6a81	af		.
l6a82h:
	ld b,000h		;6a82	06 00		. .
	push af			;6a84	f5		.
	or a			;6a85	b7		.
	jr z,l6a8ch		;6a86	28 04		( .
l6a88h:
	add hl,bc		;6a88	09		.
	dec a			;6a89	3d		=
	jr nz,l6a88h		;6a8a	20 fc		  .
l6a8ch:
	ldir			;6a8c	ed b0		. .
	pop af			;6a8e	f1		.
	pop hl			;6a8f	e1		.
	ld (hl),a		;6a90	77		w
	ret			;6a91	c9		.
R_6A92:
	ld a,000h		;6a92	3e 00		> .
	or a			;6a94	b7		.
	ret nz			;6a95	c0		.
	inc a			;6a96	3c		<
	ld (R_6A92+1),a		;6a97	32 93 6a	2 . j
	ld hl,06ecdh		;6a9a	21 cd 6e	! . n
	ld de,06ebeh		;6a9d	11 be 6e	. . n
	call R_6A76		;6aa0	cd 76 6a	. v j
	ld hl,06f15h		;6aa3	21 15 6f	! . o
	ld de,06f0ah		;6aa6	11 0a 6f	. . o
	jr R_6A76		;6aa9	18 cb		. .
R_6AAB:
	ld hl,(TEXT_LIST)	;6aab	2a d7 6b	* . k
	jr l6ab3h		;6aae	18 03		. .
R_6AB0:
	ld hl,(TEXT_BASE)	;6ab0	2a d5 6b	* . k
l6ab3h:
	or a			;6ab3	b7		.
	jr z,l6ac7h		;6ab4	28 11		( .
	push de			;6ab6	d5		.
	ld e,a			;6ab7	5f		_
	ld bc,02000h		;6ab8	01 00 20	. .  
l6abbh:
	xor a			;6abb	af		.
l6abch:
	cpir			;6abc	ed b1		. .
	ld a,(hl)		;6abe	7e		~
	inc hl			;6abf	23		#
	or a			;6ac0	b7		.
	jr nz,l6abbh		;6ac1	20 f8		  .
	dec e			;6ac3	1d		.
	jr nz,l6abch		;6ac4	20 f6		  .
	pop de			;6ac6	d1		.
l6ac7h:
	jp R_6ACA		;6ac7	c3 ca 6a	. . j
R_6ACA:
	ld (TEXT_CURSOR),de	;6aca	ed 53 d3 6b	. S . k
	push hl			;6ace	e5		.
	ld a,d			;6acf	7a		z
	add a,a			;6ad0	87		.
	ld h,05fh		;6ad1	26 5f		& _
	ld l,a			;6ad3	6f		o
	ld b,(hl)		;6ad4	46		F
	inc hl			;6ad5	23		#
	ld a,(hl)		;6ad6	7e		~
	add a,e			;6ad7	83		.
	ld c,a			;6ad8	4f		O
	ld a,d			;6ad9	7a		z
	add a,a			;6ada	87		.
	ld h,05bh		;6adb	26 5b		& [
	ld l,a			;6add	6f		o
	ld a,(hl)		;6ade	7e		~
	add a,e			;6adf	83		.
	ld e,a			;6ae0	5f		_
	inc hl			;6ae1	23		#
	ld d,(hl)		;6ae2	56		V
	pop hl			;6ae3	e1		.
	ld a,(ATTR_TMP)		;6ae4	3a d2 6b	: . k
	ld (ATTR_CUR),a		;6ae7	32 d1 6b	2 . k
R_6AEA:
	ld a,(hl)		;6aea	7e		~
	or a			;6aeb	b7		.
	jr z,l6b06h		;6aec	28 18		( .
	cp 00dh			;6aee	fe 0d		. .
	jr z,l6b39h		;6af0	28 47		( G
	cp 0feh			;6af2	fe fe		. .
	call z,sub_6b42h	;6af4	cc 42 6b	. B k
	jr z,R_6AEA		;6af7	28 f1		( .
	cp 009h			;6af9	fe 09		. .
	call c,sub_6b62h	;6afb	dc 62 6b	. b k
	jr c,R_6AEA		;6afe	38 ea		8 .
	call R_6B0C		;6b00	cd 0c 6b	. . k
	jp R_6AEA		;6b03	c3 ea 6a	. . j
l6b06h:
	ld de,(TEXT_CURSOR)	;6b06	ed 5b d3 6b	. [ . k
	inc d			;6b0a	14		.
	ret			;6b0b	c9		.
R_6B0C:
	push hl			;6b0c	e5		.
	push de			;6b0d	d5		.
	ld h,078h		;6b0e	26 78		& x
	ld l,a			;6b10	6f		o
	ld a,(hl)		;6b11	7e		~
	ld (de),a		;6b12	12		.
	inc h			;6b13	24		$
	inc d			;6b14	14		.
	ld a,(hl)		;6b15	7e		~
	ld (de),a		;6b16	12		.
	inc h			;6b17	24		$
	inc d			;6b18	14		.
	ld a,(hl)		;6b19	7e		~
	ld (de),a		;6b1a	12		.
	inc h			;6b1b	24		$
	inc d			;6b1c	14		.
	ld a,(hl)		;6b1d	7e		~
	ld (de),a		;6b1e	12		.
	inc h			;6b1f	24		$
	inc d			;6b20	14		.
	ld a,(hl)		;6b21	7e		~
	ld (de),a		;6b22	12		.
	inc h			;6b23	24		$
	inc d			;6b24	14		.
	ld a,(hl)		;6b25	7e		~
	ld (de),a		;6b26	12		.
	inc h			;6b27	24		$
	inc d			;6b28	14		.
	ld a,(hl)		;6b29	7e		~
	ld (de),a		;6b2a	12		.
	inc h			;6b2b	24		$
	inc d			;6b2c	14		.
	ld a,(hl)		;6b2d	7e		~
	ld (de),a		;6b2e	12		.
	pop de			;6b2f	d1		.
	pop hl			;6b30	e1		.
	ld a,(ATTR_CUR)		;6b31	3a d1 6b	: . k
	ld (bc),a		;6b34	02		.
	inc bc			;6b35	03		.
	inc hl			;6b36	23		#
	inc e			;6b37	1c		.
	ret			;6b38	c9		.
l6b39h:
	ld de,(TEXT_CURSOR)	;6b39	ed 5b d3 6b	. [ . k
	inc d			;6b3d	14		.
	inc hl			;6b3e	23		#
	jp R_6ACA		;6b3f	c3 ca 6a	. . j
sub_6b42h:
	inc hl			;6b42	23		#
	ld a,(hl)		;6b43	7e		~
	inc hl			;6b44	23		#
	ex af,af'		;6b45	08		.
	ld a,(hl)		;6b46	7e		~
	inc hl			;6b47	23		#
	push hl			;6b48	e5		.
	ld h,a			;6b49	67		g
	ex af,af'		;6b4a	08		.
	ld l,a			;6b4b	6f		o
	call R_6B52		;6b4c	cd 52 6b	. R k
	pop hl			;6b4f	e1		.
	xor a			;6b50	af		.
	ret			;6b51	c9		.
R_6B52:
	ld a,(hl)		;6b52	7e		~
	or a			;6b53	b7		.
	ret z			;6b54	c8		.
	cp 009h			;6b55	fe 09		. .
	call c,sub_6b62h	;6b57	dc 62 6b	. b k
	jr c,R_6B52		;6b5a	38 f6		8 .
	call R_6B0C		;6b5c	cd 0c 6b	. . k
	jp R_6B52		;6b5f	c3 52 6b	. R k
sub_6b62h:
	ex af,af'		;6b62	08		.
	inc hl			;6b63	23		#
	ld a,(hl)		;6b64	7e		~
	cp 009h			;6b65	fe 09		. .
	jr c,l6b70h		;6b67	38 07		8 .
	ex af,af'		;6b69	08		.
	dec hl			;6b6a	2b		+
	ld (ATTR_CUR),a		;6b6b	32 d1 6b	2 . k
	xor a			;6b6e	af		.
	ret			;6b6f	c9		.
l6b70h:
	exx			;6b70	d9		.
	ld c,a			;6b71	4f		O
	ex af,af'		;6b72	08		.
	cp c			;6b73	b9		.
	jr z,l6b83h		;6b74	28 0d		( .
	dec a			;6b76	3d		=
	dec c			;6b77	0d		.
	add a,a			;6b78	87		.
	add a,a			;6b79	87		.
	add a,a			;6b7a	87		.
	or c			;6b7b	b1		.
l6b7ch:
	ld (ATTR_CUR),a		;6b7c	32 d1 6b	2 . k
	scf			;6b7f	37		7
	exx			;6b80	d9		.
	inc hl			;6b81	23		#
	ret			;6b82	c9		.
l6b83h:
	or 040h			;6b83	f6 40		. @
	jr l6b7ch		;6b85	18 f5		. .
; --------------------------------------------------------------------------
; Fills 5B00h with screen addresses for the results grid.
; --------------------------------------------------------------------------
BUILD_SCREEN_ADDR_TABLE:
	ld hl,05b00h		;6b87	21 00 5b	! . [
	ld de,04000h		;6b8a	11 00 40	. . @
	ld b,0c0h		;6b8d	06 c0		. .
l6b8fh:
	ld (hl),e		;6b8f	73		s
	inc hl			;6b90	23		#
	ld (hl),d		;6b91	72		r
	inc hl			;6b92	23		#
	inc d			;6b93	14		.
	ld a,d			;6b94	7a		z
	and 007h		;6b95	e6 07		. .
	jp nz,l6ba4h		;6b97	c2 a4 6b	. . k
	ld a,e			;6b9a	7b		{
	add a,020h		;6b9b	c6 20		.  
	ld e,a			;6b9d	5f		_
	jr c,l6ba4h		;6b9e	38 04		8 .
	ld a,d			;6ba0	7a		z
	sub 008h		;6ba1	d6 08		. .
	ld d,a			;6ba3	57		W
l6ba4h:
	djnz l6b8fh		;6ba4	10 e9		. .
	ld hl,05b00h		;6ba6	21 00 5b	! . [
	ld de,05b00h		;6ba9	11 00 5b	. . [
	ld a,018h		;6bac	3e 18		> .
l6baeh:
	ldi			;6bae	ed a0		. .
	ldi			;6bb0	ed a0		. .
	ld bc,0000eh		;6bb2	01 0e 00	. . .
	add hl,bc		;6bb5	09		.
	dec a			;6bb6	3d		=
	jr nz,l6baeh		;6bb7	20 f5		  .
	ld hl,05800h		;6bb9	21 00 58	! . X
	ld de,05f00h		;6bbc	11 00 5f	. . _
	ld b,018h		;6bbf	06 18		. .
l6bc1h:
	push bc			;6bc1	c5		.
	ld a,h			;6bc2	7c		|
	ld (de),a		;6bc3	12		.
	inc de			;6bc4	13		.
	ld a,l			;6bc5	7d		}
	ld (de),a		;6bc6	12		.
	inc de			;6bc7	13		.
	ld bc,0001fh		;6bc8	01 1f 00	. . .
	add hl,bc		;6bcb	09		.
	inc hl			;6bcc	23		#
	pop bc			;6bcd	c1		.
	djnz l6bc1h		;6bce	10 f1		. .
	ret			;6bd0	c9		.

; --------------------------------------------------------------------------
; DATA byte - text VM current attribute byte
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x6BD1-0x6BD8  text VM variables
; --------------------------------------------------------------------------
ATTR_CUR:
	defb 0x00
ATTR_TMP:
	defb 0x00
TEXT_CURSOR:
	defw 0x0000
TEXT_BASE:
	defw 0x8000
TEXT_LIST:
	defw 0x6BD9

; --------------------------------------------------------------------------
; DATA - screen descriptor stream: attribute pairs (e.g. 0707h),
; ASCII runs, 0Dh = newline, 00h = end, FFh = page marker. Holds
; the version line, both model menu pages, blank filler lines,
; memory-size box templates (256K/512K/1024/2048/4096), the '*'
; fill string and the test description lines.
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x6BD9-0x6F2D  TEXT_STREAM - screen descriptors (menus/boxes/help)
; --------------------------------------------------------------------------
TEXT_STREAM:
	defb 0x07,0x07,0xFE,0xDA,0x70,0x05,0x05,0x20,0x28,0x42,0x45,0x54,0x41,0x29,0x00,0x00 ; '\x07\x07\xFE\xDAp\x05\x05 (BETA)\0\0'
	defb 0x01,0x08,0x42,0x55,0x44,0x44,0x45,0x52,0x2F,0x4D,0x47,0x4E,0x00,0x00,0x04,0x04 ; '\x01\x08BUDDER/MGN\0\0\x04\x04'
	defb 0x31,0x01,0x08,0x2E,0x50,0x65,0x6E,0x74,0x61,0x67,0x6F,0x6E,0x31,0x30,0x32,0x34 ; '1\x01\x08.Pentagon1024'
	defb 0x20,0x20,0x20,0x20,0x0D,0x04,0x04,0x32,0x01,0x08,0x2E,0x53,0x63,0x6F,0x72,0x70 ; '    \r\x04\x042\x01\x08.Scorp'
	defb 0x69,0x6F,0x6E,0x31,0x30,0x32,0x34,0x20,0x20,0x20,0x20,0x0D,0x04,0x04,0x33,0x01 ; 'ion1024    \r\x04\x043\x01'
	defb 0x08,0x2E,0x4B,0x41,0x59,0x31,0x30,0x32,0x34,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '\x08.KAY1024       '
	defb 0x20,0x20,0x0D,0x04,0x04,0x34,0x01,0x08,0x2E,0x4B,0x41,0x59,0x32,0x30,0x34,0x38 ; '  \r\x04\x044\x01\x08.KAY2048'
	defb 0x28,0x50,0x68,0x6F,0x65,0x6E,0x69,0x78,0x29,0x0D,0x04,0x04,0x35,0x01,0x08,0x2E ; '(Phoenix)\r\x04\x045\x01\x08.'
	defb 0x50,0x72,0x6F,0x66,0x69,0x31,0x30,0x32,0x34,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; 'Profi1024       '
	defb 0x0D,0x04,0x04,0x36,0x01,0x08,0x2E,0x41,0x54,0x4D,0x34,0x2E,0x35,0x28,0x31,0x30 ; '\r\x04\x046\x01\x08.ATM4.5(10'
	defb 0x32,0x34,0x29,0x20,0x20,0x20,0x20,0x0D,0x04,0x04,0x37,0x01,0x08,0x2E,0x41,0x54 ; '24)    \r\x04\x047\x01\x08.AT'
	defb 0x4D,0x37,0x2E,0x31,0x28,0x31,0x30,0x32,0x34,0x29,0x20,0x20,0x20,0x20,0x0D,0x04 ; 'M7.1(1024)    \r\x04'
	defb 0x04,0x38,0x01,0x08,0x2E,0x50,0x65,0x6E,0x74,0x45,0x76,0x6F,0x28,0x54,0x53,0x43 ; '\x048\x01\x08.PentEvo(TSC'
	defb 0x6F,0x6E,0x66,0x29,0x20,0x0D,0x04,0x04,0x39,0x01,0x08,0x2E,0x53,0x70,0x72,0x69 ; 'onf) \r\x04\x049\x01\x08.Spri'
	defb 0x6E,0x74,0x65,0x72,0x28,0x34,0x30,0x39,0x36,0x4B,0x29,0x20,0x0D,0x04,0x04,0x58 ; 'nter(4096K) \r\x04\x04X'
	defb 0x01,0x08,0x2E,0x47,0x4D,0x58,0x28,0x32,0x30,0x34,0x38,0x29,0x20,0x20,0x20,0x20 ; '\x01\x08.GMX(2048)    '
	defb 0x20,0x20,0x20,0x00,0x00,0x04,0x04,0x31,0x01,0x08,0x2E,0x53,0x63,0x6F,0x72,0x70 ; '   \0\0\x04\x041\x01\x08.Scorp'
	defb 0x69,0x6F,0x6E,0x32,0x35,0x36,0x28,0x4B,0x41,0x59,0x29,0x0D,0x04,0x04,0x32,0x01 ; 'ion256(KAY)\r\x04\x042\x01'
	defb 0x08,0x2E,0x50,0x65,0x6E,0x74,0x61,0x67,0x6F,0x6E,0x35,0x31,0x32,0x20,0x20,0x20 ; '\x08.Pentagon512   '
	defb 0x20,0x20,0x0D,0x04,0x04,0x33,0x01,0x08,0x2E,0x50,0x72,0x6F,0x66,0x69,0x35,0x31 ; '  \r\x04\x043\x01\x08.Profi51'
	defb 0x32,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x04,0x04,0x34,0x01,0x08,0x2E ; '2        \r\x04\x044\x01\x08.'
	defb 0x41,0x54,0x4D,0x34,0x2E,0x35,0x28,0x35,0x31,0x32,0x29,0x20,0x20,0x20,0x20,0x20 ; 'ATM4.5(512)     '
	defb 0x0D,0x04,0x04,0x35,0x01,0x08,0x2E,0x53,0x70,0x65,0x63,0x74,0x72,0x75,0x6D,0x20 ; '\r\x04\x045\x01\x08.Spectrum '
	defb 0x31,0x32,0x38,0x4B,0x20,0x20,0x20,0x0D,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '128K   \r        '
	defb 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x20,0x20,0x20,0x20,0x20 ; '          \r     '
	defb 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x20,0x20 ; '             \r  '
	defb 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '                '
	defb 0x0D,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '\r               '
	defb 0x20,0x20,0x20,0x0D,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '   \r            '
	defb 0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x07,0x07,0xDA,0xC4,0x32,0x35,0x36,0x4B ; '      \0\0\x07\x07\xDA\xC4256K'
	defb 0xC4,0xBF,0x0D,0x0D,0x0D,0x07,0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0x00 ; '\xC4\xBF\r\r\r\x07\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\0'
	defb 0x00,0x07,0x07,0xDA,0xC4,0x35,0x31,0x32,0x4B,0xC4,0xBF,0x0D,0x0D,0x0D,0x0D,0x0D ; '\0\x07\x07\xDA\xC4512K\xC4\xBF\r\r\r\r\r'
	defb 0x07,0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0x00,0x00,0x07,0x07,0xDA,0xC4 ; '\x07\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\0\0\x07\x07\xDA\xC4'
	defb 0x31,0x30,0x32,0x34,0xC4,0xBF,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x07 ; '1024\xC4\xBF\r\r\r\r\r\r\r\r\r\x07'
	defb 0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0x00,0x00,0x07,0x07,0xDA,0xC4,0x31 ; '\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\0\0\x07\x07\xDA\xC41'
	defb 0x30,0x32,0x34,0xC4,0xBF,0xDA,0xC4,0x32,0x30,0x34,0x38,0xC4,0xBF,0x0D,0x0D,0x0D ; '024\xC4\xBF\xDA\xC42048\xC4\xBF\r\r\r'
	defb 0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x07,0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9 ; '\r\r\r\r\r\r\x07\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9'
	defb 0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0x00,0x00,0x07,0x07,0xDA,0xC4,0x31,0x30 ; '\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\0\0\x07\x07\xDA\xC410'
	defb 0x32,0x34,0xC4,0xBF,0xDA,0xC4,0x32,0x30,0x34,0x38,0xC4,0xBF,0xDA,0xC4,0x33,0x30 ; '24\xC4\xBF\xDA\xC42048\xC4\xBF\xDA\xC430'
	defb 0x37,0x32,0xC4,0xBF,0xDA,0xC4,0x34,0x30,0x39,0x36,0xC4,0xBF,0x0D,0x0D,0x0D,0x0D ; '72\xC4\xBF\xDA\xC44096\xC4\xBF\r\r\r\r'
	defb 0x0D,0x0D,0x0D,0x0D,0x0D,0x07,0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0xC0 ; '\r\r\r\r\r\x07\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\xC0'
	defb 0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0xC0 ; '\xC4\xC4\xC4\xC4\xC4\xC4\xD9\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xD9\xC0'
	defb 0xC4,0xC4,0xC4,0xC4,0xC4,0xC4,0xD9,0x00,0x00,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A ; '\xC4\xC4\xC4\xC4\xC4\xC4\xD9\0\0*******'
	defb 0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A ; '****************'
	defb 0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x2A,0x00,0x00,0x07,0x07,0xDA,0xC4,0x31 ; '*********\0\0\x07\x07\xDA\xC41'
	defb 0x32,0x38,0x4B,0xC4,0xBF,0x0D,0x0D,0x07,0x07,0xC0,0xC4,0xC4,0xC4,0xC4,0xC4,0xC4 ; '28K\xC4\xBF\r\r\x07\x07\xC0\xC4\xC4\xC4\xC4\xC4\xC4'
	defb 0xD9,0x00,0x00,0x07,0x07,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '\xD9\0\0\x07\x07           '
	defb 0x20,0x20,0x20,0x00,0x01,0x0E,0x04,0x56,0x41,0x4C,0x55,0x45,0x53,0x20,0x23,0x30 ; '   \0\x01\x0E\x04VALUES #0'
	defb 0x30,0x2C,0x23,0x46,0x46,0x56,0x41,0x4C,0x55,0x45,0x53,0x20,0x23,0x35,0x35,0x2C ; '0,#FFVALUES #55,'
	defb 0x23,0x41,0x41,0x52,0x41,0x4E,0x44,0x4F,0x4D,0x20,0x56,0x41,0x4C,0x55,0x45,0x53 ; '#AARANDOM VALUES'
	defb 0x20,0x43,0x49,0x52,0x43,0x55,0x4C,0x41,0x52,0x20,0x53,0x48,0x49,0x46,0x54,0x07 ; ' CIRCULAR SHIFT\x07'
	defb 0x07,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0xFF,0x0A,0x02,0x31 ; '\x07          \0\xFF\x0A\x021'
	defb 0x30,0x32,0x34,0x2D,0x34,0x30,0x39,0x36,0x4B,0x31,0x32,0x38,0x2D,0x35,0x31,0x32 ; '024-4096K128-512'
	defb 0x4B,0x20,0x20,0x05,0x05                        ; 'K  \x05\x05'

; --------------------------------------------------------------------------
; DATA - 16-byte buffer with the selected model's name, copied
; from MODEL_TABLE for the status line.
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x6F2E-0x6F3D  NAME_BUFFER - selected model name (16 bytes)
; --------------------------------------------------------------------------
NAME_BUFFER:
	defb 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20 ; '                '

; --------------------------------------------------------------------------
; DATA 0x6F3E-0x6F3F  spare byte + MODEL_INDEX
; --------------------------------------------------------------------------
	defb 0x00,0x00

; --------------------------------------------------------------------------
; DATA - 15 entries x 27 bytes: [class byte][w0..w4 LE words]
; [16-char name]. w0 maps a page (ports/bits per clone), w1 marks
; pages, w2 runs pattern tests, w3 checksum tests, w4 rotate tests.
; Class byte: 4=256K, 5=512K, 6=1024K, 7=2048K, 8=4096K models;
; 0Ah for the plain 128K entry (standard #7FFD-only paging).
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x6F40-0x70D4  MODEL_TABLE - 15 x 27-byte model descriptors
; --------------------------------------------------------------------------
MODEL_TABLE:
MTAB_00:		 ; [ 0] Pentagon1024
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_PENTAGON     ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "Pentagon1024    "
MTAB_01:		 ; [ 1] Scorpion1024
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_SCORPION     ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "Scorpion1024    "
MTAB_02:		 ; [ 2] KAY1024
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_KAY          ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "KAY1024         "
MTAB_03:		 ; [ 3] KAY2048(Phoenix)
	defb 0x07		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_KAY          ; w0 map-page
	defw MARK_2M          ; w1 mark
	defw PAT_2M           ; w2 pattern
	defw SUM_2M           ; w3 checksum
	defw ROT_2M           ; w4 rotate
	defb "KAY2048(Phoenix)"
MTAB_04:		 ; [ 4] Profi1024
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_PROFI        ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "Profi1024       "
MTAB_05:		 ; [ 5] ATM4.5(1024)
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_ATM45        ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "ATM4.5(1024)    "
MTAB_06:		 ; [ 6] ATM7.1(1024)
	defb 0x06		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_ATM71        ; w0 map-page
	defw MARK_1M          ; w1 mark
	defw PAT_1M           ; w2 pattern
	defw SUM_1M           ; w3 checksum
	defw ROT_1M           ; w4 rotate
	defb "ATM7.1(1024)    "
MTAB_07:		 ; [ 7] PentEvo (TSConf)
	defb 0x08		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_TSCONF       ; w0 map-page
	defw MARK_4M_TSCONF   ; w1 mark
	defw PAT_4M           ; w2 pattern
	defw SUM_4M           ; w3 checksum
	defw ROT_4M           ; w4 rotate
	defb "PentEvo (TSConf)"
MTAB_08:		 ; [ 8] Sprinter (4096K)
	defb 0x08		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_SPRINTER     ; w0 map-page
	defw MARK_4M_SPRINTER ; w1 mark
	defw PAT_4M           ; w2 pattern
	defw SUM_4M           ; w3 checksum
	defw ROT_4M           ; w4 rotate
	defb "Sprinter (4096K)"
MTAB_09:		 ; [ 9] GMX (2048)
	defb 0x07		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_GMX          ; w0 map-page
	defw MARK_2M          ; w1 mark
	defw PAT_2M           ; w2 pattern
	defw SUM_2M           ; w3 checksum
	defw ROT_2M           ; w4 rotate
	defb "GMX (2048)      "
MTAB_10:		 ; [10] Scorpion256(KAY)
	defb 0x04		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_SCORPION     ; w0 map-page
	defw MARK_256K        ; w1 mark
	defw PAT_256K         ; w2 pattern
	defw SUM_256K         ; w3 checksum
	defw ROT_256K         ; w4 rotate
	defb "Scorpion256(KAY)"
MTAB_11:		 ; [11] Pentagon512
	defb 0x05		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_PENTAGON     ; w0 map-page
	defw MARK_512K        ; w1 mark
	defw PAT_512K         ; w2 pattern
	defw SUM_512K         ; w3 checksum
	defw ROT_512K         ; w4 rotate
	defb "Pentagon512     "
MTAB_12:		 ; [12] Profi512
	defb 0x05		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_PROFI        ; w0 map-page
	defw MARK_512K        ; w1 mark
	defw PAT_512K         ; w2 pattern
	defw SUM_512K         ; w3 checksum
	defw ROT_512K         ; w4 rotate
	defb "Profi512        "
MTAB_13:		 ; [13] ATM4.5(512)
	defb 0x05		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_ATM45        ; w0 map-page
	defw MARK_512K        ; w1 mark
	defw PAT_512K         ; w2 pattern
	defw SUM_512K         ; w3 checksum
	defw ROT_512K         ; w4 rotate
	defb "ATM4.5(512)     "
MTAB_14:		 ; [14] Spectrum 128K
	defb 0x0A		 ; class byte (4=256K..8=4096K, 0Ah=128K)
	defw MAP_PENTAGON     ; w0 map-page
	defw MARK_128K        ; w1 mark
	defw PAT_128K         ; w2 pattern
	defw SUM_128K         ; w3 checksum
	defw ROT_128K         ; w4 rotate
	defb "Spectrum 128K   "

; --------------------------------------------------------------------------
; DATA 0x70D5-0x70D9  table end marker: 03 03 '0' '0' 00
; --------------------------------------------------------------------------
	defb 0x03,0x03,0x30,0x30,0x00

; --------------------------------------------------------------------------
; DATA - 'UMTv2.3x', zero, attr pair
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x70DA-0x70E2  VERSION_STRING 'UMTv2.3x' + terminator + attr
; --------------------------------------------------------------------------
VERSION_STRING:
	defb 0x55,0x4D,0x54,0x76,0x32,0x2E,0x33,0x78,0x00    ; 'UMTv2.3x\0'

; --------------------------------------------------------------------------
; DATA 0x70E3-0x791F  Russian help texts (custom FONT code page)
; --------------------------------------------------------------------------
	defb 0x07,0x07,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x48,0x45,0x4C,0x50,0x20,0x50,0x41 ; '\x07\x07       HELP PA'
	defb 0x47,0x45,0x20,0x66,0x6F,0x72,0x20,0xFE,0xDA,0x70,0x0D,0x0D,0x01,0x08,0x63,0x6F ; 'GE for \xFE\xDAp\r\r\x01\x08co'
	defb 0x64,0x65,0x3A,0x20,0x42,0x55,0x44,0x44,0x45,0x52,0x2F,0x4D,0x47,0x4E,0x0D,0x01 ; 'de: BUDDER/MGN\r\x01'
	defb 0x08,0x66,0x6F,0x6E,0x74,0x3A,0x20,0x57,0x49,0x5A,0x41,0x52,0x44,0x2F,0x44,0x54 ; '\x08font: WIZARD/DT'
	defb 0x0D,0x0D,0x0D,0x06,0x06,0x43,0x4F,0x4E,0x54,0x52,0x4F,0x4C,0x53,0x3A,0x0D,0x01 ; '\r\r\r\x06\x06CONTROLS:\r\x01'
	defb 0x06,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; '\x06---------------'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; '----------------'
	defb 0x2D,0x0D,0x01,0x07,0x20,0x20,0x20,0x6B,0x65,0x79,0x20,0x30,0x20,0x01,0x08,0x2D ; '-\r\x01\x07   key 0 \x01\x08-'
	defb 0x20,0x63,0x68,0x6F,0x6F,0x73,0x65,0x20,0x54,0x45,0x53,0x54,0x0D,0x01,0x07,0x20 ; ' choose TEST\r\x01\x07 '
	defb 0x20,0x20,0x6B,0x65,0x79,0x20,0x4D,0x20,0x01,0x08,0x2D,0x20,0x73,0x65,0x6C,0x65 ; '  key M \x01\x08- sele'
	defb 0x63,0x74,0x20,0x54,0x59,0x50,0x45,0x20,0x6F,0x66,0x20,0x52,0x41,0x4D,0x0D,0x0D ; 'ct TYPE of RAM\r\r'
	defb 0x01,0x07,0x20,0x6B,0x65,0x79,0x73,0x20,0x31,0x2D,0x39,0x2C,0x58,0x20,0x01,0x08 ; '\x01\x07 keys 1-9,X \x01\x08'
	defb 0x2D,0x20,0x53,0x54,0x41,0x52,0x54,0x20,0x63,0x68,0x6F,0x73,0x65,0x6E,0x20,0x54 ; '- START chosen T'
	defb 0x45,0x53,0x54,0x0D,0x0D,0x01,0x07,0x20,0x20,0x20,0x6B,0x65,0x79,0x20,0x48,0x20 ; 'EST\r\r\x01\x07   key H '
	defb 0x01,0x08,0x2D,0x20,0x50,0x52,0x49,0x4E,0x54,0x20,0x54,0x48,0x49,0x53,0x20,0x50 ; '\x01\x08- PRINT THIS P'
	defb 0x41,0x47,0x45,0x0D,0x0D,0x01,0x07,0x20,0x20,0x20,0x6B,0x65,0x79,0x20,0x45,0x20 ; 'AGE\r\r\x01\x07   key E '
	defb 0x01,0x08,0x2D,0x20,0x72,0x65,0x74,0x75,0x72,0x6E,0x20,0x74,0x6F,0x20,0x42,0x41 ; '\x01\x08- return to BA'
	defb 0x53,0x49,0x43,0x0D,0x01,0x06,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; 'SIC\r\x01\x06----------'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; '----------------'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x0D,0x0D,0x0D,0x02,0x02,0x46,0x4F,0x52,0x20,0x46 ; '------\r\r\r\x02\x02FOR F'
	defb 0x55,0x4C,0x4C,0x20,0x52,0x41,0x4D,0x20,0x54,0x45,0x53,0x54,0x3A,0x0D,0x07,0x07 ; 'ULL RAM TEST:\r\x07\x07'
	defb 0x55,0x53,0x45,0x20,0x41,0x4C,0x4C,0x20,0x54,0x45,0x53,0x54,0x53,0x21,0x20,0x28 ; 'USE ALL TESTS! ('
	defb 0x4B,0x45,0x59,0x20,0x30,0x29,0x0D,0x0D,0x0D,0x20,0x41,0x4E,0x59,0x20,0x4B,0x45 ; 'KEY 0)\r\r\r ANY KE'
	defb 0x59,0x20,0x2D,0x20,0x52,0x45,0x54,0x55,0x52,0x4E,0x20,0x54,0x4F,0x20,0x4D,0x41 ; 'Y - RETURN TO MA'
	defb 0x49,0x4E,0x20,0x4D,0x45,0x4E,0x55,0x21,0x20,0x00,0x00,0x04,0x04,0x48,0x01,0x04 ; 'IN MENU! \0\0\x04\x04H\x01\x04'
	defb 0x2D,0x03,0x03,0x53,0x48,0x4F,0x57,0x20,0x48,0x45,0x4C,0x50,0x20,0x50,0x41,0x47 ; '-\x03\x03SHOW HELP PAG'
	defb 0x45,0x2E,0x2E,0x2E,0x00,0x00,0x02,0x02,0x57,0x41,0x52,0x4E,0x49,0x4E,0x47,0x3A ; 'E...\0\0\x02\x02WARNING:'
	defb 0x0D,0x07,0x07,0x54,0x48,0x49,0x53,0x20,0x54,0x45,0x53,0x54,0x20,0x57,0x49,0x4C ; '\r\x07\x07THIS TEST WIL'
	defb 0x4C,0x20,0x43,0x4C,0x45,0x41,0x52,0x20,0x41,0x4C,0x4C,0x20,0x52,0x41,0x4D,0x21 ; 'L CLEAR ALL RAM!'
	defb 0x00,0x00,0x04,0x04,0x30,0x01,0x08,0x2D,0x05,0x05,0x54,0x45,0x53,0x54,0x20,0x54 ; '\0\0\x04\x040\x01\x08-\x05\x05TEST T'
	defb 0x59,0x50,0x45,0x3A,0xFE,0xBC,0x6E,0x00,0x00,0x04,0x04,0x4D,0x01,0x08,0x2D,0x05 ; 'YPE:\xFE\xBCn\0\0\x04\x04M\x01\x08-\x05'
	defb 0x05,0x4D,0x45,0x4D,0x4F,0x52,0x59,0x20,0x54,0x59,0x50,0x45,0x3A,0xFE,0x08,0x6F ; '\x05MEMORY TYPE:\xFE\x08o'
	defb 0x00,0x00,0x07,0x07,0x4C,0x45,0x47,0x45,0x4E,0x44,0x3A,0x0D,0x06,0x06,0x2A,0x01 ; '\0\0\x07\x07LEGEND:\r\x06\x06*\x01'
	defb 0x07,0x20,0x2D,0x20,0x73,0x6B,0x69,0x70,0x70,0x65,0x64,0x20,0x50,0x41,0x47,0x45 ; '\x07 - skipped PAGE'
	defb 0x0D,0x04,0x04,0x2A,0x01,0x07,0x20,0x2D,0x20,0x6E,0x6F,0x20,0x65,0x72,0x72,0x6F ; '\r\x04\x04*\x01\x07 - no erro'
	defb 0x72,0x73,0x0D,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D,0x20,0x65,0x72,0x72,0x6F,0x72 ; 'rs\r\x02\x02*\x01\x07 - error'
	defb 0x20,0x28,0x23,0x30,0x30,0x2F,0x35,0x35,0x29,0x0D,0x03,0x03,0x2A,0x01,0x07,0x20 ; ' (#00/55)\r\x03\x03*\x01\x07 '
	defb 0x2D,0x20,0x65,0x72,0x72,0x6F,0x72,0x20,0x28,0x23,0x46,0x46,0x2F,0x41,0x41,0x29 ; '- error (#FF/AA)'
	defb 0x0D,0x0D,0x01,0x08,0x41,0x4C,0x47,0x3A,0x20,0xFE,0xBC,0x6E,0x00,0x00,0x07,0x07 ; '\r\r\x01\x08ALG: \xFE\xBCn\0\0\x07\x07'
	defb 0x4C,0x45,0x47,0x45,0x4E,0x44,0x3A,0x0D,0x06,0x06,0x2A,0x01,0x07,0x20,0x2D,0x20 ; 'LEGEND:\r\x06\x06*\x01\x07 - '
	defb 0x73,0x6B,0x69,0x70,0x70,0x65,0x64,0x0D,0x05,0x05,0x2A,0x01,0x07,0x20,0x2D,0x20 ; 'skipped\r\x05\x05*\x01\x07 - '
	defb 0x6E,0x6F,0x6E,0x2D,0x63,0x68,0x65,0x63,0x6B,0x65,0x64,0x20,0x28,0x66,0x69,0x6C ; 'non-checked (fil'
	defb 0x6C,0x65,0x64,0x29,0x0D,0x04,0x04,0x2A,0x01,0x07,0x20,0x2D,0x20,0x63,0x68,0x65 ; 'led)\r\x04\x04*\x01\x07 - che'
	defb 0x63,0x6B,0x65,0x64,0x20,0x28,0x6E,0x6F,0x20,0x65,0x72,0x72,0x6F,0x72,0x73,0x29 ; 'cked (no errors)'
	defb 0x0D,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D,0x20,0x66,0x6F,0x75,0x6E,0x64,0x20,0x45 ; '\r\x02\x02*\x01\x07 - found E'
	defb 0x52,0x52,0x4F,0x52,0x53,0x0D,0x0D,0x01,0x08,0x41,0x4C,0x47,0x3A,0x20,0xFE,0xBC ; 'RRORS\r\r\x01\x08ALG: \xFE\xBC'
	defb 0x6E,0x00,0x00,0x01,0x01,0x2A,0x01,0x07,0x2F,0x06,0x06,0x2A,0x01,0x07,0x2F,0x05 ; 'n\0\0\x01\x01*\x01\x07/\x06\x06*\x01\x07/\x05'
	defb 0x05,0x2A,0x01,0x07,0x20,0x2D,0x20,0x66,0x69,0x6C,0x6C,0x65,0x64,0x2F,0x73,0x6B ; '\x05*\x01\x07 - filled/sk'
	defb 0x69,0x70,0x70,0x65,0x64,0x2F,0x63,0x68,0x61,0x6E,0x67,0x65,0x64,0x0D,0x20,0x04 ; 'ipped/changed\r \x04'
	defb 0x04,0x2A,0x01,0x07,0x2F,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D,0x20,0x6F,0x6B,0x2F ; '\x04*\x01\x07/\x02\x02*\x01\x07 - ok/'
	defb 0x65,0x72,0x72,0x6F,0x72,0x00,0x00,0x07,0x07,0x52,0x45,0x53,0x00,0x00,0x07,0x07 ; 'error\0\0\x07\x07RES\0\0\x07\x07'
	defb 0x54,0x45,0x53,0x54,0x49,0x4E,0x47,0x20,0x49,0x53,0x20,0x49,0x4E,0x20,0x50,0x52 ; 'TESTING IS IN PR'
	defb 0x4F,0x47,0x52,0x45,0x53,0x53,0x21,0x21,0x21,0x00,0x00,0x06,0x06,0x4D,0x45,0x4D ; 'OGRESS!!!\0\0\x06\x06MEM'
	defb 0x4F,0x52,0x59,0x3A,0xFE,0x2C,0x6F,0x00,0x00,0x02,0x02,0x20,0x20,0x20,0x20,0x57 ; 'ORY:\xFE,o\0\0\x02\x02    W'
	defb 0x52,0x4F,0x4E,0x47,0x20,0x50,0x41,0x47,0x49,0x4E,0x47,0x21,0x21,0x21,0x0D,0x07 ; 'RONG PAGING!!!\r\x07'
	defb 0x07,0x20,0x46,0x41,0x49,0x4C,0x45,0x44,0x20,0x41,0x54,0x20,0x50,0x41,0x47,0x45 ; '\x07 FAILED AT PAGE'
	defb 0x20,0x23,0xFE,0xD5,0x70,0x07,0x07,0x21,0x21,0x00,0x00,0x07,0x07,0x49,0x46,0x20 ; ' #\xFE\xD5p\x07\x07!!\0\0\x07\x07IF '
	defb 0x59,0x4F,0x55,0x20,0x48,0x41,0x53,0x4E,0x54,0x20,0x50,0x45,0x4E,0x54,0x41,0x47 ; 'YOU HASNT PENTAG'
	defb 0x4F,0x4E,0x31,0x30,0x32,0x34,0x2C,0x20,0x54,0x48,0x45,0x4E,0x0D,0x07,0x07,0x50 ; 'ON1024, THEN\r\x07\x07P'
	defb 0x52,0x45,0x53,0x53,0x20,0x02,0x02,0x52,0x45,0x53,0x45,0x54,0x07,0x07,0x21,0x21 ; 'RESS \x02\x02RESET\x07\x07!!'
	defb 0x21,0x20,0x28,0x48,0x69,0x2D,0x4D,0x65,0x6D,0x20,0x42,0x6C,0x6F,0x63,0x6B,0x65 ; '! (Hi-Mem Blocke'
	defb 0x64,0x29,0x00,0x00,0x20,0x41,0x4E,0x59,0x20,0x4B,0x45,0x59,0x20,0x2D,0x20,0x52 ; 'd)\0\0 ANY KEY - R'
	defb 0x45,0x54,0x55,0x52,0x4E,0x20,0x54,0x4F,0x20,0x4D,0x41,0x49,0x4E,0x20,0x4D,0x45 ; 'ETURN TO MAIN ME'
	defb 0x4E,0x55,0x21,0x20,0x00,0x00,0x02,0x02,0x55,0x4E,0x53,0x54,0x41,0x42,0x4C,0x45 ; 'NU! \0\0\x02\x02UNSTABLE'
	defb 0x20,0x52,0x41,0x4D,0x20,0x46,0x4F,0x55,0x4E,0x44,0x21,0x21,0x21,0x00,0x00,0x01 ; ' RAM FOUND!!!\0\0\x01'
	defb 0x08,0x28,0x50,0x52,0x45,0x53,0x53,0x20,0x53,0x20,0x54,0x4F,0x20,0x43,0x4F,0x4E ; '\x08(PRESS S TO CON'
	defb 0x54,0x49,0x4E,0x55,0x45,0x20,0x41,0x4E,0x59,0x57,0x41,0x59,0x2E,0x2E,0x2E,0x29 ; 'TINUE ANYWAY...)'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x7C ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0||'
	defb 0x44,0x10,0x38,0x10,0x00,0xFE,0x00,0xFE,0x00,0x38,0x1E,0x3E,0x92,0x80,0x02,0x10 ; 'D\x108\x10\0\xFE\0\xFE\08\x1E>\x92\x80\x02\x10'
	defb 0x6C,0x7E,0x3C,0x00,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x10,0xFE,0x00,0x00,0x00 ; 'l~<\0\x10\x10\x10\0\0\0\0\x10\xFE\0\0\0'
	defb 0x00,0x10,0x63,0x00,0x00,0x06,0x60,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00 ; '\0\x10c\0\0\x06`\0\0\0\0\0\x03\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x60,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\x06\0`\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1E,0xC0,0x78,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\x1E\xC0x\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1C,0x18,0x70,0x00,0x3C,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\x1C\x18p\0<\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\x0C\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x55,0x77 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0"Uw'
	defb 0x18,0x18,0x18,0x66,0x00,0x00,0x66,0x66,0x00,0x66,0x66,0x18,0x00,0x18,0x18,0x00 ; '\x18\x18\x18f\0\0ff\0ff\x18\0\x18\x18\0'
	defb 0x18,0x00,0x18,0x18,0x66,0x66,0x00,0x66,0x00,0x66,0x00,0x66,0x18,0x66,0x00,0x00 ; '\x18\0\x18\x18ff\0f\0f\0f\x18f\0\0'
	defb 0x66,0x18,0x00,0x00,0x66,0x18,0x18,0x00,0xFF,0x00,0xF0,0x0F,0xFF,0x00,0x00,0x00 ; 'f\x18\0\0f\x18\x18\0\xFF\0\xF0\x0F\xFF\0\0\0'
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x00,0x30 ; '\0\0\0\0\0\0\0\0\0\0\0\0\0$\00'
	defb 0x18,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x0E,0x8F,0x38,0x00,0xAA,0x00,0x82,0xFE ; '\x18\0\x18\0\0\0\0\0\x0E\x8F8\0\xAA\0\x82\xFE'
	defb 0xEE,0x38,0x38,0x38,0x00,0xFE,0x00,0xFE,0x0E,0x6C,0x12,0x22,0x7C,0xE0,0x0E,0x38 ; '\xEE888\0\xFE\0\xFE\x0El\x12"|\xE0\x0E8'
	defb 0x6C,0xB6,0x60,0x00,0x38,0x38,0x10,0x08,0x10,0x00,0x28,0x10,0x7C ; 'l\xB6`\088\x10\x08\x10\0(\x10|'

; --------------------------------------------------------------------------
; DATA - 8x220 glyph font (codes 00h-DBh) used for both text
; pages; Russian text is encoded in this custom code page.
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x7920-0x7FFF  FONT - 220 glyphs x 8 bytes
; --------------------------------------------------------------------------
FONT:
	defb 0x00,0x30,0x6C,0x24,0x7C,0x66,0x18,0x18 ; glyph 0x00
	defb 0x0C,0x30,0x56,0x18,0x00,0x00,0x00,0x06 ; glyph 0x08
	defb 0x3C,0x18,0x3C,0x3C,0x66,0x7E,0x3C,0x7E ; glyph 0x10
	defb 0x3C,0x3C,0x00,0x00,0x0C,0x00,0x30,0x3C ; glyph 0x18
	defb 0x3C,0x3C,0x7C,0x3C,0x7C,0x7E,0x7E,0x3C ; glyph 0x20
	defb 0x66,0x3C,0x06,0x66,0x60,0x82,0x46,0x3C ; glyph 0x28
	defb 0x7C,0x3C,0x7C,0x3C,0x7E,0x66,0x66,0xC6 ; glyph 0x30
	defb 0x66,0x66,0x7E,0x18,0x60,0x18,0x18,0x00 ; glyph 0x38
	defb 0x18,0x00,0x60,0x00,0x06,0x00,0x1C,0x00 ; glyph 0x40
	defb 0x60,0x18,0x0C,0x60,0x30,0x00,0x00,0x00 ; glyph 0x48
	defb 0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00 ; glyph 0x50
	defb 0x00,0x00,0x00,0x30,0x18,0x18,0x34,0x42 ; glyph 0x58
	defb 0x3C,0x7C,0x7C,0x7E,0x1E,0x7E,0xD6,0x3C ; glyph 0x60
	defb 0x62,0x6A,0x66,0x1E,0x82,0x66,0x3C,0x7E ; glyph 0x68
	defb 0x7C,0x3C,0x7E,0x66,0x7C,0x66,0x66,0x66 ; glyph 0x70
	defb 0xD6,0xD6,0xE0,0xC6,0x60,0x3C,0xCE,0x3E ; glyph 0x78
	defb 0x00,0x3C,0x00,0x00,0x3C,0x00,0x00,0x00 ; glyph 0x80
	defb 0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x88
	defb 0x88,0xAA,0xDD,0x18,0x18,0x18,0x66,0x00 ; glyph 0x90
	defb 0x00,0x66,0x66,0x00,0x66,0x66,0x18,0x00 ; glyph 0x98
	defb 0x18,0x18,0x00,0x18,0x00,0x18,0x18,0x66 ; glyph 0xA0
	defb 0x66,0x00,0x66,0x00,0x66,0x00,0x66,0x18 ; glyph 0xA8
	defb 0x66,0x00,0x00,0x66,0x18,0x00,0x00,0x66 ; glyph 0xB0
	defb 0x18,0x18,0x00,0xFF,0x00,0xF0,0x0F,0xFF ; glyph 0xB8
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0xC0
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0xC8
	defb 0x7E,0x24,0x18,0x30,0x0E,0x18,0x18,0x76 ; glyph 0xD0
	defb 0x3C,0x18,0x00,0x0E,0xCD,0x6C,0x00,0x54 ; glyph 0xD8
	defb 0x00,0xAA,0xD6,0xFE,0x7C,0xD6,0x7C,0x38 ; glyph 0xE0
	defb 0xC6,0x38,0xC6,0x06,0x44,0x1E,0x3E,0x6C ; glyph 0xE8
	defb 0xF8,0x3E,0x7C,0x6C,0xB6,0x3C,0x00,0x7C ; glyph 0xF0
	defb 0x7C,0x10,0x04,0x20,0x40,0x6C,0x38,0x7C ; glyph 0xF8
	defb 0x00,0x30,0x24,0x7E,0xD0,0x0C,0x34,0x08 ; glyph 0x100
	defb 0x18,0x18,0x3E,0x18,0x00,0x00,0x00,0x0C ; glyph 0x108
	defb 0x66,0x38,0x66,0x66,0x66,0x60,0x60,0x06 ; glyph 0x110
	defb 0x66,0x66,0x30,0x30,0x18,0x3C,0x18,0x66 ; glyph 0x118
	defb 0x66,0x66,0x66,0x66,0x66,0x60,0x60,0x66 ; glyph 0x120
	defb 0x66,0x18,0x06,0x6C,0x60,0xC6,0x66,0x66 ; glyph 0x128
	defb 0x66,0x66,0x66,0x60,0x18,0x66,0x66,0xC6 ; glyph 0x130
	defb 0x66,0x66,0x0C,0x18,0x30,0x18,0x3C,0x00 ; glyph 0x138
	defb 0x10,0x3C,0x60,0x3C,0x06,0x3C,0x36,0x3E ; glyph 0x140
	defb 0x60,0x00,0x00,0x66,0x30,0xFC,0x7C,0x3C ; glyph 0x148
	defb 0x7C,0x3E,0x3E,0x3C,0x78,0x66,0x66,0xC6 ; glyph 0x150
	defb 0x66,0x66,0x7E,0x30,0x18,0x18,0x58,0x99 ; glyph 0x158
	defb 0x66,0x60,0x66,0x60,0x36,0x60,0xD6,0x66 ; glyph 0x160
	defb 0x66,0x66,0x6C,0x36,0xC6,0x66,0x66,0x66 ; glyph 0x168
	defb 0x66,0x66,0x18,0x66,0xD6,0x66,0x66,0x66 ; glyph 0x170
	defb 0xD6,0xD6,0x60,0xC6,0x60,0x66,0xDB,0x66 ; glyph 0x178
	defb 0x3C,0x60,0x7C,0x3C,0x06,0x3C,0xD6,0x3C ; glyph 0x180
	defb 0x62,0x6A,0x66,0x1E,0x82,0x66,0x3C,0x7E ; glyph 0x188
	defb 0x22,0x55,0x77,0x18,0x18,0xF8,0x66,0x00 ; glyph 0x190
	defb 0xF8,0xE6,0x66,0xFE,0xE6,0x66,0xF8,0x00 ; glyph 0x198
	defb 0x18,0x18,0x00,0x18,0x00,0x18,0x1F,0x66 ; glyph 0x1A0
	defb 0x67,0x7F,0xE7,0xFF,0x67,0xFF,0xE7,0xFF ; glyph 0x1A8
	defb 0x66,0xFF,0x00,0x66,0x1F,0x1F,0x00,0x66 ; glyph 0x1B0
	defb 0xFF,0x18,0x00,0xFF,0x00,0xF0,0x0F,0xFF ; glyph 0x1B8
	defb 0x7C,0x3C,0x7E,0x66,0x7C,0x66,0x66,0x66 ; glyph 0x1C0
	defb 0xD6,0xD6,0xE0,0xC6,0x60,0x3C,0xCE,0x3E ; glyph 0x1C8
	defb 0x60,0x3C,0x0C,0x60,0x1B,0x18,0x00,0xDC ; glyph 0x1D0
	defb 0x66,0x3C,0x00,0x0C,0xEF,0x18,0x3C,0xAA ; glyph 0x1D8
	defb 0x00,0x82,0xFE,0xFE,0xFE,0xFE,0xFE,0x7C ; glyph 0x1E0
	defb 0x82,0x44,0xBA,0x7A,0x6C,0x10,0x22,0xC6 ; glyph 0x1E8
	defb 0xFE,0xFE,0x10,0x6C,0x76,0x66,0x00,0x10 ; glyph 0x1F0
	defb 0x10,0x10,0x7E,0x7E,0x40,0xFE,0x38,0x38 ; glyph 0x1F8
	defb 0x00,0x30,0x48,0x24,0x7C,0x18,0x18,0x10 ; glyph 0x200
	defb 0x18,0x18,0x7E,0x7E,0x00,0x7C,0x00,0x18 ; glyph 0x208
	defb 0x6E,0x18,0x06,0x0C,0x66,0x7C,0x7C,0x06 ; glyph 0x210
	defb 0x3C,0x66,0x30,0x30,0x30,0x00,0x0C,0x06 ; glyph 0x218
	defb 0x5A,0x66,0x7C,0x60,0x66,0x78,0x60,0x60 ; glyph 0x220
	defb 0x7E,0x18,0x06,0x78,0x60,0xEE,0x76,0x66 ; glyph 0x228
	defb 0x66,0x66,0x66,0x3C,0x18,0x66,0x66,0xC6 ; glyph 0x230
	defb 0x3C,0x66,0x18,0x18,0x18,0x18,0x66,0x00 ; glyph 0x238
	defb 0x08,0x06,0x7C,0x66,0x3E,0x66,0x30,0x66 ; glyph 0x240
	defb 0x7C,0x38,0x0C,0x6C,0x30,0xD6,0x66,0x66 ; glyph 0x248
	defb 0x66,0x66,0x60,0x60,0x30,0x66,0x66,0xC6 ; glyph 0x250
	defb 0x66,0x66,0x0C,0xE0,0x18,0x0E,0x00,0xA1 ; glyph 0x258
	defb 0x66,0x7C,0x7C,0x60,0x66,0x78,0x7C,0x0C ; glyph 0x260
	defb 0x6E,0x6E,0x78,0x66,0xEE,0x7E,0x66,0x66 ; glyph 0x268
	defb 0x66,0x60,0x18,0x66,0xD6,0x3C,0x66,0x66 ; glyph 0x270
	defb 0xD6,0xD6,0x7C,0xF6,0x7C,0x0E,0xFB,0x66 ; glyph 0x278
	defb 0x06,0x3C,0x66,0x06,0x3E,0x66,0x7C,0x66 ; glyph 0x280
	defb 0x66,0x66,0x6C,0x36,0xC6,0x66,0x66,0x66 ; glyph 0x288
	defb 0x88,0xAA,0xDD,0x18,0xF8,0x18,0xE6,0xFE ; glyph 0x290
	defb 0x18,0x06,0x66,0x06,0x06,0xFE,0x18,0xF8 ; glyph 0x298
	defb 0x1F,0xFF,0xFF,0x1F,0xFF,0xFF,0x18,0x67 ; glyph 0x2A0
	defb 0x60,0x60,0x00,0x00,0x60,0x00,0x00,0x00 ; glyph 0x2A8
	defb 0xFF,0x00,0xFF,0x7F,0x18,0x18,0x7F,0xFF ; glyph 0x2B0
	defb 0x00,0xF8,0x1F,0xFF,0x00,0xF0,0x0F,0xFF ; glyph 0x2B8
	defb 0x66,0x66,0x18,0x66,0xD6,0x66,0x66,0x66 ; glyph 0x2C0
	defb 0xD6,0xD6,0x60,0xC6,0x60,0x66,0xDB,0x66 ; glyph 0x2C8
	defb 0x78,0x66,0x18,0x30,0x18,0x18,0x7E,0x00 ; glyph 0x2D0
	defb 0x3C,0x18,0x00,0x0C,0xFC,0x30,0x3C,0x54 ; glyph 0x2D8
	defb 0x00,0x82,0xFE,0x7C,0x7C,0xD6,0x7C,0x7C ; glyph 0x2E0
	defb 0x82,0x44,0xBA,0xD8,0x38,0x10,0x22,0x6C ; glyph 0x2E8
	defb 0xF8,0x3E,0x10,0x6C,0x36,0x3C,0x7C,0x7C ; glyph 0x2F0
	defb 0x10,0x7C,0x04,0x20,0x40,0x6C,0x7C,0x38 ; glyph 0x2F8
	defb 0x00,0x30,0x00,0x24,0x16,0x30,0x2C,0x00 ; glyph 0x300
	defb 0x18,0x18,0x3E,0x18,0x00,0x00,0x00,0x30 ; glyph 0x308
	defb 0x76,0x18,0x3C,0x06,0x7E,0x06,0x66,0x0C ; glyph 0x310
	defb 0x66,0x3E,0x00,0x00,0x18,0x3C,0x18,0x1C ; glyph 0x318
	defb 0x5E,0x7E,0x66,0x60,0x66,0x60,0x7C,0x6E ; glyph 0x320
	defb 0x66,0x18,0x06,0x6C,0x60,0xD6,0x6E,0x66 ; glyph 0x328
	defb 0x7C,0x66,0x7C,0x06,0x18,0x66,0x66,0xD6 ; glyph 0x330
	defb 0x66,0x3C,0x30,0x18,0x0C,0x18,0x00,0x00 ; glyph 0x338
	defb 0x00,0x3E,0x66,0x60,0x66,0x7E,0x78,0x66 ; glyph 0x340
	defb 0x66,0x18,0x0C,0x78,0x30,0xD6,0x66,0x66 ; glyph 0x348
	defb 0x66,0x66,0x60,0x3C,0x30,0x66,0x66,0xD6 ; glyph 0x350
	defb 0x3C,0x66,0x18,0x30,0x18,0x18,0x00,0xA1 ; glyph 0x358
	defb 0x7E,0x66,0x66,0x60,0x66,0x60,0x7C,0x06 ; glyph 0x360
	defb 0x76,0x76,0x6C,0x66,0xD6,0x66,0x66,0x66 ; glyph 0x368
	defb 0x7C,0x60,0x18,0x3E,0xD6,0x66,0x66,0x3E ; glyph 0x370
	defb 0xD6,0xD6,0x66,0xDE,0x66,0x06,0xDB,0x3E ; glyph 0x378
	defb 0x3E,0x66,0x7C,0x3C,0x66,0x7E,0x7C,0x0C ; glyph 0x380
	defb 0x6E,0x6E,0x78,0x66,0xEE,0x7E,0x66,0x66 ; glyph 0x388
	defb 0x22,0x55,0x77,0x18,0x18,0xF8,0x66,0x66 ; glyph 0x390
	defb 0xF8,0xE6,0x66,0xE6,0xFE,0x00,0xF8,0x18 ; glyph 0x398
	defb 0x00,0x00,0x18,0x18,0x00,0x18,0x1F,0x66 ; glyph 0x3A0
	defb 0x7F,0x67,0xFF,0xE7,0x67,0xFF,0xE7,0xFF ; glyph 0x3A8
	defb 0x00,0xFF,0x66,0x00,0x1F,0x1F,0x66,0x66 ; glyph 0x3B0
	defb 0xFF,0x00,0x18,0xFF,0xFF,0xF0,0x0F,0x00 ; glyph 0x3B8
	defb 0x66,0x60,0x18,0x66,0xD6,0x3C,0x66,0x3E ; glyph 0x3C0
	defb 0xD6,0xD6,0x7C,0xF6,0x7C,0x0E,0xFB,0x3E ; glyph 0x3C8
	defb 0x60,0x7E,0x30,0x18,0x18,0x18,0x00,0x76 ; glyph 0x3D0
	defb 0x00,0x00,0x18,0x6C,0xDC,0x7C,0x3C,0xAA ; glyph 0x3D8
	defb 0x00,0xBA,0xC6,0x38,0x38,0x10,0x10,0x38 ; glyph 0x3E0
	defb 0xC6,0x38,0xC6,0x88,0x10,0x70,0x2E,0x7C ; glyph 0x3E8
	defb 0xE0,0x0E,0x7C,0x00,0x36,0x06,0x7C,0x38 ; glyph 0x3F0
	defb 0x10,0x38,0x08,0x10,0x7E,0x28,0x7C,0x10 ; glyph 0x3F8
	defb 0x00,0x00,0x00,0x7E,0x7C,0x66,0x66,0x00 ; glyph 0x400
	defb 0x0C,0x30,0x7E,0x18,0x00,0x00,0x30,0x60 ; glyph 0x408
	defb 0x66,0x18,0x60,0x66,0x06,0x66,0x66,0x18 ; glyph 0x410
	defb 0x66,0x06,0x30,0x30,0x0C,0x00,0x30,0x00 ; glyph 0x418
	defb 0x60,0x66,0x66,0x66,0x66,0x60,0x60,0x66 ; glyph 0x420
	defb 0x66,0x18,0x66,0x66,0x60,0xC6,0x66,0x66 ; glyph 0x428
	defb 0x60,0x6E,0x66,0x66,0x18,0x66,0x24,0xEE ; glyph 0x430
	defb 0x66,0x18,0x60,0x18,0x06,0x18,0x00,0x00 ; glyph 0x438
	defb 0x00,0x66,0x66,0x66,0x66,0x60,0x30,0x3E ; glyph 0x440
	defb 0x66,0x18,0x0C,0x6C,0x30,0xD6,0x66,0x66 ; glyph 0x448
	defb 0x7C,0x3E,0x60,0x06,0x30,0x66,0x24,0xEE ; glyph 0x450
	defb 0x66,0x3E,0x30,0x30,0x18,0x18,0x00,0x99 ; glyph 0x458
	defb 0x66,0x66,0x66,0x60,0x66,0x60,0xD6,0x66 ; glyph 0x460
	defb 0x66,0x66,0x66,0x66,0xC6,0x66,0x66,0x66 ; glyph 0x468
	defb 0x60,0x66,0x18,0x06,0x7C,0x66,0x66,0x06 ; glyph 0x470
	defb 0xD6,0xD6,0x66,0xDE,0x66,0x66,0xDB,0x66 ; glyph 0x478
	defb 0x66,0x66,0x66,0x60,0x66,0x60,0xD6,0x66 ; glyph 0x480
	defb 0x76,0x76,0x6C,0x66,0xD6,0x66,0x66,0x66 ; glyph 0x488
	defb 0x88,0xAA,0xDD,0x18,0x18,0x18,0x66,0x66 ; glyph 0x490
	defb 0x18,0x66,0x66,0x66,0x00,0x00,0x00,0x18 ; glyph 0x498
	defb 0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x66 ; glyph 0x4A0
	defb 0x00,0x66,0x00,0x66,0x66,0x00,0x66,0x00 ; glyph 0x4A8
	defb 0x00,0x18,0x66,0x00,0x00,0x18,0x66,0x66 ; glyph 0x4B0
	defb 0x18,0x00,0x18,0xFF,0xFF,0xF0,0x0F,0x00 ; glyph 0x4B8
	defb 0x7C,0x66,0x18,0x3E,0x7C,0x66,0x66,0x06 ; glyph 0x4C0
	defb 0xD6,0xD6,0x66,0xDE,0x66,0x66,0xDB,0x66 ; glyph 0x4C8
	defb 0x60,0x60,0x00,0x00,0x18,0xD8,0x18,0xDC ; glyph 0x4D0
	defb 0x00,0x00,0x00,0x3C,0xCC,0x00,0x3C,0x54 ; glyph 0x4D8
	defb 0x00,0x92,0xEE,0x10,0x10,0x38,0x38,0x00 ; glyph 0x4E0
	defb 0xFE,0x00,0xFE,0xD8,0x38,0xE0,0xE4,0x92 ; glyph 0x4E8
	defb 0x80,0x02,0x38,0x6C,0x36,0x3C,0x7C,0x10 ; glyph 0x4F0
	defb 0x10,0x10,0x00,0x00,0x00,0x00,0xFE,0x10 ; glyph 0x4F8
	defb 0x00,0x30,0x00,0x24,0x10,0xC6,0x3A,0x00 ; glyph 0x500
	defb 0x06,0x60,0x7E,0x00,0x18,0x00,0x30,0xC0 ; glyph 0x508
	defb 0x3C,0x18,0x7E,0x3C,0x06,0x3C,0x3C,0x18 ; glyph 0x510
	defb 0x3C,0x3C,0x30,0x10,0x06,0x00,0x60,0x18 ; glyph 0x518
	defb 0x3C,0x66,0x7C,0x3C,0x7C,0x7E,0x60,0x3E ; glyph 0x520
	defb 0x66,0x3C,0x3C,0x66,0x7E,0xC6,0x62,0x3C ; glyph 0x528
	defb 0x60,0x3C,0x66,0x3C,0x18,0x3C,0x18,0x44 ; glyph 0x530
	defb 0x66,0x18,0x7E,0x1E,0x03,0x78,0x00,0x00 ; glyph 0x538
	defb 0x00,0x3E,0x7C,0x3C,0x3E,0x3C,0x30,0x06 ; glyph 0x540
	defb 0x66,0x3C,0x0C,0x66,0x1C,0xD6,0x66,0x3C ; glyph 0x548
	defb 0x60,0x06,0x60,0x7C,0x1C,0x3E,0x18,0x44 ; glyph 0x550
	defb 0x66,0x06,0x7E,0x1C,0x18,0x70,0x00,0x42 ; glyph 0x558
	defb 0x66,0x7C,0x7C,0x60,0xFF,0x7E,0xD6,0x3C ; glyph 0x560
	defb 0x46,0x46,0x66,0x66,0xC6,0x66,0x3C,0x66 ; glyph 0x568
	defb 0x60,0x3C,0x18,0x3C,0x10,0x66,0x7F,0x06 ; glyph 0x570
	defb 0xFE,0xFF,0x7C,0xF6,0x7C,0x3C,0xCE,0x66 ; glyph 0x578
	defb 0x3E,0x3C,0x7C,0x3E,0x3C,0x3C,0xD6,0x3C ; glyph 0x580
	defb 0x66,0x66,0x66,0x66,0xC6,0x66,0x3C,0x66 ; glyph 0x588
	defb 0x22,0x55,0x77,0x18,0x18,0x18,0x66,0x66 ; glyph 0x590
	defb 0x18,0x66,0x66,0x66,0x00,0x00,0x00,0x18 ; glyph 0x598
	defb 0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x66 ; glyph 0x5A0
	defb 0x00,0x66,0x00,0x66,0x66,0x00,0x66,0x00 ; glyph 0x5A8
	defb 0x00,0x18,0x66,0x00,0x00,0x18,0x66,0x66 ; glyph 0x5B0
	defb 0x18,0x00,0x18,0xFF,0xFF,0xF0,0x0F,0x00 ; glyph 0x5B8
	defb 0x60,0x3C,0x18,0x06,0x10,0x66,0x7F,0x06 ; glyph 0x5C0
	defb 0xFE,0xFF,0x7C,0xF6,0x7C,0x3C,0xCE,0x66 ; glyph 0x5C8
	defb 0x7E,0x3C,0x7C,0x7C,0x18,0x70,0x00,0x00 ; glyph 0x5D0
	defb 0x00,0x00,0x00,0x1C,0xCC,0x00,0x00,0xAA ; glyph 0x5D8
	defb 0x00,0x7C,0x7C,0x00,0x00,0x00,0x00,0x00 ; glyph 0x5E0
	defb 0xFE,0x00,0xFE,0x70,0x10,0x40,0x40,0x00 ; glyph 0x5E8
	defb 0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x7C ; glyph 0x5F0
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x5F8
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x600
	defb 0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00 ; glyph 0x608
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x610
	defb 0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00 ; glyph 0x618
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x620
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x628
	defb 0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x630
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF ; glyph 0x638
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3C ; glyph 0x640
	defb 0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00 ; glyph 0x648
	defb 0x60,0x06,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x650
	defb 0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x3C ; glyph 0x658
	defb 0x00,0x00,0x00,0x00,0xC3,0x00,0x00,0x00 ; glyph 0x660
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x668
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00 ; glyph 0x670
	defb 0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x678
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x680
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x688
	defb 0x88,0xAA,0xDD,0x18,0x18,0x18,0x66,0x66 ; glyph 0x690
	defb 0x18,0x66,0x66,0x66,0x00,0x00,0x00,0x18 ; glyph 0x698
	defb 0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x66 ; glyph 0x6A0
	defb 0x00,0x66,0x00,0x66,0x66,0x00,0x66,0x00 ; glyph 0x6A8
	defb 0x00,0x18,0x66,0x00,0x00,0x18,0x66,0x66 ; glyph 0x6B0
	defb 0x18,0x00,0x18,0xFF,0xFF,0xF0,0x0F,0x00 ; glyph 0x6B8
	defb 0x60,0x00,0x00,0x3C,0x10,0x00,0x01,0x00 ; glyph 0x6C0
	defb 0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x6C8
	defb 0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00 ; glyph 0x6D0
	defb 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 ; glyph 0x6D8

; --------------------------------------------------------------------------
; DATA - English help text page
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0x8000-0x84F9  English help text page
; --------------------------------------------------------------------------
TEXT_EN:
	defb 0x06,0x01,0x31,0x01,0x08,0x2E,0x07,0x07,0x45,0x4E,0x47,0x4C,0x49,0x53,0x48,0x00 ; '\x06\x011\x01\x08.\x07\x07ENGLISH\0'
	defb 0x00,0x06,0x01,0x32,0x01,0x08,0x2E,0x07,0x07,0x90,0x93,0x91,0x91,0x8A,0x88,0x89 ; '\0\x06\x012\x01\x08.\x07\x07\x90\x93\x91\x91\x8A\x88\x89'
	defb 0x00,0x00,0x07,0x07,0x28,0x43,0x29,0x04,0x04,0x42,0x55,0x44,0x44,0x45,0x52,0x2F ; '\0\0\x07\x07(C)\x04\x04BUDDER/'
	defb 0x4D,0x4E,0x47,0x20,0x02,0x02,0x32,0x30,0x31,0x35,0x00,0x00,0x07,0x07,0x55,0x6E ; 'MNG \x02\x022015\0\0\x07\x07Un'
	defb 0x69,0x76,0x65,0x72,0x73,0x61,0x6C,0x20,0x4D,0x65,0x6D,0x6F,0x72,0x79,0x20,0x54 ; 'iversal Memory T'
	defb 0x65,0x73,0x74,0x65,0x72,0x06,0x06,0x20,0x76,0x32,0x2E,0x33,0x78,0x00,0x00,0x07 ; 'ester\x06\x06 v2.3x\0\0\x07'
	defb 0x07,0x56,0x69,0x73,0x69,0x74,0x3A,0x01,0x01,0x66,0x6F,0x72,0x75,0x6D,0x2E,0x74 ; '\x07Visit:\x01\x01forum.t'
	defb 0x73,0x6C,0x61,0x62,0x73,0x2E,0x69,0x6E,0x66,0x6F,0x00,0x00,0x07,0x07,0x20,0x20 ; 'slabs.info\0\0\x07\x07  '
	defb 0x20,0x20,0x20,0x20,0x20,0x8E,0x20,0xAF,0xE0,0xAE,0xA3,0xE0,0xA0,0xAC,0xAC,0xA5 ; '     \x8E \xAF\xE0\xAE\xA3\xE0\xA0\xAC\xAC\xA5'
	defb 0x20,0xFE,0xDA,0x70,0x0D,0x0D,0x01,0x08,0x20,0xAA,0xAE,0xA4,0x3A,0x20,0x42,0x55 ; ' \xFE\xDAp\r\r\x01\x08 \xAA\xAE\xA4: BU'
	defb 0x44,0x44,0x45,0x52,0x2F,0x4D,0x47,0x4E,0x0D,0x01,0x08,0xE4,0xAE,0xAD,0xE2,0x3A ; 'DDER/MGN\r\x01\x08\xE4\xAE\xAD\xE2:'
	defb 0x20,0x57,0x49,0x5A,0x41,0x52,0x44,0x2F,0x44,0x54,0x0D,0x0D,0x0D,0x06,0x06,0x93 ; ' WIZARD/DT\r\r\r\x06\x06\x93'
	defb 0xAF,0xE0,0xA0,0xA2,0xAB,0xA5,0xAD,0xA8,0xA5,0x3A,0x0D,0x01,0x06,0x2D,0x2D,0x2D ; '\xAF\xE0\xA0\xA2\xAB\xA5\xAD\xA8\xA5:\r\x01\x06---'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; '----------------'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x0D,0x01,0x07 ; '-------------\r\x01\x07'
	defb 0x20,0x20,0x20,0x30,0x20,0x01,0x08,0x2D,0x20,0x82,0xEB,0xA1,0xAE,0xE0,0x20,0xE2 ; '   0 \x01\x08- \x82\xEB\xA1\xAE\xE0 \xE2'
	defb 0xA8,0xAF,0xA0,0x20,0xE2,0xA5,0xE1,0xE2,0xA0,0x0D,0x01,0x07,0x20,0x20,0x20,0x4D ; '\xA8\xAF\xA0 \xE2\xA5\xE1\xE2\xA0\r\x01\x07   M'
	defb 0x20,0x01,0x08,0x2D,0x20,0x82,0xEB,0xA1,0xAE,0xE0,0x20,0xAE,0xA1,0xEA,0xA5,0xAC ; ' \x01\x08- \x82\xEB\xA1\xAE\xE0 \xAE\xA1\xEA\xA5\xAC'
	defb 0xA0,0x20,0xAF,0xA0,0xAC,0xEF,0xE2,0xA8,0x0D,0x0D,0x01,0x07,0x31,0x2D,0x39,0x2C ; '\xA0 \xAF\xA0\xAC\xEF\xE2\xA8\r\r\x01\x071-9,'
	defb 0x58,0x20,0x01,0x08,0x2D,0x20,0x87,0xA0,0xAF,0xE3,0xE1,0xE2,0xA8,0xE2,0xEC,0x20 ; 'X \x01\x08- \x87\xA0\xAF\xE3\xE1\xE2\xA8\xE2\xEC '
	defb 0xA2,0xEB,0xA1,0xE0,0xA0,0xAD,0xAD,0xEB,0xA9,0x20,0xE2,0xA5,0xE1,0xE2,0x0D,0x0D ; '\xA2\xEB\xA1\xE0\xA0\xAD\xAD\xEB\xA9 \xE2\xA5\xE1\xE2\r\r'
	defb 0x01,0x07,0x20,0x20,0x20,0x48,0x20,0x01,0x08,0x2D,0x20,0x82,0xEB,0xA2,0xAE,0xA4 ; '\x01\x07   H \x01\x08- \x82\xEB\xA2\xAE\xA4'
	defb 0x20,0xED,0xE2,0xAE,0xA9,0x20,0xE1,0xE2,0xE0,0xA0,0xAD,0xA8,0xE6,0xEB,0x0D,0x0D ; ' \xED\xE2\xAE\xA9 \xE1\xE2\xE0\xA0\xAD\xA8\xE6\xEB\r\r'
	defb 0x01,0x07,0x20,0x20,0x20,0x45,0x20,0x01,0x08,0x2D,0x20,0xA2,0xEB,0xE5,0xAE,0xA4 ; '\x01\x07   E \x01\x08- \xA2\xEB\xE5\xAE\xA4'
	defb 0x20,0xA2,0x20,0x42,0x41,0x53,0x49,0x43,0x0D,0x01,0x06,0x2D,0x2D,0x2D,0x2D,0x2D ; ' \xA2 BASIC\r\x01\x06-----'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D ; '----------------'
	defb 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x0D,0x0D,0x0D,0x02,0x02 ; '-----------\r\r\r\x02\x02'
	defb 0x84,0xAB,0xEF,0x20,0xAF,0xAE,0xAB,0xAD,0xAE,0xA9,0x20,0xAF,0xE0,0xAE,0xA2,0xA5 ; '\x84\xAB\xEF \xAF\xAE\xAB\xAD\xAE\xA9 \xAF\xE0\xAE\xA2\xA5'
	defb 0xE0,0xAA,0xA8,0x20,0x8E,0x87,0x93,0x3A,0x0D,0x07,0x07,0x88,0xE1,0xAF,0xAE,0xAB ; '\xE0\xAA\xA8 \x8E\x87\x93:\r\x07\x07\x88\xE1\xAF\xAE\xAB'
	defb 0xEC,0xA7,0xE3,0xA9,0xE2,0xA5,0x20,0xA2,0xE1,0xA5,0x20,0x34,0x20,0xE2,0xA5,0xE1 ; '\xEC\xA7\xE3\xA9\xE2\xA5 \xA2\xE1\xA5 4 \xE2\xA5\xE1'
	defb 0xE2,0xA0,0x21,0x20,0x28,0xAA,0xAB,0x2E,0x20,0x30,0x29,0x0D,0x0D,0x0D,0x07,0x07 ; '\xE2\xA0! (\xAA\xAB. 0)\r\r\r\x07\x07'
	defb 0x20,0x8B,0x9E,0x81,0x80,0x9F,0x20,0x8A,0x8B,0x80,0x82,0x88,0x98,0x80,0x20,0x2D ; ' \x8B\x9E\x81\x80\x9F \x8A\x8B\x80\x82\x88\x98\x80 -'
	defb 0x20,0x82,0x8E,0x87,0x82,0x90,0x80,0x92,0x20,0x82,0x20,0x8C,0x85,0x8D,0x9E,0x21 ; ' \x82\x8E\x87\x82\x90\x80\x92 \x82 \x8C\x85\x8D\x9E!'
	defb 0x00,0x00,0x04,0x04,0x48,0x01,0x04,0x2D,0x03,0x03,0x8F,0x8E,0x8A,0x80,0x87,0x80 ; '\0\0\x04\x04H\x01\x04-\x03\x03\x8F\x8E\x8A\x80\x87\x80'
	defb 0x92,0x9C,0x20,0x8E,0x8F,0x88,0x91,0x80,0x8D,0x88,0x85,0x2E,0x2E,0x2E,0x00,0x00 ; '\x92\x9C \x8E\x8F\x88\x91\x80\x8D\x88\x85...\0\0'
	defb 0x02,0x02,0x82,0x8D,0x88,0x8C,0x80,0x8D,0x88,0x85,0x3A,0x0D,0x07,0x07,0x84,0x80 ; '\x02\x02\x82\x8D\x88\x8C\x80\x8D\x88\x85:\r\x07\x07\x84\x80'
	defb 0x8D,0x8D,0x9B,0x89,0x20,0x92,0x85,0x91,0x92,0x20,0x8E,0x97,0x88,0x91,0x92,0x88 ; '\x8D\x8D\x9B\x89 \x92\x85\x91\x92 \x8E\x97\x88\x91\x92\x88'
	defb 0x92,0x20,0x82,0x91,0xF0,0x20,0x8E,0x87,0x93,0x21,0x00,0x00,0x04,0x04,0x30,0x01 ; '\x92 \x82\x91\xF0 \x8E\x87\x93!\0\0\x04\x040\x01'
	defb 0x08,0x2D,0x05,0x05,0x92,0x88,0x8F,0x20,0x92,0x85,0x91,0x92,0x80,0x3A,0xFE,0xBC ; '\x08-\x05\x05\x92\x88\x8F \x92\x85\x91\x92\x80:\xFE\xBC'
	defb 0x6E,0x00,0x00,0x04,0x04,0x4D,0x01,0x08,0x2D,0x05,0x05,0x92,0x88,0x8F,0x20,0x8F ; 'n\0\0\x04\x04M\x01\x08-\x05\x05\x92\x88\x8F \x8F'
	defb 0x80,0x8C,0x9F,0x92,0x88,0x3A,0xFE,0x08,0x6F,0x00,0x00,0x07,0x07,0x8B,0x85,0x83 ; '\x80\x8C\x9F\x92\x88:\xFE\x08o\0\0\x07\x07\x8B\x85\x83'
	defb 0x85,0x8D,0x84,0x80,0x3A,0x0D,0x06,0x06,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAF,0xE0 ; '\x85\x8D\x84\x80:\r\x06\x06*\x01\x07 - \xAF\xE0'
	defb 0xAE,0xAF,0xE3,0xE9,0xA5,0xAD,0xAD,0xA0,0xEF,0x20,0xE1,0xE2,0xE0,0xA0,0xAD,0xA8 ; '\xAE\xAF\xE3\xE9\xA5\xAD\xAD\xA0\xEF \xE1\xE2\xE0\xA0\xAD\xA8'
	defb 0xE6,0xA0,0x0D,0x04,0x04,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAD,0xA5,0xE2,0x20,0xAE ; '\xE6\xA0\r\x04\x04*\x01\x07 - \xAD\xA5\xE2 \xAE'
	defb 0xE8,0xA8,0xA1,0xAE,0xAA,0x0D,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAE,0xE8 ; '\xE8\xA8\xA1\xAE\xAA\r\x02\x02*\x01\x07 - \xAE\xE8'
	defb 0xA8,0xA1,0xAA,0xA0,0x20,0x28,0x23,0x30,0x30,0x2F,0x35,0x35,0x29,0x0D,0x03,0x03 ; '\xA8\xA1\xAA\xA0 (#00/55)\r\x03\x03'
	defb 0x2A,0x01,0x07,0x20,0x2D,0x20,0xAE,0xE8,0xA8,0xA1,0xAA,0xA0,0x20,0x28,0x23,0x46 ; '*\x01\x07 - \xAE\xE8\xA8\xA1\xAA\xA0 (#F'
	defb 0x46,0x2F,0x41,0x41,0x29,0x0D,0x0D,0x01,0x08,0x80,0xAB,0xA3,0xAE,0xE0,0xA8,0xE2 ; 'F/AA)\r\r\x01\x08\x80\xAB\xA3\xAE\xE0\xA8\xE2'
	defb 0xAC,0x3A,0x20,0xFE,0xBC,0x6E,0x00,0x00,0x07,0x07,0x8B,0x85,0x83,0x85,0x8D,0x84 ; '\xAC: \xFE\xBCn\0\0\x07\x07\x8B\x85\x83\x85\x8D\x84'
	defb 0x80,0x3A,0x0D,0x06,0x06,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAF,0xE0,0xAE,0xAF,0xE3 ; '\x80:\r\x06\x06*\x01\x07 - \xAF\xE0\xAE\xAF\xE3'
	defb 0xE9,0xA5,0xAD,0xAE,0x0D,0x05,0x05,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAD,0xA5,0x20 ; '\xE9\xA5\xAD\xAE\r\x05\x05*\x01\x07 - \xAD\xA5 '
	defb 0xAF,0xE0,0xAE,0xA2,0xA5,0xE0,0xA5,0xAD,0xAE,0x0D,0x04,0x04,0x2A,0x01,0x07,0x20 ; '\xAF\xE0\xAE\xA2\xA5\xE0\xA5\xAD\xAE\r\x04\x04*\x01\x07 '
	defb 0x2D,0x20,0xAF,0xE0,0xAE,0xA2,0xA5,0xE0,0xA5,0xAD,0xAE,0x20,0x28,0xAD,0xA5,0xE2 ; '- \xAF\xE0\xAE\xA2\xA5\xE0\xA5\xAD\xAE (\xAD\xA5\xE2'
	defb 0x20,0xAE,0xE8,0xA8,0xA1,0xAE,0xAA,0x29,0x0D,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D ; ' \xAE\xE8\xA8\xA1\xAE\xAA)\r\x02\x02*\x01\x07 -'
	defb 0x20,0xA5,0xE1,0xE2,0xEC,0x20,0xAE,0xE8,0xA8,0xA1,0xAA,0xA8,0x0D,0x0D,0x01,0x08 ; ' \xA5\xE1\xE2\xEC \xAE\xE8\xA8\xA1\xAA\xA8\r\r\x01\x08'
	defb 0x80,0xAB,0xA3,0xAE,0xE0,0xA8,0xE2,0xAC,0x3A,0x20,0xFE,0xBC,0x6E,0x00,0x00,0x01 ; '\x80\xAB\xA3\xAE\xE0\xA8\xE2\xAC: \xFE\xBCn\0\0\x01'
	defb 0x01,0x2A,0x01,0x07,0x2F,0x06,0x06,0x2A,0x01,0x07,0x2F,0x05,0x05,0x2A,0x01,0x07 ; '\x01*\x01\x07/\x06\x06*\x01\x07/\x05\x05*\x01\x07'
	defb 0x20,0x2D,0x20,0xA3,0xA5,0xAD,0xA5,0xE0,0x2E,0x2F,0xAF,0xE0,0xAE,0xAF,0xE3,0xE9 ; ' - \xA3\xA5\xAD\xA5\xE0./\xAF\xE0\xAE\xAF\xE3\xE9'
	defb 0x2E,0x2F,0xA8,0xA7,0xAC,0xA5,0xAD,0xA5,0xAD,0xA8,0xA5,0x0D,0x20,0x04,0x04,0x2A ; './\xA8\xA7\xAC\xA5\xAD\xA5\xAD\xA8\xA5\r \x04\x04*'
	defb 0x01,0x07,0x2F,0x02,0x02,0x2A,0x01,0x07,0x20,0x2D,0x20,0xAD,0xA5,0xE2,0x20,0xAE ; '\x01\x07/\x02\x02*\x01\x07 - \xAD\xA5\xE2 \xAE'
	defb 0xE8,0xA8,0xA1,0xAE,0xAA,0x2F,0xA5,0xE1,0xE2,0xEC,0x20,0xAE,0xE8,0xA8,0xA1,0xAA ; '\xE8\xA8\xA1\xAE\xAA/\xA5\xE1\xE2\xEC \xAE\xE8\xA8\xA1\xAA'
	defb 0xA8,0x00,0x00,0x07,0x07,0x52,0x45,0x53,0x00,0x00,0x07,0x07,0x92,0xA5,0xE1,0xE2 ; '\xA8\0\0\x07\x07RES\0\0\x07\x07\x92\xA5\xE1\xE2'
	defb 0xA8,0xE0,0xAE,0xA2,0xA0,0xAD,0xA8,0xA5,0x20,0xA2,0x20,0xAF,0xE0,0xAE,0xE6,0xA5 ; '\xA8\xE0\xAE\xA2\xA0\xAD\xA8\xA5 \xA2 \xAF\xE0\xAE\xE6\xA5'
	defb 0xE1,0xE1,0xA5,0x21,0x21,0x21,0x00,0x00,0x06,0x06,0xAF,0xA0,0xAC,0xEF,0xE2,0xEC ; '\xE1\xE1\xA5!!!\0\0\x06\x06\xAF\xA0\xAC\xEF\xE2\xEC'
	defb 0x3A,0xFE,0x2C,0x6F,0x00,0x00,0x02,0x02,0x20,0x20,0x20,0xAE,0xE8,0xA8,0xA1,0xAA ; ':\xFE,o\0\0\x02\x02   \xAE\xE8\xA8\xA1\xAA'
	defb 0xA0,0x20,0xAD,0xE3,0xAC,0xA5,0xE0,0xA0,0xE6,0xA8,0xA8,0x21,0x0D,0x07,0x07,0xAD ; '\xA0 \xAD\xE3\xAC\xA5\xE0\xA0\xE6\xA8\xA8!\r\x07\x07\xAD'
	defb 0xA0,0xE7,0xA8,0xAD,0xA0,0xEF,0x20,0xE1,0xAE,0x20,0xE1,0xE2,0xE0,0xA0,0xAD,0xA8 ; '\xA0\xE7\xA8\xAD\xA0\xEF \xE1\xAE \xE1\xE2\xE0\xA0\xAD\xA8'
	defb 0xE6,0xEB,0x20,0x23,0xFE,0xD5,0x70,0x07,0x07,0x21,0x21,0x00,0x00,0x07,0x07,0x85 ; '\xE6\xEB #\xFE\xD5p\x07\x07!!\0\0\x07\x07\x85'
	defb 0xE1,0xAB,0xA8,0x20,0xE3,0x20,0xA2,0xA0,0xE1,0x20,0xAD,0xA5,0x20,0x50,0x45,0x4E ; '\xE1\xAB\xA8 \xE3 \xA2\xA0\xE1 \xAD\xA5 PEN'
	defb 0x54,0x41,0x47,0x4F,0x4E,0x31,0x30,0x32,0x34,0x2C,0x20,0xE2,0xAE,0x0D,0x07,0x07 ; 'TAGON1024, \xE2\xAE\r\x07\x07'
	defb 0xAD,0xA0,0xA6,0xAC,0xA8,0xE2,0xA5,0x20,0x02,0x02,0x52,0x45,0x53,0x45,0x54,0x07 ; '\xAD\xA0\xA6\xAC\xA8\xE2\xA5 \x02\x02RESET\x07'
	defb 0x07,0x21,0x21,0x21,0x0D,0x06,0x06,0x28,0xA2,0xA5,0xE0,0xE5,0xAD,0xEF,0xEF,0x20 ; '\x07!!!\r\x06\x06(\xA2\xA5\xE0\xE5\xAD\xEF\xEF '
	defb 0xAF,0xA0,0xAC,0xEF,0xE2,0xEC,0x20,0xA7,0xA0,0xA1,0xAB,0xAE,0xAA,0xA8,0xE0,0xAE ; '\xAF\xA0\xAC\xEF\xE2\xEC \xA7\xA0\xA1\xAB\xAE\xAA\xA8\xE0\xAE'
	defb 0xA2,0xA0,0xAD,0xA0,0x21,0x29,0x00,0x00,0x20,0x8B,0x9E,0x81,0x80,0x9F,0x20,0x8A ; '\xA2\xA0\xAD\xA0!)\0\0 \x8B\x9E\x81\x80\x9F \x8A'
	defb 0x8B,0x80,0x82,0x88,0x98,0x80,0x20,0x2D,0x20,0x82,0x8E,0x87,0x82,0x90,0x80,0x92 ; '\x8B\x80\x82\x88\x98\x80 - \x82\x8E\x87\x82\x90\x80\x92'
	defb 0x20,0x82,0x20,0x8C,0x85,0x8D,0x9E,0x21,0x00,0x00,0x02,0x02,0xAE,0xA1,0xAD,0xA0 ; ' \x82 \x8C\x85\x8D\x9E!\0\0\x02\x02\xAE\xA1\xAD\xA0'
	defb 0xE0,0xE3,0xA6,0xA5,0xAD,0xEB,0x20,0xAE,0xE8,0xA8,0xA1,0xAA,0xA8,0x20,0xA2,0x20 ; '\xE0\xE3\xA6\xA5\xAD\xEB \xAE\xE8\xA8\xA1\xAA\xA8 \xA2 '
	defb 0x8E,0x87,0x93,0x21,0x21,0x21,0x00,0x00,0x01,0x08,0x20,0x28,0x53,0x20,0x2D,0x20 ; '\x8E\x87\x93!!!\0\0\x01\x08 (S - '
	defb 0xA2,0xE1,0xA5,0x20,0xE0,0xA0,0xA2,0xAD,0xAE,0x20,0xAF,0xE0,0xAE,0xA4,0xAE,0xAB ; '\xA2\xE1\xA5 \xE0\xA0\xA2\xAD\xAE \xAF\xE0\xAE\xA4\xAE\xAB'
	defb 0xA6,0xA8,0xE2,0xEC,0x2E,0x2E,0x2E,0x29,0x00,0x00 ; '\xA6\xA8\xE2\xEC...)\0\0'

; --------------------------------------------------------------------------
; DATA 0x84FA-0xBEFF  unused RAM (zero in the reference capture)
; --------------------------------------------------------------------------
; 14854 bytes of zero fill omitted (0x84FA-0xBEFF)

; --------------------------------------------------------------------------
; Resident MegaLZ depacker (stays here after unpacking). LDDR
; self-copy rewind, then SP-based bit reader: ADD HL,HL with
; DJNZ/POP refill; el0 is an UNCONDITIONAL literal; carry=1 ->
; literal; matches copy from DE + 0FF00h|A. End marker 0Fh runs
; the LD SP,IX pop epilogue writing the final 6 bytes from
; END_TABLE, then LD HL,2758h; EXX; LD SP,6000h; DI; JP ENTRY.
; --------------------------------------------------------------------------
DEPACK_ENTRY:
	lddr			;bf00	ed b8		. .
	inc de			;bf02	13		.
	ex de,hl		;bf03	eb		.
	ld sp,hl		;bf04	f9		.
	ld de,ENTRY		;bf05	11 00 60	. . `
	exx			;bf08	d9		.
	ld d,0bfh		;bf09	16 bf		. .
	ld bc,01010h		;bf0b	01 10 10	. . .
	pop hl			;bf0e	e1		.
lbf0fh:
	dec sp			;bf0f	3b		;
	pop af			;bf10	f1		.
	exx			;bf11	d9		.
lbf12h:
	ld (de),a		;bf12	12		.
	inc de			;bf13	13		.
lbf14h:
	exx			;bf14	d9		.
lbf15h:
	add hl,hl		;bf15	29		)
	djnz lbf1ah		;bf16	10 02		. .
	pop hl			;bf18	e1		.
	ld b,c			;bf19	41		A
lbf1ah:
	jr c,lbf0fh		;bf1a	38 f3		8 .
	ld e,001h		;bf1c	1e 01		. .
lbf1eh:
	ld a,080h		;bf1e	3e 80		> .
lbf20h:
	add hl,hl		;bf20	29		)
	djnz lbf25h		;bf21	10 02		. .
	pop hl			;bf23	e1		.
	ld b,c			;bf24	41		A
lbf25h:
	rla			;bf25	17		.
	jr c,lbf20h		;bf26	38 f8		8 .
	cp 003h			;bf28	fe 03		. .
	jr c,lbf31h		;bf2a	38 05		8 .
	add a,e			;bf2c	83		.
	ld e,a			;bf2d	5f		_
	xor c			;bf2e	a9		.
	jr nz,lbf1eh		;bf2f	20 ed		  .
lbf31h:
	add a,e			;bf31	83		.
	cp 004h			;bf32	fe 04		. .
	jr z,lbf90h		;bf34	28 5a		( Z
	adc a,0ffh		;bf36	ce ff		. .
	cp 002h			;bf38	fe 02		. .
	exx			;bf3a	d9		.
lbf3bh:
	ld c,a			;bf3b	4f		O
lbf3ch:
	exx			;bf3c	d9		.
	ld a,0bfh		;bf3d	3e bf		> .
	jr c,lbf55h		;bf3f	38 14		8 .
lbf41h:
	add hl,hl		;bf41	29		)
	djnz lbf46h		;bf42	10 02		. .
	pop hl			;bf44	e1		.
	ld b,c			;bf45	41		A
lbf46h:
	rla			;bf46	17		.
	jr c,lbf41h		;bf47	38 f8		8 .
	jr z,lbf50h		;bf49	28 05		( .
	inc a			;bf4b	3c		<
	add a,d			;bf4c	82		.
	jr nc,lbf57h		;bf4d	30 08		0 .
	sub d			;bf4f	92		.
lbf50h:
	inc a			;bf50	3c		<
	jr nz,lbf5fh		;bf51	20 0c		  .
	ld a,0efh		;bf53	3e ef		> .
lbf55h:
	rrca			;bf55	0f		.
	cp a			;bf56	bf		.
lbf57h:
	add hl,hl		;bf57	29		)
	djnz lbf5ch		;bf58	10 02		. .
	pop hl			;bf5a	e1		.
	ld b,c			;bf5b	41		A
lbf5ch:
	rla			;bf5c	17		.
	jr c,lbf57h		;bf5d	38 f8		8 .
lbf5fh:
	exx			;bf5f	d9		.
	ld h,0ffh		;bf60	26 ff		& .
	jr z,lbf6ah		;bf62	28 06		( .
	ld h,a			;bf64	67		g
	dec sp			;bf65	3b		;
	inc a			;bf66	3c		<
	jr z,lbf75h		;bf67	28 0c		( .
	pop af			;bf69	f1		.
lbf6ah:
	ld l,a			;bf6a	6f		o
	add hl,de		;bf6b	19		.
	ldir			;bf6c	ed b0		. .
lbf6eh:
	jr lbf14h		;bf6e	18 a4		. .
lbf70h:
	exx			;bf70	d9		.
	rrc d			;bf71	cb 0a		. .
	jr lbf15h		;bf73	18 a0		. .
lbf75h:
	pop af			;bf75	f1		.
	cp 0e0h			;bf76	fe e0		. .
	jr c,lbf6ah		;bf78	38 f0		8 .
	rlca			;bf7a	07		.
	xor c			;bf7b	a9		.
	inc a			;bf7c	3c		<
	jr z,lbf70h		;bf7d	28 f1		( .
	sub 010h		;bf7f	d6 10		. .
lbf81h:
	ld l,a			;bf81	6f		o
	ld c,a			;bf82	4f		O
	ld h,0ffh		;bf83	26 ff		& .
	add hl,de		;bf85	19		.
	ldi			;bf86	ed a0		. .
	dec sp			;bf88	3b		;
	pop af			;bf89	f1		.
	ld (de),a		;bf8a	12		.
	inc hl			;bf8b	23		#
	inc de			;bf8c	13		.
	ld a,(hl)		;bf8d	7e		~
	jr lbf12h		;bf8e	18 82		. .
lbf90h:
	ld a,080h		;bf90	3e 80		> .
lbf92h:
	add hl,hl		;bf92	29		)
	djnz lbf97h		;bf93	10 02		. .
	pop hl			;bf95	e1		.
	ld b,c			;bf96	41		A
lbf97h:
	adc a,a			;bf97	8f		.
	jr nz,lbfb3h		;bf98	20 19		  .
	jr c,lbf92h		;bf9a	38 f6		8 .
	ld a,0fch		;bf9c	3e fc		> .
	jr lbfb6h		;bf9e	18 16		. .
lbfa0h:
	dec sp			;bfa0	3b		;
	pop bc			;bfa1	c1		.
	ld c,b			;bfa2	48		H
	ld b,a			;bfa3	47		G
	ccf			;bfa4	3f		?
	jr lbf3ch		;bfa5	18 95		. .
lbfa7h:
	cp 00fh			;bfa7	fe 0f		. .
	jr c,lbfa0h		;bfa9	38 f5		8 .
	jr nz,lbf3bh		;bfab	20 8e		  .
	add a,0f4h		;bfad	c6 f4		. .
	ld sp,ix		;bfaf	dd f9		. .
	jr lbfc7h		;bfb1	18 14		. .
lbfb3h:
	sbc a,a			;bfb3	9f		.
	ld a,0efh		;bfb4	3e ef		> .
lbfb6h:
	add hl,hl		;bfb6	29		)
	djnz lbfbbh		;bfb7	10 02		. .
	pop hl			;bfb9	e1		.
	ld b,c			;bfba	41		A
lbfbbh:
	rla			;bfbb	17		.
	jr c,lbfb6h		;bfbc	38 f8		8 .
	exx			;bfbe	d9		.
	jr nz,lbf81h		;bfbf	20 c0		  .
	bit 7,a			;bfc1	cb 7f		. .
	jr z,lbfa7h		;bfc3	28 e2		( .
	sub 0eah		;bfc5	d6 ea		. .
lbfc7h:
	ex de,hl		;bfc7	eb		.
lbfc8h:
	pop de			;bfc8	d1		.
	ld (hl),e		;bfc9	73		s
	inc hl			;bfca	23		#
	ld (hl),d		;bfcb	72		r
	inc hl			;bfcc	23		#
	dec a			;bfcd	3d		=
	jr nz,lbfc8h		;bfce	20 f8		  .
	ex de,hl		;bfd0	eb		.
	jr nc,lbf6eh		;bfd1	30 9b		0 .
	ld hl,02758h		;bfd3	21 58 27	! X '
	exx			;bfd6	d9		.
	ld sp,ENTRY		;bfd7	31 00 60	1 . `
	di			;bfda	f3		.
	jp ENTRY		;bfdb	c3 00 60	. . `
	nop			;bfde	00		.
	nop			;bfdf	00		.

; --------------------------------------------------------------------------
; DATA - six bytes ('...)',0,0) emitted by the depacker epilogue
; through LD SP,IX + three POP DE pairs (IX was set by stage-1).
; --------------------------------------------------------------------------
; --------------------------------------------------------------------------
; DATA 0xBFE0-0xBFE5  END_TABLE - depacker end-marker bytes
; --------------------------------------------------------------------------
END_TABLE:
	defw 0x2E2E
	defw 0x292E
	defw 0x0000

; --------------------------------------------------------------------------
; DATA 0xBFE6-0xBFFF  unused RAM (zero in the reference capture)
; --------------------------------------------------------------------------
; 26 bytes of zero fill omitted (0xBFE6-0xBFFF)

