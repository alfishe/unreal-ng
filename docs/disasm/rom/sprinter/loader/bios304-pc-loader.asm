;===========================================================================
; Peters Plus Sprinter Sp2000 - BIOS 3.04: the PLD configuration loader
; ROM page #C (file offset #30000), the first 256 bytes; the CPU runs it
; at #0000 after power-on, when the ACEX PLD is still empty.
;===========================================================================
;
; Before the PLD is configured only the small EPM7064 CPLD works: it shows
; the ROM to the CPU and turns every CPU memory write in the configuration
; window into one configuration clock (DCLK) with data bit D0 (BIOS-TT
; 0271ac3 src/altera/max/SP2_MAX.TDF). The loader therefore writes each
; bitstream byte 8 times, rotating it right between the writes (bit 0
; first). The bitstream is ROM #0100-#E84E (59 215 bytes, identical to
; BIOS-PP ALTERA/SP2K_304.BIN); the CPU sees ROM pages #C-#F at
; #0000-#FFFF here.
;
; Exact count (tdd-ports-memory.md section 6): 59 215 bytes x 8 writes =
; 473 720 memory writes, no stack or other memory writes before them (no
; CALL / PUSH). The loop at #0088 never ends by itself: it keeps writing
; (from #E84F on: #FF filler bytes) until the configured PLD resets the
; CPU. To be confirmed by a runtime trace (MAME or S1).
;
; Compare: BIOS-TT bios/loader/loader.asm (same 8-write loop; it adds a
; packed-bitstream header check that 3.04 does not have).
;===========================================================================

	org 00000h

l0100h:	equ 0x0100
;---------------------------------------------------------------------------
; Z84C15 system control registers #EE/#EF (wait states, chip-select
; boundary #FE: #FE00-#FFFF is the fast RAM), SIO A (#19 <- #05, #62)
; and PIO (#1D <- #CF, #00; port A #1C <- #EA).
;---------------------------------------------------------------------------

LoaderStart:
	di			;0000	f3		.
	ld bc,0ffeeh		;0001	01 ee ff	. . .
	xor a			;0004	af		.
	out (c),a		;0005	ed 79		. y
	inc c			;0007	0c		.
	ld a,004h		;0008	3e 04		> .
	out (c),a		;000a	ed 79		. y
	dec c			;000c	0d		.
	dec a			;000d	3d		=
	out (c),a		;000e	ed 79		. y
	inc c			;0010	0c		.
	out (c),a		;0011	ed 79		. y
	dec c			;0013	0d		.
	dec a			;0014	3d		=
	out (c),a		;0015	ed 79		. y
	inc c			;0017	0c		.
	ld a,0feh		;0018	3e fe		> .
	out (c),a		;001a	ed 79		. y
	ld c,019h		;001c	0e 19		. .
	ld a,005h		;001e	3e 05		> .
	out (c),a		;0020	ed 79		. y
	ld a,062h		;0022	3e 62		> b
	out (c),a		;0024	ed 79		. y
	ld c,01dh		;0026	0e 1d		. .
	ld a,0cfh		;0028	3e cf		> .
	out (c),a		;002a	ed 79		. y
	xor a			;002c	af		.
	out (c),a		;002d	ed 79		. y
	ld c,01ch		;002f	0e 1c		. .
	ld a,0eah		;0031	3e ea		> .
	out (c),a		;0033	ed 79		. y
	jr CheckReloadRequest	;0035	18 04		. .

; BLOCK 'Fill0037' (start 0x0037 end 0x0038)
Fill0037:
	defb 0ffh		;0037	ff		.
IntRestart:
	jp LoaderStart		;0038	c3 00 00	. . .
;---------------------------------------------------------------------------
; Reload request: if fast RAM #FEF0-#FEFF holds "ACEX_30K_LOADING" (the
; BIOS puts it there with a new bitstream at #1000 before a reset), the
; chip-select boundary becomes #F0 and the stream starts at RAM #1000
; (HL = #1000). Otherwise the stream is the ROM bitstream at #0100.
;---------------------------------------------------------------------------
CheckReloadRequest:
	ld hl,0fef0h		;003b	21 f0 fe	! . .
	ld de,ReloadString		;003e	11 9e 00	. . .
l0041h:
	ld a,(de)		;0041	1a		.
	cp (hl)			;0042	be		.
	jr nz,StreamFromRom	;0043	20 24		  $
	inc e			;0045	1c		.
	inc l			;0046	2c		,
	jr nz,l0041h		;0047	20 f8		  .
	ld bc,0ffeeh		;0049	01 ee ff	. . .
	ld a,002h		;004c	3e 02		> .
	out (c),a		;004e	ed 79		. y
	inc c			;0050	0c		.
	ld a,0f0h		;0051	3e f0		> .
	out (c),a		;0053	ed 79		. y
	ld c,01ch		;0055	0e 1c		. .
	ld a,062h		;0057	3e 62		> b
	out (c),a		;0059	ed 79		. y
	ld hl,01000h		;005b	21 00 10	! . .
	jr PickDestination	;005e	18 0c		. .

; BLOCK 'Fill0060' (start 0x0060 end 0x0066)
Fill0060:
	defb 0ffh		;0060	ff		.
	defb 0ffh		;0061	ff		.
	defb 0ffh		;0062	ff		.
	defb 0ffh		;0063	ff		.
	defb 0ffh		;0064	ff		.
	defb 0ffh		;0065	ff		.
NmiRestart:
	jp LoaderStart		;0066	c3 00 00	. . .
StreamFromRom:
	ld hl,l0100h	;0069	21 00 01	! . .
;---------------------------------------------------------------------------
; Destination of the writes: DE = #FE00 (#FD00 when #FEE0 holds "IM").
; Only E is incremented, so the writes cycle through #FE00-#FEFF.
; IY = #0107 and IX = #FFFD are handed to the BIOS (page 8 #02B3 checks
; IY = #0107 and stores IX at #C13E).
;---------------------------------------------------------------------------
PickDestination:
	ld de,0fe00h		;006c	11 00 fe	. . .
	ld a,(0fee0h)		;006f	3a e0 fe	: . .
	cp 049h			;0072	fe 49		. I
	jr nz,l007eh		;0074	20 08		  .
	ld a,(0fee1h)		;0076	3a e1 fe	: . .
	cp 04dh			;0079	fe 4d		. M
	jr nz,l007eh		;007b	20 01		  .
	dec d			;007d	15		.
l007eh:
	ld iy,00107h		;007e	fd 21 07 01	. ! . .
	ld ix,0fffdh		;0082	dd 21 fd ff	. ! . .
	jr StreamLoop		;0086	18 00		. .
;---------------------------------------------------------------------------
; StreamLoop: per bitstream byte 8 writes of A to (DE), RRCA between them,
; so bit 0 goes first. Worked example: byte #A5 = 1010 0101 -> D0 of the
; eight writes = 1, 0, 1, 0, 0, 1, 0, 1. The loop has no exit: the
; configured PLD (CONF_DONE) resets the CPU after the last bit.
;---------------------------------------------------------------------------
StreamLoop:
	ld a,(hl)		;0088	7e		~
	ld (de),a		;0089	12		.
	rrca			;008a	0f		.
	ld (de),a		;008b	12		.
	rrca			;008c	0f		.
	ld (de),a		;008d	12		.
	rrca			;008e	0f		.
	ld (de),a		;008f	12		.
	rrca			;0090	0f		.
	ld (de),a		;0091	12		.
	rrca			;0092	0f		.
	ld (de),a		;0093	12		.
	rrca			;0094	0f		.
	ld (de),a		;0095	12		.
	rrca			;0096	0f		.
	ld (de),a		;0097	12		.
	inc e			;0098	1c		.
	inc hl			;0099	23		#
	jr StreamLoop		;009a	18 ec		. .
Halt:

; BLOCK 'Halt' (start 0x009c end 0x009e)
	defb 0f3h		;009c	f3		.
	defb 076h		;009d	76		v
ReloadString:

; BLOCK 'ReloadString' (start 0x009e end 0x00ae)
	defb 041h		;009e	41		A
	defb 043h		;009f	43		C
	defb 045h		;00a0	45		E
	defb 058h		;00a1	58		X
	defb 05fh		;00a2	5f		_
	defb 033h		;00a3	33		3
	defb 030h		;00a4	30		0
	defb 04bh		;00a5	4b		K
	defb 05fh		;00a6	5f		_
	defb 04ch		;00a7	4c		L
	defb 04fh		;00a8	4f		O
	defb 041h		;00a9	41		A
	defb 044h		;00aa	44		D
	defb 049h		;00ab	49		I
	defb 04eh		;00ac	4e		N
	defb 047h		;00ad	47		G
l00aeh:

; BLOCK 'PostCodeTable' (start 0x00ae end 0x00be)
PostCodeTable:
	defb 028h		;00ae	28		(
	defb 0bdh		;00af	bd		.
	defb 032h		;00b0	32		2
	defb 034h		;00b1	34		4
	defb 0a5h		;00b2	a5		.
	defb 064h		;00b3	64		d
	defb 060h		;00b4	60		`
	defb 03dh		;00b5	3d		=
	defb 020h		;00b6	20		 
	defb 024h		;00b7	24		$
	defb 021h		;00b8	21		!
	defb 0e0h		;00b9	e0		.
	defb 06ah		;00ba	6a		j
	defb 0b0h		;00bb	b0		.
	defb 062h		;00bc	62		b
	defb 063h		;00bd	63		c
l00beh:

; BLOCK 'Fill00BE' (start 0x00be end 0x0100)
Fill00BE:
	defb 0ffh		;00be	ff		.
	defb 0feh		;00bf	fe		.
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
	defb 0ffh		;00ff	ff		.
l0100h:
