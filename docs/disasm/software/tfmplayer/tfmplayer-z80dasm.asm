; ==========================================================================
; TFM Music Compiler 1.12 player ('_tsfmplaye' block of TSFM-EL.TAP)
; binary: player_org61A8.bin  org 0x61A8  len 1883
; 
; Machine-generated z80dasm reference. The hand-written annotated
; source (same labels, full commentary, assembles bit-identical)
; lives in tfmplayer.asm - read that one instead.
; 
; Drives the TurboSound FM board (2xYM2203, ports 0xFFFD/0xBFFD,
; control words 0xF8/0xF9 select the chip; #FFFD reads = status).
; Position-dependent: must sit at 0x61A8; tunes at 0x8000.
; 
; READING ORDER
;   1. MAIN_ENTRY / PLAY_LOOP     the .sna hand-off point
;   2. INIT                       version check + pattern relocation
;   3. CHIP_RESET / REG_WRITE     how the chips are silenced
;   4. FRAME + channel stubs      the per-interrupt interpreter
;   5. OUT_7FFD                   free service entry for embedders
; ==========================================================================

	org 0x61A8

; --------------------------------------------------------------------------
; symbol table - named anchors (auto labels appear inline at use sites)
; --------------------------------------------------------------------------
MAIN_ENTRY          : equ 0x61A8
PLAY_LOOP           : equ 0x61AF
TRAMP_INIT          : equ 0x61CC
TRAMP_FRAME         : equ 0x61CF
TRAMP_STOP          : equ 0x61D2
INIT                : equ 0x61D5
INIT_PATCH          : equ 0x61E4
RELOC_LOOP          : equ 0x61EE
CHIP_RESET          : equ 0x622A
REG_MOP             : equ 0x6238
REG_WRITE           : equ 0x628A
WAIT_REG            : equ 0x628B
WAIT_DATA           : equ 0x6293
SELECT_F8           : equ 0x629D
SELECT_F9           : equ 0x62A3
OUT_7FFD            : equ 0x62A9
FRAME               : equ 0x62B1
FRAME_DIVIDER       : equ 0x62D2
OUTI_PAIRS          : equ 0x62DF
OP_INSTRUMENT       : equ 0x62F3
OP_BIG_DISPATCH     : equ 0x6315
OP_SET_STATE        : equ 0x6323
OP_REG_WRITE        : equ 0x6327
WRITE_REG_DATA      : equ 0x6337
OP_LOOP             : equ 0x6359
OP_RESTART          : equ 0x635F
CH1                 : equ 0x6365
OP_DISPATCH         : equ 0x637A
CH2                 : equ 0x6465
CH3                 : equ 0x6566
CH4                 : equ 0x6667
CH5                 : equ 0x6767
CH6                 : equ 0x6868
RELOC_TABLE         : equ 0x68F7

; --------------------------------------------------------------------------
; LD HL,08000h (tune base); CALL TRAMP_INIT; EI; then the HALT loop:
; one interrupt = one frame. Border flips black/white around the frame
; call - the classic visual heartbeat. Any key half-row bit low exits:
; CALL TRAMP_STOP (chip reset), HL'=02758h for the caller, RET.
; This is the address a .sna sets as PC to start playback (see
; tsfm-sna-guide.md in this folder).
; --------------------------------------------------------------------------
MAIN_ENTRY:
	ld hl,08000h		;61a8	21 00 80	! . .
	call TRAMP_INIT		;61ab	cd cc 61	. . a
	ei			;61ae	fb		.
; --------------------------------------------------------------------------
; HALT loop body: border 0, FRAME, border 7, key scan
; --------------------------------------------------------------------------
PLAY_LOOP:
	halt			;61af	76		v
	xor a			;61b0	af		.
	out (0feh),a		;61b1	d3 fe		. .
	call TRAMP_FRAME	;61b3	cd cf 61	. . a
	ld a,007h		;61b6	3e 07		> .
	out (0feh),a		;61b8	d3 fe		. .
	xor a			;61ba	af		.
	in a,(0feh)		;61bb	db fe		. .
	and 01fh		;61bd	e6 1f		. .
	cp 01fh			;61bf	fe 1f		. .
	jr z,PLAY_LOOP		;61c1	28 ec		( .
	call TRAMP_STOP		;61c3	cd d2 61	. . a
	exx			;61c6	d9		.
	ld hl,02758h		;61c7	21 58 27	! X '
	exx			;61ca	d9		.
	ret			;61cb	c9		.
; --------------------------------------------------------------------------
; JP INIT - trampoline so callers need no internals
; --------------------------------------------------------------------------
TRAMP_INIT:
	jp INIT			;61cc	c3 d5 61	. . a
; --------------------------------------------------------------------------
; JP FRAME - per-interrupt entry from the HALT loop
; --------------------------------------------------------------------------
TRAMP_FRAME:
	jp FRAME		;61cf	c3 b1 62	. . b
; --------------------------------------------------------------------------
; JP CHIP_RESET - mute + rearm both chips
; --------------------------------------------------------------------------
TRAMP_STOP:
	jp CHIP_RESET		;61d2	c3 2a 62	. * b
; --------------------------------------------------------------------------
; HL = tune base ('TFMcom1.12'). tune+9 is the version character:
; '2' (032h) = current format  -> patch FRAME epilogue to plain RET
; anything else              -> keep LD A,6 divider (play 1 of 6 frames)
; Then the relocation pass: six 16-bit offsets from tune+10..tune+21
; are converted to absolute addresses and stored through RELOC_TABLE
; into the channel stubs' LD HL,nnnn slots (one per YM2203 channel).
; Each stub's LD A,0FDh operand (channel state slot) is reset to 0FFh
; ('pattern not started'). FALLS THROUGH into CHIP_RESET - init also
; mutes and rearms both chips before the first frame.
; --------------------------------------------------------------------------
INIT:
	ex de,hl		;61d5	eb		.
	ld hl,00009h		;61d6	21 09 00	! . .
	add hl,de		;61d9	19		.
	ld a,(hl)		;61da	7e		~
	inc hl			;61db	23		#
	cp 032h			;61dc	fe 32		. 2
	ld a,0c9h		;61de	3e c9		> .
	jr z,INIT_PATCH		;61e0	28 02		( .
	ld a,03eh		;61e2	3e 3e		> >
; --------------------------------------------------------------------------
; LD (FRAME_DIVIDER),A - the version-dependent SMC patch
; --------------------------------------------------------------------------
INIT_PATCH:
	ld (FRAME_DIVIDER),a	;61e4	32 d2 62	2 . b
	exx			;61e7	d9		.
	ld hl,RELOC_TABLE	;61e8	21 f7 68	! . h
	ld bc,006fdh		;61eb	01 fd 06	. . .
; --------------------------------------------------------------------------
; Six-iteration LDI/LDI + pointer-fix loop. HL' walks RELOC_TABLE,
; DE'=061FDh is the tune-base-relative delta, BC counts 0x06FD times
; for the LDI pair (copies the tune header area), and each offset from
; the tune is ADDed to the tune base before storing to the stub slot.
; --------------------------------------------------------------------------
RELOC_LOOP:
	ld de,061fdh		;61ee	11 fd 61	. . a
	ldi			;61f1	ed a0		. .
	ldi			;61f3	ed a0		. .
	exx			;61f5	d9		.
	ld a,(hl)		;61f6	7e		~
	inc hl			;61f7	23		#
	push hl			;61f8	e5		.
	ld h,(hl)		;61f9	66		f
	ld l,a			;61fa	6f		o
	add hl,de		;61fb	19		.
	ld (00000h),hl		;61fc	22 00 00	" . .
	pop hl			;61ff	e1		.
	inc hl			;6200	23		#
	exx			;6201	d9		.
	djnz RELOC_LOOP		;6202	10 ea		. .
	xor a			;6204	af		.
	ld (0636eh),a		;6205	32 6e 63	2 n c
	ld (0646eh),a		;6208	32 6e 64	2 n d
	ld (0656fh),a		;620b	32 6f 65	2 o e
	ld (06670h),a		;620e	32 70 66	2 p f
	ld (06770h),a		;6211	32 70 67	2 p g
	ld (06871h),a		;6214	32 71 68	2 q h
	dec a			;6217	3d		=
	ld (CH1+1),a		;6218	32 66 63	2 f c
	ld (CH2+1),a		;621b	32 66 64	2 f d
	ld (CH3+1),a		;621e	32 67 65	2 g e
	ld (CH4+1),a		;6221	32 68 66	2 h f
	ld (CH5+1),a		;6224	32 68 67	2 h g
	ld (CH6+1),a		;6227	32 69 68	2 i h
; --------------------------------------------------------------------------
; Select control word 0xF8 (chip 0), CALL REG_MOP (zero/silence every
; YM2203 register group), then SELECT_F9 for chip 1 - but note
; REG_MOP itself leaves the selection as it found it: the final
; SELECT_F9 call here switches the ACTIVE chip for the next frame.
; Shared by INIT fall-through and the keypress exit path.
; --------------------------------------------------------------------------
CHIP_RESET:
	ld de,0ffbfh		;622a	11 bf ff	. . .
	ld c,0fdh		;622d	0e fd		. .
	call SELECT_F8		;622f	cd 9d 62	. . b
	call REG_MOP		;6232	cd 38 62	. 8 b
	call SELECT_F9		;6235	cd a3 62	. . b
; --------------------------------------------------------------------------
; Zeroing sweep, register/data pairs written through REG_WRITE:
;   regs 0x0D..0x00 data 0x00   (period/noise/env - AY half)
;   regs 0xB3..0x40 data 0x0F   (FM operators: max attenuation = silent)
;     (0x4F would collide with the SSG envelope; patched to 0x3F)
;   regs 0x8F..0x80 data 0x0F   (channel total levels = silent)
;   reg  0x28 data 0,1,2 then 0x2A,0x2B key-off every FM channel
;   regs 0x7F..0x40 data 0x0F / reg 0x2F / reg 0x2D (0x2D = mode: AY)
; The same sweep, with 0xF9 selected first, silences chip 1.
; --------------------------------------------------------------------------
REG_MOP:
	xor a			;6238	af		.
	ex af,af'		;6239	08		.
	ld a,00dh		;623a	3e 0d		> .
l623ch:
	call REG_WRITE		;623c	cd 8a 62	. . b
	dec a			;623f	3d		=
	jp p,l623ch		;6240	f2 3c 62	. < b
	ld a,0b3h		;6243	3e b3		> .
l6245h:
	cp 04fh			;6245	fe 4f		. O
	jr nz,l624bh		;6247	20 02		  .
	ld a,03fh		;6249	3e 3f		> ?
l624bh:
	call REG_WRITE		;624b	cd 8a 62	. . b
	dec a			;624e	3d		=
	cp 030h			;624f	fe 30		. 0
	jr nc,l6245h		;6251	30 f2		0 .
	ld a,00fh		;6253	3e 0f		> .
	ex af,af'		;6255	08		.
	ld a,08fh		;6256	3e 8f		> .
l6258h:
	call REG_WRITE		;6258	cd 8a 62	. . b
	dec a			;625b	3d		=
	jp m,l6258h		;625c	fa 58 62	. X b
	xor a			;625f	af		.
	ex af,af'		;6260	08		.
	ld a,028h		;6261	3e 28		> (
	call REG_WRITE		;6263	cd 8a 62	. . b
	ex af,af'		;6266	08		.
	inc a			;6267	3c		<
	ex af,af'		;6268	08		.
	call REG_WRITE		;6269	cd 8a 62	. . b
	ex af,af'		;626c	08		.
	inc a			;626d	3c		<
	ex af,af'		;626e	08		.
	call REG_WRITE		;626f	cd 8a 62	. . b
	dec a			;6272	3d		=
	call REG_WRITE		;6273	cd 8a 62	. . b
	ld a,07fh		;6276	3e 7f		> .
	ex af,af'		;6278	08		.
	ld a,04fh		;6279	3e 4f		> O
l627bh:
	call REG_WRITE		;627b	cd 8a 62	. . b
	dec a			;627e	3d		=
	cp 040h			;627f	fe 40		. @
	jr nc,l627bh		;6281	30 f8		0 .
	ld a,02fh		;6283	3e 2f		> /
	call REG_WRITE		;6285	cd 8a 62	. . b
	ld a,02dh		;6288	3e 2d		> -
; --------------------------------------------------------------------------
; The register/data pair helper (A = register, A' = data):
;   poll IN F,(C) on 0xFFFD until bit7 clear (not busy), OUT register
;   swap AF; poll again; OUT data to 0xBFFD (B=0xBF for the data half)
; B=0xFF/B=0xBF are kept in D/E by every caller. On TSFM hardware
; 0xFFFD reads return a status byte (bit7=busy); the 32-cycle busy
; window after each write is what these loops wait out.
; --------------------------------------------------------------------------
REG_WRITE:
	ld b,d			;628a	42		B
; --------------------------------------------------------------------------
; busy-poll before the register-select OUT
; --------------------------------------------------------------------------
WAIT_REG:
	defb 0edh,070h ;in f,(c)	;628b	ed 70		. p
	jp m,WAIT_REG		;628d	fa 8b 62	. . b
	out (c),a		;6290	ed 79		. y
	ex af,af'		;6292	08		.
; --------------------------------------------------------------------------
; busy-poll before the data OUT (the P4 park site on
; legacy 2xAY TurboSound: the AY mixer reads back 0xFF
; with bit7 set, so the loop never exits)
; --------------------------------------------------------------------------
WAIT_DATA:
	defb 0edh,070h ;in f,(c)	;6293	ed 70		. p
	jp m,WAIT_DATA		;6295	fa 93 62	. . b
	ld b,e			;6298	43		C
	out (c),a		;6299	ed 79		. y
	ex af,af'		;629b	08		.
	ret			;629c	c9		.
; --------------------------------------------------------------------------
; OUT 0xFFFD,0xF8 - control word: chip 0 active
; --------------------------------------------------------------------------
SELECT_F8:
	ld a,0f8h		;629d	3e f8		> .
	ld b,d			;629f	42		B
	out (c),a		;62a0	ed 79		. y
	ret			;62a2	c9		.
; --------------------------------------------------------------------------
; OUT 0xFFFD,0xF9 - control word: chip 1 active
; --------------------------------------------------------------------------
SELECT_F9:
	ld a,0f9h		;62a3	3e f9		> .
	ld b,d			;62a5	42		B
	out (c),a		;62a6	ed 79		. y
	ret			;62a8	c9		.
; --------------------------------------------------------------------------
; OUT (0x7FFD),A helper (Pentagon latch). Not called from inside the
; player - a service entry for the embedding program (e.g. to restore
; ROM paging after a TR-DOS load, exactly what the tsfm-elite boot
; stub replicates).
; --------------------------------------------------------------------------
OUT_7FFD:
	push bc			;62a9	c5		.
	ld bc,07ffdh		;62aa	01 fd 7f	. . .
	out (c),a		;62ad	ed 79		. y
	pop bc			;62af	c1		.
	ret			;62b0	c9		.
; --------------------------------------------------------------------------
; The per-interrupt play routine, called from PLAY_LOOP:
;   SELECT_F8; CALL CH1/CH2/CH3 (chip 0 - YM #1)
;   SELECT_F9; CALL CH4/CH5/CH6 (chip 1 - YM #2)
; Each channel stub interprets its pattern stream and emits YM2203
; register writes through the same busy-polled port pairs.
; --------------------------------------------------------------------------
FRAME:
	ld de,0ffbfh		;62b1	11 bf ff	. . .
	ld c,0fdh		;62b4	0e fd		. .
	ld b,d			;62b6	42		B
	ld a,0f8h		;62b7	3e f8		> .
	out (c),a		;62b9	ed 79		. y
	call CH1		;62bb	cd 65 63	. e c
	call CH2		;62be	cd 65 64	. e d
	call CH3		;62c1	cd 66 65	. f e
	ld b,d			;62c4	42		B
	ld a,0f9h		;62c5	3e f9		> .
	out (c),a		;62c7	ed 79		. y
	call CH4		;62c9	cd 67 66	. g f
	call CH5		;62cc	cd 67 67	. g g
	call CH6		;62cf	cd 68 68	. h h
; --------------------------------------------------------------------------
; SMC frame-rate divider, patched by INIT: for TFMcom1.12 tunes
; (version '2') INIT writes a RET here, so every frame plays; older
; tunes keep LD A,6 / DEC A / JR NZ and only re-enter FRAME every 6th
; interrupt (12.5 Hz effective frame rate for pre-1.0 data).
; --------------------------------------------------------------------------
FRAME_DIVIDER:
	ld a,006h		;62d2	3e 06		> .
	dec a			;62d4	3d		=
	jr nz,l62d9h		;62d5	20 02		  .
	ld a,006h		;62d7	3e 06		> .
l62d9h:
	ld (FRAME_DIVIDER+1),a	;62d9	32 d3 62	2 . b
	jr z,FRAME		;62dc	28 d3		( .
	ret			;62de	c9		.
; --------------------------------------------------------------------------
; Bulk block writer for register/data streams: poll, OUTI the register
; byte to 0xFFFD, poll, OUTI the data byte to 0xBFFD; A counts pairs.
; Used by the instrument-load op to push a whole operator table.
; --------------------------------------------------------------------------
OUTI_PAIRS:
	ld b,d			;62df	42		B
l62e0h:
	defb 0edh,070h ;in f,(c)	;62e0	ed 70		. p
	jp m,l62e0h		;62e2	fa e0 62	. . b
	outi			;62e5	ed a3		. .
l62e7h:
	defb 0edh,070h ;in f,(c)	;62e7	ed 70		. p
	jp m,l62e7h		;62e9	fa e7 62	. . b
	ld b,e			;62ec	43		C
	outi			;62ed	ed a3		. .
	dec a			;62ef	3d		=
	jr nz,OUTI_PAIRS	;62f0	20 ed		  .
	ret			;62f2	c9		.
; --------------------------------------------------------------------------
; Pattern op: byte  -> CH1's LD A,(HL) slot at 636Eh (operator table
; length), then BC = 16-bit instrument data pointer from the stream,
; stored into the stub's LD HL,nnnn slot at 6378h; entry continues
; into OP_DISPATCH inline (CALL-less tail sharing).
; --------------------------------------------------------------------------
OP_INSTRUMENT:
	ld a,(hl)		;62f3	7e		~
	inc hl			;62f4	23		#
	ld (0636eh),a		;62f5	32 6e 63	2 n c
	ld b,(hl)		;62f8	46		F
	inc hl			;62f9	23		#
	ld c,(hl)		;62fa	4e		N
	inc hl			;62fb	23		#
	ld (06378h),hl		;62fc	22 78 63	" x c
	add hl,bc		;62ff	09		.
	ld c,0fdh		;6300	0e fd		. .
	jp OP_DISPATCH		;6302	c3 7a 63	. z c
l6305h:
	ld b,(hl)		;6305	46		F
	inc hl			;6306	23		#
l6307h:
	ld c,(hl)		;6307	4e		N
	inc hl			;6308	23		#
	push hl			;6309	e5		.
	add hl,bc		;630a	09		.
	ld c,0fdh		;630b	0e fd		. .
	call OP_DISPATCH	;630d	cd 7a 63	. z c
	pop hl			;6310	e1		.
	ld (0636bh),hl		;6311	22 6b 63	" k c
	ret			;6314	c9		.
; --------------------------------------------------------------------------
; Second-level dispatch for opcodes >= 0xBF reached from OP_DISPATCH:
;   0xBF   JR Z taken -> OP_CALL (call pattern, 0x6305)
;   >=0xE0 B=A (register block select), 0xFF -> OP_CALL_16 (0x6307)
;          else the opcode becomes the channel's next state value
;          (written into the stub's LD A,nn slot - see 0x6323)
;   0xC0..0xDF -> OP_REG_WRITE (0x6327) after the +0x30/+0xC3 SMC dance
; --------------------------------------------------------------------------
OP_BIG_DISPATCH:
	jr z,l6305h		;6315	28 ee		( .
	cp 0e0h			;6317	fe e0		. .
	jr c,OP_REG_WRITE	;6319	38 0c		8 .
	ld b,a			;631b	47		G
	cp 0ffh			;631c	fe ff		. .
	jr z,l6307h		;631e	28 e7		( .
	ld (0636bh),hl		;6320	22 6b 63	" k c
; --------------------------------------------------------------------------
; LD (CH1+1),A - opcode becomes the channel state byte
; checked by the stub prologue on the next frame
; --------------------------------------------------------------------------
OP_SET_STATE:
	ld (CH1+1),a		;6323	32 66 63	2 f c
	ret			;6326	c9		.
; --------------------------------------------------------------------------
; General YM register write op: A' carries the data byte; the two
; ADDs map the opcode into a register number, SMC'd into 632Ch, then
; HL = 22A4h-style (reg<<8|data) pairs are pushed through the busy-
; polled OUT sequence (register to 0xFFFD, data to 0xBFFD, then a
; 0xA0-class register write with A) at WRITE_REG_DATA.
; --------------------------------------------------------------------------
OP_REG_WRITE:
	add a,030h		;6327	c6 30		. 0
	jr z,OP_INSTRUMENT	;6329	28 c8		( .
	add a,0c3h		;632b	c6 c3		. .
	ld (0632ch),a		;632d	32 2c 63	2 , c
	ld (0636bh),hl		;6330	22 6b 63	" k c
	ld b,d			;6333	42		B
	ld hl,022a4h		;6334	21 a4 22	! . "
; --------------------------------------------------------------------------
; poll/OUT L (register), poll/OUT H (data) - the
; interpreter's write helper (like REG_WRITE but
; register+data travel in HL)
; --------------------------------------------------------------------------
WRITE_REG_DATA:
	defb 0edh,070h ;in f,(c)	;6337	ed 70		. p
	jp m,WRITE_REG_DATA	;6339	fa 37 63	. 7 c
	out (c),l		;633c	ed 69		. i
l633eh:
	defb 0edh,070h ;in f,(c)	;633e	ed 70		. p
	jp m,l633eh		;6340	fa 3e 63	. > c
	ld b,e			;6343	43		C
	out (c),h		;6344	ed 61		. a
	ld b,d			;6346	42		B
	ld l,0a0h		;6347	2e a0		. .
l6349h:
	defb 0edh,070h ;in f,(c)	;6349	ed 70		. p
	jp m,l6349h		;634b	fa 49 63	. I c
	out (c),l		;634e	ed 69		. i
l6350h:
	defb 0edh,070h ;in f,(c)	;6350	ed 70		. p
	jp m,l6350h		;6352	fa 50 63	. P c
	ld b,e			;6355	43		C
	out (c),a		;6356	ed 79		. y
	ret			;6358	c9		.
; --------------------------------------------------------------------------
; 0x7E marker: remember the current stream position as the
; pattern loop point (SMC slot at 0x6360)
; --------------------------------------------------------------------------
OP_LOOP:
	ld (OP_RESTART+1),hl	;6359	22 60 63	" ` c
	jp OP_DISPATCH		;635c	c3 7a 63	. z c
; --------------------------------------------------------------------------
; 0x7F marker: reload HL from the loop point and replay
; --------------------------------------------------------------------------
OP_RESTART:
	ld hl,00000h		;635f	21 00 00	! . .
	jp OP_DISPATCH		;6362	c3 7a 63	. z c
; --------------------------------------------------------------------------
; channel stub 1 - chip 0 channel 1 (see CHANNEL STUBS below)
; --------------------------------------------------------------------------
CH1:
	ld a,0fdh		;6365	3e fd		> .
	inc a			;6367	3c		<
	jr nz,OP_SET_STATE	;6368	20 b9		  .
	ld hl,00000h		;636a	21 00 00	! . .
	or 000h			;636d	f6 00		. .
	jr z,OP_DISPATCH	;636f	28 09		( .
	dec a			;6371	3d		=
	ld (0636eh),a		;6372	32 6e 63	2 n c
	jr nz,OP_DISPATCH	;6375	20 03		  .
	ld hl,00000h		;6377	21 00 00	! . .
; --------------------------------------------------------------------------
; Fetch opcode from (HL):
;   0x7E -> OP_LOOP      0x7F -> OP_RESTART
;   < 0x80 (bit6 set)   -> key control path (reg 0x28 + freq pairs)
;   >= 0xBF              -> OP_BIG_DISPATCH
; everything else falls through as data for the previous op.
; --------------------------------------------------------------------------
OP_DISPATCH:
	ld a,(hl)		;637a	7e		~
	inc hl			;637b	23		#
	cp 07eh			;637c	fe 7e		. ~
	jr z,OP_LOOP		;637e	28 d9		( .
	cp 07fh			;6380	fe 7f		. .
	jr z,OP_RESTART		;6382	28 db		( .
	cp e			;6384	bb		.
	jr nc,OP_BIG_DISPATCH	;6385	30 8e		0 .
	jp p,l639fh		;6387	f2 9f 63	. . c
	ex af,af'		;638a	08		.
	ld b,d			;638b	42		B
	ld a,028h		;638c	3e 28		> (
l638eh:
	defb 0edh,070h ;in f,(c)	;638e	ed 70		. p
	jp m,l638eh		;6390	fa 8e 63	. . c
	out (c),a		;6393	ed 79		. y
	xor a			;6395	af		.
l6396h:
	defb 0edh,070h ;in f,(c)	;6396	ed 70		. p
	jp m,l6396h		;6398	fa 96 63	. . c
	ld b,e			;639b	43		C
	out (c),a		;639c	ed 79		. y
	ex af,af'		;639e	08		.
l639fh:
	or a			;639f	b7		.
	push af			;63a0	f5		.
	rra			;63a1	1f		.
	jr nc,l63d4h		;63a2	30 30		0 0
	ex af,af'		;63a4	08		.
	ld b,d			;63a5	42		B
	ld a,0a4h		;63a6	3e a4		> .
l63a8h:
	defb 0edh,070h ;in f,(c)	;63a8	ed 70		. p
	jp m,l63a8h		;63aa	fa a8 63	. . c
	out (c),a		;63ad	ed 79		. y
	ld a,(hl)		;63af	7e		~
	inc hl			;63b0	23		#
	ld (06336h),a		;63b1	32 36 63	2 6 c
l63b4h:
	defb 0edh,070h ;in f,(c)	;63b4	ed 70		. p
	jp m,l63b4h		;63b6	fa b4 63	. . c
	ld b,e			;63b9	43		C
	out (c),a		;63ba	ed 79		. y
	ld b,d			;63bc	42		B
	ld a,0a0h		;63bd	3e a0		> .
l63bfh:
	defb 0edh,070h ;in f,(c)	;63bf	ed 70		. p
	jp m,l63bfh		;63c1	fa bf 63	. . c
	out (c),a		;63c4	ed 79		. y
	ld a,(hl)		;63c6	7e		~
	inc hl			;63c7	23		#
	ld (0632ch),a		;63c8	32 2c 63	2 , c
l63cbh:
	defb 0edh,070h ;in f,(c)	;63cb	ed 70		. p
	jp m,l63cbh		;63cd	fa cb 63	. . c
	ld b,e			;63d0	43		C
	out (c),a		;63d1	ed 79		. y
	ex af,af'		;63d3	08		.
l63d4h:
	and 01fh		;63d4	e6 1f		. .
	call nz,OUTI_PAIRS	;63d6	c4 df 62	. . b
	ld (0636bh),hl		;63d9	22 6b 63	" k c
	pop af			;63dc	f1		.
	ret p			;63dd	f0		.
	ld b,d			;63de	42		B
	ld a,028h		;63df	3e 28		> (
l63e1h:
	defb 0edh,070h ;in f,(c)	;63e1	ed 70		. p
	jp m,l63e1h		;63e3	fa e1 63	. . c
	out (c),a		;63e6	ed 79		. y
	ld a,0f0h		;63e8	3e f0		> .
l63eah:
	defb 0edh,070h ;in f,(c)	;63ea	ed 70		. p
	jp m,l63eah		;63ec	fa ea 63	. . c
	ld b,e			;63ef	43		C
	out (c),a		;63f0	ed 79		. y
	ret			;63f2	c9		.
l63f3h:
	ld a,(hl)		;63f3	7e		~
	inc hl			;63f4	23		#
	ld (0646eh),a		;63f5	32 6e 64	2 n d
	ld b,(hl)		;63f8	46		F
	inc hl			;63f9	23		#
	ld c,(hl)		;63fa	4e		N
	inc hl			;63fb	23		#
	ld (06478h),hl		;63fc	22 78 64	" x d
	add hl,bc		;63ff	09		.
	ld c,0fdh		;6400	0e fd		. .
	jp R_647A		;6402	c3 7a 64	. z d
l6405h:
	ld b,(hl)		;6405	46		F
	inc hl			;6406	23		#
l6407h:
	ld c,(hl)		;6407	4e		N
	inc hl			;6408	23		#
	push hl			;6409	e5		.
	add hl,bc		;640a	09		.
	ld c,0fdh		;640b	0e fd		. .
	call R_647A		;640d	cd 7a 64	. z d
	pop hl			;6410	e1		.
	ld (0646bh),hl		;6411	22 6b 64	" k d
	ret			;6414	c9		.
l6415h:
	jr z,l6405h		;6415	28 ee		( .
	cp 0e0h			;6417	fe e0		. .
	jr c,l6427h		;6419	38 0c		8 .
	ld b,a			;641b	47		G
	cp 0ffh			;641c	fe ff		. .
	jr z,l6407h		;641e	28 e7		( .
	ld (0646bh),hl		;6420	22 6b 64	" k d
l6423h:
	ld (CH2+1),a		;6423	32 66 64	2 f d
	ret			;6426	c9		.
l6427h:
	add a,030h		;6427	c6 30		. 0
	jr z,l63f3h		;6429	28 c8		( .
	add a,0afh		;642b	c6 af		. .
	ld (0642ch),a		;642d	32 2c 64	2 , d
	ld (0646bh),hl		;6430	22 6b 64	" k d
	ld b,d			;6433	42		B
	ld hl,013a5h		;6434	21 a5 13	! . .
l6437h:
	defb 0edh,070h ;in f,(c)	;6437	ed 70		. p
	jp m,l6437h		;6439	fa 37 64	. 7 d
	out (c),l		;643c	ed 69		. i
l643eh:
	defb 0edh,070h ;in f,(c)	;643e	ed 70		. p
	jp m,l643eh		;6440	fa 3e 64	. > d
	ld b,e			;6443	43		C
	out (c),h		;6444	ed 61		. a
	ld b,d			;6446	42		B
	ld l,0a1h		;6447	2e a1		. .
l6449h:
	defb 0edh,070h ;in f,(c)	;6449	ed 70		. p
	jp m,l6449h		;644b	fa 49 64	. I d
	out (c),l		;644e	ed 69		. i
l6450h:
	defb 0edh,070h ;in f,(c)	;6450	ed 70		. p
	jp m,l6450h		;6452	fa 50 64	. P d
	ld b,e			;6455	43		C
	out (c),a		;6456	ed 79		. y
	ret			;6458	c9		.
l6459h:
	ld (l645fh+1),hl	;6459	22 60 64	" ` d
	jp R_647A		;645c	c3 7a 64	. z d
l645fh:
	ld hl,00000h		;645f	21 00 00	! . .
	jp R_647A		;6462	c3 7a 64	. z d
; --------------------------------------------------------------------------
; channel stub 2 - chip 0 channel 2
; --------------------------------------------------------------------------
CH2:
	ld a,0ech		;6465	3e ec		> .
	inc a			;6467	3c		<
	jr nz,l6423h		;6468	20 b9		  .
	ld hl,00000h		;646a	21 00 00	! . .
	or 000h			;646d	f6 00		. .
	jr z,R_647A		;646f	28 09		( .
	dec a			;6471	3d		=
	ld (0646eh),a		;6472	32 6e 64	2 n d
	jr nz,R_647A		;6475	20 03		  .
	ld hl,00000h		;6477	21 00 00	! . .
R_647A:
	ld a,(hl)		;647a	7e		~
	inc hl			;647b	23		#
	cp 07eh			;647c	fe 7e		. ~
	jr z,l6459h		;647e	28 d9		( .
	cp 07fh			;6480	fe 7f		. .
	jr z,l645fh		;6482	28 db		( .
	cp e			;6484	bb		.
	jr nc,l6415h		;6485	30 8e		0 .
	jp p,l64a0h		;6487	f2 a0 64	. . d
	ex af,af'		;648a	08		.
	ld b,d			;648b	42		B
	ld a,028h		;648c	3e 28		> (
l648eh:
	defb 0edh,070h ;in f,(c)	;648e	ed 70		. p
	jp m,l648eh		;6490	fa 8e 64	. . d
	out (c),a		;6493	ed 79		. y
	ld a,001h		;6495	3e 01		> .
l6497h:
	defb 0edh,070h ;in f,(c)	;6497	ed 70		. p
	jp m,l6497h		;6499	fa 97 64	. . d
	ld b,e			;649c	43		C
	out (c),a		;649d	ed 79		. y
	ex af,af'		;649f	08		.
l64a0h:
	or a			;64a0	b7		.
	push af			;64a1	f5		.
	rra			;64a2	1f		.
	jr nc,l64d5h		;64a3	30 30		0 0
	ex af,af'		;64a5	08		.
	ld b,d			;64a6	42		B
	ld a,0a5h		;64a7	3e a5		> .
l64a9h:
	defb 0edh,070h ;in f,(c)	;64a9	ed 70		. p
	jp m,l64a9h		;64ab	fa a9 64	. . d
	out (c),a		;64ae	ed 79		. y
	ld a,(hl)		;64b0	7e		~
	inc hl			;64b1	23		#
	ld (06436h),a		;64b2	32 36 64	2 6 d
l64b5h:
	defb 0edh,070h ;in f,(c)	;64b5	ed 70		. p
	jp m,l64b5h		;64b7	fa b5 64	. . d
	ld b,e			;64ba	43		C
	out (c),a		;64bb	ed 79		. y
	ld b,d			;64bd	42		B
	ld a,0a1h		;64be	3e a1		> .
l64c0h:
	defb 0edh,070h ;in f,(c)	;64c0	ed 70		. p
	jp m,l64c0h		;64c2	fa c0 64	. . d
	out (c),a		;64c5	ed 79		. y
	ld a,(hl)		;64c7	7e		~
	inc hl			;64c8	23		#
	ld (0642ch),a		;64c9	32 2c 64	2 , d
l64cch:
	defb 0edh,070h ;in f,(c)	;64cc	ed 70		. p
	jp m,l64cch		;64ce	fa cc 64	. . d
	ld b,e			;64d1	43		C
	out (c),a		;64d2	ed 79		. y
	ex af,af'		;64d4	08		.
l64d5h:
	and 01fh		;64d5	e6 1f		. .
	call nz,OUTI_PAIRS	;64d7	c4 df 62	. . b
	ld (0646bh),hl		;64da	22 6b 64	" k d
	pop af			;64dd	f1		.
	ret p			;64de	f0		.
	ld b,d			;64df	42		B
	ld a,028h		;64e0	3e 28		> (
l64e2h:
	defb 0edh,070h ;in f,(c)	;64e2	ed 70		. p
	jp m,l64e2h		;64e4	fa e2 64	. . d
	out (c),a		;64e7	ed 79		. y
	ld a,0f1h		;64e9	3e f1		> .
l64ebh:
	defb 0edh,070h ;in f,(c)	;64eb	ed 70		. p
	jp m,l64ebh		;64ed	fa eb 64	. . d
	ld b,e			;64f0	43		C
	out (c),a		;64f1	ed 79		. y
	ret			;64f3	c9		.
l64f4h:
	ld a,(hl)		;64f4	7e		~
	inc hl			;64f5	23		#
	ld (0656fh),a		;64f6	32 6f 65	2 o e
	ld b,(hl)		;64f9	46		F
	inc hl			;64fa	23		#
	ld c,(hl)		;64fb	4e		N
	inc hl			;64fc	23		#
	ld (06579h),hl		;64fd	22 79 65	" y e
	add hl,bc		;6500	09		.
	ld c,0fdh		;6501	0e fd		. .
	jp R_657B		;6503	c3 7b 65	. { e
l6506h:
	ld b,(hl)		;6506	46		F
	inc hl			;6507	23		#
l6508h:
	ld c,(hl)		;6508	4e		N
	inc hl			;6509	23		#
	push hl			;650a	e5		.
	add hl,bc		;650b	09		.
	ld c,0fdh		;650c	0e fd		. .
	call R_657B		;650e	cd 7b 65	. { e
	pop hl			;6511	e1		.
	ld (0656ch),hl		;6512	22 6c 65	" l e
	ret			;6515	c9		.
l6516h:
	jr z,l6506h		;6516	28 ee		( .
	cp 0e0h			;6518	fe e0		. .
	jr c,l6528h		;651a	38 0c		8 .
	ld b,a			;651c	47		G
	cp 0ffh			;651d	fe ff		. .
	jr z,l6508h		;651f	28 e7		( .
	ld (0656ch),hl		;6521	22 6c 65	" l e
l6524h:
	ld (CH3+1),a		;6524	32 67 65	2 g e
	ret			;6527	c9		.
l6528h:
	add a,030h		;6528	c6 30		. 0
	jr z,l64f4h		;652a	28 c8		( .
	add a,0c3h		;652c	c6 c3		. .
	ld (0652dh),a		;652e	32 2d 65	2 - e
	ld (0656ch),hl		;6531	22 6c 65	" l e
	ld b,d			;6534	42		B
	ld hl,01aa6h		;6535	21 a6 1a	! . .
l6538h:
	defb 0edh,070h ;in f,(c)	;6538	ed 70		. p
	jp m,l6538h		;653a	fa 38 65	. 8 e
	out (c),l		;653d	ed 69		. i
l653fh:
	defb 0edh,070h ;in f,(c)	;653f	ed 70		. p
	jp m,l653fh		;6541	fa 3f 65	. ? e
	ld b,e			;6544	43		C
	out (c),h		;6545	ed 61		. a
	ld b,d			;6547	42		B
	ld l,0a2h		;6548	2e a2		. .
l654ah:
	defb 0edh,070h ;in f,(c)	;654a	ed 70		. p
	jp m,l654ah		;654c	fa 4a 65	. J e
	out (c),l		;654f	ed 69		. i
l6551h:
	defb 0edh,070h ;in f,(c)	;6551	ed 70		. p
	jp m,l6551h		;6553	fa 51 65	. Q e
	ld b,e			;6556	43		C
	out (c),a		;6557	ed 79		. y
	ret			;6559	c9		.
l655ah:
	ld (l6560h+1),hl	;655a	22 61 65	" a e
	jp R_657B		;655d	c3 7b 65	. { e
l6560h:
	ld hl,00000h		;6560	21 00 00	! . .
	jp R_657B		;6563	c3 7b 65	. { e
; --------------------------------------------------------------------------
; channel stub 3 - chip 0 channel 3
; --------------------------------------------------------------------------
CH3:
	ld a,0ech		;6566	3e ec		> .
	inc a			;6568	3c		<
	jr nz,l6524h		;6569	20 b9		  .
	ld hl,00000h		;656b	21 00 00	! . .
	or 000h			;656e	f6 00		. .
	jr z,R_657B		;6570	28 09		( .
	dec a			;6572	3d		=
	ld (0656fh),a		;6573	32 6f 65	2 o e
	jr nz,R_657B		;6576	20 03		  .
	ld hl,00000h		;6578	21 00 00	! . .
R_657B:
	ld a,(hl)		;657b	7e		~
	inc hl			;657c	23		#
	cp 07eh			;657d	fe 7e		. ~
	jr z,l655ah		;657f	28 d9		( .
	cp 07fh			;6581	fe 7f		. .
	jr z,l6560h		;6583	28 db		( .
	cp e			;6585	bb		.
	jr nc,l6516h		;6586	30 8e		0 .
	jp p,l65a1h		;6588	f2 a1 65	. . e
	ex af,af'		;658b	08		.
	ld b,d			;658c	42		B
	ld a,028h		;658d	3e 28		> (
l658fh:
	defb 0edh,070h ;in f,(c)	;658f	ed 70		. p
	jp m,l658fh		;6591	fa 8f 65	. . e
	out (c),a		;6594	ed 79		. y
	ld a,002h		;6596	3e 02		> .
l6598h:
	defb 0edh,070h ;in f,(c)	;6598	ed 70		. p
	jp m,l6598h		;659a	fa 98 65	. . e
	ld b,e			;659d	43		C
	out (c),a		;659e	ed 79		. y
	ex af,af'		;65a0	08		.
l65a1h:
	or a			;65a1	b7		.
	push af			;65a2	f5		.
	rra			;65a3	1f		.
	jr nc,l65d6h		;65a4	30 30		0 0
	ex af,af'		;65a6	08		.
	ld b,d			;65a7	42		B
	ld a,0a6h		;65a8	3e a6		> .
l65aah:
	defb 0edh,070h ;in f,(c)	;65aa	ed 70		. p
	jp m,l65aah		;65ac	fa aa 65	. . e
	out (c),a		;65af	ed 79		. y
	ld a,(hl)		;65b1	7e		~
	inc hl			;65b2	23		#
	ld (06537h),a		;65b3	32 37 65	2 7 e
l65b6h:
	defb 0edh,070h ;in f,(c)	;65b6	ed 70		. p
	jp m,l65b6h		;65b8	fa b6 65	. . e
	ld b,e			;65bb	43		C
	out (c),a		;65bc	ed 79		. y
	ld b,d			;65be	42		B
	ld a,0a2h		;65bf	3e a2		> .
l65c1h:
	defb 0edh,070h ;in f,(c)	;65c1	ed 70		. p
	jp m,l65c1h		;65c3	fa c1 65	. . e
	out (c),a		;65c6	ed 79		. y
	ld a,(hl)		;65c8	7e		~
	inc hl			;65c9	23		#
	ld (0652dh),a		;65ca	32 2d 65	2 - e
l65cdh:
	defb 0edh,070h ;in f,(c)	;65cd	ed 70		. p
	jp m,l65cdh		;65cf	fa cd 65	. . e
	ld b,e			;65d2	43		C
	out (c),a		;65d3	ed 79		. y
	ex af,af'		;65d5	08		.
l65d6h:
	and 01fh		;65d6	e6 1f		. .
	call nz,OUTI_PAIRS	;65d8	c4 df 62	. . b
	ld (0656ch),hl		;65db	22 6c 65	" l e
	pop af			;65de	f1		.
	ret p			;65df	f0		.
	ld b,d			;65e0	42		B
	ld a,028h		;65e1	3e 28		> (
l65e3h:
	defb 0edh,070h ;in f,(c)	;65e3	ed 70		. p
	jp m,l65e3h		;65e5	fa e3 65	. . e
	out (c),a		;65e8	ed 79		. y
	ld a,0f2h		;65ea	3e f2		> .
l65ech:
	defb 0edh,070h ;in f,(c)	;65ec	ed 70		. p
	jp m,l65ech		;65ee	fa ec 65	. . e
	ld b,e			;65f1	43		C
	out (c),a		;65f2	ed 79		. y
	ret			;65f4	c9		.
l65f5h:
	ld a,(hl)		;65f5	7e		~
	inc hl			;65f6	23		#
	ld (06670h),a		;65f7	32 70 66	2 p f
	ld b,(hl)		;65fa	46		F
	inc hl			;65fb	23		#
	ld c,(hl)		;65fc	4e		N
	inc hl			;65fd	23		#
	ld (0667ah),hl		;65fe	22 7a 66	" z f
	add hl,bc		;6601	09		.
	ld c,0fdh		;6602	0e fd		. .
	jp R_667C		;6604	c3 7c 66	. | f
l6607h:
	ld b,(hl)		;6607	46		F
	inc hl			;6608	23		#
l6609h:
	ld c,(hl)		;6609	4e		N
	inc hl			;660a	23		#
	push hl			;660b	e5		.
	add hl,bc		;660c	09		.
	ld c,0fdh		;660d	0e fd		. .
	call R_667C		;660f	cd 7c 66	. | f
	pop hl			;6612	e1		.
	ld (0666dh),hl		;6613	22 6d 66	" m f
	ret			;6616	c9		.
l6617h:
	jr z,l6607h		;6617	28 ee		( .
	cp 0e0h			;6619	fe e0		. .
	jr c,l6629h		;661b	38 0c		8 .
	ld b,a			;661d	47		G
	cp 0ffh			;661e	fe ff		. .
	jr z,l6609h		;6620	28 e7		( .
	ld (0666dh),hl		;6622	22 6d 66	" m f
l6625h:
	ld (CH4+1),a		;6625	32 68 66	2 h f
	ret			;6628	c9		.
l6629h:
	add a,030h		;6629	c6 30		. 0
	jr z,l65f5h		;662b	28 c8		( .
	add a,0c3h		;662d	c6 c3		. .
	ld (0662eh),a		;662f	32 2e 66	2 . f
	ld (0666dh),hl		;6632	22 6d 66	" m f
	ld b,d			;6635	42		B
	ld hl,01aa4h		;6636	21 a4 1a	! . .
l6639h:
	defb 0edh,070h ;in f,(c)	;6639	ed 70		. p
	jp m,l6639h		;663b	fa 39 66	. 9 f
	out (c),l		;663e	ed 69		. i
l6640h:
	defb 0edh,070h ;in f,(c)	;6640	ed 70		. p
	jp m,l6640h		;6642	fa 40 66	. @ f
	ld b,e			;6645	43		C
	out (c),h		;6646	ed 61		. a
	ld b,d			;6648	42		B
	ld l,0a0h		;6649	2e a0		. .
l664bh:
	defb 0edh,070h ;in f,(c)	;664b	ed 70		. p
	jp m,l664bh		;664d	fa 4b 66	. K f
	out (c),l		;6650	ed 69		. i
l6652h:
	defb 0edh,070h ;in f,(c)	;6652	ed 70		. p
	jp m,l6652h		;6654	fa 52 66	. R f
	ld b,e			;6657	43		C
	out (c),a		;6658	ed 79		. y
	ret			;665a	c9		.
l665bh:
	ld (l6661h+1),hl	;665b	22 62 66	" b f
	jp R_667C		;665e	c3 7c 66	. | f
l6661h:
	ld hl,00000h		;6661	21 00 00	! . .
	jp R_667C		;6664	c3 7c 66	. | f
; --------------------------------------------------------------------------
; channel stub 4 - chip 1 channel 1
; --------------------------------------------------------------------------
CH4:
	ld a,0feh		;6667	3e fe		> .
	inc a			;6669	3c		<
	jr nz,l6625h		;666a	20 b9		  .
	ld hl,00000h		;666c	21 00 00	! . .
	or 000h			;666f	f6 00		. .
	jr z,R_667C		;6671	28 09		( .
	dec a			;6673	3d		=
	ld (06670h),a		;6674	32 70 66	2 p f
	jr nz,R_667C		;6677	20 03		  .
	ld hl,00000h		;6679	21 00 00	! . .
R_667C:
	ld a,(hl)		;667c	7e		~
	inc hl			;667d	23		#
	cp 07eh			;667e	fe 7e		. ~
	jr z,l665bh		;6680	28 d9		( .
	cp 07fh			;6682	fe 7f		. .
	jr z,l6661h		;6684	28 db		( .
	cp e			;6686	bb		.
	jr nc,l6617h		;6687	30 8e		0 .
	jp p,l66a1h		;6689	f2 a1 66	. . f
	ex af,af'		;668c	08		.
	ld b,d			;668d	42		B
	ld a,028h		;668e	3e 28		> (
l6690h:
	defb 0edh,070h ;in f,(c)	;6690	ed 70		. p
	jp m,l6690h		;6692	fa 90 66	. . f
	out (c),a		;6695	ed 79		. y
	xor a			;6697	af		.
l6698h:
	defb 0edh,070h ;in f,(c)	;6698	ed 70		. p
	jp m,l6698h		;669a	fa 98 66	. . f
	ld b,e			;669d	43		C
	out (c),a		;669e	ed 79		. y
	ex af,af'		;66a0	08		.
l66a1h:
	or a			;66a1	b7		.
	push af			;66a2	f5		.
	rra			;66a3	1f		.
	jr nc,l66d6h		;66a4	30 30		0 0
	ex af,af'		;66a6	08		.
	ld b,d			;66a7	42		B
	ld a,0a4h		;66a8	3e a4		> .
l66aah:
	defb 0edh,070h ;in f,(c)	;66aa	ed 70		. p
	jp m,l66aah		;66ac	fa aa 66	. . f
	out (c),a		;66af	ed 79		. y
	ld a,(hl)		;66b1	7e		~
	inc hl			;66b2	23		#
	ld (06638h),a		;66b3	32 38 66	2 8 f
l66b6h:
	defb 0edh,070h ;in f,(c)	;66b6	ed 70		. p
	jp m,l66b6h		;66b8	fa b6 66	. . f
	ld b,e			;66bb	43		C
	out (c),a		;66bc	ed 79		. y
	ld b,d			;66be	42		B
	ld a,0a0h		;66bf	3e a0		> .
l66c1h:
	defb 0edh,070h ;in f,(c)	;66c1	ed 70		. p
	jp m,l66c1h		;66c3	fa c1 66	. . f
	out (c),a		;66c6	ed 79		. y
	ld a,(hl)		;66c8	7e		~
	inc hl			;66c9	23		#
	ld (0662eh),a		;66ca	32 2e 66	2 . f
l66cdh:
	defb 0edh,070h ;in f,(c)	;66cd	ed 70		. p
	jp m,l66cdh		;66cf	fa cd 66	. . f
	ld b,e			;66d2	43		C
	out (c),a		;66d3	ed 79		. y
	ex af,af'		;66d5	08		.
l66d6h:
	and 01fh		;66d6	e6 1f		. .
	call nz,OUTI_PAIRS	;66d8	c4 df 62	. . b
	ld (0666dh),hl		;66db	22 6d 66	" m f
	pop af			;66de	f1		.
	ret p			;66df	f0		.
	ld b,d			;66e0	42		B
	ld a,028h		;66e1	3e 28		> (
l66e3h:
	defb 0edh,070h ;in f,(c)	;66e3	ed 70		. p
	jp m,l66e3h		;66e5	fa e3 66	. . f
	out (c),a		;66e8	ed 79		. y
	ld a,0f0h		;66ea	3e f0		> .
l66ech:
	defb 0edh,070h ;in f,(c)	;66ec	ed 70		. p
	jp m,l66ech		;66ee	fa ec 66	. . f
	ld b,e			;66f1	43		C
	out (c),a		;66f2	ed 79		. y
	ret			;66f4	c9		.
l66f5h:
	ld a,(hl)		;66f5	7e		~
	inc hl			;66f6	23		#
	ld (06770h),a		;66f7	32 70 67	2 p g
	ld b,(hl)		;66fa	46		F
	inc hl			;66fb	23		#
	ld c,(hl)		;66fc	4e		N
	inc hl			;66fd	23		#
	ld (0677ah),hl		;66fe	22 7a 67	" z g
	add hl,bc		;6701	09		.
	ld c,0fdh		;6702	0e fd		. .
	jp R_677C		;6704	c3 7c 67	. | g
l6707h:
	ld b,(hl)		;6707	46		F
	inc hl			;6708	23		#
l6709h:
	ld c,(hl)		;6709	4e		N
	inc hl			;670a	23		#
	push hl			;670b	e5		.
	add hl,bc		;670c	09		.
	ld c,0fdh		;670d	0e fd		. .
	call R_677C		;670f	cd 7c 67	. | g
	pop hl			;6712	e1		.
	ld (0676dh),hl		;6713	22 6d 67	" m g
	ret			;6716	c9		.
l6717h:
	jr z,l6707h		;6717	28 ee		( .
	cp 0e0h			;6719	fe e0		. .
	jr c,l6729h		;671b	38 0c		8 .
	ld b,a			;671d	47		G
	cp 0ffh			;671e	fe ff		. .
	jr z,l6709h		;6720	28 e7		( .
	ld (0676dh),hl		;6722	22 6d 67	" m g
l6725h:
	ld (CH5+1),a		;6725	32 68 67	2 h g
	ret			;6728	c9		.
l6729h:
	add a,030h		;6729	c6 30		. 0
	jr z,l66f5h		;672b	28 c8		( .
	add a,0afh		;672d	c6 af		. .
	ld (0672eh),a		;672f	32 2e 67	2 . g
	ld (0676dh),hl		;6732	22 6d 67	" m g
	ld b,d			;6735	42		B
	ld hl,01ba5h		;6736	21 a5 1b	! . .
l6739h:
	defb 0edh,070h ;in f,(c)	;6739	ed 70		. p
	jp m,l6739h		;673b	fa 39 67	. 9 g
	out (c),l		;673e	ed 69		. i
l6740h:
	defb 0edh,070h ;in f,(c)	;6740	ed 70		. p
	jp m,l6740h		;6742	fa 40 67	. @ g
	ld b,e			;6745	43		C
	out (c),h		;6746	ed 61		. a
	ld b,d			;6748	42		B
	ld l,0a1h		;6749	2e a1		. .
l674bh:
	defb 0edh,070h ;in f,(c)	;674b	ed 70		. p
	jp m,l674bh		;674d	fa 4b 67	. K g
	out (c),l		;6750	ed 69		. i
l6752h:
	defb 0edh,070h ;in f,(c)	;6752	ed 70		. p
	jp m,l6752h		;6754	fa 52 67	. R g
	ld b,e			;6757	43		C
	out (c),a		;6758	ed 79		. y
	ret			;675a	c9		.
l675bh:
	ld (l6761h+1),hl	;675b	22 62 67	" b g
	jp R_677C		;675e	c3 7c 67	. | g
l6761h:
	ld hl,00000h		;6761	21 00 00	! . .
	jp R_677C		;6764	c3 7c 67	. | g
; --------------------------------------------------------------------------
; channel stub 5 - chip 1 channel 2
; --------------------------------------------------------------------------
CH5:
	ld a,0fdh		;6767	3e fd		> .
	inc a			;6769	3c		<
	jr nz,l6725h		;676a	20 b9		  .
	ld hl,00000h		;676c	21 00 00	! . .
	or 000h			;676f	f6 00		. .
	jr z,R_677C		;6771	28 09		( .
	dec a			;6773	3d		=
	ld (06770h),a		;6774	32 70 67	2 p g
	jr nz,R_677C		;6777	20 03		  .
	ld hl,00000h		;6779	21 00 00	! . .
R_677C:
	ld a,(hl)		;677c	7e		~
	inc hl			;677d	23		#
	cp 07eh			;677e	fe 7e		. ~
	jr z,l675bh		;6780	28 d9		( .
	cp 07fh			;6782	fe 7f		. .
	jr z,l6761h		;6784	28 db		( .
	cp e			;6786	bb		.
	jr nc,l6717h		;6787	30 8e		0 .
	jp p,l67a2h		;6789	f2 a2 67	. . g
	ex af,af'		;678c	08		.
	ld b,d			;678d	42		B
	ld a,028h		;678e	3e 28		> (
l6790h:
	defb 0edh,070h ;in f,(c)	;6790	ed 70		. p
	jp m,l6790h		;6792	fa 90 67	. . g
	out (c),a		;6795	ed 79		. y
	ld a,001h		;6797	3e 01		> .
l6799h:
	defb 0edh,070h ;in f,(c)	;6799	ed 70		. p
	jp m,l6799h		;679b	fa 99 67	. . g
	ld b,e			;679e	43		C
	out (c),a		;679f	ed 79		. y
	ex af,af'		;67a1	08		.
l67a2h:
	or a			;67a2	b7		.
	push af			;67a3	f5		.
	rra			;67a4	1f		.
	jr nc,l67d7h		;67a5	30 30		0 0
	ex af,af'		;67a7	08		.
	ld b,d			;67a8	42		B
	ld a,0a5h		;67a9	3e a5		> .
l67abh:
	defb 0edh,070h ;in f,(c)	;67ab	ed 70		. p
	jp m,l67abh		;67ad	fa ab 67	. . g
	out (c),a		;67b0	ed 79		. y
	ld a,(hl)		;67b2	7e		~
	inc hl			;67b3	23		#
	ld (06738h),a		;67b4	32 38 67	2 8 g
l67b7h:
	defb 0edh,070h ;in f,(c)	;67b7	ed 70		. p
	jp m,l67b7h		;67b9	fa b7 67	. . g
	ld b,e			;67bc	43		C
	out (c),a		;67bd	ed 79		. y
	ld b,d			;67bf	42		B
	ld a,0a1h		;67c0	3e a1		> .
l67c2h:
	defb 0edh,070h ;in f,(c)	;67c2	ed 70		. p
	jp m,l67c2h		;67c4	fa c2 67	. . g
	out (c),a		;67c7	ed 79		. y
	ld a,(hl)		;67c9	7e		~
	inc hl			;67ca	23		#
	ld (0672eh),a		;67cb	32 2e 67	2 . g
l67ceh:
	defb 0edh,070h ;in f,(c)	;67ce	ed 70		. p
	jp m,l67ceh		;67d0	fa ce 67	. . g
	ld b,e			;67d3	43		C
	out (c),a		;67d4	ed 79		. y
	ex af,af'		;67d6	08		.
l67d7h:
	and 01fh		;67d7	e6 1f		. .
	call nz,OUTI_PAIRS	;67d9	c4 df 62	. . b
	ld (0676dh),hl		;67dc	22 6d 67	" m g
	pop af			;67df	f1		.
	ret p			;67e0	f0		.
	ld b,d			;67e1	42		B
	ld a,028h		;67e2	3e 28		> (
l67e4h:
	defb 0edh,070h ;in f,(c)	;67e4	ed 70		. p
	jp m,l67e4h		;67e6	fa e4 67	. . g
	out (c),a		;67e9	ed 79		. y
	ld a,0f1h		;67eb	3e f1		> .
l67edh:
	defb 0edh,070h ;in f,(c)	;67ed	ed 70		. p
	jp m,l67edh		;67ef	fa ed 67	. . g
	ld b,e			;67f2	43		C
	out (c),a		;67f3	ed 79		. y
	ret			;67f5	c9		.
l67f6h:
	ld a,(hl)		;67f6	7e		~
	inc hl			;67f7	23		#
	ld (06871h),a		;67f8	32 71 68	2 q h
	ld b,(hl)		;67fb	46		F
	inc hl			;67fc	23		#
	ld c,(hl)		;67fd	4e		N
	inc hl			;67fe	23		#
	ld (0687bh),hl		;67ff	22 7b 68	" { h
	add hl,bc		;6802	09		.
	ld c,0fdh		;6803	0e fd		. .
	jp R_687D		;6805	c3 7d 68	. } h
l6808h:
	ld b,(hl)		;6808	46		F
	inc hl			;6809	23		#
l680ah:
	ld c,(hl)		;680a	4e		N
	inc hl			;680b	23		#
	push hl			;680c	e5		.
	add hl,bc		;680d	09		.
	ld c,0fdh		;680e	0e fd		. .
	call R_687D		;6810	cd 7d 68	. } h
	pop hl			;6813	e1		.
	ld (0686eh),hl		;6814	22 6e 68	" n h
	ret			;6817	c9		.
l6818h:
	jr z,l6808h		;6818	28 ee		( .
	cp 0e0h			;681a	fe e0		. .
	jr c,l682ah		;681c	38 0c		8 .
	ld b,a			;681e	47		G
	cp 0ffh			;681f	fe ff		. .
	jr z,l680ah		;6821	28 e7		( .
	ld (0686eh),hl		;6823	22 6e 68	" n h
l6826h:
	ld (CH6+1),a		;6826	32 69 68	2 i h
	ret			;6829	c9		.
l682ah:
	add a,030h		;682a	c6 30		. 0
	jr z,l67f6h		;682c	28 c8		( .
	add a,0afh		;682e	c6 af		. .
	ld (0682fh),a		;6830	32 2f 68	2 / h
	ld (0686eh),hl		;6833	22 6e 68	" n h
	ld b,d			;6836	42		B
	ld hl,023a6h		;6837	21 a6 23	! . #
l683ah:
	defb 0edh,070h ;in f,(c)	;683a	ed 70		. p
	jp m,l683ah		;683c	fa 3a 68	. : h
	out (c),l		;683f	ed 69		. i
l6841h:
	defb 0edh,070h ;in f,(c)	;6841	ed 70		. p
	jp m,l6841h		;6843	fa 41 68	. A h
	ld b,e			;6846	43		C
	out (c),h		;6847	ed 61		. a
	ld b,d			;6849	42		B
	ld l,0a2h		;684a	2e a2		. .
l684ch:
	defb 0edh,070h ;in f,(c)	;684c	ed 70		. p
	jp m,l684ch		;684e	fa 4c 68	. L h
	out (c),l		;6851	ed 69		. i
l6853h:
	defb 0edh,070h ;in f,(c)	;6853	ed 70		. p
	jp m,l6853h		;6855	fa 53 68	. S h
	ld b,e			;6858	43		C
	out (c),a		;6859	ed 79		. y
	ret			;685b	c9		.
l685ch:
	ld (l6862h+1),hl	;685c	22 63 68	" c h
	jp R_687D		;685f	c3 7d 68	. } h
l6862h:
	ld hl,00000h		;6862	21 00 00	! . .
	jp R_687D		;6865	c3 7d 68	. } h
; --------------------------------------------------------------------------
; channel stub 6 - chip 1 channel 3
; --------------------------------------------------------------------------
CH6:
	ld a,0ffh		;6868	3e ff		> .
	inc a			;686a	3c		<
	jr nz,l6826h		;686b	20 b9		  .
	ld hl,00000h		;686d	21 00 00	! . .
	or 000h			;6870	f6 00		. .
	jr z,R_687D		;6872	28 09		( .
	dec a			;6874	3d		=
	ld (06871h),a		;6875	32 71 68	2 q h
	jr nz,R_687D		;6878	20 03		  .
	ld hl,00000h		;687a	21 00 00	! . .
R_687D:
	ld a,(hl)		;687d	7e		~
	inc hl			;687e	23		#
	cp 07eh			;687f	fe 7e		. ~
	jr z,l685ch		;6881	28 d9		( .
	cp 07fh			;6883	fe 7f		. .
	jr z,l6862h		;6885	28 db		( .
	cp e			;6887	bb		.
	jr nc,l6818h		;6888	30 8e		0 .
	jp p,l68a3h		;688a	f2 a3 68	. . h
	ex af,af'		;688d	08		.
	ld b,d			;688e	42		B
	ld a,028h		;688f	3e 28		> (
l6891h:
	defb 0edh,070h ;in f,(c)	;6891	ed 70		. p
	jp m,l6891h		;6893	fa 91 68	. . h
	out (c),a		;6896	ed 79		. y
	ld a,002h		;6898	3e 02		> .
l689ah:
	defb 0edh,070h ;in f,(c)	;689a	ed 70		. p
	jp m,l689ah		;689c	fa 9a 68	. . h
	ld b,e			;689f	43		C
	out (c),a		;68a0	ed 79		. y
	ex af,af'		;68a2	08		.
l68a3h:
	or a			;68a3	b7		.
	push af			;68a4	f5		.
	rra			;68a5	1f		.
	jr nc,l68d8h		;68a6	30 30		0 0
	ex af,af'		;68a8	08		.
	ld b,d			;68a9	42		B
	ld a,0a6h		;68aa	3e a6		> .
l68ach:
	defb 0edh,070h ;in f,(c)	;68ac	ed 70		. p
	jp m,l68ach		;68ae	fa ac 68	. . h
	out (c),a		;68b1	ed 79		. y
	ld a,(hl)		;68b3	7e		~
	inc hl			;68b4	23		#
	ld (06839h),a		;68b5	32 39 68	2 9 h
l68b8h:
	defb 0edh,070h ;in f,(c)	;68b8	ed 70		. p
	jp m,l68b8h		;68ba	fa b8 68	. . h
	ld b,e			;68bd	43		C
	out (c),a		;68be	ed 79		. y
	ld b,d			;68c0	42		B
	ld a,0a2h		;68c1	3e a2		> .
l68c3h:
	defb 0edh,070h ;in f,(c)	;68c3	ed 70		. p
	jp m,l68c3h		;68c5	fa c3 68	. . h
	out (c),a		;68c8	ed 79		. y
	ld a,(hl)		;68ca	7e		~
	inc hl			;68cb	23		#
	ld (0682fh),a		;68cc	32 2f 68	2 / h
l68cfh:
	defb 0edh,070h ;in f,(c)	;68cf	ed 70		. p
	jp m,l68cfh		;68d1	fa cf 68	. . h
	ld b,e			;68d4	43		C
	out (c),a		;68d5	ed 79		. y
	ex af,af'		;68d7	08		.
l68d8h:
	and 01fh		;68d8	e6 1f		. .
	call nz,OUTI_PAIRS	;68da	c4 df 62	. . b
	ld (0686eh),hl		;68dd	22 6e 68	" n h
	pop af			;68e0	f1		.
	ret p			;68e1	f0		.
	ld b,d			;68e2	42		B
	ld a,028h		;68e3	3e 28		> (
l68e5h:
	defb 0edh,070h ;in f,(c)	;68e5	ed 70		. p
	jp m,l68e5h		;68e7	fa e5 68	. . h
	out (c),a		;68ea	ed 79		. y
	ld a,0f2h		;68ec	3e f2		> .
l68eeh:
	defb 0edh,070h ;in f,(c)	;68ee	ed 70		. p
	jp m,l68eeh		;68f0	fa ee 68	. . h
	ld b,e			;68f3	43		C
	out (c),a		;68f4	ed 79		. y
	ret			;68f6	c9		.
; --------------------------------------------------------------------------
; DATA - six defw: 636B 646B 656C 666D 676D 686E
; addresses of the six channel stubs' LD HL,nnnn pattern-pointer
; slots. INIT walks this table and stores the relocated (absolute)
; tune pattern pointers there. The block is position-dependent:
; the player MUST be loaded at 0x61A8.
; --------------------------------------------------------------------------
RELOC_TABLE:
; DATA - six defw: 636B 646B 656C 666D 676D 686E
; addresses of the six channel stubs' LD HL,nnnn pattern-pointer
; slots. INIT walks this table and stores the relocated (absolute)
; tune pattern pointers there. The block is position-dependent:
; the player MUST be loaded at 0x61A8.
; --------------------------------------------------------------------------
; DATA 0x68F7-0x6902  RELOC_TABLE - six pattern-pointer slot addresses (defw)
; --------------------------------------------------------------------------
DATA_68F7:
	defw 0x636B,0x646B,0x656C,0x666D,0x676D,0x686E

